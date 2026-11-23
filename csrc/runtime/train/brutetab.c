/* brutetab (lane/brutetab): a row of the brute-force table (GOAL.md "Done means" 1), all of it in C on the CPUs. A
 * random policy over the vector env (vecenv.h) with the host's ray cast, the run file's sim, macros and starts: each
 * env plays R episodes of the run's length; then, for each milestone of the chain (stages.h STAGE_CHAIN: the wooden
 * pickaxe to the End, then the dragon's death; a log first), the share of episodes that reached it and the wall time
 * one success is expected in at the measured env-steps/s, 1 / (rate x episodes a second) = ep_len / (rate x
 * env-steps/s). A milestone no episode reached prints its 95% bound (rate under 3 / E: the wall over that).
 *
 *   out/runtime/train/brutetab --run configs/tests/brutetab-chain.yaml [--macros SPEC|BITS] [--label NAME]
 *                              [--episodes R] [--ep-seconds S] [--n N] [--groups G] [--threads T]
 *                              [--seeds A-B,C] [--seed-set DIR] [--sample K] [--window W[:S]]
 *                              [--cam W] [--keys P] [--seed K] [--every SEC] [--json FILE]
 *   out/runtime/train/brutetab --macro-names      the name table (the macros' registration table's, as ntloop reads
 *                                                 it: vecenv.h neth_vec_macro_switch; brutetab.mk's names check)
 *
 * The policy is lane/intent's (DEVLOG 2026-10-27): each step each of the two camera bins uniform over 20 +- W of the
 * 41 (--cam, default 5: that table's better rows), each of the 14 keys held with probability P (--keys, 0.5). The
 * sampler is a xorshift an env seeded from --seed and the env's id, so a row is the same trajectory at any --threads
 * when the starts are (the vector env's draws are its own state's).
 *
 * The run file (ntloop's grammar: parts at column 0, sections at 2, keys at 4) gives the sim (the binding reads the
 * file's sim part: world, client, engine, pipeline, fidelity, performance) and these: sim pipeline.worlds (N),
 * pipeline.groups (G), pipeline.episode_seconds (the episode, 20 steps a second at ticks_per_step 1); policy
 * actions.macros (train/loop.c macro_set's spec: names left to right, -NAME out, base and recipe their lists, and
 * here `all`, every bit), starts.seeds and starts.seed_set. The flags override them. Every other key is the trainer's
 * and is skipped, so a brutetab run file also runs ntloop (--score with a model: the same row for a frozen net).
 *
 * The starts: without seeds the run's own pipeline.start (seed 42's spawn). With seeds (a spec of world seeds) each
 * seed's start is SEEDSET/sN (the oracle's fresh world of that seed, lane/seeds; the seed set whose world is the
 * run's but its seed, as ntloop finds it, curric.h cur_find_seedset, or --seed-set), the list shuffled by --seed,
 * the run's own start given no share. --sample K keeps the shuffled list's first K (every start of the mix is loaded
 * at the make, about 50 MB each); --window W[:S] loads W at a time, moving S a round (neth_vec_opts.start_window: a
 * group's r-th reset draws from starts r S .. r S + W - 1), for many rounds of short episodes. The summary names
 * how many distinct starts the episodes came from.
 *
 * Output: a progress line every --every seconds (60), then the row: `row LABEL macros BITS: E episodes of S s from
 * STARTS, X env-steps/s, deaths D a run`, one line a milestone (count, rate, the CPU wall a success), the table's
 * markdown row, and with --json FILE one JSON line appended. CUDA is never needed: run it with CUDA_VISIBLE_DEVICES=
 * on a run file whose engine has device_mesh 0, raycast host, render none. */
#define _GNU_SOURCE
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

#include "../rlgui.h"
#include "../vecenv.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#include "curric.h"
#pragma GCC diagnostic pop

/* a macro name's bit, 0 none (lane/macromod: the macros' registration table's names, as train/loop.c macro_bit reads
 * them; a copy of loop.c's table before); `all` is every name's */
