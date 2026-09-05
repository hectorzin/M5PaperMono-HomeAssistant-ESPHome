#include "nfc_official_diagnostic.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "esphome/components/m5ioe1/m5ioe1.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::nfc_official_diagnostic {

namespace {

static const char *const TAG = "nfc_official_diag";

constexpr uint8_t NFC_ENABLE_PIN = 4;
constexpr uint8_t OP_READ = 0x40;
constexpr uint8_t OP_LOAD_FIFO = 0x80;
constexpr uint8_t OP_READ_FIFO = 0x9F;

constexpr uint8_t CMD_STOP_ALL = 0xC2;
constexpr uint8_t CMD_SET_DEFAULT = 0xC1;
constexpr uint8_t CMD_TRANSMIT_CRC = 0xC4;
constexpr uint8_t CMD_TRANSMIT_NO_CRC = 0xC5;
constexpr uint8_t CMD_TRANSMIT_REQA = 0xC6;
constexpr uint8_t CMD_CLEAR_FIFO = 0xDB;
constexpr uint8_t CMD_INITIAL_FIELD_ON = 0xC8;
constexpr uint8_t CMD_ADJUST_REGULATORS = 0xD6;
constexpr uint8_t CMD_RESET_RX_GAIN = 0xD5;
constexpr uint8_t CMD_TEST_ACCESS = 0xFC;

constexpr uint8_t REG_IO_CONFIGURATION_1 = 0x00;
constexpr uint8_t REG_OPERATION_CONTROL = 0x02;
constexpr uint8_t REG_MODE_DEFINITION = 0x03;
constexpr uint8_t REG_BITRATE_DEFINITION = 0x04;
constexpr uint8_t REG_ISO14443A = 0x05;
constexpr uint8_t REG_NFCIP1_PASSIVE_TARGET = 0x08;
constexpr uint8_t REG_AUXILIARY = 0x0A;
constexpr uint8_t REG_RECEIVER_1 = 0x0B;
constexpr uint8_t REG_RECEIVER_2 = 0x0C;
constexpr uint8_t REG_RECEIVER_3 = 0x0D;
constexpr uint8_t REG_RECEIVER_4 = 0x0E;
constexpr uint8_t REG_NRT1 = 0x10;
constexpr uint8_t REG_NRT2 = 0x11;
constexpr uint8_t REG_MASK_MAIN_IRQ = 0x16;
constexpr uint8_t REG_MAIN_IRQ = 0x1A;
constexpr uint8_t REG_ERROR_IRQ = 0x1C;
constexpr uint8_t REG_FIFO_STATUS_1 = 0x1E;
constexpr uint8_t REG_COLLISION = 0x20;
constexpr uint8_t REG_AUX_DISPLAY = 0x31;
constexpr uint8_t REG_IDENTITY = 0x3F;

constexpr uint8_t ISO14443A_ANTICOLLISION = 0x01;
constexpr uint8_t AUX_NO_CRC_RX = 0x80;
constexpr uint8_t OP_EN = 0x80;
constexpr uint8_t OP_RX_EN = 0x40;
constexpr uint8_t OP_TX_EN = 0x08;

constexpr uint32_t IRQ_RXE = 0x10000000UL;
constexpr uint32_t IRQ_RXS = 0x20000000UL;
constexpr uint32_t IRQ_COL = 0x04000000UL;
constexpr uint32_t IRQ_NRE = 0x00400000UL;
constexpr uint32_t IRQ_TXE = 0x08000000UL;

std::string format_uid(const uint8_t *uid, size_t length) {
  std::string result;
  result.reserve(length * 2);
  for (size_t i = 0; i < length; ++i) {
    char byte[3]{};
    std::snprintf(byte, sizeof(byte), "%02X", uid[i]);
    result += byte;
  }
  return result;
}

}  // namespace

void NfcOfficialDiagnostic::irq_isr_(NfcOfficialDiagnostic *self) {
  // ST25R3916 IRQ is active HIGH and the official driver uses a positive edge.
  self->irq_seen_ = true;
}

