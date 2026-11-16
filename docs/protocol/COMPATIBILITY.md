# Compatibility

This project targets the Solar Plug / Solar of Things RS232 telemetry path used by some PowMr/MrPow hybrid inverter families.

Compatibility is tracked in three tiers:

- **Confirmed**: the local active replacement has been tested against the inverter.
- **Likely compatible**: the inverter is listed by the same dongle/vendor compatibility surface, but no local capture has been submitted yet.
- **Research lead**: public references suggest the same dongle/protocol family, but evidence is weaker.

## Confirmed Setup

| Brand | Inverter model | Wi-Fi dongle | Solar of Things type | Firmware/software | Status |
|---|---|---|---|---|---|
| PowMr / MrPow | `POW-HVM6.2KP` | `WIFI-RELAB` | `HPVINV02` | `40.05` | confirmed H-protocol replacement; most beta writes round-trip verified (WRITE_TEST_LEDGER.md) |

Community reports (read-only unless noted):

| Brand | Model | QPRTL | Firmware | Report | Notes |
|---|---|---|---|---|---|
| PowMr | `POW-HVM6.2KP` | `HPVINV02` | `40.09` (`0040.09 20260119 12`) | issue #4 | Same frame layouts as 40.05: HEEP1 20 tokens, HEEP2 17. HGEN reported an uninitialised total (`036274176.0`) on a unit that never had PV; the HGEN guard rejects it. |
| Easun | SMT 4 kW (24 V) | not reported | — | PR #3 and the contributor's fork | Uses PEC / SOR priorities (extended options), 24 V voltage ranges and charge current up to 120 A; battery type code 03 (shown as `LIA`) is LiFePO4 on its LCD. Its QPRTL reply is not reported, so set `battery_voltage: 24`, the charge-current options and, once known, `machine_type`. |

Confirmed local identity:

- PI30 identity: `QPI -> (PI30`
- PI30 model: `QMN -> (VMII-6200`
- Solar Plug protocol type: `QPRTL -> (HPVINV02`
- H-protocol firmware block: `HIMSG1 -> (0040.05 20250923 12`
- observed Wi-Fi dongle used for sniffing: `WIFI-RELAB`

## WIFI-RELAB Listed Compatibility

The sniffer source dongle for this project is `WIFI-RELAB`. The compatibility list for that module says it is only compatible with the inverters below. Because these models are listed together for the same Wi-Fi module, they are good candidates for the same Solar Plug / Solar of Things H-command family, but they remain **likely compatible** until users provide captures.

| Inverter model | Evidence | Project status |
|---|---|---|
| `POW-HVM6.2KP` | same `WIFI-RELAB` module, local sniffer and active replacement tested | confirmed |
| `POW-HVM11KP` | listed for `WIFI-RELAB` | likely compatible, unverified |
| `POW-RELAB 3KU` | listed for `WIFI-RELAB` | likely compatible, unverified |
| `POW-RELAB 3KE` | listed for `WIFI-RELAB` | likely compatible, unverified |
| `POW-RELAB 5KU` | listed for `WIFI-RELAB` | likely compatible, unverified |
| `POW-RELAB 5KE` | listed for `WIFI-RELAB` | likely compatible, unverified |
| `POW-RELAB 10KU` | listed for `WIFI-RELAB` | likely compatible, unverified |
| `POW-RELAB 10KE` | listed for `WIFI-RELAB` | likely compatible, unverified |
| `POW-RELAB SPLIT Series` | listed for `WIFI-RELAB` | likely compatible, unverified |

## Command-Set Status By Family

