# Unsupported PI30 / PI30MAX Path

The inverter still speaks a PI30-style telemetry surface, and that path is useful for live voltage, current, power, and identity reads. It is not the production path for the Solar Plug totals or the Solar of Things write surface.

## Why it is not the primary solution

- Standard PI30 energy queries timed out on this firmware for the total counters we wanted.
- The missing daily / monthly / yearly / total generation values are exposed by the Solar Plug H-protocol path, not by the plain PI30 counter commands we probed first.
- The live bus evidence for the writable control surface now comes from the H-family write probes and the portal write-history pages.
- Treating PI30 as the main source would keep the project stuck in the wrong branch of the protocol tree.

## What remains useful

- `QPI` / `QMN` / `QVFW` for identity and firmware checks.
- Live telemetry reads that already work and are covered by the decoder tests.
- Cross-checks against the inverter-side sensor values when comparing portal and local data.

## What to avoid

- Blind expansion of `QET` / `QEY` / `QEM` / `QED` / `QLT` / `QLY` / `QLM` / `QLD`.
- Treating cloud totals as proof of a local serial counter family.
- Reintroducing the old blind probe loop as the main discovery strategy.
