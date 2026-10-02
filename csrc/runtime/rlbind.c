/* The trainer's binding (rlbind.h). */
#define _GNU_SOURCE
#include "rlbind.h"

#include <dlfcn.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../engine/env.h"
#include "../engine/envmem.h"
#include "../engine/player.h"
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

_Static_assert(NWRL_KEYS == PK_KEYS, "nwrl_act.press is pool_act.press");
_Static_assert(NWRL_OPS == POOL_GUI_OPS, "nwrl_act.ops is pool_act.ops");

enum { NWRL_STARTS = 4096 };

/* a start of the binding's (its index is its place in nwrl.start): the
 * pool's, and for one saved from an env (nwrl_start_save) that env's
 * binding state then, which an env reset to it takes */
struct nwrl_start {
    struct pool_start *s;
    int saved;
    struct nwrl_state st;
    int64_t last_scan;
    int last_window;
};

struct nwrl_env {
    struct nwrl_state st;       /* written by the worker stepping the env, read after its poll */
    int64_t last_scan;          /* the tick of the last chest scan (-1 none since the reset) */
    int last_window;
    FILE *rows;                 /* rlgui.h nwrl_rows: each tick's row record */
    int64_t cw_from;
    struct rlact_env act;       /* rlact.h: the action compiler's queue */
};

struct nwrl {
    struct pipe *p;
    char *config;               /* the pool keeps the path */
    struct nwrl_start *start[NWRL_STARTS];  /* NULL: a free index (nwrl_start_free) */
    int nstart;                 /* the indices used so far */
    int n, w, h, ticks, device;
    int chest_radius, chest_every;
    struct nwrl_actconf actconf;    /* rlact.h */
    struct nwrl_env *e;
    struct pool_act *acts;      /* nwrl_step's conversion */
    const struct pipe_result **res;
    struct moveact *mv;         /* the action compiler (rlmove.h) */
    /* the semantic camera (client.camera semantic or both): env id's cast at
     * sem_buf + id * sem.w * sem.h, written at its step's end */
    int camera;
    struct semcam_spec sem;
    struct semcam_cell *sem_buf;
};

int nwrl_version(void) { return NWRL_VERSION; }

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

/* every chest in the loaded chunks within R chunks of (px, pz) of world W,
 * merged into the env's list (a new one appended with its item count; a
 * known one's count updated: what the player took is what it lost) */
