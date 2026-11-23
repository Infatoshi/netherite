/* lane/stronghold: what the player sees of thrown eyes, the stronghold's blocks and lava (shview.h) */
#define _GNU_SOURCE
#include "shview.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../engine/env.h"
#include "../engine/item_entity.h"
#include "../engine/raytrace.h"
#include "../engine/serverreplay.h"
#include "../engine/session.h"
#include "../engine/world.h"
#include "pipeline.h"
#include "pool.h"

#define SHV_RAYS 24             /* the lines of sight tried a kind, nearest first */
#define SHV_CAND 256            /* the candidates kept a kind */

struct shv_cand { float d2; int32_t x, y, z, b, m; };

/* the kinds of block the scan looks for (brick, frame, lava, the end portal) and the height in the cell it aims at */
#define SHV_KINDS 5
static const double SHV_AIM[SHV_KINDS] = {0.5, 0.5, 0.85, 0.03, 0.5};   /* ... and leaves (SHV_LEAVES) */

static struct session *shv_session(struct pipe *p, int id, struct env **saved)
{
    struct pool *pl = pipe_pool(p);
    struct session *ss = pool_session(pl, id);
    if (ss == NULL || ss->client_world == NULL) return NULL;
    *saved = neth_env;
    neth_env = pool_env(pl, id);
    return ss;
}

/* the eye sees (x, y, z) past every block of the client world, within the cone of the look (lx, ly, lz) */
static int shv_sees(struct world *cw, double ex, double ey, double ez, double lx, double ly, double lz, double x,
                    double y, double z)
{
    double dx = x - ex, dy = y - ey, dz = z - ez, d = sqrt(dx * dx + dy * dy + dz * dz);
    if (d < 1e-6) return 1;
    if ((dx * lx + dy * ly + dz * lz) / d < cos(SHV_CONE * M_PI / 180.0)) return 0;
    struct rt_mop m;
    return !(raytrace_blocks(cw, ex, ey, ez, x, y, z, 0, 1, 0, &m) && m.type >= 0);
}

/* the kept candidates of one kind: the nearest SHV_CAND (the farthest gives way) */
static void cand_put(struct shv_cand *v, int *n, struct shv_cand c)
{
    if (*n < SHV_CAND)
    {
        v[(*n)++] = c;
        return;
    }
    int w = 0;
    for (int i = 1; i < SHV_CAND; ++i)
        if (v[i].d2 > v[w].d2) w = i;
    if (v[w].d2 > c.d2) v[w] = c;
}

/* lane/macroperf: section sy of c, the local box x ax..bx, z az..bz, y ylo..yhi (inside the section): the bit
 * y << 8 | z << 4 | x of bits set for each cell whose id byte wtab marks (ascending bits: the walk's y, z, x order); 0
 * when none. The band is first tested whole for any of wv's nine bytes (vector words; most bands hold none). A section
 * missing or masked off reads air, as chunk_get_block does; an id past 255 is judged by its whole id after */
static int shv_gather(const struct chunk *c, int sy, int ax, int bx, int az, int bz, int ylo, int yhi,
                      const uint8_t wv[9], const uint8_t wtab[256], uint64_t bits[64])
{
    if (!(c->mask >> sy & 1)) return 0;
    const struct chunk_sec *s = chunk_sec_at(c, sy);
    if (s == NULL) return 0;
    unsigned any = 0;
    for (int i = 0; i < SEC_CELLS; ++i)
    {
        uint8_t v = s->ids[i];
        any |= (unsigned)((v == wv[0]) | (v == wv[1]) | (v == wv[2]) | (v == wv[3]) | (v == wv[4]) | (v == wv[5]) |
                          (v == wv[6]) | (v == wv[7]) | (v == wv[8]));
    }
    if (!any) return 0;
    memset(bits, 0, 64 * sizeof *bits);
    int found = 0;
    for (int x = ax; x <= bx; ++x)
        for (int z = az; z <= bz; ++z)
        {
            const uint8_t *col = s->ids + SEC_XYZ(x, 0, z);
            for (int y = ylo; y <= yhi; ++y)
                if (wtab[col[y]])
                {
                    int key = y << 8 | z << 4 | x;
                    bits[key >> 6] |= 1ull << (key & 63);
                    found = 1;
                }
        }
    return found;
}

