/* pearl_check (lane/pearl): NETH_VEC_PEARL's case, no Python: N envs from a start (a dev start: --dev), one episode of
 * S steps each under vec_bench's policy (--policy 1, the random one, seeded by --seed: the sampler; 0 idle), and what
 * the macro's books hold at the episode's end (vecenv.h neth_vec_pearl): the pearls held (their distribution), the
 * endermen seen, provoked and killed, the pillars, the deaths and what caused each, the steps to each pearl, the
 * steps by phase; and env-steps/s.
 *
 *   out/runtime/pearl_check --config configs/s64pearl.yaml --start out/java/snapshots/pearl-night-s42 --dev
 *                           [--n 32] [--groups 2] [--threads 16] [--steps 8800] [--seed 1] [--policy 1]
 *                           [--macros BITS] [-v]
 *
 * --macros defaults to surface, mine, fight, food and pearl. The last line is PEARL-CHECK and the totals; exit 1 on
 * an engine error */
#define _GNU_SOURCE
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "pool.h"
#include "rlgui.h"
#include "vecenv.h"
#include "../mods/macros/pearl.h"

static uint64_t xs(uint64_t *s)
{
    uint64_t x = *s;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return *s = x;
}

static int coin(uint64_t *s, int p) { return (int)(xs(s) & 1023) < p; }

/* vec_bench's draw: idle (0), or the camera's centre bin half the time and the keys at the trainer's rates (1) */
static void draw(uint64_t *s, int32_t *a, int policy)
{
    memset(a, 0, NETH_VEC_ACTION * sizeof *a);
    a[0] = a[1] = NETH_VEC_CAM / 2;
    if (policy == 0) return;
    for (int j = 0; j < 2; ++j) a[j] = coin(s, 512) ? 20 : 16 + (int)(xs(s) % 9);
    static const int p[14] = {700, 40, 60, 60, 200, 100, 20, 500, 80, 20, 10, 100, 0, 0};
    for (int j = 0; j < 14; ++j) a[2 + j] = coin(s, p[j]);
}

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + 1e-9 * (double)t.tv_nsec;
}

/* mods/macros/pearl.c's PP_* */
#define NPH 12
static const char *const PH[NPH] = {"walk", "up", "scan", "down", "drop", "flee", "guard", "hide", "stair", "tunnel",
                                    "hold", "travel"};

/* survival.h's EGG_* order */
static const char *const EGGS[24] = {"creeper", "skeleton", "spider", "zombie", "slime", "ghast", "pig_zombie",
                                     "enderman", "cave_spider", "silverfish", "blaze", "magma_cube", "bat", "witch",
                                     "pig", "sheep", "cow", "chicken", "squid", "wolf", "mooshroom", "ocelot", "horse",
                                     "villager"};

static const char *cause_name(int c)
{
    return c >= 0 && c < 24 ? EGGS[c] : c == 100 ? "burning" : c == 101 ? "drowning" : c == 102 ? "starving" : "other";
}

