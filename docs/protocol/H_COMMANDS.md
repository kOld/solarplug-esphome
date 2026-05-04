# H Commands

This file records the Solar Plug / Solar of Things serial labels observed on the passive sniffer and reproduced by the active read-only replacement on the confirmed `POW-HVM6.2KP` / `HPVINV02` setup.

The ESPHome implementation lives in `components/solarplug`. The replay decoder lives in `tools/decode_h_protocol.py`. This document is the canonical protocol reference for the sniffer, replay tooling, and active read-only component.

## Framing

Observed H-family request frames are plain ASCII plus `0D`. No CRC bytes are present on these requests in the current capture set.

Inverter response frames start with byte `0x28`, ASCII `(`. That byte is a response-frame sentinel, not part of the first field. There is no matching `)`. Many ACK/NAK responses also carry two trailing CRC16/XMODEM bytes before `0D`; for example raw `(ACK9 ` is payload `(ACK` plus CRC bytes `39 20`, and raw `(NAKss` is payload `(NAK` plus CRC bytes `73 73`.

Captured raw examples:

- `HSTS` -> `48 53 54 53 0D`
- `HTEMP` -> `48 54 45 4D 50 0D`
- `HBMS1` -> `48 42 4D 53 31 0D`
- `QPRTL` -> `51 50 52 54 4C 0D`
- `HIMSG1` -> `48 49 4D 53 47 31 0D`

`PCP00`, `PCP01`, and `PCP02` are the captured CRC-framed charger-priority writes so far. They use CRC16/XMODEM with the same byte-safety adjustment already used in the local pipsolar code, and the CRC bytes are transmitted high byte first, then low byte. The latest live round-trip on the charger-priority row captured `PCP01` on `OSO -> SNU`, `PCP02` on `SNU -> OSO`, `PCP00` on `OSO -> CSO`, and `PCP02` again on `CSO -> OSO`. The select-state mapping is now closed: `PCP00` = `CSO`, `PCP01` = `SNU`, `PCP02` = `OSO`. `PCP02` is shared by both restore windows, and `PCP03` remains unconfirmed.

Captured CRC reference frames:

- `PCP00` -> `50 43 50 30 30 8D 7A 0D`
- `PCP01` -> `50 43 50 30 31 9D 5B 0D`
- `PCP02` -> `50 43 50 30 32 AD 38 0D`

## Live Select Families

The current release component and decoder also expose the two portal select families that sit above the write surface:

- `Output Source Priority Setting` -> `POP00` / `POP01` / `POP02`
  - the raw capture report pins `POP00` as `50 4F 50 30 30 C2 48 0D`
  - the current best mapping is `POP00` = `SUB priority`, `POP01` = `SBU priority`, `POP02` = `Utility first (legacy)`
  - `POP01` and `POP02` are current-best mappings from the portal round-trips; keep them paired with the decoder until a separate isolated raw replay is captured for each one
- `PV Energy Feeding Priority Setting` -> `PVENGUSE00` / `PVENGUSE01`
  - the live bus capture paired `PVENGUSE00^a` with `BLU` and `PVENGUSE01N@` with `LBU`, both returning raw `(ACK9 ` (`(ACK` plus CRC `39 20`)
  - the current best mapping is `PVENGUSE00` = `BLU` and `PVENGUSE01` = `LBU`
  - keep the family label and mapping together in the decoder until a cleaner raw-byte replay is written out

The inverter clock setter is another CRC-framed family. The observed live payload is `^S???DAT<YYMMDDHHMMSS>`; the three `?` bytes are still opaque selector bytes, and the trailing two bytes are CRC16/XMODEM high-byte-first before `0D`.

Observed clock-set examples:

- `^S???DAT260429200700` -> `5E 53 3F 3F 3F 44 41 54 32 36 30 34 32 39 32 30 30 37 30 30 E8 57 0D`
- `^S???DAT260429200600` -> `5E 53 3F 3F 3F 44 41 54 32 36 30 34 32 39 32 30 30 36 30 30 DF 67 0D`
- response payload -> `^1`

The equalization interval setter is also CRC-framed. The live bus capture used `PBEQP030` / `PBEQP031` for a one-day delta:

- `PBEQP030` -> `50 42 45 51 50 30 33 30 36 12 0D`
- `PBEQP031` -> `50 42 45 51 50 30 33 31 26 33 0D`

The equalization timeout setter is also CRC-framed. The live bus capture used `PBEQOT120` for the current inverter value:

- `PBEQOT120` -> `50 42 45 51 4F 54 31 32 30 88 44 0D`

The battery cut-off voltage setter is also CRC-framed. The live bus capture used `PSDV41.0` as the current probe value:

