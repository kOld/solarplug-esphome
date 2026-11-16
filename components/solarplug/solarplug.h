#pragma once

#include "esphome/components/uart/uart.h"
#include "esphome/components/button/button.h"
#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"
#include "esphome/core/defines.h"
#include "solarplug_codec.h"
#include "solarplug_writes.h"
#ifdef USE_TIME
#include "esphome/components/time/real_time_clock.h"
#endif

#include <array>
#include <map>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace esphome {
namespace solarplug {

static const char *const TAG = "solarplug";

class SolarPlugWriteSwitch;
class SolarPlugWriteNumber;
class SolarPlugWriteSelect;

inline std::string hex_encode_bytes(const uint8_t *data, size_t len) {
  std::string out;
  out.reserve(len * 3);
  char buf[4];
  for (size_t i = 0; i < len; i++) {
    if (i != 0) {
      out.push_back(' ');
    }
    std::snprintf(buf, sizeof(buf), "%02X", data[i]);
    out.append(buf);
  }
  return out;
}

inline std::string ascii_encode_bytes(const uint8_t *data, size_t len) {
  std::string out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; i++) {
    uint8_t byte = data[i];
    switch (byte) {
      case '\r':
        out.append("\\r");
        break;
      case '\n':
        out.append("\\n");
        break;
      case '\t':
        out.append("\\t");
        break;
      case '\\':
        out.append("\\\\");
        break;
      case '"':
        out.append("\\\"");
        break;
      default:
        if (byte >= 0x20 && byte <= 0x7E) {
          out.push_back(static_cast<char>(byte));
        } else {
          out.push_back('.');
        }
        break;
    }
  }
  return out;
}

class SolarPlugComponent : public Component, public uart::UARTDevice {
 public:
  static constexpr size_t MAX_PENDING_COMMANDS = 32;
  static constexpr size_t MAX_REQUEST_FRAME_BYTES = 64;
  static constexpr size_t MAX_RESPONSE_FRAME_BYTES = 512;

  void set_enable_writes(bool value) { this->enable_writes_ = value; }
  void set_raw_frame_logging(bool value) { this->raw_frame_logging_ = value; }
  void set_decoded_field_logging(bool value) { this->decoded_field_logging_ = value; }
  void set_response_timeout_ms(uint32_t value) { this->response_timeout_ms_ = value; }
  void set_passive_mode(bool value) { this->passive_mode_ = value; }
  void set_fast_poll_interval_ms(uint32_t value) { this->fast_poll_interval_ms_ = value; }
  void set_energy_poll_interval_ms(uint32_t value) { this->energy_poll_interval_ms_ = value; }
  void set_identity_poll_interval_ms(uint32_t value) { this->identity_poll_interval_ms_ = value; }
  void set_settings_poll_interval_ms(uint32_t value) { this->settings_poll_interval_ms_ = value; }
  void set_boot_poll_delay_ms(uint32_t value) { this->boot_poll_delay_ms_ = value; }
  // Keep the inverter in response-CRC mode (HOSTCRCEN every 30 s; the mode lapses ~64 s after each one).
  void set_response_crc(bool value) { this->response_crc_ = value; }
  // Model profile. machine_type: -1 = auto (from QPRTL), else a codec::MachineType value.
  void set_machine_type(int value) {
    if (value >= 0) this->machine_type_config_ = static_cast<codec::MachineType>(value);
  }
  void set_battery_voltage(int volts) { this->battery_voltage_ = volts; }
  void set_charge_current_envelope(const std::vector<int> &max_charge, const std::vector<int> &mains_charge) {
    this->max_charge_envelope_ = max_charge;
    this->mains_charge_envelope_ = mains_charge;
  }
  void register_sensor(const std::string &command, const std::string &field, sensor::Sensor *sensor);
  void register_text_sensor(const std::string &command, const std::string &field, text_sensor::TextSensor *sensor);
  void register_write_switch(const std::string &key, SolarPlugWriteSwitch *sensor);
  void register_write_number(const std::string &key, SolarPlugWriteNumber *sensor);
  void register_write_select(const std::string &key, SolarPlugWriteSelect *sensor);
  bool send_command(const std::string &command);
  void send_command_group(const std::string &group);
  bool send_write_switch(const std::string &key, bool state);
  bool send_write_number(const std::string &key, float value);
  bool send_write_select(const std::string &key, const std::string &value);
  bool sync_clock();
  // Diagnostics, for an ESPHome API action. probe_read sends one read (any polled command, or an
  // allow-listed query this firmware does not poll) and publishes the raw reply to probe_result.
  // probe_write only takes HOSTCRCEN / HOSTCRCDN (response-CRC mode) and needs enable_writes.
  bool probe_read(const std::string &command);
  bool probe_write(const std::string &command);
#ifdef USE_TIME
  void set_time(time::RealTimeClock *time) { this->time_ = time; }
#endif

