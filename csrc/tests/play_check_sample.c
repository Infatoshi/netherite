/* make play-check-sample: rewrite tests/play_check.list's cover section.
 *
 *     out/native/play_check_sample --store S --data D
 *         [--times OUT/suite-times.txt] [--list tests/play_check.list]
 *
 * The suite's play checks (tests/play_check.sh, PLAYCHECK=sample in the
 * Makefile) run on the list's recordings and on every recording with a
 * clientview.jsonl or cwhash.txt. The list's head, up to the "# cover" line, is
 * kept by hand: each recording a play-only failure was found on. This program
 * rewrites what follows it from the test selection's records (S, make
 * test-map's store: each job's executed functions): for each snapshot
 * recording, the functions its play check runs in play or play-dev that its
 * test_snapshots job does not run; then, greedily by time, recordings whose
 * play checks together run every one of those functions, after the kept ones
 * and the clientview and cwhash ones. A job's time is its last suite run's
 * (OUT/suite-times.txt, make test writes it), else its tape's size. Run it
 * after make test-map when recordings or client code were added.
 * (The C port of tests/play_check_sample.py, lane/cport.) */
#define _GNU_SOURCE
#include "tool.h"

static const char *store, *data, *times = "", *list = "tests/play_check.list";

/* function names as ids; a set of them as a bitset */
static struct smap fnames;
static int nwords;

typedef uint64_t *bits;

struct table {
    int n;
    char **prog;
    int *fn;
};
static struct smap tables;

static struct table *table(const char *tid)
{
    struct table *t = smap_get(&tables, tid);
    if (t) return t;
    t = xcalloc(1, sizeof *t);
    char *p = xasprintf("%s/names/%s.txt", store, tid);
    char *text = read_file(p, NULL);
    if (!text) die("play_check_sample: cannot read %s", p);
    int cap = 0;
    for (char *l = text; *l;) {
        char *e = strchr(l, '\n');
        if (e) *e = 0;
        char *sp = strchr(l, ' ');
        if (t->n == cap) {
            cap = cap ? cap * 2 : 1024;
            t->prog = xrealloc(t->prog, (size_t)cap * sizeof *t->prog);
            t->fn = xrealloc(t->fn, (size_t)cap * sizeof *t->fn);
        }
        if (sp) *sp = 0;
        t->prog[t->n] = l;
        t->fn[t->n] = smap_add(&fnames, sp ? sp + 1 : "");
        ++t->n;
        if (!e) break;
        l = e + 1;
    }
    smap_put(&tables, tid, t);
    free(p);
    return t;
}

/* a record's functions in the given programs, as name ids */
struct ids {
    int *v, n;
};

static struct ids functions(const char *rec, const char *const *programs)
{
    struct ids out = {0};
    char *text = read_file(rec, NULL);
    if (!text) return out;
    char *tid = NULL, *bitstr = NULL;
    for (char *l = text; *l;) {
        char *e = strchr(l, '\n');
        if (e) *e = 0;
        char *f[3] = {0};
        if (starts_with(l, "names ")) {
            int n = split_ws(l, f, 3);
            if (n > 1) tid = f[1];
        } else if (starts_with(l, "bits ")) {
            int n = split_ws(l, f, 3);
            bitstr = n > 1 ? f[1] : "";
            break;
        }
        if (!e) break;
        l = e + 1;
    }
    if (!tid || !bitstr) return out;
    struct table *t = table(tid);
    int cap = 0;
    for (int i = 0; bitstr[i]; ++i) {
        char ch = bitstr[i];
        int v = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : 0;
        for (int k = 0; k < 4; ++k) {
            int j = i * 4 + k;
            if (!(v & (8 >> k)) || j >= t->n) continue;
            int ok = 0;
            for (int p = 0; programs[p]; ++p) ok |= !strcmp(t->prog[j], programs[p]);
            if (!ok) continue;
            if (out.n == cap) cap = cap ? cap * 2 : 256, out.v = xrealloc(out.v, (size_t)cap * sizeof *out.v);
            out.v[out.n++] = t->fn[j];
        }
    }
    return out;
}

