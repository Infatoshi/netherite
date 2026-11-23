/* The tick's call graph and the cycles in it (make -C csrc callgraph).
 *
 * Reads the .ci files gcc -fcallgraph-info=su writes beside each object
 * (built without inlining, so each edge is a call in the source), keeps the
 * functions reachable from the tick's root (session_tick), and prints every
 * strongly connected component: a recursion a GPU thread would need a stack
 * for. Chunk generation, population and loading are cut at their entry
 * points (worldgen: the generation queue and kernel's side, SPEC's L4/L10),
 * and the cycles wholly inside the GenLayer stack and the chunk loader are
 * listed as worldgen; any other cycle fails the check.
 *
 * --pointers also resolves the indirect calls gcc leaves as a placeholder:
 * a call through x.f or x->f reaches every function whose address is stored
 * into a field named f, a call through a parameter every function passed in
 * that position. It over-approximates (two structs with a same-named field
 * share targets) and reports; only the direct graph gates.
 *
 *   out/native/callgraph OBJ_DIR SRC_DIR [--pointers] [--root F] [--sites F] [--path F,G]
 *       [--stack [--roots F,G] [--frame N] [--chain N] [--cut-worldgen]]
 *
 * The C port of tests/callgraph.py (lane/cport): the same output. Its
 * regular expressions over the source are the matchers below, each the
 * leftmost match Python's re finds (the source is ASCII once comments are
 * blanked; a byte over 0x7f counts as a word character). Where Python's
 * order was a set's (--sites among same-named callees, --path among equal
 * paths, the --stack frames list among same-named equal frames), titles
 * break the tie. */
#define _GNU_SOURCE
#include "tool.h"

/* ------------------------------------------------------------ options */

static const char *opt_obj, *opt_src, *opt_root = "session_tick", *opt_sites, *opt_path,
                  *opt_roots = "session_tick,serverreplay_tick,server_player_tick,client_player_tick";
static int opt_pointers, opt_stack, opt_cut_worldgen;
static long long opt_frame = 4096, opt_chain = 32768;

/* the worldgen entry points the tick reaches (world.c's chunk request, the provide and on_chunk
 * hooks, the structure and population item sinks), and the phase tracer's entry points
 * (engine/phase.h): the harness's, reached only when a test turns it on */
static const char *const CUT[] = {"world_chunk_request", "sr_hell_provide", "sr_provide", "sr_sky_provide",
                                  "sr_hell_on_chunk", "sr_sky_on_chunk", "sw_on_chunk", "sr_on_chunk",
                                  "offer_trampoline", "env_drop_item", "pop_item_drop", "sw_item_sink",
                                  "phase_begin_", "phase_end_", "phase_row_begin_", "phase_row_end_", NULL};
static const char *const WORLDGEN_FILES[] = {"layers.c", "chunkload.c", NULL};

/* The recursions left in worldgen whose depth is fixed by the code:
 * GenLayer's layer_ints and seed_world walk the layer graph layers_init
 * builds (44 layers, the deepest parent chain 33, the same for every seed);
 * a cave tunnel's branches are narrower than 1.0 and never branch again
 * (MapGenCaves' width > 1.0F test), so tunnel and hell_tunnel go 2 deep. */
static const struct {
    const char *name;
    int depth;
} RECURSION[] = {{"layer_ints", 33}, {"seed_world", 33}, {"tunnel", 2}, {"hell_tunnel", 2}};

static int in_list(const char *const *l, const char *s)
{
    for (; *l; ++l)
        if (!strcmp(*l, s)) return 1;
    return 0;
}

static int recursion_depth(const char *name)
{
    for (int i = 0; i < 4; ++i)
        if (!strcmp(RECURSION[i].name, name)) return RECURSION[i].depth;
    return 0;
}

/* ------------------------------------------------------------ int sets */

struct iset {
    int *v, n, cap;
};

static int iset_has(const struct iset *s, int x)
{
    for (int i = 0; i < s->n; ++i)
        if (s->v[i] == x) return 1;
    return 0;
}

static int iset_add(struct iset *s, int x)
{
    if (iset_has(s, x)) return 0;
    if (s->n == s->cap) s->cap = s->cap ? s->cap * 2 : 4, s->v = xrealloc(s->v, (size_t)s->cap * sizeof *s->v);
    s->v[s->n++] = x;
    return 1;
}

/* ------------------------------------------------------------ the graph */

static struct smap titles; /* title -> id; vals unused */
static char **node_name;   /* the node's name (NULL: never named) */
static char **defined;     /* its definition's location, or NULL */
static long long *frame_bytes;
static char **frame_kind;
static struct iset *edges; /* the callees of each title */
static struct sv *indirect_labs;
static struct smap labels; /* "S T" -> struct sv * */
static int *def_order, ndef;
static int cap_ids;

static int title_id(const char *t)
{
    int x = smap_find(&titles, t);
    if (x >= 0) return x;
    x = smap_add(&titles, t);
    if (x >= cap_ids) {
        int c = cap_ids ? cap_ids * 2 : 4096;
        node_name = xrealloc(node_name, (size_t)c * sizeof *node_name);
        defined = xrealloc(defined, (size_t)c * sizeof *defined);
        frame_bytes = xrealloc(frame_bytes, (size_t)c * sizeof *frame_bytes);
        frame_kind = xrealloc(frame_kind, (size_t)c * sizeof *frame_kind);
        edges = xrealloc(edges, (size_t)c * sizeof *edges);
        indirect_labs = xrealloc(indirect_labs, (size_t)c * sizeof *indirect_labs);
        for (int i = cap_ids; i < c; ++i) {
            node_name[i] = defined[i] = frame_kind[i] = NULL;
            frame_bytes[i] = 0;
            edges[i] = (struct iset){0};
            indirect_labs[i] = (struct sv){0};
        }
        cap_ids = c;
    }
    return x;
}

static const char *nm(int t) { return node_name[t] ? node_name[t] : titles.keys[t]; } /* nodes.get(v, v) */

static void add_label(int s, int t, const char *lab)
{
    char *k = xasprintf("%d %d", s, t);
    struct sv *l = smap_get(&labels, k);
    if (!l) l = xcalloc(1, sizeof *l), smap_put(&labels, k, l);
    sv_push(l, xstrdup(lab));
    free(k);
}

/* os.path.basename(defined[t].split(':')[0]) */
static char *where(int t)
{
    static char b[8][256];
    static int i;
    char *o = b[i++ & 7];
    const char *loc = defined[t] ? defined[t] : "?";
    const char *c = strchr(loc, ':');
    size_t n = c ? (size_t)(c - loc) : strlen(loc);
    char tmp[256];
    if (n >= sizeof tmp) n = sizeof tmp - 1;
    memcpy(tmp, loc, n);
    tmp[n] = 0;
    snprintf(o, 256, "%s", base_name(tmp));
    return o;
}

