/* ChunkProviderGenerate, ported from oracle/src/world/gen/ChunkProviderGenerate.java.
 * Float and double are kept exactly where Java uses them; build with -ffp-contract=off. */
#include "terrain.h"
#include "biomes.h"
#include "envstack.h"
#include "jmath.h"

#include <math.h>
#include <string.h>

void terrain_init(struct terrain *t, int64_t seed)
{
    memset(t, 0, sizeof *t);
    t->seed = seed;
    jr_seed(&t->rand, seed);
    /* construction order consumes the same rand, so it is part of the result */
    octaves_init(&t->min_limit, &t->rand, 16);  /* field_147431_j */
    octaves_init(&t->max_limit, &t->rand, 16);  /* field_147432_k */
    octaves_init(&t->main, &t->rand, 8);        /* field_147429_l */
    perlin_init(&t->stone, &t->rand, 4);        /* field_147430_m */
    octaves_init(&t->depth5, &t->rand, 10);     /* noiseGen5, unused by terrain */
    octaves_init(&t->depth, &t->rand, 16);      /* noiseGen6 */
    octaves_init(&t->mob, &t->rand, 8);         /* mobSpawnerNoise */
    for (int i = -2; i <= 2; ++i)
        for (int j = -2; j <= 2; ++j)
            t->parabolic[i + 2 + (j + 2) * 5] = 10.0f / (float)sqrt((double)((float)(i * i + j * j) + 0.2f));
    layers_init(&t->layers, seed);
}

static double denormalize_clamp(double a, double b, double t)
{
    return t < 0.0 ? a : t > 1.0 ? b : a + (b - a) * t;
}

/* func_147423_a: the 5x33x5 density field for chunk-grid origin (x, 0, z). */
void terrain_field(struct terrain *t, int x, int z, const int *biomes, double *field)
{
    /* the noise fields (env scratch) */
    double *depth ENV_LOCAL = envstack_take(25 * sizeof *depth);
    double *main ENV_LOCAL = envstack_take(825 * sizeof *main);
    double *lo ENV_LOCAL = envstack_take(825 * sizeof *lo);
    double *hi ENV_LOCAL = envstack_take(825 * sizeof *hi);
    octaves_2d(&t->depth, depth, x, z, 5, 5, 200.0, 200.0);
    unsigned char *need_lo ENV_LOCAL = envstack_take(825);
    unsigned char *need_hi ENV_LOCAL = envstack_take(825);
    octaves_3d(&t->main, main, x, 0, z, 5, 33, 5, 8.555150000000001, 4.277575000000001, 8.555150000000001);
    /* denormalize_clamp reads the low limit unless the main noise's blend is
     * above 1 and the high one unless it is below 0: the limits are made
     * only where they are read */
    for (int k = 0; k < 825; ++k)
    {
        double m = (main[k] / 10.0 + 1.0) / 2.0;
        need_lo[k] = !(m > 1.0);
        need_hi[k] = !(m < 0.0);
    }
    octaves_3d_need(&t->min_limit, lo, need_lo, x, 0, z, 5, 33, 5, 684.412, 684.412, 684.412);
    octaves_3d_need(&t->max_limit, hi, need_hi, x, 0, z, 5, 33, 5, 684.412, 684.412, 684.412);
    int k = 0, dk = 0;
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 5; ++j)
        {
            float var_sum = 0.0f, root_sum = 0.0f, weight_sum = 0.0f;
            const struct biome_def *center = &BIOMES[biomes[i + 2 + (j + 2) * 10]];
            for (int a = -2; a <= 2; ++a)
                for (int b = -2; b <= 2; ++b)
                {
                    const struct biome_def *bi = &BIOMES[biomes[i + a + 2 + (j + b + 2) * 10]];
                    float root = bi->root, var = bi->variation;
                    float w = t->parabolic[a + 2 + (b + 2) * 5] / (root + 2.0f);
                    if (bi->root > center->root) w /= 2.0f;
                    var_sum += var * w;
                    root_sum += root * w;
                    weight_sum += w;
                }
            var_sum /= weight_sum;
            root_sum /= weight_sum;
            var_sum = var_sum * 0.9f + 0.1f;
            root_sum = (root_sum * 4.0f - 1.0f) / 8.0f;
            double d = depth[dk] / 8000.0;
            if (d < 0.0) d = -d * 0.3;
            d = d * 3.0 - 2.0;
            if (d < 0.0)
            {
                d /= 2.0;
                if (d < -1.0) d = -1.0;
                d /= 1.4;
                d /= 2.0;
            }
            else
            {
                if (d > 1.0) d = 1.0;
                d /= 8.0;
            }
            ++dk;
            double r = (double)root_sum;
            double v = (double)var_sum;
            r += d * 0.2;
            r = r * 8.5 / 8.0;
            double base = 8.5 + r * 4.0;
            for (int y = 0; y < 33; ++y)
            {
                double fall = ((double)y - base) * 12.0 * 128.0 / 256.0 / v;
                if (fall < 0.0) fall *= 4.0;
                double l = lo[k] / 512.0;
                double h = hi[k] / 512.0;
                double m = (main[k] / 10.0 + 1.0) / 2.0;
                double dens = denormalize_clamp(l, h, m) - fall;
                if (y > 29)
                {
                    double s = (double)((float)(y - 29) / 3.0f);
                    dens = dens * (1.0 - s) + -10.0 * s;
                }
                field[k++] = dens;
            }
        }
}

