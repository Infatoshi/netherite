/* Choose make smoke's jobs (tests/smoke.list) from measurements.
 *
 *     out/native/smoke_select [--stage all|phase|cover|time|select]
 *         [--jobs N] [--budget S] [--mem-budget MBS] [--max-job S] [--depth ROWS]
 *         [--min-rate R] [--cover-jobs N] [--fresh]
 *
 * Run from csrc/ (make smoke-select does). Three measurements, cached under
 * out/native/smoke/ and redone only for recordings or jobs whose files changed
 * (--fresh redoes all):
 *
 * 1. phase: test_snapshots --phase-check over every snapshot recording, the
 *    (phase, state class) pairs it wrote and the row each was first written in
 *    (engine/phase.c's "phase wrote: P C N from row R").
 * 2. cover: every job of make test (the same enumeration: each test_X over each
 *    directory of its data, or once with none), plus each recording cut at
 *    --rows 300, 1500 and 5000 and the long case tests cut short (LIMITS), run
 *    on a --coverage build (out/native/cov) with its own GCOV_PREFIX; gcov turns
 *    each into the set of engine/ lines it ran. A job is measured again when its
 *    directory's files change; after code changes that move what the tests
 *    reach, --fresh.
 * 2b. time: every candidate once more on the normal build (out/native), its
 *    wall seconds the selection's cost.
 * 3. select, greedy by cost: first one job for every test binary, then every
 *    (phase, class) pair any recording wrote (a recording cut at a pair's first
 *    row covers it), then the jobs that add the most engine/ lines the full suite
 *    runs per second, while the total stays under --budget and no job over
 *    --max-job; and one replay that reaches --depth rows. Writes
 *    tests/smoke.list: the jobs that reserve memory first, then the rest, each
 *    longest first.
 *
 * The C port of tests/smoke_select.py (lane/cport): the same cache files (keys,
 * records) and the same selection. */
#define _GNU_SOURCE
#include <zlib.h>
#include "../engine/tape.h"
#include "tool.h"

static char ROOT[4096], NATIVE[4200], DATA[4200], OUT[4200], COV[4300], WORK[4300], LIST[4300];

static const char *const DATA_MAP[][2] = {{"worldgen", "chunks"}, {"probe", "probes"}, {"structures", "structures"},
                                          {"move", "moves"}, {"feature", "probes"}};
static const int CUTS[3] = {300, 1500, 5000};
/* the long case tests' own early stops (--cases N) and test_interleave's --rows */
static const struct {
    const char *test, *flag;
    int n[3], k;
} LIMITS[] = {{"explosions", "--cases", {300, 1000}, 2}, {"ticks", "--cases", {2000, 6000}, 2},
              {"harvestdrops", "--cases", {1000, 3000}, 2}, {"paths", "--cases", {2000, 6000}, 2},
              {"itemuse", "--cases", {1000, 3000}, 2}, {"interleave", "--rows", {100, 300, 1000}, 3}};

static int opt_jobs = 48, opt_cover_jobs = 8, opt_fresh = 0;
static long long opt_depth = 12000;
static double opt_budget = 300.0, opt_mem_budget = 15000.0, opt_max_job = 8.0, opt_min_rate = 1.0;

/* ------------------------------------------------------------ jobs */

struct job {
    char *test, *dir; /* dir: relative to out/java, or NULL */
    char *flag, *val; /* the extra flag and its value, or NULL */
};

static struct sv tests(void)
{
    char *d = xasprintf("%s/tests", NATIVE);
    struct sv all = list_dir(d, 0), out = {0};
    for (int i = 0; i < all.n; ++i)
        if (starts_with(all.v[i], "test_") && ends_with(all.v[i], ".c")) sv_push(&out, xstrndup(all.v[i] + 5, strlen(all.v[i]) - 7));
    sv_sort(&out);
    free(d);
    return out;
}

static int cmp_slash(const void *a, const void *b)
{
    char *x = xasprintf("%s/", *(char *const *)a), *y = xasprintf("%s/", *(char *const *)b);
    int c = strcmp(x, y);
    free(x), free(y);
    return c;
}

/* make test's jobs, as (test, directory relative to out/java or NULL) */
static struct job *suite_jobs(int *n)
{
    struct sv ts = tests();
    struct job *jobs = NULL;
    int nj = 0;
    for (int i = 0; i < ts.n; ++i) {
        const char *x = ts.v[i], *dd = x;
        for (int k = 0; k < 5; ++k)
            if (!strcmp(DATA_MAP[k][0], x)) dd = DATA_MAP[k][1];
        char *d = xasprintf("%s/%s", DATA, dd);
        if (!is_dir(d)) {
            jobs = xrealloc(jobs, (size_t)(nj + 1) * sizeof *jobs);
            jobs[nj++] = (struct job){xstrdup(x), NULL, NULL, NULL};
            free(d);
            continue;
        }
        struct sv es = list_dir(d, 0), dirs = {0};
        for (int k = 0; k < es.n; ++k) {
            char *p = path_join(d, es.v[k]);
            if (is_dir(p)) sv_push(&dirs, es.v[k]);
            free(p);
        }
        if (dirs.n > 1) qsort(dirs.v, (size_t)dirs.n, sizeof *dirs.v, cmp_slash);
        for (int k = 0; k < dirs.n; ++k) {
            if (!strcmp(x, "feature") || !strcmp(x, "probe")) {
                char *mp = xasprintf("%s/%s/manifest.json", d, dirs.v[k]);
                char *text = read_file(mp, NULL);
                free(mp);
                const char *kind = "setblock";
                struct jval *v = text ? json_parse(text) : NULL;
                if (v && v->kind == J_OBJ) {
                    const struct jval *kv = json_get(v, "kind");
                    if (kv && kv->kind == J_STR && kv->str[0]) kind = kv->str;
                    else if (kv && kv->kind != J_NULL && kv->kind != J_STR && !(kv->kind == J_BOOL && !kv->boolean) &&
                             !(kv->kind == J_NUM && kv->dbl == 0))
                        kind = "?"; /* a truthy non-string kind: neither feature nor setblock */
                }
                int feat = !strcmp(kind, "feature");
                if (v) json_free(v);
                if ((strcmp(x, "feature") == 0) != feat) continue;
            }
            jobs = xrealloc(jobs, (size_t)(nj + 1) * sizeof *jobs);
            jobs[nj++] = (struct job){xstrdup(x), xasprintf("%s/%s", dd, dirs.v[k]), NULL, NULL};
        }
        free(d);
    }
    *n = nj;
    return jobs;
}

/* the recording's file list, sizes and mtimes (make test's TESTCACHE key) */
struct walkent {
    char *dp;
    struct sv files;
};