bool NfcOfficialDiagnostic::read_regs_(uint8_t reg, uint8_t *data, size_t length) {
  const uint8_t op = static_cast<uint8_t>(OP_READ | (reg & 0x3F));
  return this->write_read(&op, 1, data, length) == i2c::ERROR_OK;
}

bool NfcOfficialDiagnostic::read_reg_(uint8_t reg, uint8_t &value) {
  return this->read_regs_(reg, &value, 1);
}

bool NfcOfficialDiagnostic::write_regs_(uint8_t reg, const uint8_t *data, size_t length) {
  uint8_t buffer[1 + 32]{};
  if (length > 32) return false;
  // WRITE_FIFO is a command opcode and must be sent literally as 0x80.
  buffer[0] = static_cast<uint8_t>(reg >= OP_LOAD_FIFO ? reg : (reg & 0x3F));
  std::memcpy(buffer + 1, data, length);
  return this->write(buffer, length + 1) == i2c::ERROR_OK;
}

bool NfcOfficialDiagnostic::write_reg_(uint8_t reg, uint8_t value) {
  return this->write_regs_(reg, &value, 1);
}

bool NfcOfficialDiagnostic::write_reg16_be_(uint8_t reg, uint16_t value) {
  const uint8_t data[2] = {static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value)};
  return this->write_regs_(reg, data, sizeof(data));
}

bool NfcOfficialDiagnostic::write_space_b_(uint8_t reg, uint8_t value) {
  const uint8_t data[3] = {0xFB, static_cast<uint8_t>(reg & 0x3F), value};
  return this->write(data, sizeof(data)) == i2c::ERROR_OK;
}

bool NfcOfficialDiagnostic::direct_command_(uint8_t command) {
  return this->write(&command, 1) == i2c::ERROR_OK;
}

bool NfcOfficialDiagnostic::clear_interrupts_() {
  uint8_t discard[4]{};
  return this->read_regs_(REG_MAIN_IRQ, discard, sizeof(discard));
}

bool NfcOfficialDiagnostic::read_irq_(uint32_t &irq) {
  uint8_t error = 0;
  uint8_t main_timer[2]{};
  uint8_t passive = 0;
  irq = 0;
  if (!this->read_reg_(REG_ERROR_IRQ, error) || !this->read_regs_(REG_MAIN_IRQ, main_timer, sizeof(main_timer)) ||
      !this->read_reg_(0x1D, passive)) {
    return false;
  }
  irq = (static_cast<uint32_t>(main_timer[0]) << 24) | (static_cast<uint32_t>(main_timer[1]) << 16) |
        (static_cast<uint32_t>(error) << 8) | passive;
  return true;
}

bool NfcOfficialDiagnostic::wait_for_irq_(uint32_t mask, uint32_t timeout_ms, uint32_t &irq) {
  const uint32_t deadline = millis() + timeout_ms;
  irq = 0;
  do {
    // The official implementation uses the ISR flag and also samples the active-high line.
    if (this->irq_seen_ || this->irq_pin_->digital_read()) {
      this->irq_seen_ = false;
      uint32_t current = 0;
      if (this->read_irq_(current)) irq |= current;
    }
    if (irq & mask) return true;
    yield();
  } while (static_cast<int32_t>(millis() - deadline) <= 0);

  // Preserve the official timeout result semantics without doing I2C in the ISR.
  return false;
}

bool NfcOfficialDiagnostic::read_fifo_(uint8_t *data, size_t capacity, size_t &actual) {
  uint8_t status[2]{};
  actual = 0;
  if (!this->read_regs_(REG_FIFO_STATUS_1, status, sizeof(status))) return false;
  const uint16_t bytes = static_cast<uint16_t>(status[0] | ((status[1] & 0xC0) << 2));
  actual = bytes;
  if (bytes == 0 || bytes > capacity) return false;
  // READ_FIFO is a command opcode, not a normal register address: send 0x9F literally.
  if (this->write_read(&OP_READ_FIFO, 1, data, bytes) != i2c::ERROR_OK) return false;
  return true;
}