static int bt_macro_bit(const char *name)
{
    int32_t bit;
    for (int i = 0;; ++i)
    {
        const char *n = neth_vec_macro_switch(i, &bit);
        if (!n) return 0;
        if (!strcmp(n, name)) return bit;
    }
}

static int bt_macro_all(void)
{
    int32_t bit, all = 0;
    for (int i = 0; neth_vec_macro_switch(i, &bit); ++i) all |= bit;
    return all;
}

/* the policy's defaults (lane/intent's better row) and the run's */
#define BT_CAM_W 5              /* the camera bins uniform over 20 +- BT_CAM_W */
#define BT_KEY_P 0.5            /* each key held with this probability */
#define BT_WINDOW_STRIDE_DIV 2  /* --window W without :S moves W / 2 a round */
#define BT_RING 65536           /* episode records drained at each receive */

/* macro_set's grammar (train/loop.c) plus `all`; a bare number is the bits themselves; -1 on a bad name */
static int bt_macro_set(const char *spec, char *err, size_t en)
{
    static const char *const BASE = "surface,loot,mine,craft";
    static const char *const RECIPE = "surface,loot,mine,craft,seek,carry,iron,late,liquid,portal,end,fight,table-anywhere";
    char *e;
    long num = strtol(spec, &e, 10);
    if (*spec && !*e) return (int)num;
    int bits = 0;
    char buf[512];
    snprintf(buf, sizeof buf, "%s", spec);
    for (char *sv, *t = strtok_r(buf, ",", &sv); t; t = strtok_r(NULL, ",", &sv))
    {
        while (*t == ' ') ++t;
        int drop = *t == '-';
        while (*t == '-' || *t == '+') ++t;
        int add = 0;
        if (!strcmp(t, "none") || !*t) add = 0;
        else if (!strcmp(t, "all"))
            add = bt_macro_all();
        else if (!strcmp(t, "base") || !strcmp(t, "recipe"))
        {
            char l[256];
            snprintf(l, sizeof l, "%s", !strcmp(t, "base") ? BASE : RECIPE);
            for (char *s2, *u = strtok_r(l, ",", &s2); u; u = strtok_r(NULL, ",", &s2)) add |= bt_macro_bit(u);
        }
        else
        {
            add = bt_macro_bit(t);
            if (!add) return snprintf(err, en, "macros: no such macro %s", t), -1;
        }
        bits = drop ? bits & ~add : bits | add;
    }
    return bits;
}

struct btrun {
    int worlds, groups, macros, world_seed;
    double ep_seconds;
    char macro_spec[512], seeds[256], seed_set[1024];
    char mopts[1024];           /* lane/pearlgate: the run file's policy.actions.NAME macro options, NAME=WORD,... */
};

/* lane/pearlgate: NAME=WORD appended to a macro option list (neth_vec_opts.macro_opts) */
static void bt_mopt_add(char *list, size_t n, const char *name, const char *word)
{
    size_t l = strlen(list);
    snprintf(list + l, n - l, "%s%s=%s", l ? "," : "", name, word);
}

static void bt_trim(char *s)
{
    char *h = strstr(s, " #");
    if (s[0] == '#') s[0] = 0;
    if (h) *h = 0;
    size_t l = strlen(s);
    while (l && (s[l - 1] == '\n' || s[l - 1] == '\r' || s[l - 1] == ' ')) s[--l] = 0;
}

