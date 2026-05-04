import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button, number, select, sensor, switch, text_sensor, uart
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
    "daily_power_gen": ("HGEN", "daily_power_gen_kwh", UNIT_KILOWATT_HOURS, DEVICE_CLASS_ENERGY, STATE_CLASS_MEASUREMENT, 3, None),
    "monthly_electricity_generation": ("HGEN", "monthly_electricity_generation_kwh", UNIT_KILOWATT_HOURS, DEVICE_CLASS_ENERGY, STATE_CLASS_MEASUREMENT, 3, None),
    "yearly_electricity_generation": ("HGEN", "yearly_electricity_generation_kwh", UNIT_KILOWATT_HOURS, DEVICE_CLASS_ENERGY, STATE_CLASS_MEASUREMENT, 3, None),
    "total_power_generation": ("HGEN", "total_power_generation_kwh", UNIT_KILOWATT_HOURS, DEVICE_CLASS_ENERGY, STATE_CLASS_TOTAL_INCREASING, 3, None),
    "bms_soc": ("HBMS1", "bms_soc_percent", UNIT_PERCENT, DEVICE_CLASS_BATTERY, STATE_CLASS_MEASUREMENT, 0, ENTITY_CATEGORY_DIAGNOSTIC),
    "hpvb_pv_power": ("HPVB", "pv_power_w", UNIT_WATT, DEVICE_CLASS_POWER, STATE_CLASS_MEASUREMENT, 0, ENTITY_CATEGORY_DIAGNOSTIC),
}

TEXT_SENSOR_FIELDS = {
    "operating_mode": ("HSTS", "mode_label", None),
    "battery_type": ("HBAT", "battery_type", None),
    "device_type": ("QPRTL", "device_type", ENTITY_CATEGORY_DIAGNOSTIC),
    "software_version": ("HIMSG1", "software_version", ENTITY_CATEGORY_DIAGNOSTIC),
    "generation_date": ("HGEN", "date_iso", None),
    "generation_time": ("HGEN", "time_hm", None),
    "mains_current_flow_direction": ("HGRID", "mains_current_flow_direction", None),
    "hsts_status_bits": ("HSTS", "status_bits", ENTITY_CATEGORY_DIAGNOSTIC),
    "heep1_raw_payload": ("HEEP1", "raw_payload", ENTITY_CATEGORY_DIAGNOSTIC),
    "heep2_raw_payload": ("HEEP2", "raw_payload", ENTITY_CATEGORY_DIAGNOSTIC),
    "hpvb_raw_payload": ("HPVB", "raw_payload", ENTITY_CATEGORY_DIAGNOSTIC),
    "last_write_status": ("_WRITE", "last_write_status", ENTITY_CATEGORY_DIAGNOSTIC),
}

BUTTON_GROUPS = {
    "refresh_fast": "fast",
    "refresh_energy": "energy",
    "refresh_identity": "identity",
    "refresh_all": "all",
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
}

WRITE_NUMBERS = {
    "battery_equalization_voltage": ("battery_equalization_voltage", UNIT_VOLT, DEVICE_CLASS_VOLTAGE, 48.0, 60.0, 0.1),
    "battery_equalization_interval": ("battery_equalization_interval", "day", None, 0.0, 90.0, 1.0),
    "battery_equalization_timeout": ("battery_equalization_timeout", "min", None, 5.0, 900.0, 5.0),
    "battery_equalization_time": ("battery_equalization_time", "min", None, 5.0, 900.0, 5.0),
    "battery_cut_off_voltage": ("battery_cut_off_voltage", UNIT_VOLT, DEVICE_CLASS_VOLTAGE, 40.0, 60.0, 0.1),
    "battery_bulk_voltage": ("battery_bulk_voltage", UNIT_VOLT, DEVICE_CLASS_VOLTAGE, 48.0, 58.4, 0.1),
    "battery_recharge_voltage": ("battery_recharge_voltage", UNIT_VOLT, DEVICE_CLASS_VOLTAGE, 44.0, 51.0, 1.0),
    "battery_redischarge_voltage": ("battery_redischarge_voltage", UNIT_VOLT, DEVICE_CLASS_VOLTAGE, 0.0, 58.0, 1.0),
    "battery_float_voltage": ("battery_float_voltage", UNIT_VOLT, DEVICE_CLASS_VOLTAGE, 48.0, 58.4, 0.1),
    "restore_second_output_battery_capacity": ("restore_second_output_battery_capacity", UNIT_PERCENT, DEVICE_CLASS_BATTERY, 0.0, 50.0, 1.0),
    "restore_second_output_delay_time": ("restore_second_output_delay_time", "min", None, 0.0, 60.0, 5.0),
    "bms_lock_machine_battery_capacity": ("bms_lock_machine_battery_capacity", UNIT_PERCENT, DEVICE_CLASS_BATTERY, 0.0, 20.0, 1.0),
    "grid_connected_current": ("grid_connected_current", UNIT_AMPERE, DEVICE_CLASS_CURRENT, 0.0, 150.0, 1.0),
    "maximum_mains_charging_current": ("maximum_mains_charging_current", UNIT_AMPERE, DEVICE_CLASS_CURRENT, 0.0, 60.0, 10.0),
    "maximum_charging_current": ("maximum_charging_current", UNIT_AMPERE, DEVICE_CLASS_CURRENT, 10.0, 120.0, 10.0),
}

UNSAFE_WRITE_NUMBERS = {
    "battery_cut_off_voltage",
    "battery_bulk_voltage",
    "battery_float_voltage",
}

WRITE_SELECTS = {
    "charger_priority": ("CSO", "SNU", "OSO"),
    "output_source_priority": ("SUB priority", "SBU priority", "Utility first (legacy)"),
    "pv_energy_feeding_priority": ("BLU", "LBU"),
    "battery_type": ("LIA", "PYL"),
}


def sensor_config_schema(field):
    _, _, unit, device_class, state_class, decimals, entity_category = field
    kwargs = {
        "unit_of_measurement": unit,
        "accuracy_decimals": decimals,
        "state_class": state_class,
    }
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
    unsafe_numbers = UNSAFE_WRITE_NUMBERS.intersection(config.get(CONF_NUMBERS, {}))
    if unsafe_numbers and config.get(CONF_WRITE_PROFILE) != WRITE_PROFILE_UNSAFE:
        names = ", ".join(sorted(unsafe_numbers))
        raise cv.Invalid(f"{names} require write_profile: unsafe because live probes were NAK-only or unsupported")
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
        write_key, _, _, min_value, max_value, step = WRITE_NUMBERS[key]
        num = await number.new_number(conf, min_value=min_value, max_value=max_value, step=step)
        cg.add(num.set_parent(parent))
        cg.add(num.set_write_key(write_key))
        cg.add(parent.register_write_number(write_key, num))


async def register_configured_selects(parent, config):
    for key, conf in config.get(CONF_SELECTS, {}).items():
        sel = cg.new_Pvariable(conf[CONF_ID])
        await select.register_select(sel, conf, options=list(WRITE_SELECTS[key]))
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
    cg.add(var.set_boot_poll_delay_ms(config[CONF_BOOT_POLL_DELAY].total_milliseconds))
    await register_configured_sensors(var, config)
    await register_configured_text_sensors(var, config)
    await register_configured_buttons(var, config)
    await register_configured_switches(var, config)
    await register_configured_numbers(var, config)
    await register_configured_selects(var, config)
