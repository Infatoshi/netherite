/* sh_check (lane/stronghold): NETH_VEC_STRONGHOLD's case. N envs from a start (a dev start: the kit given at its
 * first tick, NETH_RL_DEV), one episode each of S steps under a seeded stand-in policy (vec_bench's: --policy 1 random
 * keys and camera, 0 idle), the macros' bits given; then each env's books (vecenv.h neth_vec_sh_book) and episode
 * record, one line an env, and the case's table: how many reached each step of the way (the macro started, stone
 * brick seen, a frame seen, in the room, the portal lit, the End) and their median step, eyes left, throws, deaths by
 * cause, env-steps/s.
 *
 *   out/runtime/sh_check --config configs/s64stronghold.yaml --start DIR --dev --ns 32 --groups 2 --threads 16
 *                        --steps 36000 --seed 1 [--policy 1] [--macros BITS]
 *
 * Exit 0 when every env played its episode (the numbers are the result, not a pass/fail). */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rlgui.h"
#include "vecenv.h"

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + 1e-9 * (double)t.tv_nsec;
}

static uint64_t xs(uint64_t *s)
{
    uint64_t x = *s;
    x ^= x << 13, x ^= x >> 7, x ^= x << 17;
    return *s = x;
}

static int coin(uint64_t *s, int per1024) { return (int)(xs(s) % 1024) < per1024; }

/* vec_bench's stand-in policy */
static void draw(uint64_t *s, int32_t *a, int policy)
{
    memset(a, 0, NETH_VEC_ACTION * sizeof *a);
    a[0] = a[1] = NETH_VEC_CAM / 2;
    if (policy == 0) return;
    for (int j = 0; j < 2; ++j) a[j] = coin(s, 512) ? 20 : 16 + (int)(xs(s) % 9);
    static const int p[14] = {700, 40, 60, 60, 200, 100, 20, 500, 80, 20, 10, 100, 0, 0};
    for (int j = 0; j < 14; ++j) a[2 + j] = coin(s, p[j]);
}

static int cmp_int(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }
static int cmp64(const void *a, const void *b)
{
    int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
    return x < y ? -1 : x > y;
}

