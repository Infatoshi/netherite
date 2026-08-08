#!/usr/bin/env python3
"""Exact real-Java/native player death inventory and Vanishing Curse gate."""

import argparse
import json
import pathlib
import subprocess

from test_dragon_crystal_notification import request


MAGMA = pathlib.Path(__file__).resolve().parents[1]
NATIVE = MAGMA / "game" / "test_player_death_inventory_oracle"


def stack(slot, item, count=1, meta=0, repair=0, enchants=()):
    return {
        "slot": slot, "item": item, "count": count, "meta": meta,
        "repair": repair,
        "enchants": [{"id": ench, "level": level}
                     for ench, level in enchants],
    }


def cases():
    full = [stack(slot, 4, 1, slot & 3) for slot in range(36)]
    full += [stack(36, 313, meta=7), stack(37, 312, meta=8),
             stack(38, 311, meta=9), stack(39, 310, meta=10),
             stack(40, 442)]
    all_vanishing = [dict(value, enchants=[{"id": 71, "level": 1}])
                     for value in full]
    return (
        ("empty", 0, 0, 700000, False, []),
        ("one_main", 1, 2, 700100, False,
         [stack(0, 3, 12, 2)]),
        ("slot_order", 12345, 67890, 700200, False, [
            stack(40, 442), stack(39, 310, meta=19),
            stack(9, 260, count=4), stack(0, 1, count=2, meta=3),
            stack(36, 313, meta=7), stack(38, 311, meta=11),
        ]),
        ("vanishing_mix", (1 << 48) - 1, 7, 700300, False, [
            stack(0, 276, meta=33, enchants=((71, 1),)),
            stack(1, 297, count=3),
            stack(38, 443, meta=12, enchants=((71, 1),)),
            stack(40, 442),
        ]),
        ("payload", 0x123456789ABC, 0x0FEDCBA98765,
         700400, False, [
            stack(0, 276, meta=33, repair=7,
                  enchants=((16, 3), (34, 2))),
            stack(38, 311, meta=101, repair=3,
                  enchants=((7, 2), (34, 1))),
            stack(40, 403, enchants=((70, 1),)),
        ]),
        ("keep_inventory", 99, 101, 700500, True, [
            stack(0, 276, meta=4, enchants=((71, 1),)),
            stack(1, 297, count=8), stack(39, 310), stack(40, 442),
        ]),
        ("full_41", 314159, 271828, 700600, False, full),
        ("all_vanishing_41", 11, 13, 700700, False, all_vanishing),
    )


def native(player_seed, math_seed, next_id, keep, stacks):
    command = [str(NATIVE), str(player_seed), str(math_seed), str(next_id),
               str(int(keep)), str(len(stacks))]
    for value in stacks:
        enchants = value.get("enchants", [])
        command += [str(value["slot"]), str(value["item"]),
                    str(value.get("count", 1)), str(value.get("meta", 0)),
                    str(value.get("repair", 0)), str(len(enchants))]
        for enchantment in enchants:
            command += [str(enchantment["id"]),
                        str(enchantment["level"])]
    return json.loads(subprocess.check_output(command, text=True))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25600)
    parser.add_argument("--case")
    args = parser.parse_args()
    selected = [value for value in cases()
                if not args.case or value[0] == args.case]
    if not selected:
        raise SystemExit("unknown case")
    request(args.port, "server_step_lock")
    try:
        for name, player_seed, math_seed, next_id, keep, stacks in selected:
            action = {
                "player_seed48": player_seed,
                "math_seed48": math_seed,
                "next_entity_id": next_id,
                "keep_inventory": keep,
                "stacks": stacks,
            }
            java = request(
                args.port, "player_death_inventory_locked", action)
            c_result = native(
                player_seed, math_seed, next_id, keep, stacks)
            if java != c_result:
                raise AssertionError(
                    f"{name}\njava={json.dumps(java, sort_keys=True)}"
                    f"\nnative={json.dumps(c_result, sort_keys=True)}")
    finally:
        request(args.port, "server_step_unlock")
    print(f"PASS real Java/native: {len(selected)} player death inventory cases")


if __name__ == "__main__":
    main()
