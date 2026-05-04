# Write Surface

This is the canonical public summary of the writable Solar Plug surface observed during local bus research.

Writes are documented for protocol completeness. The default v1 ESPHome replacement build remains read-only. A separate lab build can expose a small opt-in write surface for controlled validation on a real inverter.

Response frames begin with `(`. Raw ACK/NAK frames include response CRC bytes before `0D`: `(ACK9 ` decodes as payload `(ACK` plus CRC `39 20`, and `(NAKss` decodes as payload `(NAK` plus CRC `73 73`.

## Bus-Confirmed Families

| Portal label | Bus family | Observed response | Current status |
|---|---|---|---|
| Buzzer On | `PDa` / `PEa` | `(ACK9 ` | confirmed |
| Backlight On | ``PDx`Y`` / `PExSh` | `(ACK9 ` | confirmed |
| Display Automatically Returns To Homepage | `PDk` / `PEkq:` | `(ACK9 ` | confirmed |
| Over Temperature Automatic Restart | `PDv` / `PEv` | `(ACK9 ` | confirmed |
| Overload Automatic Restart | `PDu` / `PEu` | `(ACK9 ` | confirmed |
| Input Source Detection Prompt Sound | `PDypx` / `PEyCI` | `(ACK9 ` | confirmed |
| Inverter System Clock | `^S???DAT<YYMMDDHHMMSS>` | `^1` | confirmed |
| Battery Equalization Voltage Setting | `PBEQV<XX.XX>` | `(ACK9 ` | readback-confirmed on `HEEP2[7]` |
| Battery Recharge Voltage Setting | `PBCV<XX.X>` | `(ACK9 ` | readback-confirmed on `HEEP2[4]` |
| Battery Redischarge Voltage Setting | `PBDV<XX.X>` | `(ACK9 ` | readback-confirmed on `HEEP2[5]` |
| Battery Type Setting | `PBT<NN>` | `(ACK9 ` | confirmed; `03` accepted as `LIA` and `04` accepted as `PYL` |
| BMS Function Enable Setting | `BMSC<NN>` | `(ACK9 ` | confirmed; `00` accepted as `Off` and `01` accepted as `On` |
| Battery Equalization Interval Setting | `PBEQP<DDD>` | `(ACK9 ` | readback-confirmed on `HEEP2[10]` |
| Battery Equalization Timeout Setting | `PBEQOT<DDD>` | `(ACK9 ` | readback-confirmed on `HEEP2[9]` |
| Battery Cut Off Voltage Setting | `PSDV<XX.X>` | `(NAKss` | bus frame observed; tested 41.0 V delta rejected |
| BMS Lock Machine Battery Capacity (%) | `BMSSDC<DDD>` | `(ACK9 ` / `(NAKss` | bus-confirmed; `20` accepted and `21` rejected on this firmware; disabled by default until the accepted range is mapped |
| Grid Connected Current Setting | `PGFC<DDD>` | `(ACK9 ` | readback-confirmed on `HEEP1[17]` |
| Maximum Mains Charging Current Setting | `MUCHGC<DDD>` | `(ACK9 ` | readback-confirmed on `HEEP1[2]` |
| Battery Equalization Time Setting | `PBEQT<DDD>` | `(ACK9 ` | readback-confirmed on `HEEP2[8]` |
| Battery Equalization Mode Enable Setting | `PBEQE1` / `PBEQE0Z2` | `(ACK9 ` | confirmed |
| Maximum Charging Current Setting | `MNCHGC<DDD>` | `(ACK9 ` | readback-confirmed on `HEEP1[1]` |
| Output Source Priority Setting | `POP00` / `POP01` / `POP02` | `(ACK9 ` | live write family; `POP00` raw frame is pinned, and the current best mapping is `POP00` = `SUB priority`, `POP01` = `SBU priority`, `POP02` = `Utility first (legacy)` |
| Charger Priority Setting | `PCP00` / `PCP01` / `PCP02` | `(ACK9 ` | bus-confirmed; latest live round-trip captured `PCP01` on `OSO -> SNU`, `PCP02` on `SNU -> OSO`, `PCP00` on `OSO -> CSO`, and `PCP02` again on `CSO -> OSO`; current best mapping is `PCP00` = `CSO`, `PCP01` = `SNU`, `PCP02` = `OSO`, and `PCP02` is shared by both restore windows; `PCP03` remains unconfirmed |
| PV Energy Feeding Priority Setting | `PVENGUSE00` / `PVENGUSE01` | `(ACK9 ` | live write family; current best mapping is `PVENGUSE00` = `BLU`, `PVENGUSE01` = `LBU`; `PVENGUSE02` remains unconfirmed |
| Restore Second Output Battery Capacity Setting | `PDSRS<DDD>` | `ACK` / `NAK` on range probes | family shape confirmed |
| Restore Second Output Delay Time Setting | `PDDLYT<DDD>` | `ACK` / `NAK` on range probes | family shape confirmed |

## Bus-Observed Low-Risk Families

These rows have live wire evidence, but the tested values are not yet a clean accepted round-trip:

| Portal label | Bus family | Observed response | Current status |
|---|---|---|---|
| Battery Float Charging Voltage Setting | `PBFT<XX.X>` | `(NAKss` | bus frame observed; tested `56.5 V`, `56.4 V`, and `56.3 V` deltas rejected |
The same write surface also includes the select families above. The current best mapping is now stable enough to expose in the beta firmware:

- `chargerPrioritySetting` -> `PCP00` / `PCP01` / `PCP02`
- `outputSourcePrioritySetting` -> `POP00` / `POP01` / `POP02`
- `pvEnergyFeedingPrioritySetting` -> `PVENGUSE00` / `PVENGUSE01`

## Portal Rows Not Productized

These rows are useful research targets but are not production controls:

- Battery Constant Charging Voltage Setting

The legacy pipsolar family for this row is `PCVV<XX.X>`, but the current portal write path on this firmware returns `Command not supported`, so it remains portal-only until a live bus round-trip is proven.

The low-risk queue is now exhausted on this firmware. The following portal rows were probed and rejected with `Command not supported`, so they should stay out of the lab queue:

- `lowBatteryAlarmVoltageSetting`
- `parallelShutdownBatteryVoltageSetting`
- `restoreBatteryDischargingBatteryCapacity`
- `restoreMainsChargingBatteryCapacity`
- `secondOutputDischargeTimeSetting`

Grouped from the portal matrix, the still-unpromoted rows are:

- BMS Lock Machine Battery Capacity (%)
- Grid Connection Function Enable Setting
- Grid Connection Protocol Type Setting
- Grid Working Range Setting
- INV Dual Output Time
- Output Model Setting
- Overload To Bypass Operation
- Parallel Mode Setting
- Rated Frequency Setting
- Rated Voltage Setting

These remaining rows are topology or source-priority controls and are passive-only for this pass.

`batteryRechargeVoltageSetting` and `batteryRedischargeVoltageSetting` are now promoted on this firmware and map to `PBCV<XX.X>` / `PBDV<XX.X>`.

`POP` is now a live-observed write family for `Output Source Priority Setting`. The current best mapping is `POP00` = `SUB priority`, `POP01` = `SBU priority`, and `POP02` = `Utility first (legacy)`. The raw `POP00` frame is pinned; keep `POP01` and `POP02` paired with the decoder until a separate isolated raw replay is captured for each one.

`PVENGUSE` is now a live-observed write family for `PV Energy Feeding Priority Setting`. The live bus capture paired `PVENGUSE00^a` with `BLU` and `PVENGUSE01N@` with `LBU`; both returned `(ACK9 `, and the current best mapping is `PVENGUSE00` = `BLU` and `PVENGUSE01` = `LBU`. Keep the family label and mapping together in the decoder until a cleaner raw-byte replay is written out.

## ESPHome Beta Write Surface

The ESPHome component will not create write entities unless both conditions are true:

- `enable_writes: true`
- `passive_mode: false`
- `write_profile: beta` for bus-confirmed controls, or `write_profile: unsafe` for NAK-only / unsupported research probes

The beta write surface intentionally has no arbitrary command sender. Each entity maps to one bus-confirmed encoder:

| Entity key | ESPHome domain | Encoded frame |
|---|---|---|
| `buzzer` | switch | off `PDa`; on `PEa` |
| `backlight` | switch | off ``PDx`Y``; on `PExSh` |
| `display_return_homepage` | switch | off `PDk`; on `PEkq:` |
| `over_temperature_restart` | switch | off `PDv`; on `PEv` |
| `overload_restart` | switch | off `PDu`; on `PEu` |
| `input_source_detection_prompt_sound` | switch | off `PDypx`; on `PEyCI` |
| `battery_equalization_voltage` | number | `PBEQV<XX.XX>`; range `48.0`-`60.0 V` |
| `battery_recharge_voltage` | number | `PBCV<XX.X>`; observed `45.0` / `46.0` round-trip on live bus |
| `battery_redischarge_voltage` | number | `PBDV<XX.X>`; observed `53.0` / `54.0` round-trip on live bus |
| `battery_equalization_interval` | number | `PBEQP<DDD>`; manual range `0`-`90 days`; 1-day step |
| `battery_equalization_timeout` | number | `PBEQOT<DDD>`; manual range `5`-`900 min`; 5-minute step |
| `grid_connected_current` | number | `PGFC<DDD>`; observed `20 A` and `21 A` in the live round-trip |
| `maximum_mains_charging_current` | number | `MUCHGC<DDD>`; observed `20 A` and `30 A` in the live round-trip |
| `maximum_charging_current` | number | `MNCHGC<DDD>`; manual model maximum `120 A`; observed `50 A`, `60 A`, `70 A`, and `80 A` in live round-trips |
| `battery_equalization_time` | number | `PBEQT<DDD>`; manual range `5`-`900 min`; 5-minute step |
| `battery_equalization_mode_enable` | switch | off `PBEQE0Z2`; on `PBEQE1` |
| `restore_second_output_battery_capacity` | number | `PDSRS<DDD>`; range `0`-`50 %` |
| `restore_second_output_delay_time` | number | `PDDLYT<DDD>`; manual range `0`-`60 min`; 5-minute step; `4 min` was rejected on this firmware |
| `charger_priority` | select | `PCP00` = `CSO`, `PCP01` = `SNU`, `PCP02` = `OSO` |
| `output_source_priority` | select | `POP00` = `SUB priority`, `POP01` = `SBU priority`, `POP02` = `Utility first (legacy)` |
| `pv_energy_feeding_priority` | select | `PVENGUSE00` = `BLU`, `PVENGUSE01` = `LBU` |

The beta write firmware publishes the requested switch/number/select state after a write is accepted into the command queue, and then exposes the actual bus result through `last_write_status`. Treat the requested state as optimistic until `last_write_status` reports `ack` and a fresh readback poll confirms the relevant `HEEP1` or `HEEP2` token. Use one change at a time and restore values after each probe.

## Manual Cross-Check

The local POW-HVM6.2KP manual supports the tighter beta ranges used by the component:

| Manual program | Manual setting | Component entity | Manual constraint used |
|---:|---|---|---|
| 02 | Maximum charging current | `maximum_charging_current` | 10 A steps up to the model maximum charging current; model rating lists `120 A`. |
| 11 | Maximum utility charging current | `maximum_mains_charging_current` | `2 A`, then 10 A steps up to max AC charging current; the live inverter option list currently tops out at `60 A`. |
| 12 | Back to utility voltage | `battery_recharge_voltage` | `44 V`-`51 V`, 1 V step. |
| 13 | Back to battery voltage | `battery_redischarge_voltage` | full-charge option or `48 V`-`58 V`, 1 V step; component keeps `0` as the full-charge sentinel. |
| 31 | Battery equalization voltage | `battery_equalization_voltage` | `48.0 V`-`60.0 V`, 0.1 V step. |
| 33 | Battery equalized time | `battery_equalization_time` | `5`-`900 min`, 5-minute step. |
| 34 | Battery equalized timeout | `battery_equalization_timeout` | `5`-`900 min`, 5-minute step. |
| 35 | Equalization interval | `battery_equalization_interval` | `0`-`90 days`, 1-day step. |
| 56 | GRID-tie current | `grid_connected_current` | 1 A step; current tested value restored to `20 A`. |
| 66 | Dual Recover Delay Time | `restore_second_output_delay_time` | `0`-`60 min`; `PDDLYT004` NAK matches the manual's 5-minute stepping behavior. |

NAK-only or unsupported rows are excluded from the normal beta profile. The component still keeps explicit unsafe encoders for focused research, but `battery_cut_off_voltage`, `battery_float_voltage`, and `battery_bulk_voltage` require `write_profile: unsafe` and should not be enabled on the normal beta firmware.

Live decoder verification on 2026-05-01 confirmed the `display_return_homepage` disable wire form as `50 44 6B 42 0B 0D` and the restore wire form as `50 45 6B 71 3A 0D`, both with `(ACK9 ` responses.
Live decoder verification on 2026-05-01 also confirmed the binary equalization mode row: `PBEQE1` on the wire as `50 42 45 51 45 31 4A 13 0D` and `PBEQE0Z2` as `50 42 45 51 45 30 5A 32 0D`, both with `(ACK9 ` responses. The portal snapshot renders this row as `Off` / `On`.

## Safety Policy

The production examples exclude all write families:

- `PD*`
- `PE*`
- `PBEQV*`
- `PBCV*`
- `PBDV*`
- `PCVV*`
- `PBFT*`
- `PSDV*`
- `PDSRS*`
- `PBEQOT*`
- `PBEQT*`
- `PDDLYT*`
- `^S???DAT*`
- `PCPxx`
- `POPxx`
- `PVENGUSExx`
- unknown `P*` setters

Write support should stay opt-in and beta-scoped until each control has repeated ACK/restore evidence and a documented readback path across more inverter units.

## Related Docs

- [H_COMMANDS.md](H_COMMANDS.md)
- [CURRENT_FIELD_STATUS.md](CURRENT_FIELD_STATUS.md)
- [WRITE_TEST_LEDGER.md](WRITE_TEST_LEDGER.md)
- [REPLAY_WORKFLOW.md](REPLAY_WORKFLOW.md)
