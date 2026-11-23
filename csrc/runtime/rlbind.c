/* The trainer's binding (rlbind.h). */
#define _GNU_SOURCE
#include "rlbind.h"
#include "rlraw.h"
#include "rlscan.h"

#include <dlfcn.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../engine/env.h"
#include "../engine/envmem.h"
#include "../engine/combat.h"
#include "../engine/itemuse.h"
#include "../engine/living.h"
#include "../engine/raytrace.h"
#include "../engine/player.h"
#include "../engine/place.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "../engine/recipes.h"
#pragma GCC diagnostic pop
#include "../engine/seedworld.h"
#include "../engine/serverreplay.h"
#include "../engine/session.h"
#include "../engine/structure.h"
#include "../engine/structure_blocks.h"
#include "../engine/temple.h"
#include "../engine/village.h"
#include "../engine/survival.h"
#include "../engine/tileentity.h"
#include "../engine/world.h"
#include "../engine/worldconf.h"
#include "moveact.h"
#include "pipeline.h"
#include "rlact.h"
#include "rlact_int.h"
#include "rlgui.h"
#include "rlgui_int.h"
#include "rlmove.h"
#include "semcam.h"
#include "raycast.h"
#include "rchead.h"
#include "rcmirror.h"
#include "../cuda/raycast/rcdev.h"
#include "vecenv.h"
#include "endview.h"

_Static_assert(NETH_RL_KEYS == PK_KEYS, "neth_rl_act.press is pool_act.press");
_Static_assert(NETH_RL_OPS == POOL_GUI_OPS, "neth_rl_act.ops is pool_act.ops");
_Static_assert(NETH_RL_SEEN == RC_SEEN, "neth_rl_state.seen is raycast_seen's");

enum { NETH_RL_STARTS = 4096 };

/* a start of the binding's (its index is its place in nwrl.start): the
 * pool's, and for one saved from an env (neth_rl_start_save) that env's
 * binding state then, which an env reset to it takes */
struct neth_rl_start {
    struct pool_start *s;
    int saved;
    struct neth_rl_state st;
    int64_t last_scan;
    int last_window;
    char stage[48];             /* lane/proom: a library start's stage ("" none; neth_rl_start_stage) */
};

struct neth_rl_env {
    struct neth_rl_state st;       /* written by the worker stepping the env, read after its poll */
    int64_t last_scan;          /* the tick of the last chest scan (-1 none since the reset) */
    int last_window;
    FILE *rows;                 /* rlgui.h neth_rl_rows: each tick's row record */
    int64_t cw_from;
    struct rlact_env act;       /* rlact.h: the action compiler's queue */
    struct rlraw_env *raw;      /* rlraw.h: the raw form's pointer and screen input (neth_rl_raw on) */
    struct neth_rl_end end;        /* lane/dragon: the End in view (endview.h), at the step's end; zero elsewhere */
    struct neth_rl_far far;        /* lane/pearl: neth_rl_far's, written with st */
    /* bit-exact cache for census_scan's hi_* scan (invalidated on world/pos/bseq/map_ver/reset) */
    const struct world *hi_w;
    int64_t hi_seed, hi_t;
    uint64_t hi_bseq;
    uint32_t hi_mver;
    int hi_px, hi_pz, hi_best, hi_d_cached;
    float hi_x, hi_y, hi_z;
};

struct nwrl {
    struct pipe *p;
    char *config;               /* the pool keeps the path */
    struct neth_rl_start *start[NETH_RL_STARTS];  /* NULL: a free index (neth_rl_start_free) */
    int nstart;                 /* the indices used so far */
    int n, w, h, ticks, device;
    int chest_radius, chest_every;
    struct neth_rl_actconf actconf;    /* rlact.h */
    struct neth_rl_env *e;
    struct pool_act *acts;      /* neth_rl_step's conversion */
    const struct pipe_result **res;
    struct moveact *mv;         /* the action compiler (rlmove.h) */
    /* the semantic camera (client.camera semantic or both): env id's cast at
     * sem_buf + id * sem.w * sem.h, written at its step's end */
    int raw;                    /* neth_rl_raw: the raw keyboard and mouse form (rlraw.h) */
    struct rlscan_spec *scan;   /* neth_rl_scan: the privileged world scan (rlscan.h), NULL off */
    struct rlscan_out *scan_buf;
    /* neth_rl_layer_auto: each env's screen layer at its step's end (NULL off) */
    float *layer_mul, *layer_add;
    int32_t *layer_flag;
    int camera;
    struct semcam_spec sem;
    struct semcam_cell *sem_buf;
    /* the textured ray cast (client.camera raycast): env id's RGB frame at
     * rc_buf + id * rc.w * rc.h * 3, written at its step's end */
    int rc_on;
    struct raycast_spec rc;
    uint8_t *rc_buf;
    struct raycast_keep *rc_keep;   /* each env's last host frame and its inputs (raycast.h raycast_seen_keep) */
    /* engine.raycast device (lane/rcdev): the rays on the GPU, the frames there (rc_buf NULL); the worker stages
     * each step's cast (its setup, the world's changes), a launch draws a group's (neth_rl_raycast_launch) */
    struct rcdev *rcd;
    uint64_t rcd_refused;       /* casts the device copy could not take (black frames) */
    /* the split (engine.raycast_host_groups, lane/rcsplit): rc_host[id] 1, env id casts on its worker into rc_hbuf
     * (rcdev_host_frames, page-locked) and a put copies its row to the device's frames */
    int rc_host_groups;
    uint8_t *rc_host, *rc_hbuf;
    struct neth_vec *vec;       /* vecenv.h: the vector env's act and books on the workers (NULL off) */
};

int neth_rl_version(void) { return NWRL_VERSION; }

const struct neth_rl_end *neth_rl_end_view(const struct nwrl *h, int id) { return &h->e[id].end; }

static void seterr(char *err, int n, const char *what, const char *more)
{
    if (err && n > 0) snprintf(err, (size_t)n, "%s%s%s", what, more ? ": " : "", more ? more : "");
}

/* ------------------------------------------------------------ the state */

static int stack_items(const struct te_stack *s, int k)
{
    int n = 0;
    for (int i = 0; i < k; ++i)
        if (s[i].item >= 0 && s[i].count > 0) n += s[i].count;
    return n;
}

/* chunk c's chests merged into the env's list (a new one appended with its
 * item count; a known one's count updated: what the player took is what it
 * lost) */
static void chest_chunk(struct neth_rl_state *st, const struct chunk *c, int dim)
{
    for (int i = 0; i < c->tes.n; ++i)
    {
        const struct tile_entity *te = c->tes.v[i];
        if (te == NULL || te->kind != TE_CHEST || te->invalid) continue;
        int items = stack_items(te->u.chest.slots, 27), k = 0;
        while (k < st->nchest && !(st->chest[k].x == te->x && st->chest[k].y == te->y && st->chest[k].z == te->z &&
                                   st->chest[k].dim == dim))
            ++k;
        if (k == st->nchest)
        {
            if (st->nchest == NETH_RL_CHESTS) continue;
            ++st->nchest;
            st->chest[k].x = te->x;
            st->chest[k].y = te->y;
            st->chest[k].z = te->z;
            st->chest[k].dim = dim;
            st->chest[k].opened = 0;
            st->chest[k].items0 = items;
            st->chest[k].items = items;
            continue;
        }
        if (items < st->chest[k].items) st->chest_items_taken += st->chest[k].items - items;
        st->chest[k].items = items;
    }
}

/* every chest in the loaded chunks within R chunks of (px, pz) of world W,
 * chunk by chunk (x, then z) */
static void chest_scan_all(int R, struct neth_rl_state *st, struct world *w, int dim, int cx0, int cz0)
{
    for (int cx = cx0 - R; cx <= cx0 + R; ++cx)
        for (int cz = cz0 - R; cz <= cz0 + R; ++cz)
        {
            const struct chunk *c = world_chunk(w, cx, cz);
            if (c != NULL) chest_chunk(st, c, dim);
        }
}

enum { CHEST_CHUNKS = 64 };

/* chest_scan_all over only the window's chunks that hold a chest: a chest in
 * a loaded chunk's map is on the world's tile entity walk lists (te_list, or
 * te_added during the walk: World.loadedTileEntityList, which a chunk's load
 * and setTileEntity fill), so the chests on those lists name the chunks, in
 * the walk's order; the 289 chunks' map lines it read were 2.5% of the env's
 * L3 misses (lane/sessheap). More such chunks than CHEST_CHUNKS: the walk. */
static void chest_scan(struct nwrl *h, struct neth_rl_state *st, struct world *w, int dim, double px, double pz)
{
    int R = h->chest_radius, cx0 = (int)floor(px) >> 4, cz0 = (int)floor(pz) >> 4;
    int64_t key[CHEST_CHUNKS];
    int n = 0;
    for (int l = 0; l < 2; ++l)
    {
        struct tile_entity *const *v = l ? w->te_added : w->te_list;
        int m = l ? w->te_added_n : w->te_n;
        for (int i = 0; i < m; ++i)
        {
            const struct tile_entity *te = v[i];
            if (te == NULL || te->kind != TE_CHEST || te->invalid) continue;
            int cx = te->x >> 4, cz = te->z >> 4;
            if (cx < cx0 - R || cx > cx0 + R || cz < cz0 - R || cz > cz0 + R) continue;
            /* (cx, cz) as one key ordered as the walk: x, then z */
            int64_t k = (int64_t)(cx - cx0 + R) << 32 | (uint32_t)(cz - cz0 + R);
            int j = n;
            while (j > 0 && key[j - 1] > k) --j;
            if (j > 0 && key[j - 1] == k) continue;
            if (n == CHEST_CHUNKS)
            {
                chest_scan_all(R, st, w, dim, cx0, cz0);
                return;
            }
            memmove(&key[j + 1], &key[j], (size_t)(n - j) * sizeof *key);
            key[j] = k;
            ++n;
        }
    }
    for (int j = 0; j < n; ++j)
    {
        const struct chunk *c = world_chunk(w, cx0 - R + (int)(key[j] >> 32), cz0 - R + (int)(key[j] & 0xffffffff));
        if (c != NULL) chest_chunk(st, c, dim);
    }
}

/* lane/fight: a living kind a fight is for */
static int hostile_kind(int k)
{
    return (k >= HK_ZOMBIE && k <= HK_SPIDER) || k == GK_GHAST || (k >= HK_ENDERMAN && k <= HK_CAVE_SPIDER) ||
           k == SK_SLIME || k == SK_MAGMA_CUBE;
}

/* lane/fight: the crosshair's living and the hostile livings in view (neth_rl_state's mo_entity, vis): the client
 * entities within 16 blocks of the eye and 75 degrees of the look whose box middle the eye sees past every block;
 * lane/food: the food animals (pig, cow, mooshroom, chicken) within 48 blocks after every hostile, nearest first
 * among them (the nearest of seed 42's to its spawn stand 78 blocks off) */
/* lane/pearl: an enderman for neth_rl_far (vis_scan's rule out to NETH_RL_FAR_R blocks: within 75 degrees of the look, the
 * box middle seen past every block), kept nearest first */
static void far_add(struct neth_rl_far *far, struct session *ss, const struct client_entity *ce, double ex, double ey,
                    double ez, double my, double d2, double lx, double ly, double lz, double cmin)
{
    double d = sqrt(d2);
    if (((ce->x - ex) * lx + (my - ey) * ly + (ce->z - ez) * lz) / d < cmin) return;
    int k = far->n;
    if (k == NETH_RL_FAR && d2 >= far->d2[NETH_RL_FAR - 1]) return;
    struct rt_mop m;
    if (raytrace_blocks(ss->client_world, ex, ey, ez, ce->x, my, ce->z, 0, 1, 0, &m) && m.type >= 0) return;
    if (k == NETH_RL_FAR) --k;
    while (k > 0 && far->d2[k - 1] > d2)
    {
        far->d2[k] = far->d2[k - 1];
        far->e[k] = far->e[k - 1];
        --k;
    }
    far->d2[k] = (float)d2;
    far->e[k].id = ce->id;
    far->e[k].scream = (ce->kindw >> 16) & 1;
    far->e[k].x = (float)ce->x;
    far->e[k].y = (float)ce->y;
    far->e[k].z = (float)ce->z;
    far->e[k].h = ce->height;
    if (far->n < NETH_RL_FAR) ++far->n;
}

static void vis_scan(struct neth_rl_state *st, struct neth_rl_far *far, struct session *ss, struct client_player *cp)
{
    const struct surv_mouseover_pos *mo = &neth_env->survival.mo.cur;
    struct clientworld *cw = cp->combat ? &cp->combat->client : NULL;
    st->mo_entity = mo->entity_id;     /* the entity half wins: num 0, entity_id set (survival.c surv_client_mouseover) */
    st->mo_ekind = -1;
    st->nvis = 0;
    far->n = 0;
    memset(&far->near, 0, sizeof far->near);
    if (cw == NULL || ss->client_world == NULL) return;
    if (st->mo_entity)
    {
        struct client_entity *ce = clientworld_get_entity(cw, st->mo_entity);
        st->mo_ekind = ce && ce->is_living ? ce->kind : -1;
    }
    double ex = cp->e.pos_x, ey = cp->e.pos_y + (double)(cp->e.y_offset - 1.62F), ez = cp->e.pos_z;
    double yr = (double)cp->rotation_yaw * M_PI / 180.0, pr = (double)cp->rotation_pitch * M_PI / 180.0;
    double lx = -sin(yr) * cos(pr), ly = -sin(pr), lz = cos(yr) * cos(pr), cmin = cos(75.0 * M_PI / 180.0);
    float best[NETH_RL_VIS];
    for (int i = 0; i < cw->nents; ++i)
    {
        const struct client_entity *ce = &cw->ents[i];
        int food = ce->kind == AK_PIG || ce->kind == AK_COW || ce->kind == AK_MOOSHROOM || ce->kind == AK_CHICKEN;
        if (!ce->is_living || ce->is_dead || ce->dying || !(hostile_kind(ce->kind) || food)) continue;
        double my = ce->y + 0.5 * ce->height, dx = ce->x - ex, dy = my - ey, dz = ce->z - ez;
        double d2 = dx * dx + dy * dy + dz * dz;
        if (ce->kind == HK_ENDERMAN && d2 <= NETH_RL_FAR_R * NETH_RL_FAR_R && d2 >= 1e-6)
            far_add(far, ss, ce, ex, ey, ez, my, d2, lx, ly, lz, cmin);
        if (ce->kind == HK_ENDERMAN && d2 <= NETH_RL_NEAR_R * NETH_RL_NEAR_R && (!far->near.id || d2 < far->near.d * far->near.d))
        {
            far->near.id = ce->id, far->near.scream = (ce->kindw >> 16) & 1;
            far->near.x = (float)ce->x, far->near.y = (float)ce->y, far->near.z = (float)ce->z;
            far->near.h = ce->height, far->near.d = (float)sqrt(d2);
        }
        if (d2 > (food ? 6400.0 : 256.0) || d2 < 1e-6) continue;   /* a food animal within 80 blocks (the tracker's own
                                                                        * reach: the spawn world's nearest sit 75 to 81 out) */
        double d = sqrt(d2);
        if ((dx * lx + dy * ly + dz * lz) / d < cmin) continue;
        if (food) d2 += 1000.0;             /* the rank's: after every hostile (a hostile's d2 is at most 256) */
        int k = st->nvis < NETH_RL_VIS ? st->nvis : NETH_RL_VIS;
        if (k == NETH_RL_VIS && d2 >= best[NETH_RL_VIS - 1]) continue;
        struct rt_mop m;
        if (raytrace_blocks(ss->client_world, ex, ey, ez, ce->x, my, ce->z, 0, 1, 0, &m) && m.type >= 0) continue;
        if (k == NETH_RL_VIS) --k;
        while (k > 0 && best[k - 1] > d2)
        {
            best[k] = best[k - 1];
            st->vis[k] = st->vis[k - 1];
            --k;
        }
        best[k] = (float)d2;
        st->vis[k].id = ce->id;
        st->vis[k].kind = ce->kind;
        st->vis[k].x = (float)ce->x;
        st->vis[k].y = (float)ce->y;
        st->vis[k].z = (float)ce->z;
        st->vis[k].h = ce->height;
        if (st->nvis < NETH_RL_VIS) ++st->nvis;
    }
}

