/*
 * SPDX-FileCopyrightText: 2026 PaperMono Home Assistant contributors
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <algorithm>

#include "esp_sleep.h"

#include "esphome/core/component.h"
#include "esphome/components/globals/globals_component.h"

namespace esphome::globals {
template<typename T> class GlobalsComponent;
}

namespace esphome::m5pm1 {
class M5PM1Component;
}

namespace esphome::m5ioe1 {
class M5IOE1Component;
}

namespace esphome::papermono_epaper {
class PaperMonoEpaper;
}

namespace esphome::papermono_rtc {
class PaperMonoRtcComponent;
}

namespace esphome::time {
class RealTimeClock;
}

namespace esphome::papermono_activity {

enum class ActivitySource : uint8_t {
  MOTION = 0,
  TOUCH = 1,
};

enum class PeriodicWakePhase : uint8_t {
  NONE = 0,
  WAIT_API = 1,
  SETTLE = 2,
};

enum class LightSleepTimerReason : uint8_t {
  NORMAL_REFRESH = 0,
  QUIET_HOURS_START = 1,
};

enum class ShutdownPhase : uint8_t {
  NONE = 0,
  WAIT_DISPLAY = 1,
};

enum class PmicHwRecoveryPhase : uint8_t {
  NONE = 0,
  WAIT_M5IOE1 = 1,
  EPD_RECOVERY = 2,
  FRONTLIGHT = 3,
  COMPLETE = 4,
  DONE = 5,
};

class PaperMonoActivityComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::DATA - 1.0f; }

  void set_m5pm1(m5pm1::M5PM1Component *pmu) { this->pmu_ = pmu; }
  void set_rtc(papermono_rtc::PaperMonoRtcComponent *rtc) { this->rtc_ = rtc; }
  void set_display(papermono_epaper::PaperMonoEpaper *display) { this->display_ = display; }
  void set_controls_view(globals::GlobalsComponent<bool> *controls_view) { this->controls_view_ = controls_view; }
  void set_ha_connection_state(globals::GlobalsComponent<int> *state) { this->ha_connection_state_ = state; }
  void set_wifi_transition_pending(globals::GlobalsComponent<bool> *pending) { this->wifi_transition_pending_ = pending; }
  void set_light_sleep_wake_recovery(globals::GlobalsComponent<bool> *recovery) {
    this->light_sleep_wake_recovery_ = recovery;
  }
  void set_quiet_hours_sleep_display(globals::GlobalsComponent<bool> *value) {
    this->quiet_hours_sleep_display_ = value;
  }
  void set_quiet_hours_user_override(globals::GlobalsComponent<bool> *value) {
    this->quiet_hours_user_override_ = value;
  }
  void set_battery_display_level(globals::GlobalsComponent<float> *value) { this->battery_display_level_ = value; }
  void set_frontlight_default_brightness(globals::RestoringGlobalsComponent<int> *value) {
    this->frontlight_default_brightness_ = value;
  }
  void set_frontlight_timeout_seconds(globals::RestoringGlobalsComponent<uint32_t> *value) {
    this->frontlight_timeout_seconds_ = value;
  }
  void set_sleep_timeout_seconds(globals::RestoringGlobalsComponent<uint32_t> *value) {
    this->sleep_timeout_seconds_ = value;
  }
  void set_screensaver_refresh_minutes(globals::RestoringGlobalsComponent<int> *value) {
    this->screensaver_refresh_minutes_global_ = value;
  }
  void set_quiet_hours_start(globals::RestoringGlobalStringComponent<std::string, 64> *value) {
    this->quiet_hours_start_ = value;
  }
  void set_quiet_hours_end(globals::RestoringGlobalStringComponent<std::string, 64> *value) {
    this->quiet_hours_end_ = value;
  }
  void set_time(time::RealTimeClock *time) { this->time_ = time; }

  void report_activity(ActivitySource source);
  void report_touch() { this->report_activity(ActivitySource::TOUCH); }

  void enter_controls();
  void exit_controls();

  void begin_pmic_wake_hardware_recovery(m5ioe1::M5IOE1Component *ioe);

  // Clock-aligned timer handling for quiet-hours and periodic light-sleep wakes.
  void on_screensaver_tick();

 protected:
  void exit_controls_(bool preserve_sleep_pending);
  void apply_frontlight_(bool on, ActivitySource source);
  void turn_off_timeout_();
  void on_pickup_transition_();
  bool in_controls_view_() const;
  void request_screensaver_refresh_policy_();
  void request_controls_refresh_policy_();
  void cancel_light_sleep_();
  void cancel_periodic_wake_recovery_();
  void clear_wake_recovery_flag_();
  void run_screensaver_periodic_tick_(bool quiet_sleep_display);
  void arm_light_sleep_after_refresh_();
  bool can_enter_light_sleep_() const;
  bool can_begin_shutdown_() const;
  bool is_network_api_ready_() const;
  void process_periodic_wake_recovery_();
  void process_shutdown_pending_();
  void disable_wifi_for_sleep_();
  void enable_wifi_after_wake_(bool timer_wake);
  void enter_light_sleep_();
  void handle_light_sleep_wake_(esp_sleep_wakeup_cause_t cause, LightSleepTimerReason timer_reason);
  uint32_t current_time_bucket_() const;
  uint64_t compute_timer_wakeup_us_(LightSleepTimerReason *reason) const;
  bool parse_quiet_time_(const std::string &value, int *minutes_out) const;
  bool is_in_quiet_hours_() const;
  uint32_t seconds_until_quiet_hours_start_() const;
  uint32_t seconds_until_quiet_hours_end_() const;
  void handle_boot_wake_source_();
  void sync_battery_display_for_shutdown_();
  void process_pmic_hw_recovery_();
  void complete_pmic_wake_hardware_recovery();
  void begin_pmic_ha_final_full_recovery_();
  bool begin_quiet_hours_shutdown_();
  void request_quiet_hours_shutdown_refresh_(const char *source);
  void cancel_shutdown_();
  uint32_t timeout_ms_() const {
    const uint32_t seconds = this->frontlight_timeout_seconds_ != nullptr ? this->frontlight_timeout_seconds_->value() : 30U;
    return seconds == 0 ? 30000U : seconds * 1000U;
  }
  uint32_t sleep_timeout_ms_() const {
    const uint32_t seconds = this->sleep_timeout_seconds_ != nullptr ? this->sleep_timeout_seconds_->value() : 60U;
    return seconds == 0 ? 60000U : seconds * 1000U;
  }
  uint8_t on_brightness_percent_() const {
    return this->frontlight_default_brightness_ != nullptr
               ? static_cast<uint8_t>(std::clamp(this->frontlight_default_brightness_->value(), 0, 100))
               : 30;
  }
  uint8_t screensaver_refresh_minutes_() const {
    const int minutes = this->screensaver_refresh_minutes_global_ != nullptr
                            ? this->screensaver_refresh_minutes_global_->value()
                            : 5;
    return minutes > 0 && minutes <= 60 && 60 % minutes == 0 ? static_cast<uint8_t>(minutes) : 5;
  }
  const std::string &quiet_hours_start_value_() const {
    static const std::string fallback{"00:00"};
    return this->quiet_hours_start_ != nullptr ? this->quiet_hours_start_->value() : fallback;
  }
  const std::string &quiet_hours_end_value_() const {
    static const std::string fallback{"08:00"};
    return this->quiet_hours_end_ != nullptr ? this->quiet_hours_end_->value() : fallback;
  }

  m5pm1::M5PM1Component *pmu_{nullptr};
  papermono_rtc::PaperMonoRtcComponent *rtc_{nullptr};
  papermono_epaper::PaperMonoEpaper *display_{nullptr};
  globals::GlobalsComponent<bool> *controls_view_{nullptr};
  globals::GlobalsComponent<int> *ha_connection_state_{nullptr};
  globals::GlobalsComponent<bool> *wifi_transition_pending_{nullptr};
  globals::GlobalsComponent<bool> *light_sleep_wake_recovery_{nullptr};
  globals::GlobalsComponent<bool> *quiet_hours_sleep_display_{nullptr};
  globals::GlobalsComponent<bool> *quiet_hours_user_override_{nullptr};
  globals::GlobalsComponent<float> *battery_display_level_{nullptr};
  time::RealTimeClock *time_{nullptr};
  globals::RestoringGlobalsComponent<int> *frontlight_default_brightness_{nullptr};
  globals::RestoringGlobalsComponent<uint32_t> *frontlight_timeout_seconds_{nullptr};
  globals::RestoringGlobalsComponent<uint32_t> *sleep_timeout_seconds_{nullptr};
  globals::RestoringGlobalsComponent<int> *screensaver_refresh_minutes_global_{nullptr};
  globals::RestoringGlobalStringComponent<std::string, 64> *quiet_hours_start_{nullptr};
  globals::RestoringGlobalStringComponent<std::string, 64> *quiet_hours_end_{nullptr};
  uint32_t last_activity_ms_{0};
  uint32_t sleep_eligible_activity_ms_{0};
  uint32_t shutdown_eligible_activity_ms_{0};
  uint32_t periodic_wake_activity_ms_{0};
  uint32_t periodic_wake_settle_start_ms_{0};
  uint32_t periodic_wake_started_ms_{0};
  uint32_t last_periodic_bucket_{UINT32_MAX};
  uint32_t last_periodic_tick_activity_ms_{0};
  uint32_t last_gpio_block_log_ms_{0};
  bool activity_active_{false};
  bool frontlight_on_{false};
  bool pickup_cleanup_pending_{false};
  bool light_sleep_pending_{false};
  bool sleep_timeout_logged_{false};
  bool periodic_wake_recovery_timeout_logged_{false};
  bool pending_pmic_motion_activity_{false};
  PmicHwRecoveryPhase pmic_hw_recovery_phase_{PmicHwRecoveryPhase::NONE};
  uint32_t pmic_hw_recovery_started_ms_{0};
  uint32_t pmic_hw_recovery_next_probe_ms_{0};
  bool pmic_hw_recovery_wait_logged_{false};
  bool pmic_ha_final_full_pending_{false};
  m5ioe1::M5IOE1Component *m5ioe1_{nullptr};
  PeriodicWakePhase periodic_wake_phase_{PeriodicWakePhase::NONE};
  ShutdownPhase shutdown_phase_{ShutdownPhase::NONE};
  LightSleepTimerReason light_sleep_timer_reason_{LightSleepTimerReason::NORMAL_REFRESH};
  uint32_t last_motion_log_ms_{0};
};

}  // namespace esphome::papermono_activity
