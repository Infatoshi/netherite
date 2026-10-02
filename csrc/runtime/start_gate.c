/* The saved start's gate (lane/savestart): an env reset to a start saved
 * from another env (pipeline.h pipe_start_from_env) continues exactly as
 * that env does, its rows and its frames, for the same acts.
 *
 *   start_gate [--config FILE] [--ticks n] [--device-mesh M] [--draw]
 *              [-j THREADS] [--snapdir DIR] CASE...
 *
 * CASE is NAME@S+M (the recording NAME under --snapdir, its tape's rows the
 * acts, as pipe_gate steps them: n rows a step) or walk:SEED@S+M (the
 * config's pipeline.start under pipe_bench's seeded walk, policy 2, from
 * seed SEED: forward, sprint, jumps, attacks, uses, look deltas, hotbar
 * keys). Each case runs on four envs:
 *
 *   A  reset to the case's start, S steps, saved (start X), M steps more
 *   B  reset to X, the same M steps
 *   C  reset to X, M/2 steps, saved (start Y, a save of a saved start), the
 *      rest of the M steps
 *   D  reset to Y, the same rest
 *
 * and B, C and D must give A's rows and frames: every tick's row record
 * (tests/rowrec.h: every field test_snapshots holds to the oracle's row),
 * every step's result (the HUD, the player, the events, the flags) and every
 * step's frame (the device's, copied back; with --draw the view's C frame
 * too), each folded into a digest per tick or step and compared one by one
 * (the first difference named, a row by its first differing field). The
 * save point's state is printed (entities, the open screen) and each saved
 * start's size (pool_start_info).
 *
 * One line per case and env, a summary; exit 0 when every case is equal. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../engine/dev.h"
#include "../engine/env.h"
#include "../engine/image.h"
#include "../engine/worldconf.h"
#include "../tests/rowrec.h"
#include "pipeline.h"
#include "view.h"

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static uint64_t fold(uint64_t h, const void *p, size_t n)
{
    const unsigned char *b = p;
    for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 0x100000001b3ull;
    return h;
}

/* pipe_bench's walk (policy 2) */
static uint64_t rnd(uint64_t *s)
{
    *s ^= *s << 13;
    *s ^= *s >> 7;
    *s ^= *s << 17;
    return *s;
}

static void walk_act(struct pool_act *a, uint64_t *s)
{
    memset(a, 0, sizeof *a);
    a->kind = PA_AGENT;
    a->hotbar = -1;
    uint64_t r = rnd(s);
    if ((r & 7) < 6) a->hold |= 1u << PK_FORWARD;
    if ((r >> 3 & 3) == 0) a->hold |= 1u << PK_SPRINT;
    if ((r >> 5 & 7) == 0) a->hold |= 1u << PK_JUMP;
    if ((r >> 8 & 3) == 0) a->hold |= 1u << PK_ATTACK;
    if ((r >> 10 & 31) == 0) a->press[PK_USE] = 1;
    a->look_mode = PL_DLOOK;
    a->look[0] = ((float)(r >> 16 & 255) - 127.5F) * 0.1F;
    a->look[1] = ((float)(r >> 24 & 63) - 31.5F) * 0.05F;
    if ((r >> 32 & 63) == 0) a->hotbar = (int)(r >> 40 & 7);
}

static char **read_lines(const char *path, int64_t *n)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    size_t cap = 1024;
    char **v = malloc(cap * sizeof *v);
    char *line = NULL;
    size_t lcap = 0;
    ssize_t len;
    *n = 0;
    while ((len = getline(&line, &lcap, f)) > 0)
    {
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
        if ((size_t)*n == cap) v = realloc(v, (cap *= 2) * sizeof *v);
        v[(*n)++] = strdup(line);
    }
    free(line);
    fclose(f);
    return v;
}

/* ----------------------------------------------------------- the records */

/* what one env did over the compared steps: each tick's row record (its
 * text and digest), each step's result and frame digests */
struct track {
    int on;
    int64_t t0;                 /* the first compared tick */
    int64_t nrows, caprows;
    char **rows;
    uint64_t *rowd;
    int nsteps;
    uint64_t *resd, *framed, *drawd;
    int64_t *step_t;            /* the tick each step ended at */
    uint64_t endd;              /* the end state's record (rowrec_end) after the last step */
};

