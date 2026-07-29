/* rk_13_ao_pack_helpers.c - compute core of render-opt kernel 13_ao_pack_helpers.
 * getAoBrightness / getVertexBrightness copied VERBATIM from candidate.c; only main()/stdio
 * removed. See rk.h. Build with -ffp-contract=off. */
#include "rk.h"

int32_t rk_ao_get_ao_brightness(int32_t br1, int32_t br2, int32_t br3, int32_t br4) {
    if (br1 == 0) br1 = br4;
    if (br2 == 0) br2 = br4;
    if (br3 == 0) br3 = br4;
    uint32_t sum = (uint32_t)br1 + (uint32_t)br2 + (uint32_t)br3 + (uint32_t)br4;
    int32_t shifted = (int32_t)sum >> 2;   /* arithmetic shift, matches Java signed >> */
    return shifted & 16711935;
}

int32_t rk_ao_get_vertex_brightness(int32_t p1, int32_t p2, int32_t p3, int32_t p4,
                                    float f5, float f6, float f7, float f8) {
    int32_t i = (int32_t)((float)((p1 >> 16) & 255) * f5 + (float)((p2 >> 16) & 255) * f6
                        + (float)((p3 >> 16) & 255) * f7 + (float)((p4 >> 16) & 255) * f8) & 255;
    int32_t j = (int32_t)((float)(p1 & 255) * f5 + (float)(p2 & 255) * f6
                        + (float)(p3 & 255) * f7 + (float)(p4 & 255) * f8) & 255;
    return i << 16 | j;
}
