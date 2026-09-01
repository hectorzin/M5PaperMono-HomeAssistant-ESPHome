/*
 * SPDX-FileCopyrightText: 2026 PaperMono Home Assistant contributors
 * SPDX-License-Identifier: MIT
 *
 * M5PM1 register map derived from M5Stack M5PM1 (MIT) and M5Unified Power_Class.cpp (MIT).
 */
#pragma once

#include <functional>

#include "esp_err.h"

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/core/preferences.h"

namespace esphome::m5pm1 {

struct LightSleepWakeupArmResult {
  esp_err_t disable_timer{ESP_OK};
  esp_err_t disable_gpio{ESP_OK};
  esp_err_t disable_ext1{ESP_OK};
  esp_err_t gpio_disable{ESP_OK};
  esp_err_t timer{ESP_OK};
  esp_err_t ext1{ESP_OK};
  bool gpio1_high{true};
};

// M5PM1 WAKE_SRC (reg 0x05) classification after M5PM1 shutdown wake.
enum class Pm5BootWakeSource : uint8_t {
  UNKNOWN = 0,
  NORMAL = 1,
  RTC_GPIO0 = 2,
  MOTION_GPIO4 = 3,
  POWER_BUTTON = 4,
};

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

  // M5PM1::setLedEnLevel() — PWR_CFG[4] LED_EN. Official PaperMono L1 shutdown step only.
  bool set_led_en_level(bool level);

  // PaperMono frontlight via M5PM1 GPIO3 / PWM0 (M5GFX Light_M5PaperMono).
  bool set_frontlight_level(uint8_t percent);
  // Rebuild GPIO3 mux/PWM0 after M5PM1 shutdown wake (RTC/MOTION). Leaves brightness at 0%.
  bool recover_frontlight_after_pmic_wake();
  bool is_frontlight_recovery_failed() const { return this->frontlight_recovery_failed_; }

  // True after M5PM1 shutdown wake: EXT_WAKE (RTC/MOTION GPIO) or PWRBTN with shutdown-pending marker.
  bool is_boot_from_pmic_shutdown() const { return this->boot_from_pmic_shutdown_; }
  bool is_boot_shutdown_pending() const { return this->boot_shutdown_pending_; }

  void note_epd_recovery_attempted();
  void note_epd_recovery_result(bool ok, const char *failure_stage);
  void note_frontlight_recovery_attempted();
  void note_frontlight_recovery_result(bool ok);
  void note_epd_mandatory_full_started();
  void note_epd_mandatory_full_completed(uint32_t duration_ms);
  void note_epd_normal_full_after_mandatory();
  void log_pmic_wake_recovery_summary_if_needed();
  void log_pmic_boot_cause() const;
  void log_post_shutdown_power_readback();
  void clear_shutdown_pending();

  // BMI270 INT1 -> M5PM1 GPIO4 -> PY_IRQ (ESP32 GPIO1).
  bool configure_imu_irq_route_();
  void set_motion_handler(std::function<bool()> handler) { this->motion_handler_ = std::move(handler); }
  void set_power_button_handler(std::function<void()> handler) { this->power_button_handler_ = std::move(handler); }

  // Read PWR_SRC + VBAT and publish linked sensors. Safe to call from the main loop only.
  void refresh_power_and_battery();

  // ESP32-S3 light sleep: PY_IRQ (GPIO1) via EXT1 ANY_LOW + RTC timer wake.
  LightSleepWakeupArmResult arm_light_sleep_wakeup(uint64_t timer_us);
  void restore_after_light_sleep();
  // Main-loop or post-wake IRQ dispatch. Returns true when BMI270 any-motion was handled.
  bool process_pending_irq();
  // Clear PMIC IRQ state and verify PY_IRQ is idle-high before light sleep entry.
  bool prepare_light_sleep_entry();

