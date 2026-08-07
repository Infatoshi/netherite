/* game/mob_lineup_demo.c - visual harness for game/entity_render.c.
 *
 * Renders all 10 modeled mob types in a row (front view + 3/4 view + back
 * view) with the existing transform + CPU raster pipeline, dumping PPM frames
 * for eyeballing. Not a pass/fail test; see test_entity_render.c for asserts.
 *
 * Build/run: bash game/mob_lineup_demo.sh [outdir]   (default /tmp)
 */
#include "core/types.h"
#include "game/entity_render.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int   TYPES[10] = { 2, 3, 4, 5, 6, 7, 10, 11, 12, 13 };
static const char *NAMES[10] = { "zombie", "skeleton", "creeper", "spider",
                                 "enderman", "blaze", "sheep", "pig", "cow",
                                 "chicken" };
#define NMOBS   10
#define SPACING 3.0f
#define MAXV    (10 * 468)

static void look_at(CrVec3 p, CrVec3 t, float *yaw, float *pitch) {
    float dx = t.x - p.x, dy = t.y - p.y, dz = t.z - p.z;
    *yaw   = atan2f(dx, -dz);
    *pitch = atan2f(dy, sqrtf(dx * dx + dz * dz));
}

static void render_frame(const char *path, const GmEntityView *ents, int n,
                         CrVec3 campos, CrVec3 target, int W, int H) {
    static CrVertex verts[MAXV];
    static CrScreenTri tris[MAXV];   /* >= 1 tri per 3 verts */
    int nv = gm_entities_emit(ents, n, verts, MAXV);

    CrCamera cam = {0};
    cam.pos = campos;
    look_at(campos, target, &cam.yaw, &cam.pitch);
    cam.fov_deg = 60.0f;
    cam.aspect  = (float)W / (float)H;
    cam.znear   = 0.05f;
    cam.zfar    = 256.0f;

    int nt = cr_transform(verts, nv, NULL, 0, &cam, W, H, tris, MAXV);

    CrFramebuffer fb;
    cr_fb_alloc(&fb, W, H);
    CrRgba bg = { 120, 170, 220, 255 };
    cr_fb_clear(&fb, bg);

    CrTexture atlas = gm_entity_atlas();
    CrShadeCtx sh;
    memset(&sh, 0, sizeof(sh));
    sh.atlas = &atlas;
    sh.alpha_test = 1;
    sh.layer = CR_LAYER_CUTOUT;
    cr_raster_cpu(&fb, tris, nt, &sh);

    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); exit(1); }
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; ++i) {
        unsigned char rgb[3] = { fb.color[i].r, fb.color[i].g, fb.color[i].b };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("wrote %s (%d verts, %d tris)\n", path, nv, nt);
    cr_fb_free(&fb);
}

int main(int argc, char **argv) {
    const char *outdir = argc > 1 ? argv[1] : "/tmp";
    char path[512];

    GmEntityView ents[NMOBS];
    memset(ents, 0, sizeof ents);
    float x0 = 0.0f;
    for (int i = 0; i < NMOBS; ++i) {
        ents[i].type = TYPES[i];
        ents[i].x = x0 + (float)i * SPACING;
        ents[i].y = 64.0f;
        ents[i].z = 0.0f;
        ents[i].yaw = 0.0f;      /* MC yaw 0 = facing +Z (south) */
        ents[i].health = 20.0f;
        printf("%-9s at x=%.1f\n", NAMES[i], ents[i].x);
    }
    float midx = x0 + (NMOBS - 1) * SPACING * 0.5f;

    const int W = 1600, H = 400;
    /* front view: camera south of the row (mobs face +Z at yaw 0). */
    snprintf(path, sizeof path, "%s/mob_lineup_front.ppm", outdir);
    render_frame(path, ents, NMOBS,
                 (CrVec3){ midx, 65.6f, 9.0f }, (CrVec3){ midx, 65.0f, 0.0f },
                 W, H);
    /* back view */
    snprintf(path, sizeof path, "%s/mob_lineup_back.ppm", outdir);
    render_frame(path, ents, NMOBS,
                 (CrVec3){ midx, 65.6f, -9.0f }, (CrVec3){ midx, 65.0f, 0.0f },
                 W, H);
    /* 3/4 view, yawed mobs */
    for (int i = 0; i < NMOBS; ++i) ents[i].yaw = 45.0f;
    snprintf(path, sizeof path, "%s/mob_lineup_quarter.ppm", outdir);
    render_frame(path, ents, NMOBS,
                 (CrVec3){ midx, 66.5f, 8.0f }, (CrVec3){ midx, 65.0f, 0.0f },
                 W, H);
    return 0;
}
