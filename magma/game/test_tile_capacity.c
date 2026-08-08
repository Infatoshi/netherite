#include "game/runtime.h"

#include <stdio.h>

#define CHECK(c, m) do { if (!(c)) { \
    fprintf(stderr, "FAIL: %s\n", (m)); return 1; } } while (0)

int main(void) {
    const char *checkpoint = "tile-capacity-checkpoint.bin";
    GmRuntime runtime;
    GmConfig config;
    GmRuntimeFurnace furnace;
    GmRuntimeComparator comparator;
    char error[256];

    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.mobs = 0;
    config.weather = 0;
    CHECK(gm_runtime_init(&runtime, &config, error, sizeof error), error);
    for (int index = 0; index <= GM_RUNTIME_FURNACES; ++index) {
        int x = index;
        CHECK(gm_runtime_load_block(&runtime, x, 200, 0, 61, 0)
                  && gm_runtime_furnace_set_slot(
                      &runtime, 0, x, 200, 0,
                      0, 4, 1, 0, 0, 0, 0, 200),
              "furnace store grows beyond hot capacity");
    }
    for (int index = 0; index <= GM_RUNTIME_COMPARATORS; ++index)
        CHECK(gm_runtime_load_block(
                  &runtime, index, 202, 0, 149, 0),
              "comparator store grows beyond hot capacity");
    for (int index = 0; index <= GM_RUNTIME_DAYLIGHT_DETECTORS; ++index)
        CHECK(gm_runtime_load_block(
                  &runtime, index, 204, 0, 151, 0),
              "daylight-detector store grows beyond hot capacity");
    CHECK(runtime.furnaces_cap > GM_RUNTIME_FURNACES
              && gm_runtime_furnace_count(&runtime)
                  == GM_RUNTIME_FURNACES + 1
              && gm_runtime_furnace_get(
                  &runtime, GM_RUNTIME_FURNACES, &furnace)
              && furnace.wx == GM_RUNTIME_FURNACES,
          "grown furnace store preserves terminal payload");
    CHECK(runtime.comparators_cap > GM_RUNTIME_COMPARATORS
              && gm_runtime_comparator_count(&runtime)
                  == GM_RUNTIME_COMPARATORS + 1
              && gm_runtime_comparator_get(
                  &runtime, GM_RUNTIME_COMPARATORS, &comparator)
              && comparator.x == GM_RUNTIME_COMPARATORS,
          "grown comparator store preserves terminal payload");
    CHECK(runtime.daylight_detectors_cap > GM_RUNTIME_DAYLIGHT_DETECTORS
              && runtime.daylight_detector_count
                  == GM_RUNTIME_DAYLIGHT_DETECTORS + 1
              && runtime.daylight_detectors[GM_RUNTIME_DAYLIGHT_DETECTORS].x
                  == GM_RUNTIME_DAYLIGHT_DETECTORS,
          "grown daylight-detector store preserves terminal payload");
    CHECK(gm_runtime_write_checkpoint(&runtime, checkpoint)
              && gm_runtime_load_checkpoint(&runtime, checkpoint)
              && runtime.furnaces_cap > GM_RUNTIME_FURNACES
              && runtime.comparators_cap > GM_RUNTIME_COMPARATORS
              && runtime.daylight_detectors_cap
                  > GM_RUNTIME_DAYLIGHT_DETECTORS
              && runtime.daylight_detector_count
                  == GM_RUNTIME_DAYLIGHT_DETECTORS + 1,
          "grown tile stores survive checkpoint reload");
    (void)remove(checkpoint);
    gm_runtime_destroy(&runtime);
    puts("tile_capacity: PASS");
    return 0;
}
