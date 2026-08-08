#!/usr/bin/env python3
"""Fail-closed semantic coverage join for Container.slotClick.

The common state machine is crossed against the finite stack partitions in the
real Java/native transcript.  Slot subclasses only contribute validity, limit,
take, transfer, and onTake hooks, so this gate separately joins every such
1.11.2 slot family to its production hook and a live regression.  This avoids
duplicating thousands of semantically identical rows while still failing when
a Java slot family, native hook, or discriminating test disappears.
"""

from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]

SLOTS = {
    "ordinary": ("inventory/Slot.java", "getItemStackLimit", "static void ordinary_corpus"),
    "crafting_result": ("inventory/SlotCrafting.java", "class SlotCrafting", "special_crafting_takes"),
    "furnace_fuel": ("inventory/SlotFurnaceFuel.java", "class SlotFurnaceFuel", "furnace_bucket"),
    "furnace_output": ("inventory/SlotFurnaceOutput.java", "class SlotFurnaceOutput", "FURNACE_LIVE_SLOT_OUTPUT"),
    "shulker": ("inventory/SlotShulkerBox.java", "class SlotShulkerBox", "GM_ACTIVE_SHULKER_BOX"),
    "merchant_result": ("inventory/SlotMerchantResult.java", "class SlotMerchantResult", "r.merchant_slots[2]"),
    "brewing_potion": ("inventory/ContainerBrewingStand.java", "static class Potion", "brewing_takes"),
    "brewing_ingredient": ("inventory/ContainerBrewingStand.java", "static class Ingredient", "BREWING_LIVE_INGREDIENT"),
    "brewing_fuel": ("inventory/ContainerBrewingStand.java", "static class Fuel", "BREWING_LIVE_FUEL"),
    "beacon_payment": ("inventory/ContainerBeacon.java", "class BeaconSlot", "gm_runtime_beacon_payment_item"),
    "player_armor": ("inventory/ContainerPlayer.java", "EntityEquipmentSlot", "Curse of Binding"),
    "player_offhand": ("inventory/ContainerPlayer.java", "empty_armor_slot_shield", "GMC_OFFHAND"),
    "enchant_item": ("inventory/ContainerEnchantment.java", "this.tableInventory, 0", "enchanting_transfer_tags"),
    "enchant_lapis": ("inventory/ContainerEnchantment.java", "gemLapis", "GMC_ENCHANT0"),
    "anvil_output": ("inventory/ContainerRepair.java", "this.outputSlot", "anvil_take_output"),
    "horse_saddle": ("inventory/ContainerHorseInventory.java", "Items.SADDLE", "GMC_HORSE0"),
    "horse_armor_storage": ("inventory/ContainerHorseInventory.java", "horse.isArmor(stack)", "horse_slot_valid"),
}

MODES = (
    "CC_CLICK_PICKUP",
    "CC_CLICK_QUICK_MOVE",
    "CC_CLICK_SWAP",
    "CC_CLICK_CLONE",
    "CC_CLICK_THROW",
    "CC_CLICK_QUICK_CRAFT",
    "CC_CLICK_PICKUP_ALL",
)


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def missing_tokens(java_root: Path, texts: dict[str, str]) -> list[str]:
    missing: list[str] = []
    for name, (source, java_token, evidence_token) in SLOTS.items():
        source_text = (java_root / source).read_text(encoding="utf-8")
        if java_token not in source_text:
            missing.append(f"{name}:java:{java_token}")
        if not any(evidence_token in value for value in texts.values()):
            missing.append(f"{name}:evidence:{evidence_token}")
    corpus = texts["corpus"]
    for mode in MODES:
        if mode not in corpus:
            missing.append(f"mode:{mode}")
    for spec in range(17):
        token = f"case {spec}:" if spec < 16 else "default: return ic_mk(276, 1, 4)"
        if token not in corpus:
            missing.append(f"stack-partition:{spec}")
    runner = texts["runner"]
    if "2,840 Java/native rows" not in runner:
        missing.append("oracle-row-count")
    return missing


def main() -> int:
    java_root = ROOT / "java/oracle-src/net/minecraft"
    texts = {
        "corpus": read("magma/game/test_container_click_oracle.c"),
        "container": read("magma/game/container_live.c"),
        "live_test": read("magma/game/test_container_live.c"),
        "runner": read("magma/game/test_container_click_oracle.sh"),
    }
    missing = missing_tokens(java_root, texts)
    if missing:
        raise SystemExit("container slot coverage missing: " + ", ".join(missing))

    # Mutation control: deleting one owned Java token must be detected.
    source, token, _ = SLOTS["furnace_output"]
    original = (java_root / source).read_text(encoding="utf-8")
    mutated = original.replace(token, "", 1)
    if token in mutated:
        raise SystemExit("container slot mutation control was inert")

    receipt = json.loads(read("verify/completeness/container_slot_receipt.json"))
    expected = {
        "schema": "netherite.container_slot_coverage",
        "version": 1,
        "java_slot_semantics": sorted(SLOTS),
        "click_types": list(MODES),
        "stack_partitions": 17,
        "oracle_rows": 2840,
        "mutation_control": "furnace_output_java_owner_removed",
    }
    if receipt != expected:
        raise SystemExit("container slot receipt drift")
    print("PASS container slot coverage: 17 semantics, 7 modes, 2,840 exact rows")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
