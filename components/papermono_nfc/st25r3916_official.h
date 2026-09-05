/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 * SPDX-License-Identifier: MIT
 *
 * NFC-A portion adapted from M5Unit-NFC 0.1.0 (commit
 * 93745b547364f310cd64b5155a870103a7800a5d).  M5UnitUnified and its
 * transport are intentionally not used here; the transport boundary is
 * backed by ESPHome's public i2c::I2CDevice API.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "esphome/components/i2c/i2c.h"

namespace esphome::papermono_nfc::official {

class St25r3916NfcA {
 public:
  explicit St25r3916NfcA(i2c::I2CDevice *device) : device_(device) {}

  bool begin();
  bool poll_uid(std::string &uid);
  void rearm();

 private:
  bool read_reg_(uint8_t reg, uint8_t *data, size_t len);
  bool write_reg_(uint8_t reg, const uint8_t *data, size_t len);
  bool read_reg_b_(uint8_t reg, uint8_t &value);
  bool write_reg_b_(uint8_t reg, uint8_t value);
  bool command_(uint8_t command);
  bool clear_interrupts_();
  bool enable_osc_();
  bool configure_nfc_a_();
  bool set_fwt_timer_(uint32_t timeout_ms);
  bool wait_for_interrupt_(uint8_t main_mask, uint8_t timer_mask, uint32_t timeout_ms, uint8_t irq[4]);
  bool wait_for_fifo_(uint32_t timeout_ms, uint16_t required_bytes, uint8_t irq[4]);
  bool read_fifo_(uint8_t *data, uint16_t capacity, uint16_t &actual);
  bool write_fifo_(const uint8_t *data, uint16_t length);
  bool write_tx_length_(uint16_t bytes, uint8_t bits);
  bool request_(uint16_t &atqa, bool wakeup);
  bool transceive_(uint8_t *rx, uint16_t &rx_len, const uint8_t *tx, uint16_t tx_len, uint32_t timeout_ms,
                  uint16_t minimum_rx);
  bool anticollision_(uint8_t level, uint8_t result[5]);
  bool select_(uint8_t level, const uint8_t cl[5], uint8_t &sak);
  static uint8_t bcc_(const uint8_t *data);
  static std::string format_uid_(const uint8_t *uid, size_t length);

  i2c::I2CDevice *device_;
  bool initialized_{false};
};

}  // namespace esphome::papermono_nfc::official
