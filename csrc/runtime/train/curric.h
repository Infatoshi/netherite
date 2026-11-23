/* The trainer's starts (lane/starts): the seed set, the library of starts (train/starts.c) and the curriculum over
 * them, for ntloop (its modules include it; every function static inline).
 *
 * The mix (neth_vec_opts.starts): start 0 the sim's own (its pipeline.start), then each seed of starts.seeds but the
 * sim's own (the oracle's fresh world of that seed: out/java/try/seedset-KEY/sN, the seed set whose world.yaml is the
 * sim's world but its seed, captured once, lane/seeds; starts.seed_set DIR names one), then each library start of
 * starts.library (LIB/index.tsv) kept by starts.stages (a '+' list of stages.h names) and starts.library_seeds. Every
 * start is loaded at the make (no window: a start is about 50 MB resident). With a seed set start 0 plays only when the
 * sim's seed is in it.
 *
 * Each reset draws a stage by the stage weights, then one of its starts (neth_vec_start_weights: a stage's weight
 * spread equally over its starts). The weights (starts.curriculum):
 *   uniform    every stage present the same share
 *   adaptive   (the default with a library) the policy's own results: each stage's episodes since the last row,
 *              where the rate of reaching its next milestone (stages.h stage_next: the first chain entry past the
 *              start's stage it does not hold) is r now and r0 at the last row with episodes, scored (1 - r) + 2 |r -
 *              r0| (low or still moving gets more), a stage with fewer than starts.min_eps episodes keeping its last
 *              score (1 before any); each share floor + (1 - floor * S) * score / sum, then spawn raised to at least
 *              starts.spawn_min (the others scaled down)
 *   off        the mix's equal shares per start (lane/mixstart's), the stages ignored
 * The weights, each stage's episodes, rate and score go in iters.jsonl's "curriculum" each row.
 *
 * lane/stagelen, each off by default: starts.episode_seconds ("STAGE/SECONDS,...") gives a stage's starts their own
 * episode length (neth_vec_opts.start_ep_len; a stage not named plays pipeline.episode_seconds), so a late start gets
 * the time its step takes; starts.share steps divides each start's draw by its length, so the weights above are
 * shares of env-steps (resets, the default: shares of episodes); starts.clock left puts the time left in the
 * observation's clock (neth_vec_opts.clock). The score (--score) plays one length, its --minutes. */
#ifndef NETHERITE_RUNTIME_TRAIN_CURRIC_H
#define NETHERITE_RUNTIME_TRAIN_CURRIC_H

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "stages.h"

enum { CUR_OFF, CUR_UNIFORM, CUR_ADAPTIVE };

struct curric {
    int mode, nstart;           /* nstart: the mix's starts besides start 0 */
    char **path;                /* [nstart] */
    int *stage, *dim, *seed;    /* [nstart + 1]: each start's stage, dimension and world seed */
    uint32_t *ms;               /* [nstart + 1]: the milestones it holds */
    int start0;                 /* start 0 in the draws */
    double floor, spawn_min;
    int min_eps;
    int cnt[STAGE_N];           /* starts a stage */
    double w[STAGE_N], score[STAGE_N], rate[STAGE_N], rate0[STAGE_N];
    int64_t n[STAGE_N], hit[STAGE_N], dead[STAGE_N], tot_n[STAGE_N], tot_hit[STAGE_N];
    int has_rate[STAGE_N];
    double *sw;                 /* [nstart + 1] */
    /* lane/stagelen: starts.episode_seconds (each stage's seconds an episode, 0: pipeline.episode_seconds) and, with
     * starts.share steps, len[nstart + 1] each start's episode length in ticks (NULL: the shares are of resets) */
    double stage_sec[STAGE_N];
    double *len;
    /* the eval's table (score mode): per stage, each chain entry's episodes reaching it, deaths */
    int64_t chain[STAGE_N][STAGE_N], deaths[STAGE_N];
    /* lane/curric2: each stage's books, the row's (the episodes since the last row the stage was scored at: their
     * mean return, the share with a death) and the running return's (ntloop's ret_run of the envs playing from the
     * stage, every env every step: its Welford mean and spread, the scale ppo.stage_scale divides the stage's rewards
     * by) */
    double ret_sum[STAGE_N], row_ret[STAGE_N], row_dead[STAGE_N], tot_ret[STAGE_N];
    double rw_n[STAGE_N], rw_mean[STAGE_N], rw_m2[STAGE_N];
};

