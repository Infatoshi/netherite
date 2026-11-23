/* strip_case (lane/strip2): the strip mine measured over a window in the vec env, no GPU. N envs from a start, one
 * episode of S steps each (a step is a tick: client.ticks_per_step 1) under an idle policy (--policy 0: the centre
 * bins, no key) or vec_bench's random one (--policy 1, its xorshift seeded by --seed: the sampler seed), the macros
 * --macros (default recipe,strip,food: 32767; add 16777216 for NETH_VEC_STRIP2). Read from each env's books one step
 * before the window ends (neth_vec_strip, neth_vec_obsidian: kept with every switch off too):
 * - the goods picked up an hour (coal, iron ore, gold ore, diamonds, redstone, cobblestone: the inventory's rises);
 * - deaths by what hurt last (lava, fire, fall, mob, drown, starve, other);
 * - where each env stopped: the strip's last walk into a new cell (the step, the place), and the envs still mining
 *   in the window's last --tail steps (a cell walked into there).
 *
 *   out/runtime/strip_case [--config configs/s64strip2.yaml] [--start DIR] [--ns 32] [--groups 2] [--threads T]
 *                          [--steps 36000] [--seed K] [--policy 0|1] [--macros BITS] [--tail 3000] [-v]
 *
 * The last line is "strip_case: N envs, ..." for a gate's grep. */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "vecenv.h"

#define SC_TICKS_HOUR 72000.0 /* 20 ticks a second */

static const char *const GOODS[6] = {"coal", "iron_ore", "gold_ore", "diamond", "redstone", "cobblestone"};
static const char *const CAUSES[7] = {"lava", "fire", "fall", "mob", "drown", "starve", "other"};

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + 1e-9 * (double)t.tv_nsec;
}

static uint64_t xs(uint64_t *s)
{
    uint64_t x = *s;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return *s = x;
}

static int coin(uint64_t *s, int p) { return (int)(xs(s) & 1023) < p; }

/* vec_bench's draw (trade_case's): 0 idle, 1 its random policy */
static void draw(uint64_t *s, int32_t *a, int policy)
{
    memset(a, 0, NETH_VEC_ACTION * sizeof *a);
    a[0] = a[1] = NETH_VEC_CAM / 2;
    if (policy == 0) return;
    for (int j = 0; j < 2; ++j) a[j] = coin(s, 512) ? 20 : 16 + (int)(xs(s) % 9);
    static const int p[14] = {700, 40, 60, 60, 200, 100, 20, 500, 80, 20, 10, 100, 0, 0};
    for (int j = 0; j < 14; ++j) a[2 + j] = coin(s, p[j]);
}