  const std::string &hsts_status_code() const { return this->latest_.hsts_status_code; }
  const std::string &hsts_mode_code() const { return this->latest_.hsts_mode_code; }
  const std::string &hsts_mode_label() const { return this->latest_.hsts_mode_label; }
  const std::string &hsts_status_bits() const { return this->latest_.hsts_status_bits; }
  const std::string &hsts_fault_bits() const { return this->latest_.hsts_fault_bits; }

  std::optional<float> hgrid_ac_input_voltage_v() const { return this->latest_.hgrid_ac_input_voltage_v; }
  std::optional<float> hgrid_mains_frequency_hz() const { return this->latest_.hgrid_mains_frequency_hz; }
  std::optional<float> hgrid_high_mains_loss_voltage_v() const { return this->latest_.hgrid_high_mains_loss_voltage_v; }
  std::optional<float> hgrid_low_mains_loss_voltage_v() const { return this->latest_.hgrid_low_mains_loss_voltage_v; }
  std::optional<float> hgrid_high_mains_loss_frequency_hz() const { return this->latest_.hgrid_high_mains_loss_frequency_hz; }
  std::optional<float> hgrid_low_mains_loss_frequency_hz() const { return this->latest_.hgrid_low_mains_loss_frequency_hz; }
  std::optional<float> hgrid_mains_power_w() const { return this->latest_.hgrid_mains_power_w; }
  const std::string &hgrid_mains_current_flow_direction_code() const { return this->latest_.hgrid_mains_current_flow_direction_code; }
  const std::string &hgrid_mains_current_flow_direction() const { return this->latest_.hgrid_mains_current_flow_direction; }
  std::optional<float> hgrid_rated_power_w() const { return this->latest_.hgrid_rated_power_w; }
  const std::string &hgrid_raw_tail() const { return this->latest_.hgrid_raw_tail; }

  std::optional<float> hop_ac_output_voltage_v() const { return this->latest_.hop_ac_output_voltage_v; }
  std::optional<float> hop_ac_output_frequency_hz() const { return this->latest_.hop_ac_output_frequency_hz; }
  std::optional<float> hop_output_apparent_power_va() const { return this->latest_.hop_output_apparent_power_va; }
  std::optional<float> hop_output_active_power_w() const { return this->latest_.hop_output_active_power_w; }
  std::optional<float> hop_output_load_percent() const { return this->latest_.hop_output_load_percent; }
  const std::string &hop_output_dc_component_status() const { return this->latest_.hop_output_dc_component_status; }
  std::optional<float> hop_rated_power_w() const { return this->latest_.hop_rated_power_w; }
  std::optional<float> hop_inductor_current_a() const { return this->latest_.hop_inductor_current_a; }
  const std::string &hop_raw_tail() const { return this->latest_.hop_raw_tail; }

  std::optional<float> hbat_battery_voltage_v() const { return this->latest_.hbat_battery_voltage_v; }
  std::optional<float> hbat_battery_capacity_percent() const { return this->latest_.hbat_battery_capacity_percent; }
  std::optional<float> hbat_battery_charging_current_a() const { return this->latest_.hbat_battery_charging_current_a; }
  std::optional<float> hbat_battery_discharge_current_a() const { return this->latest_.hbat_battery_discharge_current_a; }
  std::optional<float> hbat_bus_voltage_v() const { return this->latest_.hbat_bus_voltage_v; }

  std::optional<float> hpv_pv_voltage_v() const { return this->latest_.hpv_pv_voltage_v; }
  std::optional<float> hpv_pv_current_a() const { return this->latest_.hpv_pv_current_a; }
  std::optional<float> hpv_pv_power_w() const { return this->latest_.hpv_pv_power_w; }
  std::optional<float> hpv_generation_power_kw() const { return this->latest_.hpv_generation_power_kw; }

