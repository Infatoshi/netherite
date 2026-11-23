/* nether CLI: the exact statistics of an eval (cli_stats.h). */
#define _GNU_SOURCE
#include "cli_stats.h"
#include "cli_json.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ntloop's depth (csrc/runtime/train/neth_report.c END_NAMES): the milestones counted, in its table's order */
static const char *const DEPTH_MS[] = {"wooden_pickaxe", "stone_pickaxe", "furnace", "iron_ingot", "iron_pickaxe",
                                       "bucket", "flint_and_steel", "diamond_pickaxe", "obsidian", "nether",
                                       "blaze_rod", "ender_pearl", "ender_eye", "end", "dragon"};
#define NDEPTH ((int)(sizeof DEPTH_MS / sizeof DEPTH_MS[0]))
#define Z975 1.959963984540054 /* the standard normal's 97.5% point */

/* ---- the binomial: Loader's saddle-point form, as R's dbinom (C. Loader, "Fast and accurate computation of
 * binomial probabilities", 2000) */

#define LN_SQRT_2PI 0.918938533204672741780329736406
#define LN_2PI 1.837877066409345483560659472811

/* log(n!) - log(sqrt(2 pi n) (n/e)^n) */
static double stirlerr(double n)
{
    if (n <= 15) {
        /* exact for the small integers: log n! as a sum (each log correctly rounded) */
        double lf = 0;
        for (int i = 2; i <= (int)n; ++i) lf += log((double)i);
        if (n == 0) return 0;
        return lf - (n + 0.5) * log(n) + n - LN_SQRT_2PI;
    }
    const double S0 = 1.0 / 12, S1 = 1.0 / 360, S2 = 1.0 / 1260, S3 = 1.0 / 1680, S4 = 1.0 / 1188;
    double nn = n * n;
    if (n > 500) return (S0 - S1 / nn) / n;
    if (n > 80) return (S0 - (S1 - S2 / nn) / nn) / n;
    if (n > 35) return (S0 - (S1 - (S2 - S3 / nn) / nn) / nn) / n;
    return (S0 - (S1 - (S2 - (S3 - S4 / nn) / nn) / nn) / nn) / n;
}

/* x log(x/np) + np - x, stably near x = np */
static double bd0(double x, double np)
{
    if (fabs(x - np) < 0.1 * (x + np)) {
        double v = (x - np) / (x + np), s = (x - np) * v, ej = 2 * x * v;
        v *= v;
        for (int j = 1; j < 1000; ++j) {
            ej *= v;
            double s1 = s + ej / (2 * j + 1);
            if (s1 == s) return s1;
            s = s1;
        }
    }
    return x * log(x / np) + np - x;
}

double cs_log_dbinom_half(long x, long n)
{
    if (x < 0 || x > n) return -INFINITY;
    if (x == 0 || x == n) return -(double)n * M_LN2;
    double X = (double)x, N = (double)n;
    double lc = stirlerr(N) - stirlerr(X) - stirlerr(N - X) - bd0(X, N * 0.5) - bd0(N - X, N * 0.5);
    double lf = LN_2PI + log(X) + log1p(-X / N);
    return lc - 0.5 * lf;
}

double cs_mcnemar_exact(long b, long c)
{
    long n = b + c;
    if (n <= 0) return 1.0;
    long k = b < c ? b : c;
    /* P(X <= k) = t_k (1 + t_{k-1}/t_k + ...), t_{i-1}/t_i = i / (n - i + 1): the ratios exact, the sum from the
     * largest term down */
    double lt = cs_log_dbinom_half(k, n), r = 1, s = 1;
    for (long i = k; i > 0; --i) {
        r *= (double)i / (double)(n - i + 1);
        s += r;
        if (r < 1e-20 * s) break;
    }
    double p = 2.0 * exp(lt) * s;
    return p > 1 ? 1 : p;
}

