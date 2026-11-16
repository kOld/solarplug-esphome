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
// Envelope check with a configured option list (empty or null: the HPVINV02 defaults above).
bool charge_current_allowed(const std::vector<int> *envelope, bool mains, int amps);
// "54 V" / "24.5 V": the text of a voltage option, shared by the select options and the readback.
std::string volts_option(float volts);
std::string amps_option(int amps);                            // 80 -> "80 A"
std::optional<int> parse_amps_option(const std::string &option);  // "80 A" -> 80

// Machine type, as the vendor tool (TQF-AllInOne, SerialCommunicationService.MachineType) derives it from
// QPRTL. It decides the output- and charger-priority codes; everything else is shared:
//   A: HPVINV01   B: HPVINV02, HPVINV05   C: HPVINV03   D: HPVINV04   F: HPVINV08
// Any other reply (sibling brands, HPVINV06/07) is UNKNOWN and uses the type B codes, which is what every
// earlier release did. Only type B is verified on hardware (PowMr POW-HVM6.2KP, fw 40.05).
enum class MachineType : uint8_t { UNKNOWN, A, B, C, D, F };
MachineType machine_type_from_qprtl(const std::string &device_type);
const char *machine_type_name(MachineType type);
// Priority option <-> code for a machine type; nullptr / nullopt where the type has no such option.
//   output:  B/D/UNKNOWN 0 SUB, 1 SBU, 3 PEC    A/C 0 UTI, 1 SUB, 2 SBU    F 0 SUB, 1 SBU, 3 PEC
//   charger: B/D/UNKNOWN/F 0 CSO, 1 SNU, 2 OSO, 3 SOR    A/C 1 CSO, 2 SNU, 3 OSO
// Never offered: POP02 on B/D/F (SUF, feeds the grid) and charger code 0 on A/C (CUT).
// true when two types share the priority code tables (an override must not contradict QPRTL).
bool same_priority_codes(MachineType a, MachineType b);
std::optional<int> output_priority_code(MachineType type, const std::string &option);
const char *output_priority_option(MachineType type, int code);
std::optional<int> charger_priority_code(MachineType type, const std::string &option);
const char *charger_priority_option(MachineType type, int code);

// HEEP1 in the HPVINV02 layout (20 tokens). Two sibling layouts from the vendor tool are mapped onto it:
//   21 tokens: the generic GB3024 layout (HPVINV06; HPVINV02 before 2025-09) has an extra token at [4];
//   19 tokens with a 5-digit [4]: LPVINV02 (VQ3024) packs [4] d0-2 plus the buzzer in d4.
// Anything else returns nullopt, so readback is skipped rather than read from the wrong slots.
std::optional<std::vector<std::string>> normalize_heep1(const std::vector<std::string> &tokens);

// Settings readback: what a snapshot reply says about each write control.
enum class ReadingKind { SWITCH, SELECT, NUMBER };
struct Reading {
  std::string key;
  ReadingKind kind;
  std::string text;  // "on"/"off" for switches, the option for selects
  float number;      // numbers
  bool flag;         // switches
};
// machine: the priority code table to use; nullopt (type not known yet) skips the priority readings.
std::vector<Reading> heep1_readback(const std::vector<std::string> &tokens,
                                    std::optional<MachineType> machine = MachineType::B);
std::vector<Reading> heep2_readback(const std::vector<std::string> &tokens);
// QPIRI's priority fields are mapped only for the verified type B codes (and UNKNOWN, which uses them).
std::vector<Reading> qpiri_readback(const std::vector<std::string> &tokens,
                                    std::optional<MachineType> machine = MachineType::B);
std::vector<Reading> qflag_readback(const std::vector<std::string> &tokens);
std::vector<Reading> qbeqi_readback(const std::vector<std::string> &tokens);
std::vector<Reading> settings_readback(const std::string &command, const std::string &payload,
                                       std::optional<MachineType> machine = MachineType::B);

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