static void depth_scan(struct neth_rl_far *far, struct session *ss, struct client_player *cp);

/* lane/pearl2: neth_rl_census (rlbind.h), measurement only: the server's livings read, nothing written */
static int census_sky(struct world *w, double x, double y, double z)
{
    return (int)floor(y + 1e-3) >= world_get_height_value(w, (int)floor(x), (int)floor(z));
}

static void census_scan(struct neth_rl_env *ne, struct neth_rl_census *c, struct session *ss, struct client_player *cp, int64_t t)
{
    const struct server_player *sp = &ss->sp;
    c->alive = c->n = c->pl_sky = c->skel_los = 0;
    memset(c->hostile, 0, sizeof c->hostile);
    if (ss->sr == NULL) return;
    int di = sr_dim_index(sp->dimension);
    struct world *w = ss->sr->w[di].st.w;
    struct an_world *an = &ss->sr->dims[di].anw;
    struct clientworld *cw = cp->combat ? &cp->combat->client : NULL;
    if (w == NULL) return;
    double ex = cp->e.pos_x, ey = cp->e.pos_y + (double)(cp->e.y_offset - 1.62F), ez = cp->e.pos_z;
    double yr = (double)cp->rotation_yaw * M_PI / 180.0, pr = (double)cp->rotation_pitch * M_PI / 180.0;
    double lx = -sin(yr) * cos(pr), ly = -sin(pr), lz = cos(yr) * cos(pr), cmin = cos(75.0 * M_PI / 180.0);
    c->pl_sky = census_sky(w, sp->e.pos_x, sp->e.pos_y, sp->e.pos_z);
    for (int k = 0; k < 5; ++k) c->hnear[k] = -1.0f;    /* lane/proom (a trace's) */
    c->ground_dy = -1.0f;
    {
        int gx = (int)floor(sp->e.pos_x), gz = (int)floor(sp->e.pos_z), fy = (int)floor(sp->e.pos_y + 1e-3);
        for (int y = fy - 1; y >= 0 && y >= fy - 64; --y)
        {
            int b = world_get_block(w, gx, y, gz);
            if (b == 0 || b == 31 || b == 37 || b == 38 || b == 78) continue;
            c->ground_dy = (float)(sp->e.pos_y - (double)(y + 1));
            break;
        }
    }
    c->biome = world_get_biome(w, (int)floor(sp->e.pos_x), (int)floor(sp->e.pos_z));
    /* lane/pearlsee: the highest dry, treeless column within NETH_RL_CENSUS_HI (every 2 blocks; the nearest of the ties).
     * Chunk-wise scan + bit-exact cache keyed on (world, seed, px, pz, bseq, map_ver, monotonic tick). */
    c->hi_d = -1, c->hi_dy = 0;
    if (sp->dimension == 0)
    {
        int px = (int)floor(sp->e.pos_x), pz = (int)floor(sp->e.pos_z);
        if (!(ne->hi_w == w && ne->hi_seed == w->seed && t > ne->hi_t &&
              ne->hi_px == px && ne->hi_pz == pz && ne->hi_bseq == w->bseq && ne->hi_mver == w->map_ver))
        {
            int best = -1, bd2i = INT_MAX, best_x = 0, best_z = 0;
            const int r = NETH_RL_CENSUS_HI, r2 = r * r;
            const int x0 = px - r, x1 = px + r, z0 = pz - r, z1 = pz + r;
            for (int cz = z0 >> 4; cz <= z1 >> 4; ++cz)
            {
                int zlo = cz * 16 > z0 ? cz * 16 : z0;
                int zhi = cz * 16 + 15 < z1 ? cz * 16 + 15 : z1;
                if ((zlo - z0) & 1) ++zlo;
                for (int cx = x0 >> 4; cx <= x1 >> 4; ++cx)
                {
                    struct chunk *ch = world_chunk(w, cx, cz);
                    if (ch == NULL) continue;
                    int xlo = cx * 16 > x0 ? cx * 16 : x0;
                    int xhi = cx * 16 + 15 < x1 ? cx * 16 + 15 : x1;
                    if ((xlo - x0) & 1) ++xlo;
                    for (int z = zlo; z <= zhi; z += 2)
                    {
                        int dz = z - pz, dz2 = dz * dz;
                        int row = (z & 15) << 4;
                        for (int x = xlo; x <= xhi; x += 2)
                        {
                            int dx = x - px, d2i = dx * dx + dz2;
                            if (d2i > r2) continue;
                            int hgt = ch->height[row | (x & 15)];
                            if (hgt <= 0 || hgt > 255 || hgt < best) continue;
                            if (hgt == best && (d2i > bd2i || (d2i == bd2i && (z > best_z || (z == best_z && x >= best_x)))))
                                continue;
                            int b = chunk_get_block(ch, x & 15, hgt - 1, z & 15) & 4095;
                            if (b == 17 || b == 18 || (b >= 8 && b <= 11) || b == 0) continue;
                            best = hgt, bd2i = d2i, best_x = x, best_z = z;
                        }
                    }
                }
            }
            ne->hi_w = w;
            ne->hi_seed = w->seed;
            ne->hi_bseq = w->bseq;
            ne->hi_mver = w->map_ver;
            ne->hi_px = px;
            ne->hi_pz = pz;
            ne->hi_best = best;
            if (best >= 0)
            {
                ne->hi_x = (float)best_x + 0.5f;
                ne->hi_y = (float)best;
                ne->hi_z = (float)best_z + 0.5f;
                ne->hi_d_cached = (int32_t)lround(sqrt((double)bd2i));
            }
            else
                ne->hi_d_cached = -1;
        }
        ne->hi_t = t;
        if (ne->hi_best >= 0)
        {
            c->hi_x = ne->hi_x, c->hi_y = ne->hi_y, c->hi_z = ne->hi_z;
            c->hi_d = ne->hi_d_cached;
            c->hi_dy = ne->hi_best - (int)floor(sp->e.pos_y + 1e-3);
        }
    }
    /* lane/pearlsee (3rd pass): the ender pearl items about (the living world's drops and the other item list) */
    c->pearl_n = 0;
    {
        double pb = NETH_RL_CENSUS_PEARL_R * NETH_RL_CENSUS_PEARL_R;
        const struct ie_world *L[2] = {&an->iew, an->iew.peer};
        for (int k = 0; k < 2; ++k)
            for (int i = 0; L[k] != NULL && i < L[k]->n; ++i)
            {
                const ie_ent *it = ie_ent_at(L[k]->slot[i]);
                if (it == NULL || it->is_dead || it->kind != IE_ITEM || it->stack_item != 368) continue;
                double dx = it->e.pos_x - sp->e.pos_x, dy = it->e.pos_y - sp->e.pos_y, dz = it->e.pos_z - sp->e.pos_z;
                double d2 = dx * dx + dy * dy + dz * dz;
                if (d2 > NETH_RL_CENSUS_PEARL_R * NETH_RL_CENSUS_PEARL_R) continue;
                if (c->pearl_n++ == 0 || d2 < pb)
                    pb = d2, c->pearl_x = (float)it->e.pos_x, c->pearl_y = (float)it->e.pos_y,
                    c->pearl_z = (float)it->e.pos_z;
            }
    }
    float hd2[NETH_RL_CENSUS];
    double fd2 = 1e30;
    c->food_n = c->food_sky = 0;
    for (int i = 0; i < an->n; ++i)
    {
        const struct an_ent *en = an_ent_at(an->slot[i]);
        if (en == NULL || !en->used || !en->is_living || en->ghost) continue;
        const struct living *l = lv_get(en->livh);
        if (l == NULL || l->is_dead || l->health <= 0.0f) continue;
        double mx = l->e.pos_x, my = l->e.pos_y + 0.5 * l->e.height, mz = l->e.pos_z;
        double dx = mx - ex, dy = my - ey, dz = mz - ez, h2 = dx * dx + dz * dz;
        /* lane/chain (food_oracle): the nearest food animal, one under the sky before any other */
        if ((l->kind == AK_PIG || l->kind == AK_COW || l->kind == AK_MOOSHROOM || l->kind == AK_CHICKEN) &&
            h2 <= NETH_RL_CENSUS_R * NETH_RL_CENSUS_R)
        {
            int sky = census_sky(w, l->e.pos_x, l->e.pos_y, l->e.pos_z);
            double k2 = h2 + 16.0 * dy * dy + (sky ? 0.0 : 1e12);   /* (a hill's climb costs more than its run) */
            c->food_n += 1;
            if (k2 < fd2)
            {
                fd2 = k2;
                c->food_x = (float)l->e.pos_x, c->food_y = (float)l->e.pos_y, c->food_z = (float)l->e.pos_z;
                c->food_sky = sky;
            }
        }
        int hk = l->kind == HK_SKELETON ? 0 : l->kind == HK_ZOMBIE ? 1 : l->kind == HK_SPIDER ? 2
                 : l->kind == HK_CREEPER ? 3 : -1;
        if ((hk >= 0 || l->kind == HK_ENDERMAN) && h2 + dy * dy <= 256.0)
        {
            int nk = hk >= 0 ? hk : 4;     /* lane/proom: the nearest of each kind (a trace's) */
            float d = (float)sqrt(h2 + dy * dy);
            if (c->hnear[nk] < 0.0f || d < c->hnear[nk]) c->hnear[nk] = d;
        }
        if (hk >= 0 && h2 + dy * dy <= 256.0)
        {
            c->hostile[hk] += 1;
            struct rt_mop m;
            if (hk == 0 && ss->client_world &&
                !(raytrace_blocks(ss->client_world, ex, ey, ez, mx, my, mz, 0, 1, 0, &m) && m.type >= 0))
                c->skel_los += 1;
        }
        if (l->kind != HK_ENDERMAN) continue;
        c->alive += 1;
        if (h2 > NETH_RL_CENSUS_R * NETH_RL_CENSUS_R) continue;
        int k = c->n < NETH_RL_CENSUS ? c->n : NETH_RL_CENSUS;
        if (k == NETH_RL_CENSUS && h2 >= hd2[NETH_RL_CENSUS - 1]) continue;
        if (k == NETH_RL_CENSUS) --k;
        while (k > 0 && hd2[k - 1] > h2)
        {
            hd2[k] = hd2[k - 1];
            c->e[k] = c->e[k - 1];
            --k;
        }
        hd2[k] = (float)h2;
        memset(&c->e[k], 0, sizeof c->e[k]);
        c->e[k].dx = (float)dx, c->e[k].dy = (float)dy, c->e[k].dz = (float)dz;
        c->e[k].sky = (int8_t)census_sky(w, l->e.pos_x, l->e.pos_y, l->e.pos_z);
        const struct client_entity *ce = cw ? clientworld_get_entity(cw, l->entity_id) : NULL;
        if (ce && !ce->is_dead)
        {
            double d = sqrt(h2 + dy * dy);
            struct rt_mop m;
            c->e[k].tracked = 1;
            c->e[k].angry = (int8_t)((ce->kindw >> 16) & 1);
            c->e[k].cone = d > 1e-6 && (dx * lx + dy * ly + dz * lz) / d >= cmin;
            c->e[k].clear = ss->client_world &&
                            !(raytrace_blocks(ss->client_world, ex, ey, ez, mx, my, mz, 0, 1, 0, &m) && m.type >= 0);
            if (ss->client_world && h2 + dy * dy <= NETH_RL_FAR_R * NETH_RL_FAR_R)
            {
                c->e[k].clear2 = !(raytrace_blocks(ss->client_world, ex, ey + 2.0, ez, mx, my, mz, 0, 1, 0, &m) &&
                                   m.type >= 0);
                c->e[k].clear4 = !(raytrace_blocks(ss->client_world, ex, ey + 4.0, ez, mx, my, mz, 0, 1, 0, &m) &&
                                   m.type >= 0);
                /* lane/pearlsee: higher eyes, and from the high ground (census_high) */
                c->e[k].clear8 = !(raytrace_blocks(ss->client_world, ex, ey + 8.0, ez, mx, my, mz, 0, 1, 0, &m) &&
                                   m.type >= 0);
                c->e[k].clear12 = !(raytrace_blocks(ss->client_world, ex, ey + 12.0, ez, mx, my, mz, 0, 1, 0, &m) &&
                                    m.type >= 0);
                c->e[k].clear16 = !(raytrace_blocks(ss->client_world, ex, ey + 16.0, ez, mx, my, mz, 0, 1, 0, &m) &&
                                    m.type >= 0);
                if (c->hi_d >= 0)
                {
                    double hx = c->hi_x, hy = c->hi_y + 1.62, hz = c->hi_z;
                    c->e[k].hi = !(raytrace_blocks(ss->client_world, hx, hy, hz, mx, my, mz, 0, 1, 0, &m) && m.type >= 0);
                    c->e[k].hi10 = !(raytrace_blocks(ss->client_world, hx, hy + 10.0, hz, mx, my, mz, 0, 1, 0, &m) &&
                                     m.type >= 0);
                }
            }
        }
        if (c->n < NETH_RL_CENSUS) ++c->n;
    }
}

/* the worker, after each tick of env id (pool.h tick_hook): the engine's
 * state read, nothing written but the env's own neth_rl_state */
