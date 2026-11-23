#include "tape.h"
#include "gunzip.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

/* --------------------------------------------------------------- the reader */

/* A tape row's values, keys, strings and line copy: one bump region per
 * tape, rewound for each row (a row lives until the next). */
struct jarena {
    char *base;
    size_t used, cap;
    char **extra;
    int nextra, capextra;
    size_t extra_bytes;
};

struct reader {
    char *p;
    int failed;
    struct jarena *a;   /* NULL: every value malloc'd, json_free releases it */
};

static void *xmalloc(size_t n);
static void *xrealloc(void *p, size_t n);

static void *arena_alloc(struct jarena *a, size_t n)
{
    n = (n + 7) & ~(size_t)7;
    if (a->used + n <= a->cap)
    {
        void *p = a->base + a->used;
        a->used += n;
        return p;
    }
    /* past the block: a block of its own until the next rewind, which
     * grows the first block to hold them all */
    if (a->nextra == a->capextra)
    {
        a->capextra = a->capextra ? 2 * a->capextra : 16;
        a->extra = xrealloc(a->extra, (size_t)a->capextra * sizeof *a->extra);
    }
    char *p = xmalloc(n);
    a->extra[a->nextra++] = p;
    a->extra_bytes += n;
    return p;
}

static void arena_rewind(struct jarena *a)
{
    if (a->nextra)
    {
        for (int i = 0; i < a->nextra; ++i) free(a->extra[i]);
        size_t want = a->cap + a->extra_bytes;
        a->nextra = 0;
        a->extra_bytes = 0;
        free(a->base);
        a->cap = want > 2 * a->cap ? want : 2 * a->cap;
        a->base = xmalloc(a->cap);
    }
    a->used = 0;
}

static void arena_free(struct jarena *a)
{
    if (a == NULL) return;
    for (int i = 0; i < a->nextra; ++i) free(a->extra[i]);
    free(a->extra);
    free(a->base);
    free(a);
}

static void *ralloc(struct reader *r, size_t n)
{
    return r->a ? arena_alloc(r->a, n) : xmalloc(n);
}

static void *rgrow(struct reader *r, void *p, size_t old, size_t n)
{
    if (!r->a) return xrealloc(p, n);
    void *q = arena_alloc(r->a, n);
    memcpy(q, p, old);
    return q;
}

/* json_free inside the parser: nothing for an arena's values */
static void rfree_value(struct reader *r, struct jval *v)
{
    if (!r->a) json_free(v);
}

static void rfree(struct reader *r, void *p)
{
    if (!r->a) free(p);
}

static void *xmalloc(size_t n)
{
    void *p = malloc(n);
    if (!p) { fprintf(stderr, "tape: out of memory\n"); exit(2); }
    return p;
}

static void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n);
    if (!q) { fprintf(stderr, "tape: out of memory\n"); exit(2); }
    return q;
}

static char *xstrndup(const char *s, size_t n)
{
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

static void skip_ws(struct reader *r)
{
    while (*r->p == ' ' || *r->p == '\t' || *r->p == '\r' || *r->p == '\n') ++r->p;
}

static struct jval *parse_value(struct reader *r);

static void utf8(char **out, unsigned cp)
{
    char *o = *out;
    if (cp < 0x80) *o++ = (char)cp;
    else if (cp < 0x800) { *o++ = (char)(0xc0 | cp >> 6); *o++ = (char)(0x80 | (cp & 63)); }
    else if (cp < 0x10000) { *o++ = (char)(0xe0 | cp >> 12); *o++ = (char)(0x80 | (cp >> 6 & 63)); *o++ = (char)(0x80 | (cp & 63)); }
    else { *o++ = (char)(0xf0 | cp >> 18); *o++ = (char)(0x80 | (cp >> 12 & 63)); *o++ = (char)(0x80 | (cp >> 6 & 63)); *o++ = (char)(0x80 | (cp & 63)); }
    *out = o;
}

static int hex4(const char *p, unsigned *out)
{
    unsigned v = 0;
    for (int i = 0; i < 4; ++i)
    {
        char c = p[i];
        int d = c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1));
        if (d < 0) return 0;
        v = v << 4 | (unsigned)d;
    }
    *out = v;
    return 1;
}