int main(int argc, char **argv)
{
    const char *config = "configs/s64stronghold.yaml", *start = NULL;
    int n = 32, groups = 2, threads = 16, steps = 36000, policy = 1, dev = 0;
    int macros = 16383 | NETH_VEC_FOOD | NETH_VEC_STRONGHOLD;
    uint64_t seed = 1;
    char mopts[1024] = "";
    const char *tape = NULL;    /* lane/shold: --tape DIR */
    int rows = 0;
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--config") && i + 1 < argc) config = argv[++i];
        else if (!strcmp(argv[i], "--start") && i + 1 < argc) start = argv[++i];
        else if (!strcmp(argv[i], "--dev")) dev = 1;
        else if (!strcmp(argv[i], "--ns") && i + 1 < argc) n = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--groups") && i + 1 < argc) groups = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--threads") && i + 1 < argc) threads = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--steps") && i + 1 < argc) steps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--policy") && i + 1 < argc) policy = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--tape") && i + 1 < argc) tape = argv[++i];
        else if (!strcmp(argv[i], "--rows")) rows = 1;
        else if (!strcmp(argv[i], "--macros") && i + 1 < argc) macros = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--opt") && i + 1 < argc)
        {
            /* lane/macroopts: a macro option, NAME=WORD (neth_vec_opts.macro_opts; --opts lists them) */
            size_t l = strlen(mopts);
            snprintf(mopts + l, sizeof mopts - l, "%s%s", l ? "," : "", argv[++i]);
        }
        else if (!strcmp(argv[i], "--opts"))
        {
            neth_vec_mopt_list(stdout);
            return 0;
        }
        else
        {
            fprintf(stderr, "usage: sh_check [--config F] [--start DIR [--dev]] [--ns N] [--groups G] [--threads T] "
                            "[--steps S] [--seed K] [--policy 0|1] [--macros BITS] [--opt NAME=WORD]... [--opts]\n");
            return 2;
        }
    }
    if (n <= 0 || groups <= 0 || n % groups)
    {
        printf("FAIL: --ns %d in --groups %d\n", n, groups);
        return 2;
    }
    struct neth_vec_opts o;
    memset(&o, 0, sizeof o);
    o.env.config = config;
    o.env.start = start;
    o.env.n = n;
    o.env.threads = threads;
    o.env.device_mesh = -1;
    o.groups = groups;
    o.ep_len = steps;
    o.macros = macros;
    o.objective = NETH_VEC_FULL;
    o.flags = dev ? NETH_RL_DEV : 0;
    o.macro_opts = mopts;
    char err[1024];
    double t0 = now_s();
    struct neth_vec *v = neth_vec_make(&o, err, sizeof err);
    if (v == NULL)
    {
        printf("FAIL make: %s\n", err);
        return 1;
    }
    struct neth_vec_bufs b;
    memset(&b, 0, sizeof b);
    neth_vec_buffers(v, &b);
    int aw = neth_vec_action_width(v), gs = n / groups;
    uint64_t *rng = malloc((size_t)n * sizeof *rng);
    for (int i = 0; i < n; ++i) rng[i] = (seed * 0x9e3779b97f4a7c15ull) ^ ((uint64_t)(i + 1) * 0xbf58476d1ce4e5b9ull);
    int *done = calloc((size_t)groups, sizeof *done), live = groups;
    struct neth_vec_kit *kbk = calloc((size_t)n, sizeof *kbk);
    struct neth_vec_obsidian *obk = calloc((size_t)n, sizeof *obk);
    double tm = now_s();
    /* lane/shold (--tape DIR): two episodes an env, the second written as the oracle's script (neth_vec_tape: an env's
     * next episode), DIR/env-I.jsonl, and with --rows its row records besides (test_snapshots --row-dump's: the
     * taped episode's from its second t=2 row), DIR/env-I.rows; the books printed are the second episode's */
    int steps1 = steps;
    if (tape) steps = 2 * steps1;
    /* every group exactly steps + 1 receives: the make's step, then its episode's steps (the last one ends it) */
    while (live > 0)
    {
        int g = neth_vec_recv(v, err, sizeof err);
        if (g < 0)
        {
            printf("FAIL: %s\n", err);
            return 1;
        }
        if (tape && done[g] == 0)
            for (int i = g * gs; i < (g + 1) * gs; ++i)
            {
                char p[1024];
                snprintf(p, sizeof p, "%s/env-%d.jsonl", tape, i);
                if (neth_vec_tape(v, i, p))
                {
                    printf("FAIL: --tape %s\n", p);
                    return 1;
                }
                snprintf(p, sizeof p, "%s/env-%d.rows", tape, i);
                if (rows && neth_rl_rows(neth_vec_env(v), i, p, 0))
                {
                    printf("FAIL: --rows %s\n", p);
                    return 1;
                }
            }
        /* lane/snowleg: the kit's and the deaths' books a step before the episode's end (its reset clears them) */
        if (done[g] == steps - 50)     /* (kit_case's rule: the run's last steps are the next episode's) */
            for (int i = g * gs; i < (g + 1) * gs; ++i) neth_vec_kit(v, i, &kbk[i]), neth_vec_obsidian(v, i, &obk[i]);
        if (++done[g] > steps + 1)
        {
            --live;
            continue;
        }
        if (done[g] == steps + 1)
        {
            --live;
            continue;
        }
        for (int i = g * gs; i < (g + 1) * gs; ++i) draw(&rng[i], b.actions + (size_t)i * aw, policy);
        neth_vec_send(v, g);
        if (done[g] % 6000 == 0 && g == 0)
        {
            printf("  group 0: %d steps, %.0f s\n", done[g], now_s() - tm);
            fflush(stdout);
        }
    }
    double tw = now_s() - tm;
    static struct neth_vec_episode ep[4096];
    int ne = neth_vec_episodes(v, ep, 4096);
    struct neth_vec_episode *by = calloc((size_t)n, sizeof *by);
    for (int k = 0; k < ne; ++k)
        if (ep[k].env >= 0 && ep[k].env < n) by[ep[k].env] = ep[k];
    static const char *PH[] = {"throw", "watch", "pick", "plan", "go",   "dig",
                               "roof",  "room",  "enter", "leave", "done", "hide", "oracle"};
    static const char *CAUSE[] = {"lava/fire", "fall", "drowning", "starving", "mob", "other"};
    int reached[6] = {0}, *at[6], causes[6] = {0}, deaths = 0, after = 0, eyes = 0, throws = 0, lost = 0, near = 0;
    int snaps = 0, dropseen = 0, picked = 0, stepped = 0, room_eyes = 0, nroom = 0, canopies = 0, relocs = 0;
    int hides = 0, hid = 0, skies = 0, crafts = 0, pillars = 0, stuck = 0, hunts = 0, flesh = 0;
    for (int m = 0; m < 6; ++m) at[m] = malloc((size_t)n * sizeof(int));
    printf("env started brick frame room lit end  throws near lost eyes filled deaths phase gave_up dist dims gave_y "
           "after snap snap_d snap_d2 dseen picked room_eyes stepped hides hid skies death_step osub opaths cause\n");
    for (int i = 0; i < n; ++i)
    {
        struct neth_vec_sh_book k;
        neth_vec_stronghold_book(v, i, 1, &k);
        int32_t st[6] = {k.started, k.brick, k.frame, k.room, k.lit, k.end};
        for (int m = 0; m < 6; ++m)
            if (st[m] >= 0) at[m][reached[m]++] = st[m];
        for (int c = 0; c < 6; ++c) causes[c] += k.cause[c];
        deaths += k.deaths, after += k.deaths_after, eyes += k.eyes, throws += k.throws, lost += k.lost, near += k.near;
        snaps += k.snaps, dropseen += k.dropseen, picked += k.picked, stepped += k.stepped;
        canopies += k.canopies, relocs += k.relocs;
        hides += k.hides, hid += k.hid, skies += k.skies, crafts += k.crafts, pillars += k.pillars;
        stuck += k.stuck, hunts += k.hunts, flesh += k.flesh;
        if (k.room >= 0) room_eyes += k.room_eyes, nroom += 1;
        printf("%3d %7d %5d %5d %4d %3d %4d  %6d %4d %4d %4d %6d %6d %-5s %7d %5.0f %d %6d %5d %4d %8.5f %7.3f %5d %6d "
               "%9d %7d %5d %5d %5d %10d %4d %6d %s\n",
               i, k.started, k.brick, k.frame, k.room, k.lit, k.end, k.throws, k.near, k.lost, k.eyes, k.filled,
               k.deaths, k.phase >= 0 && k.phase < 13 ? PH[k.phase] : "?", k.gave_up, k.dist, by[i].dims,
               k.gave_up >= 0 ? k.gave_y : -1, k.deaths_after, k.snaps, k.snap_d, k.snap_d2, k.dropseen, k.picked,
               k.room_eyes, k.stepped, k.hides, k.hid, k.skies, k.deaths ? k.death_step : -1, k.osub, k.opaths,
               k.deaths == 0 ? "-" : k.cause[0] ? "lava" : k.cause[1] ? "fall" : k.cause[2] ? "drown" : k.cause[3] ? "starve"
                                   : k.cause[4] ? "mob" : "other");
    }
    /* lane/snowleg: snow_kit's books (neth_vec_kit) and every death of the episode by cause (neth_vec_obsidian's) */
    {
        static const char *OC[7] = {"lava", "fire", "fall", "mob", "drown", "hunger", "other"};
        int n64 = 0, back = 0, end64 = 0, endn = 0, oc[7] = {0}, od = 0, trips = 0, cols = 0, quits = 0, logs = 0;
        int64_t *t64 = malloc((size_t)n * sizeof *t64);
        printf("\nenv snow t_shovel t_snow64 trip0 trip1 trip_steps noway col col_d logs quit w0 room t_end end_snow "
               "deaths cause hops d_step d_part d_ago d_x d_y d_z d_air\n");
        static const char *PART[4] = {"other", "wood", "trip", "dig"};
        int dparts[4] = {0}, dsh = 0, dragon = 0, dragon64 = 0;
        for (int i = 0; i < n; ++i)
        {
            struct neth_vec_kit kb = kbk[i];
            struct neth_vec_sh_book k;
            struct neth_vec_obsidian ob = obk[i];
            neth_vec_stronghold_book(v, i, 1, &k);
            int got = kb.t_snow64 >= 0, bk = got && k.room >= 0 && k.room > kb.t_snow64;
            if (got) t64[n64++] = kb.t_snow64;
            back += bk, end64 += kb.t_end >= 0 && kb.sn_end_snow >= 64, endn += kb.t_end >= 0;
            trips += kb.t_trip0 >= 0, cols += kb.sn_col > 0, quits += kb.sn_quit, logs += kb.sn_logs;
            for (int c = 0; c < 7; ++c) oc[c] += ob.cause[c];
            od += ob.deaths;
            int dp = kb.sn_dpart >= 0 && kb.sn_dpart < 4 ? kb.sn_dpart : 0;
            if (kb.sn_dstep >= 0) dparts[dp] += 1, dsh += k.deaths > 0;
            int dg = (by[i].ms >> NETH_VEC_MS_DRAGON) & 1u;     /* the episode's record: the dragon's death seen */
            dragon += dg, dragon64 += dg && kb.sn_end_snow >= 64;
            printf("%3d %4d %8lld %8lld %5lld %5lld %10d %5d %3d %5d %4d %4d %5lld %5d %6lld %8d %6d %-6s %4d %6lld %-5s "
                   "%5d %5d %3d %5d %4d\n", i, kb.n_snow,
                   (long long)kb.t_shovel, (long long)kb.t_snow64, (long long)kb.t_trip0, (long long)kb.t_trip1,
                   kb.sn_trip_steps, kb.sn_noway, kb.sn_col, kb.sn_col_d, kb.sn_logs, kb.sn_quit, (long long)kb.sn_w0,
                   k.room, (long long)kb.t_end, kb.sn_end_snow, ob.deaths,
                   ob.deaths && ob.d_cause >= 0 && ob.d_cause < 7 ? OC[ob.d_cause] : "-", kb.sn_hops,
                   (long long)kb.sn_dstep, kb.sn_dstep >= 0 ? PART[dp] : "-", kb.sn_dstep >= 0 ? kb.sn_dago : -1,
                   kb.sn_dpos[0], kb.sn_dpos[1], kb.sn_dpos[2], kb.sn_dair);
        }
        qsort(t64, (size_t)n64, sizeof *t64, cmp64);
        printf("SNOWLEG: %d envs: 64 snowballs %d (median step %lld), back in the room after them %d, the End %d, the End "
               "with 64+ %d; trips begun %d, a snowy column found %d, given up %d, logs dug %d; deaths %d:",
               n, n64, n64 ? (long long)t64[n64 / 2] : -1LL, back, endn, end64, trips, cols, quits, logs, od);
        for (int c = 0; c < 7; ++c)
            if (oc[c]) printf(" %s %d", OC[c], oc[c]);
        printf("; first deaths by who drove the step before: snow's wood %d, trip %d, dig %d, another macro or the "
               "policy %d (the stronghold's run %d); the dragon dead %d (of them reaching the End with 64+ %d)\n",
               dparts[1], dparts[2], dparts[3], dparts[0], dsh, dragon, dragon64);
        free(t64);
    }
    static const char *NAME[6] = {"started", "brick seen", "frame seen", "in the room", "portal lit", "the End"};
    printf("\n%d envs, %d steps, seed %llu, policy %d, macros %d, %s%s\n", n, steps, (unsigned long long)seed, policy,
           macros, start ? start : config, dev ? " (dev)" : "");
    printf("  macro options: %s\n", mopts[0] ? mopts : "(defaults)");
    for (int m = 0; m < 6; ++m)
    {
        qsort(at[m], (size_t)reached[m], sizeof(int), cmp_int);
        printf("  %-12s %3d of %d", NAME[m], reached[m], n);
        if (reached[m]) printf("   steps median %d, min %d, max %d", at[m][reached[m] / 2], at[m][0], at[m][reached[m] - 1]);
        printf("\n");
    }
    printf("  throws %d (%.1f an env), flew down %d, eyes lost %d, eyes left %.2f an env\n", throws,
           (double)throws / n, near, lost, (double)eyes / n);
    printf("  far lines read as the start's chunk %d, picked up again %d, lost with the drop seen %d, eyes on reaching "
           "the room %.2f an env, stepped off the frame row %d\n",
           snaps, picked, dropseen, nroom ? (double)room_eyes / nroom : 0.0, stepped);
    printf("  throw sites moved off trees %d, canopies over a drop cleared %d\n", relocs, canopies);
    printf("  nights hidden %d (%.0f steps each), climbs to the sky %d, craft starts for a stone pickaxe %d, pillars up "
           "a wall %d, stuck walks turned %d, hunts %d, rotten flesh eaten %d\n",
           hides, hides ? (double)hid / hides : 0.0, skies, crafts, pillars, stuck, hunts, flesh);
    printf("  deaths while it ran %d:", deaths);
    for (int c = 0; c < 6; ++c)
        if (causes[c]) printf(" %s %d", CAUSE[c], causes[c]);
    printf("; after it was over (the policy's) %d", after);
    printf("\n  %.0f env-steps/s (%.0f s, the make %.1f s)\n", (double)n * steps / tw, tw, tm - t0);
    neth_vec_free(v);
    return 0;
}