static void state_hook(void *ctx, int id, struct session *ss, int64_t t, int last)
{
    struct nwrl *h = ctx;
    struct neth_rl_env *ne = &h->e[id];
    struct neth_rl_state *st = &ne->st;
    if (ne->rows) rlgui_row(ne->rows, ss, t, ne->cw_from);
    const struct server_player *sp = &ss->sp;
    struct client_player *cp = &ss->cp;
    for (int i = 0; i < 36; ++i)
    {
        const struct surv_stack *s = &sp->sv.inv[i];
        st->inv[i].item = (int16_t)(s->count > 0 ? s->item : -1);
        st->inv[i].count = (int16_t)(s->count > 0 ? s->count : 0);
        st->inv[i].damage = (int16_t)s->damage;
    }
    for (int i = 0; i < 4; ++i)   /* lane/rods2: the armour slots, books only */
    {
        const struct surv_stack *s = &sp->sv.inv[36 + i];
        st->armor[i].item = (int16_t)(s->count > 0 ? s->item : -1);
        st->armor[i].count = (int16_t)(s->count > 0 ? s->count : 0);
        st->armor[i].damage = (int16_t)s->damage;
    }
    st->current = sp->sv.current_item;
    st->window = sp->window_id;
    st->gui_x = sp->gui_x;
    st->gui_y = sp->gui_y;
    st->gui_z = sp->gui_z;
    const struct surv_mouseover_pos *mo = &neth_env->survival.mo.cur;
    struct world *cw = ss->client_world;
    st->mo_hit = mo->num != 0 && !mo->is_miss && mo->entity_id == 0;
    st->mo_x = mo->x;
    st->mo_y = mo->y;
    st->mo_z = mo->z;
    st->mo_side = mo->side;
    st->mo_block = st->mo_hit && cw ? world_get_block(cw, mo->x, mo->y, mo->z) & 4095 : 0;
    st->mo_meta = st->mo_hit && cw ? world_get_meta(cw, mo->x, mo->y, mo->z) : 0;
    st->lq_hit = 0;
    if (cw)
    {
        struct iu_player ip;
        memset(&ip, 0, sizeof ip);
        ip.pos_x = cp->e.pos_x;
        ip.pos_y = cp->e.pos_y + (double)(cp->e.y_offset - 1.62F) - 1.62;   /* the feet: the ray starts 1.62 over */
        ip.pos_z = cp->e.pos_z;
        ip.yaw = cp->rotation_yaw;
        ip.pitch = cp->rotation_pitch;
        int x, y, z, face;
        if (itemuse_look_liquid(cw, &ip, &x, &y, &z, &face))
        {
            st->lq_hit = 1;
            st->lq_x = x;
            st->lq_y = y;
            st->lq_z = z;
            st->lq_side = face;
            st->lq_block = world_get_block(cw, x, y, z) & 4095;
            st->lq_meta = world_get_meta(cw, x, y, z);
        }
    }
    st->eye_water = 0;
    if (cw)
    {
        int vx = (int)floor(cp->e.pos_x), vy = (int)floor(cp->e.pos_y), vz = (int)floor(cp->e.pos_z);
        int b = world_get_block(cw, vx, vy, vz) & 4095;
        st->eye_water = b == 8 || b == 9;
    }
    const struct surv_stats *ss_ = &sp->sv.stats;
    uint64_t ach = 0;
    for (int a = 0; a < ACH_COUNT && a < 64; ++a)
        if (ss_->v[a] > 0) ach |= 1ull << a;
    st->ach = ach;
    st->mined_logs = ss_->v[STAT_MINE_BLOCK + 17] + ss_->v[STAT_MINE_BLOCK + 162];
    st->mined_obsidian = ss_->v[STAT_MINE_BLOCK + 49];
    st->mob_kills = ss_->v[STAT_MOB_KILLS];
    /* lane/slim: the view scan is the step's, not every tick's: on a multi-tick step the macros read it once, at
     * the step's end, and the state's cheap parts (position, inventory, stats) are per tick either way */
    if (last) vis_scan(st, &ne->far, ss, cp);
    /* lane/pearlsee (3rd pass): the privileged reads (the census, the path ask) see only the loaded chunks: a read past
     * them generated the chunk on demand, which the Java client never did (a recorded pearl_oracle episode's entities
     * left Java's at t 15,960 and the pearl it picked up in the sim was never on Java's ground) */
    struct world *pw = ss->sr != NULL ? ss->sr->w[sr_dim_index(ss->sp.dimension)].st.w : NULL;
    int pw_ng = pw != NULL ? pw->no_generate : 0;
    if (pw != NULL) pw->no_generate = 1;
    if (last && ne->far.census.on) census_scan(ne, &ne->far.census, ss, cp, t);
    /* lane/pearlsee (3rd pass): a privileged option's path ask (walkto.h), answered once, from the feet */
    if (last && ne->far.wask.seq != ne->far.wpath.seq && pw != NULL)
        walkto_search(pw, ss->sp.e.pos_x, ss->sp.e.pos_y, ss->sp.e.pos_z, &ne->far.wask, &ne->far.wpath);
    if (pw != NULL) pw->no_generate = pw_ng;
    if (last && ne->far.depth_on == 1) depth_scan(&ne->far, ss, cp);   /* 2: on demand (neth_rl_far_depth_now) */
    if (last && (cp->dimension == 1 || ne->end.on)) end_scan(&ne->end, ss, cp);
    /* lane/pearl: the death screen's cause (stat.entityKilledBy.<name>, what the death message names) */
    _Static_assert(NETH_RL_EGGS == EGG_COUNT, "neth_rl_far's eggs");
    for (int k = 0; k < NETH_RL_EGGS; ++k) ne->far.killed_by[k] = ss_->v[STAT_ENTITY_KILLED_BY + k];
    ne->far.ender_kills = ss_->v[STAT_KILL_ENTITY + EGG_ENDERMAN];
    ne->far.armor_worn = surv_client_armor_value(cp);
    for (int k = 0; k < 4; ++k)
        ne->far.armor_item[k] = cp->sv.inv[36 + k].count > 0 ? cp->sv.inv[36 + k].item : 0;
    ne->far.blaze_kills = ss_->v[STAT_KILL_ENTITY + EGG_BLAZE];   /* lane/rod */
    st->crafted = ss_->crafted;     /* the craft stats' sum, kept by survival.c's writes */
    /* the chests of the player's dimension */
    struct world *w = ss->sr ? ss->sr->w[sr_dim_index(sp->dimension)].st.w : NULL;
    int window_changed = sp->window_id != ne->last_window;
    ne->last_window = sp->window_id;
    if (w && (ne->last_scan < 0 || t - ne->last_scan >= h->chest_every || window_changed))
    {
        chest_scan(h, st, w, sp->dimension, sp->e.pos_x, sp->e.pos_z);
        ne->last_scan = t;
    }
    /* an open chest window marks its chest (and a double chest's pair) opened */
    if (sp->window_id != 0 && sp->gui_te != NULL)
        for (int k = 0; k < st->nchest; ++k)
        {
            int at = st->chest[k].x == sp->gui_x && st->chest[k].y == sp->gui_y && st->chest[k].z == sp->gui_z;
            int pair = sp->gui_has_pair && st->chest[k].x == sp->gui_pair_x && st->chest[k].y == sp->gui_y &&
                       st->chest[k].z == sp->gui_pair_z;
            if ((at || pair) && st->chest[k].dim == sp->dimension && !st->chest[k].opened)
            {
                st->chest[k].opened = 1;
                ++st->chests_opened;
            }
        }
    st->chests_known = st->nchest;
    st->nearest = -1;
    st->nearest_dist = -1;
    double ex = sp->e.pos_x, ey = sp->e.pos_y + 1.62, ez = sp->e.pos_z;
    for (int k = 0; k < st->nchest; ++k)
    {
        if (st->chest[k].opened || st->chest[k].dim != sp->dimension) continue;
        double dx = st->chest[k].x + 0.5 - ex, dy = st->chest[k].y + 0.5 - ey, dz = st->chest[k].z + 0.5 - ez;
        float d = (float)sqrt(dx * dx + dy * dy + dz * dz);
        if (st->nearest < 0 || d < st->nearest_dist)
        {
            st->nearest = k;
            st->nearest_dist = d;
        }
    }
}

/* the worker, at the end of env id's step (pool.h step_hook): the semantic
 * camera's cast into the env's row of the buffer */
static void sem_hook(void *ctx, int id, struct session *ss, struct pool_result *r)
{
    struct nwrl *h = ctx;
    /* the third seen block nether brick in the Nether, gravel elsewhere (lane/flint: flint comes from gravel; the
     * vector env's seek takes it while flint and steel is wanted) */
    static const int16_t NETHER_IDS[RC_SEEN] = {15, 56, 112, 17}, OVER_IDS[RC_SEEN] = {15, 56, 13, 17};
    const int16_t *SEEN_IDS = r->priv.dim == -1 ? NETHER_IDS : OVER_IDS;
    if (h->camera) semcam_cast(&h->sem, ss, h->sem_buf + (size_t)id * (size_t)h->sem.w * (size_t)h->sem.h);
    if (h->rcd && !h->rc_host[id])
    {
        struct rcd_head *hd = rcdev_head(h->rcd, id);
        if (raycast_head(&h->rc, ss, SEEN_IDS, hd) == 0 &&
            rcmirror_sync(rcdev_mirror(h->rcd), id, pool_epoch(pipe_pool(h->p), id), ss, hd))
            __atomic_add_fetch(&h->rcd_refused, 1, __ATOMIC_RELAXED);
    }
    else if (h->rc_on)
    {
        struct raycast_seen seen;
        struct neth_rl_state *st = &h->e[id].st;
        /* lane/wood (seen_log2 on): the seen log either log block (the host cast's; the device's keeps 17) */
        static const int16_t OVER_LOGS[RC_SEEN] = {15, 56, 13, RC_SEEN_LOGS};
        if (r->priv.dim != -1 && h->vec && neth_vec_seen_log2(h->vec)) SEEN_IDS = OVER_LOGS;
        raycast_seen_keep(&h->rc, ss, (h->rcd ? h->rc_hbuf : h->rc_buf) + (size_t)id * (size_t)h->rc.w * (size_t)h->rc.h * 3,
                          SEEN_IDS, &seen, h->rc_keep, id, pool_epoch(pipe_pool(h->p), id));
        for (int q = 0; q < NETH_RL_SEEN; ++q)
        {
            st->seen_n[q] = seen.n[q];
            st->seen[q].x = seen.x[q], st->seen[q].y = seen.y[q], st->seen[q].z = seen.z[q];
            st->seen[q].d = seen.d[q];
        }
    }
    /* neth_rl_scan: the privileged world scan (rlscan.h) */
    if (h->scan) rlscan_run(h->scan, ss, &h->scan_buf[id]);
    /* neth_rl_layer_auto: the open screen's layer with the raw pointer, drawn here on the worker */
    if (h->layer_flag)
    {
        int32_t o[8];
        neth_rl_raw_state(h, &id, 1, o);
        size_t px = (size_t)h->w * (size_t)h->h * 3;
        h->layer_flag[id] = o[7] ? rlgui_layer(ss, h->w, h->h, h->layer_mul + (size_t)id * px,
                                               h->layer_add + (size_t)id * px, o[0], o[1]) : 0;
    }
    /* vecenv.h: the vector env's books after the step */
    if (h->vec) neth_vec_step_hook(h->vec, id, r, &h->e[id].st);
}

/* the worker, before each tick of env id (pool.h act_hook): the action
 * compilers, lane/moveact's for moving, mining and looking (moveact.h), then
 * lane/guiact's for the screens (rlact.h) on the act it leaves; the act is
 * written to out (1) when either changed it, else src runs as it is (0) */
static int act_hook(void *ctx, int id, struct session *ss, int k, const struct pool_act *src, int hold_only,
                    struct pool_act *out)
{
    struct nwrl *h = ctx;
    if (h->vec) return neth_vec_act_hook(h->vec, id, k, &h->e[id].st, out);
    if (h->raw) return rlraw_hook(h->e[id].raw, ss, k, src, hold_only, out);
    int wrote = moveact_hook(h->mv, id, ss, k, src, hold_only, out);
    if (!rlact_on(&h->actconf)) return wrote;
    if (!wrote)
    {
        if (hold_only)
        {
            /* Act.holdOnly: the holds stay, nothing else repeats (and the
             * compiler's op, if it gives one) */
            memset(out, 0, sizeof *out);
            out->kind = PA_AGENT;
            out->hold = src->hold;
            out->look_mode = PL_NONE;
            out->hotbar = -1;
        }
        else
            *out = *src;
    }
    rlact_tick(&h->actconf, &h->e[id].act, ss, !hold_only, out);
    return wrote || !hold_only || out->nops > 0;
}

/* the worker, after each tick of env id (pool.h act_done): the raw form's log */
static void act_done(void *ctx, int id, int64_t t, const struct act *a)
{
    struct nwrl *h = ctx;
    if (h->raw) rlraw_done(h->e[id].raw, t, a);
}

/* ------------------------------------------------------------------ API */

struct nwrl *neth_rl_make(const struct neth_rl_opts *o, char *err, int en)
{
    return neth_rl_make_ex(o, 0, err, en);
}

