#!/usr/bin/env python3
"""Lock the measured AI-01 live-bounded Stray boundary."""

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
        (HERE / "stray_family_manifest.json").read_text(encoding="utf-8"))
    require(manifest["schema"] == "netherite.stray_family_gate"
            and manifest["version"] == 1 and manifest["todo"] == "AI-01",
            "invalid Stray manifest identity")
    require(manifest["ranged_oracle"] == {
        "projectile": "EntityTippedArrow",
        "custom_effect_id": 2,
        "custom_effect_amplifier": 0,
        "custom_effect_duration": 600,
        "custom_effect_ambient": False,
        "custom_effect_show_particles": True,
    }, "Stray ranged boundary changed")
    require(manifest["death_loot_oracle"] == {
        "stray_death_rows": 9, "aggregate_death_rows": 261,
        "stray_loot_rows": 15, "aggregate_hostile_loot_rows": 375,
        "terminal_xp": 5, "native_terminal_cases": 25,
    }, "Stray death/loot evidence changed")
    registry = json.loads(
        (HERE / "registry_manifest.json").read_text(encoding="utf-8"))
    row = next(row for row in registry["entities"]
               if row["class"] == "EntityStray")
    require(row["status"] == "live_bounded" and row["todo"] == "AI-01",
            "Stray registry row is not live-bounded")
    source_has(ROOT / "java/oracle-src/net/minecraft/entity/monster/EntityStray.java", (
        "EntityTippedArrow", "MobEffects.SLOWNESS, 600",
        "LootTableList.ENTITIES_STRAY", "SoundEvents.ENTITY_STRAY_STEP"))
    source_has(ROOT / "java/Minecraft/src/main/java/qrl/Recorder.java", (
        'case "stray_arrow_locked":', "oracleStrayArrowLocked",
        'getTagList("CustomPotionEffects", 10)'))
    source_has(ROOT / "magma/game/mob_live.c", (
        "type==EW_TYPE_SKELETON||type==EW_TYPE_STRAY",
        "GM_MOB_SOUND_STRAY_AMBIENT", "GM_HOSTILE_LOOT_POTION_SLOWNESS"))
    source_has(ROOT / "magma/game/runtime.c", (
        "p->arrow_kind = GM_ARROW_TIPPED", "runtime_intern_fixed_loot_tags",
        "minecraft:slowness"))
    source_has(ROOT / "magma/game/test_stray_runtime.c", (
        "Stray arrow carries exact visible Slowness-I 600 payload",
        "native save restores Stray tipped-arrow loot NBT",
        "flying Stray arrow continuation is byte-exact"))
    source_has(ROOT / "magma/trace/test_stray_arrow.py", (
        '"stray_arrow_locked"', '"duration": 600',
        '"show_particles": True'))
    source_has(ROOT / "magma/trace/test_hostile_loot.py", (
        '("stray", 1)', "exact hostile loot rows"))
    source_has(ROOT / "magma/trace/test_hostile_player_death.py", (
        '("stray", 1)', "exact composed living deaths"))
    source_has(ROOT / "magma/game/test_hostile_death_live.c", (
        "attacks=25 terminal=25", "particles=500"))
    source_has(ROOT / "magma/game/entity_render.c", (
        '"EntityStray"', "CR_MOB_STRAY"))
    source_has(ROOT / "magma/assets/build_sound_manifest.py", (
        '"entity.stray.ambient"', '"entity.stray.step"'))
    print("PASS Stray family: bounded inherited ranged AI, exact tipped "
          "arrow/loot NBT, death, continuation, render, audio")


if __name__ == "__main__":
    try:
        main()
    except (KeyError, OSError, StopIteration, ValueError, RuntimeError) as error:
        print(f"FAIL Stray family: {error}")
        raise SystemExit(1)
