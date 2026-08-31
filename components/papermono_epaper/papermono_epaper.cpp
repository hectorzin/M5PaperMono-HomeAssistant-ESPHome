/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD (OTP command sequences)
 * SPDX-FileCopyrightText: 2026 PaperMono Home Assistant contributors (ESPHome integration)
 * SPDX-License-Identifier: MIT
 *
 * OTP full/partial/reset/deep-sleep sequences and RAM window addressing are
 * derived from M5Stack PaperMono OTP LUT Demo:
 *   components/EDP_OTP_LUT_demo/src/EDP_OTP_LUT_demo.cpp
 *   components/EDP_OTP_LUT_demo/src/EDP_SPI.cpp
 */
#include "papermono_epaper.h"

#include "esphome/components/m5ioe1/m5ioe1.h"
#include "esphome/components/m5pm1/m5pm1.h"
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cstring>

namespace esphome::papermono_epaper {

static const char *const TAG = "papermono_epaper";

static constexpr uint8_t AUTO_FULL_THRESHOLD = 10;
static constexpr uint8_t USER_FULL_THRESHOLD = 15;

// SSD1677 commands used by the PaperMono OTP demo.
static constexpr uint8_t CMD_DRIVER_OUTPUT = 0x01;
static constexpr uint8_t CMD_BOOSTER = 0x0C;
static constexpr uint8_t CMD_DEEP_SLEEP = 0x10;
static constexpr uint8_t CMD_DATA_ENTRY_MODE = 0x11;
static constexpr uint8_t CMD_SOFT_RESET = 0x12;
static constexpr uint8_t CMD_TEMP_SENSOR = 0x18;
static constexpr uint8_t CMD_MASTER_ACTIVATION = 0x20;
static constexpr uint8_t CMD_DISPLAY_UPDATE_1 = 0x21;
static constexpr uint8_t CMD_UPDATE_CONTROL = 0x22;
static constexpr uint8_t CMD_WRITE_RAM_1 = 0x24;
static constexpr uint8_t CMD_WRITE_RAM_2 = 0x26;
static constexpr uint8_t CMD_BORDER = 0x3C;
static constexpr uint8_t CMD_RAM_X_RANGE = 0x44;
static constexpr uint8_t CMD_RAM_Y_RANGE = 0x45;
static constexpr uint8_t CMD_RAM_X_COUNTER = 0x4E;
static constexpr uint8_t CMD_RAM_Y_COUNTER = 0x4F;

// M5IOE1 ESPHome pin numbers (see packages/hardware.yaml).
// M5IOE1 sits on L2/L3A; it enables L3B rails for EPD/touch after PMIC wake.
static constexpr uint8_t EPD_POWER_IOE_PIN = 2;
static constexpr uint8_t TOUCH_POWER_IOE_PIN = 12;
static constexpr uint8_t TOUCH_RESET_IOE_PIN = 5;

static void note_epd_recovery_result_(m5pm1::M5PM1Component *pmu, bool ok, const char *failure_stage) {
  if (pmu == nullptr) {
    return;
  }
  pmu->note_epd_recovery_result(ok, failure_stage);
}

const char *PaperMonoEpaper::busy_level_label_(bool busy) { return busy ? "HIGH(busy)" : "LOW(idle)"; }

bool PaperMonoEpaper::wait_busy_idle_(uint32_t timeout_ms) {
  const uint32_t deadline = millis() + timeout_ms;
  while (!this->is_idle_()) {
    if (millis() >= deadline) {
      return false;
    }
    delay(10);
  }
  return true;
}

void PaperMonoEpaper::begin_pmic_wake_recovery(m5pm1::M5PM1Component *pmu) {
  if (this->recovery_pending_ || this->pmic_recovery_attempted_) {
    return;
  }
  if (pmu != nullptr && pmu->is_boot_from_pmic_shutdown()) {
    this->arm_pmic_initial_full();
  }
  this->recovery_pending_ = true;
  this->pmic_recovery_attempted_ = true;
  this->hw_ready_for_refresh_ = false;
  this->hw_recovery_failed_ = false;
  if (pmu != nullptr) {
    pmu->note_epd_recovery_attempted();
  }
}

void PaperMonoEpaper::arm_pmic_initial_full() {
  if (this->pmic_mandatory_full_done_ || this->pmic_initial_full_required_)
    return;
  this->pmic_initial_full_required_ = true;
  ESP_LOGI(TAG, "PMIC boot: blocking refresh until initial FULL");
  ESP_LOGI(TAG, "EPD: initial_full_required=yes");
}

bool PaperMonoEpaper::must_force_pmic_mandatory_full_() const {
  if (this->pmic_mandatory_full_done_) {
    return false;
  }
  if (this->pmic_initial_full_required_) {
    return true;
  }
  return this->pmu_ != nullptr && this->pmu_->is_boot_from_pmic_shutdown();
}

void PaperMonoEpaper::sync_pmic_mandatory_full_gate_() {
  if (this->pmic_mandatory_full_done_) {
    return;
  }
  if (this->pmu_ != nullptr && this->pmu_->is_boot_from_pmic_shutdown() && !this->pmic_initial_full_required_) {
    ESP_LOGI(TAG, "EPD: late PMIC mandatory FULL gate armed from WAKE_SRC");
    this->pmic_initial_full_required_ = true;
  }
}

bool PaperMonoEpaper::enforce_pmic_mandatory_full_gate_(const char *stage) {
  this->sync_pmic_mandatory_full_gate_();
  if (!this->must_force_pmic_mandatory_full_()) {
    return false;
  }
  if (!this->full_refresh_) {
    if (!this->pmic_partial_blocked_logged_) {
      ESP_LOGI(TAG, "PMIC boot: PARTIAL request deferred until mandatory FULL (%s)", stage);
      this->pmic_partial_blocked_logged_ = true;
    }
    this->full_refresh_ = true;
    return true;
  }
  return false;
}

void PaperMonoEpaper::log_physical_refresh_commit_(const char *stage) const {
  ESP_LOGI(TAG, "EPD physical refresh request (%s):", stage);
  ESP_LOGI(TAG, "  mode=%s", this->full_refresh_ ? "FULL" : "PARTIAL");
  ESP_LOGI(TAG, "  source=%s", this->refresh_source_ != nullptr ? this->refresh_source_ : "unknown");
  ESP_LOGI(TAG, "  pmic_initial_full_required=%s", this->pmic_initial_full_required_ ? "yes" : "no");
  ESP_LOGI(TAG, "  pmic_initial_full_started=%s", this->pmic_initial_full_started_ ? "yes" : "no");
  ESP_LOGI(TAG, "  pmic_mandatory_full_done=%s", this->pmic_mandatory_full_done_ ? "yes" : "no");
  ESP_LOGI(TAG, "  hw_ready=%s", this->hw_ready_for_refresh_ ? "yes" : "no");
  ESP_LOGI(TAG, "  recovery_pending=%s", this->recovery_pending_ ? "yes" : "no");
  ESP_LOGI(TAG, "  baseline_ready=%s", this->baseline_ready_ ? "yes" : "no");
}

void PaperMonoEpaper::fail_pmic_wake_recovery(m5pm1::M5PM1Component *pmu, const char *failure_stage) {
  this->recovery_pending_ = false;
  this->hw_ready_for_refresh_ = false;
  this->hw_recovery_failed_ = true;
  note_epd_recovery_result_(pmu, false, failure_stage);
}

bool PaperMonoEpaper::run_pmic_wake_epd_hardware_recovery(m5ioe1::M5IOE1Component *ioe, m5pm1::M5PM1Component *pmu) {
  if (ioe == nullptr || !ioe->probe_device_responding()) {
    ESP_LOGE(TAG, "PMIC wake: M5IOE1 unavailable");
    this->fail_pmic_wake_recovery(pmu, "M5IOE1 unavailable");
    return false;
  }

  ESP_LOGI(TAG, "PMIC wake: recovering EPD hardware");

  ioe->set_pin_output_level(EPD_POWER_IOE_PIN, true);
  ESP_LOGI(TAG, "EPD power ON");
  ioe->set_pin_output_level(TOUCH_POWER_IOE_PIN, true);
  ESP_LOGI(TAG, "Touch power ON");

  delay(PMIC_WAKE_POWER_SETTLE_MS);

  const bool busy_before = !this->is_idle_();
  ESP_LOGI(TAG, "EPD BUSY before reset: %s", busy_level_label_(busy_before));

  ioe->set_pin_output_level(TOUCH_RESET_IOE_PIN, false);
  delay(10);
  ioe->set_pin_output_level(TOUCH_RESET_IOE_PIN, true);
  ESP_LOGI(TAG, "Touch reset complete");

  this->hardware_reset_begin_(false);
  delay(10);
  this->hardware_reset_begin_(true);
  delay(10);
  ESP_LOGI(TAG, "EPD reset complete");

  if (!this->wait_busy_idle_(PMIC_WAKE_BUSY_WAIT_MS)) {
    ESP_LOGE(TAG, "EPD BUSY after hardware reset: %s (recovery failed)", busy_level_label_(!this->is_idle_()));
    this->fail_pmic_wake_recovery(pmu, "BUSY wait");
    return false;
  }

  ESP_LOGI(TAG, "EPD BUSY after hardware reset: %s", busy_level_label_(false));
  ESP_LOGI(TAG, "PMIC wake: EPD ready");
  note_epd_recovery_result_(pmu, true, nullptr);
  this->baseline_ready_ = false;
  this->partial_count_ = 0;
  this->recovery_pending_ = false;
  this->hw_ready_for_refresh_ = true;
  return true;
}

void PaperMonoEpaper::setup() {
  RAMAllocator<uint8_t> allocator;
  this->buffer_ = allocator.allocate(FRAME_SIZE);
  if (this->buffer_ == nullptr) {
    this->mark_failed(LOG_STR("Failed to allocate 800x480 frame buffer"));
    return;
  }
  std::memset(this->buffer_, 0xFF, FRAME_SIZE);

  this->dc_pin_->setup();
  this->dc_pin_->digital_write(false);
  this->reset_pin_->setup();
  this->reset_pin_->digital_write(true);
  this->busy_pin_->setup();
  this->spi_setup();
  this->disable_loop();

  if (this->pmu_ != nullptr && this->pmu_->is_boot_from_pmic_shutdown()) {
    this->arm_pmic_initial_full();
  }
  ESP_LOGI(TAG, "EPD setup: initial_full_required=%s", this->pmic_initial_full_required_ ? "yes" : "no");
}

void PaperMonoEpaper::dump_config() {
  LOG_DISPLAY("", "PaperMono E-Paper", this);
  if (this->full_update_every_ == 0) {
    ESP_LOGCONFIG(TAG,
                  "  Native: %ux%u\n"
                  "  Full update every: legacy disabled (central policy: auto=%u user=%u)\n"
                  "  Mirror X: %s\n"
                  "  Mirror Y: %s",
                  NATIVE_WIDTH, NATIVE_HEIGHT, AUTO_FULL_THRESHOLD, USER_FULL_THRESHOLD,
                  YESNO(this->transform_ & TRANSFORM_MIRROR_X), YESNO(this->transform_ & TRANSFORM_MIRROR_Y));
  } else {
    ESP_LOGCONFIG(TAG,
                  "  Native: %ux%u\n"
                  "  Full update every: legacy value %u (ignored; central policy: auto=%u user=%u)\n"
                  "  Mirror X: %s\n"
                  "  Mirror Y: %s",
                  NATIVE_WIDTH, NATIVE_HEIGHT, this->full_update_every_, AUTO_FULL_THRESHOLD, USER_FULL_THRESHOLD,
                  YESNO(this->transform_ & TRANSFORM_MIRROR_X), YESNO(this->transform_ & TRANSFORM_MIRROR_Y));
  }
  LOG_PIN("  CS Pin: ", this->cs_);
  LOG_PIN("  DC Pin: ", this->dc_pin_);
  LOG_PIN("  Reset Pin: ", this->reset_pin_);
  LOG_PIN("  Busy Pin: ", this->busy_pin_);
  LOG_UPDATE_INTERVAL(this);
}

int PaperMonoEpaper::get_width() {
  return this->effective_transform_ & TRANSFORM_SWAP_XY ? NATIVE_HEIGHT : NATIVE_WIDTH;
}

int PaperMonoEpaper::get_height() {
  return this->effective_transform_ & TRANSFORM_SWAP_XY ? NATIVE_WIDTH : NATIVE_HEIGHT;
}

void PaperMonoEpaper::fill(Color color) {
  if (this->get_clipping().is_set()) {
    display::Display::fill(color);
    return;
  }
  std::memset(this->buffer_, this->color_to_bit_(color) ? 0xFF : 0x00, FRAME_SIZE);
}

void PaperMonoEpaper::clear() { this->fill(display::COLOR_ON); }

void PaperMonoEpaper::update_effective_transform_() {
  switch (this->rotation_) {
    case display::DISPLAY_ROTATION_90_DEGREES:
      this->effective_transform_ = this->transform_ ^ (TRANSFORM_SWAP_XY | TRANSFORM_MIRROR_X);
      break;
    case display::DISPLAY_ROTATION_180_DEGREES:
      this->effective_transform_ = this->transform_ ^ (TRANSFORM_MIRROR_X | TRANSFORM_MIRROR_Y);
      break;
    case display::DISPLAY_ROTATION_270_DEGREES:
      this->effective_transform_ = this->transform_ ^ (TRANSFORM_SWAP_XY | TRANSFORM_MIRROR_Y);
      break;
    default:
      this->effective_transform_ = this->transform_;
      break;
  }
}

bool PaperMonoEpaper::rotate_point_(int &x, int &y) const {
  if (this->effective_transform_ & TRANSFORM_SWAP_XY)
    std::swap(x, y);
  if (this->effective_transform_ & TRANSFORM_MIRROR_X)
    x = NATIVE_WIDTH - x - 1;
  if (this->effective_transform_ & TRANSFORM_MIRROR_Y)
    y = NATIVE_HEIGHT - y - 1;
  return x >= 0 && y >= 0 && x < NATIVE_WIDTH && y < NATIVE_HEIGHT;
}

void HOT PaperMonoEpaper::draw_pixel_at(int x, int y, Color color) {
  if (!this->get_clipping().inside(x, y))
    return;
  if (!this->rotate_point_(x, y))
    return;
  const size_t byte_position = static_cast<size_t>(y) * BYTES_PER_ROW + static_cast<size_t>(x) / 8;
  if (byte_position >= FRAME_SIZE)
    return;
  const uint8_t pixel_bit = static_cast<uint8_t>(0x80 >> (x % 8));
  if (this->color_to_bit_(color) == 0) {
    this->buffer_[byte_position] = static_cast<uint8_t>(this->buffer_[byte_position] & ~pixel_bit);
  } else {
    this->buffer_[byte_position] = static_cast<uint8_t>(this->buffer_[byte_position] | pixel_bit);
  }
}

void PaperMonoEpaper::add_partial_region(int x, int y, int width, int height) {
  if (width <= 0 || height <= 0)
    return;
  if (this->logical_region_count_ >= MAX_PARTIAL_REGIONS) {
    ESP_LOGW(TAG, "partial region overflow, merging into last region");
    Rect &last = this->logical_regions_[MAX_PARTIAL_REGIONS - 1];
    const int x2 = std::max(last.x + last.w, x + width);
    const int y2 = std::max(last.y + last.h, y + height);
    last.x = static_cast<int16_t>(std::min<int>(last.x, x));
    last.y = static_cast<int16_t>(std::min<int>(last.y, y));
    last.w = static_cast<int16_t>(x2 - last.x);
    last.h = static_cast<int16_t>(y2 - last.y);
    return;
  }
  Rect &rect = this->logical_regions_[this->logical_region_count_++];
  rect.x = static_cast<int16_t>(x);
  rect.y = static_cast<int16_t>(y);
  rect.w = static_cast<int16_t>(width);
  rect.h = static_cast<int16_t>(height);
}

void PaperMonoEpaper::update() {
  this->request_refresh(RefreshPolicy::AUTOMATIC, RefreshKind::MANDATORY_FULL, "update");
}

void PaperMonoEpaper::update_from_pmic_recovery() {
  this->request_refresh(RefreshPolicy::AUTOMATIC, RefreshKind::MANDATORY_FULL, "pmic_recovery_complete");
}

void PaperMonoEpaper::update_partial(int x, int y, int width, int height) {
  this->add_partial_region(x, y, width, height);
  this->update_partial();
}

void PaperMonoEpaper::update_partial() {
  this->request_refresh(RefreshPolicy::AUTOMATIC, RefreshKind::NORMAL, "update_partial");
}

bool PaperMonoEpaper::is_idle() const { return this->state_ == State::IDLE; }

bool PaperMonoEpaper::has_refresh_pending() const {
  return this->must_force_pmic_mandatory_full_() || this->pending_refresh_.active;
}

bool PaperMonoEpaper::has_baseline() const { return this->baseline_ready_; }

const char *PaperMonoEpaper::policy_label_(RefreshPolicy policy) {
  return policy == RefreshPolicy::USER_INTERACTION ? "USER" : "AUTOMATIC";
}

uint8_t PaperMonoEpaper::policy_threshold_(RefreshPolicy policy) {
  return policy == RefreshPolicy::USER_INTERACTION ? USER_FULL_THRESHOLD : AUTO_FULL_THRESHOLD;
}

RefreshPolicy PaperMonoEpaper::merge_pending_policy_(RefreshPolicy existing, RefreshPolicy incoming) {
  // Mixed pending requests use the lowest threshold (AUTOMATIC=10 over USER=15).
  if (existing == RefreshPolicy::AUTOMATIC || incoming == RefreshPolicy::AUTOMATIC) {
    return RefreshPolicy::AUTOMATIC;
  }
  return RefreshPolicy::USER_INTERACTION;
}

RefreshKind PaperMonoEpaper::merge_pending_kind_(RefreshKind existing, RefreshKind incoming) {
  if (existing == RefreshKind::MANDATORY_FULL || incoming == RefreshKind::MANDATORY_FULL) {
    return RefreshKind::MANDATORY_FULL;
  }
  return RefreshKind::NORMAL;
}

bool PaperMonoEpaper::resolve_refresh_full_(RefreshKind kind, RefreshPolicy policy) const {
  if (kind == RefreshKind::MANDATORY_FULL) {
    return true;
  }
  if (this->must_force_pmic_mandatory_full_()) {
    return true;
  }
  if (!this->baseline_ready_) {
    return true;
  }
  return this->partial_count_ >= policy_threshold_(policy);
}

void PaperMonoEpaper::log_refresh_execute_(const char *source, RefreshPolicy policy, RefreshKind kind,
                                             bool full) const {
  const char *decision;
  if (full) {
    if (kind == RefreshKind::MANDATORY_FULL || this->must_force_pmic_mandatory_full_()) {
      decision = "FULL_MANDATORY";
    } else {
      decision = "FULL_AUTO";
    }
  } else {
    decision = "PARTIAL";
  }
  ESP_LOGI(TAG, "Refresh execute: source=%s policy=%s count=%u decision=%s",
           source != nullptr ? source : "unknown", policy_label_(policy), this->partial_count_, decision);
}

void PaperMonoEpaper::merge_pending_refresh_(RefreshPolicy policy, RefreshKind kind, const char *source) {
  if (!this->pending_refresh_.active) {
    this->pending_refresh_.policy = policy;
    this->pending_refresh_.kind = kind;
    this->pending_refresh_.source = source != nullptr ? source : "unknown";
    this->pending_refresh_.active = true;
    return;
  }
  this->pending_refresh_.policy = merge_pending_policy_(this->pending_refresh_.policy, policy);
  this->pending_refresh_.kind = merge_pending_kind_(this->pending_refresh_.kind, kind);
  if (source != nullptr) {
    this->pending_refresh_.source = source;
  }
}

void PaperMonoEpaper::execute_refresh_request_(RefreshPolicy policy, RefreshKind kind, const char *source) {
  this->refresh_source_ = source != nullptr ? source : "unknown";
  const bool full = this->resolve_refresh_full_(kind, policy);
  this->log_refresh_execute_(this->refresh_source_, policy, kind, full);
  this->begin_refresh_(full);
}

void PaperMonoEpaper::request_refresh(RefreshPolicy policy, RefreshKind kind, const char *source) {
  this->sync_pmic_mandatory_full_gate_();

  if (source != nullptr && (std::strcmp(source, "boot") == 0 || std::strcmp(source, "wifi_transition") == 0 ||
                            std::strcmp(source, "ha_connected") == 0 || std::strcmp(source, "ha_demo_timeout") == 0)) {
    ESP_LOGI(TAG, "Refresh request audit: source=%s state=%u hw_ready=%s baseline=%s pending=%s",
             source, static_cast<unsigned>(this->state_), this->is_hw_ready_for_refresh() ? "yes" : "no",
             this->baseline_ready_ ? "yes" : "no", this->pending_refresh_.active ? "yes" : "no");
  }

  if (this->hw_recovery_failed_) {
    ESP_LOGW(TAG, "refresh blocked: EPD hardware recovery failed");
    return;
  }

  RefreshKind effective_kind = kind;
  if (this->must_force_pmic_mandatory_full_() && kind == RefreshKind::NORMAL) {
    if (!this->pmic_partial_blocked_logged_) {
      ESP_LOGI(TAG, "PMIC boot: PARTIAL request deferred until mandatory FULL (request_refresh)");
      this->pmic_partial_blocked_logged_ = true;
    }
    effective_kind = RefreshKind::MANDATORY_FULL;
  }

  if (this->recovery_pending_ || !this->hw_ready_for_refresh_) {
    if (this->must_force_pmic_mandatory_full_()) {
      this->do_update_();
      this->merge_pending_refresh_(policy, RefreshKind::MANDATORY_FULL, source);
      if (!this->pmic_refresh_deferred_logged_) {
        ESP_LOGI(TAG, "PMIC boot: refresh deferred until initial FULL");
        this->pmic_refresh_deferred_logged_ = true;
      }
      return;
    }
    ESP_LOGW(TAG, "refresh blocked: PMIC wake EPD recovery not complete");
    return;
  }

  if (this->state_ != State::IDLE) {
    ESP_LOGD(TAG, "refresh busy, coalescing pending update");
    this->do_update_();
    this->merge_pending_refresh_(policy, effective_kind, source);
    return;
  }

  this->execute_refresh_request_(policy, effective_kind, source);
}

void PaperMonoEpaper::begin_refresh_(bool full) {
  this->sync_pmic_mandatory_full_gate_();

  if (this->recovery_pending_ || !this->hw_ready_for_refresh_ || this->hw_recovery_failed_) {
    return;
  }
  if (this->must_force_pmic_mandatory_full_()) {
    if (!full && !this->pmic_partial_blocked_logged_) {
      ESP_LOGI(TAG, "PMIC boot: PARTIAL request deferred until mandatory FULL (begin_refresh)");
      this->pmic_partial_blocked_logged_ = true;
    }
    full = true;
    this->pending_refresh_.active = false;
  }
  this->full_refresh_ = full;
  if (full)
    this->logical_region_count_ = 0;
  this->update_start_ms_ = millis();
  this->set_state_(State::UPDATE);
  this->enable_loop();
}

void PaperMonoEpaper::process_pending_refresh_() {
  if (!this->pending_refresh_.active)
    return;

  const RefreshPolicy policy = this->pending_refresh_.policy;
  const RefreshKind kind = this->pending_refresh_.kind;
  const char *source = this->pending_refresh_.source;
  this->pending_refresh_.active = false;
  this->execute_refresh_request_(policy, kind, source);
}

void PaperMonoEpaper::set_state_(State state, uint16_t delay_ms) {
  this->state_ = state;
  this->wait_for_idle_(state > State::SHOULD_WAIT);
  this->delay_until_ = millis() + delay_ms;
  if (state == State::IDLE)
    this->disable_loop();
}

void PaperMonoEpaper::wait_for_idle_(bool should_wait) {
  this->waiting_for_idle_ = should_wait;
  if (should_wait)
    this->busy_wait_start_ = millis();
}

bool PaperMonoEpaper::is_idle_() const {
  if (this->busy_pin_ == nullptr)
    return true;
  return !this->busy_pin_->digital_read();
}

void PaperMonoEpaper::loop() {
  const uint32_t now = millis();
  if (static_cast<int32_t>(now - this->delay_until_) < 0)
    return;

  if (this->waiting_for_idle_) {
    if (this->is_idle_()) {
      this->waiting_for_idle_ = false;
    } else {
      if (now - this->busy_wait_start_ >= BUSY_TIMEOUT_MS) {
        ESP_LOGE(TAG, "BUSY timeout after %u ms in state %u", static_cast<unsigned>(BUSY_TIMEOUT_MS),
                 static_cast<unsigned>(this->state_));
        if (this->pmic_recovery_attempted_) {
          this->hw_recovery_failed_ = true;
          this->hw_ready_for_refresh_ = false;
          ESP_LOGE(TAG, "PMIC wake: EPD refresh failed; staying awake for diagnosis");
        }
        this->send_deep_sleep_();
        this->finish_refresh_(false);
      }
      return;
    }
  }
  this->process_state_();
}

void PaperMonoEpaper::command_(uint8_t cmd) {
  this->dc_pin_->digital_write(false);
  this->enable();
  this->write_byte(cmd);
  this->disable();
}

void PaperMonoEpaper::cmd_data_(uint8_t cmd, const uint8_t *data, size_t length) {
  this->dc_pin_->digital_write(false);
  this->enable();
  this->write_byte(cmd);
  if (length > 0) {
    this->dc_pin_->digital_write(true);
    this->write_array(data, length);
  }
  this->disable();
}

void PaperMonoEpaper::write_u16_(uint16_t value) {
  const uint8_t bytes[] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8)};
  this->write_array(bytes, sizeof(bytes));
}