struct nwrl *neth_rl_make_ex(const struct neth_rl_opts *o, int flags, char *err, int en)
{
    if (o == NULL || o->config == NULL || o->n <= 0) { seterr(err, en, "neth_rl_make: a config and n > 0 are needed", NULL); return NULL; }
    struct pipe_config cfg = {0};
    struct wconf wc;
    char e[512];
    if (pipe_config_file(&cfg, o->config, &wc, e, sizeof e)) { seterr(err, en, "config", e); return NULL; }
    if ((flags & NETH_RL_SKIP_AMBIENT) || ((flags >> 17) & 255))
    {
        if (fidelity_vanilla(&cfg.pool.fid) && fidelity_from_conf(&cfg.pool.fid, &wc, e, sizeof e))
        {
            seterr(err, en, "fidelity", e);
            return NULL;
        }
        if (flags & NETH_RL_SKIP_AMBIENT) cfg.pool.fid.bits |= FID_SKIP_AMBIENT;
        if ((flags >> 17) & 255)
        {
            cfg.pool.fid.distant_tick_radius = (flags >> 17) & 255;
            cfg.pool.fid.bits |= FID_DISTANT_TICKS;
        }
    }
    char start[PATH_MAX];
    if (o->start) snprintf(start, sizeof start, "%s", o->start);
    else wconf_start_path(&wc, start, sizeof start);
    /* playview.so beside this library */
    char so[PATH_MAX];
    if (o->view_so) snprintf(so, sizeof so, "%s", o->view_so);
    else
    {
        Dl_info di;
        if (!dladdr((void *)neth_rl_make, &di) || di.dli_fname == NULL) { seterr(err, en, "cannot find libneth.so's directory", NULL); return NULL; }
        snprintf(so, sizeof so, "%s", di.dli_fname);
        char *slash = strrchr(so, '/');
        snprintf(slash ? slash + 1 : so, sizeof so - (size_t)(slash ? slash + 1 - so : 0), "playview.so");
    }
    struct nwrl *h = calloc(1, sizeof *h);
    h->config = strdup(o->config);
    cfg.pool.config = h->config;
    h->n = o->n;
    h->chest_radius = o->chest_radius > 0 ? o->chest_radius : 8;
    h->chest_every = o->chest_every > 0 ? o->chest_every : 10;
    h->e = calloc((size_t)o->n, sizeof *h->e);
    h->acts = calloc((size_t)o->n, sizeof *h->acts);
    h->res = calloc((size_t)o->n, sizeof *h->res);
    /* the config's sim.actions (rlact.h; neth_rl_actions changes them) */
    h->actconf.click_interval = wconf_int(&wc, WC_CLICK_INTERVAL);
    h->actconf.mask_close_grid = wconf_on(&wc, WC_MASK_CLOSE_GRID);
    h->actconf.mask_noop_clicks = wconf_on(&wc, WC_MASK_NOOP_CLICKS);
    h->actconf.move_stack = wconf_on(&wc, WC_MOVE_STACK);
    h->actconf.item_select = wconf_on(&wc, WC_ITEM_SELECT);
    h->actconf.recipes = wconf_on(&wc, WC_RECIPES);
    for (int i = 0; i < o->n; ++i)
    {
        h->e[i].last_scan = -1;
        rlact_reset(&h->e[i].act);
    }
    cfg.pool.n = o->n;
    if (o->region_spill != NULL) cfg.pool.region_spill = o->region_spill;   /* NULL: the config's (pipe_config_file) */
    if (o->threads > 0) cfg.pool.threads = o->threads;   /* 0: the config's performance.workers */
    cfg.pool.privileged = 1;
    cfg.pool.no_counters = 1;
    cfg.pool.tick_hook = state_hook;
    if (flags & NETH_RL_DEV) rlgui_dev(&cfg.pool);
    if (flags & NETH_RL_SAVE)
    {
        int k = flags >> 8 & 63;
        cfg.pool.save_slots = k <= 0 ? 4 : k > 32 ? 32 : k;
    }
    if (flags & NETH_RL_ANYSEED) cfg.pool.any_seed = 1;
    cfg.pool.tick_hook_ctx = h;
    h->mv = moveact_new(o->n);
    cfg.pool.act_hook = act_hook;
    cfg.pool.act_done = act_done;
    cfg.pool.act_hook_ctx = h;
    /* client.camera: semantic runs no view and no renderer (the pool alone,
     * pipeline.h no_view); semantic and both cast after each step */
    wconf_semantic(&wc, &h->camera, &h->sem.w, &h->sem.h, &h->sem.range);
    h->sem.fp = wconf_cast_precision(&wc);
    if (h->camera) h->sem_buf = calloc((size_t)o->n * (size_t)h->sem.w * (size_t)h->sem.h, sizeof *h->sem_buf);
    /* the cast and the scan (neth_rl_scan may turn it on later) at each step's end */
    cfg.pool.step_hook = sem_hook;
    cfg.pool.step_hook_ctx = h;
    if (h->camera == 1) cfg.no_view = 1;
    if (wconf_raycast(&wc))
    {
        /* client.camera raycast: the cast alone at the observation's size, no
         * renderer; its textures from the checkout the config names (the
         * start's root) */
        char root[PATH_MAX];
        const char *sv = wconf_get(&wc, WC_START);
        size_t ls = strlen(start), lv = strlen(sv);
        if (!o->start && sv[0] != '/' && ls > lv + 1 && !strcmp(start + ls - lv, sv)) snprintf(root, sizeof root, "%.*s", (int)(ls - lv - 1), start);
        else snprintf(root, sizeof root, ".");
        if (raycast_load(root, e, sizeof e)) { seterr(err, en, "raycast", e); neth_rl_free(h); return NULL; }
        h->rc_on = 1;
        h->rc.w = cfg.pool.obs.w > 0 ? cfg.pool.obs.w : 128;
        h->rc.h = cfg.pool.obs.h > 0 ? cfg.pool.obs.h : 128;
        h->rc.ch = cfg.pool.obs.ch > 0 ? cfg.pool.obs.ch : 3;
        h->rc.rd = cfg.pool.obs.rd;
        h->rc.gamma = cfg.pool.obs.gamma;
        h->rc.fancy = !strcmp(wconf_get(&wc, WC_GRAPHICS), "fancy");
        h->rc.fp = wconf_cast_precision(&wc);   /* client.cast_precision: the cast's instance, host and device */
        h->rc.range = wconf_cast_radius(&wc);   /* client.cast_radius: opt-in far-plane cap (0: rd * 16, vanilla) */
        if ((h->rc_keep = raycast_keep_new(o->n)) == NULL) { seterr(err, en, "raycast", "out of memory"); neth_rl_free(h); return NULL; }
        if (wconf_raycast_device(&wc))
        {
            /* the rings reach the cast's range: raycast.c's render distance (the config's, under 2 taken as 4; the
             * session's when 0, here up to 8) */
            struct rcd_tabsrc tabs;
            int rd = h->rc.rd > 0 ? (h->rc.rd < 2 ? 4 : h->rc.rd) : 8;
            int rc_range = (h->rc.range > 0 && h->rc.range < rd * 16) ? h->rc.range : rd * 16;
            if (raycast_tables(&tabs)) { seterr(err, en, "engine.raycast device", "the tables did not load"); neth_rl_free(h); return NULL; }
            h->rcd = rcdev_new(o->n, h->rc.w, h->rc.h, h->rc.ch, rc_range, o->device, &tabs, e, sizeof e);
            if (h->rcd == NULL) { seterr(err, en, "engine.raycast device", e); neth_rl_free(h); return NULL; }
            h->rc_host_groups = wconf_raycast_host_groups(&wc);
            h->rc_host = calloc((size_t)o->n, 1);
            if (h->rc_host == NULL) { seterr(err, en, "engine.raycast device", "out of memory"); neth_rl_free(h); return NULL; }
        }
        else
            h->rc_buf = calloc((size_t)o->n * (size_t)h->rc.w * (size_t)h->rc.h * (size_t)h->rc.ch, 1);
        cfg.no_view = 1;
    }
    /* no view (the cast, the semantic camera): the pipeline's layout (performance.layout pipe, the default) and doms
     * put the envs by cache domain, env i on L3 i mod their number (pool.h env_dom NULL), any worker of that L3 steps
     * it and an idle one takes another L3's waiting step: a group of consecutive ids spans every L3, and an env's
     * lines stay in the L3 that steps it. The pipeline's own domain layout follows its render streams, so with no
     * view it put whole groups on one L3. On the GPU host (eight L3s of 8 cores) at 512 envs in 4 groups, 64 workers:
     * 390k env-steps/s against 259k with any worker anywhere, the workers' step 0.149 ms against 0.237; without the
     * steals 22% fewer (170k, 168k against 223k, 212k). Each worker on one core, the L3s in turn (spread; more
     * workers than cores go to the second threads the same way): against any CPU of its L3, eight pairs beside
     * another project's load, ratios 0.97 to 1.38, median 1.05, the runs closer together (lane/placement) */
    if (cfg.no_view && (cfg.pool_layout == PIPE_LAYOUT_PIPE || cfg.pool_layout == PIPE_LAYOUT_DOMS) && pool_domains() > 1)
    {
        /* lane/xhigh-patch: said, not silent (the old workstation-scalable.yaml's pin 0 and place none ran as pin 2, domain; configs/sim/blackwell.yaml names pin 2 and place domain) */
        if (cfg.pool.pin != 2 || cfg.pool.place == PP_NONE)
            fprintf(stderr, "neth_rl: performance.layout %s with no view on %d L3s: pin 2 (the config's %d), place %s "
                            "(the config's %s), each env's steps on its L3's workers\n",
                    cfg.pool_layout == PIPE_LAYOUT_DOMS ? "doms" : "pipe", pool_domains(), cfg.pool.pin,
                    pool_place_name(cfg.pool.place != PP_NONE ? cfg.pool.place : PP_DOMAIN),
                    pool_place_name(cfg.pool.place));
        cfg.pool_layout = PIPE_LAYOUT_GIVEN;
        cfg.pool.pin = 2;
        /* each worker on any CPU of its L3 (domain), or the config's performance.place when it names one: on the dev
         * host (two CCDs, the 3D V-Cache on one) the trainer at 512x4 ran 357,938 to 364,854 env-steps/s with
         * domain against 322,018 to 335,263 with spread and 284,575 to 293,295 with layout any, and at 128x4
         * (bound by the main thread) spread lost to any while domain did not (lane/cpuhot) */
        cfg.pool.place = cfg.pool.place != PP_NONE ? cfg.pool.place : PP_DOMAIN;
        cfg.pool.steal = 1;
        cfg.pool.env_dom = NULL;
        /* performance.workers 0: one a physical core, at most the envs (the pipeline's default without a view is every
         * CPU: 128 on the GPU host, 324k to 351k env-steps/s). With the host to itself 96 ran 6 to 13% faster than 64
         * (438k to 446k against 387k to 390k); with another project's processes on about 30 of its 128 threads, 64 ran
         * 4 to 6% faster than 96 (311k, 289k, 260k against 286k, 284k, 249k): a host shared like that gets the cores,
         * performance.workers sets more */
        if (cfg.pool.threads <= 0) cfg.pool.threads = pool_cores() < o->n ? pool_cores() : o->n;
    }
    /* 0: the config's performance section (itself 0: the pipeline's) */
    if (o->streams > 0) cfg.par.streams = o->streams;
    if (o->batch > 0) cfg.par.batch = o->batch;
    if (o->group > 0) cfg.par.group = o->group;
    if (o->depth > 0) cfg.par.depth = o->depth;
    if (o->device_mesh >= 0) cfg.device_mesh = o->device_mesh;
    cfg.device = o->device;
    cfg.view_so = so;
    h->p = pipe_make(&cfg, e, sizeof e);
    if (h->p == NULL) { seterr(err, en, "pipe_make", e); neth_rl_free(h); return NULL; }
    h->w = cfg.pool.obs.w > 0 ? cfg.pool.obs.w : 128;
    h->h = cfg.pool.obs.h > 0 ? cfg.pool.obs.h : 128;
    h->ticks = cfg.pool.obs.ticks > 0 ? cfg.pool.obs.ticks : 4;
    h->device = o->device;
    rlmove_init(h->mv, h->w, h->h, &wc);
    if (neth_rl_start_load(h, start, err, en) != 0) { neth_rl_free(h); return NULL; }
    int *ids = malloc((size_t)o->n * sizeof *ids);
    for (int i = 0; i < o->n; ++i) ids[i] = i;
    int rc = neth_rl_reset(h, ids, o->n, 0, err, en);
    free(ids);
    if (rc) { neth_rl_free(h); return NULL; }
    return h;
}

void neth_rl_free(struct nwrl *h)
{
    if (h == NULL) return;
    if (h->p) pipe_free(h->p);
    moveact_free(h->mv);
    for (int i = 0; h->e && i < h->n; ++i)
    {
        if (h->e[i].raw && h->e[i].raw->log) fclose(h->e[i].raw->log);
        free(h->e[i].raw);
        if (h->e[i].rows) fclose(h->e[i].rows);
        if (h->e[i].act.log) fclose(h->e[i].act.log);
    }
    for (int i = 0; i < h->nstart; ++i)
        if (h->start[i])
        {
            pool_start_free(h->start[i]->s);
            free(h->start[i]);
        }
    free(h->config);
    free(h->e);
    free(h->acts);
    free(h->res);
    free(h->sem_buf);
    free(h->rc_buf);
    raycast_keep_free(h->rc_keep);
    free(h->rc_host);
    rcdev_free(h->rcd);
    free(h->scan);
    free(h->scan_buf);
    free(h->layer_mul);
    free(h->layer_add);
    free(h->layer_flag);
    free(h);
}

int neth_rl_size(const struct nwrl *h) { return h->n; }

const struct neth_rl_far *neth_rl_far(const struct nwrl *h, int id)
{
    return id >= 0 && id < h->n ? &h->e[id].far : NULL;
}

void neth_rl_walkto_ask(struct nwrl *h, int id, int x, int y, int z, float range)
{
    if (id < 0 || id >= h->n) return;
    struct walkto_ask *q = &h->e[id].far.wask;
    q->seq += 1, q->x = x, q->y = y, q->z = z, q->range = range, q->rel = 0;
}

void neth_rl_walkto_ask_rel(struct nwrl *h, int id, int dx, int dy, int dz, float range)
{
    if (id < 0 || id >= h->n) return;
    neth_rl_walkto_ask(h, id, dx, dy, dz, range);
    h->e[id].far.wask.rel = 1;
}

void neth_rl_far_census(struct nwrl *h, int on)
{
    for (int i = 0; i < h->n; ++i) h->e[i].far.census.on = on;
}

void neth_rl_far_depth(struct nwrl *h, int on)
{
    for (int i = 0; i < h->n; ++i) h->e[i].far.depth_on = on;
}

/* lane/macroperf: env id's depth rows now, from its step hook (pool.h step_hook: the client world and the player as
 * the step's last tick hook left them, so the rows are the ones depth_on 1 writes there) */
void neth_rl_far_depth_now(struct nwrl *h, int id)
{
    if (id < 0 || id >= h->n) return;
    struct session *ss = pool_session(pipe_pool(h->p), id);
    if (ss == NULL) return;
    struct env *saved = neth_env;
    neth_env = pool_env(pipe_pool(h->p), id);
    depth_scan(&h->e[id].far, ss, &ss->cp);
    neth_env = saved;
}

/* lane/pnight (PRIVILEGED, a trace's only): the server player's combat tracker as the death message reads it (endview.c
 * end_scan's books, any dimension) */
int neth_rl_combat(const struct nwrl *h, int id, struct neth_rl_combat_now *out)
{
    memset(out, 0, sizeof *out);
    out->dt = out->name = out->fall_dt = -1;
    if (id < 0 || id >= h->n) return -1;
    struct session *ss = pool_session(pipe_pool(h->p), id);
    if (ss == NULL) return -1;
    const struct surv_combat *c = &ss->sp.sv.combat;
    out->n = c->n;
    out->age = ss->sp.sv.ticks_existed - c->last_tick;
    /* the death's own tick clears the entries (combat_reset: n 0) and keeps the last entry and the fall's, which the
     * death message read: a dead player's are its killing hit's */
    int dead = ss->sp.sv.health <= 0.0F;
    if (c->n > 0 || dead) out->dt = c->last.dtype, out->name = c->last.name, out->damage = c->last.damage;
    if (c->has_fall && (c->n > 0 || dead)) out->fall_dt = c->fall_entry.dtype, out->fall_len = c->fall_len;
    return 0;
}

