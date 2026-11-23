/* nether harvest: a bank of starts built seed by seed from a declarative file, each seed's state verified, the
 * passed seeds split into disjoint train and eval sets (cli_verbs.h).
 *
 *   nether harvest BANK.yaml --out DIR [--seeds SPEC] [--target-seeds N] [--threads T] [--lock FILE] [--dry-run]
 *   nether harvest --env NAME --out DIR ...   (the bank composed from configs/envs/NAME.yaml, cli_compose.c)
 *
 * BANK.yaml:
 *   bank.seeds         A-B,C,... or A..B                the seeds, and/or (--seeds SPEC replaces every seed source)
 *   bank.seeds_file    a file of seeds (a line each, # comments; or JSON with "seeds")
 *   bank.seeds_tsv     a TSV with a header, and bank.seeds_column (default seed): its distinct values
 *   bank.exclude       A-B,C,...  and  bank.exclude_files: files of seeds never harvested (held-out sets)
 *   bank.split_train   the train fraction, 0 to 1 (required); bank.split_key the hash's key (default the file's name)
 *   vars.NAME          {NAME} in the commands
 *   harvest.cmd        a seed's command: {seed} {work} (DIR/work/s{seed}) {out} {root} and vars substituted, split
 *                      into words, run by posix_spawn in the checkout (no shell)
 *   harvest.prepare    a command, or NAME: COMMAND each, run once before any seed (DIR/prepare-NAME.log)
 *   harvest.if_missing, harvest.first   first runs for a seed whose if_missing path is absent (a recording on demand)
 *   harvest.source     LIB,LIB...: libraries of starts (index.tsv); the seed's rows at harvest.index_stage are copied
 *                      into harvest.index (a seed with none is rejected), and with no bank seeds their seeds are the set
 *   harvest.steps      NAME: COMMAND or NAME: {cmd, stdout} each, after harvest.cmd, in order
 *   harvest.reject     a regex (POSIX extended, per line): a step whose output matches rejects the seed (not a failure;
 *                      a rejected seed is done and never run again)
 *   harvest.produces   a path the command must leave (after substitution)
 *   harvest.verify     the check of what it made (test_snapshots on the state, say); its rc 0 is a pass. With {start}
 *                      in it, it runs once a row of harvest.index, {start} the row's path
 *   harvest.index      the seed's index.tsv of starts (library columns: id, seed, stage, ..., path); merged at the end
 *   harvest.env        NAME=VALUE words; harvest.timeout seconds a command (default 1800)
 *
 * Each finished seed is one line of DIR/progress.tsv (seed, status pass/fail/reject, seconds, detail), appended under
 * flock, so a rerun skips every seed already passed, rejected or failed (--retry-failed runs the failed again) and
 * SIGTERM or SIGINT stops the running seeds without a line: they run again next time. DIR/splits.json: every passed
 * seed by SHA-256(split_key:seed) into train_seeds or eval_seeds, overlap 0 checked; with harvest.index, every passed
 * seed's rows merged into DIR/train/index.tsv and DIR/eval/index.tsv (paths absolute), each row's seed checked against
 * its split again (index_errors), so a library ntloop --library reads never holds an eval seed in train. */
#define _GNU_SOURCE
#include "cli_json.h"
#include "cli_proc.h"
#include "cli_verbs.h"
#include "cli_yaml.h"

#include <errno.h>
#include <regex.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

struct seeds {
    long *v;
    int n, cap;
};

static void seeds_add(struct seeds *s, long x)
{
    for (int i = 0; i < s->n; ++i)
        if (s->v[i] == x) return;
    if (s->n == s->cap) s->cap = s->cap ? s->cap * 2 : 64, s->v = xrealloc(s->v, sizeof *s->v * (size_t)s->cap);
    s->v[s->n++] = x;
}

static int cmp_long(const void *a, const void *b)
{
    long x = *(const long *)a, y = *(const long *)b;
    return (x > y) - (x < y);
}

static int seeds_has(const struct seeds *s, long x)
{
    for (int i = 0; i < s->n; ++i)
        if (s->v[i] == x) return 1;
    return 0;
}

/* "A-B,C" (or A..B) into s; 0 or -1 */
static int seeds_spec(struct seeds *s, const char *spec)
{
    for (const char *p = spec; *p;) {
        char *e;
        long a = strtol(p, &e, 10), b = a;
        if (e == p) return -1;
        if (*e == '-' || (e[0] == '.' && e[1] == '.')) {
            const char *q = e + (*e == '-' ? 1 : 2);
            b = strtol(q, &e, 10);
            if (e == q || b < a || b - a > 10000000) return -1;
        }
        for (long x = a; x <= b; ++x) seeds_add(s, x);
        p = e;
        while (*p == ',' || *p == ' ') ++p;
        if (*p && (*p < '0' || *p > '9')) return -1;
    }
    return 0;
}

