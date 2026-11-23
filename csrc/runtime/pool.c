/* The env pool (pool.h). */
#define _GNU_SOURCE
#include "pool.h"

#include <errno.h>
#include <limits.h>
#include <linux/perf_event.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#if defined(__x86_64__)
#include <immintrin.h>
#endif

#include "../engine/arena.h"
#include "../engine/container.h"
#include "../engine/cwrand.h"
#include "../engine/env.h"
#include "../engine/envmem.h"
#include "../engine/gencache.h"
#include "../engine/genahead.h"
#include "../engine/grave.h"
#include "../engine/image.h"
#include "../engine/image_wt.h"
#include "../engine/nbtjson.h"
#include "../engine/phase.h"
#ifdef LIVFAULT_AUDIT
#include "../engine/living.h"
#endif
#include "../engine/player.h"
#include "../engine/serverreplay.h"
#include "../engine/session.h"
#include "../engine/snapshot.h"
#include "../engine/survival.h"
#include "../engine/tape.h"
#include "../engine/world.h"
#include "../engine/worldconf.h"
#include "feed.h"
#include "agent.h"

#define PAGE 4096u

/* ------------------------------------------------------------ the act log
 *
 * cfg.save_slots: what an env ran since its reset, for a save's replays
 * (pool_start_from_env), in process memory: per step a header (LOG_STEP,
 * the ticks it ran), then per tick the act step_act made it from: an
 * agent-form act as agent_act took it (after the act hook, with its
 * hold_only), a tape act, or a row's text. A replay runs the same step_job
 * over it, so the same ticks, view calls and frames come out. */
struct actlog {
    unsigned char *b;
    size_t n, cap;
    size_t step_at;             /* the open step's header */
    int64_t steps, ticks;
    int bad;                    /* a step errored: the env cannot be saved */
};
enum { LOG_STEP = 0x50455453u };
struct log_ent {
    uint8_t kind, hold_only;
    uint16_t pad;
    uint32_t len;
};

static void *log_put(struct actlog *l, size_t len)
{
    size_t need = l->n + ((len + 7) & ~(size_t)7);
    if (need > l->cap)
    {
        size_t cap = l->cap ? l->cap : 65536;
        while (cap < need) cap *= 2;
        unsigned char *b = proc_malloc(cap);
        if (b == NULL) abort();
        if (l->n) memcpy(b, l->b, l->n);
        proc_free(l->b);
        l->b = b;
        l->cap = cap;
    }
    void *at = l->b + l->n;
    memset(at, 0, (len + 7) & ~(size_t)7);
    l->n = need;
    return at;
}

static void log_clear(struct actlog *l)
{
    l->n = 0;
    l->step_at = 0;
    l->steps = l->ticks = 0;
    l->bad = 0;
}

static void log_free(struct actlog *l)
{
    proc_free(l->b);
    memset(l, 0, sizeof *l);
}

static void log_step_begin(struct actlog *l)
{
    l->step_at = l->n;
    uint32_t *h = log_put(l, 8);
    h[0] = LOG_STEP;
}

static void log_step_end(struct actlog *l, int ticks, int error)
{
    ((uint32_t *)(l->b + l->step_at))[1] = (uint32_t)ticks;
    ++l->steps;
    l->ticks += ticks;
    if (error) l->bad = 1;
}

static void log_tick(struct actlog *l, int kind, int hold_only, const void *p, size_t len)
{
    struct log_ent *e = log_put(l, sizeof *e + len);
    e->kind = (uint8_t)kind;
    e->hold_only = (uint8_t)hold_only;
    e->len = (uint32_t)len;
    memcpy(e + 1, p, len);
}

/* ------------------------------------------------------------ the starts */

struct pool_start {
    char dir[4096];
    struct env *t, *twin;
    /* the nonzero pages of the image's live extents, as runs of pages */
    struct { uint64_t off, len; } *runs;
    size_t nruns, pages;
    /* the words that point into the image (offsets from its base) */
    uint64_t *ptrs;
    size_t nptrs, outside, value_diffs;
#ifdef POOL_CROSS_COUNT
    size_t scanned;             /* the count build: pages start_scan read */
#endif
    /* the image's used bytes (struct env to the heap's top, whole pages),
     * and its number among the starts runs were made for: an env in step
     * with it (pool_env wt_serial) copies back only what it wrote */
    size_t used;
    uint64_t serial;
    int64_t tick;
    /* the client world's Random from the start's tape rows (cwrand.h): set
     * in the image when cw_from is the start's tick, after the first row
     * when it is the next */
    uint64_t cw_seed;
    int32_t cw_lcg;
    int64_t cw_from;
    double load_ms;
    char err[640];
    int ok;
    /* cfg.save_slots: its holders (the caller, each env reset to it, each
     * save being made from it); it goes when the last lets go */
    int refs;
    /* a saved start (pool_start_from_env): while it is made (making 1), its
     * parent (the env's start), the env's act log, the save slot and the
     * replays' jobs left, images and views' memory */
    int making, slot, jobs_left;
    int rung;                       /* a save: the env's rung then (its image's cfg.rung): its replays run at it */
    struct pool_start *parent;
    struct actlog *log;
    struct env *rep[2];
    struct pool_vmem rvm[2];
    char rerr[2][320];
    int64_t replay_steps, replay_ticks;
    size_t log_bytes;
    /* its view: the t replay's library copy (vm: the mapping and the
     * segments, at the replay's addresses) and their bytes, with the words
     * that name the copy and those that name the image (offsets from vm.lo);
     * the image's words that name the copy (offsets from the image's base) */
    struct pool_vmem vm;
    unsigned char *vbytes;
    size_t nvbytes;
    /* the pages of vbytes: runs of the view's pages (offsets from vm.lo) a
     * fresh copy does not already hold (initialized data, or written) */
    struct { uint64_t off, len; } *vruns;
    size_t nvruns;
    uint64_t *vlib, *vimg, *libptrs;
    size_t nvlib, nvimg, nlibptrs;
    size_t view_diffs;          /* the view's words that differ and are not pointers */
    /* the frozen pending sets its image points to, a dimension each (start_image; a save's are its parent's):
     * one reference each (tbase_ref) */
    struct ticks_base *tbase[3];
    /* the overworld's chunks of its image packed (serverreplay.h struct sr_blobs, from the twin; a save's are its
     * parent's): one reference */
    struct sr_blobs *blobs;
};

/* ---------------------------------------------------------------- the envs */

enum { ENV_IDLE, ENV_QUEUED, ENV_DONE };

struct pool_env {
    int id, state;
    struct env *e;
    struct session *ss;
    /* the start the env was last reset to (never read after the reset: a
     * start may be freed once its resets are done), and what the step needs
     * of it: its tick and the client world's Random from its tape */
    struct pool_start *start;
    int64_t start_tick, cw_from;
    uint64_t cw_seed;
    int32_t cw_lcg;
    /* the frozen pending sets the env's image points to since its reset (its start's, which may be freed first):
     * one reference each, and its packed chunks */
    struct ticks_base *tbase[3];
    struct sr_blobs *blobs;
    int64_t t;
    int n, per_tick;
    struct pool_act *acts;          /* the queued step's acts (cap: obs ticks, grown) */
    int acts_cap;
    struct pool_result res;
    struct pool_feed_state *feed;
    uint64_t last_ins;
    int profiling;
    /* cfg.view_reset failed after the last reset: its steps fail with this */
    int view_bad;
    uint32_t epoch;                 /* resets so far (pool_epoch) */
    /* the write tracker (p->wt) watches the image (wt_on), which is the
     * start numbered wt_serial plus the pages written since (0: none) */
    int wt_on;
    uint64_t wt_serial;
    /* the runs the last episode wrote (merged across gaps): not protected
     * again, so the next episode's writes there take no fault, and every
     * reset copies back what is unprotected in them. A page stays
     * unprotected once written, so every POOL_HOT_EVERY resets the whole
     * image is protected and the next episode's writes name the runs anew. */
#define POOL_HOT 256
#ifndef POOL_HOT_EVERY
#define POOL_HOT_EVERY 4
#endif
    struct img_wrun hot[POOL_HOT];
    int nhot;
    uint32_t hot_resets;            /* delta resets since the last whole copy */
    int wt_prot;                    /* pages of the image may be protected */
    /* lane/resetcopy: what a delta reset protects again, every page written
     * since the last protect outside the hot runs: the scan's runs that found
     * no room among them and the zap log's (nprot -1: past room, and the
     * protect walks the whole image) */
    struct img_wrun prot[2 * POOL_HOT + IMG_ZAP_MAX];
    int nprot;
    char view_err[256];
    /* cfg.gen: the env's cache of chunks made ahead (in its image, made at
     * its first step after a reset); its predicted requests (process
     * memory: the round's thread reads them) and, after the round, each
     * one's result; the round's state (gstate 0 none, 1 in a round, 2 its
     * results here; gwait: a step waits for it; gdrop: the env was reset
     * meanwhile); predicted: the next step's generations were asked for at
     * the end of the last one; the tape's Dev tp entries (tps_ok) */
    struct genahead *ga;
    struct ga_req *greq;
    struct gen_res **gres;
    int ngreq, capreq;
    int gstate, gwait, gdrop, predicted;
    uint64_t gwait_start;
    struct gen_tp { int64_t t; double x, z; } *tps;
    int ntps, captps, tps_ok;
    uint64_t ga_ins, ga_ns;         /* the step's first half, before its round */
    int resetting, step_after;      /* async_reset: its reset queued or running; a step queued behind it */
    /* the server player's client stat mirror as the last step left it
     * (stats_ok 0: none since the reset), and its write count then: a step
     * after which the count stands wrote nothing, so has no events */
    int32_t *stats_v;
    uint8_t *stats_p;
    uint32_t stats_writes;
    int stats_ok;
    /* cfg.save_slots: the acts since the reset; and in a save's replay
     * (replay 1, an env of the save's own) the log's step being replayed,
     * [rp, rend) its ticks' entries */
    struct actlog log;
    int replay;
    int replay_rung;                /* a replay's rung: the saved env's (p->rung holds the n envs' only) */
    const unsigned char *rp, *rend;
};

/* One generation's result, held by a reference per request that asked for
 * it (an env keeps it until its tick takes the chunk, or drops it). Its
 * class: the raw ids alone (every metadata value 0: most raw chunks), the
 * ids and metas, or built (ga_gen run_built: an overworld chunk's header and
 * bands 0 to n - 1, class GR_BUILT + n). */
enum { GR_RAW, GR_RAW_METAS, GR_BUILT, GR_CLASSES = GR_BUILT + 17 };
struct gen_res {
    int refs;
    int cls;
    struct pool *pool;          /* whose free list it goes back to */
    uint64_t rand;
    uint8_t biome[256], tops[256];  /* biome's first bytes link a free result */
    struct ga_built built;      /* GR_BUILT's header */
    uint8_t data[];             /* raw: ids GA_CELLS, then metas GA_CELLS; built: the bands */
};

/* lane/resetcopy: an env image the pool holds for no env, with what the
 * tracker knows of it (pool_env's fields of the same names, which move with
 * the image): a reset to another start takes one that holds that start
 * already, and leaves its own (spare_trade) */
struct pool_img {
    struct env *e;
    int wt_on, wt_prot;
    uint64_t wt_serial;
    uint32_t hot_resets;
    uint64_t stamp;             /* when it last came in: the oldest goes first */
};
/* the held images: one for every POOL_SPARE_DIV envs */
#ifndef POOL_SPARE_DIV
#define POOL_SPARE_DIV 4
#endif

enum job_kind { J_STEP, J_RESET, J_LOAD, J_GENSTEP, J_SAVE, J_LOADBG };
struct job {
    int kind, id;               /* J_SAVE: id is the replay (0 the image, 1 its twin) */
    struct pool_start *start;
};

struct jobq {
    struct job *v;
    size_t cap, head, n;
};

struct pool {
    struct pool_config cfg;
    int n, nthreads;
    /* each env's rung from its next reset (env.h neth_rung), seeded from
     * cfg.rung or the config's fidelity.rung; reset_job applies one after
     * the image copy, so one start image serves every rung */
    int *rung;
    /* lane/ladder: each env's fidelity at the stripped rung (pool_rung_fid), rfid_on[id] 0 for stripped's own
     * (the four switches over cfg.fid) */
    struct fidelity *rfid;
    uint8_t *rfid_on;
    int rung_noted;                 /* the micro notice is out (once a pool) */
    struct pool_env *envs;
    pthread_t *th;
    pthread_mutex_t mu;
    pthread_cond_t work, done;
    /* the shared queue, and with cfg.pin nq more: one per worker (pin 1:
     * env i's steps and resets on worker i mod threads) or per cache domain
     * (pin 2: on worker i mod threads' domain); worker w takes wq[wqof[w]] */
    struct jobq q, *wq;
    int nq, *wqof;
    int *qidle, *qdom;              /* each queue's workers waiting for work, its cache domain (cfg.steal) */
    cpu_set_t mask;                 /* the CPUs the pool was made under (cfg.place) */
    int stop;
    /* finished steps, in finishing order */
    int *doneq;
    size_t done_head, ndone;
    int stepping;                   /* steps queued or running (atomic: a ring's step ends without the lock) */
    /* lane/balance, the rings (fast: one queue or a queue a domain, no device generation): pool_step's steps in
     * the env's queue's ring, which a worker claims without the lock (head) while its queues hold no other job,
     * and steals from the fullest other ring (cfg.steal); a step's end in dring, published by its sequence
     * (d_claim, then the slot's seq), taken under the lock (d_read); a consumer waiting on done names how many it
     * waits for (d_want, INT_MAX none) and only the step that brings them wakes it. Each worker's counts in wst,
     * summed by pool_stats. At 64 workers the lock's hold a job (the pop, the books, the wake) queued every
     * worker 24 us a step of 60. */
    int fast, nring;
    struct pool_ring {
        int *v;
        uint64_t tail, add;                         /* published; pool_step's, before its publish */
        uint64_t head __attribute__((aligned(64))); /* claimed */
    } __attribute__((aligned(64))) *rings;
    struct pool_dslot { uint64_t seq; int id; } *dring;
    uint64_t d_read;
    int d_want, nsleep;
    struct pool_wstat { uint64_t instructions, cycles, ns, steps, ticks; } __attribute__((aligned(64))) *wst;
    uint64_t d_claim __attribute__((aligned(64)));
    char ring_pad_ __attribute__((aligned(64)));
    int sync_left;                  /* the pool_reset or load call's jobs still running */
    /* pool_async_resets (lane/balance): pool_reset queues its jobs and returns; an env's step queued while its
     * reset runs goes in when the reset ends (its worker pushes it). At 16 workers on seed starts a group's
     * reset was one pool_reset a start (up to nine in turn, about seven envs each) with the main thread waiting
     * on each: 76 ms a group of 64, the workers idle 19% of the window */
    int async_reset, resets_left;
    int saving;                     /* saves being made (cfg.save_slots) */
    int loading;                    /* background loads queued or running (pool_start_load_bg) */
    unsigned save_busy;             /* their slots */
    /* the config every start must match (cfg.config), or the first
     * start's world keys (without one) */
    struct wconf wc;
    int have_conf;
    char world[4096];
    int have_world;
    struct pool_stats stats;
    struct img_wtrack *wt;          /* the envs' write tracker (image.h), NULL none: every reset copies the start */
    int pagemap;                    /* image_pagemap_open's: the pages an image holds (start_scan), -1 none */
    /* lane/resetcopy: images held for no env (spare_trade), nspare of them, made as they are first needed */
    struct pool_img *spare;
    int nspare;
    uint64_t spare_clock;
    struct gencache *gc;            /* cfg.gencache_mb's, NULL none */
    /* cfg.gen: the generation thread, the envs waiting for it (in order),
     * its env (the C generator's scratch), the counts (under mu) */
#define GEN_THREADS_MAX 4
    pthread_t gth[GEN_THREADS_MAX];
    struct gen_thread { struct pool *p; const struct ga_gen *gen; struct env *genv; } gt[GEN_THREADS_MAX];
    struct ga_gen *gen_more[GEN_THREADS_MAX];   /* the generators the pool made (ga_gen.another) */
    int ngth, gstop;
    pthread_cond_t gwork;
    int *gq;
    size_t gq_n;
    struct pool_gen_stats gst;
    /* results no request holds, kept for the next rounds (a fresh 131 KB
     * block faulted every page in: 58 us a result) */
    pthread_mutex_t gfree_mu;
    struct gen_res *gfree[GR_CLASSES];  /* by class */
    size_t ngfree[GR_CLASSES];
    uint64_t res_bytes, free_bytes;   /* results held, and on the free lists (under gfree_mu) */
    /* the last GEN_KEEP results made, by request, each holding a reference:
     * a later round's request for the same chunk (envs of one seed walk the
     * same ground) takes it instead of generating it again. Open addressing
     * (kt, r NULL empty) and the order they came in (ring); the rounds'
     * threads share it under gkeep_mu. */
    pthread_mutex_t gkeep_mu;
    struct gen_kept *kt;
    struct ga_req *kring;
    size_t krhead, krn;
};

/* what a job leaves for the end of its turn (cfg.gen), done under the pool's
 * lock once the worker has let go of the env */
enum gen_act { GA_NONE, GA_WAIT, GA_SUBMIT_WAIT, GA_SUBMIT_AHEAD };

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

static void seterr(char *err, size_t n, const char *fmt, ...)
{
    if (err == NULL || n == 0) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, n, fmt, ap);
    va_end(ap);
}

/* fidelity.rung when the caller set none: the config's (worldconf.c; its
 * engine.tick_scope: batched alias is already read as micro there), else
 * exact */
static int pool_rung_conf(const struct pool *p)
{
    if (p->cfg.rung != NETH_RUNG_UNSET) return p->cfg.rung;
    if (p->have_conf)
    {
        int r = neth_rung_parse(wconf_get(&p->wc, WC_FID_RUNG));
        if (r != NETH_RUNG_UNSET) return r;
    }
    return NETH_RUNG_EXACT;
}

/* fidelity.rung micro has no row of its own yet (S3: session_row_micro): it
 * steps as exact, and says so once a pool, as the engine.tick_scope: batched
 * read says its own (pipeline.c) */
static void rung_note(struct pool *p, int rung)
{
    if (rung != NETH_RUNG_MICRO || p->rung_noted) return;
    p->rung_noted = 1;
    fprintf(stderr, "%s: fidelity.rung: micro runs the exact row until the micro row is wired "
                    "(csrc/engine/session.c, session_row_micro)\n",
            p->cfg.config != NULL ? p->cfg.config : "pool");
}

/* ------------------------------------------------------------- the queue */

static void jobq_grow(struct jobq *q)
{
    size_t cap = q->cap ? 2 * q->cap : 256;
    struct job *v = malloc(cap * sizeof *v);
    for (size_t i = 0; i < q->n; ++i) v[i] = q->v[(q->head + i) % q->cap];
    free(q->v);
    q->v = v;
    q->cap = cap;
    q->head = 0;
}

static void jobq_push(struct jobq *q, struct job j)
{
    if (q->n == q->cap) jobq_grow(q);
    q->v[(q->head + q->n) % q->cap] = j;
    ++q->n;
}

/* ahead of every queued job (a step waiting on its generation round goes on first) */
static void jobq_push_front(struct jobq *q, struct job j)
{
    if (q->n == q->cap) jobq_grow(q);
    q->head = (q->head + q->cap - 1) % q->cap;
    q->v[q->head] = j;
    ++q->n;
}

static struct job jobq_pop(struct jobq *q)
{
    struct job j = q->v[q->head];
    q->head = (q->head + 1) % q->cap;
    --q->n;
    return j;
}

/* ------------------------------------------------ the hardware counter */

/* the thread's user instructions and cycles, one group read at once (-1
 * without the counters) */
static int pmu_open(void)
{
    struct perf_event_attr a;
    memset(&a, 0, sizeof a);
    a.size = sizeof a;
    a.type = PERF_TYPE_HARDWARE;
    a.config = PERF_COUNT_HW_INSTRUCTIONS;
    a.exclude_kernel = 1;
    a.exclude_hv = 1;
    a.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
    int fd = (int)syscall(SYS_perf_event_open, &a, 0, -1, -1, 0);
    a.config = PERF_COUNT_HW_CPU_CYCLES;
    if (fd >= 0 && syscall(SYS_perf_event_open, &a, 0, -1, fd, 0) < 0) { close(fd); fd = -1; }
    return fd;
}

/* the counts and the group's time enabled and on the PMU: a profiler's own
 * events multiplex with them (under perf stat they ran about half the
 * time), so a difference is scaled by the two times' (pmu_delta) */
struct pmu { uint64_t ins, cyc, en, run; };
static struct pmu pmu_read(int fd)
{
    uint64_t v[5] = {0};
    struct pmu c = {0, 0, 0, 0};
    if (fd < 0 || read(fd, v, sizeof v) != (ssize_t)sizeof v) return c;
    c.en = v[1];
    c.run = v[2];
    c.ins = v[3];
    c.cyc = v[4];
    return c;
}

static void pmu_delta(struct pmu a, struct pmu b, uint64_t *ins, uint64_t *cyc)
{
    uint64_t en = b.en - a.en, run = b.run - a.run;
    *ins = b.ins - a.ins;
    *cyc = b.cyc - a.cyc;
    if (run > 0 && run < en)
    {
        *ins = (uint64_t)((double)*ins * (double)en / (double)run);
        *cyc = (uint64_t)((double)*cyc * (double)en / (double)run);
    }
}

/* ----------------------------------------------------------- the load */

static char *read_first_line(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    char *line = NULL;
    size_t cap = 0;
    ssize_t len = getline(&line, &cap, f);
    fclose(f);
    if (len <= 0) { free(line); return NULL; }
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
    /* getline's buffer is the C library's: the copy is the current heap's
     * (the image's, during a load) */
    char *copy = strdup(line);
    free(line);
    return copy;
}

/* The world keys of a manifest or tape header ("world"), as one canonical
 * line: key=value in the order they appear. */
static void world_line(const struct jval *world, char *out, size_t n)
{
    size_t len = 0;
    out[0] = 0;
    for (int i = 0; world != NULL && i < world->nfields && len < n; ++i)
    {
        const char *v = json_str(world->fields[i].val);
        len += (size_t)snprintf(out + len, n - len, "%s=%s;", world->fields[i].key, v ? v : "?");
    }
}

/* test_snapshots' checks between the load and the first row that touch the
 * environment: the snapshot's entities built again as NBT in each
 * dimension's order (the entity digest's memo), then the overworld entered.
 * The pool's envs start where test_snapshots' replay starts. */
static void start_boundary(struct session *ss)
{
    struct snapshot *s = ss->s;
    struct serverreplay *sr = ss->sr;
    if (!ss->server_rows || s->version < 2) return;
    int k = 0, dim = -999;
    for (int i = 0; i < s->nents; ++i)
    {
        if (s->ents[i].player) continue;
        if (s->ents[i].dim != dim)
        {
            dim = s->ents[i].dim;
            serverreplay_enter(sr, dim);
            k = 0;
        }
        while (k < sr->d->nents && sr->d->ents[k].pool == 3) ++k;
        if (k >= sr->d->nents) continue;
        int gid;
        nbt *got = sr_entry_nbt(s, &ss->sp, &sr->d->ents[k], &gid);
        nbt_free(got);
        ++k;
    }
    serverreplay_enter(sr, 0);
}

/* The pending sets frozen (ticks.h ticks_load_all, lane/resettick): a start's backlog (the spawn start's 154,039
 * worldgen falling blocks, drained 1,000 a tick by every episode's first 154 ticks) is held once, outside the
 * images, and each env's set drains it by a cursor. A set under TBASE_MIN entries stays as it is. The references
 * are atomic: a reset (any worker) takes its start's and lets go of its last one's. */
#define TBASE_MIN 4096

static void tbase_ref(struct ticks_base *b)
{
    if (b != NULL) __atomic_add_fetch(&b->refs, 1, __ATOMIC_ACQ_REL);
}

static void tbase_unref(struct ticks_base *b)
{
    if (b != NULL && __atomic_sub_fetch(&b->refs, 1, __ATOMIC_ACQ_REL) == 0) ticks_base_free(b);
}

static void blobs_ref(struct sr_blobs *b)
{
    if (b != NULL) __atomic_add_fetch(&b->refs, 1, __ATOMIC_ACQ_REL);
}

static void blobs_unref(struct sr_blobs *b)
{
    if (b != NULL && __atomic_sub_fetch(&b->refs, 1, __ATOMIC_ACQ_REL) == 0) serverreplay_blobs_free(b);
}

/* the env's image now points to s's frozen sets and packed chunks: their references taken, its last ones' let go */
static void tbase_take(struct pool_env *pe, const struct pool_start *s)
{
    for (int i = 0; i < 3; ++i)
    {
        struct ticks_base *was = pe->tbase[i];
        pe->tbase[i] = s->tbase[i];
        tbase_ref(pe->tbase[i]);
        tbase_unref(was);
    }
    struct sr_blobs *was = pe->blobs;
    pe->blobs = s->blobs;
    blobs_ref(pe->blobs);
    blobs_unref(was);
}

/* the start's overworld chunks packed once, in its twin (whose image goes next: the pack drops its bands), and
 * the image pointed to them (lane/resettick: the park takes a chunk unwritten since the start from them) */
static void start_blobs(struct pool_start *s)
{
    struct env *was = neth_env;
    neth_env = s->twin;
    int heap = image_heap_set(s->twin, 1);
    struct session *tw = (struct session *)((unsigned char *)s->twin + s->twin->img.dir.session);
    if (tw->sr != NULL) s->blobs = serverreplay_start_blobs(tw->sr, 0);
    image_heap_set(s->twin, heap);
    neth_env = was;
    struct session *ts = (struct session *)((unsigned char *)s->t + s->t->img.dir.session);
    if (s->blobs != NULL && ts->sr != NULL) ts->sr->dims[0].blobs = s->blobs;
}

