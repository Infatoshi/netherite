/* Chunk generation ahead of the step (genahead.h). */
#define _POSIX_C_SOURCE 200809L
#include "genahead.h"

#include "chunkgen.h"
#include "dragon.h"
#include "endfight.h"
#include "env.h"
#include "phase.h"
#include "regionspill.h"
#include "living.h"
#include "nether.h"
#include "player.h"
#include "serverreplay.h"
#include "ticks.h"
#include "jmath.h"
#include "arena.h"
#include "world.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------ the cache */

enum { GA_EMPTY, GA_PENDING, GA_READY, GA_STORED };

/* An entry: a chunk made ahead (READY, with the provider state), one being
 * made this step (PENDING), the state alone of an owed generation
 * (owed_only), or a chunk the region store holds (STORED: its load needs no
 * generation; remembered so the prediction does not look it up again). */
struct ga_entry {
    int32_t cx, cz;
    int8_t dim;
    uint8_t status;
    uint8_t owed_only;
    uint8_t raw_built;          /* the raw result is built (genahead_fill_built): raw_ids its header, raw_metas its bands */
    int32_t state;              /* the overworld's biome and topBlock table (states), -1 none */
    uint64_t mark;              /* the last step that predicted it */
    uint64_t rand;              /* the provider Random's state after the generation */
    struct chunk *c;
    /* genahead_fill_raw's: the result, constructed when the tick takes it,
     * and its holder's reference, released then (or when swept) */
    const uint8_t *raw_ids, *raw_metas;
    void *raw_owner;
};

struct ga_state {
    uint8_t biome[256], tops[256];
};

/* Steps an entry stays after the prediction last named it. */
#define GA_KEEP 32
#define GA_SEEN 128
/* the pathfinder window's half width in blocks */
#define GA_PATH 50

struct genahead {
    int ahead;                  /* genahead_set_ahead's rings */
    struct ga_entry *t;         /* open addressing on (dim, cx, cz) */
    size_t cap, used;
    uint64_t step;
    struct ga_stats st;
    uint64_t bytes;             /* the cached chunks' and states' storage */
    int ready;                  /* READY entries holding a chunk */
    /* the overworld states, and the free ones (the tick frees, the step makes) */
    struct ga_state *states;
    int32_t *sfree;
    int nstates, nsfree;
    /* the step's new requests: (dim, cx, cz) triples */
    int32_t *pend;
    size_t npend, cappend;
    /* the same as requests, for genahead_predict's caller */
    struct ga_req *reqs;
    int capreq;
    /* the player's chunk and dimension at the step start, and the square
     * the prediction walked around it */
    int pdim, pcx, pcz, pr;
    FILE *misslog;              /* genahead_log_misses */
    /* genahead_hint's points for the next step, in the player's dimension */
    double hint[8][2];
    int nhint;
    struct tick_entry due[1000];   /* the pending ticks the next tick runs */
    /* the step's chunks already looked at in the player's dimension, a
     * GA_SEEN x GA_SEEN grid centred on the player's chunk */
    uint64_t seen[GA_SEEN * GA_SEEN / 64];
    /* the livings' windows walked this step: (dim, x0, z0), valid when
     * win_step is the step */
    int32_t win[256][3];
    uint64_t win_step[256];
    uint16_t ids16[GA_CELLS];   /* a result widened for the constructors */
    void (*release)(void *owner);   /* genahead_fill_raw's */
};

static uint64_t ga_now_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int dim_index(int dim)
{
    return dim == -1 ? 1 : dim == 1 ? 2 : 0;
}

static size_t ga_hash(int dim, int cx, int cz)
{
    uint64_t h = (uint64_t)(uint32_t)cx * 0x9E3779B97F4A7C15ULL ^ (uint64_t)(uint32_t)cz * 0xC2B2AE3D27D4EB4FULL ^
                 (uint64_t)(dim + 1) * 0x165667B19E3779F9ULL;
    return (size_t)(h ^ h >> 29);
}

static struct ga_entry *ga_find(const struct genahead *g, int dim, int cx, int cz)
{
    if (g->used == 0) return NULL;
    for (size_t i = ga_hash(dim, cx, cz) & (g->cap - 1);; i = (i + 1) & (g->cap - 1))
    {
        struct ga_entry *e = &g->t[i];

        if (e->status == GA_EMPTY) return NULL;
        if (e->cx == cx && e->cz == cz && e->dim == dim) return e;
    }
}

static struct ga_entry *ga_slot(struct genahead *g, int dim, int cx, int cz);

static void ga_grow(struct genahead *g)
{
    struct ga_entry *old = g->t;
    size_t oldcap = g->cap;

    g->cap = oldcap ? oldcap * 2 : 1024;
    g->t = calloc(g->cap, sizeof *g->t);
    g->used = 0;
    for (size_t i = 0; i < oldcap; ++i)
        if (old[i].status != GA_EMPTY) *ga_slot(g, old[i].dim, old[i].cx, old[i].cz) = old[i];
    free(old);
}

/* The entry of (dim, cx, cz), made (status EMPTY, no state) when absent.
 * Outside the tick only: it may grow the table. */
static struct ga_entry *ga_slot(struct genahead *g, int dim, int cx, int cz)
{
    if ((g->used + 1) * 2 > g->cap) ga_grow(g);
    for (size_t i = ga_hash(dim, cx, cz) & (g->cap - 1);; i = (i + 1) & (g->cap - 1))
    {
        struct ga_entry *e = &g->t[i];

        if (e->status == GA_EMPTY)
        {
            memset(e, 0, sizeof *e);
            e->cx = cx;
            e->cz = cz;
            e->dim = (int8_t)dim;
            e->state = -1;
            ++g->used;
            return e;
        }
        if (e->cx == cx && e->cz == cz && e->dim == dim) return e;
    }
}

