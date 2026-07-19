/* rk_18_biome_color_blend.c - compute core of render-opt kernel 18_biome_color_blend.
 * blend() copied VERBATIM from candidate.c; only main()/stdio removed. Integer arithmetic. */
#include "rk.h"

int rk_biome_color_blend(const int c[9]) {
    int i = 0, j = 0, k = 0;
    for (int n = 0; n < 9; ++n) {
        int l = c[n];
        i += (l & 16711680) >> 16;   /* R */
        j += (l & 65280) >> 8;       /* G */
        k += l & 255;                /* B */
    }
    /* precedence identical to the verbatim Java: ((x)&255); parens only silence -Wparentheses */
    return (i / 9 & 255) << 16 | (j / 9 & 255) << 8 | (k / 9 & 255);
}
