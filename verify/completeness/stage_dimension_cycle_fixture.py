#!/usr/bin/env python3
"""Stage and prove twenty parked-server dimension leave/return cycles."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import pathlib
import subprocess
import sys
import time


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
START = ROOT / "java" / "start_oracle_instance.sh"
sys.path.insert(0, str(HERE))
import save_fork  # noqa: E402


class StageError(RuntimeError):
    pass


ENTITY_UUID_LEAST = 0x9001


def _oracle(action: str, instance: int, seed: int,
            environment: dict[str, str]) -> None:
    command = ["bash", str(START), action, str(instance)]
    if action == "start":
        command.append(str(seed))
    result = subprocess.run(command, cwd=ROOT, env=environment, check=False)
    if result.returncode:
        raise StageError(
            f"oracle instance {action} failed with rc={result.returncode}")


def _wait_player(port: int, timeout: float = 120.0) -> dict:
    deadline = time.monotonic() + timeout
    while True:
        try:
            observation = save_fork.request(port, "obs")
            if observation.get("ok") and "x" in observation \
                    and observation.get("dim") == 0:
                return observation
        except save_fork.SaveForkError:
            pass
        if time.monotonic() >= deadline:
            raise StageError("cold Java reload did not produce a player")
        time.sleep(0.1)


def _target_entities(authoritative: dict) -> list[dict]:
    return [row for row in authoritative.get("entities", [])
            if row.get("uuid_most") == 0
            and row.get("uuid_least") == ENTITY_UUID_LEAST]


def _target_tiles(authoritative: dict,
                  positions: set[tuple[int, int, int]]) -> list[dict]:
    return [row for row in sorted(
        authoritative.get("loaded_tiles", []),
        key=lambda value: value["loaded_order"])
        if (row.get("x"), row.get("y"), row.get("z")) in positions]


def _containers(authoritative: dict,
                positions: set[tuple[int, int, int]]) -> list[dict]:
    rows = []
    for source in authoritative.get("containers", []):
        if (source.get("x"), source.get("y"), source.get("z")) \
                not in positions:
            continue
        row = dict(source)
        row.pop("loaded_order", None)
        rows.append(row)
    return sorted(rows, key=lambda row: (row["x"], row["y"], row["z"]))


def _block_digest(port: int, path: pathlib.Path,
                  box: tuple[int, int, int, int, int, int]) -> str:
    result = save_fork.request(port, "getblocks_locked", {
        "x0": box[0], "y0": box[1], "z0": box[2],
        "x1": box[3], "y1": box[4], "z1": box[5],
        "file": str(path),
    })
    if not result.get("ok") or not path.is_file():
        raise StageError(f"could not inspect cross-border structure: {result}")
    return hashlib.sha256(path.read_bytes()).hexdigest()


def stage(source: pathlib.Path, output: pathlib.Path,
          instance: int, seed: int, cycles: int) -> None:
    save_fork.validate_snapshot(source)
    if output.exists():
        raise StageError(f"output already exists: {output}")
    if cycles != 20:
        raise StageError("SAVE-09 fixture requires exactly 20 cycles")
    source_contract = json.loads(
        (source / "fixture_chunk_cycle.json").read_text())
    tile_positions = {
        tuple(position) for position in source_contract["target_tiles"]}
    pending_position = tuple(source_contract["border_scheduled_tick"])
    target_chunk_x, target_chunk_z = source_contract["target_chunk"]
    target_y = min(position[1] for position in tile_positions)
    base_z = target_chunk_z * 16 + 8
    portal_x = target_chunk_x * 16 + 14
    portal_z = base_z + 3
    portal_box = (
        portal_x - 1, target_y, portal_z,
        portal_x + 2, target_y + 4, portal_z)

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
    started = False
    locked = False
    try:
        _oracle("start", instance, seed, environment)
        started = True
        _wait_player(port)
        save_fork.request(port, "step_lock", {"wait_ms": 60000})
        save_fork.request(port, "server_step_lock")
        locked = True
        normalized = save_fork.request(port, "normalize_reload_locked")
        if not normalized.get("ok"):
            raise StageError(f"could not normalize cold reload: {normalized}")
        baseline = (save_fork.request(
            port, "authoritative_state_locked").get("authoritative") or {})
        entities = _target_entities(baseline)
        tiles = _target_tiles(baseline, tile_positions)
        containers = _containers(baseline, tile_positions)
        if len(entities) != 1 or len(tiles) != 2 or len(containers) != 2:
            raise StageError(
                "dimension-cycle source lacks the chunk-cycle payload")
        tile_signature = [
            (row["x"], row["y"], row["z"], row["class"])
            for row in tiles
        ]
        container_signature = containers
        portal_signature = _block_digest(
            port, run_root / "portal_baseline.u16le", portal_box)
        return_position = (
            float(baseline["x"]), float(baseline["y"]),
            float(baseline["z"]))
        prior_age = entities[0]["age"]
        cycle_rows = []
        pending_seen = 0
        pending_drained = False
        last_matching_pending = []

        for cycle in range(1, cycles + 1):
            saved = save_fork.request(port, "save_world_locked")
            if not saved.get("ok"):
                raise StageError(f"cycle {cycle} could not save: {saved}")
            before = (save_fork.request(
                port, "authoritative_state_locked").get("authoritative") or {})
            before_time = before["total_time"]
            left = save_fork.request(
                port, "transfer_dimension_locked", {"id": -1})
            left_state = left.get("authoritative") or {}
            if not left.get("ok") or left_state.get("dim") != -1 \
                    or left_state.get("total_time") != before_time:
                raise StageError(
                    f"cycle {cycle} hid a tick while leaving: {left}")
            away_tick = save_fork.request(port, "server_tick_locked")
            away_state = away_tick.get("authoritative") or {}
            if away_state.get("dim") != -1 \
                    or away_state.get("total_time") != before_time + 1:
                raise StageError(
                    f"cycle {cycle} Nether tick was not exact: {away_tick}")
            returned = save_fork.request(
                port, "transfer_dimension_locked", {"id": 0})
            returned_state = returned.get("authoritative") or {}
            if not returned.get("ok") or returned_state.get("dim") != 0 \
                    or returned_state.get("total_time") != before_time + 1:
                raise StageError(
                    f"cycle {cycle} hid a tick while returning: {returned}")
            moved = save_fork.request(port, "setplayer_locked", {
                "x": return_position[0], "y": return_position[1],
                "z": return_position[2],
            })
            if not moved.get("ok"):
                raise StageError(
                    f"cycle {cycle} could not restore fixture pose: {moved}")

            loaded_after = -1
            final = {}
            for elapsed in range(1, 121):
                ticked = save_fork.request(port, "server_tick_locked")
                final = ticked.get("authoritative") or {}
                if len(_target_entities(final)) == 1 \
                        and len(_target_tiles(final, tile_positions)) == 2:
                    loaded_after = elapsed
                    break
            if loaded_after < 0:
                raise StageError(
                    f"cycle {cycle} did not restore the Overworld payload")
            # The watcher load can complete after World's scheduled-update
            # phase. Grant one explicit resident tick so pending work gets a
            # real opportunity to execute before the next leave.
            resident_tick = save_fork.request(port, "server_tick_locked")
            final = resident_tick.get("authoritative") or {}
            expected_time = before_time + 2 + loaded_after
            if final.get("total_time") != expected_time:
                raise StageError(
                    f"cycle {cycle} granted {2 + loaded_after} ticks but "
                    f"advanced from {before_time} to {final.get('total_time')}")
            entities = _target_entities(final)
            actual_tiles = [
                (row["x"], row["y"], row["z"], row["class"])
                for row in _target_tiles(final, tile_positions)
            ]
            actual_containers = _containers(final, tile_positions)
            age_delta = entities[0]["age"] - prior_age
            if len(entities) != 1 \
                    or not 0 <= age_delta <= 2 + loaded_after \
                    or actual_tiles != tile_signature \
                    or actual_containers != container_signature:
                raise StageError(
                    f"cycle {cycle} changed entity/tile identity or payload: "
                    f"entities={entities} tiles={actual_tiles} "
                    f"containers={actual_containers}")
            if _block_digest(
                    port, run_root / f"portal_cycle_{cycle}.u16le",
                    portal_box) != portal_signature:
                raise StageError(
                    f"cycle {cycle} changed the cross-border portal")
            villages = final.get("villages", [])
            if len(villages) != 1 \
                    or len(villages[0].get("Doors", [])) != 1:
                raise StageError(
                    f"cycle {cycle} changed village identity: {villages}")
            matching_pending = [
                row for row in final.get("scheduled_ticks", [])
                if (row.get("x"), row.get("y"), row.get("z"))
                == pending_position
            ]
            last_matching_pending = matching_pending
            if len(matching_pending) > 1 or (
                    pending_drained and matching_pending):
                raise StageError(
                    f"cycle {cycle} duplicated/revived pending work: "
                    f"{matching_pending}")
            if matching_pending:
                pending_seen += 1
            else:
                pending_drained = True
            cycle_rows.append({
                "cycle": cycle,
                "source_time": before_time,
                "nether_ticks_granted": 1,
                "overworld_ticks_granted": loaded_after + 1,
                "final_time": final["total_time"],
                "entity_age": entities[0]["age"],
                "loaded_after_ticks": loaded_after,
                "pending_present": bool(matching_pending),
            })
            prior_age = entities[0]["age"]

        if not pending_drained:
            raise StageError(
                "near-border scheduled work never dispatched: "
                f"total_time={final.get('total_time')} "
                f"pending={last_matching_pending}")
        save_fork.capture_locked(port, output)
        contract = {
            "schema": "netherite.completeness_fixture",
            "version": 1,
            "id": "save09-twenty-dimension-leave-return-cycles",
            "todo": "SAVE-09",
            "fixture": "dimension-lifecycle",
            "paired_boundary": {
                "left": "returned-after-20-real-round-trips",
                "right": "hidden-transfer-tick-negative-control",
                "same_tick": True,
            },
            "negative_control": {
                "mutation": "advance either transfer by one hidden tick",
                "expected_path": "$/cycles/*/final_time",
                "before": "source+explicit-grants",
                "after": "source+explicit-grants+1",
            },
            "horizons": [1, 20],
            "inputs": [{} for _ in range(20)],
            "comparisons": [
                {"family": family, "required": True,
                 "minimum_observations": 1}
                for family in (
                    "nbt", "numeric", "blocks", "light", "queues", "order")
            ],
            "cycles": cycle_rows,
            "source_chunk_fixture": "save09-twenty-chunk-unload-reload-cycles",
            "portal_digest": portal_signature,
            "pending_seen_cycles": pending_seen,
            "pending_drained": pending_drained,
        }
        (output / "fixture_dimension_cycle.json").write_text(
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
    parser.add_argument("--instance", type=int, default=99)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--cycles", type=int, default=20)
    args = parser.parse_args()
    stage(args.source.resolve(), args.output.resolve(), args.instance,
          args.seed, args.cycles)
    print(
        f"PASS staged {args.cycles} Java dimension lifecycle cycles -> "
        f"{args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, ValueError, StageError,
            save_fork.SaveForkError) as exc:
        print(f"FAIL stage dimension-cycle fixture: {exc}", file=sys.stderr)
        raise SystemExit(1)
