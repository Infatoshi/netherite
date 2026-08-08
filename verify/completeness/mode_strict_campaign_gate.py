#!/usr/bin/env python3
"""Lock the finite game-mode and integrated-command strict surfaces."""

from __future__ import annotations

import json
import pathlib
import subprocess


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def require(value: bool, message: str) -> None:
    if not value:
        raise RuntimeError(message)


def run(target: str, marker: str) -> None:
    subprocess.run(["make", "-C", "magma", target], cwd=ROOT, check=True,
                   stdout=subprocess.DEVNULL)
    result = subprocess.run([str(ROOT / "magma" / target)], cwd=ROOT,
                            check=True, text=True, capture_output=True)
    require(marker in result.stdout + result.stderr,
            f"{target} lost strict pass marker")


def main() -> int:
    receipt = json.loads((HERE / "mode_strict_campaign_receipt.json")
                         .read_text(encoding="utf-8"))
    require(receipt["schema"] == "netherite.mode_strict_campaign"
            and receipt["version"] == 1
            and receipt["todos"] == ["MODE-01", "MODE-02"]
            and len(receipt["game_modes"]) == 4
            and receipt["integrated_server_commands"] == 47
            and receipt["simulation_commands"] == 45
            and receipt["command_registry_open"] == 0,
            "mode strict receipt changed")
    commands = json.loads((HERE / "command_registry_manifest.json")
                          .read_text(encoding="utf-8"))
    require(len(commands["supported"]) == 45
            and len(commands["host_control"]) == 2
            and commands["open"] == [],
            "command registry no longer has a complete classification")
    run("game/test_game_mode_runtime", "PASS game-mode runtime")
    run("game/test_command_block_tick", "command_block_tick: PASS")
    print("PASS MODE strict campaign: four game modes and all 47 integrated "
          "command classes are classified with 45 executable boundaries")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, subprocess.CalledProcessError,
            json.JSONDecodeError) as error:
        print(f"FAIL MODE strict campaign: {error}")
        raise SystemExit(1)