int main(int argc, char **argv)
{
    const char *config = "configs/s64strip2.yaml", *start = NULL, *mopts = NULL;
    int n = 32, groups = 2, threads = 0, steps = 36000, policy = 1, verbose = 0, macros = 32767, tail = 3000;
    uint64_t seed = 1;
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--config") && i + 1 < argc) config = argv[++i];
        else if (!strcmp(argv[i], "--start") && i + 1 < argc) start = argv[++i];
        else if (!strcmp(argv[i], "--ns") && i + 1 < argc) n = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--groups") && i + 1 < argc) groups = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--threads") && i + 1 < argc) threads = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--steps") && i + 1 < argc) steps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--policy") && i + 1 < argc) policy = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--macros") && i + 1 < argc) macros = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--tail") && i + 1 < argc) tail = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--mopts") && i + 1 < argc) mopts = argv[++i];   /* lane/diamond: vec_bench's */
        else if (!strcmp(argv[i], "-v")) verbose = 1;
        else
        {
            fprintf(stderr, "usage: strip_case [--config F] [--start DIR] [--ns N] [--groups G] [--threads T] "
                            "[--steps S] [--seed K] [--policy 0|1] [--macros BITS] [--tail T] [--mopts SPEC] [-v]\n");
            return 2;
        }
    }
    if (n <= 0 || groups <= 0 || n % groups || steps < 2)
    {
        printf("FAIL: --ns a multiple of --groups, --steps 2 or more\n");
        return 1;
    }
    struct neth_vec_opts o;
    memset(&o, 0, sizeof o);
    o.env.config = config;
    o.env.start = start;
    o.env.n = n;
    o.env.threads = threads;
    o.env.device_mesh = -1;
    o.groups = groups;
    o.ep_len = steps;                   /* one episode: the books are read a step before it ends */
    o.macros = macros;
    o.macro_opts = mopts;
    o.objective = NETH_VEC_FULL;
    char err[1024];
    double t0 = now_s();
    struct neth_vec *v = neth_vec_make(&o, err, sizeof err);
    if (v == NULL)
    {
        printf("FAIL: %s\n", err);
        return 1;
    }
    struct neth_vec_bufs b;
    memset(&b, 0, sizeof b);
    neth_vec_buffers(v, &b);
    int aw = neth_vec_action_width(v), gs = n / groups;
    uint64_t *rng = malloc((size_t)n * sizeof *rng);
    for (int i = 0; i < n; ++i) rng[i] = (seed * 0x9e3779b97f4a7c15ull) ^ ((uint64_t)(i + 1) * 0xbf58476d1ce4e5b9ull);
    int *gcount = calloc((size_t)groups, sizeof *gcount), live = groups;
    struct neth_vec_strip *sb = calloc((size_t)n, sizeof *sb);
    struct neth_vec_obsidian *ob = calloc((size_t)n, sizeof *ob);
    while (live > 0)
    {
        int g = neth_vec_recv(v, err, sizeof err);
        if (g < 0)
        {
            printf("FAIL: %s\n", err);
            return 1;
        }
        struct neth_vec_episode got[64];
        while (neth_vec_episodes(v, got, 64) > 0) {}
        /* the episode's steps count its reset's: at the steps - 1'th receive its last act is still to come, and the
         * books are the episode's before the reset clears them */
        if (++gcount[g] == steps - 1)
            for (int i = g * gs; i < (g + 1) * gs; ++i)
            {
                neth_vec_strip(v, i, &sb[i]);
                neth_vec_obsidian(v, i, &ob[i]);
            }
        if (gcount[g] > steps)
        {
            --live;
            continue;
        }
        for (int i = g * gs; i < (g + 1) * gs; ++i)
        {
            draw(&rng[i], b.actions + (size_t)i * aw, policy);
            if (aw > NETH_VEC_ACTION) b.actions[(size_t)i * aw + NETH_VEC_ACTION] = 0;
        }
        if (neth_vec_send(v, g))
        {
            printf("FAIL: send %d refused\n", g);
            return 1;
        }
    }
    double wall = now_s() - t0;
    long got[6] = {0}, cells = 0, deaths = 0, dead = 0, cause[7] = {0}, first[7] = {0}, mining = 0, done = 0, sst = 0;
    double hours = 0, lastsum = 0;
    /* lane/strip3: the envs that held 3 diamonds at some step (picked up 3+), those that made the diamond pickaxe, the
     * wood errands begun and the surfaces reached, and where each env stopped (mining, died, the pickaxe made, 3+
     * diamonds and no pickaxe, other) */
    long d3 = 0, d3p = 0, werr = 0, wtop = 0, wmade = 0, w_mine = 0, w_dead = 0, w_dp = 0, w_d3 = 0, w_oth = 0;
    for (int i = 0; i < n; ++i)
    {
        const struct neth_vec_strip *q = &sb[i];
        int live_tail = q->last >= 0 && q->last > q->steps - tail;
        d3 += q->got[3] >= 3;
        d3p += q->got[3] >= 3 && q->dpick_step >= 0;
        werr += q->errand[0] > 0;
        wtop += q->errand[1] > 0;
        wmade += q->errand[3] > 0;
        if (live_tail) w_mine++;
        else if (ob[i].deaths > 0) w_dead++;
        else if (q->dpick > 0) w_dp++;
        else if (q->held[0] >= 3) w_d3++;
        else w_oth++;
    }
    for (int i = 0; i < n; ++i)
    {
        const struct neth_vec_strip *q = &sb[i];
        hours += (double)q->steps / SC_TICKS_HOUR;
        for (int k = 0; k < 6; ++k) got[k] += q->got[k];
        cells += q->cells;
        sst += q->strip_steps;
        deaths += ob[i].deaths;
        dead += ob[i].deaths > 0;
        if (ob[i].deaths > 0 && ob[i].d_cause >= 0 && ob[i].d_cause < 7) first[ob[i].d_cause] += 1;
        for (int k = 0; k < 7; ++k) cause[k] += ob[i].cause[k];
        int live_tail = q->last >= 0 && q->last > q->steps - tail;
        mining += live_tail;
        done += !live_tail && q->dpick > 0 && ob[i].deaths == 0;   /* the strip's last want met, alive */
        lastsum += q->last >= 0 ? (double)q->last : 0.0;
        if (verbose)
        {
            printf("env %2d steps %lld cells %4d last %6lld at %.1f %.1f %.1f now %.1f %.1f %.1f dim %d strip %5.1f%% "
                   "deaths %d",
                   i, (long long)q->steps, q->cells, (long long)q->last, q->lpos[0], q->lpos[1], q->lpos[2], q->pos[0],
                   q->pos[1], q->pos[2], q->dim, q->steps ? 100.0 * (double)q->strip_steps / (double)q->steps : 0.0,
                   ob[i].deaths);
            for (int k = 0; k < 7; ++k)
                if (ob[i].cause[k]) printf(" %s %d", CAUSES[k], ob[i].cause[k]);
            if (ob[i].deaths > 0)
                printf(" (first: %s at step %lld, %.1f %.1f %.1f, who %d)",
                       ob[i].d_cause >= 0 && ob[i].d_cause < 7 ? CAUSES[ob[i].d_cause] : "?", (long long)ob[i].d_step,
                       ob[i].d_pos[0], ob[i].d_pos[1], ob[i].d_pos[2], ob[i].d_who);
            for (int k = 0; k < 6; ++k) printf(" %s %d", GOODS[k], q->got[k]);
            printf(" diamond_pickaxe %d%s", q->dpick, live_tail ? " MINING" : "");
            /* lane/strip3: what the last step held, the table, the errand's books, the climb out */
            printf(" | held dia %d planks %d sticks %d logs %d tables %d ingots %d tier %d table %d dpick_at %lld "
                   "wood %d/%d/%d/%d out %lld\n",
                   q->held[0], q->held[1], q->held[2], q->held[3], q->held[4], q->held[5], q->held[6], q->table,
                   (long long)q->dpick_step, q->errand[0], q->errand[1], q->errand[2], q->errand[3],
                   (long long)q->out_steps);
        }
    }
    printf("goods an hour:");
    for (int k = 0; k < 6; ++k) printf(" %s %.1f (%ld)", GOODS[k], hours ? got[k] / hours : 0.0, got[k]);
    printf("\ndeaths by cause:");
    for (int k = 0; k < 7; ++k) printf(" %s %ld", CAUSES[k], cause[k]);
    printf("\nfirst deaths by cause:");
    for (int k = 0; k < 7; ++k) printf(" %s %ld", CAUSES[k], first[k]);
    printf("\nthree diamonds: %ld envs picked up 3+, %ld of them made the diamond pickaxe; wood errands %ld envs, the "
           "surface %ld, a pickaxe after one %ld\n",
           d3, d3p, werr, wtop, wmade);
    printf("where: mining %ld, died %ld, diamond pickaxe %ld, 3+ diamonds held no pickaxe %ld, other %ld",
           w_mine, w_dead, w_dp, w_d3, w_oth);
    /* lane/diamond: the link's books (struct neth_vec_strip dia), summed over the envs */
    long nopick = 0, pil = 0, pilq = 0, broke = 0, swims = 0;
    for (int i = 0; i < n; ++i)
    {
        nopick += sb[i].dia[3] > 0;
        broke += sb[i].dia[9];
        pil += sb[i].dia[16];
        pilq += sb[i].dia[17];
        swims += sb[i].dia[18];
    }
    printf("\nthe link (from flint and steel held): envs ever without a pickaxe %ld, pickaxes broken %ld; climb_way "
           "pillar levels %ld, give-ups %ld; climb_swim swims %ld", nopick, broke, pil, pilq, swims);
    printf("\nstrip_case: %d envs, %.2f env-hours, mining in the last %d steps %ld of %d (and %ld alive with the want met: "
           "a diamond pickaxe), last cell walked at step %.0f (mean), cells %ld (%.1f an hour), strip steps %.1f%%, died "
           "%ld (deaths %ld), coal %.1f, iron ore %.1f, diamonds %.1f an hour, %.0f env-steps/s\n",
           n, hours, tail, mining, n, done, n ? lastsum / n : 0.0, cells, hours ? cells / hours : 0.0,
           hours ? 100.0 * (double)sst / (hours * SC_TICKS_HOUR) : 0.0, dead, deaths, hours ? got[0] / hours : 0.0,
           hours ? got[1] / hours : 0.0, hours ? got[3] / hours : 0.0, (double)n * steps / wall);
    neth_vec_free(v);
    return 0;
}
