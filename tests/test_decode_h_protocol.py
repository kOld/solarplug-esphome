from __future__ import annotations

import importlib.util
import json
import shutil
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "decode_h_protocol.py"
FIXTURE = ROOT / "tests" / "fixtures" / "dual_channel_sample.md"


def load_module():
    spec = importlib.util.spec_from_file_location("decode_h_protocol", SCRIPT)
    if spec is None or spec.loader is None:
        raise RuntimeError("failed to load decode_h_protocol module")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class DecodeHProtocolTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.module = load_module()

    def test_parse_fixture_and_metadata(self) -> None:
        lines = FIXTURE.read_text(encoding="utf-8").splitlines()
        frames = self.module.parse_capture(lines)
        pairs, unpaired_requests, unpaired_responses = self.module.pair_frames(frames)

        self.assertEqual(len(frames), 16)
        self.assertEqual(len(pairs), 8)
        self.assertEqual(len(unpaired_requests), 0)
        self.assertEqual(len(unpaired_responses), 0)

        first_pair = pairs[0]
        self.assertEqual(first_pair["command"], "HSTS")
        self.assertEqual(first_pair["request"].frame_style, "ascii_cr")
        self.assertEqual(first_pair["response"].frame_style, "ascii_cr")
        self.assertIn("status_bits", first_pair["decoded_fields"])
        self.assertEqual(first_pair["decoded_fields"]["mode_label"]["value"], "Mains Mode")
        self.assertEqual(first_pair["decoded_fields"]["status_code"]["unit"], None)
        self.assertEqual(first_pair["decoded_fields"]["status_code"]["confidence"], "high")

        battery_mode = self.module.decode_hsts("(00 B010000000000 10211002100B127000000")
        self.assertEqual(battery_mode["mode_code"], "B")
        self.assertEqual(battery_mode["mode_label"], "Battery Mode")

        hgen = self.module.decode_hgen("(260429 20:06 03.043 0059.4 0066.4 000000066.4 000000000000")
        hgen_meta = self.module.build_decoded_field_metadata("HGEN", hgen)
        self.assertEqual(hgen["date_iso"], "2026-04-29")
        self.assertEqual(hgen_meta["date_iso"]["raw"], "260429")
        self.assertEqual(hgen_meta["daily_power_gen_kwh"]["unit"], "kWh")
        self.assertEqual(hgen_meta["daily_power_gen_kwh"]["confidence"], "high")

        bms_enable_frame = self.module.parse_sniffer_line(
            'SNIFF ts=107 end=108 dur=1 ch=A idx=6 len=9 reason=cr hex="42 4D 53 43 30 30 48 9E 0D" ascii="BMSC00H.\\r"',
            6,
        )
        self.assertIsNotNone(bms_enable_frame)
        self.assertEqual(bms_enable_frame.frame_style, "crc_cr")
        self.assertEqual(bms_enable_frame.frame_kind, "request")
        self.assertEqual(bms_enable_frame.command, "BMSC00")

        bms_disable = self.module.decode_payload("BMSC00", "(ACK9 ")
        bms_disable_meta = self.module.build_decoded_field_metadata("bmsFunctionEnableSetting", bms_disable)
        self.assertEqual(bms_disable["portal_key"], "bmsFunctionEnableSetting")
        self.assertEqual(bms_disable["portal_value"], "Off")
        self.assertTrue(bms_disable["ack_observed"])
        self.assertEqual(bms_disable_meta["portal_label"]["confidence"], "high")

        bms_enable = self.module.decode_payload("BMSC01", "(ACK9 ")
        bms_enable_meta = self.module.build_decoded_field_metadata("bmsFunctionEnableSetting", bms_enable)
        self.assertEqual(bms_enable["portal_key"], "bmsFunctionEnableSetting")
        self.assertEqual(bms_enable["portal_value"], "On")
        self.assertTrue(bms_enable["ack_observed"])
        self.assertEqual(bms_enable_meta["portal_label"]["confidence"], "high")

        ack_frame = self.module.parse_sniffer_line(
            'SNIFF ts=108 end=109 dur=1 ch=B idx=7 len=7 reason=cr hex="28 41 43 4B 39 20 0D" ascii="(ACK9 \\r"',
            7,
        )
        self.assertIsNotNone(ack_frame)
        self.assertEqual(ack_frame.frame_kind, "response")
        self.assertEqual(ack_frame.frame_style, "crc_response_cr")
        self.assertEqual(ack_frame.payload_text, "(ACK")
        self.assertEqual(ack_frame.crc_wire, "39 20")
        self.assertEqual(ack_frame.crc_expected, "39 20")
        self.assertTrue(ack_frame.crc_ok)

        nak_frame = self.module.parse_sniffer_line(
            'SNIFF ts=109 end=110 dur=1 ch=B idx=8 len=7 reason=cr hex="28 4E 41 4B 73 73 0D" ascii="(NAKss\\r"',
            8,
        )
        self.assertIsNotNone(nak_frame)
        self.assertEqual(nak_frame.frame_kind, "response")
        self.assertEqual(nak_frame.frame_style, "crc_response_cr")
        self.assertEqual(nak_frame.payload_text, "(NAK")
        self.assertEqual(nak_frame.crc_wire, "73 73")
        self.assertEqual(nak_frame.crc_expected, "73 73")
        self.assertTrue(nak_frame.crc_ok)

        write_frame = self.module.parse_sniffer_line(
            'SNIFF ts=100 end=101 dur=1 ch=A idx=0 len=6 reason=cr hex="50 44 61 E3 41 0D" ascii="PDa.A\\r"',
            0,
        )
        self.assertIsNotNone(write_frame)
        self.assertEqual(write_frame.frame_style, "crc_cr")
        self.assertEqual(write_frame.frame_kind, "request")
        self.assertEqual(write_frame.command, "PDa")

        buzzer_disable = self.module.decode_payload("PDa", "(ACK9 ")
        buzzer_disable_meta = self.module.build_decoded_field_metadata("PDa", buzzer_disable)
        self.assertEqual(buzzer_disable["portal_key"], "buzzerOn")
        self.assertEqual(buzzer_disable["portal_value"], "Disable")
        self.assertTrue(buzzer_disable["ack_observed"])
        self.assertEqual(buzzer_disable_meta["portal_label"]["confidence"], "high")

        backlight_disable_frame = self.module.parse_sniffer_line(
            'SNIFF ts=102 end=103 dur=1 ch=A idx=2 len=6 reason=cr hex="50 44 78 60 59 0D" ascii="PDx`Y\\r"',
            2,
        )
        self.assertIsNotNone(backlight_disable_frame)
        self.assertEqual(backlight_disable_frame.frame_style, "ascii_cr")
        self.assertEqual(backlight_disable_frame.frame_kind, "request")
        self.assertEqual(backlight_disable_frame.command, "PDx`Y")

        backlight_disable = self.module.decode_payload("PDx`Y", "(ACK9 ")
        backlight_disable_meta = self.module.build_decoded_field_metadata("PDx`Y", backlight_disable)
        self.assertEqual(backlight_disable["portal_key"], "backlightOn")
        self.assertEqual(backlight_disable["portal_value"], "Disable")
        self.assertTrue(backlight_disable["ack_observed"])
        self.assertEqual(backlight_disable["heep1_token_6_effect"], 0)
        self.assertEqual(backlight_disable_meta["portal_label"]["confidence"], "high")

        prompt_disable_frame = self.module.parse_sniffer_line(
            'SNIFF ts=101 end=102 dur=1 ch=A idx=1 len=6 reason=cr hex="50 44 79 70 78 0D" ascii="PDypx\\r"',
            1,
        )
        self.assertIsNotNone(prompt_disable_frame)
        self.assertEqual(prompt_disable_frame.frame_style, "ascii_cr")
        self.assertEqual(prompt_disable_frame.frame_kind, "request")
        self.assertEqual(prompt_disable_frame.command, "PDypx")

        prompt_enable = self.module.decode_payload("PEyCI", "(ACK9 ")
        prompt_enable_meta = self.module.build_decoded_field_metadata("PEyCI", prompt_enable)
        self.assertEqual(prompt_enable["portal_key"], "inputSourceDetectionPromptSound")
        self.assertEqual(prompt_enable["portal_value"], "Enable")
        self.assertTrue(prompt_enable["ack_observed"])
        self.assertEqual(prompt_enable_meta["portal_label"]["confidence"], "high")

        backlight_enable = self.module.decode_payload("PExSh", "(ACK9 ")
        backlight_enable_meta = self.module.build_decoded_field_metadata("PExSh", backlight_enable)
        self.assertEqual(backlight_enable["portal_key"], "backlightOn")
        self.assertEqual(backlight_enable["portal_value"], "Enable")
        self.assertTrue(backlight_enable["ack_observed"])
        self.assertEqual(backlight_enable["heep1_token_6_effect"], 1)
        self.assertEqual(backlight_enable_meta["portal_label"]["confidence"], "high")

        display_disable_frame = self.module.parse_sniffer_line(
            'SNIFF ts=104 end=105 dur=1 ch=A idx=3 len=6 reason=cr hex="50 44 6B 42 0B 0D" ascii="PDkB\\x0b\\r"',
            3,
        )
        self.assertIsNotNone(display_disable_frame)
        self.assertEqual(display_disable_frame.frame_style, "crc_cr")
        self.assertEqual(display_disable_frame.frame_kind, "request")
        self.assertEqual(display_disable_frame.command, "PDk")

        display_disable = self.module.decode_payload("PDk", "(ACK9 ")
        display_disable_meta = self.module.build_decoded_field_metadata("PDk", display_disable)
        self.assertEqual(display_disable["portal_key"], "displayAutomaticallyReturnsToHomepage")
        self.assertEqual(display_disable["portal_value"], "Disable")
        self.assertTrue(display_disable["ack_observed"])
        self.assertEqual(display_disable["heep1_token_5_effect"], "012 -> 002")
        self.assertEqual(display_disable_meta["portal_label"]["confidence"], "high")

        display_enable = self.module.decode_payload("PEkq:", "(ACK9 ")
        display_enable_meta = self.module.build_decoded_field_metadata("PEkq:", display_enable)
        self.assertEqual(display_enable["portal_key"], "displayAutomaticallyReturnsToHomepage")
        self.assertEqual(display_enable["portal_value"], "Enable")
        self.assertTrue(display_enable["ack_observed"])
        self.assertEqual(display_enable["heep1_token_5_effect"], "002 -> 012")
        self.assertEqual(display_enable_meta["portal_label"]["confidence"], "high")

        overtemp_disable_frame = self.module.parse_sniffer_line(
            'SNIFF ts=105 end=106 dur=1 ch=A idx=4 len=6 reason=cr hex="50 44 76 81 97 0D" ascii="PDv..\\r"',
            4,
        )
        self.assertIsNotNone(overtemp_disable_frame)
        self.assertEqual(overtemp_disable_frame.frame_style, "crc_cr")
        self.assertEqual(overtemp_disable_frame.frame_kind, "request")
        self.assertEqual(overtemp_disable_frame.command, "PDv")

        overtemp_disable = self.module.decode_payload("PDv", "(ACK9 ")
        overtemp_disable_meta = self.module.build_decoded_field_metadata("PDv", overtemp_disable)
        self.assertEqual(overtemp_disable["portal_key"], "overTemperatureAutomaticRestart")
        self.assertEqual(overtemp_disable["portal_value"], "Disable")
        self.assertTrue(overtemp_disable["ack_observed"])
        self.assertNotIn("heep1_token_5_effect", overtemp_disable)
        self.assertEqual(overtemp_disable_meta["portal_label"]["confidence"], "high")

        overtemp_enable = self.module.decode_payload("PEv", "(ACK9 ")
        overtemp_enable_meta = self.module.build_decoded_field_metadata("PEv", overtemp_enable)
        self.assertEqual(overtemp_enable["portal_key"], "overTemperatureAutomaticRestart")
        self.assertEqual(overtemp_enable["portal_value"], "Enable")
        self.assertTrue(overtemp_enable["ack_observed"])
        self.assertNotIn("heep1_token_5_effect", overtemp_enable)
        self.assertEqual(overtemp_enable_meta["portal_label"]["confidence"], "high")

        overload_disable_frame = self.module.parse_sniffer_line(
            'SNIFF ts=106 end=107 dur=1 ch=A idx=5 len=6 reason=cr hex="50 44 75 B1 F4 0D" ascii="PDu..\\r"',
            5,
        )
        self.assertIsNotNone(overload_disable_frame)
        self.assertEqual(overload_disable_frame.frame_style, "crc_cr")
        self.assertEqual(overload_disable_frame.frame_kind, "request")
        self.assertEqual(overload_disable_frame.command, "PDu")

        overload_disable = self.module.decode_payload("PDu", "(ACK9 ")
        overload_disable_meta = self.module.build_decoded_field_metadata("PDu", overload_disable)
        self.assertEqual(overload_disable["portal_key"], "overloadAutomaticRestart")
        self.assertEqual(overload_disable["portal_value"], "Disable")
        self.assertTrue(overload_disable["ack_observed"])
        self.assertNotIn("heep1_token_5_effect", overload_disable)
        self.assertEqual(overload_disable_meta["portal_label"]["confidence"], "high")

        overload_enable = self.module.decode_payload("PEu", "(ACK9 ")
        overload_enable_meta = self.module.build_decoded_field_metadata("PEu", overload_enable)
        self.assertEqual(overload_enable["portal_key"], "overloadAutomaticRestart")
        self.assertEqual(overload_enable["portal_value"], "Enable")
        self.assertTrue(overload_enable["ack_observed"])
        self.assertNotIn("heep1_token_5_effect", overload_enable)
        self.assertEqual(overload_enable_meta["portal_label"]["confidence"], "high")

        clock_write_frame = self.module.parse_sniffer_line(
            'SNIFF ts=107 end=108 dur=1 ch=A idx=6 len=23 reason=cr hex="5E 53 3F 3F 3F 44 41 54 32 36 30 34 32 39 32 30 30 37 30 30 E8 57 0D" ascii="^S???DAT260429200700.W\\r"',
            6,
        )
        self.assertIsNotNone(clock_write_frame)
        self.assertEqual(clock_write_frame.frame_style, "crc_cr")
        self.assertEqual(clock_write_frame.frame_kind, "request")
        self.assertEqual(clock_write_frame.command, "^S???DAT260429200700")

        clock_ack_frame = self.module.parse_sniffer_line(
            'SNIFF ts=108 end=109 dur=1 ch=B idx=6 len=5 reason=cr hex="5E 31 0B C2 0D" ascii="^1\\x0b\\xc2\\r"',
            7,
        )
        self.assertIsNotNone(clock_ack_frame)
        self.assertEqual(clock_ack_frame.frame_style, "crc_cr")
        self.assertEqual(clock_ack_frame.frame_kind, "response")

        clock_write = self.module.decode_payload("^S???DAT260429200700", "^1")
        clock_meta = self.module.build_decoded_field_metadata("inverterSystemClock", clock_write)
        self.assertEqual(clock_write["command_family"], "inverterSystemClock")
        self.assertEqual(clock_write["portal_key"], "inverterSystemClock")
        self.assertEqual(clock_write["portal_value"], "2026-04-29 20:07:00")
        self.assertTrue(clock_write["ack_observed"])
        self.assertEqual(clock_meta["clock_iso"]["value"], "2026-04-29 20:07:00")
        self.assertEqual(clock_meta["portal_label"]["confidence"], "high")

        restore_capacity_write = self.module.decode_payload("PDSRS051", "(NAKss")
        restore_capacity_meta = self.module.build_decoded_field_metadata(
            "restoreSecondOutputBatCapacitySetting", restore_capacity_write
        )
        self.assertEqual(restore_capacity_write["command_family"], "restoreSecondOutputBatCapacitySetting")
        self.assertEqual(restore_capacity_write["portal_key"], "restoreSecondOutputBatCapacitySetting")
        self.assertEqual(restore_capacity_write["portal_value"], 51)
        self.assertFalse(restore_capacity_write["ack_observed"])
        self.assertEqual(restore_capacity_meta["requested_value"]["value"], 51)
        self.assertEqual(restore_capacity_meta["portal_label"]["confidence"], "medium")

        restore_capacity_ack = self.module.decode_payload("PDSRS050", "(ACK9 ")
        restore_capacity_ack_meta = self.module.build_decoded_field_metadata(
            "restoreSecondOutputBatCapacitySetting", restore_capacity_ack
        )
        self.assertEqual(restore_capacity_ack["command_family"], "restoreSecondOutputBatCapacitySetting")
        self.assertEqual(restore_capacity_ack["portal_value"], 50)
        self.assertTrue(restore_capacity_ack["ack_observed"])
        self.assertEqual(restore_capacity_ack_meta["requested_value"]["raw"], "050")

        cutoff_write_frame = self.module.parse_sniffer_line(
            'SNIFF ts=109 end=110 dur=1 ch=A idx=7 len=11 reason=cr hex="50 53 44 56 34 31 2E 30 4A 40 0D" ascii="PSDV41.0J@\\r"',
            8,
        )
        self.assertIsNotNone(cutoff_write_frame)
        self.assertEqual(cutoff_write_frame.frame_style, "crc_cr")
        self.assertEqual(cutoff_write_frame.frame_kind, "request")
        self.assertEqual(cutoff_write_frame.command, "PSDV41.0")

        cutoff_write = self.module.decode_payload("PSDV41.0", "(NAKss")
        cutoff_write_meta = self.module.build_decoded_field_metadata("batteryCutOffVoltageSetting", cutoff_write)
        self.assertEqual(cutoff_write["command_family"], "batteryCutOffVoltageSetting")
        self.assertEqual(cutoff_write["portal_key"], "batteryCutOffVoltageSetting")
        self.assertEqual(cutoff_write["portal_label"], "Battery Cut Off Voltage Setting")
        self.assertEqual(cutoff_write["portal_value"], 41.0)
        self.assertFalse(cutoff_write["ack_observed"])
        self.assertEqual(cutoff_write["ack_response"], "(NAKss")
        self.assertEqual(cutoff_write_meta["requested_value"]["unit"], "V")
        self.assertEqual(cutoff_write_meta["requested_value"]["confidence"], "high")

        constant_voltage_write = self.module.decode_payload("PCVV56.4", "(ACK9 ")
        constant_voltage_meta = self.module.build_decoded_field_metadata(
            "batteryConstantChargingVoltageSetting", constant_voltage_write
        )
        self.assertEqual(constant_voltage_write["command_family"], "batteryConstantChargingVoltageSetting")
        self.assertEqual(constant_voltage_write["portal_key"], "batteryConstantChargingVoltageSetting")
        self.assertEqual(constant_voltage_write["portal_value"], 56.4)
        self.assertTrue(constant_voltage_write["ack_observed"])
        self.assertEqual(constant_voltage_meta["requested_value"]["unit"], "V")
        self.assertEqual(constant_voltage_meta["portal_label"]["confidence"], "medium")

        recharge_voltage_write = self.module.decode_payload("PBCV45.0", "(ACK9 ")
        recharge_voltage_meta = self.module.build_decoded_field_metadata(
            "batteryRechargeVoltageSetting", recharge_voltage_write
        )
        self.assertEqual(recharge_voltage_write["command_family"], "batteryRechargeVoltageSetting")
        self.assertEqual(recharge_voltage_write["portal_key"], "batteryRechargeVoltageSetting")
        self.assertEqual(recharge_voltage_write["portal_value"], 45.0)
        self.assertTrue(recharge_voltage_write["ack_observed"])
        self.assertEqual(recharge_voltage_meta["requested_value"]["value"], 45.0)
        self.assertEqual(recharge_voltage_meta["portal_label"]["confidence"], "medium-high")

        redischarge_voltage_write = self.module.decode_payload("PBDV53.0", "(ACK9 ")
        redischarge_voltage_meta = self.module.build_decoded_field_metadata(
            "batteryRedischargeVoltageSetting", redischarge_voltage_write
        )
        self.assertEqual(redischarge_voltage_write["command_family"], "batteryRedischargeVoltageSetting")
        self.assertEqual(redischarge_voltage_write["portal_key"], "batteryRedischargeVoltageSetting")
        self.assertEqual(redischarge_voltage_write["portal_value"], 53.0)
        self.assertTrue(redischarge_voltage_write["ack_observed"])
        self.assertEqual(redischarge_voltage_meta["requested_value"]["raw"], "53.0")
        self.assertEqual(redischarge_voltage_meta["portal_label"]["confidence"], "medium-high")

        restore_delay_write = self.module.decode_payload("PDDLYT006", "(NAKss")
        restore_delay_meta = self.module.build_decoded_field_metadata(
            "restoreSecondOutputDelayTimeSetting", restore_delay_write
        )
        self.assertEqual(restore_delay_write["command_family"], "restoreSecondOutputDelayTimeSetting")
        self.assertEqual(restore_delay_write["portal_key"], "restoreSecondOutputDelayTimeSetting")
        self.assertEqual(restore_delay_write["portal_value"], 6)
        self.assertFalse(restore_delay_write["ack_observed"])
        self.assertEqual(restore_delay_meta["requested_value"]["value"], 6)

        restore_delay_ack = self.module.decode_payload("PDDLYT005", "(ACK9 ")
        restore_delay_ack_meta = self.module.build_decoded_field_metadata(
            "restoreSecondOutputDelayTimeSetting", restore_delay_ack
        )
        self.assertEqual(restore_delay_ack["command_family"], "restoreSecondOutputDelayTimeSetting")
        self.assertEqual(restore_delay_ack["portal_value"], 5)
        self.assertTrue(restore_delay_ack["ack_observed"])
        self.assertEqual(restore_delay_ack_meta["requested_value"]["raw"], "005")

        charger_cso = self.module.decode_payload("PCP00", "(ACK9 ")
        charger_cso_meta = self.module.build_decoded_field_metadata("chargerPrioritySetting", charger_cso)
        self.assertEqual(charger_cso["command_family"], "chargerPrioritySetting")
        self.assertEqual(charger_cso["portal_value"], "CSO")
        self.assertTrue(charger_cso["ack_observed"])
        self.assertEqual(charger_cso_meta["portal_value"]["confidence"], "medium-high")

        charger_snu = self.module.decode_payload("PCP01", "(ACK9 ")
        self.assertEqual(charger_snu["portal_value"], "SNU")

        charger_oso = self.module.decode_payload("PCP02", "(ACK9 ")
        self.assertEqual(charger_oso["portal_value"], "OSO")

        output_sub = self.module.decode_payload("POP00", "(ACK9 ")
        output_sub_meta = self.module.build_decoded_field_metadata("outputSourcePrioritySetting", output_sub)
        self.assertEqual(output_sub["command_family"], "outputSourcePrioritySetting")
        self.assertEqual(output_sub["portal_value"], "SUB priority")
        self.assertTrue(output_sub["ack_observed"])
        self.assertEqual(output_sub_meta["portal_label"]["confidence"], "high")

        output_sbu = self.module.decode_payload("POP01", "(ACK9 ")
        self.assertEqual(output_sbu["portal_value"], "SBU priority")

        output_utility = self.module.decode_payload("POP02", "(ACK9 ")
        self.assertEqual(output_utility["portal_value"], "Utility first (legacy)")

        pv_feed_blu = self.module.decode_payload("PVENGUSE00", "(ACK9 ")
        pv_feed_blu_meta = self.module.build_decoded_field_metadata("pvEnergyFeedingPrioritySetting", pv_feed_blu)
        self.assertEqual(pv_feed_blu["command_family"], "pvEnergyFeedingPrioritySetting")
        self.assertEqual(pv_feed_blu["portal_value"], "BLU")
        self.assertTrue(pv_feed_blu["ack_observed"])
        self.assertEqual(pv_feed_blu_meta["portal_value"]["confidence"], "high")

        pv_feed_lbu = self.module.decode_payload("PVENGUSE01", "(ACK9 ")
        self.assertEqual(pv_feed_lbu["portal_value"], "LBU")

        equalization_interval_write_frame = self.module.parse_sniffer_line(
            'SNIFF ts=108 end=109 dur=1 ch=A idx=6 len=11 reason=cr hex="50 42 45 51 50 30 33 30 36 12 0D" ascii="PBEQP0306.\\r"',
            7,
        )
        self.assertIsNotNone(equalization_interval_write_frame)
        self.assertEqual(equalization_interval_write_frame.frame_style, "crc_cr")
        self.assertEqual(equalization_interval_write_frame.frame_kind, "request")
        self.assertEqual(equalization_interval_write_frame.command, "PBEQP030")

        equalization_interval_write = self.module.decode_payload("PBEQP030", "(ACK9 ")
        equalization_interval_meta = self.module.build_decoded_field_metadata(
            "batteryEqualizationIntervalSetting", equalization_interval_write
        )
        self.assertEqual(equalization_interval_write["command_family"], "batteryEqualizationIntervalSetting")
        self.assertEqual(equalization_interval_write["portal_key"], "batteryEqualizationIntervalSetting")
        self.assertEqual(equalization_interval_write["portal_value"], 30)
        self.assertTrue(equalization_interval_write["ack_observed"])
        self.assertEqual(equalization_interval_meta["requested_value"]["value"], 30)
        self.assertEqual(equalization_interval_meta["portal_label"]["confidence"], "high")

        equalization_timeout_write_frame = self.module.parse_sniffer_line(
            'SNIFF ts=109 end=110 dur=1 ch=A idx=7 len=12 reason=cr hex="50 42 45 51 4F 54 31 32 30 88 44 0D" ascii="PBEQOT120.D\\r"',
            8,
        )
        self.assertIsNotNone(equalization_timeout_write_frame)
        self.assertEqual(equalization_timeout_write_frame.frame_style, "crc_cr")
        self.assertEqual(equalization_timeout_write_frame.frame_kind, "request")
        self.assertEqual(equalization_timeout_write_frame.command, "PBEQOT120")

        equalization_timeout_write = self.module.decode_payload("PBEQOT120", "(ACK9 ")
        equalization_timeout_meta = self.module.build_decoded_field_metadata(
            "batteryEqualizationTimeoutSetting", equalization_timeout_write
        )
        self.assertEqual(equalization_timeout_write["command_family"], "batteryEqualizationTimeoutSetting")
        self.assertEqual(equalization_timeout_write["portal_key"], "batteryEqualizationTimeoutSetting")
        self.assertEqual(equalization_timeout_write["portal_value"], 120)
        self.assertTrue(equalization_timeout_write["ack_observed"])
        self.assertEqual(equalization_timeout_meta["requested_value"]["value"], 120)
        self.assertEqual(equalization_timeout_meta["portal_label"]["confidence"], "high")

        equalization_time_write_frame = self.module.parse_sniffer_line(
            'SNIFF ts=110 end=111 dur=1 ch=A idx=8 len=11 reason=cr hex="50 42 45 51 54 30 35 39 C7 6C 0D" ascii="PBEQT059..\\r"',
            9,
        )
        self.assertIsNotNone(equalization_time_write_frame)
        self.assertEqual(equalization_time_write_frame.frame_style, "crc_cr")
        self.assertEqual(equalization_time_write_frame.frame_kind, "request")
        self.assertEqual(equalization_time_write_frame.command, "PBEQT059")

        equalization_time_write = self.module.decode_payload("PBEQT059", "(ACK9 ")
        equalization_time_meta = self.module.build_decoded_field_metadata(
            "batteryEqualizationTimeSetting", equalization_time_write
        )
        self.assertEqual(equalization_time_write["command_family"], "batteryEqualizationTimeSetting")
        self.assertEqual(equalization_time_write["portal_key"], "batteryEqualizationTimeSetting")
        self.assertEqual(equalization_time_write["portal_value"], 59)
        self.assertTrue(equalization_time_write["ack_observed"])
        self.assertEqual(equalization_time_meta["requested_value"]["value"], 59)
        self.assertEqual(equalization_time_meta["portal_label"]["confidence"], "high")

        equalization_voltage_write_frame = self.module.parse_sniffer_line(
            'SNIFF ts=109 end=110 dur=1 ch=A idx=7 len=13 reason=cr hex="50 42 45 51 56 35 38 2E 33 30 13 1E 0D" ascii="PBEQV58.30..\\r"',
            8,
        )
        self.assertIsNotNone(equalization_voltage_write_frame)
        self.assertEqual(equalization_voltage_write_frame.frame_style, "crc_cr")
        self.assertEqual(equalization_voltage_write_frame.frame_kind, "request")
        self.assertEqual(equalization_voltage_write_frame.command, "PBEQV58.30")

        equalization_voltage_write = self.module.decode_payload("PBEQV58.30", "(ACK9 ")
        equalization_voltage_meta = self.module.build_decoded_field_metadata(
            "batteryEqualizationVoltageSetting", equalization_voltage_write
        )
        self.assertEqual(equalization_voltage_write["command_family"], "batteryEqualizationVoltageSetting")
        self.assertEqual(equalization_voltage_write["portal_key"], "batteryEqualizationVoltageSetting")
        self.assertEqual(equalization_voltage_write["portal_value"], 58.3)
        self.assertTrue(equalization_voltage_write["ack_observed"])
        self.assertEqual(equalization_voltage_meta["requested_value"]["value"], 58.3)
        self.assertEqual(equalization_voltage_meta["portal_label"]["confidence"], "high")

        equalization_mode_enable_frame = self.module.parse_sniffer_line(
            'SNIFF ts=111 end=112 dur=1 ch=A idx=9 len=9 reason=cr hex="50 42 45 51 45 31 4A 13 0D" ascii="PBEQE1J.\\r"',
            10,
        )
        self.assertIsNotNone(equalization_mode_enable_frame)
        self.assertEqual(equalization_mode_enable_frame.frame_style, "crc_cr")
        self.assertEqual(equalization_mode_enable_frame.frame_kind, "request")
        self.assertEqual(equalization_mode_enable_frame.command, "PBEQE1")

        equalization_mode_enable = self.module.decode_payload("PBEQE1", "(ACK9 ")
        self.assertEqual(equalization_mode_enable["command_family"], "batteryEqualizationModeEnableSetting")
        self.assertEqual(equalization_mode_enable["portal_key"], "batteryEqualizationModeEnableSetting")
        self.assertEqual(equalization_mode_enable["portal_label"], "Battery Equalization Mode Enable Setting")
        self.assertEqual(equalization_mode_enable["portal_value"], "On")
        self.assertTrue(equalization_mode_enable["ack_observed"])

        equalization_mode_disable_frame = self.module.parse_sniffer_line(
            'SNIFF ts=112 end=113 dur=1 ch=A idx=10 len=9 reason=cr hex="50 42 45 51 45 30 5A 32 0D" ascii="PBEQE0Z2\\r"',
            11,
        )
        self.assertIsNotNone(equalization_mode_disable_frame)
        self.assertEqual(equalization_mode_disable_frame.frame_style, "ascii_cr")
        self.assertEqual(equalization_mode_disable_frame.frame_kind, "request")
        self.assertEqual(equalization_mode_disable_frame.command, "PBEQE0Z2")

        equalization_mode_disable = self.module.decode_payload("PBEQE0Z2", "(ACK9 ")
        self.assertEqual(equalization_mode_disable["command_family"], "batteryEqualizationModeEnableSetting")
        self.assertEqual(equalization_mode_disable["portal_key"], "batteryEqualizationModeEnableSetting")
        self.assertEqual(equalization_mode_disable["portal_label"], "Battery Equalization Mode Enable Setting")
        self.assertEqual(equalization_mode_disable["portal_value"], "Off")
        self.assertTrue(equalization_mode_disable["ack_observed"])

        hbat = self.module.decode_hbat("(04 053.5 080 002 00000 394 101002010000 00000000")
        self.assertEqual(hbat["battery_series_count"], 4)
        self.assertNotIn("battery_type", hbat)

        hpvb = self.module.decode_hpvb("(000.0 00.0 00000 0 380.0 000000000000000000000")
        hpvb_meta = self.module.build_decoded_field_metadata("HPVB", hpvb)
        self.assertEqual(hpvb["pv_voltage_v"], 0.0)
        self.assertEqual(hpvb["pv_charging_mark"], "Close")
        self.assertEqual(hpvb["bus_voltage_v"], 380.0)
        self.assertEqual(hpvb_meta["raw_tail"]["raw"], ["000000000000000000000"])
        self.assertEqual(hpvb_meta["bus_voltage_v"]["confidence"], "medium")

        hbms3 = self.module.decode_hbms3("(0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 00000000")
        hbms3_meta = self.module.build_decoded_field_metadata("HBMS3", hbms3)
        self.assertEqual(len(hbms3["cell_voltage_list"]), 16)
        self.assertEqual(hbms3_meta["cell_voltage_list"]["confidence"], "low")
        self.assertEqual(hbms3_meta["raw_tail"]["raw"], ["00000000"])

        hbms1 = self.module.decode_hbms1("(02 1001100000000000 044.8 057.6 150.0 053 0015.0 0000.0 02946 000000")
        hbms1_meta = self.module.build_decoded_field_metadata("HBMS1", hbms1)
        self.assertEqual(hbms1["bms_soc_percent"], 53)
        self.assertEqual(hbms1_meta["bms_soc_percent"]["confidence"], "medium-high")
        self.assertEqual(hbms1_meta["bms_charging_current_a"]["confidence"], "medium-high")

        hbms2 = self.module.decode_hbms2("(0000.0 0000.0 1 3358 0015 3356 0016 0000000000000000000000")
        hbms2_meta = self.module.build_decoded_field_metadata("HBMS2", hbms2)
        self.assertEqual(hbms2["max_voltage"], 3358)
        self.assertEqual(hbms2_meta["remaining_capacity"]["confidence"], "low")
        self.assertEqual(hbms2_meta["max_voltage"]["confidence"], "medium")

        himsg1 = self.module.decode_himsg1("(0040.05 20250923 12")
        self.assertEqual(himsg1["software_date_iso"], "2025-09-23")

    def test_parameterized_crc_writes_classify_as_crc_requests(self) -> None:
        commands = [
            "PBCV45.0",
            "PBDV53.0",
            "PBT04",
            "PCVV56.4",
            "BMSSDC080",
            "PGFC060",
            "MUCHGC040",
            "MNCHGC080",
        ]
        for index, command in enumerate(commands):
            with self.subTest(command=command):
                raw = self.module.build_crc_frame(command)
                hex_text = self.module.bytes_to_hex(raw)
                frame = self.module.parse_sniffer_line(
                    f'SNIFF ts=1 end=2 dur=1 ch=A idx={index} len={len(raw)} reason=cr hex="{hex_text}" ascii=""',
                    index,
                )
                self.assertIsNotNone(frame)
                self.assertEqual(frame.frame_style, "crc_cr")
                self.assertEqual(frame.frame_kind, "request")
                self.assertEqual(frame.command, command)

    def test_read_commands_classify_as_ascii_requests(self) -> None:
        commands = [
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
            "HEEP1",
            "HEEP2",
            "HPVB",
        ]
        for index, command in enumerate(commands):
            with self.subTest(command=command):
                raw = command.encode("ascii") + b"\r"
                frame = self.module.parse_sniffer_line(
                    f'SNIFF ts=1 end=2 dur=1 ch=A idx={index} len={len(raw)} reason=cr '
                    f'hex="{self.module.bytes_to_hex(raw)}" ascii=""',
                    index,
                )
                self.assertIsNotNone(frame)
                self.assertEqual(frame.frame_style, "ascii_cr")
                self.assertEqual(frame.frame_kind, "request")
                self.assertEqual(frame.command, command)

    def test_golden_crc_write_frames(self) -> None:
        expected_hex = {
            "PCP00": "50 43 50 30 30 8D 7A 0D",
            "PBEQV58.30": "50 42 45 51 56 35 38 2E 33 30 13 1E 0D",
            "PBCV45.0": "50 42 43 56 34 35 2E 30 D1 DB 0D",
            "PBDV53.0": "50 42 44 56 35 33 2E 30 DD 8E 0D",
            "PBT04": "50 42 54 30 34 67 8A 0D",
            "BMSC00": "42 4D 53 43 30 30 48 9E 0D",
            "BMSSDC020": "42 4D 53 53 44 43 30 32 30 48 44 0D",
            "PGFC020": "50 47 46 43 30 32 30 F1 6A 0D",
            "MUCHGC030": "4D 55 43 48 47 43 30 33 30 C0 C0 0D",
            "MNCHGC050": "4D 4E 43 48 47 43 30 35 30 81 7D 0D",
        }
        for command, hex_text in expected_hex.items():
            with self.subTest(command=command):
                self.assertEqual(self.module.bytes_to_hex(self.module.build_crc_frame(command)), hex_text)

    def test_cli_emits_expected_outputs(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            outdir = Path(tmpdir)
            subprocess.run(
                [sys.executable, str(SCRIPT), "--input", str(FIXTURE), "--outdir", str(outdir)],
                check=True,
                cwd=str(ROOT),
            )

            for name in [
                "frames.csv",
                "frames.jsonl",
                "command_responses.jsonl",
                "decoded_state.json",
                "report.md",
                "unpaired_frames.md",
            ]:
                self.assertTrue((outdir / name).exists(), name)

            decoded_state = json.loads((outdir / "decoded_state.json").read_text(encoding="utf-8"))
            self.assertEqual(decoded_state["frames_total"], 16)
            self.assertEqual(decoded_state["pairs_total"], 8)
            self.assertEqual(decoded_state["unpaired_requests"], [])
            self.assertEqual(decoded_state["unpaired_responses"], [])
            self.assertIn("write_reference_frames", decoded_state["commands"])
            self.assertEqual(decoded_state["commands"]["write_reference_frames"]["PDa"]["portal_label"], "Buzzer On")
            self.assertEqual(decoded_state["commands"]["write_reference_frames"]["PEa"]["portal_value"], "Enable")
            self.assertEqual(decoded_state["commands"]["write_reference_frames"]["PDx`Y"]["portal_key"], "backlightOn")
            self.assertEqual(decoded_state["commands"]["write_reference_frames"]["PExSh"]["heep1_token_6_effect"], 1)
            self.assertEqual(
                decoded_state["commands"]["write_reference_frames"]["PDk"]["portal_key"],
                "displayAutomaticallyReturnsToHomepage",
            )
            self.assertEqual(decoded_state["commands"]["write_reference_frames"]["PDk"]["heep1_token_5_effect"], "012 -> 002")
            self.assertEqual(decoded_state["commands"]["write_reference_frames"]["PEkq:"]["portal_value"], "Enable")
            self.assertEqual(decoded_state["commands"]["write_reference_frames"]["PDv"]["portal_key"], "overTemperatureAutomaticRestart")
            self.assertEqual(decoded_state["commands"]["write_reference_frames"]["PEv"]["portal_value"], "Enable")
            self.assertEqual(decoded_state["commands"]["write_reference_frames"]["PDu"]["portal_key"], "overloadAutomaticRestart")
            self.assertEqual(decoded_state["commands"]["write_reference_frames"]["PEu"]["portal_value"], "Enable")
            self.assertEqual(decoded_state["commands"]["write_reference_frames"]["PDypx"]["portal_key"], "inputSourceDetectionPromptSound")
            self.assertEqual(decoded_state["commands"]["write_reference_frames"]["PEyCI"]["request_frame_style"], "ascii_cr")
            self.assertIn("parameterized_write_frames", decoded_state["commands"])
            self.assertEqual(
                decoded_state["commands"]["parameterized_write_frames"]["inverterSystemClock"]["portal_label"],
                "Inverter System Clock",
            )
            self.assertEqual(
                decoded_state["commands"]["parameterized_write_frames"]["inverterSystemClock"]["request_template"],
                "^S???DAT<YYMMDDHHMMSS>",
            )
            self.assertEqual(
                decoded_state["commands"]["parameterized_write_frames"]["restoreSecondOutputBatCapacitySetting"]["portal_key"],
                "restoreSecondOutputBatCapacitySetting",
            )
            self.assertEqual(
                decoded_state["commands"]["parameterized_write_frames"]["restoreSecondOutputBatCapacitySetting"]["request_template"],
                "PDSRS<DDD>",
            )
            self.assertEqual(
                decoded_state["commands"]["parameterized_write_frames"]["restoreSecondOutputDelayTimeSetting"]["portal_key"],
                "restoreSecondOutputDelayTimeSetting",
            )
            self.assertEqual(
                decoded_state["commands"]["parameterized_write_frames"]["restoreSecondOutputDelayTimeSetting"]["request_template"],
                "PDDLYT<DDD>",
            )
            self.assertEqual(
                decoded_state["commands"]["parameterized_write_frames"]["batteryEqualizationVoltageSetting"]["portal_key"],
                "batteryEqualizationVoltageSetting",
            )
            self.assertEqual(
                decoded_state["commands"]["parameterized_write_frames"]["batteryEqualizationVoltageSetting"]["request_template"],
                "PBEQV<XX.XX>",
            )
            self.assertEqual(
                decoded_state["commands"]["parameterized_write_frames"]["batteryConstantChargingVoltageSetting"][
                    "portal_key"
                ],
                "batteryConstantChargingVoltageSetting",
            )
            self.assertEqual(
                decoded_state["commands"]["parameterized_write_frames"]["batteryConstantChargingVoltageSetting"][
                    "request_template"
                ],
                "PCVV<XX.X>",
            )
            self.assertEqual(
                decoded_state["commands"]["parameterized_write_frames"]["batteryRechargeVoltageSetting"][
                    "portal_key"
                ],
                "batteryRechargeVoltageSetting",
            )
            self.assertEqual(
                decoded_state["commands"]["parameterized_write_frames"]["batteryRechargeVoltageSetting"][
                    "request_template"
                ],
                "PBCV<XX.X>",
            )
            self.assertEqual(
                decoded_state["commands"]["parameterized_write_frames"]["batteryRedischargeVoltageSetting"][
                    "portal_key"
                ],
                "batteryRedischargeVoltageSetting",
            )
            self.assertEqual(
                decoded_state["commands"]["parameterized_write_frames"]["batteryRedischargeVoltageSetting"][
                    "request_template"
                ],
                "PBDV<XX.X>",
            )
            self.assertEqual(
                decoded_state["commands"]["parameterized_write_frames"]["batteryTypeSetting"]["portal_key"],
                "batteryTypeSetting",
            )
            self.assertEqual(
                decoded_state["commands"]["parameterized_write_frames"]["batteryTypeSetting"]["request_template"],
                "PBT<NN>",
            )
            self.assertEqual(
                decoded_state["commands"]["parameterized_write_frames"]["bmsLockMachineBatteryCapacitySetting"][
                    "portal_key"
                ],
                "bmsLockMachineBatteryCapacitySetting",
            )
            self.assertEqual(
                decoded_state["commands"]["parameterized_write_frames"]["bmsLockMachineBatteryCapacitySetting"][
                    "request_template"
                ],
                "BMSSDC<DDD>",
            )
            self.assertEqual(
                decoded_state["commands"]["write_reference_frames"]["PBEQE1"]["command_family"],
                "batteryEqualizationModeEnableSetting",
            )
            self.assertEqual(
                decoded_state["commands"]["write_reference_frames"]["PBEQE1"]["portal_value"],
                "On",
            )
            self.assertEqual(
                decoded_state["commands"]["write_reference_frames"]["PBEQE1"]["request_frame_style"],
                "crc_cr",
            )
            self.assertEqual(
                decoded_state["commands"]["write_reference_frames"]["PBEQE0Z2"]["portal_value"],
                "Off",
            )
            self.assertEqual(
                decoded_state["commands"]["write_reference_frames"]["PBEQE0Z2"]["request_frame_style"],
                "ascii_cr",
            )
            self.assertEqual(decoded_state["commands"]["write_reference_frames"]["PCP00"]["portal_value"], "CSO")
            self.assertEqual(decoded_state["commands"]["write_reference_frames"]["PCP01"]["portal_value"], "SNU")
            self.assertEqual(decoded_state["commands"]["write_reference_frames"]["PCP02"]["portal_value"], "OSO")
            self.assertEqual(
                decoded_state["commands"]["write_reference_frames"]["POP00"]["portal_value"],
                "SUB priority",
            )
            self.assertEqual(
                decoded_state["commands"]["write_reference_frames"]["POP01"]["portal_value"],
                "SBU priority",
            )
            self.assertEqual(
                decoded_state["commands"]["write_reference_frames"]["POP02"]["portal_value"],
                "Utility first (legacy)",
            )
            self.assertEqual(decoded_state["commands"]["write_reference_frames"]["PVENGUSE00"]["portal_value"], "BLU")
            self.assertEqual(decoded_state["commands"]["write_reference_frames"]["PVENGUSE01"]["portal_value"], "LBU")
            self.assertEqual(
                decoded_state["commands"]["parameterized_write_frames"]["batteryEqualizationIntervalSetting"]["portal_key"],
                "batteryEqualizationIntervalSetting",
            )
            self.assertEqual(
                decoded_state["commands"]["parameterized_write_frames"]["batteryEqualizationIntervalSetting"]["request_template"],
                "PBEQP<DDD>",
            )
            self.assertEqual(
                decoded_state["commands"]["parameterized_write_frames"]["batteryEqualizationTimeoutSetting"]["portal_key"],
                "batteryEqualizationTimeoutSetting",
            )
            self.assertEqual(
                decoded_state["commands"]["parameterized_write_frames"]["batteryEqualizationTimeoutSetting"]["request_template"],
                "PBEQOT<DDD>",
            )
            self.assertEqual(
                decoded_state["commands"]["parameterized_write_frames"]["batteryCutOffVoltageSetting"]["portal_key"],
                "batteryCutOffVoltageSetting",
            )
            self.assertEqual(
                decoded_state["commands"]["parameterized_write_frames"]["batteryCutOffVoltageSetting"]["request_template"],
                "PSDV<XX.X>",
            )
            self.assertEqual(
                decoded_state["commands"]["parameterized_write_frames"]["batteryEqualizationTimeSetting"]["portal_key"],
                "batteryEqualizationTimeSetting",
            )
            self.assertEqual(
                decoded_state["commands"]["parameterized_write_frames"]["batteryEqualizationTimeSetting"]["request_template"],
                "PBEQT<DDD>",
            )

            frames_jsonl = (outdir / "frames.jsonl").read_text(encoding="utf-8").splitlines()
            self.assertEqual(len(frames_jsonl), 16)
            first_frame = json.loads(frames_jsonl[0])
            self.assertEqual(first_frame["frame_style"], "ascii_cr")

            command_responses = (outdir / "command_responses.jsonl").read_text(encoding="utf-8").splitlines()
            self.assertEqual(len(command_responses), 8)
            first_pair = json.loads(command_responses[0])
            self.assertIn("decoded_fields", first_pair)
            self.assertIn("status_code", first_pair["decoded_fields"])

    def test_heep1_settings_readback_matches_portal_and_write_deltas(self) -> None:
        tail = " 1 020 025 090 050 056.4 056.4 042.0 020 0 0"
        # 2026-04-29 baseline; portal: PV LBU, overload restart off, prompt sound on, over-temp restart on,
        # display return on, charger OSO, buzzer on, backlight on.
        baseline = self.module.decode_heep1_settings("(1 060 030 03410110230 012 1 1 0 0" + tail)
        self.assertEqual(
            baseline,
            {
                "pv_energy_feeding_priority": "LBU",
                "overload_restart": False,
                "input_source_detection_prompt_sound": True,
                "over_temperature_restart": True,
                "display_return_homepage": True,
                "charger_priority": "OSO",
                "buzzer": True,
                "backlight": True,
                "overload_bypass": False,
                "bms_function_enable": True,
                "output_source_priority": "SBU priority",
                "ac_input_voltage_range": "Appliance",
                "battery_type": "PYL",
                # Unsafe-profile controls seeded from the same frame.
                "grid_protocol": "Mode 4",
                "ct_function": False,
                "output_voltage": "230 V",
                "output_frequency": "50 Hz",
                "output_mode": "SIG",
                "grid_feed": False,
            },
        )
        # One-setting deltas captured on the bus (command -> HEEP1 change).
        cases = {
            "PDa": ("(1 060 030 03410110230 012 0 1 0 0" + tail, "buzzer", False),
            "PDx`Y": ("(1 060 030 03410110230 012 1 0 0 0" + tail, "backlight", False),
            "PDk": ("(1 060 030 03410110230 002 1 1 0 0" + tail, "display_return_homepage", False),
            "PDv": ("(1 060 030 03410100230 012 1 1 0 0" + tail, "over_temperature_restart", False),
            "PDypx": ("(1 060 030 03410010230 012 1 1 0 0" + tail, "input_source_detection_prompt_sound", False),
            "PVENGUSE00": ("(1 060 030 03400010230 012 1 1 0 0" + tail, "pv_energy_feeding_priority", "BLU"),
            "PCP00": ("(1 080 030 03311010230 010 1 1 0 0" + tail, "charger_priority", "CSO"),
            "PCP01": ("(1 060 030 03410110230 011 1 1 0 0" + tail, "charger_priority", "SNU"),
            "PEu": ("(1 080 030 03311010230 012 1 1 0 0" + tail, "overload_restart", True),
        }
        for command, (payload, key, expected) in cases.items():
            with self.subTest(command=command):
                self.assertEqual(self.module.decode_heep1_settings(payload)[key], expected)
        self.assertEqual(self.module.decode_heep1_settings("(1 060 030 0341x110230 0x2"), {})

    def test_heep1_live_2026_11_16_battery_type_and_cross_checks(self) -> None:
        live = "(1 080 030 03311010230 012 1 1 0 0 1 020 025 040 050 056.4 056.4 042.0 020 0 0"
        settings = self.module.decode_heep1_settings(live)
        # Digit 2 = 3 = LIA in the shared enum; QPIRI[12] read 3 in the same poll.
        self.assertEqual(settings["battery_type"], "LIA")
        # Cross-checks against independent readbacks in the same poll:
        # QPIRI[16]=1 (SBU), QPIRI[15]=0, QFLAG "EakuvxDbjyz" (b off, u/v on, y off).
        self.assertEqual(settings["output_source_priority"], "SBU priority")
        self.assertEqual(settings["ac_input_voltage_range"], "Appliance")
        self.assertFalse(settings["overload_bypass"])
        self.assertTrue(settings["overload_restart"])
        self.assertTrue(settings["over_temperature_restart"])
        self.assertFalse(settings["input_source_detection_prompt_sound"])
        qflag = self.module.decode_qflag_settings("(EakuvxDbjyz")
        for key in ("buzzer", "backlight", "display_return_homepage", "overload_restart",
                    "over_temperature_restart", "input_source_detection_prompt_sound", "overload_bypass"):
            with self.subTest(key=key):
                self.assertEqual(settings[key], qflag[key])

    def test_htemp_order_matches_portal_snapshot(self) -> None:
        # 2026-04-29 portal: PV 20, inverter 28, boost 21, transformer 26, max 28, fans 30 %.
        htemp = self.module.decode_htemp("(020 028 021 026 028 030 030 11000000000000000000")
        self.assertEqual(htemp["pv_temperature_c"], 20)
        self.assertEqual(htemp["inverter_temperature_c"], 28)
        self.assertEqual(htemp["boost_temperature_c"], 21)
        self.assertEqual(htemp["transformer_temperature_c"], 26)
        self.assertEqual(htemp["max_temperature_c"], 28)
        self.assertEqual(htemp["fan_1_speed_percent"], 30)
        self.assertEqual(htemp["fan_2_speed_percent"], 30)

    def test_heep2_restore_second_output_capacity(self) -> None:
        head = "(0 044.0 020 044.0 046.0 054.0 0 058.4 060 120 030 0000 0000 10 0000 52.0 "
        # PDSRS035 then PDSRS030 on 2026-11-16 moved token 16 30000 -> 35000 -> 30000.
        base = {"dual_output_mode": False, "ac_charge_start_hour": 0, "ac_charge_stop_hour": 0,
                "ac_output_start_hour": 0, "ac_output_stop_hour": 0}
        self.assertEqual(self.module.decode_heep2_settings(head + "35000"),
                         {**base, "restore_second_output_battery_capacity": 35})
        self.assertEqual(self.module.decode_heep2_settings(head + "30000"),
                         {**base, "restore_second_output_battery_capacity": 30})
        self.assertEqual(self.module.decode_heep2_settings(head + "3x000"), base)

    def test_status_decodes_from_vendor_positions(self) -> None:
        # April capture (line mode) and today's token 2 (battery mode, off-grid).
        april = self.module.decode_hsts_status("(00 L010000000000 11211001000L112000000")
        self.assertEqual(april["fault_code"], "00")
        self.assertEqual(april["alarms"], "None")
        self.assertEqual(april["grid_tie_state"], "Waiting")
        self.assertEqual(april["front_panel_leds"], "AC on, inverter off, charge off, alarm off")
        today = self.module.decode_hsts_status("(00 B010000000000 10211002120B117200000")
        self.assertEqual(today["grid_tie_state"], "Off-grid")
        self.assertEqual(today["main_output_relay"], "On")
        self.assertEqual(today["second_output"], "On")
        self.assertEqual(today["front_panel_leds"], "AC flashing, inverter on, charge flashing, alarm off")
        # Synthetic: token1 pos3 (battery low), pos5 (overload), pos12 (fan); token2 pos5 (BMS comm fault).
        alarm = self.module.decode_hsts_status("(05 B011010000001 10211102120B117200000")
        self.assertEqual(alarm["alarms"], "Battery low, Overload, Fan fault, BMS communication fault")
        bms = self.module.decode_hbms1_status("(02 1001100000000000 044.8 057.6 150.0 028 0001.6 0000.0 02929 000000")
        self.assertEqual(bms["bms_flags_decoded"], "Communication OK, Charge allowed, Discharge allowed")
        self.assertEqual(bms["bms_temperature_c"], 19.75)
        extra = self.module.decode_extra_fields
        self.assertEqual(extra("HOP", "(242.0 49.9 00532 00529 008 000 06200 005.9 00102")["inductor_power_w"], 102)
        self.assertEqual(extra("HTEMP", "(020 028 021 026 028 030 030 11000000000000000000")["fan_enable"], "On")
        self.assertEqual(extra("HIMSG1", "(0040.05 20250923 12")["software_integrity"], "Complete")
        self.assertEqual(extra("HEEP2", "(0 044.0 020 044.0 046.0 054.0 0 058.4 060 120 030 0000 0000 10 0000 52.0 30000")
                         ["second_output_discharge_time_min"], 0)
        self.assertEqual(extra("HBAT", "(04 052.6 028 002 00000 388 101002010000 00000000")["charge_switches"],
                         "Master on, solar off, AC on")

    def test_pi30_settings_readback(self) -> None:
        qpiri = (
            "(230.0 26.9 230.0 50.0 26.9 6200 6200 48.0 46.0 44.8 56.4 56.4 4 30 060 0 1 2 9 01 0 0 54.0 1 1"
        )
        self.assertEqual(
            self.module.decode_qpiri_settings(qpiri),
            {"ac_input_voltage_range": "Appliance", "output_source_priority": "SBU priority", "charger_priority": "OSO",
             # cross-check only (HEEP1 is primary)
             "output_voltage": "230 V", "output_frequency": "50 Hz", "output_mode": "SIG"},
        )
        self.assertEqual(self.module.decode_qpiri_settings("(230.0 26.9"), {})
        flags = self.module.decode_qflag_settings("(EakxyDbjuvz")
        self.assertTrue(flags["buzzer"])
        self.assertTrue(flags["display_return_homepage"])
        self.assertTrue(flags["backlight"])
        self.assertFalse(flags["overload_bypass"])
        self.assertFalse(flags["power_saving"])
        self.assertFalse(flags["fault_code_record"])
        self.assertEqual(self.module.decode_qflag_settings("(NAK"), {})
        self.assertEqual(
            self.module.decode_qbeqi_settings("(0 060 030 060 030 58.40 000 120 0 0000"),
            {"battery_equalization_mode_enable": False},
        )
        for command in ("QPIRI", "QFLAG", "QBEQI", "PEb", "PDb", "PEz", "PDz", "PGR00", "PGR01"):
            with self.subTest(command=command):
                self.assertEqual(self.module.build_crc_frame(command)[-1:], b"\r")

    def test_v1_entity_policy_keeps_risky_and_low_confidence_fields_out_of_production(self) -> None:
        component_schema = (ROOT / "components" / "solarplug" / "__init__.py").read_text(encoding="utf-8")
        component_header = (ROOT / "components" / "solarplug" / "solarplug.h").read_text(encoding="utf-8")
        component_cpp = (ROOT / "components" / "solarplug" / "solarplug.cpp").read_text(encoding="utf-8")

        self.assertIn('"bms_soc": ("HBMS1"', component_schema)
        self.assertIn("ENTITY_CATEGORY_DIAGNOSTIC", component_schema)
        self.assertNotIn("cell_voltage_list", component_schema)
        self.assertNotIn("PBEQV", component_schema)
        self.assertNotIn("PDSRS", component_schema)
        self.assertNotIn("PDDLYT", component_schema)
        self.assertIn('"charger_priority": ("CSO", "SNU", "OSO")', component_schema)
        self.assertIn('"output_source_priority": ("SUB priority", "SBU priority")', component_schema)
        self.assertNotIn("Utility first", component_schema)  # POP02 is SUF (grid feed) on sibling firmwares
        self.assertIn('"pv_energy_feeding_priority": ("BLU", "LBU")', component_schema)
        self.assertIn('"battery_bulk_voltage": ("battery_bulk_voltage"', component_schema)
        self.assertIn('"battery_recharge_voltage": ("battery_recharge_voltage"', component_schema)
        self.assertIn('"battery_redischarge_voltage": ("Full",)', component_schema)
        self.assertIn('"battery_float_voltage": ("battery_float_voltage"', component_schema)
        self.assertIn('"battery_cut_off_voltage": ("battery_cut_off_voltage"', component_schema)
        self.assertIn('CONF_WRITE_PROFILE = "write_profile"', component_schema)
        self.assertIn("WRITE_PROFILE_UNSAFE", component_schema)
        self.assertIn("UNSAFE_WRITE_NUMBERS", component_schema)
        self.assertNotIn("^S???DAT", component_schema)
        self.assertIn('"battery_equalization_timeout": ("battery_equalization_timeout"', component_schema)
        self.assertIn('"battery_equalization_mode_enable": "battery_equalization_mode_enable"', component_schema)
        self.assertIn(
            '"daily_power_gen": ("HGEN", "daily_power_gen_kwh", UNIT_KILOWATT_HOURS, DEVICE_CLASS_ENERGY, STATE_CLASS_TOTAL_INCREASING',
            component_schema,
        )
        self.assertIn(
            '"monthly_electricity_generation": ("HGEN", "monthly_electricity_generation_kwh", UNIT_KILOWATT_HOURS, DEVICE_CLASS_ENERGY, STATE_CLASS_TOTAL_INCREASING',
            component_schema,
        )
        self.assertIn(
            '"yearly_electricity_generation": ("HGEN", "yearly_electricity_generation_kwh", UNIT_KILOWATT_HOURS, DEVICE_CLASS_ENERGY, STATE_CLASS_TOTAL_INCREASING',
            component_schema,
        )
        self.assertIn(
            '"total_power_generation": ("HGEN", "total_power_generation_kwh", UNIT_KILOWATT_HOURS, DEVICE_CLASS_ENERGY, STATE_CLASS_TOTAL_INCREASING',
            component_schema,
        )

        decoder = (ROOT / "tools" / "decode_h_protocol.py").read_text(encoding="utf-8")
        self.assertNotIn("SolarOfThingsClient", decoder)
        self.assertNotIn("--compare-sot", decoder)

        self.assertIn("MAX_REQUEST_FRAME_BYTES", component_header)
        self.assertIn("MAX_RESPONSE_FRAME_BYTES", component_header)
        self.assertIn("std::array<PendingCommand, MAX_PENDING_COMMANDS>", component_header)
        self.assertIn("build_frame_(const std::string &command, FrameStyle frame_style, uint8_t *frame", component_header)
        self.assertIn("CONF_DECODED_FIELD_LOGGING", component_schema)
        self.assertIn("set_decoded_field_logging", component_header)
        self.assertIn("decoded_field_logging_", component_header)
        self.assertNotIn("std::vector<uint8_t> SolarPlugComponent::build_frame_", component_cpp)

    def test_beta_example_hides_unsafe_and_topology_controls_by_default(self) -> None:
        beta_yaml = (ROOT / "examples" / "atom-s3-lite-write-beta.yaml").read_text(encoding="utf-8")
        beta_package = (ROOT / "packages" / "solarplug-write-beta.yaml").read_text(encoding="utf-8")

        self.assertIn("write_profile: beta", beta_package)
        self.assertIn("raw_frame_logging: false", beta_package)
        self.assertIn("decoded_field_logging: false", beta_package)
        self.assertIn("debug:\n  update_interval: 30s", beta_yaml)
        self.assertIn("platform: debug", beta_yaml)
        self.assertIn("name: Beta Heap Largest Block", beta_yaml)
        self.assertIn("name: Beta Reset Reason", beta_yaml)
        self.assertNotIn("battery_cut_off_voltage:", beta_package)
        self.assertNotIn("battery_bulk_voltage:", beta_package)
        self.assertNotIn("battery_float_voltage:", beta_package)
        self.assertIn("battery_recharge_voltage:", beta_package)
        self.assertIn("battery_redischarge_voltage:", beta_package)
        switches_block = beta_package.split("  switches:", 1)[1].split("\n  numbers:", 1)[0]
        numbers_block = beta_package.split("  numbers:", 1)[1].split("\n  selects:", 1)[0]
        selects_block = beta_package.split("  selects:", 1)[1]
        for block, key in (
            (selects_block, "battery_type"),
            (selects_block, "charger_priority"),
            (selects_block, "output_source_priority"),
            (selects_block, "pv_energy_feeding_priority"),
            (numbers_block, "bms_lock_machine_battery_capacity"),
            (switches_block, "bms_function_enable"),
            (switches_block, "battery_equalization_mode_enable"),
            (switches_block, "power_saving"),
            (numbers_block, "battery_equalization_voltage"),
        ):
            self.assertRegex(block, rf"    {re.escape(key)}:\n(?:      .*\n)*      disabled_by_default: true")


CODEC_SOURCES = [
    ROOT / "components" / "solarplug" / "solarplug_codec.cpp",
    ROOT / "components" / "solarplug" / "solarplug_writes.cpp",
    ROOT / "tests" / "cpp" / "codec_harness.cpp",
]
CORPUS = ROOT / "tests" / "fixtures" / "payload_corpus.tsv"


def build_codec_harness(tmpdir: str) -> Path | None:
    compiler = shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
    if compiler is None:
        return None
    exe = Path(tmpdir) / "codec_harness"
    subprocess.run(
        [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "components" / "solarplug"),
         *map(str, CODEC_SOURCES), "-o", str(exe)],
        check=True,
    )
    return exe