void terrain_density(struct terrain *t, int cx, int cz, uint16_t *blocks)
{
    int biomes[100];
    biomes_gen(&t->layers, cx * 4 - 2, cz * 4 - 2, 10, 10, biomes);
    terrain_density_biomes(t, cx, cz, biomes, blocks);
}

void terrain_density_biomes(struct terrain *t, int cx, int cz, const int *biomes, uint16_t *blocks)
{
    double *f ENV_LOCAL = envstack_take(825 * sizeof *f);
    terrain_field(t, cx * 4, cz * 4, biomes, f);
    for (int i = 0; i < 4; ++i)
    {
        int a = i * 5, b = (i + 1) * 5;
        for (int j = 0; j < 4; ++j)
        {
            int c00 = (a + j) * 33, c01 = (a + j + 1) * 33, c10 = (b + j) * 33, c11 = (b + j + 1) * 33;
            for (int y = 0; y < 32; ++y)
            {
                double d00 = f[c00 + y], d01 = f[c01 + y], d10 = f[c10 + y], d11 = f[c11 + y];
                double s00 = (f[c00 + y + 1] - d00) * 0.125;
                double s01 = (f[c01 + y + 1] - d01) * 0.125;
                double s10 = (f[c10 + y + 1] - d10) * 0.125;
                double s11 = (f[c11 + y + 1] - d11) * 0.125;
                for (int yy = 0; yy < 8; ++yy)
                {
                    double e0 = d00, e1 = d01;
                    double sx0 = (d10 - d00) * 0.25, sx1 = (d11 - d01) * 0.25;
                    uint16_t fill = y * 8 + yy < 63 ? BLK_WATER : BLK_AIR;
                    for (int xx = 0; xx < 4; ++xx)
                    {
                        uint16_t *col = blocks + ((xx + i * 4) << 12 | (j * 4) << 8 | (y * 8 + yy));
                        double sz = (e1 - e0) * 0.25;
                        double v = e0 - sz;
                        v += sz;
                        col[0] = v > 0.0 ? BLK_STONE : fill;
                        v += sz;
                        col[256] = v > 0.0 ? BLK_STONE : fill;
                        v += sz;
                        col[512] = v > 0.0 ? BLK_STONE : fill;
                        v += sz;
                        col[768] = v > 0.0 ? BLK_STONE : fill;
                        e0 += sx0;
                        e1 += sx1;
                    }
                    d00 += s00;
                    d01 += s01;
                    d10 += s10;
                    d11 += s11;
                }
            }
        }
    }
}
