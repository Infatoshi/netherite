/* What the repository's tools share (the C ports of the Python tools,
 * lane/cport): growing strings and arrays, an insertion-ordered string map,
 * Python's float repr and round, JSON string escaping as json.dumps writes
 * it, Python's random module (MT19937, int and str seeds) for the fuzz
 * generators, SHA-1 and SHA-512, files, directories and subprocesses.
 * Header only; a tool includes it after its own feature macros. */
#ifndef NETHERITE_TOOL_H
#define NETHERITE_TOOL_H

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define TOOL static inline

TOOL void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

TOOL void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) die("out of memory");
    return p;
}

TOOL void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n ? n : 1);
    if (!p) die("out of memory");
    return p;
}

TOOL void *xcalloc(size_t n, size_t k)
{
    void *p = calloc(n ? n : 1, k ? k : 1);
    if (!p) die("out of memory");
    return p;
}

TOOL char *xstrndup(const char *s, size_t n)
{
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

TOOL char *xstrdup(const char *s) { return xstrndup(s, strlen(s)); }

/* ------------------------------------------------------------ strings */

struct sb {
    char *s;
    size_t n, cap;
};

TOOL void sb_grow(struct sb *b, size_t more)
{
    if (b->n + more + 1 <= b->cap) return;
    size_t c = b->cap ? b->cap : 64;
    while (c < b->n + more + 1) c *= 2;
    b->s = xrealloc(b->s, c);
    b->cap = c;
}

TOOL void sb_putn(struct sb *b, const char *s, size_t n)
{
    sb_grow(b, n);
    memcpy(b->s + b->n, s, n);
    b->n += n;
    b->s[b->n] = 0;
}

TOOL void sb_puts(struct sb *b, const char *s) { sb_putn(b, s, strlen(s)); }

TOOL void sb_putc(struct sb *b, char c) { sb_putn(b, &c, 1); }

TOOL void sb_vprintf(struct sb *b, const char *fmt, va_list ap)
{
    va_list aq;
    va_copy(aq, ap);
    int n = vsnprintf(NULL, 0, fmt, aq);
    va_end(aq);
    if (n < 0) return;
    sb_grow(b, (size_t)n);
    vsnprintf(b->s + b->n, (size_t)n + 1, fmt, ap);
    b->n += (size_t)n;
}

TOOL void sb_printf(struct sb *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
TOOL void sb_printf(struct sb *b, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    sb_vprintf(b, fmt, ap);
    va_end(ap);
}

TOOL const char *sb_str(struct sb *b)
{
    if (!b->s) sb_grow(b, 0), b->s[0] = 0;
    return b->s;
}

TOOL void sb_free(struct sb *b)
{
    free(b->s);
    b->s = NULL;
    b->n = b->cap = 0;
}

TOOL char *xasprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
TOOL char *xasprintf(const char *fmt, ...)
{
    struct sb b = {0};
    va_list ap;
    va_start(ap, fmt);
    sb_vprintf(&b, fmt, ap);
    va_end(ap);
    return (char *)sb_str(&b);
}

TOOL int starts_with(const char *s, const char *p) { return strncmp(s, p, strlen(p)) == 0; }

TOOL int ends_with(const char *s, const char *p)
{
    size_t a = strlen(s), b = strlen(p);
    return a >= b && memcmp(s + a - b, p, b) == 0;
}

TOOL int is_space(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

/* s without leading and trailing whitespace, in place */
TOOL char *strip(char *s)
{
    while (is_space((unsigned char)*s)) ++s;
    size_t n = strlen(s);
    while (n && is_space((unsigned char)s[n - 1])) s[--n] = 0;
    return s;
}

/* ------------------------------------------------------------ string arrays */

struct sv {
    char **v;
    int n, cap;
};

TOOL void sv_push(struct sv *a, char *s)
{
    if (a->n == a->cap) {
        a->cap = a->cap ? a->cap * 2 : 16;
        a->v = xrealloc(a->v, (size_t)a->cap * sizeof *a->v);
    }
    a->v[a->n++] = s;
}

TOOL int cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

TOOL void sv_sort(struct sv *a)
{
    if (a->n > 1) qsort(a->v, (size_t)a->n, sizeof *a->v, cmp_str);
}

/* sorted, each string once */
TOOL void sv_sort_uniq(struct sv *a)
{
    sv_sort(a);
    int k = 0;
    for (int i = 0; i < a->n; ++i)
        if (!k || strcmp(a->v[k - 1], a->v[i])) a->v[k++] = a->v[i];
    a->n = k;
}

/* Python's str.split(): runs of whitespace, no empty fields; the fields
 * point into s, which is modified */
TOOL int split_ws(char *s, char **f, int max)
{
    int n = 0;
    while (*s) {
        while (is_space((unsigned char)*s)) ++s;
        if (!*s) break;
        if (n == max) break;
        f[n++] = s;
        while (*s && !is_space((unsigned char)*s)) ++s;
        if (*s) *s++ = 0;
    }
    return n;
}

/* ------------------------------------------------------------ an ordered map */

/* keys in insertion order (Python's dict), a value each; open addressing
 * over indices */
struct smap {
    char **keys;
    void **vals;
    int n, cap;
    int *slot;
    int nslot;
};

TOOL uint64_t str_hash(const char *s)
{
    uint64_t h = 1469598103934665603ULL;
    while (*s) h = (h ^ (unsigned char)*s++) * 1099511628211ULL;
    return h;
}

TOOL int smap_find(const struct smap *m, const char *k)
{
    if (!m->nslot) return -1;
    for (uint64_t i = str_hash(k) & (uint64_t)(m->nslot - 1);; i = (i + 1) & (uint64_t)(m->nslot - 1)) {
        int x = m->slot[i];
        if (x < 0) return -1;
        if (!strcmp(m->keys[x], k)) return x;
    }
}

TOOL void smap_rehash(struct smap *m, int nslot)
{
    free(m->slot);
    m->slot = xmalloc((size_t)nslot * sizeof *m->slot);
    m->nslot = nslot;
    for (int i = 0; i < nslot; ++i) m->slot[i] = -1;
    for (int x = 0; x < m->n; ++x) {
        uint64_t i = str_hash(m->keys[x]) & (uint64_t)(nslot - 1);
        while (m->slot[i] >= 0) i = (i + 1) & (uint64_t)(nslot - 1);
        m->slot[i] = x;
    }
}

/* the key's index, added (with value NULL, the key copied) if new */
TOOL int smap_add(struct smap *m, const char *k)
{
    int x = smap_find(m, k);
    if (x >= 0) return x;
    if (m->n == m->cap) {
        m->cap = m->cap ? m->cap * 2 : 16;
        m->keys = xrealloc(m->keys, (size_t)m->cap * sizeof *m->keys);
        m->vals = xrealloc(m->vals, (size_t)m->cap * sizeof *m->vals);
    }
    x = m->n++;
    m->keys[x] = xstrdup(k);
    m->vals[x] = NULL;
    if (2 * m->n > m->nslot) smap_rehash(m, m->nslot ? m->nslot * 2 : 32);
    else {
        uint64_t i = str_hash(k) & (uint64_t)(m->nslot - 1);
        while (m->slot[i] >= 0) i = (i + 1) & (uint64_t)(m->nslot - 1);
        m->slot[i] = x;
    }
    return x;
}

TOOL void *smap_get(const struct smap *m, const char *k)
{
    int x = smap_find(m, k);
    return x < 0 ? NULL : m->vals[x];
}

TOOL int smap_has(const struct smap *m, const char *k) { return smap_find(m, k) >= 0; }

TOOL void smap_put(struct smap *m, const char *k, void *v)
{
    int x = smap_add(m, k);
    m->vals[x] = v;
}

/* ------------------------------------------------------------ Python's floats */

/* the shortest decimal digits that read back as x (repr's), and the
 * decimal point's position: x = 0.DIGITS * 10^decpt. x finite and not 0 */
TOOL void py_shortest(double x, char *digits, int *decpt)
{
    char buf[64];
    for (int p = 1; p <= 17; ++p) {
        snprintf(buf, sizeof buf, "%.*e", p - 1, fabs(x));
        char d[32];
        int nd = 0;
        const char *q = buf;
        for (; *q && *q != 'e'; ++q)
            if (*q >= '0' && *q <= '9') d[nd++] = *q;
        d[nd] = 0;
        int e = atoi(q + 1);
        char cand[3][32];
        int ce[3];
        /* the correctly rounded P digits, and the decimal neighbours on
         * either side (one of them may read back where the nearest does
         * not, next to a power of two) */
        strcpy(cand[0], d);
        ce[0] = e;
        for (int s = 1; s <= 2; ++s) {
            char t[32];
            strcpy(t, d);
            int te = e, i = nd - 1;
            if (s == 1) {
                while (i >= 0 && t[i] == '9') t[i--] = '0';
                if (i < 0) {
                    memmove(t + 1, t, (size_t)nd + 1);
                    t[0] = '1';
                    t[nd] = 0;
                    ++te;
                } else t[i]++;
            } else {
                while (i >= 0 && t[i] == '0') t[i--] = '9';
                if (i < 0 || (i == 0 && t[0] == '1' && nd == 1)) { cand[s][0] = 0; continue; }
                t[i]--;
                if (t[0] == '0') { memmove(t, t + 1, (size_t)nd); t[nd - 1] = '9'; t[nd] = 0; --te; }
            }
            strcpy(cand[s], t);
            ce[s] = te;
        }
        int best = -1;
        double bestd = 0;
        for (int s = 0; s < 3; ++s) {
            if (!cand[s][0]) continue;
            char t[64];
            snprintf(t, sizeof t, "%c.%se%d", cand[s][0], cand[s] + 1, ce[s]);
            double v = strtod(t, NULL);
            if (v != fabs(x)) continue;
            double dd = fabs(v - fabs(x));
            if (best < 0 || dd < bestd) best = s, bestd = dd;
            if (s == 0) break;
        }
        if (best >= 0) {
            char *c = cand[best];
            int n = (int)strlen(c);
            while (n > 1 && c[n - 1] == '0') c[--n] = 0;
            strcpy(digits, c);
            *decpt = ce[best] + 1;
            return;
        }
    }
    /* not reached: 17 digits always read back */
    snprintf(buf, sizeof buf, "%.16e", fabs(x));
    strcpy(digits, "0");
    *decpt = 1;
}

/* repr(x), as Python 3 prints a float (and json.dumps) */
TOOL char *py_repr(double x, char *out)
{
    if (isnan(x)) return strcpy(out, "nan");
    if (isinf(x)) return strcpy(out, x < 0 ? "-inf" : "inf");
    if (x == 0) return strcpy(out, signbit(x) ? "-0.0" : "0.0");
    char d[32];
    int decpt;
    py_shortest(x, d, &decpt);
    int nd = (int)strlen(d);
    char *o = out;
    if (x < 0) *o++ = '-';
    if (decpt <= -4 || decpt > 16) {
        *o++ = d[0];
        if (nd > 1) {
            *o++ = '.';
            memcpy(o, d + 1, (size_t)nd - 1);
            o += nd - 1;
        }
        int e = decpt - 1;
        sprintf(o, "e%c%02d", e < 0 ? '-' : '+', e < 0 ? -e : e);
        return out;
    }
    if (decpt <= 0) {
        *o++ = '0';
        *o++ = '.';
        for (int i = 0; i < -decpt; ++i) *o++ = '0';
        memcpy(o, d, (size_t)nd);
        o += nd;
    } else if (decpt >= nd) {
        memcpy(o, d, (size_t)nd);
        o += nd;
        for (int i = 0; i < decpt - nd; ++i) *o++ = '0';
        *o++ = '.';
        *o++ = '0';
    } else {
        memcpy(o, d, (size_t)decpt);
        o += decpt;
        *o++ = '.';
        memcpy(o, d + decpt, (size_t)(nd - decpt));
        o += nd - decpt;
    }
    *o = 0;
    return out;
}

/* round(x, n) for n >= 0: the double nearest the correctly rounded decimal */
TOOL double py_round(double x, int n)
{
    if (!isfinite(x)) return x;
    char buf[512];
    snprintf(buf, sizeof buf, "%.*f", n, x);
    return strtod(buf, NULL);
}

/* round(x): the nearest integer, ties to even */
TOOL double py_round0(double x) { return rint(x); }

/* math.hypot(a, b): CPython's vector_norm (an fma-based correction step),
 * which glibc's hypot differs from by an ulp in about 0.2% of cases */
struct py_dl {
    double hi, lo;
};

TOOL struct py_dl py_dl_fast_sum(double a, double b)
{
    double x = a + b;
    double y = (a - x) + b;
    return (struct py_dl){x, y};
}

TOOL struct py_dl py_dl_mul(double x, double y)
{
    double z = x * y;
    double zz = fma(x, y, -z);
    return (struct py_dl){z, zz};
}

TOOL double py_vector_norm(int n, double *vec, double max)
{
    double x, h, scale, csum = 1.0, frac1 = 0.0, frac2 = 0.0;
    struct py_dl pr, sm;
    int max_e;
    if (isinf(max)) return max;
    if (max == 0.0 || n <= 1) return max;
    frexp(max, &max_e);
    if (max_e < -1023) {
        for (int i = 0; i < n; i++) vec[i] /= 2.2250738585072014e-308;
        return 2.2250738585072014e-308 * py_vector_norm(n, vec, max / 2.2250738585072014e-308);
    }
    scale = ldexp(1.0, -max_e);
    for (int i = 0; i < n; i++) {
        x = vec[i] * scale;
        pr = py_dl_mul(x, x);
        sm = py_dl_fast_sum(csum, pr.hi);
        csum = sm.hi;
        frac1 += pr.lo;
        frac2 += sm.lo;
    }
    h = sqrt(csum - 1.0 + (frac1 + frac2));
    pr = py_dl_mul(-h, h);
    sm = py_dl_fast_sum(csum, pr.hi);
    csum = sm.hi;
    frac1 += pr.lo;
    frac2 += sm.lo;
    x = csum - 1.0 + (frac1 + frac2);
    h += x / (2.0 * h);
    return h / scale;
}

TOOL double py_hypot(double a, double b)
{
    if (isinf(a) || isinf(b)) return INFINITY;
    if (isnan(a) || isnan(b)) return NAN;
    double v[2] = {fabs(a), fabs(b)};
    return py_vector_norm(2, v, v[0] > v[1] ? v[0] : v[1]);
}

/* math.degrees */
TOOL double py_degrees(double x) { return x * (180.0 / 3.141592653589793); }

/* Python's min(a, b) and max(a, b) of floats: the first of equals */
TOOL double py_min(double a, double b) { return b < a ? b : a; }
TOOL double py_max(double a, double b) { return b > a ? b : a; }

/* ------------------------------------------------------------ JSON output */

/* s as json.dumps writes a str (ensure_ascii: every non-ASCII code point as
 * \uXXXX, astral ones as a surrogate pair) */
TOOL void json_esc(struct sb *b, const char *s)
{
    sb_putc(b, '"');
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        unsigned c = *p;
        if (c == '"') sb_puts(b, "\\\""), ++p;
        else if (c == '\\') sb_puts(b, "\\\\"), ++p;
        else if (c == '\n') sb_puts(b, "\\n"), ++p;
        else if (c == '\r') sb_puts(b, "\\r"), ++p;
        else if (c == '\t') sb_puts(b, "\\t"), ++p;
        else if (c == '\b') sb_puts(b, "\\b"), ++p;
        else if (c == '\f') sb_puts(b, "\\f"), ++p;
        else if (c < 0x20 || c == 0x7f) {
            if (c == 0x7f) sb_putc(b, (char)c);
            else sb_printf(b, "\\u%04x", c);
            ++p;
        } else if (c < 0x80) sb_putc(b, (char)c), ++p;
        else {
            unsigned cp;
            int n;
            if ((c & 0xe0) == 0xc0) cp = c & 0x1f, n = 1;
            else if ((c & 0xf0) == 0xe0) cp = c & 0x0f, n = 2;
            else if ((c & 0xf8) == 0xf0) cp = c & 0x07, n = 3;
            else cp = 0xfffd, n = 0;
            ++p;
            for (int i = 0; i < n; ++i) {
                if ((*p & 0xc0) != 0x80) { cp = 0xfffd; break; }
                cp = cp << 6 | (*p++ & 0x3f);
            }
            if (cp >= 0x10000) {
                cp -= 0x10000;
                sb_printf(b, "\\u%04x\\u%04x", 0xd800 + (cp >> 10), 0xdc00 + (cp & 0x3ff));
            } else sb_printf(b, "\\u%04x", cp);
        }
    }
    sb_putc(b, '"');
}

TOOL void json_num(struct sb *b, double x)
{
    char t[64];
    sb_puts(b, py_repr(x, t));
}

/* ------------------------------------------------------------ Python's random */

/* random.Random: MT19937 seeded as CPython seeds it (init_by_array over the
 * seed's 32-bit words, least significant first), random() from two draws,
 * getrandbits and _randbelow as the 3.x module draws them */
struct pyrand {
    uint32_t mt[624];
    int mti;
};

TOOL void pr_init_genrand(struct pyrand *r, uint32_t s)
{
    r->mt[0] = s;
    for (int i = 1; i < 624; ++i) r->mt[i] = 1812433253U * (r->mt[i - 1] ^ (r->mt[i - 1] >> 30)) + (uint32_t)i;
    r->mti = 624;
}

TOOL void pr_init_by_array(struct pyrand *r, const uint32_t *key, size_t len)
{
    pr_init_genrand(r, 19650218U);
    size_t i = 1, j = 0, k = 624 > len ? 624 : len;
    uint32_t *mt = r->mt;
    for (; k; --k) {
        mt[i] = (mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1664525U)) + key[j] + (uint32_t)j;
        ++i, ++j;
        if (i >= 624) { mt[0] = mt[623]; i = 1; }
        if (j >= len) j = 0;
    }
    for (k = 623; k; --k) {
        mt[i] = (mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1566083941U)) - (uint32_t)i;
        ++i;
        if (i >= 624) { mt[0] = mt[623]; i = 1; }
    }
    mt[0] = 0x80000000U;
}

