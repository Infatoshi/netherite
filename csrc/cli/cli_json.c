/* nether CLI: the JSON reader (cli_json.h). */
#define _GNU_SOURCE
#include "cli_json.h"

#include <stdlib.h>
#include <string.h>

struct jp {
    const char *p;
    char *err;
    size_t en;
    int depth;
};

static void ws(struct jp *j)
{
    while (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r') ++j->p;
}

static struct jv *fail(struct jp *j, const char *why)
{
    if (j->err && !j->err[0]) snprintf(j->err, j->en, "JSON: %s at \"%.20s\"", why, j->p);
    return NULL;
}

static void put_utf8(struct sb *b, unsigned c)
{
    if (c < 0x80) sb_putc(b, (char)c);
    else if (c < 0x800) sb_putc(b, (char)(0xc0 | c >> 6)), sb_putc(b, (char)(0x80 | (c & 63)));
    else if (c < 0x10000)
        sb_putc(b, (char)(0xe0 | c >> 12)), sb_putc(b, (char)(0x80 | (c >> 6 & 63))), sb_putc(b, (char)(0x80 | (c & 63)));
    else
        sb_putc(b, (char)(0xf0 | c >> 18)), sb_putc(b, (char)(0x80 | (c >> 12 & 63))), sb_putc(b, (char)(0x80 | (c >> 6 & 63))),
            sb_putc(b, (char)(0x80 | (c & 63)));
}

static char *jstring(struct jp *j)
{
    if (*j->p != '"') return (fail(j, "a string expected"), NULL);
    ++j->p;
    struct sb b = {0};
    for (;;) {
        char c = *j->p++;
        if (!c) {
            sb_free(&b);
            return (fail(j, "an unterminated string"), NULL);
        }
        if (c == '"') break;
        if (c != '\\') {
            sb_putc(&b, c);
            continue;
        }
        c = *j->p++;
        switch (c) {
        case 'n': sb_putc(&b, '\n'); break;
        case 't': sb_putc(&b, '\t'); break;
        case 'r': sb_putc(&b, '\r'); break;
        case 'b': sb_putc(&b, '\b'); break;
        case 'f': sb_putc(&b, '\f'); break;
        case 'u': {
            unsigned u = 0;
            for (int i = 0; i < 4; ++i) {
                char h = *j->p++;
                u = u * 16 + (unsigned)(h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : 0);
            }
            put_utf8(&b, u);
            break;
        }
        default: sb_putc(&b, c);
        }
    }
    sb_str(&b);
    return b.s;
}

static struct jv *value(struct jp *j);

static void push(struct jv *v, char *key, struct jv *k)
{
    if (!(v->n & (v->n - 1))) {
        int c = v->n ? v->n * 2 : 4;
        v->kids = xrealloc(v->kids, sizeof *v->kids * (size_t)c);
        if (v->t == JV_OBJ) v->keys = xrealloc(v->keys, sizeof *v->keys * (size_t)c);
    }
    if (v->t == JV_OBJ) v->keys[v->n] = key;
    v->kids[v->n++] = k;
}

static struct jv *value(struct jp *j)
{
    ws(j);
    if (++j->depth > 64) return fail(j, "nested too deep");
    struct jv *v = xcalloc(1, sizeof *v);
    char c = *j->p;
    if (c == '{' || c == '[') {
        v->t = c == '{' ? JV_OBJ : JV_ARR;
        ++j->p;
        ws(j);
        char close = c == '{' ? '}' : ']';
        if (*j->p == close) ++j->p;
        else
            for (;;) {
                char *key = NULL;
                if (v->t == JV_OBJ) {
                    ws(j);
                    if (!(key = jstring(j))) goto bad;
                    ws(j);
                    if (*j->p++ != ':') {
                        free(key);
                        --j->p;
                        fail(j, "':' expected");
                        goto bad;
                    }
                }
                struct jv *k = value(j);
                if (!k) {
                    free(key);
                    goto bad;
                }
                push(v, key, k);
                ws(j);
                if (*j->p == ',') {
                    ++j->p;
                    continue;
                }
                if (*j->p == close) {
                    ++j->p;
                    break;
                }
                fail(j, "',' or a close expected");
                goto bad;
            }
    } else if (c == '"') {
        v->t = JV_STR;
        if (!(v->str = jstring(j))) goto bad;
    } else if (!strncmp(j->p, "true", 4)) v->t = JV_BOOL, v->num = 1, j->p += 4;
    else if (!strncmp(j->p, "false", 5)) v->t = JV_BOOL, j->p += 5;
    else if (!strncmp(j->p, "null", 4)) v->t = JV_NULL, j->p += 4;
    else if (!strncmp(j->p, "NaN", 3)) v->t = JV_NUM, v->num = 0.0 / 0.0, j->p += 3;
    else {
        char *e;
        v->t = JV_NUM;
        v->num = strtod(j->p, &e);
        if (e == j->p) {
            fail(j, "a value expected");
            goto bad;
        }
        j->p = e;
    }
    --j->depth;
    return v;
bad:
    jv_free(v);
    return NULL;
}

struct jv *jv_parse(const char *s, char *err, size_t en)
{
    struct jp j = {s, err, en, 0};
    if (err && en) err[0] = 0;
    struct jv *v = value(&j);
    if (!v) return NULL;
    ws(&j);
    if (*j.p) {
        fail(&j, "text after the value");
        jv_free(v);
        return NULL;
    }
    return v;
}

void jv_free(struct jv *v)
{
    if (!v) return;
    for (int i = 0; i < v->n; ++i) {
        jv_free(v->kids[i]);
        if (v->t == JV_OBJ) free(v->keys[i]);
    }
    free(v->kids);
    free(v->keys);
    free(v->str);
    free(v);
}

const struct jv *jv_get(const struct jv *v, const char *key)
{
    if (!v || v->t != JV_OBJ) return NULL;
    for (int i = 0; i < v->n; ++i)
        if (!strcmp(v->keys[i], key)) return v->kids[i];
    return NULL;
}

double jv_num(const struct jv *v, const char *key, double dflt)
{
    const struct jv *k = jv_get(v, key);
    return k && (k->t == JV_NUM || k->t == JV_BOOL) ? k->num : dflt;
}

const char *jv_str(const struct jv *v, const char *key, const char *dflt)
{
    const struct jv *k = jv_get(v, key);
    return k && k->t == JV_STR ? k->str : dflt;
}
