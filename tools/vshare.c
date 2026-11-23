/* vshare (lane/pground, the orders of 8:10 PM): how many places an enderman can spawn on that a standing player's eye
 * has a clear line to, from the world generator alone (raw chunks: no population, so no trees, lakes or structures, and
 * nothing the player dug or built).
 *
 *   out/native/vshare SEED pos  < CELLS          a line "x y z" a standing cell (the feet's); out: x y z vshare vsky
 *                                               vcave all sky cave (below)
 *   out/native/vshare SEED search X Z R STEP [PART NPARTS]   candidates on a STEP grid within R of (X, Z): each column's
 *                                               surface cell and up to two cave floors (the highest pocket and the
 *                                               tallest); out: kind x y z dist depth vshare vsky vcave all
 *
 * A spawn cell: air with two air cells over it (an enderman is 2.9 high) on an opaque solid block (not ice or a
 * liquid); a sky cell when it is over its column's top block, else a cave cell. Its weight: 1 for a cave cell, F (0.266:
 * EntityMob.isValidLightLevel at midnight, saved sky light 15 against a dark cell) for a sky cell. Within 64 of the eye
 * (the middle of the mob, 1.45 over the cell's floor); visible when the straight segment from the eye (feet + 1.62) to
 * that middle passes no opaque solid cell (liquids do not stop the census's ray). vshare = the visible cells' weights
 * summed (vsky, vcave its two parts); all, sky, cave: the weights within 64 whether visible or not. One thread (the
 * generator's scratch is one env's); a chunk cache of CACHE_N chunks. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../csrc/engine/chunkgen.h"
#include "../csrc/engine/terrain.h"

#define F_SKY 0.266
#define VR 64
#define CACHE_N 4096

enum { C_AIR, C_SOLID, C_LIQ, C_OTHER };
struct cchunk {
    int cx, cz, used;
    uint8_t cls[16 * 16 * 256];   /* x << 12 | z << 8 | y */
    uint8_t top[256];             /* z << 4 | x: the highest non-air cell */
};

static struct chunkgen *G;
static uint16_t *IDS;
static uint8_t *METAS;
static struct cchunk *CACHE;
static int NEXT;

static int cls_of(int b)
{
    b &= 4095;
    if (b == 0) return C_AIR;
    if (b == 8 || b == 9 || b == 10 || b == 11) return C_LIQ;
    if (b == 79 || b == 78 || b == 31 || b == 32 || b == 37 || b == 38 || b == 39 || b == 40 || b == 83 || b == 106 ||
        b == 175 || b == 30 || b == 50 || b == 18 || b == 161)
        return C_OTHER;
    return C_SOLID;
}

static struct cchunk *chunk_at(int cx, int cz)
{
    unsigned h = ((unsigned)cx * 73856093u ^ (unsigned)cz * 19349663u) % CACHE_N;
    for (int p = 0; p < 8; ++p)
    {
        struct cchunk *c = &CACHE[(h + p) % CACHE_N];
        if (c->used && c->cx == cx && c->cz == cz) return c;
    }
    struct cchunk *c = &CACHE[(h + (NEXT++ & 7)) % CACHE_N];
    int biomes[256];
    chunkgen_raw(G, cx, cz, IDS, METAS, biomes);
    c->cx = cx, c->cz = cz, c->used = 1;
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z)
        {
            int top = 0;
            for (int y = 0; y < 256; ++y)
            {
                int k = x << 12 | z << 8 | y;
                c->cls[k] = (uint8_t)cls_of(IDS[k]);
                if (IDS[k] & 4095) top = y;
            }
            c->top[z << 4 | x] = (uint8_t)top;
        }
    return c;
}

static int cell(int x, int y, int z)
{
    if (y < 0) return C_SOLID;
    if (y > 255) return C_AIR;
    struct cchunk *c = chunk_at(x >> 4, z >> 4);
    return c->cls[(x & 15) << 12 | (z & 15) << 8 | y];
}

static int top_of(int x, int z)
{
    struct cchunk *c = chunk_at(x >> 4, z >> 4);
    return c->top[(z & 15) << 4 | (x & 15)];
}

/* the segment from o to t passes no solid cell (the end cell left out) */
static int clear_line(double ox, double oy, double oz, double tx, double ty, double tz)
{
    double o[3] = {ox, oy, oz}, d[3] = {tx - ox, ty - oy, tz - oz};
    int c[3], e[3] = {(int)floor(tx), (int)floor(ty), (int)floor(tz)}, st[3];
    double tm[3], td[3];
    for (int a = 0; a < 3; ++a)
    {
        c[a] = (int)floor(o[a]);
        st[a] = d[a] > 0 ? 1 : d[a] < 0 ? -1 : 0;
        td[a] = st[a] ? fabs(1.0 / d[a]) : 1e30;
        double nb = st[a] > 0 ? (double)(c[a] + 1) - o[a] : o[a] - (double)c[a];
        tm[a] = st[a] ? nb * td[a] : 1e30;
    }
    for (int it = 0; it < 400; ++it)
    {
        if (c[0] == e[0] && c[1] == e[1] && c[2] == e[2]) return 1;
        if (cell(c[0], c[1], c[2]) == C_SOLID && it > 0) return 0;
        int a = tm[0] < tm[1] ? (tm[0] < tm[2] ? 0 : 2) : (tm[1] < tm[2] ? 1 : 2);
        if (tm[a] > 1.0) return 1;
        c[a] += st[a], tm[a] += td[a];
    }
    return 1;
}