bool NfcOfficialDiagnostic::request_atqa_(uint16_t &atqa) {
  atqa = 0;
  if (!this->write_reg_(REG_NRT1, 0x03) || !this->write_reg_(REG_NRT2, 0x50) ||
      !this->write_reg_(REG_ISO14443A, ISO14443A_ANTICOLLISION)) return false;
  uint8_t aux = 0;
  if (!this->read_reg_(REG_AUXILIARY, aux) || !this->write_reg_(REG_AUXILIARY, aux | AUX_NO_CRC_RX) ||
      !this->clear_interrupts_() || !this->direct_command_(CMD_CLEAR_FIFO) ||
      !this->direct_command_(CMD_TRANSMIT_REQA)) return false;
  uint32_t irq = 0;
  const bool irq_ok = this->wait_for_irq_(IRQ_RXE | IRQ_RXS | IRQ_NRE, 8, irq);
  ESP_LOGI(TAG, "REQA IRQ main=0x%02X timer=0x%02X error=0x%02X target=0x%02X",
           static_cast<unsigned>((irq >> 24) & 0xFF), static_cast<unsigned>((irq >> 16) & 0xFF),
           static_cast<unsigned>((irq >> 8) & 0xFF), static_cast<unsigned>(irq & 0xFF));
  if (!irq_ok) {
    uint8_t response[2]{};
    size_t actual = 0;
    this->read_fifo_(response, sizeof(response), actual);
    ESP_LOGI(TAG, "REQA FIFO bytes=%u", static_cast<unsigned>(actual));
    this->log_transaction_failure_("REQA timeout", irq);
    return false;
  }
  uint8_t response[2]{};
  size_t actual = 0;
  if (!this->read_fifo_(response, sizeof(response), actual) || actual != 2) {
    ESP_LOGW(TAG, "REQA FIFO error bytes=%u", static_cast<unsigned>(actual));
    return false;
  }
  atqa = static_cast<uint16_t>(response[0] | (static_cast<uint16_t>(response[1]) << 8));
  ESP_LOGI(TAG, "REQA FIFO bytes=%u ATQA=0x%04X", static_cast<unsigned>(actual), atqa);
  return true;
}

bool NfcOfficialDiagnostic::anticollision_(uint8_t level, uint8_t uid_cl[5], bool no_crc_rx) {
  if (!this->write_reg_(REG_NRT1, 0x06) || !this->write_reg_(REG_NRT2, 0x9F) ||
      !this->write_reg_(REG_ISO14443A, ISO14443A_ANTICOLLISION)) return false;
  uint8_t aux = 0;
  if (!this->read_reg_(REG_AUXILIARY, aux) ||
      !this->write_reg_(REG_AUXILIARY, no_crc_rx ? static_cast<uint8_t>(aux | AUX_NO_CRC_RX)
                                                  : static_cast<uint8_t>(aux & ~AUX_NO_CRC_RX)) ||
      !this->clear_interrupts_() || !this->direct_command_(CMD_CLEAR_FIFO)) return false;

  const uint8_t frame[2] = {static_cast<uint8_t>(0x93 + (level - 1) * 2), 0x20};
  const bool gpio_before = this->irq_pin_->digital_read();
  const bool flag_before = this->irq_seen_;
  this->irq_seen_ = false;
  const uint32_t gpio_deadline = millis() + 2;
  while (this->irq_pin_->digital_read() && static_cast<int32_t>(millis() - gpio_deadline) < 0) yield();
  const bool gpio_after_clear = this->irq_pin_->digital_read();
  if (!this->write_regs_(OP_LOAD_FIFO, frame, sizeof(frame)) || !this->write_reg16_be_(0x22, 0x0010)) return false;
  const bool tx_ok = this->direct_command_(CMD_TRANSMIT_NO_CRC);
  ESP_LOGI(TAG, "CL%u TX=%02X %02X IRQ GPIO before=%u after_clear=%u after TX=%u flag_before=%u flag_after=%u", level,
           frame[0], frame[1], gpio_before ? 1U : 0U, gpio_after_clear ? 1U : 0U,
           this->irq_pin_->digital_read() ? 1U : 0U, flag_before ? 1U : 0U, this->irq_seen_ ? 1U : 0U);
  if (!tx_ok) return false;

  uint32_t irq = 0;
  const bool irq_ok = this->wait_for_irq_(IRQ_RXE | IRQ_COL, 12, irq);
  uint8_t collision = 0;
  if (!this->read_reg_(REG_COLLISION, collision)) return false;
  ESP_LOGI(TAG, "CL%u IRQ main=0x%02X timer=0x%02X error=0x%02X target=0x%02X wait=%s",
           level, static_cast<unsigned>((irq >> 24) & 0xFF), static_cast<unsigned>((irq >> 16) & 0xFF),
           static_cast<unsigned>((irq >> 8) & 0xFF), static_cast<unsigned>(irq & 0xFF), irq_ok ? "yes" : "no");
  size_t actual = 0;
  if (!this->read_fifo_(uid_cl, 5, actual) || actual != 5) {
    ESP_LOGW(TAG, "CL%u FIFO bytes=%u error", level, static_cast<unsigned>(actual));
    return false;
  }
  ESP_LOGI(TAG, "CL%u FIFO bytes=%u", level, static_cast<unsigned>(actual));
  const bool bcc_valid = static_cast<uint8_t>(uid_cl[0] ^ uid_cl[1] ^ uid_cl[2] ^ uid_cl[3]) == uid_cl[4];
  ESP_LOGI(TAG, "CL%u UID+BCC=%02X %02X %02X %02X %02X BCC %s collision=0x%02X no_crc_rx=%u", level, uid_cl[0],
           uid_cl[1], uid_cl[2], uid_cl[3], uid_cl[4], bcc_valid ? "valid" : "invalid", collision,
           no_crc_rx ? 1U : 0U);
  if (!bcc_valid) {
    return false;
  }
  return irq_ok && (irq & (IRQ_RXE | IRQ_COL)) != 0;
}