/* Removal: the chunk (if any) is the caller's; the state goes back to the
 * free list. Linear-probe backward shift, so later probes still find their
 * chains; no allocation (the tick removes what it takes). */
static void ga_remove(struct genahead *g, struct ga_entry *e)
{
    size_t i = (size_t)(e - g->t);

    if (e->state >= 0)
    {
        g->sfree[g->nsfree++] = e->state;
        g->bytes -= sizeof(struct ga_state);
    }
    g->t[i].status = GA_EMPTY;
    --g->used;
    for (size_t j = (i + 1) & (g->cap - 1); g->t[j].status != GA_EMPTY; j = (j + 1) & (g->cap - 1))
    {
        size_t home = ga_hash(g->t[j].dim, g->t[j].cx, g->t[j].cz) & (g->cap - 1);

        /* j's entry may move to i when i lies cyclically in [home, j) */
        if ((j > i && (home <= i || home > j)) || (j < i && home <= i && home > j))
        {
            g->t[i] = g->t[j];
            g->t[j].status = GA_EMPTY;
            i = j;
        }
    }
}

static int32_t ga_state_new(struct genahead *g)
{
    if (g->nsfree == 0)
    {
        int n = g->nstates ? g->nstates * 2 : 256;

        g->states = realloc(g->states, sizeof *g->states * (size_t)n);
        g->sfree = realloc(g->sfree, sizeof *g->sfree * (size_t)n);
        for (int i = n - 1; i >= g->nstates; --i) g->sfree[g->nsfree++] = i;
        g->nstates = n;
    }
    g->bytes += sizeof(struct ga_state);
    return g->sfree[--g->nsfree];
}

/* ------------------------------------------------------------ the backend */

static void ga_note_miss(struct genahead *g, int dim, int cx, int cz)
{
    int di = dim_index(dim);

    ++g->st.misses[di];
    if (g->misslog != NULL)
        fprintf(g->misslog, "gen-ahead miss: step %llu dim %d chunk %d %d, player dim %d chunk %d %d, phase %s\n",
                (unsigned long long)g->step, dim, cx, cz, g->pdim, g->pcx, g->pcz,
                nw_env->phase.open >= 0 && nw_env->phase.on ? PHASES[nw_env->phase.open].label : "?");
    if (dim != g->pdim)
    {
        ++g->st.miss_other_dim;
        return;
    }
    int d = abs(cx - g->pcx) > abs(cz - g->pcz) ? abs(cx - g->pcx) : abs(cz - g->pcz);
    int b = d < 5 ? 0 : d < 9 ? 1 : d < 10 ? 2 : d < 12 ? 3 : d < 14 ? 4 : d < 20 ? 5 : d < 29 ? 6 : 7;

    ++g->st.miss_ring[di][b];
}

static void ga_apply(struct world *w, const struct genahead *g, const struct ga_entry *e)
{
    const struct ga_state *s = e->state >= 0 ? &g->states[e->state] : NULL;

    world_gen_state(w, s ? s->biome : NULL, s ? s->tops : NULL, e->rand);
}

static void ga_construct(struct genahead *g, struct ga_entry *e, const uint8_t *ids, const uint8_t *metas);
static void ga_construct_built(struct genahead *g, struct ga_entry *e, const struct ga_built *b, const uint8_t *bands);

/* a raw entry's chunk, constructed now; its result's reference released */
static void ga_build(struct genahead *g, struct ga_entry *e)
{
    if (e->raw_built) ga_construct_built(g, e, (const struct ga_built *)(const void *)e->raw_ids, e->raw_metas);
    else ga_construct(g, e, e->raw_ids, e->raw_metas);
    g->release(e->raw_owner);
    e->raw_ids = e->raw_metas = NULL;
    e->raw_owner = NULL;
    e->raw_built = 0;
}

static void ga_drop_raw(struct genahead *g, struct ga_entry *e)
{
    if (e->raw_owner == NULL) return;
    g->release(e->raw_owner);
    e->raw_ids = e->raw_metas = NULL;
    e->raw_owner = NULL;
    e->raw_built = 0;
}

static struct chunk *ga_take(struct world *w, int cx, int cz)
{
    struct genahead *g = w->backend_ctx;
    struct ga_entry *e = ga_find(g, w->dim, cx, cz);

    if (e == NULL || e->status != GA_READY || (e->c == NULL && e->raw_ids == NULL))
    {
        ga_note_miss(g, w->dim, cx, cz);
        return NULL;
    }
    if (e->c == NULL) ga_build(g, e);

    struct chunk *c = e->c;

    ga_apply(w, g, e);
    ++g->st.hits[dim_index(w->dim)];
    g->bytes -= chunk_bytes_resident(c);
    --g->ready;
    ga_remove(g, e);
    return c;
}

static int ga_owed(struct world *w, int cx, int cz)
{
    struct genahead *g = w->backend_ctx;
    struct ga_entry *e = ga_find(g, w->dim, cx, cz);

    if (e == NULL || e->status != GA_READY)
    {
        ++g->st.owed_misses;
        return 0;
    }
    ga_apply(w, g, e);
    ++g->st.owed_hits;
    if (e->owed_only) ga_remove(g, e);
    return 1;
}

static void ga_generate(struct world *w, int cx, int cz, struct chunk *c)
{
    chunk_backend_c.generate(w, cx, cz, c);
}

static const struct chunk_backend genahead_backend = {"genahead", ga_generate, ga_take, ga_owed};

/* ------------------------------------------------------------ the step */

struct genahead *genahead_create(void)
{
    return calloc(1, sizeof(struct genahead));
}

void genahead_set_ahead(struct genahead *g, int rings)
{
    g->ahead = rings > 0 ? rings : 0;
}

void genahead_free(struct genahead *g)
{
    if (g == NULL) return;
    for (size_t i = 0; i < g->cap; ++i)
        if (g->t[i].status != GA_EMPTY) chunk_free(g->t[i].c);
    free(g->t);
    free(g->states);
    free(g->sfree);
    free(g->pend);
    free(g->reqs);
    free(g);
}

