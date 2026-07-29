/* mc.h - base types + host/device portability for the shared sim core.
 * Compiles under cc/gcc (host only) and nvcc (host + device). No OOP, no STL on the hot path. */
#ifndef MC_H
#define MC_H

#include <stdint.h>

#if defined(__CUDACC__)
#define MC_HD __host__ __device__
#define MC_DEV __device__
/* Compile-speed policy: heavy worldgen compute functions are marked MC_NOINLINE so nvcc's cicc
 * compiles them as separate device functions instead of inlining the whole tree into one giant
 * __global__ (that blowup is superlinear: chunk_provider went from tens of minutes / 6GB+ to
 * ~12s / 0.45GB). Numerics are unchanged, so CPU==CUDA bitwise verification is preserved. Use
 * ONLY on large multi-statement functions; keep tiny hot accessors (cb_get/cb_set) inline. */
#define MC_NOINLINE __noinline__
#else
#define MC_HD
#define MC_DEV
#define MC_NOINLINE
#endif

typedef int8_t   i8;
typedef uint8_t  u8;
typedef int16_t  i16;
typedef uint16_t u16;
typedef int32_t  i32;
typedef uint32_t u32;
typedef int64_t  i64;
typedef uint64_t u64;

/* Java semantics helpers (we match Java's int math where the spec depends on it). */
MC_HD static inline i32 mc_floor_div(i32 a, i32 b) {
    i32 q = a / b;
    if ((a ^ b) < 0 && q * b != a) q--;   /* floor toward -inf, like Java Math.floorDiv */
    return q;
}
MC_HD static inline i32 mc_floor_mod(i32 a, i32 b) {
    i32 r = a % b;
    if (r != 0 && (r ^ b) < 0) r += b;
    return r;
}
/* Java (int) cast on a double: truncate toward zero, NaN -> 0 (C leaves NaN cast undefined). */
MC_HD static inline i32 mc_d2i(double d) {
    if (d != d) return 0;                  /* NaN */
    if (d >=  2147483647.0) return  2147483647;
    if (d <= -2147483648.0) return -2147483648;
    return (i32)d;
}

#endif /* MC_H */
