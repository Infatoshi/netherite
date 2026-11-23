/* nether CLI: the YAML reader and writer (cli_yaml.h). */
#define _GNU_SOURCE
#include "cli_yaml.h"

#include <stdlib.h>
#include <string.h>

static struct cy *cy_new(const char *key, const char *val, int line)
{
    struct cy *n = xcalloc(1, sizeof *n);
    n->key = key ? xstrdup(key) : NULL;
    n->val = val ? xstrdup(val) : NULL;
    n->line = line;
    return n;
}

static void cy_add(struct cy *p, struct cy *k)
{
    if (p->nk == p->cap) {
        p->cap = p->cap ? p->cap * 2 : 8;
        p->kid = xrealloc(p->kid, sizeof *p->kid * (size_t)p->cap);
    }
    p->kid[p->nk++] = k;
}

void cy_free(struct cy *t)
{
    if (!t) return;
    for (int i = 0; i < t->nk; ++i) cy_free(t->kid[i]);
    free(t->kid);
    free(t->key);
    free(t->val);
    free(t);
}

static struct cy *child(const struct cy *t, const char *key, size_t n)
{
    for (int i = 0; t && i < t->nk; ++i)
        if (t->kid[i]->key && strlen(t->kid[i]->key) == n && !strncmp(t->kid[i]->key, key, n)) return t->kid[i];
    return NULL;
}

/* a value's text: quotes taken off, a trailing " # comment" dropped, spaces trimmed */
static char *scalar(const char *s, int line, const char *name, char *err, size_t en, int *bad)
{
    while (*s == ' ') ++s;
    if (*s == '"' || *s == '\'') {
        char q = *s;
        struct sb b = {0};
        const char *p = s + 1;
        for (; *p && *p != q; ++p) {
            if (q == '"' && *p == '\\' && p[1]) {
                ++p;
                sb_putc(&b, *p == 'n' ? '\n' : *p == 't' ? '\t' : *p);
            } else
                sb_putc(&b, *p);
        }
        if (*p != q) {
            snprintf(err, en, "%s:%d: an unclosed %c", name, line, q);
            *bad = 1;
            sb_free(&b);
            return NULL;
        }
        for (++p; *p == ' '; ++p) {
        }
        if (*p && *p != '#') {
            snprintf(err, en, "%s:%d: text after a quoted value", name, line);
            *bad = 1;
            sb_free(&b);
            return NULL;
        }
        sb_str(&b);
        return b.s;
    }
    size_t n = strlen(s);
    for (size_t i = 0; i < n; ++i)
        if (s[i] == '#' && (i == 0 || s[i - 1] == ' ')) {
            n = i;
            break;
        }
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\r')) --n;
    return xstrndup(s, n);
}

struct cy *cy_parse(const char *text, const char *name, char *err, size_t en)
{
    struct cy *root = cy_new(NULL, NULL, 0);
    struct {
        struct cy *n;
        int ind;
    } st[64];
    int sp = 0;
    st[0].n = root;
    st[0].ind = -1;
    int line = 0;
    for (const char *p = text; *p;) {
        const char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        char *l = xstrndup(p, len);
        p += len + (e ? 1 : 0);
        ++line;
        if (len && l[len - 1] == '\r') l[--len] = 0;
        int ind = 0;
        while (l[ind] == ' ') ++ind;
        if (l[ind] == '\t') {
            snprintf(err, en, "%s:%d: a tab (spaces only)", name, line);
            goto fail_l;
        }
        if (!l[ind] || l[ind] == '#' || !strcmp(l + ind, "---")) {
            free(l);
            continue;
        }
        while (sp > 0 && st[sp].ind >= ind) --sp;
        struct cy *parent = st[sp].n;
        int bad = 0;
        if (l[ind] == '-' && (l[ind + 1] == ' ' || !l[ind + 1])) {
            if (parent->val) {
                snprintf(err, en, "%s:%d: a list item under a scalar", name, line);
                goto fail_l;
            }
            char *v = scalar(l + ind + 1, line, name, err, en, &bad);
            if (bad) goto fail_l;
            struct cy *it = cy_new(NULL, v, line);
            free(v);
            cy_add(parent, it);
            free(l);
            continue;
        }
        char *c = l + ind;
        size_t kn = 0;
        while (c[kn] && c[kn] != ':' && c[kn] != ' ') ++kn;
        if (c[kn] != ':' || !kn || (c[kn + 1] && c[kn + 1] != ' ')) {
            snprintf(err, en, "%s:%d: not \"key: value\", \"key:\" or \"- item\": %s", name, line, c);
            goto fail_l;
        }
        if (parent->val) {
            snprintf(err, en, "%s:%d: a key under the scalar %s", name, line, parent->key ? parent->key : "?");
            goto fail_l;
        }
        if (child(parent, c, kn)) {
            snprintf(err, en, "%s:%d: %.*s twice", name, line, (int)kn, c);
            goto fail_l;
        }
        char *key = xstrndup(c, kn);
        char *v = scalar(c + kn + 1, line, name, err, en, &bad);
        if (bad) {
            free(key);
            goto fail_l;
        }
        struct cy *n = cy_new(key, *v ? v : NULL, line);
        free(key);
        free(v);
        cy_add(parent, n);
        if (!n->val) {
            if (sp + 1 >= 64) {
                snprintf(err, en, "%s:%d: nested too deep", name, line);
                goto fail_l;
            }
            st[++sp].n = n;
            st[sp].ind = ind;
        }
        free(l);
        continue;
    fail_l:
        free(l);
        cy_free(root);
        return NULL;
    }
    return root;
}

