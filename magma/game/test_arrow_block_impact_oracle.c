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

int main(int argc, char **argv) {
    if (argc != 16) return 2;
    int block = atoi(argv[1]);
    int meta = atoi(argv[2]);
    uint64_t seed = strtoull(argv[3], NULL, 10);
    double x = strtod(argv[4], NULL), y = strtod(argv[5], NULL);
    double z = strtod(argv[6], NULL), vx = strtod(argv[7], NULL);
    double vy = strtod(argv[8], NULL), vz = strtod(argv[9], NULL);
    double hit_x = strtod(argv[10], NULL), hit_y = strtod(argv[11], NULL);
    double hit_z = strtod(argv[12], NULL);
    int critical = atoi(argv[13]);
    int ticks = atoi(argv[14]);
    int remove_block = atoi(argv[15]);
    if (block <= 0 || block > 4095 || meta < 0 || meta > 15
            || seed >= (UINT64_C(1) << 48)
            || (critical != 0 && critical != 1)
            || ticks < 0 || ticks > 1200
            || (remove_block != 0 && remove_block != 1))
        return 2;
    GmConfig config;
    GmRuntime runtime;
    char error[256];
    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.seed = 0;
    if (!gm_runtime_init(&runtime, &config, error, sizeof error)) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    gm_world_set_block_meta(runtime.world, 25, 220, 24, block, meta);
    GmRuntimeProjectile *arrow = &runtime.projectiles[0];
    memset(arrow, 0, sizeof *arrow);
    arrow->active = 1;
    arrow->type = 1;
    arrow->eid = runtime.next_entity_id++;
    arrow->x = x; arrow->y = y; arrow->z = z;
    arrow->vx = vx; arrow->vy = vy; arrow->vz = vz;
    arrow->arrow_damage = 2.0;
    arrow->arrow_pickup_status = 1;
    arrow->arrow_critical = critical;
    arrow->fire_ticks = -1;
    arrow->random_seed48 = seed;
    if (!gm_runtime_player_arrow_block_hit_now(
            &runtime, 0, 25, 220, 24, hit_x, hit_y, hit_z)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    if (remove_block)
        gm_world_set_block_meta(runtime.world, 25, 220, 24, 0, 0);
    GmAction idle;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    for (int tick = 0; tick < ticks; ++tick)
        gm_runtime_tick(&runtime, idle);
    printf("{\"ok\":true,"
           "\"x_bits\":\"%016" PRIx64 "\","
           "\"y_bits\":\"%016" PRIx64 "\","
           "\"z_bits\":\"%016" PRIx64 "\","
           "\"vx_bits\":\"%016" PRIx64 "\","
           "\"vy_bits\":\"%016" PRIx64 "\","
           "\"vz_bits\":\"%016" PRIx64 "\","
           "\"in_ground\":%s,\"shake\":%d,\"critical\":%s,"
           "\"arrow_alive\":%s,"
           "\"tile_x\":%d,\"tile_y\":%d,\"tile_z\":%d,"
           "\"tile_block\":%d,\"tile_meta\":%d,"
           "\"ticks_in_ground\":%d,\"arrow_seed48\":%" PRIu64 "}\n",
           double_bits(arrow->x), double_bits(arrow->y),
           double_bits(arrow->z), double_bits(arrow->vx),
           double_bits(arrow->vy), double_bits(arrow->vz),
           arrow->arrow_in_ground ? "true" : "false",
           arrow->arrow_shake,
           arrow->arrow_critical ? "true" : "false",
           arrow->active ? "true" : "false",
           arrow->arrow_tile_x, arrow->arrow_tile_y,
           arrow->arrow_tile_z, arrow->arrow_tile_block,
           arrow->arrow_tile_meta, arrow->arrow_ticks_in_ground,
           arrow->random_seed48);
    gm_runtime_destroy(&runtime);
    return 0;
}
