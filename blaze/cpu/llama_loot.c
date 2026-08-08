#include "../core/mc_rng.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
    for (int64_t row = 0; row < 96; ++row) {
        uint64_t seed = UINT64_C(0x3456789abcdef) + row * 104729;
        JavaRandom random;
        int looting = (int)(row % 4);
        jrand_set(&random, seed);
        (void)jrand_int_bound(&random, 1);
        int leather = jrand_int_bound(&random, 3);
        if (looting > 0) {
            float scaled = (float)looting * jrand_float(&random);
            leather += (int)(scaled + 0.5F);
        }
        printf("%" PRId64 ",%d,%d,%016" PRIx64 "\n",
               row, looting, leather, (uint64_t)jrand_long(&random));
    }
    return 0;
}
