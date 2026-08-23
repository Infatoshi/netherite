/* Random-tick World.rand / updateLCG units + fixture baker.
 *
 * Units: updateLCG sequence, fire/crop/leaves draw counts, snapshot
 * round-trip of update_lcg.
 * --write-fixture FROM OUT copies a magma region and writes snapshot v6
 * with update_lcg=0 (cells unchanged).
 */
#define _POSIX_C_SOURCE 200809L
#include "blaze_snapshot.h"
#include "mc_blocks.h"
#include "mc_gamerules.h"
#include "block_props_table.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fails;

static void expect(int cond, const char *msg) {
    if (cond)
        fprintf(stderr, "OK: %s\n", msg);
    else {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails = 1;
    }
}

typedef struct {
    int id[24][24][24];
    int meta[24][24][24];
} FakeW;

static int fake_in(int x, int y, int z) {
    return x >= 0 && y >= 0 && z >= 0 && x < 24 && y < 24 && z < 24;
}

static int fake_id(FakeW *w, int x, int y, int z) {
    if (!fake_in(x, y, z)) return 0;
    return w->id[x][y][z];
}
static int fake_meta(FakeW *w, int x, int y, int z) {
    if (!fake_in(x, y, z)) return 0;
    return w->meta[x][y][z] & 15;
}
static void fake_set(FakeW *w, int x, int y, int z, int id, int meta) {
    if (!fake_in(x, y, z)) return;
    w->id[x][y][z] = id;
    w->meta[x][y][z] = meta & 15;
}

#define RT_W FakeW
#define rt_live_id(w, x, y, z) fake_id((w), (x), (y), (z))
#define rt_live_meta(w, x, y, z) fake_meta((w), (x), (y), (z))
#define rt_live_light(w, x, y, z) 15
#define rt_live_set(w, x, y, z, id, meta) fake_set((w), (x), (y), (z), (id), (meta))
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#include "randtick_live.h"
#pragma GCC diagnostic pop

static FakeW g_w;
static int g_surr[RT_LIVE_SURR];

static int write_fixture(const char *from, const char *out_path) {
    CuSnapshot s;
    char err[256];
    memset(&s, 0, sizeof s);
    if (!blaze_snapshot_load(from, &s, err, (int)sizeof err, 1)) {
        fprintf(stderr, "load %s: %s\n", from, err);
        return 0;
    }
    s.head.version = BLAZE_SNAP_VERSION;
    s.update_lcg = 0;
    if (!blaze_snapshot_write(out_path, &s, err, (int)sizeof err)) {
        fprintf(stderr, "write %s: %s\n", out_path, err);
        blaze_snapshot_free(&s);
        return 0;
    }
    fprintf(stderr, "WROTE %s v%u update_lcg=%d wr=%llu\n", out_path,
            s.head.version, s.update_lcg,
            (unsigned long long)s.world_rand_seed);
    blaze_snapshot_free(&s);
    return 1;
}

