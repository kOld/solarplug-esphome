import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button, number, select, sensor, switch, text_sensor, time, uart
from esphome.const import (
    CONF_ENTITY_CATEGORY,
    CONF_ID,
    DEVICE_CLASS_APPARENT_POWER,
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_ENERGY,
    DEVICE_CLASS_FREQUENCY,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_VOLTAGE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    ENTITY_CATEGORY_CONFIG,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_AMPERE,
    UNIT_CELSIUS,
    UNIT_HERTZ,
    UNIT_PERCENT,
    UNIT_VOLT,
    UNIT_WATT,
)

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["button", "number", "select", "sensor", "switch", "text_sensor"]
MULTI_CONF = True

CONF_ENABLE_WRITES = "enable_writes"
CONF_RESPONSE_TIMEOUT_MS = "response_timeout_ms"
CONF_PASSIVE_MODE = "passive_mode"
CONF_FAST_POLL_INTERVAL = "fast_poll_interval"
CONF_ENERGY_POLL_INTERVAL = "energy_poll_interval"
CONF_IDENTITY_POLL_INTERVAL = "identity_poll_interval"
CONF_SETTINGS_POLL_INTERVAL = "settings_poll_interval"
CONF_BOOT_POLL_DELAY = "boot_poll_delay"
CONF_WRITE_PROFILE = "write_profile"
CONF_RAW_FRAME_LOGGING = "raw_frame_logging"
CONF_DECODED_FIELD_LOGGING = "decoded_field_logging"
CONF_SENSORS = "sensors"
CONF_TEXT_SENSORS = "text_sensors"
CONF_BUTTONS = "buttons"
CONF_SWITCHES = "switches"
CONF_NUMBERS = "numbers"
CONF_SELECTS = "selects"
CONF_EXTENDED_SELECT_OPTIONS = "extended_select_options"
CONF_TIME_ID = "time_id"
CONF_RESPONSE_CRC = "response_crc"
CONF_MACHINE_TYPE = "machine_type"
CONF_BATTERY_VOLTAGE = "battery_voltage"
CONF_CHARGING_CURRENT_OPTIONS = "charging_current_options"
CONF_MAINS_CHARGING_CURRENT_OPTIONS = "mains_charging_current_options"

# codec::MachineType values; "auto" reads QPRTL (see docs/protocol/COMPATIBILITY.md).
MACHINE_TYPES = {"auto": -1, "UNKNOWN": 0, "A": 1, "B": 2, "C": 3, "D": 4, "F": 5}
# Machine types A / C (HPVINV01 / 03): POP00 is utility first and there is no PEC or SOR (vendor tool tables).
PRIORITY_OPTIONS_AC = {
    "output_source_priority": ("Utility first (UTI)", "SUB priority", "SBU priority"),
    "charger_priority": ("CSO", "SNU", "OSO"),
}
DEFAULT_CHARGING_CURRENTS = list(range(10, 81, 10))  # HPVINV02 QMCHGCR
DEFAULT_MAINS_CHARGING_CURRENTS = [2] + list(range(10, 61, 10))  # HPVINV02 QMUCHGCR


def redischarge_options(battery_voltage):
    """Program 13 options: Full, then 48-58 V scaled to the bank. Mirrors writes::redischarge_options."""
    options = ["Full"]
    for volts in range(48, 59):
        value = volts * battery_voltage / 48
        options.append(f"{value:.0f} V" if abs(value - round(value)) < 0.01 else f"{value:.1f} V")
    return options


def scale_voltage_traits(min_value, max_value, step, battery_voltage):
    """48 V ranges scaled to the bank; the frame format resolves 0.1 V. Mirrors writes::encode_number."""
    scale = battery_voltage / 48
    return round(min_value * scale, 2), round(max_value * scale, 2), max(0.1, round(step * scale, 2))


def validate_current_list(value):
    value = cv.ensure_list(cv.int_range(min=1, max=200))(value)
    if len(value) < 2 or sorted(set(value)) != value:
        raise cv.Invalid("charging current options must be at least 2 ascending, distinct values")
    return value

UNIT_KILOWATT = "kW"
UNIT_KILOWATT_HOURS = "kWh"
UNIT_VOLT_AMPS = "VA"

WRITE_PROFILE_READ_ONLY = "read_only"
WRITE_PROFILE_BETA = "beta"
WRITE_PROFILE_UNSAFE = "unsafe"

solarplug_ns = cg.esphome_ns.namespace("solarplug")
SolarPlugComponent = solarplug_ns.class_("SolarPlugComponent", cg.Component, uart.UARTDevice)
SolarPlugButton = solarplug_ns.class_("SolarPlugButton", button.Button)
SolarPlugWriteSwitch = solarplug_ns.class_("SolarPlugWriteSwitch", switch.Switch)
SolarPlugWriteNumber = solarplug_ns.class_("SolarPlugWriteNumber", number.Number)
SolarPlugWriteSelect = solarplug_ns.class_("SolarPlugWriteSelect", select.Select)


