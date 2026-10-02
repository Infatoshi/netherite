/* The surface pass, ported from ChunkProviderGenerate.func_147422_a and the
 * func_150573_a overrides in oracle/src/world/biome/. Two 1.7.10 quirks are kept:
 * the column for world (x, z) is written at chunk index (z*16 + x)*256 + y,
 * transposed against the terrain pass, and the biome and noise arrays are read
 * transposed the same way. */
#include "surface.h"
#include "biomes.h"

#include <math.h>
#include <string.h>

/* Java 8 Math.round(double) */
static int64_t jround(double a)
{
    if (a == 0x1.fffffffffffffp-2) return 0;
    return (int64_t)floor(a + 0.5);
}

void surface_init(struct surface *m, int64_t seed)
{
    for (int i = 0; i < 256; ++i) m->top[i] = BIOMES[i].top;
    /* func_150619_a */
    memset(m->bands, 16, sizeof m->bands);
    jrand r;
    jr_seed(&r, seed);
    perlin_init(&m->band_shift, &r, 1);
    for (int i = 0; i < 64; ++i)
    {
        i += jr_int_n(&r, 5) + 1;
        if (i < 64) m->bands[i] = 1;
    }
    static const struct { int bound, add; unsigned char color; } runs[3] = {{3, 1, 4}, {3, 2, 12}, {3, 1, 14}};
    for (int k = 0; k < 3; ++k)
    {
        int n = jr_int_n(&r, 4) + 2;
        for (int i = 0; i < n; ++i)
        {
            int len = jr_int_n(&r, runs[k].bound) + runs[k].add;
            int at = jr_int_n(&r, 64);
            for (int j = 0; at + j < 64 && j < len; ++j) m->bands[at + j] = runs[k].color;
        }
    }
    int n = jr_int_n(&r, 3) + 3, at = 0;
    for (int i = 0; i < n; ++i)
    {
        at += jr_int_n(&r, 16) + 4;
        if (at < 64)
        {
            m->bands[at] = 0;
            if (at > 1 && jr_bool(&r)) m->bands[at - 1] = 8;
            if (at < 63 && jr_bool(&r)) m->bands[at + 1] = 8;
        }
    }
    /* func_150573_a seeds these from Random(field_150622_aD) before it first
     * stores the world seed there, so in a fresh JVM they come from Random(0). */
    jr_seed(&r, 0);
    perlin_init(&m->spire, &r, 4);
    perlin_init(&m->spire_cap, &r, 1);
}

/* func_150618_d: the band color at height y; both noise inputs are x. */
static int mesa_band(const struct surface *m, int x, int y)
{
    int shift = (int)jround(perlin_point(&m->band_shift, (double)x * 1.0 / 512.0, (double)x * 1.0 / 512.0) * 2.0);
    return m->bands[(y + shift + 64) % 64];
}

/* func_150560_b's step for a stone cell: the top of a run (run -1) or a
 * cell inside it (run above 0); a run of 0 leaves the cell */
struct surf_col {
    int top, meta, filler, run, depth, top0, meta0, filler0;
    float temperature;
};

static inline void generic_stone(struct surf_col *c, uint16_t *ids, uint8_t *metas, jrand *r, int i, int y)
{
    if (c->run == -1)
    {
        if (c->depth <= 0)
        {
            c->top = BLK_AIR;
            c->meta = 0;
            c->filler = BLK_STONE;
        }
        else if (y >= 59 && y <= 64)
        {
            c->top = c->top0;
            c->meta = c->meta0;
            c->filler = c->filler0;
        }
        /* getFloatTemperature is the plain biome temperature below y 65 */
        if (y < 63 && c->top == BLK_AIR)
        {
            c->top = c->temperature < 0.15f ? BLK_ICE : BLK_WATER;
            c->meta = 0;
        }
        c->run = c->depth;
        if (y >= 62)
        {
            ids[i] = (uint16_t)c->top;
            metas[i] = (uint8_t)c->meta;
        }
        else if (y < 56 - c->depth)
        {
            c->top = BLK_AIR;
            c->filler = BLK_STONE;
            ids[i] = BLK_GRAVEL;
        }
        else
            ids[i] = (uint16_t)c->filler;
    }
    else if (c->run > 0)
    {
        --c->run;
        ids[i] = (uint16_t)c->filler;
        if (c->run == 0 && c->filler == BLK_SAND)
        {
            c->run = jr_int_n(r, 4) + (y - 63 > 0 ? y - 63 : 0);
            c->filler = BLK_SANDSTONE;
        }
    }
}

