#pragma once

#include <cstdint>
#include <string>

#include "esphome/components/i2c/i2c.h"
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "st25r3916_official.h"

namespace esphome::m5ioe1 {
class M5IOE1Component;
}

namespace esphome::papermono_activity {
class PaperMonoActivityComponent;
}

namespace esphome::controls {
class Controls;
}

namespace esphome::text_sensor {
class TextSensor;
}

namespace esphome::papermono_nfc {

class PaperMonoNfc : public Component, public i2c::I2CDevice {
 public:
  PaperMonoNfc() : driver_(this) {}

  float get_setup_priority() const override { return setup_priority::IO; }
  void setup() override;
  void loop() override;

  void set_m5ioe1(m5ioe1::M5IOE1Component *value) { m5ioe1_ = value; }
  void set_activity(papermono_activity::PaperMonoActivityComponent *value) { activity_ = value; }
  void set_irq_pin(InternalGPIOPin *value) { irq_pin_ = value; }
  void set_uid_sensor(text_sensor::TextSensor *value) { uid_sensor_ = value; }
  void set_controls(controls::Controls *value) { controls_ = value; }

  void prepare_for_light_sleep();
  void resume_after_user_wake();
  void resume_after_light_sleep_failure();

 private:
  enum class State : uint8_t {
    WAIT_WAKEUP,
    NFC_READ,
    WAIT_NFC_CONFIRMATION,
    WAIT_CARD_REMOVAL,
  };

  static void IRAM_ATTR irq_isr_(PaperMonoNfc *self);

  bool read_reg_(uint8_t reg, uint8_t &value);
  bool read_regs_(uint8_t reg, uint8_t *data, size_t length);
  bool write_reg_(uint8_t reg, uint8_t value);
  bool write_regs_(uint8_t reg, const uint8_t *data, size_t length);
  bool direct_command_(uint8_t command);
  bool read_irq_(uint8_t &main_irq, uint8_t &timer_irq, uint8_t &error_irq, uint8_t &target_irq);
  bool clear_interrupts_();
  bool measure_amplitude_(uint8_t &value);
  bool configure_wakeup_();
  bool arm_wakeup_();
  bool enter_power_down_();
  bool enter_active_reader_();
  bool enter_removal_wait_();
  bool recover_false_wakeup_();
  bool card_removed_();
  bool power_on_and_arm_();
  bool confirm_uid_(std::string &uid);

  m5ioe1::M5IOE1Component *m5ioe1_{nullptr};
  papermono_activity::PaperMonoActivityComponent *activity_{nullptr};
  InternalGPIOPin *irq_pin_{nullptr};
  text_sensor::TextSensor *uid_sensor_{nullptr};
  controls::Controls *controls_{nullptr};
  official::St25r3916NfcA driver_;
  State state_{State::WAIT_WAKEUP};
  volatile bool irq_seen_{false};
  bool initialized_{false};
  uint32_t next_action_ms_{0};
  uint8_t amplitude_reference_{0};
  uint8_t removal_stable_samples_{0};
  uint8_t nfc_retry_count_{0};
};

}  // namespace esphome::papermono_nfc
