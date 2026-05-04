#include "solarplug.h"

#include <cstdlib>
#include <cstring>

namespace esphome {
namespace solarplug {

namespace {

std::optional<float> parse_optional_float(const std::string &value) {
  char *end = nullptr;
  float parsed = std::strtof(value.c_str(), &end);
  if (end == value.c_str()) {
    return std::nullopt;
  }
  return parsed;
}

struct DecodedField {
  std::string name;
  std::string value;
  std::string unit;
  std::string confidence;
};

using FieldList = std::vector<DecodedField>;

void add_field(FieldList &fields, const char *name, const std::string &value, const char *unit, const char *confidence) {
  fields.push_back({name, value, unit ? unit : "", confidence ? confidence : ""});
}

bool is_token_space(char value) { return value == ' ' || value == '\t' || value == '\r' || value == '\n'; }

size_t find_token_start(const std::string &payload, size_t pos) {
  while (pos < payload.size() && is_token_space(payload[pos])) {
    pos++;
  }
  return pos;
}

size_t count_tokens_from(const std::string &payload, size_t pos) {
  size_t count = 0;
  while (pos < payload.size()) {
    pos = find_token_start(payload, pos);
    if (pos >= payload.size()) {
      break;
    }
    count++;
    while (pos < payload.size() && !is_token_space(payload[pos])) {
      pos++;
    }
  }
  return count;
}

std::vector<std::string> split_tokens(const std::string &payload) {
  std::vector<std::string> tokens;
  size_t pos = find_token_start(payload, 0);
  if (pos >= payload.size()) {
    return tokens;
  }
  if (payload[pos] == '(') {
    pos++;
  }
  pos = find_token_start(payload, pos);
  if (pos >= payload.size()) {
    return tokens;
  }
  tokens.reserve(count_tokens_from(payload, pos));
  while (pos < payload.size()) {
    size_t next = pos;
    while (next < payload.size() && !is_token_space(payload[next])) {
      next++;
    }
    if (next > pos) {
      tokens.push_back(payload.substr(pos, next - pos));
    }
    pos = find_token_start(payload, next);
    if (pos >= payload.size()) {
      break;
    }
  }
  return tokens;
}

std::string join_tokens(const std::vector<std::string> &tokens, size_t start) {
  if (start >= tokens.size()) {
    return "";
  }
  std::string out;
  for (size_t index = start; index < tokens.size(); index++) {
    if (!out.empty()) {
      out.push_back(' ');
    }
    out.append(tokens[index]);
  }
  return out;
}

void append_token_fields(FieldList &fields, const std::vector<std::string> &tokens, size_t start, const char *prefix,
                         const char *unit, const char *confidence) {
  if (start < tokens.size()) {
    fields.reserve(fields.size() + tokens.size() - start);
  }
  for (size_t index = start; index < tokens.size(); index++) {
    char name[32];
    std::snprintf(name, sizeof(name), "%s%02u", prefix, static_cast<unsigned>(index - start + 1));
    add_field(fields, name, tokens[index], unit, confidence);
  }
}

bool is_digits(const std::string &value) {
  for (char ch : value) {
    if (std::isdigit(static_cast<unsigned char>(ch)) == 0) {
      return false;
    }
  }
  return true;
}

int parse_digits(const std::string &value, size_t start, size_t len) {
  int parsed = 0;
  for (size_t index = 0; index < len; index++) {
    parsed = (parsed * 10) + (value[start + index] - '0');
  }
  return parsed;
}

std::string iso_from_yymmdd(const std::string &value) {
  if (value.size() != 6 || !is_digits(value)) {
    return value;
  }
  int year = 2000 + parse_digits(value, 0, 2);
  int month = parse_digits(value, 2, 2);
  int day = parse_digits(value, 4, 2);
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", year, month, day);
  return buffer;
}

std::string iso_from_yyyymmdd(const std::string &value) {
  if (value.size() != 8 || !is_digits(value)) {
    return value;
  }
  int year = parse_digits(value, 0, 4);
  int month = parse_digits(value, 4, 2);
  int day = parse_digits(value, 6, 2);
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", year, month, day);
  return buffer;
}

FieldList decode_generic_tokens(const std::vector<std::string> &tokens, const char *confidence) {
  FieldList fields;
  fields.reserve(tokens.size());
  append_token_fields(fields, tokens, 0, "token_", nullptr, confidence);
  return fields;
}

FieldList decode_hsts(const std::vector<std::string> &tokens) {
  FieldList fields;
  fields.reserve(5);
  if (!tokens.empty()) {
    add_field(fields, "status_code", tokens[0], nullptr, "high");
  }
  if (tokens.size() > 1) {
    const std::string &mode_and_status = tokens[1];
    if (!mode_and_status.empty()) {
      std::string mode_code = mode_and_status.substr(0, 1);
      add_field(fields, "mode_code", mode_code, nullptr, "high");
      if (mode_code == "L") {
        add_field(fields, "mode_label", "Mains Mode", nullptr, "high");
      } else if (mode_code == "B") {
        add_field(fields, "mode_label", "Battery Mode", nullptr, "medium");
      } else {
        add_field(fields, "mode_label", "unknown", nullptr, "medium");
      }
      if (mode_and_status.size() > 1) {
        add_field(fields, "status_bits", mode_and_status.substr(1), nullptr, "high");
      }
    }
  }
  if (tokens.size() > 2) {
    add_field(fields, "fault_bits", tokens[2], nullptr, "high");
  }
  return fields;
}

FieldList decode_hgrid(const std::vector<std::string> &tokens) {
  FieldList fields;
  fields.reserve(11);
  if (tokens.size() > 0)
    add_field(fields, "ac_input_voltage_v", tokens[0], "V", "high");
  if (tokens.size() > 1)
    add_field(fields, "mains_frequency_hz", tokens[1], "Hz", "high");
  if (tokens.size() > 2)
    add_field(fields, "high_mains_loss_voltage_v", tokens[2], "V", "high");
  if (tokens.size() > 3)
    add_field(fields, "low_mains_loss_voltage_v", tokens[3], "V", "high");
  if (tokens.size() > 4)
    add_field(fields, "high_mains_loss_frequency_hz", tokens[4], "Hz", "high");
  if (tokens.size() > 5)
    add_field(fields, "low_mains_loss_frequency_hz", tokens[5], "Hz", "high");
  if (tokens.size() > 6)
    add_field(fields, "mains_power_w", tokens[6], "W", "high");
  if (tokens.size() > 7) {
    add_field(fields, "mains_current_flow_direction_code", tokens[7], nullptr, "high");
    if (tokens[7] == "0") {
      add_field(fields, "mains_current_flow_direction", "Mains To Inverter", nullptr, "high");
    } else if (tokens[7] == "1") {
      add_field(fields, "mains_current_flow_direction", "Inverter To Mains", nullptr, "high");
    } else {
      add_field(fields, "mains_current_flow_direction", "unknown", nullptr, "medium");
    }
  }
  if (tokens.size() > 8)
    add_field(fields, "rated_power_w", tokens[8], "W", "high");
  if (tokens.size() > 9)
    add_field(fields, "raw_tail", join_tokens(tokens, 9), nullptr, "low");
  return fields;
}

FieldList decode_hop(const std::vector<std::string> &tokens) {
  FieldList fields;
  fields.reserve(9);
  if (tokens.size() > 0)
    add_field(fields, "ac_output_voltage_v", tokens[0], "V", "high");
  if (tokens.size() > 1)
    add_field(fields, "ac_output_frequency_hz", tokens[1], "Hz", "high");
  if (tokens.size() > 2)
    add_field(fields, "output_apparent_power_va", tokens[2], "VA", "high");
  if (tokens.size() > 3)
    add_field(fields, "output_active_power_w", tokens[3], "W", "high");
  if (tokens.size() > 4)
    add_field(fields, "output_load_percent", tokens[4], "%", "high");
  if (tokens.size() > 5)
    add_field(fields, "output_dc_component_status", tokens[5], nullptr, "high");
  if (tokens.size() > 6)
    add_field(fields, "rated_power_w", tokens[6], "W", "high");
  if (tokens.size() > 7)
    add_field(fields, "inductor_current_a", tokens[7], "A", "high");
  if (tokens.size() > 8)
    add_field(fields, "raw_tail", join_tokens(tokens, 8), nullptr, "low");
  return fields;
}

FieldList decode_hbat(const std::vector<std::string> &tokens) {
  FieldList fields;
  fields.reserve(8);
  if (!tokens.empty()) {
    add_field(fields, "battery_type_code", tokens[0], nullptr, "high");
    if (tokens[0] == "04") {
      add_field(fields, "battery_type", "PYL", nullptr, "high");
    } else {
      add_field(fields, "battery_type", tokens[0], nullptr, "medium");
    }
  }
  if (tokens.size() > 1)
    add_field(fields, "battery_voltage_v", tokens[1], "V", "high");
  if (tokens.size() > 2)
    add_field(fields, "battery_capacity_percent", tokens[2], "%", "high");
  if (tokens.size() > 3)
    add_field(fields, "battery_charging_current_a", tokens[3], "A", "high");
  if (tokens.size() > 4)
    add_field(fields, "battery_discharge_current_a", tokens[4], "A", "high");
  if (tokens.size() > 5)
    add_field(fields, "bus_voltage_v", tokens[5], "V", "high");
  if (tokens.size() > 6)
    add_field(fields, "raw_tail", join_tokens(tokens, 6), nullptr, "low");
  return fields;
}

FieldList decode_hpv(const std::vector<std::string> &tokens) {
  FieldList fields;
  fields.reserve(5);
  if (tokens.size() > 0)
    add_field(fields, "pv_voltage_v", tokens[0], "V", "high");
  if (tokens.size() > 1)
    add_field(fields, "pv_current_a", tokens[1], "A", "high");
  if (tokens.size() > 2)
    add_field(fields, "pv_power_w", tokens[2], "W", "high");
  if (tokens.size() > 3)
    add_field(fields, "generation_power_kw", tokens[3], "kW", "low");
  if (tokens.size() > 4)
    add_field(fields, "raw_tail", join_tokens(tokens, 4), nullptr, "low");
  return fields;
}

FieldList decode_htemp(const std::vector<std::string> &tokens) {
  FieldList fields;
  fields.reserve(9);
  if (tokens.size() > 0)
    add_field(fields, "inverter_temperature_c", tokens[0], "C", "high");
  if (tokens.size() > 1)
    add_field(fields, "boost_temperature_c", tokens[1], "C", "high");
  if (tokens.size() > 2)
    add_field(fields, "transformer_temperature_c", tokens[2], "C", "high");
  if (tokens.size() > 3)
    add_field(fields, "pv_temperature_c", tokens[3], "C", "high");
  if (tokens.size() > 4)
    add_field(fields, "fan_1_speed_percent", tokens[4], "%", "high");
  if (tokens.size() > 5)
    add_field(fields, "fan_2_speed_percent", tokens[5], "%", "high");
  if (tokens.size() > 6)
    add_field(fields, "max_temperature_c", tokens[6], "C", "high");
  if (tokens.size() > 7)
    add_field(fields, "temperature_status_bits", tokens[7], nullptr, "high");
  if (tokens.size() > 8)
    add_field(fields, "raw_tail", join_tokens(tokens, 8), nullptr, "low");
  return fields;
}

FieldList decode_hgen(const std::vector<std::string> &tokens) {
  FieldList fields;
  fields.reserve(8);
  if (tokens.size() > 0) {
    add_field(fields, "date_ymd", tokens[0], nullptr, "high");
    add_field(fields, "date_iso", iso_from_yymmdd(tokens[0]), nullptr, "high");
  }
  if (tokens.size() > 1)
    add_field(fields, "time_hm", tokens[1], nullptr, "high");
  if (tokens.size() > 2)
    add_field(fields, "daily_power_gen_kwh", tokens[2], "kWh", "high");
  if (tokens.size() > 3)
    add_field(fields, "monthly_electricity_generation_kwh", tokens[3], "kWh", "high");
  if (tokens.size() > 4)
    add_field(fields, "yearly_electricity_generation_kwh", tokens[4], "kWh", "high");
  if (tokens.size() > 5)
    add_field(fields, "total_power_generation_kwh", tokens[5], "kWh", "high");
  if (tokens.size() > 6)
    add_field(fields, "raw_tail", join_tokens(tokens, 6), nullptr, "low");
  return fields;
}

FieldList decode_qprtl(const std::vector<std::string> &tokens) {
  FieldList fields;
  fields.reserve(1);
  if (!tokens.empty()) {
    add_field(fields, "device_type", tokens[0], nullptr, "medium");
  }
  return fields;
}

FieldList decode_himsg1(const std::vector<std::string> &tokens) {
  FieldList fields;
  fields.reserve(5);
  if (tokens.size() > 0)
    add_field(fields, "software_version", tokens[0], nullptr, "high");
  if (tokens.size() > 1) {
    add_field(fields, "software_date", tokens[1], nullptr, "high");
    add_field(fields, "software_date_iso", iso_from_yyyymmdd(tokens[1]), nullptr, "high");
  }
  if (tokens.size() > 2)
    add_field(fields, "revision", tokens[2], nullptr, "high");
  if (tokens.size() > 3)
    add_field(fields, "raw_tail", join_tokens(tokens, 3), nullptr, "low");
  return fields;
}

FieldList decode_hbms1(const std::vector<std::string> &tokens) {
  FieldList fields;
  fields.reserve(9);
  if (tokens.size() > 0)
    add_field(fields, "bms_status_code", tokens[0], nullptr, "medium");
  if (tokens.size() > 1)
    add_field(fields, "bms_flags", tokens[1], nullptr, "medium");
  if (tokens.size() > 2)
    add_field(fields, "bms_discharge_voltage_limit_v", tokens[2], "V", "medium");
  if (tokens.size() > 3)
    add_field(fields, "bms_charge_voltage_limit_v", tokens[3], "V", "medium");
  if (tokens.size() > 4)
    add_field(fields, "bms_charge_current_limit_a", tokens[4], "A", "medium");
  if (tokens.size() > 5)
    add_field(fields, "bms_soc_percent", tokens[5], "%", "medium-high");
  if (tokens.size() > 6)
    add_field(fields, "bms_charging_current_a", tokens[6], "A", "medium-high");
  if (tokens.size() > 7)
    add_field(fields, "bms_discharge_current_a", tokens[7], "A", "medium-high");
  if (tokens.size() > 8)
    add_field(fields, "raw_tail", join_tokens(tokens, 8), nullptr, "low");
  return fields;
}

FieldList decode_hbms2(const std::vector<std::string> &tokens) {
  FieldList fields;
  fields.reserve(8);
  if (tokens.size() > 0)
    add_field(fields, "remaining_capacity", tokens[0], "Ah", "low");
  if (tokens.size() > 1)
    add_field(fields, "nominal_capacity", tokens[1], "Ah", "low");
  if (tokens.size() > 2)
    add_field(fields, "display_mode", tokens[2], nullptr, "medium");
  if (tokens.size() > 3)
    add_field(fields, "max_voltage", tokens[3], "mV", "medium");
  if (tokens.size() > 4)
    add_field(fields, "max_voltage_cell_position", tokens[4], "cell", "medium");
  if (tokens.size() > 5)
    add_field(fields, "min_voltage", tokens[5], "mV", "medium");
  if (tokens.size() > 6)
    add_field(fields, "min_voltage_cell_position", tokens[6], "cell", "medium");
  if (tokens.size() > 7)
    add_field(fields, "raw_tail", join_tokens(tokens, 7), nullptr, "low");
  return fields;
}

FieldList decode_hbms3(const std::vector<std::string> &tokens) {
  FieldList fields;
  size_t count = std::min<size_t>(16, tokens.size());
  fields.reserve(count + (tokens.size() > 16 ? 1 : 0));
  for (size_t index = 0; index < count; index++) {
    char name[32];
    std::snprintf(name, sizeof(name), "cell_%02u_mv", static_cast<unsigned>(index + 1));
    add_field(fields, name, tokens[index], "mV", "low");
  }
  if (tokens.size() > 16) {
    add_field(fields, "raw_tail", join_tokens(tokens, 16), nullptr, "low");
  }
  return fields;
}

FieldList decode_heep(const std::vector<std::string> &tokens) {
  if (tokens.empty()) {
    return {};
  }
  FieldList fields;
  fields.reserve(1 + tokens.size());
  fields.push_back({"raw_payload", join_tokens(tokens, 0), "", "low"});
  append_token_fields(fields, tokens, 0, "token_", nullptr, "low");
  return fields;
}

FieldList decode_hpvb(const std::vector<std::string> &tokens) {
  if (tokens.empty()) {
    return {};
  }
  FieldList fields;
  fields.reserve(8);
  fields.push_back({"raw_payload", join_tokens(tokens, 0), "", "low"});
  if (tokens.size() > 0)
    add_field(fields, "pv_voltage_v", tokens[0], "V", "medium");
  if (tokens.size() > 1)
    add_field(fields, "pv_current_a", tokens[1], "A", "medium");
  if (tokens.size() > 2)
    add_field(fields, "pv_power_w", tokens[2], "W", "medium");
  if (tokens.size() > 3) {
    add_field(fields, "pv_charging_mark_code", tokens[3], nullptr, "medium");
    add_field(fields, "pv_charging_mark", tokens[3] == "0" ? "Close" : (tokens[3] == "1" ? "Open" : "unknown"), nullptr,
              "medium");
  }
  if (tokens.size() > 4)
    add_field(fields, "bus_voltage_v", tokens[4], "V", "medium");
  if (tokens.size() > 5)
    add_field(fields, "raw_tail", join_tokens(tokens, 5), nullptr, "low");
  return fields;
}

FieldList decode_fields(const std::string &command, const std::string &payload) {
  std::vector<std::string> tokens = split_tokens(payload);
  if (command == "HSTS") return decode_hsts(tokens);
  if (command == "HGRID") return decode_hgrid(tokens);
  if (command == "HOP") return decode_hop(tokens);
  if (command == "HBAT") return decode_hbat(tokens);
  if (command == "HPV") return decode_hpv(tokens);
  if (command == "HTEMP") return decode_htemp(tokens);
  if (command == "HGEN") return decode_hgen(tokens);
  if (command == "QPRTL") return decode_qprtl(tokens);
  if (command == "HIMSG1") return decode_himsg1(tokens);
  if (command == "HBMS1") return decode_hbms1(tokens);
  if (command == "HBMS2") return decode_hbms2(tokens);
  if (command == "HBMS3") return decode_hbms3(tokens);
  if (command == "HEEP1" || command == "HEEP2") return decode_heep(tokens);
  if (command == "HPVB") return decode_hpvb(tokens);
  return decode_generic_tokens(tokens, "low");
}

}  // namespace

void SolarPlugComponent::setup() {
  this->boot_time_ms_ = millis();
  ESP_LOGI(TAG, "ready");
}

void SolarPlugComponent::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Solar Plug decoder\n"
                "  response_timeout: %u ms\n"
                "  passive_mode: %s\n"
                "  enable_writes: %s\n"
                "  raw_frame_logging: %s\n"
                "  decoded_field_logging: %s\n"
                "  poll_intervals: fast=%u ms energy=%u ms identity=%u ms boot_delay=%u ms\n"
                "  allowed_commands: HSTS HTEMP HBMS1 HBMS2 HBMS3 QPRTL HIMSG1 HGEN HBAT HOP HGRID HPV HEEP1 HEEP2 HPVB",
                static_cast<unsigned>(this->response_timeout_ms_), this->passive_mode_ ? "true" : "false",
                this->enable_writes_ ? "true" : "false", this->raw_frame_logging_ ? "true" : "false",
                this->decoded_field_logging_ ? "true" : "false",
                static_cast<unsigned>(this->fast_poll_interval_ms_), static_cast<unsigned>(this->energy_poll_interval_ms_),
                static_cast<unsigned>(this->identity_poll_interval_ms_), static_cast<unsigned>(this->boot_poll_delay_ms_));
}

