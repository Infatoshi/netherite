/* ncustall: where a kernel's time goes, by instruction, from Nsight Compute's warp stall samples.
 *
 * Elliot's method as one command: find "the few instructions where 90 or more percent of the time is going", then
 * the reasons they wait. A report's source page (ncu --import R.ncu-rep --page source --csv --print-units base) has
 * every SASS instruction's stall samples, by reason; lanes summed it by hand or with throwaway Python
 * (lane/casthot's sasshot.py and sassstall.py, 2026-10-31). This reads it and prints:
 *
 *   - the total stall samples (and the not-issued ones) and the instructions, by function;
 *   - how many instructions hold 50% and 90% of the samples;
 *   - the top N instructions: share, cumulative share, not-issued share, average threads, function, address, the
 *     instruction, its source line (file:line, from the cuda,sass view) and its three largest reasons;
 *   - the samples by reason (ncu's stall_* columns, the issued and not-issued together) and by opcode.
 *
 * Given two reports (before and after), each is summarized and then the reasons and opcodes side by side: samples
 * and share in each, and the change.
 *
 *   ncustall [-n N] [-k REGEX] [--ncu PATH] A.ncu-rep|A.csv [B.ncu-rep|B.csv]
 *
 *   A.ncu-rep   ncu runs twice on it: --print-source sass (the samples, one row an instruction) and cuda,sass (the
 *               source lines; there an instruction inlined from a header is listed under each line it came through,
 *               so the samples are never summed from that view). The line shown is the first one outside the CUDA
 *               toolkit's headers, "+k" when the instruction came through k more lines of other files.
 *   A.csv       an export made beforehand (--print-source sass, or cuda,sass); from a sass export no lines.
 *   -n N        top instructions shown (default 20)
 *   -l N        top source lines shown (default 10; 0 none): each instruction's samples once, under its line. The
 *               cuda,sass view's own line rows overlap (inlined code is under each line it came through: base64's add
 *               up to 40,953 where the kernel's sample count, smsp__pcsamp_sample_count, is 38,570), so never sum them
 *   -k REGEX    only functions whose name matches (POSIX extended; default all: a kernel's own device
 *               subroutines, __cuda_sm20_div_rn_f64_full and the like, are functions of their own in the export)
 *   --ncu PATH  the ncu binary (default ncu on PATH, else /usr/local/cuda/bin/ncu). --import reads the report as
 *               any user; a report is made in a profile window (ncuwrap.sh through gpuq.sh ncu).
 *
 * Built as out/native/ncustall by csrc/Makefile. POSIX only (the Mac builds it; ncu there reads a copied report). */
#define _GNU_SOURCE
#define _DARWIN_C_SOURCE
#include "../csrc/tests/tool.h"

#include <regex.h>

#define NREASON_MAX 32

struct insn {
    char *fn, *addr, *src, *line; /* line: file:line or NULL */
    int nline;                    /* the other non-toolkit lines it came through */
    double st, ni, ex, th;
    double r[NREASON_MAX];
};

struct report {
    const char *path;
    struct insn *in;
    int n;
    char *reason[NREASON_MAX];
    int nreason;
    double total, total_ni;
    double line_rows_total; /* the cuda,sass view's line rows summed as ncu lists them (inlined code under each line) */
    struct smap linesrc;    /* file:line -> its text */
};

/* ------------------------------------------------------------ CSV */

/* one CSV record from *p (quoted fields, "" inside quotes, records that span lines); fields into *f; returns the
 * field count, -1 at the end */
static int csv_row(char **p, struct sv *f)
{
    for (int i = 0; i <f->n; ++i) free(f->v[i]);
    f->n = 0;
    char *s = *p;
    if (!*s) return -1;
    struct sb b = {0};
    for (;;) {
        b.n = 0;
        sb_str(&b);
        if (*s == '"') {
            ++s;
            for (;;) {
                if (!*s) break;
                if (*s == '"') {
                    if (s[1] == '"') sb_putc(&b, '"'), s += 2;
                    else {
                        ++s;
                        break;
                    }
                } else
                    sb_putc(&b, *s++);
            }
        }
        while (*s && *s != ',' && *s != '\n' && *s != '\r') sb_putc(&b, *s++);
        sv_push(f, xstrdup(sb_str(&b)));
        if (*s == ',') {
            ++s;
            continue;
        }
        while (*s == '\r') ++s;
        if (*s == '\n') ++s;
        break;
    }
    sb_free(&b);
    *p = s;
    return (int)f->n;
}

