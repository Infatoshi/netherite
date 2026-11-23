/* nether CLI: the shared mechanics (cli.h). */
#define _GNU_SOURCE
#include "cli.h"
#include "cli_proc.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

char cli_root[PATH_MAX];

static void oom(void)
{
    fprintf(stderr, "nether: out of memory\n");
    exit(2);
}

void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) oom();
    return p;
}

void *xcalloc(size_t n, size_t k)
{
    void *p = calloc(n ? n : 1, k ? k : 1);
    if (!p) oom();
    return p;
}

void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n ? n : 1);
    if (!p) oom();
    return p;
}

char *xstrdup(const char *s) { return xstrndup(s, strlen(s)); }

char *xstrndup(const char *s, size_t n)
{
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

char *xasprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    char *p = xmalloc((size_t)n + 1);
    va_start(ap, fmt);
    vsnprintf(p, (size_t)n + 1, fmt, ap);
    va_end(ap);
    return p;
}

void cli_fmt(char *buf, size_t n, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    if (r < 0 || (size_t)r >= n) {
        fprintf(stderr, "nether: a path or name is too long (%d bytes for %zu): %.80s...\n", r, n, buf);
        exit(2);
    }
}

/* ---- strings */

static void sb_grow(struct sb *b, size_t add)
{
    if (b->n + add + 1 <= b->cap) return;
    size_t c = b->cap ? b->cap : 256;
    while (c < b->n + add + 1) c *= 2;
    b->s = xrealloc(b->s, c);
    b->cap = c;
}

void sb_putn(struct sb *b, const char *s, size_t n)
{
    sb_grow(b, n);
    memcpy(b->s + b->n, s, n);
    b->n += n;
    b->s[b->n] = 0;
}

void sb_puts(struct sb *b, const char *s) { sb_putn(b, s, strlen(s)); }
void sb_putc(struct sb *b, char c) { sb_putn(b, &c, 1); }

void sb_printf(struct sb *b, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    sb_grow(b, (size_t)n);
    va_start(ap, fmt);
    vsnprintf(b->s + b->n, (size_t)n + 1, fmt, ap);
    va_end(ap);
    b->n += (size_t)n;
}

const char *sb_str(struct sb *b)
{
    if (!b->s) sb_grow(b, 0), b->s[0] = 0;
    return b->s;
}

void sb_free(struct sb *b)
{
    free(b->s);
    *b = (struct sb){0};
}

void sv_push(struct sv *l, const char *s)
{
    if (l->n + 2 > l->cap) {
        l->cap = l->cap ? l->cap * 2 : 16;
        l->v = xrealloc(l->v, sizeof *l->v * (size_t)l->cap);
    }
    l->v[l->n++] = xstrdup(s);
    l->v[l->n] = NULL;
}

void sv_pushf(struct sv *l, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    char *p = xmalloc((size_t)n + 1);
    va_start(ap, fmt);
    vsnprintf(p, (size_t)n + 1, fmt, ap);
    va_end(ap);
    sv_push(l, p);
    free(p);
}

void sv_free(struct sv *l)
{
    for (int i = 0; i < l->n; ++i) free(l->v[i]);
    free(l->v);
    *l = (struct sv){0};
}

/* ---- files */

char *cli_read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    struct sb b = {0};
    char buf[65536];
    size_t r;
    while ((r = fread(buf, 1, sizeof buf, f)) > 0) sb_putn(&b, buf, r);
    int bad = ferror(f);
    fclose(f);
    if (bad) {
        sb_free(&b);
        return NULL;
    }
    sb_str(&b);
    if (len) *len = b.n;
    return b.s;
}

int cli_write_file_atomic(const char *path, const char *data, size_t len)
{
    char *tmp = xasprintf("%s.tmp.%ld", path, (long)getpid());
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        free(tmp);
        return -1;
    }
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(fd, data + off, len - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            close(fd);
            unlink(tmp);
            free(tmp);
            return -1;
        }
        off += (size_t)w;
    }
    if (fsync(fd) || close(fd) || rename(tmp, path)) {
        unlink(tmp);
        free(tmp);
        return -1;
    }
    free(tmp);
    return 0;
}

int cli_append_locked(const char *path, const char *header, const char *row)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return -1;
    if (flock(fd, LOCK_EX)) {
        close(fd);
        return -1;
    }
    struct stat st;
    int rc = 0;
    if (header && !fstat(fd, &st) && st.st_size == 0 && write(fd, header, strlen(header)) < 0) rc = -1;
    if (!rc && write(fd, row, strlen(row)) < 0) rc = -1;
    if (fsync(fd)) rc = -1;
    flock(fd, LOCK_UN);
    close(fd);
    return rc;
}

int cli_mkdirs(const char *path)
{
    char p[PATH_MAX];
    if (snprintf(p, sizeof p, "%s", path) >= (int)sizeof p) return -1;
    for (char *s = p + 1; *s; ++s) {
        if (*s != '/') continue;
        *s = 0;
        if (mkdir(p, 0755) && errno != EEXIST) return -1;
        *s = '/';
    }
    if (mkdir(p, 0755) && errno != EEXIST) return -1;
    return 0;
}

