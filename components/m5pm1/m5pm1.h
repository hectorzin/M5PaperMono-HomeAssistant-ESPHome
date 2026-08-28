/*
 * SPDX-FileCopyrightText: 2026 PaperMono Home Assistant contributors
 * SPDX-License-Identifier: MIT
 *
 * M5PM1 register map derived from M5Stack M5PM1 (MIT) and M5Unified Power_Class.cpp (MIT).
 */
#pragma once

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"

namespace esphome::m5pm1 {

class M5PM1Component : public PollingComponent, public i2c::I2CDevice {
 public:
  void dump_config() override;
  void update() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_battery_voltage_sensor(sensor::Sensor *sensor) { this->battery_voltage_sensor_ = sensor; }
  void set_battery_level_sensor(sensor::Sensor *sensor) { this->battery_level_sensor_ = sensor; }
  void set_external_power_binary_sensor(binary_sensor::BinarySensor *sensor) {
    this->external_power_binary_sensor_ = sensor;
  }

 protected:
  optional<uint16_t> read_vbat_mv_();
  optional<uint8_t> read_pwr_src_();
  int battery_level_from_mv_(uint16_t mv) const;

  sensor::Sensor *battery_voltage_sensor_{nullptr};
  sensor::Sensor *battery_level_sensor_{nullptr};
  binary_sensor::BinarySensor *external_power_binary_sensor_{nullptr};
};

}  // namespace esphome::m5pm1
