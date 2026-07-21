/* Shared deterministic rasterizer-verify scene. Consumed identically by the GL
 * golden (gl_golden.c) and the C candidate (c_candidate.c) so the ONLY variable
 * is the rasterizer implementation (real Mesa GL vs our cr_raster_cpu).
 *
 * The kernel under test is the triangle->pixel step. Inputs are given in CLIP
 * space (x,y,z,w) so the transform stage is out of scope; both paths apply the
 * SAME OpenGL viewport mapping. Per-vertex attribute is a scalar brightness
 * (grayscale light multiplier), which both paths interpolate perspective-correctly,
 * plus a uv into a shared nearest-sampled atlas. Shading = texel * light
 * (GL_MODULATE with glColor(light,light,light); our cr_shade with tint=white,
 * ao=1). No blend, no MSAA, no dither, no mipmap, depth func LESS. */
#ifndef MAGMA_VERIFY_SCENE_H
#define MAGMA_VERIFY_SCENE_H
#include <stdint.h>
#include <math.h>

#define SCN_W 256
#define SCN_H 256
#define ATLAS_W 16
#define ATLAS_H 16

typedef struct { float x, y, z, w; float u, v; float light; } ScnVert;

/* Each consecutive triple of verts is one triangle. Clip coords chosen so ndc is
 * in range; some verts use w != 1 to force perspective-correct interpolation. */
static const ScnVert SCN_VERTS[] = {
    /* Tri 0+1: background quad at far depth (ndc z = 0.6), brightness gradient,
     * uv spans the whole atlas. w=1 (affine). */
    { -0.9f, -0.9f, 0.6f, 1.f,  0.f, 0.f, 0.40f },
    {  0.9f, -0.9f, 0.6f, 1.f,  1.f, 0.f, 1.00f },
    {  0.9f,  0.9f, 0.6f, 1.f,  1.f, 1.f, 0.70f },
    { -0.9f, -0.9f, 0.6f, 1.f,  0.f, 0.f, 0.40f },
    {  0.9f,  0.9f, 0.6f, 1.f,  1.f, 1.f, 0.70f },
    { -0.9f,  0.9f, 0.6f, 1.f,  0.f, 1.f, 0.55f },

    /* Tri 2: foreground triangle at near depth (ndc z = 0.2), overlaps the quad
     * center to exercise the z-buffer (must occlude the far quad). */
    { -0.5f, -0.6f, 0.2f, 1.f,  0.f, 0.f, 1.00f },
    {  0.6f, -0.2f, 0.2f, 1.f,  1.f, 0.f, 0.85f },
    { -0.1f,  0.7f, 0.2f, 1.f,  0.5f,1.f, 0.60f },

    /* Tri 3: strongly perspective triangle (w=3 on one vertex) to make the
     * perspective-correct uv/light recovery bite. ndc = clip.xyz / w. */
    {  0.30f, -0.85f, 0.35f, 1.f,  0.f, 0.f, 1.00f },
    {  2.70f, -0.30f, 1.05f, 3.f,  1.f, 0.f, 0.90f }, /* ndc (0.9,-0.1,0.35) */
    {  0.45f,  0.85f, 0.35f, 1.f,  0.f, 1.f, 0.50f },
};
#define SCN_NVERTS ((int)(sizeof(SCN_VERTS)/sizeof(SCN_VERTS[0])))

/* ---- Layer test geometry (all affine, w=1). Drawn AFTER the SOLID group, in
 * this order: CUTOUT (alpha test) -> TRANSLUCENT (blend) -> mipped SOLID. Each
 * sits over the SOLID background so the layer behaviour is visible in the diff. */

/* CUTOUT quad, top-right, nearer (ndc z=0.4) than the background (0.6). Uses the
 * holed cutout texture: alpha-0 texels are discarded, showing the background. */