int neth_rl_damage_stats(const struct nwrl *h, int id, int32_t *dealt, int32_t *taken)
{
    *dealt = *taken = -1;
    if (id < 0 || id >= h->n) return -1;
    struct session *ss = pool_session(pipe_pool(h->p), id);
    if (ss == NULL) return -1;
    *dealt = (int32_t)ss->sp.sv.stats.v[STAT_DAMAGE_DEALT];
    *taken = (int32_t)ss->sp.sv.stats.v[STAT_DAMAGE_TAKEN];
    return 0;
}

int neth_rl_tower(struct nwrl *h, int id, struct neth_rl_tower *out)
{
    static const double TH[NETH_RL_TOWER_NH] = {0.0, 5.0, 10.0, 20.0};
    memset(out, 0, sizeof *out);
    if (id < 0 || id >= h->n) return -1;
    struct session *ss = pool_session(pipe_pool(h->p), id);
    if (ss == NULL || ss->sr == NULL || ss->client_world == NULL || ss->sp.dimension != 0) return -1;
    struct client_player *cp = &ss->cp;
    int di = sr_dim_index(ss->sp.dimension);
    struct world *w = ss->sr->w[di].st.w;
    struct an_world *an = &ss->sr->dims[di].anw;
    if (w == NULL) return -1;
    struct env *saved = neth_env;
    neth_env = pool_env(pipe_pool(h->p), id);     /* the livings are the env's arena's (lv_get, an_ent_at) */
    int ng = w->no_generate;
    w->no_generate = 1;     /* the census's rule: only the loaded chunks (a read past them would generate one) */
    double ex = cp->e.pos_x, ey = cp->e.pos_y + (double)(cp->e.y_offset - 1.62F), ez = cp->e.pos_z;
    for (int i = 0; i < an->n; ++i)
    {
        const struct an_ent *en = an_ent_at(an->slot[i]);
        if (en == NULL || !en->used || !en->is_living || en->ghost) continue;
        const struct living *l = lv_get(en->livh);
        if (l == NULL || l->is_dead || l->health <= 0.0f || l->kind != HK_ENDERMAN) continue;
        double mx = l->e.pos_x, my = l->e.pos_y + 0.5 * l->e.height, mz = l->e.pos_z;
        double dx = mx - ex, dz = mz - ez, h2 = dx * dx + dz * dz;
        if (h2 > 128.0 * 128.0 || !census_sky(w, l->e.pos_x, l->e.pos_y, l->e.pos_z)) continue;
        for (int b = h2 <= 64.0 * 64.0 ? 0 : 1; b < 2; ++b)
        {
            out->n[b] += 1;
            for (int j = 0; j < NETH_RL_TOWER_NH; ++j)
            {
                struct rt_mop m;
                double hy = ey + TH[j], dy = my - hy;
                if (raytrace_blocks(ss->client_world, ex, hy, ez, mx, my, mz, 0, 1, 0, &m) && m.type >= 0) continue;
                out->clear[b][j] += 1;
                out->reach[b][j] += h2 + dy * dy <= NETH_RL_TOWER_R * NETH_RL_TOWER_R;
            }
        }
    }
    w->no_generate = ng;
    neth_env = saved;
    return 0;
}

/* lane/pground (item A): World.updateEntityWithOptionalForce's test (ghasts.c gh_chunks_exist's): every chunk within
 * 32 blocks of the feet loaded */
static int gr_ticks(struct world *w, double px, double pz)
{
    int x = (int)floor(px), z = (int)floor(pz);
    for (int cx = (x - 32) >> 4; cx <= (x + 32) >> 4; ++cx)
        for (int cz = (z - 32) >> 4; cz <= (z + 32) >> 4; ++cz)
            if (!world_chunk_loaded(w, cx, cz)) return 0;
    return 1;
}
static int gr_monster(int k)
{
    return k == HK_ZOMBIE || k == HK_SKELETON || k == HK_CREEPER || k == HK_SPIDER || k == HK_CAVE_SPIDER ||
           k == HK_ENDERMAN || k == HK_WITCH || k == SK_SLIME || k == HK_SILVERFISH;
}

int neth_rl_ground(struct nwrl *h, int id, int cells, struct neth_rl_ground *out)
{
    memset(out, 0, sizeof *out);
    out->biome = -1;
    if (id < 0 || id >= h->n) return -1;
    struct session *ss = pool_session(pipe_pool(h->p), id);
    if (ss == NULL || ss->sr == NULL || ss->client_world == NULL || ss->sp.dimension != 0) return -1;
    struct client_player *cp = &ss->cp;
    const struct server_player *sp = &ss->sp;
    int di = sr_dim_index(sp->dimension);
    struct world *w = ss->sr->w[di].st.w;
    struct an_world *an = &ss->sr->dims[di].anw;
    if (w == NULL) return -1;
    struct env *saved = neth_env;
    neth_env = pool_env(pipe_pool(h->p), id);     /* the livings are the env's arena's (lv_get, an_ent_at) */
    int ng = w->no_generate;
    w->no_generate = 1;     /* the census's rule: only the loaded chunks */
    double fx = sp->e.pos_x, fy = sp->e.pos_y, fz = sp->e.pos_z;
    int px = (int)floor(fx), pz = (int)floor(fz);
    out->biome = world_get_biome(w, px, pz);
    out->y = (float)fy;
    out->pl_sky = census_sky(w, fx, fy, fz);
    if (cells & 2)
    {
        /* the eye's cell (the server player's feet + 1.62) and the ball round it on the client world */
        int ex0 = px, ey0 = (int)floor(fy + 1.62), ez0 = pz, R = NETH_RL_GROUND_AR;
        for (int dx = -R; dx <= R; ++dx)
            for (int dz = -R; dz <= R; ++dz)
            {
                if (dx * dx + dz * dz > R * R) continue;
                if (world_chunk(ss->client_world, (ex0 + dx) >> 4, (ez0 + dz) >> 4) == NULL) continue;
                for (int dy = -R; dy <= R; ++dy)
                {
                    int y = ey0 + dy;
                    if (dx * dx + dy * dy + dz * dz > R * R || y < 0 || y > 255) continue;
                    out->air8 += (world_get_block(ss->client_world, ex0 + dx, y, ez0 + dz) & 4095) == 0;
                }
            }
    }
    if (cells)
        for (int dz = -NETH_RL_GROUND_R; dz <= NETH_RL_GROUND_R; dz += 2)
            for (int dx = -NETH_RL_GROUND_R; dx <= NETH_RL_GROUND_R; dx += 2)
            {
                if (dx * dx + dz * dz > NETH_RL_GROUND_R * NETH_RL_GROUND_R) continue;
                int x = px + dx, z = pz + dz;
                if (world_chunk(w, x >> 4, z >> 4) == NULL) { ++out->cells[NETH_RL_GR_NONE]; continue; }
                int hv = world_get_height_value(w, x, z);
                int b = hv > 0 ? world_get_block(w, x, hv - 1, z) & 4095 : 0;
                int k = hv <= 0 ? NETH_RL_GR_NONE : (b == 8 || b == 9 || b == 79) ? NETH_RL_GR_WATER :
                        (b == 17 || b == 18 || b == 161 || b == 162) ? NETH_RL_GR_CANOPY : NETH_RL_GR_OPEN;
                ++out->cells[k];
            }
    int pcx = px >> 4, pcz = pz >> 4, view = ss->sr->view;
    if (cells & 4)
    {
        out->view = view;
        for (int dx = -NETH_RL_GROUND_LR; dx <= NETH_RL_GROUND_LR; ++dx)
            for (int dz = -NETH_RL_GROUND_LR; dz <= NETH_RL_GROUND_LR; ++dz)
            {
                if (!world_chunk_loaded(w, pcx + dx, pcz + dz)) continue;
                int c = abs(dx) > abs(dz) ? abs(dx) : abs(dz);
                ++out->nload, out->nload_w += c <= view;
                if (c > out->lmax) out->lmax = c;
            }
        const struct spawner *spn = &ss->sr->dims[di].spawner;
        for (int i = 0; i < spn->nrecs; ++i) out->capn += spn->rec_flags[i] == (uint8_t)((CT_MONSTER + 1) << 2 | 1);
        out->cap = CAP_MONSTER * spn->elig_n / 256;
        for (int i = 0; i < an->n; ++i)
        {
            const struct an_ent *en = an_ent_at(an->slot[i]);
            if (en == NULL || !en->used || !en->is_living || en->ghost) continue;
            const struct living *l = lv_get(en->livh);
            if (l == NULL || l->is_dead || !gr_monster(l->kind)) continue;
            int tk = gr_ticks(w, l->e.pos_x, l->e.pos_z);
            int lcx = (int)floor(l->e.pos_x) >> 4, lcz = (int)floor(l->e.pos_z) >> 4;
            int wt = abs(lcx - pcx) <= view && abs(lcz - pcz) <= view;
            ++out->mon[tk][wt];
            if (l->kind == HK_ENDERMAN) ++out->end[tk][wt];
        }
    }
    double ex = cp->e.pos_x, ey = cp->e.pos_y + (double)(cp->e.y_offset - 1.62F), ez = cp->e.pos_z;
    float d2s[NETH_RL_GROUND_E];
    const struct living *ls[NETH_RL_GROUND_E];
    for (int i = 0; i < an->n; ++i)
    {
        const struct an_ent *en = an_ent_at(an->slot[i]);
        if (en == NULL || !en->used || !en->is_living || en->ghost) continue;
        const struct living *l = lv_get(en->livh);
        if (l == NULL || l->kind != HK_ENDERMAN) continue;
        double dx = l->e.pos_x - fx, dy = l->e.pos_y - fy, dz = l->e.pos_z - fz, d2 = dx * dx + dy * dy + dz * dz;
        out->all += 1;
        int k = out->n < NETH_RL_GROUND_E ? out->n : NETH_RL_GROUND_E;
        if (k == NETH_RL_GROUND_E && d2 >= d2s[NETH_RL_GROUND_E - 1]) continue;
        if (k == NETH_RL_GROUND_E) --k;
        else out->n += 1;
        while (k > 0 && d2s[k - 1] > d2)
        {
            d2s[k] = d2s[k - 1], ls[k] = ls[k - 1], out->e[k] = out->e[k - 1];
            --k;
        }
        d2s[k] = (float)d2, ls[k] = l;
        out->e[k].id = l->entity_id, out->e[k].d = (float)sqrt(d2);
    }
    for (int k = 0; k < out->n; ++k)
    {
        const struct living *l = ls[k];
        double mx = l->e.pos_x, my = l->e.pos_y + 0.5 * l->e.height, mz = l->e.pos_z;
        struct rt_mop m;
        out->e[k].age = l->ticks_existed;
        out->e[k].dead = (int8_t)(l->is_dead || l->health <= 0.0f);
        out->e[k].sky = (int8_t)census_sky(w, l->e.pos_x, l->e.pos_y, l->e.pos_z);
        out->e[k].tick = (int8_t)gr_ticks(w, l->e.pos_x, l->e.pos_z);
        out->e[k].watch = (int8_t)(abs(((int)floor(l->e.pos_x) >> 4) - pcx) <= view &&
                                   abs(((int)floor(l->e.pos_z) >> 4) - pcz) <= view);
        out->e[k].clear = out->e[k].reach = 0;
        if (out->e[k].d > NETH_RL_GROUND_ER) continue;
        out->e[k].clear = (int8_t)!(raytrace_blocks(ss->client_world, ex, ey, ez, mx, my, mz, 0, 1, 0, &m) && m.type >= 0);
        double qx = mx - ex, qy = my - ey, qz = mz - ez;
        out->e[k].reach = (int8_t)(out->e[k].clear && qx * qx + qy * qy + qz * qz <= NETH_RL_TOWER_R * NETH_RL_TOWER_R);
    }
    w->no_generate = ng;
    neth_env = saved;
    return 0;
}

int neth_rl_floor(struct nwrl *h, int id, const int32_t b[6], int light, struct neth_rl_floor *out)
{
    memset(out, 0, sizeof *out);
    if (id < 0 || id >= h->n) return -1;
    struct session *ss = pool_session(pipe_pool(h->p), id);
    if (ss == NULL || ss->sr == NULL || ss->sp.dimension != 0) return -1;
    int di = sr_dim_index(ss->sp.dimension);
    struct world *w = ss->sr->w[di].st.w;
    struct an_world *an = &ss->sr->dims[di].anw;
    if (w == NULL) return -1;
    struct env *saved = neth_env;
    neth_env = pool_env(pipe_pool(h->p), id);     /* the livings are the env's arena's (lv_get, an_ent_at) */
    int ng = w->no_generate;
    w->no_generate = 1;     /* only the loaded chunks */
    const struct spawner *spn = &ss->sr->dims[di].spawner;
    for (int i = 0; i < spn->nrecs; ++i) out->capn += spn->rec_flags[i] == (uint8_t)((CT_MONSTER + 1) << 2 | 1);
    out->cap = CAP_MONSTER * spn->elig_n / 256;
    out->gate[0] = spn->mon_tests, out->gate[1] = spn->mon_open, out->gate[2] = spn->mon_walks, out->gate[3] = spn->mon_made;
    for (int i = 0; i < an->n; ++i)
    {
        const struct an_ent *en = an_ent_at(an->slot[i]);
        if (en == NULL || !en->used || !en->is_living || en->ghost) continue;
        const struct living *l = lv_get(en->livh);
        if (l == NULL || !gr_monster(l->kind)) continue;
        int x = (int)floor(l->e.pos_x), y = (int)floor(l->e.pos_y), z = (int)floor(l->e.pos_z);
        if (light & 2)
        {
            static const int K[NETH_RL_FLOOR_K] = {HK_ZOMBIE, HK_SKELETON, HK_CREEPER, HK_SPIDER, HK_CAVE_SPIDER,
                                                HK_ENDERMAN, HK_WITCH, SK_SLIME, HK_SILVERFISH};
            int tk = gr_ticks(w, l->e.pos_x, l->e.pos_z), k = 0;
            while (k < NETH_RL_FLOOR_K - 1 && K[k] != l->kind) ++k;
            out->kind[k][tk] += 1;
            int dcx = abs((x >> 4) - ((int)floor(ss->sp.e.pos_x) >> 4)), dcz = abs((z >> 4) - ((int)floor(ss->sp.e.pos_z) >> 4));
            int c = dcx > dcz ? dcx : dcz;
            out->band[c <= 4 ? 0 : c <= 8 ? 1 : c <= 16 ? 2 : 3][tk] += 1;
            out->mall += 1;
            if (out->mn < NETH_RL_FLOOR_M) out->mid[out->mn] = l->entity_id, out->mtick[out->mn] = (uint8_t)tk, out->mn += 1;
        }
        if (x < b[0] || x > b[3] || y < b[1] || y > b[4] || z < b[2] || z > b[5]) continue;
        out->all += 1;
        if (out->n >= NETH_RL_FLOOR) continue;
        out->e[out->n].id = l->entity_id, out->e[out->n].kind = l->kind;
        out->e[out->n].age = l->ticks_existed, out->e[out->n].dead = l->is_dead || l->health <= 0.0f;
        out->e[out->n].ender = l->kind == HK_ENDERMAN;
        out->n += 1;
    }
    if (light & 1)
        for (int x = b[0]; x <= b[3]; ++x)
            for (int z = b[2]; z <= b[5]; ++z)
            {
                if (!world_chunk_loaded(w, x >> 4, z >> 4)) continue;
                for (int y = b[1]; y <= b[4]; ++y)
                {
                    if ((world_get_block(w, x, y, z) & 4095) != 0) continue;
                    out->cells += 1;
                    int sk = world_get_light(w, LIGHT_SKY, x, y, z), bl = world_get_light(w, LIGHT_BLOCK, x, y, z);
                    if (sk > out->sky_max) out->sky_max = sk;
                    if (bl > out->blk_max) out->blk_max = bl;
                    out->spawnable += y > 0 && y < 255 && (world_get_block(w, x, y + 1, z) & 4095) == 0 &&
                                      place_solid_top_surface(w, x, y - 1, z);
                }
            }
    w->no_generate = ng;
    neth_env = saved;
    return 0;
}