void SolarPlugComponent::register_sensor(const std::string &command, const std::string &field, sensor::Sensor *sensor) {
  if (sensor == nullptr) {
    return;
  }
  this->sensors_.push_back({command, field, sensor});
}

void SolarPlugComponent::register_text_sensor(const std::string &command, const std::string &field,
                                       text_sensor::TextSensor *sensor) {
  if (sensor == nullptr) {
    return;
  }
  this->text_sensors_.push_back({command, field, sensor});
}

void SolarPlugComponent::register_write_switch(const std::string &key, SolarPlugWriteSwitch *sensor) {
  if (sensor == nullptr) {
    return;
  }
  this->write_switches_.push_back({key, sensor});
}

void SolarPlugComponent::register_write_number(const std::string &key, SolarPlugWriteNumber *sensor) {
  if (sensor == nullptr) {
    return;
  }
  this->write_numbers_.push_back({key, sensor});
}

void SolarPlugComponent::register_write_select(const std::string &key, SolarPlugWriteSelect *sensor) {
  if (sensor == nullptr) {
    return;
  }
  this->write_selects_.push_back({key, sensor});
}

bool SolarPlugComponent::is_allowed_command_(const std::string &command) {
  return command == "HSTS" || command == "HTEMP" || command == "HBMS1" || command == "HBMS2" ||
         command == "HBMS3" || command == "QPRTL" || command == "HIMSG1" || command == "HGEN" ||
         command == "HBAT" || command == "HOP" || command == "HGRID" || command == "HPV" ||
         command == "HEEP1" || command == "HEEP2" || command == "HPVB";
}

