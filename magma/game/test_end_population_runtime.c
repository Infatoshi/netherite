#include "game/runtime.h"
#include "chunk_provider_end.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(c, m) do { if (!(c)) { \
    fprintf(stderr, "FAIL: %s\n", (m)); return 1; } } while (0)

static int count_chorus(GmWorld *world, int cx, int cz) {
    int count = 0;
    for (int x = cx * 16; x < cx * 16 + 32; ++x)
        for (int z = cz * 16; z < cz * 16 + 32; ++z)
            for (int y = 32; y < 160; ++y) {
                int id = gm_world_block(world, x, y, z);
                count += id == 199 || id == 200;
            }
    return count;
}

int main(void) {
    GmRuntime runtime;
    GmConfig config;
    EndNoise *noise;
    char error[256];
    int found_x = 0, found_z = 0, found_count = 0;
    gm_config_defaults(&config);
    config.seed = 0;
    config.view_distance = 1;
    config.mobs = 0;
    config.weather = 0;
    CHECK(gm_runtime_init(&runtime, &config, error, sizeof error), error);
    CHECK(gm_runtime_set_dimension(&runtime, 1), "create End world");
    noise = (EndNoise *)malloc(sizeof *noise);
    CHECK(noise != NULL, "End noise allocation");
    cpe_noise_init(noise, config.seed);
    for (int cx = -128; cx <= 128 && !found_count; ++cx)
        for (int cz = -128; cz <= 128 && !found_count; ++cz) {
            JavaRandom random;
            if ((long long)cx * cx + (long long)cz * cz <= 4096
                    || cpe_getIslandHeightValue(noise, cx, cz, 1, 1) <= 40.0f)
                continue;
            jrand_set(&random,
                (long long)(cx + 1) * 341873128712LL
                + (long long)(cz + 1) * 132897987541LL);
            CHECK(gm_runtime_populate_end_chunk(
                      &runtime, cx, cz, random.seed),
                  "injected-cursor population");
            found_count = count_chorus(runtime.world, cx, cz);
            if (found_count) { found_x = cx; found_z = cz; }
        }
    free(noise);
    CHECK(found_count > 0, "natural population places chorus on outer island");
    CHECK(runtime.end_gateway_count <= GM_RUNTIME_END_GATEWAYS,
          "natural gateways remain inside fixed active cap");
    printf("P %d %d %d\n", found_x, found_z, found_count);
    gm_runtime_destroy(&runtime);

    CHECK(gm_runtime_init(&runtime, &config, error, sizeof error), error);
    CHECK(gm_runtime_set_dimension(&runtime, 1), "recreate End world");
    gm_runtime_set_pose(&runtime,
        found_x * 16 + 8.5, 100.0, found_z * 16 + 8.5, 0.0f, 0.0f);
    gm_runtime_tick(&runtime, (GmAction){0});
    CHECK(runtime.end_population_count == 25,
          "first outer-End tick populates bounded 5x5 discovery region");
    {
        int count = runtime.end_population_count;
        gm_runtime_tick(&runtime, (GmAction){0});
        CHECK(runtime.end_population_count == count,
              "ordinary idle tick performs no repeated population work");
    }
    gm_runtime_destroy(&runtime);
    puts("end_population_runtime: PASS (outer terrain, chorus, discovery cache)");
    return 0;
}
