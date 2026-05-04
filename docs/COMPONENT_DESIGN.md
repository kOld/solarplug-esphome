# Component Design

`solarplug` is an ESPHome external component built as `Component + uart::UARTDevice`.

## Loop Model

The component intentionally does not inherit `PollingComponent`. It has three poll tiers (`fast`, `energy`, and `identity`), manual refresh buttons, passive-sniffer mode, and a nonblocking command queue that must wait for UART responses before sending the next frame. A single `loop()` state machine keeps those behaviors in one place without blocking ESPHome.

## Framing

Read commands use plain ASCII plus carriage return. Most write commands use CRC-XMODEM plus carriage return. The firmware tracks this with `FrameStyle` and queues each command with its frame style, so setters such as `PCP00`, `PBEQV58.30`, `PBCV45.0`, `PBDV53.0`, `PBT04`, `PGFC020`, `MUCHGC030`, and `MNCHGC050` are encoded with CRC bytes before `0x0D`.

## Write Safety

Write entities are gated by all of the following:

- `enable_writes: true`
- `passive_mode: false`
- `write_profile: beta` or `write_profile: unsafe`

The normal beta profile excludes NAK-only or unsupported controls. `battery_cut_off_voltage`, `battery_float_voltage`, and `battery_bulk_voltage` require `write_profile: unsafe`.

Write entities publish the requested switch/number/select state after a frame is accepted into the command queue, but that state is optimistic. The component publishes a diagnostic `last_write_status` text sensor with `pending`, `ack`, `nak`, `timeout`, or parser error status for the active write. Confirmed control state should come from decoded readback telemetry when a readback mapping exists.

## Runtime Footprint

The UART request frame, receive frame, and pending command queue are fixed-capacity buffers. Normal operation does not format raw hex/ASCII strings or per-field decoder logs for every frame. `raw_frame_logging: true` is available for lab captures where exact TX/RX bytes matter, and `decoded_field_logging: true` adds decoded field traces without changing entity publication.

The beta write example also enables ESPHome `debug` heap, largest-block, loop-time, CPU-frequency, device-info, and reset-reason diagnostics. Those sensors stay in the beta firmware rather than the minimal/read-only examples.

Some decoder paths still build token and decoded-field strings after a complete frame arrives. The parser now avoids whole-payload trim copies, reserves token/field vectors before filling them, and avoids substring allocation for date and HBMS cell-index parsing. That keeps the readable implementation intact while removing the highest-frequency vector growth and front-erase patterns from the UART hot path.

## YAML Packages

Reusable ESPHome package files live in `packages/`:

- `solarplug-read-full.yaml` exposes the full read-only entity surface used by the Atom S3 replacement.
- `solarplug-read-minimal.yaml` exposes the smaller active-reader surface.
- `solarplug-passive-sniffer.yaml` exposes passive text diagnostics only.
- `solarplug-write-beta.yaml` layers bus-confirmed write controls on top of the full reader.

Examples keep the hardware, network, and security configuration local to each device and import the Solar Plug entity/control surface from these package files. ESPHome merges packages non-destructively, so users can override fields locally or use `!extend` / `!remove` for customization without copying the entire `solarplug:` block.
