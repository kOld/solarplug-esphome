#include "solarplug_writes.h"

#include "solarplug_codec.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

// Frame formats and ranges: docs/protocol/HEEP_SETTINGS_MAP.md (evidence per control) and
// docs/protocol/WRITE_SURFACE.md. Formats from the vendor HPVINV02 screen (TQF-AllInOne) are marked.
namespace esphome {
namespace solarplug {
namespace writes {

namespace {

std::string format(const char *pattern, double value) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), pattern, value);
  return buf;
}

std::string format_int(const char *pattern, unsigned value) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), pattern, value);
  return buf;
}

// value is inside [min, max] and a whole multiple of step (step 0 = any value).
bool on_grid(float value, float min_value, float max_value, float step) {
  if (!(value >= min_value && value <= max_value)) return false;  // also rejects NaN
  if (step <= 0.0f) return true;
  const float steps = (value - min_value) / step;
  return std::fabs(steps - std::round(steps)) < 0.01f;
}

std::string range_reason(float min_value, float max_value, float step) {
  char buf[128];  // three %g of up to 13 characters each, plus the text
  if (step > 0.0f) {
    std::snprintf(buf, sizeof(buf), "outside %g-%g or not a multiple of %g", min_value, max_value, step);
  } else {
    std::snprintf(buf, sizeof(buf), "outside %g-%g", min_value, max_value);
  }
  return buf;
}

unsigned whole(float value) { return static_cast<unsigned>(std::lround(value)); }  // callers range-check first

// A voltage setting is refused when the configured bank differs from the one the inverter reports.
std::optional<std::string> bank_mismatch(const Context &context) {
  if (context.detected_battery_voltage == 0) return std::string("battery bank size (HBAT) not read from the inverter yet");
  if (context.detected_battery_voltage == context.battery_voltage) return std::nullopt;
  return "battery_voltage is " + std::to_string(context.battery_voltage) + " V but the inverter reports a " +
         std::to_string(context.detected_battery_voltage) + " V bank";
}

std::optional<float> reported(const Context &context, const char *key) {
  if (context.reported == nullptr) return std::nullopt;
  auto it = context.reported->find(key);
  if (it == context.reported->end()) return std::nullopt;
  return it->second;
}

}  // namespace

std::vector<std::string> redischarge_options(int battery_voltage) {
  std::vector<std::string> options = {"Full"};
  const float scale = static_cast<float>(battery_voltage) / 48.0f;
  for (int volts = 48; volts <= 58; volts++) options.push_back(codec::volts_option(static_cast<float>(volts) * scale));
  return options;
}

Result encode_switch(const std::string &key, bool on) {
  struct Entry {
    const char *key;
    const char *on;
    const char *off;
    const char *label;
  };
  static const Entry entries[] = {
      // PI30 PE/PD flags, read back through HEEP1 and QFLAG.
      {"buzzer", "PEa", "PDa", "Buzzer (program 18)"},
      {"backlight", "PEx", "PDx", "Backlight (program 20)"},
      {"display_return_homepage", "PEk", "PDk", "Return to default screen (program 19)"},
      {"over_temperature_restart", "PEv", "PDv", "Over-temperature restart (program 07)"},
      {"overload_restart", "PEu", "PDu", "Overload restart (program 06)"},
      {"input_source_detection_prompt_sound", "PEy", "PDy", "Input source prompt sound (program 22)"},
      {"overload_bypass", "PEb", "PDb", "Overload bypass (program 23)"},
      {"power_saving", "PEj", "PDj", "ECO power saving (program 08)"},
      {"fault_code_record", "PEz", "PDz", "Record fault code (program 25)"},
      {"battery_equalization_mode_enable", "PBEQE1", "PBEQE0", "Battery equalization (program 30)"},
      {"bms_function_enable", "BMSC01", "BMSC00", "BMS function (program 37)"},
      // Unsafe profile, vendor HPVINV02 screen; never bus-tested on this unit.
      {"dual_output_mode", "PDAULC01", "PDAULC00", "Dual output mode (program 60)"},
      {"second_output_power", "PDAULON01", "PDAULON00", "Second output on/off"},
      {"grid_feed", "GRIDFEED01", "GRIDFEED00", "Grid feed function"},
      {"ct_function", "EXTCT01", "EXTCT00", "CT function"},
  };
  for (const auto &entry : entries) {
    if (key == entry.key) return Result::ok(on ? entry.on : entry.off, entry.label);
  }
  return Result::refuse("unknown switch");
}

