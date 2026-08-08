#!/usr/bin/env python3
"""Long mixed-store native checkpoint continuation campaign.

This is the native precursor to AI-05's real-Java mixed-order campaign.  It
puts more than one hundred entities from five independently stored families
under opposite authoritative loaded orders, forks at tick 600, and requires
every subsequent state row and final raw world surface to remain byte exact
through runtime tick 1200.
"""

from __future__ import annotations

import json
import os
import pathlib
import subprocess
import tempfile
from typing import Any


ROOT = pathlib.Path(__file__).resolve().parents[2]
MAGMA = ROOT / "magma"
GAME = MAGMA / "magma_game"
FORK_TICK = 600
FINAL_TICK = 1200


class CampaignError(RuntimeError):
    pass


def _position(index: int) -> tuple[float, float, float]:
    # Three-block spacing prevents this persistence campaign from turning
    # into an accidental same-family collision fixture.
    return (12.5 + (index % 13) * 3.0, 5.0, 12.5 + (index // 13) * 3.0)


def _definitions() -> list[dict[str, Any]]:
    definitions: list[dict[str, Any]] = []
    eid = 61000

    # Every ordinary living model family accepted by the common fixture path.
    mob_types = (
        2, 3, 4, 5, 6, 7, 10, 11, 12, 13, 14, 15, 16, 17,
        23, 24, 25, 26, 27, 32, 35, 36, 39, 40, 41, 51, 52,
        53, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65,
    )
    for entity_type in mob_types:
        x, y, z = _position(len(definitions))
        definitions.append({
            "tick": 0, "type": "spawn_mob_fixture",
            "entity": entity_type, "eid": eid,
            "x": x, "y": y, "z": z,
            "vx": 0.0, "vy": 0.0, "vz": 0.0, "yaw": 0.0,
            "health": 1.0, "no_ai": 1, "hurt_time": 0,
            "death_time": 0, "hurt_resistant_time": 0, "size": 1,
        })
        eid += 1

    for index in range(16):
        x, y, z = _position(len(definitions))
        definitions.append({
            "tick": 0, "type": "spawn_armor_stand_fixture", "eid": eid,
            "x": x, "y": y, "z": z,
            "vx": 0.0, "vy": 0.0, "vz": 0.0,
            "yaw": float(index * 15), "pitch": 0.0, "health": 20.0,
            "on_ground": 0, "no_gravity": 0, "invisible": 0,
            "status": 0, "disabled_slots": 0, "ticks_existed": index,
            "fire": -1, "punch_cooldown": 0,
        })
        eid += 1

    for index in range(16):
        x, y, z = _position(len(definitions))
        definitions.append({
            "tick": 0, "type": "spawn_minecart_fixture",
            "kind": index % 6, "eid": eid,
            "x": x, "y": y, "z": z,
            "vx": 0.0, "vy": 0.0, "vz": 0.0,
            "yaw": 0.0, "pitch": 0.0, "reverse": 0,
            "rolling_amplitude": 0, "rolling_direction": 1,
            "damage": 0.0, "fuel": 0, "push_x": 0.0, "push_z": 0.0,
            "tnt_fuse": -1, "hopper_enabled": 1,
            "transfer_cooldown": -1,
            "entity_seed48": 0x123400000000 + index,
            "entity_have_gaussian": 0, "entity_gaussian": 0.0,
        })
        eid += 1

    for index in range(16):
        x, y, z = _position(len(definitions))
        definitions.append({
            "tick": 0, "type": "spawn_end_crystal_fixture", "eid": eid,
            "x": x, "y": y, "z": z,
            "inner_rotation": index, "show_bottom": index & 1,
            "has_beam": 0, "beam_x": 0, "beam_y": 0, "beam_z": 0,
        })
        eid += 1

    for index in range(16):
        x, y, z = _position(len(definitions))
        definitions.append({
            "tick": 0, "type": "spawn_item_fixture", "eid": eid,
            "x": x, "y": y, "z": z,
            "vx": 0.0, "vy": 0.0, "vz": 0.0,
            "item": 1 + (index % 5), "count": 1, "meta": 0,
            "age": index, "pickup_delay": 32767,
            "controlled_stationary": 1,
        })
        eid += 1

    if len(definitions) != 103:
        raise AssertionError(f"campaign definition count is {len(definitions)}")
    return definitions


def _setup_events(reverse: bool) -> tuple[list[dict[str, Any]], list[int]]:
    definitions = _definitions()
    ordered = list(reversed(definitions)) if reverse else definitions
    expected = [int(row["eid"]) for row in ordered]
    events: list[dict[str, Any]] = [
        {"tick": 0, "type": "restore_loaded_entity_order",
         "order": order, "eid": eid}
        for order, eid in enumerate(expected)
    ]
    events.extend(ordered)
    events.extend([
        {"tick": FORK_TICK, "type": "write_chunk_store",
         "file": "world.bin", "dim": 0},
        {"tick": FORK_TICK, "type": "write_runtime_checkpoint",
         "file": "runtime.bin"},
    ])
    return events, expected


def _run(
    root: pathlib.Path, name: str, events: list[dict[str, Any]], ticks: int,
) -> tuple[subprocess.CompletedProcess[str], dict[str, bytes]]:
    script = root / f"{name}.jsonl"
    script.write_text("".join(
        json.dumps(event, separators=(",", ":"), sort_keys=True) + "\n"
        for event in events))
    paths = {
        "state": root / f"{name}.state.jsonl",
        "blocks": root / f"{name}.blocks.u16le",
        "sky": root / f"{name}.sky.u8",
        "block_light": root / f"{name}.block_light.u8",
        "biomes": root / f"{name}.biomes.u8",
        "heights": root / f"{name}.heights.u16le",
    }
    environment = dict(os.environ)
    environment.update({
        "MAGMA_CAPSULE_DIR": str(root),
        "MAGMA_NATIVE_SAVE_DIR": str(root),
        "MAGMA_BLOCKS_OUT": str(paths["blocks"]),
        "MAGMA_SKY_LIGHT_OUT": str(paths["sky"]),
        "MAGMA_BLOCK_LIGHT_OUT": str(paths["block_light"]),
        "MAGMA_BIOMES_OUT": str(paths["biomes"]),
        "MAGMA_HEIGHTS_OUT": str(paths["heights"]),
        # The live superflat fixture has a one-view-distance resident world.
        # Keep raw probes inside its guaranteed resident core even though the
        # entity stores deliberately span a wider coordinate range.
        "MAGMA_BLOCKS_BOX": "8,0,8,18,16,18",
    })
    result = subprocess.run([
        str(GAME), "--world", "superflat", "--seed", "904771",
        "--headless", "--ticks", str(ticks), "--view-distance", "1",
        "--mobs", "off", "--script", str(script),
        "--state-out", str(paths["state"]), "--render", "off",
        "--pace", "unlimited",
    ], cwd=MAGMA, env=environment, stdout=subprocess.PIPE,
       stderr=subprocess.STDOUT, text=True, check=False)
    return result, {
        key: path.read_bytes() for key, path in paths.items() if path.exists()
    }


def _require_success(result: subprocess.CompletedProcess[str], label: str) -> None:
    if result.returncode != 0:
        raise CampaignError(
            f"{label} failed (rc={result.returncode}):\n{result.stdout}")


def _campaign(root: pathlib.Path, reverse: bool) -> None:
    label = "reverse" if reverse else "forward"
    setup, expected_order = _setup_events(reverse)
    direct_result, direct = _run(
        root, f"{label}_direct", setup, FINAL_TICK + 1)
    _require_success(direct_result, f"{label} direct campaign")
    rows = direct["state"].splitlines(keepends=True)
    if len(rows) != FINAL_TICK + 1:
        raise CampaignError(f"{label}: emitted {len(rows)} state rows")
    initial = json.loads(rows[0])
    if initial.get("loaded_entity_order") != expected_order:
        raise CampaignError(f"{label}: loaded order was not preserved")
    if len(initial.get("entities", [])) != len(expected_order):
        raise CampaignError(
            f"{label}: expected {len(expected_order)} initial entities, got "
            f"{len(initial.get('entities', []))}")

    reload_events = [
        {"tick": 0, "type": "attach_chunk_store",
         "file": "world.bin", "dim": 0},
        {"tick": 0, "type": "load_runtime_checkpoint",
         "file": "runtime.bin"},
    ]
    reload_result, reloaded = _run(
        root, f"{label}_reload", reload_events,
        FINAL_TICK - FORK_TICK + 1)
    _require_success(reload_result, f"{label} reloaded campaign")
    reload_rows = reloaded.pop("state").splitlines(keepends=True)
    if rows[FORK_TICK:] != reload_rows:
        raise CampaignError(
            f"{label}: state continuation differs after tick {FORK_TICK}")
    direct.pop("state")
    if direct != reloaded:
        raise CampaignError(
            f"{label}: raw world continuation differs at tick {FINAL_TICK}")


def main() -> int:
    if not GAME.is_file():
        raise CampaignError(f"missing {GAME}; run make -C magma game")
    temporary_root = ROOT / ".tmp"
    temporary_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(
            prefix="netherite-mixed-campaign-", dir=temporary_root) as raw:
        root = pathlib.Path(raw)
        _campaign(root, reverse=False)
        # Reuse no checkpoint pathname or state between the causal branches.
        reverse_root = root / "reverse"
        reverse_root.mkdir()
        _campaign(reverse_root, reverse=True)
    print(
        "PASS mixed checkpoint campaign: 103 entities, five stores, opposite "
        "loaded orders, exact tick-600 reload through tick 1200")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (CampaignError, OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"FAIL mixed checkpoint campaign: {exc}", file=os.sys.stderr)
        raise SystemExit(1)