/* A string, with the escapes Gson writes. The result is allocated. */
static char *parse_string(struct reader *r)
{
    if (*r->p != '"') { r->failed = 1; return NULL; }
    ++r->p;

    /* no escape before the closing quote: one copy */
    const char *q = r->p;
    while (*q && *q != '"' && *q != '\\') ++q;
    if (*q == '"')
    {
        size_t n = (size_t)(q - r->p);
        char *s = ralloc(r, n + 1);
        memcpy(s, r->p, n);
        s[n] = 0;
        r->p = (char *)q + 1;
        return s;
    }

    size_t cap = 32, n = 0;
    char *s = ralloc(r, cap);
    while (*r->p && *r->p != '"')
    {
        if (n + 8 > cap) { s = rgrow(r, s, cap, 2 * cap); cap *= 2; }
        if (*r->p == '\\')
        {
            ++r->p;
            char c = *r->p++;
            switch (c)
            {
                case 'n': s[n++] = '\n'; break;
                case 't': s[n++] = '\t'; break;
                case 'r': s[n++] = '\r'; break;
                case 'b': s[n++] = '\b'; break;
                case 'f': s[n++] = '\f'; break;
                case 'u':
                {
                    unsigned cp;
                    if (!hex4(r->p, &cp)) { rfree(r, s); r->failed = 1; return NULL; }
                    r->p += 4;
                    /* a surrogate pair is one code point */
                    if (cp >= 0xd800 && cp < 0xdc00 && r->p[0] == '\\' && r->p[1] == 'u')
                    {
                        unsigned lo;
                        if (hex4(r->p + 2, &lo) && lo >= 0xdc00 && lo < 0xe000)
                        {
                            cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
                            r->p += 6;
                        }
                    }
                    char *o = s + n;
                    utf8(&o, cp);
                    n = (size_t)(o - s);
                    break;
                }
                default: s[n++] = c; break;
            }
        }
        else s[n++] = *r->p++;
    }
    if (*r->p != '"') { rfree(r, s); r->failed = 1; return NULL; }
    ++r->p;
    s[n] = 0;
    return s;
}

static struct jval *new_value(struct reader *r, int kind)
{
    struct jval *v = ralloc(r, sizeof *v);
    memset(v, 0, sizeof *v);
    v->kind = kind;
    return v;
}

/* strtod(p, &end) where the number's digits allow an exact shortcut
 * (Clinger's): at most 15 significant digits and at most 22 after the point,
 * no exponent; the value is then m / 10^k with m and 10^k exact doubles, one
 * correctly rounded division, which is what strtod's correct rounding
 * gives. Anything else is strtod's. */
