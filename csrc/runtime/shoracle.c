/* lane/shold (stronghold_oracle): PRIVILEGED (shoracle.h): the portal room from the world's structure data, its blocks
 * and a way over the server's overworld cells. Runs in the env's own context between steps (as netherview's oracle). */
#define _GNU_SOURCE
#include "shoracle.h"

#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "../engine/combat.h"
#include "../engine/env.h"
#include "../engine/layers.h"
#include "../engine/living.h"
#include "../engine/populate.h"
#include "../engine/raytrace.h"
#include "../engine/serverreplay.h"
#include "../engine/session.h"
#include "../engine/stronghold.h"
#include "../engine/structure.h"
#include "../engine/terrain.h"
#include "../engine/world.h"
#include "pipeline.h"
#include "pool.h"

#define SHO_CACHE 8             /* seeds whose three rooms are kept */
#define END_FRAME_B 120
#define END_PORTAL_B 119

/* the seeds' rooms from the structure walk (a scratch env's: the walk keeps its ring and layers there), kept process-wide
 * by seed: the walk is a pure function of it */
static struct {
    int64_t seed;
    int n;
    int32_t box[SH_COUNT][6], cx[SH_COUNT], cz[SH_COUNT];
} sho_cache[SHO_CACHE];
static int sho_ncache;
static pthread_mutex_t sho_mu = PTHREAD_MUTEX_INITIALIZER;

static int sho_portal_piece(const struct start *st, int32_t box[6])
{
    for (int k = 0; k < st->n; ++k)
    {
        const struct piece *pc = st->pieces[k];
        if (pc == NULL || pc->kind != PIECE_STRONGHOLD) continue;
        const struct sh_state *ss = stronghold_state(pc);
        if (ss == NULL || ss->kind != SHS_PORTALROOM) continue;
        box[0] = pc->bb.minX, box[1] = pc->bb.minY, box[2] = pc->bb.minZ;
        box[3] = pc->bb.maxX, box[4] = pc->bb.maxY, box[5] = pc->bb.maxZ;
        return 1;
    }
    return 0;
}

static int sho_seed_rooms(int64_t seed, int k_out[1])
{
    pthread_mutex_lock(&sho_mu);
    for (int i = 0; i < sho_ncache; ++i)
        if (sho_cache[i].seed == seed)
        {
            pthread_mutex_unlock(&sho_mu);
            k_out[0] = i;
            return 0;
        }
    struct env *saved = neth_env, *e = env_new();
    if (e == NULL)
    {
        pthread_mutex_unlock(&sho_mu);
        return -1;
    }
    neth_env = e;
    int slot = sho_ncache < SHO_CACHE ? sho_ncache++ : (int)(seed & (SHO_CACHE - 1));
    sho_cache[slot].seed = seed;
    sho_cache[slot].n = 0;
    structure_stronghold.begin(seed);
    struct sh_pos ring[SH_COUNT];
    memcpy(ring, neth_env->stronghold.coords, sizeof ring);
    for (int i = 0; i < SH_COUNT; ++i)
    {
        struct start_list l = {0};
        structure_walk(&structure_stronghold, seed, ring[i].cx, ring[i].cz, ring[i].cx, ring[i].cz, &l);
        for (int j = 0; j < l.n; ++j)
        {
            if (l.v[j]->chunk_x != ring[i].cx || l.v[j]->chunk_z != ring[i].cz) continue;
            int n = sho_cache[slot].n;
            if (n < SH_COUNT && sho_portal_piece(l.v[j], sho_cache[slot].box[n]))
            {
                sho_cache[slot].cx[n] = ring[i].cx, sho_cache[slot].cz[n] = ring[i].cz;
                sho_cache[slot].n = n + 1;
            }
        }
        start_list_free(&l);
    }
    neth_env = saved;
    env_free(e);
    k_out[0] = slot;
    pthread_mutex_unlock(&sho_mu);
    return 0;
}

static struct world *sho_world(struct pipe *p, int id, struct serverreplay **sr, struct env **saved)
{
    struct pool *pl = pipe_pool(p);
    struct session *ss = pool_session(pl, id);
    if (ss == NULL || ss->sr == NULL) return NULL;
    *saved = neth_env;
    neth_env = pool_env(pl, id);
    if (sr) *sr = ss->sr;
    return ss->sr->w[sr_dim_index(0)].st.w;
}

static int sho_get(struct world *w, int x, int y, int z, int *meta)
{
    if (w == NULL || y < 0 || y > 255) return -1;
    struct chunk *c = world_chunk(w, x >> 4, z >> 4);
    if (c == NULL) return -1;
    if (meta) *meta = chunk_get_meta(c, x & 15, y, z & 15);
    return chunk_get_block(c, x & 15, y, z & 15) & 4095;
}