/* a "..." field after key in a .ci line, into out; the position after it */
static const char *quoted(const char *p, const char *key, char *out, size_t n)
{
    size_t k = strlen(key);
    if (strncmp(p, key, k)) return NULL;
    p += k;
    const char *e = strchr(p, '"');
    if (!e || e == p) return NULL;
    size_t m = (size_t)(e - p);
    if (m >= n) return NULL;
    memcpy(out, p, m);
    out[m] = 0;
    return e + 1;
}

static void parse_ci(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return;
    char *line = NULL;
    size_t cap = 0;
    while (getline(&line, &cap, f) >= 0) {
        if (starts_with(line, "node: { title: \"")) {
            /* node: { title: "T" label: "NAME\nLOC(\nSTACK)?" */
            char t[4096];
            const char *p = quoted(line, "node: { title: \"", t, sizeof t);
            if (!p || strncmp(p, " label: \"", 9)) continue;
            p += 9;
            const char *q = p;
            while (*q && *q != '\\' && *q != '"') ++q;
            if (q == p || strncmp(q, "\\n", 2)) continue;
            char *name = xstrndup(p, (size_t)(q - p));
            const char *l0 = q + 2, *l1 = l0;
            while (*l1 && *l1 != '\\' && *l1 != '"') ++l1;
            if (l1 == l0) { free(name); continue; }
            char *loc = xstrndup(l0, (size_t)(l1 - l0));
            char *stack = NULL;
            if (!strncmp(l1, "\\n", 2)) {
                const char *s0 = l1 + 2, *s1 = strchr(s0, '"');
                if (!s1) { free(name), free(loc); continue; }
                stack = xstrndup(s0, (size_t)(s1 - s0));
            } else if (*l1 != '"') {
                free(name), free(loc);
                continue;
            }
            int id = title_id(t);
            free(node_name[id]);
            node_name[id] = name;
            if (stack) {
                if (!defined[id]) {
                    if (ndef % 4096 == 0) def_order = xrealloc(def_order, (size_t)(ndef + 4096) * sizeof *def_order);
                    def_order[ndef++] = id;
                }
                free(defined[id]);
                defined[id] = loc;
                /* (\d+) bytes \(([^)]*)\) */
                char *e;
                long long by = strtoll(stack, &e, 10);
                char *close;
                if (e != stack && stack[0] >= '0' && stack[0] <= '9' && !strncmp(e, " bytes (", 8) && (close = strchr(e + 8, ')'))) {
                    frame_bytes[id] = by;
                    free(frame_kind[id]);
                    frame_kind[id] = xstrndup(e + 8, (size_t)(close - e - 8));
                } else {
                    frame_bytes[id] = 0;
                    free(frame_kind[id]);
                    frame_kind[id] = xstrdup("?");
                }
                free(stack);
            } else free(loc);
            continue;
        }
        if (starts_with(line, "edge: { sourcename: \"")) {
            char s[4096], t[4096], lab[4096];
            const char *p = quoted(line, "edge: { sourcename: \"", s, sizeof s);
            if (!p) continue;
            p = quoted(p, " targetname: \"", t, sizeof t);
            if (!p) continue;
            p = quoted(p, " label: \"", lab, sizeof lab);
            if (!p) continue;
            int si = title_id(s);
            if (!strcmp(t, "__indirect_call")) sv_push(&indirect_labs[si], xstrdup(lab));
            else {
                int ti = title_id(t);
                iset_add(&edges[si], ti);
                add_label(si, ti, lab);
            }
        }
    }
    free(line);
    fclose(f);
}

/* ------------------------------------------------------------ the regular expressions */

static int W(unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c >= 0x80; }
static int A(unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c >= 0x80; }
static int S(unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v' || (c >= 0x1c && c <= 0x1f); }

/* the end of s[a, n) once trailing whitespace is gone */
static size_t rstrip_end(const char *s, size_t a, size_t n)
{
    while (n > a && S((unsigned char)s[n - 1])) --n;
    return n;
}

/* (\w+)\s*$ or ([A-Za-z_]\w*)\s*$ (alpha): the group's start and end in s[0, n), 0 if none */
static int word_at_end(const char *s, size_t n, int alpha, size_t *g0, size_t *g1)
{
    size_t e = rstrip_end(s, 0, n), b = e;
    while (b > 0 && W((unsigned char)s[b - 1])) --b;
    if (b == e) return 0;
    if (alpha) {
        while (b < e && !A((unsigned char)s[b])) ++b;
        if (b == e) return 0;
    }
    *g0 = b, *g1 = e;
    return 1;
}

/* (?:\.|->)\s*(\w+)\s*$ */
static int member_at_end(const char *s, size_t n, size_t *g0, size_t *g1)
{
    if (!word_at_end(s, n, 0, g0, g1)) return 0;
    size_t p = *g0;
    while (p > 0 && S((unsigned char)s[p - 1])) --p;
    if (p >= 1 && s[p - 1] == '.') return 1;
    if (p >= 2 && s[p - 1] == '>' && s[p - 2] == '-') return 1;
    return 0;
}

/* the part before an assignment's value: ...\s*=\s*&?\s*$, the length before the '=' or -1 */
static long assign_end(const char *s, size_t n)
{
    size_t e = rstrip_end(s, 0, n);
    if (e && s[e - 1] == '&') e = rstrip_end(s, 0, e - 1);
    if (!e || s[e - 1] != '=') return -1;
    return (long)rstrip_end(s, 0, e - 1);
}

/* (?:\.|->)\s*(\w+)\s*=\s*&?\s*$ (member) or \b(\w+)\s*=\s*&?\s*$ */
static int assigned(const char *s, size_t n, int member, char *out, size_t on)
{
    long e = assign_end(s, n);
    if (e <= 0) return 0;
    size_t g0, g1;
    if (member) {
        if (!member_at_end(s, (size_t)e, &g0, &g1) || g1 != (size_t)e) return 0;
    } else {
        size_t b = (size_t)e;
        while (b > 0 && W((unsigned char)s[b - 1])) --b;
        if (b == (size_t)e) return 0;
        g0 = b, g1 = (size_t)e;
    }
    size_t m = g1 - g0 < on - 1 ? g1 - g0 : on - 1;
    memcpy(out, s + g0, m);
    out[m] = 0;
    return 1;
}

/* ------------------------------------------------------------ the source */

/* comments blanked, one space per character, newlines kept */
static char *strip_comments(const char *s)
{
    size_t n = strlen(s);
    char *o = xmalloc(n + 1), *w = o;
    char *mark = xcalloc(n + 1, 1);
    /* /\*.*?\*\/ first, then //[^\n]* over what is left */
    for (size_t i = 0; i + 1 < n; ++i) {
        if (s[i] == '/' && s[i + 1] == '*') {
            const char *e = strstr(s + i + 2, "*/");
            if (!e) break;
            size_t j = (size_t)(e - s) + 2;
            for (size_t k = i; k < j; ++k) mark[k] = 1;
            i = j - 1;
        }
    }
    for (size_t i = 0; i + 1 < n; ++i) {
        if (!mark[i] && s[i] == '/' && s[i + 1] == '/') {
            /* the comment's text as the first pass left it (blanks count as text) */
            size_t j = i;
            while (j < n && s[j] != '\n') mark[j++] = 2;
            i = j;
        }
    }
    for (size_t i = 0; i < n; ++i) {
        if (!mark[i] || s[i] == '\n') *w++ = s[i];
        else if (((unsigned char)s[i] & 0xc0) != 0x80) *w++ = ' ';
    }
    *w = 0;
    free(mark);
    return o;
}

struct srcfile {
    char *name, *code;
    size_t len;
    char **lines;
    int nlines;
    size_t *nl;
    int nnl;
    int *start_line, *start_t, nstart;
};

static struct srcfile *srcs;
static int nsrcs;
static struct smap srcidx;

static struct srcfile *src_of(const char *base)
{
    int x = smap_find(&srcidx, base);
    return x < 0 ? NULL : &srcs[(intptr_t)srcidx.vals[x]];
}

static int title_for(const char *name, const char *path)
{
    char *st = xasprintf("engine/%s:%s", base_name(path), name);
    int x = smap_find(&titles, st);
    free(st);
    if (x >= 0 && defined[x]) return x;
    x = smap_find(&titles, name);
    return x >= 0 && defined[x] ? x : -1;
}

static int line_of(const struct srcfile *f, size_t pos)
{
    int lo = 0, hi = f->nnl;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (f->nl[mid] < pos) lo = mid + 1;
        else hi = mid;
    }
    return lo + 1;
}

