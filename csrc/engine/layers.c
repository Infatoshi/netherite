/* Minecraft 1.7.10 GenLayer stack. Each function mirrors one Java class in
 * oracle/src/world/gen/layer; variable roles follow the Java so the two can be
 * read side by side. */
#include "layers.h"
#include "biomes.h"

#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- lcg */

static int64_t lcg(int64_t s, int64_t add)
{
    /* Java: s *= s * 6364136223846793005L + 1442695040888963407L; s += add;
     * done unsigned because signed overflow is undefined in C */
    uint64_t u = (uint64_t)s;
    u = u * (u * 6364136223846793005ULL + 1442695040888963407ULL);
    return (int64_t)(u + (uint64_t)add);
}

struct rng { int64_t cs, ws; };

static void cseed(struct rng *r, const struct layer *l, int64_t x, int64_t z)
{
    r->ws = l->world_seed;
    r->cs = l->world_seed;
    r->cs = lcg(r->cs, x);
    r->cs = lcg(r->cs, z);
    r->cs = lcg(r->cs, x);
    r->cs = lcg(r->cs, z);
}

static int next_int(struct rng *r, int n)
{
    int v = (int)((r->cs >> 24) % (int64_t)n);   /* arithmetic shift, like Java >> */
    if (v < 0) v += n;
    r->cs = lcg(r->cs, r->ws);
    return v;
}

static int pick2(struct rng *r, int a, int b)
{
    return next_int(r, 2) == 0 ? a : b;
}

static int pick4(struct rng *r, int a, int b, int c, int d)
{
    int v[4] = {a, b, c, d};
    return v[next_int(r, 4)];
}

/* GenLayer.func_151617_b: the mode of four, or a random one. */
static int mode4(struct rng *r, int a, int b, int c, int d)
{
    return b == c && c == d ? b
        : a == b && a == c ? a
        : a == b && a == d ? a
        : a == c && a == d ? a
        : a == b && c != d ? a
        : a == c && b != d ? a
        : a == d && b != c ? a
        : b == c && a != d ? b
        : b == d && a != c ? b
        : c == d && a != b ? c
        : pick4(r, a, b, c, d);
}

/* ---------------------------------------------------------------- biomes */

enum {
    OCEAN = 0, PLAINS = 1, DESERT = 2, EXTREME_HILLS = 3, FOREST = 4, TAIGA = 5, SWAMPLAND = 6,
    RIVER = 7, FROZEN_OCEAN = 10, FROZEN_RIVER = 11, ICE_PLAINS = 12, ICE_MOUNTAINS = 13,
    MUSHROOM_ISLAND = 14, MUSHROOM_SHORE = 15, BEACH = 16, DESERT_HILLS = 17, FOREST_HILLS = 18,
    TAIGA_HILLS = 19, EXTREME_HILLS_EDGE = 20, JUNGLE = 21, JUNGLE_HILLS = 22, JUNGLE_EDGE = 23,
    DEEP_OCEAN = 24, STONE_BEACH = 25, COLD_BEACH = 26, BIRCH_FOREST = 27, BIRCH_FOREST_HILLS = 28,
    ROOFED_FOREST = 29, COLD_TAIGA = 30, COLD_TAIGA_HILLS = 31, MEGA_TAIGA = 32, MEGA_TAIGA_HILLS = 33,
    EXTREME_HILLS_PLUS = 34, SAVANNA = 35, SAVANNA_PLATEAU = 36, MESA = 37, MESA_PLATEAU_F = 38,
    MESA_PLATEAU = 39,
};

/* BiomeGenBase.func_150568_d(id) != null. Out-of-range ids fall back to ocean in Java. */
static int exists(int id)
{
    return id < 0 || id > 255 ? 1 : BIOMES[id].exists;
}

static const struct biome_def *def(int id)
{
    return id < 0 || id > 255 ? &BIOMES[OCEAN] : &BIOMES[id];
}

/* GenLayer.func_151616_a: same biome, or same Java class (mesa plateaus only match each other). */
static int same(int a, int b)
{
    if (a == b) return 1;
    if (a == MESA_PLATEAU_F || a == MESA_PLATEAU) return b == MESA_PLATEAU_F || b == MESA_PLATEAU;
    return exists(a) && exists(b) ? def(a)->cls == def(b)->cls : 0;
}

/* GenLayer.func_151618_b */
static int oceanic(int id)
{
    return id == OCEAN || id == DEEP_OCEAN || id == FROZEN_OCEAN;
}