static double num(const char *s)
{
    char buf[64];
    size_t n = 0;
    for (; *s && n + 1 < sizeof buf; ++s)
        if (*s != ',' && *s != ' ') buf[n++] = *s;
    buf[n] = 0;
    if (!n || !strcmp(buf, "-")) return 0;
    return strtod(buf, NULL);
}

static int col(const struct sv *h, const char *name)
{
    for (int i = 0; i <h->n; ++i)
        if (!strcmp(h->v[i], name)) return (int)i;
    return -1;
}

/* the second header field named name (the cuda,sass view has two "Source" columns: the line's, the instruction's) */
static int col2(const struct sv *h, const char *name)
{
    int first = col(h, name);
    if (first < 0) return -1;
    for (int i = first + 1; i < h->n; ++i)
        if (!strcmp(h->v[i], name)) return (int)i;
    return first;
}

static int is_toolkit(const char *path)
{
    return starts_with(path, "/usr/local/cuda") || starts_with(path, "/usr/include") || starts_with(path, "/opt/nvidia") ||
           strstr(path, "/targets/x86_64-linux/include/") || strstr(path, "/targets/sbsa-linux/include/");
}

static char *run_ncu(const char *ncu, const char *rep, const char *view)
{
    char *argv[] = {(char *)ncu, "--import", (char *)rep, "--page", "source", "--csv", "--print-units", "base",
                    "--print-source", (char *)view, NULL};
    struct sb out = {0};
    int rc = run_cmd(argv, NULL, NULL, &out, 1, NULL);
    if (rc) {
        die("ncustall: %s --import %s --print-source %s: rc %d%s", ncu, rep, view, rc,
            rc == 127 ? " (no ncu: --ncu PATH)" : " (an old ncu, or not a report; on the dev host ncuwrap.sh makes them)");
    }
    return (char *)sb_str(&out);
}

/* the sass view: one row an instruction, a "Kernel Name" record before each function's */
static void parse_sass(struct report *R, char *text, regex_t *kre)
{
    struct sv f = {0}, h = {0};
    char *fn = NULL;
    int c_addr = -1, c_src = -1, c_st = -1, c_ni = -1, c_ex = -1, c_th = -1, c_r[NREASON_MAX];
    for (char *p = text; csv_row(&p, &f) >= 0;) {
        if (f.n >= 2 && !strcmp(f.v[0], "Kernel Name")) {
            free(fn);
            fn = xstrdup(f.v[1]);
            continue;
        }
        if (f.n && !strcmp(f.v[0], "Address")) {
            for (int i = 0; i <h.n; ++i) free(h.v[i]);
            h.n = 0;
            for (int i = 0; i <f.n; ++i) sv_push(&h, xstrdup(f.v[i]));
            c_addr = col(&h, "Address"), c_src = col(&h, "Source");
            c_st = col(&h, "Warp Stall Sampling (All Samples)"), c_ni = col(&h, "Warp Stall Sampling (Not-issued Samples)");
            c_ex = col(&h, "Instructions Executed"), c_th = col(&h, "Avg. Threads Executed");
            if (c_st < 0) die("ncustall: %s: no \"Warp Stall Sampling (All Samples)\" column (profile with --set full)", R->path);
            /* the reasons: stall_* without " (Not Issued)" (the all-samples counts, as the total is) */
            int k = 0;
            for (int i = 0; i <h.n; ++i) {
                if (!starts_with(h.v[i], "stall_") || strchr(h.v[i], ' ')) continue;
                if (k == NREASON_MAX) die("ncustall: more than %d stall reasons", NREASON_MAX);
                const char *name = h.v[i] + 6;
                int at = -1;
                for (int j = 0; j < R->nreason; ++j)
                    if (!strcmp(R->reason[j], name)) at = j;
                if (at < 0) at = R->nreason, R->reason[R->nreason++] = xstrdup(name);
                c_r[at] = (int)i, ++k;
            }
            continue;
        }
        if (c_addr < 0 || f.n != h.n || !starts_with(f.v[c_addr], "0x")) continue;
        if (kre && fn && regexec(kre, fn, 0, NULL, 0)) continue;
        R->in = xrealloc(R->in, sizeof *R->in * (size_t)(R->n + 1));
        struct insn *x = &R->in[R->n++];
        memset(x, 0, sizeof *x);
        x->fn = xstrdup(fn ? fn : "?");
        x->addr = xstrdup(f.v[c_addr]);
        x->src = xstrdup(strip(f.v[c_src]));
        x->st = num(f.v[c_st]);
        x->ni = c_ni >= 0 ? num(f.v[c_ni]) : 0;
        x->ex = c_ex >= 0 ? num(f.v[c_ex]) : 0;
        x->th = c_th >= 0 ? num(f.v[c_th]) : 0;
        for (int j = 0; j < R->nreason; ++j) x->r[j] = num(f.v[c_r[j]]);
    }
    free(fn);
}

