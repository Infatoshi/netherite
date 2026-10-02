/* The env pool (pool.h). */
#define _GNU_SOURCE
#include "pool.h"

#include <errno.h>
#include <linux/perf_event.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include "../engine/arena.h"
#include "../engine/container.h"
#include "../engine/cwrand.h"
#include "../engine/env.h"
#include "../engine/envmem.h"
#include "../engine/gencache.h"
#include "../engine/genahead.h"
#include "../engine/grave.h"
#include "../engine/image.h"
#include "../engine/nbtjson.h"
#include "../engine/phase.h"
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

enum job_kind { J_STEP, J_RESET, J_LOAD, J_GENSTEP, J_SAVE };
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
    int stepping;
    int sync_left;                  /* the pool_reset or load call's jobs still running */
    int saving;                     /* saves being made (cfg.save_slots) */
    unsigned save_busy;             /* their slots */
    /* the config every start must match (cfg.config), or the first
     * start's world keys (without one) */
    struct wconf wc;
    int have_conf;
    char world[4096];
    int have_world;
    struct pool_stats stats;
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

/* One load of dir into a new image environment. */
static struct env *start_image(struct pool *p, struct pool_start *st, const char *dir, char *err, size_t n)
{
    struct env *e = env_new();
    if (e == NULL) { seterr(err, n, "no image environment (the address window is full)"); return NULL; }
    struct env *saved = nw_env;
    nw_env = e;
    if (p->cfg.region_spill != NULL && !rspill_flag(p->cfg.region_spill))
    {
        seterr(err, n, "region_spill takes on, off or an absolute directory, not %s", p->cfg.region_spill);
        nw_env = saved;
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
    struct session *ss = calloc(1, sizeof *ss);
    if (hdr == NULL) { seterr(err, n, "%s: no tape header", path); goto out; }
    if (!snapshot_load(snap, dir)) { seterr(err, n, "%s: the snapshot does not load", dir); goto out; }
    if (session_refuse_dev(hdr, snap->manifest_json, p->cfg.dev_apply != NULL))
    {
        seterr(err, n, "%s: a dev start, refused (the product runs no dev ops)", dir);
        goto out;
    }
    if (!session_open_at(ss, snap, dir, dir, hdr)) { seterr(err, n, "%s: %s", dir, ss->err); goto out; }
    ss->dev_apply = p->cfg.dev_apply;
    ss->dev_end = p->cfg.dev_end;
    ss->start_difficulty = p->cfg.difficulty;
    start_boundary(ss);
    /* what only the open reads goes: no dev op runs on a start that is not
     * a dev one (session_refuse_dev), so nothing binds the session again */
    if (!session_refuse_dev(hdr, snap->manifest_json, 0)) snapshot_drop_runtime(snap);
    /* and the pages of every block the load freed: no env copies them */
    image_heap_trim(e);
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
    nw_env = saved;
    if (ret == NULL) env_free(e);
    return ret;
}

/* The image's pages and pointers against its twin (the same load at
 * another address): a word that points into the image and differs from the
 * twin's is a pointer; any other difference is counted. */
static int start_scan(struct pool_start *s, char *err, size_t n)
{
    struct img_extent ext[64], ext2[64];
    int next = image_extents(s->t, ext, 64), next2 = image_extents(s->twin, ext2, 64);
    if (next != next2) { seterr(err, n, "the twin's extents differ (%d, %d)", next, next2); return 0; }
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
    for (int i = 0; i < next; ++i)
    {
        size_t len = ext[i].len > ext2[i].len ? ext[i].len : ext2[i].len;
        if (ext[i].off != ext2[i].off)
        {
            seterr(err, n, "the twin's extent %d is at another offset", i);
            return 0;
        }
        uint64_t p0 = ext[i].off & ~(uint64_t)(PAGE - 1), p1 = (ext[i].off + len + PAGE - 1) & ~(uint64_t)(PAGE - 1);
        if (last_end != UINT64_MAX && p0 < last_end) p0 = last_end;
        for (uint64_t pg = p0; pg < p1; pg += PAGE)
        {
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
    return 1;
}

static void start_load_job(struct pool *p, struct pool_start *s)
{
    double t0 = now_ms();
    s->t = start_image(p, s, s->dir, s->err, sizeof s->err);
    if (s->t) s->twin = start_image(p, NULL, s->dir, s->err, sizeof s->err);
    if (s->t && s->twin && start_scan(s, s->err, sizeof s->err)) s->ok = 1;
    /* the twin has named the pointers: it is not needed again */
    if (s->twin) { env_free(s->twin); s->twin = NULL; }
    s->load_ms = now_ms() - t0;
}

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
    free(s);
}

/* one holder of s lets go (cfg.save_slots; any thread) */
static void start_unref(struct pool_start *s)
{
    if (__atomic_sub_fetch(&s->refs, 1, __ATOMIC_ACQ_REL) == 0) start_destroy(s);
}

static void start_ref(struct pool_start *s) { __atomic_add_fetch(&s->refs, 1, __ATOMIC_ACQ_REL); }

static void reset_job(struct pool *p, struct pool_env *pe, struct pool_start *s)
{
    struct env *E = pe->e, *T = s->t;
    /* the cache lives in the image the copy replaces */
    if (p->cfg.gen != NULL) gen_reset(p, pe);
    /* so does the region stores' spill file: E's closed, and after the copy
     * the start's records go to a file of E's own */
    rspill_close(&E->rspill);
    unsigned char *eb = (unsigned char *)E;
    const unsigned char *tb = (const unsigned char *)T;
    /* E as a fresh mapping holding T's pages: every page E wrote that T's
     * runs do not cover goes back (reads zeros), the rest is overwritten in
     * place (a reused env's pages stay resident: no fault per page) */
    size_t used = (size_t)E->img.heap_off + E->img.heap_used;
    used = (used + PAGE - 1) & ~(size_t)(PAGE - 1);
    size_t at = 0;
    for (size_t i = 0; i < s->nruns; ++i)
    {
        size_t lo = s->runs[i].off, hi = lo + s->runs[i].len;
        if (lo > at && at < used) madvise(eb + at, (lo < used ? lo : used) - at, MADV_DONTNEED);
        memcpy(eb + lo, tb + lo, hi - lo);
        at = hi;
    }
    if (at < used) madvise(eb + at, used - at, MADV_DONTNEED);
    const int64_t delta = (int64_t)((uintptr_t)eb - (uintptr_t)tb);
    for (size_t i = 0; i < s->nptrs; ++i) *(int64_t *)(eb + s->ptrs[i]) += delta;
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
    pe->ss->hooks.ctx = pe;
    struct env *was = nw_env;
    nw_env = E;
    int heap = image_heap_set(E, 1);
    rspill_adopt();
    image_heap_set(E, heap);
    nw_env = was;
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
                if (ok && logging) log_agent(pe, &c, 0);
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
        if (pe->cw_from == pe->t + 1 && pe->t == pe->start_tick)
            client_player_set_world_rand(cp, pe->cw_seed, pe->cw_lcg);
        if (p->cfg.view_tick) p->cfg.view_tick(p->cfg.view_ctx, pe->id, pe->t, a, 1);
        if (p->cfg.tick_hook && !pe->replay) p->cfg.tick_hook(p->cfg.tick_hook_ctx, pe->id, ss, pe->t);
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
    nw_env = gt->genv;
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

/* Replay WHICH of save s: a new image reset to the parent start, with
 * the spare view n + 2 * slot + which, and the log's steps run. */
static void save_job(struct pool *p, struct pool_start *s, int which)
{
    char *err = s->rerr[which];
    const size_t en = sizeof s->rerr[which];
    struct pool_env *pe = proc_calloc(1, sizeof *pe);
    if (pe == NULL) abort();
    pe->id = p->n + 2 * s->slot + which;
    pe->replay = 1;
    pe->res.id = pe->id;
    pe->e = env_new();
    if (pe->e == NULL)
    {
        seterr(err, en, "no image for the save's replay (the address window is full)");
        proc_free(pe);
        return;
    }
    struct env *saved = nw_env;
    nw_env = pe->e;
    reset_job(p, pe, s->parent);
    int heap = image_heap_set(pe->e, 1);
    int bad = 0;
    if (s->parent->vm.nseg > 0) bad = view_restore(p, pe, s->parent, err, en) != 0;
    else if (p->cfg.view_reset) bad = p->cfg.view_reset(p->cfg.view_ctx, pe->id, pe->ss, err, en) != 0;
    const unsigned char *q = s->log->b, *end = s->log->b + s->log->n;
    int64_t steps = 0;
    while (!bad && q < end)
    {
        const uint32_t *h = (const uint32_t *)q;
        if (h[0] != LOG_STEP)
        {
            seterr(err, en, "the act log is damaged at byte %zu", (size_t)(q - s->log->b));
            bad = 1;
            break;
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
            seterr(err, en, "the replay's step %lld ran %d of %u ticks: %.300s", (long long)steps, pe->res.ticks, h[1],
                   pe->res.err);
            bad = 1;
            break;
        }
        ++steps;
        q = e1;
    }
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
    image_heap_set(pe->e, heap);
    nw_env = saved;
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
        ok = start_scan(s, s->err, sizeof s->err);
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

static void *worker_main(void *arg)
{
    struct worker *w = arg;
    struct pool *p = w->p;
    if (w->placed) pthread_setaffinity_np(pthread_self(), sizeof w->set, &w->set);
    /* one scratch for every env this worker steps (env.h: nw_scratch),
     * its pages first written here, near the worker */
    nw_thread_scratch = env_scratch_new();
    int fd = p->cfg.no_counters ? -1 : pmu_open();
    pthread_mutex_lock(&p->mu);
    for (;;)
    {
        int qi = p->wq ? p->wqof[w->index] : -1;
        struct jobq *mine = p->wq ? &p->wq[qi] : NULL, *take = NULL;
        while (!p->stop)
        {
            if (mine && mine->n) take = mine;
            else if (p->q.n) take = &p->q;
            else if (mine && p->cfg.steal) take = steal_queue(p, qi);
            if (take) break;
            if (mine) ++p->qidle[qi];
            pthread_cond_wait(&p->work, &p->mu);
            if (mine) --p->qidle[qi];
        }
        if (p->stop) break;
        struct job j = jobq_pop(take);
        pthread_mutex_unlock(&p->mu);

        struct pmu c0 = pmu_read(fd);
        uint64_t t0 = now_ns();
        struct pool_env *pe = j.kind == J_LOAD || j.kind == J_SAVE ? NULL : &p->envs[j.id];
        int gact = GA_NONE;   /* cfg.gen: the job's generation act (gen_act) */
        if (p->cfg.range)
            p->cfg.range(j.kind == J_RESET ? "pool reset" : j.kind == J_LOAD ? "pool load" : j.kind == J_SAVE ? "pool save" : "pool step", 1);
        if (j.kind == J_LOAD) start_load_job(p, j.start);
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
            struct env *saved = nw_env;
            nw_env = pe->e;
            if (j.kind == J_RESET) reset_job(p, pe, j.start);
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
                if (p->cfg.gen != NULL) gact = gen_step_begin(p, pe);
                if (gact == GA_NONE)
                {
                    step_job(p, pe);
                    if (p->cfg.gen != NULL) gact = gen_step_end(p, pe);
                }
            }
            image_heap_set(pe->e, heap);
            nw_env = saved;
        }
        if (p->cfg.range) p->cfg.range(NULL, 0);
        struct pmu c1 = pmu_read(fd);
        uint64_t di, dc, dt = now_ns() - t0;
        pmu_delta(c0, c1, &di, &dc);

        pthread_mutex_lock(&p->mu);
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
            --p->stepping;
            pthread_cond_broadcast(&p->done);
        }
        else if (j.kind != J_SAVE)
        {
            if (j.kind == J_RESET)
            {
                p->stats.reset_ns += dt;
                p->stats.reset_instructions += di;
                ++p->stats.resets;
            }
            if (--p->sync_left == 0) pthread_cond_broadcast(&p->done);
        }
    }
    pthread_mutex_unlock(&p->mu);
    if (fd >= 0) close(fd);
    env_scratch_free(nw_thread_scratch);
    nw_thread_scratch = NULL;
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

/* The CPUs of the mask in cfg.place's order (PP_DOMAIN and pin 2 without a
 * place: PP_CORES's), with each CPU's core (its first sibling), cache domain
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
    if (place == PP_SPREAD || place == PP_DOMS) qsort(v, (size_t)k, sizeof *v, slot_cmp_spread);
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
    p->stop = 1;
    pthread_cond_broadcast(&p->work);
    pthread_mutex_unlock(&p->mu);
    for (int i = 0; i < p->nthreads; ++i) pthread_join(p->th[i], NULL);
    p->stop = 0;
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
    if (p->wq && p->cfg.pin == 2 && p->cfg.env_dom) return &p->wq[p->cfg.env_dom[id] % p->nq];
    return p->wq ? &p->wq[p->wqof[id % p->nthreads]] : &p->q;
}

/* ------------------------------------------------------------------ API */

struct pool *pool_make(const struct pool_config *cfg, char *err, size_t n)
{
    if (cfg->n <= 0) { seterr(err, n, "no envs"); return NULL; }
    /* dead livings are freed once nothing in their env points at them
     * (grave.h): each env's sweep reads its own image, on its worker */
    grave_enable_images(GRAVE_IMAGE_LIVINGS);
    struct pool *p = calloc(1, sizeof *p);
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
    /* device generation serves the ticks from each env's own cache; the
     * host path shares one cache between the envs */
    if (cfg->gencache_mb >= 0 && cfg->gen == NULL)
        p->gc = gencache_new((size_t)(cfg->gencache_mb ? cfg->gencache_mb : 1024) << 20);
    p->envs = calloc((size_t)p->n, sizeof *p->envs);
    p->doneq = calloc((size_t)p->n, sizeof *p->doneq);
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
    pthread_mutex_init(&p->mu, NULL);
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
    /* the saves being made end first (their replays run on the workers) */
    pthread_mutex_lock(&p->mu);
    while (p->saving > 0) pthread_cond_wait(&p->done, &p->mu);
    pthread_mutex_unlock(&p->mu);
    if (p->th) workers_stop(p);
    for (int i = 0; i < p->n; ++i)
    {
        struct pool_env *pe = &p->envs[i];
        if (p->cfg.save_slots > 0 && pe->start != NULL) start_unref(pe->start);
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
    free(p->envs);
    free(p->doneq);
    free(p->q.v);
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
    int busy = p->stepping > 0 || p->sync_left > 0 || p->q.n > 0 || p->saving > 0;
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
    struct env *saved = nw_env;
    nw_env = s->t;
    const struct session *ss = (const struct session *)((unsigned char *)s->t + s->t->img.dir.session);
    if (p->have_conf)
    {
        /* the config's world: the manifest's keys, the header's difficulty and rd */
        char e[400];
        int ok = wconf_match_start(&p->wc, json_get(ss->s->manifest_json, "world"), json_get(ss->hdr, "options"), 1, e, sizeof e);
        nw_env = saved;
        if (!ok) snprintf(s->err, sizeof s->err, "%.120s: the start is not %.120s's world: %.200s", s->dir, p->cfg.config, e);
        return ok;
    }
    char line[4096];
    world_line(json_get(ss->s->manifest_json, "world"), line, sizeof line);
    nw_env = saved;
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
        snprintf(out[i]->dir, sizeof out[i]->dir, "%s", dirs[i]);
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
        free(s);
        return NULL;
    }
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
    start_ref(s->parent);
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

int pool_reset(struct pool *p, const int *ids, int k, struct pool_start *s, char *err, size_t n)
{
    pthread_mutex_lock(&p->mu);
    /* a saved start being made: its making first */
    while (s->making) pthread_cond_wait(&p->done, &p->mu);
    if (!s->ok)
    {
        pthread_mutex_unlock(&p->mu);
        seterr(err, n, "%s: the start failed: %s", s->dir, s->err);
        return -1;
    }
    for (int i = 0; i < k; ++i)
        if (ids[i] < 0 || ids[i] >= p->n || p->envs[ids[i]].state == ENV_QUEUED)
        {
            pthread_mutex_unlock(&p->mu);
            seterr(err, n, "env %d is stepping or out of range", ids[i]);
            return -1;
        }
    for (int i = 0; i < k; ++i)
    {
        p->envs[ids[i]].state = ENV_IDLE;
        /* cfg.save_slots: the env will hold the start (reset_job) */
        if (p->cfg.save_slots > 0) start_ref(s);
        jobq_push(env_queue(p, ids[i]), (struct job){J_RESET, ids[i], s});
    }
    p->sync_left += k;
    pthread_cond_broadcast(&p->work);
    while (p->sync_left) pthread_cond_wait(&p->done, &p->mu);
    pthread_mutex_unlock(&p->mu);
    return 0;
}

int pool_step(struct pool *p, const int *ids, int k, const struct pool_act *acts, int n, int per_tick)
{
    if (n <= 0) return -1;
    pthread_mutex_lock(&p->mu);
    for (int i = 0; i < k; ++i)
        if (ids[i] < 0 || ids[i] >= p->n || p->envs[ids[i]].state == ENV_QUEUED || p->envs[ids[i]].start == NULL)
        {
            pthread_mutex_unlock(&p->mu);
            return -1;
        }
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
        pe->state = ENV_QUEUED;
        memset(&pe->res, 0, offsetof(struct pool_result, hud));
        pe->res.id = ids[i];
        jobq_push(env_queue(p, ids[i]), (struct job){J_STEP, ids[i], NULL});
        ++p->stepping;
    }
    pthread_cond_broadcast(&p->work);
    pthread_mutex_unlock(&p->mu);
    return 0;
}

int pool_poll(struct pool *p, int b, const struct pool_result **out)
{
    pthread_mutex_lock(&p->mu);
    while ((int)p->ndone < b && p->stepping > 0) pthread_cond_wait(&p->done, &p->mu);
    int got = 0;
    while (got < b && p->ndone > 0)
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
    pthread_mutex_lock(&p->mu);
    while (p->ndone == 0 && p->stepping > 0) pthread_cond_wait(&p->done, &p->mu);
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

int pool_env_mem(struct pool *p, int id, struct envmem *m)
{
    struct pool_env *pe = &p->envs[id];
    if (pe->start == NULL) return 0;
    struct env *saved = nw_env;
    nw_env = pe->e;
    envmem_session(pe->ss, m);
    nw_env = saved;
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
    struct env *saved = nw_env;
    nw_env = pe->e;
    phase_profile_summary(to, rows);
    nw_env = saved;
}

size_t pool_profile_totals(struct pool *p, int id, uint64_t *rows_ins, uint64_t *sub_ins)
{
    struct pool_env *pe = &p->envs[id];
    if (!pe->profiling) return 0;
    struct env *saved = nw_env;
    nw_env = pe->e;
    size_t n = phase_profile_totals(rows_ins, sub_ins);
    nw_env = saved;
    return n;
}

void pool_stats(struct pool *p, struct pool_stats *st)
{
    pthread_mutex_lock(&p->mu);
    *st = p->stats;
    pthread_mutex_unlock(&p->mu);
}
