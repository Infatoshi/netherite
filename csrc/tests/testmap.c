/* make test-map: record, for every job of make test, what it ran.
 *
 *     out/native/testmap --root R --out O --data D --global G --store S --fncov F
 *         --order ORDER [--jobs N] [--only REGEX] [--keep 3] [--path P] [--fnkey BIN]
 *         [--skip FILE] [--logs DIR]
 *
 * Run from csrc/ (make test-map does, after building the normal tree and the
 * function coverage build F = out/native/fncov: gcc -finstrument-functions-once
 * with tests/fncov_rt.h). Each job of ORDER (make test's order file: N RUNNER
 * DIR TAG [FLAGS]) runs as make test runs it, through tests/memslot.sh (the
 * one beside this program when there is one, else csrc/tests'), on F's
 * programs, under strace:
 *
 * - each program's hook writes the offset of every function it entered
 *   (NETHERITE_FNCOV/PID.EXE); nm on F's program names them;
 * - strace -f lists every path the job's processes opened, probed (stat,
 *   access, readlink) or ran, resolved against each process's directory;
 *   paths under /proc, /sys, /dev, /tmp and TMPDIR, the job's own recording
 *   directory (its files are keyed as a whole), files opened only for
 *   writing and F's programs themselves are left out.
 *
 * A job that passes gets a record (tests/fnkey.c reads it): its job line, a
 * bitset over one names table (PROGRAM FUNCTION lines, S/names/ID.txt) and its
 * paths; fnkey key gives the record's key on this tree, and the record and the
 * run's log go to S/JOBID/KEY.rec and KEY.log. make test replays the log when
 * fnkey reproduces KEY (TESTSELECT in csrc/Makefile). The newest three
 * records of a job are kept. rawjudge.sh (it runs the Java oracle) and a job
 * that fails get none. --only is a POSIX extended regular expression. The C
 * port of tests/testmap.py (lane/cport): the same records. */
#define _GNU_SOURCE
#include <regex.h>
#include <utime.h>
#include "tool.h"

static char *A_root, *A_out, *A_data, *A_global, *A_store, *A_fncov, *A_order, *A_only, *A_path, *A_fnkey, *A_skip, *A_logs;
static int A_jobs = 16, A_keep = 3;
static char *NATIVE, *HERE_MEMSLOT, *ROOT, *OUT, *DATA, *FNCOV, *STORE, *WORK;
static struct sv TMPS;

/* the job's line as tests/fnkey.c's job_line writes it, or NULL */
static char *jobline(char **f, int n)
{
    const char *d = f[1];
    if (strcmp(d, "-")) {
        char *pre = xasprintf("%s/", DATA);
        int ok = starts_with(d, pre);
        free(pre);
        if (!ok) return NULL;
        d += strlen(DATA) + 1;
    }
    struct sb b = {0};
    sb_printf(&b, "%s %s", base_name(f[0]), d);
    for (int i = 2; i < n; ++i) sb_printf(&b, " %s", f[i]);
    return (char *)sb_str(&b);
}

static char *job_id(const char *line)
{
    size_t n = strlen(line);
    char hex[41];
    sha1_of(line, n + 1, hex); /* the line and its NUL */
    return xstrndup(hex, 16);
}

/* the job's command on the coverage build, as make test runs it, or NULL */
static char **command(char **f, int n)
{
    const char *runner = f[0], *d = f[1], *b = base_name(runner);
    char **c = xcalloc((size_t)n + 4, sizeof *c);
    int k = 0;
    if (starts_with(b, "test_")) {
        c[k++] = path_join(FNCOV, b);
        if (strcmp(d, "-")) {
            for (int i = 3; i < n; ++i) c[k++] = f[i];
            c[k++] = (char *)d;
        }
        return c;
    }
    if (!strcmp(b, "play_check.sh") || !strcmp(b, "frame_judge.sh") || !strcmp(b, "gui_judge.sh")) {
        c[k++] = (char *)runner;
        for (int i = 3; i < n; ++i) c[k++] = f[i];
        c[k++] = (char *)d;
        c[k++] = !strcmp(b, "gui_judge.sh") ? path_join(FNCOV, "play-dev") : FNCOV;
        return c;
    }
    free(c);
    return NULL;
}

/* ---- names */
struct syms {
    char *prog;
    uint64_t *addr;
    char **name;
    int n;
    uint64_t start;
};
static struct smap symtab;

struct symrow {
    uint64_t a;
    char *n;
};

static int cmp_symrow(const void *x, const void *y)
{
    const struct symrow *a = x, *b = y;
    if (a->a != b->a) return a->a < b->a ? -1 : 1;
    return strcmp(a->n, b->n);
}