static int seeds_file(struct seeds *s, const char *path)
{
    char *t = cli_read_file(path, NULL);
    if (!t) return -1;
    const char *p = t;
    while (*p == ' ' || *p == '\n') ++p;
    if (*p == '{') {
        char err[200];
        struct jv *v = jv_parse(t, err, sizeof err);
        const struct jv *a = v ? jv_get(v, "seeds") : NULL;
        for (int i = 0; a && a->t == JV_ARR && i < a->n; ++i) seeds_add(s, (long)a->kids[i]->num);
        int ok = a && a->t == JV_ARR;
        jv_free(v);
        free(t);
        return ok ? 0 : -1;
    }
    for (char *q = t, *e; *q; q = e) {
        e = strchr(q, '\n');
        if (e) *e++ = 0;
        else e = q + strlen(q);
        char *h = strchr(q, '#');
        if (h) *h = 0;
        while (*q == ' ' || *q == '\t') ++q;
        if (!*q) continue;
        if (seeds_spec(s, q)) {
            free(t);
            return -1;
        }
    }
    free(t);
    return 0;
}

static int seeds_tsv(struct seeds *s, const char *path, const char *col)
{
    char *t = cli_read_file(path, NULL);
    if (!t) return -1;
    int ci = -1, line = 0;
    for (char *q = t, *e; *q; q = e) {
        e = strchr(q, '\n');
        if (e) *e++ = 0;
        else e = q + strlen(q);
        char *f = q;
        for (int k = 0;; ++k) {
            char *tab = strchr(f, '\t');
            if (tab) *tab = 0;
            if (!line && !strcmp(f, col)) ci = k;
            if (line && k == ci) {
                long x;
                if (!cli_long(f, &x)) seeds_add(s, x);
            }
            if (!tab) break;
            f = tab + 1;
        }
        ++line;
        if (ci < 0) break;
    }
    free(t);
    return ci < 0 ? -1 : 0;
}

/* the hash's fraction in [0, 1) */
static double split_u(const char *key, long seed)
{
    char *m = xasprintf("%s:%ld", key, seed);
    uint8_t d[32];
    cli_sha256(m, strlen(m), d);
    free(m);
    uint64_t x = 0;
    for (int i = 0; i < 8; ++i) x = x << 8 | d[i];
    return (double)(x >> 11) / 9007199254740992.0;
}

/* ---- a library's index (train/starts.c's columns: id, seed, stage, ..., path; a header line, then a row a start) */

static int col_of(const char *header, const char *name)
{
    int k = 0;
    for (const char *p = header; *p; ++k) {
        size_t n = strcspn(p, "\t\n");
        if (n == strlen(name) && !strncmp(p, name, n)) return k;
        p += n;
        if (*p != '\t') break;
        ++p;
    }
    return -1;
}

/* column col of a row, in out */
static void col_get(const char *row, int col, char *out, size_t n)
{
    *out = 0;
    const char *p = row;
    for (int k = 0; k < col && p; ++k) p = strchr(p, '\t') ? strchr(p, '\t') + 1 : NULL;
    if (p) snprintf(out, n, "%.*s", (int)strcspn(p, "\t\n"), p);
}

struct idx {
    char *header;   /* the first file's header line, without its newline */
    struct sv rows; /* rows without their newline, each path made absolute from its index's directory */
    int seed_col, stage_col, path_col;
};

/* FILE's rows with seed (any when < 0) and stage (any when NULL or "") added to x; 0, or -1 (unreadable, no seed or path
 * column, or a header other than the first file's) */
static int idx_read(const char *file, long seed, const char *stage, struct idx *x)
{
    char *t = cli_read_file(file, NULL);
    if (!t) return -1;
    char *nl = strchr(t, '\n');
    if (!nl) {
        free(t);
        return -1;
    }
    *nl = 0;
    if (!x->header) {
        x->header = xstrdup(t);
        x->seed_col = col_of(t, "seed"), x->stage_col = col_of(t, "stage"), x->path_col = col_of(t, "path");
    } else if (strcmp(x->header, t)) {
        free(t);
        return -1;
    }
    if (x->seed_col < 0 || x->path_col < 0) {
        free(t);
        return -1;
    }
    char dir[PATH_MAX];
    snprintf(dir, sizeof dir, "%s", file);
    char *sl = strrchr(dir, '/');
    if (sl) *sl = 0;
    else snprintf(dir, sizeof dir, ".");
    for (char *q = nl + 1, *e; *q; q = e) {
        e = strchr(q, '\n');
        if (e) *e++ = 0;
        else e = q + strlen(q);
        if (!*q) continue;
        char f[PATH_MAX];
        long s;
        col_get(q, x->seed_col, f, sizeof f);
        if (cli_long(f, &s) || (seed >= 0 && s != seed)) continue;
        if (stage && *stage && x->stage_col >= 0) {
            col_get(q, x->stage_col, f, sizeof f);
            if (strcmp(f, stage)) continue;
        }
        col_get(q, x->path_col, f, sizeof f);
        if (f[0] == '/') sv_push(&x->rows, q);
        else {
            /* the row again with its path from the index's directory */
            struct sb r = {0};
            const char *p = q;
            for (int k = 0; *p || k == 0; ++k) {
                size_t n = strcspn(p, "\t");
                if (k) sb_putc(&r, '\t');
                if (k == x->path_col) sb_printf(&r, "%s/%.*s", dir, (int)n, p);
                else sb_putn(&r, p, n);
                p += n;
                if (*p != '\t') break;
                ++p;
            }
            sv_push(&x->rows, sb_str(&r));
            sb_free(&r);
        }
    }
    free(t);
    return 0;
}

