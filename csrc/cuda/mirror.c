/* The mirror rule (GPU design 2026-09-28, part 6): which device gates a C
 * change can reach.
 *
 * csrc/cuda/mirror.tsv names every device kernel, the C it mirrors (roots
 * whose closure over direct calls and macros is the mirrored set, cuts where
 * the closure stops, files whose any change counts) and its gates (build,
 * quick, full). This program parses csrc/engine at two revisions into
 * top-level items (functions, function-like and object macros, tables and
 * other variables, enum constants; comments and whitespace do not count),
 * finds the items whose text changed, and names every kernel whose mirrored
 * set holds one, with the build and quick gate commands to run on the device.
 *
 *   out/native/mirror [--base REV] [--head REV|--worktree]
 *       [--run [--keep-going]] [--full] [--check] [--closure KERNEL] [--verify-ci OBJDIR] [--manifest FILE]
 *
 * --base defaults to master, --head to HEAD (--worktree: the files on disk).
 * Prints one line per kernel (GATES or clear, with the changed items that
 * reach it) and then the commands. --run runs them from the repository root,
 * each through bash, and exits 1 at the first failure (the merge trial's use).
 * --full: every kernel's full gates, whatever changed (the nightly's list).
 * --check: every root and cut names an item of the head tree, every file
 * pattern matches, every gate's make target exists (exit 1 otherwise).
 * --closure K: the mirrored set of kernel K, one item a line.
 * --verify-ci DIR: the parser's function ranges against gcc's
 * -fcallgraph-info=su start lines (make callgraph's objects, out/native/cg/obj):
 * each function the .ci files define must start inside the parsed range of the
 * item of that name in that file. The repository is the one holding the
 * current directory (else the one this program was built in). make -C csrc
 * mirror-check and mirror-gates run it. The C port of mirror.py (lane/cport):
 * the same items, closures, checks and output; its regular expressions are
 * the matchers below (the source is ASCII outside comments; a comment's
 * character is one blank, as Python's str had it). */
#define _GNU_SOURCE
#include <glob.h>
#include "../tests/tool.h"

static char ROOT[4096], NATIVE[4200], MANIFEST[4300];

static const char *const KEYWORDS[] = {
    "auto", "break", "case", "char", "const", "continue", "default", "do", "double", "else", "enum", "extern", "float",
    "for", "goto", "if", "inline", "int", "long", "register", "restrict", "return", "short", "signed", "sizeof",
    "static", "struct", "switch", "typedef", "union", "unsigned", "void", "volatile", "while", "_Bool",
    "_Static_assert", "__attribute__", "__restrict", "__inline", "__typeof__", "typeof", "_Alignas", "_Thread_local", NULL};

static int is_kw(const char *s, size_t n)
{
    for (int i = 0; KEYWORDS[i]; ++i)
        if (strlen(KEYWORDS[i]) == n && !memcmp(KEYWORDS[i], s, n)) return 1;
    return 0;
}

static int W(unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c >= 0x80; }
static int A(unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c >= 0x80; }
static int D(unsigned char c) { return c >= '0' && c <= '9'; }
static int S(unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v' || (c >= 0x1c && c <= 0x1f); }

/* ------------------------------------------------------------ strings with a length */

struct str {
    const char *s;
    size_t n;
};

static struct str sl(const char *s, size_t a, size_t b) { return (struct str){s + a, b > a ? b - a : 0}; }

static size_t lskip(struct str t, size_t i)
{
    while (i < t.n && S((unsigned char)t.s[i])) ++i;
    return i;
}

static size_t rstripn(struct str t)
{
    size_t n = t.n;
    while (n && S((unsigned char)t.s[n - 1])) --n;
    return n;
}

static long find_ch(struct str t, char c, long from)
{
    if (from < 0) from += (long)t.n;
    if (from < 0) from = 0;
    for (size_t i = (size_t)from; i < t.n; ++i)
        if (t.s[i] == c) return (long)i;
    return -1;
}

static long find_str(struct str t, const char *p, long from)
{
    if (from < 0) from += (long)t.n;
    if (from < 0) from = 0;
    size_t k = strlen(p);
    for (size_t i = (size_t)from; i + k <= t.n; ++i)
        if (!memcmp(t.s + i, p, k)) return (long)i;
    return -1;
}

/* Python's s[a:b] */
static struct str pyslice(struct str t, long a, long b)
{
    long n = (long)t.n;
    if (a < 0) a += n;
    if (b < 0) b += n;
    if (a < 0) a = 0;
    if (b < 0) b = 0;
    if (a > n) a = n;
    if (b > n) b = n;
    return (struct str){t.s + a, b > a ? (size_t)(b - a) : 0};
}

/* IDENT.findall: [A-Za-z_]\w* scanning left to right */
typedef void (*ident_fn)(void *ctx, const char *s, size_t n);
static void idents(struct str t, ident_fn fn, void *ctx)
{
    for (size_t i = 0; i < t.n;) {
        if (A((unsigned char)t.s[i])) {
            size_t j = i;
            while (j < t.n && W((unsigned char)t.s[j])) ++j;
            fn(ctx, t.s + i, j - i);
            i = j;
        } else ++i;
    }
}

/* ------------------------------------------------------------ cleaning */

/* (nocomm, masked): comments blanked (newlines kept); masked also blanks
 * string and character literal contents */
static void clean(const char *text, size_t n, char **nocomm, char **mask, size_t *len)
{
    struct sb o = {0}, m = {0};
    size_t i = 0;
    while (i < n) {
        char c = text[i];
        if (c == '/' && i + 1 < n && (text[i + 1] == '*' || text[i + 1] == '/')) {
            size_t j;
            if (text[i + 1] == '*') {
                const char *e = memmem(text + i + 2, n - i - 2 > 0 ? n - i - 2 : 0, "*/", 2);
                j = e ? (size_t)(e - text) + 2 : n;
            } else {
                const char *e = memchr(text + i, '\n', n - i);
                j = e ? (size_t)(e - text) : n;
            }
            for (size_t k = i; k < j; ++k) {
                if (text[k] == '\n') sb_putc(&o, '\n'), sb_putc(&m, '\n');
                else if (((unsigned char)text[k] & 0xc0) != 0x80) sb_putc(&o, ' '), sb_putc(&m, ' ');
            }
            i = j;
        } else if (c == '"' || c == '\'') {
            size_t j = i + 1;
            while (j < n && text[j] != c && text[j] != '\n') j += text[j] == '\\' ? 2 : 1;
            j = j + 1 < n ? j + 1 : n;
            sb_putn(&o, text + i, j - i);
            if (j - i >= 2) {
                sb_putc(&m, c);
                for (size_t k = i + 1; k < j - 1; ++k) sb_putc(&m, text[k] == '\n' ? '\n' : '_');
                sb_putc(&m, c);
            } else sb_putn(&m, text + i, j - i);
            i = j;
        } else {
            sb_putc(&o, c), sb_putc(&m, c);
            ++i;
        }
    }
    *nocomm = (char *)sb_str(&o);
    *mask = (char *)sb_str(&m);
    *len = m.n;
}

/* norm: the tokens joined by single spaces */
static char *norm(struct str t)
{
    struct sb b = {0};
    size_t i = 0, n = t.n;
    const char *s = t.s;
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        size_t j = i;
        if (A(c)) {
            j = i + 1;
            while (j < n && W((unsigned char)s[j])) ++j;
        } else if (D(c)) {
            j = i + 1;
            while (j < n && (W((unsigned char)s[j]) || s[j] == '.')) ++j;
        } else if (c == '"' || c == '\'') {
            size_t k = i + 1;
            int ok = 0;
            while (k < n) {
                if (s[k] == '\\') {
                    if (k + 1 < n && s[k + 1] != '\n') { k += 2; continue; }
                    break;
                }
                if (s[k] == (char)c) { ok = 1; break; }
                ++k;
            }
            j = ok ? k + 1 : i + 1;
        } else if (!S(c)) j = i + 1;
        if (j == i) { ++i; continue; }
        if (b.n) sb_putc(&b, ' ');
        sb_putn(&b, s + i, j - i);
        i = j;
    }
    return (char *)sb_str(&b);
}