/* the cuda,sass view: "File Path", "Function Name", a "Line No" header, then each source line and its instructions
 * (an instruction inlined through several lines is under each). Gives each instruction its line; when the export is
 * the only input (a csv), its instructions too, each counted once */
static void parse_lines(struct report *R, char *text, regex_t *kre, int take_insns)
{
    struct sv f = {0}, h = {0};
    char *file = NULL, *fn = NULL, *line = NULL;
    int c_line = -1, c_addr = -1, c_src = -1;
    struct smap seen = {0}; /* fn \t addr -> index + 1 */
    for (int i = 0; i < R->n; ++i) {
        char *k = xasprintf("%s\t%s", R->in[i].fn, R->in[i].addr);
        smap_put(&seen, k, (void *)(intptr_t)(i + 1));
        free(k);
    }
    int c_st = -1, c_ni = -1, c_ex = -1, c_th = -1, c_r[NREASON_MAX];
    for (char *p = text; csv_row(&p, &f) >= 0;) {
        if (f.n >= 2 && !strcmp(f.v[0], "File Path")) {
            free(file);
            file = xstrdup(f.v[1]);
            continue;
        }
        if (f.n >= 2 && !strcmp(f.v[0], "Function Name")) {
            free(fn);
            fn = xstrdup(f.v[1]);
            continue;
        }
        if (f.n && !strcmp(f.v[0], "Line No")) {
            for (int i = 0; i <h.n; ++i) free(h.v[i]);
            h.n = 0;
            for (int i = 0; i <f.n; ++i) sv_push(&h, xstrdup(f.v[i]));
            c_line = 0, c_addr = col(&h, "Address"), c_src = col2(&h, "Source");
            c_st = col(&h, "Warp Stall Sampling (All Samples)"), c_ni = col(&h, "Warp Stall Sampling (Not-issued Samples)");
            c_ex = col(&h, "Instructions Executed"), c_th = col(&h, "Avg. Threads Executed");
            if (take_insns) {
                for (int i = 0; i <h.n; ++i) {
                    if (!starts_with(h.v[i], "stall_") || strchr(h.v[i], ' ')) continue;
                    int at = -1;
                    for (int j = 0; j < R->nreason; ++j)
                        if (!strcmp(R->reason[j], h.v[i] + 6)) at = j;
                    if (at < 0) {
                        if (R->nreason == NREASON_MAX) die("ncustall: more than %d stall reasons", NREASON_MAX);
                        at = R->nreason, R->reason[R->nreason++] = xstrdup(h.v[i] + 6);
                    }
                    c_r[at] = (int)i;
                }
            }
            continue;
        }
        if (c_line < 0 || c_addr < 0 || !f.n) continue;
        if (f.v[c_line][0]) {
            /* a source line. ncu does not escape a quote inside its text (an asm string in the toolkit's __ldg),
             * so such a row splits into more fields: its numbers are read from the end, where they still are */
            free(line);
            const char *b = file ? strrchr(file, '/') : NULL;
            line = xasprintf("%s:%s", b ? b + 1 : file ? file : "?", f.v[c_line]);
            if (f.n >= h.n && c_st >= 0) R->line_rows_total += num(f.v[f.n - (h.n - c_st)]);
            if (f.n >= 2 && !smap_has(&R->linesrc, line)) smap_put(&R->linesrc, line, xstrdup(strip(f.v[1])));
            continue;
        }
        if (f.n != h.n) continue;
        if (!starts_with(f.v[c_addr], "0x")) continue;
        if (kre && fn && regexec(kre, fn, 0, NULL, 0)) continue;
        char *k = xasprintf("%s\t%s", fn ? fn : "?", f.v[c_addr]);
        intptr_t at = (intptr_t)smap_get(&seen, k);
        if (!at && take_insns) {
            R->in = xrealloc(R->in, sizeof *R->in * (size_t)(R->n + 1));
            struct insn *x = &R->in[R->n++];
            memset(x, 0, sizeof *x);
            x->fn = xstrdup(fn ? fn : "?");
            x->addr = xstrdup(f.v[c_addr]);
            x->src = xstrdup(strip(f.v[c_src]));
            x->st = c_st >= 0 ? num(f.v[c_st]) : 0;
            x->ni = c_ni >= 0 ? num(f.v[c_ni]) : 0;
            x->ex = c_ex >= 0 ? num(f.v[c_ex]) : 0;
            x->th = c_th >= 0 ? num(f.v[c_th]) : 0;
            for (int j = 0; j < R->nreason; ++j) x->r[j] = num(f.v[c_r[j]]);
            at = R->n;
            smap_put(&seen, k, (void *)at);
        }
        free(k);
        if (!at || !line) continue;
        struct insn *x = &R->in[at - 1];
        int tk = file && is_toolkit(file);
        if (!x->line) x->line = xstrdup(line), x->nline = tk ? -1 : 0; /* nline -1: the line is a toolkit header's */
        else if (!tk) {
            if (x->nline < 0) free(x->line), x->line = xstrdup(line), x->nline = 0;
            else if (strcmp(x->line, line)) ++x->nline;
        }
    }
    free(file), free(fn), free(line);
}

