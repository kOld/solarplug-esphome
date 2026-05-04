# Contributing

Contributions are most useful when they include reproducible evidence.

## Capture Submissions

For new inverters or firmware versions, include:

- inverter brand and model
- Solar of Things device type, if known
- firmware/software version
- dongle model, for example `WIFI-RELAB` or Solar Plug-RWB1-06R
- raw request/response hex
- ASCII rendering
- same-time portal/app value, if available

Redact credentials, tokens, Wi-Fi passwords, public IPs, exact home addresses, and personal account identifiers.

## Decoder Changes

Every decoder change should include:

- replay fixture or small sample frame
- unit test coverage
- confidence level update in `protocol/fields.yaml`
- docs update when field meaning changes

Do not promote diagnostic fields to production entities without capture evidence and a matching trusted source.

## Compatibility Matrix Updates

Add new models to `docs/protocol/COMPATIBILITY.md` using one of these statuses:

- `confirmed`: active read-only replacement or passive sniffer capture proves the command set.
- `likely compatible, unverified`: the model is listed for the same dongle family, but no capture has been submitted.
- `research lead`: public evidence suggests a relation, but dongle/model/protocol identity is not proven.

Do not mark a model confirmed from a sales listing alone.

## Write Commands

Write/control commands are research-only for v1. Do not add write controls to the ESPHome component without a separate safety design and explicit opt-in lab mode.
