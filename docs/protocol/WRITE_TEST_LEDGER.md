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
4. Press `Refresh Identity Poll`.
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

## Promotion Rules

A control can move from experimental to confirmed for this inverter model only when it has:

- a captured frame;
- `ack` status for the change frame;
- a readback token change on `HEEP1`, `HEEP2`, or another stable config frame;
- an ACKed restore frame;
- a final readback matching the original value.

ACK-only controls remain disabled by default or research-only. Rejected values should be removed from the exposed range unless there is evidence that a different inverter mode or adjacent accepted value explains the NAK.
