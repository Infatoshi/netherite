/* rk_15_light_combine_pack.c - compute core of render-opt kernel 15_light_combine_pack.
 * getCombinedLight (pure tail) copied VERBATIM from candidate.c; only main()/stdio removed.
 * Shifts done in uint32_t then reinterpreted so Java's two's-complement <<-wrap is preserved. */
#include "rk.h"

int32_t rk_light_combine_pack(int32_t sky, int32_t block, int32_t override_value) {
    int32_t i = sky, j = block, lightValue = override_value;
    if (j < lightValue) {
        j = lightValue;
    }
    uint32_t packed = ((uint32_t)i << 20) | ((uint32_t)j << 4);
    return (int32_t)packed;
}