/* a stage's running-return scale: fastppo's scale() on the stage's own returns (its spread, at least 1, 1 until 100
 * samples) */
static inline double cur_scale(const struct curric *C, int j)
{
    double n = C->rw_n[j];
    double sd = n > 100 ? sqrt(C->rw_m2[j] / (n - 1 > 1 ? n - 1 : 1)) : 1.0;
    return sd > 1.0 ? sd : 1.0;
}

/* an env's running return x while it plays from start k into its stage's spread; the stage (0: spawn, or a start
 * outside the mix: a saved one) */
static inline int cur_stage_of(const struct curric *C, int k) { return k >= 0 && k <= C->nstart ? C->stage[k] : 0; }

static inline void cur_ret(struct curric *C, int j, double x)
{
    double d = x - C->rw_mean[j];
    C->rw_n[j] += 1;
    C->rw_mean[j] += d / C->rw_n[j];
    C->rw_m2[j] += d * (x - C->rw_mean[j]);
}

/* "A-B,C,..." into seeds[] (at most cap), the count, -1 on a bad spec */
static inline int cur_seeds(const char *spec, int *seeds, int cap)
{
    char b[4096];               /* lane/ppo-ac: 512 cut a list of fresh seeds (10 digits each) after 46 */
    if (snprintf(b, sizeof b, "%s", spec) >= (int)sizeof b) return -1;
    int n = 0;
    for (char *sv, *t = strtok_r(b, ",", &sv); t; t = strtok_r(NULL, ",", &sv))
    {
        char *e;
        long a = strtol(t, &e, 10), z = a;
        if (e == t) return -1;
        if (*e == '-') z = strtol(e + 1, &e, 10);
        if (*e || z < a) return -1;
        for (long k = a; k <= z && n < cap; ++k) seeds[n++] = (int)k;
    }
    return n;
}

static inline int cur_has_seed(const int *seeds, int n, int s)
{
    for (int i = 0; i < n; ++i)
        if (seeds[i] == s) return 1;
    return 0;
}

/* the world section of a YAML file without its seed line, lines trimmed, into out */
static inline void cur_world_key(const char *path, char *out, size_t cap)
{
    out[0] = 0;
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[512];
    int in = 0;
    size_t p = 0;
    while (fgets(line, sizeof line, f))
    {
        char *s = line;
        size_t l = strlen(s);
        while (l && (s[l - 1] == '\n' || s[l - 1] == ' ' || s[l - 1] == '\r')) s[--l] = 0;
        char *h = strstr(s, " #");
        if (h) *h = 0;
        if (!l) continue;
        int ind = 0;
        while (s[ind] == ' ') ++ind;
        if (!strcmp(s, "sim:")) continue;
        if (ind <= 2 && s[l - 1] == ':' && !strchr(s + ind, ' '))
        {
            in = !strcmp(s + ind, "world:");
            continue;
        }
        if (!in) continue;
        char *k = s + ind;
        if (!strncmp(k, "seed:", 5)) continue;
        /* the value without its spaces */
        char kv[256];
        size_t q = 0;
        for (char *c = k; *c && q + 1 < sizeof kv; ++c)
            if (*c != ' ') kv[q++] = *c;
        kv[q] = 0;
        p += (size_t)snprintf(out + p, cap > p ? cap - p : 0, "%s;", kv);
    }
    fclose(f);
}