static int cmp_walk(const void *a, const void *b) { return strcmp(((const struct walkent *)a)->dp, ((const struct walkent *)b)->dp); }

static void walk(const char *dp, struct walkent **v, int *n)
{
    DIR *d = opendir(dp);
    if (!d) return;
    struct walkent w = {xstrdup(dp), {0}};
    struct sv subs = {0};
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char *p = path_join(dp, e->d_name);
        if (is_dir(p)) sv_push(&subs, xstrdup(e->d_name));
        else sv_push(&w.files, xstrdup(e->d_name));
        free(p);
    }
    closedir(d);
    *v = xrealloc(*v, (size_t)(*n + 1) * sizeof **v);
    (*v)[(*n)++] = w;
    for (int i = 0; i < subs.n; ++i) {
        char *p = path_join(dp, subs.v[i]);
        walk(p, v, n);
        free(p);
    }
}

static char *dir_key(const char *rel)
{
    struct sha1ctx h;
    sha1_init(&h);
    if (rel) {
        char *base = xasprintf("%s/%s", DATA, rel);
        struct walkent *v = NULL;
        int n = 0;
        walk(base, &v, &n);
        if (n > 1) qsort(v, (size_t)n, sizeof *v, cmp_walk);
        size_t bl = strlen(base);
        for (int i = 0; i < n; ++i) {
            sv_sort(&v[i].files);
            for (int k = 0; k < v[i].files.n; ++k) {
                char *p = path_join(v[i].dp, v[i].files.v[k]);
                struct stat st;
                if (!stat(p, &st)) {
                    char mt[64];
#ifdef __APPLE__
                    double m = (double)st.st_mtimespec.tv_sec + (double)st.st_mtimespec.tv_nsec * 1e-9;
#else
                    double m = (double)st.st_mtim.tv_sec + (double)st.st_mtim.tv_nsec * 1e-9;
#endif
                    char *line = xasprintf("%s %lld %s\n", p + bl + 1, (long long)st.st_size, py_repr(m, mt));
                    sha1_add(&h, line, strlen(line));
                    free(line);
                }
                free(p);
            }
        }
        free(base);
    }
    char hex[41];
    sha1_hex(&h, hex);
    return xstrndup(hex, 16);
}

static char *job_id(const struct job *j)
{
    char *s = j->flag ? xasprintf("%s-%s-%s-%s", j->test, j->dir ? j->dir : "none", j->flag, j->val)
                      : xasprintf("%s-%s", j->test, j->dir ? j->dir : "none");
    for (char *p = s; *p; ++p)
        if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '.' || *p == '_' || *p == '-')) *p = '_';
    return s;
}

/* ------------------------------------------------------------ running */

/* argv through tests/memslot.sh when the job has a directory */
static char **memslot(const char *rel, char **argv, const char *tfile)
{
    int n = 0;
    while (argv[n]) ++n;
    char **out = xcalloc((size_t)n + 8, sizeof *out);
    int k = 0;
    if (rel) {
        out[k++] = "bash";
        out[k++] = xasprintf("%s/tests/memslot.sh", NATIVE);
        if (tfile) out[k++] = "--t", out[k++] = (char *)tfile;
        out[k++] = xasprintf("%s/%s", DATA, rel);
    }
    for (int i = 0; i < n; ++i) out[k++] = argv[i];
    out[k] = NULL;
    return out;
}

static char **job_argv(const struct job *j, const char *bin)
{
    char **a = xcalloc(6, sizeof *a);
    int k = 0;
    a[k++] = (char *)bin;
    if (j->flag) a[k++] = j->flag, a[k++] = j->val;
    if (j->dir) a[k++] = xasprintf("%s/%s", DATA, j->dir);
    return a;
}

/* t0 from memslot's --t file (ns), if there is one */
static double acq_time(const char *tfile, double t0, int remove_it)
{
    char *s = read_file(tfile, NULL);
    if (s) {
        char *e;
        long long ns = strtoll(s, &e, 10);
        while (*e && is_space((unsigned char)*e)) ++e;
        if (e != s && !*e) {
            t0 = (double)ns / 1e9;
            if (remove_it) unlink(tfile);
        }
        free(s);
    }
    return t0;
}

/* 1. phase pairs */
static char *phase_one(const char *rec)
{
    char *out = xasprintf("%s/phase/%s.txt", WORK, rec);
    char *rel = xasprintf("snapshots/%s", rec);
    char *key = dir_key(rel);
    FILE *f = fopen(out, "r");
    if (f) {
        char line[256] = "";
        if (!fgets(line, sizeof line, f)) line[0] = 0;
        fclose(f);
        char *want = xasprintf("# %s", key);
        int hit = !strcmp(strip(line), want);
        free(want);
        if (hit) return xstrdup("cached");
    }
    char *d = xasprintf("%s/snapshots/%s", DATA, rec), *bin = xasprintf("%s/test_snapshots", OUT);
    char *a[] = {bin, "--phase-check", d, NULL};
    struct sb o = {0};
    int fd[2];
    if (pipe(fd)) die("pipe");
    fflush(stdout);
    pid_t pid = fork();
    if (!pid) {
        if (chdir(NATIVE)) _exit(127);
        dup2(fd[1], 1);
        dup2(fd[1], 2);
        close(fd[0]);
        close(fd[1]);
        char **av = memslot(rel, a, NULL);
        execvp(av[0], av);
        _exit(127);
    }
    close(fd[1]);
    char buf[65536];
    ssize_t k;
    while ((k = read(fd[0], buf, sizeof buf)) > 0) sb_putn(&o, buf, (size_t)k);
    close(fd[0]);
    int st;
    waitpid(pid, &st, 0);
    int rc = wait_rc(st);
    if (rc) return xasprintf("FAIL rc=%d", rc);
    struct sb w = {0};
    sb_printf(&w, "# %s\n", key);
    int first = 1;
    for (char *l = (char *)sb_str(&o); *l;) {
        char *e = strchr(l, '\n');
        if (e) *e = 0;
        size_t n = strlen(l);
        if (n && l[n - 1] == '\r') l[--n] = 0;
        if (starts_with(l, "phase wrote:") || starts_with(l, "phase trace:")) {
            if (!first) sb_putc(&w, '\n');
            sb_puts(&w, l);
            first = 0;
        }
        if (!e) break;
        l = e + 1;
    }
    sb_putc(&w, '\n');
    char *tmp = xasprintf("%s.tmp", out);
    if (write_file(tmp, w.s, w.n) || rename(tmp, out)) die("smoke_select: cannot write %s", out);
    return xstrdup("ran");
}

struct pair {
    char *p, *c;
    long long row;
};

