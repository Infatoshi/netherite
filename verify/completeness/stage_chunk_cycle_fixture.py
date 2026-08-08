#!/usr/bin/env python3
"""Stage and prove twenty real Java chunk unload/reload cycles."""

from __future__ import annotations

import argparse
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


ENTITY_UUID = "00000000-0000-0000-0000-000000009001"


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


def _world(hidden: dict, dimension: int = 0) -> dict:
    rows = [row for row in hidden.get("worlds", [])
            if row.get("dim") == dimension]
    if len(rows) != 1:
        raise StageError(f"hidden state has {len(rows)} dimension {dimension} worlds")
    return rows[0]


def _target_entities(authoritative: dict) -> list[dict]:
    return [row for row in authoritative.get("entities", [])
            if row.get("uuid_most") == 0 and row.get("uuid_least") == 0x9001]


def _target_tiles(authoritative: dict, positions: set[tuple[int, int, int]]) \
        -> list[dict]:
    return [row for row in sorted(
        authoritative.get("loaded_tiles", []),
        key=lambda value: value["loaded_order"])
        if (row.get("x"), row.get("y"), row.get("z")) in positions]


def _target_containers(
        authoritative: dict, positions: set[tuple[int, int, int]]) -> list[dict]:
    rows = []
    for source in authoritative.get("containers", []):
        if (source.get("x"), source.get("y"), source.get("z")) not in positions:
            continue
        row = dict(source)
        row.pop("loaded_order", None)
        rows.append(row)
    return sorted(rows, key=lambda row: (row["x"], row["y"], row["z"]))


