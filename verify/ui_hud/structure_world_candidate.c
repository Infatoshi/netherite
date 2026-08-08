/* Focused native TileEntityStructureRenderer fixture.
 *
 * The matching Java capture renders the controller underground and places
 * its legal +32Y volume in empty sky. This candidate therefore exercises the
 * production Structure geometry, camera transform, CPU rasterizer, depth,
 * and POSITION_COLOR shade without pricing unrelated terrain/HUD pixels into
 * the comparison. Output uses a chroma backdrop; compare_structure_world.py
 * composites owned native pixels over the Java off frame. */
#include "game/overlay.h"
#include "game/runtime.h"
#include "core/types.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 854
#define H 480
#define MAX_VERTS (90 + 5 * 3 * 4 * 144)

static const CrRgba CHROMA = {13, 17, 19, 255};

/* Unused overlay.c loading-screen symbols in this focused link. */
int gm_font_width(const char *s) { return (int)strlen(s) * 6; }
void gm_font_draw(CrFramebuffer *fb, const char *s, int x, int y, int scale,
                  unsigned rgb, int shadow) {
    (void)fb; (void)s; (void)x; (void)y; (void)scale; (void)rgb; (void)shadow;
}

static int write_ppm(const char *path, const CrFramebuffer *fb) {
    FILE *stream = fopen(path, "wb");
    if (!stream) return 0;
    fprintf(stream, "P6\n%d %d\n255\n", fb->w, fb->h);
    for (int i = 0; i < fb->w * fb->h; ++i) {
        const unsigned char rgb[3] = {
            fb->color[i].r, fb->color[i].g, fb->color[i].b,
        };
        if (fwrite(rgb, 1, sizeof rgb, stream) != sizeof rgb) {
            fclose(stream);
            return 0;
        }
    }
    return fclose(stream) == 0;
}

static int render_state(const char *path, int mode, int mirror, int rotation,
                        int show_air, int show_bounds) {
    CrRgba *color = calloc((size_t)W * H, sizeof *color);
    float *depth = malloc((size_t)W * H * sizeof *depth);
    CrVertex *verts = malloc((size_t)MAX_VERTS * sizeof *verts);
    CrScreenTri *tris = malloc((size_t)MAX_VERTS / 3 * 2 * sizeof *tris);
    CrFramebuffer fb = {W, H, color, depth};
    CrCamera camera = {
        .pos = {0.5f, 66.62f, -6.5f},
        .yaw = 3.14159265358979323846f,
        .pitch = -20.0f * 0.01745329251994329577f,
        .fov_deg = 70.0f,
        .aspect = (float)W / H,
        .znear = 0.05f,
        .zfar = 512.0f,
    };
    GmRuntimeStructureBlock structure;
    CrRgba white_px = {255, 255, 255, 255};
    CrTexture white = {1, 1, &white_px, 0, 0, {0}, {0}, {0}};
    CrShadeCtx shade = {0};
    int n = 0, ntris;
    if (!color || !depth || !verts || !tris) {
        free(color); free(depth); free(verts); free(tris);
        return 0;
    }
    for (int i = 0; i < W * H; ++i) {
        color[i] = CHROMA;
        depth[i] = 1.0f;
    }
    memset(&structure, 0, sizeof structure);
    structure.active = 1;
    structure.wx = 0; structure.wy = 32; structure.wz = 0;
    structure.pos_x = -2; structure.pos_y = 32; structure.pos_z = 1;
    structure.size_x = 5; structure.size_y = 3; structure.size_z = 4;
    structure.mode = mode;
    structure.mirror = mirror;
    structure.rotation = rotation;
    structure.show_air = show_air;
    structure.show_bounding_box = show_bounds;
    n += gm_overlay_emit_structure_bounds(
        verts + n, MAX_VERTS - n, &structure, &camera, W, H);
    if (mode == GM_STRUCTURE_MODE_SAVE && show_air) {
        for (int pass = 0; pass < 2; ++pass)
            for (int rz = 0; rz < structure.size_z; ++rz)
                for (int ry = 0; ry < structure.size_y; ++ry)
                    for (int rx = 0; rx < structure.size_x; ++rx) {
                        int wx = structure.wx + structure.pos_x + rx;
                        int wy = structure.wy + structure.pos_y + ry;
                        int wz = structure.wz + structure.pos_z + rz;
                        int block = rx == 1 && ry == 1 && rz == 1 ? 217 : 0;
                        n += gm_overlay_emit_structure_marker(
                            verts + n, MAX_VERTS - n, &structure,
                            wx, wy, wz, block, pass, &camera, W, H);
                    }
    }
    shade.atlas = &white;
    shade.layer = CR_LAYER_TRANSLUCENT;
    shade.blend = 4;
    shade.untextured = 1;
    shade.depth_lequal = 1;
    shade.depth_d24 = 1;
    ntris = cr_transform(
        verts, n, NULL, 0, &camera, W, H, tris, MAX_VERTS / 3 * 2);
    cr_raster_cpu(&fb, tris, ntris, &shade);
    if (!write_ppm(path, &fb)) n = 0;
    fprintf(stderr, "%s: verts=%d tris=%d\n", path, n, ntris);
    free(color); free(depth); free(verts); free(tris);
    return n > 0 || (mode == GM_STRUCTURE_MODE_LOAD && !show_bounds);
}

int main(int argc, char **argv) {
    char path[1024];
    const char *out = argc > 1 ? argv[1] : ".";
#define RENDER(name, mode, mirror, rotation, air, bounds) do { \
    snprintf(path, sizeof path, "%s/structure_world_%s.ppm", out, name); \
    if (!render_state(path, mode, mirror, rotation, air, bounds)) return 1; \
} while (0)
    RENDER("save_air", GM_STRUCTURE_MODE_SAVE, GM_STRUCTURE_MIRROR_NONE,
           GM_STRUCTURE_ROTATION_NONE, 1, 1);
    RENDER("load_transform", GM_STRUCTURE_MODE_LOAD,
           GM_STRUCTURE_MIRROR_LEFT_RIGHT, GM_STRUCTURE_ROTATION_CW90, 0, 1);
    RENDER("load_hidden", GM_STRUCTURE_MODE_LOAD,
           GM_STRUCTURE_MIRROR_LEFT_RIGHT, GM_STRUCTURE_ROTATION_CW90, 0, 0);
#undef RENDER
    return 0;
}
