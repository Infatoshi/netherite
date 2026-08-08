#!/usr/bin/env python3
"""Real Java fresh-NBT active-villager capsule continuation."""

import argparse
import json
import math
import os
import pathlib
import struct
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
MAGMA = HERE.parent
sys.path.insert(0, str(HERE))

from state_capsule import create_capsule, emit_magma
from test_dragon_crystal_notification import request
from trace_java import canonicalize


CONTINUATION_TICKS = 20


def dbits(value):
    return struct.pack(">d", float(value))


def fbits(value):
    return struct.pack(">f", float(value))


def by_eid(values, eid):
    matches = [value for value in values if value.get("eid") == eid]
    if len(matches) != 1:
        raise AssertionError(f"expected eid {eid}, got {matches!r}")
    return matches[0]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25575)
    args = parser.parse_args()
    subprocess.run(
        ["make", "-C", str(MAGMA), "game"], check=True,
        stdout=subprocess.DEVNULL,
    )
    temp_root = pathlib.Path(os.environ.get(
        "TMPDIR", str(MAGMA.parent / ".tmp")))
    temp_root.mkdir(parents=True, exist_ok=True)
    locked = False
    with tempfile.TemporaryDirectory(
            prefix="active_villager_capsule_", dir=temp_root) as raw_temp:
        temp = pathlib.Path(raw_temp)
        obs = request(args.port, "obs")
        rules = request(args.port, "runcmds", {"cmds": [
            "gamerule doMobSpawning false",
            "gamerule doDaylightCycle false",
        ]})
        if not rules.get("ok") or rules.get("failed"):
            raise AssertionError(f"could not stabilize oracle rules: {rules}")
        request(args.port, "killentities")
        try:
            locked_state = request(args.port, "server_step_lock")
            locked = True
            player = locked_state["authoritative"]
            center_x = math.floor(float(player["x"])) + 24
            ground_y = math.floor(float(player["y"])) - 1
            center_z = math.floor(float(player["z"]))
            blocks = [
                [x, ground_y, z, 1, 0]
                for z in range(center_z - 8, center_z + 9)
                for x in range(center_x - 8, center_x + 9)
            ]
            blocks.extend(
                [x, y, z, 0, 0]
                for y in range(ground_y + 1, ground_y + 5)
                for z in range(center_z - 8, center_z + 9)
                for x in range(center_x - 8, center_x + 9)
            )
            changed = request(
                args.port, "setblocks_locked", {"blocks": blocks})
            if not changed.get("ok"):
                raise AssertionError(f"platform setup failed: {changed}")
            summoned = request(args.port, "summon_locked", {
                "type": "villager", "profession": 1, "no_ai": 0,
                "entity_seed48": 0x123456789ABC,
                "x": center_x + 0.5, "y": ground_y + 1.0,
                "z": center_z + 0.5,
                "mx": 0.0, "my": 0.0, "mz": 0.0,
            })
            eid = summoned["eid"]
            roundtrip = request(args.port, "entity_nbt_roundtrip_locked")
            if roundtrip.get("count") != 1:
                raise AssertionError(f"NBT round-trip count: {roundtrip}")
            authoritative = roundtrip["authoritative"]
            fresh = by_eid(authoritative["entities"], eid)
            if fresh.get("type") != "EntityVillager" \
                    or fresh.get("active_fresh_villager_exact") is not True \
                    or fresh.get("ticks_existed") != 0 \
                    or fresh.get("no_ai") is not False:
                raise AssertionError(
                    f"Java did not expose an exact fresh villager: {fresh}")

            box = [
                center_x - 9, max(0, ground_y - 2), center_z - 9,
                center_x + 9, ground_y + 5, center_z + 9,
            ]
            block_file = temp / "blocks.bin"
            request(args.port, "getblocks_locked", {
                "x0": box[0], "y0": box[1], "z0": box[2],
                "x1": box[3], "y1": box[4], "z1": box[5],
                "file": str(block_file),
            })
            observation = dict(obs)
            observation["authoritative"] = authoritative
            state = canonicalize(-1, observation, box)
            state_file = temp / "state.json"
            state_file.write_text(json.dumps(state), encoding="utf-8")
            capsule = temp / "capsule"
            create_capsule(
                state_file, block_file, box, capsule, seed=42,
                source_engine="minecraft-java", source_version="1.11.2",
            )
            script = temp / "load.jsonl"
            emit_magma(capsule, script)
            events = [
                json.loads(line)
                for line in script.read_text(encoding="utf-8").splitlines()
            ]
            toggle = next(
                index for index, event in enumerate(events)
                if event.get("type") == "set_mob_no_ai"
                and event.get("eid") == eid
            )
            base = next(
                index for index, event in enumerate(events)
                if event.get("type") == "restore_no_ai_mob_state"
                and event.get("eid") == eid
            )
            if base >= toggle or events[toggle].get("no_ai") != 0:
                raise AssertionError("capsule activated villager out of order")
            java_states = [
                request(args.port, "step")["authoritative"]
                for _ in range(CONTINUATION_TICKS)
            ]
        finally:
            if locked:
                request(args.port, "server_step_unlock")
            request(args.port, "runcmds", {"cmds": [
                "gamerule doMobSpawning true",
                "gamerule doDaylightCycle true",
            ]})

        native_state = temp / "native.jsonl"
        subprocess.run([
            str(MAGMA / "magma_game"),
            "--world", "superflat", "--headless", "--ticks",
            str(CONTINUATION_TICKS), "--mobs", "off",
            "--script", str(script), "--state-out", str(native_state),
            "--render", "off", "--pace", "unlimited",
        ], check=True, stdout=subprocess.DEVNULL)
        native_states = [
            json.loads(line) for line in
            native_state.read_text(encoding="utf-8").splitlines()
        ]
        if len(native_states) != CONTINUATION_TICKS:
            raise AssertionError(
                f"native emitted {len(native_states)} continuation ticks")
        for tick, (java_state, native_state) in enumerate(
                zip(java_states, native_states), start=1):
            java = by_eid(java_state["entities"], eid)
            native = by_eid(native_state["entities"], eid)
            mismatches = []
            if native.get("type") != 40:
                mismatches.append("type")
            for field in ("x", "y", "z", "vx", "vy", "vz"):
                if dbits(java[field]) != dbits(native[field]):
                    mismatches.append(field)
            for field in ("yaw", "pitch", "health"):
                if fbits(java[field]) != fbits(native[field]):
                    mismatches.append(field)
            for field in (
                    "hurt_time", "death_time", "hurt_resistant_time",
                    "profession", "growing_age", "career", "career_level",
                    "offers_initialized", "living_sound_time",
                    "entity_seed48"):
                if java[field] != native[field]:
                    mismatches.append(field)
            if native.get("no_ai") is not False:
                mismatches.append("no_ai")
            if native_state.get("mob_update_order") != [eid]:
                mismatches.append("loaded update order")
            if mismatches:
                raise AssertionError(
                    f"active villager tick {tick} mismatch {mismatches}: "
                    f"Java={java!r} native={native!r} "
                    f"native_order={native_state.get('mob_update_order')!r} "
                    f"native_entities={[(value.get('eid'), value.get('type'), value.get('kind')) for value in native_state.get('entities', [])]!r} "
                    f"java_entities={[(value.get('eid'), value.get('type')) for value in java_state.get('entities', [])]!r}")
    print(
        "PASS real Java fresh-NBT/capsule/native active villager: "
        f"{CONTINUATION_TICKS} exact continuation ticks")


if __name__ == "__main__":
    main()