/* ---------------------------------------------------------------- layers */

static int *area(const struct layer *p, int x, int z, int w, int h)
{
    int *a = malloc(sizeof(int) * (size_t)w * (size_t)h);
    layer_ints(p, x, z, w, h, a);
    return a;
}

static void island(const struct layer *l, int x, int z, int w, int h, int *out)
{
    struct rng r;
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i)
        {
            cseed(&r, l, x + i, z + j);
            out[i + j * w] = next_int(&r, 10) == 0 ? 1 : 0;
        }
    if (x > -w && x <= 0 && z > -h && z <= 0) out[-x + -z * w] = 1;
}

static void zoom(const struct layer *l, int x, int z, int w, int h, int *out, int fuzzy)
{
    struct rng r;
    int px = x >> 1, pz = z >> 1, pw = (w >> 1) + 2, ph = (h >> 1) + 2;
    int *in = area(l->p1, px, pz, pw, ph);
    int nw = (pw - 1) << 1, nh = (ph - 1) << 1;
    int *t = malloc(sizeof(int) * (size_t)nw * (size_t)nh);
    for (int j = 0; j < ph - 1; ++j)
    {
        int k = (j << 1) * nw;
        int i = 0;
        int a = in[i + (j + 0) * pw];
        int b = in[i + (j + 1) * pw];
        for (; i < pw - 1; ++i)
        {
            cseed(&r, l, (i + px) << 1, (j + pz) << 1);
            int c = in[i + 1 + (j + 0) * pw];
            int d = in[i + 1 + (j + 1) * pw];
            t[k] = a;
            t[k + nw] = pick2(&r, a, b);
            ++k;
            t[k] = pick2(&r, a, c);
            t[k + nw] = fuzzy ? pick4(&r, a, c, b, d) : mode4(&r, a, c, b, d);
            ++k;
            a = c;
            b = d;
        }
    }
    for (int j = 0; j < h; ++j) memcpy(out + j * w, t + (j + (z & 1)) * nw + (x & 1), sizeof(int) * (size_t)w);
    free(t);
    free(in);
}

static void add_island(const struct layer *l, int x, int z, int w, int h, int *out)
{
    struct rng r;
    int pw = w + 2;
    int *in = area(l->p1, x - 1, z - 1, w + 2, h + 2);
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i)
        {
            int a = in[i + 0 + (j + 0) * pw], b = in[i + 2 + (j + 0) * pw];
            int c = in[i + 0 + (j + 2) * pw], d = in[i + 2 + (j + 2) * pw];
            int v = in[i + 1 + (j + 1) * pw];
            cseed(&r, l, x + i, z + j);
            int *o = &out[i + j * w];
            if (v == 0 && (a != 0 || b != 0 || c != 0 || d != 0))
            {
                int n = 1, pick = 1;
                if (a != 0 && next_int(&r, n++) == 0) pick = a;
                if (b != 0 && next_int(&r, n++) == 0) pick = b;
                if (c != 0 && next_int(&r, n++) == 0) pick = c;
                if (d != 0 && next_int(&r, n++) == 0) pick = d;
                if (next_int(&r, 3) == 0) *o = pick;
                else *o = pick == 4 ? 4 : 0;
            }
            else if (v > 0 && (a == 0 || b == 0 || c == 0 || d == 0))
            {
                if (next_int(&r, 5) == 0) *o = v == 4 ? 4 : 0;
                else *o = v;
            }
            else
            {
                *o = v;
            }
        }
    free(in);
}

static void remove_ocean(const struct layer *l, int x, int z, int w, int h, int *out)
{
    struct rng r;
    int pw = w + 2;
    int *in = area(l->p1, x - 1, z - 1, w + 2, h + 2);
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i)
        {
            int n = in[i + 1 + (j + 0) * pw], e = in[i + 2 + (j + 1) * pw];
            int wv = in[i + 0 + (j + 1) * pw], s = in[i + 1 + (j + 2) * pw];
            int v = in[i + 1 + (j + 1) * pw];
            out[i + j * w] = v;
            cseed(&r, l, x + i, z + j);
            if (v == 0 && n == 0 && e == 0 && wv == 0 && s == 0 && next_int(&r, 2) == 0) out[i + j * w] = 1;
        }
    free(in);
}