  std::optional<float> htemp_inverter_temperature_c() const { return this->latest_.htemp_inverter_temperature_c; }
  std::optional<float> htemp_boost_temperature_c() const { return this->latest_.htemp_boost_temperature_c; }
  std::optional<float> htemp_transformer_temperature_c() const { return this->latest_.htemp_transformer_temperature_c; }
  std::optional<float> htemp_pv_temperature_c() const { return this->latest_.htemp_pv_temperature_c; }
  std::optional<float> htemp_fan_1_speed_percent() const { return this->latest_.htemp_fan_1_speed_percent; }
  std::optional<float> htemp_fan_2_speed_percent() const { return this->latest_.htemp_fan_2_speed_percent; }
  std::optional<float> htemp_max_temperature_c() const { return this->latest_.htemp_max_temperature_c; }
  const std::string &htemp_temperature_status_bits() const { return this->latest_.htemp_temperature_status_bits; }
  const std::string &htemp_raw_tail() const { return this->latest_.htemp_raw_tail; }

  const std::string &hgen_date_ymd() const { return this->latest_.hgen_date_ymd; }
  const std::string &hgen_date_iso() const { return this->latest_.hgen_date_iso; }
  const std::string &hgen_time_hm() const { return this->latest_.hgen_time_hm; }
  std::optional<float> hgen_daily_power_gen_kwh() const { return this->latest_.hgen_daily_power_gen_kwh; }
  std::optional<float> hgen_monthly_electricity_generation_kwh() const { return this->latest_.hgen_monthly_electricity_generation_kwh; }
  std::optional<float> hgen_yearly_electricity_generation_kwh() const { return this->latest_.hgen_yearly_electricity_generation_kwh; }
  std::optional<float> hgen_total_power_generation_kwh() const { return this->latest_.hgen_total_power_generation_kwh; }
  const std::string &hgen_raw_tail() const { return this->latest_.hgen_raw_tail; }

  const std::string &qprtl_device_type() const { return this->latest_.qprtl_device_type; }

  const std::string &himsg1_software_version() const { return this->latest_.himsg1_software_version; }
  const std::string &himsg1_software_date() const { return this->latest_.himsg1_software_date; }
  const std::string &himsg1_software_date_iso() const { return this->latest_.himsg1_software_date_iso; }
  const std::string &himsg1_revision() const { return this->latest_.himsg1_revision; }

  const std::string &hbms1_bms_status_code() const { return this->latest_.hbms1_bms_status_code; }
  const std::string &hbms1_bms_flags() const { return this->latest_.hbms1_bms_flags; }
  std::optional<float> hbms1_bms_discharge_voltage_limit_v() const { return this->latest_.hbms1_bms_discharge_voltage_limit_v; }
  std::optional<float> hbms1_bms_charge_voltage_limit_v() const { return this->latest_.hbms1_bms_charge_voltage_limit_v; }
  std::optional<float> hbms1_bms_charge_current_limit_a() const { return this->latest_.hbms1_bms_charge_current_limit_a; }
  std::optional<float> hbms1_bms_soc_percent() const { return this->latest_.hbms1_bms_soc_percent; }
  std::optional<float> hbms1_bms_charging_current_a() const { return this->latest_.hbms1_bms_charging_current_a; }
  std::optional<float> hbms1_bms_discharge_current_a() const { return this->latest_.hbms1_bms_discharge_current_a; }
  const std::string &hbms1_raw_tail() const { return this->latest_.hbms1_raw_tail; }

  const std::string &hbms2_display_mode() const { return this->latest_.hbms2_display_mode; }
  std::optional<float> hbms2_remaining_capacity_ah() const { return this->latest_.hbms2_remaining_capacity_ah; }
  std::optional<float> hbms2_nominal_capacity_ah() const { return this->latest_.hbms2_nominal_capacity_ah; }
  std::optional<float> hbms2_max_voltage_mv() const { return this->latest_.hbms2_max_voltage_mv; }
  std::optional<float> hbms2_max_voltage_cell_position() const { return this->latest_.hbms2_max_voltage_cell_position; }
  std::optional<float> hbms2_min_voltage_mv() const { return this->latest_.hbms2_min_voltage_mv; }
  std::optional<float> hbms2_min_voltage_cell_position() const { return this->latest_.hbms2_min_voltage_cell_position; }
  const std::string &hbms2_raw_tail() const { return this->latest_.hbms2_raw_tail; }