void cs_wilson(long k, long n, double *lo, double *hi)
{
    if (n <= 0) {
        *lo = 0, *hi = 1;
        return;
    }
    double N = (double)n, p = (double)k / N, z2 = Z975 * Z975;
    double den = 1 + z2 / N, centre = (p + z2 / (2 * N)) / den;
    double half = Z975 * sqrt(p * (1 - p) / N + z2 / (4 * N * N)) / den;
    *lo = centre - half;
    *hi = centre + half;
    if (*lo < 0) *lo = 0;
    if (*hi > 1) *hi = 1;
}

double cs_norm_p2(double z)
{
    if (isnan(z)) return NAN;
    return erfc(fabs(z) / M_SQRT2);
}

/* ---- loading */

void cs_free(struct cs_run *r)
{
    free(r->dir);
    free(r->model);
    free(r->ep);
    for (int i = 0; i < r->nms; ++i) free(r->ms[i]);
    memset(r, 0, sizeof *r);
}

int cs_ms_index(const struct cs_run *r, const char *name)
{
    for (int i = 0; i < r->nms; ++i)
        if (!strcmp(r->ms[i], name)) return i;
    return -1;
}

static int ms_slot(struct cs_run *r, const char *name)
{
    int i = cs_ms_index(r, name);
    if (i >= 0 || r->nms >= CS_MAXMS) return i;
    r->ms[r->nms] = xstrdup(name);
    return r->nms++;
}

static int cmp_key(const void *a, const void *b)
{
    const struct cs_ep *x = a, *y = b;
    return (x->key > y->key) - (x->key < y->key);
}

