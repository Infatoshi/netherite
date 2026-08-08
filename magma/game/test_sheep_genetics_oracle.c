#include "game/mob_live.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s SEED48_A SEED48_B\n", argv[0]);
        return 2;
    }
    char *end_a = NULL, *end_b = NULL;
    uint64_t seeds[2] = {
        strtoull(argv[1], &end_a, 0), strtoull(argv[2], &end_b, 0)
    };
    if (!argv[1][0] || !argv[2][0] || !end_a || *end_a
            || !end_b || *end_b || seeds[0] >= (UINT64_C(1) << 48)
            || seeds[1] >= (UINT64_C(1) << 48))
        return 2;

    printf("{\"ok\":true,\"seeds\":[%" PRIu64 ",%" PRIu64 "],\"rows\":[",
        seeds[0], seeds[1]);
    int row = 0;
    for (int seed_index = 0; seed_index < 2; ++seed_index) {
        for (int first = 0; first < 16; ++first) {
            for (int second = 0; second < 16; ++second) {
                JavaRandom random;
                jrand_set_seed48(&random, seeds[seed_index]);
                int fleece = gm_mobs_sheep_child_color(
                    &random, first, second);
                if (fleece < 0) return 1;
                if (row++) putchar(',');
                printf("{\"seed48\":%" PRIu64 ",\"first\":%d,"
                       "\"second\":%d,\"fleece\":%d,"
                       "\"world_seed48\":%" PRIu64 "}",
                    seeds[seed_index], first, second, fleece, random.seed);
            }
        }
    }
    puts("]}");
    return 0;
}