/* (sorted addresses, names, executable start) of F's program */
static struct syms *symbols(const char *prog)
{
    struct syms *s = smap_get(&symtab, prog);
    if (s) return s;
    s = xcalloc(1, sizeof *s);
    char *path = path_join(FNCOV, prog);
    char *av[] = {"nm", "--defined-only", path, NULL};
    struct sb o = {0};
    if (run_cmd(av, NULL, NULL, &o, 0, NULL)) die("testmap: nm %s failed", path);
    struct symrow *rows = NULL;
    int n = 0, cap = 0;
    for (char *l = (char *)sb_str(&o); *l;) {
        char *e = strchr(l, '\n');
        if (e) *e = 0;
        char *f[4] = {0};
        int nf = split_ws(l, f, 4);
        if (nf == 3) {
            if (!strcmp(f[2], "__executable_start")) s->start = strtoull(f[0], NULL, 16);
            if (strlen(f[1]) == 1 && strchr("tTwWiI", f[1][0])) {
                if (n == cap) cap = cap ? cap * 2 : 4096, rows = xrealloc(rows, (size_t)cap * sizeof *rows);
                rows[n++] = (struct symrow){strtoull(f[0], NULL, 16), xstrdup(f[2])};
            }
        }
        if (!e) break;
        l = e + 1;
    }
    qsort(rows, (size_t)n, sizeof *rows, cmp_symrow);
    s->addr = xmalloc((size_t)(n + 1) * sizeof *s->addr);
    s->name = xmalloc((size_t)(n + 1) * sizeof *s->name);
    for (int i = 0; i < n; ++i) {
        s->addr[i] = rows[i].a;
        char *dot = strchr(rows[i].n, '.');
        if (dot) *dot = 0;
        s->name[i] = rows[i].n;
    }
    s->n = n;
    free(rows);
    sb_free(&o);
    free(path);
    smap_put(&symtab, prog, s);
    return s;
}

