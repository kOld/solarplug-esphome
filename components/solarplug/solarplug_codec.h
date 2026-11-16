#pragma once

// Pure Solar Plug / HPVINV02 protocol codec (no ESPHome dependency). Host-tested by tests/cpp.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace esphome {
namespace solarplug {
namespace codec {

struct DecodedField {
  std::string name;
  std::string value;
  const char *unit;        // string literal, "" when none (debug logging only)
  const char *confidence;  // string literal (debug logging only)
};
using FieldList = std::vector<DecodedField>;

// Token helpers. split_tokens drops a leading '(' and splits on whitespace.
std::optional<float> parse_optional_float(const std::string &value);  // whole token must be numeric
size_t min_tokens_for(const std::string &command);

// Reply length per command, learned at runtime. Each H reply has a fixed token count on a given firmware,
// and neighbours in the poll order differ, so holding a command to its own length rejects a late reply
// credited to the next command. The length is learned, not hard-coded: it is adopted after two identical
// replies (the first one is accepted on the minimum alone), and a different length is dropped once and
// re-adopted only if the next reply repeats it, so a firmware with an extra token costs one frame.
class ReplyLengthGuard {
 public:
  bool accept(const std::string &command, size_t tokens);

 private:
  struct Entry {
    std::string command;
    uint8_t learned{0};    // 0 = not learned yet
    uint8_t candidate{0};  // last length seen that differs from learned
  };
  std::vector<Entry> entries_;
};
size_t count_tokens_from(const std::string &payload, size_t pos);
std::vector<std::string> split_tokens(const std::string &payload);
std::string join_tokens(const std::vector<std::string> &tokens, size_t start);
bool is_digits(const std::string &value);
int parse_digits(const std::string &value, size_t start, size_t len);
const char *battery_type_label(int code);  // PBT enum: 0 AGM .. 4 PYL .. 9 FEL
std::string fault_code_text(const std::string &code);  // manual 5.10.1 / 5.10.3

// Text states must be printable ASCII: any other byte makes HA's API client drop the connection.
bool is_printable_ascii(const std::string &value);
std::string printable_ascii(const std::string &value);

// Classification of one complete reply (CRC already checked). expects_ack: a setter or the CRC keep-alive
// is active. ACK / NAK need a valid CRC; an ACK during a read is a late reply to an earlier write; a write
// answered by anything else is not proof either way. HOSTCRCEN / HOSTCRCDN are answered "(CRC16EN" /
// "(CRC16DN" instead of "(ACK".
enum class ReplyClass : uint8_t { DATA, ACK, NAK, INVALID };
ReplyClass classify_reply(const std::string &command, const std::string &payload, bool crc_present, bool expects_ack);

// Field decode of one reply payload (the leading '(' included or not).
FieldList decode_fields(const std::string &command, const std::string &payload);

// CRC16/XMODEM with the H-family byte adjustment, high byte first on the wire.
uint16_t crc16_xmodem(const uint8_t *data, size_t len);
std::pair<uint8_t, uint8_t> adjust_crc_bytes(uint16_t crc);

// Charge-current selects (MNCHGC / MUCHGC). The options are the inverter's announced lists (QMCHGCR /
// QMUCHGCR) inside a static envelope for this model, so a reply can only ever narrow what is offered:
// maximum 10-80 A in 10 A steps; mains 2 A or 10-60 A in 10 A steps (HPVINV02 fw 40.05).
bool charge_current_in_envelope(bool mains, int amps);
std::string amps_option(int amps);                            // 80 -> "80 A"
std::optional<int> parse_amps_option(const std::string &option);  // "80 A" -> 80

// Settings readback: what a snapshot reply says about each write control.
enum class ReadingKind { SWITCH, SELECT, NUMBER };
struct Reading {
  std::string key;
  ReadingKind kind;
  std::string text;  // "on"/"off" for switches, the option for selects
  float number;      // numbers
  bool flag;         // switches
};
std::vector<Reading> heep1_readback(const std::vector<std::string> &tokens);
std::vector<Reading> heep2_readback(const std::vector<std::string> &tokens);
std::vector<Reading> qpiri_readback(const std::vector<std::string> &tokens);
std::vector<Reading> qflag_readback(const std::vector<std::string> &tokens);
std::vector<Reading> qbeqi_readback(const std::vector<std::string> &tokens);
std::vector<Reading> settings_readback(const std::string &command, const std::string &payload);

// Response-CRC mode (HOSTCRCEN). The inverter answers "(CRC16EN" and then appends a CRC to every H reply
// for about 64 s. The tracker follows the mode from the replies themselves, so an ESP reboot or a lapsed
// mode never lets CRC bytes into decoded values:
//  - mode off, reply with a valid trailing CRC: ambiguous (1 in 65536 plain frames match by chance), so it
//    is dropped; SWITCH_AFTER in a row mean the mode is on.
//  - mode on, reply without CRC: dropped and a refresh is wanted; SWITCH_AFTER in a row mean the inverter
//    no longer honours the mode, and plain parsing resumes from the next reply (the one that tips the
//    switch is still unverified, so it is dropped too).
class ResponseCrcTracker {
 public:
  static constexpr uint8_t SWITCH_AFTER = 3;
  struct Result {
    bool decode;        // false: drop this frame
    bool mode_changed;  // on() flipped on this reply
  };
  Result on_h_reply(bool crc_present);
  bool set_mode(bool on);  // after an acknowledged HOSTCRCEN / HOSTCRCDN; true if the mode changed
  bool on() const { return this->on_; }
  bool refresh_wanted() const { return this->refresh_wanted_; }
  void refresh_sent() { this->refresh_wanted_ = false; }

 private:
  bool on_{false};
  bool refresh_wanted_{false};
  uint8_t crc_streak_{0};
  uint8_t plain_streak_{0};
};

// Keep-alive cadence: re-send HOSTCRCEN every CRC_KEEPALIVE_MS (the mode lapses ~64 s after each one, so
// one lost keep-alive still leaves margin), or sooner when a refresh is wanted, but never more often than
// CRC_MIN_REFRESH_SPACING_MS. After CRC_BACKOFF_AFTER unanswered keep-alives in a row the firmware does not
// support the mode: back off to CRC_BACKOFF_MS and ignore refresh requests.
constexpr uint32_t CRC_KEEPALIVE_MS = 30000;
constexpr uint32_t CRC_MIN_REFRESH_SPACING_MS = 5000;
constexpr uint8_t CRC_BACKOFF_AFTER = 3;
constexpr uint32_t CRC_BACKOFF_MS = 600000;
bool crc_keepalive_due(bool sent_before, uint32_t ms_since_last, bool refresh_wanted, uint8_t failures);

}  // namespace codec
}  // namespace solarplug
}  // namespace esphome
