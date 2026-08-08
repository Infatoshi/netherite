#include "game/runtime.h"

#include <stdio.h>
#include <string.h>

static int init_runtime(GmRuntime *runtime) {
    GmConfig cfg;
    char error[256];
    gm_config_defaults(&cfg);
    cfg.view_distance = 1;
    if (!gm_runtime_init(runtime, &cfg, error, sizeof error)) {
        fprintf(stderr, "runtime init: %s\n", error);
        return 0;
    }
    for (int y = 77; y <= 82; ++y)
        for (int z = 7; z <= 10; ++z)
            for (int x = 10; x <= 16; ++x)
                gm_world_set_block_meta(runtime->world, x, y, z, 0, 0);
    return 1;
}

static void dump(const char *label, const GmRuntime *runtime) {
    fprintf(stderr,
        "%s count=%d base=%d:%d front=%d:%d far=%d:%d "
        "rng=%012llx\n",
        label, runtime->piston_count,
        gm_world_block(runtime->world, 12, 78, 8),
        gm_world_meta(runtime->world, 12, 78, 8),
        gm_world_block(runtime->world, 13, 78, 8),
        gm_world_meta(runtime->world, 13, 78, 8),
        gm_world_block(runtime->world, 14, 78, 8),
        gm_world_meta(runtime->world, 14, 78, 8),
        (unsigned long long)runtime->world_random_seed48);
    for (int index = 0; index < runtime->piston_count; ++index) {
        const GmRuntimePiston *piston = &runtime->pistons[index];
        fprintf(stderr,
            "  [%d] xyz=%d,%d,%d moved=%d:%d face=%d ext=%d src=%d "
            "progress=%.1f last=%.1f\n",
            index, piston->x, piston->y, piston->z,
            piston->moved_block, piston->moved_meta, piston->facing,
            piston->extending, piston->source,
            piston->progress, piston->last_progress);
    }
}

static int sticky_minimum_pulse(void) {
    GmRuntime runtime = {0};
    GmAction idle = {0};
    int ok = 1;
    idle.hotbar_sel = -1;
    if (!init_runtime(&runtime)) return 0;
    gm_world_set_block_meta(runtime.world, 12, 78, 8, 29, 5);
    gm_world_set_block_meta(runtime.world, 13, 78, 8, 1, 0);
    ok = gm_runtime_set_world_random_seed48(&runtime, UINT64_C(0))
        && gm_runtime_set_math_random_seed48(
            &runtime, UINT64_C(0x0FEDCBA98765))
        && gm_runtime_set_block(&runtime, 12, 78, 9, 152, 0)
        && runtime.piston_count == 2;
    gm_runtime_tick(&runtime, idle);
    ok = ok && runtime.piston_count == 2
        && runtime.pistons[0].progress == 0.5f
        && runtime.pistons[1].progress == 0.5f
        && gm_runtime_set_block(&runtime, 12, 78, 9, 0, 0);
    if (!(runtime.piston_count == 1
            && runtime.pistons[0].moved_block == 29
            && !runtime.pistons[0].extending
            && runtime.pistons[0].source)) {
        dump("sticky removal", &runtime);
        ok = 0;
    }
    for (int tick = 0; tick < 3; ++tick)
        gm_runtime_tick(&runtime, idle);
    if (!(runtime.piston_count == 0
            && gm_world_block(runtime.world, 12, 78, 8) == 29
            && gm_world_meta(runtime.world, 12, 78, 8) == 5
            && gm_world_block(runtime.world, 13, 78, 8) == 0
            && gm_world_block(runtime.world, 14, 78, 8) == 1)) {
        dump("sticky settle", &runtime);
        ok = 0;
    }
    gm_runtime_destroy(&runtime);
    return ok;
}

static int normal_minimum_pulse(void) {
    GmRuntime runtime = {0};
    GmAction idle = {0};
    int ok = 1;
    idle.hotbar_sel = -1;
    if (!init_runtime(&runtime)) return 0;
    gm_world_set_block_meta(runtime.world, 12, 78, 8, 33, 5);
    gm_world_set_block_meta(runtime.world, 13, 78, 8, 1, 0);
    ok = gm_runtime_set_world_random_seed48(&runtime, UINT64_C(0))
        && gm_runtime_set_block(&runtime, 12, 78, 9, 152, 0)
        && runtime.piston_count == 2;
    gm_runtime_tick(&runtime, idle);
    ok = ok && gm_runtime_set_block(&runtime, 12, 78, 9, 0, 0);
    if (!(runtime.piston_count == 2
            && runtime.pistons[0].moved_block == 1
            && runtime.pistons[0].extending
            && runtime.pistons[1].moved_block == 33
            && !runtime.pistons[1].extending
            && runtime.pistons[1].source)) {
        dump("normal removal", &runtime);
        ok = 0;
    }
    for (int tick = 0; tick < 3; ++tick)
        gm_runtime_tick(&runtime, idle);
    if (!(runtime.piston_count == 0
            && gm_world_block(runtime.world, 12, 78, 8) == 33
            && gm_world_meta(runtime.world, 12, 78, 8) == 5
            && gm_world_block(runtime.world, 13, 78, 8) == 0
            && gm_world_block(runtime.world, 14, 78, 8) == 1)) {
        dump("normal settle", &runtime);
        ok = 0;
    }
    gm_runtime_destroy(&runtime);
    return ok;
}

static int slime_extension(void) {
    GmRuntime runtime = {0};
    int ok;
    if (!init_runtime(&runtime)) return 0;
    gm_world_set_block_meta(runtime.world, 12, 79, 8, 33, 5);
    gm_world_set_block_meta(runtime.world, 13, 79, 8, 165, 0);
    gm_world_set_block_meta(runtime.world, 13, 80, 8, 1, 0);
    ok = gm_runtime_set_world_random_seed48(&runtime, UINT64_C(0))
        && gm_runtime_set_block(&runtime, 12, 79, 9, 152, 0)
        && runtime.piston_count == 3
        && runtime.pistons[0].x == 14 && runtime.pistons[0].y == 80
        && runtime.pistons[0].moved_block == 1
        && runtime.pistons[1].x == 14 && runtime.pistons[1].y == 79
        && runtime.pistons[1].moved_block == 165
        && runtime.pistons[2].x == 13 && runtime.pistons[2].y == 79
        && runtime.pistons[2].moved_block == 34
        && runtime.pistons[2].source;
    if (!ok) {
        fprintf(stderr, "slime count=%d rng=%012llx\n",
            runtime.piston_count,
            (unsigned long long)runtime.world_random_seed48);
        for (int index = 0; index < runtime.piston_count; ++index) {
            const GmRuntimePiston *piston = &runtime.pistons[index];
            fprintf(stderr, "  [%d] xyz=%d,%d,%d moved=%d:%d src=%d\n",
                index, piston->x, piston->y, piston->z,
                piston->moved_block, piston->moved_meta, piston->source);
        }
    }
    gm_runtime_destroy(&runtime);
    return ok;
}

int main(void) {
    if (!sticky_minimum_pulse()
            || !normal_minimum_pulse()
            || !slime_extension())
        return 1;
    puts("piston reentrant: PASS");
    return 0;
}
