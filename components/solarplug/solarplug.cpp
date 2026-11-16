#include "solarplug.h"

#include <cstdlib>
#include <cmath>
#include <cstring>

namespace esphome {
namespace solarplug {

using namespace codec;  // pure decode/CRC/readback helpers live in solarplug_codec.cpp

void SolarPlugComponent::setup() {
  this->boot_time_ms_ = millis();
  this->load_charge_lists_();
  ESP_LOGI(TAG, "ready");
}

// Charge-current options: the inverter's announced list inside the model envelope, persisted so the
// selects offer it from boot (HA reads select options only when it connects).
void SolarPlugComponent::load_charge_lists_() {
  // One key per component instance (MULTI_CONF): the index follows the YAML order, which is stable.
  static uint32_t instances = 0;
  const uint32_t instance = instances++;
  this->charge_lists_pref_ =
      global_preferences->make_preference<SavedChargeLists>(fnv1_hash("solarplug.charge_lists") + instance);
  SavedChargeLists saved{};
  if (!this->charge_lists_pref_.load(&saved) || saved.version != CHARGE_LISTS_VERSION || saved.max_count > 16 ||
      saved.mains_count > 16) {
    return;  // first boot: the codegen defaults stay until two identical QM*CGCR replies
  }
  // Keep only values inside the model envelope; a list left empty counts as "not announced".
  auto envelope_ok = [this](bool mains, int amps) { return this->in_charge_envelope_(mains, amps); };
  auto load = [&envelope_ok](const uint8_t *values, uint8_t count, bool mains, std::vector<int> *out) {
    out->clear();
    for (uint8_t i = 0; i < count; i++) {
      if (envelope_ok(mains, values[i])) out->push_back(values[i]);
    }
  };
  load(saved.max, saved.max_count, false, &this->allowed_max_charge_a_);
  load(saved.mains, saved.mains_count, true, &this->allowed_mains_charge_a_);
  this->apply_charge_options_(false);
  this->apply_charge_options_(true);
}

// The configured option lists are the envelope; without them, the HPVINV02 defaults.
bool SolarPlugComponent::in_charge_envelope_(bool mains, int amps) const {
  return codec::charge_current_allowed(mains ? &this->mains_charge_envelope_ : &this->max_charge_envelope_, mains, amps);
}

void SolarPlugComponent::note_machine_type_(const std::string &device_type) {
  const codec::MachineType detected = codec::machine_type_from_qprtl(device_type);
  const bool first_detection = !this->machine_type_detected_.has_value();
  if (!this->machine_type_detected_.has_value() || *this->machine_type_detected_ != detected) {
    this->machine_type_detected_ = detected;
    ESP_LOGI(TAG, "machine type %s from QPRTL %s%s", codec::machine_type_name(detected), device_type.c_str(),
             this->machine_type_config_.has_value() ? " (overridden in YAML)" : "");
  }
  std::string text;
  if (this->machine_type_conflict_()) {
    ESP_LOGW(TAG, "machine_type %s in YAML contradicts QPRTL %s (type %s); priority readback and writes are off",
             codec::machine_type_name(*this->machine_type_config_), device_type.c_str(), codec::machine_type_name(detected));
    text = std::string("conflict: YAML ") + codec::machine_type_name(*this->machine_type_config_) + ", QPRTL " + device_type;
  } else {
    const codec::MachineType effective = *this->machine_type_();
    text = std::string(codec::machine_type_name(effective)) + " (" + device_type + ")";
    if (this->machine_type_config_.has_value()) text += ", set in YAML";
    if (effective == codec::MachineType::UNKNOWN) text += ", type B codes";
  }
  this->publish_text_("_MODEL", "machine_type", text);
  if (first_detection) this->queue_command_group_("settings");  // seed the priorities now, not in 5 min
}

void SolarPlugComponent::save_charge_lists_() {
  if (this->allowed_max_charge_a_.size() > 16 || this->allowed_mains_charge_a_.size() > 16) {
    ESP_LOGW(TAG, "charge-current list longer than 16 entries; not persisted (it is re-learned after each boot)");
    return;
  }
  SavedChargeLists saved{};
  saved.version = CHARGE_LISTS_VERSION;
  saved.max_count = static_cast<uint8_t>(std::min<size_t>(this->allowed_max_charge_a_.size(), 16));
  saved.mains_count = static_cast<uint8_t>(std::min<size_t>(this->allowed_mains_charge_a_.size(), 16));
  for (size_t i = 0; i < saved.max_count; i++) saved.max[i] = static_cast<uint8_t>(this->allowed_max_charge_a_[i]);
  for (size_t i = 0; i < saved.mains_count; i++) saved.mains[i] = static_cast<uint8_t>(this->allowed_mains_charge_a_[i]);
  this->charge_lists_pref_.save(&saved);
}

void SolarPlugComponent::apply_charge_options_(bool mains) {
  const std::vector<int> &allowed = mains ? this->allowed_mains_charge_a_ : this->allowed_max_charge_a_;
  const char *key = mains ? "maximum_mains_charging_current" : "maximum_charging_current";
  std::vector<std::string> options;
  for (int amps : allowed) {
    if (this->in_charge_envelope_(mains, amps)) options.push_back(amps_option(amps));
  }
  if (options.empty()) return;  // nothing usable announced: keep the current options
  for (const auto &binding : this->write_selects_) {
    if (binding.key != key || binding.sensor == nullptr) continue;
    binding.sensor->set_runtime_options(std::move(options));
    // The active index now points into the new list: re-publish the last value the inverter reported.
    auto last = this->heep_values_.find(key);
    if (last != this->heep_values_.end() && binding.sensor->has_option(last->second)) {
      binding.sensor->publish_state(last->second);
    }
    break;
  }
}

void SolarPlugComponent::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Solar Plug decoder\n"
                "  response_timeout: %u ms\n"
                "  passive_mode: %s\n"
                "  enable_writes: %s\n"
                "  raw_frame_logging: %s\n"
                "  decoded_field_logging: %s\n"
                "  response_crc: %s\n"
                "  poll_intervals: fast=%u ms energy=%u ms settings=%u ms identity=%u ms boot_delay=%u ms",
                static_cast<unsigned>(this->response_timeout_ms_), this->passive_mode_ ? "true" : "false",
                this->enable_writes_ ? "true" : "false", this->raw_frame_logging_ ? "true" : "false",
                this->decoded_field_logging_ ? "true" : "false", this->response_crc_ ? "true" : "false",
                static_cast<unsigned>(this->fast_poll_interval_ms_), static_cast<unsigned>(this->energy_poll_interval_ms_),
                static_cast<unsigned>(this->settings_poll_interval_ms_),
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
         command == "HEEP1" || command == "HEEP2" || command == "HEEP3" || command == "HPVB" || command == "HIMSG2" ||
         is_pi30_query_(command) || is_probe_only_command_(command);
}

// Read-only queries that are never polled but may be probed once from the API: H reads the vendor tool
// uses for sibling models (HEEP3, HCTMSG1, HSTS2) plus PI30/PI17 identity and status queries.
// Q commands go out with CRC like every PI30 query. No setter is ever on this list.
bool SolarPlugComponent::is_probe_only_command_(const std::string &command) {
  static const char *const probes[] = {"HEEP4", "HCTMSG1", "HSTS2", "HSTS3",  "QPI",    "QMN",
                                       "QGMN",  "QID",   "QSID",    "QVFW",  "QVFW2", "QVFW3", "QPIGS",
                                       "QPIGS2", "QMOD", "QPIWS",   "QDI",   "QOPPT", "QCHPT", "QT"};
  for (const char *probe : probes) {
    if (command == probe) return true;
  }
  return false;
}

// Read-only PI30 queries this inverter answers on the same port (log-proven 2026-04-25: QPIRI,
// QBEQI, QOP, QCP). Unlike the H labels they must carry a CRC16/XMODEM, like the P* setters.
bool SolarPlugComponent::is_pi30_query_(const std::string &command) {
  return command == "QPIRI" || command == "QFLAG" || command == "QBEQI" || command == "QMCHGCR" ||
         command == "QMUCHGCR" || (is_probe_only_command_(command) && command[0] == 'Q');
}

bool SolarPlugComponent::probe_read(const std::string &command) {
  const char *refusal = nullptr;
  if (!is_allowed_command_(command)) {
    refusal = "not an allow-listed read";
  } else if (this->passive_mode_) {
    refusal = "passive mode";
  } else if (!this->queue_pending_command_({command, read_frame_style_(command), 0, CommandKind::READ, true})) {
    refusal = "command queue full";
  }
  if (refusal != nullptr) {
    ESP_LOGW(TAG, "probe_read %s refused: %s", command.c_str(), refusal);
    this->publish_text_("_PROBE", "probe_result", command + " refused (" + refusal + ")");
    return false;
  }
  this->publish_text_("_PROBE", "probe_result", command + " pending");
  return true;
}

bool SolarPlugComponent::probe_write(const std::string &command) {
  const char *refusal = nullptr;
  if (command != "HOSTCRCEN" && command != "HOSTCRCDN") {
    refusal = "only HOSTCRCEN / HOSTCRCDN";
  } else if (command == "HOSTCRCDN" && this->response_crc_) {
    refusal = "response_crc keeps the mode on";
  } else if (!this->queue_write_frame_("Response CRC mode", command, FrameStyle::CRC_XMODEM_CR, true)) {
    refusal = "writes disabled, passive mode or queue full";
  }
  if (refusal != nullptr) {
    ESP_LOGW(TAG, "probe_write %s refused: %s", command.c_str(), refusal);
    this->publish_text_("_PROBE", "probe_result", command + " refused (" + refusal + ")");
    return false;
  }
  this->publish_text_("_PROBE", "probe_result", command + " pending");
  return true;
}

SolarPlugComponent::FrameStyle SolarPlugComponent::read_frame_style_(const std::string &command) {
  return is_pi30_query_(command) ? FrameStyle::CRC_XMODEM_CR : FrameStyle::ASCII_CR;
}

bool SolarPlugComponent::is_known_passive_label_(const std::string &command) {
  return is_allowed_command_(command) || command == "PCP00" || command == "PCP01" || command == "PCP02" || command == "PCP03" ||
         command == "POP00" || command == "POP01" || command == "POP02" || command == "POP03" || command == "PVENGUSE00" ||
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
  if (!this->queue_pending_command_({command, read_frame_style_(command), 0, CommandKind::READ})) {
    ESP_LOGW(TAG, "command queue full; dropping command=%s", command.c_str());
    return false;
  }
  return true;
}

bool SolarPlugComponent::queue_write_frame_(const std::string &label, const std::string &frame, FrameStyle frame_style,
                                            bool probe) {
  if (!this->enable_writes_) {
    ESP_LOGW(TAG, "writes disabled; ignoring write=%s", label.c_str());
    return false;
  }
  if (this->passive_mode_) {
    ESP_LOGW(TAG, "passive mode enabled; ignoring write=%s", label.c_str());
    return false;
  }
  if (!this->queue_pending_command_({frame, frame_style, 0, CommandKind::WRITE, probe})) {
    ESP_LOGW(TAG, "command queue full; dropping write=%s", label.c_str());
    return false;
  }
  ESP_LOGW(TAG, "queueing write label=%s frame=%s frame_style=%s", label.c_str(), frame.c_str(),
           frame_style == FrameStyle::CRC_XMODEM_CR ? "crc_xmodem_cr" : "ascii_cr");
  this->publish_write_status_("pending " + frame);
  this->last_queued_write_frame_ = frame;
  return true;
}

void SolarPlugComponent::expect_write_effect_(WriteKind kind, const std::string &key, bool bool_value, float float_value,
                                              const std::string &text_value) {
  if (this->pending_write_effects_.size() >= MAX_PENDING_COMMANDS) {
    this->pending_write_effects_.erase(this->pending_write_effects_.begin());
  }
  this->pending_write_effects_.push_back({this->last_queued_write_frame_, kind, key, bool_value, float_value, text_value});
}

// Writes are accepted only for configured entities. Config validation (__init__.py) gates the write
// profile per entity, so this keeps a YAML lambda from reaching an encoder the profile does not allow.
bool SolarPlugComponent::send_write_switch(const std::string &key, bool state) {
  bool configured = false;
  for (const auto &binding : this->write_switches_) configured |= binding.key == key;
  if (!configured) {
    ESP_LOGW(TAG, "write refused: no configured switch %s", key.c_str());
    return false;
  }
  if (!this->queue_encoded_write_(WriteKind::SWITCH, key, writes::encode_switch(key, state))) return false;
  this->expect_write_effect_(WriteKind::SWITCH, key, state, 0.0f, "");
  return true;
}

bool SolarPlugComponent::send_write_number(const std::string &key, float value) {
  const SolarPlugWriteNumber *entity = nullptr;
  for (const auto &binding : this->write_numbers_) {
    if (binding.key == key) entity = binding.sensor;
  }
  if (entity == nullptr) {
    ESP_LOGW(TAG, "write refused: no configured number %s", key.c_str());
    return false;
  }
  // Snap to the entity's step, so the value checked, sent and recorded on ACK is the one the frame encodes
  // (a lambda can pass 24.6 where the encoder sends 025).
  const float step = entity->traits.get_step();
  const float min_value = entity->traits.get_min_value();
  if (std::isfinite(value) && step > 0.0f) {
    const float snapped = min_value + std::round((value - min_value) / step) * step;
    if (std::fabs(value - snapped) > 0.01f * step) {  // off the entity's step: refuse rather than round
      this->refuse_write_(WriteKind::NUMBER, key, "not a multiple of the entity step");
      return false;
    }
    value = snapped;  // removes float noise only (52.299999 -> 52.3)
  }
  if (!this->queue_encoded_write_(WriteKind::NUMBER, key, writes::encode_number(key, value, this->write_context_()))) {
    return false;
  }
  this->expect_write_effect_(WriteKind::NUMBER, key, false, value, "");
  return true;
}

bool SolarPlugComponent::send_write_select(const std::string &key, const std::string &value) {
  bool configured = false;  // the option must be one the entity offers (extended options are opt-in)
  for (const auto &binding : this->write_selects_) {
    configured |= binding.key == key && binding.sensor != nullptr && binding.sensor->has_option(value);
  }
  if (!configured) {
    ESP_LOGW(TAG, "write refused: no configured select %s with option %s", key.c_str(), value.c_str());
    return false;
  }
  if (!this->queue_encoded_write_(WriteKind::SELECT, key, writes::encode_select(key, value, this->write_context_()))) {
    return false;
  }
  this->expect_write_effect_(WriteKind::SELECT, key, false, 0.0f, value);
  return true;
}

void SolarPlugComponent::queue_commands_(const char *const *commands, size_t count) {
  for (size_t index = 0; index < count; index++) {
    // Scheduled polls skip commands nobody consumes (e.g. HBMS3 returns 16 zeroed cells here):
    // at 2400 baud every skipped frame frees ~0.3-0.5 s of bus time per cycle.
    if (this->scheduled_poll_ && !this->command_has_consumer_(commands[index])) {
      continue;
    }
    this->send_command(commands[index]);
  }
}

void SolarPlugComponent::queue_command_group_(const std::string &group) {
  static const char *const fast_commands[] = {"HSTS", "HGRID", "HOP", "HBAT", "HPV", "HTEMP"};
  static const char *const energy_commands[] = {"HGEN", "HBMS1", "HBMS2", "HBMS3"};
  static const char *const identity_commands[] = {"HIMSG1", "QPRTL", "HPVB", "HIMSG2", "QMCHGCR", "QMUCHGCR"};
  // Settings snapshots: every write control is seeded from these (HEEP1 first, so QPIRI/QFLAG can
  // cross-check the values it decoded in the same cycle).
  static const char *const settings_commands[] = {"HEEP1", "HEEP2", "QPIRI", "QFLAG", "QBEQI", "HEEP3"};

  if (group == "fast") {
    this->queue_commands_(fast_commands, sizeof(fast_commands) / sizeof(fast_commands[0]));
  } else if (group == "energy") {
    this->queue_commands_(energy_commands, sizeof(energy_commands) / sizeof(energy_commands[0]));
  } else if (group == "identity") {
    this->queue_commands_(identity_commands, sizeof(identity_commands) / sizeof(identity_commands[0]));
  } else if (group == "settings") {
    this->queue_commands_(settings_commands, sizeof(settings_commands) / sizeof(settings_commands[0]));
  } else if (group == "all") {
    this->queue_command_group_("identity");
    this->queue_command_group_("settings");
    this->queue_command_group_("fast");
    this->queue_command_group_("energy");
  } else if (group == "sync_clock") {
    this->sync_clock();
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
  this->scheduled_poll_ = true;
  struct ResetFlag {
    bool &flag;
    ~ResetFlag() { flag = false; }
  } reset_flag{this->scheduled_poll_};
  if (!this->boot_poll_sent_ && now - this->boot_time_ms_ >= this->boot_poll_delay_ms_) {
    this->queue_command_group_("all");
    this->queue_crc_keepalive_(now);  // queued in front, so the boot poll is already CRC-checked
    this->publish_crc_state_();
    this->boot_poll_sent_ = true;
    this->last_fast_poll_ms_ = now;
    this->last_energy_poll_ms_ = now;
    this->last_identity_poll_ms_ = now;
    this->last_settings_poll_ms_ = now;
    return;
  }
  if (this->boot_poll_sent_) this->queue_crc_keepalive_(now);
  // Auto machine type: ask QPRTL every 20 s until it answers; after 5 unanswered tries fall back to UNKNOWN
  // (type B codes, as earlier releases) and say so, so a silent QPRTL cannot disable priorities for good.
  if (this->boot_poll_sent_ && !this->machine_type_config_.has_value() && !this->machine_type_detected_.has_value() &&
      now - this->last_qprtl_attempt_ms_ >= 20000) {
    this->last_qprtl_attempt_ms_ = now;
    if (this->qprtl_attempts_ >= 5) {
      ESP_LOGW(TAG, "QPRTL not answered after %u tries; using the type B priority codes (set machine_type to override)",
               static_cast<unsigned>(this->qprtl_attempts_));
      this->note_machine_type_("no QPRTL reply");
    } else {
      this->qprtl_attempts_++;
      this->send_command("QPRTL");
    }
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
  if (this->boot_poll_sent_ && this->settings_poll_interval_ms_ > 0 &&
      now - this->last_settings_poll_ms_ >= this->settings_poll_interval_ms_) {
    this->queue_command_group_("settings");
    this->last_settings_poll_ms_ = now;
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
  // Bus bytes reach text states verbatim, and one byte outside printable ASCII (a CRC, line noise) makes
  // HA's ESPHome client drop the connection on every reconnect (seen 2026-11-16).
  for (const auto &binding : this->text_sensors_) {
    if (binding.command == command && binding.field == field_name && binding.sensor != nullptr) {
      if (codec::is_printable_ascii(value)) {
        binding.sensor->publish_state(value);  // the common case: no copy
      } else {
        binding.sensor->publish_state(codec::printable_ascii(value));
      }
    }
  }
}

void SolarPlugComponent::publish_write_status_(const std::string &status) {
  this->publish_text_("_WRITE", "last_write_status", status);
}

// Readback runs on every identity poll; publish only when the value changes (or on first seed)
// so unchanged settings do not generate API traffic and log lines every poll.
void SolarPlugComponent::publish_write_switch_state_(const std::string &key, bool state) {
  for (auto &binding : this->write_switches_) {
    if (binding.key == key && binding.sensor != nullptr) {
      if (binding.seeded && binding.sensor->state == state) continue;
      binding.seeded = true;
      binding.sensor->publish_state(state);
    }
  }
}

void SolarPlugComponent::publish_write_number_state_(const std::string &key, float value) {
  for (const auto &binding : this->write_numbers_) {
    if (binding.key == key && binding.sensor != nullptr) {
      if (binding.sensor->has_state() && binding.sensor->state == value) continue;
      binding.sensor->publish_state(value);
    }
  }
}

void SolarPlugComponent::publish_write_select_state_(const std::string &key, const std::string &value) {
  for (const auto &binding : this->write_selects_) {
    if (binding.key != key || binding.sensor == nullptr) continue;
    if (!binding.sensor->has_option(value)) {
      // The inverter reports a value the select does not offer (set on the LCD, or SOR without extended
      // options). Say so once per key and value instead of silently keeping the old state.
      const std::string note = key + "=" + value;
      if (this->off_list_noted_ != note) {
        this->off_list_noted_ = note;
        ESP_LOGW(TAG, "%s reads %s, which is not one of its options; HA keeps showing the last offered value",
                 key.c_str(), value.c_str());
      }
      continue;
    }
    if (binding.sensor->has_state() && binding.sensor->current_option().str() == value) continue;
    binding.sensor->publish_state(value);
  }
}

// Readback policy. The codec (solarplug_codec.cpp) decodes each snapshot into readings; here we decide
// which source is primary for each control so HA cannot flip between two disagreeing readbacks:
//   HEEP1  primary: charger, battery type, PV feeding, BMS function, a/k/u/v/x/y flags, charge currents,
//          output frequency / voltage / mode, grid protocol, CT, grid feed, HEEP1 numbers
//          cross-check only: output priority, AC range, overload bypass
//   HEEP2  primary: redischarge voltage, dual output mode, hour windows, HEEP2 numbers
//   QPIRI  primary: output priority, AC range; cross-checks charger, battery type, charge currents,
//          redischarge voltage, output frequency / voltage / mode
//   QFLAG  primary: b/j/z; cross-checks a/k/u/v/x/y (and publishes them if HEEP1 gave no value)
//   HSTS   primary: second output power
void SolarPlugComponent::apply_reading_(const codec::Reading &reading) {
  if (reading.kind == codec::ReadingKind::NUMBER) this->reported_numbers_[reading.key] = reading.number;
  switch (reading.kind) {
    case codec::ReadingKind::SWITCH:
      this->publish_write_switch_state_(reading.key, reading.flag);
      break;
    case codec::ReadingKind::SELECT:
      this->publish_write_select_state_(reading.key, reading.text);
      break;
    case codec::ReadingKind::NUMBER:
      this->publish_write_number_state_(reading.key, reading.number);
      break;
  }
}

void SolarPlugComponent::publish_write_states_from_heep1_(const std::string &payload) {
  this->heep_values_.clear();
  for (const auto &reading : codec::heep1_readback(split_tokens(payload), this->machine_type_())) {
    if (reading.kind != codec::ReadingKind::NUMBER) this->heep_values_[reading.key] = reading.text;
    // QPIRI is primary for output priority only where its codes are the verified type B ones.
    const auto type = this->machine_type_();
    const bool qpiri_has_priority = type.has_value() && codec::same_priority_codes(*type, codec::MachineType::B);
    const bool cross_check_only = (reading.key == "output_source_priority" && qpiri_has_priority) ||
                                  reading.key == "ac_input_voltage_range" || reading.key == "overload_bypass";
    if (!cross_check_only) this->apply_reading_(reading);
  }
  if (!codec::normalize_heep1(split_tokens(payload)).has_value() && !this->heep1_layout_warned_) {
    this->heep1_layout_warned_ = true;  // M3: say why settings stay unknown on a sibling model
    ESP_LOGW(TAG, "HEEP1 has an unknown layout (%u tokens); settings readback is off. Please report a capture.",
             static_cast<unsigned>(split_tokens(payload).size()));
  }
}

void SolarPlugComponent::publish_write_states_from_heep2_(const std::string &payload) {
  // HEEP2 follows HEEP1 in the settings tier, so its switch/select values join the same comparison set
  // (e.g. battery_redischarge_voltage, cross-checked by QPIRI[22]).
  for (const auto &reading : codec::heep2_readback(split_tokens(payload))) {
    if (reading.kind != codec::ReadingKind::NUMBER) this->heep_values_[reading.key] = reading.text;
    this->apply_reading_(reading);
  }
}

void SolarPlugComponent::compare_with_heep1_(const codec::Reading &reading, const char *source, std::string *mismatches,
                                             int *compared) {
  auto it = this->heep_values_.find(reading.key);
  if (it == this->heep_values_.end()) return;  // no fresh HEEP1/HEEP2 value to compare against
  (*compared)++;
  if (it->second != reading.text) {
    if (!mismatches->empty()) *mismatches += "; ";
    *mismatches += reading.key + " HEEP1=" + it->second + " " + source + "=" + reading.text;
  }
}

void SolarPlugComponent::publish_write_states_from_qpiri_(const std::string &payload) {
  std::string mismatches;
  int compared = 0;
  for (const auto &reading : codec::qpiri_readback(split_tokens(payload), this->machine_type_())) {
    if (reading.key == "output_source_priority" || reading.key == "ac_input_voltage_range") this->apply_reading_(reading);
    this->compare_with_heep1_(reading, "QPIRI", &mismatches, &compared);
  }
  this->qpiri_mismatches_ = mismatches;
  this->qpiri_compared_ = compared;
  this->publish_cross_check_();
}

void SolarPlugComponent::publish_write_states_from_qflag_(const std::string &payload) {
  std::string mismatches;
  int compared = 0;
  for (const auto &reading : codec::qflag_readback(split_tokens(payload))) {
    const bool heep_primary = reading.key == "buzzer" || reading.key == "display_return_homepage" ||
                              reading.key == "overload_restart" || reading.key == "over_temperature_restart" ||
                              reading.key == "backlight" || reading.key == "input_source_detection_prompt_sound";
    const bool have_heep = this->heep_values_.count(reading.key) > 0;
    if (!heep_primary || !have_heep) this->apply_reading_(reading);
    this->compare_with_heep1_(reading, "QFLAG", &mismatches, &compared);
  }
  this->qflag_mismatches_ = mismatches;
  this->qflag_compared_ = compared;
  this->publish_cross_check_();
}

void SolarPlugComponent::publish_cross_check_() {
  std::string text;
  if (this->qpiri_compared_ <= 0 && this->qflag_compared_ <= 0) {
    text = "Pending";  // nothing compared yet (no fresh HEEP1, or PI30 replies missing)
  } else if (this->qpiri_mismatches_.empty() && this->qflag_mismatches_.empty()) {
    text = "OK";
  } else {
    text = this->qpiri_mismatches_;
    if (!this->qflag_mismatches_.empty()) text += (text.empty() ? "" : "; ") + this->qflag_mismatches_;
    ESP_LOGW(TAG, "settings readback disagreement: %s", text.c_str());
  }
  this->publish_text_("_CHECK", "readback_cross_check", text);
}

void SolarPlugComponent::publish_write_states_from_qbeqi_(const std::string &payload) {
  for (const auto &reading : codec::qbeqi_readback(split_tokens(payload))) this->apply_reading_(reading);
}

void SolarPlugComponent::empty_uart_buffer_() {
  uint8_t buf[64];
  while (this->available() > 0) {
    if (!this->read_array(buf, std::min<size_t>(sizeof(buf), this->available()))) {
      break;
    }
  }
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
    uint16_t crc = codec::crc16_xmodem(frame, frame_len);
    auto [high, low] = codec::adjust_crc_bytes(crc);
    frame[frame_len++] = high;
    frame[frame_len++] = low;
  }
  frame[frame_len++] = 0x0D;
  return frame_len;
}

void SolarPlugComponent::start_command_(const PendingCommand &pending) {
  const std::string &command = pending.command;
  const FrameStyle frame_style = pending.frame_style;
  const uint8_t retry_count = pending.retry_count;
  this->empty_uart_buffer_();
  this->clear_response_frame_();
  this->active_command_ = command;
  this->active_retry_count_ = retry_count;
  this->active_kind_ = pending.kind;
  this->active_probe_ = pending.probe;
  this->state_ = State::WAITING;
  // Explicit flags, not a 0 sentinel: a timestamp must never be adjusted (millis()|1 can lie 1 ms in the
  // future and make `millis() - t` underflow into an instant timeout).
  this->last_activity_ms_ = millis();
  this->activity_armed_ = true;
  this->command_start_ms_ = this->last_activity_ms_;
  this->command_started_ = true;

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
    ESP_LOGD(TAG, "send command=%s retry=%u frame_style=%s", command.c_str(), static_cast<unsigned>(retry_count),
             frame_style == FrameStyle::CRC_XMODEM_CR ? "crc_xmodem_cr" : "ascii_cr");
  }
}

void SolarPlugComponent::account_command_(const char *status) {
  if (!this->command_started_) return;  // passive mode: we did not start this exchange
  const uint32_t elapsed = millis() - this->command_start_ms_;
  this->command_started_ = false;
  this->stats_busy_ms_ += elapsed;
  if (std::strcmp(status, "supported") == 0 || std::strcmp(status, "ack") == 0) {
    this->stats_response_ms_sum_ += elapsed;
    this->stats_response_count_++;
  } else if (this->active_probe_ || this->active_kind_ == CommandKind::CRC_KEEPALIVE) {
    // An unsupported probe or keep-alive (timeout / NAK) is an answer, not a bus fault: keep the health
    // counters about the data path. Keep-alive failures have their own backoff and the Response CRC state.
  } else if (std::strcmp(status, "timeout") == 0) {
    this->stats_timeouts_total_++;
  } else if (std::strcmp(status, "nak") == 0) {
    this->stats_naks_total_++;
  } else {
    this->stats_frame_errors_total_++;
  }
}

void SolarPlugComponent::publish_stat_(const char *field, float value) {
  for (const auto &binding : this->sensors_) {
    if (binding.command == "_STATS" && binding.field == field && binding.sensor != nullptr) {
      binding.sensor->publish_state(value);
    }
  }
}

void SolarPlugComponent::publish_stats_(uint32_t now) {
  const uint32_t window = now - this->stats_window_start_ms_;
  if (window == 0) return;
  // A command still in flight belongs to the next window; utilization can only slightly undercount.
  this->publish_stat_("bus_utilization_percent", 100.0f * static_cast<float>(this->stats_busy_ms_) / window);
  if (this->stats_response_count_ > 0) {
    this->publish_stat_("bus_response_time_ms",
                        static_cast<float>(this->stats_response_ms_sum_) / this->stats_response_count_);
  }
  this->publish_stat_("bus_timeouts", static_cast<float>(this->stats_timeouts_total_));
  this->publish_stat_("bus_frame_errors", static_cast<float>(this->stats_frame_errors_total_));
  this->publish_stat_("bus_naks", static_cast<float>(this->stats_naks_total_));
  this->stats_window_start_ms_ = now;
  this->stats_busy_ms_ = 0;
  this->stats_response_ms_sum_ = 0;
  this->stats_response_count_ = 0;
}

void SolarPlugComponent::finish_frame_(const char *status) {
  this->account_command_(status);
  const char *kind = this->passive_mode_ ? "passive" : "active";
  const bool routine = std::strcmp(status, "supported") == 0;
  if (this->raw_frame_logging_) {
    ESP_LOGI(TAG, "%s command=%s status=%s response_hex=\"%s\" response_ascii=\"%s\"", kind,
             this->active_command_.c_str(), status,
             hex_encode_bytes(this->response_frame_.data(), this->response_frame_len_).c_str(),
             ascii_encode_bytes(this->response_frame_.data(), this->response_frame_len_).c_str());
  } else if (routine) {
    // Every poll lands here (~25/min); keep it out of INFO so it costs nothing at the default log level.
    ESP_LOGD(TAG, "%s command=%s status=%s", kind, this->active_command_.c_str(), status);
  } else if (this->active_kind_ == CommandKind::CRC_KEEPALIVE && std::strcmp(status, "ack") == 0) {
    ESP_LOGD(TAG, "%s command=%s status=%s", kind, this->active_command_.c_str(), status);  // every 30 s
  } else if (std::strcmp(status, "ack") == 0) {
    ESP_LOGI(TAG, "%s command=%s status=%s", kind, this->active_command_.c_str(), status);
  } else {
    ESP_LOGW(TAG, "%s command=%s status=%s", kind, this->active_command_.c_str(), status);
  }
  const bool failed = std::strcmp(status, "timeout") == 0 || std::strcmp(status, "crc_error") == 0 ||
                      std::strcmp(status, "invalid_frame") == 0;
  // Unsupported probe-only queries time out by design, so they are not retried.
  const bool retry = !this->passive_mode_ && failed && this->active_kind_ == CommandKind::READ &&
                     this->active_retry_count_ == 0 && !this->active_command_.empty() &&
                     !is_probe_only_command_(this->active_command_);
  if (failed && !this->passive_mode_) {
    // Drain a late or partial reply before the next request. A timeout already means the bus has been quiet
    // since the last byte, so the silence window counts from there.
    // After a timeout the bus has been quiet for the whole timeout already, so only a short drain window
    // (RESYNC_AFTER_TIMEOUT_MS) remains, for a reply that starts just as the timeout fires.
    this->resync_pending_ = true;
    this->resync_start_ms_ = millis();
    this->resync_last_rx_ms_ = std::strcmp(status, "timeout") == 0
                                   ? this->resync_start_ms_ - (RESYNC_SILENCE_MS - RESYNC_AFTER_TIMEOUT_MS)
                                   : this->resync_start_ms_;
  }
  if (this->active_probe_) {
    std::string reply = ascii_encode_bytes(this->response_frame_.data(), this->response_frame_len_);
    if (reply.size() > 200) reply.resize(200);  // HA state limit is 255
    this->publish_text_("_PROBE", "probe_result", this->active_command_ + " " + status + " " + reply);
  }
  if (std::strcmp(status, "ack") == 0 && this->active_kind_ != CommandKind::READ &&
      (this->active_command_ == "HOSTCRCEN" || this->active_command_ == "HOSTCRCDN")) {
    if (this->crc_mode_.set_mode(this->active_command_ == "HOSTCRCEN")) this->on_crc_mode_changed_("inverter acknowledged");
  }
  if (std::strcmp(status, "supported") == 0) this->reads_ok_since_keepalive_ = true;
  if (std::strcmp(status, "ack") == 0 && this->active_command_ == "HOSTCRCEN") this->crc_keepalive_failures_ = 0;
  if (this->active_kind_ == CommandKind::CRC_KEEPALIVE) {
    this->crc_keepalive_answered_ = true;
    // Only an unanswered keep-alive while reads still succeed says the firmware lacks the mode; during a bus
    // outage everything fails and the normal cadence must resume as soon as the inverter is back.
    const bool counts = std::strcmp(status, "ack") != 0 && this->reads_ok_since_keepalive_;
    this->reads_ok_since_keepalive_ = false;
    if (counts && this->crc_keepalive_failures_ < 255 && ++this->crc_keepalive_failures_ == codec::CRC_BACKOFF_AFTER) {
      ESP_LOGW(TAG, "HOSTCRCEN unanswered %u times; retrying every %u min", static_cast<unsigned>(codec::CRC_BACKOFF_AFTER),
               static_cast<unsigned>(codec::CRC_BACKOFF_MS / 60000));
    }
    this->publish_crc_state_();  // "Lost" if it was not acknowledged and the mode is off
  }
  if (this->active_kind_ == CommandKind::WRITE && !this->active_command_.empty()) {
    this->publish_write_status_(std::string(status) + " " + this->active_command_);
    this->on_write_finished_(this->active_command_, status);
  }
  if (retry) {
    ESP_LOGW(TAG, "retrying read-only command after %s command=%s", status, this->active_command_.c_str());
    if (!this->queue_pending_front_(
            {this->active_command_, read_frame_style_(this->active_command_), 1, CommandKind::READ, this->active_probe_})) {
      ESP_LOGW(TAG, "command queue full; retry dropped command=%s", this->active_command_.c_str());
    }
  }
  this->clear_response_frame_();
  this->active_command_.clear();
  this->active_retry_count_ = 0;
  this->active_kind_ = CommandKind::READ;
  this->active_probe_ = false;
  this->state_ = State::IDLE;
  this->activity_armed_ = false;
}

// Splits a complete frame (ending in CR) into payload and CRC. Every reply is checked: PI30 replies and
// ACK/NAKs always carry a CRC, H replies only in response-CRC mode (tracked by crc_mode_). A plain reply
// whose last two bytes happen to equal its CRC (1 in 65536) is handled by the tracker, not here.
bool SolarPlugComponent::extract_frame_payload_(std::string *payload_text, bool *crc_present) {
  if (payload_text == nullptr || this->response_frame_len_ == 0 ||
      this->response_frame_[this->response_frame_len_ - 1] != 0x0D) {
    return false;
  }

  size_t payload_len = this->response_frame_len_ - 1;
  bool crc = false;
  if (payload_len >= 3) {
    const size_t maybe_crc_payload_len = payload_len - 2;
    uint16_t crc_value = codec::crc16_xmodem(this->response_frame_.data(), maybe_crc_payload_len);
    auto [high, low] = codec::adjust_crc_bytes(crc_value);
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
      const char *unit = *field.unit == '\0' ? "-" : field.unit;
      const char *confidence = *field.confidence == '\0' ? "-" : field.confidence;
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
    else {
      if (field_name == "second_output") this->publish_write_switch_state_("second_output_power", value == "On");
      this->publish_numeric_(command, field_name, value);
      this->publish_text_(command, field_name, value);
    }
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
    else {
      this->publish_numeric_(command, field_name, value);  // decodes added later: sensor or text binding
      this->publish_text_(command, field_name, value);
    }
    return;
  }

  if (command == "HBAT") {
    if (field_name == "battery_series_count") {
      assign_float(this->latest_.hbat_battery_series_count);
      if (this->latest_.hbat_battery_series_count.has_value()) {
        const int volts = static_cast<int>(std::lround(*this->latest_.hbat_battery_series_count)) * 12;
        if (volts != this->detected_battery_voltage_ && volts > 0 && volts <= 96) {
          this->detected_battery_voltage_ = volts;
          if (volts != this->battery_voltage_) {
            ESP_LOGW(TAG, "inverter reports a %d V battery bank but battery_voltage is %d; voltage settings are refused",
                     volts, this->battery_voltage_);
          }
        }
      }
    }
    else if (field_name == "battery_voltage_v") assign_float(this->latest_.hbat_battery_voltage_v);
    else if (field_name == "battery_capacity_percent") assign_float(this->latest_.hbat_battery_capacity_percent);
    else if (field_name == "battery_charging_current_a") assign_float(this->latest_.hbat_battery_charging_current_a);
    else if (field_name == "battery_discharge_current_a") assign_float(this->latest_.hbat_battery_discharge_current_a);
    else if (field_name == "bus_voltage_v") assign_float(this->latest_.hbat_bus_voltage_v);
    else if (field_name == "charge_switches") this->publish_text_(command, field_name, value);
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
    else {
      this->publish_numeric_(command, field_name, value);  // decodes added later: sensor or text binding
      this->publish_text_(command, field_name, value);
    }
    return;
  }

  if (command == "HGEN") {
    if (field_name == "date_ymd") {
      assign_text(this->latest_.hgen_date_ymd);
      const int month = value.size() == 6 && is_digits(value) ? parse_digits(value, 2, 2) : 0;
      const int day = value.size() == 6 && is_digits(value) ? parse_digits(value, 4, 2) : 0;
      if (month >= 1 && month <= 12 && day >= 1 && day <= 31) {
        const uint32_t now = millis();
        auto mark = [now](PeriodGuard &guard, const std::string &key) {
          if (guard.period_key != key) {
            if (!guard.period_key.empty()) {  // first sight after boot is not a period change
              guard.period_change_ms = now;
              guard.period_changed = true;
            }
            guard.period_key = key;
          }
        };
        mark(this->daily_guard_, value);
        mark(this->monthly_guard_, value.substr(0, 4));
        mark(this->yearly_guard_, value.substr(0, 2));
      }
    }
    else if (field_name == "date_iso") assign_text(this->latest_.hgen_date_iso);
    else if (field_name == "time_hm") assign_text(this->latest_.hgen_time_hm);
    else if (field_name == "daily_power_gen_kwh") {
      if (this->hgen_value_plausible_(value, 0.0f, 200.0f, this->daily_guard_)) assign_float(this->latest_.hgen_daily_power_gen_kwh);
    } else if (field_name == "monthly_electricity_generation_kwh") {
      if (this->hgen_value_plausible_(value, 0.0f, 6000.0f, this->monthly_guard_))
        assign_float(this->latest_.hgen_monthly_electricity_generation_kwh);
    } else if (field_name == "yearly_electricity_generation_kwh") {
      if (this->hgen_value_plausible_(value, 0.0f, 75000.0f, this->yearly_guard_))
        assign_float(this->latest_.hgen_yearly_electricity_generation_kwh);
    } else if (field_name == "total_power_generation_kwh") {
      if (this->hgen_total_plausible_(value)) assign_float(this->latest_.hgen_total_power_generation_kwh);
    } else if (field_name == "raw_tail") assign_text(this->latest_.hgen_raw_tail);
    return;
  }

  if (command == "QPRTL") {
    if (field_name == "device_type") {
      assign_text(this->latest_.qprtl_device_type);
      this->note_machine_type_(value);
    }
    return;
  }

  if (command == "HIMSG1") {
    if (field_name == "software_version") assign_text(this->latest_.himsg1_software_version);
    else if (field_name == "software_date") assign_text(this->latest_.himsg1_software_date);
    else if (field_name == "software_date_iso") assign_text(this->latest_.himsg1_software_date_iso);
    else if (field_name == "revision") assign_text(this->latest_.himsg1_revision);
    else {
      this->publish_numeric_(command, field_name, value);  // decodes added later: sensor or text binding
      this->publish_text_(command, field_name, value);
    }
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
    else if (field_name == "bms_temperature_c") this->publish_numeric_(command, field_name, value);
    else if (field_name == "bms_flags_decoded") this->publish_text_(command, field_name, value);
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
    } else {
      this->publish_numeric_(command, field_name, value);  // token_NN and numeric named fields
      if (field_name.rfind("token_", 0) != 0) this->publish_text_(command, field_name, value);
    }
    return;
  }

  if (command == "HEEP2") {
    if (field_name == "raw_payload") {
      assign_text(this->latest_.heep2_raw_payload);
      this->publish_write_states_from_heep2_(value);
    } else {
      this->publish_numeric_(command, field_name, value);
      if (field_name.rfind("token_", 0) != 0) this->publish_text_(command, field_name, value);
    }
    return;
  }

  if (command == "HEEP3") {
    if (field_name == "raw_payload") this->publish_text_(command, field_name, value);
    else this->publish_numeric_(command, field_name, value);
    return;
  }

  if (command == "HIMSG2") {
    if (field_name == "raw_payload") this->publish_text_(command, field_name, value);
    else this->publish_numeric_(command, field_name, value);
    return;
  }

  if (command == "QMCHGCR" || command == "QMUCHGCR") {
    if (field_name != "raw_payload") return;
    // The inverter's own list of allowed MNCHGC / MUCHGC values. It may only NARROW the static envelope
    // (see writes::encode_select), must be ascending 1-200 A with at least 2 entries, and replaces the
    // stored list only after two identical replies, so one misattributed reply cannot change it.
    const bool max_list = command == "QMCHGCR";
    std::vector<int> parsed;
    bool shape_ok = true;
    for (const auto &token : split_tokens(value)) {
      if (token.empty() || !is_digits(token)) {
        shape_ok = false;
        break;
      }
      const int amps = std::atoi(token.c_str());
      if (amps < 1 || amps > 200 || (!parsed.empty() && amps <= parsed.back())) {
        shape_ok = false;
        break;
      }
      parsed.push_back(amps);
    }
    if (!shape_ok || parsed.size() < 2) {
      ESP_LOGW(TAG, "%s reply ignored (unexpected shape): %s", command.c_str(), value.c_str());
      return;
    }
    std::vector<int> &candidate = max_list ? this->candidate_max_charge_a_ : this->candidate_mains_charge_a_;
    std::vector<int> &list = max_list ? this->allowed_max_charge_a_ : this->allowed_mains_charge_a_;
    std::vector<int> usable;  // only what the configured envelope allows
    for (int amps : parsed) {
      if (this->in_charge_envelope_(!max_list, amps)) usable.push_back(amps);
    }
    if (candidate == parsed && list != usable && !usable.empty()) {
      list = usable;
      ESP_LOGI(TAG, "%s allowed list set: %s", command.c_str(), value.c_str());
      this->apply_charge_options_(!max_list);
      this->save_charge_lists_();
    } else if (candidate == parsed && usable.empty()) {
      ESP_LOGW(TAG, "%s announces no value inside the configured options (%s); keeping them", command.c_str(), value.c_str());
    }
    candidate = parsed;
    this->publish_text_(command, field_name, value);
    return;
  }

  if (command == "QPIRI" || command == "QFLAG" || command == "QBEQI") {
    if (field_name == "raw_payload") {
      if (command == "QPIRI") {
        assign_text(this->latest_.qpiri_raw_payload);
        this->publish_write_states_from_qpiri_(value);
      } else if (command == "QFLAG") {
        assign_text(this->latest_.qflag_raw_payload);
        this->publish_write_states_from_qflag_(value);
      } else {
        assign_text(this->latest_.qbeqi_raw_payload);
        this->publish_write_states_from_qbeqi_(value);
      }
    } else if (field_name.rfind("token_", 0) == 0) {
      this->publish_numeric_(command, field_name, value);
    } else {
      this->publish_numeric_(command, field_name, value);  // no-op for non-numeric values
      this->publish_text_(command, field_name, value);
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

// Period counters (daily/monthly/yearly) are published as total_increasing, where Home Assistant
// treats ANY decrease as a meter reset. So a counter may only go down when it rolls over to ~0;
// a dip that is not a rollover is held back unless it repeats on 3 consecutive polls.
bool SolarPlugComponent::hgen_value_plausible_(const std::string &value, float min_value, float max_value,
                                               PeriodGuard &guard) {
  static constexpr float ROLLOVER_MAX_KWH = 0.5f;
  static constexpr uint8_t CONFIRM_POLLS = 3;
  auto parsed = parse_optional_float(value);
  if (!parsed.has_value() || *parsed < min_value || *parsed > max_value) {
    ESP_LOGW(TAG, "HGEN value rejected as implausible: %s (allowed %.0f-%.0f)", value.c_str(), min_value, max_value);
    return false;
  }
  const bool drop = guard.last.has_value() && *parsed < *guard.last - 0.05f;
  // A drop to ~0 is a rollover only if this counter's period (day / month / year from the HGEN date)
  // changed within the last 2 h. Without a date we fall back to accepting it, as before.
  const uint32_t now = millis();
  static constexpr uint32_t WINDOW_MS = 2UL * 3600UL * 1000UL;
  if (guard.period_changed && now - guard.period_change_ms >= WINDOW_MS) {
    guard.period_changed = false;  // close the window, so it cannot reopen at the 49.7-day millis() wrap
  }
  bool rollover;
  if (guard.period_key.empty()) {
    rollover = *parsed <= ROLLOVER_MAX_KWH;  // no valid HGEN date seen: previous, permissive behaviour
  } else {
    // A drop to ~0 is a rollover only if this counter's period (day / month / year) differs from the one
    // of the last accepted value, or changed within the last 2 h.
    const bool period_moved = !guard.key_at_last.empty() && guard.period_key != guard.key_at_last;
    rollover = *parsed <= ROLLOVER_MAX_KWH && (period_moved || guard.period_changed);
  }
  if (!drop || rollover) {
    guard.key_at_last = guard.period_key;
    guard.last = *parsed;
    guard.hits = 0;
    return true;
  }
  if (guard.candidate.has_value() && std::fabs(*guard.candidate - *parsed) < 0.05f) {
    guard.hits++;
  } else {
    guard.candidate = *parsed;
    guard.hits = 1;
  }
  if (guard.hits >= CONFIRM_POLLS) {
    guard.key_at_last = guard.period_key;
    guard.last = *parsed;
    guard.hits = 0;
    guard.candidate.reset();
    return true;
  }
  ESP_LOGW(TAG, "HGEN period counter dip held back (%.3f -> %s kWh, %u/%u)", *guard.last, value.c_str(),
           static_cast<unsigned>(guard.hits), static_cast<unsigned>(CONFIRM_POLLS));
  return false;
}

// Lifetime PV counter guard. It feeds a total_increasing statistic, so one bad sample permanently
// corrupts Home Assistant's energy history (see issue #4: 36274176.0 kWh on a unit with no PV).
// Rules: hard cap; small forward steps accepted; a drop or a big jump is accepted only after it
// repeats on 3 consecutive polls (genuine reset or long ESP outage).
bool SolarPlugComponent::hgen_total_plausible_(const std::string &value) {
  static constexpr float HARD_CAP_KWH = 500000.0f;
  static constexpr float MAX_STEP_KWH = 50.0f;
  static constexpr uint8_t CONFIRM_POLLS = 3;
  auto parsed = parse_optional_float(value);
  if (!parsed.has_value() || *parsed < 0.0f || *parsed > HARD_CAP_KWH) {
    ESP_LOGW(TAG, "HGEN total rejected (out of range): %s", value.c_str());
    return false;
  }
  const auto &last = this->latest_.hgen_total_power_generation_kwh;
  if (!last.has_value()) {
    // First value after boot feeds a total_increasing statistic directly: require two matching polls.
    if (this->hgen_total_candidate_.has_value() && std::fabs(*this->hgen_total_candidate_ - *parsed) < 0.05f) {
      this->hgen_total_candidate_.reset();
      this->hgen_total_candidate_hits_ = 0;
      return true;
    }
    this->hgen_total_candidate_ = *parsed;
    this->hgen_total_candidate_hits_ = 1;
    return false;
  }
  if (*parsed >= *last - 0.05f && *parsed <= *last + MAX_STEP_KWH) {
    this->hgen_total_candidate_.reset();
    this->hgen_total_candidate_hits_ = 0;
    return true;
  }
  if (this->hgen_total_candidate_.has_value() && std::fabs(*this->hgen_total_candidate_ - *parsed) < 0.05f) {
    this->hgen_total_candidate_hits_++;
  } else {
    this->hgen_total_candidate_ = *parsed;
    this->hgen_total_candidate_hits_ = 1;
  }
  if (this->hgen_total_candidate_hits_ >= CONFIRM_POLLS) {
    ESP_LOGW(TAG, "HGEN total accepted after %u identical polls: %.1f -> %.1f kWh",
             static_cast<unsigned>(CONFIRM_POLLS), *last, *parsed);
    this->hgen_total_candidate_.reset();
    this->hgen_total_candidate_hits_ = 0;
    return true;
  }
  ESP_LOGW(TAG, "HGEN total held back (%.1f -> %s kWh, %u/%u)", *last, value.c_str(),
           static_cast<unsigned>(this->hgen_total_candidate_hits_), static_cast<unsigned>(CONFIRM_POLLS));
  return false;
}

bool SolarPlugComponent::command_has_consumer_(const std::string &command) const {
  if (this->raw_frame_logging_ || this->decoded_field_logging_) {
    return true;
  }
  for (const auto &binding : this->sensors_) {
    if (binding.command == command) return true;
  }
  for (const auto &binding : this->text_sensors_) {
    if (binding.command == command) return true;
  }
  if ((command == "HEEP1" || command == "HEEP2") && (!this->write_numbers_.empty() || !this->write_switches_.empty())) {
    return true;
  }
  if (command == "HEEP1" && !this->write_selects_.empty()) {
    return true;
  }
  if ((command == "QMCHGCR" || command == "QMUCHGCR") && !this->write_selects_.empty()) {  // charge-current options
    return true;
  }
  // The machine type (QPRTL) decides the priority codes, and the bank size (HBAT[0]) gates every voltage
  // setting, so any write entity needs both even when no sensor shows them.
  const bool any_write = !this->write_selects_.empty() || !this->write_numbers_.empty() || !this->write_switches_.empty();
  if ((command == "QPRTL" || command == "HBAT") && any_write) return true;
  if ((command == "QPIRI" || command == "QFLAG" || command == "QBEQI") &&
      (!this->write_selects_.empty() || !this->write_switches_.empty())) {
    return true;
  }
  return false;
}

void SolarPlugComponent::on_write_finished_(const std::string &frame, const char *status) {
  for (auto it = this->pending_write_effects_.begin(); it != this->pending_write_effects_.end(); ++it) {
    if (it->frame != frame) {
      continue;
    }
    // Read the settings back after every outcome: on a timeout or garbled reply the inverter may still
    // have applied the write, and HA must show what it actually stored. Pre-write HEEP1 values must not be
    // used for the cross-check, so drop them until the re-read arrives.
    this->heep_values_.clear();
    this->send_command("HEEP1");
    this->send_command("HEEP2");
    if (it->kind == WriteKind::SELECT) this->send_command("QPIRI");
    if (it->kind == WriteKind::SWITCH) {
      this->send_command("QFLAG");
      this->send_command("QBEQI");
      if (it->key == "second_output_power") this->send_command("HSTS");
    }
    if (std::strcmp(status, "ack") == 0) {
      switch (it->kind) {
        case WriteKind::SWITCH:
          this->publish_write_switch_state_(it->key, it->bool_value);
          break;
        case WriteKind::NUMBER:
          this->reported_numbers_[it->key] = it->float_value;  // the readback will confirm it
          this->publish_write_number_state_(it->key, it->float_value);
          break;
        case WriteKind::SELECT:
          this->publish_write_select_state_(it->key, it->text_value);
          break;
        case WriteKind::NONE:
          break;
      }
    } else {
      ESP_LOGW(TAG, "write %s for %s; Home Assistant keeps the previous value", status, it->key.c_str());
      // The inverter may or may not have applied it: dependent writes fail closed until the readback.
      if (it->kind == WriteKind::NUMBER) this->reported_numbers_.erase(it->key);
      this->republish_write_state_(it->kind, it->key);
    }
    this->pending_write_effects_.erase(it);
    return;
  }
}

// Re-publish an entity's unchanged state so the HA UI drops the value the user just typed.
void SolarPlugComponent::republish_write_state_(WriteKind kind, const std::string &key) {
  switch (kind) {
    case WriteKind::SWITCH:
      for (const auto &b : this->write_switches_)
        if (b.key == key && b.sensor != nullptr && b.seeded) b.sensor->publish_state(b.sensor->state);
      break;
    case WriteKind::NUMBER:
      for (const auto &b : this->write_numbers_)
        if (b.key == key && b.sensor != nullptr && b.sensor->has_state()) b.sensor->publish_state(b.sensor->state);
      break;
    case WriteKind::SELECT:
      for (const auto &b : this->write_selects_)
        if (b.key == key && b.sensor != nullptr && b.sensor->has_state())
          b.sensor->publish_state(std::string(b.sensor->current_option()));
      break;
    case WriteKind::NONE:
      break;
  }
}

// A write refused before anything was sent (range, ordering, a write in flight): say so where the user
// looks, and put the entity back to the inverter's value. The reason is in the log.
void SolarPlugComponent::refuse_write_(WriteKind kind, const std::string &key, const std::string &reason) {
  ESP_LOGW(TAG, "write %s refused: %s", key.c_str(), reason.c_str());
  this->publish_write_status_("refused " + key + ": " + reason);
  this->republish_write_state_(kind, key);
}

writes::Context SolarPlugComponent::write_context_() const {
  writes::Context context;
  context.reported = &this->reported_numbers_;
  context.allowed_max_charge_a = &this->allowed_max_charge_a_;
  context.allowed_mains_charge_a = &this->allowed_mains_charge_a_;
  context.writes_in_flight = this->writes_in_flight_();
  context.machine_type = this->machine_type_();
  context.machine_type_conflict = this->machine_type_conflict_();
  context.battery_voltage = this->battery_voltage_;
  context.detected_battery_voltage = this->detected_battery_voltage_;
  context.max_charge_envelope = &this->max_charge_envelope_;
  context.mains_charge_envelope = &this->mains_charge_envelope_;
  return context;
}

// The encoders (solarplug_writes.cpp) decide what may be sent; this only queues it. Every setter frame
// carries a CRC.
bool SolarPlugComponent::queue_encoded_write_(WriteKind kind, const std::string &key, const writes::Result &result) {
  if (!result.frame.has_value()) {
    this->refuse_write_(kind, key, result.reason);
    return false;
  }
  if (!this->queue_write_frame_(result.frame->label, result.frame->command, FrameStyle::CRC_XMODEM_CR)) {
    this->refuse_write_(kind, key, "writes disabled, passive mode or queue full");
    return false;
  }
  return true;
}

bool SolarPlugComponent::sync_clock() {
#ifdef USE_TIME
  if (this->time_ == nullptr) {
    ESP_LOGW(TAG, "sync_clock needs time_id");
    return false;
  }
  auto now = this->time_->now();
  if (!now.is_valid()) {
    ESP_LOGW(TAG, "sync_clock: time source not valid yet");
    return false;
  }
  char frame[32];
  std::snprintf(frame, sizeof(frame), "^S???DAT%02u%02u%02u%02u%02u%02u", static_cast<unsigned>(now.year % 100),
                static_cast<unsigned>(now.month), static_cast<unsigned>(now.day_of_month),
                static_cast<unsigned>(now.hour), static_cast<unsigned>(now.minute), static_cast<unsigned>(now.second));
  return this->queue_write_frame_("Inverter System Clock", frame, FrameStyle::CRC_XMODEM_CR);
#else
  ESP_LOGW(TAG, "sync_clock needs a time component");
  return false;
#endif
}

void SolarPlugComponent::process_complete_frame_() {
  std::string payload_text;
  bool crc_present = false;
  if (!this->extract_frame_payload_(&payload_text, &crc_present)) {
    this->finish_frame_("crc_error");
    return;
  }
  const bool expects_ack = this->active_kind_ != CommandKind::READ;  // setters and the CRC keep-alive
  switch (codec::classify_reply(this->active_command_, payload_text, crc_present, expects_ack)) {
    case codec::ReplyClass::ACK:
      this->finish_frame_("ack");
      return;
    case codec::ReplyClass::NAK:
      this->finish_frame_("nak");  // a rejected setter (unsupported reads time out instead)
      return;
    case codec::ReplyClass::INVALID:
      this->finish_frame_("invalid_frame");
      return;
    case codec::ReplyClass::DATA:
      break;
  }
  if (is_pi30_query_(this->active_command_)) {
    // PI30 replies always carry a CRC. Without it the two CRC bytes stay in the payload, and for QFLAG
    // they would be parsed as flag letters, so such a reply must not be decoded.
    if (!crc_present) {
      this->finish_frame_("crc_error");
      return;
    }
  } else {
    const auto verdict = this->crc_mode_.on_h_reply(crc_present);
    if (verdict.mode_changed) {
      this->on_crc_mode_changed_(this->crc_mode_.on() ? "3 H replies with a valid CRC" : "3 H replies without CRC");
    }
    if (!verdict.decode) {
      this->finish_frame_(this->crc_mode_.on() || !crc_present ? "crc_error" : "invalid_frame");
      return;
    }
  }
  if (!this->frame_shape_ok_(this->active_command_, payload_text, true)) {
    this->finish_frame_("invalid_frame");
    return;
  }
  this->log_decoded_payload_(this->active_command_, payload_text);
  this->finish_frame_("supported");
}

void SolarPlugComponent::on_crc_mode_changed_(const char *why) {
  // Expected transitions (boot, the probe actions) are info; losing the mode while it is wanted is a warning.
  if (!this->crc_mode_.on() && this->response_crc_) {
    ESP_LOGW(TAG, "response-CRC mode lost (%s); H replies are unverified until HOSTCRCEN is accepted", why);
  } else {
    ESP_LOGI(TAG, "response-CRC mode %s (%s)", this->crc_mode_.on() ? "on" : "off", why);
  }
  this->publish_crc_state_();
}

// The keep-alive jumps the queue so it precedes the reads that depend on it.
void SolarPlugComponent::queue_crc_keepalive_(uint32_t now) {
  if (!this->response_crc_) return;
  const bool refresh = this->crc_mode_.refresh_wanted();
  if (!codec::crc_keepalive_due(this->crc_keepalive_sent_, now - this->last_crc_keepalive_ms_, refresh,
                                this->crc_keepalive_failures_)) {
    return;
  }
  if (this->active_command_ == "HOSTCRCEN" || this->pending_command_exists_("HOSTCRCEN")) return;
  if (!this->queue_pending_front_({"HOSTCRCEN", FrameStyle::CRC_XMODEM_CR, 0, CommandKind::CRC_KEEPALIVE})) return;
  if (refresh) {
    ESP_LOGD(TAG, "H reply without CRC while the mode should be on; refreshing HOSTCRCEN");
  }
  this->crc_keepalive_sent_ = true;
  this->last_crc_keepalive_ms_ = now;
  this->crc_mode_.refresh_sent();
}

void SolarPlugComponent::publish_crc_state_() {
  const char *state = !this->response_crc_ ? (this->crc_mode_.on() ? "On (probe)" : "Off")
                      : this->crc_mode_.on() ? "On"
                      : this->crc_keepalive_answered_ ? "Lost"
                                                      : "Starting";
  if (this->crc_state_published_ == state) return;
  this->crc_state_published_ = state;
  this->publish_text_("_CRC", "response_crc", state);
}

bool SolarPlugComponent::frame_shape_ok_(const std::string &command, const std::string &payload, bool hold_length) {
  if (payload.empty() || payload[0] != '(') {
    ESP_LOGW(TAG, "dropping %s response without '(' prefix", command.c_str());
    return false;
  }
  if (!is_printable_ascii(payload)) {  // replies are printable ASCII; anything else is line noise
    ESP_LOGW(TAG, "dropping %s response with a non-printable byte", command.c_str());
    return false;
  }
  const size_t tokens = count_tokens_from(payload, 1);
  // Active polling holds each command to the length it has shown (a late reply credited to the next command
  // has the wrong length); a passive sniffer only checks the minimum.
  const bool ok = hold_length ? this->reply_lengths_.accept(command, tokens) : tokens >= min_tokens_for(command);
  if (!ok) {
    ESP_LOGW(TAG, "dropping %s response with %u tokens (%s)", command.c_str(), static_cast<unsigned>(tokens),
             tokens < min_tokens_for(command) ? "truncated" : "length changed; adopted if it repeats");
  }
  return ok;
}

void SolarPlugComponent::process_passive_frame_() {
  std::string payload_text;
  bool crc_present = false;
  if (!this->extract_frame_payload_(&payload_text, &crc_present)) {
    this->clear_response_frame_();
    this->observed_command_.clear();
    this->activity_armed_ = false;
    return;
  }

  if (payload_text.empty()) {
    this->clear_response_frame_();
    this->activity_armed_ = false;
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
    this->activity_armed_ = false;
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
    this->activity_armed_ = false;
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
  if (!this->frame_shape_ok_(this->active_command_, payload_text, false)) {
    this->finish_frame_("invalid_frame");
    return;
  }

  if (crc_present) {
    ESP_LOGD(TAG, "crc-framed response detected for command=%s", this->active_command_.c_str());
  }
  this->log_decoded_payload_(this->active_command_, payload_text);
  this->finish_frame_("supported");
}

// After a failed exchange, wait until the bus has been quiet for RESYNC_SILENCE_MS, draining whatever
// arrives, so a late reply is never credited to the next request.
bool SolarPlugComponent::resync_done_(uint32_t now) {
  if (!this->resync_pending_) return true;
  uint8_t buf[32];
  while (this->available() > 0) {
    const size_t n = std::min<size_t>(sizeof(buf), this->available());
    if (!this->read_array(buf, n)) break;
    this->resync_last_rx_ms_ = now;
  }
  if (now - this->resync_last_rx_ms_ < RESYNC_SILENCE_MS) {
    if (now - this->resync_start_ms_ < RESYNC_MAX_MS) return false;
    ESP_LOGW(TAG, "bus never went quiet for %u ms after a failed exchange; resuming polling",
             static_cast<unsigned>(RESYNC_SILENCE_MS));
  }
  this->resync_pending_ = false;
  return true;
}

void SolarPlugComponent::loop() {
  if (this->passive_mode_) {
    while (this->available() > 0) {
      uint8_t byte = 0;
      if (!this->read_byte(&byte)) {
        break;
      }
      if (this->response_frame_len_ == 0) {
        this->last_activity_ms_ = millis(); this->activity_armed_ = true;
      }
      if (!this->append_response_byte_(byte)) {
        ESP_LOGW(TAG, "frame overflow in passive mode");
        this->clear_response_frame_();
        this->observed_command_.clear();
        this->activity_armed_ = false;
        return;
      }
      this->last_activity_ms_ = millis(); this->activity_armed_ = true;
      if (byte == 0x0D) {
        this->process_passive_frame_();
        return;
      }
    }

    if (this->activity_armed_ && millis() - this->last_activity_ms_ >= this->response_timeout_ms_) {
      this->clear_response_frame_();
      this->observed_command_.clear();
      this->activity_armed_ = false;
    }
    return;
  }

  this->poll_due_groups_();
  {
    const uint32_t now = millis();
    if (!this->stats_window_started_) {
      this->stats_window_start_ms_ = now;
      this->stats_window_started_ = true;
    } else if (now - this->stats_window_start_ms_ >= 60000) {
      this->publish_stats_(now);
    }
  }

  PendingCommand pending;
  if (this->state_ == State::IDLE && !this->resync_done_(millis())) return;
  if (this->state_ == State::IDLE && this->pop_pending_command_(&pending)) {
    this->start_command_(pending);
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
      this->last_activity_ms_ = millis(); this->activity_armed_ = true;
    }
    if (!this->append_response_byte_(byte)) {
      ESP_LOGW(TAG, "frame overflow command=%s", this->active_command_.c_str());
      this->finish_frame_("crc_error");
      return;
    }
    this->last_activity_ms_ = millis(); this->activity_armed_ = true;
    if (byte == 0x0D) {
      this->process_complete_frame_();
      return;
    }
  }

  if (this->activity_armed_ && millis() - this->last_activity_ms_ >= this->response_timeout_ms_) {
    this->finish_frame_("timeout");
  }
}

}  // namespace solarplug
}  // namespace esphome
