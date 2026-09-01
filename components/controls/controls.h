#pragma once

#include <array>
#include <string>
#include <vector>

#include "esphome/components/globals/globals_component.h"
#include "esphome/components/papermono_epaper/papermono_epaper.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"

namespace esphome::controls {

constexpr uint32_t MEDIA_PLAYER_FEATURE_PAUSE = 1;
constexpr uint32_t MEDIA_PLAYER_FEATURE_VOLUME_SET = 4;
constexpr uint32_t MEDIA_PLAYER_FEATURE_PREVIOUS_TRACK = 16;
constexpr uint32_t MEDIA_PLAYER_FEATURE_NEXT_TRACK = 32;
constexpr uint32_t MEDIA_PLAYER_FEATURE_PLAY = 16384;

enum class LightEditMode : uint8_t {
  BRIGHTNESS = 0,
  RGB = 1,
  COLOR_TEMP = 2,
};

struct ControlEntry {
  std::string entity_id;
  std::string configured_name;
  std::string domain;
  text_sensor::TextSensor *state{nullptr};
  text_sensor::TextSensor *friendly_name{nullptr};
  text_sensor::TextSensor *modes{nullptr};
  text_sensor::TextSensor *hs_color{nullptr};
  sensor::Sensor *brightness{nullptr};
  sensor::Sensor *current_temperature{nullptr};
  sensor::Sensor *min_temperature{nullptr};
  sensor::Sensor *max_temperature{nullptr};
  sensor::Sensor *target_temperature{nullptr};
  sensor::Sensor *current_position{nullptr};
  sensor::Sensor *volume{nullptr};
  text_sensor::TextSensor *media_title{nullptr};
  sensor::Sensor *supported_features{nullptr};
  text_sensor::TextSensor *media_artist{nullptr};
  text_sensor::TextSensor *media_album_name{nullptr};
};

class Controls : public Component {
 public:
  void set_display(papermono_epaper::PaperMonoEpaper *display) { display_ = display; }
  void set_controls_view(globals::GlobalsComponent<bool> *view) { controls_view_ = view; }
  void set_ha_connection_state(globals::GlobalsComponent<int> *state) { ha_connection_state_ = state; }
  void add_control(uint8_t index, const char *entity_id, const char *name, const char *domain,
                   text_sensor::TextSensor *state, text_sensor::TextSensor *friendly_name,
                   text_sensor::TextSensor *modes, text_sensor::TextSensor *hs_color,
                   sensor::Sensor *brightness,
                   sensor::Sensor *current_temperature, sensor::Sensor *min_temperature,
                   sensor::Sensor *max_temperature, sensor::Sensor *target_temperature,
                   sensor::Sensor *current_position, sensor::Sensor *volume,
                   text_sensor::TextSensor *media_title, sensor::Sensor *supported_features,
                   text_sensor::TextSensor *media_artist, text_sensor::TextSensor *media_album_name);
  void setup() override;
  void loop() override;
  void dump_config() override;
  void log_unsupported(uint8_t index, const char *entity_id);

  size_t count() const { return count_; }
  const char *type_at(size_t index) const;
  const char *entity_id_at(size_t index) const;
  std::string resolved_name_at(size_t index) const;
  bool state_valid(size_t index) const;
  std::string state_at(size_t index) const;
  bool friendly_valid(size_t index) const;
  std::string friendly_at(size_t index) const;
  bool modes_valid(size_t index) const;
  std::string modes_at(size_t index) const;
  bool color_capable(size_t index) const;
  bool light_supports_rgb(size_t index) const;
  bool light_supports_color_temp(size_t index) const;
  bool light_supports_brightness(size_t index) const;
  LightEditMode light_edit_mode_at(size_t index) const {
    return index < count_ ? light_edit_mode_[index] : LightEditMode::BRIGHTNESS;
  }
  bool cycle_light_edit_mode(size_t index);
  bool color_valid(size_t index) const;
  float hue_at(size_t index) const;
  float saturation_at(size_t index) const;
  void set_color(size_t index, float hue, float saturation) {
    if (index >= count_) return;
    hue_[index] = hue;
    saturation_[index] = saturation;
    color_valid_[index] = true;
  }
  int color_step_at(size_t index) const { return index < count_ ? color_step_[index] : 0; }
  float chromatic_saturation_at(size_t index) const {
    return index < count_ && chromatic_saturation_[index] >= 10.0f ? chromatic_saturation_[index] : 100.0f;
  }
  void set_color_step(size_t index, int step, float hue, float saturation) {
    if (index >= count_) return;
    color_step_[index] = step;
    hue_[index] = hue;
    saturation_[index] = saturation;
    if (step != 2) chromatic_saturation_[index] = saturation >= 10.0f ? saturation : chromatic_saturation_at(index);
    color_valid_[index] = true;
  }
  void set_color_preview_started(size_t index, bool value) {
    if (index < count_) color_preview_started_[index] = value;
  }
  bool color_preview_started(size_t index) const {
    return index < count_ && color_preview_started_[index];
  }
  bool color_edit_mode_at(size_t index) const { return index < count_ && light_edit_mode_[index] == LightEditMode::RGB; }
  void set_color_edit_mode(size_t index, bool value) { if (index < count_) light_edit_mode_[index] = value ? LightEditMode::RGB : LightEditMode::BRIGHTNESS; }
  int active_slot() const { return active_slot_; }
  void set_active_slot(int slot) { active_slot_ = (slot >= 0 && static_cast<size_t>(slot) < count_) ? slot : -1; }
  bool number_valid(size_t index, const char *field) const;
  float number_at(size_t index, const char *field) const;
  std::string text_at(size_t index, const char *field) const;
  bool media_feature_supported(size_t index, uint32_t feature) const;
  bool media_volume_valid(size_t index) const;
  float media_volume_at(size_t index) const;
  void set_media_volume_optimistic(size_t index, float value);
  std::string last_active_mode_at(size_t index) const;

 protected:
  static bool valid_text_(const text_sensor::TextSensor *value);
  const ControlEntry *entry_(size_t index) const;
  void refresh_();
  void media_volume_confirmed_(size_t index, float value);
  std::vector<ControlEntry> entries_{};
  std::vector<std::string> last_active_modes_{};
  std::vector<LightEditMode> light_edit_mode_{};
  std::vector<float> hue_{};
  std::vector<float> saturation_{};
  std::vector<bool> color_valid_{};
  std::vector<bool> color_preview_started_{};
  std::vector<int> color_step_{};
  std::vector<float> chromatic_saturation_{};
  std::vector<float> optimistic_volume_{};
  std::vector<bool> optimistic_volume_valid_{};
  size_t count_{0};
  int active_slot_{-1};
  papermono_epaper::PaperMonoEpaper *display_{nullptr};
  globals::GlobalsComponent<bool> *controls_view_{nullptr};
  globals::GlobalsComponent<int> *ha_connection_state_{nullptr};
  bool refresh_pending_{false};
  uint32_t refresh_due_{0};
};

}  // namespace esphome::controls
