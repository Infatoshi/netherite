#!/usr/bin/env python3
"""Run and lock the long mixed AI-03 save/reload campaign."""

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
    receipt = json.loads((HERE / "special_mob_strict_campaign_receipt.json")
                         .read_text(encoding="utf-8"))
    require(receipt["schema"] == "netherite.special_mob_strict_campaign"
            and receipt["version"] == 1 and receipt["todo"] == "AI-03"
            and receipt["ticks"] == 1200
            and len(receipt["registry_rows"]) == 9
            and len(set(receipt["registry_rows"])) == 9
            and receipt["reload_after_ticks"]
                == [1, 20, 40, 100, 300, 600, 1199]
            and receipt["continuation"]
                == "future_driving_checkpoint_bytes_exact",
            "AI-03 strict campaign receipt changed")
    subprocess.run(
        ["make", "-C", "magma", "game/test_special_mob_strict_campaign"],
        cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    result = subprocess.run(
        [str(ROOT / "magma/game/test_special_mob_strict_campaign")],
        cwd=ROOT, check=True, text=True, capture_output=True)
    require(result.stdout.strip() == (
        "special_mob_strict_campaign: PASS 1200 ticks, 9 registry rows, "
        "target moves, fangs, bullets, owners, 7 reload boundaries"),
        "AI-03 mixed campaign did not emit its exact pass receipt")
    print("PASS AI-03 strict campaign: all nine rows compose for 1,200 "
          "ticks and seven reload boundaries")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError,
            json.JSONDecodeError) as error:
        print(f"FAIL AI-03 strict campaign: {error}")
        raise SystemExit(1)