/* lane/pdig: a block's kind for neth_rl_dig's line */
static int dig_kind(int b)
{
    switch (b & 4095)
    {
    case 0: case 6: case 27: case 28: case 30: case 31: case 32: case 37: case 38: case 39: case 40: case 50: case 51:
    case 55: case 59: case 63: case 65: case 66: case 68: case 69: case 70: case 72: case 75: case 76: case 77: case 78:
    case 83: case 104: case 105: case 106: case 111: case 115: case 141: case 142: case 143: case 147: case 148:
    case 157: case 171: case 175:
        return NETH_RL_DIG_AIR;
    case 12: case 13: return NETH_RL_DIG_LOOSE;
    case 8: case 9: return NETH_RL_DIG_WATER;
    case 10: case 11: return NETH_RL_DIG_LAVA;
    case 7: case 49: return NETH_RL_DIG_HARD;
    default: return NETH_RL_DIG_STONE;
    }
}

int neth_rl_dig(struct nwrl *h, int id, struct neth_rl_dig *out)
{
    memset(out, 0, sizeof *out);
    if (id < 0 || id >= h->n) return -1;
    struct session *ss = pool_session(pipe_pool(h->p), id);
    if (ss == NULL || ss->sr == NULL || ss->sp.dimension != 0) return -1;
    struct client_player *cp = &ss->cp;
    const struct server_player *sp = &ss->sp;
    int di = sr_dim_index(sp->dimension);
    struct world *w = ss->sr->w[di].st.w;
    struct an_world *an = &ss->sr->dims[di].anw;
    if (w == NULL) return -1;
    struct env *saved = neth_env;
    neth_env = pool_env(pipe_pool(h->p), id);     /* the livings are the env's arena's (lv_get, an_ent_at) */
    int ng = w->no_generate;
    w->no_generate = 1;
    double fx = sp->e.pos_x, fy = sp->e.pos_y, fz = sp->e.pos_z;
    double ex = cp->e.pos_x, ey = cp->e.pos_y + (double)(cp->e.y_offset - 1.62F), ez = cp->e.pos_z;
    float d2s[NETH_RL_DIG];
    const struct living *ls[NETH_RL_DIG];
    for (int i = 0; i < an->n; ++i)
    {
        const struct an_ent *en = an_ent_at(an->slot[i]);
        if (en == NULL || !en->used || !en->is_living || en->ghost) continue;
        const struct living *l = lv_get(en->livh);
        if (l == NULL || l->is_dead || l->health <= 0.0f || l->kind != HK_ENDERMAN) continue;
        double dx = l->e.pos_x - fx, dy = l->e.pos_y - fy, dz = l->e.pos_z - fz, d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > NETH_RL_DIG_R * NETH_RL_DIG_R) continue;
        out->all += 1;
        int k = out->n < NETH_RL_DIG ? out->n : NETH_RL_DIG;
        if (k == NETH_RL_DIG && d2 >= d2s[NETH_RL_DIG - 1]) continue;
        if (k == NETH_RL_DIG) --k;
        else out->n += 1;
        while (k > 0 && d2s[k - 1] > d2)
        {
            d2s[k] = d2s[k - 1], ls[k] = ls[k - 1], out->e[k] = out->e[k - 1];
            --k;
        }
        d2s[k] = (float)d2, ls[k] = l;
        memset(&out->e[k], 0, sizeof out->e[k]);
        out->e[k].id = l->entity_id;
        out->e[k].dx = (float)dx, out->e[k].dy = (float)dy, out->e[k].dz = (float)dz, out->e[k].d = (float)sqrt(d2);
    }
    out->pl_sky = census_sky(w, fx, fy, fz);
    for (int k = 0; k < out->n; ++k)
    {
        const struct living *l = ls[k];
        out->e[k].sky = (int8_t)census_sky(w, l->e.pos_x, l->e.pos_y, l->e.pos_z);
        /* the cells the segment from the eye to the box middle passes (a 3D DDA), the end cells left out */
        double mx = l->e.pos_x, my = l->e.pos_y + 0.5 * l->e.height, mz = l->e.pos_z;
        double o[3] = {ex, ey, ez}, dv[3] = {mx - ex, my - ey, mz - ez};
        int c[3], e3[3] = {(int)floor(mx), (int)floor(my), (int)floor(mz)}, st[3];
        double tmax[3], tdel[3];
        for (int a = 0; a < 3; ++a)
        {
            c[a] = (int)floor(o[a]);
            st[a] = dv[a] > 0 ? 1 : dv[a] < 0 ? -1 : 0;
            tdel[a] = st[a] ? fabs(1.0 / dv[a]) : 1e30;
            double nb = st[a] > 0 ? (double)(c[a] + 1) - o[a] : o[a] - (double)c[a];
            tmax[a] = st[a] ? nb * tdel[a] : 1e30;
        }
        for (int guard = 0; guard < 400; ++guard)
        {
            int a = tmax[0] < tmax[1] ? (tmax[0] < tmax[2] ? 0 : 2) : (tmax[1] < tmax[2] ? 1 : 2);
            if (tmax[a] > 1.0) break;
            c[a] += st[a], tmax[a] += tdel[a];
            if (c[0] == e3[0] && c[1] == e3[1] && c[2] == e3[2]) break;
            int b = c[1] < 0 || c[1] > 255 ? 0 : world_get_block(w, c[0], c[1], c[2]);
            out->e[k].cells[dig_kind(b)] += 1;
        }
    }
    w->no_generate = ng;
    neth_env = saved;
    return 0;
}

/* lane/pearl2: neth_rl_far's depth rows (the client world: what the frame shows) */
static void depth_scan(struct neth_rl_far *far, struct session *ss, struct client_player *cp)
{
    if (ss->client_world == NULL) return;
    double ex = cp->e.pos_x, ey = cp->e.pos_y + (double)(cp->e.y_offset - 1.62F), ez = cp->e.pos_z;
    for (int row = 0; row < 2; ++row)
        for (int i = 0; i < NETH_RL_DEPTH; ++i)
        {
            double yr = ((double)cp->rotation_yaw + 10.0 * (i - NETH_RL_DEPTH / 2)) * M_PI / 180.0;
            double pr = (row ? 8.0 : 0.0) * M_PI / 180.0;
            double lx = -sin(yr) * cos(pr), ly = -sin(pr), lz = cos(yr) * cos(pr);
            struct rt_mop m;
            far->depth[row][i] = (float)NETH_RL_DEPTH_R;
            far->depth_block[row][i] = 0;
            if (raytrace_blocks(ss->client_world, ex, ey, ez, ex + lx * NETH_RL_DEPTH_R, ey + ly * NETH_RL_DEPTH_R,
                                ez + lz * NETH_RL_DEPTH_R, 1, 0, 0, &m) && m.type >= 0)
            {
                far->depth[row][i] = (float)sqrt((m.hx - ex) * (m.hx - ex) + (m.hy - ey) * (m.hy - ey) +
                                                 (m.hz - ez) * (m.hz - ez));
                far->depth_block[row][i] = (int16_t)(world_get_block(ss->client_world, m.x, m.y, m.z) & 4095);
            }
        }
    /* lane/pearlsee: the hill row, NETH_RL_HILL_PITCH up (inside the frame's 35 degrees when the look is level) */
    for (int i = 0; i < NETH_RL_DEPTH; ++i)
    {
        double yr = ((double)cp->rotation_yaw + 10.0 * (i - NETH_RL_DEPTH / 2)) * M_PI / 180.0;
        double pr = -NETH_RL_HILL_PITCH * M_PI / 180.0;
        double lx = -sin(yr) * cos(pr), ly = -sin(pr), lz = cos(yr) * cos(pr);
        struct rt_mop m;
        far->hill_y[i] = -1.0f;
        if (raytrace_blocks(ss->client_world, ex, ey, ez, ex + lx * NETH_RL_DEPTH_R, ey + ly * NETH_RL_DEPTH_R,
                            ez + lz * NETH_RL_DEPTH_R, 1, 0, 0, &m) && m.type >= 0)
            far->hill_y[i] = (float)m.hy;
    }
    /* lane/pearlsee: the centre ray, along the look itself */
    {
        double yr = (double)cp->rotation_yaw * M_PI / 180.0, pr = (double)cp->rotation_pitch * M_PI / 180.0;
        double lx = -sin(yr) * cos(pr), ly = -sin(pr), lz = cos(yr) * cos(pr);
        struct rt_mop m;
        far->look_depth = (float)NETH_RL_DEPTH_R;
        far->look_block = 0;
        if (raytrace_blocks(ss->client_world, ex, ey, ez, ex + lx * NETH_RL_DEPTH_R, ey + ly * NETH_RL_DEPTH_R,
                            ez + lz * NETH_RL_DEPTH_R, 1, 0, 0, &m) && m.type >= 0)
        {
            far->look_depth = (float)sqrt((m.hx - ex) * (m.hx - ex) + (m.hy - ey) * (m.hy - ey) + (m.hz - ez) * (m.hz - ez));
            far->look_block = (int16_t)(world_get_block(ss->client_world, m.x, m.y, m.z) & 4095);
        }
    }
}

void neth_rl_obs_spec(const struct nwrl *h, int *w, int *hh, int *ticks, int *device)
{
    *w = h->w;
    *hh = h->h;
    *ticks = h->ticks;
    *device = h->device;
}

void *neth_rl_obs(struct nwrl *h, int64_t shape[5])
{
    if (h->camera == 1 || h->rc_on)
    {
        for (int i = 0; i < 5; ++i) shape[i] = 0;
        return NULL;
    }
    return pipe_buffer(h->p, shape);
}

void *neth_rl_semantic(struct nwrl *h, int64_t shape[4])
{
    shape[0] = h->n;
    shape[1] = h->camera ? h->sem.h : 0;
    shape[2] = h->camera ? h->sem.w : 0;
    shape[3] = 4;
    return h->sem_buf;
}

void *neth_rl_raycast(struct nwrl *h, int64_t shape[4])
{
    shape[0] = h->n;
    shape[1] = h->rc_on ? h->rc.h : 0;
    shape[2] = h->rc_on ? h->rc.w : 0;
    shape[3] = h->rc.ch == 1 ? 1 : 3;
    return h->rc_buf;
}

void *neth_rl_raycast_device(struct nwrl *h, int64_t shape[4])
{
    neth_rl_raycast(h, shape);
    return h->rcd ? rcdev_frames(h->rcd) : NULL;
}

int neth_rl_raycast_launch(struct nwrl *h, const int *ids, int k, void *stream)
{
    return h->rcd ? rcdev_launch(h->rcd, ids, k, stream) : -1;
}

int neth_rl_raycast_wait(struct nwrl *h, int ticket) { return h->rcd ? rcdev_wait(h->rcd, ticket) : -1; }

void neth_rl_raycast_fold(struct nwrl *h, int id, int32_t n[NETH_RL_SEEN])
{
    const struct rcd_seen *s = rcdev_seen(h->rcd, id);
    struct neth_rl_state *st = &h->e[id].st;
    for (int q = 0; q < NETH_RL_SEEN; ++q)
    {
        st->seen_n[q] = n[q] = s->n[q];
        st->seen[q].x = s->x[q], st->seen[q].y = s->y[q], st->seen[q].z = s->z[q];
        st->seen[q].d = s->d[q];
    }
}

int neth_rl_raycast_host_groups(const struct nwrl *h) { return h->rcd ? h->rc_host_groups : 0; }

int neth_rl_raycast_split(struct nwrl *h, int id, int host)
{
    if (h->rcd == NULL || id < 0 || id >= h->n) return -1;
    if (host && h->rc_hbuf == NULL && (h->rc_hbuf = rcdev_host_frames(h->rcd)) == NULL) return -1;
    h->rc_host[id] = host != 0;
    return 0;
}

int neth_rl_raycast_dump(struct nwrl *h, const int *ids, int k, const char *path) { return h->rcd ? rcdev_dump(h->rcd, ids, k, path) : -1; }

int neth_rl_raycast_put(struct nwrl *h, int lo, int k, void *stream) { return h->rcd ? rcdev_put(h->rcd, lo, k, stream) : -1; }

uint64_t neth_rl_raycast_refused(struct nwrl *h) { return __atomic_load_n(&h->rcd_refused, __ATOMIC_RELAXED); }

/* the lowest free index for a new start, -1 when every one is taken */
static int start_index(struct nwrl *h)
{
    for (int i = 0; i < h->nstart; ++i)
        if (h->start[i] == NULL) return i;
    return h->nstart < NETH_RL_STARTS ? h->nstart : -1;
}

static int start_put(struct nwrl *h, int i, struct pool_start *s)
{
    struct neth_rl_start *b = calloc(1, sizeof *b);
    b->s = s;
    h->start[i] = b;
    if (i == h->nstart) ++h->nstart;
    return i;
}

static struct neth_rl_start *start_of(struct nwrl *h, int start)
{
    return start >= 0 && start < h->nstart ? h->start[start] : NULL;
}

/* lane/proom: the library stage of start dir LIB/ID/start (train/starts.c's layout): LIB/ID/stage's first word, else
 * the stage column of LIB/index.tsv's row whose id is ID; "" when dir is not in a library */