static struct track *tracks;
static int TICKS;

static void track_begin(struct track *k, int64_t t0, int steps)
{
    for (int64_t i = 0; i < k->nrows; ++i) proc_free(k->rows[i]);
    free(k->rows);
    free(k->rowd);
    free(k->resd);
    free(k->framed);
    free(k->drawd);
    free(k->step_t);
    memset(k, 0, sizeof *k);
    k->t0 = t0;
    k->resd = calloc((size_t)steps + 1, sizeof *k->resd);
    k->framed = calloc((size_t)steps + 1, sizeof *k->framed);
    k->drawd = calloc((size_t)steps + 1, sizeof *k->drawd);
    k->step_t = calloc((size_t)steps + 1, sizeof *k->step_t);
    /* the hook runs with the env's heap on: its records go in memory made
     * here and by proc_malloc, the process's */
    k->caprows = (int64_t)(steps + 1) * TICKS;
    k->rows = calloc((size_t)k->caprows, sizeof *k->rows);
    k->rowd = calloc((size_t)k->caprows, sizeof *k->rowd);
    k->on = 1;
}

/* after each tick, on the worker */
static void gate_hook(void *ctx, int id, struct session *ss, int64_t t)
{
    (void)ctx;
    struct track *k = &tracks[id];
    if (!k->on) return;
    static _Thread_local char got[16384];
    size_t len = rowrec_line(ss, t, 1, got, sizeof got);
    if (len && got[len - 1] == '\n') got[--len] = 0;
    if (k->nrows == k->caprows) { k->on = 0; return; }
    k->rows[k->nrows] = proc_malloc(len + 1);
    memcpy(k->rows[k->nrows], got, len + 1);
    k->rowd[k->nrows] = fold(0xcbf29ce484222325ull, got, len);
    ++k->nrows;
}

static uint64_t fold_result(const struct pool_result *r)
{
    uint64_t h = 0xcbf29ce484222325ull;
    const struct pool_priv *v = &r->priv;
    int32_t iv[] = {r->flags, r->ticks, r->ops_refused, v->dim, v->food, v->xp_total, v->air, v->fire, v->on_ground};
    int64_t lv[] = {r->t, v->world_time, v->total_time};
    double dv[] = {v->x, v->y, v->z};
    float fv[] = {v->yaw, v->pitch, v->health, v->saturation, v->exhaustion};
    h = fold(h, iv, sizeof iv);
    h = fold(h, lv, sizeof lv);
    h = fold(h, dv, sizeof dv);
    h = fold(h, fv, sizeof fv);
    h = fold(h, &r->hud, sizeof r->hud);
    h = fold(h, &r->nevents, sizeof r->nevents);
    return fold(h, r->events, (size_t)r->nevents * sizeof r->events[0]);
}

static void row_diff(const char *want, const char *got, char *out, size_t n)
{
    char *a = strdup(want), *b = strdup(got);
    char *sa = NULL, *sb = NULL;
    char *ta = strtok_r(a, " ", &sa), *tb = strtok_r(b, " ", &sb);
    while (ta && tb && !strcmp(ta, tb))
    {
        ta = strtok_r(NULL, " ", &sa);
        tb = strtok_r(NULL, " ", &sb);
    }
    snprintf(out, n, "want %.300s got %.300s", ta ? ta : "(end)", tb ? tb : "(end)");
    free(a);
    free(b);
}

/* B (from step `from` of A's compared steps) against A: 0 equal, else 1
 * with the first difference in why */
