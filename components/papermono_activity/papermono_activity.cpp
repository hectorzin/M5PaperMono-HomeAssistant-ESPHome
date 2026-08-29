/*
 * SPDX-FileCopyrightText: 2026 PaperMono Home Assistant contributors
 * SPDX-License-Identifier: MIT
 */
#include "papermono_activity.h"

#include "esphome/components/api/api_server.h"
#include "esphome/components/globals/globals_component.h"
#include "esphome/components/m5pm1/m5pm1.h"
#include "esphome/components/papermono_epaper/papermono_epaper.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/wifi/wifi_component.h"
#include "esphome/core/log.h"

#include "esp_sleep.h"

namespace esphome::papermono_activity {

static const char *const TAG = "papermono_activity";

// Screensaver: auto-FULL disabled (full_update_every=0).
static constexpr uint8_t SCREENSAVER_FULL_EVERY = 0;
// Controls: soft auto-FULL threshold while interacting.
static constexpr uint8_t CONTROLS_FULL_EVERY = 15;
// Opportunistic FULL on pickup / enter controls when ghosting is visible.
static constexpr uint8_t PICKUP_FULL_THRESHOLD = 8;
static constexpr uint8_t CONTROLS_ENTER_FULL_THRESHOLD = 8;
static constexpr uint8_t CONTROLS_EXIT_FULL_THRESHOLD = 10;
static constexpr uint32_t HA_STATE_SETTLE_MS = 1750;
static constexpr uint32_t PERIODIC_WAKE_RECOVERY_TIMEOUT_MS = 30000;
static constexpr uint32_t GPIO_BLOCK_LOG_INTERVAL_MS = 10000;

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
  this->periodic_wake_phase_ = PeriodicWakePhase::NONE;
  this->last_activity_ms_ = 0;
  this->sleep_eligible_activity_ms_ = 0;
  this->periodic_wake_activity_ms_ = 0;
  this->periodic_wake_settle_start_ms_ = 0;
  this->periodic_wake_started_ms_ = 0;
  this->last_periodic_bucket_ = UINT32_MAX;
  this->last_gpio_block_log_ms_ = 0;
  this->periodic_wake_recovery_timeout_logged_ = false;
  this->request_screensaver_refresh_policy_();
  ESP_LOGI(TAG, "Frontlight init: OFF (boot default)");
  ESP_LOGI(TAG, "Activity timeout: %u ms, on level: %u%%", this->timeout_ms_, this->on_brightness_percent_);
  ESP_LOGI(TAG, "Screensaver refresh interval: %u min (clock-aligned)", this->screensaver_refresh_minutes_);
}

void PaperMonoActivityComponent::loop() {
  if (this->pickup_cleanup_pending_ && this->display_ != nullptr && this->display_->is_idle()) {
    this->pickup_cleanup_pending_ = false;
  }

  if (this->periodic_wake_phase_ != PeriodicWakePhase::NONE) {
    this->process_periodic_wake_recovery_();
  } else if (this->light_sleep_wake_recovery_ != nullptr && this->light_sleep_wake_recovery_->value() &&
             this->is_network_api_ready_()) {
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

void PaperMonoActivityComponent::run_screensaver_periodic_tick_() {
  if (this->pmu_ != nullptr) {
    this->pmu_->refresh_power_and_battery();
  }

  const bool allow_partial = !this->in_controls_view_() &&
                             (this->ha_connection_state_ == nullptr || this->ha_connection_state_->value() != 0);
  ESP_LOGI(TAG, "Scheduler /%u: controls=%d ha_state=%d -> partial=%s", this->screensaver_refresh_minutes_,
           this->in_controls_view_(), this->ha_connection_state_ != nullptr ? this->ha_connection_state_->value() : -1,
           allow_partial ? "yes" : "no");

  if (!allow_partial || this->display_ == nullptr) {
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

  this->display_->update_partial(0, 0, 480, 800);
  this->arm_light_sleep_after_refresh_();
}

void PaperMonoActivityComponent::on_screensaver_tick() {
  if (this->light_sleep_wake_recovery_ != nullptr && this->light_sleep_wake_recovery_->value()) {
    ESP_LOGD(TAG, "Scheduler tick skipped (light_sleep_wake_recovery active)");
    return;
  }
  this->run_screensaver_periodic_tick_();
}

void PaperMonoActivityComponent::arm_light_sleep_after_refresh_() {
  this->sleep_eligible_activity_ms_ = this->last_activity_ms_;
  this->light_sleep_pending_ = true;
}

void PaperMonoActivityComponent::process_periodic_wake_recovery_() {
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

  if (!this->is_network_api_ready_()) {
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
    return;
  }

  this->periodic_wake_recovery_timeout_logged_ = false;

  if (this->periodic_wake_phase_ == PeriodicWakePhase::WAIT_API) {
    ESP_LOGI(TAG, "Periodic wake: API ready, settling HA states");
    this->periodic_wake_settle_start_ms_ = millis();
    this->periodic_wake_phase_ = PeriodicWakePhase::SETTLE;
    return;
  }

  if (this->periodic_wake_phase_ == PeriodicWakePhase::SETTLE) {
    const uint32_t elapsed = millis() - this->periodic_wake_settle_start_ms_;
    if (elapsed < HA_STATE_SETTLE_MS) {
      return;
    }

    ESP_LOGI(TAG, "Periodic wake: refresh after %u ms settle", elapsed);
    if (this->pmu_ != nullptr) {
      this->pmu_->refresh_power_and_battery();
    }
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
  if (this->periodic_wake_phase_ != PeriodicWakePhase::NONE) {
    return false;
  }
  if (this->in_controls_view_()) {
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

uint64_t PaperMonoActivityComponent::compute_timer_wakeup_us_() const {
  const uint32_t interval_seconds = static_cast<uint32_t>(this->screensaver_refresh_minutes_) * 60U;
  if (interval_seconds == 0) {
    return 60ULL * 1000000ULL;
  }

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
      return static_cast<uint64_t>(sleep_seconds) * 1000000ULL;
    }
  }

  return static_cast<uint64_t>(interval_seconds) * 1000000ULL;
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

  const uint64_t timer_us = this->compute_timer_wakeup_us_();

  this->disable_wifi_for_sleep_();

  const m5pm1::LightSleepWakeupArmResult arm = this->pmu_->arm_light_sleep_wakeup(timer_us);
  ESP_LOGI(TAG, "Light sleep arm:");
  ESP_LOGI(TAG, "  timer=%llus result=%s", timer_us / 1000000ULL, esp_err_to_name(arm.timer));
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

  this->handle_light_sleep_wake_(cause);
}

void PaperMonoActivityComponent::handle_light_sleep_wake_(esp_sleep_wakeup_cause_t cause) {
  if (cause == ESP_SLEEP_WAKEUP_TIMER) {
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