bool SolarPlugComponent::is_known_passive_label_(const std::string &command) {
  return is_allowed_command_(command) || command == "PCP00" || command == "PCP01" || command == "PCP02" || command == "PCP03" ||
         command == "POP00" || command == "POP01" || command == "POP02" || command == "PVENGUSE00" ||
         command == "PVENGUSE01" || command == "PVENGUSE02";
}

bool SolarPlugComponent::queue_pending_command_(const PendingCommand &command) {
  if (this->pending_command_count_ >= MAX_PENDING_COMMANDS) {
    return false;
  }
  this->pending_commands_[this->pending_command_tail_] = command;
  this->pending_command_tail_ = (this->pending_command_tail_ + 1) % MAX_PENDING_COMMANDS;
  this->pending_command_count_++;
  return true;
}

bool SolarPlugComponent::queue_pending_front_(const PendingCommand &command) {
  if (this->pending_command_count_ >= MAX_PENDING_COMMANDS) {
    return false;
  }
  this->pending_command_head_ = (this->pending_command_head_ + MAX_PENDING_COMMANDS - 1) % MAX_PENDING_COMMANDS;
  this->pending_commands_[this->pending_command_head_] = command;
  this->pending_command_count_++;
  return true;
}

bool SolarPlugComponent::pop_pending_command_(PendingCommand *command) {
  if (command == nullptr || this->pending_command_count_ == 0) {
    return false;
  }
  *command = std::move(this->pending_commands_[this->pending_command_head_]);
  this->pending_commands_[this->pending_command_head_] = PendingCommand{};
  this->pending_command_head_ = (this->pending_command_head_ + 1) % MAX_PENDING_COMMANDS;
  this->pending_command_count_--;
  return true;
}

bool SolarPlugComponent::pending_command_exists_(const std::string &command) const {
  for (size_t i = 0; i < this->pending_command_count_; i++) {
    const size_t index = (this->pending_command_head_ + i) % MAX_PENDING_COMMANDS;
    if (this->pending_commands_[index].command == command) {
      return true;
    }
  }
  return false;
}

void SolarPlugComponent::clear_response_frame_() { this->response_frame_len_ = 0; }

bool SolarPlugComponent::append_response_byte_(uint8_t byte) {
  if (this->response_frame_len_ >= this->response_frame_.size()) {
    return false;
  }
  this->response_frame_[this->response_frame_len_++] = byte;
  return true;
}

bool SolarPlugComponent::send_command(const std::string &command) {
  if (this->passive_mode_) {
    ESP_LOGW(TAG, "passive mode enabled; ignoring command=%s", command.c_str());
    return false;
  }
  if (!is_allowed_command_(command)) {
    ESP_LOGW(TAG, "rejecting non-read-only command=%s", command.c_str());
    return false;
  }
  if (command == this->active_command_ || this->pending_command_exists_(command)) {
    ESP_LOGD(TAG, "command already queued or active; command=%s", command.c_str());
    return true;
  }
  if (!this->queue_pending_command_({command, FrameStyle::ASCII_CR, 0, true})) {
    ESP_LOGW(TAG, "command queue full; dropping command=%s", command.c_str());
    return false;
  }
  return true;
}

bool SolarPlugComponent::queue_write_frame_(const std::string &label, const std::string &frame, FrameStyle frame_style) {
  if (!this->enable_writes_) {
    ESP_LOGW(TAG, "writes disabled; ignoring write=%s", label.c_str());
    return false;
  }
  if (this->passive_mode_) {
    ESP_LOGW(TAG, "passive mode enabled; ignoring write=%s", label.c_str());
    return false;
  }
  if (!this->queue_pending_command_({frame, frame_style, 0, false})) {
    ESP_LOGW(TAG, "command queue full; dropping write=%s", label.c_str());
    return false;
  }
  ESP_LOGW(TAG, "queueing write label=%s frame=%s frame_style=%s", label.c_str(), frame.c_str(),
           frame_style == FrameStyle::CRC_XMODEM_CR ? "crc_xmodem_cr" : "ascii_cr");
  this->publish_write_status_("pending " + frame);
  return true;
}

