#include "nfc_original_diagnostic.h"

#include <cstdio>
#include <string>
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/components/m5ioe1/m5ioe1.h"

// Expose only the original driver's NFC-A transaction primitives to this
// diagnostic translation unit; the protected driver files are untouched.
#define private public
#include "../../../../../../../components/papermono_nfc/st25r3916_official.h"
#undef private
#include "../../../../../../../components/papermono_nfc/st25r3916_official.cpp"

namespace esphome::nfc_original_diagnostic {
static const char *const TAG = "nfc_original_diag";
static papermono_nfc::official::St25r3916NfcA *driver_instance = nullptr;

void NfcOriginalDiagnostic::setup() {
  if (m5ioe1_ == nullptr) { ESP_LOGE(TAG, "M5IOE1 missing"); mark_failed(); return; }
  m5ioe1_->pin_mode(4, gpio::FLAG_OUTPUT);
  m5ioe1_->set_pin_output_level(4, true);
  delay(10);

  driver_instance = new papermono_nfc::official::St25r3916NfcA(this);
  uint8_t id = 0;
  const uint8_t op = 0x40 | 0x3F;
  if (write_read(&op, 1, &id, 1) != i2c::ERROR_OK)
    ESP_LOGE(TAG, "IC_ID read failed");
  else
    ESP_LOGI(TAG, "IC_ID=0x%02X", id);
  if (!driver_instance->begin()) { ESP_LOGE(TAG, "ST25R3916 begin failed"); mark_failed(); return; }
  next_attempt_ms_ = millis();
}

void NfcOriginalDiagnostic::loop() {
  if (driver_instance == nullptr || static_cast<uint32_t>(millis() - next_attempt_ms_) >= 0x80000000UL)
    return;
  if (millis() < next_attempt_ms_) return;
  next_attempt_ms_ = millis() + 2000;
  ESP_LOGI(TAG, "NFC_DIAG_ATTEMPT=%lu", static_cast<unsigned long>(++attempt_));

  uint16_t atqa = 0;
  if (!driver_instance->request_(atqa, false)) { ESP_LOGW(TAG, "REQA failed"); return; }
  ESP_LOGI(TAG, "ATQA=0x%04X", atqa);

  uint8_t full_uid[10]{}, total = 0;
  for (uint8_t level = 1; level <= 3; ++level) {
    uint8_t cl[5]{};
    if (!driver_instance->anticollision_(level, cl)) { ESP_LOGW(TAG, "anticollision failed"); return; }
    if ((cl[0] ^ cl[1] ^ cl[2] ^ cl[3]) != cl[4]) { ESP_LOGW(TAG, "anticollision BCC failed"); return; }
    const bool cascade = cl[0] == 0x88;
    memcpy(full_uid + total, cl + (cascade ? 1 : 0), cascade ? 3 : 4);
    total = static_cast<uint8_t>(total + (cascade ? 3 : 4));
    uint8_t sak = 0;
    if (!driver_instance->select_(level, cl, sak)) { ESP_LOGW(TAG, "SELECT failed"); return; }
    ESP_LOGI(TAG, "SAK=0x%02X", sak);
    if ((sak & 0x04) == 0) break;
  }
  ESP_LOGI(TAG, "UID=%s", driver_instance->format_uid_(full_uid, total).c_str());
}
}