static void cand_sort(struct shv_cand *v, int n)
{
    for (int i = 1; i < n; ++i)
    {
        struct shv_cand c = v[i];
        int j = i;
        while (j > 0 && v[j - 1].d2 > c.d2) v[j] = v[j - 1], --j;
        v[j] = c;
    }
}

int shview_scan(struct pipe *p, int id, double ex, double ey, double ez, float yaw, float pitch, int radius, int mask,
                struct shview *out)
{
    memset(out, 0, sizeof *out);
    out->bd = out->ld = -1.0f;
    struct env *saved;
    struct session *ss = shv_session(p, id, &saved);
    if (ss == NULL) return -1;
    struct world *cw = ss->client_world;
    double yr = (double)yaw * M_PI / 180.0, pr = (double)pitch * M_PI / 180.0;
    double lx = -sin(yr) * cos(pr), ly = -sin(pr), lz = cos(yr) * cos(pr);
    if ((mask & (SHV_EYE | SHV_DROP)) && ss->sr != NULL && ss->sr->d != NULL)
    {
        /* the thrown eye lives in the living world's item list (throw.c), its drop in either list */
        ie_world *lists[2] = {&ss->sr->d->anw.iew, &ss->sr->d->iew};
        double best = 1e30;
        for (int L = 0; L < 2; ++L)
            for (int i = 0; i < lists[L]->n; ++i)
            {
                const struct ie_ent *en = ie_ent_at(lists[L]->slot[i]);
                if (en == NULL || en->is_dead) continue;
                double x = en->e.pos_x, y = en->e.pos_y, z = en->e.pos_z;
                if ((mask & SHV_EYE) && en->kind == IE_ENDER_EYE && en->entity_id > out->eye_id)
                {
                    double dx = x - ex, dy = y - ey, dz = z - ez;
                    if (dx * dx + dy * dy + dz * dz < 64.0 * 64.0 && shv_sees(cw, ex, ey, ez, lx, ly, lz, x, y, z))
                    {
                        out->eye_id = en->entity_id, out->eye_x = x, out->eye_y = y, out->eye_z = z;
#ifdef SH_TRACE
                        out->trace_drop = en->eye_drop;
#endif
                    }
                }
                if ((mask & SHV_DROP) && en->kind == IE_ITEM && en->stack_item == 381)
                {
                    double dx = x - ex, dy = y + 0.125 - ey, dz = z - ez, d2 = dx * dx + dy * dy + dz * dz;
                    if (d2 < best && d2 < 32.0 * 32.0 && shv_sees(cw, ex, ey, ez, lx, ly, lz, x, y + 0.125, z))
                    {
                        best = d2;
                        out->drop_n = 1, out->drop_x = x, out->drop_y = y, out->drop_z = z;
                    }
                }
            }
    }
    if (mask & (SHV_BLOCKS | SHV_LEAVES))
    {
        /* on the heap: libneth's thread-locals stay within the 512 bytes a loaded library may use (rlbind.mk) */
        struct shv_cand(*cand)[SHV_CAND] = malloc(sizeof(struct shv_cand[SHV_KINDS][SHV_CAND]));
        if (cand == NULL)
        {
            neth_env = saved;
            return -1;
        }
        int nc[SHV_KINDS] = {0};
        int x0 = (int)floor(ex) - radius, x1 = (int)floor(ex) + radius, z0 = (int)floor(ez) - radius,
            z1 = (int)floor(ez) + radius, y0 = (int)floor(ey) - radius, y1 = (int)floor(ey) + radius;
        if (y0 < 0) y0 = 0;
        if (y1 > 255) y1 = 255;
        double r2 = (double)radius * radius, cmin = cos(SHV_CONE * M_PI / 180.0);
        /* lane/macroperf: the id bytes of the kinds the mask asks for (shv_gather) */
        static const uint8_t BLOCKS[7] = {98, 97, 101, 120, 10, 11, 119}, LEAVES[2] = {18, 161};
        uint8_t wv[9], wtab[256] = {0};
        memset(wv, (mask & SHV_BLOCKS) ? BLOCKS[0] : LEAVES[0], sizeof wv);   /* the unused ones repeat a wanted one */
        int nw = 0;
        for (int i = 0; i < 7 && (mask & SHV_BLOCKS); ++i) wv[nw++] = BLOCKS[i];
        for (int i = 0; i < 2 && (mask & SHV_LEAVES); ++i) wv[nw++] = LEAVES[i];
        for (int i = 0; i < 9; ++i) wtab[wv[i]] = 1;
        for (int cx = x0 >> 4; cx <= x1 >> 4; ++cx)
            for (int cz = z0 >> 4; cz <= z1 >> 4; ++cz)
            {
                struct chunk *c = world_chunk(cw, cx, cz);
                if (c == NULL) continue;
                int ax = cx * 16 > x0 ? cx * 16 : x0, bx = cx * 16 + 15 < x1 ? cx * 16 + 15 : x1;
                int az = cz * 16 > z0 ? cz * 16 : z0, bz = cz * 16 + 15 < z1 ? cz * 16 + 15 : z1;
                /* lane/macroperf: the cells whose id byte is a wanted kind's, band by band in the walk's own order
                 * (y, z, x): the same candidates in the same order, without reading every cell */
                for (int sy = y0 >> 4; sy <= y1 >> 4; ++sy)
                {
                    uint64_t bits[64];
                    if (!shv_gather(c, sy, ax & 15, bx & 15, az & 15, bz & 15, (sy << 4 > y0 ? sy << 4 : y0) & 15,
                                    ((sy << 4) + 15 < y1 ? (sy << 4) + 15 : y1) & 15, wv, wtab, bits))
                        continue;
                    for (int wi = 0; wi < 64; ++wi)
                        for (uint64_t wb = bits[wi]; wb != 0; wb &= wb - 1)
                        {
                            int key = wi << 6 | __builtin_ctzll(wb);
                            int y = sy << 4 | key >> 8, z = cz << 4 | (key >> 4 & 15), x = cx << 4 | (key & 15);
                            int b = chunk_get_block(c, x & 15, y, z & 15) & 4095;
                            int k = b == 98 || b == 97 || b == 101 ? 0
                                    : b == 120                     ? 1
                                    : b == 10 || b == 11           ? 2
                                    : b == 119                     ? 3
                                    : b == 18 || b == 161          ? 4
                                                                   : -1;
                            if (k < 0 || (k < 4 && !(mask & SHV_BLOCKS))) continue;
                            if (k == 4 && (!(mask & SHV_LEAVES) || y < ey - 2.0 ||
                                           hypot(x + 0.5 - ex, z + 0.5 - ez) < SHV_SITE_IN))
                                continue;
                            double px = x + 0.5, py = y + SHV_AIM[k], pz = z + 0.5;
                            double dx = px - ex, dy = py - ey, dz = pz - ez, d2 = dx * dx + dy * dy + dz * dz;
                            if (d2 > r2 || d2 < 1e-6 || (dx * lx + dy * ly + dz * lz) / sqrt(d2) < cmin) continue;
                            if (k == 2 && (chunk_get_block(c, x & 15, y + 1 > 255 ? 255 : y + 1, z & 15) & 4095) ==
                                              (unsigned)b)
                                continue;   /* lava under lava: its top face is the one seen */
                            cand_put(cand[k], &nc[k],
                                     (struct shv_cand){(float)d2, x, y, z, b, chunk_get_meta(c, x & 15, y, z & 15)});
                        }
                }
            }
        for (int k = 0; k < SHV_KINDS; ++k)
        {
            cand_sort(cand[k], nc[k]);
            int tried = 0;
            for (int i = 0; i < nc[k] && tried < (k == 1 ? 3 * SHV_RAYS : SHV_RAYS); ++i, ++tried)
            {
                const struct shv_cand *v = &cand[k][i];
                double px = v->x + 0.5, py = v->y + SHV_AIM[k], pz = v->z + 0.5;
                struct rt_mop m;
                int hit = raytrace_blocks(cw, ex, ey, ez, px, py, pz, 0, 1, 0, &m) && m.type >= 0;
                int self = hit && m.x == v->x && m.y == v->y && m.z == v->z;
                /* a block: the line's first block is the cell itself; lava: nothing solid before its top; the portal
                 * (1/16 high): the cell itself or nothing before its top */
                if (k == 2 ? hit : k == 3 ? hit && !self : !self) continue;
                if (k == 4)
                {
                    out->leaves_x = (out->leaves_x * out->leaves_n + v->x + 0.5) / (out->leaves_n + 1);
                    out->leaves_z = (out->leaves_z * out->leaves_n + v->z + 0.5) / (out->leaves_n + 1);
                    out->leaves_n += 1;
                    continue;
                }
                if (k == 3 && !out->portal_n)
                {
                    out->portal_n = 1, out->px = v->x, out->py = v->y, out->pz = v->z;
                    break;
                }
                if (k == 0 && !out->brick_n)
                {
                    out->brick_n = 1, out->bx = v->x, out->by = v->y, out->bz = v->z, out->bblock = v->b;
                    out->bd = sqrtf(v->d2);
                    break;
                }
                if (k == 2 && !out->lava_n)
                {
                    out->lava_n = 1, out->lx = v->x, out->ly = v->y, out->lz = v->z;
                    out->ld = sqrtf(v->d2);
                    break;
                }
                if (k == 1 && out->nframe < SHV_FRAMES)
                {
                    int j = out->nframe++;
                    out->frame[j].x = v->x, out->frame[j].y = v->y, out->frame[j].z = v->z, out->frame[j].meta = v->m;
                }
            }
        }
        free(cand);
    }
    neth_env = saved;
    return 0;
}

