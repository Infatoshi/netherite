#include "game/runtime.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t double_bits(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static uint32_t float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static void print_double_bits3(double x, double y, double z) {
    printf("[\"%016" PRIx64 "\",\"%016" PRIx64
           "\",\"%016" PRIx64 "\"]",
           double_bits(x), double_bits(y), double_bits(z));
}

int main(int argc, char **argv) {
    GmConfig config;
    GmRuntime runtime;
    const EwStore *store;
    char error[256];
    uint64_t world_seed, entity_seed, uuid_seed, math_seed;
    int fixture, jockey_fixture, next_id, mob_spawning;
    int continuation_ticks = -1;
    int checkpoint_tick = -1;
    const char *checkpoint_path = NULL;
    int x = 768, y = 220, z = 768;
    int slot;
    if (argc != 9 && argc != 10 && argc != 12) return 2;
    fixture = atoi(argv[1]);
    jockey_fixture = atoi(argv[2]);
    world_seed = strtoull(argv[3], NULL, 10);
    entity_seed = strtoull(argv[4], NULL, 10);
    uuid_seed = strtoull(argv[5], NULL, 10);
    math_seed = strtoull(argv[6], NULL, 10);
    next_id = atoi(argv[7]);
    mob_spawning = atoi(argv[8]);
    if (argc == 10) continuation_ticks = atoi(argv[9]);
    if (argc == 12) {
        continuation_ticks = atoi(argv[9]);
        checkpoint_tick = atoi(argv[10]);
        checkpoint_path = argv[11];
    }
    if (continuation_ticks > 0) x = z = 16;
    if (fixture < 0 || fixture > 31
            || jockey_fixture < 0 || jockey_fixture > 2
            || world_seed >= (UINT64_C(1) << 48)
            || entity_seed >= (UINT64_C(1) << 48)
            || uuid_seed >= (UINT64_C(1) << 48)
            || math_seed >= (UINT64_C(1) << 48)
            || next_id <= 0 || next_id == INT32_MAX
            || continuation_ticks > 20
            || (checkpoint_tick >= 0
                && (checkpoint_tick < 1
                    || checkpoint_tick > continuation_ticks
                    || !checkpoint_path || !*checkpoint_path))
            || (mob_spawning != 0 && mob_spawning != 1))
        return 2;
    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.render = GM_RENDER_OFF;
    config.weather = 0;
    config.mobs = 0;
    if (!gm_runtime_init(&runtime, &config, error, sizeof error)) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    if (continuation_ticks > 0)
        gm_runtime_set_pose(&runtime, 0.5D, 220.0D, 0.5D, 0.0F, 0.0F);
    for (int dy = -3; dy <= 1; ++dy)
        gm_world_set_block_meta(runtime.world, x, y + dy, z, 0, 0);
    gm_world_set_block_meta(
        runtime.world, x, y + (fixture == 1 ? -3 : -1), z, 1, 0);
    gm_world_set_block_meta(runtime.world, x, y, z, 90, 1);
    if (continuation_ticks > 0)
        gm_world_set_block_meta(runtime.world, x, y + 4, z, 1, 0);
    if (jockey_fixture == 1) {
        runtime.mobs.active_dimension = runtime.dimension;
        int existing = gm_mobs_spawn_natural_eid(
            &runtime.mobs, EW_TYPE_CHICKEN, next_id - 1,
            x + 0.5D, y + 0.1D, z + 0.5D);
        if (existing < 0
                || runtime.loaded_entity_order_count
                    >= GM_RUNTIME_LOADED_ENTITY_ORDER) {
            gm_runtime_destroy(&runtime);
            return 1;
        }
        runtime.loaded_entity_order[
            runtime.loaded_entity_order_count++] = next_id - 1;
    }
    runtime.gamerules.doMobSpawning = mob_spawning;
    if (!gm_runtime_set_world_random_seed48(&runtime, world_seed)
            || !gm_runtime_set_entity_seed_generator_seed48(
                &runtime, entity_seed)
            || !gm_runtime_set_server_uuid_random_seed48(
                &runtime, uuid_seed)
            || !gm_runtime_set_math_random_seed48(&runtime, math_seed)
            || !gm_runtime_set_entity_id_cursor(&runtime, next_id)
            || !gm_runtime_random_tick_block(&runtime, x, y, z, 90)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    for (int tick = 0; tick < continuation_ticks; ++tick) {
        runtime.mobs.active_entity_seed_generator_seed48 =
            &runtime.entity_seed_generator_seed48;
        runtime.mobs.active_server_uuid_random_seed48 =
            &runtime.server_uuid_random_seed48;
        gm_mobs_tick(
            &runtime.mobs, runtime.world, NULL,
            (const struct McSinTable *)&runtime.sin_table,
            (struct PsvPlayer *)&runtime.player,
            (struct PvStats *)&runtime.vitals,
            runtime.ox, runtime.oz, runtime.dimension,
            runtime.clock.world_time, &runtime.clock,
            runtime.mob_griefing, &runtime.world_random_seed48,
            &runtime.math_random_seed48, &runtime.next_entity_id,
            runtime.do_mob_loot, &runtime.entities, 0.0F, 0.0F);
        runtime.mobs.active_entity_seed_generator_seed48 = NULL;
        runtime.mobs.active_server_uuid_random_seed48 = NULL;
        if (tick + 1 == checkpoint_tick
                && (!gm_runtime_write_checkpoint(
                        &runtime, checkpoint_path)
                    || !gm_runtime_load_checkpoint(
                        &runtime, checkpoint_path))) {
            (void)remove(checkpoint_path);
            gm_runtime_destroy(&runtime);
            return 1;
        }
    }
    if (checkpoint_path) (void)remove(checkpoint_path);
    store = runtime.mobs.current ? &runtime.mobs.b : &runtime.mobs.a;
    slot = gm_mobs_find_slot_by_eid(&runtime.mobs, next_id);
    printf("{\"ok\":true,\"case\":%d,\"difficulty\":2,"
           "\"local_difficulty_bits\":\"00000000\","
           "\"world_seed48\":%" PRIu64
           ",\"math_seed48\":%" PRIu64
           ",\"entity_seed48\":%" PRIu64
           ",\"server_uuid_seed48\":%" PRIu64
           ",\"next_entity_id\":%d,\"spawned\":%s",
           fixture, runtime.world_random_seed48,
           runtime.math_random_seed48,
           runtime.entity_seed_generator_seed48,
           runtime.server_uuid_random_seed48,
           runtime.next_entity_id, slot > 0 ? "true" : "false");
    if (slot > 0) {
        ICStack held = runtime.mobs.entity_mainhand[slot];
        printf(",\"eid\":%d,\"position_bits\":", store->id[slot]);
        print_double_bits3(
            store->x[slot] - (double)x,
            store->y[slot] - (double)y,
            store->z[slot] - (double)z);
        if (continuation_ticks >= 0) {
            printf(",\"continuation_ticks\":%d,\"motion_bits\":",
                   continuation_ticks);
            print_double_bits3(
                store->vx[slot], store->vy[slot], store->vz[slot]);
            printf(",\"ticks_existed\":%d,\"on_ground\":%s"
                   ",\"fire_ticks\":%d"
                   ",\"fall_distance_bits\":\"%08" PRIx32 "\"",
                   runtime.mobs.entity_ticks_existed[slot],
                   store->on_ground[slot] ? "true" : "false",
                   runtime.mobs.fire_ticks[slot],
                   float_bits(runtime.mobs.entity_fall_distance[slot]));
        }
        printf(",\"rotation_bits\":[\"%08" PRIx32
               "\",\"%08" PRIx32 "\",\"%08" PRIx32
               "\",\"%08" PRIx32 "\"]"
               ",\"health_bits\":\"%08" PRIx32 "\""
               ",\"child\":%s,\"left_handed\":%s"
               ",\"can_pick_up_loot\":%s"
               ",\"held_item\":%d,\"held_count\":%d"
               ",\"held_meta\":%d,\"portal_cooldown\":%d"
               ",\"seed48\":%" PRIu64
               ",\"have_gaussian\":%s"
               ",\"next_gaussian_bits\":\"%016" PRIx64 "\""
               ",\"uuid_most\":%" PRId64
               ",\"uuid_least\":%" PRId64
               ",\"follow_range_bits\":\"%016" PRIx64 "\""
               ",\"attack_damage_bits\":\"%016" PRIx64 "\""
               ",\"riding\":%s,\"vehicle_eid\":%d"
               ",\"chicken_jockey\":%s,\"chicken_new\":%s",
               float_bits(store->yaw[slot]),
               float_bits(runtime.mobs.entity_pitch[slot]),
               float_bits(runtime.mobs.passive_head_yaw[slot]),
               float_bits(runtime.mobs.squid_render_yaw_offset[slot]),
               float_bits(store->health[slot]),
               runtime.mobs.growing_age[slot] < 0 ? "true" : "false",
               runtime.mobs.entity_left_handed[slot] ? "true" : "false",
               runtime.mobs.entity_can_pick_up_loot[slot]
                    ? "true" : "false",
               held.item, held.count, held.meta,
               runtime.mobs.entity_portal_cooldown[slot],
               runtime.mobs.entity_random[slot].random.seed,
               runtime.mobs.entity_random[slot].have_next_next_gaussian
                    ? "true" : "false",
               double_bits(
                    runtime.mobs.entity_random[slot].next_next_gaussian),
               runtime.mobs.entity_uuid_most[slot],
               runtime.mobs.entity_uuid_least[slot],
               double_bits(35.0D + 35.0D
                    * runtime.mobs.entity_random_follow_range_bonus[slot]),
               double_bits(5.0D
                    + (runtime.mobs.entity_ticks_existed[slot] > 0
                        && held.item == 283 && held.count > 0
                        ? 3.0D : 0.0D)),
               runtime.mobs.entity_vehicle_eid[slot] >= 0
                    ? "true" : "false",
               runtime.mobs.entity_vehicle_eid[slot],
               runtime.mobs.entity_vehicle_eid[slot] >= 0
                    ? "true" : "false",
               runtime.mobs.entity_vehicle_eid[slot] >= next_id
                    ? "true" : "false");
        int chicken = gm_mobs_find_slot_by_eid(
            &runtime.mobs, runtime.mobs.entity_vehicle_eid[slot]);
        if (chicken > 0 && store->id[chicken] >= next_id) {
            printf(",\"chicken_eid\":%d,\"chicken_position_bits\":",
                   store->id[chicken]);
            print_double_bits3(
                store->x[chicken] - (double)x,
                store->y[chicken] - (double)y,
                store->z[chicken] - (double)z);
            if (continuation_ticks >= 0) {
                printf(",\"chicken_motion_bits\":");
                print_double_bits3(
                    store->vx[chicken], store->vy[chicken],
                    store->vz[chicken]);
                printf(",\"chicken_ticks_existed\":%d"
                       ",\"chicken_on_ground\":%s"
                       ",\"chicken_fire_ticks\":%d"
                       ",\"chicken_fall_distance_bits\":\"%08"
                            PRIx32 "\""
                       ",\"chicken_wing_rotation_bits\":\"%08"
                            PRIx32 "\""
                       ",\"chicken_dest_pos_bits\":\"%08"
                            PRIx32 "\"",
                       runtime.mobs.entity_ticks_existed[chicken],
                       store->on_ground[chicken] ? "true" : "false",
                       runtime.mobs.fire_ticks[chicken],
                       float_bits(
                           runtime.mobs.entity_fall_distance[chicken]),
                       float_bits(
                           runtime.mobs.chicken_wing_rotation[chicken]),
                       float_bits(runtime.mobs.chicken_dest_pos[chicken]));
            }
            printf(",\"chicken_rotation_bits\":[\"%08" PRIx32
                   "\",\"%08" PRIx32 "\",\"%08" PRIx32
                   "\",\"%08" PRIx32 "\"]"
                   ",\"chicken_health_bits\":\"%08" PRIx32 "\""
                   ",\"chicken_child\":%s"
                   ",\"chicken_left_handed\":%s"
                   ",\"chicken_persistent\":%s"
                   ",\"chicken_egg_time\":%d"
                   ",\"chicken_seed48\":%" PRIu64
                   ",\"chicken_have_gaussian\":%s"
                   ",\"chicken_next_gaussian_bits\":\"%016"
                        PRIx64 "\""
                   ",\"chicken_uuid_most\":%" PRId64
                   ",\"chicken_uuid_least\":%" PRId64
                   ",\"chicken_follow_range_bits\":\"%016"
                        PRIx64 "\"",
                   float_bits(store->yaw[chicken]),
                   float_bits(runtime.mobs.entity_pitch[chicken]),
                   float_bits(runtime.mobs.passive_head_yaw[chicken]),
                   float_bits(runtime.mobs.squid_render_yaw_offset[chicken]),
                   float_bits(store->health[chicken]),
                   runtime.mobs.growing_age[chicken] < 0 ? "true" : "false",
                   runtime.mobs.entity_left_handed[chicken]
                        ? "true" : "false",
                   runtime.mobs.persistence_required[chicken]
                        ? "true" : "false",
                   runtime.mobs.chicken_time_until_next_egg[chicken],
                   runtime.mobs.entity_random[chicken].random.seed,
                   runtime.mobs.entity_random[chicken]
                        .have_next_next_gaussian ? "true" : "false",
                   double_bits(runtime.mobs.entity_random[chicken]
                        .next_next_gaussian),
                   runtime.mobs.entity_uuid_most[chicken],
                   runtime.mobs.entity_uuid_least[chicken],
                   double_bits(16.0D + 16.0D
                        * runtime.mobs
                            .entity_random_follow_range_bonus[chicken]));
        }
        printf(",\"spawn_order\":[");
        int emitted = 0;
        for (int order = 0;
                order < runtime.loaded_entity_order_count; ++order) {
            int ordered_eid = runtime.loaded_entity_order[order];
            if (ordered_eid < next_id
                    || ordered_eid >= runtime.next_entity_id)
                continue;
            if (emitted++) putchar(',');
            printf("%d", ordered_eid);
        }
        putchar(']');
    }
    puts("}");
    gm_runtime_destroy(&runtime);
    return 0;
}