SENSOR_FIELDS = {
    "ac_input_voltage": ("HGRID", "ac_input_voltage_v", UNIT_VOLT, DEVICE_CLASS_VOLTAGE, STATE_CLASS_MEASUREMENT, 1, None),
    "mains_frequency": ("HGRID", "mains_frequency_hz", UNIT_HERTZ, DEVICE_CLASS_FREQUENCY, STATE_CLASS_MEASUREMENT, 1, None),
    "mains_power": ("HGRID", "mains_power_w", UNIT_WATT, DEVICE_CLASS_POWER, STATE_CLASS_MEASUREMENT, 0, None),
    "ac_output_voltage": ("HOP", "ac_output_voltage_v", UNIT_VOLT, DEVICE_CLASS_VOLTAGE, STATE_CLASS_MEASUREMENT, 1, None),
    "ac_output_frequency": ("HOP", "ac_output_frequency_hz", UNIT_HERTZ, DEVICE_CLASS_FREQUENCY, STATE_CLASS_MEASUREMENT, 1, None),
    "output_apparent_power": ("HOP", "output_apparent_power_va", UNIT_VOLT_AMPS, DEVICE_CLASS_APPARENT_POWER, STATE_CLASS_MEASUREMENT, 0, None),
    "output_active_power": ("HOP", "output_active_power_w", UNIT_WATT, DEVICE_CLASS_POWER, STATE_CLASS_MEASUREMENT, 0, None),
    "output_load": ("HOP", "output_load_percent", UNIT_PERCENT, None, STATE_CLASS_MEASUREMENT, 0, None),
    "pv_voltage": ("HPV", "pv_voltage_v", UNIT_VOLT, DEVICE_CLASS_VOLTAGE, STATE_CLASS_MEASUREMENT, 1, None),
    "pv_current": ("HPV", "pv_current_a", UNIT_AMPERE, DEVICE_CLASS_CURRENT, STATE_CLASS_MEASUREMENT, 1, None),
    "pv_power": ("HPV", "pv_power_w", UNIT_WATT, DEVICE_CLASS_POWER, STATE_CLASS_MEASUREMENT, 0, None),
    "pv_generation_power": ("HPV", "generation_power_kw", UNIT_KILOWATT, DEVICE_CLASS_POWER, STATE_CLASS_MEASUREMENT, 3, ENTITY_CATEGORY_DIAGNOSTIC),
    "inverter_temperature": ("HTEMP", "inverter_temperature_c", UNIT_CELSIUS, DEVICE_CLASS_TEMPERATURE, STATE_CLASS_MEASUREMENT, 1, None),
    "boost_temperature": ("HTEMP", "boost_temperature_c", UNIT_CELSIUS, DEVICE_CLASS_TEMPERATURE, STATE_CLASS_MEASUREMENT, 1, None),
    "transformer_temperature": ("HTEMP", "transformer_temperature_c", UNIT_CELSIUS, DEVICE_CLASS_TEMPERATURE, STATE_CLASS_MEASUREMENT, 1, None),
    "pv_temperature": ("HTEMP", "pv_temperature_c", UNIT_CELSIUS, DEVICE_CLASS_TEMPERATURE, STATE_CLASS_MEASUREMENT, 1, None),
    "fan_1_speed": ("HTEMP", "fan_1_speed_percent", UNIT_PERCENT, None, STATE_CLASS_MEASUREMENT, 0, None),
    "fan_2_speed": ("HTEMP", "fan_2_speed_percent", UNIT_PERCENT, None, STATE_CLASS_MEASUREMENT, 0, None),
    "battery_voltage": ("HBAT", "battery_voltage_v", UNIT_VOLT, DEVICE_CLASS_VOLTAGE, STATE_CLASS_MEASUREMENT, 1, None),
    "battery_capacity": ("HBAT", "battery_capacity_percent", UNIT_PERCENT, DEVICE_CLASS_BATTERY, STATE_CLASS_MEASUREMENT, 0, None),
    "battery_charging_current": ("HBAT", "battery_charging_current_a", UNIT_AMPERE, DEVICE_CLASS_CURRENT, STATE_CLASS_MEASUREMENT, 1, None),
    "battery_discharge_current": ("HBAT", "battery_discharge_current_a", UNIT_AMPERE, DEVICE_CLASS_CURRENT, STATE_CLASS_MEASUREMENT, 1, None),
    "bus_voltage": ("HBAT", "bus_voltage_v", UNIT_VOLT, DEVICE_CLASS_VOLTAGE, STATE_CLASS_MEASUREMENT, 1, None),
    "daily_power_gen": ("HGEN", "daily_power_gen_kwh", UNIT_KILOWATT_HOURS, DEVICE_CLASS_ENERGY, STATE_CLASS_TOTAL_INCREASING, 3, None),
    "monthly_electricity_generation": ("HGEN", "monthly_electricity_generation_kwh", UNIT_KILOWATT_HOURS, DEVICE_CLASS_ENERGY, STATE_CLASS_TOTAL_INCREASING, 3, None),
    "yearly_electricity_generation": ("HGEN", "yearly_electricity_generation_kwh", UNIT_KILOWATT_HOURS, DEVICE_CLASS_ENERGY, STATE_CLASS_TOTAL_INCREASING, 3, None),
    "total_power_generation": ("HGEN", "total_power_generation_kwh", UNIT_KILOWATT_HOURS, DEVICE_CLASS_ENERGY, STATE_CLASS_TOTAL_INCREASING, 3, None),
    "bms_soc": ("HBMS1", "bms_soc_percent", UNIT_PERCENT, DEVICE_CLASS_BATTERY, STATE_CLASS_MEASUREMENT, 0, ENTITY_CATEGORY_DIAGNOSTIC),
    "hpvb_pv_power": ("HPVB", "pv_power_w", UNIT_WATT, DEVICE_CLASS_POWER, STATE_CLASS_MEASUREMENT, 0, ENTITY_CATEGORY_DIAGNOSTIC),
    # HBMS1: what the inverter receives from the BMS over the PYL link (CVL / CCL / DVL).
    "bms_charge_voltage_limit": ("HBMS1", "bms_charge_voltage_limit_v", UNIT_VOLT, DEVICE_CLASS_VOLTAGE, STATE_CLASS_MEASUREMENT, 1, ENTITY_CATEGORY_DIAGNOSTIC),
    "bms_discharge_voltage_limit": ("HBMS1", "bms_discharge_voltage_limit_v", UNIT_VOLT, DEVICE_CLASS_VOLTAGE, STATE_CLASS_MEASUREMENT, 1, ENTITY_CATEGORY_DIAGNOSTIC),
    "bms_charge_current_limit": ("HBMS1", "bms_charge_current_limit_a", UNIT_AMPERE, DEVICE_CLASS_CURRENT, STATE_CLASS_MEASUREMENT, 1, ENTITY_CATEGORY_DIAGNOSTIC),
    "bms_charging_current": ("HBMS1", "bms_charging_current_a", UNIT_AMPERE, DEVICE_CLASS_CURRENT, STATE_CLASS_MEASUREMENT, 1, ENTITY_CATEGORY_DIAGNOSTIC),
    "bms_discharge_current": ("HBMS1", "bms_discharge_current_a", UNIT_AMPERE, DEVICE_CLASS_CURRENT, STATE_CLASS_MEASUREMENT, 1, ENTITY_CATEGORY_DIAGNOSTIC),
    # HBMS2: min/max cell summary as forwarded to the inverter.
    "bms_max_cell_voltage": ("HBMS2", "max_voltage", "mV", DEVICE_CLASS_VOLTAGE, STATE_CLASS_MEASUREMENT, 0, ENTITY_CATEGORY_DIAGNOSTIC),
    "bms_min_cell_voltage": ("HBMS2", "min_voltage", "mV", DEVICE_CLASS_VOLTAGE, STATE_CLASS_MEASUREMENT, 0, ENTITY_CATEGORY_DIAGNOSTIC),
    # VENDOR label only; values do not track output power (107 W @ 177 W, 102 W @ 529 W, 449 W @ ~400 W), so
    # no state_class: it should not feed long-term statistics until its meaning is confirmed.
    "inductor_power": ("HOP", "inductor_power_w", UNIT_WATT, DEVICE_CLASS_POWER, None, 0, ENTITY_CATEGORY_DIAGNOSTIC),
    "rated_inverter_power": ("HIMSG2", "rated_inverter_power_w", UNIT_WATT, DEVICE_CLASS_POWER, None, 0, ENTITY_CATEGORY_DIAGNOSTIC),
    "rated_mppt_power": ("HIMSG2", "rated_mppt_power_w", UNIT_WATT, DEVICE_CLASS_POWER, None, 0, ENTITY_CATEGORY_DIAGNOSTIC),
    "rated_ac_power": ("HIMSG2", "rated_ac_power_w", UNIT_WATT, DEVICE_CLASS_POWER, None, 0, ENTITY_CATEGORY_DIAGNOSTIC),
    "second_output_discharge_time": ("HEEP2", "second_output_discharge_time_min", "min", None, STATE_CLASS_MEASUREMENT, 0, ENTITY_CATEGORY_DIAGNOSTIC),
    # HPVINV07 / 08 only (vendor tool); polled only on those models.
    "grid_power_limit": ("HEEP3", "grid_power_limit_w", UNIT_WATT, DEVICE_CLASS_POWER, None, 0, ENTITY_CATEGORY_DIAGNOSTIC),
    "discharge_current_limit": ("HEEP3", "discharge_current_limit_a", UNIT_AMPERE, DEVICE_CLASS_CURRENT, None, 0, ENTITY_CATEGORY_DIAGNOSTIC),
    "ct_current": ("HCTMSG1", "ct_current_a", UNIT_AMPERE, DEVICE_CLASS_CURRENT, STATE_CLASS_MEASUREMENT, 1, None),
    "ct_power": ("HCTMSG1", "ct_power_w", UNIT_WATT, DEVICE_CLASS_POWER, STATE_CLASS_MEASUREMENT, 0, None),
    "ct_zero_adjust_power": ("HEEP3", "ct_zero_adjust_power_w", UNIT_WATT, DEVICE_CLASS_POWER, None, 0, ENTITY_CATEGORY_DIAGNOSTIC),
    "bms_temperature": ("HBMS1", "bms_temperature_c", UNIT_CELSIUS, DEVICE_CLASS_TEMPERATURE, STATE_CLASS_MEASUREMENT, 2, ENTITY_CATEGORY_DIAGNOSTIC),
    # HEEP1 settings snapshot (1-based token index; candidate names from FIELD_MAP + live cross-check 2026-11-16).
    "heep_bms_lock_soc": ("HEEP1", "token_11", UNIT_PERCENT, None, STATE_CLASS_MEASUREMENT, 0, ENTITY_CATEGORY_DIAGNOSTIC),
    "heep_soc_back_to_grid": ("HEEP1", "token_12", UNIT_PERCENT, None, STATE_CLASS_MEASUREMENT, 0, ENTITY_CATEGORY_DIAGNOSTIC),
    "heep_soc_back_to_battery": ("HEEP1", "token_13", UNIT_PERCENT, None, STATE_CLASS_MEASUREMENT, 0, ENTITY_CATEGORY_DIAGNOSTIC),
    "heep_token_14_soc": ("HEEP1", "token_14", UNIT_PERCENT, None, STATE_CLASS_MEASUREMENT, 0, ENTITY_CATEGORY_DIAGNOSTIC),
    "heep_bulk_voltage": ("HEEP1", "token_15", UNIT_VOLT, DEVICE_CLASS_VOLTAGE, STATE_CLASS_MEASUREMENT, 1, ENTITY_CATEGORY_DIAGNOSTIC),
    "heep_float_voltage": ("HEEP1", "token_16", UNIT_VOLT, DEVICE_CLASS_VOLTAGE, STATE_CLASS_MEASUREMENT, 1, ENTITY_CATEGORY_DIAGNOSTIC),
    "heep_low_cutoff_voltage": ("HEEP1", "token_17", UNIT_VOLT, DEVICE_CLASS_VOLTAGE, STATE_CLASS_MEASUREMENT, 1, ENTITY_CATEGORY_DIAGNOSTIC),
    # HEEP2 second-output (L2) thresholds.
    "heep_second_output_cutoff_voltage": ("HEEP2", "token_02", UNIT_VOLT, DEVICE_CLASS_VOLTAGE, STATE_CLASS_MEASUREMENT, 1, ENTITY_CATEGORY_DIAGNOSTIC),
    "heep_low_battery_alarm_voltage": ("HEEP2", "token_04", UNIT_VOLT, DEVICE_CLASS_VOLTAGE, STATE_CLASS_MEASUREMENT, 1, ENTITY_CATEGORY_DIAGNOSTIC),
    "heep_second_output_cutoff_soc": ("HEEP2", "token_03", UNIT_PERCENT, None, STATE_CLASS_MEASUREMENT, 0, ENTITY_CATEGORY_DIAGNOSTIC),
    # Program 63, unique 52.0 match against the portal's secondOutputBatteryVoltage (heep_alignment run).
    "heep_second_output_recovery_voltage": ("HEEP2", "token_16", UNIT_VOLT, DEVICE_CLASS_VOLTAGE, STATE_CLASS_MEASUREMENT, 1, ENTITY_CATEGORY_DIAGNOSTIC),
    # QPIRI settings as sensors (output voltage / frequency are writable only in the unsafe profile).
    "setting_output_voltage": ("QPIRI", "output_voltage_setting_v", UNIT_VOLT, DEVICE_CLASS_VOLTAGE, STATE_CLASS_MEASUREMENT, 0, ENTITY_CATEGORY_DIAGNOSTIC),
    "setting_output_frequency": ("QPIRI", "output_frequency_setting_hz", UNIT_HERTZ, DEVICE_CLASS_FREQUENCY, STATE_CLASS_MEASUREMENT, 0, ENTITY_CATEGORY_DIAGNOSTIC),
    "setting_battery_under_voltage": ("QPIRI", "battery_under_voltage_v", UNIT_VOLT, DEVICE_CLASS_VOLTAGE, STATE_CLASS_MEASUREMENT, 1, ENTITY_CATEGORY_DIAGNOSTIC),
    # Bus profiling, published every 60 s by the component itself.
    "bus_utilization": ("_STATS", "bus_utilization_percent", UNIT_PERCENT, None, STATE_CLASS_MEASUREMENT, 1, ENTITY_CATEGORY_DIAGNOSTIC),
    "bus_response_time": ("_STATS", "bus_response_time_ms", "ms", None, STATE_CLASS_MEASUREMENT, 0, ENTITY_CATEGORY_DIAGNOSTIC),
    "bus_timeouts": ("_STATS", "bus_timeouts", None, None, STATE_CLASS_TOTAL_INCREASING, 0, ENTITY_CATEGORY_DIAGNOSTIC),
    "bus_frame_errors": ("_STATS", "bus_frame_errors", None, None, STATE_CLASS_TOTAL_INCREASING, 0, ENTITY_CATEGORY_DIAGNOSTIC),
    "bus_naks": ("_STATS", "bus_naks", None, None, STATE_CLASS_TOTAL_INCREASING, 0, ENTITY_CATEGORY_DIAGNOSTIC),
}

