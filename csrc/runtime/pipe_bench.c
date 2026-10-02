/* The pipeline's throughput: env-steps per second with observations (each
 * step's frame drawn on the device), for a few start kinds and N, as the RL
 * product runs them: n ticks a step of an agent policy (pool_bench's seeded
 * random walk: forward, sprint, jump, attack, look drift; a dead player
 * respawns), envs handed back B = N/4 at a time and stepped again at once.
 * Where the time goes: the workers' steps (the tick; the view's per-tick
 * hooks and its frame recording inside them), the streams' renders (staging,
 * upload and device work: CUDA events), the streams waiting for a batch,
 * the trainer waiting in pipe_poll.
 *
 *   pipe_bench [--config FILE] [--kinds village,nether,explore] [--ns 16,64,128] [--ticks 4]
 *              [--steps S] [--warm W] [--seconds T [--warm-seconds U]] [--env-base E] [--policy 1|2]
 *              [--profile | --counters] [-j THREADS] [--tune] [--tune-steps K]
 *              [--streams S] [--batch B] [--group G] [--depth D] [--host-only] [--no-view]
 *              [--device-mesh M [--mesh-tab]] [--device-light L] [--arena MB] [--step-each] [--usual-trip]
 *              [--perf-ctl FIFO] [--render-prec P] [--env-seeds] [--gen-device [--gen-raw]] [--product]
 *              [--mem-every T] [--region-spill on|off|DIR] [--size WxH]
 *              [--pin P] [--place L] [--steal] [--layout doms|any] [--frame-digest]
 *              [--rd R] [--difficulty D] [--start NAME=DIR] [--shots DIR]
 *
 * --config FILE (config.yaml, configs/NAME.yaml; pipeline.h
 * pipe_config_file): the observation (size, ticks a step, HUD, the world's
 * render distance), the device switches and one more kind, the file's
 * pipeline.kind starting at its pipeline.start, which is the default
 * --kinds; every start is held to the file's world (a start of another
 * difficulty or render distance fails). The flags after it override it.
 *
 * Two modes, named at the head of every result line:
 *   corpus (the default): the same work whatever the build's speed, for
 *     comparing implementations: every env reset to the kind's start, W
 *     steps not measured (default 100), then S measured (default 1000: at
 *     N 128 village 19 s), each env's actions from its own
 *     seeded stream (a dead player's respawn aside, which the state
 *     decides the same way every run), so every run ticks every env
 *     exactly W + S steps of the same trajectory; the trajectory's digest
 *     (every result's flags, ticks, row, the player's position, look,
 *     dimension, health and food, env by env) is printed to prove it.
 *   capacity (--seconds T): the trainer's view, steady state: U seconds of
 *     warm-up (default 10), then T seconds; each env steps until the time
 *     is up, so a faster build reaches other game states (a wall-time
 *     window is not the same work for two builds: never an A/B).
 * --env-base E: the envs' policy seeds are those of global envs E..E+N-1
 * (two processes of 64 with E 0 and 64 run the streams of one of 128).
 * --policy 2 (the default since 2026-09-29, lane/benchfix): the look deltas
 * symmetric about 0; 1 is the walk before (its pitch delta (0..63 - 32) x
 * 0.05 averaged -0.025 degrees a step: long runs looked up; its yaw -0.05).
 * A run with a runtime error or a missing or lost frame prints FAIL and no
 * throughput, and the program exits 1; ticks are the ticks the steps ran.
 * --counters: the workers' and views' hardware counters (instructions,
 * cycles: read syscalls) and the view's per-tick hook times, off by
 * default (they cost the hot path); --profile: those and the device's
 * timeline (CUPTI: the union of its busy intervals over every stream).
 * --tune: pipe_tune's sweep chooses the parameters for each (kind, N)
 * before the measurement (the envs are reset to the start after it);
 * --host-only: the frames are recorded but not drawn (the host's share
 * alone; refused with device meshing, which it would move to the host);
 * --device-mesh 1: the sections are meshed on the device (pipeline.h
 * device_mesh), the views send feeds instead; --device-light 1: the client
 * world's light runs on the device (device_light); -j: the pool's workers
 * (default: the pipeline's, a physical core each less the streams'
 * threads); --arena: each stream's triangle arena (arena_mb); --step-each:
 * the polled envs stepped a call each (the loop before 2026-09-29; since,
 * the batch in one call). --usual-trip: the renderer waits three times a
 * render, as before lane/simclimb (pipeline.h usual_trip). --perf-ctl: perf record's control fifo (perf
 * record -D -1 --control fifo:FIFO): perf counts the measured steps alone.
 * --env-seeds: global env g's worlds of seed S + g from its reset on (S the
 * start's; populate_reseed), a trainer's different worlds; --gen-device: raw
 * generation on the device (pipeline.h gen_device, lane/genphase), its
 * overworld chunks built there too (the constructor's bands and the sky map,
 * cuda/worldgen/built.cu) unless --gen-raw (pool.h gen_raw: the raw arrays,
 * the envs construct them). --gen-prio high: the generation stream at the
 * device's highest priority (pipeline.h gen_high; default its lowest).
 * --gen-serve PATH: this process's generators serve the processes that
 * connect to the Unix socket PATH as well, every caller's rounds batched
 * together (--gen-batchers B: B rounds in flight, 1 or 2); --gen-nowait: a
 * step never waits for its round (pool.h gen_nowait); --gen-ahead R: the
 * prediction R chunk rings wider (pool.h gen_ahead); --gen-threads T: T
 * generation threads, each with a generator of its own (pool.h
 * gen_threads; default 2: fewer merge more waiting envs into a round);
 * --gen-client PATH: generation on that server (runtime/gensrv.h, lane/gpuspec: one card
 * generating for a machine's pipeline processes).
 * --product: leave the pool's dev hooks null, as the trainer does; a dev
 * checkpoint is refused at load. It checks a prepared start's product path.
 * --mem-every T (with --seconds): the measured window in slices of T
 * seconds (every env handed back at a slice's end, so its throughput is not
 * a capacity), and after each a line of the process's and the envs' memory:
 * resident, the envs' images and heaps, envmem.h's owners summed over the
 * envs, the generation cache and the device's buffers. --region-spill: the
 * region stores' spill file (csrc/engine/regionspill.h; default on).
 * --size WxH: the observation's size (pool_obs w, h; 128x128 by default).
 * --pin and --place: the pool's env to worker map and the workers' CPUs
 * (pool.h pool_config.pin, enum pool_place by name: none, cores, siblings,
 * spread, domain; --steal pool_config.steal; lane/pincache): any of them
 * runs the pool's layout as given, none the pipeline's (pipe_make).
 * --layout doms|any: the pipeline's layout by cache domain or the one before
 * it (pipeline.h PIPE_LAYOUT_DOMS, _ANY; lane/cachefit).
 * --frame-digest: every frame copied back from the device and folded, env
 * by env, into a frame digest printed after the trajectory's (two builds
 * drew the same pixels on equal digests; the copies cost the measurement,
 * so never time a run that takes it).
 * --rd R: the client's render distance (pool_obs rd, 2 to 16; 4, the
 * kinds' recordings' own, by default): the frames draw R chunks, and the
 * agent's first act sets the option (agent.h), so the server's view
 * distance follows it. --difficulty D: every start at difficulty D (0
 * PEACEFUL .. 3 HARD) from its first server tick (pool_config.difficulty);
 * by default the start's own (the kinds' are NORMAL). --start NAME=DIR:
 * one more kind, NAME, starting at the snapshot DIR (the walk, not the
 * explore walk). --shots DIR: no measurement; for each kind, env 0 stands
 * W steps (--warm, look unchanged: the client's chunks and the view's
 * meshes catch up), then takes one step at each of four headings a quarter
 * turn apart at pitch 0, and each frame is written as
 * DIR/KIND-rdR-hK.ppm (K 0..3; DIR/KIND-rdR.txt the positions). */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#include "../cuda/render/render.h"
