/* The env pool's gate: K recordings (tapes with their start snapshots)
 * stepped in one pool of N envs, recordings repeated and interleaved, each
 * env's every row held to test_snapshots' own record of that row
 * (csrc/tests/rowrec.h: every field test_snapshots compares; made by
 * test_snapshots --row-dump into REFDIR/NAME.rows), and after each tape's
 * last row the end state.
 *
 *   pool_gate [-n N] [-j THREADS] [--ticks n] [--episodes E] [--poll B]
 *             [--snapdir DIR] [--refdir DIR] [--agent SCRIPTS] [--feed]
 *             [--sparse K] [--world|--config CONF] [--gen c|cuda]
 *             [--fast] [--mask FIELD,...] [--raycast-rows DIR] NAME...
 *
 * --agent SCRIPTS: the steps are each recording's own script's agent acts
 * (SCRIPTS/NAME.jsonl after its Snapshot command: Act.java's agent form,
 * resolved in the tick), not the tape's rows; the rows must still be
 * test_snapshots'. --feed: the render feed on, and each env's client world
 * rebuilt from its feeds and checked against the world after every step,
 * with the feed's sizes. --sparse K: a record every K rows only, so most
 * ticks run with no record's reads between them, as the product runs them.
 * --world CONF (or --config CONF): the pool made from a config.yaml (every
 * start must be its world: the seed, the switches, the game rules, the
 * difficulty and the render distance; engine/worldconf.h wconf_match_start). --gen: raw generation through the pool's
 * batched generator (pool.h cfg.gen) instead of the tick's C provider: c the
 * C engine's (genahead.h ga_gen_c_create, the same seam without a device),
 * cuda the device's (out/runtime/pool_gate_cuda only, built with the CUDA
 * generators: ga_gen_cuda_local_create). --fast: engine.tick_scope fast (the
 * client's draw-side calls skipped, pool.h tick_fast). --mask FIELD,...: those
 * row fields compared as equal whatever they hold (fast's declared three:
 * d.cseed,d.cw,d.cmath; pipe_gate's --mask). --raycast-rows DIR: each
 * recording's first episode writes DIR/NAME.rc, one line a row, "t digest" of
 * that row's fast cast at the episode's spec, so two runs (full and fast) name
 * the first row whose cast differs (raycast-fast-gate).
 *
 * Env i starts on recording i mod K; each env that finishes an episode is
 * reset (an image copy) to the next recording in turn until E episodes ran
 * (default max(2N, K)): so every recording runs, most envs run several, and
 * the tapes interleave on the workers in whatever order they finish. Steps
 * are n ticks of the tape's rows (the last one shorter); poll hands back
 * B envs at a time (default N/4), the rest keep running.
 *
 * Prints one line per recording (episodes, rows, the first differing row
 * and field) and a summary; exit 0 when every row of every episode equals
 * the reference. */
#define _GNU_SOURCE
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../engine/cwrand.h"
#include "../engine/dev.h"
#include "../engine/env.h"
#include "../engine/gencache.h"
#include "../engine/genahead.h"
#include "../engine/session.h"
#include "../tests/rowrec.h"
#include "feed.h"
#include "pool.h"
#include "semcam.h"
#include "raycast.h"
#include "rchead.h"
#ifdef POOL_GATE_RCD
#include "../cuda/raycast/rcdev.h"
#include "rcmirror.h"
#endif
#include "../engine/raster.h"

struct rec {
    const char *name;
    char dir[4096];
    /* the tape's rows by t (the header aside) */
    char **rows;
    int64_t nrows;
    /* the reference records by t, and the end record */
    char **ref;
    int64_t nref;
    char *ref_end;
    int64_t tick, cw_from;
    struct pool_start *start;
    int left;               /* its episodes not yet reset to */
    int episodes, bad;
    int64_t rows_checked;
    char first_bad[1024];
    /* --agent: the script's steps after its Snapshot (agent form) */
    struct pool_act *steps;
    int *step_n, nsteps;
    /* --semcam: the first episode's digest of every cast; later episodes must give it again */
    uint64_t sem_digest;
    int sem_have, sem_nondet, sem_bad;
    int64_t sem_casts, sem_hits, sem_rays;
    uint64_t sem_ref_ns, sem_fast_ns;
    char sem_first[300];
    /* --raycast: the same for the textured ray casts (every spec's, one digest) */
    uint64_t rc_digest[4];      /* by spec with --raycast-each, else [0] */
    int rc_have[4], rc_nondet, rc_bad;
    int64_t rc_casts;
    uint64_t rc_ref_ns, rc_fast_ns;
    char rc_first[300];
    /* --raycast-device: the device's casts, each against the reference's frame and the fast cast's seen record */
    int64_t rc_dev_casts;
    uint64_t rc_dev_ns, rc_dev_recs;
    uint64_t cmp[24];           /* --cast-cmp's counts (CMP_*), every episode's */
};

struct genv {
    struct rec *r;
    int episode, step;
    int bad;
    int64_t checked;
    char why[1024];
    /* --semcam: the casts after each tick, the fast against the reference */
    uint64_t sem_digest, sem_ref_ns, sem_fast_ns;
    int64_t sem_casts, sem_hits, sem_rays;
    int sem_bad;
    char sem_why[256];
    /* --raycast: the casts after each tick, the fast against the reference */
    uint64_t rc_digest, rc_ref_ns, rc_fast_ns;
    int rc_spec;                /* --raycast-each: the spec this episode checks, -1 every one */
    int64_t rc_casts;
    int rc_bad;
    char rc_why[256];
    int64_t rc_dev_casts;
    uint64_t rc_dev_ns;
    uint64_t *rc_rows;          /* --raycast-rows: each row's cast digest, by t (the process's heap) */
    uint64_t cmp[24];           /* --cast-cmp: the FP64 and FP32 casts' differences (CMP_*) */
};

/* --cast-cmp (lane/castf32): with --semcam and --raycast, each cast also made by both instances (raycast.h "The two
 * instances": FP64 the reference, FP32 production, both the fast path), what differs counted, not failed: the
 * semantic camera's rays (by kind: the distance alone, one 1/256 apart or more; the block or face, and of those the
 * ones grazing their face (|d . n| < 0.1), on a face's edge (the reference's hit within 1/128 of a block of one),
 * far (past 32 blocks), or a hit against a miss), and the textured cast's pixels and bytes */
enum {
    CMP_S_RAYS, CMP_S_DIFF, CMP_S_DIST1, CMP_S_DISTN, CMP_S_TOPO, CMP_S_GRAZE, CMP_S_EDGE, CMP_S_FAR, CMP_S_MISSHIT,
    CMP_S_NEARTIE, CMP_P_PIX, CMP_P_DIFF, CMP_P_BYTES, CMP_P_SUMD, CMP_P_MAXD,
    /* the textured cast's rays (raycast_rays, lane/cxfix): all, differing, by the distance alone (one 1/256 or more),
     * in the cell, face or entity, and of those a block, the sky or an entity against another kind */
    CMP_R_RAYS, CMP_R_DIFF, CMP_R_DIST1, CMP_R_DISTN, CMP_R_TOPO, CMP_R_KIND,
    /* the casts compared, and the ones with a ray differing in cell, face or entity */
    CMP_R_CASTS, CMP_R_TFRAMES, CMP_N
};
_Static_assert(CMP_N <= 24, "struct genv's and struct grec's cmp hold every count");
static int cast_cmp;
/* --cast-cmp-with f32|fx|f16 (lane/castexact): the textured cast's instance compared against FP64 (default FP32) */
static int cmp_fp = 32;
/* a spec's instance from its suffix (:f32, :fx, :f16; none FP64), and back */
static int spec_fp(const char *f)
{
    return strstr(f, "f32") ? 32 : strstr(f, "f16") ? 16 : strstr(f, "fx") ? RAYCAST_FP_FIXED : 64;
}
static const char *fp_tag(int fp)
{
    return fp == 32 ? ":f32" : fp == 16 ? ":f16" : fp == RAYCAST_FP_FIXED ? ":fx" : "";
}

/* --feed: each env's client world as its feeds rebuild it (chunk -> the
 * section hashes), checked after every step against the world itself */
struct mchunk {
    int32_t cx, cz;
    int used;
    uint64_t sec[16];
};
struct mirror {
    struct mchunk *tab;
    size_t cap;
};

static struct mchunk *mirror_get(struct mirror *m, int cx, int cz, int make)
{
    if (m->cap == 0)
    {
        m->cap = 4096;
        m->tab = calloc(m->cap, sizeof *m->tab);
    }
    size_t i = ((uint32_t)cx * 0x9e3779b1u ^ (uint32_t)cz * 0x85ebca6bu) & (m->cap - 1);
    for (;; i = (i + 1) & (m->cap - 1))
    {
        struct mchunk *e = &m->tab[i];
        if (!e->used)
        {
            if (!make) return NULL;
            memset(e, 0, sizeof *e);
            e->used = 1;
            e->cx = cx;
            e->cz = cz;
            return e;
        }
        if (e->cx == cx && e->cz == cz) return e;
    }
}

static void mirror_drop(struct mirror *m, int cx, int cz)
{
    struct mchunk *e = mirror_get(m, cx, cz, 0);
    if (e == NULL) return;
    /* rebuild without it (open addressing) */
    e->used = 0;
    struct mchunk *old = m->tab;
    m->tab = calloc(m->cap, sizeof *m->tab);
    for (size_t i = 0; i < m->cap; ++i)
        if (old[i].used) *mirror_get(m, old[i].cx, old[i].cz, 1) = old[i];
    free(old);
}

struct feedstat {
    uint64_t n, bytes, max, bursts, burst_bytes, burst_max;
    uint64_t *sizes;
    size_t nsizes, capsizes;
};
static struct feedstat fstat;

