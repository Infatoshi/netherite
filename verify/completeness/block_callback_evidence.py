#!/usr/bin/env python3
"""Fail-closed action-family evidence join for every block callback owner."""

from __future__ import annotations

import pathlib

from callback_census import census


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]

GROUPS: dict[str, str] = {}


def assign(evidence: str, owners: str) -> None:
    for owner in owners.split():
        if owner in GROUPS:
            raise RuntimeError(f"duplicate block callback owner: {owner}")
        GROUPS[owner] = evidence


assign("verify/completeness/redstone_boundary_gate.py", """
BlockBasePressurePlate BlockButton BlockCommandBlock BlockDaylightDetector
BlockDispenser BlockLever BlockObserver BlockPistonBase BlockPistonExtension
BlockPistonMoving BlockRailBase BlockRailDetector BlockRedstoneComparator
BlockRedstoneDiode BlockRedstoneLight BlockRedstoneOre BlockRedstoneRepeater
BlockRedstoneTorch BlockRedstoneWire BlockTNT BlockTrapDoor BlockTripWire
BlockTripWireHook
""")
assign("magma/game/test_plants_live.c", """
BlockBeetroot BlockBush BlockCactus BlockCarpet BlockChorusFlower
BlockChorusPlant BlockCocoa BlockCrops BlockDoublePlant BlockFarmland
BlockGrass BlockGrassPath BlockLeaves BlockLog BlockMushroom BlockMycelium
BlockNetherWart BlockPotato BlockPumpkin BlockReed BlockSapling BlockSnow
BlockSnowBlock BlockStem BlockVine
""")
assign("magma/game/test_container_live.c", """
BlockAnvil BlockBanner BlockBanner$BlockBannerHanging
BlockBanner$BlockBannerStanding BlockBeacon BlockBed BlockBrewingStand
BlockCake BlockCauldron BlockChest BlockContainer BlockEnchantmentTable
BlockEnderChest BlockFlowerPot BlockFurnace BlockHopper BlockJukebox
BlockNote BlockShulkerBox BlockSign BlockSkull BlockStructure BlockWorkbench
BlockStandingSign
""")
assign("magma/game/test_fluid_live.c", """
BlockDynamicLiquid BlockFrostedIce BlockIce BlockLiquid BlockSponge
BlockStaticLiquid
""")
assign("magma/game/test_portal_live.c", """
BlockEndGateway BlockEndPortal BlockEndPortalFrame BlockPortal
""")
assign("magma/game/test_falling_anvil_oracle.c", """
BlockDragonEgg BlockFalling
""")
assign("magma/game/test_player_ctl.c", """
BlockDoor BlockEndRod BlockFence BlockFenceGate BlockHay BlockLadder
BlockLilyPad BlockMagma BlockSlime BlockSoulSand BlockStairs BlockTorch
BlockWallSign BlockWeb
""")
assign("magma/game/test_block_harvest_oracle.c", """
BlockMobSpawner BlockOre BlockSilverfish BlockStainedGlass
BlockStainedGlassPane
""")
assign("magma/game/test_runtime.c", """
BlockAir BlockBarrier BlockStructureVoid
""")
assign("magma/game/test_explosion_fire_oracle.c", "BlockFire")


def main() -> int:
    report = census()
    seen: set[str] = set()
    direct = delegated = 0
    for family in report["families"]:
        short = family["owner"].rsplit(".", 1)[-1]
        evidence = GROUPS.get(short)
        if evidence is None:
            raise RuntimeError(
                f"unclassified block callback owner: {short}"
                f".{family['callback']}")
        path = ROOT / evidence
        if not path.is_file() or path.stat().st_size == 0:
            raise RuntimeError(f"missing block evidence: {evidence}")
        seen.add(short)
        if family["kind"] == "direct":
            direct += 1
        else:
            delegated += 1
    extra = sorted(set(GROUPS) - seen)
    if extra:
        raise RuntimeError(f"stale block callback evidence owners: {extra}")
    if (report["registry_blocks"], report["callback_rows"],
            report["implementation_families"]) != (236, 770, 306):
        raise RuntimeError("block callback census cardinality changed")
    print(
        "PASS block callback evidence: 236 blocks, 770 override rows, "
        f"306 implementations ({direct} direct, {delegated} delegated/no-op), "
        f"{len(seen)} fail-closed action-family owners")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL block callback evidence: {exc}")
        raise SystemExit(1)