TEXT_SENSOR_FIELDS = {
    "operating_mode": ("HSTS", "mode_label", None),
    "battery_type": ("HEEP1", "battery_type", None),  # HEEP1[3] digit 2 (HBAT[0] is the series count)
    "grid_protocol_mode": ("HEEP1", "grid_protocol_mode", ENTITY_CATEGORY_DIAGNOSTIC),
    "ct_function": ("HEEP1", "ct_function", ENTITY_CATEGORY_DIAGNOSTIC),
    "bms_communication": ("HEEP1", "bms_communication", ENTITY_CATEGORY_DIAGNOSTIC),
    "grid_feed": ("HEEP1", "grid_feed", ENTITY_CATEGORY_DIAGNOSTIC),
    "device_type": ("QPRTL", "device_type", ENTITY_CATEGORY_DIAGNOSTIC),
    "software_version": ("HIMSG1", "software_version", ENTITY_CATEGORY_DIAGNOSTIC),
    "generation_date": ("HGEN", "date_iso", None),
    "generation_time": ("HGEN", "time_hm", None),
    "mains_current_flow_direction": ("HGRID", "mains_current_flow_direction", None),
    "hsts_status_bits": ("HSTS", "status_bits", ENTITY_CATEGORY_DIAGNOSTIC),
    "heep1_raw_payload": ("HEEP1", "raw_payload", ENTITY_CATEGORY_DIAGNOSTIC),
    "heep2_raw_payload": ("HEEP2", "raw_payload", ENTITY_CATEGORY_DIAGNOSTIC),
    "hpvb_raw_payload": ("HPVB", "raw_payload", ENTITY_CATEGORY_DIAGNOSTIC),
    "bms_status_code": ("HBMS1", "bms_status_code", ENTITY_CATEGORY_DIAGNOSTIC),
    "bms_flags": ("HBMS1", "bms_flags", ENTITY_CATEGORY_DIAGNOSTIC),
    "hbms1_raw_tail": ("HBMS1", "raw_tail", ENTITY_CATEGORY_DIAGNOSTIC),
    "hsts_fault_bits": ("HSTS", "fault_bits", ENTITY_CATEGORY_DIAGNOSTIC),
    "inverter_alarms": ("HSTS", "alarms", None),
    "fault_code": ("HSTS", "fault_code", None),
    "fault_description": ("HSTS", "fault_description", None),
    "front_panel_leds": ("HSTS", "front_panel_leds", ENTITY_CATEGORY_DIAGNOSTIC),
    "fan_enable": ("HTEMP", "fan_enable", ENTITY_CATEGORY_DIAGNOSTIC),
    "software_integrity": ("HIMSG1", "software_integrity", ENTITY_CATEGORY_DIAGNOSTIC),
    "himsg2_raw_payload": ("HIMSG2", "raw_payload", ENTITY_CATEGORY_DIAGNOSTIC),
    "heep3_raw_payload": ("HEEP3", "raw_payload", ENTITY_CATEGORY_DIAGNOSTIC),
    "allowed_charge_currents": ("QMCHGCR", "raw_payload", ENTITY_CATEGORY_DIAGNOSTIC),
    "allowed_mains_charge_currents": ("QMUCHGCR", "raw_payload", ENTITY_CATEGORY_DIAGNOSTIC),
    "grid_tie_state": ("HSTS", "grid_tie_state", ENTITY_CATEGORY_DIAGNOSTIC),
    "main_output_relay": ("HSTS", "main_output_relay", ENTITY_CATEGORY_DIAGNOSTIC),
    "second_output": ("HSTS", "second_output", None),
    "charge_switches": ("HBAT", "charge_switches", ENTITY_CATEGORY_DIAGNOSTIC),
    "bms_status": ("HBMS1", "bms_flags_decoded", ENTITY_CATEGORY_DIAGNOSTIC),
    "qpiri_raw_payload": ("QPIRI", "raw_payload", ENTITY_CATEGORY_DIAGNOSTIC),
    "readback_cross_check": ("_CHECK", "readback_cross_check", ENTITY_CATEGORY_DIAGNOSTIC),
    # Machine type used for the priority codes, e.g. "B (HPVINV02)".
    "machine_type": ("_MODEL", "machine_type", ENTITY_CATEGORY_DIAGNOSTIC),
    # HSTS2 (HPVINV08 only): power-flow flags and the raw inverter-state digits.
    "power_flow": ("HSTS2", "power_flow", None),
    "inverter_flags": ("HSTS2", "inverter_flags", ENTITY_CATEGORY_DIAGNOSTIC),
    "ac_output_mode": ("QPIRI", "ac_output_mode", ENTITY_CATEGORY_DIAGNOSTIC),
    "qflag_raw_payload": ("QFLAG", "raw_payload", ENTITY_CATEGORY_DIAGNOSTIC),
    "qbeqi_raw_payload": ("QBEQI", "raw_payload", ENTITY_CATEGORY_DIAGNOSTIC),
    "last_write_status": ("_WRITE", "last_write_status", ENTITY_CATEGORY_DIAGNOSTIC),
    # HEEP2[0] program 60 (vendor: 0 Off, 1 On; LCD unconfirmed) and the raw time-window slots.
    "dual_output_mode": ("HEEP2", "dual_output_mode", ENTITY_CATEGORY_DIAGNOSTIC),
    "ac_charge_window_raw": ("HEEP2", "ac_charge_window_raw", ENTITY_CATEGORY_DIAGNOSTIC),
    "ac_output_window_raw": ("HEEP2", "ac_output_window_raw", ENTITY_CATEGORY_DIAGNOSTIC),
    "output_timer_raw": ("HEEP2", "output_timer_raw", ENTITY_CATEGORY_DIAGNOSTIC),
    # HSTS[2] d0 / d2: parallel system size and this unit's role (raw).
    "parallel_units": ("HSTS", "parallel_units", ENTITY_CATEGORY_DIAGNOSTIC),
    "parallel_role": ("HSTS", "parallel_role", ENTITY_CATEGORY_DIAGNOSTIC),
    # Raw reply of the last probe_read / probe_write API action.
    "probe_result": ("_PROBE", "probe_result", ENTITY_CATEGORY_DIAGNOSTIC),
    # Response-CRC mode: On / Starting / Lost / Off (see response_crc).
    "response_crc": ("_CRC", "response_crc", ENTITY_CATEGORY_DIAGNOSTIC),
}

