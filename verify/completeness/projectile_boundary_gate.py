#!/usr/bin/env python3
"""Lock registry-complete bounded projectile lifecycle evidence."""

from __future__ import annotations

import json
import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[2]
REGISTRY = ROOT / "verify/completeness/registry_manifest.json"


def require_tokens(relative: str, tokens: tuple[str, ...]) -> None:
    text = (ROOT / relative).read_text(encoding="utf-8")
    for token in tokens:
        if token not in text:
            raise RuntimeError(f"{relative} lost projectile evidence {token!r}")


def main() -> int:
    registry = json.loads(REGISTRY.read_text(encoding="utf-8"))
    rows = [row for row in registry["entities"] if row["todo"] == "ENT-06"]
    expected = {
        "EntityEgg", "EntityTippedArrow", "EntitySnowball",
        "EntityLargeFireball", "EntitySmallFireball", "EntityEnderPearl",
        "EntityEnderEye", "EntityExpBottle", "EntitySpectralArrow",
        "EntityDragonFireball",
    }
    if {row["class"] for row in rows} != expected or any(
            row["status"] != "live_bounded" for row in rows):
        raise RuntimeError("ENT-06 registry coverage changed")
    for relative in (
        "magma/game/test_throwable_launch_oracle.c",
        "magma/game/test_arrow_impact_oracle.c",
        "magma/game/test_arrow_block_impact_oracle.c",
        "magma/game/test_arrow_payload_oracle.c",
        "magma/game/test_arrow_pickup_oracle.c",
        "magma/game/test_egg_impact_oracle.c",
        "magma/game/test_snowball_impact_oracle.c",
        "magma/game/test_xp_bottle_impact_oracle.c",
        "magma/game/test_ender_eye_oracle.c",
        "magma/game/test_ender_pearl_impact_oracle.c",
        "magma/game/test_ender_pearl_gateway_oracle.c",
        "magma/game/test_throwable_portal_oracle.c",
        "magma/game/test_throwable_foreign_dimension_oracle.c",
        "magma/game/test_throwable_save.c",
    ):
        path = ROOT / relative
        if not path.is_file() or path.stat().st_size == 0:
            raise RuntimeError(f"missing projectile evidence {relative}")
    require_tokens("verify/completeness/spectral_arrow_family_gate.py", (
        "bow_launch_and_ammunition_selection",
        "exact_configurable_glowing_duration",
        "uuid_entity_rng_and_semantic_nbt_continuation"))
    require_tokens("magma/game/runtime.c", (
        "tick_projectiles", "runtime_block_hit",
        "throwable_tick_done"))
    print(
        "PASS projectile boundary: all 10 ENT-06 registry rows are "
        "live-bounded with launch, motion, block/entity terminal, payload, "
        "pickup, portal/dimension, and reload evidence")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL projectile boundary: {exc}")
        raise SystemExit(1)
