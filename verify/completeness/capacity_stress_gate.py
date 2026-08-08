#!/usr/bin/env python3
"""Lock PERF-02's allocator-isolated cold-capacity stress campaign."""

from __future__ import annotations

import json
import pathlib


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
RECEIPT = HERE / "capacity_stress_receipt.json"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def main() -> int:
    receipt = json.loads(RECEIPT.read_text(encoding="utf-8"))
    require(receipt.get("schema") == "netherite.capacity_stress"
            and receipt.get("version") == 1,
            "invalid capacity-stress receipt identity")
    require(receipt.get("lane_count", 0) >= 32
            and receipt.get("command_count") == 7
            and receipt.get("process_runs", 0) >= 224,
            "capacity-stress breadth fell below the strict campaign")
    lanes = receipt.get("lanes", [])
    require(len(lanes) == receipt["lane_count"],
            "capacity-stress lane count changed")
    perturbations = {lane.get("malloc_perturb") for lane in lanes}
    require(len(perturbations) == len(lanes),
            "capacity-stress allocator perturbations are not independent")
    expected = {
        "living", "loaded_order", "specialized_mobs", "stack_tags",
        "structure_registry", "tiles", "pistons",
    }
    hashes = receipt.get("command_output_sha256", {})
    require(set(hashes) == expected
            and all(isinstance(value, str) and len(value) == 64
                    for value in hashes.values()),
            "capacity-stress deterministic outputs changed")
    for lane in lanes:
        require({row.get("name") for row in lane.get("commands", [])}
                == expected,
                f"lane {lane.get('lane')} lost a capacity family")
        require(all(row.get("exit_code") == 0
                    for row in lane.get("commands", [])),
                f"lane {lane.get('lane')} did not pass")
    runner = (HERE / "run_capacity_stress.py").read_text(encoding="utf-8")
    for token in ("MALLOC_PERTURB_", "TemporaryDirectory",
                  "test_living_cold_slot", "test_piston_capacity",
                  "test_structure_registry_capacity"):
        require(token in runner, f"capacity-stress runner lost {token!r}")
    print(
        f"PASS PERF-02 capacity stress: {receipt['lane_count']} isolated "
        f"allocator layouts, {receipt['process_runs']} deterministic runs")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as error:
        print(f"FAIL PERF-02 capacity stress: {error}")
        raise SystemExit(1)