/* the seed as a little-endian byte string of abs(n) */
TOOL void pr_seed_bytes(struct pyrand *r, const unsigned char *le, size_t n)
{
    while (n && !le[n - 1]) --n;
    size_t w = n ? (n + 3) / 4 : 1;
    uint32_t *key = xcalloc(w, sizeof *key);
    for (size_t i = 0; i < n; ++i) key[i / 4] |= (uint32_t)le[i] << (8 * (i % 4));
    pr_init_by_array(r, key, w);
    free(key);
}

TOOL void pr_seed_int(struct pyrand *r, long long s)
{
    unsigned long long u = s < 0 ? 0ULL - (unsigned long long)s : (unsigned long long)s;
    unsigned char le[8];
    for (int i = 0; i < 8; ++i) le[i] = (unsigned char)(u >> (8 * i));
    pr_seed_bytes(r, le, 8);
}

/* ---- SHA-512 (random.seed of a str: int.from_bytes(s + sha512(s), 'big')) */
TOOL uint64_t sha512_ror(uint64_t x, int s) { return x >> s | x << (64 - s); }

TOOL void sha512(const unsigned char *msg, size_t len, unsigned char out[64])
{
    static const uint64_t K[80] = {
        0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL,
        0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL, 0x12835b0145706fbeULL,
        0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL, 0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL,
        0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
        0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL, 0x983e5152ee66dfabULL,
        0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
        0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL,
        0x53380d139d95b3dfULL, 0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
        0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL, 0xd192e819d6ef5218ULL,
        0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL, 0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL,
        0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL,
        0x682e6ff3d6b2b8a3ULL, 0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
        0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL, 0xca273eceea26619cULL,
        0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL,
        0x113f9804bef90daeULL, 0x1b710b35131c471bULL, 0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL,
        0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL};
    uint64_t h[8] = {0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
                     0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};
    size_t total = ((len + 17 + 127) / 128) * 128;
    unsigned char *m = xcalloc(total, 1);
    memcpy(m, msg, len);
    m[len] = 0x80;
    uint64_t bits = (uint64_t)len * 8;
    for (int i = 0; i < 8; ++i) m[total - 1 - i] = (unsigned char)(bits >> (8 * i));
    for (size_t off = 0; off < total; off += 128) {
        uint64_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = 0;
            for (int k = 0; k < 8; ++k) w[i] = w[i] << 8 | m[off + 8 * (size_t)i + (size_t)k];
        }
        for (int i = 16; i < 80; ++i) {
            uint64_t s0 = sha512_ror(w[i - 15], 1) ^ sha512_ror(w[i - 15], 8) ^ (w[i - 15] >> 7);
            uint64_t s1 = sha512_ror(w[i - 2], 19) ^ sha512_ror(w[i - 2], 61) ^ (w[i - 2] >> 6);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint64_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 80; ++i) {
            uint64_t S1 = sha512_ror(e, 14) ^ sha512_ror(e, 18) ^ sha512_ror(e, 41);
            uint64_t ch = (e & f) ^ (~e & g);
            uint64_t t1 = hh + S1 + ch + K[i] + w[i];
            uint64_t S0 = sha512_ror(a, 28) ^ sha512_ror(a, 34) ^ sha512_ror(a, 39);
            uint64_t mj = (a & b) ^ (a & c) ^ (b & c);
            uint64_t t2 = S0 + mj;
            hh = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
        }
        h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e, h[5] += f, h[6] += g, h[7] += hh;
    }
    free(m);
    for (int i = 0; i < 8; ++i)
        for (int k = 0; k < 8; ++k) out[8 * i + k] = (unsigned char)(h[i] >> (56 - 8 * k));
}

