/* Ported from oracle/src/world/gen/MapGenBase.java, MapGenCaves.java and
 * MapGenRavine.java. The carve index is x << 12 | z << 8 | y, the density-pass
 * layout. Java evaluates
 * operands left to right, so every expression that draws twice from one Random
 * is split into statements here. */
#include "carve.h"
#include "jmath.h"
#include "jrand.h"
#include "terrain.h"

#include <stddef.h>

#define PI_F ((float)3.141592653589793)
#define RANGE 8

enum { BLK_FLOWING_WATER = 8, BLK_FLOWING_LAVA = 10, BLK_LAVA = 11 };

struct cave_ctx {
    int cx, cz;
    const int *biomes;
    const uint16_t *tops;
    uint16_t *ids;
};

/* One step of a carver: clip the step's box to the chunk, give up if water
 * touches the box's shell, else clear the ellipsoid. profile is NULL for a
 * cave; for a ravine it is the per-height width profile, which flattens the
 * ellipsoid and places flowing lava instead of lava. Returns 0 on water. */
static int carve(const struct cave_ctx *c, double x, double y, double z, double w, double h, const float *profile)
{
    int x0 = mh_floor(x - w) - c->cx * 16 - 1, x1 = mh_floor(x + w) - c->cx * 16 + 1;
    int y0 = mh_floor(y - h) - 1, y1 = mh_floor(y + h) + 1;
    int z0 = mh_floor(z - w) - c->cz * 16 - 1, z1 = mh_floor(z + w) - c->cz * 16 + 1;
    if (x0 < 0) x0 = 0;
    if (x1 > 16) x1 = 16;
    if (y0 < 1) y0 = 1;
    if (y1 > 248) y1 = 248;
    if (z0 < 0) z0 = 0;
    if (z1 > 16) z1 = 16;

    /* Every cell of the edge columns, and the top and bottom of the others. */
    for (int i = x0; i < x1; ++i)
        for (int k = z0; k < z1; ++k)
            for (int j = y1 + 1; j >= y0 - 1; --j)
            {
                if (j < 0 || j >= 256) continue;
                int id = c->ids[(i * 16 + k) * 256 + j];
                if (id == BLK_FLOWING_WATER || id == BLK_WATER) return 0;
                if (j != y0 - 1 && i != x0 && i != x1 - 1 && k != z0 && k != z1 - 1) j = y0;
            }

    for (int i = x0; i < x1; ++i)
    {
        double ex = ((double)(i + c->cx * 16) + 0.5 - x) / w;
        for (int k = z0; k < z1; ++k)
        {
            double ez = ((double)(k + c->cz * 16) + 0.5 - z) / w;
            /* starts one cell above the y it tests: vanilla's off-by-one */
            int idx = (i * 16 + k) * 256 + y1;
            int grass = 0;
            if (!(ex * ex + ez * ez < 1.0)) continue;
            for (int j = y1 - 1; j >= y0; --j, --idx)
            {
                double ey = ((double)j + 0.5 - y) / h;
                int in = profile ? (ex * ex + ez * ez) * (double)profile[j] + ey * ey / 6.0 < 1.0
                                 : ey > -0.7 && ex * ex + ey * ey + ez * ez < 1.0;
                if (!in) continue;
                int id = c->ids[idx];
                if (id == BLK_GRASS) grass = 1;
                if (id != BLK_STONE && id != BLK_DIRT && id != BLK_GRASS) continue;
                if (j < 10)
                    c->ids[idx] = profile ? BLK_FLOWING_LAVA : BLK_LAVA;
                else
                {
                    c->ids[idx] = BLK_AIR;
                    if (grass && c->ids[idx - 1] == BLK_DIRT) c->ids[idx - 1] = c->tops[c->biomes[i + k * 16]];
                }
            }
        }
    }
    return 1;
}

/* MapGenCaves.func_151541_a: one tunnel, or with step == -1 one room. */
static void tunnel(const struct cave_ctx *c, int64_t seed, double x, double y, double z, float width,
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

        if (!room && step == branch && width > 1.0f && steps > 0)
        {
            int64_t s1 = jr_long(&r);
            float w1 = jr_float(&r) * 0.5f + 0.5f;
            tunnel(c, s1, x, y, z, w1, yaw - PI_F / 2.0f, pitch / 3.0f, step, steps, 1.0);
            int64_t s2 = jr_long(&r);
            float w2 = jr_float(&r) * 0.5f + 0.5f;
            tunnel(c, s2, x, y, z, w2, yaw + PI_F / 2.0f, pitch / 3.0f, step, steps, 1.0);
            return;
        }
        if (!room && jr_int_n(&r, 4) == 0) continue;

        double dx = x - ox, dz = z - oz, left = (double)(steps - step), reach = (double)(width + 2.0f + 16.0f);
        if (dx * dx + dz * dz - left * left > reach * reach) return;
        if (!(x >= ox - 16.0 - w * 2.0 && z >= oz - 16.0 - w * 2.0 && x <= ox + 16.0 + w * 2.0 && z <= oz + 16.0 + w * 2.0))
            continue;

        if (carve(c, x, y, z, w, h, NULL) && room) break;
    }
}

