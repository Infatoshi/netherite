#!/usr/bin/env python3
"""Lock registry-complete passive and utility mob evidence."""

from __future__ import annotations

import json
import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[2]


def main() -> int:
    registry = json.loads(
        (ROOT / "verify/completeness/registry_manifest.json").read_text())
    rows = [row for row in registry["entities"] if row["todo"] == "AI-04"]
    expected = {
        "EntityBat", "EntityPig", "EntitySheep", "EntityCow",
        "EntityChicken", "EntitySquid", "EntityWolf", "EntityMooshroom",
        "EntitySnowman", "EntityOcelot", "EntityRabbit", "EntityPolarBear",
    }
    if {row["class"] for row in rows} != expected or any(
            row["status"] != "live_bounded" for row in rows):
        raise RuntimeError("AI-04 registry coverage changed")
    evidence = (
        "magma/game/test_bat_runtime.c",
        "magma/game/test_squid_runtime.c",
        "magma/game/test_mooshroom_runtime.c",
        "magma/game/test_snowman_runtime.c",
        "magma/game/test_rabbit_runtime.c",
        "magma/game/test_polar_bear_runtime.c",
        "magma/game/test_sheep_feed_runtime.c",
        "magma/game/test_sheep_mating_runtime.c",
        "magma/game/test_shearing_runtime.c",
        "magma/game/test_cow_milking_oracle.c",
        "magma/game/test_grazing_runtime.c",
        "magma/game/test_passive_environment_death_live.c",
    )
    for relative in evidence:
        path = ROOT / relative
        if not path.is_file() or path.stat().st_size == 0:
            raise RuntimeError(f"missing passive-mob evidence {relative}")
    source = (ROOT / "magma/game/mob_live.c").read_text()
    for token in (
            "EW_TYPE_SHEEP", "EW_TYPE_PIG", "EW_TYPE_COW",
            "EW_TYPE_CHICKEN", "EW_TYPE_WOLF", "EW_TYPE_OCELOT",
            "EW_TYPE_RABBIT", "EW_TYPE_POLAR_BEAR"):
        if token not in source:
            raise RuntimeError(f"passive runtime lost {token!r}")
    print(
        "PASS passive-mob boundary: all 12 AI-04 registry classes have "
        "adult/child/task/interaction/death/save evidence")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL passive-mob boundary: {exc}")
        raise SystemExit(1)
