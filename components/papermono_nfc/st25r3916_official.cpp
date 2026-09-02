/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 * SPDX-License-Identifier: MIT
 *
 * NFC-A subset adapted from M5Unit-NFC 0.1.0.  The register sequences and
 * NFC-A transaction flow below follow UnitST25R3916::begin(), configure_nfc_a(),
 * nfca_request_wakeup(), nfca_anti_collision() and nfcaSelectWithAnticollision().
 */
#include "st25r3916_official.h"

#include <algorithm>
#include <cstring>

#include "esphome/core/hal.h"

namespace esphome::papermono_nfc::official {
namespace {
constexpr uint8_t OP_READ = 0x40, OP_LOAD_FIFO = 0x80, OP_READ_FIFO = 0x9F;
constexpr uint8_t REG_IO1 = 0x00, REG_IO2 = 0x01, REG_OP = 0x02, REG_MODE = 0x03, REG_RATE = 0x04;
constexpr uint8_t REG_ISO = 0x05, REG_AUX = 0x0A, REG_RX1 = 0x0B, REG_RX2 = 0x0C, REG_RX3 = 0x0D, REG_RX4 = 0x0E;
constexpr uint8_t REG_NRT = 0x10, REG_TIMER = 0x12, REG_MASK_MAIN = 0x16, REG_MAIN_IRQ = 0x1A;
constexpr uint8_t REG_FIFO1 = 0x1E, REG_TX_BYTES = 0x22, REG_ANT1 = 0x26, REG_ANT2 = 0x27;
constexpr uint8_t REG_TX_DRIVER = 0x28, REG_FIELD_ON = 0x2A, REG_FIELD_OFF = 0x2B, REG_AUX_DISPLAY = 0x31;
constexpr uint8_t REG_ID = 0x3F;
constexpr uint8_t CMD_SET_DEFAULT = 0xC1, CMD_STOP = 0xC2, CMD_TX_CRC = 0xC4, CMD_TX_NOCRC = 0xC5;
constexpr uint8_t CMD_REQA = 0xC6, CMD_WUPA = 0xC7, CMD_FIELD_ON = 0xC8, CMD_CLEAR_FIFO = 0xDB;
constexpr uint8_t CMD_ADJUST_REGULATORS = 0xD6, CMD_RESET_RX_GAIN = 0xD5;
constexpr uint8_t IRQ_RXS = 0x20, IRQ_RXE = 0x10, IRQ_COL = 0x04;
constexpr uint8_t TIMER_NRE = 0x40;
constexpr uint8_t AUX_NO_CRC_RX = 0x80;
constexpr uint8_t OP_EN = 0x80, OP_RX_EN = 0x40, OP_TX_EN = 0x08, OP_WU = 0x04;
constexpr uint8_t TIMER_NRT_STEP = 0x01;
constexpr uint8_t IRQ_OSC = 0x80;

uint16_t calculate_nrt(uint32_t ms, bool step4096) {
  constexpr uint32_t fc = 13560000;
  const uint64_t denominator = static_cast<uint64_t>(step4096 ? 4096 : 64) * 1000000ULL;
  const uint64_t ticks = (static_cast<uint64_t>(ms) * 1000ULL * fc + denominator - 1) / denominator;
  return static_cast<uint16_t>(std::max<uint64_t>(1, std::min<uint64_t>(ticks, 0xFFFF)));
}
}  // namespace

bool St25r3916NfcA::read_reg_(uint8_t reg, uint8_t *data, size_t len) {
  const uint8_t op = static_cast<uint8_t>(OP_READ | (reg & 0x3F));
  return device_->write_read(&op, 1, data, len) == i2c::ERROR_OK;
}

bool St25r3916NfcA::write_reg_(uint8_t reg, const uint8_t *data, size_t len) {
  uint8_t buffer[64];
  if (len + 1 > sizeof(buffer)) return false;
  buffer[0] = static_cast<uint8_t>(reg >= OP_LOAD_FIFO ? reg : (reg & 0x3F));
  memcpy(buffer + 1, data, len);
  return device_->write(buffer, len + 1) == i2c::ERROR_OK;
}

bool St25r3916NfcA::write_reg_b_(uint8_t reg, uint8_t value) {
  const uint8_t buffer[3] = {0xFB, static_cast<uint8_t>((reg & 0x3F) | 0x20), value};
  return device_->write(buffer, sizeof(buffer)) == i2c::ERROR_OK;
}

bool St25r3916NfcA::read_reg_b_(uint8_t reg, uint8_t &value) {
  const uint8_t op[2] = {0xFB, static_cast<uint8_t>((reg & 0x3F) | 0x40)};
  return device_->write_read(op, sizeof(op), &value, 1) == i2c::ERROR_OK;
}

bool St25r3916NfcA::command_(uint8_t command) { return device_->write(&command, 1) == i2c::ERROR_OK; }

bool St25r3916NfcA::clear_interrupts_() {
  uint8_t irq[4]{};
  return read_reg_(REG_MAIN_IRQ, irq, sizeof(irq));
}

bool St25r3916NfcA::enable_osc_() {
  uint8_t op{};
  if (!read_reg_(REG_OP, &op, 1)) return false;
  if ((op & OP_EN) == 0) {
    uint8_t mask = 0x7F;
    if (!write_reg_(REG_MASK_MAIN, &mask, 1) || !clear_interrupts_()) return false;
    op = static_cast<uint8_t>(op | OP_EN);
    if (!write_reg_(REG_OP, &op, 1)) return false;
    const uint32_t start = millis();
    bool ready = false;
    while (static_cast<uint32_t>(millis() - start) < 50U) {
      uint8_t irq[4]{}, aux{};
      if (!read_reg_(REG_MAIN_IRQ, irq, sizeof(irq)) || !read_reg_(REG_AUX_DISPLAY, &aux, 1)) return false;
      if ((irq[0] & IRQ_OSC) != 0 || (aux & 0x10) != 0) { ready = true; break; }
      yield();
    }
    uint8_t restore = 0xFF;
    if (!write_reg_(REG_MASK_MAIN, &restore, 1) || !ready) return false;
  }
  uint8_t aux{};
  return read_reg_(REG_AUX_DISPLAY, &aux, 1) && (aux & 0x10) != 0;
}

bool St25r3916NfcA::set_fwt_timer_(uint32_t timeout_ms) {
  uint8_t timer{};
  if (!read_reg_(REG_TIMER, &timer, 1)) return false;
  const uint16_t nrt = calculate_nrt(timeout_ms, (timer & TIMER_NRT_STEP) != 0);
  const uint8_t bytes[2] = {static_cast<uint8_t>(nrt >> 8), static_cast<uint8_t>(nrt)};
  return write_reg_(REG_NRT, bytes, sizeof(bytes));
}

bool St25r3916NfcA::write_fifo_(const uint8_t *data, uint16_t length) {
  return write_reg_(OP_LOAD_FIFO, data, length);
}

bool St25r3916NfcA::write_tx_length_(uint16_t bytes, uint8_t bits) {
  const uint16_t value = static_cast<uint16_t>(((bytes & 0x01FF) << 3) | (bits & 0x07));
  const uint8_t data[2] = {static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value)};
  return write_reg_(REG_TX_BYTES, data, sizeof(data));
}

