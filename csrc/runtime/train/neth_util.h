/* The trainer's small helpers (ntloop's modules share them): the clocks, FNV-1a, the CUDA check, CPython's float repr,
 * the host stand-in's xorshift and a whole file read. Header-only: every function is static inline. */
#ifndef NETHERITE_RUNTIME_TRAIN_NETH_UTIL_H
#define NETHERITE_RUNTIME_TRAIN_NETH_UTIL_H

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cuda_runtime.h>

static inline double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + 1e-9 * (double)t.tv_nsec;
}

static inline double wall_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    return (double)t.tv_sec + 1e-9 * (double)t.tv_nsec;
}

static inline uint64_t fnv(uint64_t h, const void *p, size_t n)
{
    const uint8_t *b = p;
    for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 0x100000001b3ull;
    return h;
}

/* a CUDA call that must succeed: a failure prints where and exits (the hot path's calls; setup returns errors) */
#define CK(x)                                                                                  \
    do                                                                                         \
    {                                                                                          \
        cudaError_t e_ = (x);                                                                  \
        if (e_ != cudaSuccess)                                                                 \
        {                                                                                      \
            printf("FAIL %s:%d %s: %s\n", __FILE__, __LINE__, #x, cudaGetErrorString(e_));     \
            exit(1);                                                                           \
        }                                                                                      \
    } while (0)

/* JSON as CPython's json.dumps writes it (", " and ": ", floats by repr): repr(float), the shortest digits that read
 * back to x, ".0" on an integral one */
static inline void jnum(FILE *f, double x)
{
    if (isnan(x)) { fputs("NaN", f); return; }
    if (isinf(x)) { fputs(x > 0 ? "Infinity" : "-Infinity", f); return; }
    char s[40];
    for (int p = 1; p <= 17; ++p)
    {
        snprintf(s, sizeof s, "%.*g", p, x);
        if (strtod(s, NULL) == x) break;
    }
    /* repr switches to exponents at 1e16 and below 1e-4, as %g does; %g's "1e+16" is repr's too */
    fputs(s, f);
    if (!strpbrk(s, ".en")) fputs(".0", f);
}

/* vec_bench's host stand-in's xorshift (--ab's A) and the learner's epoch order */
static inline uint64_t xs(uint64_t *s)
{
    uint64_t x = *s;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return *s = x;
}

/* a whole file, NUL-terminated past its length (NULL if it cannot be read) */
static inline void *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) return fclose(f), NULL;
    /* one byte more, a NUL past the end: a text file's bytes read as a string (strstr) stay inside the buffer */
    char *d = malloc((size_t)n + 1);
    size_t got = d ? fread(d, 1, (size_t)n, f) : 0;
    fclose(f);
    if (!d || got != (size_t)n) return free(d), NULL;
    d[n] = 0;
    *len = (size_t)n;
    return d;
}

#endif
