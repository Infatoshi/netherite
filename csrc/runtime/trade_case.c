/* trade_case (lane/eyetrade): the emerald route's input measured in the vec env, no Python. N envs from a start, one
 * episode of S steps each (a step is a tick: client.ticks_per_step 1) under an idle policy (the centre bins, no key)
 * or vec_bench's random one (--policy 1, its xorshift seeded by --seed: the sampler seed), the macros --macros (default
 * recipe,strip,food: 16383 + 16384), and what the episode's end holds (neth_vec_episode items and extra: coal,
 * cobblestone, iron ore, diamonds, emeralds are not in semteach.ITEMS so they are not counted here), the deaths, the
 * cells explored; then each good an hour (72,000 ticks) and the emeralds an hour it buys at a villager's price
 * (EntityVillager villagersSellingList: coal 16 to 23 a emerald, mean 19.5).
 *
 *   out/runtime/trade_case [--config configs/s64van.yaml] [--start DIR] [--ns 32] [--groups 2] [--threads T]
 *                          [--steps 36000] [--seed K] [--policy 0|1] [--macros BITS] [-v]
 *
 * The last line is "trade_case: N envs, ..." for a gate's grep. */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "vecenv.h"

#define TC_TICKS_HOUR 72000.0 /* 20 ticks a second */
#define TC_COAL_PRICE 19.5    /* villagersSellingList coal (16, 24): 16 + nextInt(8) */

/* semteach.ITEMS' indices (vecenv.c TEACH_ITEMS) */
enum { TI_COBBLE = 9, TI_COAL = 14, TI_IRON_ORE = 15 };

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

/* vec_bench's draw (obsidian_case's): 0 idle, 1 its random policy */
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
    const char *config = "configs/s64van.yaml", *start = NULL;
    int n = 32, groups = 2, threads = 0, steps = 36000, policy = 0, verbose = 0, macros = 16383 | 16384;
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
        else if (!strcmp(argv[i], "-v")) verbose = 1;
        else
        {
            fprintf(stderr, "usage: trade_case [--config F] [--start DIR] [--ns N] [--groups G] [--threads T] "
                            "[--steps S] [--seed K] [--policy 0|1] [--macros BITS] [-v]\n");
            return 2;
        }
    }
    struct neth_vec_opts o;
    memset(&o, 0, sizeof o);
    o.env.config = config;
    o.env.start = start;
    o.env.n = n;
    o.env.threads = threads;
    o.env.device_mesh = -1;
    o.groups = groups;
    o.ep_len = steps;                   /* one episode: it ends at the window's last step and is reported */
    o.macros = macros;
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
    struct neth_vec_episode *ep = calloc((size_t)n * 4, sizeof *ep), got[64];
    int nep = 0;
    while (live > 0)
    {
        int g = neth_vec_recv(v, err, sizeof err);
        if (g < 0)
        {
            printf("FAIL: %s\n", err);
            return 1;
        }
        int k;
        while ((k = neth_vec_episodes(v, got, 64)) > 0)
            for (int j = 0; j < k && nep < n * 4; ++j) ep[nep++] = got[j];
        if (++gcount[g] > steps + 1)
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
    /* the first episode an env ends (the window's): later ones are the reset's tail */
    char *seen = calloc((size_t)n, 1);
    long coal = 0, cobble = 0, iron = 0, deaths = 0, dead = 0, cells = 0, envs = 0, dia_pick = 0;
    int64_t st = 0;
    for (int e = 0; e < nep; ++e)
    {
        const struct neth_vec_episode *q = &ep[e];
        if (q->env < 0 || q->env >= n || seen[q->env]) continue;
        seen[q->env] = 1;
        ++envs;
        coal += q->items[TI_COAL];
        cobble += q->items[TI_COBBLE];
        iron += q->items[TI_IRON_ORE];
        deaths += q->deaths;
        dead += q->deaths > 0;
        cells += q->cells;
        dia_pick += q->extra[4];
        st += q->steps;
        if (verbose)
            printf("env %d steps %lld coal %d cobblestone %d iron_ore %d deaths %d cells %d\n", q->env,
                   (long long)q->steps, q->items[TI_COAL], q->items[TI_COBBLE], q->items[TI_IRON_ORE], q->deaths,
                   q->cells);
    }
    double hours = envs ? (double)st / TC_TICKS_HOUR : 0;
    printf("trade_case: %d envs, %ld episodes read, %.2f env-hours of game time, coal %ld (%.1f an hour, %.2f emeralds an "
           "hour at %.1f coal a emerald), cobblestone %ld (%.1f an hour), iron ore %ld, diamond pickaxes %ld, died %ld "
           "(deaths %ld, %.2f an env-hour), cells %ld, %.0f env-steps/s\n",
           n, envs, hours, coal, hours ? coal / hours : 0.0, hours ? coal / hours / TC_COAL_PRICE : 0.0, TC_COAL_PRICE,
           cobble, hours ? cobble / hours : 0.0, iron, dia_pick, dead, deaths, hours ? deaths / hours : 0.0, cells,
           (double)n * steps / wall);
    neth_vec_free(v);
    return 0;
}