/* Apply a feed to the env's mirror, then compare the mirror with the client
 * world in range; 0 with why on a difference. */
static int feed_check(struct mirror *m, const struct pool_result *r, struct session *ss, int rd, char *why, size_t n)
{
    const struct pool_feed *h = r->feed;
    if (h == NULL) { snprintf(why, n, "no feed"); return 0; }
    if (h->bytes != r->feed_bytes) { snprintf(why, n, "feed bytes %u, the result says %zu", h->bytes, r->feed_bytes); return 0; }
    const unsigned char *b = r->feed;
    if (h->reset && m->tab) memset(m->tab, 0, m->cap * sizeof *m->tab);
    const int32_t *un = (const int32_t *)(b + h->off_unloads);
    for (uint32_t i = 0; i < h->nunloads; ++i) mirror_drop(m, un[2 * i], un[2 * i + 1]);
    const struct pool_feed_chunk *ch = (const struct pool_feed_chunk *)(b + h->off_loads);
    const struct pool_feed_section *ls = (const struct pool_feed_section *)(b + h->off_load_sections);
    for (uint32_t i = 0; i < h->nloads; ++i)
    {
        struct mchunk *e = mirror_get(m, ch[i].cx, ch[i].cz, 1);
        if (ch[i].count > 0) memset(e->sec, 0, sizeof e->sec);
        for (int k = 0; k < ch[i].count; ++k)
        {
            const struct pool_feed_section *sec = &ls[ch[i].first + k];
            if (sec->cx != ch[i].cx || sec->cz != ch[i].cz || sec->sy < 0 || sec->sy > 15)
            {
                snprintf(why, n, "load section %d of chunk (%d,%d) names (%d,%d,%d)", k, ch[i].cx, ch[i].cz, sec->cx, sec->sy, sec->cz);
                return 0;
            }
            e->sec[sec->sy] = feed_section_hash(sec);
        }
    }
    const struct pool_feed_section *cs = (const struct pool_feed_section *)(b + h->off_sections);
    for (uint32_t i = 0; i < h->nsections; ++i)
    {
        struct mchunk *e = mirror_get(m, cs[i].cx, cs[i].cz, 0);
        if (e == NULL || cs[i].sy < 0 || cs[i].sy > 15)
        {
            snprintf(why, n, "section (%d,%d,%d) of a chunk the feed never loaded", cs[i].cx, cs[i].sy, cs[i].cz);
            return 0;
        }
        e->sec[cs[i].sy] = feed_section_hash(&cs[i]);
    }
    /* the world itself, in range */
    struct client_player *cp = &ss->cp;
    struct world *cw = ss->client_world;
    int r1 = rd + 1, pcx = (int)floor(cp->e.pos_x / 16.0), pcz = (int)floor(cp->e.pos_z / 16.0);
    size_t inrange = 0;
    for (int cx = pcx - r1; cx <= pcx + r1 && cw; ++cx)
        for (int cz = pcz - r1; cz <= pcz + r1; ++cz)
        {
            const struct chunk *c = world_chunk(cw, cx, cz);
            if (c == NULL) continue;
            ++inrange;
            struct mchunk *e = mirror_get(m, cx, cz, 0);
            if (e == NULL) { snprintf(why, n, "chunk (%d,%d) in range, not in the feeds", cx, cz); return 0; }
            for (int sy = 0; sy < 16; ++sy)
            {
                struct pool_feed_section tmp;
                uint64_t want = 0;
                if (chunk_sec_at(c, sy) != NULL)
                {
                    feed_section_fill(&tmp, c, sy);
                    want = feed_section_hash(&tmp);
                }
                if (want != e->sec[sy])
                {
                    snprintf(why, n, "section (%d,%d,%d): the feeds have %016llx, the world %016llx", cx, sy, cz,
                             (unsigned long long)e->sec[sy], (unsigned long long)want);
                    return 0;
                }
            }
        }
    size_t held = 0;
    for (size_t i = 0; i < m->cap; ++i) held += m->tab[i].used;
    if (held != inrange) { snprintf(why, n, "the feeds hold %zu chunks, the range %zu", held, inrange); return 0; }
    /* sizes */
    ++fstat.n;
    fstat.bytes += r->feed_bytes;
    if (r->feed_bytes > fstat.max) fstat.max = r->feed_bytes;
    if (h->nloads && h->nload_sections)
    {
        ++fstat.bursts;
        fstat.burst_bytes += r->feed_bytes;
        if (r->feed_bytes > fstat.burst_max) fstat.burst_max = r->feed_bytes;
    }
    if (fstat.nsizes == fstat.capsizes) fstat.sizes = realloc(fstat.sizes, (fstat.capsizes = fstat.capsizes ? 2 * fstat.capsizes : 4096) * sizeof *fstat.sizes);
    fstat.sizes[fstat.nsizes++] = r->feed_bytes;
    return 1;
}

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

static struct rec *recs;
static int nrecs;
static struct genv *genvs;
/* --sparse K: a row record every K rows only (and the last), so most ticks
 * run as the product runs them, with no record's reads between */
static int sparse = 1;
/* --mask FIELD,...: compare those fields as equal whatever they hold (pipe_gate's) */
static const char *masks[8];
static int nmasks;
static void mask_line(char *s)
{
    for (int i = 0; i < nmasks; ++i)
    {
        char pat[96];
        snprintf(pat, sizeof pat, " %s=", masks[i]);   /* rowrec's form: " d.cseed=0000..." */
        for (char *p = s; (p = strstr(p, pat)) != NULL; ++p)
        {
            char *v = p + strlen(pat), *q = strchr(v, ' ');
            if (q == NULL) q = v + strlen(v);
            for (char *c = v; c < q; ++c) *c = '*';
        }
    }
}
/* --raycast-rows DIR */
static const char *rc_rows_dir;
/* --semcam WxH[:RANGE]: the semantic camera cast after every tick, both ways */
static struct semcam_spec semspec;
static int semcam_on;

static uint64_t thread_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* the env's client view cast by semcam_cast_ref and semcam_cast: equal bytes */
/* (the buffers are the process's: malloc in a hook is the env's heap, which a
 * reset restores under them) */
static void semcam_check(struct genv *g, struct session *ss, int64_t t)
{
    static _Thread_local struct semcam_cell *a, *b;
    static _Thread_local size_t cap;
    size_t n = (size_t)semspec.w * (size_t)semspec.h;
    if (cap < n)
    {
        proc_free(a);
        proc_free(b);
        a = proc_malloc(n * sizeof *a);
        b = proc_malloc(n * sizeof *b);
        cap = n;
    }
    uint64_t t0 = thread_ns();
    semcam_cast_ref(&semspec, ss, a);
    uint64_t t1 = thread_ns();
    semcam_cast(&semspec, ss, b);
    uint64_t t2 = thread_ns();
    g->sem_ref_ns += t1 - t0;
    g->sem_fast_ns += t2 - t1;
    ++g->sem_casts;
    g->sem_rays += (int64_t)n;
    uint64_t h = g->sem_digest ? g->sem_digest : 0xcbf29ce484222325ull;
    const unsigned char *bytes = (const unsigned char *)b;
    for (size_t i = 0; i < n * sizeof *b; ++i) h = (h ^ bytes[i]) * 0x100000001b3ull;
    g->sem_digest = h;
    for (size_t i = 0; i < n; ++i) g->sem_hits += b[i].face < SC_MISS;
    if (cast_cmp)
    {
        /* the FP32 instance's cast against this one (semspec's FP64), each ray's difference by kind */
        static _Thread_local struct semcam_cell *c;
        static _Thread_local double *dir;
        static _Thread_local size_t ccap;
        if (ccap < n)
        {
            proc_free(c);
            proc_free(dir);
            c = proc_malloc(n * sizeof *c);
            dir = proc_malloc(n * 3 * sizeof *dir);
            ccap = n;
        }
        struct semcam_spec s32 = semspec;
        s32.fp = 32;
        semcam_cast(&s32, ss, c);
        semcam_rays(&semspec, ss->cp.rotation_yaw, ss->cp.rotation_pitch, dir);
        const double eye[3] = {ss->cp.e.pos_x, ss->cp.e.pos_y + (double)(ss->cp.e.y_offset - 1.62F), ss->cp.e.pos_z};
        g->cmp[CMP_S_RAYS] += n;
        for (size_t i = 0; i < n; ++i)
        {
            const struct semcam_cell *p = &b[i], *q = &c[i];
            if (!memcmp(p, q, sizeof *p)) continue;
            ++g->cmp[CMP_S_DIFF];
            if (p->block == q->block && p->face == q->face)
            {
                int dd = (int)p->dist - (int)q->dist;
                ++g->cmp[dd == 1 || dd == -1 ? CMP_S_DIST1 : CMP_S_DISTN];
                continue;
            }
            ++g->cmp[CMP_S_TOPO];
            if ((p->face < SC_MISS) != (q->face < SC_MISS)) ++g->cmp[CMP_S_MISSHIT];
            const struct semcam_cell *h = p->face < SC_MISS ? p : q;   /* a hit (the reference's where both) */
            if (h->face >= SC_MISS) continue;
            const double *d = &dir[3 * i];
            const int ax = h->face < 2 ? 1 : h->face < 4 ? 2 : 0;   /* the face's normal axis: DOWN UP y, N S z, W E x */
            if (fabs(d[ax]) < 0.1) ++g->cmp[CMP_S_GRAZE];
            const double t = ((double)h->dist + 0.5) / 256.0;
            if (t > 32.0) ++g->cmp[CMP_S_FAR];
            double e = 1.0;
            for (int a = 0; a < 3; ++a)
                if (a != ax)
                {
                    double v = eye[a] + d[a] * t, f = v - floor(v);
                    if (f < e) e = f;
                    if (1.0 - f < e) e = 1.0 - f;
                }
            if (e < 1.0 / 128.0) ++g->cmp[CMP_S_EDGE];
            /* a near tie on the way: the reference's own steps (semcam.c's double walk, no reads) to the earlier of
             * the two hits, the least gap between the step's t and the next axis' t against t; under 1e-5 the ray
             * passed within float's error of a cell edge or corner, where the two orders may part */
            {
                const double tend = ((double)(p->face < SC_MISS ? p->dist : 65535) < (double)(q->face < SC_MISS ? q->dist : 65535)
                                         ? (double)p->dist : (double)q->dist) / 256.0 + 0.01;
                int s3[3];
                double t3[3], td[3];
                for (int a = 0; a < 3; ++a)
                {
                    const double o = eye[a], dd = d[a];
                    const double fo = floor(o);
                    s3[a] = dd > 0 ? 1 : dd < 0 ? -1 : 0;
                    td[a] = s3[a] ? fabs(1.0 / dd) : INFINITY;
                    t3[a] = s3[a] > 0 ? (fo + 1.0 - o) / dd : s3[a] < 0 ? (fo - o) / dd : INFINITY;
                }
                double gap = INFINITY;
                for (int st = 0; st < 4096; ++st)
                {
                    int a = t3[0] <= t3[1] && t3[0] <= t3[2] ? 0 : t3[1] <= t3[2] ? 1 : 2;
                    const double t = t3[a];
                    if (t > tend) break;
                    for (int b2 = 0; b2 < 3; ++b2)
                        if (b2 != a && t3[b2] < INFINITY)
                        {
                            const double gg = fabs(t3[b2] - t) / (t > 1.0 ? t : 1.0);
                            if (gg < gap) gap = gg;
                        }
                    t3[a] += td[a];
                }
                if (gap < 1e-5) ++g->cmp[CMP_S_NEARTIE];
            }
        }
    }
    if (!g->sem_bad && memcmp(a, b, n * sizeof *a))
    {
        size_t i = 0;
        while (!memcmp(&a[i], &b[i], sizeof *a)) ++i;
        g->sem_bad = 1;
        snprintf(g->sem_why, sizeof g->sem_why,
                 "row %lld ray (%zu, %zu): reference block %u dist %u face %u, fast block %u dist %u face %u", (long long)t,
                 i % (size_t)semspec.w, i / (size_t)semspec.w, a[i].block, a[i].dist, a[i].face, b[i].block, b[i].dist,
                 b[i].face);
    }
}

