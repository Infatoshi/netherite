#!/usr/bin/env python3
"""Player-facing native slot persistence and atomic-generation gate."""

from __future__ import annotations

import json
import os
import pathlib
import subprocess
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[2]
MAGMA = ROOT / "magma"
GAME = MAGMA / "magma_game"


class NativeSaveError(RuntimeError):
    pass


def _run(
    root: pathlib.Path, name: str, events: list[dict[str, object]], ticks: int,
) -> tuple[subprocess.CompletedProcess[str], dict[str, bytes]]:
    script = root / f"{name}.jsonl"
    script.write_text("".join(
        json.dumps(event, separators=(",", ":"), sort_keys=True) + "\n"
        for event in events))
    outputs = {
        "state": root / f"{name}.state.jsonl",
        "blocks": root / f"{name}.blocks.u16le",
        "sky": root / f"{name}.sky.u8",
        "block_light": root / f"{name}.block_light.u8",
        "height": root / f"{name}.height.u16le",
    }
    environment = dict(os.environ)
    environment.update({
        "MAGMA_CAPSULE_DIR": str(root),
        "MAGMA_NATIVE_SAVE_DIR": str(root),
        "MAGMA_NATIVE_WORLD_ROOT": str(root / "saves"),
        "MAGMA_BLOCKS_OUT": str(outputs["blocks"]),
        "MAGMA_SKY_LIGHT_OUT": str(outputs["sky"]),
        "MAGMA_BLOCK_LIGHT_OUT": str(outputs["block_light"]),
        "MAGMA_HEIGHTS_OUT": str(outputs["height"]),
        "MAGMA_BLOCKS_BOX": "-2,0,-2,18,90,18",
    })
    result = subprocess.run([
        str(GAME), "--world", "superflat", "--seed", "771109",
        "--headless", "--ticks", str(ticks), "--view-distance", "1",
        "--mobs", "off", "--script", str(script),
        "--state-out", str(outputs["state"]), "--render", "off",
        "--pace", "unlimited",
    ], cwd=MAGMA, env=environment, stdout=subprocess.PIPE,
       stderr=subprocess.STDOUT, text=True, check=False)
    return result, {
        key: path.read_bytes() for key, path in outputs.items()
        if path.exists()
    }


def _success(result: subprocess.CompletedProcess[str], label: str) -> None:
    if result.returncode:
        raise NativeSaveError(
            f"{label} failed (rc={result.returncode}):\n{result.stdout}")


def _base_events() -> list[dict[str, object]]:
    return [
        {"tick": 0, "type": "restore_player_statistics",
         "file": "player_statistics.json", "play_one_minute": 37,
         "time_since_death": 19},
        {"tick": 0, "type": "snapshot_region", "dim": 0,
         "cx": 0, "cz": 0, "radius": 1},
        {"tick": 0, "type": "set_pose_state", "x": 1.25, "y": 5.0,
         "z": 1.75, "yaw": 32.5, "pitch": -11.25,
         "vx": 0.03125, "vy": 0.0, "vz": -0.015625,
         "on_ground": 1, "fall": 0.0},
        {"tick": 0, "type": "set_inventory", "slot": 0,
         "item": 276, "count": 1, "meta": 47, "repair_cost": 3,
         "custom_name": "Save Slot Blade", "n_ench": 1,
         "e0": (16 << 16) | 2},
        {"tick": 0, "type": "set_selected_slot", "slot": 0},
        {"tick": 0, "type": "set_block", "x": 4, "y": 4, "z": 4,
         "id": 57, "meta": 0},
        {"tick": 0, "type": "schedule_tick", "x": 5, "y": 4, "z": 5,
         "block": 1, "time": 200, "priority": 2, "order": 81},
    ]


