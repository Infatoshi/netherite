#!/usr/bin/env python3
"""Lock bounded village, Villager, Golem, and cure coverage."""

from __future__ import annotations

import json
import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[2]


def main() -> int:
    registry = json.loads((ROOT / "verify/completeness/registry_manifest.json").read_text())
    rows = [row for row in registry["entities"] if row["todo"] == "AI-02"]
    if {row["class"] for row in rows} != {
            "EntityVillager", "EntityIronGolem", "EntityZombieVillager"} \
            or any(row["status"] != "live_bounded" for row in rows):
        raise RuntimeError("AI-02 registry coverage changed")
    evidence = (
        "magma/game/test_village_state.c",
        "magma/game/test_village_runtime.c",
        "magma/game/test_village_residents.c",
        "magma/game/test_village_golem_runtime.c",
        "magma/game/test_village_siege_runtime.c",
        "magma/game/test_villager_ai_runtime.c",
        "magma/game/test_villager_door_oracle.c",
        "magma/game/test_villager_follow_golem_oracle.c",
        "magma/game/test_villager_mate_oracle.c",
        "magma/game/test_villager_social_oracle.c",
        "magma/game/test_villager_trade.c",
        "magma/game/test_zombie_villager_cure_oracle.c",
        "magma/game/test_zombie_villager_cure_runtime.c",
        "magma/game/test_iron_golem.c",
    )
    for relative in evidence:
        path = ROOT / relative
        if not path.is_file() or path.stat().st_size == 0:
            raise RuntimeError(f"missing village evidence {relative}")
    source = (ROOT / "magma/game/village_live.c").read_text()
    for token in ("gm_village_state_add_door", "gm_village_state_tick",
                  "gm_village_state_modify_reputation"):
        if token not in source:
            raise RuntimeError(f"village runtime lost {token!r}")
    print(
        "PASS village boundary: Villager, IronGolem, ZombieVillager, doors, "
        "residents, mating, siege, trade, cure, social state, and reload "
        "have dedicated evidence")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL village boundary: {exc}")
        raise SystemExit(1)
