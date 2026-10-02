/* The pipeline's gate: K recordings stepped through the whole pipeline
 * (pipeline.h: the env pool, each env's view, the device renderer over
 * several groups and streams), acts from their tapes, N envs, recordings
 * repeated and interleaved as pool_gate does. Every env's every row is held
 * to test_snapshots' own record of it (csrc/tests/rowrec.h, made by
 * test_snapshots --row-dump into REFDIR/NAME.rows), and every frame the
 * device drew to the C renderer's frame of the same environment and tick:
 * raster_obs_render over the same frame data in the env's own view (and
 * with --draw, the frame the view's C renderer drew live, as play draws
 * it), byte for byte.
 *
 *   pipe_gate [-n N] [-j THREADS] [--ticks n] [--episodes E] [--poll B]
 *             [--streams S] [--group G] [--batch B] [--depth D] [--draw]
 *             [--checkers C] [--play DIR] [--snapdir DIR] [--refdir DIR]
 *             [--device-mesh M] [--mesh-check] [--render-prec P] [--gen-device | --gen-serve] [--size WxH] [--rd R] NAME...
 *
 * --rd R: the frames drawn at render distance R (pool_obs rd, 2 to 16; 4
 * by default), the device's and the C renderer's alike; the rows keep the
 * tapes' own render distance (their opts), as tape-form acts do.
 *
 * --size WxH: the observation's size (pool_obs w, h; 128x128 by default,
 * SPEC.md's observation), the device's frames and the C renderer's alike;
 * with --play, play drew them at that size (pipe_play.sh -s WxH): a play
 * frame of another size fails.
 *
 * --render-prec P: the frames at that precision (engine/raster_prec.h: exact,
 * the default, fast or fast:STAGE,...; pool_obs.prec), the device and C
 * alike; with --play, play drew them so too (pipe_play.sh PREC).
 *
 * --play DIR: play's own frames too (DIR/NAME/o_TTTTTT.p1000.obs, play
 * --obs-dump with --shots at the frames' ticks, pipe_play.sh): each such
 * frame's C frame must be the gate's C frame at that tick (the view is
 * play's frame, not a copy of it).
 *
 * --device-mesh M: the sections are meshed on the device (pipeline.h
 * device_mesh). M 2: the views mesh them too, every device mesh must equal
 * the view's byte for byte (the fields the Tessellator wrote), and the C
 * renderer draws the views' meshes as before; M 1: the device's alone (the
 * product's path, with its counts fed back to the views), so the device's
 * frames are held to play's own (--play) and nothing else. --mesh-tab: the
 * device mesher in its table mode (cuda/meshing/table.h), held the same way.
 * --usual-trip: the renderer waits three times a render (pipeline.h
 * usual_trip; the default is one wait, lane/simclimb).
 *
 * --mesh-check: every stale section pass the views' mesh key spares (its
 * key unmoved, or all air: engine/raster_meshkey.h) is meshed anyway and
 * compared with the mesh kept for it (raster.h raster_live_mesh_check),
 * each must be equal (not with --device-mesh 1: the views hold no meshes).
 *
 * --device-light: the client world's light deferred to the device
 * (pipeline.h device_light), and at every frame the env's client light
 * digest (tests/cwdigest.h) held to test_snapshots' own at that row
 * (REFDIR/NAME.cwd, test_snapshots --cw-digest: the host running every
 * call as it comes), besides the rows and the frames.
 *
 * --gen-device: raw generation on the device (pipeline.h gen_device: every
 * stepping env's predicted generations in one round of worldgen).
 * --gen-serve: the same through the generation server (runtime/gensrv.h):
 * the pool is a client of this process's own server, over its socket and
 * shared mapping, as another process's pool would be. --gen-nowait: a step
 * never waits for its round (pool.h gen_nowait); --gen-ahead R: the
 * prediction R rings wider (pool.h gen_ahead).
 * --config FILE: the observation, the render distance and the device
 * switches from a config.yaml (pipeline.h pipe_config_file), every
 * recording held to its world; the flags override it.
 *
 * One line per recording (episodes, rows, frames, the first difference),
 * a summary; exit 0 when every row and every frame is equal. */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../engine/cwrand.h"
#include "../engine/dev.h"
#include "../engine/env.h"
#include "../engine/gencache.h"
#include "../engine/raster_obs.h"
#include "../engine/raster_prec.h"
#include "../engine/session.h"
#include "../engine/worldconf.h"
#include "../tests/cwdigest.h"
#include "../tests/rowrec.h"
#include "pipeline.h"
#include "view.h"

