#include "nfc_wakeup_diagnostic.h"

#include <algorithm>

#include "esphome/components/m5ioe1/m5ioe1.h"
#include "st25r3916_official.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

// This standalone diagnostic does not instantiate the normal papermono_nfc
// component, so compile the unchanged validated driver exactly once here.
#include "st25r3916_official.cpp"

namespace esphome::nfc_wakeup_diagnostic {
namespace {

static const char *const TAG = "nfc_wakeup_diag";

constexpr uint8_t NFC_ENABLE_PIN = 4;

constexpr uint8_t OP_READ = 0x40;
constexpr uint8_t OP_OPERATION_CONTROL = 0x02;
constexpr uint8_t OP_LOAD_FIFO = 0x80;

constexpr uint8_t CMD_STOP_ALL = 0xC2;
constexpr uint8_t CMD_MEASURE_AMPLITUDE = 0xD3;

constexpr uint8_t REG_MASK_MAIN_IRQ = 0x16;
constexpr uint8_t REG_MASK_ERROR_WAKEUP_IRQ = 0x18;
constexpr uint8_t REG_MAIN_IRQ = 0x1A;
constexpr uint8_t REG_ERROR_WAKEUP_IRQ = 0x1C;
constexpr uint8_t REG_PASSIVE_TARGET_IRQ = 0x1D;
constexpr uint8_t REG_ADC_OUTPUT = 0x25;
constexpr uint8_t REG_WAKEUP_TIMER_CONTROL = 0x32;
constexpr uint8_t REG_AMPLITUDE_CONFIGURATION = 0x33;
constexpr uint8_t REG_AMPLITUDE_REFERENCE = 0x34;
constexpr uint8_t REG_MODE = 0x03;
constexpr uint8_t REG_ISO14443A = 0x05;
constexpr uint8_t REG_AUXILIARY = 0x0A;
constexpr uint8_t REG_RECEIVER_1 = 0x0B;
constexpr uint8_t REG_FIFO_STATUS_1 = 0x1E;
constexpr uint8_t REG_TX_LENGTH = 0x22;
constexpr uint8_t CMD_CLEAR_FIFO = 0xDB;

constexpr uint8_t OP_WAKEUP = 0x04;
constexpr uint8_t OP_POWER_DOWN = 0x00;
constexpr uint8_t OP_ACTIVE_READER = 0xC8;
constexpr uint8_t I_WAM = 0x04;
constexpr uint8_t AMPLITUDE_CONFIGURATION_DELTA_3 = 0x30;
constexpr uint8_t AMPLITUDE_DELTA_VALUE = 3;
constexpr uint8_t REMOVAL_DELTA = 1;
constexpr uint8_t NFC_CONFIRMATION_RETRIES = 5;
constexpr uint32_t NFC_CONFIRMATION_INTERVAL_MS = 40;
constexpr uint32_t NFC_REMOVAL_CHECK_INTERVAL_MS = 100;

}  // namespace

void IRAM_ATTR NfcWakeupDiagnostic::irq_isr_(NfcWakeupDiagnostic *self) {
  self->irq_seen_ = true;
}

bool NfcWakeupDiagnostic::read_regs_(uint8_t reg, uint8_t *data, size_t length) {
  const uint8_t opcode = static_cast<uint8_t>(OP_READ | (reg & 0x3F));
  return this->write_read(&opcode, 1, data, length) == i2c::ERROR_OK;
}

bool NfcWakeupDiagnostic::read_reg_(uint8_t reg, uint8_t &value) {
  return this->read_regs_(reg, &value, 1);
}

bool NfcWakeupDiagnostic::write_regs_(uint8_t reg, const uint8_t *data, size_t length) {
  uint8_t buffer[1 + 16]{};
  if (length > 16) return false;
  buffer[0] = static_cast<uint8_t>(reg >= OP_LOAD_FIFO ? reg : (reg & 0x3F));
  std::copy(data, data + length, buffer + 1);
  return this->write(buffer, length + 1) == i2c::ERROR_OK;
}

bool NfcWakeupDiagnostic::write_reg_(uint8_t reg, uint8_t value) {
  return this->write_regs_(reg, &value, 1);
}

bool NfcWakeupDiagnostic::direct_command_(uint8_t command) {
  return this->write(&command, 1) == i2c::ERROR_OK;
}

bool NfcWakeupDiagnostic::read_irq_(uint8_t &main_irq, uint8_t &timer_irq, uint8_t &error_irq,
                                    uint8_t &target_irq) {
  uint8_t main_timer[2]{};
  // Read Error/Wake-Up before Main: reading Main clears the Error/Wake-Up register.
  if (!this->read_reg_(REG_ERROR_WAKEUP_IRQ, error_irq) ||
      !this->read_regs_(REG_MAIN_IRQ, main_timer, sizeof(main_timer)) ||
      !this->read_reg_(REG_PASSIVE_TARGET_IRQ, target_irq)) {
    return false;
  }
  main_irq = main_timer[0];
  timer_irq = main_timer[1];
  return true;
}

bool NfcWakeupDiagnostic::clear_interrupts_() {
  uint8_t main_irq = 0, timer_irq = 0, error_irq = 0, target_irq = 0;
  return this->read_irq_(main_irq, timer_irq, error_irq, target_irq);
}

bool NfcWakeupDiagnostic::measure_amplitude_(uint8_t &value) {
  value = 0;
  if (!this->clear_interrupts_() || !this->direct_command_(CMD_MEASURE_AMPLITUDE)) return false;
  // The datasheet specifies a 25 us maximum duration for D3; this margin is
  // only for this diagnostic measurement, not for the NFC-A transaction path.
  delay(1);
  if (!this->read_reg_(REG_ADC_OUTPUT, value)) return false;
  return this->clear_interrupts_();
}

bool NfcWakeupDiagnostic::configure_wakeup_() {
  // Fixed amplitude reference, delta=1, no auto-averaging.  The reference is
  // deliberately not changed while WU is active (ST25R3916 datasheet rule).
  if (!this->write_reg_(REG_AMPLITUDE_CONFIGURATION, AMPLITUDE_CONFIGURATION_DELTA_3) ||
      !this->write_reg_(REG_AMPLITUDE_REFERENCE, amplitude_reference_) ||
      // 0x04: 100 ms interval, amplitude measurement enabled, no timer IRQ.
      !this->write_reg_(REG_WAKEUP_TIMER_CONTROL, 0x04)) {
    return false;
  }

  // Mask all sources except I_wam (Error/Wake-Up IRQ bit 2).
  const uint8_t masks[4] = {0xFF, 0xFF, 0xFB, 0xFF};
  return this->write_regs_(REG_MASK_MAIN_IRQ, masks, sizeof(masks));
}

bool NfcWakeupDiagnostic::arm_wakeup_() {
  if (!this->configure_wakeup_() || !this->write_reg_(OP_OPERATION_CONTROL, OP_POWER_DOWN) ||
      !this->clear_interrupts_() || !this->write_reg_(OP_OPERATION_CONTROL, OP_WAKEUP)) {
    ESP_LOGE(TAG, "WAKEUP arm failed");
    return false;
  }
  this->state_ = State::WAIT_WAKEUP;
  this->next_action_ms_ = 0;
  ESP_LOGI(TAG, "WAKEUP armed: amplitude ref=0x%02X delta=3 config=0x04 timer=100ms", this->amplitude_reference_);
  return true;
}

bool NfcWakeupDiagnostic::enter_active_reader_() {
  if (!this->enter_power_down_()) return false;
  ESP_LOGI(TAG, "POST-WU oscillator start");
  this->driver_.rearm();
  delay(5);
  ESP_LOGI(TAG, "POST-WU oscillator ready");
  // Restore the same reader state used by the validated diagnostic before
  // handing the NFC-A exchange back to the unchanged driver.
  const uint8_t rx_config[4] = {0x08, 0x2D, 0xD8, 0x22};
  const uint8_t irq_masks[4] = {0x00, 0x00, 0x00, 0x00};
  if (!this->write_reg_(REG_MODE, 0x09) ||
      !this->write_reg_(REG_ISO14443A, 0x00) || !this->write_reg_(REG_AUXILIARY, 0x00) ||
      !this->write_regs_(REG_RECEIVER_1, rx_config, sizeof(rx_config)) ||
      !this->write_regs_(REG_MASK_MAIN_IRQ, irq_masks, sizeof(irq_masks)) ||
      !this->clear_interrupts_() || !this->direct_command_(CMD_CLEAR_FIFO) ||
      !this->write_reg_(OP_OPERATION_CONTROL, OP_ACTIVE_READER)) {
    return false;
  }
  this->post_wakeup_state_pending_ = true;
  return true;
}

void NfcWakeupDiagnostic::log_post_wakeup_state_() {
  uint8_t op = 0, mode = 0, iso = 0, aux = 0;
  uint8_t rx[4]{};
  uint8_t masks[4]{};
  uint8_t irq_main = 0, irq_timer = 0, irq_error = 0, irq_target = 0;
  uint8_t tx_length[2]{}, fifo_status[2]{};
  if (!this->read_reg_(OP_OPERATION_CONTROL, op) || !this->read_reg_(REG_MODE, mode) ||
      !this->read_reg_(REG_ISO14443A, iso) || !this->read_reg_(REG_AUXILIARY, aux) ||
      !this->read_regs_(REG_RECEIVER_1, rx, sizeof(rx)) ||
      !this->read_regs_(REG_MASK_MAIN_IRQ, masks, sizeof(masks)) ||
      !this->read_irq_(irq_main, irq_timer, irq_error, irq_target) ||
      !this->read_regs_(REG_TX_LENGTH, tx_length, sizeof(tx_length)) ||
      !this->read_regs_(REG_FIFO_STATUS_1, fifo_status, sizeof(fifo_status))) {
    ESP_LOGW(TAG, "POST-WU reader state: read failed");
    return;
  }
  ESP_LOGI(TAG,
           "POST-WU reader state: OP_CONTROL=0x%02X MODE=0x%02X ISO=0x%02X AUX=0x%02X "
           "RX_CONF1=0x%02X RX_CONF2=0x%02X RX_CONF3=0x%02X RX_CONF4=0x%02X",
           op, mode, iso, aux, rx[0], rx[1], rx[2], rx[3]);
  ESP_LOGI(TAG,
           "POST-WU reader state: IRQ_MASK_MAIN=0x%02X IRQ_MASK_TIMER=0x%02X "
           "IRQ_MASK_ERROR_WU=0x%02X IRQ_MASK_PASSIVE=0x%02X IRQ_MAIN=0x%02X IRQ_TIMER=0x%02X "
           "IRQ_ERROR_WU=0x%02X IRQ_TARGET=0x%02X NUM_TX=0x%02X%02X FIFO_STATUS=0x%02X%02X",
           masks[0], masks[1], masks[2], masks[3], irq_main, irq_timer, irq_error, irq_target, tx_length[0],
           tx_length[1], fifo_status[0], fifo_status[1]);
}

bool NfcWakeupDiagnostic::enter_power_down_() {
  return this->direct_command_(CMD_STOP_ALL) && this->write_reg_(OP_OPERATION_CONTROL, OP_POWER_DOWN);
}

bool NfcWakeupDiagnostic::enter_removal_wait_() {
  if (!this->enter_power_down_()) return false;
  this->state_ = State::WAIT_CARD_REMOVAL;
  this->removal_stable_samples_ = 0;
  this->next_action_ms_ = millis();
  return true;
}

bool NfcWakeupDiagnostic::recover_false_wakeup_() {
  if (!this->enter_power_down_()) return false;

  uint8_t measured = 0;
  bool measured_ok = false;
  // Take a short, finite stabilization sample set; the fixed WU reference is
  // intentionally not replaced by these samples.
  for (uint8_t sample = 0; sample < 3; ++sample) {
    uint8_t current = 0;
    if (this->measure_amplitude_(current)) {
      measured = current;
      measured_ok = true;
    }
  }
  if (measured_ok) {
    const uint8_t difference = static_cast<uint8_t>(
        measured > this->amplitude_reference_ ? measured - this->amplitude_reference_
                                               : this->amplitude_reference_ - measured);
    ESP_LOGI(TAG, "WAKEUP false: ref=0x%02X measured=0x%02X delta=%u", this->amplitude_reference_, measured,
             difference);
    if (difference < AMPLITUDE_DELTA_VALUE) {
      return this->arm_wakeup_();
    }
    this->nfc_retry_count_ = 0;
    this->state_ = State::WAIT_NFC_CONFIRMATION;
    this->next_action_ms_ = millis();
    ESP_LOGI(TAG, "WAKEUP NFC pending: ref=0x%02X measured=0x%02X delta=%u", this->amplitude_reference_, measured,
             difference);
    return true;
  } else {
    ESP_LOGW(TAG, "WAKEUP false: amplitude stabilization read failed");
  }
  return this->arm_wakeup_();
}

bool NfcWakeupDiagnostic::card_removed_() {
  uint8_t measured = 0;
  if (!this->measure_amplitude_(measured)) {
    this->removal_stable_samples_ = 0;
    return false;
  }
  const uint8_t difference = static_cast<uint8_t>(
      measured > this->amplitude_reference_ ? measured - this->amplitude_reference_
                                             : this->amplitude_reference_ - measured);
  if (difference <= REMOVAL_DELTA) {
    if (this->removal_stable_samples_ < 3) ++this->removal_stable_samples_;
  } else {
    this->removal_stable_samples_ = 0;
  }
  return this->removal_stable_samples_ >= 3;
}

void NfcWakeupDiagnostic::setup() {
  ESP_LOGI(TAG, "NFC WAKEUP DIAGNOSTIC START");
  if (this->m5ioe1_ == nullptr || this->irq_pin_ == nullptr) {
    ESP_LOGE(TAG, "M5IOE1 or IRQ GPIO6 missing");
    this->mark_failed();
    return;
  }

  this->m5ioe1_->set_pin_output_level(NFC_ENABLE_PIN, true);
  ESP_LOGI(TAG, "PYB_NFC_EN HIGH; delay 10 ms");
  delay(10);

  this->irq_pin_->setup();
  this->irq_pin_->pin_mode(gpio::FLAG_INPUT);
  this->irq_pin_->attach_interrupt(&NfcWakeupDiagnostic::irq_isr_, this, gpio::INTERRUPT_RISING_EDGE);
  ESP_LOGI(TAG, "NFC IRQ GPIO6 configured/enabled (active HIGH, rising edge)");

  ESP_LOGI(TAG, "ST25R3916 begin start");
  if (!this->driver_.begin()) {
    ESP_LOGE(TAG, "ST25R3916 begin FAIL");
    this->mark_failed();
    return;
  }
  ESP_LOGI(TAG, "ST25R3916 begin OK; calibrating amplitude");

  if (!this->enter_removal_wait_()) {
    ESP_LOGE(TAG, "Failed to enter power-down for amplitude calibration");
    this->mark_failed();
    return;
  }
  uint8_t reference = 0;
  if (!this->measure_amplitude_(reference)) {
    ESP_LOGE(TAG, "Amplitude calibration FAIL");
    this->mark_failed();
    return;
  }
  this->amplitude_reference_ = reference;
  ESP_LOGI(TAG, "Amplitude reference=0x%02X threshold_code=0x%02X", this->amplitude_reference_,
           AMPLITUDE_CONFIGURATION_DELTA_3);
  this->initialized_ = true;
  if (!this->arm_wakeup_()) {
    this->mark_failed();
    return;
  }
}

void NfcWakeupDiagnostic::loop() {
  if (!this->initialized_) return;

  if (this->state_ == State::WAIT_WAKEUP) {
    if (!this->irq_seen_ && !this->irq_pin_->digital_read()) return;
    this->irq_seen_ = false;
    uint8_t main_irq = 0, timer_irq = 0, error_irq = 0, target_irq = 0;
    if (!this->read_irq_(main_irq, timer_irq, error_irq, target_irq)) return;
    ESP_LOGI(TAG, "WAKEUP IRQ main=0x%02X timer=0x%02X error=0x%02X target=0x%02X", main_irq, timer_irq,
             error_irq, target_irq);
    if ((error_irq & I_WAM) == 0) {
      this->arm_wakeup_();
      return;
    }
    ESP_LOGI(TAG, "WAKEUP amplitude detected; leaving Wake-Up Mode");
    if (!this->enter_active_reader_()) {
      ESP_LOGE(TAG, "Wake-Up exit failed");
      this->arm_wakeup_();
      return;
    }
    this->state_ = State::NFC_READ;
  }

  if (this->state_ == State::NFC_READ) {
    std::string uid;
    if (this->post_wakeup_state_pending_) {
      this->log_post_wakeup_state_();
      this->post_wakeup_state_pending_ = false;
    }
    ESP_LOGI(TAG, "POST-WU REQA start");
    const bool found = this->driver_.poll_uid(uid);
    if (found) {
      ESP_LOGI(TAG, "WAKEUP NFC confirmed");
      ESP_LOGI(TAG, "POST-WU REQA success; NFC-A UID exchange complete");
      ESP_LOGI(TAG, "UID=%s", uid.c_str());
      if (!this->enter_removal_wait_()) {
        ESP_LOGE(TAG, "Failed to enter card-removal wait");
        this->arm_wakeup_();
      }
    } else {
      ESP_LOGI(TAG, "WAKEUP false: NFC read returned NO TAG");
      uint8_t main_irq = 0, timer_irq = 0, error_irq = 0, target_irq = 0;
      uint8_t fifo_status[2]{};
      this->read_irq_(main_irq, timer_irq, error_irq, target_irq);
      this->read_regs_(REG_FIFO_STATUS_1, fifo_status, sizeof(fifo_status));
      const uint16_t fifo_bytes = static_cast<uint16_t>(fifo_status[0] | ((fifo_status[1] & 0xC0) << 2));
      ESP_LOGI(TAG, "POST-WU REQA failed IRQ=%02X %02X %02X %02X FIFO=%u", main_irq, timer_irq, error_irq,
               target_irq, fifo_bytes);
      if (!this->recover_false_wakeup_()) {
        ESP_LOGE(TAG, "Failed to rearm after false Wake-Up");
        this->arm_wakeup_();
      }
    }
    return;
  }

  if (this->state_ == State::WAIT_NFC_CONFIRMATION) {
    if (static_cast<int32_t>(millis() - this->next_action_ms_) < 0) return;

    if (this->nfc_retry_count_ < NFC_CONFIRMATION_RETRIES) {
      ++this->nfc_retry_count_;
      ESP_LOGI(TAG, "WAKEUP NFC retry %u/%u", this->nfc_retry_count_, NFC_CONFIRMATION_RETRIES);
      if (!this->enter_active_reader_()) {
        ESP_LOGW(TAG, "WAKEUP NFC retry setup failed");
        this->next_action_ms_ = millis() + NFC_CONFIRMATION_INTERVAL_MS;
        return;
      }

      std::string uid;
      const bool found = this->driver_.poll_uid(uid);
      if (found) {
        ESP_LOGI(TAG, "WAKEUP NFC confirmed");
        ESP_LOGI(TAG, "UID=%s", uid.c_str());
        if (!this->enter_removal_wait_()) {
          ESP_LOGE(TAG, "Failed to enter card-removal wait");
          this->arm_wakeup_();
        }
        return;
      }

      if (!this->enter_power_down_()) {
        this->next_action_ms_ = millis() + NFC_CONFIRMATION_INTERVAL_MS;
        return;
      }
      uint8_t measured = 0;
      if (this->measure_amplitude_(measured)) {
        const uint8_t difference = static_cast<uint8_t>(
            measured > this->amplitude_reference_ ? measured - this->amplitude_reference_
                                                   : this->amplitude_reference_ - measured);
        if (difference < AMPLITUDE_DELTA_VALUE) {
          ESP_LOGI(TAG, "WAKEUP perturbation removed; rearming");
          this->arm_wakeup_();
          return;
        }
      }
      this->next_action_ms_ = millis() + NFC_CONFIRMATION_INTERVAL_MS;
      if (this->nfc_retry_count_ == NFC_CONFIRMATION_RETRIES) {
        ESP_LOGI(TAG, "WAKEUP perturbation not NFC; waiting for removal");
      }
      return;
    }

    // After the finite NFC confirmation budget, do not transmit again.  The
    // chip stays in power-down while amplitude alone is watched for removal.
    this->next_action_ms_ = millis() + NFC_REMOVAL_CHECK_INTERVAL_MS;
    uint8_t measured = 0;
    if (this->measure_amplitude_(measured)) {
      const uint8_t difference = static_cast<uint8_t>(
          measured > this->amplitude_reference_ ? measured - this->amplitude_reference_
                                                 : this->amplitude_reference_ - measured);
      if (difference < AMPLITUDE_DELTA_VALUE) {
        ESP_LOGI(TAG, "WAKEUP perturbation removed; rearming");
        this->arm_wakeup_();
      }
    }
    return;
  }

  if (static_cast<int32_t>(millis() - this->next_action_ms_) < 0) return;
  this->next_action_ms_ = millis() + 100;
  if (this->card_removed_()) {
    ESP_LOGI(TAG, "Card removed; rearming Wake-Up Mode");
    this->arm_wakeup_();
  }
}

}  // namespace esphome::nfc_wakeup_diagnostic