/* s without its __attribute__((...)) groups */
static char *strip_attr(struct str t)
{
    char *s = xstrndup(t.s, t.n);
    for (;;) {
        size_t n = strlen(s);
        struct str v = {s, n};
        long k = find_str(v, "__attribute__", 0);
        if (k < 0) return s;
        long j = find_ch(v, '(', k);
        if (j < 0) { s[k] = 0; return s; }
        long d = 0, e;
        for (e = j; e < (long)n; ++e) {
            d += s[e] == '(';
            d -= s[e] == ')';
            if (d == 0) break;
        }
        if (e == (long)n) e = (long)n - 1;
        for (long q = k; q <= e; ++q) s[q] = ' ';
    }
}

/* ------------------------------------------------------------ items */

struct item {
    char *kind, *name, *file;
    int start, end, is_static;
    struct sv ids; /* sorted, unique */
    char *norm;
};

struct items {
    struct item *v;
    int n, cap;
};

static void ids_add(void *ctx, const char *s, size_t n)
{
    struct sv *a = ctx;
    if (is_kw(s, n)) return;
    sv_push(a, xstrndup(s, n));
}

static void ids_add_all(void *ctx, const char *s, size_t n) { sv_push(ctx, xstrndup(s, n)); }

struct parse {
    const char *path;
    const char *nocomm, *m;
    size_t n;
    int *line_at;
    struct items *out;
};

static void add(struct parse *P, const char *kind, const char *name, size_t a, size_t b, long body_a, int st)
{
    struct item it = {0};
    it.kind = xstrdup(kind);
    it.name = xstrdup(name);
    it.file = xstrdup(P->path);
    struct str mm = {P->m, P->n};
    struct str body = pyslice(mm, body_a >= 0 ? body_a : (long)a, (long)b);
    idents(body, ids_add, &it.ids);
    sv_sort_uniq(&it.ids);
    int k = 0;
    for (int i = 0; i < it.ids.n; ++i)
        if (strcmp(it.ids.v[i], name)) it.ids.v[k++] = it.ids.v[i];
    it.ids.n = k;
    size_t a2 = a < P->n ? a : P->n;
    long e = (long)b - 1;
    if (e < (long)a) e = (long)a;
    if (e > (long)P->n) e = (long)P->n;
    it.start = P->line_at[a2];
    it.end = P->line_at[e];
    it.norm = norm(pyslice((struct str){P->nocomm, P->n}, (long)a, (long)b));
    it.is_static = st;
    struct items *o = P->out;
    if (o->n == o->cap) o->cap = o->cap ? o->cap * 2 : 256, o->v = xrealloc(o->v, (size_t)o->cap * sizeof *o->v);
    o->v[o->n++] = it;
}

/* re.match(r'\s*KW\b') with KW one of the words */
static int starts_kw(struct str t, const char *kw)
{
    size_t i = lskip(t, 0), k = strlen(kw);
    if (i + k > t.n || memcmp(t.s + i, kw, k)) return 0;
    return i + k == t.n || !W((unsigned char)t.s[i + k]);
}

/* split at depth-0 commas, ([{ and )]} counted */
static int split_top(struct str t, struct str *parts, int max)
{
    int n = 0, depth = 0;
    size_t cur = 0;
    for (size_t i = 0; i < t.n; ++i) {
        char ch = t.s[i];
        if (ch == '(' || ch == '[' || ch == '{') ++depth;
        else if (ch == ')' || ch == ']' || ch == '}') --depth;
        if (ch == ',' && depth == 0) {
            if (n < max) parts[n++] = sl(t.s, cur, i);
            cur = i + 1;
        }
    }
    if (n < max) parts[n++] = sl(t.s, cur, t.n);
    return n;
}


struct idl {
    struct sv v;
};

static void ids_nokw(void *ctx, const char *s, size_t n)
{
    struct idl *l = ctx;
    if (!is_kw(s, n)) sv_push(&l->v, xstrndup(s, n));
}