static int enclosing(const struct srcfile *f, int line)
{
    if (!f) return -1;
    int lo = 0, hi = f->nstart;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (f->start_line[mid] <= line) lo = mid + 1;
        else hi = mid;
    }
    return lo ? f->start_t[lo - 1] : -1;
}

/* Python's str.find with its start rules */
static long py_find(const char *t, size_t n, const char *sub, long start)
{
    if (start < 0) start += (long)n;
    if (start < 0) start = 0;
    if ((size_t)start > n) return -1;
    const char *p = memmem(t + start, n - (size_t)start, sub, strlen(sub));
    return p ? (long)(p - t) : -1;
}

/* ---- a function's parameter names (a function pointer's by (*name)) */
static struct sv *params_cache;
static char *params_done;

static void param_name(const char *q, size_t n, struct sv *out)
{
    /* \(\s*\*\s*(\w+)\s*\) */
    for (size_t s = 0; s < n; ++s) {
        if (q[s] != '(') continue;
        size_t p = s + 1;
        while (p < n && S((unsigned char)q[p])) ++p;
        if (p >= n || q[p] != '*') continue;
        ++p;
        while (p < n && S((unsigned char)q[p])) ++p;
        size_t w0 = p;
        while (p < n && W((unsigned char)q[p])) ++p;
        if (p == w0) continue;
        size_t w1 = p;
        while (p < n && S((unsigned char)q[p])) ++p;
        if (p < n && q[p] == ')') {
            sv_push(out, xstrndup(q + w0, w1 - w0));
            return;
        }
    }
    /* (\w+)\s*(\[[^\]]*\])?\s*$ */
    size_t e = rstrip_end(q, 0, n);
    for (size_t s = 0; s < e; ++s) {
        if (!W((unsigned char)q[s])) continue;
        size_t p = s;
        while (p < e && W((unsigned char)q[p])) ++p;
        size_t w1 = p;
        while (p < e && S((unsigned char)q[p])) ++p;
        int ok = p == e;
        if (!ok && q[p] == '[') {
            const char *c = memchr(q + p + 1, ']', e - p - 1);
            ok = c && (size_t)(c - q) + 1 == e;
        }
        if (ok) {
            sv_push(out, xstrndup(q + s, w1 - s));
            return;
        }
        /* a later start inside this word fails the same way (the next
         * character after it is a word character) */
        s = w1 - 1;
    }
    sv_push(out, xstrdup(""));
}

static struct sv *params_of(int t)
{
    if (params_done[t]) return &params_cache[t];
    params_done[t] = 1;
    struct sv *out = &params_cache[t];
    const char *loc = defined[t];
    char file[1024];
    const char *c1 = strchr(loc, ':');
    if (!c1) return out;
    size_t fn = (size_t)(c1 - loc) < sizeof file - 1 ? (size_t)(c1 - loc) : sizeof file - 1;
    memcpy(file, loc, fn);
    file[fn] = 0;
    char *e;
    long l = strtol(c1 + 1, &e, 10);
    if (e == c1 + 1 || (*e && *e != ':')) return out; /* int(l) fails: no parameters */
    struct srcfile *f = src_of(base_name(file));
    if (!f) return out; /* KeyError */
    /* lines[l - 1:l + 12] */
    long a = l - 1, b = l + 12, nl = f->nlines;
    if (a < 0) a += nl;
    if (a < 0) a = 0;
    if (b < 0) b += nl;
    if (b > nl) b = nl;
    struct sb j = {0};
    for (long i = a; i < b; ++i) {
        if (i > a) sb_putc(&j, '\n');
        sb_puts(&j, f->lines[i]);
    }
    char *text = strip_comments(sb_str(&j));
    sb_free(&j);
    size_t n = strlen(text);
    long i = py_find(text, n, "(", py_find(text, n, nm(t), 0));
    long jj = i;
    int depth = 0;
    while (jj < (long)n) {
        long k = jj < 0 ? jj + (long)n : jj;
        if (k < 0) { free(text); return out; } /* IndexError */
        depth += text[k] == '(';
        depth -= text[k] == ')';
        if (depth == 0) break;
        ++jj;
    }
    /* text[i + 1:j] */
    long s0 = i + 1, s1 = jj;
    if (s0 < 0) s0 += (long)n;
    if (s1 < 0) s1 += (long)n;
    if (s0 < 0) s0 = 0;
    if (s1 < 0) s1 = 0;
    if (s0 > (long)n) s0 = (long)n;
    if (s1 > (long)n) s1 = (long)n;
    if (s1 < s0) s1 = s0;
    depth = 0;
    long cur = s0;
    for (long k = s0; k < s1; ++k) {
        depth += text[k] == '(';
        depth -= text[k] == ')';
        if (text[k] == ',' && depth == 0) {
            param_name(text + cur, (size_t)(k - cur), out);
            cur = k + 1;
        }
    }
    param_name(text + cur, (size_t)(s1 - cur), out);
    free(text);
    return out;
}