bool SolarPlugComponent::send_write_switch(const std::string &key, bool state) {
  if (key == "buzzer")
    return this->queue_write_frame_("Buzzer On", state ? "PEa" : "PDa", FrameStyle::CRC_XMODEM_CR);
  if (key == "backlight")
    return this->queue_write_frame_("Backlight On", state ? "PExSh" : "PDx`Y", FrameStyle::ASCII_CR);
  if (key == "display_return_homepage")
    return this->queue_write_frame_("Display Automatically Returns To Homepage", state ? "PEkq:" : "PDk",
                                    state ? FrameStyle::ASCII_CR : FrameStyle::CRC_XMODEM_CR);
  if (key == "over_temperature_restart")
    return this->queue_write_frame_("Over Temperature Automatic Restart", state ? "PEv" : "PDv",
                                    FrameStyle::CRC_XMODEM_CR);
  if (key == "overload_restart")
    return this->queue_write_frame_("Overload Automatic Restart", state ? "PEu" : "PDu", FrameStyle::CRC_XMODEM_CR);
  if (key == "input_source_detection_prompt_sound")
    return this->queue_write_frame_("Input Source Detection Prompt Sound", state ? "PEyCI" : "PDypx",
                                    FrameStyle::ASCII_CR);
  if (key == "battery_equalization_mode_enable")
    return this->queue_write_frame_("Battery Equalization Mode Enable Setting", state ? "PBEQE1" : "PBEQE0Z2",
                                    state ? FrameStyle::CRC_XMODEM_CR : FrameStyle::ASCII_CR);
  if (key == "bms_function_enable")
    return this->queue_write_frame_("BMS Function Enable Setting", state ? "BMSC01" : "BMSC00",
                                    FrameStyle::CRC_XMODEM_CR);
  ESP_LOGW(TAG, "unknown write switch key=%s", key.c_str());
  return false;
}

bool SolarPlugComponent::send_write_select(const std::string &key, const std::string &value) {
  if (key == "charger_priority") {
    if (value == "CSO") {
      return this->queue_write_frame_("Charger Priority Setting", "PCP00", FrameStyle::CRC_XMODEM_CR);
    }
    if (value == "SNU") {
      return this->queue_write_frame_("Charger Priority Setting", "PCP01", FrameStyle::CRC_XMODEM_CR);
    }
    if (value == "OSO") {
      return this->queue_write_frame_("Charger Priority Setting", "PCP02", FrameStyle::CRC_XMODEM_CR);
    }
  }
  if (key == "output_source_priority") {
    if (value == "SUB" || value == "SUB priority") {
      return this->queue_write_frame_("Output Source Priority Setting", "POP00", FrameStyle::CRC_XMODEM_CR);
    }
    if (value == "SBU" || value == "SBU priority") {
      return this->queue_write_frame_("Output Source Priority Setting", "POP01", FrameStyle::CRC_XMODEM_CR);
    }
    if (value == "Utility first" || value == "Utility first (legacy)") {
      return this->queue_write_frame_("Output Source Priority Setting", "POP02", FrameStyle::CRC_XMODEM_CR);
    }
  }
  if (key == "pv_energy_feeding_priority") {
    if (value == "BLU") {
      return this->queue_write_frame_("PV Energy Feeding Priority Setting", "PVENGUSE00", FrameStyle::CRC_XMODEM_CR);
    }
    if (value == "LBU") {
      return this->queue_write_frame_("PV Energy Feeding Priority Setting", "PVENGUSE01", FrameStyle::CRC_XMODEM_CR);
    }
  }
  if (key == "battery_type") {
    if (value == "LIA") {
      return this->queue_write_frame_("Battery Type Setting", "PBT03", FrameStyle::CRC_XMODEM_CR);
    }
    if (value == "PYL") {
      return this->queue_write_frame_("Battery Type Setting", "PBT04", FrameStyle::CRC_XMODEM_CR);
    }
  }
  ESP_LOGW(TAG, "unknown write select key=%s value=%s", key.c_str(), value.c_str());
  return false;
}

bool SolarPlugComponent::send_write_number(const std::string &key, float value) {
  char frame[24];
  if (key == "battery_equalization_voltage") {
    if (value < 48.0f || value > 60.0f) {
      ESP_LOGW(TAG, "battery equalization voltage out of range: %.2f", value);
      return false;
    }
    std::snprintf(frame, sizeof(frame), "PBEQV%05.2f", value);
    return this->queue_write_frame_("Battery Equalization Voltage Setting", frame, FrameStyle::CRC_XMODEM_CR);
  }
  if (key == "battery_equalization_interval") {
    if (value < 0.0f || value > 90.0f) {
      ESP_LOGW(TAG, "battery equalization interval out of range: %.0f", value);
      return false;
    }
    std::snprintf(frame, sizeof(frame), "PBEQP%03u", static_cast<unsigned>(value + 0.5f));
    return this->queue_write_frame_("Battery Equalization Interval Setting", frame, FrameStyle::CRC_XMODEM_CR);
  }
  if (key == "battery_equalization_timeout") {
    unsigned rounded = static_cast<unsigned>(value + 0.5f);
    if (value < 5.0f || value > 900.0f || rounded % 5 != 0) {
      ESP_LOGW(TAG, "battery equalization timeout out of range: %.0f", value);
      return false;
    }
    std::snprintf(frame, sizeof(frame), "PBEQOT%03u", rounded);
    return this->queue_write_frame_("Battery Equalization Timeout Setting", frame, FrameStyle::CRC_XMODEM_CR);
  }
  if (key == "battery_equalization_time") {
    unsigned rounded = static_cast<unsigned>(value + 0.5f);
    if (value < 5.0f || value > 900.0f || rounded % 5 != 0) {
      ESP_LOGW(TAG, "battery equalization time out of range: %.0f", value);
      return false;
    }
    std::snprintf(frame, sizeof(frame), "PBEQT%03u", rounded);
    return this->queue_write_frame_("Battery Equalization Time Setting", frame, FrameStyle::CRC_XMODEM_CR);
  }
  if (key == "battery_cut_off_voltage") {
    if (value < 40.0f || value > 60.0f) {
      ESP_LOGW(TAG, "battery cut off voltage out of range: %.1f", value);
      return false;
    }
    std::snprintf(frame, sizeof(frame), "PSDV%.1f", value);
    return this->queue_write_frame_("Battery Cut Off Voltage Setting", frame, FrameStyle::CRC_XMODEM_CR);
  }
  if (key == "battery_bulk_voltage") {
    if (value < 48.0f || value > 58.4f) {
      ESP_LOGW(TAG, "battery bulk voltage out of range: %.1f", value);
      return false;
    }
    std::snprintf(frame, sizeof(frame), "PCVV%02.1f", value);
    return this->queue_write_frame_("Battery Constant Charging Voltage Setting", frame, FrameStyle::CRC_XMODEM_CR);
  }
  if (key == "battery_recharge_voltage") {
    if (value < 44.0f || value > 51.0f) {
      ESP_LOGW(TAG, "battery recharge voltage out of range: %.1f", value);
      return false;
    }
    std::snprintf(frame, sizeof(frame), "PBCV%02.1f", value);
    return this->queue_write_frame_("Battery Recharge Voltage Setting", frame, FrameStyle::CRC_XMODEM_CR);
  }
  if (key == "battery_redischarge_voltage") {
    if (value < 0.0f || value > 58.0f) {
      ESP_LOGW(TAG, "battery redischarge voltage out of range: %.1f", value);
      return false;
    }
    std::snprintf(frame, sizeof(frame), "PBDV%02.1f", value);
    return this->queue_write_frame_("Battery Redischarge Voltage Setting", frame, FrameStyle::CRC_XMODEM_CR);
  }
  if (key == "battery_float_voltage") {
    if (value < 48.0f || value > 58.4f) {
      ESP_LOGW(TAG, "battery float voltage out of range: %.1f", value);
      return false;
    }
    std::snprintf(frame, sizeof(frame), "PBFT%02.1f", value);
    return this->queue_write_frame_("Battery Float Charging Voltage Setting", frame, FrameStyle::CRC_XMODEM_CR);
  }
  if (key == "restore_second_output_battery_capacity") {
    if (value < 0.0f || value > 50.0f) {
      ESP_LOGW(TAG, "restore second output battery capacity out of range: %.0f", value);
      return false;
    }
    std::snprintf(frame, sizeof(frame), "PDSRS%03u", static_cast<unsigned>(value + 0.5f));
    return this->queue_write_frame_("Restore Second Output Battery Capacity Setting", frame, FrameStyle::CRC_XMODEM_CR);
  }
  if (key == "restore_second_output_delay_time") {
    unsigned rounded = static_cast<unsigned>(value + 0.5f);
    if (value < 0.0f || value > 60.0f || rounded % 5 != 0) {
      ESP_LOGW(TAG, "restore second output delay time out of range: %.0f", value);
      return false;
    }
    std::snprintf(frame, sizeof(frame), "PDDLYT%03u", rounded);
    return this->queue_write_frame_("Restore Second Output Delay Time Setting", frame, FrameStyle::CRC_XMODEM_CR);
  }
  if (key == "bms_lock_machine_battery_capacity") {
    if (value < 0.0f || value > 20.0f) {
      ESP_LOGW(TAG, "BMS lock machine battery capacity out of range: %.0f", value);
      return false;
    }
    std::snprintf(frame, sizeof(frame), "BMSSDC%03u", static_cast<unsigned>(value + 0.5f));
    return this->queue_write_frame_("BMS Lock Machine Battery Capacity Setting", frame, FrameStyle::CRC_XMODEM_CR);
  }
  if (key == "grid_connected_current") {
    if (value < 0.0f || value > 150.0f) {
      ESP_LOGW(TAG, "grid connected current out of range: %.0f", value);
      return false;
    }
    std::snprintf(frame, sizeof(frame), "PGFC%03u", static_cast<unsigned>(value + 0.5f));
    return this->queue_write_frame_("Grid Connected Current Setting", frame, FrameStyle::CRC_XMODEM_CR);
  }
  if (key == "maximum_mains_charging_current") {
    if (value < 0.0f || value > 60.0f) {
      ESP_LOGW(TAG, "maximum mains charging current out of range: %.0f", value);
      return false;
    }
    std::snprintf(frame, sizeof(frame), "MUCHGC%03u", static_cast<unsigned>(value + 0.5f));
    return this->queue_write_frame_("Maximum Mains Charging Current Setting", frame, FrameStyle::CRC_XMODEM_CR);
  }
  if (key == "maximum_charging_current") {
    if (value < 10.0f || value > 120.0f) {
      ESP_LOGW(TAG, "maximum charging current out of range: %.0f", value);
      return false;
    }
    std::snprintf(frame, sizeof(frame), "MNCHGC%03u", static_cast<unsigned>(value + 0.5f));
    return this->queue_write_frame_("Maximum Charging Current Setting", frame, FrameStyle::CRC_XMODEM_CR);
  }
  ESP_LOGW(TAG, "unknown write number key=%s", key.c_str());
  return false;
}

