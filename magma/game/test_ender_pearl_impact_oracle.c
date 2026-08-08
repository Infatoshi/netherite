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

static void print_double_bits6(
        double x, double y, double z, double vx, double vy, double vz) {
    printf("[\"%016" PRIx64 "\",\"%016" PRIx64
           "\",\"%016" PRIx64 "\",\"%016" PRIx64
           "\",\"%016" PRIx64 "\",\"%016" PRIx64 "\"]",
           double_bits(x), double_bits(y), double_bits(z),
           double_bits(vx), double_bits(vy), double_bits(vz));
}

int main(int argc, char **argv) {
    GmConfig config;
    GmRuntime runtime;
    GmRuntimeProjectile *pearl;
    const EwStore *store;
    char error[256];
    uint64_t pearl_seed, player_seed, math_seed, entity_seed, uuid_seed;
    double x, y, z;
    float health, last_damage;
    int next_id, do_mob_spawning, hurt_resistant;
    if (argc != 14) return 2;
    pearl_seed = strtoull(argv[1], NULL, 10);
    player_seed = strtoull(argv[2], NULL, 10);
    math_seed = strtoull(argv[3], NULL, 10);
    entity_seed = strtoull(argv[4], NULL, 10);
    uuid_seed = strtoull(argv[5], NULL, 10);
    next_id = atoi(argv[6]);
    do_mob_spawning = atoi(argv[7]);
    health = strtof(argv[8], NULL);
    hurt_resistant = atoi(argv[9]);
    last_damage = strtof(argv[10], NULL);
    x = strtod(argv[11], NULL);
    y = strtod(argv[12], NULL);
    z = strtod(argv[13], NULL);
    if (pearl_seed >= (UINT64_C(1) << 48)
            || player_seed >= (UINT64_C(1) << 48)
            || math_seed >= (UINT64_C(1) << 48)
            || entity_seed >= (UINT64_C(1) << 48)
            || uuid_seed >= (UINT64_C(1) << 48)
            || next_id <= 0 || next_id > INT32_MAX - 2
            || (do_mob_spawning != 0 && do_mob_spawning != 1)
            || health <= 5.0F || health > 20.0F
            || hurt_resistant < 0 || hurt_resistant > 20
            || last_damage < 0.0F)
        return 2;

    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.seed = 0;
    config.mobs = 0;
    config.weather = 0;
    if (!gm_runtime_init(&runtime, &config, error, sizeof error)) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    isr_init(&runtime.player.inv);
    gm_runtime_set_pose_state(
        &runtime, 24.5, 220.0, 24.5, 37.5F, -15.0F,
        0.25, -0.5, 0.75, 1, 7.0F);
    runtime.vitals.health = health;
    runtime.player.health = health;
    runtime.server_player.health = health;
    runtime.vitals.exhaustion = 0.0F;
    runtime.mobs.player_hurt_time = 0;
    runtime.mobs.player_max_hurt_time = 0;
    runtime.mobs.player_hurt_resistant = hurt_resistant;
    runtime.mobs.player_last_damage = last_damage;
    runtime.mobs.player_entity_age = 42;
    runtime.mobs.player_limb_swing_amount = 0.0F;
    runtime.mobs.player_attacked_yaw = 0.0F;
    runtime.mobs.player_velocity_changed = 0;
    runtime.gamerules.doMobSpawning = do_mob_spawning;
    gm_runtime_set_player_random_seed48(&runtime, player_seed);
    gm_runtime_set_math_random_seed48(&runtime, math_seed);
    gm_runtime_set_entity_seed_generator_seed48(&runtime, entity_seed);
    gm_runtime_set_server_uuid_random_seed48(&runtime, uuid_seed);
    gm_runtime_set_entity_id_cursor(&runtime, next_id);

    pearl = &runtime.projectiles[0];
    memset(pearl, 0, sizeof *pearl);
    pearl->active = 1;
    pearl->type = 12;
    pearl->dimension = runtime.dimension;
    pearl->eid = next_id - 1;
    pearl->player_thrower = 1;
    pearl->x = x;
    pearl->y = y;
    pearl->z = z;
    pearl->yaw = 12.5F;
    pearl->pitch = 3.0F;
    pearl->random_seed48 = pearl_seed;
    if (!gm_runtime_ender_pearl_impact_now(&runtime, 0)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }

    printf("{\"ok\":true,\"pearl_dead\":%s"
           ",\"pearl_seed48\":%" PRIu64
           ",\"pearl_have_gaussian\":%s"
           ",\"pearl_gaussian_bits\":\"%016" PRIx64
           "\",\"particles\":[",
           pearl->active ? "false" : "true", pearl->random_seed48,
           pearl->random_have_gaussian ? "true" : "false",
           double_bits(pearl->random_next_gaussian));
    for (int i = 0; i < gm_runtime_particle_event_count(&runtime); ++i) {
        GmRuntimeParticleEvent event;
        if (!gm_runtime_particle_event_get(&runtime, i, &event)) {
            gm_runtime_destroy(&runtime);
            return 1;
        }
        printf("%s{\"seq\":%d,\"id\":%d,\"ignore_range\":false,"
               "\"parameters\":[],\"payload_bits\":",
               i ? "," : "", i, event.kind);
        print_double_bits6(
            event.x, event.y, event.z,
            event.motion_x, event.motion_y, event.motion_z);
        putchar('}');
    }
    printf("],\"endermites\":[");
    store = runtime.mobs.current ? &runtime.mobs.b : &runtime.mobs.a;
    int emitted = 0;
    for (int order = 0; order < runtime.loaded_entity_order_count; ++order) {
        int eid = runtime.loaded_entity_order[order];
        int slot = gm_mobs_find_slot_by_eid(&runtime.mobs, eid);
        if (slot <= 0 || !store->alive[slot]
                || store->type[slot] != EW_TYPE_ENDERMITE)
            continue;
        if (emitted++) putchar(',');
        printf("{\"eid\":%d,\"position_bits\":", eid);
        print_double_bits3(store->x[slot], store->y[slot], store->z[slot]);
        printf(",\"motion_bits\":");
        print_double_bits3(
            store->vx[slot], store->vy[slot], store->vz[slot]);
        printf(",\"yaw_bits\":\"%08" PRIx32
               "\",\"pitch_bits\":\"%08" PRIx32
               "\",\"health_bits\":\"%08" PRIx32
               "\",\"player_spawned\":%s,\"lifetime\":%d"
               ",\"seed48\":%" PRIu64
               ",\"have_gaussian\":%s"
               ",\"gaussian_bits\":\"%016" PRIx64
               "\",\"uuid_most\":%" PRId64
               ",\"uuid_least\":%" PRId64 "}",
               float_bits(store->yaw[slot]),
               float_bits(runtime.mobs.entity_pitch[slot]),
               float_bits(store->health[slot]),
               runtime.mobs.endermite_player_spawned[slot]
                    ? "true" : "false",
               runtime.mobs.endermite_lifetime[slot],
               runtime.mobs.entity_random[slot].random.seed,
               runtime.mobs.entity_random[slot].have_next_next_gaussian
                    ? "true" : "false",
               double_bits(
                   runtime.mobs.entity_random[slot].next_next_gaussian),
               runtime.mobs.entity_uuid_most[slot],
               runtime.mobs.entity_uuid_least[slot]);
    }
    printf("],\"loaded_order\":[");
    int loaded = 0;
    for (int order = 0; order < runtime.loaded_entity_order_count; ++order) {
        int eid = runtime.loaded_entity_order[order];
        int slot = gm_mobs_find_slot_by_eid(&runtime.mobs, eid);
        if (slot <= 0 || !store->alive[slot]
                || store->type[slot] != EW_TYPE_ENDERMITE)
            continue;
        printf("%s%d", loaded++ ? "," : "", eid);
    }
    printf("],\"player_position_bits\":");
    print_double_bits3(
        runtime.player.ent.posX + (double)runtime.ox,
        runtime.player.ent.posY,
        runtime.player.ent.posZ + (double)runtime.oz);
    printf(",\"player_motion_bits\":");
    print_double_bits3(
        runtime.player.ent.motionX, runtime.player.ent.motionY,
        runtime.player.ent.motionZ);
    printf(",\"player_health_bits\":\"%08" PRIx32
           "\",\"player_fall_distance_bits\":\"%08" PRIx32
           "\",\"player_hurt_time\":%d"
           ",\"player_max_hurt_time\":%d"
           ",\"player_hurt_resistant_time\":%d"
           ",\"player_last_damage_bits\":\"%08" PRIx32
           "\",\"player_entity_age\":%d"
           ",\"player_limb_swing_amount_bits\":\"%08" PRIx32
           "\",\"player_attacked_yaw_bits\":\"%08" PRIx32
           "\",\"player_velocity_changed\":%s"
           ",\"player_exhaustion_bits\":\"%08" PRIx32
           "\",\"player_seed48\":%" PRIu64
           ",\"math_seed48\":%" PRIu64
           ",\"entity_seed48\":%" PRIu64
           ",\"server_uuid_seed48\":%" PRIu64
           ",\"next_entity_id\":%d}\n",
           float_bits(runtime.vitals.health),
           float_bits(runtime.player.fall_distance),
           runtime.mobs.player_hurt_time,
           runtime.mobs.player_max_hurt_time,
           runtime.mobs.player_hurt_resistant,
           float_bits(runtime.mobs.player_last_damage),
           runtime.mobs.player_entity_age,
           float_bits(runtime.mobs.player_limb_swing_amount),
           float_bits(runtime.mobs.player_attacked_yaw),
           runtime.mobs.player_velocity_changed ? "true" : "false",
           float_bits(runtime.vitals.exhaustion),
           runtime.mobs.player_random.seed,
           runtime.math_random_seed48,
           runtime.entity_seed_generator_seed48,
           runtime.server_uuid_random_seed48,
           runtime.next_entity_id);
    gm_runtime_destroy(&runtime);
    return emitted == loaded ? 0 : 1;
}
