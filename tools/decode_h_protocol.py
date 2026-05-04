#!/usr/bin/env python3
"""Decode Solar Plug H-command sniffer logs.

The script accepts either:
- a saved sniffer log / markdown file containing `SNIFF ...` lines, or
- a live `esphome logs ...` session.

It emits:
- frames.csv
- frames.jsonl
- decoded_state.json
- command_responses.jsonl
- report.md
- unpaired_frames.md

This public tool is offline-only. It never logs into vendor cloud services and
never needs account credentials.
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import json
import re
import subprocess
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass
from decimal import Decimal, InvalidOperation
from pathlib import Path
from typing import Any, Iterable

READ_ONLY_H_COMMANDS = [
    "HSTS",
    "HTEMP",
    "HBMS1",
    "HBMS2",
    "HBMS3",
    "QPRTL",
    "HIMSG1",
    "HGEN",
    "HBAT",
    "HOP",
    "HGRID",
    "HPV",
]

ASCII_REQUEST_COMMANDS = set(READ_ONLY_H_COMMANDS) | {
    "HSTS",
    "HTEMP",
    "HEEP1",
    "HBMS1",
    "HEEP2",
    "HPVB",
    "HBMS2",
    "HBMS3",
}

CRC_REFERENCE_COMMANDS = {
    "PCP00",
    "PCP01",
    "PCP02",
    "POP00",
    "POP01",
    "POP02",
    "PVENGUSE00",
    "PVENGUSE01",
}

CLOCK_WRITE_RE = re.compile(r"^\^S(?P<selector>.{3})DAT(?P<clock>\d{12})$")
BATTERY_EQUALIZATION_VOLTAGE_WRITE_RE = re.compile(r"^PBEQV(?P<value>\d+\.\d{2})$")
BATTERY_EQUALIZATION_INTERVAL_WRITE_RE = re.compile(r"^PBEQP(?P<value>\d{3})$")
BATTERY_EQUALIZATION_TIMEOUT_WRITE_RE = re.compile(r"^PBEQOT(?P<value>\d{3})$")
BATTERY_EQUALIZATION_TIME_WRITE_RE = re.compile(r"^PBEQT(?P<value>\d{3})$")
BATTERY_CUT_OFF_VOLTAGE_WRITE_RE = re.compile(r"^PSDV(?P<value>\d{1,2}\.\d)$")
BATTERY_BULK_VOLTAGE_WRITE_RE = re.compile(r"^PCVV(?P<value>\d+\.\d{1,2})$")
BATTERY_RECHARGE_VOLTAGE_WRITE_RE = re.compile(r"^PBCV(?P<value>\d+\.\d{1,2})$")
BATTERY_REDISCHARGE_VOLTAGE_WRITE_RE = re.compile(r"^PBDV(?P<value>\d+\.\d{1,2})$")
BATTERY_TYPE_WRITE_RE = re.compile(r"^PBT(?P<value>\d{2})$")
BMSC_WRITE_RE = re.compile(r"^BMSC(?P<value>\d{2})$")
BMSSDC_WRITE_RE = re.compile(r"^BMSSDC(?P<value>\d{3})$")
GRID_CONNECTED_CURRENT_WRITE_RE = re.compile(r"^PGFC(?P<value>\d{3})$")
MAXIMUM_MAINS_CHARGING_CURRENT_WRITE_RE = re.compile(r"^MUCHGC(?P<value>\d{3})$")
MAXIMUM_CHARGING_CURRENT_WRITE_RE = re.compile(r"^M(?:N)?CHGC(?P<value>\d{3})$")
BATTERY_FLOAT_CHARGING_VOLTAGE_WRITE_RE = re.compile(r"^PBFT(?P<value>\d+\.\d{1,2})$")
RESTORE_SECOND_OUTPUT_BATTERY_CAPACITY_WRITE_RE = re.compile(r"^PDSRS(?P<value>\d{3})$")
RESTORE_SECOND_OUTPUT_DELAY_TIME_WRITE_RE = re.compile(r"^PDDLYT(?P<value>\d{3})$")

BATTERY_TYPE_VALUE_LABELS = {
    "03": "LIA",
    "04": "PYL",
}

BMSC_VALUE_LABELS = {
    "00": "Off",
    "01": "On",
}

WRITE_CONTROL_COMMANDS = {
    "PDa": {
        "portal_key": "buzzerOn",
        "portal_label": "Buzzer On",
        "portal_value": "Disable",
        "request_frame_style": "crc_cr",
        "heep1_token_6_effect": 0,
        "ack_response": "(ACK9 ",
        "response_example": "(ACK9 ",
        "decoded_fields": [
            "portal_key",
            "portal_label",
            "portal_value",
            "ack_observed",
            "ack_response",
            "heep1_token_6_effect",
        ],
        "confidence": "high",
        "sot_keys": ["Buzzer On", "buzzerOn"],
    },
    "PEa": {
        "portal_key": "buzzerOn",
        "portal_label": "Buzzer On",
        "portal_value": "Enable",
        "request_frame_style": "crc_cr",
        "heep1_token_6_effect": 1,
        "ack_response": "(ACK9 ",
        "response_example": "(ACK9 ",
        "decoded_fields": [
            "portal_key",
            "portal_label",
            "portal_value",
            "ack_observed",
            "ack_response",
            "heep1_token_6_effect",
        ],
        "confidence": "high",
        "sot_keys": ["Buzzer On", "buzzerOn"],
    },
    "PDx`Y": {
        "portal_key": "backlightOn",
        "portal_label": "Backlight On",
        "portal_value": "Disable",
        "request_frame_style": "ascii_cr",
        "heep1_token_6_effect": 0,
        "ack_response": "(ACK9 ",
        "response_example": "(ACK9 ",
        "decoded_fields": [
            "portal_key",
            "portal_label",
            "portal_value",
            "ack_observed",
            "ack_response",
            "heep1_token_6_effect",
        ],
        "confidence": "high",
        "sot_keys": ["Backlight On", "backlightOn"],
    },
    "PExSh": {
        "portal_key": "backlightOn",
        "portal_label": "Backlight On",
        "portal_value": "Enable",
        "request_frame_style": "ascii_cr",
        "heep1_token_6_effect": 1,
        "ack_response": "(ACK9 ",
        "response_example": "(ACK9 ",
        "decoded_fields": [
            "portal_key",
            "portal_label",
            "portal_value",
            "ack_observed",
            "ack_response",
            "heep1_token_6_effect",
        ],
        "confidence": "high",
        "sot_keys": ["Backlight On", "backlightOn"],
    },
    "PDk": {
        "portal_key": "displayAutomaticallyReturnsToHomepage",
        "portal_label": "Display Automatically Returns To Homepage",
        "portal_value": "Disable",
        "request_frame_style": "crc_cr",
        "heep1_token_5_effect": "012 -> 002",
        "ack_response": "(ACK9 ",
        "response_example": "(ACK9 ",
        "decoded_fields": [
            "portal_key",
            "portal_label",
            "portal_value",
            "ack_observed",
            "ack_response",
            "heep1_token_5_effect",
        ],
        "confidence": "high",
        "sot_keys": ["Display Automatically Returns To Homepage", "displayAutomaticallyReturnsToHomepage"],
    },
    "PEkq:": {
        "portal_key": "displayAutomaticallyReturnsToHomepage",
        "portal_label": "Display Automatically Returns To Homepage",
        "portal_value": "Enable",
        "request_frame_style": "ascii_cr",
        "heep1_token_5_effect": "002 -> 012",
        "ack_response": "(ACK9 ",
        "response_example": "(ACK9 ",
        "decoded_fields": [
            "portal_key",
            "portal_label",
            "portal_value",
            "ack_observed",
            "ack_response",
            "heep1_token_5_effect",
        ],
        "confidence": "high",
        "sot_keys": ["Display Automatically Returns To Homepage", "displayAutomaticallyReturnsToHomepage"],
    },
    "PDv": {
        "portal_key": "overTemperatureAutomaticRestart",
        "portal_label": "Over Temperature Automatic Restart",
        "portal_value": "Disable",
        "request_frame_style": "crc_cr",
        "ack_response": "(ACK9 ",
        "response_example": "(ACK9 ",
        "decoded_fields": [
            "portal_key",
            "portal_label",
            "portal_value",
            "ack_observed",
            "ack_response",
        ],
        "confidence": "high",
        "sot_keys": ["Over Temperature Automatic Restart", "overTemperatureAutomaticRestart"],
    },
    "PEv": {
        "portal_key": "overTemperatureAutomaticRestart",
        "portal_label": "Over Temperature Automatic Restart",
        "portal_value": "Enable",
        "request_frame_style": "crc_cr",
        "ack_response": "(ACK9 ",
        "response_example": "(ACK9 ",
        "decoded_fields": [
            "portal_key",
            "portal_label",
            "portal_value",
            "ack_observed",
            "ack_response",
        ],
        "confidence": "high",
        "sot_keys": ["Over Temperature Automatic Restart", "overTemperatureAutomaticRestart"],
    },
    "PDu": {
        "portal_key": "overloadAutomaticRestart",
        "portal_label": "Overload Automatic Restart",
        "portal_value": "Disable",
        "request_frame_style": "crc_cr",
        "ack_response": "(ACK9 ",
        "response_example": "(ACK9 ",
        "decoded_fields": [
            "portal_key",
            "portal_label",
            "portal_value",
            "ack_observed",
            "ack_response",
        ],
        "confidence": "high",
        "sot_keys": ["Overload Automatic Restart", "overloadAutomaticRestart"],
    },
    "PEu": {
        "portal_key": "overloadAutomaticRestart",
        "portal_label": "Overload Automatic Restart",
        "portal_value": "Enable",
        "request_frame_style": "crc_cr",
        "ack_response": "(ACK9 ",
        "response_example": "(ACK9 ",
        "decoded_fields": [
            "portal_key",
            "portal_label",
            "portal_value",
            "ack_observed",
            "ack_response",
        ],
        "confidence": "high",
        "sot_keys": ["Overload Automatic Restart", "overloadAutomaticRestart"],
    },
    "PDypx": {
        "portal_key": "inputSourceDetectionPromptSound",
        "portal_label": "Input Source Detection Prompt Sound",
        "portal_value": "Disable",
        "request_frame_style": "ascii_cr",
        "heep1_token_6_effect": None,
        "ack_response": "(ACK9 ",
        "response_example": "(ACK9 ",
        "decoded_fields": [
            "portal_key",
            "portal_label",
            "portal_value",
            "ack_observed",
            "ack_response",
            "heep1_token_6_effect",
        ],
        "confidence": "high",
        "sot_keys": ["Input Source Detection Prompt Sound", "inputSourceDetectionPromptSound"],
    },
    "PEyCI": {
        "portal_key": "inputSourceDetectionPromptSound",
        "portal_label": "Input Source Detection Prompt Sound",
        "portal_value": "Enable",
        "request_frame_style": "ascii_cr",
        "heep1_token_6_effect": None,
        "ack_response": "(ACK9 ",
        "response_example": "(ACK9 ",
        "decoded_fields": [
            "portal_key",
            "portal_label",
            "portal_value",
            "ack_observed",
            "ack_response",
            "heep1_token_6_effect",
        ],
        "confidence": "high",
        "sot_keys": ["Input Source Detection Prompt Sound", "inputSourceDetectionPromptSound"],
    },
    "PBEQE1": {
        "command_family": "batteryEqualizationModeEnableSetting",
        "portal_key": "batteryEqualizationModeEnableSetting",
        "portal_label": "Battery Equalization Mode Enable Setting",
        "portal_value": "On",
        "request_frame_style": "crc_cr",
        "ack_response": "(ACK9 ",
        "response_example": "(ACK9 ",
        "decoded_fields": [
            "portal_key",
            "portal_label",
            "portal_value",
            "ack_observed",
            "ack_response",
        ],
        "confidence": "high",
        "sot_keys": ["Battery Equalization Mode Enable Setting", "batteryEqualizationModeEnableSetting"],
    },
    "PBEQE0Z2": {
        "command_family": "batteryEqualizationModeEnableSetting",
        "portal_key": "batteryEqualizationModeEnableSetting",
        "portal_label": "Battery Equalization Mode Enable Setting",
        "portal_value": "Off",
        "request_frame_style": "ascii_cr",
        "ack_response": "(ACK9 ",
        "response_example": "(ACK9 ",
        "decoded_fields": [
            "portal_key",
            "portal_label",
            "portal_value",
            "ack_observed",
            "ack_response",
        ],
        "confidence": "high",
        "sot_keys": ["Battery Equalization Mode Enable Setting", "batteryEqualizationModeEnableSetting"],
    },
    "PCP00": {
        "command_family": "chargerPrioritySetting",
        "portal_key": "chargerPrioritySetting",
        "portal_label": "Charger Priority Setting",
        "portal_value": "CSO",
        "request_frame_style": "crc_cr",
        "ack_response": "(ACK9 ",
        "response_example": "(ACK9 ",
        "decoded_fields": [
            "command_family",
            "portal_key",
            "portal_label",
            "portal_value",
            "ack_observed",
            "ack_response",
        ],
        "confidence": "medium-high",
        "sot_keys": ["Charger Priority Setting", "chargerPrioritySetting", "chargingPriorityOrder"],
    },
    "PCP01": {
        "command_family": "chargerPrioritySetting",
        "portal_key": "chargerPrioritySetting",
        "portal_label": "Charger Priority Setting",
        "portal_value": "SNU",
        "request_frame_style": "crc_cr",
        "ack_response": "(ACK9 ",
        "response_example": "(ACK9 ",
        "decoded_fields": [
            "command_family",
            "portal_key",
            "portal_label",
            "portal_value",
            "ack_observed",
            "ack_response",
        ],
        "confidence": "medium-high",
        "sot_keys": ["Charger Priority Setting", "chargerPrioritySetting", "chargingPriorityOrder"],
    },
    "PCP02": {
        "command_family": "chargerPrioritySetting",
        "portal_key": "chargerPrioritySetting",
        "portal_label": "Charger Priority Setting",
        "portal_value": "OSO",
        "request_frame_style": "crc_cr",
        "ack_response": "(ACK9 ",
        "response_example": "(ACK9 ",
        "decoded_fields": [
            "command_family",
            "portal_key",
            "portal_label",
            "portal_value",
            "ack_observed",
            "ack_response",
        ],
        "confidence": "medium-high",
        "sot_keys": ["Charger Priority Setting", "chargerPrioritySetting", "chargingPriorityOrder"],
    },
    "POP00": {
        "command_family": "outputSourcePrioritySetting",
        "portal_key": "outputSourcePrioritySetting",
        "portal_label": "Output Source Priority Setting",
        "portal_value": "SUB priority",
        "request_frame_style": "crc_cr",
        "ack_response": "(ACK9 ",
        "response_example": "(ACK9 ",
        "decoded_fields": [
            "command_family",
            "portal_key",
            "portal_label",
            "portal_value",
            "ack_observed",
            "ack_response",
        ],
        "confidence": "high",
        "sot_keys": ["Output Source Priority Setting", "outputSourcePrioritySetting", "workingMode"],
    },
    "POP01": {
        "command_family": "outputSourcePrioritySetting",
        "portal_key": "outputSourcePrioritySetting",
        "portal_label": "Output Source Priority Setting",
        "portal_value": "SBU priority",
        "request_frame_style": "crc_cr",
        "ack_response": "(ACK9 ",
        "response_example": "(ACK9 ",
        "decoded_fields": [
            "command_family",
            "portal_key",
            "portal_label",
            "portal_value",
            "ack_observed",
            "ack_response",
        ],
        "confidence": "high",
        "sot_keys": ["Output Source Priority Setting", "outputSourcePrioritySetting", "workingMode"],
    },
    "POP02": {
        "command_family": "outputSourcePrioritySetting",
        "portal_key": "outputSourcePrioritySetting",
        "portal_label": "Output Source Priority Setting",
        "portal_value": "Utility first (legacy)",
        "request_frame_style": "crc_cr",
        "ack_response": "(ACK9 ",
        "response_example": "(ACK9 ",
        "decoded_fields": [
            "command_family",
            "portal_key",
            "portal_label",
            "portal_value",
            "ack_observed",
            "ack_response",
        ],
        "confidence": "medium",
        "sot_keys": ["Output Source Priority Setting", "outputSourcePrioritySetting", "workingMode"],
    },
    "PVENGUSE00": {
        "command_family": "pvEnergyFeedingPrioritySetting",
        "portal_key": "pvEnergyFeedingPrioritySetting",
        "portal_label": "PV Energy Feeding Priority Setting",
        "portal_value": "BLU",
        "request_frame_style": "crc_cr",
        "ack_response": "(ACK9 ",
        "response_example": "(ACK9 ",
        "decoded_fields": [
            "command_family",
            "portal_key",
            "portal_label",
            "portal_value",
            "ack_observed",
            "ack_response",
        ],
        "confidence": "high",
        "sot_keys": ["PV Energy Feeding Priority Setting", "pvEnergyFeedingPrioritySetting", "pvEnergyFeedingPriority"],
    },
    "PVENGUSE01": {
        "command_family": "pvEnergyFeedingPrioritySetting",
        "portal_key": "pvEnergyFeedingPrioritySetting",
        "portal_label": "PV Energy Feeding Priority Setting",
        "portal_value": "LBU",
        "request_frame_style": "crc_cr",
        "ack_response": "(ACK9 ",
        "response_example": "(ACK9 ",
        "decoded_fields": [
            "command_family",
            "portal_key",
            "portal_label",
            "portal_value",
            "ack_observed",
            "ack_response",
        ],
        "confidence": "high",
        "sot_keys": ["PV Energy Feeding Priority Setting", "pvEnergyFeedingPrioritySetting", "pvEnergyFeedingPriority"],
    },
}

PARAMETERIZED_WRITE_COMMANDS = {
    "inverterSystemClock": {
        "request_template": "^S???DAT<YYMMDDHHMMSS>",
        "request_example": "^S???DAT260429200700",
        "request_frame_style": "crc_cr",
        "portal_key": "inverterSystemClock",
        "portal_label": "Inverter System Clock",
        "portal_value": "2026-04-29 20:07:00",
        "response_example": "^1",
        "decoded_fields": [
            "command_family",
            "clock_selector",
            "clock_digits",
            "clock_iso",
            "portal_key",
            "portal_label",
            "portal_value",
            "ack_observed",
            "ack_response",
        ],
        "confidence": "high",
        "sot_keys": ["Inverter System Clock", "inverterSystemClock"],
    },
    "restoreSecondOutputBatCapacitySetting": {
        "request_template": "PDSRS<DDD>",
        "request_example": "PDSRS050",
        "request_frame_style": "crc_cr",
        "portal_key": "restoreSecondOutputBatCapacitySetting",
        "portal_label": "Restore Second Output Battery Capacity Setting",
        "portal_value": "50",
        "response_example": "(ACK9 ",
        "decoded_fields": [
            "command_family",
            "requested_value",
            "portal_key",
            "portal_label",
            "portal_value",
            "ack_observed",
            "ack_response",
        ],
        "confidence": "medium",
        "sot_keys": ["Restore Second Output Battery Capacity Setting", "restoreSecondOutputBatCapacitySetting"],
    },
    "batteryEqualizationVoltageSetting": {
        "request_template": "PBEQV<XX.XX>",
        "request_example": "PBEQV58.30",
        "request_frame_style": "crc_cr",
        "portal_key": "batteryEqualizationVoltageSetting",
        "portal_label": "Battery Equalization Voltage Setting",
        "portal_value": "58.3",
        "response_example": "(ACK9 ",
        "decoded_fields": [
            "command_family",
            "requested_value",
            "portal_key",
            "portal_label",
            "portal_value",
            "ack_observed",
            "ack_response",
        ],
        "confidence": "high",
        "sot_keys": ["Battery Equalization Voltage Setting", "batteryEqualizationVoltageSetting"],
    },
    "batteryEqualizationIntervalSetting": {
        "request_template": "PBEQP<DDD>",
        "request_example": "PBEQP030",
        "request_frame_style": "crc_cr",
        "portal_key": "batteryEqualizationIntervalSetting",
        "portal_label": "Battery Equalization Interval Setting",
        "portal_value": "30",
        "response_example": "(ACK9 ",
        "decoded_fields": [
            "command_family",
            "requested_value",
            "portal_key",
            "portal_label",
            "portal_value",
            "ack_observed",
            "ack_response",
        ],
        "confidence": "high",
        "sot_keys": ["Battery Equalization Interval Setting", "batteryEqualizationIntervalSetting"],
    },
    "batteryEqualizationTimeoutSetting": {
        "request_template": "PBEQOT<DDD>",
        "request_example": "PBEQOT120",
        "request_frame_style": "crc_cr",
        "portal_key": "batteryEqualizationTimeoutSetting",
        "portal_label": "Battery Equalization Timeout Setting",
        "portal_value": "120",
        "response_example": "(ACK9 ",
        "decoded_fields": [
            "command_family",
            "requested_value",
            "portal_key",
            "portal_label",
            "portal_value",
            "ack_observed",
            "ack_response",
        ],
        "confidence": "high",
        "sot_keys": ["Battery Equalization Timeout Setting", "batteryEqualizationTimeoutSetting", "equalizationOvertime"],
    },
    "batteryCutOffVoltageSetting": {
        "request_template": "PSDV<XX.X>",
        "request_example": "PSDV41.0",
        "request_frame_style": "crc_cr",
        "portal_key": "batteryCutOffVoltageSetting",
        "portal_label": "Battery Cut Off Voltage Setting",
        "portal_value": "41.0",
        "response_example": "(NAKss",
        "decoded_fields": [
            "command_family",
            "requested_value",
            "portal_key",
            "portal_label",
            "portal_value",
            "ack_observed",
            "ack_response",
        ],
        "confidence": "high",
        "sot_keys": ["Battery Cut Off Voltage Setting", "batteryCutOffVoltageSetting", "lowElectricLockVoltage"],
    },
    "bmsLockMachineBatteryCapacitySetting": {
        "request_template": "BMSSDC<DDD>",
        "request_example": "BMSSDC020",
        "request_frame_style": "crc_cr",
        "portal_key": "bmsLockMachineBatteryCapacitySetting",
        "portal_label": "BMS Lock Machine Battery Capacity (%)",
        "portal_value": "20",
        "response_example": "(ACK9 ",
        "decoded_fields": [
            "command_family",
            "requested_value",
            "portal_key",
            "portal_label",
            "portal_value",
            "ack_observed",
            "ack_response",
        ],
        "confidence": "medium",
        "sot_keys": ["BMS Lock Machine Battery Capacity (%)", "bmsLockMachineBatteryCapacitySetting"],
    },
    "bmsFunctionEnableSetting": {
        "request_template": "BMSC<NN>",
        "request_example": "BMSC01",
        "request_frame_style": "crc_cr",
        "portal_key": "bmsFunctionEnableSetting",
        "portal_label": "BMS Function Enable Setting",
        "portal_value": "On",
        "response_example": "(ACK9 ",
        "decoded_fields": [
            "command_family",
            "requested_value",
            "portal_key",
            "portal_label",
            "portal_value",
            "ack_observed",
            "ack_response",
        ],
        "confidence": "high",
        "sot_keys": ["BMS Function Enable Setting", "bmsFunctionEnableSetting"],
    },
    "batteryConstantChargingVoltageSetting": {
        "request_template": "PCVV<XX.X>",
        "request_example": "PCVV56.4",
        "request_frame_style": "crc_cr",
        "portal_key": "batteryConstantChargingVoltageSetting",
        "portal_label": "Battery Constant Charging Voltage Setting",
        "portal_value": "56.4",
        "response_example": "(ACK9 ",
        "decoded_fields": [
            "command_family",
            "requested_value",
            "portal_key",
            "portal_label",
            "portal_value",
            "ack_observed",
            "ack_response",
        ],
        "confidence": "medium",
        "sot_keys": [
            "Battery Constant Charging Voltage Setting",
            "batteryConstantChargingVoltageSetting",
            "battery_bulk_voltage",
        ],
    },
    "batteryRechargeVoltageSetting": {
        "request_template": "PBCV<XX.X>",
        "request_example": "PBCV45.0",
        "request_frame_style": "crc_cr",
        "portal_key": "batteryRechargeVoltageSetting",
        "portal_label": "Battery Recharge Voltage Setting",
        "portal_value": "45.0",
        "response_example": "(ACK9 ",
        "decoded_fields": [
            "command_family",
            "requested_value",
            "portal_key",
            "portal_label",
            "portal_value",
            "ack_observed",
            "ack_response",
        ],
        "confidence": "medium-high",
        "sot_keys": [
            "Battery Recharge Voltage Setting",
            "batteryRechargeVoltageSetting",
            "battery_recharge_voltage",
            "returnToMainsModeVoltage",
        ],
    },
    "batteryRedischargeVoltageSetting": {
        "request_template": "PBDV<XX.X>",
        "request_example": "PBDV53.0",
        "request_frame_style": "crc_cr",
        "portal_key": "batteryRedischargeVoltageSetting",
        "portal_label": "Battery Redischarge Voltage Setting",
        "portal_value": "53.0",
        "response_example": "(ACK9 ",
        "decoded_fields": [
            "command_family",
            "requested_value",
            "portal_key",
            "portal_label",
            "portal_value",
            "ack_observed",
            "ack_response",
        ],
        "confidence": "medium-high",
        "sot_keys": [
            "Battery Redischarge Voltage Setting",
            "batteryRedischargeVoltageSetting",
            "battery_redischarge_voltage",
            "returnToBatteryModeVoltage",
        ],
    },
    "batteryTypeSetting": {
        "request_template": "PBT<NN>",
        "request_example": "PBT04",
        "request_frame_style": "crc_cr",
        "portal_key": "batteryTypeSetting",
        "portal_label": "Battery Type Setting",
        "portal_value": "PYL",
        "response_example": "(ACK9 ",
        "decoded_fields": [
            "command_family",
            "requested_value",
            "portal_key",
            "portal_label",
            "portal_value",
            "ack_observed",
            "ack_response",
        ],
        "confidence": "medium-high",
        "sot_keys": ["Battery Type Setting", "batteryTypeSetting"],
    },
    "gridConnectedCurrentSetting": {
        "request_template": "PGFC<DDD>",
        "request_example": "PGFC021",
        "request_frame_style": "crc_cr",
        "portal_key": "gridConnectedCurrentSetting",
        "portal_label": "Grid Connected Current Setting",
        "portal_value": "21",
        "response_example": "(ACK9 ",
        "decoded_fields": [
            "command_family",
            "requested_value",
            "portal_key",
            "portal_label",
            "portal_value",
            "ack_observed",
            "ack_response",
        ],
        "confidence": "high",
        "sot_keys": ["Grid Connected Current Setting", "gridConnectedCurrentSetting"],
    },
    "maximumMainsChargingCurrentSetting": {
        "request_template": "MUCHGC<DDD>",
        "request_example": "MUCHGC020",
        "request_frame_style": "crc_cr",
        "portal_key": "maximumMainsChargingCurrentSetting",
        "portal_label": "Maximum Mains Charging Current Setting",
        "portal_value": "20",
        "response_example": "(ACK9 ",
        "decoded_fields": [
            "command_family",
            "requested_value",
            "portal_key",
            "portal_label",
            "portal_value",
            "ack_observed",
            "ack_response",
        ],
        "confidence": "high",
        "sot_keys": ["Maximum Mains Charging Current Setting", "maximumMainsChargingCurrentSetting"],
    },
    "maximumChargingCurrentSetting": {
        "request_template": "MNCHGC<DDD>",
        "request_example": "MNCHGC050",
        "request_frame_style": "crc_cr",
        "portal_key": "maximumChargingCurrentSetting",
        "portal_label": "Maximum Charging Current Setting",
        "portal_value": "50",
        "response_example": "(ACK9 ",
        "decoded_fields": [
            "command_family",
            "requested_value",
            "portal_key",
            "portal_label",
            "portal_value",
            "ack_observed",
            "ack_response",
        ],
        "confidence": "medium",
        "sot_keys": ["Maximum Charging Current Setting", "maximumChargingCurrentSetting"],
    },
    "batteryFloatChargingVoltageSetting": {
        "request_template": "PBFT<XX.X>",
        "request_example": "PBFT56.5",
        "request_frame_style": "crc_cr",
        "portal_key": "batteryFloatChargingVoltageSetting",
        "portal_label": "Battery Float Charging Voltage Setting",
        "portal_value": "56.5",
        "response_example": "(NAKss",
        "decoded_fields": [
            "command_family",
            "requested_value",
            "portal_key",
            "portal_label",
            "portal_value",
            "ack_observed",
            "ack_response",
        ],
        "confidence": "medium",
        "sot_keys": ["Battery Float Charging Voltage Setting", "batteryFloatChargingVoltageSetting", "battery_float_voltage"],
    },
    "batteryEqualizationTimeSetting": {
        "request_template": "PBEQT<DDD>",
        "request_example": "PBEQT059",
        "request_frame_style": "crc_cr",
        "portal_key": "batteryEqualizationTimeSetting",
        "portal_label": "Battery Equalization Time Setting",
        "portal_value": "59",
        "response_example": "(ACK9 ",
        "decoded_fields": [
            "command_family",
            "requested_value",
            "portal_key",
            "portal_label",
            "portal_value",
            "ack_observed",
            "ack_response",
        ],
        "confidence": "high",
        "sot_keys": ["Battery Equalization Time Setting", "batteryEqualizationTimeSetting"],
    },
    "restoreSecondOutputDelayTimeSetting": {
        "request_template": "PDDLYT<DDD>",
        "request_example": "PDDLYT005",
        "request_frame_style": "crc_cr",
        "portal_key": "restoreSecondOutputDelayTimeSetting",
        "portal_label": "Restore Second Output Delay Time Setting",
        "portal_value": "5",
        "response_example": "(ACK9 ",
        "decoded_fields": [
            "command_family",
            "requested_value",
            "portal_key",
            "portal_label",
            "portal_value",
            "ack_observed",
            "ack_response",
        ],
        "confidence": "medium",
        "sot_keys": ["Restore Second Output Delay Time Setting", "restoreSecondOutputDelayTimeSetting"],
    },
}

MODE_CODE_LABELS = {
    "L": "Mains Mode",
    "B": "Battery Mode",
}

COMMAND_DEFINITIONS: dict[str, dict[str, Any]] = {
    "HSTS": {
        "response_example": "(00 L010000000000 11211001000L112000000",
        "decoded_fields": [
            "status_code",
            "mode_code",
            "mode_label",
            "status_bits",
            "fault_bits",
        ],
        "confidence": "high",
        "sot_keys": ["Mode", "Status Code", "Warnings Present", "Faults Present"],
    },
    "HGRID": {
        "response_example": "(240.2 49.9 280 090 70 40 +00291 0 06500 11+00000",
        "decoded_fields": [
            "ac_input_voltage_v",
            "mains_frequency_hz",
            "high_mains_loss_voltage_v",
            "low_mains_loss_voltage_v",
            "high_mains_loss_frequency_hz",
            "low_mains_loss_frequency_hz",
            "mains_power_w",
            "mains_current_flow_direction_code",
            "mains_current_flow_direction",
            "rated_power_w",
            "raw_tail",
        ],
        "confidence": "high",
        "sot_keys": [
            "AC input voltage",
            "Mains Frequency",
            "Mains Current Flow Direction",
            "Mains Power",
        ],
    },
    "HOP": {
        "response_example": "(240.2 49.9 00216 00177 003 006 06200 005.9 00107",
        "decoded_fields": [
            "ac_output_voltage_v",
            "ac_output_frequency_hz",
            "output_apparent_power_va",
            "output_active_power_w",
            "output_load_percent",
            "output_dc_component_status",
            "rated_power_w",
            "inductor_current_a",
            "raw_tail",
        ],
        "confidence": "high",
        "sot_keys": [
            "Output Voltage",
            "Output Frequency",
            "Output Apparent Power",
            "Output Active Power",
            "Output Load Percent",
        ],
    },
    "HBAT": {
        "response_example": "(04 053.5 083 001 00000 393 101002010000 00000000",
        "decoded_fields": [
            "battery_type_code",
            "battery_type",
            "battery_voltage_v",
            "battery_capacity_percent",
            "battery_charging_current_a",
            "battery_discharge_current_a",
            "bus_voltage_v",
            "raw_tail",
        ],
        "confidence": "high",
        "sot_keys": [
            "Battery Type",
            "Battery Voltage",
            "Battery Capacity",
            "Battery Charging Current",
            "Battery Discharge Current",
            "BUS Voltage",
        ],
    },
    "HPV": {
        "response_example": "(000.0 00.0 00000 00000.0 00000 0 060.0 027 08500",
        "decoded_fields": [
            "pv_voltage_v",
            "pv_current_a",
            "pv_power_w",
            "generation_power_kw",
            "raw_tail",
        ],
        "confidence": "high",
        "sot_keys": ["Generation Power", "PV Voltage", "PV Current", "PV Power"],
    },
    "HTEMP": {
        "response_example": "(020 028 022 026 028 030 030 11000000000000000000",
        "decoded_fields": [
            "inverter_temperature_c",
            "boost_temperature_c",
            "transformer_temperature_c",
            "pv_temperature_c",
            "fan_1_speed_percent",
            "fan_2_speed_percent",
            "max_temperature_c",
            "temperature_status_bits",
            "raw_tail",
        ],
        "confidence": "high",
        "sot_keys": [
            "Inverter Temperature",
            "Boost Temperature",
            "Transformer Temperature",
            "PV Temperature",
            "Fan 1 Speed",
            "Fan 2 Speed",
            "Max. Temperature",
        ],
    },
    "HEEP1": {
        "response_example": "(1 060 030 03410110230 012 1 1 0 0 1 020 025 090 050 056.4 056.4 042.0 020 0 0",
        "decoded_fields": ["raw_payload"],
        "confidence": "low",
        "sot_keys": [],
    },
    "HBMS1": {
        "response_example": "(02 1001100000000000 044.8 057.6 150.0 083 0001.7 0000.0 02946 000000",
        "decoded_fields": [
            "bms_status_code",
            "bms_flags",
            "bms_discharge_voltage_limit_v",
            "bms_charge_voltage_limit_v",
            "bms_charge_current_limit_a",
            "bms_soc_percent",
            "bms_charging_current_a",
            "bms_discharge_current_a",
            "raw_tail",
        ],
        "confidence": "medium-high",
        "sot_keys": [
            "BMS Charge Current Limit",
            "BMS Charge Voltage Limit",
            "BMS Charging Current",
            "BMS Communication Control Function",
            "BMS Communication Normal",
            "BMS Discharge Current",
            "BMS Discharge Voltage Limit",
            "BMS Low Battery Alarm Flag",
            "BMS Low Power SOC",
            "BMS Returns To Battery Mode SOC",
            "BMS Returns To Mains Mode SOC",
        ],
    },
    "HGEN": {
        "response_example": "(260429 23:02 03.043 0059.4 0066.4 000000066.4 000000000000",
        "decoded_fields": [
            "date_ymd",
            "date_iso",
            "time_hm",
            "daily_power_gen_kwh",
            "monthly_electricity_generation_kwh",
            "yearly_electricity_generation_kwh",
            "total_power_generation_kwh",
            "raw_tail",
        ],
        "confidence": "high",
        "sot_keys": [
            "Daily Power Gen.",
            "Monthly Electricity Generation",
            "Yearly Electricity Generation",
            "Total Power Generation",
            "dailyProducedQuantity",
            "monthlyProducedQuantity",
            "yearlyProducedQuantity",
            "totalProducedQuantity",
            "pvGeneratedEnergyOfDay",
            "currentMonthPvGenerationReadDirectly",
            "totalPvGenerationReadDirectlyK",
        ],
    },
    "HEEP2": {
        "response_example": "(0 044.0 020 044.0 046.0 054.0 0 058.4 060 120 030 0000 0000 05 0000 52.0 50000",
        "decoded_fields": ["raw_payload"],
        "confidence": "low",
        "sot_keys": [],
    },
    "HPVB": {
        "response_example": "(000.0 00.0 00000 0 380.0 000000000000000000000",
        "decoded_fields": [
            "pv_voltage_v",
            "pv_current_a",
            "pv_power_w",
            "pv_charging_mark_code",
            "pv_charging_mark",
            "bus_voltage_v",
            "raw_tail",
        ],
        "confidence": "medium",
        "sot_keys": [
            "pvVoltage",
            "pvCurrent",
            "pvPower",
            "pvChargingMark",
            "busVoltage",
        ],
    },
    "QPRTL": {
        "response_example": "(HPVINV02",
        "decoded_fields": ["device_type"],
        "confidence": "medium",
        "sot_keys": ["Device Type", "deviceType"],
    },
    "HIMSG1": {
        "response_example": "(0040.05 20250923 12",
        "decoded_fields": ["software_version", "software_date", "software_date_iso", "revision"],
        "confidence": "high",
        "sot_keys": ["Software Version", "softwareVersion"],
    },
    "HBMS2": {
        "response_example": "(0000.0 0000.0 1 3351 0015 3350 0016 0000000000000000000000",
        "decoded_fields": [
            "remaining_capacity",
            "nominal_capacity",
            "display_mode",
            "max_voltage",
            "max_voltage_cell_position",
            "min_voltage",
            "min_voltage_cell_position",
            "raw_tail",
        ],
        "confidence": "medium",
        "sot_keys": [
            "Remaining Capacity",
            "Nominal Capacity",
            "Display Mode",
            "Max Voltage",
            "Min Voltage",
            "Max Voltage Cell Position",
            "Min Voltage Cell Position",
        ],
    },
    "HBMS3": {
        "response_example": "(0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 00000000",
        "decoded_fields": ["cell_voltage_list", "raw_tail"],
        "confidence": "low",
        "sot_keys": ["Cell voltage list"],
    },
}

FIELD_ALIASES: dict[str, dict[str, list[str]]] = {
    "HSTS": {
        "status_code": ["Status Code"],
        "mode_label": ["Mode"],
        "status_bits": ["Warnings Present"],
        "fault_bits": ["Faults Present"],
    },
    "HGRID": {
        "ac_input_voltage_v": ["AC input voltage", "acInputVoltage"],
        "mains_frequency_hz": ["Mains Frequency", "mainsFrequency"],
        "high_mains_loss_voltage_v": ["High Mains Loss Voltage"],
        "low_mains_loss_voltage_v": ["Low Mains Loss Voltage"],
        "high_mains_loss_frequency_hz": ["High Mains Loss Frequency"],
        "low_mains_loss_frequency_hz": ["Low Mains Loss Frequency"],
        "mains_power_w": ["Mains Power", "gridPower"],
        "mains_current_flow_direction": ["Mains Current Flow Direction"],
        "rated_power_w": ["Rated Power"],
    },
    "HOP": {
        "ac_output_voltage_v": ["Output Voltage", "acOutputVoltage"],
        "ac_output_frequency_hz": ["Output Frequency", "acOutputFrequency"],
        "output_apparent_power_va": ["Output Apparent Power", "acOutputApparentPower"],
        "output_active_power_w": ["Output Active Power", "acOutputActivePower"],
        "output_load_percent": ["Output Load Percent", "outputLoadPercent"],
        "rated_power_w": ["ratedPower", "Output Rating"],
    },
    "HBAT": {
        "battery_type_code": [],
        "battery_type": ["Battery Type", "Battery Status"],
        "battery_voltage_v": ["Battery Voltage", "batteryVoltage"],
        "battery_capacity_percent": ["Battery Capacity", "batteryCapacity"],
        "battery_charging_current_a": ["Battery Charging Current", "batteryChargingCurrent"],
        "battery_discharge_current_a": ["Battery Discharge Current", "batteryDischargeCurrent"],
        "bus_voltage_v": ["BUS Voltage", "busVoltage"],
    },
    "HPV": {
        "pv_voltage_v": ["PV Voltage", "pvVoltage"],
        "pv_current_a": ["PV Current", "pvCurrent"],
        "pv_power_w": ["PV Power", "pvPower"],
        "generation_power_kw": ["Generation Power", "generationPower"],
    },
    "HPVB": {
        "pv_voltage_v": ["pvVoltage"],
        "pv_current_a": ["pvCurrent"],
        "pv_power_w": ["pvPower"],
        "pv_charging_mark_code": ["pvChargingMark"],
        "pv_charging_mark": ["pvChargingMark"],
        "bus_voltage_v": ["busVoltage"],
    },
    "HTEMP": {
        "inverter_temperature_c": ["Inverter Temperature", "inverterTemperature"],
        "boost_temperature_c": ["Boost Temperature", "boostTemperature"],
        "transformer_temperature_c": ["Transformer Temperature", "transformerTemperature"],
        "pv_temperature_c": ["PV Temperature", "pvTemperature"],
        "fan_1_speed_percent": ["Fan 1 Speed", "fan1Speed"],
        "fan_2_speed_percent": ["Fan 2 Speed", "fan2Speed"],
        "max_temperature_c": ["Max. Temperature", "maxTemperature"],
    },
    "HGEN": {
        "date_ymd": [],
        "date_iso": [],
        "time_hm": [],
        "daily_power_gen_kwh": [
            "Daily Power Gen.",
            "dailyProducedQuantity",
            "pvGeneratedEnergyOfDay",
            "todayPvGenerationReadDirectly",
        ],
        "monthly_electricity_generation_kwh": [
            "Monthly Electricity Generation",
            "monthlyProducedQuantity",
            "tqfMonthlyElectricityGeneration",
            "currentMonthPvGenerationReadDirectly",
        ],
        "yearly_electricity_generation_kwh": [
            "Yearly Electricity Generation",
            "yearlyProducedQuantity",
            "tqfYearlyElectricityGeneration",
        ],
        "total_power_generation_kwh": [
            "Total Power Generation",
            "totalProducedQuantity",
            "totalGeneratedEnergy",
            "pvGeneratedEnergyOfTotal",
            "totalPvGenerationReadDirectlyK",
        ],
    },
    "QPRTL": {
        "device_type": ["Device Type", "deviceType"],
    },
    "HIMSG1": {
        "software_version": ["Software Version", "softwareVersion"],
        "software_date": [],
        "software_date_iso": [],
    },
    "HBMS1": {
        "bms_status_code": ["BMS Communication Control Function", "BMS Communication Normal"],
        "bms_flags": ["BMS Communication Control Function", "BMS Communication Normal"],
        "bms_discharge_voltage_limit_v": ["BMS Discharge Voltage Limit", "bmsDischargeVoltageLimit"],
        "bms_charge_voltage_limit_v": ["BMS Charge Voltage Limit", "bmsChargeVoltageLimit"],
        "bms_charge_current_limit_a": ["BMS Charge Current Limit", "bmsChargeCurrentLimit"],
        "bms_soc_percent": ["BMS Current SOC", "bmsCurrentSOC"],
        "bms_charging_current_a": ["BMS Charging Current", "bmsChargingCurrent"],
        "bms_discharge_current_a": ["BMS Discharge Current", "bmsDischargeCurrent"],
    },
    "HBMS2": {
        "remaining_capacity": ["Remaining Capacity"],
        "nominal_capacity": ["Nominal Capacity"],
        "display_mode": ["Display Mode"],
        "max_voltage": ["Max Voltage"],
        "min_voltage": ["Min Voltage"],
        "max_voltage_cell_position": ["Max Voltage Cell Position"],
        "min_voltage_cell_position": ["Min Voltage Cell Position"],
    },
    "HBMS3": {
        "cell_voltage_list": ["Cell voltage list"],
    },
    "chargerPrioritySetting": {
        "portal_key": ["Charger Priority Setting", "chargerPrioritySetting", "chargingPriorityOrder"],
        "portal_label": ["Charger Priority Setting"],
        "portal_value": ["CSO", "SNU", "OSO"],
    },
    "outputSourcePrioritySetting": {
        "portal_key": ["Output Source Priority Setting", "outputSourcePrioritySetting", "workingMode"],
        "portal_label": ["Output Source Priority Setting"],
        "portal_value": ["SUB priority", "SBU priority", "Utility first (legacy)"],
    },
    "pvEnergyFeedingPrioritySetting": {
        "portal_key": ["PV Energy Feeding Priority Setting", "pvEnergyFeedingPrioritySetting", "pvEnergyFeedingPriority"],
        "portal_label": ["PV Energy Feeding Priority Setting"],
        "portal_value": ["BLU", "LBU"],
    },
}

SNIFFER_RE = re.compile(
    r'SNIFF ts=(?P<ts>\d+) end=(?P<end>\d+) dur=(?P<dur>\d+) ch=(?P<ch>[AB]) '
    r'idx=(?P<idx>\d+) len=(?P<len>\d+) reason=(?P<reason>\w+) '
    r'hex="(?P<hex>[^"]*)" ascii="(?P<ascii>[^"]*)"'
)


@dataclass
class FrameRecord:
    seq: int
    ts_ms: int
    end_ms: int
    dur_ms: int
    channel: str
    idx: int
    length: int
    reason: str
    hex_text: str
    ascii_text: str
    raw_bytes: bytes
    payload_bytes: bytes
    payload_text: str
    frame_kind: str
    frame_style: str
    command: str | None = None
    crc_ok: bool | None = None
    crc_expected: str | None = None
    crc_wire: str | None = None


def normalize_key(value: str) -> str:
    return re.sub(r"[^a-z0-9]+", "", value.lower())


def hex_to_bytes(hex_text: str) -> bytes:
    if not hex_text.strip():
        return b""
    return bytes(int(part, 16) for part in hex_text.split())


def bytes_to_hex(data: bytes) -> str:
    return " ".join(f"{byte:02X}" for byte in data)


def ascii_encode_bytes(data: bytes) -> str:
    out: list[str] = []
    for byte in data:
        if byte == 0x0D:
            out.append(r"\r")
        elif byte == 0x0A:
            out.append(r"\n")
        elif byte == 0x09:
            out.append(r"\t")
        elif byte == 0x5C:
            out.append(r"\\")
        elif byte == 0x22:
            out.append(r"\"")
        elif 0x20 <= byte <= 0x7E:
            out.append(chr(byte))
        else:
            out.append(".")
    return "".join(out)


def parse_scalar(value: Any) -> Any:
    if not isinstance(value, str):
        return value
    stripped = value.strip()
    if stripped == "":
        return value
    try:
        if re.fullmatch(r"[+-]?\d+", stripped):
            return int(stripped)
        if re.fullmatch(r"[+-]?\d+\.\d+", stripped):
            return float(stripped)
        return value
    except Exception:
        return value


def split_payload(payload_text: str) -> list[str]:
    cleaned = payload_text.strip()
    if cleaned.startswith("("):
        cleaned = cleaned[1:]
    cleaned = cleaned.strip()
    if not cleaned:
        return []
    return cleaned.split()


BATTERY_TYPE_LABELS = {
    "04": "PYL",
}

MAINS_CURRENT_FLOW_DIRECTION_LABELS = {
    "0": "Mains To Inverter",
    "1": "Inverter To Mains",
}

PV_CHARGING_MARK_LABELS = {
    "0": "Close",
    "1": "Open",
}


def crc16_xmodem(data: bytes) -> int:
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def adjust_crc_bytes(crc: int) -> tuple[int, int]:
    high = (crc >> 8) & 0xFF
    low = crc & 0xFF
    if low in (0x28, 0x0D, 0x0A):
        low = (low + 1) & 0xFF
    if high in (0x28, 0x0D, 0x0A):
        high = (high + 1) & 0xFF
    return high, low


def build_frame(command: str) -> bytes:
    return command.encode("ascii") + b"\r"


def build_crc_frame(command: str) -> bytes:
    payload = command.encode("ascii")
    high, low = adjust_crc_bytes(crc16_xmodem(payload))
    return payload + bytes([high, low, 0x0D])


def strip_response_crc(payload_bytes: bytes) -> tuple[bytes, str | None, str | None, bool | None]:
    """Return response payload without trailing CRC bytes when the CRC validates."""
    if len(payload_bytes) < 3 or not payload_bytes.startswith(b"("):
        return payload_bytes, None, None, None
    candidate_payload = payload_bytes[:-2]
    wire_crc = payload_bytes[-2:]
    expected_crc = bytes(adjust_crc_bytes(crc16_xmodem(candidate_payload)))
    if wire_crc == expected_crc:
        return candidate_payload, bytes_to_hex(wire_crc), bytes_to_hex(expected_crc), True
    return payload_bytes, bytes_to_hex(wire_crc), bytes_to_hex(expected_crc), False


def parse_sniffer_line(line: str, seq: int) -> FrameRecord | None:
    match = SNIFFER_RE.search(line)
    if not match:
        return None
    hex_text = match.group("hex")
    raw_bytes = hex_to_bytes(hex_text)
    frame_style = "unknown_binary"
    crc_ok = None
    crc_expected = None
    crc_wire = None
    payload_bytes = raw_bytes[:-1] if raw_bytes.endswith(b"\r") else raw_bytes
    if raw_bytes.endswith(b"\r"):
        payload_text_candidate = payload_bytes.decode("ascii", errors="replace")
        printable_ascii = all(0x20 <= byte <= 0x7E for byte in payload_bytes)
        crc_candidate_text = None
        if len(raw_bytes) >= 3:
            crc_candidate_text = raw_bytes[:-3].decode("ascii", errors="replace")
        crc_request = (
            crc_candidate_text is not None
            and (
                crc_candidate_text == "^1"
                or
                CLOCK_WRITE_RE.fullmatch(crc_candidate_text)
                or BATTERY_EQUALIZATION_VOLTAGE_WRITE_RE.fullmatch(crc_candidate_text)
                or BATTERY_EQUALIZATION_INTERVAL_WRITE_RE.fullmatch(crc_candidate_text)
                or BATTERY_EQUALIZATION_TIMEOUT_WRITE_RE.fullmatch(crc_candidate_text)
                or BATTERY_EQUALIZATION_TIME_WRITE_RE.fullmatch(crc_candidate_text)
                or BATTERY_CUT_OFF_VOLTAGE_WRITE_RE.fullmatch(crc_candidate_text)
                or BATTERY_BULK_VOLTAGE_WRITE_RE.fullmatch(crc_candidate_text)
                or BATTERY_RECHARGE_VOLTAGE_WRITE_RE.fullmatch(crc_candidate_text)
                or BATTERY_REDISCHARGE_VOLTAGE_WRITE_RE.fullmatch(crc_candidate_text)
                or BATTERY_TYPE_WRITE_RE.fullmatch(crc_candidate_text)
                or BMSC_WRITE_RE.fullmatch(crc_candidate_text)
                or BMSSDC_WRITE_RE.fullmatch(crc_candidate_text)
                or GRID_CONNECTED_CURRENT_WRITE_RE.fullmatch(crc_candidate_text)
                or MAXIMUM_MAINS_CHARGING_CURRENT_WRITE_RE.fullmatch(crc_candidate_text)
                or MAXIMUM_CHARGING_CURRENT_WRITE_RE.fullmatch(crc_candidate_text)
                or BATTERY_FLOAT_CHARGING_VOLTAGE_WRITE_RE.fullmatch(crc_candidate_text)
                or RESTORE_SECOND_OUTPUT_BATTERY_CAPACITY_WRITE_RE.fullmatch(crc_candidate_text)
                or RESTORE_SECOND_OUTPUT_DELAY_TIME_WRITE_RE.fullmatch(crc_candidate_text)
                or crc_candidate_text in WRITE_CONTROL_COMMANDS
            )
        )
        if crc_request and len(raw_bytes) >= 3:
            candidate_payload = raw_bytes[:-3]
            wire_crc = raw_bytes[-3:-1]
            expected_high, expected_low = adjust_crc_bytes(crc16_xmodem(candidate_payload))
            expected_crc = bytes([expected_high, expected_low])
            if candidate_payload and wire_crc == expected_crc:
                payload_bytes = candidate_payload
                frame_style = "crc_cr"
                crc_wire = bytes_to_hex(wire_crc)
                crc_expected = bytes_to_hex(expected_crc)
                crc_ok = True
        if frame_style != "crc_cr" and (
            payload_text_candidate.startswith("(") or payload_text_candidate in ASCII_REQUEST_COMMANDS or printable_ascii
        ):
            frame_style = "ascii_cr"

        if payload_bytes.startswith(b"("):
            stripped_payload, response_crc_wire, response_crc_expected, response_crc_ok = strip_response_crc(payload_bytes)
            if response_crc_ok is True:
                payload_bytes = stripped_payload
                frame_style = "crc_response_cr"
                crc_wire = response_crc_wire
                crc_expected = response_crc_expected
                crc_ok = True

    payload_text = payload_bytes.decode("ascii", errors="replace")
    if payload_text.startswith("("):
        frame_kind = "response"
    elif payload_text == "^1":
        frame_kind = "response"
    elif CLOCK_WRITE_RE.fullmatch(payload_text):
        frame_kind = "request"
    elif BATTERY_EQUALIZATION_VOLTAGE_WRITE_RE.fullmatch(payload_text):
        frame_kind = "request"
    elif BATTERY_EQUALIZATION_INTERVAL_WRITE_RE.fullmatch(payload_text):
        frame_kind = "request"
    elif BATTERY_EQUALIZATION_TIMEOUT_WRITE_RE.fullmatch(payload_text):
        frame_kind = "request"
    elif BATTERY_EQUALIZATION_TIME_WRITE_RE.fullmatch(payload_text):
        frame_kind = "request"
    elif BATTERY_CUT_OFF_VOLTAGE_WRITE_RE.fullmatch(payload_text):
        frame_kind = "request"
    elif BATTERY_BULK_VOLTAGE_WRITE_RE.fullmatch(payload_text):
        frame_kind = "request"
    elif BATTERY_RECHARGE_VOLTAGE_WRITE_RE.fullmatch(payload_text):
        frame_kind = "request"
    elif BATTERY_REDISCHARGE_VOLTAGE_WRITE_RE.fullmatch(payload_text):
        frame_kind = "request"
    elif BATTERY_TYPE_WRITE_RE.fullmatch(payload_text):
        frame_kind = "request"
    elif BMSC_WRITE_RE.fullmatch(payload_text):
        frame_kind = "request"
    elif BMSSDC_WRITE_RE.fullmatch(payload_text):
        frame_kind = "request"
    elif GRID_CONNECTED_CURRENT_WRITE_RE.fullmatch(payload_text):
        frame_kind = "request"
    elif MAXIMUM_MAINS_CHARGING_CURRENT_WRITE_RE.fullmatch(payload_text):
        frame_kind = "request"
    elif MAXIMUM_CHARGING_CURRENT_WRITE_RE.fullmatch(payload_text):
        frame_kind = "request"
    elif BATTERY_FLOAT_CHARGING_VOLTAGE_WRITE_RE.fullmatch(payload_text):
        frame_kind = "request"
    elif payload_text in WRITE_CONTROL_COMMANDS or re.fullmatch(r"[A-Z0-9]+", payload_text):
        frame_kind = "request"
    else:
        frame_kind = "unknown"
    command = payload_text if frame_kind == "request" else None
    return FrameRecord(
        seq=seq,
        ts_ms=int(match.group("ts")),
        end_ms=int(match.group("end")),
        dur_ms=int(match.group("dur")),
        channel=match.group("ch"),
        idx=int(match.group("idx")),
        length=int(match.group("len")),
        reason=match.group("reason"),
        hex_text=hex_text,
        ascii_text=match.group("ascii"),
        raw_bytes=raw_bytes,
        payload_bytes=payload_bytes,
        payload_text=payload_text,
        frame_kind=frame_kind,
        frame_style=frame_style,
        command=command,
        crc_ok=crc_ok,
        crc_expected=crc_expected,
        crc_wire=crc_wire,
    )


def iter_sniffer_frames(lines: Iterable[str]) -> list[FrameRecord]:
    frames: list[FrameRecord] = []
    seq = 0
    for line in lines:
        record = parse_sniffer_line(line.rstrip("\n"), seq)
        if record is None:
            continue
        frames.append(record)
        seq += 1
    return frames


def decode_hsts(payload: str) -> dict[str, Any]:
    tokens = split_payload(payload)
    decoded: dict[str, Any] = {"raw_payload": payload, "tokens": tokens}
    if tokens:
        decoded["status_code"] = tokens[0]
    if len(tokens) > 1:
        decoded["mode_code"] = tokens[1][:1]
        decoded["mode_label"] = MODE_CODE_LABELS.get(decoded["mode_code"], f"unknown:{decoded['mode_code']}")
        decoded["status_bits"] = tokens[1][1:]
    if len(tokens) > 2:
        decoded["fault_bits"] = tokens[2]
    return decoded


def decode_hgrid(payload: str) -> dict[str, Any]:
    tokens = split_payload(payload)
    decoded: dict[str, Any] = {"raw_payload": payload, "tokens": tokens}
    if len(tokens) > 0:
        decoded["ac_input_voltage_v"] = parse_scalar(tokens[0])
    if len(tokens) > 1:
        decoded["mains_frequency_hz"] = parse_scalar(tokens[1])
    if len(tokens) > 2:
        decoded["high_mains_loss_voltage_v"] = parse_scalar(tokens[2])
    if len(tokens) > 3:
        decoded["low_mains_loss_voltage_v"] = parse_scalar(tokens[3])
    if len(tokens) > 4:
        decoded["high_mains_loss_frequency_hz"] = parse_scalar(tokens[4])
    if len(tokens) > 5:
        decoded["low_mains_loss_frequency_hz"] = parse_scalar(tokens[5])
    if len(tokens) > 6:
        decoded["mains_power_w"] = parse_scalar(tokens[6])
    if len(tokens) > 7:
        decoded["mains_current_flow_direction_code"] = tokens[7]
        decoded["mains_current_flow_direction"] = MAINS_CURRENT_FLOW_DIRECTION_LABELS.get(
            tokens[7], f"unknown:{tokens[7]}"
        )
    if len(tokens) > 8:
        decoded["rated_power_w"] = parse_scalar(tokens[8])
    if len(tokens) > 9:
        decoded["raw_tail"] = tokens[9:]
    return decoded


def decode_hop(payload: str) -> dict[str, Any]:
    tokens = split_payload(payload)
    decoded: dict[str, Any] = {"raw_payload": payload, "tokens": tokens}
    if len(tokens) > 0:
        decoded["ac_output_voltage_v"] = parse_scalar(tokens[0])
    if len(tokens) > 1:
        decoded["ac_output_frequency_hz"] = parse_scalar(tokens[1])
    if len(tokens) > 2:
        decoded["output_apparent_power_va"] = parse_scalar(tokens[2])
    if len(tokens) > 3:
        decoded["output_active_power_w"] = parse_scalar(tokens[3])
    if len(tokens) > 4:
        decoded["output_load_percent"] = parse_scalar(tokens[4])
    if len(tokens) > 5:
        decoded["output_dc_component_status"] = parse_scalar(tokens[5])
    if len(tokens) > 6:
        decoded["rated_power_w"] = parse_scalar(tokens[6])
    if len(tokens) > 7:
        decoded["inductor_current_a"] = parse_scalar(tokens[7])
    if len(tokens) > 8:
        decoded["raw_tail"] = tokens[8:]
    return decoded


def decode_hbat(payload: str) -> dict[str, Any]:
    tokens = split_payload(payload)
    decoded: dict[str, Any] = {"raw_payload": payload, "tokens": tokens}
    if len(tokens) > 0:
        decoded["battery_type_code"] = tokens[0]
        decoded["battery_type"] = BATTERY_TYPE_LABELS.get(tokens[0], tokens[0])
    if len(tokens) > 1:
        decoded["battery_voltage_v"] = parse_scalar(tokens[1])
    if len(tokens) > 2:
        decoded["battery_capacity_percent"] = parse_scalar(tokens[2])
    if len(tokens) > 3:
        decoded["battery_charging_current_a"] = parse_scalar(tokens[3])
    if len(tokens) > 4:
        decoded["battery_discharge_current_a"] = parse_scalar(tokens[4])
    if len(tokens) > 5:
        decoded["bus_voltage_v"] = parse_scalar(tokens[5])
    if len(tokens) > 6:
        decoded["raw_tail"] = tokens[6:]
    return decoded


def decode_hpv(payload: str) -> dict[str, Any]:
    tokens = split_payload(payload)
    decoded: dict[str, Any] = {"raw_payload": payload, "tokens": tokens}
    if len(tokens) > 0:
        decoded["pv_voltage_v"] = parse_scalar(tokens[0])
    if len(tokens) > 1:
        decoded["pv_current_a"] = parse_scalar(tokens[1])
    if len(tokens) > 2:
        decoded["pv_power_w"] = parse_scalar(tokens[2])
    if len(tokens) > 3:
        decoded["generation_power_kw"] = parse_scalar(tokens[3])
    if len(tokens) > 4:
        decoded["raw_tail"] = tokens[4:]
    return decoded


def decode_htemp(payload: str) -> dict[str, Any]:
    tokens = split_payload(payload)
    decoded: dict[str, Any] = {"raw_payload": payload, "tokens": tokens}
    if len(tokens) > 0:
        decoded["inverter_temperature_c"] = parse_scalar(tokens[0])
    if len(tokens) > 1:
        decoded["boost_temperature_c"] = parse_scalar(tokens[1])
    if len(tokens) > 2:
        decoded["transformer_temperature_c"] = parse_scalar(tokens[2])
    if len(tokens) > 3:
        decoded["pv_temperature_c"] = parse_scalar(tokens[3])
    if len(tokens) > 4:
        decoded["fan_1_speed_percent"] = parse_scalar(tokens[4])
    if len(tokens) > 5:
        decoded["fan_2_speed_percent"] = parse_scalar(tokens[5])
    if len(tokens) > 6:
        decoded["max_temperature_c"] = parse_scalar(tokens[6])
    if len(tokens) > 7:
        decoded["temperature_status_bits"] = tokens[7]
    return decoded


def decode_hgen(payload: str) -> dict[str, Any]:
    tokens = split_payload(payload)
    decoded: dict[str, Any] = {"raw_payload": payload, "tokens": tokens}
    if len(tokens) > 0:
        decoded["date_ymd"] = tokens[0]
        decoded["date_iso"] = (
            f"20{tokens[0][0:2]}-{tokens[0][2:4]}-{tokens[0][4:6]}"
            if re.fullmatch(r"\d{6}", tokens[0])
            else tokens[0]
        )
    if len(tokens) > 1:
        decoded["time_hm"] = tokens[1]
    if len(tokens) > 2:
        decoded["daily_power_gen_kwh"] = parse_scalar(tokens[2])
    if len(tokens) > 3:
        decoded["monthly_electricity_generation_kwh"] = parse_scalar(tokens[3])
    if len(tokens) > 4:
        decoded["yearly_electricity_generation_kwh"] = parse_scalar(tokens[4])
    if len(tokens) > 5:
        decoded["total_power_generation_kwh"] = parse_scalar(tokens[5])
    if len(tokens) > 6:
        decoded["raw_tail"] = tokens[6:]
    return decoded


def decode_qprtl(payload: str) -> dict[str, Any]:
    device_type = payload.lstrip("(").strip()
    return {"raw_payload": payload, "device_type": device_type}


def decode_himsg1(payload: str) -> dict[str, Any]:
    tokens = split_payload(payload)
    decoded: dict[str, Any] = {"raw_payload": payload, "tokens": tokens}
    if len(tokens) > 0:
        decoded["software_version"] = tokens[0]
    if len(tokens) > 1:
        decoded["software_date"] = tokens[1]
        decoded["software_date_iso"] = (
            f"{tokens[1][0:4]}-{tokens[1][4:6]}-{tokens[1][6:8]}"
            if re.fullmatch(r"\d{8}", tokens[1])
            else tokens[1]
        )
    if len(tokens) > 2:
        decoded["revision"] = tokens[2]
    return decoded


def decode_hbms1(payload: str) -> dict[str, Any]:
    tokens = split_payload(payload)
    decoded: dict[str, Any] = {"raw_payload": payload, "tokens": tokens}
    if len(tokens) > 0:
        decoded["bms_status_code"] = tokens[0]
    if len(tokens) > 1:
        decoded["bms_flags"] = tokens[1]
    if len(tokens) > 2:
        decoded["bms_discharge_voltage_limit_v"] = parse_scalar(tokens[2])
    if len(tokens) > 3:
        decoded["bms_charge_voltage_limit_v"] = parse_scalar(tokens[3])
    if len(tokens) > 4:
        decoded["bms_charge_current_limit_a"] = parse_scalar(tokens[4])
    if len(tokens) > 5:
        decoded["bms_soc_percent"] = parse_scalar(tokens[5])
    if len(tokens) > 6:
        decoded["bms_charging_current_a"] = parse_scalar(tokens[6])
    if len(tokens) > 7:
        decoded["bms_discharge_current_a"] = parse_scalar(tokens[7])
    if len(tokens) > 8:
        decoded["raw_tail"] = tokens[8:]
    return decoded


def decode_hbms2(payload: str) -> dict[str, Any]:
    tokens = split_payload(payload)
    decoded: dict[str, Any] = {"raw_payload": payload, "tokens": tokens}
    if len(tokens) > 0:
        decoded["remaining_capacity"] = parse_scalar(tokens[0])
    if len(tokens) > 1:
        decoded["nominal_capacity"] = parse_scalar(tokens[1])
    if len(tokens) > 2:
        decoded["display_mode"] = parse_scalar(tokens[2])
    if len(tokens) > 3:
        decoded["max_voltage"] = parse_scalar(tokens[3])
    if len(tokens) > 4:
        decoded["max_voltage_cell_position"] = parse_scalar(tokens[4])
    if len(tokens) > 5:
        decoded["min_voltage"] = parse_scalar(tokens[5])
    if len(tokens) > 6:
        decoded["min_voltage_cell_position"] = parse_scalar(tokens[6])
    if len(tokens) > 7:
        decoded["raw_tail"] = tokens[7:]
    return decoded


def decode_hbms3(payload: str) -> dict[str, Any]:
    tokens = split_payload(payload)
    decoded: dict[str, Any] = {"raw_payload": payload, "tokens": tokens}
    if tokens:
        decoded["cell_voltage_list"] = tokens[:16]
        if len(tokens) > 16:
            decoded["raw_tail"] = tokens[16:]
    return decoded


def decode_heep(payload: str) -> dict[str, Any]:
    tokens = split_payload(payload)
    return {"raw_payload": payload, "tokens": tokens}


def decode_hpvb(payload: str) -> dict[str, Any]:
    tokens = split_payload(payload)
    decoded: dict[str, Any] = {"raw_payload": payload, "tokens": tokens}
    if len(tokens) > 0:
        decoded["pv_voltage_v"] = parse_scalar(tokens[0])
    if len(tokens) > 1:
        decoded["pv_current_a"] = parse_scalar(tokens[1])
    if len(tokens) > 2:
        decoded["pv_power_w"] = parse_scalar(tokens[2])
    if len(tokens) > 3:
        decoded["pv_charging_mark_code"] = tokens[3]
        decoded["pv_charging_mark"] = PV_CHARGING_MARK_LABELS.get(tokens[3], f"unknown:{tokens[3]}")
    if len(tokens) > 4:
        decoded["bus_voltage_v"] = parse_scalar(tokens[4])
    if len(tokens) > 5:
        decoded["raw_tail"] = tokens[5:]
    return decoded


def decode_write_control(command: str, response_payload: str) -> dict[str, Any]:
    meta = WRITE_CONTROL_COMMANDS[command]
    decoded: dict[str, Any] = {
        "raw_payload": response_payload,
        "request_command": command,
        "tokens": [command, response_payload],
        "portal_key": meta["portal_key"],
        "portal_label": meta["portal_label"],
        "portal_value": meta["portal_value"],
        "ack_observed": response_payload.lstrip("(").startswith("ACK"),
        "ack_response": meta["ack_response"],
    }
    if "command_family" in meta:
        decoded["command_family"] = meta["command_family"]
    for effect_key in ("heep1_token_5_effect", "heep1_token_6_effect"):
        if effect_key in meta:
            decoded[effect_key] = meta[effect_key]
    return decoded


def decode_clock_write(command: str, response_payload: str) -> dict[str, Any]:
    match = CLOCK_WRITE_RE.fullmatch(command)
    if match is None:
        return {"raw_payload": response_payload, "request_command": command, "tokens": [command, response_payload]}

    selector = match.group("selector")
    clock_digits = match.group("clock")
    clock_iso = (
        f"20{clock_digits[0:2]}-{clock_digits[2:4]}-{clock_digits[4:6]} "
        f"{clock_digits[6:8]}:{clock_digits[8:10]}:{clock_digits[10:12]}"
    )
    decoded: dict[str, Any] = {
        "raw_payload": response_payload,
        "request_command": command,
        "command_family": "inverterSystemClock",
        "tokens": [selector, clock_digits, response_payload],
        "clock_selector": selector,
        "clock_digits": clock_digits,
        "clock_iso": clock_iso,
        "portal_key": "inverterSystemClock",
        "portal_label": "Inverter System Clock",
        "portal_value": clock_iso,
        "ack_observed": response_payload.startswith("^1"),
        "ack_response": "^1",
    }
    return decoded


def decode_battery_equalization_voltage_write(command: str, response_payload: str) -> dict[str, Any]:
    match = BATTERY_EQUALIZATION_VOLTAGE_WRITE_RE.fullmatch(command)
    if match is None:
        return {"raw_payload": response_payload, "request_command": command, "tokens": [command, response_payload]}

    requested_value = parse_scalar(match.group("value"))
    decoded: dict[str, Any] = {
        "raw_payload": response_payload,
        "request_command": command,
        "command_family": "batteryEqualizationVoltageSetting",
        "tokens": [match.group("value"), response_payload],
        "requested_value": requested_value,
        "portal_key": "batteryEqualizationVoltageSetting",
        "portal_label": "Battery Equalization Voltage Setting",
        "portal_value": requested_value,
        "ack_observed": response_payload.startswith("(ACK"),
        "ack_response": "(ACK9 ",
    }
    return decoded


def decode_battery_equalization_interval_write(command: str, response_payload: str) -> dict[str, Any]:
    match = BATTERY_EQUALIZATION_INTERVAL_WRITE_RE.fullmatch(command)
    if match is None:
        return {"raw_payload": response_payload, "request_command": command, "tokens": [command, response_payload]}

    requested_value = parse_scalar(match.group("value"))
    decoded: dict[str, Any] = {
        "raw_payload": response_payload,
        "request_command": command,
        "command_family": "batteryEqualizationIntervalSetting",
        "tokens": [match.group("value"), response_payload],
        "requested_value": requested_value,
        "portal_key": "batteryEqualizationIntervalSetting",
        "portal_label": "Battery Equalization Interval Setting",
        "portal_value": requested_value,
        "ack_observed": response_payload.startswith("(ACK"),
        "ack_response": "(ACK9 ",
    }
    return decoded


def decode_battery_equalization_timeout_write(command: str, response_payload: str) -> dict[str, Any]:
    match = BATTERY_EQUALIZATION_TIMEOUT_WRITE_RE.fullmatch(command)
    if match is None:
        return {"raw_payload": response_payload, "request_command": command, "tokens": [command, response_payload]}

    requested_value = parse_scalar(match.group("value"))
    decoded: dict[str, Any] = {
        "raw_payload": response_payload,
        "request_command": command,
        "command_family": "batteryEqualizationTimeoutSetting",
        "tokens": [match.group("value"), response_payload],
        "requested_value": requested_value,
        "portal_key": "batteryEqualizationTimeoutSetting",
        "portal_label": "Battery Equalization Timeout Setting",
        "portal_value": requested_value,
        "ack_observed": response_payload.startswith("(ACK"),
        "ack_response": "(ACK9 ",
    }
    return decoded


def decode_battery_cut_off_voltage_write(command: str, response_payload: str) -> dict[str, Any]:
    match = BATTERY_CUT_OFF_VOLTAGE_WRITE_RE.fullmatch(command)
    if match is None:
        return {"raw_payload": response_payload, "request_command": command, "tokens": [command, response_payload]}

    requested_value = parse_scalar(match.group("value"))
    decoded: dict[str, Any] = {
        "raw_payload": response_payload,
        "request_command": command,
        "command_family": "batteryCutOffVoltageSetting",
        "tokens": [match.group("value"), response_payload],
        "requested_value": requested_value,
        "portal_key": "batteryCutOffVoltageSetting",
        "portal_label": "Battery Cut Off Voltage Setting",
        "portal_value": requested_value,
        "ack_observed": response_payload.startswith("(ACK"),
        "ack_response": response_payload[:6],
    }
    return decoded


def decode_bms_lock_machine_battery_capacity_write(command: str, response_payload: str) -> dict[str, Any]:
    match = BMSSDC_WRITE_RE.fullmatch(command)
    if match is None:
        return {"raw_payload": response_payload, "request_command": command, "tokens": [command, response_payload]}

    requested_value = parse_scalar(match.group("value"))
    ack_response = "(ACK9 " if response_payload.startswith("(ACK") else "(NAKss"
    decoded: dict[str, Any] = {
        "raw_payload": response_payload,
        "request_command": command,
        "command_family": "bmsLockMachineBatteryCapacitySetting",
        "tokens": [match.group("value"), response_payload],
        "requested_value": requested_value,
        "portal_key": "bmsLockMachineBatteryCapacitySetting",
        "portal_label": "BMS Lock Machine Battery Capacity (%)",
        "portal_value": requested_value,
        "ack_observed": response_payload.startswith("(ACK"),
        "ack_response": ack_response,
    }
    return decoded


def decode_bms_function_enable_write(command: str, response_payload: str) -> dict[str, Any]:
    match = BMSC_WRITE_RE.fullmatch(command)
    if match is None:
        return {"raw_payload": response_payload, "request_command": command, "tokens": [command, response_payload]}

    requested_value = match.group("value")
    portal_value = BMSC_VALUE_LABELS.get(requested_value, f"unknown:{requested_value}")
    decoded: dict[str, Any] = {
        "raw_payload": response_payload,
        "request_command": command,
        "command_family": "bmsFunctionEnableSetting",
        "tokens": [requested_value, response_payload],
        "requested_value": requested_value,
        "portal_key": "bmsFunctionEnableSetting",
        "portal_label": "BMS Function Enable Setting",
        "portal_value": portal_value,
        "ack_observed": response_payload.startswith("(ACK"),
        "ack_response": "(ACK9 ",
    }
    return decoded


def decode_battery_constant_charging_voltage_write(command: str, response_payload: str) -> dict[str, Any]:
    match = BATTERY_BULK_VOLTAGE_WRITE_RE.fullmatch(command)
    if match is None:
        return {"raw_payload": response_payload, "request_command": command, "tokens": [command, response_payload]}

    requested_value = parse_scalar(match.group("value"))
    decoded: dict[str, Any] = {
        "raw_payload": response_payload,
        "request_command": command,
        "command_family": "batteryConstantChargingVoltageSetting",
        "tokens": [match.group("value"), response_payload],
        "requested_value": requested_value,
        "portal_key": "batteryConstantChargingVoltageSetting",
        "portal_label": "Battery Constant Charging Voltage Setting",
        "portal_value": requested_value,
        "ack_observed": response_payload.startswith("(ACK"),
        "ack_response": "(ACK9 ",
    }
    return decoded


def decode_battery_recharge_voltage_write(command: str, response_payload: str) -> dict[str, Any]:
    match = BATTERY_RECHARGE_VOLTAGE_WRITE_RE.fullmatch(command)
    if match is None:
        return {"raw_payload": response_payload, "request_command": command, "tokens": [command, response_payload]}

    requested_value = parse_scalar(match.group("value"))
    decoded: dict[str, Any] = {
        "raw_payload": response_payload,
        "request_command": command,
        "command_family": "batteryRechargeVoltageSetting",
        "tokens": [match.group("value"), response_payload],
        "requested_value": requested_value,
        "portal_key": "batteryRechargeVoltageSetting",
        "portal_label": "Battery Recharge Voltage Setting",
        "portal_value": requested_value,
        "ack_observed": response_payload.startswith("(ACK"),
        "ack_response": "(ACK9 ",
    }
    return decoded


def decode_battery_redischarge_voltage_write(command: str, response_payload: str) -> dict[str, Any]:
    match = BATTERY_REDISCHARGE_VOLTAGE_WRITE_RE.fullmatch(command)
    if match is None:
        return {"raw_payload": response_payload, "request_command": command, "tokens": [command, response_payload]}

    requested_value = parse_scalar(match.group("value"))
    decoded: dict[str, Any] = {
        "raw_payload": response_payload,
        "request_command": command,
        "command_family": "batteryRedischargeVoltageSetting",
        "tokens": [match.group("value"), response_payload],
        "requested_value": requested_value,
        "portal_key": "batteryRedischargeVoltageSetting",
        "portal_label": "Battery Redischarge Voltage Setting",
        "portal_value": requested_value,
        "ack_observed": response_payload.startswith("(ACK"),
        "ack_response": "(ACK9 ",
    }
    return decoded


def decode_grid_connected_current_write(command: str, response_payload: str) -> dict[str, Any]:
    match = GRID_CONNECTED_CURRENT_WRITE_RE.fullmatch(command)
    if match is None:
        return {"raw_payload": response_payload, "request_command": command, "tokens": [command, response_payload]}

    requested_value = parse_scalar(match.group("value"))
    decoded: dict[str, Any] = {
        "raw_payload": response_payload,
        "request_command": command,
        "command_family": "gridConnectedCurrentSetting",
        "tokens": [match.group("value"), response_payload],
        "requested_value": requested_value,
        "portal_key": "gridConnectedCurrentSetting",
        "portal_label": "Grid Connected Current Setting",
        "portal_value": requested_value,
        "ack_observed": response_payload.startswith("(ACK"),
        "ack_response": "(ACK9 ",
    }
    return decoded


def decode_maximum_mains_charging_current_write(command: str, response_payload: str) -> dict[str, Any]:
    match = MAXIMUM_MAINS_CHARGING_CURRENT_WRITE_RE.fullmatch(command)
    if match is None:
        return {"raw_payload": response_payload, "request_command": command, "tokens": [command, response_payload]}

    requested_value = parse_scalar(match.group("value"))
    decoded: dict[str, Any] = {
        "raw_payload": response_payload,
        "request_command": command,
        "command_family": "maximumMainsChargingCurrentSetting",
        "tokens": [match.group("value"), response_payload],
        "requested_value": requested_value,
        "portal_key": "maximumMainsChargingCurrentSetting",
        "portal_label": "Maximum Mains Charging Current Setting",
        "portal_value": requested_value,
        "ack_observed": response_payload.startswith("(ACK"),
        "ack_response": "(ACK9 ",
    }
    return decoded


def decode_maximum_charging_current_write(command: str, response_payload: str) -> dict[str, Any]:
    match = MAXIMUM_CHARGING_CURRENT_WRITE_RE.fullmatch(command)
    if match is None:
        return {"raw_payload": response_payload, "request_command": command, "tokens": [command, response_payload]}

    requested_value = parse_scalar(match.group("value"))
    decoded: dict[str, Any] = {
        "raw_payload": response_payload,
        "request_command": command,
        "command_family": "maximumChargingCurrentSetting",
        "tokens": [match.group("value"), response_payload],
        "requested_value": requested_value,
        "portal_key": "maximumChargingCurrentSetting",
        "portal_label": "Maximum Charging Current Setting",
        "portal_value": requested_value,
        "ack_observed": response_payload.startswith("(ACK"),
        "ack_response": "(ACK9 ",
    }
    return decoded


def decode_battery_type_write(command: str, response_payload: str) -> dict[str, Any]:
    match = BATTERY_TYPE_WRITE_RE.fullmatch(command)
    if match is None:
        return {"raw_payload": response_payload, "request_command": command, "tokens": [command, response_payload]}

    requested_value = match.group("value")
    portal_value = BATTERY_TYPE_VALUE_LABELS.get(requested_value, f"unknown:{requested_value}")
    decoded: dict[str, Any] = {
        "raw_payload": response_payload,
        "request_command": command,
        "command_family": "batteryTypeSetting",
        "tokens": [requested_value, response_payload],
        "requested_value": requested_value,
        "portal_key": "batteryTypeSetting",
        "portal_label": "Battery Type Setting",
        "portal_value": portal_value,
        "ack_observed": response_payload.startswith("(ACK"),
        "ack_response": "(ACK9 ",
    }
    return decoded


def decode_battery_float_charging_voltage_write(command: str, response_payload: str) -> dict[str, Any]:
    match = BATTERY_FLOAT_CHARGING_VOLTAGE_WRITE_RE.fullmatch(command)
    if match is None:
        return {"raw_payload": response_payload, "request_command": command, "tokens": [command, response_payload]}

    requested_value = parse_scalar(match.group("value"))
    decoded: dict[str, Any] = {
        "raw_payload": response_payload,
        "request_command": command,
        "command_family": "batteryFloatChargingVoltageSetting",
        "tokens": [match.group("value"), response_payload],
        "requested_value": requested_value,
        "portal_key": "batteryFloatChargingVoltageSetting",
        "portal_label": "Battery Float Charging Voltage Setting",
        "portal_value": requested_value,
        "ack_observed": response_payload.startswith("(NAK"),
        "ack_response": "(NAKss",
    }
    return decoded


def decode_battery_equalization_time_write(command: str, response_payload: str) -> dict[str, Any]:
    match = BATTERY_EQUALIZATION_TIME_WRITE_RE.fullmatch(command)
    if match is None:
        return {"raw_payload": response_payload, "request_command": command, "tokens": [command, response_payload]}

    requested_value = parse_scalar(match.group("value"))
    decoded: dict[str, Any] = {
        "raw_payload": response_payload,
        "request_command": command,
        "command_family": "batteryEqualizationTimeSetting",
        "tokens": [match.group("value"), response_payload],
        "requested_value": requested_value,
        "portal_key": "batteryEqualizationTimeSetting",
        "portal_label": "Battery Equalization Time Setting",
        "portal_value": requested_value,
        "ack_observed": response_payload.startswith("(ACK"),
        "ack_response": "(ACK9 ",
    }
    return decoded


def decode_restore_second_output_battery_capacity_write(command: str, response_payload: str) -> dict[str, Any]:
    match = RESTORE_SECOND_OUTPUT_BATTERY_CAPACITY_WRITE_RE.fullmatch(command)
    if match is None:
        return {"raw_payload": response_payload, "request_command": command, "tokens": [command, response_payload]}

    requested_value = parse_scalar(match.group("value"))
    decoded: dict[str, Any] = {
        "raw_payload": response_payload,
        "request_command": command,
        "command_family": "restoreSecondOutputBatCapacitySetting",
        "tokens": [match.group("value"), response_payload],
        "requested_value": requested_value,
        "portal_key": "restoreSecondOutputBatCapacitySetting",
        "portal_label": "Restore Second Output Battery Capacity Setting",
        "portal_value": requested_value,
        "ack_observed": response_payload.startswith("(ACK"),
        "ack_response": "(ACK9 ",
    }
    return decoded


def decode_restore_second_output_delay_time_write(command: str, response_payload: str) -> dict[str, Any]:
    match = RESTORE_SECOND_OUTPUT_DELAY_TIME_WRITE_RE.fullmatch(command)
    if match is None:
        return {"raw_payload": response_payload, "request_command": command, "tokens": [command, response_payload]}

    requested_value = parse_scalar(match.group("value"))
    decoded: dict[str, Any] = {
        "raw_payload": response_payload,
        "request_command": command,
        "command_family": "restoreSecondOutputDelayTimeSetting",
        "tokens": [match.group("value"), response_payload],
        "requested_value": requested_value,
        "portal_key": "restoreSecondOutputDelayTimeSetting",
        "portal_label": "Restore Second Output Delay Time Setting",
        "portal_value": requested_value,
        "ack_observed": response_payload.startswith("(ACK"),
        "ack_response": "(ACK9 ",
    }
    return decoded


DECODERS = {
    "HSTS": decode_hsts,
    "HGRID": decode_hgrid,
    "HOP": decode_hop,
    "HBAT": decode_hbat,
    "HPV": decode_hpv,
    "HTEMP": decode_htemp,
    "HGEN": decode_hgen,
    "QPRTL": decode_qprtl,
    "HIMSG1": decode_himsg1,
    "HBMS1": decode_hbms1,
    "HBMS2": decode_hbms2,
    "HBMS3": decode_hbms3,
    "HEEP1": decode_heep,
    "HEEP2": decode_heep,
    "HPVB": decode_hpvb,
}


FIELD_METADATA: dict[str, dict[str, dict[str, Any]]] = {
    "HSTS": {
        "status_code": {"unit": None, "confidence": "high", "raw_index": 0},
        "mode_code": {"unit": None, "confidence": "high", "raw_index": 1},
        "mode_label": {"unit": None, "confidence": "high", "raw_index": 1},
        "status_bits": {"unit": None, "confidence": "high", "raw_index": 1},
        "fault_bits": {"unit": None, "confidence": "high", "raw_index": 2},
    },
    "HGRID": {
        "ac_input_voltage_v": {"unit": "V", "confidence": "high", "raw_index": 0},
        "mains_frequency_hz": {"unit": "Hz", "confidence": "high", "raw_index": 1},
        "high_mains_loss_voltage_v": {"unit": "V", "confidence": "high", "raw_index": 2},
        "low_mains_loss_voltage_v": {"unit": "V", "confidence": "high", "raw_index": 3},
        "high_mains_loss_frequency_hz": {"unit": "Hz", "confidence": "high", "raw_index": 4},
        "low_mains_loss_frequency_hz": {"unit": "Hz", "confidence": "high", "raw_index": 5},
        "mains_power_w": {"unit": "W", "confidence": "high", "raw_index": 6},
        "mains_current_flow_direction_code": {"unit": None, "confidence": "high", "raw_index": 7},
        "mains_current_flow_direction": {"unit": None, "confidence": "high", "raw_index": 7},
        "rated_power_w": {"unit": "W", "confidence": "high", "raw_index": 8},
    },
    "HOP": {
        "ac_output_voltage_v": {"unit": "V", "confidence": "high", "raw_index": 0},
        "ac_output_frequency_hz": {"unit": "Hz", "confidence": "high", "raw_index": 1},
        "output_apparent_power_va": {"unit": "VA", "confidence": "high", "raw_index": 2},
        "output_active_power_w": {"unit": "W", "confidence": "high", "raw_index": 3},
        "output_load_percent": {"unit": "%", "confidence": "high", "raw_index": 4},
        "output_dc_component_status": {"unit": None, "confidence": "high", "raw_index": 5},
        "rated_power_w": {"unit": "W", "confidence": "high", "raw_index": 6},
        "inductor_current_a": {"unit": "A", "confidence": "high", "raw_index": 7},
    },
    "HBAT": {
        "battery_type_code": {"unit": None, "confidence": "high", "raw_index": 0},
        "battery_type": {"unit": None, "confidence": "high", "raw_index": 0},
        "battery_voltage_v": {"unit": "V", "confidence": "high", "raw_index": 1},
        "battery_capacity_percent": {"unit": "%", "confidence": "high", "raw_index": 2},
        "battery_charging_current_a": {"unit": "A", "confidence": "high", "raw_index": 3},
        "battery_discharge_current_a": {"unit": "A", "confidence": "high", "raw_index": 4},
        "bus_voltage_v": {"unit": "V", "confidence": "high", "raw_index": 5},
    },
    "HPV": {
        "pv_voltage_v": {"unit": "V", "confidence": "high", "raw_index": 0},
        "pv_current_a": {"unit": "A", "confidence": "high", "raw_index": 1},
        "pv_power_w": {"unit": "W", "confidence": "high", "raw_index": 2},
        "generation_power_kw": {"unit": "kW", "confidence": "high", "raw_index": 3},
    },
    "HPVB": {
        "pv_voltage_v": {"unit": "V", "confidence": "medium", "raw_index": 0},
        "pv_current_a": {"unit": "A", "confidence": "medium", "raw_index": 1},
        "pv_power_w": {"unit": "W", "confidence": "medium", "raw_index": 2},
        "pv_charging_mark_code": {"unit": None, "confidence": "medium", "raw_index": 3},
        "pv_charging_mark": {"unit": None, "confidence": "medium", "raw_index": 3},
        "bus_voltage_v": {"unit": "V", "confidence": "medium", "raw_index": 4},
        "raw_tail": {"unit": None, "confidence": "medium", "raw_index": 5},
    },
    "inverterSystemClock": {
        "clock_selector": {"unit": None, "confidence": "high"},
        "clock_digits": {"unit": None, "confidence": "high"},
        "clock_iso": {"unit": None, "confidence": "high"},
        "portal_key": {"unit": None, "confidence": "high"},
        "portal_label": {"unit": None, "confidence": "high"},
        "portal_value": {"unit": None, "confidence": "high"},
        "ack_observed": {"unit": None, "confidence": "high"},
        "ack_response": {"unit": None, "confidence": "high"},
    },
    "restoreSecondOutputBatCapacitySetting": {
        "requested_value": {"unit": "%", "confidence": "medium", "raw_index": 0},
        "portal_key": {"unit": None, "confidence": "medium", "raw_index": 0},
        "portal_label": {"unit": None, "confidence": "medium", "raw_index": 0},
        "portal_value": {"unit": "%", "confidence": "medium", "raw_index": 0},
        "ack_observed": {"unit": None, "confidence": "medium", "raw_index": 1},
        "ack_response": {"unit": None, "confidence": "medium", "raw_index": 1},
    },
    "batteryEqualizationVoltageSetting": {
        "requested_value": {"unit": "V", "confidence": "high", "raw_index": 0},
        "portal_key": {"unit": None, "confidence": "high", "raw_index": 0},
        "portal_label": {"unit": None, "confidence": "high", "raw_index": 0},
        "portal_value": {"unit": "V", "confidence": "high", "raw_index": 0},
        "ack_observed": {"unit": None, "confidence": "high", "raw_index": 1},
        "ack_response": {"unit": None, "confidence": "high", "raw_index": 1},
    },
    "batteryEqualizationIntervalSetting": {
        "requested_value": {"unit": "day", "confidence": "high", "raw_index": 0},
        "portal_key": {"unit": None, "confidence": "high", "raw_index": 0},
        "portal_label": {"unit": None, "confidence": "high", "raw_index": 0},
        "portal_value": {"unit": "day", "confidence": "high", "raw_index": 0},
        "ack_observed": {"unit": None, "confidence": "high", "raw_index": 1},
        "ack_response": {"unit": None, "confidence": "high", "raw_index": 1},
    },
    "batteryEqualizationTimeoutSetting": {
        "requested_value": {"unit": "min", "confidence": "high", "raw_index": 0},
        "portal_key": {"unit": None, "confidence": "high", "raw_index": 0},
        "portal_label": {"unit": None, "confidence": "high", "raw_index": 0},
        "portal_value": {"unit": "min", "confidence": "high", "raw_index": 0},
        "ack_observed": {"unit": None, "confidence": "high", "raw_index": 1},
        "ack_response": {"unit": None, "confidence": "high", "raw_index": 1},
    },
    "batteryCutOffVoltageSetting": {
        "requested_value": {"unit": "V", "confidence": "high", "raw_index": 0},
        "portal_key": {"unit": None, "confidence": "high", "raw_index": 0},
        "portal_label": {"unit": None, "confidence": "high", "raw_index": 0},
        "portal_value": {"unit": "V", "confidence": "high", "raw_index": 0},
        "ack_observed": {"unit": None, "confidence": "high", "raw_index": 1},
        "ack_response": {"unit": None, "confidence": "high", "raw_index": 1},
    },
    "batteryConstantChargingVoltageSetting": {
        "requested_value": {"unit": "V", "confidence": "medium", "raw_index": 0},
        "portal_key": {"unit": None, "confidence": "medium", "raw_index": 0},
        "portal_label": {"unit": None, "confidence": "medium", "raw_index": 0},
        "portal_value": {"unit": "V", "confidence": "medium", "raw_index": 0},
        "ack_observed": {"unit": None, "confidence": "medium", "raw_index": 1},
        "ack_response": {"unit": None, "confidence": "medium", "raw_index": 1},
    },
    "batteryRechargeVoltageSetting": {
        "requested_value": {"unit": "V", "confidence": "medium-high", "raw_index": 0},
        "portal_key": {"unit": None, "confidence": "medium-high", "raw_index": 0},
        "portal_label": {"unit": None, "confidence": "medium-high", "raw_index": 0},
        "portal_value": {"unit": "V", "confidence": "medium-high", "raw_index": 0},
        "ack_observed": {"unit": None, "confidence": "medium-high", "raw_index": 1},
        "ack_response": {"unit": None, "confidence": "medium-high", "raw_index": 1},
    },
    "batteryRedischargeVoltageSetting": {
        "requested_value": {"unit": "V", "confidence": "medium-high", "raw_index": 0},
        "portal_key": {"unit": None, "confidence": "medium-high", "raw_index": 0},
        "portal_label": {"unit": None, "confidence": "medium-high", "raw_index": 0},
        "portal_value": {"unit": "V", "confidence": "medium-high", "raw_index": 0},
        "ack_observed": {"unit": None, "confidence": "medium-high", "raw_index": 1},
        "ack_response": {"unit": None, "confidence": "medium-high", "raw_index": 1},
    },
    "batteryTypeSetting": {
        "requested_value": {"unit": None, "confidence": "medium-high", "raw_index": 0},
        "portal_key": {"unit": None, "confidence": "medium-high", "raw_index": 0},
        "portal_label": {"unit": None, "confidence": "medium-high", "raw_index": 0},
        "portal_value": {"unit": None, "confidence": "medium-high", "raw_index": 0},
        "ack_observed": {"unit": None, "confidence": "medium-high", "raw_index": 1},
        "ack_response": {"unit": None, "confidence": "medium-high", "raw_index": 1},
    },
    "bmsFunctionEnableSetting": {
        "requested_value": {"unit": None, "confidence": "high", "raw_index": 0},
        "portal_key": {"unit": None, "confidence": "high", "raw_index": 0},
        "portal_label": {"unit": None, "confidence": "high", "raw_index": 0},
        "portal_value": {"unit": None, "confidence": "high", "raw_index": 0},
        "ack_observed": {"unit": None, "confidence": "high", "raw_index": 1},
        "ack_response": {"unit": None, "confidence": "high", "raw_index": 1},
    },
    "batteryEqualizationTimeSetting": {
        "requested_value": {"unit": "min", "confidence": "high", "raw_index": 0},
        "portal_key": {"unit": None, "confidence": "high", "raw_index": 0},
        "portal_label": {"unit": None, "confidence": "high", "raw_index": 0},
        "portal_value": {"unit": "min", "confidence": "high", "raw_index": 0},
        "ack_observed": {"unit": None, "confidence": "high", "raw_index": 1},
        "ack_response": {"unit": None, "confidence": "high", "raw_index": 1},
    },
    "restoreSecondOutputDelayTimeSetting": {
        "requested_value": {"unit": "min", "confidence": "medium", "raw_index": 0},
        "portal_key": {"unit": None, "confidence": "medium", "raw_index": 0},
        "portal_label": {"unit": None, "confidence": "medium", "raw_index": 0},
        "portal_value": {"unit": "min", "confidence": "medium", "raw_index": 0},
        "ack_observed": {"unit": None, "confidence": "medium", "raw_index": 1},
        "ack_response": {"unit": None, "confidence": "medium", "raw_index": 1},
    },
    "PDa": {
        "portal_key": {"unit": None, "confidence": "high", "raw_index": 0},
        "portal_label": {"unit": None, "confidence": "high", "raw_index": 0},
        "portal_value": {"unit": None, "confidence": "high", "raw_index": 0},
        "ack_observed": {"unit": None, "confidence": "high", "raw_index": 1},
        "ack_response": {"unit": None, "confidence": "high", "raw_index": 1},
        "heep1_token_6_effect": {"unit": None, "confidence": "high", "raw_index": 0},
    },
    "PEa": {
        "portal_key": {"unit": None, "confidence": "high", "raw_index": 0},
        "portal_label": {"unit": None, "confidence": "high", "raw_index": 0},
        "portal_value": {"unit": None, "confidence": "high", "raw_index": 0},
        "ack_observed": {"unit": None, "confidence": "high", "raw_index": 1},
        "ack_response": {"unit": None, "confidence": "high", "raw_index": 1},
        "heep1_token_6_effect": {"unit": None, "confidence": "high", "raw_index": 0},
    },
    "PDx`Y": {
        "portal_key": {"unit": None, "confidence": "high", "raw_index": 0},
        "portal_label": {"unit": None, "confidence": "high", "raw_index": 0},
        "portal_value": {"unit": None, "confidence": "high", "raw_index": 0},
        "ack_observed": {"unit": None, "confidence": "high", "raw_index": 1},
        "ack_response": {"unit": None, "confidence": "high", "raw_index": 1},
        "heep1_token_6_effect": {"unit": None, "confidence": "high", "raw_index": 0},
    },
    "PExSh": {
        "portal_key": {"unit": None, "confidence": "high", "raw_index": 0},
        "portal_label": {"unit": None, "confidence": "high", "raw_index": 0},
        "portal_value": {"unit": None, "confidence": "high", "raw_index": 0},
        "ack_observed": {"unit": None, "confidence": "high", "raw_index": 1},
        "ack_response": {"unit": None, "confidence": "high", "raw_index": 1},
        "heep1_token_6_effect": {"unit": None, "confidence": "high", "raw_index": 0},
    },
    "PDk": {
        "portal_key": {"unit": None, "confidence": "high", "raw_index": 0},
        "portal_label": {"unit": None, "confidence": "high", "raw_index": 0},
        "portal_value": {"unit": None, "confidence": "high", "raw_index": 0},
        "ack_observed": {"unit": None, "confidence": "high", "raw_index": 1},
        "ack_response": {"unit": None, "confidence": "high", "raw_index": 1},
        "heep1_token_5_effect": {"unit": None, "confidence": "high", "raw_index": 0},
    },
    "PEkq:": {
        "portal_key": {"unit": None, "confidence": "high", "raw_index": 0},
        "portal_label": {"unit": None, "confidence": "high", "raw_index": 0},
        "portal_value": {"unit": None, "confidence": "high", "raw_index": 0},
        "ack_observed": {"unit": None, "confidence": "high", "raw_index": 1},
        "ack_response": {"unit": None, "confidence": "high", "raw_index": 1},
        "heep1_token_5_effect": {"unit": None, "confidence": "high", "raw_index": 0},
    },
    "PDv": {
        "portal_key": {"unit": None, "confidence": "high", "raw_index": 0},
        "portal_label": {"unit": None, "confidence": "high", "raw_index": 0},
        "portal_value": {"unit": None, "confidence": "high", "raw_index": 0},
        "ack_observed": {"unit": None, "confidence": "high", "raw_index": 1},
        "ack_response": {"unit": None, "confidence": "high", "raw_index": 1},
    },
    "PEv": {
        "portal_key": {"unit": None, "confidence": "high", "raw_index": 0},
        "portal_label": {"unit": None, "confidence": "high", "raw_index": 0},
        "portal_value": {"unit": None, "confidence": "high", "raw_index": 0},
        "ack_observed": {"unit": None, "confidence": "high", "raw_index": 1},
        "ack_response": {"unit": None, "confidence": "high", "raw_index": 1},
    },
    "PDu": {
        "portal_key": {"unit": None, "confidence": "high", "raw_index": 0},
        "portal_label": {"unit": None, "confidence": "high", "raw_index": 0},
        "portal_value": {"unit": None, "confidence": "high", "raw_index": 0},
        "ack_observed": {"unit": None, "confidence": "high", "raw_index": 1},
        "ack_response": {"unit": None, "confidence": "high", "raw_index": 1},
    },
    "PEu": {
        "portal_key": {"unit": None, "confidence": "high", "raw_index": 0},
        "portal_label": {"unit": None, "confidence": "high", "raw_index": 0},
        "portal_value": {"unit": None, "confidence": "high", "raw_index": 0},
        "ack_observed": {"unit": None, "confidence": "high", "raw_index": 1},
        "ack_response": {"unit": None, "confidence": "high", "raw_index": 1},
    },
    "PDypx": {
        "portal_key": {"unit": None, "confidence": "high", "raw_index": 0},
        "portal_label": {"unit": None, "confidence": "high", "raw_index": 0},
        "portal_value": {"unit": None, "confidence": "high", "raw_index": 0},
        "ack_observed": {"unit": None, "confidence": "high", "raw_index": 1},
        "ack_response": {"unit": None, "confidence": "high", "raw_index": 1},
        "heep1_token_6_effect": {"unit": None, "confidence": "high", "raw_index": 0},
    },
    "PEyCI": {
        "portal_key": {"unit": None, "confidence": "high", "raw_index": 0},
        "portal_label": {"unit": None, "confidence": "high", "raw_index": 0},
        "portal_value": {"unit": None, "confidence": "high", "raw_index": 0},
        "ack_observed": {"unit": None, "confidence": "high", "raw_index": 1},
        "ack_response": {"unit": None, "confidence": "high", "raw_index": 1},
        "heep1_token_6_effect": {"unit": None, "confidence": "high", "raw_index": 0},
    },
    "HTEMP": {
        "inverter_temperature_c": {"unit": "C", "confidence": "high", "raw_index": 0},
        "boost_temperature_c": {"unit": "C", "confidence": "high", "raw_index": 1},
        "transformer_temperature_c": {"unit": "C", "confidence": "high", "raw_index": 2},
        "pv_temperature_c": {"unit": "C", "confidence": "high", "raw_index": 3},
        "fan_1_speed_percent": {"unit": "%", "confidence": "high", "raw_index": 4},
        "fan_2_speed_percent": {"unit": "%", "confidence": "high", "raw_index": 5},
        "max_temperature_c": {"unit": "C", "confidence": "high", "raw_index": 6},
        "temperature_status_bits": {"unit": None, "confidence": "high", "raw_index": 7},
    },
    "HGEN": {
        "date_ymd": {"unit": None, "confidence": "high", "raw_index": 0},
        "date_iso": {"unit": None, "confidence": "high", "raw_index": 0},
        "time_hm": {"unit": None, "confidence": "high", "raw_index": 1},
        "daily_power_gen_kwh": {"unit": "kWh", "confidence": "high", "raw_index": 2},
        "monthly_electricity_generation_kwh": {"unit": "kWh", "confidence": "high", "raw_index": 3},
        "yearly_electricity_generation_kwh": {"unit": "kWh", "confidence": "high", "raw_index": 4},
        "total_power_generation_kwh": {"unit": "kWh", "confidence": "high", "raw_index": 5},
    },
    "QPRTL": {
        "device_type": {"unit": None, "confidence": "medium", "raw_index": 0},
    },
    "HIMSG1": {
        "software_version": {"unit": None, "confidence": "high", "raw_index": 0},
        "software_date": {"unit": None, "confidence": "high", "raw_index": 1},
        "software_date_iso": {"unit": None, "confidence": "high", "raw_index": 1},
        "revision": {"unit": None, "confidence": "high", "raw_index": 2},
    },
    "HBMS1": {
        "bms_status_code": {"unit": None, "confidence": "medium", "raw_index": 0},
        "bms_flags": {"unit": None, "confidence": "medium", "raw_index": 1},
        "bms_discharge_voltage_limit_v": {"unit": "V", "confidence": "medium", "raw_index": 2},
        "bms_charge_voltage_limit_v": {"unit": "V", "confidence": "medium", "raw_index": 3},
        "bms_charge_current_limit_a": {"unit": "A", "confidence": "medium", "raw_index": 4},
        "bms_soc_percent": {"unit": "%", "confidence": "medium-high", "raw_index": 5},
        "bms_charging_current_a": {"unit": "A", "confidence": "medium-high", "raw_index": 6},
        "bms_discharge_current_a": {"unit": "A", "confidence": "medium-high", "raw_index": 7},
    },
    "HBMS2": {
        "remaining_capacity": {"unit": "Ah", "confidence": "low", "raw_index": 0},
        "nominal_capacity": {"unit": "Ah", "confidence": "low", "raw_index": 1},
        "display_mode": {"unit": None, "confidence": "medium", "raw_index": 2},
        "max_voltage": {"unit": "mV", "confidence": "medium", "raw_index": 3},
        "max_voltage_cell_position": {"unit": "cell", "confidence": "medium", "raw_index": 4},
        "min_voltage": {"unit": "mV", "confidence": "medium", "raw_index": 5},
        "min_voltage_cell_position": {"unit": "cell", "confidence": "medium", "raw_index": 6},
    },
    "HBMS3": {
        "cell_voltage_list": {"unit": "mV", "confidence": "low", "raw_index": 0},
        "raw_tail": {"unit": None, "confidence": "low", "raw_index": 16},
    },
    "chargerPrioritySetting": {
        "command_family": {"unit": None, "confidence": "medium-high"},
        "portal_key": {"unit": None, "confidence": "medium-high"},
        "portal_label": {"unit": None, "confidence": "medium-high"},
        "portal_value": {"unit": None, "confidence": "medium-high"},
        "ack_observed": {"unit": None, "confidence": "medium-high"},
        "ack_response": {"unit": None, "confidence": "medium-high"},
    },
    "outputSourcePrioritySetting": {
        "command_family": {"unit": None, "confidence": "high"},
        "portal_key": {"unit": None, "confidence": "high"},
        "portal_label": {"unit": None, "confidence": "high"},
        "portal_value": {"unit": None, "confidence": "high"},
        "ack_observed": {"unit": None, "confidence": "high"},
        "ack_response": {"unit": None, "confidence": "high"},
    },
    "pvEnergyFeedingPrioritySetting": {
        "command_family": {"unit": None, "confidence": "high"},
        "portal_key": {"unit": None, "confidence": "high"},
        "portal_label": {"unit": None, "confidence": "high"},
        "portal_value": {"unit": None, "confidence": "high"},
        "ack_observed": {"unit": None, "confidence": "high"},
        "ack_response": {"unit": None, "confidence": "high"},
    },
}


def decode_payload(command: str, payload: str) -> dict[str, Any]:
    if CLOCK_WRITE_RE.fullmatch(command):
        return decode_clock_write(command, payload)
    if BATTERY_EQUALIZATION_VOLTAGE_WRITE_RE.fullmatch(command):
        return decode_battery_equalization_voltage_write(command, payload)
    if BATTERY_EQUALIZATION_INTERVAL_WRITE_RE.fullmatch(command):
        return decode_battery_equalization_interval_write(command, payload)
    if BATTERY_EQUALIZATION_TIMEOUT_WRITE_RE.fullmatch(command):
        return decode_battery_equalization_timeout_write(command, payload)
    if BATTERY_CUT_OFF_VOLTAGE_WRITE_RE.fullmatch(command):
        return decode_battery_cut_off_voltage_write(command, payload)
    if BATTERY_BULK_VOLTAGE_WRITE_RE.fullmatch(command):
        return decode_battery_constant_charging_voltage_write(command, payload)
    if BATTERY_RECHARGE_VOLTAGE_WRITE_RE.fullmatch(command):
        return decode_battery_recharge_voltage_write(command, payload)
    if BATTERY_REDISCHARGE_VOLTAGE_WRITE_RE.fullmatch(command):
        return decode_battery_redischarge_voltage_write(command, payload)
    if BATTERY_TYPE_WRITE_RE.fullmatch(command):
        return decode_battery_type_write(command, payload)
    if BMSC_WRITE_RE.fullmatch(command):
        return decode_bms_function_enable_write(command, payload)
    if BMSSDC_WRITE_RE.fullmatch(command):
        return decode_bms_lock_machine_battery_capacity_write(command, payload)
    if GRID_CONNECTED_CURRENT_WRITE_RE.fullmatch(command):
        return decode_grid_connected_current_write(command, payload)
    if MAXIMUM_MAINS_CHARGING_CURRENT_WRITE_RE.fullmatch(command):
        return decode_maximum_mains_charging_current_write(command, payload)
    if MAXIMUM_CHARGING_CURRENT_WRITE_RE.fullmatch(command):
        return decode_maximum_charging_current_write(command, payload)
    if BATTERY_FLOAT_CHARGING_VOLTAGE_WRITE_RE.fullmatch(command):
        return decode_battery_float_charging_voltage_write(command, payload)
    if BATTERY_EQUALIZATION_TIME_WRITE_RE.fullmatch(command):
        return decode_battery_equalization_time_write(command, payload)
    if RESTORE_SECOND_OUTPUT_BATTERY_CAPACITY_WRITE_RE.fullmatch(command):
        return decode_restore_second_output_battery_capacity_write(command, payload)
    if RESTORE_SECOND_OUTPUT_DELAY_TIME_WRITE_RE.fullmatch(command):
        return decode_restore_second_output_delay_time_write(command, payload)
    if command in WRITE_CONTROL_COMMANDS:
        return decode_write_control(command, payload)
    decoder = DECODERS.get(command)
    if decoder is None:
        return {"raw_payload": payload, "tokens": split_payload(payload)}
    return decoder(payload)


def build_decoded_field_metadata(command: str, decoded: dict[str, Any]) -> dict[str, Any]:
    tokens = decoded.get("tokens") if isinstance(decoded.get("tokens"), list) else []
    specs = FIELD_METADATA.get(command, {})
    annotated: dict[str, Any] = {}
    for field_name, spec in specs.items():
        if field_name not in decoded:
            continue
        raw_index = spec.get("raw_index")
        raw_value: Any = decoded.get(field_name)
        if isinstance(raw_index, int) and 0 <= raw_index < len(tokens):
            if isinstance(raw_value, list):
                raw_value = tokens[raw_index:]
            else:
                raw_value = tokens[raw_index]
        annotated[field_name] = {
            "value": decoded.get(field_name),
            "unit": spec.get("unit"),
            "raw": raw_value,
            "confidence": spec.get("confidence", "medium"),
            "source_command": command,
        }
    return annotated


def extract_text_payload(obj: Any) -> dict[str, Any]:
    if isinstance(obj, dict):
        out: dict[str, Any] = {}
        for key, value in obj.items():
            if isinstance(value, (dict, list)):
                out[key] = extract_text_payload(value)
            else:
                out[key] = value
        return out
    if isinstance(obj, list):
        return [extract_text_payload(item) for item in obj]  # type: ignore[list-item]
    return {"value": obj}


def parse_capture(lines: list[str]) -> list[FrameRecord]:
    return iter_sniffer_frames(lines)


def serialize_frame(frame: FrameRecord) -> dict[str, Any]:
    return {
        "seq": frame.seq,
        "ts_ms": frame.ts_ms,
        "end_ms": frame.end_ms,
        "dur_ms": frame.dur_ms,
        "channel": frame.channel,
        "idx": frame.idx,
        "length": frame.length,
        "reason": frame.reason,
        "frame_kind": frame.frame_kind,
        "frame_style": frame.frame_style,
        "command": frame.command,
        "payload_text": frame.payload_text,
        "payload_hex": bytes_to_hex(frame.payload_bytes),
        "crc_wire": frame.crc_wire,
        "crc_expected": frame.crc_expected,
        "crc_ok": frame.crc_ok,
        "raw_hex": frame.hex_text,
        "raw_ascii": frame.ascii_text,
    }


def pair_frames(frames: list[FrameRecord]) -> tuple[list[dict[str, Any]], list[FrameRecord], list[FrameRecord]]:
    pairs: list[dict[str, Any]] = []
    pending_requests: list[FrameRecord] = []
    unpaired_responses: list[FrameRecord] = []
    for frame in frames:
        if frame.frame_kind == "request":
            pending_requests.append(frame)
            continue
        if frame.frame_kind != "response":
            continue

        candidate_index: int | None = None
        candidate_score: tuple[int, int, int, int] | None = None
        for index, request in enumerate(pending_requests):
            if request.ts_ms > frame.ts_ms:
                continue
            same_channel_penalty = 0 if request.channel != frame.channel else 1
            score = (same_channel_penalty, frame.ts_ms - request.ts_ms, request.idx, request.seq)
            if candidate_score is None or score < candidate_score:
                candidate_index = index
                candidate_score = score

        if candidate_index is None:
            unpaired_responses.append(frame)
            continue

        request = pending_requests.pop(candidate_index)
        command = request.command or request.payload_text.lstrip("(").strip()
        decoded = decode_payload(command, frame.payload_text)
        normalized_command = decoded.get("command_family") or command
        pairs.append(
            {
                "command": normalized_command,
                "request_command": command,
                "request": request,
                "response": frame,
                "decoded": decoded,
                "decoded_fields": build_decoded_field_metadata(normalized_command, decoded),
            }
        )

    return pairs, pending_requests, unpaired_responses


def write_frames_csv(path: Path, frames: list[FrameRecord]) -> None:
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=[
                "seq",
                "ts_ms",
                "end_ms",
                "dur_ms",
                "channel",
                "idx",
                "length",
                "reason",
                "frame_kind",
                "frame_style",
                "command",
                "payload_text",
                "payload_hex",
                "crc_wire",
                "crc_expected",
                "crc_ok",
                "raw_hex",
                "raw_ascii",
            ],
        )
        writer.writeheader()
        for frame in frames:
            row = serialize_frame(frame)
            writer.writerow(
                {
                    **row,
                    "command": row["command"] or "",
                    "crc_wire": row["crc_wire"] or "",
                    "crc_expected": row["crc_expected"] or "",
                    "crc_ok": "" if row["crc_ok"] is None else str(row["crc_ok"]).lower(),
                }
            )


def write_frames_jsonl(path: Path, frames: list[FrameRecord]) -> None:
    with path.open("w", encoding="utf-8") as handle:
        for frame in frames:
            handle.write(json.dumps(serialize_frame(frame), ensure_ascii=False) + "\n")


def write_command_jsonl(
    path: Path,
    pairs: list[dict[str, Any]],
) -> tuple[Counter[str], Counter[str]]:
    command_counts: Counter[str] = Counter()
    field_counts: Counter[str] = Counter()
    with path.open("w", encoding="utf-8") as handle:
        for pair in pairs:
            command = pair["command"]
            request: FrameRecord = pair["request"]
            response: FrameRecord | None = pair["response"]
            decoded = pair["decoded"]
            command_counts[command] += 1
            record: dict[str, Any] = {
                "command": command,
                "request": {
                    "seq": request.seq,
                    "channel": request.channel,
                    "ts_ms": request.ts_ms,
                    "payload_text": request.payload_text,
                    "payload_hex": bytes_to_hex(request.payload_bytes),
                    "frame_style": request.frame_style,
                    "crc_ok": request.crc_ok,
                    "frame_hex": request.hex_text,
                },
                "response": None,
                "decoded": decoded,
                "decoded_fields": pair.get("decoded_fields", {}),
            }
            if response is not None:
                record["response"] = {
                    "seq": response.seq,
                    "channel": response.channel,
                    "ts_ms": response.ts_ms,
                    "payload_text": response.payload_text,
                    "payload_hex": bytes_to_hex(response.payload_bytes),
                    "frame_style": response.frame_style,
                    "crc_ok": response.crc_ok,
                    "frame_hex": response.hex_text,
                }
                if pair.get("decoded_fields"):
                    for field_name in pair["decoded_fields"]:
                        field_counts[f"{command}.{field_name}"] += 1
            handle.write(json.dumps(record, ensure_ascii=False) + "\n")
    return command_counts, field_counts


def build_report(
    *,
    frames: list[FrameRecord],
    pairs: list[dict[str, Any]],
    unpaired_requests: list[FrameRecord],
    unpaired_responses: list[FrameRecord],
    command_counts: Counter[str],
    field_counts: Counter[str],
) -> str:
    lines = ["# H Protocol Decode Report", ""]
    lines.append(f"- Frames parsed: {len(frames)}")
    lines.append(f"- Request/response pairs: {sum(1 for item in pairs if item['response'] is not None)}")
    style_counts = Counter(frame.frame_style for frame in frames)
    if style_counts:
        lines.append("- Frame styles: " + ", ".join(f"{style}={count}" for style, count in style_counts.items()))
    lines.append("")
    lines.append("## Command Counts")
    if command_counts:
        for command, count in command_counts.most_common():
            lines.append(f"- {command}: {count}")
    else:
        lines.append("- none")
    lines.append("")
    lines.append("## Field Counts")
    if field_counts:
        for key, count in field_counts.most_common():
            lines.append(f"- {key}: {count}")
    else:
        lines.append("- none")
    lines.append("")
    lines.append("## Unpaired Requests")
    if unpaired_requests:
        for request in unpaired_requests:
            command = request.command or request.payload_text
            lines.append(f"- {command} (seq={request.seq}, ch={request.channel}, ts={request.ts_ms})")
    else:
        lines.append("- none")
    lines.append("")
    lines.append("## Unpaired Responses")
    if unpaired_responses:
        for response in unpaired_responses:
            lines.append(f"- seq={response.seq} (ch={response.channel}, ts={response.ts_ms}, len={response.length})")
    else:
        lines.append("- none")
    lines.append("")
    lines.append("## Output Files")
    lines.append("- frames.csv")
    lines.append("- frames.jsonl")
    lines.append("- command_responses.jsonl")
    lines.append("- decoded_state.json")
    lines.append("- unpaired_frames.md")
    lines.append("")
    return "\n".join(lines)


def write_unpaired_frames_md(
    path: Path,
    unpaired_requests: list[FrameRecord],
    unpaired_responses: list[FrameRecord],
) -> None:
    lines = ["# Unpaired Frames", ""]
    lines.append("## Requests")
    if unpaired_requests:
        for request in unpaired_requests:
            lines.append(
                f"- {request.command or request.payload_text} "
                f"(seq={request.seq}, ch={request.channel}, ts={request.ts_ms}, len={request.length})"
            )
    else:
        lines.append("- none")
    lines.append("")
    lines.append("## Responses")
    if unpaired_responses:
        for response in unpaired_responses:
            lines.append(
                f"- seq={response.seq}, ch={response.channel}, ts={response.ts_ms}, len={response.length}, "
                f"payload={response.payload_text}"
            )
    else:
        lines.append("- none")
    lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8")


def load_lines_from_input(path: Path | None) -> list[str]:
    if path is None:
        return [line.rstrip("\n") for line in sys.stdin]
    return path.read_text(encoding="utf-8", errors="replace").splitlines()


def run_live_capture(config: str, device: str, duration: int | None) -> list[str]:
    command = ["esphome", "logs", config, "--device", device]
    process = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
    )
    if process.stdout is None:
        raise RuntimeError("failed to open esphome logs stdout")
    lines: list[str] = []
    start = dt.datetime.now(dt.timezone.utc)
    try:
        for line in process.stdout:
            lines.append(line.rstrip("\n"))
            if duration is not None:
                elapsed = (dt.datetime.now(dt.timezone.utc) - start).total_seconds()
                if elapsed >= duration:
                    process.terminate()
                    break
    finally:
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
    return lines


def normalize_command_labels() -> dict[str, dict[str, Any]]:
    def ascii_command_entry(command: str, definition: dict[str, Any]) -> dict[str, Any]:
        payload = command.encode("ascii")
        frame = build_frame(command)
        return {
            "request_text": command,
            "request_frame_style": "ascii_cr",
            "request_payload_hex": bytes_to_hex(payload),
            "request_full_hex": bytes_to_hex(frame),
            "crc_bytes": None,
            "crc_type": None,
            "terminator": "0D",
            **definition,
        }

    def crc_reference_entry(command: str) -> dict[str, Any]:
        payload = command.encode("ascii")
        frame = build_crc_frame(command)
        return {
            "request_text": command,
            "request_frame_style": "crc_cr",
            "request_payload_hex": bytes_to_hex(payload),
            "request_full_hex": bytes_to_hex(frame),
            "crc_bytes": bytes_to_hex(frame[-3:-1]),
            "crc_type": "PI30/XMODEM",
            "terminator": "0D",
        }

    def write_control_entry(command: str, definition: dict[str, Any]) -> dict[str, Any]:
        payload = command.encode("ascii")
        request_style = definition.get("request_frame_style", "crc_cr")
        if request_style == "ascii_cr":
            frame = payload + b"\r"
            crc_bytes = None
            crc_type = None
        else:
            frame = build_crc_frame(command)
            crc_bytes = bytes_to_hex(frame[-3:-1])
            crc_type = "PI30/XMODEM"
        return {
            "request_text": command,
            "request_frame_style": request_style,
            "request_payload_hex": bytes_to_hex(payload),
            "request_full_hex": bytes_to_hex(frame),
            "crc_bytes": crc_bytes,
            "crc_type": crc_type,
            "terminator": "0D",
            **definition,
        }

    def parameterized_write_entry(family: str, definition: dict[str, Any]) -> dict[str, Any]:
        request_template = definition["request_template"]
        request_example = definition["request_example"]
        example_frame = build_crc_frame(request_example)
        return {
            "request_text": family,
            "request_template": request_template,
            "request_example": request_example,
            "request_frame_style": definition.get("request_frame_style", "crc_cr"),
            "request_payload_hex": bytes_to_hex(request_example.encode("ascii")),
            "request_full_hex": bytes_to_hex(example_frame),
            "crc_bytes": bytes_to_hex(example_frame[-3:-1]),
            "crc_type": "PI30/XMODEM",
            "terminator": "0D",
            **definition,
        }

    labels: dict[str, dict[str, Any]] = {}
    for command, definition in COMMAND_DEFINITIONS.items():
        labels[command] = ascii_command_entry(command, definition)
    labels["reference_frames"] = {
        command: crc_reference_entry(command) for command in sorted(CRC_REFERENCE_COMMANDS)
    }
    labels["write_reference_frames"] = {
        command: write_control_entry(command, definition)
        for command, definition in sorted(WRITE_CONTROL_COMMANDS.items())
    }
    labels["parameterized_write_frames"] = {
        family: parameterized_write_entry(family, definition)
        for family, definition in sorted(PARAMETERIZED_WRITE_COMMANDS.items())
    }
    return labels


def main() -> int:
    parser = argparse.ArgumentParser(description="Decode Solar Plug H-command sniffer logs")
    parser.add_argument("--input", type=Path, help="sniffer log / markdown file containing SNIFF lines")
    parser.add_argument("--live", action="store_true", help="capture live logs with esphome logs")
    parser.add_argument("--esphome-config", help="ESPHome YAML file for live capture")
    parser.add_argument("--device", help="ESPhome device address for live capture")
    parser.add_argument("--duration", type=int, help="live capture duration in seconds")
    parser.add_argument("--outdir", type=Path, default=Path.cwd(), help="output directory")
    args = parser.parse_args()

    args.outdir.mkdir(parents=True, exist_ok=True)
    if args.live:
        if not args.esphome_config or not args.device:
            parser.error("--live requires --esphome-config and --device")
        lines = run_live_capture(args.esphome_config, args.device, args.duration)
    else:
        lines = load_lines_from_input(args.input)

    frames = parse_capture(lines)
    pairs, unpaired_requests, unpaired_responses = pair_frames(frames)

    write_frames_csv(args.outdir / "frames.csv", frames)
    write_frames_jsonl(args.outdir / "frames.jsonl", frames)
    command_counts, field_counts = write_command_jsonl(args.outdir / "command_responses.jsonl", pairs)
    write_unpaired_frames_md(args.outdir / "unpaired_frames.md", unpaired_requests, unpaired_responses)

    decoded_state = {
        "generated_at": dt.datetime.now(dt.timezone.utc).isoformat(),
        "source": {
            "mode": "live" if args.live else "file",
            "input": str(args.input) if args.input else None,
            "esphome_config": args.esphome_config if args.live else None,
            "device": args.device if args.live else None,
        },
        "frames_total": len(frames),
        "pairs_total": sum(1 for item in pairs if item["response"] is not None),
        "commands": normalize_command_labels(),
        "command_counts": dict(command_counts),
        "field_counts": dict(field_counts),
        "unpaired_requests": [
            {
                "command": request.command or request.payload_text,
                "seq": request.seq,
                "channel": request.channel,
                "ts_ms": request.ts_ms,
                "payload_text": request.payload_text,
            }
            for request in unpaired_requests
        ],
        "unpaired_responses": [serialize_frame(frame) for frame in unpaired_responses],
    }
    decoded_state["field_metadata"] = FIELD_METADATA
    (args.outdir / "decoded_state.json").write_text(
        json.dumps(decoded_state, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )

    report = build_report(
        frames=frames,
        pairs=pairs,
        unpaired_requests=unpaired_requests,
        unpaired_responses=unpaired_responses,
        command_counts=command_counts,
        field_counts=field_counts,
    )
    (args.outdir / "report.md").write_text(report, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