static void add_snow(const struct layer *l, int x, int z, int w, int h, int *out)
{
    struct rng r;
    int pw = w + 2;
    int *in = area(l->p1, x - 1, z - 1, w + 2, h + 2);
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i)
        {
            int v = in[i + 1 + (j + 1) * pw];
            cseed(&r, l, x + i, z + j);
            if (v == 0)
            {
                out[i + j * w] = 0;
            }
            else
            {
                int k = next_int(&r, 6);
                out[i + j * w] = k == 0 ? 4 : k <= 1 ? 3 : 1;
            }
        }
    free(in);
}

static void edge_cool_warm(const struct layer *l, int x, int z, int w, int h, int *out)
{
    int pw = w + 2;
    int *in = area(l->p1, x - 1, z - 1, w + 2, h + 2);
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i)
        {
            int v = in[i + 1 + (j + 1) * pw];
            if (v == 1)
            {
                int n = in[i + 1 + (j + 0) * pw], e = in[i + 2 + (j + 1) * pw];
                int wv = in[i + 0 + (j + 1) * pw], s = in[i + 1 + (j + 2) * pw];
                if (n == 3 || e == 3 || wv == 3 || s == 3 || n == 4 || e == 4 || wv == 4 || s == 4) v = 2;
            }
            out[i + j * w] = v;
        }
    free(in);
}

static void edge_heat_ice(const struct layer *l, int x, int z, int w, int h, int *out)
{
    int pw = w + 2;
    int *in = area(l->p1, x - 1, z - 1, w + 2, h + 2);
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i)
        {
            int v = in[i + 1 + (j + 1) * pw];
            if (v == 4)
            {
                int n = in[i + 1 + (j + 0) * pw], e = in[i + 2 + (j + 1) * pw];
                int wv = in[i + 0 + (j + 1) * pw], s = in[i + 1 + (j + 2) * pw];
                if (n == 2 || e == 2 || wv == 2 || s == 2 || n == 1 || e == 1 || wv == 1 || s == 1) v = 3;
            }
            out[i + j * w] = v;
        }
    free(in);
}

static void edge_special(const struct layer *l, int x, int z, int w, int h, int *out)
{
    struct rng r;
    layer_ints(l->p1, x, z, w, h, out);
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i)
        {
            cseed(&r, l, x + i, z + j);
            int v = out[i + j * w];
            if (v != 0 && next_int(&r, 13) == 0) v |= (1 + next_int(&r, 15)) << 8 & 0xF00;
            out[i + j * w] = v;
        }
}

static void add_mushroom(const struct layer *l, int x, int z, int w, int h, int *out)
{
    struct rng r;
    int pw = w + 2;
    int *in = area(l->p1, x - 1, z - 1, w + 2, h + 2);
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i)
        {
            int a = in[i + 0 + (j + 0) * pw], b = in[i + 2 + (j + 0) * pw];
            int c = in[i + 0 + (j + 2) * pw], d = in[i + 2 + (j + 2) * pw];
            int v = in[i + 1 + (j + 1) * pw];
            cseed(&r, l, x + i, z + j);
            if (v == 0 && a == 0 && b == 0 && c == 0 && d == 0 && next_int(&r, 100) == 0) out[i + j * w] = MUSHROOM_ISLAND;
            else out[i + j * w] = v;
        }
    free(in);
}

static void deep_ocean(const struct layer *l, int x, int z, int w, int h, int *out)
{
    int pw = w + 2;
    int *in = area(l->p1, x - 1, z - 1, w + 2, h + 2);
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i)
        {
            int n = in[i + 1 + (j + 0) * pw], e = in[i + 2 + (j + 1) * pw];
            int wv = in[i + 0 + (j + 1) * pw], s = in[i + 1 + (j + 2) * pw];
            int v = in[i + 1 + (j + 1) * pw];
            int k = (n == 0) + (e == 0) + (wv == 0) + (s == 0);
            out[i + j * w] = v == 0 && k > 3 ? DEEP_OCEAN : v;
        }
    free(in);
}

static void river_init(const struct layer *l, int x, int z, int w, int h, int *out)
{
    struct rng r;
    layer_ints(l->p1, x, z, w, h, out);
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i)
        {
            cseed(&r, l, x + i, z + j);
            out[i + j * w] = out[i + j * w] > 0 ? next_int(&r, 299999) + 2 : 0;
        }
}