static bits to_bits(struct ids s)
{
    bits b = xcalloc((size_t)nwords, sizeof *b);
    for (int i = 0; i < s.n; ++i) b[s.v[i] / 64] |= 1ULL << (s.v[i] % 64);
    return b;
}

static int popc(const uint64_t *b)
{
    int n = 0;
    for (int i = 0; i < nwords; ++i) n += __builtin_popcountll(b[i]);
    return n;
}

static int in_list(const struct sv *a, const char *s)
{
    for (int i = 0; i < a->n; ++i)
        if (!strcmp(a->v[i], s)) return 1;
    return 0;
}

/* the entries of dir in directory order (os.listdir's, which glob keeps), no hidden */
static struct sv dir_order(const char *dir)
{
    struct sv out = {0};
    DIR *d = opendir(dir);
    if (!d) return out;
    struct dirent *e;
    while ((e = readdir(d)))
        if (e->d_name[0] != '.') sv_push(&out, xstrdup(e->d_name));
    closedir(d);
    return out;
}

static double mtime(const char *p)
{
    struct stat st;
    if (stat(p, &st)) return 0;
#ifdef __APPLE__
    return (double)st.st_mtimespec.tv_sec + st.st_mtimespec.tv_nsec * 1e-9;
#else
    return (double)st.st_mtim.tv_sec + st.st_mtim.tv_nsec * 1e-9;
#endif
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) {
        const char **dst = NULL;
        const char *a = argv[i], *v = NULL;
        if (starts_with(a, "--store")) dst = &store;
        else if (starts_with(a, "--data")) dst = &data;
        else if (starts_with(a, "--times")) dst = &times;
        else if (starts_with(a, "--list")) dst = &list;
        else die("usage: play_check_sample --store S --data D [--times FILE] [--list FILE]");
        const char *eq = strchr(a, '=');
        if (eq) v = eq + 1;
        else if (i + 1 < argc) v = argv[++i];
        else die("play_check_sample: %s needs a value", a);
        *dst = v;
    }
    if (!store || !data) die("usage: play_check_sample --store S --data D [--times FILE] [--list FILE]");

    /* the newest record of each job */
    struct smap newest = {0};
    double *newm = NULL;
    int newcap = 0;
    struct sv dirs = dir_order(store);
    for (int i = 0; i < dirs.n; ++i) {
        char *d = path_join(store, dirs.v[i]);
        if (!is_dir(d)) { free(d); continue; }
        struct sv fs = dir_order(d);
        for (int k = 0; k < fs.n; ++k) {
            if (!ends_with(fs.v[k], ".rec")) continue;
            char *rec = path_join(d, fs.v[k]);
            FILE *f = fopen(rec, "r");
            char l1[4096], l2[8192];
            if (!f || !fgets(l1, sizeof l1, f) || !fgets(l2, sizeof l2, f)) {
                if (f) fclose(f);
                free(rec);
                continue;
            }
            fclose(f);
            char *sp = strchr(l2, ' ');
            if (!sp) { free(rec); continue; }
            char *job = strip(sp + 1);
            double m = mtime(rec);
            int x = smap_find(&newest, job);
            if (x < 0) {
                x = smap_add(&newest, job);
                if (x >= newcap) newcap = (x + 1) * 2, newm = xrealloc(newm, (size_t)newcap * sizeof *newm);
                newm[x] = m;
                newest.vals[x] = rec;
            } else if (newm[x] < m) {
                newm[x] = m;
                newest.vals[x] = rec;
            } else free(rec);
        }
        free(d);
    }
    static const char *const PLAY[] = {"play", "play-dev", NULL}, *const TS[] = {"test_snapshots", NULL};
    struct smap playi = {0}, tsi = {0};
    for (int x = 0; x < newest.n; ++x) {
        char *job = xstrdup(newest.keys[x]), *f[4];
        int n = split_ws(job, f, 4);
        if (n != 3 || !starts_with(f[1], "snapshots/") || strcmp(f[2], "-")) continue;
        char *name = f[1] + 10, *sl = strchr(name, '/');
        if (sl) *sl = 0;
        if (!strcmp(f[0], "play_check.sh")) {
            struct ids *s = xmalloc(sizeof *s);
            *s = functions(newest.vals[x], PLAY);
            smap_put(&playi, name, s);
        } else if (!strcmp(f[0], "test_snapshots")) {
            struct ids *s = xmalloc(sizeof *s);
            *s = functions(newest.vals[x], TS);
            smap_put(&tsi, name, s);
        }
    }
    nwords = (fnames.n + 63) / 64 + 1;
    char *snaps = path_join(data, "snapshots");
    struct sv live = {0};
    {
        struct sv es = list_dir(snaps, 1);
        for (int i = 0; i < es.n; ++i) {
            char *t = xasprintf("%s/%s/tape.jsonl", snaps, es.v[i]);
            if (is_file(t)) sv_push(&live, es.v[i]);
            free(t);
        }
    }
    struct sv both = {0};
    for (int i = 0; i < live.n; ++i)
        if (smap_has(&playi, live.v[i]) && smap_has(&tsi, live.v[i])) sv_push(&both, live.v[i]);
    if (!both.n) {
        fprintf(stderr, "play_check_sample: no records of play checks in %s (make test-map)\n", store);
        return 1;
    }
    struct smap play = {0};
    for (int i = 0; i < playi.n; ++i) smap_put(&play, playi.keys[i], to_bits(*(struct ids *)playi.vals[i]));
    bits only = xcalloc((size_t)nwords, sizeof *only);
    for (int i = 0; i < both.n; ++i) {
        bits p = smap_get(&play, both.v[i]), t = to_bits(*(struct ids *)smap_get(&tsi, both.v[i]));
        for (int w = 0; w < nwords; ++w) only[w] |= p[w] & ~t[w];
        free(t);
    }
    int nonly = popc(only);

    struct smap cost = {0}, timed = {0};
    if (times[0] && path_exists(times)) {
        char *text = read_file(times, NULL);
        for (char *l = text; l && *l;) {
            char *e = strchr(l, '\n');
            if (e) *e = 0;
            char *f[64] = {0};
            int n = split_ws(l, f, 64);
            if (n >= 4 && !strcmp(f[2], "play_check.sh")) {
                char *d = f[3];
                size_t k = strlen(d);
                while (k && d[k - 1] == '/') d[--k] = 0;
                const char *name = base_name(d);
                smap_put(&cost, name, (void *)(intptr_t)atoll(f[0]));
                smap_put(&timed, name, (void *)1);
            }
            if (!e) break;
            l = e + 1;
        }
    }
    for (int i = 0; i < live.n; ++i)
        if (!smap_has(&cost, live.v[i])) {
            char *t = xasprintf("%s/%s/tape.jsonl", snaps, live.v[i]);
            struct stat st;
            stat(t, &st);
            smap_put(&cost, live.v[i], (void *)(intptr_t)(st.st_size / 2000));
            free(t);
        }