struct rec {
    const char *name;
    char dir[4096];
    char **rows;
    int64_t nrows;
    char **ref;
    int64_t nref;
    uint64_t *cwd;              /* --device-light: the client light digest by row (0: none) */
    char *ref_end;
    int64_t tick, cw_from;
    struct pool_start *start;
    int left;
    int episodes, bad;
    int64_t rows_checked, frames_checked, frames_play, lights_checked;
    char first_bad[1024];
};

struct genv {
    struct rec *r;
    int episode;
    int bad;
    int64_t checked;
    char why[1024];
};

static struct rec *recs;
static int nrecs;
static int device_mesh, device_light, mesh_check, prec, gen_device, mesh_tab, usual_trip, gen_serve, gen_nowait, gen_ahead;
static struct genv *genvs;
static const char *play_dir;

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
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

static void rec_diff(const char *want, const char *got, char *out, size_t n)
{
    char *a = strdup(want), *b = strdup(got);
    char *sa = NULL, *sb = NULL;
    char *ta = strtok_r(a, " ", &sa), *tb = strtok_r(b, " ", &sb);
    while (ta && tb && !strcmp(ta, tb))
    {
        ta = strtok_r(NULL, " ", &sa);
        tb = strtok_r(NULL, " ", &sb);
    }
    snprintf(out, n, "want %.400s got %.400s", ta ? ta : "(end)", tb ? tb : "(end)");
    free(a);
    free(b);
}

/* after each tick, on the worker: the env's row against the reference */
static void gate_hook(void *ctx, int id, struct session *ss, int64_t t)
{
    (void)ctx;
    struct genv *g = &genvs[id];
    struct rec *r = g->r;
    static _Thread_local char got[16384];
    rowrec_line(ss, t, r->cw_from >= 0 && t >= r->cw_from, got, sizeof got);
    size_t len = strlen(got);
    if (len && got[len - 1] == '\n') got[len - 1] = 0;
    ++g->checked;
    if (g->bad) return;
    const char *want = t < r->nref ? r->ref[t] : NULL;
    if (want == NULL)
    {
        g->bad = 1;
        snprintf(g->why, sizeof g->why, "row %lld: the reference has no such row", (long long)t);
        return;
    }
    if (strcmp(want, got))
    {
        char d[900];
        rec_diff(want, got, d, sizeof d);
        g->bad = 1;
        snprintf(g->why, sizeof g->why, "row %lld: %s", (long long)t, d);
    }
}

static int load_rec(struct rec *r, const char *snapdir, const char *refdir)
{
    if (r->name[0] == '/') snprintf(r->dir, sizeof r->dir, "%s", r->name);
    else snprintf(r->dir, sizeof r->dir, "%s/%s", snapdir, r->name);
    char path[4400];
    snprintf(path, sizeof path, "%s/tape.jsonl", r->dir);
    int64_t n;
    char **lines = read_lines(path, &n);
    if (lines == NULL || n < 1) { printf("FAIL %s: no tape\n", r->name); return 0; }
    r->rows = lines + 1;
    r->nrows = n - 1;
    const char *base = strrchr(r->name, '/') ? strrchr(r->name, '/') + 1 : r->name;
    snprintf(path, sizeof path, "%s/%s.rows", refdir, base);
    int64_t nref;
    char **ref = read_lines(path, &nref);
    if (ref == NULL || nref < 1) { printf("FAIL %s: no reference %s (test_snapshots --row-dump)\n", r->name, path); return 0; }
    int64_t maxt = -1;
    for (int64_t i = 0; i < nref; ++i)
        if (!strncmp(ref[i], "t=", 2)) { int64_t t = atoll(ref[i] + 2); if (t > maxt) maxt = t; }
        else if (!strncmp(ref[i], "end", 3)) r->ref_end = ref[i];
    r->nref = maxt + 1;
    r->ref = calloc((size_t)(r->nref > 0 ? r->nref : 1), sizeof *r->ref);
    r->tick = -1;
    for (int64_t i = 0; i < nref; ++i)
        if (!strncmp(ref[i], "t=", 2))
        {
            int64_t t = atoll(ref[i] + 2);
            r->ref[t] = ref[i];
            if (r->tick < 0 || t < r->tick) r->tick = t;
        }
    if (device_light)
    {
        snprintf(path, sizeof path, "%s/%s.cwd", refdir, base);
        int64_t nd;
        char **dl = read_lines(path, &nd);
        if (dl == NULL || nd < 1) { printf("FAIL %s: no client light reference %s (test_snapshots --cw-digest)\n", r->name, path); return 0; }
        r->cwd = calloc((size_t)(r->nref > 0 ? r->nref : 1), sizeof *r->cwd);
        for (int64_t i = 0; i < nd; ++i)
        {
            long long t;
            unsigned long long h;
            if (sscanf(dl[i], "%lld %llx", &t, &h) == 2 && t >= 0 && t < r->nref) r->cwd[t] = h;
            free(dl[i]);
        }
        free(dl);
    }
    snprintf(path, sizeof path, "%s/tape.jsonl", r->dir);
    uint64_t seed;
    int32_t lcg;
    cwrand_from_tape(path, r->tick, &seed, &lcg, &r->cw_from);
    return 1;
}

