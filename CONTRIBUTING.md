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

- a real reply added to `tests/fixtures/payload_corpus.tsv` (mark synthetic rows as such);
- the same decode in `components/solarplug/solarplug_codec.cpp` and the Python reference in `tools/decode_h_protocol.py`; the parity test compares them on every corpus row;
- the slot and its evidence class in `docs/protocol/HEEP_SETTINGS_MAP.md`.

Do not promote diagnostic fields to production entities without capture evidence and a matching trusted source.

## Compatibility Matrix Updates

Add new models to `docs/protocol/COMPATIBILITY.md` using one of these statuses:

- `confirmed`: an active replacement or a passive sniffer capture proves the command set.
- `likely compatible, unverified`: the model is listed for the same dongle family, but no capture has been submitted.
- `research lead`: public evidence suggests a relation, but dongle/model/protocol identity is not proven.

Do not mark a model confirmed from a sales listing alone.

## Write Commands

To add a write control:
1. Add its encoder to `components/solarplug/solarplug_writes.cpp`, with range, step and any ordering rule.
2. Add golden frames and refusals to `tests/cpp/codec_harness.cpp`.
3. Add the entity to `__init__.py`. If the frame is NAK-only, never sent or able to cut the output, add the key to the unsafe set.
4. Add a readback slot in the codec, so the entity is seeded from the inverter and not published optimistically.
5. Run `python3 -m unittest tests.test_decode_h_protocol`. `SchemaEncoderParityTest` fails if the entity offers a value its encoder refuses.
6. Run `python3 tools/validate_examples.py`.
7. Record a round-trip in `docs/protocol/WRITE_TEST_LEDGER.md` before the control leaves the unsafe profile.
