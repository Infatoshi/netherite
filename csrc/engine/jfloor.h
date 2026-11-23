/* MathHelper.floor_double and floor_float, the only copies, and the Java
 * casts under them. jmath.h includes this; a file that cannot take jmath.h
 * (the render mesher declares its own mh_sin) includes it alone. */
#ifndef NETHERITE_JFLOOR_H
#define NETHERITE_JFLOOR_H

#include <stdint.h>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif

/* Java's float-to-int cast: NaN is 0, out of range saturates. */
static inline int32_t j_f2i(float f)
{
#if defined(__SSE2__)
    /* cvttss2si truncates, and gives INT32_MIN for NaN and whatever is out
     * of range (and for -2^31 itself, Java's answer there too): any other
     * answer is Java's */
    int32_t i = _mm_cvttss_si32(_mm_set_ss(f));
    if (__builtin_expect(i != INT32_MIN, 1)) return i;
#endif
    if (f != f) return 0;
    if (f >= 2147483647.0f) return INT32_MAX;
    if (f <= -2147483648.0f) return INT32_MIN;
    return (int32_t)f;
}

/* Java's double-to-int cast: NaN is 0, out of range saturates. */
static inline int32_t j_d2i(double d)
{
#if defined(__SSE2__)
    /* cvttsd2si, as j_f2i's cvttss2si */
    int32_t i = _mm_cvttsd_si32(_mm_set_sd(d));
    if (__builtin_expect(i != INT32_MIN, 1)) return i;
#endif
    if (d != d) return 0;
    if (d >= 2147483647.0) return INT32_MAX;
    if (d <= -2147483648.0) return INT32_MIN;
    return (int32_t)d;
}

/* MathHelper.floor_double, the one copy: (int)d saturates as Java's cast
 * does (NaN 0, huge values the int bounds), and the minus one wraps as Java's
 * int arithmetic does, so -Infinity and anything under -2^31 give
 * Integer.MAX_VALUE. A placement facing at yaw 3e11 or -3e11 takes & 3 of
 * that. */
static inline int mh_floor(double d)
{
    int32_t i = j_d2i(d);
    return d < (double)i ? (int32_t)((uint32_t)i - 1u) : i;
}

/* MathHelper.floor_float, the same over float. */
static inline int mh_floor_float(float f)
{
    int32_t i = j_f2i(f);
    return f < (float)i ? (int32_t)((uint32_t)i - 1u) : i;
}

#endif
