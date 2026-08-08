#include "game/runtime.h"

#include <stdio.h>

static int stage(GmRuntime *runtime, int count) {
    GmWorld *world = runtime->worlds[1];
    for (int index = 0; index < count; ++index) {
        int x = index & 15;
        int z = (index >> 4) & 15;
        int y = 80 + (index >> 8);
        if (!gm_runtime_load_block_dim(runtime, 0, x, y, z, 36, 5))
            return 0;
        if (!gm_runtime_moving_piston_load(
                runtime, 0, x, y, z, 1, 0, 5,
                1, 0, 0.5f, 0.0f)) {
            fprintf(stderr, "load failed index=%d dim=%d block=%d meta=%d\n",
                index, runtime->dimension,
                gm_world_block(world, x, y, z),
                gm_world_meta(world, x, y, z));
            return 0;
        }
    }
    return gm_runtime_moving_piston_count(runtime) == count;
}

static int stage_ticks(GmRuntime *runtime, int count) {
    for (int index = 0; index < count; ++index)
        if (!gm_runtime_restore_scheduled_tick(
                runtime, -1, index, 80, 0, 55,
                100 + index, 0, index))
            return 0;
    return runtime->scheduled_tick_count == count;
}

int main(void) {
    static const int boundaries[] = {63, 64, 65, 257};
    static GmRuntime runtime;
    static GmRuntime before;
    static GmRuntime after;
    GmConfig cfg;
    char error[256];
    for (int boundary = 0;
            boundary < (int)(sizeof boundaries / sizeof boundaries[0]);
            ++boundary) {
        runtime = (GmRuntime){0};
        gm_config_defaults(&cfg);
        cfg.mobs = 0;
        cfg.weather = 0;
        if (!gm_runtime_init(&runtime, &cfg, error, sizeof error)) {
            fprintf(stderr, "runtime init: %s\n", error);
            return 1;
        }
        if (!stage(&runtime, boundaries[boundary])) {
            fprintf(stderr, "capacity failed at %d\n", boundaries[boundary]);
            gm_runtime_destroy(&runtime);
            return 1;
        }
        printf("%d:%d:%d\n", boundaries[boundary],
            runtime.piston_count, runtime.pistons_cap);
        gm_runtime_destroy(&runtime);
    }
    for (int boundary = 4095; boundary <= 4097; ++boundary) {
        runtime = (GmRuntime){0};
        gm_config_defaults(&cfg);
        cfg.mobs = 0;
        cfg.weather = 0;
        if (!gm_runtime_init(&runtime, &cfg, error, sizeof error)
                || !stage_ticks(&runtime, boundary)) {
            fprintf(stderr, "scheduled capacity failed at %d\n", boundary);
            gm_runtime_destroy(&runtime);
            return 1;
        }
        printf("tick-%d:%d:%d\n", boundary,
            runtime.scheduled_tick_count, runtime.scheduled_ticks_cap);
        gm_runtime_destroy(&runtime);
    }
    {
        const char *path = ".tmp/piston-capacity-checkpoint.bin";
        before = (GmRuntime){0};
        after = (GmRuntime){0};
        gm_config_defaults(&cfg);
        cfg.mobs = 0;
        cfg.weather = 0;
        if (!gm_runtime_init(&before, &cfg, error, sizeof error)
                || !stage(&before, 257)
                || !gm_runtime_write_checkpoint(&before, path)
                || !gm_runtime_init(&after, &cfg, error, sizeof error)
                || !gm_runtime_load_checkpoint(&after, path)
                || after.piston_count != 257
                || after.pistons_cap < 257) {
            fprintf(stderr, "257-piston checkpoint continuation failed\n");
            gm_runtime_destroy(&before);
            gm_runtime_destroy(&after);
            return 1;
        }
        gm_runtime_destroy(&before);
        gm_runtime_destroy(&after);
    }
    puts("piston capacity: PASS");
    return 0;
}
