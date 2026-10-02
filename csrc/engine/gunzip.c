/* A whole-file gzip reader (RFC 1952 around an RFC 1951 inflater) that decodes
 * straight into one buffer. The recordings' gzip streams are mostly matches
 * over runs of zero bytes (a snapshot's chunks.bin.gz inflates about 80
 * times), and zlib 1.3's inflate_fast copies an overlapping match one byte at
 * a time; here a match copies eight bytes at a time (a run of one byte is a
 * memset). The output is the same bytes zlib gives, CRC-32 (carry-less
 * multiply folding where the CPU has it, zlib's crc32 otherwise) and length
 * checked against the trailer; any stream this reader does not take (several
 * members, a truncated file) is refused and the caller reads it with zlib
 * instead. */
#include "gunzip.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

/* ------------------------------------------------------------------ CRC-32 */

#if defined(__x86_64__) && defined(__GNUC__)
#include <immintrin.h>

/* CRC-32 (gzip's, bit-reflected) of whole 16-byte blocks by carry-less
 * multiply folding (Gopal et al., "Fast CRC Computation for Generic
 * Polynomials Using PCLMULQDQ", Intel 2009; the reflected constants and the
 * Barrett step as Chromium's zlib uses them). crc is the running register,
 * pre-inverted; len >= 64 and a multiple of 16. */
__attribute__((target("pclmul,sse4.1")))
static uint32_t crc_fold(const uint8_t *buf, size_t len, uint32_t crc)
{
    const __m128i k1k2 = _mm_set_epi64x(0x01c6e41596LL, 0x0154442bd4LL);
    const __m128i k3k4 = _mm_set_epi64x(0x00ccaa009eLL, 0x01751997d0LL);
    const __m128i k5k0 = _mm_set_epi64x(0, 0x0163cd6124LL);
    const __m128i poly = _mm_set_epi64x(0x01f7011641LL, 0x01db710641LL);
    __m128i x0, x1, x2, x3, x4, x5, x6, x7, x8;

    x1 = _mm_loadu_si128((const __m128i *)(buf + 0x00));
    x2 = _mm_loadu_si128((const __m128i *)(buf + 0x10));
    x3 = _mm_loadu_si128((const __m128i *)(buf + 0x20));
    x4 = _mm_loadu_si128((const __m128i *)(buf + 0x30));
    x1 = _mm_xor_si128(x1, _mm_cvtsi32_si128((int)crc));
    x0 = k1k2;
    buf += 64;
    len -= 64;

    while (len >= 64)
    {
        x5 = _mm_clmulepi64_si128(x1, x0, 0x00);
        x6 = _mm_clmulepi64_si128(x2, x0, 0x00);
        x7 = _mm_clmulepi64_si128(x3, x0, 0x00);
        x8 = _mm_clmulepi64_si128(x4, x0, 0x00);
        x1 = _mm_clmulepi64_si128(x1, x0, 0x11);
        x2 = _mm_clmulepi64_si128(x2, x0, 0x11);
        x3 = _mm_clmulepi64_si128(x3, x0, 0x11);
        x4 = _mm_clmulepi64_si128(x4, x0, 0x11);
        x1 = _mm_xor_si128(_mm_xor_si128(x1, x5), _mm_loadu_si128((const __m128i *)(buf + 0x00)));
        x2 = _mm_xor_si128(_mm_xor_si128(x2, x6), _mm_loadu_si128((const __m128i *)(buf + 0x10)));
        x3 = _mm_xor_si128(_mm_xor_si128(x3, x7), _mm_loadu_si128((const __m128i *)(buf + 0x20)));
        x4 = _mm_xor_si128(_mm_xor_si128(x4, x8), _mm_loadu_si128((const __m128i *)(buf + 0x30)));
        buf += 64;
        len -= 64;
    }

    x0 = k3k4;
    x5 = _mm_clmulepi64_si128(x1, x0, 0x00);
    x1 = _mm_clmulepi64_si128(x1, x0, 0x11);
    x1 = _mm_xor_si128(_mm_xor_si128(x1, x2), x5);
    x5 = _mm_clmulepi64_si128(x1, x0, 0x00);
    x1 = _mm_clmulepi64_si128(x1, x0, 0x11);
    x1 = _mm_xor_si128(_mm_xor_si128(x1, x3), x5);
    x5 = _mm_clmulepi64_si128(x1, x0, 0x00);
    x1 = _mm_clmulepi64_si128(x1, x0, 0x11);
    x1 = _mm_xor_si128(_mm_xor_si128(x1, x4), x5);

    while (len >= 16)
    {
        x2 = _mm_loadu_si128((const __m128i *)buf);
        x5 = _mm_clmulepi64_si128(x1, x0, 0x00);
        x1 = _mm_clmulepi64_si128(x1, x0, 0x11);
        x1 = _mm_xor_si128(_mm_xor_si128(x1, x2), x5);
        buf += 16;
        len -= 16;
    }

    x2 = _mm_clmulepi64_si128(x1, x0, 0x10);
    x3 = _mm_setr_epi32(~0, 0, ~0, 0);
    x1 = _mm_srli_si128(x1, 8);
    x1 = _mm_xor_si128(x1, x2);
    x0 = k5k0;
    x2 = _mm_srli_si128(x1, 4);
    x1 = _mm_and_si128(x1, x3);
    x1 = _mm_clmulepi64_si128(x1, x0, 0x00);
    x1 = _mm_xor_si128(x1, x2);
    x0 = poly;
    x2 = _mm_and_si128(x1, x3);
    x2 = _mm_clmulepi64_si128(x2, x0, 0x10);
    x2 = _mm_and_si128(x2, x3);
    x2 = _mm_clmulepi64_si128(x2, x0, 0x00);
    x1 = _mm_xor_si128(x1, x2);
    return (uint32_t)_mm_extract_epi32(x1, 1);
}

