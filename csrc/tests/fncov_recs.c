/* Native coverage: which functions of csrc/engine (and csrc/play) the
 * snapshot recordings execute, and which none does (make -C csrc coverage).
 *
 *     out/native/fncov_recs --root R --fncov F --data D --out O [--jobs N] [NAME|DIR ...]
 *
 * F is the function coverage build (make fncov: every function calls
 * tests/fncov_rt.h's hook the first time it runs in a process, which appends its
 * offset to NETHERITE_FNCOV/PID.EXE). Each recording (every directory of
 * D/snapshots with a tape.jsonl, or the names or directories given) runs as
 * make test runs it: F/test_snapshots over the whole tape, and
 * tests/play_check.sh on F's play, each under tests/memslot.sh, N at a time.
 * An offset is named with nm and addr2line on the program that wrote it (file
 * and function). The universe is every function symbol of F's test_snapshots,
 * play and play-dev whose definition is under csrc/engine or csrc/play.
 * Writes into O:
 *   functions.tsv   every function: file, function, recordings running it, the first few
 *   unexecuted.tsv  the functions no recording runs, by file, with the file's index record
 *                   (layer, the Java files it ports, whether the function is an entry point)
 *   recs.tsv        each recording's jobs and their exit codes
 *   summary.txt     functions run / total by file, files none of whose functions run first
 * A job that fails still counts for the functions it ran; its rc is in recs.tsv.
 * The C port of tests/fncov_recs.py (lane/cport): the same files. */
#define _GNU_SOURCE
#include <inttypes.h>
#include "../engine/tape.h"
#include "tool.h"

static char *ROOT, *FNCOV, *DATA, *OUT, *NATIVE, *WORK;

struct job {
    char *n, *tag;
    char *argv[8];
    pid_t pid;
    int rc, started, finished;
    double t0, secs;
};

/* ---- names: nm for the symbols, addr2line for their files */
struct prog {
    uint64_t *addr;
    int n;
    uint64_t start;
    int *where; /* the function (an id into fns) at each address, -1 if not ours */
};
static struct smap progs;
static struct smap fns; /* "file\tfunction" -> id */

struct row {
    uint64_t a;
    char *n;
};

static int cmp_row(const void *x, const void *y)
{
    const struct row *a = x, *b = y;
    if (a->a != b->a) return a->a < b->a ? -1 : 1;
    return strcmp(a->n, b->n);
}

