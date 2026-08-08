#include "../core/mc_rng.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint64_t double_bits(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

int main(void) {
    for (int64_t seed = 0; seed < 64; ++seed) {
        JavaRandom random;
        double first_health = 15.0 + seed % 17;
        double second_health = 15.0 + seed % 13;
        double first_jump = 0.4 + (seed % 7) * 0.05;
        double second_jump = 0.4 + (seed % 5) * 0.07;
        double first_speed = 0.1 + (seed % 9) * 0.02;
        double second_speed = 0.1 + (seed % 6) * 0.03;
        int first_strength = 1 + (int)(seed % 5);
        int second_strength = 1 + (int)((seed * 3) % 5);
        int first_variant = (int)(seed % 4);
        int second_variant = (int)((seed * 3) % 4);
        jrand_set(&random, UINT64_C(0x123456789abc) + seed * 7919);

        double health = (first_health + second_health + 15.0
            + jrand_int_bound(&random, 8)
            + jrand_int_bound(&random, 9)) / 3.0;
        double jump = (first_jump + second_jump
            + 0.4000000059604645
            + jrand_double(&random) * 0.2
            + jrand_double(&random) * 0.2
            + jrand_double(&random) * 0.2) / 3.0;
        double speed = (first_speed + second_speed
            + (0.44999998807907104
            + jrand_double(&random) * 0.3
            + jrand_double(&random) * 0.3
            + jrand_double(&random) * 0.3) * 0.25) / 3.0;
        int bound = first_strength > second_strength
            ? first_strength : second_strength;
        int strength = jrand_int_bound(&random, bound) + 1;
        if (jrand_float(&random) < 0.03F) ++strength;
        if (strength > 5) strength = 5;
        int variant = jrand_next(&random, 1)
            ? first_variant : second_variant;
        printf("%" PRId64 ",%016" PRIx64 ",%016" PRIx64
               ",%016" PRIx64 ",%d,%d\n",
               seed, double_bits(health), double_bits(jump),
               double_bits(speed), strength, variant);
    }
    return 0;
}