/* the seed set whose world is config's: ROOT/out/java/try/seedset-* /s1/world.yaml (any sN of it) */
static inline int cur_find_seedset(const char *config, char *out, size_t cap)
{
    char want[2048], have[2048];
    cur_world_key(config, want, sizeof want);
    const char *roots[2] = {"out/java/try", NULL};
    char exe[4096];
    ssize_t l = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (l > 0)
    {
        exe[l] = 0;
        /* out/runtime/train[/V]/ntloop: the tree's out/java/try */
        char *s = strstr(exe, "/out/runtime/");
        if (s) strcpy(s, "/out/java/try"), roots[1] = exe;
    }
    for (int r = 0; r < 2; ++r)
    {
        if (!roots[r]) continue;
        DIR *d = opendir(roots[r]);
        if (!d) continue;
        struct dirent *e;
        while ((e = readdir(d)))
        {
            if (strncmp(e->d_name, "seedset-", 8)) continue;
            char p[4096];
            snprintf(p, sizeof p, "%s/%s/s1/world.yaml", roots[r], e->d_name);
            cur_world_key(p, have, sizeof have);
            if (have[0] && !strcmp(have, want))
            {
                snprintf(out, cap, "%s/%s", roots[r], e->d_name);
                closedir(d);
                return 0;
            }
        }
        closedir(d);
    }
    return -1;
}

static inline void cur_add(struct curric *C, const char *path, int stage, uint32_t ms, int dim, int seed)
{
    int k = ++C->nstart;
    C->path = realloc(C->path, (size_t)k * sizeof *C->path);
    C->stage = realloc(C->stage, (size_t)(k + 1) * sizeof *C->stage);
    C->dim = realloc(C->dim, (size_t)(k + 1) * sizeof *C->dim);
    C->seed = realloc(C->seed, (size_t)(k + 1) * sizeof *C->seed);
    C->ms = realloc(C->ms, (size_t)(k + 1) * sizeof *C->ms);
    char *abs = realpath(path, NULL);   /* absolute: the recording's header names it (ntrec tape) */
    C->path[k - 1] = abs ? abs : strdup(path);
    C->stage[k] = stage, C->ms[k] = ms, C->dim[k] = dim, C->seed[k] = seed;
}

static inline uint32_t cur_ms_names(const char *s)
{
    uint32_t m = 0;
    char b[512];
    snprintf(b, sizeof b, "%s", s);
    for (char *sv, *t = strtok_r(b, "+", &sv); t; t = strtok_r(NULL, "+", &sv))
        for (int i = 0; i < NETH_VEC_MILESTONES; ++i)
            if (!strcmp(t, STAGE_MS_NAME[i])) m |= 1u << i;
    return m;
}

