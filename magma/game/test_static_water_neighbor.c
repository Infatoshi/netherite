#include "game/runtime.h"

#include <stdio.h>

static int fail;

#define CHECK(C, M) do { \
    if (!(C)) { fprintf(stderr, "FAIL: %s\n", M); fail = 1; } \
} while (0)

int main(void) {
    GmConfig config;
    GmRuntime runtime;
    char err[256];
    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 2;
    config.mobs = 0;
    config.weather = 0;
    config.render = GM_RENDER_OFF;
    CHECK(gm_runtime_init(&runtime, &config, err, sizeof err),
          "runtime initializes");
    if (!runtime.world) return 1;
    gm_world_set_block_meta(runtime.world, 12, 78, 9, 152, 0);
    gm_world_set_block_meta(runtime.world, 13, 77, 8, 9, 0);
    gm_world_set_block_meta(runtime.world, 13, 78, 8, 111, 0);
    CHECK(gm_runtime_set_entity_id_cursor(&runtime, 5530)
              && gm_runtime_set_world_random_seed48(&runtime, UINT64_C(0))
              && gm_runtime_set_math_random_seed48(
                  &runtime, UINT64_C(0x0FEDCBA98765))
              && gm_runtime_set_block(&runtime, 12, 78, 8, 33, 5),
          "piston extension succeeds");
    GmRuntimeScheduledTick pending;
    CHECK(gm_world_block(runtime.world, 13, 77, 8) == 8
              && gm_world_meta(runtime.world, 13, 77, 8) == 0
              && gm_runtime_scheduled_tick_count(&runtime) == 1
              && gm_runtime_scheduled_tick_get(&runtime, 0, &pending)
              && pending.x == 13 && pending.y == 77 && pending.z == 8
              && pending.block == 8
              && pending.time == runtime.clock.total_time + 5
              && pending.priority == 0 && pending.order == 0,
          "piston waterlily destruction wakes its static-water support");
    if (fail)
        fprintf(stderr, "water=%d:%d scheduled=%d\n",
                gm_world_block(runtime.world, 13, 77, 8),
                gm_world_meta(runtime.world, 13, 77, 8),
                gm_runtime_scheduled_tick_count(&runtime));
    gm_runtime_destroy(&runtime);
    if (!fail) puts("static_water_neighbor: PASS");
    return fail ? 1 : 0;
}