struct phase {
    struct pair *v;
    int n;
    long long rows;
};

/* {(phase, class): first row}, rows */
static struct phase phase_read(const char *rec)
{
    struct phase ph = {0};
    char *p = xasprintf("%s/phase/%s.txt", WORK, rec);
    char *text = read_file(p, NULL);
    if (!text) die("smoke_select: no phase record %s (run --stage phase)", p);
    for (char *l = text; *l;) {
        char *e = strchr(l, '\n');
        if (e) *e = 0;
        /* phase wrote: (\S+) (\S+) (\d+) from row (\d+) */
        if (starts_with(l, "phase wrote: ")) {
            char *q = l + 13, *t1 = q;
            while (*q && !is_space((unsigned char)*q)) ++q;
            char *t1e = q;
            if (t1e > t1 && *q == ' ') {
                char *t2 = ++q;
                while (*q && !is_space((unsigned char)*q)) ++q;
                char *t2e = q;
                if (t2e > t2 && *q == ' ') {
                    char *d = ++q;
                    while (*q >= '0' && *q <= '9') ++q;
                    if (q > d && starts_with(q, " from row ") && q[10] >= '0' && q[10] <= '9') {
                        long long row = strtoll(q + 10, NULL, 10);
                        char *a = xstrndup(t1, (size_t)(t1e - t1)), *b = xstrndup(t2, (size_t)(t2e - t2));
                        int k;
                        for (k = 0; k < ph.n; ++k)
                            if (!strcmp(ph.v[k].p, a) && !strcmp(ph.v[k].c, b)) break;
                        if (k == ph.n) {
                            ph.v = xrealloc(ph.v, (size_t)(ph.n + 1) * sizeof *ph.v);
                            ph.v[ph.n++] = (struct pair){a, b, 0};
                        } else free(a), free(b);
                        ph.v[k].row = row;
                    }
                }
            }
        }
        /* phase trace: (\d+) rows */
        if (starts_with(l, "phase trace: ") && l[13] >= '0' && l[13] <= '9') {
            char *q = l + 13;
            long long rows = strtoll(q, &q, 10);
            if (starts_with(q, " rows")) ph.rows = rows;
        }
        if (!e) break;
        l = e + 1;
    }
    free(text);
    free(p);
    return ph;
}

/* 2. coverage */
static int strip_count(void)
{
    char *p = xasprintf("%s/obj", COV);
    int n = 0;
    for (char *s = p; *s;) {
        while (*s == '/') ++s;
        if (!*s) break;
        ++n;
        while (*s && *s != '/') ++s;
    }
    free(p);
    return n;
}

static int cmp_ll(const void *a, const void *b)
{
    long long x = *(const long long *)a, y = *(const long long *)b;
    return x < y ? -1 : x > y;
}

static char *gz_read(const char *path)
{
    gzFile g = gzopen(path, "rb");
    if (!g) return NULL;
    struct sb b = {0};
    char buf[65536];
    int k;
    while ((k = gzread(g, buf, sizeof buf)) > 0) sb_putn(&b, buf, (size_t)k);
    int err;
    gzerror(g, &err);
    gzclose(g);
    if (k < 0) { sb_free(&b); return NULL; }
    return (char *)sb_str(&b);
}

static struct jval *read_cov(const struct job *j)
{
    char *id = job_id(j), *p = xasprintf("%s/cov/%s.json.gz", WORK, id);
    char *t = gz_read(p);
    free(id), free(p);
    return t ? json_parse(t) : NULL;
}

