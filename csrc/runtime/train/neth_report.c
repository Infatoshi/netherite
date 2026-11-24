/* ntloop's reports: episodes.jsonl and iters.jsonl as fastppo.py writes them, the frozen eval's table (--score), the
 * score's books (--score-books) and the view books (--view-books). */
#define _GNU_SOURCE
#include <sys/stat.h>

#include "neth_pipe_internal.h"
#include "neth_runconf.h"
#include "curric.h"

static const int EP_ITEMS[NETH_VEC_ITEMS] = {17, 162, 5, 280, 58, 270, 271, 269, 268, 4, 274, 275, 273, 272, 263, 15,
                                             265, 61, 3, 12, 13, 6, 260, 295, 318, 1, 14, 264, 56, 331, 325, 259};
static const int EXTRA_ITEMS[8] = {326, 327, 49, 257, 278, 369, 368, 381};
static const int SEEN_IDS[4] = {15, 56, 112, 17};
const char *const neth_ms_names[NETH_VEC_MILESTONES] = {
    "wooden_pickaxe", "stone_pickaxe", "iron_pickaxe", "diamond_pickaxe", "furnace", "iron_ingot", "bucket",
    "flint_and_steel", "obsidian", "blaze_rod", "blaze_powder", "ender_pearl", "ender_eye"};
static const char *const MACRO_NAMES[NETH_VEC_MACROS] = {"none", "surface", "screen", "loot", "mine", "craft"};
const char *const neth_t_names[T_N] = {"recv", "host", "launch", "sync", "send", "tail"};

/* vecenv.episode_dicts' record, then env_steps, then (with the ladder, rung not INT_MIN) the rung it ran */
void neth_report_episode(FILE *f, const struct neth_vec_episode *e, int64_t steps, int rung)
{
    fprintf(f, "{\"env\": %d, \"logs\": %d, \"crafted\": %d, \"ret\": ", e->env, e->logs, e->crafted);
    jnum(f, e->ret);
    fputs(", \"value\": ", f);
    jnum(f, (double)e->value);
    fprintf(f, ", \"ach\": %d, \"deaths\": %d, \"net\": ", e->ach, e->deaths);
    jnum(f, e->net);
    fputs(", \"ms\": {", f);
    for (int i = 0; i < NETH_VEC_MILESTONES; ++i)
        fprintf(f, "%s\"%s\": %u", i ? ", " : "", neth_ms_names[i], (e->ms >> i) & 1);
    /* the End entered and the dragon killed (vecenv.h NETH_VEC_MS_END, NETH_VEC_MS_DRAGON) */
    fprintf(f, ", \"end\": %u, \"dragon\": %u", (e->ms >> NETH_VEC_MS_END) & 1, (e->ms >> NETH_VEC_MS_DRAGON) & 1);
    /* lane/audit-fixes: the task won (vecenv.h neth_vec_episode.win), a milestone to nether eval (not in its depth) */
    fprintf(f, ", \"win\": %d", e->win ? 1 : 0);
    fputs("}, \"macro\": {", f);
    for (int i = 0; i < NETH_VEC_MACROS; ++i) fprintf(f, "%s\"%s\": %lld", i ? ", " : "", MACRO_NAMES[i], (long long)e->macro[i]);
    fputs("}, \"t\": ", f);
    jnum(f, e->t);
    fprintf(f, ", \"start\": %d, \"len\": %lld, \"items\": {", e->start, (long long)e->steps);
    int k = 0;
    for (int i = 0; i < NETH_VEC_ITEMS; ++i)
        if (e->items[i]) fprintf(f, "%s\"%d\": %d", k++ ? ", " : "", EP_ITEMS[i], e->items[i]);
    for (int i = 0; i < 8; ++i)
        if (e->extra[i]) fprintf(f, "%s\"%d\": %d", k++ ? ", " : "", EXTRA_ITEMS[i], e->extra[i]);
    fputs("}, \"dims\": [", f);
    static const char *const dn[3] = {"nether", "overworld", "end"};
    k = 0;
    for (int i = 0; i < 3; ++i)
        if (e->dims & (1 << i)) fprintf(f, "%s\"%s\"", k++ ? ", " : "", dn[i]);
    fprintf(f, "], \"obsidian\": %d, \"kills\": %d, \"level\": %d, \"food\": %d, \"seen\": {", e->obsidian, e->kills,
            e->level, e->food);
    for (int i = 0; i < 4; ++i) fprintf(f, "%s\"%d\": %d", i ? ", " : "", SEEN_IDS[i], e->seen[i]);
    fprintf(f, "}, \"cells\": %d, \"env_steps\": %lld", e->cells, (long long)steps);
    if (rung != NETH_NO_RUNG) fprintf(f, ", \"rung\": %d", rung);
    fputs("}\n", f);
}

/* CPython 3.12's sum() of floats (Neumaier's compensation) */
struct fsum {
    double s, c;
};
static void fsum_add(struct fsum *a, double x)
{
    double t = a->s + x;
    a->c += fabs(a->s) >= fabs(x) ? (a->s - t) + x : (x - t) + a->s;
    a->s = t;
}

/* semteach.summarize's fields over k records; nothing for none */
static void write_summary(FILE *f, const struct neth_vec_episode *e, int k)
{
    if (k == 0) return;
    double logs = 0, crafted = 0, ach = 0;
    int64_t eps_log = 0, deaths = 0;
    struct fsum ret = {0, 0}, value = {0, 0}, net = {0, 0};
    double ms[NETH_VEC_MILESTONES] = {0}, ms_end = 0, ms_dragon = 0;
    int64_t mac[NETH_VEC_MACROS] = {0};
    for (int i = 0; i < k; ++i)
    {
        ms_end += (e[i].ms >> NETH_VEC_MS_END) & 1;
        ms_dragon += (e[i].ms >> NETH_VEC_MS_DRAGON) & 1;
        logs += e[i].logs;
        crafted += e[i].crafted;
        ach += e[i].ach;
        eps_log += e[i].logs > 0;
        deaths += e[i].deaths;
        fsum_add(&ret, e[i].ret);
        fsum_add(&value, (double)e[i].value);
        fsum_add(&net, e[i].net);
        for (int m = 0; m < NETH_VEC_MILESTONES; ++m) ms[m] += (e[i].ms >> m) & 1;
        for (int m = 0; m < NETH_VEC_MACROS; ++m) mac[m] += e[i].macro[m];
    }
    fprintf(f, "\"episodes\": %d, \"logs\": ", k);
    jnum(f, logs / k);
    fprintf(f, ", \"eps_with_log\": %lld, \"ret\": ", (long long)eps_log);
    jnum(f, (ret.s + ret.c) / k);
    fputs(", \"crafted_mean\": ", f);
    jnum(f, crafted / k);
    fputs(", \"value_mean\": ", f);
    jnum(f, (value.s + value.c) / k);
    fputs(", \"ach_mean\": ", f);
    jnum(f, ach / k);
    fprintf(f, ", \"deaths\": %lld, \"net\": ", (long long)deaths);
    jnum(f, (net.s + net.c) / k);
    fputs(", \"ms\": {", f);
    for (int m = 0; m < NETH_VEC_MILESTONES; ++m)
    {
        fprintf(f, "%s\"%s\": ", m ? ", " : "", neth_ms_names[m]);
        jnum(f, ms[m] / k);
    }
    fputs(", \"end\": ", f);
    jnum(f, ms_end / k);
    fputs(", \"dragon\": ", f);
    jnum(f, ms_dragon / k);
    int64_t s = 0;
    for (int m = 0; m < NETH_VEC_MACROS; ++m) s += mac[m];
    fputs("}, \"macro_share\": {", f);
    for (int m = 0; m < NETH_VEC_MACROS; ++m)
    {
        /* round(v / s, 4): glibc's %.4f rounds the double exactly, as CPython's round does */
        char r[64];
        snprintf(r, sizeof r, "%.4f", (double)mac[m] / (double)(s ? s : 1));
        fprintf(f, "%s\"%s\": ", m ? ", " : "", MACRO_NAMES[m]);
        jnum(f, strtod(r, NULL));
    }
    fputs("}, ", f);
}
/* ---- the frozen eval (--score), mcsr-speedrun's scripts/fromspawn.py's table ---- */