/* One load of dir into a new image environment. tbase: the image's sets frozen into it (st, the start), or the
 * twin's sets adopting them (st NULL: the same load, its entries checked equal) */
static struct env *start_image(struct pool *p, struct pool_start *st, const char *dir, char *err, size_t n,
                               struct ticks_base **tbase)
{
    struct env *e = env_new();
    if (e == NULL) { seterr(err, n, "no image environment (the address window is full)"); return NULL; }
    e->cfg.tick_fast = p->cfg.tick_fast;
    e->cfg.tick_batched = p->cfg.tick_batched;
    /* the image and its twin must end with the same livings in the same slots, which two independent sweeps do not
     * give (grave.h grave_sweep_joint): the load buries and does not sweep, start_load_job sweeps both together */
    e->grave.hold = 1;
    struct env *saved = neth_env;
    neth_env = e;
    if (p->cfg.region_spill != NULL && !rspill_flag(p->cfg.region_spill))
    {
        seterr(err, n, "region_spill takes on, off or an absolute directory, not %s", p->cfg.region_spill);
        neth_env = saved;
        env_free(e);
        return NULL;
    }
    int heap = image_heap_set(e, 1);
    struct env *ret = NULL;
    char path[4200];
    snprintf(path, sizeof path, "%s/tape.jsonl", dir);
    char *text = read_first_line(path);
    struct jval *hdr = text ? json_parse(text) : NULL;
    struct snapshot *snap = calloc(1, sizeof *snap);
    /* at its type's alignment (lane/livfault): the client player's _Alignas(64) fog tables (player.h) let the
     * compiler store to them with aligned vector moves, and the heap's blocks are 16-byte aligned, so a -march=znver5
     * build faulted on every worker in cp_send_motion_updates (2026-10-31 13:48, lane/cpuhot). The image frees the
     * block with itself, never on its own. */
    _Static_assert(_Alignof(struct session) <= 64, "the session's alignment");
    unsigned char *ss_raw = calloc(1, sizeof(struct session) + 64 - 16);
    struct session *ss = ss_raw ? (struct session *)(void *)(ss_raw + (-(uintptr_t)ss_raw & 63)) : NULL;
    if (hdr == NULL) { seterr(err, n, "%s: no tape header", path); goto out; }
#ifdef POOL_CROSS_COUNT
    double ci0 = now_ms();
#endif
    if (!snapshot_load(snap, dir)) { seterr(err, n, "%s: the snapshot does not load", dir); goto out; }
    if (session_refuse_dev(hdr, snap->manifest_json, p->cfg.dev_apply != NULL))
    {
        seterr(err, n, "%s: a dev start, refused (the product runs no dev ops)", dir);
        goto out;
    }
    /* a backlog loads straight into a base (ticks.h ticks_load_all): the image never holds its columns; the
     * twin's loads onto the image's */
    e->ticks.freeze_min = TBASE_MIN;
    for (int i = 0; i < 3; ++i) e->ticks.adopt[i] = st == NULL ? tbase[i] : NULL;
#ifdef POOL_CROSS_COUNT
    double ci1 = now_ms();
#endif
    int opened = session_open_at(ss, snap, dir, dir, hdr);
#ifdef POOL_CROSS_COUNT
    double ci2 = now_ms();
#endif
    const int nmade = e->ticks.nmade, adopt_bad = e->ticks.adopt_bad;
    struct ticks_base *made[3];
    memcpy(made, e->ticks.made, sizeof made);
    e->ticks.freeze_min = e->ticks.nmade = e->ticks.adopt_bad = 0;
    memset(e->ticks.made, 0, sizeof e->ticks.made);
    memset(e->ticks.adopt, 0, sizeof e->ticks.adopt);
    if (st != NULL)
        for (int k = 0; k < nmade; ++k) tbase[k] = made[k];
    if (!opened) { seterr(err, n, "%s: %s", dir, ss->err); goto out; }
    if (st == NULL && (adopt_bad || nmade != (tbase[0] != NULL) + (tbase[1] != NULL) + (tbase[2] != NULL)))
    {
        seterr(err, n, "%s: the twin's pending sets are not the image's", dir);
        goto out;
    }
    ss->dev_apply = p->cfg.dev_apply;
    ss->dev_end = p->cfg.dev_end;
    ss->start_difficulty = p->cfg.difficulty;
    start_boundary(ss);
    /* the run's fidelity (fidelity.h): the image holds it, every env copied from it too */
    if (!fidelity_vanilla(&p->cfg.fid) && ss->sr != NULL) fidelity_apply(&p->cfg.fid, ss->sr);
    /* what only the open reads goes: no dev op runs on a start that is not
     * a dev one (session_refuse_dev), so nothing binds the session again */
    if (!session_refuse_dev(hdr, snap->manifest_json, 0)) snapshot_drop_runtime(snap);
    if (ss->sr != NULL) serverreplay_start_keep(ss->sr);
    /* and what each base entry's tick does on the start's world (servertick_base_quiet) */
    if (ss->sr != NULL && st != NULL)
        for (int i = 0; i < 3; ++i)
            for (int k = 0; k < nmade; ++k)
                if (ss->sr->dims[i].ticks.base == tbase[k])
                    for (int w = 0; w < ss->sr->nworlds; ++w)
                        if (ss->sr->w[w].dim == ss->sr->dims[i].dim && ss->sr->w[w].st.w != NULL)
                            servertick_base_quiet(tbase[k], ss->sr->w[w].st.w);
#ifdef POOL_CROSS_COUNT
    double ci3 = now_ms();
#endif
    /* and the pages of every block the load freed: no env copies them */
    image_heap_trim(e);
#ifdef POOL_CROSS_COUNT
    fprintf(stderr, "CROSSIMG %s%s: snapshot %.1f, open %.1f, boundary to quiet %.1f, trim %.1f ms\n", dir, st ? "" : " (twin)", ci1 - ci0,
            ci2 - ci1, ci3 - ci2, now_ms() - ci3);
#endif
    uint64_t seed = 0;
    int32_t lcg = 0;
    int64_t from = -1;
    cwrand_from_tape(path, snap->tick, &seed, &lcg, &from);
    if (from == snap->tick) client_player_set_world_rand(&ss->cp, seed, lcg);
    if (st != NULL)
    {
        st->tick = snap->tick;
        st->cw_seed = seed;
        st->cw_lcg = lcg;
        st->cw_from = from;
    }
    ret = e;
out:
    image_heap_set(e, heap);
    neth_env = saved;
    if (ret == NULL) env_free(e);
    return ret;
}

/* The image's pages and pointers against its twin (the same load at
 * another address): a word that points into the image and differs from the
 * twin's is a pointer; any other difference is counted. */
static int scan_once(struct pool_start *s, int pagemap, char *err, size_t n)
{
    struct img_extent ext[64], ext2[64];
    int next = image_extents(s->t, ext, 64), next2 = image_extents(s->twin, ext2, 64);
    if (next != next2) { seterr(err, n, "the twin's extents differ (%d, %d)", next, next2); return 0; }
    /* a pointer is a word that points into the image and differs from the twin's at the same place, so the twin must
     * hold each living and list entry in the same slot: a word of a living the twin put one slot over is no pointer
     * to the scan, and a reset leaves it naming the start (lane/crash2: a chicken's `an`, an egg laid into the start's
     * chunk lists). Refuse the start rather than carry that */
    {
        const struct slot_pool *la = &s->t->arena.livings, *lb = &s->twin->arena.livings;
        const struct slot_pool *ea = &s->t->arena.an_ents, *eb = &s->twin->arena.an_ents;
        int bad = la->top != lb->top || la->live != lb->live || ea->top != eb->top || ea->live != eb->live;
        for (int32_t i = 0; !bad && i < la->top; ++i)
        {
            const struct living *a = pool_at(la, i), *b = pool_at(lb, i);
            bad = a->kind != b->kind || a->entity_id != b->entity_id || a->part != b->part;
        }
        for (int32_t i = 0; !bad && i < ea->top; ++i)
        {
            const struct an_ent *a = pool_at(ea, i), *b = pool_at(eb, i);
            bad = a->used != b->used || a->is_living != b->is_living || a->spawn_index != b->spawn_index;
        }
        if (bad)
        {
            seterr(err, n, "the twin's livings or list entries sit in other slots than the image's");
            return 0;
        }
    }
    const unsigned char *tb = (const unsigned char *)s->t, *wb = (const unsigned char *)s->twin;
    const uintptr_t lo = (uintptr_t)tb, lo2 = (uintptr_t)wb, size = s->t->img.size;
    /* a saved start's view: the replays' library copies (words naming the
     * same place in each are the image's pointers into its view) */
    const uintptr_t llo = s->rvm[0].lo, llo2 = s->rvm[1].lo, lsize = s->rvm[0].hi - s->rvm[0].lo;
    size_t cap_lib = 0;
    size_t cap_runs = 256, cap_ptrs = 1 << 16;
    s->runs = malloc(cap_runs * sizeof *s->runs);
    s->ptrs = malloc(cap_ptrs * sizeof *s->ptrs);
    uint64_t last_end = UINT64_MAX;
    /* pages in increasing order: the extents by offset (image_extents lists
     * them by part, not address) */
    for (int i = 1; i < next; ++i)
        for (int j = i; j > 0 && ext[j - 1].off > ext[j].off; --j)
        {
            struct img_extent x = ext[j]; ext[j] = ext[j - 1]; ext[j - 1] = x;
            x = ext2[j]; ext2[j] = ext2[j - 1]; ext2[j - 1] = x;
        }
    /* only the pages either image holds memory for (image_held): the
     * extents run over 421k pages for the seed starts' 7k nonzero ones, and
     * reading an unwritten page faults the zero page in (lane/crossreset:
     * the scan 212 ms of a 510 ms load); NULL: the kernel cannot tell, read
     * them all */
    size_t top = 0;
    for (int i = 0; i < next; ++i)
    {
        size_t e1 = ext[i].off + (ext[i].len > ext2[i].len ? ext[i].len : ext2[i].len);
        top = e1 > top ? e1 : top;
    }
    uint64_t *held = calloc((top / PAGE + 64) / 64, sizeof *held);
    for (int k = 0; k < 2 && held != NULL; ++k)
    {
        struct img_wrun r[256];
        for (size_t at = 0; at < top;)
        {
            long nr = image_held(pagemap, k ? s->twin : s->t, &at, top, r, sizeof r / sizeof *r);
            if (nr < 0) { free(held); held = NULL; break; }
            for (long i = 0; i < nr; ++i)
                for (size_t pg = r[i].lo / PAGE; pg < (r[i].hi + PAGE - 1) / PAGE && pg <= top / PAGE; ++pg)
                    held[pg / 64] |= (uint64_t)1 << (pg % 64);
        }
    }
    for (int i = 0; i < next; ++i)
    {
        size_t len = ext[i].len > ext2[i].len ? ext[i].len : ext2[i].len;
        if (ext[i].off != ext2[i].off)
        {
            seterr(err, n, "the twin's extent %d is at another offset", i);
            free(held);
            return 0;
        }
        uint64_t p0 = ext[i].off & ~(uint64_t)(PAGE - 1), p1 = (ext[i].off + len + PAGE - 1) & ~(uint64_t)(PAGE - 1);
        if (last_end != UINT64_MAX && p0 < last_end) p0 = last_end;
        for (uint64_t pg = p0; pg < p1; pg += PAGE)
        {
            if (held != NULL && !(held[pg / PAGE / 64] >> (pg / PAGE % 64) & 1)) continue;
#ifdef POOL_CROSS_COUNT
            ++s->scanned;
#endif
            const uint64_t *a = (const uint64_t *)(tb + pg), *b = (const uint64_t *)(wb + pg);
            uint64_t any = 0;
            for (size_t k = 0; k < PAGE / 8; ++k) any |= a[k] | b[k];
            if (!any) continue;
            for (size_t k = 0; k < PAGE / 8; ++k)
            {
                if (a[k] == b[k]) continue;
                if ((uintptr_t)a[k] - lo < size && (uintptr_t)b[k] - lo2 < size && a[k] - lo == b[k] - lo2)
                {
                    if (s->nptrs == cap_ptrs) s->ptrs = realloc(s->ptrs, (cap_ptrs *= 2) * sizeof *s->ptrs);
                    s->ptrs[s->nptrs++] = pg + 8 * k;
                }
                else if (lsize && (uintptr_t)a[k] - llo < lsize && (uintptr_t)b[k] - llo2 < lsize &&
                         a[k] - llo == b[k] - llo2)
                {
                    if (s->nlibptrs == cap_lib)
                    {
                        uint64_t *v = proc_malloc((cap_lib = cap_lib ? 2 * cap_lib : 256) * sizeof *v);
                        if (s->nlibptrs) memcpy(v, s->libptrs, s->nlibptrs * sizeof *v);
                        proc_free(s->libptrs);
                        s->libptrs = v;
                    }
                    s->libptrs[s->nlibptrs++] = pg + 8 * k;
                }
                else if ((uintptr_t)a[k] - lo < size || (uintptr_t)b[k] - lo2 < size) ++s->value_diffs;
                else if (image_owner((const void *)(uintptr_t)a[k]) == NULL && a[k] > 0x10000 && b[k] > 0x10000 &&
                         (a[k] >> 40) == (b[k] >> 40))
                    ++s->outside;
                else ++s->value_diffs;
            }
            if (s->nruns && s->runs[s->nruns - 1].off + s->runs[s->nruns - 1].len == pg) s->runs[s->nruns - 1].len += PAGE;
            else
            {
                if (s->nruns == cap_runs) s->runs = realloc(s->runs, (cap_runs *= 2) * sizeof *s->runs);
                s->runs[s->nruns].off = pg;
                s->runs[s->nruns].len = PAGE;
                ++s->nruns;
            }
            ++s->pages;
        }
        if (p1 > last_end || last_end == UINT64_MAX) last_end = p1;
    }
    free(held);
    s->used = ((size_t)s->t->img.heap_off + s->t->img.heap_used + PAGE - 1) & ~(size_t)(PAGE - 1);
    static uint64_t serials;
    s->serial = __atomic_add_fetch(&serials, 1, __ATOMIC_RELAXED);
    return 1;
}

/* the scan (scan_once), on the pages either image holds where the kernel
 * can list them; the verify build (-DPOOL_SCAN_VERIFY, lane/crossreset)
 * scans every page of the extents again, as before, and compares the runs,
 * the pointers and the counts, one line a load */
static int start_scan(struct pool_start *s, int pagemap, char *err, size_t n)
{
#ifndef POOL_SCAN_VERIFY
    return scan_once(s, pagemap, err, n);
#else
    int ok = scan_once(s, pagemap, err, n);
    if (!ok) return 0;
    struct pool_start a = *s;
    s->runs = NULL;
    s->ptrs = NULL;
    s->libptrs = NULL;
    s->nruns = s->pages = s->nptrs = s->nlibptrs = s->outside = s->value_diffs = 0;
    int ok2 = scan_once(s, -1, err, n);
    static uint64_t loads, bad;
    int same = ok2 && a.nruns == s->nruns && a.pages == s->pages && a.nptrs == s->nptrs && a.nlibptrs == s->nlibptrs &&
               a.outside == s->outside && a.value_diffs == s->value_diffs &&
               memcmp(a.runs, s->runs, a.nruns * sizeof *a.runs) == 0 && memcmp(a.ptrs, s->ptrs, a.nptrs * sizeof *a.ptrs) == 0 &&
               (a.nlibptrs == 0 || memcmp(a.libptrs, s->libptrs, a.nlibptrs * sizeof *a.libptrs) == 0);
    uint64_t k = __atomic_add_fetch(&loads, 1, __ATOMIC_RELAXED);
    if (!same) __atomic_add_fetch(&bad, 1, __ATOMIC_RELAXED);
    fprintf(stderr, "SCANVERIFY %lu loads, %lu differ: %s %s (runs %zu/%zu, pages %zu/%zu, pointers %zu/%zu, library %zu/%zu, "
            "outside %zu/%zu, values %zu/%zu)\n", (unsigned long)k, (unsigned long)bad, s->dir, same ? "same" : "DIFFER",
            a.nruns, s->nruns, a.pages, s->pages, a.nptrs, s->nptrs, a.nlibptrs, s->nlibptrs, a.outside, s->outside,
            a.value_diffs, s->value_diffs);
    free(s->runs);
    free(s->ptrs);
    proc_free(s->libptrs);
    s->runs = a.runs;
    s->ptrs = a.ptrs;
    s->libptrs = a.libptrs;
    s->nruns = a.nruns;
    s->pages = a.pages;
    s->nptrs = a.nptrs;
    s->nlibptrs = a.nlibptrs;
    s->outside = a.outside;
    s->value_diffs = a.value_diffs;
    s->serial = a.serial;
    return 1;
#endif
}

/* lane/gpu-workstation-roofline: a start's image is read only once scanned (a reset copies its runs, the nonzero pages, and
 * zeroes the rest of the env's), but the load leaves pages it wrote back to zero resident: 13 of a start's 49 MB on
 * the state bank's starts (49 resident against 36 nonzero). Each resident page of the image below its used top that
 * is all zero goes back to the kernel (image_give_pages: madvise, so it reads zero again and costs nothing until
 * written). Content is unchanged by construction: only a page that reads zero is given back. -DPOOL_KEEP_ZERO keeps
 * them (the before build of an A/B). */
static void start_drop_zero(struct pool_start *s)
{
#if defined(__linux__) && !defined(POOL_KEEP_ZERO)
    struct env *t = s->t;
    if (t == NULL || t->img.track || s->used == 0) return;
    size_t np = s->used / PAGE;
    unsigned char *vec = malloc(np ? np : 1);
    if (vec == NULL) return;
    if (mincore((void *)t, np * PAGE, vec) != 0) { free(vec); return; }
    unsigned char *base = (unsigned char *)t;
    size_t run = 0, at = 0;
    for (size_t i = 1; i <= np; ++i)   /* page 0 holds struct env: never given back */
    {
        int zero = 0;
        if (i < np && (vec[i] & 1))
        {
            const uint64_t *w = (const uint64_t *)(base + i * PAGE);
            uint64_t any = 0;
            for (size_t k = 0; k < PAGE / 8; ++k) any |= w[k];
            zero = any == 0;
        }
        if (zero) { if (!run) at = i; ++run; continue; }
        if (run) image_give_pages(t, base + at * PAGE, run * PAGE);
        run = 0;
    }
    free(vec);
#else
    (void)s;
#endif
}

static void start_load_job(struct pool *p, struct pool_start *s)
{
    double t0 = now_ms();
    s->t = start_image(p, s, s->dir, s->err, sizeof s->err, s->tbase);
#ifdef POOL_CROSS_COUNT
    double t1 = now_ms();
#endif
    if (s->t) s->twin = start_image(p, NULL, s->dir, s->err, sizeof s->err, s->tbase);
#ifdef POOL_CROSS_COUNT
    double t2 = now_ms();
#endif
    if (s->t && s->twin) grave_sweep_joint(s->t, s->twin);
    if (s->t && s->twin && start_scan(s, p->pagemap, s->err, sizeof s->err)) s->ok = 1;
    if (s->ok) start_drop_zero(s);
#ifdef POOL_CROSS_COUNT
    double t3 = now_ms();
#endif
    if (s->ok) start_blobs(s);    /* the twin has named the pointers: it is not needed again */
#ifdef POOL_CROSS_COUNT
    double t4 = now_ms();
#endif
    if (s->twin) { env_free(s->twin); s->twin = NULL; }
    s->load_ms = now_ms() - t0;
#ifdef POOL_CROSS_COUNT
    fprintf(stderr, "CROSSLOAD %s: %.1f ms: image %.1f, twin %.1f, scan %.1f, blobs %.1f, free %.1f; %zu pages, %zu runs, %zu ptrs, %zu pages scanned\n",
            s->dir, s->load_ms, t1 - t0, t2 - t1, t3 - t2, t4 - t3, now_ms() - t4, s->pages, s->nruns, s->nptrs, s->scanned);
#endif
}

static int start_world_check(struct pool *p, struct pool_start *s);

/* ----------------------------------------------------------- the reset */

static void gen_reset(struct pool *p, struct pool_env *pe);

static void start_destroy(struct pool_start *s)
{
    env_free(s->t);
    env_free(s->twin);
    for (int i = 0; i < 2; ++i)
        if (s->rep[i] != NULL && s->rep[i] != s->t && s->rep[i] != s->twin) env_free(s->rep[i]);
    free(s->runs);
    free(s->ptrs);
    if (s->log) { log_free(s->log); proc_free(s->log); }
    proc_free(s->vbytes);
    proc_free(s->vruns);
    proc_free(s->vlib);
    proc_free(s->vimg);
    proc_free(s->libptrs);
    for (int i = 0; i < 3; ++i) tbase_unref(s->tbase[i]);
    blobs_unref(s->blobs);
    free(s);
}

/* one holder of s lets go (cfg.save_slots; any thread) */
static void start_unref(struct pool_start *s)
{
    if (__atomic_sub_fetch(&s->refs, 1, __ATOMIC_ACQ_REL) == 0) start_destroy(s);
}

static void start_ref(struct pool_start *s) { __atomic_add_fetch(&s->refs, 1, __ATOMIC_ACQ_REL); }

#if defined(POOL_RESET_VERIFY) || defined(POOL_RESET_COUNT)
/* the image's parts by the start's extents (an extent's region runs to the
 * next one's start): the pages of [lo, hi) added to acc by part */
#define RV_PARTS 40
__attribute__((unused)) static const char *const rv_names[RV_PARTS] = {"env", "paths", "paths.next", "paths.gen", "livings", "livings.next", "livings.gen",
    "ai_parts", "ai_parts.next", "ai_parts.gen", "villager_parts", "villager_parts.next", "villager_parts.gen", "player_parts",
    "player_parts.next", "player_parts.gen", "an_ents", "an_ents.next", "an_ents.gen", "ie_ents", "ie_ents.next", "ie_ents.gen",
    "fh_ents", "fh_ents.next", "fh_ents.gen", "doors", "doors.next", "doors.gen", "bands", "bands.next", "bands.gen", "nibs",
    "nibs.next", "nibs.gen", "chunks", "chunks.next", "chunks.gen", "slab", "heap", "arena"};
__attribute__((unused)) static void rv_tally(const unsigned char *tb, size_t lo, size_t hi, uint64_t *acc)
{
    const struct env *T = (const struct env *)tb;
    const struct arena_env *a = &T->arena;
    const struct slot_pool *pools[] = {&a->paths, &a->livings, &a->ai_parts, &a->villager_parts, &a->player_parts,
                                       &a->an_ents, &a->ie_ents, &a->fh_ents, &a->doors, &a->bands, &a->nibs, &a->chunks};
    size_t off[40];
    off[0] = 0;
    for (int i = 0; i < 12; ++i)
    {
        off[1 + 3 * i] = (size_t)((const unsigned char *)pools[i]->base - tb);
        off[2 + 3 * i] = (size_t)((const unsigned char *)pools[i]->next - tb);
        off[3 + 3 * i] = (size_t)((const unsigned char *)pools[i]->gen - tb);
    }
    off[37] = (size_t)((const unsigned char *)a->slab.base - tb);
    off[38] = (size_t)T->img.heap_off;
    off[39] = (sizeof(struct env) + PAGE - 1) & ~(size_t)(PAGE - 1);
    for (size_t pg = lo; pg < hi; pg += PAGE)
    {
        int best = 0;
        for (int i = 0; i < 40; ++i)
            if (off[i] <= pg && off[i] >= off[best]) best = i;
        __atomic_add_fetch(&acc[best], 1, __ATOMIC_RELAXED);
    }
}
#endif

#ifdef POOL_RESET_COUNT
/* the count build (lane/resetcopy): each reset's time by phase, its pages and
 * its page faults, by kind (an env's first, to its last start, to another)
 * and path (by the pages written, or the whole copy); a RESETCOUNT line
 * every 256 resets. POOL_RESET_COUNT=2 also reads each page before the
 * restore writes it, by part (RESETREG): the copied pages that held the
 * start's bytes (rebased) already, the zeroed ones that were zero, and on a
 * whole copy the gap pages handed back that were resident and the start's
 * pages it wrote that were not (a fault each) */
enum { RC_SCAN, RC_COPY, RC_ZERO, RC_REBASE, RC_MADV, RC_PROT, RC_UNPROT, RC_ALL, RC_T };
enum { RC_PCOPY, RC_PZERO, RC_PMADV, RC_PTRS, RC_FLT, RC_HOT, RC_ZAPS, RC_P };
static const char *const rc_tn[RC_T] = {"scan", "copy", "zero", "rebase", "madvise", "protect", "unprotect", "all"};
static const char *const rc_pn[RC_P] = {"copied", "zeroed", "handed back", "pointers", "faults", "hot", "zap entries"};
static uint64_t rc_n[3][2], rc_t[3][2][RC_T], rc_p[3][2][RC_P], rc_total;
enum { RG_COPY, RG_SAME, RG_ZERO, RG_WASZERO, RG_GAVE, RG_FAULT, RG_N };
__attribute__((unused)) static const char *const rg_names[RG_N] = {"copied", "copied same", "zeroed", "zeroed zero", "whole: handed back resident",
                                           "whole: written not resident"};
