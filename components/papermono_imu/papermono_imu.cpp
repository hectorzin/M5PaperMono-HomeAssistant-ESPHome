/*
 * SPDX-FileCopyrightText: 2026 PaperMono Home Assistant contributors
 * SPDX-License-Identifier: MIT
 */
#include "papermono_imu.h"

#include "esphome/components/m5pm1/m5pm1.h"
#include "esphome/components/papermono_activity/papermono_activity.h"
#include "esphome/core/log.h"

extern "C" {
#include "bmi2.h"
#include "bmi270.h"
}

namespace esphome::papermono_imu {

static const char *const TAG = "papermono_imu";

// M5PaperMono-UserDemo app_sleep_wake.cpp
static constexpr uint16_t BMI270_ANY_MOTION_DURATION = 20;
static constexpr uint16_t BMI270_ANY_MOTION_THRESHOLD = 640;
static constexpr uint16_t BMI270_ANY_MOTION_STATUS_MASK = 0x0040;

struct Bmi270BusContext {
  esphome::i2c::I2CDevice *device;
  uint8_t addr;
};

static Bmi270BusContext g_bmi_bus;
static struct bmi2_dev g_bmi_dev;

static int8_t bmi270_i2c_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr) {
  if (reg_data == nullptr || intf_ptr == nullptr || len == 0) {
    return BMI2_E_COM_FAIL;
  }
  auto *bus = static_cast<Bmi270BusContext *>(intf_ptr);
  if (bus->device == nullptr) {
    return BMI2_E_COM_FAIL;
  }
  return bus->device->read_bytes(reg_addr, reg_data, len) ? BMI2_INTF_RET_SUCCESS : BMI2_E_COM_FAIL;
}

static int8_t bmi270_i2c_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr) {
  if (reg_data == nullptr || intf_ptr == nullptr || len == 0) {
    return BMI2_E_COM_FAIL;
  }
  auto *bus = static_cast<Bmi270BusContext *>(intf_ptr);
  if (bus->device == nullptr) {
    return BMI2_E_COM_FAIL;
  }
  return bus->device->write_bytes(reg_addr, reg_data, len) ? BMI2_INTF_RET_SUCCESS : BMI2_E_COM_FAIL;
}

static void bmi270_delay_us(uint32_t period, void *intf_ptr) {
  (void) intf_ptr;
  delayMicroseconds(period);
}

static void init_bmi_device_(struct bmi2_dev *dev, Bmi270BusContext *bus) {
  *dev = {};
  dev->chip_id = bus->addr;
  dev->read = bmi270_i2c_read;
  dev->write = bmi270_i2c_write;
  dev->delay_us = bmi270_delay_us;
  dev->intf = BMI2_I2C_INTF;
  dev->intf_ptr = bus;
  dev->read_write_len = 30;
  dev->config_file_ptr = nullptr;
}

void PaperMonoImuComponent::dump_config() {
  LOG_I2C_DEVICE(this);
  ESP_LOGCONFIG(TAG, "  Any-motion threshold: %u (~%.0f mg)", BMI270_ANY_MOTION_THRESHOLD,
                BMI270_ANY_MOTION_THRESHOLD * 0.48f);
  ESP_LOGCONFIG(TAG, "  Any-motion duration: %u (%u ms)", BMI270_ANY_MOTION_DURATION,
                BMI270_ANY_MOTION_DURATION * 20);
}

bool PaperMonoImuComponent::read_interrupt_status_(uint16_t *status) {
  uint8_t data[2] = {0, 0};
  if (!this->read_bytes(BMI2_INT_STATUS_0_ADDR, data, sizeof(data))) {
    return false;
  }
  *status = static_cast<uint16_t>(data[0] | (static_cast<uint16_t>(data[1]) << 8));
  return true;
}