static int param_index(int t, const char *name)
{
    struct sv *ps = params_of(t);
    for (int i = 0; i < ps->n; ++i)
        if (!strcmp(ps->v[i], name)) return i;
    return -1;
}

/* the call a position sits in: (callee name, argument index), or 0 */
static int call_slot(const char *text, size_t pos, char *callee, size_t cn, int *idx)
{
    int depth = 0;
    *idx = 0;
    for (long i = (long)pos - 1; i >= 0; --i) {
        char ch = text[i];
        if (ch == ')' || ch == ']' || ch == '}') ++depth;
        else if (ch == '(' || ch == '[' || ch == '{') {
            if (depth == 0) {
                if (ch != '(') return 0;
                size_t w0 = (size_t)(i >= 200 ? i - 200 : 0), g0, g1;
                if (!word_at_end(text + w0, (size_t)i - w0, 1, &g0, &g1)) return 0;
                size_t m = g1 - g0 < cn - 1 ? g1 - g0 : cn - 1;
                memcpy(callee, text + w0 + g0, m);
                callee[m] = 0;
                return 1;
            }
            --depth;
        } else if (ch == ',' && depth == 0) ++*idx;
        else if (ch == ';' && depth == 0) return 0;
    }
    return 0;
}

/* ---- the slots functions land in: ("f", field) or ("p", callee title or name, index) */
static struct smap slots; /* key -> struct iset * */

static char *slot_f(const char *field) { return xasprintf("f %s", field); }

/* ("p", title_for(name) or name, index): a title, else the name as written */
static char *slot_p(int callee_title, const char *callee_name, int idx)
{
    return xasprintf("p %s %d", callee_title >= 0 ? titles.keys[callee_title] : callee_name, idx);
}

static struct iset *slot(const char *key, int create)
{
    struct iset *s = smap_get(&slots, key);
    if (!s && create) s = xcalloc(1, sizeof *s), smap_put(&slots, key, s);
    return s;
}

static int is_fname(const char *s);
static struct smap fnames;

static int is_fname(const char *s) { return smap_has(&fnames, s); }

static int next_starts(const char *text, size_t len, size_t at, size_t span, const char *chars)
{
    size_t e = at + span < len ? at + span : len;
    while (at < e && S((unsigned char)text[at])) ++at;
    return at < e && strchr(chars, text[at]) != NULL;
}

static void resolve_pointers(void)
{
    for (int i = 0; i < ndef; ++i) smap_add(&fnames, nm(def_order[i]));
    /* each file's function starts, by (line, title) */
    for (int i = 0; i < nsrcs; ++i) srcs[i].nstart = 0;
    for (int k = 0; k < ndef; ++k) {
        int t = def_order[k];
        char file[1024];
        const char *c1 = strchr(defined[t], ':');
        if (!c1) continue;
        size_t fn = (size_t)(c1 - defined[t]) < sizeof file - 1 ? (size_t)(c1 - defined[t]) : sizeof file - 1;
        memcpy(file, defined[t], fn);
        file[fn] = 0;
        struct srcfile *f = src_of(base_name(file));
        if (!f) continue;
        f->start_line = xrealloc(f->start_line, (size_t)(f->nstart + 1) * sizeof *f->start_line);
        f->start_t = xrealloc(f->start_t, (size_t)(f->nstart + 1) * sizeof *f->start_t);
        f->start_line[f->nstart] = atoi(c1 + 1);
        f->start_t[f->nstart] = t;
        ++f->nstart;
    }
    for (int i = 0; i < nsrcs; ++i) {
        struct srcfile *f = &srcs[i];
        for (int a = 1; a < f->nstart; ++a) {
            int l = f->start_line[a], t = f->start_t[a], b = a - 1;
            while (b >= 0 && (f->start_line[b] > l || (f->start_line[b] == l && strcmp(titles.keys[f->start_t[b]], titles.keys[t]) > 0))) {
                f->start_line[b + 1] = f->start_line[b];
                f->start_t[b + 1] = f->start_t[b];
                --b;
            }
            f->start_line[b + 1] = l;
            f->start_t[b + 1] = t;
        }
    }
    struct alias {
        char *dst, *src;
    } *alias = NULL;
    int nalias = 0, calias = 0;
    char word[512], callee[512], fld[512];
    for (int fi = 0; fi < nsrcs; ++fi) {
        struct srcfile *f = &srcs[fi];
        const char *text = f->code;
        size_t len = f->len;
        for (size_t p = 0; p < len;) {
            if (!W((unsigned char)text[p])) { ++p; continue; }
            size_t q = p;
            while (q < len && W((unsigned char)text[q])) ++q;
            if (!A((unsigned char)text[p])) { p = q; continue; }
            size_t wl = q - p < sizeof word - 1 ? q - p : sizeof word - 1;
            memcpy(word, text + p, wl);
            word[wl] = 0;
            size_t m0 = p, m1 = q;
            p = q;
            int line = -1, encl = -2;
            /* every function whose address is taken, by the slot it lands in */
            if (is_fname(word) && !next_starts(text, len, m1, 40, "(")) {
                int t = title_for(word, f->name);
                if (t >= 0) {
                    line = line_of(f, m0);
                    encl = enclosing(f, line);
                    if (!(encl >= 0 && param_index(encl, word) >= 0)) {
                        size_t b0 = m0 >= 200 ? m0 - 200 : 0;
                        char *key = NULL;
                        if (assigned(text + b0, m0 - b0, 1, fld, sizeof fld) || assigned(text + b0, m0 - b0, 0, fld, sizeof fld))
                            key = slot_f(fld);
                        else {
                            int idx;
                            if (call_slot(text, m0, callee, sizeof callee, &idx)) key = slot_p(title_for(callee, f->name), callee, idx);
                        }
                        if (key) {
                            iset_add(slot(key, 1), t);
                            free(key);
                        }
                    }
                }
            }
            /* a parameter that carries a function on: stored into a field or passed to another call */
            if (next_starts(text, len, m1, 8, ";,)")) {
                if (line < 0) line = line_of(f, m0);
                if (encl == -2) encl = enclosing(f, line);
                if (encl < 0) continue;
                int pi = param_index(encl, word);
                if (pi < 0) continue;
                char *src = slot_p(encl, NULL, pi);
                size_t b0 = m0 >= 200 ? m0 - 200 : 0;
                char *dst = NULL;
                int idx;
                if (assigned(text + b0, m0 - b0, 1, fld, sizeof fld)) dst = slot_f(fld);
                else if (call_slot(text, m0, callee, sizeof callee, &idx)) dst = slot_p(title_for(callee, f->name), callee, idx);
                if (dst) {
                    if (nalias == calias) calias = calias ? calias * 2 : 256, alias = xrealloc(alias, (size_t)calias * sizeof *alias);
                    alias[nalias++] = (struct alias){dst, src};
                } else free(src);
            }
        }
    }
    for (int changed = 1; changed;) {
        changed = 0;
        for (int i = 0; i < nalias; ++i) {
            struct iset *s = slot(alias[i].src, 0), *d = slot(alias[i].dst, 1);
            if (!s) continue;
            for (int k = 0; k < s->n; ++k) changed |= iset_add(d, s->v[k]);
        }
    }
    for (int s = 0; s < titles.n; ++s) {
        for (int li = 0; li < indirect_labs[s].n; ++li) {
            const char *lab = indirect_labs[s].v[li];
            char file[1024];
            int l, c;
            const char *c1 = strchr(lab, ':'), *c2 = c1 ? strchr(c1 + 1, ':') : NULL;
            if (!c1 || !c2 || strchr(c2 + 1, ':')) die("callgraph: an indirect call label not FILE:LINE:COL: %s", lab);
            size_t fn = (size_t)(c1 - lab) < sizeof file - 1 ? (size_t)(c1 - lab) : sizeof file - 1;
            memcpy(file, lab, fn);
            file[fn] = 0;
            l = atoi(c1 + 1), c = atoi(c2 + 1);
            struct srcfile *f = src_of(base_name(file));
            if (!f || l < 1 || l > f->nlines) die("callgraph: no source line for %s", lab);
            const char *ln = f->lines[l - 1];
            size_t n = strlen(ln);
            const char *line = (size_t)(c - 1) <= n ? ln + c - 1 : ln + n;
            const char *paren = strchr(line, '(');
            size_t en = paren ? (size_t)(paren - line) : strlen(line), g0, g1;
            struct iset *got = NULL;
            if (member_at_end(line, en, &g0, &g1)) {
                char *k = xasprintf("f %.*s", (int)(g1 - g0), line + g0);
                got = slot(k, 0);
                free(k);
            } else if (word_at_end(line, en, 0, &g0, &g1)) {
                char *w = xstrndup(line + g0, g1 - g0);
                int pi = defined[s] ? param_index(s, w) : -1;
                char *k = pi >= 0 ? slot_p(s, NULL, pi) : xasprintf("f %s", w);
                got = slot(k, 0);
                free(k);
                free(w);
            }
            if (!got) continue;
            char *star = xasprintf("%s*", lab);
            for (int k = 0; k < got->n; ++k) {
                iset_add(&edges[s], got->v[k]);
                add_label(s, got->v[k], star);
            }
            free(star);
        }
    }
}