static void load(struct report *R, const char *path, const char *ncu, regex_t *kre)
{
    R->path = path;
    if (ends_with(path, ".csv")) {
        size_t len;
        char *s = read_file(path, &len);
        if (!s) die("ncustall: cannot read %s", path);
        int lines = strstr(s, "\"Line No\"") != NULL;
        if (lines) parse_lines(R, s, kre, 1);
        else parse_sass(R, s, kre);
        free(s);
    } else {
        char *s = run_ncu(ncu, path, "sass");
        parse_sass(R, s, kre);
        free(s);
        s = run_ncu(ncu, path, "cuda,sass");
        parse_lines(R, s, kre, 0);
        free(s);
    }
    if (!R->n) die("ncustall: %s: no instructions%s", path, kre ? " in the functions -k matches" : "");
    for (int i = 0; i < R->n; ++i) R->total += R->in[i].st, R->total_ni += R->in[i].ni;
}

/* ------------------------------------------------------------ output */

static const struct report *g_sort;
static int by_samples(const void *a, const void *b)
{
    const struct insn *x = &g_sort->in[*(const int *)a], *y = &g_sort->in[*(const int *)b];
    if (x->st != y->st) return x->st > y->st ? -1 : 1;
    return *(const int *)a - *(const int *)b;
}

static char *opcode(const char *src)
{
    const char *s = src;
    while (is_space(*s)) ++s;
    if (*s == '@') { /* the predicate: @P0, @!PT, @UP1 */
        while (*s && !is_space(*s)) ++s;
        while (is_space(*s)) ++s;
    }
    size_t n = 0;
    while (s[n] && !is_space(s[n]) && s[n] != '.') ++n;
    return xstrndup(s, n);
}

