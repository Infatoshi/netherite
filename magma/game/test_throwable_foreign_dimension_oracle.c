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

int main(int argc, char **argv) {
    GmConfig config;
    GmRuntime runtime;
    GmRuntimeProjectile *p = NULL;
    char error[256];
    uint64_t seed48;
    double x, y, z, vx, vy, vz;
    float yaw, pitch, prev_yaw, prev_pitch;
    int source_dimension, player_dimension, eid, type, age, ticks_in_air;
    int slot = -1;
    if (argc != 18) return 2;
    source_dimension = atoi(argv[1]);
    player_dimension = atoi(argv[2]);
    eid = atoi(argv[3]);
    type = atoi(argv[4]);
    seed48 = strtoull(argv[5], NULL, 10);
    x = strtod(argv[6], NULL);
    y = strtod(argv[7], NULL);
    z = strtod(argv[8], NULL);
    vx = strtod(argv[9], NULL);
    vy = strtod(argv[10], NULL);
    vz = strtod(argv[11], NULL);
    yaw = strtof(argv[12], NULL);
    pitch = strtof(argv[13], NULL);
    prev_yaw = strtof(argv[14], NULL);
    prev_pitch = strtof(argv[15], NULL);
    age = atoi(argv[16]);
    ticks_in_air = atoi(argv[17]);
    if ((source_dimension != -1 && source_dimension != 0)
            || (player_dimension != -1 && player_dimension != 0)
            || source_dimension == player_dimension)
        return 2;
    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.seed = 708;
    if (!gm_runtime_init(&runtime, &config, error, sizeof error)) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    if (!gm_runtime_set_dimension(&runtime, source_dimension)
            || !gm_runtime_loaded_chunks_begin(&runtime, 25)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    {
        int order = 0;
        int min_chunk_x = (mc_floor(x) - 32) >> 4;
        int max_chunk_x = (mc_floor(x) + 32) >> 4;
        int min_chunk_z = (mc_floor(z) - 32) >> 4;
        int max_chunk_z = (mc_floor(z) + 32) >> 4;
        for (int chunk_x = min_chunk_x;
                chunk_x <= max_chunk_x; ++chunk_x)
            for (int chunk_z = min_chunk_z;
                    chunk_z <= max_chunk_z; ++chunk_z)
                if (!gm_runtime_loaded_chunk_set(
                        &runtime, order++, chunk_x, chunk_z)) {
                    gm_runtime_destroy(&runtime);
                    return 1;
                }
    }
    if (!gm_runtime_loaded_chunks_finalize(&runtime)
            || !gm_runtime_spawn_throwable_state_fixture(
                &runtime, eid, type, 438, 0,
                x, y, z, vx, vy, vz,
                yaw, pitch, prev_yaw, prev_pitch,
                age, ticks_in_air, 1, 0, 0, 0, type == 12,
                0, 0, 0, -1, -1, -1, 0,
                0, 0, 0,
                0, 0, 0, 0, 0.0, 0.0, 0,
                0, 0, seed48, 0, 0.0)
            || !gm_runtime_set_dimension(&runtime, player_dimension)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i)
        if (runtime.projectiles[i].active
                && runtime.projectiles[i].eid == eid) {
            p = &runtime.projectiles[i];
            slot = i;
            break;
        }
    if (!p || !gm_runtime_tick_projectile_now(&runtime, slot)
            || !gm_runtime_set_dimension(&runtime, source_dimension)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    printf("{\"ok\":true,\"eid\":%d,\"type\":%d,"
           "\"position_bits\":[\"%016" PRIx64 "\","
           "\"%016" PRIx64 "\",\"%016" PRIx64 "\"],"
           "\"motion_bits\":[\"%016" PRIx64 "\","
           "\"%016" PRIx64 "\",\"%016" PRIx64 "\"],"
           "\"rotation_bits\":[\"%08" PRIx32 "\","
           "\"%08" PRIx32 "\",\"%08" PRIx32 "\","
           "\"%08" PRIx32 "\"],\"seed48\":%" PRIu64 ","
           "\"ticks_existed\":%d,\"ticks_in_air\":%d,"
           "\"portal_counter\":%d,\"portal_cooldown\":%d,"
           "\"dead\":%s}\n",
           p->eid, p->type,
           double_bits(p->x), double_bits(p->y), double_bits(p->z),
           double_bits(p->vx), double_bits(p->vy), double_bits(p->vz),
           float_bits(p->yaw), float_bits(p->pitch),
           float_bits(p->prev_yaw), float_bits(p->prev_pitch),
           p->random_seed48, p->age, p->throwable_ticks_in_air,
           p->portal_counter, p->portal_cooldown,
           p->active ? "false" : "true");
    gm_runtime_destroy(&runtime);
    return 0;
}
