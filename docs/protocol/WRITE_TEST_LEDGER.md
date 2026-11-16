# Write Test Ledger

This ledger records controlled write/readback tests on the confirmed PowMr / MrPow `POW-HVM6.2KP` inverter reporting `HPVINV02`, `VMII-6200`, and firmware/software `40.05`.

The goal is to separate three evidence levels:

- `readback-confirmed`: the write was ACKed, a known `HEEP1` or `HEEP2` token changed, and the original value was restored and read back.
- `ack-only`: the write was ACKed, but no isolated readback token has been proven yet.
- `rejected`: the inverter returned NAK and the readback stayed at the original value.

Token indexes below are zero-based after stripping the leading `(` and splitting the payload by spaces. Example: `HEEP1` payload `1 070 030 ...` has token `[0] = 1` and token `[1] = 070`.

## 2026-05-04 ESPHome Web API Loop

Test method:

1. Stream live logs with `esphome logs` over the encrypted ESPHome API.
2. Send one control write through the ESPHome web API.
3. Wait for `Last Write Status` to report `ack`, `nak`, or timeout.
4. Press `Refresh Identity Poll` (at the time the settings snapshots were in the identity tier; they now have a 5-minute settings tier and are re-read automatically after every write).
5. Verify the relevant `HEEP1 Raw Payload` or `HEEP2 Raw Payload`.
6. Restore the original value immediately when the write changed inverter state.

No secrets, local file paths, or private network addresses are required to reproduce the method; use the device URL and credentials from the local ESPHome deployment.

### Readback-Confirmed Writes

| Control | Frame tested | Change | ACK status | Readback proof | Restore frame | Final value |
|---|---:|---|---|---|---:|---|
| Display Automatically Returns To Homepage | `PDk` | `012 -> 002` | `ack PDk` | `HEEP1[4]` changed `012 -> 002` | `PEkq:` | `HEEP1[4] = 012` |
| Battery Recharge Voltage Setting | `PBCV45.0` | `046.0 -> 045.0` | `ack PBCV45.0` | `HEEP2[4]` changed `046.0 -> 045.0` | `PBCV46.0` | `HEEP2[4] = 046.0` |
| Battery Redischarge Voltage Setting | `PBDV53.0` | `054.0 -> 053.0` | `ack PBDV53.0` | `HEEP2[5]` changed `054.0 -> 053.0` | `PBDV54.0` | `HEEP2[5] = 054.0` |
| Battery Equalization Voltage Setting | `PBEQV58.30` | `058.4 -> 058.3` | `ack PBEQV58.30` | `HEEP2[7]` changed `058.4 -> 058.3` | `PBEQV58.40` | `HEEP2[7] = 058.4` |
| Battery Equalization Time Setting | `PBEQT059` | `060 -> 059` | `ack PBEQT059` | `HEEP2[8]` changed `060 -> 059` | `PBEQT060` | `HEEP2[8] = 060` |
| Battery Equalization Timeout Setting | `PBEQOT119` | `120 -> 119` | `ack PBEQOT119` | `HEEP2[9]` changed `120 -> 119` | `PBEQOT120` | `HEEP2[9] = 120` |
| Battery Equalization Interval Setting | `PBEQP031` | `030 -> 031` | `ack PBEQP031` | `HEEP2[10]` changed `030 -> 031` | `PBEQP030` | `HEEP2[10] = 030` |
| Grid Connected Current Setting | `PGFC021` | `020 -> 021` | `ack PGFC021` | `HEEP1[17]` changed `020 -> 021` | `PGFC020` | `HEEP1[17] = 020` |
| Maximum Mains Charging Current Setting | `MUCHGC040` | `030 -> 040` | `ack MUCHGC040` | `HEEP1[2]` changed `030 -> 040` | `MUCHGC030` | `HEEP1[2] = 030` |
| Maximum Charging Current Setting | `MNCHGC080` | `070 -> 080` | `ack MNCHGC080` | `HEEP1[1]` changed `070 -> 080` | `MNCHGC070` | `HEEP1[1] = 070` |