int shoracle_block(struct pipe *p, int id, int x, int y, int z, int *meta)
{
    struct env *saved;
    struct world *w = sho_world(p, id, NULL, &saved);
    if (w == NULL) return -1;
    int b = sho_get(w, x, y, z, meta);
    neth_env = saved;
    return b;
}

int shoracle_block_dim(struct pipe *p, int id, int dim, int x, int y, int z, int *meta)
{
    struct pool *pl = pipe_pool(p);
    struct session *ss = pool_session(pl, id);
    if (ss == NULL || ss->sr == NULL) return -1;
    struct env *saved = neth_env;
    neth_env = pool_env(pl, id);
    int b = sho_get(ss->sr->w[sr_dim_index(dim)].st.w, x, y, z, meta);
    neth_env = saved;
    return b;
}

static void sho_read(struct world *w, struct shoracle_room *o)
{
    o->loaded = 1;
    o->nfr = o->has_sp = o->portal = 0;
    for (int x = o->box[0]; x <= o->box[3]; ++x)
        for (int z = o->box[2]; z <= o->box[5]; ++z)
        {
            struct chunk *c = world_chunk(w, x >> 4, z >> 4);
            if (c == NULL)
            {
                o->loaded = 0;
                continue;
            }
            for (int y = o->box[1]; y <= o->box[4]; ++y)
            {
                int b = chunk_get_block(c, x & 15, y, z & 15) & 4095;
                if (b == END_FRAME_B && o->nfr < 12)
                {
                    int32_t *f = o->fr[o->nfr++];
                    f[0] = x, f[1] = y, f[2] = z, f[3] = (chunk_get_meta(c, x & 15, y, z & 15) & 4) != 0;
                }
                else if (b == 52 && !o->has_sp)
                    o->has_sp = 1, o->sp[0] = x, o->sp[1] = y, o->sp[2] = z;
                else if (b == END_PORTAL_B)
                {
                    if (!o->portal) o->pt[0] = x, o->pt[1] = y, o->pt[2] = z;
                    o->portal += 1;
                }
            }
        }
}

int shoracle_locate(struct pipe *p, int id, double x, double z, struct shoracle_room *out)
{
    memset(out, 0, sizeof *out);
    struct env *saved;
    struct serverreplay *sr;
    struct world *w = sho_world(p, id, &sr, &saved);
    if (w == NULL) return -1;
    double bd = -1;
    /* the server's map first (a start it holds is the world's, a saved one's boxes included) */
    const struct pop_map *m = &sr->pop.maps[2];
    for (int i = 0; i < m->n; ++i)
    {
        int32_t box[6];
        if (!sho_portal_piece(&m->starts[i].start, box)) continue;
        double cx = (box[0] + box[3]) / 2.0 + 0.5, cz = (box[2] + box[5]) / 2.0 + 0.5;
        double d = hypot(cx - x, cz - z);
        if (bd < 0 || d < bd)
        {
            bd = d, out->ok = 1;
            memcpy(out->box, box, sizeof box);
            out->scx = m->starts[i].start.chunk_x, out->scz = m->starts[i].start.chunk_z;
        }
    }
    int64_t seed = sr->pop.world.seed;
    neth_env = saved;
    if (!out->ok)
    {
        int k;
        if (sho_seed_rooms(seed, &k) < 0) return -1;
        pthread_mutex_lock(&sho_mu);
        for (int i = 0; i < sho_cache[k].n; ++i)
        {
            const int32_t *box = sho_cache[k].box[i];
            double cx = (box[0] + box[3]) / 2.0 + 0.5, cz = (box[2] + box[5]) / 2.0 + 0.5;
            double d = hypot(cx - x, cz - z);
            if (bd < 0 || d < bd)
            {
                bd = d, out->ok = 1;
                memcpy(out->box, box, 6 * sizeof *box);
                out->scx = sho_cache[k].cx[i], out->scz = sho_cache[k].cz[i];
            }
        }
        pthread_mutex_unlock(&sho_mu);
    }
    if (!out->ok) return -1;
    out->floor_y = out->box[1] + 1;
    return shoracle_read(p, id, out);
}

int shoracle_read(struct pipe *p, int id, struct shoracle_room *io)
{
    if (!io->ok) return -1;
    struct env *saved;
    struct world *w = sho_world(p, id, NULL, &saved);
    if (w == NULL) return -1;
    sho_read(w, io);
    neth_env = saved;
    return 0;
}

int shoracle_sees(struct pipe *p, int id, double ex, double ey, double ez, double tx, double ty, double tz, int x, int y,
                  int z)
{
    struct pool *pl = pipe_pool(p);
    struct session *ss = pool_session(pl, id);
    if (ss == NULL || ss->client_world == NULL) return 0;
    struct env *saved = neth_env;
    neth_env = pool_env(pl, id);
    struct rt_mop m;
    int hit = raytrace_blocks(ss->client_world, ex, ey, ez, tx, ty, tz, 0, 1, 0, &m) && m.type >= 0;
    neth_env = saved;
    return hit && m.x == x && m.y == y && m.z == z;
}

