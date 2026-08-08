#include "game/runtime.h"

#include <stdio.h>
#include <string.h>

#define CHECK(c, m) do { if (!(c)) { \
    fprintf(stderr, "FAIL: %s\n", (m)); return 1; } } while (0)

static uint64_t seed_for_next_int(int bound, int wanted) {
    for (uint64_t seed = 0; seed < UINT64_C(1) << 20; ++seed) {
        JavaRandom r;
        jrand_set_seed48(&r, seed);
        if (jrand_int_bound(&r, bound) == wanted)
            return seed;
    }
    return UINT64_MAX;
}

int main(void) {
    GmConfig cfg;
    GmRuntime r;
    GmAction idle;
    char err[256];
    gm_config_defaults(&cfg);
    cfg.seed = 0;
    cfg.view_distance = 1;
    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err), err);
    CHECK(!r.clock.raining && !r.clock.thundering
          && r.clock.rain_time == 0 && r.clock.thunder_time == 0
          && r.clock.rain_strength == 0.0f
          && r.clock.thunder_strength == 0.0f,
          "fresh WorldInfo weather state is clear/zero");

    /* Seed 0 has Extreme Hills at (64,192). Its base 0.2 temperature falls
     * below 0.15 at y=200 through Biome.TEMPERATURE_NOISE/elevation. */
    gm_world_ensure(r.world, 4, 12, 1);
    CHECK(gm_world_biome(r.world, 64, 192) == 3,
          "seed-0 cold-height fixture biome");
    CHECK(gm_world_temperature(r.world, 64, 64, 192) >= 0.15f
          && gm_world_temperature(r.world, 64, 200, 192) < 0.15f,
          "altitude-adjusted temperature crosses snow threshold");

    for (int x = 63; x <= 67; ++x)
        for (int z = 191; z <= 193; ++z)
            for (int y = 199; y <= 203; ++y)
                gm_world_set_block_meta(r.world, x, y, z, 0, 0);
    gm_world_set_block_meta(r.world, 64, 200, 192, 8, 0);
    gm_world_set_block_meta(r.world, 63, 200, 192, 8, 0);
    gm_world_set_block_meta(r.world, 65, 200, 192, 8, 0);
    gm_world_set_block_meta(r.world, 64, 200, 191, 8, 0);
    gm_world_set_block_meta(r.world, 64, 200, 193, 8, 0);
    CHECK(!gm_world_can_freeze(r.world, 64, 200, 192, 1),
          "source water surrounded on four sides does not freeze");
    gm_world_set_block_meta(r.world, 63, 200, 192, 0, 0);
    CHECK(gm_world_can_freeze(r.world, 64, 200, 192, 1),
          "exposed source water can freeze");
    CHECK((gm_runtime_weather_ice_snow_at(&r, 64, 192, 1) & 1)
          && gm_world_block(r.world, 64, 200, 192) == 79,
          "weather column freezes source water to ice");
    CHECK(!gm_world_can_snow(r.world, 64, 201, 192, 1),
          "ice cannot support a snow layer");

    gm_world_set_block_meta(r.world, 66, 200, 192, 1, 0);
    CHECK(gm_world_can_snow(r.world, 66, 201, 192, 1),
          "cold opaque support accepts snow");
    CHECK((gm_runtime_weather_ice_snow_at(&r, 66, 192, 1) & 2)
          && gm_world_block(r.world, 66, 201, 192) == 78
          && gm_world_meta(r.world, 66, 201, 192) == 0,
          "weather column places one-layer snow");

    /* Warm forest cauldron uses the exact one-in-20 World.rand draw. */
    for (int y = 80; y <= 84; ++y)
        gm_world_set_block_meta(r.world, 8, y, 8, 0, 0);
    gm_world_set_block_meta(r.world, 8, 80, 8, 118, 1);
    CHECK(gm_world_precipitation_kind(r.world, 8, 80, 8) == 1,
          "spawn forest precipitation is rain");
    {
        uint64_t seed = seed_for_next_int(20, 1);
        JavaRandom expected;
        CHECK(seed != UINT64_MAX, "find deterministic cauldron roll");
        jrand_set_seed48(&expected, seed);
        CHECK(jrand_int_bound(&expected, 20) == 1,
              "cauldron control roll");
        CHECK(gm_runtime_set_world_random_seed48(&r, seed),
              "set cauldron World.rand cursor");
        CHECK((gm_runtime_weather_ice_snow_at(&r, 8, 8, 1) & 4)
              && gm_world_meta(r.world, 8, 80, 8) == 2
              && r.world_random_seed48 == expected.seed,
              "rain fills cauldron and consumes exact cursor");
    }

    /* Timer rerolls and block weather share one World.rand in real WorldServer. */
    {
        uint64_t seed = UINT64_C(0x123456789abc);
        JavaRandom expected;
        jrand_set_seed48(&expected, seed);
        (void)jrand_int_bound(&expected, 168000);
        (void)jrand_int_bound(&expected, 168000);
        gm_runtime_set_weather(&r, 0, 0, 0, 0);
        CHECK(gm_runtime_set_world_random_seed48(&r, seed),
              "set timer World.rand cursor");
        memset(&idle, 0, sizeof idle);
        idle.hotbar_sel = -1;
        gm_runtime_tick(&r, idle);
        CHECK(r.world_random_seed48 == expected.seed
              && r.clock.rain_time >= 12000
              && r.clock.thunder_time >= 12000,
              "timer rerolls use and return the shared World.rand cursor");
    }

    gm_runtime_destroy(&r);
    puts("weather_world: PASS (temperature, freeze, snow, cauldron, shared RNG)");
    return 0;
}