/* the functions the job's programs entered, "PROGRAM FUNCTION", or 0 (no record) */
static int functions(const char *fndir, struct smap *names)
{
    DIR *d = opendir(fndir);
    if (!d) return 1;
    struct dirent *e;
    int ok = 1;
    while (ok && (e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        const char *dot = strchr(e->d_name, '.');
        if (!dot) { ok = 0; break; } /* split('.', 1)[1]: IndexError */
        const char *prog = dot + 1;
        char *pp = path_join(FNCOV, prog);
        int have = path_exists(pp);
        free(pp);
        if (!have) { ok = 0; break; }
        struct syms *s = symbols(prog);
        char *p = path_join(fndir, e->d_name);
        size_t n;
        char *data = read_file(p, &n);
        free(p);
        for (size_t i = 0; data && i + 8 <= n; i += 8) {
            uint64_t off = 0;
            for (int b = 7; b >= 0; --b) off = off << 8 | (unsigned char)data[i + (size_t)b];
            uint64_t v = s->start + off;
            int lo = 0, hi = s->n;
            while (lo < hi) {
                int mid = (lo + hi) / 2;
                if (s->addr[mid] <= v) lo = mid + 1;
                else hi = mid;
            }
            if (lo - 1 < 0) { ok = 0; break; }
            char *k = xasprintf("%s %s", prog, s->name[lo - 1]);
            smap_add(names, k);
            free(k);
        }
        free(data);
    }
    closedir(d);
    return ok;
}

/* ---- paths */

static int D(char c) { return c >= '0' && c <= '9'; }

/* strace's argument list: quoted strings and bracketed parts kept whole */
static int split_args(const char *s, char ***out)
{
    struct sv a = {0};
    struct sb cur = {0};
    int depth = 0, q = 0;
    size_t n = strlen(s);
    for (size_t i = 0; i < n; ++i) {
        char ch = s[i];
        if (q) {
            sb_putc(&cur, ch);
            if (ch == '\\' && i + 1 < n) sb_putc(&cur, s[++i]);
            else if (ch == '"') q = 0;
        } else if (ch == '"') q = 1, sb_putc(&cur, ch);
        else if (strchr("[{(<", ch)) ++depth, sb_putc(&cur, ch);
        else if (strchr("]})>", ch)) --depth, sb_putc(&cur, ch);
        else if (ch == ',' && depth == 0) {
            sv_push(&a, xstrdup(strip((char *)sb_str(&cur))));
            cur.n = 0;
            cur.s[0] = 0;
        } else sb_putc(&cur, ch);
    }
    char *t = strip((char *)sb_str(&cur));
    if (*t) sv_push(&a, xstrdup(t));
    sb_free(&cur);
    *out = a.v;
    return a.n;
}

/* a quoted strace string's bytes (unicode_escape's escapes), or NULL */
static char *unquote(const char *s)
{
    size_t n = strlen(s);
    if (n < 2 || s[0] != '"' || s[n - 1] != '"') return NULL;
    struct sb b = {0};
    for (size_t i = 1; i < n - 1; ++i) {
        char c = s[i];
        if (c != '\\' || i + 1 >= n - 1) { sb_putc(&b, c); continue; }
        char e = s[++i];
        switch (e) {
        case '\\': sb_putc(&b, '\\'); break;
        case '\'': sb_putc(&b, '\''); break;
        case '"': sb_putc(&b, '"'); break;
        case 'a': sb_putc(&b, '\a'); break;
        case 'b': sb_putc(&b, '\b'); break;
        case 'f': sb_putc(&b, '\f'); break;
        case 'n': sb_putc(&b, '\n'); break;
        case 'r': sb_putc(&b, '\r'); break;
        case 't': sb_putc(&b, '\t'); break;
        case 'v': sb_putc(&b, '\v'); break;
        case '\n': break;
        case 'x': {
            int v = 0, k = 0;
            while (k < 2 && i + 1 < n - 1 && strchr("0123456789abcdefABCDEF", s[i + 1])) {
                char h = s[++i];
                v = v * 16 + (D(h) ? h - '0' : (h | 32) - 'a' + 10);
                ++k;
            }
            if (k < 2) die("testmap: a truncated \\x escape in strace output");
            sb_putc(&b, (char)v);
            break;
        }
        default:
            if (e >= '0' && e <= '7') {
                int v = e - '0', k = 1;
                while (k < 3 && i + 1 < n - 1 && s[i + 1] >= '0' && s[i + 1] <= '7') v = v * 8 + (s[++i] - '0'), ++k;
                sb_putc(&b, (char)(v & 0xff));
            } else {
                sb_putc(&b, '\\');
                sb_putc(&b, e);
            }
        }
    }
    return (char *)sb_str(&b);
}

/* ^(?:AT_FDCWD|-?\d+)<(.*)>$ : the directory, or NULL */
static char *dirfd_dir(const char *a)
{
    const char *p = a;
    if (starts_with(p, "AT_FDCWD")) p += 8;
    else {
        if (*p == '-') ++p;
        if (!D(*p)) return NULL;
        while (D(*p)) ++p;
    }
    if (*p != '<') return NULL;
    size_t n = strlen(p);
    if (n < 2 || p[n - 1] != '>') return NULL;
    return xstrndup(p + 1, n - 2);
}

static int pathspec(const char *sc, int *dirfd_i, int *path_i)
{
    static const char *const ONE[] = {"open", "creat", "stat", "lstat", "access", "readlink", "execve", "chdir", NULL};
    static const char *const TWO[] = {"openat", "openat2", "newfstatat", "fstatat64", "statx", "faccessat", "faccessat2",
                                      "readlinkat", "execveat", NULL};
    for (int i = 0; ONE[i]; ++i)
        if (!strcmp(ONE[i], sc)) { *dirfd_i = -1, *path_i = 0; return 1; }
    for (int i = 0; TWO[i]; ++i)
        if (!strcmp(TWO[i], sc)) { *dirfd_i = 0, *path_i = 1; return 1; }
    return 0; /* truncate and the rest: none */
}

struct found {
    struct smap set; /* "kind\tpath" */
};

static int is_under(const char *p, const char *t) { return !strcmp(p, t) || (starts_with(p, t) && p[strlen(t)] == '/'); }

static struct sv paths(const char *stfile, const char *cwd0, const char *jobdir)
{
    struct smap cwd = {0}, pending = {0}, found = {0};
    FILE *f = fopen(stfile, "r");
    char *raw = NULL;
    size_t cap = 0;
    ssize_t len;
    while (f && (len = getline(&raw, &cap, f)) >= 0) {
        if (len && raw[len - 1] == '\n') raw[--len] = 0;
        /* ^(\d+) +(.*)$ */
        char *p = raw;
        if (!D(*p)) continue;
        while (D(*p)) ++p;
        if (*p != ' ') continue;
        char *pid = xstrndup(raw, (size_t)(p - raw));
        while (*p == ' ') ++p;
        char *rest = xstrdup(p);
        if (strchr(rest, '\n')) { free(pid), free(rest); continue; }
        if (ends_with(rest, "<unfinished ...>")) {
            rest[strlen(rest) - 16] = 0;
            int x = smap_add(&pending, pid);
            free(pending.vals[x]);
            pending.vals[x] = rest;
            free(pid);
            continue;
        }
        /* ^<\.\.\. [a-z0-9_]+ resumed>(.*)$ */
        if (starts_with(rest, "<... ")) {
            char *q = rest + 5, *q0 = q;
            while ((*q >= 'a' && *q <= 'z') || D(*q) || *q == '_') ++q;
            if (q > q0 && starts_with(q, " resumed>")) {
                int x = smap_find(&pending, pid);
                const char *pre = x >= 0 && pending.vals[x] ? pending.vals[x] : "";
                char *nr = xasprintf("%s%s", pre, q + 9);
                if (x >= 0) free(pending.vals[x]), pending.vals[x] = NULL;
                /* (popped: a later resume finds nothing) */
                if (x >= 0) {
                    smap_put(&pending, pid, NULL);
                }
                free(rest);
                rest = nr;
            }
        }
        for (char *q = rest; (q = strstr(q, "AT_FDCWD<"));) {
            char *s = q + 9, *e = strchr(s, '>');
            if (!e) break;
            smap_put(&cwd, pid, xstrndup(s, (size_t)(e - s)));
            q = e + 1;
        }
        /* ^(\d+) +([a-z0-9_]+)\((.*)\) += (-?\d+|\?) over pid + ' ' + rest */
        char *sc0 = rest, *q = rest;
        while ((*q >= 'a' && *q <= 'z') || D(*q) || *q == '_') ++q;
        if (q == sc0 || *q != '(') { free(pid), free(rest); continue; }
        char *sc = xstrndup(sc0, (size_t)(q - sc0));
        char *args0 = q + 1, *close_ = NULL, *ret = NULL;
        size_t rl = strlen(rest);
        for (long i = (long)rl - 1; i >= args0 - rest; --i) {
            if (rest[i] != ')') continue;
            char *r = rest + i + 1;
            if (*r != ' ') continue;
            while (*r == ' ') ++r;
            if (*r != '=' || r[1] != ' ') continue;
            r += 2;
            if (*r == '?') { close_ = rest + i, ret = xstrdup("?"); break; }
            char *r0 = r;
            if (*r == '-') ++r;
            if (!D(*r)) continue;
            while (D(*r)) ++r;
            close_ = rest + i;
            ret = xstrndup(r0, (size_t)(r - r0));
            break;
        }
        if (!close_) { free(sc), free(pid), free(rest); continue; }
        char *argstr = xstrndup(args0, (size_t)(close_ - args0));
        int retdigit = ret[0] && ret[0] != '-' && ret[0] != '?';
        const char *pcwd = smap_get(&cwd, pid) ? smap_get(&cwd, pid) : cwd0;
        if ((!strcmp(sc, "clone") || !strcmp(sc, "clone3") || !strcmp(sc, "fork") || !strcmp(sc, "vfork")) && retdigit) {
            smap_put(&cwd, ret, xstrdup(pcwd));
            goto next;
        }
        int di, pi;
        if (!pathspec(sc, &di, &pi)) goto next;
        {
            char **a;
            int na = split_args(argstr, &a);
            if (na <= pi) goto next;
            char *pth = unquote(a[pi]), *base;
            if (di >= 0) {
                char *dd = dirfd_dir(a[di]);
                base = dd ? dd : xstrdup(pcwd);
            } else base = xstrdup(pcwd);
            const char *rest0 = na > pi + 1 ? a[pi + 1] : NULL;
            if (!pth || !*pth) goto next;
            char *j = pth[0] == '/' ? xstrdup(pth) : path_join(base, pth);
            char *full = norm_path(j);
            free(j);
            if (!strcmp(sc, "chdir")) {
                if (!strcmp(ret, "0")) smap_put(&cwd, pid, xstrdup(full));
                goto next;
            }
            int isopen = !strcmp(sc, "open") || !strcmp(sc, "openat") || !strcmp(sc, "openat2");
            if (isopen && rest0 && strstr(rest0, "O_WRONLY")) goto next;
            if (!strcmp(sc, "creat")) goto next;
            /* a directory opened is read (its names); one probed is only there */
            const char *kind = isopen && rest0 && strstr(rest0, "O_DIRECTORY") ? "list" : "path";
            char *k = xasprintf("%s\t%s", kind, full);
            smap_add(&found, k);
            free(k);
        }
    next:
        free(sc), free(pid), free(rest), free(argstr), free(ret);
    }
    if (f) fclose(f);
    free(raw);
    static const char *const SKIP[] = {"/proc/", "/sys/", "/dev/", "/etc/ld.so.cache", "/etc/ld.so.preload", NULL};
    struct smap out = {0};
    char *rootp = xasprintf("%s/", ROOT), *fncovp = xasprintf("%s/", FNCOV);
    for (int i = 0; i < found.n; ++i) {
        char *tab = strchr(found.keys[i], '\t');
        char *kind = xstrndup(found.keys[i], (size_t)(tab - found.keys[i])), *p = xstrdup(tab + 1);
        int skip = 0;
        for (int s = 0; SKIP[s]; ++s) skip |= starts_with(p, SKIP[s]);
        for (int t = 0; t < TMPS.n; ++t) skip |= is_under(p, TMPS.v[t]);
        skip |= is_under(p, WORK);
        if (jobdir) skip |= is_under(p, jobdir);
        if (!skip && is_under(p, FNCOV)) {
            const char *rel = p[strlen(FNCOV)] ? p + strlen(FNCOV) + 1 : "";
            /* the coverage build's programs are the functions; its other files are the normal build's */
            if (!strchr(rel, '/') && (starts_with(rel, "test_") || !strcmp(rel, "play") || !strcmp(rel, "play-dev"))) skip = 1;
            else {
                char *np = path_join(OUT, rel);
                if (!rel[0]) { free(np); np = xasprintf("%s/", OUT); }
                free(p);
                p = np;
            }
        }
        if (!skip) {
            if (starts_with(p, rootp)) {
                char *np = xasprintf("@/%s", p + strlen(rootp));
                free(p);
                p = np;
            }
            char *k = xasprintf("%s %s", kind, p);
            smap_add(&out, k);
            free(k);
        }
        free(kind), free(p);
    }
    struct sv res = {0};
    for (int i = 0; i < out.n; ++i) sv_push(&res, out.keys[i]);
    /* sorted((kind, path)): kind first, then path */
    for (int a = 1; a < res.n; ++a) {
        char *x = res.v[a];
        int b = a - 1;
        for (; b >= 0; --b) {
            const char *y = res.v[b];
            size_t kx = strcspn(x, " "), ky = strcspn(y, " ");
            int c = strncmp(y, x, kx < ky ? kx : ky);
            if (!c) c = (int)ky - (int)kx;
            if (!c) c = strcmp(y + ky + 1, x + kx + 1);
            if (c > 0) res.v[b + 1] = res.v[b];
            else break;
        }
        res.v[b + 1] = x;
    }
    free(rootp), free(fncovp);
    return res;
}

/* ---- one job */
struct item {
    char *n;
    char **f;
    int nf;
    char *line;
    char **cmd;
    pid_t pid;
    double t0, secs;
    int rc, started, finished;
    /* the result */
    struct smap names;
    struct sv ps;
    char *what, *log;
    int ok;
};

static void launch(struct item *it)
{
    char *w = path_join(WORK, it->n);
    char *rm[] = {"rm", "-rf", w, NULL};
    run_cmd(rm, NULL, NULL, NULL, 0, NULL);
    char *fn = path_join(w, "fn"), *home = path_join(w, "home"), *st = path_join(w, "st"), *log = path_join(w, "log");
    mkdirs(fn);
    mkdirs(home);
    struct sv argv = {0};
    const char *d = it->f[1];
    if (strcmp(d, "-")) {
        sv_push(&argv, "bash");
        sv_push(&argv, HERE_MEMSLOT);
        sv_push(&argv, "--key");
        sv_push(&argv, (char *)base_name(it->f[0]));
        sv_push(&argv, (char *)d);
    }
    static char *const ST[] = {"strace", "-f", "-qq", "-y", "--seccomp-bpf", "-e", "trace=%file,%process", "-o", NULL};
    for (int i = 0; ST[i]; ++i) sv_push(&argv, ST[i]);
    sv_push(&argv, st);
    for (int i = 0; it->cmd[i]; ++i) sv_push(&argv, it->cmd[i]);
    sv_push(&argv, NULL);
    fflush(stdout);
    fflush(stderr);
    it->t0 = now_secs();
    pid_t pid = fork();
    if (pid < 0) die("fork");
    if (!pid) {
        setenv("NETHERITE_FNCOV", fn, 1);
        setenv("HOME", home, 1);
        if (A_path && A_path[0]) setenv("PATH", A_path, 1);
        int fd = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0 || chdir(NATIVE)) _exit(127);
        dup2(fd, 1);
        dup2(fd, 2);
        close(fd);
        execvp(argv.v[0], argv.v);
        _exit(127);
    }
    it->pid = pid;
    it->started = 1;
    it->log = log;
    free(fn), free(home), free(st), free(w);
}

