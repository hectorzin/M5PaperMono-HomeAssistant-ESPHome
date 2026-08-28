/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD (OTP command sequences)
 * SPDX-FileCopyrightText: 2026 PaperMono Home Assistant contributors (ESPHome integration)
 * SPDX-License-Identifier: MIT
 *
 * PaperMono C153 e-paper driver. Native panel is 800x480 SSD1677 using the
 * controller OTP waveforms from M5Stack M5PaperMono-OTP-Demo
 * (EDP_OTP_LUT_demo.cpp / EDP_SPI.cpp). Not a generic SSD1677 driver.
 *
 * Future 4-gray (not implemented here): RAM1 + RAM2 as two bit planes, OTP
 * command 0xD7. A gray refresh invalidates the monochrome partial baseline.
 */
#pragma once

#include <initializer_list>

#include "esphome/components/display/display.h"
#include "esphome/components/spi/spi.h"
#include "esphome/core/component.h"

namespace esphome::papermono_epaper {

static constexpr uint8_t TRANSFORM_NONE = 0;
static constexpr uint8_t TRANSFORM_MIRROR_X = 1;
static constexpr uint8_t TRANSFORM_MIRROR_Y = 2;
static constexpr uint8_t TRANSFORM_SWAP_XY = 4;

static constexpr uint16_t NATIVE_WIDTH = 800;
static constexpr uint16_t NATIVE_HEIGHT = 480;
static constexpr uint16_t BYTES_PER_ROW = NATIVE_WIDTH / 8;
static constexpr size_t FRAME_SIZE = BYTES_PER_ROW * NATIVE_HEIGHT;

class PaperMonoEpaper
    : public display::Display,
      public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW, spi::CLOCK_PHASE_LEADING,
                            spi::DATA_RATE_20MHZ> {
 public:
  void set_dc_pin(GPIOPin *pin) { this->dc_pin_ = pin; }
  void set_reset_pin(GPIOPin *pin) { this->reset_pin_ = pin; }
  void set_busy_pin(GPIOPin *pin) { this->busy_pin_ = pin; }
  void set_transform(uint8_t transform) {
    this->transform_ = transform;
    this->update_effective_transform_();
  }
  void set_full_update_every(uint8_t every) { this->full_update_every_ = every == 0 ? 1 : every; }
  uint8_t get_partial_count() const { return this->partial_count_; }
  bool is_idle() const;
  bool has_baseline() const;
  void set_rotation(display::DisplayRotation rotation) override {
    display::Display::set_rotation(rotation);
    this->update_effective_transform_();
  }

  float get_setup_priority() const override { return setup_priority::PROCESSOR; }
  void setup() override;
  void dump_config() override;
  void loop() override;
  void update() override;
  void on_safe_shutdown() override;

  // Logical (rotated) dirty regions — used for logging only; partial always sends full RAM1.
  void add_partial_region(int x, int y, int width, int height);
  void update_partial();
  void update_partial(int x, int y, int width, int height);

  display::DisplayType get_display_type() override { return display::DISPLAY_TYPE_BINARY; }
  int get_width() override;
  int get_height() override;
  void fill(Color color) override;
  void clear() override;
  void draw_pixel_at(int x, int y, Color color) override;

 protected:
  int get_width_internal() override { return NATIVE_WIDTH; }
  int get_height_internal() override { return NATIVE_HEIGHT; }

  enum class State : uint8_t {
    IDLE,
    UPDATE,
    RESET_LOW,
    RESET_HIGH,
    DEEP_SLEEP,
    SETTLE,
    SHOULD_WAIT,
    WAIT_POST_RESET,
    SOFT_RESET,
    INIT_MONO,
    FULL_PHASE1,
    XFER_RAM1_INV,
    ACTIVATE,
    WAIT_ACTIVATE,
    FULL_PHASE2,
    XFER_RAM2,
    XFER_RAM1,
    PARTIAL_WAKE,
    XFER_PARTIAL,
    PARTIAL_SETUP,
  };

  struct Rect {
    int16_t x{0};
    int16_t y{0};
    int16_t w{0};
    int16_t h{0};
    bool empty() const { return w <= 0 || h <= 0; }
  };

  static constexpr size_t MAX_PARTIAL_REGIONS = 4;
  static constexpr uint32_t MAX_TRANSFER_SLICE_MS = 10;
  static constexpr uint32_t BUSY_TIMEOUT_MS = 15000;
  static constexpr size_t SPI_CHUNK = 256;

  void update_effective_transform_();
  bool rotate_point_(int &x, int &y) const;

  void request_refresh_(bool full);
  void begin_refresh_(bool full);
  void process_pending_refresh_();
  void set_state_(State state, uint16_t delay_ms = 0);
  void process_state_();
  bool is_idle_() const;
  void wait_for_idle_(bool should_wait);

  void command_(uint8_t cmd);
  void cmd_data_(uint8_t cmd, const uint8_t *data, size_t length);
  void cmd_data_(uint8_t cmd, std::initializer_list<uint8_t> data) {
    this->cmd_data_(cmd, data.begin(), data.size());
  }
  void write_u16_(uint16_t value);
  void set_ram_window_(const Rect &rect);
  void begin_ram_write_(uint8_t ram_cmd);
  void end_ram_write_();
  bool transfer_rect_(const Rect &rect, bool invert);
  void hardware_reset_begin_(bool level);
  void send_soft_reset_();
  void init_mono_mode_();
  void send_deep_sleep_();
  void finish_refresh_(bool success);

  static uint8_t color_to_bit_(Color color) { return (color.r + color.g + color.b) >= 382 ? 1 : 0; }

  GPIOPin *dc_pin_{nullptr};
  GPIOPin *reset_pin_{nullptr};
  GPIOPin *busy_pin_{nullptr};
  uint8_t *buffer_{nullptr};

  uint8_t transform_{TRANSFORM_NONE};
  uint8_t effective_transform_{TRANSFORM_NONE};
  uint8_t full_update_every_{10};
  uint8_t partial_count_{0};
  bool force_full_next_{false};
  bool pending_update_{false};
  bool pending_full_{false};

  State state_{State::IDLE};
  State after_activate_{State::IDLE};
  bool waiting_for_idle_{false};
  bool full_refresh_{true};
  bool baseline_ready_{false};
  uint32_t delay_until_{0};
  uint32_t busy_wait_start_{0};
  uint32_t update_start_ms_{0};

  Rect logical_regions_[MAX_PARTIAL_REGIONS];
  uint8_t logical_region_count_{0};
  uint16_t transfer_row_{0};
  bool ram_write_open_{false};
};

}  // namespace esphome::papermono_epaper