/* ------------------------------------------------------------ the stack budget */

static int cmp_title(const void *a, const void *b) { return strcmp(titles.keys[*(const int *)a], titles.keys[*(const int *)b]); }

static void sort_titles(int *v, int n)
{
    if (n > 1) qsort(v, (size_t)n, sizeof *v, cmp_title);
}

static int in_cut(int v, int cut) { return cut && in_list(CUT, nm(v)); }

static char *seen;
static int **succ, *nsucc;
static struct iset *pred;
static long long *weight;

/* the successors of u inside seen, sorted by title */
static void build_succ(void)
{
    succ = xcalloc((size_t)titles.n, sizeof *succ);
    nsucc = xcalloc((size_t)titles.n, sizeof *nsucc);
    for (int u = 0; u < titles.n; ++u) {
        if (!seen[u]) continue;
        succ[u] = xmalloc((size_t)(edges[u].n + 1) * sizeof **succ);
        for (int k = 0; k < edges[u].n; ++k)
            if (seen[edges[u].v[k]]) succ[u][nsucc[u]++] = edges[u].v[k];
        sort_titles(succ[u], nsucc[u]);
    }
}

static char *closure(int start, int forward)
{
    char *out = xcalloc((size_t)titles.n, 1);
    int *todo = xmalloc((size_t)titles.n * sizeof *todo), nt = 0;
    out[start] = 1;
    todo[nt++] = start;
    while (nt) {
        int u = todo[--nt];
        int n = forward ? nsucc[u] : pred[u].n;
        for (int k = 0; k < n; ++k) {
            int v = forward ? succ[u][k] : pred[u].v[k];
            if (!out[v]) out[v] = 1, todo[nt++] = v;
        }
    }
    free(todo);
    return out;
}

static char *comp_, *onpath_, *memo_set_;
static long long *memo_;
static int head_;

static long long level(int u)
{
    if (memo_set_[u]) return memo_[u];
    onpath_[u] = 1;
    long long b = 0;
    for (int k = 0; k < nsucc[u]; ++k) {
        int v = succ[u][k];
        if (v == head_) b = b > 0 ? b : 0;
        else if (comp_[v] && !onpath_[v]) {
            long long x = level(v);
            if (x > b) b = x;
        }
    }
    onpath_[u] = 0;
    memo_set_[u] = 1;
    memo_[u] = frame_bytes[u] + b;
    return memo_[u];
}


static int cmp_big(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    if (frame_bytes[x] != frame_bytes[y]) return frame_bytes[x] > frame_bytes[y] ? -1 : 1;
    int c = strcmp(nm(x), nm(y));
    if (c) return c;
    c = strcmp(where(x), where(y));
    return c ? c : strcmp(titles.keys[x], titles.keys[y]);
}

static int cmp_name_title(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    int c = strcmp(nm(x), nm(y));
    return c ? c : strcmp(titles.keys[x], titles.keys[y]);
}

