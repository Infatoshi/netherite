#!/usr/bin/env python3
"""Validate the retained PERF-04 multi-client native soak receipt."""

from __future__ import annotations

import json
import pathlib


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
RECEIPT = HERE / "native_parallel_soak_receipt.json"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def main() -> int:
    receipt = json.loads(RECEIPT.read_text(encoding="utf-8"))
    require(receipt.get("schema") == "netherite.native_parallel_soak"
            and receipt.get("version") == 1,
            "invalid native soak receipt identity")
    lanes = receipt.get("lanes", [])
    require(receipt.get("lane_count") == len(lanes) >= 32,
            "native soak requires at least 32 isolated lanes")
    require(receipt.get("client_seconds", 0) >= 4 * 3600,
            "native soak retained less than four client-hours")
    require(receipt.get("aggregate_peak_rss_kib", 0) < 100 * 1024 * 1024,
            "native soak exceeded the 100 GiB regression ceiling")
    output_hashes = set()
    for lane in lanes:
        require(lane.get("exit_code") == 0 and lane.get("runtime_pass") is True,
                f"lane {lane.get('lane')} did not pass")
        require(0 < lane.get("peak_rss_kib", 0) < 1024 * 1024,
                f"lane {lane.get('lane')} exceeded the 1 GiB RSS ceiling")
        require(lane.get("major_faults") == 0,
                f"lane {lane.get('lane')} incurred major faults")
        require(lane.get("swaps") == 0,
                f"lane {lane.get('lane')} used swap")
        output_hashes.add(lane.get("output_sha256"))
    require(len(output_hashes) == 1,
            "parallel lanes did not produce byte-identical output")
    runner = (HERE / "run_native_soak.py").read_text(encoding="utf-8")
    for token in ("MAGMA_ITEM_SPAWN_LIMIT", "TMPDIR", "taskset", "nice",
                  "Maximum resident set size", "Major (requiring I/O)",
                  'time_value(resource_text, "Swaps")'):
        require(token in runner, f"native soak runner lost {token!r}")
    print(f"PASS PERF-04 native soak: {len(lanes)} lanes, "
          f"{receipt['client_seconds'] / 3600:.2f} client-hours, "
          f"{receipt['aggregate_peak_rss_kib'] / 1048576:.2f} GiB "
          "summed peak RSS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as error:
        print(f"FAIL PERF-04 native soak: {error}")
        raise SystemExit(1)
