# HGEN Rollover Behavior

`HGEN` is the native generation counter block:

```text
(YYMMDD HH:MM daily_kWh monthly_kWh yearly_kWh total_kWh reserved
```

The field meanings are already high confidence. This page tracks reset behavior separately because reset behavior affects Home Assistant state-class choices.

## Confirmed Behavior

### Daily Reset

Both captures around the April 29 to 30 boundary read 0.000 kWh daily (after sunset and before sunrise), so they show month, year and total unchanged across midnight but not the reset itself. The daily reset is shown in the monthly table below: daily goes from `2.600 kWh` to `0.000 kWh` across the boundary.

Observed examples:

| Inverter date/time | Daily | Monthly | Yearly | Total | Evidence |
|---|---:|---:|---:|---:|---|
| `2026-04-29 20:07` | `0.000 kWh` | `59.4 kWh` | `66.4 kWh` | `66.4 kWh` | local HGEN capture |
| `2026-04-30 00:55` | `0.000 kWh` | `59.4 kWh` | `66.4 kWh` | `66.4 kWh` | local HGEN capture |

The exact reset instant depends on the inverter clock, not the Home Assistant host clock.

### Monthly Reset

Home Assistant recorder history from the active ESPHome replacement captured the April to May boundary. Times below are UTC; local Lisbon time was one hour ahead on this date.

| HA UTC timestamp | Daily | Monthly | Yearly | Total | Interpretation |
|---|---:|---:|---:|---:|---|
| `2026-04-30 23:51:00` | `2.600 kWh` | `62.0 kWh` | `69.0 kWh` | `69.0 kWh` | April counters before reset |
| `2026-05-01 05:32:11` | `0.000 kWh` | `0.0 kWh` | `69.0 kWh` | `69.0 kWh` | daily/monthly reset confirmed |
| `2026-05-01 08:32:12` | `0.135 kWh` | `0.1 kWh` | `69.1 kWh` | `69.1 kWh` | May counters accumulating |
| `2026-05-01 11:32:12` | `1.745 kWh` | `1.7 kWh` | `70.7 kWh` | `70.7 kWh` | May counters still accumulating |
| `2026-05-01 13:51:12` | `2.348 kWh` | `2.3 kWh` | `71.3 kWh` | `71.3 kWh` | live ESPHome API log from replacement firmware |

This confirms:

- `daily_power_gen_kwh` resets daily.
- `monthly_electricity_generation_kwh` resets at the month boundary.
- `yearly_electricity_generation_kwh` continues across month boundaries.
- `total_power_generation_kwh` is lifetime-style and continues across month boundaries.

## Still Unconfirmed

Yearly reset behavior has not been observed. Expected behavior is that `yearly_electricity_generation_kwh` resets at the calendar year boundary according to the inverter clock, but this remains unproven until:

- a January 1 rollover is captured naturally; or
- a controlled lab clock-shift test is designed and reviewed.

Do not run a controlled clock-shift test on a production unit: moving the inverter's clock across a day, month or year boundary could reset or corrupt its own energy counters.

## Home Assistant Entity Policy

The ESPHome component intentionally uses:

| Field | Device class | State class | Reason |
|---|---|---|---|
| `daily_power_gen_kwh` | `energy` | `total_increasing` | resets every day; decoder only lets it drop on a rollover to ~0 |
| `monthly_electricity_generation_kwh` | `energy` | `total_increasing` | resets every month; same rollover guard |
| `yearly_electricity_generation_kwh` | `energy` | `total_increasing` | likely resets every year; same rollover guard |
| `total_power_generation_kwh` | `energy` | `total_increasing` | lifetime-style counter |

Daily/monthly/yearly were promoted to `total_increasing` (Home Assistant rejects `energy` + `measurement`). The decoder is reset-aware: a period counter may only decrease when it rolls over to <= 0.5 kWh; any other dip is held back unless it repeats on 3 consecutive polls, so a glitch cannot be counted as a reset.
