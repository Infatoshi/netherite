#include <stdio.h>

#include "spawn_position.h"

int main(void) {
    static const struct {
        i64 seed;
        int x, z, matches;
    } cases[] = {
        {(i64)-9055566058453653051LL, 44, -152, 53},
        {0, 44, 176, 78},
        {1, 164, 256, 115},
        {-1, -28, -112, 65},
        {(i64)1234567890123456789LL, 140, -60, 18},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        McSpawnBiomePosition got = mc_spawn_biome_position(cases[i].seed);
        if (!got.found || got.x != cases[i].x || got.z != cases[i].z
                || got.matches != cases[i].matches) {
            fprintf(stderr,
                    "spawn mismatch seed=%lld: found=%d x=%d z=%d matches=%d\n",
                    (long long)cases[i].seed, got.found, got.x, got.z,
                    got.matches);
            return 1;
        }
    }
    puts("spawn position Java golden: PASS");
    return 0;
}