/* the run file's keys this tool reads (ntloop's read_run grammar); 0 or -1 with err */
static int bt_read_run(const char *path, struct btrun *r, char *err, size_t en)
{
    FILE *f = fopen(path, "r");
    if (!f) return snprintf(err, en, "--run %s: cannot open", path), -1;
    char line[1024], part[64] = "", sec[64] = "";
    int ln = 0;
    while (fgets(line, sizeof line, f))
    {
        ++ln;
        bt_trim(line);
        if (!line[0]) continue;
        int ind = 0;
        while (line[ind] == ' ') ++ind;
        char *k = line + ind, *c = strchr(k, ':');
        if (!c)
        {
            fclose(f);
            return snprintf(err, en, "%s:%d: not KEY: VALUE", path, ln), -1;
        }
        *c = 0;
        char *v = c + 1;
        while (*v == ' ') ++v;
        if (ind == 0) { snprintf(part, sizeof part, "%s", k); continue; }
        if (ind == 2 && !*v) { snprintf(sec, sizeof sec, "%s", k); continue; }
        char key[160];
        snprintf(key, sizeof key, "%s.%s.%s", part, sec, k);
        if (!strcmp(key, "sim.pipeline.worlds")) r->worlds = atoi(v);
        else if (!strcmp(key, "sim.pipeline.groups")) r->groups = atoi(v);
        else if (!strcmp(key, "sim.pipeline.episode_seconds")) r->ep_seconds = atof(v);
        else if (!strcmp(key, "sim.world.seed")) r->world_seed = atoi(v);
        else if (!strcmp(key, "policy.actions.macros")) snprintf(r->macro_spec, sizeof r->macro_spec, "%s", v);
        else if (!strncmp(key, "policy.actions.", 15) && strcmp(key, "policy.actions.picks"))
            bt_mopt_add(r->mopts, sizeof r->mopts, key + 15, v);   /* lane/pearlgate: neth_vec_make refuses a bad one */
        else if (!strcmp(key, "policy.starts.seeds")) snprintf(r->seeds, sizeof r->seeds, "%s", v);
        else if (!strcmp(key, "policy.starts.seed_set")) snprintf(r->seed_set, sizeof r->seed_set, "%s", v);
    }
    fclose(f);
    return 0;
}

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

/* the columns: a log, then the chain's entries (stages.h STAGE_CHAIN past spawn), then the dragon */
enum { BT_LOG = 0, BT_CHAIN0 = 1, BT_DRAGON = STAGE_BASE_N, BT_NCOL = STAGE_BASE_N + 1 };
static const char *bt_col_name(int c)
{
    return c == BT_LOG ? "log" : c == BT_DRAGON ? "dragon" : STAGE_CHAIN[c].name;
}

static int bt_has(int c, const struct neth_vec_episode *e)
{
    if (c == BT_LOG) return e->logs > 0;
    if (c == BT_DRAGON) return (e->ms >> NETH_VEC_MS_DRAGON) & 1;
    if (STAGE_CHAIN[c].kind == STAGE_DIM && STAGE_CHAIN[c].v == 1 && ((e->ms >> NETH_VEC_MS_END) & 1)) return 1;
    return stage_has(c, e->ms, -2, e->dims);
}

/* seconds as the table says them */
static void bt_dur(char *out, size_t cap, double s)
{
    if (s < 120) snprintf(out, cap, "%.1f s", s);
    else if (s < 7200) snprintf(out, cap, "%.1f min", s / 60);
    else if (s < 3 * 86400) snprintf(out, cap, "%.1f h", s / 3600);
    else if (s < 2 * 365.25 * 86400) snprintf(out, cap, "%.0f days", s / 86400);
    else snprintf(out, cap, "%.0f years", s / (365.25 * 86400));
}

