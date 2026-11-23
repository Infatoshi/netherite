/* nether ab: variants timed against each other in interleaved rounds, ranked on their medians, held to one
 * digest (tools/abrun.c's engine, without a shell; cli_verbs.h).
 *
 *   nether ab [options] NAME=CMD ...        each variant's command split into words (no shell)
 *   nether ab [options] --spec FILE         variants (and options) from a YAML file, or NAME<TAB>CMD lines
 *   nether ab [options] -c TEMPLATE NAME... every variant TEMPLATE with each {} replaced by its NAME
 *
 * Round r runs the variants in order (r odd) or reversed (r even). Each run: its own process group, the metric
 * (group 1 of the last match of --metric), the digest (every run's equal, or none prints one), --require, wall,
 * user and system seconds, peak RSS, minor faults, and with --perf user-mode cycles, instructions and IPC.
 * Exit 0 when every run passed, else 1 (2 on a usage error, 3 when the GPU guard found the GPU busy). */
#define _GNU_SOURCE
#include "cli_proc.h"
#include "cli_verbs.h"
#include "cli_yaml.h"

#include <math.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct abrun {
    int round, rc, ok, perf_ok;
    double value, wall, utime, stime, ipc;
    long maxrss_kb, minflt;
    unsigned long long cycles, instructions;
    char digest[64], why[160];
};

struct abvar {
    char *name, *cwd;
    struct sv argv, env;
    double *v;
    int nv, dead;
    struct abrun *runs;
    int nruns;
};

static struct abvar *vars;
static int nvars;

