#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "esphome/components/i2c/i2c.h"
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"

namespace esphome::m5ioe1 {
class M5IOE1Component;
}

namespace esphome::nfc_official_diagnostic {

class NfcOfficialDiagnostic : public Component, public i2c::I2CDevice {
 public:
  void set_m5ioe1(m5ioe1::M5IOE1Component *m5ioe1) { m5ioe1_ = m5ioe1; }
  void set_irq_pin(InternalGPIOPin *pin) { irq_pin_ = pin; }

  float get_setup_priority() const override { return setup_priority::IO; }
  void setup() override;
  void loop() override;

 private:
  static void irq_isr_(NfcOfficialDiagnostic *self);

  bool read_reg_(uint8_t reg, uint8_t &value);
  bool read_regs_(uint8_t reg, uint8_t *data, size_t length);
  bool write_reg_(uint8_t reg, uint8_t value);
  bool write_regs_(uint8_t reg, const uint8_t *data, size_t length);
  bool write_reg16_be_(uint8_t reg, uint16_t value);
  bool write_space_b_(uint8_t reg, uint8_t value);
  bool direct_command_(uint8_t command);
  bool clear_interrupts_();
  bool read_irq_(uint32_t &irq);
  bool wait_for_irq_(uint32_t mask, uint32_t timeout_ms, uint32_t &irq);
  bool read_fifo_(uint8_t *data, size_t capacity, size_t &actual);
  bool request_atqa_(uint16_t &atqa);
  bool anticollision_(uint8_t level, uint8_t uid_cl[5], bool no_crc_rx);
  bool select_(uint8_t level, const uint8_t uid_cl[5], uint8_t &sak);
  bool read_uid_(uint8_t uid[10], size_t &uid_length, uint8_t &sak);
  bool halt_card_();
  bool initialize_reader_();
  void log_transaction_failure_(const char *phase, uint32_t irq);

  m5ioe1::M5IOE1Component *m5ioe1_{nullptr};
  InternalGPIOPin *irq_pin_{nullptr};
  volatile bool irq_seen_{false};
  bool initialized_{false};
  uint32_t next_scan_ms_{0};
  uint32_t scan_attempt_{0};
  std::string last_uid_;
  bool card_present_{false};
};

}  // namespace esphome::nfc_official_diagnostic