BUTTON_GROUPS = {
    "refresh_fast": "fast",
    "refresh_energy": "energy",
    "refresh_identity": "identity",
    "refresh_settings": "settings",
    "refresh_all": "all",
    "sync_clock": "sync_clock",  # write; needs enable_writes + time_id
}

WRITE_SWITCHES = {
    "buzzer": "buzzer",
    "backlight": "backlight",
    "display_return_homepage": "display_return_homepage",
    "over_temperature_restart": "over_temperature_restart",
    "overload_restart": "overload_restart",
    "input_source_detection_prompt_sound": "input_source_detection_prompt_sound",
    "battery_equalization_mode_enable": "battery_equalization_mode_enable",
    "bms_function_enable": "bms_function_enable",
    # PI30 PE/PD flags read back through QFLAG (programs 23, 08 and 25).
    "overload_bypass": "overload_bypass",
    "power_saving": "power_saving",
    "fault_code_record": "fault_code_record",
    # Vendor HPVINV02 screen setters, never bus-tested here: write_profile unsafe only.
    "dual_output_mode": "dual_output_mode",  # PDAULC, readback HEEP2[0]
    "second_output_power": "second_output_power",  # PDAULON, switches L2 now; readback HSTS[2] d4
    "grid_feed": "grid_feed",  # GRIDFEED, readback HEEP1[18]
    "ct_function": "ct_function",  # EXTCT, readback HEEP1[3] d7
}

