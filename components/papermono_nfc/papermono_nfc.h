#pragma once

#include <string>
#include "esphome/components/i2c/i2c.h"
#include "esphome/core/component.h"
#include "st25r3916_official.h"

namespace esphome::m5ioe1 { class M5IOE1Component; }
namespace esphome::controls { class Controls; }
namespace esphome::papermono_activity { class PaperMonoActivityComponent; }
namespace esphome::gpio { class GPIOPin; }

namespace esphome::papermono_nfc {

class PaperMonoNfc : public Component, public i2c::I2CDevice {
 public:
  void set_m5ioe1(m5ioe1::M5IOE1Component *ioe) { m5ioe1_ = ioe; }
  void set_controls(controls::Controls *c) { controls_ = c; }
  void set_activity(papermono_activity::PaperMonoActivityComponent *a) { activity_ = a; }
  void set_irq_pin(InternalGPIOPin *pin) { irq_pin_ = pin; }
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::IO; }

 protected:
  m5ioe1::M5IOE1Component *m5ioe1_{nullptr};
  controls::Controls *controls_{nullptr};
  papermono_activity::PaperMonoActivityComponent *activity_{nullptr};
  InternalGPIOPin *irq_pin_{nullptr};
  bool initialized_{false};
  bool tag_latched_{false};
  uint32_t last_poll_{0};
  uint32_t absent_since_{0};
  uint32_t last_status_log_{0};
  std::string last_uid_;
  official::St25r3916NfcA driver_{this};
};
}  // namespace esphome::papermono_nfc