static struct prog *prog_syms(const char *prog)
{
    struct prog *p = smap_get(&progs, prog);
    if (p) return p;
    p = xcalloc(1, sizeof *p);
    char *path = path_join(FNCOV, prog);
    char *av[] = {"nm", "--defined-only", path, NULL};
    struct sb o = {0};
    if (run_cmd(av, NULL, NULL, &o, 0, NULL)) die("coverage: nm %s failed", path);
    struct row *rows = NULL;
    int n = 0, cap = 0;
    for (char *l = (char *)sb_str(&o); *l;) {
        char *e = strchr(l, '\n');
        if (e) *e = 0;
        char *f[4] = {0};
        if (split_ws(l, f, 4) == 3) {
            if (!strcmp(f[2], "__executable_start")) p->start = strtoull(f[0], NULL, 16);
            if (strlen(f[1]) == 1 && strchr("tTwW", f[1][0])) {
                if (n == cap) cap = cap ? cap * 2 : 4096, rows = xrealloc(rows, (size_t)cap * sizeof *rows);
                rows[n++] = (struct row){strtoull(f[0], NULL, 16), xstrdup(f[2])};
            }
        }
        if (!e) break;
        l = e + 1;
    }
    qsort(rows, (size_t)n, sizeof *rows, cmp_row);
    p->n = n;
    p->addr = xmalloc((size_t)(n + 1) * sizeof *p->addr);
    p->where = xmalloc((size_t)(n + 1) * sizeof *p->where);
    struct sv a2 = {0};
    sv_push(&a2, "addr2line");
    sv_push(&a2, "-e");
    sv_push(&a2, path);
    for (int i = 0; i < n; ++i) {
        p->addr[i] = rows[i].a;
        sv_push(&a2, xasprintf("%" PRIx64, rows[i].a));
    }
    sv_push(&a2, NULL);
    struct sb r = {0};
    if (run_cmd(a2.v, NULL, NULL, &r, 0, NULL)) die("coverage: addr2line %s failed", path);
    char *nat_src = xasprintf("%s/engine/", NATIVE), *nat_play = xasprintf("%s/play/", NATIVE), *rootp = xasprintf("%s/", ROOT);
    /* where: by address, a later symbol at the same address overwriting an earlier one */
    struct smap byaddr = {0};
    char *l = (char *)sb_str(&r);
    for (int i = 0; i < n; ++i) p->where[i] = -1;
    for (int i = 0; i < n && l && *l; ++i) {
        char *e = strchr(l, '\n');
        if (e) *e = 0;
        char *c = strchr(l, ':');
        char *file = c ? xstrndup(l, (size_t)(c - l)) : xstrdup(l), *full;
        if (file[0] == '/') full = norm_path(file);
        else {
            char *j = path_join(NATIVE, file);
            full = norm_path(j);
            free(j);
        }
        if (starts_with(full, nat_src) || starts_with(full, nat_play)) {
            char *nm = xstrdup(rows[i].n), *dot = strchr(nm, '.');
            if (dot) *dot = 0;
            char *k = xasprintf("%s\t%s", full + strlen(rootp), nm);
            int id = smap_add(&fns, k);
            char ak[32];
            snprintf(ak, sizeof ak, "%" PRIx64, rows[i].a);
            smap_put(&byaddr, ak, (void *)(intptr_t)(id + 1));
            free(k), free(nm);
        }
        free(file), free(full);
        l = e ? e + 1 : NULL;
    }
    for (int i = 0; i < n; ++i) {
        char ak[32];
        snprintf(ak, sizeof ak, "%" PRIx64, p->addr[i]);
        intptr_t v = (intptr_t)smap_get(&byaddr, ak);
        p->where[i] = v ? (int)v - 1 : -1;
    }
    smap_put(&progs, prog, p);
    return p;
}

/* the Java files a record ports, or - */
static char *ports(const struct jval *o)
{
    const struct jval *js = o ? json_get(o, "java") : NULL;
    struct sb b = {0};
    int n = js && js->kind == J_ARR ? js->nitems : 0;
    for (int i = 0; i < n && i < 3; ++i) {
        const struct jval *p = json_get(js->items[i], "path");
        const char *s = p && p->kind == J_STR ? p->str : "";
        sb_printf(&b, "%s%s", i ? " " : "", strlen(s) > 9 ? s + 9 : "");
    }
    if (n > 3) sb_printf(&b, " +%d", n - 3);
    if (!b.n) sb_putc(&b, '-');
    return (char *)sb_str(&b);
}

static int entry(const struct jval *o, const char *fn)
{
    const struct jval *es = o ? json_get(o, "entry_points") : NULL;
    for (int i = 0; es && es->kind == J_ARR && i < es->nitems; ++i) {
        if (es->items[i]->kind != J_STR) continue;
        char *s = xstrdup(es->items[i]->str), *p = strchr(s, '(');
        if (p) *p = 0;
        int hit = !strcmp(strip(s), fn);
        free(s);
        if (hit) return 1;
    }
    return 0;
}

static const char *layer_of(const struct jval *o)
{
    const struct jval *l = o ? json_get(o, "layer") : NULL;
    if (!l) return "-";
    if (l->kind == J_STR) return l->str;
    return l->kind == J_NULL ? "None" : "?";
}

static struct smap records; /* path -> struct jval * */
static struct smap rows;    /* recording -> rows */

static long long rows_of(const char *r)
{
    int x = smap_find(&rows, r);
    return x < 0 ? (1LL << 30) : (long long)(intptr_t)rows.vals[x];
}