__attribute__((unused)) static uint64_t rc_reg[3][RG_N][RV_PARTS];
static __thread uint64_t rc_tt[RC_T], rc_tp[RC_P];
static __thread int rc_kind;
#define RC_TIME(k, stmt) do { const uint64_t q0_ = now_ns(); stmt; rc_tt[k] += now_ns() - q0_; } while (0)
static uint64_t rc_minflt(void)
{
    struct rusage u;
    return getrusage(RUSAGE_THREAD, &u) == 0 ? (uint64_t)u.ru_minflt : 0;
}
#if POOL_RESET_COUNT >= 3
static uint64_t rc_lines[3][6][RV_PARTS];
#endif
#if POOL_RESET_COUNT >= 2
/* [lo, hi) of E about to take T's bytes: the pages already equal */
static void rc_same(const unsigned char *eb, const unsigned char *tb, int64_t delta, const struct pool_start *s, size_t lo,
                    size_t hi)
{
    uint64_t want[PAGE / 8];
    size_t pi = 0, y = s->nptrs;
    while (pi < y)
    {
        size_t m = (pi + y) / 2;
        if (s->ptrs[m] < lo) pi = m + 1;
        else y = m;
    }
    rv_tally(tb, lo, hi, rc_reg[rc_kind][RG_COPY]);
    for (size_t pg = lo; pg < hi; pg += PAGE)
    {
        memcpy(want, tb + pg, PAGE);
        for (; pi < s->nptrs && s->ptrs[pi] < pg + PAGE; ++pi) want[(s->ptrs[pi] - pg) / 8] += (uint64_t)delta;
        if (memcmp(want, eb + pg, PAGE) == 0) rv_tally(tb, pg, pg + PAGE, rc_reg[rc_kind][RG_SAME]);
#if POOL_RESET_COUNT >= 3
        /* the page's 64-byte lines that differ: 0, 1, 2-4, 5-16, 17-63, 64 */
        int d = 0;
        for (int l = 0; l < 64; ++l) d += memcmp((const char *)want + 64 * l, eb + pg + 64 * l, 64) != 0;
        const int bkt = d == 0 ? 0 : d == 1 ? 1 : d <= 4 ? 2 : d <= 16 ? 3 : d < 64 ? 4 : 5;
        rv_tally(tb, pg, pg + PAGE, rc_lines[rc_kind][bkt]);
#endif
    }
}
/* [lo, hi) of E about to be zeroed: the pages zero already */
static void rc_waszero(const unsigned char *eb, const unsigned char *tb, size_t lo, size_t hi)
{
    rv_tally(tb, lo, hi, rc_reg[rc_kind][RG_ZERO]);
    for (size_t pg = lo; pg < hi; pg += PAGE)
    {
        const uint64_t *a = (const uint64_t *)(eb + pg);
        uint64_t any = 0;
        for (size_t w = 0; w < PAGE / 8; ++w) any |= a[w];
        if (!any) rv_tally(tb, pg, pg + PAGE, rc_reg[rc_kind][RG_WASZERO]);
    }
}
/* [lo, hi) of E: its resident pages (resident 1) or the others, tallied in rc_reg[kind][g] */
static void rc_resident(const unsigned char *eb, const unsigned char *tb, size_t lo, size_t hi, int resident, int g)
{
    unsigned char v[4096];
    for (size_t at = lo; at < hi; at += sizeof v * PAGE)
    {
        size_t len = hi - at < sizeof v * PAGE ? hi - at : sizeof v * PAGE;
        if (mincore((void *)(eb + at), len, v) != 0) continue;
        for (size_t k = 0; k < len / PAGE; ++k)
            if ((v[k] & 1) == resident) rv_tally(tb, at + k * PAGE, at + (k + 1) * PAGE, rc_reg[rc_kind][g]);
    }
}
#endif
static void rc_end(int whole, uint64_t t0, uint64_t f0)
{
    static const char *const kinds[3] = {"first", "same", "other"};
    rc_tt[RC_ALL] = now_ns() - t0;
    rc_tp[RC_FLT] = rc_minflt() - f0;
    const int k = rc_kind;
    __atomic_add_fetch(&rc_n[k][whole], 1, __ATOMIC_RELAXED);
    for (int i = 0; i < RC_T; ++i) __atomic_add_fetch(&rc_t[k][whole][i], rc_tt[i], __ATOMIC_RELAXED);
    for (int i = 0; i < RC_P; ++i) __atomic_add_fetch(&rc_p[k][whole][i], rc_tp[i], __ATOMIC_RELAXED);
    memset(rc_tt, 0, sizeof rc_tt);
    memset(rc_tp, 0, sizeof rc_tp);
    if (__atomic_add_fetch(&rc_total, 1, __ATOMIC_RELAXED) % 256 != 0) return;
    for (int a = 0; a < 3; ++a)
        for (int w = 0; w < 2; ++w)
        {
            const double n = (double)rc_n[a][w];
            if (n == 0) continue;
            fprintf(stderr, "RESETCOUNT %s %s %.0f: %.3f ms (", kinds[a], w ? "whole" : "delta", n, (double)rc_t[a][w][RC_ALL] / n / 1e6);
            for (int i = 0; i < RC_ALL; ++i) fprintf(stderr, "%s%s %.3f", i ? ", " : "", rc_tn[i], (double)rc_t[a][w][i] / n / 1e6);
            fprintf(stderr, ");");
            for (int i = 0; i < RC_P; ++i) fprintf(stderr, " %s %.0f", rc_pn[i], (double)rc_p[a][w][i] / n);
            fprintf(stderr, "\n");
        }
#if POOL_RESET_COUNT >= 2
    for (int a = 0; a < 3; ++a)
    {
        const double n = (double)(rc_n[a][0] + rc_n[a][1]);
        if (n == 0) continue;
        for (int g = 0; g < RG_N; ++g)
        {
            uint64_t all = 0;
            for (int i = 0; i < RV_PARTS; ++i) all += rc_reg[a][g][i];
            fprintf(stderr, "RESETREG %s %s %.1f a reset:", kinds[a], rg_names[g], (double)all / n);
            for (int i = 0; i < RV_PARTS; ++i)
                if ((double)rc_reg[a][g][i] / n >= 1) fprintf(stderr, " %s %.0f", rv_names[i], (double)rc_reg[a][g][i] / n);
            fprintf(stderr, "\n");
        }
#if POOL_RESET_COUNT >= 3
        static const char *const bk[6] = {"0", "1", "2-4", "5-16", "17-63", "64"};
        for (int b = 0; b < 6; ++b)
        {
            fprintf(stderr, "RESETLINES %s copied pages with %s lines differing:", kinds[a], bk[b]);
            for (int i = 0; i < RV_PARTS; ++i)
                if ((double)rc_lines[a][b][i] / n >= 1) fprintf(stderr, " %s %.0f", rv_names[i], (double)rc_lines[a][b][i] / n);
            fprintf(stderr, "\n");
        }
#endif
    }
#endif
}
#else
#define RC_TIME(k, stmt) do { stmt; } while (0)
#endif

#ifdef POOL_RESET_VERIFY
/* the verify build (lane/resetimg): every reset's image against what the
 * whole copy makes, page by page over the used extent; one line every 256
 * resets with the delta resets, the pages they copied and zeroed, and the
 * pages that differ */
static uint64_t rv_resets, rv_delta, rv_copied, rv_zeroed, rv_bad, rv_badresets, rv_hot, rv_hotpages, rv_hotfull;
static uint64_t rv_zap, rv_zapfull;   /* the zap log's entries at a reset, and the resets that found it full */
#define RV_ADD(x, n) __atomic_add_fetch(&(x), (n), __ATOMIC_RELAXED)
/* the pages copied (0) and zeroed (1) */
static uint64_t rv_part[2][RV_PARTS];
static void rv_count(const unsigned char *tb, size_t lo, size_t hi, int zeroed) { rv_tally(tb, lo, hi, rv_part[zeroed]); }
static void rv_report(void)
{
    fprintf(stderr, "RESETPARTS a delta reset's pages by part (copied/zeroed):");
    for (int i = 0; i < 40; ++i)
        if (rv_part[0][i] + rv_part[1][i] > rv_delta)
            fprintf(stderr, " %s %.0f/%.0f", rv_names[i], (double)rv_part[0][i] / rv_delta, (double)rv_part[1][i] / rv_delta);
    fprintf(stderr, "\n");
}

static void reset_verify_end(struct pool_env *pe, const struct pool_start *s, size_t end, int delta_path)
{
    const unsigned char *eb = (const unsigned char *)pe->e, *tb = (const unsigned char *)s->t;
    const int64_t delta = (int64_t)((uintptr_t)eb - (uintptr_t)tb);
    enum { VEC = 1 << 16 };
    unsigned char *vec = malloc(VEC);
    uint64_t want[PAGE / 8];
    uint64_t bad = 0;
    size_t ri = 0, pi = 0;
    for (size_t at = 0; at < end; at += (size_t)VEC * PAGE)
    {
        size_t len = end - at < (size_t)VEC * PAGE ? end - at : (size_t)VEC * PAGE;
        if (mincore((void *)(eb + at), len, vec) != 0) memset(vec, 1, VEC);
        for (size_t k = 0; k < len / PAGE; ++k)
        {
            size_t pg = at + k * PAGE;
            while (ri < s->nruns && s->runs[ri].off + s->runs[ri].len <= pg) ++ri;
            int in = ri < s->nruns && s->runs[ri].off <= pg;
            if (!in)
            {
                if (!(vec[k] & 1)) continue;
                const uint64_t *a = (const uint64_t *)(eb + pg);
                uint64_t any = 0;
                for (size_t w = 0; w < PAGE / 8; ++w) any |= a[w];
                bad += any != 0;
                continue;
            }
            memcpy(want, tb + pg, PAGE);
            while (pi < s->nptrs && s->ptrs[pi] < pg) ++pi;
            for (size_t q = pi; q < s->nptrs && s->ptrs[q] < pg + PAGE; ++q) want[(s->ptrs[q] - pg) / 8] += (uint64_t)delta;
            bad += memcmp(want, eb + pg, PAGE) != 0;   /* a page not resident may be in swap: read it */
        }
    }
    free(vec);
    uint64_t n = RV_ADD(rv_resets, 1);
    if (delta_path) RV_ADD(rv_delta, 1);
    for (int i = 0; i < pe->nhot; ++i) RV_ADD(rv_hotpages, (pe->hot[i].hi - pe->hot[i].lo) / PAGE);
    RV_ADD(rv_hot, (uint64_t)pe->nhot);
    if (pe->nhot == POOL_HOT) RV_ADD(rv_hotfull, 1);
    if (bad) { RV_ADD(rv_bad, bad); RV_ADD(rv_badresets, 1); }
    if (n % 256 == 0 || bad)
        fprintf(stderr, "RESETVERIFY %lu resets, %lu by the pages written (%.0f copied, %.0f zeroed a reset), %lu differ in %lu pages\n",
                (unsigned long)n, (unsigned long)rv_delta, rv_delta ? (double)rv_copied / rv_delta : 0.0,
                rv_delta ? (double)rv_zeroed / rv_delta : 0.0, (unsigned long)rv_badresets, (unsigned long)rv_bad);
    if (n % 256 == 0 && rv_delta)
    {
        rv_report();
        fprintf(stderr, "RESETHOT %.1f hot runs, %.0f pages a delta reset; %lu resets with the list full; the zap log %.1f "
                "entries a reset, full at %lu\n", (double)rv_hot / rv_delta, (double)rv_hotpages / rv_delta,
                (unsigned long)rv_hotfull, (double)rv_zap / n, (unsigned long)rv_zapfull);
    }
}
#endif

#ifdef POOL_CROSS_COUNT
/* the count build (lane/crossreset): resets by kind (an env's first, to its last start, to another), the time of
 * each kind's restore, and at a reset to another start the pages of the new start's runs E already holds byte for
 * byte (rebased) and E's nonzero pages outside them */
static uint64_t cc_n[3], cc_whole[3], cc_ns[3], cc_tm[3], cc_tc[3], cc_tr[3], cc_same, cc_diff, cc_out, cc_outz, cc_absent;
#define CC_ADD(x, n) __atomic_add_fetch(&(x), (n), __ATOMIC_RELAXED)
/* E's resident pages in [lo, hi): nonzero ones (*out) need zeros */
static void cc_gap(const unsigned char *eb, size_t lo, size_t hi, uint64_t *out, uint64_t *outz)
{
    enum { VEC = 1 << 16 };
    unsigned char *vec = malloc(VEC);
    for (size_t at = lo; at < hi; at += (size_t)VEC * PAGE)
    {
        size_t len = hi - at < (size_t)VEC * PAGE ? hi - at : (size_t)VEC * PAGE;
        if (mincore((void *)(eb + at), len, vec) != 0) continue;
        for (size_t k = 0; k < len / PAGE; ++k)
        {
            if (!(vec[k] & 1)) continue;
            const uint64_t *a = (const uint64_t *)(eb + at + k * PAGE);
            uint64_t any = 0;
            for (size_t w = 0; w < PAGE / 8; ++w) any |= a[w];
            if (any) ++*out;
            else ++*outz;
        }
    }
    free(vec);
}
static void cc_share(const struct pool_env *pe, const struct pool_start *s, size_t used, int64_t delta)
{
    const unsigned char *eb = (const unsigned char *)pe->e, *tb = (const unsigned char *)s->t;
    uint64_t want[PAGE / 8], same = 0, diff = 0, out = 0, outz = 0;
    size_t pi = 0, at = 0;
    for (size_t i = 0; i < s->nruns; ++i)
    {
        unsigned char v[1024];
        const size_t np = s->runs[i].len / PAGE;
        if (np > sizeof v || mincore((void *)(eb + s->runs[i].off), s->runs[i].len, v) != 0) memset(v, 1, sizeof v);
        for (size_t pg = s->runs[i].off; pg < s->runs[i].off + s->runs[i].len; pg += PAGE)
        {
            if (!(v[(pg - s->runs[i].off) / PAGE] & 1)) { CC_ADD(cc_absent, 1); continue; }   /* not read: E maps no page there */
            memcpy(want, tb + pg, PAGE);
            while (pi < s->nptrs && s->ptrs[pi] < pg) ++pi;
            for (size_t q = pi; q < s->nptrs && s->ptrs[q] < pg + PAGE; ++q) want[(s->ptrs[q] - pg) / 8] += (uint64_t)delta;
            if (memcmp(want, eb + pg, PAGE) == 0) ++same;
            else ++diff;
        }
        cc_gap(eb, at, s->runs[i].off < used ? s->runs[i].off : used, &out, &outz);
        at = s->runs[i].off + s->runs[i].len;
    }
    cc_gap(eb, at, used, &out, &outz);
    CC_ADD(cc_same, same);
    CC_ADD(cc_diff, diff);
    CC_ADD(cc_out, out);
    CC_ADD(cc_outz, outz);
}
static uint64_t cc_k, cc_sampled, cc_waits, cc_wait_ns;
/* a reset's times (not one whose pages cc_share read first: one in 8 resets to another start) */
static void cc_end(int kind, int whole, uint64_t ns, uint64_t tm, uint64_t tc, uint64_t tr, int sampled)
{
    static const char *const names[3] = {"first", "same", "other"};
    if (sampled) { CC_ADD(cc_sampled, 1); return; }
    uint64_t n = CC_ADD(cc_n[kind], 1);
    CC_ADD(cc_ns[kind], ns);
    CC_ADD(cc_tm[kind], tm);
    CC_ADD(cc_tc[kind], tc);
    CC_ADD(cc_tr[kind], tr);
    if (whole) CC_ADD(cc_whole[kind], 1);
    if (n % 64 != 0) return;
    fprintf(stderr, "CROSSRESET");
    for (int k = 0; k < 3; ++k)
        fprintf(stderr, " %s %lu (%lu whole, %.3f ms: madvise %.3f, copy %.3f, rebase %.3f)", names[k], (unsigned long)cc_n[k],
                (unsigned long)cc_whole[k], cc_n[k] ? (double)cc_ns[k] / cc_n[k] / 1e6 : 0.0,
                cc_whole[k] ? (double)cc_tm[k] / cc_whole[k] / 1e6 : 0.0, cc_whole[k] ? (double)cc_tc[k] / cc_whole[k] / 1e6 : 0.0,
                cc_whole[k] ? (double)cc_tr[k] / cc_whole[k] / 1e6 : 0.0);
    double o = cc_sampled ? (double)cc_sampled : 1.0;
    fprintf(stderr, "; to another start, a reset: %.0f pages held already, %.0f differ, %.0f nonzero outside (%.0f "
            "resident zero); %.0f of its pages not resident in E; %lu resets waited on their start's load, %.1f ms in all\n", (double)cc_same / o, (double)cc_diff / o, (double)cc_out / o, (double)cc_outz / o, (double)cc_absent / o, (unsigned long)cc_waits, (double)cc_wait_ns / 1e6);
}
#endif

/* T's bytes over E's [lo, hi) (whole pages inside s's runs), its pointers
 * rebased, by the 64-byte lines that differ (lane/resetcopy): most pages an
 * episode wrote differ from the start in a few lines (a band's sharer
 * count, a chunk's stamps, a living's fields), and a line already right is
 * read, not written again */
#if defined(__x86_64__)
/* a page of restore_lines in AVX-512 lanes, a line a compare, the next
 * page's lines prefetched first (the stream prefetcher stops at a page's
 * end, and a restored page is cold) */
__attribute__((target("avx512f"))) static void lines_page512(uint64_t *d, const uint64_t *a, const unsigned char *nd,
                                                              const unsigned char *na)
{
    if (nd != NULL)
        for (size_t i = 0; i < PAGE; i += 64)
        {
            _mm_prefetch((const char *)nd + i, _MM_HINT_T0);
            _mm_prefetch((const char *)na + i, _MM_HINT_T0);
        }
    for (size_t l = 0; l < PAGE / 8; l += 8)
    {
        const __m512i x = _mm512_load_si512((const void *)(a + l));
        if (_mm512_cmpneq_epi64_mask(x, _mm512_load_si512((const void *)(d + l)))) _mm512_store_si512((void *)(d + l), x);
    }
}
__attribute__((target("avx512f"))) static void zero_page512(uint64_t *d, const unsigned char *nd)
{
    if (nd != NULL)
        for (size_t i = 0; i < PAGE; i += 64) _mm_prefetch((const char *)nd + i, _MM_HINT_T0);
    for (size_t l = 0; l < PAGE / 8; l += 8)
    {
        const __m512i x = _mm512_load_si512((const void *)(d + l));
        if (_mm512_test_epi64_mask(x, x)) _mm512_store_si512((void *)(d + l), _mm512_setzero_si512());
    }
}
#endif
/* 1 where the lines' AVX-512 pages run (decided once) */
static int lines512(void)
{
#if defined(__x86_64__)
    static int v = -1;
    int k = __atomic_load_n(&v, __ATOMIC_RELAXED);
    if (k < 0)
    {
        k = __builtin_cpu_supports("avx512f") != 0;
        __atomic_store_n(&v, k, __ATOMIC_RELAXED);
    }
    return k;
#else
    return 0;
#endif
}

static void restore_lines(unsigned char *eb, const unsigned char *tb, int64_t delta, const struct pool_start *s, size_t lo,
                          size_t hi)
{
    uint64_t want[PAGE / 8] __attribute__((aligned(64)));
    const int wide = lines512();
    size_t x = 0, y = s->nptrs;
    while (x < y)
    {
        size_t m = (x + y) / 2;
        if (s->ptrs[m] < lo) x = m + 1;
        else y = m;
    }
    for (size_t pg = lo; pg < hi; pg += PAGE)
    {
        const uint64_t *a = (const uint64_t *)(tb + pg);
        if (x < s->nptrs && s->ptrs[x] < pg + PAGE)
        {
            memcpy(want, a, PAGE);
            for (; x < s->nptrs && s->ptrs[x] < pg + PAGE; ++x) want[(s->ptrs[x] - pg) / 8] += (uint64_t)delta;
            a = want;
        }
        uint64_t *d = (uint64_t *)(eb + pg);
#if defined(__x86_64__)
        if (wide)
        {
            lines_page512(d, a, pg + PAGE < hi ? eb + pg + PAGE : NULL, tb + pg + PAGE);
            continue;
        }
#else
        (void)wide;
#endif
        for (size_t l = 0; l < PAGE / 8; l += 8)
        {
            uint64_t v = 0;
            for (int w = 0; w < 8; ++w) v |= a[l + w] ^ d[l + w];
            if (v) memcpy(d + l, a + l, 64);
        }
    }
}

/* zeros over E's [lo, hi) (whole pages), by the 64-byte lines that are not */
static void zero_lines(unsigned char *eb, size_t lo, size_t hi)
{
    const int wide = lines512();
    for (size_t pg = lo; pg < hi; pg += PAGE)
    {
        uint64_t *d = (uint64_t *)(eb + pg);
#if defined(__x86_64__)
        if (wide)
        {
            zero_page512(d, pg + PAGE < hi ? eb + pg + PAGE : NULL);
            continue;
        }
#else
        (void)wide;
#endif
        for (size_t l = 0; l < PAGE / 8; l += 8)
        {
            uint64_t v = 0;
            for (int w = 0; w < 8; ++w) v |= d[l + w];
            if (v) memset(d + l, 0, 64);
        }
    }
}

/* T's bytes over E's [lo, hi) where s's runs hold them, with their
 * pointers rebased; with zero, zeros over the rest (pages E wrote outside
 * the start) */
static void restore_range(unsigned char *eb, const unsigned char *tb, int64_t delta, const struct pool_start *s,
                          size_t lo, size_t hi, int zero)
{
    /* the first run that ends past lo */
    size_t a = 0, b = s->nruns;
    while (a < b)
    {
        size_t m = (a + b) / 2;
        if (s->runs[m].off + s->runs[m].len <= lo) a = m + 1;
        else b = m;
    }
    for (size_t i = a, at = lo; at < hi; ++i)
    {
        size_t rlo = i < s->nruns && s->runs[i].off < hi ? s->runs[i].off : hi;
        if (rlo > at)
        {
#if POOL_RESET_COUNT >= 2
            if (zero) rc_waszero(eb, tb, at, rlo);
#endif
#ifdef POOL_RESET_COUNT
            if (zero) rc_tp[RC_PZERO] += (rlo - at) / PAGE;
#endif
            if (zero) RC_TIME(RC_ZERO, zero_lines(eb, at, rlo));
#ifdef POOL_RESET_VERIFY
            if (zero) RV_ADD(rv_zeroed, (rlo - at) / PAGE);
            if (zero) rv_count(tb, at, rlo, 1);
#endif
            at = rlo;
        }
        if (at >= hi) break;
        size_t c = s->runs[i].off + s->runs[i].len < hi ? s->runs[i].off + s->runs[i].len : hi;
#if POOL_RESET_COUNT >= 2
        rc_same(eb, tb, delta, s, at, c);
#endif
#ifdef POOL_RESET_COUNT
        rc_tp[RC_PCOPY] += (c - at) / PAGE;
#endif
        RC_TIME(RC_COPY, restore_lines(eb, tb, delta, s, at, c));
#ifdef POOL_RESET_VERIFY
        RV_ADD(rv_copied, (c - at) / PAGE);
        rv_count(tb, at, c, 0);
#endif
        at = c;
    }
}

/* E back to s's image by the pages it wrote since it was last in step with
 * s: those the tracker's scan finds, and those its frees handed back (the
 * zap log, read before the restore puts back s's struct env); the scan's
 * runs become pe's hot runs. 0, or -1 when the scan fails (the caller then
 * copies the whole start over what this restored). */
static int reset_written(struct pool *p, struct pool_env *pe, const struct pool_start *s, size_t end)
{
    /* runs closer than this merge into one hot run: the pages between are
     * protected already (unwritten) or holes, and the protect leaves them so */
    enum { HOT_GAP = 64 * PAGE };
    struct env *E = pe->e;
    struct img_wrun w[256];
    unsigned char *eb = (unsigned char *)E;
    const unsigned char *tb = (const unsigned char *)s->t;
    const int64_t delta = (int64_t)((uintptr_t)eb - (uintptr_t)tb);
    struct { uint64_t off, len; } zap[IMG_ZAP_MAX];
    const uint32_t nzap = E->img.nzap < IMG_ZAP_MAX ? E->img.nzap : IMG_ZAP_MAX;
    memcpy(zap, E->img.zap, nzap * sizeof *zap);
    pe->nprot = 0;
    for (size_t at = 0; at < end;)
    {
        long n;
        RC_TIME(RC_SCAN, n = image_wtrack_scan(p->wt, E, &at, end, w, sizeof w / sizeof *w));
        if (n < 0) return -1;
        for (long i = 0; i < n; ++i)
        {
            restore_range(eb, tb, delta, s, w[i].lo, w[i].hi, 1);
            if (pe->nhot > 0 && w[i].lo - pe->hot[pe->nhot - 1].hi <= HOT_GAP) pe->hot[pe->nhot - 1].hi = w[i].hi;
            else if (pe->nhot < POOL_HOT) pe->hot[pe->nhot++] = w[i];
            else if (pe->nprot >= 0 && pe->nprot < POOL_HOT) pe->prot[pe->nprot++] = w[i];
            else pe->nprot = -1;
        }
    }
    /* a page handed back reads zero: only the start's pages need copying */
    for (uint32_t k = 0; k < nzap; ++k)
        if (zap[k].off < end)
        {
            const uint64_t hi = zap[k].off + zap[k].len < end ? zap[k].off + zap[k].len : end;
            restore_range(eb, tb, delta, s, zap[k].off, hi, 0);
            if (pe->nprot >= 0) pe->prot[pe->nprot++] = (struct img_wrun){zap[k].off, hi, 0};
        }
    return 0;
}

