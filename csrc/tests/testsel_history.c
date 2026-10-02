/* The test selection (TESTSELECT, tests/fnkey.c) checked on master's history.
 *
 *     out/native/testsel_history [--merges 30] [--work DIR] [--jobs N] [--only SHA...]
 *
 * For each of master's last N merges M (first parent P): both trees are built
 * from git (csrc/ only, the recordings of this checkout's out/java), each
 * with -ffunction-sections as this lane builds, and every make test job (the
 * play checks aside) runs on each tree's function coverage build through
 * testmap (out/native/testmap), which records the jobs that pass and keeps
 * every run's log and rc. Then fnkey check selects M's jobs against P's
 * records, as make test would in a lane that made M's change on P. A job whose
 * rc or log (the tree's own path taken out) differs between P and M, or that P
 * lacks, must be selected; one that is not is a miss. One line per merge to
 * WORK/results.tsv: merge, jobs, selected, fraction, changed, misses. Trees
 * are built one commit at a time and deleted after use; a commit's records
 * and logs stay (WORK/SHA/store, runs), so a rerun resumes. Run it in this
 * checkout (the repository holding the current directory). The C port of
 * tests/testsel_history.py (lane/cport). */
#define _GNU_SOURCE
#include "tool.h"

static char LANE[4096], W[4096], TOOLS[4200], FNKEY[4300], DATA[4200];
static const char *CF = "-O2 -std=c11 -Wall -Wextra -Wno-unused-parameter -ffp-contract=off -Werror -ffunction-sections";
static char G[512];
static int A_merges = 30, A_jobs = 24;
static struct sv A_only;

static void logmsg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void logmsg(const char *fmt, ...)
{
    char ts[16];
    time_t now = time(NULL);
    strftime(ts, sizeof ts, "%H:%M:%S ", localtime(&now));
    struct sb b = {0};
    va_list ap;
    va_start(ap, fmt);
    sb_vprintf(&b, fmt, ap);
    va_end(ap);
    printf("%s%s\n", ts, b.s);
    fflush(stdout);
    char *p = path_join(W, "history.log");
    FILE *f = fopen(p, "a");
    if (f) fprintf(f, "%s%s\n", ts, b.s), fclose(f);
    free(p);
    sb_free(&b);
}

static char *wx(const char *x, const char *what) { return xasprintf("%s/%s/%s", W, x, what); }

static char *tree(const char *x) { return wx(x, "tree"); }

static void touch(const char *p)
{
    FILE *f = fopen(p, "w");
    if (f) fclose(f);
}

static void rmtree(const char *p)
{
    char *av[] = {"rm", "-rf", (char *)p, NULL};
    struct stat st;
    if (lstat(p, &st) == 0 && S_ISLNK(st.st_mode)) return; /* rmtree refuses a link */
    run_cmd(av, NULL, NULL, NULL, 0, NULL);
}

static int sh(const char *cmd, const char *cwd)
{
    char *av[] = {"sh", "-c", (char *)cmd, NULL};
    return run_cmd(av, cwd, NULL, NULL, 0, NULL);
}

/* the last n bytes of s */
static const char *tail(const char *s, size_t n)
{
    size_t l = strlen(s);
    return l > n ? s + l - n : s;
}

/* csrc/ of commit x, its normal build (plus the objects fnkey hashes that
 * older Makefiles compile inside a link) and its coverage build */