def stage(source: pathlib.Path, output: pathlib.Path,
          instance: int, seed: int, cycles: int) -> None:
    save_fork.validate_snapshot(source)
    if output.exists():
        raise StageError(f"output already exists: {output}")
    if cycles != 20:
        raise StageError("SAVE-09 fixture requires exactly 20 cycles")
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
        observation = _wait_player(port)
        save_fork.request(port, "step_lock", {"wait_ms": 60000})
        save_fork.request(port, "server_step_lock")
        locked = True
        normalized = save_fork.request(port, "normalize_reload_locked")
        state = normalized.get("authoritative") or observation

        target_x = math.floor(float(state["x"])) + 512
        target_y = math.floor(float(state["y"]))
        target_z = math.floor(float(state["z"]))
        moved = save_fork.request(port, "setplayer_locked", {
            "x": target_x + 0.5, "y": target_y, "z": target_z + 0.5,
        })
        if not moved.get("ok"):
            raise StageError(f"could not move to distant fixture: {moved}")
        save_fork.request(port, "server_tick_locked")
        save_fork.request(port, "normalize_reload_locked")

        chunk_x = target_x >> 4
        chunk_z = target_z >> 4
        base_x = chunk_x * 16 + 8
        base_z = chunk_z * 16 + 8
        chest = (base_x, target_y, base_z)
        furnace = (base_x + 1, target_y, base_z)
        pending = (chunk_x * 16 + 15, target_y + 6, base_z)
        # Keep the village door in the east neighbor. Village.tick performs an
        # unconditional getBlockState on every registered door, so a door in
        # the cycled chunk would intentionally force that chunk hot again on
        # the same tick and would not be an unload-residency test.
        door = (chunk_x * 16 + 18, target_y, base_z)
        positions = {chest, furnace}

        # One recognizable structure spans the east chunk boundary: a valid
        # obsidian portal frame whose final side column lives in the neighbor.
        portal_x = chunk_x * 16 + 14
        portal_z = base_z + 3
        blocks: list[list[int]] = [
            [chest[0], chest[1], chest[2], 54, 2],
            [furnace[0], furnace[1], furnace[2], 61, 2],
            [pending[0], pending[1] - 1, pending[2], 1, 0],
            [pending[0], pending[1], pending[2], 1, 0],
            [door[0], door[1] - 1, door[2], 1, 0],
            [door[0], door[1], door[2], 64, 0],
            [door[0], door[1] + 1, door[2], 64, 8],
        ]
        # Roof the west side of the east-facing door so vanilla's daylight
        # discriminator recognizes it as a village door.
        blocks.extend([
            [door[0] - offset, door[1] + 3, door[2], 1, 0]
            for offset in range(1, 6)
        ])
        for span in range(2):
            x = portal_x + span
            blocks.extend([
                [x, target_y, portal_z, 49, 0],
                [x, target_y + 4, portal_z, 49, 0],
            ])
        for rise in range(1, 4):
            blocks.extend([
                [portal_x - 1, target_y + rise, portal_z, 49, 0],
                [portal_x + 2, target_y + rise, portal_z, 49, 0],
            ])
        for rise in range(1, 4):
            for span in range(2):
                blocks.append([
                    portal_x + span, target_y + rise, portal_z, 90, 1])

        placed = save_fork.request(
            port, "setblocks_locked", {"blocks": blocks})
        if not placed.get("ok"):
            raise StageError(f"could not place chunk-cycle structure: {placed}")
        for position, item in ((chest, 1), (furnace, 4)):
            filled = save_fork.request(port, "set_container_slot_locked", {
                "x": position[0], "y": position[1], "z": position[2],
                "slot": 0, "item": item, "count": 3, "meta": 0,
            })
            if not filled.get("ok"):
                raise StageError(f"could not seed chunk-cycle tile: {filled}")
        spawned = save_fork.request(port, "summon_locked", {
            "type": "item", "x": base_x + 0.5, "y": target_y + 2.0,
            "z": base_z + 0.5, "mx": 0.0, "my": 0.0, "mz": 0.0,
            "item": 5, "count": 2, "meta": 0, "pickup_delay": 32767,
            "ticks_existed": 40, "eid": 9001, "uuid": ENTITY_UUID,
        })
        if not spawned.get("ok"):
            raise StageError(f"could not seed chunk-cycle entity: {spawned}")
        village = save_fork.request(port, "add_village_door_locked", {
            "x": door[0], "y": door[1], "z": door[2],
        })
        if not village.get("ok"):
            raise StageError(f"could not queue village door: {village}")
        advanced = save_fork.request(port, "server_tick_locked")
        state = advanced.get("authoritative") or {}
        if len(state.get("villages", [])) != 1 \
                or len(state["villages"][0].get("Doors", [])) != 1:
            raise StageError(f"real village discovery did not fire: {state.get('villages')}")
        scheduled = save_fork.request(port, "schedule_locked", {
            "x": pending[0], "y": pending[1], "z": pending[2], "block": 1,
            "delay": 1000, "priority": 2, "replace": True,
        })
        if not scheduled.get("ok"):
            raise StageError(f"could not arm border scheduled tick: {scheduled}")
        save_fork.request(port, "save_world_locked")

        # Keep both lifecycle halves on the ordinary watcher/server-thread
        # path. Directly reloading a dormant chunk before World's deferred
        # tile-removal list drains creates an invalid boundary that normal
        # player movement cannot observe.
        baseline = scheduled.get("authoritative") or {}
        baseline_entities = _target_entities(baseline)
        baseline_tiles = _target_tiles(baseline, positions)
        if len(baseline_entities) != 1 or len(baseline_tiles) != 2:
            raise StageError(
                f"cycle start lost fixture state: entities={baseline_entities} "
                f"tiles={baseline_tiles}")
        entity_age = baseline_entities[0]["age"]
        container_signature = _target_containers(baseline, positions)
        if len(container_signature) != 2:
            raise StageError(
                f"cycle start lost container payloads: {container_signature}")
        tile_signature = [
            (row["x"], row["y"], row["z"], row["class"])
            for row in baseline_tiles
        ]
        hidden = _world(save_fork.request(port, "hidden_state_locked"))
        queue_signature = [
            (row["x"], row["y"], row["z"], row["block"],
             row["time"], row["priority"], row["tick_entry_id"])
            for row in hidden["pendingTickListEntriesTreeSet"]
            if (row["x"], row["y"], row["z"]) == pending
        ]
        if len(queue_signature) != 1:
            raise StageError(f"border callback is not unique: {queue_signature}")
        first_village_tick = baseline["village_collection_tick"]
        cycle_rows = []
        return_x = base_x + 3.5
        away_x = return_x + 512.0
        server_ticks = 0
        prior_age = entity_age

        for cycle in range(1, cycles + 1):
            persisted = save_fork.request(port, "save_world_locked")
            if not persisted.get("ok"):
                raise StageError(
                    f"cycle {cycle} could not persist hot chunk: {persisted}")
            moved = save_fork.request(port, "setplayer_locked", {
                "x": away_x, "y": target_y, "z": target_z + 0.5,
            })
            if not moved.get("ok"):
                raise StageError(
                    f"cycle {cycle} could not move watcher away: {moved}")
            unloaded = save_fork.request(port, "queue_chunk_unload_locked", {
                "x": chunk_x, "z": chunk_z,
            })
            if not unloaded.get("ok"):
                raise StageError(f"cycle {cycle} unload failed: {unloaded}")
            absent_after = -1
            for elapsed in range(1, 81):
                absent = save_fork.request(port, "server_tick_locked")
                server_ticks += 1
                absent_state = absent.get("authoritative") or {}
                if not _target_entities(absent_state) \
                        and not _target_tiles(absent_state, positions):
                    absent_after = elapsed
                    break
            if absent_after < 0:
                raise StageError(
                    f"cycle {cycle} did not drain unloaded objects")
            moved = save_fork.request(port, "setplayer_locked", {
                "x": return_x, "y": target_y,
                "z": target_z + 0.5,
            })
            if not moved.get("ok"):
                raise StageError(
                    f"cycle {cycle} could not return watcher: {moved}")
            returned_after = -1
            returned_state = {}
            for elapsed in range(1, 121):
                returned = save_fork.request(port, "server_tick_locked")
                server_ticks += 1
                returned_state = returned.get("authoritative") or {}
                if len(_target_entities(returned_state)) == 1 \
                        and len(_target_tiles(returned_state, positions)) == 2:
                    returned_after = elapsed
                    break
            if returned_after < 0:
                raise StageError(
                    f"cycle {cycle} did not reload entity and tiles")
            entities = _target_entities(returned_state)
            tiles = _target_tiles(returned_state, positions)
            actual_tiles = [
                (row["x"], row["y"], row["z"], row["class"])
                for row in tiles
            ]
            actual_containers = _target_containers(returned_state, positions)
            age_delta = entities[0]["age"] - prior_age
            if len(entities) != 1 or age_delta not in (0, 1) \
                    or entities[0]["ticks_existed"] < 0 \
                    or actual_tiles != tile_signature \
                    or actual_containers != container_signature:
                raise StageError(
                    f"cycle {cycle} changed identity/age/order: "
                    f"entities={entities} tiles={actual_tiles} "
                    f"containers={actual_containers}")
            hidden = _world(save_fork.request(port, "hidden_state_locked"))
            actual_queue = [
                (row["x"], row["y"], row["z"], row["block"],
                 row["time"], row["priority"], row["tick_entry_id"])
                for row in hidden["pendingTickListEntriesTreeSet"]
                if (row["x"], row["y"], row["z"]) == pending
            ]
            villages = returned_state.get("villages", [])
            if actual_queue != queue_signature or len(villages) != 1 \
                    or len(villages[0].get("Doors", [])) != 1 \
                    or returned_state.get("village_collection_tick") \
                        != first_village_tick + server_ticks:
                raise StageError(
                    f"cycle {cycle} changed pending/village state: "
                    f"queue={actual_queue} villages={villages}")
            cycle_rows.append({
                "cycle": cycle,
                "unloaded_after_ticks": absent_after,
                "reloaded_after_ticks": returned_after,
                "entity_age": entities[0]["age"],
                "entity_ticks_existed": entities[0]["ticks_existed"],
                "loaded_tile_order": [list(row[:3]) for row in actual_tiles],
                "village_collection_tick":
                    returned_state["village_collection_tick"],
                "scheduled_tick_entry_id": actual_queue[0][-1],
            })
            prior_age = entities[0]["age"]

        # Re-arm the same persisted callback near enough for the fork horizons
        # to exercise its dequeue. Stone's callback is intentionally inert,
        # making any duplicate or extra dispatch directly visible as a queue
        # mismatch without conflating chunk residency with block behavior.
        final_pending = save_fork.request(port, "schedule_locked", {
            "x": pending[0], "y": pending[1], "z": pending[2], "block": 1,
            "delay": 4, "priority": 2, "replace": True,
        })
        if not final_pending.get("ok"):
            raise StageError(
                f"could not re-arm final border callback: {final_pending}")
        final_state = final_pending.get("authoritative") or {}
        # TileTicks are serialized into both +/-2-overlapping chunks, but
        # scheduling alone does not dirty either Chunk. Dirty both sides so a
        # subsequent cold load cannot admit an older duplicate from the
        # neighbor before the newly armed canonical row.
        dirty_y = target_y + 12
        dirtied = save_fork.request(port, "setblocks_locked", {"blocks": [
            [chunk_x * 16 + 8, dirty_y, base_z, 1, 0],
            [chunk_x * 16 + 18, dirty_y, base_z, 1, 0],
        ]})
        if not dirtied.get("ok"):
            raise StageError(f"could not dirty callback save boxes: {dirtied}")
        cleared = save_fork.request(port, "setblocks_locked", {"blocks": [
            [chunk_x * 16 + 8, dirty_y, base_z, 0, 0],
            [chunk_x * 16 + 18, dirty_y, base_z, 0, 0],
        ]})
        if not cleared.get("ok"):
            raise StageError(f"could not clear callback save markers: {cleared}")
        persisted_final = save_fork.request(port, "save_world_locked")
        if not persisted_final.get("ok"):
            raise StageError(
                f"could not persist final border callback: {persisted_final}")
        if len(_target_entities(final_state)) != 1 \
                or len(_target_tiles(final_state, positions)) != 2:
            raise StageError("final chunk-cycle boundary is incomplete")
        save_fork.capture_locked(port, output)
        contract = {
            "schema": "netherite.completeness_fixture",
            "version": 1,
            "id": "save09-twenty-chunk-unload-reload-cycles",
            "todo": "SAVE-09",
            "fixture": "chunk-lifecycle",
            "paired_boundary": {
                "left": "loaded-after-20-real-cycles",
                "right": "duplicate-or-extra-tick-negative-control",
                "same_tick": True,
            },
            "negative_control": {
                "mutation": "append a duplicate of persisted UUID 9001",
                "expected_path": "$/entities/uuid-9001/count",
                "before": 1,
                "after": 2,
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
            "target_chunk": [chunk_x, chunk_z],
            "target_tiles": [list(position) for position in sorted(positions)],
            "border_scheduled_tick": list(pending),
            "portal_crosses_east_border": True,
            "village_door": list(door),
        }
        (output / "fixture_chunk_cycle.json").write_text(
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
    print(f"PASS staged {args.cycles} Java chunk lifecycle cycles -> {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, ValueError, StageError,
            save_fork.SaveForkError) as exc:
        print(f"FAIL stage chunk-cycle fixture: {exc}", file=sys.stderr)
        raise SystemExit(1)