static int add_variant(const char *name, size_t nlen, const char *cmd, const char *env, const char *cwd)
{
    if (!nlen) {
        fprintf(stderr, "nether ab: a variant without a name: %s\n", cmd);
        return -1;
    }
    for (size_t i = 0; i < nlen; ++i) {
        char c = name[i];
        if (!(c == '_' || c == '-' || c == '.' || c == '+' || c == '@' || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z'))) {
            fprintf(stderr, "nether ab: variant name %.*s: only letters, digits and _-.+@\n", (int)nlen, name);
            return -1;
        }
    }
    for (int i = 0; i < nvars; ++i)
        if (strlen(vars[i].name) == nlen && !memcmp(vars[i].name, name, nlen)) {
            fprintf(stderr, "nether ab: variant %.*s twice\n", (int)nlen, name);
            return -1;
        }
    char err[512];
    struct sv cw = {0}, ew = {0};
    if (cli_split_words(cmd, &cw, err, sizeof err) || (env && cli_split_words(env, &ew, err, sizeof err))) {
        fprintf(stderr, "nether ab: variant %.*s: %s\n", (int)nlen, name, err);
        sv_free(&cw), sv_free(&ew);
        return -1;
    }
    vars = xrealloc(vars, sizeof *vars * (size_t)(nvars + 1));
    struct abvar *v = &vars[nvars];
    memset(v, 0, sizeof *v);
    v->name = xstrndup(name, nlen);
    v->cwd = cwd ? xstrdup(cwd) : NULL;
    /* the command's words: leading NAME=VALUE words are its environment; then the env: entry's words */
    int i = 0;
    for (; i < cw.n; ++i) {
        const char *w = cw.v[i];
        size_t n = strcspn(w, "=");
        if (!w[n] || !n || w[strspn(w, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_")] != '=') break;
        sv_push(&v->env, w);
    }
    for (; i < cw.n; ++i) sv_push(&v->argv, cw.v[i]);
    sv_free(&cw);
    for (int k = 0; k < ew.n; ++k) {
        if (!strchr(ew.v[k], '=')) {
            fprintf(stderr, "nether ab: variant %s: env word %s is not NAME=VALUE\n", v->name, ew.v[k]);
            return -1;
        }
        sv_push(&v->env, ew.v[k]);
    }
    sv_free(&ew);
    if (!v->argv.n) {
        fprintf(stderr, "nether ab: variant %s has no command\n", v->name);
        return -1;
    }
    ++nvars;
    return 0;
}

/* NAME<TAB>CMD lines (# comments), or YAML */
static int read_spec(const char *path, struct cy **yaml)
{
    *yaml = NULL;
    size_t len;
    char *s = cli_read_file(path, &len);
    if (!s) {
        fprintf(stderr, "nether ab: cannot read %s\n", path);
        return -1;
    }
    size_t pl = strlen(path);
    if ((pl > 5 && !strcmp(path + pl - 5, ".yaml")) || (pl > 4 && !strcmp(path + pl - 4, ".yml"))) {
        char err[512];
        struct cy *t = cy_parse(s, path, err, sizeof err);
        free(s);
        if (!t) {
            fprintf(stderr, "nether ab: %s\n", err);
            return -1;
        }
        const struct cy *vs = cy_node(t, "variants");
        for (int i = 0; vs && i < vs->nk; ++i) {
            const struct cy *k = vs->kid[i];
            const char *cmd = k->val ? k->val : cy_get(k, "cmd", NULL);
            if (!k->key || !cmd) {
                fprintf(stderr, "nether ab: %s:%d: a variant is NAME: CMD or NAME: {cmd: ..., env: ..., cwd: ...}\n", path,
                        k->line);
                cy_free(t);
                return -1;
            }
            if (add_variant(k->key, strlen(k->key), cmd, k->val ? NULL : cy_get(k, "env", NULL), k->val ? NULL : cy_get(k, "cwd", NULL))) {
                cy_free(t);
                return -1;
            }
        }
        *yaml = t;
        return 0;
    }
    int line = 0;
    for (char *p = s, *e; *p; p = e) {
        e = strchr(p, '\n');
        if (e) *e++ = 0;
        else e = p + strlen(p);
        ++line;
        while (*p == ' ') ++p;
        if (!*p || *p == '#') continue;
        char *tab = strchr(p, '\t');
        if (!tab) {
            fprintf(stderr, "nether ab: %s:%d: no tab between the name and the command\n", path, line);
            free(s);
            return -1;
        }
        size_t n = (size_t)(tab - p);
        while (n && p[n - 1] == ' ') --n;
        char *cmd = tab + 1;
        while (*cmd == ' ' || *cmd == '\t') ++cmd;
        if (add_variant(p, n, cmd, NULL, NULL)) {
            free(s);
            return -1;
        }
    }
    free(s);
    return 0;
}

static int re_compile(regex_t *r, const char *re, const char *what)
{
    int e = regcomp(r, re, REG_EXTENDED | REG_NEWLINE);
    if (e) {
        char msg[256];
        regerror(e, r, msg, sizeof msg);
        fprintf(stderr, "nether ab: %s regex %s: %s\n", what, re, msg);
        return -1;
    }
    return 0;
}

/* group 1 of the last match of r in s, or NULL */
static char *last_group(regex_t *r, const char *s)
{
    char *best = NULL;
    regmatch_t m[2];
    const char *p = s;
    while (*p && !regexec(r, p, 2, m, p == s ? 0 : REG_NOTBOL)) {
        if (m[1].rm_so >= 0) {
            free(best);
            best = xstrndup(p + m[1].rm_so, (size_t)(m[1].rm_eo - m[1].rm_so));
        }
        p += m[0].rm_eo > 0 ? m[0].rm_eo : 1;
    }
    return best;
}

static int parse_metric(const char *s, double *out)
{
    char buf[64];
    size_t n = 0;
    for (const char *p = s; *p && n + 1 < sizeof buf; ++p)
        if (*p != ',') buf[n++] = *p;
    buf[n] = 0;
    return cli_num(buf, out) == 0;
}

static int cmp_dbl(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

static double median(const double *v, int n)
{
    if (n <= 0) return NAN;
    double *s = xmalloc(sizeof *s * (size_t)n);
    memcpy(s, v, sizeof *s * (size_t)n);
    qsort(s, (size_t)n, sizeof *s, cmp_dbl);
    double m = n % 2 ? s[n / 2] : (s[n / 2 - 1] + s[n / 2]) / 2;
    free(s);
    return m;
}

static void minmax(const double *v, int n, double *lo, double *hi)
{
    *lo = *hi = v[0];
    for (int i = 1; i < n; ++i) {
        if (v[i] < *lo) *lo = v[i];
        if (v[i] > *hi) *hi = v[i];
    }
}

static void fmt_num(char *buf, size_t n, double x)
{
    if (isnan(x)) snprintf(buf, n, "-");
    else if (fabs(x) >= 1000) snprintf(buf, n, "%.0f", x);
    else snprintf(buf, n, "%.4g", x);
}

static void tail_lines(const char *s, int k)
{
    size_t n = strlen(s);
    while (n && s[n - 1] == '\n') --n;
    size_t i = n;
    for (int seen = 0; i > 0; --i)
        if (s[i - 1] == '\n' && ++seen == k) break;
    printf("%.*s\n", (int)(n - i), s + i);
}

/* a field of the passed runs' medians */
static double run_median(const struct abvar *v, size_t off, int as_long, int need_perf)
{
    double *x = xmalloc(sizeof *x * (size_t)(v->nruns ? v->nruns : 1));
    int n = 0;
    for (int i = 0; i < v->nruns; ++i) {
        const struct abrun *r = &v->runs[i];
        if (!r->ok || (need_perf && !r->perf_ok)) continue;
        x[n++] = as_long ? (double)*(const long *)((const char *)r + off) : *(const double *)((const char *)r + off);
    }
    double m = median(x, n);
    free(x);
    return m;
}

static void usage(void)
{
    fprintf(stderr, "nether ab [-r R] [-m RE] [-d RE | --no-digest] [--require RE] [--lower] [-t S] [-k] [--log DIR] [-n]\n"
                    "             [--perf] [--json F] [--tsv F] [--lock F] [--gpu G [--gpu-guard] [--gpu-wait S]]\n"
                    "             NAME=CMD ... | --spec FILE | -c TEMPLATE NAME ...   (nether ab --help)\n");
}

int v_ab(int argc, char **argv)
{
    int rounds = -1, lower = -1, keep = 0, dry = 0, nodigest = -1, perf = -1, guard = 0, max_mib = 500, max_util = 5;
    double timeout_s = -1, gwait = 0;
    const char *metric_re = NULL, *digest_re = NULL, *require_re = NULL, *logdir = NULL, *tmpl = NULL, *spec = NULL;
    const char *json = NULL, *tsv = NULL, *lock = NULL, *gpu = NULL;
    struct sv pos = {0};
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
#define V() cli_arg(argc, argv, &i, "ab")
        if (!strcmp(a, "-r") || !strcmp(a, "--rounds")) rounds = atoi(V());
        else if (!strcmp(a, "-m") || !strcmp(a, "--metric")) metric_re = V();
        else if (!strcmp(a, "-d") || !strcmp(a, "--digest")) digest_re = V(), nodigest = 0;
        else if (!strcmp(a, "--no-digest")) nodigest = 1;
        else if (!strcmp(a, "--require")) require_re = V();
        else if (!strcmp(a, "--lower")) lower = 1;
        else if (!strcmp(a, "-t") || !strcmp(a, "--timeout")) timeout_s = atof(V());
        else if (!strcmp(a, "-k") || !strcmp(a, "--keep-going")) keep = 1;
        else if (!strcmp(a, "--log")) logdir = V();
        else if (!strcmp(a, "-n") || !strcmp(a, "--dry-run")) dry = 1;
        else if (!strcmp(a, "--spec") || !strcmp(a, "-f")) spec = V();
        else if (!strcmp(a, "-c")) tmpl = V();
        else if (!strcmp(a, "--perf")) perf = 1;
        else if (!strcmp(a, "--json")) json = V();
        else if (!strcmp(a, "--tsv")) tsv = V();
        else if (!strcmp(a, "--lock")) lock = V();
        else if (!strcmp(a, "--gpu")) gpu = V();
        else if (!strcmp(a, "--gpu-guard")) guard = 1;
        else if (!strcmp(a, "--gpu-wait")) gwait = atof(V());
        else if (!strcmp(a, "--gpu-max-mib")) max_mib = atoi(V());
        else if (!strcmp(a, "--gpu-max-util")) max_util = atoi(V());
        else if (a[0] == '-' && a[1]) {
            fprintf(stderr, "nether ab: unknown option %s\n", a);
            usage();
            return 2;
        } else
            sv_push(&pos, a);
#undef V
    }
    struct cy *y = NULL;
    if (spec && read_spec(spec, &y)) return 2;
    /* the spec file's options where the command line set none */
    if (y) {
        if (rounds < 0) rounds = (int)cy_long(y, "rounds", -1);
        if (!metric_re) metric_re = cy_get(y, "metric", NULL);
        if (!digest_re) digest_re = cy_get(y, "digest", NULL);
        if (nodigest < 0 && cy_get(y, "digest", NULL) && !strcmp(cy_get(y, "digest", ""), "off")) nodigest = 1, digest_re = NULL;
        if (!require_re) require_re = cy_get(y, "require", NULL);
        if (lower < 0) lower = cy_bool(y, "lower", -1);
        if (perf < 0) perf = cy_bool(y, "perf", -1);
        if (timeout_s < 0) timeout_s = cy_num(y, "timeout", -1);
        if (!gpu) gpu = cy_get(y, "gpu", NULL);
        if (!lock) lock = cy_get(y, "lock", NULL);
    }
    if (rounds < 0) rounds = 3;
    if (lower < 0) lower = 0;
    if (perf < 0) perf = 0;
    if (nodigest < 0) nodigest = 0;
    if (timeout_s < 0) timeout_s = 600;
    if (!metric_re) metric_re = "([0-9][0-9.,]*) env-steps/s";
    if (!digest_re) digest_re = "trajectory digest ([0-9a-f]+)";
    for (int i = 0; i < pos.n; ++i) {
        const char *a = pos.v[i];
        if (tmpl) {
            const char *k[1] = {""}, *vv[1] = {a};
            char *c = cli_subst(tmpl, k, vv, 1);
            int bad = add_variant(a, strlen(a), c, NULL, NULL);
            free(c);
            if (bad) return 2;
            continue;
        }
        const char *eq = strchr(a, '=');
        if (!eq) {
            fprintf(stderr, "nether ab: %s: a variant is NAME=CMD (or -c TEMPLATE NAME...)\n", a);
            return 2;
        }
        if (add_variant(a, (size_t)(eq - a), eq + 1, NULL, NULL)) return 2;
    }
    if (!nvars || rounds < 1 || timeout_s <= 0) {
        usage();
        return 2;
    }
    if (gpu && (!*gpu || gpu[strspn(gpu, "0123456789")])) {
        fprintf(stderr, "nether ab: --gpu takes one GPU index\n");
        return 2;
    }
    regex_t rm, rd, rq;
    if (re_compile(&rm, metric_re, "metric") || (!nodigest && re_compile(&rd, digest_re, "digest")) ||
        (require_re && re_compile(&rq, require_re, "require")))
        return 2;
    if (logdir && cli_mkdirs(logdir)) {
        fprintf(stderr, "nether ab: cannot make %s\n", logdir);
        return 2;
    }
    char gvar[48];
    if (gpu) snprintf(gvar, sizeof gvar, "CUDA_VISIBLE_DEVICES=%s", gpu);
    printf("nether ab: %d variants, %d rounds, alternating order; metric /%s/%s, digest %s%s%s%s%s%s\n", nvars, rounds,
           metric_re, lower ? " (lower is better)" : "", nodigest ? "not checked" : "/", nodigest ? "" : digest_re,
           nodigest ? "" : "/", perf ? "; perf counters on" : "", gpu ? "; " : "", gpu ? gvar : "");
    for (int i = 0; i < nvars; ++i) {
        struct sb l = {0};
        if (vars[i].env.n) cli_argv_line((const char *const *)vars[i].env.v, &l), sb_putc(&l, ' ');
        cli_argv_line((const char *const *)vars[i].argv.v, &l);
        printf("  %s: %s%s%s\n", vars[i].name, l.s, vars[i].cwd ? "  (in " : "", vars[i].cwd ? vars[i].cwd : "");
        sb_free(&l);
    }
    if (dry) {
        for (int r = 1; r <= rounds; ++r) {
            printf("round %d:", r);
            for (int j = 0; j < nvars; ++j) printf(" %s", vars[r % 2 ? j : nvars - 1 - j].name);
            printf("\n");
        }
        return 0;
    }
    int lfd = -1;
    if (lock && (lfd = cli_lock_acquire(lock, 0)) < 0) return 1;
    if (gpu && guard) {
        int mib, util, g = cli_gpu_check_idle(atoi(gpu), max_mib, max_util, gwait, &mib, &util);
        if (g) {
            cli_lock_release(lfd);
            return g < 0 ? 1 : 3;
        }
        printf("nether ab: GPU %s idle (%d MiB, %d%%)\n", gpu, mib, util);
    }
    for (int i = 0; i < nvars; ++i) {
        vars[i].v = xmalloc(sizeof(double) * (size_t)rounds);
        vars[i].runs = xcalloc((size_t)rounds, sizeof(struct abrun));
    }
    int fail = 0, stop = 0;
    char *digest0 = NULL, *digest0_by = NULL;
    int with_digest = 0, without_digest = 0;
    double t0 = cli_now();
    int nw = 7;
    for (int i = 0; i < nvars; ++i)
        if ((int)strlen(vars[i].name) > nw) nw = (int)strlen(vars[i].name);
    for (int r = 1; r <= rounds && !stop && !cli_interrupted; ++r) {
        for (int j = 0; j < nvars && !stop && !cli_interrupted; ++j) {
            struct abvar *v = &vars[r % 2 ? j : nvars - 1 - j];
            if (v->dead) continue;
            double load[1] = {0};
            if (getloadavg(load, 1) < 1) load[0] = -1;
            struct sv env = {0};
            for (int k = 0; k < v->env.n; ++k) sv_push(&env, v->env.v[k]);
            if (gpu) sv_push(&env, gvar);
            struct sb out = {0};
            struct cli_spawn_opts so = {.argv = (const char *const *)v->argv.v, .cwd = v->cwd, .env_set = (const char *const *)env.v,
                                        .capture = &out, .timeout_s = timeout_s, .grace_s = 5, .new_pgroup = 1, .quiet = 1,
                                        .stdin_null = 1, .perf = perf};
            struct cli_spawn_res res;
            int sp = cli_spawn(&so, &res);
            sv_free(&env);
            const char *o = sb_str(&out);
            if (logdir) {
                char *p = xasprintf("%s/%s-r%d.log", logdir, v->name, r);
                cli_write_file_atomic(p, o, strlen(o));
                free(p);
            }
            struct abrun *ar = &v->runs[v->nruns++];
            ar->round = r;
            ar->rc = res.rc, ar->wall = res.wall, ar->utime = res.utime, ar->stime = res.stime;
            ar->maxrss_kb = res.maxrss_kb, ar->minflt = res.minflt;
            ar->perf_ok = res.perf_ok, ar->cycles = res.cycles, ar->instructions = res.instructions, ar->ipc = res.ipc;
            ar->value = NAN;
            char *ms = last_group(&rm, o), *dg = nodigest ? NULL : last_group(&rd, o);
            double x = 0;
            int parsed = 0;
            const char *why = NULL;
            char whybuf[160];
            if (sp) why = "could not be started";
            else if (res.timed_out) snprintf(whybuf, sizeof whybuf, "timed out after %.0f s", timeout_s), why = whybuf;
            else if (res.rc) snprintf(whybuf, sizeof whybuf, "rc %d", res.rc), why = whybuf;
            else if (!ms) why = "no metric in its output";
            else if (!(parsed = parse_metric(ms, &x))) snprintf(whybuf, sizeof whybuf, "metric \"%.40s\" is not a number", ms), why = whybuf;
            else if (require_re && regexec(&rq, o, 0, NULL, 0)) why = "--require did not match";
            if (!why && !nodigest) {
                if (dg) ++with_digest;
                else ++without_digest;
                if (dg && !digest0) digest0 = xstrdup(dg), digest0_by = xasprintf("%s r%d", v->name, r);
                if (dg && strcmp(dg, digest0)) {
                    snprintf(whybuf, sizeof whybuf, "digest %.20s, not %.30s's %.20s", dg, digest0_by, digest0);
                    why = whybuf;
                } else if (with_digest && without_digest)
                    why = "some runs print a digest and others none";
            }
            if (parsed) ar->value = x;
            if (dg) snprintf(ar->digest, sizeof ar->digest, "%.63s", dg);
            char num[32] = "-";
            if (parsed) fmt_num(num, sizeof num, x);
            char pc[96] = "";
            if (perf && res.perf_ok) snprintf(pc, sizeof pc, "  ipc %.3f  %.3g instr", res.ipc, (double)res.instructions);
            printf("r%d %-*s %12s  digest %-16s %7.1f s  user %7.1f s  rss %6.0f MB  load %.1f%s%s%s\n", r, nw, v->name, num,
                   dg ? dg : "-", res.wall, res.utime, res.maxrss_kb / 1024.0, load[0], pc, why ? "  FAIL: " : "", why ? why : "");
            fflush(stdout);
            if (why) {
                snprintf(ar->why, sizeof ar->why, "%s", why);
                fail = 1;
                printf("--- the last lines of %s r%d:\n", v->name, r);
                tail_lines(o, 8);
                printf("---\n");
                if (keep) v->dead = 1;
                else stop = 1;
            } else {
                ar->ok = 1;
                v->v[v->nv++] = x;
            }
            free(ms);
            free(dg);
            sb_free(&out);
        }
    }
    if (perf) {
        int any = 0;
        for (int i = 0; i < nvars; ++i)
            for (int k = 0; k < vars[i].nruns; ++k) any |= vars[i].runs[k].perf_ok;
        if (!any) printf("nether ab: no hardware counters (perf_event_open refused; kernel.perf_event_paranoid): rusage only\n");
    }
    /* the table */
    double med0 = NAN, lo0 = 0, hi0 = 0, maxmed = 0;
    int have0 = vars[0].nv > 0;
    if (have0) med0 = median(vars[0].v, vars[0].nv), minmax(vars[0].v, vars[0].nv, &lo0, &hi0);
    for (int i = 0; i < nvars; ++i)
        if (vars[i].nv) {
            double m = median(vars[i].v, vars[i].nv);
            if (m > maxmed) maxmed = m;
        }
    printf("\n%-*s %4s %12s %12s %12s %7s %7s  %-14s %8s %9s\n", nw, "variant", "runs", "median", "min", "max", "spread",
           "ratio", "vs first", "ipc", "rss MB");
    int best = -1;
    double bestm = 0;
    char stamp[40], sha[32];
    cli_stamp(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%S%z");
    cli_git_sha(sha, sizeof sha);
    struct sb js = {0}, rows = {0};
    sb_printf(&js, "{\"made\": \"%s\", \"commit\": \"%s\", \"rounds\": %d, \"metric\": ", stamp, sha, rounds);
    cli_json_str(&js, metric_re);
    sb_printf(&js, ", \"lower_is_better\": %s, \"digest_checked\": %s, \"digest\": ", lower ? "true" : "false",
              nodigest ? "false" : "true");
    if (digest0) cli_json_str(&js, digest0);
    else sb_puts(&js, "null");
    sb_printf(&js, ", \"gpu\": %s%s%s, \"perf\": %s, \"variants\": [", gpu ? "\"" : "", gpu ? gpu : "null", gpu ? "\"" : "",
              perf ? "true" : "false");
    for (int i = 0; i < nvars; ++i) {
        struct abvar *v = &vars[i];
        double m = NAN, lo = NAN, hi = NAN, spread = NAN, ratio = NAN;
        const char *vs = i ? "-" : "first";
        if (v->nv) {
            m = median(v->v, v->nv);
            minmax(v->v, v->nv, &lo, &hi);
            spread = m ? 100 * (hi - lo) / m : 0;
            if (best < 0 || (lower ? m < bestm : m > bestm)) best = i, bestm = m;
            if (have0 && med0 != 0) ratio = m / med0;
            if (i && have0)
                vs = (lo > hi0 || hi < lo0) ? (lower ? (m < med0 ? "apart, faster" : "apart, slower")
                                                     : (m > med0 ? "apart, faster" : "apart, slower"))
                                            : "overlap";
        }
        double ipc = run_median(v, offsetof(struct abrun, ipc), 0, 1);
        double rss = run_median(v, offsetof(struct abrun, maxrss_kb), 1, 0);
        double wall = run_median(v, offsetof(struct abrun, wall), 0, 0);
        char a[32], b[32], c[32], rt[16] = "-", ic[16] = "-";
        fmt_num(a, sizeof a, m), fmt_num(b, sizeof b, lo), fmt_num(c, sizeof c, hi);
        if (!isnan(ratio)) snprintf(rt, sizeof rt, "%.4f", ratio);
        if (!isnan(ipc)) snprintf(ic, sizeof ic, "%.3f", ipc);
        char sp[16] = "-";
        if (!isnan(spread)) snprintf(sp, sizeof sp, "%.2f%%", spread);
        printf("%-*s %4d %12s %12s %12s %7s %7s  %-14s %8s %9.0f\n", nw, v->name, v->nv, a, b, c, sp, rt, vs, ic,
               isnan(rss) ? 0 : rss / 1024.0);
        /* JSON */
        sb_puts(&js, i ? ", {\"name\": " : "{\"name\": ");
        cli_json_str(&js, v->name);
        struct sb cl = {0};
        if (v->env.n) cli_argv_line((const char *const *)v->env.v, &cl), sb_putc(&cl, ' ');
        cli_argv_line((const char *const *)v->argv.v, &cl);
        sb_puts(&js, ", \"cmd\": ");
        cli_json_str(&js, cl.s);
        sb_printf(&js, ", \"passed\": %d, \"median\": ", v->nv);
        cli_json_num(&js, m);
        sb_puts(&js, ", \"min\": ");
        cli_json_num(&js, lo);
        sb_puts(&js, ", \"max\": ");
        cli_json_num(&js, hi);
        sb_puts(&js, ", \"spread_pct\": ");
        cli_json_num(&js, spread);
        sb_puts(&js, ", \"ratio\": ");
        cli_json_num(&js, ratio);
        sb_puts(&js, ", \"verdict\": ");
        cli_json_str(&js, vs);
        sb_puts(&js, ", \"wall_median\": ");
        cli_json_num(&js, wall);
        sb_puts(&js, ", \"ipc_median\": ");
        cli_json_num(&js, ipc);
        sb_puts(&js, ", \"maxrss_kb_median\": ");
        cli_json_num(&js, rss);
        sb_puts(&js, ", \"runs\": [");
        for (int k = 0; k < v->nruns; ++k) {
            const struct abrun *r = &v->runs[k];
            sb_printf(&js, "%s{\"round\": %d, \"ok\": %s, \"rc\": %d, \"value\": ", k ? ", " : "", r->round,
                      r->ok ? "true" : "false", r->rc);
            cli_json_num(&js, r->value);
            sb_puts(&js, ", \"digest\": ");
            if (r->digest[0]) cli_json_str(&js, r->digest);
            else sb_puts(&js, "null");
            sb_printf(&js, ", \"wall\": %.3f, \"utime\": %.3f, \"stime\": %.3f, \"maxrss_kb\": %ld, \"minflt\": %ld", r->wall,
                      r->utime, r->stime, r->maxrss_kb, r->minflt);
            if (r->perf_ok) sb_printf(&js, ", \"cycles\": %llu, \"instructions\": %llu, \"ipc\": %.4f", r->cycles, r->instructions, r->ipc);
            if (r->why[0]) sb_puts(&js, ", \"why\": "), cli_json_str(&js, r->why);
            sb_puts(&js, "}");
        }
        sb_puts(&js, "]}");
        /* the scoreboard row */
        sb_printf(&rows, "%s\t%s\t%s\t%d\t%d\t", stamp, sha, v->name, v->nv, rounds);
        sb_printf(&rows, "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%.0f\t%s\t", a, b, c, sp, rt, vs,
                  nodigest ? "unchecked" : (digest0 ? digest0 : "none"), ic, isnan(rss) ? 0 : rss, metric_re);
        for (char *p = cl.s; p && *p; ++p)
            if (*p == '\t' || *p == '\n') *p = ' ';
        sb_printf(&rows, "%s\n", cl.s ? cl.s : "");
        sb_free(&cl);
    }
    int ok = !fail;
    sb_printf(&js, "], \"ok\": %s, \"wall\": %.1f}\n", ok ? "true" : "false", cli_now() - t0);
    printf("\n");
    for (int i = 0; i < nvars; ++i) {
        if (!vars[i].nv) continue;
        double m = median(vars[i].v, vars[i].nv);
        int w = maxmed > 0 ? (int)lround(50 * m / maxmed) : 0;
        char a[32];
        fmt_num(a, sizeof a, m);
        printf("%-*s |", nw, vars[i].name);
        for (int k = 0; k < w; ++k) fputs("#", stdout);
        printf("%*s %s%s\n", 50 - w, "", a, i == best ? (lower ? "  <- lowest" : "  <- highest") : "");
    }
    if (best >= 0 && vars[0].nv && vars[0].nv < 2) printf("(one run a variant: no spread to rank on; use -r 3 or more)\n");
    if (json) {
        if (cli_write_file_atomic(json, js.s, js.n)) fprintf(stderr, "nether ab: cannot write %s\n", json), ok = 0;
        else printf("nether ab: %s\n", json);
    }
    if (tsv) {
        if (cli_append_locked(tsv, "made\tcommit\tvariant\tpassed\trounds\tmedian\tmin\tmax\tspread\tratio\tverdict\tdigest\t"
                                   "ipc\tmaxrss_kb\tmetric\tcmd\n", rows.s ? rows.s : ""))
            fprintf(stderr, "nether ab: cannot append to %s\n", tsv), ok = 0;
        else printf("nether ab: %s (%d rows)\n", tsv, nvars);
    }
    printf("\nnether ab: %s in %.0f s; digest %s\n", ok ? "OK" : "FAIL", cli_now() - t0,
           nodigest ? "not checked" : digest0 ? digest0 : "none printed");
    sb_free(&js);
    sb_free(&rows);
    cli_lock_release(lfd);
    cy_free(y);
    return ok ? 0 : 1;
}