  std::optional<float> hbms3_cell_voltage_mv(size_t index) const;
  const std::string &hbms3_raw_tail() const { return this->latest_.hbms3_raw_tail; }

  const std::string &heep1_raw_payload() const { return this->latest_.heep1_raw_payload; }
  const std::string &heep2_raw_payload() const { return this->latest_.heep2_raw_payload; }
  std::optional<float> hpvb_pv_voltage_v() const { return this->latest_.hpvb_pv_voltage_v; }
  std::optional<float> hpvb_pv_current_a() const { return this->latest_.hpvb_pv_current_a; }
  std::optional<float> hpvb_pv_power_w() const { return this->latest_.hpvb_pv_power_w; }
  const std::string &hpvb_pv_charging_mark_code() const { return this->latest_.hpvb_pv_charging_mark_code; }
  const std::string &hpvb_pv_charging_mark() const { return this->latest_.hpvb_pv_charging_mark; }
  std::optional<float> hpvb_bus_voltage_v() const { return this->latest_.hpvb_bus_voltage_v; }
  const std::string &hpvb_raw_payload() const { return this->latest_.hpvb_raw_payload; }

  void setup() override;
  void loop() override;
  void dump_config() override;

 protected:
  enum class State { IDLE, WAITING };
  enum class FrameStyle { ASCII_CR, CRC_XMODEM_CR };
  enum class WriteKind : uint8_t { NONE, SWITCH, NUMBER, SELECT };
  struct PendingCommand;
  struct PendingWriteEffect {
    std::string frame;
    WriteKind kind{WriteKind::NONE};
    std::string key;
    bool bool_value{false};
    float float_value{0.0f};
    std::string text_value;
  };

  writes::Context write_context_() const;
  bool queue_encoded_write_(WriteKind kind, const std::string &key, const writes::Result &result);
  void expect_write_effect_(WriteKind kind, const std::string &key, bool bool_value, float float_value,
                            const std::string &text_value);
  void on_write_finished_(const std::string &frame, const char *status);
  bool command_has_consumer_(const std::string &command) const;
  bool frame_shape_ok_(const std::string &command, const std::string &payload, bool hold_length);
  void republish_write_state_(WriteKind kind, const std::string &key);
  void refuse_write_(WriteKind kind, const std::string &key, const std::string &reason);
  struct PeriodGuard {
    std::optional<float> last;
    std::optional<float> candidate;
    uint8_t hits{0};
    std::string period_key;        // YYMMDD / YYMM / YY of the last HGEN date seen
    uint32_t period_change_ms{0};  // when period_key last changed
    bool period_changed{false};    // a period change happened within the last 2 h
    std::string key_at_last;       // period_key when `last` was accepted
  };
  bool hgen_value_plausible_(const std::string &value, float min_value, float max_value, PeriodGuard &guard);
  bool hgen_total_plausible_(const std::string &value);

