/*
 * PaperMono ESPHome integration for the official M5Stack NFC-A driver.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 M5Stack Technology CO LTD (vendorized NFC-A core)
 */
#include "papermono_nfc.h"

#include "esphome/components/controls/controls.h"
#include "esphome/components/m5ioe1/m5ioe1.h"
#include "esphome/components/papermono_activity/papermono_activity.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

// ESPHome local components compile their canonical component source directly.
// Include the vendorized translation unit here so the official core is linked
// without introducing a separate PlatformIO library or dependency.
#include "st25r3916_official.cpp"

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

void PaperMonoNfc::loop() {
  const uint32_t now = millis();
  if (!initialized_ || static_cast<uint32_t>(now - last_poll_) < 100U) return;
  last_poll_ = now;

  std::string uid;
  if (driver_.poll_uid(uid)) {
    absent_since_ = 0;
    if (!tag_latched_ || uid != last_uid_) {
      ESP_LOGI(TAG, "NFC detected UID=%s", uid.c_str());
      last_uid_ = uid;
      tag_latched_ = true;
      if (controls_ != nullptr && activity_ != nullptr) {
        const int block = controls_->block_for_nfc_uid(uid);
        if (block >= 0) {
          const int page = controls_->first_page_for_block(block);
          if (page >= 0) activity_->request_controls_entry(page);
        } else {
          ESP_LOGI(TAG, "NFC UID is not configured in controls");
        }
      }
    }
  } else if (tag_latched_) {
    if (absent_since_ == 0) absent_since_ = now;
    if (static_cast<uint32_t>(now - absent_since_) >= 300U) {
      tag_latched_ = false;
      driver_.rearm();
    }
  }
}

void PaperMonoNfc::dump_config() {
  ESP_LOGCONFIG(TAG, "PaperMono NFC ST25R3916, I2C address 0x%02X, official NFC-A driver", get_i2c_address());
}

}  // namespace esphome::papermono_nfc