#include "../engine/dev.h"
#include "../engine/env.h"
#include "../engine/envmem.h"
#include "../engine/gencache.h"
#include "../engine/image.h"
#include "../engine/phase.h"
#include "../engine/populate.h"
#include "../engine/serverreplay.h"
#include "../engine/raster_prec.h"
#include "../engine/worldconf.h"
#include "devbusy.h"
#include "pipeline.h"
#include "semcam.h"
#include "gensrv.h"

struct kind {
    const char *name, *dir;
};
static struct kind KINDS[] = {
    {"village", "out/java/snapshots/cov-villagerep-s42"},   /* a village, seed 42 */
    {"nether", "out/java/snapshots/s1chain-S8"},           /* the Nether fortress of a whole game */
    {"explore", "out/java/snapshots/explore-long-s1"},     /* seed 1's spawn, walking out */
    /* a bounded, populated region generated in a fixed chunk request order
     * (oracle/tests/lib-product2-s1-pre.jsonl); outside it, normal generation runs */
    {"library", "out/java/snapshots/library-product2-full-s1"},
};

static int kind_selected(const char *list, const char *name)
{
    size_t n = strlen(name);
    for (const char *p = list; *p;)
    {
        const char *end = strchr(p, ',');
        if (!end) end = p + strlen(p);
        if ((size_t)(end - p) == n && !strncmp(p, name, n)) return 1;
        p = *end ? end + 1 : end;
    }
    return 0;
}

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static uint64_t rnd(uint64_t *s)
{
    *s ^= *s << 13;
    *s ^= *s >> 7;
    *s ^= *s << 17;
    return *s;
}

/* VERSION 2: the look deltas symmetric about 0; 1: as before 2026-09-29
 * (pitch (0..63 - 32) x 0.05, yaw (0..255 - 128) x scale: a drift up and
 * to one side). The same draws either way. */
static void policy(struct pool_act *a, uint64_t *s, int explore, int version)
{
    memset(a, 0, sizeof *a);
    a->kind = PA_AGENT;
    a->hotbar = -1;
    uint64_t r = rnd(s);
    if (explore || (r & 7) < 6) a->hold |= 1u << PK_FORWARD;
    if (explore || (r >> 3 & 3) == 0) a->hold |= 1u << PK_SPRINT;
    if ((r >> 5 & 7) == 0) a->hold |= 1u << PK_JUMP;
    if (!explore && (r >> 8 & 3) == 0) a->hold |= 1u << PK_ATTACK;
    if ((r >> 10 & 31) == 0) a->press[PK_USE] = 1;
    a->look_mode = PL_DLOOK;
    if (version == 1)
    {
        a->look[0] = (float)((int)(r >> 16 & 255) - 128) * (explore ? 0.02F : 0.1F);
        a->look[1] = (float)((int)(r >> 24 & 63) - 32) * 0.05F;
    }
    else
    {
        a->look[0] = ((float)(r >> 16 & 255) - 127.5F) * (explore ? 0.02F : 0.1F);
        a->look[1] = ((float)(r >> 24 & 63) - 31.5F) * 0.05F;
    }
    if ((r >> 32 & 63) == 0) a->hotbar = (int)(r >> 40 & 7);
}

/* --env-seeds: env id's worlds of seed S + g from here on */
static void reseed(struct pool *pl, int id, int g)
{
    struct session *ss = pool_session(pl, id);
    struct env *saved = nw_env;
    nw_env = pool_env(pl, id);
    int heap = image_heap_set(nw_env, 1);
    int64_t seed = ss->sr->pop.world.seed + g;
    populate_reseed(&ss->sr->pop, seed);
    if (ss->sr->hell.world.slot != NULL) populate_reseed(&ss->sr->hell, seed);
    image_heap_set(nw_env, heap);
    nw_env = saved;
}

/* --shots: env 0's frame after W still steps, then at four headings */
static int shots(struct pipe *p, int n, const char *kind, int rd, const char *dir, int warm, int ticks, int w, int h)
{
    int *ids = malloc((size_t)n * sizeof *ids);
    struct pool_act *acts = calloc((size_t)n, sizeof *acts);
    const struct pipe_result **res = malloc((size_t)n * sizeof *res);
    unsigned char *rgb = malloc((size_t)w * h * 3);
    char path[4096];
    snprintf(path, sizeof path, "%s/%s-rd%d.txt", dir, kind, rd);
    FILE *log = fopen(path, "w");
    int rc = log ? 0 : 1;
    float yaw0 = 0;
    for (int s = 0; s < warm + 4 && rc == 0; ++s)
    {
        for (int i = 0; i < n; ++i)
        {
            ids[i] = i;
            memset(&acts[i], 0, sizeof acts[i]);
            acts[i].kind = PA_AGENT;
            acts[i].hotbar = -1;
            if (s > warm)
            {
                acts[i].look_mode = PL_LOOK;
                acts[i].look[0] = yaw0 + 90.0F * (float)(s - warm);
                acts[i].look[1] = 0;
            }
        }
        if (pipe_step(p, ids, n, acts, ticks, 0)) { rc = 1; break; }
        for (int left = n; left > 0;)
        {
            int got = pipe_poll(p, left, res);
            if (got <= 0) { rc = 1; break; }
            left -= got;
            for (int q = 0; q < got; ++q)
            {
                const struct pipe_result *pr = res[q];
                if (pr->r->flags & PF_ERROR) { fprintf(stderr, "pipe_bench: env %d: %s\n", pr->id, pr->r->err); rc = 1; }
                if (pr->id != 0 || s < warm) continue;
                const struct pool_priv *v = &pr->r->priv;
                if (s == warm) yaw0 = v->yaw;
                int k = s - warm;
                if (pr->obs.data == NULL || pipe_download(p, &pr->obs, rgb) != 0) { fprintf(stderr, "pipe_bench: no frame\n"); rc = 1; continue; }
                snprintf(path, sizeof path, "%s/%s-rd%d-h%d.ppm", dir, kind, rd, k);
                FILE *f = fopen(path, "wb");
                if (!f) { rc = 1; continue; }
                fprintf(f, "P6\n%d %d\n255\n", w, h);
                fwrite(rgb, 1, (size_t)w * h * 3, f);
                fclose(f);
                fprintf(log, "h%d t %lld dim %d pos %.2f %.2f %.2f yaw %.1f pitch %.1f time %lld\n", k, (long long)pr->r->t, v->dim,
                        v->x, v->y, v->z, (double)v->yaw, (double)v->pitch, (long long)v->world_time);
            }
        }
    }
    if (log) fclose(log);
    free(ids);
    free(acts);
    free(res);
    free(rgb);
    return rc;
}

/* a trajectory's digest: FNV-1a over the result's bytes that the engine's
 * state decides */
static uint64_t fold(uint64_t h, const void *p, size_t n)
{
    const unsigned char *b = p;
    for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 0x100000001b3ull;
    return h;
}

static uint64_t fold_result(uint64_t h, const struct pool_result *r)
{
    const struct pool_priv *v = &r->priv;
    int32_t iv[] = {r->flags, r->ticks, v->dim, v->food, v->air, v->fire, v->on_ground, r->hud.selected, r->hud.screen};
    int64_t lv[] = {r->t, v->world_time, v->total_time};
    double dv[] = {v->x, v->y, v->z};
    float fv[] = {v->yaw, v->pitch, v->health, v->saturation, v->exhaustion};
    h = fold(h, iv, sizeof iv);
    h = fold(h, lv, sizeof lv);
    h = fold(h, dv, sizeof dv);
    return fold(h, fv, sizeof fv);
}

