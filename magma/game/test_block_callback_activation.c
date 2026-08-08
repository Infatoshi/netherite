#include "game/runtime.h"
#include "mc_rng.h"

#include <inttypes.h>
#include <stdio.h>

static int run_dragon(void) {
    GmConfig cfg;
    GmRuntime r;
    JavaRandom seed;
    char error[256];
    const int sx = 8, sy = 200, sz = 6;
    int tx = 0, ty = 0, tz = 0, found = 0;
    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.view_distance = 1;
    cfg.render = GM_RENDER_OFF;
    cfg.mobs = 0;
    if (!gm_runtime_init(&r, &cfg, error, sizeof error)) return 0;
    gm_runtime_set_pose(&r, 8.5, 200.0, 8.5, 0.0F, 0.0F);
    if (!gm_runtime_load_block(&r, sx, sy, sz, 122, 0)) return 0;
    jrand_set(&seed, 1234);
    if (!gm_runtime_set_world_random_seed48(&r, seed.seed)) return 0;
    int result = gm_runtime_use_block(&r, sx, sy, sz);
    for (int x = sx - 15; x <= sx + 15; ++x)
        for (int y = sy - 7; y <= sy + 7; ++y)
            for (int z = sz - 15; z <= sz + 15; ++z)
                if (gm_world_block(r.world, x, y, z) == 122) {
                    tx = x; ty = y; tz = z; ++found;
                }
    JavaRandom after = {r.world_random_seed48};
    printf("A BlockDragonEgg %s %d %d %d %d %" PRId64 "\n",
        result ? "true" : "false", tx, ty, tz,
        gm_world_block(r.world, sx, sy, sz) == 0 ? 0 : 1,
        (int64_t)jrand_long(&after));
    gm_runtime_destroy(&r);
    return found == 1;
}

static int run_moving(void) {
    GmConfig cfg;
    GmRuntime r;
    char error[256];
    const int x = 8, y = 200, z = 6;
    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.view_distance = 1;
    cfg.render = GM_RENDER_OFF;
    cfg.mobs = 0;
    if (!gm_runtime_init(&r, &cfg, error, sizeof error)) return 0;
    gm_runtime_set_pose(&r, 8.5, 200.0, 8.5, 0.0F, 0.0F);
    if (!gm_runtime_load_block(&r, x, y, z, 36, 0)) return 0;
    int result = gm_runtime_use_block(&r, x, y, z);
    printf("A BlockPistonMoving %s %d\n", result ? "true" : "false",
        gm_world_block(r.world, x, y, z) == 0 ? 0 : 1);
    gm_runtime_destroy(&r);
    return 1;
}

int main(void) {
    return run_dragon() && run_moving() ? 0 : 1;
}
