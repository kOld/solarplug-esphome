#pragma once

// Pure write encoders for the HPVINV02 setters (no ESPHome dependency; host-tested by tests/cpp).
// Each encoder validates a request and returns the frame to send, or a refusal with the reason. Every
// frame is sent with CRC16/XMODEM (codec::crc16_xmodem); the component only queues and logs.
#include "solarplug_codec.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace esphome {
namespace solarplug {
namespace writes {

struct Frame {
  std::string command;  // ASCII part; the CRC and CR are added on the wire
  const char *label;    // for the log
};

struct Result {
  std::optional<Frame> frame;  // set when the request is accepted
  std::string reason;          // why it was refused
  static Result ok(std::string command, const char *label) { return {Frame{std::move(command), label}, ""}; }
  static Result refuse(std::string why) { return {std::nullopt, std::move(why)}; }
};

// What ordered and paired settings are checked against.
struct Context {
  const std::map<std::string, float> *reported{nullptr};  // last values read back (or acknowledged)
  const std::vector<int> *allowed_max_charge_a{nullptr};    // QMCHGCR, empty until announced
  const std::vector<int> *allowed_mains_charge_a{nullptr};  // QMUCHGCR, empty until announced
  bool writes_in_flight{false};                             // another write is queued or active
  // Priority codes differ by machine type (codec::MachineType); nullopt = not detected yet (priority
  // writes are refused).
  std::optional<codec::MachineType> machine_type{codec::MachineType::B};
  bool machine_type_conflict{false};  // a YAML override whose codes differ from the QPRTL-detected type
  // Battery bank: voltage settings are the 48 V ranges scaled by battery_voltage / 48 (24 V halves them).
  // detected_battery_voltage comes from HBAT[0] (12 V blocks x 12); 0 = not read yet. Voltage settings are
  // refused until it is read, and on a mismatch.
  int battery_voltage{48};
  int detected_battery_voltage{0};
  // Charge-current envelopes (the configured option lists); empty = the HPVINV02 defaults.
  const std::vector<int> *max_charge_envelope{nullptr};
  const std::vector<int> *mains_charge_envelope{nullptr};
};

// The redischarge (program 13) select options for a battery voltage: "Full", then 48-58 V in 1 V steps
// scaled by battery_voltage / 48 ("24 V", "24.5 V" ... "29 V" on 24 V).
std::vector<std::string> redischarge_options(int battery_voltage);

Result encode_switch(const std::string &key, bool on);
Result encode_select(const std::string &key, const std::string &option, const Context &context);
Result encode_number(const std::string &key, float value, const Context &context);

}  // namespace writes
}  // namespace solarplug
}  // namespace esphome
