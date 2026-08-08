#!/usr/bin/env python3
"""Rerun only native against retained strict fork Java observations."""

from __future__ import annotations

import argparse
import json
import pathlib

import anvil_to_capsule
import fixture_contract
import fork_runner


def load_java(root: pathlib.Path, horizons: tuple[int, ...],
              box: list[int]) -> dict:
    ticks = (0, *horizons)
    snapshots = {tick: root / f"t{tick:03d}" for tick in ticks}
    for path in snapshots.values():
        if not path.is_dir():
            raise RuntimeError(f"missing retained Java snapshot {path}")
    trace = [json.loads(line) for line in
             (root / "authoritative_trace.jsonl").read_text().splitlines()]
    if len(trace) != max(horizons) + 1:
        raise RuntimeError("retained Java trace length does not match horizons")
    return {
        "trace": trace,
        "hidden": json.loads((root / "hidden_state.json").read_text()),
        "normalized": json.loads((root / "normalized_reload.json").read_text()),
        "hidden_snapshots": {
            tick: json.loads((path / "hidden_state.json").read_text())
            for tick, path in snapshots.items()
        },
        "snapshots": snapshots,
        "raw_snapshots": {
            tick: {
                "blocks": path / "live_blocks.u16le",
                "sky_light": path / "live_sky_light.u8",
                "block_light": path / "live_block_light.u8",
            }
            for tick, path in snapshots.items()
        },
        "box": box,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("java_run", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("--fixture", type=pathlib.Path, required=True)
    parser.add_argument("--box", required=True)
    parser.add_argument("--world-type", default="flat")
    args = parser.parse_args()
    args.java_run = args.java_run.resolve()
    args.output = args.output.resolve()
    args.fixture = args.fixture.resolve()
    if args.output.exists():
        raise RuntimeError(f"output already exists: {args.output}")
    args.output.mkdir(parents=True)
    contract = fixture_contract.load(args.fixture)
    horizons = tuple(contract["horizons"])
    box = [int(value) for value in args.box.split(",")]
    anvil_to_capsule.CAPSULE.cell_count(box)
    actions = list(contract["inputs"][:max(horizons)])
    java = load_java(args.java_run, horizons, box)
    result = fork_runner._native_capability(
        java, args.output, horizons, actions, args.world_type, box)
    (args.output / "native_rerun_report.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n")
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0 if result["status"] == "exact" else 2


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL native fork rerun: {exc}")
        raise SystemExit(1)