/* a declaration's items: enum constants, variables and tables */
static void decl(struct parse *P, struct str text, size_t a, size_t b, int st)
{
    char *t0 = strip_attr(text);
    struct str tt = {t0, strlen(t0)};
    size_t s0 = lskip(tt, 0), s1 = rstripn(tt);
    struct str t = s1 > s0 ? sl(t0, s0, s1) : (struct str){t0, 0};
    if (!t.n || (t.n == 1 && t.s[0] == ';')) { free(t0); return; }
    int is_enum = 0;
    if (t.n >= 4 && !memcmp(t.s, "enum", 4) && (t.n == 4 || !W((unsigned char)t.s[4]))) is_enum = 1;
    else if (t.n >= 7 && !memcmp(t.s, "typedef", 7)) {
        size_t i = 7;
        if (i < t.n && S((unsigned char)t.s[i])) {
            i = lskip(t, i);
            if (i + 4 <= t.n && !memcmp(t.s + i, "enum", 4) && (i + 4 == t.n || !W((unsigned char)t.s[i + 4]))) is_enum = 1;
        }
    }
    if (is_enum) {
        struct str body = {t.s, 0};
        long o = find_ch(t, '{', 0);
        if (o >= 0) {
            long c = -1;
            for (long i = (long)t.n - 1; i >= 0; --i)
                if (t.s[i] == '}') { c = i; break; }
            body = pyslice(t, o + 1, c);
        }
        struct str parts[4096];
        int np = split_top(body, parts, 4096);
        for (int i = 0; i < np; ++i) {
            struct str p = parts[i];
            size_t q0 = lskip(p, 0), q1 = rstripn(p);
            if (q1 <= q0 || !A((unsigned char)p.s[q0])) continue;
            size_t e = q0;
            while (e < q1 && W((unsigned char)p.s[e])) ++e;
            char *nm = xstrndup(p.s + q0, e - q0);
            add(P, "enum", nm, a, b, -1, st);
            free(nm);
        }
        free(t0);
        return;
    }
    /* brace blocks (a struct's fields, an initializer) collapsed */
    struct sb bb = {0};
    int depth = 0;
    for (size_t i = 0; i < t.n; ++i) {
        char ch = t.s[i];
        if (ch == '{') {
            if (depth == 0) sb_puts(&bb, "{}");
            ++depth;
        } else if (ch == '}') --depth;
        else if (depth == 0) sb_putc(&bb, ch);
    }
    struct str body = {sb_str(&bb), bb.n};
    /* typedef\b, or (struct|union)\s*\w*\s*(\{\})?\s*;$ : a type */
    int is_type = body.n >= 7 && !memcmp(body.s, "typedef", 7) && (body.n == 7 || !W((unsigned char)body.s[7]));
    if (!is_type) {
        size_t k = 0;
        if (body.n >= 6 && !memcmp(body.s, "struct", 6)) k = 6;
        else if (body.n >= 5 && !memcmp(body.s, "union", 5)) k = 5;
        if (k) {
            size_t i = lskip(body, k);
            while (i < body.n && W((unsigned char)body.s[i])) ++i;
            i = lskip(body, i);
            if (i + 1 < body.n && body.s[i] == '{' && body.s[i + 1] == '}') i = lskip(body, i + 2);
            if (i < body.n && body.s[i] == ';' && (i + 1 == body.n || (i + 2 == body.n && body.s[i + 1] == '\n'))) is_type = 1;
        }
    }
    if (is_type) { sb_free(&bb); free(t0); return; }
    struct str parts[4096];
    int np = split_top(body, parts, 4096);
    for (int k = 0; k < np; ++k) {
        struct str p = parts[k];
        long eq = find_ch(p, '=', 0);
        struct str q = eq >= 0 ? sl(p.s, 0, (size_t)eq) : p;
        /* [^(]*?([A-Za-z_]\w*)\s*\( : a prototype unless (* follows */
        long pp = find_ch(q, '(', 0);
        if (pp >= 0) {
            size_t e = (size_t)pp;
            while (e > 0 && S((unsigned char)q.s[e - 1])) --e;
            size_t w = e;
            while (w > 0 && W((unsigned char)q.s[w - 1])) --w;
            while (w < e && !A((unsigned char)q.s[w])) ++w;
            if (w < e && !is_kw(q.s + w, e - w)) {
                int fptr = (size_t)pp + 1 < q.n && q.s[pp + 1] == '*';
                if (!fptr) continue; /* a prototype */
            }
        }
        /* \(\s*\*\s*([A-Za-z_]\w*) : a function pointer variable */
        int done = 0;
        for (size_t i = 0; i < q.n && !done; ++i) {
            if (q.s[i] != '(') continue;
            size_t j = lskip(q, i + 1);
            if (j >= q.n || q.s[j] != '*') continue;
            j = lskip(q, j + 1);
            if (j >= q.n || !A((unsigned char)q.s[j])) continue;
            size_t e = j;
            while (e < q.n && W((unsigned char)q.s[e])) ++e;
            char *nm = xstrndup(q.s + j, e - j);
            add(P, "var", nm, a, b, -1, st);
            free(nm);
            done = 1;
        }
        if (done) continue;
        if (pp >= 0) continue; /* a prototype */
        long br = find_ch(q, '[', 0);
        struct str q2 = br >= 0 ? sl(q.s, 0, (size_t)br) : q;
        struct idl l = {{0}};
        idents(q2, ids_nokw, &l);
        if (l.v.n && (k > 0 || l.v.n >= 2)) add(P, "var", l.v.v[l.v.n - 1], a, b, -1, st);
    }
    sb_free(&bb);
    free(t0);
}