static char *thousands(double x)
{
    char raw[48], out[64];
    snprintf(raw, sizeof raw, "%.0f", x);
    int n = (int)strlen(raw), neg = raw[0] == '-', o = 0;
    for (int i = 0; i < n; ++i) {
        out[o++] = raw[i];
        int left = n - 1 - i;
        if (left && left % 3 == 0 && !(neg && i == 0)) out[o++] = ',';
    }
    out[o] = 0;
    return xstrdup(out);
}

struct kv {
    char *k;
    double v;
};
static int kv_desc(const void *a, const void *b)
{
    const struct kv *x = a, *y = b;
    return x->v != y->v ? (x->v > y->v ? -1 : 1) : strcmp(x->k, y->k);
}

static void add_kv(struct kv **a, int *n, const char *k, double v)
{
    for (int i = 0; i < *n; ++i)
        if (!strcmp((*a)[i].k, k)) {
            (*a)[i].v += v;
            return;
        }
    *a = xrealloc(*a, sizeof **a * (size_t)(*n + 1));
    (*a)[(*n)++] = (struct kv){xstrdup(k), v};
}

static void reasons_of(const struct report *R, struct kv **a, int *n)
{
    for (int j = 0; j < R->nreason; ++j) {
        double s = 0;
        for (int i = 0; i < R->n; ++i) s += R->in[i].r[j];
        add_kv(a, n, R->reason[j], s);
    }
    qsort(*a, (size_t)*n, sizeof **a, kv_desc);
}

static void opcodes_of(const struct report *R, struct kv **a, int *n)
{
    for (int i = 0; i < R->n; ++i) {
        char *op = opcode(R->in[i].src);
        add_kv(a, n, op, R->in[i].st);
        free(op);
    }
    qsort(*a, (size_t)*n, sizeof **a, kv_desc);
}