  static bool is_allowed_command_(const std::string &command);
  static bool is_known_passive_label_(const std::string &command);
  bool queue_write_frame_(const std::string &label, const std::string &frame, FrameStyle frame_style,
                         bool probe = false);
  bool queue_pending_command_(const PendingCommand &command);
  bool queue_pending_front_(const PendingCommand &command);
  bool pop_pending_command_(PendingCommand *command);
  bool pending_command_exists_(const std::string &command) const;
  void clear_response_frame_();
  bool append_response_byte_(uint8_t byte);
  void queue_command_group_(const std::string &group);
  void queue_commands_(const char *const *commands, size_t count);
  void poll_due_groups_();
  void publish_numeric_(const std::string &command, const std::string &field_name, const std::string &value);
  void publish_text_(const std::string &command, const std::string &field_name, const std::string &value);
  void publish_write_status_(const std::string &status);
  void publish_write_switch_state_(const std::string &key, bool state);
  void publish_write_number_state_(const std::string &key, float value);
  void publish_write_select_state_(const std::string &key, const std::string &value);
  void publish_write_states_from_heep1_(const std::string &payload);
  void publish_write_states_from_heep2_(const std::string &payload);
  void publish_write_states_from_qpiri_(const std::string &payload);
  void publish_write_states_from_qflag_(const std::string &payload);
  void publish_write_states_from_qbeqi_(const std::string &payload);
  void publish_cross_check_();
  void apply_reading_(const codec::Reading &reading);
  void compare_with_heep1_(const codec::Reading &reading, const char *source, std::string *mismatches, int *compared);
  void account_command_(const char *status);
  void publish_stats_(uint32_t now);
  void publish_stat_(const char *field, float value);
  static bool is_pi30_query_(const std::string &command);
  static bool is_probe_only_command_(const std::string &command);
  void on_crc_mode_changed_(const char *why);
  void queue_crc_keepalive_(uint32_t now);
  void publish_crc_state_();
  static FrameStyle read_frame_style_(const std::string &command);
  void empty_uart_buffer_();
  void start_command_(const PendingCommand &command);
  void finish_frame_(const char *status);
  void process_passive_frame_();
  void process_complete_frame_();
  void log_decoded_payload_(const std::string &command, const std::string &payload);
  void apply_decoded_field_(const std::string &command, const std::string &field_name, const std::string &value);
  bool extract_frame_payload_(std::string *payload_text, bool *crc_present);
  bool writes_in_flight_() const { return !this->pending_write_effects_.empty(); }
  bool resync_done_(uint32_t now);
  size_t build_frame_(const std::string &command, FrameStyle frame_style, uint8_t *frame, size_t frame_size) const;

  struct LatestState {
    std::string hsts_status_code;
    std::string hsts_mode_code;
    std::string hsts_mode_label;
    std::string hsts_status_bits;
    std::string hsts_fault_bits;

    std::optional<float> hgrid_ac_input_voltage_v;
    std::optional<float> hgrid_mains_frequency_hz;
    std::optional<float> hgrid_high_mains_loss_voltage_v;
    std::optional<float> hgrid_low_mains_loss_voltage_v;
    std::optional<float> hgrid_high_mains_loss_frequency_hz;
    std::optional<float> hgrid_low_mains_loss_frequency_hz;
    std::optional<float> hgrid_mains_power_w;
    std::string hgrid_mains_current_flow_direction_code;
    std::string hgrid_mains_current_flow_direction;
    std::optional<float> hgrid_rated_power_w;
    std::string hgrid_raw_tail;

    std::optional<float> hop_ac_output_voltage_v;
    std::optional<float> hop_ac_output_frequency_hz;
    std::optional<float> hop_output_apparent_power_va;
    std::optional<float> hop_output_active_power_w;
    std::optional<float> hop_output_load_percent;
    std::string hop_output_dc_component_status;
    std::optional<float> hop_rated_power_w;
    std::optional<float> hop_inductor_current_a;
    std::string hop_raw_tail;

    std::optional<float> hbat_battery_series_count;
    std::optional<float> hbat_battery_voltage_v;
    std::optional<float> hbat_battery_capacity_percent;
    std::optional<float> hbat_battery_charging_current_a;
    std::optional<float> hbat_battery_discharge_current_a;
    std::optional<float> hbat_bus_voltage_v;

    std::optional<float> hpv_pv_voltage_v;
    std::optional<float> hpv_pv_current_a;
    std::optional<float> hpv_pv_power_w;
    std::optional<float> hpv_generation_power_kw;

    std::optional<float> htemp_inverter_temperature_c;
    std::optional<float> htemp_boost_temperature_c;
    std::optional<float> htemp_transformer_temperature_c;
    std::optional<float> htemp_pv_temperature_c;
    std::optional<float> htemp_fan_1_speed_percent;
    std::optional<float> htemp_fan_2_speed_percent;
    std::optional<float> htemp_max_temperature_c;
    std::string htemp_temperature_status_bits;
    std::string htemp_raw_tail;

    std::string hgen_date_ymd;
    std::string hgen_date_iso;
    std::string hgen_time_hm;
    std::optional<float> hgen_daily_power_gen_kwh;
    std::optional<float> hgen_monthly_electricity_generation_kwh;
    std::optional<float> hgen_yearly_electricity_generation_kwh;
    std::optional<float> hgen_total_power_generation_kwh;
    std::string hgen_raw_tail;

