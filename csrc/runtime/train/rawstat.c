/* rawstat (lane/eval1k): the statistics of raweval's episodes, one tool for every table.
 *
 *   rawstat one FILE.tsv                  one eval: episodes, starts, success, standard errors, the variance between
 *                                         starts and within a start
 *   rawstat pair A.tsv B.tsv              two evals on the same starts: the difference A - B start by start, its 95%
 *                                         interval, the starts A won and lost
 *   rawstat table MANIFEST [-o PREFIX]    a manifest of evals, recipes and comparisons into the tables below
 *
 * An eval file is raweval.sh's TSV, a header and a row an episode: model, arm, shard, gstart (the start's number in
 * the task's held-out library), rep (which of the start's episodes), env, success, deaths, steps. Only gstart,
 * success and deaths are read (by their header names).
 *
 * Manifest lines (tab separated, # comments):
 *   eval  GROUP  LABEL  FILE        an eval; GROUP its recipe (the runs of one recipe share it)
 *   pair  NAME   LABEL_A  LABEL_B   the paired difference of two evals (A minus B), same starts
 *   gcmp  NAME   GROUP_A  GROUP_B   two recipes: the difference of their runs' means, Welch; and start by start, the
 *                                   group means over the runs
 *
 * The standard error of a success rate is the start-cluster one: a start's episodes (a start played several times)
 * are one cluster, so the replicates do not count as new starts. Variance components are the one-way ANOVA's
 * (a start the group, its episodes the members). A recipe's runs: the spread of the runs' means less the mean of
 * their squared standard errors is the training runs' own variance (the eval noise taken out of the spread); the old
 * evals' noise at n episodes is the binomial p (1 - p) / n.
 *
 * Built by csrc/runtime/train/raweval.mk into out/runtime/train/rawstat; plain C, libm only. */
#define _GNU_SOURCE
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXEV 1024
#define MAXST 8192

struct ev {
    char label[128], group[128];
    int n, nst;                 /* episodes; starts with an episode */
    int m[MAXST];               /* [start] episodes */
    int y[MAXST];               /* [start] successes */
    int ndead;                  /* deaths */
    double p, se, se_iid, deaths;
    double sb, sw, icc;         /* between-start and within-start sd; the intraclass correlation (-1: no replicate) */
    double ms_b, ms_w;
};

static struct ev *EV[MAXEV];
static int nev;

static double tq(int df)        /* the 97.5% quantile of Student's t (Cornish-Fisher, df >= 1) */
{
    double z = 1.959964;
    if (df < 1) df = 1;
    double d = df;
    double t = z + (z * z * z + z) / (4 * d) + (5 * pow(z, 5) + 16 * z * z * z + 3 * z) / (96 * d * d) +
               (3 * pow(z, 7) + 19 * pow(z, 5) + 17 * z * z * z - 15 * z) / (384 * d * d * d);
    return df == 1 ? 12.706 : df == 2 ? 4.303 : t;
}

static int colof(char *hdr, const char *name)
{
    int c = 0;
    for (char *sv, *t = strtok_r(hdr, "\t\n", &sv); t; t = strtok_r(NULL, "\t\n", &sv), ++c)
        if (!strcmp(t, name)) return c;
    return -1;
}

