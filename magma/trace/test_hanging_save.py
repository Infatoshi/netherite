#!/usr/bin/env python3
"""Prove hanging-entity Anvil reload and capsule continuation against Java."""

from __future__ import annotations

import argparse
import json
import math
import os
import pathlib
import subprocess
import sys
import tempfile
import time


HERE = pathlib.Path(__file__).resolve().parent
MAGMA = HERE.parent
ROOT = MAGMA.parent
VERIFY = ROOT / "verify" / "completeness"
START = ROOT / "java" / "start_oracle_instance.sh"
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(VERIFY))

import save_fork
from state_capsule import CapsuleError, create_capsule, emit_magma
from trace_java import canonicalize as canonicalize_java
from trace_runtime import canonicalize as canonicalize_native


TICKS = 3


class HangingSaveError(RuntimeError):
    pass


def oracle(action: str, instance: int, seed: int,
           environment: dict[str, str]) -> None:
    command = ["bash", str(START), action, str(instance)]
    if action == "start":
        command.append(str(seed))
    subprocess.run(command, cwd=ROOT, env=environment, check=True)


def wait_player(port: int) -> dict:
    deadline = time.monotonic() + 300.0
    while True:
        try:
            observation = save_fork.request(port, "obs")
            if observation.get("ok") and "x" in observation:
                return observation
        except save_fork.SaveForkError:
            pass
        if time.monotonic() >= deadline:
            raise HangingSaveError("cold Java reload did not produce a player")
        time.sleep(0.1)


def environment(run_root: pathlib.Path, source: pathlib.Path) -> dict[str, str]:
    result = dict(os.environ)
    result.update({
        "JAVA_HOME": "/usr/lib/jvm/java-8-openjdk-amd64",
        "ORACLE_POOL_OUT_ROOT": str(run_root),
        "ORACLE_POOL_SAVE_SOURCE": str(source / "save"),
        "ORACLE_POOL_USERNAME": "HangingSave99",
        "ORACLE_POOL_WORLD_TYPE": "flat",
        "ORACLE_POOL_WAIT": "1",
        "TMPDIR": str(ROOT / ".tmp"),
        "UV_CACHE_DIR": str(pathlib.Path.home() / ".cache" / "uv"),
    })
    return result


def stage(source: pathlib.Path, snapshot: pathlib.Path,
          instance: int, seed: int) -> None:
    env = environment(snapshot.parent / "stage-oracle", source)
    port = int(env.get("ORACLE_POOL_PORT_BASE", "25600")) + instance
    started = False
    locked = False
    try:
        oracle("start", instance, seed, env)
        started = True
        observation = wait_player(port)
        rules = save_fork.request(port, "runcmds", {
            "cmds": ["gamerule doMobSpawning false"],
        })
        if not rules.get("ok") or rules.get("failed"):
            raise HangingSaveError(
                f"could not disable natural spawning: {rules!r}")
        save_fork.request(port, "step_lock", {"wait_ms": 60000})
        save_fork.request(port, "server_step_lock")
        locked = True
        normalized = save_fork.request(port, "normalize_reload_locked")
        save_fork.request(port, "clear_entities_locked")
        state = normalized.get("authoritative") or observation
        x = math.floor(float(state["x"])) + 5
        y = max(8, math.floor(float(state["y"])) + 2)
        z = math.floor(float(state["z"]))
        painting = save_fork.request(port, "hanging_entity_locked", {
            "kind": "painting", "x": x, "y": y, "z": z,
            "facing": 2, "art": 21, "tick_counter": 73,
        })
        knot = save_fork.request(port, "hanging_entity_locked", {
            "kind": "knot", "x": x + 8, "y": y, "z": z,
            "attach": True,
        })
        frame = save_fork.request(port, "hanging_entity_locked", {
            "kind": "frame", "x": x + 12, "y": y, "z": z,
            "facing": 2, "item": 276, "rotation": 5,
            "tick_counter": 73, "drop_chance": 0.75,
            "entity_seed48": 0x123456789ABC,
            "math_seed48": 0x23456789ABCD,
        })
        authoritative = save_fork.request(
            port, "authoritative_state_locked")["authoritative"]
        if len(authoritative.get("paintings", [])) != 1 \
                or authoritative["paintings"][0].get("art") != 21 \
                or authoritative["paintings"][0].get("tick_counter") != 73:
            raise HangingSaveError(
                f"painting staging failed: {authoritative.get('paintings')}")
        llamas = [row for row in authoritative["entities"]
                  if row.get("type") == "EntityLlama"]
        if len(authoritative.get("leash_knots", [])) != 1 \
                or len(llamas) != 1 \
                or llamas[0].get("horse_exact") is not True \
                or llamas[0].get("llama_leash_holder_kind") != 3:
            raise HangingSaveError(
                "knot/llama staging failed: "
                f"{knot!r} {authoritative.get('leash_knots')!r} {llamas!r}")
        living_leashes = authoritative.get("living_leashes", [])
        if len(living_leashes) != 1 \
                or living_leashes[0].get("eid") != llamas[0].get("eid") \
                or living_leashes[0].get("holder_kind") != 3:
            raise HangingSaveError(
                f"generic living-leash staging failed: {living_leashes!r}")
        frames = authoritative.get("item_frames", [])
        if len(frames) != 1 \
                or frames[0].get("item") != 276 \
                or frames[0].get("meta") != 7 \
                or frames[0].get("rotation") != 5 \
                or frames[0].get("tick_counter") != 73 \
                or frames[0].get("repair_cost") != 9 \
                or frames[0].get("custom_name") != "Oracle blade" \
                or frames[0].get("enchants") != [[16, 5], [34, 3]] \
                or "stack_payload" not in frames[0]:
            raise HangingSaveError(
                f"item-frame staging failed: {frame!r} {frames!r}")
        if len({painting.get("eid"), knot.get("eid"), frame.get("eid")}) != 3:
            raise HangingSaveError("hanging entity IDs collided")
        save_fork.capture_locked(port, snapshot)
    finally:
        if locked:
            try:
                save_fork.request(port, "server_step_unlock")
            except Exception:
                pass
        if started:
            oracle("stop", instance, seed, env)