static const ScnVert SCN_CUT_VERTS[] = {
    { 0.15f, 0.15f, 0.4f, 1.f, 0.f, 0.f, 1.00f },
    { 0.85f, 0.15f, 0.4f, 1.f, 1.f, 0.f, 1.00f },
    { 0.85f, 0.85f, 0.4f, 1.f, 1.f, 1.f, 1.00f },
    { 0.15f, 0.15f, 0.4f, 1.f, 0.f, 0.f, 1.00f },
    { 0.85f, 0.85f, 0.4f, 1.f, 1.f, 1.f, 1.00f },
    { 0.15f, 0.85f, 0.4f, 1.f, 0.f, 1.f, 1.00f },
};
#define SCN_CUT_NVERTS ((int)(sizeof(SCN_CUT_VERTS)/sizeof(SCN_CUT_VERTS[0])))

/* TRANSLUCENT quad, bottom-left, ndc z=0.3 (in front of the background). Uses the
 * half-alpha texture; src-over blends it over whatever was already drawn. */
static const ScnVert SCN_TRANS_VERTS[] = {
    { -0.85f, -0.85f, 0.3f, 1.f, 0.f, 0.f, 1.00f },
    { -0.20f, -0.85f, 0.3f, 1.f, 1.f, 0.f, 1.00f },
    { -0.20f, -0.25f, 0.3f, 1.f, 1.f, 1.f, 1.00f },
    { -0.85f, -0.85f, 0.3f, 1.f, 0.f, 0.f, 1.00f },
    { -0.20f, -0.25f, 0.3f, 1.f, 1.f, 1.f, 1.00f },
    { -0.85f, -0.25f, 0.3f, 1.f, 0.f, 1.f, 1.00f },
};
#define SCN_TRANS_NVERTS ((int)(sizeof(SCN_TRANS_VERTS)/sizeof(SCN_TRANS_VERTS[0])))

/* Small mipped SOLID quad, top-left corner, ndc z=0.1 (in front of all). ~8px on
 * screen for a 16x16 texture => ~2 texels/pixel => LOD ~1 (uniform minification,
 * so the per-triangle constant LOD is exact and matches GL's level selection). */
static const ScnVert SCN_MIP_VERTS[] = {
    { -0.83f, 0.77f, 0.1f, 1.f, 0.f, 0.f, 1.00f },
    { -0.77f, 0.77f, 0.1f, 1.f, 1.f, 0.f, 1.00f },
    { -0.77f, 0.83f, 0.1f, 1.f, 1.f, 1.f, 1.00f },
    { -0.83f, 0.77f, 0.1f, 1.f, 0.f, 0.f, 1.00f },
    { -0.77f, 0.83f, 0.1f, 1.f, 1.f, 1.f, 1.00f },
    { -0.83f, 0.83f, 0.1f, 1.f, 0.f, 1.f, 1.00f },
};
#define SCN_MIP_NVERTS ((int)(sizeof(SCN_MIP_VERTS)/sizeof(SCN_MIP_VERTS[0])))

/* Cutout atlas: same checker palette as the base atlas, but the dark cells are
 * fully transparent (alpha 0) -> holes the alpha test discards. Border kept
 * opaque so the quad edge is a solid frame. */
static inline void scn_fill_cutout(uint8_t *rgba /* ATLAS_W*ATLAS_H*4 */) {
    for (int y = 0; y < ATLAS_H; ++y)
        for (int x = 0; x < ATLAS_W; ++x) {
            uint8_t *p = rgba + (y * ATLAS_W + x) * 4;
            int border = (x == 0 || y == 0 || x == ATLAS_W - 1 || y == ATLAS_H - 1);
            int cell = ((x / 4) + (y / 4)) & 1;
            if (border)    { p[0] = 220; p[1] = 40;  p[2] = 40;  p[3] = 255; }
            else if (cell) { p[0] = 210; p[1] = 180; p[2] = 60;  p[3] = 255; }
            else           { p[0] = 40;  p[1] = 40;  p[2] = 48;  p[3] = 0;   } /* hole */
        }
}