static int stack_report(void)
{
    struct sv names = {0};
    char *rs = xstrdup(opt_roots);
    for (char *p = rs;;) {
        char *c = strchr(p, ',');
        if (c) *c = 0;
        sv_push(&names, p);
        if (!c) break;
        p = c + 1;
    }
    int cut = opt_cut_worldgen;
    int **roots = xcalloc((size_t)names.n, sizeof *roots), *nroots = xcalloc((size_t)names.n, sizeof *nroots);
    seen = xcalloc((size_t)titles.n, 1);
    int *todo = xmalloc((size_t)titles.n * sizeof *todo), nt = 0, nseen = 0;
    for (int i = 0; i < names.n; ++i) {
        roots[i] = xmalloc((size_t)(ndef + 1) * sizeof **roots);
        for (int k = 0; k < ndef; ++k)
            if (!strcmp(nm(def_order[k]), names.v[i])) roots[i][nroots[i]++] = def_order[k];
        sort_titles(roots[i], nroots[i]);
        for (int k = 0; k < nroots[i]; ++k)
            if (!seen[roots[i][k]]) seen[roots[i][k]] = 1, ++nseen, todo[nt++] = roots[i][k];
    }
    while (nt) {
        int u = todo[--nt];
        for (int k = 0; k < edges[u].n; ++k) {
            int v = edges[u].v[k];
            if (!defined[v] || in_cut(v, cut) || seen[v]) continue;
            seen[v] = 1, ++nseen;
            todo[nt++] = v;
        }
    }
    build_succ();
    pred = xcalloc((size_t)titles.n, sizeof *pred);
    for (int u = 0; u < titles.n; ++u)
        for (int k = 0; k < nsucc[u]; ++k) iset_add(&pred[succ[u][k]], u);
    weight = xcalloc((size_t)titles.n, sizeof *weight);
    for (int t = 0; t < titles.n; ++t)
        if (seen[t]) weight[t] = frame_bytes[t];
    long long *levels = xcalloc((size_t)titles.n, sizeof *levels);
    char *is_level = xcalloc((size_t)titles.n, 1);
    int *heads = xmalloc((size_t)titles.n * sizeof *heads), nh = 0;
    for (int t = 0; t < titles.n; ++t)
        if (seen[t] && recursion_depth(nm(t))) heads[nh++] = t;
    sort_titles(heads, nh);
    for (int i = 0; i < nh; ++i) {
        int h = heads[i];
        char *fw = closure(h, 1), *bw = closure(h, 0);
        comp_ = xcalloc((size_t)titles.n, 1);
        for (int t = 0; t < titles.n; ++t) comp_[t] = fw[t] && bw[t];
        onpath_ = xcalloc((size_t)titles.n, 1);
        memo_set_ = xcalloc((size_t)titles.n, 1);
        memo_ = xcalloc((size_t)titles.n, sizeof *memo_);
        head_ = h;
        long long one = level(h);
        levels[h] = one;
        is_level[h] = 1;
        weight[h] = frame_bytes[h] + (long long)(recursion_depth(nm(h)) - 1) * one;
        free(fw), free(bw), free(comp_), free(onpath_), free(memo_set_), free(memo_);
    }
    /* a depth-first walk from the roots drops each back edge, so what is left
     * is acyclic and each function's worst chain is known once its callees' are */
    long long *best = xcalloc((size_t)titles.n, sizeof *best);
    int *nxt = xmalloc((size_t)titles.n * sizeof *nxt);
    char *state = xcalloc((size_t)titles.n, 1), *back = xcalloc((size_t)titles.n, 1);
    int *wv = xmalloc((size_t)titles.n * sizeof *wv), *wi = xmalloc((size_t)titles.n * sizeof *wi);
    for (int i = 0; i < names.n; ++i)
        for (int k = 0; k < nroots[i]; ++k) {
            int r = roots[i][k];
            if (state[r]) continue;
            state[r] = 1;
            int nw = 0;
            wv[nw] = r, wi[nw++] = 0;
            while (nw) {
                int v = wv[nw - 1], pushed = 0;
                while (wi[nw - 1] < nsucc[v]) {
                    int w = succ[v][wi[nw - 1]++];
                    if (!state[w]) {
                        state[w] = 1;
                        wv[nw] = w, wi[nw++] = 0;
                        pushed = 1;
                        break;
                    }
                    if (state[w] == 1) back[w] = 1;
                }
                if (pushed) continue;
                --nw;
                long long b = 0;
                int n = -1;
                for (int j = 0; j < nsucc[v]; ++j) {
                    int w = succ[v][j];
                    if (state[w] == 2 && best[w] > b) b = best[w], n = w;
                }
                best[v] = weight[v] + b, nxt[v] = n, state[v] = 2;
            }
        }
    int bad = 0;
    printf("stack: %d functions reachable from ", nseen);
    for (int i = 0; i < names.n; ++i) printf("%s%s", i ? ", " : "", names.v[i]);
    printf("%s%s\n", opt_pointers ? " (function pointers resolved)" : "", opt_cut_worldgen ? ", worldgen cut" : "");
    int *big = xmalloc((size_t)titles.n * sizeof *big), nb = 0;
    for (int t = 0; t < titles.n; ++t)
        if (seen[t] && (frame_bytes[t] > opt_frame || (frame_kind[t] && !strcmp(frame_kind[t], "dynamic")))) big[nb++] = t;
    if (nb > 1) qsort(big, (size_t)nb, sizeof *big, cmp_big);
    printf("frames over %lld bytes or unbounded: %d\n", opt_frame, nb);
    for (int i = 0; i < nb; ++i) printf("  %7lld %-16s %s@%s\n", frame_bytes[big[i]], frame_kind[big[i]], nm(big[i]), where(big[i]));
    bad += nb;
    int *lv = xmalloc((size_t)(nh + 1) * sizeof *lv), nl = 0;
    for (int i = 0; i < nh; ++i) lv[nl++] = heads[i];
    if (nl > 1) qsort(lv, (size_t)nl, sizeof *lv, cmp_name_title);
    for (int i = 0; i < nl; ++i)
        printf("recursion %s@%s: %d levels of %lld bytes\n", nm(lv[i]), where(lv[i]), recursion_depth(nm(lv[i])), levels[lv[i]]);
    struct sv bk = {0};
    for (int t = 0; t < titles.n; ++t)
        if (back[t]) sv_push(&bk, xasprintf("%s@%s(%lld)", nm(t), where(t), frame_bytes[t]));
    sv_sort(&bk);
    printf("cycles entered once (a back edge into; frame bytes): ");
    if (!bk.n) printf("none");
    for (int i = 0; i < bk.n; ++i) printf("%s%s", i ? " " : "", bk.v[i]);
    printf("\n");
    for (int i = 0; i < names.n; ++i) {
        if (!nroots[i]) {
            printf("worst chain from %s: root not found\n", names.v[i]);
            ++bad;
            continue;
        }
        int r = roots[i][0];
        for (int k = 1; k < nroots[i]; ++k)
            if (best[roots[i][k]] > best[r]) r = roots[i][k];
        int over = best[r] >= opt_chain, len = 0;
        bad += over;
        for (int j = r; j >= 0; j = nxt[j]) ++len;
        printf("worst chain from %s: %lld bytes, %d frames", names.v[i], best[r], len);
        if (over) printf(" OVER %lld", opt_chain);
        printf("\n");
        long long run = 0;
        for (int j = r; j >= 0; j = nxt[j]) {
            run += weight[j];
            printf("  %7lld %7lld  %s@%s", weight[j], run, nm(j), where(j));
            if (is_level[j]) printf(" (%d levels)", recursion_depth(nm(j)));
            printf("\n");
        }
    }
    if (bad) printf("stack: %d problem(s)\n", bad);
    else printf("stack: every frame at most %lld bytes, every chain under %lld\n", opt_frame, opt_chain);
    return bad ? 1 : 0;
}