#define COST(r) ((long long)(intptr_t)smap_get(&cost, (r)))

    size_t llen;
    char *ltext = read_file(list, &llen);
    if (!ltext) die("play_check_sample: cannot read %s", list);
    struct sv lines = {0};
    for (char *l = ltext;;) {
        char *e = strchr(l, '\n');
        if (e) *e = 0;
        sv_push(&lines, l);
        if (!e) break;
        l = e + 1;
    }
    int cut = lines.n;
    for (int i = 0; i < lines.n; ++i)
        if (starts_with(lines.v[i], "# cover")) { cut = i; break; }
    struct sv kept = {0};
    for (int i = 0; i < cut; ++i) {
        char *t = xstrdup(lines.v[i]), *h = strchr(t, '#'), *f[2] = {0};
        if (h) *h = 0;
        if (split_ws(t, f, 1)) sv_push(&kept, f[0]);
    }
    struct sv own = {0};
    for (int i = 0; i < live.n; ++i) {
        char *a = xasprintf("%s/%s/clientview.jsonl", snaps, live.v[i]), *b = xasprintf("%s/%s/cwhash.txt", snaps, live.v[i]);
        if (path_exists(a) || path_exists(b)) sv_push(&own, live.v[i]);
        free(a), free(b);
    }
    bits covered = xcalloc((size_t)nwords, sizeof *covered);
    for (int pass = 0; pass < 2; ++pass) {
        struct sv *s = pass ? &own : &kept;
        for (int i = 0; i < s->n; ++i) {
            bits p = smap_get(&play, s->v[i]);
            if (p)
                for (int w = 0; w < nwords; ++w) covered[w] |= p[w] & only[w];
        }
    }
    int first = popc(covered);
    struct sv chosen = {0};
    for (;;) {
        const char *best = NULL;
        double score = 0.0;
        for (int i = 0; i < both.n; ++i) {
            const char *r = both.v[i];
            if (in_list(&kept, r) || in_list(&own, r) || in_list(&chosen, r)) continue;
            bits p = smap_get(&play, r);
            int g = 0;
            for (int w = 0; w < nwords; ++w) g += __builtin_popcountll(p[w] & only[w] & ~covered[w]);
            double s = (double)g / (double)(COST(r) + 500);
            if (g && s > score) best = r, score = s;
        }
        if (!best) break;
        sv_push(&chosen, (char *)best);
        bits p = smap_get(&play, best);
        for (int w = 0; w < nwords; ++w) covered[w] |= p[w] & only[w];
    }
    struct sv missing = {0};
    for (int x = 0; x < fnames.n; ++x)
        if ((only[x / 64] >> (x % 64) & 1) && !(covered[x / 64] >> (x % 64) & 1)) sv_push(&missing, fnames.keys[x]);
    sv_sort(&missing);

    struct sb out = {0};
    for (int i = 0; i < cut; ++i) sb_printf(&out, "%s\n", lines.v[i]);
    sb_printf(&out, "# cover: written by make play-check-sample. Over %d recordings' records, play runs %d\n", both.n, nonly);
    sb_puts(&out, "# functions test_snapshots does not run on the same recording; the recordings above and the\n");
    sb_printf(&out, "# %d with a clientview.jsonl or cwhash.txt run %d of them, these the rest.\n", own.n, first);
    for (int i = 0; i < chosen.n; ++i) {
        const char *r = chosen.v[i];
        bits p = smap_get(&play, r);
        int k = 0;
        for (int w = 0; w < nwords; ++w) k += __builtin_popcountll(p[w] & only[w]);
        char t[64];
        if (smap_has(&timed, r)) snprintf(t, sizeof t, "%lld ms", COST(r));
        else snprintf(t, sizeof t, "no time");
        sb_printf(&out, "%-22s # runs %d of them, %s\n", r, k, t);
    }
    if (write_file(list, out.s, out.n)) die("play_check_sample: cannot write %s", list);

    long long tot = 0, run = 0;
    for (int i = 0; i < live.n; ++i) tot += COST(live.v[i]);
    struct smap uniq = {0};
    struct sv *groups[3] = {&kept, &own, &chosen};
    for (int g = 0; g < 3; ++g)
        for (int i = 0; i < groups[g]->n; ++i)
            if (!smap_has(&uniq, groups[g]->v[i])) {
                smap_add(&uniq, groups[g]->v[i]);
                run += smap_has(&cost, groups[g]->v[i]) ? COST(groups[g]->v[i]) : 0;
            }
    printf("play-check-sample: %d recordings (%d kept, %d with clientview or cwhash, %d cover), %d of %d play-only"
           " functions run, about %.0f of %.0f play-check seconds\n",
           uniq.n, kept.n, own.n, chosen.n, nonly - missing.n, nonly, run / 1000.0, tot / 1000.0);
    if (missing.n) {
        printf("play-check-sample: no recording runs");
        for (int i = 0; i < missing.n; ++i) printf(" %s", missing.v[i]);
        printf("\n");
        return 1;
    }
    return 0;
}