int main(int argc, char **argv)
{
    const char *run = NULL, *label = NULL, *json = NULL, *macros_arg = NULL, *seeds_arg = NULL, *seedset_arg = NULL;
    int rounds = 1, n = 0, groups = 0, threads = 0, sample = 0, window = 0, stride = 0;
    double ep_seconds = 0, cam_w = BT_CAM_W, key_p = BT_KEY_P, every = 60;
    uint64_t seed = 1;
    const char *mopt_args[16];   /* lane/pearlgate: --mopt NAME=WORD, over the run file's */
    int nmopt = 0;
    int key_off = -1;            /* lane/ladder10: --key-off K, the policy's key K never held */
    for (int i = 1; i < argc; ++i)
    {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--macro-names"))
        {
            int32_t bit;
            for (int k = 0; neth_vec_macro_switch(k, &bit); ++k) printf("%s %d\n", neth_vec_macro_switch(k, NULL), bit);
            return 0;
        }
        if (!v) goto usage;
        ++i;
        if (!strcmp(a, "--run")) run = v;
        else if (!strcmp(a, "--macros")) macros_arg = v;
        else if (!strcmp(a, "--label")) label = v;
        else if (!strcmp(a, "--episodes")) rounds = atoi(v);
        else if (!strcmp(a, "--ep-seconds")) ep_seconds = atof(v);
        else if (!strcmp(a, "--n")) n = atoi(v);
        else if (!strcmp(a, "--groups")) groups = atoi(v);
        else if (!strcmp(a, "--threads")) threads = atoi(v);
        else if (!strcmp(a, "--seeds")) seeds_arg = v;
        else if (!strcmp(a, "--seed-set")) seedset_arg = v;
        else if (!strcmp(a, "--sample")) sample = atoi(v);
        else if (!strcmp(a, "--window"))
        {
            window = atoi(v);
            const char *colon = strchr(v, ':');
            stride = colon ? atoi(colon + 1) : 0;
        }
        else if (!strcmp(a, "--cam")) cam_w = atof(v);
        else if (!strcmp(a, "--keys")) key_p = atof(v);
        else if (!strcmp(a, "--seed")) seed = strtoull(v, NULL, 10);
        else if (!strcmp(a, "--every")) every = atof(v);
        else if (!strcmp(a, "--json")) json = v;
        else if (!strcmp(a, "--mopt") && nmopt < 16) mopt_args[nmopt++] = v;   /* lane/pearlgate */
        else if (!strcmp(a, "--key-off")) key_off = atoi(v);   /* lane/ladder10 */
        else goto usage;
    }
    if (!run)
    {
    usage:
        fprintf(stderr, "usage: brutetab --run FILE [--macros SPEC|BITS] [--label NAME] [--episodes R] [--ep-seconds S] "
                        "[--n N] [--groups G] [--threads T] [--seeds SPEC] [--seed-set DIR] [--sample K] "
                        "[--window W[:S]] [--cam W] [--keys P] [--seed K] [--every SEC] [--json FILE] "
                        "[--mopt NAME=WORD]... | --macro-names\n");
        return 2;
    }
    /* open files to the hard limit: a seed set's window loads and frees starts all run, and with the region spill's
     * files the soft 1024 ran out on the GPU host (a start "does not load" at the 4th round, 512 envs, window 32) */
    struct rlimit nof;
    if (getrlimit(RLIMIT_NOFILE, &nof) == 0 && nof.rlim_cur < nof.rlim_max)
    {
        nof.rlim_cur = nof.rlim_max;
        setrlimit(RLIMIT_NOFILE, &nof);
    }
    char err[2048];
    struct btrun rc = {0};
    if (bt_read_run(run, &rc, err, sizeof err))
    {
        printf("FAIL: %s\n", err);
        return 2;
    }
    const char *mspec = macros_arg ? macros_arg : rc.macro_spec;
    int macros = bt_macro_set(mspec, err, sizeof err);
    if (macros < 0)
    {
        printf("FAIL: %s\n", err);
        return 2;
    }
    if (!n) n = rc.worlds > 0 ? rc.worlds : 128;
    if (!groups) groups = rc.groups > 0 ? rc.groups : 4;
    if (!ep_seconds) ep_seconds = rc.ep_seconds > 0 ? rc.ep_seconds : 180;
    int ep_len = (int)lround(ep_seconds * 20);
    int camw = (int)cam_w, keyp = (int)lround(key_p * 1024);
    if (n % groups || camw < 0 || camw > NETH_VEC_CAM / 2 || rounds < 1)
    {
        printf("FAIL: N %d over %d groups, --cam %d (0 to %d), --episodes %d\n", n, groups, camw, NETH_VEC_CAM / 2, rounds);
        return 2;
    }

    /* the starts */
    const char *sspec = seeds_arg ? seeds_arg : rc.seeds;
    char **paths = NULL;
    int *pseed = NULL, np = 0, own = 0;
    char setdir[4096] = "";
    if (sspec[0])
    {
        static int sv[100000];
        int ns = cur_seeds(sspec, sv, 100000);
        if (ns <= 0)
        {
            printf("FAIL: seeds %s: A-B,C,...\n", sspec);
            return 2;
        }
        const char *ss = seedset_arg ? seedset_arg : rc.seed_set;
        if (ss[0]) snprintf(setdir, sizeof setdir, "%s", ss);
        else if (cur_find_seedset(run, setdir, sizeof setdir))
        {
            printf("FAIL: seeds %s: no out/java/try/seedset-* has %s's world (--seed-set DIR)\n", sspec, run);
            return 2;
        }
        paths = calloc((size_t)ns, sizeof *paths);
        pseed = calloc((size_t)ns, sizeof *pseed);
        for (int k = 0; k < ns; ++k)
        {
            char p[4096], m[4200];
            snprintf(p, sizeof p, "%s/s%d", setdir, sv[k]);
            snprintf(m, sizeof m, "%s/manifest.json", p);
            FILE *t = fopen(m, "r");
            if (!t && sv[k] == rc.world_seed)
            {
                /* the run's own seed: a seed set holds every seed but its world's (curric.h), start 0 is it */
                paths[np] = NULL;
                pseed[np++] = sv[k];
                continue;
            }
            if (!t)
            {
                printf("FAIL: seed %d: no %s\n", sv[k], m);
                return 2;
            }
            fclose(t);
            paths[np] = strdup(p);
            pseed[np++] = sv[k];
        }
        /* shuffled by --seed (Fisher-Yates), so a sample or a window's first rounds are seeds at random */
        uint64_t r = seed * 0x9e3779b97f4a7c15ull + 0x2545f4914f6cdd1dull;
        for (int k = np - 1; k > 0; --k)
        {
            int j = (int)(xs(&r) % (uint64_t)(k + 1));
            char *tp = paths[k];
            paths[k] = paths[j], paths[j] = tp;
            int ts = pseed[k];
            pseed[k] = pseed[j], pseed[j] = ts;
        }
        if (sample > 0 && sample < np) np = sample;
        /* the run's own seed in the list: out of the mix, start 0 drawn in its place (its share below) */
        for (int k = 0; k < np; ++k)
            if (!paths[k])
            {
                own = 1;
                for (int j = k; j + 1 < np; ++j) paths[j] = paths[j + 1], pseed[j] = pseed[j + 1];
                --np;
                break;
            }
    }
    double *sw = NULL;
    struct neth_vec_opts o;
    memset(&o, 0, sizeof o);
    o.env.config = run;
    o.env.n = n;
    o.env.threads = threads;
    o.env.device_mesh = -1;
    o.groups = groups;
    o.ep_len = ep_len;
    o.macros = macros;
    o.objective = NETH_VEC_FULL;
    o.ring = BT_RING;
    /* lane/pearlgate: the macro options, the run file's then --mopt's (a later NAME wins: vecenv.c mopt_parse) */
    for (int k = 0; k < nmopt; ++k)
    {
        const char *eq = strchr(mopt_args[k], '=');
        if (!eq)
        {
            printf("FAIL: --mopt %s is not NAME=WORD\n", mopt_args[k]);
            return 2;
        }
        char name[64];
        snprintf(name, sizeof name, "%.*s", (int)(eq - mopt_args[k]), mopt_args[k]);
        bt_mopt_add(rc.mopts, sizeof rc.mopts, name, eq + 1);
    }
    o.macro_opts = rc.mopts;
    if (rc.mopts[0]) printf("brutetab: macro options %s\n", rc.mopts);
    if (np)
    {
        sw = calloc((size_t)np + 1, sizeof *sw);
        for (int k = 1; k <= np; ++k) sw[k] = 1;
        o.starts = (const char *const *)paths;
        o.nstarts = np;
        o.start_weights = sw;
        o.flags |= NETH_RL_ANYSEED;
        if (window > 0 && window < np)
        {
            o.start_window = window;
            o.start_stride = stride > 0 ? stride : window / BT_WINDOW_STRIDE_DIV;
        }
        /* start 0, the run's own: never drawn unless its seed is in the list, then one seed's share (a draw takes
         * one of the W loaded mix starts or start 0, and a mix start is loaded W / np of the rounds: W / np keeps
         * start 0 at about 1 / (np + 1) of the draws, as every other seed) */
        int wn = o.start_window ? o.start_window : np;
        sw[0] = own ? (double)wn / (np + 1) : 0;
    }
    double t0 = now_s();
    struct neth_vec *v = neth_vec_make(&o, err, sizeof err);
    if (!v)
    {
        printf("FAIL: %s\n", err);
        return 1;
    }
    double t_make = now_s() - t0;
    struct neth_vec_bufs b;
    memset(&b, 0, sizeof b);
    neth_vec_buffers(v, &b);
    int aw = neth_vec_action_width(v);
    printf("brutetab: %s, macros %d (%s), N %d in %d groups, %d episode%s an env of %.0f s (%d steps), camera 20 +- %d, "
           "keys p %.3f, %s; made in %.1f s\n",
           run, macros, mspec[0] ? mspec : "none", n, groups, rounds, rounds > 1 ? "s" : "", ep_seconds, ep_len, camw,
           keyp / 1024.0, np ? "a seed set's starts" : "the run's start", t_make);
    if (np) printf("starts: %d of %s (shuffled by --seed %llu)%s\n", np, setdir, (unsigned long long)seed,
                   o.start_window ? ", windowed" : ", all loaded");
    fflush(stdout);

    uint64_t *rng = malloc((size_t)n * sizeof *rng);
    for (int i = 0; i < n; ++i) rng[i] = (seed * 0x9e3779b97f4a7c15ull) ^ ((uint64_t)(i + 1) * 0xbf58476d1ce4e5b9ull);
    int *eps = calloc((size_t)n, sizeof *eps);
    int64_t hit[BT_NCOL] = {0}, deaths = 0, ndone = 0, died = 0;
    uint8_t *played = calloc((size_t)np + 2, 1);
    static struct neth_vec_episode rec[BT_RING];
    int gs = n / groups, live = groups;
    uint8_t *gdone = calloc((size_t)groups, 1);
    double s0[8], s1[8], tstart = now_s(), tlast = tstart;
    neth_vec_stats(v, s0);
    int fail = 0;
    while (live > 0)
    {
        int g = neth_vec_recv(v, err, sizeof err);
        if (g < 0)
        {
            printf("FAIL: %s\n", err);
            fail = 1;
            break;
        }
        int ne = neth_vec_episodes(v, rec, BT_RING);
        for (int k = 0; k < ne; ++k)
        {
            const struct neth_vec_episode *e = &rec[k];
            if (e->env < 0 || e->env >= n || eps[e->env] >= rounds) continue;
            ++eps[e->env];
            ++ndone;
            for (int c = 0; c < BT_NCOL; ++c) hit[c] += bt_has(c, e);
            deaths += e->deaths;
            died += e->deaths > 0;
            if (e->start >= 0 && e->start <= np + 1) played[e->start] = 1;
        }
        int all = 1;
        for (int i = g * gs; i < (g + 1) * gs; ++i) all &= eps[i] >= rounds;
        if (all)
        {
            if (!gdone[g]) gdone[g] = 1, --live;
            continue;
        }
        for (int i = g * gs; i < (g + 1) * gs; ++i)
        {
            int32_t *a = b.actions + (size_t)i * aw;
            memset(a, 0, (size_t)aw * sizeof *a);
            for (int j = 0; j < 2; ++j) a[j] = NETH_VEC_CAM / 2 - camw + (int)(xs(&rng[i]) % (uint64_t)(2 * camw + 1));
            for (int j = 0; j < NETH_VEC_KEYS; ++j) a[2 + j] = (int)(xs(&rng[i]) & 1023) < keyp;
            if (key_off >= 0 && key_off < NETH_VEC_KEYS) a[2 + key_off] = 0;
        }
        if (neth_vec_send(v, g))
        {
            printf("FAIL: send %d refused\n", g);
            fail = 1;
            break;
        }
        double t = now_s();
        if (every > 0 && t - tlast >= every)
        {
            tlast = t;
            double st[8];
            neth_vec_stats(v, st);
            struct rusage ru;
            getrusage(RUSAGE_SELF, &ru);
            printf("  %.0f s: %.0f env-steps/s, %.0f of %lld env-steps, %lld episodes; wooden pickaxe %lld, iron "
                   "pickaxe %lld, diamond pickaxe %lld, nether %lld, end %lld; peak rss %.1f GB\n",
                   t - tstart, (st[0] - s0[0]) / (t - tstart), st[0] - s0[0], (long long)n * rounds * ep_len,
                   (long long)ndone, (long long)hit[1], (long long)hit[5], (long long)hit[8], (long long)hit[10],
                   (long long)hit[15], ru.ru_maxrss / 1048576.0);
            fflush(stdout);
        }
    }
    double wall = now_s() - tstart;
    neth_vec_stats(v, s1);
    double es = s1[0] - s0[0], esps = es / (wall > 0 ? wall : 1);
    int nplayed = 0;
    for (int k = 0; k <= np + 1; ++k) nplayed += played[k];
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    char lbl[600];
    snprintf(lbl, sizeof lbl, "%s", label ? label : mspec[0] ? mspec : "none");
    printf("row %s, macros %d: %lld episodes of %.0f s from %s (%d distinct), %.0f env-steps/s (%.0f env-steps in "
           "%.1f s), %.2f deaths a run, %.1f%% with a death; peak rss %.1f GB%s\n",
           lbl, macros, (long long)ndone, ep_seconds, np ? "the seed set" : "the run's start", np ? nplayed : 1, esps,
           es, wall, ndone ? (double)deaths / ndone : 0, ndone ? 100.0 * died / ndone : 0, ru.ru_maxrss / 1048576.0,
           fail ? " (FAILED: partial)" : "");
    /* one success expected: 1 / (rate x episodes a second), episodes a second = env-steps/s / ep_len */
    char md[4096];
    int mp = snprintf(md, sizeof md, "| %s | %lld | %.0f |", lbl, (long long)ndone, esps);
    for (int c = 0; c < BT_NCOL; ++c)
    {
        double rate = ndone ? (double)hit[c] / ndone : 0, w[2];
        char d[64];
        if (hit[c])
        {
            w[0] = ep_len / (rate * esps);
            bt_dur(d, sizeof d, w[0]);
            printf("  %-16s %6lld  %7.3f%%  a success in %s of this host's CPU\n", bt_col_name(c), (long long)hit[c],
                   100 * rate, d);
            mp += snprintf(md + mp, sizeof md - (size_t)mp, " %.3g%% (%s) |", 100 * rate, d);
        }
        else
        {
            w[1] = ndone ? ep_len / (3.0 / ndone * esps) : 0;
            bt_dur(d, sizeof d, w[1]);
            printf("  %-16s %6d  %7s   none seen (95%%: rate under %.3g%%, a success over %s)\n", bt_col_name(c), 0,
                   "0", ndone ? 300.0 / ndone : 0, d);
            mp += snprintf(md + mp, sizeof md - (size_t)mp, " 0 (> %s) |", d);
        }
    }
    printf("markdown %s\n", md);
    if (json)
    {
        FILE *f = fopen(json, "a");
        if (f)
        {
            fprintf(f, "{\"label\": \"%s\", \"run\": \"%s\", \"macros\": %d, \"episodes\": %lld, \"ep_seconds\": %.0f, "
                       "\"env_steps_s\": %.1f, \"env_steps\": %.0f, \"wall\": %.1f, \"threads\": %d, \"starts\": %d, "
                       "\"distinct\": %d, \"cam\": %d, \"keys\": %.4f, \"seed\": %llu, \"deaths\": %lld, \"died\": %lld, "
                       "\"failed\": %d, \"hits\": {",
                    lbl, run, macros, (long long)ndone, ep_seconds, esps, es, wall, threads, np, np ? nplayed : 1, camw,
                    keyp / 1024.0, (unsigned long long)seed, (long long)deaths, (long long)died, fail);
            for (int c = 0; c < BT_NCOL; ++c) fprintf(f, "%s\"%s\": %lld", c ? ", " : "", bt_col_name(c), (long long)hit[c]);
            fprintf(f, "}}\n");
            fclose(f);
        }
    }
    {
        /* lane/ladder10: actions.gui cursor's books (vecenv.h neth_vec_gui_books); a label that decodes to another op
         * fails the row */
        int64_t gb[NETH_VEC_GUI_BOOKS];
        if (neth_vec_gui_books(v, gb) > 0)
        {
            printf("gui: macro GUI steps %lld, said by the action %lld, decoded back equal %lld, unequal %lld; the "
                   "policy's steps under a screen %lld, clicks %lld, closes %lld, inventory opens %lld\n",
                   (long long)gb[0], (long long)gb[1], (long long)gb[2], (long long)gb[3], (long long)gb[4],
                   (long long)gb[5], (long long)gb[6], (long long)gb[7]);
            if (gb[3]) fail = 1;
            /* lane/ladder15: NETH_VEC_TABLEKEY's starts, the recipe's (ladder13's books [8] to [11]) */
            printf("gui table: the table half started by the key 2x2 %lld, 3x3 %lld, smelt %lld; the policy's steps "
                   "with no screen and a crafting plan named %lld (3x3 or smelt %lld); recipe starts 2x2 %lld, 3x3 %lld, "
                   "smelt %lld, hand-offs at the start %lld\n",
                   (long long)gb[34], (long long)gb[35], (long long)gb[36], (long long)gb[37], (long long)gb[38],
                   (long long)gb[8], (long long)gb[9], (long long)gb[10], (long long)gb[11]);
        }
        /* lane/ladder16: NETH_VEC_SEEKKEY's books (any gui option), printed where any is counted */
        int64_t sk = 0;
        if (neth_vec_gui_books(v, gb) >= 0)
            for (int k = 39; k < 50; ++k) sk += gb[k];
        if (sk)
            printf("seek key: started on iron ore %lld, diamond ore %lld, a log %lld; ended with the item %lld, dug and "
                   "not picked up %lld, given up %lld, by a death or the lava escape %lld; its steps %lld; the policy's "
                   "steps with the start holding %lld (iron ore %lld), key 13 down with none %lld\n",
                   (long long)gb[39], (long long)gb[40], (long long)gb[41], (long long)gb[42], (long long)gb[43],
                   (long long)gb[44], (long long)gb[45], (long long)gb[46], (long long)gb[47], (long long)gb[48],
                   (long long)gb[49]);
        /* lane/ladder18: the seek's steps by half and the split's books (actions.seek_walk, seek_dig) */
        if (neth_vec_gui_books(v, gb) >= 0 && (gb[50] || gb[51] || gb[52] || gb[53] || gb[54]))
            printf("seek half: steps in the walk half %lld, in the dig half %lld; key 13 down with the start in the "
                   "policy's half %lld; the key's seeks ended as the cell left the macro's half %lld; the policy's "
                   "steps with the start in its own half %lld\n",
                   (long long)gb[51], (long long)gb[52], (long long)gb[50], (long long)gb[53], (long long)gb[54]);
        /* lane/ladder17: actions.recipe_2x2 clicks' books (vecenv.h [55] to [65]); a label that decodes to another op
         * fails the row */
        if (neth_vec_gui_books(v, gb) > 0 && gb[57] > 0)
        {
            printf("x2: the policy's 2x2 crafts %lld (the result picked up by a plain click %lld); its steps under the "
                   "inventory %lld, with no op %lld, clicks %lld; the labeller's runs started %lld, followed to the "
                   "result %lld, left %lld; steps it says the close %lld, closed %lld; decoded back unequal %lld\n",
                   (long long)gb[55], (long long)gb[56], (long long)gb[57], (long long)gb[58], (long long)gb[59],
                   (long long)gb[60], (long long)gb[61], (long long)gb[62], (long long)gb[63], (long long)gb[64],
                   (long long)gb[65]);
            if (gb[65]) fail = 1;
        }
    }
    fflush(stdout);
    neth_vec_free(v);
    return fail;
}
