#include <stdio.h>
#include <string.h>

#include "game/runtime.h"
#include "tile_entity_brewing.h"

int main(void) {
    GmMobLive source;
    GmMobLive restored;
    GmLivingColdSlot cold;
    GmLivingColdSlot round_trip;
    GmMobLive store;
    GmRuntime runtime;
    GmConfig config;
    GmEntityView views[128];
    McAABB boxes[128];
    McAABB all_entities;
    GmAction idle;
    char error[256];
    const char *checkpoint = "living-cold-slot-checkpoint.bin";
    const int slot = 7;

    memset(&source, 0xa5, sizeof source);
    memset(&restored, 0, sizeof restored);
    memset(&cold, 0, sizeof cold);
    memset(&round_trip, 0, sizeof round_trip);
    source.current = 0;
    restored.current = 0;
    gm_living_cold_from_hot(&cold, &source, slot);
    gm_living_cold_to_hot(&restored, slot, &cold);
    gm_living_cold_from_hot(&round_trip, &restored, slot);
    if (memcmp(&cold, &round_trip, sizeof cold)) {
        fprintf(stderr, "FAIL living cold slot round-trip\n");
        return 1;
    }
    memset(&store, 0, sizeof store);
    gm_mobs_init(&store, 771109);
    if (gm_mobs_spawn_exact(
            &store, EW_TYPE_COW, 4101, 1.5, 5.0, 1.5,
            0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0) != 1) {
        fprintf(stderr, "FAIL living cold slot source spawn\n");
        gm_mobs_destroy(&store);
        return 1;
    }
    for (int index = 0; index < 17; ++index)
        if (gm_mobs_living_cold_append_hot(&store, 1) != index) {
            fprintf(stderr, "FAIL living cold slot growth at %d\n", index);
            gm_mobs_destroy(&store);
            return 1;
        }
    if (store.living_cold_count != 17 || store.living_cold_cap < 17
            || !gm_mobs_living_cold_ref(&store, 16)
            || gm_mobs_living_cold_ref(&store, 17)
            || gm_mobs_living_cold_reserve(&store, 1048577)) {
        fprintf(stderr, "FAIL living cold slot boundary\n");
        gm_mobs_destroy(&store);
        return 1;
    }
    gm_mobs_destroy(&store);
    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.mobs = 0;
    config.weather = 0;
    if (!gm_runtime_init(&runtime, &config, error, sizeof error)
            || gm_mobs_spawn_exact(
                &runtime.mobs, EW_TYPE_COW, 5101, 2.5, 5.0, 2.5,
                0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0) != 1) {
        fprintf(stderr, "FAIL living cold checkpoint setup: %s\n", error);
        return 1;
    }
    for (int index = 0; index < 17; ++index) {
        if (index > 0 && gm_mobs_spawn_exact(
                &runtime.mobs, EW_TYPE_COW, 5101 + index,
                2.5 + index, 5.0, 2.5,
                0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0) != 1) {
            fprintf(stderr, "FAIL living cold checkpoint respawn\n");
            gm_runtime_destroy(&runtime);
            return 1;
        }
        if (gm_mobs_living_cold_park_hot(&runtime.mobs, 1)
                != EW_MAX_ENTITIES + index) {
            fprintf(stderr, "FAIL living cold checkpoint growth\n");
            gm_runtime_destroy(&runtime);
            return 1;
        }
    }
    if (!gm_mobs_set_entity_uuid(
            &runtime.mobs, 5117, INT64_C(771109), INT64_C(-771109))) {
        fprintf(stderr, "FAIL living cold staged mutation\n");
        gm_runtime_destroy(&runtime);
        return 1;
    }
    if (!gm_runtime_write_checkpoint(&runtime, checkpoint)
            || !gm_runtime_load_checkpoint(&runtime, checkpoint)
            || runtime.mobs.living_cold_count != 17
            || runtime.mobs.living_cold_cap < 17
            || !gm_mobs_living_cold_ref(&runtime.mobs, 16)
            || gm_mobs_living_cold_ref(
                &runtime.mobs, 16)->store_id != 5117
            || gm_mobs_find_slot_by_eid(&runtime.mobs, 5117)
                != GM_MOB_LIVING_STAGE_SLOT
            || !runtime.mobs.entity_uuid_present[GM_MOB_LIVING_STAGE_SLOT]
            || runtime.mobs.entity_uuid_most[GM_MOB_LIVING_STAGE_SLOT]
                != INT64_C(771109)) {
        fprintf(stderr, "FAIL living cold checkpoint round-trip\n");
        (void)remove(checkpoint);
        gm_runtime_destroy(&runtime);
        return 1;
    }
    (void)remove(checkpoint);
    gm_runtime_destroy(&runtime);

    if (!gm_runtime_init(&runtime, &config, error, sizeof error)) {
        fprintf(stderr, "FAIL living automatic growth setup: %s\n", error);
        return 1;
    }
    runtime.controlled_mobs_enabled = 1;
    for (int index = 0; index < 110; ++index) {
        int slot = gm_mobs_spawn_exact(
            &runtime.mobs, EW_TYPE_COW, 6000 + index,
            100.5 + 3.0 * index, 5.0, 100.5,
            0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0);
        if (slot <= 0 || slot >= EW_MAX_ENTITIES) {
            fprintf(stderr, "FAIL automatic living growth at %d\n", index);
            gm_runtime_destroy(&runtime);
            return 1;
        }
    }
    if (runtime.mobs.living_cold_count != 15
            || gm_mobs_living_count(&runtime.mobs) != 110
            || gm_mobs_loaded_order_count(&runtime.mobs) != 110
            || gm_mobs_fill_views(&runtime.mobs, views, 128) != 110) {
        fprintf(stderr, "FAIL automatic living growth census/render\n");
        gm_runtime_destroy(&runtime);
        return 1;
    }
    for (int index = 0; index < 110; ++index)
        if (views[index].ent_id != 6000 + index) {
            fprintf(stderr, "FAIL automatic living render order at %d\n", index);
            gm_runtime_destroy(&runtime);
            return 1;
        }
    all_entities = mc_aabb_make(0.0, 0.0, 0.0, 500.0, 20.0, 500.0);
    if (gm_mobs_living_boxes(
            &runtime.mobs, runtime.dimension, boxes, 128) != 110
            || gm_mobs_collision_boxes(
                &runtime.mobs, runtime.dimension, 0, boxes, 128) != 110
            || gm_mobs_trigger_collision_boxes(
                &runtime.mobs, runtime.dimension, 0, boxes, 128) != 110
            || gm_mobs_living_count_intersects_aabb(
                &runtime.mobs, runtime.dimension, &all_entities) != 110) {
        fprintf(stderr, "FAIL automatic living collision enumeration\n");
        gm_runtime_destroy(&runtime);
        return 1;
    }
    memset(&idle, 0, sizeof idle);
    gm_runtime_tick(&runtime, idle);
    if (runtime.mobs.tick_update_order_count != 110) {
        fprintf(stderr, "FAIL automatic living tick count: %d\n",
                runtime.mobs.tick_update_order_count);
        gm_runtime_destroy(&runtime);
        return 1;
    }
    for (int index = 0; index < 110; ++index) {
        int eid = -1;
        if (!gm_mobs_tick_update_order_get(&runtime.mobs, index, &eid)
                || eid != 6000 + index) {
            fprintf(stderr, "FAIL automatic living tick order at %d\n", index);
            gm_runtime_destroy(&runtime);
            return 1;
        }
    }
    if (gm_mobs_find_slot_by_eid(&runtime.mobs, 6109)
            != GM_MOB_LIVING_STAGE_SLOT
            || runtime.mobs.entity_ticks_existed[GM_MOB_LIVING_STAGE_SLOT]
                != 1) {
        fprintf(stderr, "FAIL automatic cold living tick state\n");
        gm_runtime_destroy(&runtime);
        return 1;
    }
    if (!gm_runtime_write_checkpoint(&runtime, checkpoint)
            || !gm_runtime_load_checkpoint(&runtime, checkpoint)
            || gm_mobs_living_count(&runtime.mobs) != 110
            || gm_mobs_fill_views(&runtime.mobs, views, 128) != 110) {
        fprintf(stderr, "FAIL automatic living checkpoint continuation\n");
        (void)remove(checkpoint);
        gm_runtime_destroy(&runtime);
        return 1;
    }
    gm_runtime_tick(&runtime, idle);
    if (gm_mobs_find_slot_by_eid(&runtime.mobs, 6109)
            != GM_MOB_LIVING_STAGE_SLOT
            || runtime.mobs.entity_ticks_existed[GM_MOB_LIVING_STAGE_SLOT]
                != 2) {
        fprintf(stderr, "FAIL automatic living post-checkpoint tick\n");
        (void)remove(checkpoint);
        gm_runtime_destroy(&runtime);
        return 1;
    }
    (void)remove(checkpoint);
    gm_runtime_destroy(&runtime);

    if (!gm_runtime_init(&runtime, &config, error, sizeof error)) {
        fprintf(stderr, "FAIL cold interaction setup: %s\n", error);
        return 1;
    }
    for (int index = 0; index < GM_MOB_CAPACITY; ++index)
        if (gm_mobs_spawn_exact(
                &runtime.mobs, EW_TYPE_COW, 7000 + index,
                index * 3.0, 5.0, 0.0,
                0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0) <= 0) {
            fprintf(stderr, "FAIL cold interaction hot fill\n");
            gm_runtime_destroy(&runtime);
            return 1;
        }
    if (gm_mobs_spawn_exact(
            &runtime.mobs, EW_TYPE_IRON_GOLEM, 8000,
            0.0, 5.0, 0.0, 0.0, 0.0, 0.0,
            0.0F, 100.0F, 1, 0, 0, 0) <= 0
            || gm_mobs_spawn_exact(
                &runtime.mobs, EW_TYPE_COW, 8001,
                1.0, 5.0, 0.0, 0.0, 0.0, 0.0,
                0.0F, 10.0F, 1, 0, 0, 0) <= 0) {
        fprintf(stderr, "FAIL cold interaction overflow spawn\n");
        gm_runtime_destroy(&runtime);
        return 1;
    }
    {
        float damage = 0.0F;
        if (!gm_mobs_iron_golem_attack(
                &runtime.mobs, 8000, 8001, &damage)
                || damage < 7.0F || damage > 21.0F) {
            fprintf(stderr, "FAIL two-cold-entity interaction\n");
            gm_runtime_destroy(&runtime);
            return 1;
        }
    }
    gm_mobs_living_cold_flush(&runtime.mobs);
    if (runtime.mobs.living_cold_count != 2
            || runtime.mobs.living_cold[0].golem_attack_timer != 10
            || runtime.mobs.living_cold[1].store_health >= 10.0F) {
        fprintf(stderr, "FAIL two-cold-entity interaction persistence\n");
        gm_runtime_destroy(&runtime);
        return 1;
    }
    gm_runtime_destroy(&runtime);

    if (!gm_runtime_init(&runtime, &config, error, sizeof error)) {
        fprintf(stderr, "FAIL cold caravan setup: %s\n", error);
        return 1;
    }
    for (int index = 0; index < GM_MOB_CAPACITY; ++index)
        if (gm_mobs_spawn_exact(
                &runtime.mobs, EW_TYPE_COW, 12500 + index,
                100.0 + index * 3.0, 5.0, 100.0,
                0.0, 0.0, 0.0, 0.0F, 10.0F,
                1, 0, 0, 0) <= 0) {
            fprintf(stderr, "FAIL cold caravan hot fill\n");
            gm_runtime_destroy(&runtime);
            return 1;
        }
    for (int index = 0; index < 3; ++index)
        if (gm_mobs_spawn_exact(
                &runtime.mobs, EW_TYPE_LLAMA, 13000 + index,
                index * 3.0, 5.0, 0.0,
                0.0, 0.0, 0.0, 0.0F, 15.0F,
                1, 0, 0, 0) <= 0) {
            fprintf(stderr, "FAIL cold caravan llama spawn\n");
            gm_runtime_destroy(&runtime);
            return 1;
        }
    if (!gm_mobs_llama_set_leashed(&runtime.mobs, 13000, 1)
            || !gm_mobs_llama_caravan_join(&runtime.mobs, 13001, 13000)
            || !gm_mobs_llama_caravan_join(&runtime.mobs, 13002, 13001)) {
        fprintf(stderr, "FAIL cold caravan joins\n");
        gm_runtime_destroy(&runtime);
        return 1;
    }
    gm_mobs_living_cold_flush(&runtime.mobs);
    if (runtime.mobs.living_cold_count != 3
            || runtime.mobs.living_cold[0].llama_caravan_tail_eid != 13001
            || runtime.mobs.living_cold[1].llama_caravan_head_eid != 13000
            || runtime.mobs.living_cold[1].llama_caravan_tail_eid != 13002
            || runtime.mobs.living_cold[2].llama_caravan_head_eid != 13001
            || !gm_runtime_write_checkpoint(&runtime, checkpoint)
            || !gm_runtime_load_checkpoint(&runtime, checkpoint)) {
        fprintf(stderr, "FAIL cold caravan identity/checkpoint\n");
        (void)remove(checkpoint);
        gm_runtime_destroy(&runtime);
        return 1;
    }
    {
        GmLlamaState middle, tail;
        if (!gm_mobs_get_llama_state(&runtime.mobs, 13001, &middle)
                || !gm_mobs_get_llama_state(&runtime.mobs, 13002, &tail)
                || middle.caravan_head_eid != 13000
                || middle.caravan_tail_eid != 13002
                || tail.caravan_head_eid != 13001) {
            fprintf(stderr, "FAIL cold caravan restored links\n");
            (void)remove(checkpoint);
            gm_runtime_destroy(&runtime);
            return 1;
        }
    }
    (void)remove(checkpoint);
    gm_runtime_destroy(&runtime);

    if (!gm_runtime_init(&runtime, &config, error, sizeof error)) {
        fprintf(stderr, "FAIL cold mating setup: %s\n", error);
        return 1;
    }
    gm_runtime_set_pose(&runtime, 8.5, 220.0, 8.5, 0.0F, 0.0F);
    for (int index = 0; index < GM_MOB_CAPACITY; ++index)
        if (gm_mobs_spawn_exact(
                &runtime.mobs, EW_TYPE_COW, 11500 + index,
                100.0 + index * 3.0, 220.0, 100.0,
                0.0, 0.0, 0.0, 0.0F, 10.0F,
                1, 0, 0, 0) <= 0) {
            fprintf(stderr, "FAIL cold mating hot fill\n");
            gm_runtime_destroy(&runtime);
            return 1;
        }
    runtime.mobs.next_id = 12000;
    if (gm_mobs_spawn(
            &runtime.mobs, EW_TYPE_SHEEP, 8.0, 220.0, 8.0) <= 0
            || gm_mobs_spawn(
                &runtime.mobs, EW_TYPE_SHEEP, 8.25, 220.0, 8.0) <= 0
            || !gm_mobs_set_animal_breeding_state(
                &runtime.mobs, 12000, 600, 0, 0, 0)
            || !gm_mobs_set_animal_breeding_state(
                &runtime.mobs, 12001, 600, 0, 0, 0)
            || !gm_mobs_set_entity_random_state(
                &runtime.mobs, 12000, UINT64_C(0x23456789abcd), 0, 0.0)
            || !gm_mobs_set_entity_random_state(
                &runtime.mobs, 12001, UINT64_C(0x3456789abcde), 0, 0.0)
            || !gm_mobs_set_next_animal_child_state(
                &runtime.mobs, UINT64_C(0x56789abcdef0), 0, 0.0, 0)) {
        fprintf(stderr, "FAIL cold mating parents\n");
        gm_runtime_destroy(&runtime);
        return 1;
    }
    {
        int first = gm_mobs_find_slot_by_eid(&runtime.mobs, 12000);
        if (first <= 0) {
            fprintf(stderr, "FAIL cold mating first stage\n");
            gm_runtime_destroy(&runtime);
            return 1;
        }
        runtime.mobs.sheep_mate_active[first] = 1;
        runtime.mobs.sheep_mate_eid[first] = 12001;
        runtime.mobs.sheep_mate_delay[first] = 59;
    }
    gm_mobs_living_cold_flush(&runtime.mobs);
    runtime.mobs_enabled = 1;
    runtime.controlled_mobs_enabled = 0;
    runtime.do_mob_loot = 0;
    runtime.mobs.tick = 1;
    runtime.next_entity_id = 12002;
    runtime.world_random_seed48 = UINT64_C(0x123456789abc);
    runtime.math_random_seed48 = UINT64_C(0x13579bdf2468);
    memset(&idle, 0, sizeof idle);
    gm_runtime_tick(&runtime, idle);
    gm_mobs_living_cold_flush(&runtime.mobs);
    if (runtime.mobs.living_cold_count != 3
            || gm_mobs_living_count(&runtime.mobs) != GM_MOB_CAPACITY + 3
            || runtime.mobs.living_cold[0].sheep_mate_eid != 12001
            || runtime.mobs.living_cold[2].store_id != 12002
            || runtime.mobs.living_cold[2].growing_age != -23999) {
        fprintf(stderr,
                "FAIL cold mating stable identity/birth: cold=%d living=%d "
                "mate=%d delay=%d active=%d love=%d/%d child=%d age=%d "
                "next=%d/%d tick=%lld\n",
                runtime.mobs.living_cold_count,
                gm_mobs_living_count(&runtime.mobs),
                runtime.mobs.living_cold[0].sheep_mate_eid,
                runtime.mobs.living_cold[0].sheep_mate_delay,
                runtime.mobs.living_cold[0].sheep_mate_active,
                runtime.mobs.living_cold[0].sheep_in_love,
                runtime.mobs.living_cold[1].sheep_in_love,
                runtime.mobs.living_cold_count > 2
                    ? runtime.mobs.living_cold[2].store_id : -1,
                runtime.mobs.living_cold_count > 2
                    ? runtime.mobs.living_cold[2].growing_age : 0,
                runtime.next_entity_id, runtime.mobs.next_id,
                runtime.mobs.tick);
        gm_runtime_destroy(&runtime);
        return 1;
    }
    gm_runtime_destroy(&runtime);

    if (!gm_runtime_init(&runtime, &config, error, sizeof error)) {
        fprintf(stderr, "FAIL cold cloud setup: %s\n", error);
        return 1;
    }
    for (int index = 0; index < 110; ++index)
        if (gm_mobs_spawn_exact(
                &runtime.mobs, EW_TYPE_COW, 9000 + index,
                (double)(index % 10) * 0.05, 5.0,
                (double)(index / 10) * 0.05,
                0.0, 0.0, 0.0, 0.0F, 10.0F,
                1, 0, 0, 0) <= 0) {
            fprintf(stderr, "FAIL cold cloud living spawn\n");
            gm_runtime_destroy(&runtime);
            return 1;
        }
    if (!gm_runtime_spawn_area_effect_cloud_fixture(
            &runtime, 9500, TB_PT_HARMING, 0.25, 5.0, 0.25,
            9, 600, 10, 20, 3.0F, 0.0F, 0.0F, 0)) {
        fprintf(stderr, "FAIL cold cloud spawn\n");
        gm_runtime_destroy(&runtime);
        return 1;
    }
    memset(&idle, 0, sizeof idle);
    gm_runtime_tick(&runtime, idle);
    if (runtime.area_effect_cooldown_count != 110
            || gm_runtime_area_effect_cloud_deadline(
                &runtime, 9500, 9000) != 30
            || gm_runtime_area_effect_cloud_deadline(
                &runtime, 9500, 9109) != 30
            || !gm_runtime_write_checkpoint(&runtime, checkpoint)
            || !gm_runtime_load_checkpoint(&runtime, checkpoint)
            || runtime.area_effect_cooldown_count != 110
            || gm_runtime_area_effect_cloud_deadline(
                &runtime, 9500, 9109) != 30) {
        fprintf(stderr,
                "FAIL dynamic cold cloud cooldown/checkpoint: count=%d "
                "first=%d last=%d\n",
                runtime.area_effect_cooldown_count,
                gm_runtime_area_effect_cloud_deadline(
                    &runtime, 9500, 9000),
                gm_runtime_area_effect_cloud_deadline(
                    &runtime, 9500, 9109));
        (void)remove(checkpoint);
        gm_runtime_destroy(&runtime);
        return 1;
    }
    (void)remove(checkpoint);
    gm_runtime_destroy(&runtime);

    if (!gm_runtime_init(&runtime, &config, error, sizeof error)) {
        fprintf(stderr, "FAIL cold mount setup: %s\n", error);
        return 1;
    }
    for (int index = 0; index < GM_MOB_CAPACITY; ++index)
        if (gm_mobs_spawn_exact(
                &runtime.mobs, EW_TYPE_COW, 10000 + index,
                100.0 + index * 3.0, 5.0, 100.0,
                0.0, 0.0, 0.0, 0.0F, 10.0F,
                1, 0, 0, 0) <= 0) {
            fprintf(stderr, "FAIL cold mount hot fill\n");
            gm_runtime_destroy(&runtime);
            return 1;
        }
    if (gm_mobs_spawn_exact(
            &runtime.mobs, EW_TYPE_PIG, 11000,
            0.0, 5.0, 0.0, 0.0, 0.0, 0.0,
            0.0F, 10.0F, 1, 0, 0, 0) <= 0
            || !gm_mobs_set_pig_saddled(&runtime.mobs, 11000, 1)
            || !gm_mobs_pig_mount(&runtime.mobs, 11000)) {
        fprintf(stderr, "FAIL cold pig mount\n");
        gm_runtime_destroy(&runtime);
        return 1;
    }
    (void)gm_mobs_find_slot_by_eid(&runtime.mobs, 10094);
    gm_mobs_living_cold_flush(&runtime.mobs);
    {
        int eid = 0;
        if (!gm_mobs_pig_riding(&runtime.mobs, &eid) || eid != 11000) {
            fprintf(stderr, "FAIL cold pig mount identity\n");
            gm_runtime_destroy(&runtime);
            return 1;
        }
    }
    gm_mobs_pig_dismount(&runtime.mobs);
    if (gm_mobs_spawn_exact(
            &runtime.mobs, EW_TYPE_HORSE, 11001,
            2.0, 5.0, 0.0, 0.0, 0.0, 0.0,
            0.0F, 10.0F, 1, 0, 0, 0) <= 0
            || !gm_mobs_horse_mount(&runtime.mobs, 11001)) {
        fprintf(stderr, "FAIL cold horse mount\n");
        gm_runtime_destroy(&runtime);
        return 1;
    }
    (void)gm_mobs_find_slot_by_eid(&runtime.mobs, 11000);
    gm_mobs_living_cold_flush(&runtime.mobs);
    {
        int eid = 0;
        if (!gm_mobs_horse_riding(&runtime.mobs, &eid) || eid != 11001) {
            fprintf(stderr, "FAIL cold horse mount identity\n");
            gm_runtime_destroy(&runtime);
            return 1;
        }
    }
    gm_mobs_horse_dismount(&runtime.mobs);
    if (gm_mobs_spawn_boat_exact(
            &runtime.mobs, 11002, 4.0, 5.0, 0.0, 0.0F) <= 0) {
        fprintf(stderr, "FAIL cold boat spawn\n");
        gm_runtime_destroy(&runtime);
        return 1;
    }
    runtime.player.ent.posX = 4.0 - runtime.ox;
    runtime.player.ent.posY = 5.0;
    runtime.player.ent.posZ = 0.0 - runtime.oz;
    if (!gm_mobs_boat_mount(
            &runtime.mobs, (struct PsvPlayer *)&runtime.player,
            runtime.ox, runtime.oz)) {
        fprintf(stderr, "FAIL cold boat mount\n");
        gm_runtime_destroy(&runtime);
        return 1;
    }
    (void)gm_mobs_find_slot_by_eid(&runtime.mobs, 11000);
    gm_mobs_living_cold_flush(&runtime.mobs);
    if (!gm_mobs_boat_riding(&runtime.mobs)
            || runtime.mobs.boat_ride_eid != 11002
            || !gm_runtime_write_checkpoint(&runtime, checkpoint)
            || !gm_runtime_load_checkpoint(&runtime, checkpoint)
            || !gm_mobs_boat_riding(&runtime.mobs)
            || runtime.mobs.boat_ride_eid != 11002) {
        fprintf(stderr, "FAIL cold boat mount identity/checkpoint\n");
        (void)remove(checkpoint);
        gm_runtime_destroy(&runtime);
        return 1;
    }
    (void)remove(checkpoint);
    gm_runtime_destroy(&runtime);
    fprintf(stderr,
            "living cold slot: PASS (%zu bytes, all generated fields; "
            "110 ordered entities spawn/tick/render/cloud/checkpoint; "
            "cold mating/caravan/mount identity)\n",
            sizeof cold);
    return 0;
}
