#!/usr/bin/env python3
"""Run and lock the mixed WORLD-06 spawner continuation campaign."""

from __future__ import annotations

import json
import pathlib
import subprocess

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def require(value: bool, message: str) -> None:
    if not value:
        raise RuntimeError(message)


def main() -> int:
    receipt = json.loads((HERE / "spawner_strict_campaign_receipt.json")
                         .read_text(encoding="utf-8"))
    require(receipt == {
        "schema": "netherite.spawner_strict_campaign",
        "version": 1,
        "todo": "WORLD-06",
        "ticks": 600,
        "entity_families": 13,
        "block_spawners": 12,
        "minecart_spawners": 1,
        "custom_nbt": "class_specific_and_common_living_fields",
        "weighted_potentials": "two_rows_per_block_family",
        "dimension_unload_ticks": [200, 220],
        "reload_after_ticks": [1, 7, 20, 199, 200, 220, 599],
        "continuation": "final_native_checkpoint_byte_exact",
    }, "WORLD-06 strict receipt changed")
    subprocess.run(
        ["make", "-C", "magma", "game/test_spawner_strict_campaign"],
        cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    result = subprocess.run(
        [str(ROOT / "magma/game/test_spawner_strict_campaign")],
        cwd=ROOT, check=True, text=True, capture_output=True)
    require(result.stdout.strip() == (
        "spawner_strict_campaign: PASS 13 entity families, 12 block plus "
        "minecart spawner, weighted custom NBT, 600 ticks, dimension "
        "unload, 7 reload boundaries"),
        "WORLD-06 campaign did not emit its exact pass receipt")
    print("PASS WORLD-06 strict campaign: mixed block/cart spawners, custom "
          "NBT, weighted choice, unload and seven reload boundaries")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, subprocess.CalledProcessError,
            json.JSONDecodeError) as error:
        print(f"FAIL WORLD-06 strict campaign: {error}")
        raise SystemExit(1)