TOOL void pr_seed_str(struct pyrand *r, const char *s)
{
    size_t n = strlen(s);
    unsigned char *be = xmalloc(n + 64), *le = xmalloc(n + 64);
    memcpy(be, s, n);
    sha512((const unsigned char *)s, n, be + n);
    for (size_t i = 0; i < n + 64; ++i) le[i] = be[n + 63 - i];
    pr_seed_bytes(r, le, n + 64);
    free(be);
    free(le);
}

TOOL uint32_t pr_u32(struct pyrand *r)
{
    static const uint32_t mag01[2] = {0, 0x9908b0dfU};
    uint32_t y;
    uint32_t *mt = r->mt;
    if (r->mti >= 624) {
        int kk;
        for (kk = 0; kk < 624 - 397; ++kk) {
            y = (mt[kk] & 0x80000000U) | (mt[kk + 1] & 0x7fffffffU);
            mt[kk] = mt[kk + 397] ^ (y >> 1) ^ mag01[y & 1];
        }
        for (; kk < 623; ++kk) {
            y = (mt[kk] & 0x80000000U) | (mt[kk + 1] & 0x7fffffffU);
            mt[kk] = mt[kk + (397 - 624)] ^ (y >> 1) ^ mag01[y & 1];
        }
        y = (mt[623] & 0x80000000U) | (mt[0] & 0x7fffffffU);
        mt[623] = mt[396] ^ (y >> 1) ^ mag01[y & 1];
        r->mti = 0;
    }
    y = mt[r->mti++];
    y ^= y >> 11;
    y ^= (y << 7) & 0x9d2c5680U;
    y ^= (y << 15) & 0xefc60000U;
    y ^= y >> 18;
    return y;
}

