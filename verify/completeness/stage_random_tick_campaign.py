#!/usr/bin/env python3
"""Stage a dense multi-chunk, multi-section natural random-tick campaign."""

from __future__ import annotations

import argparse
import json
import math
import os
import pathlib
import sys
import time


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
import save_fork  # noqa: E402
from stage_random_tick_fixture import StageError, _oracle  # noqa: E402


def campaign_blocks(player_x: float, player_z: float) -> list[list[int]]:
    bx = (math.floor(player_x) // 16) * 16
    bz = (math.floor(player_z) // 16) * 16
    blocks: list[list[int]] = []

    # Four independently active sections in four watched chunks. Dense patches
    # make callbacks frequent while retaining support and neighbor interactions.
    for dz in range(0, 12, 2):
        for dx in range(0, 12, 2):
            x, z = bx + 18 + dx, bz + 2 + dz
            blocks.extend(([x, 4, z, 1, 0], [x, 5, z, 12, 0],
                           [x, 6, z, 81, 0]))
    for dz in range(8):
        for dx in range(8):
            x, z = bx - 14 + dx, bz + 2 + dz
            blocks.extend(([x, 21, z, 60, 7], [x, 22, z, 59, 0]))
    for dz in range(8):
        for dx in range(8):
            x, z = bx + 2 + dx, bz + 18 + dz
            blocks.extend(([x, 37, z, 3, 0], [x, 38, z, 6, 0]))
    for dz in range(8):
        for dx in range(8):
            blocks.append([bx + 2 + dx, 54, bz - 14 + dz, 18, 8])
    return blocks


def campaign_box(player_x: float, player_z: float) -> list[int]:
    bx = (math.floor(player_x) // 16) * 16
    bz = (math.floor(player_z) // 16) * 16
    return [bx - 16, 0, bz - 16, bx + 31, 63, bz + 29]


def stage(source: pathlib.Path, output: pathlib.Path, instance: int,
          seed: int, reverse: bool) -> None:
    save_fork.validate_snapshot(source)
    if output.exists():
        raise StageError(f"output already exists: {output}")
    run_root = output.parent / f".{output.name}.oracle-{os.getpid()}"
    environment = dict(os.environ)
    environment.update({
        "JAVA_HOME": "/usr/lib/jvm/java-8-openjdk-amd64",
        "ORACLE_POOL_OUT_ROOT": str(run_root),
        "ORACLE_POOL_SAVE_SOURCE": str(source / "save"),
        "ORACLE_POOL_USERNAME": "PoolPlayer0",
        "ORACLE_POOL_WORLD_TYPE": "flat",
        "ORACLE_POOL_WAIT": "1",
        "TMPDIR": str(ROOT / ".tmp"),
        "UV_CACHE_DIR": str(pathlib.Path.home() / ".cache" / "uv"),
    })
    port = int(environment.get("ORACLE_POOL_PORT_BASE", "25600")) + instance
    started = locked = False
    try:
        _oracle("start", instance, seed, environment)
        started = True
        deadline = time.monotonic() + 120.0
        while True:
            try:
                observation = save_fork.request(port, "obs")
                if observation.get("ok") and "x" in observation:
                    break
            except save_fork.SaveForkError:
                pass
            if time.monotonic() >= deadline:
                raise StageError("cold Java reload did not produce a player")
            time.sleep(0.1)
        save_fork.request(port, "step_lock", {"wait_ms": 60000})
        save_fork.request(port, "server_step_lock")
        locked = True
        normalized = save_fork.request(port, "normalize_reload_locked")
        authoritative = normalized.get("authoritative") or observation
        blocks = campaign_blocks(float(authoritative["x"]),
                                 float(authoritative["z"]))
        placed = save_fork.request(port, "setblocks_locked", {"blocks": blocks})
        if not placed.get("ok"):
            raise StageError(f"could not place campaign: {placed}")
        for _ in range(4):
            if not save_fork.request(port, "server_tick_locked").get("authoritative"):
                raise StageError("could not settle campaign setup")
        if reverse:
            result = save_fork.request(port, "reverse_tick_chunk_order_locked")
            if not result.get("ok") or result.get("entries", 0) < 4:
                raise StageError(f"could not reverse ticking chunks: {result}")
        topology = save_fork.request(port, "chunk_topology_locked")
        worlds = topology.get("worlds", [])
        overworld = next((w for w in worlds if w.get("dim") == 0), None)
        if not overworld:
            raise StageError("campaign has no Overworld topology")
        active = [c for c in overworld.get("ticking_chunks", [])
                  if c.get("random_tick_mask", 0)]
        if len(active) < 4:
            raise StageError(f"campaign has only {len(active)} active chunks")
        save_fork.capture_locked(port, output)

        branch = "reverse" if reverse else "forward"
        contract = {
            "schema": "netherite.completeness_fixture", "version": 1,
            "id": "world01-multisection-random-tick-campaign",
            "todo": "WORLD-01", "fixture": branch,
            "paired_boundary": {
                "left": "forward", "right": "reverse", "same_tick": True,
            },
            "negative_control": {
                "mutation": "reverse ticking-chunk iteration order",
                "expected_path": "$/worlds/0/ticking_chunks/0",
                "before": "forward", "after": "reverse",
            },
            "horizons": [1, 20, 200, 1200],
            "inputs": [{} for _ in range(1200)],
            "comparisons": [
                {"family": family, "required": True,
                 "minimum_observations": 1}
                for family in ("nbt", "numeric", "blocks", "light",
                               "queues", "order")
            ],
            "campaign": {
                "placed_blocks": len(blocks), "active_chunks": len(active),
                "tick_order": branch,
                "box": campaign_box(float(authoritative["x"]),
                                    float(authoritative["z"])),
            },
        }
        (output / "fixture_random_tick_campaign.json").write_text(
            json.dumps(contract, indent=2, sort_keys=True) + "\n")
    finally:
        if locked:
            try:
                save_fork.request(port, "server_step_unlock")
            except Exception:
                pass
        if started:
            _oracle("stop", instance, seed, environment)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("--instance", type=int, default=36)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--reverse", action="store_true")
    args = parser.parse_args()
    stage(args.source.resolve(), args.output.resolve(), args.instance,
          args.seed, args.reverse)
    print(f"PASS staged random-tick campaign -> {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, ValueError, StageError,
            save_fork.SaveForkError) as exc:
        print(f"FAIL stage random-tick campaign: {exc}", file=sys.stderr)
        raise SystemExit(1)
