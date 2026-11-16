#!/usr/bin/env python3
"""Validate every example YAML against this checkout with `esphome config`.

The examples load the component and packages from GitHub (`github://kOld/solarplug-esphome...@main`) or with
relative includes. This script copies each example to a temporary directory and points both at this checkout,
so a schema change (a renamed entity key, a moved option) breaks the check before it breaks someone's device.
Secrets come from examples/secrets.yaml.example. Nothing is compiled or uploaded.

Usage: python3 tools/validate_examples.py [esphome executable]
"""
from __future__ import annotations

import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXAMPLES = ROOT / "examples"

GITHUB_PACKAGE = re.compile(r"github://kOld/solarplug-esphome/(packages/[\w.-]+\.ya?ml)@\S+")
GITHUB_COMPONENT = re.compile(r"(?m)^(\s*)- source: github://kOld/solarplug-esphome@\S+\s*$")
RELATIVE_INCLUDE = re.compile(r"!include \.\./(packages/[\w.-]+\.ya?ml)")


def localize(text: str) -> str:
    text = GITHUB_PACKAGE.sub(lambda m: f"!include {ROOT / m.group(1)}", text)
    text = RELATIVE_INCLUDE.sub(lambda m: f"!include {ROOT / m.group(1)}", text)
    return GITHUB_COMPONENT.sub(
        lambda m: f"{m.group(1)}- source:\n{m.group(1)}    type: local\n{m.group(1)}    path: {ROOT / 'components'}",
        text,
    )


def main() -> int:
    esphome = sys.argv[1] if len(sys.argv) > 1 else (shutil.which("esphome") or "esphome")
    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        # The example's API key is a placeholder; validation needs a well-formed one. This fixed dummy (bytes
        # 1..32) only ever exists in the temporary directory; ESPHome rejects the all-zero key.
        secrets = re.sub(r"(?m)^api_key:.*$", 'api_key: "AQIDBAUGBwgJCgsMDQ4PEBESExQVFhcYGRobHB0eHyA="',
                         (EXAMPLES / "secrets.yaml.example").read_text(encoding="utf-8"))
        (work / "secrets.yaml").write_text(secrets, encoding="utf-8")
        cases = {example.name: localize(example.read_text(encoding="utf-8")) for example in sorted(EXAMPLES.glob("*.yaml"))}
        # No example ships the unsafe package; validate it layered on the write-beta example.
        beta = cases.get("atom-s3-lite-write-beta.yaml")
        if beta is not None:
            unsafe = f"  solarplug_write_unsafe: !include {ROOT / 'packages' / 'solarplug-write-unsafe.yaml'}\n"
            cases["atom-s3-lite-write-beta+unsafe.yaml"] = re.sub(
                r"(?m)^(  solarplug_write_beta: .*\n)", lambda m: m.group(1) + unsafe, beta, count=1)
        for name, text in cases.items():
            target = work / name
            target.write_text(text, encoding="utf-8")
            result = subprocess.run([esphome, "config", str(target)], capture_output=True, text=True)
            ok = result.returncode == 0
            print(f"{'ok  ' if ok else 'FAIL'} {name}")
            if not ok:
                failures += 1
                lines = (result.stdout + result.stderr).splitlines()
                print("\n".join("    " + line for line in lines if "ERROR" in line or "[" in line or "Invalid" in line)[-3000:])
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
