#!/usr/bin/env python3
"""Lock Beacon callbacks to the twelve-case live oracle."""

from __future__ import annotations

import json
import pathlib

from callback_census import census


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def main() -> int:
    receipt = json.loads(
        (HERE / "block_callback_beacon_receipt.json").read_text())
    if receipt.get("schema") != "netherite.block_callback_beacon_receipt" \
            or receipt.get("version") != 1 \
            or (receipt.get("case_count"), receipt.get("pass"),
                receipt.get("fail"), receipt.get("negative_controls")) \
            != (12, 12, 0, 1):
        raise RuntimeError("invalid Beacon callback receipt")
    covered = {tuple(row) for row in receipt.get("families", [])}
    if covered != {
            ("BlockBeacon", "neighborChanged"),
            ("BlockBeacon", "onBlockActivated")}:
        raise RuntimeError("Beacon callback family set changed")
    families = {
        (family["owner"].rsplit(".", 1)[-1], family["callback"])
        for family in census()["families"]
    }
    if not covered <= families:
        raise RuntimeError("Beacon callback census owners changed")
    java = (ROOT / "java/Minecraft/src/main/java/qrl/Recorder.java").read_text()
    test = (ROOT / "magma/trace/test_beacon.py").read_text()
    native = (ROOT / "magma/game/test_beacon_oracle.c").read_text()
    for token in ("Blocks.BEACON.neighborChanged(", ".onBlockActivated(",
                  'out.addProperty("activated", activated)'):
        if token not in java:
            raise RuntimeError(f"real-Java Beacon executor lost {token}")
    if "CASES = (" not in test or "Beacon segment sabotage escaped" not in test:
        raise RuntimeError("Beacon case matrix or negative control lost")
    if '\\"activated\\":true' not in native:
        raise RuntimeError("native Beacon activation result lost")
    print("PASS Beacon callbacks: activation and neighbor update exact in 12 cases")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL Beacon callbacks: {exc}")
        raise SystemExit(1)
