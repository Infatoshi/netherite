/* abrun: variants timed against each other in interleaved rounds, ranked on their medians, held to one digest.
 *
 * The job about twenty lanes each wrote again as a script (148 of them in the lanes' out/ trees, 2026-10-31): run
 * A and B (or a grid of 48) a few times each, in an order that cancels drift, read a number from each run, check
 * the runs did the same work, and say which is faster and by how much. The scripts' mistakes, which this tool does
 * not make: ranking on one run (128-env runs of one build swung 172k to 298k env-steps/s), A always before B (a
 * warming or cooling machine favours one side), a failed or empty run read as a number, and a variant that did
 * other work (a different trajectory) compared at all.
 *
 *   abrun [options] NAME=CMD ...       each variant a shell command (sh -c), run from the current directory
 *   abrun [options] -f FILE            the variants from FILE: one NAME<TAB>CMD a line, # comments
 *   abrun [options] -c TEMPLATE NAME...  every variant TEMPLATE with each {} replaced by its NAME
 *
 * Round r runs the variants in order (r odd) or reversed (r even): A B, B A, A B, ... (for A B C: A B C, C B A).
 * One line a run: round, variant, the metric, the digest, seconds, the load average before it. Each run's whole
 * output is kept in --log DIR (NAME-rN.log) if given; on a failure its last lines are printed.
 *
 *   -r R, --rounds R     rounds (default 3; at least 2 gives every variant both positions)
 *   -m RE, --metric RE   POSIX extended regex with one group: the metric, from the run's last matching line
 *                        (default '([0-9][0-9.,]*) env-steps/s'; commas are dropped)
 *   -d RE, --digest RE   the trajectory digest (default 'trajectory digest ([0-9a-f]+)'); every run of every variant
 *                        must print the same one, or none of them may print one
 *   --no-digest          no digest check (a kernel on a dump: use --require for its own check)
 *   --require RE         a run fails unless its output matches RE (rcbench: 'frames equal, seen equal')
 *   --lower              lower is better (ms): the ranking and the "faster" words follow it
 *   -t S, --timeout S    a run's limit in seconds (default 600): past it its process group is killed, the run fails
 *   -k, --keep-going     a failed run drops its variant from the later rounds instead of ending the rounds
 *   --log DIR            every run's output kept as DIR/NAME-rN.log
 *   -n, --dry-run        print the order and the commands only
 *
 * Exit 0 when every run passed (rc 0, a metric, --require matched, digests equal), else 1.
 *
 * The table at the end: variant, runs, median, min, max, spread ((max - min) / median), the median's ratio to the
 * first variant's, and whether the two ranges [min, max] overlap ("apart" is a difference the runs' own noise does
 * not cover; "overlap" is not one). Then a bar a variant, scaled to the largest median.
 *
 * Each command runs in its own process group (a gate that ends its group, as the pool's references do without
 * setsid -w, cannot end abrun) with stdout and stderr captured together. Run it inside a window:
 *   out/flywheel/bin/gpuq.sh bench -- out/native/abrun ...   (the whole machine)
 *   out/flywheel/bin/gpuq.sh gpu -- out/native/abrun ...     (device-only timing: CUDA events)
 *
 * Built as out/native/abrun by csrc/Makefile. POSIX only (the Mac builds it). */
#define _GNU_SOURCE
#define _DARWIN_C_SOURCE
#include "../csrc/tests/tool.h"

#include <regex.h>

struct variant {
    char *name, *cmd;
    double *v;    /* the metric of each passed run */
    int nv, dead; /* dead: dropped after a failure (-k) */
};

static struct variant *vars;
static int nvars;