static char *cover_one(const struct job *j)
{
    char *jid = job_id(j), *out = xasprintf("%s/cov/%s.json.gz", WORK, jid);
    char *bin = xasprintf("%s/test_%s", COV, j->test);
    char *key = dir_key(j->dir);
    if (path_exists(out)) {
        struct jval *v = read_cov(j);
        const struct jval *k = v ? json_get(v, "key") : NULL;
        int hit = k && k->kind == J_STR && !strcmp(k->str, key);
        if (v) json_free(v);
        if (hit) return xstrdup("cached");
    }
    char *g = xasprintf("%s/gcda/%s", WORK, jid);
    char *rm[] = {"rm", "-rf", g, NULL};
    run_cmd(rm, NULL, NULL, NULL, 0, NULL);
    mkdirs(g);
    char *e1 = xasprintf("GCOV_PREFIX=%s", g), *e2 = xasprintf("GCOV_PREFIX_STRIP=%d", strip_count());
    char *env[] = {e1, e2, NULL};
    char *tfile = xasprintf("%s.acq", g);
    double t0 = now_secs();
    char **av = memslot(j->dir, job_argv(j, bin), tfile);
    int rc = run_cmd(av, NATIVE, env, NULL, 1, NULL);
    double t1 = now_secs();
    t0 = acq_time(tfile, t0, 0);
    struct sv all = {0}, gcdas = {0};
    walk_tree(g, "", 1, &all, 0);
    for (int i = 0; i < all.n; ++i)
        if (ends_with(all.v[i], ".gcda") && !starts_with(base_name(all.v[i]), "test_")) sv_push(&gcdas, path_join(g, all.v[i]));
    for (int i = 0; i < gcdas.n; ++i) {
        const char *b = base_name(gcdas.v[i]);
        char *src = xasprintf("%s/obj/%.*s.gcno", COV, (int)(strlen(b) - 5), b);
        char *dst = xasprintf("%.*s.gcno", (int)(strlen(gcdas.v[i]) - 5), gcdas.v[i]);
        if (symlink(src, dst)) {}
        free(src), free(dst);
    }
    /* gcov's output (about 12 MB a job) is one JSON object per object file, a line each */
    struct smap lines = {0}; /* file -> struct { long long *v; int n, cap; } */
    struct lset {
        long long *v;
        int n, cap;
    };
    if (gcdas.n) {
        char **gv = xcalloc((size_t)gcdas.n + 6, sizeof *gv);
        int k = 0;
        gv[k++] = "gcov";
        gv[k++] = "--json-format";
        gv[k++] = "--stdout";
        gv[k++] = "-o";
        gv[k++] = dir_name(gcdas.v[0]);
        for (int i = 0; i < gcdas.n; ++i) gv[k++] = gcdas.v[i];
        struct sb o = {0};
        run_cmd(gv, NATIVE, NULL, &o, 1, NULL);
        for (char *l = (char *)sb_str(&o); *l;) {
            char *e = strchr(l, '\n');
            size_t n = e ? (size_t)(e - l) : strlen(l);
            size_t a = 0;
            while (a < n && is_space((unsigned char)l[a])) ++a;
            if (a < n) {
                struct jval *v = json_parse(xstrndup(l + a, n - a));
                if (!v) die("smoke_select: gcov printed what is not JSON");
                const struct jval *files = json_get(v, "files");
                for (int fi = 0; files && fi < files->nitems; ++fi) {
                    const struct jval *fo = files->items[fi], *fn = json_get(fo, "file"), *ls = json_get(fo, "lines");
                    if (!fn || fn->kind != J_STR || !starts_with(fn->str, "engine/")) continue;
                    int x = smap_add(&lines, fn->str);
                    struct lset *s = lines.vals[x];
                    if (!s) s = lines.vals[x] = xcalloc(1, sizeof *s);
                    for (int li = 0; ls && li < ls->nitems; ++li) {
                        const struct jval *ln = ls->items[li], *num = json_get(ln, "line_number"), *cnt = json_get(ln, "count");
                        if (!num || !cnt || !(cnt->dbl > 0)) continue;
                        if (s->n == s->cap) s->cap = s->cap ? s->cap * 2 : 256, s->v = xrealloc(s->v, (size_t)s->cap * sizeof *s->v);
                        s->v[s->n++] = num->num;
                    }
                }
                json_free(v);
            }
            if (!e) break;
            l = e + 1;
        }
        sb_free(&o);
    }
    char *rm2[] = {"rm", "-rf", g, tfile, NULL};
    run_cmd(rm2, NULL, NULL, NULL, 0, NULL);
    /* the record, as json.dump writes it */
    struct sb r = {0};
    sb_puts(&r, "{\"key\": ");
    json_esc(&r, key);
    sb_puts(&r, ", \"test\": ");
    json_esc(&r, j->test);
    sb_puts(&r, ", \"dir\": ");
    if (j->dir) json_esc(&r, j->dir);
    else sb_puts(&r, "null");
    sb_puts(&r, ", \"flags\": [");
    if (j->flag) {
        json_esc(&r, j->flag);
        sb_puts(&r, ", ");
        json_esc(&r, j->val);
    }
    sb_printf(&r, "], \"rc\": %d, \"secs\": ", rc);
    json_num(&r, py_round(t1 - t0, 3));
    sb_puts(&r, ", \"lines\": {");
    struct sv fs = {0};
    for (int i = 0; i < lines.n; ++i)
        if (((struct lset *)lines.vals[i])->n) sv_push(&fs, lines.keys[i]);
    sv_sort(&fs);
    for (int i = 0; i < fs.n; ++i) {
        struct lset *s = smap_get(&lines, fs.v[i]);
        qsort(s->v, (size_t)s->n, sizeof *s->v, cmp_ll);
        sb_puts(&r, i ? ", " : "");
        json_esc(&r, fs.v[i]);
        sb_puts(&r, ": [");
        int first = 1;
        for (int a = 0; a < s->n;) {
            int b = a;
            while (b + 1 < s->n && (s->v[b + 1] == s->v[b] || s->v[b + 1] == s->v[b] + 1)) ++b;
            sb_printf(&r, "%s[%lld, %lld]", first ? "" : ", ", s->v[a], s->v[b]);
            first = 0;
            a = b + 1;
        }
        sb_puts(&r, "]");
    }
    sb_puts(&r, "}}");
    char *tmp = xasprintf("%s.tmp", out);
    gzFile gz = gzopen(tmp, "wb");
    if (!gz || gzwrite(gz, r.s, (unsigned)r.n) != (int)r.n || gzclose(gz) != Z_OK) die("smoke_select: cannot write %s", tmp);
    if (rename(tmp, out)) die("smoke_select: cannot write %s", out);
    return xasprintf("ran rc=%d %.1fs", rc, t1 - t0);
}

/* the job's seconds on the normal build (out/native), the selection's cost */
static char *time_one(const struct job *j)
{
    char *jid = job_id(j), *out = xasprintf("%s/time/%s.txt", WORK, jid);
    char *key = dir_key(j->dir);
    char *text = read_file(out, NULL);
    if (text) {
        char *f[3] = {0};
        int n = split_ws(text, f, 3);
        if (n == 2 && !strcmp(f[0], key)) return xstrdup("cached");
    }
    char *tfile = xasprintf("%s/time/%s.acq", WORK, jid), *bin = xasprintf("%s/test_%s", OUT, j->test);
    double t0 = now_secs();
    char **av = memslot(j->dir, job_argv(j, bin), tfile);
    int rc = run_cmd(av, NATIVE, NULL, NULL, 1, NULL);
    double t1 = now_secs();
    t0 = acq_time(tfile, t0, 1);
    if (rc) return xasprintf("FAIL rc=%d", rc);
    char *line = xasprintf("%s %.3f\n", key, t1 - t0);
    if (write_file(out, line, strlen(line))) die("smoke_select: cannot write %s", out);
    return xasprintf("ran %.1fs", t1 - t0);
}

static struct job *candidates(int *n)
{
    int nb;
    struct job *base = suite_jobs(&nb);
    struct job *jobs = xmalloc((size_t)(nb * 4 + 1) * sizeof *jobs);
    int nj = 0;
    for (int i = 0; i < nb; ++i) jobs[nj++] = base[i];
    for (int i = 0; i < nb; ++i) {
        struct job *b = &base[i];
        for (int k = 0; k < 6; ++k)
            if (!strcmp(LIMITS[k].test, b->test) && b->dir)
                for (int q = 0; q < LIMITS[k].k; ++q)
                    jobs[nj++] = (struct job){b->test, b->dir, (char *)LIMITS[k].flag, xasprintf("%d", LIMITS[k].n[q])};
        if (strcmp(b->test, "snapshots")) continue;
        char *d = xstrdup(b->dir);
        size_t dl = strlen(d);
        while (dl && d[dl - 1] == '/') d[--dl] = 0;
        struct phase ph = phase_read(base_name(d));
        free(d);
        for (int c = 0; c < 3; ++c)
            if ((double)ph.rows > CUTS[c] * 1.5) jobs[nj++] = (struct job){b->test, b->dir, "--rows", xasprintf("%d", CUTS[c])};
    }
    *n = nj;
    return jobs;
}

/* items run n at a time, each in a child process; the results in the items' order */
typedef char *(*job_fn)(const void *item);

