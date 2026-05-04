# Security and Safety

This project talks to an inverter over RS232. Incorrect commands can change operating parameters, battery thresholds, charging behavior, or system time.

## Supported v1 Behavior

- Read-only H-command telemetry.
- Diagnostic parsing of captured read responses.
- No production write controls.

## Excluded from v1

The following command families are intentionally not exposed as controls:

- `PD*`
- `PE*`
- `PBEQV*`
- `PDSRS*`
- `PDDLYT*`
- `^S???DAT*`
- `PCPxx`
- unknown `Pxxx` setters

## Reporting

Please do not publish secrets, Solar of Things credentials, serial numbers, public IPs, or unredacted cloud tokens in issues. For protocol captures, include raw hex and ASCII but redact any account or site identifiers.
