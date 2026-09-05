#pragma once

#include "esphome/components/i2c/i2c.h"
#include "esphome/core/component.h"

namespace esphome::m5ioe1 { class M5IOE1Component; }
namespace esphome::nfc_original_diagnostic {

class NfcOriginalDiagnostic : public Component, public i2c::I2CDevice {
 public:
  void set_m5ioe1(m5ioe1::M5IOE1Component *ioe) { m5ioe1_ = ioe; }
  float get_setup_priority() const override { return setup_priority::IO; }
  void setup() override;
  void loop() override;
 private:
  m5ioe1::M5IOE1Component *m5ioe1_{nullptr};
  uint32_t next_attempt_ms_{0};
  uint32_t attempt_{0};
};
}