/* the top-level items of one C file */
static void parse(const char *path, const char *text, size_t tn, struct items *out)
{
    char *nocomm, *m;
    size_t n;
    clean(text, tn, &nocomm, &m, &n);
    int *line_at = xmalloc((n + 1) * sizeof *line_at);
    int ln = 1;
    for (size_t k = 0; k < n; ++k) {
        line_at[k] = ln;
        if (m[k] == '\n') ++ln;
    }
    line_at[n] = ln;
    struct parse P = {path, nocomm, m, n, line_at, out};
    struct str M = {m, n};
    size_t i = 0;
    while (i < n) {
        char c = m[i];
        if (S((unsigned char)c)) { ++i; continue; }
        if (c == '#') {
            size_t j = i, e;
            for (;;) {
                const char *q = memchr(m + j, '\n', n - j);
                if (!q) { e = n; break; }
                e = (size_t)(q - m);
                if (e > 0 && m[e - 1] == '\\') { j = e + 1; continue; }
                break;
            }
            /* #\s*define\s+([A-Za-z_]\w*) */
            size_t p = lskip(sl(m, 0, e), i + 1);
            if (p + 6 <= e && !memcmp(m + p, "define", 6) && p + 6 < e && S((unsigned char)m[p + 6])) {
                size_t q = lskip(sl(m, 0, e), p + 6);
                if (q < e && A((unsigned char)m[q])) {
                    size_t w = q;
                    while (w < e && W((unsigned char)m[w])) ++w;
                    char *name = xstrndup(m + q, w - q);
                    add(&P, "macro", name, i, e, (long)w, ends_with(path, ".c"));
                    free(name);
                }
            }
            i = e + 1;
            continue;
        }
        /* a statement: up to ';' at depth 0, or a function body */
        size_t j = i;
        int depth = 0;
        const char *kind = NULL;
        while (j < n) {
            char ch = m[j];
            if (ch == '(' || ch == '[') ++depth;
            else if (ch == ')' || ch == ']') --depth;
            else if (ch == '{' && depth == 0) {
                struct str head = sl(m, i, j);
                size_t hr = rstripn(head);
                long lp = find_ch(head, '(', 0);
                int is_fn = hr && head.s[hr - 1] == ')' && lp >= 0 && !starts_kw(head, "typedef");
                if (is_fn)
                    for (long q = 0; q < lp; ++q)
                        if (head.s[q] == '=') { is_fn = 0; break; }
                if (is_fn) kind = "fn";
                int d2 = 0;
                size_t k = j;
                while (k < n) {
                    if (m[k] == '{') ++d2;
                    else if (m[k] == '}') {
                        --d2;
                        if (d2 == 0) break;
                    }
                    ++k;
                }
                j = k + 1;
                if (kind) break;
                continue;
            } else if (ch == ';' && depth == 0) {
                ++j;
                break;
            } else if (ch == '\n' && depth == 0) {
                /* a directive ends the statement; so does a whole top-level
                 * macro call on its line (CHUNK_NIBBLE_SETTER(chunk_set_sky, sky)) */
                size_t q = j + 1;
                while (q < n && (m[q] == ' ' || m[q] == '\t')) ++q;
                int directive = q < n && m[q] == '#';
                int call = 0;
                if (!directive) {
                    struct str h = sl(m, i, j);
                    size_t p = lskip(h, 0);
                    if (p < h.n && A((unsigned char)h.s[p])) {
                        while (p < h.n && W((unsigned char)h.s[p])) ++p;
                        p = lskip(h, p);
                        size_t hr = rstripn(h);
                        if (p < h.n && h.s[p] == '(' && hr > p + 1 && h.s[hr - 1] == ')') {
                            size_t r = q;
                            while (r < n && S((unsigned char)m[r])) ++r;
                            call = !(r < n && (m[r] == '{' || m[r] == ';'));
                        }
                    }
                }
                if (directive || call) {
                    kind = "call";
                    break;
                }
            }
            ++j;
        }
        struct str text_ = pyslice(M, (long)i, (long)j);
        int st = starts_kw(text_, "static") || starts_kw(text_, "inline");
        if (!st) {
            size_t p = lskip(text_, 0);
            if (p + 6 <= text_.n && !memcmp(text_.s + p, "extern", 6) && p + 6 < text_.n && S((unsigned char)text_.s[p + 6])) {
                size_t q = lskip(text_, p + 6);
                if (q + 6 <= text_.n && !memcmp(text_.s + q, "inline", 6) && (q + 6 == text_.n || !W((unsigned char)text_.s[q + 6])))
                    st = 1;
            }
        }
        if (!st) {
            long lp = find_ch(text_, '(', 0);
            struct str pre = lp >= 0 ? sl(text_.s, 0, (size_t)lp) : text_;
            st = find_str(pre, "static ", 0) >= 0;
        }
        if (kind && !strcmp(kind, "fn")) {
            long br = find_ch(text_, '{', 0);
            char *head = strip_attr(pyslice(text_, 0, br));
            struct str H = {head, strlen(head)};
            long p = find_ch(H, '(', 0);
            struct sv nmv = {0};
            idents(pyslice(H, 0, p), ids_add_all, &nmv);
            /* \(\s*([A-Za-z_]\w*)\s*\)\s*\( on head[p:] */
            struct str hp = pyslice(H, p, (long)H.n);
            char *paren = NULL;
            if (hp.n && hp.s[0] == '(') {
                size_t q = lskip(hp, 1);
                if (q < hp.n && A((unsigned char)hp.s[q])) {
                    size_t w = q;
                    while (w < hp.n && W((unsigned char)hp.s[w])) ++w;
                    size_t r = lskip(hp, w);
                    if (r < hp.n && hp.s[r] == ')') {
                        r = lskip(hp, r + 1);
                        if (r < hp.n && hp.s[r] == '(') paren = xstrndup(hp.s + q, w - q);
                    }
                }
            }
            if (paren) {
                struct str pre = pyslice(H, 0, p);
                size_t pr = rstripn(pre);
                int star = pr && pre.s[pr - 1] == '*';
                if (!nmv.n || is_kw(nmv.v[nmv.n - 1], strlen(nmv.v[nmv.n - 1])) || star) {
                    nmv.n = 0;
                    sv_push(&nmv, paren); /* void (nbt_put)(...): the name kept from a macro */
                }
            }
            if (nmv.n) add(&P, "fn", nmv.v[nmv.n - 1], i, j, -1, st);
            free(head);
        } else if (kind) {
            /* a function the macro makes, named by its first argument:
             * \s*([A-Za-z_]\w*)\s*\(\s*([A-Za-z_]\w*) and the text ends with ')' */
            size_t p = lskip(text_, 0);
            if (p < text_.n && A((unsigned char)text_.s[p])) {
                while (p < text_.n && W((unsigned char)text_.s[p])) ++p;
                p = lskip(text_, p);
                if (p < text_.n && text_.s[p] == '(') {
                    p = lskip(text_, p + 1);
                    size_t hr = rstripn(text_);
                    if (p < text_.n && A((unsigned char)text_.s[p]) && hr && text_.s[hr - 1] == ')') {
                        size_t w = p;
                        while (w < text_.n && W((unsigned char)text_.s[w])) ++w;
                        char *name = xstrndup(text_.s + p, w - p);
                        add(&P, "fn", name, i, j, -1, 1);
                        free(name);
                    }
                }
            }
        } else decl(&P, text_, i, j, st);
        i = j > i ? j : i + 1;
    }
    free(line_at);
    free(nocomm);
    free(m);
}

/* ------------------------------------------------------------ a tree */

struct tree {
    struct smap items;   /* "file:name" -> struct item * (same-named items of one file merged) */
    struct smap by_name; /* name -> struct items * (pointers' list: v of items' addresses) */
};

struct ilist {
    struct item **v;
    int n;
};

static void sv_union(struct sv *a, const struct sv *b)
{
    for (int i = 0; i < b->n; ++i) sv_push(a, xstrdup(b->v[i]));
    sv_sort_uniq(a);
}

static void tree_add_file(struct tree *T, const char *f, const char *text, size_t n)
{
    struct items its = {0};
    parse(f, text, n, &its);
    for (int i = 0; i < its.n; ++i) {
        struct item *it = &its.v[i];
        char *k = xasprintf("%s:%s", it->file, it->name);
        struct item *o = smap_get(&T->items, k);
        if (o) {
            /* the same name twice in a file (#if branches, a macro redefined): one item */
            sv_union(&o->ids, &it->ids);
            char *nn = xasprintf("%s %s", o->norm, it->norm);
            free(o->norm);
            o->norm = nn;
            if (it->start < o->start) o->start = it->start;
            if (it->end > o->end) o->end = it->end;
            if (!strcmp(it->kind, "fn")) o->kind = it->kind;
        } else {
            struct item *c = xmalloc(sizeof *c);
            *c = *it;
            smap_put(&T->items, k, c);
            struct ilist *l = smap_get(&T->by_name, c->name);
            if (!l) l = xcalloc(1, sizeof *l), smap_put(&T->by_name, c->name, l);
            l->v = xrealloc(l->v, (size_t)(l->n + 1) * sizeof *l->v);
            l->v[l->n++] = c;
        }
        free(k);
    }
    free(its.v);
}

static char *git_out(char *const *argv, const char *in)
{
    struct sb out = {0};
    int rc = run_cmd(argv, NULL, NULL, &out, 0, in);
    if (rc) {
        fprintf(stderr, "mirror: git failed (rc %d):", rc);
        for (int i = 0; argv[i]; ++i) fprintf(stderr, " %s", argv[i]);
        fprintf(stderr, "\n");
        exit(1);
    }
    return (char *)sb_str(&out);
}