static int compare(const struct track *a, const struct track *b, int from, char *why, size_t n)
{
    int64_t r0 = 0;
    if (from > 0) r0 = a->step_t[from - 1] - a->t0;
    if (b->t0 != a->t0 + r0) { snprintf(why, n, "starts at tick %lld, not %lld", (long long)b->t0, (long long)(a->t0 + r0)); return 1; }
    if (b->nrows != a->nrows - r0)
    {
        snprintf(why, n, "%lld rows, not %lld", (long long)b->nrows, (long long)(a->nrows - r0));
        return 1;
    }
    for (int64_t i = 0; i < b->nrows; ++i)
        if (b->rowd[i] != a->rowd[r0 + i])
        {
            char d[700];
            row_diff(a->rows[r0 + i], b->rows[i], d, sizeof d);
            snprintf(why, n, "row %lld: %s", (long long)(b->t0 + i), d);
            return 1;
        }
    if (b->endd != a->endd) { snprintf(why, n, "the end state (chunks' inhabited time, tile entities) differs"); return 1; }
    if (b->nsteps != a->nsteps - from) { snprintf(why, n, "%d steps, not %d", b->nsteps, a->nsteps - from); return 1; }
    for (int s = 0; s < b->nsteps; ++s)
    {
        if (b->resd[s] != a->resd[from + s]) { snprintf(why, n, "step %d (tick %lld): the result differs", from + s, (long long)a->step_t[from + s]); return 1; }
        if (b->framed[s] != a->framed[from + s]) { snprintf(why, n, "step %d (tick %lld): the device frame differs", from + s, (long long)a->step_t[from + s]); return 1; }
        if (b->drawd[s] != a->drawd[from + s]) { snprintf(why, n, "step %d (tick %lld): the C frame differs", from + s, (long long)a->step_t[from + s]); return 1; }
    }
    return 0;
}

/* -------------------------------------------------------------- the cases */

struct gcase {
    const char *arg;
    char dir[4096];
    int walk;
    uint64_t seed;
    int S, M;
    char **rows;                /* a recording's tape rows (by tick, as pipe_gate) */
    int64_t nrows;
};

static struct pipe *P;
static int W, H, DRAW;
static unsigned char *frame;
static const struct pipe_result **res;

/* the acts of step k of the case (k from the case's start) */
static int case_acts(struct gcase *c, int64_t tick, int k, struct pool_act *acts, int *nacts, int *per_tick)
{
    if (c->walk)
    {
        /* the walk's draws are the step's index's: replayable from any step */
        uint64_t s = c->seed * 0x9E3779B97F4A7C15ull + (uint64_t)k * 0xD1B54A32D192ED03ull + 1;
        walk_act(&acts[0], &s);
        *nacts = TICKS;
        *per_tick = 0;
        return 1;
    }
    int64_t left = c->nrows - tick;
    int kk = left < TICKS ? (int)left : TICKS;
    if (kk <= 0) return 0;
    for (int q = 0; q < kk; ++q)
    {
        memset(&acts[q], 0, sizeof acts[q]);
        acts[q].kind = PA_ROW;
        acts[q].row = c->rows[tick + q];
    }
    *nacts = kk;
    *per_tick = 1;
    return 1;
}

/* env id's steps k0..k1 of the case from tick t (into its track when on);
 * returns the tick after, -1 on a failure */
static int64_t run(struct gcase *c, int id, int64_t t, int k0, int k1)
{
    struct pool_act acts[64];
    for (int k = k0; k < k1; ++k)
    {
        int nacts, per_tick;
        if (!case_acts(c, t, k, acts, &nacts, &per_tick)) { printf("FAIL %s: the tape ends at tick %lld\n", c->arg, (long long)t); return -1; }
        if (pipe_step(P, &id, 1, acts, nacts, per_tick)) { printf("FAIL %s: env %d does not step\n", c->arg, id); return -1; }
        int got = pipe_poll(P, 1, res);
        if (got != 1) { printf("FAIL %s: env %d: no result\n", c->arg, id); return -1; }
        const struct pipe_result *pr = res[0];
        if (pr->r->flags & PF_ERROR) { printf("FAIL %s: env %d: %s\n", c->arg, id, pr->r->err); return -1; }
        t = pr->r->t;
        struct track *tr = &tracks[id];
        if (!tr->on) continue;
        tr->resd[tr->nsteps] = fold_result(pr->r);
        uint64_t fd = 0;
        if (pr->obs.data != NULL && pipe_download(P, &pr->obs, frame) == 0) fd = fold(0xcbf29ce484222325ull, frame, (size_t)W * H * 3);
        tr->framed[tr->nsteps] = fd;
        if (DRAW)
        {
            const unsigned char *live = view_drawn(pipe_views(P), id);
            tr->drawd[tr->nsteps] = live ? fold(0xcbf29ce484222325ull, live, (size_t)W * H * 3) : 0;
        }
        tr->step_t[tr->nsteps] = t;
        ++tr->nsteps;
    }
    struct track *tr = &tracks[id];
    if (tr->on)
    {
        /* the end state, read on this thread with the env's heap */
        struct pool *pl = pipe_pool(P);
        struct env *saved = nw_env;
        nw_env = pool_env(pl, id);
        int heap = image_heap_set(nw_env, 1);
        static char end[1 << 16];
        size_t len = rowrec_end(pool_session(pl, id), end, sizeof end);
        image_heap_set(nw_env, heap);
        nw_env = saved;
        tr->endd = fold(0xcbf29ce484222325ull, end, len);
    }
    return t;
}

