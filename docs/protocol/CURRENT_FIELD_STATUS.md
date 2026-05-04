# Current Field Status

This is the canonical confidence split for the public read-only v1 release. It is intentionally shorter than the full command table.

## Production Read Telemetry

These commands are supported by the v1 ESPHome component as normal read-only telemetry on the confirmed HPVINV02 setup:

| Command | Purpose | Confidence | Entity policy |
|---|---|---|---|
| `HSTS` | status, mode, status bits, fault bits | high for status fields; medium for less common mode labels | production |
| `HGRID` | AC input voltage/frequency and mains power | high | production |
| `HOP` | AC output voltage/frequency/power/load | high | production |
| `HBAT` | inverter battery summary | high | production |
| `HPV` | PV input summary | high for observed fields; needs daytime validation for nonzero PV edge cases | production |
| `HTEMP` | inverter temperatures and fan speeds | high | production |
| `HGEN` | daily/monthly/yearly/total PV generation counters | high | production |
| `QPRTL` | device/protocol type | medium | production text sensor |
| `HIMSG1` | software version/date/revision | high | production text sensor |

`HGEN` is the proof-backed local serial source for native PV generation counters. Cloud or station totals are useful for comparison, but they are not proof of local RS232 support.

The current research write surface also includes a bus-observed cutoff-voltage family:

- `batteryCutOffVoltageSetting` -> `PSDV<XX.X>`
- latest live probe: `PSDV41.0`
- response: `NAKss`
- interpretation: the family is present on the bus and HEEP1 token 17 still points at the cutoff row, but the tested `41.0 V` delta was rejected

The current research write surface also includes a bus-confirmed grid-current family:

- `gridConnectedCurrentSetting` -> `PGFC<DDD>`
- latest live probe: `PGFC021`
- restore probe: `PGFC020`
- response: `(ACK9 `
- interpretation: the family is present on the bus and the one-amp trim round-trip succeeded on this inverter

The current research write surface also includes a bus-confirmed BMS-lock family:

- `bmsLockMachineBatteryCapacitySetting` -> `BMSSDC<DDD>`
- latest live probes: `BMSSDC021Xe` for `21`, `BMSSDC020HD` for `20`
- response: `NAKss` for `21`, `(ACK9 ` for `20`
- interpretation: the family is present on the bus and the current value `20` is accepted on this inverter; `21` was rejected, so keep it disabled by default until the accepted range is mapped

The current research write surface also includes a bus-confirmed BMS-function family:

- `bmsFunctionEnableSetting` -> `BMSC<NN>`
- latest live probes: `BMSC00` for `Off`, `BMSC01` for `On`
- response: `(ACK9 `
- interpretation: the family is present on the bus and the row round-trip succeeded on this inverter

The current research write surface also includes a bus-confirmed maximum-mains-charge family:

- `maximumMainsChargingCurrentSetting` -> `MUCHGC<DDD>`
- latest live probe: `MUCHGC020`
- restore probe: `MUCHGC030`
- response: `(ACK9 `
- interpretation: the family is present on the bus and the trim round-trip to 20 A succeeded on this inverter

The current research write surface also includes a bus-confirmed maximum-charge family:

- `maximumChargingCurrentSetting` -> `MNCHGC<DDD>`
- latest live probe: `MNCHGC050`
- restore probe: `MNCHGC060`
- response: `(ACK9 `
- interpretation: the family is present on the bus and the trim round-trip to 50 A succeeded on this inverter

The current research write surface also includes bus-confirmed recharge/redischarge families:

- `batteryRechargeVoltageSetting` -> `PBCV<XX.X>`
- latest live probe: `PBCV45.0`
- restore probe: `PBCV46.0`
- response: `(ACK9 `
- interpretation: the family is present on the bus and the round-trip succeeded on this inverter

- `batteryRedischargeVoltageSetting` -> `PBDV<XX.X>`
- latest live probe: `PBDV53.0`
- restore probe: `PBDV54.0`
- response: `(ACK9 `
- interpretation: the family is present on the bus and the round-trip succeeded on this inverter