int cs_load(const char *dir, struct cs_run *r, char *err, size_t en)
{
    memset(r, 0, sizeof *r);
    r->score_depth = r->sps = NAN;
    char *path = xasprintf("%s/episodes.jsonl", dir);
    char *s = cli_read_file(path, NULL);
    if (!s) {
        snprintf(err, en, "cannot read %s", path);
        free(path);
        return -1;
    }
    r->dir = xstrdup(dir);
    int cap = 0, line = 0, nether = -1, any_dims = 0;
    /* first pass: the names (the first line's order), so every episode's ms bytes share one index */
    long *occ_env = NULL;
    int *occ_n = NULL, nocc = 0;
    for (char *p = s, *e; *p; p = e) {
        e = strchr(p, '\n');
        if (e) *e++ = 0;
        else e = p + strlen(p);
        ++line;
        while (*p == ' ') ++p;
        if (!*p) continue;
        char jerr[160];
        struct jv *v = jv_parse(p, jerr, sizeof jerr);
        if (!v) {
            snprintf(err, en, "%s:%d: %s", path, line, jerr);
            free(s);
            free(path);
            free(occ_env);
            free(occ_n);
            return -1;
        }
        if (r->n == cap) {
            cap = cap ? cap * 2 : 256;
            r->ep = xrealloc(r->ep, sizeof *r->ep * (size_t)cap);
        }
        struct cs_ep *ep = &r->ep[r->n];
        memset(ep, 0, sizeof *ep);
        ep->env = (long)jv_num(v, "env", (double)r->n);
        int o = 0;
        for (int i = 0; i < nocc; ++i)
            if (occ_env[i] == ep->env) {
                o = ++occ_n[i];
                break;
            }
        if (!o) {
            occ_env = xrealloc(occ_env, sizeof *occ_env * (size_t)(nocc + 1));
            occ_n = xrealloc(occ_n, sizeof *occ_n * (size_t)(nocc + 1));
            occ_env[nocc] = ep->env;
            occ_n[nocc++] = 0;
        }
        ep->key = ep->env * 65536L + o;
        ep->start = jv_num(v, "start", NAN);
        ep->seed = jv_num(v, "seed", NAN);
        ep->ret = jv_num(v, "ret", NAN);
        ep->deaths = jv_num(v, "deaths", 0);
        ep->len = jv_num(v, "len", NAN);
        const struct jv *ms = jv_get(v, "ms");
        if (ms && ms->t == JV_OBJ)
            for (int i = 0; i < ms->n; ++i) {
                int k = ms_slot(r, ms->keys[i]);
                if (k >= 0) ep->ms[k] = ms->kids[i]->num != 0;
            }
        const struct jv *dims = jv_get(v, "dims");
        if (dims && dims->t == JV_ARR) {
            any_dims = 1;
            if (nether < 0) nether = ms_slot(r, "nether");
            for (int i = 0; i < dims->n; ++i)
                if (dims->kids[i]->t == JV_STR && !strcmp(dims->kids[i]->str, "nether") && nether >= 0) ep->ms[nether] = 1;
        }
        const struct jv *mac = jv_get(v, "macro");
        ep->raw = NAN;
        if (mac && mac->t == JV_OBJ) {
            double tot = 0, none = 0;
            for (int i = 0; i < mac->n; ++i) {
                tot += mac->kids[i]->num;
                if (!strcmp(mac->keys[i], "none")) none = mac->kids[i]->num;
            }
            if (tot > 0) ep->raw = none / tot, r->has_raw = 1;
        }
        jv_free(v);
        ++r->n;
    }
    free(occ_env);
    free(occ_n);
    free(s);
    free(path);
    (void)any_dims;
    /* "nether" goes last whatever line first named it: move it */
    if (nether >= 0 && nether != r->nms - 1) {
        char *nm = r->ms[nether];
        for (int i = nether; i < r->nms - 1; ++i) r->ms[i] = r->ms[i + 1];
        r->ms[r->nms - 1] = nm;
        for (int q = 0; q < r->n; ++q) {
            unsigned char b = r->ep[q].ms[nether];
            for (int i = nether; i < r->nms - 1; ++i) r->ep[q].ms[i] = r->ep[q].ms[i + 1];
            r->ep[q].ms[r->nms - 1] = b;
        }
    }
    /* depth: ntloop's 15 */
    int di[NDEPTH];
    for (int m = 0; m < NDEPTH; ++m) di[m] = cs_ms_index(r, DEPTH_MS[m]);
    for (int q = 0; q < r->n; ++q) {
        int d = 0;
        for (int m = 0; m < NDEPTH; ++m) d += di[m] >= 0 && r->ep[q].ms[di[m]];
        r->ep[q].depth = d;
    }
    qsort(r->ep, (size_t)r->n, sizeof *r->ep, cmp_key);
    /* score.json: the model and what ntloop printed */
    char *sp = xasprintf("%s/score.json", dir);
    char *sj = cli_read_file(sp, NULL);
    if (sj) {
        char jerr[160];
        struct jv *v = jv_parse(sj, jerr, sizeof jerr);
        if (v) {
            const char *m = jv_str(v, "model", NULL);
            if (m) r->model = xstrdup(m);
            r->score_depth = jv_num(v, "depth", NAN);
            r->sps = jv_num(v, "env_steps_per_s", NAN);
            jv_free(v);
        }
        free(sj);
    }
    free(sp);
    if (!r->n) {
        snprintf(err, en, "%s/episodes.jsonl has no episodes", dir);
        return -1;
    }
    return 0;
}

/* ---- one run */

static struct cs_mean mean_of(const struct cs_run *r, size_t off)
{
    struct cs_mean m = {0, NAN, NAN, NAN};
    double s = 0;
    for (int q = 0; q < r->n; ++q) {
        double x = *(const double *)((const char *)&r->ep[q] + off);
        if (isnan(x)) continue;
        s += x;
        ++m.n;
    }
    if (!m.n) return m;
    m.mean = s / m.n;
    double ss = 0;
    for (int q = 0; q < r->n; ++q) {
        double x = *(const double *)((const char *)&r->ep[q] + off);
        if (!isnan(x)) ss += (x - m.mean) * (x - m.mean);
    }
    if (m.n > 1) m.sd = sqrt(ss / (m.n - 1)), m.se = m.sd / sqrt((double)m.n);
    return m;
}

void cs_summarize(const struct cs_run *r, struct cs_summary *s)
{
    memset(s, 0, sizeof *s);
    s->n = r->n;
    s->depth = mean_of(r, offsetof(struct cs_ep, depth));
    s->ret = mean_of(r, offsetof(struct cs_ep, ret));
    s->deaths = mean_of(r, offsetof(struct cs_ep, deaths));
    s->len = mean_of(r, offsetof(struct cs_ep, len));
    s->raw = mean_of(r, offsetof(struct cs_ep, raw));
    for (int m = 0; m < r->nms; ++m) {
        for (int q = 0; q < r->n; ++q) s->k[m] += r->ep[q].ms[m];
        s->rate[m] = r->n ? (double)s->k[m] / r->n : NAN;
        cs_wilson(s->k[m], r->n, &s->lo[m], &s->hi[m]);
    }
}