void PaperMonoEpaper::set_ram_window_(const Rect &rect) {
  const uint16_t x_end = static_cast<uint16_t>(rect.x + rect.w - 1);
  const uint16_t y_end = static_cast<uint16_t>(rect.y + rect.h - 1);
  this->cmd_data_(CMD_DATA_ENTRY_MODE, {0x03});

  this->dc_pin_->digital_write(false);
  this->enable();
  this->write_byte(CMD_RAM_X_RANGE);
  this->dc_pin_->digital_write(true);
  this->write_u16_(static_cast<uint16_t>(rect.x));
  this->write_u16_(x_end);
  this->disable();

  this->dc_pin_->digital_write(false);
  this->enable();
  this->write_byte(CMD_RAM_Y_RANGE);
  this->dc_pin_->digital_write(true);
  this->write_u16_(static_cast<uint16_t>(rect.y));
  this->write_u16_(y_end);
  this->disable();

  this->dc_pin_->digital_write(false);
  this->enable();
  this->write_byte(CMD_RAM_X_COUNTER);
  this->dc_pin_->digital_write(true);
  this->write_u16_(static_cast<uint16_t>(rect.x));
  this->disable();

  this->dc_pin_->digital_write(false);
  this->enable();
  this->write_byte(CMD_RAM_Y_COUNTER);
  this->dc_pin_->digital_write(true);
  this->write_u16_(static_cast<uint16_t>(rect.y));
  this->disable();
}