- `batteryTypeSetting` -> `PBT<NN>`
- latest live probes: `PBT03` for `LIA`, `PBT04` for `PYL`
- response: `(ACK9 `
- interpretation: the family is present on the bus and the round-trip succeeded on this inverter; keep the row disabled by default if you need to explore the rest of the type codes

The current research write surface also includes a bus-observed float-voltage family:

- `batteryFloatChargingVoltageSetting` -> `PBFT<XX.X>`
- latest live probes: `PBFT56.5`, `PBFT56.4`, `PBFT56.3`
- response: `(NAKss`
- interpretation: the family is present on the bus, but the tested values were rejected on this inverter

The current research write surface also includes a bus-confirmed PV-energy-feeding family:

- `pvEnergyFeedingPrioritySetting` -> `PVENGUSE00` / `PVENGUSE01`
- latest live probes: `PVENGUSE00^a` for `BLU`, `PVENGUSE01N@` for `LBU`
- response: `(ACK9 `
- interpretation: the family is present on the bus and the current best mapping is `PVENGUSE00` = `BLU`, `PVENGUSE01` = `LBU`

The current research write surface also includes a bus-confirmed charger-priority family:

- `chargerPrioritySetting` -> `PCP00` / `PCP01` / `PCP02`
- latest live probes: `PCP01` for the portal change `OSO -> SNU`, `PCP02` for the restore `SNU -> OSO`, `PCP00` for the change `OSO -> CSO`, and `PCP02` again for the restore `CSO -> OSO`
- response: `(ACK9 `
- interpretation: the family is present on the bus and the select-state mapping is now closed: `PCP00` = `CSO`, `PCP01` = `SNU`, `PCP02` = `OSO`. Keep it diagnostic/default-disabled because it is a topology control, and note that `PCP02` is shared by both restore windows

The low-risk queue is exhausted on this firmware. The following rows were probed and rejected with `Command not supported`, so they should stay out of the lab queue:

- `lowBatteryAlarmVoltageSetting`
- `parallelShutdownBatteryVoltageSetting`
- `restoreBatteryDischargingBatteryCapacity`
- `restoreMainsChargingBatteryCapacity`
- `secondOutputDischargeTimeSetting`

`Output Source Priority Setting` now has a live-observed `POP` write family. The raw `POP00` frame is pinned; the current best mapping is `POP00` = `SUB priority`, `POP01` = `SBU priority`, `POP02` = `Utility first (legacy)`.

The remaining topology/source-priority controls stay out of the production entity set even when bus-confirmed.

## Active Replacement Recorder Evidence

The current field checks should use Home Assistant recorder history while the Atom S3 Lite replacement is installed. The original Solar of Things dongle is not connected in this mode, so the cloud portal is not a same-time comparison source.

Latest recorder/API-log snapshot from the replacement path on 2026-05-01:

| Field | Latest value | Interpretation |
|---|---:|---|
| `ac_input_voltage` | `239.6 V` | live `HGRID` input voltage |
| `mains_power` | `484 W` | live `HGRID` mains power |
| `pv_voltage` | `75.4 V` | daytime `HPV` voltage now observed nonzero |
| `pv_current` | `10.0 A` | daytime `HPV` current now observed nonzero |
| `pv_power` | `756 W` | daytime `HPV` power now observed nonzero |
| `pv_generation_power` | stale/zero | HPV token 4 did not track nonzero PV production; keep diagnostic |
| `daily_power_gen` | `2.348 kWh` | live `HGEN` daily counter |
| `monthly_electricity_generation` | `2.3 kWh` | live `HGEN` monthly counter after May reset |
| `yearly_electricity_generation` | `71.3 kWh` | live `HGEN` yearly counter, continued across month boundary |
| `total_power_generation` | `71.3 kWh` | live `HGEN` lifetime-style counter |

