#!/usr/bin/env python3
"""Real-Java capture to native restore for the represented ItemStack NBT subset."""

import argparse
import json
import pathlib
import subprocess
import tempfile

from test_dragon_crystal_notification import request


MAGMA = pathlib.Path(__file__).resolve().parents[1]


CASES = [
    {
        "item": 276, "count": 1, "meta": 120, "repair": 7,
        "name": "Oracle Blade",
        "enchants": [{"id": 16, "level": 3}, {"id": 34, "level": 1}],
    },
    {
        "item": 403, "count": 1, "meta": 0, "repair": 3,
        "name": "Archive",
        "enchants": [{"id": 70, "level": 1}, {"id": 9, "level": 2}],
    },
]


def native_restore(java_stack):
    event = {
        "tick": 0,
        "type": "set_inventory",
        "slot": java_stack["slot"],
        "item": java_stack["id"],
        "count": java_stack["count"],
        "meta": java_stack["meta"],
    }
    enchants = java_stack["enchants"]
    if enchants:
        event["n_ench"] = len(enchants)
        for index, (enchantment, level) in enumerate(enchants):
            event[f"e{index}"] = (enchantment << 16) | level
    if java_stack["repair_cost"]:
        event["repair_cost"] = java_stack["repair_cost"]
    if java_stack["custom_name"]:
        event["custom_name"] = java_stack["custom_name"]
    temp_root = MAGMA / ".tmp"
    temp_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(
            prefix="itemstack_capsule_", dir=temp_root) as temp:
        root = pathlib.Path(temp)
        script = root / "fixture.jsonl"
        state = root / "state.jsonl"
        script.write_text(json.dumps(event) + "\n", encoding="utf-8")
        subprocess.run([
            str(MAGMA / "magma_game"), "--world", "superflat",
            "--headless", "--ticks", "1", "--script", str(script),
            "--state-out", str(state), "--render", "off",
            "--pace", "unlimited",
        ], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        row = json.loads(state.read_text(encoding="utf-8").splitlines()[0])
    return next(value for value in row["inventory"]
                if value["slot"] == java_stack["slot"])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25600)
    args = parser.parse_args()
    request(args.port, "server_step_lock")
    try:
        for slot, source in enumerate(CASES):
            result = request(args.port, "inventory_stack_capture_locked", {
                "slot": slot, "stack": source,
            })
            java = result["stack"]
            if java["nbt_subset_exact"] is not True:
                raise AssertionError(f"supported Java stack rejected: {java!r}")
            native = native_restore(java)
            expected = {
                "slot": java["slot"], "item": java["id"],
                "count": java["count"], "meta": java["meta"],
                "enchants": java["enchants"],
                "repair_cost": java["repair_cost"],
                "custom_name": java["custom_name"],
            }
            actual = {
                "slot": native["slot"], "item": native["item"],
                "count": native["count"], "meta": native["meta"],
                "enchants": native["enchants"],
                "repair_cost": native.get("repair_cost", 0),
                "custom_name": native.get("custom_name", ""),
            }
            if expected != actual:
                raise AssertionError(
                    f"Java/native ItemStack differs: {expected!r} != {actual!r}")
        unsupported = request(
            args.port, "inventory_stack_capture_locked", {
                "slot": 0,
                "stack": {"item": 276, "count": 1, "meta": 0,
                          "lore": "not represented"},
            })["stack"]
        if unsupported["nbt_subset_exact"] is not False:
            raise AssertionError("unsupported Lore NBT was not rejected")
    finally:
        request(args.port, "server_step_unlock")
    print("PASS real Java/capsule/native: 2 exact ItemStack metadata restores "
          "+ unsupported-NBT negative")


if __name__ == "__main__":
    main()
