# Compatibility

This project targets the Solar Plug / Solar of Things RS232 telemetry path used by some PowMr/MrPow hybrid inverter families.

Compatibility is tracked in three tiers:

- **Confirmed**: local active read-only replacement has been tested against the inverter.
- **Likely compatible**: the inverter is listed by the same dongle/vendor compatibility surface, but no local capture has been submitted yet.
- **Research lead**: public references suggest the same dongle/protocol family, but evidence is weaker.

## Confirmed Setup

| Brand | Inverter model | Wi-Fi dongle | Solar of Things type | Firmware/software | Status |
|---|---|---|---|---|---|
| PowMr / MrPow | `POW-HVM6.2KP` | `WIFI-RELAB` | `HPVINV02` | `40.05` | confirmed read-only H-protocol replacement |

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
| `HPVINV02` / `VMII-6200` path | `POW-HVM6.2KP` | `HSTS`, `HGRID`, `HOP`, `HBAT`, `HPV`, `HTEMP`, `HGEN`, `QPRTL`, `HIMSG1` | v1 production read telemetry |
| `HPVINV02` / `VMII-6200` path | `POW-HVM6.2KP` | `HBMS1`, `HBMS2`, `HBMS3`, `HEEP1`, `HEEP2`, `HPVB` | diagnostic/default-disabled in v1 |
| `WIFI-RELAB` listed models | models listed above | unknown until captured | likely same family, but do not mark supported without evidence |

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

Do not test write commands for compatibility confirmation.

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
