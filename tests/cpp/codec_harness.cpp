// Host test harness for components/solarplug/solarplug_codec.cpp (the code that runs on the ESP).
//
//   codec_harness selftest   -> fixed vectors; exit status 0 = pass
//   codec_harness dump        -> reads "COMMAND<TAB>PAYLOAD" lines on stdin and prints
//                                F<TAB>cmd<TAB>field<TAB>value   (decoded fields)
//                                R<TAB>cmd<TAB>key<TAB>value     (settings readback)
//                                for frames that pass the firmware's minimum-token gate.
// tests/test_decode_h_protocol.py builds it with the host compiler and compares the dump against the
// Python reference decoder over tests/fixtures/payload_corpus.tsv.
#include "solarplug_codec.h"
#include "solarplug_writes.h"

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace esphome::solarplug::codec;

namespace {

int failures = 0;

void check(bool ok, const std::string &what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what.c_str());
    failures++;
  }
}

std::string field(const std::string &command, const std::string &payload, const std::string &name) {
  for (const auto &f : decode_fields(command, payload)) {
    if (f.name == name) return f.value;
  }
  return "<missing>";
}

std::map<std::string, std::string> readback(const std::string &command, const std::string &payload) {
  std::map<std::string, std::string> out;
  for (const auto &r : settings_readback(command, payload)) {
    if (r.kind == ReadingKind::NUMBER) {
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%g", r.number);
      out[r.key] = buf;
    } else {
      out[r.key] = r.text;
    }
  }
  return out;
}

std::string crc_hex(const std::string &command) {
  auto crc = crc16_xmodem(reinterpret_cast<const uint8_t *>(command.data()), command.size());
  auto [hi, lo] = adjust_crc_bytes(crc);
  char buf[8];
  std::snprintf(buf, sizeof(buf), "%02X %02X", hi, lo);
  return buf;
}

