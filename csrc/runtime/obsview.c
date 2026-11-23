/* lane/obsidian: what the player sees of water, lava and obsidian (obsview.h) */
#define _GNU_SOURCE
#include "obsview.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#if defined(__AVX512BW__)
#include <immintrin.h>
#endif

#include "../engine/env.h"
#include "../engine/raytrace.h"
#include "../engine/session.h"
#include "../engine/world.h"
#include "pipeline.h"
#include "pool.h"

#define OV_CAND 384             /* the candidates kept a kind (the nearest; a lava lake's surface is a few hundred) */
#define OV_RAYS 24              /* the lines of sight tried a kind, nearest first */

struct ov_cand { float d2; int32_t x, y, z; };

static int cand_cmp(const void *a, const void *b)
{
    float x = ((const struct ov_cand *)a)->d2, y = ((const struct ov_cand *)b)->d2;
    return x < y ? -1 : x > y;
}

#if !defined(__AVX512BW__)
/* a low id byte of water, lava or obsidian (8 to 11, 49): any cell whose id is one of them has it (an id past 255
 * with that low byte is judged by its whole id after) */
static inline int ov_byte(unsigned b) { return b - 8u < 4u || b == 49u; }

/* lane/macroperf: section sy of c, the local box x ax..bx, z az..bz, y ylo..yhi (inside the section): the bit
 * y << 8 | z << 4 | x of bits set for each cell whose id byte is ov_byte's (ascending bits: the scan's y, z, x order);
 * 0 when none (a section missing or masked off reads air everywhere, as chunk_get_block does) */
/* a nonzero answer when any byte of w is one of ov_byte's: a byte is 8 to 11 when it is 8 with its low two bits cleared,
 * 49 when it xors 49 to 0; (v - 0x01..) & ~v & 0x80.. is nonzero exactly when some byte of v is 0 */
static inline uint64_t ov_word(uint64_t w)
{
    const uint64_t ones = 0x0101010101010101ull, highs = 0x8080808080808080ull;
    uint64_t a = (w & 0xfcfcfcfcfcfcfcfcull) ^ 0x0808080808080808ull, b = w ^ 0x3131313131313131ull;
    return ((a - ones) & ~a & highs) | ((b - ones) & ~b & highs);
}
#endif

static int ov_gather(const struct chunk *c, int sy, int ax, int bx, int az, int bz, int ylo, int yhi, uint64_t bits[64])
{
    if (!(c->mask >> sy & 1)) return 0;
    const struct chunk_sec *s = chunk_sec_at(c, sy);
    if (s == NULL) return 0;
    int found = 0;
#if defined(__AVX512BW__)
    const __m512i vmask = _mm512_set1_epi8((char)0xfc);
    const __m512i v8 = _mm512_set1_epi8(8);
    const __m512i v49 = _mm512_set1_epi8(49);
    const int z0 = az & ~3;
    for (int x = ax; x <= bx; ++x)
    {
        const uint8_t *plane = s->ids + (x << 8);
        for (int z = z0; z <= bz; z += 4)
        {
            __m512i v = _mm512_loadu_si512((const void *)(plane + (z << 4)));
            uint64_t m = (uint64_t)_mm512_cmpeq_epi8_mask(_mm512_and_si512(v, vmask), v8) |
                         (uint64_t)_mm512_cmpeq_epi8_mask(v, v49);
            while (m)
            {
                int bit = __builtin_ctzll(m);
                m &= m - 1;
                int zz = z + (bit >> 4), y = bit & 15;
                if (zz < az || zz > bz || y < ylo || y > yhi) continue;
                if (!found) memset(bits, 0, 64 * sizeof *bits);
                int key = y << 8 | zz << 4 | x;
                bits[key >> 6] |= 1ull << (key & 63);
                found = 1;
            }
        }
    }
#else
    for (int x = ax; x <= bx; ++x)
        for (int z = az; z <= bz; ++z)
        {
            /* a column's 16 cells are 16 bytes (SEC_XYZ: y the fastest): two words tell whether any is a kind's */
            const uint8_t *col = s->ids + SEC_XYZ(x, 0, z);
            uint64_t lo, hi;
            memcpy(&lo, col, 8);
            memcpy(&hi, col + 8, 8);
            if (!(ov_word(lo) | ov_word(hi))) continue;
            if (!found) memset(bits, 0, 64 * sizeof *bits);
            for (int y = ylo; y <= yhi; ++y)
                if (ov_byte(col[y]))
                {
                    int key = y << 8 | z << 4 | x;
                    bits[key >> 6] |= 1ull << (key & 63);
                    found = 1;
                }
        }
#endif
    return found;
}