Result encode_select(const std::string &key, const std::string &option, const Context &context) {
  struct Entry {
    const char *key;
    const char *option;
    const char *command;
    const char *label;
  };
  static const Entry entries[] = {
      {"pv_energy_feeding_priority", "BLU", "PVENGUSE00", "Solar supply priority (program 43)"},
      {"pv_energy_feeding_priority", "LBU", "PVENGUSE01", "Solar supply priority (program 43)"},
      {"ac_input_voltage_range", "Appliance", "PGR00", "AC input voltage range (program 03)"},
      {"ac_input_voltage_range", "UPS", "PGR01", "AC input voltage range (program 03)"},
      // Unsafe profile (vendor HPVINV02 screen, never bus-tested).
      {"output_frequency", "50 Hz", "F50", "Output frequency (program 09)"},
      {"output_frequency", "60 Hz", "F60", "Output frequency (program 09)"},
      {"output_voltage", "220 V", "V220", "Output voltage (program 10)"},
      {"output_voltage", "230 V", "V230", "Output voltage (program 10)"},
      {"output_voltage", "240 V", "V240", "Output voltage (program 10)"},
      {"output_mode", "SIG", "POPM00", "AC output mode (program 04)"},
      {"output_mode", "PAL", "POPM01", "AC output mode (program 04)"},
      {"output_mode", "3P1", "POPM02", "AC output mode (program 04)"},
      {"output_mode", "3P2", "POPM03", "AC output mode (program 04)"},
      {"output_mode", "3P3", "POPM04", "AC output mode (program 04)"},
      // Program 50, shown 1-based like the LCD and portal: "Mode 4" = ^S???RS03.
      {"grid_protocol", "Mode 1", "^S???RS00", "Grid protocol (program 50)"},
      {"grid_protocol", "Mode 2", "^S???RS01", "Grid protocol (program 50)"},
      {"grid_protocol", "Mode 3", "^S???RS02", "Grid protocol (program 50)"},
      {"grid_protocol", "Mode 4", "^S???RS03", "Grid protocol (program 50)"},
      {"grid_protocol", "Mode 5", "^S???RS04", "Grid protocol (program 50)"},
  };
  for (const auto &entry : entries) {
    if (key == entry.key && option == entry.option) return Result::ok(entry.command, entry.label);
  }
  if (key == "output_source_priority" || key == "charger_priority") {
    // Codes depend on the machine type (codec::MachineType). POP02 (SUF, feeds the grid) is never encodable.
    if (context.machine_type_conflict) return Result::refuse("machine_type in YAML contradicts the QPRTL reply");
    if (!context.machine_type.has_value()) return Result::refuse("machine type not known yet (QPRTL not read)");
    const bool output = key == "output_source_priority";
    const auto code = output ? codec::output_priority_code(*context.machine_type, option)
                             : codec::charger_priority_code(*context.machine_type, option);
    if (!code.has_value()) {
      return Result::refuse(option + " is not available on machine type " + codec::machine_type_name(*context.machine_type));
    }
    return Result::ok(format_int(output ? "POP%02u" : "PCP%02u", static_cast<unsigned>(*code)),
                      output ? "Output source priority (program 01)" : "Charger priority (program 16)");
  }
  if (key == "battery_type") {
    // PBT<NN> shares the readback enum (PBT03 = LIA and PBT04 = PYL are bus-confirmed).
    for (int code = 0; code < 10; code++) {
      const char *label = codec::battery_type_label(code);
      if (label != nullptr && option == label) {
        return Result::ok(format_int("PBT%02u", static_cast<unsigned>(code)), "Battery type (program 05)");
      }
    }
    return Result::refuse("unknown battery type");
  }
  if (key == "battery_redischarge_voltage") {
    // Program 13: "Full" (battery fully charged, sent as 00.0) or 48-58 V in 1 V steps, scaled to the bank.
    if (const auto why = bank_mismatch(context)) return Result::refuse(*why);
    if (option == "Full") return Result::ok("PBDV00.0", "Back to battery voltage (program 13)");
    for (const auto &offered : redischarge_options(context.battery_voltage)) {
      if (offered != "Full" && option == offered) {
        return Result::ok(format("PBDV%04.1f", std::atof(option.c_str())), "Back to battery voltage (program 13)");
      }
    }
    return Result::refuse("must be Full or one of the program 13 voltages for a " +
                          std::to_string(context.battery_voltage) + " V bank");
  }
  if (key == "maximum_charging_current" || key == "maximum_mains_charging_current") {
    // Programs 02 / 11: inside the model envelope and, once announced, on the inverter's own list.
    const bool mains = key == "maximum_mains_charging_current";
    const auto amps = codec::parse_amps_option(option);
    const std::vector<int> *envelope = mains ? context.mains_charge_envelope : context.max_charge_envelope;
    if (!amps.has_value() || !codec::charge_current_allowed(envelope, mains, *amps)) {
      return Result::refuse(option + " is outside the configured options");
    }
    const std::vector<int> *allowed = mains ? context.allowed_mains_charge_a : context.allowed_max_charge_a;
    if (allowed != nullptr && !allowed->empty() && std::find(allowed->begin(), allowed->end(), *amps) == allowed->end()) {
      return Result::refuse(option + " is not on the inverter's announced list");
    }
    return Result::ok(format_int(mains ? "MUCHGC%03u" : "MNCHGC%03u", static_cast<unsigned>(*amps)),
                      mains ? "Maximum mains charging current (program 11)" : "Maximum charging current (program 02)");
  }
  return Result::refuse("unknown option " + option);
}

