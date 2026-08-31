#pragma once

#include <array>
#include <string>

#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/globals/globals_component.h"
#include "esphome/components/papermono_epaper/papermono_epaper.h"
#include "esphome/core/component.h"

namespace esphome::controls {

struct ControlEntry {
  std::string entity_id;
  std::string configured_name;
  std::string domain;
  text_sensor::TextSensor *state{nullptr};
  text_sensor::TextSensor *friendly_name{nullptr};
  text_sensor::TextSensor *modes{nullptr};
  sensor::Sensor *brightness{nullptr};
  sensor::Sensor *current_temperature{nullptr};
  sensor::Sensor *min_temperature{nullptr};
  sensor::Sensor *max_temperature{nullptr};
  sensor::Sensor *target_temperature{nullptr};
};

class Controls : public Component {
 public:
  void set_display(papermono_epaper::PaperMonoEpaper *display) { this->display_ = display; }
  void set_controls_view(globals::GlobalsComponent<bool> *view) { this->controls_view_ = view; }
  void set_ha_connection_state(globals::GlobalsComponent<int> *state) { this->ha_connection_state_ = state; }
  void add_control(uint8_t index, const char *entity_id, const char *name, const char *domain,
                   text_sensor::TextSensor *state, text_sensor::TextSensor *friendly_name,
                   text_sensor::TextSensor *modes, sensor::Sensor *brightness,
                   sensor::Sensor *current_temperature, sensor::Sensor *min_temperature,
                   sensor::Sensor *max_temperature, sensor::Sensor *target_temperature);
  void setup() override;
  void loop() override;
  void dump_config() override;
  void log_unsupported(uint8_t index, const char *entity_id);

  size_t count() const { return this->count_; }
  const char *type_at(size_t index) const { return this->entries_[index].domain.c_str(); }
  const char *entity_id_at(size_t index) const { return this->entries_[index].entity_id.c_str(); }
  std::string resolved_name_at(size_t index) const;
  bool state_valid(size_t index) const;
  std::string state_at(size_t index) const;
  bool friendly_valid(size_t index) const;
  std::string friendly_at(size_t index) const;
  bool modes_valid(size_t index) const;
  std::string modes_at(size_t index) const;
  bool number_valid(size_t index, const char *field) const;
  float number_at(size_t index, const char *field) const;
  std::string last_active_mode_at(size_t index) const;

 protected:
  static bool valid_text_(const text_sensor::TextSensor *value);
  const ControlEntry *entry_(size_t index) const;
  void refresh_();
  void log_text_(size_t index, const char *field, const std::string &value);
  void log_number_(size_t index, const char *field, float value);
  std::array<ControlEntry, 6> entries_{};
  std::array<std::string, 6> last_active_modes_{};
  std::array<std::array<std::string, 3>, 6> last_text_values_{};
  std::array<std::array<float, 5>, 6> last_number_values_{};
  size_t count_{0};
  papermono_epaper::PaperMonoEpaper *display_{nullptr};
  globals::GlobalsComponent<bool> *controls_view_{nullptr};
  globals::GlobalsComponent<int> *ha_connection_state_{nullptr};
  bool refresh_pending_{false};
  uint32_t refresh_due_{0};
  bool loop_alive_logged_{false};
  bool api_connected_logged_{false};
  uint8_t subscription_audit_attempts_{0};
  uint32_t next_subscription_audit_ms_{0};
};

}  // namespace esphome::controls