/* the mix: the seed set's starts, then the library's; 0 or -1 with err */
static inline int cur_build(struct curric *C, const char *config, int sim_seed, const char *seeds_spec, const char *seedset,
                     const char *lib, const char *stages, const char *lib_seeds, const char *mode, char *err, size_t en)
{
    memset(C, 0, sizeof *C);
    C->stage = calloc(1, sizeof *C->stage), C->dim = calloc(1, sizeof *C->dim), C->seed = calloc(1, sizeof *C->seed);
    C->ms = calloc(1, sizeof *C->ms);
    C->seed[0] = sim_seed;
    C->start0 = 1;
    C->floor = 0.05, C->spawn_min = 0.25, C->min_eps = 16;
    C->mode = !mode || !*mode ? (lib && *lib ? CUR_ADAPTIVE : CUR_OFF)
              : !strcmp(mode, "adaptive") ? CUR_ADAPTIVE : !strcmp(mode, "uniform") ? CUR_UNIFORM
              : !strcmp(mode, "off") ? CUR_OFF : -1;
    if (C->mode < 0) return snprintf(err, en, "starts.curriculum is adaptive, uniform or off, not %s", mode), -1;
    static int seeds[4096], ls[4096];
    int ns = 0, nls = 0;
    if (seeds_spec && *seeds_spec)
    {
        if ((ns = cur_seeds(seeds_spec, seeds, 4096)) <= 0) return snprintf(err, en, "starts.seeds %s: A-B,C,...", seeds_spec), -1;
        C->start0 = cur_has_seed(seeds, ns, sim_seed);
        char dir[4096];
        if (seedset && *seedset) snprintf(dir, sizeof dir, "%s", seedset);
        else if (cur_find_seedset(config, dir, sizeof dir))
            return snprintf(err, en, "starts.seeds: no out/java/try/seedset-* has %s's world (capture it: mcsr's "
                            "seedset, lane/seeds; or name one, starts.seed_set)", config), -1;
        for (int i = 0; i < ns; ++i)
        {
            if (seeds[i] == sim_seed) continue;
            char p[4200];
            snprintf(p, sizeof p, "%s/s%d", dir, seeds[i]);
            char m[4300];
            snprintf(m, sizeof m, "%s/manifest.json", p);
            if (access(m, R_OK)) return snprintf(err, en, "starts.seeds: seed %d: no start %.400s", seeds[i], p), -1;
            cur_add(C, p, 0, 0, 0, seeds[i]);
        }
    }
    if (lib_seeds && *lib_seeds && (nls = cur_seeds(lib_seeds, ls, 4096)) <= 0)
        return snprintf(err, en, "starts.library_seeds %s: A-B,C,...", lib_seeds), -1;
    if (lib && *lib)
    {
        char ip[4096];
        snprintf(ip, sizeof ip, "%s/index.tsv", lib);
        FILE *f = fopen(ip, "r");
        if (!f) return snprintf(err, en, "starts.library %s: no index.tsv (ntstarts index)", lib), -1;
        char line[8192];
        int ln = 0, kept = 0;
        while (fgets(line, sizeof line, f))
        {
            if (ln++ == 0) continue;
            char *col[9] = {0}, *sv = NULL;
            int nc = 0;
            for (char *t = strtok_r(line, "\t\n", &sv); t && nc < 9; t = strtok_r(NULL, "\t\n", &sv)) col[nc++] = t;
            if (nc < 9) continue;
            int st = stage_by_name(col[2]), sd = atoi(col[1]);
            if (st < 0) { fclose(f); return snprintf(err, en, "%.400s:%d: no stage %.60s", ip, ln, col[2]), -1; }
            if (stages && *stages)
            {
                char b[512];
                snprintf(b, sizeof b, "+%s+", stages);
                char w[64];
                snprintf(w, sizeof w, "+%s+", col[2]);
                if (!strstr(b, w)) continue;
            }
            if (nls && !cur_has_seed(ls, nls, sd)) continue;
            int dim = !strcmp(col[4], "nether") ? -1 : !strcmp(col[4], "end") ? 1 : 0;
            cur_add(C, col[8], st, cur_ms_names(col[3]), dim, sd);
            ++kept;
        }
        fclose(f);
        if (!kept) return snprintf(err, en, "starts.library %s: no start kept (stages %s, seeds %s)", lib,
                                   stages ? stages : "all", lib_seeds ? lib_seeds : "all"), -1;
    }
    /* start 0 in a stage filter: only when spawn is one */
    if (stages && *stages)
    {
        char b[512];
        snprintf(b, sizeof b, "+%s+", stages);
        if (!strstr(b, "+spawn+")) C->start0 = 0;
    }
    for (int k = 0; k <= C->nstart; ++k)
        if (k || C->start0) ++C->cnt[C->stage[k]];
    for (int j = 0; j < STAGE_N; ++j) C->score[j] = 1.0;
    C->sw = calloc((size_t)C->nstart + 1, sizeof *C->sw);
    return 0;
}