static int build(const char *x)
{
    char *t = tree(x), *bm = wx(x, "built");
    if (path_exists(bm)) return 1;
    rmtree(t);
    char *to = xasprintf("%s/out", t);
    mkdirs(to);
    char *tj = xasprintf("%s/oracle", to);
    if (symlink(DATA, tj)) die("testsel_history: symlink %s: %s", tj, strerror(errno));
    char *c = xasprintf("git -C %s archive %s native | tar -x -C %s", LANE, x, t);
    if (sh(c, NULL)) die("testsel_history: %s failed", c);
    char *n = xasprintf("%s/csrc", t), *o = xasprintf("%s/out/native", t);
    double t0 = now_secs();
    char *cfl = xasprintf("CFLAGS=%s", CF), *pl = xasprintf("%s/play", o), *pd = xasprintf("%s/play-dev", o);
    char *av[] = {"make", "-s", "-C", n, cfl, "all", pl, pd, NULL};
    struct sb out = {0};
    int fd2[2] = {-1, -1};
    (void)fd2;
    /* stdout and stderr together */
    char *mk = xasprintf("make -s -C '%s' '%s' all '%s' '%s' 2>&1", n, cfl, pl, pd);
    char *shv[] = {"sh", "-c", mk, NULL};
    (void)av;
    if (run_cmd(shv, NULL, NULL, &out, 0, NULL)) {
        logmsg("%.10s normal build failed: %s", x, tail(sb_str(&out), 2000));
        return 0;
    }
    char *ev = xasprintf("make -s -C '%s' '%s' --eval 'pcf: ; @echo $(CFLAGS)' pcf", n, cfl);
    char *evv[] = {"sh", "-c", ev, NULL};
    struct sb effb = {0};
    run_cmd(evv, NULL, NULL, &effb, 0, NULL);
    char *eff = strip((char *)sb_str(&effb));
    char *sdlv[] = {"sh", "-c", "pkg-config --cflags sdl3", NULL};
    struct sb sdlb = {0};
    run_cmd(sdlv, NULL, NULL, &sdlb, 0, NULL);
    static const char *const SRC[3] = {"tests/input_guard.c", "play/play.c", "play/play.c"};
    static const char *const OBJ[3] = {"input_guard.o", "play.o", "play-dev.o"};
    for (int i = 0; i < 3; ++i) {
        char *op = xasprintf("%s/obj/%s", o, OBJ[i]);
        if (path_exists(op)) continue;
        struct sv cv = {0};
        sv_push(&cv, "cc");
        char *e2 = xstrdup(eff), *f;
        for (char *s = strtok_r(e2, " \t\n", &f); s; s = strtok_r(NULL, " \t\n", &f)) sv_push(&cv, s);
        if (i) {
            char *s2 = xstrdup(sb_str(&sdlb)), *g;
            for (char *s = strtok_r(s2, " \t\n", &g); s; s = strtok_r(NULL, " \t\n", &g)) sv_push(&cv, s);
            if (i == 2) sv_push(&cv, "-DNETHERITE_DEV");
        }
        sv_push(&cv, "-c");
        sv_push(&cv, "-o");
        sv_push(&cv, op);
        sv_push(&cv, (char *)SRC[i]);
        sv_push(&cv, NULL);
        if (run_cmd(cv.v, n, NULL, NULL, 0, NULL)) die("testsel_history: compiling %s failed", SRC[i]);
    }
    char *mk2 = xasprintf("make -s -C '%s' 'OUT=%s/fncov' 'CFLAGS=%s -finstrument-functions-once -include %s/fncov_rt.h' all '%s/fncov/play' "
                          "'%s/fncov/play-dev' 2>&1",
                          n, o, CF, TOOLS, o, o);
    char *mv[] = {"sh", "-c", mk2, NULL};
    out.n = 0;
    if (run_cmd(mv, NULL, NULL, &out, 0, NULL)) {
        logmsg("%.10s coverage build failed: %s", x, tail(sb_str(&out), 2000));
        return 0;
    }
    touch(bm);
    logmsg("%.10s built in %.0f s", x, now_secs() - t0);
    return 1;
}

/* make test's jobs for tree x, as this lane's Makefile lists them */
static char *order(const char *x)
{
    char *p = wx(x, "order");
    if (!path_exists(p)) {
        char *c = xasprintf("make -s -f '%s/csrc/Makefile' -C '%s/csrc' PLAYCHECK=0 --eval 'porder: ; @t=$$(mktemp -d); "
                            "$(SUITE_JOBS); cp $$t/order %s; rm -rf $$t' porder",
                            LANE, tree(x), p);
        if (sh(c, NULL)) die("testsel_history: listing the jobs of %s failed", x);
    }
    return p;
}

