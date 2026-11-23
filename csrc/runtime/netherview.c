/* lane/nether: what the player sees of the Nether's fortress blocks, its portal and its lava (netherview.h) */
#define _GNU_SOURCE
#include "netherview.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <string.h>

#include "../engine/env.h"
#include "../engine/fortress.h"
#include "../engine/item_entity.h"
#include "../engine/populate.h"
#include "../engine/raytrace.h"
#include "../engine/serverreplay.h"
#include "../engine/session.h"
#include "../engine/structure.h"
#include "../engine/world.h"
#include "pipeline.h"
#include "pool.h"

#define NV_CAND 64              /* the candidates kept a kind (the nearest) */
#define NV_RAYS 16              /* the lines of sight tried a kind, nearest first */

struct nv_cand { float d2; int32_t x, y, z; };

static int cand_cmp(const void *a, const void *b)
{
    float x = ((const struct nv_cand *)a)->d2, y = ((const struct nv_cand *)b)->d2;
    return x < y ? -1 : x > y;
}

static struct world *nv_world(struct pipe *p, int id, struct env **saved)
{
    struct pool *pl = pipe_pool(p);
    struct session *ss = pool_session(pl, id);
    if (ss == NULL || ss->client_world == NULL) return NULL;
    *saved = neth_env;
    neth_env = pool_env(pl, id);
    return ss->client_world;
}