int cli_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

int cli_is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

const char *cli_abs(const char *p, char *buf)
{
    if (p[0] == '/') {
        snprintf(buf, PATH_MAX, "%s", p);
        return buf;
    }
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof cwd) || snprintf(buf, PATH_MAX, "%s/%s", cwd, p) >= PATH_MAX) snprintf(buf, PATH_MAX, "%s", p);
    return buf;
}

const char *cli_resolve(const char *p, char *buf)
{
    cli_abs(p, buf);
    if (p[0] == '/' || cli_exists(buf) || !cli_root[0]) return buf;
    char b2[PATH_MAX];
    if (snprintf(b2, sizeof b2, "%s/%s", cli_root, p) < (int)sizeof b2 && cli_exists(b2)) snprintf(buf, PATH_MAX, "%s", b2);
    return buf;
}

/* ---- time */

double cli_now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + t.tv_nsec * 1e-9;
}

int cli_prof_on, cli_prof_nest;
double cli_prof_s[CLI_P_N];
const char *const cli_prof_name[CLI_P_N] = {"startup", "yaml_parse", "json_parse", "lock_guard", "spawn_exec", "stats"};

void cli_prof_add(int p, double t0)
{
    if (cli_prof_nest) return;
    cli_prof_s[p] += cli_now() - t0;
}

double cli_wall(void)
{
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    return (double)t.tv_sec + t.tv_nsec * 1e-9;
}

void cli_stamp(char *buf, size_t n, const char *fmt)
{
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, n, fmt, &tm);
}

/* ---- words */

int cli_split_words(const char *s, struct sv *out, char *err, size_t en)
{
    struct sb w = {0};
    int in_word = 0;
    for (const char *p = s;; ++p) {
        char c = *p;
        if (!c || c == ' ' || c == '\t' || c == '\n') {
            if (in_word) sv_push(out, sb_str(&w));
            w.n = 0;
            if (w.s) w.s[0] = 0;
            in_word = 0;
            if (!c) break;
            continue;
        }
        in_word = 1;
        if (c == '\'') {
            const char *e = strchr(p + 1, '\'');
            if (!e) {
                snprintf(err, en, "an unclosed ' in: %s", s);
                sb_free(&w);
                return -1;
            }
            sb_putn(&w, p + 1, (size_t)(e - p - 1));
            sb_str(&w);
            p = e;
        } else if (c == '"') {
            for (++p; *p && *p != '"'; ++p) {
                if (*p == '\\' && (p[1] == '"' || p[1] == '\\')) ++p;
                else if (*p == '$' || *p == '`') {
                    snprintf(err, en, "a $ or ` inside \"...\" (no expansions here; use '...'): %s", s);
                    sb_free(&w);
                    return -1;
                }
                sb_putc(&w, *p);
            }
            if (!*p) {
                snprintf(err, en, "an unclosed \" in: %s", s);
                sb_free(&w);
                return -1;
            }
            sb_str(&w);
        } else if (c == '\\' && p[1]) {
            sb_putc(&w, *++p);
        } else if (strchr("|;&<>`$()*?[]", c)) {
            /* {} and ~ stay literal: templates are substituted before the split, and nothing expands ~ */
            snprintf(err, en, "'%c' would need a shell (no pipes, redirections, expansions or globs; quote it): %s", c, s);
            sb_free(&w);
            return -1;
        } else
            sb_putc(&w, c);
    }
    sb_free(&w);
    return 0;
}

char *cli_subst(const char *tmpl, const char *const *keys, const char *const *vals, int n)
{
    struct sb b = {0};
    for (const char *p = tmpl; *p; ++p) {
        if (*p == '{') {
            const char *e = strchr(p, '}');
            int hit = 0;
            if (e)
                for (int i = 0; i < n; ++i) {
                    size_t kl = strlen(keys[i]);
                    if ((size_t)(e - p - 1) == kl && !strncmp(p + 1, keys[i], kl)) {
                        sb_puts(&b, vals[i] ? vals[i] : "");
                        p = e;
                        hit = 1;
                        break;
                    }
                }
            if (hit) continue;
        }
        sb_putc(&b, *p);
    }
    sb_str(&b);
    return b.s;
}

/* ---- JSON output */

void cli_json_str(struct sb *b, const char *s)
{
    sb_putc(b, '"');
    for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; ++p) {
        switch (*p) {
        case '"': sb_puts(b, "\\\""); break;
        case '\\': sb_puts(b, "\\\\"); break;
        case '\n': sb_puts(b, "\\n"); break;
        case '\r': sb_puts(b, "\\r"); break;
        case '\t': sb_puts(b, "\\t"); break;
        default:
            if (*p < 0x20) sb_printf(b, "\\u%04x", *p);
            else sb_putc(b, (char)*p);
        }
    }
    sb_putc(b, '"');
}

void cli_json_num(struct sb *b, double x)
{
    if (x != x || x > 1.7976931348623157e308 || x < -1.7976931348623157e308) sb_puts(b, "null");
    else if (x == (double)(long long)x && x < 9e15 && x > -9e15) sb_printf(b, "%lld", (long long)x);
    else sb_printf(b, "%.17g", x);
}

