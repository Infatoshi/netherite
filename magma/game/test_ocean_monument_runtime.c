#include "game/runtime.h"
#include "game/ocean_monument_live.h"

#include <stdio.h>

#define CHECK(c,m) do { if (!(c)) { \
    fprintf(stderr, "FAIL: %s\n", m); return 1; \
} } while (0)

int main(void) {
    GmRuntime runtime;
    GmConfig cfg;
    GmAction action = {0};
    char err[256];
    int chunk_x = 0, chunk_z = 0, found = 0;
    int prismarine = 0, lanterns = 0, gold = 0, sponge = 0;
    int elders = 0;

    gm_config_defaults(&cfg);
    cfg.seed = 0;
    cfg.world = GM_WORLD_DEFAULT;
    cfg.view_distance = 1;
    cfg.villages = 1;
    cfg.mobs = 1;
    cfg.weather = 0;
    CHECK(gm_runtime_init(&runtime, &cfg, err, sizeof err), err);

    for (int rz = -12; rz <= 12 && !found; ++rz)
        for (int rx = -12; rx <= 12 && !found; ++rx) {
            gm_monument_candidate_for_region(
                cfg.seed, rx, rz, &chunk_x, &chunk_z);
            found = gm_runtime_monument_candidate(
                &runtime, chunk_x, chunk_z);
        }
    CHECK(found, "find a Java-valid deep-ocean monument candidate");

    gm_runtime_set_pose(
        &runtime, chunk_x * 16 + 8.5, 63.0, chunk_z * 16 + 8.5,
        0.0F, 0.0F);
    gm_runtime_tick(&runtime, action);
    CHECK(runtime.monument_count == 1,
          "streaming discovers and retains the valid monument start");

    int origin_x = chunk_x * 16 + 8 - 29;
    int origin_z = chunk_z * 16 + 8 - 29;
    for (int z = origin_z - 5; z <= origin_z + 62; ++z)
        for (int x = origin_x - 5; x <= origin_x + 62; ++x)
            for (int y = 2; y <= 64; ++y) {
                int id = gm_world_block(runtime.world, x, y, z);
                prismarine += id == 168;
                lanterns += id == 169;
                gold += id == 41;
                sponge += id == 19 &&
                    gm_world_meta(runtime.world, x, y, z) == 1;
            }
    CHECK(prismarine > 12000 && lanterns > 100 && gold == 8,
          "live world contains the complete shell, lamps, and treasure core");

    {
        const EwStore *store = runtime.mobs.current
            ? &runtime.mobs.b : &runtime.mobs.a;
        for (int i = 1; i < EW_MAX_ENTITIES; ++i)
            if (store->type[i] == EW_TYPE_ELDER_GUARDIAN) {
                /* One wing site can intersect an opaque corner at elder
                 * scale. Java's first living tick likewise applies 1.0
                 * IN_WALL damage there; the other sites remain at 80. */
                CHECK((store->health[i] == 79.0F
                           || store->health[i] == 80.0F)
                          && runtime.mobs.persistence_required[i],
                      "elder keeps exact first-tick health and persistence");
                ++elders;
            }
    }
    CHECK(elders == 3, "all three structure elders materialize exactly once");
    CHECK(gm_runtime_generate_monument(&runtime, chunk_x, chunk_z) == 1,
          "repeat generation recognizes the retained structure start");
    CHECK(runtime.monument_count == 1,
          "repeat generation does not duplicate the structure");
    {
        const EwStore *store = runtime.mobs.current
            ? &runtime.mobs.b : &runtime.mobs.a;
        int repeated_elders = 0;
        for (int i = 1; i < EW_MAX_ENTITIES; ++i)
            repeated_elders += store->type[i] == EW_TYPE_ELDER_GUARDIAN;
        CHECK(repeated_elders == 3,
              "repeat generation does not duplicate elder residents");
    }

    gm_runtime_destroy(&runtime);
    printf("ocean_monument_runtime: PASS (%d,%d; %d prismarine, "
           "%d lanterns, %d gold, %d sponge, %d elders)\n",
           chunk_x, chunk_z, prismarine, lanterns, gold, sponge, elders);
    return 0;
}
