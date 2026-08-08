#include "game/runtime.h"

#include <limits.h>
#include <stdio.h>

#define CHECK(c, m) do { if (!(c)) { \
    fprintf(stderr, "FAIL: %s\n", (m)); return 1; } } while (0)

static void fill_chunk_refs(
        GmRuntimeChunkRef *refs, int count, int base) {
    for (int index = 0; index < count; ++index) {
        refs[index].chunk_x = base + index;
        refs[index].chunk_z = base - index;
    }
}

int main(void) {
    const char *checkpoint = "structure-registry-capacity.bin";
    GmRuntime runtime;
    GmConfig config;
    char error[256] = {0};

    gm_config_defaults(&config);
    config.seed = 0;
    config.world = GM_WORLD_DEFAULT;
    config.view_distance = 3;
    config.villages = 0;
    config.mobs = 1;
    config.weather = 0;
    CHECK(gm_runtime_init(&runtime, &config, error, sizeof error), error);

    runtime.village_resident_count = GM_RUNTIME_VILLAGE_RESIDENTS;
    for (int index = 0; index < runtime.village_resident_count; ++index) {
        runtime.village_residents[index].x = -100000 - index;
        runtime.village_residents[index].eid = -1;
    }
    CHECK(gm_runtime_spawn_villager_fixture(
              &runtime, 9000, 0.5, 70.0, 0.5,
              0.0, 0.0, 0.0, 0.0F, 20.0F,
              0, 0, 0, 0, 0, 1, 0, 0.0)
              && runtime.village_resident_count
                  == GM_RUNTIME_VILLAGE_RESIDENTS + 1
              && runtime.village_residents[GM_RUNTIME_VILLAGE_RESIDENTS].eid
                  == 9000,
          "villager claims grow beyond the former resident limit");

    for (int index = 0; index <= GM_RUNTIME_VILLAGE_POSITION_QUEUE; ++index)
        CHECK(gm_runtime_village_position_enqueue(
                  &runtime, index, 80, -index),
              "village collection queue accepts every distinct position");
    CHECK(runtime.village_position_queue_cap
              > GM_RUNTIME_VILLAGE_POSITION_QUEUE
              && runtime.village_position_count
                  == GM_RUNTIME_VILLAGE_POSITION_QUEUE + 1,
          "village collection queue grows without dropping its tail");

    runtime.igloo_resident_count = GM_RUNTIME_IGLOO_RESIDENTS;
    for (int index = 0; index < runtime.igloo_resident_count; ++index)
        runtime.igloo_residents[index].x = -100000.0 - index;
    gm_world_ensure(runtime.world, -499, -171, 3);
    gm_runtime_set_pose(
        &runtime, -499 * 16 + 8.5, 64.0, -171 * 16 + 8.5, 0.0F, 0.0F);
    runtime.igloo_scan_x = INT_MIN;
    CHECK(gm_runtime_sync_igloo_residents(&runtime) >= 2
              && runtime.igloo_resident_count > GM_RUNTIME_IGLOO_RESIDENTS
              && runtime.igloo_residents_cap > GM_RUNTIME_IGLOO_RESIDENTS,
          "natural igloo claims grow beyond the former resident limit");

    runtime.swamp_witch_count = GM_RUNTIME_SWAMP_WITCHES;
    for (int index = 0; index < runtime.swamp_witch_count; ++index)
        runtime.swamp_witches[index].x = -100000.0 - index;
    gm_world_ensure(runtime.world, -447, 76, 3);
    gm_runtime_set_pose(
        &runtime, -447 * 16 + 8.5, 64.0, 76 * 16 + 8.5, 0.0F, 0.0F);
    runtime.swamp_witch_scan_x = INT_MIN;
    CHECK(gm_runtime_sync_swamp_witches(&runtime) >= 1
              && runtime.swamp_witch_count > GM_RUNTIME_SWAMP_WITCHES
              && runtime.swamp_witches_cap > GM_RUNTIME_SWAMP_WITCHES,
          "natural swamp-witch claims grow beyond the former limit");

    runtime.mansion_count = GM_RUNTIME_MANSIONS;
    fill_chunk_refs(runtime.mansions, runtime.mansion_count, -200000);
    runtime.mansion_resident_count = GM_RUNTIME_MANSION_RESIDENTS;
    for (int index = 0; index < runtime.mansion_resident_count; ++index)
        runtime.mansion_residents[index].x = -100000 - index;
    CHECK(gm_runtime_generate_mansion(&runtime, 0, 0, 70) > 1
              && runtime.mansion_count == GM_RUNTIME_MANSIONS + 1
              && runtime.mansions_cap > GM_RUNTIME_MANSIONS
              && runtime.mansion_resident_count
                  > GM_RUNTIME_MANSION_RESIDENTS
              && runtime.mansion_residents_cap
                  > GM_RUNTIME_MANSION_RESIDENTS,
          "mansion starts and marker residents grow together");

    runtime.monument_count = GM_RUNTIME_MONUMENTS;
    fill_chunk_refs(runtime.monuments, runtime.monument_count, -300000);
    CHECK(gm_runtime_generate_monument(&runtime, 32, 32) > 0
              && runtime.monument_count == GM_RUNTIME_MONUMENTS + 1
              && runtime.monuments_cap > GM_RUNTIME_MONUMENTS,
          "monument starts grow beyond the former registry limit");

    CHECK(gm_runtime_set_dimension(&runtime, 1), "enter End dimension");
    runtime.end_city_count = GM_RUNTIME_END_CITIES;
    fill_chunk_refs(runtime.end_cities, runtime.end_city_count, -400000);
    CHECK(gm_runtime_generate_end_city(&runtime, 0, 0, 70) > 1
              && runtime.end_city_count == GM_RUNTIME_END_CITIES + 1
              && runtime.end_cities_cap > GM_RUNTIME_END_CITIES,
          "End-city starts grow beyond the former registry limit");

    CHECK(gm_runtime_write_checkpoint(&runtime, checkpoint)
              && gm_runtime_load_checkpoint(&runtime, checkpoint)
              && runtime.end_city_count == GM_RUNTIME_END_CITIES + 1
              && runtime.mansion_count == GM_RUNTIME_MANSIONS + 1
              && runtime.monument_count == GM_RUNTIME_MONUMENTS + 1
              && runtime.mansion_resident_count
                  > GM_RUNTIME_MANSION_RESIDENTS
              && runtime.village_position_count
                  == GM_RUNTIME_VILLAGE_POSITION_QUEUE + 1
              && runtime.igloo_resident_count > GM_RUNTIME_IGLOO_RESIDENTS
              && runtime.swamp_witch_count > GM_RUNTIME_SWAMP_WITCHES,
          "all grown structure registries survive one checkpoint reload");
    (void)remove(checkpoint);
    gm_runtime_destroy(&runtime);
    puts("structure_registry_capacity: PASS");
    return 0;
}