/* ---- two runs, paired */

static void paired(struct cs_cont *o, const double *dc, const double *db, int n)
{
    memset(o, 0, sizeof *o);
    o->cand = o->base = o->delta = o->se = o->z = o->p = NAN;
    double sc = 0, sbs = 0, sd = 0;
    int k = 0;
    for (int i = 0; i < n; ++i) {
        if (isnan(dc[i]) || isnan(db[i])) continue;
        sc += dc[i], sbs += db[i], sd += dc[i] - db[i];
        ++k;
    }
    o->n = k;
    if (!k) return;
    o->cand = sc / k, o->base = sbs / k, o->delta = sd / k;
    double ss = 0;
    for (int i = 0; i < n; ++i)
        if (!isnan(dc[i]) && !isnan(db[i])) {
            double d = dc[i] - db[i] - o->delta;
            ss += d * d;
        }
    if (k < 2) return;
    o->se = sqrt(ss / (k - 1)) / sqrt((double)k);
    if (o->se > 0) o->z = o->delta / o->se;
    else o->z = o->delta == 0 ? 0 : (o->delta > 0 ? INFINITY : -INFINITY);
    o->p = o->se > 0 ? cs_norm_p2(o->z) : (o->delta == 0 ? 1 : 0);
}

int cs_compare_runs(const struct cs_run *c, const struct cs_run *b, struct cs_compare *o)
{
    memset(o, 0, sizeof *o);
    /* the names: the candidate's order, then any only the baseline has */
    for (int i = 0; i < c->nms && o->nms < CS_MAXMS; ++i) o->ms[o->nms++] = c->ms[i];
    for (int i = 0; i < b->nms && o->nms < CS_MAXMS; ++i)
        if (cs_ms_index(c, b->ms[i]) < 0) o->ms[o->nms++] = b->ms[i];
    for (int m = 0; m < o->nms; ++m) o->ci[m] = cs_ms_index(c, o->ms[m]), o->bi[m] = cs_ms_index(b, o->ms[m]);
    int cap = c->n < b->n ? c->n : b->n;
    double *v[8];
    for (int i = 0; i < 8; ++i) v[i] = xmalloc(sizeof(double) * (size_t)(cap ? cap : 1));
    int i = 0, j = 0, n = 0;
    while (i < c->n && j < b->n) {
        const struct cs_ep *x = &c->ep[i], *y = &b->ep[j];
        if (x->key < y->key) {
            ++o->cand_unpaired, ++i;
            continue;
        }
        if (x->key > y->key) {
            ++o->base_unpaired, ++j;
            continue;
        }
        if (!isnan(x->start) && !isnan(y->start) && x->start != y->start) ++o->start_mismatch;
        for (int m = 0; m < o->nms; ++m) {
            int a = o->ci[m] >= 0 && x->ms[o->ci[m]], bb = o->bi[m] >= 0 && y->ms[o->bi[m]];
            if (a && bb) ++o->bin[m].both;
            else if (a) ++o->bin[m].b;
            else if (bb) ++o->bin[m].c;
            else ++o->bin[m].neither;
        }
        v[0][n] = x->depth, v[1][n] = y->depth, v[2][n] = x->ret, v[3][n] = y->ret;
        v[4][n] = x->deaths, v[5][n] = y->deaths, v[6][n] = x->raw, v[7][n] = y->raw;
        ++n, ++i, ++j;
    }
    o->cand_unpaired += c->n - i;
    o->base_unpaired += b->n - j;
    o->n = n;
    for (int m = 0; m < o->nms; ++m) {
        struct cs_bin *e = &o->bin[m];
        e->p = cs_mcnemar_exact(e->b, e->c);
        e->z = e->b + e->c ? (e->b - e->c) / sqrt((double)(e->b + e->c)) : 0;
    }
    paired(&o->depth, v[0], v[1], n);
    paired(&o->ret, v[2], v[3], n);
    paired(&o->deaths, v[4], v[5], n);
    paired(&o->raw, v[6], v[7], n);
    for (int k = 0; k < 8; ++k) free(v[k]);
    return n ? 0 : -1;
}