/* the files' texts at rev (NULL: the working tree); files a revision lacks are left out */
static void tree_read(struct tree *T, const char *rev, const struct sv *files)
{
    if (!rev) {
        for (int i = 0; i < files->n; ++i) {
            char *p = xasprintf("%s/%s", NATIVE, files->v[i]);
            size_t n;
            char *t = read_file(p, &n);
            if (t) tree_add_file(T, files->v[i], t, n);
            free(t);
            free(p);
        }
        return;
    }
    struct sb in = {0};
    for (int i = 0; i < files->n; ++i) sb_printf(&in, "%s:csrc/%s\n", rev, files->v[i]);
    char *argv[] = {"git", "-C", ROOT, "cat-file", "--batch", NULL};
    struct sb out = {0};
    if (run_cmd(argv, NULL, NULL, &out, 0, sb_str(&in))) die("mirror: git cat-file failed");
    size_t pos = 0;
    for (int i = 0; i < files->n; ++i) {
        char *e = memchr(out.s + pos, '\n', out.n - pos);
        if (!e) die("mirror: git cat-file output ends early");
        char *hdr = xstrndup(out.s + pos, (size_t)(e - (out.s + pos))), *f[4] = {0};
        int nf = split_ws(hdr, f, 4);
        if (nf && !strcmp(f[nf - 1], "missing")) {
            pos = (size_t)(e - out.s) + 1;
            free(hdr);
            continue;
        }
        size_t size = nf > 2 ? (size_t)strtoull(f[2], NULL, 10) : 0;
        size_t at = (size_t)(e - out.s) + 1;
        tree_add_file(T, files->v[i], out.s + at, size);
        pos = at + size + 1;
        free(hdr);
    }
    sb_free(&out);
    sb_free(&in);
}

static struct sv list_files(const char *rev)
{
    struct sv out = {0};
    if (!rev) {
        static const char *const G[2] = {"engine/*.c", "engine/*.h"};
        for (int g = 0; g < 2; ++g) {
            char *pat = xasprintf("%s/%s", NATIVE, G[g]);
            glob_t gl;
            if (!glob(pat, 0, NULL, &gl))
                for (size_t i = 0; i < gl.gl_pathc; ++i) sv_push(&out, xstrdup(gl.gl_pathv[i] + strlen(NATIVE) + 1));
            globfree(&gl);
            free(pat);
        }
        sv_sort(&out);
        return out;
    }
    char *argv[] = {"git", "-C", ROOT, "ls-tree", "-r", "--name-only", (char *)rev, "--", "csrc/engine", NULL};
    char *o = git_out(argv, NULL), *f;
    for (char *s = strtok_r(o, " \t\n\r\f\v", &f); s; s = strtok_r(NULL, " \t\n\r\f\v", &f))
        if (glob_match("csrc/engine/*.[ch]", s)) sv_push(&out, xstrdup(s + 5));
    sv_sort(&out);
    return out;
}

static struct tree *tree_new(const char *rev)
{
    struct tree *T = xcalloc(1, sizeof *T);
    struct sv files = list_files(rev);
    tree_read(T, rev, &files);
    return T;
}

/* the items a use of name in fromfile reaches */
static struct ilist resolve(struct tree *T, const char *name, const char *fromfile)
{
    struct ilist *c = smap_get(&T->by_name, name), out = {0};
    if (!c) return out;
    out.v = xmalloc((size_t)c->n * sizeof *out.v);
    for (int i = 0; i < c->n; ++i)
        if (!strcmp(c->v[i]->file, fromfile)) out.v[out.n++] = c->v[i];
    if (out.n) return out;
    for (int i = 0; i < c->n; ++i)
        if (ends_with(c->v[i]->file, ".h")) out.v[out.n++] = c->v[i];
    if (out.n) return out;
    /* another file's: a function the header's prototype names (a variable or a macro of a .c file is its own) */
    for (int i = 0; i < c->n; ++i)
        if (!c->v[i]->is_static && !strcmp(c->v[i]->kind, "fn")) out.v[out.n++] = c->v[i];
    return out;
}

static int in_sv(const struct sv *a, const char *s)
{
    for (int i = 0; i < a->n; ++i)
        if (!strcmp(a->v[i], s)) return 1;
    return 0;
}

/* the closure of roots over idents, stopping at cuts: sorted keys */
static struct sv closure(struct tree *T, const struct sv *roots, const struct sv *cuts)
{
    struct smap seen = {0};
    struct sv work = {0};
    for (int i = 0; i < roots->n; ++i)
        if (smap_has(&T->items, roots->v[i]) && !in_sv(cuts, roots->v[i]) && !smap_has(&seen, roots->v[i]))
            smap_add(&seen, roots->v[i]), sv_push(&work, roots->v[i]);
    while (work.n) {
        struct item *it = smap_get(&T->items, work.v[--work.n]);
        if (!strcmp(it->kind, "var") || !strcmp(it->kind, "enum")) continue;
        for (int i = 0; i < it->ids.n; ++i) {
            struct ilist r = resolve(T, it->ids.v[i], it->file);
            for (int k = 0; k < r.n; ++k) {
                char *key = xasprintf("%s:%s", r.v[k]->file, r.v[k]->name);
                if (smap_has(&seen, key) || in_sv(cuts, key)) { free(key); continue; }
                smap_add(&seen, key);
                sv_push(&work, smap_get(&seen, key) ? key : key);
            }
            free(r.v);
        }
    }
    struct sv out = {0};
    for (int i = 0; i < seen.n; ++i) sv_push(&out, seen.keys[i]);
    sv_sort(&out);
    return out;
}

/* ------------------------------------------------------------ the manifest */

enum { K_WHAT, K_ROOT, K_CUT, K_FILE, K_KEPT, K_BUILD, K_EDIT, K_QUICK, K_FULL, NKEYS };
static const char *const KEYNAMES[NKEYS] = {"what", "root", "cut", "file", "kept", "build", "edit", "quick", "full"};

struct kern {
    struct sv v[NKEYS];
};

static struct smap kernels; /* name -> struct kern *, in the manifest's order */

static void load_manifest(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) die("mirror: cannot read %s", path);
    char *line = NULL;
    size_t cap = 0;
    ssize_t len;
    int ln = 0;
    while ((len = getline(&line, &cap, f)) >= 0) {
        ++ln;
        if (len && line[len - 1] == '\n') line[--len] = 0;
        char *p = line;
        while (S((unsigned char)*p)) ++p;
        if (!*p || *p == '#') continue;
        /* line.split(None, 2) */
        char *parts[3];
        int np = 0;
        char *q = line;
        while (np < 3) {
            while (*q && S((unsigned char)*q)) ++q;
            if (!*q) break;
            if (np == 2) { parts[np++] = q; break; }
            parts[np++] = q;
            while (*q && !S((unsigned char)*q)) ++q;
            if (*q) *q++ = 0;
        }
        if (np < 3) {
            fprintf(stderr, "mirror.tsv:%d: want KERNEL KEY VALUE\n", ln);
            exit(1);
        }
        struct kern *k = smap_get(&kernels, parts[0]);
        if (!k) k = xcalloc(1, sizeof *k), smap_put(&kernels, parts[0], k);
        int key = -1;
        for (int i = 0; i < NKEYS; ++i)
            if (!strcmp(KEYNAMES[i], parts[1])) key = i;
        if (key < 0) {
            fprintf(stderr, "mirror.tsv:%d: unknown key %s\n", ln, parts[1]);
            exit(1);
        }
        sv_push(&k->v[key], xstrdup(strip(parts[2])));
    }
    free(line);
    fclose(f);
}

