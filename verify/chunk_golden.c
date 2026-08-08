/* GOLDEN (rung 3): rasterize the REAL 3x3-chunk scene with Mesa software GL
 * (OSMesa) from the identical geometry, atlas, camera matrices, colour folding,
 * draw order, viewport, clear colour and depth func as the candidate. Only the
 * triangle->pixel step (Mesa GL vs our cr_raster) differs. Offscreen readback,
 * top-down PPM to /tmp/chunk_golden.ppm. */
#include <stdio.h>
#include <stdlib.h>
#include <GL/osmesa.h>
#include <GL/gl.h>

#include "core/types.h"
#include "../verify/chunk_scene.h"

#ifndef GL_NEAREST_MIPMAP_NEAREST
#define GL_NEAREST_MIPMAP_NEAREST 0x2700
#endif

#define FB_W 512
#define FB_H 512

/* Upload a CrTexture: level 0 always; the mip chain too when want_mips. */
static GLuint upload_atlas(const CrTexture *tex, int want_mips) {
    GLuint id; glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    want_mips ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tex->w, tex->h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, tex->texels);
    if (want_mips && tex->mip_levels > 0) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, tex->mip_levels);
        for (int l = 0; l < tex->mip_levels; ++l)
            glTexImage2D(GL_TEXTURE_2D, l + 1, GL_RGBA8,
                         tex->mipw[l], tex->miph[l], 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, tex->mip[l]);
    }
    return id;
}

/* Submit one layer's world-space CrVertex triangle list. Colour is folded
 * EXACTLY as cr_shade does: color = tint/255 * light * ao (per channel), alpha =
 * tint.a/255; GL_MODULATE then multiplies by the atlas texel. */
static void draw_layer(const CrVertex *v, int n) {
    const float inv255 = 1.0f / 255.0f;
    glBegin(GL_TRIANGLES);
    for (int i = 0; i < n; ++i) {
        float la = v[i].light * v[i].ao;
        glColor4f(v[i].tint.r * inv255 * la,
                  v[i].tint.g * inv255 * la,
                  v[i].tint.b * inv255 * la,
                  v[i].tint.a * inv255);
        glTexCoord2f(v[i].uv.x, v[i].uv.y);
        glVertex3f(v[i].pos.x, v[i].pos.y, v[i].pos.z);
    }
    glEnd();
}

int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : "/tmp/chunk_golden.ppm";

    OSMesaContext ctx = OSMesaCreateContextExt(OSMESA_RGBA, 24, 0, 0, NULL);
    if (!ctx) { fprintf(stderr, "OSMesaCreateContextExt failed\n"); return 1; }
    unsigned char *buf = malloc((size_t)FB_W * FB_H * 4);
    if (!OSMesaMakeCurrent(ctx, buf, GL_UNSIGNED_BYTE, FB_W, FB_H)) {
        fprintf(stderr, "OSMesaMakeCurrent failed\n"); return 1;
    }
    OSMesaPixelStore(OSMESA_Y_UP, 1); /* lower-left origin; we flip on readback */

    printf("GL_RENDERER: %s\n", (const char *)glGetString(GL_RENDERER));
    printf("GL_VERSION : %s\n", (const char *)glGetString(GL_VERSION));

    ChunkScene scn;
    chunkscene_init(&scn, FB_W, FB_H);
    printf("mesh verts: solid=%d cutout_mipped=%d cutout=%d translucent=%d\n",
           scn.nverts[0], scn.nverts[1], scn.nverts[2], scn.nverts[3]);
    printf("camera: pos=(%.2f,%.2f,%.2f) yaw=%.3f pitch=%.3f fov=%.1f\n",
           scn.cam.pos.x, scn.cam.pos.y, scn.cam.pos.z,
           scn.cam.yaw, scn.cam.pitch, scn.cam.fov_deg);

    glViewport(0, 0, FB_W, FB_H);
    CrMat4 proj = scn_proj(&scn, FB_W, FB_H);
    CrMat4 view = scn_view(&scn);
    glMatrixMode(GL_PROJECTION); glLoadMatrixf(proj.m);
    glMatrixMode(GL_MODELVIEW);  glLoadMatrixf(view.m);

    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glClearDepth(1.0);
    glDisable(GL_DITHER);
    glDisable(GL_CULL_FACE);     /* candidate draws both windings to match */
    glShadeModel(GL_SMOOTH);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    GLuint tex_flat = upload_atlas(&scn.atlas, 0);
    GLuint tex_mip  = upload_atlas(&scn.atlas, 1);

    /* Sky-blue clear, matching the candidate's cr_fb_clear. */
    glClearColor(135.f/255.f, 206.f/255.f, 235.f/255.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* Draw order = CrRenderLayer enum order: SOLID, CUTOUT_MIPPED, CUTOUT,
     * TRANSLUCENT (translucent last, over the opaque scene). */

    /* SOLID: opaque, depth write on, no blend / no alpha test. */
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glBindTexture(GL_TEXTURE_2D, tex_flat);
    draw_layer(scn.verts[CR_LAYER_SOLID], scn.nverts[CR_LAYER_SOLID]);

    /* CUTOUT_MIPPED: alpha test, mip level selection, depth write on. */
    glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_GREATER, 0.5f);
    glBindTexture(GL_TEXTURE_2D, tex_mip);
    draw_layer(scn.verts[CR_LAYER_CUTOUT_MIPPED], scn.nverts[CR_LAYER_CUTOUT_MIPPED]);

    /* CUTOUT: alpha test, no mips, depth write on. */
    glBindTexture(GL_TEXTURE_2D, tex_flat);
    draw_layer(scn.verts[CR_LAYER_CUTOUT], scn.nverts[CR_LAYER_CUTOUT]);
    glDisable(GL_ALPHA_TEST);

    /* TRANSLUCENT: src-over blend, depth test on, depth write OFF, drawn last. */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glBindTexture(GL_TEXTURE_2D, tex_flat);
    draw_layer(scn.verts[CR_LAYER_TRANSLUCENT], scn.nverts[CR_LAYER_TRANSLUCENT]);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);

    glFinish();

    /* readback (Y_UP=1 => buf row 0 is bottom); emit top-down PPM. */
    unsigned char *rgb = malloc((size_t)FB_W * FB_H * 3);
    for (int y = 0; y < FB_H; ++y) {
        const unsigned char *src = buf + (size_t)(FB_H - 1 - y) * FB_W * 4;
        unsigned char *dst = rgb + (size_t)y * FB_W * 3;
        for (int x = 0; x < FB_W; ++x) {
            dst[x*3+0] = src[x*4+0];
            dst[x*3+1] = src[x*4+1];
            dst[x*3+2] = src[x*4+2];
        }
    }
    if (scn_write_ppm(out, rgb, FB_W, FB_H)) {
        fprintf(stderr, "write %s failed\n", out); return 1;
    }
    printf("wrote %s (%dx%d)\n", out, FB_W, FB_H);
    free(rgb); free(buf);
    chunkscene_free(&scn);
    OSMesaDestroyContext(ctx);
    return 0;
}
