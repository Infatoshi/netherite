/* WorldClient.rand from the recording's d.cw; see cwrand.h. */
#include "cwrand.h"
#include "tape.h"

#include <stdlib.h>

#define CW_MASK ((1ULL << 48) - 1)
#define CW_MUL 0x5DEECE66DULL
#define CW_ADD 0xBULL

/* the seed K2 <= max_steps steps past s1 whose digest is cw2 */
static int cw_reaches(uint64_t s1, int32_t lcg, int64_t cw2, int max_steps)
{
    uint64_t s = s1;
    for (int k = 0; k <= max_steps; ++k)
    {
        if (cwrand_digest(s, lcg) == cw2) return 1;
        s = (s * CW_MUL + CW_ADD) & CW_MASK;
    }
    return 0;
}

int cwrand_recover(int64_t cw0, int64_t cw1, int64_t cw2, int max_steps, uint64_t *seed, int32_t *lcg,
                   int *steps)
{
    int64_t diff = cw1 - cw0;
    if (diff == 0 || diff % 31 != 0) return 0;
    uint64_t d = (uint64_t)(diff / 31) & CW_MASK;
    /* the seeds whose LCG value fits an int */
    int64_t lo = (cw0 - (int64_t)INT32_MAX + 30) / 31, hi = (cw0 - (int64_t)INT32_MIN) / 31;
    if (lo < 0) lo = 0;
    if (hi > (int64_t)CW_MASK) hi = (int64_t)CW_MASK;
    if (lo > hi) return 0;

    /* step^K(s) = a_K s + c_K; (a_K - 1) s0 = d - c_K (mod 2^48) */
    uint64_t a = 1, c = 0;
    int found = 0;
    for (int k = 1; k <= max_steps; ++k)
    {
        a = (a * CW_MUL) & CW_MASK;
        c = (c * CW_MUL + CW_ADD) & CW_MASK;
        uint64_t am1 = (a - 1) & CW_MASK, rhs = (d - c) & CW_MASK;
        if (am1 == 0) continue;
        int v = __builtin_ctzll(am1);
        if (rhs & ((1ULL << v) - 1)) continue;
        int bits = 48 - v;
        uint64_t odd = am1 >> v, inv = odd;
        for (int i = 0; i < 6; ++i) inv *= 2 - odd * inv;
        uint64_t s = ((rhs >> v) * inv) & ((1ULL << bits) - 1);
        /* s + j 2^bits inside [lo, hi] */
        int64_t j0 = (int64_t)s >= lo ? 0 : (lo - (int64_t)s + (1LL << bits) - 1) >> bits;
        for (int64_t j = j0; (int64_t)s + (j << bits) <= hi; ++j)
        {
            uint64_t s0 = s + ((uint64_t)j << bits);
            int32_t l = (int32_t)(cw0 - (int64_t)s0 * 31);
            uint64_t s1 = (a * s0 + c) & CW_MASK;
            if (!cw_reaches(s1, l, cw2, max_steps)) continue;
            if (found++) return 0;
            *seed = s0;
            *lcg = l;
            *steps = k;
        }
    }
    return found == 1;
}

/* The rows' d.cw at ticks t0 - 1 .. t0 + 2 (have[i] for tick t0 - 1 + i). */
static void cw_rows(const char *path, int64_t t0, int64_t cw[4], int have[4])
{
    struct tape tp;
    const struct jval *row;
    if (!tape_open(&tp, path)) return;
    while (tape_next(&tp, &row) == 1)
    {
        int64_t t;
        if (!json_int(json_get(row, "t"), &t)) break;
        if (t > t0 + 2) break;
        if (t < t0 - 1) continue;
        const char *v = json_str(json_get(json_get(row, "d"), "cw"));
        if (v == NULL) continue;
        cw[t - t0 + 1] = (int64_t)strtoull(v, NULL, 16);
        have[t - t0 + 1] = 1;
    }
    tape_close(&tp);
}

void cwrand_from_tape(const char *path, int64_t t0, uint64_t *seed, int32_t *lcg, int64_t *from)
{
    int64_t cw[4] = { 0 };
    int have[4] = { 0 }, steps = 0;
    cw_rows(path, t0, cw, have);
    *from = -1;
    for (int i = 0; i < 2 && *from < 0; ++i)
        if (have[i] && have[i + 1] && have[i + 2] &&
            cwrand_recover(cw[i], cw[i + 1], cw[i + 2], 200000, seed, lcg, &steps))
            *from = t0 + i;
}
