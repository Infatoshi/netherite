/* demo/demo_cube.c - integration smoke test of the whole magma spine.
 *
 * Builds a framebuffer, a procedural atlas, and a camera a few units back
 * looking at the origin, then every frame: clear the framebuffer to a sky
 * color, spin a unit cube, run cr_transform -> cr_raster_cpu -> present.
 *
 * This file ties together modules authored by other agents (cr_transform,
 * cr_raster_cpu, cr_fb_*, cr_shade, scene_*); it is compiled here and links
 * once those modules exist. ESC or window close quits.
 *
 * Args:
 *   --frames N    run N frames then exit (headless-friendly).
 *   --ppm PATH    after the last frame, dump the color buffer as binary P6 PPM.
 * Default: interactive loop until ESC / window close.
 */
#include "core/types.h"
#include "demo/scene.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define FB_W 800
#define FB_H 600
#define MAX_TRIS 64   /* a cube is 12 tris; near-clip can add a few more */

/* Rotate a world position about the Y axis by `ang` radians. Done in the demo
 * so the spin does not depend on any particular yaw-sign convention. */
static CrVec3 rotate_y(CrVec3 p, float ang)
{
    float s = sinf(ang), c = cosf(ang);
    CrVec3 r;
    r.x = c * p.x + s * p.z;
    r.y = p.y;
    r.z = -s * p.x + c * p.z;
    return r;
}

/* Write fb->color as a binary P6 PPM (opaque RGB, top-down). Returns 0 on ok. */
static int write_ppm(const char *path, const CrFramebuffer *fb)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "demo: cannot open %s for writing\n", path);
        return -1;
    }
    fprintf(f, "P6\n%d %d\n255\n", fb->w, fb->h);
    size_t n = (size_t)fb->w * (size_t)fb->h;
    u8 *rgb = (u8 *)malloc(n * 3);
    if (!rgb) {
        fclose(f);
        return -1;
    }
    for (size_t i = 0; i < n; ++i) {
        rgb[i * 3 + 0] = fb->color[i].r;
        rgb[i * 3 + 1] = fb->color[i].g;
        rgb[i * 3 + 2] = fb->color[i].b;
    }
    size_t wrote = fwrite(rgb, 3, n, f);
    free(rgb);
    fclose(f);
    if (wrote != n) {
        fprintf(stderr, "demo: short write to %s\n", path);
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    int         want_frames = -1;      /* -1 = interactive */
    const char *ppm_path    = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            want_frames = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--ppm") == 0 && i + 1 < argc) {
            ppm_path = argv[++i];
        } else {
            fprintf(stderr, "usage: %s [--frames N] [--ppm PATH]\n", argv[0]);
            return 2;
        }
    }

    /* --- framebuffer --- */
    CrFramebuffer fb;
    cr_fb_alloc(&fb, FB_W, FB_H);
    if (!fb.color || !fb.depth) {
        fprintf(stderr, "demo: cr_fb_alloc failed\n");
        return 1;
    }

    /* --- scene geometry + texture --- */
    CrVertex cube[36];
    int nverts = scene_cube(cube, 36);
    if (nverts != 36) {
        fprintf(stderr, "demo: scene_cube returned %d (expected 36)\n", nverts);
        cr_fb_free(&fb);
        return 1;
    }
    CrTexture atlas = scene_atlas();

    /* --- camera: a few units back, looking down -Z at the origin --- */
    CrCamera cam = {0};
    cam.pos    = (CrVec3){0.0f, 0.0f, 3.0f};
    cam.yaw    = 0.0f;
    cam.pitch  = 0.0f;
    cam.fov_deg = 60.0f;
    cam.aspect  = (float)FB_W / (float)FB_H;
    cam.znear   = 0.05f;
    cam.zfar    = 100.0f;

    /* --- shading context: use the scene atlas, mild fog off by default --- */
    CrShadeCtx sh;
    memset(&sh, 0, sizeof(sh));
    sh.atlas      = &atlas;
    sh.fog_color  = (CrRgba){135, 206, 235, 255}; /* sky blue */
    sh.fog_start  = 8.0f;
    sh.fog_end    = 40.0f;
    sh.alpha_test = 0;
    sh.enable_fog = 0;

    const CrRgba sky = {135, 206, 235, 255};

    /* --- window (headless-safe; dummy driver in CI) --- */
    CrWindow *win = cr_window_open(FB_W, FB_H, "magma - spinning cube");
    if (!win) {
        fprintf(stderr, "demo: cr_window_open failed\n");
        cr_fb_free(&fb);
        return 1;
    }

    CrScreenTri tris[MAX_TRIS];
    CrVertex    frame_verts[36];

    int   frame = 0;
    int   running = 1;
    while (running) {
        float ang = (float)frame * 0.02f;

        /* Spin the cube by rotating its world vertices about Y. */
        for (int i = 0; i < 36; ++i) {
            frame_verts[i]     = cube[i];
            frame_verts[i].pos = rotate_y(cube[i].pos, ang);
        }

        cr_fb_clear(&fb, sky);

        int ntris = cr_transform(frame_verts, 36, NULL, 0,
                                 &cam, fb.w, fb.h, tris, MAX_TRIS);
        cr_raster_cpu(&fb, tris, ntris, &sh);
        cr_window_present(win, &fb);

        CrInput in;
        cr_window_poll(win, &in);
        if (in.quit)
            running = 0;

        ++frame;
        if (want_frames >= 0 && frame >= want_frames)
            running = 0;
    }

    if (ppm_path)
        write_ppm(ppm_path, &fb);

    cr_window_close(win);
    cr_fb_free(&fb);
    return 0;
}