static int pool(job_fn fn, const void *items, size_t isz, int n, int nproc, const char *label, struct sv *names)
{
    double t = now_secs();
    int done = 0, nbad = 0;
    pid_t *pid = xcalloc((size_t)n + 1, sizeof *pid);
    int *fd = xcalloc((size_t)n + 1, sizeof *fd);
    char **res = xcalloc((size_t)n + 1, sizeof *res);
    int next = 0, running = 0;
    struct sv badn = {0}, badw = {0};
    for (int i = 0; i < n; ++i) {
        while (next < n && running < nproc) {
            int p[2];
            if (pipe(p)) die("pipe");
            fflush(stdout);
            fflush(stderr);
            pid_t c = fork();
            if (!c) {
                close(p[0]);
                char *r = fn((const char *)items + (size_t)next * isz);
                size_t m = strlen(r);
                while (m) {
                    ssize_t w = write(p[1], r, m);
                    if (w <= 0) break;
                    r += w, m -= (size_t)w;
                }
                _exit(0);
            }
            close(p[1]);
            pid[next] = c, fd[next] = p[0];
            ++next, ++running;
        }
        struct sb r = {0};
        char buf[4096];
        ssize_t k;
        while ((k = read(fd[i], buf, sizeof buf)) > 0) sb_putn(&r, buf, (size_t)k);
        close(fd[i]);
        int st;
        waitpid(pid[i], &st, 0);
        --running;
        res[i] = r.n ? r.s : xstrdup("FAIL (the worker died)");
        ++done;
        const char *what = res[i];
        if (strstr(what, "FAIL") || (strstr(what, "rc=") && !strstr(what, "rc=0"))) {
            sv_push(&badn, names->v[i]);
            sv_push(&badw, res[i]);
            ++nbad;
        }
        if (done % 50 == 0 || done == n) {
            printf("%s: %d/%d %.0fs\n", label, done, n, now_secs() - t);
            fflush(stdout);
        }
    }
    for (int i = 0; i < badn.n; ++i) printf("%s: %s %s\n", label, badn.v[i], badw.v[i]);
    fflush(stdout);
    return nbad;
}

static char *phase_fn(const void *item) { return phase_one(*(char *const *)item); }
static char *cover_fn(const void *item) { return cover_one(item); }
static char *time_fn(const void *item) { return time_one(item); }

/* ------------------------------------------------------------ 3. selection */

struct peak {
    char *key;
    long long v[3];
    int n;
};

/* tests/memslot.sh's reservation for each (binary, directory): the largest of
 * its last three peaks, none under 100 MB */
static struct smap peaks(void)
{
    struct smap runs = {0}, out = {0};
    char *text = read_file("/tmp/netherite-mem/peaks", NULL);
    for (char *l = text; l && *l;) {
        char *e = strchr(l, '\n');
        if (e) *e = 0;
        char *f[4] = {0};
        int n = split_ws(l, f, 4);
        int digits = n >= 1 && *f[0];
        for (char *p = n ? f[0] : ""; digits && *p; ++p) digits = *p >= '0' && *p <= '9';
        if (n == 3 && digits) {
            char *k = xasprintf("%s %s", f[1], f[2]);
            int x = smap_add(&runs, k);
            struct peak *pk = runs.vals[x];
            if (!pk) pk = runs.vals[x] = xcalloc(1, sizeof *pk);
            if (pk->n == 3) pk->v[0] = pk->v[1], pk->v[1] = pk->v[2], pk->n = 2;
            pk->v[pk->n++] = atoll(f[0]);
            free(k);
        }
        if (!e) break;
        l = e + 1;
    }
    for (int i = 0; i < runs.n; ++i) {
        struct peak *pk = runs.vals[i];
        long long m = pk->v[0];
        for (int k = 1; k < pk->n; ++k)
            if (pk->v[k] > m) m = pk->v[k];
        if (m >= 100) smap_put(&out, runs.keys[i], (void *)(intptr_t)m);
    }
    return out;
}

/* engine/ lines as bits: file f's line n is bit base[f] + n */
static struct smap lbase;
static struct sv lfiles;
static long long *lfile_base, ltop;
static int nwords;

static void lines_assign(const struct jval *lines)
{
    for (int i = 0; lines && i < lines->nfields; ++i) {
        const char *f = lines->fields[i].key;
        if (smap_has(&lbase, f)) continue;
        char *p = xasprintf("%s/%s", NATIVE, f);
        char *text = read_file(p, NULL);
        long long n;
        if (text) {
            n = 0;
            size_t len = strlen(text);
            for (size_t k = 0; k < len; ++k) n += text[k] == '\n';
            if (len && text[len - 1] != '\n') ++n;
            n += 2;
            free(text);
        } else {
            long long mx = 0;
            const struct jval *rs = lines->fields[i].val;
            for (int k = 0; k < rs->nitems; ++k)
                if (rs->items[k]->items[1]->num > mx || k == 0) mx = rs->items[k]->items[1]->num;
            n = mx + 2;
        }
        free(p);
        smap_put(&lbase, f, (void *)(intptr_t)(lfiles.n));
        sv_push(&lfiles, xstrdup(f));
        lfile_base = xrealloc(lfile_base, (size_t)lfiles.n * sizeof *lfile_base);
        lfile_base[lfiles.n - 1] = ltop;
        ltop += n;
    }
}

static uint64_t *lines_mask(const struct jval *lines)
{
    uint64_t *m = xcalloc((size_t)nwords, sizeof *m);
    for (int i = 0; lines && i < lines->nfields; ++i) {
        long long base = lfile_base[(intptr_t)smap_get(&lbase, lines->fields[i].key)];
        const struct jval *rs = lines->fields[i].val;
        for (int k = 0; k < rs->nitems; ++k) {
            long long a = rs->items[k]->items[0]->num, b = rs->items[k]->items[1]->num;
            for (long long x = base + a; x <= base + b; ++x) m[x / 64] |= 1ULL << (x % 64);
        }
    }
    return m;
}

static int popc(const uint64_t *m)
{
    int n = 0;
    for (int i = 0; i < nwords; ++i) n += __builtin_popcountll(m[i]);
    return n;
}

static struct job *J;
static int NJ;
static double *secs, *mbs, *cost;
static uint64_t **mask;
static struct phase *rp; /* each full snapshots job's phase record, by job index */
static uint64_t *covered;

struct chosen {
    int j;
    char *why;
};
static struct chosen *ch;
static int nch;
static double tsec, tmbs;
static struct smap pairs; /* "phase class" taken */

static int same_job(int a, int b)
{
    return !strcmp(J[a].test, J[b].test) && ((!J[a].dir && !J[b].dir) || (J[a].dir && J[b].dir && !strcmp(J[a].dir, J[b].dir))) &&
           ((!J[a].flag && !J[b].flag) || (J[a].flag && J[b].flag && !strcmp(J[a].flag, J[b].flag) && !strcmp(J[a].val, J[b].val)));
}

static int same_rec(int a, int b)
{
    return !strcmp(J[a].test, J[b].test) && ((!J[a].dir && !J[b].dir) || (J[a].dir && J[b].dir && !strcmp(J[a].dir, J[b].dir)));
}