static int cmp_short(const void *a, const void *b)
{
    const char *x = *(char *const *)a, *y = *(char *const *)b;
    long long rx = rows_of(x), ry = rows_of(y);
    if (rx != ry) return rx < ry ? -1 : 1;
    return strcmp(x, y);
}

static char *shortest(struct sv *rs, int k)
{
    struct sv c = {0};
    for (int i = 0; i < rs->n; ++i) sv_push(&c, rs->v[i]);
    if (c.n > 1) qsort(c.v, (size_t)c.n, sizeof *c.v, cmp_short);
    struct sb b = {0};
    for (int i = 0; i < c.n && i < k; ++i) sb_printf(&b, "%s%s", i ? " " : "", c.v[i]);
    if (!b.n) sb_putc(&b, '-');
    free(c.v);
    return (char *)sb_str(&b);
}

struct byfile {
    long long run, total;
};

struct jres {
    char *tag;
    int rc;
    double secs;
};

static int cmp_jres(const void *a, const void *b)
{
    const struct jres *x = a, *y = b;
    int c = strcmp(x->tag, y->tag);
    if (c) return c;
    if (x->rc != y->rc) return x->rc < y->rc ? -1 : 1;
    return x->secs < y->secs ? -1 : x->secs > y->secs;
}

int main(int argc, char **argv)
{
    char *a_root = NULL, *a_fncov = NULL, *a_data = NULL, *a_out = NULL;
    int jobsn = 16;
    struct sv recs = {0};
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        char **dst = !strcmp(a, "--root") ? &a_root : !strcmp(a, "--fncov") ? &a_fncov : !strcmp(a, "--data") ? &a_data
                     : !strcmp(a, "--out") ? &a_out : NULL;
        if (dst || !strcmp(a, "--jobs")) {
            if (i + 1 >= argc) die("coverage: %s needs a value", a);
            if (dst) *dst = argv[++i];
            else jobsn = atoi(argv[++i]);
        } else sv_push(&recs, argv[i]);
    }
    if (!a_root || !a_fncov || !a_data || !a_out) die("usage: fncov_recs --root R --fncov F --data D --out O [--jobs N] [NAME|DIR ...]");
    ROOT = abs_path(a_root), FNCOV = abs_path(a_fncov), DATA = abs_path(a_data), OUT = abs_path(a_out);
    NATIVE = path_join(ROOT, "csrc"), WORK = path_join(OUT, "fn");
    int given = recs.n;
    if (!given) {
        char *sd = path_join(DATA, "snapshots");
        struct sv es = list_dir(sd, 0);
        for (int i = 0; i < es.n; ++i) {
            char *t = xasprintf("%s/%s/tape.jsonl", sd, es.v[i]);
            if (path_exists(t)) sv_push(&recs, es.v[i]);
            free(t);
        }
        sv_sort(&recs);
    }
    struct sv dirs = {0};
    for (int i = 0; i < recs.n; ++i) {
        char *d = strchr(recs.v[i], '/') ? abs_path(recs.v[i]) : xasprintf("%s/snapshots/%s", DATA, recs.v[i]);
        char *t = path_join(d, "tape.jsonl");
        if (!is_file(t)) {
            fprintf(stderr, "coverage: no tape.jsonl in %s\n", d);
            return 1;
        }
        size_t n = strlen(d);
        while (n > 1 && d[n - 1] == '/') d[--n] = 0;
        sv_push(&dirs, d);
    }
    if (!given) {
        char *rm[] = {"rm", "-rf", OUT, NULL};
        run_cmd(rm, NULL, NULL, NULL, 0, NULL);
    }
    mkdirs(WORK);
    char *memslot = xasprintf("%s/tests/memslot.sh", NATIVE);
    struct job *jobs = xcalloc((size_t)dirs.n * 2 + 1, sizeof *jobs);
    int nj = 0;
    for (int i = 0; i < dirs.n; ++i) {
        char *d = dirs.v[i];
        const char *n = base_name(d);
        char *w = path_join(WORK, n);
        char *rm[] = {"rm", "-rf", w, NULL};
        run_cmd(rm, NULL, NULL, NULL, 0, NULL);
        free(w);
        jobs[nj++] = (struct job){xstrdup(n), "snap", {"bash", memslot, d, path_join(FNCOV, "test_snapshots"), d, NULL}, 0, 0, 0, 0, 0, 0};
        jobs[nj++] = (struct job){xstrdup(n), "play", {"bash", memslot, d, xasprintf("%s/tests/play_check.sh", NATIVE), d, FNCOV, NULL}, 0, 0, 0, 0, 0, 0};
    }
    double t0 = now_secs();
    int running = 0, next = 0;
    for (int i = 0; i < nj; ++i) {
        while (!jobs[i].finished) {
            while (running < jobsn && next < nj) {
                struct job *j = &jobs[next++];
                char *w = xasprintf("%s/%s/%s", WORK, j->n, j->tag);
                mkdirs(w);
                fflush(stdout);
                j->t0 = now_secs();
                pid_t pid = fork();
                if (!pid) {
                    setenv("NETHERITE_FNCOV", w, 1);
                    int nul = open("/dev/null", O_RDWR);
                    if (nul < 0 || chdir(NATIVE)) _exit(127);
                    dup2(nul, 0), dup2(nul, 1), dup2(nul, 2);
                    execvp(j->argv[0], j->argv);
                    _exit(127);
                }
                j->pid = pid, j->started = 1;
                ++running;
                free(w);
            }
            int st;
            pid_t p = waitpid(-1, &st, 0);
            if (p < 0) die("coverage: waitpid: %s", strerror(errno));
            for (int k = 0; k < nj; ++k)
                if (jobs[k].started && !jobs[k].finished && jobs[k].pid == p) {
                    jobs[k].rc = wait_rc(st), jobs[k].secs = now_secs() - jobs[k].t0, jobs[k].finished = 1;
                    --running;
                    break;
                }
        }
    }
    double wall = now_secs() - t0;

    static const char *const PROGS[3] = {"test_snapshots", "play", "play-dev"};
    for (int i = 0; i < 3; ++i) prog_syms(PROGS[i]);
    /* the universe: what the three programs' addresses name, and every function that ran */
    char *inuni = NULL;
    int uni_cap = 0;
