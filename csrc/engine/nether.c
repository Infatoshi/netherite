/* ChunkProviderHell and MapGenCavesHell, ported from
 * oracle/src/world/gen/ChunkProviderHell.java and MapGenCavesHell.java.
 * Operation order follows the Java exactly; build with -ffp-contract=off.
 * Java evaluates operands left to right, so any expression that draws twice
 * from one Random is split into statements here. */
#include "nether.h"
#include "env.h"
#include "jmath.h"

#include <math.h>
#include <string.h>

#define PI_F ((float)3.141592653589793)
#define RANGE 8

void nether_init(struct nether *n, int64_t seed)
{
    memset(n, 0, sizeof *n);
    n->seed = seed;
    jr_seed(&n->rand, seed);
    /* construction order consumes the same rand, so it is part of the result */
    octaves_init(&n->gen1, &n->rand, 16);
    octaves_init(&n->gen2, &n->rand, 16);
    octaves_init(&n->gen3, &n->rand, 8);
    octaves_init(&n->slowsand_gravel, &n->rand, 4);
    octaves_init(&n->exclusivity, &n->rand, 4);
    octaves_init(&n->gen6, &n->rand, 10);
    octaves_init(&n->gen7, &n->rand, 16);
}

/* initializeNoiseField: the 5x17x5 density field for chunk-grid origin
 * (x, 0, z). out index (i * 5 + j) * 17 + k. StrictMath.cos here is libm cos;
 * the argument is far from a rounding midpoint, see the report. */
void nether_field(struct nether *n, int x, int z, double *field)
{
    /* the noise fields (env scratch) */
    double *sparse ENV_LOCAL = envstack_take(25 * sizeof *sparse);
    double *wide ENV_LOCAL = envstack_take(25 * sizeof *wide);
    double *main ENV_LOCAL = envstack_take(425 * sizeof *main);
    double *lo ENV_LOCAL = envstack_take(425 * sizeof *lo);
    double *hi ENV_LOCAL = envstack_take(425 * sizeof *hi);
    octaves_3d(&n->gen6, sparse, x, 0, z, 5, 1, 5, 1.0, 0.0, 1.0);
    octaves_3d(&n->gen7, wide, x, 0, z, 5, 1, 5, 100.0, 0.0, 100.0);
    octaves_3d(&n->gen3, main, x, 0, z, 5, 17, 5, 684.412 / 80.0, 2053.236 / 60.0, 684.412 / 80.0);
    octaves_3d(&n->gen1, lo, x, 0, z, 5, 17, 5, 684.412, 2053.236, 684.412);
    octaves_3d(&n->gen2, hi, x, 0, z, 5, 17, 5, 684.412, 2053.236, 684.412);

    double curve[17];
    for (int i = 0; i < 17; ++i)
    {
        curve[i] = cos((double)i * 3.141592653589793 * 6.0 / 17.0) * 2.0;
        double t = (double)i;
        if (i > 17 / 2)
            t = (double)(17 - 1 - i);
        if (t < 4.0)
        {
            t = 4.0 - t;
            curve[i] -= t * t * t * 10.0;
        }
    }

    int c = 0, k = 0;
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 5; ++j)
        {
            double above = (sparse[c] + 256.0) / 512.0;
            if (above > 1.0)
                above = 1.0;
            double sel = wide[c] / 8000.0;
            if (sel < 0.0)
                sel = -sel;
            sel = sel * 3.0 - 3.0;
            if (sel < 0.0)
            {
                sel /= 2.0;
                if (sel < -1.0)
                    sel = -1.0;
                sel /= 1.4;
                sel /= 2.0;
                above = 0.0;
            }
            else
            {
                if (sel > 1.0)
                    sel = 1.0;
                sel /= 6.0;
            }
            above += 0.5;
            sel = sel * 17.0 / 16.0;
            ++c;
            for (int y = 0; y < 17; ++y)
            {
                double v = 0.0;
                double drop = curve[y];
                double a = lo[k] / 512.0;
                double b = hi[k] / 512.0;
                double m = (main[k] / 10.0 + 1.0) / 2.0;
                if (m < 0.0)
                    v = a;
                else if (m > 1.0)
                    v = b;
                else
                    v = a + (b - a) * m;
                v -= drop;
                if (y > 17 - 4)
                {
                    double t = (double)((float)(y - (17 - 4)) / 3.0f);
                    v = v * (1.0 - t) + -10.0 * t;
                }
                /* vanilla also tests y < below here, but below is 0.0 and never
                 * moves, so that branch cannot fire */
                field[k++] = v;
            }
        }
}

