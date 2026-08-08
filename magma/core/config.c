/* core/config.c - the registry implementation (see core/config.h).
 *
 * Everything here is driven by including core/config.def with a different set of
 * CFG_* definitions each time: the struct fields live in config.h, and this file
 * generates the defaults, the parse dispatch, the dump table and the
 * changed-keys breadcrumb from the same 18 lines. There is no runtime table to
 * keep in sync and no way to add a key to one of those five places and forget
 * the other four.
 *
 * Zero heap: one file-static CrConfig, fixed-size string storage, and a
 * stack-local CrConfig for the "is this the default?" comparison.
 */
#include "core/config.h"

/* The pool-cap defaults (CR_DEF_*) and the measured values they derive from are
 * documented in game/caps.h; config.def references them so the numbers keep
 * exactly one home. This is a header-only dependency - no link edge. */
#include "game/caps.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static CrConfig g_cfg;
static int      g_loaded;
static unsigned g_gen;

/* ---- value helpers ---------------------------------------------------- */

static void cfg_str_copy(char *dst, const char *src) {
    size_t n = strlen(src);
    if (n >= CR_CFG_STR_MAX) n = CR_CFG_STR_MAX - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* Strict: the whole token must parse. "1x", "", " " and overflow are rejected so
 * a fat-fingered value fails loudly instead of silently reading as 1. */
static int cfg_p_ll(const char *v, long long *out) {
    char *end = NULL;
    errno = 0;
    long long x = strtoll(v, &end, 10);
    if (errno || !end || end == v || *end) return 0;
    *out = x;
    return 1;
}

static int cfg_p_int(const char *v, int *out) {
    long long x;
    if (!cfg_p_ll(v, &x)) return 0;
    if (x < -2147483647LL - 1 || x > 2147483647LL) return 0;
    *out = (int)x;
    return 1;
}

/* Bools accept 0/1 ONLY. "true"/"yes"/"on" would be a second spelling of the
 * same state, and presence-based env flags (set to anything == on) are exactly
 * the ambiguity this registry exists to remove. */
static int cfg_p_bool(const char *v, int *out) {
    if (!strcmp(v, "0")) { *out = 0; return 1; }
    if (!strcmp(v, "1")) { *out = 1; return 1; }
    return 0;
}

static int cfg_p_f32(const char *v, float *out) {
    char *end = NULL;
    errno = 0;
    float x = strtof(v, &end);
    if (errno || !end || end == v || *end) return 0;
    *out = x;
    return 1;
}

/* ---- defaults --------------------------------------------------------- */

static void cfg_defaults(CrConfig *c) {
#define CFG_BOOL(name, def, doc) c->name = (def);
#define CFG_INT(name, def, doc)  c->name = (def);
#define CFG_I64(name, def, doc)  c->name = (def);
#define CFG_F32(name, def, doc)  c->name = (def);
#define CFG_STR(name, def, doc)  cfg_str_copy(c->name, (def));
#include "core/config.def"
#undef CFG_BOOL
#undef CFG_INT
#undef CFG_I64
#undef CFG_F32
#undef CFG_STR
}

/* ---- set -------------------------------------------------------------- */

int cr_cfg_set(const char *key, const char *val) {
    if (!key || !val) return -1;
    if (!g_loaded) cr_cfg_load(NULL);

#define CFG_BOOL(name, def, doc)                                              \
    if (!strcmp(key, #name)) {                                                \
        int t; if (!cfg_p_bool(val, &t)) return -2;                           \
        g_cfg.name = t; g_gen++; return 0;                                    \
    }
#define CFG_INT(name, def, doc)                                               \
    if (!strcmp(key, #name)) {                                                \
        int t; if (!cfg_p_int(val, &t)) return -2;                            \
        g_cfg.name = t; g_gen++; return 0;                                    \
    }
#define CFG_I64(name, def, doc)                                               \
    if (!strcmp(key, #name)) {                                                \
        long long t; if (!cfg_p_ll(val, &t)) return -2;                       \
        g_cfg.name = t; g_gen++; return 0;                                    \
    }
#define CFG_F32(name, def, doc)                                               \
    if (!strcmp(key, #name)) {                                                \
        float t; if (!cfg_p_f32(val, &t)) return -2;                          \
        g_cfg.name = t; g_gen++; return 0;                                    \
    }
#define CFG_STR(name, def, doc)                                               \
    if (!strcmp(key, #name)) {                                                \
        cfg_str_copy(g_cfg.name, val); g_gen++; return 0;                     \
    }
#include "core/config.def"
#undef CFG_BOOL
#undef CFG_INT
#undef CFG_I64
#undef CFG_F32
#undef CFG_STR

    return -1;
}

/* ---- load ------------------------------------------------------------- */

static void cfg_die(const char *path, int line, const char *fmt, const char *a,
                    const char *b) {
    fprintf(stderr, "config: %s:%d: ", path, line);
    fprintf(stderr, fmt, a, b);
    fputc('\n', stderr);
    fprintf(stderr, "config: run with --dump-config for the full key list\n");
    exit(2);
}

void cr_cfg_load(const char *path) {
    cfg_defaults(&g_cfg);
    g_loaded = 1;   /* set BEFORE parsing: cr_cfg_set must not re-enter here */
    g_gen++;

    const char *p = path ? path : "magma.conf";
    FILE *f = fopen(p, "r");
    if (!f) return;   /* no conf file is the normal case: pure defaults */

    /* Same line discipline game/caps.c has always used: '#' starts a comment,
     * '=' is normalized to whitespace, so "key = value", "key=value" and
     * "key value" are all the same line. A value therefore may not contain '='
     * or whitespace; nothing in the registry needs to. */
    char line[64 + CR_CFG_STR_MAX + 32];
    int lineno = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        for (char *q = line; *q; ++q) if (*q == '=') *q = ' ';

        char key[64], val[CR_CFG_STR_MAX];
        /* Width must match CR_CFG_STR_MAX-1 (currently 1023). */
        int got = sscanf(line, "%63s %1023s", key, val);
        if (got <= 0) continue;                    /* blank / comment-only */
        if (got == 1) cfg_die(p, lineno, "key '%s' has no value%s", key, "");

        int rc = cr_cfg_set(key, val);
        if (rc == -1) cfg_die(p, lineno, "unknown key '%s'%s", key, "");
        if (rc == -2) cfg_die(p, lineno, "bad value for '%s': '%s'", key, val);
    }
    fclose(f);
}

const CrConfig *cr_cfg(void) {
    if (!g_loaded) cr_cfg_load(NULL);
    return &g_cfg;
}

unsigned cr_cfg_generation(void) {
    if (!g_loaded) cr_cfg_load(NULL);
    return g_gen;
}

/* ---- dump ------------------------------------------------------------- */

static void cfg_row(FILE *out, int changed, const char *key, const char *val,
                    const char *type, const char *doc) {
    fprintf(out, "%c %-21s = %-12s  [%s] %s\n", changed ? '*' : ' ', key, val,
            type, doc);
}

void cr_cfg_dump(FILE *out) {
    const CrConfig *c = cr_cfg();
    CrConfig d;
    cfg_defaults(&d);

    fprintf(out, "# magma config registry (core/config.def).\n");
    fprintf(out, "# Set with: --conf FILE  (\"key = value\" lines, '#' comments)"
                 "  and/or  --set key=value\n");
    fprintf(out, "# '*' marks a key whose effective value is not the compiled default.\n");

#define CFG_BOOL(name, def, doc) \
    { char v[32]; snprintf(v, sizeof v, "%d", c->name); \
      cfg_row(out, c->name != d.name, #name, v, "bool", doc); }
#define CFG_INT(name, def, doc) \
    { char v[32]; snprintf(v, sizeof v, "%d", c->name); \
      cfg_row(out, c->name != d.name, #name, v, "int", doc); }
#define CFG_I64(name, def, doc) \
    { char v[32]; snprintf(v, sizeof v, "%lld", c->name); \
      cfg_row(out, c->name != d.name, #name, v, "i64", doc); }
#define CFG_F32(name, def, doc) \
    { char v[32]; snprintf(v, sizeof v, "%.9g", (double)c->name); \
      cfg_row(out, c->name != d.name, #name, v, "f32", doc); }
#define CFG_STR(name, def, doc) \
    cfg_row(out, strcmp(c->name, d.name) != 0, #name, \
            c->name[0] ? c->name : "\"\"", "str", doc);
#include "core/config.def"
#undef CFG_BOOL
#undef CFG_INT
#undef CFG_I64
#undef CFG_F32
#undef CFG_STR
}

/* ---- breadcrumb ------------------------------------------------------- */

static void cfg_append(char *buf, size_t cap, size_t *n, int *any,
                       const char *key, const char *val) {
    if (*n >= cap) return;
    int w = snprintf(buf + *n, cap - *n, "%s%s=%s", *any ? " " : "", key, val);
    if (w < 0 || (size_t)w >= cap - *n) { *n = cap; return; }  /* truncated */
    *n += (size_t)w;
    *any = 1;
}

void cr_cfg_log_overrides(FILE *out) {
    const CrConfig *c = cr_cfg();
    CrConfig d;
    cfg_defaults(&d);

    char buf[1024];
    size_t n = 0;
    int any = 0;
    buf[0] = '\0';

#define CFG_BOOL(name, def, doc) \
    if (c->name != d.name) { char v[32]; snprintf(v, sizeof v, "%d", c->name); \
        cfg_append(buf, sizeof buf, &n, &any, #name, v); }
#define CFG_INT(name, def, doc) \
    if (c->name != d.name) { char v[32]; snprintf(v, sizeof v, "%d", c->name); \
        cfg_append(buf, sizeof buf, &n, &any, #name, v); }
#define CFG_I64(name, def, doc) \
    if (c->name != d.name) { char v[32]; snprintf(v, sizeof v, "%lld", c->name); \
        cfg_append(buf, sizeof buf, &n, &any, #name, v); }
#define CFG_F32(name, def, doc) \
    if (c->name != d.name) { char v[32]; snprintf(v, sizeof v, "%.9g", (double)c->name); \
        cfg_append(buf, sizeof buf, &n, &any, #name, v); }
#define CFG_STR(name, def, doc) \
    if (strcmp(c->name, d.name) != 0) \
        cfg_append(buf, sizeof buf, &n, &any, #name, c->name);
#include "core/config.def"
#undef CFG_BOOL
#undef CFG_INT
#undef CFG_I64
#undef CFG_F32
#undef CFG_STR

    if (any) fprintf(out, "config: %s\n", buf);
}