/* the kernel's mirrored item keys and how they were found */
static struct sv mirrored(struct tree *T, struct kern *d, char **how)
{
    for (int i = 0; i < d->v[K_KEPT].n; ++i) {
        char *p = xasprintf("%s/%s", ROOT, d->v[K_KEPT].v[i]);
        char *text = path_exists(p) ? read_file(p, NULL) : NULL;
        free(p);
        if (!text) continue;
        struct smap names = {0};
        for (char *l = text; *l;) {
            char *e = strchr(l, '\n');
            if (e) *e = 0;
            char *s = strip(l);
            if (*s) {
                char *dollar = strrchr(s, '$');
                smap_add(&names, dollar ? dollar + 1 : s);
            }
            if (!e) break;
            l = e + 1;
        }
        struct sv out = {0};
        for (int k = 0; k < T->items.n; ++k) {
            struct item *it = T->items.vals[k];
            if (!strcmp(it->kind, "fn") && smap_has(&names, it->name)) sv_push(&out, T->items.keys[k]);
        }
        sv_sort(&out);
        *how = xasprintf("kept %s", d->v[K_KEPT].v[i]);
        return out;
    }
    *how = "roots";
    return closure(T, &d->v[K_ROOT], &d->v[K_CUT]);
}

static struct sv changed_files(const char *base, const char *head)
{
    char *argv[] = {"git", "-C", ROOT, "diff", "--name-only", (char *)base, (char *)head, "--", "csrc", NULL};
    if (!head) argv[6] = "--", argv[7] = "csrc", argv[8] = NULL;
    char *o = git_out(argv, NULL), *f;
    struct sv out = {0};
    for (char *s = strtok_r(o, " \t\n\r\f\v", &f); s; s = strtok_r(NULL, " \t\n\r\f\v", &f))
        sv_push(&out, xstrdup(strlen(s) > 5 ? s + 5 : ""));
    return out;
}

static struct sv changed_items(const char *base, const char *head, const struct sv *files)
{
    struct sv src = {0}, out = {0};
    for (int i = 0; i < files->n; ++i)
        if (glob_match("engine/*.[ch]", files->v[i])) sv_push(&src, files->v[i]);
    if (!src.n) return out;
    struct tree *a = xcalloc(1, sizeof *a), *b = xcalloc(1, sizeof *b);
    tree_read(a, base, &src);
    tree_read(b, head, &src);
    struct smap all = {0};
    for (int i = 0; i < a->items.n; ++i) smap_add(&all, a->items.keys[i]);
    for (int i = 0; i < b->items.n; ++i) smap_add(&all, b->items.keys[i]);
    for (int i = 0; i < all.n; ++i) {
        struct item *ia = smap_get(&a->items, all.keys[i]), *ib = smap_get(&b->items, all.keys[i]);
        if (!ia || !ib || strcmp(ia->norm, ib->norm)) sv_push(&out, all.keys[i]);
    }
    sv_sort(&out);
    return out;
}

/* ------------------------------------------------------------ make targets */

static int mk_class(unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || strchr("_.%/$() -", c); }

static void add_words(struct smap *t, const char *s, size_t n)
{
    char *w = xstrndup(s, n), *f;
    for (char *p = strtok_r(w, " \t\n\r\f\v", &f); p; p = strtok_r(NULL, " \t\n\r\f\v", &f)) smap_add(t, p);
    free(w);
}

static int phony(const char *line, struct smap *t)
{
    if (strncmp(line, ".PHONY:", 7)) return 0;
    const char *p = line + 7;
    while (S((unsigned char)*p) && *p != '\n') ++p;
    const char *e = strchr(p, '\n');
    add_words(t, p, e ? (size_t)(e - p) : strlen(p));
    return 1;
}

static void targets(struct smap *t)
{
    char *mf = xasprintf("%s/Makefile", NATIVE);
    char *text = read_file(mf, NULL);
    free(mf);
    for (char *l = text; l && *l;) {
        char *e = strchr(l, '\n');
        size_t n = e ? (size_t)(e - l) : strlen(l);
        /* ^([A-Za-z0-9_.%/$() -]+?)\s*:(?!=) */
        const char *colon = memchr(l, ':', n);
        if (colon && colon > l && colon[1] != '=') {
            size_t end = (size_t)(colon - l);
            while (end > 0 && S((unsigned char)l[end - 1])) --end;
            int ok = end > 0;
            for (size_t i = 0; i < end && ok; ++i) ok = mk_class((unsigned char)l[i]);
            if (ok) add_words(t, l, end);
        }
        char *tmp = xstrndup(l, n);
        phony(tmp, t);
        free(tmp);
        if (!e) break;
        l = e + 1;
    }
    free(text);
    static const char *const G[3] = {"*.mk", "cuda/*.mk", "cuda/*/*.mk"};
    for (int g = 0; g < 3; ++g) {
        char *pat = xasprintf("%s/%s", NATIVE, G[g]);
        glob_t gl;
        if (!glob(pat, 0, NULL, &gl))
            for (size_t i = 0; i < gl.gl_pathc; ++i) {
                char *mk = read_file(gl.gl_pathv[i], NULL);
                for (char *l = mk; l && *l;) {
                    char *e = strchr(l, '\n');
                    size_t n = e ? (size_t)(e - l) : strlen(l);
                    char *tmp = xstrndup(l, n);
                    if (!phony(tmp, t)) {
                        /* ^([A-Za-z][\w-]*(?:\s+[A-Za-z][\w-]*)*)\s*:(?!=) */
                        size_t p = 0, last = 0;
                        if (A((unsigned char)tmp[0]) && tmp[0] < 0x7f) {
                            for (;;) {
                                ++p;
                                while (p < n && (W((unsigned char)tmp[p]) || tmp[p] == '-')) ++p;
                                last = p;
                                size_t q = p;
                                while (q < n && S((unsigned char)tmp[q])) ++q;
                                if (q > p && q < n && ((tmp[q] >= 'A' && tmp[q] <= 'Z') || (tmp[q] >= 'a' && tmp[q] <= 'z'))) {
                                    p = q;
                                    continue;
                                }
                                p = q;
                                break;
                            }
                            if (p < n && tmp[p] == ':' && tmp[p + 1] != '=') add_words(t, tmp, last);
                        }
                    }
                    free(tmp);
                    if (!e) break;
                    l = e + 1;
                }
                free(mk);
            }
        globfree(&gl);
        free(pat);
    }
}

