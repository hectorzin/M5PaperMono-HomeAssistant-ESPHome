/*
 * SPDX-FileCopyrightText: 2026 PaperMono Home Assistant contributors
 * SPDX-License-Identifier: MIT
 *
 * BMI270 any-motion configuration derived from M5PaperMono-UserDemo (MIT)
 * and Bosch BMI270-Sensor-API (BSD-3-Clause).
 */
#pragma once

#include "esphome/components/i2c/i2c.h"
#include "esphome/core/component.h"

namespace esphome::m5pm1 {
class M5PM1Component;
}

namespace esphome::papermono_activity {
class PaperMonoActivityComponent;
}

namespace esphome::papermono_imu {

class PaperMonoImuComponent : public Component, public i2c::I2CDevice {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_m5pm1(m5pm1::M5PM1Component *pmu) { this->pmu_ = pmu; }
  void set_activity(papermono_activity::PaperMonoActivityComponent *activity) { this->activity_ = activity; }

  bool handle_motion_irq();

 protected:
  bool configure_any_motion_();
  bool read_interrupt_status_(uint16_t *status);

  m5pm1::M5PM1Component *pmu_{nullptr};
  papermono_activity::PaperMonoActivityComponent *activity_{nullptr};
  uint8_t bmi_addr_{0x68};
  bool configured_{false};
};

}  // namespace esphome::papermono_imu
