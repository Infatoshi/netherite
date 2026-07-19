/* CANDIDATE: C port of the pure tail of MC World.getCombinedLight. Must BITWISE-match golden.
 * Input line: "skyLight blockLight override" (signed decimal int32). Output: packed int (decimal).
 *   if (j < lightValue) j = lightValue;  return i << 20 | j << 4;
 * Shifts are done in uint32_t then reinterpreted to int32_t so left-shifts that touch the sign bit
 * wrap exactly like Java (left shift of a negative / large int is well-defined two's-complement in
 * Java but UB on signed in C). The `j < lightValue` compare uses signed int32 like Java. */
#include <stdio.h>
#include <stdint.h>

static int32_t getCombinedLight(int32_t i, int32_t j, int32_t lightValue) {
    if (j < lightValue) {
        j = lightValue;
    }
    uint32_t packed = ((uint32_t)i << 20) | ((uint32_t)j << 4);
    return (int32_t)packed;
}

int main(void) {
    char line[256];
    while (fgets(line, sizeof line, stdin)) {
        long sky, block, override;
        if (sscanf(line, "%ld %ld %ld", &sky, &block, &override) != 3) continue;
        printf("%d\n", getCombinedLight((int32_t)sky, (int32_t)block, (int32_t)override));
    }
    return 0;
}