/* ---- SHA-256 */

static const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
    0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
    0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

struct sha256 {
    uint32_t h[8];
    uint8_t buf[64];
    uint64_t len;
    size_t fill;
};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha_block(struct sha256 *c, const uint8_t *p)
{
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3], e = c->h[4], f = c->h[5], g = c->h[6], h = c->h[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i];
        uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & cc) ^ (b & cc));
        h = g, g = f, f = e, e = d + t1, d = cc, cc = b, b = a, a = t1 + t2;
    }
    c->h[0] += a, c->h[1] += b, c->h[2] += cc, c->h[3] += d, c->h[4] += e, c->h[5] += f, c->h[6] += g, c->h[7] += h;
}

static void sha_init(struct sha256 *c)
{
    static const uint32_t H0[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(c->h, H0, sizeof H0);
    c->len = 0;
    c->fill = 0;
}

static void sha_update(struct sha256 *c, const void *data, size_t n)
{
    const uint8_t *p = data;
    c->len += n;
    while (n) {
        size_t k = 64 - c->fill < n ? 64 - c->fill : n;
        memcpy(c->buf + c->fill, p, k);
        c->fill += k, p += k, n -= k;
        if (c->fill == 64) sha_block(c, c->buf), c->fill = 0;
    }
}

static void sha_final(struct sha256 *c, uint8_t out[32])
{
    uint64_t bits = c->len * 8;
    uint8_t pad = 0x80, z = 0;
    sha_update(c, &pad, 1);
    while (c->fill != 56) sha_update(c, &z, 1);
    uint8_t l[8];
    for (int i = 0; i < 8; ++i) l[i] = (uint8_t)(bits >> (56 - 8 * i));
    sha_update(c, l, 8);
    for (int i = 0; i < 8; ++i)
        out[4 * i] = (uint8_t)(c->h[i] >> 24), out[4 * i + 1] = (uint8_t)(c->h[i] >> 16), out[4 * i + 2] = (uint8_t)(c->h[i] >> 8),
                out[4 * i + 3] = (uint8_t)c->h[i];
}

void cli_sha256(const void *data, size_t n, uint8_t out[32])
{
    struct sha256 c;
    sha_init(&c);
    sha_update(&c, data, n);
    sha_final(&c, out);
}

void cli_hex(const uint8_t *b, size_t n, char *out)
{
    static const char H[] = "0123456789abcdef";
    for (size_t i = 0; i < n; ++i) out[2 * i] = H[b[i] >> 4], out[2 * i + 1] = H[b[i] & 15];
    out[2 * n] = 0;
}

int cli_sha256_file(const char *path, char hex[65])
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    struct sha256 c;
    sha_init(&c);
    uint8_t buf[1 << 16];
    size_t r;
    while ((r = fread(buf, 1, sizeof buf, f)) > 0) sha_update(&c, buf, r);
    int bad = ferror(f);
    fclose(f);
    if (bad) return -1;
    uint8_t d[32];
    sha_final(&c, d);
    cli_hex(d, 32, hex);
    return 0;
}

/* ---- git */

void cli_git_sha(char *buf, size_t n)
{
    snprintf(buf, n, "nogit");
    struct sb o = {0};
    const char *av[] = {"git", "rev-parse", "--short=12", "HEAD", NULL};
    struct cli_spawn_opts so = {.argv = av, .cwd = cli_root[0] ? cli_root : NULL, .capture = &o, .quiet = 1, .null_stderr = 1};
    struct cli_spawn_res r;
    if (cli_spawn(&so, &r) == 0 && r.rc == 0 && o.n) {
        const char *s = sb_str(&o);
        snprintf(buf, n, "%.*s", (int)strcspn(s, "\n"), s);
        struct sb d = {0};
        const char *dv[] = {"git", "status", "--porcelain", "--untracked-files=no", NULL};
        struct cli_spawn_opts ds = {.argv = dv, .cwd = so.cwd, .capture = &d, .quiet = 1, .null_stderr = 1};
        if (cli_spawn(&ds, &r) == 0 && r.rc == 0 && d.n) {
            size_t l = strlen(buf);
            snprintf(buf + l, n - l, "+dirty");
        }
        sb_free(&d);
    }
    sb_free(&o);
}

/* ---- arguments */

const char *cli_arg(int argc, char **argv, int *i, const char *verb)
{
    if (*i + 1 >= argc) {
        fprintf(stderr, "nether %s: %s takes a value (nether %s --help)\n", verb, argv[*i], verb);
        exit(2);
    }
    return argv[++*i];
}

int cli_num(const char *s, double *out)
{
    char *e;
    errno = 0;
    double x = strtod(s, &e);
    if (!*s || *e || errno) return -1;
    *out = x;
    return 0;
}

int cli_long(const char *s, long *out)
{
    char *e;
    errno = 0;
    long x = strtol(s, &e, 10);
    if (!*s || *e || errno) return -1;
    *out = x;
    return 0;
}