static void stats(struct ev *e)
{
    int C = 0;
    double Y = 0, M = 0;
    for (int s = 0; s < MAXST; ++s)
        if (e->m[s]) ++C, Y += e->y[s], M += e->m[s];
    e->nst = C, e->n = (int)M;
    e->p = M > 0 ? Y / M : NAN;
    e->se_iid = M > 0 ? sqrt(e->p * (1 - e->p) / M) : NAN;
    double ss = 0;
    for (int s = 0; s < MAXST; ++s)
        if (e->m[s]) ss += (e->y[s] - e->p * e->m[s]) * (e->y[s] - e->p * e->m[s]);
    e->se = C > 1 ? sqrt(ss * C / (C - 1)) / M : NAN;
    /* one-way ANOVA */
    double ssw = 0, ssb = 0, dfw = 0, m2 = 0;
    for (int s = 0; s < MAXST; ++s)
    {
        if (!e->m[s]) continue;
        double ms = e->m[s], ps = e->y[s] / ms;
        ssw += e->y[s] - e->y[s] * e->y[s] / ms;
        dfw += ms - 1;
        ssb += ms * (ps - e->p) * (ps - e->p);
        m2 += ms * ms;
    }
    e->ms_w = dfw > 0 ? ssw / dfw : NAN;
    e->ms_b = C > 1 ? ssb / (C - 1) : NAN;
    double m0 = C > 1 ? (M - m2 / M) / (C - 1) : NAN;
    double sb2 = dfw > 0 ? (e->ms_b - e->ms_w) / m0 : NAN;
    e->sw = dfw > 0 ? sqrt(e->ms_w) : NAN;
    e->sb = dfw > 0 ? sqrt(sb2 > 0 ? sb2 : 0) : NAN;
    e->icc = dfw > 0 && (sb2 > 0 ? sb2 : 0) + e->ms_w > 0 ? (sb2 > 0 ? sb2 : 0) / ((sb2 > 0 ? sb2 : 0) + e->ms_w) : NAN;
}

/* need > 0: a success is a count (the file's last column) of at least need, in place of the file's own success */
static struct ev *load(const char *path, const char *label, const char *group, int need)
{
    FILE *f = fopen(path, "r");
    if (!f)
    {
        fprintf(stderr, "rawstat: %s: cannot open\n", path);
        return NULL;
    }
    struct ev *e = calloc(1, sizeof *e);
    snprintf(e->label, sizeof e->label, "%s", label ? label : path);
    snprintf(e->group, sizeof e->group, "%s", group ? group : "");
    char line[4096], hdr[4096];
    if (!fgets(line, sizeof line, f))
    {
        fclose(f);
        free(e);
        return NULL;
    }
    int cs = -1, cg = -1, cd = -1, cc = -1;
    snprintf(hdr, sizeof hdr, "%s", line);
    cc = colof(hdr, "count");
    snprintf(hdr, sizeof hdr, "%s", line);
    cg = colof(hdr, "gstart");
    snprintf(hdr, sizeof hdr, "%s", line);
    cs = colof(hdr, "success");
    snprintf(hdr, sizeof hdr, "%s", line);
    cd = colof(hdr, "deaths");
    if (cg < 0 || cs < 0 || (need > 0 && cc < 0))
    {
        fprintf(stderr, "rawstat: %s: no gstart/success column\n", path);
        fclose(f);
        free(e);
        return NULL;
    }
    while (fgets(line, sizeof line, f))
    {
        int g = -1, s = 0, d = 0, c = 0, k = 0;
        for (char *sv, *t = strtok_r(line, "\t\n", &sv); t; t = strtok_r(NULL, "\t\n", &sv), ++c)
        {
            if (c == cg) g = atoi(t);
            if (c == cs) s = atoi(t);
            if (c == cd) d = atoi(t);
            if (c == cc) k = atoi(t);
        }
        if (need > 0) s = k >= need;
        if (g < 0 || g >= MAXST) continue;
        ++e->m[g], e->y[g] += s != 0, e->ndead += d;
    }
    fclose(f);
    stats(e);
    e->deaths = e->n ? (double)e->ndead / e->n : NAN;
    return e;
}

/* the difference A - B over the starts both have (each start's mean over its episodes) */
struct pr {
    int c, win, lose, tie;
    double d, se, lo, hi, pa, pb;
};

static struct pr pair(const struct ev *a, const struct ev *b)
{
    struct pr r = {0};
    double s1 = 0, s2 = 0, sa = 0, sb = 0;
    for (int s = 0; s < MAXST; ++s)
    {
        if (!a->m[s] || !b->m[s]) continue;
        double pa = (double)a->y[s] / a->m[s], pb = (double)b->y[s] / b->m[s], d = pa - pb;
        ++r.c, s1 += d, s2 += d * d, sa += pa, sb += pb;
        if (d > 1e-12) ++r.win;
        else if (d < -1e-12) ++r.lose;
        else ++r.tie;
    }
    if (r.c < 2) return r;
    r.d = s1 / r.c, r.pa = sa / r.c, r.pb = sb / r.c;
    double var = (s2 - s1 * s1 / r.c) / (r.c - 1);
    r.se = sqrt(var > 0 ? var / r.c : 0);
    double t = tq(r.c - 1);
    r.lo = r.d - t * r.se, r.hi = r.d + t * r.se;
    return r;
}