void genahead_hint(struct genahead *g, double x, double z)
{
    if (g->nhint < 8)
    {
        g->hint[g->nhint][0] = x;
        g->hint[g->nhint][1] = z;
        ++g->nhint;
    }
}

void genahead_log_misses(struct genahead *g, FILE *f)
{
    g->misslog = f;
}

const struct ga_stats *genahead_stats(const struct genahead *g)
{
    return &g->st;
}

static struct world *sr_world_of(struct serverreplay *sr, int dim)
{
    struct world *w = dim == 0 ? &sr->pop.world : dim == -1 ? &sr->hell.world : &sr->sky.world;

    return w->slot != NULL ? w : NULL;
}

static void ga_attach(struct genahead *g, struct serverreplay *sr)
{
    for (int dim = -1; dim <= 1; ++dim)
    {
        struct world *w = sr_world_of(sr, dim);

        if (w == NULL) continue;
        w->backend = &genahead_backend;
        w->backend_ctx = g;
    }
}

/* A chunk the region store (or a checkpoint's region files not read yet)
 * holds: its load takes it from there, no generation. */
static int sr_stored(struct serverreplay *sr, int dim, int cx, int cz)
{
    struct sr_dimstate *d = &sr->dims[dim_index(dim)];

    if (d->saved_init && d->saved.slot != NULL && world_chunk(&d->saved, cx, cz) != NULL) return 1;
    if (rspill_has(dim_index(dim), cx, cz)) return 1;   /* in the store's spill file */
    for (int i = 0; i < d->nlazy; ++i)
        if (d->lazy[i * 3] == cx && d->lazy[i * 3 + 1] == cz) return 1;
    return 0;
}

static void ga_pend(struct genahead *g, int dim, int cx, int cz)
{
    if (g->npend + 3 > g->cappend)
    {
        g->cappend = g->cappend ? g->cappend * 2 : 3072;
        g->pend = realloc(g->pend, g->cappend * sizeof *g->pend);
    }
    g->pend[g->npend++] = dim;
    g->pend[g->npend++] = cx;
    g->pend[g->npend++] = cz;
}

static void ga_want(struct genahead *g, struct serverreplay *sr, struct world *w, int dim, int cx, int cz)
{
    ++g->st.wants;
    if (world_chunk(w, cx, cz) != NULL) return;

    struct ga_entry *e = ga_find(g, dim, cx, cz);

    if (e != NULL)
    {
        e->mark = g->step;
        return;
    }
    int stored = sr_stored(sr, dim, cx, cz);

    e = ga_slot(g, dim, cx, cz);
    e->status = stored ? GA_STORED : GA_PENDING;
    e->mark = g->step;
    if (!stored) ga_pend(g, dim, cx, cz);
}

/* The chunks [x0, x1] x [z0, z1], less the player's square the prediction
 * already walked and what it looked at this step. pm: the player manager's
 * loads, which generate even in a world whose other reads do not
 * (no_generate: a v1 snapshot's pin); every other window only where reads
 * generate. */
static void ga_rect(struct genahead *g, struct serverreplay *sr, int dim, int x0, int x1, int z0, int z1, int pm)
{
    struct world *w = sr_world_of(sr, dim);

    if (w == NULL || (w->no_generate && !pm)) return;
    for (int x = x0; x <= x1; ++x)
        for (int z = z0; z <= z1; ++z)
        {
            if (dim == g->pdim)
            {
                int gx = x - g->pcx + GA_SEEN / 2, gz = z - g->pcz + GA_SEEN / 2;

                /* the player's square: jump over it */
                if (g->pr >= 0 && abs(x - g->pcx) <= g->pr && abs(z - g->pcz) <= g->pr)
                {
                    z = g->pcz + g->pr;
                    continue;
                }
                if (gx >= 0 && gx < GA_SEEN && gz >= 0 && gz < GA_SEEN)
                {
                    int b = gx * GA_SEEN + gz;
                    uint64_t bit = 1ULL << (b & 63);

                    if (g->seen[b >> 6] & bit) continue;
                    g->seen[b >> 6] |= bit;
                }
            }
            ga_want(g, sr, w, dim, x, z);
        }
}

static void ga_square(struct genahead *g, struct serverreplay *sr, int dim, int cx, int cz, int r, int pm)
{
    ga_rect(g, sr, dim, cx - r, cx + r, cz - r, cz + r, pm);
}

/* A window this step walked already (livings in one chunk share theirs). */
static int ga_window_seen(struct genahead *g, int dim, int x0, int z0)
{
    uint32_t h = ((uint32_t)x0 * 0x9E3779B1u ^ (uint32_t)z0 * 0x85EBCA77u ^ (uint32_t)(dim + 1) * 0xC2B2AE3Du) >> 24;

    for (int k = 0; k < 256; ++k, h = (h + 1) & 255)
    {
        int32_t *e = g->win[h];

        if (g->win_step[h] != g->step)
        {
            g->win_step[h] = g->step;
            e[0] = dim;
            e[1] = x0;
            e[2] = z0;
            return 0;
        }
        if (e[0] == dim && e[1] == x0 && e[2] == z0) return 1;
    }
    return 0;
}

/* The radius around the player of dim that the next tick may generate in:
 * the player manager's square one ring out (a move of up to a chunk), and
 * where reads generate, the spawner's 17x17 window one ring out (population's
 * neighbours). */
static int ga_radius(struct serverreplay *sr, int dim, int view)
{
    struct world *w = sr_world_of(sr, dim);

    if (w == NULL || w->no_generate) return view + 1;
    /* the Nether's hidden lava reads a block past its chunk's populate
     * area, so a load at the window's edge populates a neighbour, whose
     * lava loads the next one out: two more rings there */
    return (view + 1 < 9 ? 9 : view + 1) + (dim == -1 ? 2 : 0);
}