void SolarPlugComponent::queue_commands_(const char *const *commands, size_t count) {
  for (size_t index = 0; index < count; index++) {
    this->send_command(commands[index]);
  }
}

void SolarPlugComponent::queue_command_group_(const std::string &group) {
  static const char *const fast_commands[] = {"HSTS", "HGRID", "HOP", "HBAT", "HPV", "HTEMP"};
  static const char *const energy_commands[] = {"HGEN", "HBMS1", "HBMS2", "HBMS3"};
  static const char *const identity_commands[] = {"HIMSG1", "QPRTL", "HEEP1", "HEEP2", "HPVB"};

  if (group == "fast") {
    this->queue_commands_(fast_commands, sizeof(fast_commands) / sizeof(fast_commands[0]));
  } else if (group == "energy") {
    this->queue_commands_(energy_commands, sizeof(energy_commands) / sizeof(energy_commands[0]));
  } else if (group == "identity") {
    this->queue_commands_(identity_commands, sizeof(identity_commands) / sizeof(identity_commands[0]));
  } else if (group == "all") {
    this->queue_command_group_("identity");
    this->queue_command_group_("fast");
    this->queue_command_group_("energy");
  } else if (is_allowed_command_(group)) {
    this->send_command(group);
  } else {
    ESP_LOGW(TAG, "unknown read-only command group=%s", group.c_str());
  }
}

void SolarPlugComponent::send_command_group(const std::string &group) { this->queue_command_group_(group); }

void SolarPlugComponent::poll_due_groups_() {
  if (this->passive_mode_) {
    return;
  }
  const uint32_t now = millis();
  if (!this->boot_poll_sent_ && now - this->boot_time_ms_ >= this->boot_poll_delay_ms_) {
    this->queue_command_group_("all");
    this->boot_poll_sent_ = true;
    this->last_fast_poll_ms_ = now;
    this->last_energy_poll_ms_ = now;
    this->last_identity_poll_ms_ = now;
    return;
  }
  if (this->boot_poll_sent_ && this->fast_poll_interval_ms_ > 0 &&
      now - this->last_fast_poll_ms_ >= this->fast_poll_interval_ms_) {
    this->queue_command_group_("fast");
    this->last_fast_poll_ms_ = now;
  }
  if (this->boot_poll_sent_ && this->energy_poll_interval_ms_ > 0 &&
      now - this->last_energy_poll_ms_ >= this->energy_poll_interval_ms_) {
    this->queue_command_group_("energy");
    this->last_energy_poll_ms_ = now;
  }
  if (this->boot_poll_sent_ && this->identity_poll_interval_ms_ > 0 &&
      now - this->last_identity_poll_ms_ >= this->identity_poll_interval_ms_) {
    this->queue_command_group_("identity");
    this->last_identity_poll_ms_ = now;
  }
}

void SolarPlugComponent::publish_numeric_(const std::string &command, const std::string &field_name,
                                   const std::string &value) {
  auto parsed = parse_optional_float(value);
  if (!parsed.has_value()) {
    return;
  }
  for (const auto &binding : this->sensors_) {
    if (binding.command == command && binding.field == field_name && binding.sensor != nullptr) {
      binding.sensor->publish_state(*parsed);
    }
  }
}

void SolarPlugComponent::publish_text_(const std::string &command, const std::string &field_name, const std::string &value) {
  for (const auto &binding : this->text_sensors_) {
    if (binding.command == command && binding.field == field_name && binding.sensor != nullptr) {
      binding.sensor->publish_state(value);
    }
  }
}

void SolarPlugComponent::publish_write_status_(const std::string &status) {
  this->publish_text_("_WRITE", "last_write_status", status);
}

void SolarPlugComponent::publish_write_switch_state_(const std::string &key, bool state) {
  for (const auto &binding : this->write_switches_) {
    if (binding.key == key && binding.sensor != nullptr) {
      binding.sensor->publish_state(state);
    }
  }
}

void SolarPlugComponent::publish_write_number_state_(const std::string &key, float value) {
  for (const auto &binding : this->write_numbers_) {
    if (binding.key == key && binding.sensor != nullptr) {
      binding.sensor->publish_state(value);
    }
  }
}

void SolarPlugComponent::publish_write_select_state_(const std::string &key, const std::string &value) {
  for (const auto &binding : this->write_selects_) {
    if (binding.key == key && binding.sensor != nullptr) {
      binding.sensor->publish_state(value);
    }
  }
}

void SolarPlugComponent::publish_write_states_from_hbat_() {
  if (!this->latest_.hbat_battery_type.empty()) {
    this->publish_write_select_state_("battery_type", this->latest_.hbat_battery_type);
  }
}

void SolarPlugComponent::publish_write_states_from_heep1_(const std::string &payload) {
  const std::vector<std::string> tokens = split_tokens(payload);
  if (tokens.size() > 4) {
    if (tokens[4] == "012") {
      this->publish_write_switch_state_("display_return_homepage", true);
    } else if (tokens[4] == "002") {
      this->publish_write_switch_state_("display_return_homepage", false);
    }
  }
  if (tokens.size() > 1) {
    auto max_charging_current = parse_optional_float(tokens[1]);
    if (max_charging_current.has_value()) {
      this->publish_write_number_state_("maximum_charging_current", *max_charging_current);
    }
  }
  if (tokens.size() > 2) {
    auto maximum_mains_charging_current = parse_optional_float(tokens[2]);
    if (maximum_mains_charging_current.has_value()) {
      this->publish_write_number_state_("maximum_mains_charging_current", *maximum_mains_charging_current);
    }
  }
  if (tokens.size() > 10) {
    auto bms_lock_capacity = parse_optional_float(tokens[10]);
    if (bms_lock_capacity.has_value()) {
      this->publish_write_number_state_("bms_lock_machine_battery_capacity", *bms_lock_capacity);
    }
  }
  if (tokens.size() > 17) {
    auto grid_connected_current = parse_optional_float(tokens[17]);
    if (grid_connected_current.has_value()) {
      this->publish_write_number_state_("grid_connected_current", *grid_connected_current);
    }
  }
}