    std::string qprtl_device_type;

    std::string himsg1_software_version;
    std::string himsg1_software_date;
    std::string himsg1_software_date_iso;
    std::string himsg1_revision;

    std::string hbms1_bms_status_code;
    std::string hbms1_bms_flags;
    std::optional<float> hbms1_bms_discharge_voltage_limit_v;
    std::optional<float> hbms1_bms_charge_voltage_limit_v;
    std::optional<float> hbms1_bms_charge_current_limit_a;
    std::optional<float> hbms1_bms_soc_percent;
    std::optional<float> hbms1_bms_charging_current_a;
    std::optional<float> hbms1_bms_discharge_current_a;
    std::string hbms1_raw_tail;

    std::string hbms2_display_mode;
    std::optional<float> hbms2_remaining_capacity_ah;
    std::optional<float> hbms2_nominal_capacity_ah;
    std::optional<float> hbms2_max_voltage_mv;
    std::optional<float> hbms2_max_voltage_cell_position;
    std::optional<float> hbms2_min_voltage_mv;
    std::optional<float> hbms2_min_voltage_cell_position;
    std::string hbms2_raw_tail;

    std::array<std::optional<float>, 16> hbms3_cell_voltage_mv{};
    std::string hbms3_raw_tail;

    std::string heep1_raw_payload;
    std::string heep2_raw_payload;
    std::string qpiri_raw_payload;
    std::string qflag_raw_payload;
    std::string qbeqi_raw_payload;
    std::optional<float> hpvb_pv_voltage_v;
    std::optional<float> hpvb_pv_current_a;
    std::optional<float> hpvb_pv_power_w;
    std::string hpvb_pv_charging_mark_code;
    std::string hpvb_pv_charging_mark;
    std::optional<float> hpvb_bus_voltage_v;
    std::string hpvb_raw_payload;
  };

