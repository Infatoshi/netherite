/* The env pool's throughput: env-steps per second and user instructions per
 * env-step, per-env memory, reset time and render feed size, for a few
 * start kinds, as the RL product would step them: n ticks a step of an agent
 * policy (a seeded random walk: forward, sprint, jump, attack, look drift),
 * the feed on, envs handed back B = N/4 at a time.
 *
 *   pool_bench [--kinds village,nether,explore] [--ns 1,16,64,256] [--ticks 4]
 *              [--steps S] [-j THREADS] [--profile DIR]
 *              [--rungs micro,exact] [--print-rungs] [--resets R]
 *
 * --profile DIR: the phase profiler inside the pool (envs pinned to
 * workers) at N = 16, each kind's env 0 summary into DIR/KIND.txt.
 *
 * --rungs LIST (fidelity.rung, env.h neth_rung; env i takes LIST[i mod its
 * length]): a probe in place of the throughput run -- reset, the rungs it
 * applied, 2 ticks of one act with every env's rows and end state against
 * env 0's, then --resets used-env resets (the rung array applied again).
 * --print-rungs: each env's rung after a reset. --resets R: the used-env
 * resets (default 2). */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../engine/dev.h"
#include "../engine/env.h"
#include "../tests/rowrec.h"
#include "pool.h"

struct kind {
    const char *name, *dir;
};
static const struct kind KINDS[] = {
    {"village", "out/java/snapshots/cov-villagerep-s42"},   /* a village, seed 42 */
    {"nether", "out/java/snapshots/s1chain-S8"},           /* the Nether fortress of a whole game */
    {"explore", "out/java/snapshots/explore-long-s1"},     /* seed 1's spawn, walking out */
};

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

/* the policy: a random walk, mostly forward (explore: always, sprinting) */
static void policy(struct pool_act *a, uint64_t *s, int explore)
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
    a->look[0] = (float)((int)(r >> 16 & 255) - 128) * (explore ? 0.02F : 0.1F);
    a->look[1] = (float)((int)(r >> 24 & 63) - 32) * 0.05F;
    if ((r >> 32 & 63) == 0) a->hotbar = (int)(r >> 40 & 7);
}

static int cmp_d(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

/* --rungs' probe: 2 ticks from the reset, every env's row records (the
 * gate's text and end state, rowrec.h) against env 0's */
#define PROBE_TICKS 2
#define PROBE_ROWS (PROBE_TICKS + 1)   /* the ticks' rows, then the end state */
#define PROBE_ROW 16384

struct probe {
    int n;
    int64_t t0;
    char *rows;                      /* n * PROBE_ROWS records of PROBE_ROW bytes */
};

static void probe_hook(void *ctx, int id, struct session *ss, int64_t t, int last)
{
    struct probe *c = ctx;
    if (id < 0 || id >= c->n) return;
    char *base = c->rows + (size_t)id * PROBE_ROWS * PROBE_ROW;
    if (t >= c->t0 && t - c->t0 < PROBE_TICKS) rowrec_line(ss, t, 0, base + (size_t)(t - c->t0) * PROBE_ROW, PROBE_ROW);
    if (last) rowrec_end(ss, base + (size_t)PROBE_TICKS * PROBE_ROW, PROBE_ROW);
}

/* "micro,exact" into out: how many, or -1 for a name that is not a rung */
static int parse_rungs(const char *list, int *out, int max)
{
    char buf[256];
    snprintf(buf, sizeof buf, "%s", list);
    int k = 0;
    for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ","))
    {
        int r = neth_rung_parse(tok);
        if (r == NETH_RUNG_UNSET || k == max) return -1;
        out[k++] = r;
    }
    return k;
}

/* Reset n envs (env i at rungs[i % nr]), print the rungs they run, run 2
 * ticks of one act and hold every env's rows to env 0's, then `resets`
 * used-env resets with the rungs printed again (the array is applied after
 * the image copy: one start image serves every rung). 0, or -1 with err. */
