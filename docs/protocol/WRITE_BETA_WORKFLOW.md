# Write Beta Workflow

This workflow is for controlled protocol validation with the Atom S3 Lite connected as the active Solar Plug replacement.

Do not use this workflow while the original Solar Plug is connected. The RS232 bus must have only one active master.

For OTA to an existing node, keep the `esphome.name` aligned with the deployed device name in a temporary local build file.

## Controls

The beta firmware exposes only bus-confirmed families:

- `Buzzer On`
- `Backlight On`
- `Display Returns Home`
- `Over Temperature Restart`
- `Overload Restart`
- `Input Source Prompt Sound`
- `Battery Type`
- `BMS Function Enable`
- `BMS Lock Machine Battery Capacity`
- `Grid Connected Current`
- `Maximum Mains Charging Current`
- `Maximum Charging Current`
- `Battery Equalization Mode Enable`
- `Battery Equalization Interval`
- `Battery Equalization Timeout`
- `Battery Equalization Time`
- `Battery Equalization Voltage`
- `Battery Recharge Voltage`
- `Battery Redischarge Voltage`
- `Restore Second Output Battery Capacity`
- `Restore Second Output Delay Time`
- `Charger Priority`
- `Output Source Priority`
- `PV Energy Feeding Priority`
- `Last Write Status`

There is no arbitrary serial command sender. Topology/source-priority rows and BMS rows are disabled by default in Home Assistant and should be enabled only for a single planned probe.

The beta firmware includes heap free, largest heap block, loop time, CPU frequency, device info, and reset reason diagnostics. Check those together with `Last Write Status` during longer write sessions.

Beta writes are one-shot frames. Read commands may retry once after a timeout, but write frames are not retried automatically; a missing ACK must be investigated from logs and the `Last Write Status` diagnostic before repeating the action.

## Probe Discipline

1. Capture the current value from `HEEP1`, `HEEP2`, and Home Assistant state history before changing anything.
2. Change one entity only.
3. Watch ESPHome logs for the queued frame and response status.
4. Trigger an identity/config refresh and capture `HEEP1` / `HEEP2`.
5. Restore the original value immediately.
6. Record the before/change/restore frames in `captures/` and update `WRITE_SURFACE.md` only when the mapping is repeatable.

For model-specific promotion evidence, record the result in [WRITE_TEST_LEDGER.md](WRITE_TEST_LEDGER.md). A row is not confirmed for normal use until it has ACK, readback delta, restore ACK, and final readback evidence.

## Verified Safe Round-Trip

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
- `POP02` -> `Utility first (legacy)`

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

## Evidence Levels

- `confirmed`: bus frame, ACK, readback delta, and restore delta are all captured.
- `family shape confirmed`: bus frame and ACK/NAK behavior are captured, but exact readback token is not isolated.
- `candidate`: portal/cache correlation exists but no controlled bus delta yet.

## Release Policy

The production promise remains read-only telemetry and native generation counters. Write controls are beta-scoped until the project has enough restore and readback evidence across more inverter units to make them safe for normal users.