void PaperMonoEpaper::begin_ram_write_(uint8_t ram_cmd) {
  this->dc_pin_->digital_write(false);
  this->enable();
  this->write_byte(ram_cmd);
  this->dc_pin_->digital_write(true);
  this->ram_write_open_ = true;
}

void PaperMonoEpaper::end_ram_write_() {
  if (this->ram_write_open_) {
    this->disable();
    this->ram_write_open_ = false;
  }
}

bool PaperMonoEpaper::transfer_rect_(const Rect &rect, bool invert) {
  if (rect.empty())
    return true;

  if (this->transfer_row_ == 0) {
    this->set_ram_window_(rect);
    const uint8_t ram_cmd = this->state_ == State::XFER_RAM2 ? CMD_WRITE_RAM_2 : CMD_WRITE_RAM_1;
    this->begin_ram_write_(ram_cmd);
  } else {
    this->dc_pin_->digital_write(true);
    this->enable();
    this->ram_write_open_ = true;
  }

  const uint32_t start = millis();
  const size_t x_offset = static_cast<size_t>(rect.x) / 8;
  const size_t row_length = static_cast<size_t>(rect.w) / 8;
  uint8_t chunk[SPI_CHUNK];

  while (this->transfer_row_ < static_cast<uint16_t>(rect.h)) {
    const size_t src = (static_cast<size_t>(rect.y) + this->transfer_row_) * BYTES_PER_ROW + x_offset;
    if (src + row_length > FRAME_SIZE) {
      ESP_LOGE(TAG, "RAM write clipped at framebuffer end");
      this->end_ram_write_();
      this->transfer_row_ = 0;
      return true;
    }
    size_t remaining = row_length;
    size_t offset = 0;
    while (remaining > 0) {
      const size_t n = std::min(remaining, SPI_CHUNK);
      if (invert) {
        for (size_t i = 0; i < n; i++)
          chunk[i] = static_cast<uint8_t>(~this->buffer_[src + offset + i]);
      } else {
        std::memcpy(chunk, this->buffer_ + src + offset, n);
      }
      this->write_array(chunk, n);
      offset += n;
      remaining -= n;
    }
    this->transfer_row_++;
    if (millis() - start > MAX_TRANSFER_SLICE_MS) {
      this->end_ram_write_();
      return false;
    }
  }

  this->end_ram_write_();
  this->transfer_row_ = 0;
  return true;
}

