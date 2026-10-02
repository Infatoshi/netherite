/* index_check [RECORD.json ...] | --summary   (out/native/index_check)
 *
 * Validates codebase-index records against index/schema.json's rules and the
 * tree: every cited path exists, every cited native symbol occurs in the cited
 * file, enums are valid. With no arguments it checks every record. --summary
 * also writes index/summary.json: coverage, counts by scope and status, per
 * package, and every open item (partial or not_ported files with their missing
 * behaviors). Runs at the repository root (the one holding the current
 * directory, else the one the program was built in): make -C csrc index-check.
 * The C port of index/check.py (lane/cport): the same errors, output and
 * summary.json. */
#define _GNU_SOURCE
#include "../csrc/engine/tape.h"
#include "../csrc/tests/tool.h"

static const char *const SIDE[] = {"client", "common", "server", NULL};
static const char *const SCOPE[] = {"client_cosmetic", "in_scope", "not_needed", "switched_off", NULL};
static const char *const STATUS[] = {"exact", "not_applicable", "not_ported", "partial", NULL};
static const char *const MSTATUS[] = {"exact", "missing", "n/a", "partial", NULL};
static const char *const KIND[] = {"ctor", "field", "method", NULL};
static const char *const LAYER[] = {"client", "io", "render", "test", "tick", "tool", "util", NULL};

/* ------------------------------------------------------------ Python's view of a value */

static void py_quote(struct sb *b, const char *s)
{
    char q = strchr(s, '\'') && !strchr(s, '"') ? '"' : '\'';
    sb_putc(b, q);
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
        if (*p == (unsigned char)q || *p == '\\') sb_putc(b, '\\'), sb_putc(b, (char)*p);
        else if (*p == '\n') sb_puts(b, "\\n");
        else if (*p == '\r') sb_puts(b, "\\r");
        else if (*p == '\t') sb_puts(b, "\\t");
        else if (*p < 0x20 || *p == 0x7f) sb_printf(b, "\\x%02x", *p);
        else sb_putc(b, (char)*p);
    }
    sb_putc(b, q);
}

static int is_float_raw(const struct jval *v)
{
    for (size_t i = 0; i < v->rawlen; ++i)
        if (v->raw[i] == '.' || v->raw[i] == 'e' || v->raw[i] == 'E') return 1;
    return 0;
}

static void py_num(struct sb *b, const struct jval *v)
{
    if (is_float_raw(v)) json_num(b, v->dbl);
    else {
        /* an int: the literal (a JSON int never has a sign but '-' or leading zeros) */
        if (v->rawlen == 2 && v->raw[0] == '-' && v->raw[1] == '0') sb_putc(b, '0');
        else sb_putn(b, v->raw, v->rawlen);
    }
}

/* repr() of a value (inside a container) or str() (at the top, repr 0) */
static void py_val(struct sb *b, const struct jval *v, int repr)
{
    if (!v || v->kind == J_NULL) sb_puts(b, "None");
    else if (v->kind == J_BOOL) sb_puts(b, v->boolean ? "True" : "False");
    else if (v->kind == J_NUM) py_num(b, v);
    else if (v->kind == J_STR) {
        if (repr) py_quote(b, v->str);
        else sb_puts(b, v->str);
    } else if (v->kind == J_ARR) {
        sb_putc(b, '[');
        for (int i = 0; i < v->nitems; ++i) {
            if (i) sb_puts(b, ", ");
            py_val(b, v->items[i], 1);
        }
        sb_putc(b, ']');
    } else {
        sb_putc(b, '{');
        for (int i = 0; i < v->nfields; ++i) {
            if (i) sb_puts(b, ", ");
            py_quote(b, v->fields[i].key);
            sb_puts(b, ": ");
            py_val(b, v->fields[i].val, 1);
        }
        sb_putc(b, '}');
    }
}

static const char *pstr(const struct jval *v)
{
    static struct sb b[4];
    static int k;
    struct sb *s = &b[k++ & 3];
    s->n = 0;
    sb_str(s);
    py_val(s, v, 0);
    return s->s;
}

/* Python truthiness */
static int truthy(const struct jval *v)
{
    if (!v) return 0;
    switch (v->kind) {
    case J_NULL: return 0;
    case J_BOOL: return v->boolean;
    case J_NUM: return v->dbl != 0;
    case J_STR: return v->str[0] != 0;
    case J_ARR: return v->nitems > 0;
    default: return v->nfields > 0;
    }
}