static double parse_number(const char *p, char **end)
{
    static const double P10[23] = {1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
                                   1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
    const char *q = p;
    int neg = *q == '-';
    q += neg;
    if (*q < '0' || *q > '9') return strtod(p, end);

    uint64_t m = 0;
    int sig = 0, frac = 0;
    while (*q >= '0' && *q <= '9')
    {
        if (m || *q != '0') ++sig;
        m = m * 10 + (uint64_t)(*q - '0');
        ++q;
        if (sig > 15) return strtod(p, end);
    }
    if (*q == '.')
    {
        ++q;
        if (*q < '0' || *q > '9') return strtod(p, end);
        while (*q >= '0' && *q <= '9')
        {
            if (m || *q != '0') ++sig;
            m = m * 10 + (uint64_t)(*q - '0');
            ++q;
            ++frac;
            if (sig > 15 || frac > 22) return strtod(p, end);
        }
    }
    if (*q == 'e' || *q == 'E') return strtod(p, end);

    double d = (double)m / P10[frac];
    *end = (char *)q;
    return neg ? -d : d;
}

static const char *const NAMED[] = {"null", "true", "false"};

static struct jval *parse_value(struct reader *r)
{
    skip_ws(r);
    const char *start = r->p;
    struct jval *v;

    if (*r->p == '{' || *r->p == '[')
    {
        int obj = *r->p == '{';
        ++r->p;
        v = new_value(r, obj ? J_OBJ : J_ARR);
        int cap = 8;
        if (obj) v->fields = ralloc(r, (size_t)cap * sizeof *v->fields);
        else v->items = ralloc(r, (size_t)cap * sizeof *v->items);
        skip_ws(r);

        if ((obj && *r->p == '}') || (!obj && *r->p == ']')) ++r->p;
        else for (;;)
        {
            if (obj)
            {
                skip_ws(r);
                char *key = parse_string(r);
                if (!key) { rfree_value(r, v); return NULL; }
                skip_ws(r);
                if (*r->p != ':') { rfree(r, key); rfree_value(r, v); return NULL; }
                ++r->p;
                struct jval *val = parse_value(r);
                if (!val) { rfree(r, key); rfree_value(r, v); return NULL; }
                if (v->nfields == cap)
                {
                    v->fields = rgrow(r, v->fields, (size_t)cap * sizeof *v->fields, (size_t)cap * 2 * sizeof *v->fields);
                    cap *= 2;
                }
                v->fields[v->nfields].key = key;
                v->fields[v->nfields].val = val;
                ++v->nfields;
            }
            else
            {
                struct jval *val = parse_value(r);
                if (!val) { rfree_value(r, v); return NULL; }
                if (v->nitems == cap)
                {
                    v->items = rgrow(r, v->items, (size_t)cap * sizeof *v->items, (size_t)cap * 2 * sizeof *v->items);
                    cap *= 2;
                }
                v->items[v->nitems++] = val;
            }

            skip_ws(r);
            if (*r->p == ',') { ++r->p; continue; }
            if (obj ? *r->p == '}' : *r->p == ']') { ++r->p; break; }
            rfree_value(r, v);
            return NULL;
        }
    }
    else if (*r->p == '"')
    {
        v = new_value(r, J_STR);
        v->str = parse_string(r);
        if (!v->str) { rfree(r, v); return NULL; }
    }
    else if (*r->p == 't' || *r->p == 'f' || *r->p == 'n')
    {
        v = new_value(r, J_BOOL);
        if (!strncmp(r->p, NAMED[1], 4)) { v->boolean = 1; r->p += 4; }
        else if (!strncmp(r->p, NAMED[2], 5)) { v->boolean = 0; r->p += 5; }
        else if (!strncmp(r->p, NAMED[0], 4)) { v->kind = J_NULL; r->p += 4; }
        else { rfree(r, v); r->failed = 1; return NULL; }
    }
    else
    {
        char *end = NULL;
        double d = parse_number(r->p, &end);
        if (end == r->p) { r->failed = 1; return NULL; }
        v = new_value(r, J_NUM);
        v->dbl = d;
        v->has_dbl = 1;
        if (d == (double)(int64_t)d) { v->num = (int64_t)d; v->has_num = 1; }
        r->p = end;
    }

    v->raw = start;
    v->rawlen = (size_t)(r->p - start);
    return v;
}

struct jval *json_parse(char *text)
{
    struct reader r = {text, 0, NULL};
    struct jval *v = parse_value(&r);
    if (!v) { free(text); return NULL; }
    skip_ws(&r);
    v->owned = text; /* the root owns the line every raw slice points into */
    return v;
}

void json_free(struct jval *v)
{
    if (!v) return;

    for (int i = 0; i < v->nfields; ++i)
    {
        free(v->fields[i].key);
        json_free(v->fields[i].val);
    }
    free(v->fields);

    for (int i = 0; i < v->nitems; ++i) json_free(v->items[i]);
    free(v->items);
    free(v->str);

    free(v->owned);
    free(v);
}

const struct jval *json_get(const struct jval *o, const char *key)
{
    if (!o || o->kind != J_OBJ) return NULL;

    for (int i = 0; i < o->nfields; ++i)
        if (!strcmp(o->fields[i].key, key)) return o->fields[i].val;

    return NULL;
}

const struct jval *json_at(const struct jval *a, int i)
{
    return a && a->kind == J_ARR && i >= 0 && i < a->nitems ? a->items[i] : NULL;
}

int json_len(const struct jval *a)
{
    return a && a->kind == J_ARR ? a->nitems : -1;
}

const char *json_str(const struct jval *v)
{
    return v && v->kind == J_STR ? v->str : NULL;
}

int json_int(const struct jval *v, int64_t *out)
{
    if (!v) return 0;

    if (v->kind == J_NUM) { *out = (int64_t)v->dbl; return 1; }
    if (v->kind == J_BOOL) { *out = v->boolean; return 1; }

    if (v->kind == J_STR)
    {
        char c = v->str[0];
        if (c == 'b' || c == 's' || c == 'i' || c == 'l') { *out = strtoll(v->str + 2, NULL, 10); return 1; }
    }

    return 0;
}

/* The 8 or 16 hex digits after a canonical "f:" / "d:" prefix. */
static int hex_bits(const char *s, int digits, uint64_t *bits)
{
    if (!s || (int)strlen(s) != digits) return 0;
    char *end = NULL;
    uint64_t b = strtoull(s, &end, 16);
    if (end != s + digits) return 0;
    *bits = b;
    return 1;
}

int json_double(const struct jval *v, uint64_t *bits)
{
    if (!v) return 0;

    if (v->kind == J_NUM)
    {
        double d = v->dbl;
        memcpy(bits, &d, sizeof d);
        return 1;
    }

    if (v->kind == J_STR && v->str[0] == 'd' && v->str[1] == ':')
        return hex_bits(v->str + 2, 16, bits);

    return 0;
}

int json_float(const struct jval *v, uint32_t *bits)
{
    if (!v) return 0;

    if (v->kind == J_NUM)
    {
        float f = (float)v->dbl;
        memcpy(bits, &f, sizeof f);
        return 1;
    }

    uint64_t b;
    if (v->kind == J_STR && v->str[0] == 'f' && v->str[1] == ':' && hex_bits(v->str + 2, 8, &b))
    {
        *bits = (uint32_t)b;
        return 1;
    }

    return 0;
}

char *json_raw(const struct jval *v)
{
    return v ? xstrndup(v->raw, v->rawlen) : NULL;
}

/* ------------------------------------------------------------------- lines */

void lines_init(struct lines *l)
{
    memset(l, 0, sizeof *l);
    l->cap = 1 << 16;
    l->buf = xmalloc(l->cap);
}

void lines_file(struct lines *l, FILE *f) { l->f = f; }
void lines_gz(struct lines *l, void *gz) { l->g = gz; }
void lines_gunzip(struct lines *l, void *z) { l->z = z; }

/* Up to n bytes from the line source. */
static int lines_read(struct lines *l, char *dst, size_t n)
{
    if (l->f) return (int)fread(dst, 1, n, (FILE *)l->f);
    if (l->z) return (int)gunzip_read((struct gunzip *)l->z, dst, n);
    return gzread((gzFile)l->g, dst, (unsigned)n);
}

/* One more bufferful. 0 at the end. */
static int lines_fill(struct lines *l)
{
    if (l->pos < l->len || l->eof) return l->pos < l->len;

    l->pos = 0;
    l->len = 0;
    int n = lines_read(l, l->buf, l->cap);

    if (n <= 0) { l->eof = 1; return 0; }

    l->len = (size_t)n;
    return 1;
}

const char *lines_next(struct lines *l)
{
    for (;;)
    {
        if (!lines_fill(l)) return NULL;

        char *nl = memchr(l->buf + l->pos, '\n', l->len - l->pos);
        if (nl)
        {
            size_t n = (size_t)(nl - (l->buf + l->pos));
            if (n && l->buf[l->pos + n - 1] == '\r') --n;
            char *line = l->buf + l->pos;
            l->pos += (size_t)(nl - (l->buf + l->pos)) + 1;
            line[n] = 0;
            return line;
        }

        /* a line split across the buffer: keep the tail and read more */
        size_t have = l->len - l->pos;
        if (l->pos + have == l->cap)
        {
            memmove(l->buf, l->buf + l->pos, have);
            l->pos = 0;
            l->len = have;
            l->cap *= 2;
            l->buf = xrealloc(l->buf, l->cap);
        }

        /* read more without dropping the tail */
        size_t room = l->cap - l->len;
        int n = lines_read(l, l->buf + l->len, room);
        if (n <= 0) { l->eof = 1; char *line = l->buf + l->pos; size_t nn = l->len - l->pos; if (!nn) return NULL; line[nn] = 0; l->pos = l->len; return line; }
        l->len += (size_t)n;
    }
}

void lines_free(struct lines *l)
{
    free(l->buf);
    l->buf = NULL;
}

/* -------------------------------------------------------------------- tape */

int tape_open(struct tape *t, const char *path)
{
    memset(t, 0, sizeof *t);
    lines_init(&t->in);
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "tape: cannot open %s\n", path); return 0; }
    lines_file(&t->in, f);

    const char *h = lines_next(&t->in);
    if (!h) { fprintf(stderr, "tape: %s has no header\n", path); fclose(f); return 0; }

    t->header = xstrndup(h, strlen(h));
    t->hdr = json_parse(xstrndup(h, strlen(h)));
    if (!t->hdr || t->hdr->kind != J_OBJ)
    {
        fprintf(stderr, "tape: %s has an unreadable header\n", path);
        fclose(f);
        return 0;
    }

    t->next_t = 0;
    return 1;
}