static void idx_free(struct idx *x)
{
    free(x->header);
    sv_free(&x->rows);
    memset(x, 0, sizeof *x);
}

/* x written as an index file; 0 or -1 */
static int idx_write(const struct idx *x, const char *path)
{
    struct sb b = {0};
    sb_printf(&b, "%s\n", x->header ? x->header : "id\tseed\tstage\tmilestones\tdimension\titems\tprovenance\tbytes\tpath");
    for (int i = 0; i < x->rows.n; ++i) sb_printf(&b, "%s\n", x->rows.v[i]);
    int rc = cli_write_file_atomic(path, b.s, b.n);
    sb_free(&b);
    return rc;
}

/* one step of a seed: TMPL substituted, split, run in the checkout; WORK/NAME.log (stderr there and stdout to stdout_path
 * when given); its rc, the log's path in log */
static int run_step(const char *name, const char *tmpl, const char *const *k, const char *const *v, int n, const char *work,
                    const struct sv *env, double timeout, const char *stdout_path, char *log, size_t ln, char *detail, size_t dn)
{
    char *c = cli_subst(tmpl, k, v, n), err[300];
    struct sv av = {0};
    cli_fmt(log, ln, "%s/%s.log", work, name);
    int rc;
    if (cli_split_words(c, &av, err, sizeof err)) {
        snprintf(detail, dn, "%s: %.300s", name, err);
        rc = 2;
    } else {
        char sp_[PATH_MAX];
        struct cli_spawn_opts o = {.argv = (const char *const *)av.v, .cwd = cli_root, .env_set = (const char *const *)env->v,
                                   .out_path = stdout_path ? cli_abs(stdout_path, sp_) : log, .err_path = stdout_path ? log : NULL,
                                   .timeout_s = timeout, .new_pgroup = 1, .stdin_null = 1, .quiet = 1};
        struct cli_spawn_res r;
        int sp = cli_spawn(&o, &r);
        if (cli_interrupted) _exit(143); /* no line: the seed runs again on the next harvest */
        rc = sp ? 127 : r.rc;
        if (rc) snprintf(detail, dn, "%s rc %d%s (%.300s)", name, rc, r.timed_out ? " timed out" : "", log);
    }
    sv_free(&av);
    free(c);
    return rc;
}

/* the log matches harvest.reject (POSIX extended): the seed is filtered out, not failed */
static int rejected(const struct cy *y, const char *log)
{
    const char *re = cy_get(y, "harvest.reject", NULL);
    if (!re) return 0;
    regex_t r;
    if (regcomp(&r, re, REG_EXTENDED | REG_NEWLINE | REG_NOSUB)) return 0;
    char *t = cli_read_file(log, NULL);
    int hit = t && !regexec(&r, t, 0, NULL, 0);
    free(t);
    regfree(&r);
    return hit;
}

/* one seed in a forked worker, each step only while the ones before passed: harvest.first when harvest.if_missing is
 * missing; the source library's rows of the seed (harvest.source, harvest.index_stage) as its index; harvest.cmd and
 * then harvest.steps in order (a step NAME: COMMAND, or NAME: {cmd, stdout}), each checked against harvest.reject;
 * harvest.produces; harvest.verify (once, or with {start} once a row of harvest.index). Its line appended (pass,
 * fail, or reject: a seed the filter or the source has nothing for); exit 0 */