#ifdef SH_TRACE
/* debugging only (never in the macro's decisions): the client world's block at a cell */
int shview_block_trace(struct pipe *p, int id, int x, int y, int z)
{
    struct env *saved;
    struct session *ss = shv_session(p, id, &saved);
    if (ss == NULL) return -2;
    struct chunk *c = world_chunk(ss->client_world, x >> 4, z >> 4);
    int b = c ? chunk_get_block(c, x & 15, y, z & 15) & 4095 : -1;
    neth_env = saved;
    return b;
}
#endif

int shview_ray(struct pipe *p, int id, double ex, double ey, double ez, double tx, double ty, double tz, int *x, int *y,
               int *z, int *block, int *meta)
{
    struct env *saved;
    struct session *ss = shv_session(p, id, &saved);
    if (ss == NULL) return -1;
    struct rt_mop m;
    int rc = 0;
    if (raytrace_blocks(ss->client_world, ex, ey, ez, tx, ty, tz, 1, 0, 0, &m) && m.type >= 0)
    {
        rc = 1;
        *x = m.x, *y = m.y, *z = m.z;
        struct chunk *c = world_chunk(ss->client_world, m.x >> 4, m.z >> 4);
        *block = c ? chunk_get_block(c, m.x & 15, m.y, m.z & 15) & 4095 : -1;
        *meta = c ? chunk_get_meta(c, m.x & 15, m.y, m.z & 15) : 0;
    }
    neth_env = saved;
    return rc;
}