- `PSDV41.0` -> `50 53 44 56 34 31 2E 30 4A 40 0D`

The battery type setter is also CRC-framed. The live bus capture used a one-step lithium profile change and restore:

- `PBT03` -> `50 42 54 30 33 17 6D 0D`
- `PBT04` -> `50 42 54 30 34 67 8A 0D`

The BMS lock machine battery capacity setter is also CRC-framed. The live bus capture used `BMSSDC021Xe` / `BMSSDC020HD` for a one-step delta:

- `BMSSDC021Xe` -> `42 4D 53 53 44 43 30 32 31 58 65 0D`
- `BMSSDC020HD` -> `42 4D 53 53 44 43 30 32 30 48 44 0D`

The BMS function enable setter is also CRC-framed. The live bus capture used `BMSC00` / `BMSC01` for the Off/On toggle:

- `BMSC00` -> `42 4D 53 43 30 30 48 9E 0D`
- `BMSC01` -> `42 4D 53 43 30 31 58 BF 0D`

The grid-connected current setter is also CRC-framed. The live bus capture used `PGFC021` for a one-amp delta:

- `PGFC021` -> `50 47 46 43 30 32 31 E1 4B 0D`
- `PGFC020` -> `50 47 46 43 30 32 30 F1 6A 0D`

The maximum mains charging current setter is also CRC-framed. The live bus capture used `MUCHGC020` for the current inverter value and `MUCHGC030` as the restore probe:

- `MUCHGC020` -> `4D 55 43 48 47 43 30 32 30 F3 F1 0D`
- `MUCHGC030` -> `4D 55 43 48 47 43 30 33 30 C0 C0 0D`

The equalization time setter is also CRC-framed. The live bus capture used `PBEQT059` / `PBEQT060` for a one-minute delta:

- `PBEQT059` -> `50 42 45 51 54 30 35 39 C7 6C 0D`
- `PBEQT060` -> `50 42 45 51 54 30 36 30 03 16 0D`

The battery float charging voltage setter is also CRC-framed. The live bus capture used `PBFT56.5` / `PBFT56.4` / `PBFT56.3` for a one-step delta, but all probes returned `NAKss`:

- `PBFT56.5` -> `50 42 46 54 35 36 2E 35 A9 18 0D`
- `PBFT56.4` -> `50 42 46 54 35 36 2E 34 B9 39 0D`
- `PBFT56.3` -> `50 42 46 54 35 36 2E 33 C9 DE 0D`

The equalization mode enable row is also bus-confirmed. The live probe used a one-step enable/restore pair:

- `PBEQE1` -> `50 42 45 51 45 31 4A 13 0D`
- `PBEQE0Z2` -> `50 42 45 51 45 30 5A 32 0D`

## Request Table

