/*
 * SPDX-FileCopyrightText: 2026 PaperMono Home Assistant contributors
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "esphome/core/component.h"

namespace esphome::m5pm1 {
class M5PM1Component;
}

namespace esphome::papermono_activity {

enum class ActivitySource : uint8_t {
  MOTION = 0,
  TOUCH = 1,
};

class PaperMonoActivityComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::DATA - 1.0f; }

  void set_m5pm1(m5pm1::M5PM1Component *pmu) { this->pmu_ = pmu; }
  void set_timeout_ms(uint32_t timeout_ms) { this->timeout_ms_ = timeout_ms; }
  void set_on_brightness_percent(uint8_t percent) { this->on_brightness_percent_ = percent; }

  void report_activity(ActivitySource source);
  void report_touch() { this->report_activity(ActivitySource::TOUCH); }

 protected:
  void apply_frontlight_(bool on, ActivitySource source);
  void turn_off_timeout_();

  m5pm1::M5PM1Component *pmu_{nullptr};
  uint32_t timeout_ms_{30000};
  uint8_t on_brightness_percent_{30};
  uint32_t last_activity_ms_{0};
  bool frontlight_on_{false};
  uint32_t last_motion_log_ms_{0};
};

}  // namespace esphome::papermono_activity