/* An End portal block within two blocks of (x, y, z), in loaded chunks only
 * (the prediction never loads). */
static int end_portal_near(struct world *w, double x, double y, double z)
{
    if (w == NULL) return 0;

    int bx = mh_floor(x), by = mh_floor(y), bz = mh_floor(z);

    for (int dx = -2; dx <= 2; ++dx)
        for (int dz = -2; dz <= 2; ++dz)
        {
            const struct chunk *c = world_chunk(w, (bx + dx) >> 4, (bz + dz) >> 4);

            if (c == NULL) continue;
            for (int dy = -2; dy <= 2; ++dy)
            {
                int yy = by + dy;

                if (yy < 0 || yy > 255) continue;
                if (chunk_cell_id(c, ((bx + dx) & 15) << 12 | ((bz + dz) & 15) << 8 | yy) == 119) return 1;
            }
        }
    return 0;
}

/* The chunks the next tick may generate. */
static void ga_predict(struct genahead *g, struct serverreplay *sr)
{
    struct server_player *p = sr->player;

    g->pr = -1;
    if (p == NULL) return;

    int pdim = serverreplay_player_dim(sr);
    double px = p->e.pos_x, pz = p->e.pos_z;
    int view = sr->view;

    for (int i = 0; i < sr->nworlds; ++i)
        if (sr->w[i].view > view) view = sr->w[i].view;

    g->pdim = pdim;
    g->pcx = mh_floor(px) >> 4;
    g->pcz = mh_floor(pz) >> 4;
    memset(g->seen, 0, sizeof g->seen);

    int r = ga_radius(sr, pdim, view) + g->ahead;

    ga_square(g, sr, pdim, g->pcx, g->pcz, r, 1);
    g->pr = r;

    /* the pathfinder's ChunkCache window, (int)(range + 8) blocks around the
     * living, for follow ranges up to 40 (the zombie's) plus a tick's travel */
    for (int di = 0; di < 3; ++di)
    {
        const struct sr_dimstate *d = &sr->dims[di];
        int dim = di == 0 ? 0 : di == 1 ? -1 : 1;

        if (!d->live) continue;
        for (int k = 0; k < d->anw.n; ++k)
        {
            const struct an_ent *a = an_ent_at(d->anw.slot[k]);

            if (a == NULL || !a->used || !a->is_living) continue;

            const struct living *lv = lv_get(a->livh);

            if (lv == NULL) continue;

            int x = mh_floor(lv->e.pos_x), z = mh_floor(lv->e.pos_z);
            int pw = GA_PATH + 16 * g->ahead;
            int x0 = (x - pw) >> 4, x1 = (x + pw) >> 4, z0 = (z - pw) >> 4, z1 = (z + pw) >> 4;

            /* a living in a Nether portal travels at its next update (no
             * portal time but its cooldown): the Teleporter's 128-block
             * search around its destination */
            if (lv->in_portal && dim == 0)
                ga_square(g, sr, -1, mh_floor(lv->e.pos_x / 8.0) >> 4, mh_floor(lv->e.pos_z / 8.0) >> 4, 9, 0);
            else if (lv->in_portal && dim == -1)
                ga_square(g, sr, 0, mh_floor(lv->e.pos_x * 8.0) >> 4, mh_floor(lv->e.pos_z * 8.0) >> 4, 9, 0);

            /* inside the player's square, or walked already: nothing new */
            if (dim == pdim && x0 >= g->pcx - r && x1 <= g->pcx + r && z0 >= g->pcz - r && z1 <= g->pcz + r)
                continue;
            if (ga_window_seen(g, dim, x0, z0)) continue;
            ga_rect(g, sr, dim, x0, x1, z0, z1, 0);
        }
    }

    /* where the step's own moves put the player (genahead_hint) */
    for (int i = 0; i < g->nhint; ++i)
        ga_square(g, sr, pdim, mh_floor(g->hint[i][0]) >> 4, mh_floor(g->hint[i][1]) >> 4, r, 1);
    g->nhint = 0;

    /* the scheduled ticks the next tick runs (WorldServer.tickUpdates: at
     * most 1000, only its own chunk loaded first): a liquid's flow search
     * reads up to five blocks out, so one near its chunk's edge reaches the
     * neighbour */
    for (int di = 0; di < 3; ++di)
    {
        struct sr_dimstate *d = &sr->dims[di];
        int dim = di == 0 ? 0 : di == 1 ? -1 : 1;
        struct world *w = sr_world_of(sr, dim);
        const struct ticks_set *ts = &d->ticks;

        const struct tick_entry *first = ticks_set_first(ts);

        if (!d->live || w == NULL || w->no_generate || first == NULL || first->time > ts->total_time + 1) continue;

        struct ticks_set *cur = nw_env->ticks.cur;

        ticks_use(&d->ticks);

        int n = ticks_peek(ticks_set_count(ts) < 1000 ? ticks_set_count(ts) : 1000, g->due);

        g->st.due_peeked += (uint64_t)n;

        nw_env->ticks.cur = cur;
        for (int i = 0; i < n && g->due[i].time <= ts->total_time + 1; ++i)
        {
            const struct tick_entry *e = &g->due[i];
            int bx = e->x & 15, bz = e->z & 15;
            int dx0 = bx < 6 ? -1 : 0, dx1 = bx > 9 ? 1 : 0, dz0 = bz < 6 ? -1 : 0, dz1 = bz > 9 ? 1 : 0;

            for (int dx = dx0; dx <= dx1; ++dx)
                for (int dz = dz0; dz <= dz1; ++dz)
                    if (dx || dz) ga_want(g, sr, w, dim, (e->x >> 4) + dx, (e->z >> 4) + dz);
        }
    }

    /* the dragon: its moves and block breaking reach around it */
    if (sr->end != NULL && sr->end->dragon_listed && !sr->end->d.dead)
    {
        const struct dragon_state *d = &sr->end->d;

        ga_square(g, sr, 1, mh_floor(d->x) >> 4, mh_floor(d->z) >> 4, 5, 0);
    }

    /* a Nether portal's destination (Entity.portalCounter runs for 80 ticks
     * first): the player manager and the spawner around the arrival, two
     * chunks wider for the Teleporter's 128-block search moving it */
    if (p->portal.in_portal || p->portal.portal_counter > 0)
    {
        if (pdim == 0)
            ga_square(g, sr, -1, mh_floor(px / 8.0) >> 4, mh_floor(pz / 8.0) >> 4, ga_radius(sr, -1, view) + 2, 1);
        else if (pdim == -1)
            ga_square(g, sr, 0, mh_floor(px * 8.0) >> 4, mh_floor(pz * 8.0) >> 4, ga_radius(sr, 0, view) + 2, 1);
    }

    /* a dead player respawns (the game-over screen's C16) in the overworld,
     * at the world spawn give or take ten blocks, or at its bed
     * (survival.c's respawn): the player manager's square there, a ring
     * wider for the fuzz */
    if (p->sv.health <= 0.0F && sr->d != NULL)
    {
        int r0 = ga_radius(sr, 0, view) + 1;

        ga_square(g, sr, 0, sr->d->spawner.spawn_x >> 4, sr->d->spawner.spawn_z >> 4, r0, 1);
        if (p->sv.has_spawn) ga_square(g, sr, 0, p->sv.spawn_x >> 4, p->sv.spawn_z >> 4, r0, 1);
    }

    /* an End portal next to the player (BlockEndPortal sends it at once):
     * the End around its entrance, WorldProviderEnd's (100, 50, 0) */
    if (pdim == 0 && end_portal_near(sr_world_of(sr, 0), p->e.pos_x, p->e.pos_y, p->e.pos_z))
    {
        ga_square(g, sr, 1, 100 >> 4, 0 >> 4, ga_radius(sr, 1, view), 1);
        /* and the End's chunk at the player's own coordinates, which the
         * transfer loads before it moves the player */
        ga_square(g, sr, 1, g->pcx, g->pcz, 0, 1);
    }
}