static void biome(const struct layer *l, int x, int z, int w, int h, int *out)
{
    static const int warm[] = {DESERT, DESERT, DESERT, SAVANNA, SAVANNA, PLAINS};
    static const int medium[] = {FOREST, ROOFED_FOREST, EXTREME_HILLS, PLAINS, BIRCH_FOREST, SWAMPLAND};
    static const int cold[] = {FOREST, EXTREME_HILLS, TAIGA, PLAINS};
    static const int ice[] = {ICE_PLAINS, ICE_PLAINS, ICE_PLAINS, COLD_TAIGA};
    struct rng r;
    layer_ints(l->p1, x, z, w, h, out);
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i)
        {
            cseed(&r, l, x + i, z + j);
            int v = out[i + j * w];
            int special = (v & 0xF00) >> 8;
            v &= ~0xF00;
            int *o = &out[i + j * w];
            if (oceanic(v)) *o = v;
            else if (v == MUSHROOM_ISLAND) *o = v;
            else if (v == 1) *o = special > 0 ? (next_int(&r, 3) == 0 ? MESA_PLATEAU : MESA_PLATEAU_F) : warm[next_int(&r, 6)];
            else if (v == 2) *o = special > 0 ? JUNGLE : medium[next_int(&r, 6)];
            else if (v == 3) *o = special > 0 ? MEGA_TAIGA : cold[next_int(&r, 4)];
            else if (v == 4) *o = ice[next_int(&r, 4)];
            else *o = MUSHROOM_ISLAND;
        }
}

/* GenLayerBiomeEdge.func_151634_b */
static int edge_compatible(int a, int b)
{
    if (same(a, b)) return 1;
    if (!exists(a) || !exists(b)) return 0;
    int ta = def(a)->temp_cat, tb = def(b)->temp_cat;
    return ta == tb || ta == TEMP_MEDIUM || tb == TEMP_MEDIUM;
}

static void biome_edge(const struct layer *l, int x, int z, int w, int h, int *out)
{
    int pw = w + 2;
    int *in = area(l->p1, x - 1, z - 1, w + 2, h + 2);
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i)
        {
            int v = in[i + 1 + (j + 1) * pw];
            int n = in[i + 1 + (j + 0) * pw], e = in[i + 2 + (j + 1) * pw];
            int wv = in[i + 0 + (j + 1) * pw], s = in[i + 1 + (j + 2) * pw];
            int *o = &out[i + j * w];
            /* func_151636_a (extreme hills, by class) then func_151635_b (exact ids) */
            if (same(v, EXTREME_HILLS))
            {
                int ok = edge_compatible(n, EXTREME_HILLS) && edge_compatible(e, EXTREME_HILLS)
                      && edge_compatible(wv, EXTREME_HILLS) && edge_compatible(s, EXTREME_HILLS);
                *o = ok ? v : EXTREME_HILLS_EDGE;
                continue;
            }
            if (v == MESA_PLATEAU_F || v == MESA_PLATEAU)
            {
                int ok = same(n, v) && same(e, v) && same(wv, v) && same(s, v);
                *o = ok ? v : MESA;
                continue;
            }
            if (v == MEGA_TAIGA)
            {
                int ok = same(n, v) && same(e, v) && same(wv, v) && same(s, v);
                *o = ok ? v : TAIGA;
                continue;
            }
            if (v == DESERT)
            {
                *o = n != ICE_PLAINS && e != ICE_PLAINS && wv != ICE_PLAINS && s != ICE_PLAINS ? v : EXTREME_HILLS_PLUS;
            }
            else if (v == SWAMPLAND)
            {
                if (n != DESERT && e != DESERT && wv != DESERT && s != DESERT
                    && n != COLD_TAIGA && e != COLD_TAIGA && wv != COLD_TAIGA && s != COLD_TAIGA
                    && n != ICE_PLAINS && e != ICE_PLAINS && wv != ICE_PLAINS && s != ICE_PLAINS)
                    *o = n != JUNGLE && s != JUNGLE && e != JUNGLE && wv != JUNGLE ? v : JUNGLE_EDGE;
                else
                    *o = PLAINS;
            }
            else
            {
                *o = v;
            }
        }
    free(in);
}

