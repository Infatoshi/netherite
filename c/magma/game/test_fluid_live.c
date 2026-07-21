/* game/test_fluid_live.c - live water flow through the authoritative runtime.
 *
 * Vanilla ground truth (1.11.2 BlockDynamicLiquid): a lone source on an open
 * plane spreads a diamond of flowing water with level = distance (max 7); a
 * source column pours down through air; removing a wall next to still water
 * floods the opened cell. The region must go idle (fluids.active == 0) once
 * every front settles. Build+run: bash game/test_fluid_live.sh */
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
    cfg.view_distance = 1;
    GmRuntime r; char err[256];
    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err), "runtime initializes");
    if (fail) return 1;

    /* ---- 1. source on an open plane: diamond spread, level == distance ---- */
    const int CY = 130, CX = 8, CZ = 8;
    for (int x = CX - 10; x <= CX + 10; ++x)
        for (int z = CZ - 10; z <= CZ + 10; ++z)
            CHECK(gm_runtime_set_block(&r, x, CY, z, 1, 0), "platform stone set");

    /* perch the player on the platform corner, outside the level-7 diamond
     * (a dead player freezes gm_runtime_tick entirely) */
    gm_runtime_set_pose(&r, CX - 9.5, CY + 1.0, CZ - 9.5, 180.0f, 0.0f);
    gm_runtime_set_velocity(&r, 0, 0, 0);
    CHECK(gm_runtime_set_block(&r, CX, CY + 1, CZ, 9, 0), "water source set");
    ticks(&r, 200);   /* 7 rings x 5-tick cadence, plus settling */

    CHECK(gm_world_block(r.world, CX, CY + 1, CZ) == 9 &&
          gm_world_meta(r.world, CX, CY + 1, CZ) == 0, "source stays still water");
    /* settled flow: vanilla setStatic swaps 8->9 keeping the level meta, so a
     * settled cell is id 8 OR 9 with meta == Manhattan distance */
    for (int d = 1; d <= 7; ++d) {
        int id = gm_world_block(r.world, CX + d, CY + 1, CZ);
        int meta = gm_world_meta(r.world, CX + d, CY + 1, CZ);
        char msg[96];
        snprintf(msg, sizeof msg, "distance %d is water at level %d", d, d);
        CHECK((id == 8 || id == 9) && meta == d, msg);
    }
    CHECK(gm_world_block(r.world, CX + 8, CY + 1, CZ) == 0, "spread stops at level 7");
    CHECK(gm_world_block(r.world, CX - 4, CY + 1, CZ + 4) == 0,
          "diamond metric: |dx|+|dz|=8 stays air");
    CHECK(gm_fluid_active(&r.fluids) == 0, "region goes idle after settling");

    /* ---- 2. pour-down: source over a hole falls as level-8 columns ----
     * (basin floor right below so the column lands and can settle) */
    CHECK(gm_runtime_set_block(&r, CX + 2, CY - 1, CZ, 1, 0), "basin floor");
    CHECK(gm_runtime_set_block(&r, CX + 2, CY + 1, CZ, 0, 0), "clear a cell");
    CHECK(gm_runtime_set_block(&r, CX + 2, CY, CZ, 0, 0), "open a hole under it");
    ticks(&r, 100);
    {
        int id = gm_world_block(r.world, CX + 2, CY, CZ);
        int meta = gm_world_meta(r.world, CX + 2, CY, CZ);
        CHECK((id == 8 || id == 9) && meta >= 8,
              "water pours down the hole as a falling column");
    }

    /* ---- 3. dig next to a still pond: the opened cell floods ---- */
    const int PY = 140, PX = 8, PZ = 8;
    for (int x = PX; x <= PX + 4; ++x)
        for (int z = PZ; z <= PZ + 4; ++z) {
            CHECK(gm_runtime_set_block(&r, x, PY - 1, z, 1, 0), "pond floor");
            int rim = (x == PX || x == PX + 4 || z == PZ || z == PZ + 4);
            CHECK(gm_runtime_set_block(&r, x, PY, z, rim ? 1 : 9, 0), "pond ring/water");
        }
    ticks(&r, 60);
    CHECK(gm_fluid_active(&r.fluids) == 0, "still pond settles");
    CHECK(gm_runtime_set_block(&r, PX + 4, PY, PZ + 2, 0, 0), "dig the pond wall");
    ticks(&r, 60);
    {
        int id = gm_world_block(r.world, PX + 4, PY, PZ + 2);
        CHECK(id == 8 || id == 9, "water floods the dug-out wall cell");
    }

    /* ---- 4. overworld lava: decay 2 -> 3 rings (levels 2/4/6), 30-tick cadence ---- */
    const int LY = 150, LX = 8, LZ = 8;
    for (int x = LX - 6; x <= LX + 6; ++x)
        for (int z = LZ - 6; z <= LZ + 6; ++z)
            CHECK(gm_runtime_set_block(&r, x, LY, z, 1, 0), "lava platform stone");
    CHECK(gm_runtime_set_block(&r, LX, LY + 1, LZ, 11, 0), "lava source set");
    for (int t = 0; t < 400; ++t) { r.vitals.health = 20.0f; ticks(&r, 1); }
    for (int d = 1; d <= 3; ++d) {
        int id = gm_world_block(r.world, LX + d, LY + 1, LZ);
        int meta = gm_world_meta(r.world, LX + d, LY + 1, LZ);
        char msg[96];
        snprintf(msg, sizeof msg, "overworld lava distance %d at level %d", d, 2 * d);
        CHECK((id == 10 || id == 11) && meta == 2 * d, msg);
    }
    CHECK(gm_world_block(r.world, LX + 4, LY + 1, LZ) == 0,
          "overworld lava stops after 3 rings");

    /* ---- 5. nether lava: decay 1 -> 7 rings (level == distance), 10-tick cadence ---- */
    const int NY = 200, NX = 8, NZ = 8;
    r.dimension = -1;   /* test hook: fluid scheduler keys cadence/decay off this */
    for (int x = NX - 10; x <= NX + 10; ++x)
        for (int z = NZ - 10; z <= NZ + 10; ++z)
            CHECK(gm_runtime_set_block(&r, x, NY, z, 1, 0), "nether platform stone");
    CHECK(gm_runtime_set_block(&r, NX, NY + 1, NZ, 11, 0), "nether lava source set");
    for (int t = 0; t < 400; ++t) { r.vitals.health = 20.0f; ticks(&r, 1); }
    for (int d = 1; d <= 7; ++d) {
        int id = gm_world_block(r.world, NX + d, NY + 1, NZ);
        int meta = gm_world_meta(r.world, NX + d, NY + 1, NZ);
        char msg[96];
        snprintf(msg, sizeof msg, "nether lava distance %d at level %d", d, d);
        CHECK((id == 10 || id == 11) && meta == d, msg);
    }
    CHECK(gm_world_block(r.world, NX + 8, NY + 1, NZ) == 0,
          "nether lava stops after 7 rings");

    gm_runtime_destroy(&r);
    if (fail) { fprintf(stderr, "fluid_live: FAIL\n"); return 1; }
    fprintf(stderr, "fluid_live: PASS\n");
    return 0;
}
