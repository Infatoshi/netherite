#!/usr/bin/env python3
"""Run and lock the long mixed AI-02 save/reload campaign."""

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
    receipt = json.loads((HERE / "village_strict_campaign_receipt.json")
                         .read_text(encoding="utf-8"))
    require(receipt == {
        "schema": "netherite.village_strict_campaign",
        "version": 1,
        "todo": "AI-02",
        "ticks": 1200,
        "professions": [0, 1, 2, 3, 4, 5],
        "required_transitions": [
            "mating_birth", "iron_golem_village_context",
            "player_reputation", "zombie_villager_cure",
            "door_collection",
        ],
        "reload_after_ticks": [1, 40, 299, 300, 599, 600, 1199],
        "continuation": "final_native_checkpoint_byte_exact",
    }, "AI-02 strict campaign receipt changed")
    subprocess.run(
        ["make", "-C", "magma", "game/test_village_strict_campaign"],
        cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    result = subprocess.run(
        [str(ROOT / "magma/game/test_village_strict_campaign")],
        cwd=ROOT, check=True, text=True, capture_output=True)
    expected = (
        "village_strict_campaign: PASS 1200 ticks, 6 professions, mating, "
        "golem, reputation, zombie cure, 21 doors, 7 reload boundaries")
    require(result.stdout.strip() == expected,
            "AI-02 mixed campaign did not emit its exact pass receipt")
    print("PASS AI-02 strict campaign: 1,200 mixed ticks and seven reload "
          "boundaries end in byte-identical checkpoints")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError,
            json.JSONDecodeError) as error:
        print(f"FAIL AI-02 strict campaign: {error}")
        raise SystemExit(1)