struct cy *cy_load(const char *path, char *err, size_t en)
{
    double t0 = cli_now();
    char *s = cli_read_file(path, NULL);
    if (!s) {
        snprintf(err, en, "cannot read %s", path);
        cli_prof_add(CLI_P_YAML, t0);
        return NULL;
    }
    struct cy *t = cy_parse(s, path, err, en);
    free(s);
    cli_prof_add(CLI_P_YAML, t0);
    return t;
}

struct cy *cy_node(const struct cy *t, const char *path)
{
    const struct cy *n = t;
    for (const char *p = path; n && *p;) {
        size_t k = strcspn(p, ".");
        n = child(n, p, k);
        p += k;
        if (*p == '.') ++p;
    }
    return (struct cy *)n;
}

const char *cy_get(const struct cy *t, const char *path, const char *dflt)
{
    const struct cy *n = cy_node(t, path);
    return n && n->val ? n->val : dflt;
}

double cy_num(const struct cy *t, const char *path, double dflt)
{
    const char *v = cy_get(t, path, NULL);
    double x;
    return v && !cli_num(v, &x) ? x : dflt;
}

long cy_long(const struct cy *t, const char *path, long dflt)
{
    const char *v = cy_get(t, path, NULL);
    double x;
    return v && !cli_num(v, &x) ? (long)x : dflt;
}

int cy_bool(const struct cy *t, const char *path, int dflt)
{
    const char *v = cy_get(t, path, NULL);
    if (!v) return dflt;
    if (!strcmp(v, "on") || !strcmp(v, "yes") || !strcmp(v, "true") || !strcmp(v, "1")) return 1;
    if (!strcmp(v, "off") || !strcmp(v, "no") || !strcmp(v, "false") || !strcmp(v, "0")) return 0;
    return dflt;
}

int cy_set(struct cy *t, const char *path, const char *val)
{
    struct cy *n = t;
    for (const char *p = path; *p;) {
        size_t k = strcspn(p, ".");
        int last = p[k] == 0;
        struct cy *c = child(n, p, k);
        if (!c) {
            char *key = xstrndup(p, k);
            c = cy_new(key, NULL, 0);
            free(key);
            cy_add(n, c);
        }
        if (last) {
            if (c->nk) return -1;
            free(c->val);
            c->val = xstrdup(val);
            return 0;
        }
        if (c->val) return -1;
        n = c;
        p += k + 1;
    }
    return -1;
}

static int plain(const char *v)
{
    if (!*v) return 0;
    for (const char *p = v; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || strchr("_./+,:-", *p)))
            return 0;
    return 1;
}