static struct pool_start *save(struct gcase *c, int id, const char *name, double *ms)
{
    char err[1024];
    double t0 = now_s();
    struct pool_start *s = pipe_start_from_env(P, id, err, sizeof err);
    if (s == NULL || pipe_start_wait(P, s, err, sizeof err) != 0)
    {
        printf("FAIL %s: the save of env %d (%s): %s\n", c->arg, id, name, err);
        if (s) pool_start_free(s);
        return NULL;
    }
    *ms = (now_s() - t0) * 1e3;
    struct pool_start_info in;
    pool_start_info(s, &in);
    printf("   %s %s: tick %lld, %lld steps %lld ticks replayed in %.0f ms; image %.1f MB (%zu pointers, %zu into the view), "
           "view %.2f MB (%zu pointers), spill %.1f MB, log %.1f KB; words of each replay's own: %zu image, %zu view\n",
           c->arg, name, (long long)pool_start_tick(s), (long long)in.replay_steps, (long long)in.replay_ticks, *ms,
           (double)in.image_bytes / 1e6, in.pointers, in.lib_pointers, (double)in.view_bytes / 1e6, in.view_pointers,
           (double)in.spill_bytes / 1e6, (double)in.log_bytes / 1e3, in.outside + in.value_diffs, in.view_diffs);
    return s;
}

/* the save point's state, from env id's session (idle) */
static void describe(struct gcase *c, int id)
{
    struct pool *pl = pipe_pool(P);
    struct session *ss = pool_session(pl, id);
    struct env *saved = nw_env;
    nw_env = pool_env(pl, id);
    int heap = image_heap_set(nw_env, 1);
    static char line[16384];
    rowrec_line(ss, 0, 1, line, sizeof line);
    image_heap_set(nw_env, heap);
    nw_env = saved;
    const char *ents = strstr(line, " w.ents="), *gui = strstr(line, " cp.gui="), *win = strstr(line, " sp.win=");
    int ne = ents ? atoi(ents + 8) : -1;
    char g[64] = "-", w[64] = "-";
    if (gui) sscanf(gui + 8, "%63s", g);
    if (win) sscanf(win + 8, "%63s", w);
    printf("   %s save point: %d entities, client screen %s, server window %.40s\n", c->arg, ne, g, w);
}