/* every job run on x's coverage build: records (store) and logs (runs) */
static int mapped(const char *x)
{
    char *mp = wx(x, "mapped");
    if (path_exists(mp)) return 1;
    if (!build(x)) return 0;
    char *t = tree(x), *o = xasprintf("%s/out/native", t), *store = wx(x, "store"), *runs = wx(x, "runs");
    rmtree(store);
    rmtree(runs);
    double t0 = now_secs();
    /* the jobs' PATH: this one's */
    const char *path = getenv("PATH");
    char *tm = path_join(TOOLS, "testmap"), *data = xasprintf("%s/out/java", t), *fncov = xasprintf("%s/fncov", o);
    char *jobs = xasprintf("%d", A_jobs), *ord = order(x), *nat = xasprintf("%s/csrc", t);
    char *av[] = {tm, "--root", t, "--out", o, "--data", data, "--global", G, "--store", store, "--fncov", fncov, "--order", ord,
                  "--jobs", jobs, "--path", (char *)(path ? path : ""), "--fnkey", FNKEY, "--logs", runs, NULL};
    struct sb out = {0};
    /* stdout and stderr together */
    int p[2];
    if (pipe(p)) die("pipe");
    fflush(stdout);
    pid_t pid = fork();
    if (!pid) {
        if (chdir(nat)) _exit(127);
        dup2(p[1], 1);
        dup2(p[1], 2);
        close(p[0]);
        close(p[1]);
        execvp(av[0], av);
        _exit(127);
    }
    close(p[1]);
    char buf[65536];
    ssize_t k;
    while ((k = read(p[0], buf, sizeof buf)) > 0) sb_putn(&out, buf, (size_t)k);
    close(p[0]);
    int st;
    waitpid(pid, &st, 0);
    char *ml = wx(x, "map.log");
    write_file(ml, sb_str(&out), out.n);
    if (wait_rc(st)) {
        logmsg("%.10s test-map failed: %s", x, tail(sb_str(&out), 2000));
        return 0;
    }
    touch(mp);
    char *s = strip((char *)sb_str(&out)), *last = strrchr(s, '\n');
    logmsg("%.10s mapped in %.0f s: %s", x, now_secs() - t0, last ? last + 1 : s);
    return 1;
}

static char *jobline(char **f, int n, const char *data)
{
    const char *d = f[1];
    if (strcmp(d, "-")) d = strlen(d) > strlen(data) ? d + strlen(data) + 1 : "";
    struct sb b = {0};
    sb_printf(&b, "%s %s", base_name(f[0]), d);
    for (int i = 2; i < n; ++i) sb_printf(&b, " %s", f[i]);
    return (char *)sb_str(&b);
}

static char *jid(const char *line)
{
    char hex[41];
    sha1_of(line, strlen(line) + 1, hex);
    return xstrndup(hex, 16);
}

static int Wc(unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; }
static int AN(unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }

/* rc and log of the job on x, x's tree path taken out, or NULL if not run */
static char *outcome(const char *x, const char *line, char **rc)
{
    char *id = jid(line), *b = wx(x, "runs"), *rp = xasprintf("%s/%s.rc", b, id), *lp = xasprintf("%s/%s.log", b, id);
    char *r = read_file(rp, NULL), *text = r ? read_file(lp, NULL) : NULL;
    if (!r || !text) return NULL;
    *rc = strip(r);
    /* text.replace(tree, '@') */
    char *t = tree(x);
    struct sb a = {0};
    size_t tl = strlen(t);
    for (const char *p = text; *p;) {
        const char *q = tl ? strstr(p, t) : NULL;
        if (!q) { sb_puts(&a, p); break; }
        sb_putn(&a, p, (size_t)(q - p));
        sb_putc(&a, '@');
        p = q + tl;
    }
    /* (framejudge|guijudge|tmp)\.[A-Za-z0-9]{6,} -> \1.X */
    const char *s = sb_str(&a);
    size_t n = a.n;
    struct sb c = {0};
    for (size_t i = 0; i < n;) {
        static const char *const P[3] = {"framejudge", "guijudge", "tmp"};
        int done = 0;
        for (int k = 0; k < 3 && !done; ++k) {
            size_t pl = strlen(P[k]);
            if (i + pl + 1 > n || memcmp(s + i, P[k], pl) || s[i + pl] != '.') continue;
            size_t j = i + pl + 1;
            while (j < n && AN((unsigned char)s[j])) ++j;
            if (j - (i + pl + 1) < 6) continue;
            sb_putn(&c, s + i, pl);
            sb_puts(&c, ".X");
            i = j;
            done = 1;
        }
        if (!done) sb_putc(&c, s[i++]);
    }
    /* \b\d+(\.\d+)? ?(s|ms|sec|seconds)\b -> T s */
    s = sb_str(&c), n = c.n;
    struct sb d = {0};
    for (size_t i = 0; i < n;) {
        size_t j = i;
        int ok = 0;
        if (s[i] >= '0' && s[i] <= '9' && (i == 0 || !Wc((unsigned char)s[i - 1]))) {
            while (j < n && s[j] >= '0' && s[j] <= '9') ++j;
            size_t k = j;
            for (int frac = 1; frac >= 0 && !ok; --frac) {
                size_t m = j;
                if (frac) {
                    if (m + 1 < n && s[m] == '.' && s[m + 1] >= '0' && s[m + 1] <= '9') {
                        ++m;
                        while (m < n && s[m] >= '0' && s[m] <= '9') ++m;
                    } else continue;
                }
                for (int sp = 1; sp >= 0 && !ok; --sp) {
                    size_t q = m;
                    if (sp) {
                        if (q < n && s[q] == ' ') ++q;
                        else continue;
                    }
                    static const char *const U[4] = {"s", "ms", "sec", "seconds"};
                    for (int u = 0; u < 4 && !ok; ++u) {
                        size_t ul = strlen(U[u]);
                        if (q + ul <= n && !memcmp(s + q, U[u], ul) && (q + ul == n || !Wc((unsigned char)s[q + ul]))) {
                            ok = 1;
                            k = q + ul;
                        }
                    }
                }
            }
            if (ok) {
                sb_puts(&d, "T s");
                i = k;
                continue;
            }
        }
        sb_putc(&d, s[i++]);
    }
    sb_free(&a);
    sb_free(&c);
    return (char *)sb_str(&d);
}

