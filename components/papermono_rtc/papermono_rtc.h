/*
 * SPDX-FileCopyrightText: 2026 PaperMono Home Assistant contributors
 * SPDX-License-Identifier: MIT
 *
 * RX8130 minimal driver derived from M5Stack M5Unified RX8130_Class (MIT).
 */
#pragma once

#include "esphome/components/i2c/i2c.h"
#include "esphome/core/component.h"
#include "esphome/core/optional.h"

namespace esphome::papermono_rtc {

class PaperMonoRtcComponent : public Component, public i2c::I2CDevice {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  bool is_ready() const { return this->ready_; }

  // M5Unified RX8130_Class::begin() essentials.
  bool probe_device_();

  // M5Unified RX8130_Class::clearIRQ() — W0C TF/AF on reg 0x1D.
  void clear_irq();

  // M5Unified RX8130_Class::disableIRQ() — disable timer/alarm IRQ enables.
  void disable_irq();

  // Read reg 0x1D without clearing (for boot wake classification).
  uint8_t read_irq_flags_raw();

  // Returns true when timer flag TF is set (reg 0x1D bit4).
  bool is_timer_irq_active();

  // Relative timer in milliseconds until IRQ. Returns programmed period ms, 0 on failure.
  // Port of M5Unified RX8130_Class::setTimerIRQ(uint32_t msec).
  uint32_t set_timer_irq(uint32_t msec);

 protected:
  bool update_reg_bit_(uint8_t reg, uint8_t mask, bool set);
  bool bit_on_(uint8_t reg, uint8_t mask);
  bool bit_off_(uint8_t reg, uint8_t mask);
  bool stop_timer_();

  bool ready_{false};
};

}  // namespace esphome::papermono_rtc
