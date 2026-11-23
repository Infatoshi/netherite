/* engine/lzpack.h, the codec of the parked chunks and the spill file: round
 * trips over lengths 0 to 300 and a few long inputs of every shape the
 * format has a path for (runs at offsets 1 to 20, literal and match lengths
 * past 15 + 255, offsets at 65535, incompressible bytes), with the decoder
 * writing nothing past its slack; then streams cut short, a cap below the
 * length and flipped bytes, each refused or decoded inside its buffer.
 *   lzpack_check (make lzpack-check; make test runs it) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../engine/lzpack.h"

#define GUARD 64

static uint64_t rng = 0x9e3779b97f4a7c15ull;

static uint32_t next(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (uint32_t)(rng >> 16);
}

/* shape k of n bytes into p */
static void fill(uint8_t *p, uint32_t n, int k)
{
    for (uint32_t i = 0; i < n; ++i)
        switch (k)
        {
        case 0: p[i] = 0; break;
        case 1: p[i] = (uint8_t)next(); break;
        case 2: p[i] = (uint8_t)(i % 3 == 0 ? 7 : i % 3); break;
        case 3: p[i] = (uint8_t)((i / 16) % 5 == 0 ? next() : (i % 16 < 9 ? 1 : 0)); break;   /* columns */
        case 4: p[i] = (uint8_t)(i < n / 2 ? next() : p[i - n / 2]); break;                   /* far copy */
        case 5: p[i] = (uint8_t)(next() % 4 ? 0 : next()); break;                             /* sparse */
        default: p[i] = (uint8_t)(i % (uint32_t)(k - 5)); break;                              /* period k-5 */
        }
}

static int round_trip(const uint8_t *in, uint32_t n, uint8_t *z, uint8_t *back, uint32_t *tab, const char *what)
{
    uint32_t zn = lzp_compress(in, n, z, tab);
    if (zn > LZP_BOUND(n)) { printf("FAIL %s n=%u: %u bytes past the bound\n", what, n, zn); return 1; }
    memset(back, 0xa5, (size_t)n + LZP_SLACK + GUARD);
    int64_t got = lzp_decompress(z, zn, back, n);
    if (got != (int64_t)n || memcmp(back, in, n) != 0) { printf("FAIL %s n=%u: decoded %lld bytes, not the input\n", what, n, (long long)got); return 1; }
    for (uint32_t i = n + LZP_SLACK; i < n + LZP_SLACK + GUARD; ++i)
        if (back[i] != 0xa5) { printf("FAIL %s n=%u: a write past the slack\n", what, n); return 1; }
    if (n > 0 && lzp_decompress(z, zn, back, n - 1) != -1) { printf("FAIL %s n=%u: a cap of n-1 accepted\n", what, n); return 1; }
    return 0;
}

int main(void)
{
    const uint32_t big = 300000;
    uint8_t *in = malloc(big), *z = malloc(LZP_BOUND(big)), *back = malloc(big + LZP_SLACK + GUARD);
    uint32_t *tab = malloc(LZP_TABLE_BYTES);
    int bad = 0, trips = 0, refused = 0;
    char what[64];

    for (int k = 0; k < 26; ++k)
    {
        for (uint32_t n = 0; n <= 300; ++n)
        {
            fill(in, n, k);
            snprintf(what, sizeof what, "shape %d", k);
            bad += round_trip(in, n, z, back, tab, what);
            ++trips;
        }
        const uint32_t longs[] = {4096, 65535, 65536, 70000, 196626, big};
        for (size_t j = 0; j < sizeof longs / sizeof *longs; ++j)
        {
            fill(in, longs[j], k);
            snprintf(what, sizeof what, "shape %d", k);
            bad += round_trip(in, longs[j], z, back, tab, what);
            ++trips;
        }
    }
    /* a match exactly 65535 back, and one 65536 back (out of reach) */
    for (uint32_t d = 65534; d <= 65537; ++d)
    {
        fill(in, 2 * d + 64, 1);
        memcpy(in + d, in, 64);
        snprintf(what, sizeof what, "distance %u", d);
        bad += round_trip(in, 2 * d + 64, z, back, tab, what);
        ++trips;
    }

    /* damaged streams: refused, or decoded within cap and the slack */
    for (int k = 1; k < 8; ++k)
    {
        const uint32_t n = 3000;
        fill(in, n, k);
        uint32_t zn = lzp_compress(in, n, z, tab);
        for (uint32_t cut = 0; cut < zn; ++cut)
        {
            int64_t got = lzp_decompress(z, cut, back, n);
            if (got == (int64_t)n && memcmp(back, in, n) == 0)
            {
                printf("FAIL shape %d: a stream cut to %u of %u bytes decodes whole\n", k, cut, zn);
                ++bad;
            }
            refused += got != (int64_t)n;
        }
        uint8_t *dmg = malloc(zn);
        for (int t = 0; t < 2000; ++t)
        {
            memcpy(dmg, z, zn);
            for (int f = 0; f < 1 + t % 3; ++f) dmg[next() % zn] ^= (uint8_t)(1 + next() % 255);
            memset(back, 0xa5, n + LZP_SLACK + GUARD);
            int64_t got = lzp_decompress(dmg, zn, back, n);
            if (got > (int64_t)n) { printf("FAIL shape %d: a damaged stream decoded past its cap\n", k); ++bad; }
            for (uint32_t i = n + LZP_SLACK; i < n + LZP_SLACK + GUARD; ++i)
                if (back[i] != 0xa5) { printf("FAIL shape %d: a damaged stream wrote past the slack\n", k); ++bad; break; }
            refused += got < 0;
        }
        free(dmg);
    }

    printf("%s lzpack: %d round trips, %d damaged streams refused%s\n", bad ? "FAIL" : "PASS", trips, refused, bad ? "" : ", none decoded past its buffer");
    free(in);
    free(z);
    free(back);
    free(tab);
    return bad != 0;
}