static const char *sget(const struct jval *o, const char *k)
{
    const struct jval *v = o && o->kind == J_OBJ ? json_get(o, k) : NULL;
    return v && v->kind == J_STR ? v->str : NULL;
}

static const struct jval *get(const struct jval *o, const char *k)
{
    return o && o->kind == J_OBJ ? json_get(o, k) : NULL;
}

static int in_enum(const struct jval *v, const char *const *e)
{
    if (!v || v->kind != J_STR) return 0;
    for (; *e; ++e)
        if (!strcmp(*e, v->str)) return 1;
    return 0;
}

static const char *enum_list(const char *const *e)
{
    static struct sb b;
    b.n = 0;
    sb_putc(&b, '[');
    for (int i = 0; e[i]; ++i) sb_printf(&b, "%s'%s'", i ? ", " : "", e[i]);
    sb_putc(&b, ']');
    return b.s;
}

/* the items of a list value (a JSON array; a falsy value is no items) */
static int items(const struct jval *v, struct jval ***out)
{
    if (!truthy(v) || v->kind != J_ARR) { *out = NULL; return 0; }
    *out = v->items;
    return v->nitems;
}

static int exists(const char *p) { return p && p[0] && path_exists(p); }

/* ------------------------------------------------------------ symbols */

static struct smap srcs;

static int word(unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; }

static int has_symbol(const char *path, const char *sym)
{
    int x = smap_find(&srcs, path);
    if (x < 0) {
        x = smap_add(&srcs, path);
        struct stat st;
        srcs.vals[x] = stat(path, &st) == 0 && !S_ISDIR(st.st_mode) ? read_file(path, NULL) : NULL;
    }
    const char *s = srcs.vals[x];
    if (!s) return 0;
    size_t n = strlen(sym), len = strlen(s);
    for (const char *p = s; p + n <= s + len; ++p) {
        if (n && !(p = strstr(p, sym))) return 0;
        if ((p == s || !word((unsigned char)p[-1])) && !word((unsigned char)p[n])) return 1;
        if (!n && !*p) break;
    }
    return 0;
}

/* ------------------------------------------------------------ one record */

struct errs {
    struct sv v;
};