TOOL double pr_random(struct pyrand *r)
{
    uint32_t a = pr_u32(r) >> 5, b = pr_u32(r) >> 6;
    return (a * 67108864.0 + b) * (1.0 / 9007199254740992.0);
}

/* getrandbits(k), 0 <= k <= 32 */
TOOL uint32_t pr_bits(struct pyrand *r, int k)
{
    if (k == 0) return 0;
    return pr_u32(r) >> (32 - k);
}

/* _randbelow(n), n >= 1 */
TOOL long long pr_below(struct pyrand *r, long long n)
{
    int k = 0;
    while (k < 63 && (1LL << k) <= n) ++k;
    if (k > 32) die("pr_below: %lld too large", n);
    long long v = pr_bits(r, k);
    while (v >= n) v = pr_bits(r, k);
    return v;
}

TOOL long long pr_randint(struct pyrand *r, long long a, long long b) { return a + pr_below(r, b - a + 1); }

TOOL double pr_uniform(struct pyrand *r, double a, double b) { return a + (b - a) * pr_random(r); }

/* shuffle(x) over n elements of size sz */
TOOL void pr_shuffle(struct pyrand *r, void *x, int n, size_t sz)
{
    unsigned char *p = x, t[64];
    if (sz > sizeof t) die("pr_shuffle: element too large");
    for (int i = n - 1; i >= 1; --i) {
        int j = (int)pr_below(r, i + 1);
        memcpy(t, p + (size_t)i * sz, sz);
        memcpy(p + (size_t)i * sz, p + (size_t)j * sz, sz);
        memcpy(p + (size_t)j * sz, t, sz);
    }
}

