/* rk_08_mipmap_gen_chain.c - compute core of render-opt kernel 08_mipmap_gen_chain.
 * blend_colors/blend_color_component/get_color_gamma/init_gammas and generateMipmapData's
 * loop copied VERBATIM from candidate.c; only main()/stdio removed. The printf output of the
 * original process() is replaced by storing the level pointers/lengths into RkMipChain (a
 * harness change only; the blend arithmetic and index math are unchanged).
 * Build with -ffp-contract=off. */
#include "rk.h"
#include <stdlib.h>
#include <math.h>

static float COLOR_GAMMAS[256];
static int   gammas_ready = 0;

static void init_gammas(void) {
    for (int i = 0; i < 256; ++i) {
        COLOR_GAMMAS[i] = (float)pow((double)((float)i / 255.0f), 2.2);
    }
}

static float get_color_gamma(int p) {
    return COLOR_GAMMAS[p & 255];
}

static int blend_color_component(int c0, int c1, int c2, int c3, int shift) {
    float f  = get_color_gamma(c0 >> shift);
    float f1 = get_color_gamma(c1 >> shift);
    float f2 = get_color_gamma(c2 >> shift);
    float f3 = get_color_gamma(c3 >> shift);
    float f4 = (float)((double)((float)pow((double)(f + f1 + f2 + f3) * 0.25, 0.45454545454545453)));
    return (int)((double)f4 * 255.0);
}

static int blend_colors(int c0, int c1, int c2, int c3, int has_transparency) {
    if (has_transparency) {
        int buf[4];
        buf[0] = c0; buf[1] = c1; buf[2] = c2; buf[3] = c3;
        float f = 0.0f, f1 = 0.0f, f2 = 0.0f, f3 = 0.0f;
        for (int i1 = 0; i1 < 4; ++i1) {
            if (buf[i1] >> 24 != 0) {
                f  += get_color_gamma(buf[i1] >> 24);
                f1 += get_color_gamma(buf[i1] >> 16);
                f2 += get_color_gamma(buf[i1] >> 8);
                f3 += get_color_gamma(buf[i1] >> 0);
            }
        }
        f = f / 4.0f; f1 = f1 / 4.0f; f2 = f2 / 4.0f; f3 = f3 / 4.0f;
        int i2 = (int)(pow((double)f, 0.45454545454545453) * 255.0);
        int j1 = (int)(pow((double)f1, 0.45454545454545453) * 255.0);
        int k1 = (int)(pow((double)f2, 0.45454545454545453) * 255.0);
        int l1 = (int)(pow((double)f3, 0.45454545454545453) * 255.0);
        if (i2 < 96) i2 = 0;
        uint32_t out = ((uint32_t)i2 << 24) | ((uint32_t)j1 << 16)
                     | ((uint32_t)k1 << 8) | (uint32_t)l1;
        return (int)out;
    } else {
        int i = blend_color_component(c0, c1, c2, c3, 24);
        int j = blend_color_component(c0, c1, c2, c3, 16);
        int k = blend_color_component(c0, c1, c2, c3, 8);
        int l = blend_color_component(c0, c1, c2, c3, 0);
        uint32_t out = ((uint32_t)i << 24) | ((uint32_t)j << 16)
                     | ((uint32_t)k << 8) | (uint32_t)l;
        return (int)out;
    }
}

/* Port of generateMipmapData: verbatim chain structure; stores results into `out`.
 * level[0] aliases the caller-owned base; levels 1..max_level are freshly allocated. */
void rk_mipmap_gen_chain(int max_level, int width,
                         int32_t *base, int n_base, RkMipChain *out) {
    if (!gammas_ready) { init_gammas(); gammas_ready = 1; }

    int **levels = malloc(sizeof(int *) * (max_level + 1));
    int *lengths = malloc(sizeof(int) * (max_level + 1));
    levels[0] = base;
    lengths[0] = n_base;

    if (max_level > 0) {
        int flag = 0;
        /* verbatim: loop bound is the outer array length (max_level+1), not n_base */
        for (int i = 0; i < max_level + 1; ++i) {
            if (base[i] >> 24 == 0) { flag = 1; break; }
        }
        for (int l1 = 1; l1 <= max_level; ++l1) {
            int *aint1 = levels[l1 - 1];
            int len1 = lengths[l1 - 1];
            int len2 = len1 >> 2;
            int *aint2 = calloc(len2 > 0 ? len2 : 1, sizeof(int));
            int j = width >> l1;
            if (j > 0) {
                int k = len2 / j;
                int l = j << 1;
                for (int i1 = 0; i1 < j; ++i1) {
                    for (int j1 = 0; j1 < k; ++j1) {
                        int k1 = 2 * (i1 + j1 * l);
                        aint2[i1 + j1 * j] = blend_colors(aint1[k1 + 0], aint1[k1 + 1],
                                                          aint1[k1 + 0 + l], aint1[k1 + 1 + l], flag);
                    }
                }
            }
            levels[l1] = aint2;
            lengths[l1] = len2;
        }
    }

    out->max_level = max_level;
    for (int lvl = 0; lvl <= max_level; ++lvl) {
        out->level[lvl] = levels[lvl];
        out->length[lvl] = lengths[lvl];
    }
    free(levels);
    free(lengths);
}

void rk_mipmap_chain_free(RkMipChain *chain) {
    for (int lvl = 1; lvl <= chain->max_level; ++lvl) {
        free(chain->level[lvl]);
        chain->level[lvl] = NULL;
    }
}