static void reset_job(struct pool *p, struct pool_env *pe, struct pool_start *s)
{
    struct env *E = pe->e, *T = s->t;
#ifdef POOL_RESET_COUNT
    const uint64_t rc_t0 = now_ns(), rc_f0 = rc_minflt();
    rc_kind = pe->start == NULL ? 0 : pe->start == s ? 1 : 2;
    rc_tp[RC_ZAPS] = E->img.nzap;
#endif
    /* the cache lives in the image the copy replaces */
    if (p->cfg.gen != NULL) gen_reset(p, pe);
    /* so does the region stores' spill file: E's closed, and after the copy
     * the start's records go to a file of E's own */
    rspill_close(&E->rspill);
#ifdef POOL_RESET_VERIFY
    RV_ADD(rv_zap, E->img.nzap);
    if (E->img.nzap >= IMG_ZAP_MAX) RV_ADD(rv_zapfull, 1);
#endif
    unsigned char *eb = (unsigned char *)E;
    const unsigned char *tb = (const unsigned char *)T;
    size_t used = (size_t)E->img.heap_off + E->img.heap_used;
    used = (used + PAGE - 1) & ~(size_t)(PAGE - 1);
    const size_t end = used > s->used ? used : s->used;
    const int64_t delta = (int64_t)((uintptr_t)eb - (uintptr_t)tb);
    /* an env in step with s since its last reset (the tracker has watched
     * every page it wrote) gets back only those; any other is copied whole */
    const int tracked = p->wt != NULL && !pe->replay && pe->wt_on;
    pe->nhot = 0;
    int whole = 0;
#ifdef POOL_CROSS_COUNT
    const int ckind = pe->start == NULL ? 0 : pe->start == s ? 1 : 2;
    const int csampled = ckind == 2 && CC_ADD(cc_k, 1) % 8 == 0;
    if (csampled) cc_share(pe, s, used, delta);
    uint64_t ctm = 0, ctc = 0, ctr = 0;
    const uint64_t ct0 = now_ns();
#endif
    if (!tracked || pe->wt_serial != s->serial || reset_written(p, pe, s, end) != 0)
    {
        whole = 1;
        pe->nhot = 0;
        /* a protected page would take a fault at its copy */
        if (tracked && pe->wt_prot) RC_TIME(RC_UNPROT, (void)image_wtrack_unprotect(p->wt, E, end));
        pe->wt_prot = 0;
#ifdef POOL_RESET_COUNT
        /* what a delta path that failed restored is not counted twice */
        memset(rc_tt, 0, sizeof rc_tt);
        memset(rc_tp, 0, sizeof rc_tp);
        rc_tp[RC_ZAPS] = E->img.nzap;
#endif
        /* E as a fresh mapping holding T's pages: every page E wrote that
         * T's runs do not cover goes back (reads zeros), the rest is
         * overwritten in place (a reused env's pages stay resident: no fault
         * per page) */
        size_t at = 0;
#ifdef POOL_CROSS_COUNT
        uint64_t q0, q1;
#define CC_T(acc, stmt) (q0 = now_ns(), (stmt), q1 = now_ns(), acc += q1 - q0)
#else
#define CC_T(acc, stmt) (stmt)
#endif
        for (size_t i = 0; i < s->nruns; ++i)
        {
            size_t lo = s->runs[i].off, hi = lo + s->runs[i].len;
#if POOL_RESET_COUNT >= 2
            if (lo > at && at < used) rc_resident(eb, tb, at, lo < used ? lo : used, 1, RG_GAVE);
            rc_resident(eb, tb, lo, hi, 0, RG_FAULT);
            rc_same(eb, tb, delta, s, lo, hi);
#endif
#ifdef POOL_RESET_COUNT
            if (lo > at && at < used) rc_tp[RC_PMADV] += ((lo < used ? lo : used) - at) / PAGE;
            rc_tp[RC_PCOPY] += (hi - lo) / PAGE;
#endif
            if (lo > at && at < used) RC_TIME(RC_MADV, CC_T(ctm, madvise(eb + at, (lo < used ? lo : used) - at, MADV_DONTNEED)));
            RC_TIME(RC_COPY, CC_T(ctc, memcpy(eb + lo, tb + lo, hi - lo)));
            at = hi;
        }
#if POOL_RESET_COUNT >= 2
        if (at < used) rc_resident(eb, tb, at, used, 1, RG_GAVE);
#endif
#ifdef POOL_RESET_COUNT
        if (at < used) rc_tp[RC_PMADV] += (used - at) / PAGE;
#endif
        if (at < used) RC_TIME(RC_MADV, CC_T(ctm, madvise(eb + at, used - at, MADV_DONTNEED)));
#ifdef POOL_CROSS_COUNT
        q0 = now_ns();
#endif
        RC_TIME(RC_REBASE, for (size_t i = 0; i < s->nptrs; ++i) *(int64_t *)(eb + s->ptrs[i]) += delta);
#ifdef POOL_RESET_COUNT
        rc_tp[RC_PTRS] += s->nptrs;
#endif
#ifdef POOL_CROSS_COUNT
        ctr = now_ns() - q0;
#endif
#undef CC_T
    }
#ifdef POOL_CROSS_COUNT
    cc_end(ckind, whole, now_ns() - ct0, ctm, ctc, ctr, csampled);
#endif
#ifdef POOL_RESET_VERIFY
    reset_verify_end(pe, s, end, tracked && pe->wt_serial == s->serial);
#endif
    /* from here the tracker watches E's writes: what it protects now is in
     * step with s */
    if (p->wt != NULL && !pe->replay)
    {
        if (!pe->wt_on) pe->wt_on = image_wtrack_add(p->wt, E) == 0;
        int ok;
        if (whole)
        {
            /* the episode after a whole copy runs unprotected (an env whose
             * next start is another one, or its first, takes no fault), so
             * the next reset copies back every page present; the one after
             * it learns the hot runs */
            const struct img_wrun all = {0, end, 0};
            RC_TIME(RC_PROT, ok = pe->wt_on && image_wtrack_protect(p->wt, E, end, &all, 1) == 0);
            pe->hot_resets = 0;
        }
        else
        {
            /* every page written since the last protect is in the hot runs
             * or pe->prot (lane/resetcopy): those are walked, not the image */
            if (++pe->hot_resets % POOL_HOT_EVERY == 1)
            {
                if (pe->nprot >= 0)
                    for (int i = 0; i < pe->nhot; ++i) pe->prot[pe->nprot++] = pe->hot[i];
                pe->nhot = 0;
            }
            if (pe->nprot >= 0) RC_TIME(RC_PROT, ok = image_wtrack_protect_runs(p->wt, E, pe->prot, (size_t)pe->nprot) == 0);
            else RC_TIME(RC_PROT, ok = image_wtrack_protect(p->wt, E, end, pe->hot, (size_t)pe->nhot) == 0);
            pe->wt_prot = 1;
        }
        pe->wt_serial = ok ? s->serial : 0;
    }
#ifdef POOL_RESET_COUNT
    for (int i = 0; i < pe->nhot; ++i) rc_tp[RC_HOT] += (pe->hot[i].hi - pe->hot[i].lo) / PAGE;
#endif
    /* the image points to s's frozen sets and packed chunks now, whichever way it was restored */
    if (!pe->replay) tbase_take(pe, s);   /* lane/resettick: a save's replays are its start's to hold */
    pe->ss = (struct session *)(eb + E->img.dir.session);
    if (p->cfg.save_slots > 0 && !pe->replay)
    {
        /* the env holds its start (the reference pool_reset took for the
         * job) and lets go of its last one; its act log starts over */
        struct pool_start *old = pe->start;
        pe->start = s;
        if (old != NULL) start_unref(old);
        log_clear(&pe->log);
    }
    else
        pe->start = s;
    pe->start_tick = s->tick;
    pe->cw_from = s->cw_from;
    pe->cw_seed = s->cw_seed;
    pe->cw_lcg = s->cw_lcg;
    pe->t = s->tick;
    pe->stats_ok = 0;
    pe->profiling = 0;   /* the copy's profiler is the start's: off */
    ++pe->epoch;
    pe->ss->hooks.ctx = pe;
    struct env *was = neth_env;
    neth_env = E;
    /* the rung lands after the copy, so one start image serves every rung
     * (SPEC.md "The fidelity ladder": an env changes rung only at a reset).
     * The image carries the pool's fidelity (start_image); stripped is the
     * four fidelity.* switches on top of it, applied here per env. */
    {
        /* a save's replay (id n + 2 slot + which, past p->rung's n) runs at the saved env's rung: reading
         * p->rung[pe->id] there read past the array (lane/replayrung: the seeds run's replays crashed in the
         * item entities' walk) */
        const int rung = p->rung == NULL ? NETH_RUNG_EXACT : pe->replay ? pe->replay_rung : p->rung[pe->id];
        env_rung_set(rung);
        if (rung == NETH_RUNG_STRIPPED && pe->ss->sr != NULL)
        {
            struct fidelity f = p->cfg.fid;
            f.no_spawning = 1;      /* fidelity.spawning: off */
            f.keep_on = 1;          /* fidelity.mobs: none */
            f.keep = 0;
            f.bits |= FID_NO_RANDOM_TICKS | FID_NO_WEATHER;
            /* lane/ladder: the env's own stripped fidelity (a ladder's rung, pool_rung_fid); a save's replay keeps
             * stripped's own */
            if (!pe->replay && p->rfid_on != NULL && p->rfid_on[pe->id]) f = p->rfid[pe->id];
            fidelity_apply(&f, pe->ss->sr);
        }
    }
    int heap = image_heap_set(E, 1);
    rspill_adopt();
    image_heap_set(E, heap);
    neth_env = was;
#ifdef POOL_RESET_COUNT
    rc_end(whole, rc_t0, rc_f0);
#endif
    (void)p;
}

/* ------------------------------------------------------------ the step */

static void hud_stack(struct pool_stack *o, int item, int count, int damage)
{
    if (count <= 0 && item <= 0) { o->item = -1; o->count = 0; o->damage = 0; return; }
    o->item = (int16_t)item;
    o->count = (int16_t)count;
    o->damage = (int16_t)damage;
}

static int screen_of(struct client_player *cp)
{
    if (cp->screen_gameover) return PS_GAMEOVER;
    if (cp->screen_sleep) return PS_SLEEP;
    if (cp->screen_chat) return PS_CHAT;
    if (cp->screen_credits) return PS_CREDITS;
    if (!cp->screen_inventory) return PS_NONE;
    const struct container *c = cp_gui_container(cp);
    if (c == NULL) return PS_INVENTORY;
    switch (c->kind)
    {
    case CONTAINER_WORKBENCH: return PS_CRAFTING;
    case CONTAINER_CHEST: return PS_CHEST;
    case CONTAINER_FURNACE: return PS_FURNACE;
    case CONTAINER_MERCHANT: return PS_MERCHANT;
    case CONTAINER_DISPENSER: return PS_DISPENSER;
    case CONTAINER_HOPPER: return PS_HOPPER;
    default: return PS_INVENTORY;
    }
}

static void fill_hud(struct pool_hud *h, struct session *ss)
{
    struct client_player *cp = &ss->cp;
    memset(h, 0, sizeof *h);
    h->health = cp->sv.health;
    h->food = cp->sv.food.level;
    h->armour = surv_client_armor_value(cp);
    h->air = ss->sp.sv.air;
    h->xp_level = cp->sv.xp_level;
    h->xp_bar = cp->sv.xp_progress;
    h->selected = cp->hotbar;
    for (int i = 0; i < 9; ++i) hud_stack(&h->hotbar[i], cp->sv.inv[i].item, cp->sv.inv[i].count, cp->sv.inv[i].damage);
    h->screen = screen_of(cp);
    struct container *c = cp_gui_container(cp);
    if (c != NULL && cp->screen_inventory)
    {
        h->nslots = c->nslots < 90 ? c->nslots : 90;
        for (int i = 0; i < h->nslots; ++i)
        {
            /* the player's half is the client's own stacks (the container's
             * copy follows them only at a click: survival.c client_click_on;
             * in Java the container's inventory is InventoryPlayer itself) */
            if (c->slots[i].inv == &c->player && c->slots[i].index >= 0 && c->slots[i].index < 40)
            {
                const struct surv_stack *st = &cp->sv.inv[c->slots[i].index];
                if (st->count > 0) hud_stack(&h->slots[i], st->item, st->count, st->damage);
                else hud_stack(&h->slots[i], -1, 0, 0);
                continue;
            }
            const struct craft_stack *s = container_slot(c, i);
            if (s == NULL) hud_stack(&h->slots[i], -1, 0, 0);
            else hud_stack(&h->slots[i], s->item, s->count, s->damage);
        }
        if (cp->sv.cursor.count > 0) hud_stack(&h->cursor, cp->sv.cursor.item, cp->sv.cursor.count, cp->sv.cursor.damage);
        else hud_stack(&h->cursor, -1, 0, 0);
    }
    else hud_stack(&h->cursor, -1, 0, 0);
}

static void fill_priv(struct pool_priv *pr, struct session *ss)
{
    const struct server_player *sp = &ss->sp;
    memset(pr, 0, sizeof *pr);
    pr->x = sp->e.pos_x;
    pr->y = sp->e.pos_y;
    pr->z = sp->e.pos_z;
    pr->yaw = sp->rotation_yaw;
    pr->pitch = sp->rotation_pitch;
    pr->dim = sp->dimension;
    pr->health = sp->sv.health;
    pr->saturation = sp->sv.food.saturation;
    pr->exhaustion = sp->sv.food.exhaustion;
    pr->food = sp->sv.food.level;
    pr->xp_total = sp->sv.xp_total;
    pr->air = sp->sv.air;
    pr->fire = sp->sv.fire;
    pr->on_ground = sp->e.on_ground;
    if (ss->sr != NULL)
    {
        pr->world_time = sr_world_time(ss->sr);
        pr->total_time = sr_total_time(ss->sr);
    }
}

/* A save's replay: the log's next tick (pe->rp) as step_act made it. */
static int replay_act(struct pool *p, struct pool_env *pe, int rd, struct act *a, struct agent_state *ag)
{
    const struct log_ent *e = (const struct log_ent *)pe->rp;
    if (pe->rp + sizeof *e > pe->rend)
    {
        snprintf(pe->res.err, sizeof pe->res.err, "the act log ends inside a step");
        return 0;
    }
    pe->rp += (sizeof *e + e->len + 7) & ~(size_t)7;
    const void *body = e + 1;
    switch (e->kind)
    {
    case PA_TAPE:
        memcpy(a, body, sizeof *a);
        return 1;
    case PA_ROW:
    {
        /* the row's text where step_act had it: strdup's block of the heap */
        char *nul = proc_malloc(e->len + 1u);
        memcpy(nul, body, e->len);
        nul[e->len] = 0;
        char *text = strdup(nul);
        proc_free(nul);
        struct jval *row = text ? json_parse(text) : NULL;
        memset(a, 0, sizeof *a);
        if (row == NULL) return 0;
        char why[160];
        if (session_parse_act(row, a, why, sizeof why))
            snprintf(pe->res.err, sizeof pe->res.err, "row %lld: %.200s", (long long)pe->t, why);
        json_free(row);
        return 1;
    }
    default:
    {
        struct pool_act c;
        memset(&c, 0, sizeof c);
        memcpy(&c, body, e->len < sizeof c ? e->len : sizeof c);
        return agent_act(ag, pe->ss, &c, e->hold_only, rd, a, pe->res.err, sizeof pe->res.err);
    }
    }
}

/* an agent-form act into the log: its fields up to the ops it has */
static void log_agent(struct pool_env *pe, const struct pool_act *c, int hold_only)
{
    struct pool_act x = *c;
    x.tape = NULL;
    x.row = NULL;
    int nops = x.nops < 0 ? 0 : x.nops > POOL_GUI_OPS ? POOL_GUI_OPS : x.nops;
    log_tick(&pe->log, PA_AGENT, hold_only, &x, offsetof(struct pool_act, ops) + (size_t)nops * sizeof x.ops[0]);
}

/* The act of the step's k-th tick; 0 when the step is refused whole. With
 * cfg.save_slots each act a tick runs goes in the env's log (a replay reads
 * them back). */
static int step_act(struct pool *p, struct pool_env *pe, int k, int rd, struct act *a, struct agent_state *ag)
{
    if (pe->replay) return replay_act(p, pe, rd, a, ag);
    const int logging = p->cfg.save_slots > 0;
    const struct pool_act *src = pe->per_tick ? &pe->acts[k] : &pe->acts[0];
    switch (src->kind)
    {
    case PA_TAPE:
        *a = *src->tape;
        if (logging) log_tick(&pe->log, PA_TAPE, 0, a, sizeof *a);
        return 1;
    case PA_ROW:
    {
        char *text = strdup(src->row);
        struct jval *row = text ? json_parse(text) : NULL;
        memset(a, 0, sizeof *a);
        if (row == NULL)
        {
            snprintf(pe->res.err, sizeof pe->res.err, "row %lld does not parse", (long long)pe->t);
            return 0;
        }
        char why[160];
        /* a malformed part is test_snapshots' bad row: the act holds what
         * did parse, and the tick runs */
        if (session_parse_act(row, a, why, sizeof why))
            snprintf(pe->res.err, sizeof pe->res.err, "row %lld: %.200s", (long long)pe->t, why);
        json_free(row);
        if (logging) log_tick(&pe->log, PA_ROW, 0, src->row, strlen(src->row));
        return 1;
    }
    default:
        if (p->cfg.act_hook)
        {
            struct pool_act c;
            if (p->cfg.act_hook(p->cfg.act_hook_ctx, pe->id, pe->ss, k, src, !pe->per_tick && k > 0, &c))
            {
                int ok = agent_act(ag, pe->ss, &c, 0, rd, a, pe->res.err, sizeof pe->res.err);
                /* a raw screen act goes in the log as the tick resolves it
                 * (step_job: its clicks are made inside the tick) */
                if (ok && logging && c.gui_in == NULL) log_agent(pe, &c, 0);
                return ok;
            }
        }
        int hold_only = !pe->per_tick && k > 0;
        int ok = agent_act(ag, pe->ss, src, hold_only, rd, a, pe->res.err, sizeof pe->res.err);
        if (ok && logging) log_agent(pe, src, hold_only);
        return ok;
    }
}

static void step_job(struct pool *p, struct pool_env *pe)
{
    struct pool_result *r = &pe->res;
    /* the phase profiler (phase.h) counts the thread it starts on: a pinned
     * env's worker */
    if (p->cfg.profile && p->cfg.pin == 1 && !pe->profiling && !pe->replay)
    {
        phase_profile_start();
        pe->profiling = 1;
    }
    /* cfg.save_slots: the step in the env's act log (its ticks' acts go in
     * from step_act) */
    const int logging = p->cfg.save_slots > 0 && !pe->replay;
    if (pe->view_bad)
    {
        snprintf(r->err, sizeof r->err, "the view: %.600s", pe->view_err);
        r->flags |= PF_ERROR;
        r->obs = NULL;
        if (logging) pe->log.bad = 1;
        return;
    }
    if (logging) log_step_begin(&pe->log);
    struct session *ss = pe->ss;
    struct client_player *cp = &ss->cp;
    const struct surv_stats *st = &ss->sp.sv.stats;
    if (!pe->stats_v)
    {
        pe->stats_v = proc_malloc(sizeof st->client_v);
        pe->stats_p = proc_malloc(sizeof st->client_present);
        if (!pe->stats_v || !pe->stats_p) abort();
    }
    /* the mirror before the step: the kept copy while its count stands */
    if (!pe->stats_ok || pe->stats_writes != st->client_writes)
    {
        memcpy(pe->stats_v, st->client_v, sizeof st->client_v);
        memcpy(pe->stats_p, st->client_present, sizeof st->client_present);
        pe->stats_writes = st->client_writes;
        pe->stats_ok = 1;
    }
    const int32_t *before_v = pe->stats_v;
    const uint8_t *before_p = pe->stats_p;
    int dim0 = cp->dimension, dead0 = cp->screen_gameover;
    struct agent_state ag;
    agent_begin(&ag);
    /* (a save's replay generates in its ticks: its two runs make the same
     * heap only if neither takes a chunk from the shared cache) */
    if (p->gc && ss->sr && !pe->replay) gencache_attach(p->gc, ss->sr);

    for (int k = 0; k < pe->n; ++k)
    {
        /* the worker's own stack (not a tick frame) */
        struct act act_, *a = &act_;
        int ok = step_act(p, pe, k, p->cfg.obs.rd, a, &ag);
        if (!ok)
        {
            /* a refused act ends the step there (the oracle runs no tick
             * for a refused step) */
            r->flags |= ag.refused_step ? PF_REFUSED : PF_ERROR;
            break;
        }
        if (p->cfg.view_tick) p->cfg.view_tick(p->cfg.view_ctx, pe->id, pe->t, a, 0);
        if (!session_tick(ss, pe->t, a))
        {
            snprintf(r->err, sizeof r->err, "%.600s", ss->err);
            r->flags |= PF_ERROR;
            break;
        }
        agent_end_tick(&ag);
        if (a->gui_in != NULL && !pe->replay)
        {
            /* a raw screen act: the tick's resolved act (the clicks and
             * closes its events made) is the log's, in tape form */
            if (logging)
            {
                struct act t = *a;
                t.gui_in = NULL;
                t.agent = NULL;
                log_tick(&pe->log, PA_TAPE, 0, &t, sizeof t);
            }
        }
        if (p->cfg.act_done && !pe->replay) p->cfg.act_done(p->cfg.act_hook_ctx, pe->id, pe->t, a);
        if (pe->cw_from == pe->t + 1 && pe->t == pe->start_tick)
            client_player_set_world_rand(cp, pe->cw_seed, pe->cw_lcg);
        if (p->cfg.view_tick) p->cfg.view_tick(p->cfg.view_ctx, pe->id, pe->t, a, 1);
        if (p->cfg.tick_hook && !pe->replay)
            p->cfg.tick_hook(p->cfg.tick_hook_ctx, pe->id, ss, pe->t, k == pe->n - 1);
        if (pe->feed) feed_tick(pe->feed, ss);
        ++pe->t;
        ++r->ticks;
        if (!dead0 && cp->screen_gameover) r->flags |= PF_DIED;
        dead0 = cp->screen_gameover;
    }
    if (logging) log_step_end(&pe->log, r->ticks, r->flags & PF_ERROR);
    r->ops_refused = ag.ops_refused;
    if (ag.ops_refused) r->flags |= PF_OPS_REFUSED;
    if (cp->screen_gameover || cp->sv.health <= 0.0F) r->flags |= PF_DEAD;
    if (cp->dimension != dim0) r->flags |= PF_DIM;
    r->t = pe->t;
    fill_hud(&r->hud, ss);
    r->nevents = 0;
    /* the stats in blocks of 64: a block whose values and present bits are
     * all unchanged holds no event (almost every block, almost every step);
     * none when nothing wrote the mirror (the kept copy stays its state) */
    for (int s0 = 0; st->client_writes != pe->stats_writes && s0 < STAT_COUNT && r->nevents < POOL_EVENTS; s0 += 64)
    {
        int s1 = s0 + 64 < STAT_COUNT ? s0 + 64 : STAT_COUNT;
        if (!memcmp(&before_v[s0], &st->client_v[s0], (size_t)(s1 - s0) * sizeof before_v[0]) &&
            !memcmp(&before_p[s0 >> 3], &st->client_present[s0 >> 3], (size_t)((s1 - s0 + 7) >> 3)))
            continue;
        for (int s = s0; s < s1 && r->nevents < POOL_EVENTS; ++s)
        {
            int was = STAT_BIT(before_p, s) ? before_v[s] : 0, now = STAT_BIT(st->client_present, s) ? st->client_v[s] : 0;
            if (was == now && STAT_BIT(before_p, s) == STAT_BIT(st->client_present, s)) continue;
            r->events[r->nevents++] = (struct pool_event){s, was, now};
        }
    }
    if (p->cfg.privileged) fill_priv(&r->priv, ss);
    if (pe->feed)
    {
        r->feed = feed_finish(pe->feed, ss, &r->feed_bytes);
    }
    else
    {
        r->feed = NULL;
        r->feed_bytes = 0;
    }
    r->obs = NULL;
    r->obs_rc = 0;
    if (p->cfg.view_step && !(r->flags & PF_ERROR))
    {
        if (p->cfg.range) p->cfg.range("frame record", 1);
        p->cfg.view_step(p->cfg.view_ctx, pe->id, r);
        if (p->cfg.range) p->cfg.range(NULL, 0);
    }
    if (p->cfg.step_hook && !pe->replay && !(r->flags & PF_ERROR)) p->cfg.step_hook(p->cfg.step_hook_ctx, pe->id, ss, r);
    /* the image keeps no pointer to the process's cache */
    if (p->gc && ss->sr) gencache_attach(NULL, ss->sr);
    if (p->cfg.gen && ss->sr) genahead_detach(ss->sr);
}

/* ------------------------------------------------------ device generation
 *
 * cfg.gen: at the end of each step (and at the first step after a reset)
 * the env predicts its next step's generations (gen_predict); every env
 * waiting in p->gq goes to the generation thread (gen_main) together, which
 * generates their unique requests in one round (gen_round) while the envs'
 * results go back to the trainer. At its next step the env hands the
 * results to its cache (gen_fill), and its ticks construct the chunks they
 * take; a step whose round is still running waits for it (the round queues
 * it, J_GENSTEP, ahead of other jobs). */

static struct jobq *env_queue(struct pool *p, int id);

/* requests a round takes at most (at least one env's) */
#define GEN_ROUND 2048

