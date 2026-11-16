# Write Surface

This file keeps the bus-capture history of every setter family and the reasons for the unsafe profile. The per-control reference (frame, range, readback slot, evidence class, default state) is [HEEP_SETTINGS_MAP.md](HEEP_SETTINGS_MAP.md), and [WRITE_TEST_LEDGER.md](WRITE_TEST_LEDGER.md) records each round-trip.

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
| Output Source Priority Setting | `POP00` / `POP01` | `(ACK9 ` | `POP00` = `SUB priority` (captured on the wire when the portal moved SBU→SUB). `POP01` = `SBU priority` is inferred from the code order and has **not** been captured on the wire. Readback: `QPIRI[16]` (and `QOP`) read `1` on 2026-04-25, and the portal showed SBU on 2026-04-29. `POP02` is not offered by the manual and remains unconfirmed |
| Charger Priority Setting | `PCP00` / `PCP01` / `PCP02` | `(ACK9 ` | bus-confirmed; latest live round-trip captured `PCP01` on `OSO -> SNU`, `PCP02` on `SNU -> OSO`, `PCP00` on `OSO -> CSO`, and `PCP02` again on `CSO -> OSO`; `PCP00` = `CSO`, `PCP01` = `SNU`, `PCP02` = `OSO`, readback-confirmed on `HEEP1[4]` last digit and `QPIRI[17]` (`PCP00` → `010`, `PCP02` → `012` on 2026-11-16); `PCP03` remains unconfirmed |
| PV Energy Feeding Priority Setting | `PVENGUSE00` / `PVENGUSE01` | `(ACK9 ` | readback-confirmed on `HEEP1[3]` digit 3: `PVENGUSE00` = `BLU`, `PVENGUSE01` = `LBU`; `PVENGUSE02` remains unconfirmed |
| Restore Second Output Battery Capacity Setting | `PDSRS<DDD>` | `(ACK9 ` | readback-confirmed on `HEEP2[16]` d0–1 (`30000 → 35000 → 30000`); multiples of 5 only |
| Restore Second Output Delay Time Setting | `PDDLYT<DDD>` | `ACK` / `NAK` on range probes | `005` ACK, `004`/`006` NAK; `HEEP2[13]` matches the stored value |
| SOC Back To Mains / Battery, Inverter Restart SOC (programs 39–41) | `BMSB2UC` / `BMSU2BC` / `BMSSRC<DDD>` | `(ACK9 ` | readback-confirmed on `HEEP1[11]`/`[12]`/`[13]` (2026-11-16) |
| Second Output Cut-off SOC (program 62) | `PDSDS<DDD>` | `(ACK9 ` | readback-confirmed on `HEEP2[2]` (2026-11-16) |
| Low Battery Alarm V, Second Output Cut-off / Recovery V (programs 24, 61, 63) | `PSLV` / `PDSDV` / `PDSRV<XX.X>` | `(NAKss` | NAK even at the current value with a lithium battery type (2026-11-16); unsafe profile only |

## Bus-Observed Rejections

| Portal label | Bus family | Observed response | Current status |
|---|---|---|---|
| Battery Float Charging Voltage Setting | `PBFT<XX.X>` | `(NAKss` | tested `56.5 V`, `56.4 V` and `56.3 V` rejected; unsafe profile only |
| Battery Constant Charging Voltage Setting | `PCVV<XX.X>` | — | never sent on the bus; unsafe profile only |

## Portal Rows and the Cloud

The Solar of Things cloud answers `Command not supported` for several rows that the firmware accepts on the bus, so the limit is in the cloud. Programs 39–41, 62 and 65 work (`BMSB2UC`, `BMSU2BC`, `BMSSRC`, `PDSDS`, `PDDCGT<DDDD>`), and 24, 61 and 63 NAK (`PSLV`, `PDSDV`, `PDSRV`).

The portal's topology and grid rows are encodable only in the unsafe profile (table below), from the vendor tool's formats: grid connection, grid protocol, output mode (which also covers parallel and 3-phase operation), rated frequency and rated voltage. The portal's "INV Dual Output Time" most likely matches program 65, the beta `second_output_discharge_time`.

## Gating

- **Entity creation:** write entities exist only with `enable_writes: true`, `passive_mode: false` and `write_profile: beta` or `unsafe`. The `sync_clock` button and the `probe_write` action need only the first two.
- **Encoders:** each entity maps to one fixed encoder in `components/solarplug/solarplug_writes.cpp`. There is no arbitrary command sender; the `probe_write` action accepts only `HOSTCRCEN` / `HOSTCRCDN`.
- **Beta profile:** it holds the controls with bus evidence on this unit. The exceptions are `power_saving` (never sent, disabled by default) and `POP01` (never seen on the wire). The evidence per control is in [HEEP_SETTINGS_MAP.md](HEEP_SETTINGS_MAP.md).

