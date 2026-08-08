#include "game/runtime.h"
#include "game/portal_live.h"

#include <inttypes.h>
#include <math.h>
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

int main(int argc, char **argv) {
    GmConfig config;
    GmRuntime runtime;
    GmRuntimeProjectile *p = NULL;
    char error[256];
    uint64_t old_seed, clone_seed, uuid_seed;
    double x, y, z, vx, vy, vz;
    float yaw, pitch;
    int type, old_eid, next_id, age, ticks_in_air;
    int portal_x, portal_y, portal_z, continuation_ticks;
    int destination_loaded, portal_meta, teleport_direction;
    int obstruction_side, slot = -1;
    double last_portal_vec_x, last_portal_vec_y;
    int64_t uuid_most, uuid_least;
    if (argc != 29) return 2;
    type = atoi(argv[1]);
    old_eid = atoi(argv[2]);
    next_id = atoi(argv[3]);
    old_seed = strtoull(argv[4], NULL, 10);
    clone_seed = strtoull(argv[5], NULL, 10);
    uuid_seed = strtoull(argv[6], NULL, 10);
    uuid_most = strtoll(argv[7], NULL, 10);
    uuid_least = strtoll(argv[8], NULL, 10);
    x = strtod(argv[9], NULL);
    y = strtod(argv[10], NULL);
    z = strtod(argv[11], NULL);
    vx = strtod(argv[12], NULL);
    vy = strtod(argv[13], NULL);
    vz = strtod(argv[14], NULL);
    yaw = strtof(argv[15], NULL);
    pitch = strtof(argv[16], NULL);
    age = atoi(argv[17]);
    ticks_in_air = atoi(argv[18]);
    portal_x = atoi(argv[19]);
    portal_y = atoi(argv[20]);
    portal_z = atoi(argv[21]);
    continuation_ticks = atoi(argv[22]);
    destination_loaded = atoi(argv[23]);
    portal_meta = atoi(argv[24]);
    last_portal_vec_x = strtod(argv[25], NULL);
    last_portal_vec_y = strtod(argv[26], NULL);
    teleport_direction = atoi(argv[27]);
    obstruction_side = atoi(argv[28]);
    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.seed = 708;
    if (!gm_runtime_init(&runtime, &config, error, sizeof error)) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    if (!gm_runtime_set_dimension(&runtime, -1)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    for (int pa = -1; pa <= 2; ++pa)
        for (int py = portal_y - 1; py <= portal_y + 3; ++py) {
            int frame = pa == -1 || pa == 2
                || py == portal_y - 1 || py == portal_y + 3;
            gm_world_set_block_meta(
                runtime.world,
                portal_meta == 2 ? portal_x : portal_x + pa,
                py,
                portal_meta == 2 ? portal_z + pa : portal_z,
                frame ? 49 : 90, frame ? 0 : portal_meta);
        }
    if (obstruction_side) {
        int normal_offset = obstruction_side > 0 ? 1 : -1;
        gm_world_set_block(
            runtime.world,
            portal_meta == 2 ? portal_x + normal_offset : portal_x,
            portal_y,
            portal_meta == 2 ? portal_z : portal_z + normal_offset,
            1);
    }
    /* Java's entity update gate requires every chunk in the 32-block square.
     * An empty authoritative set keeps the cached destination dormant. */
    if (!gm_runtime_loaded_chunks_begin(
            &runtime, destination_loaded ? 25 : 0)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    if (destination_loaded) {
        int order = 0;
        double landing_x = (double)portal_x;
        double landing_y = (double)portal_y;
        double landing_z = (double)portal_z;
        double landing_vx = vx, landing_vz = vz;
        float landing_yaw = yaw;
        if (!gm_portal_place_existing(
                runtime.world, portal_x, portal_y, portal_z,
                last_portal_vec_x, last_portal_vec_y,
                teleport_direction,
                &landing_x, &landing_y, &landing_z,
                &landing_vx, &landing_vz, &landing_yaw)) {
            gm_runtime_destroy(&runtime);
            return 1;
        }
        int landing_block_x = (int)floor(landing_x);
        int landing_block_z = (int)floor(landing_z);
        for (int chunk_x = (landing_block_x - 32) >> 4;
                chunk_x <= ((landing_block_x + 32) >> 4); ++chunk_x)
            for (int chunk_z = (landing_block_z - 32) >> 4;
                    chunk_z <= ((landing_block_z + 32) >> 4); ++chunk_z)
                if (!gm_runtime_loaded_chunk_set(
                        &runtime, order++, chunk_x, chunk_z)) {
                    gm_runtime_destroy(&runtime);
                    return 1;
                }
    }
    if (!gm_runtime_loaded_chunks_finalize(&runtime)
            || !gm_runtime_set_dimension(&runtime, 0)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    if (type == EW_TYPE_BOAT) {
        GmAction action;
        EwStore *state;
        int boat = gm_mobs_spawn_boat_type_exact(
            &runtime.mobs, old_eid, x, y, z, yaw, 4);
        if (boat <= 0
                || !gm_mobs_set_entity_uuid(
                    &runtime.mobs, old_eid, uuid_most, uuid_least)
                || !gm_mobs_set_entity_random_state(
                    &runtime.mobs, old_eid, old_seed, 0, 0.0)) {
            gm_runtime_destroy(&runtime);
            return 1;
        }
        state = runtime.mobs.current
            ? &runtime.mobs.b : &runtime.mobs.a;
        state->vx[boat] = vx; state->vy[boat] = vy; state->vz[boat] = vz;
        runtime.mobs.entity_pitch[boat] = pitch;
        runtime.mobs.entity_ticks_existed[boat] = age;
        runtime.mobs.boat_in_portal[boat] = 1;
        runtime.mobs.boat_portal_counter[boat] = 1;
        runtime.mobs.boat_last_portal_valid[boat] = 1;
        runtime.mobs.boat_last_portal_x[boat] = (int)x;
        runtime.mobs.boat_last_portal_y[boat] = (int)y;
        runtime.mobs.boat_last_portal_z[boat] = (int)z;
        runtime.mobs.boat_last_portal_vec_x[boat] = last_portal_vec_x;
        runtime.mobs.boat_last_portal_vec_y[boat] = last_portal_vec_y;
        runtime.mobs.boat_teleport_direction[boat] =
            (signed char)teleport_direction;
        ew_store_copy(&runtime.mobs.b, &runtime.mobs.a);
        gm_runtime_set_entity_seed_generator_seed48(&runtime, clone_seed);
        gm_runtime_set_server_uuid_random_seed48(&runtime, uuid_seed);
        gm_runtime_set_entity_id_cursor(&runtime, next_id);
        runtime.restored_active_mobs_enabled = 1;
        memset(&action, 0, sizeof action);
        action.hotbar_sel = -1;
        gm_runtime_tick(&runtime, action);
        state = runtime.mobs.current
            ? &runtime.mobs.b : &runtime.mobs.a;
        if (runtime.mobs.entity_dimension[boat] != -1) {
            gm_runtime_destroy(&runtime);
            return 1;
        }
        printf("{\"ok\":true,\"eid\":%d,\"dimension\":%d,"
               "\"position_bits\":[\"%016" PRIx64 "\","
               "\"%016" PRIx64 "\",\"%016" PRIx64 "\"],"
               "\"motion_bits\":[\"%016" PRIx64 "\","
               "\"%016" PRIx64 "\",\"%016" PRIx64 "\"],"
               "\"rotation_bits\":[\"%08" PRIx32 "\",\"00000000\","
               "\"%08" PRIx32 "\",\"00000000\"],"
               "\"ticks_existed\":%d,\"ticks_in_air\":0,"
               "\"portal_counter\":%d,\"portal_cooldown\":%d,"
               "\"in_portal\":%s,\"throwable_shake\":0,"
               "\"in_ground\":false,\"ticks_in_ground\":0,"
               "\"ignore_present\":false,\"ignore_time\":0,"
               "\"owner_pending\":false,"
               "\"pearl_private_thrower\":false,"
               "\"last_portal_vec_bits\":[\"%016" PRIx64 "\","
               "\"%016" PRIx64 "\"],\"teleport_direction\":%d,"
               "\"seed48\":%" PRIu64 ",\"have_gaussian\":false,"
               "\"next_gaussian_bits\":\"0000000000000000\","
               "\"uuid_most\":%" PRId64 ",\"uuid_least\":%" PRId64 ","
               "\"entity_seed48\":%" PRIu64 ","
               "\"server_uuid_seed48\":%" PRIu64 ","
               "\"next_entity_id\":%d,\"boat_variant\":%d,"
               "\"boat_damage_bits\":\"%08" PRIx32 "\","
               "\"boat_time_since_hit\":%d,"
               "\"boat_forward_direction\":%d}\n",
            state->id[boat], runtime.mobs.entity_dimension[boat],
            double_bits(state->x[boat]), double_bits(state->y[boat]),
            double_bits(state->z[boat]), double_bits(state->vx[boat]),
            double_bits(state->vy[boat]), double_bits(state->vz[boat]),
            float_bits(state->yaw[boat]), float_bits(state->yaw[boat]),
            runtime.mobs.entity_ticks_existed[boat],
            runtime.mobs.boat_portal_counter[boat],
            runtime.mobs.entity_portal_cooldown[boat],
            runtime.mobs.boat_in_portal[boat] ? "true" : "false",
            double_bits(last_portal_vec_x), double_bits(last_portal_vec_y),
            teleport_direction,
            runtime.mobs.entity_random[boat].random.seed,
            (int64_t)runtime.mobs.entity_uuid_most[boat],
            (int64_t)runtime.mobs.entity_uuid_least[boat],
            runtime.entity_seed_generator_seed48,
            runtime.server_uuid_random_seed48, runtime.next_entity_id,
            runtime.mobs.boat_variant[boat],
            float_bits(runtime.mobs.boat_damage[boat]),
            0, 1);
        gm_runtime_destroy(&runtime);
        return continuation_ticks == 0 ? 0 : 1;
    }
    if (!gm_runtime_spawn_throwable_state_fixture(
                &runtime, old_eid, type, 438, 0,
                x, y, z, vx, vy, vz,
                yaw, pitch, yaw, pitch,
                age, ticks_in_air, 1, 0, 0, 0, type == 12,
                0, 0, 0, -1, -1, -1, 0,
                1, 1, 0,
                1, (int)x, (int)y, (int)z,
                last_portal_vec_x, last_portal_vec_y,
                teleport_direction,
                0, 0, old_seed, 0, 0.0)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i)
        if (runtime.projectiles[i].active
                && runtime.projectiles[i].eid == old_eid) {
            p = &runtime.projectiles[i];
            slot = i;
            break;
        }
    if (!p) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    p->uuid_present = 1;
    p->uuid_most = uuid_most;
    p->uuid_least = uuid_least;
    gm_runtime_set_entity_seed_generator_seed48(&runtime, clone_seed);
    gm_runtime_set_server_uuid_random_seed48(&runtime, uuid_seed);
    gm_runtime_set_entity_id_cursor(&runtime, next_id);
    if (!gm_runtime_tick_projectile_now(&runtime, slot)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    for (int tick = 0; tick < continuation_ticks && p->active; ++tick)
        if (!gm_runtime_tick_projectile_now(&runtime, slot)) {
            gm_runtime_destroy(&runtime);
            return 1;
        }
    printf("{\"ok\":true,\"eid\":%d,\"dimension\":%d,"
           "\"position_bits\":[\"%016" PRIx64 "\","
           "\"%016" PRIx64 "\",\"%016" PRIx64 "\"],"
           "\"motion_bits\":[\"%016" PRIx64 "\","
           "\"%016" PRIx64 "\",\"%016" PRIx64 "\"],"
           "\"rotation_bits\":[\"%08" PRIx32 "\","
           "\"%08" PRIx32 "\",\"%08" PRIx32 "\","
           "\"%08" PRIx32 "\"],\"ticks_existed\":%d,"
           "\"ticks_in_air\":%d,\"portal_counter\":%d,"
           "\"portal_cooldown\":%d,\"in_portal\":%s,"
           "\"throwable_shake\":%d,\"in_ground\":%s,"
           "\"ticks_in_ground\":%d,\"ignore_present\":%s,"
           "\"ignore_time\":%d,\"owner_pending\":%s,"
           "\"pearl_private_thrower\":%s,\"last_portal_vec_bits\":["
           "\"%016" PRIx64 "\",\"%016" PRIx64 "\"],"
           "\"teleport_direction\":%d,\"seed48\":%" PRIu64 ","
           "\"have_gaussian\":%s,\"next_gaussian_bits\":"
           "\"%016" PRIx64 "\",\"uuid_most\":%" PRId64 ","
           "\"uuid_least\":%" PRId64 ",\"entity_seed48\":%" PRIu64 ","
           "\"server_uuid_seed48\":%" PRIu64 ","
           "\"next_entity_id\":%d}\n",
           p->eid, p->dimension,
           double_bits(p->x), double_bits(p->y), double_bits(p->z),
           double_bits(p->vx), double_bits(p->vy), double_bits(p->vz),
           float_bits(p->yaw), float_bits(p->pitch),
           float_bits(p->prev_yaw), float_bits(p->prev_pitch),
           p->age, p->throwable_ticks_in_air,
           p->portal_counter, p->portal_cooldown,
           p->in_portal ? "true" : "false",
           p->throwable_shake,
           p->throwable_in_ground ? "true" : "false",
           p->throwable_ticks_in_ground,
           p->ignore_player ? "true" : "false", p->ignore_player_time,
           p->thrower_player_pending ? "true" : "false",
           p->pearl_private_thrower ? "true" : "false",
           double_bits(p->last_portal_vec_x),
           double_bits(p->last_portal_vec_y), p->teleport_direction,
           p->random_seed48,
           p->random_have_gaussian ? "true" : "false",
           double_bits(p->random_next_gaussian),
           (int64_t)p->uuid_most, (int64_t)p->uuid_least,
           runtime.entity_seed_generator_seed48,
           runtime.server_uuid_random_seed48, runtime.next_entity_id);
    gm_runtime_destroy(&runtime);
    return 0;
}