bool NfcOfficialDiagnostic::select_(uint8_t level, const uint8_t uid_cl[5], uint8_t &sak) {
  if (!this->write_reg_(REG_ISO14443A, 0x00)) return false;
  uint8_t aux = 0;
  if (!this->read_reg_(REG_AUXILIARY, aux) || !this->write_reg_(REG_AUXILIARY, aux & ~AUX_NO_CRC_RX) ||
      !this->clear_interrupts_() || !this->direct_command_(CMD_CLEAR_FIFO)) return false;
  const uint8_t frame[7] = {static_cast<uint8_t>(0x93 + (level - 1) * 2), 0x70, uid_cl[0], uid_cl[1], uid_cl[2], uid_cl[3],
                            uid_cl[4]};
  if (!this->write_regs_(OP_LOAD_FIFO, frame, sizeof(frame)) || !this->write_reg16_be_(0x22, 7u << 3) ||
      !this->direct_command_(CMD_TRANSMIT_CRC)) return false;
  uint32_t irq = 0;
  const bool irq_ok = this->wait_for_irq_(IRQ_RXE, 12, irq);
  ESP_LOGI(TAG, "SELECT CL%u IRQ main=0x%02X timer=0x%02X error=0x%02X target=0x%02X wait=%s", level,
           static_cast<unsigned>((irq >> 24) & 0xFF), static_cast<unsigned>((irq >> 16) & 0xFF),
           static_cast<unsigned>((irq >> 8) & 0xFF), static_cast<unsigned>(irq & 0xFF), irq_ok ? "yes" : "no");
  if (!irq_ok) {
    this->log_transaction_failure_("SELECT timeout", irq);
    return false;
  }
  uint8_t response[3]{};
  size_t actual = 0;
  if (!this->read_fifo_(response, sizeof(response), actual) || actual < 1) return false;
  sak = response[0];
  ESP_LOGI(TAG, "CL%u SAK=0x%02X IRQ=0x%08lX", level, sak, static_cast<unsigned long>(irq));
  return true;
}