/* a run's work and what it counted */
/* --semantic (or the config's client.camera semantic or both): the
 * semantic camera cast at each step's end (semcam.h) into the env's row,
 * its thread time summed by env */
struct bsem {
    struct semcam_spec sp;
    struct semcam_cell *buf;
    uint64_t *ns;
};
static struct bsem bsem;

static void bsem_hook(void *ctx, int id, struct session *ss, struct pool_result *r)
{
    struct bsem *b = ctx;
    (void)r;
    struct timespec t0, t1;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t0);
    semcam_cast(&b->sp, ss, b->buf + (size_t)id * (size_t)b->sp.w * (size_t)b->sp.h);
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t1);
    b->ns[id] += (uint64_t)((t1.tv_sec - t0.tv_sec) * 1000000000ll + (t1.tv_nsec - t0.tv_nsec));
}

struct run {
    int ticks, explore, policy, each;
    double until;           /* --seconds: an env is stepped again only until this time (now_s), 0 none */
    uint64_t *seed, *digest;   /* per env */
    uint64_t *fdigest;         /* per env, --frame-digest: NULL off */
    uint64_t *sdigest;         /* per env, the semantic camera's casts: NULL off */
    unsigned char *frame;      /* the copied frame (--frame-digest) */
    uint64_t steps, ticks_done, errors, noframe;
    uint64_t dim_steps[3], deaths;   /* env-steps ending in the overworld, the Nether, the End; players died */
};

/* every env stepped `steps` more times (or until R->until), B handed back
 * at a time and the batch stepped again in one call (a trainer's shape: a
 * batch's actions come together; EACH: a call an env, as this loop did
 * before 2026-09-29); the env-steps polled, the ticks they ran, errors and
 * frames missing counted */
static void run(struct pipe *p, int n, int steps, struct run *R)
{
    if (steps <= 0) return;
    int *ids = malloc((size_t)n * sizeof *ids), *left = malloc((size_t)n * sizeof *left);
    const struct pipe_result **res = malloc((size_t)n * sizeof *res);
    struct pool_act *acts = malloc((size_t)n * sizeof *acts);
    for (int i = 0; i < n; ++i)
    {
        ids[i] = i;
        left[i] = steps;
        policy(&acts[i], &R->seed[i], R->explore, R->policy);
    }
    if (pipe_step(p, ids, n, acts, R->ticks, 0)) { fprintf(stderr, "pipe_bench: step refused\n"); exit(1); }
    int live = n, b = n / 4 > 0 ? n / 4 : 1;
    while (live > 0)
    {
        int got = pipe_poll(p, b, res);
        live -= got;
        int k = 0;
        for (int q = 0; q < got; ++q)
        {
            const struct pipe_result *pr = res[q];
            int id = pr->id;
            ++R->steps;
            R->ticks_done += (uint64_t)(pr->r->ticks > 0 ? pr->r->ticks : 0);
            R->digest[id] = fold_result(R->digest[id], pr->r);
            if (R->fdigest && pr->obs.data != NULL && pipe_download(p, &pr->obs, R->frame) == 0)
                R->fdigest[id] = fold(R->fdigest[id], R->frame, (size_t)(pr->obs.shape[0] * pr->obs.shape[1] * pr->obs.shape[2]));
            if (R->sdigest && !(pr->r->flags & PF_ERROR))
                R->sdigest[id] = fold(R->sdigest[id], bsem.buf + (size_t)id * (size_t)bsem.sp.w * (size_t)bsem.sp.h,
                                      (size_t)bsem.sp.w * (size_t)bsem.sp.h * sizeof *bsem.buf);
            ++R->dim_steps[pr->r->priv.dim == -1 ? 1 : pr->r->priv.dim == 1 ? 2 : 0];
            R->deaths += (pr->r->flags & PF_DIED) != 0;
            if (pr->r->flags & PF_ERROR) { ++R->errors; if (R->errors < 4) fprintf(stderr, "pipe_bench: env %d: %s\n", id, pr->r->err); }
            else if (pr->obs.data == NULL && pr->frame_rc != 0) ++R->noframe;
            if (--left[id] > 0 && !(pr->r->flags & PF_ERROR) && !(R->until > 0 && now_s() >= R->until))
            {
                policy(&acts[k], &R->seed[id], R->explore, R->policy);
                if (pr->r->flags & PF_DEAD) { acts[k].nops = 1; acts[k].ops[0].kind = PG_RESPAWN; }
                ids[k++] = id;
            }
        }
        for (int i = 0; i < k; i += R->each ? 1 : k)
            if (pipe_step(p, ids + i, R->each ? 1 : k, acts + i, R->ticks, 0)) { fprintf(stderr, "pipe_bench: step refused\n"); exit(1); }
        live += k;
    }
    free(ids);
    free(left);
    free(res);
    free(acts);
}

/* the process's resident memory by kind (/proc/self/smaps): the envs'
 * images (anonymous mappings of 1 GB and more), the views' library copies
 * (memfd), the rest */
static void rss_split(double *images_mb, double *copies_mb, double *rest_mb)
{
    FILE *f = fopen("/proc/self/smaps", "r");
    *images_mb = *copies_mb = *rest_mb = 0;
    if (!f) return;
    char line[512];
    int kind = 2;
    while (fgets(line, sizeof line, f))
    {
        unsigned long lo, hi;
        if (sscanf(line, "%lx-%lx ", &lo, &hi) == 2 && strchr(line, ' '))
        {
            kind = strstr(line, "memfd:playview") ? 1 : (hi - lo >= (1ul << 30) && !strchr(line, '/')) ? 0 : 2;
            continue;
        }
        if (!strncmp(line, "Rss:", 4))
        {
            double mb = (double)atol(line + 4) / 1024.0;
            if (kind == 0) *images_mb += mb;
            else if (kind == 1) *copies_mb += mb;
            else *rest_mb += mb;
        }
    }
    fclose(f);
}

static long rss_kb(void)
{
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) return 0;
    char line[256];
    long v = 0;
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, "VmRSS:", 6)) v = atol(line + 6);
    fclose(f);
    return v;
}

/* --mem-every: one line of the process's and the envs' memory, every env
 * handed back (none is stepping) */
