/* SHA-1 (FIPS 180-1), the one MessageDigest.getInstance("SHA-1") computes.
 * Rows.nbtLong reads the first eight bytes of it, big-endian. The block
 * function is the x86 SHA extensions' where the CPU has them (chosen once, at
 * the first call), else the portable one; both give the same state. */
#include "sha1.h"

#include <string.h>

#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#include <immintrin.h>
#define SHA1_X86 1
#endif

static uint32_t rol(uint32_t v, int n)
{
    return (v << n) | (v >> (32 - n));
}

static void blocks_portable(uint32_t h[5], const uint8_t *p, size_t nblocks)
{
    for (; nblocks > 0; --nblocks, p += 64)
    {
        uint32_t w[80];

        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 |
                   (uint32_t)p[i * 4 + 3];

        for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];

        for (int i = 0; i < 80; ++i)
        {
            uint32_t f, k;

            if (i < 20) { f = (b & c) | (~b & d); k = 0x5a827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ed9eba1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdc; }
            else { f = b ^ c ^ d; k = 0xca62c1d6; }

            uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = t;
        }

        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }
}

#ifdef SHA1_X86
/* Four rounds g (0..19) of the SHA-NI schedule: m[g % 4] holds W[4g..4g+3];
 * the same step finishes the next group's words (msg2), starts the words
 * three groups on (msg1) and folds in those eight back (xor). */
#define SHA1_GROUP(g, EC, EN, M0, M1, M2, M3, F) \
    EC = _mm_sha1nexte_epu32(EC, M0); \
    EN = abcd; \
    M1 = _mm_sha1msg2_epu32(M1, M0); \
    abcd = _mm_sha1rnds4_epu32(abcd, EC, F); \
    M3 = _mm_sha1msg1_epu32(M3, M0); \
    M2 = _mm_xor_si128(M2, M0);

__attribute__((target("sha,sse4.1,ssse3")))
static void blocks_ni(uint32_t h[5], const uint8_t *p, size_t nblocks)
{
    const __m128i bswap = _mm_set_epi64x(0x0001020304050607LL, 0x08090a0b0c0d0e0fLL);
    __m128i abcd = _mm_shuffle_epi32(_mm_loadu_si128((const __m128i *)h), 0x1b);
    __m128i e0 = _mm_set_epi32((int)h[4], 0, 0, 0);

    for (; nblocks > 0; --nblocks, p += 64)
    {
        __m128i abcd_save = abcd, e0_save = e0, e1;
        __m128i m0 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(p + 0)), bswap);
        __m128i m1 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(p + 16)), bswap);
        __m128i m2 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(p + 32)), bswap);
        __m128i m3 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(p + 48)), bswap);

        /* rounds 0..15: the message words as loaded */
        e0 = _mm_add_epi32(e0, m0);
        e1 = abcd;
        abcd = _mm_sha1rnds4_epu32(abcd, e0, 0);

        e1 = _mm_sha1nexte_epu32(e1, m1);
        e0 = abcd;
        abcd = _mm_sha1rnds4_epu32(abcd, e1, 0);
        m0 = _mm_sha1msg1_epu32(m0, m1);

        e0 = _mm_sha1nexte_epu32(e0, m2);
        e1 = abcd;
        abcd = _mm_sha1rnds4_epu32(abcd, e0, 0);
        m1 = _mm_sha1msg1_epu32(m1, m2);
        m0 = _mm_xor_si128(m0, m2);

        SHA1_GROUP(3, e1, e0, m3, m0, m1, m2, 0)
        SHA1_GROUP(4, e0, e1, m0, m1, m2, m3, 0)
        SHA1_GROUP(5, e1, e0, m1, m2, m3, m0, 1)
        SHA1_GROUP(6, e0, e1, m2, m3, m0, m1, 1)
        SHA1_GROUP(7, e1, e0, m3, m0, m1, m2, 1)
        SHA1_GROUP(8, e0, e1, m0, m1, m2, m3, 1)
        SHA1_GROUP(9, e1, e0, m1, m2, m3, m0, 1)
        SHA1_GROUP(10, e0, e1, m2, m3, m0, m1, 2)
        SHA1_GROUP(11, e1, e0, m3, m0, m1, m2, 2)
        SHA1_GROUP(12, e0, e1, m0, m1, m2, m3, 2)
        SHA1_GROUP(13, e1, e0, m1, m2, m3, m0, 2)
        SHA1_GROUP(14, e0, e1, m2, m3, m0, m1, 2)
        SHA1_GROUP(15, e1, e0, m3, m0, m1, m2, 3)
        SHA1_GROUP(16, e0, e1, m0, m1, m2, m3, 3)
        SHA1_GROUP(17, e1, e0, m1, m2, m3, m0, 3)
        SHA1_GROUP(18, e0, e1, m2, m3, m0, m1, 3)

        /* rounds 76..79 */
        e1 = _mm_sha1nexte_epu32(e1, m3);
        e0 = abcd;
        abcd = _mm_sha1rnds4_epu32(abcd, e1, 3);

        e0 = _mm_sha1nexte_epu32(e0, e0_save);
        abcd = _mm_add_epi32(abcd, abcd_save);
    }

    _mm_storeu_si128((__m128i *)h, _mm_shuffle_epi32(abcd, 0x1b));
    h[4] = (uint32_t)_mm_extract_epi32(e0, 3);
}

static int cpu_has_sha(void)
{
    unsigned a, b, c, d;

    if (!__get_cpuid(1, &a, &b, &c, &d)) return 0;
    if (!(c & (1u << 9)) || !(c & (1u << 19))) return 0; /* SSSE3, SSE4.1 */
    if (__get_cpuid_max(0, NULL) < 7) return 0;
    __cpuid_count(7, 0, a, b, c, d);
    return (b & (1u << 29)) != 0; /* SHA */
}
#endif

static void (*blocks)(uint32_t h[5], const uint8_t *p, size_t nblocks) = blocks_portable;

/* before main: the same for every environment */
__attribute__((constructor)) static void blocks_select(void)
{
#ifdef SHA1_X86
    blocks = cpu_has_sha() ? blocks_ni : blocks_portable;
#endif
}

void sha1_init(uint32_t h[5])
{
    h[0] = 0x67452301;
    h[1] = 0xefcdab89;
    h[2] = 0x98badcfe;
    h[3] = 0x10325476;
    h[4] = 0xc3d2e1f0;
}

void sha1_blocks(uint32_t h[5], const uint8_t *data, size_t nblocks)
{
    if (nblocks > 0) blocks(h, data, nblocks);
}

void sha1_final(uint32_t h[5], const uint8_t *tail, size_t rem, uint64_t n, uint8_t out[20])
{
    uint8_t last[128];

    memcpy(last, tail, rem);
    last[rem++] = 0x80;

    size_t total = rem <= 56 ? 64 : 128;

    memset(last + rem, 0, total - rem);

    uint64_t bits = n * 8;

    for (int i = 0; i < 8; ++i) last[total - 8 + i] = (uint8_t)(bits >> (56 - i * 8));

    blocks(h, last, total / 64);

    for (int i = 0; i < 5; ++i)
    {
        out[i * 4] = (uint8_t)(h[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(h[i] >> 8);
        out[i * 4 + 3] = (uint8_t)h[i];
    }
}

void sha1(const uint8_t *data, size_t n, uint8_t out[20])
{
    uint32_t h[5];
    size_t full = n / 64;

    sha1_init(h);
    sha1_blocks(h, data, full);
    sha1_final(h, data + full * 64, n - full * 64, n, out);
}
