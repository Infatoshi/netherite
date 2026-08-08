#!/usr/bin/env python3
"""Lock the measured AI-04 live-bounded Polar Bear boundary."""

import json
import pathlib


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def source_has(path, tokens):
    source = path.read_text(encoding="utf-8")
    for token in tokens:
        require(token in source, f"{path.relative_to(ROOT)} lost {token!r}")


def main():
    manifest = json.loads((HERE / "polar_bear_family_manifest.json")
                          .read_text(encoding="utf-8"))
    require(manifest["schema"] == "netherite.polar_bear_family_gate"
            and manifest["version"] == 1 and manifest["todo"] == "AI-04"
            and manifest["classification"] == "live_bounded",
            "invalid Polar Bear manifest identity")
    require(manifest["melee_oracle"] == {
        "attack_damage": 6, "attack_ticks": 20,
        "reach_squared": 4.6, "warning_distance_multiplier": 2.0,
        "warning_ticks": 40, "standing_animation_ticks": 6,
    }, "Polar Bear melee boundary changed")
    require(manifest["death_loot_oracle"] == {
        "polar_bear_death_rows": 9, "aggregate_death_rows": 261,
        "polar_bear_loot_rows": 15,
        "aggregate_hostile_loot_rows": 375,
        "adult_xp_range": [1, 3], "native_terminal_cases": 25,
        "native_terminal_particles": 500,
    }, "Polar Bear death/loot evidence changed")
    registry = json.loads((HERE / "registry_manifest.json")
                          .read_text(encoding="utf-8"))
    row = next(row for row in registry["entities"]
               if row["class"] == "EntityPolarBear")
    require(row["status"] == "live_bounded" and row["todo"] == "AI-04",
            "Polar Bear registry row is not live-bounded")
    source_has(ROOT / "java/oracle-src/net/minecraft/entity/monster/EntityPolarBear.java", (
        "class AIMeleeAttack", "4.0F + attackTarget.width",
        "this.attackTick = 20", "this.warningSoundTicks = 40"))
    source_has(ROOT / "java/Minecraft/src/main/java/qrl/Recorder.java", (
        'case "polar_bear_melee_locked":', "oraclePolarBearMeleeLocked",
        'getDeclaredField("warningSoundTicks")'))
    source_has(ROOT / "magma/game/mob_live.c", (
        "type == EW_TYPE_POLAR_BEAR && polar_aggro",
        "GM_MOB_SOUND_POLAR_BEAR_WARNING",
        "m->polar_stand_animation[i] / 6.0F"))
    source_has(ROOT / "magma/game/test_polar_bear_runtime.c", (
        "adult acquires player near child", "exact six damage",
        "continuation is byte-exact"))
    source_has(ROOT / "magma/trace/test_polar_bear_melee.py", (
        '"polar_bear_melee_locked"', '"warning_sound_ticks": 40',
        '"target_health": 4.0'))
    source_has(ROOT / "magma/trace/test_hostile_loot.py", (
        '("polar_bear", 1)', "exact hostile loot rows"))
    source_has(ROOT / "magma/trace/test_hostile_player_death.py", (
        '("polar_bear", 1)', "exact composed living deaths"))
    source_has(ROOT / "magma/game/test_hostile_death_live.c", (
        "attacks=25 terminal=25", "particles=500"))
    source_has(ROOT / "magma/game/entity_render.c", (
        '"EntityPolarBear"', "M_POLAR_BEAR", "CR_MOB_POLAR_BEAR"))
    source_has(ROOT / "magma/assets/build_sound_manifest.py", (
        '"entity.polar_bear.ambient"', '"entity.polar_bear.warning"'))
    print("PASS Polar Bear family: bounded child defense, exact melee/loot/death, "
          "continuation, model, and audio")


if __name__ == "__main__":
    try:
        main()
    except (KeyError, OSError, StopIteration, ValueError, RuntimeError) as error:
        print(f"FAIL Polar Bear family: {error}")
        raise SystemExit(1)
