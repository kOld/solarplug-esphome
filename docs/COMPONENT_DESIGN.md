# Component Design

`solarplug` is an ESPHome external component (`Component` + `uart::UARTDevice`) for the PowMr POW-HVM6.2KP (`HPVINV02`, firmware 40.05) and similar H-protocol inverters. It replaces the vendor's Solar Plug WiFi dongle on the RS232 port.

## Code layout

| File | Role |
|---|---|
| `components/solarplug/solarplug_codec.{h,cpp}` | Pure protocol code, with no ESPHome dependency. It covers: <ul><li>token helpers and every field decoder;</li><li>CRC16/XMODEM with the byte adjustment;</li><li>reply classification (`classify_reply`);</li><li>the response-CRC tracker (`ResponseCrcTracker`, `crc_keepalive_due`);</li><li>learned reply lengths (`ReplyLengthGuard`);</li><li>the settings readback parsers (`heep1_readback`, `heep2_readback`, `qpiri_readback`, `qflag_readback`, `qbeqi_readback`);</li><li>text sanitizing.</li></ul> |
| `components/solarplug/solarplug_writes.{h,cpp}` | Pure write encoders, `encode_switch`, `encode_select` and `encode_number`. Each validates a request and returns the frame or a refusal reason. |
| `components/solarplug/solarplug.{h,cpp}` | The ESPHome component. It holds the UART state machine, the command queue, the poll tiers, the readback *policy* (which source is primary for each control, and the cross-check), entity publishing, the charge-current options and bus statistics. |
| `components/solarplug/__init__.py` | Config schema, entity tables, write-profile gating and codegen. |
| `tools/decode_h_protocol.py` | Independent Python reference decoder, plus capture and replay tooling. |
| `tools/validate_examples.py` | Runs `esphome config` on every example against this checkout. |

## Loop model

The component does not inherit `PollingComponent`. It has poll tiers, manual refresh buttons, a passive-sniffer mode and a non-blocking command queue that waits for each reply before sending the next frame, all in one `loop()` state machine.

### Poll tiers

| Tier | Default interval | Commands | Feeds |
|---|---|---|---|
| `fast` | 20 s | `HSTS`, `HGRID`, `HOP`, `HBAT`, `HPV`, `HTEMP` | live telemetry, alarms, LEDs, relays, charge switches |
| `energy` | 60 s | `HGEN`, `HBMS1`, `HBMS2`, `HBMS3` | generation counters, BMS limits and status |
| `settings` | 5 min | `HEEP1`, `HEEP2`, `QPIRI`\*, `QFLAG`\*, `QBEQI`\*, `HEEP3` | every write control's state, the read-only settings, the cross-check |
| `identity` | 30 min | `HIMSG1`, `QPRTL`, `HPVB`, `HIMSG2`, `QMCHGCR`\*, `QMUCHGCR`\* | firmware, model, rated powers, the inverter's allowed charge currents |

\* PI30 queries, CRC-framed.