int tape_next(struct tape *t, const struct jval **row)
{
    for (;;)
    {
        const char *line = lines_next(&t->in);
        if (!line) return 0;
        if (!line[0]) continue;

        /* a row lives until the next one: no caller keeps one past it, and a
         * long tape held whole was most of a replay's memory; its values
         * are the tape's arena's, rewound here */
        t->row = NULL;
        if (t->arena == NULL)
        {
            t->arena = calloc(1, sizeof *t->arena);
            if (t->arena == NULL) { fprintf(stderr, "tape: out of memory\n"); exit(2); }
            t->arena->cap = 1 << 16;
            t->arena->base = xmalloc(t->arena->cap);
        }
        arena_rewind(t->arena);
        size_t len = strlen(line);
        char *copy = arena_alloc(t->arena, len + 1);
        memcpy(copy, line, len + 1);
        struct reader rd = {copy, 0, t->arena};
        struct jval *v = parse_value(&rd);
        if (!v || v->kind != J_OBJ)
        {
            fprintf(stderr, "tape: row %lld is not a JSON object\n", (long long)t->next_t);
            t->failed = 1;
            return -1;
        }

        t->row = v;
        ++t->next_t;
        *row = v;
        return 1;
    }
}

void tape_close(struct tape *t)
{
    if (t->in.f) fclose((FILE *)t->in.f);
    lines_free(&t->in);
    free(t->header);
    json_free(t->hdr);
    arena_free(t->arena);
    memset(t, 0, sizeof *t);
}