int netherview_block(struct pipe *p, int id, int x, int y, int z, int *meta)
{
    struct env *saved;
    if (y < 0 || y > 255) return -1;   /* before nv_world: a return after it would leave neth_env on env id */
    struct world *cw = nv_world(p, id, &saved);
    if (cw == NULL) return -1;
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

int netherview_seen(struct pipe *p, int id, double ex, double ey, double ez, int x, int y, int z)
{
    struct env *saved;
    struct world *cw = nv_world(p, id, &saved);
    if (cw == NULL) return 0;
    struct rt_mop m;
    int hit = raytrace_blocks(cw, ex, ey, ez, x + 0.5, y + 0.5, z + 0.5, 0, 1, 0, &m) && m.type >= 0;
    neth_env = saved;
    return !hit || (m.x == x && m.y == y && m.z == z);
}

static int nv_open(int b) { return b == 0 || b == 39 || b == 40 || b == 31 || b == 78; }

static int nv_get(struct world *cw, int x, int y, int z)
{
    if (y < 0 || y > 255) return -1;
    struct chunk *c = world_chunk(cw, x >> 4, z >> 4);
    return c ? chunk_get_block(c, x & 15, y, z & 15) & 4095 : -1;
}

int netherview_fan(struct pipe *p, int id, double ex, double ey, double ez, float yaw, float pitch, int range,
                   int nyaw, int npitch, float hfov, float vfov, struct netherview_fan *out)
{
    out->nfloor = 0, out->nlava = 0;
    for (int k = 0; k < NV_KINDS; ++k) out->fort[k] = (struct netherview_hit){0, 0, 0, 0, -1.0f};
    out->top = (struct netherview_hit){0, 0, 0, 0, -1.0f};
    struct env *saved;
    struct world *cw = nv_world(p, id, &saved);
    if (cw == NULL) return -1;
    for (int j = 0; j < npitch; ++j)
        for (int i = 0; i < nyaw; ++i)
        {
            double yw = (double)yaw + (nyaw > 1 ? hfov * ((double)i / (nyaw - 1) - 0.5) : 0.0);
            double pt = (double)pitch + (npitch > 1 ? vfov * ((double)j / (npitch - 1) - 0.5) : 0.0);
            if (pt > 89.0) pt = 89.0;
            if (pt < -89.0) pt = -89.0;
            double yr = yw * M_PI / 180.0, pr = pt * M_PI / 180.0;
            double lx = -sin(yr) * cos(pr), ly = -sin(pr), lz = cos(yr) * cos(pr);
            struct rt_mop m;
            int hit = raytrace_blocks(cw, ex, ey, ez, ex + lx * range, ey + ly * range, ez + lz * range, 0, 1, 0, &m) &&
                      m.type >= 0;
            double hd = hit ? sqrt((m.hx - ex) * (m.hx - ex) + (m.hy - ey) * (m.hy - ey) + (m.hz - ez) * (m.hz - ez))
                            : (double)range;
            /* lava's surface on the way: the first cell of lava the line enters from above (a floor of lava, seen) */
            int lava = 0;
            if (ly < -0.02)
            {
                int ly0 = (int)floor(ey), ly1 = (int)floor(ey + ly * hd);
                for (int y = ly0; y >= ly1 && y >= 1 && !lava; --y)
                {
                    double tt = (y + 1 - ey) / ly;     /* where the line crosses the top of cell row y */
                    if (tt < 0 || tt > hd) continue;
                    int x = (int)floor(ex + lx * tt), z = (int)floor(ez + lz * tt);
                    int b = nv_get(cw, x, y, z);
                    if (b == 10 || b == 11)
                    {
                        lava = 1;
                        out->nlava += 1;
                        if (out->nfloor < NV_FLOORS && nv_open(nv_get(cw, x, y + 1, z)) && nv_open(nv_get(cw, x, y + 2, z)))
                            out->floor[out->nfloor++] = (struct netherview_floor){x, y, z, 1, (int16_t)(j * nyaw + i),
                                                                                  (float)tt, b};
                    }
                }
            }
            if (!hit || lava) continue;
            int b = nv_get(cw, m.x, m.y, m.z);
            int k = b == 52 ? NV_SPAWNER : b == 113 ? NV_FENCE : b == 112 || b == 114 ? NV_BRICK : -1;
            if (k == NV_FENCE || k == NV_BRICK)
            {
                out->top.n += 1;
                if (out->top.d < 0 || m.y > out->top.y)
                    out->top.x = m.x, out->top.y = m.y, out->top.z = m.z, out->top.d = (float)hd;
            }
            if (k >= 0)
            {
                out->fort[k].n += 1;
                if (out->fort[k].d < 0 || hd < out->fort[k].d)
                    out->fort[k].x = m.x, out->fort[k].y = m.y, out->fort[k].z = m.z, out->fort[k].d = (float)hd;
            }
            if (m.face == 1 && out->nfloor < NV_FLOORS && b != 51 && nv_open(nv_get(cw, m.x, m.y + 1, m.z)) &&
                nv_open(nv_get(cw, m.x, m.y + 2, m.z)))
                out->floor[out->nfloor++] = (struct netherview_floor){m.x, m.y, m.z, 0, (int16_t)(j * nyaw + i), (float)hd,
                                                                      b};
        }
    neth_env = saved;
    return 0;
}

static int nv_kind(int b)
{
    return b == 52 ? NV_SPAWNER : b == 113 ? NV_FENCE : b == 112 ? NV_BRICK : b == 90 ? NV_PORTAL
           : b == 10 || b == 11 ? NV_LAVA : -1;
}

/* lane/macroperf: any of a band's id bytes one of wv's six (one pass over the band in vector words: most bands hold
 * none, and the cell walk below is skipped for them; a candidate's id byte is always one of them) */
static int nv_band_any(const uint8_t *ids, const uint8_t wv[6])
{
    const uint8_t a = wv[0], b = wv[1], c = wv[2], d = wv[3], e = wv[4], f = wv[5];
    unsigned any = 0;
    for (int i = 0; i < SEC_CELLS; ++i)
    {
        uint8_t x = ids[i];
        any |= (unsigned)((x == a) | (x == b) | (x == c) | (x == d) | (x == e) | (x == f));
    }
    return any != 0;
}

int netherview_scan(struct pipe *p, int id, double ex, double ey, double ez, float yaw, float pitch, int radius,
                    int mask, struct netherview_hit out[NV_KINDS])
{
    for (int k = 0; k < NV_KINDS; ++k) out[k] = (struct netherview_hit){0, 0, 0, 0, -1.0f};
    struct env *saved;
    struct world *cw = nv_world(p, id, &saved);
    if (cw == NULL) return -1;
    struct nv_cand cand[NV_KINDS][NV_CAND];
    int nc[NV_KINDS] = {0};
    double yr = (double)yaw * M_PI / 180.0, pr = (double)pitch * M_PI / 180.0;
    double lx = -sin(yr) * cos(pr), ly = -sin(pr), lz = cos(yr) * cos(pr), cmin = cos(75.0 * M_PI / 180.0);
    int x0 = (int)floor(ex) - radius, x1 = (int)floor(ex) + radius, z0 = (int)floor(ez) - radius,
        z1 = (int)floor(ez) + radius;
    int y0 = (int)floor(ey) - radius, y1 = (int)floor(ey) + radius;
    if (y0 < 0) y0 = 0;
    if (y1 > 254) y1 = 254;
    double r2 = (double)radius * radius;
    /* lane/nether2: the kinds wanted as a byte table, the bands read as bytes (a band's ids in x, z, y order); a
     * band with ids past 255 (none in the Nether) read through chunk_get_block. A kind's candidates are a max-heap
     * on the distance (the farthest kept gives way to a nearer one in log time: lava came in thousands) */
    int8_t want[256];
    uint8_t wv[6];                  /* lane/macroperf: the wanted bytes (at most six; the rest repeat the first) */
    int nw = 0;
    for (int b = 0; b < 256; ++b)
    {
        int k = nv_kind(b);
        want[b] = (int8_t)(k >= 0 && ((mask >> k) & 1) ? k : -1);
        if (want[b] >= 0 && nw < 6) wv[nw++] = (uint8_t)b;
    }
    if (nw == 0)
    {
        neth_env = saved;
        return 0;
    }
    for (int i = nw; i < 6; ++i) wv[i] = wv[0];
    for (int cx = x0 >> 4; cx <= x1 >> 4; ++cx)
        for (int cz = z0 >> 4; cz <= z1 >> 4; ++cz)
        {
            struct chunk *c = world_chunk(cw, cx, cz);
            if (c == NULL) continue;
            int ax = cx * 16 > x0 ? cx * 16 : x0, bx = cx * 16 + 15 < x1 ? cx * 16 + 15 : x1;
            int az = cz * 16 > z0 ? cz * 16 : z0, bz = cz * 16 + 15 < z1 ? cz * 16 + 15 : z1;
            for (int sy = y0 >> 4; sy <= y1 >> 4; ++sy)
            {
                if (!((c->mask >> sy) & 1)) continue;
                const struct chunk_sec *sec = chunk_sec_at(c, sy);
                if (sec == NULL || !nv_band_any(sec->ids, wv)) continue;   /* lane/macroperf: none of the kinds */
                int ay = sy * 16 > y0 ? sy * 16 : y0, by = sy * 16 + 15 < y1 ? sy * 16 + 15 : y1;
                for (int x = ax; x <= bx; ++x)
                    for (int z = az; z <= bz; ++z)
                    {
                        const uint8_t *col = sec->ids + ((x & 15) << 8 | (z & 15) << 4);
                        for (int y = ay; y <= by; ++y)
                        {
                            int k = want[col[y & 15]];
                            if (k < 0) continue;
                            if (sec->ids_hi != NULL && (chunk_get_block(c, x & 15, y, z & 15) & 4095) != col[y & 15])
                                continue;
                            double dx = x + 0.5 - ex, dy = y + 0.5 - ey, dz = z + 0.5 - ez,
                                   d2 = dx * dx + dy * dy + dz * dz;
                            if (d2 > r2 || d2 < 1e-6) continue;
                            if ((dx * lx + dy * ly + dz * lz) / sqrt(d2) < cmin) continue;
                            out[k].n += 1;
                            struct nv_cand *v = cand[k], nv = {(float)d2, x, y, z};
                            if (nc[k] < NV_CAND)
                            {
                                int i = nc[k]++;    /* sift up */
                                while (i > 0 && v[(i - 1) / 2].d2 < nv.d2) v[i] = v[(i - 1) / 2], i = (i - 1) / 2;
                                v[i] = nv;
                            }
                            else if (v[0].d2 > nv.d2)
                            {
                                int i = 0;          /* the root (farthest) replaced, sift down */
                                for (;;)
                                {
                                    int l = 2 * i + 1, m = l;
                                    if (l >= NV_CAND) break;
                                    if (l + 1 < NV_CAND && v[l + 1].d2 > v[l].d2) m = l + 1;
                                    if (v[m].d2 <= nv.d2) break;
                                    v[i] = v[m], i = m;
                                }
                                v[i] = nv;
                            }
                        }
                    }
            }
        }
    for (int k = 0; k < NV_KINDS; ++k)
    {
        struct nv_cand *v = cand[k];
        qsort(v, (size_t)nc[k], sizeof *v, cand_cmp);
        for (int i = 0; i < nc[k] && i < NV_RAYS; ++i)
        {
            double px = v[i].x + 0.5, py = v[i].y + 0.5, pz = v[i].z + 0.5;
            struct rt_mop m;
            /* lane/nether3: a solid kind's line of sight stops at a liquid (no brick seen through the lava sea) */
            int hit = raytrace_blocks(cw, ex, ey, ez, px, py, pz, k <= NV_BRICK, 1, 0, &m) && m.type >= 0;
            /* a solid kind: the ray's first block the cell itself; lava and the portal (no box): nothing before it */
            int self = hit && m.x == v[i].x && m.y == v[i].y && m.z == v[i].z;
            int seen = k == NV_LAVA ? !hit || self : k == NV_PORTAL ? !hit || self : self;
            if (k == NV_PORTAL && hit && !self && abs(m.x - v[i].x) <= 1 && abs(m.y - v[i].y) <= 1 &&
                abs(m.z - v[i].z) <= 1)
            {
                /* seen edge on: the ray ends on the portal's own frame (obsidian) or another of its cells beside it */
                struct chunk *hc = world_chunk(cw, m.x >> 4, m.z >> 4);
                int hb = hc && m.y >= 0 && m.y < 256 ? chunk_get_block(hc, m.x & 15, m.y, m.z & 15) & 4095 : -1;
                seen = hb == 49 || hb == 90;
            }
            if (!seen) continue;
            out[k].x = v[i].x, out[k].y = v[i].y, out[k].z = v[i].z;
            out[k].d = sqrtf(v[i].d2);
            break;
        }
    }
    neth_env = saved;
    return 0;
}

/* lane/nether4: the ghasts and fireballs on the screen (netherview.h netherview_threats) */
#include "../engine/combat.h"
#include "../engine/living.h"
#include "../engine/player.h"

int netherview_threats(struct pipe *p, int id, double ex, double ey, double ez, float yaw, float pitch,
                       struct netherview_threats *out)
{
    out->nghast = 0, out->nball = 0, out->nblaze = 0, out->blaze_id = 0;
    out->ghast_d = -1.0f, out->blaze_d = -1.0f;
    struct pool *pl = pipe_pool(p);
    struct session *ss = pool_session(pl, id);
    if (ss == NULL || ss->client_world == NULL || ss->cp.combat == NULL) return -1;
    struct env *saved = neth_env;
    neth_env = pool_env(pl, id);
    struct world *bw = ss->client_world;
    const struct clientworld *cw = &ss->cp.combat->client;
    double yr = (double)yaw * M_PI / 180.0, pr = (double)pitch * M_PI / 180.0;
    double lx = -sin(yr) * cos(pr), ly = -sin(pr), lz = cos(yr) * cos(pr), cmin = cos(NV_THREAT_CONE * M_PI / 180.0);
    for (int i = 0; i < cw->nents; ++i)
    {
        const struct client_entity *ce = &cw->ents[i];
        if (ce->is_dead) continue;
        int ghast = ce->is_living && !ce->dying && ce->kind == GK_GHAST, ball = !ce->is_living && ce->is_fireball;
        int blaze = ce->is_living && !ce->dying && ce->kind == HK_BLAZE;
        if (!ghast && !ball && !blaze) continue;
        double my = ce->y + (ball ? 0.0 : 0.5 * ce->height), dx = ce->x - ex, dy = my - ey, dz = ce->z - ez;
        double d2 = dx * dx + dy * dy + dz * dz, r = ghast ? NV_GHAST_R : blaze ? NV_BLAZE_R : NV_BALL_R;
        /* (a client fireball whose spawn packet had no motion has a NaN acceleration, 0 / 0 in EntityFireball's
         * constructor as in Java's, and so a NaN place after its first update: no compare against it is true, so
         * "d2 > r * r" let it through to the macros, whose look at it was a NaN yaw; it is not in anyone's view) */
        if (!(d2 <= r * r) || d2 < 1e-6) continue;
        double d = sqrt(d2);
        if ((dx * lx + dy * ly + dz * lz) / d < cmin) continue;
        struct rt_mop m;
        if (raytrace_blocks(bw, ex, ey, ez, ce->x, my, ce->z, 0, 1, 0, &m) && m.type >= 0) continue;
        if (blaze)
        {
            out->nblaze += 1;
            if (out->blaze_d < 0 || d < out->blaze_d)
                out->blaze_d = (float)d, out->bx = (float)ce->x, out->by = (float)my, out->bz = (float)ce->z,
                out->blaze_id = ce->id;
            continue;
        }
        if (ghast)
        {
            out->nghast += 1;
            if (out->ghast_d < 0 || d < out->ghast_d)
                out->ghast_d = (float)d, out->gx = (float)ce->x, out->gy = (float)my, out->gz = (float)ce->z;
            continue;
        }
        int k = out->nball < NV_BALLS ? out->nball : NV_BALLS - 1;
        if (out->nball == NV_BALLS && d >= out->ball[k].d) continue;
        while (k > 0 && out->ball[k - 1].d > d)
        {
            out->ball[k] = out->ball[k - 1];
            --k;
        }
        out->ball[k].id = ce->id, out->ball[k].large = ce->is_large_fireball;
        out->ball[k].x = (float)ce->x, out->ball[k].y = (float)ce->y, out->ball[k].z = (float)ce->z;
        out->ball[k].mx = (float)(ce->x - ce->prev_x), out->ball[k].my = (float)(ce->y - ce->prev_y);
        out->ball[k].mz = (float)(ce->z - ce->prev_z), out->ball[k].d = (float)d;
        if (out->nball < NV_BALLS) out->nball += 1;
    }
    neth_env = saved;
    return 0;
}

int netherview_mouth_blaze(struct pipe *p, int id, double ex, double ey, double ez, float yaw, float pitch, double rmin,
                           double rmax, struct netherview_eye *out)
{
    struct pool *pl = pipe_pool(p);
    struct session *ss = pool_session(pl, id);
    if (ss == NULL || ss->client_world == NULL || ss->cp.combat == NULL) return -1;
    struct env *saved = neth_env;
    neth_env = pool_env(pl, id);
    const struct clientworld *cw = &ss->cp.combat->client;
    double yr = (double)yaw * M_PI / 180.0, pr = (double)pitch * M_PI / 180.0;
    double lx = -sin(yr) * cos(pr), ly = -sin(pr), lz = cos(yr) * cos(pr), cmin = cos(NV_THREAT_CONE * M_PI / 180.0);
    int fx = (int)floor(ex), fy = (int)floor(ey - 1.62 + 0.13), fz = (int)floor(ez);
    double best = -1.0;
    for (int i = 0; i < cw->nents; ++i)
    {
        const struct client_entity *ce = &cw->ents[i];
        if (ce->is_dead || !ce->is_living || ce->dying || ce->kind != HK_BLAZE) continue;
        double my = ce->y + 0.5 * ce->height, dx = ce->x - ex, dz = ce->z - ez, dmy = my - ey;
        double dm = sqrt(dx * dx + dmy * dmy + dz * dz);
        if (dm < 1e-3 || (dx * lx + dmy * ly + dz * lz) / dm < cmin) continue;   /* on the screen (the vis cone) */
        double hy = ce->y + 0.85 * ce->height, dy = hy - ey, d = sqrt(dx * dx + dy * dy + dz * dz);
        if (d <= rmin || d > rmax || (best >= 0.0 && d >= best)) continue;
        struct rt_mop m;
        if (raytrace_blocks(ss->client_world, ex, ey, ez, ce->x, hy, ce->z, 0, 1, 0, &m) && m.type >= 0) continue;
        /* the ray's first cell outside the player's feet and head cells: the one over the head */
        int over = 0;
        for (double t = 0.05; t < d; t += 0.05)
        {
            int cx = (int)floor(ex + dx * t / d), cy = (int)floor(ey + dy * t / d), cz = (int)floor(ez + dz * t / d);
            if (cx == fx && cz == fz && (cy == fy || cy == fy + 1)) continue;
            over = cx == fx && cy == fy + 2 && cz == fz;
            break;
        }
        if (!over) continue;
        best = d;
        out->id = ce->id, out->x = (float)ce->x, out->y = (float)hy, out->z = (float)ce->z, out->d = (float)d;
        out->mx = out->x, out->my = out->y, out->mz = out->z;
    }
    neth_env = saved;
    return best >= 0.0;
}

int netherview_eye_blaze(struct pipe *p, int id, double ex, double ey, double ez, float yaw, float pitch, double r,
                         int margin, struct netherview_eye *out)
{
    struct pool *pl = pipe_pool(p);
    struct session *ss = pool_session(pl, id);
    if (ss == NULL || ss->client_world == NULL || ss->cp.combat == NULL) return -1;
    struct env *saved = neth_env;
    neth_env = pool_env(pl, id);
    const struct clientworld *cw = &ss->cp.combat->client;
    double yr = (double)yaw * M_PI / 180.0, pr = (double)pitch * M_PI / 180.0;
    double lx = -sin(yr) * cos(pr), ly = -sin(pr), lz = cos(yr) * cos(pr), cmin = cos(NV_THREAT_CONE * M_PI / 180.0);
    double best = -1.0;
    for (int i = 0; i < cw->nents; ++i)
    {
        const struct client_entity *ce = &cw->ents[i];
        if (ce->is_dead || !ce->is_living || ce->dying || ce->kind != HK_BLAZE) continue;
        double my = ce->y + 0.5 * ce->height, dx = ce->x - ex, dy = my - ey, dz = ce->z - ez;
        double dm = sqrt(dx * dx + dy * dy + dz * dz);
        /* the vis list's own reach and cone, from the middle */
        if (dm > r || dm < 1e-3 || (dx * lx + dy * ly + dz * lz) / dm < cmin) continue;
        double hy = ce->y + 0.85 * ce->height, d = sqrt(dx * dx + (hy - ey) * (hy - ey) + dz * dz);
        if (best >= 0.0 && d >= best) continue;
        struct rt_mop m;
        /* its middle hidden (the vis list does not hold it) and its eye in view */
        if (!(raytrace_blocks(ss->client_world, ex, ey, ez, ce->x, my, ce->z, 0, 1, 0, &m) && m.type >= 0)) continue;
        if (raytrace_blocks(ss->client_world, ex, ey, ez, ce->x, hy, ce->z, 0, 1, 0, &m) && m.type >= 0) continue;
        best = d;
        out->id = ce->id, out->x = (float)ce->x, out->y = (float)hy, out->z = (float)ce->z, out->d = (float)d;
        out->mx = (float)ce->x, out->my = (float)hy, out->mz = (float)ce->z;
        if (!margin) continue;
        /* the aim with a margin: a grid over the box (x and z -0.25..0.25 by 0.125, height 0.55..0.95 of it by 0.1),
         * the clear points; the one with the most clear neighbours (the six beside it in the grid), the nearest the
         * eye's place on a tie (the line to the eye alone grazes the hole's rim: the look a fraction of a degree off
         * it put the crosshair on the brick 15,000 of 20,600 strike steps) */
        enum { GX = 5, GY = 5 };
        unsigned char clear[GX][GX][GY];
        for (int ix = 0; ix < GX; ++ix)
            for (int iz = 0; iz < GX; ++iz)
                for (int iy = 0; iy < GY; ++iy)
                {
                    double px = ce->x - 0.25 + 0.125 * ix, pz = ce->z - 0.25 + 0.125 * iz,
                           py = ce->y + (0.55 + 0.1 * iy) * ce->height;
                    clear[ix][iz][iy] = !(raytrace_blocks(ss->client_world, ex, ey, ez, px, py, pz, 0, 1, 0, &m) &&
                                          m.type >= 0);
                }
        int bs = -1;
        double bt = 0.0;
        for (int ix = 0; ix < GX; ++ix)
            for (int iz = 0; iz < GX; ++iz)
                for (int iy = 0; iy < GY; ++iy)
                {
                    if (!clear[ix][iz][iy]) continue;
                    int sc = 0;
                    sc += ix > 0 && clear[ix - 1][iz][iy], sc += ix < GX - 1 && clear[ix + 1][iz][iy];
                    sc += iz > 0 && clear[ix][iz - 1][iy], sc += iz < GX - 1 && clear[ix][iz + 1][iy];
                    sc += iy > 0 && clear[ix][iz][iy - 1], sc += iy < GY - 1 && clear[ix][iz][iy + 1];
                    double tie = (ix - 2) * (ix - 2) + (iz - 2) * (iz - 2) + (iy - 4) * (iy - 4);
                    if (sc > bs || (sc == bs && tie < bt))
                    {
                        bs = sc, bt = tie;
                        out->mx = (float)(ce->x - 0.25 + 0.125 * ix), out->mz = (float)(ce->z - 0.25 + 0.125 * iz);
                        out->my = (float)(ce->y + (0.55 + 0.1 * iy) * ce->height);
                    }
                }
    }
    neth_env = saved;
    return best >= 0.0;
}

int netherview_exit_blaze_los(struct pipe *p, int id, double ex, double ey, double ez)
{
    struct pool *pl = pipe_pool(p);
    struct session *ss = pool_session(pl, id);
    if (ss == NULL || ss->client_world == NULL || ss->cp.combat == NULL) return -1;
    struct env *saved = neth_env;
    neth_env = pool_env(pl, id);
    const struct clientworld *cw = &ss->cp.combat->client;
    int seen = 0;
    for (int i = 0; i < cw->nents; ++i)
    {
        const struct client_entity *ce = &cw->ents[i];
        if (ce->is_dead || !ce->is_living || ce->dying || ce->kind != HK_BLAZE) continue;
        double my = ce->y + 0.5 * ce->height;
        double dx = ce->x - ex, dy = my - ey, dz = ce->z - ez;
        double d2 = dx * dx + dy * dy + dz * dz;
        if (!isfinite(d2) || d2 < 1e-6) continue;
        struct rt_mop m;
        if (raytrace_blocks(ss->client_world, ex, ey, ez, ce->x, my, ce->z, 0, 1, 0, &m) && m.type >= 0) continue;
        ++seen;
    }
    neth_env = saved;
    return seen;
}

/* lane/rod (nether_oracle, PRIVILEGED: the server's structure data and blocks, nothing a player sees) */
static int nv_box_d2(const struct bbox *b, double x, double z)
{
    double dx = x < b->minX ? b->minX - x : x > b->maxX + 1 ? x - b->maxX - 1 : 0.0;
    double dz = z < b->minZ ? b->minZ - z : z > b->maxZ + 1 ? z - b->maxZ - 1 : 0.0;
    double d = dx * dx + dz * dz;
    return d > 2e9 ? 2000000000 : (int)d;
}

#define NV_FORT_REGIONS 8       /* up to 2,048 blocks each way; one third of regions hold a start */
int netherview_oracle_start(struct pipe *p, int id, double x, double z, int32_t chunk[2])
{
    struct pool *pl = pipe_pool(p);
    if (pool_session(pl, id) == NULL || chunk == NULL) return -1;
    struct env *saved = neth_env;
    neth_env = pool_env(pl, id);
    int cx = 0, cz = 0;
    int found = neth_env && fortress_locate_start(neth_env->fortress.seed, (int)floor(x), (int)floor(z),
                                                NV_FORT_REGIONS, &cx, &cz);
    neth_env = saved;
    if (found) chunk[0] = cx, chunk[1] = cz;
    return found;
}

int netherview_oracle_throne(struct pipe *p, int id, double x, double z, int32_t target[2])
{
    struct pool *pl = pipe_pool(p);
    if (pool_session(pl, id) == NULL || target == NULL) return -1;
    struct env *saved = neth_env;
    neth_env = pool_env(pl, id);
    int goal[2], start[2];
    int found = neth_env && fortress_locate_throne(neth_env->fortress.seed, (int)floor(x), (int)floor(z), 400, goal, start);
    neth_env = saved;
    if (found) target[0] = goal[0], target[1] = goal[1];
    return found;
}

int netherview_oracle_entry(struct pipe *p, int id, double x, double y, double z, int32_t target[4])
{
    struct pool *pl = pipe_pool(p);
    if (pool_session(pl, id) == NULL || target == NULL) return -1;
    struct env *saved = neth_env;
    neth_env = pool_env(pl, id);
    int goal[3], kind = 0;
    int found = neth_env && fortress_locate_entry(neth_env->fortress.seed, (int)floor(x), (int)floor(y), (int)floor(z),
                                                400, goal, &kind);
    neth_env = saved;
    if (found)
    {
        for (int i = 0; i < 3; ++i) target[i] = goal[i];
        target[3] = kind;
    }
    return found;
}

int netherview_oracle(struct pipe *p, int id, double x, double y, double z, struct netherview_oracle *out)
{
    memset(out, 0, sizeof *out);
    out->piece_d = out->throne_d = -1.0f;
    struct pool *pl = pipe_pool(p);
    struct session *ss = pool_session(pl, id);
    if (ss == NULL || ss->sr == NULL) return -1;
    struct env *saved = neth_env;
    neth_env = pool_env(pl, id);
    const struct pop_map *m = &neth_env->populate_hell.hmap;
    out->nstarts = m->n;
    int best = -1, tbest = -1;
    const struct piece *bp = NULL, *tp = NULL;
    for (int i = 0; i < m->n; ++i)
    {
        const struct start *st = &m->starts[i].start;
        for (int k = 0; k < st->n; ++k)
        {
            const struct piece *pc = st->pieces[k];
            if (pc == NULL || pc->kind != PIECE_FORTRESS) continue;
            int d2 = nv_box_d2(&pc->bb, x, z);
            if (best < 0 || d2 < best) best = d2, bp = pc;
            if (pc->u.fortress.kind == F_THRONE && (tbest < 0 || d2 < tbest)) tbest = d2, tp = pc;
        }
    }
    if (bp)
    {
        out->has_piece = 1;
        out->piece_d = (float)sqrt((double)best);
        out->box[0] = bp->bb.minX, out->box[1] = bp->bb.minY, out->box[2] = bp->bb.minZ;
        out->box[3] = bp->bb.maxX, out->box[4] = bp->bb.maxY, out->box[5] = bp->bb.maxZ;
    }
    struct world *w = ss->sr->w[sr_dim_index(-1)].st.w;
    if (tp)
    {
        out->tbox[0] = tp->bb.minX, out->tbox[1] = tp->bb.minY, out->tbox[2] = tp->bb.minZ;
        out->tbox[3] = tp->bb.maxX, out->tbox[4] = tp->bb.maxY, out->tbox[5] = tp->bb.maxZ;
    }
    if (tp) bp = tp;    /* the brick of the throne's piece (the spawner's room), else the nearest piece's */
    if (bp && w)
    {
        /* the piece's nether brick nearest the player (its deck), where the server has the chunk */
        double bd = -1;
        for (int bx = bp->bb.minX; bx <= bp->bb.maxX; ++bx)
            for (int bz = bp->bb.minZ; bz <= bp->bb.maxZ; ++bz)
            {
                struct chunk *c = world_chunk(w, bx >> 4, bz >> 4);
                if (c == NULL) continue;
                for (int by = bp->bb.maxY; by >= bp->bb.minY && by > 0; --by)
                {
                    int b = chunk_get_block(c, bx & 15, by, bz & 15) & 4095;
                    if (b != 112) continue;
                    double d = (bx + 0.5 - x) * (bx + 0.5 - x) + (by - y) * (by - y) + (bz + 0.5 - z) * (bz + 0.5 - z);
                    if (bd < 0 || d < bd) bd = d, out->brick[0] = bx, out->brick[1] = by, out->brick[2] = bz;
                    break;      /* the column's top brick */
                }
            }
        out->has_brick = bd >= 0;
    }
    if (tp)
    {
        out->has_throne = 1;
        out->throne_d = (float)sqrt((double)tbest);
        out->sp[0] = (tp->bb.minX + tp->bb.maxX) / 2, out->sp[1] = tp->bb.minY + 5, out->sp[2] = (tp->bb.minZ + tp->bb.maxZ) / 2;
        for (int bx = tp->bb.minX; w && bx <= tp->bb.maxX && !out->has_sp; ++bx)
            for (int bz = tp->bb.minZ; bz <= tp->bb.maxZ && !out->has_sp; ++bz)
            {
                struct chunk *c = world_chunk(w, bx >> 4, bz >> 4);
                if (c == NULL) continue;
                for (int by = tp->bb.minY; by <= tp->bb.maxY; ++by)
                    if ((chunk_get_block(c, bx & 15, by, bz & 15) & 4095) == 52)
                    {
                        out->has_sp = 1, out->sp[0] = bx, out->sp[1] = by, out->sp[2] = bz;
                        break;
                    }
            }
    }
    neth_env = saved;
    return 0;
}

int netherview_oracle_block(struct pipe *p, int id, int x, int y, int z)
{
    struct pool *pl = pipe_pool(p);
    struct session *ss = pool_session(pl, id);
    if (ss == NULL || ss->sr == NULL || y < 0 || y > 255) return -1;
    struct env *saved = neth_env;
    neth_env = pool_env(pl, id);
    struct world *w = ss->sr->w[sr_dim_index(-1)].st.w;
    struct chunk *c = w ? world_chunk(w, x >> 4, z >> 4) : NULL;
    int b = c ? (int)(chunk_get_block(c, x & 15, y, z & 15) & 4095) : -1;
    neth_env = saved;
    return b;
}

int netherview_oracle_portal(struct pipe *p, int id, int x, int y, int z, int r, int32_t cell[3])
{
    struct pool *pl = pipe_pool(p);
    struct session *ss = pool_session(pl, id);
    if (ss == NULL || ss->sr == NULL) return 0;
    struct env *saved = neth_env;
    neth_env = pool_env(pl, id);
    struct world *w = ss->sr->w[sr_dim_index(-1)].st.w;
    int found = 0, bd = 0;
    for (int dx = -r; w && dx <= r; ++dx)
        for (int dz = -r; dz <= r; ++dz)
        {
            struct chunk *c = world_chunk(w, (x + dx) >> 4, (z + dz) >> 4);
            if (c == NULL) continue;
            for (int dy = -r; dy <= r; ++dy)
            {
                int by = y + dy;
                if (by < 1 || by > 254) continue;
                if ((chunk_get_block(c, (x + dx) & 15, by, (z + dz) & 15) & 4095) != 90) continue;
                if ((chunk_get_block(c, (x + dx) & 15, by - 1, (z + dz) & 15) & 4095) == 90) continue;  /* the lowest */
                int d = dx * dx + dy * dy + dz * dz;
                if (!found || d < bd) found = 1, bd = d, cell[0] = x + dx, cell[1] = by, cell[2] = z + dz;
            }
        }
    /* none within r (its chunks not held: the portal is far): the Nether teleporter's cache, the portals it placed
     * players at (Teleporter.destinationCoordinateCache, restored with the snapshot), the nearest */
    const struct portal_cache *pc = &ss->sr->tp_hell.cache;
    long long best = -1;
    for (size_t i = 0; !found && pc->entries && i < pc->cap; ++i)
    {
        const struct portal_cache_entry *ce = &pc->entries[i];
        if (!ce->occupied) continue;
        long long d = (long long)(ce->x - x) * (ce->x - x) + (long long)(ce->z - z) * (ce->z - z);
        if (best < 0 || d < best) best = d, cell[0] = ce->x, cell[1] = ce->y, cell[2] = ce->z;
    }
    if (!found && best >= 0) found = 2;
    neth_env = saved;
    return found;
}

int netherview_oracle_blazes(struct pipe *p, int id, double ex, double ey, double ez, double r,
                             struct netherview_blaze *out, int max)
{
    struct pool *pl = pipe_pool(p);
    struct session *ss = pool_session(pl, id);
    if (ss == NULL || ss->cp.combat == NULL) return 0;
    struct env *saved = neth_env;
    neth_env = pool_env(pl, id);
    const struct clientworld *cw = &ss->cp.combat->client;
    int n = 0;
    for (int i = 0; i < cw->nents; ++i)
    {
        const struct client_entity *ce = &cw->ents[i];
        if (ce->is_dead || !ce->is_living || ce->dying || ce->kind != HK_BLAZE) continue;
        double my = ce->y + 0.5 * ce->height, dx = ce->x - ex, dy = my - ey, dz = ce->z - ez;
        double d = sqrt(dx * dx + dy * dy + dz * dz);
        if (d > r) continue;
        int k = n < max ? n : max - 1;
        if (n == max && d >= out[k].d) continue;
        while (k > 0 && out[k - 1].d > d)
        {
            out[k] = out[k - 1];
            --k;
        }
        out[k] = (struct netherview_blaze){ce->id, (float)ce->x, (float)my, (float)ce->z, (float)d};
        if (n < max) n += 1;
    }
    neth_env = saved;
    return n;
}

int netherview_blaze_lock(struct pipe *p, int id, int eid, double ex, double ey, double ez, float *d, float *dy, int *los)
{
    struct pool *pl = pipe_pool(p);
    struct session *ss = pool_session(pl, id);
    if (ss == NULL || ss->client_world == NULL || ss->cp.combat == NULL || eid == 0) return 0;
    struct env *saved = neth_env;
    neth_env = pool_env(pl, id);
    const struct clientworld *cw = &ss->cp.combat->client;
    int found = 0;
    for (int i = 0; i < cw->nents && !found; ++i)
    {
        const struct client_entity *ce = &cw->ents[i];
        if (ce->id != eid || ce->is_dead || !ce->is_living || ce->kind != HK_BLAZE) continue;
        double my = ce->y + 0.5 * ce->height, ddx = ce->x - ex, ddy = my - ey, ddz = ce->z - ez;
        *d = (float)sqrt(ddx * ddx + ddy * ddy + ddz * ddz), *dy = (float)ddy;
        struct rt_mop m;
        *los = !(raytrace_blocks(ss->client_world, ex, ey, ez, ce->x, my, ce->z, 0, 1, 0, &m) && m.type >= 0);
        found = 1;
    }
    neth_env = saved;
    return found;
}

void netherview_blaze_lines(struct pipe *p, int id, double ex, double ey, double ez, double r, float yaw, float pitch,
                            int *n, int *los, float *ld, float *ldy, int *cone, int *mouth, int cell[3])
{
    *n = *los = *cone = *mouth = 0;
    cell[0] = cell[1] = cell[2] = -99;
    int fx = (int)floor(ex), fy = (int)floor(ey - 1.62 + 0.13), fz = (int)floor(ez);
    double yr = (double)yaw * M_PI / 180.0, pr = (double)pitch * M_PI / 180.0;
    double lx = -sin(yr) * cos(pr), ly = -sin(pr), lz = cos(yr) * cos(pr), cmin = cos(NV_THREAT_CONE * M_PI / 180.0);
    *ld = -1.0f, *ldy = 0.0f;
    struct pool *pl = pipe_pool(p);
    struct session *ss = pool_session(pl, id);
    if (ss == NULL || ss->client_world == NULL || ss->cp.combat == NULL) return;
    struct env *saved = neth_env;
    neth_env = pool_env(pl, id);
    const struct clientworld *cw = &ss->cp.combat->client;
    for (int i = 0; i < cw->nents; ++i)
    {
        const struct client_entity *ce = &cw->ents[i];
        if (ce->is_dead || !ce->is_living || ce->dying || ce->kind != HK_BLAZE) continue;
        double by = ce->y + 0.85 * ce->height, dx = ce->x - ex, dy = by - ey, dz = ce->z - ez;
        if (dx * dx + dy * dy + dz * dz > r * r) continue;
        *n += 1;
        struct rt_mop m;
        if (!(raytrace_blocks(ss->client_world, ce->x, by, ce->z, ex, ey, ez, 0, 1, 0, &m) && m.type >= 0))
        {
            *los += 1;
            float d = (float)sqrt(dx * dx + dy * dy + dz * dz);
            /* the first cell the ray (player's eye to the blaze's eye) enters outside the player's feet and head cells,
             * as nt_cover walks it: one block there shuts this line */
            int c[3] = {-99, -99, -99};
            for (double t = 0.05; t < d; t += 0.05)
            {
                int cx = (int)floor(ex + dx * t / d), cy = (int)floor(ey + dy * t / d), cz = (int)floor(ez + dz * t / d);
                if (cx == fx && cz == fz && (cy == fy || cy == fy + 1)) continue;
                c[0] = cx - fx, c[1] = cy - fy, c[2] = cz - fz;
                break;
            }
            if (c[0] == 0 && c[1] == 2 && c[2] == 0) *mouth += 1;
            if (*ld < 0.0f || d < *ld) *ld = d, *ldy = (float)dy, cell[0] = c[0], cell[1] = c[1], cell[2] = c[2];
            /* the cone as netherview_threats tests it, from the blaze's middle */
            double my = ce->y + 0.5 * ce->height - ey, dm = sqrt(dx * dx + my * my + dz * dz);
            if (dm > 1e-3 && (dx * lx + my * ly + dz * lz) / dm >= cmin) *cone += 1;
        }
    }
    neth_env = saved;
}

int netherview_fireballs(struct pipe *p, int id, double ex, double ey, double ez, double r,
                         struct netherview_fireball *out, int max)
{
    struct pool *pl = pipe_pool(p);
    struct session *ss = pool_session(pl, id);
    if (ss == NULL || ss->sr == NULL || ss->sr->d == NULL || max <= 0) return 0;
    struct env *saved = neth_env;
    neth_env = pool_env(pl, id);
    ie_world *lists[2] = {&ss->sr->d->anw.iew, &ss->sr->d->iew};
    int n = 0;
    for (int L = 0; L < 2; ++L)
        for (int i = 0; i < lists[L]->n && n < max; ++i)
        {
            const struct ie_ent *en = ie_ent_at(lists[L]->slot[i]);
            if (en == NULL || en->is_dead || en->kind != IE_SMALL_FIREBALL) continue;
            double dx = en->e.pos_x - ex, dy = en->e.pos_y - ey, dz = en->e.pos_z - ez;
            if (dx * dx + dy * dy + dz * dz > r * r) continue;
            out[n++] = (struct netherview_fireball){en->entity_id, (float)en->e.pos_x, (float)en->e.pos_y,
                                                    (float)en->e.pos_z};
        }
    neth_env = saved;
    return n;
}

int netherview_clear(struct pipe *p, int id, double x0, double y0, double z0, double x1, double y1, double z1)
{
    struct pool *pl = pipe_pool(p);
    struct session *ss = pool_session(pl, id);
    if (ss == NULL || ss->client_world == NULL) return -1;
    struct env *saved = neth_env;
    neth_env = pool_env(pl, id);
    struct rt_mop m;
    int clear = !(raytrace_blocks(ss->client_world, x0, y0, z0, x1, y1, z1, 0, 1, 0, &m) && m.type >= 0);
    neth_env = saved;
    return clear;
}

int netherview_oracle_item(struct pipe *p, int id, double ex, double ey, double ez, double r, int item, float xyz[3])
{
    struct pool *pl = pipe_pool(p);
    struct session *ss = pool_session(pl, id);
    if (ss == NULL || ss->sr == NULL || ss->sr->d == NULL) return 0;
    struct env *saved = neth_env;
    neth_env = pool_env(pl, id);
    ie_world *lists[2] = {&ss->sr->d->anw.iew, &ss->sr->d->iew};
    double best = r * r;
    int found = 0;
    for (int L = 0; L < 2; ++L)
        for (int i = 0; i < lists[L]->n; ++i)
        {
            const struct ie_ent *en = ie_ent_at(lists[L]->slot[i]);
            if (en == NULL || en->is_dead || en->kind != IE_ITEM || en->stack_item != item) continue;
            double dx = en->e.pos_x - ex, dy = en->e.pos_y - ey, dz = en->e.pos_z - ez, d2 = dx * dx + dy * dy + dz * dz;
            if (d2 < best)
                best = d2, found = 1, xyz[0] = (float)en->e.pos_x, xyz[1] = (float)en->e.pos_y,
                xyz[2] = (float)en->e.pos_z;
        }
    neth_env = saved;
    return found;
}

int netherview_oracle_items(struct pipe *p, int id, int item, struct netherview_item *out, int max)
{
    if (out == NULL || max <= 0) return 0;
    struct pool *pl = pipe_pool(p);
    struct session *ss = pool_session(pl, id);
    if (ss == NULL || ss->sr == NULL || ss->sr->d == NULL) return 0;
    struct env *saved = neth_env;
    neth_env = pool_env(pl, id);
    ie_world *lists[2] = {&ss->sr->d->anw.iew, &ss->sr->d->iew};
    int n = 0;
    for (int L = 0; L < 2; ++L)
        for (int i = 0; i < lists[L]->n; ++i)
        {
            const struct ie_ent *en = ie_ent_at(lists[L]->slot[i]);
            if (en == NULL || en->is_dead || en->kind != IE_ITEM || en->stack_item != item) continue;
            if (n < max)
                out[n] = (struct netherview_item){en->entity_id, en->age, en->stack_count,
                                                  (float)en->e.pos_x, (float)en->e.pos_y, (float)en->e.pos_z};
            ++n;
        }
    neth_env = saved;
    return n < max ? n : max;
}

/* the path's cells (nv_path_*): open (air, plants, snow), a floor (a solid box: not open, not a liquid, fire, a fence,
 * a portal), lava or fire */
/* (the portal block too, no box: dig-9s's tunnel never reached the portal's own cell, 413 of 470 plans none found) */
static int nvp_open(int b) { return b == 0 || b == 39 || b == 40 || b == 31 || b == 78 || b == 50 || b == 90; }
static int nvp_hot(int b) { return b == 10 || b == 11 || b == 51; }
static int nvp_floor(int b) { return b > 0 && !nvp_open(b) && !nvp_hot(b) && b != 8 && b != 9 && b != 113 && b != 90; }

struct nvp_grid { struct world *w; int x0, y0, z0, sx, sy, sz, nobridge; };
static int nvp_get(const struct nvp_grid *g, int x, int y, int z)
{
    if (y < 0 || y > 255) return -1;
    struct chunk *c = world_chunk(g->w, x >> 4, z >> 4);
    return c ? chunk_get_block(c, x & 15, y, z & 15) & 4095 : -1;
}
/* the feet may stand at (x, y, z): a floor under, the feet and head cells open, no lava or fire in them or beside them */
static int nvp_stand(const struct nvp_grid *g, int x, int y, int z)
{
    if (!nvp_floor(nvp_get(g, x, y - 1, z)) || !nvp_open(nvp_get(g, x, y, z)) || !nvp_open(nvp_get(g, x, y + 1, z)))
        return 0;
    static const int DX[4] = {1, -1, 0, 0}, DZ[4] = {0, 0, 1, -1};
    for (int k = 0; k < 4; ++k)
        for (int dy = -1; dy <= 1; ++dy)
        {
            int b = nvp_get(g, x + DX[k], y + dy, z + DZ[k]);
            if (b < 0 || nvp_hot(b)) return 0;
        }
    return 1;
}

/* NV_PATH_BUILD's level move onto (x, y, z): its feet and head cells open or dug (any block but bedrock, obsidian,
 * a portal, a spawner, a liquid; NV_PATH_DIG each), its floor there or placed (NV_PATH_BRIDGE: over air, never over a
 * liquid), no lava or fire in or beside the two cells or over the head; the move's cost into *cost */
static int nvp_dig_ok(int b) { return b > 0 && b != 7 && b != 49 && b != 90 && b != 52 && b != 8 && b != 9 && !nvp_hot(b); }
static int nvp_buildable(const struct nvp_grid *g, int x, int y, int z, int *cost)
{
    int c = 1;
    for (int dy = 0; dy <= 1; ++dy)
    {
        int b = nvp_get(g, x, y + dy, z);
        if (b < 0) return 0;
        if (nvp_open(b)) continue;
        if (!nvp_dig_ok(b)) return 0;
        c += NV_PATH_DIG;
    }
    int fb = nvp_get(g, x, y - 1, z), up = nvp_get(g, x, y + 2, z);
    if (fb < 0 || up < 0 || nvp_hot(up) || up == 8 || up == 9 || up == 12 || up == 13) return 0;  /* lava, sand, gravel over */
    if (!nvp_floor(fb))
    {
        if (!nvp_open(fb)) return 0;    /* a liquid, a fence, a portal under: no bridge */
        c += NV_PATH_BRIDGE;
    }
    static const int DX[4] = {1, -1, 0, 0}, DZ[4] = {0, 0, 1, -1};
    for (int k = 0; k < 4; ++k)
        for (int dy = 0; dy <= 1; ++dy)
        {
            int b = nvp_get(g, x + DX[k], y + dy, z + DZ[k]);
            if (b < 0 || nvp_hot(b) || b == 8 || b == 9) return 0;
        }
    if (c == 1) c = 2;                  /* walkable but for lava beside the floor (nvp_stand's rule): a risk's cost */
    *cost = c;
    return 1;
}

/* NV_PATH_TUNNEL's move from (x, y, z) to (nx, ny, nz), ny y - 1, y or y + 1: the cells it opens (the feet's and the
 * head's there; for a step up the head room over the jump, for a step down the head cell over the landing) open or
 * dug, a floor under (else a bridge, dear), no lava, fire or water beside the two cells, no lava, water, sand or
 * gravel over the head; its cost NV_TUN_DIG a dug block plus NV_TUN_DIG for a cell closed on its sides (but the way
 * back) and over the head, NV_TUN_OPEN for one open to the air, NV_TUN_LOWC more under NV_TUN_LOW */
static int nvp_tunnel(const struct nvp_grid *g, int x, int y, int z, int nx, int ny, int nz, int *cost)
{
    int cells[3][3] = {{nx, ny, nz}, {nx, ny + 1, nz}, {0, 0, 0}}, nc = 2, dig = 0;
    if (ny == y + 1) cells[2][0] = x, cells[2][1] = y + 2, cells[2][2] = z, nc = 3;
    else if (ny == y - 1) cells[2][0] = nx, cells[2][1] = ny + 2, cells[2][2] = nz, nc = 3;
    for (int i = 0; i < nc; ++i)
    {
        int b = nvp_get(g, cells[i][0], cells[i][1], cells[i][2]);
        if (b < 0) return 0;
        if (nvp_open(b)) continue;
        if (!nvp_dig_ok(b)) return 0;
        dig += 1;
    }
    int fb = nvp_get(g, nx, ny - 1, nz), up = nvp_get(g, nx, ny + 2, nz);
    if (fb < 0 || up < 0 || nvp_hot(fb) || nvp_hot(up) || up == 8 || up == 9 || up == 12 || up == 13) return 0;
    if (nc == 3)
    {
        /* the stair's third cell too: nothing hot or wet beside it, nor hot, wet, sand or gravel over it (dig-9qt: a
         * step down under lava at ny + 3, refused by the digger for 17,000 steps while every plan named it) */
        static const int SX[4] = {1, -1, 0, 0}, SZ[4] = {0, 0, 1, -1};
        int tx = cells[2][0], tyy = cells[2][1], tz = cells[2][2];
        int o = nvp_get(g, tx, tyy + 1, tz);
        if (o < 0 || nvp_hot(o) || o == 8 || o == 9 || o == 12 || o == 13) return 0;
        for (int k = 0; k < 4; ++k)
        {
            int b = nvp_get(g, tx + SX[k], tyy, tz + SZ[k]);
            if (b < 0 || nvp_hot(b) || b == 8 || b == 9) return 0;
        }
    }
    int c = 0;
    if (!nvp_floor(fb))
    {
        if (!nvp_open(fb) || g->nobridge) return 0;
        c += 2 * NV_TUN_OPEN;          /* a bridge */
    }
    static const int DX[4] = {1, -1, 0, 0}, DZ[4] = {0, 0, 1, -1};
    int closed = !nvp_open(up);
    for (int k = 0; k < 4; ++k)
        for (int dy = 0; dy <= 1; ++dy)
        {
            int qx = nx + DX[k], qz = nz + DZ[k], b = nvp_get(g, qx, ny + dy, qz);
            if (b < 0 || nvp_hot(b) || b == 8 || b == 9) return 0;
            if (qx == x && qz == z) continue;     /* the way back */
            if (nvp_open(b)) closed = 0;
        }
    c += dig * NV_TUN_DIG + (closed ? NV_TUN_DIG : NV_TUN_OPEN) + (ny < NV_TUN_LOW ? NV_TUN_LOWC : 0);
    *cost = c;
    return 1;
}

int netherview_oracle_path(struct pipe *p, int id, int fx, int fy, int fz, int tx, int ty, int tz, double near,
                           int flags, int16_t (*out)[3], int max)
{
    return netherview_oracle_path_dim(p, id, -1, fx, fy, fz, tx, ty, tz, near, flags, out, max);
}

int netherview_oracle_path_dim(struct pipe *p, int id, int dim, int fx, int fy, int fz, int tx, int ty, int tz,
                               double near, int flags, int16_t (*out)[3], int max)
{
    struct pool *pl = pipe_pool(p);
    struct session *ss = pool_session(pl, id);
    if (ss == NULL || ss->sr == NULL) return 0;
    struct env *saved = neth_env;
    neth_env = pool_env(pl, id);
    struct nvp_grid g = {ss->sr->w[sr_dim_index(dim)].st.w, 0, 0, 0, 0, 0, 0, !!(flags & NV_PATH_NOBRIDGE)};
    if (g.w == NULL)
    {
        neth_env = saved;
        return 0;
    }
    int lo = (fy < ty ? fy : ty) - NV_PATH_DY, hi = (fy > ty ? fy : ty) + NV_PATH_DY;
    if (lo < 1) lo = 1;
    if (hi > 253) hi = 253;
    int mx = (fx + tx) / 2, mz = (fz + tz) / 2;
    int hx = abs(fx - tx) / 2 + NV_PATH_PAD, hz = abs(fz - tz) / 2 + NV_PATH_PAD;
    if (hx > NV_PATH_HALF) hx = NV_PATH_HALF;
    if (hz > NV_PATH_HALF) hz = NV_PATH_HALF;
    g.x0 = mx - hx, g.z0 = mz - hz, g.y0 = lo, g.sx = 2 * hx + 1, g.sz = 2 * hz + 1, g.sy = hi - lo + 1;
    size_t ncell = (size_t)g.sx * g.sy * g.sz;
    /* Dijkstra: dist and the parent a cell, a binary heap of (dist, cell) with lazy deletion */
    int32_t *par = malloc(ncell * sizeof *par), *dist = malloc(ncell * sizeof *dist);
    size_t hcap = ncell < 65536 ? 65536 : ncell, hn = 0;
    int64_t *heap = malloc(hcap * sizeof *heap);
    int len = 0;
    if (par == NULL || dist == NULL || heap == NULL) goto done;
    for (size_t i = 0; i < ncell; ++i) par[i] = -1, dist[i] = INT32_MAX;
#define NVP_IDX(x, y, z) ((((size_t)((y) - g.y0) * g.sz) + (size_t)((z) - g.z0)) * g.sx + (size_t)((x) - g.x0))
#define NVP_IN(x, y, z) ((x) >= g.x0 && (x) < g.x0 + g.sx && (y) >= g.y0 && (y) < g.y0 + g.sy && (z) >= g.z0 && \
                         (z) < g.z0 + g.sz)
    if (!NVP_IN(fx, fy, fz)) goto done;
    size_t s0 = NVP_IDX(fx, fy, fz);
    par[s0] = (int32_t)s0, dist[s0] = 0;
    int goal = -1, best = -1;
    double bestd = 1e30, d0 = sqrt((double)(fx - tx) * (fx - tx) + (double)(fz - tz) * (fz - tz));
    static const int DX[4] = {1, -1, 0, 0}, DZ[4] = {0, 0, 1, -1};
    heap[hn++] = (int64_t)s0;
    while (hn > 0 && goal < 0)
    {
        /* pop the least */
        int64_t top = heap[0];
        heap[0] = heap[--hn];
        for (size_t i = 0;;)
        {
            size_t l = 2 * i + 1, r = l + 1, m = i;
            if (l < hn && heap[l] < heap[m]) m = l;
            if (r < hn && heap[r] < heap[m]) m = r;
            if (m == i) break;
            int64_t t = heap[i];
            heap[i] = heap[m], heap[m] = t, i = m;
        }
        int32_t c = (int32_t)(top & 0xffffffff), cd = (int32_t)(top >> 32);
        if (cd != dist[c]) continue;        /* a stale entry */
        int x = g.x0 + (int)(c % g.sx), z = g.z0 + (int)((c / g.sx) % g.sz), y = g.y0 + (int)(c / ((size_t)g.sx * g.sz));
        double ddx = x + 0.5 - (tx + 0.5), ddz = z + 0.5 - (tz + 0.5), dd = sqrt(ddx * ddx + ddz * ddz);
        if (c != (int32_t)s0 && dd <= near && abs(y - ty) <= ((flags & NV_PATH_EXACT) ? 0 : NV_PATH_GOAL_DY))
        {
            goal = c;
            break;
        }
        double wy = (flags & NV_PATH_EXACT) ? 2.0 : 0.25;   /* exact: a partial end at another height costs more */
        if (c != (int32_t)s0 && dd + wy * abs(y - ty) < bestd) bestd = dd + wy * abs(y - ty), best = c;
        for (int m = 0; m < ((flags & NV_PATH_TUNNEL) ? 12 : 4); ++m)
        {
            int k = (flags & NV_PATH_TUNNEL) ? m / 3 : m;
            int nx = x + DX[k], nz = z + DZ[k], ny = -1, cost = 0;
            if (flags & NV_PATH_TUNNEL)
            {
                /* the tunnel: a level, an up and a down move each direction, each dug */
                int ty2 = y + (m % 3 == 0 ? 0 : m % 3 == 1 ? 1 : -1);
                if (!NVP_IN(nx, ty2, nz) || !NVP_IN(nx, ty2 + 1, nz) || !nvp_tunnel(&g, x, y, z, nx, ty2, nz, &cost))
                    continue;
                ny = ty2;
                goto push;
            }
            /* a step up (head room over the jump) */
            if (NVP_IN(nx, y + 1, nz) && nvp_open(nvp_get(&g, x, y + 2, z)) && nvp_stand(&g, nx, y + 1, nz))
                ny = y + 1, cost = 1;
            /* level, else down the open column to its first standable cell (3 at most) */
            for (int dy = 0; ny < 0 && dy >= -3; --dy)
            {
                int yy = y + dy;
                if (!NVP_IN(nx, yy, nz)) break;
                if (!nvp_open(nvp_get(&g, nx, yy + 1, nz))) break;     /* the column's head cell over the landing */
                if (nvp_stand(&g, nx, yy, nz))
                {
                    ny = yy, cost = 1;
                    break;
                }
                if (!nvp_open(nvp_get(&g, nx, yy, nz))) break;         /* a wall or a hot cell: no way down */
            }
            /* with NV_PATH_BUILD, the level cell made walkable: its feet and head blocks dug, its floor placed */
            if (ny < 0 && (flags & NV_PATH_BUILD) && NVP_IN(nx, y, nz) && nvp_buildable(&g, nx, y, nz, &cost)) ny = y;
            if (ny < 0) continue;
        push:;
            size_t ni = NVP_IDX(nx, ny, nz);
            int32_t nd = cd + cost;
            if (nd >= dist[ni]) continue;
            dist[ni] = nd, par[ni] = c;
            if (hn == hcap)
            {
                int64_t *nh = realloc(heap, 2 * hcap * sizeof *heap);
                if (nh == NULL) goto done;
                heap = nh, hcap *= 2;
            }
            size_t i = hn++;
            heap[i] = ((int64_t)nd << 32) | (int64_t)ni;
            while (i > 0 && heap[(i - 1) / 2] > heap[i])
            {
                int64_t t = heap[i];
                heap[i] = heap[(i - 1) / 2], heap[(i - 1) / 2] = t, i = (i - 1) / 2;
            }
        }
    }
    /* the target past the server's loaded chunks (it holds the client's render distance): the way to the reachable
     * cell nearest it, when that is NV_PATH_GAIN nearer than the start */
    if (goal < 0 && best >= 0 && bestd + NV_PATH_GAIN <= d0) goal = best;
    if (goal >= 0)
    {
        int n = 0;
        for (int32_t c = goal; c != (int32_t)s0; c = par[c]) ++n;
        len = n < max ? n : max;
        int k = n;
        for (int32_t c = goal; c != (int32_t)s0; c = par[c])
        {
            --k;
            if (k >= max) continue;
            out[k][0] = (int16_t)(g.x0 + (int)(c % g.sx));
            out[k][1] = (int16_t)(g.y0 + (int)(c / ((size_t)g.sx * g.sz)));
            out[k][2] = (int16_t)(g.z0 + (int)((c / g.sx) % g.sz));
        }
    }
#undef NVP_IDX
#undef NVP_IN
done:
    free(par);
    free(dist);
    free(heap);
    neth_env = saved;
    return len;
}

/* lane/nether4: a developer's list of env id's client entities within r of (x, y, z) to stderr (NT_TRACE builds only) */
void netherview_debug_ents(struct pipe *p, int id, double x, double y, double z, double r)
{
    struct pool *pl = pipe_pool(p);
    struct session *ss = pool_session(pl, id);
    if (ss == NULL || ss->cp.combat == NULL) return;
    const struct clientworld *cw = &ss->cp.combat->client;
    for (int i = 0; i < cw->nents; ++i)
    {
        const struct client_entity *ce = &cw->ents[i];
        double dx = ce->x - x, dy = ce->y - y, dz = ce->z - z;
        if (dx * dx + dy * dy + dz * dz > r * r) continue;
        fprintf(stderr, "  ent %d kind %d living %d dead %d ball %d large %d at %.1f %.1f %.1f d %.1f\n", ce->id, ce->kind,
                ce->is_living, ce->is_dead, ce->is_fireball, ce->is_large_fireball, ce->x, ce->y, ce->z,
                sqrt(dx * dx + dy * dy + dz * dz));
    }
}