bool St25r3916NfcA::read_fifo_(uint8_t *data, uint16_t capacity, uint16_t &actual) {
  uint8_t status_bytes[2]{};
  actual = 0;
  if (!read_reg_(REG_FIFO1, status_bytes, sizeof(status_bytes))) return false;
  const uint16_t status = static_cast<uint16_t>(status_bytes[0] << 8) | status_bytes[1];
  const uint16_t available = static_cast<uint16_t>((status >> 8) | ((status & 0x00C0) << 2));
  actual = std::min<uint16_t>(available, capacity);
  if (actual == 0) return false;
  return read_reg_(OP_READ_FIFO, data, actual);
}

bool St25r3916NfcA::wait_for_interrupt_(uint8_t main_mask, uint8_t timer_mask, uint32_t timeout_ms, uint8_t irq[4]) {
  memset(irq, 0, 4);
  const uint32_t start = millis();
  while (static_cast<uint32_t>(millis() - start) <= timeout_ms) {
    uint8_t current[4]{};
    if (!read_reg_(REG_MAIN_IRQ, current, sizeof(current))) return false;
    for (size_t i = 0; i < 4; ++i) irq[i] |= current[i];
    if ((irq[0] & main_mask) != 0 || (irq[1] & timer_mask) != 0) return true;
    yield();
  }
  return (irq[1] & TIMER_NRE) != 0;
}