| Family | Confirmed model(s) | Known working command set | Notes |
|---|---|---|---|
| `HPVINV02` / `VMII-6200` path | `POW-HVM6.2KP` (fw 40.05) | every polled command in the tiers of [../COMPONENT_DESIGN.md](../COMPONENT_DESIGN.md#poll-tiers), plus the setters in [HEEP_SETTINGS_MAP.md](HEEP_SETTINGS_MAP.md) | telemetry, settings readback, beta writes, response CRC |
| `WIFI-RELAB` listed models | models listed above | unknown until captured | likely same family, but do not mark supported without evidence |

## Model Profile

The component is verified on a 48 V HPVINV02 (machine type B). Four options adapt it to siblings, using the code tables from the vendor tool (`zhuaibai/TQF-AllInOne`):

```yaml
solarplug:
  id: solarplug_reader
  machine_type: auto          # auto (from QPRTL) | A | B | C | D | F | UNKNOWN
  battery_voltage: 48         # 48 or 24: scales every voltage setting
  charging_current_options: [10, 20, 30, 40, 50, 60, 70, 80]  # the envelope for program 02
  mains_charging_current_options: [2, 10, 20, 30, 40, 50, 60]  # the envelope for program 11
```

**Machine type** decides the output- and charger-priority codes (`POPxx`, `PCPxx`). Everything else is shared.

| Type | QPRTL | Output priority codes | Charger priority codes |
|---|---|---|---|
| A, C | `HPVINV01`, `HPVINV03` | 0 UTI, 1 SUB, 2 SBU | 1 CSO, 2 SNU, 3 OSO (0 = CUT, not offered) |
| B, D | `HPVINV02`, `HPVINV05`, `HPVINV04` | 0 SUB, 1 SBU, 3 PEC (extended) | 0 CSO, 1 SNU, 2 OSO, 3 SOR (extended) |
| F | `HPVINV08` | 0 SUB, 1 SBU, 3 PEC (extended; 2 = SUF, never offered) | 0 CSO, 1 SNU, 2 OSO, 3 SOR (extended) |
| UNKNOWN | anything else (HPVINV06/07, other brands), or no QPRTL reply after 5 tries | as B, which earlier releases used | as B |

- **With `auto`:**
  - priority writes are refused, and priorities are not read back, until QPRTL has been read;
  - QPRTL is asked every 20 s until it answers; after 5 unanswered tries the type falls back to UNKNOWN;
  - on the first detection the settings are re-read at once.
- The **Machine Type** sensor shows the table in use, for example `B (HPVINV02)`.
- **Conflicting override:** a `machine_type` set in YAML that contradicts the QPRTL reply (different code tables) stops priority readback and writes, and logs a warning. It never sends codes from the wrong table, such as `POP02`, which feeds the grid on type F.
- **Where priorities are read:** QPIRI's priority fields are mapped only for the type B codes, the only ones verified on hardware. Other types read output priority from HEEP1 `[0]`. That doesn't fit HPVINV07, where the vendor reads the working mode from `HEEP1[8]`; set `machine_type` and treat its output-priority readback as unverified.
- **Type A / C options:** they offer `Utility first (UTI)`, `SUB priority` and `SBU priority`, and their charger priority has no SOR.

**HEEP1 layouts.** Readback understands three layouts. Any other layout publishes no settings readback, rather than wrong values, and logs one warning with its token count.
- **HPVINV02 (20 tokens):** read as is.
- **Generic GB3024 (21 tokens, an extra token at `[4]`):** the vendor tool used it for HPVINV06, and for HPVINV02 until its parser changed in September 2025. The vendor describes only the extra token's position, so the 21-token count is an inference until a capture confirms it.
- **LPVINV02 / VQ3024 (19 tokens):** `[4]` packs the display, frequency and charger digits plus the buzzer.

**Battery voltage.**
- Voltage settings scale by `battery_voltage / 48`, with the frame resolving 0.1 V. On 24 V that gives:
  - recharge 22–25.5 V in 0.5 V steps;
  - back-to-battery Full or 24–29 V in 0.5 V steps;
  - bulk and float 24–29.2 V;
  - cut-off 20–26 V.
- The inverter's own bank size (`HBAT[0]` × 12 V) is read whenever write entities exist. Voltage settings are refused until it has been read, and on a mismatch, which also logs a warning.

**Charge currents.**
- The option lists are the envelope.
- The inverter's announced `QMCHGCR` / `QMUCHGCR` lists narrow them further, when the model answers those queries.

Types other than B, and the 24 V scaling, come from the vendor tool and contributor reports; none is verified on hardware here. Please report a capture before relying on a write.

## Research Leads, Not Yet Confirmed

These are discoverability leads only. They are not listed as supported until users provide captures.

| Lead | Evidence | Status |
|---|---|---|
| Datouboss hybrid inverters | public Home Assistant forum posts mention Datouboss + Solar Plug-RWB1-06R + Solar of Things | research lead |
| TECHFINE inverters | public product listings mention Solar Plug-RWB1-06R and Solar of Things | research lead |
| Other Solar of Things RS232 devices | Solar Plug-RWB1 public documentation describes a generic RS232 Wi-Fi/BLE collector | research lead |

## How to Confirm a New Device

1. Capture `QPRTL`, `HIMSG1`, `HGEN`, `HBAT`, `HOP`, `HGRID`, `HPV`, `HTEMP`, and `HSTS`.
2. Run `tools/decode_h_protocol.py` against the capture.
3. Compare same-time values against the Solar of Things app or local inverter display.
4. Open an issue with redacted raw hex, ASCII frames, decoded output, device model, and firmware version.

Do not test write commands for compatibility confirmation. The read path, including the Machine Type sensor and the HEEP1 readback, tells you whether the profile fits.

## Suggested Issue Template Data

When reporting a new inverter, include:

```text
Inverter brand:
Inverter model:
Wi-Fi dongle model:
Solar of Things device type:
Firmware/software version:
QPRTL response:
HIMSG1 response:
Known working H commands:
Known timeouts:
Capture file:
Same-time app/display comparison:
```
