# Inverter LCD validation checklist

This checklist compares what the component decodes from the serial port with what the inverter's own screen shows. Use it to validate the decoding end to end. It is for the PowMr POW-HVM6.2KP (`HPVINV02`, firmware 40.05).

The "HA shows" column is a snapshot from 2026-11-16 17:40. Before you start, re-read the values in Home Assistant: they may have changed since.

## How to read a program on the LCD

The manual (section 5.4) says only this: hold **ENTER** for 3 s to enter setting mode, press **UP**/**DOWN** to select a program, and press **ENTER** to confirm the selection or **ESC** to exit. What follows is the usual behaviour of this inverter family, **not** text from the manual:
- the value usually shows next to the program number;
- pressing **ENTER** on a program opens it for editing, and the value blinks;
- **ESC** normally leaves without saving.

If in doubt, read the value from the screen without pressing ENTER at all.

### Never press ENTER on these programs

They act immediately when confirmed and sit on the scroll path between programs this checklist asks you to read:

| Program | What it does |
|---|---|
| **28** Reset factory setting | Restores factory defaults: battery type, priorities, every threshold |
| **36** Equalization activated immediately | Starts an equalization charge, which you don't want on lithium |
| **45** Reset PV energy storage | Clears the PV energy counters |

## Priority 1: the things we are least sure of

| ✓ | Program | Setting | HA shows | Decoded from | What a mismatch would mean |
|---|---|---|---|---|---|
| ☐ | **05** | Battery type | **LIA** | `HEEP1[3]` d2 = 3 and `QPIRI[12]` = 3, read with the vendor enum (3 = LIA, 4 = PYL) | If the LCD shows **PYL**, the enum is wrong for this unit, and the `battery_type` decode and the HBAT/QPIRI notes must be revisited. If it shows **LIA**, the April/May `PBT03 → PBT04` probe probably left it there. PYL is what the battery and inverter manuals recommend, but switching needs an explicit decision. |
| ☐ | **60** | Dual output (Disable / Use) | Dual Output Mode: Off | `HEEP2[0]` = 0; vendor table: 0 = Off | The April portal said "dual output On" while `HEEP2[0]` was 0. If the LCD shows **Use**, then 0 = Use on this unit (the opposite of the vendor tool). Read only: don't toggle it, because the second output is live (relay On). |
| ☐ | **50** | Country regulations | Mode 4 | `HEEP1[3]` d1 = 3, shown as d+1 | The vendor tool would call this "Mode3". The manual and portal say Mode 4. If the LCD shows Mode 3, our label is off by one. |
| ☐ | **37** | BMS function switch | On (diagnostic) | `HEEP1[9]` = 1 | It is ACK-only in our tests. If the LCD shows Off, `HEEP1[9]` is not this setting. |
| ☐ | **30** | Battery equalization | Off | `QBEQI[0]` = 0 (VENDOR: `HEEP2[6]` = 0) | Never toggle-tested. If it shows Enable, both slots are wrong. |
| ☐ | **24** | Low battery alarm voltage | 44.0 V | `HEEP2[3]` (VENDOR) | `HEEP2[1]` also reads 44.0, so the value can't tell them apart. With a lithium type it can't be changed (manual program 05). |
| ☐ | **61** | Dual output cut-off voltage | 44.0 V | `HEEP2[1]` (VENDOR) | Same as 24 |
| ☐ | **65** | Second output discharge time | 0 min (= Disable?) | `HEEP2[16]` d2–4 = `000`; slot proven by `PDDCGT0990` round-trip | If the LCD shows "Disable", `0000` is the disable code. If it shows "0 min", the inverter has a separate disable state we cannot write yet. |
| ☐ | **46 / 47** | AC charger start / stop time | AC Charge Window Raw: `0000` (disabled entity) | probably `HEEP2[11]` = `0000` (Easun) | Expect 00 / 00. See E1. |
| ☐ | **48 / 49** | AC output on / off time | AC Output Window Raw: `0000` (disabled entity) | probably `HEEP2[12]` = `0000` | Expect 00:00 / 00:00. Read only: don't change it, because it can switch the AC output off. |
| ☐ | **67 / 68** | Output open / stop time | Output Timer Raw: `0000` (disabled entity) | probably `HEEP2[14]` = `0000` | Expect Disable. Read only, for the same reason. |
| ☐ | front panel | LED states | AC flashing, inverter on, charge off, alarm off | `HSTS[2]` d7–10 (VENDOR) | The manual shows **three** LEDs (status, charging, fault), but the decode reports four. Note which physical LED is on, off or flashing, and when. HA lags by up to 20 s. |

## Priority 2: settings mapped by our own round-trips (spot-check a few)

| ✓ | Program | Setting | HA shows | Evidence |
|---|---|---|---|---|
| ☐ | 01 | Output source priority | SBU | X1 + VENDOR (`POP01` never seen on the wire) |
| ☐ | 02 | Maximum charging current | 80 A | X2 |
| ☐ | 03 | AC input voltage range | Appliance (APL) | RAW |
| ☐ | 04 | AC output mode | Single (SIG) | X1 |
| ☐ | 06 | Overload auto restart | Enable | X2 |
| ☐ | 07 | Over-temperature auto restart | Enable | RAW |
| ☐ | 08 | ECO power saving | Disable | `QFLAG` j (untested) |
| ☐ | 09 / 10 | Output frequency / voltage | 50 Hz / 230 V | X1 |
| ☐ | 11 | Maximum utility charging current | 30 A | X1 |
| ☐ | 12 | Back-to-utility voltage | 46 V | X1 |
| ☐ | 13 | Back-to-battery voltage | 54 V | X1 |
| ☐ | 16 | Charger source priority | OSO (only solar) | RAW |
| ☐ | 18 | Alarm (buzzer) | On | RAW |
| ☐ | 19 | Return to default screen | On | RAW |
| ☐ | 20 | Backlight | On | RAW |
| ☐ | 22 | Beep when primary source interrupted | Off | RAW |
| ☐ | 23 | Overload bypass | Disable | RAW |
| ☐ | 25 | Record fault code | Disable | RAW (`QFLAG` z) |
| ☐ | 26 / 27 | Bulk / float voltage | 56.4 / 56.4 V | X1. The two are equal, so their order rests on VENDOR. |
| ☐ | 29 | Low DC cut-off voltage | 42.0 V | X1 |
| ☐ | 31 / 33 / 34 / 35 | Equalization V / time / timeout / interval | 58.4 V / 60 / 120 min / 30 d | X1 |
| ☐ | 38 | Bat SOC under lock | 20 % | X1 |
| ☐ | 39 | Bat SOC turn to AC | 25 % | RAW |
| ☐ | 40 | Bat SOC turn to DC | 40 % | RAW (manual default is 95 %; it was 90 % in April) |
| ☐ | 41 | Bat restart SOC | 50 % | RAW |
| ☐ | 43 | Solar supply priority | LBU (loads first) | prose + X1 |
| ☐ | 44 | Solar feed to grid | Disable | X1 |
| ☐ | 56 | GRID-tie current | 20 A | X1 |
| ☐ | 62 | Dual output SOC point | 20 % | RAW |
| ☐ | 63 | Dual output recovery voltage | 52.0 V | X1 |
| ☐ | 64 | Dual output recovery SOC | 30 % | RAW |
| ☐ | 66 | Dual recover delay | 10 min | X1 (it was 5 in April; changed on the LCD?) |

## Optional experiment (one change you undo straight away)

Do this only if you want to settle the time-window slot. Before you start, tell me the time, so I can read the recorder history for that minute.

| ID | Change on the LCD | What it tells us | Restore | Risk |
|---|---|---|---|---|
| E1 | Program **47** (AC charger **stop** time) `00` → `01` | `HEEP2[11]` `0000` → `0001` means the slot holds "start hour + stop hour". `0100` would mean a different layout. Changing the stop time rather than the start time is what tells the two layouts apart. | set it back to `00` | Low. With charger priority OSO the AC charger is off, but the window stays at 00–01 until you restore it. |

There is no experiment for programs 24 and 61. Per program 05 in the manual, 24/26/27/29/61 can only be changed with a User-Defined battery type, which is why `PSLV`/`PDSDV` NAK. They both read 44.0 V, so which one is `HEEP2[1]` and which is `HEEP2[3]` stays a VENDOR-only mapping.

Don't experiment with 05, 28, 36, 37, 45, 48/49, 60, 61 or 67/68.

## What each result unlocks

The unsafe-profile controls in `packages/solarplug-write-unsafe.yaml` read their state from the slots below. Keep each one disabled until its check is done:

| LCD check | Control it settles | Why |
|---|---|---|
| 60 | Dual Output Mode (`PDAULC`) | Whether `HEEP2[0]` = 0 means Disable or Use |
| E1 (47) | AC Charge / AC Output start and stop hours (`^S???ACCT`/`ACLT`) | The digit layout of `HEEP2[11]`/`[12]` that the setters read the other hour from |
| 50 | Grid Protocol (`^S???RS`) | Whether "Mode 4" on HA is program 50 Mode 4 |
| 05 | Battery Type (beta select) | LIA vs PYL; no change without your explicit OK |

## Record results here

| Program | LCD shows | Matches HA? | Notes |
|---|---|---|---|
| 05 | | | |
| 60 | | | |
| 50 | | | |
| 37 | | | |
| 30 | | | |
| 24 | | | |
| 28 / 36 / 45 | (not touched) | | |
| 61 | | | |
| 65 | | | |
| 46 / 47 | | | |
| 48 / 49 | | | |
| 67 / 68 | | | |
| LEDs | | | |

Evidence classes (RAW, X1, X2, VENDOR) are defined in [protocol/HEEP_SETTINGS_MAP.md](protocol/HEEP_SETTINGS_MAP.md).