static void copy_file(const char *a, const char *b)
{
    size_t n;
    char *d = read_file(a, &n);
    if (!d || write_file(b, d, n)) die("testmap: cannot copy %s to %s", a, b);
    free(d);
}

static void finish(struct item *it)
{
    if (A_logs) {
        char *id = job_id(it->line), *lp = xasprintf("%s/%s.log", A_logs, id), *rp = xasprintf("%s/%s.rc", A_logs, id);
        copy_file(it->log, lp);
        char rc[32];
        snprintf(rc, sizeof rc, "%d\n", it->rc);
        write_file(rp, rc, strlen(rc));
        free(id), free(lp), free(rp);
    }
    if (it->rc) {
        it->what = xasprintf("rc=%d %.1fs (no record)", it->rc, it->secs);
        return;
    }
    char *w = path_join(WORK, it->n), *fn = path_join(w, "fn"), *st = path_join(w, "st");
    if (!functions(fn, &it->names) || !it->names.n) {
        it->what = xasprintf("%.1fs no functions (no record)", it->secs);
        return;
    }
    char *jobdir = strcmp(it->f[1], "-") ? norm_path(it->f[1]) : NULL;
    it->ps = paths(st, NATIVE, jobdir);
    it->ok = 1;
    it->what = xasprintf("%.1fs %d functions %d paths", it->secs, it->names.n, it->ps.n);
    free(w), free(fn), free(st), free(jobdir);
}