/* --raycast WxH[:fancy] (up to four): the textured ray cast after every tick,
 * both ways */
static struct raycast_spec rcspec[4];
static int nrcspec;
/* --raycast-each: episode e checks spec (e / recordings) mod specs alone, so a
 * recording's sizes run on different envs at once; the digests are by spec */
static int rc_each;
/* --raycast-nan (lane/crash2, raycast-nan-gate): after the real casts of a row, the camera with a NaN yaw, a NaN
 * pitch and both is cast too, each held byte-equal to the same camera at 0 (what the cast falls back to). Before the
 * fix a NaN yaw gave every ray a NaN direction and the cast never returned (a search stopped for 15 minutes on it);
 * the gate's command runs under timeout */
static int rc_nan;
static void raycast_nan_check(struct genv *g, const struct raycast_spec *sp, struct session *ss, int64_t t, size_t n)
{
    static _Thread_local uint8_t *x, *y;
    static _Thread_local size_t cap;
    if (cap < n)
    {
        proc_free(x);
        proc_free(y);
        x = proc_malloc(n);
        y = proc_malloc(n);
        cap = x && y ? n : 0;
        if (cap == 0) return;
    }
    const float yaw = ss->cp.rotation_yaw, pitch = ss->cp.rotation_pitch;
    static const char *const WHAT[3] = {"yaw", "pitch", "yaw and pitch"};
    for (int v = 0; v < 3 && !g->rc_bad; ++v)
    {
        const int ny = v != 1, np = v != 0;
        ss->cp.rotation_yaw = ny ? 0.0F : yaw, ss->cp.rotation_pitch = np ? 0.0F : pitch;
        raycast(sp, ss, x);
        ss->cp.rotation_yaw = ny ? NAN : yaw, ss->cp.rotation_pitch = np ? NAN : pitch;
        raycast(sp, ss, y);
        if (memcmp(x, y, n))
        {
            g->rc_bad = 1;
            snprintf(g->rc_why, sizeof g->rc_why, "row %lld %dx%d: a NaN %s casts a frame other than the camera at 0",
                     (long long)t, sp->w, sp->h, WHAT[v]);
        }
    }
    ss->cp.rotation_yaw = yaw, ss->cp.rotation_pitch = pitch;
}
/* --raycast-device (pool_gate_rcd, cuda/raycast): each spec's cast on the device too, after every tick: its frame
 * against raycast_ref's, its seen record against raycast_seen's (the fast cast's own: the CPU has no other) */
static int rc_device;
#ifdef POOL_GATE_RCD
static struct rcdev *rcd[4];
static struct pool *gate_pool;
#endif

/* the env's client view cast by raycast_ref and raycast at every spec: equal bytes */
static void raycast_check(struct genv *g, int id, struct session *ss, int64_t t)
{
    static _Thread_local uint8_t *a, *b;
    static _Thread_local size_t cap;
    for (int s = 0; s < nrcspec; ++s)
    {
        if (g->rc_spec >= 0 && s != g->rc_spec) continue;
        const struct raycast_spec *sp = &rcspec[s];
        size_t n = (size_t)sp->w * (size_t)sp->h * (size_t)(sp->ch == 1 ? 1 : 3);
        if (cap < n)
        {
            proc_free(a);
            proc_free(b);
            a = proc_malloc(n);
            b = proc_malloc(n);
            cap = n;
        }
        uint64_t t0 = thread_ns();
        raycast_ref(sp, ss, a);
        uint64_t t1 = thread_ns();
        raycast(sp, ss, b);
        uint64_t t2 = thread_ns();
#ifdef POOL_GATE_RCD
        if (rc_device)
        {
            static const int16_t IDS[RC_SEEN] = {15, 56, 112, 17};
            static _Thread_local uint8_t *c;
            static _Thread_local size_t ccap;
            if (ccap < n)
            {
                proc_free(c);
                c = proc_malloc(n);
                ccap = n;
            }
            struct raycast_seen cs;
            raycast_seen(sp, ss, c, IDS, &cs);
            uint64_t t3 = thread_ns();
            struct rcdev *d = rcd[s];
            struct rcd_head *hd = rcdev_head(d, id);
            int refused = raycast_head(sp, ss, IDS, hd) == 0 && rcmirror_sync(rcdev_mirror(d), id, pool_epoch(gate_pool, id), ss, hd) != 0;
            int failed = rcdev_cast_now(d, id, c) != 0;
            g->rc_dev_ns += thread_ns() - t3;
            ++g->rc_dev_casts;
            const struct rcd_seen *ds = rcdev_seen(d, id);
            if (!g->rc_bad && (refused || failed))
            {
                g->rc_bad = 1;
                snprintf(g->rc_why, sizeof g->rc_why, "row %lld %dx%d: the device %s", (long long)t, sp->w, sp->h,
                         refused ? "copy refused the world" : rcdev_error(d));
            }
            else if (!g->rc_bad && memcmp(a, c, n))
            {
                size_t i = 0;
                while (a[i] == c[i]) ++i;
                i /= 3;
                g->rc_bad = 1;
                snprintf(g->rc_why, sizeof g->rc_why, "row %lld %dx%d pixel (%zu, %zu): reference %u %u %u, device %u %u %u",
                         (long long)t, sp->w, sp->h, i % (size_t)sp->w, i / (size_t)sp->w, a[3 * i], a[3 * i + 1],
                         a[3 * i + 2], c[3 * i], c[3 * i + 1], c[3 * i + 2]);
            }
            else if (!g->rc_bad && memcmp(&cs, ds, sizeof cs))
            {
                int q = 0;
                while (q < RC_SEEN - 1 && cs.n[q] == ds->n[q] && cs.d[q] == ds->d[q] && cs.x[q] == ds->x[q] &&
                       cs.y[q] == ds->y[q] && cs.z[q] == ds->z[q])
                    ++q;
                g->rc_bad = 1;
                snprintf(g->rc_why, sizeof g->rc_why,
                         "row %lld %dx%d seen %d: fast %d pixels, nearest %.9g at %d %d %d; device %d, %.9g at %d %d %d",
                         (long long)t, sp->w, sp->h, q, cs.n[q], (double)cs.d[q], cs.x[q], cs.y[q], cs.z[q], ds->n[q],
                         (double)ds->d[q], ds->x[q], ds->y[q], ds->z[q]);
            }
        }
#endif
        if (cast_cmp && sp->fp != cmp_fp)
        {
            /* the FP32 instance's frame (--cast-cmp-with's) against this one: the pixels and bytes that differ */
            static _Thread_local uint8_t *c32;
            static _Thread_local size_t c32cap;
            if (c32cap < n)
            {
                proc_free(c32);
                c32 = proc_malloc(n);
                c32cap = n;
            }
            struct raycast_spec s32 = *sp;
            s32.fp = cmp_fp;
            /* each pixel's ray both ways (raycast_rays, lane/cxfix): this cast's again with the record on, then the
             * other instance's (its frame the one compared below) */
            static _Thread_local struct raycast_ray *ra, *rb;
            static _Thread_local size_t rcap;
            const size_t npx = (size_t)sp->w * (size_t)sp->h;
            if (rcap < npx)
            {
                proc_free(ra);
                proc_free(rb);
                ra = proc_malloc(npx * sizeof *ra);
                rb = proc_malloc(npx * sizeof *rb);
                rcap = npx;
            }
            raycast_rays(ra);
            raycast(sp, ss, c32);
            raycast_rays(rb);
            raycast(&s32, ss, c32);
            raycast_rays(NULL);
            g->cmp[CMP_R_RAYS] += npx;
            ++g->cmp[CMP_R_CASTS];
            int topo = 0;
            for (size_t p = 0; p < npx; ++p)
            {
                const struct raycast_ray *x = &ra[p], *y = &rb[p];
                if (!memcmp(x, y, sizeof *x)) continue;
                ++g->cmp[CMP_R_DIFF];
                topo |= !(x->kind == y->kind && x->x == y->x && x->y == y->y && x->z == y->z && x->axis == y->axis);
                if (x->kind == y->kind && x->x == y->x && x->y == y->y && x->z == y->z && x->axis == y->axis)
                    ++g->cmp[x->dist - y->dist == 1 || y->dist - x->dist == 1 ? CMP_R_DIST1 : CMP_R_DISTN];
                else
                {
                    ++g->cmp[CMP_R_TOPO];
                    if (x->kind != y->kind) ++g->cmp[CMP_R_KIND];
                }
            }
            g->cmp[CMP_R_TFRAMES] += (uint64_t)topo;
            const size_t chn = sp->ch == 1 ? 1 : 3;
            g->cmp[CMP_P_PIX] += n / chn;
            for (size_t p = 0; p < n; p += chn)
            {
                int any = 0;
                for (size_t q = 0; q < chn; ++q)
                {
                    int dd = (int)b[p + q] - (int)c32[p + q];
                    if (dd < 0) dd = -dd;
                    if (dd == 0) continue;
                    any = 1;
                    ++g->cmp[CMP_P_BYTES];
                    g->cmp[CMP_P_SUMD] += (uint64_t)dd;
                    if ((uint64_t)dd > g->cmp[CMP_P_MAXD]) g->cmp[CMP_P_MAXD] = (uint64_t)dd;
                }
                g->cmp[CMP_P_DIFF] += any;
            }
        }
        g->rc_ref_ns += t1 - t0;
        g->rc_fast_ns += t2 - t1;
        ++g->rc_casts;
        uint64_t h = g->rc_digest ? g->rc_digest : 0xcbf29ce484222325ull;
        for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 0x100000001b3ull;
        g->rc_digest = h;
        if (g->rc_rows != NULL && t >= g->r->tick && t < g->r->nrows && s == (g->rc_spec >= 0 ? g->rc_spec : 0))
        {
            uint64_t h1 = 0xcbf29ce484222325ull;
            for (size_t i = 0; i < n; ++i) h1 = (h1 ^ b[i]) * 0x100000001b3ull;
            g->rc_rows[t] = h1;
        }
        if (!g->rc_bad && memcmp(a, b, n))
        {
            size_t i = 0;
            while (a[i] == b[i]) ++i;
            i /= 3;
            g->rc_bad = 1;
            snprintf(g->rc_why, sizeof g->rc_why, "row %lld %dx%d pixel (%zu, %zu): reference %u %u %u, fast %u %u %u",
                     (long long)t, sp->w, sp->h, i % (size_t)sp->w, i / (size_t)sp->w, a[3 * i], a[3 * i + 1],
                     a[3 * i + 2], b[3 * i], b[3 * i + 1], b[3 * i + 2]);
        }
        if (rc_nan && t % 25 == 0 && !g->rc_bad) raycast_nan_check(g, sp, ss, t, n);
    }
}