int main(int argc, char **argv)
{
    const char *config = "configs/s64pearl.yaml", *start = NULL;
    int n = 32, groups = 2, threads = 16, steps = 8800, policy = 1, dev = 0, verbose = 0, trace = -1, census = 0;
    int free_max = 0;           /* lane/pearlhome: --free F */
    int macros = NETH_VEC_SURFACE | NETH_VEC_MINE | NETH_VEC_FIGHT | NETH_VEC_FOOD | NETH_VEC_PEARL;
    uint64_t seed = 1;
    const char *tune[8];
    int ntune = 0;
    char mopts[2048] = "";      /* lane/pearlgate: --opt's (lane/pearlhome: 2048, the chain's list is over 512) */
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--config") && i + 1 < argc) config = argv[++i];
        else if (!strcmp(argv[i], "--start") && i + 1 < argc) start = argv[++i];
        else if (!strcmp(argv[i], "--n") && i + 1 < argc) n = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--groups") && i + 1 < argc) groups = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--threads") && i + 1 < argc) threads = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--steps") && i + 1 < argc) steps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--policy") && i + 1 < argc) policy = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--macros") && i + 1 < argc) macros = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dev")) dev = 1;
        else if (!strcmp(argv[i], "-v")) verbose = 1;
        else if (!strcmp(argv[i], "--census")) census = 1;
        else if (!strcmp(argv[i], "--free") && i + 1 < argc) free_max = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--trace") && i + 1 < argc) trace = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--tune") && i + 1 < argc && ntune < 8) tune[ntune++] = argv[++i];
        else if (!strcmp(argv[i], "--opt") && i + 1 < argc)
        {
            /* lane/pearlgate: a macro option NAME=WORD (vecenv.c MOPTS: pearl_when, pearl_yield, ...) */
            size_t l = strlen(mopts);
            snprintf(mopts + l, sizeof mopts - l, "%s%s", l ? "," : "", argv[++i]);
        }
        else
        {
            fprintf(stderr, "usage: pearl_check [--config F] [--start DIR] [--dev] [--n N] [--groups G] [--threads T] "
                            "[--steps S] [--seed K] [--policy 0|1] [--macros BITS] [--opt NAME=WORD]... [-v]\n");
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
    o.ep_len = steps;
    o.macros = macros;
    o.objective = NETH_VEC_FULL;
    o.flags = dev ? NETH_RL_DEV : 0;
    o.macro_opts = mopts;
    o.chain = free_max > 0;     /* lane/pearlhome: neth_vec_chain_free's */
    if (mopts[0]) printf("macro options %s\n", mopts);
    char err[1024];
    double t0 = now_s();
    struct neth_vec *v = neth_vec_make(&o, err, sizeof err);
    if (v == NULL)
    {
        printf("FAIL: %s\n", err);
        return 1;
    }
    if (census) neth_vec_pearl_census(v, 1, -1, NULL);
    for (int i = 0; i < ntune; ++i)
    {
        /* lane/pearl3: --tune NAME=VALUE, the macro's settings (mods/macros/pearl.h neth_vec_pearl_tune) */
        char name[64];
        const char *eq = strchr(tune[i], '=');
        snprintf(name, sizeof name, "%.*s", eq ? (int)(eq - tune[i]) : 0, tune[i]);
        if (!eq || neth_vec_pearl_tune(v, name, atoi(eq + 1)))
        {
            printf("FAIL: --tune %s (zombie_pillar, drop_calm, cave_y, cave_back, cave_hold)\n", tune[i]);
            return 2;
        }
        printf("tune %s\n", tune[i]);
    }
    struct neth_vec_bufs b;
    memset(&b, 0, sizeof b);
    neth_vec_buffers(v, &b);
    uint64_t *rng = malloc((size_t)n * sizeof *rng);
    for (int i = 0; i < n; ++i) rng[i] = (seed * 0x9e3779b97f4a7c15ull) ^ ((uint64_t)(i + 1) * 0xbf58476d1ce4e5b9ull);
    /* lane/pearlhome: --free F, env i's first 0..F decisions (drawn from its sampler) with no macro, as lane/chain's
     * search does: envs one macro would drive the same way part (an idle or macro-driven case is one trajectory) */
    if (free_max > 0)
        for (int i = 0; i < n; ++i)
        {
            uint64_t k = rng[i] ^ 0x2545f4914f6cdd1dull;
            neth_vec_chain_free(v, i, (int)(xs(&k) % (uint64_t)(free_max + 1)));
        }
    int gs = n / groups, live = groups, *done = calloc((size_t)groups, sizeof *done);
    double tm = now_s();
    while (live > 0)
    {
        int g = neth_vec_recv(v, err, sizeof err);
        if (g < 0)
        {
            printf("FAIL: %s\n", err);
            return 1;
        }
        if (++done[g] > steps)
        {
            --live;
            continue;
        }
        if (trace >= g * gs && trace < (g + 1) * gs)
        {
            /* --trace ENV: a line a step: the scalars' health, food, air, height, ground and eye in water (semteach's
             * o[41..46]), the macro's kind and driver, the pearl macro's phase */
            const float *sc = b.scalars + (size_t)trace * NETH_VEC_NSCALAR + NETH_VEC_ITEMS + 9;
            struct neth_vec_pearl p;
            neth_vec_pearl(v, trace, 1, &p);
            printf("t %d (time %d): hp %.1f food %.0f air %.0f y %.2f ground %.0f water %.0f; kind %d driver %d; pearl %s "
                   "phase %d, pearls %d, seen %d, provoked %d, killed %d, pillars %d, deaths %d; target %d%s at %.1f, %d in "
                   "view; killed-by stats %d; kills %d, crosshair %d, attack %d\n", done[g] - 1, p.wtime,
                   sc[0] * 20, sc[1] * 20, sc[2] * 300, sc[3] * 32 + 64, sc[4], sc[5], b.kind[trace], b.driver[trace],
                   p.on ? "on" : "off", p.phase, p.pearls, p.seen, p.provoked, p.killed, p.pillars, p.deaths, p.tid,
                   p.angry ? " (angry)" : "", p.tdist, p.nfar, p.cause[3], p.cause[1], p.cause[2],
                   b.acts[trace].press[PK_ATTACK]);
            int32_t dg[8];
            neth_vec_pearl_dbg(v, trace, dg);
            printf("  dbg: hide %d, crosshair block %d id %d at (%d, %d, %d) from the feet, hold %#x, pitch %d\n", dg[0],
                   dg[1], dg[2], dg[3], dg[4], dg[5], (unsigned)dg[6], dg[7]);
        }
        for (int i = g * gs; i < (g + 1) * gs; ++i) draw(&rng[i], b.actions + (size_t)i * NETH_VEC_ACTION, policy);
        if (neth_vec_send(v, g))
        {
            printf("FAIL: send %d refused\n", g);
            return 1;
        }
    }
    double wall = now_s() - tm, st[8];
    neth_vec_stats(v, st);
    static struct neth_vec_episode ep[4096];
    int ne = neth_vec_episodes(v, ep, 4096);
    int hist[NETH_VEC_PEARLS + 2] = {0}, causes[128] = {0}, phases = 0, chist[NETH_VEC_PEARLS + 2] = {0};
    int first[128] = {0}, died = 0, chain = 0;
    int64_t dstep = 0;
    int64_t seen = 0, prov = 0, killed = 0, pillars = 0, deaths = 0, kills = 0, pearls = 0, ph[NPH] = {0};
    int64_t hsteps = 0, hstand = 0, hnoway = 0, hland_t = 0, hd0 = 0, hd1 = 0, hdigs = 0;     /* lane/pearlhome */
    int hland = 0, hdn = 0;
    int64_t tsum[NETH_VEC_PEARLS] = {0}, tn[NETH_VEC_PEARLS] = {0};
    int64_t upx[10] = {0}, uph[12] = {0}, vsee[4] = {0}, towers = 0, hits = 0, towered = 0;  /* lane/pearlsee */
    int64_t cen2[4] = {0}, cen3[8] = {0}, skyc = 0, drops = 0, fetched = 0, fetch_out = 0, lure = 0, hst = 0, hwk = 0, hgain = 0, dopen = 0, dsteps = 0;
    int fun[10] = {0}, fun_sky = 0, fun_view = 0, restocks = 0, ostep = 0, od[4] = {0}, oups = 0, obad = 0, ienv = 0, isteps = 0,
        iwalk = 0;
    int64_t cen4[16] = {0}, canyf[4] = {0}, cyaw[4] = {0}, cpitch[3] = {0};
    for (int i = 0; i < n; ++i)
    {
        struct neth_vec_pearl p;
        neth_vec_pearl(v, i, 0, &p);
        for (int k = 0; k < 10; ++k) upx[k] += p.upx[k];
        for (int k = 0; k < 12; ++k) uph[k] += p.uph[k];
        for (int k = 0; k < 4; ++k) vsee[k] += p.vsee[k];
        towers += p.towers, hits += p.hits, towered += p.towers > 0;
        skyc += p.sky_climbs, drops += p.drops, fetched += p.fetched, fetch_out += p.fetch_out, lure += p.lure_digs;
        hst += p.high_steps, hwk += p.high_walks, hgain += p.high_gain;
        dopen += p.dig_open, dsteps += p.dig_steps;
        for (int k = 0; k < 4; ++k) cen2[k] += p.cen2[k];
        for (int k = 0; k < 8; ++k) cen3[k] += p.cen3[k];
        for (int k = 0; k < 10; ++k) fun[k] += p.first[k] >= 0;    /* lane/pearlsee (3rd pass) */
        for (int k = 0; k < 16; ++k) cen4[k] += p.cen4[k];
        for (int k = 0; k < 4; ++k) canyf[k] += p.cany_f[k], cyaw[k] += p.cyaw[k];
        for (int k = 0; k < 3; ++k) cpitch[k] += p.cpitch[k];
        restocks += p.restocks, ostep += p.oracle_steps, oups += p.oracle_path, obad += p.oracle_bad;
        ienv += p.item_steps > 0, isteps += p.item_steps, iwalk += p.item_walk;
        for (int k = 0; k < 4; ++k) od[k] += p.oracle_d[k];
        fun_sky += p.hsky > 0, fun_view += (p.vsee[0] + p.vsee[1] + p.vsee[2] + p.vsee[3]) > 0;
    }
    /* lane/pearlsee (3rd pass): the funnel in envs (pearl.c PG_F_*: the first step each was reached) */
    printf("funnel (envs): rods in the overworld %d, night %d, hunt %d, surface %d, under the sky %d, tower %lld, in view "
           "%d, provoked %d, hit %d, killed %d, pearl %d; restocks %d; oracle steps %d (by distance <16 %d, 16-32 %d, "
           "32-64 %d, 64+ %d), on a path %d, no path %d\n", fun[0], fun[1], fun[2], fun[3], fun_sky, (long long)towered,
           fun_view, fun[5], fun[6], fun[7], fun[8], restocks, ostep, od[0], od[1], od[2], od[3], oups, obad);
    /* lane/pearlhome: every step by what held the hunt (pearl.c PG_WHY_*, pearl_want's order) and, under another
     * macro, which (the run file's macro names by bit) */
    {
        static const char *const WHY[11] = {"hunt", "dead", "nether", "end", "screen", "no-blaze", "full", "day",
                                            "yield", "other", "policy"};
        int64_t why[12] = {0}, whom[33] = {0}, tot = 0;
        for (int i = 0; i < n; ++i)
        {
            struct neth_vec_pearl p;
            neth_vec_pearl(v, i, 0, &p);
            for (int k = 0; k < 12; ++k) why[k] += p.why[k], tot += p.why[k];
            for (int k = 0; k < 33; ++k) whom[k] += p.whom[k];
        }
        printf("why (steps, what held the hunt):");
        for (int k = 0; k < 11; ++k)
            if (why[k]) printf(" %s %.1f%%", WHY[k], tot ? 100.0 * why[k] / tot : 0.0);
        printf("; other macros:");
        for (int k = 1; k < 33; ++k)
        {
            if (!whom[k]) continue;
            const char *nm = NULL;
            int32_t bit;
            for (int j = 0; (nm = neth_vec_macro_switch(j, &bit)) != NULL; ++j)
                if (bit == (int32_t)(1u << (k - 1))) break;
            printf(" %s %.1f%%", nm ? nm : "?", tot ? 100.0 * whom[k] / tot : 0.0);
        }
        printf("\n");
    }
    /* lane/pearlsee (3rd pass): the pearl items the kills left (the census: --census or pearl_oracle on) */
    printf("pearl items: envs with one within 32 blocks %d, steps %d, the oracle's steps walking to one %d\n", ienv, isteps,
           iwalk);
    {   /* lane/pearlhome (2nd pass): the steps with a pearl item about, by what held them, phase, place, walk */
        static const char *const WHY[11] = {"hunt", "dead", "nether", "end", "screen", "no-blaze", "full", "day",
                                            "yield", "other", "policy"};
        int64_t nw = 0, wy[12] = {0}, ph[12] = {0}, d[4] = {0}, dy[3] = {0}, wk[4] = {0}, full = 0, wet = 0;
        for (int i = 0; i < n; ++i)
        {
            struct neth_vec_pearl p;
            neth_vec_pearl(v, i, 0, &p);
            nw += p.it_new, full += p.it_full, wet += p.it_wet;
            for (int k = 0; k < 12; ++k) wy[k] += p.it_why[k], ph[k] += p.it_ph[k];
            for (int k = 0; k < 4; ++k) d[k] += p.it_d[k], wk[k] += p.it_wk[k];
            for (int k = 0; k < 3; ++k) dy[k] += p.it_dy[k];
        }
        printf("pearl items (2nd pass): new %lld; steps by what held them:", (long long)nw);
        for (int k = 0; k < 11; ++k)
            if (wy[k]) printf(" %s %lld", WHY[k], (long long)wy[k]);
        printf("; by phase:");
        for (int k = 0; k < NPH; ++k)
            if (ph[k]) printf(" %s %lld", PH[k], (long long)ph[k]);
        printf("; across <1.5 %lld, 1.5-6 %lld, 6-16 %lld, 16+ %lld; below %lld, level %lld, above %lld; walk no-path %lld, "
               "walking %lld, there %lld, time-over %lld; pack full %lld, eye wet %lld\n", (long long)d[0], (long long)d[1],
               (long long)d[2], (long long)d[3], (long long)dy[0], (long long)dy[1], (long long)dy[2], (long long)wk[0],
               (long long)wk[1], (long long)wk[2], (long long)wk[3], (long long)full, (long long)wet);
    }
    if (census)
    {
        static const char *const G[4] = {"walk", "up", "scan", "rest"};
        printf("clear lines by phase (steps, steps with one, enderman-steps, in the frame, steps with one in it):");
        for (int g = 0; g < 4; ++g)
            printf(" %s %lld/%lld/%lld/%lld/%lld", G[g], (long long)cen4[g * 4], (long long)cen4[g * 4 + 1],
                   (long long)cen4[g * 4 + 2], (long long)cen4[g * 4 + 3], (long long)canyf[g]);
        printf("; the scan's by yaw off <35 %lld, 35-70 %lld, 70-120 %lld, >120 %lld; by pitch over %lld, in %lld, under "
               "%lld\n", (long long)cyaw[0], (long long)cyaw[1], (long long)cyaw[2], (long long)cyaw[3],
               (long long)cpitch[0], (long long)cpitch[1], (long long)cpitch[2]);
    }
    for (int i = 0; i < n; ++i)
    {
        struct neth_vec_pearl p;
        neth_vec_pearl(v, i, 0, &p);
        int kk = -1;
        for (int k = 0; k < ne; ++k)
            if (ep[k].env == i) kk = k;
        int ek = kk >= 0 ? ep[kk].kills : 0, edeaths = kk >= 0 ? ep[kk].deaths : 0;
        hist[p.pearls > NETH_VEC_PEARLS ? NETH_VEC_PEARLS + 1 : p.pearls] += 1;
        seen += p.seen, prov += p.provoked, killed += p.killed, pillars += p.pillars, deaths += p.deaths, kills += ek;
        pearls += p.pearls;
        for (int k = 0; k < 4 && k < p.deaths; ++k) causes[p.cause[k] & 127] += 1;
        if (p.deaths > 0) first[p.cause[0] & 127] += 1, died += 1, dstep += p.death_step;
        chist[p.chain > NETH_VEC_PEARLS ? NETH_VEC_PEARLS + 1 : p.chain] += 1;
        chain += p.chain;
        for (int k = 0; k < NETH_VEC_PEARLS; ++k)
            if (p.step[k] >= 0) tsum[k] += p.step[k], tn[k] += 1;
        for (int k = 0; k < NPH; ++k) ph[k] += p.phase_steps[k], phases += p.phase_steps[k];
        if (verbose)
        {
            printf("env %2d: pearls %d, seen %d, provoked %d, killed %d (kills %d), pillars %d, deaths %d (episode %d)",
                   i, p.pearls, p.seen, p.provoked, p.killed, ek, p.pillars, p.deaths, edeaths);
            for (int k = 0; k < 4 && k < p.deaths; ++k) printf(" %s", cause_name(p.cause[k]));
            printf("; pearl steps");
            for (int k = 0; k < NETH_VEC_PEARLS && p.step[k] >= 0; ++k) printf(" %d", p.step[k]);
            printf("; phase steps");   /* lane/macroperf: the books exact, to hold a build to another's */
            for (int k = 0; k < NPH; ++k) printf(" %lld", (long long)p.phase_steps[k]);
            printf("; oracle %d %d %d %d", p.oracle_d[0], p.oracle_d[1], p.oracle_d[2], p.oracle_d[3]);
            /* lane/pearlhome: pearl_home's leg: steps, stood, first on land, the room's distance at the first leg step
             * and the end, legs with no way */
            printf("; home %d %d land %d room %d to %d noway %d", p.home_steps, p.home_stand, p.home_land,
                   p.home_d0, p.home_d1, p.home_noway);
            if (p.deaths > 0)
                printf("; first death at %.1f %.1f %.1f kind %d pearl %s phase %s leg %d wet %d", p.death_x, p.death_y,
                       p.death_z, p.death_kind, p.death_on ? "on" : "off",
                       p.death_phase >= 0 && p.death_phase < NPH ? PH[p.death_phase] : "-", p.death_leg, p.death_wet);
            if (p.deaths > 0)
            {
                const char *nm = NULL;
                int32_t bit;
                for (int j = 0; p.death_mbit && (nm = neth_vec_macro_switch(j, &bit)) != NULL; ++j)
                    if (bit == p.death_mbit) break;
                printf(" macro %s", p.death_mbit ? (nm ? nm : "?") : "policy");
            }
            printf("\n");
        }
        hsteps += p.home_steps, hstand += p.home_stand, hnoway += p.home_noway, hdigs += p.home_digs;
        if (p.home_land >= 0) hland += 1, hland_t += p.home_land;
        if (p.home_d0 >= 0) hd0 += p.home_d0, hd1 += p.home_d1, hdn += 1;
    }
    printf("pearl_home: leg steps %lld, stood %lld, no way %lld; envs on land %d (first step mean %.0f); the room's "
           "distance mean %.0f to %.0f (%d envs); steps digging to one under the feet %lld\n", (long long)hsteps,
           (long long)hstand, (long long)hnoway, hland, hland ? (double)hland_t / hland : -1.0,
           hdn ? (double)hd0 / hdn : -1.0, hdn ? (double)hd1 / hdn : -1.0, hdn, (long long)hdigs);
    printf("%d envs, %d steps each (seed %llu, policy %d, macros %d) from %s: %.0f env-steps/s (%.1f s; made in "
           "%.1f s)\n", n, steps, (unsigned long long)seed, policy, macros, start ? start : config, st[0] / wall, wall,
           tm - t0);
    printf("pearls held at the end:");
    for (int k = 0; k <= NETH_VEC_PEARLS; ++k)
        if (hist[k]) printf(" %d:%d", k, hist[k]);
    printf(" (envs by count; mean %.3f)\n", (double)pearls / n);
    printf("endermen: %lld seen, %lld provoked, %lld killed by the macro (stat.mobKills %lld over every kind); %lld "
           "pillars\n", (long long)seen, (long long)prov, (long long)killed, (long long)kills, (long long)pillars);
    printf("the chain's pearls (held at the first death, else at the end):");
    for (int k = 0; k <= NETH_VEC_PEARLS; ++k)
        if (chist[k]) printf(" %d:%d", k, chist[k]);
    printf(" (mean %.3f)\n", (double)chain / n);
    printf("envs that died %d (the first death's step, mean %.0f):", died, died ? (double)dstep / died : 0.0);
    for (int c = 0; c < 128; ++c)
        if (first[c]) printf(" %s %d", cause_name(c), first[c]);
    printf("\ndeaths %lld, every one:", (long long)deaths);
    for (int c = 0; c < 128; ++c)
        if (causes[c]) printf(" %s %d", cause_name(c), causes[c]);
    printf("\nsteps to the k-th pearl (mean over the envs reaching it):");
    for (int k = 0; k < NETH_VEC_PEARLS && tn[k]; ++k) printf(" %d:%.0f (%lld)", k + 1, (double)tsum[k] / tn[k], (long long)tn[k]);
    printf("\nthe macro's steps by phase (%.1f%% of all):", 100.0 * phases / ((double)n * steps));
    for (int k = 0; k < NPH; ++k)
        if (k < 8 || ph[k]) printf(" %s %.1f%%", PH[k], phases ? 100.0 * ph[k] / phases : 0.0);
    {
        /* lane/pearlsee (2nd pass): the climbs' ends (pearl.c UX_*), their heights, the towers, the view */
        static const char *const UX[10] = {"top", "short", "noblock", "cap", "roof", "end", "dawn", "other", "climb",
                                           "sky-refused"};
        printf("\nclimbs by end:");
        for (int k = 0; k < 10; ++k) printf(" %s %lld", UX[k], (long long)upx[k]);
        printf("; by blocks placed:");
        for (int k = 0; k < 12; ++k)
            if (uph[k]) printf(" %d:%lld", k, (long long)uph[k]);
        printf("\ntowers topped %lld (envs with one %lld of %d); in view steps <16 %lld, 16-32 %lld, 32-48 %lld, 48-64 "
               "%lld; hits %lld", (long long)towers, (long long)towered, n, (long long)vsee[0], (long long)vsee[1],
               (long long)vsee[2], (long long)vsee[3], (long long)hits);
        printf("; dug towers to the sky %lld (%lld steps mining)", (long long)dopen, (long long)dsteps);
        printf("; dug towers begun %lld; drops %lld, walked onto %lld, given up %lld; lure digs %lld", (long long)skyc,
               (long long)drops, (long long)fetched, (long long)fetch_out, (long long)lure);
        printf("; high ground: %lld steps walked, %lld walks, %lld blocks gained", (long long)hst, (long long)hwk,
               (long long)hgain);
        if (census)
            printf("\nhunt census, endermen within 64 tracked %lld; the line clear from the eye +0 %lld, +4 %lld, +8 %lld, "
                   "+12 %lld, +16 %lld; from the high ground's eye %lld, 10 over it %lld; the high ground (within %d, "
                   "%lld steps) %.1f over the feet, %.1f away", (long long)cen2[0], (long long)cen2[1], (long long)cen2[2],
                   (long long)cen3[0], (long long)cen3[1], (long long)cen3[2], (long long)cen3[3], (long long)cen3[4],
                   NETH_RL_CENSUS_HI, (long long)cen3[5], cen3[5] ? (double)cen3[6] / cen3[5] : 0.0,
                   cen3[5] ? (double)cen3[7] / cen3[5] : 0.0);
    }
    if (census)
    {
        /* the census (measurement only, privileged): the endermen of the server world each step, by horizontal band */
        struct pearl_census t, c;
        memset(&t, 0, sizeof t);
        printf("\ncensus (every step, every env, privileged: never what the macro reads)\n");
        for (int i = 0; i < n; ++i)
        {
            neth_vec_pearl_census(v, -1, i, &c);
            t.samples += c.samples, t.alive += c.alive, t.pl_sky += c.pl_sky, t.skel_los += c.skel_los;
            t.hides += c.hides, t.sealed += c.sealed;
            t.cells += c.cells, t.holds += c.holds, t.hold_seen += c.hold_seen;
            t.clear0_64 += c.clear0_64, t.clear2_64 += c.clear2_64, t.clear4_64 += c.clear4_64;
            if (c.hides) printf("  hides env %2d: %lld, sealed %lld\n", i, (long long)c.hides, (long long)c.sealed);
            t.lost_angry += c.lost_angry, t.lost_calm += c.lost_calm, t.stare_out += c.stare_out;
            t.drops += c.drops, t.drop_out += c.drop_out, t.picks += c.picks;
            for (int k = 0; k < 4; ++k) t.hostile[k] += c.hostile[k];
            for (int b = 0; b < PEARL_CBANDS; ++b)
            {
                t.n[b] += c.n[b], t.sky[b] += c.sky[b], t.tracked[b] += c.tracked[b], t.clear[b] += c.clear[b];
                t.view[b] += c.view[b], t.skyclear[b] += c.skyclear[b], t.angry[b] += c.angry[b];
                t.below[b] += c.below[b], t.level[b] += c.level[b], t.above[b] += c.above[b];
            }
            for (int k = 0; k < 64; ++k)
                t.biome_steps[k] += c.biome_steps[k], t.biome_view[k] += c.biome_view[k],
                    t.biome_sky64[k] += c.biome_sky64[k];
            for (int k = 0; k < 16; ++k)
                t.ender_y[k] += c.ender_y[k], t.pl_y[k] += c.pl_y[k], t.view_y[k] += c.view_y[k];
            for (int j = 0; j < c.ndeath && j < PEARL_CDEATHS; ++j)
                printf("  death env %2d step %5d: %-9s kind %d pearl %s phase %s%s, %s, y %d, hp 10 steps before %d; "
                       "within 16: skeleton %d (%d with a line), zombie %d, spider %d, creeper %d\n", i, c.death[j].te,
                       cause_name(c.death[j].cause), c.death[j].kind, c.death[j].on ? "on" : "off",
                       c.death[j].phase >= 0 && c.death[j].phase < NPH ? PH[c.death[j].phase] : "-",
                       c.death[j].up ? " (on the pillar)" : "",
                       c.death[j].sky ? "under the sky" : "under cover", c.death[j].y, c.death[j].hp10,
                       c.death[j].hostile[0], c.death[j].skel_los, c.death[j].hostile[1], c.death[j].hostile[2],
                       c.death[j].hostile[3]);
        }
        double S = t.samples > 0 ? (double)t.samples : 1.0;
        printf("  %lld env-steps; endermen alive in the world %.2f a step; the player under the sky %.0f%%; within 16 "
               "blocks a step: skeleton %.2f (%.2f with a line to the eye), zombie %.2f, spider %.2f, creeper %.2f\n",
               (long long)t.samples, t.alive / S, 100.0 * t.pl_sky / S, t.hostile[0] / S, t.skel_los / S,
               t.hostile[1] / S, t.hostile[2] / S, t.hostile[3] / S);
        printf("  band (blocks) | endermen a step | sky %% | below/level/above %% | tracked %% | line clear %% | sky and "
               "clear %% | in view %% | screaming %%\n");
        int64_t tn = 0, tv = 0;
        for (int b = 0; b < PEARL_CBANDS; ++b)
        {
            double m = t.n[b] > 0 ? (double)t.n[b] : 1.0;
            tn += t.n[b], tv += t.view[b];
            printf("  %3d-%-3d       | %.3f           | %3.0f   | %3.0f/%3.0f/%3.0f         | %3.0f       | %3.0f          | "
                   "%3.0f            | %4.1f      | %4.1f\n", 16 * b, 16 * b + 16, t.n[b] / S, 100.0 * t.sky[b] / m,
                   100.0 * t.below[b] / m, 100.0 * t.level[b] / m, 100.0 * t.above[b] / m, 100.0 * t.tracked[b] / m,
                   100.0 * t.clear[b] / m, 100.0 * t.skyclear[b] / m, 100.0 * t.view[b] / m, 100.0 * t.angry[b] / m);
        }
        printf("  within 128: %.3f a step, %.4f in view a step; hides %lld, sealed %lld\n", tn / S, tv / S,
               (long long)t.hides, (long long)t.sealed);
        printf("  by height (feet y, 8 a bin): endermen within 64 a step | the player's steps %% | in view a step there\n");
        for (int k = 0; k < 16; ++k)
            if (t.ender_y[k] || t.pl_y[k])
                printf("    y %3d-%-3d: %.4f | %4.1f%% | %.4f\n", 8 * k, 8 * k + 7, t.ender_y[k] / S,
                       100.0 * t.pl_y[k] / S, t.pl_y[k] ? (double)t.view_y[k] / t.pl_y[k] : 0.0);
        {
            /* lane/pearl3: the seed sweep's line: the spawn's biome (the envs' most common first sample), the steps'
             * most common biome, endermen within 64 and 128 a step, in view a step, kills, pearls, deaths */
            int b0[64] = {0}, sb = 0, tb = 0;
            for (int i = 0; i < n; ++i)
            {
                neth_vec_pearl_census(v, -1, i, &c);
                b0[c.biome0 & 63] += 1;
            }
            for (int k = 0; k < 64; ++k)
            {
                if (b0[k] > b0[sb]) sb = k;
                if (t.biome_steps[k] > t.biome_steps[tb]) tb = k;
            }
            int64_t n64 = t.n[0] + t.n[1] + t.n[2] + t.n[3], s64 = 0;
            for (int b = 0; b < 4; ++b) s64 += t.sky[b];
            printf("PEARL-SEED spawn_biome %d top_biome %d (%.0f%%) within64 %.4f sky64 %.4f within128 %.4f view %.5f "
                   "killed %lld pearls %lld chain %d died %d deaths %lld envs %d\n", sb, tb,
                   100.0 * t.biome_steps[tb] / S, n64 / S, s64 / S, tn / S, tv / S, (long long)killed,
                   (long long)pearls, chain, died, (long long)deaths, n);
        }
        printf("  endermen within 64 (horizontally) with the line clear a step: from the eye %.4f, 2 over it %.4f, 4 over "
               "it %.4f\n", t.clear0_64 / S, t.clear2_64 / S, t.clear4_64 / S);
        printf("  the cave hunt: %lld cells dug and walked into, %lld holds (caves opened), %lld with an enderman in "
               "view\n", (long long)t.cells, (long long)t.holds, (long long)t.hold_seen);
        printf("  targets let go out of view: %lld provoked, %lld calm; stares given up %lld; drops booked %lld, given up "
               "%lld; pearls picked up %lld\n", (long long)t.lost_angry, (long long)t.lost_calm, (long long)t.stare_out,
               (long long)t.drops, (long long)t.drop_out, (long long)t.picks);
        printf("  the player's biome (id & 63): share of steps, endermen within 64 under the sky a step, in view a step\n");
        for (int k = 0; k < 64; ++k)
            if (t.biome_steps[k] * 200 >= t.samples)
                printf("    biome %2d: %4.1f%%, %.3f, %.4f\n", k, 100.0 * t.biome_steps[k] / S,
                       (double)t.biome_sky64[k] / t.biome_steps[k], (double)t.biome_view[k] / t.biome_steps[k]);
    }
    printf("\nPEARL-CHECK pearls %lld, chain %d, seen %lld, provoked %lld, killed %lld, died %d, envs %d\n",
           (long long)pearls, chain, (long long)seen, (long long)prov, (long long)killed, died, n);
    neth_vec_free(v);
    free(rng);
    free(done);
    return 0;
}
