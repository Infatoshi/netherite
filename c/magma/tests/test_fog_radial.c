/* Regression for Minecraft's GL_NV_fog_distance path. The live llvmpipe
 * renderer advertises the extension and uses GL_EYE_RADIAL_NV, so fog distance
 * is the perspective-correct interpolation of per-vertex eye-space length, not
 * clip.w / axial depth. */
#include "core/types.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int fail;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: "); printf(__VA_ARGS__); \
    printf("\n"); fail = 1; } } while (0)

int main(void) {
    CrRgba white = {255,255,255,255};
    CrTexture tex = {1,1,&white,1,0,{0},{0},{0}};
    CrShadeCtx sh = {&tex,{0,0,0,255},10.0f,20.0f,0,1,
                     CR_LAYER_SOLID,0,0,0.0f};
    CrScreenTri tri;
    memset(&tri, 0, sizeof tri);
    tri.v[0].spos = (CrVec3){0,0,0.5f};
    tri.v[1].spos = (CrVec3){0,4,0.5f};
    tri.v[2].spos = (CrVec3){4,0,0.5f};
    for (int i=0; i<3; ++i) {
        tri.v[i].invw = 0.1f;
        tri.v[i].light_w = 0.1f;
        tri.v[i].ao_w = 0.1f;
        tri.v[i].tint_r_w = white.r * 0.1f;
        tri.v[i].tint_g_w = white.g * 0.1f;
        tri.v[i].tint_b_w = white.b * 0.1f;
        tri.v[i].tint_a_w = white.a * 0.1f;
    }
    tri.v[0].eye_dist_w = 1.0f;
    tri.v[1].eye_dist_w = 1.0f;
    tri.v[2].eye_dist_w = 2.0f;

    CrFramebuffer fb;
    cr_fb_alloc(&fb, 4, 4);
    cr_fb_clear(&fb, (CrRgba){1,2,3,255});
    cr_raster_cpu(&fb, &tri, 1, &sh);
    CHECK(fb.color[0].r == 223 && fb.color[0].g == 223 && fb.color[0].b == 223,
          "radial fog pixel=(%u,%u,%u), want (223,223,223)",
          fb.color[0].r, fb.color[0].g, fb.color[0].b);
    cr_fb_free(&fb);

    CrVertex verts[3];
    memset(verts, 0, sizeof verts);
    verts[0].pos = (CrVec3){6,0,-8};
    verts[1].pos = (CrVec3){7,0,-8};
    verts[2].pos = (CrVec3){6,1,-8};
    for (int i=0; i<3; ++i) {
        verts[i].light = verts[i].ao = 1.0f;
        verts[i].tint = white;
    }
    CrCamera cam = {{0,0,0},0,0,70.0f,1.0f,0.05f,128.0f};
    CrScreenTri out[2];
    int n = cr_transform(verts, 3, NULL, 0, &cam, 64, 64, out, 2);
    CHECK(n == 1, "transform emitted %d triangles, want 1", n);
    if (n == 1) {
        float recovered = out[0].v[0].eye_dist_w / out[0].v[0].invw;
        CHECK(fabsf(recovered - 10.0f) < 1e-5f,
              "radial eye distance %.8f, want 10 (not axial 8)", recovered);
    }

    if (fail) return 1;
    printf("FOG_RADIAL PASS\n");
    return 0;
}