/* --raycast-dump DIR:WxH:EVERY: each recording's first episode's cast at
 * WxH every EVERY rows as DIR/NAME.r_TTTTTT.png (DIR made by the caller) (to look at); DIR:WxH:@T row T's alone
 * (rcparity.sh: the frame of the first row the oracle's cast differs on) */
static const char *rc_dump_dir;
static struct raycast_spec rc_dump_spec;
static int rc_dump_every;
static long long rc_dump_at = -1;

/* --raycast-dump DIR:WxH:@T also writes DIR/NAME.r_TTTTTT.txt: the cast's setup (raycast_head: the eye, the camera,
 * the light, sky and fog, every entity box), each real as its bits, and each pixel's walk as it ended (raycast_rays:
 * kind 0 a block, its cell, entry axis and distance in 1/256; 1 the sky or fog; 2 an entity box, its slot, face and
 * distance), the form RayCast.java's --raycast-dump DIR:@T writes for the oracle's cast, so a diff of the two names
 * what differs (rcparity.sh) */
static uint64_t dbits(double v)
{
    uint64_t u;
    memcpy(&u, &v, 8);
    return u;
}

static uint32_t fbits(float v)
{
    uint32_t u;
    memcpy(&u, &v, 4);
    return u;
}

static void raycast_trace(struct genv *g, struct session *ss, int64_t t)
{
    char path[1400];
    snprintf(path, sizeof path, "%s/%s.r_%06lld.txt", rc_dump_dir, g->r->name, (long long)t);
    FILE *f = fopen(path, "w");
    if (f == NULL) return;
    struct rcd_head *h = proc_malloc(sizeof *h);
    const int w = rc_dump_spec.w, hh = rc_dump_spec.h;
    struct raycast_ray *r = proc_malloc((size_t)w * (size_t)hh * sizeof *r);
    uint8_t *img = proc_malloc((size_t)w * (size_t)hh * 3);
    if (h && raycast_head(&rc_dump_spec, ss, NULL, h) == 0)
    {
        fprintf(f, "eye %016llx %016llx %016llx cell %d %d %d\n", (unsigned long long)dbits(h->ox), (unsigned long long)dbits(h->oy),
                (unsigned long long)dbits(h->oz), h->eye_x, h->eye_y, h->eye_z);
        fprintf(f, "look %016llx %016llx %016llx %016llx %016llx %016llx %016llx %016llx\n", (unsigned long long)dbits(h->fx),
                (unsigned long long)dbits(h->fy), (unsigned long long)dbits(h->fz), (unsigned long long)dbits(h->rx),
                (unsigned long long)dbits(h->rz), (unsigned long long)dbits(h->ux), (unsigned long long)dbits(h->uy),
                (unsigned long long)dbits(h->uz));
        fprintf(f, "cast liquid %d range %d dim %d no_sky %d\n", h->liquid, h->range, h->dim, h->no_sky);
        fprintf(f, "sky %08x %08x %08x fog %08x %08x %08x\n", fbits(h->sky[0]), fbits(h->sky[1]), fbits(h->sky[2]), fbits(h->fog[0]),
                fbits(h->fog[1]), fbits(h->fog[2]));
        fprintf(f, "fogform exp %d fs %08x fe %08x fd %08x sky_end %08x uw %d %08x %08x %08x\n", h->exp_fog, fbits(h->fs), fbits(h->fe),
                fbits(h->fd), fbits(h->sky_end), h->uw, fbits(h->uw_rgb[0]), fbits(h->uw_rgb[1]), fbits(h->uw_rgb[2]));
        for (int i = 0; i < 256; ++i)
        {
#if NETH_RC_HEAD_U8
            /* the packed lightmap as the floats raycast.c makes of it (the same lines as the float layout's) */
            const float l0 = (float)(h->lm[i] >> 16 & 255) / 255.0f, l1 = (float)(h->lm[i] >> 8 & 255) / 255.0f,
                        l2 = (float)(h->lm[i] & 255) / 255.0f;
#else
            const float l0 = h->lm[i][0], l1 = h->lm[i][1], l2 = h->lm[i][2];
#endif
            fprintf(f, "lm %d %08x %08x %08x\n", i, fbits(l0), fbits(l1), fbits(l2));
        }
        for (int e = 0; e < h->nent; ++e)
        {
            const struct rcd_ent *en = &h->ent[e];
            fprintf(f, "ent %d", e);
            for (int a = 0; a < 6; ++a) fprintf(f, " %016llx", (unsigned long long)dbits(en->box[a]));
            fprintf(f, " rect %d %d %d %d rgb %08x %08x %08x lmi %d  (%.4f %.4f %.4f)\n", en->i0, en->i1, en->j0, en->j1,
                    fbits(en->rgb[0]), fbits(en->rgb[1]), fbits(en->rgb[2]), en->lmi, en->box[0], en->box[1], en->box[2]);
        }
    }
    else fprintf(f, "no cast\n");
    if (r && img)
    {
        raycast_rays(r);
        raycast_ref(&rc_dump_spec, ss, img);
        raycast_rays(NULL);
        for (int p = 0; p < w * hh; ++p)
        {
            fprintf(f, "px %d kind %d %d %d %d axis %d dist %d", p, r[p].kind, r[p].kind == 1 ? 0 : r[p].x, r[p].kind == 0 ? r[p].y : 0,
                    r[p].kind == 0 ? r[p].z : 0, r[p].kind == 1 ? 0 : r[p].axis, r[p].kind == 1 ? 0 : r[p].dist);
            if (r[p].kind == 0 && h && ss->client_world)
            {
                /* the client world's light at the cell hit and at the one before it on the entry axis, its biome */
                int c3[3] = {r[p].x, r[p].y, r[p].z}, e3[3] = {h->eye_x, h->eye_y, h->eye_z}, q3[3] = {r[p].x, r[p].y, r[p].z};
                q3[r[p].axis] -= c3[r[p].axis] > e3[r[p].axis] ? 1 : c3[r[p].axis] < e3[r[p].axis] ? -1 : 0;
                struct world *cw = ss->client_world;
                struct chunk *ch = world_chunk(cw, r[p].x >> 4, r[p].z >> 4);
                fprintf(f, " pre %d %d cell %d %d bio %d", world_get_light(cw, LIGHT_SKY, q3[0], q3[1], q3[2]),
                        world_get_light(cw, LIGHT_BLOCK, q3[0], q3[1], q3[2]), world_get_light(cw, LIGHT_SKY, c3[0], c3[1], c3[2]),
                        world_get_light(cw, LIGHT_BLOCK, c3[0], c3[1], c3[2]), ch ? ch->biome[(r[p].z & 15) << 4 | (r[p].x & 15)] : -1);
            }
            fputc('\n', f);
        }
    }
    proc_free(img);
    proc_free(r);
    proc_free(h);
    fclose(f);
}

