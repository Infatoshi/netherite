/* Snapshot v8 biome plane units + fixture baker.
 *
 * Units: v7 load defaults plains 1; v8 round-trip; Perlin TEMPERATURE_NOISE
 * vs NoiseGeneratorPerlin(Random(1234L), 1); getFloatTemperature y>64;
 * spawn lists by biome (Biome.java / BiomeSwamp.java / BiomeOcean.java).
 * --write-fixture FROM OUT fills the plane from genlayer at the snapshot
 * seed and region (never hand-edits cells). --force-plains writes id 1.
 */
#define _POSIX_C_SOURCE 200809L
#define GL_ARENA_INTS 262144
#include "blaze_snapshot.h"
#include "mc_blocks.h"
#include "mc_gamerules.h"
#include "block_props_table.h"
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcomment"
#endif
#include "genlayer_biomes.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

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
    int blight[24][24][24];
    int clight[24][24][24];
    int biome;
} FakeW;

static FakeW g_w;

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
static int fake_blight(FakeW *w, int x, int y, int z) {
    if (!fake_in(x, y, z)) return 0;
    return w->blight[x][y][z] & 15;
}
static int fake_clight(FakeW *w, int x, int y, int z) {
    if (!fake_in(x, y, z)) return 15;
    return w->clight[x][y][z] & 15;
}
static void fake_set(FakeW *w, int x, int y, int z, int id, int meta) {
    if (!fake_in(x, y, z)) return;
    w->id[x][y][z] = id;
    w->meta[x][y][z] = meta & 15;
}

#define RT_W FakeW
#define rt_live_id(w, x, y, z) fake_id((w), (x), (y), (z))
#define rt_live_meta(w, x, y, z) fake_meta((w), (x), (y), (z))
#define rt_live_light(w, x, y, z) fake_clight((w), (x), (y), (z))
#define rt_live_block_light(w, x, y, z) fake_blight((w), (x), (y), (z))
#define rt_live_biome(w, x, z) ((w)->biome)
#define rt_live_set(w, x, y, z, id, meta) fake_set((w), (x), (y), (z), (id), (meta))
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#include "randtick_live.h"
#pragma GCC diagnostic pop

typedef struct { int dummy; } HsTestW;
static int hs_test_block(const HsTestW *w, int x, int y, int z) {
    (void)w;
    (void)x;
    (void)z;
    return y == 64 ? BLK_STONE : 0;
}
#define HS_W HsTestW
#define HS_BLOCK(w, x, y, z) hs_test_block((w), (x), (y), (z))
#include "hostile_spawn.h"

static int fill_genlayer_plane(CuSnapshot *s) {
    GLNode nodes[GL_MAX_NODES];
    GlArena *arena;
    int voronoi, x, z, wx, wz, cx, cz, lx, lz;
    int *fb;
    long bvol;
    int cx0, cz0, cx1, cz1;
    if (!s || s->head.rnx <= 0 || s->head.rnz <= 0) return 0;
    bvol = (long)s->head.rnx * (long)s->head.rnz;
    if (!s->biome) {
        s->biome = (unsigned char *)malloc((size_t)bvol);
        if (!s->biome) return 0;
    }
    memset(s->biome, BLAZE_SNAP_BIOME_PLAINS, (size_t)bvol);
    arena = (GlArena *)malloc(sizeof *arena);
    if (!arena) return 0;
    gl_build(nodes, (i64)s->head.seed, &voronoi);
    /* Same tiling as magma LChunk.biome: gl_getInts(cx*16, cz*16, 16, 16)
     * copied to biome[(wx&15)+(wz&15)*16] (light.c:403-406). A single
     * 128x128 getInts is not equivalent: Voronoi neighborhood depends
     * on the query rectangle. */
    cx0 = s->head.rx0 >> 4;
    cz0 = s->head.rz0 >> 4;
    cx1 = (s->head.rx0 + s->head.rnx - 1) >> 4;
    cz1 = (s->head.rz0 + s->head.rnz - 1) >> 4;
    for (cx = cx0; cx <= cx1; ++cx) {
        for (cz = cz0; cz <= cz1; ++cz) {
            arena->off = 0;
            fb = gl_getInts(nodes, arena, voronoi, cx * 16, cz * 16, 16, 16);
            if (!fb) {
                free(arena);
                return 0;
            }
            for (lx = 0; lx < 16; ++lx)
                for (lz = 0; lz < 16; ++lz) {
                    wx = cx * 16 + lx;
                    wz = cz * 16 + lz;
                    x = wx - s->head.rx0;
                    z = wz - s->head.rz0;
                    if (x < 0 || z < 0 || x >= s->head.rnx || z >= s->head.rnz)
                        continue;
                    s->biome[(long)x * s->head.rnz + z] =
                        (unsigned char)(fb[lx + lz * 16] & 255);
                }
        }
    }
    free(arena);
    return 1;
}