UNSAFE_WRITE_SWITCHES = {"dual_output_mode", "second_output_power", "grid_feed", "ct_function"}

WRITE_NUMBERS = {
    "battery_equalization_voltage": ("battery_equalization_voltage", UNIT_VOLT, DEVICE_CLASS_VOLTAGE, 48.0, 60.0, 0.1),
    "battery_equalization_interval": ("battery_equalization_interval", "day", None, 0.0, 90.0, 1.0),
    "battery_equalization_timeout": ("battery_equalization_timeout", "min", None, 5.0, 900.0, 5.0),
    "battery_equalization_time": ("battery_equalization_time", "min", None, 5.0, 900.0, 5.0),
    "battery_cut_off_voltage": ("battery_cut_off_voltage", UNIT_VOLT, DEVICE_CLASS_VOLTAGE, 40.0, 52.0, 0.1),  # program 29
    "battery_bulk_voltage": ("battery_bulk_voltage", UNIT_VOLT, DEVICE_CLASS_VOLTAGE, 48.0, 58.4, 0.1),
    "battery_recharge_voltage": ("battery_recharge_voltage", UNIT_VOLT, DEVICE_CLASS_VOLTAGE, 44.0, 51.0, 1.0),
    "battery_float_voltage": ("battery_float_voltage", UNIT_VOLT, DEVICE_CLASS_VOLTAGE, 48.0, 58.4, 0.1),
    "restore_second_output_battery_capacity": ("restore_second_output_battery_capacity", UNIT_PERCENT, DEVICE_CLASS_BATTERY, 0.0, 50.0, 5.0),  # SOC values: multiples of 5
    "restore_second_output_delay_time": ("restore_second_output_delay_time", "min", None, 0.0, 60.0, 5.0),
    "bms_lock_machine_battery_capacity": ("bms_lock_machine_battery_capacity", UNIT_PERCENT, DEVICE_CLASS_BATTERY, 0.0, 20.0, 5.0),
    "grid_connected_current": ("grid_connected_current", UNIT_AMPERE, DEVICE_CLASS_CURRENT, 0.0, 30.0, 1.0),  # rated 26.9 A; only 20/21 A probed
    # Setters from the vendor HPVINV02 screen; readback HEEP1[11..13] / HEEP2[1..3,15]. SOC: multiples of 5.
    "soc_back_to_mains": ("soc_back_to_mains", UNIT_PERCENT, DEVICE_CLASS_BATTERY, 5.0, 95.0, 5.0),
    "soc_back_to_battery": ("soc_back_to_battery", UNIT_PERCENT, DEVICE_CLASS_BATTERY, 10.0, 100.0, 5.0),
    "inverter_restart_soc": ("inverter_restart_soc", UNIT_PERCENT, DEVICE_CLASS_BATTERY, 5.0, 95.0, 5.0),
    "second_output_cutoff_soc": ("second_output_cutoff_soc", UNIT_PERCENT, DEVICE_CLASS_BATTERY, 0.0, 95.0, 5.0),
    "low_battery_alarm_voltage": ("low_battery_alarm_voltage", UNIT_VOLT, DEVICE_CLASS_VOLTAGE, 40.0, 54.0, 0.1),
    "second_output_cutoff_voltage": ("second_output_cutoff_voltage", UNIT_VOLT, DEVICE_CLASS_VOLTAGE, 40.0, 54.0, 0.1),
    "second_output_recovery_voltage": ("second_output_recovery_voltage", UNIT_VOLT, DEVICE_CLASS_VOLTAGE, 44.0, 58.0, 0.1),
    "second_output_discharge_time": ("second_output_discharge_time", "min", None, 0.0, 990.0, 5.0),  # program 65; 0 vs "Disable" unconfirmed
    # ^S???ACCT / ^S???ACLT hour windows (vendor format HH-HH); readback HEEP2[11] / [12], layout tentative.
    "ac_charge_start_hour": ("ac_charge_start_hour", "h", None, 0.0, 24.0, 1.0),
    "ac_charge_stop_hour": ("ac_charge_stop_hour", "h", None, 0.0, 24.0, 1.0),
    "ac_output_start_hour": ("ac_output_start_hour", "h", None, 0.0, 24.0, 1.0),
    "ac_output_stop_hour": ("ac_output_stop_hour", "h", None, 0.0, 24.0, 1.0),
}