int selftest() {
  // CRC vectors captured on the bus / from the vendor tool.
  check(crc_hex("PBT04") == "67 8A", "crc PBT04");
  check(crc_hex("BMSC00") == "48 9E", "crc BMSC00");
  check(crc_hex("BMSSRC050") == "18 0C", "crc BMSSRC050");
  check(crc_hex("PDDCGT0000") == "3D 86", "crc PDDCGT0000");
  check(crc_hex("(ACK") == "39 20", "crc (ACK");
  check(crc_hex("(NAK") == "73 73", "crc (NAK");
  check(crc_hex("PCP00") == "8D 7A", "crc PCP00");

  // Strict numeric parsing (H replies have no CRC; line noise must not become a number).
  check(!parse_optional_float("05x.3").has_value(), "strict float rejects 05x.3");
  check(!parse_optional_float("").has_value(), "strict float rejects empty");
  check(parse_optional_float("056.4").value_or(0) == 56.4f, "float 056.4");
  check(split_tokens("(1 080 030").size() == 3, "split drops '('");

  // HTEMP order (portal snapshot 2026-04-29: PV 20, inverter 28, boost 21, transformer 26, max 28, fans 30).
  const std::string htemp = "(020 028 021 026 028 030 030 11000000000000000000";
  check(field("HTEMP", htemp, "pv_temperature_c") == "020", "htemp pv");
  check(field("HTEMP", htemp, "inverter_temperature_c") == "028", "htemp inverter");
  check(field("HTEMP", htemp, "max_temperature_c") == "028", "htemp max");
  check(field("HTEMP", htemp, "fan_2_speed_percent") == "030", "htemp fan2");
  check(field("HTEMP", htemp, "fan_enable") == "On", "htemp fan enable");

  // HSTS status, alarms and LEDs.
  const std::string hsts = "(00 B010000000000 10211002120B117200000";
  check(field("HSTS", hsts, "mode_label") == "Battery Mode", "hsts mode");
  check(field("HSTS", hsts, "alarms") == "None", "hsts no alarms");
  check(field("HSTS", hsts, "front_panel_leds") == "AC flashing, inverter on, charge flashing, alarm off", "hsts leds");
  check(field("HSTS", hsts, "grid_tie_state") == "Off-grid", "hsts grid tie");
  check(field("HSTS", hsts, "fault_description") == "None", "hsts fault none");
  check(field("HSTS", hsts, "parallel_units") == "1" && field("HSTS", hsts, "parallel_role") == "2", "hsts parallel");
  check(field("HSTS", "(61 B010000000000 10211002120B117200000", "fault_description") == "BMS communication lost",
        "hsts fault 61");
  check(field("HSTS", "(44 B010000000000 10211002120B117200000", "fault_description") == "Code 44 (not in manual 5.10)",
        "hsts fault unknown");
  check(field("HSTS", "(05 B011010000001 10211102120B117200000", "alarms") ==
            "Battery low, Overload, Fan fault, BMS communication fault",
        "hsts alarms");

  // HBMS1 deci-kelvin temperature and flags.
  const std::string hbms1 = "(02 1001100000000000 044.8 057.6 150.0 028 0001.6 0000.0 02929 000000";
  check(field("HBMS1", hbms1, "bms_temperature_c") == "19.75", "hbms1 temperature");
  check(field("HBMS1", hbms1, "bms_flags_decoded") == "Communication OK, Charge allowed, Discharge allowed",
        "hbms1 flags");

  // HBAT[0] is the series count, not the battery type.
  check(field("HBAT", "(04 052.6 028 002 00000 388 101002010000 00000000", "battery_series_count") == "04",
        "hbat series count");
  check(field("HBAT", "(04 052.6 028 002 00000 388 101002010000 00000000", "battery_type") == "<missing>",
        "hbat has no battery type");

  // HEEP1 readback (live 2026-11-16) and its cross-check-only slots.
  auto heep1 = readback("HEEP1", "(1 080 030 03311010230 012 1 1 0 0 1 020 025 040 050 056.4 056.4 042.0 020 0 0");
  check(heep1["battery_type"] == "LIA", "heep1 battery type");
  check(heep1["output_source_priority"] == "SBU priority", "heep1 output");
  check(heep1["charger_priority"] == "OSO", "heep1 charger");
  check(heep1["overload_restart"] == "on", "heep1 overload restart");
  check(heep1["input_source_detection_prompt_sound"] == "off", "heep1 prompt");
  check(heep1["soc_back_to_mains"] == "25", "heep1 soc back to mains");
  check(heep1["grid_connected_current"] == "20", "heep1 grid current");
  check(heep1["maximum_charging_current"] == "80 A" && heep1["maximum_mains_charging_current"] == "30 A",
        "heep1 charge currents as select options");
  // Unsafe-profile controls seeded from the same HEEP1 frame.
  check(heep1["grid_protocol"] == "Mode 4", "heep1 grid protocol 1-based");
  check(heep1["ct_function"] == "off", "heep1 ct function");
  check(heep1["output_voltage"] == "230 V", "heep1 output voltage");
  check(heep1["output_frequency"] == "50 Hz", "heep1 output frequency");
  check(heep1["output_mode"] == "SIG", "heep1 output mode");
  check(heep1["grid_feed"] == "off", "heep1 grid feed");
  check(readback("HEEP1", "(x 080 030 0341x110230 0x2").count("battery_type") == 0, "heep1 garbled packed token");
  check(readback("HEEP1", "(7 080").count("output_source_priority") == 0, "heep1 unknown output code skipped");

  // HEEP2: restore SOC and discharge time share token 16.
  auto heep2 = readback("HEEP2", "(0 044.0 020 044.0 046.0 054.0 0 058.4 060 120 030 0000 0000 10 0000 52.0 30990");
  check(heep2["restore_second_output_battery_capacity"] == "30", "heep2 restore soc");
  check(heep2["second_output_discharge_time"] == "990", "heep2 discharge time");
  check(heep2["battery_redischarge_voltage"] == "54 V", "heep2 redischarge as select option");
  check(readback("HEEP2", "(0 044.0 020 044.0 046.0 000.0 0 058.4 060 120 030 0000 0000 10 0000 52.0 30000")["battery_redischarge_voltage"] ==
            "Full",
        "heep2 redischarge 0 = Full");
  check(heep2["dual_output_mode"] == "off", "heep2 dual output mode");
  check(heep2["ac_charge_start_hour"] == "0" && heep2["ac_charge_stop_hour"] == "0", "heep2 charge window");
  auto window = readback("HEEP2", "(1 044.0 020 044.0 046.0 054.0 0 058.4 060 120 030 0207 2306 10 0000 52.0 30000");
  check(window["dual_output_mode"] == "on", "heep2 dual output on");
  check(window["ac_charge_start_hour"] == "2" && window["ac_charge_stop_hour"] == "7", "heep2 window layout SSEE");
  check(window["ac_output_start_hour"] == "23" && window["ac_output_stop_hour"] == "6", "heep2 output window");
  check(readback("HEEP2", "(0 044.0 020 044.0 046.0 054.0 0 058.4 060 120 030 02x7 000 10").count("ac_charge_start_hour") == 0,
        "heep2 garbled window skipped");
  const std::string heep2_live = "(0 044.0 020 044.0 046.0 054.0 0 058.4 060 120 030 0000 0000 10 0000 52.0 30000";
  check(field("HEEP2", heep2_live, "ac_output_window_raw") == "0000", "heep2 output window raw");
  check(field("HEEP2", heep2_live, "dual_output_mode") == "Off", "heep2 dual output text");

  // QPIRI never maps code 2 (SUF on sibling firmwares).
  check(readback("QPIRI", "(230.0 26.9 230.0 50.0 26.9 6200 6200 48.0 46.0 44.8 56.4 56.4 3 30 080 0 2 2 9 01 0 0 54.0 1 1")
                .count("output_source_priority") == 0,
        "qpiri output code 2 not mapped");
  auto qpiri = readback("QPIRI", "(230.0 26.9 230.0 50.0 26.9 6200 6200 48.0 46.0 44.8 56.4 56.4 3 30 080 0 1 2 9 01 0 0 54.0 1 1");
  check(qpiri["output_voltage"] == "230 V" && qpiri["output_frequency"] == "50 Hz" && qpiri["output_mode"] == "SIG",
        "qpiri output cross-check readings");
  check(qpiri["battery_redischarge_voltage"] == "54 V", "qpiri redischarge cross-check");
  check(qpiri["maximum_charging_current"] == "80 A" && qpiri["maximum_mains_charging_current"] == "30 A",
        "qpiri charge-current cross-check readings");
  check(amps_option(2) == "2 A" && parse_amps_option("60 A").value_or(0) == 60, "amps option round trip");
  check(!parse_amps_option("60").has_value() && !parse_amps_option("6x A").has_value() && !parse_amps_option("A").has_value(),
        "amps option rejects malformed text");
  check(charge_current_in_envelope(true, 2) && charge_current_in_envelope(true, 60) && !charge_current_in_envelope(true, 4) &&
            !charge_current_in_envelope(true, 70),
        "mains charge envelope: 2 or 10-60 step 10");
  check(charge_current_in_envelope(false, 80) && !charge_current_in_envelope(false, 2) && !charge_current_in_envelope(false, 90) &&
            !charge_current_in_envelope(false, 25),
        "max charge envelope: 10-80 step 10");
  auto qflag = readback("QFLAG", "(EabkuvxDjyz");
  check(qflag["overload_bypass"] == "on" && qflag["power_saving"] == "off", "qflag letters");
  check(readback("QFLAG", "(NAK").empty(), "qflag ignores NAK");

  // HEEP3 answers on HPVINV02 (probe 2026-11-16).
  check(field("HEEP3", "(2048 2048 2048 0150 0000000000000000000000000000000000000000000000000000000000",
              "ct_zero_adjust_power_w") == "0150",
        "heep3 ct zero adjust");

  // Response-CRC mode tracker.
  {
    ResponseCrcTracker t;
    check(t.on_h_reply(false).decode && !t.on(), "crc: plain while off decodes");
    check(!t.on_h_reply(true).decode && !t.on_h_reply(true).decode, "crc: first CRC frames while off are dropped");
    auto third = t.on_h_reply(true);
    check(third.decode && third.mode_changed && t.on(), "crc: 3rd CRC frame turns the mode on");
    check(t.on_h_reply(true).decode && !t.refresh_wanted(), "crc: CRC frames decode while on");
    check(!t.on_h_reply(false).decode && t.refresh_wanted() && t.on(), "crc: plain while on is dropped, refresh wanted");
    check(t.on_h_reply(true).decode, "crc: one plain frame does not end the mode");
    check(!t.on_h_reply(false).decode && !t.on_h_reply(false).decode, "crc: lapse drops 2 frames");
    auto lapse = t.on_h_reply(false);
    check(!lapse.decode && lapse.mode_changed && !t.on(), "crc: 3rd plain frame falls back, but is not decoded");
    check(t.on_h_reply(false).decode, "crc: plain parsing resumes after the fallback");
    check(t.refresh_wanted(), "crc: refresh still wanted after falling back");
    check(t.set_mode(true) && t.on() && !t.refresh_wanted(), "crc: acknowledged HOSTCRCEN turns it on");
    check(!t.set_mode(true), "crc: repeated ack is not a change");
    t.on_h_reply(false);
    t.on_h_reply(true);  // a CRC frame resets the plain streak
    check(!t.on_h_reply(false).decode && !t.on_h_reply(false).decode && t.on(), "crc: plain streak resets on CRC frame");
    check(t.set_mode(false) && !t.on(), "crc: acknowledged HOSTCRCDN turns it off");
    t.on_h_reply(true);
    t.on_h_reply(false);  // a plain frame resets the CRC streak
    check(!t.on_h_reply(true).decode && !t.on_h_reply(true).decode && !t.on(), "crc: CRC streak resets on plain frame");
  }
  check(crc_keepalive_due(false, 0, false, 0), "keepalive: first one is due at once");
  check(!crc_keepalive_due(true, 29999, false, 0) && crc_keepalive_due(true, 30000, false, 0), "keepalive: 30 s cadence");
  check(!crc_keepalive_due(true, 4999, true, 0) && crc_keepalive_due(true, 5000, true, 0), "keepalive: refresh spacing 5 s");
  check(crc_keepalive_due(true, 0xFFFFFFF0u, false, 0), "keepalive: large elapsed time is due");
  check(!crc_keepalive_due(true, 30000, true, 3) && crc_keepalive_due(true, 600000, false, 3),
        "keepalive: backoff to 10 min after 3 failures, refresh ignored");
  check(crc_keepalive_due(true, 30000, false, 2), "keepalive: no backoff before 3 failures");

  // Reply classification.
  check(classify_reply("PBT04", "(ACK", true, true) == ReplyClass::ACK, "classify: ACK to a setter");
  check(classify_reply("PBT04", "(ACK", false, true) == ReplyClass::INVALID, "classify: ACK without CRC is noise");
  check(classify_reply("HSTS", "(ACK", true, false) == ReplyClass::INVALID, "classify: late ACK during a read");
  check(classify_reply("PSLV44.0", "(NAK", true, true) == ReplyClass::NAK, "classify: NAK to a setter");
  check(classify_reply("^S???RS03", "^1", true, true) == ReplyClass::ACK, "classify: ^1");
  check(classify_reply("^S???RS03", "^0", true, true) == ReplyClass::NAK, "classify: ^0");
  check(classify_reply("HOSTCRCEN", "(CRC16EN", true, true) == ReplyClass::ACK, "classify: (CRC16EN");
  check(classify_reply("HOSTCRCDN", "(CRC16DN", true, true) == ReplyClass::ACK, "classify: (CRC16DN");
  check(classify_reply("HOSTCRCEN", "(CRC16DN", true, true) == ReplyClass::INVALID, "classify: mismatched CRC16 reply");
  check(classify_reply("HSTS", "(CRC16EN", true, false) == ReplyClass::DATA, "classify: CRC16 text during a read is data");
  check(classify_reply("PBT04", "(00 B010000000000 1021", false, true) == ReplyClass::INVALID,
        "classify: data reply to a setter");
  check(classify_reply("HSTS", "(00 B010000000000 1021", false, false) == ReplyClass::DATA, "classify: data reply");

  // Text sanitizer and token windows.
  check(printable_ascii(std::string("HPVINV02\xfb\xd3")) == "HPVINV02??", "sanitize: CRC bytes replaced");
  check(printable_ascii("OK 1.0") == "OK 1.0", "sanitize: printable unchanged");
  check(is_printable_ascii("(00 B01") && !is_printable_ascii(std::string("(00\x7f")) && !is_printable_ascii("a\r"),
        "printable check");
  {
    ReplyLengthGuard g;
    check(!g.accept("HEEP2", 13), "length: below the minimum is never accepted");
    check(g.accept("HEEP2", 17) && g.accept("HEEP2", 17), "length: learned after two identical replies");
    check(!g.accept("HEEP2", 20), "length: a HEEP1-sized reply in the HEEP2 slot is dropped");
    check(g.accept("HEEP2", 17), "length: the learned length still passes after a mismatch");
    check(!g.accept("HEEP2", 18) && g.accept("HEEP2", 18), "length: a new length is adopted when it repeats");
    check(!g.accept("HEEP2", 17), "length: the old length is now the odd one");
    check(g.accept("HIMSG2", 5) && g.accept("HIMSG2", 3) && g.accept("HIMSG2", 3) && !g.accept("HIMSG2", 5),
          "length: while learning, any length above the minimum passes");
    check(g.accept("HBMS3", 17) && !g.accept("HBMS3", 16), "length: per-command minimum still applies");
  }
  check(classify_reply("HSTS", "(NAK", true, false) == ReplyClass::INVALID, "classify: late NAK during a read");

  // ---- Write encoders: golden frames and refusals ----
  {
    namespace w = esphome::solarplug::writes;
    auto cmd = [](const w::Result &r) { return r.frame.has_value() ? r.frame->command : "REFUSED: " + r.reason; };
    auto refused = [](const w::Result &r) { return !r.frame.has_value() && !r.reason.empty(); };
    std::map<std::string, float> reported = {{"bms_lock_machine_battery_capacity", 20}, {"soc_back_to_mains", 25},
                                             {"soc_back_to_battery", 40}, {"second_output_cutoff_soc", 20},
                                             {"restore_second_output_battery_capacity", 30},
                                             {"ac_charge_start_hour", 0}, {"ac_charge_stop_hour", 0}};
    std::vector<int> max_list = {10, 20, 30, 40, 50, 60, 70, 80};
    std::vector<int> mains_list = {2, 10, 20, 30, 40, 50, 60};
    w::Context ctx;
    ctx.reported = &reported;
    ctx.allowed_max_charge_a = &max_list;
    ctx.allowed_mains_charge_a = &mains_list;
    ctx.detected_battery_voltage = 48;  // HBAT read: a 48 V bank

    // Switches: the bytes that used to be hard-coded with their CRC are the CRC of these commands.
    check(cmd(w::encode_switch("backlight", true)) == "PEx" && crc_hex("PEx") == "53 68", "switch backlight on (PExSh)");
    check(cmd(w::encode_switch("backlight", false)) == "PDx" && crc_hex("PDx") == "60 59", "switch backlight off (PDx`Y)");
    check(cmd(w::encode_switch("display_return_homepage", true)) == "PEk" && crc_hex("PEk") == "71 3A", "PEkq:");
    check(cmd(w::encode_switch("input_source_detection_prompt_sound", true)) == "PEy" && crc_hex("PEy") == "43 49", "PEyCI");
    check(cmd(w::encode_switch("input_source_detection_prompt_sound", false)) == "PDy" && crc_hex("PDy") == "70 78", "PDypx");
    check(cmd(w::encode_switch("battery_equalization_mode_enable", false)) == "PBEQE0" && crc_hex("PBEQE0") == "5A 32", "PBEQE0Z2");
    check(cmd(w::encode_switch("buzzer", false)) == "PDa" && cmd(w::encode_switch("overload_bypass", true)) == "PEb",
          "switch PE/PD flags");
    check(cmd(w::encode_switch("bms_function_enable", true)) == "BMSC01", "switch BMSC");
    check(cmd(w::encode_switch("second_output_power", false)) == "PDAULON00" && cmd(w::encode_switch("grid_feed", true)) == "GRIDFEED01",
          "switch unsafe-profile frames");
    check(refused(w::encode_switch("nope", true)), "switch unknown key refused");

    // Selects.
    check(cmd(w::encode_select("charger_priority", "OSO", ctx)) == "PCP02", "select charger OSO");
    check(cmd(w::encode_select("output_source_priority", "SBU priority", ctx)) == "POP01", "select output SBU");
    check(refused(w::encode_select("output_source_priority", "SUF", ctx)), "POP02 (SUF) is never encodable");
    check(cmd(w::encode_select("battery_type", "PYL", ctx)) == "PBT04" && cmd(w::encode_select("battery_type", "AGM", ctx)) == "PBT00",
          "select battery type enum");
    check(cmd(w::encode_select("grid_protocol", "Mode 4", ctx)) == "^S???RS03", "select grid protocol 1-based");
    check(cmd(w::encode_select("output_voltage", "230 V", ctx)) == "V230" && cmd(w::encode_select("output_mode", "SIG", ctx)) == "POPM00",
          "select output voltage / mode");
    check(cmd(w::encode_select("maximum_charging_current", "80 A", ctx)) == "MNCHGC080", "select max charge 80 A");
    check(cmd(w::encode_select("maximum_mains_charging_current", "2 A", ctx)) == "MUCHGC002", "select mains charge 2 A");
    check(refused(w::encode_select("maximum_charging_current", "90 A", ctx)), "max charge outside envelope");
    std::vector<int> narrowed = {10, 20, 30};
    w::Context narrow = ctx;
    narrow.allowed_max_charge_a = &narrowed;
    check(refused(w::encode_select("maximum_charging_current", "80 A", narrow)), "max charge not on the announced list");
    std::vector<int> empty;
    w::Context unannounced = ctx;
    unannounced.allowed_max_charge_a = &empty;
    check(cmd(w::encode_select("maximum_charging_current", "70 A", unannounced)) == "MNCHGC070", "envelope only before announcement");

    // Numbers: formats.
    check(cmd(w::encode_number("battery_equalization_voltage", 58.4f, ctx)) == "PBEQV58.40", "PBEQV 2 decimals");
    check(cmd(w::encode_number("battery_cut_off_voltage", 42.0f, ctx)) == "PSDV42.0", "PSDV");
    check(cmd(w::encode_number("battery_recharge_voltage", 46.0f, ctx)) == "PBCV46.0", "PBCV");
    check(refused(w::encode_number("battery_recharge_voltage", 46.5f, ctx)), "PBCV 1 V steps");
    check(cmd(w::encode_select("battery_redischarge_voltage", "Full", ctx)) == "PBDV00.0", "PBDV full");
    check(cmd(w::encode_select("battery_redischarge_voltage", "54 V", ctx)) == "PBDV54.0", "PBDV 54");
    check(refused(w::encode_select("battery_redischarge_voltage", "47 V", ctx)) &&
              refused(w::encode_select("battery_redischarge_voltage", "5 V", ctx)),
          "PBDV gap 1-47 refused");
    check(cmd(w::encode_number("second_output_discharge_time", 990.0f, ctx)) == "PDDCGT0990", "PDDCGT 4 digits");
    check(cmd(w::encode_number("second_output_discharge_time", 0.0f, ctx)) == "PDDCGT0000", "PDDCGT 0");
    check(refused(w::encode_number("second_output_discharge_time", 7.0f, ctx)), "PDDCGT 5-minute steps");
    check(cmd(w::encode_number("restore_second_output_delay_time", 10.0f, ctx)) == "PDDLYT010", "PDDLYT");
    check(refused(w::encode_number("restore_second_output_delay_time", 4.0f, ctx)), "PDDLYT004 refused (NAKs live)");
    check(cmd(w::encode_number("grid_connected_current", 20.0f, ctx)) == "PGFC020", "PGFC");
    check(cmd(w::encode_number("low_battery_alarm_voltage", 44.0f, ctx)) == "PSLV44.0", "PSLV vendor format");
    check(cmd(w::encode_number("inverter_restart_soc", 50.0f, ctx)) == "BMSSRC050", "BMSSRC");
    check(refused(w::encode_number("battery_float_voltage", std::nanf(""), ctx)), "NaN refused");
    check(refused(w::encode_number("grid_connected_current", -1.0f, ctx)), "negative refused");

    // Numbers: ordering against reported values (lock 20 < mains 25 < battery 40; cut-off 20 < restore 30).
    check(cmd(w::encode_number("soc_back_to_mains", 30.0f, ctx)) == "BMSB2UC030", "SOC back to mains in order");
    check(refused(w::encode_number("soc_back_to_mains", 40.0f, ctx)), "SOC back to mains must stay below back to battery");
    check(refused(w::encode_number("soc_back_to_mains", 20.0f, ctx)), "SOC back to mains must stay above BMS lock");
    check(refused(w::encode_number("soc_back_to_mains", 32.0f, ctx)), "SOC multiples of 5");
    check(cmd(w::encode_number("soc_back_to_battery", 45.0f, ctx)) == "BMSU2BC045", "SOC back to battery");
    check(refused(w::encode_number("soc_back_to_battery", 25.0f, ctx)), "back to battery must stay above back to mains");
    check(refused(w::encode_number("bms_lock_machine_battery_capacity", 25.0f, ctx)), "BMS lock above 20 refused");
    check(cmd(w::encode_number("second_output_cutoff_soc", 25.0f, ctx)) == "PDSDS025", "PDSDS");
    check(refused(w::encode_number("second_output_cutoff_soc", 30.0f, ctx)), "cut-off SOC must stay below restore SOC");
    check(cmd(w::encode_number("restore_second_output_battery_capacity", 35.0f, ctx)) == "PDSRS035", "PDSRS");
    check(refused(w::encode_number("restore_second_output_battery_capacity", 20.0f, ctx)), "restore SOC must stay above cut-off");
    std::map<std::string, float> partial = {{"soc_back_to_battery", 40}};
    w::Context unread = ctx;
    unread.reported = &partial;
    check(refused(w::encode_number("soc_back_to_mains", 30.0f, unread)), "fail closed when a bound is unread");
    w::Context busy = ctx;
    busy.writes_in_flight = true;
    check(refused(w::encode_number("soc_back_to_mains", 30.0f, busy)), "ordered write refused while another is in flight");
    check(cmd(w::encode_number("grid_connected_current", 20.0f, busy)) == "PGFC020", "independent write allowed while busy");

    // Hour windows: the other hour comes from the readback.
    check(cmd(w::encode_number("ac_charge_start_hour", 8.0f, ctx)) == "^S???ACCT08-00", "window start");
    check(cmd(w::encode_number("ac_charge_stop_hour", 18.0f, ctx)) == "^S???ACCT00-18", "window stop");
    check(refused(w::encode_number("ac_output_start_hour", 8.0f, ctx)), "window refused while the other hour is unread");
    check(refused(w::encode_number("ac_charge_start_hour", 25.0f, ctx)), "window hour range");
    check(refused(w::encode_number("nope", 1.0f, ctx)), "number unknown key refused");
  }

  // ---- Model profiles ----
  check(machine_type_from_qprtl("(HPVINV02") == MachineType::B && machine_type_from_qprtl("HPVINV05") == MachineType::B,
        "machine type B");
  check(machine_type_from_qprtl("HPVINV01") == MachineType::A && machine_type_from_qprtl("HPVINV03") == MachineType::C &&
            machine_type_from_qprtl("HPVINV04") == MachineType::D && machine_type_from_qprtl("HPVINV08") == MachineType::F,
        "machine types A C D F");
  check(machine_type_from_qprtl("HPVINV06") == MachineType::UNKNOWN && machine_type_from_qprtl("") == MachineType::UNKNOWN,
        "machine type unknown");
  check(output_priority_code(MachineType::B, "SBU priority").value_or(-1) == 1 &&
            output_priority_code(MachineType::A, "SBU priority").value_or(-1) == 2 &&
            output_priority_code(MachineType::A, "Utility first (UTI)").value_or(-1) == 0 &&
            !output_priority_code(MachineType::B, "Utility first (UTI)").has_value() &&
            !output_priority_code(MachineType::A, "PEC Mode (CT)").has_value(),
        "output priority codes per type");
  check(charger_priority_code(MachineType::A, "OSO").value_or(-1) == 3 &&
            charger_priority_code(MachineType::B, "OSO").value_or(-1) == 2 &&
            !charger_priority_code(MachineType::A, "SOR").has_value() && output_priority_option(MachineType::A, 0) != nullptr &&
            std::string(charger_priority_option(MachineType::C, 1)) == "CSO" && charger_priority_option(MachineType::A, 0) == nullptr,
        "charger priority codes per type");
  {
    const std::string live = "(1 080 030 03311010230 012 1 1 0 0 1 020 025 040 050 056.4 056.4 042.0 020 0 0";
    // Same settings in the generic GB3024 layout (extra [4]) and the LPVINV02 layout ([4] = d0-2 + buzzer).
    const std::string gb3024 = "(1 080 030 03311010230 7 012 1 1 0 0 1 020 025 040 050 056.4 056.4 042.0 020 0 0";
    const std::string lpv = "(1 080 030 03311010230 01201 1 0 0 1 020 025 040 050 056.4 056.4 042.0 020 0 0";
    auto a = readback("HEEP1", live), b = readback("HEEP1", gb3024), c = readback("HEEP1", lpv);
    check(a == b && a == c && a.size() > 20, "HEEP1 sibling layouts read the same settings");
    check(readback("HEEP1", "(1 080 030 03311010230 012 1 1 0 0 1 020 025 040 050 056.4 056.4 042.0 020 0 0 9 9").empty(),
          "HEEP1 unknown layout: no readback");
    check(field("HEEP1", gb3024, "token_11") == "020" && field("HEEP1", gb3024, "battery_type") == "LIA",
          "HEEP1 named fields from the normalized layout");
    auto typed = settings_readback("HEEP1", live, MachineType::A);
    bool priorities = false;
    for (const auto &r : typed) priorities |= (r.key == "output_source_priority" && r.text == "SUB priority");
    check(priorities, "type A reads HEEP1[0] = 1 as SUB");
    auto unknown_type = settings_readback("HEEP1", live, std::nullopt);
    bool any_priority = false;
    for (const auto &r : unknown_type) any_priority |= r.key == "output_source_priority" || r.key == "charger_priority";
    check(!any_priority && unknown_type.size() > 15, "type not known yet: priorities skipped, rest read");
    bool qpiri_priority = false;
    for (const auto &r : settings_readback("QPIRI", "(230.0 26.9 230.0 50.0 26.9 6200 6200 48.0 46.0 44.8 56.4 56.4 3 30 080 0 1 2 9 01 0 0 54.0 1 1",
                                           MachineType::F)) {
      qpiri_priority |= r.key == "output_source_priority";
    }
    check(!qpiri_priority, "QPIRI priorities only for type B codes");
  }
  {
    namespace w = esphome::solarplug::writes;
    auto cmd = [](const w::Result &r) { return r.frame.has_value() ? r.frame->command : "REFUSED: " + r.reason; };
    w::Context a;
    a.machine_type = MachineType::A;
    check(cmd(w::encode_select("output_source_priority", "SBU priority", a)) == "POP02", "type A SBU is POP02");
    check(cmd(w::encode_select("charger_priority", "OSO", a)) == "PCP03", "type A OSO is PCP03");
    check(!w::encode_select("output_source_priority", "PEC Mode (CT)", a).frame.has_value(), "type A has no PEC");
    w::Context unknown;
    unknown.machine_type = std::nullopt;
    check(!w::encode_select("charger_priority", "OSO", unknown).frame.has_value(), "priority refused until type known");
    w::Context v24;
    v24.battery_voltage = 24;
    v24.detected_battery_voltage = 24;
    check(cmd(w::encode_number("battery_recharge_voltage", 23.5f, v24)) == "PBCV23.5", "24 V recharge 0.5 V steps");
    check(!w::encode_number("battery_recharge_voltage", 46.0f, v24).frame.has_value(), "24 V refuses 48 V values");
    check(cmd(w::encode_number("battery_equalization_voltage", 29.2f, v24)) == "PBEQV29.20", "24 V equalization");
    check(cmd(w::encode_select("battery_redischarge_voltage", "26.5 V", v24)) == "PBDV26.5", "24 V redischarge option");
    check(w::redischarge_options(24).size() == 12 && w::redischarge_options(24)[1] == "24 V" &&
              w::redischarge_options(24)[2] == "24.5 V" && w::redischarge_options(48)[11] == "58 V",
          "redischarge options per bank");
    w::Context mismatch;
    mismatch.detected_battery_voltage = 24;
    check(!w::encode_number("battery_recharge_voltage", 46.0f, mismatch).frame.has_value() &&
              w::encode_number("grid_connected_current", 20.0f, mismatch).frame.has_value(),
          "bank mismatch refuses voltage settings only");
    w::Context unread_bank;  // HBAT not read yet
    check(!w::encode_number("battery_recharge_voltage", 46.0f, unread_bank).frame.has_value() &&
              !w::encode_select("battery_redischarge_voltage", "54 V", unread_bank).frame.has_value(),
          "voltage settings fail closed until the bank is read");
    w::Context conflict;
    conflict.machine_type_conflict = true;
    check(!w::encode_select("output_source_priority", "SBU priority", conflict).frame.has_value(),
          "priority refused on a machine-type conflict");
    // Every redischarge option round-trips: encode, then read the frame's value back as HEEP2[5].
    for (int bank : {24, 48}) {
      w::Context c;
      c.battery_voltage = bank;
      c.detected_battery_voltage = bank;
      for (const auto &option : w::redischarge_options(bank)) {
        const auto frame = w::encode_select("battery_redischarge_voltage", option, c);
        bool ok = frame.frame.has_value();
        if (ok) {
          const std::string value = "0" + frame.frame->command.substr(4);  // PBDV24.5 -> 024.5
          const std::string payload = "(0 044.0 020 044.0 046.0 " + value + " 0 058.4 060 120 030 0000 0000 10 0000 52.0 30000";
          ok = readback("HEEP2", payload)["battery_redischarge_voltage"] == option;
        }
        check(ok, "redischarge option round-trips: " + option + " on " + std::to_string(bank) + " V");
      }
    }
    std::vector<int> easun = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120};
    w::Context wide;
    wide.max_charge_envelope = &easun;
    check(cmd(w::encode_select("maximum_charging_current", "120 A", wide)) == "MNCHGC120", "configured envelope (Easun 120 A)");
  }

  // Minimum token gate used by the firmware before decoding.
  check(min_tokens_for("HEEP1") == 18 && min_tokens_for("QPIRI") == 18 && min_tokens_for("QBEQI") == 8, "min tokens");

  if (failures == 0) std::printf("OK\n");
  return failures == 0 ? 0 : 1;
}