static void plane_stats(const CuSnapshot *s, int *at_player, int *nswamp,
                        int *nice, int *nplains) {
    int x, z, px, pz, id;
    long i, bvol;
    *at_player = 1;
    *nswamp = *nice = *nplains = 0;
    if (!s || !s->biome) return;
    px = (int)s->head.px + s->head.ox - s->head.rx0;
    pz = (int)s->head.pz + s->head.oz - s->head.rz0;
    bvol = (long)s->head.rnx * (long)s->head.rnz;
    if (px >= 0 && pz >= 0 && px < s->head.rnx && pz < s->head.rnz)
        *at_player = (int)s->biome[(long)px * s->head.rnz + pz];
    for (i = 0; i < bvol; ++i) {
        id = (int)s->biome[i];
        if (id == B_SWAMP || id == 134) ++*nswamp;
        if (id == B_ICE_PLAINS || id == B_ICE_MOUNTAINS) ++*nice;
        if (id == B_PLAINS) ++*nplains;
    }
    (void)x;
    (void)z;
}

static int write_fixture(const char *from, const char *out_path, int plains,
                         long long set_seed) {
    CuSnapshot s;
    char err[256];
    int at_player, nswamp, nice, nplains;
    uint64_t h_live, h_plains;
    memset(&s, 0, sizeof s);
    if (!blaze_snapshot_load(from, &s, err, (int)sizeof err, 1)) {
        fprintf(stderr, "load %s: %s\n", from, err);
        return 0;
    }
    if (set_seed != -1)
        s.head.seed = set_seed;
    if (plains) {
        long bvol = (long)s.head.rnx * (long)s.head.rnz;
        if (!s.biome) {
            s.biome = (unsigned char *)malloc((size_t)bvol);
            if (!s.biome) {
                blaze_snapshot_free(&s);
                return 0;
            }
        }
        memset(s.biome, BLAZE_SNAP_BIOME_PLAINS, (size_t)bvol);
    } else if (!fill_genlayer_plane(&s)) {
        fprintf(stderr, "genlayer fill failed\n");
        blaze_snapshot_free(&s);
        return 0;
    }
    s.head.version = BLAZE_SNAP_VERSION;
    plane_stats(&s, &at_player, &nswamp, &nice, &nplains);
    h_live = bp_hash_biome_plane(bp_hash_begin(), s.biome, s.head.rnx,
                                 s.head.rnz);
    {
        long bvol = (long)s.head.rnx * (long)s.head.rnz;
        unsigned char *p = (unsigned char *)malloc((size_t)bvol);
        if (!p) {
            blaze_snapshot_free(&s);
            return 0;
        }
        memset(p, BLAZE_SNAP_BIOME_PLAINS, (size_t)bvol);
        h_plains = bp_hash_biome_plane(bp_hash_begin(), p, s.head.rnx,
                                       s.head.rnz);
        free(p);
    }
    if (!blaze_snapshot_write(out_path, &s, err, (int)sizeof err)) {
        fprintf(stderr, "write %s: %s\n", out_path, err);
        blaze_snapshot_free(&s);
        return 0;
    }
    fprintf(stderr,
            "WROTE %s v%u seed=%lld rx0=%d rz0=%d %dx%d biome_at_player=%d "
            "swamp_cols=%d ice_cols=%d plains_cols=%d "
            "digest_live=0x%llx digest_plains=0x%llx %s\n",
            out_path, s.head.version, s.head.seed, s.head.rx0, s.head.rz0,
            s.head.rnx, s.head.rnz, at_player, nswamp, nice, nplains,
            (unsigned long long)h_live, (unsigned long long)h_plains,
            h_live != h_plains ? "PLANE_LIVE" : "PLANE_EQ_PLAINS");
    blaze_snapshot_free(&s);
    return 1;
}

