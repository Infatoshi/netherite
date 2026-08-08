#!/usr/bin/env python3
"""Lock the measured AI-04 live-bounded Rabbit boundary."""

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
    manifest = json.loads((HERE / "rabbit_family_manifest.json")
                          .read_text(encoding="utf-8"))
    require(manifest["schema"] == "netherite.rabbit_family_gate"
            and manifest["version"] == 1 and manifest["todo"] == "AI-04"
            and manifest["classification"] == "live_bounded",
            "invalid Rabbit manifest identity")
    require(manifest["combat_oracle"] == {
        "ordinary_attack_damage": 3, "killer_attack_damage": 8,
        "killer_armor": 8, "reach_squared": 4.6,
    }, "Rabbit combat boundary changed")
    require(manifest["jump_oracle"] == {
        "slow_impulse": 0.2, "fast_impulse": 0.3,
        "elevated_impulse": 0.5, "jump_duration": 10,
    }, "Rabbit jump boundary changed")
    require(manifest["death_loot_oracle"] == {
        "rabbit_death_rows": 9, "aggregate_death_rows": 261,
        "rabbit_loot_rows": 15, "aggregate_hostile_loot_rows": 375,
        "native_terminal_cases": 25, "native_terminal_particles": 500,
    }, "Rabbit death/loot evidence changed")
    require(manifest["farm_oracle"] == {
        "mature_carrot_age": 7, "consumed_carrot_age": 6,
        "raid_cooldown": 40,
    }, "Rabbit farm boundary changed")
    require(manifest["render_audio_contract"] == {
        "model_boxes": 12, "coat_textures": 8, "sounds": 5,
    }, "Rabbit render/audio boundary changed")
    registry = json.loads((HERE / "registry_manifest.json")
                          .read_text(encoding="utf-8"))
    row = next(row for row in registry["entities"]
               if row["class"] == "EntityRabbit")
    require(row["status"] == "live_bounded" and row["todo"] == "AI-04",
            "Rabbit registry row is not live-bounded")
    source_has(ROOT / "java/Minecraft/src/main/java/qrl/Recorder.java", (
        'case "rabbit_locked":', "oracleRabbitLocked", '"RabbitType"'))
    source_has(ROOT / "magma/trace/test_rabbit.py", (
        '"rabbit_locked"', '"killer_health": 2.0', '"jump_duration": 10'))
    source_has(ROOT / "magma/game/mob_live.c", (
        "EW_TYPE_RABBIT", "rabbit_carrot_ticks", "GM_MOB_SOUND_RABBIT_JUMP"))
    source_has(ROOT / "magma/game/test_rabbit_runtime.c", (
        "exact eight attack damage", "exact ten-tick fast hop",
        "raids one mature carrot stage", "continuation is byte-exact"))
    source_has(ROOT / "magma/trace/test_hostile_loot.py", ('("rabbit", 1)',))
    source_has(ROOT / "magma/trace/test_hostile_player_death.py", ('("rabbit", 1)',))
    source_has(ROOT / "magma/game/test_hostile_death_live.c", (
        "attacks=25 terminal=25", "particles=500"))
    source_has(ROOT / "magma/game/entity_render.c", (
        '"EntityRabbit"', "M_RABBIT", "CR_MOB_RABBIT_CAERBANNOG"))
    source_has(ROOT / "magma/assets/build_sound_manifest.py", (
        '"entity.rabbit.ambient"', '"entity.rabbit.jump"'))
    print("PASS Rabbit family: bounded hopping, combat, farm raid, exact "
          "loot/death, continuation, model, and audio")


if __name__ == "__main__":
    try:
        main()
    except (KeyError, OSError, StopIteration, ValueError, RuntimeError) as error:
        print(f"FAIL Rabbit family: {error}")
        raise SystemExit(1)