UNSAFE_WRITE_NUMBERS = {
    "battery_cut_off_voltage",
    "battery_bulk_voltage",
    "battery_float_voltage",
    # NAK'd on HPVINV02 fw 40.05 with a lithium battery type, even at the current value (2026-11-16).
    "low_battery_alarm_voltage",
    "second_output_cutoff_voltage",
    "second_output_recovery_voltage",
    # Never bus-tested; the readback layout of HEEP2[11]/[12] is a guess until LCD experiment E1.
    "ac_charge_start_hour",
    "ac_charge_stop_hour",
    "ac_output_start_hour",
    "ac_output_stop_hour",
}

# Options bus-confirmed on the PowMr POW-HVM6.2KP (HPVINV02, fw 40.05). See docs/protocol/WRITE_SURFACE.md.
WRITE_SELECTS = {
    "charger_priority": ("CSO", "SNU", "OSO"),
    "output_source_priority": ("SUB priority", "SBU priority"),
    "pv_energy_feeding_priority": ("BLU", "LBU"),
    "battery_type": ("LIA", "PYL"),
    # Program 03; PGR00 / PGR01, read back from QPIRI field 16.
    "ac_input_voltage_range": ("Appliance", "UPS"),
    # Programs 02 / 11. Defaults = this unit's QMCHGCR / QMUCHGCR lists; at runtime the options follow the
    # inverter's announced list (within the same envelope), persisted across reboots.
    "maximum_charging_current": tuple(f"{a} A" for a in range(10, 81, 10)),
    "maximum_mains_charging_current": ("2 A",) + tuple(f"{a} A" for a in range(10, 61, 10)),
    # Program 13: back to battery when fully charged, or at 48-58 V (a slider would offer 1-47 V, never valid).
    "battery_redischarge_voltage": ("Full",) + tuple(f"{v} V" for v in range(48, 59)),
    # Unsafe profile: output topology and grid compliance (vendor HPVINV02 screen; never bus-tested).
    "output_frequency": ("50 Hz", "60 Hz"),  # F50 / F60, readback HEEP1[4] d0
    "output_voltage": ("220 V", "230 V", "240 V"),  # V220..V240, readback HEEP1[3] d8-10
    "output_mode": ("SIG", "PAL", "3P1", "3P2", "3P3"),  # POPM00..04, readback HEEP1[8]
    "grid_protocol": ("Mode 1", "Mode 2", "Mode 3", "Mode 4", "Mode 5"),  # ^S???RS00..04, HEEP1[3] d1
}

UNSAFE_WRITE_SELECTS = {"output_frequency", "output_voltage", "output_mode", "grid_protocol"}

# Options added for Easun SMT (PR #3). Not verified on PowMr: PCP03 / POP03 have no ACK+readback
# evidence there, and the extra PBTxx battery profiles change charging behaviour. Opt in with
# `extended_select_options: true` on hardware where they are known to work.
EXTENDED_SELECT_OPTIONS = {
    "charger_priority": ("SOR",),
    # PEC: Easun SMT and types B / D / F. Machine types A / C use PRIORITY_OPTIONS_AC instead.
    "output_source_priority": ("PEC Mode (CT)",),
    "battery_type": ("AGM", "FLD", "USE", "TQF", "GRO", "LIB", "LIC", "FEL"),
}