static struct world *ov_world(struct pipe *p, int id, struct env **saved)
{
    struct pool *pl = pipe_pool(p);
    struct session *ss = pool_session(pl, id);
    if (ss == NULL || ss->client_world == NULL) return NULL;
    *saved = neth_env;
    neth_env = pool_env(pl, id);
    return ss->client_world;
}

/* lane/obsidian4: air in one of a cell's four horizontal neighbours */
static int ov_air_beside(struct world *cw, int x, int y, int z)
{
    static const int D[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int k = 0; k < 4; ++k)
    {
        struct chunk *c = world_chunk(cw, (x + D[k][0]) >> 4, (z + D[k][1]) >> 4);
        if (c != NULL && (chunk_get_block(c, (x + D[k][0]) & 15, y, (z + D[k][1]) & 15) & 4095) == 0) return 1;
    }
    return 0;
}

/* lane/obsid2 (OV_FACES): a block's open faces (the top and the four sides whose neighbour is air or a liquid), each
 * face's centre a hair inside the block: seen when the ray to one of them meets the block first. From a 1x2 tunnel's
 * edge the ray to a floor block's centre crosses the rim or the block before it (a 10-source pool cast from the tunnel:
 * all 10 obsidian in the cone, none seen) where a player sees their tops */
static int ov_face_seen(struct world *cw, double ex, double ey, double ez, int x, int y, int z)
{
    static const int D[5][3] = {{0, 1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int f = 0; f < 5; ++f)
    {
        int nx = x + D[f][0], ny = y + D[f][1], nz = z + D[f][2];
        if (ny < 0 || ny > 255) continue;
        struct chunk *c = world_chunk(cw, nx >> 4, nz >> 4);
        if (c == NULL) continue;
        int b = chunk_get_block(c, nx & 15, ny, nz & 15) & 4095;
        if (b != 0 && !(b >= 8 && b <= 11)) continue;
        double px = x + 0.5 + 0.48 * D[f][0], py = y + 0.5 + 0.48 * D[f][1], pz = z + 0.5 + 0.48 * D[f][2];
        /* the face must turn to the eye */
        if ((ex - px) * D[f][0] + (ey - py) * D[f][1] + (ez - pz) * D[f][2] <= 0.0) continue;
        struct rt_mop m;
        if (raytrace_blocks(cw, ex, ey, ez, px, py, pz, 0, 1, 0, &m) && m.type >= 0 && m.x == x && m.y == y && m.z == z)
            return 1;
    }
    return 0;
}

int obsview_block(struct pipe *p, int id, int x, int y, int z, int *meta)
{
    struct env *saved;
    struct world *cw = ov_world(p, id, &saved);
    if (cw == NULL || y < 0 || y > 255) return -1;
    struct chunk *c = world_chunk(cw, x >> 4, z >> 4);
    int b = -1;
    if (c != NULL)
    {
        b = chunk_get_block(c, x & 15, y, z & 15) & 4095;
        if (meta) *meta = chunk_get_meta(c, x & 15, y, z & 15);
    }
    neth_env = saved;
    return b;
}

int obsview_sky(struct pipe *p, int id, int x, int y, int z)
{
    struct env *saved;
    struct world *cw = ov_world(p, id, &saved);
    if (cw == NULL || y < 0 || y > 255) return -1;
    struct chunk *c = world_chunk(cw, x >> 4, z >> 4);
    int v = c != NULL ? chunk_cell_sky(c, (x & 15) << 12 | (z & 15) << 8 | y) : -1;
    neth_env = saved;
    return v;
}

/* the cand list's OV_RAYS a kind, nearest first, each tried for a line of sight to its top face (liquids passed); with
 * spread, every (n / OV_RAYS)th of the n by distance (a far lake's near shore is the part a rise hides: of 71 cells in
 * a traced far look the 24 nearest were all hidden) */
#define OV_FAR_RAYS 48          /* obsview_far's lines of sight a kind */
static void ov_sight(struct world *cw, double ex, double ey, double ez, struct ov_cand *cand, const int *nc,
                     struct obsview_hit out[OV_KINDS], int spread)
{
    for (int k = 0; k < OV_KINDS; ++k)
    {
        struct ov_cand *v = cand + k * OV_CAND;
        qsort(v, (size_t)nc[k], sizeof *v, cand_cmp);
        int rays = spread ? OV_FAR_RAYS : OV_RAYS, stride = spread && nc[k] > rays ? nc[k] / rays : 1;
        for (int i = 0; i < nc[k] && i < rays * stride; i += stride)
        {
            double px = v[i].x + 0.5, py = v[i].y + (k == OV_OBSIDIAN || k == OV_HOT ? 0.5 : 0.85), pz = v[i].z + 0.5;
            struct rt_mop m;
            int hit = raytrace_blocks(cw, ex, ey, ez, px, py, pz, 0, 1, 0, &m) && m.type >= 0;
            int seen = k == OV_OBSIDIAN ? hit && m.x == v[i].x && m.y == v[i].y && m.z == v[i].z : !hit;
            if (!seen) continue;
            out[k].x = v[i].x, out[k].y = v[i].y, out[k].z = v[i].z;
            out[k].d = sqrtf(v[i].d2);
            break;
        }
    }
}

static void ov_keep(struct ov_cand *v, int *nc, float d2, int x, int y, int z)
{
    if (*nc < OV_CAND)
    {
        v[(*nc)++] = (struct ov_cand){d2, x, y, z};
        return;
    }
    int w = 0;      /* full: the farthest kept gives way to a nearer one */
    for (int i = 1; i < OV_CAND; ++i)
        if (v[i].d2 > v[w].d2) w = i;
    if (v[w].d2 > d2) v[w] = (struct ov_cand){d2, x, y, z};
}

int obsview_far(struct pipe *p, int id, double ex, double ey, double ez, float yaw, float pitch, int radius, int mask,
                int (*skip)(void *ctx, int kind, int x, int y, int z), void *ctx, struct obsview_hit out[OV_KINDS])
{
    for (int k = 0; k < OV_KINDS; ++k) out[k] = (struct obsview_hit){0, 0, 0, 0, -1.0f};
    struct env *saved;
    struct world *cw = ov_world(p, id, &saved);
    if (cw == NULL) return -1;
    struct ov_cand cand[OV_CAND * OV_KINDS];
    int nc[OV_KINDS] = {0};
    double yr = (double)yaw * M_PI / 180.0, pr = (double)pitch * M_PI / 180.0;
    double lx = -sin(yr) * cos(pr), ly = -sin(pr), lz = cos(yr) * cos(pr), cmin = cos(75.0 * M_PI / 180.0);
    int x0 = (int)floor(ex) - radius, x1 = (int)floor(ex) + radius, z0 = (int)floor(ez) - radius,
        z1 = (int)floor(ez) + radius;
    double r2 = (double)radius * radius;
    for (int cx = x0 >> 4; cx <= x1 >> 4; ++cx)
        for (int cz = z0 >> 4; cz <= z1 >> 4; ++cz)
        {
            struct chunk *c = world_chunk(cw, cx, cz);
            if (c == NULL) continue;
            int ax = cx * 16 > x0 ? cx * 16 : x0, bx = cx * 16 + 15 < x1 ? cx * 16 + 15 : x1;
            int az = cz * 16 > z0 ? cz * 16 : z0, bz = cz * 16 + 15 < z1 ? cz * 16 + 15 : z1;
            for (int z = az; z <= bz; ++z)
                for (int x = ax; x <= bx; ++x)
                {
                    double hx = x + 0.5 - ex, hz = z + 0.5 - ez;
                    if (hx * hx + hz * hz > r2) continue;
                    int h = c->height[(z & 15) << 4 | (x & 15)];
                    for (int y = h + 1; y >= h - OV_FAR_DEPTH && y >= 1; --y)
                    {
                        if (y > 254) continue;
                        int b = chunk_get_block(c, x & 15, y, z & 15) & 4095;
                        int k = b == 8 || b == 9 ? OV_WATER : b == 10 || b == 11 ? OV_LAVA : -1;
                        if (k < 0 || !((mask >> k) & 1)) continue;
                        if (chunk_get_meta(c, x & 15, y, z & 15) != 0) continue;
                        int up = chunk_get_block(c, x & 15, y + 1, z & 15) & 4095;
                        if (up != 0) continue;              /* the top face to air */
                        double px = x + 0.5, py = y + 0.85, pz = z + 0.5;
                        double dx = px - ex, dy = py - ey, dz = pz - ez, d2 = dx * dx + dy * dy + dz * dz;
                        if (d2 < 1e-6 || (dx * lx + dy * ly + dz * lz) / sqrt(d2) < cmin) continue;
                        if (skip && skip(ctx, k, x, y, z)) continue;
                        out[k].n += 1;
                        ov_keep(cand + k * OV_CAND, &nc[k], (float)d2, x, y, z);
                        break;
                    }
                }
        }
    ov_sight(cw, ex, ey, ez, cand, nc, out, 1);
    neth_env = saved;
    return 0;
}

int obsview_scan(struct pipe *p, int id, double ex, double ey, double ez, float yaw, float pitch, int radius, int mask,
                 int (*skip)(void *ctx, int kind, int x, int y, int z), void *ctx, struct obsview_hit out[OV_KINDS])
{
    for (int k = 0; k < OV_KINDS; ++k) out[k] = (struct obsview_hit){0, 0, 0, 0, -1.0f};
    struct env *saved;
    struct world *cw = ov_world(p, id, &saved);
    if (cw == NULL) return -1;
    struct ov_cand cand[OV_CAND * OV_KINDS];
    int nc[OV_KINDS] = {0};
    double yr = (double)yaw * M_PI / 180.0, pr = (double)pitch * M_PI / 180.0;
    double lx = -sin(yr) * cos(pr), ly = -sin(pr), lz = cos(yr) * cos(pr), cmin = cos(75.0 * M_PI / 180.0);
    int x0 = (int)floor(ex) - radius, x1 = (int)floor(ex) + radius, z0 = (int)floor(ez) - radius,
        z1 = (int)floor(ez) + radius;
    int y0 = (int)floor(ey) - radius, y1 = (int)floor(ey) + radius;
    if (y0 < 0) y0 = 0;
    if (y1 > 254) y1 = 254;
    double r2 = (double)radius * radius;
    for (int cx = x0 >> 4; cx <= x1 >> 4; ++cx)
        for (int cz = z0 >> 4; cz <= z1 >> 4; ++cz)
        {
            struct chunk *c = world_chunk(cw, cx, cz);
            if (c == NULL) continue;
            int ax = cx * 16 > x0 ? cx * 16 : x0, bx = cx * 16 + 15 < x1 ? cx * 16 + 15 : x1;
            int az = cz * 16 > z0 ? cz * 16 : z0, bz = cz * 16 + 15 < z1 ? cz * 16 + 15 : z1;
            /* lane/macroperf: the cells of the four kinds' ids by section (ov_gather), in the scan's own order (y, z,
             * x), each then judged as before: the same candidates in the same order, without reading every cell */
            for (int sy = y0 >> 4; sy <= y1 >> 4; ++sy)
            {
                uint64_t bits[64];
                if (!ov_gather(c, sy, ax & 15, bx & 15, az & 15, bz & 15, (sy << 4 > y0 ? sy << 4 : y0) & 15,
                               ((sy << 4) + 15 < y1 ? (sy << 4) + 15 : y1) & 15, bits))
                    continue;
                for (int wi = 0; wi < 64; ++wi)
                    for (uint64_t wb = bits[wi]; wb != 0; wb &= wb - 1)
                    {
                        int key = wi << 6 | __builtin_ctzll(wb);
                        int y = sy << 4 | key >> 8, z = cz << 4 | (key >> 4 & 15), x = cx << 4 | (key & 15);
                        int b = chunk_get_block(c, x & 15, y, z & 15) & 4095;
                        int k0 = b == 8 || b == 9 ? OV_WATER : b == 10 || b == 11 ? OV_LAVA : b == 49 ? OV_OBSIDIAN : -1;
                        if (k0 < 0) continue;
                        /* a lava cell is a candidate of two kinds: the source at its surface, and the hot */
                        for (int pass = 0; pass < 2; ++pass)
                        {
                            int k = pass == 0 ? k0 : k0 == OV_LAVA ? OV_HOT : -1;
                            if (k < 0 || !((mask >> k) & 1)) continue;
                            double px = x + 0.5, py = y + (k == OV_OBSIDIAN || k == OV_HOT ? 0.5 : 0.85), pz = z + 0.5;
                            double dx = px - ex, dy = py - ey, dz = pz - ez, d2 = dx * dx + dy * dy + dz * dz;
                            if (d2 > r2 || d2 < 1e-6) continue;
                            if (k == OV_WATER || k == OV_LAVA)
                            {
                                if (chunk_get_meta(c, x & 15, y, z & 15) != 0) continue;
                                int up = chunk_get_block(c, x & 15, y + 1, z & 15) & 4095;
                                if (k == OV_WATER ? up == 8 || up == 9 : up == 10 || up == 11) continue;
                                /* lane/obsidian4 (OV_EXPOSED): a source with no air beside or over it shows no face,
                                 * and buried ones took all the lines of sight (4,481 of 4,483 looks from the search's
                                 * tunnel had lava candidates, 2 saw one) */
                                if ((mask & OV_EXPOSED) && up != 0 && !ov_air_beside(cw, x, y, z)) continue;
                            }
                            if ((dx * lx + dy * ly + dz * lz) / sqrt(d2) < cmin) continue;
                            if (skip && skip(ctx, k, x, y, z)) continue;
                            out[k].n += 1;
                            struct ov_cand *v = cand + k * OV_CAND;
                            if (nc[k] < OV_CAND)
                                v[nc[k]++] = (struct ov_cand){(float)d2, x, y, z};
                            else
                            {
                                int w = 0;      /* full: the farthest kept gives way to a nearer one */
                                for (int i = 1; i < OV_CAND; ++i)
                                    if (v[i].d2 > v[w].d2) w = i;
                                if (v[w].d2 > (float)d2) v[w] = (struct ov_cand){(float)d2, x, y, z};
                            }
                        }
                    }
            }
        }
    for (int k = 0; k < OV_KINDS; ++k)
    {
        struct ov_cand *v = cand + k * OV_CAND;
        qsort(v, (size_t)nc[k], sizeof *v, cand_cmp);
        for (int i = 0; i < nc[k] && i < OV_RAYS; ++i)
        {
            double px = v[i].x + 0.5, py = v[i].y + (k == OV_OBSIDIAN || k == OV_HOT ? 0.5 : 0.85), pz = v[i].z + 0.5;
            struct rt_mop m;
            int hit = raytrace_blocks(cw, ex, ey, ez, px, py, pz, 0, 1, 0, &m) && m.type >= 0;
            int seen = k == OV_OBSIDIAN ? hit && m.x == v[i].x && m.y == v[i].y && m.z == v[i].z : !hit;
            if (!seen && k == OV_OBSIDIAN && (mask & OV_FACES)) seen = ov_face_seen(cw, ex, ey, ez, v[i].x, v[i].y, v[i].z);
            if (!seen) continue;
            out[k].x = v[i].x, out[k].y = v[i].y, out[k].z = v[i].z;
            out[k].d = sqrtf(v[i].d2);
            break;
        }
    }
    neth_env = saved;
    return 0;
}

static inline int ov_before_yzx(double d1, int x1, int y1, int z1, double d2, int x2, int y2, int z2)
{
    if (d1 != d2) return d1 < d2;
    if (y1 != y2) return y1 < y2;
    if (z1 != z2) return z1 < z2;
    return x1 < x2;
}

int obsview_find_sources(struct pipe *p, int id, int fx, int fy, int fz, int r, int down, int up, int lava,
                         int max_n, int *bx, int *by, int *bz)
{
    struct env *saved;
    struct world *cw = ov_world(p, id, &saved);
    if (cw == NULL) return 0;
    if (max_n <= 0) { neth_env = saved; return 0; }
    int y0 = fy - down, y1 = fy + up;
    if (y0 < 1) y0 = 1;
    if (y1 > 254) y1 = 254;
    if (y0 > y1) { neth_env = saved; return 0; }
    int x0 = fx - r, x1 = fx + r, z0 = fz - r, z1 = fz + r;
    int cx0 = x0 >> 4, cx1 = x1 >> 4, cz0 = z0 >> 4, cz1 = z1 >> 4;
    int ncx = cx1 - cx0 + 1, ncz = cz1 - cz0 + 1;
    if (ncx > 8 || ncz > 8 || max_n > 16)
    {
        /* the per-section masks hold at most 8x8 chunks (OB_ORACLE_R = 40 is at most 6x6) and 16 slots: the caller
         * scans cell by cell instead */
        neth_env = saved;
        return -1;
    }
    struct chunk *chgrid[8][8];
    for (int j = 0; j < ncz; ++j)
        for (int i = 0; i < ncx; ++i)
            chgrid[j][i] = world_chunk(cw, cx0 + i, cz0 + j);
    int nb = 0;
    double bd[16];
    const uint8_t target = lava ? 10 : 8;
#if defined(__AVX512BW__)
    const __m512i vmask = _mm512_set1_epi8((char)0xfe);
    const __m512i vtar = _mm512_set1_epi8((char)target);
#endif
    /* Exact (y asc, z asc, x asc) traversal per section sy using AVX-512BW bitmasks (bit-exact slot eviction). */
    for (int sy = y0 >> 4; sy <= y1 >> 4; ++sy)
    {
        int ylo = (sy << 4) > y0 ? 0 : (y0 & 15);
        int yhi = (sy << 4) + 15 < y1 ? 15 : (y1 & 15);
        uint16_t mask[64][16][16];   /* at most 64 chunks (8x8); OB_ORACLE_R=40 is at most 6x6=36 */
        uint16_t yany[64];
        int any_sec = 0;
        for (int j = 0; j < ncz; ++j)
        {
            int cz = cz0 + j;
            int az = cz * 16 > z0 ? cz * 16 : z0;
            int az1 = cz * 16 + 15 < z1 ? cz * 16 + 15 : z1;
            int laz = az & 15, lbz = az1 & 15;
            for (int i = 0; i < ncx; ++i)
            {
                int ci = j * ncx + i;
                yany[ci] = 0;
                struct chunk *c = chgrid[j][i];
                if (c == NULL || !(c->mask >> sy & 1)) continue;
                const struct chunk_sec *s = chunk_sec_at(c, sy);
                if (s == NULL) continue;
                int cx = cx0 + i;
                int ax = cx * 16 > x0 ? cx * 16 : x0;
                int ax1 = cx * 16 + 15 < x1 ? cx * 16 + 15 : x1;
                int lax = ax & 15, lbx = ax1 & 15;
                const uint8_t *metas = chunk_sec_metas(s);
                int cleared = 0;
#if defined(__AVX512BW__)
                int lz0 = laz & ~3;
                for (int lx = lax; lx <= lbx; ++lx)
                {
                    const uint8_t *plane = s->ids + (lx << 8);
                    for (int lz = lz0; lz <= lbz; lz += 4)
                    {
                        __m512i v = _mm512_loadu_si512((const void *)(plane + (lz << 4)));
                        uint64_t m = (uint64_t)_mm512_cmpeq_epi8_mask(_mm512_and_si512(v, vmask), vtar);
                        while (m)
                        {
                            int bit = __builtin_ctzll(m);
                            m &= m - 1;
                            int zloc = lz + (bit >> 4), yloc = bit & 15;
                            if (zloc < laz || zloc > lbz || yloc < ylo || yloc > yhi) continue;
                            int idx = SEC_XYZ(lx, yloc, zloc);
                            if (s->ids_hi != NULL && (chunk_sec_id(s, idx) & 4095) != (int)(s->ids[idx])) continue;
                            if (nibble_get(metas, idx) != 0) continue;
                            if (!cleared) { memset(mask[ci], 0, sizeof mask[ci]); cleared = 1; }
                            mask[ci][yloc][zloc] |= (uint16_t)(1u << lx);
                            yany[ci] |= (uint16_t)(1u << yloc);
                            any_sec = 1;
                        }
                    }
                }
#else
                for (int lx = lax; lx <= lbx; ++lx)
                    for (int lz = laz; lz <= lbz; ++lz)
                    {
                        const uint8_t *col = s->ids + SEC_XYZ(lx, 0, lz);
                        for (int yloc = ylo; yloc <= yhi; ++yloc)
                        {
                            if ((col[yloc] & 0xfe) != target) continue;
                            int idx = SEC_XYZ(lx, yloc, lz);
                            if (s->ids_hi != NULL && (chunk_sec_id(s, idx) & 4095) != (int)col[yloc]) continue;
                            if (nibble_get(metas, idx) != 0) continue;
                            if (!cleared) { memset(mask[ci], 0, sizeof mask[ci]); cleared = 1; }
                            mask[ci][yloc][lz] |= (uint16_t)(1u << lx);
                            yany[ci] |= (uint16_t)(1u << yloc);
                            any_sec = 1;
                        }
                    }
#endif
            }
        }
        if (!any_sec) continue;
        for (int yloc = ylo; yloc <= yhi; ++yloc)
        {
            int y = (sy << 4) + yloc;
            double dy2 = (double)(y - fy) * (y - fy);
            for (int j = 0; j < ncz; ++j)
            {
                int row_any = 0;
                for (int i = 0; i < ncx; ++i) row_any |= (yany[j * ncx + i] >> yloc) & 1;
                if (!row_any) continue;
                int cz = cz0 + j;
                int az = cz * 16 > z0 ? cz * 16 : z0;
                int az1 = cz * 16 + 15 < z1 ? cz * 16 + 15 : z1;
                int laz = az & 15, lbz = az1 & 15;
                for (int zloc = laz; zloc <= lbz; ++zloc)
                {
                    int z = cz * 16 + zloc;
                    double dz2 = (double)(z - fz) * (z - fz) + dy2;
                    for (int i = 0; i < ncx; ++i)
                    {
                        int ci = j * ncx + i;
                        if (!((yany[ci] >> yloc) & 1)) continue;
                        unsigned m = mask[ci][yloc][zloc];
                        if (!m) continue;
                        int cx = cx0 + i;
                        while (m)
                        {
                            int lx = __builtin_ctz(m);
                            m &= m - 1;
                            int x = cx * 16 + lx;
                            double d = (double)(x - fx) * (x - fx) + dz2;
                            int k = nb < max_n ? nb++ : -1;
                            if (k < 0)
                            {
                                for (int q = 0; q < max_n; ++q)
                                    if (d < bd[q] && (k < 0 || bd[q] > bd[k])) k = q;
                                if (k < 0) continue;
                            }
                            bx[k] = x, by[k] = y, bz[k] = z, bd[k] = d;
                        }
                    }
                }
            }
        }
    }
    neth_env = saved;
    return nb;
}

int obsview_find_diamond(struct pipe *p, int id, int fx, int fy, int fz, int r, int ybot, int ytop,
                         int (*bad)(const void *ctx, int x, int y, int z), const void *ctx, int *ox, int *oy, int *oz)
{
    struct env *saved;
    struct world *cw = ov_world(p, id, &saved);
    if (cw == NULL) return 0;
    if (ybot < 0) ybot = 0;
    if (ytop > 255) ytop = 255;
    int x0 = fx - r, x1 = fx + r, z0 = fz - r, z1 = fz + r;
    int found = 0, best_x = 0, best_y = 0, best_z = 0;
    double bd = 1e30;
#if defined(__AVX512BW__)
    const __m512i v56 = _mm512_set1_epi8(56);
#endif
    for (int cx = x0 >> 4; cx <= x1 >> 4; ++cx)
    {
        int ax = cx * 16 > x0 ? cx * 16 : x0;
        int ax1 = cx * 16 + 15 < x1 ? cx * 16 + 15 : x1;
        int lax = ax & 15, lbx = ax1 & 15;
        for (int cz = z0 >> 4; cz <= z1 >> 4; ++cz)
        {
            struct chunk *c = world_chunk(cw, cx, cz);
            if (c == NULL) continue;
            int az = cz * 16 > z0 ? cz * 16 : z0;
            int az1 = cz * 16 + 15 < z1 ? cz * 16 + 15 : z1;
            int laz = az & 15, lbz = az1 & 15;
            for (int sy = ybot >> 4; sy <= ytop >> 4; ++sy)
            {
                if (!(c->mask >> sy & 1)) continue;
                const struct chunk_sec *s = chunk_sec_at(c, sy);
                if (s == NULL) continue;
                int ylo = (sy << 4) > ybot ? 0 : (ybot & 15);
                int yhi = (sy << 4) + 15 < ytop ? 15 : (ytop & 15);
#if defined(__AVX512BW__)
                int lz0 = laz & ~3;
                for (int lx = lax; lx <= lbx; ++lx)
                {
                    const uint8_t *plane = s->ids + (lx << 8);
                    for (int lz = lz0; lz <= lbz; lz += 4)
                    {
                        __m512i v = _mm512_loadu_si512((const void *)(plane + (lz << 4)));
                        uint64_t m = (uint64_t)_mm512_cmpeq_epi8_mask(v, v56);
                        while (m)
                        {
                            int bit = __builtin_ctzll(m);
                            m &= m - 1;
                            int zloc = lz + (bit >> 4), yloc = bit & 15;
                            if (zloc < laz || zloc > lbz || yloc < ylo || yloc > yhi) continue;
                            int idx = SEC_XYZ(lx, yloc, zloc);
                            if (s->ids_hi != NULL && (chunk_sec_id(s, idx) & 4095) != 56) continue;
                            int x = cx * 16 + lx, y = (sy << 4) + yloc, z = cz * 16 + zloc;
                            double d = (double)(x - fx) * (x - fx) + (double)(z - fz) * (z - fz) + (double)(y - fy) * (y - fy);
                            if (found && !ov_before_yzx(d, x, y, z, bd, best_x, best_y, best_z)) continue;
                            if (bad && bad(ctx, x, y, z)) continue;
                            int hot = 0;
                            for (int k = 0; k < 27 && !hot; ++k)
                            {
                                int nx = x + k % 3 - 1, ny = y + k / 3 % 3 - 1, nz = z + k / 9 - 1;
                                if (ny < 0 || ny > 255) continue;
                                struct chunk *nc = (nx >> 4) == cx && (nz >> 4) == cz ? c : world_chunk(cw, nx >> 4, nz >> 4);
                                if (nc == NULL) continue;
                                int b = chunk_get_block(nc, nx & 15, ny, nz & 15) & 4095;
                                hot = b >= 8 && b <= 11;
                            }
                            if (hot) continue;
                            bd = d, found = 1, best_x = x, best_y = y, best_z = z;
                        }
                    }
                }
#else
                for (int lx = lax; lx <= lbx; ++lx)
                    for (int lz = laz; lz <= lbz; ++lz)
                    {
                        const uint8_t *col = s->ids + SEC_XYZ(lx, 0, lz);
                        for (int yloc = ylo; yloc <= yhi; ++yloc)
                        {
                            if (col[yloc] != 56) continue;
                            int idx = SEC_XYZ(lx, yloc, lz);
                            if (s->ids_hi != NULL && (chunk_sec_id(s, idx) & 4095) != 56) continue;
                            int x = cx * 16 + lx, y = (sy << 4) + yloc, z = cz * 16 + lz;
                            double d = (double)(x - fx) * (x - fx) + (double)(z - fz) * (z - fz) + (double)(y - fy) * (y - fy);
                            if (found && !ov_before_yzx(d, x, y, z, bd, best_x, best_y, best_z)) continue;
                            if (bad && bad(ctx, x, y, z)) continue;
                            int hot = 0;
                            for (int k = 0; k < 27 && !hot; ++k)
                            {
                                int nx = x + k % 3 - 1, ny = y + k / 3 % 3 - 1, nz = z + k / 9 - 1;
                                if (ny < 0 || ny > 255) continue;
                                struct chunk *nc = (nx >> 4) == cx && (nz >> 4) == cz ? c : world_chunk(cw, nx >> 4, nz >> 4);
                                if (nc == NULL) continue;
                                int b = chunk_get_block(nc, nx & 15, ny, nz & 15) & 4095;
                                hot = b >= 8 && b <= 11;
                            }
                            if (hot) continue;
                            bd = d, found = 1, best_x = x, best_y = y, best_z = z;
                        }
                    }
#endif
            }
        }
    }
    neth_env = saved;
    if (found) *ox = best_x, *oy = best_y, *oz = best_z;
    return found;
}
