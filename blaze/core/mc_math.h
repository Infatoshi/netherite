/* mc_math.h - exact C port of MC 1.11.2 MathHelper table-based trig + floor
 * (net/minecraft/util/math/MathHelper.java). MC's sin/cos are NOT libm: they index a 65536-entry
 * SIN_TABLE built at class init as (float)Math.sin((double)i * PI * 2 / 65536). Worldgen features
 * (e.g. WorldGenMinable) depend on these exact values, so libm sinf will NOT match the golden.
 *
 * The lookup index math is done in float then narrowed to int with Java's float->int narrowing
 * (JLS 5.1.3: NaN->0, saturate at +/-2^31, else truncate toward zero). Build C with
 * -ffp-contract=off / CUDA with --fmad=false so the index float-math rounds like the JVM.
 *
 * SIN_TABLE is built on the HOST (libm sin) and the SAME bytes are used by both the CPU and the
 * CUDA path (the .cu driver copies the host table to the device), so CPU==CUDA holds by
 * construction and Java==CPU holds because (float)sin(...) matches Math.sin after the float cast
 * (the render-opt 01_sin_cos_table kernel verified this on this platform). */
#ifndef MC_MATH_H
#define MC_MATH_H

#include <math.h>
#include "mc.h"

#ifndef MC_PI
#define MC_PI 3.14159265358979323846
#endif

#define MC_SIN_TABLE_LEN 65536

typedef struct { float sin_table[MC_SIN_TABLE_LEN]; } McSinTable;

/* MathHelper SIN_TABLE static init: SIN_TABLE[i] = (float)Math.sin((double)i*Math.PI*2/65536). */
MC_HD static inline void mc_sin_table_init(McSinTable *t) {
    for (int i = 0; i < MC_SIN_TABLE_LEN; ++i)
        t->sin_table[i] = (float)sin((double)i * MC_PI * 2.0 / 65536.0);
}

/* Java float->int narrowing (JLS 5.1.3). C's plain (int) cast is UB out of range; emulate. */
MC_HD static inline i32 mc_f2i(float f) {
    if (f != f) return 0;                              /* NaN */
    if (f >=  2147483648.0f) return  2147483647;       /* >= 2^31 */
    if (f <= -2147483648.0f) return (i32)(-2147483647 - 1);
    return (i32)f;
}

/* MathHelper.sin / cos: sine LUT, cos = sin offset by a quarter turn (16384). */
MC_HD static inline float mc_sin(const McSinTable *t, float value) {
    return t->sin_table[mc_f2i(value * 10430.378f) & 65535];
}
MC_HD static inline float mc_cos(const McSinTable *t, float value) {
    return t->sin_table[mc_f2i(value * 10430.378f + 16384.0f) & 65535];
}

/* MathHelper.floor(double): greatest int <= value (toward -inf). (int)value is the Java
 * double->int narrowing (mc_d2i); the < check then steps down for negative fractions. */
MC_HD static inline int mc_floor(double value) {
    int i = mc_d2i(value);
    return value < (double)i ? i - 1 : i;
}

/* MathHelper.floor(float) overload. */
MC_HD static inline int mc_floorf(float value) {
    int i = (int)mc_f2i(value);
    return value < (float)i ? i - 1 : i;
}

#endif /* MC_MATH_H */