int cs_metric(const struct cs_run *cr, const struct cs_summary *c, const struct cs_summary *b, const struct cs_compare *cmp,
              const char *name, double *cand, double *base, double *delta, double *p, double *z, int *lower)
{
    *cand = *base = *delta = *p = *z = NAN;
    *lower = 0;
    const struct cs_cont *k = NULL;
    const struct cs_mean *mc = NULL, *mb = NULL;
    if (!strcmp(name, "depth")) k = cmp ? &cmp->depth : NULL, mc = &c->depth, mb = b ? &b->depth : NULL;
    else if (!strcmp(name, "ret") || !strcmp(name, "return")) k = cmp ? &cmp->ret : NULL, mc = &c->ret, mb = b ? &b->ret : NULL;
    else if (!strcmp(name, "deaths")) k = cmp ? &cmp->deaths : NULL, mc = &c->deaths, mb = b ? &b->deaths : NULL, *lower = 1;
    else if (!strcmp(name, "raw") || !strcmp(name, "raw_share")) k = cmp ? &cmp->raw : NULL, mc = &c->raw, mb = b ? &b->raw : NULL;
    if (mc) {
        *cand = mc->mean;
        if (mb) *base = mb->mean;
        if (k) {
            *delta = k->delta;
            *p = k->p;
            *z = *lower ? -k->z : k->z;
        }
        return 0;
    }
    const char *ms = !strncmp(name, "ms.", 3) ? name + 3 : name;
    int ci = cs_ms_index(cr, ms);
    if (ci < 0 && !cmp) return -1;
    if (ci >= 0) *cand = c->rate[ci];
    if (cmp) {
        for (int m = 0; m < cmp->nms; ++m)
            if (!strcmp(cmp->ms[m], ms)) {
                if (cmp->bi[m] >= 0 && b) *base = b->rate[cmp->bi[m]];
                else *base = 0;
                if (isnan(*cand)) *cand = 0;
                *delta = *cand - *base;
                *p = cmp->bin[m].p;
                *z = cmp->bin[m].z;
                return 0;
            }
        return -1;
    }
    return 0;
}

/* ---- output */

static int is_depth(const char *name)
{
    for (int m = 0; m < NDEPTH; ++m)
        if (!strcmp(DEPTH_MS[m], name)) return 1;
    return 0;
}

static void j_mean(struct sb *o, const char *name, const struct cs_mean *m)
{
    cli_json_str(o, name);
    sb_puts(o, ": {\"mean\": ");
    cli_json_num(o, m->mean);
    sb_puts(o, ", \"se\": ");
    cli_json_num(o, m->se);
    sb_puts(o, ", \"sd\": ");
    cli_json_num(o, m->sd);
    sb_printf(o, ", \"n\": %d}", m->n);
}

static void j_run(struct sb *o, const struct cs_run *r, const struct cs_summary *s)
{
    sb_puts(o, "{\"dir\": ");
    cli_json_str(o, r->dir);
    sb_puts(o, ", \"model\": ");
    if (r->model) cli_json_str(o, r->model);
    else sb_puts(o, "null");
    sb_printf(o, ", \"n\": %d, ", s->n);
    j_mean(o, "depth", &s->depth);
    sb_puts(o, ", ");
    j_mean(o, "ret", &s->ret);
    sb_puts(o, ", ");
    j_mean(o, "deaths", &s->deaths);
    sb_puts(o, ", ");
    j_mean(o, "raw_share", &s->raw);
    sb_puts(o, ", \"score_depth\": ");
    cli_json_num(o, r->score_depth);
    sb_puts(o, ", \"env_steps_per_s\": ");
    cli_json_num(o, r->sps);
    sb_puts(o, ", \"milestones\": {");
    for (int m = 0; m < r->nms; ++m) {
        sb_puts(o, m ? ", " : "");
        cli_json_str(o, r->ms[m]);
        sb_printf(o, ": {\"k\": %d, \"n\": %d, \"rate\": ", s->k[m], s->n);
        cli_json_num(o, s->rate[m]);
        sb_puts(o, ", \"wilson95\": [");
        cli_json_num(o, s->lo[m]);
        sb_puts(o, ", ");
        cli_json_num(o, s->hi[m]);
        sb_printf(o, "], \"in_depth\": %s}", is_depth(r->ms[m]) ? "true" : "false");
    }
    sb_puts(o, "}}");
}