static int start_ready(struct rec *r, struct pool_start *st)
{
    if (pool_start_tick(st) != r->tick)
    {
        printf("FAIL %s: the start's tick %lld is not the reference's first row %lld\n", r->name,
               (long long)pool_start_tick(st), (long long)r->tick);
        return 0;
    }
    r->start = st;
    return 1;
}

/* ---------------------------------------------------------- frame checks */

struct check {
    int id;
    const struct pipe_result *res;
    int64_t tick;                   /* the tick the frame follows */
    int bad;
    char why[512];
    int played;                     /* play's frame was there and compared */
};

struct checkers {
    struct pipe *p;
    int w, h;
    pthread_mutex_t mu;
    pthread_cond_t go, done;
    struct check *jobs;
    int njobs, next, finished, gen, stop, draw;
    pthread_t *th;
    int nth;
};

static int diff_px(const unsigned char *a, const unsigned char *b, int w, int h, int *first)
{
    int n = 0;
    *first = -1;
    for (int i = 0; i < w * h; ++i)
        if (memcmp(a + i * 3, b + i * 3, 3))
        {
            if (*first < 0) *first = i;
            ++n;
        }
    return n;
}

static void check_one(struct checkers *c, struct check *k, unsigned char *dev, unsigned char *cref)
{
    const struct pool_result *r = k->res->r;
    const int w = c->w, h = c->h;
    if (pipe_download(c->p, &k->res->obs, dev) != 0) { k->bad = 1; snprintf(k->why, sizeof k->why, "no device frame"); return; }
    int first, nd;
    if (device_mesh == 1)
    {
        /* the views hold no meshes: the device's frame against play's */
        memcpy(cref, dev, (size_t)w * h * 3);
        goto play;
    }
    view_render(pipe_views(c->p), k->id, r->obs, cref);
    nd = diff_px(dev, cref, w, h, &first);
    if (nd)
    {
        k->bad = 1;
        snprintf(k->why, sizeof k->why, "frame after tick %lld: device differs from the C renderer at %d px (first %d,%d)",
                 (long long)k->tick, nd, first % w, first / w);
        return;
    }
    if (c->draw)
    {
        const unsigned char *live = view_drawn(pipe_views(c->p), k->id);
        nd = diff_px(live, cref, w, h, &first);
        if (nd)
        {
            k->bad = 1;
            snprintf(k->why, sizeof k->why, "frame after tick %lld: the view's live C frame differs from its frame data's at %d px (first %d,%d)",
                     (long long)k->tick, nd, first % w, first / w);
            return;
        }
    }
play:
    if (play_dir)
    {
        char path[4400];
        const char *base = strrchr(genvs[k->id].r->name, '/');
        base = base ? base + 1 : genvs[k->id].r->name;
        snprintf(path, sizeof path, "%s/%s/o_%06lld.p1000.obs", play_dir, base, (long long)k->tick);
        if (access(path, R_OK) == 0)
        {
            struct raster_obs po;
            unsigned char *prgb = NULL;
            if (raster_obs_read(path, &po, &prgb) != 0 || prgb == NULL)
            {
                k->bad = 1;
                snprintf(k->why, sizeof k->why, "frame after tick %lld: %.400s does not read", (long long)k->tick, path);
                return;
            }
            if (po.w != w || po.h != h)
            {
                k->bad = 1;
                snprintf(k->why, sizeof k->why, "frame after tick %lld: play's frame is %dx%d, the pipeline's %dx%d (pipe_play.sh -s)",
                         (long long)k->tick, po.w, po.h, w, h);
                free(prgb);
                raster_obs_free(&po);
                return;
            }
            nd = diff_px(prgb, cref, w, h, &first);
            free(prgb);
            raster_obs_free(&po);
            k->played = 1;
            if (nd)
            {
                k->bad = 1;
                snprintf(k->why, sizeof k->why, "frame after tick %lld: play's C frame differs from the pipeline's at %d px (first %d,%d)",
                         (long long)k->tick, nd, first % w, first / w);
            }
        }
    }
}