bool St25r3916NfcA::wait_for_fifo_(uint32_t timeout_ms, uint16_t required_bytes, uint8_t irq[4]) {
  if (!wait_for_interrupt_(IRQ_RXE | IRQ_RXS | IRQ_COL, TIMER_NRE, timeout_ms, irq)) return false;
  if ((irq[0] & IRQ_RXE) != 0) return true;
  const uint32_t start = millis();
  while (static_cast<uint32_t>(millis() - start) <= timeout_ms) {
    uint8_t status_bytes[2]{};
    if (!read_reg_(REG_FIFO1, status_bytes, sizeof(status_bytes))) return false;
    const uint16_t status = static_cast<uint16_t>(status_bytes[0] << 8) | status_bytes[1];
    const uint16_t available = static_cast<uint16_t>((status >> 8) | ((status & 0x00C0) << 2));
    if (available >= required_bytes) return true;
    yield();
  }
  return false;
}

bool St25r3916NfcA::configure_nfc_a_() {
  // M5Unit-NFC configure_nfc_a(): ISO14443A settings remain standard (0x00)
  // during initialization; antcl (0x01) is selected by request/anticollision.
  const uint8_t mode = 0x09, bitrate = 0x00, iso = 0x00;
  if (!write_reg_(REG_MODE, &mode, 1) || !write_reg_(REG_RATE, &bitrate, 1) || !write_reg_(REG_ISO, &iso, 1)) return false;
  uint8_t aux{};
  if (!read_reg_(REG_AUX, &aux, 1)) return false;
  aux = static_cast<uint8_t>(aux & ~0x20);
  if (!write_reg_(REG_AUX, &aux, 1)) return false;
  if (!write_reg_b_(0x30, 0x40) || !write_reg_b_(0x31, 0x03) || !write_reg_b_(0x32, 0x40) ||
      !write_reg_b_(0x33, 0x03) || !write_reg_b_(0x0C, 0x47) || !write_reg_b_(0x0D, 0x00)) return false;
  const uint8_t rx1 = 0x08, rx2 = 0x2D, rx3 = 0xD8, rx4 = 0x22;
  if (!write_reg_(REG_RX1, &rx1, 1) || !write_reg_(REG_RX2, &rx2, 1) || !write_reg_(REG_RX3, &rx3, 1) ||
      !write_reg_(REG_RX4, &rx4, 1) || !command_(CMD_RESET_RX_GAIN)) return false;
  const uint8_t masks[4] = {0, 0, 0, 0};
  return write_reg_(REG_MASK_MAIN, masks, sizeof(masks)) && command_(CMD_FIELD_ON) && [&]() {
    uint8_t op{};
    if (!read_reg_(REG_OP, &op, 1) || (op & OP_TX_EN) != 0) return false;
    delay(5);
    op = static_cast<uint8_t>(op | OP_TX_EN | OP_RX_EN);
    if (!write_reg_(REG_OP, &op, 1)) return false;
    return clear_interrupts_() && command_(CMD_CLEAR_FIFO);
  }();
}