static int run_units(void) {
    char err[256];
    char pa[128], pb[128], pv7[128];
    CuSnapshot a, b, v7;
    CpPerlin tn;
    double perlin;
    float t80, t64, hand;
    float f;

    /* ---- spawn lists (Biome.java:142-153, BiomeSwamp.java:34) ---- */
    expect(hs_total_weight(1) == 515, "plains monster total 515");
    expect(hs_total_weight(HS_BIOME_SWAMP) == 516,
           "swamp monster total 516 (BiomeSwamp.java:34 extra slime 1)");
    expect(hs_monster_entry_count(1) == 8, "plains monster list has 8 entries");
    expect(hs_monster_entry_count(HS_BIOME_SWAMP) == 9,
           "swamp appends a second slime entry");
    expect(hs_monster_entry_type(HS_BIOME_SWAMP, 8) == HS_SLIME &&
               hs_monster_entry_weight(HS_BIOME_SWAMP, 8) == 1,
           "swamp extra slime is list index 8 weight 1 (BiomeSwamp.java:34)");
    expect(hs_weight_at_biome(HS_SLIME, HS_BIOME_SWAMP) == 100,
           "type-indexed swamp slime stays Biome.java:151 weight 100");
    expect(hs_is_snow_biome(HS_BIOME_ICE_PLAINS) &&
               hs_monster_entry_count(HS_BIOME_ICE_PLAINS) == 9,
           "ice plains monster list is 9 (BiomeSnow.java:36-49)");
    expect(hs_monster_entry_type(HS_BIOME_ICE_PLAINS, 3) == HS_CREEPER &&
               hs_monster_entry_type(HS_BIOME_ICE_PLAINS, 7) == HS_SKELETON &&
               hs_monster_entry_weight(HS_BIOME_ICE_PLAINS, 7) == 20 &&
               hs_monster_entry_type(HS_BIOME_ICE_PLAINS, 8) == HS_STRAY &&
               hs_monster_entry_weight(HS_BIOME_ICE_PLAINS, 8) == 80,
           "ice plains removes skeleton then appends skeleton 20 + stray 80");
    expect(hs_total_weight(HS_BIOME_ICE_PLAINS) == 515,
           "ice plains monster total stays 515");
    expect(!hs_is_roster(HS_STRAY), "stray is not on the live roster");
    expect(hs_biome_or_plains(0, HS_BIOME_SWAMP) == 1,
           "OOR spawn biome clips to plains 1");
    expect(hs_biome_or_plains(1, HS_BIOME_SWAMP) == HS_BIOME_SWAMP,
           "in-region swamp stays swamp");
    expect(hs_biome_or_plains(1, -1) == 1,
           "missing chunk biome is plains");
    expect(hs_creature_total_weight(1) == 40, "plains creature total 40");
    expect(hs_creature_total_weight(HS_BIOME_SWAMP) == 40,
           "swamp creature list is Biome.java default");
    expect(hs_creature_total_weight(HS_BIOME_OCEAN) == 0,
           "ocean creature list cleared (BiomeOcean.java:8)");
    expect(hs_creature_total_weight(HS_BIOME_ICE_PLAINS) == 0,
           "ice plains roster has no sheep/pig/chicken/cow (BiomeSnow.java:33)");

    /* ---- Perlin: NoiseGeneratorPerlin(new Random(1234L), 1).getValue ---- */
    rt_live_temperature_noise_init(&tn);
    perlin = cp_perlin_getValue(&tn, 1.0, 2.0);
    /* Java 8 oracle-src NoiseGeneratorPerlin.java:21-32 + Simplex.java:24-40,
     * javac 1.8 dump: getValue(1,2) = -0.23526496123584156. */
    expect(perlin == -0.23526496123584156,
           "TEMPERATURE_NOISE getValue(1,2) matches Java 8 Perlin");
    fprintf(stderr, "perlin(1,2)=%.17g\n", perlin);

    /* getFloatTemperature plains y=64: no Perlin (Biome.java:265-268). */
    t64 = rt_live_float_temperature(&g_w, 1, 0, 64, 0);
    expect(t64 == 0.8f, "plains y<=64 is base 0.8 (Biome.java:86 / :265)");

    /* y=80 at (8,16): x/8=1 z/8=2. Java dump t=0.773333 is (0,0);
     * (8,16) t=0.774901807. f=(float)(perlin*4). */
    t80 = rt_live_float_temperature(&g_w, 1, 8, 80, 16);
    f = (float)(perlin * 4.0);
    hand = 0.8f - (f + 80.0f - 64.0f) * 0.05f / 30.0f;
    expect(t80 == hand, "getFloatTemperature y=80 matches Biome.java:258-263");
    {
        union { float f; unsigned u; } bits;
        bits.f = t80;
        expect(bits.u == 1061576695u,
               "Java 8 plains (8,80,16) float bits 1061576695");
    }
    fprintf(stderr, "getFloatTemperature plains (8,80,16)=%.9g hand=%.9g\n",
            t80, hand);

    /* ice plains y=80 can drop below 0.15; plains 0.8 does not at y=80
     * unless Perlin is huge. */
    expect(rt_live_float_temperature(&g_w, 12, 0, 64, 0) == 0.0f,
           "ice plains base temp 0.0");

    /* ---- snapshot v7 load default plains; v8 round-trip ---- */
    snprintf(pv7, sizeof pv7, "/tmp/biomeplane_v7_%d.bsnp", (int)getpid());
    snprintf(pa, sizeof pa, "/tmp/biomeplane_v8a_%d.bsnp", (int)getpid());
    snprintf(pb, sizeof pb, "/tmp/biomeplane_v8b_%d.bsnp", (int)getpid());
    memset(&a, 0, sizeof a);
    a.head.magic[0] = 'B';
    a.head.magic[1] = 'S';
    a.head.magic[2] = 'N';
    a.head.magic[3] = 'P';
    a.head.version = BLAZE_SNAP_VERSION_ENDER; /* v7, no plane on disk */
    a.head.rnx = 2;
    a.head.rny = 2;
    a.head.rnz = 2;
    a.cells = (unsigned short *)calloc(8, sizeof(unsigned short));
    a.light = (unsigned char *)calloc(8, 1);
    expect(a.cells && a.light, "v7 tiny alloc");
    expect(blaze_snapshot_write(pv7, &a, err, (int)sizeof err),
           "write v7 (no biome pointer; writer omits plane)");
    /* write() of version 7 must not emit a plane. Force the header version
     * to 7 and write through blaze_snapshot_write. */
    blaze_snapshot_free(&a);
    memset(&a, 0, sizeof a);
    a.head.magic[0] = 'B';
    a.head.magic[1] = 'S';
    a.head.magic[2] = 'N';
    a.head.magic[3] = 'P';
    a.head.version = 7;
    a.head.rnx = 2;
    a.head.rny = 2;
    a.head.rnz = 2;
    a.cells = (unsigned short *)calloc(8, sizeof(unsigned short));
    a.light = (unsigned char *)calloc(8, 1);
    expect(blaze_snapshot_write(pv7, &a, err, (int)sizeof err), "write v7 file");
    memset(&v7, 0, sizeof v7);
    expect(blaze_snapshot_load(pv7, &v7, err, (int)sizeof err, 1), "load v7");
    expect(v7.biome != NULL, "v7 load allocated a plane");
    expect(v7.biome && v7.biome[0] == BLAZE_SNAP_BIOME_PLAINS &&
               v7.biome[3] == BLAZE_SNAP_BIOME_PLAINS,
           "v7 load plane is plains 1");
    blaze_snapshot_free(&a);
    blaze_snapshot_free(&v7);

    memset(&a, 0, sizeof a);
    a.head.magic[0] = 'B';
    a.head.magic[1] = 'S';
    a.head.magic[2] = 'N';
    a.head.magic[3] = 'P';
    a.head.version = BLAZE_SNAP_VERSION;
    a.head.rnx = 2;
    a.head.rny = 2;
    a.head.rnz = 3;
    a.cells = (unsigned short *)calloc(12, sizeof(unsigned short));
    a.light = (unsigned char *)calloc(12, 1);
    a.biome = (unsigned char *)malloc(2 * 3);
    expect(a.cells && a.light && a.biome, "v8 tiny alloc");
    a.biome[0] = 6;
    a.biome[1] = 6;
    a.biome[2] = 1;
    a.biome[3] = 12;
    a.biome[4] = 12;
    a.biome[5] = 1;
    expect(blaze_snapshot_write(pa, &a, err, (int)sizeof err), "write v8");
    memset(&b, 0, sizeof b);
    expect(blaze_snapshot_load(pa, &b, err, (int)sizeof err, 1), "load v8");
    expect(b.head.version == BLAZE_SNAP_VERSION, "loaded version is current");
    expect(b.biome && b.biome[0] == 6 && b.biome[3] == 12,
           "v8 swamp/ice columns round-trip");
    expect(blaze_snapshot_write(pb, &b, err, (int)sizeof err), "rewrite v8");
    blaze_snapshot_free(&a);
    blaze_snapshot_free(&b);
    unlink(pa);
    unlink(pb);
    unlink(pv7);
    return fails ? 1 : 0;
}

int main(int argc, char **argv) {
    int i;
    const char *from = NULL, *out = NULL;
    int plains = 0;
    long long set_seed = -1;
    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--write-fixture") && i + 2 < argc) {
            from = argv[++i];
            out = argv[++i];
        } else if (!strcmp(argv[i], "--force-plains")) {
            plains = 1;
        } else if (!strcmp(argv[i], "--seed") && i + 1 < argc) {
            set_seed = strtoll(argv[++i], NULL, 10);
        } else {
            fprintf(stderr,
                    "usage: %s [--write-fixture FROM.bsnp OUT.bsnp] "
                    "[--force-plains] [--seed N]\n",
                    argv[0]);
            return 2;
        }
    }
    if (from && out) {
        if (!write_fixture(from, out, plains, set_seed)) return 1;
        return 0;
    }
    return run_units();
}