static void gen_stats_add(struct pool_gen_stats *s, const struct ga_stats *g)
{
    for (int d = 0; d < 3; ++d)
    {
        s->hits += g->hits[d];
        s->misses += g->misses[d];
        s->generated += g->generated[d];
        s->wasted += g->wasted[d];
        for (int b = 0; b < 8; ++b) s->miss_ring[d][b] += g->miss_ring[d][b];
    }
    s->miss_other_dim += g->miss_other_dim;
    s->owed_hits += g->owed_hits;
    s->owed_misses += g->owed_misses;
    s->predict_ins += g->ins_predict;
    s->construct_ins += g->ins_construct;
    s->construct_ns += g->ns_construct;
}

/* the free lists keep this many of each kind */
#define GEN_FREE 256

static size_t gen_res_size(int cls)
{
    return sizeof(struct gen_res) +
           (cls == GR_RAW ? GA_CELLS : cls == GR_RAW_METAS ? 2 * (size_t)GA_CELLS : (size_t)(cls - GR_BUILT) * GA_BAND_BYTES);
}

static struct gen_res *gen_res_new(struct pool *p, int cls)
{
    size_t bytes = gen_res_size(cls);
    pthread_mutex_lock(&p->gfree_mu);
    struct gen_res *r = p->gfree[cls];
    if (r != NULL)
    {
        memcpy(&p->gfree[cls], r->biome, sizeof r);
        --p->ngfree[cls];
        p->free_bytes -= bytes;
    }
    p->res_bytes += bytes;
    pthread_mutex_unlock(&p->gfree_mu);
    if (r == NULL) r = proc_malloc(bytes);
    r->pool = p;
    r->cls = cls;
    return r;
}

static void gen_res_put(struct gen_res *r)
{
    struct pool *p = r->pool;
    int k = r->cls;
    size_t bytes = gen_res_size(k);
    pthread_mutex_lock(&p->gfree_mu);
    p->res_bytes -= bytes;
    if (p->ngfree[k] >= GEN_FREE)
    {
        pthread_mutex_unlock(&p->gfree_mu);
        proc_free(r);
        return;
    }
    memcpy(r->biome, &p->gfree[k], sizeof r);
    p->gfree[k] = r;
    ++p->ngfree[k];
    p->free_bytes += bytes;
    pthread_mutex_unlock(&p->gfree_mu);
}

/* a request's reference to its result: the last one lets it go, from
 * whichever thread (a tick that took its chunk, a sweep, a reset) */
static void gen_res_release(void *owner)
{
    struct gen_res *r = owner;
    if (__atomic_sub_fetch(&r->refs, 1, __ATOMIC_ACQ_REL) == 0) gen_res_put(r);
}

/* the env's requests' references, unused (a reset) */
static void gen_drop(struct pool_env *pe)
{
    for (int i = 0; i < pe->ngreq; ++i)
        if (pe->gres[i] != NULL) gen_res_release(pe->gres[i]);
    pe->ngreq = 0;
}

/* the reset's part (the env's image is about to be copied over): the cache
 * goes with it, a round in flight drops its results when it ends */
static void gen_reset(struct pool *p, struct pool_env *pe)
{
    pthread_mutex_lock(&p->mu);
    if (pe->ga != NULL) gen_stats_add(&p->gst, genahead_stats(pe->ga));
    if (pe->gstate == 1) pe->gdrop = 1;
    else
    {
        if (pe->gstate == 2) gen_drop(pe);
        pe->gstate = 0;
    }
    pthread_mutex_unlock(&p->mu);
    if (pe->ga != NULL) genahead_release_raw(pe->ga);
    pe->ga = NULL;
    pe->predicted = 0;
    pe->tps_ok = 0;
}

/* the generations the steps from tick t (n ticks) may ask for (the env
 * current, its heap on) into pe->greq. A dev tape's tp entry due in them
 * moves the player where the world cannot see: its target is predicted too,
 * as test_snapshots' gen-ahead does. */
static void gen_predict(struct pool *p, struct pool_env *pe, int64_t t, int n)
{
    struct session *ss = pe->ss;
    pe->ngreq = 0;
    if (ss->sr == NULL) return;
    if (pe->ga == NULL)
    {
        pe->ga = genahead_create();
        genahead_set_ahead(pe->ga, p->cfg.gen_ahead);
    }
    if (!pe->tps_ok)
    {
        pe->ntps = 0;
        for (int k = 0; ss->setups != NULL && k < (int)json_len(ss->setups); ++k)
        {
            const struct jval *e = json_at(ss->setups, k), *cmd = json_get(e, "cmd");
            const char *cls = json_str(json_get(e, "class")), *op = json_str(json_get(cmd, "op"));
            int64_t at = -1;
            uint64_t bx, bz;
            if (!json_int(json_get(e, "tick"), &at) || !cls || strcmp(cls, "Dev") || !op || strcmp(op, "tp") ||
                !json_double(json_get(cmd, "x"), &bx) || !json_double(json_get(cmd, "z"), &bz))
                continue;
            if (pe->ntps == pe->captps)
            {
                struct gen_tp *v = proc_malloc(sizeof *v * (size_t)(pe->captps ? 2 * pe->captps : 16));
                if (pe->ntps) memcpy(v, pe->tps, sizeof *v * (size_t)pe->ntps);
                proc_free(pe->tps);
                pe->tps = v;
                pe->captps = pe->captps ? 2 * pe->captps : 16;
            }
            struct gen_tp *tp = &pe->tps[pe->ntps++];
            tp->t = at;
            memcpy(&tp->x, &bx, 8);
            memcpy(&tp->z, &bz, 8);
        }
        pe->tps_ok = 1;
    }
    for (int k = 0; k < pe->ntps; ++k)
        if (pe->tps[k].t >= t && pe->tps[k].t < t + n) genahead_hint(pe->ga, pe->tps[k].x, pe->tps[k].z);
    const struct ga_req *req;
    int m = genahead_predict(pe->ga, ss->sr, &req);
    if (m > pe->capreq)
    {
        proc_free(pe->greq);
        proc_free(pe->gres);
        pe->capreq = m > 2 * pe->capreq ? m : 2 * pe->capreq;
        pe->greq = proc_malloc(sizeof *pe->greq * (size_t)pe->capreq);
        pe->gres = proc_malloc(sizeof *pe->gres * (size_t)pe->capreq);
    }
    if (m > 0) memcpy(pe->greq, req, sizeof *req * (size_t)m);
    pe->ngreq = m;
}

/* the round's results into the env's cache (the env current, its heap on):
 * each kept by reference, its chunk constructed when a tick takes it */
static void gen_fill(struct pool_env *pe)
{
    for (int i = 0; i < pe->ngreq; ++i)
    {
        struct gen_res *r = pe->gres[i];
        if (r != NULL && r->cls >= GR_BUILT)
            genahead_fill_built(pe->ga, &pe->greq[i], &r->built, r->data, r->biome, r->tops, r->rand, r, gen_res_release);
        else if (r != NULL)
            genahead_fill_raw(pe->ga, &pe->greq[i], r->data, r->cls == GR_RAW_METAS ? r->data + GA_CELLS : NULL, r->biome,
                              r->tops, r->rand, r, gen_res_release);
        else genahead_fill(pe->ga, &pe->greq[i], NULL, NULL, NULL, NULL, 0);
    }
    pe->ngreq = 0;
}

/* a step's start (the env current, its heap on): the results of the round
 * its last step asked for go to its cache; with none asked for (the first
 * step after a reset) the step predicts now. GA_WAIT or GA_SUBMIT_WAIT when
 * the step waits for a round. */
static int gen_step_begin(struct pool *p, struct pool_env *pe)
{
    pthread_mutex_lock(&p->mu);
    int st = pe->gstate;
    if (st == 2) pe->gstate = 0;
    pthread_mutex_unlock(&p->mu);
    /* cfg.gen_nowait: the round finishes while the step runs (a reset's
     * dropped round aside: its step starts over when it ends) */
    if (st == 1) return p->cfg.gen_nowait && !pe->gdrop ? GA_NONE : GA_WAIT;
    if (st == 2)
    {
        gen_fill(pe);
        return GA_NONE;
    }
    if (pe->predicted || pe->view_bad) return GA_NONE;
    gen_predict(p, pe, pe->t, pe->n);
    return pe->ngreq > 0 ? GA_SUBMIT_WAIT : GA_NONE;
}

/* a step's end: the next step's generations, asked for now (cfg.gen_nowait:
 * a round still in flight asks for none; one finished during the step goes
 * to the cache first) */
static int gen_step_end(struct pool *p, struct pool_env *pe)
{
    pe->predicted = 0;
    if (pe->res.flags & PF_ERROR || pe->view_bad) return GA_NONE;
    if (p->cfg.gen_nowait)
    {
        pthread_mutex_lock(&p->mu);
        int st = pe->gstate;
        if (st == 2) pe->gstate = 0;
        pthread_mutex_unlock(&p->mu);
        if (st == 1) return GA_NONE;
        if (st == 2) gen_fill(pe);
    }
    gen_predict(p, pe, pe->t, pe->n);
    pe->predicted = 1;
    return pe->ngreq > 0 ? GA_SUBMIT_AHEAD : GA_NONE;
}

/* the job's generation act, under the pool's lock, the env let go */
static void gen_act(struct pool *p, struct pool_env *pe, int act)
{
    switch (act)
    {
    case GA_WAIT:
        /* the round may have ended since the step looked */
        if (pe->gstate == 1) pe->gwait = 1, pe->gwait_start = now_ns();
        else jobq_push_front(env_queue(p, pe->id), (struct job){J_GENSTEP, pe->id, NULL});
        break;
    case GA_SUBMIT_WAIT:
    case GA_SUBMIT_AHEAD:
        pe->gstate = 1;
        pe->gwait = act == GA_SUBMIT_WAIT;
        if (pe->gwait) pe->gwait_start = now_ns();
        p->gq[p->gq_n++] = pe->id;
        pthread_cond_signal(&p->gwork);
        break;
    default:
        break;
    }
    if (act != GA_NONE) pthread_cond_broadcast(&p->work);
}

static uint64_t gen_hash(const struct ga_req *r)
{
    uint64_t h = (uint64_t)r->seed * 0x9E3779B97F4A7C15ull ^ (uint64_t)(uint32_t)r->cx * 0xC2B2AE3D27D4EB4Full ^
                 (uint64_t)(uint32_t)r->cz * 0x165667B19E3779F9ull ^ (uint64_t)(uint32_t)(r->dim + 1) * 0x27D4EB2F165667C5ull;
    return h ^ h >> 29;
}

/* the round's scratch (the generation thread's) */
struct gen_scratch {
    size_t cap, ucap;
    int *tbl, *refs, *miss;
    struct ga_req *uniq, *todo;
    struct gen_res **res;
    size_t bcap;
    uint8_t *ids, *metas, *biome, *tops;
    uint64_t *rand;
    struct ga_built *built;     /* the generator's run_built */
    uint8_t *bands;
    int made;                   /* the last round's generations */
};

struct gen_kept { struct ga_req key; struct gen_res *r; };

/* results kept for later rounds (at most 131 KB each) */
#define GEN_KEEP 2048
#define GEN_KCAP 8192

static int gen_same(const struct ga_req *a, const struct ga_req *b)
{
    return a->seed == b->seed && a->dim == b->dim && a->cx == b->cx && a->cz == b->cz;
}

static size_t gen_kfind(const struct pool *g, const struct ga_req *r)
{
    size_t h = gen_hash(r) & (GEN_KCAP - 1);
    while (g->kt[h].r != NULL && !gen_same(&g->kt[h].key, r)) h = (h + 1) & (GEN_KCAP - 1);
    return h;
}

/* the oldest kept result goes (its reference released); backward shift */
static void gen_kdrop(struct pool *g)
{
    struct ga_req key = g->kring[g->krhead];
    g->krhead = (g->krhead + 1) % GEN_KEEP;
    --g->krn;
    size_t i = gen_kfind(g, &key);
    if (g->kt[i].r == NULL) return;
    gen_res_release(g->kt[i].r);
    g->kt[i].r = NULL;
    for (size_t j = (i + 1) & (GEN_KCAP - 1); g->kt[j].r != NULL; j = (j + 1) & (GEN_KCAP - 1))
    {
        size_t home = gen_hash(&g->kt[j].key) & (GEN_KCAP - 1);
        if ((j > i && (home <= i || home > j)) || (j < i && home <= i && home > j))
        {
            g->kt[i] = g->kt[j];
            g->kt[j].r = NULL;
            i = j;
        }
    }
}

static void gen_keep(struct pool *g, const struct ga_req *key, struct gen_res *r)
{
    if (g->krn == GEN_KEEP) gen_kdrop(g);
    size_t i = gen_kfind(g, key);
    if (g->kt[i].r != NULL) return;
    __atomic_add_fetch(&r->refs, 1, __ATOMIC_RELAXED);
    g->kt[i].key = *key;
    g->kt[i].r = r;
    g->kring[(g->krhead + g->krn) % GEN_KEEP] = *key;
    ++g->krn;
}

/* the round's output buffers: the generator's pinned pages when it has them */
static void *gen_host_alloc(const struct ga_gen *gen, size_t bytes)
{
    void *p = gen->host_alloc ? gen->host_alloc(gen->ctx, bytes) : NULL;
    return p != NULL ? p : proc_malloc(bytes);
}

static void gen_scratch_free(const struct ga_gen *gen, struct gen_scratch *g)
{
    void *v[7] = {g->ids, g->metas, g->biome, g->tops, g->rand, g->built, g->bands};
    for (int i = 0; i < 7; ++i)
        if (v[i] != NULL) (gen->host_free ? gen->host_free(gen->ctx, v[i]) : proc_free(v[i]));
    g->ids = g->metas = g->biome = g->tops = g->bands = NULL;
    g->rand = NULL;
    g->built = NULL;
}

/* one round over envs take[0..m) (total requests): each unique request
 * generated once, every env's request given its result (NULL when the
 * generator dropped the round). Returns the unique count; *ok. */
static int gen_round(struct pool *p, const struct ga_gen *gen, struct gen_scratch *g, const int *take, size_t m,
                     size_t total, int *ok)
{
    size_t cap = 16;
    while (cap < 2 * total) cap *= 2;
    if (cap > g->cap)
    {
        proc_free(g->tbl);
        g->tbl = proc_malloc(cap * sizeof *g->tbl);
        g->cap = cap;
    }
    if (total > g->ucap)
    {
        proc_free(g->uniq);
        proc_free(g->todo);
        proc_free(g->refs);
        proc_free(g->miss);
        proc_free(g->res);
        g->ucap = total;
        g->uniq = proc_malloc(total * sizeof *g->uniq);
        g->todo = proc_malloc(total * sizeof *g->todo);
        g->refs = proc_malloc(total * sizeof *g->refs);
        g->miss = proc_malloc(total * sizeof *g->miss);
        g->res = proc_malloc(total * sizeof *g->res);
    }

    memset(g->tbl, 0xff, cap * sizeof *g->tbl);
    int nu = 0;
    for (size_t e = 0; e < m; ++e)
    {
        struct pool_env *pe = &p->envs[take[e]];
        for (int i = 0; i < pe->ngreq; ++i)
        {
            const struct ga_req *r = &pe->greq[i];
            size_t h = gen_hash(r) & (cap - 1);
            for (;; h = (h + 1) & (cap - 1))
            {
                if (g->tbl[h] < 0)
                {
                    g->tbl[h] = nu;
                    g->refs[nu] = 0;
                    g->uniq[nu++] = *r;
                    break;
                }
                const struct ga_req *u = &g->uniq[g->tbl[h]];
                if (u->seed == r->seed && u->dim == r->dim && u->cx == r->cx && u->cz == r->cz) break;
            }
            ++g->refs[g->tbl[h]];
            /* the slot for now; its result below */
            pe->gres[i] = (struct gen_res *)(uintptr_t)g->tbl[h];
        }
    }
    size_t B = (size_t)gen->max_batch;
    /* built results when the generator builds them (the chunks' bands made
     * where they were generated), unless the config asks for raw ones */
    int built = gen->run_built != NULL && !p->cfg.gen_raw;
    if (B > g->bcap)
    {
        gen_scratch_free(gen, g);
        g->bcap = B;
        g->ids = gen_host_alloc(gen, B * GA_CELLS);
        g->metas = built ? NULL : gen_host_alloc(gen, B * GA_CELLS);
        g->biome = gen_host_alloc(gen, B * 256);
        g->tops = gen_host_alloc(gen, B * 256);
        g->rand = gen_host_alloc(gen, B * sizeof *g->rand);
        g->built = built ? gen_host_alloc(gen, B * sizeof *g->built) : NULL;
        g->bands = built ? gen_host_alloc(gen, B * 16 * GA_BAND_BYTES) : NULL;
    }
    /* the ones an earlier round made, kept: taken; the rest to generate */
    int nm = 0;
    pthread_mutex_lock(&p->gkeep_mu);
    for (int u = 0; u < nu; ++u)
    {
        size_t i = gen_kfind(p, &g->uniq[u]);
        if (p->kt[i].r != NULL)
        {
            g->res[u] = p->kt[i].r;
            __atomic_add_fetch(&g->res[u]->refs, g->refs[u], __ATOMIC_RELAXED);
            continue;
        }
        g->res[u] = NULL;
        g->miss[nm] = u;
        g->todo[nm++] = g->uniq[u];
    }
    pthread_mutex_unlock(&p->gkeep_mu);
    *ok = 1;
    for (int a = 0; a < nm; a += (int)B)
    {
        int bn = nm - a < (int)B ? nm - a : (int)B;
        if (p->cfg.range) p->cfg.range("gen run", 1);
        int top = -1;
        if (*ok && (built ? gen->run_built(gen->ctx, g->todo + a, bn, g->built, g->bands, &top, g->ids, g->biome, g->tops, g->rand)
                          : gen->run(gen->ctx, g->todo + a, bn, g->ids, g->metas, g->biome, g->tops, g->rand)) != 0)
            *ok = 0;
        if (p->cfg.range) p->cfg.range(NULL, 0);
        if (p->cfg.range) p->cfg.range("gen results", 1);
        for (int k = 0; k < bn && *ok; ++k)
        {
            int u = g->miss[a + k], cls = GR_RAW;
            if (built && g->todo[a + k].dim == 0)
            {
                unsigned mask = g->built[k].mask;
                cls = GR_BUILT + (mask != 0 ? 32 - __builtin_clz(mask) : 0);
            }
            else if (!built && g->todo[a + k].dim == 0)
            {
                const uint8_t *mk = g->metas + (size_t)k * GA_CELLS;
                for (size_t q = 0; q < GA_CELLS && cls == GR_RAW; q += 8)
                {
                    uint64_t w8;
                    memcpy(&w8, mk + q, 8);
                    if (w8 != 0) cls = GR_RAW_METAS;
                }
            }
            struct gen_res *r = gen_res_new(p, cls);
            r->refs = g->refs[u];
            r->rand = g->rand[k];
            memcpy(r->biome, g->biome + (size_t)k * 256, 256);
            memcpy(r->tops, g->tops + (size_t)k * 256, 256);
            if (cls >= GR_BUILT)
            {
                r->built = g->built[k];
                memcpy(r->data, g->bands + (size_t)k * 16 * GA_BAND_BYTES, (size_t)(cls - GR_BUILT) * GA_BAND_BYTES);
            }
            else
            {
                memcpy(r->data, g->ids + (size_t)k * GA_CELLS, GA_CELLS);
                if (cls == GR_RAW_METAS) memcpy(r->data + GA_CELLS, g->metas + (size_t)k * GA_CELLS, GA_CELLS);
            }
            g->res[u] = r;
            pthread_mutex_lock(&p->gkeep_mu);
            gen_keep(p, &g->uniq[u], r);
            pthread_mutex_unlock(&p->gkeep_mu);
        }
        if (p->cfg.range) p->cfg.range(NULL, 0);
    }
    /* a dropped round: every request's result NULL (the references taken on
     * kept results, and the results made before the failure, go back) */
    if (!*ok)
        for (int u = 0; u < nu; ++u)
        {
            if (g->res[u] != NULL)
                for (int q = 0; q < g->refs[u]; ++q) gen_res_release(g->res[u]);
            g->res[u] = NULL;
        }
    g->made = nm;
    for (size_t e = 0; e < m; ++e)
    {
        struct pool_env *pe = &p->envs[take[e]];
        for (int i = 0; i < pe->ngreq; ++i) pe->gres[i] = g->res[(uintptr_t)pe->gres[i]];
    }
    return nu;
}

static void *gen_main(void *arg)
{
    struct gen_thread *gt = arg;
    struct pool *p = gt->p;
    /* the C generator's scratch (the CUDA one reads none) */
    neth_env = gt->genv;
    int *take = proc_malloc((size_t)p->n * sizeof *take);
    struct gen_scratch gs;
    memset(&gs, 0, sizeof gs);
    pthread_mutex_lock(&p->mu);
    for (;;)
    {
        while (!p->gstop && p->gq_n == 0) pthread_cond_wait(&p->gwork, &p->mu);
        if (p->gstop) break;
        /* the waiting envs in order, up to GEN_ROUND requests */
        size_t m = 0, total = 0;
        while (m < p->gq_n && (m == 0 || total + (size_t)p->envs[p->gq[m]].ngreq <= GEN_ROUND))
        {
            take[m] = p->gq[m];
            total += (size_t)p->envs[take[m]].ngreq;
            ++m;
        }
        memmove(p->gq, p->gq + m, (p->gq_n - m) * sizeof *p->gq);
        p->gq_n -= m;
        pthread_mutex_unlock(&p->mu);

        if (p->cfg.range) p->cfg.range("gen round", 1);
        uint64_t t0 = now_ns();
        int ok, nu = gen_round(p, gt->gen, &gs, take, m, total, &ok);
        uint64_t dt = now_ns() - t0;
        if (p->cfg.range) p->cfg.range(NULL, 0);

        pthread_mutex_lock(&p->mu);
        uint64_t ready_ns = now_ns();
        for (size_t i = 0; i < m; ++i)
        {
            struct pool_env *pe = &p->envs[take[i]];
            if (pe->gwait)
            {
                ++p->gst.waits;
                p->gst.wait_ns += ready_ns - pe->gwait_start;
            }
            if (pe->gdrop)
            {
                /* reset while the round ran: its results go, and a step
                 * waiting on it starts over */
                gen_drop(pe);
                pe->gdrop = 0;
                pe->gstate = 0;
                if (pe->gwait) jobq_push_front(env_queue(p, pe->id), (struct job){J_STEP, pe->id, NULL});
            }
            else
            {
                pe->gstate = 2;
                if (pe->gwait) jobq_push_front(env_queue(p, pe->id), (struct job){J_GENSTEP, pe->id, NULL});
            }
            pe->gwait = 0;
        }
        ++p->gst.batches;
        p->gst.steps += m;
        p->gst.requests += total;
        p->gst.unique += (uint64_t)gs.made;
        p->gst.kept += (uint64_t)(nu - gs.made);
        if (!ok) p->gst.failed += total;
        if ((uint64_t)gs.made > p->gst.max_unique) p->gst.max_unique = (uint64_t)gs.made;
        p->gst.gen_ns += dt;
        pthread_cond_broadcast(&p->work);
    }
    pthread_mutex_unlock(&p->mu);
    proc_free(take);
    proc_free(gs.tbl);
    proc_free(gs.refs);
    proc_free(gs.uniq);
    proc_free(gs.res);
    proc_free(gs.todo);
    proc_free(gs.miss);
    gen_scratch_free(gt->gen, &gs);
    return NULL;
}

/* ------------------------------------------------------------- the saves
 *
 * pool_start_from_env: the env's act log replayed twice from its start, each
 * replay in an image of its own with a view of its own (save slot k's views
 * n + 2k and n + 2k + 1). The two images, and the two views' data, then
 * differ only where they point into themselves, so start_scan names the
 * image's pointers as a load's twin does, and view_scan the view's. A replay
 * is step_job itself over the log (step_act reads it back), without the
 * shared generation cache, the tick hook and the profiler: none changes
 * what a tick computes, and the cache would make the two heaps differ (one
 * replay generating a chunk the other takes from the cache). */

/* the view of an env just reset to a saved start (or of a replay of one): a
 * fresh library copy with the start's view data written in, every word
 * that named the start's copy or image moved to the env's, and the image's
 * words that named the start's copy moved too */
static int view_restore(struct pool *p, struct pool_env *pe, const struct pool_start *s, char *err, size_t n)
{
    struct pool_vmem m;
    if (p->cfg.view_reload == NULL || p->cfg.view_resume == NULL)
    {
        seterr(err, n, "a saved start's view needs pool_config.view_reload and view_resume");
        return -1;
    }
    if (p->cfg.view_reload(p->cfg.view_ctx, pe->id, pe->ss, &m, err, n) != 0) return -1;
    if (m.nseg != s->vm.nseg || m.hi - m.lo != s->vm.hi - s->vm.lo)
    {
        seterr(err, n, "the view's library is not the one the start was saved with");
        return -1;
    }
    for (int i = 0; i < m.nseg; ++i)
        if (m.seg[i].len != s->vm.seg[i].len || m.seg[i].addr - m.lo != s->vm.seg[i].addr - s->vm.lo)
        {
            seterr(err, n, "the view's library is not the one the start was saved with (segment %d)", i);
            return -1;
        }
    /* the fresh copy holds its initialized data and zeros: the saved pages
     * over it */
    size_t at = 0;
    for (size_t i = 0; i < s->nvruns; ++i)
    {
        memcpy((void *)(m.lo + s->vruns[i].off), s->vbytes + at, s->vruns[i].len);
        at += s->vruns[i].len;
    }
    const int64_t dlib = (int64_t)(m.lo - s->vm.lo), dimg = (int64_t)((uintptr_t)pe->e - (uintptr_t)s->t);
    for (size_t i = 0; i < s->nvlib; ++i) *(int64_t *)(m.lo + s->vlib[i]) += dlib;
    for (size_t i = 0; i < s->nvimg; ++i) *(int64_t *)(m.lo + s->vimg[i]) += dimg;
    unsigned char *eb = (unsigned char *)pe->e;
    for (size_t i = 0; i < s->nlibptrs; ++i) *(int64_t *)(eb + s->libptrs[i]) += dlib;
    p->cfg.view_resume(p->cfg.view_ctx, pe->id);
    return 0;
}