void PaperMonoEpaper::hardware_reset_begin_(bool level) { this->reset_pin_->digital_write(level); }

void PaperMonoEpaper::send_soft_reset_() { this->command_(CMD_SOFT_RESET); }

void PaperMonoEpaper::init_mono_mode_() {
  // Matches OTP init_mono_mode() after software reset.
  this->cmd_data_(CMD_TEMP_SENSOR, {0x80});
  this->cmd_data_(CMD_BOOSTER, {0xAE, 0xC7, 0xC3, 0xC0, 0x80});
  this->cmd_data_(CMD_DRIVER_OUTPUT, {0xDF, 0x01, 0x02});  // 480 gate outputs (OTP), not ESPHome's width-1
  this->cmd_data_(CMD_BORDER, {0x01});
  this->cmd_data_(CMD_DISPLAY_UPDATE_1, {0x00});
  const Rect full{0, 0, static_cast<int16_t>(NATIVE_WIDTH), static_cast<int16_t>(NATIVE_HEIGHT)};
  this->set_ram_window_(full);
}

void PaperMonoEpaper::send_deep_sleep_() {
  // Deep Sleep Mode 1 keeps RAM, which is the monochrome partial baseline.
  this->cmd_data_(CMD_DEEP_SLEEP, {0x01});
}