static void work_seed(const struct cy *y, long seed, const char *out, const char *progress, const struct sv *env,
                      const char *const *vk, const char *const *vv, int nv)
{
    char sd[32], work[PATH_MAX];
    snprintf(sd, sizeof sd, "%ld", seed);
    cli_fmt(work, sizeof work, "%s/work/s%ld", out, seed);
    cli_mkdirs(work);
    enum { NK = 5 };
    const char **k = xcalloc((size_t)nv + NK, sizeof *k), **v = xcalloc((size_t)nv + NK, sizeof *v);
    k[0] = "seed", v[0] = sd, k[1] = "work", v[1] = work, k[2] = "out", v[2] = out, k[3] = "root", v[3] = cli_root;
    k[4] = "start", v[4] = "{start}";
    for (int i = 0; i < nv; ++i) k[NK + i] = vk[i], v[NK + i] = vv[i];
    int n = nv + NK;
    double t0 = cli_now(), timeout = cy_num(y, "harvest.timeout", 1800);
    const char *status = "pass", *stage = cy_get(y, "harvest.index_stage", NULL);
    char detail[600] = "", log[PATH_MAX + 64], idxp[PATH_MAX] = "";
    if (cy_get(y, "harvest.index", NULL)) {
        char *p = cli_subst(cy_get(y, "harvest.index", ""), k, v, n);
        cli_resolve(p, idxp);
        free(p);
    }
    /* 1. what the seed needs made first (its spawn recorded by the oracle, say) */
    const char *im = cy_get(y, "harvest.if_missing", NULL), *first = cy_get(y, "harvest.first", NULL);
    if (im && first) {
        char *p = cli_subst(im, k, v, n), pb[PATH_MAX];
        cli_resolve(p, pb);
        free(p);
        if (!cli_exists(pb)) {
            char *sl = strrchr(pb, '/');
            if (sl) *sl = 0, cli_mkdirs(pb); /* the oracle writes a tape only into a directory that exists */
            if (run_step("first", first, k, v, n, work, env, timeout, NULL, log, sizeof log, detail, sizeof detail))
                status = "fail";
        }
    }
    /* 2. the source library's rows of this seed */
    if (!strcmp(status, "pass") && cy_get(y, "harvest.source", NULL)) {
        struct idx x = {0};
        for (const char *p = cy_get(y, "harvest.source", ""); *p;) {
            size_t ln = strcspn(p, ",");
            char *lib = xstrndup(p, ln), f[PATH_MAX], fb[PATH_MAX + 16];
            cli_resolve(lib, f);
            cli_fmt(fb, sizeof fb, "%s/index.tsv", f);
            if (idx_read(fb, seed, stage, &x)) status = "fail", snprintf(detail, sizeof detail, "cannot read %.500s", fb);
            free(lib);
            p += ln;
            if (*p == ',') ++p;
        }
        if (!strcmp(status, "pass")) {
            if (!idxp[0]) status = "fail", snprintf(detail, sizeof detail, "harvest.source needs harvest.index");
            else if (!x.rows.n) status = "reject", snprintf(detail, sizeof detail, "no start of stage %s in the source", stage ? stage : "any");
            else {
                char d[PATH_MAX];
                snprintf(d, sizeof d, "%s", idxp);
                char *sl = strrchr(d, '/');
                if (sl) *sl = 0, cli_mkdirs(d);
                if (idx_write(&x, idxp)) status = "fail", snprintf(detail, sizeof detail, "cannot write %.500s", idxp);
            }
        }
        idx_free(&x);
    }
    /* 3. the commands */
    const struct cy *steps = cy_node(y, "harvest.steps");
    int nsteps = steps ? steps->nk : 0;
    for (int s = -1; s < nsteps && !strcmp(status, "pass"); ++s) {
        const char *name = "harvest", *tmpl, *so = NULL;
        char *sop = NULL;
        if (s < 0) tmpl = cy_get(y, "harvest.cmd", NULL);
        else {
            const struct cy *st = steps->kid[s];
            name = st->key ? st->key : "step";
            tmpl = st->val ? st->val : cy_get(st, "cmd", NULL);
            if (!st->val && cy_get(st, "stdout", NULL)) sop = cli_subst(cy_get(st, "stdout", ""), k, v, n), so = sop;
        }
        if (tmpl) {
            if (run_step(name, tmpl, k, v, n, work, env, timeout, so, log, sizeof log, detail, sizeof detail)) status = "fail";
            else if (rejected(y, log)) status = "reject", snprintf(detail, sizeof detail, "%s: harvest.reject matched (%.400s)", name, log);
        }
        free(sop);
    }
    if (!strcmp(status, "pass") && cy_get(y, "harvest.produces", NULL)) {
        char *p = cli_subst(cy_get(y, "harvest.produces", ""), k, v, n);
        char pb[PATH_MAX];
        if (!cli_exists(cli_resolve(p, pb))) status = "fail", snprintf(detail, sizeof detail, "the commands made no %.400s", p);
        free(p);
    }
    /* 4. the check: once, or once a start of the seed's index ({start} its path) */
    int verified = 0;
    const char *vt = cy_get(y, "harvest.verify", NULL);
    if (!strcmp(status, "pass") && vt) {
        if (strstr(vt, "{start}")) {
            struct idx x = {0};
            if (!idxp[0] || idx_read(idxp, seed, stage, &x)) status = "fail", snprintf(detail, sizeof detail, "{start}: no index %.400s", idxp);
            else if (!x.rows.n) status = "reject", snprintf(detail, sizeof detail, "no start of stage %s in %.400s", stage ? stage : "any", idxp);
            for (int i = 0; i < x.rows.n && !strcmp(status, "pass"); ++i) {
                char path[PATH_MAX], nm[32];
                col_get(x.rows.v[i], x.path_col, path, sizeof path);
                v[4] = path;
                snprintf(nm, sizeof nm, "verify-%d", i);
                if (run_step(nm, vt, k, v, n, work, env, timeout, NULL, log, sizeof log, detail, sizeof detail)) status = "fail";
                else ++verified;
            }
            v[4] = "{start}";
            idx_free(&x);
        } else if (run_step("verify", vt, k, v, n, work, env, timeout, NULL, log, sizeof log, detail, sizeof detail))
            status = "fail";
        else
            verified = 1;
    }
    if (!detail[0]) snprintf(detail, sizeof detail, "%.540s (%d verified)", work, verified);
    char row[1024];
    snprintf(row, sizeof row, "%ld\t%s\t%.1f\t%s\n", seed, status, cli_now() - t0, detail);
    cli_append_locked(progress, "seed\tstatus\tseconds\tdetail\n", row);
    printf("nether harvest: seed %ld %s in %.1f s%s%s\n", seed, status, cli_now() - t0, strcmp(status, "pass") ? ": " : "",
           strcmp(status, "pass") ? detail : "");
    fflush(stdout);
    _exit(0);
}