/* make\s+(?:-\S+\s+)*-C\s+native\s+(?:-\S+\s+)*([A-Za-z][\w-]*), the target or NULL */
static char *gate_target(const char *cmd)
{
    size_t n = strlen(cmd);
    for (size_t s = 0; s + 4 <= n; ++s) {
        if (memcmp(cmd + s, "make", 4)) continue;
        size_t p = s + 4;
        if (p >= n || !S((unsigned char)cmd[p])) continue;
        /* the tokens after make */
        size_t tb[256], te[256];
        int nt = 0;
        size_t q = p;
        while (q < n && nt < 256) {
            while (q < n && S((unsigned char)cmd[q])) ++q;
            if (q >= n) break;
            tb[nt] = q;
            while (q < n && !S((unsigned char)cmd[q])) ++q;
            te[nt++] = q;
        }
        /* dash tokens (each followed by whitespace), the last "-C", then "csrc" and whitespace */
        int k = 0;
        while (k < nt && cmd[tb[k]] == '-' && te[k] - tb[k] >= 2 && te[k] < n) ++k;
        if (k == 0 || k >= nt) continue;
        if (te[k - 1] - tb[k - 1] != 2 || memcmp(cmd + tb[k - 1], "-C", 2)) continue;
        if (te[k] - tb[k] != 4 || memcmp(cmd + tb[k], "csrc", 4) || te[k] >= n) continue;
        int j = k + 1;
        while (j < nt && cmd[tb[j]] == '-' && te[j] - tb[j] >= 2 && te[j] < n) ++j;
        if (j >= nt) continue;
        unsigned char c0 = (unsigned char)cmd[tb[j]];
        if (!((c0 >= 'A' && c0 <= 'Z') || (c0 >= 'a' && c0 <= 'z'))) continue;
        size_t e = tb[j] + 1;
        while (e < te[j] && (W((unsigned char)cmd[e]) || cmd[e] == '-')) ++e;
        return xstrndup(cmd + tb[j], e - tb[j]);
    }
    return NULL;
}

/* ------------------------------------------------------------ main */