/* the stage weights from the scores (CUR_UNIFORM: every score 1), then each start's */
static inline void cur_weights(struct curric *C)
{
    int S = 0;
    double tot = 0.0;
    for (int j = 0; j < STAGE_N; ++j)
        if (C->cnt[j]) ++S, tot += C->mode == CUR_ADAPTIVE ? C->score[j] : 1.0;
    for (int j = 0; j < STAGE_N; ++j)
    {
        double sc = C->mode == CUR_ADAPTIVE ? C->score[j] : 1.0;
        C->w[j] = !C->cnt[j] ? 0.0 : C->mode == CUR_ADAPTIVE && S > 1 ? C->floor + (1.0 - C->floor * S) * sc / tot
                                                                     : 1.0 / S;
    }
    if (C->mode == CUR_ADAPTIVE && C->cnt[0] && S > 1 && C->w[0] < C->spawn_min)
    {
        double rest = 1.0 - C->w[0];
        for (int j = 1; j < STAGE_N; ++j) C->w[j] *= (1.0 - C->spawn_min) / rest;
        C->w[0] = C->spawn_min;
    }
    for (int k = 0; k <= C->nstart; ++k)
        C->sw[k] = (!k && !C->start0) ? 0.0 : C->mode == CUR_OFF ? 1.0 : C->w[C->stage[k]] / C->cnt[C->stage[k]];
    /* lane/stagelen (starts.share steps): each start's draw over its episode's length, so the shares are of
     * env-steps, not of resets */
    for (int k = 0; C->len && k <= C->nstart; ++k) C->sw[k] /= C->len[k];
}

/* lane/stagelen: starts.episode_seconds, "STAGE/SECONDS,..." (or STAGE=SECONDS; stages.h names), into stage_sec; then
 * each start's episode length in ticks into len[nstart + 1] (ep_len, the sim's, where its stage has none); 0, or -1
 * with err */
static inline int cur_stage_len(struct curric *C, const char *spec, int ep_len, int32_t *len, char *err, size_t en)
{
    char b[1024];
    snprintf(b, sizeof b, "%s", spec);
    for (char *sv, *t = strtok_r(b, ",", &sv); t; t = strtok_r(NULL, ",", &sv))
    {
        char *eq = strpbrk(t, "=/"), *e = NULL;
        if (eq) *eq = 0;
        int j = stage_by_name(t);
        double s = eq ? strtod(eq + 1, &e) : 0.0;
        if (!eq || j < 0 || e == eq + 1 || *e || !(s > 0.0) || s * 20.0 > 2e9)
        {
            snprintf(err, en, "starts.episode_seconds %s: STAGE/SECONDS,..., STAGE a stage (stages.h), SECONDS over 0",
                     spec);
            return -1;
        }
        C->stage_sec[j] = s;
    }
    for (int k = 0; k <= C->nstart; ++k)
    {
        double s = C->stage_sec[C->stage[k]];
        len[k] = s > 0.0 ? (int32_t)(s * 20.0) : ep_len;
    }
    return 0;
}

/* an episode's books: its start's stage, the next milestone reached, its deaths, and the chain entries it reached */
static inline void cur_episode(struct curric *C, const struct neth_vec_episode *e)
{
    int k = e->start;
    if (k < 0 || k > C->nstart) return;
    int st = C->stage[k];
    int nx = stage_next(st, C->ms[k], C->dim[k]);
    int dims = e->dims;
    int got = nx < STAGE_N && stage_has(nx, e->ms, 99, dims);
    ++C->n[st], ++C->tot_n[st];
    C->hit[st] += got, C->tot_hit[st] += got;
    C->dead[st] += e->deaths > 0;
    C->deaths[st] += e->deaths;
    C->ret_sum[st] += e->ret;
    C->tot_ret[st] += e->ret;
    for (int j = 0; j < STAGE_N; ++j) C->chain[st][j] += stage_has(j, e->ms, 99, dims);
}

/* a row: each stage's rate and score from the episodes since the last row, the weights moved (adaptive) */
static inline void cur_row(struct curric *C)
{
    for (int j = 0; j < STAGE_N; ++j)
    {
        if (!C->cnt[j] || C->n[j] < C->min_eps) continue;
        double r = (double)C->hit[j] / (double)C->n[j];
        C->score[j] = (1.0 - r) + 2.0 * (C->has_rate[j] ? fabs(r - C->rate[j]) : 0.0);
        C->rate0[j] = C->has_rate[j] ? C->rate[j] : r;
        C->rate[j] = r, C->has_rate[j] = 1;
        C->row_ret[j] = C->ret_sum[j] / (double)C->n[j];
        C->row_dead[j] = (double)C->dead[j] / (double)C->n[j];
        C->n[j] = C->hit[j] = C->dead[j] = 0;
        C->ret_sum[j] = 0.0;
    }
    if (C->mode != CUR_OFF) cur_weights(C);
}

