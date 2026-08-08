#include "game/runtime.h"

#include <stdio.h>
#include <string.h>

#define CHECK(c, m) do { if (!(c)) { \
    fprintf(stderr, "FAIL: %s\n", (m)); return 1; } } while (0)

int main(void) {
    GmConfig config;
    GmRuntime runtime;
    GmAction idle;
    char error[256];
    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.mobs = 0;
    config.render = GM_RENDER_OFF;
    CHECK(gm_runtime_init(&runtime, &config, error, sizeof error), error);
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    gm_runtime_set_pose(&runtime, 10.5, 78.0, 5.5, 0.0f, 0.0f);
    gm_world_set_block_meta(runtime.world, 10, 78, 7, 53, 0);
    CHECK(!gm_runtime_use_block(&runtime, 10, 78, 7)
            && gm_world_block(runtime.world, 10, 78, 7) == 53
            && gm_world_meta(runtime.world, 10, 78, 7) == 0,
          "ordinary stair activation delegates to false model callback");
    gm_runtime_set_total_time(&runtime, 50);
    CHECK(gm_runtime_schedule_tick(&runtime, 10, 78, 7, 53, 51, 0, 0),
          "stair model update delegate enters scheduled queue");
    gm_runtime_tick(&runtime, idle);
    CHECK(gm_world_block(runtime.world, 10, 78, 7) == 53
            && gm_world_meta(runtime.world, 10, 78, 7) == 0
            && gm_runtime_scheduled_tick_count(&runtime) == 0,
          "ordinary stair update delegates to empty model callback");
    gm_runtime_destroy(&runtime);
    puts("PASS block callback no-op native controls");
    return 0;
}
