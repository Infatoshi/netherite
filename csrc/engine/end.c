/* ChunkProviderEnd, ported from oracle/src/world/gen/ChunkProviderEnd.java.
 * Float and double are kept exactly where Java uses them; build with
 * -ffp-contract=off. */
#include "end.h"
#include "env.h"
#include "jmath.h"

#include <math.h>
#include <string.h>

void end_init(struct end *e, int64_t seed)
{
    memset(e, 0, sizeof *e);
    e->seed = seed;
    jr_seed(&e->rand, seed);
    /* construction order consumes the same rand, so it is part of the result */
    octaves_init(&e->gen1, &e->rand, 16);
    octaves_init(&e->gen2, &e->rand, 16);
    octaves_init(&e->gen3, &e->rand, 8);
    octaves_init(&e->gen4, &e->rand, 10);
    octaves_init(&e->gen5, &e->rand, 16);
}

void end_biomes(uint8_t *biomes)
{
    memset(biomes, DIM_BIOME_SKY, 256);
}

/* initializeNoiseField: the 3x33x3 density field for chunk-grid origin
 * (x, 0, z). out index (i * 3 + j) * 33 + k. */
void end_field(struct end *e, int x, int z, double *field)
{
    /* the noise fields (env scratch) */
    double *island ENV_LOCAL = envstack_take(9 * sizeof *island);
    double *wide ENV_LOCAL = envstack_take(9 * sizeof *wide);
    double *main ENV_LOCAL = envstack_take(297 * sizeof *main);
    double *lo ENV_LOCAL = envstack_take(297 * sizeof *lo);
    double *hi ENV_LOCAL = envstack_take(297 * sizeof *hi);
    /* the 2D overload: 3D at y = 10, ySize 1, yScale 1.0 */
    octaves_2d(&e->gen4, island, x, z, 3, 3, 1.121, 0.5);
    octaves_2d(&e->gen5, wide, x, z, 3, 3, 200.0, 0.5);
    double scale = 684.412 * 2.0;
    octaves_3d(&e->gen3, main, x, 0, z, 3, 33, 3, scale / 80.0, 684.412 / 160.0, scale / 80.0);
    octaves_3d(&e->gen1, lo, x, 0, z, 3, 33, 3, scale, 684.412, scale);
    octaves_3d(&e->gen2, hi, x, 0, z, 3, 33, 3, scale, 684.412, scale);

    int c = 0, k = 0;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
        {
            double above = (island[c] + 256.0) / 512.0;
            if (above > 1.0)
                above = 1.0;
            /* vanilla computes a second selector from wide[c] here and then
             * overwrites it with 0.0 before use: it has no effect */
            if (above < 0.0)
                above = 0.0;
            above += 0.5;

            float fx = (float)(i + x);
            float fz = (float)(j + z);
            float edge = 100.0f - (float)sqrt((double)(fx * fx + fz * fz)) * 8.0f;
            if (edge > 80.0f)
                edge = 80.0f;
            if (edge < -100.0f)
                edge = -100.0f;

            ++c;
            double mid = 33.0 / 2.0;
            int half = 33 / 2;               /* p_73187_6_ / 2 in Java is int division */
            for (int y = 0; y < 33; ++y)
            {
                double v = 0.0;
                double drop = ((double)y - mid) * 8.0 / above;
                if (drop < 0.0)
                    drop *= -1.0;
                double a = lo[k] / 512.0;
                double b = hi[k] / 512.0;
                double m = (main[k] / 10.0 + 1.0) / 2.0;
                if (m < 0.0)
                    v = a;
                else if (m > 1.0)
                    v = b;
                else
                    v = a + (b - a) * m;
                v -= 8.0;
                v += (double)edge;
                int fade = 2;
                if (y > half - fade)
                {
                    double t = (double)((float)(y - (half - fade)) / 64.0f);
                    if (t < 0.0)
                        t = 0.0;
                    if (t > 1.0)
                        t = 1.0;
                    v = v * (1.0 - t) + -3000.0 * t;
                }
                fade = 8;
                if (y < fade)
                {
                    double t = (double)((float)(fade - y) / ((float)fade - 1.0f));
                    v = v * (1.0 - t) + -30.0 * t;
                }
                field[k++] = v;
            }
        }
}

void end_terrain(struct end *e, int cx, int cz, uint16_t *blocks)
{
    double *field = nw_scratch->end_field;
    end_field(e, cx * 2, cz * 2, field);
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j)
            for (int k = 0; k < 32; ++k)
            {
                double e0 = field[((i + 0) * 3 + j + 0) * 33 + k + 0];
                double e1 = field[((i + 0) * 3 + j + 1) * 33 + k + 0];
                double e2 = field[((i + 1) * 3 + j + 0) * 33 + k + 0];
                double e3 = field[((i + 1) * 3 + j + 1) * 33 + k + 0];
                double d0 = (field[((i + 0) * 3 + j + 0) * 33 + k + 1] - e0) * 0.25;
                double d1 = (field[((i + 0) * 3 + j + 1) * 33 + k + 1] - e1) * 0.25;
                double d2 = (field[((i + 1) * 3 + j + 0) * 33 + k + 1] - e2) * 0.25;
                double d3 = (field[((i + 1) * 3 + j + 1) * 33 + k + 1] - e3) * 0.25;
                for (int yy = 0; yy < 4; ++yy)
                {
                    double a0 = e0, a1 = e1;
                    double sx0 = (e2 - e0) * 0.125, sx1 = (e3 - e1) * 0.125;
                    for (int xx = 0; xx < 8; ++xx)
                    {
                        int idx = (xx + i * 8) << 11 | (0 + j * 8) << 7 | (k * 4 + yy);
                        double sz = (a1 - a0) * 0.125;
                        double v = a0;
                        for (int zz = 0; zz < 8; ++zz)
                        {
                            blocks[idx] = v > 0.0 ? DIM_END_STONE : DIM_AIR;
                            idx += 128;
                            v += sz;
                        }
                        a0 += sx0;
                        a1 += sx1;
                    }
                    e0 += d0;
                    e1 += d1;
                    e2 += d2;
                    e3 += d3;
                }
            }
}

void end_surface(int cx, int cz, uint16_t *blocks)
{
    (void)cx;
    (void)cz;
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x)
        {
            int depth = 1;
            int top = -1;
            uint16_t surf = DIM_END_STONE, fill = DIM_END_STONE;
            for (int y = 127; y >= 0; --y)
            {
                int idx = DIM_CELL(x, y, z);
                uint16_t b = blocks[idx];
                if (b != DIM_AIR)
                {
                    /* vanilla only rewrites stone; the density pass writes end
                     * stone, so this never fires */
                    if (b == DIM_STONE)
                    {
                        if (top == -1)
                        {
                            if (depth <= 0)
                            {
                                surf = DIM_AIR;
                                fill = DIM_END_STONE;
                            }
                            top = depth;
                            if (y >= 0)
                                blocks[idx] = surf;
                            else
                                blocks[idx] = fill;
                        }
                        else if (top > 0)
                        {
                            --top;
                            blocks[idx] = fill;
                        }
                    }
                }
                else
                    top = -1;
            }
        }
}
