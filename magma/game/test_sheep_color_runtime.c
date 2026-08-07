#include "game/runtime.h"

#include <stdio.h>
#include <string.h>

static int fail;
#define CHECK(C, M) do { \
    if (!(C)) { fprintf(stderr, "FAIL: %s\n", M); fail = 1; } \
} while (0)

int main(void) {
    static const struct {
        uint64_t seed48;
        int color;
        uint64_t final_seed48;
    } cases[] = {
        {UINT64_C(0), 15, UINT64_C(11)},
        {UINT64_C(31), 7, UINT64_C(781662021438)},
        {UINT64_C(19), 8, UINT64_C(479083174434)},
        {UINT64_C(15), 12, UINT64_C(378223558766)},
        {UINT64_C(1), 0, UINT64_C(206026503483683)},
        {UINT64_C(55), 6, UINT64_C(57480970249033)},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        JavaRandom random;
        jrand_set_seed48(&random, cases[i].seed48);
        CHECK(gm_mobs_random_sheep_color(&random) == cases[i].color
                  && random.seed == cases[i].final_seed48,
              "selector returns exact color and World.rand cursor");
    }

    GmMobLive mobs;
    JavaRandom random;
    GmEntityView view;
    gm_mobs_init(&mobs, 0);
    CHECK(gm_mobs_spawn_exact(
              &mobs, EW_TYPE_SHEEP, 660100, 0.5, 64.0, 0.5,
              0.0, 0.0, 0.0, 0.0F, 8.0F, 1, 0, 0, 0) >= 0
              && gm_mobs_set_sheep_state(&mobs, 660100, 14, 1),
          "colored sheared sheep fixture initializes");
    jrand_set_seed48(&random, 55);
    CHECK(gm_mobs_sheep_on_initial_spawn(&mobs, 660100, &random)
              && random.seed == UINT64_C(57480970249033)
              && gm_mobs_fill_views(&mobs, &view, 1) == 1
              && view.fleece_color == 6 && view.sheared,
          "onInitialSpawn writes pink low nibble and preserves sheared bit");

    gm_mobs_init(&mobs, 0);
    jrand_set_seed48(&random, 0);
    CHECK(!gm_mobs_sheep_on_initial_spawn(&mobs, 660100, &random)
              && random.seed == 0,
          "missing sheep rejects without consuming World.rand");

    GmConfig cfg;
    GmRuntime runtime;
    char err[256];
    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.view_distance = 1;
    cfg.mobs = 0;
    CHECK(gm_runtime_init(&runtime, &cfg, err, sizeof err),
          "natural sheep-color runtime initializes");
    if (!fail) {
        int found_sheep = 0;
        for (int spawn_seed = 0; spawn_seed < 64 && !found_sheep;
                ++spawn_seed) {
            gm_mobs_init(&runtime.mobs, spawn_seed);
            runtime.mobs.active_dimension = 0;
            runtime.mobs.tick = 200;
            memset(&runtime.entities, 0, sizeof runtime.entities);
            uint64_t world_seed48 = 55;
            gm_mobs_tick(
                &runtime.mobs, runtime.world,
                (const struct Chunk *)runtime.window,
                (const struct McSinTable *)&runtime.sin_table,
                (struct PsvPlayer *)&runtime.player,
                (struct PvStats *)&runtime.vitals,
                runtime.ox, runtime.oz, 0, 6000, 1,
                &world_seed48, &runtime.math_random_seed48,
                &runtime.next_entity_id, runtime.do_mob_loot,
                &runtime.entities, 0.0F, 0.0F);
            int count = gm_mobs_fill_views(
                &runtime.mobs, &view, 1);
            if (count == 1 && view.type == GM_MOB_SHEEP) {
                found_sheep = 1;
                CHECK(view.fleece_color == 6 && !view.sheared
                          && world_seed48 == UINT64_C(57480970249033),
                      "ordinary passive sheep spawn consumes World.rand and becomes pink");
            } else {
                CHECK(world_seed48 == 55,
                      "non-sheep passive spawn does not consume the color cursor");
            }
        }
        CHECK(found_sheep,
              "bounded ordinary passive-spawn trials include a sheep");
    }
    gm_runtime_destroy(&runtime);
    if (fail) return 1;
    puts("PASS sheep color: six branches, exact cursor, natural spawn, state preservation");
    return 0;
}