/* Translucent atlas: checker palette at half alpha (128) everywhere -> ~0.5 blend. */
static inline void scn_fill_translucent(uint8_t *rgba /* ATLAS_W*ATLAS_H*4 */) {
    for (int y = 0; y < ATLAS_H; ++y)
        for (int x = 0; x < ATLAS_W; ++x) {
            uint8_t *p = rgba + (y * ATLAS_W + x) * 4;
            int border = (x == 0 || y == 0 || x == ATLAS_W - 1 || y == ATLAS_H - 1);
            int cell = ((x / 4) + (y / 4)) & 1;
            if (border)    { p[0] = 220; p[1] = 40;  p[2] = 40;  }
            else if (cell) { p[0] = 210; p[1] = 180; p[2] = 60;  }
            else           { p[0] = 40;  p[1] = 40;  p[2] = 48;  }
            p[3] = 128;
        }
}

/* Gamma-correct (gamma 2.2) 2x2 box downsample, src (sw x sh) -> dst (sw/2 x sh/2).
 * RGB filtered in linear light, alpha averaged linearly. Both the golden and the
 * candidate consume the SAME generated levels, so this only needs to be
 * deterministic, not bit-match GL's own glGenerateMipmap. */
static inline void scn_box_down(const uint8_t *src, int sw, int sh, uint8_t *dst) {
    int dw = sw / 2, dh = sh / 2;
    for (int y = 0; y < dh; ++y)
        for (int x = 0; x < dw; ++x) {
            const uint8_t *s00 = src + ((2*y)   * sw + (2*x)  ) * 4;
            const uint8_t *s10 = src + ((2*y)   * sw + (2*x+1)) * 4;
            const uint8_t *s01 = src + ((2*y+1) * sw + (2*x)  ) * 4;
            const uint8_t *s11 = src + ((2*y+1) * sw + (2*x+1)) * 4;
            uint8_t *d = dst + (y * dw + x) * 4;
            for (int c = 0; c < 3; ++c) {
                float l = powf(s00[c]/255.f, 2.2f) + powf(s10[c]/255.f, 2.2f)
                        + powf(s01[c]/255.f, 2.2f) + powf(s11[c]/255.f, 2.2f);
                l *= 0.25f;
                float v = powf(l, 1.f/2.2f) * 255.f + 0.5f;
                d[c] = (uint8_t)(v > 255.f ? 255.f : v);
            }
            int a = (s00[3] + s10[3] + s01[3] + s11[3] + 2) / 4;
            d[3] = (uint8_t)a;
        }
}

/* Build the full mip chain for a 16x16 level 0 into caller buffers.
 * lvl1=8x8, lvl2=4x4, lvl3=2x2, lvl4=1x1 (RGBA8). Returns level count above 0 (4). */
static inline int scn_build_mips(const uint8_t *lvl0,
                                 uint8_t *lvl1, uint8_t *lvl2,
                                 uint8_t *lvl3, uint8_t *lvl4) {
    scn_box_down(lvl0, 16, 16, lvl1);
    scn_box_down(lvl1, 8, 8, lvl2);
    scn_box_down(lvl2, 4, 4, lvl3);
    scn_box_down(lvl3, 2, 2, lvl4);
    return 4;
}

/* Procedural 16x16 RGBA atlas: 4x4 checker of two colors + a 1px red border,
 * so nearest sampling and uv orientation are visible in the diff. */
static inline void scn_fill_atlas(uint8_t *rgba /* ATLAS_W*ATLAS_H*4 */) {
    for (int y = 0; y < ATLAS_H; ++y)
        for (int x = 0; x < ATLAS_W; ++x) {
            uint8_t *p = rgba + (y * ATLAS_W + x) * 4;
            int border = (x == 0 || y == 0 || x == ATLAS_W - 1 || y == ATLAS_H - 1);
            int cell = ((x / 4) + (y / 4)) & 1;
            uint8_t r, g, b;
            if (border)      { r = 220; g = 40;  b = 40;  }
            else if (cell)   { r = 210; g = 180; b = 60;  }
            else             { r = 40;  g = 40;  b = 48;  }
            p[0] = r; p[1] = g; p[2] = b; p[3] = 255;
        }
}

/* Write an RGB PPM (P6), top-down (row 0 = top). */
static inline int scn_write_ppm(const char *path, const uint8_t *rgb, int w, int h) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    fwrite(rgb, 1, (size_t)w * h * 3, f);
    fclose(f);
    return 0;
}
#endif
