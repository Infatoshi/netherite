#include "game/runtime.h"

#include <stdint.h>
#include <stdio.h>

#define CHECK(c, m) do { \
    if (!(c)) { fprintf(stderr, "FAIL: %s\n", m); return 1; } \
} while (0)

static int zombie_count(const GmRuntime *runtime) {
    const EwStore *store = runtime->mobs.current
        ? &runtime->mobs.b : &runtime->mobs.a;
    int count = 0;
    for (int slot = 1; slot < EW_MAX_ENTITIES; ++slot)
        if (store->alive[slot] && store->type[slot] == EW_TYPE_ZOMBIE)
            ++count;
    return count;
}

static int last_zombie(
        const GmRuntime *runtime, int *x, int *y, int *z, uint32_t *yaw) {
    const EwStore *store = runtime->mobs.current
        ? &runtime->mobs.b : &runtime->mobs.a;
    int found = 0;
    for (int slot = 1; slot < EW_MAX_ENTITIES; ++slot) {
        if (!store->alive[slot] || store->type[slot] != EW_TYPE_ZOMBIE)
            continue;
        union { float f; uint32_t u; } bits = {store->yaw[slot]};
        if (x) *x = (int)store->x[slot];
        if (y) *y = (int)store->y[slot];
        if (z) *z = (int)store->z[slot];
        if (yaw) *yaw = bits.u;
        found = 1;
    }
    return found;
}

int main(void) {
    GmConfig config;
    GmRuntime runtime;
    JavaRandom random;
    char error[256] = {0};
    gm_config_defaults(&config);
    config.seed = 0;
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    /* VillageSiege is independent of doMobSpawning. Keep ordinary loaded
     * entity AI cold so this state-machine regression remains sub-second. */
    config.mobs = 0;
    config.villages = 0;
    config.weather = 0;
    config.daylight = 0;
    CHECK(gm_runtime_init(&runtime, &config, error, sizeof error), error);
    runtime.gamerules.doMobSpawning = 0;
    for (int x = -16; x <= 31; ++x)
        for (int z = -16; z <= 31; ++z) {
            gm_world_set_block(runtime.world, x, 63, z, 1);
            for (int y = 64; y <= 66; ++y)
                gm_world_set_block(runtime.world, x, y, z, 0);
        }
    gm_runtime_set_pose(&runtime, 0.5, 64.0, 0.5, 0.0F, 0.0F);
    runtime.server_player = runtime.player;
    runtime.clock.world_time = 1000;

    CHECK(gm_runtime_village_collection_begin(&runtime, 100, 1),
          "restore one siege-eligible village collection");
    CHECK(gm_runtime_village_state_restore(
              &runtime, 0, 20, 32, 0, 80, 100, 0,
              0, 64, 0, 0, 640, 0),
          "restore siege-eligible village state");
    for (int door = 0; door < 10; ++door) {
        int x = door - 5;
        gm_world_set_block(runtime.world, x, 64, 4, 64);
        CHECK(gm_runtime_village_door_restore(
                  &runtime, 0, x, 64, 4, 0, 2, 80),
              "restore retained village door");
    }
    gm_runtime_tick_village_siege_fixture(&runtime);
    CHECK(runtime.village_siege_state == 0,
          "daytime initializes the transient siege controller");
    runtime.clock.world_time = 17999;
    /* This cursor takes the accepted lottery and has twenty valid flat-world
     * spawn searches after each intervening Village.tick nextInt(50). */
    jrand_set(&random, 113668);
    CHECK(gm_runtime_set_world_random_seed48(&runtime, random.seed),
          "seed Village.tick plus VillageSiege cursor");
    gm_runtime_tick_village_siege_fixture(&runtime);
    if (!(runtime.village_siege_has_setup
            && runtime.village_siege_state == 1
            && runtime.village_siege_count == 19))
        fprintf(stderr, "siege setup state: setup=%d state=%d count=%d "
                "next=%d village=%d xyz=%d,%d,%d\n",
                runtime.village_siege_has_setup,
                runtime.village_siege_state,
                runtime.village_siege_count,
                runtime.village_siege_next_spawn,
                runtime.village_siege_village,
                runtime.village_siege_x,
                runtime.village_siege_y,
                runtime.village_siege_z);
    CHECK(runtime.village_siege_has_setup
              && runtime.village_siege_state == 1
              && runtime.village_siege_count == 19,
          "midnight 1-in-10 branch sets up and consumes one siege slot");
    if (zombie_count(&runtime) != 1)
        fprintf(stderr, "siege first zombie count: %d\n",
                zombie_count(&runtime));
    CHECK(zombie_count(&runtime) == 1,
          "the first eligible siege boundary spawns one zombie");
    int first_x = runtime.village_siege_x;
    int first_z = runtime.village_siege_z;
    CHECK(first_x * first_x + first_z * first_z < 32 * 32,
          "siege center lies inside the selected village radius");
    int zx = 0, zy = 0, zz = 0;
    uint32_t yaw = 0;
    CHECK(first_x == 19 && first_z == 20
              && last_zombie(&runtime, &zx, &zy, &zz, &yaw)
              && zx == 12 && zy == 64 && zz == 16
              && yaw == UINT32_C(0x4360166e)
              && runtime.world_random_seed48
                    == UINT64_C(0x9f59e8268502),
          "first siege setup/spawn matches the direct Java oracle");

    for (int tick = 0; tick < 57; ++tick)
        gm_runtime_tick_village_siege_fixture(&runtime);
    CHECK(runtime.village_siege_count == 0
              && zombie_count(&runtime) == 16
              && last_zombie(&runtime, &zx, &zy, &zz, &yaw)
              && zx == 24 && zy == 64 && zz == 21
              && yaw == UINT32_C(0x431374e1)
              && runtime.world_random_seed48
                    == UINT64_C(0xab660c2d8e9c),
          "three-tick attempts and accepted zombies match Java");
    for (int tick = 0; tick < 3; ++tick)
        gm_runtime_tick_village_siege_fixture(&runtime);
    CHECK(runtime.village_siege_state == 2
              && runtime.world_random_seed48
                    == UINT64_C(0x53d28fcb9d89),
          "exhausted siege becomes terminal for the rest of the night");

    runtime.clock.world_time = 1000;
    gm_runtime_tick_village_siege_fixture(&runtime);
    CHECK(runtime.village_siege_state == 0
              && runtime.world_random_seed48
                    == UINT64_C(0x13d731872960),
          "daytime rearms the next night's siege lottery");
    gm_runtime_destroy(&runtime);
    puts("village_siege_runtime: PASS setup, spawn cadence, terminal, rearm");
    return 0;
}