/* The overworld's owed generations (world.c pay_owed), once a generation in
 * the overworld is coming: the state each leaves. */
static void ga_predict_owed(struct genahead *g, struct serverreplay *sr)
{
    struct world *w = sr_world_of(sr, 0);

    if (w == NULL || !w->gen.owed || w->nowed == 0) return;

    int coming = 0;

    for (size_t j = 0; j < g->npend && !coming; j += 3) coming = g->pend[j] == 0;
    for (size_t i = 0; i < g->cap && !coming; ++i)
        coming = g->t[i].status == GA_READY && g->t[i].dim == 0 && !g->t[i].owed_only;
    if (!coming) return;
    for (size_t k = 0; k < w->nowed; ++k)
    {
        int cx = (int)(uint32_t)w->owed[k], cz = (int)(w->owed[k] >> 32);
        struct ga_entry *e = ga_find(g, 0, cx, cz);

        if (e != NULL)
        {
            e->mark = g->step;
            continue;
        }
        e = ga_slot(g, 0, cx, cz);
        e->status = GA_PENDING;
        e->owed_only = 1;
        e->mark = g->step;
        ga_pend(g, 0, cx, cz);
    }
}

/* Drop what the prediction has not named for GA_KEEP steps (each removal
 * may shift the table, so the keys are listed first). */
static void ga_sweep(struct genahead *g)
{
    int32_t *old = NULL;
    size_t n = 0, cap = 0;

    for (size_t i = 0; i < g->cap; ++i)
    {
        struct ga_entry *e = &g->t[i];

        if (e->status == GA_EMPTY || e->mark + GA_KEEP >= g->step) continue;
        if (n + 3 > cap)
        {
            cap = cap ? cap * 2 : 768;
            old = realloc(old, cap * sizeof *old);
        }
        old[n++] = e->dim;
        old[n++] = e->cx;
        old[n++] = e->cz;
    }
    for (size_t j = 0; j < n; j += 3)
    {
        struct ga_entry *e = ga_find(g, old[j], old[j + 1], old[j + 2]);

        if (e->c != NULL)
        {
            ++g->st.wasted[dim_index(e->dim)];
            g->bytes -= chunk_bytes_resident(e->c);
            --g->ready;
            chunk_free(e->c);
        }
        else if (e->raw_owner != NULL)
        {
            ++g->st.wasted[dim_index(e->dim)];
            --g->ready;
            ga_drop_raw(g, e);
        }
        ga_remove(g, e);
    }
    free(old);
}

/* a result with no metadata (genahead_fill_raw's metas NULL) */
static const uint8_t ga_zero_metas[GA_CELLS];

/* e's chunk from a result (the Chunk constructor the C provider runs) */
static void ga_construct(struct genahead *g, struct ga_entry *e, const uint8_t *ids, const uint8_t *metas)
{
    struct chunk *c = chunk_new();

    if (metas == NULL) metas = ga_zero_metas;

    if (e->dim == 0)
    {
        int b[256];

        for (int i = 0; i < GA_CELLS; ++i) g->ids16[i] = ids[i];
        for (int i = 0; i < 256; ++i) b[i] = g->states[e->state].biome[i];
        chunkgen_construct(c, e->cx, e->cz, g->ids16, metas, b);
    }
    else
    {
        for (int i = 0; i < GA_DIM_CELLS; ++i) g->ids16[i] = ids[i];
        world_dim_construct(c, e->cx, e->cz, e->dim, g->ids16);
    }
    c->cx = e->cx;
    c->cz = e->cz;
    e->c = c;
    g->bytes += chunk_bytes_resident(c);
}

