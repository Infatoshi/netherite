#!/usr/bin/env python3
"""Run and lock ITEM-07 fishing, vanilla loot, and explorer-map closure."""

from __future__ import annotations

import json
import pathlib
import subprocess


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def require(value: bool, message: str) -> None:
    if not value:
        raise RuntimeError(message)


def run_binary(target: str, expected: str) -> None:
    subprocess.run(["make", "-C", "magma", target], cwd=ROOT, check=True,
                   stdout=subprocess.DEVNULL)
    result = subprocess.run([str(ROOT / "magma" / target)],
                            cwd=ROOT, check=True, text=True,
                            capture_output=True)
    require(expected in result.stdout, f"{target} lost strict pass receipt")


def main() -> int:
    receipt = json.loads((HERE / "item_strict_campaign_receipt.json")
                         .read_text(encoding="utf-8"))
    require(receipt == {
        "schema": "netherite.item_strict_campaign",
        "version": 1,
        "todo": "ITEM-07",
        "vanilla_loot_table_identities": 81,
        "custom_resource_pack_policy":
            "rejected_outside_vanilla_1_11_2_product_scope",
        "fishing": ["block_collision", "living_collision",
                    "dimension_unload", "rng", "loot", "xp", "events",
                    "checkpoint"],
        "explorer_maps": ["monument", "mansion"],
        "explorer_map_state": ["scale_2", "tracking",
                               "unlimited_tracking", "target_decoration",
                               "display_name", "stack_nbt"],
        "continuation": "pre_restock_final_native_checkpoint_byte_exact",
    }, "ITEM-07 strict receipt changed")
    surfaces = json.loads((HERE / "surface_registry_manifest.json")
                          .read_text(encoding="utf-8"))
    require(len(surfaces["loot_tables"]) == 81,
            "ITEM-07 vanilla loot registry cardinality changed")
    run_binary("game/test_fishing", "fishing: PASS")
    run_binary("game/test_village_runtime", "village_runtime: PASS")
    print("PASS ITEM-07 strict campaign: 81 vanilla loot identities, fishing "
          "collision/unload, and both checkpoint-exact explorer maps")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, subprocess.CalledProcessError,
            json.JSONDecodeError) as error:
        print(f"FAIL ITEM-07 strict campaign: {error}")
        raise SystemExit(1)
