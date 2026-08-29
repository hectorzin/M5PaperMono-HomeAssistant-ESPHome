/*
 * SPDX-FileCopyrightText: 2026 PaperMono Home Assistant contributors
 * SPDX-License-Identifier: MIT
 *
 * RX8130 register usage derived from M5Stack M5Unified RX8130_Class.cpp (MIT).
 */
#include "papermono_rtc.h"

#include "esphome/core/log.h"

namespace esphome::papermono_rtc {

static const char *const TAG = "papermono_rtc";

// RX8130 registers (M5Unified RX8130_Class.cpp).
static constexpr uint8_t RX8130_REG_TIMER_CNT0 = 0x1A;
static constexpr uint8_t RX8130_REG_EXT = 0x1C;
static constexpr uint8_t RX8130_REG_FLAG = 0x1D;
static constexpr uint8_t RX8130_REG_CTRL = 0x1E;
static constexpr uint8_t RX8130_REG_INIT = 0x1F;

static constexpr uint8_t RX8130_FLAG_CLEAR_TF_AF = 0xA7;
static constexpr uint8_t RX8130_EXT_TE = 0x10;
static constexpr uint8_t RX8130_CTRL_TIE = 0x10;

void PaperMonoRtcComponent::dump_config() { LOG_I2C_DEVICE(this); }

bool PaperMonoRtcComponent::update_reg_bit_(uint8_t reg, uint8_t mask, bool set) {
  uint8_t value = 0;
  if (!this->read_byte(reg, &value)) {
    return false;
  }
  if (set) {
    value |= mask;
  } else {
    value &= static_cast<uint8_t>(~mask);
  }
  return this->write_byte(reg, value);
}

bool PaperMonoRtcComponent::bit_on_(uint8_t reg, uint8_t mask) { return this->update_reg_bit_(reg, mask, true); }

bool PaperMonoRtcComponent::bit_off_(uint8_t reg, uint8_t mask) { return this->update_reg_bit_(reg, mask, false); }

bool PaperMonoRtcComponent::probe_device_() {
  if (!this->bit_on_(RX8130_REG_INIT, 0x30)) {
    return false;
  }
  if (!this->write_byte(0x30, 0x00)) {
    return false;
  }
  if (!this->write_byte(RX8130_REG_CTRL, 0x00)) {
    return false;
  }
  return true;
}

void PaperMonoRtcComponent::setup() {
  this->ready_ = this->probe_device_();
  if (this->ready_) {
    ESP_LOGI(TAG, "RX8130 ready at I2C 0x%02X", this->address_);
  } else {
    ESP_LOGE(TAG, "RX8130 probe failed at I2C 0x%02X", this->address_);
  }
}

uint8_t PaperMonoRtcComponent::read_irq_flags_raw() {
  uint8_t flags = 0;
  if (!this->ready_ || !this->read_byte(RX8130_REG_FLAG, &flags)) {
    return 0;
  }
  return flags;
}

void PaperMonoRtcComponent::clear_irq() {
  if (!this->ready_) {
    return;
  }
  this->write_byte(RX8130_REG_FLAG, RX8130_FLAG_CLEAR_TF_AF);
}

void PaperMonoRtcComponent::disable_irq() {
  if (!this->ready_) {
    return;
  }
  this->bit_off_(RX8130_REG_CTRL, 0x18);
  this->write_byte(RX8130_REG_FLAG, RX8130_FLAG_CLEAR_TF_AF);
}

bool PaperMonoRtcComponent::is_timer_irq_active() {
  if (!this->ready_) {
    return false;
  }
  uint8_t flags = 0;
  if (!this->read_byte(RX8130_REG_FLAG, &flags)) {
    return false;
  }
  return (flags & 0x10) != 0;
}

bool PaperMonoRtcComponent::stop_timer_() {
  for (int retry = 0; retry < 3; ++retry) {
    uint8_t ext = 0;
    uint8_t ctl = 0;
    if (!this->bit_off_(RX8130_REG_EXT, RX8130_EXT_TE)) {
      continue;
    }
    if (!this->bit_off_(RX8130_REG_CTRL, RX8130_CTRL_TIE)) {
      continue;
    }
    if (!this->read_byte(RX8130_REG_EXT, &ext)) {
      continue;
    }
    if (!this->read_byte(RX8130_REG_CTRL, &ctl)) {
      continue;
    }
    if ((ext & RX8130_EXT_TE) == 0 && (ctl & RX8130_CTRL_TIE) == 0) {
      return true;
    }
  }
  return false;
}

uint32_t PaperMonoRtcComponent::set_timer_irq(uint32_t msec) {
  if (!this->ready_) {
    return 0;
  }

  struct Clk {
    uint32_t mul_ms;
    uint32_t div;
    uint32_t max_ms;
    uint16_t max_cnt;
    uint8_t tsel;
  };
  static constexpr Clk clks[] = {
      {1000, 64, 1023984, 65535, 0x01},
      {1000, 1, 65535000, 65535, 0x02},
      {60000, 1, 3932100000U, 65535, 0x03},
      {3600000, 1, 0xFFFFFFFFU, 1193, 0x04},
      {1000, 4096, 15999, 65535, 0x00},
  };
  static constexpr size_t nclk = sizeof(clks) / sizeof(clks[0]);
  static constexpr uint32_t min_count = 16;

  uint32_t cycle = 0;
  const Clk *sel = nullptr;
  if (msec != 0) {
    bool overflowed = false;
    for (size_t i = 0; i < nclk; ++i) {
      const Clk &c = clks[i];
      if (msec > c.max_ms) {
        overflowed = true;
        continue;
      }
      uint32_t num = msec * c.div;
      uint32_t cnt = num / c.mul_ms;
      uint32_t err = num % c.mul_ms;
      if (overflowed ? (err != 0) : (err * 2 >= c.mul_ms)) {
        ++cnt;
        err = c.mul_ms - err;
      }
      if (cnt > c.max_cnt) {
        cnt = c.max_cnt;
        err = num - cnt * c.mul_ms;
      }
      if (i + 1 == nclk || (cnt >= min_count && (err << 8) <= num)) {
        sel = &c;
        cycle = cnt;
        break;
      }
    }
    if (sel == nullptr) {
      return 0;
    }
  }

  if (cycle == 0) {
    this->stop_timer_();
    this->write_byte(RX8130_REG_FLAG, RX8130_FLAG_CLEAR_TF_AF);
    return 0;
  }

  uint8_t reg_ext = 0;
  bool ok = this->read_byte(RX8130_REG_EXT, &reg_ext);
  if (ok) {
    reg_ext = static_cast<uint8_t>((reg_ext & ~0x17U) | sel->tsel);
    ok = this->write_byte(RX8130_REG_EXT, reg_ext) && this->write_byte(RX8130_REG_FLAG, RX8130_FLAG_CLEAR_TF_AF) &&
         this->bit_on_(RX8130_REG_CTRL, RX8130_CTRL_TIE);
  }

  if (ok) {
    const uint8_t regdata[2] = {static_cast<uint8_t>(cycle & 0xFF), static_cast<uint8_t>((cycle >> 8) & 0xFF)};
    ok = false;
    for (int retry = 0; retry < 3 && !ok; ++retry) {
      uint8_t verify[2] = {0, 0};
      ok = this->write_bytes(RX8130_REG_TIMER_CNT0, regdata, 2) && this->read_bytes(RX8130_REG_TIMER_CNT0, verify, 2) &&
           verify[0] == regdata[0] && verify[1] == regdata[1];
    }
  }

  if (ok) {
    ok = this->write_byte(RX8130_REG_EXT, static_cast<uint8_t>(reg_ext | RX8130_EXT_TE));
  }

  if (!ok) {
    this->stop_timer_();
    return 0;
  }

  const uint32_t result = (cycle * sel->mul_ms + (sel->div >> 1)) / sel->div;
  return result != 0 ? result : 1;
}

}  // namespace esphome::papermono_rtc