bool NfcOfficialDiagnostic::read_uid_(uint8_t uid[10], size_t &uid_length, uint8_t &sak) {
  uid_length = 0;
  for (uint8_t level = 1; level <= 3; ++level) {
    uint8_t uid_cl[5]{};
    bool ok = this->anticollision_(level, uid_cl, false);
    if (!ok) ok = this->anticollision_(level, uid_cl, true);
    if (!ok) return false;
    const size_t count = uid_cl[0] == 0x88 ? 3 : 4;
    if (uid_length + count > 10) return false;
    std::memcpy(uid + uid_length, uid_cl + (uid_cl[0] == 0x88 ? 1 : 0), count);
    uid_length += count;
    if (!this->select_(level, uid_cl, sak)) return false;
    if ((sak & 0x04) == 0) return true;
  }
  return false;
}

bool NfcOfficialDiagnostic::halt_card_() {
  if (!this->write_reg_(REG_ISO14443A, 0x00)) return false;
  uint8_t aux = 0;
  if (!this->read_reg_(REG_AUXILIARY, aux) || !this->write_reg_(REG_AUXILIARY, aux & ~AUX_NO_CRC_RX) ||
      !this->clear_interrupts_() || !this->direct_command_(CMD_CLEAR_FIFO)) return false;
  const uint8_t frame[2] = {0x50, 0x00};
  if (!this->write_regs_(OP_LOAD_FIFO, frame, sizeof(frame)) || !this->write_reg16_be_(0x22, 2u << 3) ||
      !this->direct_command_(CMD_TRANSMIT_CRC)) return false;
  uint32_t irq = 0;
  const bool ok = this->wait_for_irq_(IRQ_TXE, 5, irq);
  ESP_LOGD(TAG, "HLTA IRQ=0x%08lX", static_cast<unsigned long>(irq));
  return ok;
}

bool NfcOfficialDiagnostic::initialize_reader_() {
  uint8_t identity = 0;
  if (!this->read_reg_(REG_IDENTITY, identity)) return false;
  ESP_LOGI(TAG, "IC_ID=0x%02X", identity);
  if (!this->direct_command_(CMD_STOP_ALL) || !this->write_reg_(REG_OPERATION_CONTROL, 0x00) ||
      !this->direct_command_(CMD_SET_DEFAULT)) return false;
  const uint8_t protection[2] = {0x04, 0x10};
  const uint8_t test_access[3] = {CMD_TEST_ACCESS, protection[0], protection[1]};
  if (this->write(test_access, sizeof(test_access)) != i2c::ERROR_OK) return false;
  if (!this->write_reg16_be_(REG_IO_CONFIGURATION_1, 0x1084) || !this->write_reg_(0x28, 0xD0) ||
      !this->write_reg_(0x2A, 0x13) || !this->write_reg_(0x2B, 0x02) ||
      !this->write_reg_(REG_NFCIP1_PASSIVE_TARGET, 0x50)) return false;
  if (!this->write_space_b_(0x05, 0x40) || !this->write_space_b_(0x30, 0x40) || !this->write_space_b_(0x31, 0x03) ||
      !this->write_space_b_(0x32, 0x40) || !this->write_space_b_(0x33, 0x03) || !this->write_space_b_(0x0C, 0x47) ||
      !this->write_space_b_(0x0D, 0x00)) return false;
  if (!this->write_reg_(REG_MODE_DEFINITION, 0x09) || !this->write_reg_(REG_BITRATE_DEFINITION, 0x00) ||
      !this->write_reg_(REG_ISO14443A, 0x00) || !this->write_reg_(REG_AUXILIARY, 0x00) ||
      !this->write_reg_(REG_RECEIVER_1, 0x08) || !this->write_reg_(REG_RECEIVER_2, 0x2D) ||
      !this->write_reg_(REG_RECEIVER_3, 0xD8) || !this->write_reg_(REG_RECEIVER_4, 0x22) ||
      !this->direct_command_(CMD_RESET_RX_GAIN)) return false;

  if (!this->write_reg_(REG_MASK_MAIN_IRQ, 0x00) || !this->clear_interrupts_()) return false;
  uint8_t op = 0;
  if (!this->read_reg_(REG_OPERATION_CONTROL, op) || !this->write_reg_(REG_OPERATION_CONTROL, op | OP_EN)) return false;
  const uint32_t deadline = millis() + 50;
  uint8_t aux_display = 0;
  while (static_cast<int32_t>(millis() - deadline) < 0) {
    if (this->read_reg_(REG_AUX_DISPLAY, aux_display) && (aux_display & 0x10)) break;
    yield();
  }
  if ((aux_display & 0x10) == 0) return false;
  if (!this->direct_command_(CMD_ADJUST_REGULATORS)) return false;
  delay(5);
  if (!this->read_reg_(REG_OPERATION_CONTROL, op) || !this->write_reg_(REG_OPERATION_CONTROL, op | 0x03) ||
      !this->write_reg_(REG_MASK_MAIN_IRQ, 0x00) || !this->direct_command_(CMD_INITIAL_FIELD_ON)) return false;
  delay(5);
  if (!this->read_reg_(REG_OPERATION_CONTROL, op) ||
      !this->write_reg_(REG_OPERATION_CONTROL, op | OP_TX_EN | OP_RX_EN)) return false;
  return true;
}