static void lib_stage(const char *dir, char *out, size_t on)
{
    out[0] = 0;
    char d[PATH_MAX];
    snprintf(d, sizeof d, "%s", dir);
    size_t n = strlen(d);
    while (n > 1 && d[n - 1] == '/') d[--n] = 0;
    char *sl = strrchr(d, '/');
    if (sl == NULL || strcmp(sl + 1, "start")) return;
    *sl = 0;                                    /* d: LIB/ID */
    char p[PATH_MAX + 16], line[1024];
    snprintf(p, sizeof p, "%s/stage", d);
    FILE *f = fopen(p, "r");
    if (f)
    {
        if (fscanf(f, "%47s", out) != 1) out[0] = 0;
        fclose(f);
        if (out[0]) return;
    }
    char *id = strrchr(d, '/');
    if (id == NULL) return;
    *id++ = 0;                                  /* d: LIB */
    snprintf(p, sizeof p, "%s/index.tsv", d);
    if ((f = fopen(p, "r")) == NULL) return;
    size_t il = strlen(id);
    while (fgets(line, sizeof line, f))
    {
        if (strncmp(line, id, il) || line[il] != '\t') continue;
        char *c = strchr(line + il + 1, '\t');  /* past the seed */
        if (c == NULL) break;
        size_t k = strcspn(++c, "\t\n");
        snprintf(out, on, "%.*s", (int)(k < on ? k : on - 1), c);
        break;
    }
    fclose(f);
}

int neth_rl_start_load(struct nwrl *h, const char *dir, char *err, int en)
{
    int i = start_index(h);
    if (i < 0) { seterr(err, en, "too many starts", NULL); return -1; }
    char e[512];
    struct pool_start *s = pipe_start_load(h->p, dir, e, sizeof e);
    if (s == NULL) { seterr(err, en, dir, e); return -1; }
    start_put(h, i, s);
    lib_stage(dir, h->start[i]->stage, sizeof h->start[i]->stage);
    return i;
}

int neth_rl_start_load_bg(struct nwrl *h, const char *dir, char *err, int en)
{
    int i = start_index(h);
    if (i < 0) { seterr(err, en, "too many starts", NULL); return -1; }
    start_put(h, i, pipe_start_load_bg(h->p, dir));
    lib_stage(dir, h->start[i]->stage, sizeof h->start[i]->stage);
    return i;
}

const char *neth_rl_start_stage(struct nwrl *h, int start)
{
    struct neth_rl_start *b = start_of(h, start);
    return b == NULL || b->saved || !b->stage[0] ? NULL : b->stage;
}

int neth_rl_reset(struct nwrl *h, const int *ids, int k, int start, char *err, int en)
{
    struct neth_rl_start *b = start_of(h, start);
    if (b == NULL) { seterr(err, en, "no such start", NULL); return -1; }
    char e[512];
    if (pipe_reset(h->p, ids, k, b->s, e, sizeof e)) { seterr(err, en, "reset", e); return -1; }
    for (int i = 0; i < k; ++i)
    {
        struct neth_rl_env *ne = &h->e[ids[i]];
        if (b->saved)
        {
            /* the saving env's chests and scans go on */
            ne->st = b->st;
            ne->last_scan = b->last_scan;
            ne->last_window = b->last_window;
        }
        else
        {
            memset(&ne->st, 0, sizeof ne->st);
            ne->st.nearest = -1;
            ne->st.nearest_dist = -1;
            ne->last_scan = -1;
            ne->last_window = 0;
        }
        ne->hi_w = NULL;    /* census_scan's hi_* cache: the world under it was restored */
        rlact_reset(&ne->act);
        moveact_reset(h->mv, ids[i]);
        if (ne->raw) rlraw_reset(ne->raw);
    }
    return 0;
}

/* ------------------------------------------------------------- raw acts */

int neth_rl_raw(struct nwrl *h, int on)
{
    if (on && !h->raw)
        for (int i = 0; i < h->n; ++i)
        {
            if (h->e[i].raw == NULL)
            {
                h->e[i].raw = calloc(1, sizeof *h->e[i].raw);
                if (h->e[i].raw == NULL) return -1;
            }
            rlraw_reset(h->e[i].raw);
        }
    h->raw = on != 0;
    return 0;
}

int neth_rl_raw_state(struct nwrl *h, const int *ids, int k, int32_t *out)
{
    for (int i = 0; i < k; ++i)
    {
        int32_t *o = out + (size_t)i * 8;
        memset(o, 0, 8 * sizeof *o);
        if (ids[i] < 0 || ids[i] >= h->n) return -1;
        struct rlraw_env *r = h->e[ids[i]].raw;
        if (r == NULL) continue;
        struct session *ss = pool_session(pipe_pool(h->p), ids[i]);
        struct client_player *cp = ss ? &ss->cp : NULL;
        struct env *saved = neth_env;
        neth_env = pool_env(pipe_pool(h->p), ids[i]);
        const struct container *c = cp && cp->screen_inventory ? cp_gui_container(cp) : NULL;
        o[0] = (int32_t)floorf(r->x);
        o[1] = (int32_t)floorf(r->y);
        o[6] = -1;
        if (c == NULL)
        {
            /* the pointer a screen opened now would have */
            struct rlraw_env t = *r;
            rlraw_reset(&t);
            o[0] = (int32_t)t.x;
            o[1] = (int32_t)t.y;
            neth_env = saved;
            continue;
        }
        /* a screen the step's ticks opened: its pointer is the centre until
         * the next tick (rlraw_hook) */
        if (!r->up || r->window != c->window_id)
        {
            struct rlraw_env t = *r;
            rlraw_reset(&t);
            o[0] = (int32_t)t.x;
            o[1] = (int32_t)t.y;
        }
        struct gui_screen_layout l = gui_input_layout(&r->g, c, cp->sv.potions.size > 0);
        o[2] = l.left;
        o[3] = l.top;
        o[4] = l.width;
        o[5] = l.height;
        int hit = gui_screen_hit(c, l, o[0], o[1]);
        int outside = o[0] < l.left || o[1] < l.top || o[0] >= l.left + l.width || o[1] >= l.top + l.height;
        o[6] = hit >= 0 ? hit : outside ? -999 : -1;
        o[7] = 1;
        neth_env = saved;
    }
    return 0;
}

int neth_rl_scan(struct nwrl *h, int radius, int below, int above, int up_weight, const uint8_t *cls)
{
    if (radius < 0 || radius > 100 || below < 0 || above < 0 || below > 100 || above > 100) return -1;
    if (h->scan_buf == NULL) h->scan_buf = calloc((size_t)h->n, sizeof *h->scan_buf);
    struct rlscan_spec *s = calloc(1, sizeof *s);
    if (s == NULL || h->scan_buf == NULL) { free(s); return -1; }
    s->radius = radius;
    s->below = below;
    s->above = above;
    s->up_weight = up_weight > 0 ? up_weight : 1;
    memcpy(s->cls, cls, sizeof s->cls);
    /* the envs are idle between a poll and a step (the caller's): no worker reads it now */
    struct rlscan_spec *old = h->scan;
    h->scan = s;
    free(old);
    return 0;
}

int neth_rl_layer_auto(struct nwrl *h, float **mul, float **add, int32_t **flag)
{
    if (!h->raw) return -1;
    if (h->layer_flag == NULL)
    {
        size_t px = (size_t)h->w * (size_t)h->h * 3 * (size_t)h->n;
        h->layer_mul = malloc(px * sizeof *h->layer_mul);
        h->layer_add = malloc(px * sizeof *h->layer_add);
        h->layer_flag = calloc((size_t)h->n, sizeof *h->layer_flag);
        if (!h->layer_mul || !h->layer_add || !h->layer_flag) return -1;
    }
    *mul = h->layer_mul;
    *add = h->layer_add;
    *flag = h->layer_flag;
    return 0;
}

void *neth_rl_scan_buf(struct nwrl *h, int64_t *bytes)
{
    *bytes = h->scan_buf ? (int64_t)sizeof *h->scan_buf : 0;
    return h->scan_buf;
}

int neth_rl_raw_log(struct nwrl *h, int id, const char *path)
{
    if (id < 0 || id >= h->n || h->e[id].raw == NULL) return -1;
    struct rlraw_env *r = h->e[id].raw;
    if (r->log) fclose(r->log);
    r->log = NULL;
    if (path == NULL) return 0;
    r->log = fopen(path, "w");
    return r->log ? 0 : -1;
}

int neth_rl_start_save(struct nwrl *h, int id, char *err, int en)
{
    if (id < 0 || id >= h->n) { seterr(err, en, "no such env", NULL); return -1; }
    int i = start_index(h);
    if (i < 0) { seterr(err, en, "too many starts", NULL); return -1; }
    char e[512];
    struct pool_start *s = pipe_start_from_env(h->p, id, e, sizeof e);
    if (s == NULL) { seterr(err, en, "save", e); return -1; }
    start_put(h, i, s);
    struct neth_rl_start *b = h->start[i];
    const struct neth_rl_env *ne = &h->e[id];
    b->saved = 1;
    b->st = ne->st;
    b->last_scan = ne->last_scan;
    b->last_window = ne->last_window;
    return i;
}

int neth_rl_start_ready(struct nwrl *h, int start)
{
    struct neth_rl_start *b = start_of(h, start);
    return b == NULL ? -1 : pipe_start_ready(h->p, b->s);
}

int neth_rl_start_wait(struct nwrl *h, int start, char *err, int en)
{
    struct neth_rl_start *b = start_of(h, start);
    if (b == NULL) { seterr(err, en, "no such start", NULL); return -1; }
    char e[900];
    if (pipe_start_wait(h->p, b->s, e, sizeof e)) { seterr(err, en, "save", e); return -1; }
    return 0;
}

void neth_rl_start_free(struct nwrl *h, int start)
{
    struct neth_rl_start *b = start_of(h, start);
    if (b == NULL) return;
    pool_start_free(b->s);
    free(b);
    h->start[start] = NULL;
}

int64_t neth_rl_start_tick(struct nwrl *h, int start)
{
    struct neth_rl_start *b = start_of(h, start);
    return b == NULL ? -1 : pool_start_tick(b->s);
}

int neth_rl_start_info(struct nwrl *h, int start, double out[8])
{
    struct neth_rl_start *b = start_of(h, start);
    if (b == NULL) return -1;
    struct pool_start_info in;
    pool_start_info(b->s, &in);
    out[0] = (double)in.image_bytes;
    out[1] = (double)in.view_bytes;
    out[2] = (double)in.spill_bytes;
    out[3] = (double)in.log_bytes;
    out[4] = (double)in.replay_ticks;
    out[5] = in.load_ms;
    out[6] = (double)in.pointers;
    out[7] = (double)pool_start_tick(b->s);
    return 0;
}

int neth_rl_actions(struct nwrl *h, const struct neth_rl_actconf *c)
{
    if (c == NULL || !rlact_conf_ok(c)) return -1;
    h->actconf = *c;
    for (int i = 0; i < h->n; ++i)
    {
        int64_t em = h->e[i].act.emitted, rf = h->e[i].act.refused, fl = h->e[i].act.failed, dn = h->e[i].act.done;
        rlact_reset(&h->e[i].act);
        h->e[i].act.emitted = em;
        h->e[i].act.refused = rf;
        h->e[i].act.failed = fl;
        h->e[i].act.done = dn;
    }
    return 0;
}

void neth_rl_actions_get(const struct nwrl *h, struct neth_rl_actconf *c) { *c = h->actconf; }

int neth_rl_act_info(struct nwrl *h, int id, struct neth_rl_actinfo *out)
{
    if (id < 0 || id >= h->n) return -1;
    struct session *ss = pool_session(pipe_pool(h->p), id);
    if (ss == NULL) return -1;
    struct env *saved = neth_env;
    neth_env = pool_env(pipe_pool(h->p), id);
    rlact_info(&h->actconf, &h->e[id].act, ss, out);
    neth_env = saved;
    return 0;
}

int neth_rl_act_log(struct nwrl *h, int id, const char *path)
{
    if (id < 0 || id >= h->n) return -1;
    struct rlact_env *e = &h->e[id].act;
    if (e->log) fclose(e->log);
    e->log = NULL;
    if (path == NULL) return 0;
    e->log = fopen(path, "w");
    return e->log ? 0 : -1;
}
struct moveact *rlmove_of(struct nwrl *h) { return h->mv; }
void neth_vec_attach(struct nwrl *h, struct neth_vec *v) { h->vec = v; }
struct pipe *neth_rl_pipe(struct nwrl *h) { return h->p; }

int neth_rl_step(struct nwrl *h, const int *ids, int k, const struct neth_rl_act *acts)
{
    return neth_rl_step_ticks(h, ids, k, acts, h->ticks);
}

int neth_rl_rows(struct nwrl *h, int id, const char *path, int64_t cw_from)
{
    if (id < 0 || id >= h->n) return -1;
    struct neth_rl_env *ne = &h->e[id];
    if (ne->rows) fclose(ne->rows);
    ne->rows = NULL;
    if (path == NULL) return 0;
    ne->rows = fopen(path, "w");
    ne->cw_from = cw_from;
    return ne->rows ? 0 : -1;
}

int neth_rl_step_ticks(struct nwrl *h, const int *ids, int k, const struct neth_rl_act *acts, int n)
{
    for (int i = 0; i < k; ++i)
    {
        struct pool_act *a = &h->acts[i];
        const struct neth_rl_act *s = &acts[i];
        memset(a, 0, sizeof *a);
        a->kind = PA_AGENT;
        a->hold = s->hold;
        memcpy(a->press, s->press, sizeof a->press);
        a->look_mode = s->look_mode == 1 ? PL_LOOK : s->look_mode == 2 ? PL_DLOOK : PL_NONE;
        a->look[0] = s->look[0];
        a->look[1] = s->look[1];
        a->hotbar = s->hotbar;
        a->nops = s->nops < 0 ? 0 : s->nops > POOL_GUI_OPS ? POOL_GUI_OPS : s->nops;
        for (int j = 0; j < a->nops; ++j)
        {
            a->ops[j].kind = s->ops[j].kind;
            a->ops[j].window = s->ops[j].window;
            a->ops[j].slot = s->ops[j].slot;
            a->ops[j].button = s->ops[j].button;
            a->ops[j].mode = s->ops[j].mode;
            a->ops[j].index = s->ops[j].index;
        }
    }
    return pipe_step(h->p, ids, k, h->acts, n, 0);
}

static void stack_out(struct neth_rl_stack *d, const struct pool_stack *s)
{
    d->item = s->item;
    d->count = s->count;
    d->damage = s->damage;
    d->pad = 0;
}

static int poll_out(struct nwrl *h, int got, struct neth_rl_result *out);