This promotes the basic daytime `HPV` token mapping for voltage/current/power to high confidence on the confirmed setup. HPV token 4, previously labeled generation power, did not track nonzero PV production in the Home Assistant recorder snapshot and remains diagnostic. The remaining `HPV` raw-tail tokens and `HPVB` still need separate validation.

## Diagnostic Read Blocks

These commands decode structurally and are useful for debugging, but they stay diagnostic/default-disabled for v1:

| Command | Current interpretation | Confidence | Entity policy |
|---|---|---|---|
| `HBMS1` | inverter-side BMS summary: SOC, current, charge/discharge limits | medium-high for SOC/current; medium for flags/limits | diagnostic |
| `HBMS2` | inverter-side BMS min/max voltage summary and positions | medium for min/max summary; low for capacity placeholders | diagnostic |
| `HBMS3` | 16 cell-voltage slots plus tail | low on this setup because slots have been observed zeroed while the independent BMS node reports real cells | diagnostic |
| `HPVB` | PV/bus auxiliary block | medium | diagnostic |
| `HEEP1` | configuration snapshot, packed head unresolved | low | diagnostic |
| `HEEP2` | configuration snapshot, packed head unresolved | low | diagnostic |

The independent BMS node should remain the authority for direct BMS telemetry and full cell values. The inverter H-path appears to expose only a summarized or bridged PYL/BMS view.

## Remaining Gaps

The remaining protocol gaps are narrow:

- `HEEP1` packed head tokens, especially long composite tokens.
- `HEEP2` packed head tokens, especially long composite tokens.
- One isolated `HPVB` active read under known conditions.
- Daytime `HPVB` validation while PV is producing.
- Same-time `HBMS1` / `HBMS2` comparison against the independent BMS node.
- `HGEN` yearly reset behavior. Daily and monthly resets are now confirmed; see [HGEN_ROLLOVER.md](HGEN_ROLLOVER.md).

Current strongest unresolved scalar values:

| Command | Token | Value | Best current match | Status |
|---|---:|---|---|---|
| `HEEP1` | 4 | `03410110230` | packed composite value; observed variants `03410010230`, `03410100230` | unresolved |
| `HEEP1` | 5 | `012` | `bmsReturnsToMainsModeSOC` / display / mode-related rows | ambiguous |
| `HEEP2` | 17 | `50000` | packed composite value | unresolved |

Keep these packed values diagnostic-only until isolated write/readback deltas prove exact semantics.

Live attach note:

- A 2026-05-02 sniffer attach observed a provisional `HEEP1` token 4 value of `03310010230` while charger priority remained restored to `OSO`. Keep that value provisional until it is folded into the corpus-scanned head profile.

## Research-Only Writes

The project has bus-confirmed write-family evidence, documented in [WRITE_SURFACE.md](WRITE_SURFACE.md). These writes are excluded from the public read-only v1 examples. A separate beta write example exists for controlled local testing only:

- `PD*`
- `PE*`
- `PBEQE1` / `PBEQE0Z2`
- `PBEQV*`
- `PCVV*`
- `PBCV*`
- `PBDV*`
- `PSDV*`
- `batteryFloatChargingVoltageSetting`
- `PBEQP*`
- `PBEQOT*`
- `PBEQT*`
- `PDSRS*`
- `PDDLYT*`
- `PGFC*`
- `MNCHGC*`
- `MUCHGC*`
- `PBFT*`
- `^S???DAT*`
- `PCPxx`

Keep write support out of production until readback/restore evidence is repeatable and documented for each entity.

## Next Safe Probes

The only near-term live probes recommended before a public tag are:

- isolated `HPVB\r` active read with the Solar Plug disconnected;
- daytime `HPVB\r` under real PV production;
- same-time `HTEMP\r` comparison with portal details;
- same-time `HBMS1` / `HBMS2` comparison with the independent BMS node;
- `HGEN\r` observation across the yearly reset boundary.
