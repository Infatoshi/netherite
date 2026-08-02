/* CANDIDATE (rung 4): render the seed-0 hard leaf-canopy scene at the REAL MC
 * client resolution (854x480) and dump a frame to pixel-diff against the
 * captured Minecraft golden (mc_frame.png).
 *
 * The golden was re-captured with the instrumented camera (camera.json /
 * hard_scene.json): eye (8.3, 95, 40.5), magma yaw 0 / pitch -35, FOV 77
 * (= options 70 * spectator fly 1.1), RD=8, noon (time 6000). This binary
 * freezes that pose. The older FOV-70 / z=40.0 freeze is stale against the
 * golden and is what left whole residual ~42 after the alpha_ref shade fix.
 *
 * Render path matches game_candidate / hard-scene-verify:
 *   pose_scene (cumulative populate prepare) + gm_sky_draw + terrain fog +
 *   single winding. game_candidate is the arbitrary-pose generalization of
 *   this binary; hard-scene-verify drives that path with ablations.
 *
 * MC vs magma camera convention (see core/math.c header + capture.sh):
 *   magma forward = (-sin(yaw)cos(pitch), sin(pitch), -cos(yaw)cos(pitch));
 *   yaw 0 looks toward -Z, POSITIVE pitch looks UP. The scene uses yaw 0,
 *   pitch -35deg (toward -Z, tilted down). The MC look vector matches that
 *   forward at MC yaw 180, MC pitch +35.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "core/types.h"
#include "core/config.h"   /* --set key=value -> cr_cfg_set (e.g. no_cull) */
#include "game/sky.h"
#include "../verify/mc_capture/pose_scene.h"

/* Real MC client window content region (see mc_capture/pose.json width/height). */
#define FB_W 854
#define FB_H 480

/* Frozen hard-scene pose (camera.json / hard_scene.json). */
#define EYE_X 8.3f
#define EYE_Y 95.0f
#define EYE_Z 40.5f
#define YAW_DEG 0.0f
#define PITCH_DEG (-35.0f)
#define FOV_DEG 77.0f
/* EntityRenderer.setupCameraTransform: far = RD*16 * sqrt(2); RD=8 -> 181.01933. */
#define ZFAR 181.01933f

static void render_layer(CrFramebuffer *fb, const PoseScene *s,
                         int layer, const CrShadeCtx *sh) {
    int nv = s->nverts[layer];
    if (nv < 3) return;
    int max_tris = (nv / 3) * 2;
    CrScreenTri *tris = malloc(sizeof(CrScreenTri) * (size_t)max_tris);
    int n = cr_transform(s->verts[layer], nv, NULL, 0, &s->cam,
                         FB_W, FB_H, tris, max_tris);
    /* Single winding matches game_main / game_candidate default. Dual wind is
     * a hard-scene no-op (crop delta << 0.1); opt-in via MAGMA_DUAL_WIND=1. */
    int dual = 0;
    {
        const char *e = getenv("MAGMA_DUAL_WIND");
        if (e && atoi(e) != 0) dual = 1;
    }
    if (!dual) {
        cr_raster_cpu(fb, tris, n, sh);
    } else {
        CrScreenTri *both = malloc(sizeof(CrScreenTri) * (size_t)n * 2);
        for (int i = 0; i < n; ++i) {
            both[2 * i] = tris[i];
            both[2 * i + 1] = tris[i];
            CrScreenVert tmp = both[2 * i + 1].v[1];
            both[2 * i + 1].v[1] = both[2 * i + 1].v[2];
            both[2 * i + 1].v[2] = tmp;
        }
        cr_raster_cpu(fb, both, n * 2, sh);
        free(both);
    }
    free(tris);
}