static void raycast_dump(struct genv *g, struct session *ss, int64_t t)
{
    if (g->episode >= nrecs || (rc_dump_at >= 0 ? t != rc_dump_at : t % rc_dump_every != 0)) return;
    size_t n = (size_t)rc_dump_spec.w * (size_t)rc_dump_spec.h * (size_t)(rc_dump_spec.ch == 1 ? 1 : 3);
    uint8_t *img = proc_malloc(n);
    char path[1400];
    snprintf(path, sizeof path, "%s/%s.r_%06lld.png", rc_dump_dir, g->r->name, (long long)t);
    if (img && raycast(&rc_dump_spec, ss, img) == 0) raster_png(path, rc_dump_spec.w, rc_dump_spec.h, img);
    proc_free(img);
    if (rc_dump_at >= 0) raycast_trace(g, ss, t);
}

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

/* The first differing field of two records (space-separated name=value). */
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

/* After each tick, on the worker: the env's record of row t against the
 * reference's. */
static void gate_hook(void *ctx, int id, struct session *ss, int64_t t, int last)
{
    (void)ctx;
    struct genv *g = &genvs[id];
    struct rec *r = g->r;
    if (semcam_on) semcam_check(g, ss, t);
    if (nrcspec) raycast_check(g, id, ss, t);
    if (rc_dump_dir) raycast_dump(g, ss, t);
    if (sparse > 1 && t % sparse != 0 && t != r->nrows - 1) return;
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
    if (nmasks > 0)
    {
        static _Thread_local char wantbuf[16384];
        snprintf(wantbuf, sizeof wantbuf, "%s", want);
        mask_line(wantbuf);
        mask_line(got);
        want = wantbuf;
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
    /* rows: line i + 1 is row t = i */
    r->rows = lines + 1;
    r->nrows = n - 1;
    const char *base = strrchr(r->name, '/') ? strrchr(r->name, '/') + 1 : r->name;
    snprintf(path, sizeof path, "%s/%s.rows", refdir, base);
    int64_t nref;
    char **ref = read_lines(path, &nref);
    if (ref == NULL || nref < 1) { printf("FAIL %s: no reference %s (test_snapshots --row-dump)\n", r->name, path); return 0; }
    /* index the reference by t */
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
    snprintf(path, sizeof path, "%s/tape.jsonl", r->dir);
    uint64_t seed;
    int32_t lcg;
    cwrand_from_tape(path, r->tick, &seed, &lcg, &r->cw_from);
    return 1;
}

/* Act.SHORT's names (and the bindings' own) to enum pool_key */
static int agent_key(const char *name)
{
    static const char *const names[PK_KEYS] = {"forward", "back", "left", "right", "jump", "sneak", "sprint", "attack",
                                               "use", "drop", "inventory", "pick"};
    if (!strncmp(name, "key.", 4)) name += 4;
    if (!strcmp(name, "pickItem")) return PK_PICK;
    for (int k = 0; k < PK_HOTBAR1; ++k)
        if (names[k] && !strcmp(name, names[k])) return k;
    if (!strncmp(name, "hotbar.", 7) && name[7] >= '1' && name[7] <= '9' && !name[8]) return PK_HOTBAR1 + (name[7] - '1');
    return -1;
}

/* One script line's act (Act.parseAgent); 0 with why for a form the pool
 * does not take (aim, chat, opts). */
static int agent_parse(const struct jval *o, struct pool_act *a, char *why, size_t n)
{
    memset(a, 0, sizeof *a);
    a->kind = PA_AGENT;
    a->hotbar = -1;
    for (int i = 0; o != NULL && i < o->nfields; ++i)
    {
        const char *k = o->fields[i].key;
        const struct jval *v = o->fields[i].val;
        if (!strcmp(k, "hold") || !strcmp(k, "press"))
            for (int j = 0; j < json_len(v); ++j)
            {
                int key = agent_key(json_str(json_at(v, j)) ? json_str(json_at(v, j)) : "");
                if (key < 0) { snprintf(why, n, "key %s", json_str(json_at(v, j))); return 0; }
                if (k[0] == 'h') a->hold |= 1u << key;
                else ++a->press[key];
            }
        else if (!strcmp(k, "look") || !strcmp(k, "dlook"))
        {
            a->look_mode = k[0] == 'l' ? PL_LOOK : PL_DLOOK;
            a->look_prev = k[0] == 'l' && json_len(v) >= 4;
            for (int j = 0; j < 4 && j < json_len(v); ++j)
            {
                char *raw = json_raw(json_at(v, j));
                a->look[j] = strtof(raw, NULL);   /* Float.parseFloat */
                free(raw);
            }
        }
        else if (!strcmp(k, "hotbar"))
        {
            int64_t h;
            json_int(v, &h);
            a->hotbar = h == -1 ? -2 : (int)h;   /* an explicit -1 is refused as Act.checkHotbar does */
        }
        else if (!strcmp(k, "ctrl")) { int64_t c; json_int(v, &c); a->ctrl = (int)c; }
        else if (!strcmp(k, "gui"))
            for (int j = 0; j < json_len(v); ++j)
            {
                const struct jval *op = json_at(v, j);
                const char *kind = json_str(json_at(op, 0));
                struct pool_gui_op g = {0};
                int64_t x[4] = {0, 0, 0, 0};
                for (int q = 0; q < 4; ++q) json_int(json_at(op, q + 1), &x[q]);
                if (!kind) { snprintf(why, n, "gui op"); return 0; }
                if (!strcmp(kind, "click")) g = (struct pool_gui_op){PG_CLICK, (int)x[0], (int)x[1], (int)x[2], (int)x[3], 0};
                else if (!strcmp(kind, "close")) g.kind = PG_CLOSE;
                else if (!strcmp(kind, "respawn")) g.kind = PG_RESPAWN;
                else if (!strcmp(kind, "wake")) g.kind = PG_WAKE;
                else if (!strcmp(kind, "trsel")) g = (struct pool_gui_op){PG_TRSEL, (int)x[0], 0, 0, 0, (int)x[1]};
                else { snprintf(why, n, "gui op %s", kind); return 0; }
                if (a->nops == POOL_GUI_OPS) { snprintf(why, n, "more than %d gui ops", POOL_GUI_OPS); return 0; }
                a->ops[a->nops++] = g;
            }
        else { snprintf(why, n, "act field %s", k); return 0; }
    }
    return 1;
}

/* The script's steps after its Snapshot command; 0 with why when the pool
 * cannot take them. */
static int load_script(struct rec *r, const char *scripts, char *why, size_t n)
{
    char path[4400];
    const char *base = strrchr(r->name, '/') ? strrchr(r->name, '/') + 1 : r->name;
    snprintf(path, sizeof path, "%s/%s.jsonl", scripts, base);
    int64_t nl;
    char **lines = read_lines(path, &nl);
    if (lines == NULL) { snprintf(why, n, "no script %.200s", path); return 0; }
    int after = 0;
    r->steps = calloc((size_t)nl + 1, sizeof *r->steps);
    r->step_n = calloc((size_t)nl + 1, sizeof *r->step_n);
    for (int64_t i = 0; i < nl; ++i)
    {
        struct jval *j = json_parse(strdup(lines[i]));
        if (j == NULL) continue;
        const char *cmd = json_str(json_get(j, "cmd"));
        const char *cls = json_str(json_get(j, "class"));
        int64_t steps = 0;
        if (cmd && !strcmp(cmd, "run") && cls && !strcmp(cls, "Snapshot") && !json_get(j, "sub")) after = 1;
        else if (after && cmd && strcmp(cmd, "dev") && strcmp(cmd, "run")) { snprintf(why, n, "command %s after the snapshot", cmd); json_free(j); return 0; }
        /* the header carries the setup classes; Stats and a sub snapshot
         * (the end state) only read the oracle */
        else if (after && cmd && !strcmp(cmd, "run") && !(cls && !strcmp(cls, "Snapshot")) &&
                 !(cls && (!strcmp(cls, "MobFree") || !strcmp(cls, "MobMove") || !strcmp(cls, "Stats"))))
        {
            snprintf(why, n, "run %s after the snapshot", cls ? cls : "?");
            json_free(j);
            return 0;
        }
        else if (after && !cmd && json_int(json_get(j, "n"), &steps))
        {
            if (!agent_parse(json_get(j, "act"), &r->steps[r->nsteps], why, n)) { json_free(j); return 0; }
            r->step_n[r->nsteps++] = (int)steps;
        }
        json_free(j);
    }
    if (!after) { snprintf(why, n, "no Snapshot command"); return 0; }
    return 1;
}

/* A loaded start: its first row must be the reference's; its numbers. */
static int start_ready(struct rec *r, struct pool_start *st, size_t *outside, size_t *vdiff)
{
    struct pool_start_info info;
    pool_start_info(st, &info);
    *outside += info.outside;
    *vdiff += info.value_diffs;
    if (pool_start_tick(st) != r->tick)
    {
        printf("FAIL %s: the start's tick %lld is not the reference's first row %lld\n", r->name,
               (long long)pool_start_tick(st), (long long)r->tick);
        return 0;
    }
    printf("start %s: tick %lld, %zu pages (%.1f MB), %zu pointers, %zu outside, %zu value diffs, %.0f ms\n", r->name,
           (long long)r->tick, info.image_pages, info.image_bytes / 1048576.0, info.pointers, info.outside,
           info.value_diffs, info.load_ms);
    r->start = st;
    return 1;
}

int main(int argc, char **argv)
{
    int n = 1, threads = 1, ticks = 4, episodes = -1, pollb = -1;
    const char *snapdir = "out/java/snapshots", *refdir = "out/runtime/ref", *scripts = NULL;
    int feed = 0;
    const char *world = NULL, *gen_kind = NULL;
    int fast = 0;
    const char **names = calloc((size_t)argc, sizeof *names);
    int nnames = 0;
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "-n") && i + 1 < argc) n = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-j") && i + 1 < argc) threads = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--ticks") && i + 1 < argc) ticks = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--episodes") && i + 1 < argc) episodes = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--poll") && i + 1 < argc) pollb = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--snapdir") && i + 1 < argc) snapdir = argv[++i];
        else if (!strcmp(argv[i], "--refdir") && i + 1 < argc) refdir = argv[++i];
        else if (!strcmp(argv[i], "--agent") && i + 1 < argc) scripts = argv[++i];
        else if (!strcmp(argv[i], "--feed")) feed = 1;
        else if (!strcmp(argv[i], "--sparse") && i + 1 < argc) sparse = atoi(argv[++i]);
        else if ((!strcmp(argv[i], "--world") || !strcmp(argv[i], "--config")) && i + 1 < argc) world = argv[++i];
        else if (!strcmp(argv[i], "--gen") && i + 1 < argc) gen_kind = argv[++i];
        else if (!strcmp(argv[i], "--fast")) fast = 1;
        else if (!strcmp(argv[i], "--raycast-rows") && i + 1 < argc) rc_rows_dir = argv[++i];
        else if (!strcmp(argv[i], "--mask") && i + 1 < argc)
        {
            char *list = argv[++i];
            while (*list != 0 && nmasks < 8)
            {
                masks[nmasks++] = list;
                char *c = strchr(list, ',');
                if (c == NULL) break;
                *c = 0;
                list = c + 1;
            }
        }
        else if (!strcmp(argv[i], "--semcam") && i + 1 < argc)
        {
            const char *v = argv[++i];
            semspec.range = 64;
            if (sscanf(v, "%dx%d:%d", &semspec.w, &semspec.h, &semspec.range) < 2 || semspec.w < 1 || semspec.h < 1 ||
                semspec.range < 1)
            {
                fprintf(stderr, "pool_gate: --semcam WxH[:RANGE][:f32]\n");
                return 2;
            }
            semspec.fp = strstr(v, ":f32") != NULL ? 32 : 64;   /* the FP32 instance (client.cast_precision 32) */
            semcam_on = 1;
        }
        else if (!strcmp(argv[i], "--raycast-dump") && i + 1 < argc)
        {
            static char dir[1024];
            if (sscanf(argv[i + 1], "%1023[^:]:%dx%d:@%lld", dir, &rc_dump_spec.w, &rc_dump_spec.h, &rc_dump_at) == 4)
                rc_dump_every = 1;
            else rc_dump_at = -1;
            if ((rc_dump_at < 0 && sscanf(argv[i + 1], "%1023[^:]:%dx%d:%d", dir, &rc_dump_spec.w, &rc_dump_spec.h, &rc_dump_every) != 4) ||
                rc_dump_spec.w < 1 || rc_dump_spec.h < 1 || rc_dump_every < 1)
            {
                fprintf(stderr, "pool_gate: --raycast-dump DIR:WxH:EVERY[:f32] or DIR:WxH:@ROW\n");
                return 2;
            }
            rc_dump_spec.fp = spec_fp(argv[++i]);   /* the FP32 (fixed, FP16) instance's frames */
            rc_dump_dir = dir;
        }
        else if (!strcmp(argv[i], "--raycast-each")) rc_each = 1;
        else if (!strcmp(argv[i], "--raycast-nan")) rc_nan = 1;
        else if (!strcmp(argv[i], "--cast-cmp")) cast_cmp = 1;
        else if (!strcmp(argv[i], "--cast-cmp-with") && i + 1 < argc) cast_cmp = 1, cmp_fp = spec_fp(argv[++i]);
        else if (!strcmp(argv[i], "--raycast-device")) rc_device = 1;
        else if (!strcmp(argv[i], "--raycast") && i + 1 < argc)
        {
            const char *v = argv[++i];
            struct raycast_spec sp = {0};
            char f[16] = "";
            /* :fancy (client.graphics fancy), :f32 (the cast's FP32 instance, client.cast_precision 32; :fx fixed,
             * :f16 16), in that order */
            int ok = nrcspec < 4 && sscanf(v, "%dx%d:%15s", &sp.w, &sp.h, f) >= 2 && sp.w >= 1 && sp.h >= 1;
            if (ok && f[0])
            {
                const char *g = strncmp(f, "fancy", 5) == 0 ? f + 5 + (f[5] == ':') : f;
                ok = !*g || !strcmp(g, "f32") || !strcmp(g, "fx") || !strcmp(g, "f16");
                sp.fancy = strncmp(f, "fancy", 5) == 0;
                sp.fp = spec_fp(g);
            }
            if (!ok)
            {
                fprintf(stderr, "pool_gate: --raycast WxH[:fancy][:f32] (at most four)\n");
                return 2;
            }
            {
                /* WxHxCH: a second x names the frame's channels (client.channels; 3 without it) */
                const char *x2 = strchr(v, 'x');
                x2 = x2 ? strchr(x2 + 1, 'x') : NULL;
                sp.ch = x2 != NULL && atoi(x2 + 1) == 1 ? 1 : 3;
            }
            rcspec[nrcspec++] = sp;
        }
        else if (argv[i][0] == '-') { fprintf(stderr, "pool_gate: unknown flag %s\n", argv[i]); return 2; }
        else names[nnames++] = argv[i];
    }
    if (nnames == 0 || n < 1 || ticks < 1)
    {
        fprintf(stderr, "usage: pool_gate [-n N] [-j THREADS] [--ticks n] [--episodes E] [--poll B] [--snapdir DIR] [--refdir DIR] NAME...\n");
        return 2;
    }
    if (nrcspec || rc_dump_dir)
    {
        /* the textures, before any env: a cast without them is black both ways and proves nothing */
        char e[1600];
        if (raycast_load(NULL, e, sizeof e)) { printf("FAIL %s\n", e); return 1; }
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
    /* --agent DIR: the steps are the scripts' agent acts, not the tapes' rows */
    if (scripts != NULL)
    {
        int k = 0;
        for (int i = 0; i < nrecs; ++i)
        {
            char why[256];
            if (!load_script(&recs[i], scripts, why, sizeof why))
            {
                printf("skip %s: %s\n", recs[i].name, why);
                continue;
            }
            recs[k++] = recs[i];
        }
        nrecs = k;
        if (nrecs == 0) { printf("FAIL no recording's script is the pool's agent form\n"); return 1; }
        if (episodes > 2 * n && episodes > nrecs) episodes = 2 * n > nrecs ? 2 * n : nrecs;
    }

    struct pool_config cfg = {0};
    cfg.n = n;
    cfg.threads = threads;
    cfg.obs.ticks = ticks;
    cfg.dev_apply = dev_apply;
    cfg.dev_end = dev_end;
    cfg.tick_hook = gate_hook;
    cfg.feed = feed;
    cfg.config = world;
    cfg.tick_fast = fast;
    cfg.privileged = 1;
    cfg.vanilla_only = 1;   /* the gate checks rows the oracle recorded */
    struct ga_gen *gen = NULL;
    if (gen_kind != NULL)
    {
        if (!strcmp(gen_kind, "c")) gen = ga_gen_c_create();
#ifdef POOL_GATE_CUDA
        else if (!strcmp(gen_kind, "cuda")) gen = ga_gen_cuda_local_create(1024, 256);
#endif
        if (gen == NULL) { printf("FAIL --gen %s: no such generator in this build\n", gen_kind); return 1; }
        cfg.gen = gen;
    }
    char err[512];
    genvs = calloc((size_t)n, sizeof *genvs);
    struct pool *p = pool_make(&cfg, err, sizeof err);
    if (p == NULL) { printf("FAIL pool_make: %s\n", err); return 1; }
    if (rc_device)
    {
#ifdef POOL_GATE_RCD
        /* each spec's device: its rings for render distance 8 (the gate's recordings' most) */
        struct rcd_tabsrc tabs;
        if (raycast_tables(&tabs)) { printf("FAIL --raycast-device: no tables\n"); return 1; }
        gate_pool = p;
        for (int s = 0; s < nrcspec; ++s)
            if ((rcd[s] = rcdev_new(n, rcspec[s].w, rcspec[s].h, rcspec[s].ch, 128, 0, &tabs, err, sizeof err)) == NULL)
            {
                printf("FAIL --raycast-device: %s\n", err);
                return 1;
            }
#else
        printf("FAIL --raycast-device: pool_gate_rcd's (cuda/raycast)\n");
        return 1;
#endif
    }

    /* the starts are loaded when their first episode needs them (the first
     * N at once) and freed after their last reset: the pool holds about N
     * start images, not one per recording */
    for (int e = 0; e < episodes; ++e) ++recs[e % nrecs].left;
    double load_s = 0;
    size_t outside = 0, vdiff = 0;
    {
        int k0 = n < nrecs ? n : nrecs;
        if (k0 > episodes) k0 = episodes;
        const char **dirs = malloc((size_t)k0 * sizeof *dirs);
        struct pool_start **starts = malloc((size_t)k0 * sizeof *starts);
        for (int i = 0; i < k0; ++i) dirs[i] = recs[i].dir;
        double tl = now_s();
        if (pool_start_load_many(p, dirs, k0, starts, err, sizeof err) != 0) { printf("FAIL start: %s\n", err); return 1; }
        load_s += now_s() - tl;
        for (int i = 0; i < k0; ++i)
            if (!start_ready(&recs[i], starts[i], &outside, &vdiff)) return 1;
        free(dirs);
        free(starts);
    }

    struct pool_act *acts = calloc((size_t)ticks, sizeof *acts);
    int next_episode = 0, live = 0;
    int64_t *rowpos = calloc((size_t)n, sizeof *rowpos);
    double reset_s = 0;
    int nresets = 0;

    /* an env's next episode: reset to its recording, then its first step */
    int *one = malloc(sizeof *one);
    #define START_EPISODE(id) do {                                                           \
        struct genv *g_ = &genvs[(id)];                                                      \
        g_->r = &recs[next_episode % nrecs];                                                 \
        g_->episode = next_episode++;                                                        \
        g_->bad = 0; g_->checked = 0; g_->why[0] = 0; g_->step = 0;                          \
        g_->sem_digest = 0; g_->sem_ref_ns = g_->sem_fast_ns = 0; g_->sem_bad = 0;          \
        g_->sem_casts = g_->sem_hits = g_->sem_rays = 0; g_->sem_why[0] = 0;                \
        g_->rc_digest = 0; g_->rc_ref_ns = g_->rc_fast_ns = 0; g_->rc_bad = 0;              \
        g_->rc_casts = 0; g_->rc_why[0] = 0; g_->rc_dev_casts = 0; g_->rc_dev_ns = 0;             \
        memset(g_->cmp, 0, sizeof g_->cmp);                                                 \
        g_->rc_spec = rc_each ? (g_->episode / nrecs) % nrcspec : -1;                       \
        if (rc_rows_dir != NULL && nrcspec && g_->episode < nrecs)                          \
        {                                                                                    \
            g_->rc_rows = proc_malloc((size_t)g_->r->nrows * sizeof *g_->rc_rows);           \
            memset(g_->rc_rows, 0, (size_t)g_->r->nrows * sizeof *g_->rc_rows);              \
        }                                                                                    \
        if (g_->r->start == NULL)                                                            \
        {                                                                                    \
            double tl_ = now_s();                                                            \
            struct pool_start *s_ = pool_start_load(p, g_->r->dir, err, sizeof err);         \
            load_s += now_s() - tl_;                                                         \
            if (s_ == NULL) { printf("FAIL start: %s\n", err); return 1; }                   \
            if (!start_ready(g_->r, s_, &outside, &vdiff)) return 1;                         \
        }                                                                                    \
        one[0] = (id);                                                                       \
        double tr_ = now_s();                                                                \
        if (pool_reset(p, one, 1, g_->r->start, err, sizeof err)) { printf("FAIL reset: %s\n", err); return 1; } \
        reset_s += now_s() - tr_; ++nresets;                                                 \
        if (--g_->r->left == 0) { pool_start_free(g_->r->start); g_->r->start = NULL; }      \
        rowpos[(id)] = g_->r->tick;                                                          \
    } while (0)

    struct mirror *mirrors = calloc((size_t)n, sizeof *mirrors);
    int *ids = malloc((size_t)n * sizeof *ids);
    const struct pool_result **res = malloc((size_t)n * sizeof *res);
    int failed = 0;
    int64_t total_rows = 0, refused_steps = 0, ops_refused = 0;

    /* step env id from its row; 0 when its tape has no rows left */
    #define STEP(id) ({                                                                      \
        struct genv *g_ = &genvs[(id)];                                                      \
        int64_t t_ = rowpos[(id)], left_ = g_->r->nrows - t_;                                \
        int k_ = left_ < ticks ? (int)left_ : ticks;                                         \
        if (g_->r->steps != NULL)                                                            \
        {                                                                                    \
            k_ = 0;                                                                          \
            if (g_->step < g_->r->nsteps)                                                    \
            {                                                                                \
                k_ = g_->r->step_n[g_->step];                                                \
                one[0] = (id);                                                               \
                if (pool_step(p, one, 1, &g_->r->steps[g_->step], k_, 0)) { printf("FAIL step env %d\n", (id)); return 1; } \
                ++g_->step;                                                                  \
                ++live;                                                                      \
            }                                                                                \
        }                                                                                    \
        else if (k_ > 0)                                                                     \
        {                                                                                    \
            for (int q_ = 0; q_ < k_; ++q_) { acts[q_].kind = PA_ROW; acts[q_].row = g_->r->rows[t_ + q_]; } \
            one[0] = (id);                                                                   \
            if (pool_step(p, one, 1, acts, k_, 1)) { printf("FAIL step env %d\n", (id)); return 1; } \
            ++live;                                                                          \
        }                                                                                    \
        k_;                                                                                  \
    })

    double ts = now_s();
    for (int id = 0; id < n && next_episode < episodes; ++id)
    {
        START_EPISODE(id);
        STEP(id);
    }
    while (live > 0)
    {
        int got = pool_poll(p, pollb, res);
        live -= got;
        for (int k = 0; k < got; ++k)
        {
            const struct pool_result *r = res[k];
            int id = r->id;
            struct genv *g = &genvs[id];
            rowpos[id] = r->t;
            total_rows += r->ticks;
            /* a step Act.parseAgent refuses runs no tick in the oracle either */
            if ((r->flags & PF_ERROR) || ((r->flags & PF_REFUSED) && g->r->steps == NULL))
            {
                g->bad = 1;
                snprintf(g->why, sizeof g->why, "row %lld: the step stopped: %.900s", (long long)r->t, r->err);
            }
            if (r->flags & PF_REFUSED) ++refused_steps;
            if (feed && !g->bad)
            {
                char why[512];
                struct env *saved = neth_env;
                neth_env = pool_env(p, id);
                int ok = feed_check(&mirrors[id], r, pool_session(p, id), 4, why, sizeof why);
                neth_env = saved;
                if (!ok)
                {
                    g->bad = 1;
                    snprintf(g->why, sizeof g->why, "row %lld: feed: %.600s", (long long)r->t, why);
                }
            }
            ops_refused += r->ops_refused;
            int done = g->bad || rowpos[id] >= g->r->nrows || (g->r->steps != NULL && g->step >= g->r->nsteps);
            if (!done)
            {
                STEP(id);
                continue;
            }
            if (!g->bad && rowpos[id] != g->r->nrows)
            {
                g->bad = 1;
                snprintf(g->why, sizeof g->why, "the steps ended at row %lld, the tape at %lld", (long long)rowpos[id],
                         (long long)g->r->nrows);
            }
            /* the episode's end: its end state, then the verdict */
            if (!g->bad && g->r->ref_end != NULL)
            {
                struct session *ss = pool_session(p, id);
                struct env *saved = neth_env;
                neth_env = pool_env(p, id);
                char end[512];
                rowrec_end(ss, end, sizeof end);
                neth_env = saved;
                size_t len = strlen(end);
                if (len && end[len - 1] == '\n') end[len - 1] = 0;
                if (strcmp(end, g->r->ref_end))
                {
                    g->bad = 1;
                    snprintf(g->why, sizeof g->why, "end state: want %.400s got %.400s", g->r->ref_end, end);
                }
            }
            struct rec *rc = g->r;
            if (g->rc_rows != NULL)
            {
                char path[4400];
                const char *base = strrchr(rc->name, '/') ? strrchr(rc->name, '/') + 1 : rc->name;
                snprintf(path, sizeof path, "%s/%s.rc", rc_rows_dir, base);
                FILE *f = fopen(path, "w");
                if (f == NULL) { printf("FAIL %s: does not open\n", path); return 1; }
                for (int64_t t = rc->tick; t < rowpos[id]; ++t) fprintf(f, "%lld %016llx\n", (long long)t, (unsigned long long)g->rc_rows[t]);
                fclose(f);
                proc_free(g->rc_rows);
                g->rc_rows = NULL;
            }
            ++rc->episodes;
            for (int c = 0; c < CMP_N; ++c)
                rc->cmp[c] = c == CMP_P_MAXD ? (g->cmp[c] > rc->cmp[c] ? g->cmp[c] : rc->cmp[c]) : rc->cmp[c] + g->cmp[c];
            rc->rows_checked += g->checked;
            if (semcam_on)
            {
                rc->sem_casts += g->sem_casts;
                rc->sem_hits += g->sem_hits;
                rc->sem_rays += g->sem_rays;
                rc->sem_ref_ns += g->sem_ref_ns;
                rc->sem_fast_ns += g->sem_fast_ns;
                if (g->sem_bad && !rc->sem_bad++)
                    snprintf(rc->sem_first, sizeof rc->sem_first, "env %d episode %d: %.250s", id, g->episode, g->sem_why);
                /* a whole episode's casts: the same digest every time (a failed episode stopped early) */
                if (!g->bad)
                {
                    if (!rc->sem_have) rc->sem_digest = g->sem_digest, rc->sem_have = 1;
                    else if (rc->sem_digest != g->sem_digest) ++rc->sem_nondet;
                }
                if (g->sem_bad || rc->sem_nondet) failed = 1;
            }
            if (nrcspec)
            {
                rc->rc_casts += g->rc_casts;
                rc->rc_ref_ns += g->rc_ref_ns;
                rc->rc_fast_ns += g->rc_fast_ns;
                rc->rc_dev_casts += g->rc_dev_casts;
                rc->rc_dev_ns += g->rc_dev_ns;
                if (g->rc_bad && !rc->rc_bad++)
                    snprintf(rc->rc_first, sizeof rc->rc_first, "env %d episode %d: %.250s", id, g->episode, g->rc_why);
                if (!g->bad)
                {
                    int si = g->rc_spec >= 0 ? g->rc_spec : 0;
                    if (!rc->rc_have[si]) rc->rc_digest[si] = g->rc_digest, rc->rc_have[si] = 1;
                    else if (rc->rc_digest[si] != g->rc_digest) ++rc->rc_nondet;
                }
                if (g->rc_bad || rc->rc_nondet) failed = 1;
            }
            if (g->bad)
            {
                failed = 1;
                if (!rc->bad++)
                    snprintf(rc->first_bad, sizeof rc->first_bad, "env %d episode %d: %.980s", id, g->episode, g->why);
            }
            if (next_episode < episodes)
            {
                START_EPISODE(id);
                STEP(id);
            }
        }
    }
    double run_s = now_s() - ts;

    int64_t checked = 0;
    for (int i = 0; i < nrecs; ++i)
    {
        struct rec *r = &recs[i];
        checked += r->rows_checked;
        if (r->bad) printf("%s: FAIL %d of %d episodes (%lld rows); first: %s\n", r->name, r->bad, r->episodes,
                           (long long)r->rows_checked, r->first_bad);
        else printf("%s: OK %d episodes, %lld rows equal\n", r->name, r->episodes, (long long)r->rows_checked);
        if (r->episodes == 0) failed = 1;
        if (semcam_on)
            printf("%s: semcam %s: %lld casts, %lld rays, %.1f%% hits, digest %016llx%s, reference %.3f ms a cast, fast %.3f ms%s%s\n",
                   r->name, r->sem_bad || r->sem_nondet ? "FAIL" : "OK", (long long)r->sem_casts, (long long)r->sem_rays,
                   r->sem_rays ? 100.0 * (double)r->sem_hits / (double)r->sem_rays : 0.0, (unsigned long long)r->sem_digest,
                   r->sem_nondet ? " (an episode's digest differs)" : "",
                   r->sem_casts ? (double)r->sem_ref_ns / 1e6 / (double)r->sem_casts : 0.0,
                   r->sem_casts ? (double)r->sem_fast_ns / 1e6 / (double)r->sem_casts : 0.0, r->sem_bad ? "; first: " : "",
                   r->sem_bad ? r->sem_first : "");
        if (nrcspec)
        {
            /* the digests: one, or one a spec (--raycast-each), each from every episode that ran it */
            char dg[160] = "";
            for (int s = 0; s < (rc_each ? nrcspec : 1); ++s)
            {
                size_t l = strlen(dg);
                if (rc_each)
                    snprintf(dg + l, sizeof dg - l, "%s%dx%d%s %016llx", s ? ", " : "", rcspec[s].w, rcspec[s].h,
                             fp_tag(rcspec[s].fp), (unsigned long long)r->rc_digest[s]);
                else snprintf(dg + l, sizeof dg - l, "%016llx", (unsigned long long)r->rc_digest[0]);
            }
            char dv[96] = "";
            if (rc_device)
                snprintf(dv, sizeof dv, ", %lld device casts equal, %.3f ms each with its copies", (long long)r->rc_dev_casts,
                         r->rc_dev_casts ? (double)r->rc_dev_ns / 1e6 / (double)r->rc_dev_casts : 0.0);
            printf("%s: raycast %s: %lld casts, digest%s %s%s, reference %.3f ms a cast, fast %.3f ms%s%s%s\n", r->name,
                   r->rc_bad || r->rc_nondet ? "FAIL" : "OK", (long long)r->rc_casts, rc_each ? "s" : "", dg,
                   r->rc_nondet ? " (an episode's digest differs)" : "",
                   r->rc_casts ? (double)r->rc_ref_ns / 1e6 / (double)r->rc_casts : 0.0,
                   r->rc_casts ? (double)r->rc_fast_ns / 1e6 / (double)r->rc_casts : 0.0, dv, r->rc_bad ? "; first: " : "",
                   r->rc_bad ? r->rc_first : "");
        }
    }
    if (cast_cmp)
    {
        /* FP32 against FP64, a line a recording and the total (parts per million) */
        uint64_t T_[CMP_N] = {0};
        for (int i = 0; i <= nrecs; ++i)
        {
            const uint64_t *c = i < nrecs ? recs[i].cmp : T_;
            if (i < nrecs)
                for (int q = 0; q < CMP_N; ++q) T_[q] = q == CMP_P_MAXD ? (c[q] > T_[q] ? c[q] : T_[q]) : T_[q] + c[q];
            const double sr = c[CMP_S_RAYS] ? 1e6 / (double)c[CMP_S_RAYS] : 0.0, pp = c[CMP_P_PIX] ? 1e6 / (double)c[CMP_P_PIX] : 0.0;
            printf("cast-cmp %s: rays %llu, differ %.2f ppm (distance by 1/256 %.2f, by more %.2f; block or face %.2f: %llu rays, "
                   "a near tie on the way %llu, grazing %llu, edge %llu, far %llu, hit against miss %llu); pixels %llu, differ %.2f "
                   "ppm, bytes %llu, mean |delta| %.2f, max %llu\n",
                   i < nrecs ? recs[i].name : "total", (unsigned long long)c[CMP_S_RAYS], (double)c[CMP_S_DIFF] * sr,
                   (double)c[CMP_S_DIST1] * sr, (double)c[CMP_S_DISTN] * sr, (double)c[CMP_S_TOPO] * sr,
                   (unsigned long long)c[CMP_S_TOPO], (unsigned long long)c[CMP_S_NEARTIE],
                   (unsigned long long)c[CMP_S_GRAZE], (unsigned long long)c[CMP_S_EDGE], (unsigned long long)c[CMP_S_FAR],
                   (unsigned long long)c[CMP_S_MISSHIT], (unsigned long long)c[CMP_P_PIX], (double)c[CMP_P_DIFF] * pp,
                   (unsigned long long)c[CMP_P_BYTES], c[CMP_P_BYTES] ? (double)c[CMP_P_SUMD] / (double)c[CMP_P_BYTES] : 0.0,
                   (unsigned long long)c[CMP_P_MAXD]);
            const double rr = c[CMP_R_RAYS] ? 1e6 / (double)c[CMP_R_RAYS] : 0.0;
            printf("cast-cmp-rays %s: textured rays %llu, differ %.2f ppm (distance by 1/256 %.2f, by more %.2f; cell, face "
                   "or entity %.2f: %llu rays, %llu of another kind, in %llu of %llu casts)\n",
                   i < nrecs ? recs[i].name : "total", (unsigned long long)c[CMP_R_RAYS], (double)c[CMP_R_DIFF] * rr,
                   (double)c[CMP_R_DIST1] * rr, (double)c[CMP_R_DISTN] * rr, (double)c[CMP_R_TOPO] * rr,
                   (unsigned long long)c[CMP_R_TOPO], (unsigned long long)c[CMP_R_KIND],
                   (unsigned long long)c[CMP_R_TFRAMES], (unsigned long long)c[CMP_R_CASTS]);
        }
    }
    struct pool_stats st;
    pool_stats(p, &st);
    printf("pool_gate: %s: %d envs, %d threads, n %d, poll %d, %d recordings, %d episodes, %lld rows checked "
           "(%lld stepped); loads %.1f s, %d resets %.3f s (%.2f ms each), run %.1f s (%.0f rows/s), total %.1f s; "
           "%zu outside words, %zu value diffs; %s, %lld steps refused, %lld gui ops refused\n",
           failed ? "FAIL" : "OK", n, pool_threads(p), ticks, pollb, nrecs, next_episode, (long long)checked,
           (long long)total_rows, load_s, nresets, reset_s, nresets ? 1e3 * reset_s / nresets : 0.0, run_s,
           run_s > 0 ? (double)total_rows / run_s : 0.0, now_s() - t0, outside, vdiff,
           scripts ? "agent acts (the scripts)" : "tape rows", (long long)refused_steps, (long long)ops_refused);
    {
        struct gencache_stats gs;
        pool_gencache_stats(p, &gs);
        printf("pool_gate: generation cache: %llu chunks from it, %llu generated, %llu owed states from it, %llu held\n",
               (unsigned long long)gs.hits, (unsigned long long)gs.misses, (unsigned long long)gs.owed_hits,
               (unsigned long long)gs.entries);
    }
    if (gen != NULL)
    {
        struct pool_gen_stats g;
        pool_gen_stats(p, &g);
        printf("pool_gate: %s generation: %llu rounds for %llu steps, %llu requests, %llu generated, %llu from earlier rounds (largest round %llu), "
               "%llu dropped; ticks served %llu, by C %llu (%.2f%%), owed %llu/%llu; made ahead %llu, dropped unused %llu\n",
               gen->name, (unsigned long long)g.batches, (unsigned long long)g.steps, (unsigned long long)g.requests,
               (unsigned long long)g.unique, (unsigned long long)g.kept, (unsigned long long)g.max_unique, (unsigned long long)g.failed,
               (unsigned long long)g.hits, (unsigned long long)g.misses,
               g.hits + g.misses ? 100.0 * (double)g.hits / (double)(g.hits + g.misses) : 100.0,
               (unsigned long long)g.owed_hits, (unsigned long long)(g.owed_hits + g.owed_misses),
               (unsigned long long)g.generated, (unsigned long long)g.wasted);
    }
    if (feed && fstat.n)
    {
        qsort(fstat.sizes, fstat.nsizes, sizeof *fstat.sizes, cmp_u64);
        printf("feed: %llu feeds checked, bytes per step median %llu, p90 %llu, p99 %llu, mean %.0f, max %llu; "
               "%llu with chunk loads, mean %.0f, max %llu\n",
               (unsigned long long)fstat.n, (unsigned long long)fstat.sizes[fstat.nsizes / 2],
               (unsigned long long)fstat.sizes[fstat.nsizes * 9 / 10], (unsigned long long)fstat.sizes[fstat.nsizes * 99 / 100],
               (double)fstat.bytes / (double)fstat.n, (unsigned long long)fstat.max, (unsigned long long)fstat.bursts,
               fstat.bursts ? (double)fstat.burst_bytes / (double)fstat.bursts : 0.0, (unsigned long long)fstat.burst_max);
    }
    pool_free(p);
    ga_gen_free(gen);
    return failed;
}
