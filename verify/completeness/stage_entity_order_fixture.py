#!/usr/bin/env python3
"""Stage opposite loaded-entity insertion orders in a parked Java save."""

from __future__ import annotations

import argparse
import json
import math
import os
import pathlib
import subprocess
import sys
import time
import uuid


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
START = ROOT / "java" / "start_oracle_instance.sh"
sys.path.insert(0, str(HERE))
import save_fork  # noqa: E402


class StageError(RuntimeError):
    pass


def _oracle(action: str, instance: int, seed: int,
            environment: dict[str, str]) -> None:
    command = ["bash", str(START), action, str(instance)]
    if action == "start":
        command.append(str(seed))
    result = subprocess.run(command, cwd=ROOT, env=environment, check=False)
    if result.returncode:
        raise StageError(
            f"oracle instance {action} failed with rc={result.returncode}")


def stage(source: pathlib.Path, output: pathlib.Path,
          instance: int, seed: int, reverse: bool,
          cycle_chunk: bool) -> None:
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
    started = False
    locked = False
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
        save_fork.request(port, "normalize_reload_locked")
        boundary = save_fork.request(port, "authoritative_state_locked")
        authoritative = boundary.get("authoritative") or {}
        if authoritative.get("entities"):
            raise StageError("source has pre-existing represented entities")
        if cycle_chunk:
            # A fresh flat-world player is inside the dimension's permanently
            # retained spawn square. Stage the unload fixture outside that
            # square, then normalize its one-player watch window before adding
            # either entity.
            staged_x = float(authoritative.get("x", observation["x"])) + 512.0
            staged_y = float(authoritative.get("y", observation["y"]))
            staged_z = float(authoritative.get("z", observation["z"]))
            moved = save_fork.request(port, "setplayer_locked", {
                "x": staged_x, "y": staged_y, "z": staged_z,
            })
            if not moved.get("ok"):
                raise StageError(f"could not stage distant watcher: {moved}")
            save_fork.request(port, "server_tick_locked")
            normalized = save_fork.request(port, "normalize_reload_locked")
            if normalized.get("watched_chunks", 0) < 1:
                raise StageError(
                    f"distant watcher normalization failed: {normalized}")
            boundary = save_fork.request(port, "authoritative_state_locked")
            authoritative = boundary.get("authoritative") or {}
        x = float(authoritative.get("x", observation["x"]))
        y = math.floor(float(authoritative.get("y", observation["y"]))) + 1.0
        z = float(authoritative.get("z", observation["z"])) + 4.0
        rows = [
            {
                "eid": 5101,
                "uuid": "00000000-0000-0000-0000-000000005101",
                "x": x - 1.0,
            },
            {
                "eid": 5102,
                "uuid": "00000000-0000-0000-0000-000000005102",
                "x": x + 1.0,
            },
        ]
        insertion = list(reversed(rows)) if reverse else rows
        for row in insertion:
            result = save_fork.request(port, "summon_locked", {
                "type": "item", "x": row["x"], "y": y, "z": z,
                "mx": 0.0, "my": 0.0, "mz": 0.0,
                "item": 1, "count": 1, "meta": 0,
                "pickup_delay": 0,
                "eid": row["eid"], "uuid": row["uuid"],
            })
            if not result.get("ok"):
                raise StageError(f"could not spawn ordered item: {result}")
            authoritative = result.get("authoritative") or {}
        actual = [
            row["eid"] for row in sorted(
                authoritative.get("entities", []),
                key=lambda row: row["loaded_order"])
            if row.get("eid") in {5101, 5102}
        ]
        expected = [row["eid"] for row in insertion]
        if actual != expected:
            raise StageError(
                f"Java entity insertion order mismatch: {actual!r} != "
                f"{expected!r}")
        # World.spawnEntity appends the live list immediately, but the chunk
        # entity slices used by Anvil are finalized by the next server tick.
        # The two items are deliberately outside merge range, so this is a
        # persistence flush rather than the causal order observation.
        advanced = save_fork.request(port, "server_tick_locked")
        authoritative = advanced.get("authoritative") or {}
        actual = [
            row["eid"] for row in sorted(
                authoritative.get("entities", []),
                key=lambda row: row["loaded_order"])
            if row.get("eid") in {5101, 5102}
        ]
        if actual != expected:
            raise StageError("Java entity order changed during persistence tick")
        unload_reload = None
        if cycle_chunk:
            # Persist the entity slice, then move the sole watcher more than
            # two view radii away. Wait for both UUIDs to leave the world's
            # loaded list, return, and require the same insertion order after
            # Chunk.onLoad reconstructs the slice from Anvil.
            save_fork.request(port, "save_world_locked")
            start_x = float(authoritative.get("x", x))
            start_y = float(authoritative.get("y", y))
            start_z = float(authoritative.get("z", z))
            moved = save_fork.request(port, "setplayer_locked", {
                "x": start_x + 512.0, "y": start_y, "z": start_z,
            })
            if not moved.get("ok"):
                raise StageError(f"could not move watcher away: {moved}")
            entity_chunk = (math.floor(x) >> 4, math.floor(z) >> 4)
            queued = save_fork.request(port, "queue_chunk_unload_locked", {
                "x": entity_chunk[0], "z": entity_chunk[1],
            })
            if not queued.get("ok"):
                raise StageError(f"could not queue entity chunk unload: {queued}")
            absent_tick = -1
            distant = {}
            for tick in range(1, 81):
                distant = save_fork.request(port, "server_tick_locked")
                distant_entities = (
                    distant.get("authoritative") or {}).get("entities", [])
                if not any(row.get("eid") in {5101, 5102}
                           for row in distant_entities):
                    absent_tick = tick
                    break
            if absent_tick < 0:
                topology = save_fork.request(port, "chunk_topology_locked")
                state = distant.get("authoritative") or {}
                topology_world = next(
                    row for row in topology.get("worlds", [])
                    if row.get("dim") == int(state.get("dim", 0)))
                def owns(name: str) -> list[dict]:
                    return [row for row in topology_world.get(name, [])
                            if (row.get("x"), row.get("z")) == entity_chunk]
                raise StageError(
                    "ordered entity chunk did not unload: "
                    f"player=({state.get('x')},{state.get('z')}) "
                    f"entities={[(row.get('eid'), row.get('x'), row.get('z')) for row in state.get('entities', [])]} "
                    f"chunk={entity_chunk} "
                    f"watched={owns('watched_entries')} "
                    f"loaded={owns('loaded_chunks')} "
                    f"pending={owns('pending_chunk_unloads')}")
            moved = save_fork.request(port, "setplayer_locked", {
                "x": start_x, "y": start_y, "z": start_z,
            })
            if not moved.get("ok"):
                raise StageError(f"could not return watcher: {moved}")
            reloaded_tick = -1
            for tick in range(1, 121):
                returned = save_fork.request(port, "server_tick_locked")
                authoritative = returned.get("authoritative") or {}
                reloaded_rows = [
                    row for row in sorted(
                        authoritative.get("entities", []),
                        key=lambda row: row["loaded_order"])
                    if row.get("uuid_most") == 0
                    and row.get("uuid_least") in {0x5101, 0x5102}
                ]
                if len(reloaded_rows) == 2:
                    reloaded_tick = tick
                    break
            if reloaded_tick < 0:
                topology = save_fork.request(port, "chunk_topology_locked")
                state = returned.get("authoritative") or {}
                topology_world = next(
                    row for row in topology.get("worlds", [])
                    if row.get("dim") == int(state.get("dim", 0)))
                def owns_return(name: str) -> list[dict]:
                    return [row for row in topology_world.get(name, [])
                            if (row.get("x"), row.get("z")) == entity_chunk]
                raise StageError(
                    "ordered entity chunk did not reload: "
                    f"player=({state.get('x')},{state.get('z')}) "
                    f"entities={[(row.get('eid'), row.get('x'), row.get('z')) for row in state.get('entities', [])]} "
                    f"chunk={entity_chunk} "
                    f"watched={owns_return('watched_entries')} "
                    f"loaded={owns_return('loaded_chunks')} "
                    f"pending={owns_return('pending_chunk_unloads')}")
            actual_identities = [
                row["uuid_least"] for row in reloaded_rows]
            expected_identities = [
                uuid.UUID(row["uuid"]).int & 0xFFFFFFFFFFFFFFFF
                for row in insertion]
            if actual_identities != expected_identities:
                raise StageError(
                    f"entity order changed across chunk cycle: "
                    f"{actual_identities!r} != {expected_identities!r}")
            for definition in rows:
                definition_uuid_least = (
                    uuid.UUID(definition["uuid"]).int
                    & 0xFFFFFFFFFFFFFFFF)
                current = next(
                    row for row in reloaded_rows
                    if row["uuid_least"] == definition_uuid_least)
                repinned = save_fork.request(
                    port, "set_entity_position_locked", {
                        "uuid": definition["uuid"],
                        "x": current["x"], "y": current["y"],
                        "z": current["z"], "set_eid": definition["eid"],
                    })
                if not repinned.get("ok"):
                    raise StageError(
                        f"could not re-pin transient entity id: {repinned}")
                authoritative = repinned.get("authoritative") or {}
            unload_reload = {
                "unloaded_after_ticks": absent_tick,
                "reloaded_after_ticks": reloaded_tick,
                "uuid_order": actual_identities,
            }
        # Persist the pair while it is outside merge range. Then, without
        # granting another tick or rewriting Anvil, move both fixed UUIDs to
        # the same contact point and capture that in-memory continuation. A
        # hidden-resume fork must therefore execute a real order-sensitive
        # merge on its first tick, while a cold reload remains a valid saved
        # state with both entities present.
        oracle = save_fork.request(port, "save_world_locked")
        contact_x = x
        for row in rows:
            moved = save_fork.request(port, "set_entity_position_locked", {
                "eid": row["eid"], "x": contact_x, "y": y, "z": z,
                "ticks_existed": 24,
            })
            if not moved.get("ok"):
                raise StageError(f"could not arm ordered item contact: {moved}")
            authoritative = moved.get("authoritative") or {}
        actual = [
            row["eid"] for row in sorted(
                authoritative.get("entities", []),
                key=lambda row: row["loaded_order"])
            if row.get("eid") in {5101, 5102}
        ]
        positions = [
            (row.get("x"), row.get("y"), row.get("z"))
            for row in authoritative.get("entities", [])
            if row.get("eid") in {5101, 5102}
        ]
        if actual != expected or positions != [(contact_x, y, z)] * 2:
            raise StageError("Java entity contact arming changed identity/order")
        hidden_state = save_fork.request(port, "hidden_state_locked")
        save_fork.write_snapshot(
            pathlib.Path(oracle["world_directory"]), output,
            oracle, hidden_state)
        (output / "fixture_entity_order.json").write_text(json.dumps({
            "schema": "netherite.completeness_fixture",
            "version": 1,
            "id": (
                "save03-item-merge-order-chunk-cycle"
                if cycle_chunk else "save03-item-merge-order"),
            "todo": "SAVE-03",
            "paired_boundary": {
                "left": "entity-order-forward",
                "right": "entity-order-reverse",
                "same_tick": True,
            },
            "negative_control": {
                "mutation": "reverse loadedEntityList insertion order",
                "expected_path": "$/loaded_entity_order[0]",
                "before": 5101,
                "after": 5102,
            },
            "horizons": [1],
            "inputs": [{}],
            "comparisons": [
                {"family": "numeric", "required": True,
                 "minimum_observations": 1},
                {"family": "blocks", "required": True,
                 "minimum_observations": 1},
                {"family": "light", "required": True,
                 "minimum_observations": 1},
                {"family": "order", "required": True,
                 "minimum_observations": 1},
            ],
            "fixture": "item_merge_reverse" if reverse else "item_merge_forward",
            "loaded_entity_order": expected,
            "positions": [[row["eid"], contact_x, y, z] for row in rows],
            "chunk_cycle": unload_reload,
        }, indent=2, sort_keys=True) + "\n")
    finally:
        if locked:
            try:
                save_fork.request(port, "server_step_unlock")
            except Exception:
                pass
        if started:
            _oracle("stop", instance, seed, environment)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("--instance", type=int, default=50)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--reverse", action="store_true")
    parser.add_argument("--cycle-chunk", action="store_true")
    args = parser.parse_args()
    stage(args.source.resolve(), args.output.resolve(), args.instance,
          args.seed, args.reverse, args.cycle_chunk)
    print(f"PASS staged real Java entity order -> {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, ValueError, StageError,
            save_fork.SaveForkError) as exc:
        print(f"FAIL stage entity-order fixture: {exc}", file=sys.stderr)
        raise SystemExit(1)