void nether_terrain(struct nether *n, int cx, int cz, uint16_t *blocks)
{
    double *field = nw_scratch->nether_field;
    nether_field(n, cx * 4, cz * 4, field);
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 16; ++k)
            {
                double e0 = field[((i + 0) * 5 + j + 0) * 17 + k + 0];
                double e1 = field[((i + 0) * 5 + j + 1) * 17 + k + 0];
                double e2 = field[((i + 1) * 5 + j + 0) * 17 + k + 0];
                double e3 = field[((i + 1) * 5 + j + 1) * 17 + k + 0];
                double d0 = (field[((i + 0) * 5 + j + 0) * 17 + k + 1] - e0) * 0.125;
                double d1 = (field[((i + 0) * 5 + j + 1) * 17 + k + 1] - e1) * 0.125;
                double d2 = (field[((i + 1) * 5 + j + 0) * 17 + k + 1] - e2) * 0.125;
                double d3 = (field[((i + 1) * 5 + j + 1) * 17 + k + 1] - e3) * 0.125;
                for (int yy = 0; yy < 8; ++yy)
                {
                    double a0 = e0, a1 = e1;
                    double sx0 = (e2 - e0) * 0.25, sx1 = (e3 - e1) * 0.25;
                    for (int xx = 0; xx < 4; ++xx)
                    {
                        int idx = (xx + i * 4) << 11 | (0 + j * 4) << 7 | (k * 8 + yy);
                        double sz = (a1 - a0) * 0.25;
                        double v = a0;
                        for (int zz = 0; zz < 4; ++zz)
                        {
                            uint16_t b = DIM_AIR;
                            if (k * 8 + yy < 32)
                                b = DIM_LAVA;
                            if (v > 0.0)
                                b = DIM_NETHERRACK;
                            blocks[idx] = b;
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

void nether_surface(struct nether *n, int cx, int cz, uint16_t *blocks)
{
    double *slowsand = nw_scratch->nether_slowsand, *gravel = nw_scratch->nether_gravel;
    double *exclusivity = nw_scratch->nether_exclusivity;
    /* 16x16x1 over (x, z) then 16x1x16 at y = 109 then 16x16x1 at half scale,
     * exactly the three generateNoiseOctaves calls func_147418_b makes */
    octaves_3d(&n->slowsand_gravel, slowsand, cx * 16, cz * 16, 0, 16, 16, 1, 0.03125, 0.03125, 1.0);
    octaves_3d(&n->slowsand_gravel, gravel, cx * 16, 109, cz * 16, 16, 1, 16, 0.03125, 1.0, 0.03125);
    octaves_3d(&n->exclusivity, exclusivity, cx * 16, cz * 16, 0, 16, 16, 1, 0.0625, 0.0625, 0.0625);

    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x)
        {
            int i = z + x * 16;                 /* the noise index order (x * 16 + z) */
            int soul = slowsand[i] + jr_double(&n->rand) * 0.2 > 0.0;
            int deposit = gravel[i] + jr_double(&n->rand) * 0.2 > 0.0;
            int depth = (int)(exclusivity[i] / 3.0 + 3.0 + jr_double(&n->rand) * 0.25);
            int top = -1;
            uint16_t surf = DIM_NETHERRACK, fill = DIM_NETHERRACK;
            for (int y = 127; y >= 0; --y)
            {
                int idx = DIM_CELL(x, y, z);
                /* the two draws short-circuit: the second only happens when the
                 * first comparison holds */
                if (y < 127 - jr_int_n(&n->rand, 5) && y > 0 + jr_int_n(&n->rand, 5))
                {
                    uint16_t b = blocks[idx];
                    if (b != DIM_AIR)
                    {
                        if (b == DIM_NETHERRACK)
                        {
                            if (top == -1)
                            {
                                if (depth <= 0)
                                {
                                    surf = DIM_AIR;
                                    fill = DIM_NETHERRACK;
                                }
                                else if (y >= 64 - 4 && y <= 64 + 1)
                                {
                                    surf = DIM_NETHERRACK;
                                    fill = DIM_NETHERRACK;
                                    if (deposit)
                                    {
                                        surf = DIM_GRAVEL;
                                        fill = DIM_NETHERRACK;
                                    }
                                    if (soul)
                                    {
                                        surf = DIM_SOUL_SAND;
                                        fill = DIM_SOUL_SAND;
                                    }
                                }
                                if (y < 64 && surf == DIM_AIR)
                                    surf = DIM_LAVA;
                                top = depth;
                                if (y >= 64 - 1)
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
                else
                    blocks[idx] = DIM_BEDROCK;
            }
        }
}

/* ---------------------------------------------------------- MapGenCavesHell */

struct hell_cave {
    int cx, cz;
    uint16_t *ids;
};

/* MapGenCavesHell.func_151543_a's carve step: clip the step's box to the chunk,
 * give up if lava touches the box's shell, else clear the ellipsoid. Returns 0
 * on lava. Unlike MapGenCaves there is no lava placement and no top-block
 * restore, and the y range is 0..128. */
static int hell_carve(const struct hell_cave *c, double x, double y, double z, double w, double h)
{
    int x0 = mh_floor(x - w) - c->cx * 16 - 1, x1 = mh_floor(x + w) - c->cx * 16 + 1;
    int y0 = mh_floor(y - h) - 1, y1 = mh_floor(y + h) + 1;
    int z0 = mh_floor(z - w) - c->cz * 16 - 1, z1 = mh_floor(z + w) - c->cz * 16 + 1;
    if (x0 < 0) x0 = 0;
    if (x1 > 16) x1 = 16;
    if (y0 < 1) y0 = 1;
    if (y1 > 120) y1 = 120;
    if (z0 < 0) z0 = 0;
    if (z1 > 16) z1 = 16;

    /* Every cell of the edge columns, and the top and bottom of the others. */
    int lava = 0;
    for (int i = x0; i < x1 && !lava; ++i)
        for (int k = z0; k < z1 && !lava; ++k)
            for (int j = y1 + 1; j >= y0 - 1 && !lava; --j)
            {
                if (j < 0 || j >= 128)
                    continue;
                int id = c->ids[(i * 16 + k) * 128 + j];
                if (id == DIM_FLOWING_LAVA || id == DIM_LAVA)
                    lava = 1;
                if (j != y0 - 1 && i != x0 && i != x1 - 1 && k != z0 && k != z1 - 1)
                    j = y0;
            }

    if (lava)
        return 0;
    for (int i = x0; i < x1; ++i)
    {
        double ex = ((double)(i + c->cx * 16) + 0.5 - x) / w;
        for (int k = z0; k < z1; ++k)
        {
            double ez = ((double)(k + c->cz * 16) + 0.5 - z) / w;
            /* starts one cell above the y it tests: vanilla's off-by-one */
            int idx = (i * 16 + k) * 128 + y1;
            for (int j = y1 - 1; j >= y0; --j, --idx)
            {
                double ey = ((double)j + 0.5 - y) / h;
                if (!(ey > -0.7 && ex * ex + ey * ey + ez * ez < 1.0))
                    continue;
                uint16_t b = c->ids[idx];
                if (b == DIM_NETHERRACK || b == DIM_DIRT || b == DIM_GRASS)
                    c->ids[idx] = DIM_AIR;
            }
        }
    }
    return 1;
}

/* MapGenCavesHell.func_151543_a: one tunnel, or with step == -1 one room. */
static void hell_tunnel(const struct hell_cave *c, int64_t seed, double x, double y, double z, float width,
                        float yaw, float pitch, int step, int steps, double yscale)
{
    double ox = (double)(c->cx * 16 + 8), oz = (double)(c->cz * 16 + 8);
    float dyaw = 0.0f, dpitch = 0.0f;
    jrand r;
    jr_seed(&r, seed);
    if (steps <= 0)
    {
        int m = RANGE * 16 - 16;
        steps = m - jr_int_n(&r, m / 4);
    }
    int room = 0;
    if (step == -1)
    {
        step = steps / 2;
        room = 1;
    }
    int branch = jr_int_n(&r, steps / 2) + steps / 4;
    for (int steep = jr_int_n(&r, 6) == 0; step < steps; ++step)
    {
        double w = 1.5 + (double)(mh_sin((float)step * PI_F / (float)steps) * width * 1.0f);
        double h = w * yscale;
        float cp = mh_cos(pitch), sp = mh_sin(pitch);
        x += (double)(mh_cos(yaw) * cp);
        y += (double)sp;
        z += (double)(mh_sin(yaw) * cp);
        pitch *= steep ? 0.92f : 0.7f;
        pitch += dpitch * 0.1f;
        yaw += dyaw * 0.1f;
        dpitch *= 0.9f;
        dyaw *= 0.75f;
        float a = jr_float(&r), b = jr_float(&r), d = jr_float(&r);
        dpitch += (a - b) * d * 2.0f;
        a = jr_float(&r);
        b = jr_float(&r);
        d = jr_float(&r);
        dyaw += (a - b) * d * 4.0f;

        /* MapGenCavesHell has no steps > 0 test here, unlike MapGenCaves */
        if (!room && step == branch && width > 1.0f)
        {
            int64_t s1 = jr_long(&r);
            float w1 = jr_float(&r) * 0.5f + 0.5f;
            hell_tunnel(c, s1, x, y, z, w1, yaw - PI_F / 2.0f, pitch / 3.0f, step, steps, 1.0);
            int64_t s2 = jr_long(&r);
            float w2 = jr_float(&r) * 0.5f + 0.5f;
            hell_tunnel(c, s2, x, y, z, w2, yaw + PI_F / 2.0f, pitch / 3.0f, step, steps, 1.0);
            return;
        }
        if (!room && jr_int_n(&r, 4) == 0)
            continue;

        double dx = x - ox, dz = z - oz, left = (double)(steps - step), reach = (double)(width + 2.0f + 16.0f);
        if (dx * dx + dz * dz - left * left > reach * reach)
            return;
        if (!(x >= ox - 16.0 - w * 2.0 && z >= oz - 16.0 - w * 2.0 && x <= ox + 16.0 + w * 2.0 &&
              z <= oz + 16.0 + w * 2.0))
            continue;

        if (hell_carve(c, x, y, z, w, h) && room)
            break;
    }
}

/* func_151538_a: the caves that start in chunk (sx, sz), carved into c's chunk */
static void hell_source(const struct hell_cave *c, jrand *r, int sx, int sz)
{
    int a = jr_int_n(r, 10);
    int b = jr_int_n(r, a + 1);
    int n = jr_int_n(r, b + 1);
    if (jr_int_n(r, 5) != 0)
        n = 0;
    for (int i = 0; i < n; ++i)
    {
        double x = (double)(sx * 16 + jr_int_n(r, 16));
        double y = (double)jr_int_n(r, 128);
        double z = (double)(sz * 16 + jr_int_n(r, 16));
        int tunnels = 1;
        if (jr_int_n(r, 4) == 0)
        {
            /* func_151544_a: a room */
            int64_t s = jr_long(r);
            float w = 1.0f + jr_float(r) * 6.0f;
            hell_tunnel(c, s, x, y, z, w, 0.0f, 0.0f, -1, -1, 0.5);
            tunnels += jr_int_n(r, 4);
        }
        for (int j = 0; j < tunnels; ++j)
        {
            float yaw = jr_float(r) * PI_F * 2.0f;
            float pitch = (jr_float(r) - 0.5f) * 2.0f / 8.0f;
            float w = jr_float(r) * 2.0f;
            w += jr_float(r);
            w *= 2.0f;
            hell_tunnel(c, jr_long(r), x, y, z, w, yaw, pitch, 0, 0, 0.5);
        }
    }
}

/* MapGenBase.func_151539_a: every chunk within RANGE may start carvers that
 * reach this one. The per-chunk seed is (sx * k1) ^ (sz * k2) ^ world seed. */
void nether_caves(int64_t seed, int cx, int cz, uint16_t *blocks)
{
    struct hell_cave c = {cx, cz, blocks};
    jrand r;
    jr_seed(&r, seed);
    uint64_t kx = (uint64_t)jr_long(&r), kz = (uint64_t)jr_long(&r);
    for (int sx = cx - RANGE; sx <= cx + RANGE; ++sx)
        for (int sz = cz - RANGE; sz <= cz + RANGE; ++sz)
        {
            jr_seed(&r, (int64_t)(((uint64_t)(int64_t)sx * kx) ^ ((uint64_t)(int64_t)sz * kz) ^ (uint64_t)seed));
            hell_source(&c, &r, sx, sz);
        }
}
