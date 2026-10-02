/* The env pool's gate: K recordings (tapes with their start snapshots)
 * stepped in one pool of N envs, recordings repeated and interleaved, each
 * env's every row held to test_snapshots' own record of that row
 * (csrc/tests/rowrec.h: every field test_snapshots compares; made by
 * test_snapshots --row-dump into REFDIR/NAME.rows), and after each tape's
 * last row the end state.
 *
 *   pool_gate [-n N] [-j THREADS] [--ticks n] [--episodes E] [--poll B]
 *             [--snapdir DIR] [--refdir DIR] [--agent SCRIPTS] [--feed]
 *             [--sparse K] [--world|--config CONF] [--gen c|cuda] NAME...
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
 * generators: ga_gen_cuda_local_create).
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
};

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
static void semcam_check(struct genv *g, struct session *ss, int64_t t)
{
    static _Thread_local struct semcam_cell *a, *b;
    static _Thread_local size_t cap;
    size_t n = (size_t)semspec.w * (size_t)semspec.h;
    if (cap < n)
    {
        free(a);
        free(b);
        a = malloc(n * sizeof *a);
        b = malloc(n * sizeof *b);
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
static void gate_hook(void *ctx, int id, struct session *ss, int64_t t)
{
    (void)ctx;
    struct genv *g = &genvs[id];
    struct rec *r = g->r;
    if (semcam_on) semcam_check(g, ss, t);
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
        else if (!strcmp(argv[i], "--semcam") && i + 1 < argc)
        {
            const char *v = argv[++i];
            semspec.range = 64;
            if (sscanf(v, "%dx%d:%d", &semspec.w, &semspec.h, &semspec.range) < 2 || semspec.w < 1 || semspec.h < 1 ||
                semspec.range < 1)
            {
                fprintf(stderr, "pool_gate: --semcam WxH[:RANGE]\n");
                return 2;
            }
            semcam_on = 1;
        }
        else if (argv[i][0] == '-') { fprintf(stderr, "pool_gate: unknown flag %s\n", argv[i]); return 2; }
        else names[nnames++] = argv[i];
    }
    if (nnames == 0 || n < 1 || ticks < 1)
    {
        fprintf(stderr, "usage: pool_gate [-n N] [-j THREADS] [--ticks n] [--episodes E] [--poll B] [--snapdir DIR] [--refdir DIR] NAME...\n");
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
    cfg.privileged = 1;
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
                struct env *saved = nw_env;
                nw_env = pool_env(p, id);
                int ok = feed_check(&mirrors[id], r, pool_session(p, id), 4, why, sizeof why);
                nw_env = saved;
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
                struct env *saved = nw_env;
                nw_env = pool_env(p, id);
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