static int probe_run(struct pool *p, struct pool_start *st, int n, const int *rungs, int nr, int print, int resets,
                     struct probe *pb, char *err, size_t en)
{
    int *ids = malloc((size_t)n * sizeof *ids);
    char *rows = calloc((size_t)n * PROBE_ROWS, PROBE_ROW);
    if (ids == NULL || rows == NULL)
    {
        free(ids);
        free(rows);
        snprintf(err, en, "out of memory");
        return -1;
    }
    memset(pb, 0, sizeof *pb);
    pb->n = n;
    pb->rows = rows;
    pb->t0 = pool_start_tick(st);
    for (int i = 0; i < n; ++i)
    {
        ids[i] = i;
        if (nr > 0 && pool_rung_set(p, i, rungs[i % nr]))
        {
            snprintf(err, en, "env %d takes no rung", i);
            free(ids);
            free(rows);
            return -1;
        }
    }
    if (pool_reset(p, ids, n, st, err, en)) goto fail;
    if (print)
        for (int i = 0; i < n; ++i) printf("env%d %s\n", i, neth_rung_name(env_rung_of(pool_env(p, i))));
    {
        struct pool_act a;
        memset(&a, 0, sizeof a);
        a.hotbar = -1;
        for (int i = 0; i < n; ++i)
            if (pool_step(p, &ids[i], 1, &a, PROBE_TICKS, 0)) goto fail;
        const struct pool_result *res[256];
        int left = n;
        while (left > 0)
        {
            int b = n < 256 ? n : 256;
            int got = pool_poll(p, b < left ? b : left, res);
            if (got <= 0) break;
            left -= got;
        }
        if (left > 0)
        {
            snprintf(err, en, "%d envs never finished", left);
            goto fail;
        }
    }
    {
        int diffs = 0;
        for (int i = 1; i < n; ++i)
            for (int k = 0; k < PROBE_ROWS; ++k)
            {
                const char *want = rows + (size_t)k * PROBE_ROW;
                const char *got = rows + ((size_t)i * PROBE_ROWS + (size_t)k) * PROBE_ROW;
                if (!strcmp(want, got)) continue;
                if (diffs++ == 0)
                    printf("%s: env%d %s differs from env0 %s\n",
                           k < PROBE_TICKS ? "row" : "end", i, neth_rung_name(env_rung_of(pool_env(p, i))),
                           neth_rung_name(env_rung_of(pool_env(p, 0))));
            }
        printf("rows: %d ticks x %d envs + the end state, %d diffs\n", PROBE_TICKS, n, diffs);
    }
    for (int r = 0; r < resets; ++r)
    {
        if (pool_reset(p, ids, n, st, err, en)) goto fail;
        if (print)
        {
            printf("reset %d:", r + 1);
            for (int i = 0; i < n; ++i)
                printf(" env%d %s", i, neth_rung_name(env_rung_of(pool_env(p, i))));
            printf("\n");
        }
    }
    free(ids);
    free(rows);
    return 0;
fail:
    free(ids);
    free(rows);
    return -1;
}