/* fromspawn.py's TIMED: the items whose first holding is timed, by their index in the scalars' counts (EP_ITEMS) */
static const char *const TIMED_NAMES[SC_TIMED] = {"wooden_pickaxe", "stone_pickaxe", "iron_ore", "furnace",
                                                  "iron_ingot", "bucket", "flint_and_steel", "diamond"};
const int neth_timed_col[SC_TIMED] = {5, 10, 15, 17, 16, 30, 31, 27};
/* fromspawn.py's END, the depth's 13 (the milestones but blaze powder, and the Nether from the dims), then
 * the End and the dragon: the bit of the record's ms (neth_ms_names', NETH_VEC_MS_*), or -1 the Nether from the dims */
#define SC_END 15
static const char *const END_NAMES[SC_END] = {"wooden_pickaxe", "stone_pickaxe", "furnace", "iron_ingot",
                                              "iron_pickaxe", "bucket", "flint_and_steel", "diamond_pickaxe",
                                              "obsidian", "nether", "blaze_rod", "ender_pearl", "ender_eye",
                                              "end", "dragon"};
static const int END_BIT[SC_END] = {0, 1, 4, 5, 2, 6, 7, 3, 8, -1, 9, 11, 12, NETH_VEC_MS_END, NETH_VEC_MS_DRAGON};

