/* CANDIDATE: C port of MC smallestEncompassingPowerOfTwo + log2DeBruijn. Must BITWISE-match golden.
 * Reads one int per line; prints "smallestEncompassingPowerOfTwo log2DeBruijn" per line.
 * All integer ops done in uint32/int64 to match Java two's-complement wrap exactly
 * (Java int arithmetic wraps; C signed overflow is UB, so the additive ops use uint32_t). */
#include <stdio.h>
#include <stdint.h>

static const int MULTIPLY_DE_BRUIJN_BIT_POSITION[32] = {
    0, 1, 28, 2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4, 8,
    31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6, 11, 5, 10, 9
};

static int32_t smallestEncompassingPowerOfTwo(int32_t value) {
    int32_t i = (int32_t)((uint32_t)value - 1u); /* value - 1 (wrap) */
    i = i | (i >> 1);                            /* Java >> on int is arithmetic */
    i = i | (i >> 2);
    i = i | (i >> 4);
    i = i | (i >> 8);
    i = i | (i >> 16);
    return (int32_t)((uint32_t)i + 1u);          /* i + 1 (wrap) */
}

static int isPowerOfTwo(int32_t value) {
    return value != 0 && (value & (int32_t)((uint32_t)value - 1u)) == 0;
}

static int32_t log2DeBruijn(int32_t value) {
    value = isPowerOfTwo(value) ? value : smallestEncompassingPowerOfTwo(value);
    int idx = (int)(((int64_t)value * 125613361LL) >> 27) & 31;
    return MULTIPLY_DE_BRUIJN_BIT_POSITION[idx];
}

int main(void) {
    char line[256];
    while (fgets(line, sizeof line, stdin)) {
        long v;
        if (sscanf(line, "%ld", &v) != 1) continue;
        int32_t x = (int32_t)v;
        printf("%d %d\n", smallestEncompassingPowerOfTwo(x), log2DeBruijn(x));
    }
    return 0;
}