void genahead_chunk_built(struct chunk *c, int cx, int cz, const struct ga_built *b, const uint8_t *bands,
                          const uint8_t *biome)
{
    /* chunk_construct: the bands in the mask with their ids, their metadata
     * (the constructor's writes take the band's own array when a byte is
     * not 0), every band's random-tick count */
    chunk_clear(c);
    c->cx = cx;
    c->cz = cz;
    c->queued_light_checks = 4096;
    memcpy(c->sections_ticking, b->ticking, sizeof c->sections_ticking);
    for (int s = 0; s < 16; ++s)
    {
        if (!(b->mask >> s & 1)) continue;

        const uint8_t *band = bands + (size_t)s * GA_BAND_BYTES;
        struct chunk_sec *sec = chunk_sec_make(c, s);

        c->mask |= (uint16_t)(1 << s);
        memcpy(sec->ids, band, SEC_CELLS);
        if (b->metas_any >> s & 1)
        {
            chunk_sec_nib_expand(sec, NIB_METAS);
            memcpy((uint8_t *)sec + sec->nib[NIB_METAS], band + SEC_CELLS, SEC_NIB_BYTES);
        }
    }
    /* chunkgen_construct's biome copy, then generate_skylight_map: the
     * height map, heightMapMinimum, the precipitation map's reset and the
     * sky arrays (a band's one value its constant, as the fill's compaction
     * leaves it) */
    memcpy(c->biome, biome, sizeof c->biome);
    memcpy(c->height, b->height, sizeof c->height);
    c->height_min = b->height_min;
    for (int i = 0; i < 256; ++i) c->precip[i] = -999;
    for (int s = 0; s < 16; ++s)
        if (b->mask >> s & 1)
            chunk_sec_nib_store(chunk_sec_at(c, s), NIB_SKY, bands + (size_t)s * GA_BAND_BYTES + SEC_CELLS + SEC_NIB_BYTES);
}

/* e's chunk from a built result */
static void ga_construct_built(struct genahead *g, struct ga_entry *e, const struct ga_built *b, const uint8_t *bands)
{
    struct chunk *c = chunk_new();

    genahead_chunk_built(c, e->cx, e->cz, b, bands, g->states[e->state].biome);
    e->c = c;
    g->bytes += chunk_bytes_resident(c);
}

/* A result for a PENDING entry: its provider state, and its chunk
 * constructed now, or (owner not NULL) the result kept by reference until
 * the tick takes it (built: ids is its ga_built header, metas its bands). */
static void ga_fill_(struct genahead *g, int dim, int cx, int cz, const uint8_t *ids, const uint8_t *metas,
                     const uint8_t *biome, const uint8_t *tops, uint64_t rand, void *owner, int built)
{
    struct ga_entry *e = ga_find(g, dim, cx, cz);

    if (e == NULL || e->status != GA_PENDING)
    {
        if (owner != NULL) g->release(owner);
        return;
    }
    e->rand = rand;
    if (dim == 0)
    {
        e->state = ga_state_new(g);
        memcpy(g->states[e->state].biome, biome, 256);
        memcpy(g->states[e->state].tops, tops, 256);
    }
    e->status = GA_READY;
    if (e->owed_only)
    {
        ++g->st.owed_generated;
        if (owner != NULL) g->release(owner);
        return;
    }
    ++g->st.generated[dim_index(dim)];
    ++g->ready;
    if (owner != NULL)
    {
        e->raw_ids = ids;
        e->raw_metas = metas;
        e->raw_owner = owner;
        e->raw_built = (uint8_t)built;
    }
    else if (built) ga_construct_built(g, e, (const struct ga_built *)(const void *)ids, metas);
    else ga_construct(g, e, ids, metas);
}

static void ga_fill(struct genahead *g, int dim, int cx, int cz, const uint8_t *ids, const uint8_t *metas,
                    const uint8_t *biome, const uint8_t *tops, uint64_t rand)
{
    ga_fill_(g, dim, cx, cz, ids, metas, biome, tops, rand, NULL, 0);
}

/* A batch the generator dropped: its entries go, so the next step asks again. */
static void ga_unpend(struct genahead *g, int dim, int cx, int cz)
{
    struct ga_entry *e = ga_find(g, dim, cx, cz);

    if (e != NULL && e->status == GA_PENDING) ga_remove(g, e);
}

/* the step's prediction for one environment */
static void ga_predict_step(struct genahead *g, struct serverreplay *sr)
{
    ++g->step;
    ++g->st.steps;
    g->npend = 0;

    uint64_t i0 = phase_prof_instructions();

    ga_attach(g, sr);
    ga_predict(g, sr);
    ga_predict_owed(g, sr);
    if (g->step % GA_KEEP == 0) ga_sweep(g);
    g->st.ins_predict += phase_prof_instructions() - i0;
}

static void ga_peaks(struct genahead *g)
{
    if (g->ready > g->st.peak_entries) g->st.peak_entries = g->ready;
    if (g->bytes > g->st.peak_bytes) g->st.peak_bytes = g->bytes;
}

int genahead_predict(struct genahead *g, struct serverreplay *sr, const struct ga_req **req)
{
    ga_predict_step(g, sr);

    int n = (int)(g->npend / 3);

    if (n > g->capreq)
    {
        g->capreq = n;
        g->reqs = realloc(g->reqs, sizeof *g->reqs * (size_t)n);
    }
    for (int i = 0; i < n; ++i)
    {
        struct ga_req *r = &g->reqs[i];

        r->dim = g->pend[3 * i];
        r->seed = sr_world_of(sr, r->dim)->seed;
        r->cx = g->pend[3 * i + 1];
        r->cz = g->pend[3 * i + 2];
    }
    if (n > 0)
    {
        ++g->st.batches;
        if (n > g->st.max_batch_seen) g->st.max_batch_seen = n;
    }
    *req = g->reqs;
    return n;
}