/* the full job of a snapshots job's recording */
static int full_of(int j)
{
    for (int i = 0; i < NJ; ++i)
        if (!J[i].flag && same_rec(i, j)) return i;
    return -1;
}

/* the (phase, class) pairs a job covers, as "P C" keys */
static int pairs_of(int j, struct sv *out)
{
    out->n = 0;
    if (strcmp(J[j].test, "snapshots")) return 0;
    struct phase *ph = &rp[full_of(j)];
    long long n = J[j].flag ? atoll(J[j].val) : ph->rows + 1;
    for (int i = 0; i < ph->n; ++i)
        if (ph->v[i].row < n) sv_push(out, xasprintf("%s %s", ph->v[i].p, ph->v[i].c));
    return out->n;
}

static int new_pairs(int j)
{
    struct sv s = {0};
    pairs_of(j, &s);
    int n = 0;
    for (int i = 0; i < s.n; ++i) n += !smap_has(&pairs, s.v[i]), free(s.v[i]);
    free(s.v);
    return n;
}

static void take(int j, const char *why)
{
    /* a recording already chosen at a shorter cut is replaced by the longer one */
    for (int i = 0; i < nch;) {
        if (same_rec(ch[i].j, j)) {
            tsec -= secs[ch[i].j];
            tmbs -= mbs[ch[i].j];
            memmove(ch + i, ch + i + 1, (size_t)(nch - i - 1) * sizeof *ch);
            --nch;
        } else ++i;
    }
    ch = xrealloc(ch, (size_t)(nch + 1) * sizeof *ch);
    ch[nch++] = (struct chosen){j, xstrdup(why)};
    tsec += secs[j];
    tmbs += mbs[j];
    for (int w = 0; w < nwords; ++w) covered[w] |= mask[j][w];
    struct sv s = {0};
    pairs_of(j, &s);
    for (int i = 0; i < s.n; ++i) smap_add(&pairs, s.v[i]);
}

static int gain(int j)
{
    int n = 0;
    for (int w = 0; w < nwords; ++w) n += __builtin_popcountll(mask[j][w] & ~covered[w]);
    return n;
}

static long long reach(int j)
{
    struct phase *ph = &rp[full_of(j)];
    return J[j].flag ? atoll(J[j].val) : ph->rows;
}

static int chosen_has(int j)
{
    for (int i = 0; i < nch; ++i)
        if (same_job(ch[i].j, j)) return 1;
    return 0;
}

static int cmp_left_n;
struct leftent {
    int file, count;
};