  // Read WAKE_SRC + IRQ_STATUS1 once at boot before any PMIC IRQ clearing.
  void capture_boot_wake_registers_();
  Pm5BootWakeSource get_boot_wake_source() const { return this->boot_wake_source_; }
  uint8_t get_boot_wake_src_raw() const { return this->boot_wake_src_raw_; }
  uint8_t get_boot_irq_status1_raw() const { return this->boot_irq_status1_raw_; }
  uint8_t get_boot_irq_status3_raw() const { return this->boot_irq_status3_raw_; }
  void set_boot_wake_source(Pm5BootWakeSource source) { this->boot_wake_source_ = source; }
  void clear_wake_source(uint8_t mask);

  // M5PM1 GPIO0 (RTC nIRQ) / GPIO4 (BMI270 INT1) falling-edge wake for shutdown.
  bool configure_shutdown_wake_gpio0_falling();
  bool configure_shutdown_wake_gpio4_falling();
  bool is_gpio_input_low(uint8_t pin);

  // L1 hold + shutdown (M5PaperMono official sequence).
  bool set_ldo_enable(bool enable);
  bool ldo_set_power_hold(bool enable);
  bool execute_shutdown();

  optional<uint8_t> get_last_battery_level_percent() const { return this->last_battery_level_; }
  bool is_external_power_present() const { return this->last_external_power_; }

 protected:
  static void IRAM_ATTR irq_isr_(M5PM1Component *arg);

  bool update_reg_bit_(uint8_t reg, uint8_t mask, bool set);
  bool configure_gpio0_rtc_input_();
  bool configure_gpio1_irq_output_();
  bool configure_gpio4_imu_input_();
  bool configure_usb_irq_masks_();
  bool configure_frontlight_pwm_hw_();
  bool set_single_reset_disable_(bool disable);
  optional<bool> get_single_reset_disabled_();
  bool process_irq_();
  void clear_all_irq_status_();
  optional<uint16_t> read_vbat_mv_();
  optional<uint8_t> read_pwr_src_();
  int battery_level_from_mv_(uint16_t mv) const;
  bool gpio_set_wake_enable_(uint8_t pin, bool enable);
  bool gpio_set_wake_edge_falling_(uint8_t pin);
  void mark_shutdown_pending_();
  bool load_shutdown_pending_();

  InternalGPIOPin *irq_pin_{nullptr};
  volatile bool irq_pending_{false};
  sensor::Sensor *battery_voltage_sensor_{nullptr};
  sensor::Sensor *battery_level_sensor_{nullptr};
  binary_sensor::BinarySensor *external_power_binary_sensor_{nullptr};
  std::function<bool()> motion_handler_;
  std::function<void()> power_button_handler_;
  bool frontlight_hw_ready_{false};
  bool frontlight_recovery_failed_{false};
  bool boot_from_pmic_shutdown_{false};
  bool boot_shutdown_pending_{false};
  bool pmic_wake_detected_{false};
  ESPPreferenceObject shutdown_pending_pref_{};
  bool epd_recovery_attempted_{false};
  bool epd_recovery_ok_{false};
  const char *epd_recovery_failure_stage_{nullptr};
  bool frontlight_recovery_attempted_{false};
  bool frontlight_recovery_ok_{false};
  bool recovery_summary_logged_{false};
  uint8_t mandatory_full_started_count_{0};
  uint8_t mandatory_full_completed_count_{0};
  uint32_t mandatory_full_completed_ms_{0};
  uint8_t normal_full_after_mandatory_count_{0};
  Pm5BootWakeSource boot_wake_source_{Pm5BootWakeSource::UNKNOWN};
  uint8_t boot_wake_src_raw_{0};
  uint8_t boot_irq_status1_raw_{0};
  uint8_t boot_irq_status3_raw_{0};
  bool boot_wake_source_captured_{false};
  optional<uint8_t> last_battery_level_{};
  bool last_external_power_{false};
};

}  // namespace esphome::m5pm1
