/* surface: surface.c's func_150573_a over the chunk, a thread per chunk
 * drawing the provider rand in C's column order. */
#include "dev.cuh"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* NoiseGeneratorPerlin.func_151601_a: one point, no generator offsets */
__device__ static __noinline__ double perlin_point(const struct g_simplex *g, int oct, double x, double z)
{
    double sum = 0.0, f = 1.0;
    for (int i = 0; i < oct; ++i)
    {
        sum += 70.0 * simplex_sum(&g[i], x * f, z * f) / f;
        f /= 2.0;
    }
    return sum;
}

__device__ static int64_t jround(double a)
{
    if (a == 0.49999999999999994) return 0;
    return (int64_t)floor(a + 0.5);
}

__device__ static int mesa_band(const struct cg_seed *s, int x, int y)
{
    int shift = (int)jround(perlin_point(&s->band_shift, 1, (double)x * 1.0 / 512.0, (double)x * 1.0 / 512.0) * 2.0);
    return s->bands[(y + shift + 64) % 64];
}

/* The column passes, each two ways: counting (W false: no writes, the
 * column's draws beyond its 258, an extra sand run's nextInt(4) or a
 * rejected nextInt(5), returned) and writing (W true). Every read of a
 * cell comes before its own write and no column reads another's, so the
 * count needs no copy. */

/* java.util.Random.nextInt(n), counting the next() calls */
__device__ static inline int32_t cnt_int_n(jrand *r, int32_t n, int *calls)
{
    ++*calls;
    if ((n & -n) == n) return (int32_t)(((int64_t)n * (int64_t)jr_next(r, 31)) >> 31);
    int32_t bits, val;
    bits = jr_next(r, 31);
    val = bits % n;
    while ((int32_t)((uint32_t)bits - (uint32_t)val + (uint32_t)(n - 1)) < 0)
    {
        ++*calls;
        bits = jr_next(r, 31);
        val = bits % n;
    }
    return val;
}

template <bool W>
__device__ static __noinline__ int generic(uint8_t *ids, uint8_t *metas, jrand *r, double noise, int top0, int meta0,
                                           int filler0, float temperature)
{
    int top = top0, meta = meta0, filler = filler0, run = -1, calls = 2;
    double off = c_negative == WORLDGEN_NEG_SURFACE ? 3.1 : 3.0;
    int depth = (int)(noise / 3.0 + off + jr_double(r) * 0.25);
    for (int y = 255; y >= 0; --y)
    {
        if (y <= 0 + cnt_int_n(r, 5, &calls))
        {
            if (W) ids[y] = BLK_BEDROCK;
            continue;
        }
        int id = ids[y];
        if (id == BLK_AIR)
        {
            run = -1;
            continue;
        }
        if (id != BLK_STONE) continue;
        if (run == -1)
        {
            if (depth <= 0)
            {
                top = BLK_AIR;
                meta = 0;
                filler = BLK_STONE;
            }
            else if (y >= 59 && y <= 64)
            {
                top = top0;
                meta = meta0;
                filler = filler0;
            }
            if (y < 63 && top == BLK_AIR)
            {
                top = temperature < 0.15f ? BLK_ICE : BLK_WATER;
                meta = 0;
            }
            run = depth;
            if (y >= 62)
            {
                if (W)
                {
                    ids[y] = (uint8_t)top;
                    metas[y] = (uint8_t)meta;
                }
            }
            else if (y < 56 - depth)
            {
                top = BLK_AIR;
                filler = BLK_STONE;
                if (W) ids[y] = BLK_GRAVEL;
            }
            else if (W)
                ids[y] = (uint8_t)filler;
        }
        else if (run > 0)
        {
            --run;
            if (W) ids[y] = (uint8_t)filler;
            if (run == 0 && filler == BLK_SAND)
            {
                run = cnt_int_n(r, 4, &calls) + (y - 63 > 0 ? y - 63 : 0);
                filler = BLK_SANDSTONE;
            }
        }
    }
    return calls - 258;
}