static void selection(void)
{
    J = candidates(&NJ);
    struct smap mem = peaks();
    secs = xcalloc((size_t)NJ, sizeof *secs);
    mbs = xcalloc((size_t)NJ, sizeof *mbs);
    cost = xcalloc((size_t)NJ, sizeof *cost);
    mask = xcalloc((size_t)NJ, sizeof *mask);
    struct jval **recs = xcalloc((size_t)NJ, sizeof *recs);
    for (int j = 0; j < NJ; ++j) {
        recs[j] = read_cov(&J[j]);
        char *id = job_id(&J[j]);
        if (!recs[j]) die("smoke_select: no coverage record for %s (run --stage cover)", id);
        const struct jval *rc = json_get(recs[j], "rc");
        if (!rc || rc->num != 0) {
            fprintf(stderr, "select: %s failed under coverage (rc %s); fix the tree first\n", id, rc ? json_raw(rc) : "None");
            exit(1);
        }
        char *dir = xstrdup(J[j].dir ? J[j].dir : "-");
        size_t dl = strlen(dir);
        while (dl && dir[dl - 1] == '/') dir[--dl] = 0;
        char *mk = xasprintf("test_%s %s", J[j].test, base_name(dir));
        long long mb = (long long)(intptr_t)smap_get(&mem, mk);
        char *tp = xasprintf("%s/time/%s.txt", WORK, id);
        char *tt = read_file(tp, NULL);
        if (!tt) die("smoke_select: no time record %s (run --stage time)", tp);
        char *f[3] = {0};
        if (split_ws(tt, f, 3) < 2) die("smoke_select: a bad time record %s", tp);
        secs[j] = strtod(f[1], NULL);
        if (secs[j] < 0.05) secs[j] = 0.05;
        mbs[j] = (double)mb * secs[j];
        cost[j] = secs[j] / opt_budget + mbs[j] / opt_mem_budget;
        lines_assign(json_get(recs[j], "lines"));
        free(id), free(dir), free(mk), free(tp), free(tt);
    }
    nwords = (int)(ltop / 64 + 2);
    for (int j = 0; j < NJ; ++j) mask[j] = lines_mask(json_get(recs[j], "lines"));
    uint64_t *suite = xcalloc((size_t)nwords, sizeof *suite);
    int nfull = 0;
    double suite_secs = 0;
    for (int j = 0; j < NJ; ++j)
        if (!J[j].flag) {
            for (int w = 0; w < nwords; ++w) suite[w] |= mask[j][w];
            ++nfull;
            suite_secs += secs[j];
        }
    int suite_n = popc(suite);
    /* every (phase, class) pair and the cheapest cut of each recording covering it */
    rp = xcalloc((size_t)NJ, sizeof *rp);
    struct smap all_pairs = {0};
    for (int j = 0; j < NJ; ++j)
        if (!J[j].flag && !strcmp(J[j].test, "snapshots")) {
            char *d = xstrdup(J[j].dir);
            size_t dl = strlen(d);
            while (dl && d[dl - 1] == '/') d[--dl] = 0;
            rp[j] = phase_read(base_name(d));
            free(d);
            for (int i = 0; i < rp[j].n; ++i) {
                char *k = xasprintf("%s %s", rp[j].v[i].p, rp[j].v[i].c);
                smap_add(&all_pairs, k);
                free(k);
            }
        }
    covered = xcalloc((size_t)nwords, sizeof *covered);
    char *ok = xcalloc((size_t)NJ, 1);
    for (int j = 0; j < NJ; ++j) ok[j] = secs[j] <= opt_max_job;
    /* one job for each test binary: the one with the most new lines per second */
    struct sv ts = tests();
    for (int ti = 0; ti < ts.n; ++ti) {
        const char *t = ts.v[ti];
        int *js = xmalloc((size_t)NJ * sizeof *js), n = 0;
        for (int j = 0; j < NJ; ++j)
            if (ok[j] && !strcmp(J[j].test, t)) js[n++] = j;
        if (!n) {
            int best = -1;
            for (int j = 0; j < NJ; ++j)
                if (!strcmp(J[j].test, t) && (best < 0 || secs[j] < secs[best])) best = j;
            if (best >= 0) js[n++] = best;
        }
        int have = 0;
        for (int i = 0; i < nch; ++i) have |= !strcmp(J[ch[i].j].test, t);
        if (n && !have) {
            int best = js[0];
            double bs = (gain(best) + 1) / cost[best];
            for (int i = 1; i < n; ++i) {
                double s = (gain(js[i]) + 1) / cost[js[i]];
                if (s > bs) best = js[i], bs = s;
            }
            take(best, "binary");
        }
        free(js);
    }
    /* every (phase, class) pair any recording wrote */
    for (;;) {
        int left = 0;
        for (int i = 0; i < all_pairs.n; ++i) left += !smap_has(&pairs, all_pairs.keys[i]);
        if (!left) break;
        int best = -1;
        double bs = 0;
        for (int j = 0; j < NJ; ++j) {
            if (strcmp(J[j].test, "snapshots")) continue;
            double s = new_pairs(j) / cost[j];
            if (best < 0 || s > bs) best = j, bs = s;
        }
        if (best < 0 || !new_pairs(best)) break;
        take(best, "phase");
    }
    struct sv missing = {0};
    for (int i = 0; i < all_pairs.n; ++i)
        if (!smap_has(&pairs, all_pairs.keys[i])) {
            char *k = xstrdup(all_pairs.keys[i]);
            char *sp = strchr(k, ' ');
            *sp = '.';
            sv_push(&missing, k);
        }
    sv_sort(&missing);
    /* one replay at least --depth rows long */
    if (opt_depth) {
        int deepest = -1, has = 0;
        for (int i = 0; i < nch; ++i) has |= !strcmp(J[ch[i].j].test, "snapshots") && reach(ch[i].j) >= opt_depth;
        for (int j = 0; j < NJ; ++j)
            if (!strcmp(J[j].test, "snapshots") && reach(j) >= opt_depth && (deepest < 0 || cost[j] < cost[deepest])) deepest = j;
        if (deepest >= 0 && !has) {
            char why[64];
            snprintf(why, sizeof why, "depth %lld", opt_depth);
            take(deepest, why);
        }
    }
    /* then engine/ lines per cost while both budgets hold */
    for (;;) {
        int best = -1;
        double score = 0;
        for (int j = 0; j < NJ; ++j) {
            if (!ok[j] || tsec + secs[j] > opt_budget || tmbs + mbs[j] > opt_mem_budget || chosen_has(j)) continue;
            double r = gain(j) / cost[j];
            if (r > score) best = j, score = r;
        }
        if (best < 0 || score * opt_budget < opt_min_rate) break;
        take(best, "lines");
    }
    /* the lines left, by file */
    struct leftent *left = xcalloc((size_t)lfiles.n + 1, sizeof *left);
    int nleft = 0;
    for (int fi = 0; fi < lfiles.n; ++fi) {
        long long a = lfile_base[fi], b = fi + 1 < lfiles.n ? lfile_base[fi + 1] : ltop;
        int c = 0;
        for (long long x = a; x < b; ++x) c += (suite[x / 64] >> (x % 64) & 1) && !(covered[x / 64] >> (x % 64) & 1);
        if (c) left[nleft++] = (struct leftent){fi, c};
    }
    for (int a = 1; a < nleft; ++a) { /* stable, most first */
        struct leftent x = left[a];
        int b = a - 1;
        while (b >= 0 && left[b].count < x.count) left[b + 1] = left[b], --b;
        left[b + 1] = x;
    }
    (void)cmp_left_n;
    int got = 0;
    for (int w = 0; w < nwords; ++w) got += __builtin_popcountll(covered[w] & suite[w]);
    int npairs = 0;
    for (int i = 0; i < all_pairs.n; ++i) npairs += smap_has(&pairs, all_pairs.keys[i]);
    char stamp[32];
    time_t now = time(NULL);
    strftime(stamp, sizeof stamp, "%Y-%m-%d", localtime(&now));
    struct sb o = {0};
    sb_printf(&o, "# make smoke's jobs, written by tests/smoke_select.c on %s (make smoke-select refreshes it).\n", stamp);
    sb_puts(&o, "# TEST DIR [FLAGS]: out/native/test_TEST on out/java/DIR (- for none), those that reserve memory\n");
    sb_puts(&o, "# first, each group longest first; the\n");
    sb_puts(&o, "# seconds are the job's on the normal build, then the memory it reserves and why it was chosen.\n");
    sb_printf(&o, "# %d of %d suite jobs, %.0f of %.0f job-seconds, %.1f GB-seconds reserved (--budget %g --mem-budget %g --max-job %g);\n",
              nch, nfull, tsec, suite_secs, tmbs / 1000, opt_budget, opt_mem_budget, opt_max_job);
    sb_printf(&o, "# engine/ lines the suite runs: %d of %d (%.1f%%); (phase, class) pairs: %d of %d.\n", got, suite_n,
              100.0 * got / suite_n, npairs, all_pairs.n);
    if (missing.n) {
        sb_printf(&o, "# pairs only in jobs over %g s:", opt_max_job);
        for (int i = 0; i < missing.n; ++i) sb_printf(&o, " %s", missing.v[i]);
        sb_putc(&o, '\n');
    }
    sb_puts(&o, "# lines left to the full suite, by file:");
    for (int i = 0; i < nleft && i < 12; ++i) sb_printf(&o, "%s%s:%d", i ? " " : " ", lfiles.v[left[i].file] + 4, left[i].count);
    if (!nleft) sb_puts(&o, " ");
    sb_putc(&o, '\n');
    /* the jobs that reserve memory first, so all of them queue for the budget from the start, then the rest longest first */
    int *ord = xmalloc((size_t)(nch + 1) * sizeof *ord);
    for (int i = 0; i < nch; ++i) ord[i] = i;
    for (int a = 1; a < nch; ++a) {
        int x = ord[a], b = a - 1;
        for (; b >= 0; --b) {
            int y = ord[b];
            int kx = mbs[ch[x].j] == 0, ky = mbs[ch[y].j] == 0;
            if (ky > kx || (ky == kx && -secs[ch[y].j] > -secs[ch[x].j])) ord[b + 1] = ord[b];
            else break;
        }
        ord[b + 1] = x;
    }
    for (int i = 0; i < nch; ++i) {
        int j = ch[ord[i]].j;
        sb_printf(&o, "%s %s", J[j].test, J[j].dir ? J[j].dir : "-");
        if (J[j].flag) sb_printf(&o, " %s %s", J[j].flag, J[j].val);
        sb_printf(&o, "  # %.1fs", secs[j]);
        if (mbs[j]) sb_printf(&o, " %lld MB", (long long)(mbs[j] / secs[j]));
        sb_printf(&o, " %s\n", ch[ord[i]].why);
    }
    if (write_file(LIST, o.s, o.n)) die("smoke_select: cannot write %s", LIST);
    for (char *l = o.s; *l;) {
        char *e = strchr(l, '\n');
        if (*l == '#') printf("%.*s\n", (int)(e ? e - l : (long)strlen(l)), l);
        if (!e) break;
        l = e + 1;
    }
    printf("smoke-select: %d jobs written to tests/smoke.list\n", nch);
}

