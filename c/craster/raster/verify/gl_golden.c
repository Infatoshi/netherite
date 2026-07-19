/* GOLDEN: rasterize the shared scene with real Mesa software GL (OSMesa, the same
 * llvmpipe/swrast path Minecraft uses on anvil). Offscreen, glReadPixels readback.
 * This is the KernelBench reference for the triangle->pixel kernel. */
#include <stdio.h>
#include <stdlib.h>
#include <GL/osmesa.h>
#include <GL/gl.h>
#include "scene.h"

int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : "/tmp/raster_golden.ppm";

    OSMesaContext ctx = OSMesaCreateContextExt(OSMESA_RGBA, 24, 0, 0, NULL);
    if (!ctx) { fprintf(stderr, "OSMesaCreateContextExt failed\n"); return 1; }
    unsigned char *buf = malloc((size_t)SCN_W * SCN_H * 4);
    if (!OSMesaMakeCurrent(ctx, buf, GL_UNSIGNED_BYTE, SCN_W, SCN_H)) {
        fprintf(stderr, "OSMesaMakeCurrent failed\n"); return 1;
    }
    /* OSMesa origin is lower-left; we flip on readback to top-down. */
    OSMesaPixelStore(OSMESA_Y_UP, 1);

    printf("GL_RENDERER: %s\n", (const char *)glGetString(GL_RENDERER));
    printf("GL_VERSION : %s\n", (const char *)glGetString(GL_VERSION));

    glViewport(0, 0, SCN_W, SCN_H);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);  glLoadIdentity();

    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glClearDepth(1.0);
    glDisable(GL_BLEND); glDisable(GL_DITHER); glDisable(GL_CULL_FACE);
    glShadeModel(GL_SMOOTH);

    /* GL_MODULATE so frag = texel * vertexColor for every layer. */
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    /* base opaque atlas (nearest, no mips) */
    uint8_t atlas[ATLAS_W * ATLAS_H * 4];
    scn_fill_atlas(atlas);
    GLuint tex_solid; glGenTextures(1, &tex_solid);
    glBindTexture(GL_TEXTURE_2D, tex_solid);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, ATLAS_W, ATLAS_H, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, atlas);

    /* cutout atlas (holes at alpha 0) */
    uint8_t cut[ATLAS_W * ATLAS_H * 4];
    scn_fill_cutout(cut);
    GLuint tex_cut; glGenTextures(1, &tex_cut);
    glBindTexture(GL_TEXTURE_2D, tex_cut);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, ATLAS_W, ATLAS_H, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, cut);

    /* translucent atlas (alpha 128) */
    uint8_t trans[ATLAS_W * ATLAS_H * 4];
    scn_fill_translucent(trans);
    GLuint tex_trans; glGenTextures(1, &tex_trans);
    glBindTexture(GL_TEXTURE_2D, tex_trans);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, ATLAS_W, ATLAS_H, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, trans);

    /* mipped atlas: supply the SAME gamma-correct box-filter chain the candidate
     * uses (not glGenerateMipmap) so only level SELECTION can differ, and use
     * GL_NEAREST_MIPMAP_NEAREST to pick one level nearest. */
    uint8_t m1[8*8*4], m2[4*4*4], m3[2*2*4], m4[1*1*4];
    scn_build_mips(atlas, m1, m2, m3, m4);
    GLuint tex_mip; glGenTextures(1, &tex_mip);
    glBindTexture(GL_TEXTURE_2D, tex_mip);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, atlas);
    glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, m1);
    glTexImage2D(GL_TEXTURE_2D, 2, GL_RGBA8, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, m2);
    glTexImage2D(GL_TEXTURE_2D, 3, GL_RGBA8, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, m3);
    glTexImage2D(GL_TEXTURE_2D, 4, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, m4);

    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    #define DRAW_GROUP(VERTS, N) \
        glBegin(GL_TRIANGLES); \
        for (int i = 0; i < (N); ++i) { \
            const ScnVert *v = &(VERTS)[i]; \
            glColor3f(v->light, v->light, v->light); \
            glTexCoord2f(v->u, v->v); \
            glVertex4f(v->x, v->y, v->z, v->w); \
        } \
        glEnd();

    /* 1) SOLID: opaque, depth write on, no blend/alpha test. */
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glBindTexture(GL_TEXTURE_2D, tex_solid);
    DRAW_GROUP(SCN_VERTS, SCN_NVERTS);

    /* 2) CUTOUT: alpha test discards texel.a<128 (GL_GREATER 0.5), depth write on. */
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.5f);
    glBindTexture(GL_TEXTURE_2D, tex_cut);
    DRAW_GROUP(SCN_CUT_VERTS, SCN_CUT_NVERTS);
    glDisable(GL_ALPHA_TEST);

    /* 3) TRANSLUCENT: src-over blend, depth test on but depth write OFF. */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glBindTexture(GL_TEXTURE_2D, tex_trans);
    DRAW_GROUP(SCN_TRANS_VERTS, SCN_TRANS_NVERTS);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);

    /* 4) mipped SOLID quad: GL_NEAREST_MIPMAP_NEAREST level selection. */
    glBindTexture(GL_TEXTURE_2D, tex_mip);
    DRAW_GROUP(SCN_MIP_VERTS, SCN_MIP_NVERTS);

    #undef DRAW_GROUP
    glFinish();

    /* readback RGBA (Y_UP=1 => row 0 is bottom in buf); write top-down PPM */
    uint8_t *rgb = malloc((size_t)SCN_W * SCN_H * 3);
    for (int y = 0; y < SCN_H; ++y) {
        const unsigned char *src = buf + (size_t)(SCN_H - 1 - y) * SCN_W * 4;
        uint8_t *dst = rgb + (size_t)y * SCN_W * 3;
        for (int x = 0; x < SCN_W; ++x) {
            dst[x*3+0] = src[x*4+0];
            dst[x*3+1] = src[x*4+1];
            dst[x*3+2] = src[x*4+2];
        }
    }
    if (scn_write_ppm(out, rgb, SCN_W, SCN_H)) { fprintf(stderr, "write %s failed\n", out); return 1; }
    printf("wrote %s (%dx%d)\n", out, SCN_W, SCN_H);
    free(rgb); free(buf); OSMesaDestroyContext(ctx);
    return 0;
}
