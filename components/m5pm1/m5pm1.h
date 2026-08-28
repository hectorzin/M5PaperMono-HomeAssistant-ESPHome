/*
 * SPDX-FileCopyrightText: 2026 PaperMono Home Assistant contributors
 * SPDX-License-Identifier: MIT
 *
 * M5PM1 register map derived from M5Stack M5PM1 (MIT) and M5Unified Power_Class.cpp (MIT).
 */
#pragma once

#include <functional>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"

namespace esphome::m5pm1 {

class M5PM1Component : public Component, public i2c::I2CDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_irq_pin(InternalGPIOPin *pin) { this->irq_pin_ = pin; }
  void set_battery_voltage_sensor(sensor::Sensor *sensor) { this->battery_voltage_sensor_ = sensor; }
  void set_battery_level_sensor(sensor::Sensor *sensor) { this->battery_level_sensor_ = sensor; }
  void set_external_power_binary_sensor(binary_sensor::BinarySensor *sensor) {
    this->external_power_binary_sensor_ = sensor;
  }

  // PaperMono status LED red channel (PMIC LED_EN / PM_LED).
  bool set_status_red_led(bool on);

  // PaperMono frontlight via M5PM1 GPIO3 / PWM0 (M5GFX Light_M5PaperMono).
  bool set_frontlight_level(uint8_t percent);

  // BMI270 INT1 -> M5PM1 GPIO4 -> PY_IRQ (ESP32 GPIO1).
  bool configure_imu_irq_route_();
  void set_motion_handler(std::function<void()> handler) { this->motion_handler_ = std::move(handler); }

  // Read PWR_SRC + VBAT and publish linked sensors. Safe to call from the main loop only.
  void refresh_power_and_battery();

 protected:
  static void IRAM_ATTR irq_isr_(M5PM1Component *arg);

  bool update_reg_bit_(uint8_t reg, uint8_t mask, bool set);
  bool configure_gpio1_irq_output_();
  bool configure_gpio4_imu_input_();
  bool configure_usb_irq_masks_();
  bool configure_frontlight_pwm_hw_();
  bool set_single_reset_disable_(bool disable);
  optional<bool> get_single_reset_disabled_();
  void process_irq_();
  void clear_all_irq_status_();
  optional<uint16_t> read_vbat_mv_();
  optional<uint8_t> read_pwr_src_();
  int battery_level_from_mv_(uint16_t mv) const;

  InternalGPIOPin *irq_pin_{nullptr};
  volatile bool irq_pending_{false};
  sensor::Sensor *battery_voltage_sensor_{nullptr};
  sensor::Sensor *battery_level_sensor_{nullptr};
  binary_sensor::BinarySensor *external_power_binary_sensor_{nullptr};
  std::function<void()> motion_handler_;
  bool frontlight_hw_ready_{false};
};

}  // namespace esphome::m5pm1