static int find_root(const char *argv0)
{
    char cwd[4096];
    if (getcwd(cwd, sizeof cwd))
        for (;;) {
            char *a = xasprintf("%s/csrc/tests/smoke.list", cwd);
            int ok = is_file(a);
            free(a);
            if (ok) { snprintf(ROOT, sizeof ROOT, "%s", cwd); return 1; }
            char *s = strrchr(cwd, '/');
            if (!s || s == cwd) break;
            *s = 0;
        }
    char rp[4096];
    if (strchr(argv0, '/') && realpath(argv0, rp)) {
        for (int up = 0; up < 3; ++up) {
            char *s = strrchr(rp, '/');
            if (!s) return 0;
            *s = 0;
        }
        snprintf(ROOT, sizeof ROOT, "%s", rp);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *stage = "all";
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i], *v = NULL;
        const char *eq = strchr(a, '=');
        char name[64];
        snprintf(name, sizeof name, "%.*s", eq ? (int)(eq - a) : (int)strlen(a), a);
        if (!strcmp(name, "--fresh")) { opt_fresh = 1; continue; }
        if (eq) v = eq + 1;
        else if (i + 1 < argc) v = argv[++i];
        else die("smoke_select: %s needs a value", a);
        if (!strcmp(name, "--stage")) {
            if (strcmp(v, "all") && strcmp(v, "phase") && strcmp(v, "cover") && strcmp(v, "time") && strcmp(v, "select"))
                die("smoke_select: --stage is all, phase, cover, time or select");
            stage = v;
        } else if (!strcmp(name, "--jobs")) opt_jobs = atoi(v);
        else if (!strcmp(name, "--budget")) opt_budget = strtod(v, NULL);
        else if (!strcmp(name, "--mem-budget")) opt_mem_budget = strtod(v, NULL);
        else if (!strcmp(name, "--max-job")) opt_max_job = strtod(v, NULL);
        else if (!strcmp(name, "--depth")) opt_depth = atoll(v);
        else if (!strcmp(name, "--min-rate")) opt_min_rate = strtod(v, NULL);
        else if (!strcmp(name, "--cover-jobs")) opt_cover_jobs = atoi(v);
        else die("smoke_select: unknown option %s", a);
    }
    if (!find_root(argv[0])) die("smoke_select: no repository above the current directory");
    snprintf(NATIVE, sizeof NATIVE, "%s/csrc", ROOT);
    snprintf(DATA, sizeof DATA, "%s/out/java", ROOT);
    snprintf(OUT, sizeof OUT, "%s/out/native", ROOT);
    snprintf(COV, sizeof COV, "%s/cov", OUT);
    snprintf(WORK, sizeof WORK, "%s/smoke", OUT);
    snprintf(LIST, sizeof LIST, "%s/tests/smoke.list", NATIVE);
    char *p;
    mkdirs(p = xasprintf("%s/phase", WORK)), free(p);
    mkdirs(p = xasprintf("%s/cov", WORK)), free(p);
    mkdirs(p = xasprintf("%s/time", WORK)), free(p);
    if (opt_fresh) {
        char *c = xasprintf("rm -f %s/phase/* %s/cov/* %s/time/*", WORK, WORK, WORK);
        char *av[] = {"sh", "-c", c, NULL};
        run_cmd(av, NULL, NULL, NULL, 0, NULL);
        free(c);
    }
    int all = !strcmp(stage, "all");
    if (all || !strcmp(stage, "phase")) {
        char *sd = xasprintf("%s/snapshots", DATA);
        struct sv es = list_dir(sd, 0), recs = {0};
        for (int i = 0; i < es.n; ++i) {
            char *q = path_join(sd, es.v[i]);
            if (is_dir(q)) sv_push(&recs, es.v[i]);
            free(q);
        }
        sv_sort(&recs);
        if (pool(phase_fn, recs.v, sizeof *recs.v, recs.n, opt_jobs, "phase", &recs)) {
            fprintf(stderr, "smoke-select: a recording fails its phase check\n");
            return 1;
        }
    }
    if (all || !strcmp(stage, "cover")) {
        int n;
        struct job *jobs = candidates(&n);
        /* longest first, from the last measurement where there is one */
        double *last = xmalloc((size_t)n * sizeof *last);
        for (int i = 0; i < n; ++i) {
            struct jval *v = read_cov(&jobs[i]);
            const struct jval *s = v ? json_get(v, "secs") : NULL;
            last[i] = s ? -s->dbl : (!strcmp(jobs[i].test, "snapshots") && !jobs[i].flag ? -1e9 : 0);
            if (v) json_free(v);
        }
        int *ord = xmalloc((size_t)n * sizeof *ord);
        for (int i = 0; i < n; ++i) ord[i] = i;
        for (int a = 1; a < n; ++a) {
            int x = ord[a], b = a - 1;
            while (b >= 0 && last[ord[b]] > last[x]) ord[b + 1] = ord[b], --b;
            ord[b + 1] = x;
        }
        struct job *sorted = xmalloc((size_t)n * sizeof *sorted);
        struct sv names = {0};
        for (int i = 0; i < n; ++i) sorted[i] = jobs[ord[i]], sv_push(&names, job_id(&sorted[i]));
        if (pool(cover_fn, sorted, sizeof *sorted, n, opt_jobs < opt_cover_jobs ? opt_jobs : opt_cover_jobs, "cover", &names)) {
            fprintf(stderr, "smoke-select: a job fails under coverage\n");
            return 1;
        }
    }
    if (all || !strcmp(stage, "time")) {
        int n;
        struct job *jobs = candidates(&n);
        struct sv names = {0};
        for (int i = 0; i < n; ++i) sv_push(&names, job_id(&jobs[i]));
        /* one at a time per worker, beside nothing else of ours: the seconds are the cost */
        if (pool(time_fn, jobs, sizeof *jobs, n, opt_jobs < opt_cover_jobs ? opt_jobs : opt_cover_jobs, "time", &names)) {
            fprintf(stderr, "smoke-select: a job fails\n");
            return 1;
        }
    }
    if (all || !strcmp(stage, "select")) selection();
    return 0;
}