/* the stage's next chain entry by name ("-" past the last) */
static inline const char *cur_next_name(int j) { return j == STAGE_N - 1 ? "-" : STAGE_CHAIN[stage_next(j, 0, 0)].name; }

static inline void cur_json(const struct curric *C, FILE *f)
{
    fputs(", \"curriculum\": {", f);
    int any = 0;
    for (int j = 0; j < STAGE_N; ++j)
    {
        if (!C->cnt[j]) continue;
        fprintf(f, "%s\"%s\": {\"w\": %.4f, \"starts\": %d, \"episodes\": %lld, \"next\": \"%s\", \"rate\": %.4f, "
                "\"score\": %.4f, \"ret\": %.4f, \"dead\": %.4f, \"ret_all\": %.4f, \"deaths_all\": %.4f, "
                "\"ret_mean\": %.4f, \"ret_sd\": %.4f}", any++ ? ", " : "",
                STAGE_CHAIN[j].name, C->w[j], C->cnt[j], (long long)C->tot_n[j], cur_next_name(j),
                C->has_rate[j] ? C->rate[j] : -1.0, C->score[j], C->has_rate[j] ? C->row_ret[j] : 0.0,
                C->has_rate[j] ? C->row_dead[j] : -1.0, C->tot_n[j] ? C->tot_ret[j] / (double)C->tot_n[j] : 0.0,
                C->tot_n[j] ? (double)C->deaths[j] / (double)C->tot_n[j] : 0.0, C->rw_mean[j], cur_scale(C, j));
    }
    fputs("}", f);
}

/* the eval's per-stage table: runs, the next milestone's rate (the stage's first next entry), deaths a run, and each
 * later chain entry's share */
static inline void cur_table(const struct curric *C, FILE *f, FILE *js)
{
    fprintf(f, "%-16s %5s %-16s %6s %6s  %s\n", "stage", "runs", "next", "rate", "deaths", "the chain past it (share of runs)");
    if (js) fputs("{\"stages\": {", js);
    int anyj = 0;
    for (int j = 0; j < STAGE_N; ++j)
    {
        if (!C->cnt[j] || !C->tot_n[j]) continue;
        int nx = stage_next(j, 0, 0);
        double n = (double)C->tot_n[j];
        fprintf(f, "%-16s %5lld %-16s %6.3f %6.3f ", STAGE_CHAIN[j].name, (long long)C->tot_n[j], cur_next_name(j),
                C->tot_hit[j] / n, C->deaths[j] / n);
        if (js)
            fprintf(js, "%s\"%s\": {\"runs\": %lld, \"next\": \"%s\", \"rate\": %.4f, \"deaths\": %.4f, \"chain\": {",
                    anyj++ ? ", " : "", STAGE_CHAIN[j].name, (long long)C->tot_n[j], cur_next_name(j),
                    C->tot_hit[j] / n, C->deaths[j] / n);
        int anyc = 0;
        for (int q = (j == 16 || j == 17) ? 15 : j + 1; q < STAGE_N; ++q)
        {
            if (q == j) continue;
            if (!C->chain[j][q] && q > nx) continue;
            fprintf(f, " %s %.3f", STAGE_CHAIN[q].name, C->chain[j][q] / n);
            if (js) fprintf(js, "%s\"%s\": %.4f", anyc++ ? ", " : "", STAGE_CHAIN[q].name, C->chain[j][q] / n);
        }
        fputs("\n", f);
        if (js) fputs("}}", js);
    }
    if (js) fputs("}}\n", js);
}

#endif