static double mtime_of(const char *p)
{
    struct stat st;
    if (stat(p, &st)) return 0;
#ifdef __APPLE__
    return (double)st.st_mtimespec.tv_sec + (double)st.st_mtimespec.tv_nsec * 1e-9;
#else
    return (double)st.st_mtim.tv_sec + (double)st.st_mtim.tv_nsec * 1e-9;
#endif
}

struct recm {
    char *p;
    double m;
};

static int cmp_recm(const void *a, const void *b)
{
    double x = ((const struct recm *)a)->m, y = ((const struct recm *)b)->m;
    return x > y ? -1 : x < y;
}

static const char *exe_dir_memslot(const char *argv0)
{
    char rp[4096];
    if (strchr(argv0, '/') && realpath(argv0, rp)) {
        char *d = dir_name(rp), *m = path_join(d, "memslot.sh");
        free(d);
        if (is_file(m)) return m;
        free(m);
    }
    return xasprintf("%s/tests/memslot.sh", NATIVE);
}

int main(int argc, char **argv)
{
    struct {
        const char *name;
        char **dst;
    } opts[] = {{"--root", &A_root}, {"--out", &A_out}, {"--data", &A_data}, {"--global", &A_global}, {"--store", &A_store},
                {"--fncov", &A_fncov}, {"--order", &A_order}, {"--only", &A_only}, {"--path", &A_path}, {"--fnkey", &A_fnkey},
                {"--skip", &A_skip}, {"--logs", &A_logs}};
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i], *eq = strchr(a, '=');
        char name[64];
        snprintf(name, sizeof name, "%.*s", eq ? (int)(eq - a) : (int)strlen(a), a);
        const char *v = eq ? eq + 1 : i + 1 < argc ? argv[++i] : NULL;
        if (!v) die("testmap: %s needs a value", a);
        int hit = 0;
        for (size_t k = 0; k < sizeof opts / sizeof *opts; ++k)
            if (!strcmp(opts[k].name, name)) *opts[k].dst = (char *)v, hit = 1;
        if (!strcmp(name, "--jobs")) A_jobs = atoi(v), hit = 1;
        if (!strcmp(name, "--keep")) A_keep = atoi(v), hit = 1;
        if (!hit) die("testmap: unknown option %s", a);
    }
    if (!A_root || !A_out || !A_data || !A_global || !A_store || !A_fncov || !A_order)
        die("usage: testmap --root R --out O --data D --global G --store S --fncov F --order ORDER [...]");
    char cwd[4096];
    if (!getcwd(cwd, sizeof cwd)) die("getcwd");
    NATIVE = xstrdup(cwd);
    HERE_MEMSLOT = (char *)exe_dir_memslot(argv[0]);
    char rp[4096];
    ROOT = realpath(A_root, rp) ? xstrdup(rp) : abs_path(A_root);
    OUT = abs_path(A_out), DATA = abs_path(A_data), FNCOV = abs_path(A_fncov), STORE = abs_path(A_store);
    WORK = path_join(FNCOV, "map");
    sv_push(&TMPS, "/tmp");
    const char *td = getenv("TMPDIR");
    if (td && *td && strcmp(td, "/tmp")) sv_push(&TMPS, (char *)td);
    /* the order and the skip list */
    regex_t rx;
    if (A_only && *A_only && regcomp(&rx, A_only, REG_EXTENDED | REG_NOSUB)) die("testmap: --only is not a regular expression");
    struct smap skip = {0};
    if (A_skip && *A_skip) {
        char *t = read_file(A_skip, NULL);
        for (char *l = t; l && *l;) {
            char *e = strchr(l, '\n');
            if (e) *e = 0;
            char *f[2] = {0};
            if (split_ws(l, f, 1)) smap_add(&skip, f[0]);
            if (!e) break;
            l = e + 1;
        }
    }
    struct item *items = NULL;
    int n = 0;
    char *ot = read_file(A_order, NULL);
    if (!ot) die("testmap: cannot read %s", A_order);
    for (char *l = ot; *l;) {
        char *e = strchr(l, '\n');
        if (e) *e = 0;
        char **f = xcalloc(256, sizeof *f);
        int nf = split_ws(l, f, 256);
        if (nf) {
            struct sb rest = {0};
            for (int i = 1; i < nf; ++i) sb_printf(&rest, "%s%s", i > 1 ? " " : "", f[i]);
            int take = (!A_only || !*A_only || !regexec(&rx, sb_str(&rest), 0, NULL, 0)) && !smap_has(&skip, f[0]);
            if (take) {
                items = xrealloc(items, (size_t)(n + 1) * sizeof *items);
                memset(&items[n], 0, sizeof *items);
                items[n].n = f[0];
                items[n].f = f + 1;
                items[n].nf = nf - 1;
                ++n;
            }
            sb_free(&rest);
        }
        if (!e) break;
        l = e + 1;
    }
    if (skip.n) printf("test-map: %d jobs already have a record this tree reproduces\n", skip.n);
    mkdirs(WORK);
    if (A_logs) mkdirs(A_logs);
    char *namesdir = path_join(STORE, "names");
    mkdirs(namesdir);
    double t0 = now_secs();
    int done = 0, running = 0, next = 0;
    for (int i = 0; i < n; ++i) {
        items[i].line = jobline(items[i].f, items[i].nf);
        items[i].cmd = items[i].line ? command(items[i].f, items[i].nf) : NULL;
    }
    for (int i = 0; i < n; ++i) {
        struct item *it = &items[i];
        if (!it->line || !it->cmd) it->finished = 1, it->what = xstrdup("no record (runner)");
        while (!it->finished) {
            while (running < A_jobs && next < n) {
                if (!items[next].line || !items[next].cmd) { ++next; continue; }
                launch(&items[next++]);
                ++running;
            }
            int st;
            pid_t p = waitpid(-1, &st, 0);
            if (p < 0) die("testmap: waitpid: %s", strerror(errno));
            for (int k = 0; k < n; ++k)
                if (items[k].started && !items[k].finished && items[k].pid == p) {
                    items[k].rc = wait_rc(st);
                    items[k].secs = now_secs() - items[k].t0;
                    items[k].finished = 1;
                    --running;
                    break;
                }
        }
        if (it->line && it->cmd) finish(it);
        ++done;
        if (done % 100 == 0 || done == n) {
            printf("test-map: %d/%d jobs run, %.0f s\n", done, n, now_secs() - t0);
            fflush(stdout);
        }
    }
    /* one names table for the run */
    struct smap all = {0};
    for (int i = 0; i < n; ++i)
        for (int k = 0; items[i].ok && k < items[i].names.n; ++k) smap_add(&all, items[i].names.keys[k]);
    struct sv table = {0};
    for (int i = 0; i < all.n; ++i) sv_push(&table, all.keys[i]);
    sv_sort(&table);
    struct sb body = {0};
    for (int i = 0; i < table.n; ++i) sb_printf(&body, "%s\n", table.v[i]);
    char hex[41];
    sha1_of(sb_str(&body), body.n, hex);
    char *tid = xstrndup(hex, 16), *tpath = xasprintf("%s/%s.txt", namesdir, tid);
    if (!path_exists(tpath)) {
        char *tmp = xasprintf("%s.tmp", tpath);
        if (write_file(tmp, body.s ? body.s : "", body.n) || rename(tmp, tpath)) die("testmap: cannot write %s", tpath);
    }
    struct smap index = {0};
    for (int i = 0; i < table.n; ++i) smap_put(&index, table.v[i], (void *)(intptr_t)i);
    struct sv bad = {0};
    struct sv recp = {0};
    int *recidx = xmalloc((size_t)(n + 1) * sizeof *recidx), nrec = 0;
    for (int i = 0; i < n; ++i) {
        struct item *it = &items[i];
        if (!it->ok) {
            sv_push(&bad, xasprintf("test-map: job %s %s: %s", it->n, it->line ? it->line : "None", it->what));
            continue;
        }
        size_t nb = ((size_t)table.n + 3) / 4;
        unsigned char *bits = xcalloc(nb + 1, 1);
        for (int k = 0; k < it->names.n; ++k) {
            int x = (int)(intptr_t)smap_get(&index, it->names.keys[k]);
            bits[x / 4] |= (unsigned char)(8 >> (x % 4));
        }
        struct sb r = {0};
        sb_printf(&r, "fnkey 1\njob %s\nnames %s\nbits ", it->line, tid);
        size_t hs = r.n;
        for (size_t k = 0; k < nb; ++k) sb_printf(&r, "%x", bits[k]);
        while (r.n > hs && r.s[r.n - 1] == '0') r.s[--r.n] = 0;
        sb_putc(&r, '\n');
        for (int k = 0; k < it->ps.n; ++k) sb_printf(&r, "%s\n", it->ps.v[k]);
        char *rpth = xasprintf("%s/%s/rec", WORK, it->n);
        if (write_file(rpth, r.s, r.n)) die("testmap: cannot write %s", rpth);
        sv_push(&recp, rpth);
        recidx[nrec++] = i;
        sb_free(&r);
        free(bits);
    }
    struct smap keys = {0};
    for (int i = 0; i < recp.n; i += 200) {
        struct sv av = {0};
        sv_push(&av, A_fnkey && *A_fnkey ? A_fnkey : path_join(OUT, "fnkey"));
        const char *fixed[] = {"key", "--root", ROOT, "--out", OUT, "--data", DATA, "--store", STORE, "--global", A_global};
        for (size_t k = 0; k < sizeof fixed / sizeof *fixed; ++k) sv_push(&av, (char *)fixed[k]);
        for (int k = i; k < recp.n && k < i + 200; ++k) sv_push(&av, recp.v[k]);
        sv_push(&av, NULL);
        struct sb o = {0};
        int fd = -1;
        (void)fd;
        run_cmd(av.v, NULL, NULL, &o, 0, NULL);
        for (char *l = (char *)sb_str(&o); *l;) {
            char *e = strchr(l, '\n');
            if (e) *e = 0;
            char *sp = strrchr(l, ' ');
            if (sp) {
                *sp = 0;
                smap_put(&keys, l, xstrdup(sp + 1));
            }
            if (!e) break;
            l = e + 1;
        }
    }
    int wrote = 0;
    for (int r = 0; r < nrec; ++r) {
        struct item *it = &items[recidx[r]];
        const char *k = smap_get(&keys, recp.v[r]);
        if (!k || !strcmp(k, "-")) {
            sv_push(&bad, xasprintf("test-map: job %s %s: fnkey gave no key (no record)", it->n, it->line));
            continue;
        }
        char *id = job_id(it->line), *d = path_join(STORE, id);
        mkdirs(d);
        char *lt = xasprintf("%s/%s.log.tmp", d, k), *lf = xasprintf("%s/%s.log", d, k);
        char *rt = xasprintf("%s/%s.rec.tmp", d, k), *rf = xasprintf("%s/%s.rec", d, k);
        copy_file(it->log, lt);
        rename(lt, lf);
        copy_file(recp.v[r], rt);
        rename(rt, rf);
        utime(rf, NULL);
        ++wrote;
        /* the newest records of the job */
        struct sv fs = list_dir(d, 0);
        struct recm *rm = xmalloc((size_t)(fs.n + 1) * sizeof *rm);
        int nr = 0;
        for (int q = 0; q < fs.n; ++q)
            if (ends_with(fs.v[q], ".rec")) {
                rm[nr].p = path_join(d, fs.v[q]);
                rm[nr].m = mtime_of(rm[nr].p);
                ++nr;
            }
        qsort(rm, (size_t)nr, sizeof *rm, cmp_recm);
        for (int q = A_keep; q < nr; ++q) {
            unlink(rm[q].p);
            char *lg = xasprintf("%.*s.log", (int)(strlen(rm[q].p) - 4), rm[q].p);
            unlink(lg);
            free(lg);
        }
        free(rm);
        free(id), free(d), free(lt), free(lf), free(rt), free(rf);
    }
    /* names tables no record names any more */
    struct smap used = {0};
    struct sv sd = list_dir(STORE, 0);
    for (int i = 0; i < sd.n; ++i) {
        char *d = path_join(STORE, sd.v[i]);
        if (is_dir(d)) {
            struct sv fs = list_dir(d, 0);
            for (int q = 0; q < fs.n; ++q) {
                if (!ends_with(fs.v[q], ".rec")) continue;
                char *p = path_join(d, fs.v[q]);
                FILE *f = fopen(p, "r");
                char line[512];
                while (f && fgets(line, sizeof line, f))
                    if (starts_with(line, "names ")) {
                        char *fl[3];
                        if (split_ws(line, fl, 3) > 1) smap_add(&used, fl[1]);
                        break;
                    }
                if (f) fclose(f);
                free(p);
            }
        }
        free(d);
    }
    struct sv nt = list_dir(namesdir, 0);
    for (int i = 0; i < nt.n; ++i)
        if (ends_with(nt.v[i], ".txt")) {
            char *t = xstrndup(nt.v[i], strlen(nt.v[i]) - 4);
            if (!smap_has(&used, t)) {
                char *p = path_join(namesdir, nt.v[i]);
                unlink(p);
                free(p);
            }
            free(t);
        }
    char *rm[] = {"rm", "-rf", WORK, NULL};
    run_cmd(rm, NULL, NULL, NULL, 0, NULL);
    for (int i = 0; i < bad.n; ++i) printf("%s\n", bad.v[i]);
    printf("test-map: %d of %d jobs recorded in %.0f s, %d functions named, store %s\n", wrote, n, now_secs() - t0, table.n, STORE);
    return 0;
}