struct cmpres {
    int n, sel;
    struct sv changed, misses, bad;
};

static struct cmpres compare(const char *m, const char *p)
{
    struct cmpres R = {0};
    char *om = order(m), *tm = tree(m), *data = xasprintf("%s/out/java", tm), *on = xasprintf("%s/out/native", tm);
    char *text = read_file(om, NULL);
    struct sv ns = {0}, lines = {0};
    struct smap seen = {0};
    for (char *l = text; l && *l;) {
        char *e = strchr(l, '\n');
        if (e) *e = 0;
        char *f[256] = {0};
        int nf = split_ws(l, f, 256);
        if (nf) {
            /* lines[f[0]] = jobline(...): a repeated number keeps its first place, the last line */
            char *jl = jobline(f + 1, nf - 1, data);
            int x = smap_find(&seen, f[0]);
            if (x < 0) {
                smap_put(&seen, f[0], (void *)(intptr_t)ns.n);
                sv_push(&ns, f[0]);
                sv_push(&lines, jl);
            } else lines.v[(intptr_t)seen.vals[x]] = jl;
        }
        if (!e) break;
        l = e + 1;
    }
    char *ordtext = read_file(om, NULL);
    char *store = wx(p, "store");
    char *av[] = {FNKEY, "check", "--root", tm, "--out", on, "--data", data, "--global", G, "--store", store, NULL};
    struct sb out = {0};
    run_cmd(av, NULL, NULL, &out, 0, ordtext ? ordtext : "");
    struct smap hits = {0};
    for (char *l = (char *)sb_str(&out); *l;) {
        char *e = strchr(l, '\n');
        if (e) *e = 0;
        char *f[2] = {0};
        if (split_ws(l, f, 1)) smap_add(&hits, f[0]);
        if (!e) break;
        l = e + 1;
    }
    for (int i = 0; i < ns.n; ++i) {
        char *ra = NULL, *rb = NULL;
        char *a = outcome(p, lines.v[i], &ra), *b = outcome(m, lines.v[i], &rb);
        int ch = !a || !b || strcmp(ra, rb) || strcmp(a, b);
        if (ch) {
            sv_push(&R.changed, lines.v[i]);
            if (smap_has(&hits, ns.v[i])) sv_push(&R.misses, lines.v[i]);
        } else if (smap_has(&hits, ns.v[i]) && strcmp(ra, "0")) sv_push(&R.bad, lines.v[i]);
    }
    R.n = ns.n;
    R.sel = ns.n - hits.n;
    return R;
}

static int in_sv(const struct sv *a, const char *s)
{
    for (int i = 0; i < a->n; ++i)
        if (!strcmp(a->v[i], s)) return 1;
    return 0;
}

static void copy_to(const char *src, const char *dir)
{
    size_t n;
    char *d = read_file(src, &n);
    if (!d) die("testsel_history: cannot read %s", src);
    char *dst = path_join(dir, base_name(src));
    struct stat st;
    stat(src, &st);
    if (write_file(dst, d, n)) die("testsel_history: cannot write %s", dst);
    chmod(dst, st.st_mode & 0777);
    free(dst), free(d);
}

