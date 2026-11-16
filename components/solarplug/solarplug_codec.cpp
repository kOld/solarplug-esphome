// Pure protocol codec for the Solar Plug / HPVINV02 serial protocol. No ESPHome dependency, so it is
// compiled on the host by tests/cpp/codec_harness.cpp and checked against the Python reference decoder.
#include "solarplug_codec.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace esphome {
namespace solarplug {
namespace codec {

std::string fault_code_text(const std::string &code);

// Strict: the whole token must be a number. A line-noise token such as "05x.3" or "" must not
// silently become 5.0 / 0.0 and get published (H replies are CRC-checked only in response-CRC mode).
std::optional<float> parse_optional_float(const std::string &value) {
  if (value.empty()) {
    return std::nullopt;
  }
  char *end = nullptr;
  float parsed = std::strtof(value.c_str(), &end);
  if (end == value.c_str() || *end != '\0' || !std::isfinite(parsed)) {
    return std::nullopt;
  }
  return parsed;
}

// Minimum token count for a frame to be trusted. Short frames are truncated / corrupted reads;
// publishing their first few fields would mix stale and new values inside one sample.
size_t min_tokens_for(const std::string &command) {
  if (command == "HSTS") return 3;
  if (command == "HGRID") return 9;
  if (command == "HOP") return 8;
  if (command == "HBAT") return 6;
  if (command == "HPV") return 4;
  if (command == "HTEMP") return 6;
  if (command == "HGEN") return 6;
  if (command == "HBMS1") return 8;
  if (command == "HBMS2") return 7;
  if (command == "HBMS3") return 17;  // packed cell block: 16S here, longer on bigger packs
  if (command == "HEEP1") return 18;
  if (command == "HEEP2") return 14;
  if (command == "HEEP3") return 4;
  if (command == "HSTS2") return 3;
  if (command == "HCTMSG1") return 4;
  if (command == "HPVB") return 5;
  // PI30 reads (CRC-framed). QPIRI is 25 fields on HPVINV02 fw 40.05; fields up to 18 are consumed.
  if (command == "QPIRI") return 18;
  if (command == "HIMSG2") return 3;
  if (command == "QBEQI") return 8;
  if (command == "QFLAG") return 1;
  return 1;
}

bool ReplyLengthGuard::accept(const std::string &command, size_t tokens) {
  if (tokens < min_tokens_for(command)) return false;
  const uint8_t length = static_cast<uint8_t>(std::min<size_t>(tokens, 255));
  Entry *entry = nullptr;
  for (auto &candidate : this->entries_) {
    if (candidate.command == command) entry = &candidate;
  }
  if (entry == nullptr) {
    this->entries_.push_back({command, 0, length});  // first reply: accepted on the minimum alone
    return true;
  }
  if (entry->learned == length) {
    entry->candidate = 0;
    return true;
  }
  if (entry->candidate == length) {  // the same new length twice in a row: adopt it
    entry->learned = length;
    entry->candidate = 0;
    return true;
  }
  entry->candidate = length;
  return entry->learned == 0;  // still learning: accept; learned and different: drop once
}

// Battery type enum shared by HEEP1[3] digit 2, QPIRI[12] and the PBT<NN> setter (vendor tool
// TQF-AllInOne and the Easun SMT-III project agree; PBT03 = LIA and PBT04 = PYL are bus-confirmed here).
// HBAT[0] is NOT the battery type: it is the 12 V block series count (04 on a 48 V system).
const char *battery_type_label(int code) {
  static const char *const labels[] = {"AGM", "FLD", "USE", "LIA", "PYL", "TQF", "GRO", "LIB", "LIC", "FEL"};
  if (code < 0 || code > 9) return nullptr;
  return labels[code];
}


void add_field(FieldList &fields, const char *name, const std::string &value, const char *unit, const char *confidence) {
  fields.push_back({name, value, unit != nullptr ? unit : "", confidence != nullptr ? confidence : ""});
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
  char buffer[40];
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
  char buffer[40];
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
      } else if (mode_code == "S") {  // PI30 QMOD letters; only L and B are observed on this unit so far
        add_field(fields, "mode_label", "Standby Mode", nullptr, "low");
      } else if (mode_code == "F") {
        add_field(fields, "mode_label", "Fault Mode", nullptr, "low");
      } else if (mode_code == "H") {
        add_field(fields, "mode_label", "Power Saving Mode", nullptr, "low");
      } else if (mode_code == "P") {
        add_field(fields, "mode_label", "Power On Mode", nullptr, "low");
      } else if (mode_code == "D") {
        add_field(fields, "mode_label", "Shutdown Mode", nullptr, "low");
      } else if (mode_code == "X") {
        add_field(fields, "mode_label", "Test Mode", nullptr, "low");
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
  // Flag positions from the vendor HPVINV02 decoder (TQF-AllInOne HSTS view model). Live check on
  // this unit: "010000000000" = output on, no alarm; token 2 position 5 = 0 while the BMS link is up.
  if (tokens.size() > 2 && tokens[1].size() >= 13) {
    static const char *const alarm_labels[] = {nullptr, nullptr, nullptr, "Battery low", "Battery not connected",
                                               "Overload", "Over temperature", "EEPROM data error",
                                               "EEPROM read/write error", "PV power low", "Input over voltage",
                                               "Battery over voltage", "Fan fault"};
    std::string alarms;
    for (size_t pos = 3; pos <= 12; pos++) {
      if (tokens[1][pos] == '1') {
        if (!alarms.empty()) alarms += ", ";
        alarms += alarm_labels[pos];
      }
    }
    if (tokens[2].size() > 6) {
      if (tokens[2][5] == '1') alarms += alarms.empty() ? "BMS communication fault" : ", BMS communication fault";
      if (tokens[2][6] == '1') alarms += alarms.empty() ? "Temperature sensor fault" : ", Temperature sensor fault";
    }
    add_field(fields, "alarms", alarms.empty() ? "None" : alarms, nullptr, "medium");
  }
  if (!tokens.empty() && tokens[0].size() == 2 && is_digits(tokens[0])) {
    add_field(fields, "fault_code", tokens[0], nullptr, "medium");  // vendor: HSTS[0] is the fault code
    add_field(fields, "fault_description", fault_code_text(tokens[0]), nullptr, "low");
  }
  if (tokens.size() > 2 && tokens[2].size() >= 11) {
    // [2] d7-10: AC / inverter / charge / alarm LED, 0 off, 1 steady, 2 flashing (vendor HSTS_GB decoder).
    auto led = [](char c) -> const char * { return c == '0' ? "off" : (c == '1' ? "on" : (c == '2' ? "flashing" : "?")); };
    std::string leds = std::string("AC ") + led(tokens[2][7]) + ", inverter " + led(tokens[2][8]) + ", charge " +
                       led(tokens[2][9]) + ", alarm " + led(tokens[2][10]);
    add_field(fields, "front_panel_leds", leds, nullptr, "medium");
  }
  if (tokens.size() > 2 && tokens[2].size() >= 5) {
    const char tie = tokens[2][1];
    add_field(fields, "grid_tie_state", tie == '0' ? "Off-grid" : (tie == '1' ? "Waiting" : (tie == '2' ? "Grid-tied" : "Unknown")),
              nullptr, "medium");
    add_field(fields, "main_output_relay", tokens[2][3] == '1' ? "On" : "Off", nullptr, "medium");
    add_field(fields, "second_output", tokens[2][4] == '1' ? "On" : "Off", nullptr, "medium");
    // [2] d0 units in the parallel system, d2 this unit's role (raw: the vendor tool has no role table).
    add_field(fields, "parallel_units", tokens[2].substr(0, 1), nullptr, "medium");
    add_field(fields, "parallel_role", tokens[2].substr(2, 1), nullptr, "low");
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
  if (tokens.size() > 8 && tokens[8].size() >= 5 && is_digits(tokens[8].substr(0, 5)))
    add_field(fields, "inductor_power_w", tokens[8].substr(0, 5), "W", "medium");  // vendor: first 5 chars
  if (tokens.size() > 8)
    add_field(fields, "raw_tail", join_tokens(tokens, 8), nullptr, "low");
  return fields;
}

FieldList decode_hbat(const std::vector<std::string> &tokens) {
  FieldList fields;
  fields.reserve(8);
  if (!tokens.empty()) {
    add_field(fields, "battery_series_count", tokens[0], nullptr, "medium");  // 12 V blocks in series
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
  if (tokens.size() > 6 && tokens[6].size() >= 3) {
    // [6] d0 charge master switch, d1 solar charging, d2 AC charging (vendor HBAT_GB decoder).
    std::string sw = std::string("Master ") + (tokens[6][0] == '1' ? "on" : "off") + ", solar " +
                     (tokens[6][1] == '1' ? "on" : "off") + ", AC " + (tokens[6][2] == '1' ? "on" : "off");
    add_field(fields, "charge_switches", sw, nullptr, "medium");
  }
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
  // Order proven against the 2026-04-29 portal snapshot: capture "020 028 021 026 028 030 030" =
  // PV 20, inverter 28, boost 21, transformer 26, max 28, fan1 30 %, fan2 30 %. Token 4 always equals
  // max(tokens 0-3) in the capture corpus. (Before 2026-11-16 this decoder had tokens 0-6 rotated.)
  if (tokens.size() > 0)
    add_field(fields, "pv_temperature_c", tokens[0], "C", "high");
  if (tokens.size() > 1)
    add_field(fields, "inverter_temperature_c", tokens[1], "C", "high");
  if (tokens.size() > 2)
    add_field(fields, "boost_temperature_c", tokens[2], "C", "high");
  if (tokens.size() > 3)
    add_field(fields, "transformer_temperature_c", tokens[3], "C", "high");
  if (tokens.size() > 4)
    add_field(fields, "max_temperature_c", tokens[4], "C", "high");
  if (tokens.size() > 5)
    add_field(fields, "fan_1_speed_percent", tokens[5], "%", "high");
  if (tokens.size() > 6)
    add_field(fields, "fan_2_speed_percent", tokens[6], "%", "high");
  if (tokens.size() > 7) {
    add_field(fields, "temperature_status_bits", tokens[7], nullptr, "high");
    if (!tokens[7].empty() && (tokens[7][0] == '0' || tokens[7][0] == '1'))
      add_field(fields, "fan_enable", tokens[7][0] == '1' ? "On" : "Off", nullptr, "medium");  // vendor HTEMP d0
  }
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
  if (tokens.size() > 2) {
    add_field(fields, "revision", tokens[2], nullptr, "high");
    // Vendor tool: only digit 0 is meaningful, a program-integrity flag (1 complete, 0 incomplete).
    if (!tokens[2].empty() && (tokens[2][0] == '0' || tokens[2][0] == '1'))
      add_field(fields, "software_integrity", tokens[2][0] == '1' ? "Complete" : "Incomplete", nullptr, "medium");
  }
  if (tokens.size() > 3)
    add_field(fields, "raw_tail", join_tokens(tokens, 3), nullptr, "low");
  return fields;
}

FieldList decode_heep(const std::vector<std::string> &tokens);  // defined below

// HIMSG2: rated totals the vendor tool polls for HPVINV02 and uses as gauge denominators
// ([0] inverter, [1] MPPT, [2] AC input power). Never captured before 2026-11-16.
FieldList decode_himsg2(const std::vector<std::string> &tokens) {
  FieldList fields = decode_heep(tokens);  // raw_payload + token_NN
  if (tokens.size() > 0) add_field(fields, "rated_inverter_power_w", tokens[0], "W", "medium");
  if (tokens.size() > 1) add_field(fields, "rated_mppt_power_w", tokens[1], "W", "medium");
  if (tokens.size() > 2) add_field(fields, "rated_ac_power_w", tokens[2], "W", "medium");
  return fields;
}

// HEEP3 answers on HPVINV02 fw 40.05 (probe 2026-11-16: "2048 2048 2048 0150 000..."), although the vendor
// tool polls it only for HPVINV07/08. There [3] is the CT zero-adjust power (EZCTP, 4 digits, W); the 0150
// here fits that format but is unconfirmed. [0]-[2] (2048 = a 12-bit midpoint?) are unknown.
FieldList decode_heep3(const std::vector<std::string> &tokens) {
  FieldList fields = decode_heep(tokens);
  if (tokens.size() > 3 && tokens[3].size() == 4 && is_digits(tokens[3])) {
    add_field(fields, "ct_zero_adjust_power_w", tokens[3], "W", "low");
  }
  // HPVINV07/08 only (vendor HEEP3_PDF): [5] grid power limit (PGFP), [6] battery discharge current limit
  // (DISCC). HPVINV02 answers with 5 tokens, so these stay absent here.
  if (tokens.size() > 5 && is_digits(tokens[5])) add_field(fields, "grid_power_limit_w", tokens[5], "W", "low");
  if (tokens.size() > 6 && is_digits(tokens[6])) add_field(fields, "discharge_current_limit_a", tokens[6], "A", "low");
  return fields;
}

// HSTS2 (HPVINV08; vendor HSTS2_HPVINV08 decoder, sample "(00 B0010001 10701000 0000..."):
//   [1] d1 PV present, d2 AC input, d3 inverter output, d4 grid-tie, d5 energy to load, d6 battery charging,
//       d7 battery discharging; [2] d0 inverter state, d1 PV voltage OK, d2 inverter bridge, d3 MPPT, d4 PLL.
// The [2] digits are not all 0/1 in the vendor sample, so they are reported raw.
FieldList decode_hsts2(const std::vector<std::string> &tokens) {
  FieldList fields = decode_heep(tokens);
  if (tokens.size() > 1 && tokens[1].size() >= 8) {
    static const char *const labels[] = {nullptr,  "PV",     "AC input",          "output",
                                         "grid-tie", "to load", "battery charging", "battery discharging"};
    std::string flow;
    for (size_t pos = 1; pos <= 7; pos++) {
      if (tokens[1][pos] == '1') flow += (flow.empty() ? "" : ", ") + std::string(labels[pos]);
    }
    add_field(fields, "power_flow", flow.empty() ? "None" : flow, nullptr, "low");
  }
  if (tokens.size() > 2 && tokens[2].size() >= 5) {
    const std::string &t = tokens[2];
    add_field(fields, "inverter_flags",
              std::string("state ") + t[0] + ", PV OK " + t[1] + ", bridge " + t[2] + ", MPPT " + t[3] + ", PLL " + t[4],
              nullptr, "low");
  }
  return fields;
}

// HCTMSG1 (HPVINV07/08; vendor HCTMSG1_PDF): [0] CT current (first 4 characters), [3] CT power.
FieldList decode_hctmsg1(const std::vector<std::string> &tokens) {
  FieldList fields = decode_heep(tokens);
  if (!tokens.empty() && tokens[0].size() >= 4) add_field(fields, "ct_current_a", tokens[0].substr(0, 4), "A", "low");
  if (tokens.size() > 3) add_field(fields, "ct_power_w", tokens[3], "W", "low");
  return fields;
}

// HEEP2 named read-only fields (vendor HEEP2 decoder).
FieldList decode_heep2(const std::vector<std::string> &tokens) {
  FieldList fields = decode_heep(tokens);
  if (!tokens.empty() && (tokens[0] == "0" || tokens[0] == "1")) {
    // [0] dual output mode (program 60; PDAULC). Vendor: 0 Off, 1 On. Unconfirmed on the LCD.
    add_field(fields, "dual_output_mode", tokens[0] == "1" ? "On" : "Off", nullptr, "low");
  }
  // [11], [12], [14]: time windows the vendor tool does not parse. Easun names them the AC charge
  // window (ACCT, programs 46/47), the AC output window (ACLT, 48/49) and the output timer (67/68).
  // Kept raw until LCD experiment E1 settles the digit layout.
  if (tokens.size() > 11) add_field(fields, "ac_charge_window_raw", tokens[11], nullptr, "low");
  if (tokens.size() > 12) add_field(fields, "ac_output_window_raw", tokens[12], nullptr, "low");
  if (tokens.size() > 14) add_field(fields, "output_timer_raw", tokens[14], nullptr, "low");
  if (tokens.size() > 16 && tokens[16].size() == 5 && is_digits(tokens[16])) {
    // [16] d2-4: second-output discharge time in minutes (program 65; 000 = disabled).
    add_field(fields, "second_output_discharge_time_min", tokens[16].substr(2, 3), "min", "medium");
  }
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
  if (tokens.size() > 8) {
    // Deci-kelvin BMS average temperature (vendor decoder; 02946 -> 21.5 C matched the packs' 20-22 C).
    auto raw = parse_optional_float(tokens[8]);
    if (raw.has_value() && *raw > 2000.0f && *raw < 3800.0f) {
      char buf[16];
      std::snprintf(buf, sizeof(buf), "%.2f", (*raw - 2731.5f) / 10.0f);  // deci-kelvin: exact to 0.05 C
      add_field(fields, "bms_temperature_c", buf, "C", "medium");
    }
    add_field(fields, "raw_tail", join_tokens(tokens, 8), nullptr, "low");
  }
  if (tokens.size() > 1 && tokens[1].size() >= 9) {
    static const char *const flag_labels[] = {"Communication OK", "Low battery alarm", "Low battery fault",
                                              "Charge allowed", "Discharge allowed", "Charge over current",
                                              "Discharge over current", "Under temperature", "Over temperature"};
    std::string flags;
    for (size_t pos = 0; pos < 9; pos++) {
      if (tokens[1][pos] == '1') {
        if (!flags.empty()) flags += ", ";
        flags += flag_labels[pos];
      }
    }
    add_field(fields, "bms_flags_decoded", flags.empty() ? "None" : flags, nullptr, "medium");
  }
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

// QPIRI ratings/settings (25 fields on HPVINV02 fw 40.05, decode in docs/protocol QPIRI notes).
// HEEP1 named settings that are read-only here (layout: docs/protocol/HEEP_SETTINGS_MAP.md).
FieldList decode_heep1(const std::vector<std::string> &raw) {
  // Named fields and token_NN come from the HPVINV02 layout (normalize_heep1); raw_payload stays raw.
  const auto normalized = normalize_heep1(raw);
  FieldList fields;
  if (!normalized.has_value()) {
    if (!raw.empty()) fields.push_back({"raw_payload", join_tokens(raw, 0), "", "low"});
    return fields;
  }
  const std::vector<std::string> &tokens = *normalized;
  fields = decode_heep(tokens);
  fields[0].value = join_tokens(raw, 0);  // raw_payload: the reply as received
  if (tokens.size() > 4 && tokens[3].size() == 11 && is_digits(tokens[3])) {
    const std::string &packed = tokens[3];
    const char *battery = battery_type_label(packed[2] - '0');
    if (battery != nullptr) add_field(fields, "battery_type", battery, nullptr, "high");
    add_field(fields, "grid_protocol_mode", std::string("Mode ") + static_cast<char>(packed[1] + 1), nullptr, "medium");
    add_field(fields, "ct_function", packed[7] == '1' ? "On" : "Off", nullptr, "medium");
    add_field(fields, "rated_output_voltage_v", packed.substr(8, 3), "V", "high");
  }
  if (tokens.size() > 4 && tokens[4].size() == 3 && is_digits(tokens[4])) {
    add_field(fields, "output_frequency_setting_hz", tokens[4][0] == '1' ? "60" : "50", "Hz", "medium");
  }
  if (tokens.size() > 9) {
    add_field(fields, "bms_communication", tokens[9] == "1" ? "On" : "Off", nullptr, "high");
  }
  if (tokens.size() > 18 && !tokens[18].empty()) {
    add_field(fields, "grid_feed", tokens[18][0] == '1' ? "On" : "Off", nullptr, "medium");
  }
  return fields;
}

FieldList decode_qpiri(const std::vector<std::string> &tokens) {
  FieldList fields = decode_heep(tokens);  // raw_payload + token_NN keep the select readback path
  if (tokens.size() < 22) return fields;
  add_field(fields, "output_voltage_setting_v", tokens[2], "V", "high");       // program 10
  add_field(fields, "output_frequency_setting_hz", tokens[3], "Hz", "high");   // program 09
  add_field(fields, "battery_under_voltage_v", tokens[9], "V", "medium");
  add_field(fields, "battery_bulk_voltage_v", tokens[10], "V", "high");       // program 26
  add_field(fields, "battery_float_voltage_v", tokens[11], "V", "high");      // program 27
  static const char *const output_modes[] = {"Single", "Parallel", "Phase L1", "Phase L2", "Phase L3"};
  const std::string &mode = tokens[21];
  if (mode.size() == 1 && mode[0] >= '0' && mode[0] <= '4') {
    add_field(fields, "ac_output_mode", output_modes[mode[0] - '0'], nullptr, "medium");  // program 04
  }
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
  if (command == "HEEP1") return decode_heep1(tokens);
  if (command == "HEEP2") return decode_heep2(tokens);
  if (command == "HIMSG2") return decode_himsg2(tokens);
  if (command == "HEEP3") return decode_heep3(tokens);
  if (command == "HSTS2") return decode_hsts2(tokens);
  if (command == "HCTMSG1") return decode_hctmsg1(tokens);
  if (command == "QPIRI") return decode_qpiri(tokens);
  if (command == "QFLAG" || command == "QBEQI" || command == "QMCHGCR" || command == "QMUCHGCR")
    return decode_heep(tokens);  // raw + token_NN
  if (command == "HPVB") return decode_hpvb(tokens);
  return decode_generic_tokens(tokens, "low");
}


// Inverter manual 5.10.1 (fault codes) and 5.10.3 (BMS information codes). Whether HSTS[0] also carries
// the 5.10.2 warning codes (which reuse 01-04) is unconfirmed, so unknown codes are reported raw.
std::string fault_code_text(const std::string &code) {
  struct Entry {
    const char *code;
    const char *text;
  };
  static const Entry table[] = {
      {"00", "None"},
      {"01", "Fan locked while inverter off"},
      {"02", "Over temperature or NTC not connected"},
      {"03", "Battery voltage too high"},
      {"04", "Battery voltage too low"},
      {"05", "Output short circuit or converter over temperature"},
      {"06", "Output voltage too high"},
      {"07", "Overload time out"},
      {"08", "Bus voltage too high"},
      {"09", "Bus soft start failed"},
      {"51", "Over current or surge"},
      {"52", "Bus voltage too low"},
      {"53", "Inverter soft start failed"},
      {"55", "Over DC voltage in AC output"},
      {"57", "Current sensor failed"},
      {"58", "Output voltage too low"},
      {"59", "PV voltage over limit"},
      {"60", "BMS forbids charge and discharge"},
      {"61", "BMS communication lost"},
      {"69", "BMS forbids charging"},
      {"70", "BMS requests charging"},
      {"71", "BMS forbids discharging"},
  };
  for (const auto &entry : table) {
    if (code == entry.code) return entry.text;
  }
  return "Code " + code + " (not in manual 5.10)";
}

// ---- CRC16/XMODEM with the H-family byte adjustment (0x28, 0x0D, 0x0A are incremented) ----
uint16_t crc16_xmodem(const uint8_t *data, size_t len) {
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

std::pair<uint8_t, uint8_t> adjust_crc_bytes(uint16_t crc) {
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

// ---- Settings readback: what each snapshot says about every write control ----
namespace {
Reading flag_reading(const char *key, bool on) { return {key, ReadingKind::SWITCH, on ? "on" : "off", 0.0f, on}; }
Reading select_reading(const char *key, const std::string &value) { return {key, ReadingKind::SELECT, value, 0.0f, false}; }
Reading number_reading(const char *key, float value) { return {key, ReadingKind::NUMBER, "", value, false}; }
std::optional<bool> digit_flag(const std::string &token, size_t index) {
  if (index >= token.size()) return std::nullopt;
  if (token[index] == '0') return false;
  if (token[index] == '1') return true;
  return std::nullopt;
}
void add_flag(std::vector<Reading> &out, const char *key, std::optional<bool> value) {
  if (value.has_value()) out.push_back(flag_reading(key, *value));
}
void add_number(std::vector<Reading> &out, const char *key, const std::string &token) {
  auto value = parse_optional_float(token);
  if (value.has_value()) out.push_back(number_reading(key, *value));
}
int code_of(const std::string &token) {
  if (token.empty() || !is_digits(token)) return -1;
  return std::atoi(token.c_str());
}
// POPM00..04 / HEEP1[8] / QPIRI[21] (vendor labels).
const char *const OUTPUT_MODE_OPTIONS[] = {"SIG", "PAL", "3P1", "3P2", "3P3"};
// Program 13 readback ("054.0" / "00.0") as the select option: "Full" for 0, else "NN V".
void add_redischarge(std::vector<Reading> &out, const std::string &token) {
  const auto volts = parse_optional_float(token);
  if (!volts.has_value() || *volts < 0.0f || *volts > 99.0f) return;
  out.push_back(select_reading("battery_redischarge_voltage", *volts < 0.05f ? std::string("Full") : volts_option(*volts)));
}
// HEEP2[11] / [12] as "SSEE" (start hour, end hour). Tentative: LCD experiment E1 decides the layout.
void add_window(std::vector<Reading> &out, const char *start_key, const char *stop_key, const std::string &token) {
  if (token.size() != 4 || !is_digits(token)) return;
  out.push_back(number_reading(start_key, static_cast<float>(std::atoi(token.substr(0, 2).c_str()))));
  out.push_back(number_reading(stop_key, static_cast<float>(std::atoi(token.substr(2, 2).c_str()))));
}
}  // namespace

// HEEP1 zero-based layout (docs/protocol/HEEP_SETTINGS_MAP.md):
//   [0] output priority 0 SUB / 1 SBU; [1] max charge A; [2] max mains charge A;
//   [3] packed: d0 AC range, d2 battery type, d3 PV feeding, d4 overload restart, d5 prompt sound,
//       d6 over-temperature restart; [4] "0DC": D display return, C charger priority;
//   [5] buzzer; [6] backlight; [7] overload bypass; [9] BMS function; [10] BMS lock SOC;
//   [11] SOC back to mains; [12] SOC back to battery; [13] restart SOC; [14] bulk; [15] float;
//   [16] cut-off; [17] grid-connected current.
std::vector<Reading> heep1_readback(const std::vector<std::string> &raw, std::optional<MachineType> machine) {
  std::vector<Reading> out;
  const auto normalized = normalize_heep1(raw);
  if (!normalized.has_value()) return out;  // unknown layout: no readback rather than wrong slots
  const std::vector<std::string> &tokens = *normalized;
  if (machine.has_value() && tokens[0].size() == 1 && is_digits(tokens[0])) {
    const char *option = output_priority_option(*machine, tokens[0][0] - '0');
    if (option != nullptr) out.push_back(select_reading("output_source_priority", option));
  }
  if (tokens.size() > 1 && is_digits(tokens[1])) {
    out.push_back(select_reading("maximum_charging_current", amps_option(std::atoi(tokens[1].c_str()))));
  }
  if (tokens.size() > 2 && is_digits(tokens[2])) {
    out.push_back(select_reading("maximum_mains_charging_current", amps_option(std::atoi(tokens[2].c_str()))));
  }
  if (tokens.size() > 3 && tokens[3].size() == 11 && is_digits(tokens[3])) {
    const std::string &packed = tokens[3];
    if (packed[0] == '0') out.push_back(select_reading("ac_input_voltage_range", "Appliance"));
    if (packed[0] == '1') out.push_back(select_reading("ac_input_voltage_range", "UPS"));
    const char *battery = battery_type_label(packed[2] - '0');
    if (battery != nullptr) out.push_back(select_reading("battery_type", battery));
    if (packed[3] == '0') out.push_back(select_reading("pv_energy_feeding_priority", "BLU"));
    if (packed[3] == '1') out.push_back(select_reading("pv_energy_feeding_priority", "LBU"));
    add_flag(out, "overload_restart", digit_flag(packed, 4));
    add_flag(out, "input_source_detection_prompt_sound", digit_flag(packed, 5));
    add_flag(out, "over_temperature_restart", digit_flag(packed, 6));
    // Unsafe-profile controls (grid / output topology). d1 grid protocol, shown 1-based like the LCD
    // and portal ("Mode 4" = ^S???RS03); d7 CT function; d8-10 output voltage.
    if (packed[1] >= '0' && packed[1] <= '4') {
      out.push_back(select_reading("grid_protocol", std::string("Mode ") + static_cast<char>(packed[1] + 1)));
    }
    add_flag(out, "ct_function", digit_flag(packed, 7));
    out.push_back(select_reading("output_voltage", packed.substr(8, 3) + " V"));
  }
  if (tokens.size() > 4 && tokens[4].size() == 3 && is_digits(tokens[4])) {
    if (tokens[4][0] == '0') out.push_back(select_reading("output_frequency", "50 Hz"));
    if (tokens[4][0] == '1') out.push_back(select_reading("output_frequency", "60 Hz"));
    add_flag(out, "display_return_homepage", digit_flag(tokens[4], 1));
    const char *charger = machine.has_value() ? charger_priority_option(*machine, tokens[4][2] - '0') : nullptr;
    if (charger != nullptr) out.push_back(select_reading("charger_priority", charger));
  }
  if (tokens.size() > 5) add_flag(out, "buzzer", digit_flag(tokens[5], 0));
  if (tokens.size() > 6) add_flag(out, "backlight", digit_flag(tokens[6], 0));
  if (tokens.size() > 7) add_flag(out, "overload_bypass", digit_flag(tokens[7], 0));
  if (tokens.size() > 8 && tokens[8].size() == 1 && tokens[8][0] >= '0' && tokens[8][0] <= '4') {
    out.push_back(select_reading("output_mode", OUTPUT_MODE_OPTIONS[tokens[8][0] - '0']));
  }
  if (tokens.size() > 9) add_flag(out, "bms_function_enable", digit_flag(tokens[9], 0));
  if (tokens.size() > 18) add_flag(out, "grid_feed", digit_flag(tokens[18], 0));
  static const char *const number_keys[] = {
      "bms_lock_machine_battery_capacity", "soc_back_to_mains", "soc_back_to_battery", "inverter_restart_soc",
      "battery_bulk_voltage", "battery_float_voltage", "battery_cut_off_voltage", "grid_connected_current"};
  for (size_t i = 0; i < 8; i++) {
    if (tokens.size() > 10 + i) add_number(out, number_keys[i], tokens[10 + i]);
  }
  return out;
}

// HEEP2: [1] second-output cut-off V, [2] cut-off SOC, [3] low battery alarm V, [4] recharge V,
// [5] redischarge V, [7] equalization V, [8] eq time, [9] eq timeout, [10] eq interval,
// [13] second-output restore delay, [15] second-output recovery V,
// [16] "SSnnn": SS restore SOC, nnn second-output discharge time.
std::vector<Reading> heep2_readback(const std::vector<std::string> &tokens) {
  std::vector<Reading> out;
  if (!tokens.empty()) add_flag(out, "dual_output_mode", tokens[0].size() == 1 ? digit_flag(tokens[0], 0) : std::optional<bool>{});
  if (tokens.size() > 5) add_redischarge(out, tokens[5]);  // program 13 select: Full or NN V
  if (tokens.size() > 11) add_window(out, "ac_charge_start_hour", "ac_charge_stop_hour", tokens[11]);
  if (tokens.size() > 12) add_window(out, "ac_output_start_hour", "ac_output_stop_hour", tokens[12]);
  struct Slot {
    size_t index;
    const char *key;
  };
  static const Slot slots[] = {{1, "second_output_cutoff_voltage"},   {2, "second_output_cutoff_soc"},
                               {3, "low_battery_alarm_voltage"},      {4, "battery_recharge_voltage"},
                               {7, "battery_equalization_voltage"},
                               {8, "battery_equalization_time"},      {9, "battery_equalization_timeout"},
                               {10, "battery_equalization_interval"}, {13, "restore_second_output_delay_time"},
                               {15, "second_output_recovery_voltage"}};
  for (const auto &slot : slots) {
    if (tokens.size() > slot.index) add_number(out, slot.key, tokens[slot.index]);
  }
  if (tokens.size() > 16 && tokens[16].size() == 5 && is_digits(tokens[16])) {
    out.push_back(number_reading("restore_second_output_battery_capacity",
                                 static_cast<float>(std::atoi(tokens[16].substr(0, 2).c_str()))));
    out.push_back(number_reading("second_output_discharge_time",
                                 static_cast<float>(std::atoi(tokens[16].substr(2, 3).c_str()))));
  }
  return out;
}

// QPIRI (25 fields here): [12] battery type (PBT enum), [15] AC input range, [16] output priority
// (0 SUB, 1 SBU, 2 = SUF on sibling firmwares: not mapped, 3 PEC), [17] charger priority.
std::vector<Reading> qpiri_readback(const std::vector<std::string> &tokens, std::optional<MachineType> machine) {
  std::vector<Reading> out;
  if (tokens.size() < 18) return out;
  const bool type_b_codes = machine.has_value() && (*machine == MachineType::B || *machine == MachineType::D ||
                                                     *machine == MachineType::UNKNOWN);
  const int input_range = code_of(tokens[15]);
  if (input_range == 0) out.push_back(select_reading("ac_input_voltage_range", "Appliance"));
  if (input_range == 1) out.push_back(select_reading("ac_input_voltage_range", "UPS"));
  if (type_b_codes) {
    const char *output = output_priority_option(*machine, code_of(tokens[16]));
    if (output != nullptr) out.push_back(select_reading("output_source_priority", output));
    const char *charger = charger_priority_option(*machine, code_of(tokens[17]));
    if (charger != nullptr) out.push_back(select_reading("charger_priority", charger));
  }
  const char *battery = battery_type_label(code_of(tokens[12]));
  if (battery != nullptr) out.push_back(select_reading("battery_type", battery));
  // Cross-check only (HEEP1 is primary): [13] / [14] mains / maximum charging current, [2] output voltage,
  // [3] output frequency, [21] output mode.
  if (is_digits(tokens[13])) {
    out.push_back(select_reading("maximum_mains_charging_current", amps_option(std::atoi(tokens[13].c_str()))));
  }
  if (is_digits(tokens[14])) {
    out.push_back(select_reading("maximum_charging_current", amps_option(std::atoi(tokens[14].c_str()))));
  }
  auto output_voltage = parse_optional_float(tokens[2]);
  if (output_voltage.has_value()) {
    char text[12];
    std::snprintf(text, sizeof(text), "%03d V", static_cast<int>(*output_voltage + 0.5f));
    out.push_back(select_reading("output_voltage", text));
  }
  if (tokens.size() > 22) add_redischarge(out, tokens[22]);  // cross-check of HEEP2[5]
  if (tokens[3] == "50.0") out.push_back(select_reading("output_frequency", "50 Hz"));
  if (tokens[3] == "60.0") out.push_back(select_reading("output_frequency", "60 Hz"));
  if (tokens.size() > 21) {
    const int mode = code_of(tokens[21]);
    if (mode >= 0 && mode <= 4) out.push_back(select_reading("output_mode", OUTPUT_MODE_OPTIONS[mode]));
  }
  return out;
}

// QFLAG "(E<enabled letters>D<disabled letters>" with the PI30 letters used by the PE/PD setters.
std::vector<Reading> qflag_readback(const std::vector<std::string> &tokens) {
  std::vector<Reading> out;
  if (tokens.empty() || tokens[0].empty() || tokens[0][0] != 'E') return out;
  struct FlagKey {
    char letter;
    const char *key;
  };
  static const FlagKey flags[] = {
      {'a', "buzzer"},           {'b', "overload_bypass"},          {'j', "power_saving"},
      {'k', "display_return_homepage"}, {'u', "overload_restart"}, {'v', "over_temperature_restart"},
      {'x', "backlight"},        {'y', "input_source_detection_prompt_sound"}, {'z', "fault_code_record"},
  };
  bool enabled = true;
  for (char ch : tokens[0]) {
    if (ch == 'E' || ch == 'D') {
      enabled = ch == 'E';
      continue;
    }
    for (const auto &flag : flags) {
      if (flag.letter == ch) out.push_back(flag_reading(flag.key, enabled));
    }
  }
  return out;
}

// QBEQI "(E TTT PPP CCC RRR VV.VV ...": [0] equalization enabled (0/1).
std::vector<Reading> qbeqi_readback(const std::vector<std::string> &tokens) {
  std::vector<Reading> out;
  if (!tokens.empty() && (tokens[0] == "0" || tokens[0] == "1")) {
    out.push_back(flag_reading("battery_equalization_mode_enable", tokens[0] == "1"));
  }
  return out;
}

ResponseCrcTracker::Result ResponseCrcTracker::on_h_reply(bool crc_present) {
  if (crc_present) {
    this->plain_streak_ = 0;
    if (this->on_) return {true, false};
    if (++this->crc_streak_ < SWITCH_AFTER) return {false, false};
    return {true, this->set_mode(true)};  // verified by its CRC: safe to decode
  }
  this->crc_streak_ = 0;
  if (!this->on_) return {true, false};
  this->refresh_wanted_ = true;
  if (++this->plain_streak_ < SWITCH_AFTER) return {false, false};
  return {false, this->set_mode(false)};  // refresh_wanted_ stays set: the keep-alive keeps trying
}

bool ResponseCrcTracker::set_mode(bool on) {
  this->crc_streak_ = 0;
  this->plain_streak_ = 0;
  if (on) this->refresh_wanted_ = false;
  const bool changed = this->on_ != on;
  this->on_ = on;
  return changed;
}

bool crc_keepalive_due(bool sent_before, uint32_t ms_since_last, bool refresh_wanted, uint8_t failures) {
  if (!sent_before) return true;
  if (failures >= CRC_BACKOFF_AFTER) return ms_since_last >= CRC_BACKOFF_MS;
  if (ms_since_last >= CRC_KEEPALIVE_MS) return true;
  return refresh_wanted && ms_since_last >= CRC_MIN_REFRESH_SPACING_MS;
}

MachineType machine_type_from_qprtl(const std::string &device_type) {
  std::string id = device_type;
  if (!id.empty() && id[0] == '(') id.erase(0, 1);
  if (id == "HPVINV01") return MachineType::A;
  if (id == "HPVINV02" || id == "HPVINV05") return MachineType::B;
  if (id == "HPVINV03") return MachineType::C;
  if (id == "HPVINV04") return MachineType::D;
  if (id == "HPVINV08") return MachineType::F;
  return MachineType::UNKNOWN;
}

const char *machine_type_name(MachineType type) {
  switch (type) {
    case MachineType::A:
      return "A";
    case MachineType::B:
      return "B";
    case MachineType::C:
      return "C";
    case MachineType::D:
      return "D";
    case MachineType::F:
      return "F";
    case MachineType::UNKNOWN:
      break;
  }
  return "unknown";
}

namespace {
struct PriorityCode {
  const char *option;
  int code;
};
// Indexed by family: 0 = B/D/UNKNOWN, 1 = A/C, 2 = F.
int priority_family(MachineType type) {
  if (type == MachineType::A || type == MachineType::C) return 1;
  if (type == MachineType::F) return 2;
  return 0;
}
const std::vector<PriorityCode> &output_table(MachineType type) {
  static const std::vector<PriorityCode> tables[3] = {
      {{"SUB priority", 0}, {"SBU priority", 1}, {"PEC Mode (CT)", 3}},
      {{"Utility first (UTI)", 0}, {"SUB priority", 1}, {"SBU priority", 2}},
      {{"SUB priority", 0}, {"SBU priority", 1}, {"PEC Mode (CT)", 3}},
  };
  return tables[priority_family(type)];
}
const std::vector<PriorityCode> &charger_table(MachineType type) {
  static const std::vector<PriorityCode> tables[3] = {
      {{"CSO", 0}, {"SNU", 1}, {"OSO", 2}, {"SOR", 3}},
      {{"CSO", 1}, {"SNU", 2}, {"OSO", 3}},
      {{"CSO", 0}, {"SNU", 1}, {"OSO", 2}, {"SOR", 3}},
  };
  return tables[priority_family(type)];
}
std::optional<int> code_for(const std::vector<PriorityCode> &table, const std::string &option) {
  for (const auto &entry : table) {
    if (option == entry.option) return entry.code;
  }
  return std::nullopt;
}
const char *option_for(const std::vector<PriorityCode> &table, int code) {
  for (const auto &entry : table) {
    if (entry.code == code) return entry.option;
  }
  return nullptr;
}
}  // namespace

bool same_priority_codes(MachineType a, MachineType b) { return priority_family(a) == priority_family(b); }

std::optional<int> output_priority_code(MachineType type, const std::string &option) {
  return code_for(output_table(type), option);
}
const char *output_priority_option(MachineType type, int code) { return option_for(output_table(type), code); }
std::optional<int> charger_priority_code(MachineType type, const std::string &option) {
  return code_for(charger_table(type), option);
}
const char *charger_priority_option(MachineType type, int code) { return option_for(charger_table(type), code); }

std::optional<std::vector<std::string>> normalize_heep1(const std::vector<std::string> &tokens) {
  auto shaped = [](const std::vector<std::string> &t) {
    return t.size() == 20 && t[3].size() == 11 && is_digits(t[3]) && t[4].size() == 3 && is_digits(t[4]);
  };
  if (shaped(tokens)) return tokens;
  if (tokens.size() == 21) {  // generic GB3024: drop the extra [4]
    std::vector<std::string> out(tokens);
    out.erase(out.begin() + 4);
    if (shaped(out)) return out;
  }
  if (tokens.size() == 19 && tokens[4].size() == 5 && is_digits(tokens[4])) {  // LPVINV02
    std::vector<std::string> out(tokens.begin(), tokens.begin() + 4);
    out.push_back(tokens[4].substr(0, 3));  // frequency / display return / charger, as on HPVINV02
    out.push_back(tokens[4].substr(4, 1));  // buzzer (d3 is the fault-log record)
    out.insert(out.end(), tokens.begin() + 5, tokens.end());
    if (shaped(out)) return out;
  }
  return std::nullopt;
}

bool charge_current_in_envelope(bool mains, int amps) {
  if (mains && amps == 2) return true;
  return amps >= 10 && amps <= (mains ? 60 : 80) && amps % 10 == 0;
}

std::string amps_option(int amps) { return std::to_string(amps) + " A"; }

bool charge_current_allowed(const std::vector<int> *envelope, bool mains, int amps) {
  if (envelope == nullptr || envelope->empty()) return charge_current_in_envelope(mains, amps);
  return std::find(envelope->begin(), envelope->end(), amps) != envelope->end();
}

std::string volts_option(float volts) {
  char buf[16];
  if (std::fabs(volts - std::round(volts)) < 0.01f) {
    std::snprintf(buf, sizeof(buf), "%d V", static_cast<int>(std::lround(volts)));
  } else {
    std::snprintf(buf, sizeof(buf), "%.1f V", volts);
  }
  return buf;
}

std::optional<int> parse_amps_option(const std::string &option) {
  if (option.size() < 3 || option.size() > 5 || option.compare(option.size() - 2, 2, " A") != 0) return std::nullopt;
  const std::string digits = option.substr(0, option.size() - 2);
  if (!is_digits(digits)) return std::nullopt;
  return std::atoi(digits.c_str());
}

bool is_printable_ascii(const std::string &value) {
  for (char ch : value) {
    const auto byte = static_cast<unsigned char>(ch);
    if (byte < 0x20 || byte > 0x7E) return false;
  }
  return true;
}

std::string printable_ascii(const std::string &value) {
  std::string out = value;
  for (char &ch : out) {
    const auto byte = static_cast<unsigned char>(ch);
    if (byte < 0x20 || byte > 0x7E) ch = '?';
  }
  return out;
}

ReplyClass classify_reply(const std::string &command, const std::string &payload, bool crc_present, bool expects_ack) {
  const bool crc_mode_ack = expects_ack && (command == "HOSTCRCEN" || command == "HOSTCRCDN") &&
                            payload == "(CRC16" + command.substr(7, 2);
  const bool ack = payload.rfind("(ACK", 0) == 0 || payload == "^1" || crc_mode_ack;
  const bool nak = payload.rfind("(NAK", 0) == 0 || payload == "^0";  // ^1 / ^0 answer ^S??? setters
  if (ack || nak) {
    // Every captured ACK/NAK/^1 carries a valid CRC. An ACK or NAK during a read is a late reply to an
    // earlier write (unsupported reads time out instead), so it is never credited to the read.
    if (!crc_present || !expects_ack) return ReplyClass::INVALID;
    return nak ? ReplyClass::NAK : ReplyClass::ACK;
  }
  return expects_ack ? ReplyClass::INVALID : ReplyClass::DATA;
}

std::vector<Reading> settings_readback(const std::string &command, const std::string &payload,
                                       std::optional<MachineType> machine) {
  const std::vector<std::string> tokens = split_tokens(payload);
  if (command == "HEEP1") return heep1_readback(tokens, machine);
  if (command == "HEEP2") return heep2_readback(tokens);
  if (command == "QPIRI") return qpiri_readback(tokens, machine);
  if (command == "QFLAG") return qflag_readback(tokens);
  if (command == "QBEQI") return qbeqi_readback(tokens);
  return {};
}

}  // namespace codec
}  // namespace solarplug
}  // namespace esphome
