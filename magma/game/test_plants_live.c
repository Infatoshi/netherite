/* game/test_plants_live.c - plant support rules in the live world.
 *
 * 1. Worldgen never leaves an unsupported plant (the gen_chunk support sweep;
 *    real MC never generates one, so the sweep converges toward the oracle).
 *    Mushrooms are exempt: vanilla lets them sit on any solid block.
 * 2. Digging the block under a plant breaks the plant (BlockBush
 *    checkAndDropBlock via neighborChanged), cascading up reed columns and
 *    dropping the plant item. Build+run: bash game/test_plants_live.sh */
#include "game/runtime.h"

#include <stdio.h>

static int fail;
#define CHECK(C, M) do { if (!(C)) { fprintf(stderr, "FAIL: %s\n", M); fail = 1; } } while (0)

static void ticks(GmRuntime *r, int n)
{
    GmAction a; memset(&a, 0, sizeof a); a.hotbar_sel = -1;
    for (int i = 0; i < n; ++i) gm_runtime_tick(r, a);
}

int main(void)
{
    GmConfig cfg; gm_config_defaults(&cfg);
    cfg.view_distance = 2;
    GmRuntime r; char err[256];
    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err), "runtime initializes");
    if (fail) return 1;

    /* ---- 1. generated world has no unsupported plants (mushrooms exempt) ---- */
    gm_world_ensure(r.world, 0, 0, 2);
    int floating = 0;
    for (int x = -32; x < 48; ++x)
        for (int z = -32; z < 48; ++z)
            for (int y = 1; y < 128; ++y) {
                int id = gm_world_block(r.world, x, y, z);
                int meta = gm_world_meta(r.world, x, y, z);
                int below = gm_world_block(r.world, x, y - 1, z);
                int ok;
                switch (id) {
                case 6: case 31: case 37: case 38:
                    ok = below == 2 || below == 3 || below == 60; break;
                case 32:
                    ok = below == 3 || below == 12 || below == 159 || below == 172; break;
                case 83:
                    ok = below == 83 || below == 2 || below == 3 || below == 12; break;
                case 175:
                    ok = meta >= 8 ? below == 175 : (below == 2 || below == 3); break;
                case 111:
                    ok = below == 8 || below == 9; break;
                default: continue;
                }
                if (!ok) ++floating;
            }
    CHECK(floating == 0, "no unsupported plants in the generated world");

    /* ---- 2. digging under plants breaks them (reeds cascade + drop) ---- */
    const int X = 8, Z = 8, Y = 130;
    CHECK(gm_runtime_set_block(&r, X, Y, Z, 1, 0), "perch stone");
    gm_runtime_set_pose(&r, X + 0.5, Y + 1.0, Z + 0.5, 180.0f, 0.0f);
    gm_runtime_set_velocity(&r, 0, 0, 0);

    CHECK(gm_runtime_set_block(&r, X + 2, Y, Z, 3, 0), "dirt base");
    CHECK(gm_runtime_set_block(&r, X + 2, Y + 1, Z, 31, 1), "tallgrass on dirt");
    CHECK(gm_runtime_set_block(&r, X + 3, Y, Z, 12, 0), "sand base");
    CHECK(gm_runtime_set_block(&r, X + 4, Y, Z, 9, 0), "water beside reeds");
    CHECK(gm_runtime_set_block(&r, X + 3, Y + 1, Z, 83, 0), "reed 1");
    CHECK(gm_runtime_set_block(&r, X + 3, Y + 2, Z, 83, 0), "reed 2");
    CHECK(gm_runtime_set_block(&r, X + 3, Y + 3, Z, 83, 0), "reed 3");
    ticks(&r, 5);
    CHECK(gm_world_block(r.world, X + 2, Y + 1, Z) == 31, "tallgrass stays supported");

    /* every world edit (player dig or set_block) runs the support hook, like
     * vanilla neighborChanged on setBlockState */
    CHECK(gm_runtime_set_block(&r, X + 2, Y, Z, 0, 0), "dig dirt under tallgrass");
    CHECK(gm_world_block(r.world, X + 2, Y + 1, Z) == 0, "tallgrass breaks");
    CHECK(gm_runtime_set_block(&r, X + 3, Y, Z, 0, 0), "dig sand under reeds");
    CHECK(gm_world_block(r.world, X + 3, Y + 1, Z) == 0, "reed 1 breaks");
    CHECK(gm_world_block(r.world, X + 3, Y + 2, Z) == 0, "reed column cascades");
    CHECK(gm_world_block(r.world, X + 3, Y + 3, Z) == 0, "reed top breaks too");

    gm_runtime_destroy(&r);
    if (fail) { fprintf(stderr, "plants_live: FAIL\n"); return 1; }
    fprintf(stderr, "plants_live: PASS\n");
    return 0;
}