/* push v[*k] = x, growing (process memory) */
static void push_off(uint64_t **v, size_t *k, size_t *cap, uint64_t x)
{
    if (*k == *cap)
    {
        size_t c = *cap ? 2 * *cap : 256;
        uint64_t *w = proc_malloc(c * sizeof *w);
        if (*k) memcpy(w, *v, *k * sizeof *w);
        proc_free(*v);
        *v = w;
        *cap = c;
    }
    (*v)[(*k)++] = x;
}

/* The replays' views' data against each other: a word equal in both is a
 * value (or names memory both share: the program, the C library, the
 * process's tables), one naming the same place in each library copy or each
 * image is a pointer to rebase; anything else is an error. */
static int view_scan(struct pool_start *s, const struct env *twin, char *err, size_t n)
{
    const struct pool_vmem *a = &s->rvm[0], *b = &s->rvm[1];
    if (a->nseg != b->nseg || a->hi - a->lo != b->hi - b->lo)
    {
        seterr(err, n, "the replays' views differ in layout");
        return 0;
    }
    for (int i = 0; i < a->nseg; ++i)
        if (a->seg[i].len != b->seg[i].len || a->seg[i].addr - a->lo != b->seg[i].addr - b->lo)
        {
            seterr(err, n, "the replays' views differ in layout (segment %d)", i);
            return 0;
        }
    const uintptr_t lsize = a->hi - a->lo, tlo = (uintptr_t)s->t, wlo = (uintptr_t)twin, size = s->t->img.size;
    size_t cap_lib = 0, cap_img = 0, cap_runs = 0, cap_bytes = 0, bad = 0;
    for (int i = 0; i < a->nseg; ++i)
    {
        const uintptr_t sa = a->seg[i].addr, se = sa + a->seg[i].len, init = sa + a->seg[i].init;
        /* page by page (the segment's ends cut to it): a page the file
         * initializes, or that either replay wrote, is kept */
        for (uintptr_t p0 = sa; p0 < se;)
        {
            uintptr_t p1 = (p0 & ~(uintptr_t)(PAGE - 1)) + PAGE;
            if (p1 > se) p1 = se;
            const unsigned char *pa = (const unsigned char *)p0, *pb = (const unsigned char *)(b->seg[i].addr + (p0 - sa));
            int keep = p0 < init;
            for (size_t k = 0; !keep && k < p1 - p0; ++k) keep = pa[k] | pb[k];
            if (keep)
            {
                const uint64_t off = p0 - a->lo, len = p1 - p0;
                if (s->nvruns && s->vruns[s->nvruns - 1].off + s->vruns[s->nvruns - 1].len == off) s->vruns[s->nvruns - 1].len += len;
                else
                {
                    if (s->nvruns == cap_runs)
                    {
                        cap_runs = cap_runs ? 2 * cap_runs : 64;
                        void *v = proc_malloc(cap_runs * sizeof *s->vruns);
                        if (s->nvruns) memcpy(v, s->vruns, s->nvruns * sizeof *s->vruns);
                        proc_free(s->vruns);
                        s->vruns = v;
                    }
                    s->vruns[s->nvruns].off = off;
                    s->vruns[s->nvruns].len = len;
                    ++s->nvruns;
                }
                if (s->nvbytes + len > cap_bytes)
                {
                    size_t c = cap_bytes ? 2 * cap_bytes : 1 << 20;
                    while (c < s->nvbytes + len) c *= 2;
                    unsigned char *v = proc_malloc(c);
                    if (s->nvbytes) memcpy(v, s->vbytes, s->nvbytes);
                    proc_free(s->vbytes);
                    s->vbytes = v;
                    cap_bytes = c;
                }
                memcpy(s->vbytes + s->nvbytes, pa, len);
                s->nvbytes += len;
                /* the page's words: the ones naming each copy or image */
                for (uintptr_t w = (p0 + 7) & ~(uintptr_t)7; w + 8 <= p1; w += 8)
                {
                    uint64_t x, y;
                    memcpy(&x, (const void *)w, 8);
                    memcpy(&y, (const void *)(b->seg[i].addr + (w - sa)), 8);
                    if (x == y) continue;
                    if (x - a->lo < lsize && y - b->lo < lsize && x - a->lo == y - b->lo)
                        push_off(&s->vlib, &s->nvlib, &cap_lib, w - a->lo);
                    else if (x - tlo < size && y - wlo < size && x - tlo == y - wlo)
                        push_off(&s->vimg, &s->nvimg, &cap_img, w - a->lo);
                    else
                        ++bad;   /* a value of each run's own (as start_scan's) */
                }
            }
            p0 = p1;
        }
    }
    s->view_diffs = bad;
    (void)err;
    (void)n;
    return 1;
}

/* One replay of save s (WHICH 0 or 1): a new image reset to the parent start, with the spare view n + 2 * slot + which,
 * and the log's steps run by save_step_one. The two replays of a save run in step, on one worker (save_job): see there. */
struct save_rep {
    struct pool_env *pe;
    int heap, bad;
    const unsigned char *q;
    int64_t steps;
};

static void save_open(struct pool *p, struct pool_start *s, int which, struct save_rep *r)
{
    char *err = s->rerr[which];
    const size_t en = sizeof s->rerr[which];
    struct pool_env *pe = proc_calloc(1, sizeof *pe);
    if (pe == NULL) abort();
    pe->id = p->n + 2 * s->slot + which;
    pe->replay = 1;
    pe->replay_rung = s->rung;
    pe->res.id = pe->id;
    pe->e = env_new();
    if (pe->e == NULL)
    {
        seterr(err, en, "no image for the save's replay (the address window is full)");
        proc_free(pe);
        r->bad = 1;
        return;
    }
    r->pe = pe;
    neth_env = pe->e;
    reset_job(p, pe, s->parent);
    /* after the copy, which brought the parent's flag: no sweep of its own; save_job sweeps the pair together */
    pe->e->grave.hold = 1;
    r->heap = image_heap_set(pe->e, 1);
    if (s->parent->vm.nseg > 0) r->bad = view_restore(p, pe, s->parent, err, en) != 0;
    else if (p->cfg.view_reset) r->bad = p->cfg.view_reset(p->cfg.view_ctx, pe->id, pe->ss, err, en) != 0;
    r->q = s->log->b;
}

/* the log's next step in the replay's image */
static void save_step_one(struct pool *p, struct pool_start *s, int which, struct save_rep *r)
{
    char *err = s->rerr[which];
    const size_t en = sizeof s->rerr[which];
    struct pool_env *pe = r->pe;
    const unsigned char *end = s->log->b + s->log->n, *q = r->q;
    neth_env = pe->e;
    const uint32_t *h = (const uint32_t *)q;
    if (h[0] != LOG_STEP)
    {
        seterr(err, en, "the act log is damaged at byte %zu", (size_t)(q - s->log->b));
        r->bad = 1;
        return;
    }
    const unsigned char *e1 = q + 8;
    for (uint32_t k = 0; k < h[1] && e1 < end; ++k)
        e1 += (sizeof(struct log_ent) + ((const struct log_ent *)e1)->len + 7) & ~(size_t)7;
    pe->rp = q + 8;
    pe->rend = e1;
    pe->n = (int)h[1];
    pe->per_tick = 1;
    memset(&pe->res, 0, offsetof(struct pool_result, hud));
    pe->res.id = pe->id;
    step_job(p, pe);
    if ((pe->res.flags & PF_ERROR) || pe->res.ticks != (int)h[1] || pe->rp != e1)
    {
        seterr(err, en, "the replay's step %lld ran %d of %u ticks: %.300s", (long long)r->steps, pe->res.ticks, h[1],
               pe->res.err);
        r->bad = 1;
        return;
    }
    ++r->steps;
    r->q = e1;
}

static void save_close(struct pool *p, struct pool_start *s, int which, struct save_rep *r)
{
    char *err = s->rerr[which];
    const size_t en = sizeof s->rerr[which];
    struct pool_env *pe = r->pe;
    if (pe == NULL) return;
    neth_env = pe->e;
    int bad = r->bad;
    if (!bad && pe->t != s->tick)
    {
        seterr(err, en, "the replay ended at tick %lld, the env at %lld", (long long)pe->t, (long long)s->tick);
        bad = 1;
    }
    /* the session's hook context is the env's own (reset_job sets it), the
     * tile pass's celestial angle a pointer to its caller's frame
     * (serverreplay.c sr_tile_pass) and the shared path search's stamp the
     * worker's (pathfind.c): each set before it is read, so cleared */
    pe->ss->hooks.ctx = NULL;
    pe->e->tileticks.cos_table = NULL;
    pe->e->ai.nav_shared.f.vstamp = 0;
    if (!bad && p->cfg.view_reset && p->cfg.view_mem(p->cfg.view_ctx, pe->id, &s->rvm[which]) != 0)
    {
        seterr(err, en, "the replay's view memory cannot be read");
        bad = 1;
    }
    image_heap_set(pe->e, r->heap);
    if (!bad)
    {
        s->rep[which] = pe->e;
        pe->e = NULL;
    }
    else if (!err[0])
        seterr(err, en, "the replay failed");
    if (pe->e) env_free(pe->e);
    proc_free(pe->stats_v);
    proc_free(pe->stats_p);
    proc_free(pe);
}

/* Both replays of save s, from job 0 (job 1 has nothing to do: it only counts). The two images must end with every
 * living and list entry in the same pool slot, which two independent sweeps do not give (grave.h), and holding every
 * burial to the end keeps the whole chain's dead livings resident (a 400,000 tick resume: 21 GB at 32 envs). So the
 * replays run in step on this worker, a step of one then the same step of the other, and after each pair of steps
 * grave_pair_collect sweeps the two together once they have buried what a sweep waits for: a record is freed only
 * when neither image's scan reaches it, so the burials held are bounded by the threshold and not by the log. */
static void save_job(struct pool *p, struct pool_start *s, int which)
{
    if (which != 0) return;
    struct env *saved = neth_env;
    struct save_rep r[2] = {{0}, {0}};
    for (int w = 0; w < 2; ++w) save_open(p, s, w, &r[w]);
    const unsigned char *end = s->log->b + s->log->n;
    while (!r[0].bad && !r[1].bad && r[0].q < end)
    {
        for (int w = 0; w < 2 && !r[0].bad && !r[1].bad; ++w) save_step_one(p, s, w, &r[w]);
        if (!r[0].bad && !r[1].bad) grave_pair_collect(r[0].pe->e, r[1].pe->e);
    }
    for (int w = 0; w < 2; ++w) save_close(p, s, w, &r[w]);
    neth_env = saved;
}

/* both replays done: the scan (as a load's), the view's data, the parent
 * and the log let go */
static void save_finish(struct pool *p, struct pool_start *s)
{
    int ok = s->rep[0] != NULL && s->rep[1] != NULL;
    if (!ok) seterr(s->err, sizeof s->err, "%s", s->rerr[s->rep[0] == NULL ? 0 : 1]);
    if (ok)
    {
        s->t = s->rep[0];
        s->twin = s->rep[1];
        s->rep[0] = s->rep[1] = NULL;
        /* the region spill files are each image's own (their descriptors
         * differ): the twin takes the image's for the scan */
        int fd1 = s->twin->rspill.fd1, fd2 = s->twin->rspill.fd2;
        s->twin->rspill.fd1 = s->t->rspill.fd1;
        s->twin->rspill.fd2 = s->t->rspill.fd2;
        grave_sweep_joint(s->t, s->twin);
        ok = start_scan(s, p->pagemap, s->err, sizeof s->err);
        s->twin->rspill.fd1 = fd1;
        s->twin->rspill.fd2 = fd2;
        /* a word that differs between the replays and names neither copy
         * is a value each run makes its own (the view's frame timings, a
         * worker's search stamp): the start keeps the first replay's, as
         * the env itself kept a third; counted (pool_start_info) */
        if (ok && p->cfg.view_reset) ok = view_scan(s, s->twin, s->err, sizeof s->err);
        if (ok && p->cfg.view_reset) s->vm = s->rvm[0];
        env_free(s->twin);
        s->twin = NULL;
    }
    start_unref(s->parent);
    s->parent = NULL;
    log_free(s->log);
    proc_free(s->log);
    s->log = NULL;
    s->ok = ok;
    s->load_ms += now_ms();   /* from pool_start_from_env's call */
}

/* ---------------------------------------------------------- the workers */

struct worker {
    struct pool *p;
    int index;
    int placed;                     /* cfg.place: set is the worker's CPUs */
    cpu_set_t set;
};

/* cfg.steal: for a worker of queue qi with nothing to do, the queue holding
 * the most jobs beyond its own idle workers (who take them first), in qi's
 * cache domain if one has any; NULL none */
static struct jobq *steal_queue(struct pool *p, int qi)
{
    struct jobq *best = NULL;
    int most = 0, near = 0;
    for (int q = 0; q < p->nq; ++q)
    {
        int extra = (int)p->wq[q].n - p->qidle[q], same = p->qdom[q] == p->qdom[qi];
        if (q == qi || extra <= 0 || (near && !same)) continue;
        if ((same && !near) || extra > most) { best = &p->wq[q]; most = extra; near = same; }
    }
    return best;
}

/* fast: a step from ring r, without the lock; 0 when it is empty. The slot is read before the claim: it is not
 * written again while the head stands at it (the ring's entries from the head on are distinct queued envs, at most
 * n) */
