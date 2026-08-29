/*
 * SPDX-FileCopyrightText: 2026 PaperMono Home Assistant contributors
 * SPDX-License-Identifier: MIT
 */
#include "papermono_activity.h"

#include <cmath>
#include <cstring>

#include "esphome/components/api/api_server.h"
#include "esphome/components/globals/globals_component.h"
#include "esphome/components/m5ioe1/m5ioe1.h"
#include "esphome/components/m5pm1/m5pm1.h"
#include "esphome/components/papermono_epaper/papermono_epaper.h"
#include "esphome/components/papermono_rtc/papermono_rtc.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/wifi/wifi_component.h"
#include "esphome/core/log.h"

#include "esp_sleep.h"

namespace esphome::papermono_activity {

static const char *const TAG = "papermono_activity";

static constexpr uint8_t SCREENSAVER_FULL_EVERY = 0;
static constexpr uint8_t CONTROLS_FULL_EVERY = 15;
static constexpr uint8_t PICKUP_FULL_THRESHOLD = 8;
static constexpr uint8_t CONTROLS_ENTER_FULL_THRESHOLD = 8;
static constexpr uint8_t CONTROLS_EXIT_FULL_THRESHOLD = 10;
static constexpr uint32_t HA_STATE_SETTLE_MS = 1750;
static constexpr uint32_t PERIODIC_WAKE_RECOVERY_TIMEOUT_MS = 30000;
static constexpr uint32_t GPIO_BLOCK_LOG_INTERVAL_MS = 10000;
static constexpr uint32_t SHUTDOWN_SETTLE_MS = 100;
static constexpr uint32_t PMIC_M5IOE1_PROBE_INTERVAL_MS = 75;
static constexpr uint32_t PMIC_M5IOE1_PROBE_TIMEOUT_MS = 1500;

static const char *wakeup_cause_to_string_(esp_sleep_wakeup_cause_t cause) {
  switch (cause) {
    case ESP_SLEEP_WAKEUP_TIMER:
      return "TIMER";
    case ESP_SLEEP_WAKEUP_GPIO:
      return "GPIO";
    case ESP_SLEEP_WAKEUP_EXT1:
      return "EXT1";
    default:
      return "OTHER";
  }
}

static bool arm_result_ok_(const m5pm1::LightSleepWakeupArmResult &arm) {
  if (arm.timer != ESP_OK) {
    return false;
  }
  if (arm.ext1 != ESP_OK) {
    return false;
  }
  return true;
}

void PaperMonoActivityComponent::setup() {
  if (this->pmu_ == nullptr) {
    ESP_LOGE(TAG, "M5PM1 reference missing");
    return;
  }

  this->pmu_->set_frontlight_level(0);
  this->frontlight_on_ = false;
  this->activity_active_ = false;
  this->pickup_cleanup_pending_ = false;
  this->light_sleep_pending_ = false;
  this->shutdown_phase_ = ShutdownPhase::NONE;
  this->periodic_wake_phase_ = PeriodicWakePhase::NONE;
  this->last_activity_ms_ = 0;
  this->sleep_eligible_activity_ms_ = 0;
  this->shutdown_eligible_activity_ms_ = 0;
  this->periodic_wake_activity_ms_ = 0;
  this->periodic_wake_settle_start_ms_ = 0;
  this->periodic_wake_started_ms_ = 0;
  this->last_periodic_bucket_ = UINT32_MAX;
  this->last_gpio_block_log_ms_ = 0;
  this->periodic_wake_recovery_timeout_logged_ = false;
  this->light_sleep_timer_reason_ = LightSleepTimerReason::NORMAL_REFRESH;

  ESP_LOGI(TAG, "Activity setup: pmic_shutdown=%s", this->pmu_->is_boot_from_pmic_shutdown() ? "yes" : "no");
  if (this->pmu_->is_boot_from_pmic_shutdown() && this->display_ != nullptr) {
    this->display_->arm_pmic_initial_full();
  }

  if (this->quiet_hours_sleep_display_ != nullptr) {
    this->quiet_hours_sleep_display_->value() = false;
  }
  if (this->quiet_hours_user_override_ != nullptr) {
    this->quiet_hours_user_override_->value() = false;
  }

  this->handle_boot_wake_source_();
  this->request_screensaver_refresh_policy_();
  ESP_LOGI(TAG, "Frontlight init: OFF (boot default)");
  ESP_LOGI(TAG, "Activity timeout: %u ms, on level: %u%%", this->timeout_ms_, this->on_brightness_percent_);
  ESP_LOGI(TAG, "Screensaver refresh interval: %u min (clock-aligned)", this->screensaver_refresh_minutes_);
  ESP_LOGI(TAG, "Quiet hours: %s -> %s", this->quiet_hours_start_.c_str(), this->quiet_hours_end_.c_str());
}

void PaperMonoActivityComponent::loop() {
  if (this->pmic_hw_recovery_phase_ != PmicHwRecoveryPhase::NONE &&
      this->pmic_hw_recovery_phase_ != PmicHwRecoveryPhase::DONE) {
    this->process_pmic_hw_recovery_();
    return;
  }

  if (this->pickup_cleanup_pending_ && this->display_ != nullptr && this->display_->is_idle()) {
    this->pickup_cleanup_pending_ = false;
  }

  if (this->shutdown_phase_ != ShutdownPhase::NONE) {
    this->process_shutdown_pending_();
    return;
  }

  if (this->periodic_wake_phase_ != PeriodicWakePhase::NONE) {
    this->process_periodic_wake_recovery_();
  } else if (this->pmic_ha_final_full_pending_ && this->display_ != nullptr &&
             this->display_->is_pmic_mandatory_full_done()) {
    this->begin_pmic_ha_final_full_recovery_();
  } else if (this->light_sleep_wake_recovery_ != nullptr && this->light_sleep_wake_recovery_->value() &&
             this->is_network_api_ready_() && !this->pmic_ha_final_full_pending_) {
    this->light_sleep_wake_recovery_->value() = false;
  }

  if (this->light_sleep_pending_ && this->can_enter_light_sleep_()) {
    this->enter_light_sleep_();
  }

  if (!this->activity_active_ || this->timeout_ms_ == 0) {
    return;
  }

  const uint32_t now = millis();
  if (now - this->last_activity_ms_ >= this->timeout_ms_) {
    this->turn_off_timeout_();
  }
}

void PaperMonoActivityComponent::cancel_shutdown_() {
  this->shutdown_phase_ = ShutdownPhase::NONE;
  if (this->quiet_hours_sleep_display_ != nullptr) {
    this->quiet_hours_sleep_display_->value() = false;
  }
}

void PaperMonoActivityComponent::cancel_periodic_wake_recovery_() {
  this->periodic_wake_phase_ = PeriodicWakePhase::NONE;
  this->periodic_wake_settle_start_ms_ = 0;
  this->periodic_wake_started_ms_ = 0;
  this->clear_wake_recovery_flag_();
}

void PaperMonoActivityComponent::clear_wake_recovery_flag_() {
  if (this->light_sleep_wake_recovery_ != nullptr) {
    this->light_sleep_wake_recovery_->value() = false;
  }
}

void PaperMonoActivityComponent::cancel_light_sleep_() {
  this->light_sleep_pending_ = false;
  this->periodic_wake_phase_ = PeriodicWakePhase::NONE;
  this->periodic_wake_settle_start_ms_ = 0;
}

bool PaperMonoActivityComponent::in_controls_view_() const {
  return this->controls_view_ != nullptr && this->controls_view_->value();
}

bool PaperMonoActivityComponent::is_network_api_ready_() const {
#ifdef USE_WIFI
  if (wifi::global_wifi_component == nullptr || !wifi::global_wifi_component->is_connected()) {
    return false;
  }
#endif
#ifdef USE_API
  if (api::global_api_server == nullptr || !api::global_api_server->is_connected_with_state_subscription()) {
    return false;
  }
#endif
  return true;
}

void PaperMonoActivityComponent::request_screensaver_refresh_policy_() {
  if (this->display_ == nullptr) {
    return;
  }
  this->display_->set_full_update_every(SCREENSAVER_FULL_EVERY);
}

void PaperMonoActivityComponent::request_controls_refresh_policy_() {
  if (this->display_ == nullptr) {
    return;
  }
  this->display_->set_full_update_every(CONTROLS_FULL_EVERY);
}

void PaperMonoActivityComponent::on_pickup_transition_() {
  if (this->display_ == nullptr) {
    return;
  }
  if (this->in_controls_view_()) {
    return;
  }

  const uint8_t count = this->display_->get_partial_count();
  if (count >= PICKUP_FULL_THRESHOLD) {
    ESP_LOGI(TAG, "Pickup: partial_count=%u -> FULL cleanup", count);
    this->pickup_cleanup_pending_ = true;
    this->display_->update();
    return;
  }

  ESP_LOGI(TAG, "Pickup: partial_count=%u -> no refresh", count);
}

void PaperMonoActivityComponent::enter_controls() {
  this->cancel_light_sleep_();
  this->cancel_shutdown_();
  this->clear_wake_recovery_flag_();

  if (this->display_ == nullptr) {
    return;
  }

  this->request_controls_refresh_policy_();

  if (this->pickup_cleanup_pending_) {
    ESP_LOGI(TAG, "Pickup cleanup already pending -> Controls PARTIAL only");
    this->display_->update_partial(0, 0, 480, 800);
    return;
  }

  const uint8_t count = this->display_->get_partial_count();
  if (count >= CONTROLS_ENTER_FULL_THRESHOLD) {
    ESP_LOGI(TAG, "Enter controls: partial_count=%u -> FULL", count);
    this->display_->update();
    return;
  }

  ESP_LOGI(TAG, "Enter controls: partial_count=%u -> PARTIAL", count);
  this->display_->update_partial(0, 0, 480, 800);
}

void PaperMonoActivityComponent::exit_controls() {
  this->cancel_light_sleep_();
  this->cancel_shutdown_();
  this->clear_wake_recovery_flag_();

  if (this->display_ == nullptr) {
    return;
  }

  const uint8_t count = this->display_->get_partial_count();
  if (count >= CONTROLS_EXIT_FULL_THRESHOLD) {
    ESP_LOGI(TAG, "Exit controls: partial_count=%u -> FULL cleanup", count);
    this->display_->update();
  } else {
    ESP_LOGI(TAG, "Exit controls: partial_count=%u -> PARTIAL", count);
    this->display_->update_partial(0, 0, 480, 800);
  }

  this->request_screensaver_refresh_policy_();
}

uint32_t PaperMonoActivityComponent::current_time_bucket_() const {
  if (this->time_ == nullptr || this->screensaver_refresh_minutes_ == 0) {
    return UINT32_MAX;
  }
  const ESPTime now = this->time_->now();
  if (!now.is_valid()) {
    return UINT32_MAX;
  }
  const uint32_t day_minutes = static_cast<uint32_t>(now.hour * 60U + now.minute);
  return day_minutes / this->screensaver_refresh_minutes_;
}

bool PaperMonoActivityComponent::parse_quiet_time_(const std::string &value, int *minutes_out) const {
  if (minutes_out == nullptr || value.size() != 5 || value[2] != ':') {
    return false;
  }
  if (value[0] < '0' || value[0] > '9' || value[1] < '0' || value[1] > '9' || value[3] < '0' || value[3] > '9' ||
      value[4] < '0' || value[4] > '9') {
    return false;
  }
  const int hour = (value[0] - '0') * 10 + (value[1] - '0');
  const int minute = (value[3] - '0') * 10 + (value[4] - '0');
  if (hour > 23 || minute > 59) {
    return false;
  }
  *minutes_out = hour * 60 + minute;
  return true;
}

bool PaperMonoActivityComponent::is_in_quiet_hours_() const {
  int quiet_start = -1;
  int quiet_end = -1;
  if (!this->parse_quiet_time_(this->quiet_hours_start_, &quiet_start) ||
      !this->parse_quiet_time_(this->quiet_hours_end_, &quiet_end)) {
    return false;
  }
  if (quiet_start == quiet_end) {
    return false;
  }

  if (this->time_ == nullptr) {
    return false;
  }
  const ESPTime now = this->time_->now();
  if (!now.is_valid()) {
    return false;
  }
  const int current_minutes = now.hour * 60 + now.minute;
  if (quiet_start < quiet_end) {
    return current_minutes >= quiet_start && current_minutes < quiet_end;
  }
  return current_minutes >= quiet_start || current_minutes < quiet_end;
}

uint32_t PaperMonoActivityComponent::seconds_until_quiet_hours_start_() const {
  int quiet_start = -1;
  int quiet_end = -1;
  if (!this->parse_quiet_time_(this->quiet_hours_start_, &quiet_start) ||
      !this->parse_quiet_time_(this->quiet_hours_end_, &quiet_end) || this->time_ == nullptr) {
    return UINT32_MAX;
  }
  const ESPTime now = this->time_->now();
  if (!now.is_valid()) {
    return UINT32_MAX;
  }

  const uint32_t seconds_into_day = static_cast<uint32_t>(now.hour * 3600U + now.minute * 60U + now.second);
  const uint32_t quiet_start_seconds = static_cast<uint32_t>(quiet_start) * 60U;
  uint32_t delta = 0;
  if (seconds_into_day < quiet_start_seconds) {
    delta = quiet_start_seconds - seconds_into_day;
  } else {
    delta = (24U * 3600U) - seconds_into_day + quiet_start_seconds;
  }
  if (this->is_in_quiet_hours_() && quiet_start < quiet_end) {
    return 0;
  }
  return delta;
}

uint32_t PaperMonoActivityComponent::seconds_until_quiet_hours_end_() const {
  int quiet_start = -1;
  int quiet_end = -1;
  if (!this->parse_quiet_time_(this->quiet_hours_start_, &quiet_start) ||
      !this->parse_quiet_time_(this->quiet_hours_end_, &quiet_end) || this->time_ == nullptr) {
    return 0;
  }
  const ESPTime now = this->time_->now();
  if (!now.is_valid()) {
    return 0;
  }

  const uint32_t seconds_into_day = static_cast<uint32_t>(now.hour * 3600U + now.minute * 60U + now.second);
  const uint32_t quiet_end_seconds = static_cast<uint32_t>(quiet_end) * 60U;
  if (!this->is_in_quiet_hours_()) {
    return 0;
  }
  if (quiet_start < quiet_end) {
    if (seconds_into_day < quiet_end_seconds) {
      return quiet_end_seconds - seconds_into_day;
    }
    return 0;
  }
  if (seconds_into_day >= static_cast<uint32_t>(quiet_start) * 60U) {
    return (24U * 3600U) - seconds_into_day + quiet_end_seconds;
  }
  if (seconds_into_day < quiet_end_seconds) {
    return quiet_end_seconds - seconds_into_day;
  }
  return 0;
}

void PaperMonoActivityComponent::handle_boot_wake_source_() {
  const uint8_t wake_src = this->pmu_->get_boot_wake_src_raw();
  const uint8_t gpio_irq = this->pmu_->get_boot_irq_status1_raw();
  const uint8_t rtc_irq = this->rtc_ != nullptr ? this->rtc_->read_irq_flags_raw() : 0;
  ESP_LOGI(TAG, "Boot RX8130 IRQ=0x%02X", rtc_irq);

  m5pm1::Pm5BootWakeSource source = this->pmu_->get_boot_wake_source();
  if (wake_src & 0x20) {  // M5PM1_WAKE_SRC_EXT_WAKE
    if ((rtc_irq & 0x10) != 0) {
      source = m5pm1::Pm5BootWakeSource::RTC_GPIO0;
    } else {
      source = m5pm1::Pm5BootWakeSource::MOTION_GPIO4;
      if (gpio_irq & 0x10) {
        ESP_LOGD(TAG, "Boot IRQ_STATUS1 GPIO4 set (diagnostic); classified as MOTION via RX8130 TF");
      } else if (gpio_irq & 0x01) {
        ESP_LOGD(TAG, "Boot IRQ_STATUS1 GPIO0 set but RX8130 TF clear; classified as MOTION");
      }
    }
  } else if (this->pmu_->is_boot_from_pmic_shutdown() && (wake_src & 0x04)) {  // M5PM1_WAKE_SRC_PWRBTN
    source = m5pm1::Pm5BootWakeSource::POWER_BUTTON;
  } else if (source == m5pm1::Pm5BootWakeSource::UNKNOWN) {
    source = m5pm1::Pm5BootWakeSource::NORMAL;
  }

  this->pmu_->set_boot_wake_source(source);
  this->pmu_->log_pmic_boot_cause();

  if (!this->pmu_->is_boot_from_pmic_shutdown() && this->pmu_->is_boot_shutdown_pending()) {
    ESP_LOGI(TAG, "PMIC shutdown-pending marker cleared (boot not classified as shutdown recovery)");
    this->pmu_->clear_shutdown_pending();
  }

  switch (source) {
    case m5pm1::Pm5BootWakeSource::RTC_GPIO0:
      if (this->quiet_hours_user_override_ != nullptr) {
        this->quiet_hours_user_override_->value() = false;
      }
      break;
    case m5pm1::Pm5BootWakeSource::MOTION_GPIO4:
      if (this->is_in_quiet_hours_()) {
        ESP_LOGI(TAG, "Motion wake during quiet hours -> user override active");
        if (this->quiet_hours_user_override_ != nullptr) {
          this->quiet_hours_user_override_->value() = true;
        }
        this->pending_pmic_motion_activity_ = true;
      }
      break;
    case m5pm1::Pm5BootWakeSource::POWER_BUTTON:
      ESP_LOGI(TAG, "Power-button wake after PMIC shutdown -> user override active");
      if (this->quiet_hours_user_override_ != nullptr) {
        this->quiet_hours_user_override_->value() = true;
      }
      break;
    default:
      break;
  }

  if (this->rtc_ != nullptr) {
    this->rtc_->clear_irq();
  }

  this->pmu_->clear_wake_source(0xFF);
}

void PaperMonoActivityComponent::begin_pmic_wake_hardware_recovery(m5ioe1::M5IOE1Component *ioe) {
  if (this->pmu_ == nullptr || !this->pmu_->is_boot_from_pmic_shutdown()) {
    return;
  }
  if (this->pmic_hw_recovery_phase_ != PmicHwRecoveryPhase::NONE) {
    return;
  }
  this->pmic_ha_final_full_pending_ = true;
  if (this->light_sleep_wake_recovery_ != nullptr) {
    this->light_sleep_wake_recovery_->value() = true;
  }
  ESP_LOGI(TAG, "PMIC boot: waiting for HA state recovery");
  this->m5ioe1_ = ioe;
  this->pmic_hw_recovery_phase_ = PmicHwRecoveryPhase::WAIT_M5IOE1;
  this->pmic_hw_recovery_started_ms_ = millis();
  this->pmic_hw_recovery_next_probe_ms_ = millis();
  this->pmic_hw_recovery_wait_logged_ = false;
  if (this->pmu_ != nullptr) {
    this->pmu_->log_post_shutdown_power_readback();
    this->pmu_->clear_shutdown_pending();
  }
  if (this->display_ != nullptr) {
    this->display_->begin_pmic_wake_recovery(this->pmu_);
  }
}

void PaperMonoActivityComponent::process_pmic_hw_recovery_() {
  const uint32_t now = millis();

  if (this->pmic_hw_recovery_phase_ == PmicHwRecoveryPhase::WAIT_M5IOE1) {
    if (!this->pmic_hw_recovery_wait_logged_) {
      ESP_LOGI(TAG, "PMIC wake: waiting for M5IOE1");
      this->pmic_hw_recovery_wait_logged_ = true;
    }
    if (static_cast<int32_t>(now - this->pmic_hw_recovery_next_probe_ms_) < 0) {
      return;
    }

    const bool ready = this->m5ioe1_ != nullptr && this->m5ioe1_->probe_device_responding();
    if (!ready) {
      const uint32_t elapsed = now - this->pmic_hw_recovery_started_ms_;
      if (elapsed >= PMIC_M5IOE1_PROBE_TIMEOUT_MS) {
        ESP_LOGE(TAG, "PMIC wake: M5IOE1 unavailable after %u ms", elapsed);
        if (this->m5ioe1_ != nullptr) {
          this->m5ioe1_->log_recovery_diagnostic();
        }
        if (this->pmu_ != nullptr) {
          this->pmu_->log_post_shutdown_power_readback();
        }
        if (this->display_ != nullptr) {
          this->display_->fail_pmic_wake_recovery(this->pmu_, "M5IOE1 unavailable");
        }
        this->pmic_hw_recovery_phase_ = PmicHwRecoveryPhase::DONE;
        return;
      }
      this->pmic_hw_recovery_next_probe_ms_ = now + PMIC_M5IOE1_PROBE_INTERVAL_MS;
      return;
    }

    if (this->m5ioe1_ != nullptr) {
      this->m5ioe1_->log_recovery_diagnostic();
    }

    ESP_LOGI(TAG, "PMIC wake: M5IOE1 ready after %u ms", now - this->pmic_hw_recovery_started_ms_);
    this->pmic_hw_recovery_phase_ = PmicHwRecoveryPhase::EPD_RECOVERY;
  }

  if (this->pmic_hw_recovery_phase_ == PmicHwRecoveryPhase::EPD_RECOVERY) {
    if (this->display_ == nullptr ||
        !this->display_->run_pmic_wake_epd_hardware_recovery(this->m5ioe1_, this->pmu_)) {
      this->pmic_hw_recovery_phase_ = PmicHwRecoveryPhase::DONE;
      return;
    }
    this->pmic_hw_recovery_phase_ = PmicHwRecoveryPhase::FRONTLIGHT;
  }

  if (this->pmic_hw_recovery_phase_ == PmicHwRecoveryPhase::FRONTLIGHT) {
    if (this->pmu_ == nullptr || !this->pmu_->recover_frontlight_after_pmic_wake()) {
      this->pmic_hw_recovery_phase_ = PmicHwRecoveryPhase::DONE;
      return;
    }
    this->pmic_hw_recovery_phase_ = PmicHwRecoveryPhase::COMPLETE;
  }

  if (this->pmic_hw_recovery_phase_ == PmicHwRecoveryPhase::COMPLETE) {
    this->complete_pmic_wake_hardware_recovery();
    if (this->display_ != nullptr && this->display_->is_hw_ready_for_refresh()) {
      ESP_LOGI(TAG, "PMIC wake: requesting initial FULL refresh");
      this->display_->update_from_pmic_recovery();
    }
    this->pmic_hw_recovery_phase_ = PmicHwRecoveryPhase::DONE;
  }
}

void PaperMonoActivityComponent::begin_pmic_ha_final_full_recovery_() {
  if (!this->pmic_ha_final_full_pending_ || this->periodic_wake_phase_ != PeriodicWakePhase::NONE) {
    return;
  }
  this->periodic_wake_started_ms_ = millis();
  this->periodic_wake_recovery_timeout_logged_ = false;
  this->periodic_wake_phase_ = PeriodicWakePhase::WAIT_API;
  ESP_LOGI(TAG, "PMIC boot: mandatory FULL complete; awaiting HA API for final FULL");
}

void PaperMonoActivityComponent::complete_pmic_wake_hardware_recovery() {
  if (!this->pending_pmic_motion_activity_) {
    return;
  }
  this->pending_pmic_motion_activity_ = false;
  if (this->pmu_ != nullptr && this->pmu_->is_frontlight_recovery_failed()) {
    ESP_LOGW(TAG, "PMIC motion activity skipped: frontlight recovery failed");
    return;
  }
  this->report_activity(ActivitySource::MOTION);
  this->last_periodic_tick_activity_ms_ = this->last_activity_ms_;
}

void PaperMonoActivityComponent::sync_battery_display_for_shutdown_() {
  if (this->pmu_ == nullptr) {
    return;
  }
  this->pmu_->refresh_power_and_battery();
  const auto level = this->pmu_->get_last_battery_level_percent();
  if (!level.has_value() || this->battery_display_level_ == nullptr) {
    return;
  }

  float rounded = static_cast<float>(*level);
  if (!this->pmu_->is_external_power_present() && !std::isnan(this->battery_display_level_->value()) &&
      rounded > this->battery_display_level_->value()) {
    rounded = this->battery_display_level_->value();
  }
  this->battery_display_level_->value() = rounded;
}

void PaperMonoActivityComponent::request_quiet_hours_shutdown_refresh_() {
  if (this->quiet_hours_sleep_display_ != nullptr) {
    this->quiet_hours_sleep_display_->value() = true;
  }
  this->sync_battery_display_for_shutdown_();
  if (this->display_ != nullptr) {
    this->display_->update_partial(0, 0, 480, 800);
  }
  this->shutdown_eligible_activity_ms_ = this->last_activity_ms_;
  this->shutdown_phase_ = ShutdownPhase::WAIT_DISPLAY;
}

bool PaperMonoActivityComponent::can_begin_shutdown_() const {
  if (this->pmu_ == nullptr || this->rtc_ == nullptr || this->display_ == nullptr) {
    return false;
  }
  if (this->display_->is_pmic_recovery_failed()) {
    return false;
  }
  if (this->display_->is_pmic_recovery_pending()) {
    return false;
  }
  if (this->pmu_->is_frontlight_recovery_failed()) {
    return false;
  }
  if (!this->rtc_->is_ready()) {
    return false;
  }
  if (this->in_controls_view_()) {
    return false;
  }
  if (!this->display_->is_idle() || this->display_->has_refresh_pending()) {
    return false;
  }
  if (this->last_activity_ms_ != this->shutdown_eligible_activity_ms_) {
    return false;
  }
  return true;
}

bool PaperMonoActivityComponent::begin_quiet_hours_shutdown_() {
  if (!this->can_begin_shutdown_()) {
    if (this->last_activity_ms_ != this->shutdown_eligible_activity_ms_) {
      ESP_LOGI(TAG, "Quiet-hours shutdown aborted (user activity)");
      this->cancel_shutdown_();
    }
    return false;
  }

  const uint32_t sleep_seconds = this->seconds_until_quiet_hours_end_();
  if (sleep_seconds == 0) {
    ESP_LOGW(TAG, "Quiet-hours shutdown skipped: already past quiet_hours_end");
    this->cancel_shutdown_();
    return false;
  }

  if (this->pmu_->is_external_power_present()) {
    ESP_LOGW(TAG, "USB/external power present (PWR_SRC): shutdown keeps M5PM1+RTC+IMU alive; "
                  "ESP32 rail may re-power immediately while 5VIN is present");
  }

  this->rtc_->clear_irq();
  if (this->rtc_->is_timer_irq_active()) {
    ESP_LOGE(TAG, "Quiet-hours shutdown aborted: RTC IRQ line still active after clear");
    this->cancel_shutdown_();
    return false;
  }
  if (this->pmu_->is_gpio_input_low(0)) {
    ESP_LOGE(TAG, "Quiet-hours shutdown aborted: M5PM1 GPIO0 (RTC nIRQ) already LOW");
    this->cancel_shutdown_();
    return false;
  }

  const uint32_t rtc_ms = sleep_seconds * 1000U;
  const uint32_t programmed_ms = this->rtc_->set_timer_irq(rtc_ms);
  if (programmed_ms == 0) {
    ESP_LOGE(TAG, "Quiet-hours shutdown aborted: RTC timer program failed (%u s)", sleep_seconds);
    this->cancel_shutdown_();
    return false;
  }
  ESP_LOGI(TAG, "RTC timer armed for quiet_hours_end in %u s (programmed %u ms)", sleep_seconds, programmed_ms);

  if (!this->pmu_->configure_shutdown_wake_gpio0_falling()) {
    ESP_LOGE(TAG, "Quiet-hours shutdown aborted: M5PM1 GPIO0 wake config failed");
    this->rtc_->disable_irq();
    this->cancel_shutdown_();
    return false;
  }
  if (!this->pmu_->configure_shutdown_wake_gpio4_falling()) {
    ESP_LOGE(TAG, "Quiet-hours shutdown aborted: M5PM1 GPIO4 wake config failed");
    this->rtc_->disable_irq();
    this->cancel_shutdown_();
    return false;
  }
  if (!this->pmu_->set_ldo_enable(true)) {
    ESP_LOGE(TAG, "Quiet-hours shutdown aborted: LDO enable failed");
    this->rtc_->disable_irq();
    this->cancel_shutdown_();
    return false;
  }
  if (!this->pmu_->ldo_set_power_hold(true)) {
    ESP_LOGE(TAG, "Quiet-hours shutdown aborted: LDO power hold failed");
    this->rtc_->disable_irq();
    this->cancel_shutdown_();
    return false;
  }
  // Official PaperMono L1 shutdown sequence (M5Stack docs): setLedEnLevel(true) before shutdown.
  if (!this->pmu_->set_led_en_level(true)) {
    ESP_LOGE(TAG, "Quiet-hours shutdown aborted: LED_EN level set failed");
    this->rtc_->disable_irq();
    this->cancel_shutdown_();
    return false;
  }

  ESP_LOGI(TAG, "M5PM1 L1 hold active; entering PMIC shutdown");
  delay(SHUTDOWN_SETTLE_MS);
  if (!this->pmu_->execute_shutdown()) {
    ESP_LOGE(TAG, "Quiet-hours shutdown aborted: M5PM1 shutdown command failed");
    this->rtc_->disable_irq();
    this->cancel_shutdown_();
    return false;
  }

  while (true) {
    delay(1000);
  }
  return true;
}

void PaperMonoActivityComponent::process_shutdown_pending_() {
  if ((this->display_ != nullptr &&
       (this->display_->is_pmic_recovery_failed() || this->display_->is_pmic_recovery_pending())) ||
      (this->pmu_ != nullptr && this->pmu_->is_frontlight_recovery_failed())) {
    if (this->shutdown_phase_ != ShutdownPhase::NONE) {
      ESP_LOGW(TAG, "Quiet-hours shutdown cancelled: PMIC wake hardware recovery failed");
      this->cancel_shutdown_();
    }
    return;
  }
  if (this->last_activity_ms_ != this->shutdown_eligible_activity_ms_) {
    ESP_LOGI(TAG, "Quiet-hours shutdown cancelled (user activity during transition)");
    this->cancel_shutdown_();
    return;
  }
  if (this->in_controls_view_()) {
    ESP_LOGI(TAG, "Quiet-hours shutdown cancelled (controls active)");
    this->cancel_shutdown_();
    return;
  }
  if (this->display_ == nullptr || !this->display_->is_idle() || this->display_->has_refresh_pending()) {
    return;
  }
  this->begin_quiet_hours_shutdown_();
}

void PaperMonoActivityComponent::run_screensaver_periodic_tick_(bool quiet_sleep_display) {
  if (this->pmu_ != nullptr) {
    this->pmu_->refresh_power_and_battery();
  }

  const bool allow_partial = !this->in_controls_view_() &&
                             (this->ha_connection_state_ == nullptr || this->ha_connection_state_->value() != 0);
  ESP_LOGI(TAG, "Scheduler /%u: controls=%d ha_state=%d quiet_sleep=%d -> partial=%s",
           this->screensaver_refresh_minutes_, this->in_controls_view_(),
           this->ha_connection_state_ != nullptr ? this->ha_connection_state_->value() : -1, quiet_sleep_display,
           allow_partial ? "yes" : "no");

  if (!allow_partial || this->display_ == nullptr) {
    return;
  }
  if (this->display_->is_pmic_recovery_failed()) {
    ESP_LOGW(TAG, "Scheduler tick skipped: EPD recovery failed after PMIC wake");
    return;
  }
  if (this->display_->is_pmic_recovery_pending()) {
    ESP_LOGD(TAG, "Scheduler tick skipped: PMIC wake hardware recovery pending");
    return;
  }
  if (this->display_->is_pmic_mandatory_full_pending()) {
    ESP_LOGD(TAG, "Scheduler tick skipped: PMIC mandatory initial FULL pending");
    return;
  }
  if (this->pmu_ != nullptr && this->pmu_->is_frontlight_recovery_failed()) {
    ESP_LOGW(TAG, "Scheduler tick skipped: frontlight recovery failed after PMIC wake");
    return;
  }

  const uint32_t bucket = this->current_time_bucket_();
  if (bucket != UINT32_MAX && bucket == this->last_periodic_bucket_) {
    ESP_LOGD(TAG, "Skipping duplicate periodic tick for bucket %u", bucket);
    return;
  }
  if (bucket != UINT32_MAX) {
    this->last_periodic_bucket_ = bucket;
  }

  if (quiet_sleep_display) {
    this->request_quiet_hours_shutdown_refresh_();
    return;
  }

  this->display_->update_partial(0, 0, 480, 800);
  if (this->is_in_quiet_hours_()) {
    ESP_LOGD(TAG, "Quiet-hours override tick: staying awake until inactive aligned tick");
    return;
  }
  this->arm_light_sleep_after_refresh_();
}

void PaperMonoActivityComponent::on_screensaver_tick() {
  if (this->light_sleep_wake_recovery_ != nullptr && this->light_sleep_wake_recovery_->value()) {
    ESP_LOGD(TAG, "Scheduler tick skipped (light_sleep_wake_recovery active)");
    return;
  }

  const uint32_t activity_at_tick = this->last_activity_ms_;
  bool quiet_sleep_display = false;
  if (this->is_in_quiet_hours_()) {
    if (activity_at_tick != this->last_periodic_tick_activity_ms_) {
      quiet_sleep_display = false;
    } else {
      quiet_sleep_display = true;
    }
  }
  this->last_periodic_tick_activity_ms_ = activity_at_tick;

  this->run_screensaver_periodic_tick_(quiet_sleep_display);
}

void PaperMonoActivityComponent::arm_light_sleep_after_refresh_() {
  this->sleep_eligible_activity_ms_ = this->last_activity_ms_;
  this->light_sleep_pending_ = true;
}

void PaperMonoActivityComponent::process_periodic_wake_recovery_() {
  if (!this->pmic_ha_final_full_pending_) {
    if (this->last_activity_ms_ != this->periodic_wake_activity_ms_) {
      ESP_LOGI(TAG, "Periodic wake recovery cancelled (user activity)");
      this->cancel_light_sleep_();
      this->clear_wake_recovery_flag_();
      return;
    }

    if (this->in_controls_view_()) {
      ESP_LOGI(TAG, "Periodic wake recovery cancelled (controls active)");
      this->cancel_light_sleep_();
      this->clear_wake_recovery_flag_();
      return;
    }
  }

  if (!this->is_network_api_ready_()) {
    if (!this->pmic_ha_final_full_pending_) {
      const uint32_t elapsed = millis() - this->periodic_wake_started_ms_;
      if (this->periodic_wake_started_ms_ != 0 && elapsed >= PERIODIC_WAKE_RECOVERY_TIMEOUT_MS) {
        if (!this->periodic_wake_recovery_timeout_logged_) {
          ESP_LOGI(TAG, "Periodic wake: network/API timeout, returning to sleep");
          this->periodic_wake_recovery_timeout_logged_ = true;
        }
        this->periodic_wake_phase_ = PeriodicWakePhase::NONE;
        this->periodic_wake_settle_start_ms_ = 0;
        this->periodic_wake_started_ms_ = 0;
        this->clear_wake_recovery_flag_();
        this->sleep_eligible_activity_ms_ = this->last_activity_ms_;
        this->light_sleep_pending_ = true;
        if (this->can_enter_light_sleep_()) {
          this->enter_light_sleep_();
        }
      }
    }
    return;
  }

  this->periodic_wake_recovery_timeout_logged_ = false;

  if (this->periodic_wake_phase_ == PeriodicWakePhase::WAIT_API) {
    if (this->pmic_ha_final_full_pending_) {
      ESP_LOGI(TAG, "PMIC boot: HA API connected, settling state subscriptions");
    } else {
      ESP_LOGI(TAG, "Periodic wake: API ready, settling HA states");
    }
    this->periodic_wake_settle_start_ms_ = millis();
    this->periodic_wake_phase_ = PeriodicWakePhase::SETTLE;
    return;
  }

  if (this->periodic_wake_phase_ == PeriodicWakePhase::SETTLE) {
    const uint32_t elapsed = millis() - this->periodic_wake_settle_start_ms_;
    if (elapsed < HA_STATE_SETTLE_MS) {
      return;
    }

    if (this->pmu_ != nullptr) {
      this->pmu_->refresh_power_and_battery();
    }

    if (this->pmic_ha_final_full_pending_) {
      ESP_LOGI(TAG, "PMIC boot: HA settle complete (%u ms); starting final FULL", elapsed);
      if (this->ha_connection_state_ != nullptr && this->is_network_api_ready_()) {
        this->ha_connection_state_->value() = 1;
      }
      if (this->display_ != nullptr) {
        ESP_LOGI(TAG, "PMIC boot: final HA FULL requested");
        this->display_->update();
      }
      this->pmic_ha_final_full_pending_ = false;
      this->clear_wake_recovery_flag_();
      this->cancel_periodic_wake_recovery_();
      ESP_LOGI(TAG, "PMIC boot: final HA FULL recovery complete");
      return;
    }

    ESP_LOGI(TAG, "Periodic wake: refresh after %u ms settle", elapsed);
    if (this->display_ != nullptr) {
      const uint32_t bucket = this->current_time_bucket_();
      if (bucket != UINT32_MAX) {
        this->last_periodic_bucket_ = bucket;
      }
      this->display_->update_partial(0, 0, 480, 800);
    }
    this->arm_light_sleep_after_refresh_();
    this->cancel_periodic_wake_recovery_();
  }
}

bool PaperMonoActivityComponent::can_enter_light_sleep_() const {
  if (!this->light_sleep_pending_ || this->pmu_ == nullptr || this->display_ == nullptr) {
    return false;
  }
  if (this->shutdown_phase_ != ShutdownPhase::NONE) {
    return false;
  }
  if (this->periodic_wake_phase_ != PeriodicWakePhase::NONE) {
    return false;
  }
  if (this->in_controls_view_()) {
    return false;
  }
  if (this->is_in_quiet_hours_() && this->quiet_hours_user_override_ != nullptr &&
      this->quiet_hours_user_override_->value()) {
    return false;
  }
  if (this->ha_connection_state_ != nullptr && this->ha_connection_state_->value() == 0) {
    return false;
  }
  if (this->wifi_transition_pending_ != nullptr && this->wifi_transition_pending_->value()) {
    return false;
  }
  if (!this->display_->is_idle() || this->display_->has_refresh_pending()) {
    return false;
  }
  if (this->last_activity_ms_ != this->sleep_eligible_activity_ms_) {
    return false;
  }
  return true;
}

uint64_t PaperMonoActivityComponent::compute_timer_wakeup_us_(LightSleepTimerReason *reason) const {
  const uint32_t interval_seconds = static_cast<uint32_t>(this->screensaver_refresh_minutes_) * 60U;
  uint64_t refresh_us = static_cast<uint64_t>(interval_seconds) * 1000000ULL;

  if (this->time_ != nullptr) {
    const ESPTime now = this->time_->now();
    if (now.is_valid()) {
      const uint32_t seconds_into_day =
          static_cast<uint32_t>(now.hour * 3600U + now.minute * 60U + now.second);
      const uint32_t next_aligned = ((seconds_into_day / interval_seconds) + 1U) * interval_seconds;
      uint32_t sleep_seconds = next_aligned - seconds_into_day;
      if (sleep_seconds == 0) {
        sleep_seconds = interval_seconds;
      }
      refresh_us = static_cast<uint64_t>(sleep_seconds) * 1000000ULL;
    }
  }

  LightSleepTimerReason selected = LightSleepTimerReason::NORMAL_REFRESH;
  uint64_t timer_us = refresh_us;

  if (!this->is_in_quiet_hours_()) {
    const uint32_t quiet_start_seconds = this->seconds_until_quiet_hours_start_();
    if (quiet_start_seconds != UINT32_MAX) {
      const uint64_t quiet_us = static_cast<uint64_t>(quiet_start_seconds) * 1000000ULL;
      if (quiet_us < timer_us) {
        timer_us = quiet_us;
        selected = LightSleepTimerReason::QUIET_HOURS_START;
      }
    }
  }

  if (reason != nullptr) {
    *reason = selected;
  }
  return timer_us;
}

void PaperMonoActivityComponent::disable_wifi_for_sleep_() {
#ifdef USE_WIFI
  if (wifi::global_wifi_component != nullptr && !wifi::global_wifi_component->is_disabled()) {
    ESP_LOGI(TAG, "Disabling WiFi before light sleep (wifi::WiFiComponent::disable)");
    wifi::global_wifi_component->disable();
  }
#endif
}

void PaperMonoActivityComponent::enable_wifi_after_wake_(bool timer_wake) {
  if (this->light_sleep_wake_recovery_ != nullptr) {
    this->light_sleep_wake_recovery_->value() = true;
  }

#ifdef USE_WIFI
  if (wifi::global_wifi_component != nullptr && wifi::global_wifi_component->is_disabled()) {
    ESP_LOGI(TAG, "Enabling WiFi after light sleep (wifi::WiFiComponent::enable)");
    wifi::global_wifi_component->enable();
  }
#endif

  if (timer_wake) {
    this->periodic_wake_activity_ms_ = this->last_activity_ms_;
    this->periodic_wake_started_ms_ = millis();
    this->periodic_wake_recovery_timeout_logged_ = false;
    this->periodic_wake_phase_ = PeriodicWakePhase::WAIT_API;
  }
}

void PaperMonoActivityComponent::enter_light_sleep_() {
  if (!this->pmu_->prepare_light_sleep_entry()) {
    const uint32_t now = millis();
    if (now - this->last_gpio_block_log_ms_ >= GPIO_BLOCK_LOG_INTERVAL_MS) {
      ESP_LOGI(TAG, "Light sleep deferred: PY_IRQ (GPIO1) still LOW");
      this->last_gpio_block_log_ms_ = now;
    }
    return;
  }

  if (!this->can_enter_light_sleep_()) {
    if (this->last_activity_ms_ != this->sleep_eligible_activity_ms_) {
      ESP_LOGI(TAG, "Light sleep aborted after IRQ prep (activity)");
    }
    return;
  }

  this->light_sleep_pending_ = false;

  LightSleepTimerReason timer_reason = LightSleepTimerReason::NORMAL_REFRESH;
  const uint64_t timer_us = this->compute_timer_wakeup_us_(&timer_reason);
  this->light_sleep_timer_reason_ = timer_reason;

  this->disable_wifi_for_sleep_();

  const m5pm1::LightSleepWakeupArmResult arm = this->pmu_->arm_light_sleep_wakeup(timer_us);
  ESP_LOGI(TAG, "Light sleep arm:");
  ESP_LOGI(TAG, "  timer=%llus reason=%s", timer_us / 1000000ULL,
           timer_reason == LightSleepTimerReason::QUIET_HOURS_START ? "QUIET_HOURS_START" : "NORMAL_REFRESH");
  ESP_LOGI(TAG, "  ext1_gpio1=%s", esp_err_to_name(arm.ext1));
  ESP_LOGI(TAG, "  gpio1_level=%s", arm.gpio1_high ? "HIGH" : "LOW");
  if (arm.disable_timer != ESP_OK || arm.disable_gpio != ESP_OK || arm.disable_ext1 != ESP_OK ||
      arm.gpio_disable != ESP_OK) {
    ESP_LOGW(TAG, "  disable: timer=%s gpio=%s ext1=%s pin=%s", esp_err_to_name(arm.disable_timer),
             esp_err_to_name(arm.disable_gpio), esp_err_to_name(arm.disable_ext1), esp_err_to_name(arm.gpio_disable));
  }

  if (!arm_result_ok_(arm)) {
    ESP_LOGE(TAG, "Light sleep arm failed; aborting entry");
    this->pmu_->restore_after_light_sleep();
    this->enable_wifi_after_wake_(false);
    this->light_sleep_pending_ = true;
    return;
  }

  ESP_LOGI(TAG, "Calling esp_light_sleep_start()");
  const esp_err_t sleep_err = esp_light_sleep_start();
  ESP_LOGI(TAG, "esp_light_sleep_start returned: %s", esp_err_to_name(sleep_err));
  this->pmu_->restore_after_light_sleep();

  if (sleep_err != ESP_OK) {
    ESP_LOGE(TAG, "Light sleep FAILED: %s", esp_err_to_name(sleep_err));
    this->enable_wifi_after_wake_(false);
    this->light_sleep_pending_ = true;
    return;
  }

  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  ESP_LOGI(TAG, "Light sleep returned:");
  ESP_LOGI(TAG, "  result=%s", esp_err_to_name(sleep_err));
  ESP_LOGI(TAG, "  wake_cause=%s", wakeup_cause_to_string_(cause));

  this->handle_light_sleep_wake_(cause, timer_reason);
}

void PaperMonoActivityComponent::handle_light_sleep_wake_(esp_sleep_wakeup_cause_t cause,
                                                          LightSleepTimerReason timer_reason) {
  if (cause == ESP_SLEEP_WAKEUP_TIMER) {
    if (timer_reason == LightSleepTimerReason::QUIET_HOURS_START) {
      ESP_LOGI(TAG, "Wake from light sleep: quiet_hours_start -> PMIC shutdown path");
      this->request_quiet_hours_shutdown_refresh_();
      return;
    }

    ESP_LOGI(TAG, "Wake from light sleep: timer");
    this->enable_wifi_after_wake_(true);
    return;
  }

  if (cause == ESP_SLEEP_WAKEUP_GPIO || cause == ESP_SLEEP_WAKEUP_EXT1) {
    this->enable_wifi_after_wake_(false);
    const bool motion = this->pmu_->process_pending_irq();
    if (motion) {
      ESP_LOGI(TAG, "Wake from light sleep: motion");
    } else {
      ESP_LOGI(TAG, "Wake from light sleep: gpio");
    }
    return;
  }

  ESP_LOGW(TAG, "Wake from light sleep: cause=%d", static_cast<int>(cause));
  this->enable_wifi_after_wake_(false);
}

void PaperMonoActivityComponent::report_activity(ActivitySource source) {
  if (this->pmu_ == nullptr) {
    return;
  }

  this->cancel_light_sleep_();
  this->cancel_shutdown_();
  if (source == ActivitySource::TOUCH) {
    this->clear_wake_recovery_flag_();
  }

  const uint32_t now = millis();
  this->last_activity_ms_ = now;

  if (!this->activity_active_) {
    this->activity_active_ = true;
    this->apply_frontlight_(true, source);
    if (source == ActivitySource::MOTION) {
      this->on_pickup_transition_();
    }
    return;
  }

  if (source == ActivitySource::MOTION && now - this->last_motion_log_ms_ >= 5000) {
    ESP_LOGD(TAG, "Frontlight: kept ON (activity=motion)");
    this->last_motion_log_ms_ = now;
  }
}

void PaperMonoActivityComponent::apply_frontlight_(bool on, ActivitySource source) {
  if (this->pmu_ == nullptr) {
    return;
  }

  if (on) {
    this->pmu_->set_frontlight_level(this->on_brightness_percent_);
    this->frontlight_on_ = true;
    if (source == ActivitySource::MOTION) {
      ESP_LOGI(TAG, "Frontlight: ON (activity=motion)");
      this->last_motion_log_ms_ = millis();
    } else {
      ESP_LOGI(TAG, "Frontlight: ON (activity=touch)");
    }
    return;
  }

  this->pmu_->set_frontlight_level(0);
  this->frontlight_on_ = false;
}

void PaperMonoActivityComponent::turn_off_timeout_() {
  if (!this->frontlight_on_) {
    this->activity_active_ = false;
    return;
  }
  this->apply_frontlight_(false, ActivitySource::MOTION);
  this->activity_active_ = false;
  ESP_LOGI(TAG, "Frontlight: OFF (timeout)");
}

}  // namespace esphome::papermono_activity