static void hills(const struct layer *l, int x, int z, int w, int h, int *out)
{
    struct rng r;
    int pw = w + 2;
    int *in = area(l->p1, x - 1, z - 1, w + 2, h + 2);
    int *rv = area(l->p2, x - 1, z - 1, w + 2, h + 2);
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i)
        {
            cseed(&r, l, x + i, z + j);
            int v = in[i + 1 + (j + 1) * pw];
            int k = rv[i + 1 + (j + 1) * pw];
            int mutate = (k - 2) % 29 == 0;
            int *o = &out[i + j * w];
            if (v != 0 && k >= 2 && (k - 2) % 29 == 1 && v < 128)
            {
                *o = exists(v + 128) ? v + 128 : v;
            }
            else if (next_int(&r, 3) != 0 && !mutate)
            {
                *o = v;
            }
            else
            {
                int t = v;
                if (v == DESERT) t = DESERT_HILLS;
                else if (v == FOREST) t = FOREST_HILLS;
                else if (v == BIRCH_FOREST) t = BIRCH_FOREST_HILLS;
                else if (v == ROOFED_FOREST) t = PLAINS;
                else if (v == TAIGA) t = TAIGA_HILLS;
                else if (v == MEGA_TAIGA) t = MEGA_TAIGA_HILLS;
                else if (v == COLD_TAIGA) t = COLD_TAIGA_HILLS;
                else if (v == PLAINS) t = next_int(&r, 3) == 0 ? FOREST_HILLS : FOREST;
                else if (v == ICE_PLAINS) t = ICE_MOUNTAINS;
                else if (v == JUNGLE) t = JUNGLE_HILLS;
                else if (v == OCEAN) t = DEEP_OCEAN;
                else if (v == EXTREME_HILLS) t = EXTREME_HILLS_PLUS;
                else if (v == SAVANNA) t = SAVANNA_PLATEAU;
                else if (same(v, MESA_PLATEAU_F)) t = MESA;
                else if (v == DEEP_OCEAN && next_int(&r, 3) == 0) t = next_int(&r, 2) == 0 ? PLAINS : FOREST;

                if (mutate && t != v) t = exists(t + 128) ? t + 128 : v;

                if (t == v)
                {
                    *o = v;
                }
                else
                {
                    int n = in[i + 1 + (j + 0) * pw], e = in[i + 2 + (j + 1) * pw];
                    int wv = in[i + 0 + (j + 1) * pw], s = in[i + 1 + (j + 2) * pw];
                    int c = same(n, v) + same(e, v) + same(wv, v) + same(s, v);
                    *o = c >= 3 ? t : v;
                }
            }
        }
    free(rv);
    free(in);
}

static int river_class(int v)
{
    return v >= 2 ? 2 + (v & 1) : v;
}

static void river(const struct layer *l, int x, int z, int w, int h, int *out)
{
    int pw = w + 2;
    int *in = area(l->p1, x - 1, z - 1, w + 2, h + 2);
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i)
        {
            int wv = river_class(in[i + 0 + (j + 1) * pw]), e = river_class(in[i + 2 + (j + 1) * pw]);
            int n = river_class(in[i + 1 + (j + 0) * pw]), s = river_class(in[i + 1 + (j + 2) * pw]);
            int v = river_class(in[i + 1 + (j + 1) * pw]);
            out[i + j * w] = v == wv && v == n && v == e && v == s ? -1 : RIVER;
        }
    free(in);
}

static void smooth(const struct layer *l, int x, int z, int w, int h, int *out)
{
    struct rng r;
    int pw = w + 2;
    int *in = area(l->p1, x - 1, z - 1, w + 2, h + 2);
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i)
        {
            int wv = in[i + 0 + (j + 1) * pw], e = in[i + 2 + (j + 1) * pw];
            int n = in[i + 1 + (j + 0) * pw], s = in[i + 1 + (j + 2) * pw];
            int v = in[i + 1 + (j + 1) * pw];
            if (wv == e && n == s)
            {
                cseed(&r, l, x + i, z + j);
                v = next_int(&r, 2) == 0 ? wv : n;
            }
            else
            {
                if (wv == e) v = wv;
                if (n == s) v = n;
            }
            out[i + j * w] = v;
        }
    free(in);
}

static void rare_biome(const struct layer *l, int x, int z, int w, int h, int *out)
{
    struct rng r;
    int pw = w + 2;
    int *in = area(l->p1, x - 1, z - 1, w + 2, h + 2);
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i)
        {
            cseed(&r, l, x + i, z + j);
            int v = in[i + 1 + (j + 1) * pw];
            out[i + j * w] = next_int(&r, 57) == 0 && v == PLAINS ? PLAINS + 128 : v;
        }
    free(in);
}

/* GenLayerShore.func_151631_c */
static int jungle_compatible(int id)
{
    return (exists(id) && def(id)->cls == CLS_JUNGLE) || id == JUNGLE_EDGE || id == JUNGLE || id == JUNGLE_HILLS
        || id == FOREST || id == TAIGA || oceanic(id);
}