def reload_and_compare(snapshot: pathlib.Path, work: pathlib.Path,
                       instance: int, seed: int) -> None:
    env = environment(work / "reload-oracle", snapshot)
    port = int(env.get("ORACLE_POOL_PORT_BASE", "25600")) + instance
    started = False
    locked = False
    try:
        oracle("start", instance, seed, env)
        started = True
        observation = wait_player(port)
        save_fork.request(port, "step_lock", {"wait_ms": 60000})
        save_fork.request(port, "server_step_lock")
        locked = True
        normalized = save_fork.request(port, "normalize_reload_locked")
        hidden = save_fork.request(port, "hidden_state_locked")
        save_fork.request(port, "isolate_server_globals_locked", hidden)
        authoritative = save_fork.request(
            port, "authoritative_state_locked")["authoritative"]
        paintings = authoritative.get("paintings", [])
        knots = authoritative.get("leash_knots", [])
        frames = authoritative.get("item_frames", [])
        llamas = [row for row in authoritative["entities"]
                  if row.get("type") == "EntityLlama"]
        living_leashes = authoritative.get("living_leashes", [])
        if len(paintings) != 1 or paintings[0].get("art") != 21:
            raise HangingSaveError(
                f"cold Anvil reload lost painting: {paintings!r}")
        if knots or len(llamas) != 1 \
                or llamas[0].get("horse_exact") is not True \
                or llamas[0].get("llama_leashed") is not True \
                or llamas[0].get("llama_leash_holder_kind") != 0 \
                or llamas[0].get("llama_leash_pending") is not True:
            raise HangingSaveError(
                f"cold Anvil reload lost knot graph: {knots!r} {llamas!r}")
        if len(living_leashes) != 1 \
                or living_leashes[0].get("eid") != llamas[0].get("eid") \
                or living_leashes[0].get("pending") is not True:
            raise HangingSaveError(
                "cold Anvil reload lost generic pending leash state: "
                f"{living_leashes!r}")
        if len(frames) != 1 \
                or frames[0].get("item") != 276 \
                or frames[0].get("meta") != 7 \
                or frames[0].get("rotation") != 5 \
                or frames[0].get("tick_counter") != 0 \
                or frames[0].get("item_drop_chance") != 0.75 \
                or frames[0].get("repair_cost") != 9 \
                or frames[0].get("custom_name") != "Oracle blade" \
                or frames[0].get("enchants") != [[16, 5], [34, 3]] \
                or "stack_payload" not in frames[0]:
            raise HangingSaveError(
                f"cold Anvil reload lost item-frame state: {frames!r}")
        selected = [paintings[0], frames[0], llamas[0], authoritative, {
            "x": llamas[0]["llama_leash_pending_x"],
            "y": llamas[0]["llama_leash_pending_y"],
            "z": llamas[0]["llama_leash_pending_z"],
        }]
        box = [
            math.floor(min(float(row["x"]) for row in selected)) - 5,
            max(0, math.floor(min(float(row["y"]) for row in selected)) - 5),
            math.floor(min(float(row["z"]) for row in selected)) - 5,
            math.floor(max(float(row["x"]) for row in selected)) + 5,
            math.floor(max(float(row["y"]) for row in selected)) + 6,
            math.floor(max(float(row["z"]) for row in selected)) + 5,
        ]
        blocks = work / "blocks.bin"
        save_fork.request(port, "getblocks_locked", {
            "x0": box[0], "y0": box[1], "z0": box[2],
            "x1": box[3], "y1": box[4], "z1": box[5],
            "file": str(blocks),
        })
        initial_observation = dict(observation)
        initial_observation["authoritative"] = authoritative
        state = canonicalize_java(-1, initial_observation, box)
        state_file = work / "state.json"
        state_file.write_text(json.dumps(state), encoding="utf-8")
        capsule = work / "capsule"
        try:
            create_capsule(
                state_file, blocks, box, capsule, seed=seed,
                source_engine="minecraft-java", source_version="1.11.2")
        except CapsuleError as error:
            raise HangingSaveError(
                f"capsule rejected cold boundary ({error}); player="
                f"{authoritative.get('player_eid')} entities="
                f"{[(row.get('eid'), row.get('type')) for row in authoritative['entities']]} "
                f"paintings={[(row.get('eid'), row.get('art')) for row in paintings]}"
            ) from error
        script = work / "load.jsonl"
        emit_magma(capsule, script)
        java_states = []
        for tick in range(TICKS):
            response = save_fork.request(port, "step")
            current = dict(observation)
            current["authoritative"] = response["authoritative"]
            java_states.append(canonicalize_java(tick, current, box))
    finally:
        if locked:
            try:
                save_fork.request(port, "server_step_unlock")
            except Exception:
                pass
        if started:
            oracle("stop", instance, seed, env)

    native_file = work / "native.jsonl"
    native_environment = dict(os.environ)
    native_environment["MAGMA_CAPSULE_DIR"] = str(capsule)
    subprocess.run([
        str(MAGMA / "magma_game"), "--world", "superflat",
        "--headless", "--ticks", str(TICKS), "--mobs", "off",
        "--script", str(script), "--state-out", str(native_file),
        "--render", "off", "--pace", "unlimited",
    ], check=True, env=native_environment, stdout=subprocess.DEVNULL)
    native_raw_states = [
        json.loads(line) for line in
        native_file.read_text(encoding="utf-8").splitlines()
    ]
    native_states = [
        canonicalize_native(tick, raw)
        for tick, raw in enumerate(native_raw_states)
    ]
    if len(native_states) != TICKS:
        raise HangingSaveError(
            f"native emitted {len(native_states)} continuation rows")
    for tick, (java, native) in enumerate(zip(java_states, native_states), 1):
        for key in (
                "item_frames", "paintings", "leash_knots",
                "living_leashes"):
            if java[key] != native[key]:
                raise HangingSaveError(
                    f"tick {tick} {key} mismatch: "
                    f"Java={java[key]!r} native={native[key]!r}; "
                    f"Java RNG={java.get('world_rng')!r} "
                    f"native RNG={native.get('world_rng')!r}; "
                    f"S0 RNG={state.get('world_rng')!r} "
                    f"native raw UUID cursor="
                    f"{native_raw_states[tick - 1].get('server_uuid_seed48')!r}")
        java_llamas = [row for row in java["entities"]
                       if row.get("type") == "EntityLlama"]
        native_llamas = [row for row in native["entities"]
                         if row.get("type") == "EntityLlama"]
        if len(java_llamas) != 1 or len(native_llamas) != 1:
            raise HangingSaveError(
                f"tick {tick} llama count mismatch: "
                f"Java={java_llamas!r} native={native_llamas!r}")
        for field in (
                "llama_leashed", "llama_leash_holder_kind",
                "llama_leash_holder_eid", "llama_leash_holder_uuid_most",
                "llama_leash_holder_uuid_least", "llama_leash_pending",
                "llama_leash_pending_x", "llama_leash_pending_y",
                "llama_leash_pending_z"):
            if java_llamas[0].get(field) != native_llamas[0].get(field):
                raise HangingSaveError(
                    f"tick {tick} llama {field} mismatch: "
                    f"Java={java_llamas[0].get(field)!r} "
                    f"native={native_llamas[0].get(field)!r}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=pathlib.Path)
    parser.add_argument("--instance", type=int, default=99)
    parser.add_argument("--seed", type=int, default=0)
    args = parser.parse_args()
    source = args.source.resolve()
    save_fork.validate_snapshot(source)
    subprocess.run(
        ["make", "-C", str(MAGMA), "game"], check=True,
        stdout=subprocess.DEVNULL)
    temp_root = ROOT / ".tmp"
    temp_root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
            prefix="hanging-save-", dir=temp_root) as raw:
        work = pathlib.Path(raw)
        snapshot = work / "staged-snapshot"
        stage(source, snapshot, args.instance, args.seed)
        reload_and_compare(snapshot, work, args.instance, args.seed)
    print(
        "PASS hanging save: real Anvil cold reload retained tagged item-frame "
        "and painting state plus pending llama leash NBT; Java/native both "
        "recreated the knot on the first controlled tick and continuation "
        f"matched for {TICKS} ticks")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (HangingSaveError, KeyError, OSError, ValueError,
            json.JSONDecodeError, save_fork.SaveForkError,
            subprocess.CalledProcessError) as error:
        print(f"FAIL hanging save: {error}", file=sys.stderr)
        raise SystemExit(1)
