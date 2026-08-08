#!/usr/bin/env python3
"""Run the growable-store battery under allocator and process isolation stress."""

from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import os
import pathlib
import subprocess
import tempfile
import time


ROOT = pathlib.Path(__file__).resolve().parents[2]
COMMANDS = (
    ("living", "magma/game/test_living_cold_slot"),
    ("loaded_order", "magma/game/test_loaded_order_capacity"),
    ("specialized_mobs", "magma/game/test_specialized_mob_capacity"),
    ("stack_tags", "magma/game/test_stack_tag_capacity"),
    ("structure_registry", "magma/game/test_structure_registry_capacity"),
    ("tiles", "magma/game/test_tile_capacity"),
    ("pistons", "magma/game/test_piston_capacity"),
)


class StressError(RuntimeError):
    pass


def _lane(index: int, scratch: pathlib.Path) -> dict[str, object]:
    lane_root = scratch / f"lane-{index}"
    work = lane_root / "work"
    work.mkdir(parents=True)
    (lane_root / ".tmp").mkdir()
    (work / ".tmp").mkdir()
    environment = dict(os.environ)
    environment.update({
        "MALLOC_PERTURB_": str((index * 73 + 19) % 255 + 1),
        "TMPDIR": str(lane_root / ".tmp"),
    })
    digest = hashlib.sha256()
    started = time.monotonic()
    command_rows = []
    for name, relative in COMMANDS:
        result = subprocess.run(
            [str(ROOT / relative)], cwd=work, env=environment,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            check=False, text=False,
        )
        digest.update(name.encode("ascii") + b"\0")
        digest.update(result.stdout)
        command_rows.append({
            "name": name,
            "exit_code": result.returncode,
            "output_sha256": hashlib.sha256(result.stdout).hexdigest(),
        })
        if result.returncode:
            raise StressError(
                f"lane {index} {name} failed with rc={result.returncode}: "
                + result.stdout[-2000:].decode("utf-8", "replace"))
    return {
        "lane": index,
        "malloc_perturb": int(environment["MALLOC_PERTURB_"]),
        "elapsed_seconds": round(time.monotonic() - started, 3),
        "aggregate_sha256": digest.hexdigest(),
        "commands": command_rows,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--lanes", type=int, default=32)
    parser.add_argument("--workers", type=int, default=16)
    parser.add_argument("--json-out", type=pathlib.Path)
    args = parser.parse_args()
    if not 1 <= args.lanes <= 256 or not 1 <= args.workers <= 64:
        raise StressError("lanes must be 1..256 and workers 1..64")
    missing = [relative for _, relative in COMMANDS
               if not os.access(ROOT / relative, os.X_OK)]
    if missing:
        raise StressError("missing capacity binaries: " + ", ".join(missing))
    started = time.monotonic()
    scratch_root = ROOT / ".tmp"
    scratch_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(
            prefix="capacity-stress-", dir=scratch_root) as raw:
        scratch = pathlib.Path(raw)
        with concurrent.futures.ThreadPoolExecutor(
                max_workers=min(args.workers, args.lanes)) as executor:
            lanes = list(executor.map(
                lambda index: _lane(index, scratch), range(args.lanes)))
    command_hash_sets = {
        name: sorted({
            row["output_sha256"] for lane in lanes
            for row in lane["commands"] if row["name"] == name
        }) for name, _ in COMMANDS
    }
    nondeterministic = {
        name: hashes for name, hashes in command_hash_sets.items()
        if len(hashes) != 1
    }
    if nondeterministic:
        raise StressError(
            "allocator perturbation changed capacity output: "
            + json.dumps(nondeterministic, sort_keys=True))
    report = {
        "schema": "netherite.capacity_stress",
        "version": 1,
        "lane_count": args.lanes,
        "worker_count": min(args.workers, args.lanes),
        "command_count": len(COMMANDS),
        "process_runs": args.lanes * len(COMMANDS),
        "wall_seconds": round(time.monotonic() - started, 3),
        "command_output_sha256": {
            name: hashes[0] for name, hashes in command_hash_sets.items()
        },
        "lanes": lanes,
    }
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n",
            encoding="utf-8")
    print(
        f"PASS capacity stress: {args.lanes} allocator-isolated lanes, "
        f"{report['process_runs']} process runs, deterministic output in "
        f"{report['wall_seconds']:.3f}s")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, StressError) as error:
        print(f"FAIL capacity stress: {error}")
        raise SystemExit(1)
