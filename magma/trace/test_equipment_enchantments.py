#!/usr/bin/env python3
"""Exact Java/native equipment-enchantment effects for Minecraft 1.11.2."""

import argparse
import json
import pathlib
import subprocess

from test_dragon_crystal_notification import request


MAGMA = pathlib.Path(__file__).resolve().parents[1]
NATIVE = MAGMA / "game" / "test_equipment_enchantment_oracle"


def cases():
    values = []
    for level in range(4):
        for seed in (0, 1, 2, 3, 7, 15, 12345, (1 << 48) - 1):
            values.append((f"respiration_l{level}_s{seed}", {
                "mode": "respiration", "level": level, "air": 10,
                "player_seed48": seed,
            }))
    for level in (0, 1, 3):
        for on_ground in (False, True):
            values.append((f"aqua_l{level}_g{int(on_ground)}", {
                "mode": "aqua", "level": level,
                "on_ground": on_ground,
            }))
    for level in (0, 1, 3):
        for creative in (False, True):
            values.append((f"binding_l{level}_c{int(creative)}", {
                "mode": "binding", "level": level,
                "creative": creative,
            }))
    thorns_fixtures = (
        ((0, 0, 0, 0, 0, 0), (0, 0, 0, 0), (0, 0, 0, 0)),
        ((0, 0, 1, 0, 0, 0), (0, 0, 0, 0), (0, 0, 0, 0)),
        ((0, 0, 0, 1, 0, 0), (0, 0, 0, 0), (0, 0, 0, 0)),
        ((0, 0, 0, 0, 1, 0), (0, 0, 0, 0), (0, 0, 0, 0)),
        ((0, 0, 0, 0, 0, 1), (0, 0, 0, 0), (0, 0, 0, 0)),
        ((1, 0, 0, 0, 0, 1), (0, 0, 0, 0), (0, 0, 0, 0)),
        ((0, 3, 1, 2, 3, 1), (0, 0, 0, 0), (0, 0, 0, 0)),
        ((1, 1, 3, 3, 3, 3), (1, 2, 3, 4), (2, 3, 4, 5)),
        ((0, 0, 11, 0, 0, 0), (0, 0, 0, 0), (8, 0, 0, 0)),
        ((0, 0, 20, 20, 20, 20), (3, 3, 3, 3), (9, 8, 7, 6)),
    )
    for fixture_index, (levels, unbreaking, damage) in enumerate(
            thorns_fixtures):
        for seed in (0, 1, 2, 7, 15, 12345, (1 << 48) - 1):
            values.append((f"thorns_f{fixture_index}_s{seed}", {
                "mode": "thorns", "levels": levels,
                "unbreaking": unbreaking, "damage": damage,
                "player_seed48": seed,
            }))
    armor_damage_fixtures = (
        (0.1, (0, 0, 0, 0), (0, 0, 0, 0)),
        (3.0, (1, 0, 0, 0), (2, 3, 4, 5)),
        (8.0, (1, 2, 3, 4), (8, 7, 6, 5)),
        (20.0, (3, 3, 3, 3), (20, 30, 40, 50)),
        (31.75, (0, 4, 0, 8), (100, 110, 120, 130)),
    )
    for fixture_index, (amount, unbreaking, damage) in enumerate(
            armor_damage_fixtures):
        for seed in (0, 1, 2, 7, 15, 12345, (1 << 48) - 1):
            values.append((f"armor_damage_f{fixture_index}_s{seed}", {
                "mode": "armor_damage", "amount": amount,
                "unbreaking": unbreaking, "damage": damage,
                "player_seed48": seed,
            }))
    depth_vectors = (
        (1.0, 0.0, 0.0, 0.1),
        (1.0, 0.25, 37.0, 0.1),
        (-0.5, -1.0, -143.0, 0.13),
    )
    for level in (0, 1, 2, 3, 4):
        for on_ground in (False, True):
            for vector_index, (forward, strafe, yaw, ai_speed) in enumerate(
                    depth_vectors):
                values.append((
                    f"depth_l{level}_g{int(on_ground)}_v{vector_index}", {
                        "mode": "depth", "level": level,
                        "on_ground": on_ground, "forward": forward,
                        "strafe": strafe, "yaw": yaw,
                        "ai_speed": ai_speed,
                    }))
    for level, on_ground, seed in (
            (1, True, 0), (2, True, 0), (2, True, 12345),
            (4, True, (1 << 48) - 1), (20, True, 7),
            (2, False, 0)):
        values.append((f"frost_l{level}_g{int(on_ground)}_s{seed}", {
            "mode": "frost", "level": level,
            "on_ground": on_ground, "player_seed48": seed,
        }))
    return values


def native(action):
    mode = action["mode"]
    if mode == "respiration":
        args = [mode, action["level"], action["air"],
                action["player_seed48"]]
    elif mode == "aqua":
        args = [mode, action["level"], int(action["on_ground"])]
    elif mode == "binding":
        args = [mode, action["level"], int(action["creative"])]
    elif mode == "thorns":
        args = [mode, action["player_seed48"], *action["levels"],
                *action["unbreaking"], *action["damage"]]
    elif mode == "armor_damage":
        args = [mode, action["player_seed48"], action["amount"],
                *action["unbreaking"], *action["damage"]]
    elif mode == "depth":
        args = [mode, action["level"], int(action["on_ground"]),
                action["forward"], action["strafe"], action["yaw"],
                action["ai_speed"]]
    else:
        args = [mode, action["level"], int(action["on_ground"]),
                action["player_seed48"]]
    command = [str(NATIVE), *(str(value) for value in args)]
    return json.loads(subprocess.check_output(command, text=True))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25600)
    parser.add_argument("--case")
    args = parser.parse_args()
    selected = [entry for entry in cases()
                if not args.case or entry[0] == args.case]
    if not selected:
        raise SystemExit("unknown case")
    request(args.port, "server_step_lock")
    try:
        for name, action in selected:
            java = request(
                args.port, "equipment_enchantment_locked", action)
            c_result = native(action)
            if java != c_result:
                raise AssertionError(
                    f"{name}\njava={json.dumps(java, sort_keys=True)}"
                    f"\nnative={json.dumps(c_result, sort_keys=True)}")
    finally:
        request(args.port, "server_step_unlock")
    print(f"PASS real Java/native: {len(selected)} exact equipment enchantments")


if __name__ == "__main__":
    main()
