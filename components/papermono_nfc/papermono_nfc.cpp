#include "papermono_nfc.h"

#include <algorithm>

#include "esphome/components/m5ioe1/m5ioe1.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::papermono_nfc {
namespace {

static const char *const TAG = "papermono_nfc";

constexpr uint8_t NFC_ENABLE_PIN = 4;
constexpr uint8_t OP_READ = 0x40;
constexpr uint8_t OP_OPERATION_CONTROL = 0x02;
constexpr uint8_t OP_LOAD_FIFO = 0x80;
constexpr uint8_t CMD_STOP_ALL = 0xC2;
constexpr uint8_t CMD_MEASURE_AMPLITUDE = 0xD3;
constexpr uint8_t CMD_CLEAR_FIFO = 0xDB;
constexpr uint8_t REG_MASK_MAIN_IRQ = 0x16;
constexpr uint8_t REG_MAIN_IRQ = 0x1A;
constexpr uint8_t REG_ERROR_WAKEUP_IRQ = 0x1C;
constexpr uint8_t REG_PASSIVE_TARGET_IRQ = 0x1D;
constexpr uint8_t REG_ADC_OUTPUT = 0x25;
constexpr uint8_t REG_MODE = 0x03;
constexpr uint8_t REG_ISO14443A = 0x05;
constexpr uint8_t REG_AUXILIARY = 0x0A;
constexpr uint8_t REG_RECEIVER_1 = 0x0B;
constexpr uint8_t REG_WAKEUP_TIMER_CONTROL = 0x32;
constexpr uint8_t REG_AMPLITUDE_CONFIGURATION = 0x33;
constexpr uint8_t REG_AMPLITUDE_REFERENCE = 0x34;
constexpr uint8_t I_WAM = 0x04;
constexpr uint8_t AMPLITUDE_CONFIGURATION_DELTA_3 = 0x30;
constexpr uint8_t AMPLITUDE_DELTA_VALUE = 3;
constexpr uint8_t REMOVAL_DELTA = 1;
constexpr uint8_t NFC_CONFIRMATION_RETRIES = 5;
constexpr uint32_t NFC_CONFIRMATION_INTERVAL_MS = 40;
constexpr uint32_t NFC_REMOVAL_CHECK_INTERVAL_MS = 100;

}  // namespace

void IRAM_ATTR PaperMonoNfc::irq_isr_(PaperMonoNfc *self) { self->irq_seen_ = true; }

bool PaperMonoNfc::read_regs_(uint8_t reg, uint8_t *data, size_t length) {
  const uint8_t opcode = static_cast<uint8_t>(OP_READ | (reg & 0x3F));
  return this->write_read(&opcode, 1, data, length) == i2c::ERROR_OK;
}

bool PaperMonoNfc::read_reg_(uint8_t reg, uint8_t &value) { return this->read_regs_(reg, &value, 1); }

bool PaperMonoNfc::write_regs_(uint8_t reg, const uint8_t *data, size_t length) {
  uint8_t buffer[17]{};
  if (length > 16) return false;
  buffer[0] = static_cast<uint8_t>(reg >= OP_LOAD_FIFO ? reg : (reg & 0x3F));
  std::copy(data, data + length, buffer + 1);
  return this->write(buffer, length + 1) == i2c::ERROR_OK;
}

bool PaperMonoNfc::write_reg_(uint8_t reg, uint8_t value) { return this->write_regs_(reg, &value, 1); }

bool PaperMonoNfc::direct_command_(uint8_t command) { return this->write(&command, 1) == i2c::ERROR_OK; }

bool PaperMonoNfc::read_irq_(uint8_t &main_irq, uint8_t &timer_irq, uint8_t &error_irq, uint8_t &target_irq) {
  uint8_t main_timer[2]{};
  // Read Error/Wake-Up before Main because reading Main clears Error/Wake-Up.
  if (!this->read_reg_(REG_ERROR_WAKEUP_IRQ, error_irq) ||
      !this->read_regs_(REG_MAIN_IRQ, main_timer, sizeof(main_timer)) ||
      !this->read_reg_(REG_PASSIVE_TARGET_IRQ, target_irq)) {
    return false;
  }
  main_irq = main_timer[0];
  timer_irq = main_timer[1];
  return true;
}

