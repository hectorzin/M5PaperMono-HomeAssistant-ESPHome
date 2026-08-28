/*
 * SPDX-FileCopyrightText: 2026 PaperMono Home Assistant contributors
 * SPDX-License-Identifier: MIT
 */
#include "papermono_activity.h"

#include "esphome/components/m5pm1/m5pm1.h"
#include "esphome/core/log.h"

namespace esphome::papermono_activity {

static const char *const TAG = "papermono_activity";

void PaperMonoActivityComponent::setup() {
  if (this->pmu_ == nullptr) {
    ESP_LOGE(TAG, "M5PM1 reference missing");
    return;
  }

  this->pmu_->set_frontlight_level(0);
  this->frontlight_on_ = false;
  this->last_activity_ms_ = 0;
  ESP_LOGI(TAG, "Frontlight init: OFF (boot default)");
  ESP_LOGI(TAG, "Activity timeout: %u ms, on level: %u%%", this->timeout_ms_, this->on_brightness_percent_);
}

void PaperMonoActivityComponent::loop() {
  if (!this->frontlight_on_ || this->timeout_ms_ == 0) {
    return;
  }

  const uint32_t now = millis();
  if (now - this->last_activity_ms_ >= this->timeout_ms_) {
    this->turn_off_timeout_();
  }
}

void PaperMonoActivityComponent::report_activity(ActivitySource source) {
  if (this->pmu_ == nullptr) {
    return;
  }

  const uint32_t now = millis();
  this->last_activity_ms_ = now;

  if (!this->frontlight_on_) {
    this->apply_frontlight_(true, source);
    return;
  }

  if (source == ActivitySource::MOTION && now - this->last_motion_log_ms_ >= 5000) {
    ESP_LOGD(TAG, "Frontlight: kept ON (activity=motion)");
    this->last_motion_log_ms_ = now;
  }
}

void PaperMonoActivityComponent::apply_frontlight_(bool on, ActivitySource source) {
  if (this->pmu_ == nullptr) {
    return;
  }

  if (on) {
    this->pmu_->set_frontlight_level(this->on_brightness_percent_);
    this->frontlight_on_ = true;
    if (source == ActivitySource::MOTION) {
      ESP_LOGI(TAG, "Frontlight: ON (activity=motion)");
      this->last_motion_log_ms_ = millis();
    } else {
      ESP_LOGI(TAG, "Frontlight: ON (activity=touch)");
    }
    return;
  }

  this->pmu_->set_frontlight_level(0);
  this->frontlight_on_ = false;
}

void PaperMonoActivityComponent::turn_off_timeout_() {
  if (!this->frontlight_on_) {
    return;
  }
  this->apply_frontlight_(false, ActivitySource::MOTION);
  ESP_LOGI(TAG, "Frontlight: OFF (timeout)");
}

}  // namespace esphome::papermono_activity