static void mem_line(struct pipe *p, int n, double t, uint64_t steps)
{
    static struct envmem m, sum;
    memset(&sum, 0, sizeof sum);
    double heap = 0, blocks = 0;
    for (int i = 0; i < n; ++i)
    {
        if (!pool_env_mem(pipe_pool(p), i, &m)) continue;
        for (int k = 0; k < m.nitems; ++k) envmem_add(&sum, m.items[k].owner, m.items[k].what, m.items[k].bytes);
        for (int c = 0; c < 3; ++c) sum.chunks[c] += m.chunks[c];
        sum.spilled += m.spilled;
        sum.spill_file += m.spill_file;
        struct env *e = pool_env(pipe_pool(p), i);
        heap += (double)e->img.heap_used;
        blocks += (double)e->img.heap_live;
    }
    double im, cp, rest;
    rss_split(&im, &cp, &rest);
    struct gencache_stats gs;
    pool_gencache_stats(pipe_pool(p), &gs);
    struct pipe_stats s;
    pipe_stats(p, &s);
    const double MB = 1048576.0;
    printf("mem t %.0f s, %llu env-steps: RSS %.0f MB (images %.0f, library copies %.0f, rest %.0f); per env: image %.1f MB "
           "resident, heap %.1f MB used in %.0f blocks, envmem %.1f MB:",
           t, (unsigned long long)steps, (double)rss_kb() / 1024.0, im, cp, rest, im / n, heap / n / MB, blocks / n,
           (double)envmem_total(&sum) / n / MB);
    for (int o = 0; o < EM_N; ++o) printf(" %s %.2f", envmem_name(o), (double)sum.b[o] / n / MB);
    printf(" (chunks per env: server %.0f, region %.0f, client %.0f, spilled %.0f in a file of %.1f MB); generation cache "
           "%.0f MB (%llu entries); device %.0f MB\n",
           (double)sum.chunks[0] / n, (double)sum.chunks[1] / n, (double)sum.chunks[2] / n, (double)sum.spilled / n,
           (double)sum.spill_file / n / MB, (double)gs.bytes / MB,
           (unsigned long long)gs.entries, (double)s.device_bytes / MB);
    for (int k = 0; k < sum.nitems; ++k)
        if (sum.items[k].bytes / n >= 65536)
            printf("    %-14s %-42s %8.2f MB per env\n", envmem_name(sum.items[k].owner), sum.items[k].what,
                   (double)sum.items[k].bytes / n / MB);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    char kinds[256] = "village,nether,explore", ns[256] = "16,64,128";
    double seconds = 0, warm_seconds = 10;
    int ticks = 4, steps = 1000, warm = 100, threads = 0, tune = 0, tune_steps = 4, host_only = 0, no_view = 0, dmesh = 0, dlight = 0, arena = 0, step_each = 0, prec = 0;
    int mesh_tab = 0, usual_trip = 0;
    int env_base = 0, pol = 2, counters = 0, profile = 0;
    int env_seeds = 0, gen_device = 0, gen_raw = 0, gen_card = -1, render_card = 0, product = 0, gen_high = 0, gen_batchers = 0, gen_nowait = 0, gen_ahead = 0, gen_threads = 0;
    const char *gen_serve = NULL, *gen_client = NULL;
    int ow = 128, oh = 128, rd = 0, hud = 0;
    float gamma = 0.0F;
    /* the kinds: the built-in starts, and --config's */
    struct kind kl[8];
    size_t nk = sizeof KINDS / sizeof KINDS[0];
    memcpy(kl, KINDS, sizeof KINDS);
    const char *config = NULL;
    char cstart[1024], ckind[WCONF_VAL];
    for (int i = 1; i + 1 < argc; ++i)
        if (!strcmp(argv[i], "--config")) config = argv[i + 1];
    if (config)
    {
        struct pipe_config cf = {0};
        struct wconf wc;
        char e[512];
        if (pipe_config_file(&cf, config, &wc, e, sizeof e)) { fprintf(stderr, "pipe_bench: %s\n", e); return 2; }
        ow = cf.pool.obs.w, oh = cf.pool.obs.h, ticks = cf.pool.obs.ticks, rd = cf.pool.obs.rd, hud = cf.pool.obs.hud;
        gamma = cf.pool.obs.gamma;   /* the config's client.gamma (it drew at 0 before lane/rlbind) */
        dmesh = cf.device_mesh, gen_device = cf.gen_device, dlight = cf.device_light;
        int camera;
        wconf_semantic(&wc, &camera, &bsem.sp.w, &bsem.sp.h, &bsem.sp.range);
        if (camera == 0) bsem.sp.w = 0;
        if (camera == 1) no_view = 1;
        wconf_start_path(&wc, cstart, sizeof cstart);
        snprintf(ckind, sizeof ckind, "%s", wconf_get(&wc, WC_KIND));
        size_t existing = 0;
        while (existing < nk && strcmp(kl[existing].name, ckind)) ++existing;
        if (existing < nk) kl[existing].dir = cstart;
        else kl[nk++] = (struct kind){ckind, cstart};
        snprintf(kinds, sizeof kinds, "%s", ckind);
    }
    int pin = 0, place = PP_NONE, steal = 0, layout = 0, frame_digest = 0, phases = 0;
    int difficulty = -1;
    const char *shots_dir = NULL;
    double mem_every = 0;
    const char *region_spill = NULL;
    struct pipe_params par = {0};
    FILE *perf_ctl = NULL;
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--config") && i + 1 < argc) ++i;    /* read above */
        else if (!strcmp(argv[i], "--kinds") && i + 1 < argc) snprintf(kinds, sizeof kinds, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--ns") && i + 1 < argc) snprintf(ns, sizeof ns, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--ticks") && i + 1 < argc) ticks = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--steps") && i + 1 < argc) steps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--warm") && i + 1 < argc) warm = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "--warm-seconds") && i + 1 < argc) warm_seconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "--env-base") && i + 1 < argc) env_base = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--policy") && i + 1 < argc) pol = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--counters")) counters = 1;
        else if (!strcmp(argv[i], "--rd") && i + 1 < argc)
        {
            if ((rd = atoi(argv[++i])) < 2 || rd > 16) { fprintf(stderr, "pipe_bench: --rd takes 2 to 16\n"); return 2; }
        }
        else if (!strcmp(argv[i], "--difficulty") && i + 1 < argc)
        {
            if ((difficulty = atoi(argv[++i])) < 0 || difficulty > 3) { fprintf(stderr, "pipe_bench: --difficulty takes 0 to 3\n"); return 2; }
        }
        else if (!strcmp(argv[i], "--start") && i + 1 < argc)
        {
            char *eq = strchr(argv[++i], '=');
            if (!eq || eq == argv[i] || nk == sizeof kl / sizeof kl[0])
            { fprintf(stderr, "pipe_bench: --start takes NAME=DIR (at most %zu kinds)\n", sizeof kl / sizeof kl[0]); return 2; }
            *eq = 0;
            kl[nk].name = argv[i];
            kl[nk++].dir = eq + 1;
        }
        else if (!strcmp(argv[i], "--shots") && i + 1 < argc) shots_dir = argv[++i];
        else if (!strcmp(argv[i], "--env-seeds")) env_seeds = 1;
        else if (!strcmp(argv[i], "--gen-device")) gen_device = 1;
        else if (!strcmp(argv[i], "--gen-raw")) gen_raw = 1;
        else if (!strcmp(argv[i], "--product")) product = 1;
        else if (!strcmp(argv[i], "--gen-card") && i + 1 < argc) gen_card = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--gen-prio") && i + 1 < argc) gen_high = !strcmp(argv[++i], "high");
        else if (!strcmp(argv[i], "--gen-batchers") && i + 1 < argc) gen_batchers = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--gen-nowait")) gen_nowait = 1;
        else if (!strcmp(argv[i], "--gen-ahead") && i + 1 < argc) gen_ahead = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--gen-threads") && i + 1 < argc) gen_threads = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--gen-serve") && i + 1 < argc) gen_serve = argv[++i], gen_device = 1;
        else if (!strcmp(argv[i], "--gen-client") && i + 1 < argc) gen_client = argv[++i], gen_device = 1;
        else if (!strcmp(argv[i], "--render-card") && i + 1 < argc) render_card = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--size") && i + 1 < argc)
        {
            if (sscanf(argv[++i], "%dx%d", &ow, &oh) != 2 || ow < 16 || oh < 16 || ow > 4096 || oh > 4096)
            { fprintf(stderr, "pipe_bench: --size takes WxH (16 to 4096 each)\n"); return 2; }
        }
        else if (!strcmp(argv[i], "--profile")) counters = profile = 1;
        else if (!strcmp(argv[i], "-j") && i + 1 < argc) threads = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--tune")) tune = 1;
        else if (!strcmp(argv[i], "--tune-steps") && i + 1 < argc) tune_steps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--streams") && i + 1 < argc) par.streams = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--batch") && i + 1 < argc) par.batch = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--group") && i + 1 < argc) par.group = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--depth") && i + 1 < argc) par.depth = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--host-only")) host_only = 1;
        else if (!strcmp(argv[i], "--no-view")) no_view = 1;
        else if (!strcmp(argv[i], "--semantic") && i + 1 < argc)
        {
            bsem.sp.range = 64;
            if (sscanf(argv[++i], "%dx%d:%d", &bsem.sp.w, &bsem.sp.h, &bsem.sp.range) < 2 || bsem.sp.w < 1 || bsem.sp.h < 1)
            {
                fprintf(stderr, "pipe_bench: --semantic WxH[:RANGE]\n");
                return 2;
            }
        }
        else if (!strcmp(argv[i], "--device-mesh") && i + 1 < argc) dmesh = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--mesh-tab")) mesh_tab = 1;
        else if (!strcmp(argv[i], "--usual-trip")) usual_trip = 1;
        else if (!strcmp(argv[i], "--device-light") && i + 1 < argc) dlight = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--arena") && i + 1 < argc) arena = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--step-each")) step_each = 1;
        else if (!strcmp(argv[i], "--pin") && i + 1 < argc) pin = atoi(argv[++i]), layout = 1;
        else if (!strcmp(argv[i], "--steal")) steal = layout = 1;
        else if (!strcmp(argv[i], "--layout") && i + 1 < argc && (!strcmp(argv[i + 1], "doms") || !strcmp(argv[i + 1], "any")))
            layout = !strcmp(argv[++i], "doms") ? PIPE_LAYOUT_DOMS : PIPE_LAYOUT_ANY;
        else if (!strcmp(argv[i], "--frame-digest")) frame_digest = 1;
        else if (!strcmp(argv[i], "--phases")) phases = layout = pin = 1;
        else if (!strcmp(argv[i], "--kind-dir") && i + 1 < argc && strchr(argv[i + 1], '='))
        {
            /* NAME=DIR: kind NAME's start from DIR (a recording outside this tree) */
            char *eq = strchr(argv[++i], '=');
            for (size_t k = 0; k < nk; ++k)
                if (strlen(kl[k].name) == (size_t)(eq - argv[i]) && !strncmp(kl[k].name, argv[i], (size_t)(eq - argv[i])))
                    kl[k].dir = eq + 1;
        }
        else if (!strcmp(argv[i], "--place") && i + 1 < argc && (place = pool_place_parse(argv[++i])) >= 0) layout = 1;
        else if (!strcmp(argv[i], "--mem-every") && i + 1 < argc) mem_every = atof(argv[++i]);
        else if (!strcmp(argv[i], "--region-spill") && i + 1 < argc) region_spill = argv[++i];
        else if (!strcmp(argv[i], "--render-prec") && i + 1 < argc)
        {
            if ((prec = rp_parse(argv[++i])) < 0) { fprintf(stderr, "pipe_bench: --render-prec takes exact, fast or fast:STAGE,...\n"); return 2; }
        }
        else if (!strcmp(argv[i], "--perf-ctl") && i + 1 < argc)
        {
            if (!(perf_ctl = fopen(argv[++i], "w"))) { fprintf(stderr, "pipe_bench: %s does not open\n", argv[i]); return 2; }
            setvbuf(perf_ctl, NULL, _IONBF, 0);
        }
        else { fprintf(stderr, "pipe_bench: unknown argument %s\n", argv[i]); return 2; }
    }
    if (pol != 1 && pol != 2) { fprintf(stderr, "pipe_bench: --policy takes 1 or 2\n"); return 2; }
    if (mem_every > 0 && seconds <= 0) { fprintf(stderr, "pipe_bench: --mem-every slices a capacity window (--seconds)\n"); return 2; }
    if (env_base < 0) { fprintf(stderr, "pipe_bench: --env-base takes a global env id, 0 or more\n"); return 2; }
    if (host_only && dmesh && !no_view)
    {
        fprintf(stderr, "pipe_bench: --host-only with --device-mesh would mesh on the host (a different workload): "
                        "measure the host's share with host meshing, or the device's with the device\n");
        return 2;
    }
    /* the device's timeline: before the first CUDA call */
    int timeline = profile && !host_only && !no_view;
    if (timeline && devbusy_start() != 0) { fprintf(stderr, "pipe_bench: CUPTI's activity records do not start\n"); return 1; }
    int failed = 0;
    for (size_t k = 0; k < nk; ++k)
    {
        if (!kind_selected(kinds, kl[k].name)) continue;
        char list[256];
        snprintf(list, sizeof list, "%s", ns);
        for (char *tok = strtok(list, ","); tok; tok = strtok(NULL, ","))
        {
            int n = atoi(tok);
            int thr = threads > 0 ? threads : 0;   /* 0: the pipeline's default (pipe_make) */
            struct pipe_config cfg = {0};
            cfg.pool.n = n;
            cfg.pool.threads = thr;
            cfg.pool.obs.w = ow;
            cfg.pool.obs.h = oh;
            cfg.pool.obs.ticks = ticks;
            cfg.pool.obs.rd = rd;
            cfg.pool.obs.hud = hud;
            cfg.pool.obs.gamma = gamma;
            cfg.pool.obs.prec = prec;
            cfg.pool.difficulty = difficulty + 1;
            cfg.pool.config = config;
            cfg.pool.dev_apply = product ? NULL : dev_apply;
            cfg.pool.dev_end = product ? NULL : dev_end;
            cfg.pool.privileged = 1;        /* the trajectory's digest reads the player */
            cfg.pool.no_counters = !counters;
            cfg.pool.pin = pin;
            cfg.pool.profile = phases;
            cfg.pool.place = place;
            cfg.pool.steal = steal;
            cfg.pool_layout = layout;
            cfg.pool.region_spill = region_spill;
            cfg.par = par;
            cfg.no_device = host_only;
            cfg.no_view = no_view;
            cfg.device_mesh = dmesh;
            cfg.mesh_tab = mesh_tab;
            cfg.usual_trip = usual_trip;
            cfg.device_light = dlight;
            cfg.arena_mb = arena;
            cfg.gen_device = gen_device;
            cfg.pool.gen_raw = gen_raw;
            cfg.device = render_card;
            cfg.gen_card = gen_card >= 0 ? gen_card + 1 : 0;
            cfg.gen_high = gen_high;
            cfg.gen_batchers = gen_batchers;
            cfg.pool.gen_nowait = gen_nowait;
            cfg.pool.gen_ahead = gen_ahead;
            cfg.pool.gen_threads = gen_threads;
            cfg.gen_serve = gen_serve;
            cfg.gen_client = gen_client;
            if (bsem.sp.w > 0)
            {
                free(bsem.buf);
                free(bsem.ns);
                bsem.buf = calloc((size_t)n * (size_t)bsem.sp.w * (size_t)bsem.sp.h, sizeof *bsem.buf);
                bsem.ns = calloc((size_t)n, sizeof *bsem.ns);
                cfg.pool.step_hook = bsem_hook;
                cfg.pool.step_hook_ctx = &bsem;
            }
            char err[512];
            double tm = now_s();
            struct pipe *p = pipe_make(&cfg, err, sizeof err);
            if (!p) { printf("FAIL %s N %d: %s\n", kl[k].name, n, err); return 1; }
            struct pool_start *st = pipe_start_load(p, kl[k].dir, err, sizeof err);
            if (!st) { printf("FAIL %s: %s\n", kl[k].name, err); return 1; }
            int *ids = malloc((size_t)n * sizeof *ids);
            for (int i = 0; i < n; ++i) ids[i] = i;
            double tune_s = 0;
            if (tune && !host_only && !no_view)
            {
                double tt = now_s();
                if (pipe_tune(p, st, tune_steps, stdout, err, sizeof err)) { printf("FAIL tune: %s\n", err); return 1; }
                tune_s = now_s() - tt;
            }
            thr = pool_threads(pipe_pool(p));
            double tr = now_s();
            if (pipe_reset(p, ids, n, st, err, sizeof err)) { printf("FAIL reset: %s\n", err); return 1; }
            for (int i = 0; env_seeds && i < n; ++i)
                if (env_base + i > 0) reseed(pipe_pool(p), i, env_base + i);
            double reset_s = now_s() - tr, make_s = tr - tm - tune_s;
            if (shots_dir)
            {
                int rc = shots(p, n, kl[k].name, rd > 0 ? rd : 4, shots_dir, warm, ticks, ow, oh);
                printf("%s %s N %d rd %d: shots in %s\n", rc ? "FAIL" : "OK", kl[k].name, n, rd > 0 ? rd : 4, shots_dir);
                failed |= rc;
                free(ids);
                pipe_free(p);
                pool_start_free(st);
                continue;
            }
            struct run R = {.ticks = ticks, .explore = !strcmp(kl[k].name, "explore") || !strcmp(kl[k].name, "library"),
                            .policy = pol, .each = step_each};
            R.seed = malloc((size_t)n * sizeof *R.seed);
            R.digest = malloc((size_t)n * sizeof *R.digest);
            R.fdigest = frame_digest ? malloc((size_t)n * sizeof *R.fdigest) : NULL;
            R.frame = frame_digest ? malloc((size_t)ow * oh * 3) : NULL;
            /* --frame-digest: the casts' digest too (FNV over every cast on the polling thread: not for timing) */
            R.sdigest = bsem.sp.w > 0 && frame_digest ? malloc((size_t)n * sizeof *R.sdigest) : NULL;
            /* global env g's stream (the one env g of a single process has) */
            for (int i = 0; i < n; ++i)
            {
                R.seed[i] = 0x9e3779b97f4a7c15ull * (uint64_t)(env_base + i + 1) + k;
                R.digest[i] = 0xcbf29ce484222325ull;
                if (R.fdigest) R.fdigest[i] = 0xcbf29ce484222325ull;
                if (R.sdigest) R.sdigest[i] = 0xcbf29ce484222325ull;
            }
            double tw = now_s();
            R.until = seconds > 0 ? now_s() + warm_seconds : 0;
            run(p, n, seconds > 0 ? 1 << 30 : warm, &R);
            R.until = 0;
            double warm_s = now_s() - tw;
            uint64_t warm_steps = R.steps, warm_errors = R.errors, warm_noframe = R.noframe;
            {
                /* the envs' images: the heap they hold (the view's allocations are the env's) */
                double used = 0, live = 0;
                for (int i = 0; i < n; ++i)
                {
                    struct env *e = pool_env(pipe_pool(p), i);
                    used += (double)e->img.heap_used;
                    live += (double)e->img.heap_live;
                }
                double im, cp, rest;
                rss_split(&im, &cp, &rest);
                printf("   after the warm-up: resident %.0f MB per env in its image (heap %.0f MB of address space, "
                       "%.0f blocks), %.1f MB per env of library copies, %.0f MB the rest of the process\n",
                       im / n, used / n / 1048576.0, live / n, cp / n, rest);
            }
            struct pipe_params in;
            pipe_params_get(p, &in);
            pipe_stats_reset(p);
            struct pool_gen_stats g0, g1;
            pool_gen_stats(pipe_pool(p), &g0);
            R.steps = R.ticks_done = R.errors = R.noframe = R.deaths = 0;
            if (bsem.ns) memset(bsem.ns, 0, (size_t)n * sizeof *bsem.ns);
            memset(R.dim_steps, 0, sizeof R.dim_steps);
            struct devbusy db = {0};
            if (timeline) devbusy_read(0, devbusy_now(), &db);   /* the warm-up's records dropped */
            struct rusage ru0, ru1;
            if (perf_ctl) fputs("enable\n", perf_ctl);
            getrusage(RUSAGE_SELF, &ru0);
            uint64_t d0 = timeline ? devbusy_now() : 0;
            double t0 = now_s();
            if (mem_every > 0)
            {
                /* slices: every env handed back at each end */
                mem_line(p, n, 0, 0);
                for (double end = t0 + seconds, now; (now = now_s()) < end;)
                {
                    R.until = now + mem_every < end ? now + mem_every : end;
                    run(p, n, 1 << 30, &R);
                    mem_line(p, n, now_s() - t0, R.steps);
                }
            }
            else
            {
                R.until = seconds > 0 ? t0 + seconds : 0;
                run(p, n, seconds > 0 ? 1 << 30 : steps, &R);
            }
            R.until = 0;
            double wall = now_s() - t0;
            uint64_t d1 = timeline ? devbusy_now() : 0;
            getrusage(RUSAGE_SELF, &ru1);
            if (perf_ctl) fputs("disable\n", perf_ctl);
            if (timeline) devbusy_read(d0, d1, &db);
            struct pipe_stats s;
            pipe_stats(p, &s);
            size_t gfree = 0, gtotal = 0;
            render_dev_mem(&gfree, &gtotal);
            double per = (double)R.steps;
            char mode[224];
            if (seconds > 0)
                snprintf(mode, sizeof mode, "capacity %.0f+%.0f s%s, policy %d, envs %d-%d, %dx%d", warm_seconds, seconds,
                         mem_every > 0 ? " in memory slices" : "", pol, env_base, env_base + n - 1, ow, oh);
            else
                snprintf(mode, sizeof mode, "corpus %d+%d steps, policy %d, envs %d-%d, %dx%d", warm, steps, pol, env_base,
                         env_base + n - 1, ow, oh);
            if (layout == PIPE_LAYOUT_GIVEN) snprintf(mode + strlen(mode), sizeof mode - strlen(mode), ", pin %d place %s%s", pin, pool_place_name(place), steal ? " steal" : "");
            else if (layout) snprintf(mode + strlen(mode), sizeof mode - strlen(mode), ", layout %s", layout == PIPE_LAYOUT_DOMS ? "doms" : "any");
            if (rd > 0) snprintf(mode + strlen(mode), sizeof mode - strlen(mode), ", rd %d", rd);
            if (difficulty >= 0) snprintf(mode + strlen(mode), sizeof mode - strlen(mode), ", difficulty %d", difficulty);
            /* a valid run: no env stopped, every frame there, and (corpus)
             * every env stepped its whole corpus */
            uint64_t want = seconds > 0 ? 0 : (uint64_t)n * (uint64_t)(warm + steps);
            int invalid = R.errors || R.noframe || s.frames_failed || warm_errors || warm_noframe ||
                          (want && warm_steps + R.steps != want);
            if (invalid)
            {
                printf("FAIL %-8s N %3d [%s]: %llu errors, %llu frames missing, %llu frames lost, %llu of %llu env-steps: "
                       "not a valid run, no throughput (stderr names the first errors)\n",
                       kl[k].name, n, mode, (unsigned long long)(R.errors + warm_errors),
                       (unsigned long long)(R.noframe + warm_noframe), (unsigned long long)s.frames_failed,
                       (unsigned long long)(warm_steps + R.steps), (unsigned long long)want);
                failed = 1;
            }
            else
            {
                printf("%-8s N %3d [%s], %2d threads, groups of %d on %d streams, batch %d, depth %d, %s meshing: %.0f env-steps/s "
                       "with observations (%.0f ticks/s), %llu env-steps, %llu ticks, %llu frames in %llu renders%s\n",
                       kl[k].name, n, mode, thr, in.group, in.streams, in.batch, in.depth,
                       dmesh ? (mesh_tab ? "device (table mode)" : "device") : "host",
                       per / wall, (double)R.ticks_done / wall, (unsigned long long)R.steps, (unsigned long long)R.ticks_done,
                       (unsigned long long)s.frames, (unsigned long long)s.renders,
                       no_view ? " (no view: the pool alone, no frames)" : host_only ? " (host only: frames recorded, not drawn)" : "");
            }
            if (seconds <= 0)
            {
                uint64_t dg = 0xcbf29ce484222325ull;
                for (int i = 0; i < n; ++i) dg = fold(dg, &R.digest[i], sizeof R.digest[i]);
                printf("   trajectory digest %016llx (every env's results, warm-up and measured, env by env: the same work "
                       "for any build, stream or worker setting)\n", (unsigned long long)dg);
                if (R.fdigest)
                {
                    uint64_t fg = 0xcbf29ce484222325ull;
                    for (int i = 0; i < n; ++i) fg = fold(fg, &R.fdigest[i], sizeof R.fdigest[i]);
                    printf("   frame digest %016llx (every frame drawn, warm-up and measured, env by env)\n", (unsigned long long)fg);
                }
                if (R.sdigest)
                {
                    uint64_t sg = 0xcbf29ce484222325ull;
                    for (int i = 0; i < n; ++i) sg = fold(sg, &R.sdigest[i], sizeof R.sdigest[i]);
                    printf("   semantic digest %016llx (every cast, warm-up and measured, env by env)\n", (unsigned long long)sg);
                }
            }
            printf("   the measured env-steps: %.1f%% ended in the overworld, %.1f%% in the Nether, %.1f%% in the End; "
                   "%llu deaths (a dead player respawns in the overworld)\n",
                   100.0 * (double)R.dim_steps[0] / per, 100.0 * (double)R.dim_steps[1] / per,
                   100.0 * (double)R.dim_steps[2] / per, (unsigned long long)R.deaths);
            double host_ms = (double)s.tick_ns / 1e6 / per;
            if (bsem.sp.w > 0)
            {
                uint64_t sns = 0;
                for (int i = 0; i < n; ++i) sns += bsem.ns[i];
                printf("   semantic camera %dx%d, range %d: %.3f ms of a worker's thread a cast (within the host step)\n",
                       bsem.sp.w, bsem.sp.h, bsem.sp.range, (double)sns / 1e6 / per);
            }
            double vt = (double)s.view_tick_ns / 1e6 / per, vf = (double)s.view_frame_ns / 1e6 / per;
            if (counters)
                printf("   per env-step (ms of the thread that spent it): host step %.2f = tick %.2f + view hooks %.2f + frame "
                       "record %.2f; M user instructions %.3f = tick %.3f + view hooks %.3f + frame record %.3f\n",
                       host_ms, host_ms - vt - vf, vt, vf, (double)s.pool_instructions / 1e6 / per,
                       (double)(s.pool_instructions - s.view_tick_ins - s.view_frame_ins) / 1e6 / per,
                       (double)s.view_tick_ins / 1e6 / per, (double)s.view_frame_ins / 1e6 / per);
            else
                printf("   per env-step (ms of the thread that spent it): host step %.2f = tick and view hooks %.2f + frame "
                       "record %.2f (--counters or --profile: the hooks' share, instructions and cycles)\n",
                       host_ms, host_ms - vf, vf);
            printf("   host meshing feeds %.3f ms per env-step (within frame record)\n", s.feed_ms / per);
            printf("   per env-step: %.1f section passes meshed, %.1f kept by key, %.1f empty, %.0f drawn; render %.2f ms "
                   "(its CUDA events: %.3f ms from the render's start to its first launch, the host's staging of the draw lists "
                   "with the copies it queued, then %.3f ms to its end); "
                   "upload %.1f KB (meshes %.1f in %.1f sections, atlas tiles %.1f, textures %.1f, entity quads %.1f); "
                   "%llu mesh arena restarts; %llu renders in one trip (%llu run again); device meshing %.1f section passes %.1f KB of feeds (bands %.1f of which runs %.1f, chunk records "
                   "%.1f, lists %.1f, requests %.1f, copy lists and launches %.1f) %.3f device-ms, %llu frames lost\n",
                   (double)s.meshed / per, (double)s.mesh_reused / per, (double)s.mesh_empty / per, (double)s.drawn / per,
                   (double)s.render_ns / 1e6 / per, s.upload_ms / per, s.device_ms / per,
                   (double)s.upload_bytes / 1024.0 / per, (double)s.mesh_bytes / 1024.0 / per, (double)s.fresh_meshes / per,
                   (double)s.atlas_bytes / 1024.0 / per, (double)s.tex_bytes / 1024.0 / per,
                   (double)s.quad_bytes / 1024.0 / per, (unsigned long long)s.mesh_resets,
                   (unsigned long long)s.trips, (unsigned long long)s.trip_overs,
                   (double)s.dmesh_requests / per, (double)s.dmesh_upload_bytes / 1024.0 / per,
                   (double)s.dmesh_band_bytes / 1024.0 / per, (double)s.dmesh_band_run_bytes / 1024.0 / per,
                   (double)s.dmesh_chunk_bytes / 1024.0 / per,
                   (double)s.dmesh_list_bytes / 1024.0 / per, (double)s.dmesh_req_bytes / 1024.0 / per,
                   (double)s.dmesh_ctl_bytes / 1024.0 / per, s.dmesh_ms / per,
                   (unsigned long long)s.frames_failed);
            if (counters)
            {
                uint64_t tick_ins = s.pool_instructions - s.view_tick_ins - s.view_frame_ins;
                uint64_t tick_cyc = s.pool_cycles - s.view_tick_cyc - s.view_frame_cyc;
                printf("   tick per env-step: %.1f M user cycles, %.2f instructions a cycle; frame record %.1f M user cycles, "
                       "%.2f instructions a cycle; view hooks %.1f M cycles, %.2f a cycle\n",
                       (double)tick_cyc / 1e6 / per, tick_cyc ? (double)tick_ins / (double)tick_cyc : 0.0,
                       (double)s.view_frame_cyc / 1e6 / per,
                       s.view_frame_cyc ? (double)s.view_frame_ins / (double)s.view_frame_cyc : 0.0,
                       (double)s.view_tick_cyc / 1e6 / per,
                       s.view_tick_cyc ? (double)s.view_tick_ins / (double)s.view_tick_cyc : 0.0);
            }
            printf("   an env-step's wall ms: step %.2f, queued for its stream %.2f, the stream's render %.2f, ready until "
                   "polled %.2f, polled until stepped again %.2f\n",
                   (double)s.lat_step_ns / 1e6 / per, (double)s.lat_queue_ns / 1e6 / per, (double)s.lat_render_ns / 1e6 / per,
                   (double)s.lat_poll_ns / 1e6 / per, (double)s.lat_trainer_ns / 1e6 / per);
            printf("   busy: workers %.0f%% of %d threads, streams %.0f%% of %d (meshers %.0f%%, idle waiting for a batch %.0f%%), "
                   "trainer waiting in poll %.0f%%; mean frames per render %.1f\n",
                   100.0 * (double)s.tick_ns / 1e9 / wall / thr, thr, 100.0 * (double)s.render_ns / 1e9 / wall / in.streams,
                   in.streams, 100.0 * (double)s.mesher_ns / 1e9 / wall / in.streams,
                   100.0 * (double)s.stream_idle_ns / 1e9 / wall / in.streams,
                   100.0 * (double)s.poll_wait_ns / 1e9 / wall, s.renders ? (double)s.frames / (double)s.renders : 0.0);
            if (timeline)
                printf("   device busy %.1f%% of the wall (the union of its kernels, copies and memsets on every stream: CUPTI), "
                       "kernels %.1f%%, copies and memsets %.1f%%; the same intervals summed over the streams %.1f%%; "
                       "%llu records%s\n",
                       100.0 * db.busy_ms / 1e3 / wall, 100.0 * db.kernel_ms / 1e3 / wall, 100.0 * db.copy_ms / 1e3 / wall,
                       100.0 * db.summed_ms / 1e3 / wall, (unsigned long long)db.records,
                       db.dropped ? " (some dropped: a lower bound)" : "");
            else if (!host_only && !no_view)
                printf("   the renders' CUDA events (start to end) %.0f%% of the wall summed over %d streams: overlapping intervals "
                       "with the host's staging in them, not the device's utilization (--profile measures that)\n",
                       100.0 * (s.device_ms + s.upload_ms) / 1e3 / wall, in.streams);
            {
                /* the process's page faults and kernel time over the measured steps */
                double ut = (double)(ru1.ru_utime.tv_sec - ru0.ru_utime.tv_sec) + (double)(ru1.ru_utime.tv_usec - ru0.ru_utime.tv_usec) / 1e6;
                double kt = (double)(ru1.ru_stime.tv_sec - ru0.ru_stime.tv_sec) + (double)(ru1.ru_stime.tv_usec - ru0.ru_stime.tv_usec) / 1e6;
                printf("   memory: %.0f minor and %.1f major page faults per env-step; CPU %.2f ms per env-step, %.0f%% of it "
                       "in the kernel\n",
                       (double)(ru1.ru_minflt - ru0.ru_minflt) / per, (double)(ru1.ru_majflt - ru0.ru_majflt) / per,
                       (ut + kt) * 1e3 / per, ut + kt > 0 ? 100.0 * kt / (ut + kt) : 0.0);
            }
            pool_gen_stats(pipe_pool(p), &g1);
            if (gen_device)
                printf("   device generation: %llu rounds, %.2f requests, %.2f generated, %.2f from earlier rounds an env-step; "
                       "ticks' generations served %llu, by C %llu; steps waited %.2f an env-step, %.1f us a wait; results held %.0f MB\n",
                       (unsigned long long)(g1.batches - g0.batches), (double)(g1.requests - g0.requests) / per,
                       (double)(g1.unique - g0.unique) / per, (double)(g1.kept - g0.kept) / per,
                       (unsigned long long)(g1.hits - g0.hits), (unsigned long long)(g1.misses - g0.misses),
                       (double)(g1.waits - g0.waits) / per,
                       g1.waits > g0.waits ? (double)(g1.wait_ns - g0.wait_ns) / (double)(g1.waits - g0.waits) / 1e3 : 0.0,
                       (double)g1.res_bytes / 1048576.0);
            if (gen_device && g1.misses > g0.misses)
            {
                /* the overworld's C generations by the player's distance at the step start */
                printf("   generations by C in the overworld by chunk distance from the player (0-4, 5-8, 9, 10-11, 12-13, "
                       "14-19, 20-28, 29+):");
                for (int b = 0; b < 8; ++b) printf(" %llu", (unsigned long long)(g1.miss_ring[0][b] - g0.miss_ring[0][b]));
                printf("; other dimension %llu\n", (unsigned long long)(g1.miss_other_dim - g0.miss_other_dim));
            }
            if (pipe_gensrv(p))
            {
                struct gensrv_stats gs;
                gensrv_stats(pipe_gensrv(p), &gs);
                double r = gs.rounds ? (double)gs.rounds : 1;
                printf("   generation server (since the start, every client's calls): %llu clients, %llu rounds, %.1f calls and "
                       "%.1f requests (%.1f unique) a round (at most %llu); a round's generator %.2f ms and copies out %.2f ms, a call "
                       "queued %.2f ms\n",
                       (unsigned long long)gs.clients, (unsigned long long)gs.rounds, (double)gs.calls / r,
                       (double)gs.requests / r, (double)gs.unique / r, (unsigned long long)gs.max_requests, (double)gs.run_ns / r / 1e6,
                       (double)gs.scatter_ns / r / 1e6, gs.calls ? (double)gs.queued_ns / (double)gs.calls / 1e6 : 0.0);
            }
            if (dlight)
                printf("   device light: %.2f syncs an env-step (before client writes %.2f, at light reads %.2f, at frames "
                       "%.2f), %.1f operations a sync; %llu envs failed\n",
                       (double)s.light_syncs / per, (double)s.light_sync_write / per, (double)s.light_sync_read / per,
                       (double)s.light_sync_frame / per, s.light_syncs ? (double)s.light_ops / (double)s.light_syncs : 0.0,
                       (unsigned long long)s.light_failed);
            if (phases)
            {
                /* every env's profiled rows (warm-up and measured, from its
                 * reset): the tick's user instructions and the sub-marks' */
                uint64_t rows = 0, all = 0, sub[PS_COUNT] = {0};
                for (int i = 0; i < n; ++i)
                {
                    uint64_t ri, si[PS_COUNT];
                    rows += pool_profile_totals(pipe_pool(p), i, &ri, si);
                    all += ri;
                    for (int q = 0; q < PS_COUNT; ++q) sub[q] += si[q];
                }
                double f = rows ? (double)ticks / (double)rows / 1e6 : 0;
                printf("   phases (%llu rows of every env, M user instructions an env-step, sub-marks inclusive): tick %.3f; "
                       "load %.3f (generation %.3f, population %.3f), light %.3f, new chunk light %.3f, light flags %.3f, relight checks %.3f, "
                       "random ticks %.3f, paths %.3f, park %.3f, grave %.3f\n",
                       (unsigned long long)rows, all * f, sub[PS_LOAD] * f, sub[PS_GEN] * f, sub[PS_POP] * f,
                       sub[PS_LIGHT] * f, sub[PS_NEWLIGHT] * f, sub[PS_LFLAGS] * f, sub[PS_RELIGHT] * f, sub[PS_RTICK] * f, sub[PS_PATH] * f,
                       sub[PS_PARK] * f, sub[PS_GRAVE] * f);
            }
            struct gencache_stats gs;
            pool_gencache_stats(pipe_pool(p), &gs);
            printf("   generation cache (since the make): %llu chunks from it, %llu generated (%llu owed states from it), "
                   "%llu held in %.0f MB%s\n",
                   (unsigned long long)gs.hits, (unsigned long long)gs.misses, (unsigned long long)gs.owed_hits,
                   (unsigned long long)gs.entries, (double)gs.bytes / 1048576.0, gs.full ? " (full)" : "");
            printf("   make %.1f s, sweep %.1f s, reset %.1f s, warm-up %llu env-steps %.1f s; process RSS %.0f MB (%.0f MB per env); "
                   "device memory %.0f MB (%.1f MB per env), device free %.1f of %.1f GB (the co-tenants' included)\n",
                   make_s, tune_s, reset_s, (unsigned long long)warm_steps, warm_s, (double)rss_kb() / 1024.0,
                   (double)rss_kb() / 1024.0 / n, (double)s.device_bytes / 1048576.0, (double)s.device_bytes / 1048576.0 / n,
                   (double)gfree / 1e9, (double)gtotal / 1e9);
            fflush(stdout);
            free(ids);
            free(R.seed);
            free(R.digest);
            free(R.fdigest);
            free(R.frame);
            pipe_free(p);
            pool_start_free(st);
        }
    }
    return failed;
}
