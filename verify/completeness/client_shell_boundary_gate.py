#!/usr/bin/env python3
"""Lock bounded mode, screen, inventory, and HUD shell evidence."""

from __future__ import annotations

import json
import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[2]
GROUPS = {
    "MODE-01": (
        "magma/game/player_ctl.c", "magma/game/test_player_ctl.c",
        "magma/game/test_hud.c", "magma/game/test_config.c",
        "magma/game/test_game_mode_runtime.c"),
    "MODE-03": (
        "magma/game/runtime.c", "magma/game/test_runtime.c",
        "verify/completeness/test_native_checkpoint.py"),
    "MODE-04": (
        "magma/game/config.c", "magma/game/test_config.c",
        "magma/app/game_main.c", "magma/game/screen.c"),
    "UI-01": (
        "verify/completeness/test_native_save_ui.sh",
        "magma/app/game_main.c", "magma/game/screen.c"),
    "UI-02": (
        "magma/game/test_container_click_oracle.c",
        "magma/game/test_container_live.c",
        "magma/game/test_chest_loot.c"),
    "UI-03": (
        "magma/game/player_preview.c", "magma/game/test_input_map.c",
        "magma/game/test_screen.c"),
    "UI-04": (
        "magma/game/test_ender_chest_runtime.c",
        "magma/game/test_shulker_box_runtime.c",
        "magma/game/test_beacon_oracle.c",
        "magma/game/test_horse_runtime.c"),
    "UI-05": (
        "magma/game/test_hud.c", "verify/ui_hud/run_ui_hud_gates.sh"),
}


def main() -> int:
    surfaces = json.loads((ROOT / "verify/completeness/surface_registry_manifest.json").read_text())
    if len(surfaces["containers"]) != 13 or len(surfaces["guis"]) != 13:
        raise RuntimeError("container/GUI registry cardinality changed")
    for todo, evidence in GROUPS.items():
        for relative in evidence:
            path = ROOT / relative
            if not path.is_file() or path.stat().st_size == 0:
                raise RuntimeError(f"{todo}: missing evidence {relative}")
    config = (ROOT / "magma/game/config.c").read_text()
    runtime = (ROOT / "magma/game/runtime.c").read_text()
    tests = (ROOT / "magma/game/test_config.c").read_text()
    mode_tests = (ROOT / "magma/game/test_game_mode_runtime.c").read_text()
    for token in (
            '"--gamemode"', '"survival"', '"creative"',
            '"adventure"', '"spectator"'):
        if token not in config:
            raise RuntimeError(f"MODE-01: missing launch-profile token {token}")
    if "r->tape_game_mode = cfg->game_mode" not in runtime:
        raise RuntimeError("MODE-01: launch game mode is not wired to runtime")
    if "numeric creative/adventure/spectator profile is accepted" not in tests:
        raise RuntimeError("MODE-01: launch-profile regression is missing")
    for token in (
            "GM_MODE_SPECTATOR", "player_disable_damage",
            "player_creative", "CanDestroy", "CanPlaceOn"):
        if token not in mode_tests:
            raise RuntimeError(
                f"MODE-01: missing focused capability token {token}")
    print(
        "PASS client-shell boundary: supported modes/options, title/save UI, "
        "13 container and 13 GUI identities, item/preview rendering, tile "
        "screens, and HUD have fail-closed evidence")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL client-shell boundary: {exc}")
        raise SystemExit(1)
