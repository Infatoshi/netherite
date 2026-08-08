#!/usr/bin/env python3
"""Run and lock the finite RED-04 slime/piston continuation campaign."""

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
    receipt = json.loads((HERE / "piston_strict_campaign_receipt.json")
                         .read_text(encoding="utf-8"))
    require(receipt["schema"] == "netherite.piston_strict_campaign"
            and receipt["version"] == 1 and receipt["todo"] == "RED-04"
            and receipt["generated_topologies"] == 144
            and receipt["move_sizes"] == list(range(1, 13))
            and receipt["facings"] == list(range(6))
            and receipt["piston_types"] == ["normal", "sticky"]
            and receipt["continuation"]
                == "final_native_checkpoint_byte_exact"
            and receipt["pixel_residual_owner"] == "VIS-07",
            "RED-04 strict campaign receipt changed")
    java = receipt["java_state_evidence"]
    require(java == {
        "generated_topology_cases": 256,
        "named_family_cases_minimum": 500,
        "state_divergences": 0,
        "unrepresented_state_fields": 0,
    }, "RED-04 Java-state join changed")
    fuzz = json.loads((HERE / "redstone_fuzz_256_receipt.json")
                      .read_text(encoding="utf-8"))
    require((fuzz["case_count"], fuzz["pass"], fuzz["fail"],
             fuzz["state_divergences"], fuzz["unrepresented_state_fields"])
            == (256, 256, 0, 0, 0),
            "RED-04 lost exact generated Java topology evidence")
    subprocess.run(
        ["make", "-C", "magma", "game/test_piston_strict_campaign"],
        cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    result = subprocess.run(
        [str(ROOT / "magma/game/test_piston_strict_campaign")],
        cwd=ROOT, check=True, text=True, capture_output=True)
    require(result.stdout.strip() == (
        "piston_strict_campaign: PASS 144 generated topologies, sizes 1..12, "
        "six faces, normal/sticky, entity collision, extension/pull and "
        "mid-motion reload"),
        "RED-04 native campaign did not emit its exact pass receipt")
    print("PASS RED-04 strict campaign: 144 finite-limit slime topologies "
          "compose in every direction with entity collision and exact "
          "mid-motion continuation")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, subprocess.CalledProcessError,
            json.JSONDecodeError) as error:
        print(f"FAIL RED-04 strict campaign: {error}")
        raise SystemExit(1)
