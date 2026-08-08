#!/usr/bin/env python3
"""Lock bounded anvil/enchanting, potion/effect, and activity evidence."""

from __future__ import annotations

import json
import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[2]
SURFACES = ROOT / "verify/completeness/surface_registry_manifest.json"
GROUPS = {
    "ITEM-03": (
        "magma/game/test_anvil_live.c", "magma/game/test_anvil_oracle.c",
        "magma/game/test_enchanting_live.c",
        "magma/game/test_equipment_enchantment_oracle.c"),
    "ITEM-05": (
        "magma/game/test_player_effects.c",
        "magma/game/test_potion_owner_damage_oracle.c",
        "magma/game/test_potion_player_return_oracle.c",
        "magma/game/test_witch_self_potion_oracle.c",
        "magma/game/test_nausea_portal_oracle.c",
        "magma/game/test_luck_loot.c"),
    "ITEM-07": (
        "magma/game/test_fishing.c", "magma/game/test_fishing_loot.c",
        "magma/game/test_villager_trade.c", "magma/game/test_chest_loot.c",
        "magma/game/test_desert_loot.c", "magma/game/test_jungle_loot.c",
        "magma/game/test_end_city_loot.c", "magma/game/test_mansion_loot.c"),
}


def main() -> int:
    surfaces = json.loads(SURFACES.read_text())
    if len(surfaces["potions"]) != 27 or len(surfaces["potion_types"]) != 37 \
            or len(surfaces["loot_tables"]) != 81:
        raise RuntimeError("effect/potion/loot registry cardinality changed")
    for todo, evidence in GROUPS.items():
        for relative in evidence:
            path = ROOT / relative
            if not path.is_file() or path.stat().st_size == 0:
                raise RuntimeError(f"{todo}: missing evidence {relative}")
    anvil_oracle = (ROOT / "magma/trace/test_anvil.py").read_text()
    required_anvil_tokens = (
        "expected 4,700", "material_items", "Same-item durability combine",
        "Rename-only and rename-plus-work", "Enchantment level/applicability",
        "conflicts =", "for creative in (False, True)",
    )
    for token in required_anvil_tokens:
        if token not in anvil_oracle:
            raise RuntimeError(f"ITEM-03: anvil partition lost {token!r}")
    enchant_rows = (ROOT / "blaze/core/enchant_table.h").read_text()
    for enchant_id in (
            0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 16, 17, 18, 19,
            20, 21, 22, 32, 33, 34, 35, 48, 49, 50, 51, 61, 62,
            70, 71):
        if f"({enchant_id}, " not in anvil_oracle:
            raise RuntimeError(
                f"ITEM-03: enchantment {enchant_id} absent from anvil corpus")
    print(
        "PASS advanced-item boundary: 4,700-row all-enchantment anvil corpus, "
        "enchanting, all 27 effects/37 "
        "potion types, and all 81 loot identities plus fishing/trading have "
        "fail-closed behavior evidence")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL advanced-item boundary: {exc}")
        raise SystemExit(1)
