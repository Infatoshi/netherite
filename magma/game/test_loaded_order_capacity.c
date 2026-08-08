#include "game/runtime.h"

#include <stdio.h>

#define CHECK(c, m) do { if (!(c)) { \
    fprintf(stderr, "FAIL: %s\n", (m)); return 1; } } while (0)

int main(void) {
    const char *checkpoint = "loaded-order-capacity-checkpoint.bin";
    GmRuntime runtime;
    GmConfig config;
    GmRuntimeLoadedTile tile;
    char error[256];
    int eid;

    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.mobs = 0;
    config.weather = 0;
    CHECK(gm_runtime_init(&runtime, &config, error, sizeof error), error);
    for (int order = 0; order <= GM_RUNTIME_LOADED_ENTITY_ORDER; ++order) {
        int x = order;
        int z = -order;
        CHECK(gm_runtime_restore_loaded_entity_order(
                  &runtime, order, 100000 + order),
              "loaded-entity order grows beyond hot capacity");
        CHECK(gm_runtime_restore_loaded_tile_order(
                  &runtime, order, x, 64, z),
              "loaded-tile order grows beyond hot capacity");
        CHECK(gm_runtime_restore_tickable_tile_order(
                  &runtime, order, x, 64, z),
              "tickable-tile order grows beyond hot capacity");
    }
    CHECK(runtime.loaded_entity_order_cap > GM_RUNTIME_LOADED_ENTITY_ORDER
              && runtime.loaded_tile_order_cap > GM_RUNTIME_LOADED_TILE_ORDER
              && runtime.tickable_tile_order_cap > GM_RUNTIME_LOADED_TILE_ORDER
              && gm_runtime_loaded_entity_order_get(
                  &runtime, GM_RUNTIME_LOADED_ENTITY_ORDER, &eid)
              && eid == 100000 + GM_RUNTIME_LOADED_ENTITY_ORDER
              && gm_runtime_loaded_tile_order_get(
                  &runtime, GM_RUNTIME_LOADED_TILE_ORDER, &tile)
              && tile.x == GM_RUNTIME_LOADED_TILE_ORDER
              && tile.z == -GM_RUNTIME_LOADED_TILE_ORDER,
          "grown causal orders preserve terminal payload");

    CHECK(gm_runtime_set_dimension(&runtime, -1)
              && gm_runtime_restore_loaded_entity_order(&runtime, 0, 77)
              && gm_runtime_restore_loaded_tile_order(
                  &runtime, 0, 7, 70, 7)
              && gm_runtime_restore_tickable_tile_order(
                  &runtime, 0, 7, 70, 7)
              && gm_runtime_set_dimension(&runtime, 0),
          "grown causal orders survive an opposite-dimension fork");
    CHECK(runtime.loaded_entity_order_count
                  == GM_RUNTIME_LOADED_ENTITY_ORDER + 1
              && gm_runtime_loaded_entity_order_get(
                  &runtime, GM_RUNTIME_LOADED_ENTITY_ORDER, &eid)
              && eid == 100000 + GM_RUNTIME_LOADED_ENTITY_ORDER,
          "dimension return restores grown entity order exactly");
    CHECK(gm_runtime_write_checkpoint(&runtime, checkpoint)
              && gm_runtime_load_checkpoint(&runtime, checkpoint)
              && runtime.loaded_entity_order_count
                  == GM_RUNTIME_LOADED_ENTITY_ORDER + 1
              && runtime.loaded_entity_order_cap
                  > GM_RUNTIME_LOADED_ENTITY_ORDER
              && gm_runtime_set_dimension(&runtime, -1)
              && gm_runtime_loaded_entity_order_get(&runtime, 0, &eid)
              && eid == 77
              && gm_runtime_set_dimension(&runtime, 0)
              && gm_runtime_loaded_tile_order_get(
                  &runtime, GM_RUNTIME_LOADED_TILE_ORDER, &tile)
              && tile.x == GM_RUNTIME_LOADED_TILE_ORDER,
          "grown cross-dimension causal orders survive checkpoint reload");
    (void)remove(checkpoint);
    gm_runtime_destroy(&runtime);
    puts("loaded_order_capacity: PASS");
    return 0;
}