void genahead_fill(struct genahead *g, const struct ga_req *r, const uint8_t *ids, const uint8_t *metas,
                   const uint8_t *biome, const uint8_t *tops, uint64_t rand)
{
    uint64_t i0 = phase_prof_instructions(), t0 = ga_now_ns();

    if (ids == NULL) ga_unpend(g, r->dim, r->cx, r->cz);
    else ga_fill(g, r->dim, r->cx, r->cz, ids, metas, biome, tops, rand);
    g->st.ins_construct += phase_prof_instructions() - i0;
    g->st.ns_construct += ga_now_ns() - t0;
    ga_peaks(g);
}

void genahead_fill_raw(struct genahead *g, const struct ga_req *r, const uint8_t *ids, const uint8_t *metas,
                       const uint8_t *biome, const uint8_t *tops, uint64_t rand, void *owner,
                       void (*release)(void *owner))
{
    uint64_t i0 = phase_prof_instructions(), t0 = ga_now_ns();

    g->release = release;
    ga_fill_(g, r->dim, r->cx, r->cz, ids, metas, biome, tops, rand, owner, 0);
    g->st.ins_construct += phase_prof_instructions() - i0;
    g->st.ns_construct += ga_now_ns() - t0;
    ga_peaks(g);
}

void genahead_fill_built(struct genahead *g, const struct ga_req *r, const struct ga_built *b, const uint8_t *bands,
                         const uint8_t *biome, const uint8_t *tops, uint64_t rand, void *owner,
                         void (*release)(void *owner))
{
    uint64_t i0 = phase_prof_instructions(), t0 = ga_now_ns();

    g->release = release;
    ga_fill_(g, r->dim, r->cx, r->cz, (const uint8_t *)(const void *)b, bands, biome, tops, rand, owner, 1);
    g->st.ins_construct += phase_prof_instructions() - i0;
    g->st.ns_construct += ga_now_ns() - t0;
    ga_peaks(g);
}

void genahead_release_raw(struct genahead *g)
{
    for (size_t i = 0; i < g->cap; ++i)
        if (g->t[i].status != GA_EMPTY) ga_drop_raw(g, &g->t[i]);
}

void genahead_detach(struct serverreplay *sr)
{
    for (int dim = -1; dim <= 1; ++dim)
    {
        struct world *w = sr_world_of(sr, dim);

        if (w == NULL || w->backend != &genahead_backend) continue;
        w->backend = NULL;
        w->backend_ctx = NULL;
    }
}

void genahead_step(struct genahead **gs, struct serverreplay **srs, int n, const struct ga_gen *gen)
{
    for (int k = 0; k < n; ++k) ga_predict_step(gs[k], srs[k]);

    /* one batch over every environment's new requests */
    size_t total = 0;

    for (int k = 0; k < n; ++k) total += gs[k]->npend / 3;
    if (total == 0) goto done;

    size_t B = gen != NULL ? (size_t)gen->max_batch : 1;
    struct ga_req *req = malloc(sizeof *req * total);
    int *owner = malloc(sizeof *owner * total);
    uint8_t *ids = malloc((size_t)GA_CELLS * B), *metas = malloc((size_t)GA_CELLS * B);
    uint8_t *biome = malloc(256 * B), *tops = malloc(256 * B);
    uint64_t *rand = malloc(sizeof *rand * B);
    size_t m = 0;

    for (int k = 0; k < n; ++k)
        for (size_t j = 0; j < gs[k]->npend; j += 3)
        {
            req[m].dim = gs[k]->pend[j];
            req[m].seed = sr_world_of(srs[k], req[m].dim)->seed;
            req[m].cx = gs[k]->pend[j + 1];
            req[m].cz = gs[k]->pend[j + 2];
            owner[m++] = k;
        }
    for (size_t a = 0; a < total; a += B)
    {
        int bn = (int)(total - a < B ? total - a : B);
        int ok;

        ++gs[owner[a]]->st.batches;
        if (bn > gs[owner[a]]->st.max_batch_seen) gs[owner[a]]->st.max_batch_seen = bn;
        uint64_t i0 = phase_prof_instructions(), t0 = ga_now_ns();

        ok = gen != NULL && gen->run(gen->ctx, req + a, bn, ids, metas, biome, tops, rand) == 0;

        uint64_t i1 = phase_prof_instructions(), t1 = ga_now_ns();

        gs[owner[a]]->st.ins_generate += i1 - i0;
        gs[owner[a]]->st.ns_generate += t1 - t0;
        for (int i = 0; i < bn; ++i)
        {
            const struct ga_req *r = &req[a + (size_t)i];
            struct genahead *g = gs[owner[a + (size_t)i]];

            if (!ok) ga_unpend(g, r->dim, r->cx, r->cz);
            else
                ga_fill(g, r->dim, r->cx, r->cz, ids + (size_t)i * GA_CELLS, metas + (size_t)i * GA_CELLS,
                        biome + (size_t)i * 256, tops + (size_t)i * 256, rand[i]);
        }
        gs[owner[a]]->st.ins_construct += phase_prof_instructions() - i1;
        gs[owner[a]]->st.ns_construct += ga_now_ns() - t1;
    }
    free(req);
    free(owner);
    free(ids);
    free(metas);
    free(biome);
    free(tops);
    free(rand);

done:
    for (int k = 0; k < n; ++k) ga_peaks(gs[k]);
}