static void *checker_main(void *arg)
{
    struct checkers *c = arg;
    unsigned char *dev = malloc((size_t)c->w * c->h * 3), *cref = malloc((size_t)c->w * c->h * 3);
    int seen = 0;
    pthread_mutex_lock(&c->mu);
    for (;;)
    {
        while (!c->stop && (c->gen == seen || c->next >= c->njobs)) pthread_cond_wait(&c->go, &c->mu);
        if (c->stop) break;
        if (c->next >= c->njobs) { seen = c->gen; continue; }
        struct check *k = &c->jobs[c->next++];
        pthread_mutex_unlock(&c->mu);
        check_one(c, k, dev, cref);
        pthread_mutex_lock(&c->mu);
        if (++c->finished == c->njobs) pthread_cond_broadcast(&c->done);
    }
    pthread_mutex_unlock(&c->mu);
    free(dev);
    free(cref);
    return NULL;
}

static void checks_run(struct checkers *c, struct check *jobs, int n)
{
    if (n == 0) return;
    pthread_mutex_lock(&c->mu);
    c->jobs = jobs;
    c->njobs = n;
    c->next = c->finished = 0;
    ++c->gen;
    pthread_cond_broadcast(&c->go);
    while (c->finished < n) pthread_cond_wait(&c->done, &c->mu);
    c->njobs = 0;
    pthread_mutex_unlock(&c->mu);
}

