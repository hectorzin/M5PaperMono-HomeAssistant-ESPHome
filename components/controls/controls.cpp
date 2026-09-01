#include "controls.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cctype>

#include "esphome/core/log.h"

namespace esphome::controls {
static const char *const TAG = "controls";

void Controls::add_control(uint8_t index, const char *entity_id, const char *name, const char *domain,
                           text_sensor::TextSensor *state, text_sensor::TextSensor *friendly_name,
                           text_sensor::TextSensor *modes, text_sensor::TextSensor *hs_color,
                           sensor::Sensor *color_temperature, sensor::Sensor *min_color_temperature,
                           sensor::Sensor *max_color_temperature,
                           sensor::Sensor *brightness,
                           sensor::Sensor *current_temperature, sensor::Sensor *min_temperature,
                           sensor::Sensor *max_temperature, sensor::Sensor *target_temperature,
                           sensor::Sensor *current_position, sensor::Sensor *volume,
                           text_sensor::TextSensor *media_title, sensor::Sensor *supported_features,
                           text_sensor::TextSensor *media_artist, text_sensor::TextSensor *media_album_name,
                           const char *block_name, uint8_t block_index, uint16_t) {
  const size_t slot = static_cast<size_t>(index);
  if (slot >= entries_.size()) {
    entries_.resize(slot + 1);
    last_active_modes_.resize(slot + 1);
    light_edit_mode_.resize(slot + 1);
    hue_.resize(slot + 1);
    saturation_.resize(slot + 1);
    color_valid_.resize(slot + 1);
    color_preview_started_.resize(slot + 1);
    color_step_.resize(slot + 1);
    chromatic_saturation_.resize(slot + 1);
    optimistic_volume_.resize(slot + 1);
    optimistic_volume_valid_.resize(slot + 1);
    block_names_.resize(slot + 1);
    block_indices_.resize(slot + 1);
  }
  entries_[slot] = {entity_id, name, domain, state, friendly_name, modes, hs_color,
                    color_temperature, min_color_temperature, max_color_temperature, brightness,
                    current_temperature, min_temperature, max_temperature, target_temperature,
                    current_position, volume, media_title, supported_features, media_artist, media_album_name};
  block_names_[slot] = block_name != nullptr ? block_name : "Control";
  block_indices_[slot] = block_index;
  count_ = std::max(count_, slot + 1);
}

void Controls::setup() {
  for (size_t i = 0; i < count_; i++) {
    const auto &entry = entries_[i];
    if (entry.domain == "none")
      ESP_LOGW(TAG, "Control %u unsupported domain: %s", static_cast<unsigned>(i + 1), entry.entity_id.c_str());
    else
      ESP_LOGI(TAG, "Control %u: entity_id=%s type=%s", static_cast<unsigned>(i + 1), entry.entity_id.c_str(), entry.domain.c_str());
    ESP_LOGI(TAG, "Control[%u] subscriptions: state=%s friendly_name=%s", static_cast<unsigned>(i),
             entry.state != nullptr ? "yes" : "no", entry.friendly_name != nullptr ? "yes" : "no");
    if (entry.state != nullptr)
      entry.state->add_on_state_callback([this, i](const std::string &value) {
        if (value != "off" && value != "unknown" && value != "unavailable") last_active_modes_[i] = value;
        refresh_();
      });
    if (entry.friendly_name != nullptr)
      entry.friendly_name->add_on_state_callback([this](const std::string &) { refresh_(); });
    if (entry.modes != nullptr)
      entry.modes->add_on_state_callback([this](const std::string &) { refresh_(); });
    if (entry.hs_color != nullptr)
      entry.hs_color->add_on_state_callback([this, i](const std::string &value) {
        float parsed[2] = {0.0f, 0.0f};
        int found = 0;
        const char *cursor = value.c_str();
        while (*cursor != '\0' && found < 2) {
          char *end = nullptr;
          const float number = std::strtof(cursor, &end);
          if (end != cursor) {
            parsed[found++] = number;
            cursor = end;
          } else {
            cursor++;
          }
        }
        if (found == 2 && parsed[0] >= 0.0f && parsed[0] <= 360.0f &&
            parsed[1] >= 0.0f && parsed[1] <= 100.0f) {
          hue_[i] = parsed[0];
          saturation_[i] = parsed[1];
          if (parsed[1] < 10.0f) {
            color_step_[i] = 2;  // WHITE is a selector state, not a hue.
          } else {
            chromatic_saturation_[i] = parsed[1];
            color_step_[i] = (static_cast<int>(std::floor((parsed[0] + 30.0f) / 60.0f)) % 6 + 6) % 6;
            if (color_step_[i] >= 2) color_step_[i]++;
          }
          color_valid_[i] = true;
        } else {
          color_valid_[i] = false;
        }
        refresh_();
      });
    if (entry.color_temperature != nullptr) entry.color_temperature->add_on_state_callback([this](float) { refresh_(); });
    if (entry.min_color_temperature != nullptr) entry.min_color_temperature->add_on_state_callback([this](float) { refresh_(); });
    if (entry.max_color_temperature != nullptr) entry.max_color_temperature->add_on_state_callback([this](float) { refresh_(); });
    if (entry.brightness != nullptr)
      entry.brightness->add_on_state_callback([this](float) { refresh_(); });
    if (entry.current_temperature != nullptr)
      entry.current_temperature->add_on_state_callback([this](float) { refresh_(); });
    if (entry.min_temperature != nullptr)
      entry.min_temperature->add_on_state_callback([this](float) { refresh_(); });
    if (entry.max_temperature != nullptr)
      entry.max_temperature->add_on_state_callback([this](float) { refresh_(); });
    if (entry.target_temperature != nullptr)
      entry.target_temperature->add_on_state_callback([this](float) { refresh_(); });
    if (entry.current_position != nullptr)
      entry.current_position->add_on_state_callback([this](float) { refresh_(); });
    if (entry.volume != nullptr)
      entry.volume->add_on_state_callback([this, i](float value) { media_volume_confirmed_(i, value); });
    if (entry.media_title != nullptr)
      entry.media_title->add_on_state_callback([this](const std::string &) { refresh_(); });
    if (entry.supported_features != nullptr)
      entry.supported_features->add_on_state_callback([this](float) { refresh_(); });
    if (entry.media_artist != nullptr)
      entry.media_artist->add_on_state_callback([this](const std::string &) { refresh_(); });
    if (entry.media_album_name != nullptr)
      entry.media_album_name->add_on_state_callback([this](const std::string &) { refresh_(); });
  }
}

void Controls::refresh_() {
  if (display_ != nullptr && controls_view_ != nullptr && controls_view_->value() &&
      ha_connection_state_ != nullptr && ha_connection_state_->value() == 1) {
    refresh_pending_ = true;
    refresh_due_ = millis() + 120U;
  }
}

void Controls::loop() {
  if (refresh_pending_ && static_cast<int32_t>(millis() - refresh_due_) >= 0) {
    refresh_pending_ = false;
    if (display_ != nullptr && controls_view_ != nullptr && controls_view_->value() &&
        ha_connection_state_ != nullptr && ha_connection_state_->value() == 1) {
      display_->add_partial_region(0, 100, 480, 700);
      display_->request_refresh(papermono_epaper::RefreshPolicy::USER_INTERACTION,
                                papermono_epaper::RefreshKind::NORMAL, "controls_ha");
    }
  }
}

void Controls::dump_config() { ESP_LOGCONFIG(TAG, "Configured controls: %u", static_cast<unsigned>(count_)); }
void Controls::log_unsupported(uint8_t index, const char *entity_id) {
  ESP_LOGW(TAG, "Control %u unsupported domain: %s", static_cast<unsigned>(index + 1), entity_id);
}
const ControlEntry *Controls::entry_(size_t index) const { return index < count_ ? &entries_[index] : nullptr; }
const char *Controls::type_at(size_t index) const { auto *e = entry_(index); return e != nullptr ? e->domain.c_str() : "none"; }
const char *Controls::entity_id_at(size_t index) const { auto *e = entry_(index); return e != nullptr ? e->entity_id.c_str() : ""; }
const char *Controls::block_name_at_page(int page) const {
  const int index = control_index_at_page_slot(page, 0);
  if (index < 0) return "Control";
  return block_names_[static_cast<size_t>(index)].c_str();
}
int Controls::page_count() const {
  if (count_ == 0) return 1;
  int pages = 0;
  size_t start = 0;
  while (start < count_) {
    size_t end = start + 1;
    while (end < count_ && block_indices_[end] == block_indices_[start]) end++;
    pages += static_cast<int>((end - start + 5) / 6);
    start = end;
  }
  return pages;
}
int Controls::control_index_at_page_slot(int page, int visual_slot) const {
  if (page < 0 || visual_slot < 0 || visual_slot >= 6) return -1;
  int physical_page = 0;
  size_t start = 0;
  while (start < count_) {
    size_t end = start + 1;
    while (end < count_ && block_indices_[end] == block_indices_[start]) end++;
    const int block_pages = static_cast<int>((end - start + 5) / 6);
    if (page < physical_page + block_pages) {
      const size_t index = start + static_cast<size_t>(page - physical_page) * 6 + visual_slot;
      return index < end ? static_cast<int>(index) : -1;
    }
    physical_page += block_pages;
    start = end;
  }
  return -1;
}
int Controls::first_page_for_block(int block_index) const {
  int page = 0;
  size_t start = 0;
  while (start < count_) {
    size_t end = start + 1;
    while (end < count_ && block_indices_[end] == block_indices_[start]) end++;
    if (block_indices_[start] == block_index) return page;
    page += static_cast<int>((end - start + 5) / 6);
    start = end;
  }
  return -1;
}
bool Controls::valid_text_(const text_sensor::TextSensor *value) {
  return value != nullptr && value->has_state() && !value->state.empty() && value->state != "unknown" && value->state != "unavailable";
}
bool Controls::state_valid(size_t index) const { auto *e = entry_(index); return e != nullptr && valid_text_(e->state); }
std::string Controls::state_at(size_t index) const { auto *e = entry_(index); return e != nullptr && e->state != nullptr && e->state->has_state() ? e->state->state : ""; }
bool Controls::friendly_valid(size_t index) const { auto *e = entry_(index); return e != nullptr && valid_text_(e->friendly_name); }
std::string Controls::friendly_at(size_t index) const { auto *e = entry_(index); return e != nullptr && e->friendly_name != nullptr && e->friendly_name->has_state() ? e->friendly_name->state : ""; }
bool Controls::modes_valid(size_t index) const { auto *e = entry_(index); return e != nullptr && valid_text_(e->modes); }
std::string Controls::modes_at(size_t index) const { auto *e = entry_(index); return e != nullptr && e->modes != nullptr && e->modes->has_state() ? e->modes->state : ""; }
bool Controls::color_capable(size_t index) const {
  if (!modes_valid(index)) return false;
  std::string raw = modes_at(index);
  std::string token;
  for (size_t i = 0; i <= raw.size(); i++) {
    const char c = i < raw.size() ? raw[i] : '\0';
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
      token += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    } else if (!token.empty()) {
      if (token == "hs" || token == "rgb" || token == "rgbw" || token == "rgbww" || token == "xy") return true;
      token.clear();
    }
  }
  return false;
}
bool Controls::light_supports_rgb(size_t index) const {
  if (!modes_valid(index)) return false;
  std::string raw = modes_at(index);
  std::string token;
  for (size_t i = 0; i <= raw.size(); i++) {
    const char c = i < raw.size() ? raw[i] : '\0';
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
      token += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    } else if (!token.empty()) {
      if (token == "hs" || token == "rgb" || token == "rgbw" || token == "rgbww") return true;
      token.clear();
    }
  }
  return false;
}
bool Controls::light_supports_color_temp(size_t index) const {
  if (!modes_valid(index)) return false;
  std::string raw = modes_at(index);
  std::string token;
  for (size_t i = 0; i <= raw.size(); i++) {
    const char c = i < raw.size() ? raw[i] : '\0';
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
      token += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    } else if (!token.empty()) {
      if (token == "color_temp" || token == "color_temp_kelvin") return true;
      token.clear();
    }
  }
  return false;
}
bool Controls::cycle_light_edit_mode(size_t index) {
  if (index >= count_) return false;
  const bool has_rgb = light_supports_rgb(index);
  const bool has_color_temp = light_supports_color_temp(index);
  if (!has_rgb && !has_color_temp) return false;
  LightEditMode next = LightEditMode::BRIGHTNESS;
  switch (light_edit_mode_[index]) {
    case LightEditMode::BRIGHTNESS:
      next = has_rgb ? LightEditMode::RGB : LightEditMode::COLOR_TEMP;
      break;
    case LightEditMode::RGB:
      next = has_color_temp ? LightEditMode::COLOR_TEMP : LightEditMode::BRIGHTNESS;
      break;
    case LightEditMode::COLOR_TEMP:
      next = LightEditMode::BRIGHTNESS;
      break;
  }
  light_edit_mode_[index] = next;
  refresh_();
  return true;
}
bool Controls::light_supports_brightness(size_t index) const {
  if (!modes_valid(index)) return false;
  std::string raw = modes_at(index);
  std::string token;
  for (size_t i = 0; i <= raw.size(); i++) {
    const char c = i < raw.size() ? raw[i] : '\0';
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
      token += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    } else if (!token.empty()) {
      if (token == "brightness" || token == "color_temp" || token == "hs" || token == "xy" ||
          token == "rgb" || token == "rgbw" || token == "rgbww" || token == "white") return true;
      token.clear();
    }
  }
  return false;
}
bool Controls::color_valid(size_t index) const { return index < count_ && color_valid_[index]; }
float Controls::hue_at(size_t index) const { return index < count_ ? hue_[index] : 0.0f; }
float Controls::saturation_at(size_t index) const { return index < count_ ? saturation_[index] : 0.0f; }
float Controls::color_temperature_at(size_t index) const { auto *e = entry_(index); return e && e->color_temperature && e->color_temperature->has_state() ? e->color_temperature->state : 0.0f; }
float Controls::min_color_temperature_at(size_t index) const { auto *e = entry_(index); return e && e->min_color_temperature && e->min_color_temperature->has_state() ? e->min_color_temperature->state : 0.0f; }
float Controls::max_color_temperature_at(size_t index) const { auto *e = entry_(index); return e && e->max_color_temperature && e->max_color_temperature->has_state() ? e->max_color_temperature->state : 0.0f; }
bool Controls::number_valid(size_t index, const char *field) const {
  auto *e = entry_(index); if (e == nullptr) return false;
  const sensor::Sensor *s = nullptr;
  if (!strcmp(field, "brightness")) s = e->brightness;
  else if (!strcmp(field, "color_temperature")) s = e->color_temperature;
  else if (!strcmp(field, "min_color_temperature")) s = e->min_color_temperature;
  else if (!strcmp(field, "max_color_temperature")) s = e->max_color_temperature;
  else if (!strcmp(field, "current_temperature")) s = e->current_temperature;
  else if (!strcmp(field, "min_temperature")) s = e->min_temperature;
  else if (!strcmp(field, "max_temperature")) s = e->max_temperature;
  else if (!strcmp(field, "target_temperature")) s = e->target_temperature;
  else if (!strcmp(field, "current_position")) s = e->current_position;
  else if (!strcmp(field, "volume_level")) s = e->volume;
  else if (!strcmp(field, "supported_features")) s = e->supported_features;
  return s != nullptr && s->has_state() && !std::isnan(s->state);
}
float Controls::number_at(size_t index, const char *field) const {
  auto *e = entry_(index); if (e == nullptr) return 0.0f;
  const sensor::Sensor *s = nullptr;
  if (!strcmp(field, "brightness")) s = e->brightness;
  else if (!strcmp(field, "color_temperature")) s = e->color_temperature;
  else if (!strcmp(field, "min_color_temperature")) s = e->min_color_temperature;
  else if (!strcmp(field, "max_color_temperature")) s = e->max_color_temperature;
  else if (!strcmp(field, "current_temperature")) s = e->current_temperature;
  else if (!strcmp(field, "min_temperature")) s = e->min_temperature;
  else if (!strcmp(field, "max_temperature")) s = e->max_temperature;
  else if (!strcmp(field, "target_temperature")) s = e->target_temperature;
  else if (!strcmp(field, "current_position")) s = e->current_position;
  else if (!strcmp(field, "volume_level")) s = e->volume;
  else if (!strcmp(field, "supported_features")) s = e->supported_features;
  return s != nullptr ? s->state : 0.0f;
}
std::string Controls::text_at(size_t index, const char *field) const {
  auto *e = entry_(index); if (e == nullptr) return {};
  const auto *s = !strcmp(field, "media_title") ? e->media_title :
                  !strcmp(field, "media_artist") ? e->media_artist :
                  !strcmp(field, "media_album_name") ? e->media_album_name : nullptr;
  return s != nullptr && s->has_state() ? s->state : std::string{};
}
bool Controls::media_feature_supported(size_t index, uint32_t feature) const {
  if (!number_valid(index, "supported_features")) return false;
  const float raw = number_at(index, "supported_features");
  return raw >= 0.0f && (static_cast<uint32_t>(raw) & feature) != 0;
}
bool Controls::media_volume_valid(size_t index) const {
  if (index >= count_) return false;
  if (optimistic_volume_valid_[index]) return true;
  return number_valid(index, "volume_level");
}
float Controls::media_volume_at(size_t index) const {
  if (index >= count_) return 0.0f;
  if (optimistic_volume_valid_[index]) return optimistic_volume_[index];
  return number_at(index, "volume_level");
}
void Controls::set_media_volume_optimistic(size_t index, float value) {
  if (index >= count_) return;
  optimistic_volume_[index] = std::max(0.0f, std::min(1.0f, value));
  optimistic_volume_valid_[index] = true;
}
void Controls::media_volume_confirmed_(size_t index, float value) {
  if (index >= count_ || std::isnan(value)) return;
  if (optimistic_volume_valid_[index]) {
    if (std::fabs(value - optimistic_volume_[index]) <= 0.011f) {
      optimistic_volume_valid_[index] = false;
      ESP_LOGI(TAG, "Media player volume confirmed: slot=%u value=%.2f", static_cast<unsigned>(index), value);
    } else {
      refresh_();
      return;
    }
  }
  refresh_();
}
std::string Controls::resolved_name_at(size_t index) const {
  auto *e = entry_(index); if (e == nullptr) return {};
  if (!e->configured_name.empty()) return e->configured_name;
  if (friendly_valid(index)) return friendly_at(index);
  std::string fallback = e->entity_id;
  const auto dot = fallback.find('.');
  if (dot != std::string::npos) fallback.erase(0, dot + 1);
  for (char &c : fallback) if (c == '_') c = ' ';
  return fallback.empty() ? "Control" : fallback;
}
std::string Controls::last_active_mode_at(size_t index) const { return index < count_ ? last_active_modes_[index] : std::string{}; }
}  // namespace esphome::controls