static int ring_claim(struct pool *p, struct pool_ring *r, int *id)
{
    uint64_t h = __atomic_load_n(&r->head, __ATOMIC_RELAXED);
    for (;;)
    {
        if (h >= __atomic_load_n(&r->tail, __ATOMIC_ACQUIRE)) return 0;
        int v = r->v[h % (uint64_t)p->n];
        if (__atomic_compare_exchange_n(&r->head, &h, h + 1, 1, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
        {
            *id = v;
            return 1;
        }
    }
}

/* a queued step to its queue (fast: its queue's ring, seen once step_publish publishes the ring's tail); under the
 * lock */
static void step_push(struct pool *p, int id)
{
    if (!p->fast)
    {
        jobq_push(env_queue(p, id), (struct job){J_STEP, id, NULL});
        return;
    }
    struct pool_ring *r = &p->rings[p->wq ? (int)(env_queue(p, id) - p->wq) : 0];
    r->v[(r->tail + r->add++) % (uint64_t)p->n] = id;
}

/* the rings' pushed steps published, each ring's tail once (under the lock) */
static void step_publish(struct pool *p)
{
    for (int q = 0; q < p->nring; ++q)
        if (p->rings[q].add)
        {
            __atomic_store_n(&p->rings[q].tail, p->rings[q].tail + p->rings[q].add, __ATOMIC_RELEASE);
            p->rings[q].add = 0;
        }
}

/* cfg.steal: a step from the ring holding the most beyond its own idle workers, as steal_queue */
static int ring_steal(struct pool *p, int qi, int *id)
{
    for (int tries = 0; tries < 4; ++tries)
    {
        int best = -1;
        int64_t most = 0;
        for (int q = 0; q < p->nring; ++q)
        {
            const struct pool_ring *r = &p->rings[q];
            int64_t extra = (int64_t)(__atomic_load_n(&r->tail, __ATOMIC_ACQUIRE) - __atomic_load_n(&r->head, __ATOMIC_RELAXED)) -
                            (p->qidle ? __atomic_load_n(&p->qidle[q], __ATOMIC_RELAXED) : 0);
            if (q != qi && extra > most) { best = q; most = extra; }
        }
        if (best < 0) return 0;
        if (ring_claim(p, &p->rings[best], id)) return 1;
    }
    return 0;
}

/* fast: a worker's step without the lock: 1 taken (its ring's, else a steal), 0 none, -1 its locked queues hold a
 * job (taken first, under the lock) */
#define POOL_WORKER_SPIN_NS POOL_WSPIN
static int ring_take_step(struct pool *p, int rq, int *id)
{
    if (__atomic_load_n(&p->q.n, __ATOMIC_RELAXED) != 0 || (p->wq && __atomic_load_n(&p->wq[rq].n, __ATOMIC_RELAXED) != 0))
        return -1;
    return ring_claim(p, &p->rings[rq], id) || (p->cfg.steal && ring_steal(p, rq, id));
}

/* fast: a step's end without the lock: its counts in the worker's, its id published in dring, and the waiting
 * consumer woken once what it waits for is published or nothing is left in flight (the publish and the count's
 * fall come before the read of d_want, as the consumer's d_want comes before its read of the slots: one of the
 * two sees the other) */
static void ring_done(struct pool *p, struct pool_wstat *w, struct pool_env *pe, uint64_t di, uint64_t dc, uint64_t dt)
{
    pe->res.instructions = di + pe->ga_ins;
    pe->res.ns = dt + pe->ga_ns;
    pe->ga_ins = pe->ga_ns = 0;
    __atomic_store_n(&w->instructions, w->instructions + di, __ATOMIC_RELAXED);
    __atomic_store_n(&w->cycles, w->cycles + dc, __ATOMIC_RELAXED);
    __atomic_store_n(&w->ns, w->ns + dt, __ATOMIC_RELAXED);
    __atomic_store_n(&w->steps, w->steps + 1, __ATOMIC_RELAXED);
    __atomic_store_n(&w->ticks, w->ticks + (uint64_t)pe->res.ticks, __ATOMIC_RELAXED);
    __atomic_store_n(&pe->state, ENV_DONE, __ATOMIC_RELAXED);
    uint64_t c = __atomic_fetch_add(&p->d_claim, 1, __ATOMIC_RELAXED);
    struct pool_dslot *d = &p->dring[c % (uint64_t)p->n];
    d->id = pe->id;
    __atomic_store_n(&d->seq, c + 1, __ATOMIC_SEQ_CST);
    int left = __atomic_sub_fetch(&p->stepping, 1, __ATOMIC_SEQ_CST);
    int want = __atomic_load_n(&p->d_want, __ATOMIC_SEQ_CST);
    if (want == INT_MAX) return;
    uint64_t have = __atomic_load_n(&p->d_claim, __ATOMIC_SEQ_CST) - __atomic_load_n(&p->d_read, __ATOMIC_SEQ_CST);
    if (left > 0 && have < (uint64_t)want) return;
    pthread_mutex_lock(&p->mu);
    pthread_cond_broadcast(&p->done);
    pthread_mutex_unlock(&p->mu);
}

/* fast: the published ends from d_read on, at most max (under the lock, or a hint without it) */
static int ring_ready(struct pool *p, int max)
{
    uint64_t r = __atomic_load_n(&p->d_read, __ATOMIC_RELAXED);
    int k = 0;
    while (k < max && __atomic_load_n(&p->dring[(r + (uint64_t)k) % (uint64_t)p->n].seq, __ATOMIC_SEQ_CST) == r + (uint64_t)k + 1)
        ++k;
    return k;
}

/* fast: before the locked wait, up to POOL_POLL_SPIN_NS spent watching the ring without the lock: the ends a
 * consumer waits for come microseconds apart, and a sleeping consumer costs the step that wakes it the lock and a
 * futex call */
#define POOL_POLL_SPIN_NS POOL_PSPIN
static void ring_spin(struct pool *p, int want)
{
    uint64_t t0 = now_ns();
    for (int i = 1; ring_ready(p, want) < want && __atomic_load_n(&p->stepping, __ATOMIC_SEQ_CST) > 0; ++i)
    {
        if ((i & 63) == 0 && now_ns() - t0 > POOL_POLL_SPIN_NS) return;
#if defined(__x86_64__)
        __builtin_ia32_pause();
#endif
    }
}

/* fast: wait (under the lock) until want ends are published or nothing is in flight; the ends published */
static int ring_wait(struct pool *p, int want)
{
    for (;;)
    {
        int k = ring_ready(p, want);
        if (k >= want || __atomic_load_n(&p->stepping, __ATOMIC_SEQ_CST) == 0) return k;
        __atomic_store_n(&p->d_want, want, __ATOMIC_SEQ_CST);
        k = ring_ready(p, want);
        if (k < want && __atomic_load_n(&p->stepping, __ATOMIC_SEQ_CST) > 0) pthread_cond_wait(&p->done, &p->mu);
        __atomic_store_n(&p->d_want, INT_MAX, __ATOMIC_SEQ_CST);
    }
}

/* fast: take k published ends (single consumer; lock-free when called from pool_poll_min fast path) */
static int ring_take(struct pool *p, int k, const struct pool_result **out)
{
    uint64_t r = p->d_read;
    for (int i = 0; i < k; ++i)
    {
        int id = p->dring[(r + (uint64_t)i) % (uint64_t)p->n].id;
        __atomic_store_n(&p->envs[id].state, ENV_IDLE, __ATOMIC_RELAXED);
        out[i] = &p->envs[id].res;
    }
    __atomic_store_n(&p->d_read, r + (uint64_t)k, __ATOMIC_SEQ_CST);
    return k;
}

static void *worker_main(void *arg)
{
    struct worker *w = arg;
    struct pool *p = w->p;
    if (w->placed) pthread_setaffinity_np(pthread_self(), sizeof w->set, &w->set);
    /* one scratch for every env this worker steps (env.h: neth_scratch),
     * its pages first written here, near the worker */
    neth_thread_scratch = env_scratch_new();
    int fd = p->cfg.no_counters ? -1 : pmu_open();
    /* a finished job's wake of the done waiters, sent once the lock is let go (a broadcast with a waiter is a
     * futex syscall: under the lock it held every other worker's pop behind it) */
    int wake_done = 0, wake_work = 0, locked = 1;
    struct pool_start *unref = NULL;    /* async_reset: a finished reset's start, let go after the unlock */
    pthread_mutex_lock(&p->mu);
    for (;;)
    {
        struct job j;
        int rid, rq = p->wq ? p->wqof[w->index] : 0, got = 0;
        /* fast: a step from the worker's ring, else another's (cfg.steal), without the lock while its queues hold
         * no other job (those first, under the lock); with nothing to take, up to POOL_WORKER_SPIN_NS of tries
         * before the lock and the sleep (a sleeping worker costs pool_step a futex broadcast) */
        for (uint64_t i = 0, s0 = 0; !locked && p->fast && (got = ring_take_step(p, rq, &rid)) == 0; ++i)
        {
            if ((i & 63) == 0)
            {
                uint64_t t = now_ns();
                if (s0 == 0) s0 = t;
                else if (t - s0 > POOL_WORKER_SPIN_NS) break;
            }
#if defined(__x86_64__)
            __builtin_ia32_pause();
#endif
        }
        if (got == 1)
            j = (struct job){J_STEP, rid, NULL};
        else
        {
            if (!locked) pthread_mutex_lock(&p->mu);
            locked = 1;
            int qi = p->wq ? p->wqof[w->index] : -1, ring = 0;
            struct jobq *mine = p->wq ? &p->wq[qi] : NULL, *take = NULL;
            while (!p->stop)
            {
                if (mine && mine->n) take = mine;
                else if (p->q.n) take = &p->q;
                else if (p->fast && (ring = ring_claim(p, &p->rings[rq], &rid))) break;
                else if (mine && p->cfg.steal && (take = steal_queue(p, qi)) != NULL) break;
                else if (p->fast && p->cfg.steal && (ring = ring_steal(p, rq, &rid))) break;
                if (take) break;
                if (wake_done) pthread_cond_broadcast(&p->done), wake_done = 0;
                if (wake_work) pthread_cond_broadcast(&p->work), wake_work = 0;
                if (unref) start_unref(unref), unref = NULL;
                if (mine) ++p->qidle[qi];
                ++p->nsleep;
                pthread_cond_wait(&p->work, &p->mu);
                --p->nsleep;
                if (mine) --p->qidle[qi];
            }
            if (p->stop) break;
            j = ring ? (struct job){J_STEP, rid, NULL} : jobq_pop(take);
            pthread_mutex_unlock(&p->mu);
            if (wake_done) pthread_cond_broadcast(&p->done), wake_done = 0;
            if (wake_work) pthread_cond_broadcast(&p->work), wake_work = 0;
            if (unref) start_unref(unref), unref = NULL;
        }
        locked = 0;

        struct pmu c0 = pmu_read(fd);
        uint64_t t0 = now_ns();
        struct pool_env *pe = j.kind == J_LOAD || j.kind == J_LOADBG || j.kind == J_SAVE ? NULL : &p->envs[j.id];
        int gact = GA_NONE;   /* cfg.gen: the job's generation act (gen_act) */
        if (p->cfg.range)
            p->cfg.range(j.kind == J_RESET ? "pool reset" : j.kind == J_LOAD || j.kind == J_LOADBG ? "pool load" : j.kind == J_SAVE ? "pool save" : "pool step", 1);
        if (j.kind == J_LOAD) start_load_job(p, j.start);
        else if (j.kind == J_LOADBG)
        {
            start_load_job(p, j.start);
            if (j.start->ok && !start_world_check(p, j.start)) j.start->ok = 0;
        }
        else if (j.kind == J_SAVE)
        {
            struct pool_start *s = j.start;
            save_job(p, s, j.id);
            pthread_mutex_lock(&p->mu);
            int last = --s->jobs_left == 0;
            pthread_mutex_unlock(&p->mu);
            if (last)
            {
                save_finish(p, s);
                pthread_mutex_lock(&p->mu);
                s->making = 0;
                p->save_busy &= ~(1u << s->slot);
                --p->saving;
                pthread_cond_broadcast(&p->done);
                pthread_mutex_unlock(&p->mu);
                start_unref(s);   /* the making's reference */
            }
        }
        else
        {
            struct env *saved = neth_env;
            neth_env = pe->e;
            if (j.kind == J_RESET) reset_job(p, pe, j.start);
#ifdef LIVFAULT_AUDIT
            if (j.kind == J_RESET) living_audit("reset", j.id);
#endif
            /* whatever the pool makes for the env stays in its image */
            int heap = image_heap_set(pe->e, 1);
            if (j.kind == J_RESET)
            {
                feed_reset(pe->feed, pe->ss, pe->e);
                /* a saved start's view is its own data in a fresh copy */
                if (j.start->vm.nseg > 0) pe->view_bad = view_restore(p, pe, j.start, pe->view_err, sizeof pe->view_err) != 0;
                else
                    pe->view_bad = p->cfg.view_reset &&
                                   p->cfg.view_reset(p->cfg.view_ctx, pe->id, pe->ss, pe->view_err, sizeof pe->view_err) != 0;
            }
            else
            {
                /* the step's result cleared where it is written (pool_step queued it) */
                if (j.kind == J_STEP)
                {
                    memset(&pe->res, 0, offsetof(struct pool_result, hud));
                    pe->res.id = j.id;
                }
                if (p->cfg.gen != NULL) gact = gen_step_begin(p, pe);
                if (gact == GA_NONE)
                {
                    step_job(p, pe);
                    if (p->cfg.gen != NULL) gact = gen_step_end(p, pe);
                }
            }
#ifdef LIVFAULT_AUDIT
            living_audit(j.kind == J_RESET ? "reset's view" : "step", j.id);
#endif
            image_heap_set(pe->e, heap);
            neth_env = saved;
        }
        if (p->cfg.range) p->cfg.range(NULL, 0);
        struct pmu c1 = pmu_read(fd);
        uint64_t di, dc, dt = now_ns() - t0;
        pmu_delta(c0, c1, &di, &dc);

        if (p->fast && j.kind == J_STEP)
        {
            ring_done(p, &p->wst[w->index], pe, di, dc, dt);
            continue;
        }
        pthread_mutex_lock(&p->mu);
        locked = 1;
        int waits = gact == GA_WAIT || gact == GA_SUBMIT_WAIT;
        if (gact != GA_NONE) gen_act(p, pe, gact);
        if (waits)
        {
            /* the step goes on when its round ends */
            pe->ga_ins += di;
            pe->ga_ns += dt;
            p->stats.instructions += di;
            p->stats.ns += dt;
        }
        else if (j.kind == J_STEP || j.kind == J_GENSTEP)
        {
            pe->res.instructions = di + pe->ga_ins;
            pe->res.ns = dt + pe->ga_ns;
            pe->ga_ins = pe->ga_ns = 0;
            p->stats.instructions += di;
            p->stats.cycles += dc;
            p->stats.ns += dt;
            ++p->stats.steps;
            p->stats.ticks += (uint64_t)pe->res.ticks;
            pe->state = ENV_DONE;
            p->doneq[(p->done_head + p->ndone) % (size_t)p->n] = j.id;
            ++p->ndone;
            __atomic_sub_fetch(&p->stepping, 1, __ATOMIC_SEQ_CST);
            wake_done = 1;
        }
        else if (j.kind == J_LOADBG)
        {
            j.start->making = 0;
            --p->loading;
            wake_done = 1;
        }
        else if (j.kind != J_SAVE)
        {
            if (j.kind == J_RESET)
            {
                p->stats.reset_ns += dt;
                p->stats.reset_instructions += di;
                ++p->stats.resets;
            }
            if (j.kind == J_RESET && pe->resetting)
            {
                /* async: the step queued behind the reset goes in now; the job's start let go after the unlock */
                pe->resetting = 0;
                --p->resets_left;
                if (pe->step_after)
                {
                    pe->step_after = 0;
                    step_push(p, j.id);
                    step_publish(p);
                    wake_work |= p->nsleep > 0 || !p->fast;
                }
                unref = j.start;
                wake_done = 1;
            }
            else if (--p->sync_left == 0)
                pthread_cond_broadcast(&p->done);
        }
    }
    if (wake_done) pthread_cond_broadcast(&p->done);
    if (wake_work) pthread_cond_broadcast(&p->work);
    pthread_mutex_unlock(&p->mu);
    if (unref) start_unref(unref);
    if (fd >= 0) close(fd);
    env_scratch_free(neth_thread_scratch);
    neth_thread_scratch = NULL;
    free(w);
    return NULL;
}

const char *pool_place_name(int place)
{
    static const char *const names[] = {"none", "cores", "siblings", "spread", "domain", "doms"};
    return place >= 0 && place <= PP_DOMS ? names[place] : "?";
}

int pool_place_parse(const char *s)
{
    for (int i = 0; i <= PP_DOMS; ++i)
        if (!strcmp(s, pool_place_name(i))) return i;
    return -1;
}

/* the first number in a sysfs list file, -1 when it does not read */
static int sysfs_first(int cpu, const char *what)
{
    char path[128];
    snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/%s", cpu, what);
    FILE *f = fopen(path, "r");
    if (f == NULL) return -1;
    int v = -1;
    if (fscanf(f, "%d", &v) != 1) v = -1;
    fclose(f);
    return v;
}

struct cpu_slot { int cpu, core, dom, rank, nth; };

static int slot_cmp_cores(const void *a, const void *b)
{
    const struct cpu_slot *x = a, *y = b;
    if (x->rank != y->rank) return x->rank - y->rank;
    if (x->dom != y->dom) return x->dom - y->dom;
    return x->cpu - y->cpu;
}

static int slot_cmp_siblings(const void *a, const void *b)
{
    const struct cpu_slot *x = a, *y = b;
    if (x->dom != y->dom) return x->dom - y->dom;
    if (x->core != y->core) return x->core - y->core;
    return x->rank - y->rank;
}

static int slot_cmp_spread(const void *a, const void *b)
{
    const struct cpu_slot *x = a, *y = b;
    if (x->rank != y->rank) return x->rank - y->rank;
    if (x->nth != y->nth) return x->nth - y->nth;
    return x->dom - y->dom;
}

/* The CPUs of the mask in cfg.place's order (PP_DOMAIN: PP_SPREAD's, so fewer
 * workers than cores still reach every domain; pin 2 without a place:
 * PP_CORES's), with each CPU's core (its first sibling), cache domain
 * (its L3's id) and rank among its core's CPUs in the mask. Where sysfs does
 * not say, a CPU is its own core and every CPU one domain. Returns how many. */
static int place_order(const cpu_set_t *mask, int place, struct cpu_slot *v)
{
    int k = 0;
    for (int c = 0; c < CPU_SETSIZE; ++c)
    {
        if (!CPU_ISSET(c, mask)) continue;
        int core = sysfs_first(c, "topology/thread_siblings_list"), dom = sysfs_first(c, "cache/index3/id");
        v[k] = (struct cpu_slot){c, core < 0 ? c : core, dom < 0 ? 0 : dom, 0, 0};
        for (int i = 0; i < k; ++i) v[k].rank += v[i].core == v[k].core;
        ++k;
    }
    qsort(v, (size_t)k, sizeof *v, slot_cmp_cores);
    /* each CPU's place among its domain's CPUs of its rank */
    for (int i = 0; i < k; ++i)
        for (int j = 0; j < i; ++j) v[i].nth += v[j].dom == v[i].dom && v[j].rank == v[i].rank;
    if (place == PP_SIBLINGS) qsort(v, (size_t)k, sizeof *v, slot_cmp_siblings);
    if (place == PP_SPREAD || place == PP_DOMS || place == PP_DOMAIN) qsort(v, (size_t)k, sizeof *v, slot_cmp_spread);
    return k;
}

/* the calling thread's CPUs in PP_DOMS's order (v, *k of them) and their
 * domains' L3 ids in increasing order (doms); how many domains */
static int mask_domains(struct cpu_slot *v, int *k, int *doms)
{
    cpu_set_t mask;
    *k = 0;
    if (sched_getaffinity(0, sizeof mask, &mask) != 0) return 0;
    *k = place_order(&mask, PP_DOMS, v);
    int nd = 0;
    for (int i = 0; i < *k; ++i)
    {
        int q = 0;
        while (q < nd && doms[q] != v[i].dom) ++q;
        if (q == nd) doms[nd++] = v[i].dom;
    }
    return nd;
}

int pool_domains(void)
{
    struct cpu_slot *v = malloc(CPU_SETSIZE * sizeof *v);
    int k, doms[CPU_SETSIZE], nd = mask_domains(v, &k, doms);
    free(v);
    return nd > 0 ? nd : 1;
}

int pool_domain_pin(int d)
{
    struct cpu_slot *v = malloc(CPU_SETSIZE * sizeof *v);
    int k, doms[CPU_SETSIZE], nd = mask_domains(v, &k, doms), rc = -1;
    if (nd > 0)
    {
        cpu_set_t set;
        CPU_ZERO(&set);
        for (int i = 0; i < k; ++i)
            if (v[i].dom == doms[((d % nd) + nd) % nd]) CPU_SET(v[i].cpu, &set);
        rc = pthread_setaffinity_np(pthread_self(), sizeof set, &set) == 0 ? 0 : -1;
    }
    free(v);
    return rc;
}

/* p->nthreads workers (and with cfg.pin their queues), each on its CPUs
 * (cfg.place) */
static void workers_start(struct pool *p)
{
    p->th = calloc((size_t)p->nthreads, sizeof *p->th);
    struct cpu_slot *order = NULL;
    int ncpu = 0;
    if (p->cfg.place != PP_NONE || p->cfg.pin == 2 || p->cfg.steal)
    {
        order = malloc(CPU_SETSIZE * sizeof *order);
        ncpu = place_order(&p->mask, p->cfg.place, order);
    }
    if (p->cfg.pin)
    {
        p->wqof = calloc((size_t)p->nthreads, sizeof *p->wqof);
        p->nq = p->nthreads;
        if (p->cfg.pin == 2 && ncpu > 0)
        {
            /* a queue per domain the workers are in, in the workers' order */
            int doms[CPU_SETSIZE];
            p->nq = 0;
            for (int i = 0; i < p->nthreads; ++i)
            {
                int d = order[i % ncpu].dom, q = 0;
                while (q < p->nq && doms[q] != d) ++q;
                if (q == p->nq) doms[p->nq++] = d;
                p->wqof[i] = q;
            }
        }
        else
            for (int i = 0; i < p->nthreads; ++i) p->wqof[i] = i;
        p->wq = calloc((size_t)p->nq, sizeof *p->wq);
        p->qidle = calloc((size_t)p->nq, sizeof *p->qidle);
        p->qdom = calloc((size_t)p->nq, sizeof *p->qdom);
        for (int i = 0; i < p->nthreads; ++i) p->qdom[p->wqof[i]] = ncpu > 0 ? order[i % ncpu].dom : 0;
    }
    /* the rings: one, or one a domain queue (pin 2); a queue a worker (pin 1) keeps its locked queues */
    p->fast = p->cfg.gen == NULL && (p->wq == NULL || p->cfg.pin == 2);
    p->nring = p->fast ? (p->wq ? p->nq : 1) : 0;
    p->rings = p->nring ? aligned_alloc(64, (size_t)p->nring * sizeof *p->rings) : NULL;
    for (int r = 0; r < p->nring; ++r)
    {
        memset(&p->rings[r], 0, sizeof p->rings[r]);
        p->rings[r].v = calloc((size_t)p->n, sizeof *p->rings[r].v);
    }
    p->wst = aligned_alloc(64, (size_t)p->nthreads * sizeof *p->wst);
    memset(p->wst, 0, (size_t)p->nthreads * sizeof *p->wst);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    /* the load recurses deep in places (test_interleave's runners take 256 MB) */
    pthread_attr_setstacksize(&attr, (size_t)256 << 20);
    for (int i = 0; i < p->nthreads; ++i)
    {
        struct worker *w = calloc(1, sizeof *w);
        w->p = p;
        w->index = i;
        if (p->cfg.place != PP_NONE && ncpu > 0)
        {
            const struct cpu_slot *s = &order[i % ncpu];
            w->placed = 1;
            CPU_ZERO(&w->set);
            if (p->cfg.place == PP_DOMAIN || p->cfg.place == PP_DOMS)
            {
                for (int c = 0; c < ncpu; ++c)
                    if (order[c].dom == s->dom) CPU_SET(order[c].cpu, &w->set);
            }
            else
                CPU_SET(s->cpu, &w->set);
        }
        pthread_create(&p->th[i], &attr, worker_main, w);
    }
    pthread_attr_destroy(&attr);
    free(order);
}

static void workers_stop(struct pool *p)
{
    pthread_mutex_lock(&p->mu);
    /* async_reset: the resets in flight end first (their jobs hold their starts) */
    while (p->resets_left) pthread_cond_wait(&p->done, &p->mu);
    p->stop = 1;
    pthread_cond_broadcast(&p->work);
    pthread_mutex_unlock(&p->mu);
    for (int i = 0; i < p->nthreads; ++i) pthread_join(p->th[i], NULL);
    p->stop = 0;
    for (int i = 0; i < p->nthreads; ++i)
    {
        p->stats.instructions += p->wst[i].instructions;
        p->stats.cycles += p->wst[i].cycles;
        p->stats.ns += p->wst[i].ns;
        p->stats.steps += p->wst[i].steps;
        p->stats.ticks += p->wst[i].ticks;
    }
    free(p->wst);
    p->wst = NULL;
    for (int r = 0; r < p->nring; ++r) free(p->rings[r].v);
    free(p->rings);
    p->rings = NULL;
    p->nring = 0;
    p->fast = 0;
    free(p->th);
    p->th = NULL;
    for (int i = 0; p->wq && i < p->nq; ++i) free(p->wq[i].v);
    free(p->wq);
    free(p->wqof);
    free(p->qidle);
    free(p->qdom);
    p->wq = NULL;
    p->wqof = NULL;
    p->qidle = p->qdom = NULL;
    p->nq = 0;
}

static struct jobq *env_queue(struct pool *p, int id)
{
    if (p->wq && p->cfg.pin == 2) return &p->wq[(p->cfg.env_dom ? p->cfg.env_dom[id] : id) % p->nq];
    return p->wq ? &p->wq[p->wqof[id % p->nthreads]] : &p->q;
}

/* ------------------------------------------------------------------ API */

struct pool *pool_make(const struct pool_config *cfg, char *err, size_t n)
{
    if (cfg->n <= 0) { seterr(err, n, "no envs"); return NULL; }
    /* dead livings are freed once nothing in their env points at them
     * (grave.h): each env's sweep reads its own image, on its worker */
    grave_enable_images(GRAVE_IMAGE_LIVINGS);
    /* the rings' counters on lines of their own */
    struct pool *p = aligned_alloc(64, (sizeof *p + 63) & ~(size_t)63);
    memset(p, 0, sizeof *p);
    p->pagemap = image_pagemap_open();
    p->cfg = *cfg;
    if (p->cfg.obs.ticks <= 0) p->cfg.obs.ticks = 4;
    if (p->cfg.obs.w <= 0) p->cfg.obs.w = 128;
    if (p->cfg.obs.h <= 0) p->cfg.obs.h = 128;
    if (p->cfg.obs.rd <= 0) p->cfg.obs.rd = 4;
    p->n = cfg->n;
    p->nthreads = cfg->threads > 0 ? cfg->threads : pool_cores();
    if (sched_getaffinity(0, sizeof p->mask, &p->mask) != 0)
    {
        CPU_ZERO(&p->mask);
        for (int c = 0; c < (int)sysconf(_SC_NPROCESSORS_ONLN) && c < CPU_SETSIZE; ++c) CPU_SET(c, &p->mask);
    }
    if (cfg->config != NULL)
    {
        char e[512];
        if (wconf_load(&p->wc, cfg->config, e, sizeof e) || wconf_unimplemented(&p->wc, e, sizeof e))
        {
            seterr(err, n, "%s", e);
            free(p);
            return NULL;
        }
        p->have_conf = 1;
    }
    {
        char e[512];
        const int rung = pool_rung_conf(p);
        if (fidelity_vanilla(&p->cfg.fid) && p->have_conf && fidelity_from_conf(&p->cfg.fid, &p->wc, e, sizeof e))
        {
            seterr(err, n, "%s", e);
            free(p);
            return NULL;
        }
        /* the gates check the rows the oracle recorded: exact is the only
         * rung with them (a stripped or micro pool has no recorded rows) */
        if (p->cfg.vanilla_only && rung != NETH_RUNG_EXACT)
        {
            seterr(err, n, "fidelity.rung %s: refused (the gates check exact, the only rung the oracle's rows describe)",
                   neth_rung_name(rung));
            free(p);
            return NULL;
        }
        if (p->cfg.vanilla_only && !fidelity_vanilla(&p->cfg.fid))
        {
            fidelity_describe(&p->cfg.fid, e, sizeof e);
            seterr(err, n, "fidelity %s: refused (the gates check vanilla, the only fidelity the oracle records)", e);
            free(p);
            return NULL;
        }
        rung_note(p, rung);
        p->rung = calloc((size_t)p->n, sizeof *p->rung);
        p->rfid = calloc((size_t)p->n, sizeof *p->rfid);
        p->rfid_on = calloc((size_t)p->n, 1);
        if (p->rung == NULL || p->rfid == NULL || p->rfid_on == NULL)
        {
            seterr(err, n, "the rungs: out of memory");
            free(p);
            return NULL;
        }
        for (int i = 0; i < p->n; ++i) p->rung[i] = rung;
    }
    /* device generation serves the ticks from each env's own cache; the
     * host path shares one cache between the envs */
    if (cfg->gencache_mb >= 0 && cfg->gen == NULL)
        p->gc = gencache_new((size_t)(cfg->gencache_mb ? cfg->gencache_mb : 1024) << 20);
    p->wt = image_wtrack_open();
    p->envs = calloc((size_t)p->n, sizeof *p->envs);
    p->doneq = calloc((size_t)p->n, sizeof *p->doneq);
    p->dring = calloc((size_t)p->n, sizeof *p->dring);
    p->d_want = INT_MAX;
    for (int i = 0; i < p->n; ++i)
    {
        struct pool_env *pe = &p->envs[i];
        pe->id = i;
        pe->e = env_new();
        if (pe->e == NULL)
        {
            seterr(err, n, "env %d: no image (the address window holds %d)", i, i);
            p->n = i;
            pool_free(p);
            return NULL;
        }
        pe->acts_cap = p->cfg.obs.ticks;
        pe->acts = calloc((size_t)pe->acts_cap, sizeof *pe->acts);
        pe->res.id = i;
        if (cfg->feed) pe->feed = feed_new(&p->cfg.obs);
    }
    /* held images for resets to another start (spare_trade): with the
     * tracker, and none where an image is more than its bytes (device
     * generation's cache, a view); the address window keeps 256 slots for
     * the starts' images (two each), the saves and generation (image.h
     * IMG_SLOTS: 1,024 envs leave none) */
    const int spares = p->n / POOL_SPARE_DIV < IMG_SLOTS - p->n - 256 ? p->n / POOL_SPARE_DIV : IMG_SLOTS - p->n - 256;
    if (p->wt != NULL && cfg->gen == NULL && !cfg->view_reset && spares > 0)
    {
        p->nspare = spares;
        p->spare = calloc((size_t)p->nspare, sizeof *p->spare);
        if (p->spare == NULL) p->nspare = 0;
    }
    pthread_mutexattr_t ma;
    pthread_mutexattr_init(&ma);
    pthread_mutexattr_settype(&ma, PTHREAD_MUTEX_ADAPTIVE_NP);
    pthread_mutex_init(&p->mu, &ma);
    pthread_mutexattr_destroy(&ma);
    pthread_cond_init(&p->work, NULL);
    pthread_cond_init(&p->done, NULL);
    workers_start(p);
    if (cfg->gen != NULL)
    {
        pthread_cond_init(&p->gwork, NULL);
        pthread_mutex_init(&p->gfree_mu, NULL);
        pthread_mutex_init(&p->gkeep_mu, NULL);
        p->gq = calloc((size_t)p->n, sizeof *p->gq);
        p->kt = proc_malloc(GEN_KCAP * sizeof *p->kt);
        memset(p->kt, 0, GEN_KCAP * sizeof *p->kt);
        p->kring = proc_malloc(GEN_KEEP * sizeof *p->kring);
        int want = cfg->gen_threads > 0 ? cfg->gen_threads : 2;
        if (want > GEN_THREADS_MAX) want = GEN_THREADS_MAX;
        for (int i = 0; i < want; ++i)
        {
            const struct ga_gen *g = cfg->gen;
            if (i > 0)
            {
                if (cfg->gen->another == NULL || (p->gen_more[i] = cfg->gen->another(cfg->gen)) == NULL) break;
                g = p->gen_more[i];
            }
            struct gen_thread *gt = &p->gt[i];
            gt->p = p;
            gt->gen = g;
            gt->genv = env_new();
            if (gt->genv == NULL || pthread_create(&p->gth[i], NULL, gen_main, gt) != 0)
            {
                seterr(err, n, "the generation thread does not start");
                pool_free(p);
                return NULL;
            }
            ++p->ngth;
        }
    }
    return p;
}

void pool_free(struct pool *p)
{
    if (p == NULL) return;
    if (p->ngth > 0)
    {
        pthread_mutex_lock(&p->mu);
        p->gstop = 1;
        pthread_cond_broadcast(&p->gwork);
        pthread_mutex_unlock(&p->mu);
        for (int i = 0; i < p->ngth; ++i) pthread_join(p->gth[i], NULL);
        pthread_cond_destroy(&p->gwork);
    }
    /* the saves being made and the background loads end first (they run on the workers) */
    pthread_mutex_lock(&p->mu);
    while (p->saving > 0 || p->loading > 0) pthread_cond_wait(&p->done, &p->mu);
    pthread_mutex_unlock(&p->mu);
    if (p->th) workers_stop(p);
    for (int i = 0; i < p->n; ++i)
    {
        struct pool_env *pe = &p->envs[i];
        if (p->cfg.save_slots > 0 && pe->start != NULL) start_unref(pe->start);
        for (int k = 0; k < 3; ++k) tbase_unref(pe->tbase[k]);
        blobs_unref(pe->blobs);
        log_free(&pe->log);
        if (pe->gstate == 2) gen_drop(pe);
        if (pe->ga != NULL) genahead_release_raw(pe->ga);
        env_free(pe->e);
        free(p->envs[i].acts);
        proc_free(pe->greq);
        proc_free(pe->gres);
        proc_free(pe->tps);
        proc_free(pe->stats_v);
        proc_free(pe->stats_p);
        feed_free(p->envs[i].feed);
    }
    for (int j = 0; j < p->nspare; ++j) env_free(p->spare[j].e);
    free(p->spare);
    free(p->envs);
    free(p->rung);
    free(p->rfid);
    free(p->rfid_on);
    free(p->doneq);
    free(p->dring);
    free(p->q.v);
    image_wtrack_close(p->wt);   /* after the envs: their mappings went with them */
    if (p->pagemap >= 0) close(p->pagemap);
    gencache_free(p->gc);
    free(p->gq);
    for (int i = 0; i < GEN_THREADS_MAX; ++i)
    {
        env_free(p->gt[i].genv);
        ga_gen_free(p->gen_more[i]);
    }
    if (p->kt != NULL)
    {
        while (p->krn > 0) gen_kdrop(p);
        proc_free(p->kt);
        proc_free(p->kring);
    }
    for (int k = 0; k < GR_CLASSES; ++k)
        while (p->gfree[k] != NULL)
        {
            struct gen_res *r = p->gfree[k];
            memcpy(&p->gfree[k], r->biome, sizeof r);
            proc_free(r);
        }
    free(p);
}

void pool_gen_stats(struct pool *p, struct pool_gen_stats *s)
{
    pthread_mutex_lock(&p->mu);
    *s = p->gst;
    if (p->ngth > 0)
    {
        pthread_mutex_lock(&p->gfree_mu);
        s->res_bytes = p->res_bytes;
        s->free_bytes = p->free_bytes;
        pthread_mutex_unlock(&p->gfree_mu);
        pthread_mutex_lock(&p->gkeep_mu);
        s->kept_now = p->krn;
        pthread_mutex_unlock(&p->gkeep_mu);
    }
    for (int i = 0; i < p->n; ++i)
        if (p->envs[i].ga != NULL) gen_stats_add(s, genahead_stats(p->envs[i].ga));
    pthread_mutex_unlock(&p->mu);
}

void pool_gencache_stats(const struct pool *p, struct gencache_stats *s) { gencache_stats(p->gc, s); }

int pool_size(const struct pool *p) { return p->n; }
int pool_threads(const struct pool *p) { return p->nthreads; }

int pool_threads_set(struct pool *p, int k, char *err, size_t n)
{
    if (k < 1) { seterr(err, n, "no workers"); return -1; }
    pthread_mutex_lock(&p->mu);
    int busy = __atomic_load_n(&p->stepping, __ATOMIC_SEQ_CST) > 0 || p->sync_left > 0 || p->q.n > 0 || p->saving > 0 ||
               p->loading > 0 || p->resets_left > 0;
    pthread_mutex_unlock(&p->mu);
    if (busy) { seterr(err, n, "envs are in flight"); return -1; }
    if (k == p->nthreads) return 0;
    workers_stop(p);
    p->nthreads = k;
    workers_start(p);
    return 0;
}

int pool_cores(void)
{
    cpu_set_t mask;
    if (sched_getaffinity(0, sizeof mask, &mask) != 0) return (int)sysconf(_SC_NPROCESSORS_ONLN);
    int cores = 0;
    for (int c = 0; c < CPU_SETSIZE; ++c)
    {
        if (!CPU_ISSET(c, &mask)) continue;
        /* a core counts once: at the first of its CPUs in the mask */
        char path[96], line[256];
        snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list", c);
        FILE *f = fopen(path, "r");
        if (f == NULL) return CPU_COUNT(&mask);   /* sysfs does not say: the CPUs */
        int first = 1;
        if (fgets(line, sizeof line, f))
            for (char *q = line; *q;)
            {
                char *end;
                long a = strtol(q, &end, 10), b = a;
                if (end == q) break;
                if (*end == '-') b = strtol(end + 1, &end, 10);
                for (long k = a; k <= b && k < c; ++k)
                    if (k >= 0 && CPU_ISSET((int)k, &mask)) first = 0;
                q = *end == ',' ? end + 1 : end;
                if (*q == '\n') break;
            }
        fclose(f);
        cores += first;
    }
    return cores > 0 ? cores : 1;
}

static int start_world_check(struct pool *p, struct pool_start *s)
{
    struct env *saved = neth_env;
    neth_env = s->t;
    const struct session *ss = (const struct session *)((unsigned char *)s->t + s->t->img.dir.session);
    if (p->have_conf)
    {
        /* the config's world: the manifest's keys, the header's difficulty and rd */
        char e[400];
        int ok = wconf_match_start(&p->wc, json_get(ss->s->manifest_json, "world"), json_get(ss->hdr, "options"),
                                   !p->cfg.any_seed, e, sizeof e);
        neth_env = saved;
        if (!ok) snprintf(s->err, sizeof s->err, "%.120s: the start is not %.120s's world: %.200s", s->dir, p->cfg.config, e);
        return ok;
    }
    char line[4096];
    world_line(json_get(ss->s->manifest_json, "world"), line, sizeof line);
    neth_env = saved;
    if (!p->have_world)
    {
        snprintf(p->world, sizeof p->world, "%s", line);
        p->have_world = 1;
        return 1;
    }
    /* every key the first start's world names must hold its value */
    for (const char *k = p->world; *k;)
    {
        const char *semi = strchr(k, ';');
        if (!semi) break;
        char kv[256];
        snprintf(kv, sizeof kv, "%.*s;", (int)(semi - k), k);
        /* the features only: the pool took its first start's world, and the
         * gate mixes seeds */
        if (strncmp(kv, "seed=", 5) != 0 && strstr(line, kv) == NULL)
        {
            snprintf(s->err, sizeof s->err, "%.80s: the start's world (%.80s) is not the pool's (%.60s)", s->dir, line, kv);
            return 0;
        }
        k = semi + 1;
    }
    return 1;
}

/* a start by name (2026-10-15, GOAL.md: the checkpoint a run starts from, chosen by configuration): DIR as given when
 * it is a directory; else NAME in configs/checkpoints.tsv (the current directory's, then the tree of the program or
 * library this code is in: out/runtime/X two levels under its root, found at run time so the object names no
 * checkout); else out/java/snapshots/NAME; else as given (the load's own error names it) */
static void checkpoint_resolve(const char *name, char *out, size_t n)
{
    struct stat st;
    snprintf(out, n, "%s", name);
    if (stat(name, &st) == 0 && S_ISDIR(st.st_mode)) return;
    /* (a config file's start comes prefixed with the file's root: a missing path's last part is the name) */
    const char *base = strrchr(name, '/');
    if (base != NULL) name = base + 1;
    if (!*name) return;
    char tree[4096] = ".";
    Dl_info di;
    if (dladdr((void *)checkpoint_resolve, &di) && di.dli_fname && strchr(di.dli_fname, '/'))
    {
        char obj[4096];
        if (realpath(di.dli_fname, obj) == NULL) snprintf(obj, sizeof obj, "%s", di.dli_fname);
        for (int up = 0; up < 3; ++up)
        {
            char *sl = strrchr(obj, '/');
            if (sl == NULL) break;
            *sl = 0;
        }
        snprintf(tree, sizeof tree, "%s", obj);
    }
    const char *roots[2] = { ".", tree };
    for (int r = 0; r < 2; ++r)
    {
        char path[4200];
        snprintf(path, sizeof path, "%s/configs/checkpoints.tsv", roots[r]);
        FILE *f = fopen(path, "r");
        if (f == NULL) continue;
        char line[2048];
        while (fgets(line, sizeof line, f))
        {
            if (line[0] == '#' || line[0] == '\n') continue;
            char *tab = strchr(line, '\t');
            if (tab == NULL) continue;
            *tab = 0;
            if (strcmp(line, name)) continue;
            char *snap = tab + 1, *end = strchr(snap, '\t');
            if (end) *end = 0;
            else snap[strcspn(snap, "\n")] = 0;
            snprintf(out, n, "%s/%s", roots[r], snap);
            fclose(f);
            return;
        }
        fclose(f);
    }
    for (int r = 0; r < 2; ++r)
    {
        char path[4200];
        snprintf(path, sizeof path, "%s/out/java/snapshots/%s", roots[r], name);
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) { snprintf(out, n, "%s", path); return; }
    }
}

int pool_start_load_many(struct pool *p, const char *const *dirs, int k, struct pool_start **out, char *err, size_t n)
{
    pthread_mutex_lock(&p->mu);
    if (p->sync_left)
    {
        pthread_mutex_unlock(&p->mu);
        seterr(err, n, "a reset or load is in progress (one caller at a time)");
        return -1;
    }
    for (int i = 0; i < k; ++i)
    {
        out[i] = calloc(1, sizeof **out);
        out[i]->refs = 1;
        checkpoint_resolve(dirs[i], out[i]->dir, sizeof out[i]->dir);
        jobq_push(&p->q, (struct job){J_LOAD, -1, out[i]});
    }
    p->sync_left = k;
    pthread_cond_broadcast(&p->work);
    while (p->sync_left) pthread_cond_wait(&p->done, &p->mu);
    pthread_mutex_unlock(&p->mu);
    int bad = 0;
    for (int i = 0; i < k; ++i)
    {
        if (out[i]->ok && !start_world_check(p, out[i])) out[i]->ok = 0;
        if (!out[i]->ok)
        {
            if (!bad) seterr(err, n, "%s", out[i]->err);
            ++bad;
        }
    }
    return bad ? -1 : 0;
}

struct pool_start *pool_start_load(struct pool *p, const char *dir, char *err, size_t n)
{
    struct pool_start *s = NULL;
    if (pool_start_load_many(p, &dir, 1, &s, err, n) != 0)
    {
        if (s && s->t) env_free(s->t);
        if (s) for (int i = 0; i < 3; ++i) tbase_unref(s->tbase[i]);
        if (s) blobs_unref(s->blobs);
        free(s);
        return NULL;
    }
    return s;
}

struct pool_start *pool_start_load_bg(struct pool *p, const char *dir)
{
    struct pool_start *s = calloc(1, sizeof *s);
    s->refs = 1;
    s->making = 1;
    checkpoint_resolve(dir, s->dir, sizeof s->dir);
    pthread_mutex_lock(&p->mu);
    ++p->loading;
    jobq_push(&p->q, (struct job){J_LOADBG, -1, s});
    pthread_cond_broadcast(&p->work);
    pthread_mutex_unlock(&p->mu);
    return s;
}

int64_t pool_start_tick(const struct pool_start *s) { return s->tick; }

void pool_start_free(struct pool_start *s)
{
    if (s == NULL) return;
    start_unref(s);
}

void pool_start_info(const struct pool_start *s, struct pool_start_info *info)
{
    memset(info, 0, sizeof *info);
    info->image_pages = s->pages;
    info->image_bytes = s->pages * PAGE;
    info->pointers = s->nptrs;
    info->outside = s->outside;
    info->value_diffs = s->value_diffs;
    info->load_ms = s->load_ms;
    info->replay_steps = s->replay_steps;
    info->replay_ticks = s->replay_ticks;
    info->log_bytes = s->log_bytes;
    info->lib_pointers = s->nlibptrs;
    info->view_bytes = s->nvbytes;
    info->view_pointers = s->nvlib + s->nvimg;
    info->view_diffs = s->view_diffs;
    if (s->t != NULL) info->spill_bytes = s->t->rspill.live + s->t->rspill.live2;
}

struct pool_start *pool_start_from_env(struct pool *p, int id, char *err, size_t n)
{
    if (p->cfg.save_slots <= 0)
    {
        seterr(err, n, "saves are off (pool_config.save_slots)");
        return NULL;
    }
    if (p->cfg.view_reset && (!p->cfg.view_mem || !p->cfg.view_reload || !p->cfg.view_resume))
    {
        seterr(err, n, "the view cannot be saved (pool_config.view_mem, view_reload, view_resume)");
        return NULL;
    }
    pthread_mutex_lock(&p->mu);
    struct pool_env *pe = id >= 0 && id < p->n ? &p->envs[id] : NULL;
    const char *why = pe == NULL ? "out of range" : pe->state == ENV_QUEUED ? "stepping" : pe->start == NULL ? "never reset"
                    : pe->log.bad || pe->view_bad ? "errored since its reset" : NULL;
    int slot = 0;
    while (why == NULL && slot < p->cfg.save_slots && slot < 32 && (p->save_busy >> slot & 1)) ++slot;
    if (why == NULL && (slot >= p->cfg.save_slots || slot >= 32)) why = "waiting: every save slot is busy";
    if (why != NULL)
    {
        pthread_mutex_unlock(&p->mu);
        seterr(err, n, "env %d: %s", id, why);
        return NULL;
    }
    struct pool_start *s = calloc(1, sizeof *s);
    s->refs = 2;   /* the caller's and the making's */
    s->making = 1;
    s->slot = slot;
    s->jobs_left = 2;
    s->parent = pe->start;
    s->rung = pe->e->cfg.rung;     /* the rung its reset set (p->rung[id] may be the next reset's already) */
    start_ref(s->parent);
    /* its replays start from the parent's image, so its image points to the parent's frozen sets */
    for (int i = 0; i < 3; ++i) tbase_ref(s->tbase[i] = s->parent->tbase[i]);
    blobs_ref(s->blobs = s->parent->blobs);
    s->log = proc_calloc(1, sizeof *s->log);
    s->log->b = proc_malloc(pe->log.n ? pe->log.n : 1);
    memcpy(s->log->b, pe->log.b, pe->log.n);
    s->log->n = s->log->cap = pe->log.n;
    s->replay_steps = pe->log.steps;
    s->replay_ticks = pe->log.ticks;
    s->log_bytes = pe->log.n;
    s->tick = pe->t;
    /* the client world's Random still to set after the start's first row
     * (reset_job, step_job) only when the env has run no tick */
    s->cw_from = pe->t == pe->start_tick ? pe->cw_from : -1;
    s->cw_seed = pe->cw_seed;
    s->cw_lcg = pe->cw_lcg;
    snprintf(s->dir, sizeof s->dir, "%.4000s@%lld", s->parent->dir, (long long)pe->t);
    s->load_ms = -now_ms();
    p->save_busy |= 1u << slot;
    ++p->saving;
    jobq_push(&p->q, (struct job){J_SAVE, 0, s});
    jobq_push(&p->q, (struct job){J_SAVE, 1, s});
    pthread_cond_broadcast(&p->work);
    pthread_mutex_unlock(&p->mu);
    return s;
}

int pool_start_ready(struct pool *p, const struct pool_start *s)
{
    pthread_mutex_lock(&p->mu);
    int r = s->making ? 0 : s->ok ? 1 : -1;
    pthread_mutex_unlock(&p->mu);
    return r;
}

int pool_start_wait(struct pool *p, struct pool_start *s, char *err, size_t n)
{
    pthread_mutex_lock(&p->mu);
    while (s->making) pthread_cond_wait(&p->done, &p->mu);
    pthread_mutex_unlock(&p->mu);
    if (!s->ok) seterr(err, n, "%s: %s", s->dir, s->err);
    return s->ok ? 0 : -1;
}

#ifdef POOL_RESET_COUNT
static uint64_t sp_hit, sp_miss;
#endif
/* An env about to be reset to s whose image holds another start (a whole
 * copy) takes a held image that holds s already, in step with it since its
 * last reset (a reset by the pages written), and leaves its own image in its
 * place for an env that will want its start (lane/resetcopy: on seed starts
 * 93% of resets went to another start). Without one it takes the image held
 * longest (made here the first time) and the copy goes there. An image's
 * bytes after its reset are its start's whichever env it serves, and its
 * tracker state moves with it. Under the pool's lock, the env idle. */
static void spare_trade(struct pool *p, struct pool_env *pe, const struct pool_start *s)
{
    struct pool_img *g = NULL;
    for (int j = 0; j < p->nspare; ++j)
    {
        struct pool_img *h = &p->spare[j];
        if (h->e != NULL && h->wt_on && h->wt_serial == s->serial)
        {
            g = h;
            break;
        }
        if (g == NULL || h->stamp < g->stamp) g = h;
    }
#ifdef POOL_RESET_COUNT
    const int hit = g != NULL && g->e != NULL && g->wt_on && g->wt_serial == s->serial;
    const uint64_t c = __atomic_add_fetch(hit ? &sp_hit : &sp_miss, 1, __ATOMIC_RELAXED);
    if ((sp_hit + sp_miss) % 256 == 0)
        fprintf(stderr, "RESETSPARE %d images: %lu resets to another start took one holding it, %lu did not\n", p->nspare,
                (unsigned long)sp_hit, (unsigned long)sp_miss);
    (void)c;
#endif
    if (g == NULL) return;
    if (g->e == NULL)
    {
        g->e = env_new();
        if (g->e == NULL) return;   /* the address window is full: the env keeps its image */
        g->wt_on = g->wt_prot = 0;
        g->wt_serial = 0;
        g->hot_resets = 0;
    }
    struct pool_img mine = {pe->e, pe->wt_on, pe->wt_prot, pe->wt_serial, pe->hot_resets, ++p->spare_clock};
    pe->e = g->e;
    pe->wt_on = g->wt_on;
    pe->wt_prot = g->wt_prot;
    pe->wt_serial = g->wt_serial;
    pe->hot_resets = g->hot_resets;
    *g = mine;
}

int pool_reset(struct pool *p, const int *ids, int k, struct pool_start *s, char *err, size_t n)
{
    pthread_mutex_lock(&p->mu);
    /* a saved start being made: its making first */
#ifdef POOL_CROSS_COUNT
    if (s->making)
    {
        const uint64_t w0 = now_ns();
        while (s->making) pthread_cond_wait(&p->done, &p->mu);
        CC_ADD(cc_waits, 1);
        CC_ADD(cc_wait_ns, now_ns() - w0);
        fprintf(stderr, "CROSSWAIT a reset of %d envs waited %.1f ms on %s's load, after %lu resets\n", k, (now_ns() - w0) / 1e6,
                s->dir, (unsigned long)(cc_n[0] + cc_n[1] + cc_n[2] + cc_sampled));
    }
#endif
    while (s->making) pthread_cond_wait(&p->done, &p->mu);
    if (!s->ok)
    {
        pthread_mutex_unlock(&p->mu);
        seterr(err, n, "%s: the start failed: %s", s->dir, s->err);
        return -1;
    }
    for (int i = 0; i < k; ++i)
        if (ids[i] < 0 || ids[i] >= p->n || __atomic_load_n(&p->envs[ids[i]].state, __ATOMIC_RELAXED) == ENV_QUEUED)
        {
            pthread_mutex_unlock(&p->mu);
            seterr(err, n, "env %d is stepping or out of range", ids[i]);
            return -1;
        }
    /* async_reset: an env's reset still running ends first (two of its resets never run at once) */
    for (int i = 0; p->async_reset && i < k; ++i)
        while (p->envs[ids[i]].resetting) pthread_cond_wait(&p->done, &p->mu);
    for (int i = 0; i < k; ++i)
    {
        struct pool_env *pe = &p->envs[ids[i]];
        if (p->nspare > 0 && pe->wt_on && pe->wt_serial != s->serial && !pe->replay) spare_trade(p, pe, s);
        p->envs[ids[i]].state = ENV_IDLE;
        /* cfg.save_slots: the env will hold the start (reset_job) */
        if (p->cfg.save_slots > 0) start_ref(s);
        /* async_reset: the job holds the start until it ends (the worker lets it go) */
        if (p->async_reset) start_ref(s), p->envs[ids[i]].resetting = 1;
        jobq_push(env_queue(p, ids[i]), (struct job){J_RESET, ids[i], s});
    }
    if (p->async_reset) p->resets_left += k;
    else p->sync_left += k;
    pthread_cond_broadcast(&p->work);
    while (p->sync_left) pthread_cond_wait(&p->done, &p->mu);
    pthread_mutex_unlock(&p->mu);
    return 0;
}

void pool_async_resets(struct pool *p, int on)
{
    pthread_mutex_lock(&p->mu);
    while (!on && p->resets_left) pthread_cond_wait(&p->done, &p->mu);
    p->async_reset = on != 0;
    pthread_mutex_unlock(&p->mu);
}

int pool_step(struct pool *p, const int *ids, int k, const struct pool_act *acts, int n, int per_tick)
{
    if (n <= 0) return -1;
    pthread_mutex_lock(&p->mu);
    for (int i = 0; i < k; ++i)
        if (ids[i] < 0 || ids[i] >= p->n || __atomic_load_n(&p->envs[ids[i]].state, __ATOMIC_RELAXED) == ENV_QUEUED ||
            p->envs[ids[i]].start == NULL)
        {
            pthread_mutex_unlock(&p->mu);
            return -1;
        }
    /* the envs held (queued: no call takes them) while their acts go in outside the lock, then their jobs pushed
     * in a second short hold, and the workers woken after it: at 64 workers this loop under the lock held every
     * finished worker behind it (lane/balance: 50 us a group of 128, the workers' wait for the lock 24 us a job).
     * The result's clear is the worker's, at the job's start (worker_main). */
    for (int i = 0; i < k; ++i) p->envs[ids[i]].state = ENV_QUEUED;
    __atomic_add_fetch(&p->stepping, k, __ATOMIC_SEQ_CST);
    pthread_mutex_unlock(&p->mu);
    for (int i = 0; i < k; ++i)
    {
        struct pool_env *pe = &p->envs[ids[i]];
        int need = per_tick ? n : 1;
        if (need > pe->acts_cap)
        {
            free(pe->acts);
            pe->acts_cap = need;
            pe->acts = calloc((size_t)need, sizeof *pe->acts);
        }
        memcpy(pe->acts, per_tick ? &acts[(size_t)i * n] : &acts[i], (size_t)need * sizeof *pe->acts);
        pe->n = n;
        pe->per_tick = per_tick;
    }
    /* one publish a call, the sleepers counted in its hold woken after it (a worker counts itself a sleeper only
     * after it saw every ring empty under this lock). Published in chunks of 32 (the workers starting on one while
     * the next one's acts went in), 2 groups of 256 at 64 workers gained 3.7% but 96 workers lost 26%: each chunk
     * met the workers the last one woke on this lock; and a wake only for a call's first chunk lost a wake-up (a
     * worker woken by it, finding it taken, slept again before the next: hung at 96 workers) */
    pthread_mutex_lock(&p->mu);
    /* (an env whose async reset still runs: its step goes in when the reset ends) */
    for (int i = 0; i < k; ++i)
        if (p->envs[ids[i]].resetting) p->envs[ids[i]].step_after = 1;
        else step_push(p, ids[i]);
    step_publish(p);
    int sleepers = p->nsleep;
    pthread_mutex_unlock(&p->mu);
    if (sleepers || !p->fast) pthread_cond_broadcast(&p->work);
    return 0;
}

int pool_poll(struct pool *p, int b, const struct pool_result **out) { return pool_poll_min(p, b, b, out); }

int pool_poll_min(struct pool *p, int min, int max, const struct pool_result **out)
{
    if (min > max) min = max;
    if (p->fast)
    {
        int k = ring_ready(p, max);
        if (k >= min || min == 0)
            return k ? ring_take(p, k, out) : 0;
        ring_spin(p, min);
        k = ring_ready(p, max);
        if (k >= min || __atomic_load_n(&p->stepping, __ATOMIC_SEQ_CST) == 0)
            return k ? ring_take(p, k, out) : 0;
        pthread_mutex_lock(&p->mu);
        ring_wait(p, min);
        k = ring_take(p, ring_ready(p, max), out);
        pthread_mutex_unlock(&p->mu);
        return k;
    }
    pthread_mutex_lock(&p->mu);
    while ((int)p->ndone < min && __atomic_load_n(&p->stepping, __ATOMIC_SEQ_CST) > 0) pthread_cond_wait(&p->done, &p->mu);
    int got = 0;
    while (got < max && p->ndone > 0)
    {
        int id = p->doneq[p->done_head];
        p->done_head = (p->done_head + 1) % (size_t)p->n;
        --p->ndone;
        p->envs[id].state = ENV_IDLE;
        out[got++] = &p->envs[id].res;
    }
    pthread_mutex_unlock(&p->mu);
    return got;
}

int pool_poll_some(struct pool *p, int max, const struct pool_result **out)
{
    if (p->fast) return pool_poll_min(p, 1, max, out);
    pthread_mutex_lock(&p->mu);
    while (p->ndone == 0 && __atomic_load_n(&p->stepping, __ATOMIC_SEQ_CST) > 0) pthread_cond_wait(&p->done, &p->mu);
    int got = 0;
    while (got < max && p->ndone > 0)
    {
        int id = p->doneq[p->done_head];
        p->done_head = (p->done_head + 1) % (size_t)p->n;
        --p->ndone;
        p->envs[id].state = ENV_IDLE;
        out[got++] = &p->envs[id].res;
    }
    pthread_mutex_unlock(&p->mu);
    return got;
}

struct session *pool_session(struct pool *p, int id)
{
    return id >= 0 && id < p->n && p->envs[id].start ? p->envs[id].ss : NULL;
}

struct env *pool_env(struct pool *p, int id)
{
    return id >= 0 && id < p->n ? p->envs[id].e : NULL;
}

uint32_t pool_epoch(const struct pool *p, int id)
{
    return id >= 0 && id < p->n ? p->envs[id].epoch : 0;
}

int pool_rung(const struct pool *p, int id)
{
    if (p == NULL || id < 0 || id >= p->n) return -1;
    return p->rung != NULL ? p->rung[id] : NETH_RUNG_EXACT;
}

int pool_rung_set(struct pool *p, int id, int rung)
{
    if (p == NULL || p->rung == NULL || id < 0 || id >= p->n) return -1;
    if (rung != NETH_RUNG_EXACT && rung != NETH_RUNG_STRIPPED && rung != NETH_RUNG_MICRO) return -1;
    p->rung[id] = rung;
    rung_note(p, rung);
    return 0;
}

int pool_rung_fid(struct pool *p, int id, const struct fidelity *f)
{
    if (p == NULL || p->rfid == NULL || id < 0 || id >= p->n) return -1;
    p->rfid_on[id] = f != NULL;
    if (f != NULL) p->rfid[id] = *f;
    return 0;
}

int pool_env_mem(struct pool *p, int id, struct envmem *m)
{
    struct pool_env *pe = &p->envs[id];
    if (pe->start == NULL) return 0;
    struct env *saved = neth_env;
    neth_env = pe->e;
    envmem_session(pe->ss, m);
    neth_env = saved;
    return 1;
}

size_t pool_env_resident(struct pool *p, int id)
{
    /* the process's heap, not a thread-local: 17 KB of TLS would not fit
     * the static TLS a loaded library's (rlbind.mk) may take */
    struct envmem *m = proc_calloc(1, sizeof *m);
    if (m == NULL) return 0;
    size_t b = pool_env_mem(p, id, m) ? envmem_total(m) : 0;
    proc_free(m);
    return b;
}

void pool_profile_summary(struct pool *p, int id, FILE *to, FILE *rows)
{
    struct pool_env *pe = &p->envs[id];
    if (!pe->profiling) return;
    struct env *saved = neth_env;
    neth_env = pe->e;
    phase_profile_summary(to, rows);
    neth_env = saved;
}

size_t pool_profile_totals(struct pool *p, int id, uint64_t *rows_ins, uint64_t *sub_ins)
{
    struct pool_env *pe = &p->envs[id];
    if (!pe->profiling) return 0;
    struct env *saved = neth_env;
    neth_env = pe->e;
    size_t n = phase_profile_totals(rows_ins, sub_ins);
    neth_env = saved;
    return n;
}

void pool_stats(struct pool *p, struct pool_stats *st)
{
    pthread_mutex_lock(&p->mu);
    *st = p->stats;
    for (int i = 0; p->wst && i < p->nthreads; ++i)
    {
        const struct pool_wstat *w = &p->wst[i];
        st->instructions += __atomic_load_n(&w->instructions, __ATOMIC_RELAXED);
        st->cycles += __atomic_load_n(&w->cycles, __ATOMIC_RELAXED);
        st->ns += __atomic_load_n(&w->ns, __ATOMIC_RELAXED);
        st->steps += __atomic_load_n(&w->steps, __ATOMIC_RELAXED);
        st->ticks += __atomic_load_n(&w->ticks, __ATOMIC_RELAXED);
    }
    pthread_mutex_unlock(&p->mu);
}