static int crc_fold_ok;

/* before main: the same for every environment */
__attribute__((constructor)) static void crc_fold_init(void)
{
    __builtin_cpu_init();
    crc_fold_ok = __builtin_cpu_supports("pclmul") && __builtin_cpu_supports("sse4.1");
}

/* zlib's crc32 for what the fold does not take */
static uint32_t crc_update(uint32_t crc, const uint8_t *p, size_t n)
{
    if (crc_fold_ok && n >= 64)
    {
        size_t k = n & ~(size_t)15;

        crc = ~crc_fold(p, k, ~crc);
        p += k;
        n -= k;
    }
    while (n > 0)
    {
        uInt k = n < (1u << 30) ? (uInt)n : (1u << 30);

        crc = (uint32_t)crc32(crc, p, k);
        p += k;
        n -= k;
    }
    return crc;
}
#else
static uint32_t crc_update(uint32_t crc, const uint8_t *p, size_t n)
{
    while (n > 0)
    {
        uInt k = n < (1u << 30) ? (uInt)n : (1u << 30);

        crc = (uint32_t)crc32(crc, p, k);
        p += k;
        n -= k;
    }
    return crc;
}
#endif

/* ------------------------------------------------------------ Huffman tables */

/* A decode table entry: bits 0-7 the code length (0 for no code, 0xff for a
 * subtable link), bits 16-31 the symbol or the subtable's offset. The primary
 * table is indexed by the next PRIMARY input bits; a longer code goes through
 * a subtable of 2^(15 - PRIMARY) entries indexed by the bits after those. */
#define PRIMARY 10
#define SUBBITS (15 - PRIMARY)
#define LINK 0xff

struct htab {
    uint32_t e[(1 << PRIMARY) + 320 * (1 << SUBBITS)];
};

static unsigned rev(unsigned code, int len)
{
    unsigned r = 0;

    for (int i = 0; i < len; ++i) { r = r << 1 | (code & 1); code >>= 1; }
    return r;
}

/* The canonical code for lens[0..n), as DEFLATE assigns it; 0 on an
 * over-subscribed set of lengths (an incomplete one is allowed, its unused
 * entries decode as errors). */