template <bool W>
__device__ static __noinline__ int mesa_column(const struct cg_seed *s, const struct worldgen_biome *b, uint8_t *ids,
                                               uint8_t *metas, jrand *r, int x, int z, double noise)
{
    double spire = 0.0;
    if (b->p1)
    {
        int sx = (x & -16) + (z & 15), sz = (z & -16) + (x & 15);
        double a = fabs(noise), n = perlin_point(s->spire, 4, (double)sx * 0.25, (double)sz * 0.25);
        double h = a <= n ? a : n;
        if (h > 0.0)
        {
            double cap = fabs(perlin_point(&s->spire_cap, 1, (double)sx * 0.001953125, (double)sz * 0.001953125));
            spire = h * h * 2.5;
            double lim = ceil(cap * 50.0) + 14.0;
            if (spire > lim) spire = lim;
            spire += 64.0;
        }
    }
    int filler = b->filler, calls = 2;
    int depth = (int)(noise / 3.0 + 3.0 + jr_double(r) * 0.25);
    int plain = cos(noise / 3.0 * 3.141592653589793) > 0.0;
    int run = -1, capped = 0;
    for (int y = 255; y >= 0; --y)
    {
        int id = ids[y];
        if (id == BLK_AIR && y < (int)spire)
        {
            id = BLK_STONE;
            if (W) ids[y] = BLK_STONE;
        }
        if (y <= 0 + cnt_int_n(r, 5, &calls))
        {
            if (W) ids[y] = BLK_BEDROCK;
            continue;
        }
        if (id == BLK_AIR)
        {
            run = -1;
            continue;
        }
        if (id != BLK_STONE) continue;
        if (!W) continue;   /* the rest draws nothing */
        if (run == -1)
        {
            capped = 0;
            if (depth <= 0) filler = BLK_STONE;
            else if (y >= 59 && y <= 64) filler = b->filler;
            run = depth + (y - 63 > 0 ? y - 63 : 0);
            if (y >= 62)
            {
                if (b->p2 && y > 86 + depth * 2)
                {
                    if (plain)
                    {
                        ids[y] = BLK_DIRT;
                        metas[y] = 1;
                    }
                    else
                        ids[y] = BLK_GRASS;
                }
                else if (y > 66 + depth)
                {
                    int band = 16;
                    if (y >= 64 && y <= 127)
                    {
                        if (!plain) band = mesa_band(s, x, y);
                    }
                    else
                        band = 1;
                    if (band < 16)
                    {
                        ids[y] = BLK_STAINED_CLAY;
                        metas[y] = (uint8_t)band;
                    }
                    else
                        ids[y] = BLK_HARDENED_CLAY;
                }
                else
                {
                    ids[y] = (uint8_t)b->top;
                    metas[y] = (uint8_t)b->top_meta;
                    capped = 1;
                }
            }
            else
            {
                ids[y] = (uint8_t)filler;
                if (filler == BLK_STAINED_CLAY) metas[y] = 1;
            }
        }
        else if (run > 0)
        {
            --run;
            if (capped)
            {
                ids[y] = BLK_STAINED_CLAY;
                metas[y] = 1;
            }
            else
            {
                int band = mesa_band(s, x, y);
                if (band < 16)
                {
                    ids[y] = BLK_STAINED_CLAY;
                    metas[y] = (uint8_t)band;
                }
                else
                    ids[y] = BLK_HARDENED_CLAY;
            }
        }
    }
    return calls - 258;
}

/* func_150573_a for one column (ids and metas its 256 cells): its extra
 * draws; *tid and *top the topBlock entry it rewrites (tid -1 none) */
template <bool W>
__device__ static int column(const struct cg_seed *s, int id, uint8_t *ids, uint8_t *metas, jrand *r, int x, int z,
                             double noise, int *tid, int *tval)
{
    const struct worldgen_biome *b = &d_biomes[id];
    if (b->surface == SURF_MUTATED)
    {
        /* BiomeGenMutated hands the whole call to its base object */
        id = b->base;
        b = &d_biomes[id];
    }
    /* a biome whose columns rewrite its topBlock computes its own top
     * first; every other reads the table's first value */
    int top = b->top, meta = b->top_meta, filler = b->filler;
    *tid = -1;
    switch (b->surface)
    {
    case SURF_MESA:
        return mesa_column<W>(s, b, ids, metas, r, x, z, noise);
    case SURF_HILLS:
        top = BLK_GRASS;
        meta = 0;
        filler = BLK_DIRT;
        if ((noise < -1.0 || noise > 2.0) && b->p1 == 2) top = filler = BLK_GRAVEL;
        else if (noise > 1.0 && b->p1 != 1) top = filler = BLK_STONE;
        *tid = id;
        break;
    case SURF_TAIGA:
        if (b->p1 == 1 || b->p1 == 2)
        {
            top = BLK_GRASS;
            meta = 0;
            filler = BLK_DIRT;
            if (noise > 1.75)
            {
                top = BLK_DIRT;
                meta = 1;
            }
            else if (noise > -0.95)
            {
                top = BLK_DIRT;
                meta = 2;
            }
            *tid = id;
        }
        break;
    case SURF_SAVANNA_M:
        top = BLK_GRASS;
        meta = 0;
        filler = BLK_DIRT;
        if (noise > 1.75) top = filler = BLK_STONE;
        else if (noise > -0.5)
        {
            top = BLK_DIRT;
            meta = 1;
        }
        *tid = id;
        break;
    default:
        break;
    }
    *tval = top;
    return generic<W>(ids, metas, r, noise, top, meta, filler, b->temperature);
}

