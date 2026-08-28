/*
 * SPDX-FileCopyrightText: 2026 PaperMono Home Assistant contributors
 * SPDX-License-Identifier: MIT
 *
 * M5PM1 register map derived from M5Stack M5PM1 (MIT) and M5Unified Power_Class.cpp (MIT).
 */
#include "m5pm1.h"

#include "esphome/core/gpio.h"
#include "esphome/core/log.h"

namespace esphome::m5pm1 {

static const char *const TAG = "m5pm1";

// M5PM1 registers (M5Stack M5PM1 library, MIT).
static constexpr uint8_t M5PM1_REG_PWR_SRC = 0x04;
static constexpr uint8_t M5PM1_REG_PWR_CFG = 0x06;
static constexpr uint8_t M5PM1_REG_GPIO_MODE = 0x10;
static constexpr uint8_t M5PM1_REG_GPIO_OUT = 0x11;
static constexpr uint8_t M5PM1_REG_GPIO_DRV = 0x13;
static constexpr uint8_t M5PM1_REG_GPIO_PUPD0 = 0x14;
static constexpr uint8_t M5PM1_REG_GPIO_FUNC0 = 0x16;
static constexpr uint8_t M5PM1_REG_IRQ_STATUS1 = 0x40;
static constexpr uint8_t M5PM1_REG_IRQ_STATUS2 = 0x41;
static constexpr uint8_t M5PM1_REG_IRQ_STATUS3 = 0x42;
static constexpr uint8_t M5PM1_REG_IRQ_MASK1 = 0x43;
static constexpr uint8_t M5PM1_REG_IRQ_MASK2 = 0x44;
static constexpr uint8_t M5PM1_REG_IRQ_MASK3 = 0x45;
static constexpr uint8_t M5PM1_REG_VBAT_L = 0x22;

// PWR_CFG[4] LED_EN level: 0=low (red off), 1=high (red on). Matches setLedEnLevel().
static constexpr uint8_t M5PM1_PWR_CFG_LED_CTRL = 0x10;
// GPIO_DRV[5] LED_EN drive: 0=push-pull, 1=open-drain. LED_PaperMono_Class::begin().
static constexpr uint8_t M5PM1_GPIO_DRV_LED_EN = 0x20;

// IRQ_STATUS2 system events (M5PM1.h / usb_interrupt_sleep.ino).
static constexpr uint8_t M5PM1_IRQ_SYS_5VIN_INSERT = 0x01;
static constexpr uint8_t M5PM1_IRQ_SYS_5VIN_REMOVE = 0x02;
static constexpr uint8_t M5PM1_IRQ_SYS_MASK_ALL = 0x3F;

// M5PM1 GPIO1 is the PaperMono IRQ output (PY_IRQ -> ESP32 GPIO1).
static constexpr uint8_t M5PM1_GPIO_NUM_1 = 1;
static constexpr uint8_t M5PM1_GPIO_FUNC_IRQ = 0x01;
static constexpr uint8_t M5PM1_GPIO_PULL_UP = 0x01;

static constexpr uint8_t M5PM1_REG_BTN_CFG_1 = 0x49;
// BTN_CFG_1[0] SINGLE_RST_DIS: 0=enable single-click reset, 1=disable (setSingleResetDisable).
static constexpr uint8_t M5PM1_BTN_CFG1_SINGLE_RST_DIS = 0x01;

// PWR_SRC is a bitmap: bit0=5VIN, bit1=5VINOUT, bit2=BAT (M5Unified Power_Class.cpp).
static constexpr uint8_t M5PM1_PWR_SRC_5VIN = 0x01;
static constexpr uint8_t M5PM1_PWR_SRC_5VINOUT = 0x02;

// Official M5Unified voltage-to-percent mapping for M5PM1 boards (no fuel gauge SOC).
static constexpr uint16_t BATTERY_LEVEL_EMPTY_MV = 3300;
static constexpr uint16_t BATTERY_LEVEL_SPAN_MV = 800;  // 4150 - 3350

void M5PM1Component::dump_config() {
  LOG_I2C_DEVICE(this);
  LOG_PIN("  IRQ Pin: ", this->irq_pin_);
}

bool M5PM1Component::update_reg_bit_(uint8_t reg, uint8_t mask, bool set) {
  uint8_t value = 0;
  if (!this->read_byte(reg, &value)) {
    return false;
  }
  if (set) {
    value |= mask;
  } else {
    value &= static_cast<uint8_t>(~mask);
  }
  return this->write_byte(reg, value);
}

void IRAM_ATTR M5PM1Component::irq_isr_(M5PM1Component *arg) { arg->irq_pending_ = true; }

bool M5PM1Component::configure_gpio1_irq_output_() {
  // M5Unified board_M5PaperMono: PM1 GPIO1 push-pull high, then IRQ function.
  const uint8_t pin = M5PM1_GPIO_NUM_1;
  const uint8_t pin_mask = static_cast<uint8_t>(1 << pin);

  if (!this->update_reg_bit_(M5PM1_REG_GPIO_MODE, pin_mask, true)) {
    return false;
  }
  if (!this->update_reg_bit_(M5PM1_REG_GPIO_OUT, pin_mask, true)) {
    return false;
  }
  if (!this->update_reg_bit_(M5PM1_REG_GPIO_DRV, pin_mask, false)) {
    return false;
  }

  uint8_t pupd0 = 0;
  if (!this->read_byte(M5PM1_REG_GPIO_PUPD0, &pupd0)) {
    return false;
  }
  pupd0 &= static_cast<uint8_t>(~(0x03 << (pin * 2)));
  pupd0 |= static_cast<uint8_t>(M5PM1_GPIO_PULL_UP << (pin * 2));
  if (!this->write_byte(M5PM1_REG_GPIO_PUPD0, pupd0)) {
    return false;
  }

  uint8_t func0 = 0;
  if (!this->read_byte(M5PM1_REG_GPIO_FUNC0, &func0)) {
    return false;
  }
  func0 &= static_cast<uint8_t>(~(0x03 << (pin * 2)));
  func0 |= static_cast<uint8_t>(M5PM1_GPIO_FUNC_IRQ << (pin * 2));
  if (!this->write_byte(M5PM1_REG_GPIO_FUNC0, func0)) {
    return false;
  }

  return true;
}

bool M5PM1Component::configure_usb_irq_masks_() {
  // usb_interrupt_sleep.ino: mask GPIO/button IRQs, then unmask 5VIN insert/remove.
  if (!this->write_byte(M5PM1_REG_IRQ_MASK1, 0x1F)) {
    return false;
  }
  if (!this->write_byte(M5PM1_REG_IRQ_MASK3, 0x07)) {
    return false;
  }

  uint8_t sys_mask = M5PM1_IRQ_SYS_MASK_ALL;
  sys_mask &= static_cast<uint8_t>(~(M5PM1_IRQ_SYS_5VIN_INSERT | M5PM1_IRQ_SYS_5VIN_REMOVE));
  if (!this->write_byte(M5PM1_REG_IRQ_MASK2, sys_mask)) {
    return false;
  }

  this->clear_all_irq_status_();
  return true;
}

void M5PM1Component::clear_all_irq_status_() {
  this->write_byte(M5PM1_REG_IRQ_STATUS1, 0x00);
  this->write_byte(M5PM1_REG_IRQ_STATUS2, 0x00);
  this->write_byte(M5PM1_REG_IRQ_STATUS3, 0x00);
}

bool M5PM1Component::set_single_reset_disable_(bool disable) {
  // Matches M5PM1::setSingleResetDisable().
  return this->update_reg_bit_(M5PM1_REG_BTN_CFG_1, M5PM1_BTN_CFG1_SINGLE_RST_DIS, disable);
}

optional<bool> M5PM1Component::get_single_reset_disabled_() {
  // Matches M5PM1::getSingleResetDisable().
  uint8_t reg_val = 0;
  if (!this->read_byte(M5PM1_REG_BTN_CFG_1, &reg_val)) {
    return {};
  }
  return (reg_val & M5PM1_BTN_CFG1_SINGLE_RST_DIS) != 0;
}

void M5PM1Component::setup() {
  if (!this->update_reg_bit_(M5PM1_REG_GPIO_DRV, M5PM1_GPIO_DRV_LED_EN, false)) {
    ESP_LOGW(TAG, "GPIO_DRV LED_EN push-pull config failed");
  }
  if (!this->set_status_red_led(false)) {
    ESP_LOGW(TAG, "Status LED red off failed during setup");
  } else {
    ESP_LOGI(TAG, "Status LED init: red off via M5PM1 PWR_CFG[4] (reg 0x06)");
  }

  if (!this->configure_gpio1_irq_output_()) {
    ESP_LOGW(TAG, "PM1 GPIO1 IRQ output config failed");
  }
  if (!this->configure_usb_irq_masks_()) {
    ESP_LOGW(TAG, "PM1 USB IRQ mask config failed");
  }

  if (this->irq_pin_ != nullptr) {
    this->irq_pin_->setup();
    this->irq_pin_->pin_mode(gpio::FLAG_INPUT | gpio::FLAG_PULLUP);
    this->irq_pin_->attach_interrupt(M5PM1Component::irq_isr_, this, gpio::INTERRUPT_FALLING_EDGE);
    ESP_LOGI(TAG, "USB IRQ armed on ESP32 GPIO%d (PY_IRQ, falling edge)", this->irq_pin_->get_pin());
    this->enable_loop();
  } else {
    ESP_LOGW(TAG, "IRQ pin not configured; USB plug/unplug will not be detected immediately");
  }

  if (!this->set_single_reset_disable_(true)) {
    ESP_LOGW(TAG, "Power button single-click reset disable failed");
  } else {
    ESP_LOGI(TAG, "Power button: single-click reset DISABLED");
    ESP_LOGI(TAG, "Power button: double-click power-off unchanged");
    const auto single_reset_disabled = this->get_single_reset_disabled_();
    if (single_reset_disabled.has_value() && *single_reset_disabled) {
      ESP_LOGI(TAG, "Power button: single-reset-disabled readback=true (BTN_CFG_1[0])");
    } else if (single_reset_disabled.has_value()) {
      ESP_LOGW(TAG, "Power button: single-reset-disabled readback=false (expected true)");
    } else {
      ESP_LOGW(TAG, "Power button: single-reset-disabled readback failed");
    }
  }

  this->refresh_power_and_battery();
}

void M5PM1Component::loop() {
  if (this->irq_pin_ == nullptr) {
    return;
  }

  if (!this->irq_pending_ && this->irq_pin_->digital_read()) {
    return;
  }

  if (this->irq_pending_) {
    this->irq_pending_ = false;
    this->process_irq_();
    return;
  }

  if (!this->irq_pin_->digital_read()) {
    ESP_LOGD(TAG, "IRQ line still low; clearing PM1 IRQ status");
    this->clear_all_irq_status_();
  }
}

void M5PM1Component::process_irq_() {
  uint8_t sys_irq = 0;
  if (!this->read_byte(M5PM1_REG_IRQ_STATUS2, &sys_irq)) {
    ESP_LOGW(TAG, "IRQ_STATUS2 read failed");
    return;
  }

  if (sys_irq == 0) {
    this->clear_all_irq_status_();
    return;
  }

  if (sys_irq & M5PM1_IRQ_SYS_5VIN_INSERT) {
    ESP_LOGI(TAG, "USB IRQ: 5VIN inserted (IRQ_STATUS2 bit0)");
  }
  if (sys_irq & M5PM1_IRQ_SYS_5VIN_REMOVE) {
    ESP_LOGI(TAG, "USB IRQ: 5VIN removed (IRQ_STATUS2 bit1)");
  }

  // irqGetSysStatus(..., M5PM1_CLEAN_ONCE): write-0-to-clear triggered bits.
  const uint8_t clear_val = static_cast<uint8_t>(~sys_irq);
  this->write_byte(M5PM1_REG_IRQ_STATUS2, clear_val);
  this->write_byte(M5PM1_REG_IRQ_STATUS1, 0x00);
  this->write_byte(M5PM1_REG_IRQ_STATUS3, 0x00);

  this->refresh_power_and_battery();
}

bool M5PM1Component::set_status_red_led(bool on) {
  if (!this->update_reg_bit_(M5PM1_REG_PWR_CFG, M5PM1_PWR_CFG_LED_CTRL, on)) {
    ESP_LOGW(TAG, "PWR_CFG LED_CTRL write failed");
    return false;
  }
  ESP_LOGD(TAG, "Red LED -> M5PM1 PWR_CFG[4] %s", on ? "ON" : "OFF");
  return true;
}

optional<uint16_t> M5PM1Component::read_vbat_mv_() {
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

void M5PM1Component::refresh_power_and_battery() {
  const auto pwr_src = this->read_pwr_src_();
  if (pwr_src.has_value()) {
    const bool external_power = (*pwr_src & (M5PM1_PWR_SRC_5VIN | M5PM1_PWR_SRC_5VINOUT)) != 0;
    ESP_LOGI(TAG, "PWR_SRC=0x%02X, external_power=%s", *pwr_src, YESNO(external_power));

    if (this->external_power_binary_sensor_ != nullptr) {
      this->external_power_binary_sensor_->publish_state(external_power);
    }
  }

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
}

}  // namespace esphome::m5pm1
