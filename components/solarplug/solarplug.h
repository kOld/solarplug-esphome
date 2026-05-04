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

#include <array>
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
  void set_boot_poll_delay_ms(uint32_t value) { this->boot_poll_delay_ms_ = value; }
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

  const std::string &hbat_battery_type() const { return this->latest_.hbat_battery_type; }
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
  struct PendingCommand;

  static bool is_allowed_command_(const std::string &command);
  static bool is_known_passive_label_(const std::string &command);
  bool queue_write_frame_(const std::string &label, const std::string &frame, FrameStyle frame_style);
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
  void publish_write_states_from_hbat_();
  void publish_write_states_from_heep1_(const std::string &payload);
  void publish_write_states_from_heep2_(const std::string &payload);
  void empty_uart_buffer_();
  void start_command_(const std::string &command, FrameStyle frame_style, uint8_t retry_count, bool retry_allowed);
  void finish_frame_(const char *status);
  void process_passive_frame_();
  void process_complete_frame_();
  void log_decoded_payload_(const std::string &command, const std::string &payload);
  void apply_decoded_field_(const std::string &command, const std::string &field_name, const std::string &value);
  bool extract_frame_payload_(std::string *payload_text, bool *crc_present);
  size_t build_frame_(const std::string &command, FrameStyle frame_style, uint8_t *frame, size_t frame_size) const;
  uint16_t crc16_xmodem_(const uint8_t *data, size_t len) const;
  std::pair<uint8_t, uint8_t> adjust_crc_bytes_(uint16_t crc) const;

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

    std::string hbat_battery_type;
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
  struct PendingCommand {
    std::string command;
    FrameStyle frame_style{FrameStyle::ASCII_CR};
    uint8_t retry_count{0};
    bool retry_allowed{true};
  };
  std::array<PendingCommand, MAX_PENDING_COMMANDS> pending_commands_;
  size_t pending_command_head_{0};
  size_t pending_command_tail_{0};
  size_t pending_command_count_{0};
  std::string active_command_;
  uint8_t active_retry_count_{0};
  bool active_retry_allowed_{true};
  std::string observed_command_;
  std::array<uint8_t, MAX_RESPONSE_FRAME_BYTES> response_frame_;
  size_t response_frame_len_{0};
  uint32_t last_activity_ms_{0};
  uint32_t response_timeout_ms_{1500};
  bool passive_mode_{true};
  bool enable_writes_{false};
  bool raw_frame_logging_{false};
  bool decoded_field_logging_{false};
  uint32_t boot_poll_delay_ms_{10000};
  uint32_t fast_poll_interval_ms_{20000};
  uint32_t energy_poll_interval_ms_{60000};
  uint32_t identity_poll_interval_ms_{1800000};
  uint32_t boot_time_ms_{0};
  uint32_t last_fast_poll_ms_{0};
  uint32_t last_energy_poll_ms_{0};
  uint32_t last_identity_poll_ms_{0};
  bool boot_poll_sent_{false};

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
      if (this->parent_->send_write_switch(this->write_key_, state)) {
        this->publish_state(state);
      }
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
      if (this->parent_->send_write_number(this->write_key_, value)) {
        this->publish_state(value);
      }
    }
  }

  SolarPlugComponent *parent_{nullptr};
  std::string write_key_;
};

class SolarPlugWriteSelect : public select::Select {
 public:
  void set_parent(SolarPlugComponent *parent) { this->parent_ = parent; }
  void set_write_key(const std::string &key) { this->write_key_ = key; }

 protected:
  void control(const std::string &value) override {
    if (this->parent_ != nullptr) {
      if (this->parent_->send_write_select(this->write_key_, value)) {
        this->publish_state(value);
      }
    }
  }

  SolarPlugComponent *parent_{nullptr};
  std::string write_key_;
};

}  // namespace solarplug
}  // namespace esphome