static void summary(const struct report *R, int top, int nlines)
{
    char *t = thousands(R->total), *tn = thousands(R->total_ni);
    printf("%s: %d instructions, %s stall samples (%s not issued)\n", R->path, R->n, t, tn);
    free(t), free(tn);
    struct kv *fns = NULL;
    int nf = 0;
    for (int i = 0; i < R->n; ++i) add_kv(&fns, &nf, R->in[i].fn, R->in[i].st);
    qsort(fns, (size_t)nf, sizeof *fns, kv_desc);
    for (int i = 0; i < nf; ++i) {
        int k = 0;
        for (int j = 0; j < R->n; ++j) k += !strcmp(R->in[j].fn, fns[i].k);
        char *s = thousands(fns[i].v);
        printf("  %5.1f%%  %10s  %5d instructions  %.90s\n", R->total ? 100 * fns[i].v / R->total : 0, s, k, fns[i].k);
        free(s);
    }
    int *ord = xmalloc(sizeof *ord * (size_t)R->n);
    for (int i = 0; i < R->n; ++i) ord[i] = i;
    g_sort = R;
    qsort(ord, (size_t)R->n, sizeof *ord, by_samples);
    int h50 = 0, h90 = 0, sampled = 0;
    double c = 0;
    for (int i = 0; i < R->n; ++i) {
        if (R->in[ord[i]].st > 0) ++sampled;
        c += R->in[ord[i]].st;
        if (!h50 && c >= 0.5 * R->total) h50 = i + 1;
        if (!h90 && c >= 0.9 * R->total) h90 = i + 1;
    }
    printf("50%% of the samples are in %d instructions, 90%% in %d (of %d with any; %d in all)\n", h50, h90, sampled, R->n);
    printf("top %d: share, cumulative, not issued, threads, function, address, instruction, line, top reasons\n", top);
    c = 0;
    for (int i = 0; i < top && i < R->n; ++i) {
        const struct insn *x = &R->in[ord[i]];
        c += x->st;
        int rk[3] = {-1, -1, -1};
        for (int j = 0; j < R->nreason; ++j) {
            if (x->r[j] <= 0) continue;
            for (int q = 0; q < 3; ++q)
                if (rk[q] < 0 || x->r[j] > x->r[rk[q]]) {
                    for (int z = 2; z > q; --z) rk[z] = rk[z - 1];
                    rk[q] = j;
                    break;
                }
        }
        char reasons[160] = "", fnshort[16];
        for (int q = 0; q < 3 && rk[q] >= 0; ++q) {
            size_t L = strlen(reasons);
            snprintf(reasons + L, sizeof reasons - L, "%s%s %.0f%%", q ? ", " : "", R->reason[rk[q]],
                     x->st ? 100 * x->r[rk[q]] / x->st : 0);
        }
        snprintf(fnshort, sizeof fnshort, "%.14s", x->fn);
        char *lp = strchr(fnshort, '(');
        if (lp) *lp = 0;
        char loc[96];
        if (!x->line) snprintf(loc, sizeof loc, "-");
        else if (x->nline > 0) snprintf(loc, sizeof loc, "%s +%d", x->line, x->nline);
        else snprintf(loc, sizeof loc, "%s", x->line);
        printf("  %5.2f%% %5.1f%%  ni %3.0f%%  thr %4.1f  %-14s %s  %-40.40s  %-22s  %s\n", R->total ? 100 * x->st / R->total : 0,
               R->total ? 100 * c / R->total : 0, x->st ? 100 * x->ni / x->st : 0, x->th, fnshort, x->addr, x->src, loc, reasons);
    }
    free(ord);
    if (nlines > 0 && R->linesrc.n) {
        /* each instruction once, under its line (the first outside the toolkit's headers) */
        struct kv *ls = NULL;
        int nl = 0;
        double un = 0;
        for (int i = 0; i < R->n; ++i) {
            if (R->in[i].line) add_kv(&ls, &nl, R->in[i].line, R->in[i].st);
            else un += R->in[i].st;
        }
        qsort(ls, (size_t)nl, sizeof *ls, kv_desc);
        char *lt = thousands(R->line_rows_total), *u = thousands(un);
        printf("top %d source lines (each instruction once, under its line; %s samples on instructions with no line). The\n"
               "cuda,sass view's line rows add up to %s: code inlined through several lines is listed under each\n",
               nlines, u, lt);
        free(lt), free(u);
        c = 0;
        for (int i = 0; i < nl && i < nlines; ++i) {
            c += ls[i].v;
            const char *txt = smap_get(&R->linesrc, ls[i].k);
            printf("  %5.2f%% %5.1f%%  %-22s  %.90s\n", R->total ? 100 * ls[i].v / R->total : 0, R->total ? 100 * c / R->total : 0,
                   ls[i].k, txt ? txt : "");
        }
    }
    struct kv *rs = NULL, *ops = NULL;
    int nr = 0, no = 0;
    reasons_of(R, &rs, &nr);
    double rsum = 0;
    for (int i = 0; i < nr; ++i) rsum += rs[i].v;
    printf("by reason (%s samples over the reasons):\n", thousands(rsum));
    for (int i = 0; i < nr && rs[i].v > 0; ++i) {
        char *s = thousands(rs[i].v);
        printf("  %-20s %10s  %5.1f%%\n", rs[i].k, s, rsum ? 100 * rs[i].v / rsum : 0);
        free(s);
    }
    opcodes_of(R, &ops, &no);
    printf("by opcode:");
    for (int i = 0; i < no && i < 16 && ops[i].v > 0; ++i) printf("%s %s %.1f%%", i ? "," : "", ops[i].k, R->total ? 100 * ops[i].v / R->total : 0);
    printf("\n");
}

