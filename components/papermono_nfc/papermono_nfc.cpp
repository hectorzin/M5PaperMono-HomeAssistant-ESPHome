/*
 * PaperMono ESPHome integration for the official M5Stack NFC-A driver.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 M5Stack Technology CO LTD (vendorized NFC-A core)
 */
#include "papermono_nfc.h"

#include "esphome/components/m5ioe1/m5ioe1.h"
#include "esphome/components/papermono_activity/papermono_activity.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::papermono_nfc {
static const char *const TAG = "papermono_nfc";

void PaperMonoNfc::setup() {
  ESP_LOGI(TAG, "PaperMono NFC ST25R3916 setup");
  if (m5ioe1_ == nullptr) {
    ESP_LOGE(TAG, "M5IOE1 dependency is missing");
    mark_failed();
    return;
  }

  // PaperMono hardware integration remains outside the vendorized driver.
  m5ioe1_->pin_mode(4, gpio::FLAG_OUTPUT);
  m5ioe1_->set_pin_output_level(4, true);  // PYB_NFC_EN
  delay(10);

  if (!driver_.begin()) {
    ESP_LOGE(TAG, "Official ST25R3916 NFC-A initialization failed");
    mark_failed();
    return;
  }
  initialized_ = true;
  ESP_LOGI(TAG, "Official M5Unit-NFC NFC-A driver initialized");
}

void PaperMonoNfc::prepare_for_light_sleep() {
  if (m5ioe1_ == nullptr) return;
  initialized_ = false;
  tag_latched_ = false;
  m5ioe1_->set_pin_output_level(4, false);
  ESP_LOGI(TAG, "NFC power OFF before light sleep (M5IOE1 GPIO4 LOW)");
}

void PaperMonoNfc::resume_after_user_wake() {
  if (m5ioe1_ == nullptr || initialized_) return;
  m5ioe1_->pin_mode(4, gpio::FLAG_OUTPUT);
  m5ioe1_->set_pin_output_level(4, true);
  delay(10);
  if (!driver_.begin()) {
    ESP_LOGW(TAG, "NFC reinitialization after user wake failed");
    return;
  }
  initialized_ = true;
  last_poll_ = millis();
  ESP_LOGI(TAG, "NFC power ON and ST25R3916 reinitialized after user wake");
}

void PaperMonoNfc::loop() {
  const uint32_t now = millis();
  if (!initialized_ || activity_ == nullptr || !activity_->nfc_polling_allowed() ||
      static_cast<uint32_t>(now - last_poll_) < 1000U) return;
  last_poll_ = now;

  std::string uid;
  if (driver_.poll_uid(uid)) {
    absent_since_ = 0;
    if (!tag_latched_ || uid != last_uid_) {
      ESP_LOGI(TAG, "NFC detected UID=%s", uid.c_str());
      last_uid_ = uid;
      tag_latched_ = true;
      if (uid_sensor_ != nullptr) uid_sensor_->publish_state(last_uid_);
    }
  } else if (tag_latched_) {
    if (absent_since_ == 0) absent_since_ = now;
    if (static_cast<uint32_t>(now - absent_since_) >= 2000U) {
      tag_latched_ = false;
      driver_.rearm();
    }
  }
}

void PaperMonoNfc::dump_config() {
  ESP_LOGCONFIG(TAG, "PaperMono NFC ST25R3916, I2C address 0x%02X, official NFC-A driver", get_i2c_address());
}

}  // namespace esphome::papermono_nfc