static volatile sig_atomic_t stop_sig;
static void on_stop(int sig) { stop_sig = sig; }

int v_harvest(int argc, char **argv)
{
    const char *bank = NULL, *out = NULL, *lock = NULL, *seeds_cli = NULL;
    long target = -1, threads = 1;
    int dry = 0, retry = 0;
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (!strcmp(a, "--out")) out = cli_arg(argc, argv, &i, "harvest");
        else if (!strcmp(a, "--seeds")) seeds_cli = cli_arg(argc, argv, &i, "harvest");
        else if (!strcmp(a, "--target-seeds")) target = atol(cli_arg(argc, argv, &i, "harvest"));
        else if (!strcmp(a, "--threads")) threads = atol(cli_arg(argc, argv, &i, "harvest"));
        else if (!strcmp(a, "--lock")) lock = cli_arg(argc, argv, &i, "harvest");
        else if (!strcmp(a, "--dry-run")) dry = 1;
        else if (!strcmp(a, "--retry-failed")) retry = 1;
        else if (a[0] == '-') {
            fprintf(stderr, "nether harvest: unknown flag %s (nether harvest --help)\n", a);
            return 2;
        } else if (!bank) bank = a;
        else {
            fprintf(stderr, "nether harvest: one bank file\n");
            return 2;
        }
    }
    if (!bank || !out || threads < 1) {
        fprintf(stderr, "nether harvest BANK.yaml | --env NAME --out DIR [--seeds A-B,C] [--target-seeds N] [--threads T] "
                        "[--lock FILE] [--dry-run]\n");
        return 2;
    }
    char bp[PATH_MAX], od[PATH_MAX], err[512];
    cli_resolve(bank, bp);
    cli_abs(out, od);
    struct cy *y = cy_load(bp, err, sizeof err);
    if (!y) {
        fprintf(stderr, "nether harvest: %s\n", err);
        return 2;
    }
    double frac0 = cy_num(y, "bank.split_train", -1);
    if ((!cy_get(y, "harvest.cmd", NULL) && !cy_node(y, "harvest.steps") && !cy_get(y, "harvest.source", NULL)) || frac0 < 0 ||
        frac0 > 1) {
        fprintf(stderr, "nether harvest: %s needs harvest.cmd (or harvest.steps or harvest.source) and bank.split_train "
                        "(the train share, 0 to 1)\n", bp);
        cy_free(y);
        return 2;
    }
    /* the seeds: --seeds, else the bank's lists, else every seed the source library has a start of the stage for */
    struct seeds all = {0}, ex = {0}, done = {0}, passed = {0};
    char pb[PATH_MAX];
    int bad = 0;
    if (seeds_cli) bad = seeds_spec(&all, seeds_cli) != 0;
    else {
        if (cy_get(y, "bank.seeds", NULL) && seeds_spec(&all, cy_get(y, "bank.seeds", ""))) bad = 1;
        if (cy_get(y, "bank.seeds_file", NULL) && seeds_file(&all, cli_resolve(cy_get(y, "bank.seeds_file", ""), pb))) bad = 1;
        if (cy_get(y, "bank.seeds_tsv", NULL) &&
            seeds_tsv(&all, cli_resolve(cy_get(y, "bank.seeds_tsv", ""), pb), cy_get(y, "bank.seeds_column", "seed")))
            bad = 1;
        if (!all.n && cy_get(y, "harvest.source", NULL)) {
            struct idx x = {0};
            for (const char *p = cy_get(y, "harvest.source", ""); *p;) {
                size_t ln = strcspn(p, ",");
                char *lib = xstrndup(p, ln), f[PATH_MAX], fb[PATH_MAX + 16];
                cli_resolve(lib, f);
                cli_fmt(fb, sizeof fb, "%s/index.tsv", f);
                if (idx_read(fb, -1, cy_get(y, "harvest.index_stage", NULL), &x)) {
                    fprintf(stderr, "nether harvest: harvest.source: cannot read %s (an index with seed and path columns)\n", fb);
                    bad = 1;
                }
                free(lib);
                p += ln;
                if (*p == ',') ++p;
            }
            for (int i = 0; i < x.rows.n; ++i) {
                char f[64];
                long s;
                col_get(x.rows.v[i], x.seed_col, f, sizeof f);
                if (!cli_long(f, &s)) seeds_add(&all, s);
            }
            idx_free(&x);
        }
    }
    if (cy_get(y, "bank.exclude", NULL) && seeds_spec(&ex, cy_get(y, "bank.exclude", ""))) bad = 1;
    for (const char *p = cy_get(y, "bank.exclude_files", ""); *p;) {
        size_t n = strcspn(p, ",");
        char *f = xstrndup(p, n);
        if (seeds_file(&ex, cli_resolve(f, pb))) {
            fprintf(stderr, "nether harvest: cannot read the exclusion %s\n", f);
            bad = 1;
        }
        free(f);
        p += n;
        if (*p == ',') ++p;
    }
    if (bad || !all.n) {
        fprintf(stderr, "nether harvest: %s: no seeds, or a seed list that does not parse (bank.seeds A-B,C,...)\n", bp);
        cy_free(y);
        return 2;
    }
    if (cli_mkdirs(od)) {
        fprintf(stderr, "nether harvest: cannot make %s\n", od);
        cy_free(y);
        return 2;
    }
    char progress[PATH_MAX + 32];
    cli_fmt(progress, sizeof progress, "%s/progress.tsv", od);
    /* what earlier harvests finished */
    char *pt = cli_read_file(progress, NULL);
    for (char *q = pt, *e; q && *q; q = e) {
        e = strchr(q, '\n');
        if (e) *e++ = 0;
        else e = q + strlen(q);
        long s;
        char st[16];
        if (sscanf(q, "%ld\t%15s", &s, st) != 2) continue;
        if (!strcmp(st, "pass")) seeds_add(&passed, s), seeds_add(&done, s);
        else if (!retry || !strcmp(st, "reject")) seeds_add(&done, s); /* a reject is the filter's answer: never rerun */
    }
    free(pt);
    struct seeds todo = {0};
    int nex = 0;
    for (int i = 0; i < all.n; ++i) {
        if (seeds_has(&ex, all.v[i])) {
            ++nex;
            continue;
        }
        if (!seeds_has(&done, all.v[i])) seeds_add(&todo, all.v[i]);
    }
    printf("nether harvest: %s: %d seeds, %d excluded, %d passed before, %d to run, %ld at a time%s\n", bp, all.n, nex,
           passed.n, todo.n, threads, target > 0 ? "" : "");
    if (target > 0) printf("nether harvest: stops once %ld seeds have passed\n", target);
    /* vars and env */
    const struct cy *vars = cy_node(y, "vars");
    int nv = vars ? vars->nk : 0;
    const char **vk = xcalloc((size_t)nv + 1, sizeof *vk), **vv = xcalloc((size_t)nv + 1, sizeof *vv);
    for (int i = 0; i < nv; ++i) vk[i] = vars->kid[i]->key, vv[i] = vars->kid[i]->val ? vars->kid[i]->val : "";
    struct sv env = {0};
    if (cy_get(y, "harvest.env", NULL) && cli_split_words(cy_get(y, "harvest.env", ""), &env, err, sizeof err)) {
        fprintf(stderr, "nether harvest: harvest.env: %s\n", err);
        cy_free(y);
        return 2;
    }
    if (!env.v) sv_push(&env, "NETHER_HARVEST=1");
    const struct cy *steps = cy_node(y, "harvest.steps"), *prep = cy_node(y, "harvest.prepare");
    if (dry) {
        if (prep) printf("  prepare: %s\n", prep->val ? prep->val : "(a command a key)");
        for (int i = 0; i < todo.n && i < 5; ++i) {
            char sd[32], work[PATH_MAX];
            snprintf(sd, sizeof sd, "%ld", todo.v[i]);
            cli_fmt(work, sizeof work, "%s/work/s%ld", od, todo.v[i]);
            const char **k = xcalloc((size_t)nv + 4, sizeof *k), **v = xcalloc((size_t)nv + 4, sizeof *v);
            k[0] = "seed", v[0] = sd, k[1] = "work", v[1] = work, k[2] = "out", v[2] = od, k[3] = "root", v[3] = cli_root;
            for (int j = 0; j < nv; ++j) k[4 + j] = vk[j], v[4 + j] = vv[j];
            static const char *const ONE[] = {"harvest.first", "harvest.source", "harvest.cmd"};
            printf("  seed %s:\n", sd);
            for (size_t s = 0; s < sizeof ONE / sizeof ONE[0]; ++s)
                if (cy_get(y, ONE[s], NULL)) {
                    char *c = cli_subst(cy_get(y, ONE[s], ""), k, v, nv + 4);
                    printf("    %-8s %s\n", ONE[s] + 8, c);
                    free(c);
                }
            for (int s = 0; steps && s < steps->nk; ++s) {
                const char *t = steps->kid[s]->val ? steps->kid[s]->val : cy_get(steps->kid[s], "cmd", "");
                char *c = cli_subst(t, k, v, nv + 4);
                printf("    %-8s %s\n", steps->kid[s]->key ? steps->kid[s]->key : "step", c);
                free(c);
            }
            if (cy_get(y, "harvest.verify", NULL)) {
                char *c = cli_subst(cy_get(y, "harvest.verify", ""), k, v, nv + 4);
                printf("    verify   %s\n", c);
                free(c);
            }
            free(k), free(v);
        }
        cy_free(y);
        return 0;
    }
    /* once, before any seed: what the commands need built (a scalar, or a command a key) */
    for (int s = 0; prep && s < (prep->val ? 1 : prep->nk); ++s) {
        const char *name = prep->val ? "prepare" : prep->kid[s]->key, *t = prep->val ? prep->val : prep->kid[s]->val;
        if (!t || !name) continue;
        const char *k[] = {"out", "root"}, *v[] = {od, cli_root};
        char *c = cli_subst(t, k, v, 2), plog[PATH_MAX + 64];
        cli_fmt(plog, sizeof plog, "%s/prepare-%s.log", od, name);
        struct sv av = {0};
        int prc = cli_split_words(c, &av, err, sizeof err) ? 2 : -1;
        if (prc < 0) {
            struct cli_spawn_opts o = {.argv = (const char *const *)av.v, .cwd = cli_root, .out_path = plog, .stdin_null = 1};
            struct cli_spawn_res r;
            prc = cli_spawn(&o, &r) ? 127 : r.rc;
        }
        sv_free(&av);
        free(c);
        if (prc) {
            fprintf(stderr, "nether harvest: harvest.prepare %s failed (rc %d): %s\n", name, prc, plog);
            cy_free(y);
            return prc;
        }
    }
    int lfd = -1;
    if (lock && (lfd = cli_lock_acquire(lock, 0)) < 0) {
        cy_free(y);
        return 1;
    }
    struct sigaction sa, old[2];
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_stop;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, &old[0]);
    sigaction(SIGINT, &sa, &old[1]);
    pid_t *kids = xcalloc((size_t)threads, sizeof *kids);
    int running = 0, next = 0;
    long npass = passed.n;
    double t0 = cli_now();
    while ((next < todo.n && !stop_sig && (target <= 0 || npass < target)) || running) {
        while (running < threads && next < todo.n && !stop_sig && (target <= 0 || npass + running < target)) {
            long seed = todo.v[next++];
            fflush(stdout);
            pid_t pid = fork();
            if (pid < 0) {
                fprintf(stderr, "nether harvest: fork: %s\n", strerror(errno));
                stop_sig = SIGTERM;
                break;
            }
            if (!pid) {
                sigaction(SIGTERM, &old[0], NULL);
                sigaction(SIGINT, &old[1], NULL);
                work_seed(y, seed, od, progress, &env, vk, vv, nv);
            }
            for (int k = 0; k < threads; ++k)
                if (!kids[k]) {
                    kids[k] = pid;
                    break;
                }
            ++running;
        }
        int st;
        pid_t w = waitpid(-1, &st, 0);
        if (w < 0) {
            if (errno == EINTR) {
                if (stop_sig) {
                    printf("nether harvest: signal %d: stopping the %d running seeds (they run again next time)\n",
                           (int)stop_sig, running);
                    for (int k = 0; k < threads; ++k)
                        if (kids[k]) kill(kids[k], SIGTERM);
                }
                continue;
            }
            break;
        }
        for (int k = 0; k < threads; ++k)
            if (kids[k] == w) kids[k] = 0;
        --running;
        /* the count of passes, from the file the workers wrote */
        struct seeds now = {0};
        char *t = cli_read_file(progress, NULL);
        for (char *q = t, *e; q && *q; q = e) {
            e = strchr(q, '\n');
            if (e) *e++ = 0;
            else e = q + strlen(q);
            long s;
            char stt[16];
            if (sscanf(q, "%ld\t%15s", &s, stt) == 2 && !strcmp(stt, "pass")) seeds_add(&now, s);
        }
        free(t);
        npass = now.n;
        free(now.v);
    }
    sigaction(SIGTERM, &old[0], NULL);
    sigaction(SIGINT, &old[1], NULL);
    cli_lock_release(lfd);
    /* the splits, in seed order (the progress file is in the order seeds finished, which threads make vary) */
    struct seeds pass2 = {0}, fail2 = {0}, rej2 = {0};
    char *t = cli_read_file(progress, NULL);
    for (char *q = t, *e; q && *q; q = e) {
        e = strchr(q, '\n');
        if (e) *e++ = 0;
        else e = q + strlen(q);
        long s;
        char stt[16];
        if (sscanf(q, "%ld\t%15s", &s, stt) != 2) continue;
        if (!strcmp(stt, "pass")) seeds_add(&pass2, s);
        else if (!strcmp(stt, "reject")) seeds_add(&rej2, s);
        else seeds_add(&fail2, s);
    }
    free(t);
    qsort(pass2.v, (size_t)pass2.n, sizeof *pass2.v, cmp_long);
    qsort(fail2.v, (size_t)fail2.n, sizeof *fail2.v, cmp_long);
    qsort(rej2.v, (size_t)rej2.n, sizeof *rej2.v, cmp_long);
    const char *key = cy_get(y, "bank.split_key", strrchr(bp, '/') ? strrchr(bp, '/') + 1 : bp);
    double frac = frac0;
    struct sb tr = {0}, ev = {0}, fl = {0}, rj = {0};
    int ntr = 0, nev = 0, overlap = 0;
    struct seeds trs = {0}, evs = {0};
    for (int i = 0; i < pass2.n; ++i) {
        long s = pass2.v[i];
        if (seeds_has(&ex, s)) continue;
        if (split_u(key, s) < frac) sb_printf(&tr, "%s%ld", ntr++ ? ", " : "", s), seeds_add(&trs, s);
        else sb_printf(&ev, "%s%ld", nev++ ? ", " : "", s), seeds_add(&evs, s);
    }
    for (int i = 0; i < evs.n; ++i) overlap += seeds_has(&trs, evs.v[i]);
    int nfl = 0, nrj = 0;
    for (int i = 0; i < fail2.n; ++i)
        if (!seeds_has(&pass2, fail2.v[i])) sb_printf(&fl, "%s%ld", nfl++ ? ", " : "", fail2.v[i]);
    for (int i = 0; i < rej2.n; ++i)
        if (!seeds_has(&pass2, rej2.v[i])) sb_printf(&rj, "%s%ld", nrj++ ? ", " : "", rej2.v[i]);
    /* the libraries: each passed seed's index rows (harvest.index, harvest.index_stage) into DIR/train/index.tsv and
     * DIR/eval/index.tsv, every row's seed checked again against the hash and against the other split */
    int irows[2] = {0, 0}, ibad = 0, iwrote = 0;
    if (cy_get(y, "harvest.index", NULL) && !stop_sig) {
        struct idx part[2];
        memset(part, 0, sizeof part);
        const char *stage = cy_get(y, "harvest.index_stage", NULL);
        for (int i = 0; i < pass2.n; ++i) {
            long s = pass2.v[i];
            if (seeds_has(&ex, s)) continue;
            int which = seeds_has(&evs, s);
            char sd[32], work[PATH_MAX];
            snprintf(sd, sizeof sd, "%ld", s);
            cli_fmt(work, sizeof work, "%s/work/s%ld", od, s);
            const char **k = xcalloc((size_t)nv + 4, sizeof *k), **v = xcalloc((size_t)nv + 4, sizeof *v);
            k[0] = "seed", v[0] = sd, k[1] = "work", v[1] = work, k[2] = "out", v[2] = od, k[3] = "root", v[3] = cli_root;
            for (int j = 0; j < nv; ++j) k[4 + j] = vk[j], v[4 + j] = vv[j];
            char *p = cli_subst(cy_get(y, "harvest.index", ""), k, v, nv + 4), ip[PATH_MAX];
            if (idx_read(cli_resolve(p, ip), s, stage, &part[which])) {
                fprintf(stderr, "nether harvest: seed %ld passed but its index %s does not read (or its header differs)\n", s, ip);
                ibad = 1;
            }
            free(p), free(k), free(v);
        }
        for (int w = 0; w < 2; ++w)
            for (int i = 0; i < part[w].rows.n; ++i) {
                char f[64];
                long s;
                col_get(part[w].rows.v[i], part[w].seed_col, f, sizeof f);
                if (cli_long(f, &s) || (split_u(key, s) < frac) != (w == 0) || seeds_has(w ? &trs : &evs, s)) ++ibad;
            }
        for (int w = 0; w < 2 && !ibad; ++w) {
            char d[PATH_MAX + 16], f[PATH_MAX + 32];
            cli_fmt(d, sizeof d, "%s/%s", od, w ? "eval" : "train");
            cli_fmt(f, sizeof f, "%s/index.tsv", d);
            if (cli_mkdirs(d) || idx_write(&part[w], f)) ibad = 1;
            irows[w] = part[w].rows.n;
        }
        iwrote = !ibad;
        idx_free(&part[0]), idx_free(&part[1]);
    }
    struct sb js = {0};
    char made[40];
    cli_stamp(made, sizeof made, "%Y-%m-%dT%H:%M:%S%z");
    sb_puts(&js, "{\"bank\": ");
    cli_json_str(&js, bp);
    sb_printf(&js, ", \"made\": \"%s\", \"split_key\": ", made);
    cli_json_str(&js, key);
    sb_printf(&js, ", \"split_hash\": \"sha256(split_key:seed) < split_train\", \"split_train\": %g, \"train_seeds\": [%s], "
                   "\"eval_seeds\": [%s], \"failed_seeds\": [%s], \"rejected_seeds\": [%s], \"n_train\": %d, \"n_eval\": %d, "
                   "\"n_failed\": %d, \"n_rejected\": %d, \"excluded\": %d, \"overlap\": %d, \"train_starts\": %d, "
                   "\"eval_starts\": %d, \"index_errors\": %d, \"complete\": %s}\n",
              frac, tr.s ? tr.s : "", ev.s ? ev.s : "", fl.s ? fl.s : "", rj.s ? rj.s : "", ntr, nev, nfl, nrj, nex, overlap,
              irows[0], irows[1], ibad, stop_sig ? "false" : "true");
    char sp[PATH_MAX + 32];
    cli_fmt(sp, sizeof sp, "%s/splits.json", od);
    int wrc = cli_write_file_atomic(sp, js.s, js.n);
    printf("nether harvest: %d passed (%d train, %d eval, overlap %d), %d rejected, %d failed, in %.1f s%s; %s\n", ntr + nev, ntr,
           nev, overlap, nrj, nfl, cli_now() - t0, stop_sig ? " (stopped by a signal: rerun to resume)" : "",
           wrc ? "cannot write splits.json" : sp);
    if (iwrote)
        printf("nether harvest: libraries %s/train/index.tsv (%d starts) and %s/eval/index.tsv (%d starts), no seed in both\n", od,
               irows[0], od, irows[1]);
    else if (ibad)
        fprintf(stderr, "nether harvest: the train and eval libraries were not written (%d index errors)\n", ibad);
    sb_free(&js), sb_free(&tr), sb_free(&ev), sb_free(&fl), sb_free(&rj);
    cy_free(y);
    if (stop_sig) return 128 + (int)stop_sig;
    return wrc || overlap || ibad ? 1 : 0;
}
