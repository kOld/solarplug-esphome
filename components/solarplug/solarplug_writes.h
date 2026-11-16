#pragma once

// Pure write encoders for the HPVINV02 setters (no ESPHome dependency; host-tested by tests/cpp).
// Each encoder validates a request and returns the frame to send, or a refusal with the reason. Every
// frame is sent with CRC16/XMODEM (codec::crc16_xmodem); the component only queues and logs.
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
};

Result encode_switch(const std::string &key, bool on);
Result encode_select(const std::string &key, const std::string &option, const Context &context);
Result encode_number(const std::string &key, float value, const Context &context);

}  // namespace writes
}  // namespace solarplug
}  // namespace esphome