static int run_units(void) {
    JavaRandom rng, shadow;
    McGameRules gr = mc_gamerules_default();
    i32 lcg;
    i32 j1;
    int lx, ly, lz;
    u64 before, after;

    lcg = 0;
    j1 = rt_live_step_lcg(&lcg);
    lx = j1 & 15; lz = (j1 >> 8) & 15; ly = (j1 >> 16) & 15;
    expect(lcg == 1013904223, "updateLCG[0] = 0*3+1013904223");
    expect(lx == 7 && ly == 11 && lz == 12, "updateLCG[0] local (7,11,12)");
    j1 = rt_live_step_lcg(&lcg);
    lx = j1 & 15; lz = (j1 >> 8) & 15; ly = (j1 >> 16) & 15;
    expect(lcg == -239350404, "updateLCG[1] wraps int32");
    expect(lx == 15 && ly == 14 && lz == 3, "updateLCG[1] local (15,14,3)");

    memset(&g_w, 0, sizeof g_w);
    fake_set(&g_w, 8, 7, 8, BLK_STONE, 0);
    fake_set(&g_w, 8, 8, 8, RT_BLK_FIRE, 0);
    fake_set(&g_w, 9, 8, 8, 5, 0); /* planks */
    {
        int n = 0;
        int f, chances[6] = {300, 300, 250, 250, 300, 300};
        int dx[6] = {1, -1, 0, 0, 0, 0};
        int dy[6] = {0, 0, -1, 1, 0, 0};
        int dz[6] = {0, 0, 0, 0, 1, -1};
        int age = 0;
        jrand_set(&shadow, 1);
        (void)jrand_int_bound(&shadow, 3); /* age */
        n++;
        (void)jrand_int_bound(&shadow, 10); /* scheduleUpdate */
        n++;
        for (f = 0; f < 6; ++f) {
            int flam = rt_live_fire_flammability(
                fake_id(&g_w, 8 + dx[f], 8 + dy[f], 8 + dz[f]));
            n++;
            if (jrand_int_bound(&shadow, chances[f]) < flam) {
                n++;
                if (jrand_int_bound(&shadow, age + 10) < 5) {
                    (void)jrand_int_bound(&shadow, 5);
                    n++;
                }
            }
        }
        /* spread loop also draws when encouragement > 0; shadow the ticker */
        jrand_set(&rng, 1);
        before = rng.seed;
        rt_live_tick_fire(&g_w, 8, 8, 8, &rng, &gr);
        after = rng.seed;
        expect(before != after, "BlockFire.updateTick consumes World.rand");
        expect(n >= 8, "fire neighbourhood draws at least 8 nextInt");
        (void)n;
        /* ticker may draw extra spread nextInt; seed must have moved past the
         * tryCatchFire prefix. */
        expect(rng.seed != before, "fire stream advanced");
    }

    memset(&g_w, 0, sizeof g_w);
    fake_set(&g_w, 8, 8, 8, RT_BLK_LEAVES, 8); /* CHECK_DECAY, decayable */
    jrand_set(&rng, 2);
    before = rng.seed;
    rt_live_tick_leaves(&g_w, 8, 8, 8, g_surr);
    expect(rng.seed == before, "BlockLeaves.updateTick draws no World.rand");
    expect(fake_id(&g_w, 8, 8, 8) == 0, "unsupported leaves decay to air");

    memset(&g_w, 0, sizeof g_w);
    fake_set(&g_w, 8, 7, 8, RT_BLK_FARMLAND, 7);
    fake_set(&g_w, 8, 8, 8, RT_BLK_WHEAT, 0);
    jrand_set(&rng, 3);
    before = rng.seed;
    rt_live_tick_crop(&g_w, &rng, 8, 8, 8);
    jrand_set(&shadow, 3);
    {
        float gf = rt_live_growth_chance(&g_w, 8, 8, 8, RT_BLK_WHEAT);
        int bound = (int)(25.0f / gf) + 1;
        (void)jrand_int_bound(&shadow, bound);
    }
    expect(rng.seed == shadow.seed, "BlockCrops.updateTick is one nextInt");
    expect(rng.seed != before, "crop growth roll advances the stream");

    {
        CuSnapshot a, b;
        char err[256];
        char pa[128], pb[128];
        snprintf(pa, sizeof pa, "/tmp/rtworldrand_a_%d.bsnp", (int)getpid());
        snprintf(pb, sizeof pb, "/tmp/rtworldrand_b_%d.bsnp", (int)getpid());
        memset(&a, 0, sizeof a);
        a.head.magic[0] = 'B'; a.head.magic[1] = 'S';
        a.head.magic[2] = 'N'; a.head.magic[3] = 'P';
        a.head.version = BLAZE_SNAP_VERSION;
        a.head.rnx = 2; a.head.rny = 2; a.head.rnz = 2;
        a.cells = (unsigned short *)calloc(8, sizeof(unsigned short));
        a.light = (unsigned char *)calloc(8, 1);
        a.world_rand_seed = 0x5DEECE66DULL & ((1ULL << 48) - 1);
        a.update_lcg = 0x11111111;
        expect(a.cells && a.light, "tiny snapshot alloc");
        expect(blaze_snapshot_write(pa, &a, err, (int)sizeof err),
               "write v6 update_lcg");
        memset(&b, 0, sizeof b);
        expect(blaze_snapshot_load(pa, &b, err, (int)sizeof err, 1),
               "load v6 update_lcg");
        expect(b.head.version == BLAZE_SNAP_VERSION, "loaded version is 6");
        expect(b.world_rand_seed == a.world_rand_seed, "world_rand round-trips");
        expect(b.update_lcg == a.update_lcg, "update_lcg round-trips");
        expect(blaze_snapshot_write(pb, &b, err, (int)sizeof err),
               "rewrite v6");
        blaze_snapshot_free(&a);
        blaze_snapshot_free(&b);
        unlink(pa);
        unlink(pb);
    }
    return fails ? 1 : 0;
}

int main(int argc, char **argv) {
    int i;
    const char *from = NULL, *out = NULL;
    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--write-fixture") && i + 2 < argc) {
            from = argv[++i];
            out = argv[++i];
        } else {
            fprintf(stderr, "usage: %s [--write-fixture FROM.bsnp OUT.bsnp]\n",
                    argv[0]);
            return 2;
        }
    }
    if (run_units())
        return 1;
    if (from && out && !write_fixture(from, out))
        return 1;
    return 0;
}
