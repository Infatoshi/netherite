#!/usr/bin/env python3
"""Lock the measured AI-01 live-bounded Husk boundary."""

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
    manifest = json.loads(
        (HERE / "husk_family_manifest.json").read_text(encoding="utf-8"))
    require(manifest["schema"] == "netherite.husk_family_gate"
            and manifest["version"] == 1 and manifest["todo"] == "AI-01",
            "invalid Husk manifest identity")
    require(manifest["behavior_oracle"] == {
        "difficulty": "normal",
        "fresh_world_hunger_ticks": 140,
        "aged_world_hunger_ticks": 280,
        "exhaustion_per_tick": 0.005,
        "burns_in_daylight": False,
    }, "Husk behavior boundary changed")
    require(manifest["death_loot_oracle"] == {
        "husk_death_rows": 9, "aggregate_death_rows": 261,
        "husk_loot_rows": 15, "aggregate_hostile_loot_rows": 375,
        "terminal_xp": 5, "native_terminal_cases": 25,
    }, "Husk death/loot evidence changed")
    registry = json.loads(
        (HERE / "registry_manifest.json").read_text(encoding="utf-8"))
    row = next(row for row in registry["entities"]
               if row["class"] == "EntityHusk")
    require(row["status"] == "live_bounded" and row["todo"] == "AI-01",
            "Husk registry row is not live-bounded")
    source_has(ROOT / "java/oracle-src/net/minecraft/entity/monster/EntityHusk.java", (
        "return false;", "MobEffects.HUNGER, 140 * (int)f",
        "LootTableList.ENTITIES_HUSK", "SoundEvents.ENTITY_HUSK_STEP"))
    source_has(ROOT / "java/Minecraft/src/main/java/qrl/Recorder.java", (
        'case "husk_melee_locked":', "oracleHuskMeleeLocked",
        "EnumDifficulty.NORMAL"))
    source_has(ROOT / "magma/game/mob_live.c", (
        "type!=EW_TYPE_HUSK", "husk_hunger_duration",
        "GM_MOB_SOUND_HUSK_AMBIENT", "case EW_TYPE_HUSK:"))
    source_has(ROOT / "magma/game/test_husk_runtime.c", (
        "Husk is immune to daylight ignition",
        "save/reload preserves exact Hunger duration",
        "280-tick aged-world cast"))
    source_has(ROOT / "magma/trace/test_husk_melee.py", (
        '"husk_melee_locked"', "(13000, 140)", "(1512000, 280)"))
    source_has(ROOT / "magma/trace/test_hostile_loot.py", (
        '("husk", 1)', "exact hostile loot rows"))
    source_has(ROOT / "magma/trace/test_hostile_player_death.py", (
        '("husk", 1)', "exact composed living deaths"))
    source_has(ROOT / "magma/game/test_hostile_death_live.c", (
        "attacks=25 terminal=25", "particles=500"))
    source_has(ROOT / "magma/game/entity_render.c", (
        '"EntityHusk"', "CR_MOB_HUSK"))
    source_has(ROOT / "magma/assets/build_sound_manifest.py", (
        '"entity.husk.ambient"', '"entity.husk.step"'))
    print("PASS Husk family: bounded inherited AI, daylight immunity, exact "
          "Normal-difficulty Hunger, loot/death, continuation, render, audio")


if __name__ == "__main__":
    try:
        main()
    except (KeyError, OSError, StopIteration, ValueError, RuntimeError) as error:
        print(f"FAIL Husk family: {error}")
        raise SystemExit(1)
