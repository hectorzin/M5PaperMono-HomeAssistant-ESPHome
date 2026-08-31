#include "controls.h"

#include <cmath>
#include <cstring>

#include "esphome/components/api/api_server.h"
#include "esphome/core/log.h"

namespace esphome::controls {
static const char *const TAG = "controls";

void Controls::add_control(uint8_t index, const char *entity_id, const char *name, const char *domain,
                           text_sensor::TextSensor *state, text_sensor::TextSensor *friendly_name,
                           text_sensor::TextSensor *modes, sensor::Sensor *brightness,
                           sensor::Sensor *current_temperature, sensor::Sensor *min_temperature,
                           sensor::Sensor *max_temperature, sensor::Sensor *target_temperature) {
  if (index >= this->entries_.size()) return;
  this->entries_[index] = {entity_id, name, domain, state, friendly_name, modes, brightness,
                           current_temperature, min_temperature, max_temperature, target_temperature};
  this->count_ = std::max(this->count_, static_cast<size_t>(index + 1));
}

void Controls::setup() {
  ESP_LOGI(TAG, "setup");
  for (size_t i = 0; i < this->count_; i++) {
    if (this->entries_[i].domain == "none")
      ESP_LOGW(TAG, "Control %u unsupported: entity_id=%s", static_cast<unsigned>(i + 1), this->entries_[i].entity_id.c_str());
    else
      ESP_LOGI(TAG, "Control %u: entity_id=%s type=%s", static_cast<unsigned>(i + 1), this->entries_[i].entity_id.c_str(), this->entries_[i].domain.c_str());
    ESP_LOGI(TAG, "Control[%u] subscriptions: state%s friendly_name%s%s", static_cast<unsigned>(i),
             this->entries_[i].state != nullptr ? "" : "=none",
             this->entries_[i].friendly_name != nullptr ? "" : "=none",
             this->entries_[i].domain == "climate" ? " climate={target_temperature,current_temperature,hvac_modes,min_temp,max_temp}" :
             this->entries_[i].domain == "light" ? " light={brightness,supported_color_modes}" : "");
    if (this->entries_[i].state != nullptr)
      this->entries_[i].state->add_on_state_callback([this, i](const std::string &value) {
        this->log_text_(i, "state", value);
        if (value != "off" && value != "unknown" && value != "unavailable") this->last_active_modes_[i] = value;
        if (!value.empty() && value != "unknown" && value != "unavailable") this->refresh_();
      });
    if (this->entries_[i].friendly_name != nullptr)
      this->entries_[i].friendly_name->add_on_state_callback([this, i](const std::string &value) { this->log_text_(i, "friendly_name", value); if (!value.empty() && value != "unknown" && value != "unavailable") this->refresh_(); });
    if (this->entries_[i].modes != nullptr)
      this->entries_[i].modes->add_on_state_callback([this, i](const std::string &value) {
        this->log_text_(i, this->entries_[i].domain == "climate" ? "hvac_modes" : "supported_color_modes", value);
        if (!value.empty() && value != "unknown" && value != "unavailable") this->refresh_();
      });
    if (this->entries_[i].brightness != nullptr)
      this->entries_[i].brightness->add_on_state_callback([this, i](float value) { this->log_number_(i, "brightness", value); if (!std::isnan(value)) this->refresh_(); });
    if (this->entries_[i].current_temperature != nullptr)
      this->entries_[i].current_temperature->add_on_state_callback([this, i](float value) { this->log_number_(i, "current_temperature", value); if (!std::isnan(value)) this->refresh_(); });
    if (this->entries_[i].min_temperature != nullptr)
      this->entries_[i].min_temperature->add_on_state_callback([this, i](float value) { this->log_number_(i, "min_temperature", value); if (!std::isnan(value)) this->refresh_(); });
    if (this->entries_[i].max_temperature != nullptr)
      this->entries_[i].max_temperature->add_on_state_callback([this, i](float value) { this->log_number_(i, "max_temperature", value); if (!std::isnan(value)) this->refresh_(); });
    if (this->entries_[i].target_temperature != nullptr)
      this->entries_[i].target_temperature->add_on_state_callback([this, i](float value) { this->log_number_(i, "target_temperature", value); if (!std::isnan(value)) this->refresh_(); });
  }
}

void Controls::refresh_() {
  if (this->display_ != nullptr && this->controls_view_ != nullptr && this->controls_view_->value() &&
      this->ha_connection_state_ != nullptr && this->ha_connection_state_->value() == 1) {
    this->refresh_pending_ = true;
    this->refresh_due_ = millis() + 120U;
  }
}

void Controls::loop() {
  const uint32_t now = millis();
  if (!this->loop_alive_logged_ && now >= 3000U) {
    ESP_LOGI(TAG, "loop alive");
    this->loop_alive_logged_ = true;
  }
  if (!this->api_connected_logged_ && api::global_api_server != nullptr && api::global_api_server->is_connected()) {
    ESP_LOGI(TAG, "API connected");
    this->api_connected_logged_ = true;
    this->next_subscription_audit_ms_ = now;
  }
  if (this->api_connected_logged_ && this->subscription_audit_attempts_ < 4 &&
      static_cast<int32_t>(now - this->next_subscription_audit_ms_) >= 0) {
#ifdef USE_API_HOMEASSISTANT_STATES
    const auto &subscriptions = api::global_api_server->get_state_subs();
    ESP_LOGI(TAG, "Runtime subscription audit: API connected, total=%u", static_cast<unsigned>(subscriptions.size()));
    for (size_t slot = 0; slot < this->count_; slot++) {
      const auto &entry = this->entries_[slot];
      for (const auto &subscription : subscriptions) {
        if (entry.entity_id != subscription.entity_id)
          continue;
        const char *attribute = subscription.attribute != nullptr ? subscription.attribute : "state";
        ESP_LOGI(TAG, "subscription active slot=%u attribute=%s entity=%s callback=%s",
                 static_cast<unsigned>(slot), attribute, subscription.entity_id,
                 subscription.callback ? "yes" : "no");
      }
    }
#endif
    this->subscription_audit_attempts_++;
    this->next_subscription_audit_ms_ = now + 1000U;
  }
  if (this->refresh_pending_ && static_cast<int32_t>(millis() - this->refresh_due_) >= 0) {
    this->refresh_pending_ = false;
    if (this->display_ != nullptr && this->controls_view_ != nullptr && this->controls_view_->value() &&
        this->ha_connection_state_ != nullptr && this->ha_connection_state_->value() == 1) {
      this->display_->add_partial_region(0, 100, 480, 700);
      this->display_->request_refresh(papermono_epaper::RefreshPolicy::USER_INTERACTION,
                                      papermono_epaper::RefreshKind::NORMAL, "controls_ha");
    }
  }
}

void Controls::log_text_(size_t index, const char *field, const std::string &value) {
  const size_t field_index = !strcmp(field, "state") ? 0 : !strcmp(field, "friendly_name") ? 1 : 2;
  if (this->last_text_values_[index][field_index] != value) {
    this->last_text_values_[index][field_index] = value;
    ESP_LOGI(TAG, "Control %u %s=%s", static_cast<unsigned>(index + 1), field, value.c_str());
  }
}

void Controls::log_number_(size_t index, const char *field, float value) {
  const size_t field_index = !strcmp(field, "brightness") ? 0 : !strcmp(field, "current_temperature") ? 1 :
      !strcmp(field, "min_temperature") ? 2 : !strcmp(field, "max_temperature") ? 3 : 4;
  if (std::isnan(this->last_number_values_[index][field_index]) || this->last_number_values_[index][field_index] != value) {
    this->last_number_values_[index][field_index] = value;
    ESP_LOGI(TAG, "Control %u %s=%.2f", static_cast<unsigned>(index + 1), field, value);
  }
}

void Controls::dump_config() { ESP_LOGCONFIG(TAG, "Configured controls: %u/6", static_cast<unsigned>(this->count_)); }
void Controls::log_unsupported(uint8_t index, const char *entity_id) { ESP_LOGW(TAG, "Control %u unsupported domain: %s", index + 1, entity_id); }
const ControlEntry *Controls::entry_(size_t index) const { return index < this->count_ ? &this->entries_[index] : nullptr; }
bool Controls::valid_text_(const text_sensor::TextSensor *value) {
  return value != nullptr && value->has_state() && !value->state.empty() &&
         value->state != "unknown" && value->state != "unavailable";
}
bool Controls::state_valid(size_t index) const { auto *e = entry_(index); return e != nullptr && valid_text_(e->state); }
std::string Controls::state_at(size_t index) const { auto *e = entry_(index); return e != nullptr && e->state != nullptr && e->state->has_state() ? e->state->state : ""; }
bool Controls::friendly_valid(size_t index) const { auto *e = entry_(index); return e != nullptr && valid_text_(e->friendly_name); }
std::string Controls::friendly_at(size_t index) const { auto *e = entry_(index); return e != nullptr && e->friendly_name != nullptr && e->friendly_name->has_state() ? e->friendly_name->state : ""; }
bool Controls::modes_valid(size_t index) const { auto *e = entry_(index); return e != nullptr && valid_text_(e->modes); }
std::string Controls::modes_at(size_t index) const { auto *e = entry_(index); return e != nullptr && e->modes != nullptr && e->modes->has_state() ? e->modes->state : ""; }
bool Controls::number_valid(size_t index, const char *field) const { auto *e = entry_(index); if (e == nullptr) return false; const sensor::Sensor *s = nullptr; if (!strcmp(field, "brightness")) s=e->brightness; else if (!strcmp(field,"current_temperature")) s=e->current_temperature; else if (!strcmp(field,"min_temperature")) s=e->min_temperature; else if (!strcmp(field,"max_temperature")) s=e->max_temperature; else if (!strcmp(field,"target_temperature")) s=e->target_temperature; return s != nullptr && s->has_state() && !std::isnan(s->state); }
float Controls::number_at(size_t index, const char *field) const { auto *e = entry_(index); if (e == nullptr) return 0; const sensor::Sensor *s = nullptr; if (!strcmp(field, "brightness")) s=e->brightness; else if (!strcmp(field,"current_temperature")) s=e->current_temperature; else if (!strcmp(field,"min_temperature")) s=e->min_temperature; else if (!strcmp(field,"max_temperature")) s=e->max_temperature; else if (!strcmp(field,"target_temperature")) s=e->target_temperature; return s != nullptr ? s->state : 0; }
std::string Controls::resolved_name_at(size_t index) const { auto *e = entry_(index); if (e == nullptr) return {}; if (!e->configured_name.empty()) return e->configured_name; if (friendly_valid(index)) return friendly_at(index); std::string value=e->entity_id; auto dot=value.find('.'); if(dot != std::string::npos) value.erase(0,dot+1); for(char &c:value) if(c=='_') c=' '; return value.empty() ? "Control" : value; }
std::string Controls::last_active_mode_at(size_t index) const { return index < this->count_ ? this->last_active_modes_[index] : std::string{}; }
}  // namespace esphome::controls
