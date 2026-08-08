#!/usr/bin/env python3
"""Fail-closed evidence join for every distinct 1.11.2 item callback."""

from __future__ import annotations

import pathlib

from item_callback_census import census


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]

# An owner may have several reflected callbacks, but one action-family oracle
# owns their common lifecycle. Client-only continuations are explicitly handed
# to the screen/render row instead of being mistaken for a gameplay fallback.
GROUPS: dict[str, tuple[str, str]] = {
    "ItemArmor": ("native", "magma/game/test_runtime.c"),
    "ItemArmorStand": ("native", "magma/game/test_armor_stand_runtime.c"),
    "ItemBanner": ("native", "magma/game/test_special_item_use_oracle.c"),
    "ItemBed": ("native", "magma/game/test_special_item_use_oracle.c"),
    "ItemBlock": ("native", "magma/game/test_item_block_oracle.c"),
    "ItemBlockSpecial": ("native", "magma/game/test_item_block_oracle.c"),
    "ItemBoat": ("native", "magma/game/test_boat_oracle.c"),
    "ItemBow": ("native", "magma/game/test_bow_release_oracle.c"),
    "ItemBucket": ("native", "magma/game/test_player_ctl.c"),
    "ItemBucketMilk": ("native", "magma/game/test_player_ctl.c"),
    "ItemCarrotOnAStick": ("native", "magma/game/test_sheep_feed_runtime.c"),
    "ItemDoor": ("native", "magma/game/test_item_block_oracle.c"),
    "ItemDye": ("native", "magma/game/test_shearing_runtime.c"),
    "ItemEgg": ("native", "magma/game/test_egg_impact_oracle.c"),
    "ItemElytra": ("native", "magma/game/test_runtime.c"),
    "ItemEmptyMap": ("native", "magma/game/test_runtime.c"),
    "ItemEndCrystal": ("native", "magma/game/test_dragon_crystal_oracle.c"),
    "ItemEnderEye": ("native", "magma/game/test_ender_eye_oracle.c"),
    "ItemEnderPearl": ("native", "magma/game/test_ender_pearl_impact_oracle.c"),
    "ItemExpBottle": ("native", "magma/game/test_xp_bottle_impact_oracle.c"),
    "ItemFireball": ("native", "magma/game/test_player_ctl.c"),
    "ItemFirework": ("native", "magma/game/test_firework.c"),
    "ItemFishingRod": ("native", "magma/game/test_fishing.c"),
    "ItemFlintAndSteel": ("native", "magma/game/test_player_ctl.c"),
    "ItemFood": ("native", "magma/game/test_food_oracle.c"),
    "ItemGlassBottle": ("native", "magma/game/test_player_ctl.c"),
    "ItemHangingEntity": ("native", "magma/game/test_hanging_runtime.c"),
    "ItemHoe": ("native", "magma/game/test_tool_callback_oracle.c"),
    "ItemLead": ("native", "magma/game/test_hanging_runtime.c"),
    "ItemLilyPad": ("native", "magma/game/test_player_ctl.c"),
    "ItemLingeringPotion": ("native", "magma/game/test_player_ctl.c"),
    "ItemMap": ("native", "magma/game/test_map_update_oracle.c"),
    "ItemMinecart": ("native", "magma/game/test_minecart_live.c"),
    "ItemMonsterPlacer": ("native", "magma/game/test_spawner_live.c"),
    "ItemNameTag": ("native", "magma/game/test_sheep_feed_runtime.c"),
    "ItemPickaxe": ("native", "magma/game/test_tool_callback_oracle.c"),
    "ItemPotion": ("native", "magma/game/test_player_ctl.c"),
    "ItemRecord": ("native", "magma/game/test_runtime.c"),
    "ItemRedstone": ("native", "magma/game/test_item_block_oracle.c"),
    "ItemSaddle": ("native", "magma/game/test_sheep_feed_runtime.c"),
    "ItemSeedFood": ("native", "magma/game/test_item_block_oracle.c"),
    "ItemSeeds": ("native", "magma/game/test_item_block_oracle.c"),
    "ItemShears": ("native", "magma/game/test_tool_callback_oracle.c"),
    "ItemShield": ("native", "magma/game/test_player_ctl.c"),
    "ItemSign": ("native", "magma/game/test_special_item_use_oracle.c"),
    "ItemSkull": ("native", "magma/game/test_special_item_use_oracle.c"),
    "ItemSlab": ("native", "magma/game/test_item_block_oracle.c"),
    "ItemSnow": ("native", "magma/game/test_item_block_oracle.c"),
    "ItemSnowball": ("native", "magma/game/test_player_ctl.c"),
    "ItemSpade": ("native", "magma/game/test_tool_callback_oracle.c"),
    "ItemSplashPotion": ("native", "magma/game/test_player_ctl.c"),
    "ItemSword": ("native", "magma/game/test_tool_callback_oracle.c"),
    "ItemTool": ("native", "magma/game/test_tool_callback_oracle.c"),
    "ItemWritableBook": ("UI-02", "java/oracle-src/net/minecraft/item/ItemWritableBook.java"),
    "ItemWrittenBook": ("UI-02", "java/oracle-src/net/minecraft/item/ItemWrittenBook.java"),
}


def main() -> int:
    report = census()
    seen: set[str] = set()
    native = delegated = 0
    for family in report["families"]:
        short = family["owner"].rsplit(".", 1)[-1]
        if short not in GROUPS:
            raise RuntimeError(f"unclassified item callback owner: {short}")
        owner, evidence = GROUPS[short]
        path = ROOT / evidence
        if not path.is_file() or path.stat().st_size == 0:
            raise RuntimeError(f"missing item evidence: {evidence}")
        seen.add(short)
        if owner == "native":
            native += 1
        else:
            delegated += 1
    extra = sorted(set(GROUPS) - seen)
    if extra:
        raise RuntimeError(f"stale item callback evidence owners: {extra}")
    if (report["registry_items"], report["callback_rows"],
            report["implementation_families"]) != (391, 485, 79):
        raise RuntimeError("item callback census cardinality changed")
    print(
        "PASS item callback evidence: 391 items, 485 override rows, "
        f"79 implementations ({native} native, {delegated} UI-owned), "
        f"{len(seen)} fail-closed owner groups")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL item callback evidence: {exc}")
        raise SystemExit(1)