/* func_151538_a: the caves that start in chunk (sx, sz), carved into c's chunk */
static void cave_source(const struct cave_ctx *c, jrand *r, int sx, int sz)
{
    int a = jr_int_n(r, 15) + 1;
    int b = jr_int_n(r, a) + 1;
    int n = jr_int_n(r, b);
    if (jr_int_n(r, 7) != 0) n = 0;
    for (int i = 0; i < n; ++i)
    {
        double x = (double)(sx * 16 + jr_int_n(r, 16));
        double y = (double)jr_int_n(r, jr_int_n(r, 120) + 8);
        double z = (double)(sz * 16 + jr_int_n(r, 16));
        int tunnels = 1;
        if (jr_int_n(r, 4) == 0)
        {
            /* func_151542_a: a room */
            int64_t s = jr_long(r);
            float w = 1.0f + jr_float(r) * 6.0f;
            tunnel(c, s, x, y, z, w, 0.0f, 0.0f, -1, -1, 0.5);
            tunnels += jr_int_n(r, 4);
        }
        for (int j = 0; j < tunnels; ++j)
        {
            float yaw = jr_float(r) * PI_F * 2.0f;
            float pitch = (jr_float(r) - 0.5f) * 2.0f / 8.0f;
            float w = jr_float(r) * 2.0f;
            w += jr_float(r);
            if (jr_int_n(r, 10) == 0)
            {
                float f = jr_float(r);
                f *= jr_float(r);
                w *= f * 3.0f + 1.0f;
            }
            tunnel(c, jr_long(r), x, y, z, w, yaw, pitch, 0, 0, 1.0);
        }
    }
}

/* MapGenRavine.func_151540_a */
static void ravine(const struct cave_ctx *c, int64_t seed, double x, double y, double z, float width,
                   float yaw, float pitch, int step, int steps, double yscale)
{
    jrand r;
    jr_seed(&r, seed);
    double ox = (double)(c->cx * 16 + 8), oz = (double)(c->cz * 16 + 8);
    float dyaw = 0.0f, dpitch = 0.0f;
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
    float profile[256], f = 1.0f;
    for (int i = 0; i < 256; ++i)
    {
        if (i == 0 || jr_int_n(&r, 3) == 0)
        {
            float a = jr_float(&r), b = jr_float(&r);
            f = 1.0f + a * b * 1.0f;
        }
        profile[i] = f * f;
    }
    for (; step < steps; ++step)
    {
        double w = 1.5 + (double)(mh_sin((float)step * PI_F / (float)steps) * width * 1.0f);
        double h = w * yscale;
        w *= (double)jr_float(&r) * 0.25 + 0.75;
        h *= (double)jr_float(&r) * 0.25 + 0.75;
        float cp = mh_cos(pitch), sp = mh_sin(pitch);
        x += (double)(mh_cos(yaw) * cp);
        y += (double)sp;
        z += (double)(mh_sin(yaw) * cp);
        pitch *= 0.7f;
        pitch += dpitch * 0.05f;
        yaw += dyaw * 0.05f;
        dpitch *= 0.8f;
        dyaw *= 0.5f;
        float a = jr_float(&r), b = jr_float(&r), d = jr_float(&r);
        dpitch += (a - b) * d * 2.0f;
        a = jr_float(&r);
        b = jr_float(&r);
        d = jr_float(&r);
        dyaw += (a - b) * d * 4.0f;

        if (!room && jr_int_n(&r, 4) == 0) continue;
        double dx = x - ox, dz = z - oz, left = (double)(steps - step), reach = (double)(width + 2.0f + 16.0f);
        if (dx * dx + dz * dz - left * left > reach * reach) return;
        if (!(x >= ox - 16.0 - w * 2.0 && z >= oz - 16.0 - w * 2.0 && x <= ox + 16.0 + w * 2.0 && z <= oz + 16.0 + w * 2.0))
            continue;
        if (carve(c, x, y, z, w, h, profile) && room) break;
    }
}

/* MapGenRavine.func_151538_a */
static void ravine_source(const struct cave_ctx *c, jrand *r, int sx, int sz)
{
    if (jr_int_n(r, 50) != 0) return;
    double x = (double)(sx * 16 + jr_int_n(r, 16));
    double y = (double)(jr_int_n(r, jr_int_n(r, 40) + 8) + 20);
    double z = (double)(sz * 16 + jr_int_n(r, 16));
    float yaw = jr_float(r) * PI_F * 2.0f;
    float pitch = (jr_float(r) - 0.5f) * 2.0f / 8.0f;
    float w = jr_float(r) * 2.0f;
    w = (w + jr_float(r)) * 2.0f;
    ravine(c, jr_long(r), x, y, z, w, yaw, pitch, 0, 0, 3.0);
}

/* MapGenBase.func_151539_a: every chunk within RANGE may start carvers that
 * reach this one. Caves and ravines draw the same per-chunk seeds. */
static void carve_pass(int64_t seed, int cx, int cz, const int *biomes, const uint16_t *tops, uint16_t *ids,
                       void (*source)(const struct cave_ctx *, jrand *, int, int))
{
    struct cave_ctx c = {cx, cz, biomes, tops, ids};
    jrand r;
    jr_seed(&r, seed);
    uint64_t kx = (uint64_t)jr_long(&r), kz = (uint64_t)jr_long(&r);
    for (int sx = cx - RANGE; sx <= cx + RANGE; ++sx)
        for (int sz = cz - RANGE; sz <= cz + RANGE; ++sz)
        {
            jr_seed(&r, (int64_t)(((uint64_t)(int64_t)sx * kx) ^ ((uint64_t)(int64_t)sz * kz) ^ (uint64_t)seed));
            source(&c, &r, sx, sz);
        }
}

void caves_pass(int64_t seed, int cx, int cz, const int *biomes, const uint16_t *tops, uint16_t *ids)
{
    carve_pass(seed, cx, cz, biomes, tops, ids, cave_source);
}

void ravines_pass(int64_t seed, int cx, int cz, const int *biomes, const uint16_t *tops, uint16_t *ids)
{
    carve_pass(seed, cx, cz, biomes, tops, ids, ravine_source);
}
