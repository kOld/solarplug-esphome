# H Commands

This file records the Solar Plug / Solar of Things serial labels observed on the passive sniffer and reproduced by the active replacement on the confirmed `POW-HVM6.2KP` / `HPVINV02` setup.

The ESPHome implementation lives in `components/solarplug`. The replay decoder lives in `tools/decode_h_protocol.py`. This document is the canonical protocol reference for the sniffer, the replay tooling and the component. The per-control settings reference is [HEEP_SETTINGS_MAP.md](HEEP_SETTINGS_MAP.md).

## Framing

Observed H-family request frames are plain ASCII plus `0D`, with no CRC. Several write frames listed below look like plain ASCII (``PDx`Y``, `PExSh`, `PDypx`, `PEyCI`, `PEkq:`, `PBEQE0Z2`, `BMSSDC021Xe`), but they are the command followed by its two CRC bytes, which happen to be printable.

Inverter response frames start with byte `0x28`, ASCII `(`. That byte is a response-frame sentinel, not part of the first field. There is no matching `)`. Many ACK/NAK responses also carry two trailing CRC16/XMODEM bytes before `0D`; for example raw `(ACK9 ` is payload `(ACK` plus CRC bytes `39 20`, and raw `(NAKss` is payload `(NAK` plus CRC bytes `73 73`.

Captured raw examples:

- `HSTS` -> `48 53 54 53 0D`
- `HTEMP` -> `48 54 45 4D 50 0D`
- `HBMS1` -> `48 42 4D 53 31 0D`
- `QPRTL` -> `51 50 52 54 4C 0D`
- `HIMSG1` -> `48 49 4D 53 47 31 0D`

`PCP00`, `PCP01`, and `PCP02` are the captured CRC-framed charger-priority writes so far. They use CRC16/XMODEM with the same byte-safety adjustment already used in the local pipsolar code, and the CRC bytes are transmitted high byte first, then low byte. The latest live round-trip on the charger-priority row captured `PCP01` on `OSO -> SNU`, `PCP02` on `SNU -> OSO`, `PCP00` on `OSO -> CSO`, and `PCP02` again on `CSO -> OSO`. The select-state mapping is now closed: `PCP00` = `CSO`, `PCP01` = `SNU`, `PCP02` = `OSO`. `PCP02` is shared by both restore windows, and `PCP03` remains unconfirmed.

Captured CRC reference frames:

- `PCP00` -> `50 43 50 30 30 8D 7A 0D`
- `PCP01` -> `50 43 50 30 31 9D 5B 0D`
- `PCP02` -> `50 43 50 30 32 AD 38 0D`

## Live Select Families

The current release component and decoder also expose the two portal select families that sit above the write surface:

- `Output Source Priority Setting` -> `POP00` / `POP01`
  - the raw capture report pins `POP00` as `50 4F 50 30 30 C2 48 0D`
  - `POP00` = `SUB priority`; `POP01` = `SBU priority` has never been seen on the wire; readback `QPIRI[16]`
  - `POP02` is **not** offered: the manual's program 01 has only SUB/SBU, and on sibling firmwares (Easun, vendor tool) `POP02` is SUF, which feeds power to the grid
- `PV Energy Feeding Priority Setting` -> `PVENGUSE00` / `PVENGUSE01`
  - the live bus capture paired `PVENGUSE00^a` with `BLU` and `PVENGUSE01N@` with `LBU`, both returning raw `(ACK9 ` (`(ACK` plus CRC `39 20`)
  - the current best mapping is `PVENGUSE00` = `BLU` and `PVENGUSE01` = `LBU`
  - keep the family label and mapping together in the decoder until a cleaner raw-byte replay is written out

The inverter clock setter is another CRC-framed family. The observed live payload is `^S???DAT<YYMMDDHHMMSS>`; the three `?` bytes are still opaque selector bytes, and the trailing two bytes are CRC16/XMODEM high-byte-first before `0D`.

Observed clock-set examples:

- `^S???DAT260429200700` -> `5E 53 3F 3F 3F 44 41 54 32 36 30 34 32 39 32 30 30 37 30 30 E8 57 0D`
- `^S???DAT260429200600` -> `5E 53 3F 3F 3F 44 41 54 32 36 30 34 32 39 32 30 30 36 30 30 DF 67 0D`
- response payload -> `^1`

The equalization interval setter is also CRC-framed. The live bus capture used `PBEQP030` / `PBEQP031` for a one-day delta:

- `PBEQP030` -> `50 42 45 51 50 30 33 30 36 12 0D`
- `PBEQP031` -> `50 42 45 51 50 30 33 31 26 33 0D`

The equalization timeout setter is also CRC-framed. The live bus capture used `PBEQOT120` for the current inverter value:

- `PBEQOT120` -> `50 42 45 51 4F 54 31 32 30 88 44 0D`

The battery cut-off voltage setter is also CRC-framed. The live bus capture used `PSDV41.0` as the current probe value:

- `PSDV41.0` -> `50 53 44 56 34 31 2E 30 4A 40 0D`

The battery type setter is also CRC-framed. The live bus capture used a one-step lithium profile change and restore:

- `PBT03` -> `50 42 54 30 33 17 6D 0D`
- `PBT04` -> `50 42 54 30 34 67 8A 0D`

The BMS lock machine battery capacity setter is also CRC-framed. The live bus capture used `BMSSDC021Xe` / `BMSSDC020HD` for a one-step delta:

- `BMSSDC021Xe` -> `42 4D 53 53 44 43 30 32 31 58 65 0D`
- `BMSSDC020HD` -> `42 4D 53 53 44 43 30 32 30 48 44 0D`

The BMS function enable setter is also CRC-framed. The live bus capture used `BMSC00` / `BMSC01` for the Off/On toggle:

- `BMSC00` -> `42 4D 53 43 30 30 48 9E 0D`
- `BMSC01` -> `42 4D 53 43 30 31 58 BF 0D`

The grid-connected current setter is also CRC-framed. The live bus capture used `PGFC021` for a one-amp delta:

- `PGFC021` -> `50 47 46 43 30 32 31 E1 4B 0D`
- `PGFC020` -> `50 47 46 43 30 32 30 F1 6A 0D`

The maximum mains charging current setter is also CRC-framed. The live bus capture used `MUCHGC020` for the current inverter value and `MUCHGC030` as the restore probe:

- `MUCHGC020` -> `4D 55 43 48 47 43 30 32 30 F3 F1 0D`
- `MUCHGC030` -> `4D 55 43 48 47 43 30 33 30 C0 C0 0D`

The equalization time setter is also CRC-framed. The live bus capture used `PBEQT059` / `PBEQT060` for a one-minute delta:

- `PBEQT059` -> `50 42 45 51 54 30 35 39 C7 6C 0D`
- `PBEQT060` -> `50 42 45 51 54 30 36 30 03 16 0D`

The battery float charging voltage setter is also CRC-framed. The live bus capture used `PBFT56.5` / `PBFT56.4` / `PBFT56.3` for a one-step delta, but all probes returned `NAKss`:

- `PBFT56.5` -> `50 42 46 54 35 36 2E 35 A9 18 0D`
- `PBFT56.4` -> `50 42 46 54 35 36 2E 34 B9 39 0D`
- `PBFT56.3` -> `50 42 46 54 35 36 2E 33 C9 DE 0D`

The equalization mode enable row is also bus-confirmed. The live probe used a one-step enable/restore pair:

- `PBEQE1` -> `50 42 45 51 45 31 4A 13 0D`
- `PBEQE0Z2` -> `50 42 45 51 45 30 5A 32 0D`

## Request Table

| Request | Full hex | CRC bytes | CRC type | Terminator |
|---|---|---|---|---|
| `HSTS` | `48 53 54 53 0D` | none | none | `0D` |
| `HGRID` | `48 47 52 49 44 0D` | none | none | `0D` |
| `HOP` | `48 4F 50 0D` | none | none | `0D` |
| `HBAT` | `48 42 41 54 0D` | none | none | `0D` |
| `HPV` | `48 50 56 0D` | none | none | `0D` |
| `HTEMP` | `48 54 45 4D 50 0D` | none | none | `0D` |
| `HBMS1` | `48 42 4D 53 31 0D` | none | none | `0D` |
| `HGEN` | `48 47 45 4E 0D` | none | none | `0D` |
| `QPRTL` | `51 50 52 54 4C 0D` | none | none | `0D` |
| `HIMSG1` | `48 49 4D 53 47 31 0D` | none | none | `0D` |
| `HBMS2` | `48 42 4D 53 32 0D` | none | none | `0D` |
| `HBMS3` | `48 42 4D 53 33 0D` | none | none | `0D` |
| `HEEP1` | `48 45 45 50 31 0D` | none | none | `0D` |
| `HEEP2` | `48 45 45 50 32 0D` | none | none | `0D` |
| `HPVB` | `48 50 56 42 0D` | none | none | `0D` |
| `HEEP3` | `48 45 45 50 33 0D` | none | none | `0D` |
| `HIMSG2` | `48 49 4D 53 47 32 0D` | none | none | `0D` |
| `QPIRI`, `QFLAG`, `QBEQI`, `QMCHGCR`, `QMUCHGCR` | command + CRC + `0D` | 2 | `CRC16/XMODEM` | `0D` |
| `HOSTCRCEN` | command + CRC + `0D` (answered `(CRC16EN` + CRC) | 2 | `CRC16/XMODEM` | `0D` |
| `PCP00` | `50 43 50 30 30 8D 7A 0D` | 2 | `CRC16/XMODEM` | `0D` |
| `PCP01` | `50 43 50 30 31 9D 5B 0D` | 2 | `CRC16/XMODEM` | `0D` |
| `PCP02` | `50 43 50 30 32 AD 38 0D` | 2 | `CRC16/XMODEM` | `0D` |
| `inverterSystemClock` | `5E 53 3F 3F 3F 44 41 54 32 36 30 34 32 39 32 30 30 37 30 30 E8 57 0D` | 2 | `CRC16/XMODEM` | `0D` |
| `PBEQP<DDD>` | `50 42 45 51 50 30 33 30 36 12 0D` | 2 | `CRC16/XMODEM` | `0D` |
| `PBEQOT<DDD>` | `50 42 45 51 4F 54 31 32 30 88 44 0D` | 2 | `CRC16/XMODEM` | `0D` |
| `PSDV<XX.X>` | `50 53 44 56 34 31 2E 30 4A 40 0D` | 2 | `CRC16/XMODEM` | `0D` |
| `PGFC<DDD>` | `50 47 46 43 30 32 31 E1 4B 0D` | 2 | `CRC16/XMODEM` | `0D` |
| `MUCHGC<DDD>` | `4D 55 43 48 47 43 30 32 30 F3 F1 0D` | 2 | `CRC16/XMODEM` | `0D` |
| `PBEQT<DDD>` | `50 42 45 51 54 30 35 39 C7 6C 0D` | 2 | `CRC16/XMODEM` | `0D` |
| `PBFT<XX.X>` | `50 42 46 54 35 36 2E 35 A9 18 0D` | 2 | `CRC16/XMODEM` | `0D` |
| `PBEQE1` | `50 42 45 51 45 31 4A 13 0D` | 2 | `CRC16/XMODEM` | `0D` |
| `PBEQE0` | `50 42 45 51 45 30 5A 32 0D` (shown as `PBEQE0Z2`) | 2 (`5A 32`) | `CRC16/XMODEM` | `0D` |
| `PBEQV<XX.XX>` | `50 42 45 51 56 35 38 2E 33 30 13 1E 0D` | 2 | `CRC16/XMODEM` | `0D` |
| `PDa` | `50 44 61 E3 41 0D` | 2 | `CRC16/XMODEM` | `0D` |
| `PEa` | `50 45 61 D0 70 0D` | 2 | `CRC16/XMODEM` | `0D` |
| `PDx` | `50 44 78 60 59 0D` (shown as ``PDx`Y``) | 2 (`60 59`) | `CRC16/XMODEM` | `0D` |
| `PEx` | `50 45 78 53 68 0D` (shown as `PExSh`) | 2 (`53 68`) | `CRC16/XMODEM` | `0D` |
| `PDy` | `50 44 79 70 78 0D` (shown as `PDypx`) | 2 (`70 78`) | `CRC16/XMODEM` | `0D` |
| `PEy` | `50 45 79 43 49 0D` (shown as `PEyCI`) | 2 (`43 49`) | `CRC16/XMODEM` | `0D` |

## Additional reads (2026-11-16)

| Command | Framing | Live reply (HPVINV02 fw 40.05) | Decode | Source |
|---|---|---|---|---|
| `HIMSG2` | ASCII + CR | rated inverter / MPPT / AC power: `6200` / `8500` / `6200` W in Battery mode; `6200` / `8500` / `9372` W in Mains mode | `[0]` rated inverter power, `[1]` rated MPPT (PV) power, `[2]` AC power capacity (entity "AC Power Capacity", key `rated_ac_power`): the 6200 W rating in Battery mode, but the AC input voltage × 40 A in Mains mode (`9372` at 234.2 V, `9336` at 233.4 V), so it is live, not a rating. `[3]` stayed `6200`. The vendor tool uses `[1]` and `[2]` as gauge denominators. `8500` W matches the manual's 8.5 kW PV maximum. | vendor tool `HIMSG2_PDF_ViewModel.cs`; captures 2026-11-16 (Battery and Mains mode) |
| `QMCHGCR` | PI30, CRC | `(010 020 030 040 050 060 070 080` | allowed `MNCHGC` values; the component validates writes against this list | log-proven 2026-04-25 and 2026-11-16 |
| `QMUCHGCR` | PI30, CRC | `(002 010 020 030 040 050 060` | allowed `MUCHGC` values | same |

## Probing unpolled commands

`packages/solarplug-write-beta.yaml` adds two ESPHome API actions. In HA, run them from Developer tools > Actions as `esphome.<node>_probe_read` / `esphome.<node>_probe_write`. The raw reply, with the status (`supported`, `nak`, `timeout`, `crc_error`, `invalid_frame`, `ack`), appears on the **Probe Result** text sensor.

- `probe_read` takes any command the component polls, or one of the unpolled read-only queries on its allow-list:
  - **H reads** with no decoder: `HEEP4`, `HSTS3`. `HSTS2` and `HCTMSG1` are decoded and polled on the models that have them, and can also be probed.
  - **PI30/PI17 identity and status queries**, sent with CRC: `QPI`, `QMN`, `QGMN`, `QID`, `QSID`, `QVFW`, `QVFW2`, `QVFW3`, `QPIGS`, `QPIGS2`, `QMOD`, `QPIWS`, `QDI`, `QOPPT`, `QCHPT`, `QT`.

  Anything else is refused. Setters are never on the list.
- `probe_write` accepts only `HOSTCRCEN` and `HOSTCRCDN`, and needs `enable_writes`.

Unsupported probes time out; they never NAK. A probe is not retried, and its timeout or NAK is left out of the bus-health counters.

Probe sweep on 2026-11-16 (HPVINV02 fw 40.05, battery mode):

| Command | Result | Notes |
|---|---|---|
| `HEEP3` | **answers**: `(2048 2048 2048 0150 000…0` (5 tokens) | The vendor tool polls it only for HPVINV07/08. There, `[3]` is the CT zero-adjust power (`EZCTP`, 4 digits), and `0150` fits that format; unconfirmed here. `[0]`–`[2]` are unknown. Now polled with the settings tier: HEEP3 Raw Payload and CT Zero Adjust Power, both disabled by default. |
| `HCTMSG1`, `HSTS2`, `HEEP4`, `HSTS3` | timeout | Not on this firmware |
| `QPI` | `(PI30` | protocol ID |
| `QMN` | `(VMII-6200` | model |
| `QVFW` / `QVFW2` | `(VERFW:00040.05` / `(VERFW2:00000.00` | |
| `QID` / `QSID` | 14- and 22-digit serials | Values are not recorded in this public repo |
| `QPIGS` | full PI30 general status | Duplicates the H telemetry. The battery voltage has 2 decimals (`52.90`). |
| `QMOD` | `(B` | matches HSTS mode `B` |
| `QPIWS` | `(1000…0` (32 bits) | bit 0 set at dusk (PV 76 V, no PV current). Its meaning on this firmware is unconfirmed. |
| `QDI` | `(230.0 50.0 0030 42.0 54.0 56.4 46.0 50 0 0 2 0 0 0 0 0 1 1 1 0 1 0 54.0 0 1` | Factory defaults, unchanged since April. In PI30 order, `[11]` battery type = `0`, and the vendor enum says 0 = AGM. |
| `QGMN`, `QVFW3`, `QPIGS2`, `QOPPT`, `QCHPT`, `QT` | timeout | `QPIGS2` and `QT` had already timed out in April |

## Response CRC mode (`HOSTCRCEN` / `HOSTCRCDN`)

Tested on 2026-11-16 (HPVINV02 fw 40.05). Evidence: `runs/20261116-response-crc-mode/` in the research tree.

| Step | Frame | Reply | Effect |
|---|---|---|---|
| enable | `HOSTCRCEN` + CRC | `(CRC16EN` + CRC (not `(ACK`) | Every H reply then ends with a CRC16/XMODEM (same byte adjustment) over `(`…payload, just before the CR. Example: HSTS `…127000000` becomes `…127000000ei`. |
| disable | `HOSTCRCDN` + CRC | `(CRC16DN` + CRC | The next H reply is plain again. |
| no action | — | — | **The mode expires on its own about 64–65 s after `HOSTCRCEN`** (first plain reply after 65.0 s and 64.2 s in two runs). It is RAM state, not a persistent setting, which is why the vendor tool re-sends `HOSTCRCEN` every poll cycle. |

How the component handles it:
- `(CRC16EN` / `(CRC16DN` count as the ACK for those two frames.
- Every H reply is checked for a trailing CRC. 3 replies in a row with a valid CRC switch CRC stripping on; 3 without switch it off. A frame whose framing is ambiguous during a switch is dropped, not decoded. A natural expiry therefore costs 3 dropped frames (counted as frame errors) and no bad values.
- Text states are reduced to printable ASCII before publishing.

The first test, before these fixes, caused an outage. The firmware did not recognise `(CRC16EN`, so it kept parsing without stripping the CRC. The CRC bytes then reached text states, for example QPRTL `HPVINV02` plus 2 non-UTF-8 bytes. HA's ESPHome client rejects such a state and drops the connection on every reconnect. HA had no inverter data for about 5 minutes, from 17:58:37 to 18:03:50, until the fixed firmware was flashed over OTA. The inverter itself was unaffected.

### Always-on CRC (`response_crc: true`, set in `packages/solarplug-read-full.yaml`)

- **Keep-alive:** the component sends `HOSTCRCEN` once before the boot poll, then every 30 s. The mode lapses about 64 s after each one, so a single lost keep-alive is harmless. The keep-alive jumps the command queue, never touches Last Write Status, and logs only at debug level.
- **Strict checking:** while the mode is on, an H reply without a valid CRC is dropped as `crc_error` (and retried once), and a `HOSTCRCEN` refresh goes out within 5 s.
- **Fallback:** if 3 H replies in a row arrive without CRC, the inverter is not honouring the mode. That third reply is dropped too, then plain parsing resumes so data keeps flowing. The component logs a warning and keeps re-sending `HOSTCRCEN`.
- **Backoff:** after 3 keep-alives in a row go unanswered while reads still succeed (firmware without the mode), `HOSTCRCEN` is retried only every 10 min. A bus outage does not count, and any acknowledged `HOSTCRCEN` resets the backoff. Keep-alive and probe failures stay out of the bus-health counters; the Response CRC state shows them instead.
- **Writes:** the keep-alive is sent even with `enable_writes: false`. It changes no setting, only the framing of the replies for the next ~64 s, which is what the vendor tool does on every poll.
- **Status:** the **Response CRC** text sensor shows one of:
  - `On`: replies are CRC-checked;
  - `Starting`: before the first keep-alive is answered;
  - `Lost`: the keep-alive is not honoured and replies are unverified;
  - `Off`: not configured;
  - `On (probe)`: turned on through `probe_write` without the keep-alive.
- **Cost:** one 12-byte frame and an 11-byte reply every 30 s, plus 2 bytes per H reply: about 1 % of the bus.
- **Code:** the mode logic is `codec::ResponseCrcTracker` and `codec::crc_keepalive_due()`, host-tested in `tests/cpp/codec_harness.cpp`.

`probe_write HOSTCRCDN` is refused while `response_crc: true`. Without the keep-alive, an acknowledged `HOSTCRCDN` turns the mode off and it stays off.

## Command Table

| Request | Response example | Decoded fields | Confidence | Matching Solar of Things keys |
|---|---|---|---|---|
| `HSTS` | `(00 L010000000000 11211001000L112000000`; live replacement capture also observed `(00 B010000000000 10211002100B127000000` | status code; mode code; mode label; status bits; fault bits. `L` maps to `Mains Mode`; `B` maps to `Battery Mode` but stays medium-confidence until same-time portal confirmation. | high for status fields; medium for `B` label | `Mode`, `Status Code`, `Warnings Present`, `Faults Present` |
| `HGRID` | `(240.2 49.9 280 090 70 40 +00291 0 06500 11+00000` | AC input voltage; mains frequency; high/low mains-loss voltage; high/low mains-loss frequency; mains power; mains current flow direction code (`0` import, `1` export; it stays `0` when idle, so the component reports `No Flow` while mains power is 0 W); rated power | high | `AC input voltage`, `Mains Frequency`, `Mains Current Flow Direction`, `Mains Power` |
| `HOP` | `(240.2 49.9 00216 00177 003 006 06200 005.9 00107` | AC output voltage; output frequency; apparent power; active power; load percent; DC component/status; rated power; inductor current | high | `Output Voltage`, `Output Frequency`, `Output Apparent Power`, `Output Active Power`, `Output Load Percent` |
| `HBAT` | `(04 053.5 083 001 00000 393 101002010000 00000000` | battery series count (12 V blocks; **not** the battery type, which is `HEEP1[3]` d2); battery voltage; battery capacity; charge current; discharge current; BUS voltage; charge switches (`[6]` d0–2) | high | `Battery Voltage`, `Battery Capacity`, `Battery Charging Current`, `Battery Discharge Current`, `BUS Voltage` |
| `HPV` | `(000.0 00.0 00000 00000.0 00000 0 060.0 027 08500`; 2026-05-01 active replacement recorder also observed nonzero PV values | PV voltage; PV current; PV power; token 4 generation-power candidate | high for voltage/current/power; low for token 4 | `PV Voltage`, `PV Current`, `PV Power`; `Generation Power` candidate only |
| `HTEMP` | `(020 028 022 026 028 030 030 11000000000000000000` | PV temperature; inverter temperature; boost temperature; transformer temperature; max temperature; fan 1 speed; fan 2 speed; `[7]` d0 fan enable (order matched to the 2026-04-29 portal snapshot) | high | `Inverter Temperature`, `Boost Temperature`, `Transformer Temperature`, `PV Temperature`, `Fan 1 Speed`, `Fan 2 Speed`, `Max. Temperature` |
| `HBMS1` | `(02 1001100000000000 044.8 057.6 150.0 083 0001.7 0000.0 02946 000000` | BMS status code; BMS flags; discharge limit; charge limit; charge-current limit; SOC; charging current; discharge current | medium-high for SOC/current after independent BMS cross-check; medium for flags/limits | `BMS Charge Current Limit`, `BMS Charge Voltage Limit`, `BMS Charging Current`, `BMS Communication Control Function`, `BMS Communication Normal`, `BMS Discharge Current`, `BMS Discharge Voltage Limit`, `BMS Low Battery Alarm Flag`, `BMS Low Power SOC`, `BMS Returns To Battery Mode SOC`, `BMS Returns To Mains Mode SOC` |
| `HGEN` | `(260429 23:02 03.043 0059.4 0066.4 000000066.4 000000000000` | date; ISO date; time; daily generation; monthly generation; yearly generation; total generation | high; daily/monthly rollover confirmed | `Daily Power Gen.`, `Monthly Electricity Generation`, `Yearly Electricity Generation`, `Total Power Generation`, `dailyProducedQuantity`, `monthlyProducedQuantity`, `yearlyProducedQuantity`, `totalProducedQuantity`, `pvGeneratedEnergyOfDay`, `currentMonthPvGenerationReadDirectly`, `totalPvGenerationReadDirectlyK` |
| `QPRTL` | `(HPVINV02` | protocol type / device type string | medium | `Device Type`, `deviceType` |
| `HIMSG1` | `(0040.05 20250923 12` | software version; software date; ISO software date; revision | high | `Software Version`, `softwareVersion` |
| `HBMS2` | `(0000.0 0000.0 1 3351 0015 3350 0016 0000000000000000000000` | remaining/nominal capacity placeholders; display mode; max/min voltage summary; max/min cell position | medium for min/max voltage summary; low for capacity fields on this setup | `Remaining Capacity`, `Nominal Capacity`, `Display Mode`, `Max Voltage`, `Min Voltage`, `Max Voltage Cell Position`, `Min Voltage Cell Position` |
| `HBMS3` | `(0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 00000000` | 16-cell voltage slots; raw tail | low on this setup; slots are zeroed while the independent BMS node reports real cell voltages | `Cell voltage list` |
| `HEEP1` | `(1 060 030 03410110230 012 1 1 0 0 1 020 025 090 050 056.4 056.4 042.0 020 0 0` | settings snapshot; slot map in [HEEP_SETTINGS_MAP.md](HEEP_SETTINGS_MAP.md) | high for mapped slots | primary readback for most write controls |
| `HEEP2` | `(0 044.0 020 044.0 046.0 054.0 0 058.4 060 120 030 0000 0000 05 0000 52.0 50000` | settings snapshot; slot map in [HEEP_SETTINGS_MAP.md](HEEP_SETTINGS_MAP.md) | high for mapped slots | recharge/redischarge, equalization, second-output delay/recovery/restore SOC |
| `HPVB` | `(000.0 00.0 00000 0 380.0 00000000000000000000000` | PV voltage; PV current; PV power; PV charging mark; bus voltage; raw tail | medium | `pvVoltage`, `pvCurrent`, `pvPower`, `pvChargingMark`, `busVoltage` |
| `inverterSystemClock` | `^1` | clock selector; clock digits; clock ISO; portal key; portal label; portal value; ack observed; ack response | high | `Inverter System Clock` / `inverterSystemClock` |
| `batteryEqualizationIntervalSetting` | raw `(ACK9 ` -> payload `(ACK` + CRC `39 20` | requested value; portal key; portal label; portal value; ack observed; ack response | high | `Battery Equalization Interval Setting` / `batteryEqualizationIntervalSetting` |
| `restoreSecondOutputBatCapacitySetting` | raw `(ACK9 ` / raw `(NAKss` -> payload `(ACK` / `(NAK` plus CRC | requested value; portal key; portal label; portal value; ack observed; ack response | medium | `Restore Second Output Battery Capacity Setting` / `restoreSecondOutputBatCapacitySetting` |
| `batteryRechargeVoltageSetting` | raw `(ACK9 ` -> payload `(ACK` + CRC `39 20` | requested value; portal key; portal label; portal value; ack observed; ack response | high | `Battery Recharge Voltage Setting` / `batteryRechargeVoltageSetting` |
| `batteryRedischargeVoltageSetting` | raw `(ACK9 ` -> payload `(ACK` + CRC `39 20` | requested value; portal key; portal label; portal value; ack observed; ack response | high | `Battery Redischarge Voltage Setting` / `batteryRedischargeVoltageSetting` |
| `batteryEqualizationVoltageSetting` | raw `(ACK9 ` -> payload `(ACK` + CRC `39 20` | requested value; portal key; portal label; portal value; ack observed; ack response | high | `Battery Equalization Voltage Setting` / `batteryEqualizationVoltageSetting` |
| `batteryEqualizationModeEnableSetting` | raw `(ACK9 ` -> payload `(ACK` + CRC `39 20` | requested value; portal key; portal label; portal value; ack observed; ack response | high | `Battery Equalization Mode Enable Setting` / `batteryEqualizationModeEnableSetting` |

The slots that are still unmapped are listed under "Still unmapped" in [HEEP_SETTINGS_MAP.md](HEEP_SETTINGS_MAP.md).

## Confirmed write frames

The first write-side control mapping we have proven on the live inverter is the portal `Buzzer On` row. The response column below preserves raw bus text; decode tooling strips validated response CRC bytes before classifying ACK/NAK.

| Frame | Portal field | Value | Response | HEEP1 effect | Confidence |
|---|---|---|---|---|---|
| `PDa` | `buzzerOn` / `Buzzer On` | Disable | `(ACK9 ` | `HEEP1[5]`: `1 -> 0` | high |
| `PEa` | `buzzerOn` / `Buzzer On` | Enable | `(ACK9 ` | `HEEP1[5]`: `0 -> 1` | high |
| ``PDx`Y`` | `backlightOn` / `Backlight On` | Disable | `(ACK9 ` | `HEEP1[6]`: `1 -> 0` | high |
| `PExSh` | `backlightOn` / `Backlight On` | Enable | `(ACK9 ` | `HEEP1[6]`: `0 -> 1` | high |
| `PDk` | `displayAutomaticallyReturnsToHomepage` / `Display Automatically Returns To Homepage` | Disable | `(ACK9 ` | zero-based `HEEP1[4]`: `012 -> 002` | high |
| `PEkq:` | `displayAutomaticallyReturnsToHomepage` / `Display Automatically Returns To Homepage` | Enable | `(ACK9 ` | zero-based `HEEP1[4]`: `002 -> 012` | high |
| `PBEQP030` | `batteryEqualizationIntervalSetting` / `Battery Equalization Interval Setting` | `30` | `(ACK9 ` | zero-based `HEEP2[10]`: `031 -> 030` restore | high |
| `PBEQP031` | `batteryEqualizationIntervalSetting` / `Battery Equalization Interval Setting` | `31` | `(ACK9 ` | zero-based `HEEP2[10]`: `030 -> 031` | high |
| `PBEQOT119` | `batteryEqualizationTimeoutSetting` / `Battery Equalization Timeout Setting` | `119` | `(ACK9 ` | zero-based `HEEP2[9]`: `120 -> 119` | high |
| `PBEQOT120` | `batteryEqualizationTimeoutSetting` / `Battery Equalization Timeout Setting` | `120` | `(ACK9 ` | zero-based `HEEP2[9]`: `119 -> 120` restore | high |
| `PSDV41.0` | `batteryCutOffVoltageSetting` / `Battery Cut Off Voltage Setting` | `41.0` | `(NAKss` | tested value rejected; HEEP1 token 17 still points at the cutoff row | high |
| `BMSSDC021Xe` | `bmsLockMachineBatteryCapacitySetting` / `BMS Lock Machine Battery Capacity (%)` | `21` | `(NAKss` | one-step raise rejected; current value remains 20 | medium |
| `BMSSDC020HD` | `bmsLockMachineBatteryCapacitySetting` / `BMS Lock Machine Battery Capacity (%)` | `20` | `(ACK9 ` | restore probe; current value accepted | medium |
| `BMSC00` | `bmsFunctionEnableSetting` / `BMS Function Enable Setting` | Off | `(ACK9 ` | change probe; accepted as Off on this firmware | high |
| `BMSC01` | `bmsFunctionEnableSetting` / `BMS Function Enable Setting` | On | `(ACK9 ` | restore probe; accepted as On on this firmware | high |
| `PBT03` | `batteryTypeSetting` / `Battery Type Setting` | `LIA` | `(ACK9 ` | change probe; accepted as lithium-ion profile on this firmware | medium-high |
| `PBT04` | `batteryTypeSetting` / `Battery Type Setting` | `PYL` | `(ACK9 ` | restore probe; accepted as Pylontech profile on this firmware | medium-high |
| `PGFC021` | `gridConnectedCurrentSetting` / `Grid Connected Current Setting` | `21` | `(ACK9 ` | zero-based `HEEP1[17]`: `020 -> 021` | high |
| `PGFC020` | `gridConnectedCurrentSetting` / `Grid Connected Current Setting` | `20` | `(ACK9 ` | zero-based `HEEP1[17]`: `021 -> 020` restore | high |
| `MUCHGC020` | `maximumMainsChargingCurrentSetting` / `Maximum Mains Charging Current Setting` | `20` | `(ACK9 ` | family confirmed on the live bus; trim round-trip to 20 A succeeded | high |
| `MUCHGC030` | `maximumMainsChargingCurrentSetting` / `Maximum Mains Charging Current Setting` | `30` | `(ACK9 ` | restore probe; family confirmed on the live bus | high |
| `MUCHGC040` | `maximumMainsChargingCurrentSetting` / `Maximum Mains Charging Current Setting` | `40` | `(ACK9 ` | zero-based `HEEP1[2]`: `030 -> 040 -> 030` restore loop | high |
| `MNCHGC050` | `maximumChargingCurrentSetting` / `Maximum Charging Current Setting` | `50` | `(ACK9 ` | family confirmed on the live bus; trim round-trip to 50 A succeeded | high |
| `MNCHGC060` | `maximumChargingCurrentSetting` / `Maximum Charging Current Setting` | `60` | `(ACK9 ` | restore probe; family confirmed on the live bus | high |
| `MNCHGC070` | `maximumChargingCurrentSetting` / `Maximum Charging Current Setting` | `70` | `(ACK9 ` | zero-based `HEEP1[1]`: `060 -> 070`, preserved after ESPHome node restart | high |
| `MNCHGC080` | `maximumChargingCurrentSetting` / `Maximum Charging Current Setting` | `80` | `(ACK9 ` | zero-based `HEEP1[1]`: `070 -> 080 -> 070` restore loop | high |
| `PBEQT059` | `batteryEqualizationTimeSetting` / `Battery Equalization Time Setting` | `59` | `(ACK9 ` | zero-based `HEEP2[8]`: `060 -> 059` | high |
| `PBEQT060` | `batteryEqualizationTimeSetting` / `Battery Equalization Time Setting` | `60` | `(ACK9 ` | zero-based `HEEP2[8]`: `059 -> 060` restore | high |
| `PBFT56.5` | `batteryFloatChargingVoltageSetting` / `Battery Float Charging Voltage Setting` | `56.5` | `(NAKss` | bus family observed; tested values rejected | medium |
| `PBFT56.4` | `batteryFloatChargingVoltageSetting` / `Battery Float Charging Voltage Setting` | `56.4` | `(NAKss` | restore probe; tested values rejected | medium |
| `PBFT56.3` | `batteryFloatChargingVoltageSetting` / `Battery Float Charging Voltage Setting` | `56.3` | `(NAKss` | additional probe; tested values rejected | medium |
| `PBEQE1` | `batteryEqualizationModeEnableSetting` / `Battery Equalization Mode Enable Setting` | On | `(ACK9 ` | readback `QBEQI[0]` (VENDOR: `HEEP2[6]`); not toggle-tested on lithium | high |
| `PBEQE0Z2` | `batteryEqualizationModeEnableSetting` / `Battery Equalization Mode Enable Setting` | Off | `(ACK9 ` | readback `QBEQI[0]` = 0 | high |
| `PDu` | `overloadAutomaticRestart` / `Overload Automatic Restart` | Disable | `(ACK9 ` | `HEEP1[3]` d4 (0 = portal Disable); `QFLAG` u | high |
| `PEu` | `overloadAutomaticRestart` / `Overload Automatic Restart` | Enable | `(ACK9 ` | `HEEP1[3]` d4 `0 -> 1` (decode pinned in the tests; ledger 2026-11-16) | high |
| `PDypx` | `inputSourceDetectionPromptSound` / `Input Source Detection Prompt Sound` | Disable | `(ACK9 ` | `HEEP1[3]` d5 `1 -> 0` (2026-04-30) | high |
| `PEyCI` | `inputSourceDetectionPromptSound` / `Input Source Detection Prompt Sound` | Enable | `(ACK9 ` | `HEEP1[3]` d5 `0 -> 1` | high |

### Parameterized write family

The inverter clock is a parameterized, CRC-framed write family rather than a fixed short token. The live probe used a one-minute step forward and restore:

| Family | Example frame | Portal field | Example value | Ack | HEEP1 effect | Confidence |
|---|---|---|---|---|---|---|
| `^S???DAT<YYMMDDHHMMSS>` | `^S???DAT260429200700` | `inverterSystemClock` / `Inverter System Clock` | `2026-04-29 20:07:00` | `^1` | no confirmed HEEP delta in current capture | high |
| `^S???DAT<YYMMDDHHMMSS>` | `^S???DAT260429200600` | `inverterSystemClock` / `Inverter System Clock` | `2026-04-29 20:06:00` | `^1` | restore probe; no confirmed HEEP delta | high |
| `PDSRS<DDD>` | `PDSRS051` | `restoreSecondOutputBatCapacitySetting` / `Restore Second Output Battery Capacity Setting` | `51` | `(NAKss` | rejected at 51; restore probe `PDSRS050` returned `(ACK9 ` | medium |
| `PDSRS<DDD>` | `PDSRS050` | `restoreSecondOutputBatCapacitySetting` / `Restore Second Output Battery Capacity Setting` | `50` | `(ACK9 ` | restore probe; readback is `HEEP2[16]` d0–1 (round-trip `30000 → 35000 → 30000`, 2026-11-16) | medium |
| `PBEQV<XX.XX>` | `PBEQV58.30` | `batteryEqualizationVoltageSetting` / `Battery Equalization Voltage Setting` | `58.3` | `(ACK9 ` | zero-based `HEEP2[7]`: `058.4 -> 058.3` | high |
| `PBEQV<XX.XX>` | `PBEQV58.40` | `batteryEqualizationVoltageSetting` / `Battery Equalization Voltage Setting` | `58.4` | `(ACK9 ` | zero-based `HEEP2[7]`: `058.3 -> 058.4` restore | high |
| `PDDLYT<DDD>` | `PDDLYT006` | `restoreSecondOutputDelayTimeSetting` / `Restore Second Output Delay Time Setting` | `6` | `(NAKss` | rejected at 6; restore probe `PDDLYT005` returned `(ACK9 ` | medium |
| `PDDLYT<DDD>` | `PDDLYT004` | `restoreSecondOutputDelayTimeSetting` / `Restore Second Output Delay Time Setting` | `4` | `(NAKss` | rejected at 4; zero-based `HEEP2[13]` stayed `05` | medium |
| `PDDLYT<DDD>` | `PDDLYT005` | `restoreSecondOutputDelayTimeSetting` / `Restore Second Output Delay Time Setting` | `5` | `(ACK9 ` | accepted restore value; zero-based `HEEP2[13]` reads `05` | medium |

## Notes

- `HGEN` is the key proof-backed block for today / month / year / total PV generation. Daily and monthly reset behavior is documented in [HGEN_ROLLOVER.md](HGEN_ROLLOVER.md).
- `QPRTL` is the best current match for the `HPVINV02` device-type block. The 2026-04-30 active replacement boot captures showed the first identity command after boot can time out once while the bus wakes. The firmware now retries read-only active commands once on timeout.
- `HIMSG1` matches the version/date block used by the portal.
- `HPV` voltage/current/power are now confirmed under daytime production on the active Atom S3 Lite replacement path. In the same Home Assistant recorder snapshot, `pv_power` was live at `569 W` while the token-4 `generation_power_kw` entity stayed stale/zero, so token 4 is diagnostic-only until a better field match is proven.
- `HBMS1`, `HBMS2`, and `HBMS3` decode structurally, but they represent the inverter's PYL/BMS summary view rather than the direct BMS protocol. `HBMS1` SOC/current now match the independent BMS node after rounding; `HBMS2` is useful as a min/max cell summary; `HBMS3` remains low-value on this setup because the inverter returns zeroed cell slots while the independent BMS node reports real cells.
- `HEEP1` and `HEEP2` are the inverter's settings snapshots. Their slots are mapped from one-setting write deltas in [HEEP_SETTINGS_MAP.md](HEEP_SETTINGS_MAP.md), and the component seeds the write controls from them. The slots that are still unmapped are listed there too.
- The live `Buzzer On` write probe is the first confirmed portal write that maps back to the inverter-side config stream: `PDa` disables it, `PEa` enables it, and zero-based `HEEP1[5]` flips accordingly.
- `Backlight On` is now the second confirmed portal write mapping. The bus accepts ``PDx`Y`` to disable and `PExSh` to enable, both return `(ACK9 `, and zero-based `HEEP1[6]` flips `1 -> 0 -> 1`.
- `Display Automatically Returns To Homepage` has now been re-validated in a live safe round-trip. The disable frame on the wire is `50 44 6B 42 0B 0D` and the restore frame is `50 45 6B 71 3A 0D`; both return `(ACK9 `, and zero-based `HEEP1[4]` flips `012 -> 002 -> 012`.
- `Input Source Detection Prompt Sound` is now the third confirmed portal write mapping. The inverter accepts `PDypx` to disable and `PEyCI` to enable, both return `(ACK9 `, and digit 5 of zero-based `HEEP1[3]` flips `1 -> 0 -> 1` (`QFLAG` letter `y` agrees).
- `Battery Equalization Interval Setting` is now readback-confirmed as `PBEQP<DDD>`. The latest ESPHome web API loop used `PBEQP031` and `PBEQP030` for a one-day delta, both returned `(ACK9 `, and zero-based `HEEP2[10]` changed `030 -> 031 -> 030`.
- `Battery Equalization Timeout Setting` is now readback-confirmed as `PBEQOT<DDD>`. The latest ESPHome web API loop used `PBEQOT119` and `PBEQOT120`, both returned `(ACK9 `, and zero-based `HEEP2[9]` changed `120 -> 119 -> 120`.
- `Battery Equalization Time Setting` is now readback-confirmed as `PBEQT<DDD>`. The latest ESPHome web API loop used `PBEQT059` and `PBEQT060`, both returned `(ACK9 `, and zero-based `HEEP2[8]` changed `060 -> 059 -> 060`.
- `BMS Lock Machine Battery Capacity (%)` is now bus-confirmed as `BMSSDC<DDD>`. The live capture used `BMSSDC021Xe` and `BMSSDC020HD` for a one-step delta, the former returned `NAKss` and the latter returned `(ACK9 `, so keep the row disabled by default until the accepted range is proven.
- `Battery Equalization Mode Enable Setting` is now bus-confirmed as a binary row with live wire forms `PBEQE1` and `PBEQE0Z2`. The portal snapshot renders the row as `Off` / `On`, and both directions returned `(ACK9 `. The component reads the state from `QBEQI[0]` (PI30 field order; reads `0` while Off). That readback is not toggle-tested.
- `Battery Equalization Voltage Setting` is now readback-confirmed. The latest ESPHome web API loop used `PBEQV58.30` and `PBEQV58.40`, both return `(ACK9 `, and zero-based `HEEP2[7]` changed `058.4 -> 058.3 -> 058.4`.
- `Grid Connected Current Setting` is now readback-confirmed as `PGFC<DDD>`. The latest ESPHome web API loop used `PGFC021` and `PGFC020`, both returned `(ACK9 `, and zero-based `HEEP1[17]` changed `020 -> 021 -> 020`.
- `Maximum Mains Charging Current Setting` is now readback-confirmed as `MUCHGC<DDD>`. The live captures used `MUCHGC020`, `MUCHGC030`, and `MUCHGC040`; the latest ESPHome web API loop proved zero-based `HEEP1[2]` changes and restored the inverter to `030`.
- `Maximum Charging Current Setting` is now readback-confirmed as `MNCHGC<DDD>`. The live captures used `MNCHGC050`, `MNCHGC060`, `MNCHGC070`, and `MNCHGC080`; the latest ESPHome web API loop proved zero-based `HEEP1[1]` changes and restored the inverter to `070`.
- `Battery Recharge Voltage Setting` is now readback-confirmed as `PBCV<XX.X>`. The latest ESPHome web API loop used `PBCV45.0` and `PBCV46.0`, both returned `(ACK9 `, and zero-based `HEEP2[4]` changed `046.0 -> 045.0 -> 046.0`.
- `Battery Redischarge Voltage Setting` is now readback-confirmed as `PBDV<XX.X>`. The latest ESPHome web API loop used `PBDV53.0` and `PBDV54.0`, both returned `(ACK9 `, and zero-based `HEEP2[5]` changed `054.0 -> 053.0 -> 054.0`.
- `Battery Float Charging Voltage Setting` is now bus-observed as `PBFT<XX.X>`. The live capture used `PBFT56.5`, `PBFT56.4`, and `PBFT56.3`, all returned `NAKss`, so keep it research-only until the accepted range or mode is proven.
- `PV Energy Feeding Priority Setting` is now bus-observed as `PVENGUSE<00>`. The live capture paired `PVENGUSE00^a` with `BLU` and `PVENGUSE01N@` with `LBU`; both returned `(ACK9 `, and digit 3 of zero-based `HEEP1[3]` reads back the state (0 = `BLU`, 1 = `LBU`).
- `Battery Constant Charging Voltage Setting` (`PCVV<XX.X>`, bulk voltage) has never been sent on the bus. It is encodable only in the unsafe profile; its twins `PBFT` and `PSDV` NAK with a lithium battery type.
- Treat the long head tokens as packed composite blocks rather than ordinary scalar fields.
- External lead, unverified here: a PowMr reverse-engineering repo reports that the stock dongle polls at 2400 8N1 over Modbus (slave id 5, register windows 4501–4545 and 4546–4561). This component does not use Modbus.
- `HPVB` is the current best match for live PV / bus telemetry and now decodes as a medium-confidence block. The 2026-05-01 Atom S3 Lite OTA run captured it actively as `(000.0 00.0 00000 0 380.0 00000000000000000000000`; daytime validation is still needed for nonzero PV values.
- The latest portal cross-check shows that the station-overview generation values can drift relative to the direct `HGEN` frame; use same-snapshot portal data when validating totals.

## Field validation notes

### Energy counters come from HGEN, not PI30

The PI30 energy queries probed in April 2026 timed out on this firmware; the rest of the `QE*`/`QL*` family was not tried. The daily, monthly, yearly and total PV counters come from `HGEN` (see [HGEN_ROLLOVER.md](HGEN_ROLLOVER.md)). Cloud or station totals are comparison data, not proof of a local serial counter.

### 2026-05-01 recorder snapshot

Taken with the Atom S3 replacement in place of the dongle, so the portal was not a same-time source.

Latest recorder/API-log snapshot from the replacement path on 2026-05-01:

| Field | Latest value | Interpretation |
|---|---:|---|
| `ac_input_voltage` | `239.6 V` | live `HGRID` input voltage |
| `mains_power` | `484 W` | live `HGRID` mains power |
| `pv_voltage` | `75.4 V` | daytime `HPV` voltage now observed nonzero |
| `pv_current` | `10.0 A` | daytime `HPV` current now observed nonzero |
| `pv_power` | `756 W` | daytime `HPV` power now observed nonzero |
| `pv_generation_power` | stale/zero | HPV token 4 did not track nonzero PV production; keep diagnostic |
| `daily_power_gen` | `2.348 kWh` | live `HGEN` daily counter |
| `monthly_electricity_generation` | `2.3 kWh` | live `HGEN` monthly counter after May reset |
| `yearly_electricity_generation` | `71.3 kWh` | live `HGEN` yearly counter, continued across month boundary |
| `total_power_generation` | `71.3 kWh` | live `HGEN` lifetime-style counter |

This promotes the basic daytime `HPV` token mapping for voltage/current/power to high confidence on the confirmed setup. HPV token 4, previously labeled generation power, did not track nonzero PV production in the Home Assistant recorder snapshot and remains diagnostic. The remaining `HPV` raw-tail tokens and `HPVB` still need separate validation.

### BMS data

The independent BMS nodes are the authority for pack telemetry and cell voltages. `HBMS1`/`HBMS2` are the inverter's summarized view of the PYL link. `HBMS3` cell slots have been seen zeroed while the packs report real cells.