void PaperMonoEpaper::finish_refresh_(bool success) {
  this->end_ram_write_();
  if (success) {
    if (this->full_refresh_) {
      this->baseline_ready_ = true;
      this->partial_count_ = 0;
      if (this->pmic_initial_full_started_) {
        this->pmic_initial_full_started_ = false;
        this->pmic_initial_full_required_ = false;
        this->pmic_mandatory_full_done_ = true;
        this->pmic_refresh_deferred_logged_ = false;
        this->pmic_partial_blocked_logged_ = false;
        this->hw_ready_for_refresh_ = true;
        const uint32_t duration_ms = static_cast<uint32_t>(millis() - this->update_start_ms_);
        if (this->pmu_ != nullptr) {
          this->pmu_->note_epd_mandatory_full_completed(duration_ms);
        }
        ESP_LOGI(TAG, "PMIC boot: mandatory initial FULL complete in %u ms; partial refresh enabled",
                 duration_ms);
      }
    } else {
      this->partial_count_++;
      ESP_LOGD(TAG, "partial count %u", this->partial_count_);
    }
  }
  this->logical_region_count_ = 0;
  ESP_LOGI(TAG, "%s refresh took %u ms", this->full_refresh_ ? "FULL" : "PARTIAL",
           static_cast<unsigned>(millis() - this->update_start_ms_));
  this->set_state_(State::IDLE);
  this->process_pending_refresh_();
}