static void j_cont(struct sb *o, const char *name, const struct cs_cont *k)
{
    cli_json_str(o, name);
    sb_printf(o, ": {\"n\": %d, \"cand\": ", k->n);
    cli_json_num(o, k->cand);
    sb_puts(o, ", \"base\": ");
    cli_json_num(o, k->base);
    sb_puts(o, ", \"delta\": ");
    cli_json_num(o, k->delta);
    sb_puts(o, ", \"se\": ");
    cli_json_num(o, k->se);
    sb_puts(o, ", \"z\": ");
    cli_json_num(o, k->z);
    sb_puts(o, ", \"p\": ");
    cli_json_num(o, k->p);
    sb_puts(o, "}");
}

void cs_json(struct sb *o, const struct cs_run *cr, const struct cs_summary *c, const struct cs_run *br,
             const struct cs_summary *b, const struct cs_compare *cmp)
{
    sb_puts(o, "{\"candidate\": ");
    j_run(o, cr, c);
    sb_puts(o, ", \"baseline\": ");
    if (br) j_run(o, br, b);
    else sb_puts(o, "null");
    sb_puts(o, ", \"paired\": ");
    if (!cmp) {
        sb_puts(o, "null}\n");
        return;
    }
    sb_printf(o, "{\"by\": \"env\", \"n\": %d, \"cand_unpaired\": %d, \"base_unpaired\": %d, \"start_mismatch\": %d, ",
              cmp->n, cmp->cand_unpaired, cmp->base_unpaired, cmp->start_mismatch);
    j_cont(o, "depth", &cmp->depth);
    sb_puts(o, ", ");
    j_cont(o, "ret", &cmp->ret);
    sb_puts(o, ", ");
    j_cont(o, "deaths", &cmp->deaths);
    sb_puts(o, ", ");
    j_cont(o, "raw_share", &cmp->raw);
    sb_puts(o, ", \"milestones\": {");
    for (int m = 0; m < cmp->nms; ++m) {
        const struct cs_bin *e = &cmp->bin[m];
        sb_puts(o, m ? ", " : "");
        cli_json_str(o, cmp->ms[m]);
        sb_printf(o, ": {\"both\": %d, \"cand_only\": %d, \"base_only\": %d, \"neither\": %d, \"p_mcnemar\": ", e->both, e->b,
                  e->c, e->neither);
        cli_json_num(o, e->p);
        sb_puts(o, ", \"z\": ");
        cli_json_num(o, e->z);
        sb_puts(o, "}");
    }
    sb_puts(o, "}}}\n");
}

static void pm(char *buf, size_t n, const struct cs_mean *m, const char *f)
{
    if (isnan(m->mean)) snprintf(buf, n, "-");
    else {
        char a[32], b[32];
        snprintf(a, sizeof a, f, m->mean);
        snprintf(b, sizeof b, f, m->se);
        cli_fmt(buf, n, "%s +/- %s", a, isnan(m->se) ? "-" : b);
    }
}

static void row_cont(FILE *f, const char *name, const struct cs_mean *c, const struct cs_mean *b, const struct cs_cont *k,
                     const char *fm)
{
    char x[64], y[64] = "", d[32] = "", z[32] = "", p[32] = "";
    pm(x, sizeof x, c, fm);
    if (b) pm(y, sizeof y, b, fm);
    if (k && !isnan(k->delta)) {
        snprintf(d, sizeof d, "%+.4f", k->delta);
        snprintf(z, sizeof z, "%+.2f", k->z);
        snprintf(p, sizeof p, "%.3g", k->p);
    }
    fprintf(f, "  %-17s %-22s %-22s %10s %8s %10s\n", name, x, y, d, z, p);
}

