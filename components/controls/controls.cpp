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
                           sensor::Sensor *brightness,
                           sensor::Sensor *current_temperature, sensor::Sensor *min_temperature,
                           sensor::Sensor *max_temperature, sensor::Sensor *target_temperature,
                           sensor::Sensor *current_position) {
  if (index >= entries_.size()) return;
  entries_[index] = {entity_id, name, domain, state, friendly_name, modes, hs_color, brightness,
                     current_temperature, min_temperature, max_temperature, target_temperature,
                     current_position};
  count_ = std::max(count_, static_cast<size_t>(index + 1));
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

void Controls::dump_config() { ESP_LOGCONFIG(TAG, "Configured controls: %u/6", static_cast<unsigned>(count_)); }
void Controls::log_unsupported(uint8_t index, const char *entity_id) {
  ESP_LOGW(TAG, "Control %u unsupported domain: %s", static_cast<unsigned>(index + 1), entity_id);
}
const ControlEntry *Controls::entry_(size_t index) const { return index < count_ ? &entries_[index] : nullptr; }
const char *Controls::type_at(size_t index) const { auto *e = entry_(index); return e != nullptr ? e->domain.c_str() : "none"; }
const char *Controls::entity_id_at(size_t index) const { auto *e = entry_(index); return e != nullptr ? e->entity_id.c_str() : ""; }
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
bool Controls::number_valid(size_t index, const char *field) const {
  auto *e = entry_(index); if (e == nullptr) return false;
  const sensor::Sensor *s = nullptr;
  if (!strcmp(field, "brightness")) s = e->brightness;
  else if (!strcmp(field, "current_temperature")) s = e->current_temperature;
  else if (!strcmp(field, "min_temperature")) s = e->min_temperature;
  else if (!strcmp(field, "max_temperature")) s = e->max_temperature;
  else if (!strcmp(field, "target_temperature")) s = e->target_temperature;
  else if (!strcmp(field, "current_position")) s = e->current_position;
  return s != nullptr && s->has_state() && !std::isnan(s->state);
}
float Controls::number_at(size_t index, const char *field) const {
  auto *e = entry_(index); if (e == nullptr) return 0.0f;
  const sensor::Sensor *s = nullptr;
  if (!strcmp(field, "brightness")) s = e->brightness;
  else if (!strcmp(field, "current_temperature")) s = e->current_temperature;
  else if (!strcmp(field, "min_temperature")) s = e->min_temperature;
  else if (!strcmp(field, "max_temperature")) s = e->max_temperature;
  else if (!strcmp(field, "target_temperature")) s = e->target_temperature;
  else if (!strcmp(field, "current_position")) s = e->current_position;
  return s != nullptr ? s->state : 0.0f;
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