Additional maximum-charge bring-up from the same session:

| Control | Frame tested | Change | ACK status | Readback proof | Preservation proof |
|---|---:|---|---|---|---|
| Maximum Charging Current Setting | `MNCHGC070` | `060 -> 070` | `ack MNCHGC070` | `HEEP1[1]` changed `060 -> 070` | ESPHome node restart preserved `HEEP1[1] = 070` |

Manual cross-check note: the inverter accepted `PBEQT059` and `PBEQOT119`, but Programs 33 and 34 document 5-minute stepping. The beta component keeps those controls on 5-minute values even though the protocol parser records the accepted off-step probes.

### Rejected Writes

| Control | Frame tested | Requested change | Status | Readback proof | Action taken |
|---|---:|---|---|---|---|
| Restore Second Output Delay Time Setting | `PDDLYT004` | `05 -> 04` | `nak PDDLYT004` | `HEEP2[13]` stayed `05` | Manual program 66 uses 5-minute stepping; component range now exposes `0`-`60 min` in 5-minute steps. |

### Current Confirmed Baseline

After the 2026-05-04 write loop, the inverter was left at:

| Field | Readback |
|---|---|
| Maximum Charging Current Setting | `HEEP1[1] = 070` |
| Maximum Mains Charging Current Setting | `HEEP1[2] = 030` |
| Display Automatically Returns To Homepage | `HEEP1[4] = 012` |
| Grid Connected Current Setting | `HEEP1[17] = 020` |
| Battery Recharge Voltage Setting | `HEEP2[4] = 046.0` |
| Battery Redischarge Voltage Setting | `HEEP2[5] = 054.0` |
| Battery Equalization Voltage Setting | `HEEP2[7] = 058.4` |
| Battery Equalization Time Setting | `HEEP2[8] = 060` |
| Battery Equalization Timeout Setting | `HEEP2[9] = 120` |
| Battery Equalization Interval Setting | `HEEP2[10] = 030` |
| Restore Second Output Delay Time Setting | `HEEP2[13] = 05` |

## 2026-11-16 ESPHome API Loop (firmware 6732a9f+)

Each write was sent through Home Assistant, ACK checked in the ESPHome log, and the readback frame captured in the same window:

| Control | Change frame | Readback | Restore frame | Final |
|---|---|---|---|---|
| Record Fault Codes | `PEz` | `QFLAG` `EakuvxDbjyz → EakuvxzDbjy` | `PDz` | `EakuvxDbjyz` (off) |
| Overload Bypass | `PEb` | `QFLAG` `EakuvxDbjyz → EabkuvxDjyz` | `PDb` | `EakuvxDbjyz` (off) |
| AC Input Voltage Range | `PGR01` | `QPIRI[15]` `0 → 1` | `PGR00` | `0` (Appliance) |
| Restore Second Output Battery Capacity | `PDSRS035` | `HEEP2[16]` `30000 → 35000` | `PDSRS030` | `30000` |
| Charger Priority (earlier the same day) | `PCP00` | `HEEP1[4]` `012 → 010` | `PCP02` | `012` (OSO) |

## Other writes on 2026-11-16

- `PVENGUSE01` was written and ACKed twice, at 03:21:24 and 03:21:58 (recorder export in `runs/20261116-esphome-api-roundtrips/`). These were deliberate writes by the assistant, setting PV feeding to LBU while reverting the tariff changes. The setting was already LBU, so no readback change was expected.
- `^S???DAT…` clock sync was ACKed at 03:39:26 and 03:39:54.

## Verification status per exposed control (2026-11-16)

**Round-trip** means a change and a restore were both observed in a readback slot. **One-way** means only one direction was seen in readback. **ACK-only** means the inverter accepted the frame, but no readback change was observed.