/* ------------------------------------------------------------ the cycles */

static int cmp_comp_names(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

struct comp {
    int *v, n, seq;
    char **keys; /* its members' names, sorted (the order of comps) */
};

static int cmp_comp(const void *a, const void *b)
{
    const struct comp *x = a, *y = b;
    if (x->n != y->n) return x->n > y->n ? -1 : 1;
    for (int i = 0; i < x->n && i < y->n; ++i) {
        int c = strcmp(x->keys[i], y->keys[i]);
        if (c) return c;
    }
    return x->seq - y->seq;
}


int main(int argc, char **argv)
{
    int pos = 0;
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
#define VAL() (i + 1 < argc ? argv[++i] : (die("callgraph: %s needs a value", a), ""))
        if (!strcmp(a, "--root")) opt_root = VAL();
        else if (!strcmp(a, "--pointers")) opt_pointers = 1;
        else if (!strcmp(a, "--sites")) opt_sites = VAL();
        else if (!strcmp(a, "--path")) opt_path = VAL();
        else if (!strcmp(a, "--stack")) opt_stack = 1;
        else if (!strcmp(a, "--roots")) opt_roots = VAL();
        else if (!strcmp(a, "--frame")) opt_frame = atoll(VAL());
        else if (!strcmp(a, "--chain")) opt_chain = atoll(VAL());
        else if (!strcmp(a, "--cut-worldgen")) opt_cut_worldgen = 1;
        else if (a[0] == '-' && a[1]) die("callgraph: unknown option %s", a);
        else if (pos == 0) opt_obj = a, ++pos;
        else if (pos == 1) opt_src = a, ++pos;
        else die("callgraph: too many arguments");
#undef VAL
    }
    if (pos < 2) die("usage: callgraph OBJ_DIR SRC_DIR [--pointers] [--root F] [--sites F] [--path F,G] [--stack ...]");
    {
        DIR *d = opendir(opt_obj);
        struct dirent *e;
        while (d && (e = readdir(d)))
            if (e->d_name[0] != '.' && ends_with(e->d_name, ".ci")) {
                char *p = path_join(opt_obj, e->d_name);
                parse_ci(p);
                free(p);
            }
        if (d) closedir(d);
    }
    if (!ndef) {
        fprintf(stderr, "callgraph: no .ci files in %s (build with -fcallgraph-info=su)\n", opt_obj);
        return 1;
    }
    if (opt_pointers) {
        struct sv fs = list_dir(opt_src, 0);
        srcs = xcalloc((size_t)fs.n + 1, sizeof *srcs);
        for (int i = 0; i < fs.n; ++i) {
            if (!glob_match("*.[ch]", fs.v[i])) continue;
            char *p = path_join(opt_src, fs.v[i]);
            char *raw = read_file(p, NULL);
            free(p);
            if (!raw) continue;
            struct srcfile *f = &srcs[nsrcs];
            f->name = fs.v[i];
            f->code = strip_comments(raw);
            free(raw);
            f->len = strlen(f->code);
            char *cp = xstrdup(f->code);
            for (char *l = cp;;) {
                char *e = strchr(l, '\n');
                f->lines = xrealloc(f->lines, (size_t)(f->nlines + 1) * sizeof *f->lines);
                f->lines[f->nlines++] = l;
                if (!e) break;
                *e = 0;
                l = e + 1;
            }
            for (size_t k = 0; k < f->len; ++k)
                if (f->code[k] == '\n') {
                    f->nl = xrealloc(f->nl, (size_t)(f->nnl + 1) * sizeof *f->nl);
                    f->nl[f->nnl++] = k;
                }
            smap_put(&srcidx, f->name, (void *)(intptr_t)nsrcs);
            ++nsrcs;
        }
        params_cache = xcalloc((size_t)titles.n, sizeof *params_cache);
        params_done = xcalloc((size_t)titles.n, 1);
        resolve_pointers();
    }
    if (opt_stack) return stack_report();

    int n = titles.n;
    int *roots = xmalloc((size_t)(ndef + 1) * sizeof *roots), nr = 0;
    for (int k = 0; k < ndef; ++k)
        if (!strcmp(nm(def_order[k]), opt_root)) roots[nr++] = def_order[k];
    seen = xcalloc((size_t)n, 1);
    int *todo = xmalloc((size_t)n * sizeof *todo), nt = 0, nseen = 0;
    for (int i = 0; i < nr; ++i)
        if (!seen[roots[i]]) seen[roots[i]] = 1, ++nseen, todo[nt++] = roots[i];
    while (nt) {
        int u = todo[--nt];
        for (int k = 0; k < edges[u].n; ++k) {
            int v = edges[u].v[k];
            if (in_list(CUT, nm(v)) || seen[v]) continue;
            seen[v] = 1, ++nseen;
            todo[nt++] = v;
        }
    }
    build_succ();
    /* Tarjan's strongly connected components, iterative, over sorted titles */
    int *index = xmalloc((size_t)n * sizeof *index), *low = xmalloc((size_t)n * sizeof *low);
    char *onstack = xcalloc((size_t)n, 1);
    for (int i = 0; i < n; ++i) index[i] = -1;
    int *stk = xmalloc((size_t)n * sizeof *stk), ns = 0, counter = 0;
    int *wv = xmalloc((size_t)n * sizeof *wv), *wi = xmalloc((size_t)n * sizeof *wi);
    int *order = xmalloc((size_t)n * sizeof *order), no = 0;
    for (int t = 0; t < n; ++t)
        if (seen[t]) order[no++] = t;
    sort_titles(order, no);
    struct comp *comps = NULL;
    int ncomp = 0;
    for (int oi = 0; oi < no; ++oi) {
        int root = order[oi];
        if (index[root] >= 0) continue;
        int nw = 0;
        wv[nw] = root, wi[nw++] = 0;
        index[root] = low[root] = counter++;
        stk[ns++] = root;
        onstack[root] = 1;
        while (nw) {
            int v = wv[nw - 1], advanced = 0;
            while (wi[nw - 1] < nsucc[v]) {
                int w = succ[v][wi[nw - 1]++];
                if (index[w] < 0) {
                    index[w] = low[w] = counter++;
                    stk[ns++] = w;
                    onstack[w] = 1;
                    wv[nw] = w, wi[nw++] = 0;
                    advanced = 1;
                    break;
                }
                if (onstack[w] && index[w] < low[v]) low[v] = index[w];
            }
            if (advanced) continue;
            --nw;
            if (nw && low[v] < low[wv[nw - 1]]) low[wv[nw - 1]] = low[v];
            if (low[v] == index[v]) {
                struct comp c = {0};
                for (;;) {
                    int w = stk[--ns];
                    onstack[w] = 0;
                    c.v = xrealloc(c.v, (size_t)(c.n + 1) * sizeof *c.v);
                    c.v[c.n++] = w;
                    if (w == v) break;
                }
                if (c.n > 1 || iset_has(&edges[v], v)) {
                    c.keys = xmalloc((size_t)c.n * sizeof *c.keys);
                    for (int k = 0; k < c.n; ++k) c.keys[k] = (char *)nm(c.v[k]);
                    qsort(c.keys, (size_t)c.n, sizeof *c.keys, cmp_comp_names);
                    c.seq = ncomp;
                    comps = xrealloc(comps, (size_t)(ncomp + 1) * sizeof *comps);
                    comps[ncomp++] = c;
                } else free(c.v);
            }
        }
    }
    if (ncomp > 1) qsort(comps, (size_t)ncomp, sizeof *comps, cmp_comp);
    int bad = 0;
    printf("%d functions reachable from %s%s\n", nseen, opt_root, opt_pointers ? " (function pointers resolved)" : "");
    for (int ci = 0; ci < ncomp; ++ci) {
        struct comp *c = &comps[ci];
        int wg = 1;
        for (int k = 0; k < c->n; ++k) wg &= in_list(WORLDGEN_FILES, where(c->v[k]));
        bad += !wg;
        struct sv ms = {0};
        for (int k = 0; k < c->n; ++k) sv_push(&ms, xasprintf("%s@%s", nm(c->v[k]), where(c->v[k])));
        sv_sort(&ms);
        printf("%s cycle of %d: ", wg ? "worldgen" : "TICK", c->n);
        for (int k = 0; k < ms.n; ++k) printf("%s%s", k ? " " : "", ms.v[k]);
        printf("\n");
        int has = 0;
        for (int k = 0; opt_sites && k < c->n; ++k) has |= !strcmp(nm(c->v[k]), opt_sites);
        if (!has) continue;
        int *mem = xmalloc((size_t)c->n * sizeof *mem);
        memcpy(mem, c->v, (size_t)c->n * sizeof *mem);
        /* sorted by name, stably over the component's own order */
        for (int a = 1; a < c->n; ++a) {
            int x = mem[a], b = a - 1;
            while (b >= 0 && strcmp(nm(mem[b]), nm(x)) > 0) mem[b + 1] = mem[b], --b;
            mem[b + 1] = x;
        }
        char *inside = xcalloc((size_t)n, 1);
        for (int k = 0; k < c->n; ++k) inside[c->v[k]] = 1;
        for (int k = 0; k < c->n; ++k) {
            int t = mem[k];
            int *vs = xmalloc((size_t)(edges[t].n + 1) * sizeof *vs);
            memcpy(vs, edges[t].v, (size_t)edges[t].n * sizeof *vs);
            sort_titles(vs, edges[t].n);
            for (int a = 1; a < edges[t].n; ++a) {
                int x = vs[a], b = a - 1;
                while (b >= 0 && strcmp(nm(vs[b]), nm(x)) > 0) vs[b + 1] = vs[b], --b;
                vs[b + 1] = x;
            }
            for (int j = 0; j < edges[t].n; ++j) {
                int v = vs[j];
                if (!inside[v]) continue;
                char *key = xasprintf("%d %d", t, v);
                struct sv *labs = smap_get(&labels, key);
                free(key);
                struct sv bn = {0};
                for (int q = 0; labs && q < labs->n; ++q) sv_push(&bn, (char *)base_name(labs->v[q]));
                sv_sort_uniq(&bn);
                printf("    %s -> %s  ", nm(t), nm(v));
                for (int q = 0; q < bn.n; ++q) printf("%s%s", q ? " " : "", bn.v[q]);
                printf("\n");
            }
            free(vs);
        }
        free(inside);
        free(mem);
    }
    if (opt_path) {
        char *ps = xstrdup(opt_path);
        for (char *want = ps;;) {
            char *comma = strchr(want, ',');
            if (comma) *comma = 0;
            int *prev = xmalloc((size_t)n * sizeof *prev);
            char *in = xcalloc((size_t)n, 1);
            int *queue = xmalloc((size_t)n * sizeof *queue), qh = 0, qt = 0, hit = -1;
            for (int i = 0; i < nr; ++i)
                if (!in[roots[i]]) in[roots[i]] = 1, prev[roots[i]] = -1, queue[qt++] = roots[i];
            while (qh < qt) {
                int u = queue[qh++];
                if (node_name[u] && !strcmp(node_name[u], want)) { hit = u; break; }
                int *vs = xmalloc((size_t)(edges[u].n + 1) * sizeof *vs);
                memcpy(vs, edges[u].v, (size_t)edges[u].n * sizeof *vs);
                sort_titles(vs, edges[u].n);
                for (int k = 0; k < edges[u].n; ++k) {
                    int v = vs[k];
                    if (!in_list(CUT, nm(v)) && !in[v]) in[v] = 1, prev[v] = u, queue[qt++] = v;
                }
                free(vs);
            }
            printf("path to %s: ", want);
            if (hit < 0) printf("not reachable");
            for (int h = hit, first = 1; h >= 0; h = prev[h], first = 0) printf("%s%s", first ? "" : " <- ", nm(h));
            printf("\n");
            free(prev), free(in), free(queue);
            if (!comma) break;
            want = comma + 1;
        }
    }
    if (!opt_pointers) {
        if (bad) printf("callgraph: %d cycle(s) outside worldgen\n", bad);
        else printf("callgraph: no cycle outside worldgen\n");
        return bad ? 1 : 0;
    }
    return 0;
}