def sensor_config_schema(field):
    _, _, unit, device_class, state_class, decimals, entity_category = field
    kwargs = {"accuracy_decimals": decimals}
    if state_class is not None:
        kwargs["state_class"] = state_class
    if unit is not None:
        kwargs["unit_of_measurement"] = unit
    if device_class is not None:
        kwargs["device_class"] = device_class
    if entity_category is not None:
        kwargs["entity_category"] = entity_category
    return sensor.sensor_schema(**kwargs)


def text_sensor_config_schema(field):
    _, _, entity_category = field
    kwargs = {}
    if entity_category is not None:
        kwargs["entity_category"] = entity_category
    return text_sensor.text_sensor_schema(**kwargs)


def validate_write_config(config):
    has_writes = bool(config.get(CONF_SWITCHES)) or bool(config.get(CONF_NUMBERS)) or bool(config.get(CONF_SELECTS))
    if has_writes and not config.get(CONF_ENABLE_WRITES):
        raise cv.Invalid("write entities require enable_writes: true; keep production configs read-only")
    if has_writes and config.get(CONF_PASSIVE_MODE):
        raise cv.Invalid("write entities require passive_mode: false")
    if has_writes and config.get(CONF_WRITE_PROFILE) == WRITE_PROFILE_READ_ONLY:
        raise cv.Invalid("write entities require write_profile: beta or unsafe")
    if config.get(CONF_RESPONSE_CRC) and config.get(CONF_PASSIVE_MODE):
        raise cv.Invalid("response_crc sends HOSTCRCEN; it requires passive_mode: false")
    if "sync_clock" in config.get(CONF_BUTTONS, {}):
        if not config.get(CONF_ENABLE_WRITES) or config.get(CONF_PASSIVE_MODE):
            raise cv.Invalid("sync_clock sets the inverter clock; it requires enable_writes: true and passive_mode: false")
        if CONF_TIME_ID not in config:
            raise cv.Invalid("sync_clock requires time_id")
    unsafe = (
        UNSAFE_WRITE_NUMBERS.intersection(config.get(CONF_NUMBERS, {}))
        | UNSAFE_WRITE_SWITCHES.intersection(config.get(CONF_SWITCHES, {}))
        | UNSAFE_WRITE_SELECTS.intersection(config.get(CONF_SELECTS, {}))
    )
    if unsafe and config.get(CONF_WRITE_PROFILE) != WRITE_PROFILE_UNSAFE:
        names = ", ".join(sorted(unsafe))
        raise cv.Invalid(
            f"{names} require write_profile: unsafe (NAK-only, never bus-tested, or able to cut the output; "
            "see docs/protocol/WRITE_SURFACE.md)"
        )
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(SolarPlugComponent),
            cv.Optional(CONF_ENABLE_WRITES, default=False): cv.boolean,
            cv.Optional(CONF_RESPONSE_TIMEOUT_MS, default="1500ms"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_PASSIVE_MODE, default=True): cv.boolean,
            cv.Optional(CONF_FAST_POLL_INTERVAL, default="20s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_ENERGY_POLL_INTERVAL, default="60s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_IDENTITY_POLL_INTERVAL, default="30min"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_SETTINGS_POLL_INTERVAL, default="5min"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_BOOT_POLL_DELAY, default="10s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_WRITE_PROFILE, default=WRITE_PROFILE_READ_ONLY): cv.enum(
                {
                    WRITE_PROFILE_READ_ONLY: WRITE_PROFILE_READ_ONLY,
                    WRITE_PROFILE_BETA: WRITE_PROFILE_BETA,
                    WRITE_PROFILE_UNSAFE: WRITE_PROFILE_UNSAFE,
                },
                lower=True,
            ),
            cv.Optional(CONF_RAW_FRAME_LOGGING, default=False): cv.boolean,
            cv.Optional(CONF_DECODED_FIELD_LOGGING, default=False): cv.boolean,
            cv.Optional(CONF_EXTENDED_SELECT_OPTIONS, default=False): cv.boolean,
            # Keep the inverter in response-CRC mode so every H reply is CRC-checked (HPVINV02 fw 40.05).
            cv.Optional(CONF_RESPONSE_CRC, default=False): cv.boolean,
            # Model profile for inverters other than the verified HPVINV02 48 V setup.
            cv.Optional(CONF_MACHINE_TYPE, default="auto"): cv.one_of(*MACHINE_TYPES, upper=False),
            cv.Optional(CONF_BATTERY_VOLTAGE, default=48): cv.one_of(24, 48, int=True),
            cv.Optional(CONF_CHARGING_CURRENT_OPTIONS, default=DEFAULT_CHARGING_CURRENTS): validate_current_list,
            cv.Optional(CONF_MAINS_CHARGING_CURRENT_OPTIONS, default=DEFAULT_MAINS_CHARGING_CURRENTS): validate_current_list,
            cv.Optional(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
            cv.Optional(CONF_SENSORS): cv.Schema(
                {cv.Optional(key): sensor_config_schema(field) for key, field in SENSOR_FIELDS.items()}
            ),
            cv.Optional(CONF_TEXT_SENSORS): cv.Schema(
                {cv.Optional(key): text_sensor_config_schema(field) for key, field in TEXT_SENSOR_FIELDS.items()}
            ),
            cv.Optional(CONF_BUTTONS): cv.Schema(
                {cv.Optional(key): button.button_schema(SolarPlugButton, entity_category=ENTITY_CATEGORY_DIAGNOSTIC) for key in BUTTON_GROUPS}
            ),
            cv.Optional(CONF_SWITCHES): cv.Schema(
                {cv.Optional(key): switch.switch_schema(SolarPlugWriteSwitch, entity_category=ENTITY_CATEGORY_CONFIG) for key in WRITE_SWITCHES}
            ),
            cv.Optional(CONF_NUMBERS): cv.Schema(
                {
                    cv.Optional(key): number.number_schema(
                        SolarPlugWriteNumber,
                        entity_category=ENTITY_CATEGORY_CONFIG,
                        unit_of_measurement=field[1],
                        device_class=field[2] if field[2] is not None else cv.UNDEFINED,
                    )
                    for key, field in WRITE_NUMBERS.items()
                }
            ),
            cv.Optional(CONF_SELECTS): cv.Schema(
                {
                    cv.Optional(key): select.select_schema(SolarPlugWriteSelect, entity_category=ENTITY_CATEGORY_CONFIG).extend(cv.COMPONENT_SCHEMA)
                    for key in WRITE_SELECTS
                }
            ),
        }
    ).extend(uart.UART_DEVICE_SCHEMA),
    validate_write_config,
)