bool PaperMonoNfc::clear_interrupts_() {
  uint8_t main_irq = 0, timer_irq = 0, error_irq = 0, target_irq = 0;
  return this->read_irq_(main_irq, timer_irq, error_irq, target_irq);
}

bool PaperMonoNfc::measure_amplitude_(uint8_t &value) {
  value = 0;
  if (!this->clear_interrupts_() || !this->direct_command_(CMD_MEASURE_AMPLITUDE)) return false;
  delay(1);
  if (!this->read_reg_(REG_ADC_OUTPUT, value)) return false;
  return this->clear_interrupts_();
}

bool PaperMonoNfc::configure_wakeup_() {
  if (!this->write_reg_(REG_AMPLITUDE_CONFIGURATION, AMPLITUDE_CONFIGURATION_DELTA_3) ||
      !this->write_reg_(REG_AMPLITUDE_REFERENCE, this->amplitude_reference_) ||
      !this->write_reg_(REG_WAKEUP_TIMER_CONTROL, 0x04)) {
    return false;
  }
  const uint8_t masks[4] = {0xFF, 0xFF, 0xFB, 0xFF};
  return this->write_regs_(REG_MASK_MAIN_IRQ, masks, sizeof(masks));
}

bool PaperMonoNfc::arm_wakeup_() {
  if (!this->configure_wakeup_() || !this->write_reg_(OP_OPERATION_CONTROL, 0x00) ||
      !this->clear_interrupts_() || !this->write_reg_(OP_OPERATION_CONTROL, 0x04)) {
    ESP_LOGE(TAG, "NFC low-power detection arm failed");
    return false;
  }
  this->state_ = State::WAIT_WAKEUP;
  this->next_action_ms_ = 0;
  ESP_LOGI(TAG, "NFC low-power detection armed: ref=0x%02X delta=3 timer=100ms", this->amplitude_reference_);
  return true;
}

bool PaperMonoNfc::enter_power_down_() {
  return this->direct_command_(CMD_STOP_ALL) && this->write_reg_(OP_OPERATION_CONTROL, 0x00);
}

bool PaperMonoNfc::enter_active_reader_() {
  if (!this->enter_power_down_()) return false;
  this->driver_.rearm();
  delay(5);

  // This is the validated post-Wake-Up reader restoration sequence.
  const uint8_t rx_config[4] = {0x08, 0x2D, 0xD8, 0x22};
  const uint8_t irq_masks[4] = {0x00, 0x00, 0x00, 0x00};
  return this->write_reg_(REG_MODE, 0x09) && this->write_reg_(REG_ISO14443A, 0x00) &&
         this->write_reg_(REG_AUXILIARY, 0x00) && this->write_regs_(REG_RECEIVER_1, rx_config, sizeof(rx_config)) &&
         this->write_regs_(REG_MASK_MAIN_IRQ, irq_masks, sizeof(irq_masks)) && this->clear_interrupts_() &&
         this->direct_command_(CMD_CLEAR_FIFO) && this->write_reg_(OP_OPERATION_CONTROL, 0xC8);
}

bool PaperMonoNfc::confirm_uid_(std::string &uid) { return this->driver_.poll_uid(uid); }

bool PaperMonoNfc::enter_removal_wait_() {
  if (!this->enter_power_down_()) return false;
  this->state_ = State::WAIT_CARD_REMOVAL;
  this->removal_stable_samples_ = 0;
  this->next_action_ms_ = millis();
  return true;
}

bool PaperMonoNfc::recover_false_wakeup_() {
  if (!this->enter_power_down_()) return false;
  uint8_t measured = 0;
  bool measured_ok = false;
  for (uint8_t sample = 0; sample < 3; ++sample) {
    uint8_t current = 0;
    if (this->measure_amplitude_(current)) {
      measured = current;
      measured_ok = true;
    }
  }
  if (!measured_ok) return this->arm_wakeup_();
  const uint8_t difference = static_cast<uint8_t>(
      measured > this->amplitude_reference_ ? measured - this->amplitude_reference_ : this->amplitude_reference_ - measured);
  if (difference < AMPLITUDE_DELTA_VALUE) return this->arm_wakeup_();
  this->nfc_retry_count_ = 0;
  this->state_ = State::WAIT_NFC_CONFIRMATION;
  this->next_action_ms_ = millis();
  ESP_LOGI(TAG, "NFC amplitude pending: ref=0x%02X measured=0x%02X delta=%u", this->amplitude_reference_, measured,
           difference);
  return true;
}