/* sample(range(n), k) as indices into out: the pool form (n at most the
 * small set size, which every caller's population is) */
TOOL void pr_sample_idx(struct pyrand *r, int n, int k, int *out)
{
    int setsize = 21;
    if (k > 5) {
        int c = 0;
        double l = log(k * 3.0) / log(4.0);
        c = (int)ceil(l);
        setsize += 1 << (2 * c);
    }
    if (n > setsize) die("pr_sample_idx: the set form is not ported (n %d k %d)", n, k);
    int *pool = xmalloc((size_t)n * sizeof *pool);
    for (int i = 0; i < n; ++i) pool[i] = i;
    for (int i = 0; i < k; ++i) {
        int j = (int)pr_below(r, n - i);
        out[i] = pool[j];
        pool[j] = pool[n - i - 1];
    }
    free(pool);
}

/* ------------------------------------------------------------ SHA-1 */

struct sha1ctx {
    uint32_t h[5];
    uint64_t n;
    uint8_t b[64];
    int k;
};

TOOL uint32_t sha1_rol(uint32_t x, int s) { return x << s | x >> (32 - s); }

TOOL void sha1_block(struct sha1ctx *c, const uint8_t *p)
{
    uint32_t w[80], a = c->h[0], b = c->h[1], d = c->h[2], e = c->h[3], f = c->h[4];
    for (int i = 0; i < 16; ++i) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 80; ++i) w[i] = sha1_rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    for (int i = 0; i < 80; ++i) {
        uint32_t g, k;
        if (i < 20) g = (b & d) | (~b & e), k = 0x5a827999U;
        else if (i < 40) g = b ^ d ^ e, k = 0x6ed9eba1U;
        else if (i < 60) g = (b & d) | (b & e) | (d & e), k = 0x8f1bbcdcU;
        else g = b ^ d ^ e, k = 0xca62c1d6U;
        uint32_t t = sha1_rol(a, 5) + g + f + k + w[i];
        f = e, e = d, d = sha1_rol(b, 30), b = a, a = t;
    }
    c->h[0] += a, c->h[1] += b, c->h[2] += d, c->h[3] += e, c->h[4] += f;
}