void SolarPlugComponent::publish_write_states_from_heep2_(const std::string &payload) {
  const std::vector<std::string> tokens = split_tokens(payload);
  if (tokens.size() > 4) {
    auto recharge_voltage = parse_optional_float(tokens[4]);
    if (recharge_voltage.has_value()) {
      this->publish_write_number_state_("battery_recharge_voltage", *recharge_voltage);
    }
  }
  if (tokens.size() > 5) {
    auto redischarge_voltage = parse_optional_float(tokens[5]);
    if (redischarge_voltage.has_value()) {
      this->publish_write_number_state_("battery_redischarge_voltage", *redischarge_voltage);
    }
  }
  if (tokens.size() > 7) {
    auto equalization_voltage = parse_optional_float(tokens[7]);
    if (equalization_voltage.has_value()) {
      this->publish_write_number_state_("battery_equalization_voltage", *equalization_voltage);
    }
  }
  if (tokens.size() > 8) {
    auto equalization_time = parse_optional_float(tokens[8]);
    if (equalization_time.has_value()) {
      this->publish_write_number_state_("battery_equalization_time", *equalization_time);
    }
  }
  if (tokens.size() > 9) {
    auto equalization_timeout = parse_optional_float(tokens[9]);
    if (equalization_timeout.has_value()) {
      this->publish_write_number_state_("battery_equalization_timeout", *equalization_timeout);
    }
  }
  if (tokens.size() > 10) {
    auto equalization_interval = parse_optional_float(tokens[10]);
    if (equalization_interval.has_value()) {
      this->publish_write_number_state_("battery_equalization_interval", *equalization_interval);
    }
  }
  if (tokens.size() > 13) {
    auto restore_delay_time = parse_optional_float(tokens[13]);
    if (restore_delay_time.has_value()) {
      this->publish_write_number_state_("restore_second_output_delay_time", *restore_delay_time);
    }
  }
}

void SolarPlugComponent::empty_uart_buffer_() {
  uint8_t buf[64];
  while (this->available() > 0) {
    if (!this->read_array(buf, std::min<size_t>(sizeof(buf), this->available()))) {
      break;
    }
  }
}

std::pair<uint8_t, uint8_t> SolarPlugComponent::adjust_crc_bytes_(uint16_t crc) const {
  uint8_t high = static_cast<uint8_t>(crc >> 8);
  uint8_t low = static_cast<uint8_t>(crc & 0xFF);
  if (low == 0x28 || low == 0x0D || low == 0x0A) {
    low++;
  }
  if (high == 0x28 || high == 0x0D || high == 0x0A) {
    high++;
  }
  return {high, low};
}