void genahead_report(const struct genahead *g, const char *gen_name, FILE *f)
{
    const struct ga_stats *s = &g->st;
    uint64_t h = s->hits[0] + s->hits[1] + s->hits[2], m = s->misses[0] + s->misses[1] + s->misses[2];
    uint64_t made = s->generated[0] + s->generated[1] + s->generated[2];
    uint64_t waste = s->wasted[0] + s->wasted[1] + s->wasted[2];

    fprintf(f,
            "gen-ahead: %s hits %llu misses %llu hit rate %.1f%% (overworld %llu/%llu, nether %llu/%llu, end "
            "%llu/%llu); owed %llu/%llu; generated %llu (+%llu owed) wasted %llu; batches %llu of %llu steps, "
            "largest %d; cache peak %d chunks %.1f MB\n",
            gen_name, (unsigned long long)h, (unsigned long long)m, h + m ? 100.0 * (double)h / (double)(h + m) : 100.0,
            (unsigned long long)s->hits[0], (unsigned long long)(s->hits[0] + s->misses[0]),
            (unsigned long long)s->hits[1], (unsigned long long)(s->hits[1] + s->misses[1]),
            (unsigned long long)s->hits[2], (unsigned long long)(s->hits[2] + s->misses[2]),
            (unsigned long long)s->owed_hits, (unsigned long long)(s->owed_hits + s->owed_misses),
            (unsigned long long)made, (unsigned long long)s->owed_generated, (unsigned long long)waste,
            (unsigned long long)s->batches, (unsigned long long)s->steps, s->max_batch_seen, s->peak_entries,
            (double)s->peak_bytes / 1048576.0);
    fprintf(f, "gen-ahead wall: generator calls %.2f s, construction %.2f s\n", (double)s->ns_generate / 1e9,
            (double)s->ns_construct / 1e9);
    if (s->ins_predict + s->ins_generate + s->ins_construct > 0)
        fprintf(f, "gen-ahead host instructions: predict %.1f M (%llu chunks looked at, %llu due ticks read), "
                "generator call %.1f M, construct %.1f M (%.2f M per chunk)\n",
                (double)s->ins_predict / 1e6, (unsigned long long)s->wants, (unsigned long long)s->due_peeked,
                (double)s->ins_generate / 1e6, (double)s->ins_construct / 1e6,
                made ? (double)s->ins_construct / 1e6 / (double)made : 0.0);
    if (m > 0)
    {
        static const char *const ring[8] = {"0-4", "5-8", "9", "10-11", "12-13", "14-19", "20-28", "29+"};

        fprintf(f, "gen-ahead misses by distance from the player's chunk:");
        for (int di = 0; di < 3; ++di)
            for (int b = 0; b < 8; ++b)
                if (s->miss_ring[di][b])
                    fprintf(f, " %s %s:%llu", di == 0 ? "ow" : di == 1 ? "ne" : "end", ring[b],
                            (unsigned long long)s->miss_ring[di][b]);
        if (s->miss_other_dim) fprintf(f, " other-dim:%llu", (unsigned long long)s->miss_other_dim);
        fprintf(f, "\n");
    }
}

/* ------------------------------------------------------------ the C generator */

#define GA_C_SEEDS 4

struct ga_gen_c {
    int n;
    int64_t seed[GA_C_SEEDS];
    struct world *w[GA_C_SEEDS];   /* shadow providers, one per seed */
    uint16_t ids[GA_CELLS];
    uint8_t metas[GA_CELLS];
    int biomes[256];
};

static struct world *ga_c_world(struct ga_gen_c *c, int64_t seed)
{
    for (int i = 0; i < c->n; ++i)
        if (c->seed[i] == seed) return c->w[i];

    int i = c->n < GA_C_SEEDS ? c->n++ : GA_C_SEEDS - 1;

    if (c->w[i] != NULL)
    {
        world_free(c->w[i]);
        free(c->w[i]);
    }
    c->w[i] = malloc(sizeof *c->w[i]);
    world_init(c->w[i], seed);
    c->seed[i] = seed;
    return c->w[i];
}

static int ga_c_run(void *ctx, const struct ga_req *req, int n, uint8_t *ids, uint8_t *metas, uint8_t *biome,
                    uint8_t *tops, uint64_t *rand)
{
    struct ga_gen_c *c = ctx;

    for (int k = 0; k < n; ++k)
    {
        struct world *w = ga_c_world(c, req[k].seed);
        uint8_t *o = ids + (size_t)k * GA_CELLS;

        if (req[k].dim == 0)
        {
            chunkgen_raw(world_gen(w), req[k].cx, req[k].cz, c->ids, c->metas, c->biomes);
            /* the Chunk constructor keeps metadata only under a block */
            for (int i = 0; i < GA_CELLS; ++i)
            {
                o[i] = (uint8_t)c->ids[i];
                metas[(size_t)k * GA_CELLS + (size_t)i] = c->ids[i] ? c->metas[i] : 0;
            }
            for (int i = 0; i < 256; ++i)
            {
                biome[(size_t)k * 256 + (size_t)i] = (uint8_t)c->biomes[i];
                tops[(size_t)k * 256 + (size_t)i] = (uint8_t)w->gen.surface.top[i];
            }
            rand[k] = w->gen.terrain.rand.seed;
        }
        else
        {
            world_dim_raw(w, req[k].cx, req[k].cz, req[k].dim, c->ids);
            for (int i = 0; i < GA_DIM_CELLS; ++i) o[i] = (uint8_t)c->ids[i];
            rand[k] = req[k].dim == -1 ? w->nether.rand.seed : w->end.rand.seed;
        }
    }
    return 0;
}

static void ga_c_free(void *ctx)
{
    struct ga_gen_c *c = ctx;

    for (int i = 0; i < c->n; ++i)
    {
        world_free(c->w[i]);
        free(c->w[i]);
    }
    free(c);
}

static struct ga_gen *ga_c_another(const struct ga_gen *g)
{
    (void)g;
    return ga_gen_c_create();
}

struct ga_gen *ga_gen_c_create(void)
{
    struct ga_gen *g = calloc(1, sizeof *g);

    g->name = "c";
    g->ctx = calloc(1, sizeof(struct ga_gen_c));
    g->max_batch = 64;
    g->run = ga_c_run;
    g->free = ga_c_free;
    g->another = ga_c_another;
    return g;
}

void ga_gen_free(struct ga_gen *g)
{
    if (g == NULL) return;
    if (g->free) g->free(g->ctx);
    free(g);
}