def main() -> int:
    if not GAME.is_file():
        raise NativeSaveError(f"missing {GAME}; run make -C magma game")
    temporary_root = ROOT / ".tmp"
    temporary_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(
            prefix="netherite-native-save-", dir=temporary_root) as raw:
        root = pathlib.Path(raw)
        statistics = {
            "stat.playOneMinute": 37,
            "stat.timeSinceDeath": 19,
            "achievement.exploreAllBiomes": {
                "value": 0, "progress": ["Plains", "Desert"],
            },
            "netherite.unknown": {"nested": [1, True, "kept"]},
        }
        (root / "player_statistics.json").write_text(
            json.dumps(statistics, separators=(",", ":")))

        source_events = _base_events() + [
            {"tick": 7, "type": "write_native_save", "slot": "World_One"},
            {"tick": 8, "type": "action", "forward": 0.75,
             "strafe": -0.25, "sprint": 1},
            {"tick": 9, "type": "action", "forward": 0.5,
             "strafe": 0.125, "dyaw": 3.25},
            {"tick": 10, "type": "set_block", "x": 6, "y": 4, "z": 6,
             "id": 41, "meta": 0},
        ]
        source_result, source = _run(root, "source", source_events, 18)
        _success(source_result, "native slot source")
        slot = root / "saves" / "World_One"
        if slot.joinpath("current").read_text() != "0000000000000001\n":
            raise NativeSaveError("first save did not publish generation 1")

        reload_events = [
            {"tick": 0, "type": "load_native_save", "slot": "World_One"},
            {"tick": 1, "type": "action", "forward": 0.75,
             "strafe": -0.25, "sprint": 1},
            {"tick": 2, "type": "action", "forward": 0.5,
             "strafe": 0.125, "dyaw": 3.25},
            {"tick": 3, "type": "set_block", "x": 6, "y": 4, "z": 6,
             "id": 41, "meta": 0},
        ]
        reload_result, reloaded = _run(root, "reloaded", reload_events, 11)
        _success(reload_result, "native slot reload")
        source_rows = source.pop("state").splitlines(keepends=True)[7:]
        reload_rows = reloaded.pop("state").splitlines(keepends=True)
        if source_rows != reload_rows:
            raise NativeSaveError(
                "native slot state changed across fresh-process reload")
        if source != reloaded:
            raise NativeSaveError(
                "native slot raw world changed across fresh-process reload")

        # Re-save the loaded boundary. The pointer advances only after a full
        # immutable generation is present, and loading generation 2 is exact.
        resave_result, _ = _run(root, "resave", [
            {"tick": 0, "type": "load_native_save", "slot": "World_One"},
            {"tick": 0, "type": "write_native_save", "slot": "World_One"},
        ], 1)
        _success(resave_result, "native slot repeated save")
        if slot.joinpath("current").read_text() != "0000000000000002\n":
            raise NativeSaveError("repeated save did not publish generation 2")
        if not slot.joinpath(
                "generation-0000000000000001", "manifest.bin").is_file():
            raise NativeSaveError("previous generation was destructively removed")
        generation_two = slot / "generation-0000000000000002"
        written = json.loads(
            generation_two.joinpath("player_statistics.json").read_text())
        if written.get("stat.playOneMinute") != 44 \
                or written.get("stat.timeSinceDeath") != 26 \
                or written.get("netherite.unknown") \
                    != statistics["netherite.unknown"]:
            raise NativeSaveError(
                "native slot statistics were not semantic and exact")

        # A complete-looking unpublished directory cannot steal the current
        # pointer. A corrupt current pointer and traversal slot both fail closed.
        slot.joinpath("generation-ffffffffffffffff").mkdir()
        current = slot.joinpath("current")
        original_current = current.read_bytes()
        current.write_text("corrupt\n")
        corrupt_result, _ = _run(root, "corrupt_current", [
            {"tick": 0, "type": "load_native_save", "slot": "World_One"},
        ], 1)
        if corrupt_result.returncode == 0 \
                or "invalid load_native_save" not in corrupt_result.stdout:
            raise NativeSaveError("corrupt current pointer did not fail closed")
        current.write_bytes(original_current)
        traversal_result, _ = _run(root, "traversal", [
            {"tick": 0, "type": "write_native_save", "slot": "../escape"},
        ], 1)
        if traversal_result.returncode == 0 \
                or "invalid write_native_save" not in traversal_result.stdout:
            raise NativeSaveError("native save slot traversal did not fail closed")

    print(
        "PASS native save slot: atomic generations, repeated save/reload, "
        "future inputs, state/world/statistics, corrupt/traversal fail-closed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (NativeSaveError, OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"FAIL native save slot: {exc}", file=os.sys.stderr)
        raise SystemExit(1)