static void shore(const struct layer *l, int x, int z, int w, int h, int *out)
{
    struct rng r;
    int pw = w + 2;
    int *in = area(l->p1, x - 1, z - 1, w + 2, h + 2);
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i)
        {
            cseed(&r, l, x + i, z + j);
            int v = in[i + 1 + (j + 1) * pw];
            int n = in[i + 1 + (j + 0) * pw], e = in[i + 2 + (j + 1) * pw];
            int wv = in[i + 0 + (j + 1) * pw], s = in[i + 1 + (j + 2) * pw];
            int near_ocean = oceanic(n) || oceanic(e) || oceanic(wv) || oceanic(s);
            int *o = &out[i + j * w];
            if (v == MUSHROOM_ISLAND)
            {
                *o = n != OCEAN && e != OCEAN && wv != OCEAN && s != OCEAN ? v : MUSHROOM_SHORE;
            }
            else if (exists(v) && def(v)->cls == CLS_JUNGLE)
            {
                if (jungle_compatible(n) && jungle_compatible(e) && jungle_compatible(wv) && jungle_compatible(s))
                    *o = near_ocean ? BEACH : v;
                else
                    *o = JUNGLE_EDGE;
            }
            else if (v != EXTREME_HILLS && v != EXTREME_HILLS_PLUS && v != EXTREME_HILLS_EDGE)
            {
                if (exists(v) && def(v)->snow)
                {
                    *o = oceanic(v) ? v : near_ocean ? COLD_BEACH : v;
                }
                else if (v != MESA && v != MESA_PLATEAU_F)
                {
                    if (v != OCEAN && v != DEEP_OCEAN && v != RIVER && v != SWAMPLAND) *o = near_ocean ? BEACH : v;
                    else *o = v;
                }
                else
                {
                    if (near_ocean) *o = v;
                    else
                    {
                        int all_mesa = def(n)->mesa && exists(n) && def(e)->mesa && exists(e)
                                    && def(wv)->mesa && exists(wv) && def(s)->mesa && exists(s);
                        *o = all_mesa ? v : DESERT;
                    }
                }
            }
            else
            {
                *o = oceanic(v) ? v : near_ocean ? STONE_BEACH : v;
            }
        }
    free(in);
}

static void river_mix(const struct layer *l, int x, int z, int w, int h, int *out)
{
    int *rv = area(l->p2, x, z, w, h);
    layer_ints(l->p1, x, z, w, h, out);
    for (int k = 0; k < w * h; ++k)
    {
        int b = out[k];
        if (b != OCEAN && b != DEEP_OCEAN && rv[k] == RIVER)
        {
            if (b == ICE_PLAINS) out[k] = FROZEN_RIVER;
            else if (b != MUSHROOM_ISLAND && b != MUSHROOM_SHORE) out[k] = rv[k] & 255;
            else out[k] = MUSHROOM_SHORE;
        }
    }
    free(rv);
}

static double jitter(struct rng *r)
{
    return ((double)next_int(r, 1024) / 1024.0 - 0.5) * 3.6;
}

/* the voronoi over its parent's area in, rows stride apart */
static void voronoi_in(const struct layer *l, int x, int z, int w, int h, int *out, const int *in, int stride);

static void voronoi(const struct layer *l, int x, int z, int w, int h, int *out)
{
    int px = (x - 2) >> 2, pz = (z - 2) >> 2, pw = (w >> 2) + 2, ph = (h >> 2) + 2;
    int *in = area(l->p1, px, pz, pw, ph);

    voronoi_in(l, x, z, w, h, out, in, pw);
    free(in);
}

