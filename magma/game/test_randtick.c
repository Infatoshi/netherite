/* game/test_randtick.c - deterministic random-tick behaviors (live path).
 *
 * 1. grass dies to dirt under opaque / light < 4
 * 2. grass spreads to adjacent lit dirt
 * 3. unsupported leaves (CHECK_DECAY + DECAYABLE, no log) decay to air
 * 4. fire on wood spreads / consumes fuel (doFireTick on); doFireTick off is no-op
 * 5. randomTickSpeed 0 disables the pass
 * 6. seed-deterministic pass outcomes
 *
 * Build+run: bash game/test_randtick.sh
 */
#include "game/runtime.h"
#include "game/randtick.h"
#include "mc_gamerules.h"

#include <stdio.h>
#include <string.h>

static int fail;
#define CHECK(C, M) do { if (!(C)) { fprintf(stderr, "FAIL: %s\n", M); fail = 1; } } while (0)

static void force_ticks(GmWorld *w, long long seed, int x, int y, int z,
                        int n, const McGameRules *gr) {
    int t;
    for (t = 0; t < n; ++t)
        gm_randtick_block(w, x, y, z, seed, (long long)t, gr);
}

int main(void)
{
    GmConfig cfg;
    GmRuntime r;
    char err[256];
    McGameRules gr = mc_gamerules_default();
    const long long SEED = 42;
    const int X = 8, Z = 8, Y = 64;

    gm_config_defaults(&cfg);
    cfg.seed = SEED;
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.view_distance = 2;
    cfg.mobs = 0;
    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err), "runtime initializes");
    if (fail) return 1;
    CHECK(r.randtick_enabled == 1, "randtick enabled by default on live runtime");
    CHECK(r.gamerules.randomTickSpeed == 3, "default randomTickSpeed is 3");
    CHECK(r.gamerules.doFireTick == 1, "default doFireTick is 1");

    gm_world_ensure(r.world, 0, 0, 2);

    /* ---- 1. grass dies under stone ---- */
    {
        CHECK(gm_runtime_set_block(&r, X, Y, Z, 2, 0), "place grass");
        CHECK(gm_runtime_set_block(&r, X, Y + 1, Z, 1, 0), "stone over grass");
        force_ticks(r.world, SEED, X, Y, Z, 1, &gr);
        CHECK(gm_world_block(r.world, X, Y, Z) == 3, "grass dies to dirt under stone");
    }

    /* ---- 2. grass spreads to lit dirt ---- */
    {
        int t, became = 0;
        /* open sky grass next to open dirt */
        CHECK(gm_runtime_set_block(&r, X + 2, Y, Z, 2, 0), "source grass");
        CHECK(gm_runtime_set_block(&r, X + 2, Y + 1, Z, 0, 0), "air above grass");
        CHECK(gm_runtime_set_block(&r, X + 3, Y, Z, 3, 0), "target dirt");
        CHECK(gm_runtime_set_block(&r, X + 3, Y + 1, Z, 0, 0), "air above dirt");
        /* force many ticks so at least one of the 4 spread attempts hits X+3 */
        for (t = 0; t < 200 && !became; ++t) {
            gm_randtick_block(r.world, X + 2, Y, Z, SEED, (long long)t, &gr);
            if (gm_world_block(r.world, X + 3, Y, Z) == 2) became = 1;
        }
        CHECK(became, "grass spreads to adjacent lit dirt within 200 forced ticks");
        CHECK(gm_world_block(r.world, X + 2, Y, Z) == 2, "source grass remains");
    }

    /* ---- 3. unsupported leaves decay ---- */
    {
        /* meta: variant 0, DECAYABLE (no bit4), CHECK_DECAY (bit8) => meta 8 */
        int meta = 8;
        CHECK(gm_runtime_set_block(&r, X + 5, Y + 2, Z, 18, meta), "place checking leaves");
        /* clear any nearby logs that worldgen might have left in superflat (none) */
        gm_randtick_block(r.world, X + 5, Y + 2, Z, SEED, 0, &gr);
        CHECK(gm_world_block(r.world, X + 5, Y + 2, Z) == 0,
              "unsupported leaves decay to air (no sapling drop path)");
    }

    /* ---- 3b. leaves attached to log clear CHECK_DECAY, stay ---- */
    {
        int meta = 8;
        CHECK(gm_runtime_set_block(&r, X + 6, Y + 2, Z, 17, 0), "log support");
        CHECK(gm_runtime_set_block(&r, X + 7, Y + 2, Z, 18, meta), "leaves next to log");
        gm_randtick_block(r.world, X + 7, Y + 2, Z, SEED, 1, &gr);
        CHECK(gm_world_block(r.world, X + 7, Y + 2, Z) == 18, "supported leaves remain");
        CHECK((gm_world_meta(r.world, X + 7, Y + 2, Z) & 8) == 0,
              "supported leaves clear CHECK_DECAY");
    }

    /* ---- 4. fire spreads to planks and ages/burns ---- */
    {
        int t, saw_spread = 0, saw_consume = 0;
        /* planks at (X+10,Y,Z) and (X+10,Y,Z+1); fire on top of stone base */
        CHECK(gm_runtime_set_block(&r, X + 10, Y - 1, Z, 1, 0), "stone base under fire");
        CHECK(gm_runtime_set_block(&r, X + 10, Y, Z, 51, 0), "fire age 0");
        CHECK(gm_runtime_set_block(&r, X + 10, Y, Z + 1, 5, 0), "planks neighbor");
        CHECK(gm_runtime_set_block(&r, X + 11, Y, Z, 5, 0), "planks neighbor 2");

        for (t = 0; t < 80; ++t) {
            gm_randtick_block(r.world, X + 10, Y, Z, SEED, (long long)t, &gr);
            /* fire may move onto the planks cell or age on the origin */
            if (gm_world_block(r.world, X + 10, Y, Z + 1) == 51 ||
                gm_world_block(r.world, X + 11, Y, Z) == 51)
                saw_spread = 1;
            if (gm_world_block(r.world, X + 10, Y, Z + 1) == 0 ||
                gm_world_block(r.world, X + 11, Y, Z) == 0)
                saw_consume = 1;
            /* re-place fire if extinguished so later ticks still exercise tables */
            if (gm_world_block(r.world, X + 10, Y, Z) != 51 &&
                gm_world_block(r.world, X + 10, Y, Z + 1) != 51 &&
                gm_world_block(r.world, X + 11, Y, Z) != 51) {
                gm_runtime_set_block(&r, X + 10, Y, Z, 51, 0);
                if (gm_world_block(r.world, X + 10, Y, Z + 1) == 0)
                    gm_runtime_set_block(&r, X + 10, Y, Z + 1, 5, 0);
                if (gm_world_block(r.world, X + 11, Y, Z) == 0)
                    gm_runtime_set_block(&r, X + 11, Y, Z, 5, 0);
            }
        }
        CHECK(saw_spread || saw_consume,
              "fire spreads onto flammable neighbors or consumes them");

        /* doFireTick=0: fire stays put, no spread */
        {
            McGameRules off = gr;
            int p0, p1, f0;
            off.doFireTick = 0;
            gm_runtime_set_block(&r, X + 12, Y - 1, Z, 1, 0);
            gm_runtime_set_block(&r, X + 12, Y, Z, 51, 0);
            gm_runtime_set_block(&r, X + 12, Y, Z + 1, 5, 0);
            f0 = gm_world_block(r.world, X + 12, Y, Z);
            p0 = gm_world_block(r.world, X + 12, Y, Z + 1);
            force_ticks(r.world, SEED, X + 12, Y, Z, 20, &off);
            p1 = gm_world_block(r.world, X + 12, Y, Z + 1);
            CHECK(f0 == 51 && gm_world_block(r.world, X + 12, Y, Z) == 51,
                  "doFireTick=0 leaves fire in place");
            CHECK(p0 == 5 && p1 == 5, "doFireTick=0 does not burn neighbor planks");
        }
    }

    /* ---- 5. randomTickSpeed 0: pass is no-op ---- */
    {
        McGameRules zero = gr;
        int before, after;
        zero.randomTickSpeed = 0;
        gm_runtime_set_block(&r, X, Y + 5, Z, 2, 0);
        gm_runtime_set_block(&r, X, Y + 6, Z, 1, 0);
        before = gm_world_block(r.world, X, Y + 5, Z);
        gm_randtick_pass(r.world, SEED, 0, 0, 0, 2, &zero);
        after = gm_world_block(r.world, X, Y + 5, Z);
        CHECK(before == 2 && after == 2, "randomTickSpeed 0 leaves grass alone");
        /* with default speed, forced block still dies */
        gm_randtick_block(r.world, X, Y + 5, Z, SEED, 0, &gr);
        CHECK(gm_world_block(r.world, X, Y + 5, Z) == 3,
              "direct block tick still works when pass is gated by speed");
    }

    /* ---- 6. pass is seed-deterministic ---- */
    {
        GmRuntime a, b;
        char e2[256];
        int wx, wy, wz, mismatch = 0;
        GmConfig c2 = cfg;
        c2.seed = 7;
        CHECK(gm_runtime_init(&a, &c2, e2, sizeof e2), "det runtime A");
        CHECK(gm_runtime_init(&b, &c2, e2, sizeof e2), "det runtime B");
        gm_world_ensure(a.world, 0, 0, 1);
        gm_world_ensure(b.world, 0, 0, 1);
        /* plant a small grass/dirt/fire fixture in both worlds identically */
        for (wx = 4; wx <= 6; ++wx)
            for (wz = 4; wz <= 6; ++wz) {
                gm_runtime_set_block(&a, wx, 4, wz, 2, 0);
                gm_runtime_set_block(&b, wx, 4, wz, 2, 0);
                gm_runtime_set_block(&a, wx, 5, wz, 0, 0);
                gm_runtime_set_block(&b, wx, 5, wz, 0, 0);
            }
        gm_runtime_set_block(&a, 5, 4, 7, 3, 0);
        gm_runtime_set_block(&b, 5, 4, 7, 3, 0);
        gm_runtime_set_block(&a, 5, 5, 7, 0, 0);
        gm_runtime_set_block(&b, 5, 5, 7, 0, 0);
        for (wy = 0; wy < 40; ++wy) {
            gm_randtick_pass(a.world, 7, wy, 0, 0, 1, &gr);
            gm_randtick_pass(b.world, 7, wy, 0, 0, 1, &gr);
        }
        for (wx = 0; wx < 16 && !mismatch; ++wx)
            for (wz = 0; wz < 16 && !mismatch; ++wz)
                for (wy = 0; wy < 16 && !mismatch; ++wy) {
                    if (gm_world_block(a.world, wx, wy, wz) !=
                        gm_world_block(b.world, wx, wy, wz))
                        mismatch = 1;
                    if (gm_world_meta(a.world, wx, wy, wz) !=
                        gm_world_meta(b.world, wx, wy, wz))
                        mismatch = 1;
                }
        CHECK(!mismatch, "identical seeds yield identical pass results");
        gm_runtime_destroy(&a);
        gm_runtime_destroy(&b);
    }

    /* ---- 7. carrots share BlockCrops growth (optional smoke) ---- */
    {
        int t, age0, age1;
        CHECK(gm_runtime_set_block(&r, X + 14, Y - 1, Z, 60, 7), "farmland moist");
        CHECK(gm_runtime_set_block(&r, X + 14, Y, Z, 141, 0), "carrot age 0");
        age0 = gm_world_meta(r.world, X + 14, Y, Z) & 15;
        for (t = 0; t < 500; ++t)
            gm_randtick_block(r.world, X + 14, Y, Z, SEED, (long long)t, &gr);
        age1 = gm_world_meta(r.world, X + 14, Y, Z) & 15;
        CHECK(gm_world_block(r.world, X + 14, Y, Z) == 141, "carrot still present");
        CHECK(age1 > age0, "carrot ages under forced BlockCrops rolls");
    }

    gm_runtime_destroy(&r);
    if (fail) { fprintf(stderr, "randtick: FAIL\n"); return 1; }
    fprintf(stderr, "randtick: PASS\n");
    return 0;
}
