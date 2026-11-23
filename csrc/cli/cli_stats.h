/* nether CLI: the exact statistics of an eval (csrc/cli/cli_stats.c).
 *
 * An eval directory is what ntloop --score --out DIR writes: DIR/episodes.jsonl (one JSON line an episode: env,
 * start, ret, deaths, len, the boolean milestones in "ms", the macros' steps in "macro", the dimensions in "dims")
 * and DIR/score.json. Read here directly, no jq or awk:
 *
 *   one run:   n, depth (ntloop's: the 15 milestones of neth_report.c's END_NAMES, the Nether from dims) mean and
 *              standard error, return, deaths an episode, the raw (no-macro) share of steps, each milestone's count,
 *              rate and exact Wilson 95% interval
 *   two runs:  paired episode by episode on env (each env plays the same start and sampler stream in both), each
 *              milestone's exact two-sided McNemar binomial p (b candidate only, c baseline only:
 *              min(1, 2 sum_{k<=min(b,c)} C(b+c,k) / 2^(b+c)), Loader's saddle-point binomial, accurate to the last
 *              digits up to n = 100,000 and beyond) and its z (b-c)/sqrt(b+c); the paired z of depth, return and
 *              deaths (mean difference over its standard error) and its two-sided normal p erfc(|z|/sqrt 2). */
#ifndef NETH_CLI_STATS_H
#define NETH_CLI_STATS_H

#include "cli.h"

#define CS_MAXMS 48

struct cs_ep {
    long env, key;          /* key: env and its occurrence (one episode an env in an eval) */
    double start, seed;     /* NAN when absent */
    double ret, deaths, len, raw, depth;
    unsigned char ms[CS_MAXMS];
};

struct cs_run {
    char *dir, *model;
    int n, nms, has_raw;
    struct cs_ep *ep;
    char *ms[CS_MAXMS];      /* milestone names: "ms" keys in file order, "nether" (from dims) last */
    double score_depth, sps; /* score.json's, NAN when absent */
};

struct cs_mean {
    int n;
    double mean, sd, se;
};

struct cs_summary {
    int n;
    struct cs_mean depth, ret, deaths, len, raw;
    int k[CS_MAXMS];
    double rate[CS_MAXMS], lo[CS_MAXMS], hi[CS_MAXMS];
};

struct cs_bin {
    int both, b, c, neither; /* b: the candidate only, c: the baseline only */
    double p, z;
};

struct cs_cont {
    int n;
    double cand, base, delta, se, z, p;
};

struct cs_compare {
    int n, cand_unpaired, base_unpaired, start_mismatch;
    int nms;
    char *ms[CS_MAXMS];
    int ci[CS_MAXMS], bi[CS_MAXMS]; /* each name's index in the candidate's and the baseline's lists, -1 none */
    struct cs_bin bin[CS_MAXMS];
    struct cs_cont depth, ret, deaths, raw;
};

/* DIR/episodes.jsonl (and DIR/score.json when there); 0, or -1 with the reason in err */
int cs_load(const char *dir, struct cs_run *r, char *err, size_t en);
void cs_free(struct cs_run *r);
int cs_ms_index(const struct cs_run *r, const char *name);

void cs_summarize(const struct cs_run *r, struct cs_summary *s);
/* the two runs paired on env; 0, or -1 when no episode pairs */
int cs_compare_runs(const struct cs_run *c, const struct cs_run *b, struct cs_compare *o);

double cs_mcnemar_exact(long b, long c);
/* the Wilson score interval of k of n at the two-sided 95% level */
void cs_wilson(long k, long n, double *lo, double *hi);
double cs_norm_p2(double z);
/* log of the binomial(n, 1/2) probability of x, exact to rounding (Loader 2000) */
double cs_log_dbinom_half(long x, long n);

/* a named metric (depth, ret, deaths, raw, or a milestone) of a comparison: candidate and baseline values,
 * delta (candidate - baseline), p, z (signed so that positive is better), and whether lower is better. 0, or -1
 * when the name is unknown. Without a baseline (b and cmp NULL) only cand is set. */
int cs_metric(const struct cs_run *cr, const struct cs_summary *c, const struct cs_summary *b, const struct cs_compare *cmp,
              const char *name, double *cand, double *base, double *delta, double *p, double *z, int *lower);

/* eval.json and the table */
void cs_json(struct sb *o, const struct cs_run *cr, const struct cs_summary *c, const struct cs_run *br,
             const struct cs_summary *b, const struct cs_compare *cmp);
void cs_print(FILE *f, const struct cs_run *cr, const struct cs_summary *c, const struct cs_run *br,
              const struct cs_summary *b, const struct cs_compare *cmp);

#endif