int shoracle_hostile(struct pipe *p, int id, double ex, double ey, double ez, double r, float xyz[3])
{
    struct pool *pl = pipe_pool(p);
    struct session *ss = pool_session(pl, id);
    if (ss == NULL || ss->cp.combat == NULL) return 0;
    struct env *saved = neth_env;
    neth_env = pool_env(pl, id);
    const struct clientworld *cw = &ss->cp.combat->client;
    int best = 0;
    double bd = r;
    for (int i = 0; i < cw->nents; ++i)
    {
        const struct client_entity *ce = &cw->ents[i];
        if (ce->is_dead || !ce->is_living || ce->dying) continue;
        int k = ce->kind;
        if (!((k >= HK_ZOMBIE && k <= HK_SPIDER) || (k >= HK_ENDERMAN && k <= HK_CAVE_SPIDER && k != HK_PIGMAN) ||
              k == SK_SLIME))
            continue;
        double my = ce->y + 0.5 * ce->height, dx = ce->x - ex, dy = my - ey, dz = ce->z - ez;
        double d = sqrt(dx * dx + dy * dy + dz * dz);
        if (d >= bd) continue;
        bd = d, best = ce->id;
        xyz[0] = (float)ce->x, xyz[1] = (float)my, xyz[2] = (float)ce->z;
    }
    neth_env = saved;
    return best;
}

/* ---------------------------------------------------------------- the way */

int shoracle_open(int b)
{
    switch (b)
    {
    case 0: case 6: case 31: case 32: case 37: case 38: case 39: case 40: case 50: case 55: case 59: case 63: case 66:
    case 68: case 69: case 70: case 72: case 75: case 76: case 77: case 78: case 83: case 104: case 105: case 106:
    case 131: case 132: case 141: case 142: case 143: case 147: case 148: case 157: case 175:
        return 1;
    default:
        return 0;
    }
}
int shoracle_hot(int b) { return b == 10 || b == 11 || b == 51; }
static int sho_water(int b) { return b == 8 || b == 9; }
int shoracle_floor(int b)
{
    return b > 0 && !shoracle_open(b) && !shoracle_hot(b) && !sho_water(b) && b != 30 && b != 81 && b != 85 &&
           b != 90 && b != 107 && b != 113 && b != 119 && b != 139;
}
/* a block the way may dig (its drop and its time aside) */
static int sho_dig_ok(int b)
{
    return b > 0 && b != 7 && b != 49 && b != 52 && b != 54 && b != 90 && b != 97 && b != 119 && b != END_FRAME_B &&
           !sho_water(b) && !shoracle_hot(b);
}

struct sho_grid { struct world *w; int x0, y0, z0, sx, sy, sz; };
static int sho_g(const struct sho_grid *g, int x, int y, int z) { return sho_get(g->w, x, y, z, NULL); }

static int sho_hot_beside(const struct sho_grid *g, int x, int y, int z, int water_too)
{
    static const int DX[4] = {1, -1, 0, 0}, DZ[4] = {0, 0, 1, -1};
    for (int k = 0; k < 4; ++k)
    {
        int b = sho_g(g, x + DX[k], y, z + DZ[k]);
        if (b < 0 || shoracle_hot(b) || (water_too && sho_water(b))) return 1;
    }
    return 0;
}

/* the feet may be at (x, y, z): 1 standing, 2 swimming, 0 not */
static int sho_cell(const struct sho_grid *g, int x, int y, int z)
{
    int fb = sho_g(g, x, y, z), hb = sho_g(g, x, y + 1, z);
    if (fb < 0 || hb < 0) return 0;
    int kind;
    if (shoracle_open(fb) && shoracle_open(hb) && shoracle_floor(sho_g(g, x, y - 1, z))) kind = 1;
    else if (sho_water(fb) && (sho_water(hb) || shoracle_open(hb))) kind = 2;
    else return 0;
    for (int dy = -1; dy <= 1; ++dy)
        if (sho_hot_beside(g, x, y + dy, z, 0)) return 0;
    if (shoracle_hot(sho_g(g, x, y + 2, z))) return 0;
    return kind;
}

static int sho_passable(int b) { return shoracle_open(b) || sho_water(b); }

/* the sand and gravel resting over (x, y, z) (each falls into the cell when it is dug: one more dig), -1 when what is
 * over them is liquid or hot (it would flow in) */
static int sho_falling(const struct sho_grid *g, int x, int y, int z)
{
    int k = 0, b;
    while ((b = sho_g(g, x, y + 1 + k, z)) == 12 || b == 13) ++k;
    if (b < 0 || shoracle_hot(b) || sho_water(b)) return -1;
    return k;
}