bool St25r3916NfcA::begin() {
  delay(50);
  uint8_t id{};
  bool detected = false;
  for (int attempt = 0; attempt < 5; ++attempt) {
    if (read_reg_(REG_ID, &id, 1) && ((id >> 3) & 0x1F) == 0x05 && (id & 0x07) != 0) { detected = true; break; }
    delay(20);
  }
  if (!detected) return false;
  if (!command_(CMD_STOP)) return false;
  uint8_t op{};
  if (!read_reg_(REG_OP, &op, 1)) return false;
  op = static_cast<uint8_t>(op & ~(OP_TX_EN | OP_RX_EN));
  if (!write_reg_(REG_OP, &op, 1)) return false;
  delay(2);
  if (!command_(CMD_SET_DEFAULT)) return false;
  const uint8_t protection[2] = {0x04, 0x10};
  if (!write_reg_(0xFC, protection, sizeof(protection))) return false;
  const uint8_t io1 = 0x07, io2 = 0xA4, tx = 0xD0, field_on = 0x13, field_off = 0x02;
  if (!write_reg_(REG_IO1, &io1, 1) || !write_reg_(REG_IO2, &io2, 1) || !write_reg_(REG_TX_DRIVER, &tx, 1) ||
      !write_reg_b_(0x2A, 0x80) || !write_reg_(REG_IO2, &io2, 1) || !write_reg_b_(0x2A, 0x00) ||
      !write_reg_(REG_FIELD_ON, &field_on, 1) || !write_reg_(REG_FIELD_OFF, &field_off, 1)) return false;
  uint8_t op_field{};
  if (!read_reg_(REG_OP, &op_field, 1)) return false;
  op_field = static_cast<uint8_t>(op_field | 0x03);
  if (!write_reg_(REG_OP, &op_field, 1) || !command_(CMD_CLEAR_FIFO)) return false;
  const uint8_t masks[4] = {0xFF, 0xFF, 0x00, 0xFF};
  if (!write_reg_(REG_MASK_MAIN, masks, sizeof(masks)) || !clear_interrupts_() || !enable_osc_()) return false;
  const uint8_t no_masks[4] = {0, 0, 0, 0};
  if (!write_reg_(REG_MASK_MAIN, no_masks, sizeof(no_masks)) || !command_(CMD_ADJUST_REGULATORS)) return false;
  delay(5);
  if (!command_(CMD_STOP) || !read_reg_(REG_OP, &op, 1)) return false;
  op = static_cast<uint8_t>(op & ~OP_WU);
  return write_reg_(REG_OP, &op, 1) && configure_nfc_a_();
}

bool St25r3916NfcA::transceive_(uint8_t *rx, uint16_t &rx_len, const uint8_t *tx, uint16_t tx_len,
                                uint32_t timeout_ms, uint16_t minimum_rx) {
  // M5Unit-NFC nfcaTransmit(): the generic NFC-A exchange restores standard
  // ISO framing, enables CRC reception, clears IRQ/FIFO, then loads FIFO and
  // starts TRANSMIT_WITH_CRC.
  uint8_t iso = 0x00, aux{};
  if (!set_fwt_timer_(timeout_ms) || !write_reg_(REG_ISO, &iso, 1) || !read_reg_(REG_AUX, &aux, 1)) return false;
  aux = static_cast<uint8_t>(aux & ~AUX_NO_CRC_RX);
  if (!write_reg_(REG_AUX, &aux, 1) || !clear_interrupts_() || !command_(CMD_CLEAR_FIFO) ||
      !write_fifo_(tx, tx_len) || !write_tx_length_(tx_len, 0) || !command_(CMD_TX_CRC)) return false;
  uint8_t irq[4]{};
  if (!wait_for_fifo_(timeout_ms, minimum_rx, irq)) return false;
  uint16_t actual{};
  if (!read_fifo_(rx, rx_len, actual)) return false;
  rx_len = actual;
  return actual >= minimum_rx;
}

bool St25r3916NfcA::request_(uint16_t &atqa, bool wakeup) {
  atqa = 0;
  // M5Unit-NFC nfca_request_wakeup(): antcl framing is selected for REQA/WUPA.
  const uint8_t iso = 0x01;
  if (!set_fwt_timer_(4) || !write_reg_(REG_ISO, &iso, 1)) return false;
  const uint8_t aux = AUX_NO_CRC_RX;
  if (!write_reg_(REG_AUX, &aux, 1) || !clear_interrupts_() || !command_(CMD_CLEAR_FIFO) ||
      !command_(wakeup ? CMD_WUPA : CMD_REQA)) return false;
  uint8_t irq[4]{};
  if (!wait_for_fifo_(4, 2, irq)) return false;
  uint8_t response[2]{};
  uint16_t actual{};
  if (!read_fifo_(response, sizeof(response), actual) || actual != 2) return false;
  atqa = static_cast<uint16_t>(response[0] | (response[1] << 8));
  return true;
}

