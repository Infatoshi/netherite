/* game/test_sky.c - standalone multi-direction test for the magma sky.
 *
 * Renders 854x480 SKY-ONLY frames at several view directions (the poses the task
 * calls out: looking AT the sun, at the HORIZON, straight UP / zenith, AWAY from
 * the sun, and a NIGHT zenith) and writes a PPM per pose for eyeballing. It then
 * runs asserts on the daytime AT-sun frame:
 *   (1) a real vertical gradient: horizon fog color differs from the zenith sky.
 *   (2) a sun disc: a bright near-white cluster (now the REAL textured sun.png quad,
 *       additive, at MC's celestial position) near the azimuth center, upper half.
 * No Makefile, no GL, libm only.
 */
#include "core/types.h"
#include "game/sky.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PIf
#define M_PIf 3.14159265358979323846f
#endif

static const int W = 854, H = 480;

static int write_ppm(const char *path, const CrFramebuffer *fb) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", fb->w, fb->h);
    size_t n = (size_t)fb->w * fb->h;
    unsigned char *rgb = (unsigned char *)malloc(n * 3);
    for (size_t i = 0; i < n; ++i) {
        rgb[i*3+0] = fb->color[i].r; rgb[i*3+1] = fb->color[i].g; rgb[i*3+2] = fb->color[i].b;
    }
    size_t wrote = fwrite(rgb, 3, n, f);
    free(rgb); fclose(f);
    return wrote == n ? 0 : -1;
}

/* Re-derive the celestial sun direction the shader uses (WorldProvider math). */
static float celestial_angle(float tod) {
    float f = tod - 0.25f;
    if (f < 0.0f) f += 1.0f;
    if (f > 1.0f) f -= 1.0f;
    float f1 = 1.0f - (cosf(f * M_PIf) + 1.0f) * 0.5f;
    return f + (f1 - f) / 3.0f;
}

/* Render one sky-only frame at (yaw,pitch,tod) into fb (must be preallocated,
 * depth pre-cleared to far). */
static void render_pose(CrFramebuffer *fb, float yaw, float pitch, float tod) {
    CrCamera cam = {0};
    cam.pos = (CrVec3){0.0f, 80.0f, 0.0f};
    cam.yaw = yaw; cam.pitch = pitch;
    cam.fov_deg = 70.0f; cam.aspect = (float)W / (float)H;
    cam.znear = 0.05f; cam.zfar = 600.0f;
    for (int i = 0; i < W * H; ++i) fb->depth[i] = 1.0f;
    gm_sky_draw(fb, &cam, tod);
}