void cs_print(FILE *f, const struct cs_run *cr, const struct cs_summary *c, const struct cs_run *br,
              const struct cs_summary *b, const struct cs_compare *cmp)
{
    fprintf(f, "candidate: %s (n %d%s%s)\n", cr->dir, c->n, cr->model ? ", model " : "", cr->model ? cr->model : "");
    if (br) fprintf(f, "baseline:  %s (n %d%s%s)\n", br->dir, b->n, br->model ? ", model " : "", br->model ? br->model : "");
    if (cmp)
        fprintf(f, "paired on env: %d pairs (%d candidate and %d baseline episodes without a partner), %d start mismatches\n",
                cmp->n, cmp->cand_unpaired, cmp->base_unpaired, cmp->start_mismatch);
    if (!isnan(cr->score_depth) && fabs(cr->score_depth - c->depth.mean) > 5e-5)
        fprintf(f, "WARNING: depth %.4f here, score.json says %.4f\n", c->depth.mean, cr->score_depth);
    fprintf(f, "\n  %-17s %-22s %-22s %10s %8s %10s\n", "metric", "candidate", br ? "baseline" : "", br ? "delta" : "",
            br ? "z" : "", br ? "p" : "");
    row_cont(f, "depth", &c->depth, br ? &b->depth : NULL, cmp ? &cmp->depth : NULL, "%.4f");
    row_cont(f, "return", &c->ret, br ? &b->ret : NULL, cmp ? &cmp->ret : NULL, "%.3f");
    row_cont(f, "deaths/episode", &c->deaths, br ? &b->deaths : NULL, cmp ? &cmp->deaths : NULL, "%.4f");
    if (cr->has_raw) row_cont(f, "raw share", &c->raw, br ? &b->raw : NULL, cmp ? &cmp->raw : NULL, "%.4f");
    fprintf(f, "\n  %-17s %-27s %-27s %10s %10s\n", "milestone", "candidate k/n (rate) [95%]", br ? "baseline k/n (rate) [95%]" : "",
            br ? "+cand/-base" : "", br ? "p McNemar" : "");
    struct sb zero = {0};
    int nz = 0;
    int nms = cmp ? cmp->nms : cr->nms;
    for (int m = 0; m < nms; ++m) {
        const char *name = cmp ? cmp->ms[m] : cr->ms[m];
        int ci = cmp ? cmp->ci[m] : m, bi = cmp ? cmp->bi[m] : -1;
        int kc = ci >= 0 ? c->k[ci] : 0, kb = bi >= 0 && b ? b->k[bi] : 0;
        if (!kc && !kb) {
            sb_printf(&zero, "%s%s", nz++ ? ", " : "", name);
            continue;
        }
        char x[64], y[64] = "", d[32] = "", p[32] = "";
        double lo, hi;
        cs_wilson(kc, c->n, &lo, &hi);
        snprintf(x, sizeof x, "%d/%d (%.2f%%) [%.1f, %.1f]", kc, c->n, 100.0 * kc / c->n, 100 * lo, 100 * hi);
        if (br) {
            cs_wilson(kb, b->n, &lo, &hi);
            snprintf(y, sizeof y, "%d/%d (%.2f%%) [%.1f, %.1f]", kb, b->n, 100.0 * kb / b->n, 100 * lo, 100 * hi);
        }
        if (cmp) {
            snprintf(d, sizeof d, "+%d/-%d", cmp->bin[m].b, cmp->bin[m].c);
            snprintf(p, sizeof p, "%.3g", cmp->bin[m].p);
        }
        fprintf(f, "  %-17s %-27s %-27s %10s %10s\n", name, x, y, d, p);
    }
    if (nz) fprintf(f, "  (0 in %s: %s)\n", br ? "both" : "every episode", sb_str(&zero));
    sb_free(&zero);
}
