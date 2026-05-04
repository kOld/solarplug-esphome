# Replay Workflow

The replay workflow keeps protocol changes regression-backed without requiring a live inverter.

## Curated Fixture

The public fixture is:

```text
captures/2026-04-29-clean-h-protocol/dual_channel_sample.md
```

It includes representative request/response traffic for the core read commands and is used by the Python unit tests.

## Decoder Outputs

Run the decoder against a capture:

```bash
python3 tools/decode_h_protocol.py \
  --input captures/2026-04-29-clean-h-protocol/dual_channel_sample.md \
  --outdir out/solarplug-replay
```

Expected output files:

- `frames.csv`
- `frames.jsonl`
- `command_responses.jsonl`
- `decoded_state.json`
- `report.md`
- `unpaired_frames.md`

## Regression Tests

Run:

```bash
python3 -m unittest discover -s tests
```

The tests validate:

- frame classification: `ascii_cr`, `crc_cr`, and unknown binary handling;
- H-command decoding for `HGEN`, `HBAT`, `HOP`, `HGRID`, `HPV`, `HTEMP`, `HSTS`, `QPRTL`, and `HIMSG1`;
- diagnostic confidence for `HBMS1`, `HBMS2`, and `HBMS3`;
- reference decoding for known write frames without promoting them to production entities;
- release guardrails that keep `HEEP*`, low-confidence cell slots, and writes out of the production entity map.

## Live Comparison

Live comparison is optional and should be used only to validate a specific question. Do not treat Solar of Things station totals as proof of local serial support; use local serial frames as the source of truth and compare portal values only as secondary evidence.

Recommended live checks before a public tag:

- isolated `HPVB` read;
- daytime `HPV` / `HPVB` read;
- same-time `HBMS1` / `HBMS2` versus an independent BMS node;
- `HGEN` yearly reset observation. Daily and monthly resets are already documented in [HGEN_ROLLOVER.md](HGEN_ROLLOVER.md).
