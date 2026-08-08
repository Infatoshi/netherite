#!/usr/bin/env python3
"""Lock cold-growth and fail-closed residual capacity boundaries."""

from __future__ import annotations

import pathlib
import re


ROOT = pathlib.Path(__file__).resolve().parents[2]


def require_tokens(relative: str, tokens: tuple[str, ...]) -> None:
    text = (ROOT / relative).read_text(encoding="utf-8")
    for token in tokens:
        if token not in text:
            raise RuntimeError(f"{relative} lost capacity evidence {token!r}")


def main() -> int:
    require_tokens("magma/game/test_sheep_mating_runtime.c", (
        "fill_living_capacity", "fill_xp_capacity",
        "full XP hot store grows without overwriting an orb",
        "GM_MOB_PARTICLE_BATCH_CAPACITY + 1",
        "particle batch stream grows without losing the oldest birth batch",
        "chicken egg capacity fixture fills exact item store"))
    require_tokens("magma/game/test_xp_bottle_live.c", (
        "XP and authoritative loaded order both grow past hot caps",
        "XP update trace grows past its hot order cap",
        "grown XP payload and order survive checkpoint reload",
        "grown XP update trace survives checkpoint reload"))
    require_tokens("magma/game/test_horse_runtime.c", (
        "capacity_rejection_is_atomic",
        "insufficient drop capacity rejects death, RNG, state, and events atomically"))
    require_tokens("magma/game/test_hanging_runtime.c", (
        "item-frame cold store grows beyond historical maximum",
        "grown item-frame store survives checkpoint reload"))
    require_tokens("magma/game/test_chest_loot.c", (
        "GM_LIVE_OVERFLOW_INITIAL * 8 + 1",
        "opening >old overflow capacity succeeds via cold growth"))
    require_tokens("magma/game/test_mob_live.c", (
        "saturated hot projectile pool grows for pending ghast fireball",
        'CHECK(GM_MOB_CAPACITY>7'))
    require_tokens("magma/game/test_tnt_explosion.c", (
        "standalone end-crystal store grows beyond hot capacity",
        "grown end-crystal payload survives checkpoint reload",
        "falling-block store grows beyond hot capacity",
        "world-event storage grows past falling-block hot capacity without loss",
        "firework store grows beyond hot capacity",
        "grown firework store survives checkpoint reload",
        "firework event storage grows without dropping dense launch/explode order",
        "firework twinkle storage grows beyond fixed audio capacity",
        "grown firework event and twinkle stores survive checkpoint reload"))
    require_tokens("magma/game/test_tnt_explosion.c", (
        "projectile store grows beyond hot capacity",
        "grown projectile store preserves payload and render views",
        "grown projectile store survives checkpoint reload"))
    require_tokens("magma/game/test_tnt_explosion.c", (
        "wither store grows beyond hot capacity",
        "grown wither store preserves payload, order, and render views",
        "grown wither store survives checkpoint reload"))
    require_tokens("magma/game/test_tnt_explosion.c", (
        "area-effect-cloud store grows beyond hot capacity",
        "grown area-effect-cloud store preserves payload and loaded order",
        "grown area-effect-cloud store survives checkpoint reload"))
    require_tokens("magma/game/test_tnt_explosion.c", (
        "primed-TNT store grows beyond hot capacity",
        "grown primed-TNT store preserves payload and loaded order",
        "grown primed-TNT store survives checkpoint reload"))
    require_tokens("magma/game/test_tnt_explosion.c", (
        "lightning store grows beyond hot capacity",
        "grown lightning store preserves payload and render views",
        "grown lightning store survives checkpoint reload"))
    require_tokens("magma/game/test_tnt_explosion.c", (
        "weather-event storage grows without dropping dense lightning order",
        "grown weather events retain exact insertion order",
        "grown weather-event store survives checkpoint reload"))
    require_tokens("magma/game/test_tnt_explosion.c", (
        "sound-event storage grows without dropping dense audio order",
        "grown sound events retain exact insertion order",
        "grown sound-event store survives checkpoint reload"))
    require_tokens("magma/game/test_tnt_explosion.c", (
        "particle-event storage grows without dropping dense visual order",
        "grown particle events retain exact terminal payload",
        "grown particle-event store survives checkpoint reload"))
    require_tokens("magma/game/test_fishing.c", (
        "fish-event storage grows without dropping dense cast order",
        "grown fish-event store survives checkpoint reload"))
    require_tokens("magma/game/test_furnace_output_oracle.c", (
        "r.smelt_event_count == GM_RUNTIME_SMELT_EVENTS + 1",
        "r.smelt_events_cap > GM_RUNTIME_SMELT_EVENTS"))
    require_tokens("magma/game/test_brewing_live.c", (
        "test_brew_event_cold_growth",
        "r.brew_event_count == GM_RUNTIME_BREW_EVENTS + 1",
        "r.brew_events_cap > GM_RUNTIME_BREW_EVENTS"))
    require_tokens("magma/game/test_minecart_live.c", (
        "minecart store grows beyond hot capacity",
        "grown minecart store preserves payload and render order",
        "grown minecart store survives checkpoint reload"))
    require_tokens("magma/game/test_end_gateway.c", (
        "End-gateway store grows beyond hot capacity",
        "grown End-gateway store preserves insertion payload",
        "grown End-gateway store survives checkpoint reload"))
    require_tokens("magma/game/test_loaded_order_capacity.c", (
        "loaded-entity order grows beyond hot capacity",
        "loaded-tile order grows beyond hot capacity",
        "tickable-tile order grows beyond hot capacity",
        "grown cross-dimension causal orders survive checkpoint reload"))
    require_tokens("magma/game/test_tile_capacity.c", (
        "furnace store grows beyond hot capacity",
        "comparator store grows beyond hot capacity",
        "daylight-detector store grows beyond hot capacity",
        "grown tile stores survive checkpoint reload"))
    require_tokens("magma/game/test_structure_registry_capacity.c", (
        "villager claims grow beyond the former resident limit",
        "village collection queue grows without dropping its tail",
        "natural igloo claims grow beyond the former resident limit",
        "natural swamp-witch claims grow beyond the former limit",
        "mansion starts and marker residents grow together",
        "monument starts grow beyond the former registry limit",
        "End-city starts grow beyond the former registry limit",
        "all grown structure registries survive one checkpoint reload"))
    require_tokens("magma/game/test_structure_block_runtime.c", (
        "Structure Blocks and Templates grow past former fixed limits",
        "GM_STRUCTURE_TEMPLATE_TILES_MAX + 1",
        "GM_STRUCTURE_TEMPLATE_ENTITIES_MAX + 1",
        "GM_RUNTIME_STRUCTURE_TEMPLATES_MAX + 3",
        "GM_RUNTIME_STRUCTURE_BLOCKS_MAX + 3",
        "structure_template_capacity.bin"))
    require_tokens("magma/game/test_stack_tag_capacity.c", (
        "GM_RUNTIME_STACK_TAGS_MAX + 1",
        "stack-tag store did not grow past 8192",
        "grown stack-tag store did not continue",
        "GM_RUNTIME_ITEM_NAMES + 1",
        "item-name store did not grow past 64",
        "GM_RUNTIME_ITEM_NAME_LENGTH_MAX",
        "long custom item name did not continue",
        "overlong custom item name was accepted",
        "stack_tag_capacity.bin"))
    require_tokens("magma/game/test_living_cold_slot.c", (
        "all generated fields",
        "gm_mobs_living_cold_append_hot",
        "living cold checkpoint round-trip",
        "dynamic cold cloud cooldown/checkpoint",
        "cold mating/caravan/mount identity",
        "cold pig mount identity", "cold horse mount identity",
        "cold boat mount identity/checkpoint"))
    require_tokens("magma/game/living_cold_slot.generated.h", (
        "Generated by verify/completeness/generate_living_cold_slot.py",
        "gm_living_cold_from_hot", "gm_living_cold_to_hot"))
    require_tokens("magma/game/frame_capture.c", (
        "gm_mobs_living_count(&r->mobs)",
        "frame_entity_views_reserve(c, (int)fc_ents_needed)",
        "frame_leash_views_reserve(c, living_views)",))
    require_tokens("magma/game/window_compose.c", (
        "r->shulkers_cap + r->armor_stands_cap",))
    require_tokens("magma/game/runtime.h", (
        "#define GM_RUNTIME_POTION_EFFECTS 27",))
    require_tokens("magma/game/mob_live.h", (
        "#define GM_MOB_EFFECT_CAPACITY 27",))
    require_tokens("magma/game/test_spawner_live.c", (
        "block-spawner store grows beyond former 64-tile limit",
        "block-spawner potentials grow beyond former 16-row limit",
        "grown block-spawner stores and potentials survive checkpoint reload",
        "runtime.mobs.spawners_cap > GM_SPAWNERS"))
    require_tokens("magma/game/test_minecart_live.c", (
        "minecart spawner potentials grow beyond former 16-row limit",
        "cart.spawner_potential_count == 18"))
    require_tokens("magma/game/test_structure_block_runtime.c", (
        "Structure captures all grown spawner potentials",
        "Structure placement deep-copies grown spawner potentials"))
    require_tokens("magma/game/test_specialized_mob_capacity.c", (
        "evoker casts grow beyond the former fang limit",
        "llama attacks grow spit and ordered event streams together",
        "Snow Golem attacks grow beyond the former launch limit",
        "skeleton-trap shots grow beyond the four-rider burst limit",
        "terminal-particle stream grows without losing a death batch",
        "all grown specialized-mob registries survive checkpoint reload"))
    require_tokens("magma/game/test_piston_capacity.c", (
        "boundaries[] = {63, 64, 65, 257}",
        "scheduled capacity failed",
        "piston-capacity-checkpoint.bin"))
    require_tokens("magma/game/runtime.c", (
        "new_cap = old_cap > 0 ? old_cap * 2 : 16",
        "runtime_scheduled_ticks_reserve",
        "runtime_end_crystals_reserve",
        "runtime_falling_blocks_reserve", "runtime_world_events_reserve",
        "runtime_fireworks_reserve", "runtime_firework_events_reserve",
        "runtime_firework_twinkles_reserve",
        "runtime_projectiles_reserve", "runtime_projectile_free_slot",
        "runtime_withers_reserve", "runtime_area_effect_clouds_reserve",
        "runtime_area_effect_cooldown_set",
        "runtime_living_targets",
        "runtime_primed_tnt_reserve",
        "runtime_lightning_reserve",
        "runtime_weather_events_reserve",
        "runtime_fish_events_reserve",
        "runtime_smelt_events_reserve", "runtime_brew_events_reserve",
        "runtime_sound_events_reserve", "runtime_particle_events_reserve",
        "runtime_minecarts_reserve",
        "runtime_end_gateways_reserve",
        "runtime_loaded_entity_order_reserve",
        "runtime_loaded_tile_order_reserve",
        "runtime_furnaces_reserve",
        "RUNTIME_COLD_RESERVE",
        "runtime_checkpoint_write_structure_templates",
        "runtime_checkpoint_read_structure_templates",
        "runtime_village_count_villagers",
        "runtime_village_count_golems",
        "gm_mobs_living_next_slot(&r->mobs, &cursor)",
        "need > 1048576", "mobs.xp_orb_cold", "mobs.living_cold",
        "mobs.loaded_order_cold", "mobs.tick_update_order_cold",
        "inline_mob_accumulated"))
    require_tokens("magma/game/mob_live.c", (
        "xp_cold_reserve", "loaded_order_reserve", "tick_order_reserve",
        "MOB_COLD_RESERVE",
        "XP_COLD_SAFETY_MAX", "LOADED_ORDER_SAFETY_MAX",
        "TICK_ORDER_SAFETY_MAX"))
    require_tokens("magma/game/live_sim.c", (
        "live_overflow_reserve",
        "GM_LIVE_OVERFLOW_SAFETY_MAX",
        "The hot tick loop remains fixed"))
    recorder = (ROOT / "java/Minecraft/src/main/java/qrl/Recorder.java").read_text(
        encoding="utf-8")
    for token in ("int count = loaded.size();", "int emax = ents.size();"):
        if token not in recorder:
            raise RuntimeError(
                f"Java authoritative entity capture lost {token!r}")
    if "N_ENTITIES_MAX" in recorder:
        raise RuntimeError(
            "Java authoritative entity capture silently regained a fixed cap")
    runtime_test = (ROOT / "magma/game/test_runtime.c").read_text(
        encoding="utf-8")
    if re.search(
            r"memset\(\s*(?:r->|r\.)pistons\s*,\s*0\s*,\s*"
            r"sizeof\s+(?:r->|r\.)pistons\s*\)", runtime_test):
        raise RuntimeError(
            "test_runtime clears only sizeof(pointer) bytes of piston state")
    if "CLEAR_PISTONS" not in runtime_test or "pistons_cap" not in runtime_test:
        raise RuntimeError("test_runtime lost capacity-aware piston reset")
    require_tokens("verify/completeness/anvil_to_capsule.py", (
        '"status": "reject"', "active_unsupported"))
    print(
        "PASS capacity boundary: living/XP/item/particle/piston/queue "
        "saturation is atomic or growable, import rejects overflow, and "
        "checkpoint boundaries are covered")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError) as exc:
        print(f"FAIL capacity boundary: {exc}")
        raise SystemExit(1)