int main(int argc, char **argv) {
    const char *out = "/tmp/rung4_candidate.ppm";
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--set") && i + 1 < argc) {
            const char *kv = argv[++i];
            const char *eq = strchr(kv, '=');
            if (!eq || eq == kv) {
                fprintf(stderr, "bad --set %s (want key=value)\n", kv);
                return 2;
            }
            char key[64];
            size_t klen = (size_t)(eq - kv);
            if (klen >= sizeof key) {
                fprintf(stderr, "bad --set %s: key too long\n", kv);
                return 2;
            }
            memcpy(key, kv, klen);
            key[klen] = '\0';
            int rc = cr_cfg_set(key, eq + 1);
            if (rc != 0) {
                fprintf(stderr, "error: --set %s: %s\n", kv,
                        rc == -1 ? "unknown key" : "bad value for this key");
                return 2;
            }
        } else if (argv[i][0] != '-') {
            out = argv[i];
        } else {
            fprintf(stderr, "unknown arg %s\n", argv[i]);
            return 2;
        }
    }
    const float pi = 3.14159265358979323846f;

    CrCamera cam;
    memset(&cam, 0, sizeof(cam));
    cam.pos.x = EYE_X;
    cam.pos.y = EYE_Y;
    cam.pos.z = EYE_Z;
    cam.yaw = YAW_DEG * (pi / 180.0f);
    cam.pitch = PITCH_DEG * (pi / 180.0f);
    cam.fov_deg = FOV_DEG;
    cam.aspect = (float)FB_W / (float)FB_H;
    cam.znear = 0.05f;
    cam.zfar = ZFAR;

    CrFramebuffer fb;
    cr_fb_alloc(&fb, FB_W, FB_H);
    cr_fb_clear(&fb, (CrRgba){0, 0, 0, 255});
    /* Goldens captured with `time set 6000` -> day fraction 0.25 (noon). */
    gm_sky_draw(&fb, &cam, 0.25f);

    PoseScene scn;
    posescene_init_seed(&scn, &cam, FB_W, FB_H, 0);

    float mc_yaw = 180.0f - YAW_DEG;
    float mc_pitch = -PITCH_DEG;
    printf("POSE x=%.4f y=%.4f z=%.4f "
           "magma_yaw_deg=%.4f magma_pitch_deg=%.4f "
           "mc_yaw=%.4f mc_pitch=%.4f fov=%.1f w=%d h=%d\n",
           cam.pos.x, cam.pos.y, cam.pos.z,
           YAW_DEG, PITCH_DEG, mc_yaw, mc_pitch, FOV_DEG, FB_W, FB_H);
    printf("mesh verts: solid=%d cutout_mipped=%d cutout=%d translucent=%d\n",
           scn.nverts[0], scn.nverts[1], scn.nverts[2], scn.nverts[3]);
    printf("view-distance: center_chunk=(%d,%d) radius=%d chunks | "
           "frustum kept=%d culled=%d of %d\n",
           scn.center_cx, scn.center_cz, scn.view_radius,
           scn.n_kept, scn.n_culled, scn.n_kept + scn.n_culled);

    /* MC terrain fog (EntityRenderer.setupFog(0)) at noon; DEFAULT ON. */
    int fon = gm_terrain_fog_enabled();
    CrRgba fog = gm_terrain_fog_color(0.25f);
    const float fst = GM_TERRAIN_FOG_START, fen = GM_TERRAIN_FOG_END;
    CrShadeCtx sh_solid = {
            .atlas = &scn.atlas, .fog_color = fog, .fog_start = fst,
            .fog_end = fen, .alpha_test = 0, .enable_fog = fon,
            .layer = CR_LAYER_SOLID, .blend = 0, .use_mips = 0,
            .mip_bias = 0.f};
    CrShadeCtx sh_cmip = {
            .atlas = &scn.atlas, .fog_color = fog, .fog_start = fst,
            .fog_end = fen, .alpha_test = 1, .enable_fog = fon,
            .layer = CR_LAYER_CUTOUT_MIPPED, .blend = 0, .use_mips = 1,
            .mip_bias = 0.f};
    CrShadeCtx sh_cut = {
            .atlas = &scn.atlas, .fog_color = fog, .fog_start = fst,
            .fog_end = fen, .alpha_test = 1, .enable_fog = fon,
            .layer = CR_LAYER_CUTOUT, .blend = 0, .use_mips = 0,
            .mip_bias = 0.f};
    CrShadeCtx sh_trans = {
            .atlas = &scn.atlas, .fog_color = fog, .fog_start = fst,
            .fog_end = fen, .alpha_test = 0, .enable_fog = fon,
            .layer = CR_LAYER_TRANSLUCENT, .blend = 1, .use_mips = 0,
            .mip_bias = 0.f};

    render_layer(&fb, &scn, CR_LAYER_SOLID, &sh_solid);
    render_layer(&fb, &scn, CR_LAYER_CUTOUT_MIPPED, &sh_cmip);
    render_layer(&fb, &scn, CR_LAYER_CUTOUT, &sh_cut);
    render_layer(&fb, &scn, CR_LAYER_TRANSLUCENT, &sh_trans);

    unsigned char *rgb = malloc((size_t)FB_W * FB_H * 3);
    for (int i = 0; i < FB_W * FB_H; ++i) {
        rgb[i * 3 + 0] = fb.color[i].r;
        rgb[i * 3 + 1] = fb.color[i].g;
        rgb[i * 3 + 2] = fb.color[i].b;
    }
    if (pscn_write_ppm(out, rgb, FB_W, FB_H)) {
        fprintf(stderr, "write failed\n");
        return 1;
    }
    printf("wrote %s (%dx%d)\n", out, FB_W, FB_H);
    free(rgb);
    cr_fb_free(&fb);
    posescene_free(&scn);
    return 0;
}