| Control | Frames | Status | Evidence |
|---|---|---|---|
| Display Returns Home | `PDk` / `PEkq:` | round-trip | `HEEP1[4]` `012 → 002 → 012` (2026-04-30, 2026-05-04) |
| Buzzer | `PDa` / `PEa` | round-trip | `HEEP1[5]` `1 → 0 → 1` (2026-04-30 captures) |
| Backlight | ``PDx`Y`` / `PExSh` | round-trip | `HEEP1[6]` `1 → 0 → 1` (2026-04-30) |
| Over-temperature restart | `PDv` / `PEv` | round-trip | `HEEP1[3]` digit 6 `1 → 0 → 1` (2026-04-30) |
| Input source prompt sound | `PDypx` / `PEyCI` | round-trip | `HEEP1[3]` digit 5 `1 → 0 → 1` (2026-04-30) |
| PV feeding priority | `PVENGUSE00` / `01` | round-trip | `HEEP1[3]` digit 3 `1 → 0 → 1` (2026-05-02, portal-driven) |
| Charger priority | `PCP00` / `01` / `02` | round-trip | `HEEP1[4]` `010`, `011`, `012` (2026-05-02, 2026-11-16) |
| Recharge / redischarge voltage, equalization voltage / time / timeout / interval, grid-connected current, max mains / max charging current | see table above | round-trip | `HEEP1`/`HEEP2` deltas (2026-05-04) |
| Restore second output battery capacity | `PDSRS` | round-trip | `HEEP2[16]` `30000 → 35000 → 30000` (2026-11-16) |
| Overload bypass | `PEb` / `PDb` | round-trip | `QFLAG` `b` and `HEEP1[7]` moved together, twice (`runs/20261116-esphome-api-roundtrips`) |
| Record fault codes | `PEz` / `PDz` | round-trip | `QFLAG` `z` moved enabled ↔ disabled (2026-11-16) |
| AC input voltage range | `PGR01` / `PGR00` | round-trip | `QPIRI[15]` and `HEEP1[3]` d0 `0 → 1 → 0` (`runs/20261116-esphome-api-roundtrips`) |
| Overload restart | `PEu` / `PDu` | one-way | `PEu` moved `HEEP1[3]` digit 4 `0 → 1` (2026-11-16); `PDu` was only ACK-checked (2026-04-30). `QFLAG` `u` agrees with the current value. |
| Restore second output delay | `PDDLYT` | ACK + value match | `PDDLYT005` ACK / `004` NAK; `HEEP2[13]` matches the stored value (`05`, now `10`) |
| BMS lock SOC | `BMSSDC` | ACK + value match | `020` ACK / `021` NAK; `HEEP1[10]` reads `020` |
| Battery equalization enable | `PBEQE1` / `PBEQE0Z2` | ACK-only | readback from `QBEQI[0]` matches Off; deliberately not toggled on lithium |
| Battery type | `PBT03` / `PBT04` | ACK + X2 | readback `HEEP1[3]` d2, cross-checked against `QPIRI[12]`. `HBAT[0]` is the series count, so earlier ledgers that used it as readback were wrong. Both slots read `4` (PYL) in April and `3` (LIA) now; the April/May `PBT03 → PBT04` probe may have left the unit on LIA. Not confirmed: LCD program 05 has not been checked. |
| SOC back to mains / battery, inverter restart SOC | `BMSB2UC` / `BMSU2BC` / `BMSSRC` | round-trip | `HEEP1[11]`/`[12]`/`[13]` `025→030→025`, `040→045→040`, `050→055→050` (`runs/20261116-vendor-setter-roundtrips`) |
| Second-output cut-off SOC | `PDSDS` | round-trip | `HEEP2[2]` `020→025→020` (same run) |
| Second-output discharge time | `PDDCGT0990` / `PDDCGT0000` | round-trip | `HEEP2[16]` `30000→30990→30000`; the 3-digit `PDDCGT000` got no reply (`runs/20261116-pddcgt-format-probe`) |
| Low battery alarm V, second-output cut-off / recovery V | `PSLV` / `PDSDV` / `PDSRV` | NAK | rejected even at the current value (same run); unsafe profile only |
| BMS function enable | `BMSC00` / `BMSC01` | ACK + X1 | readback `HEEP1[9]` (vendor tool); reads 1 while the BMS link is up. Disabled by default. |
| Output source priority | `POP00` / `POP01` | partial | `POP00` captured with ACK. `POP01` has never been seen on the wire; the readback is `QPIRI[16]`. |
| ECO power saving | `PEj` / `PDj` | untested | disabled by default; cuts output at low load in battery mode |

## Method

1. Read the current value from `HEEP1`/`HEEP2` (or the PI30 readback) and HA history before changing anything.
2. Change one entity only.
3. Watch Last Write Status for the frame and its reply (`ack`, `nak`, `timeout`, `refused …`).
4. The component re-reads the settings snapshots after every write; Refresh Settings Poll forces one.
5. Restore the original value at once, and confirm the readback returns to it.
6. Record the change, the restore and the readback in this ledger and in `research/powmr-protocol-20260425-0348/runs/`.

Evidence levels used below:
- **round-trip**: frame, ACK, readback change, restore ACK and final readback are all captured;
- **ACK-only**: the frame is ACKed, but no readback change is isolated;
- **NAK**: the frame is rejected on this unit;
- **untested**: never sent.

The per-control classes (RAW, X1, X2, VENDOR, ACK, NAK) in [HEEP_SETTINGS_MAP.md](HEEP_SETTINGS_MAP.md) summarise the same evidence.

## Promotion Rules

A control can move from experimental to confirmed for this inverter model only when it has:

- a captured frame;
- `ack` status for the change frame;
- a readback token change on `HEEP1`, `HEEP2`, or another stable config frame;
- an ACKed restore frame;
- a final readback matching the original value.

ACK-only controls remain disabled by default or research-only. Rejected values should be removed from the exposed range unless there is evidence that a different inverter mode or adjacent accepted value explains the NAK.

## Appendix A: 2026-04/05 per-family probe notes

Historical. These notes were written when the component was read-only, and the policy remarks reflect that time. The "Command not supported" rejections came from the Solar of Things cloud portal, not the firmware: `BMSB2UC`, `BMSU2BC`, `PDDCGT` and `PDSDS` were later ACKed on the bus (2026-11-16).

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

`Output Source Priority Setting` now has a live-observed `POP` write family. The raw `POP00` frame is pinned. `POP00` = `SUB priority`; `POP01` = `SBU priority` is inferred, and its readback is `QPIRI[16]`. `POP02` is **not** offered: the manual's program 01 has only SUB/SBU, and on sibling firmwares (Easun, vendor tool) `POP02` is SUF, which feeds power to the grid.

The remaining topology/source-priority controls stay out of the production entity set even when bus-confirmed.

## Appendix B: 2026-04/05 capture details

Historical bus captures from the first write round-trips. At the time, the refresh button was Refresh Identity Poll; settings now have their own 5-minute tier.


`Backlight On` and `Display Automatically Returns Home` have both been exercised live through the Solar of Things portal and restored successfully. They are the current templates for a safe write probe:

1. toggle one reversible UI-only setting
2. send once
3. confirm portal success
4. restore the original value
5. keep the sniffer trace paired with the portal action

`Battery Equalization Interval` is now the current template for a low-risk numeric probe:

- lower or raise the day count by one
- send once
- confirm portal success
- restore the original value immediately
- keep the bus capture paired with the portal action

The latest ESPHome web API loop confirmed the live wire forms and readback token for `Battery Equalization Interval Setting`:

- `PBEQP030` -> `50 42 45 51 50 30 33 30 36 12 0D`, response `(ACK9 `
- `PBEQP031` -> `50 42 45 51 50 30 33 31 26 33 0D`, response `(ACK9 `
- readback: zero-based `HEEP2[10]` changed `030 -> 031 -> 030`

`Battery Equalization Mode Enable` is now the current template for the binary equalization row:

- flip the row once
- send once
- confirm portal success
- restore the original value immediately
- keep the bus capture paired with the portal action

The latest decoder pass confirmed the live wire forms for `Battery Equalization Mode Enable Setting`:

- enable: `PBEQE1` -> `50 42 45 51 45 31 4A 13 0D`, response `(ACK9 `
- disable: `PBEQE0Z2` -> `50 42 45 51 45 30 5A 32 0D`, response `(ACK9 `

`Battery Equalization Time` is now the current template for a low-risk numeric probe:

- lower or raise the time by one manual-supported 5-minute step
- send once
- confirm portal success
- restore the original value immediately
- keep the bus capture paired with the portal action

The latest decoder pass confirmed the live wire forms for `Battery Equalization Time Setting`:

- `PBEQT059` -> `50 42 45 51 54 30 35 39 C7 6C 0D`, response `(ACK9 `
- `PBEQT060` -> `50 42 45 51 54 30 36 30 03 16 0D`, response `(ACK9 `
- readback: zero-based `HEEP2[8]` changed `060 -> 059 -> 060`
- manual cross-check: the inverter accepted `59 min`, but Program 33 documents 5-minute steps; beta firmware exposes only 5-minute values.

`Battery Equalization Timeout` is now the current template for the equalization-overtime row:

- lower or raise the timeout by one manual-supported 5-minute step
- send once
- confirm portal success
- restore the original value immediately
- keep the bus capture paired with the portal action

The latest decoder pass confirmed the live wire form for `Battery Equalization Timeout Setting`:

- `PBEQOT120` -> `50 42 45 51 4F 54 31 32 30 88 44 0D`, response `(ACK9 `
- `PBEQOT119`, response `ack PBEQOT119`, readback zero-based `HEEP2[9]` changed `120 -> 119 -> 120`
- manual cross-check: the inverter accepted `119 min`, but Program 34 documents 5-minute steps; beta firmware exposes only 5-minute values.

`Grid Connected Current` is now the current template for the grid-current trim row:

- lower or raise the current by one amp
- send once
- confirm portal success
- restore the original value immediately
- keep the bus capture paired with the portal action

The latest decoder pass confirmed the live wire forms for `Grid Connected Current Setting`:

- `PGFC021` -> `50 47 46 43 30 32 31 E1 4B 0D`, response `(ACK9 `
- `PGFC020` -> `50 47 46 43 30 32 30 F1 6A 0D`, response `(ACK9 `
- readback: zero-based `HEEP1[17]` changed `020 -> 021 -> 020`

`BMS Lock Machine Battery Capacity` is now the current template for the BMS lock row:

- raise the capacity by one point
- send once
- confirm portal success or reject
- restore the original value immediately
- keep the bus capture paired with the portal action

The latest decoder pass confirmed the live wire forms for `BMS Lock Machine Battery Capacity Setting`:

- `BMSSDC021Xe` -> `42 4D 53 53 44 43 30 32 31 58 65 0D`, response `(NAKss`
- `BMSSDC020HD` -> `42 4D 53 53 44 43 30 32 30 48 44 0D`, response `(ACK9 `

`Maximum Mains Charging Current` is now the current template for the mains-charge trim row:

- lower or raise the current by one amp
- send once
- confirm portal success
- restore the original value immediately
- keep the bus capture paired with the portal action

The latest decoder pass confirmed the live wire forms for `Maximum Mains Charging Current Setting`:

- `MUCHGC020` -> `4D 55 43 48 47 43 30 32 30 F3 F1 0D`, response `(ACK9 `
- `MUCHGC030` -> `4D 55 43 48 47 43 30 33 30 C0 C0 0D`, response `(ACK9 `
- `MUCHGC040`, response `ack MUCHGC040`, readback zero-based `HEEP1[2]` changed `030 -> 040 -> 030`

`Maximum Charging Current` is now the current template for the inverter-charge trim row:

- lower or raise the current by one supported step
- send once
- confirm portal success
- restore the original value immediately
- keep the bus capture paired with the portal action

The latest decoder pass and ESPHome web API loop confirmed the live wire forms for `Maximum Charging Current Setting`:

- `MNCHGC050` -> `4D 4E 43 48 47 43 30 35 30 81 7D 0D`, response `(ACK9 `
- `MNCHGC060` -> `4D 4E 43 48 47 43 30 36 30 D4 2E 0D`, response `(ACK9 `
- `MNCHGC070`, response `ack MNCHGC070`, readback zero-based `HEEP1[1]` changed `060 -> 070`
- `MNCHGC080`, response `ack MNCHGC080`, readback zero-based `HEEP1[1]` changed `070 -> 080 -> 070`

`Charger Priority` is now the current template for the topology priority row:

- change one value only
- send once
- confirm portal success
- restore the original value immediately
- keep the bus capture paired with the portal action

The current best mapping is:

- `PCP00` -> `CSO`
- `PCP01` -> `SNU`
- `PCP02` -> `OSO`

Latest live round-trip:

- `PCP00` on `OSO -> CSO`
- `PCP02` on `CSO -> OSO`
- `PCP01` on `OSO -> SNU`
- `PCP02` on `SNU -> OSO`

`PCP02` is the shared restore frame on both `SNU -> OSO` and `CSO -> OSO`.

`Output Source Priority` is now the current template for the output-priority row:

- change one value only
- send once
- confirm portal success
- restore the original value immediately
- keep the bus capture paired with the portal action

The current best mapping is:

- `POP00` -> `SUB priority`
- `POP01` -> `SBU priority`

`POP02` is **not** offered: the manual's program 01 has only SUB/SBU, and on sibling firmwares (Easun, vendor tool) `POP02` is SUF, which feeds power to the grid.

`PV Energy Feeding Priority` is now the current template for the PV-feeding row:

- change one value only
- send once
- confirm portal success
- restore the original value immediately
- keep the bus capture paired with the portal action

The current best mapping is:

- `PVENGUSE00` -> `BLU`
- `PVENGUSE01` -> `LBU`

`Battery Recharge Voltage` and `Battery Redischarge Voltage` are now the other low-risk numeric templates:

- lower or raise the voltage by one step
- send once
- confirm portal success
- restore the original value immediately
- keep the bus capture paired with the portal action

The latest ESPHome web API loop confirmed their readback tokens:

- `PBCV45.0`, response `ack PBCV45.0`, zero-based `HEEP2[4]` changed `046.0 -> 045.0 -> 046.0`
- `PBDV53.0`, response `ack PBDV53.0`, zero-based `HEEP2[5]` changed `054.0 -> 053.0 -> 054.0`

`Battery Equalization Voltage` is now readback-confirmed:

- `PBEQV58.30`, response `ack PBEQV58.30`, zero-based `HEEP2[7]` changed `058.4 -> 058.3 -> 058.4`

`Battery Cut Off Voltage Setting` is the next observed write family on the bus:

- live probe: `PSDV41.0`
- raw hex: `50 53 44 56 34 31 2E 30 4A 40 0D`
- response: `NAKss`
- interpretation: the family is present, but the tested `41.0 V` delta was rejected

The low-risk queue is now exhausted on this firmware. The following portal rows were probed and rejected with `Command not supported`, so they should stay out of the lab queue:

- `lowBatteryAlarmVoltageSetting`
- `parallelShutdownBatteryVoltageSetting`
- `restoreBatteryDischargingBatteryCapacity`
- `restoreMainsChargingBatteryCapacity`
- `secondOutputDischargeTimeSetting`

`batteryFloatChargingVoltageSetting` is now bus-observed as `PBFT<XX.X>`, but the tested values were rejected, so it should stay out of the lab queue for now.

`Restore Second Output Delay Time` is currently range-limited by manual and live rejection evidence:

- accepted restore frame: `PDDLYT005`
- rejected probe: `PDDLYT004`, response `nak PDDLYT004`
- readback: zero-based `HEEP2[13]` stayed `05`
- manual cross-check: Program 66 is `0..60 min` in 5-minute steps, so the beta component exposes that range and rejects non-5-minute values before sending

The latest decoder pass confirmed the live wire forms for `Display Automatically Returns To Homepage`:

- disable: `PDk` on the portal, wire form `50 44 6B 42 0B 0D`, response `(ACK9 `
- enable: `PEkq:`, wire form `50 45 6B 71 3A 0D`, response `(ACK9 `