bool PaperMonoNfc::card_removed_() {
  uint8_t measured = 0;
  if (!this->measure_amplitude_(measured)) {
    this->removal_stable_samples_ = 0;
    return false;
  }
  const uint8_t difference = static_cast<uint8_t>(
      measured > this->amplitude_reference_ ? measured - this->amplitude_reference_ : this->amplitude_reference_ - measured);
  if (difference <= REMOVAL_DELTA) {
    if (this->removal_stable_samples_ < 3) ++this->removal_stable_samples_;
  } else {
    this->removal_stable_samples_ = 0;
  }
  return this->removal_stable_samples_ >= 3;
}

bool PaperMonoNfc::power_on_and_arm_() {
  if (this->m5ioe1_ == nullptr) return false;
  this->m5ioe1_->set_pin_output_level(NFC_ENABLE_PIN, true);
  ESP_LOGI(TAG, "NFC power ON after wake");
  delay(10);
  if (!this->driver_.begin()) return false;
  if (!this->enter_power_down_()) return false;
  uint8_t reference = 0;
  if (!this->measure_amplitude_(reference)) return false;
  this->amplitude_reference_ = reference;
  this->initialized_ = true;
  return this->arm_wakeup_();
}

void PaperMonoNfc::setup() {
  ESP_LOGI(TAG, "NFC normal setup start");
  if (this->m5ioe1_ == nullptr || this->activity_ == nullptr || this->irq_pin_ == nullptr || this->uid_sensor_ == nullptr) {
    ESP_LOGE(TAG, "NFC configuration incomplete");
    this->mark_failed();
    return;
  }
  this->m5ioe1_->set_pin_output_level(NFC_ENABLE_PIN, true);
  ESP_LOGI(TAG, "NFC power ON; GPIO4 HIGH");
  delay(10);
  this->irq_pin_->setup();
  this->irq_pin_->pin_mode(gpio::FLAG_INPUT);
  this->irq_pin_->attach_interrupt(&PaperMonoNfc::irq_isr_, this, gpio::INTERRUPT_RISING_EDGE);
  ESP_LOGI(TAG, "NFC IRQ enabled on GPIO6");
  if (!this->driver_.begin()) {
    ESP_LOGE(TAG, "ST25R3916 init failed");
    this->mark_failed();
    return;
  }
  if (!this->enter_power_down_()) {
    this->mark_failed();
    return;
  }
  uint8_t reference = 0;
  if (!this->measure_amplitude_(reference)) {
    ESP_LOGE(TAG, "NFC amplitude reference calibration failed");
    this->mark_failed();
    return;
  }
  this->amplitude_reference_ = reference;
  this->initialized_ = true;
  if (!this->arm_wakeup_()) {
    this->mark_failed();
    return;
  }
}

void PaperMonoNfc::prepare_for_light_sleep() {
  ESP_LOGI(TAG, "NFC stopping for light sleep");
  this->initialized_ = false;
  this->irq_seen_ = false;
  if (this->irq_pin_ != nullptr) this->irq_pin_->detach_interrupt();
  if (this->m5ioe1_ != nullptr) {
    this->enter_power_down_();
    this->m5ioe1_->set_pin_output_level(NFC_ENABLE_PIN, false);
    ESP_LOGI(TAG, "NFC power OFF before light sleep");
  }
}

void PaperMonoNfc::resume_after_user_wake() {
  if (this->initialized_) return;
  if (!this->power_on_and_arm_()) {
    ESP_LOGE(TAG, "NFC resume after user wake failed");
    return;
  }
  this->irq_pin_->setup();
  this->irq_pin_->pin_mode(gpio::FLAG_INPUT);
  this->irq_pin_->attach_interrupt(&PaperMonoNfc::irq_isr_, this, gpio::INTERRUPT_RISING_EDGE);
}