bool St25r3916NfcA::anticollision_(uint8_t level, uint8_t result[5]) {
  const uint8_t iso = 0x01, aux = 0x00;
  if (!set_fwt_timer_(8) || !write_reg_(REG_ISO, &iso, 1) || !write_reg_(REG_AUX, &aux, 1)) return false;
  uint8_t frame[7] = {static_cast<uint8_t>(0x91 + level * 2), 0x20, 0, 0, 0, 0, 0};
  uint16_t bytes = 2;
  uint8_t bits = 0, offset = 0, collision_byte = 1;
  for (uint32_t count = 0; count < 32; ++count) {
    if (!clear_interrupts_() || !command_(CMD_CLEAR_FIFO) || !write_fifo_(frame, static_cast<uint16_t>(bytes + (bits != 0))) ||
        !write_tx_length_(bytes, bits) || !command_(CMD_TX_NOCRC)) return false;
    uint8_t irq[4]{};
    if (!wait_for_interrupt_(IRQ_RXE | IRQ_COL, TIMER_NRE, 8, irq)) return false;
    const bool collision = (irq[0] & IRQ_COL) != 0;
    if (!collision && (irq[0] & IRQ_RXE) == 0) return false;
    uint16_t actual{};
    if (!read_fifo_(result + offset, static_cast<uint16_t>(5 - offset), actual) || actual == 0) return false;
    if (collision) {
      uint8_t display{};
      if (!read_reg_(0x20, &display, 1)) return false;
      const uint8_t cbytes = static_cast<uint8_t>((display >> 4) & 0x0F);
      const uint8_t cbits = static_cast<uint8_t>((display >> 1) & 0x07);
      collision_byte = static_cast<uint8_t>(result[offset + actual - 1] | (1U << cbits));
      bytes = static_cast<uint16_t>(cbytes + (cbits == 7));
      bits = static_cast<uint8_t>((cbits + 1) & 0x07);
      frame[1] = static_cast<uint8_t>((bytes << 4) | bits);
      memcpy(frame + 2 + offset, result + offset, actual);
      frame[bytes] = collision_byte;
      offset = static_cast<uint8_t>(actual - 1);
    }
    if (bits) {
      result[offset] = static_cast<uint8_t>((result[offset] & ~((1U << bits) - 1U)) | collision_byte);
    }
    if (!collision) return true;
    yield();
  }
  return false;
}

bool St25r3916NfcA::select_(uint8_t level, const uint8_t cl[5], uint8_t &sak) {
  uint8_t frame[7] = {static_cast<uint8_t>(0x91 + level * 2), 0x70, cl[0], cl[1], cl[2], cl[3], cl[4]};
  uint8_t response[3]{};
  uint16_t length = sizeof(response);
  if (!transceive_(response, length, frame, sizeof(frame), 8, 3)) return false;
  sak = response[0];
  return true;
}

bool St25r3916NfcA::poll_uid(std::string &uid) {
  uint16_t atqa{};
  if (!request_(atqa, false)) return false;
  uint8_t full_uid[10]{}, total = 0;
  for (uint8_t level = 1; level <= 3; ++level) {
    uint8_t cl[5]{};
    if (!anticollision_(level, cl)) return false;
    if ((cl[0] ^ cl[1] ^ cl[2] ^ cl[3]) != cl[4]) return false;
    const bool cascade = cl[0] == 0x88;
    memcpy(full_uid + total, cl + (cascade ? 1 : 0), cascade ? 3 : 4);
    total = static_cast<uint8_t>(total + (cascade ? 3 : 4));
    uint8_t sak{};
    if (!select_(level, cl, sak)) return false;
    if ((sak & 0x04) == 0) break;
  }
  uid = format_uid_(full_uid, total);
  return !uid.empty();
}

void St25r3916NfcA::rearm() {
  command_(CMD_STOP);
  command_(CMD_FIELD_ON);
}

uint8_t St25r3916NfcA::bcc_(const uint8_t *data) { return data[0] ^ data[1] ^ data[2] ^ data[3]; }

std::string St25r3916NfcA::format_uid_(const uint8_t *uid, size_t length) {
  static constexpr char hex[] = "0123456789ABCDEF";
  std::string result;
  result.reserve(length * 2);
  for (size_t i = 0; i < length; ++i) {
    result.push_back(hex[uid[i] >> 4]);
    result.push_back(hex[uid[i] & 0x0F]);
  }
  return result;
}

}  // namespace esphome::papermono_nfc::official
