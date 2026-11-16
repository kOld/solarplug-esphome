# Security and Safety

This project talks to an inverter over RS232. Incorrect commands can change operating parameters, battery thresholds, charging behavior, or system time.

## What can write to the inverter

**Read-only by default**
- The component's defaults are `passive_mode: true` and `enable_writes: false`: it sends nothing.
- The read packages send only read queries.
- The full reader also sends the `HOSTCRCEN` keep-alive (`response_crc: true`). It changes the framing of the replies for about 64 s, not a setting.

**Writes need three explicit opt-ins:** `enable_writes: true`, `passive_mode: false` and `write_profile: beta` or `unsafe`.
- Two diagnostics need only the first two: the `sync_clock` button (`^S???DAT`, which also needs `time_id`) and the `probe_write` action (`HOSTCRCEN` / `HOSTCRCDN`).
- `packages/solarplug-write-beta.yaml` holds the controls with bus evidence on this unit. The exceptions are `power_saving` (never sent, disabled by default) and `POP01` (never seen on the wire).
- `packages/solarplug-write-unsafe.yaml` holds NAK-only, never-sent or output-cutting controls, all disabled by default. Config validation refuses them without `write_profile: unsafe`.

**Encoding**
- Each entity maps to one fixed encoder (`components/solarplug/solarplug_writes.cpp`). Every value is range-checked in its encoder and step-checked there or against the entity step in the component. Ordered settings fail closed while a bound is unread.
- There is no arbitrary command sender. `probe_write` accepts only `HOSTCRCEN` / `HOSTCRCDN`.

**Never encodable**
- `PF` (factory reset), `^S???CLE` (clears the energy counters) and `PSAVE`;
- the calibration frames `IVA±` and `BTA±`;
- `POP02` (feed to grid on sibling firmwares).

See [docs/protocol/HEEP_SETTINGS_MAP.md](docs/protocol/HEEP_SETTINGS_MAP.md) for the evidence behind every control.

## Reporting

Please do not publish secrets, Solar of Things credentials, serial numbers, public IPs, or unredacted cloud tokens in issues. For protocol captures, include raw hex and ASCII but redact any account or site identifiers.