NAK-only, never-sent and output-cutting controls need `write_profile: unsafe`, which config validation enforces. They live in `packages/solarplug-write-unsafe.yaml`, where every entity is disabled by default:

| Entity key | Domain | Frame | Why it is unsafe |
|---|---|---|---|
| `battery_cut_off_voltage`, `battery_bulk_voltage`, `battery_float_voltage` | number | `PSDV` / `PCVV` / `PBFT<XX.X>` | NAK with a lithium battery type (`PCVV` never sent) |
| `low_battery_alarm_voltage`, `second_output_cutoff_voltage`, `second_output_recovery_voltage` | number | `PSLV` / `PDSDV` / `PDSRV<XX.X>` | NAK even at the current value (2026-11-16) |
| `dual_output_mode` | switch | `PDAULC01` / `PDAULC00` | never sent; `HEEP2[0]` polarity unconfirmed (LCD program 60) |
| `second_output_power` | switch | `PDAULON01` / `PDAULON00` | switches the L2 loads immediately; never sent |
| `grid_feed` | switch | `GRIDFEED01` / `GRIDFEED00` | grid export; never sent |
| `ct_function` | switch | `EXTCT01` / `EXTCT00` | needs a CT clamp; never sent |
| `output_frequency` | select | `F50` / `F60` | can damage loads; never sent |
| `output_voltage` | select | `V220` / `V230` / `V240` | never sent |
| `output_mode` | select | `POPM00`–`04` | PAL/3P* on a single unit can stop the output; never sent |
| `grid_protocol` | select | `^S???RS00`–`04` (Mode 1–5) | grid-compliance setting; never sent |
| `ac_charge_start_hour` / `_stop_hour` | number | `^S???ACCT<HH>-<HH>` | vendor format only; readback layout of `HEEP2[11]` tentative |
| `ac_output_start_hour` / `_stop_hour` | number | `^S???ACLT<HH>-<HH>` | can switch the AC output off for part of the day; `HEEP2[12]` tentative |

`^S???` setters may answer `^1` (accepted) or `^0` (rejected); `^0` is reported as `nak`.

## Manual Cross-Check

The POW-HVM6.2KP manual's ranges, as the component enforces them. The parity test checks that every value an entity offers is accepted by its encoder, given a valid ordering against the other SOC thresholds.

| Manual program | Manual setting | Component entity | Constraint |
|---:|---|---|---|
| 02 | Maximum charging current | `maximum_charging_current` (select) | The options are the unit's `QMCHGCR` list, `10`–`80 A` (the manual's `120 A` is the model-family figure). |
| 11 | Maximum utility charging current | `maximum_mains_charging_current` (select) | `2 A`, then `10`–`60 A` in 10 A steps (`QMUCHGCR`). |
| 12 | Back to utility voltage | `battery_recharge_voltage` | `44`–`51 V`, 1 V step. |
| 13 | Back to battery voltage | `battery_redischarge_voltage` (select) | `Full` (sent as `PBDV00.0`) or `48`–`58 V`. |
| 31 | Battery equalization voltage | `battery_equalization_voltage` | `48.0`–`60.0 V`. |
| 33 / 34 | Equalized time / timeout | `battery_equalization_time` / `_timeout` | `5`–`900 min`, 5-minute step. |
| 35 | Equalization interval | `battery_equalization_interval` | `0`–`90 days`. |
| 56 | GRID-tie current | `grid_connected_current` | 1 A step; capped at 30 A near the 26.9 A rating. |
| 66 | Dual recover delay | `restore_second_output_delay_time` | `0`–`60 min`, 5-minute step (`PDDLYT004` and `006` NAK). |

Both charge currents and the back-to-battery voltage were sliders that offered values the inverter rejects. They are selects now. The charge-current options are rebuilt from the inverter's own lists once two identical replies agree, and stored in flash. A list can only narrow the model envelope.

## Wire Notes

- On 2026-05-01, `display_return_homepage` was sent as `50 44 6B 42 0B 0D` (off) and `50 45 6B 71 3A 0D` (on); both were answered `(ACK9 `.
- `PBEQE1` goes on the wire as `50 42 45 51 45 31 4A 13 0D` and `PBEQE0` as `50 42 45 51 45 30 5A 32 0D`.

## Examples and Profiles

| Example | Package | Sends |
|---|---|---|
| `minimal-passive-sniffer.yaml` | passive sniffer | nothing |
| `active-reader.yaml` | minimal reader | read queries only |
| `atom-s3-lite-replacement.yaml` | full reader | read queries plus the `HOSTCRCEN` keep-alive (`response_crc: true`), no setters |
| `atom-s3-lite-write-beta.yaml` | full reader + write beta | the above plus the beta setters |

## Related Docs

- [HEEP_SETTINGS_MAP.md](HEEP_SETTINGS_MAP.md)
- [H_COMMANDS.md](H_COMMANDS.md)
- [WRITE_TEST_LEDGER.md](WRITE_TEST_LEDGER.md)