#define MARK(id) do { if ((id) >= uni_cap) { int c_ = ((id) + 1) * 2; inuni = xrealloc(inuni, (size_t)c_); memset(inuni + uni_cap, 0, (size_t)(c_ - uni_cap)); uni_cap = c_; } inuni[id] = 1; } while (0)
    for (int i = 0; i < 3; ++i) {
        struct prog *pr = smap_get(&progs, PROGS[i]);
        for (int k = 0; k < pr->n; ++k)
            if (pr->where[k] >= 0) MARK(pr->where[k]);
    }
    /* ran: function -> recordings */
    struct sv *ran = NULL;
    int ran_cap = 0;
    for (int di = 0; di < dirs.n; ++di) {
        const char *n = base_name(dirs.v[di]);
        char *nd = path_join(WORK, n);
        struct sv tags = list_dir(nd, 0);
        for (int ti = 0; ti < tags.n; ++ti) {
            char *td = path_join(nd, tags.v[ti]);
            struct sv fs = list_dir(td, 0);
            for (int fi = 0; fi < fs.n; ++fi) {
                const char *dot = strchr(fs.v[fi], '.');
                if (!dot) die("coverage: an offsets file without a program: %s", fs.v[fi]);
                const char *prog = dot + 1;
                char *pp = path_join(FNCOV, prog);
                int have = path_exists(pp);
                free(pp);
                if (!have) continue;
                struct prog *pr = prog_syms(prog);
                char *p = path_join(td, fs.v[fi]);
                size_t len;
                char *data = read_file(p, &len);
                free(p);
                for (size_t i = 0; data && i + 8 <= len; i += 8) {
                    uint64_t off = 0;
                    for (int b = 7; b >= 0; --b) off = off << 8 | (unsigned char)data[i + (size_t)b];
                    uint64_t v = pr->start + off;
                    int lo = 0, hi = pr->n;
                    while (lo < hi) {
                        int mid = (lo + hi) / 2;
                        if (pr->addr[mid] <= v) lo = mid + 1;
                        else hi = mid;
                    }
                    int k = lo - 1;
                    if (k < 0 || pr->where[k] < 0) continue;
                    int id = pr->where[k];
                    MARK(id);
                    if (id >= ran_cap) {
                        int c = (id + 1) * 2;
                        ran = xrealloc(ran, (size_t)c * sizeof *ran);
                        memset(ran + ran_cap, 0, (size_t)(c - ran_cap) * sizeof *ran);
                        ran_cap = c;
                    }
                    int seen = 0;
                    for (int q = 0; q < ran[id].n; ++q) seen |= !strcmp(ran[id].v[q], n);
                    if (!seen) sv_push(&ran[id], (char *)n);
                }
                free(data);
            }
        }
    }
    MARK(fns.n);
    inuni[fns.n] = 0;
    if (fns.n > ran_cap) {
        ran = xrealloc(ran, (size_t)(fns.n + 1) * sizeof *ran);
        memset(ran + ran_cap, 0, (size_t)(fns.n + 1 - ran_cap) * sizeof *ran);
        ran_cap = fns.n + 1;
    }
    /* the index's native records */
    {
        char *idx = path_join(ROOT, "index/csrc");
        struct sv all = {0};
        walk_tree(idx, "", 0, &all, 0);
        for (int i = 0; i < all.n; ++i) {
            if (!ends_with(all.v[i], ".json")) continue;
            char *p = path_join(idx, all.v[i]);
            char *t = read_file(p, NULL);
            struct jval *o = t ? json_parse(t) : NULL;
            if (!o) die("coverage: %s is not JSON", p);
            const struct jval *pv = json_get(o, "path");
            smap_put(&records, pv && pv->kind == J_STR ? pv->str : "", o);
            free(p);
        }
    }
    for (int i = 0; i < dirs.n; ++i) {
        char *t = path_join(dirs.v[i], "tape.jsonl");
        FILE *f = fopen(t, "r");
        long long n = 0;
        int c, last = '\n';
        while (f && (c = fgetc(f)) != EOF) n += c == '\n', last = c;
        if (last != '\n') ++n;
        if (f) fclose(f);
        smap_put(&rows, base_name(dirs.v[i]), (void *)(intptr_t)(n - 1));
        free(t);
    }
    /* sorted(universe): by file, then function */
    int *order = xmalloc((size_t)(fns.n + 1) * sizeof *order);
    int nord = 0;
    for (int i = 0; i < fns.n; ++i)
        if (inuni[i]) order[nord++] = i;
    for (int a = 1; a < nord; ++a) {
        int x = order[a], b = a - 1;
        while (b >= 0 && strcmp(fns.keys[order[b]], fns.keys[x]) > 0) order[b + 1] = order[b], --b;
        order[b + 1] = x;
    }
    struct smap byfile = {0}, files_run = {0};
    for (int i = 0; i < fns.n; ++i)
        if (ran[i].n) {
            char *t = strchr(fns.keys[i], '\t');
            char *f = xstrndup(fns.keys[i], (size_t)(t - fns.keys[i]));
            smap_add(&files_run, f);
            free(f);
        }
    char *fo_p = path_join(OUT, "functions.tsv"), *uo_p = path_join(OUT, "unexecuted.tsv");
    FILE *fo = fopen(fo_p, "w"), *uo = fopen(uo_p, "w");
    if (!fo || !uo) die("coverage: cannot write into %s", OUT);
    fprintf(fo, "# file\tfunction\trecordings\tshortest recordings running it\n");
    fprintf(uo, "# file\tfunction\tfile layer\tentry point\tfile runs in any recording\tJava files the record ports\n");
    for (int oi = 0; oi < nord; ++oi) {
        int id = order[oi];
        char *t = strchr(fns.keys[id], '\t');
        char *f = xstrndup(fns.keys[id], (size_t)(t - fns.keys[id]));
        const char *fn = t + 1;
        int x = smap_add(&byfile, f);
        struct byfile *bf = byfile.vals[x];
        if (!bf) bf = byfile.vals[x] = xcalloc(1, sizeof *bf);
        bf->total += 1;
        if (ran[id].n) bf->run += 1;
        char *sh = shortest(&ran[id], 5);
        fprintf(fo, "%s\t%s\t%d\t%s\n", f, fn, ran[id].n, sh);
        free(sh);
        if (!ran[id].n) {
            const struct jval *o = smap_get(&records, f);
            char *pt = ports(o);
            fprintf(uo, "%s\t%s\t%s\t%s\t%s\t%s\n", f, fn, layer_of(o), o && o->nfields && entry(o, fn) ? "yes" : "no",
                    smap_has(&files_run, f) ? "yes" : "no", pt);
            free(pt);
        }
        free(f);
    }
    fclose(fo), fclose(uo);
    char *ro_p = path_join(OUT, "recs.tsv");
    FILE *ro = fopen(ro_p, "w");
    if (!ro) die("coverage: cannot write %s", ro_p);
    int nfail = 0;
    struct sv fails = {0};
    for (int i = 0; i < dirs.n; ++i) {
        const char *n = base_name(dirs.v[i]);
        struct jres r[2];
        int k = 0;
        for (int j = 0; j < nj; ++j)
            if (!strcmp(jobs[j].n, n)) r[k++] = (struct jres){jobs[j].tag, jobs[j].rc, jobs[j].secs};
        qsort(r, (size_t)k, sizeof *r, cmp_jres);
        fprintf(ro, "%s\t%s\t", n, dirs.v[i]);
        for (int q = 0; q < k; ++q) fprintf(ro, "%s%s:rc=%d:%.0fs", q ? " " : "", r[q].tag, r[q].rc, r[q].secs);
        fprintf(ro, "\n");
    }
    fclose(ro);
    /* fails: (n, tag, rc), in results' order (recordings in run order, jobs in theirs) */
    for (int j = 0; j < nj; ++j)
        if (jobs[j].rc) {
            ++nfail;
            sv_push(&fails, xasprintf("%s\t%s\t%011d", jobs[j].n, jobs[j].tag, jobs[j].rc + 1000000000));
        }
    sv_sort(&fails);
    long long tot = 0, run_ = 0;
    for (int i = 0; i < byfile.n; ++i) tot += ((struct byfile *)byfile.vals[i])->total, run_ += ((struct byfile *)byfile.vals[i])->run;
    struct sv lines = {0};
    sv_push(&lines, xasprintf("native coverage over %d recordings (%d jobs in %.0f s, %d failed)", dirs.n, nj, wall, nfail));
    for (int i = 0; i < fails.n && i < 20; ++i) {
        char *s = xstrdup(fails.v[i]), *t1 = strchr(s, '\t'), *t2 = strchr(t1 + 1, '\t');
        *t1 = *t2 = 0;
        sv_push(&lines, xasprintf("  failed: %s %s rc=%d (its functions still count)", s, t1 + 1, (int)(atoll(t2 + 1) - 1000000000)));
    }
    sv_push(&lines, xasprintf("functions run: %lld / %lld (%.1f%%)", run_, tot, 100.0 * (double)run_ / (double)(tot > 1 ? tot : 1)));
    struct sv layers = {0};
    for (int i = 0; i < byfile.n; ++i) sv_push(&layers, (char *)layer_of(smap_get(&records, byfile.keys[i])));
    sv_sort_uniq(&layers);
    for (int li = 0; li < layers.n; ++li) {
        long long r = 0, t = 0;
        int nf = 0;
        for (int i = 0; i < byfile.n; ++i)
            if (!strcmp(layer_of(smap_get(&records, byfile.keys[i])), layers.v[li])) {
                r += ((struct byfile *)byfile.vals[i])->run, t += ((struct byfile *)byfile.vals[i])->total, ++nf;
            }
        sv_push(&lines, xasprintf("  layer %-7s %5lld / %5lld functions in %d files", layers.v[li], r, t, nf));
    }
    int nent = 0;
    for (int id = 0; id < fns.n; ++id) {
        if (!inuni[id] || ran[id].n) continue;
        char *t = strchr(fns.keys[id], '\t');
        char *f = xstrndup(fns.keys[id], (size_t)(t - fns.keys[id]));
        nent += entry(smap_get(&records, f), t + 1);
        free(f);
    }
    sv_push(&lines, xasprintf("entry points (index/csrc entry_points) no recording runs: %d", nent));
    struct sv files = {0};
    for (int i = 0; i < byfile.n; ++i) sv_push(&files, byfile.keys[i]);
    sv_sort(&files);
    sv_push(&lines, xstrdup(""));
    sv_push(&lines, xstrdup("files none of whose functions any recording runs (file, functions, layer, Java it ports):"));
    for (int i = 0; i < files.n; ++i) {
        struct byfile *bf = smap_get(&byfile, files.v[i]);
        if (bf->run) continue;
        const struct jval *o = smap_get(&records, files.v[i]);
        sv_push(&lines, xasprintf("  %s %lld %s %s", files.v[i], bf->total, layer_of(o), ports(o)));
    }
    sv_push(&lines, xstrdup(""));
    sv_push(&lines, xstrdup("tick-layer files by functions no recording runs (file: run / total):"));
    {
        struct sv tf = {0};
        for (int i = 0; i < files.n; ++i) {
            struct byfile *bf = smap_get(&byfile, files.v[i]);
            if (!strcmp(layer_of(smap_get(&records, files.v[i])), "tick") && bf->run < bf->total) sv_push(&tf, files.v[i]);
        }
        for (int a = 1; a < tf.n; ++a) { /* by (run - total, file), stable over the sorted files */
            char *x = tf.v[a];
            struct byfile *bx = smap_get(&byfile, x);
            int b = a - 1;
            while (b >= 0) {
                struct byfile *by = smap_get(&byfile, tf.v[b]);
                long long ky = by->run - by->total, kx = bx->run - bx->total;
                if (ky > kx || (ky == kx && strcmp(tf.v[b], x) > 0)) tf.v[b + 1] = tf.v[b], --b;
                else break;
            }
            tf.v[b + 1] = x;
        }
        for (int i = 0; i < tf.n; ++i) {
            struct byfile *bf = smap_get(&byfile, tf.v[i]);
            sv_push(&lines, xasprintf("  %-36s %4lld / %4lld", tf.v[i], bf->run, bf->total));
        }
    }
    sv_push(&lines, xstrdup(""));
    sv_push(&lines, xstrdup("every file: run / total"));
    for (int i = 0; i < files.n; ++i) {
        struct byfile *bf = smap_get(&byfile, files.v[i]);
        sv_push(&lines, xasprintf("  %-36s %4lld / %4lld", files.v[i], bf->run, bf->total));
    }
    struct sb s = {0};
    for (int i = 0; i < lines.n; ++i) sb_printf(&s, "%s\n", lines.v[i]);
    char *sp = path_join(OUT, "summary.txt");
    if (write_file(sp, s.s, s.n)) die("coverage: cannot write %s", sp);
    int show = 12 + (nfail < 20 ? nfail : 20);
    for (int i = 0; i < lines.n && i < show; ++i) printf("%s\n", lines.v[i]);
    printf("coverage: %s/summary.txt, unexecuted.tsv, functions.tsv\n", OUT);
    return 0;
}
