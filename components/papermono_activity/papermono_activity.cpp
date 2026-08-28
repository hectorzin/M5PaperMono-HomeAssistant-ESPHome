/*
 * SPDX-FileCopyrightText: 2026 PaperMono Home Assistant contributors
 * SPDX-License-Identifier: MIT
 */
#include "papermono_activity.h"

#include "esphome/components/globals/globals_component.h"
#include "esphome/components/m5pm1/m5pm1.h"
#include "esphome/components/papermono_epaper/papermono_epaper.h"
#include "esphome/core/log.h"

namespace esphome::papermono_activity {

static const char *const TAG = "papermono_activity";

// Screensaver: auto-FULL disabled (full_update_every=0).
static constexpr uint8_t SCREENSAVER_FULL_EVERY = 0;
// Controls: soft auto-FULL threshold while interacting.
static constexpr uint8_t CONTROLS_FULL_EVERY = 15;
// Opportunistic FULL on pickup / enter controls when ghosting is visible.
static constexpr uint8_t PICKUP_FULL_THRESHOLD = 8;
static constexpr uint8_t CONTROLS_ENTER_FULL_THRESHOLD = 8;
static constexpr uint8_t CONTROLS_EXIT_FULL_THRESHOLD = 10;

void PaperMonoActivityComponent::setup() {
  if (this->pmu_ == nullptr) {
    ESP_LOGE(TAG, "M5PM1 reference missing");
    return;
  }

  this->pmu_->set_frontlight_level(0);
  this->frontlight_on_ = false;
  this->activity_active_ = false;
  this->pickup_cleanup_pending_ = false;
  this->last_activity_ms_ = 0;
  this->request_screensaver_refresh_policy_();
  ESP_LOGI(TAG, "Frontlight init: OFF (boot default)");
  ESP_LOGI(TAG, "Activity timeout: %u ms, on level: %u%%", this->timeout_ms_, this->on_brightness_percent_);
}

void PaperMonoActivityComponent::loop() {
  if (this->pickup_cleanup_pending_ && this->display_ != nullptr && this->display_->is_idle()) {
    this->pickup_cleanup_pending_ = false;
  }

  if (!this->activity_active_ || this->timeout_ms_ == 0) {
    return;
  }

  const uint32_t now = millis();
  if (now - this->last_activity_ms_ >= this->timeout_ms_) {
    this->turn_off_timeout_();
  }
}

bool PaperMonoActivityComponent::in_controls_view_() const {
  return this->controls_view_ != nullptr && this->controls_view_->value();
}

void PaperMonoActivityComponent::request_screensaver_refresh_policy_() {
  if (this->display_ == nullptr) {
    return;
  }
  this->display_->set_full_update_every(SCREENSAVER_FULL_EVERY);
}

void PaperMonoActivityComponent::request_controls_refresh_policy_() {
  if (this->display_ == nullptr) {
    return;
  }
  this->display_->set_full_update_every(CONTROLS_FULL_EVERY);
}

void PaperMonoActivityComponent::on_pickup_transition_() {
  if (this->display_ == nullptr) {
    return;
  }
  if (this->in_controls_view_()) {
    return;
  }

  const uint8_t count = this->display_->get_partial_count();
  if (count >= PICKUP_FULL_THRESHOLD) {
    ESP_LOGI(TAG, "Pickup: partial_count=%u -> FULL cleanup", count);
    this->pickup_cleanup_pending_ = true;
    this->display_->update();
    return;
  }

  ESP_LOGI(TAG, "Pickup: partial_count=%u -> no refresh", count);
}

void PaperMonoActivityComponent::enter_controls() {
  if (this->display_ == nullptr) {
    return;
  }

  this->request_controls_refresh_policy_();

  if (this->pickup_cleanup_pending_) {
    ESP_LOGI(TAG, "Pickup cleanup already pending -> Controls PARTIAL only");
    this->display_->update_partial(0, 0, 480, 800);
    return;
  }

  const uint8_t count = this->display_->get_partial_count();
  if (count >= CONTROLS_ENTER_FULL_THRESHOLD) {
    ESP_LOGI(TAG, "Enter controls: partial_count=%u -> FULL", count);
    this->display_->update();
    return;
  }

  ESP_LOGI(TAG, "Enter controls: partial_count=%u -> PARTIAL", count);
  this->display_->update_partial(0, 0, 480, 800);
}

void PaperMonoActivityComponent::exit_controls() {
  if (this->display_ == nullptr) {
    return;
  }

  const uint8_t count = this->display_->get_partial_count();
  if (count >= CONTROLS_EXIT_FULL_THRESHOLD) {
    ESP_LOGI(TAG, "Exit controls: partial_count=%u -> FULL cleanup", count);
    this->display_->update();
  } else {
    ESP_LOGI(TAG, "Exit controls: partial_count=%u -> PARTIAL", count);
    this->display_->update_partial(0, 0, 480, 800);
  }

  this->request_screensaver_refresh_policy_();
}

void PaperMonoActivityComponent::report_activity(ActivitySource source) {
  if (this->pmu_ == nullptr) {
    return;
  }

  const uint32_t now = millis();
  this->last_activity_ms_ = now;

  if (!this->activity_active_) {
    this->activity_active_ = true;
    this->apply_frontlight_(true, source);
    if (source == ActivitySource::MOTION) {
      this->on_pickup_transition_();
    }
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
    this->activity_active_ = false;
    return;
  }
  this->apply_frontlight_(false, ActivitySource::MOTION);
  this->activity_active_ = false;
  ESP_LOGI(TAG, "Frontlight: OFF (timeout)");
}

}  // namespace esphome::papermono_activity