static int build(struct htab *t, const uint8_t *lens, int n)
{
    int count[16] = {0}, next[16];

    for (int i = 0; i < n; ++i) count[lens[i]]++;
    count[0] = 0;

    int left = 1;

    for (int l = 1; l < 16; ++l)
    {
        left <<= 1;
        left -= count[l];
        if (left < 0) return 0;
    }

    int code = 0;

    for (int l = 1; l < 16; ++l)
    {
        code = (code + count[l - 1]) << 1;
        next[l] = code;
    }

    memset(t->e, 0, sizeof t->e[0] << PRIMARY);

    int nsub = 0;

    for (int s = 0; s < n; ++s)
    {
        int len = lens[s];
        if (len == 0) continue;

        unsigned r = rev((unsigned)next[len]++, len);

        if (len <= PRIMARY)
        {
            for (unsigned i = r; i < 1u << PRIMARY; i += 1u << len) t->e[i] = (uint32_t)s << 16 | (uint32_t)len;
            continue;
        }

        unsigned p = r & ((1u << PRIMARY) - 1);

        if ((t->e[p] & 0xff) != LINK)
        {
            if (nsub == 320) return 0;
            unsigned off = (1u << PRIMARY) + (unsigned)nsub++ * (1u << SUBBITS);
            memset(&t->e[off], 0, sizeof t->e[0] << SUBBITS);
            t->e[p] = off << 16 | LINK;
        }

        unsigned off = t->e[p] >> 16;

        for (unsigned i = r >> PRIMARY; i < 1u << SUBBITS; i += 1u << (len - PRIMARY))
            t->e[off + i] = (uint32_t)s << 16 | (uint32_t)len;
    }

    return 1;
}

/* ----------------------------------------------------------------- inflater */

struct bits {
    const uint8_t *in, *end;   /* end: the first byte past the stream; the
                                * buffer holds 8 zero bytes after it */
    uint64_t buf;
    int cnt;
};

/* The next bytes into the bit buffer, at least 56 bits after it. The reads
 * run at most 8 bytes past the stream (the trailer and the zero padding), and
 * a stream that reads past that is refused (the callers check overrun). */
static inline void refill(struct bits *b)
{
    uint64_t w;

    memcpy(&w, b->in, 8);
    b->buf |= w << b->cnt;
    b->in += (63 - b->cnt) >> 3;
    b->cnt |= 56;
}

static inline int overrun(const struct bits *b)
{
    return b->in > b->end + 8;
}

static inline unsigned take(struct bits *b, int n)
{
    unsigned v = (unsigned)(b->buf & ((1ull << n) - 1));

    b->buf >>= n;
    b->cnt -= n;
    return v;
}

static inline int decode(struct bits *b, const struct htab *t)
{
    uint32_t e = t->e[b->buf & ((1u << PRIMARY) - 1)];

    if ((e & 0xff) == LINK) e = t->e[(e >> 16) + ((b->buf >> PRIMARY) & ((1u << SUBBITS) - 1))];

    int len = (int)(e & 0xff);

    if (len == 0) return -1;
    b->buf >>= len;
    b->cnt -= len;
    return (int)(e >> 16);
}