static void err(struct errs *e, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void err(struct errs *e, const char *fmt, ...)
{
    struct sb b = {0};
    va_list ap;
    va_start(ap, fmt);
    sb_vprintf(&b, fmt, ap);
    va_end(ap);
    sv_push(&e->v, (char *)sb_str(&b));
}

static struct jval *check(const char *rec_path, struct errs *e)
{
    size_t n;
    char *text = read_file(rec_path, &n);
    struct jval *r = text ? json_parse(text) : NULL;
    if (!r) {
        err(e, "not JSON: %s", text ? "a syntax error" : strerror(errno));
        return NULL;
    }
    char kind[256] = "?";
    if (starts_with(rec_path, "index/")) {
        const char *a = rec_path + 6, *s = strchr(a, '/');
        size_t k = s ? (size_t)(s - a) : strlen(a);
        if (k >= sizeof kind) k = sizeof kind - 1;
        memcpy(kind, a, k);
        kind[k] = 0;
    }
    const struct jval *pv = get(r, "path");
    const char *p = pv && pv->kind == J_STR ? pv->str : NULL;
    if (!truthy(pv) || !exists(p)) err(e, "path missing or not in tree: %s", pstr(pv));
    else if (!strcmp(kind, "java") || !strcmp(kind, "csrc") || !strcmp(kind, "oracle")) {
        char *want;
        if (!strcmp(kind, "java")) want = xasprintf("index/java/%s.json", strlen(p) > 11 ? p + 11 : "");   /* past oracle/src/ */
        else if (!strcmp(kind, "csrc")) want = xasprintf("index/%s.json", p);
        else want = xasprintf("index/oracle/%s.json", base_name(p));
        char *np = norm_path(rec_path);
        if (strcmp(np, want)) err(e, "record is at %s; it belongs at %s (move it there)", rec_path, want);
        free(np);
        free(want);
    }
    struct jval **it;
    int ni;
    if (!strcmp(kind, "java")) {
        static const char *const keys[] = {"classes", "lines", "side", "category", "role", "scope", "status",
                                           "native", "evidence", "members", "missing", NULL};
        for (int i = 0; keys[i]; ++i)
            if (!get(r, keys[i])) err(e, "key missing: %s", keys[i]);
        static const char *const ek[] = {"side", "scope", "status"};
        const char *const *ev[] = {SIDE, SCOPE, STATUS};
        for (int i = 0; i < 3; ++i) {
            const struct jval *v = get(r, ek[i]);
            if (v && !in_enum(v, ev[i])) err(e, "%s not one of %s: %s", ek[i], enum_list(ev[i]), pstr(v));
        }
        const char *scope = sget(r, "scope");
        const struct jval *st = get(r, "status");
        if (!(scope && !strcmp(scope, "in_scope")) && st && st->kind != J_NULL &&
            !(st->kind == J_STR && !strcmp(st->str, "not_applicable")))
            err(e, "scope is not in_scope, so status must be not_applicable");
        ni = items(get(r, "native"), &it);
        for (int i = 0; i < ni; ++i) {
            const struct jval *f = get(it[i], "file");
            const char *fs = f && f->kind == J_STR ? f->str : "";
            if (!exists(fs)) { err(e, "native file missing: %s", f ? pstr(f) : ""); continue; }
            struct jval **sy;
            int ns = items(get(it[i], "symbols"), &sy);
            for (int k = 0; k < ns; ++k) {
                const char *s = sy[k]->kind == J_STR ? sy[k]->str : pstr(sy[k]);
                if (!has_symbol(fs, s)) err(e, "symbol not in %s: %s", fs, s);
            }
        }
        ni = items(get(r, "evidence"), &it);
        for (int i = 0; i < ni; ++i)
            if (!(it[i]->kind == J_STR && exists(it[i]->str))) err(e, "evidence missing: %s", pstr(it[i]));
        ni = items(get(r, "members"), &it);
        for (int i = 0; i < ni; ++i) {
            const struct jval *m = it[i], *nm = get(m, "name");
            if (!in_enum(get(m, "kind"), KIND)) err(e, "member kind invalid: %s: %s", pstr(nm), pstr(get(m, "kind")));
            if (!in_enum(get(m, "status"), MSTATUS)) err(e, "member status invalid: %s: %s", pstr(nm), pstr(get(m, "status")));
            const struct jval *nv = get(m, "native");
            if (truthy(nv) && nv->kind == J_STR) {
                const char *c = strchr(nv->str, ':');
                if (!c) { err(e, "member native not file:symbol: %s: %s", pstr(nm), nv->str); continue; }
                char *f = xstrndup(nv->str, (size_t)(c - nv->str));
                if (!exists(f)) err(e, "member native file missing: %s", nv->str);
                else if (!has_symbol(f, c + 1)) err(e, "member native symbol not in file: %s", nv->str);
                free(f);
            }
        }
    } else if (!strcmp(kind, "csrc")) {
        static const char *const keys[] = {"lines", "role", "layer", "java", "structs", "entry_points", "tick_path",
                                           "gpu", "tests", NULL};
        for (int i = 0; keys[i]; ++i)
            if (!get(r, keys[i])) err(e, "key missing: %s", keys[i]);
        if (!in_enum(get(r, "layer"), LAYER)) err(e, "layer invalid: %s", pstr(get(r, "layer")));
        ni = items(get(r, "java"), &it);
        for (int i = 0; i < ni; ++i) {
            const char *jp = sget(it[i], "path");
            if (!exists(jp ? jp : "")) err(e, "java path missing: %s", pstr(get(it[i], "path")));
        }
        ni = items(get(r, "entry_points"), &it);
        for (int i = 0; i < ni; ++i)
            if (truthy(pv) && exists(p) && !has_symbol(p, it[i]->kind == J_STR ? it[i]->str : pstr(it[i])))
                err(e, "entry point not in file: %s", pstr(it[i]));
        ni = items(get(r, "tests"), &it);
        for (int i = 0; i < ni; ++i)
            if (!(it[i]->kind == J_STR && exists(it[i]->str))) err(e, "test missing: %s", pstr(it[i]));
    } else if (!strcmp(kind, "oracle")) {
        static const char *const keys[] = {"lines", "role", "kind", "used_by", "vanilla_hooks", NULL};
        for (int i = 0; keys[i]; ++i)
            if (!get(r, keys[i])) err(e, "key missing: %s", keys[i]);
        ni = items(get(r, "vanilla_hooks"), &it);
        for (int i = 0; i < ni; ++i)
            if (!(it[i]->kind == J_STR && exists(it[i]->str))) err(e, "hook file missing: %s", pstr(it[i]));
    }
    return r;
}

/* ------------------------------------------------------------ summary.json */

/* json.dump(..., indent=1) of a parsed value at depth d */
static void dump(struct sb *b, const struct jval *v, int d)
{
    if (!v || v->kind == J_NULL) sb_puts(b, "null");
    else if (v->kind == J_BOOL) sb_puts(b, v->boolean ? "true" : "false");
    else if (v->kind == J_NUM) py_num(b, v);
    else if (v->kind == J_STR) json_esc(b, v->str);
    else if (v->kind == J_ARR) {
        if (!v->nitems) { sb_puts(b, "[]"); return; }
        sb_putc(b, '[');
        for (int i = 0; i < v->nitems; ++i) {
            sb_printf(b, "%s\n%*s", i ? "," : "", d + 1, "");
            dump(b, v->items[i], d + 1);
        }
        sb_printf(b, "\n%*s]", d, "");
    } else {
        if (!v->nfields) { sb_puts(b, "{}"); return; }
        sb_putc(b, '{');
        for (int i = 0; i < v->nfields; ++i) {
            sb_printf(b, "%s\n%*s", i ? "," : "", d + 1, "");
            json_esc(b, v->fields[i].key);
            sb_puts(b, ": ");
            dump(b, v->fields[i].val, d + 1);
        }
        sb_printf(b, "\n%*s}", d, "");
    }
}

/* a dict key as json.dumps writes a non-string key */
static void key_of(struct sb *b, const struct jval *v)
{
    struct sb t = {0};
    if (!v || v->kind == J_NULL) sb_puts(&t, "null");
    else if (v->kind == J_BOOL) sb_puts(&t, v->boolean ? "true" : "false");
    else if (v->kind == J_NUM) py_num(&t, v);
    else if (v->kind == J_STR) sb_puts(&t, v->str);
    else py_val(&t, v, 1);
    json_esc(b, sb_str(&t));
    sb_free(&t);
}

/* a Counter: keys (as written) in first-seen order, counts */
struct counter {
    struct smap m;
};

static void count(struct counter *c, const struct jval *v)
{
    struct sb k = {0};
    key_of(&k, v);
    int x = smap_add(&c->m, sb_str(&k));
    c->m.vals[x] = (void *)((intptr_t)c->m.vals[x] + 1);
    sb_free(&k);
}

static void dump_counter(struct sb *b, const struct counter *c, int d)
{
    if (!c->m.n) { sb_puts(b, "{}"); return; }
    sb_putc(b, '{');
    for (int i = 0; i < c->m.n; ++i)
        sb_printf(b, "%s\n%*s%s: %ld", i ? "," : "", d + 1, "", c->m.keys[i], (long)(intptr_t)c->m.vals[i]);
    sb_printf(b, "\n%*s}", d, "");
}

static void dump_strs(struct sb *b, const struct sv *a, int d)
{
    if (!a->n) { sb_puts(b, "[]"); return; }
    sb_putc(b, '[');
    for (int i = 0; i < a->n; ++i) {
        sb_printf(b, "%s\n%*s", i ? "," : "", d + 1, "");
        json_esc(b, a->v[i]);
    }
    sb_printf(b, "\n%*s]", d, "");
}

/* every file under base whose name matches pat, sorted: a recursive glob (hidden names skipped, links followed) */
static struct sv glob_rec(const char *base, const char *pat)
{
    struct sv all = {0}, out = {0};
    walk_tree(base, "", 0, &all, 0);
    for (int i = 0; i < all.n; ++i) {
        if (glob_match(pat, base_name(all.v[i]))) sv_push(&out, path_join(base, all.v[i]));
        free(all.v[i]);
    }
    free(all.v);
    sv_sort(&out);
    return out;
}

static int find_root(const char *argv0, char *out, size_t n)
{
    char cwd[4096];
    if (getcwd(cwd, sizeof cwd)) {
        for (;;) {
            char *a = xasprintf("%s/index/schema.json", cwd), *b = xasprintf("%s/csrc/Makefile", cwd);
            int ok = is_file(a) && is_file(b);
            free(a), free(b);
            if (ok) { snprintf(out, n, "%s", cwd); return 1; }
            char *s = strrchr(cwd, '/');
            if (!s || s == cwd) break;
            *s = 0;
        }
    }
    char rp[4096];
    if (strchr(argv0, '/') && realpath(argv0, rp)) {
        for (int up = 0; up < 3; ++up) {
            char *s = strrchr(rp, '/');
            if (!s) return 0;
            *s = 0;
        }
        char *a = xasprintf("%s/index/schema.json", rp);
        int ok = is_file(a);
        free(a);
        if (ok) { snprintf(out, n, "%s", rp); return 1; }
    }
    return 0;
}

int main(int argc, char **argv)
{
    char root[4096];
    if (!find_root(argv[0], root, sizeof root)) die("index_check: no repository (index/schema.json) above the current directory");
    if (chdir(root)) die("index_check: chdir %s: %s", root, strerror(errno));
    int summary = 0;
    struct sv recs = {0};
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--summary")) summary = 1;
        else sv_push(&recs, argv[i]);
    }
    if (!recs.n) {
        struct sv kinds = list_dir("index", 0);
        for (int i = 0; i < kinds.n; ++i) {
            char *d = path_join("index", kinds.v[i]);
            if (is_dir(d)) {
                struct sv f = glob_rec(d, "*.json");
                for (int k = 0; k < f.n; ++k) sv_push(&recs, f.v[k]);
            }
        }
        sv_sort(&recs);
    }
    int bad = 0;
    struct smap data = {0};
    for (int i = 0; i < recs.n; ++i) {
        struct errs e = {{0}};
        struct jval *r = check(recs.v[i], &e);
        if (e.v.n) {
            ++bad;
            printf("FAIL %s\n", recs.v[i]);
            for (int k = 0; k < e.v.n && k < 12; ++k) printf("  - %s\n", e.v.v[k]);
        }
        if (r) smap_put(&data, recs.v[i], r);
    }
    printf("%d ok, %d with errors, %d records\n", recs.n - bad, bad, recs.n);
    if (!summary) return bad ? 1 : 0;

    struct sv java_all = glob_rec("oracle/src", "*.java"), nat_all = glob_rec("csrc", "*.[ch]");
    struct smap jr = {0}, nr = {0};
    for (int i = 0; i < data.n; ++i) {
        const struct jval *r = data.vals[i];
        const struct jval *p = get(r, "path");
        if (!p) continue;
        struct sb k = {0};
        key_of(&k, p);
        if (starts_with(data.keys[i], "index/java/")) smap_put(&jr, p->kind == J_STR ? p->str : sb_str(&k), (void *)r);
        else if (starts_with(data.keys[i], "index/csrc/")) smap_put(&nr, p->kind == J_STR ? p->str : sb_str(&k), (void *)r);
        sb_free(&k);
    }
    struct counter by_scope = {{0}}, by_status = {{0}}, by_cat = {{0}};
    struct smap pkg = {0};
    struct sv junidx = {0}, nunidx = {0};
    for (int i = 0; i < java_all.n; ++i)
        if (!smap_has(&jr, java_all.v[i])) sv_push(&junidx, java_all.v[i]);
    for (int i = 0; i < nat_all.n; ++i)
        if (!smap_has(&nr, nat_all.v[i])) sv_push(&nunidx, nat_all.v[i]);
    for (int i = 0; i < jr.n; ++i) {
        const struct jval *r = jr.vals[i];
        count(&by_scope, get(r, "scope"));
        count(&by_status, get(r, "status"));
        count(&by_cat, get(r, "category"));
        char *dn = dir_name(jr.keys[i]);
        const char *pk = strlen(dn) > 11 ? dn + 11 : "";   /* past oracle/src/ */
        int x = smap_add(&pkg, pk);
        if (!pkg.vals[x]) pkg.vals[x] = xcalloc(1, sizeof(struct counter));
        count(pkg.vals[x], get(r, "status"));
        free(dn);
    }
    struct sb b = {0};
    sb_printf(&b, "{\n \"java_files\": %d,\n \"java_indexed\": %d,\n \"java_unindexed\": ", java_all.n, jr.n);
    dump_strs(&b, &junidx, 1);
    sb_printf(&b, ",\n \"native_files\": %d,\n \"native_indexed\": %d,\n \"native_unindexed\": ", nat_all.n, nr.n);
    dump_strs(&b, &nunidx, 1);
    sb_puts(&b, ",\n \"by_scope\": ");
    dump_counter(&b, &by_scope, 1);
    sb_puts(&b, ",\n \"by_status\": ");
    dump_counter(&b, &by_status, 1);
    sb_puts(&b, ",\n \"by_category\": ");
    dump_counter(&b, &by_cat, 1);
    sb_puts(&b, ",\n \"by_package\": ");
    {
        struct sv keys = {0};
        for (int i = 0; i < pkg.n; ++i) sv_push(&keys, pkg.keys[i]);
        sv_sort(&keys);
        if (!keys.n) sb_puts(&b, "{}");
        else {
            sb_putc(&b, '{');
            for (int i = 0; i < keys.n; ++i) {
                sb_printf(&b, "%s\n  ", i ? "," : "");
                json_esc(&b, keys.v[i]);
                sb_puts(&b, ": ");
                dump_counter(&b, smap_get(&pkg, keys.v[i]), 2);
            }
            sb_puts(&b, "\n }");
        }
    }
    sb_puts(&b, ",\n \"open\": ");
    int nopen = 0;
    {
        struct sb o = {0};
        for (int i = 0; i < jr.n; ++i) {
            const struct jval *r = jr.vals[i];
            const char *sc = sget(r, "scope"), *st = sget(r, "status");
            if (!sc || strcmp(sc, "in_scope") || !st || (strcmp(st, "partial") && strcmp(st, "not_ported"))) continue;
            sb_printf(&o, "%s\n  {\n   \"path\": ", nopen ? "," : "");
            dump(&o, get(r, "path"), 3);
            sb_puts(&o, ",\n   \"status\": ");
            dump(&o, get(r, "status"), 3);
            sb_puts(&o, ",\n   \"scope\": ");
            dump(&o, get(r, "scope"), 3);
            sb_puts(&o, ",\n   \"missing\": ");
            const struct jval *mi = get(r, "missing");
            if (mi) dump(&o, mi, 3);
            else sb_puts(&o, "[]");
            sb_puts(&o, ",\n   \"members_missing\": ");
            struct jval **ms;
            const struct jval *mv = get(r, "members");
            int nm = mv && mv->kind == J_ARR ? (ms = mv->items, mv->nitems) : (ms = NULL, 0);
            int k = 0;
            for (int j = 0; j < nm; ++j) {
                const char *s = sget(ms[j], "status");
                if (!s || (strcmp(s, "missing") && strcmp(s, "partial"))) continue;
                sb_printf(&o, "%s\n    ", k++ ? "," : "[");
                dump(&o, get(ms[j], "name"), 4);
            }
            sb_puts(&o, k ? "\n   ]" : "[]");
            sb_puts(&o, "\n  }");
            ++nopen;
        }
        if (nopen) sb_printf(&b, "[%s\n ]", sb_str(&o));
        else sb_puts(&b, "[]");
        sb_free(&o);
    }
    sb_puts(&b, ",\n \"gpu_hazards\": ");
    {
        int k = 0;
        for (int i = 0; i < nr.n; ++i) {
            const struct jval *r = nr.vals[i];
            if (!truthy(get(r, "tick_path")) || !truthy(get(r, "gpu"))) continue;
            sb_printf(&b, "%s\n  {\n   \"path\": ", k++ ? "," : "[");
            dump(&b, get(r, "path"), 3);
            sb_puts(&b, ",\n   \"gpu\": ");
            dump(&b, get(r, "gpu"), 3);
            sb_puts(&b, "\n  }");
        }
        sb_puts(&b, k ? "\n ]" : "[]");
    }
    sb_puts(&b, "\n}");
    if (write_file("index/summary.json", b.s, b.n)) die("index_check: cannot write index/summary.json");
    printf("summary: %d/%d java, %d/%d native, open in-scope files %d\n", jr.n, java_all.n, nr.n, nat_all.n, nopen);
    return 0;
}
