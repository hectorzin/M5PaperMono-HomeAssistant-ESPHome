/*
 * SPDX-FileCopyrightText: 2026 PaperMono Home Assistant contributors
 * SPDX-License-Identifier: MIT
 *
 * M5PM1 register map derived from M5Stack M5PM1 (MIT) and M5Unified Power_Class.cpp (MIT).
 */
#include "m5pm1.h"

#include "esphome/core/log.h"

namespace esphome::m5pm1 {

static const char *const TAG = "m5pm1";

// M5PM1 registers (M5Stack M5PM1 library, MIT).
static constexpr uint8_t M5PM1_REG_PWR_SRC = 0x04;
static constexpr uint8_t M5PM1_REG_VBAT_L = 0x22;

// PWR_SRC is a bitmap: bit0=5VIN, bit1=5VINOUT, bit2=BAT (M5Unified Power_Class.cpp).
static constexpr uint8_t M5PM1_PWR_SRC_5VIN = 0x01;
static constexpr uint8_t M5PM1_PWR_SRC_5VINOUT = 0x02;

// Official M5Unified voltage-to-percent mapping for M5PM1 boards (no fuel gauge SOC).
static constexpr uint16_t BATTERY_LEVEL_EMPTY_MV = 3300;
static constexpr uint16_t BATTERY_LEVEL_SPAN_MV = 800;  // 4150 - 3350

void M5PM1Component::dump_config() {
  LOG_I2C_DEVICE(this);
  LOG_UPDATE_INTERVAL(this);
}

optional<uint16_t> M5PM1Component::read_vbat_mv_() {
  // M5PM1 16-bit registers are little-endian (M5PM1_i2c_compat.h). ESPHome read_byte_16 uses
  // i2ctohs (big-endian) and must not be used here.
  uint8_t buf[2] = {0, 0};
  if (!this->read_bytes(M5PM1_REG_VBAT_L, buf, 2)) {
    ESP_LOGW(TAG, "VBAT read failed");
    return {};
  }

  const uint16_t mv = static_cast<uint16_t>(buf[0]) | (static_cast<uint16_t>(buf[1]) << 8);
  ESP_LOGD(TAG, "VBAT raw: L=0x%02X H=0x%02X -> %u mV", buf[0], buf[1], mv);
  if (mv == 0xFFFF) {
    ESP_LOGW(TAG, "VBAT invalid: 0xFFFF");
    return {};
  }

  return mv;
}

optional<uint8_t> M5PM1Component::read_pwr_src_() {
  uint8_t value = 0;
  if (!this->read_byte(M5PM1_REG_PWR_SRC, &value)) {
    ESP_LOGW(TAG, "PWR_SRC read failed");
    return {};
  }
  return value;
}

int M5PM1Component::battery_level_from_mv_(uint16_t mv) const {
  // M5Unified Power_Class::getBatteryLevel() for pmic_m5pm1.
  const int level = static_cast<int>((static_cast<int>(mv) - static_cast<int>(BATTERY_LEVEL_EMPTY_MV)) * 100 /
                                     static_cast<float>(BATTERY_LEVEL_SPAN_MV));
  if (level < 0) {
    return 0;
  }
  if (level >= 100) {
    return 100;
  }
  return level;
}

void M5PM1Component::update() {
  const auto vbat_mv = this->read_vbat_mv_();
  if (vbat_mv.has_value()) {
    const float volts = *vbat_mv / 1000.0f;
    const int level = this->battery_level_from_mv_(*vbat_mv);
    ESP_LOGI(TAG, "VBAT=%u mV (%.3f V), level=%d%%", *vbat_mv, volts, level);

    if (this->battery_voltage_sensor_ != nullptr) {
      this->battery_voltage_sensor_->publish_state(volts);
    }
    if (this->battery_level_sensor_ != nullptr) {
      this->battery_level_sensor_->publish_state(level);
    }
  }

  const auto pwr_src = this->read_pwr_src_();
  if (pwr_src.has_value()) {
    const bool external_power = (*pwr_src & (M5PM1_PWR_SRC_5VIN | M5PM1_PWR_SRC_5VINOUT)) != 0;
    ESP_LOGI(TAG, "PWR_SRC=0x%02X, external_power=%s", *pwr_src, YESNO(external_power));

    if (this->external_power_binary_sensor_ != nullptr) {
      this->external_power_binary_sensor_->publish_state(external_power);
    }
  }
}

}  // namespace esphome::m5pm1