struct vres { double v, vsky, vcave, all, sky, cave; };

static void vshare_at(int fx, int fy, int fz, struct vres *r)
{
    memset(r, 0, sizeof *r);
    double ex = fx + 0.5, ey = fy + 1.62, ez = fz + 0.5;
    for (int dx = -VR; dx <= VR; ++dx)
        for (int dz = -VR; dz <= VR; ++dz)
        {
            if (dx * dx + dz * dz > VR * VR) continue;
            int x = fx + dx, z = fz + dz, top = top_of(x, z);
            int y0 = (int)ey - VR, y1 = (int)ey + VR;
            if (y0 < 1) y0 = 1;
            if (y1 > top + 1) y1 = top + 1;
            for (int y = y0; y <= y1; ++y)
            {
                if (cell(x, y, z) != C_AIR || cell(x, y + 1, z) != C_AIR || cell(x, y + 2, z) != C_AIR ||
                    cell(x, y - 1, z) != C_SOLID)
                    continue;
                double my = y + 1.45, ddx = x + 0.5 - ex, ddy = my - ey, ddz = z + 0.5 - ez;
                if (ddx * ddx + ddy * ddy + ddz * ddz > (double)VR * VR) continue;
                int sky = y > top;
                double wgt = sky ? F_SKY : 1.0;
                r->all += wgt, *(sky ? &r->sky : &r->cave) += wgt;
                if (!clear_line(ex, ey, ez, x + 0.5, my, z + 0.5)) continue;
                r->v += wgt, *(sky ? &r->vsky : &r->vcave) += wgt;
            }
        }
}

static int standable(int x, int y, int z)
{
    return cell(x, y, z) == C_AIR && cell(x, y + 1, z) == C_AIR && cell(x, y - 1, z) == C_SOLID;
}

int main(int argc, char **argv)
{
    if (argc < 3) return fprintf(stderr, "usage: vshare SEED pos < CELLS | vshare SEED search X Z R STEP [PART NPARTS]\n"), 2;
    G = malloc(sizeof *G);
    IDS = malloc(CHUNK_CELLS * sizeof *IDS);
    METAS = malloc(CHUNK_CELLS);
    CACHE = calloc(CACHE_N, sizeof *CACHE);
    chunkgen_init(G, strtoll(argv[1], NULL, 10));
    struct vres r;
    if (!strcmp(argv[2], "pos"))
    {
        int x, y, z;
        while (scanf("%d %d %d", &x, &y, &z) == 3)
        {
            vshare_at(x, y, z, &r);
            printf("%d\t%d\t%d\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\n", x, y, z, r.v, r.vsky, r.vcave, r.all, r.sky, r.cave);
            fflush(stdout);
        }
        return 0;
    }
    if (strcmp(argv[2], "search") || argc < 7) return fprintf(stderr, "vshare: search X Z R STEP [PART NPARTS]\n"), 2;
    int x0 = atoi(argv[3]), z0 = atoi(argv[4]), rr = atoi(argv[5]), step = atoi(argv[6]);
    int part = argc > 8 ? atoi(argv[7]) : 0, np = argc > 8 ? atoi(argv[8]) : 1, k = 0;
    if (rr <= 0 || step <= 0 || np <= 0) return fprintf(stderr, "vshare: bad R, STEP or NPARTS\n"), 2;
    for (int dz = -rr; dz <= rr; dz += step)
        for (int dx = -rr; dx <= rr; dx += step)
        {
            double dist = sqrt((double)dx * dx + (double)dz * dz);
            if (dist > rr) continue;
            if (k++ % np != part) continue;
            int x = x0 + dx, z = z0 + dz, top = top_of(x, z);
            /* the surface cell, then the highest cave floor and the floor under the tallest air run */
            int cand[3][2], nc = 0;
            if (cell(x, top, z) == C_SOLID && standable(x, top + 1, z)) cand[nc][0] = top + 1, cand[nc++][1] = 0;
            int hi = -1, best = -1, bestrun = 0;
            for (int y = top - 1; y >= 5; --y)
            {
                if (!standable(x, y, z)) continue;
                int run = 0;
                while (y + run < top && cell(x, y + run, z) == C_AIR) ++run;
                if (hi < 0) hi = y;
                if (run > bestrun) bestrun = run, best = y;
            }
            if (hi >= 0) cand[nc][0] = hi, cand[nc++][1] = 1;
            if (best >= 0 && best != hi) cand[nc][0] = best, cand[nc++][1] = 2;
            for (int c = 0; c < nc; ++c)
            {
                vshare_at(x, cand[c][0], z, &r);
                printf("%s\t%d\t%d\t%d\t%.0f\t%d\t%.1f\t%.1f\t%.1f\t%.1f\n",
                       cand[c][1] == 0 ? "surface" : cand[c][1] == 1 ? "cave_hi" : "cave_tall", x, cand[c][0], z, dist,
                       top + 1 - cand[c][0], r.v, r.vsky, r.vcave, r.all);
            }
            fflush(stdout);
        }
    return 0;
}