int main(int argc, char **argv)
{
    int n = 4, threads = 4, ticks = 4, episodes = -1, pollb = -1, ncheck = 8, draw = 0, ow = 128, oh = 128;
    struct pipe_params par = {0};
    const char *snapdir = "out/java/snapshots", *refdir = "out/runtime/ref";
    const char **names = calloc((size_t)argc, sizeof *names);
    int nnames = 0;
    const char *config = NULL;
    int rd = 0, hud = 0;
    float gamma = 0.0F;
    for (int i = 1; i + 1 < argc; ++i)
        if (!strcmp(argv[i], "--config")) config = argv[i + 1];
    if (config)
    {
        struct pipe_config cf = {0};
        char e[512];
        if (pipe_config_file(&cf, config, NULL, e, sizeof e)) { fprintf(stderr, "pipe_gate: %s\n", e); return 2; }
        ow = cf.pool.obs.w, oh = cf.pool.obs.h, ticks = cf.pool.obs.ticks, rd = cf.pool.obs.rd, hud = cf.pool.obs.hud;
        gamma = cf.pool.obs.gamma;
        device_mesh = cf.device_mesh, device_light = cf.device_light, gen_device = cf.gen_device;
    }
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--config") && i + 1 < argc) ++i;    /* read above */
        else if (!strcmp(argv[i], "-n") && i + 1 < argc) n = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-j") && i + 1 < argc) threads = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--ticks") && i + 1 < argc) ticks = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--episodes") && i + 1 < argc) episodes = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--poll") && i + 1 < argc) pollb = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--streams") && i + 1 < argc) par.streams = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--group") && i + 1 < argc) par.group = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--batch") && i + 1 < argc) par.batch = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--depth") && i + 1 < argc) par.depth = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--checkers") && i + 1 < argc) ncheck = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--draw")) draw = 1;
        else if (!strcmp(argv[i], "--device-mesh") && i + 1 < argc) device_mesh = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--mesh-tab")) mesh_tab = 1;
        else if (!strcmp(argv[i], "--usual-trip")) usual_trip = 1;
        else if (!strcmp(argv[i], "--mesh-check")) mesh_check = 1;
        else if (!strcmp(argv[i], "--render-prec") && i + 1 < argc)
        {
            if ((prec = rp_parse(argv[++i])) < 0) { fprintf(stderr, "pipe_gate: --render-prec takes exact, fast or fast:STAGE,...\n"); return 2; }
        }
        else if (!strcmp(argv[i], "--device-light")) device_light = 1;
        else if (!strcmp(argv[i], "--gen-device")) gen_device = 1;
        else if (!strcmp(argv[i], "--gen-serve")) gen_device = gen_serve = 1;
        else if (!strcmp(argv[i], "--gen-nowait")) gen_nowait = 1;
        else if (!strcmp(argv[i], "--gen-ahead") && i + 1 < argc) gen_ahead = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--rd") && i + 1 < argc)
        {
            if ((rd = atoi(argv[++i])) < 2 || rd > 16) { fprintf(stderr, "pipe_gate: --rd takes 2 to 16\n"); return 2; }
        }
        else if (!strcmp(argv[i], "--size") && i + 1 < argc)
        {
            if (sscanf(argv[++i], "%dx%d", &ow, &oh) != 2 || ow < 16 || oh < 16 || ow > 4096 || oh > 4096)
            { fprintf(stderr, "pipe_gate: --size takes WxH (16 to 4096 each)\n"); return 2; }
        }
        else if (!strcmp(argv[i], "--play") && i + 1 < argc) play_dir = argv[++i];
        else if (!strcmp(argv[i], "--snapdir") && i + 1 < argc) snapdir = argv[++i];
        else if (!strcmp(argv[i], "--refdir") && i + 1 < argc) refdir = argv[++i];
        else if (argv[i][0] == '-') { fprintf(stderr, "pipe_gate: unknown flag %s\n", argv[i]); return 2; }
        else names[nnames++] = argv[i];
    }
    if (nnames == 0 || n < 1 || ticks < 1)
    {
        fprintf(stderr, "usage: pipe_gate [-n N] [-j THREADS] [--ticks n] [--streams S] [--group G] [--batch B] [--depth D] NAME...\n");
        return 2;
    }
    if (episodes < 0) episodes = 2 * n > nnames ? 2 * n : nnames;
    if (pollb < 1) pollb = n / 4 > 0 ? n / 4 : 1;
    double t0 = now_s();
    nrecs = nnames;
    recs = calloc((size_t)nrecs, sizeof *recs);
    for (int i = 0; i < nrecs; ++i)
    {
        recs[i].name = names[i];
        if (!load_rec(&recs[i], snapdir, refdir)) return 1;
    }

    struct pipe_config cfg = {0};
    cfg.pool.n = n;
    cfg.pool.threads = threads;
    cfg.pool.obs.w = ow;
    cfg.pool.obs.h = oh;
    cfg.pool.obs.ticks = ticks;
    cfg.pool.obs.rd = rd;
    cfg.pool.obs.hud = hud;
    cfg.pool.obs.gamma = gamma;
    cfg.pool.obs.prec = prec;
    cfg.pool.config = config;
    cfg.pool.dev_apply = dev_apply;
    cfg.pool.dev_end = dev_end;
    cfg.pool.tick_hook = gate_hook;
    cfg.pool.privileged = 1;
    cfg.par = par;
    cfg.draw = draw;
    cfg.device_mesh = device_mesh;
    cfg.mesh_tab = mesh_tab;
    cfg.usual_trip = usual_trip;
    cfg.mesh_check = mesh_check;
    cfg.device_light = device_light;
    cfg.gen_device = gen_device;
    cfg.pool.gen_nowait = gen_nowait;
    cfg.pool.gen_ahead = gen_ahead;
    char gsrv[64];
    if (gen_serve)
    {
        /* the pool a client of its own server, by an abstract socket name */
        snprintf(gsrv, sizeof gsrv, "@nw-pipe-gate-gensrv-%d", (int)getpid());
        cfg.gen_serve = cfg.gen_client = gsrv;
    }
    char err[512];
    genvs = calloc((size_t)n, sizeof *genvs);
    struct pipe *p = pipe_make(&cfg, err, sizeof err);
    if (p == NULL) { printf("FAIL pipe_make: %s\n", err); return 1; }
    pipe_params_get(p, &par);

    struct checkers ck = {0};
    ck.p = p;
    ck.w = ow;
    ck.h = oh;
    ck.draw = draw;
    pthread_mutex_init(&ck.mu, NULL);
    pthread_cond_init(&ck.go, NULL);
    pthread_cond_init(&ck.done, NULL);
    ck.nth = ncheck;
    ck.th = calloc((size_t)ncheck, sizeof *ck.th);
    for (int i = 0; i < ncheck; ++i) pthread_create(&ck.th[i], NULL, checker_main, &ck);

    for (int e = 0; e < episodes; ++e) ++recs[e % nrecs].left;
    double load_s = 0, reset_s = 0;
    int nresets = 0;
    {
        int k0 = n < nrecs ? n : nrecs;
        if (k0 > episodes) k0 = episodes;
        const char **dirs = malloc((size_t)k0 * sizeof *dirs);
        struct pool_start **starts = malloc((size_t)k0 * sizeof *starts);
        for (int i = 0; i < k0; ++i) dirs[i] = recs[i].dir;
        double tl = now_s();
        if (pipe_start_load_many(p, dirs, k0, starts, err, sizeof err) != 0) { printf("FAIL start: %s\n", err); return 1; }
        load_s += now_s() - tl;
        for (int i = 0; i < k0; ++i)
            if (!start_ready(&recs[i], starts[i])) return 1;
        free(dirs);
        free(starts);
    }

    struct pool_act *acts = calloc((size_t)ticks, sizeof *acts);
    int next_episode = 0;
    int64_t *rowpos = calloc((size_t)n, sizeof *rowpos);
    int one[1];
    #define START_EPISODE(id) do {                                                           \
        struct genv *g_ = &genvs[(id)];                                                      \
        g_->r = &recs[next_episode % nrecs];                                                 \
        g_->episode = next_episode++;                                                        \
        g_->bad = 0; g_->checked = 0; g_->why[0] = 0;                                        \
        if (g_->r->start == NULL)                                                            \
        {                                                                                    \
            double tl_ = now_s();                                                            \
            struct pool_start *s_ = pipe_start_load(p, g_->r->dir, err, sizeof err);         \
            load_s += now_s() - tl_;                                                         \
            if (s_ == NULL) { printf("FAIL start: %s\n", err); return 1; }                   \
            if (!start_ready(g_->r, s_)) return 1;                                           \
        }                                                                                    \
        one[0] = (id);                                                                       \
        double tr_ = now_s();                                                                \
        if (pipe_reset(p, one, 1, g_->r->start, err, sizeof err)) { printf("FAIL reset: %s\n", err); return 1; } \
        reset_s += now_s() - tr_; ++nresets;                                                 \
        if (--g_->r->left == 0) { pool_start_free(g_->r->start); g_->r->start = NULL; }      \
        rowpos[(id)] = g_->r->tick;                                                          \
    } while (0)
    #define STEP(id) ({                                                                      \
        struct genv *g_ = &genvs[(id)];                                                      \
        int64_t t_ = rowpos[(id)], left_ = g_->r->nrows - t_;                                \
        int k_ = left_ < ticks ? (int)left_ : ticks;                                         \
        if (k_ > 0)                                                                          \
        {                                                                                    \
            for (int q_ = 0; q_ < k_; ++q_) { acts[q_].kind = PA_ROW; acts[q_].row = g_->r->rows[t_ + q_]; } \
            one[0] = (id);                                                                   \
            if (pipe_step(p, one, 1, acts, k_, 1)) { printf("FAIL step env %d\n", (id)); return 1; } \
        }                                                                                    \
        k_;                                                                                  \
    })

    const struct pipe_result **res = malloc((size_t)n * sizeof *res);
    struct check *jobs = calloc((size_t)n, sizeof *jobs);
    int failed = 0, live = 0;
    int64_t total_rows = 0, frames = 0, frames_play = 0, no_frame = 0;
    double ts = now_s();
    for (int id = 0; id < n && next_episode < episodes; ++id)
    {
        START_EPISODE(id);
        if (STEP(id) > 0) ++live;
    }
    while (live > 0)
    {
        int got = pipe_poll(p, pollb, res);
        live -= got;
        int nj = 0;
        for (int k = 0; k < got; ++k)
        {
            const struct pipe_result *pr = res[k];
            const struct pool_result *r = pr->r;
            int id = pr->id;
            struct genv *g = &genvs[id];
            total_rows += r->ticks;
            if ((r->flags & PF_ERROR) || (r->flags & PF_REFUSED))
            {
                if (!g->bad)
                    snprintf(g->why, sizeof g->why, "row %lld: the step stopped: %.900s", (long long)r->t, r->err);
                g->bad = 1;
            }
            else if (pr->obs.data == NULL)
            {
                if (!g->bad)
                    snprintf(g->why, sizeof g->why, "row %lld: no frame (view rc %d, frame rc %d)", (long long)r->t, r->obs_rc, pr->frame_rc);
                g->bad = 1;
                ++no_frame;
            }
            else jobs[nj++] = (struct check){id, pr, r->t - 1, 0, "", 0};
            /* the client light at the frame (synced: the view drew it) */
            if (device_light && !g->bad && r->t >= 1 && r->t - 1 < g->r->nref && g->r->cwd[r->t - 1] != 0)
            {
                struct session *ss = pool_session(pipe_pool(p), id);
                struct env *saved = nw_env;
                nw_env = pool_env(pipe_pool(p), id);
                uint64_t got = cw_digest(ss->client_world);
                nw_env = saved;
                ++g->r->lights_checked;
                if (got != g->r->cwd[r->t - 1])
                {
                    g->bad = 1;
                    snprintf(g->why, sizeof g->why, "row %lld: the client light digest %016llx, the host's %016llx",
                             (long long)(r->t - 1), (unsigned long long)got, (unsigned long long)g->r->cwd[r->t - 1]);
                }
            }
        }
        checks_run(&ck, jobs, nj);
        for (int k = 0; k < nj; ++k)
        {
            struct genv *g = &genvs[jobs[k].id];
            ++frames;
            ++g->r->frames_checked;
            if (jobs[k].played) { ++frames_play; ++g->r->frames_play; }
            if (jobs[k].bad && !g->bad)
            {
                g->bad = 1;
                snprintf(g->why, sizeof g->why, "%s", jobs[k].why);
            }
        }
        for (int k = 0; k < got; ++k)
        {
            int id = res[k]->id;
            struct genv *g = &genvs[id];
            rowpos[id] = res[k]->r->t;
            int done = g->bad || rowpos[id] >= g->r->nrows;
            if (!done)
            {
                if (STEP(id) > 0) ++live;
                continue;
            }
            if (!g->bad && g->r->ref_end != NULL)
            {
                struct session *ss = pool_session(pipe_pool(p), id);
                struct env *saved = nw_env;
                nw_env = pool_env(pipe_pool(p), id);
                char end[512];
                rowrec_end(ss, end, sizeof end);
                nw_env = saved;
                size_t len = strlen(end);
                if (len && end[len - 1] == '\n') end[len - 1] = 0;
                if (strcmp(end, g->r->ref_end))
                {
                    g->bad = 1;
                    snprintf(g->why, sizeof g->why, "end state: want %.400s got %.400s", g->r->ref_end, end);
                }
            }
            struct rec *rc = g->r;
            ++rc->episodes;
            rc->rows_checked += g->checked;
            if (g->bad)
            {
                failed = 1;
                if (!rc->bad++)
                    snprintf(rc->first_bad, sizeof rc->first_bad, "env %d episode %d: %.980s", id, g->episode, g->why);
            }
            if (next_episode < episodes)
            {
                START_EPISODE(id);
                if (STEP(id) > 0) ++live;
            }
        }
    }
    double run_s = now_s() - ts;
    int64_t checked = 0;
    for (int i = 0; i < nrecs; ++i)
    {
        struct rec *r = &recs[i];
        checked += r->rows_checked;
        if (r->bad) printf("%s: FAIL %d of %d episodes (%lld rows, %lld frames); first: %s\n", r->name, r->bad, r->episodes,
                           (long long)r->rows_checked, (long long)r->frames_checked, r->first_bad);
        else printf("%s: OK %d episodes, %lld rows equal, %lld frames bit-exact%s%s\n", r->name, r->episodes,
                    (long long)r->rows_checked, (long long)r->frames_checked,
                    play_dir ? (r->frames_play ? ", play's frames equal" : ", no play frames") : "",
                    device_light ? ", the client light equal at every frame" : "");
        if (r->episodes == 0) failed = 1;
    }
    struct pipe_stats st;
    pipe_stats(p, &st);
    printf("pipe_gate: %s: %dx%d, %d envs, %d threads, n %d, poll %d, groups of %d on %d streams, batch %d, depth %d, "
           "%d recordings, %d episodes, %lld rows checked (%lld stepped), %lld frames bit-exact against the C renderer%s, "
           "%lld without a frame; %llu device renders; loads %.1f s, %d resets %.1f s, run %.1f s, total %.1f s\n",
           failed ? "FAIL" : "OK", ow, oh, n, threads, ticks, pollb, par.group, par.streams, par.batch, par.depth, nrecs,
           next_episode, (long long)checked, (long long)total_rows, (long long)frames,
           device_mesh == 1 ? " (device meshes alone: the frames are held to play's, below)" : draw ? " and its live frames" : "", (long long)no_frame, (unsigned long long)st.renders, load_s, nresets, reset_s,
           run_s, now_s() - t0);
    {
        struct gencache_stats gs;
        pool_gencache_stats(pipe_pool(p), &gs);
        printf("pipe_gate: generation cache: %llu chunks from it, %llu generated, %llu owed states from it, %llu held\n",
               (unsigned long long)gs.hits, (unsigned long long)gs.misses, (unsigned long long)gs.owed_hits,
               (unsigned long long)gs.entries);
    }
    if (gen_device)
    {
        struct pool_gen_stats g;
        pool_gen_stats(pipe_pool(p), &g);
        printf("pipe_gate: device generation: %llu rounds for %llu steps, %llu requests, %llu generated, %llu from earlier rounds (largest round %llu), "
               "%llu dropped; ticks served %llu, by C %llu (%.2f%%), owed %llu/%llu; made ahead %llu, dropped unused %llu\n",
               (unsigned long long)g.batches, (unsigned long long)g.steps, (unsigned long long)g.requests,
               (unsigned long long)g.unique, (unsigned long long)g.kept, (unsigned long long)g.max_unique, (unsigned long long)g.failed,
               (unsigned long long)g.hits, (unsigned long long)g.misses,
               g.hits + g.misses ? 100.0 * (double)g.hits / (double)(g.hits + g.misses) : 100.0,
               (unsigned long long)g.owed_hits, (unsigned long long)(g.owed_hits + g.owed_misses),
               (unsigned long long)g.generated, (unsigned long long)g.wasted);
    }
    if (play_dir) printf("pipe_gate: %lld frames equal to play's own\n", (long long)frames_play);
    if (device_light)
    {
        int64_t lc = 0;
        for (int i = 0; i < nrecs; ++i) lc += recs[i].lights_checked;
        printf("pipe_gate: the client light on the device: %s: %lld frames' client light digests checked against the host's; "
               "%llu syncs (%llu before a client write, %llu at a light read, %llu at a frame) ran %llu deferred operations, "
               "%llu envs failed a sync\n",
               st.light_failed ? "FAIL" : "OK", (long long)lc, (unsigned long long)st.light_syncs,
               (unsigned long long)st.light_sync_write, (unsigned long long)st.light_sync_read,
               (unsigned long long)st.light_sync_frame, (unsigned long long)st.light_ops,
               (unsigned long long)st.light_failed);
        if (st.light_failed) failed = 1;
    }
    printf("pipe_gate: mesh keys: %llu stale section passes kept their mesh, %llu took the empty mesh, %llu meshed",
           (unsigned long long)st.mesh_reused, (unsigned long long)st.mesh_empty, (unsigned long long)st.meshed);
    if (mesh_check)
    {
        int kbad = st.mesh_check_bad || st.mesh_checked != (device_mesh == 1 ? 0 : st.mesh_reused + st.mesh_empty);
        printf("; %s: %llu of %llu kept and empty meshes equal to the same meshes made again",
               kbad ? "FAIL" : "OK", (unsigned long long)(st.mesh_checked - st.mesh_check_bad),
               (unsigned long long)st.mesh_checked);
        if (kbad) failed = 1;
        printf("; textures: %s: %llu of %llu copies taken back unchecked equal to their sources",
               st.tex_bad ? "FAIL" : "OK", (unsigned long long)(st.tex_checked - st.tex_bad),
               (unsigned long long)st.tex_checked);
        if (st.tex_bad) failed = 1;
        printf("; bands by identity: %s: %llu of %llu kept key hashes and unsent feed bands equal to their bands' bytes",
               st.band_bad ? "FAIL" : "OK", (unsigned long long)(st.band_checked - st.band_bad),
               (unsigned long long)st.band_checked);
        if (st.band_bad) failed = 1;
    }
    printf("\n");
    if (device_mesh)
    {
        int mbad = st.dmesh_diffs || st.frames_failed || (device_mesh == 2 && st.dmesh_checked != st.dmesh_requests);
        printf("pipe_gate: device meshing %d: %s: %llu section passes meshed on the device, %llu of %llu equal to the views' "
               "meshes, %llu frames lost (resynced), %.1f MB of mesh feeds, %.1f ms of device mesh time\n",
               device_mesh, mbad ? "FAIL" : "OK", (unsigned long long)st.dmesh_requests,
               (unsigned long long)(st.dmesh_checked - st.dmesh_diffs), (unsigned long long)st.dmesh_checked,
               (unsigned long long)st.frames_failed, st.dmesh_upload_bytes / 1e6, st.dmesh_ms);
        if (mbad) failed = 1;
    }
    pthread_mutex_lock(&ck.mu);
    ck.stop = 1;
    pthread_cond_broadcast(&ck.go);
    pthread_mutex_unlock(&ck.mu);
    for (int i = 0; i < ncheck; ++i) pthread_join(ck.th[i], NULL);
    pipe_free(p);
    return failed;
}