/* BiomeGenBase.func_150560_b */
static void generic(uint16_t *ids, uint8_t *metas, jrand *r, int x, int z, double noise,
                    int top0, int meta0, int filler0, float temperature)
{
    struct surf_col c = {top0, meta0, filler0, -1, 0, top0, meta0, filler0, temperature};

    c.depth = (int)(noise / 3.0 + 3.0 + jr_double(r) * 0.25);

    int col = ((z & 15) * 16 + (x & 15)) * 256;
    uint64_t seed = r->seed;

    /* nextInt(5) is at most 4, so above y 4 only its draw matters: one step
     * of the seed, and another while the 31 bits land in the three values its
     * rejection loop redraws (bits - bits % 5 + 4 overflows) */
    for (int y = 255; y > 4; --y)
    {
        do seed = (seed * 0x5DEECE66DULL + 0xBULL) & JR_MASK;
        while ((seed >> 17) >= 2147483645u);

        int i = col + y;

        if (ids[i] == BLK_AIR)
        {
            c.run = -1;
            continue;
        }
        if (ids[i] != BLK_STONE || c.run == 0) continue;

        r->seed = seed;
        generic_stone(&c, ids, metas, r, i, y);
        seed = r->seed;
    }

    r->seed = seed;

    for (int y = 4; y >= 0; --y)
    {
        int i = col + y;

        if (y <= 0 + jr_int_n(r, 5))
        {
            ids[i] = BLK_BEDROCK;
            continue;
        }
        if (ids[i] == BLK_AIR)
        {
            c.run = -1;
            continue;
        }
        if (ids[i] != BLK_STONE) continue;
        generic_stone(&c, ids, metas, r, i, y);
    }
}

/* BiomeGenMesa.func_150573_a */
static void mesa_column(const struct surface *m, const struct biome_def *b, uint16_t *ids, uint8_t *metas,
                        jrand *r, int x, int z, double noise)
{
    double spire = 0.0;
    if (b->p1)
    {
        /* Bryce spires: the noise coordinates swap x and z inside the chunk */
        int sx = (x & -16) + (z & 15), sz = (z & -16) + (x & 15);
        double a = fabs(noise), n = perlin_point(&m->spire, (double)sx * 0.25, (double)sz * 0.25);
        double h = a <= n ? a : n;
        if (h > 0.0)
        {
            double cap = fabs(perlin_point(&m->spire_cap, (double)sx * 0.001953125, (double)sz * 0.001953125));
            spire = h * h * 2.5;
            double lim = ceil(cap * 50.0) + 14.0;
            if (spire > lim) spire = lim;
            spire += 64.0;
        }
    }
    int filler = b->filler;
    int depth = (int)(noise / 3.0 + 3.0 + jr_double(r) * 0.25);
    int plain = cos(noise / 3.0 * 3.141592653589793) > 0.0;   /* Math.PI; StrictMath.cos, only its sign is used */
    int run = -1, capped = 0;
    int col = ((z & 15) * 16 + (x & 15)) * 256;
    for (int y = 255; y >= 0; --y)
    {
        int i = col + y;
        if (ids[i] == BLK_AIR && y < (int)spire) ids[i] = BLK_STONE;
        if (y <= 0 + jr_int_n(r, 5))
        {
            ids[i] = BLK_BEDROCK;
            continue;
        }
        if (ids[i] == BLK_AIR)
        {
            run = -1;
            continue;
        }
        if (ids[i] != BLK_STONE) continue;
        if (run == -1)
        {
            /* Java also tracks a top block here that it never places */
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
                        ids[i] = BLK_DIRT;
                        metas[i] = 1;
                    }
                    else
                        ids[i] = BLK_GRASS;
                }
                else if (y > 66 + depth)
                {
                    int band = 16;
                    if (y >= 64 && y <= 127)
                    {
                        if (!plain) band = mesa_band(m, x, y);
                    }
                    else
                        band = 1;
                    if (band < 16)
                    {
                        ids[i] = BLK_STAINED_CLAY;
                        metas[i] = (uint8_t)band;
                    }
                    else
                        ids[i] = BLK_HARDENED_CLAY;
                }
                else
                {
                    ids[i] = b->top;
                    metas[i] = (uint8_t)b->top_meta;
                    capped = 1;
                }
            }
            else
            {
                ids[i] = (uint16_t)filler;
                if (filler == BLK_STAINED_CLAY) metas[i] = 1;
            }
        }
        else if (run > 0)
        {
            --run;
            if (capped)
            {
                ids[i] = BLK_STAINED_CLAY;
                metas[i] = 1;
            }
            else
            {
                int band = mesa_band(m, x, y);
                if (band < 16)
                {
                    ids[i] = BLK_STAINED_CLAY;
                    metas[i] = (uint8_t)band;
                }
                else
                    ids[i] = BLK_HARDENED_CLAY;
            }
        }
    }
}

