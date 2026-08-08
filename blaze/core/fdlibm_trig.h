#ifndef MC_FDLIBM_TRIG_H
#define MC_FDLIBM_TRIG_H

#include <math.h>
#include <stdint.h>

/*
 * Reduced-domain fdlibm sine/cosine used by java.lang.StrictMath.  Entity
 * steering passes a float atan2 result, so the input is always in [-pi, pi].
 * libc sin/cos differ from the Java result by one ULP for some such inputs.
 *
 * The polynomial and reduction constants below derive from fdlibm, whose
 * original notice is preserved:
 *
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 * Developed at SunSoft, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this software is freely
 * granted, provided that this notice is preserved.
 */

static inline int32_t mc_fdlibm_high_word(double value) {
    union { double value; uint64_t bits; } word = { value };
    return (int32_t)(word.bits >> 32);
}

static inline double mc_fdlibm_kernel_sin(
        double x, double y, int tail_nonzero) {
    static const double half = 5.00000000000000000000e-01;
    static const double s1 = -1.66666666666666324348e-01;
    static const double s2 = 8.33333333332248946124e-03;
    static const double s3 = -1.98412698298579493134e-04;
    static const double s4 = 2.75573137070700676789e-06;
    static const double s5 = -2.50507602534068634195e-08;
    static const double s6 = 1.58969099521155010221e-10;
    int32_t ix = mc_fdlibm_high_word(x) & 0x7fffffff;
    double z, r, v;
    if (ix < 0x3e400000 && (int)x == 0) return x;
    z = x * x;
    v = z * x;
    r = s2 + z * (s3 + z * (s4 + z * (s5 + z * s6)));
    if (!tail_nonzero) return x + v * (s1 + z * r);
    return x - ((z * (half * y - v * r) - y) - v * s1);
}

static inline double mc_fdlibm_kernel_cos(double x, double y) {
    static const double one = 1.00000000000000000000e+00;
    static const double c1 = 4.16666666666666019037e-02;
    static const double c2 = -1.38888888888741095749e-03;
    static const double c3 = 2.48015872894767294178e-05;
    static const double c4 = -2.75573143513906633035e-07;
    static const double c5 = 2.08757232129817482790e-09;
    static const double c6 = -1.13596475577881948265e-11;
    union { double value; uint64_t bits; } qword;
    int32_t ix = mc_fdlibm_high_word(x) & 0x7fffffff;
    double a, hz, z, r, qx;
    if (ix < 0x3e400000 && (int)x == 0) return one;
    z = x * x;
    r = z * (c1 + z * (c2 + z * (c3 + z * (c4 + z * (c5 + z * c6)))));
    if (ix < 0x3fd33333)
        return one - (0.5 * z - (z * r - x * y));
    if (ix > 0x3fe90000) {
        qx = 0.28125;
    } else {
        qword.bits = (uint64_t)(uint32_t)(ix - 0x00200000) << 32;
        qx = qword.value;
    }
    hz = 0.5 * z - qx;
    a = one - qx;
    return a - (hz - (z * r - x * y));
}

static inline int mc_fdlibm_rem_pio2_small(
        double x, double *head, double *tail) {
    static const double invpio2 = 6.36619772367581382433e-01;
    static const double pio2_1 = 1.57079632673412561417e+00;
    static const double pio2_1t = 6.07710050650619224932e-11;
    static const double pio2_2 = 6.07710050630396597660e-11;
    static const double pio2_2t = 2.02226624879595063154e-21;
    int32_t hx = mc_fdlibm_high_word(x);
    int32_t ix = hx & 0x7fffffff;
    double z, r, w, fn, magnitude;
    int n;
    if (ix <= 0x3fe921fb) {
        *head = x;
        *tail = 0.0;
        return 0;
    }
    if (ix < 0x4002d97c) {
        if (hx > 0) {
            z = x - pio2_1;
            if (ix != 0x3ff921fb) {
                *head = z - pio2_1t;
                *tail = (z - *head) - pio2_1t;
            } else {
                z -= pio2_2;
                *head = z - pio2_2t;
                *tail = (z - *head) - pio2_2t;
            }
            return 1;
        }
        z = x + pio2_1;
        if (ix != 0x3ff921fb) {
            *head = z + pio2_1t;
            *tail = (z - *head) + pio2_1t;
        } else {
            z += pio2_2;
            *head = z + pio2_2t;
            *tail = (z - *head) + pio2_2t;
        }
        return -1;
    }
    magnitude = fabs(x);
    n = (int)(magnitude * invpio2 + 0.5);
    fn = (double)n;
    r = magnitude - fn * pio2_1;
    w = fn * pio2_1t;
    *head = r - w;
    *tail = (r - *head) - w;
    if (hx < 0) {
        *head = -*head;
        *tail = -*tail;
        return -n;
    }
    return n;
}

static inline double mc_strict_sin_small(double x) {
    double head, tail;
    int quadrant = mc_fdlibm_rem_pio2_small(x, &head, &tail);
    switch (quadrant & 3) {
        case 0: return mc_fdlibm_kernel_sin(head, tail, 1);
        case 1: return mc_fdlibm_kernel_cos(head, tail);
        case 2: return -mc_fdlibm_kernel_sin(head, tail, 1);
        default: return -mc_fdlibm_kernel_cos(head, tail);
    }
}

static inline double mc_strict_cos_small(double x) {
    double head, tail;
    int quadrant = mc_fdlibm_rem_pio2_small(x, &head, &tail);
    switch (quadrant & 3) {
        case 0: return mc_fdlibm_kernel_cos(head, tail);
        case 1: return -mc_fdlibm_kernel_sin(head, tail, 1);
        case 2: return -mc_fdlibm_kernel_cos(head, tail);
        default: return mc_fdlibm_kernel_sin(head, tail, 1);
    }
}

/* Java 8 HotSpot's x86 Math intrinsics use the x87 instruction directly in
 * [-pi/4, pi/4], then fall back to the fdlibm runtime outside that interval.
 * Math is permitted to differ from StrictMath by an ULP, and the difference
 * compounds in homing projectile trajectories. */
static inline double mc_java_math_sin_small(double x) {
#if defined(__i386__) || defined(__x86_64__)
    if (fabs(x) <= 0.7853981633974483) {
        double result;
        __asm__ volatile ("fldl %1; fsin; fstpl %0"
            : "=m" (result) : "m" (x) : "st");
        return result;
    }
#endif
    return mc_strict_sin_small(x);
}

static inline double mc_java_math_cos_small(double x) {
#if defined(__i386__) || defined(__x86_64__)
    if (fabs(x) <= 0.7853981633974483) {
        double result;
        __asm__ volatile ("fldl %1; fcos; fstpl %0"
            : "=m" (result) : "m" (x) : "st");
        return result;
    }
#endif
    return mc_strict_cos_small(x);
}

#endif