/* a dug move from (x, y, z) to (nx, ny, nz), ny y - 1, y or y + 1: the cells it opens dug (the feet's and head's there;
 * for a step up the head room over the jump, for a step down the head cell over the landing), a floor under, nothing
 * liquid or hot beside a dug cell, nothing liquid, hot or falling over the head; its cost */
static int sho_dug(const struct sho_grid *g, int x, int y, int z, int nx, int ny, int nz, int *cost)
{
    int cells[3][3] = {{nx, ny, nz}, {nx, ny + 1, nz}, {0, 0, 0}}, nc = 2, dig = 0;
    if (ny == y + 1) cells[2][0] = x, cells[2][1] = y + 2, cells[2][2] = z, nc = 3;
    else if (ny == y - 1) cells[2][0] = nx, cells[2][1] = ny + 2, cells[2][2] = nz, nc = 3;
    for (int i = 0; i < nc; ++i)
    {
        int b = sho_g(g, cells[i][0], cells[i][1], cells[i][2]);
        if (b < 0) return 0;
        if (shoracle_open(b)) continue;
        if (!sho_dig_ok(b)) return 0;
        if (sho_hot_beside(g, cells[i][0], cells[i][1], cells[i][2], 1)) return 0;
        int k = sho_falling(g, cells[i][0], cells[i][1], cells[i][2]);
        if (k < 0) return 0;
        dig += 1 + k;
    }
    if (dig == 0) return 0;     /* the walk's moves take it */
    if (!shoracle_floor(sho_g(g, nx, ny - 1, nz))) return 0;
    for (int dy = 0; dy <= 1; ++dy)
        if (sho_hot_beside(g, nx, ny + dy, nz, 0)) return 0;
    int top = sho_g(g, nx, ny + 2, nz);
    if (top < 0 || shoracle_hot(top) || sho_water(top)) return 0;
    if (top == 12 || top == 13)
    {
        /* sand over the head: it falls into the head's cell once that is open (dug again first) */
        int k = sho_falling(g, nx, ny + 1, nz);
        if (k < 0) return 0;
        dig += k;
    }
    *cost = 1 + SHO_DIG * dig + (ny != y);
    return 1;
}

/* a solid side of floor cell b (x, y, z) to place against, the player's own floor (px, pz at y) aside: 1, into n */
static int sho_face(const struct sho_grid *g, int x, int y, int z, int px, int pz, int n[3])
{
    static const int D[5][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}};
    for (int k = 0; k < 5; ++k)
    {
        int nx = x + D[k][0], ny = y + D[k][1], nz = z + D[k][2];
        if (ny == y && nx == px && nz == pz) continue;
        int b = sho_g(g, nx, ny, nz);
        if (shoracle_floor(b) || b == END_FRAME_B)
        {
            n[0] = nx, n[1] = ny, n[2] = nz;
            return 1;
        }
    }
    return 0;
}

int shoracle_bridge_face(struct pipe *p, int id, int bx, int by, int bz, int px, int pz, int n[3])
{
    struct env *saved;
    struct world *w = sho_world(p, id, NULL, &saved);
    if (w == NULL) return 0;
    struct sho_grid g = {w, 0, 0, 0, 0, 0, 0};
    int ok = sho_face(&g, bx, by, bz, px, pz, n);
    neth_env = saved;
    return ok;
}