static void voronoi_in(const struct layer *l, int x, int z, int w, int h, int *out, const int *in, int stride)
{
    struct rng r;
    x -= 2;
    z -= 2;
    int px = x >> 2, pz = z >> 2, pw = (w >> 2) + 2, ph = (h >> 2) + 2;
    int nw = (pw - 1) << 2, nh = (ph - 1) << 2;
    int *t = malloc(sizeof(int) * (size_t)nw * (size_t)nh);
    for (int j = 0; j < ph - 1; ++j)
    {
        int i = 0;
        int a = in[i + (j + 0) * stride];
        int b = in[i + (j + 1) * stride];
        for (; i < pw - 1; ++i)
        {
            cseed(&r, l, (i + px) << 2, (j + pz) << 2);
            double ax = jitter(&r), az = jitter(&r);
            cseed(&r, l, (i + px + 1) << 2, (j + pz) << 2);
            double cx = jitter(&r) + 4.0, cz = jitter(&r);
            cseed(&r, l, (i + px) << 2, (j + pz + 1) << 2);
            double bx = jitter(&r), bz = jitter(&r) + 4.0;
            cseed(&r, l, (i + px + 1) << 2, (j + pz + 1) << 2);
            double dx = jitter(&r) + 4.0, dz = jitter(&r) + 4.0;
            int c = in[i + 1 + (j + 0) * stride] & 255;
            int d = in[i + 1 + (j + 1) * stride] & 255;
            for (int zz = 0; zz < 4; ++zz)
            {
                int k = ((j << 2) + zz) * nw + (i << 2);
                for (int xx = 0; xx < 4; ++xx)
                {
                    double da = (zz - az) * (zz - az) + (xx - ax) * (xx - ax);
                    double dc = (zz - cz) * (zz - cz) + (xx - cx) * (xx - cx);
                    double db = (zz - bz) * (zz - bz) + (xx - bx) * (xx - bx);
                    double dd = (zz - dz) * (zz - dz) + (xx - dx) * (xx - dx);
                    if (da < dc && da < db && da < dd) t[k++] = a;
                    else if (dc < da && dc < db && dc < dd) t[k++] = c;
                    else if (db < da && db < dc && db < dd) t[k++] = b;
                    else t[k++] = d;
                }
            }
            a = c;
            b = d;
        }
    }
    for (int j = 0; j < h; ++j) memcpy(out + j * w, t + (j + (z & 3)) * nw + (x & 3), sizeof(int) * (size_t)w);
    free(t);
}

void layer_ints(const struct layer *l, int x, int z, int w, int h, int *out)
{
    switch (l->kind)
    {
    case L_ISLAND: island(l, x, z, w, h, out); break;
    case L_ZOOM: zoom(l, x, z, w, h, out, 0); break;
    case L_FUZZY_ZOOM: zoom(l, x, z, w, h, out, 1); break;
    case L_ADD_ISLAND: add_island(l, x, z, w, h, out); break;
    case L_REMOVE_OCEAN: remove_ocean(l, x, z, w, h, out); break;
    case L_ADD_SNOW: add_snow(l, x, z, w, h, out); break;
    case L_EDGE_COOL_WARM: edge_cool_warm(l, x, z, w, h, out); break;
    case L_EDGE_HEAT_ICE: edge_heat_ice(l, x, z, w, h, out); break;
    case L_EDGE_SPECIAL: edge_special(l, x, z, w, h, out); break;
    case L_ADD_MUSHROOM: add_mushroom(l, x, z, w, h, out); break;
    case L_DEEP_OCEAN: deep_ocean(l, x, z, w, h, out); break;
    case L_RIVER_INIT: river_init(l, x, z, w, h, out); break;
    case L_BIOME: biome(l, x, z, w, h, out); break;
    case L_BIOME_EDGE: biome_edge(l, x, z, w, h, out); break;
    case L_HILLS: hills(l, x, z, w, h, out); break;
    case L_RIVER: river(l, x, z, w, h, out); break;
    case L_SMOOTH: smooth(l, x, z, w, h, out); break;
    case L_RARE_BIOME: rare_biome(l, x, z, w, h, out); break;
    case L_SHORE: shore(l, x, z, w, h, out); break;
    case L_RIVER_MIX: river_mix(l, x, z, w, h, out); break;
    /* the full layer's root, which no layer has for a parent: biomes_full
     * calls it, so the recursion's frames stay the smaller layers' */
    case L_VORONOI: abort();
    }
}

/* ---------------------------------------------------------------- stack */

static struct layer *mk(struct layers *g, enum layer_kind k, int64_t seed, struct layer *p1, struct layer *p2)
{
    struct layer *l = &g->node[g->n++];
    l->kind = k;
    l->base_seed = seed;
    for (int i = 0; i < 3; ++i) l->base_seed = lcg(l->base_seed, seed);
    l->world_seed = 0;
    l->p1 = p1;
    l->p2 = p2;
    return l;
}

static struct layer *magnify(struct layers *g, int64_t seed, struct layer *p, int n)
{
    for (int i = 0; i < n; ++i) p = mk(g, L_ZOOM, seed + i, p, 0);
    return p;
}

/* GenLayer.initWorldGenSeed: parent first, then this layer. Only river mix seeds
 * its second input; hills does not, so the zooms feeding hills' river noise keep
 * world seed 0 exactly as in Java. */