void PaperMonoNfc::resume_after_light_sleep_failure() {
  if (this->initialized_) return;
  if (!this->power_on_and_arm_()) ESP_LOGE(TAG, "NFC resume after failed light sleep failed");
  else {
    this->irq_pin_->setup();
    this->irq_pin_->pin_mode(gpio::FLAG_INPUT);
    this->irq_pin_->attach_interrupt(&PaperMonoNfc::irq_isr_, this, gpio::INTERRUPT_RISING_EDGE);
  }
}

void PaperMonoNfc::loop() {
  if (!this->initialized_) return;

  if (this->state_ == State::WAIT_WAKEUP) {
    if (!this->irq_seen_ && !this->irq_pin_->digital_read()) return;
    this->irq_seen_ = false;
    uint8_t main_irq = 0, timer_irq = 0, error_irq = 0, target_irq = 0;
    if (!this->read_irq_(main_irq, timer_irq, error_irq, target_irq)) return;
    if ((error_irq & I_WAM) == 0) {
      this->arm_wakeup_();
      return;
    }
    ESP_LOGI(TAG, "NFC amplitude IRQ");
    if (!this->enter_active_reader_()) {
      this->arm_wakeup_();
      return;
    }
    this->state_ = State::NFC_READ;
  }

  if (this->state_ == State::NFC_READ) {
    std::string uid;
    if (this->confirm_uid_(uid)) {
      ESP_LOGI(TAG, "NFC UID confirmed: %s", uid.c_str());
      this->uid_sensor_->publish_state(uid);
      if (!this->enter_removal_wait_()) this->arm_wakeup_();
    } else if (!this->recover_false_wakeup_()) {
      this->arm_wakeup_();
    }
    return;
  }

  if (this->state_ == State::WAIT_NFC_CONFIRMATION) {
    if (static_cast<int32_t>(millis() - this->next_action_ms_) < 0) return;
    if (this->nfc_retry_count_ < NFC_CONFIRMATION_RETRIES) {
      ++this->nfc_retry_count_;
      ESP_LOGI(TAG, "NFC confirmation retry %u/%u", this->nfc_retry_count_, NFC_CONFIRMATION_RETRIES);
      if (!this->enter_active_reader_()) {
        this->next_action_ms_ = millis() + NFC_CONFIRMATION_INTERVAL_MS;
        return;
      }
      std::string uid;
      if (this->confirm_uid_(uid)) {
        ESP_LOGI(TAG, "NFC UID confirmed: %s", uid.c_str());
        this->uid_sensor_->publish_state(uid);
        if (!this->enter_removal_wait_()) this->arm_wakeup_();
        return;
      }
      this->enter_power_down_();
      uint8_t measured = 0;
      if (this->measure_amplitude_(measured)) {
        const uint8_t difference = static_cast<uint8_t>(
            measured > this->amplitude_reference_ ? measured - this->amplitude_reference_ : this->amplitude_reference_ - measured);
        if (difference < AMPLITUDE_DELTA_VALUE) {
          ESP_LOGI(TAG, "NFC amplitude perturbation removed; rearming");
          this->arm_wakeup_();
          return;
        }
      }
      this->next_action_ms_ = millis() + NFC_CONFIRMATION_INTERVAL_MS;
      return;
    }

    this->next_action_ms_ = millis() + NFC_REMOVAL_CHECK_INTERVAL_MS;
    uint8_t measured = 0;
    if (this->measure_amplitude_(measured)) {
      const uint8_t difference = static_cast<uint8_t>(
          measured > this->amplitude_reference_ ? measured - this->amplitude_reference_ : this->amplitude_reference_ - measured);
      if (difference < AMPLITUDE_DELTA_VALUE) {
        ESP_LOGI(TAG, "NFC amplitude perturbation removed; rearming");
        this->arm_wakeup_();
      }
    }
    return;
  }

  if (static_cast<int32_t>(millis() - this->next_action_ms_) < 0) return;
  this->next_action_ms_ = millis() + NFC_REMOVAL_CHECK_INTERVAL_MS;
  if (this->card_removed_()) {
    ESP_LOGI(TAG, "NFC card removed; low-power detection rearmed");
    this->arm_wakeup_();
  }
}

}  // namespace esphome::papermono_nfc
