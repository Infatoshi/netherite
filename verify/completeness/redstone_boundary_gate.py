#!/usr/bin/env python3
"""Lock the promoted bounded redstone family evidence."""

from __future__ import annotations

import json
import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[2]


def require(relative: str, tokens: tuple[str, ...]) -> str:
    text = (ROOT / relative).read_text(encoding="utf-8")
    for token in tokens:
        if token not in text:
            raise RuntimeError(f"{relative} lost redstone evidence {token!r}")
    return text


def main() -> int:
    receipt = json.loads((ROOT / "verify/completeness/redstone_fuzz_256_receipt.json").read_text())
    if receipt.get("schema") != "netherite.redstone_topology_receipt" \
            or (receipt.get("case_count"), receipt.get("pass"),
                receipt.get("fail"), receipt.get("retry_count")) != (256, 256, 0, 0) \
            or receipt.get("state_divergences") != 0 \
            or receipt.get("unrepresented_state_fields") != 0:
        raise RuntimeError("redstone topology receipt is not a clean 256-case run")
    matrix = require("magma/trace/run_oracle_matrix.py", (
        "redstone_fuzz", "redstone_torch_floor_burnout", "redstone_tripwire",
        "redstone_comparator", "redstone_observer", "piston_",
        "dispenser_", "dropper_", "hopper_"))
    if len(set(__import__("re").findall(r'"redstone_[A-Za-z0-9_]+', matrix))) < 500:
        raise RuntimeError("redstone named-case surface unexpectedly shrank")
    require("magma/game/runtime.c", (
        "runtime_redstone_neighbor_changed",
        "runtime_redstone_update_wire_component",
        "runtime_redstone_piston_start_extension",
        "runtime_schedule_tick_insert_dim",
        "runtime_tick_dispenser",
        "runtime_tick_hopper_tile"))
    require("magma/game/test_piston_capacity.c", (
        "{63, 64, 65, 257}", "4095; boundary <= 4097",
        "257-piston checkpoint continuation failed"))
    for relative in (
        "verify/completeness/command_block_gate.py",
        "verify/completeness/structure_block_gate.py",
        "verify/completeness/minecart_variant_gate.py",
        "magma/game/test_container_click_oracle.c",
        "magma/game/test_container_live.c",
    ):
        if not (ROOT / relative).is_file():
            raise RuntimeError(f"missing redstone boundary evidence {relative}")
    print(
        "PASS redstone boundary: 256 topology forks, 500+ named fixtures, "
        "dust/control/diode/observer/piston/automation/rail/command families, "
        "capacity edges, checkpointing, and UI evidence are pinned")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL redstone boundary: {exc}")
        raise SystemExit(1)