void NfcOfficialDiagnostic::log_transaction_failure_(const char *phase, uint32_t irq) {
  ESP_LOGD(TAG, "%s IRQ=0x%08lX", phase, static_cast<unsigned long>(irq));
}

void NfcOfficialDiagnostic::setup() {
  ESP_LOGI(TAG, "NFC OFFICIAL DIAG START");
  if (this->m5ioe1_ == nullptr || this->irq_pin_ == nullptr) {
    ESP_LOGE(TAG, "M5IOE1 or IRQ GPIO6 missing");
    this->mark_failed();
    return;
  }
  ESP_LOGI(TAG, "M5IOE1 GPIO4 high-impedance OFF");
  ESP_LOGI(TAG, "M5IOE1 GPIO4 OUTPUT");
  this->m5ioe1_->set_pin_output_level(NFC_ENABLE_PIN, true);
  ESP_LOGI(TAG, "PYB_NFC_EN HIGH");
  ESP_LOGI(TAG, "delay 10 ms");
  delay(10);
  this->irq_pin_->setup();
  this->irq_pin_->pin_mode(gpio::FLAG_INPUT);
  this->irq_pin_->attach_interrupt(&NfcOfficialDiagnostic::irq_isr_, this, gpio::INTERRUPT_RISING_EDGE);
  ESP_LOGI(TAG, "IRQ GPIO6 configured (active HIGH, rising edge)");
  ESP_LOGI(TAG, "ST25R3916 init start");
  if (!this->initialize_reader_()) {
    ESP_LOGE(TAG, "ST25R3916 init FAIL");
    this->mark_failed();
    return;
  }
  this->initialized_ = true;
  this->next_scan_ms_ = millis();
  ESP_LOGI(TAG, "ST25R3916 init OK; RF field ON; NFC-A ready");
}

void NfcOfficialDiagnostic::loop() {
  if (!this->initialized_ || static_cast<int32_t>(millis() - this->next_scan_ms_) < 0) return;
  this->next_scan_ms_ = millis() + 500;
  ESP_LOGI(TAG, "SCAN attempt=%lu", static_cast<unsigned long>(++this->scan_attempt_));
  uint16_t atqa = 0;
  if (!this->request_atqa_(atqa)) {
    this->card_present_ = false;
    this->last_uid_.clear();
    ESP_LOGD(TAG, "NO TAG");
    return;
  }
  ESP_LOGI(TAG, "ATQA=0x%04X", atqa);
  uint8_t uid[10]{};
  size_t uid_length = 0;
  uint8_t sak = 0;
  if (!this->read_uid_(uid, uid_length, sak)) {
    ESP_LOGW(TAG, "UID read failed");
    return;
  }
  const std::string formatted_uid = format_uid(uid, uid_length);
  if (!this->card_present_ || formatted_uid != this->last_uid_) {
    ESP_LOGI(TAG, "UID=%s", formatted_uid.c_str());
    this->last_uid_ = formatted_uid;
    this->card_present_ = true;
  }
}

}  // namespace esphome::nfc_official_diagnostic