int main(int argc, char **argv)
{
    char kinds[256] = "village,nether,explore", ns[256] = "1,16,64,256";
    int ticks = 4, steps = 150, threads = 0;
    int print_rungs = 0, resets = 2, nr = 0, rungs[16];
    const char *profile = NULL;
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--kinds") && i + 1 < argc) snprintf(kinds, sizeof kinds, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--ns") && i + 1 < argc) snprintf(ns, sizeof ns, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--ticks") && i + 1 < argc) ticks = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--steps") && i + 1 < argc) steps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-j") && i + 1 < argc) threads = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--profile") && i + 1 < argc) profile = argv[++i];
        else if (!strcmp(argv[i], "--rungs") && i + 1 < argc)
        {
            nr = parse_rungs(argv[++i], rungs, 16);
            if (nr < 1) { fprintf(stderr, "pool_bench: --rungs takes exact, stripped or micro, not %s\n", argv[i]); return 2; }
        }
        else if (!strcmp(argv[i], "--print-rungs")) print_rungs = 1;
        else if (!strcmp(argv[i], "--resets") && i + 1 < argc) resets = atoi(argv[++i]);
        else { fprintf(stderr, "pool_bench: unknown argument %s\n", argv[i]); return 2; }
    }
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    if (nr == 0)
        printf("kind      N  thr   env-steps/s  ticks/s   M instr/step (steady)  us/step p50 p99  env MB mean max  feed KB mean max\n");
    for (size_t k = 0; k < sizeof KINDS / sizeof KINDS[0]; ++k)
    {
        if (!strstr(kinds, KINDS[k].name)) continue;
        char list[256];
        snprintf(list, sizeof list, "%s", ns);
        for (char *tok = strtok(list, ","); tok; tok = strtok(NULL, ","))
        {
            int n = atoi(tok);
            int thr = threads > 0 ? threads : (n < ncpu ? n : (int)ncpu);
            int prof = profile != NULL && n == 16;
            struct pool_config cfg = {0};
            cfg.n = n;
            cfg.threads = thr;
            cfg.obs.ticks = ticks;
            cfg.feed = 1;
            cfg.dev_apply = dev_apply;
            cfg.dev_end = dev_end;
            cfg.pin = prof;
            cfg.profile = prof;
            struct probe pb;
            memset(&pb, 0, sizeof pb);
            if (nr > 0)
            {
                cfg.tick_hook = probe_hook;
                cfg.tick_hook_ctx = &pb;
            }
            char err[512];
            struct pool *p = pool_make(&cfg, err, sizeof err);
            if (!p) { printf("FAIL %s N %d: %s\n", KINDS[k].name, n, err); return 1; }
            struct pool_start *st = pool_start_load(p, KINDS[k].dir, err, sizeof err);
            if (!st) { printf("FAIL %s: %s\n", KINDS[k].name, err); return 1; }
            if (nr > 0)
            {
                printf("probe %s N %d\n", KINDS[k].name, n);
                int rc = probe_run(p, st, n, rungs, nr, print_rungs, resets, &pb, err, sizeof err);
                fflush(stdout);
                pool_free(p);
                pool_start_free(st);
                if (rc) { printf("FAIL probe %s: %s\n", KINDS[k].name, err); return 1; }
                continue;
            }
            int *ids = malloc((size_t)n * sizeof *ids);
            for (int i = 0; i < n; ++i) ids[i] = i;
            struct pool_stats s0;
            pool_stats(p, &s0);
            double tr = now_s();
            if (pool_reset(p, ids, n, st, err, sizeof err)) { printf("FAIL reset: %s\n", err); return 1; }
            double reset_s = now_s() - tr;

            uint64_t *seed = malloc((size_t)n * sizeof *seed);
            int *done = calloc((size_t)n, sizeof *done);
            struct pool_act act;
            for (int i = 0; i < n; ++i)
            {
                seed[i] = 0x9e3779b97f4a7c15ull * (uint64_t)(i + 1) + k;
                policy(&act, &seed[i], k == 2);
                pool_step(p, &ids[i], 1, &act, ticks, 0);
            }
            int live = n, b = n / 4 > 0 ? n / 4 : 1;
            const struct pool_result **res = malloc((size_t)n * sizeof *res);
            size_t cap = (size_t)n * steps, nrec = 0;
            double *us = malloc(cap * sizeof *us);
            uint64_t ins_all = 0, ins_steady = 0, steady = 0, feed_bytes = 0, feed_max = 0, errors = 0;
            double t0 = now_s();
            while (live > 0)
            {
                int got = pool_poll(p, b, res);
                live -= got;
                for (int q = 0; q < got; ++q)
                {
                    const struct pool_result *r = res[q];
                    int id = r->id;
                    if (r->flags & PF_ERROR) ++errors;
                    ins_all += r->instructions;
                    if (done[id] >= 5) { ins_steady += r->instructions; ++steady; }
                    if (nrec < cap) us[nrec++] = (double)r->ns / 1e3;
                    feed_bytes += r->feed_bytes;
                    if (r->feed_bytes > feed_max) feed_max = r->feed_bytes;
                    if (++done[id] < steps && !(r->flags & PF_ERROR))
                    {
                        /* a dead player respawns, as the agent's gui op would */
                        policy(&act, &seed[id], k == 2);
                        if (r->flags & PF_DEAD) { act.nops = 1; act.ops[0].kind = PG_RESPAWN; }
                        pool_step(p, &ids[id], 1, &act, ticks, 0);
                        ++live;
                    }
                }
            }
            double wall = now_s() - t0;
            uint64_t total = 0;
            for (int i = 0; i < n; ++i) total += (uint64_t)done[i];
            qsort(us, nrec, sizeof *us, cmp_d);
            double mem = 0, memmax = 0;
            for (int i = 0; i < n; ++i)
            {
                double m = (double)pool_env_resident(p, i) / 1048576.0;
                mem += m;
                if (m > memmax) memmax = m;
            }
            struct pool_stats s1;
            pool_stats(p, &s1);
            printf("%-8s %4d %4d %12.0f %8.0f %10.2f (%7.2f) %9.0f %8.0f %8.1f %6.1f %8.1f %8.1f%s\n", KINDS[k].name, n, thr,
                   (double)total / wall, (double)total * ticks / wall, (double)ins_all / 1e6 / (double)total,
                   steady ? (double)ins_steady / 1e6 / (double)steady : 0.0, nrec ? us[nrec / 2] : 0.0,
                   nrec ? us[nrec * 99 / 100] : 0.0, mem / n, memmax,
                   (double)feed_bytes / 1024.0 / (double)total, (double)feed_max / 1024.0,
                   errors ? " (engine errors: see stderr)" : "");
            printf("         reset: %d envs in %.3f s on %d threads (%.2f ms each), %.1f M instructions each\n", n, reset_s, thr,
                   (double)(s1.reset_ns - s0.reset_ns) / 1e6 / (double)(s1.resets - s0.resets ? s1.resets - s0.resets : 1),
                   (double)(s1.reset_instructions - s0.reset_instructions) / 1e6 / (double)(s1.resets - s0.resets ? s1.resets - s0.resets : 1));
            fflush(stdout);
            /* the profile before the resets below turn it off */
            if (prof)
            {
                char path[4200];
                snprintf(path, sizeof path, "%s/%s.txt", profile, KINDS[k].name);
                FILE *f = fopen(path, "w");
                if (f) { pool_profile_summary(p, 0, f, NULL); fclose(f); }
            }
            /* a reused env's reset (the RL case): every env again, --resets times */
            for (int rep = 0; rep < resets; ++rep)
            {
                struct pool_stats r0, r1;
                pool_stats(p, &r0);
                double t1 = now_s();
                if (pool_reset(p, ids, n, st, err, sizeof err)) { printf("FAIL reset: %s\n", err); return 1; }
                double wall_r = now_s() - t1;
                pool_stats(p, &r1);
                uint64_t k2 = r1.resets - r0.resets ? r1.resets - r0.resets : 1;
                printf("         reset of a used env (%d): %d envs in %.3f s (%.2f ms each on its worker), %.1f M instructions each\n",
                       rep + 1, n, wall_r, (double)(r1.reset_ns - r0.reset_ns) / 1e6 / (double)k2,
                       (double)(r1.reset_instructions - r0.reset_instructions) / 1e6 / (double)k2);
            }
            fflush(stdout);
            free(ids);
            free(seed);
            free(done);
            free(res);
            free(us);
            pool_free(p);
            pool_start_free(st);
        }
    }
    return 0;
}