static const uint16_t LBASE[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                   35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t LEXT[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t DBASE[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
                                   1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t DEXT[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

/* One compressed block's symbols into out[*pos..cap); cap leaves 8 bytes of
 * slack in the buffer for the word copies. 1 at the end of the block, 2 when
 * the output reached limit first (the block goes on at the next call), 0 on
 * a bad stream. */
static int block(struct bits *b, const struct htab *lit, const struct htab *dist, uint8_t *out, size_t *pos, size_t cap,
                 size_t limit)
{
    size_t o = *pos;

    for (;;)
    {
        if (o >= limit) { *pos = o; return 2; }
        refill(b);
        if (overrun(b)) return 0;

        int s = decode(b, lit);

        if (s < 0) return 0;
        if (s < 256)
        {
            if (o >= cap) return 0;
            out[o++] = (uint8_t)s;
            continue;
        }
        if (s == 256) break;
        s -= 257;
        if (s >= 29) return 0;

        size_t len = LBASE[s] + take(b, LEXT[s]);
        int d = decode(b, dist);

        if (d < 0 || d >= 30) return 0;

        size_t dd = DBASE[d] + take(b, DEXT[d]);

        if (dd > o || len > cap - o) return 0;

        uint8_t *dst = out + o;
        const uint8_t *src = dst - dd;

        if (dd >= 8)
        {
            /* each 8-byte read lies wholly before the write it feeds; the
             * last word may run up to 7 bytes past the match (slack) */
            for (size_t k = 0; k < len; k += 8)
            {
                uint64_t w;
                memcpy(&w, src + k, 8);
                memcpy(dst + k, &w, 8);
            }
        }
        else if (dd == 1) memset(dst, *src, len);
        else
            for (size_t k = 0; k < len; ++k) dst[k] = src[k];

        o += len;
    }

    *pos = o;
    return 1;
}

static int dynamic(struct bits *b, struct htab *lit, struct htab *dist)
{
    static const uint8_t ORDER[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    uint8_t cl[19] = {0}, lens[320];

    refill(b);
    int nlit = (int)take(b, 5) + 257, ndist = (int)take(b, 5) + 1, ncl = (int)take(b, 4) + 4;

    if (nlit > 286 || ndist > 30) return 0;
    for (int i = 0; i < ncl; ++i)
    {
        refill(b);
        cl[ORDER[i]] = (uint8_t)take(b, 3);
    }
    if (overrun(b)) return 0;

    struct htab *clt = malloc(sizeof *clt);

    if (clt == NULL || !build(clt, cl, 19)) { free(clt); return 0; }

    int n = 0;

    while (n < nlit + ndist)
    {
        refill(b);
        int s = decode(b, clt);

        if (s < 0 || overrun(b)) { free(clt); return 0; }
        if (s < 16) { lens[n++] = (uint8_t)s; continue; }

        int rep, val = 0;

        if (s == 16)
        {
            if (n == 0) { free(clt); return 0; }
            val = lens[n - 1];
            rep = 3 + (int)take(b, 2);
        }
        else if (s == 17) rep = 3 + (int)take(b, 3);
        else rep = 11 + (int)take(b, 7);

        if (n + rep > nlit + ndist) { free(clt); return 0; }
        while (rep--) lens[n++] = (uint8_t)val;
    }

    free(clt);
    if (lens[256] == 0) return 0;
    return build(lit, lens, nlit) && build(dist, lens + nlit, ndist);
}

/* The raw DEFLATE stream at b into out[0..cap); *n the bytes written. */
static struct htab fixed_lit, fixed_dist;

/* before main: the same for every environment */
__attribute__((constructor)) static void fixed_init(void)
{
    uint8_t l[288];

    for (int i = 0; i < 288; ++i) l[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
    build(&fixed_lit, l, 288);
    for (int i = 0; i < 30; ++i) l[i] = 5;
    build(&fixed_dist, l, 30);
}

static int inflate_all(struct bits *b, uint8_t *out, size_t cap, size_t *n)
{
    struct htab *lit = NULL, *dist = NULL;
    size_t pos = 0;
    int last = 0, ok = 1;

    while (ok && !last)
    {
        refill(b);
        last = (int)take(b, 1);

        int type = (int)take(b, 2);

        if (type == 0)
        {
            /* stored: the rest of this byte is dropped, LEN and NLEN follow */
            take(b, b->cnt & 7);
            b->in -= b->cnt >> 3;
            b->buf = 0;
            b->cnt = 0;
            if (b->end - b->in < 4) { ok = 0; break; }

            size_t len = (size_t)(b->in[0] | b->in[1] << 8);
            size_t nlen = (size_t)(b->in[2] | b->in[3] << 8);

            b->in += 4;
            if ((len ^ 0xffff) != nlen || (size_t)(b->end - b->in) < len || len > cap - pos) { ok = 0; break; }
            memcpy(out + pos, b->in, len);
            b->in += len;
            pos += len;
        }
        else if (type == 1) ok = block(b, &fixed_lit, &fixed_dist, out, &pos, cap, (size_t)-1) == 1;
        else if (type == 2)
        {
            if (lit == NULL)
            {
                lit = malloc(sizeof *lit);
                dist = malloc(sizeof *dist);
                if (lit == NULL || dist == NULL) { ok = 0; break; }
            }
            ok = dynamic(b, lit, dist) && block(b, lit, dist, out, &pos, cap, (size_t)-1) == 1;
        }
        else ok = 0;

        /* the bit buffer reads ahead; past the stream's end it reads the zero
         * padding, and a stream that needs more than that is truncated */
        if (b->in - (b->cnt >> 3) > b->end) ok = 0;
    }

    free(lit);
    free(dist);

    /* back to the byte after the last block */
    b->in -= b->cnt >> 3;
    b->buf = 0;
    b->cnt = 0;
    *n = pos;
    return ok;
}

/* ------------------------------------------------------------------- gzip */

/* The whole file with 8 zero bytes after it, and the start of the DEFLATE
 * stream after the member header (magic, CM 8, FLG and its optional fields);
 * NULL when the file cannot be read or the header is not one. */
static uint8_t *read_member(const char *path, size_t *size, const uint8_t **stream)
{
    FILE *f = fopen(path, "rb");

    if (f == NULL) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }

    long sz = ftell(f);

    if (sz < 18 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }

    uint8_t *in = malloc((size_t)sz + 8);

    if (in == NULL || fread(in, 1, (size_t)sz, f) != (size_t)sz) { free(in); fclose(f); return NULL; }
    fclose(f);
    memset(in + sz, 0, 8);

    const uint8_t *p = in, *end = in + sz;
    int flg = p[3];

    if (p[0] != 0x1f || p[1] != 0x8b || p[2] != 8 || (flg & 0xe0)) { free(in); return NULL; }
    p += 10;
    if (flg & 4)
    {
        if (end - p < 2) { free(in); return NULL; }
        size_t xlen = (size_t)(p[0] | p[1] << 8);
        if ((size_t)(end - p) < 2 + xlen) { free(in); return NULL; }
        p += 2 + xlen;
    }
    for (int k = 8; k <= 16; k += 8)
        if (flg & k)
        {
            while (p < end && *p) ++p;
            if (p == end) { free(in); return NULL; }
            ++p;
        }
    if (flg & 2) p += 2;
    if (p >= end - 8) { free(in); return NULL; }

    *size = (size_t)sz;
    *stream = p;
    return in;
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24);
}

int gunzip_file(const char *path, uint8_t **out, size_t *len)
{
    return gunzip_file_crc(path, out, len, 1);
}

int gunzip_file_crc(const char *path, uint8_t **out, size_t *len, int check_crc)
{
    *out = NULL;
    *len = 0;

    size_t sz;
    const uint8_t *p;
    uint8_t *in = read_member(path, &sz, &p);

    if (in == NULL) return 0;

    const uint8_t *end = in + sz;

    /* ISIZE, the length mod 2^32, sizes the buffer; a single member only */
    size_t isize = le32(end - 4);
    uint8_t *buf = malloc(isize + 8);

    if (buf == NULL) { free(in); return 0; }

    struct bits b = {p, end - 8, 0, 0};
    size_t n = 0;
    int ok = inflate_all(&b, buf, isize, &n);

    if (ok && (b.in != end - 8 || n != isize)) ok = 0;
    if (ok && check_crc)
    {
        uint32_t crc = le32(end - 8);
        if (crc_update(0, buf, n) != crc) ok = 0;
    }

    free(in);
    if (!ok) { free(buf); return 0; }
    *out = buf;
    *len = n;
    return 1;
}

/* ----------------------------------------------------------------- stream */

/* WIN bytes of history before each SPAN of new output: the window a match
 * can reach back into (32 KiB) stays in front of the write position. */
#define WIN ((size_t)1 << 15)
#define SPAN ((size_t)1 << 22)

struct gunzip {
    gzFile g;                 /* the zlib path, when the file is not taken */
    uint8_t *in;
    const uint8_t *trailer;
    struct bits b;
    struct htab *dyn_lit, *dyn_dist;
    const struct htab *lit, *dist;
    uint8_t *buf;
    size_t pos, rd, stored;
    uint64_t total;
    uint32_t crc;
    int nocrc;                /* the trailer's CRC-32 not computed nor checked */
    int stage, last;          /* 0 a block header next, 1 in a coded block, 2 in
                               * a stored one, 3 the end (trailer checked), -1 a
                               * bad stream */
};

struct gunzip *gunzip_open(const char *path)
{
    return gunzip_open_crc(path, 1);
}

struct gunzip *gunzip_open_crc(const char *path, int check_crc)
{
    struct gunzip *z = calloc(1, sizeof *z);

    if (z == NULL) return NULL;
    z->nocrc = !check_crc;

    size_t sz;
    const uint8_t *p;

    z->in = read_member(path, &sz, &p);
    if (z->in != NULL) z->buf = malloc(WIN + SPAN + 8);
    if (z->in == NULL || z->buf == NULL)
    {
        free(z->in);
        free(z->buf);
        z->in = z->buf = NULL;
        z->g = gzopen(path, "rb");
        if (z->g == NULL) { free(z); return NULL; }
        return z;
    }

    z->trailer = z->in + sz - 8;
    z->b.in = p;
    z->b.end = z->trailer;
    z->crc = 0;
    return z;
}

/* More output after pos, up to the span's end; the stage says where the
 * stream stands. 0 on a bad stream. */
static int produce(struct gunzip *z)
{
    const size_t cap = WIN + SPAN, limit = cap - 258;
    size_t start = z->pos;
    struct bits *b = &z->b;

    while (z->pos < limit && z->stage != 3)
    {
        if (z->stage == 0)
        {
            if (z->last)
            {
                b->in -= b->cnt >> 3;
                b->buf = 0;
                b->cnt = 0;
                z->stage = 3;
                break;
            }
            refill(b);
            z->last = (int)take(b, 1);

            int type = (int)take(b, 2);

            if (type == 0)
            {
                take(b, b->cnt & 7);
                b->in -= b->cnt >> 3;
                b->buf = 0;
                b->cnt = 0;
                if (b->end - b->in < 4) return 0;

                size_t len = (size_t)(b->in[0] | b->in[1] << 8), nlen = (size_t)(b->in[2] | b->in[3] << 8);

                b->in += 4;
                if ((len ^ 0xffff) != nlen || (size_t)(b->end - b->in) < len) return 0;
                z->stored = len;
                z->stage = 2;
            }
            else if (type == 1)
            {
                z->lit = &fixed_lit;
                z->dist = &fixed_dist;
                z->stage = 1;
            }
            else if (type == 2)
            {
                if (z->dyn_lit == NULL)
                {
                    z->dyn_lit = malloc(sizeof *z->dyn_lit);
                    z->dyn_dist = malloc(sizeof *z->dyn_dist);
                    if (z->dyn_lit == NULL || z->dyn_dist == NULL) return 0;
                }
                if (!dynamic(b, z->dyn_lit, z->dyn_dist)) return 0;
                z->lit = z->dyn_lit;
                z->dist = z->dyn_dist;
                z->stage = 1;
            }
            else return 0;
        }
        else if (z->stage == 1)
        {
            int r = block(b, z->lit, z->dist, z->buf, &z->pos, cap, limit);

            if (r == 0) return 0;
            if (r == 1) z->stage = 0;
        }
        else
        {
            size_t k = z->stored < limit - z->pos ? z->stored : limit - z->pos;

            memcpy(z->buf + z->pos, b->in, k);
            b->in += k;
            z->pos += k;
            z->stored -= k;
            if (z->stored == 0) z->stage = 0;
        }

        if (b->in - (b->cnt >> 3) > b->end) return 0;
    }

    if (!z->nocrc) z->crc = crc_update(z->crc, z->buf + start, z->pos - start);
    z->total += z->pos - start;

    if (z->stage == 3 && (b->in != z->trailer || (!z->nocrc && le32(z->trailer) != z->crc) ||
                          le32(z->trailer + 4) != (uint32_t)z->total))
        return 0;
    return 1;
}

long gunzip_read(struct gunzip *z, void *dst, size_t n)
{
    if (z->g != NULL)
    {
        long got = 0;

        while ((size_t)got < n)
        {
            size_t k = n - (size_t)got < (1u << 30) ? n - (size_t)got : (1u << 30);
            int r = gzread(z->g, (uint8_t *)dst + got, (unsigned)k);

            if (r < 0) return -1;
            if (r == 0) break;
            got += r;
        }
        return got;
    }

    size_t got = 0;

    while (got < n)
    {
        if (z->rd == z->pos)
        {
            if (z->stage < 0) return -1;
            if (z->stage == 3) break;
            if (z->pos > WIN)
            {
                memmove(z->buf, z->buf + z->pos - WIN, WIN);
                z->pos = z->rd = WIN;
            }
            if (!produce(z)) { z->stage = -1; return -1; }
            continue;
        }

        size_t k = n - got < z->pos - z->rd ? n - got : z->pos - z->rd;

        memcpy((uint8_t *)dst + got, z->buf + z->rd, k);
        z->rd += k;
        got += k;
    }

    return (long)got;
}

void gunzip_close(struct gunzip *z)
{
    if (z == NULL) return;
    if (z->g != NULL) gzclose(z->g);
    free(z->in);
    free(z->buf);
    free(z->dyn_lit);
    free(z->dyn_dist);
    free(z);
}

char *gunzip_gets(struct gunzip *z, char *dst, int len)
{
    if (z->g != NULL) return gzgets(z->g, dst, len);
    if (len <= 0) return NULL;

    int n = 0;

    while (n < len - 1)
    {
        if (z->rd == z->pos)
        {
            if (z->stage < 0 || z->stage == 3) break;
            if (z->pos > WIN)
            {
                memmove(z->buf, z->buf + z->pos - WIN, WIN);
                z->pos = z->rd = WIN;
            }
            if (!produce(z)) { z->stage = -1; return NULL; }
            continue;
        }

        char c = (char)z->buf[z->rd++];

        dst[n++] = c;
        if (c == '\n') break;
    }

    dst[n] = 0;
    return n > 0 ? dst : NULL;
}