TOOL void sha1_init(struct sha1ctx *c)
{
    static const uint32_t h0[5] = {0x67452301U, 0xefcdab89U, 0x98badcfeU, 0x10325476U, 0xc3d2e1f0U};
    memcpy(c->h, h0, sizeof h0);
    c->n = 0;
    c->k = 0;
}

TOOL void sha1_add(struct sha1ctx *c, const void *data, size_t n)
{
    const uint8_t *p = data;
    c->n += n;
    while (n) {
        size_t t = 64 - (size_t)c->k < n ? 64 - (size_t)c->k : n;
        memcpy(c->b + c->k, p, t);
        c->k += (int)t, p += t, n -= t;
        if (c->k == 64) sha1_block(c, c->b), c->k = 0;
    }
}

/* the digest as 40 hex characters */
TOOL void sha1_hex(struct sha1ctx *c, char out[41])
{
    uint64_t bits = c->n * 8;
    uint8_t pad = 0x80, z = 0;
    sha1_add(c, &pad, 1);
    while (c->k != 56) sha1_add(c, &z, 1);
    uint8_t L[8];
    for (int i = 0; i < 8; ++i) L[i] = (uint8_t)(bits >> (56 - 8 * i));
    sha1_add(c, L, 8);
    for (int i = 0; i < 5; ++i) sprintf(out + 8 * i, "%08x", c->h[i]);
    out[40] = 0;
}