static int find_root(const char *argv0)
{
    char cwd[4096];
    if (getcwd(cwd, sizeof cwd))
        for (;;) {
            char *a = xasprintf("%s/csrc/cuda/mirror.tsv", cwd);
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
        char *a = xasprintf("%s/csrc/cuda/mirror.tsv", rp);
        int ok = is_file(a);
        free(a);
        if (ok) { snprintf(ROOT, sizeof ROOT, "%s", rp); return 1; }
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (!find_root(argv[0])) die("mirror: no repository (csrc/cuda/mirror.tsv) above the current directory");
    snprintf(NATIVE, sizeof NATIVE, "%s/csrc", ROOT);
    snprintf(MANIFEST, sizeof MANIFEST, "%s/cuda/mirror.tsv", NATIVE);
    const char *base = "master", *headrev = "HEAD", *closure_k = NULL, *verify_ci = NULL, *manifest = MANIFEST;
    int worktree = 0, run = 0, full = 0, keep_going = 0, check = 0;
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
#define VAL() (i + 1 < argc ? argv[++i] : (die("mirror: %s needs a value", a), ""))
        if (!strcmp(a, "--base")) base = VAL();
        else if (!strcmp(a, "--head")) headrev = VAL();
        else if (!strcmp(a, "--worktree")) worktree = 1;
        else if (!strcmp(a, "--run")) run = 1;
        else if (!strcmp(a, "--full")) full = 1;
        else if (!strcmp(a, "--keep-going")) keep_going = 1;
        else if (!strcmp(a, "--check")) check = 1;
        else if (!strcmp(a, "--closure")) closure_k = VAL();
        else if (!strcmp(a, "--verify-ci")) verify_ci = VAL();
        else if (!strcmp(a, "--manifest")) manifest = VAL();
        else die("mirror: unknown argument %s", a);
#undef VAL
    }
    const char *head = worktree ? NULL : headrev;
    load_manifest(manifest);

    if (check || closure_k || verify_ci) {
        struct tree *T = tree_new(head);
        int bad = 0;
        if (verify_ci) {
            int n = 0;
            DIR *d = opendir(verify_ci);
            struct dirent *e;
            while (d && (e = readdir(d))) {
                if (e->d_name[0] == '.' || !ends_with(e->d_name, ".ci")) continue;
                char *p = path_join(verify_ci, e->d_name);
                FILE *f = fopen(p, "r");
                free(p);
                if (!f) continue;
                char *line = NULL;
                size_t cap = 0;
                while (getline(&line, &cap, f) >= 0) {
                    /* node: \{ title: "([^"]+)" label: "([^\\"]+)\\n([^\\"]+)\\n */
                    if (strncmp(line, "node: { title: \"", 16)) continue;
                    char *q = strchr(line + 16, '"');
                    if (!q || q == line + 16 || strncmp(q, "\" label: \"", 10)) continue;
                    char *n0 = q + 10, *n1 = n0;
                    while (*n1 && *n1 != '\\' && *n1 != '"') ++n1;
                    if (n1 == n0 || strncmp(n1, "\\n", 2)) continue;
                    char *l0 = n1 + 2, *l1 = l0;
                    while (*l1 && *l1 != '\\' && *l1 != '"') ++l1;
                    if (l1 == l0 || strncmp(l1, "\\n", 2)) continue;
                    char *name = xstrndup(n0, (size_t)(n1 - n0)), *loc = xstrndup(l0, (size_t)(l1 - l0));
                    char *c1 = strchr(loc, ':');
                    char *path = c1 ? xstrndup(loc, (size_t)(c1 - loc)) : xstrdup(loc);
                    char *lstr = c1 ? xstrdup(c1 + 1) : xstrdup("");
                    char *c2 = strchr(lstr, ':');
                    if (c2) *c2 = 0;
                    if (starts_with(path, "engine/")) {
                        char *rel = path[0] == '/' ? xstrdup(path) : norm_path(path);
                        char *key = xasprintf("%s:%s", rel, name);
                        struct item *it = smap_get(&T->items, key);
                        ++n;
                        long lv = atol(lstr);
                        if (!it || strcmp(it->kind, "fn") || !(it->start <= lv && lv <= it->end)) {
                            ++bad;
                            if (bad <= 20) {
                                printf("verify-ci: %s:%s at %s: parsed ", rel, name, lstr);
                                if (!it) printf("None\n");
                                else printf("%s %d-%d\n", it->kind, it->start, it->end);
                            }
                        }
                        free(key), free(rel);
                    }
                    free(name), free(loc), free(path), free(lstr);
                }
                free(line);
                fclose(f);
            }
            if (d) closedir(d);
            printf("verify-ci: %d functions, %d outside their parsed range\n", n, bad);
        }
        if (closure_k) {
            struct kern *d = smap_get(&kernels, closure_k);
            if (!d) die("mirror: no kernel %s in the manifest", closure_k);
            char *how;
            struct sv got = mirrored(T, d, &how);
            for (int i = 0; i < got.n; ++i) printf("%s %s\n", got.v[i], ((struct item *)smap_get(&T->items, got.v[i]))->kind);
            fflush(stdout);
            fprintf(stderr, "%s: %d items (%s)\n", closure_k, got.n, how);
        }
        if (check) {
            struct smap tg = {0};
            targets(&tg);
            for (int ki = 0; ki < kernels.n; ++ki) {
                const char *k = kernels.keys[ki];
                struct kern *d = kernels.vals[ki];
                for (int w = 0; w < 2; ++w) {
                    struct sv *l = &d->v[w ? K_CUT : K_ROOT];
                    for (int i = 0; i < l->n; ++i)
                        if (!smap_has(&T->items, l->v[i])) {
                            ++bad;
                            printf("mirror check: %s names %s, which csrc/engine does not define\n", k, l->v[i]);
                        }
                }
                for (int i = 0; i < d->v[K_FILE].n; ++i) {
                    char *pat = xasprintf("%s/%s", NATIVE, d->v[K_FILE].v[i]);
                    glob_t gl;
                    int none = glob(pat, 0, NULL, &gl) != 0 || !gl.gl_pathc;
                    if (!none) globfree(&gl);
                    free(pat);
                    if (none) {
                        ++bad;
                        printf("mirror check: %s file %s matches nothing\n", k, d->v[K_FILE].v[i]);
                    }
                }
                static const int GK[4] = {K_BUILD, K_EDIT, K_QUICK, K_FULL};
                for (int g = 0; g < 4; ++g)
                    for (int i = 0; i < d->v[GK[g]].n; ++i) {
                        const char *c = d->v[GK[g]].v[i];
                        char *t = gate_target(c);
                        if (!t || !smap_has(&tg, t)) {
                            ++bad;
                            printf("mirror check: %s gate \"%s\": no csrc/Makefile target %s\n", k, c, t ? t : "None");
                        }
                        free(t);
                    }
                char *how;
                struct sv got = mirrored(T, d, &how);
                printf("mirror check: %s: %d mirrored items (%s), %d files, %d quick and %d full gates\n", k, got.n, how,
                       d->v[K_FILE].n, d->v[K_QUICK].n, d->v[K_FULL].n);
            }
            printf("mirror check: %s\n", bad ? "FAIL" : "ok");
        }
        return bad ? 1 : 0;
    }

    /* the gates to run: (kernel, command) */
    struct sv ck = {0}, cc = {0};
    if (full) {
        for (int ki = 0; ki < kernels.n; ++ki) {
            struct kern *d = kernels.vals[ki];
            for (int i = 0; i < d->v[K_BUILD].n; ++i) sv_push(&ck, kernels.keys[ki]), sv_push(&cc, d->v[K_BUILD].v[i]);
            for (int i = 0; i < d->v[K_FULL].n; ++i) sv_push(&ck, kernels.keys[ki]), sv_push(&cc, d->v[K_FULL].v[i]);
        }
    } else {
        struct sv files = changed_files(base, head);
        struct sv items = changed_items(base, head, &files);
        struct tree *T = tree_new(head);
        for (int ki = 0; ki < kernels.n; ++ki) {
            const char *k = kernels.keys[ki];
            struct kern *d = kernels.vals[ki];
            struct sv why = {0};
            for (int i = 0; i < files.n; ++i)
                for (int p = 0; p < d->v[K_FILE].n; ++p)
                    if (glob_match(d->v[K_FILE].v[p], files.v[i])) {
                        sv_push(&why, xasprintf("file %s", files.v[i]));
                        break;
                    }
            char *how;
            struct sv got = mirrored(T, d, &how);
            for (int i = 0; i < items.n; ++i)
                if (in_sv(&got, items.v[i])) sv_push(&why, items.v[i]);
            /* an item gone from the head tree that a root named */
            for (int i = 0; i < items.n; ++i)
                if (in_sv(&d->v[K_ROOT], items.v[i]) && !smap_has(&T->items, items.v[i])) sv_push(&why, items.v[i]);
            if (why.n) {
                printf("%-4s GATES  ", k);
                for (int i = 0; i < why.n && i < 12; ++i) printf("%s%s", i ? ", " : "", why.v[i]);
                if (why.n > 12) printf(" and %d more", why.n - 12);
                printf("\n");
                for (int i = 0; i < d->v[K_BUILD].n; ++i) sv_push(&ck, (char *)k), sv_push(&cc, d->v[K_BUILD].v[i]);
                for (int i = 0; i < d->v[K_QUICK].n; ++i) sv_push(&ck, (char *)k), sv_push(&cc, d->v[K_QUICK].v[i]);
            } else
                printf("%-4s clear  (%d mirrored items; %d changed C items in %d files reach none)\n", k, got.n, items.n, files.n);
        }
    }
    for (int i = 0; i < ck.n; ++i) printf("%s: %s\n", ck.v[i], cc.v[i]);
    if (!run) return 0;
    struct sv fk = {0}, fc = {0};
    char *fbuild = xcalloc((size_t)ck.n + 1, 1);
    int nf = 0;
    for (int i = 0; i < ck.n; ++i) {
        const char *k = ck.v[i], *c = cc.v[i];
        if (nf && !keep_going) break;
        if (keep_going) {
            int skip = 0;
            for (int j = 0; j < nf; ++j) skip |= !strcmp(fk.v[j], k) && fbuild[j];
            if (skip) {
                printf("mirror run: SKIP %s (its build failed): %s\n", k, c);
                fflush(stdout);
                continue;
            }
        }
        printf("mirror run: %s: %s\n", k, c);
        fflush(stdout);
        double t0 = now_secs();
        char *av[] = {"bash", "-c", (char *)c, NULL};
        int rc = run_cmd(av, ROOT, NULL, NULL, 0, NULL);
        printf("mirror run: %s %s rc %d %.0f s: %s\n", rc == 0 ? "ok" : "FAIL", k, rc, now_secs() - t0, c);
        fflush(stdout);
        if (rc) {
            struct kern *d = smap_get(&kernels, k);
            fbuild[nf] = (char)in_sv(&d->v[K_BUILD], c);
            sv_push(&fk, (char *)k), sv_push(&fc, (char *)c);
            ++nf;
        }
    }
    if (nf) {
        printf("mirror run: FAIL %d of %d gates: ", nf, ck.n);
        for (int j = 0; j < nf; ++j) printf("%s%s %s", j ? "; " : "", fk.v[j], fc.v[j]);
        printf("\n");
        return 1;
    }
    printf("mirror run: %d gates passed\n", ck.n);
    return 0;
}