async def register_configured_sensors(parent, config):
    for key, conf in config.get(CONF_SENSORS, {}).items():
        command, field, *_ = SENSOR_FIELDS[key]
        sens = await sensor.new_sensor(conf)
        cg.add(parent.register_sensor(command, field, sens))


async def register_configured_text_sensors(parent, config):
    for key, conf in config.get(CONF_TEXT_SENSORS, {}).items():
        command, field, _ = TEXT_SENSOR_FIELDS[key]
        sens = await text_sensor.new_text_sensor(conf)
        cg.add(parent.register_text_sensor(command, field, sens))


async def register_configured_buttons(parent, config):
    for key, conf in config.get(CONF_BUTTONS, {}).items():
        btn = await button.new_button(conf)
        cg.add(btn.set_parent(parent))
        cg.add(btn.set_command_group(BUTTON_GROUPS[key]))


async def register_configured_switches(parent, config):
    for key, conf in config.get(CONF_SWITCHES, {}).items():
        sw = await switch.new_switch(conf)
        cg.add(sw.set_parent(parent))
        cg.add(sw.set_write_key(WRITE_SWITCHES[key]))
        cg.add(parent.register_write_switch(WRITE_SWITCHES[key], sw))


async def register_configured_numbers(parent, config):
    for key, conf in config.get(CONF_NUMBERS, {}).items():
        write_key, unit, _, min_value, max_value, step = WRITE_NUMBERS[key]
        if unit == UNIT_VOLT:
            min_value, max_value, step = scale_voltage_traits(min_value, max_value, step, config[CONF_BATTERY_VOLTAGE])
        num = await number.new_number(conf, min_value=min_value, max_value=max_value, step=step)
        cg.add(num.set_parent(parent))
        cg.add(num.set_write_key(write_key))
        cg.add(parent.register_write_number(write_key, num))


async def register_configured_selects(parent, config):
    extended = config[CONF_EXTENDED_SELECT_OPTIONS]
    type_ac = config[CONF_MACHINE_TYPE] in ("A", "C")
    for key, conf in config.get(CONF_SELECTS, {}).items():
        options = list(WRITE_SELECTS[key])
        if type_ac and key in PRIORITY_OPTIONS_AC:
            options = list(PRIORITY_OPTIONS_AC[key])
        if key == "maximum_charging_current":
            options = [f"{a} A" for a in config[CONF_CHARGING_CURRENT_OPTIONS]]
        elif key == "maximum_mains_charging_current":
            options = [f"{a} A" for a in config[CONF_MAINS_CHARGING_CURRENT_OPTIONS]]
        elif key == "battery_redischarge_voltage":
            options = redischarge_options(config[CONF_BATTERY_VOLTAGE])
        if extended and not (type_ac and key in PRIORITY_OPTIONS_AC):
            options += list(EXTENDED_SELECT_OPTIONS.get(key, ()))
        sel = cg.new_Pvariable(conf[CONF_ID])
        await select.register_select(sel, conf, options=options)
        cg.add(sel.set_parent(parent))
        cg.add(sel.set_write_key(key))
        cg.add(parent.register_write_select(key, sel))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    cg.add(var.set_enable_writes(config[CONF_ENABLE_WRITES]))
    cg.add(var.set_response_timeout_ms(config[CONF_RESPONSE_TIMEOUT_MS].total_milliseconds))
    cg.add(var.set_passive_mode(config[CONF_PASSIVE_MODE]))
    cg.add(var.set_raw_frame_logging(config[CONF_RAW_FRAME_LOGGING]))
    cg.add(var.set_decoded_field_logging(config[CONF_DECODED_FIELD_LOGGING]))
    cg.add(var.set_fast_poll_interval_ms(config[CONF_FAST_POLL_INTERVAL].total_milliseconds))
    cg.add(var.set_energy_poll_interval_ms(config[CONF_ENERGY_POLL_INTERVAL].total_milliseconds))
    cg.add(var.set_identity_poll_interval_ms(config[CONF_IDENTITY_POLL_INTERVAL].total_milliseconds))
    cg.add(var.set_settings_poll_interval_ms(config[CONF_SETTINGS_POLL_INTERVAL].total_milliseconds))
    cg.add(var.set_boot_poll_delay_ms(config[CONF_BOOT_POLL_DELAY].total_milliseconds))
    cg.add(var.set_response_crc(config[CONF_RESPONSE_CRC]))
    cg.add(var.set_machine_type(MACHINE_TYPES[config[CONF_MACHINE_TYPE]]))
    cg.add(var.set_battery_voltage(config[CONF_BATTERY_VOLTAGE]))
    cg.add(var.set_charge_current_envelope(config[CONF_CHARGING_CURRENT_OPTIONS], config[CONF_MAINS_CHARGING_CURRENT_OPTIONS]))
    if CONF_TIME_ID in config:
        time_var = await cg.get_variable(config[CONF_TIME_ID])
        cg.add(var.set_time(time_var))
    await register_configured_sensors(var, config)
    await register_configured_text_sensors(var, config)
    await register_configured_buttons(var, config)
    await register_configured_switches(var, config)
    await register_configured_numbers(var, config)
    await register_configured_selects(var, config)