static int cmp_dbl(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* numpy's median and percentile (linear) of k values, sorted in place; NAN for none */
static double pctl(double *v, int k, double p)
{
    if (k == 0) return NAN;
    qsort(v, (size_t)k, sizeof *v, cmp_dbl);
    double r = p / 100 * (k - 1);
    int i = (int)floor(r);
    return i + 1 < k ? v[i] + (r - i) * (v[i + 1] - v[i]) : v[k - 1];
}

/* the score of one episode a run (eps[0..k), every env's): the table and one JSON line (also to json when given) */
void neth_report_score(const struct neth_pipe *L, const struct neth_vec_episode *eps, int k, double minutes, double wall,
                        int64_t env_steps, const char *init, uint64_t seed, int macros, FILE *json)
{
    int n = L->n;
    double *v = malloc((size_t)(n > k ? n : k) * sizeof *v);
    double share[SC_END], depth = 0, dsq = 0, deaths = 0;
    for (int m = 0; m < SC_END; ++m) share[m] = 0;
    for (int q = 0; q < k; ++q)
    {
        int d = 0;
        for (int m = 0; m < SC_END; ++m)
        {
            int got = END_BIT[m] < 0 ? (eps[q].dims & 1) != 0 : ((eps[q].ms >> END_BIT[m]) & 1) != 0;
            share[m] += got;
            d += got;
        }
        depth += d;
        dsq += (double)d * d;
        deaths += eps[q].deaths;
    }
    double kk = k > 0 ? k : 1, mean = depth / kk;
    double se = k > 1 ? sqrt((dsq / kk - mean * mean) * kk / (kk - 1) / kk) : NAN;
    for (int m = 0; m < SC_END; ++m) share[m] /= kk;
    double eps_s = wall > 0 ? (double)env_steps / wall : 0;
    printf("score: %s: %d runs from the start, %g min each, %s clock, eval seed %llu, macros %d; %.0f env-steps/s, "
           "%.0f s\n", init, k, minutes, L->clock_wrap ? "wrap" : "raw", (unsigned long long)seed, macros, eps_s, wall);
    printf("score: first held (scalars): share of runs, median minute\n");
    double fsh[SC_TIMED], fmed[SC_TIMED];
    /* first[] counts the env's steps; a step is b.ticks ticks (client.ticks_per_step x action_repeat), a minute 1200
     * ticks (before lane/xhigh-patch the steps were taken as ticks: at tps 2 every minute halved) */
    double tps = L->b.ticks > 0 ? (double)L->b.ticks : 1.0;
    for (int t = 0; t < SC_TIMED; ++t)
    {
        int c = 0;
        for (int i = 0; i < n; ++i)
            if (L->first[i * SC_TIMED + t] >= 0) v[c++] = L->first[i * SC_TIMED + t] * tps / 1200.0;
        fsh[t] = (double)c / n;
        fmed[t] = pctl(v, c, 50);
        printf("score:   %-16s %5.2f  %5.1f\n", TIMED_NAMES[t], fsh[t], fmed[t]);
    }
    printf("score: at the end:");
    for (int m = 0; m < SC_END; ++m) printf("%s %s %.3f", m ? "," : "", END_NAMES[m], share[m]);
    printf("\nscore: runs reaching:");
    for (int m = 0; m < SC_END; ++m) printf("%s %s %d", m ? "," : "", END_NAMES[m], (int)lround(share[m] * k));
    for (int q = 0; q < k; ++q) v[q] = eps[q].cells;
    double c50 = pctl(v, k, 50), c90 = pctl(v, k, 90);
    for (int q = 0; q < k; ++q) v[q] = eps[q].net;
    double n50 = pctl(v, k, 50);
    printf("\nscore: deaths a run %.3f; cells median %.0f, p90 %.0f; blocks from the start, median %.0f\n",
           deaths / kk, c50, c90, n50);
    printf("score: depth %.3f milestones a run (of %d), standard error %.3f\n", mean, SC_END, se);
    if (macros & NETH_VEC_STRONGHOLD)
    {
        /* the End (the episode's dims) and the eyes held at the end (extra[7]) */
        int end = 0;
        double eyes = 0;
        for (int q = 0; q < k; ++q) end += (eps[q].dims & 4) != 0, eyes += eps[q].extra[7];
        printf("score: stronghold: the End reached by %d of %d runs; eyes of ender held at the end %.2f a run\n", end, k,
               eyes / kk);
    }
    /* the JSON line: every number above */
    FILE *o[2] = {stdout, json};
    for (int f = 0; f < 2; ++f)
    {
        if (!o[f]) continue;
        fprintf(o[f], "%s{\"model\": \"%s\", \"runs\": %d, \"minutes\": %g, \"clock\": \"%s\", \"seed\": %llu, "
                "\"macros\": %d, \"depth\": %.4f, \"depth_se\": %.4f, \"deaths\": %.4f, \"env_steps_per_s\": %.0f, "
                "\"wall\": %.1f, \"cells_median\": %g, \"net_median\": %.1f, \"end\": {",
                f ? "" : "score json: ", init, k, minutes, L->clock_wrap ? "wrap" : "raw", (unsigned long long)seed,
                macros, mean, se, deaths / kk, eps_s, wall, c50, n50);
        for (int m = 0; m < SC_END; ++m) fprintf(o[f], "%s\"%s\": %.4f", m ? ", " : "", END_NAMES[m], share[m]);
        fputs("}, \"first\": {", o[f]);
        for (int t = 0; t < SC_TIMED; ++t)
        {
            fprintf(o[f], "%s\"%s\": [%.4f, ", t ? ", " : "", TIMED_NAMES[t], fsh[t]);
            if (isnan(fmed[t])) fputs("null]", o[f]);
            else fprintf(o[f], "%.2f]", fmed[t]);
        }
        fputs("}}\n", o[f]);
    }
    free(v);
}

/* ---- --score-books FILE, the dragon benchmark's books of each run's episode (one JSON line an env) ---- */

static void sb_arr(FILE *f, const char *name, const int32_t *a, int n)
{
    fprintf(f, ", \"%s\": [", name);
    for (int i = 0; i < n; ++i) fprintf(f, "%s%d", i ? ", " : "", a[i]);
    fputs("]", f);
}

/* envs lo..hi-1's books as their episode left them (a group's last receive, never sent again: the whole episode's) */
static void score_books(const struct neth_pipe *L, int lo, int hi)
{
    for (int i = lo; i < hi; ++i)
    {
        struct neth_vec_db db;
        if (neth_vec_db(L->v, i, &db)) continue;
        FILE *f = L->sbf;
        fprintf(f, "{\"env\": %d, \"steps\": %d, \"food0\": %d", i, db.steps, db.food0);
        fprintf(f, ", \"stall_steps\": %d, \"stall_runs\": %d, \"stall_max\": %d", db.stall_steps, db.stall_runs,
                db.stall_max);   /* lane/speedrun-recipe */
        sb_arr(f, "ms_first", db.ms_first, NETH_VEC_MS_BITS);
        fputs(", \"mb\": [", f);
        int first = 1;
        for (int j = 0; j < NETH_VEC_DB_STAGES; ++j)
            for (int m = 0; m < NETH_VEC_DB_DRIVERS; ++m)
                if (db.mb_stage[j][m])
                    fprintf(f, "%s[%d, %d, %d]", first ? "" : ", ", j, m, db.mb_stage[j][m]), first = 0;
        fprintf(f, "], \"ndeath\": %d", db.ndeath);
        sb_arr(f, "d_step", db.d_step, db.ndeath);
        sb_arr(f, "d_dim", db.d_dim, db.ndeath);
        sb_arr(f, "d_mb", db.d_mb, db.ndeath);
        sb_arr(f, "d_stage", db.d_stage, db.ndeath);
        sb_arr(f, "d_fall", db.d_fall, db.ndeath);
        sb_arr(f, "d_fire", db.d_fire, db.ndeath);
        sb_arr(f, "d_egg", db.d_egg, db.ndeath);
        sb_arr(f, "d_air", db.d_air, db.ndeath);
        sb_arr(f, "d_food", db.d_food, db.ndeath);
        struct neth_vec_pearl pe;
        if (!neth_vec_pearl(L->v, i, 1, &pe))
        {
            fprintf(f, ", \"pearl\": {\"seen\": %d, \"killed\": %d, \"pearls\": %d, \"deaths\": %d, \"death_step\": %d, "
                    "\"chain\": %d, \"phase\": %d, \"on\": %d", pe.seen, pe.killed, pe.pearls, pe.deaths, pe.death_step,
                    pe.chain, pe.phase, pe.on);
            sb_arr(f, "cause", pe.cause, 4);
            sb_arr(f, "step", pe.step, NETH_VEC_PEARLS);
            sb_arr(f, "phase_steps", pe.phase_steps, 12);
            /* the census (vecenv.h) */
            fprintf(f, ", \"provoked\": %d, \"hits\": %d, \"pillars\": %d, \"wtime0\": %d", pe.provoked, pe.hits,
                    pe.pillars, pe.wtime0);
            sb_arr(f, "why", pe.why, 12);
            sb_arr(f, "whom", pe.whom, 33);
            sb_arr(f, "first", pe.first, 10);
            sb_arr(f, "hunt_y", pe.hunt_y, 8);
            fprintf(f, ", \"cen\": [%lld, %lld, %lld, %lld, %lld, %lld, %lld, %lld]", (long long)pe.cen[0],
                    (long long)pe.cen[1], (long long)pe.cen[2], (long long)pe.cen[3], (long long)pe.cen[4],
                    (long long)pe.cen[5], (long long)pe.cen[6], (long long)pe.cen[7]);
            fprintf(f, ", \"hsky\": %d", pe.hsky);
            sb_arr(f, "hbiome", pe.hbiome, 64);
            sb_arr(f, "vsee", pe.vsee, 4);
            fprintf(f, ", \"towers\": %d, \"turns\": %d, \"dips\": %d, \"cen2\": [%lld, %lld, %lld, %lld]", pe.towers,
                    pe.turns, pe.dips, (long long)pe.cen2[0], (long long)pe.cen2[1], (long long)pe.cen2[2],
                    (long long)pe.cen2[3]);
            sb_arr(f, "upx", pe.upx, 10);
            sb_arr(f, "uph", pe.uph, 12);
            fputs(", \"cen3\": [", f);
            for (int k = 0; k < 8; ++k) fprintf(f, "%s%lld", k ? ", " : "", (long long)pe.cen3[k]);
            fputs("]", f);
            fprintf(f, ", \"dug\": [%d, %d, %d], \"drops\": [%d, %d, %d], \"lure\": %d, \"high\": [%d, %d, %d]",
                    pe.sky_climbs, pe.dig_open, pe.dig_steps, pe.drops, pe.fetched, pe.fetch_out, pe.lure_digs,
                    pe.high_steps, pe.high_walks, pe.high_gain);
            fputs(", \"cen4\": [", f);
            for (int k = 0; k < 16; ++k) fprintf(f, "%s%lld", k ? ", " : "", (long long)pe.cen4[k]);
            fputs("]", f);
            sb_arr(f, "cany_f", pe.cany_f, 4);
            sb_arr(f, "cyaw", pe.cyaw, 4);
            sb_arr(f, "cpitch", pe.cpitch, 3);
            fprintf(f, ", \"restocks\": %d", pe.restocks);
            fputs("}", f);
        }
        struct neth_vec_sh_book sh;
        if (!neth_vec_stronghold_book(L->v, i, 0, &sh))
        {
            fprintf(f, ", \"sh\": {\"started\": %d, \"brick\": %d, \"frame\": %d, \"room\": %d, \"lit\": %d, \"end\": %d, "
                    "\"throws\": %d, \"near\": %d, \"lost\": %d, \"eyes\": %d, \"filled\": %d, \"deaths\": %d, "
                    "\"phase\": %d, \"gave_up\": %d, \"dist\": %.0f, \"room_eyes\": %d, \"picked\": %d, \"stuck\": %d",
                    sh.started, sh.brick, sh.frame, sh.room, sh.lit, sh.end, sh.throws, sh.near, sh.lost, sh.eyes,
                    sh.filled, sh.deaths, sh.phase, sh.gave_up, (double)sh.dist, sh.room_eyes, sh.picked, sh.stuck);
            sb_arr(f, "cause", sh.cause, 6);
            fputs("}", f);
        }
        struct neth_vec_nether ne;
        if (!neth_vec_nether(L->v, i, &ne))
        {
            fprintf(f, ", \"nether\": {\"arrive\": %lld, \"found\": %lld, \"spawner\": %lld, \"rod1\": %lld, "
                    "\"rods7\": %lld, \"home\": %lld, \"rods_max\": %d, \"kills\": %d, \"deaths\": %d, \"phase\": %d, "
                    "\"nsteps\": %lld", (long long)ne.arrive, (long long)ne.found, (long long)ne.spawner,
                    (long long)ne.rod1, (long long)ne.rods7, (long long)ne.home, ne.rods_max, ne.kills, ne.deaths,
                    ne.phase, (long long)ne.nsteps);
            /* the link's funnel and the death's place */
            fprintf(f, ", \"fortin\": %lld, \"bseen\": %lld, \"bhit\": %lld, \"bkill\": %lld, \"bhits\": %d, "
                    "\"bkills\": %d, \"st_drive\": %lld, \"st_off\": %lld, \"st_esc\": %lld, \"st_screen\": %lld, "
                    "\"death_t\": %lld, \"death_pos\": [%.1f, %.1f, %.1f], \"death_ground\": [%d, %d, %d], "
                    "\"fort\": [%d, %d, %d, %d], \"sp\": [%d, %d, %d, %d]",
                    (long long)ne.fortin, (long long)ne.bseen, (long long)ne.bhit, (long long)ne.bkill, ne.bhits,
                    ne.bkills, (long long)ne.st_drive, (long long)ne.st_off, (long long)ne.st_esc,
                    (long long)ne.st_screen, (long long)ne.death_t, ne.death_pos[0], ne.death_pos[1], ne.death_pos[2],
                    ne.death_ground[0], ne.death_ground[1], ne.death_ground[2], ne.fort[0], ne.fort[1], ne.fort[2],
                    ne.fort[3], ne.sp[0], ne.sp[1], ne.sp[2], ne.sp[3]);
            fprintf(f, ", \"bstat\": %d", ne.bstat);    /* the stats screen's blaze kills (credited) */
            sb_arr(f, "cause", ne.cause, 8);
            fputs("}", f);
        }
        struct neth_vec_obsidian ob;
        if (!neth_vec_obsidian(L->v, i, &ob))
        {
            fprintf(f, ", \"obsidian\": {\"nether\": %lld, \"kit\": %lld, \"obs_max\": %d, \"mined\": %d, \"deaths\": %d, "
                    "\"d_cause\": %d, \"d_kit\": %d", (long long)ob.nether, (long long)ob.kit, ob.obs_max, ob.mined,
                    ob.deaths, ob.d_cause, ob.d_kit);
            sb_arr(f, "jobs", ob.jobs, 16);
            sb_arr(f, "steps", ob.steps, 16);
            sb_arr(f, "cause", ob.cause, 7);
            /* the funnel (obsidian.h OF_*) and the water trip */
            fprintf(f, ", \"fn\": [%lld, %lld, %lld, %lld, %lld, %lld, %lld, %lld], \"wb_fills\": %d, \"wb_first\": %lld, "
                    "\"sky_trips\": %d, \"sky_water\": %d, \"sw_ok\": %d, \"sw_trips\": %d, \"sw_fill\": %d, "
                    "\"sky_why\": %d, \"sky_d\": %.1f, \"rims\": %d, \"glances\": %d, \"arounds\": %d",
                    (long long)ob.fn[0], (long long)ob.fn[1], (long long)ob.fn[2], (long long)ob.fn[3],
                    (long long)ob.fn[4], (long long)ob.fn[5], (long long)ob.fn[6], (long long)ob.fn[7], ob.wb_fills,
                    (long long)ob.wb_first, ob.sky_trips, ob.sky_water, ob.sw_ok, ob.sw_trips, ob.sw_fill, ob.sky_why,
                    (double)ob.sky_d, ob.rims, ob.glances, ob.arounds);
            sb_arr(f, "trip_end", ob.trip_end, 5);
            fputs("}", f);
        }
        struct neth_vec_strip sp;
        if (!neth_vec_strip(L->v, i, &sp))
        {
            fprintf(f, ", \"strip\": {\"cells\": %d, \"dpick\": %d, \"strip_steps\": %lld, \"out_steps\": %lld, "
                    "\"dpick_step\": %lld", sp.cells, sp.dpick, (long long)sp.strip_steps, (long long)sp.out_steps,
                    (long long)sp.dpick_step);
            sb_arr(f, "got", sp.got, 6);
            sb_arr(f, "held", sp.held, 7);
            sb_arr(f, "errand", sp.errand, 4);
            fputs(", \"dia\": [", f);       /* struct neth_vec_strip dia */
            for (int k = 0; k < NETH_VEC_DIA_N; ++k) fprintf(f, "%s%lld", k ? ", " : "", (long long)sp.dia[k]);
            fputs("]}", f);
        }
        struct neth_vec_bportal bp;
        if (!neth_vec_bportal(L->v, i, &bp))
        {
            fprintf(f, ", \"bportal\": {\"starts\": %d, \"sites\": %d, \"aborts\": %d, \"casts\": %d, \"lit\": %d, "
                    "\"lit_step\": %lld, \"steps_on\": %lld, \"trips_lava\": %d, \"trips_water\": %d, "
                    "\"search_found\": %d, \"site_lost\": %d, \"roam_lava\": %d", bp.starts, bp.sites, bp.aborts,
                    bp.casts, bp.lit, (long long)bp.lit_step, (long long)bp.steps_on, bp.trips_lava, bp.trips_water,
                    bp.search_found, bp.site_lost, bp.roam_lava);
            sb_arr(f, "fail_kind", bp.fail_kind, 8);
            fputs("}", f);
        }
        struct neth_vec_food fo;    /* the food census */
        if (!neth_vec_food(L->v, i, &fo))
        {
            fprintf(f, ", \"food\": {\"low_step\": %d, \"zero_step\": %d, \"low_meat\": %d, \"zero_meat\": %d, "
                    "\"low_other\": %d, \"zero_other\": %d, \"low_y\": %d, \"zero_y\": %d, \"low_mb\": %d, "
                    "\"zero_mb\": %d, \"low_animal\": %d, \"zero_animal\": %d, \"eat_starts\": %d, \"eats\": %d, "
                    "\"hunts\": %d, \"hunt_meat\": %d, \"meat_got\": %d, \"other_got\": %d, \"other_eaten\": %d, "
                    "\"animal_steps\": %d", fo.low_step, fo.zero_step, fo.low_meat, fo.zero_meat, fo.low_other,
                    fo.zero_other, fo.low_y, fo.zero_y, fo.low_mb, fo.zero_mb, fo.low_animal, fo.zero_animal,
                    fo.eat_starts, fo.eats, fo.hunts, fo.hunt_meat, fo.meat_got, fo.other_got, fo.other_eaten,
                    fo.animal_steps);
            sb_arr(f, "hungry", fo.hungry, FW_N);
            sb_arr(f, "starving", fo.starving, FW_N);
            sb_arr(f, "meat_mb", fo.meat_mb, 32);
            sb_arr(f, "band_steps", fo.band_steps, 3);
            sb_arr(f, "band_cm", fo.band_cm, 3);
            sb_arr(f, "band_deep", fo.band_deep, 3);
            fprintf(f, ", \"scouts\": %d, \"scout_found\": %d, \"scout_far\": %d", fo.scouts, fo.scout_found,
                    fo.scout_far);
            sb_arr(f, "scout_end", fo.scout_end, 6);
            sb_arr(f, "hunt_end", fo.hunt_end, 4);
            fprintf(f, ", \"homes\": %d", fo.homes);
            sb_arr(f, "home_end", fo.home_end, 3);
            sb_arr(f, "lost_why", fo.lost_why, 5);
            fprintf(f, ", \"trips\": %d, \"trip_steps\": %d, \"trip_meat\": %d", fo.trips, fo.trip_steps, fo.trip_meat);
            fprintf(f, ", \"low_held\": %d, \"deep_after\": %d", fo.low_held, fo.deep_after);
            fputs("}", f);
        }
        double di[NETH_VEC_DRAGON_INFO];
        if (!neth_vec_dragon_info(L->v, i, di))
            fprintf(f, ", \"dragon\": {\"hp\": %.1f, \"phase\": %.0f, \"arrows\": %.0f, \"crystals_hit\": %.0f, "
                    "\"kill_step\": %.0f, \"exit_step\": %.0f, \"deaths\": %.0f, \"d_void\": %.0f, \"d_dragon\": %.0f, "
                    "\"d_ender\": %.0f, \"d_else\": %.0f, \"sword_hits\": %.0f, \"dragon_min_hp\": %.1f, "
                    "\"d_first\": %.0f, \"d_cause\": %.0f, \"arrows_held\": %.0f, \"food\": %.0f}",
                    di[4], di[9], di[10], di[11], di[12], di[13], di[14], di[16], di[17], di[18], di[19], di[20],
                    di[21], di[24], di[25], di[48], di[50]);
        fputs("}\n", f);
    }
    fflush(L->sbf);
}

/* --view-books: env e's episode's view books as a line of view.jsonl, then a fresh episode's; open 1
 * at the run's end (the episode unfinished) */
static void view_flush(struct neth_pipe *L, int e, int open)
{
    struct vbook *v = &L->vb[e];
    fprintf(L->vbf,
            "{\"env\": %d, \"ep\": %d, \"steps\": %d, \"in\": %d, \"first\": %d, \"lost\": %d, \"reacq\": %d, "
            "\"gapmax\": %d, \"in_end\": %d, \"out_after\": %d, \"open\": %d}\n",
            e, v->ep, v->steps, v->nin, v->first, v->lost, v->reacq, v->gapmax, v->in,
            v->first < 0 ? 0 : v->steps - v->first - v->nin, open);
    int ep = v->ep + 1;
    memset(v, 0, sizeof *v);
    v->ep = ep, v->first = -1;
}

/* the staged hook: env obs_env's frame as the policy reads it (--obs-dump, the host cast's bytes), each env's view
 * books (--view-books), group g's step before --zero-sc */
void neth_report_staged(struct neth_pipe *L, int g)
{
    const struct neth_vec_bufs *b = &L->b;
    int gs = L->gs, lo = g * gs;
    const uint8_t *src = (const uint8_t *)b->obs + (size_t)lo * L->oe;
    if (L->obs_dir && !b->obs_device && L->obs_env >= lo && L->obs_env < lo + gs && L->obs_seq < L->obs_max)
    {
        /* --obs-dump */
        char p[4200];
        snprintf(p, sizeof p, "%.4000s/o-%06lld.ppm", L->obs_dir, (long long)L->obs_seq);
        FILE *f = fopen(p, "wb");
        if (f)
        {
            fprintf(f, "P6\n%lld %lld\n255\n", (long long)b->obs_shape[2], (long long)b->obs_shape[1]);
            const uint8_t *o = src + (size_t)(L->obs_env - lo) * L->oe;
            if (L->tone)
            {
                /* the frame as the policy reads it, through --obs-tone (neth_tone's arithmetic) */
                int m = 0;
                for (size_t j = 0; j < L->oe; ++j) m = o[j] > m ? o[j] : m;
                for (size_t j = 0; j < L->oe; ++j)
                    fputc(L->tone == 1 ? (m ? o[j] * 255 / m : 0) : L->hlut[o[j]], f);
            }
            else
                fwrite(o, 1, L->oe, f);
            fclose(f);
        }
        snprintf(p, sizeof p, "%.4000s/index.tsv", L->obs_dir);
        if ((f = fopen(p, "a")) != NULL)
        {
            fprintf(f, "%lld\t%lld\n", (long long)L->obs_seq, (long long)b->te[L->obs_env]);
            fclose(f);
        }
        ++L->obs_seq;
    }
    if (L->vbf)
        for (int i = 0; i < gs; ++i)
        {
            /* --view-books; a step into the episode below the last one: a new episode */
            struct vbook *v = &L->vb[lo + i];
            int64_t te = b->te[lo + i];
            if (v->steps && te <= v->te) view_flush(L, lo + i, 0);
            v->te = te;
            int in = b->scalars[(size_t)(lo + i) * NETH_NET_NS + L->vb_col] > 0.0f;
            if (in)
            {
                if (v->first < 0) v->first = v->steps;
                else if (!v->in) ++v->reacq;
                ++v->nin, v->gap = 0;
            }
            else if (v->first >= 0)
            {
                v->lost += v->in;
                if (++v->gap > v->gapmax) v->gapmax = v->gap;
            }
            v->in = in;
            ++v->steps;
        }
}

/* the received hook: group g's books two receives before the last (the truncating step's recv resets the ended env
 * before it returns the group, and that receive is the one before the quota's: its books are the new episode's, the
 * macros' having no "last"; the books here are the episode's after ep_len - 1 steps) */
void neth_report_received(struct neth_pipe *L, int g)
{
    if (L->sbf && L->quota && L->gcount[g] == L->quota - 3) score_books(L, g * L->gs, (g + 1) * L->gs);
}

/* --out DIR's files (iters.jsonl, episodes.jsonl, distill.jsonl), and the episodes' buffer the rows read; 0, or 1 */
int neth_report_open(struct neth_pipe *L, const struct neth_runconf *c)
{
    const char *out = c->out;
    if (out)
    {
        char p[4096];
        if (mkdir(out, 0755) && errno != EEXIST)
        {
            printf("FAIL --out %s: %s\n", out, strerror(errno));
            return 1;
        }
        snprintf(p, sizeof p, "%s/iters.jsonl", out);
        L->rows = fopen(p, "a");
        snprintf(p, sizeof p, "%s/episodes.jsonl", out);
        L->eprows = fopen(p, "a");
        if (L->dist) snprintf(p, sizeof p, "%s/distill.jsonl", out), L->dist_rows = fopen(p, "a");
        if (!L->rows || !L->eprows)
        {
            printf("FAIL --out %s: %s\n", out, strerror(errno));
            return 1;
        }
    }
    L->epcap = 65536, L->nep = 0, L->snapk = 1;
    L->eps = malloc((size_t)L->epcap * sizeof *L->eps);
    return 0;
}

/* --score's end: every env's one episode, in the ring since nothing else read it (the first record an env; a second
 * would be a bug in the quota), the table and its JSON, the books of the GUI's macros, the per-stage table */
void neth_report_score_run(struct neth_pipe *L, const struct neth_runconf *c, double wall)
{
    int n = L->n, macros = c->macros;
    struct neth_vec_episode *eps = L->eps;
    int k = neth_vec_episodes(L->v, eps, L->epcap), kept = 0;
    char *got = calloc((size_t)n, 1);
    for (int q = 0; q < k; ++q)
        if (eps[q].env >= 0 && eps[q].env < n && !got[eps[q].env])
        {
            got[eps[q].env] = 1;
            if (L->eprows) neth_report_episode(L->eprows, &eps[q], L->env_steps, NETH_NO_RUNG);
            eps[kept++] = eps[q];
        }
    free(got);
    if (L->eprows) L->nep += kept;
    /* with the task on an episode ends at its pickup or death, and the env's next one plays on in the quota (more
     * records than envs; the first an env is the run's) */
    if (kept != n || (k != n && !c->task_item))
    {
        printf("FAIL --score: %d episode records for %d envs (%d distinct)\n", k, n, kept);
        L->fail = 1;
    }
    FILE *js = c->score_json ? fopen(c->score_json, "w") : NULL;
    if (c->score_json && !js) printf("FAIL --score-json %s: %s\n", c->score_json, strerror(errno));
    neth_report_score(L, eps, kept, c->ep_len / 1200.0, wall, L->env_steps, c->init, c->seed, macros, js);
    if (js) fclose(js);
    {
        /* actions.gui cursor's books (vecenv.h neth_vec_gui_books), a runs' mean each */
        int64_t gb[NETH_VEC_GUI_BOOKS];
        if (neth_vec_gui_books(L->v, gb) > 0 && n > 0)
            printf("score gui: a run's macro GUI steps %.1f (said %.1f, decoded back unequal %lld); the "
                   "policy's steps under a screen %.1f, clicks %.1f, closes %.1f, inventory opens %.1f\n",
                   (double)gb[0] / n, (double)gb[1] / n, (long long)gb[3], (double)gb[4] / n,
                   (double)gb[5] / n, (double)gb[6] / n, (double)gb[7] / n);
        /* the recipe half's books, a runs' mean each */
        if (neth_vec_gui_books(L->v, gb) >= 0 && n > 0)
            printf("score recipe: a run's recipe starts 2x2 %.2f, 3x3 %.2f, smelt %.3f; hand-offs at the "
                   "start %.2f, after it %.3f (smelt %.3f); smelt ingots taken %.3f; the policy's steps under "
                   "a furnace %.1f; windows opened: crafting table %.2f, furnace %.2f\n",
                   (double)gb[8] / n, (double)gb[9] / n, (double)gb[10] / n, (double)gb[11] / n,
                   (double)gb[12] / n, (double)gb[13] / n, (double)gb[14] / n, (double)gb[15] / n,
                   (double)gb[16] / n, (double)gb[17] / n);
        /* the policy's own windows, by what the recipe start finds there */
        if (neth_vec_gui_books(L->v, gb) > 0 && n > 0)
            printf("score window: a run's policy steps under a crafting table: untidy %.1f, no plan %.1f, "
                   "the plan fits (no jump) %.1f, another window's plan %.1f (planks %.1f, table %.1f, "
                   "sticks %.1f), no window's %.1f; under the inventory: untidy %.1f, no plan %.1f, fits "
                   "%.1f, another's %.1f, no window's %.1f\n",
                   (double)gb[18] / n, (double)gb[19] / n, (double)gb[20] / n, (double)gb[21] / n,
                   (double)gb[28] / n, (double)gb[29] / n, (double)gb[30] / n, (double)gb[22] / n,
                   (double)gb[23] / n, (double)gb[24] / n, (double)gb[25] / n, (double)gb[26] / n,
                   (double)gb[27] / n);
        if (neth_vec_gui_books(L->v, gb) > 0 && n > 0)
            printf("score keys: a run's policy steps under a screen with the inventory key %.1f (held the "
                   "step before too, no close %.1f), the jump %.1f\n",
                   (double)gb[31] / n, (double)gb[32] / n, (double)gb[33] / n);
        if (neth_vec_gui_books(L->v, gb) > 0 && n > 0 && (macros & NETH_VEC_TABLEKEY))
            printf("score table: a run's table half started by the key: 2x2 %.2f, 3x3 %.2f, smelt %.3f; "
                   "the policy's steps with no screen and a crafting plan named %.1f (a 3x3 plan or the "
                   "smelt %.1f)\n",
                   (double)gb[34] / n, (double)gb[35] / n, (double)gb[36] / n, (double)gb[37] / n,
                   (double)gb[38] / n);
        if (neth_vec_gui_books(L->v, gb) >= 0 && n > 0 && (macros & NETH_VEC_SEEKKEY))
            printf("score seek: a run's seek started by key 13 on iron ore %.2f, diamond ore %.3f, a log "
                   "%.2f; ended with the item %.2f, dug and not picked up %.3f, given up %.2f, by a death or "
                   "the lava escape %.3f; its steps %.1f; the policy's steps with the start holding %.1f "
                   "(iron ore %.1f), key 13 down with none %.1f\n",
                   (double)gb[39] / n, (double)gb[40] / n, (double)gb[41] / n, (double)gb[42] / n,
                   (double)gb[43] / n, (double)gb[44] / n, (double)gb[45] / n, (double)gb[46] / n,
                   (double)gb[47] / n, (double)gb[48] / n, (double)gb[49] / n);
        /* the seek's steps by half (the census; any macros with a seek) and the split's books (actions.seek_walk,
         * seek_dig) */
        if (neth_vec_gui_books(L->v, gb) >= 0 && n > 0 && (macros & (NETH_VEC_SEEK | NETH_VEC_SEEKKEY)))
            printf("score seekhalf: a run's seek steps in the walk half %.1f, in the dig half %.1f; key 13 "
                   "down with the start holding in the policy's half %.1f; the key's seeks ended as the "
                   "cell left the macro's half %.3f; the policy's steps with the start in its own half "
                   "%.1f\n",
                   (double)gb[51] / n, (double)gb[52] / n, (double)gb[50] / n, (double)gb[53] / n,
                   (double)gb[54] / n);
        if (neth_vec_gui_books(L->v, gb) > 0 && n > 0 && gb[57] > 0)
            printf("score x2: a run's 2x2 crafts by the policy's clicks %.2f (the result picked up by a "
                   "plain click %.2f); its steps under the inventory %.1f, with no op %.1f, clicks %.1f; "
                   "the labeller's runs started %.2f, followed to the result %.2f, left %.2f; steps it "
                   "says the close %.1f, closed %.2f; labels decoded back unequal %lld\n",
                   (double)gb[55] / n, (double)gb[56] / n, (double)gb[57] / n, (double)gb[58] / n,
                   (double)gb[59] / n, (double)gb[60] / n, (double)gb[61] / n, (double)gb[62] / n,
                   (double)gb[63] / n, (double)gb[64] / n, (long long)gb[65]);
        if (neth_vec_gui_books(L->v, gb) > 0 && n > 0 && gb[57] > 0)
            printf("score x2 left: a run's labeller runs left at the first op %.2f, a middle op %.2f, the "
                   "result %.2f; by a close %.2f, the label's slot with another button or mode %.2f, "
                   "another slot %.2f; clicks on another stack of the part %.2f (x2_source)\n",
                   (double)gb[66] / n, (double)gb[67] / n, (double)gb[68] / n, (double)gb[69] / n,
                   (double)gb[70] / n, (double)gb[71] / n, (double)gb[72] / n);
    }
    if (L->crc)
    {
        /* the per-stage table (beside score.json: stages.json) */
        for (int q = 0; q < kept; ++q) cur_episode(L->crc, &eps[q]);
        char sp[4096] = "";
        if (c->score_json)
        {
            snprintf(sp, sizeof sp, "%s", c->score_json);
            char *sl = strrchr(sp, '/');
            snprintf(sl ? sl + 1 : sp, sizeof sp - (size_t)(sl ? sl + 1 - sp : 0), "stages.json");
        }
        FILE *sj = sp[0] ? fopen(sp, "w") : NULL;
        cur_table(L->crc, stdout, sj);
        if (sj) fclose(sj);
    }
}

/* --digest's lines: the envs' hashes summed (vec_bench --digest's), and --frame-digest's */
void neth_report_digest(const struct neth_pipe *L)
{
    uint64_t dg = 0;
    for (int i = 0; i < L->n; ++i) dg += L->hash[i];
    printf("trajectory digest %016llx\n", (unsigned long long)dg);
    if (L->fhash)
    {
        uint64_t fd = 0;
        for (int i = 0; i < L->n; ++i) fd += L->fhash[i];
        printf("frame digest %016llx\n", (unsigned long long)fd);
    }
    if (L->phash)
    {
        /* lane/cuda-graph: the policy's outputs as the segments hold them (actions, logp, value, each bootstrap) */
        uint64_t pd = 0;
        for (int i = 0; i < L->n; ++i) pd += L->phash[i];
        printf("policy digest %016llx\n", (unsigned long long)pd);
    }
}

/* --out's end: the distillation's last row, the final rows, the view books the end cut, the model and the critic */
void neth_report_finish(struct neth_pipe *L, const struct neth_runconf *c)
{
    if (!L->rows) return;
    const char *out = c->out;
    if (L->dist_rows)
    {
        double bk[NETH_DISTILL_NBOOK];
        neth_distill_books(L->dist, bk, L->lr.s);
        neth_distill_row(L->dist, L->dist_rows, L->env_steps, wall_s() - L->t_start);
    }
    neth_report_rows(L, 1);
    if (L->dist_rows) fclose(L->dist_rows), L->dist_rows = NULL;
    if (L->vbf)
    {
        /* --view-books, the episodes the run's end cut */
        for (int i = 0; i < L->n; ++i)
            if (L->vb[i].steps) view_flush(L, i, 1);
        fclose(L->vbf);
        L->vbf = NULL;
    }
    fclose(L->rows);
    fclose(L->eprows);
    L->rows = L->eprows = NULL;
    printf("--out %s: %d episodes\n", out, L->nep);
    if (c->learn)
    {
        if (neth_checkpoint_save(L, out, wall_s() - L->t_start))
            printf("FAIL: --out %s: the model: %s\n", out, strerror(errno));
        else printf("--out %s: model.ntw (Adam's step %d)\n", out, L->lr.step);
        if (L->lr.crit)
        {   /* the critic beside it (the policy never needs it; a warm start may) */
            char p[4096];
            snprintf(p, sizeof p, "%s/critic.neth_critic", out);
            if (neth_critic_save(L->lr.crit, p)) printf("FAIL: --out %s: the critic\n", out);
            else printf("--out %s: critic.neth_critic\n", out);
        }
    }
    neth_eval_finish(L, out);
}

/* the episodes since the last call to episodes.jsonl, a row over them to iters.jsonl (final: fastppo's last row) */
void neth_report_rows(struct neth_pipe *L, int final)
{
    FILE *rows = L->rows, *eprows = L->eprows;
    struct neth_vec_episode *eps = L->eps;
    int epcap = L->epcap;
    double t_start = L->t_start, *last_log = &L->last_log;
    int64_t *last_steps = &L->last_steps;
    double now = wall_s();
    int k;
    if (L->stash_cap)
    {
        /* --rec (or the ladder) took them as recv made them (the stash, not L->rec: rec_close clears that before the
         * final row, which read the vec env's drained ring and lost every episode since the last row) */
        k = L->nstash < epcap ? L->nstash : epcap;
        memcpy(eps, L->stash, (size_t)k * sizeof *eps);
        /* the ladder's: the rung each ran, after env_steps */
        for (int q = 0; q < k; ++q)
            neth_report_episode(eprows, &eps[q], L->env_steps, L->lad ? L->stash_rung[q] : NETH_NO_RUNG);
        memmove(L->stash, L->stash + k, (size_t)(L->nstash - k) * sizeof *L->stash);
        memmove(L->stash_rung, L->stash_rung + k, (size_t)(L->nstash - k) * sizeof *L->stash_rung);
        L->nstash -= k;
    }
    else
    {
        k = neth_vec_episodes(L->v, eps, epcap);
        for (int q = 0; q < k; ++q) neth_report_episode(eprows, &eps[q], L->env_steps, NETH_NO_RUNG);
    }
    fflush(eprows);
    if (L->crc)
        for (int q = 0; q < k; ++q) cur_episode(L->crc, &eps[q]);
    L->nep += k;
    if (final && k == 0) return;
    fputs("{", rows);
    write_summary(rows, eps, k);
    fputs("\"wall\": ", rows);
    jnum(rows, now - t_start);
    fprintf(rows, ", \"env_steps\": %lld", (long long)L->env_steps);
    if (L->lad)
    {
        /* the ladder's: the envs whose next reset puts them on each rung, and the env-steps run at each so far (the
         * first the make's episodes') */
        const struct neth_ladder *D = L->lad;
        int cnt[LAD_MAX] = {0};
        for (int i = 0; i < L->n; ++i) ++cnt[D->pend[i] < 0 ? 0 : D->pend[i]];
        fputs(", \"ladder_envs\": [", rows);
        for (int r = 0; r < D->nr; ++r) fprintf(rows, "%s%d", r ? ", " : "", cnt[r]);
        fputs("], \"ladder_steps\": [", rows);
        for (int r = 0; r <= D->nr; ++r) fprintf(rows, "%s%lld", r ? ", " : "", (long long)D->steps[r]);
        fputs("]", rows);
    }
    if (final)
        fputs(", \"final\": true}\n", rows);
    else
    {
        struct neth_learner *R = &L->lr;
        fputs(", \"env_steps_per_s\": ", rows);
        jnum(rows, (double)(L->env_steps - *last_steps) / (now - *last_log));
        float acc[NETH_LEARN_NBOOK] = {0}, gap = 0;
        if (L->learn)
        {
            /* the update in flight finished first, so the books are whole updates'; actor_gap is the actor's weights
             * against the learner's at the row (fastppo's: the in-flight update's newer weights not yet taken) */
            if (R->busy) CK(cudaEventSynchronize(R->busy->freed));
            neth_maxabs(neth_policy_actor_source(&L->P), R->w, NETH_LEARN_NPARAM, R->gap, R->s);
            CK(cudaMemcpyAsync(R->hacc, R->acc, (NETH_LEARN_NBOOK + 1) * sizeof(float), cudaMemcpyDeviceToHost, R->s));
            CK(cudaMemsetAsync(R->acc, 0, NETH_LEARN_NBOOK * sizeof(float), R->s));
            CK(cudaStreamSynchronize(R->s));
            memcpy(acc, R->hacc, sizeof acc);
            gap = R->hacc[NETH_LEARN_NBOOK];
            if (R->busy) neth_train_done(L);
            if (L->dist)
            {
                /* the distillation's books since the last row and the labels' census (the last row main's,
                 * written with or without episodes) */
                double bk[NETH_DISTILL_NBOOK];
                neth_distill_books(L->dist, bk, R->s);
                if (L->dist_rows) neth_distill_row(L->dist, L->dist_rows, L->env_steps, now - t_start);
            }
            fprintf(rows, ", \"updates\": %lld, \"samples\": %lld, \"t_learn\": ", (long long)R->n, (long long)R->samples);
            jnum(rows, R->t_learn);
            fputs(", \"gpu_learn\": ", rows);
            jnum(rows, R->gpu_learn);
        }
        fputs(", \"reward_scale\": ", rows);
        jnum(rows, neth_train_reward_scale(L));
        if (L->learn)
        {
            fprintf(rows, ", \"refreshes\": %lld, \"lr\": ", (long long)R->refreshes);
            jnum(rows, R->lr);
            fputs(", \"ent_coef\": ", rows);
            jnum(rows, (double)R->entc);
            if (R->wref)    /* the KL term's coefficient now (its schedule's) */
            {
                fputs(", \"ref_kl_coef\": ", rows);
                jnum(rows, (double)R->h.ref_kl);
            }
            if (R->vwarm) fprintf(rows, ", \"value_warmup\": %d", R->h.vonly);
            if (R->h.kl_stop > 0)
            {
                /* --kl-stop's Adam steps taken and updates stopped since the last row */
                long long tk, sp;
                neth_learn_kstat(R->ln, &tk, &sp);
                fprintf(rows, ", \"kl_steps\": %lld, \"kl_stops\": %lld", tk, sp);
            }
            fputs(", \"actor_gap\": ", rows);
            jnum(rows, (double)gap);
        }
        fputs(", \"t_loop\": {", rows);
        for (int q = 0; q < T_N; ++q)
        {
            char r[32];
            snprintf(r, sizeof r, "%.2f", L->tm[q] - L->tm_row[q]);
            fprintf(rows, "%s\"%s\": ", q ? ", " : "", neth_t_names[q]);
            jnum(rows, strtod(r, NULL));
        }
        fputs("}", rows);
        if (L->learn && R->n)
        {
            /* (acc / n).tolist(): float32 division */
            static const char *const bn[NETH_LEARN_NBOOK] = {"loss", "pg", "vl", "ent", "kl", "clipfrac", "ref_kl"};
            for (int i = 0; i < NETH_LEARN_NBOOK; ++i)
            {
                fprintf(rows, ", \"%s\": ", bn[i]);
                jnum(rows, (double)(acc[i] / (float)R->n));
            }
            double ts[4];
            neth_learn_tstat(R->ln, ts);
            fputs(", \"grad_norm\": ", rows);
            jnum(rows, ts[0]);
            fputs(", \"grad_norm_max\": ", rows);
            jnum(rows, ts[1]);
            fprintf(rows, ", \"nan_skips\": %lld", (long long)llround(ts[2]));
            fputs(", \"explained_var\": ", rows);
            jnum(rows, ts[3]);
        }
        if (L->saving)
        {
            /* fastppo's row["own_starts"] = v.saves() */
            double sv[4];
            neth_vec_saves(L->v, sv);
            fprintf(rows, ", \"own_starts\": {\"saves\": %d, \"failed\": %d, \"pool\": %d, \"pending\": %d}", (int)sv[0],
                    (int)sv[1], (int)sv[2], (int)sv[3]);
        }
        if (L->crc)
        {
            /* the stages' rates since the last row, the weights moved for the next resets */
            cur_row(L->crc);
            if (L->crc->mode != CUR_OFF && neth_vec_start_weights(L->v, L->crc->sw))
                printf("WARN: the curriculum's weights were refused\n");
            cur_json(L->crc, rows);
        }
        fputs("}\n", rows);
        R->n = R->jobs = R->samples = 0;
        R->t_learn = R->gpu_learn = 0;
    }
    fflush(rows);
    *last_log = now;
    *last_steps = L->env_steps;
    memcpy(L->tm_row, L->tm, sizeof L->tm);
}