/* the LCG n steps on (java.util.Random's multiplier and addend, mod 2^48) */
__device__ static uint64_t lcg_jump(uint64_t s, uint64_t n)
{
    uint64_t a = 0x5DEECE66DULL, c = 0xBULL;
    while (n)
    {
        if (n & 1) s = (s * a + c) & JR_MASK;
        c = (c * (a + 1)) & JR_MASK;
        a = (a * a) & JR_MASK;
        n >>= 1;
    }
    return s;
}

/* A block per chunk, a thread per column (256), in C's draw order (x
 * outer, z inner). The provider rand draws 258 times a column (depth's
 * nextDouble, a bedrock nextInt(5) a cell) and more only on a sand run's
 * nextInt(4) or a rejected nextInt(5): each column counts its extras from
 * where the extras before it would put it, a block scan moves every start,
 * until no start moves; then the columns write. The topBlock table ends as
 * the last column of each biome that rewrites it leaves it. */
__global__ void surface(const struct cg_seed *seeds, const struct worldgen_req *req, int n, const uint8_t *full256,
                          const double *stone, uint8_t *ids_all, uint8_t *metas_all, uint8_t *tops_all,
                          uint64_t *rand_all)
{
    __shared__ int wsum[8], last[256];
    __shared__ int moved;
    int k = blockIdx.x, t = threadIdx.x;
    if (k >= n) return;
    const struct cg_seed *s = &seeds[req[k].seed_index];
    int cx = req[k].cx, cz = req[k].cz;
    int a = t >> 4, b = t & 15;
    int x = cx * 16 + a, z = cz * 16 + b;
    uint8_t *ids = ids_all + (size_t)k * CELLS + (size_t)(b * 16 + a) * 256;
    uint8_t *metas = metas_all + (size_t)k * CELLS + (size_t)(b * 16 + a) * 256;
    int id = full256[(size_t)k * 256 + b + a * 16];
    double noise = stone[(size_t)k * 256 + b + a * 16];
    /* provideChunk: rand.setSeed(cx * 341873128712 + cz * 132897987541) */
    jrand r0;
    jr_seed(&r0, (int64_t)((uint64_t)(int64_t)cx * 341873128712ULL + (uint64_t)(int64_t)cz * 132897987541ULL));
    last[t] = -1;
    int before = 0, tid, tval, total;   /* the extras of the columns before this one */
    for (;;)
    {
        jrand r = {lcg_jump(r0.seed, (uint64_t)t * 258 + (uint64_t)before)};
        int extra = column<false>(s, id, ids, metas, &r, x, z, noise, &tid, &tval);
        /* the block's exclusive scan of the extras */
        int v = extra;
        for (int o = 1; o < 32; o <<= 1)
        {
            int u = __shfl_up_sync(0xffffffffu, v, o);
            if ((t & 31) >= o) v += u;
        }
        if ((t & 31) == 31) wsum[t >> 5] = v;
        if (t == 0) moved = 0;
        __syncthreads();
        int w = 0;
        for (int i = 0; i < (t >> 5); ++i) w += wsum[i];
        total = 0;
        for (int i = 0; i < 8; ++i) total += wsum[i];
        int now = w + v - extra;
        if (now != before) moved = 1;
        __syncthreads();
        int again = moved;
        before = now;
        __syncthreads();
        if (!again) break;
    }
    jrand r = {lcg_jump(r0.seed, (uint64_t)t * 258 + (uint64_t)before)};
    column<true>(s, id, ids, metas, &r, x, z, noise, &tid, &tval);
    if (tid >= 0) atomicMax(&last[tid], t << 8 | tval);
    __syncthreads();
    tops_all[(size_t)k * 256 + t] = last[t] >= 0 ? (uint8_t)(last[t] & 255) : (uint8_t)d_biomes[t].top;
    /* the provider rand as the pass leaves it (the provider's state) */
    if (t == 255) rand_all[k] = lcg_jump(r0.seed, 256ull * 258 + (uint64_t)total);
}
