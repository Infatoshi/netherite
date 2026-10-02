/* java.util.Random, bit for bit. Arithmetic is unsigned where Java relies on
 * two's-complement wraparound, because signed overflow is undefined in C. */
#ifndef NETHERITE_JRAND_H
#define NETHERITE_JRAND_H

#include <stdint.h>

#define JR_MASK ((1ULL << 48) - 1)

typedef struct { uint64_t seed; } jrand;

static inline void jr_seed(jrand *r, int64_t s)
{
    r->seed = ((uint64_t)s ^ 0x5DEECE66DULL) & JR_MASK;
}

static inline int32_t jr_next(jrand *r, int bits)
{
    r->seed = (r->seed * 0x5DEECE66DULL + 0xBULL) & JR_MASK;
    return (int32_t)(uint32_t)(r->seed >> (48 - bits));
}

static inline int32_t jr_int(jrand *r)
{
    return jr_next(r, 32);
}

/* Random.nextInt(n)'s rejection loop, for an n not a power of two */
static int32_t jr_int_n_mod(jrand *r, int32_t n)
{
    int32_t bits, val;
    do
    {
        bits = jr_next(r, 31);
        val = bits % n;
    } while ((int32_t)((uint32_t)bits - (uint32_t)val + (uint32_t)(n - 1)) < 0);
    return val;
}

/* the power-of-two case inline (the loop kept gcc from inlining any of it:
 * a call a draw, and cp_world_tick's thousand cells draw six each) */
static inline int32_t jr_int_n(jrand *r, int32_t n)
{
    if ((n & -n) == n) return (int32_t)(((int64_t)n * (int64_t)jr_next(r, 31)) >> 31);
    return jr_int_n_mod(r, n);
}

static inline int64_t jr_long(jrand *r)
{
    uint64_t hi = (uint64_t)(int64_t)jr_next(r, 32);
    uint64_t lo = (uint64_t)(int64_t)jr_next(r, 32);
    return (int64_t)((hi << 32) + lo);
}

static inline double jr_double(jrand *r)
{
    int64_t hi = jr_next(r, 26), lo = jr_next(r, 27);
    return (double)((hi << 27) + lo) * 0x1.0p-53;
}

static inline float jr_float(jrand *r)
{
    return (float)jr_next(r, 24) / (float)(1 << 24);
}

static inline int jr_bool(jrand *r)
{
    return jr_next(r, 1) != 0;
}

#endif