static int run_case(struct gcase *c, struct pool_start *base)
{
    int A = 0, B = 1, C = 2, D = 3, ids[4] = {A, B, C, D};
    char err[1024], why[1200];
    for (int i = 0; i < 4; ++i) tracks[i].on = 0;
    if (pipe_reset(P, &ids[0], 1, base, err, sizeof err)) { printf("FAIL %s: reset: %s\n", c->arg, err); return 1; }
    int64_t t0 = pool_start_tick(base);
    int64_t t = run(c, A, t0, 0, c->S);
    if (t < 0) return 1;
    describe(c, A);
    double ms;
    struct pool_start *X = save(c, A, "X", &ms);
    if (X == NULL) return 1;
    int half = c->M / 2;
    track_begin(&tracks[A], t, c->M);
    if (run(c, A, t, c->S, c->S + c->M) < 0) return 1;
    tracks[A].on = 0;
    int bad = 0;
    /* B: X, the M steps */
    if (pipe_reset(P, &ids[1], 1, X, err, sizeof err)) { printf("FAIL %s: reset B: %s\n", c->arg, err); return 1; }
    track_begin(&tracks[B], t, c->M);
    if (run(c, B, t, c->S, c->S + c->M) < 0) return 1;
    tracks[B].on = 0;
    if (compare(&tracks[A], &tracks[B], 0, why, sizeof why)) { printf("FAIL %s B (reset to X): %s\n", c->arg, why); bad = 1; }
    else printf("OK   %s B (reset to X): %lld rows, %d steps, %d frames equal to A's\n", c->arg, (long long)tracks[B].nrows,
                tracks[B].nsteps, tracks[B].nsteps);
    /* C: X, M/2 steps, saved Y, the rest; D: Y, the rest */
    if (pipe_reset(P, &ids[2], 1, X, err, sizeof err)) { printf("FAIL %s: reset C: %s\n", c->arg, err); return 1; }
    int64_t tc = run(c, C, t, c->S, c->S + half);
    if (tc < 0) return 1;
    describe(c, C);
    struct pool_start *Y = save(c, C, "Y", &ms);
    if (Y == NULL) return 1;
    track_begin(&tracks[C], tc, c->M - half);
    if (run(c, C, tc, c->S + half, c->S + c->M) < 0) return 1;
    tracks[C].on = 0;
    if (compare(&tracks[A], &tracks[C], half, why, sizeof why)) { printf("FAIL %s C (X, saved again): %s\n", c->arg, why); bad = 1; }
    else printf("OK   %s C (reset to X, saved as Y): %lld rows, %d steps equal to A's\n", c->arg, (long long)tracks[C].nrows, tracks[C].nsteps);
    if (pipe_reset(P, &ids[3], 1, Y, err, sizeof err)) { printf("FAIL %s: reset D: %s\n", c->arg, err); return 1; }
    track_begin(&tracks[D], tc, c->M - half);
    if (run(c, D, tc, c->S + half, c->S + c->M) < 0) return 1;
    tracks[D].on = 0;
    if (compare(&tracks[A], &tracks[D], half, why, sizeof why)) { printf("FAIL %s D (reset to Y): %s\n", c->arg, why); bad = 1; }
    else printf("OK   %s D (reset to Y): %lld rows, %d steps equal to A's\n", c->arg, (long long)tracks[D].nrows, tracks[D].nsteps);
    /* A itself (a reused env) to Y: the same once more */
    if (pipe_reset(P, &ids[0], 1, Y, err, sizeof err)) { printf("FAIL %s: reset A: %s\n", c->arg, err); return 1; }
    track_begin(&tracks[A], tc, c->M - half);
    if (run(c, A, tc, c->S + half, c->S + c->M) < 0) return 1;
    tracks[A].on = 0;
    if (compare(&tracks[D], &tracks[A], 0, why, sizeof why)) { printf("FAIL %s A (reset to Y): %s\n", c->arg, why); bad = 1; }
    else printf("OK   %s A (reused, reset to Y): equal to D's\n", c->arg);
    pool_start_free(X);
    pool_start_free(Y);
    return bad;
}