- A scheduled poll skips any command with no consumer: no bound sensor, and no write entity that needs it.
- All tiers are queued once at boot, after `boot_poll_delay`.
- After every write, whatever the outcome, the settings snapshots are re-read.
- With `response_crc: true`, a `HOSTCRCEN` keep-alive is sent every 30 s, ahead of the queue (see [protocol/H_COMMANDS.md](protocol/H_COMMANDS.md#response-crc-mode-hostcrcen--hostcrcdn)).

## Framing and reply checking

**Request framing**
- H reads (`HSTS`, `HEEP1`, …) are plain ASCII plus CR.
- PI30 queries (`QPIRI`, `QFLAG`, `QBEQI`, `QMCHGCR`, `QMUCHGCR`) and every setter carry CRC16/XMODEM before the CR.
- Setters are encoded as the bare command (`PDx`, `PEk`, `PBEQE0`, `PBT04`, `MUCHGC030` …), and the CRC is added on the wire.
- Older captures show some of these with their CRC bytes, which happen to be printable: ``PDx`Y`` is `PDx` plus CRC `60 59`. The bytes are identical, and the tests pin that.

**Every reply is checked for a trailing CRC**
- PI30 replies and ACK/NAKs must carry one.
- H replies carry one only in response-CRC mode. `ResponseCrcTracker` follows that mode from the replies themselves: 3 in a row with a valid CRC turn stripping on, and 3 without turn it off.
- While the mode is on, a plain H reply is dropped as `crc_error` and a refresh is sent.
- `HOSTCRCEN` / `HOSTCRCDN` are acknowledged with `(CRC16EN` / `(CRC16DN`.

**Guards against a misattributed reply**
- **Classification:** `classify_reply` sorts each reply into data, ACK, NAK or invalid. An ACK or NAK during a read is a late reply to an earlier write and is never credited to the read.
- **Resync:** after a bad frame, the next request waits until the bus has been quiet for 400 ms, at most 3 s, draining whatever arrives. After a timeout, a 100 ms drain remains.
- **Learned lengths:** each command must keep the token count it has shown in two identical replies (`ReplyLengthGuard`). A different count is dropped once and adopted if it repeats, so a firmware with an extra token costs one frame. A fixed table would break other firmware: an early corpus row for HIMSG2 had 3 tokens, but the live reply has 5. Passive mode checks only the per-command minimum.
- **Shape:** the reply must start with `(`, and every byte must be printable ASCII.
- **Retry:** a failed read is retried once. Probe-only queries, which time out when unsupported, are not retried, and neither is any setter: a write is sent once.

## Write safety

**Gates**
- **Entity creation:** write entities exist only with `enable_writes: true`, `passive_mode: false` and `write_profile: beta` or `unsafe`. The `sync_clock` button (which also needs `time_id`) and the `probe_write` action need only the first two.
- **Unsafe profile:** config validation refuses controls that are NAK-only, never sent, or able to cut the output unless `write_profile: unsafe` is set ([protocol/WRITE_SURFACE.md](protocol/WRITE_SURFACE.md)).
- **Runtime:** writes are accepted only for configured entities, and for selects only the options the entity offers. A YAML lambda therefore cannot reach an encoder the profile does not allow.

**Encoders** (`solarplug_writes.cpp`, golden-frame tests for every setter)
- **Ranges and steps:** every number is range-checked in its encoder, and step-checked there or against the entity step in the component. A value off the step is refused, not rounded.
- **Ordered settings:**
  - The rules are: BMS lock SOC < SOC back to mains < SOC back to battery, and cut-off SOC < restore SOC.
  - They are checked against the values the inverter last reported or acknowledged, and fail closed while a bound is unread.
  - Ordered settings and the paired hour windows are refused while another write is in flight.
- **Charge currents:** the options are the inverter's announced `QMCHGCR` / `QMUCHGCR` lists inside the model envelope.
  - The lists are adopted after two identical replies and persisted in flash, so the options are right from boot.
  - HA reads select options when it connects.

**Publishing**
- **Seeding:** every control is seeded from the inverter's snapshots at boot and on every settings poll: `HEEP1`, `HEEP2`, `QPIRI`, `QFLAG`, `QBEQI`, and `HSTS` for `second_output_power`. One source is primary for each control; the others feed the Readback Cross-Check sensor ([protocol/HEEP_SETTINGS_MAP.md](protocol/HEEP_SETTINGS_MAP.md)).
- **No optimistic state:** the requested state is published only after the inverter ACKs it. An ACKed number becomes the reported value at once; a write that times out forgets the value until the readback arrives.
- **Last Write Status** shows one of:
  - `pending <frame>`;
  - `ack <frame>`, `nak <frame>` or `timeout <frame>`;
  - `refused <key>: <reason>`; the entity is then put back to the inverter's value.
- **Write kinds:** queued commands carry a kind: `READ`, `WRITE` or `CRC_KEEPALIVE`. The keep-alive never touches Last Write Status or the write read-back logic.

## Runtime footprint

- **Buffers:** the request frame, the receive frame and the 32-entry command queue have fixed capacity.
- **Logging:** a routine poll logs at debug level only. `raw_frame_logging: true` logs every TX/RX for lab captures, and `decoded_field_logging: true` adds per-field traces.
- **Text states:** they are copied for sanitizing only when they contain a non-printable byte. Field units are string literals.
- **Bus statistics:** once a minute the component publishes bus utilization, mean response time, and cumulative timeouts, frame errors and NAKs. Probe and keep-alive failures are left out of the health counters.

Measured on HPVINV02 on 2026-11-16, with response CRC on:
- bus about 10–16 % busy, about 260 ms mean response, 0 errors;
- loop time 17–19 ms;
- about 234 KB free heap, with `flash_chip: gd` set under `esp32 > framework > advanced`.

## Tests

`python3 -m unittest tests.test_decode_h_protocol` runs:
- the Python reference tests: frame classification, decoders, and write-frame classification;
- `FirmwareCodecTest`, which compiles `solarplug_codec.cpp`, `solarplug_writes.cpp` and `tests/cpp/codec_harness.cpp` with `-Wall -Wextra -Werror`.
  - It runs the harness self-test: CRC vectors, decode, readback, the CRC tracker, the length guard, reply classification and golden write frames.
  - It compares firmware and Python decodes on every row of `tests/fixtures/payload_corpus.tsv`, and fails if a row is skipped.
- `SchemaEncoderParityTest`, which reads the entity tables from `__init__.py`. Every switch state, every select option, and every number grid point HA can offer must be accepted by the firmware's encoder; values just outside are refused.

**Replay without an inverter**
```bash
python3 tools/decode_h_protocol.py --input tests/fixtures/dual_channel_sample.md --outdir out/solarplug-replay
```
This writes `frames.csv`, `frames.jsonl`, `command_responses.jsonl`, `decoded_state.json`, `report.md` and `unpaired_frames.md`.

**CI** (`.github/workflows/tests.yml`) runs the unit tests and `tools/validate_examples.py`, which validates every example plus the unsafe package with ESPHome 2026.10.2. Firmware builds happen in ESPHome.

**On the device:** the state machine (`loop()`, timeouts, retries) is not host-tested. Check the bus statistics sensors after every flash.

## YAML packages

| Package | Adds |
|---|---|
| `solarplug-read-full.yaml` | The full read surface, with `response_crc: true` |
| `solarplug-read-minimal.yaml` | A smaller active-reader surface |
| `solarplug-passive-sniffer.yaml` | Passive text diagnostics only; nothing is sent |
| `solarplug-write-beta.yaml` | The beta write controls (bus evidence on this unit, except `power_saving` and `POP01`), Last Write Status, the cross-check, the `probe_read` / `probe_write` API actions |
| `solarplug-write-unsafe.yaml` | The unsafe-profile controls, all disabled by default. Include it after the beta package. |

The beta example also enables ESPHome `debug` heap, loop-time and reset-reason sensors. Keep `esphome: name:` unchanged after the first flash, because OTA finds the node by that name.

Examples keep hardware, network and security settings local and import the entity surface from these packages. ESPHome merges packages non-destructively, so fields can be overridden locally or with `!extend` / `!remove`.