int shoracle_path(struct pipe *p, int id, int fx, int fy, int fz, int tx, int ty, int tz, double near, int flags,
                  int16_t (*out)[3], int max)
{
    struct env *saved;
    struct world *w = sho_world(p, id, NULL, &saved);
    if (w == NULL) return 0;
    struct sho_grid g = {w, 0, 0, 0, 0, 0, 0};
    int lo = (fy < ty ? fy : ty) - SHO_DY, hi = (fy > ty ? fy : ty) + SHO_DY;
    if (lo < 1) lo = 1;
    if (hi > 253) hi = 253;
    int mx = (fx + tx) / 2, mz = (fz + tz) / 2;
    int hx = abs(fx - tx) / 2 + SHO_PAD, hz = abs(fz - tz) / 2 + SHO_PAD;
    if (hx > SHO_HALF) hx = SHO_HALF;
    if (hz > SHO_HALF) hz = SHO_HALF;
    g.x0 = mx - hx, g.z0 = mz - hz, g.y0 = lo, g.sx = 2 * hx + 1, g.sz = 2 * hz + 1, g.sy = hi - lo + 1;
    size_t ncell = (size_t)g.sx * g.sy * g.sz;
    int32_t *par = malloc(ncell * sizeof *par), *dist = malloc(ncell * sizeof *dist);
    size_t hcap = ncell < 65536 ? 65536 : ncell, hn = 0;
    int64_t *heap = malloc(hcap * sizeof *heap);
    int len = 0;
    if (par == NULL || dist == NULL || heap == NULL) goto done;
    for (size_t i = 0; i < ncell; ++i) par[i] = -1, dist[i] = INT32_MAX;
#define SHO_IDX(x, y, z) ((((size_t)((y) - g.y0) * g.sz) + (size_t)((z) - g.z0)) * g.sx + (size_t)((x) - g.x0))
#define SHO_IN(x, y, z) ((x) >= g.x0 && (x) < g.x0 + g.sx && (y) >= g.y0 && (y) < g.y0 + g.sy && (z) >= g.z0 && \
                         (z) < g.z0 + g.sz)
    if (!SHO_IN(fx, fy, fz)) goto done;
    size_t s0 = SHO_IDX(fx, fy, fz);
    par[s0] = (int32_t)s0, dist[s0] = 0;
    int goal = -1, best = -1;
    double bestd = 1e30, d0 = sqrt((double)(fx - tx) * (fx - tx) + (double)(fz - tz) * (fz - tz));
    static const int DX[4] = {1, -1, 0, 0}, DZ[4] = {0, 0, 1, -1};
    int gdy = (flags & SHO_EXACT) ? 0 : (flags & SHO_ANYY) ? 256 : SHO_GOAL_DY;
    heap[hn++] = (int64_t)s0;
    while (hn > 0)
    {
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
        if (cd != dist[c]) continue;
        int x = g.x0 + (int)(c % g.sx), z = g.z0 + (int)((c / g.sx) % g.sz), y = g.y0 + (int)(c / ((size_t)g.sx * g.sz));
        double ddx = x + 0.5 - (tx + 0.5), ddz = z + 0.5 - (tz + 0.5), dd = sqrt(ddx * ddx + ddz * ddz);
        if (c != (int32_t)s0 && dd <= near && abs(y - ty) <= gdy)
        {
            goal = c;
            break;
        }
        double wy = (flags & SHO_EXACT) ? 2.0 : (flags & SHO_ANYY) ? 0.0 : 0.25;
        if (c != (int32_t)s0 && dd + wy * abs(y - ty) < bestd) bestd = dd + wy * abs(y - ty), best = c;
        /* the cell's kind as the world is now; a cell the way digs (solid now) stands on its floor once dug */
        int here = sho_cell(&g, x, y, z);
        if (here == 0) here = 1;
        /* the moves: per way a walked one (a step up, level, or a drop) and the dug ones (one up, level, one down:
         * each, the walked one aside), and a swim down */
        int mv[20][4], nm = 0;
        for (int k = 0; k < 4; ++k)
        {
            int nx = x + DX[k], nz = z + DZ[k], ny = -1, cost = 0;
            /* a step up (head room over the jump; from water a climb out) */
            if (SHO_IN(nx, y + 1, nz) && sho_passable(sho_g(&g, x, y + 2, z)))
            {
                int kc = sho_cell(&g, nx, y + 1, nz);
                if (kc) ny = y + 1, cost = kc == 2 ? SHO_SWIM : 2;
            }
            /* level, else down the open column (3 at most; any depth of water ends it) */
            for (int dy = 0; ny < 0 && dy >= -3; --dy)
            {
                int yy = y + dy;
                if (!SHO_IN(nx, yy, nz)) break;
                if (!sho_passable(sho_g(&g, nx, yy + 1, nz))) break;
                int kc = sho_cell(&g, nx, yy, nz);
                if (kc)
                {
                    ny = yy, cost = (kc == 2 ? SHO_SWIM : 1) - dy;
                    break;
                }
                if (!shoracle_open(sho_g(&g, nx, yy, nz))) break;
            }
            if (ny < 0 && (flags & SHO_BRIDGE) && here == 1 && SHO_IN(nx, y, nz))
            {
                /* the bridge: the feet's and head's cells open, the floor air, a side to place it against */
                int n3[3];
                int fb = sho_g(&g, nx, y - 1, nz);
                if (shoracle_open(sho_g(&g, nx, y, nz)) && shoracle_open(sho_g(&g, nx, y + 1, nz)) && fb == 0 &&
                    !sho_hot_beside(&g, nx, y, nz, 0) && !sho_hot_beside(&g, nx, y - 1, nz, 1) &&
                    sho_face(&g, nx, y - 1, nz, x, z, n3))
                    ny = y, cost = 1 + SHO_BRIDGE_C;
            }
            if (ny >= 0 && ny < SHO_CAVE_Y && !(flags & SHO_BRIDGE)) cost += SHO_CAVE;   /* a cave's open cell */
            if (ny >= 0) mv[nm][0] = nx, mv[nm][1] = ny, mv[nm][2] = nz, mv[nm][3] = cost, ++nm;
            for (int m = 0; !(flags & SHO_NODIG) && here == 1 && m < 3; ++m)
            {
                int ty2 = y + (m == 0 ? 0 : m == 1 ? -1 : 1);
                if (ty2 == ny || !SHO_IN(nx, ty2, nz) || !SHO_IN(nx, ty2 + 2, nz)) continue;
                if (sho_dug(&g, x, y, z, nx, ty2, nz, &cost))
                    mv[nm][0] = nx, mv[nm][1] = ty2, mv[nm][2] = nz, mv[nm][3] = cost, ++nm;
            }
        }
        if (here == 2 && SHO_IN(x, y - 1, z) && sho_cell(&g, x, y - 1, z) == 2)
            mv[nm][0] = x, mv[nm][1] = y - 1, mv[nm][2] = z, mv[nm][3] = SHO_SWIM, ++nm;
        for (int j = 0; j < nm; ++j)
        {
            int nx = mv[j][0], ny = mv[j][1], nz = mv[j][2], cost = mv[j][3];
            size_t ni = SHO_IDX(nx, ny, nz);
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
#ifdef SH_TRACE
    {
        int bx = best >= 0 ? g.x0 + (int)(best % g.sx) : 0, bz = best >= 0 ? g.z0 + (int)((best / g.sx) % g.sz) : 0,
            by = best >= 0 ? g.y0 + (int)(best / ((size_t)g.sx * g.sz)) : 0;
        size_t reached = 0;
        for (size_t i = 0; i < ncell; ++i) reached += dist[i] != INT32_MAX;
        fprintf(stderr, "sho_path id %d from %d %d %d (cell %d floor %d) to %d %d %d: reached %zu goal %d best %d %d %d "
                        "(%.1f, d0 %.1f) box %d %d %d size %d %d %d\n",
                id, fx, fy, fz, sho_cell(&g, fx, fy, fz), sho_g(&g, fx, fy - 1, fz), tx, ty, tz, reached, goal, bx, by,
                bz, bestd, d0, g.x0, g.y0, g.z0, g.sx, g.sy, g.sz);
        if (goal < 0 && best >= 0)
            for (int dx = -1; dx <= 1; ++dx)
            {
                fprintf(stderr, "  column %d %d:", bx + dx, bz);
                for (int y = by + 3; y >= by - 10; --y) fprintf(stderr, " %d", sho_g(&g, bx + dx, y, bz));
                fprintf(stderr, "\n");
            }
    }
#endif
    if (goal < 0 && best >= 0 && bestd + SHO_GAIN <= d0) goal = best;
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
#undef SHO_IDX
#undef SHO_IN
done:
    free(par);
    free(dist);
    free(heap);
    neth_env = saved;
    return len;
}

/* lane/snowleg (snow_kit far) */
static int sho_snowy(int b)
{
    return b == 12 || b == 13 || b == 26 || b == 30 || b == 31 || b == 140 || b == 158;
}

#define SHO_SNOW_TILE 64        /* biome generation cells a tile side (256 blocks) */

int shoracle_snow_column(struct pipe *p, int id, double x, double z, int r, int *tx, int *tz)
{
    struct env *saved;
    struct world *w = sho_world(p, id, NULL, &saved);
    if (w == NULL) return -1;
    const struct layers *g = &world_gen(w)->terrain.layers;
    /* the generation layer's cells (block >> 2) over the square of r round (x, z), a tile at a time with a cell of
     * margin each side (the neighbours' test) */
    int cx = (int)floor(x) >> 2, cz = (int)floor(z) >> 2, cr = r / 4 + 1;
    int T = SHO_SNOW_TILE, W = T + 2;
    int *buf = malloc((size_t)W * W * sizeof *buf);
    if (buf == NULL)
    {
        neth_env = saved;
        return -1;
    }
    double bd = -1;
    for (int z0 = cz - cr; z0 <= cz + cr; z0 += T)
        for (int x0 = cx - cr; x0 <= cx + cr; x0 += T)
        {
            biomes_gen(g, x0 - 1, z0 - 1, W, W, buf);
            for (int j = 0; j < T; ++j)
                for (int i = 0; i < T; ++i)
                {
                    int gx = x0 + i, gz = z0 + j;
                    if (abs(gx - cx) > cr || abs(gz - cz) > cr) continue;
                    double d = hypot((double)(gx - cx), (double)(gz - cz));
                    if (bd >= 0 && d >= bd) continue;
                    int ok = 1;
                    for (int k = 0; k < 9 && ok; ++k) ok = sho_snowy(buf[(j + 1 + k / 3 - 1) * W + (i + 1 + k % 3 - 1)]);
                    if (!ok) continue;
                    bd = d, *tx = gx * 4 + 2, *tz = gz * 4 + 2;
                }
        }
    free(buf);
    neth_env = saved;
    return bd >= 0 && bd * 4 <= r ? 0 : -1;
}

/* lane/chain3 (snow_height, PRIVILEGED): a nearby high Extreme Hills column can snow even when no cold biome lies
 * inside the far biome search. The local snow scanner must still see and dig a real snow block after the walk. */
int shoracle_snow_height_column(struct pipe *p, int id, double x, double z, int r,
                                 int *tx, int *ty, int *tz)
{
    struct env *saved;
    struct world *w = sho_world(p, id, NULL, &saved);
    if (w == NULL) return -1;
    struct terrain *t = malloc(sizeof *t);
    uint16_t *blocks = malloc(CHUNK_CELLS * sizeof *blocks);
    if (t == NULL || blocks == NULL)
    {
        free(t), free(blocks);
        neth_env = saved;
        return -1;
    }
    terrain_init(t, world_gen(w)->terrain.seed);
    int pcx = (int)floor(x) >> 4, pcz = (int)floor(z) >> 4, span = r / 16 + 1;
    double best = -1;
    int bx = 0, by = 0, bz = 0;
    for (int cz = pcz - span; cz <= pcz + span; ++cz)
        for (int cx = pcx - span; cx <= pcx + span; ++cx)
        {
            terrain_density(t, cx, cz, blocks);
            int biome[256];
            biomes_full(&t->layers, cx * 16, cz * 16, 16, 16, biome);
            for (int lz = 0; lz < 16; ++lz)
                for (int lx = 0; lx < 16; ++lx)
                {
                    int b = biome[lz * 16 + lx];
                    if (b != 3 && b != 20 && b != 34) continue;
                    int gx = cx * 16 + lx, gz = cz * 16 + lz;
                    double d = hypot(gx + 0.5 - x, gz + 0.5 - z);
                    if (d > r || (best >= 0 && d >= best)) continue;
                    for (int y = 255; y >= 94; --y)
                        if (blocks[CELL(lx, y, lz)] != BLK_AIR &&
                            blocks[CELL(lx, y, lz)] != BLK_WATER)
                        {
                            if (y + 1 >= 95) best = d, bx = gx, by = y + 1, bz = gz;
                            break;
                        }
                }
        }
    free(blocks), free(t);
    neth_env = saved;
    if (best < 0) return -1;
    *tx = bx, *ty = by, *tz = bz;
    return 0;
}

int shoracle_log(struct pipe *p, int id, int x, int y, int z, int r, int dy, const int (*bad)[3], int nbad, int xyz[3])
{
    struct env *saved;
    struct world *w = sho_world(p, id, NULL, &saved);
    if (w == NULL) return -1;
    double bd = -1;
    for (int yy = y - dy; yy <= y + dy; ++yy)
        for (int dz = -r; dz <= r; ++dz)
            for (int dx = -r; dx <= r; ++dx)
            {
                int b = sho_get(w, x + dx, yy, z + dz, NULL);
                if (b != 17 && b != 162) continue;
                int u = sho_get(w, x + dx, yy - 1, z + dz, NULL);
                if (u != 2 && u != 3) continue;
                double d = (double)dx * dx + (double)dz * dz + 4.0 * (double)(yy - y) * (yy - y);
                if (bd >= 0 && d >= bd) continue;
                int hot = 0;
                for (int k = 0; k < nbad && !hot; ++k)
                    hot = bad[k][0] == x + dx && bad[k][1] == yy && bad[k][2] == z + dz;
                for (int k = 0; k < 27 && !hot; ++k)
                {
                    int n = sho_get(w, x + dx + k % 3 - 1, yy + k / 3 % 3 - 1, z + dz + k / 9 - 1, NULL);
                    hot = n == 8 || n == 9 || shoracle_hot(n);
                }
                if (hot) continue;
                bd = d, xyz[0] = x + dx, xyz[1] = yy, xyz[2] = z + dz;
            }
    neth_env = saved;
    return bd >= 0 ? 0 : -1;
}

int shoracle_loaded_log_stand(struct pipe *p, int id, int x, int y, int z, int down, int up, int out[6])
{
    struct env *saved;
    struct world *w = sho_world(p, id, NULL, &saved);
    if (w == NULL) return -1;
    static const int dx[4] = {1, -1, 0, 0}, dz[4] = {0, 0, 1, -1};
    double best = -1;
    for (size_t i = 0; i < w->lon; ++i)
    {
        int64_t key = w->load_order[i];
        int cx = (int32_t)key, cz = (int32_t)(key >> 32);
        struct chunk *c = world_chunk(w, cx, cz);
        if (c == NULL) continue;
        for (int yy = y - down < 1 ? 1 : y - down; yy <= y + up && yy < 254; ++yy)
            for (int lz = 0; lz < 16; ++lz)
                for (int lx = 0; lx < 16; ++lx)
                {
                    int b = chunk_get_block(c, lx, yy, lz) & 4095;
                    if (b != 17 && b != 162) continue;
                    int gx = cx * 16 + lx, gz = cz * 16 + lz;
                    double xx = (double)gx - x, zz = (double)gz - z, yy0 = 2.0 * ((double)yy - y);
                    double score = xx * xx + zz * zz + yy0 * yy0;
                    if (best >= 0 && score >= best) continue;
                    for (int k = 0; k < 4; ++k)
                    {
                        int sx = gx + dx[k], sz = gz + dz[k];
                        if (!shoracle_floor(sho_get(w, sx, yy - 1, sz, NULL)) ||
                            !shoracle_open(sho_get(w, sx, yy, sz, NULL)) ||
                            !shoracle_open(sho_get(w, sx, yy + 1, sz, NULL))) continue;
                        best = score;
                        out[0] = gx, out[1] = yy, out[2] = gz;
                        out[3] = sx, out[4] = yy, out[5] = sz;
                        break;
                    }
                }
    }
    neth_env = saved;
    return best >= 0 ? 0 : -1;
}

static int sho_sky_through_foliage(struct world *w, int x, int eye_y, int z)
{
    int top = world_get_height_value(w, x, z);
    if (top < 1) return 0;
    for (int y = eye_y; y < top; ++y)
    {
        int b = sho_get(w, x, y, z, NULL);
        if (b != 0 && b != 17 && b != 18 && b != 161 && b != 162) return 0;
    }
    return 1;
}

static int sho_dry_column(struct world *w, int x, int y, int z)
{
    int top = world_get_height_value(w, x, z);
    int floor_b = sho_get(w, x, y - 1, z, NULL);
    if (top < 2 || top > 252 || y > 253 ||
        (y < top && !sho_sky_through_foliage(w, x, y + 1, z)) ||
        floor_b == 17 || floor_b == 18 || floor_b == 161 || floor_b == 162 ||
        !shoracle_floor(floor_b) ||
        !shoracle_open(sho_get(w, x, y, z, NULL)) ||
        !shoracle_open(sho_get(w, x, y + 1, z, NULL))) return 0;
    for (int dz = -4; dz <= 4; ++dz)
        for (int dx = -4; dx <= 4; ++dx)
        {
            int h = world_get_height_value(w, x + dx, z + dz);
            if (h < 2) return 0;
            int b = sho_get(w, x + dx, h - 1, z + dz, NULL);
            if (b < 0 || b == 8 || b == 9 || shoracle_hot(b)) return 0;
        }
    return 1;
}

int shoracle_dry_here(struct pipe *p, int id, int x, int y, int z)
{
    struct env *saved;
    struct world *w = sho_world(p, id, NULL, &saved);
    int yes = w && sho_dry_column(w, x, y, z);
    neth_env = saved;
    return yes;
}

int shoracle_dry_near(struct pipe *p, int id, int x, int z, int r, int out[3])
{
    struct env *saved;
    struct world *w = sho_world(p, id, NULL, &saved);
    if (w == NULL) return -1;
    int best = -1;
    for (int dz = -r; dz <= r; ++dz)
        for (int dx = -r; dx <= r; ++dx)
        {
            int d = dx * dx + dz * dz;
            if (d > r * r || (best >= 0 && d >= best)) continue;
            int yy = world_get_height_value(w, x + dx, z + dz);
            if (!sho_dry_column(w, x + dx, yy, z + dz)) continue;
            best = d;
            out[0] = x + dx, out[1] = yy, out[2] = z + dz;
        }
    neth_env = saved;
    return best >= 0 ? 0 : -1;
}

int shoracle_sky_here(struct pipe *p, int id, int x, int eye_y, int z)
{
    struct env *saved;
    struct world *w = sho_world(p, id, NULL, &saved);
    int yes = w && sho_sky_through_foliage(w, x, eye_y, z);
    neth_env = saved;
    return yes;
}

#ifdef PEARL_CENSUS_TRACE
int shoracle_monster_count_cap(struct pipe *p, int id, int *count, int *cap, int *eligible)
{
    struct env *saved;
    struct serverreplay *sr;
    if (sho_world(p, id, &sr, &saved) == NULL) return -1;
    struct spawner *sp = &sr->dims[sr_dim_index(0)].spawner;
    int n = 0;
    uint8_t want = (uint8_t)((CT_MONSTER + 1) << 2 | 1);
    for (int i = 0; i < sp->nrecs; ++i) n += sp->rec_flags[i] == want;
    int limit = CAP_MONSTER + (sp->neg_cap_type == CT_MONSTER ? sp->neg_cap_add : 0);
    *count = n;
    *eligible = sp->elig_n;
    *cap = limit * sp->elig_n / 256;
    neth_env = saved;
    return 0;
}

#endif