TOOL void sha1_of(const void *data, size_t n, char out[41])
{
    struct sha1ctx c;
    sha1_init(&c);
    sha1_add(&c, data, n);
    sha1_hex(&c, out);
}

/* ------------------------------------------------------------ files */

/* the whole file, NUL terminated (*len its size), or NULL */
TOOL char *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    struct sb b = {0};
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) sb_putn(&b, buf, n);
    fclose(f);
    if (len) *len = b.n;
    return (char *)sb_str(&b);
}

TOOL int path_exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0;
}

TOOL int is_dir(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

TOOL int is_file(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

TOOL int write_file(const char *path, const char *data, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t w = fwrite(data, 1, n, f);
    return fclose(f) == 0 && w == n ? 0 : -1;
}

/* mkdir -p */
TOOL void mkdirs(const char *path)
{
    char *p = xstrdup(path);
    for (char *q = p + 1; *q; ++q)
        if (*q == '/') {
            *q = 0;
            mkdir(p, 0777);
            *q = '/';
        }
    mkdir(p, 0777);
    free(p);
}

/* the entries of a directory (no . or ..), sorted; hidden ones only with all */
TOOL struct sv list_dir(const char *dir, int all)
{
    struct sv out = {0};
    DIR *d = opendir(dir);
    if (!d) return out;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (!all && e->d_name[0] == '.') continue;
        sv_push(&out, xstrdup(e->d_name));
    }
    closedir(d);
    sv_sort(&out);
    return out;
}

TOOL char *path_join(const char *a, const char *b)
{
    if (b[0] == '/' || !a[0]) return xstrdup(b);
    size_t n = strlen(a);
    return xasprintf("%s%s%s", a, a[n - 1] == '/' ? "" : "/", b);
}

TOOL const char *base_name(const char *p)
{
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

/* os.path.dirname */
TOOL char *dir_name(const char *p)
{
    const char *s = strrchr(p, '/');
    if (!s) return xstrdup("");
    if (s == p) return xstrdup("/");
    return xstrndup(p, (size_t)(s - p));
}

/* os.path.normpath for absolute or relative paths */
TOOL char *norm_path(const char *p)
{
    if (!*p) return xstrdup(".");
    int abs_ = p[0] == '/';
    char *t = xstrdup(p);
    char **parts = xmalloc((strlen(p) + 1) * sizeof *parts);
    int n = 0;
    for (char *s = strtok(t, "/"); s; s = strtok(NULL, "/")) {
        if (!strcmp(s, ".")) continue;
        if (!strcmp(s, "..")) {
            if (n && strcmp(parts[n - 1], "..")) { --n; continue; }
            if (abs_) continue;
        }
        parts[n++] = s;
    }
    struct sb b = {0};
    if (abs_) sb_putc(&b, '/');
    /* POSIX keeps a leading // */
    if (abs_ && p[1] == '/' && p[2] != '/') sb_putc(&b, '/');
    for (int i = 0; i < n; ++i) {
        if (i) sb_putc(&b, '/');
        sb_puts(&b, parts[i]);
    }
    if (!b.n) sb_putc(&b, '.');
    free(parts);
    free(t);
    return (char *)sb_str(&b);
}

/* os.path.abspath */
TOOL char *abs_path(const char *p)
{
    if (p[0] == '/') return norm_path(p);
    char cwd[4096];
    if (!getcwd(cwd, sizeof cwd)) die("getcwd: %s", strerror(errno));
    char *j = path_join(cwd, p);
    char *r = norm_path(j);
    free(j);
    return r;
}

/* fnmatch-style match of one name against a pattern with * ? and [...] */
TOOL int glob_match(const char *pat, const char *s)
{
    while (*pat) {
        if (*pat == '*') {
            while (*pat == '*') ++pat;
            if (!*pat) return 1;
            for (; *s; ++s)
                if (glob_match(pat, s)) return 1;
            return glob_match(pat, s);
        }
        if (!*s) return 0;
        if (*pat == '?') { ++pat, ++s; continue; }
        if (*pat == '[') {
            const char *q = pat + 1;
            int neg = *q == '!' || *q == '^', hit = 0;
            if (neg) ++q;
            const char *start = q;
            while (*q && (*q != ']' || q == start)) {
                if (q[1] == '-' && q[2] && q[2] != ']') {
                    if ((unsigned char)*s >= (unsigned char)q[0] && (unsigned char)*s <= (unsigned char)q[2]) hit = 1;
                    q += 3;
                } else {
                    if (*q == *s) hit = 1;
                    ++q;
                }
            }
            if (*q != ']') { if (*pat != *s) return 0; ++pat, ++s; continue; }
            if (hit == neg) return 0;
            pat = q + 1, ++s;
            continue;
        }
        if (*pat != *s) return 0;
        ++pat, ++s;
    }
    return !*s;
}

/* every regular file or directory under dir (recursing through links to
 * directories, as os.walk(followlinks=True) and glob's ** do), as paths
 * relative to dir, unsorted; hidden names skipped unless all */
TOOL void walk_tree(const char *dir, const char *rel, int all, struct sv *out, int dirs_too)
{
    char *full = rel[0] ? path_join(dir, rel) : xstrdup(dir);
    struct sv es = list_dir(full, all);
    for (int i = 0; i < es.n; ++i) {
        char *r = rel[0] ? path_join(rel, es.v[i]) : xstrdup(es.v[i]);
        char *f = path_join(full, es.v[i]);
        if (is_dir(f)) {
            if (dirs_too) sv_push(out, xstrdup(r));
            walk_tree(dir, r, all, out, dirs_too);
        } else sv_push(out, xstrdup(r));
        free(f);
        free(r);
        free(es.v[i]);
    }
    free(es.v);
    free(full);
}

/* ------------------------------------------------------------ processes */

/* run argv (cwd and extra "K=V" environment entries may be NULL), stdout
 * into *out when out is given (else inherited or /dev/null by quiet),
 * stderr likewise; the exit status as a shell reports it */
TOOL int run_cmd(char *const argv[], const char *cwd, char *const env[], struct sb *out, int quiet, const char *in)
{
    int po[2] = {-1, -1}, pi[2] = {-1, -1};
    if (out && pipe(po)) die("pipe: %s", strerror(errno));
    if (in && pipe(pi)) die("pipe: %s", strerror(errno));
    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();
    if (pid < 0) die("fork: %s", strerror(errno));
    if (!pid) {
        if (cwd && chdir(cwd)) _exit(127);
        if (env)
            for (int i = 0; env[i]; ++i) putenv(env[i]);
        if (in) {
            dup2(pi[0], 0);
            close(pi[0]);
            close(pi[1]);
        }
        if (out) {
            dup2(po[1], 1);
            close(po[0]);
            close(po[1]);
        }
        if (quiet) {
            int nul = open("/dev/null", O_WRONLY);
            if (!out) dup2(nul, 1);
            dup2(nul, 2);
            close(nul);
        }
        execvp(argv[0], argv);
        _exit(127);
    }
    /* stdin written and stdout read together (poll): a child that answers as
     * it reads (git cat-file --batch) fills one pipe while we fill the other */
    if (in) close(pi[0]);
    if (out) close(po[1]);
    void (*oldpipe)(int) = in ? signal(SIGPIPE, SIG_IGN) : SIG_DFL;
    size_t n = in ? strlen(in) : 0, w = 0;
    int infd = in ? pi[1] : -1, outfd = out ? po[0] : -1;
    if (in && !n) close(infd), infd = -1;
    if (infd >= 0) fcntl(infd, F_SETFL, fcntl(infd, F_GETFL) | O_NONBLOCK);
    while (infd >= 0 || outfd >= 0) {
        struct pollfd pf[2];
        int np = 0, wi = -1, ri = -1;
        if (infd >= 0) pf[np] = (struct pollfd){infd, POLLOUT, 0}, wi = np++;
        if (outfd >= 0) pf[np] = (struct pollfd){outfd, POLLIN, 0}, ri = np++;
        if (poll(pf, (nfds_t)np, -1) < 0) {
            if (errno == EINTR) continue;
            die("poll: %s", strerror(errno));
        }
        if (wi >= 0 && pf[wi].revents) {
            ssize_t k = write(infd, in + w, n - w);
            if (k > 0) w += (size_t)k;
            if (k < 0 && errno != EAGAIN && errno != EINTR) w = n;
            if (w >= n) close(infd), infd = -1;
        }
        if (ri >= 0 && pf[ri].revents) {
            char buf[65536];
            ssize_t k = read(outfd, buf, sizeof buf);
            if (k > 0) sb_putn(out, buf, (size_t)k);
            else if (k == 0 || (errno != EAGAIN && errno != EINTR)) close(outfd), outfd = -1;
        }
    }
    if (in) signal(SIGPIPE, oldpipe);
    if (out) sb_str(out);
    int st;
    while (waitpid(pid, &st, 0) < 0)
        if (errno != EINTR) die("waitpid: %s", strerror(errno));
    if (WIFEXITED(st)) return WEXITSTATUS(st);
    if (WIFSIGNALED(st)) return -WTERMSIG(st);
    return -1;
}

/* the status of a waitpid as subprocess's returncode */
TOOL int wait_rc(int st)
{
    if (WIFEXITED(st)) return WEXITSTATUS(st);
    if (WIFSIGNALED(st)) return -WTERMSIG(st);
    return -1;
}

TOOL double now_secs(void)
{
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

#endif