/* func_150573_a, dispatched on the biome object's class */
static void column(struct surface *m, int id, uint16_t *ids, uint8_t *metas, jrand *r, int x, int z, double noise)
{
    const struct biome_def *b = &BIOMES[id];
    int top = m->top[id], meta = b->top_meta, filler = b->filler;
    switch (b->surface)
    {
    case SURF_MUTATED:
        /* BiomeGenMutated hands the whole call, temperature included, to its base object */
        column(m, b->base, ids, metas, r, x, z, noise);
        return;
    case SURF_MESA:
        mesa_column(m, b, ids, metas, r, x, z, noise);
        return;
    case SURF_HILLS:
        /* p1 is field_150638_aH: 0 plain, 1 edge and plus, 2 mutated */
        top = BLK_GRASS;
        meta = 0;
        filler = BLK_DIRT;
        if ((noise < -1.0 || noise > 2.0) && b->p1 == 2) top = filler = BLK_GRAVEL;
        else if (noise > 1.0 && b->p1 != 1) top = filler = BLK_STONE;
        m->top[id] = (uint16_t)top;
        break;
    case SURF_TAIGA:
        /* p1 is field_150644_aH: 1 mega, 2 mega spruce */
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
            m->top[id] = (uint16_t)top;
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
        m->top[id] = (uint16_t)top;
        break;
    case SURF_SWAMP:
        /* The swamp water/lily loop stops at y 255 on its first cell, because it
         * treats null (air) as a hit, so it never places anything. */
        break;
    default:
        break;
    }
    generic(ids, metas, r, x, z, noise, top, meta, filler, b->temperature);
}

void surface_pass(struct terrain *t, struct surface *m, int cx, int cz, const int *biomes,
                  uint16_t *ids, uint8_t *metas)
{
    double stone[256];
    perlin_2d(&t->stone, stone, (double)(cx * 16), (double)(cz * 16), 16, 16, 0.0625, 0.0625, 1.0);
    for (int a = 0; a < 16; ++a)
        for (int b = 0; b < 16; ++b)
            column(m, biomes[b + a * 16], ids, metas, &t->rand, cx * 16 + a, cz * 16 + b, stone[b + a * 16]);
}

/* The biome object whose topBlock column() writes for a column of biome id,
 * or -1 when it writes none. */
static int column_writes_top(int id)
{
    const struct biome_def *b = &BIOMES[id];

    while (b->surface == SURF_MUTATED)
    {
        id = b->base;
        b = &BIOMES[id];
    }
    switch (b->surface)
    {
    case SURF_HILLS:
    case SURF_SAVANNA_M:
        return id;
    case SURF_TAIGA:
        return b->p1 == 1 || b->p1 == 2 ? id : -1;
    default:
        return -1;
    }
}

void surface_tops_merge(struct surface *s, const uint8_t *biome, const uint8_t *tops)
{
    for (int i = 0; i < 256; ++i)
    {
        int id = column_writes_top(biome[i]);

        if (id >= 0) s->top[id] = tops[id];
    }
}