| Request | Full hex | CRC bytes | CRC type | Terminator |
|---|---|---|---|---|
| `HSTS` | `48 53 54 53 0D` | none | none | `0D` |
| `HGRID` | `48 47 52 49 44 0D` | none | none | `0D` |
| `HOP` | `48 4F 50 0D` | none | none | `0D` |
| `HBAT` | `48 42 41 54 0D` | none | none | `0D` |
| `HPV` | `48 50 56 0D` | none | none | `0D` |
| `HTEMP` | `48 54 45 4D 50 0D` | none | none | `0D` |
| `HBMS1` | `48 42 4D 53 31 0D` | none | none | `0D` |
| `HGEN` | `48 47 45 4E 0D` | none | none | `0D` |
| `QPRTL` | `51 50 52 54 4C 0D` | none | none | `0D` |
| `HIMSG1` | `48 49 4D 53 47 31 0D` | none | none | `0D` |
| `HBMS2` | `48 42 4D 53 32 0D` | none | none | `0D` |
| `HBMS3` | `48 42 4D 53 33 0D` | none | none | `0D` |
| `HEEP1` | `48 45 45 50 31 0D` | none | none | `0D` |
| `HEEP2` | `48 45 45 50 32 0D` | none | none | `0D` |
| `HPVB` | `48 50 56 42 0D` | none | none | `0D` |
| `PCP00` | `50 43 50 30 30 8D 7A 0D` | 2 | `CRC16/XMODEM` | `0D` |
| `PCP01` | `50 43 50 30 31 9D 5B 0D` | 2 | `CRC16/XMODEM` | `0D` |
| `PCP02` | `50 43 50 30 32 AD 38 0D` | 2 | `CRC16/XMODEM` | `0D` |
| `inverterSystemClock` | `5E 53 3F 3F 3F 44 41 54 32 36 30 34 32 39 32 30 30 37 30 30 E8 57 0D` | 2 | `CRC16/XMODEM` | `0D` |
| `PBEQP<DDD>` | `50 42 45 51 50 30 33 30 36 12 0D` | 2 | `CRC16/XMODEM` | `0D` |
| `PBEQOT<DDD>` | `50 42 45 51 4F 54 31 32 30 88 44 0D` | 2 | `CRC16/XMODEM` | `0D` |
| `PSDV<XX.X>` | `50 53 44 56 34 31 2E 30 4A 40 0D` | 2 | `CRC16/XMODEM` | `0D` |
| `PGFC<DDD>` | `50 47 46 43 30 32 31 E1 4B 0D` | 2 | `CRC16/XMODEM` | `0D` |
| `MUCHGC<DDD>` | `4D 55 43 48 47 43 30 32 30 F3 F1 0D` | 2 | `CRC16/XMODEM` | `0D` |
| `PBEQT<DDD>` | `50 42 45 51 54 30 35 39 C7 6C 0D` | 2 | `CRC16/XMODEM` | `0D` |
| `PBFT<XX.X>` | `50 42 46 54 35 36 2E 35 A9 18 0D` | 2 | `CRC16/XMODEM` | `0D` |
| `PBEQE1` | `50 42 45 51 45 31 4A 13 0D` | 2 | `CRC16/XMODEM` | `0D` |
| `PBEQE0Z2` | `50 42 45 51 45 30 5A 32 0D` | none | none | `0D` |
| `PBEQV<XX.XX>` | `50 42 45 51 56 35 38 2E 33 30 13 1E 0D` | 2 | `CRC16/XMODEM` | `0D` |
| `PDa` | `50 44 61 E3 41 0D` | 2 | `CRC16/XMODEM` | `0D` |
| `PEa` | `50 45 61 D0 70 0D` | 2 | `CRC16/XMODEM` | `0D` |
| ``PDx`Y`` | `50 44 78 60 59 0D` | none | none | `0D` |
| `PExSh` | `50 45 78 53 68 0D` | none | none | `0D` |
| `PDypx` | `50 44 79 70 78 0D` | none | none | `0D` |
| `PEyCI` | `50 45 79 43 49 0D` | none | none | `0D` |

## Command Table

| Request | Response example | Decoded fields | Confidence | Matching Solar of Things keys |
|---|---|---|---|---|
| `HSTS` | `(00 L010000000000 11211001000L112000000`; live replacement capture also observed `(00 B010000000000 10211002100B127000000` | status code; mode code; mode label; status bits; fault bits. `L` maps to `Mains Mode`; `B` maps to `Battery Mode` but stays medium-confidence until same-time portal confirmation. | high for status fields; medium for `B` label | `Mode`, `Status Code`, `Warnings Present`, `Faults Present` |
| `HGRID` | `(240.2 49.9 280 090 70 40 +00291 0 06500 11+00000` | AC input voltage; mains frequency; high/low mains-loss voltage; high/low mains-loss frequency; mains power; mains current flow direction code; mains current flow direction; rated power | high | `AC input voltage`, `Mains Frequency`, `Mains Current Flow Direction`, `Mains Power` |
| `HOP` | `(240.2 49.9 00216 00177 003 006 06200 005.9 00107` | AC output voltage; output frequency; apparent power; active power; load percent; DC component/status; rated power; inductor current | high | `Output Voltage`, `Output Frequency`, `Output Apparent Power`, `Output Active Power`, `Output Load Percent` |
| `HBAT` | `(04 053.5 083 001 00000 393 101002010000 00000000` | battery type code; battery type; battery voltage; battery capacity; charge current; discharge current; BUS voltage | high | `Battery Type`, `Battery Voltage`, `Battery Capacity`, `Battery Charging Current`, `Battery Discharge Current`, `BUS Voltage` |
| `HPV` | `(000.0 00.0 00000 00000.0 00000 0 060.0 027 08500`; 2026-05-01 active replacement recorder also observed nonzero PV values | PV voltage; PV current; PV power; token 4 generation-power candidate | high for voltage/current/power; low for token 4 | `PV Voltage`, `PV Current`, `PV Power`; `Generation Power` candidate only |
| `HTEMP` | `(020 028 022 026 028 030 030 11000000000000000000` | inverter temperature; boost temperature; transformer temperature; PV temperature; fan 1 speed; fan 2 speed; max temperature; temperature status bits | high | `Inverter Temperature`, `Boost Temperature`, `Transformer Temperature`, `PV Temperature`, `Fan 1 Speed`, `Fan 2 Speed`, `Max. Temperature` |
| `HBMS1` | `(02 1001100000000000 044.8 057.6 150.0 083 0001.7 0000.0 02946 000000` | BMS status code; BMS flags; discharge limit; charge limit; charge-current limit; SOC; charging current; discharge current | medium-high for SOC/current after independent BMS cross-check; medium for flags/limits | `BMS Charge Current Limit`, `BMS Charge Voltage Limit`, `BMS Charging Current`, `BMS Communication Control Function`, `BMS Communication Normal`, `BMS Discharge Current`, `BMS Discharge Voltage Limit`, `BMS Low Battery Alarm Flag`, `BMS Low Power SOC`, `BMS Returns To Battery Mode SOC`, `BMS Returns To Mains Mode SOC` |
| `HGEN` | `(260429 23:02 03.043 0059.4 0066.4 000000066.4 000000000000` | date; ISO date; time; daily generation; monthly generation; yearly generation; total generation | high; daily/monthly rollover confirmed | `Daily Power Gen.`, `Monthly Electricity Generation`, `Yearly Electricity Generation`, `Total Power Generation`, `dailyProducedQuantity`, `monthlyProducedQuantity`, `yearlyProducedQuantity`, `totalProducedQuantity`, `pvGeneratedEnergyOfDay`, `currentMonthPvGenerationReadDirectly`, `totalPvGenerationReadDirectlyK` |
| `QPRTL` | `(HPVINV02` | protocol type / device type string | medium | `Device Type`, `deviceType` |
| `HIMSG1` | `(0040.05 20250923 12` | software version; software date; ISO software date; revision | high | `Software Version`, `softwareVersion` |
| `HBMS2` | `(0000.0 0000.0 1 3351 0015 3350 0016 0000000000000000000000` | remaining/nominal capacity placeholders; display mode; max/min voltage summary; max/min cell position | medium for min/max voltage summary; low for capacity fields on this setup | `Remaining Capacity`, `Nominal Capacity`, `Display Mode`, `Max Voltage`, `Min Voltage`, `Max Voltage Cell Position`, `Min Voltage Cell Position` |
| `HBMS3` | `(0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 00000000` | 16-cell voltage slots; raw tail | low on this setup; slots are zeroed while the independent BMS node reports real cell voltages | `Cell voltage list` |
| `HEEP1` | `(1 060 030 03410110230 012 1 1 0 0 1 020 025 090 050 056.4 056.4 042.0 020 0 0` | raw config snapshot; packed head unresolved | low | `bmsReturnsToMainsModeSOC`, `batteryCutOffVoltageSetting`, constant/float charge voltage candidates |
| `HEEP2` | `(0 044.0 020 044.0 046.0 054.0 0 058.4 060 120 030 0000 0000 05 0000 52.0 50000` | raw config snapshot; packed head unresolved | low | `batteryRechargeVoltageSetting`, `batteryRedischargeVoltageSetting`, `batteryEqualizationVoltageSetting`, `batteryEqualizationTimeoutSetting`, `restoreSecondOutputDelayTimeSetting`, `restoreSecondOutputBatteryVoltageSetting` |
| `HPVB` | `(000.0 00.0 00000 0 380.0 00000000000000000000000` | PV voltage; PV current; PV power; PV charging mark; bus voltage; raw tail | medium | `pvVoltage`, `pvCurrent`, `pvPower`, `pvChargingMark`, `busVoltage` |

Still unmatched as scalar values:

| Command | Token | Value | Best current match | Status |
|---|---:|---|---|---|
| `HEEP1` | 4 | `03410110230` | packed composite value; observed variants `03410010230`, `03410100230` | unresolved |
| `HEEP1` | 5 | `012` | `bmsReturnsToMainsModeSOC` / display / mode-related rows | ambiguous |
| `HEEP2` | 17 | `50000` | packed composite value | unresolved |

Keep these packed slots diagnostic-only until isolated write/readback deltas prove exact semantics.

| `inverterSystemClock` | `^1` | clock selector; clock digits; clock ISO; portal key; portal label; portal value; ack observed; ack response | high | `Inverter System Clock` / `inverterSystemClock` |
| `batteryEqualizationIntervalSetting` | raw `(ACK9 ` -> payload `(ACK` + CRC `39 20` | requested value; portal key; portal label; portal value; ack observed; ack response | high | `Battery Equalization Interval Setting` / `batteryEqualizationIntervalSetting` |
| `restoreSecondOutputBatCapacitySetting` | raw `(ACK9 ` / raw `(NAKss` -> payload `(ACK` / `(NAK` plus CRC | requested value; portal key; portal label; portal value; ack observed; ack response | medium | `Restore Second Output Battery Capacity Setting` / `restoreSecondOutputBatCapacitySetting` |
| `batteryRechargeVoltageSetting` | raw `(ACK9 ` -> payload `(ACK` + CRC `39 20` | requested value; portal key; portal label; portal value; ack observed; ack response | high | `Battery Recharge Voltage Setting` / `batteryRechargeVoltageSetting` |
| `batteryRedischargeVoltageSetting` | raw `(ACK9 ` -> payload `(ACK` + CRC `39 20` | requested value; portal key; portal label; portal value; ack observed; ack response | high | `Battery Redischarge Voltage Setting` / `batteryRedischargeVoltageSetting` |
| `batteryEqualizationVoltageSetting` | raw `(ACK9 ` -> payload `(ACK` + CRC `39 20` | requested value; portal key; portal label; portal value; ack observed; ack response | high | `Battery Equalization Voltage Setting` / `batteryEqualizationVoltageSetting` |
| `batteryEqualizationModeEnableSetting` | raw `(ACK9 ` -> payload `(ACK` + CRC `39 20` | requested value; portal key; portal label; portal value; ack observed; ack response | high | `Battery Equalization Mode Enable Setting` / `batteryEqualizationModeEnableSetting` |

## Confirmed write frames

The first write-side control mapping we have proven on the live inverter is the portal `Buzzer On` row. The response column below preserves raw bus text; decode tooling strips validated response CRC bytes before classifying ACK/NAK.

| Frame | Portal field | Value | Response | HEEP1 effect | Confidence |
|---|---|---|---|---|---|
| `PDa` | `buzzerOn` / `Buzzer On` | Disable | `(ACK9 ` | token 6: `1 -> 0` | high |
| `PEa` | `buzzerOn` / `Buzzer On` | Enable | `(ACK9 ` | token 6: `0 -> 1` | high |
| ``PDx`Y`` | `backlightOn` / `Backlight On` | Disable | `(ACK9 ` | token 6: `1 -> 0` | high |
| `PExSh` | `backlightOn` / `Backlight On` | Enable | `(ACK9 ` | token 6: `0 -> 1` | high |
| `PDk` | `displayAutomaticallyReturnsToHomepage` / `Display Automatically Returns To Homepage` | Disable | `(ACK9 ` | zero-based `HEEP1[4]`: `012 -> 002` | high |
| `PEkq:` | `displayAutomaticallyReturnsToHomepage` / `Display Automatically Returns To Homepage` | Enable | `(ACK9 ` | zero-based `HEEP1[4]`: `002 -> 012` | high |
| `PBEQP030` | `batteryEqualizationIntervalSetting` / `Battery Equalization Interval Setting` | `30` | `(ACK9 ` | zero-based `HEEP2[10]`: `031 -> 030` restore | high |
| `PBEQP031` | `batteryEqualizationIntervalSetting` / `Battery Equalization Interval Setting` | `31` | `(ACK9 ` | zero-based `HEEP2[10]`: `030 -> 031` | high |
| `PBEQOT119` | `batteryEqualizationTimeoutSetting` / `Battery Equalization Timeout Setting` | `119` | `(ACK9 ` | zero-based `HEEP2[9]`: `120 -> 119` | high |
| `PBEQOT120` | `batteryEqualizationTimeoutSetting` / `Battery Equalization Timeout Setting` | `120` | `(ACK9 ` | zero-based `HEEP2[9]`: `119 -> 120` restore | high |
| `PSDV41.0` | `batteryCutOffVoltageSetting` / `Battery Cut Off Voltage Setting` | `41.0` | `(NAKss` | tested value rejected; HEEP1 token 17 still points at the cutoff row | high |
| `BMSSDC021Xe` | `bmsLockMachineBatteryCapacitySetting` / `BMS Lock Machine Battery Capacity (%)` | `21` | `(NAKss` | one-step raise rejected; current value remains 20 | medium |
| `BMSSDC020HD` | `bmsLockMachineBatteryCapacitySetting` / `BMS Lock Machine Battery Capacity (%)` | `20` | `(ACK9 ` | restore probe; current value accepted | medium |
| `BMSC00` | `bmsFunctionEnableSetting` / `BMS Function Enable Setting` | Off | `(ACK9 ` | change probe; accepted as Off on this firmware | high |
| `BMSC01` | `bmsFunctionEnableSetting` / `BMS Function Enable Setting` | On | `(ACK9 ` | restore probe; accepted as On on this firmware | high |
| `PBT03` | `batteryTypeSetting` / `Battery Type Setting` | `LIA` | `(ACK9 ` | change probe; accepted as lithium-ion profile on this firmware | medium-high |
| `PBT04` | `batteryTypeSetting` / `Battery Type Setting` | `PYL` | `(ACK9 ` | restore probe; accepted as Pylontech profile on this firmware | medium-high |
| `PGFC021` | `gridConnectedCurrentSetting` / `Grid Connected Current Setting` | `21` | `(ACK9 ` | zero-based `HEEP1[17]`: `020 -> 021` | high |
| `PGFC020` | `gridConnectedCurrentSetting` / `Grid Connected Current Setting` | `20` | `(ACK9 ` | zero-based `HEEP1[17]`: `021 -> 020` restore | high |
| `MUCHGC020` | `maximumMainsChargingCurrentSetting` / `Maximum Mains Charging Current Setting` | `20` | `(ACK9 ` | family confirmed on the live bus; trim round-trip to 20 A succeeded | high |
| `MUCHGC030` | `maximumMainsChargingCurrentSetting` / `Maximum Mains Charging Current Setting` | `30` | `(ACK9 ` | restore probe; family confirmed on the live bus | high |
| `MUCHGC040` | `maximumMainsChargingCurrentSetting` / `Maximum Mains Charging Current Setting` | `40` | `(ACK9 ` | zero-based `HEEP1[2]`: `030 -> 040 -> 030` restore loop | high |
| `MNCHGC050` | `maximumChargingCurrentSetting` / `Maximum Charging Current Setting` | `50` | `(ACK9 ` | family confirmed on the live bus; trim round-trip to 50 A succeeded | high |
| `MNCHGC060` | `maximumChargingCurrentSetting` / `Maximum Charging Current Setting` | `60` | `(ACK9 ` | restore probe; family confirmed on the live bus | high |
| `MNCHGC070` | `maximumChargingCurrentSetting` / `Maximum Charging Current Setting` | `70` | `(ACK9 ` | zero-based `HEEP1[1]`: `060 -> 070`, preserved after ESPHome node restart | high |
| `MNCHGC080` | `maximumChargingCurrentSetting` / `Maximum Charging Current Setting` | `80` | `(ACK9 ` | zero-based `HEEP1[1]`: `070 -> 080 -> 070` restore loop | high |
| `PBEQT059` | `batteryEqualizationTimeSetting` / `Battery Equalization Time Setting` | `59` | `(ACK9 ` | zero-based `HEEP2[8]`: `060 -> 059` | high |
| `PBEQT060` | `batteryEqualizationTimeSetting` / `Battery Equalization Time Setting` | `60` | `(ACK9 ` | zero-based `HEEP2[8]`: `059 -> 060` restore | high |
| `PBFT56.5` | `batteryFloatChargingVoltageSetting` / `Battery Float Charging Voltage Setting` | `56.5` | `(NAKss` | bus family observed; tested values rejected | medium |
| `PBFT56.4` | `batteryFloatChargingVoltageSetting` / `Battery Float Charging Voltage Setting` | `56.4` | `(NAKss` | restore probe; tested values rejected | medium |
| `PBFT56.3` | `batteryFloatChargingVoltageSetting` / `Battery Float Charging Voltage Setting` | `56.3` | `(NAKss` | additional probe; tested values rejected | medium |
| `PBEQE1` | `batteryEqualizationModeEnableSetting` / `Battery Equalization Mode Enable Setting` | On | `(ACK9 ` | no confirmed HEEP delta yet | high |
| `PBEQE0Z2` | `batteryEqualizationModeEnableSetting` / `Battery Equalization Mode Enable Setting` | Off | `(ACK9 ` | no confirmed HEEP delta yet | high |
| `PDu` | `overloadAutomaticRestart` / `Overload Automatic Restart` | Disable | `(ACK9 ` | no confirmed HEEP delta yet | high |
| `PEu` | `overloadAutomaticRestart` / `Overload Automatic Restart` | Enable | `(ACK9 ` | no confirmed HEEP delta yet | high |
| `PDypx` | `inputSourceDetectionPromptSound` / `Input Source Detection Prompt Sound` | Disable | `(ACK9 ` | no confirmed HEEP1 delta in current capture | high |
| `PEyCI` | `inputSourceDetectionPromptSound` / `Input Source Detection Prompt Sound` | Enable | `(ACK9 ` | no confirmed HEEP1 delta in current capture | high |

### Parameterized write family

The inverter clock is a parameterized, CRC-framed write family rather than a fixed short token. The live probe used a one-minute step forward and restore:

| Family | Example frame | Portal field | Example value | Ack | HEEP1 effect | Confidence |
|---|---|---|---|---|---|---|
| `^S???DAT<YYMMDDHHMMSS>` | `^S???DAT260429200700` | `inverterSystemClock` / `Inverter System Clock` | `2026-04-29 20:07:00` | `^1` | no confirmed HEEP delta in current capture | high |
| `^S???DAT<YYMMDDHHMMSS>` | `^S???DAT260429200600` | `inverterSystemClock` / `Inverter System Clock` | `2026-04-29 20:06:00` | `^1` | restore probe; no confirmed HEEP delta | high |
| `PDSRS<DDD>` | `PDSRS051` | `restoreSecondOutputBatCapacitySetting` / `Restore Second Output Battery Capacity Setting` | `51` | `(NAKss` | rejected at 51; restore probe `PDSRS050` returned `(ACK9 ` | medium |
| `PDSRS<DDD>` | `PDSRS050` | `restoreSecondOutputBatCapacitySetting` / `Restore Second Output Battery Capacity Setting` | `50` | `(ACK9 ` | restore probe; no confirmed HEEP delta | medium |
| `PBEQV<XX.XX>` | `PBEQV58.30` | `batteryEqualizationVoltageSetting` / `Battery Equalization Voltage Setting` | `58.3` | `(ACK9 ` | zero-based `HEEP2[7]`: `058.4 -> 058.3` | high |
| `PBEQV<XX.XX>` | `PBEQV58.40` | `batteryEqualizationVoltageSetting` / `Battery Equalization Voltage Setting` | `58.4` | `(ACK9 ` | zero-based `HEEP2[7]`: `058.3 -> 058.4` restore | high |
| `PDDLYT<DDD>` | `PDDLYT006` | `restoreSecondOutputDelayTimeSetting` / `Restore Second Output Delay Time Setting` | `6` | `(NAKss` | rejected at 6; restore probe `PDDLYT005` returned `(ACK9 ` | medium |
| `PDDLYT<DDD>` | `PDDLYT004` | `restoreSecondOutputDelayTimeSetting` / `Restore Second Output Delay Time Setting` | `4` | `(NAKss` | rejected at 4; zero-based `HEEP2[13]` stayed `05` | medium |
| `PDDLYT<DDD>` | `PDDLYT005` | `restoreSecondOutputDelayTimeSetting` / `Restore Second Output Delay Time Setting` | `5` | `(ACK9 ` | accepted restore value; zero-based `HEEP2[13]` reads `05` | medium |

## Notes

- `HGEN` is the key proof-backed block for today / month / year / total PV generation. Daily and monthly reset behavior is documented in [HGEN_ROLLOVER.md](HGEN_ROLLOVER.md).
- `QPRTL` is the best current match for the `HPVINV02` device-type block. The 2026-04-30 active replacement boot captures showed the first identity command after boot can time out once while the bus wakes. The firmware now retries read-only active commands once on timeout.
- `HIMSG1` matches the version/date block used by the portal.
- `HPV` voltage/current/power are now confirmed under daytime production on the active Atom S3 Lite replacement path. In the same Home Assistant recorder snapshot, `pv_power` was live at `569 W` while the token-4 `generation_power_kw` entity stayed stale/zero, so token 4 is diagnostic-only until a better field match is proven.
- `HBMS1`, `HBMS2`, and `HBMS3` decode structurally, but they represent the inverter's PYL/BMS summary view rather than the direct BMS protocol. `HBMS1` SOC/current now match the independent BMS node after rounding; `HBMS2` is useful as a min/max cell summary; `HBMS3` remains low-value on this setup because the inverter returns zeroed cell slots while the independent BMS node reports real cells.
- `HEEP1` and `HEEP2` are configuration snapshots that overlap the portal settings cache, but the token order is still not proven. Keep them diagnostic-only.
- The main open work is the exact split of `HEEP1` token 4 and the meaning of `HEEP2` token 17.
- The current HEEP alignment report pins `HEEP1` token 12 `025` to `BMS Returns To Mains Mode SOC` as a unique exact match. It also gives strong candidates for `HEEP2` tokens 5, 6, 8, 10, 14, and 16, but these remain diagnostic until one-setting deltas prove exact positions.
- The live `Buzzer On` write probe is the first confirmed portal write that maps back to the inverter-side config stream: `PDa` disables it, `PEa` enables it, and `HEEP1` token 6 flips accordingly.
- `Backlight On` is now the second confirmed portal write mapping. The bus accepts ``PDx`Y`` to disable and `PExSh` to enable, both return `(ACK9 `, and the same HEEP1 token 6 flips `1 -> 0 -> 1`.
- `Display Automatically Returns To Homepage` has now been re-validated in a live safe round-trip. The disable frame on the wire is `50 44 6B 42 0B 0D` and the restore frame is `50 45 6B 71 3A 0D`; both return `(ACK9 `, and zero-based `HEEP1[4]` flips `012 -> 002 -> 012`.
- `Input Source Detection Prompt Sound` is now the third confirmed portal write mapping. The inverter accepts `PDypx` to disable and `PEyCI` to enable, both return `(ACK9 `, and the current live capture does not show a distinct HEEP1 delta for that row.
- `Battery Equalization Interval Setting` is now readback-confirmed as `PBEQP<DDD>`. The latest ESPHome web API loop used `PBEQP031` and `PBEQP030` for a one-day delta, both returned `(ACK9 `, and zero-based `HEEP2[10]` changed `030 -> 031 -> 030`.
- `Battery Equalization Timeout Setting` is now readback-confirmed as `PBEQOT<DDD>`. The latest ESPHome web API loop used `PBEQOT119` and `PBEQOT120`, both returned `(ACK9 `, and zero-based `HEEP2[9]` changed `120 -> 119 -> 120`.
- `Battery Equalization Time Setting` is now readback-confirmed as `PBEQT<DDD>`. The latest ESPHome web API loop used `PBEQT059` and `PBEQT060`, both returned `(ACK9 `, and zero-based `HEEP2[8]` changed `060 -> 059 -> 060`.
- `BMS Lock Machine Battery Capacity (%)` is now bus-confirmed as `BMSSDC<DDD>`. The live capture used `BMSSDC021Xe` and `BMSSDC020HD` for a one-step delta, the former returned `NAKss` and the latter returned `(ACK9 `, so keep the row disabled by default until the accepted range is proven.
- `Battery Equalization Mode Enable Setting` is now bus-confirmed as a binary row with live wire forms `PBEQE1` and `PBEQE0Z2`. The portal snapshot renders the row as `Off` / `On`, both directions returned `(ACK9 `, and the live capture did not show a distinct HEEP delta.
- `Battery Equalization Voltage Setting` is now readback-confirmed. The latest ESPHome web API loop used `PBEQV58.30` and `PBEQV58.40`, both return `(ACK9 `, and zero-based `HEEP2[7]` changed `058.4 -> 058.3 -> 058.4`.
- `Grid Connected Current Setting` is now readback-confirmed as `PGFC<DDD>`. The latest ESPHome web API loop used `PGFC021` and `PGFC020`, both returned `(ACK9 `, and zero-based `HEEP1[17]` changed `020 -> 021 -> 020`.
- `Maximum Mains Charging Current Setting` is now readback-confirmed as `MUCHGC<DDD>`. The live captures used `MUCHGC020`, `MUCHGC030`, and `MUCHGC040`; the latest ESPHome web API loop proved zero-based `HEEP1[2]` changes and restored the inverter to `030`.
- `Maximum Charging Current Setting` is now readback-confirmed as `MNCHGC<DDD>`. The live captures used `MNCHGC050`, `MNCHGC060`, `MNCHGC070`, and `MNCHGC080`; the latest ESPHome web API loop proved zero-based `HEEP1[1]` changes and restored the inverter to `070`.
- `Battery Recharge Voltage Setting` is now readback-confirmed as `PBCV<XX.X>`. The latest ESPHome web API loop used `PBCV45.0` and `PBCV46.0`, both returned `(ACK9 `, and zero-based `HEEP2[4]` changed `046.0 -> 045.0 -> 046.0`.
- `Battery Redischarge Voltage Setting` is now readback-confirmed as `PBDV<XX.X>`. The latest ESPHome web API loop used `PBDV53.0` and `PBDV54.0`, both returned `(ACK9 `, and zero-based `HEEP2[5]` changed `054.0 -> 053.0 -> 054.0`.
- `Battery Float Charging Voltage Setting` is now bus-observed as `PBFT<XX.X>`. The live capture used `PBFT56.5`, `PBFT56.4`, and `PBFT56.3`, all returned `NAKss`, so keep it research-only until the accepted range or mode is proven.
- `PV Energy Feeding Priority Setting` is now bus-observed as `PVENGUSE<00>`. The live capture paired `PVENGUSE00^a` with `BLU` and `PVENGUSE01N@` with `LBU`; both returned `(ACK9 `, and the current evidence suggests `PVENGUSE00` = `BLU` and `PVENGUSE01` = `LBU`.
- `Battery Constant Charging Voltage Setting` remains portal-only on this firmware. The legacy `PCVV<XX.X>` family is not bus-confirmed in the current capture set and should stay unsupported.
- The PowMr-specific reverse-engineering repo confirms the original dongle polls the inverter at 2400 baud, 8N1, slave id 5, using two windows: 4501-4545 and 4546-4561. That second window extends beyond the published register map, so the unresolved total counters are more likely to sit in that tail range than in the generic PI30 command set.
- Treat the long head tokens as packed composite blocks rather than ordinary scalar fields.
- `HPVB` is the current best match for live PV / bus telemetry and now decodes as a medium-confidence block. The 2026-05-01 Atom S3 Lite OTA run captured it actively as `(000.0 00.0 00000 0 380.0 00000000000000000000000`; daytime validation is still needed for nonzero PV values.
- The latest portal cross-check shows that the station-overview generation values can drift relative to the direct `HGEN` frame; use same-snapshot portal data when validating totals.