int main(void) {
    const float tod_day = 0.10f;   /* morning; sun well up, full daylight */
    const float tod_night = 0.75f; /* midnight; moon up, stars out */

    /* Sun geometry at the daytime tod (sun_dir = (-sin a, cos a, 0)). */
    float a = celestial_angle(tod_day) * 2.0f * M_PIf;
    float sx = -sinf(a), syd = cosf(a);
    float sun_elev = asinf(syd);
    float sun_yaw  = atan2f(-sx, -0.0001f);   /* azimuth along +/-X (sun_dir.z ~ 0) */

    CrFramebuffer fb;
    fb.w = W; fb.h = H;
    fb.color = (CrRgba *)malloc((size_t)W * H * sizeof(CrRgba));
    fb.depth = (float *)malloc((size_t)W * H * sizeof(float));

    /* The pose set. AT-sun matches the old test (sun rides upper-center). */
    struct { const char *name; float yaw, pitch, tod; } poses[] = {
        { "at_sun",   sun_yaw,           sun_elev - 0.35f, tod_day   },
        { "horizon",  sun_yaw,           0.0f,             tod_day   },
        { "zenith",   sun_yaw,           1.5533f,          tod_day   }, /* ~89 deg up */
        { "away_sun", sun_yaw + M_PIf,   0.30f,            tod_day   },
        { "night_up", sun_yaw + M_PIf,   1.2f,             tod_night },
    };
    int npose = (int)(sizeof(poses) / sizeof(poses[0]));
    int at_sun_idx = 0;

    /* store the at-sun frame for the asserts */
    CrRgba *at_sun_frame = (CrRgba *)malloc((size_t)W * H * sizeof(CrRgba));

    for (int p = 0; p < npose; ++p) {
        render_pose(&fb, poses[p].yaw, poses[p].pitch, poses[p].tod);
        char path[128];
        snprintf(path, sizeof(path), "game/sky_pose_%s.ppm", poses[p].name);
        if (write_ppm(path, &fb) == 0) printf("wrote %s\n", path);
        if (p == at_sun_idx) memcpy(at_sun_frame, fb.color, (size_t)W * H * sizeof(CrRgba));
    }

    /* legacy single-frame preview name kept for existing tooling */
    memcpy(fb.color, at_sun_frame, (size_t)W * H * sizeof(CrRgba));
    if (write_ppm("game/sky_preview.ppm", &fb) == 0)
        printf("wrote game/sky_preview.ppm (at_sun)\n");

    CrRgba *C = at_sun_frame;

    /* ---- assert (1): vertical gradient. Sample the LEFT quarter (no sun there). ---- */
    long tr = 0, tg = 0, tb = 0, br = 0, bg = 0, bb = 0; int nt = 0, nb = 0;
    int xlo = 0, xhi = W / 4;
    for (int y = 0; y < 40; ++y)
        for (int x = xlo; x < xhi; ++x) {
            CrRgba c = C[y * W + x]; tr += c.r; tg += c.g; tb += c.b; nt++;
        }
    for (int y = H - 40; y < H; ++y)
        for (int x = xlo; x < xhi; ++x) {
            CrRgba c = C[y * W + x]; br += c.r; bg += c.g; bb += c.b; nb++;
        }
    int trm = (int)(tr/nt), tgm = (int)(tg/nt), tbm = (int)(tb/nt);
    int brm = (int)(br/nb), bgm = (int)(bg/nb), bbm = (int)(bb/nb);
    int grad_diff = abs(trm-brm) + abs(tgm-bgm) + abs(tbm-bbm);
    printf("gradient: top-left  rgb=(%3d,%3d,%3d)\n", trm, tgm, tbm);
    printf("gradient: bot-left  rgb=(%3d,%3d,%3d)  |sum-diff|=%d\n", brm, bgm, bbm, grad_diff);
    int pass_grad = grad_diff >= 25;
    int pass_grad_dir = (brm > trm + 15) && (bgm > tgm + 10);

    /* ---- assert (2): sun disc = a bright near-white cluster (the real sun.png core). ---- */
    long cxs = 0, cys = 0; int nwhite = 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            CrRgba c = C[y * W + x];
            if (c.r > 235 && c.g > 230 && c.b > 200 && (int)c.r - (int)c.b < 60) {
                cxs += x; cys += y; nwhite++;
            }
        }
    int pass_sun = nwhite > 40;
    int scx = nwhite ? (int)(cxs / nwhite) : -1;
    int scy = nwhite ? (int)(cys / nwhite) : -1;
    int pass_sun_pos = pass_sun && abs(scx - W/2) < W/4 && scy < H/2;
    printf("sun: near-white pixels=%d  centroid=(%d,%d)  expect ~center-x/upper-half\n",
           nwhite, scx, scy);

    free(fb.color); free(fb.depth); free(at_sun_frame);

    int ok = pass_grad && pass_grad_dir && pass_sun && pass_sun_pos;
    printf("\nASSERTS: gradient=%s gradient_dir=%s sun=%s sun_pos=%s => %s\n",
           pass_grad?"PASS":"FAIL", pass_grad_dir?"PASS":"FAIL",
           pass_sun?"PASS":"FAIL", pass_sun_pos?"PASS":"FAIL",
           ok?"ALL PASS":"FAIL");
    return ok ? 0 : 1;
}