bool PaperMonoImuComponent::configure_any_motion_() {
  g_bmi_bus.device = this;
  int8_t result = BMI2_E_DEV_NOT_FOUND;

  for (const uint8_t candidate : {static_cast<uint8_t>(0x68), static_cast<uint8_t>(0x69)}) {
    g_bmi_bus.addr = candidate;
    init_bmi_device_(&g_bmi_dev, &g_bmi_bus);
    result = bmi270_init(&g_bmi_dev);
    if (result == BMI2_OK) {
      this->bmi_addr_ = candidate;
      break;
    }
    ESP_LOGW(TAG, "BMI270 init addr=0x%02X failed: %d", candidate, static_cast<int>(result));
  }

  if (result != BMI2_OK) {
    ESP_LOGE(TAG, "BMI270 not found on main I2C bus");
    return false;
  }

  struct bmi2_sens_config base_config[2] = {};
  base_config[0].type = BMI2_ACCEL;
  base_config[1].type = BMI2_GYRO;
  result = bmi270_get_sensor_config(base_config, 2, &g_bmi_dev);
  if (result != BMI2_OK) {
    ESP_LOGE(TAG, "BMI270 get accel/gyro config failed: %d", static_cast<int>(result));
    return false;
  }

  base_config[0].cfg.acc.filter_perf = BMI2_PERF_OPT_MODE;
  base_config[0].cfg.acc.bwp = BMI2_ACC_OSR2_AVG2;
  base_config[0].cfg.acc.odr = BMI2_ACC_ODR_100HZ;
  base_config[0].cfg.acc.range = BMI2_ACC_RANGE_2G;
  base_config[1].cfg.gyr.filter_perf = BMI2_PERF_OPT_MODE;
  base_config[1].cfg.gyr.noise_perf = BMI2_PERF_OPT_MODE;
  base_config[1].cfg.gyr.bwp = BMI2_GYR_OSR2_MODE;
  base_config[1].cfg.gyr.odr = BMI2_GYR_ODR_100HZ;
  base_config[1].cfg.gyr.range = BMI2_GYR_RANGE_2000;
  base_config[1].cfg.gyr.ois_range = BMI2_GYR_OIS_2000;
  result = bmi270_set_sensor_config(base_config, 2, &g_bmi_dev);
  if (result != BMI2_OK) {
    ESP_LOGE(TAG, "BMI270 set accel/gyro config failed: %d", static_cast<int>(result));
    return false;
  }

  uint8_t base_sensor_list[] = {BMI2_ACCEL, BMI2_GYRO};
  result = bmi270_sensor_enable(base_sensor_list, 2, &g_bmi_dev);
  if (result != BMI2_OK) {
    ESP_LOGE(TAG, "BMI270 enable accel/gyro failed: %d", static_cast<int>(result));
    return false;
  }

  struct bmi2_int_pin_config int_pin_config = {};
  int_pin_config.pin_type = BMI2_INT1;
  int_pin_config.int_latch = BMI2_INT_NON_LATCH;
  int_pin_config.pin_cfg[0].lvl = BMI2_INT_ACTIVE_LOW;
  int_pin_config.pin_cfg[0].od = BMI2_INT_PUSH_PULL;
  int_pin_config.pin_cfg[0].output_en = BMI2_INT_OUTPUT_ENABLE;
  int_pin_config.pin_cfg[0].input_en = BMI2_INT_INPUT_DISABLE;
  result = bmi2_set_int_pin_config(&int_pin_config, &g_bmi_dev);
  if (result != BMI2_OK) {
    ESP_LOGE(TAG, "BMI270 set INT1 config failed: %d", static_cast<int>(result));
    return false;
  }

  struct bmi2_sens_config any_motion_config = {};
  any_motion_config.type = BMI2_ANY_MOTION;
  result = bmi270_get_sensor_config(&any_motion_config, 1, &g_bmi_dev);
  if (result != BMI2_OK) {
    ESP_LOGE(TAG, "BMI270 get any-motion config failed: %d", static_cast<int>(result));
    return false;
  }

  any_motion_config.cfg.any_motion.threshold = BMI270_ANY_MOTION_THRESHOLD;
  any_motion_config.cfg.any_motion.duration = BMI270_ANY_MOTION_DURATION;
  any_motion_config.cfg.any_motion.select_x = BMI2_ENABLE;
  any_motion_config.cfg.any_motion.select_y = BMI2_ENABLE;
  any_motion_config.cfg.any_motion.select_z = BMI2_ENABLE;
  result = bmi270_set_sensor_config(&any_motion_config, 1, &g_bmi_dev);
  if (result != BMI2_OK) {
    ESP_LOGE(TAG, "BMI270 set any-motion config failed: %d", static_cast<int>(result));
    return false;
  }

  uint8_t sensor_list[] = {BMI2_ACCEL, BMI2_ANY_MOTION};
  result = bmi270_sensor_enable(sensor_list, 2, &g_bmi_dev);
  if (result != BMI2_OK) {
    ESP_LOGE(TAG, "BMI270 enable accel/any-motion failed: %d", static_cast<int>(result));
    return false;
  }

  result = bmi2_map_feat_int(BMI2_ANY_MOTION, BMI2_INT1, &g_bmi_dev);
  if (result != BMI2_OK) {
    ESP_LOGE(TAG, "BMI270 map any-motion to INT1 failed: %d", static_cast<int>(result));
    return false;
  }

  uint16_t int_status = 0;
  result = bmi2_get_int_status(&int_status, &g_bmi_dev);
  if (result != BMI2_OK) {
    ESP_LOGW(TAG, "BMI270 initial int status read failed: %d", static_cast<int>(result));
  }

  ESP_LOGI(TAG, "BMI270 any-motion configured addr=0x%02X threshold=%u duration=%u int_status=0x%04X",
           this->bmi_addr_, BMI270_ANY_MOTION_THRESHOLD, BMI270_ANY_MOTION_DURATION, int_status);
  return true;
}

void PaperMonoImuComponent::setup() {
  if (this->pmu_ == nullptr || this->activity_ == nullptr) {
    ESP_LOGE(TAG, "M5PM1 or activity reference missing");
    return;
  }

  if (!this->configure_any_motion_()) {
    return;
  }

  if (!this->pmu_->configure_imu_irq_route_()) {
    ESP_LOGW(TAG, "M5PM1 IMU IRQ route config failed");
    return;
  }

  this->pmu_->set_motion_handler([this]() { this->handle_motion_irq(); });
  this->configured_ = true;
  ESP_LOGI(TAG, "Motion IRQ route: BMI270 INT1 -> M5PM1 GPIO4 -> PY_IRQ (GPIO1)");
}

bool PaperMonoImuComponent::handle_motion_irq() {
  if (!this->configured_ || this->activity_ == nullptr) {
    return false;
  }

  uint16_t int_status = 0;
  if (!this->read_interrupt_status_(&int_status)) {
    ESP_LOGW(TAG, "BMI270 interrupt status read failed");
    return false;
  }

  if ((int_status & BMI270_ANY_MOTION_STATUS_MASK) == 0) {
    return false;
  }

  ESP_LOGD(TAG, "Motion detected: int_status=0x%04X", int_status);
  this->activity_->report_activity(papermono_activity::ActivitySource::MOTION);
  return true;
}

}  // namespace esphome::papermono_imu
