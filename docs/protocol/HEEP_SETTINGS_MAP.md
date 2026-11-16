# Settings reference

This is the canonical reference for every inverter setting the component reads or writes. It covers the PowMr POW-HVM6.2KP (`HPVINV02`, `VMII-6200`, firmware `40.05`). Each row gives the frame, the value range, the readback slot, the strength of the evidence, and the default state in `packages/solarplug-write-beta.yaml`. The "unsafe" rows live in `packages/solarplug-write-unsafe.yaml`, which needs `write_profile: unsafe`; every entity there is disabled by default.

Token indexes are zero-based after stripping `(`. "dN" means character N of a packed token. Slots are given for the HPVINV02 layout; the GB3024 and LPVINV02 layouts are normalized onto it, and the priority codes depend on the machine type (see [COMPATIBILITY.md](COMPATIBILITY.md#model-profile)).

## Evidence classes

| Class | Meaning |
|---|---|
| **RAW** | Change **and** restore are visible in a saved capture or recorder export under `research/powmr-protocol-20260425-0348/runs/`. |
| **X2** | The value agrees with an independent source (another query, the portal, the BMS) at two different values. |
| **X1** | The value agrees with an independent source at one value only. |
| **VENDOR** | The slot comes from the vendor PC tool `zhuaibai/TQF-AllInOne` (its HPVINV02 screen, commit `82409ae`). Each VENDOR row also carries its local class. |
| **ACK** | The inverter ACKs the frame, but no readback change has been observed. |
| **NAK** | The inverter rejects the frame on this unit. |

Slot meanings come from three sources:
- write round-trips on this unit;
- the 2026-04-29 portal snapshot;
- the vendor tool.

A slot backed only by the vendor tool stays VENDOR until a local check agrees.

Saved evidence:
- `runs/2026043*` and `03-live-probes/2026050*`: passive bus captures (`command_responses.jsonl`). `runs/20260502-*` hold only prose `result.md` reports.
- `runs/20261116-esphome-api-roundtrips/`: `PCP`, `PEb`, `PEz`, `PGR`, `PDSRS`.
- `runs/20261116-vendor-setter-roundtrips/`: `BMSB2UC`, `BMSU2BC`, `BMSSRC`, `PDSDS`, plus the `PSLV`/`PDSDV`/`PDSRV` NAKs.

## Write controls

Frames are listed as the command the firmware encodes. The CRC16/XMODEM is always added on the wire; where those CRC bytes happen to be printable, the wire form is shown in brackets.

| Control (entity key) | Frame | Range / step | Readback | Evidence | Default |
|---|---|---|---|---|---|
| Charger priority (`charger_priority`) | `PCP00` CSO / `01` SNU / `02` OSO | select | `HEEP1[4]` d2 (primary); `QPIRI[17]` cross-check | RAW (`010`/`012` 2026-11-16; `011` 2026-05-02 prose) + X1 | disabled (package); enabled in the owner's HA |
| Output source priority (`output_source_priority`) | `POP00` SUB / `POP01` SBU | select | `QPIRI[16]` (primary); `HEEP1[0]` cross-check | `POP00` on the wire; `POP01` never seen on the wire; X1 (QPIRI 1 + portal SBU; `HEEP1[0]` has never been seen at 0) + VENDOR | disabled (package); enabled in the owner's HA |
| PV feeding priority (`pv_energy_feeding_priority`) | `PVENGUSE00` BLU / `01` LBU | select | `HEEP1[3]` d3 | portal-driven capture 2026-05-02 (`result.md` prose only) + X1 + VENDOR | disabled (package); enabled in the owner's HA |
| AC input range (`ac_input_voltage_range`) | `PGR00` Appliance / `PGR01` UPS | select | `QPIRI[15]` (primary); `HEEP1[3]` d0 cross-check | RAW (both slots moved 0→1→0, 2026-11-16) | enabled |
| Battery type (`battery_type`) | `PBT00`–`08` (AGM FLD USE LIA PYL TQF GRO LIB LIC); `PBT09` FEL from Easun only | select (LIA/PYL; the rest are opt-in via `extended_select_options`) | `HEEP1[3]` d2 (primary); `QPIRI[12]` cross-check | X2 (4 = PYL with portal PYL in April; 3 = LIA in both slots now) + VENDOR. **Both serial fields currently decode to LIA. This is an inference from the vendor enum; LCD program 05 has not been checked yet.** | disabled |
| Buzzer (`buzzer`) | `PEa` / `PDa` | switch | `HEEP1[5]`; `QFLAG` a | RAW (2026-04-30) | enabled |
| Backlight (`backlight`) | `PEx` / `PDx` (wire `PExSh` / ``PDx`Y``) | switch | `HEEP1[6]`; `QFLAG` x | RAW | enabled |
| Display returns home (`display_return_homepage`) | `PEk` / `PDk` (wire `PEkq:`) | switch | `HEEP1[4]` d1; `QFLAG` k | RAW | enabled |
| Over-temperature restart (`over_temperature_restart`) | `PEv` / `PDv` | switch | `HEEP1[3]` d6; `QFLAG` v | RAW | enabled |
| Overload restart (`overload_restart`) | `PEu` / `PDu` | switch | `HEEP1[3]` d4; `QFLAG` u | X2 (0 = portal Disable; 1 = QFLAG u) | enabled |
| Input source prompt sound (`input_source_detection_prompt_sound`) | `PEy` / `PDy` (wire `PEyCI` / `PDypx`) | switch | `HEEP1[3]` d5; `QFLAG` y | RAW | enabled |
| Overload bypass (`overload_bypass`) | `PEb` / `PDb` | switch | `QFLAG` b (primary); `HEEP1[7]` cross-check | RAW (both moved, twice) | enabled |
| Record fault codes (`fault_code_record`) | `PEz` / `PDz` | switch | `QFLAG` z | RAW | enabled |
| ECO power saving (`power_saving`) | `PEj` / `PDj` | switch | `QFLAG` j | untested; cuts output at low load in battery mode | disabled |
| BMS function (`bms_function_enable`) | `BMSC01` / `BMSC00` | switch | `HEEP1[9]` | ACK + X1 (1 while the BMS link is up) + VENDOR | disabled |
| Equalization enable (`battery_equalization_mode_enable`) | `PBEQE1` / `PBEQE0` (wire `PBEQE0Z2`) | switch | `QBEQI[0]` (`HEEP2[6]` per VENDOR) | ACK; deliberately not toggled on lithium | disabled |
| Max charging current (`maximum_charging_current`, program 02) | `MNCHGC<DDD>` | select. The options are the inverter's announced `QMCHGCR` list (here 10–80 A, step 10) inside the model envelope, persisted across reboots. | `HEEP1[1]` (primary); `QPIRI[14]` cross-check | round-trip `070 → 080 → 070` (2026-05-04) + X2 (`QPIRI[14]` 060 and 080) | enabled |
| Max mains charging current (`maximum_mains_charging_current`, program 11) | `MUCHGC<DDD>` | select. The options are the announced `QMUCHGCR` list (here 2, 10–60 A). | `HEEP1[2]` (primary); `QPIRI[13]` cross-check | round-trip `030 → 040 → 030` (2026-05-04, again 2026-11-16) + X1 | enabled |
| BMS lock SOC (`bms_lock_machine_battery_capacity`) | `BMSSDC<DDD>` | 0–20 %, step 5 | `HEEP1[10]` | ACK (`020`) / NAK (`021`) + X1 | disabled |
| SOC back to mains (`soc_back_to_mains`, program 39) | `BMSB2UC<DDD>` | 5–95 %, step 5 | `HEEP1[11]` | RAW (`025→030→025`) + VENDOR | enabled |
| SOC back to battery (`soc_back_to_battery`, program 40) | `BMSU2BC<DDD>` | 10–100 %, step 5 | `HEEP1[12]` | RAW (`040→045→040`) + VENDOR | enabled |
| Inverter restart SOC (`inverter_restart_soc`, program 41) | `BMSSRC<DDD>` | 5–95 %, step 5 | `HEEP1[13]` | RAW (`050→055→050`) + VENDOR | enabled |
| Second-output cut-off SOC (`second_output_cutoff_soc`, program 62) | `PDSDS<DDD>` | 0–95 %, step 5; must stay below the restore SOC (program 64) | `HEEP2[2]` | RAW (`020→025→020`) + VENDOR | enabled |
| Restore second-output SOC (`restore_second_output_battery_capacity`, program 64) | `PDSRS<DDD>` | 0–50 %, step 5 (the 50 % maximum rests on one off-grid NAK, `051`) | `HEEP2[16]` d0–1 | RAW (`30000→35000→30000`) | enabled |
| Second-output discharge time (`second_output_discharge_time`, program 65) | `PDDCGT<DDDD>` (4 digits; 3 digits gets no reply) | 0–990 min, step 5; 0 = disabled (LCD to confirm) | `HEEP2[16]` d2–4 | RAW (`30000→30990→30000`; recorder export in `runs/20261116-pddcgt-format-probe/README.md`) | disabled (cuts the second output only when the time is reached, program 60 is disabled and the battery is below the program 61 voltage) |
| Restore second-output delay (`restore_second_output_delay_time`, program 66) | `PDDLYT<DDD>` | 0–60 min, step 5 | `HEEP2[13]` | ACK (`005`) / NAK (`004`, `006`) + X1 | enabled |
| Recharge voltage (`battery_recharge_voltage`, program 12) | `PBCV<XX.X>` | 44–51 V, step 1 | `HEEP2[4]` | round-trip `046 → 045 → 046` (2026-05-04) + X1 | enabled |
| Redischarge voltage (`battery_redischarge_voltage`, program 13) | `PBDV<XX.X>`; `Full` = `PBDV00.0` | select: Full, 48–58 V | `HEEP2[5]` (primary); `QPIRI[22]` cross-check | round-trip `054 → 053 → 054` (2026-05-04) + X1 | enabled |
| Equalization voltage / time / timeout / interval | `PBEQV` / `PBEQT` / `PBEQOT` / `PBEQP` | 48–60 V / 5–900 min / 5–900 min / 0–90 d | `HEEP2[7]`–`[10]` | round-trip for all four (`PBEQV58.30`, `PBEQT059`, `PBEQOT119`, `PBEQP031` and their restores, 2026-05-04) + X1 (`QBEQI`, portal) | disabled |
| Grid-connected current (`grid_connected_current`, program 56) | `PGFC<DDD>` | 0–30 A (only 20/21 probed) | `HEEP1[17]` | round-trip `020 → 021 → 020` (2026-05-04) + X1 | enabled |
| Bulk / float / cut-off voltage | `PCVV` / `PBFT` / `PSDV` | unsafe profile only | `HEEP1[14]`–`[16]` | NAK (`PBFT`, `PSDV`) | unsafe |
| Low battery alarm voltage (program 24) | `PSLV<XX.X>` | unsafe profile only | `HEEP2[3]` | **NAK** even at the current value (2026-11-16) | unsafe |
| Second-output cut-off / recovery voltage (programs 61 / 63) | `PDSDV` / `PDSRV<XX.X>` | unsafe profile only | `HEEP2[1]` / `[15]` | **NAK** (2026-11-16) | unsafe |
| Dual output mode (`dual_output_mode`, program 60) | `PDAULC01` / `PDAULC00` | switch | `HEEP2[0]` | VENDOR only, never sent. `HEEP2[0]` = 0 while the April portal said "dual output On": LCD program 60 decides which way round it is. | unsafe |
| Second output power (`second_output_power`) | `PDAULON01` on / `PDAULON00` off | switch; acts immediately on the L2 loads | `HSTS[2]` d4 (re-read after every write) | VENDOR only, never sent | unsafe |
| Grid feed (`grid_feed`, program 44) | `GRIDFEED01` / `GRIDFEED00` | switch | `HEEP1[18]` d0 | VENDOR + X1 (Off = portal), never sent. Exporting needs grid-operator approval. | unsafe |
| CT function (`ct_function`) | `EXTCT01` / `EXTCT00` | switch | `HEEP1[3]` d7 | VENDOR class only (not on its HPVINV02 screen), never sent | unsafe |
| Output frequency (`output_frequency`, program 09) | `F50` / `F60` | select | `HEEP1[4]` d0 (primary); `QPIRI[3]` cross-check | VENDOR + X1 (50 Hz), never sent | unsafe |
| Output voltage (`output_voltage`, program 10) | `V220` / `V230` / `V240` | select | `HEEP1[3]` d8–10 (primary); `QPIRI[2]` cross-check | VENDOR + X1 (230 V), never sent | unsafe |
| AC output mode (`output_mode`, program 04) | `POPM00`–`04` (SIG PAL 3P1 3P2 3P3) | select | `HEEP1[8]` (primary); `QPIRI[21]` cross-check | VENDOR + X1 (SIG), never sent. PAL / 3P* on a single unit can stop the output. | unsafe |
| Grid protocol (`grid_protocol`, program 50) | `^S???RS00`–`04`, shown as Mode 1–5 | select | `HEEP1[3]` d1 | VENDOR + X1 (Mode 4 = `RS03`), never sent | unsafe |
| AC charge window (`ac_charge_start_hour` / `_stop_hour`, programs 46/47) | `^S???ACCT<HH>-<HH>` (vendor format; both hours in one frame) | 0–24 h; the other hour comes from the last readback (fail closed) | `HEEP2[11]` read as start/stop hour (**tentative**) | VENDOR format only, never sent. Easun uses `HHMM-HHMM`. LCD experiment E1 settles the readback layout. Irrelevant on a flat tariff with charger priority OSO. | unsafe |
| AC output window (`ac_output_start_hour` / `_stop_hour`, programs 48/49) | `^S???ACLT<HH>-<HH>` | as above | `HEEP2[12]` (**tentative**) | VENDOR format only, never sent. Can switch the AC output off for part of the day. | unsafe |

Pattern on this unit: the firmware accepts the SOC thresholds and rejects the voltage thresholds. That the cause is the lithium battery type is an inference: a wrong frame format would also NAK, although the vendor tool uses exactly these formats. The Solar of Things portal's "Command not supported" for programs 39–41, 62 and 63 is a cloud limit; the firmware accepts four of them on the bus.

## Settings also exposed as sensors

Most of these have a write control (beta or unsafe profile, see above); `QPIRI[9]` has none.

| Sensor | Source | Value 2026-11-16 | Evidence |
|---|---|---|---|
| Dual output mode (program 60) | `HEEP2[0]`, vendor 0 Off / 1 On | Off | VENDOR only; contradicts the April portal ("On"). LCD program 60. |
| AC charge / AC output / output timer windows (raw) | `HEEP2[11]`, `[12]`, `[14]` | `0000` / `0000` / `0000` | unparsed by the vendor tool; Easun names. LCD experiment E1. |
| Grid protocol mode (program 50) | `HEEP1[3]` d1, shown as `Mode d+1` | Mode 4 | X1 (portal "Mode 4" = 3). The vendor tool labels the same code "Mode3", using 0-based names. The manual's program 50 has Modes 1–4 with default 4, so the 1-based label matches the manual and portal. |
| CT function | `HEEP1[3]` d7 | Off | X1 (portal) + VENDOR |
| Output voltage / frequency (programs 10 / 09) | `QPIRI[2]`, `[3]` (also `HEEP1[3]` d8–10, `[4]` d0) | 230 V / 50 Hz | X1 |
| AC output mode (program 04) | `QPIRI[21]` (also `HEEP1[8]`) | Single | X1 |
| BMS communication | `HEEP1[9]` | On | X1 |
| Grid feed (program 44) | `HEEP1[18]` d0 | Off | X1 (portal) + VENDOR |
| Battery under-voltage | `QPIRI[9]` | 44.8 V | X1 (equals the BMS discharge limit) |
| Bulk / float / low cut-off voltage | `HEEP1[14]`–`[16]` | 56.4 / 56.4 / 42.0 V | X1 (`QPIRI[10]`, `[11]`; portal 42). Bulk and float are equal, so their order rests on VENDOR. |
| Second-output cut-off / recovery voltage, low battery alarm voltage | `HEEP2[1]`, `[15]`, `[3]` | 44.0 / 52.0 / 44.0 V | X1 (portal) + VENDOR. `[1]` and `[3]` are both 44.0, so which is which rests on VENDOR. |

## Status decodes

| Sensor | Source | Evidence |
|---|---|---|
| Inverter alarms | `HSTS[1]` d3–12, `HSTS[2]` d5–6 | VENDOR only; the local check is an all-zero match, so individual labels are unverified |
| Grid tie state, main output relay, second output | `HSTS[2]` d1, d3, d4 | VENDOR + X1 (off-grid, relay on, output on) |
| Parallel units / parallel role | `HSTS[2]` d0, d2 (raw) | VENDOR (`1` unit; role `2` has no vendor table) |
| BMS status | `HBMS1[1]` d0–8 | VENDOR + X1 |
| BMS temperature | `HBMS1[8]` in tenths of a kelvin | VENDOR + X1 (21.5 °C vs. the packs' 20–22 °C) |
| Charge switches | `HBAT[6]` d0–2 (master, solar, AC) | VENDOR + X1 (`AC off` with charger priority OSO) |
| Temperatures and fans | `HTEMP[0]`–`[6]`: PV, inverter, boost, transformer, max, fan 1, fan 2 | the 2026-04-29 portal snapshot matches the capture `020 028 021 026 028 030 030` exactly |
| Fault code / description | `HSTS[0]` (2 digits); text from manual 5.10.1 / 5.10.3 | VENDOR only (`00` today; no non-zero code observed yet). Whether warning codes (5.10.2, which reuse 01–04) also appear here is unconfirmed. |
| Front-panel LEDs | `HSTS[2]` d7–10: AC, inverter, charge, alarm; 0 off, 1 on, 2 flashing | VENDOR only. The manual shows **three** front LEDs (status, charging, fault), so the 4-LED labels need the physical check in the LCD checklist. |
| Inductor power | `HOP[8]` first 5 characters (W) | VENDOR only, **contradicted**: 107 W at 177 W output, 102 W at 529 W, 449 W at ~400 W. It is published without a state class, so it is excluded from statistics. |
| Fan enable | `HTEMP[7]` d0 | VENDOR only (`1` while the fans run at 30 %) |
| Software integrity | `HIMSG1[2]` d0 (1 complete) | VENDOR only |
| HEEP3 raw / CT zero-adjust power | `HEEP3` (`[3]` = `0150`) | VENDOR for HPVINV07/08 only; HEEP3 answering on HPVINV02 was first seen in the 2026-11-16 probe. The meaning here is unconfirmed. |
| Rated inverter / MPPT / AC power | `HIMSG2[0..2]` | VENDOR + X1 (6200 / 8500 / 6200 W = nameplate) |
| Allowed charge currents | `QMCHGCR`, `QMUCHGCR` | PI30, CRC; also used to validate writes |
| BMS protocol type | `HBMS1[0]` (`02`) | VENDOR. The old entity key is `bms_status_code`. |
| Battery series count | `HBAT[0]` (`04` = 48 V) | VENDOR + Easun + portal `batteryNumberInSeries = 4`. Earlier versions of this repo wrongly read it as the battery type. |

## Never sent (block-list)

These frames exist in the vendor tool but must not be sent by this component:

| Frame | Effect |
|---|---|
| `PF` | Reset; factory defaults in PI30 |
| `^S???CLE` | Clears the energy counters |
| `PSAVE` | Persistent settings commit |
| `IVA±01`, `BTA±1` | Output-voltage and battery-voltage calibration |
| `PGFP`, `DISCC`, `EZCTP` | Grid power, discharge-current limit and CT zero-adjust from the HPVINV07/08 screens; not part of the HPVINV02 surface |
| `POP02` (`POP03` only via the opt-in `extended_select_options`, from Easun) | `POP02` is SUF (grid feed) on sibling firmwares |
| `^S???DALT` | Easun's output-timer window (`HEEP2[14]`); the vendor tool has no such setter |

`HOSTCRCEN` / `HOSTCRCDN` (response CRC on / off) are not on this list:
- `HOSTCRCEN` is the automatic keep-alive when `response_crc: true`, and also a `probe_write` diagnostic;
- `HOSTCRCDN` is `probe_write` only, and is refused while `response_crc` is on.

See "Response CRC mode" in [H_COMMANDS.md](H_COMMANDS.md).

## Still unmapped

- `HEEP1[19]`; `HEEP2[0]` (polarity conflicts, see above); `HEEP2[6]` (VENDOR equalization flag, never toggled); `HEEP3[0]`–`[2]` (`2048` each); `HEEP2[11]`, `[12]`, `[14]` (time windows; digit layout tentative); `HSTS[2]` d11+ (d11 repeats the mode letter); `HTEMP[7]` beyond d0; `HBAT[6]` d3+ (vendor reads d6 as the charge/discharge state, which contradicts live data); `HGRID[7]`+ tail; `HPV[3]`+.

## Open checks

These need a live check:
- `HPVB` in daytime, under real PV production;
- same-time `HBMS1` / `HBMS2` against the independent BMS nodes;
- the yearly `HGEN` reset (the daily and monthly resets are confirmed);
- the LCD readings in [../LCD_VALIDATION_CHECKLIST.md](../LCD_VALIDATION_CHECKLIST.md).