int neth_rl_poll(struct nwrl *h, int b, struct neth_rl_result *out)
{
    if (b > h->n) b = h->n;
    return poll_out(h, pipe_poll(h->p, b, h->res), out);
}

int neth_rl_poll_some(struct nwrl *h, int min, int b, struct neth_rl_result *out)
{
    if (b > h->n) b = h->n;
    if (min < 1) min = 1;
    return poll_out(h, pipe_poll_some(h->p, min, b, h->res), out);
}

static int poll_out(struct nwrl *h, int got, struct neth_rl_result *out)
{
    for (int q = 0; q < got; ++q)
    {
        const struct pipe_result *pr = h->res[q];
        const struct pool_result *r = pr->r;
        struct neth_rl_result *o = &out[q];
        o->id = pr->id;
        o->flags = r->flags;
        o->ticks = r->ticks;
        o->frame_rc = pr->frame_rc != 0 ? pr->frame_rc : pr->obs.data == NULL && h->camera != 1 && !h->rc_on ? -3 : 0;
        o->t = r->t;
        o->slot = pr->obs.slot;
        o->ops_refused = r->ops_refused;
        const struct pool_priv *v = &r->priv;
        o->x = v->x;
        o->y = v->y;
        o->z = v->z;
        o->yaw = v->yaw;
        o->pitch = v->pitch;
        o->dim = v->dim;
        o->health = v->health;
        o->saturation = v->saturation;
        o->exhaustion = v->exhaustion;
        o->food = v->food;
        o->xp_total = v->xp_total;
        o->air = v->air;
        o->fire = v->fire;
        o->on_ground = v->on_ground;
        o->pad0 = 0;
        o->world_time = v->world_time;
        o->total_time = v->total_time;
        o->selected = r->hud.selected;
        o->screen = r->hud.screen;
        o->nslots = r->hud.nslots;
        o->xp_level = r->hud.xp_level;
        for (int i = 0; i < 9; ++i) stack_out(&o->hotbar[i], &r->hud.hotbar[i]);
        for (int i = 0; i < 90; ++i) stack_out(&o->slots[i], &r->hud.slots[i]);
        stack_out(&o->cursor, &r->hud.cursor);
        o->nevents = r->nevents < 64 ? r->nevents : 64;
        o->pad1 = 0;
        for (int i = 0; i < o->nevents; ++i)
        {
            o->events[i].stat = r->events[i].stat;
            o->events[i].before = r->events[i].before;
            o->events[i].after = r->events[i].after;
        }
        o->st = h->e[pr->id].st;
        snprintf(o->err, sizeof o->err, "%.255s", (r->flags & PF_ERROR) ? r->err : "");
    }
    return got;
}

int neth_rl_download(struct nwrl *h, int id, int slot, unsigned char *rgb)
{
    int64_t shape[5];
    unsigned char *buf = pipe_buffer(h->p, shape);
    struct pipe_obs ob = {0};
    ob.data = buf + ((size_t)slot * (size_t)h->n + (size_t)id) * (size_t)h->w * (size_t)h->h * 3;
    ob.shape[0] = h->h;
    ob.shape[1] = h->w;
    ob.shape[2] = 3;
    ob.device = h->device;
    ob.slot = slot;
    return pipe_download(h->p, &ob, rgb);
}

uint64_t neth_rl_fnv(uint64_t hh, const void *p, int64_t n)
{
    const unsigned char *b = p;
    for (int64_t i = 0; i < n; ++i) hh = (hh ^ b[i]) * 0x100000001b3ull;
    return hh;
}

void neth_rl_stats(struct nwrl *h, uint64_t out[8])
{
    struct pipe_stats s;
    pipe_stats(h->p, &s);
    memset(out, 0, 8 * sizeof out[0]);
    out[0] = s.steps;
    out[1] = s.frames;
    out[2] = s.poll_wait_ns;
    out[3] = s.tick_ns;
    out[4] = s.render_ns;
    out[5] = s.stream_idle_ns;
    out[6] = s.mesher_ns;
    out[7] = s.renders;
    pipe_stats_reset(h->p);
}

int neth_rl_env_mem(struct nwrl *h, int id, double out[12], char *items, int len)
{
    if (id < 0 || id >= h->n) return -1;
    struct envmem *m = proc_calloc(1, sizeof *m);
    if (m == NULL) return -1;
    struct pool *pl = pipe_pool(h->p);
    int ok = pool_env_mem(pl, id, m);
    for (int o = 0; o < EM_N && o < 9; ++o) out[o] = (double)m->b[o];
    struct env *e = pool_env(pl, id);
    out[9] = e ? (double)e->img.heap_used : 0;
    out[10] = e ? (double)e->img.heap_live : 0;
    out[11] = (double)envmem_total(m);
    int at = 0;
    if (items && len > 0)
    {
        items[0] = 0;
        for (int k = 0; k < m->nitems && at < len; ++k)
            at += snprintf(items + at, (size_t)(len - at), "%s\t%s\t%zu\n", envmem_name(m->items[k].owner),
                           m->items[k].what, m->items[k].bytes);
    }
    proc_free(m);
    return ok ? 0 : -1;
}

void neth_rl_stats_ext(struct nwrl *h, uint64_t out[16])
{
    struct pipe_stats s;
    pipe_stats(h->p, &s);
    memset(out, 0, 16 * sizeof out[0]);
    out[0] = s.steps;
    out[1] = s.frames;
    out[2] = s.poll_wait_ns;
    out[3] = s.tick_ns;
    out[4] = s.render_ns;
    out[5] = s.stream_idle_ns;
    out[6] = s.mesher_ns;
    out[7] = s.renders;
    out[8] = s.lat_step_ns;
    out[9] = s.lat_queue_ns;
    out[10] = s.lat_render_ns;
    out[11] = s.lat_poll_ns;
    out[12] = s.lat_trainer_ns;
    out[13] = (uint64_t)(s.device_ms * 1e6);
    out[14] = (uint64_t)(s.upload_ms * 1e6);
    out[15] = s.upload_bytes;
    pipe_stats_reset(h->p);
}

/* ------------------------------------------------------- the seed's chests */

static void site_add(struct neth_rl_site *out, int max, int *n, int x, int y, int z, int kind)
{
    if (*n < max) out[*n] = (struct neth_rl_site){x, y, z, kind};
    ++*n;
}

int neth_rl_structure_chests(int64_t seed, int cx0, int cz0, int cx1, int cz1, struct neth_rl_site *out, int max)
{
    /* the walks keep their layer stacks in the current environment: a
     * scratch one, never an env of a pool */
    struct env *saved = neth_env, *e = env_new();
    neth_env = e;
    int n = 0;
    struct start_list l = {0};
    structure_walk(&structure_village, seed, cx0, cz0, cx1, cz1, &l);
    for (int i = 0; i < l.n; ++i)
    {
        const struct start *s = l.v[i];
        if (s->n == 0 || !s->pieces[0]->u.village.valid) continue;
        for (int k = 0; k < s->n; ++k)
        {
            const struct piece *p = s->pieces[k];
            if (p->kind == PIECE_VILLAGE && p->u.village.kind == V_HOUSE2)
                site_add(out, max, &n, sc_x_with_offset(p, 5, 5), sc_y_with_offset(p, 1), sc_z_with_offset(p, 5, 5), 1);
        }
    }
    start_list_free(&l);
    memset(&l, 0, sizeof l);
    structure_walk(&structure_temple, seed, cx0, cz0, cx1, cz1, &l);
    for (int i = 0; i < l.n; ++i)
        for (int k = 0; k < l.v[i]->n; ++k)
        {
            const struct piece *p = l.v[i]->pieces[k];
            if (p->kind != PIECE_TEMPLE) continue;
            if (p->u.temple.kind == TEMPLE_DESERT_PYRAMID)
            {
                /* DesertPyramid: one chest a horizontal direction around (10, -11, 10) */
                static const int d[4][2] = {{0, 1}, {-1, 0}, {0, -1}, {1, 0}};
                for (int j = 0; j < 4; ++j)
                    site_add(out, max, &n, sc_x_with_offset(p, 10 + d[j][0], 10 + d[j][1]), sc_y_with_offset(p, -11),
                             sc_z_with_offset(p, 10 + d[j][0], 10 + d[j][1]), 2);
            }
            else if (p->u.temple.kind == TEMPLE_JUNGLE_PYRAMID)
            {
                site_add(out, max, &n, sc_x_with_offset(p, 8, 3), sc_y_with_offset(p, -3), sc_z_with_offset(p, 8, 3), 3);
                site_add(out, max, &n, sc_x_with_offset(p, 9, 10), sc_y_with_offset(p, -3), sc_z_with_offset(p, 9, 10), 3);
            }
        }
    start_list_free(&l);
    neth_env = saved;
    env_free(e);
    return n;
}

int neth_rl_seed_chests(int64_t seed, int64_t spawn[3], struct neth_rl_site *out, int max)
{
    struct env *saved = neth_env, *e = env_new();
    neth_env = e;
    struct seedworld *sw = calloc(1, sizeof *sw);
    seedworld_init(sw, seed);
    seedworld_build(sw);
    spawn[0] = sw->spawn_x;
    spawn[1] = sw->spawn_y;
    spawn[2] = sw->spawn_z;
    struct world *w = &sw->p.world;
    int n = 0, cx0 = (int)(sw->spawn_x >> 4), cz0 = (int)(sw->spawn_z >> 4);
    for (int cx = cx0 - 13; cx <= cx0 + 13; ++cx)
        for (int cz = cz0 - 13; cz <= cz0 + 13; ++cz)
        {
            struct chunk *c = world_chunk(w, cx, cz);
            if (c == NULL) continue;
            for (int i = 0; i < c->tes.n; ++i)
            {
                const struct tile_entity *te = c->tes.v[i];
                if (te != NULL && te->kind == TE_CHEST) site_add(out, max, &n, te->x, te->y, te->z, 4);
            }
        }
    seedworld_free(sw);
    free(sw);
    neth_env = saved;
    env_free(e);
    return n;
}

int neth_rl_blocks(struct nwrl *h, int id, int x0, int y0, int z0, int x1, int y1, int z1, int16_t *out)
{
    if (id < 0 || id >= h->n || x1 < x0 || y1 < y0 || z1 < z0 || y0 < 0 || y1 > 255) return -1;
    struct session *ss = pool_session(pipe_pool(h->p), id);
    if (ss == NULL || ss->client_world == NULL) return -1;
    struct env *saved = neth_env;
    neth_env = pool_env(pipe_pool(h->p), id);
    size_t k = 0;
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x)
            {
                struct chunk *c = world_chunk(ss->client_world, x >> 4, z >> 4);
                out[k++] = c ? (int16_t)(chunk_get_block(c, x & 15, y, z & 15) & 4095) : -1;
            }
    neth_env = saved;
    return 0;
}

int neth_rl_screen_layer(struct nwrl *h, int id, int w, int hh, float *mul, float *add)
{
    if (id < 0 || id >= h->n || w <= 0 || hh <= 0) return -1;
    struct session *ss = pool_session(pipe_pool(h->p), id);
    if (ss == NULL) return -1;
    struct env *saved = neth_env;
    neth_env = pool_env(pipe_pool(h->p), id);
    /* the raw form draws its pointer */
    int mx = -1, my = -1;
    if (h->raw && h->e[id].raw)
    {
        int32_t o[8];
        neth_rl_raw_state(h, &id, 1, o);
        mx = o[0];
        my = o[1];
    }
    int rc = rlgui_layer(ss, w, hh, mul, add, mx, my);
    neth_env = saved;
    return rc;
}

struct layer_job {
    struct nwrl *h;
    const int *ids;
    int k, w, hh, step, first;
    float *mul, *add;
    int *rc;
};

static void *layer_thread(void *arg)
{
    struct layer_job *j = arg;
    size_t px = (size_t)j->w * j->hh * 3;
    for (int i = j->first; i < j->k; i += j->step)
        j->rc[i] = neth_rl_screen_layer(j->h, j->ids[i], j->w, j->hh, j->mul + i * px, j->add + i * px);
    return NULL;
}

int neth_rl_screen_layers(struct nwrl *h, const int *ids, int k, int w, int hh, float *mul, float *add, int *rc,
                       int threads)
{
    for (int i = 0; i < k; ++i)
        if (ids[i] < 0 || ids[i] >= h->n) return -1;
    if (w <= 0 || hh <= 0) return -1;
    int T = threads > 0 ? threads : k;
    if (T > 16) T = 16;
    if (T > k) T = k;
    if (T <= 1)
    {
        struct layer_job j = {h, ids, k, w, hh, 1, 0, mul, add, rc};
        layer_thread(&j);
        return 0;
    }
    struct layer_job jobs[16];
    pthread_t th[16];
    int started = 0;
    for (int t = 0; t < T; ++t)
    {
        jobs[t] = (struct layer_job){h, ids, k, w, hh, T, t, mul, add, rc};
        if (t > 0 && pthread_create(&th[t], NULL, layer_thread, &jobs[t]) == 0) started |= 1 << t;
    }
    layer_thread(&jobs[0]);
    for (int t = 1; t < T; ++t)
    {
        if (started & (1 << t)) pthread_join(th[t], NULL);
        else layer_thread(&jobs[t]);
    }
    return 0;
}

int neth_rl_recipes(struct neth_rl_recipe *out, int max)
{
    int n = 0;
    for (int i = 0; i < RECIPE_COUNT; ++i)
    {
        const struct recipe_def *r = &RECIPES[i];
        if (r->kind != RECIPE_SHAPED && r->kind != RECIPE_SHAPELESS) continue;
        if (n < max)
        {
            struct neth_rl_recipe *o = &out[n];
            memset(o, 0, sizeof *o);
            o->kind = r->kind == RECIPE_SHAPED ? 0 : 1;
            o->out_item = r->output.item;
            o->out_count = r->output.count;
            o->out_damage = r->output.damage;
            for (int k = 0; k < r->count && o->n < 9; ++k)
            {
                const struct stack_def *d = &RECIPE_ITEMS[r->items + k];
                if (!d->exists) continue;
                o->in_item[o->n] = d->item;
                o->in_damage[o->n] = d->damage;
                ++o->n;
            }
        }
        ++n;
    }
    for (int i = 0; i < SMELTING_COUNT; ++i)
    {
        if (n < max)
        {
            struct neth_rl_recipe *o = &out[n];
            memset(o, 0, sizeof *o);
            o->kind = 2;
            o->out_item = SMELTING[i].output.item;
            o->out_count = SMELTING[i].output.count;
            o->out_damage = SMELTING[i].output.damage;
            o->n = 1;
            o->in_item[0] = SMELTING[i].input.item;
            o->in_damage[0] = SMELTING[i].input.damage;
        }
        ++n;
    }
    return n;
}