uint16_t SolarPlugComponent::crc16_xmodem_(const uint8_t *data, size_t len) const {
  uint16_t crc = 0;
  for (size_t i = 0; i < len; i++) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (int bit = 0; bit < 8; bit++) {
      if (crc & 0x8000) {
        crc = static_cast<uint16_t>((crc << 1) ^ 0x1021);
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}

size_t SolarPlugComponent::build_frame_(const std::string &command, FrameStyle frame_style, uint8_t *frame,
                                        size_t frame_size) const {
  if (frame == nullptr) {
    return 0;
  }
  const size_t required_size = command.size() + (frame_style == FrameStyle::CRC_XMODEM_CR ? 3 : 1);
  if (required_size > frame_size) {
    return 0;
  }

  size_t frame_len = 0;
  for (char c : command) {
    frame[frame_len++] = static_cast<uint8_t>(c);
  }
  if (frame_style == FrameStyle::CRC_XMODEM_CR) {
    uint16_t crc = this->crc16_xmodem_(frame, frame_len);
    auto [high, low] = this->adjust_crc_bytes_(crc);
    frame[frame_len++] = high;
    frame[frame_len++] = low;
  }
  frame[frame_len++] = 0x0D;
  return frame_len;
}

void SolarPlugComponent::start_command_(const std::string &command, FrameStyle frame_style, uint8_t retry_count,
                                        bool retry_allowed) {
  this->empty_uart_buffer_();
  this->clear_response_frame_();
  this->active_command_ = command;
  this->active_retry_count_ = retry_count;
  this->active_retry_allowed_ = retry_allowed;
  this->state_ = State::WAITING;
  this->last_activity_ms_ = millis();

  std::array<uint8_t, MAX_REQUEST_FRAME_BYTES> frame{};
  const size_t frame_len = this->build_frame_(command, frame_style, frame.data(), frame.size());
  if (frame_len == 0) {
    ESP_LOGW(TAG, "failed to encode command frame command=%s", command.c_str());
    this->finish_frame_("encode_error");
    return;
  }

  this->write_array(frame.data(), frame_len);
  if (this->raw_frame_logging_) {
    ESP_LOGI(TAG, "send command=%s retry=%u frame_style=%s frame_hex=\"%s\"", command.c_str(),
             static_cast<unsigned>(retry_count),
             frame_style == FrameStyle::CRC_XMODEM_CR ? "crc_xmodem_cr" : "ascii_cr",
             hex_encode_bytes(frame.data(), frame_len).c_str());
  } else {
    ESP_LOGI(TAG, "send command=%s retry=%u frame_style=%s", command.c_str(), static_cast<unsigned>(retry_count),
             frame_style == FrameStyle::CRC_XMODEM_CR ? "crc_xmodem_cr" : "ascii_cr");
  }
}

void SolarPlugComponent::finish_frame_(const char *status) {
  const char *kind = this->passive_mode_ ? "passive" : "probe";
  if (this->raw_frame_logging_) {
    ESP_LOGI(TAG, "%s command=%s status=%s response_hex=\"%s\" response_ascii=\"%s\"", kind,
             this->active_command_.c_str(), status,
             hex_encode_bytes(this->response_frame_.data(), this->response_frame_len_).c_str(),
             ascii_encode_bytes(this->response_frame_.data(), this->response_frame_len_).c_str());
  } else {
    ESP_LOGI(TAG, "%s command=%s status=%s", kind, this->active_command_.c_str(), status);
  }
  if (!this->active_retry_allowed_ && !this->active_command_.empty()) {
    this->publish_write_status_(std::string(status) + " " + this->active_command_);
  }
  if (!this->passive_mode_ && this->active_retry_allowed_ && std::strcmp(status, "timeout") == 0 &&
      this->active_retry_count_ == 0 && !this->active_command_.empty()) {
    ESP_LOGW(TAG, "retrying read-only command after timeout command=%s", this->active_command_.c_str());
    this->queue_pending_front_({this->active_command_, FrameStyle::ASCII_CR, 1, true});
  }
  this->clear_response_frame_();
  this->active_command_.clear();
  this->active_retry_count_ = 0;
  this->active_retry_allowed_ = true;
  this->state_ = State::IDLE;
  this->last_activity_ms_ = 0;
}

bool SolarPlugComponent::extract_frame_payload_(std::string *payload_text, bool *crc_present) {
  if (payload_text == nullptr || this->response_frame_len_ == 0 ||
      this->response_frame_[this->response_frame_len_ - 1] != 0x0D) {
    return false;
  }

  size_t payload_len = this->response_frame_len_ - 1;
  bool crc = false;
  if (payload_len >= 3) {
    const size_t maybe_crc_payload_len = payload_len - 2;
    uint16_t crc_value = this->crc16_xmodem_(this->response_frame_.data(), maybe_crc_payload_len);
    auto [high, low] = this->adjust_crc_bytes_(crc_value);
    uint8_t wire_high = this->response_frame_[payload_len - 2];
    uint8_t wire_low = this->response_frame_[payload_len - 1];
    if (high == wire_high && low == wire_low) {
      payload_len = maybe_crc_payload_len;
      crc = true;
    }
  }

  *payload_text = std::string(reinterpret_cast<const char *>(this->response_frame_.data()), payload_len);
  if (crc_present != nullptr) {
    *crc_present = crc;
  }
  return true;
}

void SolarPlugComponent::log_decoded_payload_(const std::string &command, const std::string &payload) {
  FieldList fields = decode_fields(command, payload);
  if (this->decoded_field_logging_) {
    ESP_LOGD(TAG, "decoded command=%s payload=%s field_count=%u", command.c_str(), payload.c_str(),
             static_cast<unsigned>(fields.size()));
  }
  for (const auto &field : fields) {
    if (this->decoded_field_logging_) {
      const char *unit = field.unit.empty() ? "-" : field.unit.c_str();
      const char *confidence = field.confidence.empty() ? "-" : field.confidence.c_str();
      ESP_LOGD(TAG, "decoded command=%s field=%s value=%s unit=%s confidence=%s", command.c_str(), field.name.c_str(),
               field.value.c_str(), unit, confidence);
    }
    this->apply_decoded_field_(command, field.name, field.value);
  }
}

std::optional<float> SolarPlugComponent::hbms3_cell_voltage_mv(size_t index) const {
  if (index == 0 || index > this->latest_.hbms3_cell_voltage_mv.size()) {
    return std::nullopt;
  }
  return this->latest_.hbms3_cell_voltage_mv[index - 1];
}

void SolarPlugComponent::apply_decoded_field_(const std::string &command, const std::string &field_name,
                                       const std::string &value) {
  auto assign_float = [&](std::optional<float> &target) {
    target = parse_optional_float(value);
    this->publish_numeric_(command, field_name, value);
  };
  auto assign_text = [&](std::string &target) {
    target = value;
    this->publish_text_(command, field_name, value);
  };

  if (command == "HSTS") {
    if (field_name == "status_code") assign_text(this->latest_.hsts_status_code);
    else if (field_name == "mode_code") assign_text(this->latest_.hsts_mode_code);
    else if (field_name == "mode_label") assign_text(this->latest_.hsts_mode_label);
    else if (field_name == "status_bits") assign_text(this->latest_.hsts_status_bits);
    else if (field_name == "fault_bits") assign_text(this->latest_.hsts_fault_bits);
    return;
  }

  if (command == "HGRID") {
    if (field_name == "ac_input_voltage_v") assign_float(this->latest_.hgrid_ac_input_voltage_v);
    else if (field_name == "mains_frequency_hz") assign_float(this->latest_.hgrid_mains_frequency_hz);
    else if (field_name == "high_mains_loss_voltage_v") assign_float(this->latest_.hgrid_high_mains_loss_voltage_v);
    else if (field_name == "low_mains_loss_voltage_v") assign_float(this->latest_.hgrid_low_mains_loss_voltage_v);
    else if (field_name == "high_mains_loss_frequency_hz")
      assign_float(this->latest_.hgrid_high_mains_loss_frequency_hz);
    else if (field_name == "low_mains_loss_frequency_hz")
      assign_float(this->latest_.hgrid_low_mains_loss_frequency_hz);
    else if (field_name == "mains_power_w") assign_float(this->latest_.hgrid_mains_power_w);
    else if (field_name == "mains_current_flow_direction_code")
      assign_text(this->latest_.hgrid_mains_current_flow_direction_code);
    else if (field_name == "mains_current_flow_direction") assign_text(this->latest_.hgrid_mains_current_flow_direction);
    else if (field_name == "rated_power_w") assign_float(this->latest_.hgrid_rated_power_w);
    else if (field_name == "raw_tail") assign_text(this->latest_.hgrid_raw_tail);
    return;
  }

  if (command == "HOP") {
    if (field_name == "ac_output_voltage_v") assign_float(this->latest_.hop_ac_output_voltage_v);
    else if (field_name == "ac_output_frequency_hz") assign_float(this->latest_.hop_ac_output_frequency_hz);
    else if (field_name == "output_apparent_power_va") assign_float(this->latest_.hop_output_apparent_power_va);
    else if (field_name == "output_active_power_w") assign_float(this->latest_.hop_output_active_power_w);
    else if (field_name == "output_load_percent") assign_float(this->latest_.hop_output_load_percent);
    else if (field_name == "output_dc_component_status") assign_text(this->latest_.hop_output_dc_component_status);
    else if (field_name == "rated_power_w") assign_float(this->latest_.hop_rated_power_w);
    else if (field_name == "inductor_current_a") assign_float(this->latest_.hop_inductor_current_a);
    else if (field_name == "raw_tail") assign_text(this->latest_.hop_raw_tail);
    return;
  }

  if (command == "HBAT") {
    if (field_name == "battery_type") {
      assign_text(this->latest_.hbat_battery_type);
      this->publish_write_states_from_hbat_();
    } else if (field_name == "battery_voltage_v") assign_float(this->latest_.hbat_battery_voltage_v);
    else if (field_name == "battery_capacity_percent") assign_float(this->latest_.hbat_battery_capacity_percent);
    else if (field_name == "battery_charging_current_a") assign_float(this->latest_.hbat_battery_charging_current_a);
    else if (field_name == "battery_discharge_current_a") assign_float(this->latest_.hbat_battery_discharge_current_a);
    else if (field_name == "bus_voltage_v") assign_float(this->latest_.hbat_bus_voltage_v);
    return;
  }

  if (command == "HPV") {
    if (field_name == "pv_voltage_v") assign_float(this->latest_.hpv_pv_voltage_v);
    else if (field_name == "pv_current_a") assign_float(this->latest_.hpv_pv_current_a);
    else if (field_name == "pv_power_w") assign_float(this->latest_.hpv_pv_power_w);
    else if (field_name == "generation_power_kw") assign_float(this->latest_.hpv_generation_power_kw);
    return;
  }

  if (command == "HTEMP") {
    if (field_name == "inverter_temperature_c") assign_float(this->latest_.htemp_inverter_temperature_c);
    else if (field_name == "boost_temperature_c") assign_float(this->latest_.htemp_boost_temperature_c);
    else if (field_name == "transformer_temperature_c") assign_float(this->latest_.htemp_transformer_temperature_c);
    else if (field_name == "pv_temperature_c") assign_float(this->latest_.htemp_pv_temperature_c);
    else if (field_name == "fan_1_speed_percent") assign_float(this->latest_.htemp_fan_1_speed_percent);
    else if (field_name == "fan_2_speed_percent") assign_float(this->latest_.htemp_fan_2_speed_percent);
    else if (field_name == "max_temperature_c") assign_float(this->latest_.htemp_max_temperature_c);
    else if (field_name == "temperature_status_bits") assign_text(this->latest_.htemp_temperature_status_bits);
    else if (field_name == "raw_tail") assign_text(this->latest_.htemp_raw_tail);
    return;
  }

  if (command == "HGEN") {
    if (field_name == "date_ymd") assign_text(this->latest_.hgen_date_ymd);
    else if (field_name == "date_iso") assign_text(this->latest_.hgen_date_iso);
    else if (field_name == "time_hm") assign_text(this->latest_.hgen_time_hm);
    else if (field_name == "daily_power_gen_kwh") assign_float(this->latest_.hgen_daily_power_gen_kwh);
    else if (field_name == "monthly_electricity_generation_kwh")
      assign_float(this->latest_.hgen_monthly_electricity_generation_kwh);
    else if (field_name == "yearly_electricity_generation_kwh")
      assign_float(this->latest_.hgen_yearly_electricity_generation_kwh);
    else if (field_name == "total_power_generation_kwh") assign_float(this->latest_.hgen_total_power_generation_kwh);
    else if (field_name == "raw_tail") assign_text(this->latest_.hgen_raw_tail);
    return;
  }

  if (command == "QPRTL") {
    if (field_name == "device_type") assign_text(this->latest_.qprtl_device_type);
    return;
  }

  if (command == "HIMSG1") {
    if (field_name == "software_version") assign_text(this->latest_.himsg1_software_version);
    else if (field_name == "software_date") assign_text(this->latest_.himsg1_software_date);
    else if (field_name == "software_date_iso") assign_text(this->latest_.himsg1_software_date_iso);
    else if (field_name == "revision") assign_text(this->latest_.himsg1_revision);
    return;
  }

  if (command == "HBMS1") {
    if (field_name == "bms_status_code") assign_text(this->latest_.hbms1_bms_status_code);
    else if (field_name == "bms_flags") assign_text(this->latest_.hbms1_bms_flags);
    else if (field_name == "bms_discharge_voltage_limit_v") assign_float(this->latest_.hbms1_bms_discharge_voltage_limit_v);
    else if (field_name == "bms_charge_voltage_limit_v") assign_float(this->latest_.hbms1_bms_charge_voltage_limit_v);
    else if (field_name == "bms_charge_current_limit_a") assign_float(this->latest_.hbms1_bms_charge_current_limit_a);
    else if (field_name == "bms_soc_percent") assign_float(this->latest_.hbms1_bms_soc_percent);
    else if (field_name == "bms_charging_current_a") assign_float(this->latest_.hbms1_bms_charging_current_a);
    else if (field_name == "bms_discharge_current_a") assign_float(this->latest_.hbms1_bms_discharge_current_a);
    else if (field_name == "raw_tail") assign_text(this->latest_.hbms1_raw_tail);
    return;
  }

  if (command == "HBMS2") {
    if (field_name == "display_mode") assign_text(this->latest_.hbms2_display_mode);
    else if (field_name == "remaining_capacity") assign_float(this->latest_.hbms2_remaining_capacity_ah);
    else if (field_name == "nominal_capacity") assign_float(this->latest_.hbms2_nominal_capacity_ah);
    else if (field_name == "max_voltage") assign_float(this->latest_.hbms2_max_voltage_mv);
    else if (field_name == "max_voltage_cell_position")
      assign_float(this->latest_.hbms2_max_voltage_cell_position);
    else if (field_name == "min_voltage") assign_float(this->latest_.hbms2_min_voltage_mv);
    else if (field_name == "min_voltage_cell_position")
      assign_float(this->latest_.hbms2_min_voltage_cell_position);
    else if (field_name == "raw_tail") assign_text(this->latest_.hbms2_raw_tail);
    return;
  }

  if (command == "HBMS3") {
    if (field_name.rfind("cell_", 0) == 0) {
      size_t index = 0;
      if (field_name.size() == 10 && std::isdigit(static_cast<unsigned char>(field_name[5])) &&
          std::isdigit(static_cast<unsigned char>(field_name[6])) && field_name[7] == '_' && field_name[8] == 'm' &&
          field_name[9] == 'v') {
        index = static_cast<size_t>(((field_name[5] - '0') * 10) + (field_name[6] - '0'));
      }
      if (index >= 1 && index <= this->latest_.hbms3_cell_voltage_mv.size()) {
        this->latest_.hbms3_cell_voltage_mv[index - 1] = parse_optional_float(value);
        this->publish_numeric_(command, field_name, value);
      }
    } else if (field_name == "raw_tail") {
      assign_text(this->latest_.hbms3_raw_tail);
    }
    return;
  }

  if (command == "HEEP1") {
    if (field_name == "raw_payload") {
      assign_text(this->latest_.heep1_raw_payload);
      this->publish_write_states_from_heep1_(value);
    }
    return;
  }

  if (command == "HEEP2") {
    if (field_name == "raw_payload") {
      assign_text(this->latest_.heep2_raw_payload);
      this->publish_write_states_from_heep2_(value);
    }
    return;
  }

  if (command == "HPVB") {
    if (field_name == "raw_payload") {
      assign_text(this->latest_.hpvb_raw_payload);
    } else if (field_name == "pv_voltage_v") {
      assign_float(this->latest_.hpvb_pv_voltage_v);
    } else if (field_name == "pv_current_a") {
      assign_float(this->latest_.hpvb_pv_current_a);
    } else if (field_name == "pv_power_w") {
      assign_float(this->latest_.hpvb_pv_power_w);
    } else if (field_name == "pv_charging_mark_code") {
      assign_text(this->latest_.hpvb_pv_charging_mark_code);
    } else if (field_name == "pv_charging_mark") {
      assign_text(this->latest_.hpvb_pv_charging_mark);
    } else if (field_name == "bus_voltage_v") {
      assign_float(this->latest_.hpvb_bus_voltage_v);
    }
    return;
  }
}

void SolarPlugComponent::process_complete_frame_() {
  std::string payload_text;
  bool crc_present = false;
  if (!this->extract_frame_payload_(&payload_text, &crc_present)) {
    this->finish_frame_("crc_error");
    return;
  }
  if (payload_text.rfind("(NAK", 0) == 0) {
    this->finish_frame_("nak");
    return;
  }
  if (payload_text.rfind("(ACK", 0) == 0 || payload_text == "^1") {
    this->finish_frame_("ack");
    return;
  }

  if (crc_present) {
    ESP_LOGD(TAG, "crc-framed response detected for command=%s", this->active_command_.c_str());
  }
  this->log_decoded_payload_(this->active_command_, payload_text);
  this->finish_frame_("supported");
}

void SolarPlugComponent::process_passive_frame_() {
  std::string payload_text;
  bool crc_present = false;
  if (!this->extract_frame_payload_(&payload_text, &crc_present)) {
    this->clear_response_frame_();
    this->observed_command_.clear();
    this->last_activity_ms_ = 0;
    return;
  }

  if (payload_text.empty()) {
    this->clear_response_frame_();
    this->last_activity_ms_ = 0;
    return;
  }

  if (is_known_passive_label_(payload_text)) {
    this->observed_command_ = payload_text;
    if (this->raw_frame_logging_) {
      ESP_LOGI(TAG, "passive observed request command=%s frame_hex=\"%s\" frame_ascii=\"%s\"",
               payload_text.c_str(), hex_encode_bytes(this->response_frame_.data(), this->response_frame_len_).c_str(),
               ascii_encode_bytes(this->response_frame_.data(), this->response_frame_len_).c_str());
    } else {
      ESP_LOGI(TAG, "passive observed request command=%s", payload_text.c_str());
    }
    this->clear_response_frame_();
    this->last_activity_ms_ = 0;
    return;
  }

  if (this->observed_command_.empty()) {
    if (this->raw_frame_logging_) {
      ESP_LOGI(TAG, "passive observed frame frame_hex=\"%s\" frame_ascii=\"%s\"",
               hex_encode_bytes(this->response_frame_.data(), this->response_frame_len_).c_str(),
               ascii_encode_bytes(this->response_frame_.data(), this->response_frame_len_).c_str());
    } else {
      ESP_LOGI(TAG, "passive observed unpaired frame");
    }
    this->clear_response_frame_();
    this->last_activity_ms_ = 0;
    return;
  }

  this->active_command_ = this->observed_command_;
  this->observed_command_.clear();

  if (payload_text.rfind("(NAK", 0) == 0) {
    this->finish_frame_("nak");
    return;
  }
  if (payload_text.rfind("(ACK", 0) == 0 || payload_text == "^1") {
    this->finish_frame_("ack");
    return;
  }

  if (crc_present) {
    ESP_LOGD(TAG, "crc-framed response detected for command=%s", this->active_command_.c_str());
  }
  this->log_decoded_payload_(this->active_command_, payload_text);
  this->finish_frame_("supported");
}

void SolarPlugComponent::loop() {
  if (this->passive_mode_) {
    while (this->available() > 0) {
      uint8_t byte = 0;
      if (!this->read_byte(&byte)) {
        break;
      }
      if (this->response_frame_len_ == 0) {
        this->last_activity_ms_ = millis();
      }
      if (!this->append_response_byte_(byte)) {
        ESP_LOGW(TAG, "frame overflow in passive mode");
        this->clear_response_frame_();
        this->observed_command_.clear();
        this->last_activity_ms_ = 0;
        return;
      }
      this->last_activity_ms_ = millis();
      if (byte == 0x0D) {
        this->process_passive_frame_();
        return;
      }
    }

    if (this->last_activity_ms_ != 0 && millis() - this->last_activity_ms_ >= this->response_timeout_ms_) {
      this->clear_response_frame_();
      this->observed_command_.clear();
      this->last_activity_ms_ = 0;
    }
    return;
  }

  this->poll_due_groups_();

  PendingCommand pending;
  if (this->state_ == State::IDLE && this->pop_pending_command_(&pending)) {
    this->start_command_(pending.command, pending.frame_style, pending.retry_count, pending.retry_allowed);
    return;
  }

  if (this->state_ != State::WAITING) {
    return;
  }

  while (this->available() > 0) {
    uint8_t byte = 0;
    if (!this->read_byte(&byte)) {
      break;
    }
    if (this->response_frame_len_ == 0) {
      this->last_activity_ms_ = millis();
    }
    if (!this->append_response_byte_(byte)) {
      ESP_LOGW(TAG, "frame overflow command=%s", this->active_command_.c_str());
      this->finish_frame_("crc_error");
      return;
    }
    this->last_activity_ms_ = millis();
    if (byte == 0x0D) {
      this->process_complete_frame_();
      return;
    }
  }

  if (this->last_activity_ms_ != 0 && millis() - this->last_activity_ms_ >= this->response_timeout_ms_) {
    this->finish_frame_("timeout");
  }
}

}  // namespace solarplug
}  // namespace esphome