static void seed_world(struct layer *l, int64_t seed)
{
    if (l->kind == L_RIVER_MIX)
    {
        seed_world(l->p1, seed);
        seed_world(l->p2, seed);
    }
    else if (l->p1)
    {
        seed_world(l->p1, seed);
    }
    l->world_seed = seed;
    for (int i = 0; i < 3; ++i) l->world_seed = lcg(l->world_seed, l->base_seed);
}

/* GenLayer.initializeAllBiomeGenerators for WorldType.DEFAULT. */
void layers_init(struct layers *g, int64_t world_seed)
{
    memset(g, 0, sizeof *g);
    struct layer *a = mk(g, L_ISLAND, 1, 0, 0);
    a = mk(g, L_FUZZY_ZOOM, 2000, a, 0);
    a = mk(g, L_ADD_ISLAND, 1, a, 0);
    a = mk(g, L_ZOOM, 2001, a, 0);
    a = mk(g, L_ADD_ISLAND, 2, a, 0);
    a = mk(g, L_ADD_ISLAND, 50, a, 0);
    a = mk(g, L_ADD_ISLAND, 70, a, 0);
    a = mk(g, L_REMOVE_OCEAN, 2, a, 0);
    a = mk(g, L_ADD_SNOW, 2, a, 0);
    a = mk(g, L_ADD_ISLAND, 3, a, 0);
    a = mk(g, L_EDGE_COOL_WARM, 2, a, 0);
    a = mk(g, L_EDGE_HEAT_ICE, 2, a, 0);
    a = mk(g, L_EDGE_SPECIAL, 3, a, 0);
    a = mk(g, L_ZOOM, 2002, a, 0);
    a = mk(g, L_ZOOM, 2003, a, 0);
    a = mk(g, L_ADD_ISLAND, 4, a, 0);
    a = mk(g, L_ADD_MUSHROOM, 5, a, 0);
    a = mk(g, L_DEEP_OCEAN, 4, a, 0);
    struct layer *base = magnify(g, 1000, a, 0);

    struct layer *rinit = mk(g, L_RIVER_INIT, 100, magnify(g, 1000, base, 0), 0);
    struct layer *b = mk(g, L_BIOME, 200, base, 0);
    b = magnify(g, 1000, b, 2);
    b = mk(g, L_BIOME_EDGE, 1000, b, 0);
    struct layer *hill_noise = magnify(g, 1000, rinit, 2);
    b = mk(g, L_HILLS, 1000, b, hill_noise);

    struct layer *rv = magnify(g, 1000, rinit, 2);
    rv = magnify(g, 1000, rv, 4);
    rv = mk(g, L_RIVER, 1, rv, 0);
    rv = mk(g, L_SMOOTH, 1000, rv, 0);

    b = mk(g, L_RARE_BIOME, 1001, b, 0);
    for (int i = 0; i < 4; ++i)
    {
        b = mk(g, L_ZOOM, 1000 + i, b, 0);
        if (i == 0) b = mk(g, L_ADD_ISLAND, 3, b, 0);
        if (i == 1) b = mk(g, L_SHORE, 1000, b, 0);
    }
    b = mk(g, L_SMOOTH, 1000, b, 0);
    struct layer *mix = mk(g, L_RIVER_MIX, 100, b, rv);
    struct layer *vor = mk(g, L_VORONOI, 10, mix, 0);
    seed_world(mix, world_seed);
    seed_world(vor, world_seed);
    g->gen = mix;
    g->full = vor;
}

static void to_object_ids(int *v, int n)
{
    for (int i = 0; i < n; ++i) v[i] = BIOMES[v[i] & 255].object_id;
}

void biomes_gen(const struct layers *g, int x, int z, int w, int h, int *out)
{
    layer_ints(g->gen, x, z, w, h, out);
    to_object_ids(out, w * h);
}

void biomes_full(const struct layers *g, int x, int z, int w, int h, int *out)
{
    voronoi(g->full, x, z, w, h, out);
    to_object_ids(out, w * h);
}

void biomes_chunk(const struct layers *g, int cx, int cz, int *gen, int *full)
{
    int raw[100];

    layer_ints(g->gen, cx * 4 - 2, cz * 4 - 2, 10, 10, raw);
    /* the voronoi's parent area starts one cell in */
    voronoi_in(g->full, cx * 16, cz * 16, 16, 16, full, raw + 1 + 10, 10);
    to_object_ids(full, 256);
    memcpy(gen, raw, sizeof raw);
    to_object_ids(gen, 100);
}