int dump() {
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.empty() || line[0] == '#') continue;
    const size_t tab = line.find('\t');
    if (tab == std::string::npos) continue;
    const std::string command = line.substr(0, tab);
    const std::string payload = line.substr(tab + 1);
    // Every corpus row must pass the firmware's minimum gate, or parity coverage would shrink silently.
    if (count_tokens_from(payload, payload.rfind('(', 0) == 0 ? 1 : 0) < min_tokens_for(command)) {
      std::printf("SKIPPED\t%s\t%s\n", command.c_str(), payload.c_str());
      continue;
    }
    for (const auto &f : decode_fields(command, payload)) {
      std::printf("F\t%s\t%s\t%s\n", command.c_str(), f.name.c_str(), f.value.c_str());
    }
    for (const auto &[key, value] : readback(command, payload)) {
      std::printf("R\t%s\t%s\t%s\n", command.c_str(), key.c_str(), value.c_str());
    }
    std::printf("E\t%s\t%s\n", command.c_str(), payload.c_str());  // end of frame
  }
  return 0;
}

// codec_harness encode: one request per stdin line, answered with the encoder's verdict.
//   S<TAB>key<TAB>0|1            switch
//   L<TAB>key<TAB>option         select (announced lists = this unit's QMCHGCR / QMUCHGCR)
//   N<TAB>key<TAB>value[<TAB>k=v;k=v]   number, with the reported values the ordering rules read
// prints OK<TAB>command or REFUSED<TAB>reason per line.
int encode(MachineType machine, int battery_voltage) {
  namespace w = esphome::solarplug::writes;
  const std::vector<int> max_list = {10, 20, 30, 40, 50, 60, 70, 80};
  const std::vector<int> mains_list = {2, 10, 20, 30, 40, 50, 60};
  std::string line;
  while (std::getline(std::cin, line)) {
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t tab = line.find('\t'); tab != std::string::npos; tab = line.find('\t', start)) {
      parts.push_back(line.substr(start, tab - start));
      start = tab + 1;
    }
    parts.push_back(line.substr(start));
    if (parts.size() < 3) continue;
    std::map<std::string, float> reported;
    if (parts.size() > 3) {
      size_t pos = 0;
      while (pos < parts[3].size()) {
        size_t end = parts[3].find(';', pos);
        if (end == std::string::npos) end = parts[3].size();
        const std::string pair = parts[3].substr(pos, end - pos);
        const size_t eq = pair.find('=');
        if (eq != std::string::npos) reported[pair.substr(0, eq)] = std::strtof(pair.c_str() + eq + 1, nullptr);
        pos = end + 1;
      }
    }
    w::Context ctx;
    ctx.reported = &reported;
    ctx.allowed_max_charge_a = &max_list;
    ctx.allowed_mains_charge_a = &mains_list;
    ctx.machine_type = machine;
    ctx.battery_voltage = battery_voltage;
    ctx.detected_battery_voltage = battery_voltage;
    w::Result result = w::Result::refuse("bad request kind");
    if (parts[0] == "S") result = w::encode_switch(parts[1], parts[2] == "1");
    if (parts[0] == "L") result = w::encode_select(parts[1], parts[2], ctx);
    if (parts[0] == "N") result = w::encode_number(parts[1], std::strtof(parts[2].c_str(), nullptr), ctx);
    if (parts[0] == "O") {  // O<TAB>redischarge<TAB>volts: the select options, joined by '|'
      std::string joined;
      for (const auto &option : w::redischarge_options(std::atoi(parts[2].c_str()))) joined += (joined.empty() ? "" : "|") + option;
      std::printf("OK\t%s\n", joined.c_str());
      continue;
    }
    if (result.frame.has_value()) {
      std::printf("OK\t%s\n", result.frame->command.c_str());
    } else {
      std::printf("REFUSED\t%s\n", result.reason.c_str());
    }
  }
  return 0;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc > 1 && std::strcmp(argv[1], "dump") == 0) return dump();
  if (argc > 1 && std::strcmp(argv[1], "encode") == 0) {
    // codec_harness encode [machine type A|B|C|D|F|UNKNOWN] [battery voltage]
    MachineType machine = MachineType::B;
    if (argc > 2) {
      const std::string t = argv[2];
      machine = t == "A" ? MachineType::A : t == "C" ? MachineType::C : t == "D" ? MachineType::D
              : t == "F" ? MachineType::F : t == "UNKNOWN" ? MachineType::UNKNOWN : MachineType::B;
    }
    return encode(machine, argc > 3 ? std::atoi(argv[3]) : 48);
  }
  return selftest();
}