static void compare(const char *title, struct kv *a, int na, double ta, struct kv *b, int nb, double tb, int limit)
{
    printf("%s, before -> after (samples, share; the change in samples):\n", title);
    struct kv *u = NULL;
    int nu = 0;
    for (int i = 0; i < na; ++i) add_kv(&u, &nu, a[i].k, a[i].v);
    for (int i = 0; i < nb; ++i) add_kv(&u, &nu, b[i].k, 0); /* keep before's order, after-only ones last */
    int shown = 0;
    for (int i = 0; i < nu && shown < limit; ++i) {
        double va = 0, vb = 0;
        for (int j = 0; j < na; ++j)
            if (!strcmp(a[j].k, u[i].k)) va = a[j].v;
        for (int j = 0; j < nb; ++j)
            if (!strcmp(b[j].k, u[i].k)) vb = b[j].v;
        if (!va && !vb) continue;
        char *sa = thousands(va), *sb2 = thousands(vb), *sd = thousands(vb - va);
        printf("  %-20s %10s %5.1f%%  -> %10s %5.1f%%   %s%s\n", u[i].k, sa, ta ? 100 * va / ta : 0, sb2, tb ? 100 * vb / tb : 0,
               vb - va > 0 ? "+" : "", sd);
        free(sa), free(sb2), free(sd);
        ++shown;
    }
}

static void usage(void)
{
    fprintf(stderr, "usage: ncustall [-n N] [-l N] [-k REGEX] [--ncu PATH] A.ncu-rep|A.csv [B.ncu-rep|B.csv]\n"
                    "(the header of tools/ncustall.c has the whole of it)\n");
    exit(2);
}

int main(int argc, char **argv)
{
    int top = 20, nlines = 10;
    const char *kpat = NULL, *ncu = NULL;
    struct sv in = {0};
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (!strcmp(a, "-n") && i + 1 < argc) top = atoi(argv[++i]);
        else if (!strcmp(a, "-l") && i + 1 < argc) nlines = atoi(argv[++i]);
        else if (!strcmp(a, "-k") && i + 1 < argc) kpat = argv[++i];
        else if (!strcmp(a, "--ncu") && i + 1 < argc) ncu = argv[++i];
        else if (a[0] == '-') usage();
        else sv_push(&in, xstrdup(a));
    }
    if (in.n < 1 || in.n > 2) usage();
    if (!ncu) {
        struct sb o = {0};
        char *w[] = {"sh", "-c", "command -v ncu", NULL};
        ncu = run_cmd(w, NULL, NULL, &o, 1, NULL) == 0 && o.n ? xstrdup(strip((char *)sb_str(&o))) : "/usr/local/cuda/bin/ncu";
    }
    regex_t kre;
    if (kpat && regcomp(&kre, kpat, REG_EXTENDED | REG_NOSUB)) die("ncustall: -k %s: not a regex", kpat);
    struct report R[2];
    memset(R, 0, sizeof R);
    for (int i = 0; i <in.n; ++i) {
        load(&R[i], in.v[i], ncu, kpat ? &kre : NULL);
        if (i) printf("\n");
        summary(&R[i], top, nlines);
    }
    if (in.n == 2) {
        char *ta = thousands(R[0].total), *tb = thousands(R[1].total), *td = thousands(R[1].total - R[0].total);
        printf("\nbefore %s, after %s: %s samples -> %s (%s%s, %+.1f%%); instructions %d -> %d\n", R[0].path, R[1].path, ta, tb,
               R[1].total > R[0].total ? "+" : "", td, R[0].total ? 100 * (R[1].total - R[0].total) / R[0].total : 0, R[0].n,
               R[1].n);
        struct kv *ra = NULL, *rb = NULL, *oa = NULL, *ob = NULL;
        int nra = 0, nrb = 0, noa = 0, nob = 0;
        reasons_of(&R[0], &ra, &nra), reasons_of(&R[1], &rb, &nrb);
        double sa = 0, sb2 = 0;
        for (int i = 0; i < nra; ++i) sa += ra[i].v;
        for (int i = 0; i < nrb; ++i) sb2 += rb[i].v;
        compare("by reason", ra, nra, sa, rb, nrb, sb2, 32);
        opcodes_of(&R[0], &oa, &noa), opcodes_of(&R[1], &ob, &nob);
        compare("by opcode", oa, noa, R[0].total, ob, nob, R[1].total, 16);
    }
    return 0;
}