static void add_variant(const char *name, size_t nlen, const char *cmd)
{
    if (!nlen) die("abrun: a variant without a name: %s", cmd);
    for (size_t i = 0; i < nlen; ++i) {
        char c = name[i];
        if (!(c == '_' || c == '-' || c == '.' || c == '+' || c == '@' || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z')))
            die("abrun: variant name %.*s: only letters, digits and _-.+@", (int)nlen, name);
    }
    for (int i = 0; i < nvars; ++i)
        if (strlen(vars[i].name) == nlen && !memcmp(vars[i].name, name, nlen)) die("abrun: variant %.*s twice", (int)nlen, name);
    vars = xrealloc(vars, sizeof *vars * (size_t)(nvars + 1));
    vars[nvars] = (struct variant){xstrndup(name, nlen), xstrdup(cmd), NULL, 0, 0};
    ++nvars;
}

static char *subst(const char *tmpl, const char *name)
{
    struct sb b = {0};
    for (const char *p = tmpl; *p; ++p) {
        if (p[0] == '{' && p[1] == '}') sb_puts(&b, name), ++p;
        else sb_putc(&b, *p);
    }
    return (char *)sb_str(&b);
}

static void read_file_variants(const char *path)
{
    size_t len;
    char *s = read_file(path, &len);
    if (!s) die("abrun: cannot read %s", path);
    int line = 0;
    for (char *p = s, *e; *p; p = e) {
        e = strchr(p, '\n');
        if (e) *e++ = 0;
        else e = p + strlen(p);
        ++line;
        char *t = strip(p);
        if (!*t || *t == '#') continue;
        char *tab = strchr(t, '\t');
        if (!tab) die("abrun: %s:%d: no tab between the name and the command", path, line);
        char *cmd = strip(tab + 1);
        size_t n = (size_t)(tab - t);
        while (n && is_space(t[n - 1])) --n;
        add_variant(t, n, cmd);
    }
    free(s);
}

static regex_t re_compile(const char *re, const char *what)
{
    regex_t r;
    int e = regcomp(&r, re, REG_EXTENDED | REG_NEWLINE);
    if (e) {
        char msg[256];
        regerror(e, &r, msg, sizeof msg);
        die("abrun: %s regex %s: %s", what, re, msg);
    }
    return r;
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
    char *e;
    *out = strtod(buf, &e);
    return n && !*e;
}

static volatile sig_atomic_t g_child_pgid;

static void on_signal(int sig)
{
    if (g_child_pgid > 0) kill(-(pid_t)g_child_pgid, SIGKILL);
    signal(sig, SIG_DFL);
    raise(sig);
}

/* run cmd under sh -c in its own process group, stdout and stderr into *out; the exit status as a shell reports
 * it, -999 on the timeout */
static int run_one(const char *cmd, double timeout_s, struct sb *out)
{
    int po[2];
    if (pipe(po)) die("abrun: pipe: %s", strerror(errno));
    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();
    if (pid < 0) die("abrun: fork: %s", strerror(errno));
    if (!pid) {
        setpgid(0, 0);
        dup2(po[1], 1);
        dup2(po[1], 2);
        close(po[0]);
        close(po[1]);
        int nul = open("/dev/null", O_RDONLY);
        if (nul >= 0) dup2(nul, 0), close(nul);
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
    setpgid(pid, pid); /* both sides, so the group exists before either goes on */
    g_child_pgid = pid;
    close(po[1]);
    double t_end = now_secs() + timeout_s;
    int timed_out = 0;
    for (;;) {
        double left = t_end - now_secs();
        if (left <= 0) {
            timed_out = 1;
            break;
        }
        struct pollfd pf = {po[0], POLLIN, 0};
        int k = poll(&pf, 1, left > 1e6 ? -1 : (int)(left * 1000) + 1);
        if (k < 0) {
            if (errno == EINTR) continue;
            die("abrun: poll: %s", strerror(errno));
        }
        if (!k) continue;
        char buf[65536];
        ssize_t r = read(po[0], buf, sizeof buf);
        if (r > 0) sb_putn(out, buf, (size_t)r);
        else if (r == 0 || (errno != EAGAIN && errno != EINTR)) break;
    }
    /* the command's group goes with it: nothing it started stays behind to share the window */
    kill(-pid, timed_out ? SIGKILL : SIGTERM);
    close(po[0]);
    int st;
    while (waitpid(pid, &st, 0) < 0)
        if (errno != EINTR) die("abrun: waitpid: %s", strerror(errno));
    g_child_pgid = 0;
    sb_str(out);
    if (timed_out) return -999;
    return wait_rc(st);
}

static int cmp_dbl(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

static double median(const double *v, int n)
{
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

/* a number as the runs print it: integers for big values, else four significant digits */
static void fmt_num(char *buf, size_t n, double x)
{
    if (fabs(x) >= 1000) snprintf(buf, n, "%.0f", x);
    else snprintf(buf, n, "%.4g", x);
}

static void tail_lines(const char *s, int k)
{
    size_t n = strlen(s);
    while (n && s[n - 1] == '\n') --n;
    size_t i = n;
    for (int seen = 0; i > 0; --i)
        if (s[i - 1] == '\n' && ++seen == k) break;
    fprintf(stdout, "%.*s\n", (int)(n - i), s + i);
}

static void usage(void)
{
    fprintf(stderr, "usage: abrun [-r R] [-m RE] [-d RE | --no-digest] [--require RE] [--lower] [-t S] [-k] [--log DIR] [-n]\n"
                    "             NAME=CMD ... | -f FILE | -c TEMPLATE NAME ...\n"
                    "(the header of tools/abrun.c has the whole of it)\n");
    exit(2);
}

int main(int argc, char **argv)
{
    int rounds = 3, lower = 0, keep = 0, dry = 0, nodigest = 0;
    double timeout_s = 600;
    const char *metric_re = "([0-9][0-9.,]*) env-steps/s", *digest_re = "trajectory digest ([0-9a-f]+)";
    const char *require_re = NULL, *logdir = NULL, *tmpl = NULL;
    struct sv files = {0}, pos = {0};
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
#define ARG() (i + 1 < argc ? argv[++i] : (usage(), (char *)0))
        if (!strcmp(a, "-r") || !strcmp(a, "--rounds")) rounds = atoi(ARG());
        else if (!strcmp(a, "-m") || !strcmp(a, "--metric")) metric_re = ARG();
        else if (!strcmp(a, "-d") || !strcmp(a, "--digest")) digest_re = ARG();
        else if (!strcmp(a, "--no-digest")) nodigest = 1;
        else if (!strcmp(a, "--require")) require_re = ARG();
        else if (!strcmp(a, "--lower")) lower = 1;
        else if (!strcmp(a, "-t") || !strcmp(a, "--timeout")) timeout_s = atof(ARG());
        else if (!strcmp(a, "-k") || !strcmp(a, "--keep-going")) keep = 1;
        else if (!strcmp(a, "--log")) logdir = ARG();
        else if (!strcmp(a, "-n") || !strcmp(a, "--dry-run")) dry = 1;
        else if (!strcmp(a, "-f")) sv_push(&files, xstrdup(ARG()));
        else if (!strcmp(a, "-c")) tmpl = ARG();
        else if (!strcmp(a, "-h") || !strcmp(a, "--help")) usage();
        else if (a[0] == '-' && a[1]) die("abrun: unknown option %s (abrun --help)", a);
        else sv_push(&pos, xstrdup(a));
#undef ARG
    }
    if (rounds < 1 || timeout_s <= 0) usage();
    for (int i = 0; i < files.n; ++i) read_file_variants(files.v[i]);
    for (int i = 0; i < pos.n; ++i) {
        const char *a = pos.v[i];
        if (tmpl) {
            char *c = subst(tmpl, a);
            add_variant(a, strlen(a), c);
            free(c);
            continue;
        }
        const char *eq = strchr(a, '=');
        if (!eq) die("abrun: %s: a variant is NAME=CMD (or -c TEMPLATE NAME...)", a);
        add_variant(a, (size_t)(eq - a), eq + 1);
    }
    if (!nvars) usage();

    regex_t rm = re_compile(metric_re, "metric"), rd, rq;
    memset(&rd, 0, sizeof rd);
    memset(&rq, 0, sizeof rq);
    if (!nodigest) rd = re_compile(digest_re, "digest");
    if (require_re) rq = re_compile(require_re, "require");
    if (logdir) mkdirs(logdir);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP, on_signal);
    for (int i = 0; i < nvars; ++i) vars[i].v = xmalloc(sizeof(double) * (size_t)rounds);

    printf("abrun: %d variants, %d rounds, alternating order; metric /%s/%s, digest %s%s%s\n", nvars, rounds, metric_re,
           lower ? " (lower is better)" : "", nodigest ? "not checked" : "/", nodigest ? "" : digest_re, nodigest ? "" : "/");
    for (int i = 0; i < nvars; ++i) printf("  %s: %s\n", vars[i].name, vars[i].cmd);
    if (dry) {
        for (int r = 1; r <= rounds; ++r) {
            printf("round %d:", r);
            for (int j = 0; j < nvars; ++j) printf(" %s", vars[r % 2 ? j : nvars - 1 - j].name);
            printf("\n");
        }
        return 0;
    }

    int fail = 0, stop = 0;
    char *digest0 = NULL, *digest0_by = NULL; /* the first digest seen and the run that printed it */
    int with_digest = 0, without_digest = 0;
    double t0 = now_secs();
    int nw = 7; /* "variant" */
    for (int i = 0; i < nvars; ++i)
        if ((int)strlen(vars[i].name) > nw) nw = (int)strlen(vars[i].name);
    for (int r = 1; r <= rounds && !stop; ++r) {
        for (int j = 0; j < nvars && !stop; ++j) {
            struct variant *v = &vars[r % 2 ? j : nvars - 1 - j];
            if (v->dead) continue;
            double load[1] = {0};
            if (getloadavg(load, 1) < 1) load[0] = -1;
            struct sb out = {0};
            double ts = now_secs();
            int rc = run_one(v->cmd, timeout_s, &out);
            double secs = now_secs() - ts;
            const char *o = sb_str(&out);
            if (logdir) {
                char *p = xasprintf("%s/%s-r%d.log", logdir, v->name, r);
                write_file(p, o, strlen(o));
                free(p);
            }
            char *ms = last_group(&rm, o), *dg = nodigest ? NULL : last_group(&rd, o);
            double x = 0;
            int parsed = 0;
            const char *why = NULL;
            char whybuf[160];
            if (rc == -999) snprintf(whybuf, sizeof whybuf, "timed out after %.0f s", timeout_s), why = whybuf;
            else if (rc) snprintf(whybuf, sizeof whybuf, "rc %d", rc), why = whybuf;
            else if (!ms) why = "no metric in its output";
            else if (!(parsed = parse_metric(ms, &x)))
                snprintf(whybuf, sizeof whybuf, "metric \"%.40s\" is not a number", ms), why = whybuf;
            else if (require_re && regexec(&rq, o, 0, NULL, 0)) why = "--require did not match";
            if (!why && !nodigest) {
                if (dg) ++with_digest;
                else ++without_digest;
                if (dg && !digest0) {
                    digest0 = xstrdup(dg);
                    digest0_by = xasprintf("%s r%d", v->name, r);
                }
                if (dg && strcmp(dg, digest0)) {
                    snprintf(whybuf, sizeof whybuf, "digest %s, not %s's %s", dg, digest0_by, digest0);
                    why = whybuf;
                } else if (with_digest && without_digest)
                    why = "some runs print a digest and others none";
            }
            char num[32] = "-";
            if (parsed) fmt_num(num, sizeof num, x); /* shown on a failure too; only a passed run is ranked */
            printf("r%d %-*s %12s  digest %-16s %7.1f s  load %.1f%s%s\n", r, nw, v->name, num, dg ? dg : "-", secs, load[0],
                   why ? "  FAIL: " : "", why ? why : "");
            fflush(stdout);
            if (why) {
                fail = 1;
                printf("--- the last lines of %s r%d:\n", v->name, r);
                tail_lines(o, 8);
                printf("---\n");
                if (keep) v->dead = 1;
                else stop = 1;
            } else
                v->v[v->nv++] = x;
            free(ms);
            free(dg);
            sb_free(&out);
        }
    }

    /* the table and the bars */
    double med0 = 0, lo0 = 0, hi0 = 0, maxmed = 0;
    int have0 = vars[0].nv > 0;
    if (have0) med0 = median(vars[0].v, vars[0].nv), minmax(vars[0].v, vars[0].nv, &lo0, &hi0);
    for (int i = 0; i < nvars; ++i)
        if (vars[i].nv) {
            double m = median(vars[i].v, vars[i].nv);
            if (m > maxmed) maxmed = m;
        }
    printf("\n%-*s %4s %12s %12s %12s %7s %7s  %s\n", nw, "variant", "runs", "median", "min", "max", "spread", "ratio",
           "vs first");
    int best = -1;
    double bestm = 0;
    for (int i = 0; i < nvars; ++i) {
        struct variant *v = &vars[i];
        if (!v->nv) {
            printf("%-*s %4d %12s\n", nw, v->name, 0, "-");
            continue;
        }
        double m = median(v->v, v->nv), lo, hi;
        minmax(v->v, v->nv, &lo, &hi);
        if (best < 0 || (lower ? m < bestm : m > bestm)) best = i, bestm = m;
        char a[32], b[32], c[32], ratio[16] = "-";
        fmt_num(a, sizeof a, m), fmt_num(b, sizeof b, lo), fmt_num(c, sizeof c, hi);
        const char *vs = "";
        if (have0 && med0 != 0) snprintf(ratio, sizeof ratio, "%.4f", m / med0);
        if (i && have0) vs = (lo > hi0 || hi < lo0) ? (lower ? (m < med0 ? "apart, faster" : "apart, slower")
                                                               : (m > med0 ? "apart, faster" : "apart, slower"))
                                                     : "overlap";
        printf("%-*s %4d %12s %12s %12s %6.2f%% %7s  %s\n", nw, v->name, v->nv, a, b, c, m ? 100 * (hi - lo) / m : 0, ratio, vs);
    }
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
    if (best >= 0 && vars[0].nv && vars[0].nv < 2)
        printf("(one run a variant: no spread to rank on; use -r 3 or more)\n");
    printf("\nabrun: %s in %.0f s; digest %s\n", fail ? "FAIL" : "OK", now_secs() - t0,
           nodigest ? "not checked" : digest0 ? digest0 : "none printed");
    return fail;
}