static const struct ev *find(const char *label)
{
    for (int i = 0; i < nev; ++i)
        if (!strcmp(EV[i]->label, label)) return EV[i];
    return NULL;
}

static void print_one(const struct ev *e)
{
    printf("%-34s n %4d  starts %4d  success %.3f  se %.3f (iid %.3f)  deaths %.3f  sd between starts %.3f, within %.3f, "
           "icc %.2f\n", e->label, e->n, e->nst, e->p, e->se, e->se_iid, e->deaths, e->sb, e->sw, e->icc);
}

static int cmpd(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* a recipe's runs: the evals of one group */
struct grp {
    char name[128];
    int k;
    double p[MAXEV], se2[MAXEV], w2[MAXEV];   /* a run's mean, its cluster variance, its within-start variance */
    double mean, sd, lo, hi, mse2, mw2, st2, st2w, pvar, mmsw;
    const struct ev *run[MAXEV];
};

static struct grp *G[256];
static int ng;

static struct grp *grp_of(const char *name)
{
    for (int i = 0; i < ng; ++i)
        if (!strcmp(G[i]->name, name)) return G[i];
    G[ng] = calloc(1, sizeof *G[ng]);
    snprintf(G[ng]->name, sizeof G[ng]->name, "%s", name);
    return G[ng++];
}

static void grp_stats(struct grp *g)
{
    double s = 0, lo = 9, hi = -9, se2 = 0, pv = 0, w2 = 0, msw = 0;
    for (int i = 0; i < g->k; ++i)
        s += g->p[i], se2 += g->se2[i], w2 += g->w2[i], msw += g->run[i]->ms_w == g->run[i]->ms_w ? g->run[i]->ms_w : 0,
            pv += g->p[i] * (1 - g->p[i]), lo = g->p[i] < lo ? g->p[i] : lo, hi = g->p[i] > hi ? g->p[i] : hi;
    g->mean = s / g->k, g->lo = lo, g->hi = hi, g->mse2 = se2 / g->k, g->mw2 = w2 / g->k, g->mmsw = msw / g->k;
    double ss = 0;
    for (int i = 0; i < g->k; ++i) ss += (g->p[i] - g->mean) * (g->p[i] - g->mean);
    g->sd = g->k > 1 ? sqrt(ss / (g->k - 1)) : NAN;
    /* the training runs' variance: the runs' spread less what the eval adds. The runs play the same starts, so the
     * start sampling is common to all and only the within-start noise is each eval's own (st2w); st2 takes the whole
     * start-cluster variance off (a new set of starts for every run); either may come out negative */
    g->st2 = g->k > 1 ? (ss / (g->k - 1) - g->mse2) : NAN;
    g->st2w = g->k > 1 ? (ss / (g->k - 1) - g->mw2) : NAN;
    g->pvar = pv / g->k;                                       /* the mean p (1 - p) of its runs */
}

int main(int argc, char **argv)
{
    if (argc >= 3 && !strcmp(argv[1], "one"))
    {
        struct ev *e = load(argv[2], argv[2], "", argc > 3 ? atoi(argv[3]) : 0);
        if (!e) return 1;
        print_one(e);
        return 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "pair"))
    {
        struct ev *a = load(argv[2], argv[2], "", 0), *b = load(argv[3], argv[3], "", 0);
        if (!a || !b) return 1;
        struct pr r = pair(a, b);
        printf("A %.3f, B %.3f over %d shared starts; A - B %+.3f (95%% %+.3f to %+.3f); A won %d starts, lost %d, tied %d\n",
               r.pa, r.pb, r.c, r.d, r.lo, r.hi, r.win, r.lose, r.tie);
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "table"))
    {
        FILE *f = fopen(argv[2], "r");
        if (!f)
        {
            fprintf(stderr, "rawstat: %s: cannot open\n", argv[2]);
            return 1;
        }
        char line[2048];
        char *pl[512];
        int np = 0;
        printf("kind\tgroup\tlabel\tepisodes\tstarts\tsuccess\tse\tse_iid\tdeaths\tsd_between_starts\tsd_within_start\ticc\n");
        while (fgets(line, sizeof line, f))
        {
            if (line[0] == '#' || line[0] == '\n') continue;
            char *fld[6] = {0};
            int c = 0;
            for (char *sv, *t = strtok_r(line, "\t\n", &sv); t && c < 6; t = strtok_r(NULL, "\t\n", &sv)) fld[c++] = t;
            if (c < 4) continue;
            if (!strcmp(fld[0], "eval"))
            {
                struct ev *e = load(fld[3], fld[2], fld[1], c > 4 ? atoi(fld[4]) : 0);
                if (!e || nev >= MAXEV) continue;
                EV[nev++] = e;
                struct grp *g = grp_of(fld[1]);
                g->p[g->k] = e->p, g->se2[g->k] = e->se * e->se;
                g->w2[g->k] = (e->ms_w == e->ms_w ? e->ms_w : e->p * (1 - e->p)) / e->n;
                g->run[g->k++] = e;
                printf("EVAL\t%s\t%s\t%d\t%d\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.2f\n", e->group, e->label, e->n, e->nst,
                       e->p, e->se, e->se_iid, e->deaths, e->sb, e->sw, e->icc);
            }
            else if (np < 512)
                pl[np++] = strdup(line[0] ? line : "");
        }
        fclose(f);
        /* the pairs and comparisons need the full manifest again (their lines were consumed above): reread */
        f = fopen(argv[2], "r");
        printf("\nkind\tname\ta\tb\tstarts\ta_mean\tb_mean\tdiff\tci_lo\tci_hi\ta_won\ta_lost\ttied\n");
        while (fgets(line, sizeof line, f))
        {
            if (line[0] == '#' || line[0] == '\n') continue;
            char *fld[6] = {0};
            int c = 0;
            for (char *sv, *t = strtok_r(line, "\t\n", &sv); t && c < 6; t = strtok_r(NULL, "\t\n", &sv)) fld[c++] = t;
            if (c < 4 || strcmp(fld[0], "pair")) continue;
            const struct ev *a = find(fld[2]), *b = find(fld[3]);
            if (!a || !b)
            {
                printf("PAIR\t%s\t%s\t%s\tMISSING\n", fld[1], fld[2], fld[3]);
                continue;
            }
            struct pr r = pair(a, b);
            printf("PAIR\t%s\t%s\t%s\t%d\t%.3f\t%.3f\t%+.3f\t%+.3f\t%+.3f\t%d\t%d\t%d\n", fld[1], fld[2], fld[3], r.c, r.pa,
                   r.pb, r.d, r.lo, r.hi, r.win, r.lose, r.tie);
        }
        fclose(f);
        printf("\nkind\tgroup\truns\tmean\tsd_of_runs\tmin\tmax\teval_se_cluster\teval_se_within\tsd_training\tsd_training_cluster\t"
               "eval_share_of_spread_at_64\tat_128\tat_this_n\tbinomial_share_at_64\tat_128\n");
        double pooled_st2 = 0, pooled_pv = 0, pooled_msw = 0;
        int npool = 0;
        for (int i = 0; i < ng; ++i)
        {
            struct grp *g = G[i];
            grp_stats(g);
            if (g->k < 2) continue;
            double st2 = g->st2w > 0 ? g->st2w : 0, stc = g->st2 > 0 ? g->st2 : 0;
            /* the old evals' own noise at n episodes against the spread they saw: the within-start variance over n (the
             * starts being the same for every run), and the binomial p (1 - p) / n the lanes quoted */
            double e64 = g->mmsw / 64, e128 = g->mmsw / 128, b64 = g->pvar / 64, b128 = g->pvar / 128;
            printf("RECIPE\t%s\t%d\t%.3f\t%.3f\t%.3f\t%.3f\t%.4f\t%.4f\t%.3f\t%.3f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\n", g->name, g->k,
                   g->mean, g->sd, g->lo, g->hi, sqrt(g->mse2), sqrt(g->mw2), sqrt(st2), sqrt(stc), e64 / (st2 + e64),
                   e128 / (st2 + e128), g->mw2 / (st2 + g->mw2), b64 / (st2 + b64), b128 / (st2 + b128));
            if (g->k >= 5 && g->lo > 0.05) pooled_st2 += st2, pooled_pv += g->pvar, pooled_msw += g->mmsw, ++npool;
        }
        printf("\nkind\tname\ta\tb\truns_a\truns_b\tmean_a\tmean_b\tdiff\tci_lo\tci_hi\tse\tdf\n");
        f = fopen(argv[2], "r");
        while (fgets(line, sizeof line, f))
        {
            if (line[0] == '#' || line[0] == '\n') continue;
            char *fld[6] = {0};
            int c = 0;
            for (char *sv, *t = strtok_r(line, "\t\n", &sv); t && c < 6; t = strtok_r(NULL, "\t\n", &sv)) fld[c++] = t;
            if (c < 4 || strcmp(fld[0], "gcmp")) continue;
            struct grp *a = NULL, *b = NULL;
            for (int i = 0; i < ng; ++i)
            {
                if (!strcmp(G[i]->name, fld[2])) a = G[i];
                if (!strcmp(G[i]->name, fld[3])) b = G[i];
            }
            if (!a || !b)
            {
                printf("GCMP\t%s\t%s\t%s\tMISSING\n", fld[1], fld[2], fld[3]);
                continue;
            }
            double va = a->k > 1 ? a->sd * a->sd / a->k : 0, vb = b->k > 1 ? b->sd * b->sd / b->k : 0;
            double se = sqrt(va + vb), df = (va + vb) * (va + vb) /
                                          ((a->k > 1 ? va * va / (a->k - 1) : 0) + (b->k > 1 ? vb * vb / (b->k - 1) : 0) + 1e-300);
            double d = a->mean - b->mean, t = tq((int)floor(df));
            printf("GCMP\t%s\t%s\t%s\t%d\t%d\t%.3f\t%.3f\t%+.3f\t%+.3f\t%+.3f\t%.3f\t%.1f\n", fld[1], a->name, b->name, a->k,
                   b->k, a->mean, b->mean, d, d - t * se, d + t * se, se, df);
        }
        fclose(f);
        if (npool)
        {
            double st2 = pooled_st2 / npool, pv = pooled_pv / npool, msw = pooled_msw / npool, z2 = 2.8016 * 2.8016;
            printf("\nPOWER\tpooled over %d recipes with 5+ runs and no collapsed run (the seed-split groups are not pooled): sd of a recipe's runs, training only %.3f; mean within-start variance "
                   "%.3f; mean p(1-p) %.3f\n", npool, sqrt(st2), msw, pv);
            printf("POWER\truns of EACH recipe to tell two recipes 0.05 apart (95%%, power 80%%, one eval a run), by episodes an eval:\n");
            printf("POWER\tepisodes\t64\t128\t256\t1000\tinfinite\n");
            double ns[5] = {64, 128, 256, 1000, 1e18};
            printf("POWER\truns, both recipes on the same starts (the within-start noise)");
            for (int j = 0; j < 5; ++j) printf("\t%.1f", 2 * (st2 + msw / ns[j]) * z2 / (0.05 * 0.05));
            printf("\nPOWER\truns, each eval on its own starts (the binomial noise)");
            for (int j = 0; j < 5; ++j) printf("\t%.1f", 2 * (st2 + pv / ns[j]) * z2 / (0.05 * 0.05));
            printf("\n");
        }
        (void)cmpd;
        (void)pl;
        return 0;
    }
    fprintf(stderr, "usage: rawstat one FILE | pair A B | table MANIFEST\n");
    return 2;
}