class FirmwareCodecTest(unittest.TestCase):
    """Compiles the ESP's own decode/readback code on the host and checks it against the Python reference."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = build_codec_harness(cls.tmp.name)
        cls.module = load_module()

    @classmethod
    def tearDownClass(cls) -> None:
        cls.tmp.cleanup()

    def setUp(self) -> None:
        if self.exe is None:
            self.skipTest("no C++ compiler on this host")

    def test_codec_selftest(self) -> None:
        result = subprocess.run([str(self.exe)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stdout.strip(), "OK")

    def _dump(self) -> list[tuple[str, str, dict[str, str], dict[str, str]]]:
        result = subprocess.run([str(self.exe), "dump"], input=CORPUS.read_text(encoding="utf-8"),
                                capture_output=True, text=True, check=True)
        frames, fields, readings = [], {}, {}
        self.assertNotIn("SKIPPED", result.stdout, "corpus rows below the firmware's minimum token count")
        for line in result.stdout.splitlines():
            kind, command, *rest = line.split("\t")
            if kind == "F":
                fields[rest[0]] = rest[1] if len(rest) > 1 else ""
            elif kind == "R":
                readings[rest[0]] = rest[1]
            elif kind == "E":
                frames.append((command, rest[0], fields, readings))
                fields, readings = {}, {}
        return frames

    def test_corpus_parity_with_python_reference(self) -> None:
        frames = self._dump()
        self.assertGreater(len(frames), 50)
        m = self.module
        checked = 0

        def same(py: dict, cpp: dict, label: str) -> None:
            nonlocal checked
            for key, value in py.items():
                expected = ("on" if value else "off") if isinstance(value, bool) else str(value)
                with self.subTest(frame=label, key=key):
                    self.assertIn(key, cpp)
                    if isinstance(value, (int, float)) and not isinstance(value, bool):
                        self.assertAlmostEqual(float(cpp[key]), float(value), places=3)
                    else:
                        self.assertEqual(cpp[key], expected)
                    checked += 1

        for command, payload, fields, readings in frames:
            label = f"{command} {payload}"
            if command == "HEEP1":
                same(m.decode_heep1_settings(payload), readings, label)
            elif command == "HEEP2":
                same(m.decode_heep2_settings(payload), readings, label)
                same(m.decode_extra_fields(command, payload), fields, label)
            elif command == "QPIRI":
                same(m.decode_qpiri_settings(payload), readings, label)
            elif command == "QFLAG":
                same(m.decode_qflag_settings(payload), readings, label)
            elif command == "QBEQI":
                same(m.decode_qbeqi_settings(payload), readings, label)
            elif command == "HSTS":
                same(m.decode_hsts_status(payload), fields, label)
            elif command == "HBMS1":
                same(m.decode_hbms1_status(payload), fields, label)
            elif command in ("HOP", "HTEMP", "HIMSG1", "HIMSG2", "HBAT", "HEEP3"):
                same(m.decode_extra_fields(command, payload), fields, label)
            if command == "HTEMP":
                py = m.decode_htemp(payload)
                for key in ("pv_temperature_c", "inverter_temperature_c", "boost_temperature_c",
                            "transformer_temperature_c", "max_temperature_c", "fan_1_speed_percent",
                            "fan_2_speed_percent"):
                    with self.subTest(frame=label, key=key):
                        self.assertAlmostEqual(float(fields[key]), float(py[key]))
                        checked += 1
        self.assertGreater(checked, 200)


def schema_tables() -> dict[str, dict]:
    """The write-entity tables of components/solarplug/__init__.py, read without importing ESPHome."""
    import ast

    tree = ast.parse((ROOT / "components" / "solarplug" / "__init__.py").read_text(encoding="utf-8"))
    tables: dict[str, dict] = {}
    for node in tree.body:
        if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
            name = node.targets[0].id
            if name in ("WRITE_SWITCHES", "WRITE_SELECTS", "EXTENDED_SELECT_OPTIONS"):
                tables[name] = eval(compile(ast.Expression(node.value), name, "eval"),
                                    {"__builtins__": {"tuple": tuple, "range": range}})
            elif name == "WRITE_NUMBERS":  # (key, unit, device_class, min, max, step): constants only for 0, 3-5
                tables[name] = {
                    ast.literal_eval(k): tuple(ast.literal_eval(v.elts[i]) for i in (0, 3, 4, 5))
                    for k, v in zip(node.value.keys, node.value.values)
                }
    return tables


# Reported values that satisfy every ordering rule for the key under test (bounds set wide open).
ORDERED_CONTEXT = {
    "bms_lock_machine_battery_capacity": "soc_back_to_mains=1001",
    "soc_back_to_mains": "bms_lock_machine_battery_capacity=-1;soc_back_to_battery=1001",
    "soc_back_to_battery": "soc_back_to_mains=-1",
    "second_output_cutoff_soc": "restore_second_output_battery_capacity=1001",
    "restore_second_output_battery_capacity": "second_output_cutoff_soc=-1",
    "ac_charge_start_hour": "ac_charge_stop_hour=0",
    "ac_charge_stop_hour": "ac_charge_start_hour=0",
    "ac_output_start_hour": "ac_output_stop_hour=0",
    "ac_output_stop_hour": "ac_output_start_hour=0",
}


class SchemaEncoderParityTest(unittest.TestCase):
    """Every value an HA entity offers must be accepted by the firmware's encoder (solarplug_writes.cpp)."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = build_codec_harness(cls.tmp.name)
        cls.tables = schema_tables()

    @classmethod
    def tearDownClass(cls) -> None:
        cls.tmp.cleanup()

    def setUp(self) -> None:
        if self.exe is None:
            self.skipTest("no C++ compiler on this host")

    def encode(self, requests: list[str]) -> list[str]:
        result = subprocess.run([str(self.exe), "encode"], input="\n".join(requests) + "\n",
                                capture_output=True, text=True, check=True)
        lines = result.stdout.splitlines()
        self.assertEqual(len(lines), len(requests))
        return lines

    def assert_all_accepted(self, requests: list[str]) -> int:
        for request, answer in zip(requests, self.encode(requests)):
            with self.subTest(request=request):
                self.assertTrue(answer.startswith("OK\t"), f"{request!r} -> {answer!r}")
        return len(requests)

    def test_every_switch_state_encodes(self) -> None:
        keys = self.tables["WRITE_SWITCHES"].values()
        self.assertGreaterEqual(self.assert_all_accepted([f"S\t{k}\t{s}" for k in keys for s in (0, 1)]), 30)

    def test_every_select_option_encodes(self) -> None:
        requests = []
        for key, options in self.tables["WRITE_SELECTS"].items():
            requests += [f"L\t{key}\t{option}" for option in options]
        for key, options in self.tables["EXTENDED_SELECT_OPTIONS"].items():
            requests += [f"L\t{key}\t{option}" for option in options]
        self.assertGreaterEqual(self.assert_all_accepted(requests), 40)

    def test_every_number_grid_point_encodes(self) -> None:
        requests = []
        for key, (write_key, low, high, step) in self.tables["WRITE_NUMBERS"].items():
            count = int(round((high - low) / step))
            context = ORDERED_CONTEXT.get(write_key, "")
            for i in range(count + 1):
                value = round(low + i * step, 6)
                requests.append(f"N\t{write_key}\t{value}" + (f"\t{context}" if context else ""))
        self.assertGreater(self.assert_all_accepted(requests), 1000)

    def test_off_grid_and_out_of_range_numbers_are_refused(self) -> None:
        requests, keys = [], []
        for key, (write_key, low, high, step) in self.tables["WRITE_NUMBERS"].items():
            context = ORDERED_CONTEXT.get(write_key, "")
            suffix = f"\t{context}" if context else ""
            requests.append(f"N\t{write_key}\t{high + step}{suffix}")
            keys.append(write_key)
            if step >= 1:
                requests.append(f"N\t{write_key}\t{low + step / 2}{suffix}")
                keys.append(write_key)
        for request, answer in zip(requests, self.encode(requests)):
            with self.subTest(request=request):
                self.assertTrue(answer.startswith("REFUSED\t"), f"{request!r} -> {answer!r}")


if __name__ == "__main__":
    unittest.main()