  State state_{State::IDLE};
  LatestState latest_;
  // READ: polled or probed query (retried once, decoded). WRITE: a setter from an entity, button or
  // probe_write (ACK/NAK reported on last_write_status). CRC_KEEPALIVE: the internal HOSTCRCEN that keeps
  // response-CRC mode alive; it never touches last_write_status and logs only at debug level.
  enum class CommandKind : uint8_t { READ, WRITE, CRC_KEEPALIVE };
  struct PendingCommand {
    std::string command;
    FrameStyle frame_style{FrameStyle::ASCII_CR};
    uint8_t retry_count{0};
    CommandKind kind{CommandKind::READ};
    bool probe{false};  // publish the reply on probe_result
  };
  std::array<PendingCommand, MAX_PENDING_COMMANDS> pending_commands_;
  size_t pending_command_head_{0};
  size_t pending_command_tail_{0};
  size_t pending_command_count_{0};
  std::string active_command_;
  uint8_t active_retry_count_{0};
  CommandKind active_kind_{CommandKind::READ};
  bool active_probe_{false};
  std::string observed_command_;
  std::array<uint8_t, MAX_RESPONSE_FRAME_BYTES> response_frame_;
  size_t response_frame_len_{0};
  uint32_t last_activity_ms_{0};
  bool activity_armed_{false};  // true while a response timeout is running
  uint32_t response_timeout_ms_{1500};
  bool passive_mode_{true};
  bool enable_writes_{false};
  bool raw_frame_logging_{false};
  bool decoded_field_logging_{false};
  uint32_t boot_poll_delay_ms_{10000};
  uint32_t fast_poll_interval_ms_{20000};
  uint32_t energy_poll_interval_ms_{60000};
  uint32_t identity_poll_interval_ms_{1800000};
  uint32_t settings_poll_interval_ms_{300000};
  uint32_t boot_time_ms_{0};
  uint32_t last_fast_poll_ms_{0};
  uint32_t last_energy_poll_ms_{0};
  uint32_t last_identity_poll_ms_{0};
  uint32_t last_settings_poll_ms_{0};
  bool boot_poll_sent_{false};
  bool scheduled_poll_{false};
  std::string last_queued_write_frame_;
  // After a timeout or a bad frame the bus must be quiet for RESYNC_SILENCE_MS before the next request,
  // so a late reply is drained instead of being credited to the next command.
  static constexpr uint32_t RESYNC_SILENCE_MS = 400;
  static constexpr uint32_t RESYNC_MAX_MS = 3000;  // a noisy line must not stall polling forever
  static constexpr uint32_t RESYNC_AFTER_TIMEOUT_MS = 100;
  bool resync_pending_{false};
  uint32_t resync_last_rx_ms_{0};
  uint32_t resync_start_ms_{0};
  codec::ReplyLengthGuard reply_lengths_;
  codec::ResponseCrcTracker crc_mode_;  // whether H replies currently carry a CRC
  // Model profile (see codec::MachineType and writes::Context).
  std::optional<codec::MachineType> machine_type_config_;    // set in YAML, else auto
  std::optional<codec::MachineType> machine_type_detected_;  // from QPRTL
  // The type whose priority codes are used; nullopt while unknown, or when a YAML override contradicts the
  // QPRTL-detected type (then priority readback and writes stop rather than use the wrong codes).
  bool machine_type_conflict_() const {
    return this->machine_type_config_.has_value() && this->machine_type_detected_.has_value() &&
           *this->machine_type_detected_ != codec::MachineType::UNKNOWN &&
           !codec::same_priority_codes(*this->machine_type_config_, *this->machine_type_detected_);
  }
  std::optional<codec::MachineType> machine_type_() const {
    if (this->machine_type_conflict_()) return std::nullopt;
    return this->machine_type_config_.has_value() ? this->machine_type_config_ : this->machine_type_detected_;
  }
  bool heep1_layout_warned_{false};
  uint8_t qprtl_attempts_{0};            // QPRTL reads without a reply while the type is auto (M1)
  uint32_t last_qprtl_attempt_ms_{0};
  int battery_voltage_{48};
  int detected_battery_voltage_{0};  // HBAT[0] x 12, 0 until read
  std::vector<int> max_charge_envelope_;
  std::vector<int> mains_charge_envelope_;
  bool in_charge_envelope_(bool mains, int amps) const;
  void note_machine_type_(const std::string &device_type);
  bool response_crc_{false};            // config: keep the inverter in response-CRC mode
  bool crc_keepalive_sent_{false};
  bool crc_keepalive_answered_{false};  // the first keep-alive finished (any status): state leaves Starting
  uint8_t crc_keepalive_failures_{0};   // unanswered keep-alives in a row (backoff, see codec)
  bool reads_ok_since_keepalive_{false};  // a keep-alive failure only counts while the bus answers reads
  uint32_t last_crc_keepalive_ms_{0};
  std::string crc_state_published_;
  std::string off_list_noted_;  // last select readback that was not an option (logged once)
  std::vector<PendingWriteEffect> pending_write_effects_;
  // Settings cross-check: switch/select values from the last HEEP1 + HEEP2, compared with QPIRI / QFLAG.
  std::map<std::string, std::string> heep_values_;
  std::string qpiri_mismatches_;
  std::string qflag_mismatches_;
  int qpiri_compared_{-1};
  std::vector<int> allowed_max_charge_a_;    // from QMCHGCR
  std::vector<int> allowed_mains_charge_a_;  // from QMUCHGCR
  // The adopted lists survive a reboot, so the selects offer them before HA connects.
  struct SavedChargeLists {
    uint8_t version;
    uint8_t max_count;
    uint8_t mains_count;
    uint8_t max[16];
    uint8_t mains[16];
  };
  static constexpr uint8_t CHARGE_LISTS_VERSION = 1;
  ESPPreferenceObject charge_lists_pref_;
  void load_charge_lists_();
  void save_charge_lists_();
  void apply_charge_options_(bool mains);
  std::vector<int> candidate_max_charge_a_;   // last QMCHGCR reply; becomes the list when repeated
  std::vector<int> candidate_mains_charge_a_;
  std::map<std::string, float> reported_numbers_;  // last number values read back from HEEP1/HEEP2
  int qflag_compared_{-1};
  // Bus profiling (published every 60 s as "_STATS" sensors).
  uint32_t command_start_ms_{0};
  bool command_started_{false};
  uint32_t stats_window_start_ms_{0};
  bool stats_window_started_{false};
  uint32_t stats_busy_ms_{0};
  uint32_t stats_response_ms_sum_{0};
  uint32_t stats_response_count_{0};
  uint32_t stats_timeouts_total_{0};
  uint32_t stats_frame_errors_total_{0};
  uint32_t stats_naks_total_{0};
  std::optional<float> hgen_total_candidate_;
  uint8_t hgen_total_candidate_hits_{0};
  PeriodGuard daily_guard_;
  PeriodGuard monthly_guard_;
  PeriodGuard yearly_guard_;
#ifdef USE_TIME
  time::RealTimeClock *time_{nullptr};
#endif

