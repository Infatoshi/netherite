#include "game/runtime.h"

#include <stdio.h>

static int init(GmRuntime *runtime) {
    GmConfig config;
    char error[256];
    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    /* A scale-zero map samples 16x16 chunks around the player.  Keep that
     * complete window resident so bulk fixture writes cannot alias the
     * world's toroidal chunk slots. */
    config.view_distance = 9;
    config.mobs = 0;
    config.weather = 0;
    if (!gm_runtime_init(runtime, &config, error, sizeof error)) {
        fprintf(stderr, "map update oracle init: %s\n", error);
        return 0;
    }
    gm_mobs_set_natural_spawning(&runtime->mobs, 0);
    gm_runtime_set_pose(runtime, 0.5, 70.0, 0.5, 0.0F, 0.0F);
    return 1;
}

static void terrain(GmRuntime *runtime, int kind) {
    gm_world_finish_bulk_edit(runtime->world, 0, 0, 9);
    for (int x = -128; x < 128; ++x)
        for (int z = -128; z < 128; ++z) {
            int top = kind == 2 ? 60 + ((x + 128) >> 4) : 64;
            gm_world_load_block_meta(runtime->world, x, top, z, 1, 0);
            if (kind == 1 && x >= 0) {
                gm_world_load_block_meta(runtime->world, x, top + 1, z, 9, 0);
                if ((z & 8) != 0)
                    gm_world_load_block_meta(
                        runtime->world, x, top + 2, z, 9, 0);
            } else if (kind == 2 && (z & 16) != 0) {
                gm_world_load_block_meta(runtime->world, x, top + 1, z, 2, 0);
            }
        }
}

static int run(int kind) {
    GmRuntime runtime;
    if (!init(&runtime)) return 0;
    terrain(&runtime, kind);
    if (!gm_runtime_map_data_set(&runtime, 0, 0, 0)) {
        gm_runtime_destroy(&runtime);
        return 0;
    }
    GmRuntimeMapData *map = (GmRuntimeMapData *)
        gm_runtime_map_data_ref(&runtime, 0);
    if (!map) {
        gm_runtime_destroy(&runtime);
        return 0;
    }
    map->dimension = 0;
    map->x_center = 0;
    map->z_center = 0;
    for (int tick = 0; tick < 16; ++tick)
        if (!gm_runtime_update_filled_map_now(&runtime, 0)) {
            gm_runtime_destroy(&runtime);
            return 0;
        }
    printf("U %d %d ", kind, map->update_step);
    for (int index = 0; index < 128 * 128; ++index)
        printf("%02x", map->colors[index]);
    putchar('\n');
    gm_runtime_destroy(&runtime);
    return 1;
}

int main(void) {
    for (int kind = 0; kind < 3; ++kind)
        if (!run(kind)) return 1;
    return 0;
}