Result encode_number(const std::string &key, float value, const Context &context) {
  // NaN and negative or huge values never reach a float-to-unsigned conversion.
  if (!std::isfinite(value) || value < 0.0f || value > 1000.0f) return Result::refuse("not a finite value in 0-1000");

  // Independent settings: a fixed range, step and frame format.
  struct Plain {
    const char *key;
    float min_value;  // 48 V bank; voltage settings scale with battery_voltage / 48
    float max_value;
    float step;  // 0: any value in range
    const char *pattern;
    bool integer;  // pattern takes an unsigned, else a double
    bool volts;    // a battery voltage: scaled to the bank and refused on a bank mismatch
    const char *label;
  };
  static const Plain plain[] = {
      {"battery_equalization_voltage", 48.0f, 60.0f, 0.0f, "PBEQV%05.2f", false, true, "Equalization voltage (program 31)"},
      {"battery_equalization_interval", 0.0f, 90.0f, 1.0f, "PBEQP%03u", true, false, "Equalization interval (program 35)"},
      {"battery_equalization_timeout", 5.0f, 900.0f, 5.0f, "PBEQOT%03u", true, false, "Equalization timeout (program 34)"},
      {"battery_equalization_time", 5.0f, 900.0f, 5.0f, "PBEQT%03u", true, false, "Equalization time (program 33)"},
      {"battery_cut_off_voltage", 40.0f, 52.0f, 0.0f, "PSDV%04.1f", false, true, "Low DC cut-off voltage (program 29)"},
      {"battery_bulk_voltage", 48.0f, 58.4f, 0.0f, "PCVV%04.1f", false, true, "Bulk charging voltage (program 26)"},
      {"battery_float_voltage", 48.0f, 58.4f, 0.0f, "PBFT%04.1f", false, true, "Float charging voltage (program 27)"},
      {"battery_recharge_voltage", 44.0f, 51.0f, 1.0f, "PBCV%04.1f", false, true, "Back to utility voltage (program 12)"},
      {"restore_second_output_delay_time", 0.0f, 60.0f, 5.0f, "PDDLYT%03u", true, false, "Dual recover delay (program 66)"},
      // Vendor formats; PSLV / PDSDV / PDSRV NAK with a lithium battery type (unsafe profile).
      {"low_battery_alarm_voltage", 40.0f, 54.0f, 0.0f, "PSLV%04.1f", false, true, "Low battery alarm voltage (program 24)"},
      {"second_output_cutoff_voltage", 40.0f, 54.0f, 0.0f, "PDSDV%04.1f", false, true, "Dual output cut-off voltage (program 61)"},
      {"second_output_recovery_voltage", 44.0f, 58.0f, 0.0f, "PDSRV%04.1f", false, true, "Dual output recovery voltage (program 63)"},
      // Program 65: 4 digits (PDDCGT000 got no reply at all, PDDCGT0000 was ACKed). 0 vs "Disable": unconfirmed.
      {"second_output_discharge_time", 0.0f, 990.0f, 5.0f, "PDDCGT%04u", true, false, "Dual output discharge time (program 65)"},
      // Program 56: only 20 and 21 A have been sent; capped near the 26.9 A rating.
      {"grid_connected_current", 0.0f, 30.0f, 1.0f, "PGFC%03u", true, false, "Grid-tie current (program 56)"},
      {"inverter_restart_soc", 5.0f, 95.0f, 5.0f, "BMSSRC%03u", true, false, "Battery restart SOC (program 41)"},
  };
  for (const auto &entry : plain) {
    if (key != entry.key) continue;
    float min_value = entry.min_value;
    float max_value = entry.max_value;
    float step = entry.step;
    if (entry.volts) {
      if (const auto why = bank_mismatch(context)) return Result::refuse(*why);
      const float scale = static_cast<float>(context.battery_voltage) / 48.0f;
      min_value *= scale;
      max_value *= scale;
      if (step > 0.0f) step = std::max(0.1f, step * scale);  // the frame format resolves 0.1 V
    }
    if (!on_grid(value, min_value, max_value, step)) return Result::refuse(range_reason(min_value, max_value, step));
    return Result::ok(entry.integer ? format_int(entry.pattern, whole(value)) : format(entry.pattern, value), entry.label);
  }

  // Ordered and paired settings: checked against what the inverter last reported (or acknowledged). While
  // another write is queued or active that value may be about to change, so they fail closed until it ends.
  struct Ordered {
    const char *key;
    float min_value;
    float max_value;
    const char *prefix;
    const char *label;
  };
  static const Ordered ordered[] = {
      {"bms_lock_machine_battery_capacity", 0.0f, 20.0f, "BMSSDC", "BMS lock SOC (program 38)"},
      {"soc_back_to_mains", 5.0f, 95.0f, "BMSB2UC", "SOC back to mains (program 39)"},
      {"soc_back_to_battery", 10.0f, 100.0f, "BMSU2BC", "SOC back to battery (program 40)"},
      {"second_output_cutoff_soc", 0.0f, 95.0f, "PDSDS", "Dual output cut-off SOC (program 62)"},
      // Program 64; the 50 % maximum rests on one off-grid NAK (051).
      {"restore_second_output_battery_capacity", 0.0f, 50.0f, "PDSRS", "Dual output restore SOC (program 64)"},
  };
  for (const auto &entry : ordered) {
    if (key != entry.key) continue;
    if (context.writes_in_flight) return Result::refuse("another write is still in flight");
    // SOC settings only accept multiples of 5 (every captured NAK was off the 5 % grid).
    if (!on_grid(value, entry.min_value, entry.max_value, 5.0f)) {
      return Result::refuse(range_reason(entry.min_value, entry.max_value, 5.0f));
    }
    // BMS lock SOC < SOC back to mains < SOC back to battery; cut-off SOC < restore SOC.
    struct Bound {
      const char *other;
      bool must_be_below;  // this value < other (else this value > other)
    };
    std::vector<Bound> bounds;
    if (key == "bms_lock_machine_battery_capacity") bounds = {{"soc_back_to_mains", true}};
    if (key == "soc_back_to_mains") bounds = {{"bms_lock_machine_battery_capacity", false}, {"soc_back_to_battery", true}};
    if (key == "soc_back_to_battery") bounds = {{"soc_back_to_mains", false}};
    if (key == "second_output_cutoff_soc") bounds = {{"restore_second_output_battery_capacity", true}};
    if (key == "restore_second_output_battery_capacity") bounds = {{"second_output_cutoff_soc", false}};
    for (const auto &bound : bounds) {
      const auto other = reported(context, bound.other);
      if (!other.has_value()) return Result::refuse(std::string(bound.other) + " not read from the inverter yet");
      if (bound.must_be_below ? value >= *other : value <= *other) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "must be %s %s (%g)", bound.must_be_below ? "below" : "above", bound.other, *other);
        return Result::refuse(buf);
      }
    }
    return Result::ok(format_int((std::string(entry.prefix) + "%03u").c_str(), whole(value)), entry.label);
  }

  // Hour windows, vendor format ^S???ACCT<HH>-<HH> / ^S???ACLT<HH>-<HH>. Both hours go in one frame, so the
  // other one comes from the last readback (HEEP2[11]/[12], layout tentative until LCD experiment E1).
  struct Window {
    const char *start_key;
    const char *stop_key;
    const char *prefix;
    const char *label;
  };
  static const Window windows[] = {
      {"ac_charge_start_hour", "ac_charge_stop_hour", "^S???ACCT", "AC charge window (programs 46/47)"},
      {"ac_output_start_hour", "ac_output_stop_hour", "^S???ACLT", "AC output window (programs 48/49)"},
  };
  for (const auto &window : windows) {
    const bool is_start = key == window.start_key;
    if (!is_start && key != window.stop_key) continue;
    if (context.writes_in_flight) return Result::refuse("another write is still in flight");
    if (!on_grid(value, 0.0f, 24.0f, 1.0f)) return Result::refuse(range_reason(0.0f, 24.0f, 1.0f));
    const char *other_key = is_start ? window.stop_key : window.start_key;
    const auto other = reported(context, other_key);
    if (!other.has_value()) return Result::refuse(std::string(other_key) + " not read from the inverter yet");
    if (!on_grid(*other, 0.0f, 24.0f, 1.0f)) return Result::refuse(std::string(other_key) + " reads an invalid hour");
    const unsigned start = is_start ? whole(value) : whole(*other);
    const unsigned stop = is_start ? whole(*other) : whole(value);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%s%02u-%02u", window.prefix, start, stop);
    return Result::ok(buf, window.label);
  }
  return Result::refuse("unknown number");
}

}  // namespace writes
}  // namespace solarplug
}  // namespace esphome