static void chest_scan(struct nwrl *h, struct nwrl_state *st, struct world *w, int dim, double px, double pz)
{
    int R = h->chest_radius, cx0 = (int)floor(px) >> 4, cz0 = (int)floor(pz) >> 4;
    for (int cx = cx0 - R; cx <= cx0 + R; ++cx)
        for (int cz = cz0 - R; cz <= cz0 + R; ++cz)
        {
            struct chunk *c = world_chunk(w, cx, cz);
            if (c == NULL) continue;
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
                    if (st->nchest == NWRL_CHESTS) continue;
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
}

/* the worker, after each tick of env id (pool.h tick_hook): the engine's
 * state read, nothing written but the env's own nwrl_state */
static void state_hook(void *ctx, int id, struct session *ss, int64_t t)
{
    struct nwrl *h = ctx;
    struct nwrl_env *ne = &h->e[id];
    struct nwrl_state *st = &ne->st;
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
    st->current = sp->sv.current_item;
    st->window = sp->window_id;
    st->gui_x = sp->gui_x;
    st->gui_y = sp->gui_y;
    st->gui_z = sp->gui_z;
    const struct surv_mouseover_pos *mo = &nw_env->survival.mo.cur;
    struct world *cw = ss->client_world;
    st->mo_hit = mo->num != 0 && !mo->is_miss && mo->entity_id == 0;
    st->mo_x = mo->x;
    st->mo_y = mo->y;
    st->mo_z = mo->z;
    st->mo_side = mo->side;
    st->mo_block = st->mo_hit && cw ? world_get_block(cw, mo->x, mo->y, mo->z) & 4095 : 0;
    st->mo_meta = st->mo_hit && cw ? world_get_meta(cw, mo->x, mo->y, mo->z) : 0;
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
    int crafted = 0;
    for (int i = 0; i < 4096; ++i) crafted += ss_->v[STAT_CRAFT_ITEM + i];
    st->crafted = crafted;
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
    (void)r;
    semcam_cast(&h->sem, ss, h->sem_buf + (size_t)id * (size_t)h->sem.w * (size_t)h->sem.h);
}

/* the worker, before each tick of env id (pool.h act_hook): the action
 * compilers, lane/moveact's for moving, mining and looking (moveact.h), then
 * lane/guiact's for the screens (rlact.h) on the act it leaves; the act is
 * written to out (1) when either changed it, else src runs as it is (0) */
static int act_hook(void *ctx, int id, struct session *ss, int k, const struct pool_act *src, int hold_only,
                    struct pool_act *out)
{
    struct nwrl *h = ctx;
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

/* ------------------------------------------------------------------ API */

struct nwrl *nwrl_make(const struct nwrl_opts *o, char *err, int en)
{
    return nwrl_make_ex(o, 0, err, en);
}

struct nwrl *nwrl_make_ex(const struct nwrl_opts *o, int flags, char *err, int en)
{
    if (o == NULL || o->config == NULL || o->n <= 0) { seterr(err, en, "nwrl_make: a config and n > 0 are needed", NULL); return NULL; }
    struct pipe_config cfg = {0};
    struct wconf wc;
    char e[512];
    if (pipe_config_file(&cfg, o->config, &wc, e, sizeof e)) { seterr(err, en, "config", e); return NULL; }
    char start[PATH_MAX];
    if (o->start) snprintf(start, sizeof start, "%s", o->start);
    else wconf_start_path(&wc, start, sizeof start);
    /* playview.so beside this library */
    char so[PATH_MAX];
    if (o->view_so) snprintf(so, sizeof so, "%s", o->view_so);
    else
    {
        Dl_info di;
        if (!dladdr((void *)nwrl_make, &di) || di.dli_fname == NULL) { seterr(err, en, "cannot find libnwrl.so's directory", NULL); return NULL; }
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
    /* the config's sim.actions (rlact.h; nwrl_actions changes them) */
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
    cfg.pool.threads = o->threads;
    cfg.pool.privileged = 1;
    cfg.pool.no_counters = 1;
    cfg.pool.tick_hook = state_hook;
    if (flags & NWRL_DEV) rlgui_dev(&cfg.pool);
    if (flags & NWRL_SAVE) cfg.pool.save_slots = 4;
    cfg.pool.tick_hook_ctx = h;
    h->mv = moveact_new(o->n);
    cfg.pool.act_hook = act_hook;
    cfg.pool.act_hook_ctx = h;
    /* client.camera: semantic runs no view and no renderer (the pool alone,
     * pipeline.h no_view); semantic and both cast after each step */
    wconf_semantic(&wc, &h->camera, &h->sem.w, &h->sem.h, &h->sem.range);
    if (h->camera)
    {
        h->sem_buf = calloc((size_t)o->n * (size_t)h->sem.w * (size_t)h->sem.h, sizeof *h->sem_buf);
        cfg.pool.step_hook = sem_hook;
        cfg.pool.step_hook_ctx = h;
    }
    if (h->camera == 1) cfg.no_view = 1;
    cfg.par.streams = o->streams;
    cfg.par.batch = o->batch;
    cfg.par.group = o->group;
    cfg.par.depth = o->depth;
    if (o->device_mesh >= 0) cfg.device_mesh = o->device_mesh;
    cfg.device = o->device;
    cfg.view_so = so;
    h->p = pipe_make(&cfg, e, sizeof e);
    if (h->p == NULL) { seterr(err, en, "pipe_make", e); nwrl_free(h); return NULL; }
    h->w = cfg.pool.obs.w > 0 ? cfg.pool.obs.w : 128;
    h->h = cfg.pool.obs.h > 0 ? cfg.pool.obs.h : 128;
    h->ticks = cfg.pool.obs.ticks > 0 ? cfg.pool.obs.ticks : 4;
    h->device = o->device;
    rlmove_init(h->mv, h->w, h->h, &wc);
    if (nwrl_start_load(h, start, err, en) != 0) { nwrl_free(h); return NULL; }
    int *ids = malloc((size_t)o->n * sizeof *ids);
    for (int i = 0; i < o->n; ++i) ids[i] = i;
    int rc = nwrl_reset(h, ids, o->n, 0, err, en);
    free(ids);
    if (rc) { nwrl_free(h); return NULL; }
    return h;
}

void nwrl_free(struct nwrl *h)
{
    if (h == NULL) return;
    if (h->p) pipe_free(h->p);
    moveact_free(h->mv);
    for (int i = 0; h->e && i < h->n; ++i)
    {
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
    free(h);
}

int nwrl_size(const struct nwrl *h) { return h->n; }

void nwrl_obs_spec(const struct nwrl *h, int *w, int *hh, int *ticks, int *device)
{
    *w = h->w;
    *hh = h->h;
    *ticks = h->ticks;
    *device = h->device;
}

void *nwrl_obs(struct nwrl *h, int64_t shape[5])
{
    if (h->camera == 1)
    {
        for (int i = 0; i < 5; ++i) shape[i] = 0;
        return NULL;
    }
    return pipe_buffer(h->p, shape);
}

void *nwrl_semantic(struct nwrl *h, int64_t shape[4])
{
    shape[0] = h->n;
    shape[1] = h->camera ? h->sem.h : 0;
    shape[2] = h->camera ? h->sem.w : 0;
    shape[3] = 4;
    return h->sem_buf;
}

/* the lowest free index for a new start, -1 when every one is taken */
static int start_index(struct nwrl *h)
{
    for (int i = 0; i < h->nstart; ++i)
        if (h->start[i] == NULL) return i;
    return h->nstart < NWRL_STARTS ? h->nstart : -1;
}

static int start_put(struct nwrl *h, int i, struct pool_start *s)
{
    struct nwrl_start *b = calloc(1, sizeof *b);
    b->s = s;
    h->start[i] = b;
    if (i == h->nstart) ++h->nstart;
    return i;
}

static struct nwrl_start *start_of(struct nwrl *h, int start)
{
    return start >= 0 && start < h->nstart ? h->start[start] : NULL;
}

int nwrl_start_load(struct nwrl *h, const char *dir, char *err, int en)
{
    int i = start_index(h);
    if (i < 0) { seterr(err, en, "too many starts", NULL); return -1; }
    char e[512];
    struct pool_start *s = pipe_start_load(h->p, dir, e, sizeof e);
    if (s == NULL) { seterr(err, en, dir, e); return -1; }
    return start_put(h, i, s);
}

int nwrl_reset(struct nwrl *h, const int *ids, int k, int start, char *err, int en)
{
    struct nwrl_start *b = start_of(h, start);
    if (b == NULL) { seterr(err, en, "no such start", NULL); return -1; }
    char e[512];
    if (pipe_reset(h->p, ids, k, b->s, e, sizeof e)) { seterr(err, en, "reset", e); return -1; }
    for (int i = 0; i < k; ++i)
    {
        struct nwrl_env *ne = &h->e[ids[i]];
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
        rlact_reset(&ne->act);
        moveact_reset(h->mv, ids[i]);
    }
    return 0;
}

int nwrl_start_save(struct nwrl *h, int id, char *err, int en)
{
    if (id < 0 || id >= h->n) { seterr(err, en, "no such env", NULL); return -1; }
    int i = start_index(h);
    if (i < 0) { seterr(err, en, "too many starts", NULL); return -1; }
    char e[512];
    struct pool_start *s = pipe_start_from_env(h->p, id, e, sizeof e);
    if (s == NULL) { seterr(err, en, "save", e); return -1; }
    start_put(h, i, s);
    struct nwrl_start *b = h->start[i];
    const struct nwrl_env *ne = &h->e[id];
    b->saved = 1;
    b->st = ne->st;
    b->last_scan = ne->last_scan;
    b->last_window = ne->last_window;
    return i;
}

int nwrl_start_ready(struct nwrl *h, int start)
{
    struct nwrl_start *b = start_of(h, start);
    return b == NULL ? -1 : pipe_start_ready(h->p, b->s);
}

int nwrl_start_wait(struct nwrl *h, int start, char *err, int en)
{
    struct nwrl_start *b = start_of(h, start);
    if (b == NULL) { seterr(err, en, "no such start", NULL); return -1; }
    char e[900];
    if (pipe_start_wait(h->p, b->s, e, sizeof e)) { seterr(err, en, "save", e); return -1; }
    return 0;
}

void nwrl_start_free(struct nwrl *h, int start)
{
    struct nwrl_start *b = start_of(h, start);
    if (b == NULL) return;
    pool_start_free(b->s);
    free(b);
    h->start[start] = NULL;
}

int64_t nwrl_start_tick(struct nwrl *h, int start)
{
    struct nwrl_start *b = start_of(h, start);
    return b == NULL ? -1 : pool_start_tick(b->s);
}

int nwrl_start_info(struct nwrl *h, int start, double out[8])
{
    struct nwrl_start *b = start_of(h, start);
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

int nwrl_actions(struct nwrl *h, const struct nwrl_actconf *c)
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

void nwrl_actions_get(const struct nwrl *h, struct nwrl_actconf *c) { *c = h->actconf; }

int nwrl_act_info(struct nwrl *h, int id, struct nwrl_actinfo *out)
{
    if (id < 0 || id >= h->n) return -1;
    struct session *ss = pool_session(pipe_pool(h->p), id);
    if (ss == NULL) return -1;
    struct env *saved = nw_env;
    nw_env = pool_env(pipe_pool(h->p), id);
    rlact_info(&h->actconf, &h->e[id].act, ss, out);
    nw_env = saved;
    return 0;
}

int nwrl_act_log(struct nwrl *h, int id, const char *path)
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

int nwrl_step(struct nwrl *h, const int *ids, int k, const struct nwrl_act *acts)
{
    return nwrl_step_ticks(h, ids, k, acts, h->ticks);
}

int nwrl_rows(struct nwrl *h, int id, const char *path, int64_t cw_from)
{
    if (id < 0 || id >= h->n) return -1;
    struct nwrl_env *ne = &h->e[id];
    if (ne->rows) fclose(ne->rows);
    ne->rows = NULL;
    if (path == NULL) return 0;
    ne->rows = fopen(path, "w");
    ne->cw_from = cw_from;
    return ne->rows ? 0 : -1;
}

int nwrl_step_ticks(struct nwrl *h, const int *ids, int k, const struct nwrl_act *acts, int n)
{
    for (int i = 0; i < k; ++i)
    {
        struct pool_act *a = &h->acts[i];
        const struct nwrl_act *s = &acts[i];
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

static void stack_out(struct nwrl_stack *d, const struct pool_stack *s)
{
    d->item = s->item;
    d->count = s->count;
    d->damage = s->damage;
    d->pad = 0;
}

static int poll_out(struct nwrl *h, int got, struct nwrl_result *out);

int nwrl_poll(struct nwrl *h, int b, struct nwrl_result *out)
{
    if (b > h->n) b = h->n;
    return poll_out(h, pipe_poll(h->p, b, h->res), out);
}

int nwrl_poll_some(struct nwrl *h, int min, int b, struct nwrl_result *out)
{
    if (b > h->n) b = h->n;
    if (min < 1) min = 1;
    return poll_out(h, pipe_poll_some(h->p, min, b, h->res), out);
}

static int poll_out(struct nwrl *h, int got, struct nwrl_result *out)
{
    for (int q = 0; q < got; ++q)
    {
        const struct pipe_result *pr = h->res[q];
        const struct pool_result *r = pr->r;
        struct nwrl_result *o = &out[q];
        o->id = pr->id;
        o->flags = r->flags;
        o->ticks = r->ticks;
        o->frame_rc = pr->frame_rc != 0 ? pr->frame_rc : pr->obs.data == NULL && h->camera != 1 ? -3 : 0;
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

int nwrl_download(struct nwrl *h, int id, int slot, unsigned char *rgb)
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

uint64_t nwrl_fnv(uint64_t hh, const void *p, int64_t n)
{
    const unsigned char *b = p;
    for (int64_t i = 0; i < n; ++i) hh = (hh ^ b[i]) * 0x100000001b3ull;
    return hh;
}

void nwrl_stats(struct nwrl *h, uint64_t out[8])
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

int nwrl_env_mem(struct nwrl *h, int id, double out[12], char *items, int len)
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

void nwrl_stats_ext(struct nwrl *h, uint64_t out[16])
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

static void site_add(struct nwrl_site *out, int max, int *n, int x, int y, int z, int kind)
{
    if (*n < max) out[*n] = (struct nwrl_site){x, y, z, kind};
    ++*n;
}

int nwrl_structure_chests(int64_t seed, int cx0, int cz0, int cx1, int cz1, struct nwrl_site *out, int max)
{
    /* the walks keep their layer stacks in the current environment: a
     * scratch one, never an env of a pool */
    struct env *saved = nw_env, *e = env_new();
    nw_env = e;
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
    nw_env = saved;
    env_free(e);
    return n;
}

int nwrl_seed_chests(int64_t seed, int64_t spawn[3], struct nwrl_site *out, int max)
{
    struct env *saved = nw_env, *e = env_new();
    nw_env = e;
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
    nw_env = saved;
    env_free(e);
    return n;
}

int nwrl_blocks(struct nwrl *h, int id, int x0, int y0, int z0, int x1, int y1, int z1, int16_t *out)
{
    if (id < 0 || id >= h->n || x1 < x0 || y1 < y0 || z1 < z0 || y0 < 0 || y1 > 255) return -1;
    struct session *ss = pool_session(pipe_pool(h->p), id);
    if (ss == NULL || ss->client_world == NULL) return -1;
    struct env *saved = nw_env;
    nw_env = pool_env(pipe_pool(h->p), id);
    size_t k = 0;
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x)
            {
                struct chunk *c = world_chunk(ss->client_world, x >> 4, z >> 4);
                out[k++] = c ? (int16_t)(chunk_get_block(c, x & 15, y, z & 15) & 4095) : -1;
            }
    nw_env = saved;
    return 0;
}

int nwrl_screen_layer(struct nwrl *h, int id, int w, int hh, float *mul, float *add)
{
    if (id < 0 || id >= h->n || w <= 0 || hh <= 0) return -1;
    struct session *ss = pool_session(pipe_pool(h->p), id);
    if (ss == NULL) return -1;
    struct env *saved = nw_env;
    nw_env = pool_env(pipe_pool(h->p), id);
    int rc = rlgui_layer(ss, w, hh, mul, add);
    nw_env = saved;
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
        j->rc[i] = nwrl_screen_layer(j->h, j->ids[i], j->w, j->hh, j->mul + i * px, j->add + i * px);
    return NULL;
}

int nwrl_screen_layers(struct nwrl *h, const int *ids, int k, int w, int hh, float *mul, float *add, int *rc,
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

int nwrl_recipes(struct nwrl_recipe *out, int max)
{
    int n = 0;
    for (int i = 0; i < RECIPE_COUNT; ++i)
    {
        const struct recipe_def *r = &RECIPES[i];
        if (r->kind != RECIPE_SHAPED && r->kind != RECIPE_SHAPELESS) continue;
        if (n < max)
        {
            struct nwrl_recipe *o = &out[n];
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
            struct nwrl_recipe *o = &out[n];
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