int cy_override(struct cy *t, const char *kv, int strict, char *err, size_t en)
{
    const char *eq = strchr(kv, '=');
    if (!eq || eq == kv) {
        snprintf(err, en, "--set %s: KEY=VALUE", kv);
        return -1;
    }
    char *key = xstrndup(kv, (size_t)(eq - kv));
    const char *val = eq + 1;
    if (strict && !plain(val)) {
        snprintf(err, en, "--set %s: the value must be a plain scalar ([A-Za-z0-9_./+,:-], the run files' grammar)", kv);
        free(key);
        return -1;
    }
    char *path = NULL;
    size_t first = strcspn(key, ".");
    if (child(t, key, first) || !key[first]) path = xstrdup(key);
    else {
        /* the one top-level part that has it (sim:, policy:, actions:) */
        int hits = 0;
        for (int i = 0; i < t->nk; ++i) {
            if (!t->kid[i]->key || t->kid[i]->val) continue;
            if (cy_node(t->kid[i], key)) {
                free(path);
                path = xasprintf("%s.%s", t->kid[i]->key, key);
                ++hits;
            }
        }
        if (hits == 0) {
            /* a new key: under the part whose section exists */
            char *sec = xstrndup(key, first);
            for (int i = 0; i < t->nk; ++i)
                if (t->kid[i]->key && !t->kid[i]->val && cy_node(t->kid[i], sec)) {
                    free(path);
                    path = xasprintf("%s.%s", t->kid[i]->key, key);
                    ++hits;
                }
            free(sec);
        }
        if (hits != 1) {
            snprintf(err, en, "--set %s: %s is %s; give its full path (sim.SECTION.KEY, policy.SECTION.KEY)", kv, key,
                     hits ? "under more than one part" : "in no part of the file");
            free(key);
            free(path);
            return -1;
        }
    }
    int rc = cy_set(t, path, val);
    if (rc) snprintf(err, en, "--set %s: %s runs through a scalar", kv, path);
    free(path);
    free(key);
    return rc;
}

static int needs_quotes(const char *v)
{
    if (!*v) return 1;
    for (const char *p = v; *p; ++p)
        if (*p == '#' || *p == '\'' || *p == '"' || (*p == ':' && (p[1] == ' ' || !p[1])) || *p == '\n') return 1;
    /* a leading '-' is a list marker only before a space or alone: -0.5 stays plain (ntloop's reader takes no quotes) */
    return v[0] == ' ' || v[strlen(v) - 1] == ' ' || (v[0] == '-' && (!v[1] || v[1] == ' ')) || v[0] == '[' || v[0] == '{';
}

static void dump(const struct cy *n, int depth, struct sb *o)
{
    for (int i = 0; i < n->nk; ++i) {
        const struct cy *k = n->kid[i];
        for (int s = 0; s < depth * 2; ++s) sb_putc(o, ' ');
        if (k->key) sb_printf(o, "%s:", k->key);
        else sb_puts(o, "-");
        if (k->val) {
            sb_putc(o, ' ');
            if (needs_quotes(k->val)) {
                sb_putc(o, '"');
                for (const char *p = k->val; *p; ++p)
                    if (*p == '"' || *p == '\\') sb_putc(o, '\\'), sb_putc(o, *p);
                    else if (*p == '\n') sb_puts(o, "\\n");
                    else sb_putc(o, *p);
                sb_putc(o, '"');
            } else
                sb_puts(o, k->val);
        }
        sb_putc(o, '\n');
        if (!k->val) dump(k, depth + 1, o);
    }
}

void cy_dump(const struct cy *t, struct sb *out)
{
    dump(t, 0, out);
    sb_str(out);
}

struct cy *cy_clone(const struct cy *n)
{
    struct cy *c = cy_new(n->key, n->val, n->line);
    for (int i = 0; i < n->nk; ++i) cy_add(c, cy_clone(n->kid[i]));
    return c;
}

int cy_put(struct cy *t, const char *path, const struct cy *src)
{
    if (src->val) return cy_set(t, path, src->val);
    struct cy *n = t;
    for (const char *p = path; *p;) {
        size_t k = strcspn(p, ".");
        struct cy *c = child(n, p, k);
        if (!c) {
            char *key = xstrndup(p, k);
            c = cy_new(key, NULL, 0);
            free(key);
            cy_add(n, c);
        }
        if (c->val) return -1;
        n = c;
        p += k;
        if (*p == '.') ++p;
    }
    /* a list replaces what was there; a map merges key by key */
    int list = src->nk && !src->kid[0]->key;
    if (list) {
        for (int i = 0; i < n->nk; ++i) cy_free(n->kid[i]);
        n->nk = 0;
        for (int i = 0; i < src->nk; ++i) cy_add(n, cy_clone(src->kid[i]));
        return 0;
    }
    return cy_merge(n, src);
}

int cy_merge(struct cy *dst, const struct cy *src)
{
    for (int i = 0; i < src->nk; ++i) {
        const struct cy *k = src->kid[i];
        if (!k->key) {
            cy_add(dst, cy_clone(k));
            continue;
        }
        struct cy *d = child(dst, k->key, strlen(k->key));
        if (!d) {
            cy_add(dst, cy_clone(k));
            continue;
        }
        if (k->val) {
            if (d->nk) return -1;
            free(d->val);
            d->val = xstrdup(k->val);
        } else {
            if (d->val) return -1;
            if (cy_put(dst, k->key, k)) return -1;
        }
    }
    return 0;
}