int main(int argc, char **argv)
{
    char *home = getenv("HOME");
    snprintf(W, sizeof W, "%s/dev/nw/.tmp/testsel-history", home ? home : ".");
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (!strcmp(a, "--merges") && i + 1 < argc) A_merges = atoi(argv[++i]);
        else if (!strcmp(a, "--work") && i + 1 < argc) snprintf(W, sizeof W, "%s", argv[++i]);
        else if (!strcmp(a, "--jobs") && i + 1 < argc) A_jobs = atoi(argv[++i]);
        else if (!strcmp(a, "--only")) {
            while (i + 1 < argc && argv[i + 1][0] != '-') sv_push(&A_only, argv[++i]);
        } else die("usage: testsel_history [--merges 30] [--work DIR] [--jobs N] [--only SHA...]");
    }
    /* the checkout: the repository holding the current directory */
    char cwd[4096];
    if (!getcwd(cwd, sizeof cwd)) die("getcwd");
    for (;;) {
        char *a = xasprintf("%s/csrc/tests/fnkey.c", cwd);
        int ok = is_file(a);
        free(a);
        if (ok) break;
        char *s = strrchr(cwd, '/');
        if (!s || s == cwd) die("testsel_history: run it inside the repository");
        *s = 0;
    }
    snprintf(LANE, sizeof LANE, "%s", cwd);
    snprintf(G, sizeof G, "history %s", CF);
    snprintf(DATA, sizeof DATA, "%s/out/java", LANE);
    snprintf(TOOLS, sizeof TOOLS, "%s/tools", W);
    snprintf(FNKEY, sizeof FNKEY, "%s/fnkey", TOOLS);
    mkdirs(W);
    /* the tools as they were when the run started: fnkey, testmap, memslot.sh
     * and the hook, copied to WORK/tools (a resumed run keeps its copies) */
    if (!is_dir(TOOLS)) {
        char *tmp = xasprintf("%s.tmp", TOOLS);
        mkdirs(tmp);
        char *t1 = xasprintf("%s/out/native/testmap", LANE), *t2 = xasprintf("%s/csrc/tests/memslot.sh", LANE);
        char *t3 = xasprintf("%s/csrc/tests/fncov_rt.h", LANE), *t4 = xasprintf("%s/out/native/fnkey", LANE);
        copy_to(t1, tmp), copy_to(t2, tmp), copy_to(t3, tmp), copy_to(t4, tmp);
        if (rename(tmp, TOOLS)) die("testsel_history: cannot make %s", TOOLS);
    }
    char *gl = xasprintf("git -C '%s' log --merges --first-parent master --format='%%H %%P %%s'", LANE);
    char *glv[] = {"sh", "-c", gl, NULL};
    struct sb lo = {0};
    if (run_cmd(glv, NULL, NULL, &lo, 0, NULL)) die("testsel_history: git log failed");
    struct sv ml = {0};
    for (char *l = (char *)sb_str(&lo); *l && ml.n < A_merges;) {
        char *e = strchr(l, '\n');
        if (e) *e = 0;
        sv_push(&ml, l);
        if (!e) break;
        l = e + 1;
    }
    int nm = ml.n;
    char **M = xcalloc((size_t)nm + 1, sizeof *M), **P = xcalloc((size_t)nm + 1, sizeof *P), **SUBJ = xcalloc((size_t)nm + 1, sizeof *SUBJ);
    for (int i = 0; i < nm; ++i) { /* reversed: oldest first */
        char *l = ml.v[nm - 1 - i];
        char *s1 = strchr(l, ' '), *s2 = s1 ? strchr(s1 + 1, ' ') : NULL, *s3 = s2 ? strchr(s2 + 1, ' ') : NULL;
        if (!s1 || !s2 || !s3) die("testsel_history: a merge line I cannot read: %s", l);
        M[i] = xstrndup(l, (size_t)(s1 - l));
        P[i] = xstrndup(s1 + 1, (size_t)(s2 - s1 - 1));
        SUBJ[i] = s3 + 1;
    }
    struct smap done = {0}, need = {0};
    char *res = path_join(W, "results.tsv");
    char *rt = read_file(res, NULL);
    for (char *l = rt; l && *l;) {
        char *e = strchr(l, '\n');
        if (e) *e = 0;
        char *tab = strchr(l, '\t');
        if (tab) *tab = 0;
        smap_add(&done, l);
        if (!e) break;
        l = e + 1;
    }
    for (int i = 0; i < nm; ++i) {
        int x = smap_add(&need, P[i]);
        need.vals[x] = (void *)((intptr_t)need.vals[x] + 1);
        x = smap_add(&need, M[i]);
        need.vals[x] = (void *)((intptr_t)need.vals[x] + 1);
    }
    for (int i = 0; i < nm; ++i) {
        const char *m = M[i], *p = P[i];
        if (A_only.n) {
            int any = 0;
            for (int k = 0; k < A_only.n; ++k) any |= starts_with(m, A_only.v[k]);
            if (!any) continue;
        }
        if (!smap_has(&done, m)) {
            if (!mapped(p) || !mapped(m)) {
                logmsg("%.10s: skipped (build or map failed)", m);
                continue;
            }
            if (!build(m)) continue;
            double t0 = now_secs();
            struct cmpres R = compare(m, p);
            /* re.sub(r'^merge (lane/\S+):.*', r'\1', subj) */
            char *lane = xstrdup(SUBJ[i]);
            if (starts_with(lane, "merge lane/")) {
                char *q = lane + 6;
                while (*q && !is_space((unsigned char)*q)) ++q;
                /* \S+ greedy, then backtracking to the last ':' inside it */
                char *colon = NULL;
                for (char *r = lane + 11; r < q + 1 && *r; ++r)
                    if (*r == ':' && r > lane + 11) colon = r;
                if (colon && (colon < q || *colon == ':')) {
                    char *nl = xstrndup(lane + 6, (size_t)(colon - lane - 6));
                    free(lane);
                    lane = nl;
                }
            }
            FILE *f = fopen(res, "a");
            if (!f) die("testsel_history: cannot write %s", res);
            fprintf(f, "%s\t%s\t%d\t%d\t%.3f\t%d\t%d\t%d\n", m, lane, R.n, R.sel, (double)R.sel / R.n, R.changed.n, R.misses.n, R.bad.n);
            fclose(f);
            char *cp = wx(m, "changed.txt");
            f = fopen(cp, "w");
            for (int k = 0; f && k < R.changed.n; ++k) fprintf(f, "%s%s\n", in_sv(&R.misses, R.changed.v[k]) ? "MISS " : "", R.changed.v[k]);
            if (f) fclose(f);
            logmsg("%.10s %s: %d jobs, %d selected (%.1f%%), %d changed, %d misses, %d hits on a failing parent (%.0f s)", m, lane, R.n,
                   R.sel, 100.0 * R.sel / R.n, R.changed.n, R.misses.n, R.bad.n, now_secs() - t0);
        }
        /* a commit's builds go once no later merge needs them (its records and logs stay) */
        const char *xs[2] = {p, m};
        for (int k = 0; k < 2; ++k) {
            int x = smap_find(&need, xs[k]);
            need.vals[x] = (void *)((intptr_t)need.vals[x] - 1);
            if (!(intptr_t)need.vals[x]) {
                rmtree(tree(xs[k]));
                unlink(wx(xs[k], "built"));
            }
        }
    }
    int nt = 0, misses = 0, allzero = 1;
    double mn = 0, mx = 0;
    rt = read_file(res, NULL);
    for (char *l = rt; l && *l;) {
        char *e = strchr(l, '\n');
        if (e) *e = 0;
        char *f[16] = {0};
        int k = 0;
        for (char *q = l; k < 16;) {
            f[k++] = q;
            char *tab = strchr(q, '\t');
            if (!tab) break;
            *tab = 0;
            q = tab + 1;
        }
        if (k < 7) die("testsel_history: a results line I cannot read");
        double fr = strtod(f[4], NULL);
        mn = nt ? (fr < mn ? fr : mn) : fr;
        mx = nt ? (fr > mx ? fr : mx) : fr;
        misses += atoi(f[6]);
        allzero &= !strcmp(f[6], "0");
        ++nt;
        if (!e) break;
        l = e + 1;
    }
    logmsg("history: %d merges, %d misses, selected fraction %.3f to %.3f", nt, misses, mn, mx);
    return allzero ? 0 : 1;
}
