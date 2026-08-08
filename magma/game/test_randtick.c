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
#include <stdlib.h>
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
    /* GmRuntime owns the fixed-capacity world/entity stores. Keep the three
     * independent fixtures out of the process stack as the product state
     * grows; their lifetime is already the whole test. */
    static GmRuntime r, a, b;
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

    /* ---- 8. registry randomTick/updateTick ownership distinctions ---- */
    {
        static const int no_op_ids[] = {
            28, 50, 70, 72, 75, 76, 77, 86, 91, 92,
            131, 132, 143, 147, 148, 171
        };
        for (unsigned i = 0; i < sizeof no_op_ids / sizeof no_op_ids[0]; ++i) {
            int id = no_op_ids[i];
            uint64_t rng_before = r.world_random_seed48;
            gm_world_set_block_meta(r.world, X + 20, Y, Z, id, 0);
            CHECK(gm_runtime_random_tick_block(
                      &r, X + 20, Y, Z, id),
                  "registry no-op random callback is accepted");
            CHECK(gm_world_block(r.world, X + 20, Y, Z) == id
                      && gm_world_meta(r.world, X + 20, Y, Z) == 0,
                  "registry no-op random callback preserves state");
            CHECK(r.world_random_seed48 == rng_before,
                  "registry no-op random callback consumes no RNG");
        }
    }

    /* ---- 9. inherited BlockBush random callback checks support ---- */
    {
        gm_world_set_block_meta(r.world, X + 30, Y - 1, Z, 3, 0);
        gm_world_set_block_meta(r.world, X + 30, Y, Z, 37, 0);
        r.world_random_seed48 = 25214903879ULL;
        CHECK(gm_runtime_random_tick_block(&r, X + 30, Y, Z, 37),
              "supported flower callback is accepted");
        CHECK(gm_world_block(r.world, X + 30, Y, Z) == 37,
              "supported flower remains");
        CHECK(r.world_random_seed48 == 25214903879ULL,
              "supported flower consumes no real-Java RNG");
        gm_world_set_block_meta(r.world, X + 30, Y - 1, Z, 0, 0);
        CHECK(gm_runtime_random_tick_block(&r, X + 30, Y, Z, 37),
              "unsupported flower callback is accepted");
        CHECK(gm_world_block(r.world, X + 30, Y, Z) == 0,
              "unsupported flower drops and becomes air");
        if (r.world_random_seed48 != 13493716152507ULL)
            fprintf(stderr, "flower RNG: got %llu expected %llu\n",
                    (unsigned long long)r.world_random_seed48,
                    (unsigned long long)13493716152507ULL);
        CHECK(r.world_random_seed48 == 13493716152507ULL,
              "unsupported flower matches the real-Java drop RNG cursor");
    }

    /* ---- 10. lit ore decay and magma-water callback payload ---- */
    {
        GmRuntimeSoundEvent sound;
        GmRuntimeParticleEvent particle;
        gm_world_set_block_meta(r.world, X + 22, Y, Z, 74, 0);
        CHECK(gm_runtime_random_tick_block(&r, X + 22, Y, Z, 74),
              "lit redstone ore callback is accepted");
        CHECK(gm_world_block(r.world, X + 22, Y, Z) == 73
                  && gm_world_meta(r.world, X + 22, Y, Z) == 0,
              "lit redstone ore decays to default unlit state");

        r.sound_event_head = r.sound_event_count = 0;
        r.particle_event_count = 0;
        /* java.util.Random.setSeed(42) stores 42 xor the multiplier. The
         * direct real-WorldServer random_tick_locked oracle leaves the cursor
         * at 15386904305625 after BlockMagma's two nextFloat calls. */
        r.world_random_seed48 = 25214903879ULL;
        gm_world_set_block_meta(r.world, X + 23, Y, Z, 213, 0);
        gm_world_set_block_meta(r.world, X + 23, Y + 1, Z, 9, 0);
        CHECK(gm_runtime_random_tick_block(&r, X + 23, Y, Z, 213),
              "magma-water callback is accepted");
        CHECK(gm_world_block(r.world, X + 23, Y + 1, Z) == 0,
              "magma removes static water above");
        CHECK(r.world_random_seed48 == 15386904305625ULL,
              "magma consumes the exact real-Java world RNG cursor");
        CHECK(gm_runtime_sound_event_count(&r) == 1
                  && gm_runtime_sound_event_get(&r, 0, &sound)
                  && sound.sound == GM_SOUND_FIRE_EXTINGUISH
                  && sound.category == GM_SOUND_CATEGORY_BLOCKS
                  && sound.volume == 0.5F,
              "magma emits the exact extinguish sound family");
        CHECK(gm_runtime_particle_event_count(&r) == 1
                  && gm_runtime_particle_event_get(&r, 0, &particle)
                  && particle.kind == GM_PARTICLE_SMOKE_LARGE
                  && particle.count == 8
                  && particle.x == (double)(X + 23) + 0.5
                  && particle.y == (double)Y + 1.25
                  && particle.z == (double)Z + 0.5
                  && particle.offset_x == 0.5
                  && particle.offset_y == 0.25
                  && particle.offset_z == 0.5,
              "magma emits the exact server particle batch descriptor");
    }

    /* ---- 11. dynamic fluid random callbacks share scheduled semantics ---- */
    {
        static const int dx[4] = {0, 0, -1, 1};
        static const int dz[4] = {-1, 1, 0, 0};
        int wx = X, wz = Z + 20;
        for (int oz = -4; oz <= 4; ++oz)
            for (int ox = -4; ox <= 4; ++ox)
                if (ox * ox + oz * oz <= 32) {
                    gm_world_set_block_meta(r.world, wx + ox, Y - 1,
                                            wz + oz, 1, 0);
                    gm_world_set_block_meta(r.world, wx + ox, Y,
                                            wz + oz, 0, 0);
                    gm_world_set_block_meta(r.world, wx + ox, Y + 1,
                                            wz + oz, 0, 0);
                    gm_world_set_block_meta(r.world, wx + ox, Y + 2,
                                            wz + oz, 0, 0);
                }
        gm_world_set_block_meta(r.world, wx, Y, wz, 8, 0);
        CHECK(gm_runtime_random_tick_block(&r, wx, Y, wz, 8),
              "dynamic water random callback is accepted");
        CHECK(gm_world_block(r.world, wx, Y, wz) == 8
                  && gm_world_meta(r.world, wx, Y, wz) == 0,
              "water source is rewoken by its spreading neighbors");
        for (int i = 0; i < 4; ++i)
            CHECK(gm_world_block(r.world, wx + dx[i], Y, wz + dz[i]) == 8
                      && gm_world_meta(
                          r.world, wx + dx[i], Y, wz + dz[i]) == 1,
                  "water random callback spreads level one in EnumSet order");

        wx = X; wz = Z + 40;
        for (int oz = -5; oz <= 5; ++oz)
            for (int ox = -5; ox <= 5; ++ox)
                if (abs(ox) + abs(oz) <= 5) {
                    gm_world_set_block_meta(r.world, wx + ox, Y - 1,
                                            wz + oz, 1, 0);
                    gm_world_set_block_meta(r.world, wx + ox, Y,
                                            wz + oz, 0, 0);
                    gm_world_set_block_meta(r.world, wx + ox, Y + 1,
                                            wz + oz, 0, 0);
                }
        gm_world_set_block_meta(r.world, wx, Y, wz, 10, 0);
        CHECK(gm_runtime_random_tick_block(&r, wx, Y, wz, 10),
              "dynamic lava random callback is accepted");
        CHECK(gm_world_block(r.world, wx, Y, wz) == 10
                  && gm_world_meta(r.world, wx, Y, wz) == 0,
              "lava source is rewoken by its spreading neighbors");
        for (int i = 0; i < 4; ++i)
            CHECK(gm_world_block(r.world, wx + dx[i], Y, wz + dz[i]) == 10
                      && gm_world_meta(
                          r.world, wx + dx[i], Y, wz + dz[i]) == 2,
                  "Overworld lava random callback spreads level two");
    }

    /* ---- 12. dynamic fluids replace non-air material in Java order ---- */
    {
        const int fx = X + 40, fy = Y + 16, fz = Z;
        /* Force EAST as the only horizontal outlet. */
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz) {
                gm_world_set_block_meta(r.world, fx + dx, fy - 1,
                                        fz + dz, 1, 0);
                gm_world_set_block_meta(r.world, fx + dx, fy,
                                        fz + dz, 1, 0);
                gm_world_set_block_meta(r.world, fx + dx, fy + 1,
                                        fz + dz, 0, 0);
            }
        gm_world_set_block_meta(r.world, fx, fy, fz, 8, 0);
        gm_world_set_block_meta(r.world, fx + 1, fy, fz, 37, 0);
        r.world_random_seed48 = 25214903879ULL;
        CHECK(gm_runtime_random_tick_block(&r, fx, fy, fz, 8),
              "water-to-flower callback is accepted");
        CHECK(gm_world_block(r.world, fx + 1, fy, fz) == 8
                  && gm_world_meta(r.world, fx + 1, fy, fz) == 1,
              "water replaces a flower with level one flow");
        CHECK(r.world_random_seed48 == 13493716152507ULL,
              "water replacement matches flower drop RNG");

        gm_world_set_block_meta(r.world, fx, fy, fz, 8, 0);
        gm_world_set_block_meta(r.world, fx + 1, fy, fz, 78, 0);
        r.world_random_seed48 = 25214903879ULL;
        CHECK(gm_runtime_random_tick_block(&r, fx, fy, fz, 8),
              "water-to-snow callback is accepted");
        CHECK(gm_world_block(r.world, fx + 1, fy, fz) == 8
                  && gm_world_meta(r.world, fx + 1, fy, fz) == 1,
              "water replaces a snow layer with level one flow");
        CHECK(r.world_random_seed48 == 25214903879ULL,
              "snow-layer water replacement consumes no drop RNG");

        gm_world_set_block_meta(r.world, fx, fy, fz, 10, 0);
        gm_world_set_block_meta(r.world, fx + 1, fy, fz, 37, 0);
        r.world_random_seed48 = 25214903879ULL;
        CHECK(gm_runtime_random_tick_block(&r, fx, fy, fz, 10),
              "lava-to-flower callback is accepted");
        CHECK(gm_world_block(r.world, fx + 1, fy, fz) == 10
                  && gm_world_meta(r.world, fx + 1, fy, fz) == 2,
              "Overworld lava replaces a flower with level two flow");
        CHECK(r.world_random_seed48 == 15386904305625ULL,
              "lava replacement matches mixing-effect RNG");

        gm_world_set_block_meta(r.world, fx, fy - 2, fz, 1, 0);
        gm_world_set_block_meta(r.world, fx, fy - 1, fz, 9, 0);
        gm_world_set_block_meta(r.world, fx, fy, fz, 10, 0);
        r.world_random_seed48 = 25214903879ULL;
        CHECK(gm_runtime_random_tick_block(&r, fx, fy, fz, 10),
              "lava-down-into-water callback is accepted");
        CHECK(gm_world_block(r.world, fx, fy - 1, fz) == 1,
              "lava downward flow replaces water with stone");
        CHECK(r.world_random_seed48 == 15386904305625ULL,
              "lava-water mixing matches real-Java effect RNG");
    }

    /* ---- 13. every newly generalized scheduled callback is admitted ---- */
    gm_runtime_destroy(&r);
    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err),
          "scheduled callback matrix runtime initializes");
    if (!fail) {
        static const int blocks[] = {
            2, 6, 9, 11, 18, 31, 32, 37, 38, 39, 40,
            53, 59, 60, 67, 73, 74, 78, 79, 80, 81, 83, 90,
            104, 105, 106, 108, 109, 110, 111, 114, 115, 123,
            127, 128, 134, 135, 136, 141, 142, 156, 161, 163,
            164, 175, 180, 203, 207, 213
        };
        gm_runtime_set_total_time(&r, 42);
        for (size_t i = 0; i < sizeof blocks / sizeof blocks[0]; ++i) {
            int x = X - 3 + (int)(i % 7);
            int z = Z - 3 + (int)(i / 7);
            gm_world_set_block_meta(r.world, x, 200, z, blocks[i], 0);
            CHECK(gm_runtime_schedule_tick(
                      &r, x, 200, z, blocks[i], 1000, 0,
                      100 + (long long)i),
                  "implemented updateTick family enters the pending queue");
        }
        CHECK(gm_runtime_scheduled_tick_count(&r)
                  == (int)(sizeof blocks / sizeof blocks[0]),
              "all generalized scheduled callback families remain queued");
    }

    /* ---- 14. scheduled dispatch uses the same Java callback body ---- */
    gm_runtime_destroy(&r);
    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err),
          "scheduled dispatch runtime initializes");
    if (!fail) {
        GmAction idle;
        memset(&idle, 0, sizeof idle);
        idle.hotbar_sel = -1;
        r.randtick_enabled = 0;
        gm_runtime_set_total_time(&r, 42);
        gm_world_set_block_meta(r.world, X, 200, Z, 74, 0);
        gm_world_set_block_meta(r.world, X + 1, 200, Z, 213, 0);
        gm_world_set_block_meta(r.world, X + 1, 201, Z, 9, 0);
        r.world_random_seed48 = 25214903879ULL;
        CHECK(gm_runtime_schedule_tick(&r, X, 200, Z, 74, 43, 0, 1)
                  && gm_runtime_schedule_tick(
                      &r, X + 1, 200, Z, 213, 43, 0, 2),
              "lit ore and magma scheduled callbacks enter exact due time");
        gm_runtime_tick(&r, idle);
        CHECK(gm_world_block(r.world, X, 200, Z) == 73,
              "scheduled lit ore decays through its Java update body");
        CHECK(gm_world_block(r.world, X + 1, 201, Z) == 0,
              "scheduled magma removes water through its Java update body");
        CHECK(r.world_random_seed48 == 15386904305625ULL,
              "scheduled callback dispatch retains Java RNG ordering");
    }

    gm_runtime_destroy(&r);
    if (fail) { fprintf(stderr, "randtick: FAIL\n"); return 1; }
    fprintf(stderr, "randtick: PASS\n");
    return 0;
}