int main(int argc, char **argv)
{
    int threads = 8, device_mesh = -1, no_view = 0;
    const char *snapdir = "out/java/snapshots", *config = NULL;
    int ticks = 0;
    const char **cases = calloc((size_t)argc, sizeof *cases);
    int ncases = 0;
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--config") && i + 1 < argc) config = argv[++i];
        else if (!strcmp(argv[i], "--ticks") && i + 1 < argc) ticks = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--device-mesh") && i + 1 < argc) device_mesh = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--draw")) DRAW = 1;
        else if (!strcmp(argv[i], "--no-view")) no_view = 1;
        else if (!strcmp(argv[i], "-j") && i + 1 < argc) threads = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--snapdir") && i + 1 < argc) snapdir = argv[++i];
        else if (argv[i][0] == '-') { fprintf(stderr, "start_gate: unknown flag %s\n", argv[i]); return 2; }
        else cases[ncases++] = argv[i];
    }
    if (ncases == 0)
    {
        fprintf(stderr, "usage: start_gate [--config FILE] [--ticks n] [--device-mesh M] [--draw] [-j THREADS] "
                        "[--snapdir DIR] NAME@S+M|walk:SEED@S+M...\n");
        return 2;
    }
    struct pipe_config cfg = {0};
    struct wconf wc;
    char err[1024];
    /* without a config: pipe_gate's observation (128x128, rd 4, 4 ticks,
     * no HUD), every start's world its own */
    cfg.pool.obs = (struct pool_obs){128, 128, 0, 4, 4, 0, 0.0F};
    if (config && pipe_config_file(&cfg, config, &wc, err, sizeof err)) { fprintf(stderr, "start_gate: %s\n", err); return 2; }
    if (ticks > 0) cfg.pool.obs.ticks = ticks;
    if (device_mesh >= 0) cfg.device_mesh = device_mesh;
    TICKS = cfg.pool.obs.ticks;
    W = cfg.pool.obs.w;
    H = cfg.pool.obs.h;
    if (TICKS > 64) { fprintf(stderr, "start_gate: at most 64 ticks a step\n"); return 2; }
    struct gcase *gc = calloc((size_t)ncases, sizeof *gc);
    for (int i = 0; i < ncases; ++i)
    {
        struct gcase *c = &gc[i];
        c->arg = cases[i];
        const char *at = strrchr(cases[i], '@');
        if (at == NULL || sscanf(at + 1, "%d+%d", &c->S, &c->M) != 2 || c->S < 0 || c->M < 2)
        {
            fprintf(stderr, "start_gate: %s: a case is NAME@S+M or walk:SEED@S+M (M at least 2)\n", cases[i]);
            return 2;
        }
        if (!strncmp(cases[i], "walk:", 5))
        {
            if (config == NULL) { fprintf(stderr, "start_gate: %s: a walk starts at the config's start (--config)\n", cases[i]); return 2; }
            c->walk = 1;
            c->seed = strtoull(cases[i] + 5, NULL, 10);
            wconf_start_path(&wc, c->dir, sizeof c->dir);
            continue;
        }
        snprintf(c->dir, sizeof c->dir, "%s/%.*s", snapdir, (int)(at - cases[i]), cases[i]);
        char path[4200];
        snprintf(path, sizeof path, "%s/tape.jsonl", c->dir);
        int64_t n;
        char **lines = read_lines(path, &n);
        if (lines == NULL || n < 2) { printf("FAIL %s: no tape at %s\n", c->arg, path); return 1; }
        c->rows = lines + 1;
        c->nrows = n - 1;
    }
    cfg.pool.n = 4;
    cfg.pool.threads = threads;
    cfg.pool.tick_hook = gate_hook;
    cfg.pool.privileged = 1;
    cfg.pool.dev_apply = dev_apply;
    cfg.pool.dev_end = dev_end;
    cfg.pool.save_slots = 2;
    cfg.draw = DRAW;
    cfg.no_view = no_view;
    tracks = calloc(4, sizeof *tracks);
    P = pipe_make(&cfg, err, sizeof err);
    if (P == NULL) { printf("FAIL pipe_make: %s\n", err); return 1; }
    frame = malloc((size_t)W * H * 3);
    res = calloc(4, sizeof *res);
    printf("start_gate: %s, %dx%d, %d ticks a step, device_mesh %d%s, %d cases\n", config ? config : "no config", W, H, TICKS, cfg.device_mesh,
           DRAW ? ", C frames drawn" : "", ncases);
    int failed = 0;
    double t0 = now_s();
    for (int i = 0; i < ncases; ++i)
    {
        struct gcase *c = &gc[i];
        double tc = now_s();
        struct pool_start *base = pipe_start_load(P, c->dir, err, sizeof err);
        if (base == NULL) { printf("FAIL %s: %s\n", c->arg, err); ++failed; continue; }
        int bad = run_case(c, base);
        pool_start_free(base);
        printf("%s %s (%.1f s)\n", bad ? "FAIL" : "PASS", c->arg, now_s() - tc);
        failed += bad;
    }
    printf("start_gate: %d of %d cases equal (%.1f s)\n", ncases - failed, ncases, now_s() - t0);
    pipe_free(P);
    return failed ? 1 : 0;
}