void PaperMonoEpaper::process_state_() {
  const Rect full{0, 0, static_cast<int16_t>(NATIVE_WIDTH), static_cast<int16_t>(NATIVE_HEIGHT)};

  switch (this->state_) {
    case State::IDLE:
      this->disable_loop();
      break;

    case State::UPDATE:
      this->do_update_();
      if (this->enforce_pmic_mandatory_full_gate_("state_update")) {
        this->logical_region_count_ = 0;
      }
      this->log_physical_refresh_commit_("state_update");
      if (this->full_refresh_) {
        if (this->must_force_pmic_mandatory_full_()) {
          if (!this->pmic_initial_full_started_) {
            if (this->pmu_ != nullptr) {
              this->pmu_->note_epd_mandatory_full_started();
            }
            ESP_LOGI(TAG, "PMIC boot: mandatory initial FULL physical start");
          }
          this->pmic_initial_full_started_ = true;
        } else if (this->pmic_mandatory_full_done_) {
          if (this->pmu_ != nullptr) {
            this->pmu_->note_epd_normal_full_after_mandatory();
          }
          ESP_LOGI(TAG, "PMIC boot: normal FULL after mandatory complete (source=%s)",
                   this->refresh_source_ != nullptr ? this->refresh_source_ : "unknown");
        }
        ESP_LOGI(TAG, "FULL refresh OTP Mode 1");
        if (this->pmic_initial_full_started_) {
          ESP_LOGI(TAG, "PMIC boot: starting mandatory initial FULL");
        }
      } else {
        for (uint8_t i = 0; i < this->logical_region_count_; i++) {
          const Rect &logical = this->logical_regions_[i];
          ESP_LOGI(TAG, "requested dirty logical=(%d,%d,%d,%d)", logical.x, logical.y, logical.w, logical.h);
        }
        if (this->logical_region_count_ == 0)
          ESP_LOGI(TAG, "requested dirty logical=(full canvas redraw)");
      }
      this->transfer_row_ = 0;
      this->set_state_(State::RESET_LOW);
      break;

    case State::RESET_LOW:
      this->hardware_reset_begin_(false);
      this->set_state_(State::RESET_HIGH, 10);
      break;

    case State::RESET_HIGH:
      this->hardware_reset_begin_(true);
      this->set_state_(State::WAIT_POST_RESET, 10);
      break;

    case State::WAIT_POST_RESET:
      if (this->enforce_pmic_mandatory_full_gate_("wait_post_reset")) {
        if (this->must_force_pmic_mandatory_full_()) {
          this->pmic_initial_full_started_ = true;
        }
        this->log_physical_refresh_commit_("wait_post_reset_forced_full");
        this->set_state_(State::SOFT_RESET);
      } else if (this->full_refresh_) {
        this->set_state_(State::SOFT_RESET);
      } else {
        this->set_state_(State::PARTIAL_WAKE);
      }
      break;

    case State::SOFT_RESET:
      this->send_soft_reset_();
      this->set_state_(State::INIT_MONO, 10);
      break;

    case State::INIT_MONO:
      this->init_mono_mode_();
      this->set_state_(State::FULL_PHASE1);
      break;

    case State::FULL_PHASE1:
      this->cmd_data_(CMD_UPDATE_CONTROL, {0xF8});
      this->set_state_(State::XFER_RAM1_INV);
      break;

    case State::XFER_RAM1_INV:
      if (!this->transfer_rect_(full, true))
        return;
      this->after_activate_ = State::FULL_PHASE2;
      this->set_state_(State::ACTIVATE);
      break;

    case State::ACTIVATE:
      this->command_(CMD_MASTER_ACTIVATION);
      this->set_state_(State::WAIT_ACTIVATE);
      break;

    case State::WAIT_ACTIVATE:
      this->set_state_(this->after_activate_);
      break;

    case State::FULL_PHASE2:
      this->cmd_data_(CMD_UPDATE_CONTROL, {0x14});
      this->set_state_(State::XFER_RAM2);
      break;

    case State::XFER_RAM2:
      if (!this->transfer_rect_(full, false))
        return;
      this->set_state_(State::XFER_RAM1);
      break;

    case State::XFER_RAM1:
      if (!this->transfer_rect_(full, false))
        return;
      this->after_activate_ = State::DEEP_SLEEP;
      this->set_state_(State::ACTIVATE);
      break;

    case State::PARTIAL_WAKE:
      if (this->enforce_pmic_mandatory_full_gate_("partial_wake")) {
        if (this->must_force_pmic_mandatory_full_()) {
          this->pmic_initial_full_started_ = true;
        }
        this->log_physical_refresh_commit_("partial_wake_forced_full");
        this->transfer_row_ = 0;
        this->set_state_(State::SOFT_RESET);
        break;
      }
      // Hardware reset already ran. OTP omits software reset so RAM baseline survives.
      this->log_physical_refresh_commit_("partial_wake");
      this->cmd_data_(CMD_BORDER, {0x80});
      ESP_LOGI(TAG, "PARTIAL OTP full RAM1 transfer (%u bytes)", static_cast<unsigned>(FRAME_SIZE));
      this->transfer_row_ = 0;
      this->set_state_(State::XFER_PARTIAL);
      break;

    case State::XFER_PARTIAL:
      // OTP demo writes the complete current frame to RAM1 (full_screen window).
      if (!this->transfer_rect_(full, false))
        return;
      this->set_state_(State::PARTIAL_SETUP);
      break;

    case State::PARTIAL_SETUP:
      this->cmd_data_(CMD_DISPLAY_UPDATE_1, {0x00});
      this->cmd_data_(CMD_UPDATE_CONTROL, {0xFF});
      this->after_activate_ = State::DEEP_SLEEP;
      this->set_state_(State::ACTIVATE);
      break;

    case State::DEEP_SLEEP:
      this->send_deep_sleep_();
      this->set_state_(State::SETTLE, 100);
      break;

    case State::SETTLE:
      this->finish_refresh_(true);
      break;

    default:
      ESP_LOGE(TAG, "Unhandled state %u", static_cast<unsigned>(this->state_));
      this->finish_refresh_(false);
      break;
  }
}

void PaperMonoEpaper::on_safe_shutdown() { this->send_deep_sleep_(); }

}  // namespace esphome::papermono_epaper