  struct SensorBinding {
    std::string command;
    std::string field;
    sensor::Sensor *sensor;
  };
  struct TextSensorBinding {
    std::string command;
    std::string field;
    text_sensor::TextSensor *sensor;
  };
  struct WriteSwitchBinding {
    std::string key;
    SolarPlugWriteSwitch *sensor;
    bool seeded{false};  // switch_::Switch has no has_state(); track the first readback publish
  };
  struct WriteNumberBinding {
    std::string key;
    SolarPlugWriteNumber *sensor;
  };
  struct WriteSelectBinding {
    std::string key;
    SolarPlugWriteSelect *sensor;
  };
  std::vector<SensorBinding> sensors_;
  std::vector<TextSensorBinding> text_sensors_;
  std::vector<WriteSwitchBinding> write_switches_;
  std::vector<WriteNumberBinding> write_numbers_;
  std::vector<WriteSelectBinding> write_selects_;
};

class SolarPlugButton : public button::Button {
 public:
  void set_parent(SolarPlugComponent *parent) { this->parent_ = parent; }
  void set_command_group(const std::string &group) { this->command_group_ = group; }

 protected:
  void press_action() override {
    if (this->parent_ != nullptr) {
      this->parent_->send_command_group(this->command_group_);
    }
  }

  SolarPlugComponent *parent_{nullptr};
  std::string command_group_;
};

class SolarPlugWriteSwitch : public switch_::Switch {
 public:
  void set_parent(SolarPlugComponent *parent) { this->parent_ = parent; }
  void set_write_key(const std::string &key) { this->write_key_ = key; }

 protected:
  void write_state(bool state) override {
    if (this->parent_ != nullptr) {
      this->parent_->send_write_switch(this->write_key_, state);  // state is published on ACK
    }
  }

  SolarPlugComponent *parent_{nullptr};
  std::string write_key_;
};

class SolarPlugWriteNumber : public number::Number {
 public:
  void set_parent(SolarPlugComponent *parent) { this->parent_ = parent; }
  void set_write_key(const std::string &key) { this->write_key_ = key; }

 protected:
  void control(float value) override {
    if (this->parent_ != nullptr) {
      this->parent_->send_write_number(this->write_key_, value);  // state is published on ACK
    }
  }

  SolarPlugComponent *parent_{nullptr};
  std::string write_key_;
};

class SolarPlugWriteSelect : public select::Select {
 public:
  void set_parent(SolarPlugComponent *parent) { this->parent_ = parent; }
  void set_write_key(const std::string &key) { this->write_key_ = key; }
  const std::string &write_key() const { return this->write_key_; }
  // Replace the options at runtime (the announced charge-current lists). The traits keep pointers, so the
  // strings live here. HA reads options when it connects, so a change shows after the next reconnect.
  void set_runtime_options(std::vector<std::string> options) {
    // The shown state is an index into the option table, so remember the text and re-point it afterwards.
    const std::string shown = this->has_state() ? this->current_option().str() : std::string();
    this->option_storage_ = std::move(options);
    FixedVector<const char *> pointers;
    pointers.init(this->option_storage_.size());
    for (const auto &option : this->option_storage_) pointers.push_back(option.c_str());
    this->traits.set_options(pointers);
    if (!shown.empty() && this->has_option(shown)) {
      this->publish_state(shown);
    } else {
      this->set_has_state(false);  // no longer offered: unknown until the next readback
    }
  }

 protected:
  void control(const std::string &value) override {
    if (this->parent_ != nullptr) {
      this->parent_->send_write_select(this->write_key_, value);  // state is published on ACK
    }
  }

  SolarPlugComponent *parent_{nullptr};
  std::string write_key_;
  std::vector<std::string> option_storage_;
};

}  // namespace solarplug
}  // namespace esphome
