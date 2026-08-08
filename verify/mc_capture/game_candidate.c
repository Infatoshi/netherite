/* game_candidate - render the seed-0 view-distance world through magma at an
 * ARBITRARY camera pose (from argv) and dump a PPM to pixel-diff against a live
 * Minecraft frame captured at the SAME pose. This is rung4_candidate.c generalized
 * off chunk_scene.h's single frozen pose: it drives pose_scene.h's view-distance
 * mesher from a caller-supplied CrCamera.
 *
 * Convention: yaw/pitch here are MAGMA convention (core/math.c) in DEGREES.
 *   forward = (-sin(yaw)cos(pitch), sin(pitch), -cos(yaw)cos(pitch));
 *   yaw 0 looks toward -Z, POSITIVE pitch looks UP.
 * The caller (run_game_verify.sh) converts an MC pose read from pose.json into
 * this convention via magma_yaw = 180 - MC_yaw, magma_pitch = -MC_pitch, so a
 * frame rendered here registers to the MC golden captured at MC (yaw,pitch).
 *
 * Running it with the frozen rung-4 pose
 *   --eye 8.2994 95 40 --yaw 0 --pitch -35 --fov 70 --w 854 --h 480
 * reproduces rung4_candidate's mesh + render exactly (pose 0 == rung 4).
 *
 * Default: single winding (matches game_main). Opt-in dual via MAGMA_DUAL_WIND=1
 * (GL-parity both-windings path); our raster still keeps one front face per pair.
 *
 * Usage:
 *   game_candidate --eye X Y Z --yaw DEG --pitch DEG [--fov 70]
 *                  [--w 854 --h 480] [--ppm PATH]
 * (--ppm may also be given as the first positional arg for rung4 parity.)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "core/types.h"
#include "game/sky.h"
#include "../verify/mc_capture/pose_scene.h"

static void render_layer(CrFramebuffer *fb, const PoseScene *s, int W, int H,
                         int layer, const CrShadeCtx *sh) {
    int nv = s->nverts[layer];
    if (nv < 3) return;
    int max_tris = (nv / 3) * 2;
    CrScreenTri *tris = malloc(sizeof(CrScreenTri) * (size_t)max_tris);
    int n = cr_transform(s->verts[layer], nv, NULL, 0, &s->cam,
                         W, H, tris, max_tris);
    /* Dual winding is a hard-scene no-op (crop delta << 0.1 vs single wind).
     * game_main never dual-winds. Default OFF; MAGMA_DUAL_WIND=1 restores the
     * old GL-parity "both windings" path. MAGMA_NO_DUAL_WIND=1 still forces off. */
    int dual = 0;
    {
        const char *s = getenv("MAGMA_DUAL_WIND");
        if (s && atoi(s) != 0) dual = 1;
        s = getenv("MAGMA_NO_DUAL_WIND");
        if (s && atoi(s) != 0) dual = 0;
    }
    if (!dual) {
        cr_raster_cpu(fb, tris, n, sh);
    } else {
        CrScreenTri *both = malloc(sizeof(CrScreenTri) * (size_t)n * 2);
        for (int i = 0; i < n; ++i) {
            both[2*i] = tris[i];
            both[2*i+1] = tris[i];
            CrScreenVert tmp = both[2*i+1].v[1];
            both[2*i+1].v[1] = both[2*i+1].v[2];
            both[2*i+1].v[2] = tmp;
        }
        cr_raster_cpu(fb, both, n * 2, sh);
        free(both);
    }
    free(tris);
}

int main(int argc, char **argv) {
    const float pi = 3.14159265358979323846f;

    /* Defaults == the frozen rung-4 pose so a bare run reproduces rung 4. */
    float eye_x = 8.2994f, eye_y = 95.0f, eye_z = 40.0f;
    float yaw_deg = 0.0f, pitch_deg = -35.0f, fov_deg = 70.0f;
    int   W = 854, H = 480;
    long long seed = 0;
    const char *out = NULL;
    const char *depth_out = NULL; /* optional float32 LE depth dump (w*h) */

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--eye") && i + 3 < argc) {
            eye_x = (float)atof(argv[++i]);
            eye_y = (float)atof(argv[++i]);
            eye_z = (float)atof(argv[++i]);
        } else if (!strcmp(argv[i], "--yaw") && i + 1 < argc) {
            yaw_deg = (float)atof(argv[++i]);
        } else if (!strcmp(argv[i], "--pitch") && i + 1 < argc) {
            pitch_deg = (float)atof(argv[++i]);
        } else if (!strcmp(argv[i], "--fov") && i + 1 < argc) {
            fov_deg = (float)atof(argv[++i]);
        } else if (!strcmp(argv[i], "--w") && i + 1 < argc) {
            W = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--h") && i + 1 < argc) {
            H = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--seed") && i + 1 < argc) {
            seed = atoll(argv[++i]);
        } else if (!strcmp(argv[i], "--ppm") && i + 1 < argc) {
            out = argv[++i];
        } else if (!strcmp(argv[i], "--depth") && i + 1 < argc) {
            depth_out = argv[++i];
        } else if (argv[i][0] != '-' && !out) {
            out = argv[i];   /* positional PPM path, rung4 parity */
        }
    }
    if (!out) out = "/tmp/game_candidate.ppm";

    CrCamera cam;
    memset(&cam, 0, sizeof(cam));
    cam.pos.x = eye_x; cam.pos.y = eye_y; cam.pos.z = eye_z;
    cam.yaw   = yaw_deg   * (pi / 180.0f);
    cam.pitch = pitch_deg * (pi / 180.0f);
    cam.fov_deg = fov_deg;
    cam.aspect  = (float)W / (float)H;
    cam.znear   = 0.05f;
    /* EntityRenderer.setupCameraTransform: far = renderDistanceChunks*16 * sqrt(2).
     * Capture pack RD=8 -> 128 * 1.41421356 = 181.01933 (camera.json zfar). */
    cam.zfar    = 181.01933f;

    CrFramebuffer fb;
    cr_fb_alloc(&fb, W, H);
    cr_fb_clear(&fb, (CrRgba){0, 0, 0, 255});
    /* Goldens are captured by capture.sh/capture_poses.sh with `time set 6000`,
     * so the MC day fraction is 6000/24000 = 0.25 (noon). Draw real sky first;
     * terrain is rasterized on top against the far-depth clear. */
    gm_sky_draw(&fb, &cam, 0.25f);

    PoseScene scn;
    posescene_init_seed(&scn, &cam, W, H, seed);

    /* MC-convention yaw/pitch that reproduces this magma pose (for the operator
     * / capture side): magma_yaw = 180 - MC_yaw, magma_pitch = -MC_pitch. */
    float mc_yaw   = 180.0f - yaw_deg;
    float mc_pitch = -pitch_deg;
    printf("POSE seed=%lld x=%.4f y=%.4f z=%.4f "
           "magma_yaw_deg=%.4f magma_pitch_deg=%.4f "
           "mc_yaw=%.4f mc_pitch=%.4f fov=%.1f w=%d h=%d\n",
           seed, cam.pos.x, cam.pos.y, cam.pos.z, yaw_deg, pitch_deg,
           mc_yaw, mc_pitch, fov_deg, W, H);
    printf("mesh verts: solid=%d cutout_mipped=%d cutout=%d translucent=%d\n",
           scn.nverts[0], scn.nverts[1], scn.nverts[2], scn.nverts[3]);
    printf("view-distance: center_chunk=(%d,%d) radius=%d chunks | "
           "frustum kept=%d culled=%d of %d\n",
           scn.center_cx, scn.center_cz, scn.view_radius,
           scn.n_kept, scn.n_culled, scn.n_kept + scn.n_culled);

    /* MC terrain fog (EntityRenderer.setupFog(0)): GL_LINEAR, start=128*0.75=96,
     * end=128 (goldens captured at renderDistance 8; see game/sky.h), color =
     * updateFogColor view-fog at noon (0.25, matches the goldens' `time set 6000`
     * and the sky pass above). Applied to all layers; TRANSLUCENT (water) fogs
     * toward the same color before src-over blend. DEFAULT ON (MAGMA_FOG=0 off).
     * Live on hard-scene far canopy; may be no-op on short near-only poses. */
    int    fon = gm_terrain_fog_enabled();
    CrRgba fog = gm_terrain_fog_color(0.25f);
    const float fst = GM_TERRAIN_FOG_START, fen = GM_TERRAIN_FOG_END;
    CrShadeCtx sh_solid = {
            .atlas = &scn.atlas, .fog_color = fog, .fog_start = fst,
            .fog_end = fen, .alpha_test = 0, .enable_fog = fon,
            .layer = CR_LAYER_SOLID, .blend = 0, .use_mips = 0,
            .mip_bias = 0.f };
    CrShadeCtx sh_cmip  = {
            .atlas = &scn.atlas, .fog_color = fog, .fog_start = fst,
            .fog_end = fen, .alpha_test = 1, .enable_fog = fon,
            .layer = CR_LAYER_CUTOUT_MIPPED, .blend = 0, .use_mips = 1,
            .mip_bias = 0.f };
    CrShadeCtx sh_cut   = {
            .atlas = &scn.atlas, .fog_color = fog, .fog_start = fst,
            .fog_end = fen, .alpha_test = 1, .enable_fog = fon,
            .layer = CR_LAYER_CUTOUT, .blend = 0, .use_mips = 0,
            .mip_bias = 0.f };
    CrShadeCtx sh_trans = {
            .atlas = &scn.atlas, .fog_color = fog, .fog_start = fst,
            .fog_end = fen, .alpha_test = 0, .enable_fog = fon,
            .layer = CR_LAYER_TRANSLUCENT, .blend = 1, .use_mips = 0,
            .mip_bias = 0.f };

    render_layer(&fb, &scn, W, H, CR_LAYER_SOLID,         &sh_solid);
    if (!getenv("MAGMA_NO_CUTOUT")) {
        render_layer(&fb, &scn, W, H, CR_LAYER_CUTOUT_MIPPED, &sh_cmip);
        render_layer(&fb, &scn, W, H, CR_LAYER_CUTOUT,        &sh_cut);
    }
    if (!getenv("MAGMA_NO_TRANS"))
        render_layer(&fb, &scn, W, H, CR_LAYER_TRANSLUCENT,   &sh_trans);

    unsigned char *rgb = malloc((size_t)W * H * 3);
    for (int i = 0; i < W * H; ++i) {
        rgb[i*3+0] = fb.color[i].r;
        rgb[i*3+1] = fb.color[i].g;
        rgb[i*3+2] = fb.color[i].b;
    }
    if (pscn_write_ppm(out, rgb, W, H)) {
        fprintf(stderr, "write failed\n"); return 1;
    }
    printf("wrote %s (%dx%d)\n", out, W, H);
    if (depth_out) {
        FILE *df = fopen(depth_out, "wb");
        if (!df) {
            perror(depth_out);
            free(rgb); cr_fb_free(&fb); posescene_free(&scn);
            return 1;
        }
        size_t n = (size_t)W * (size_t)H;
        if (fwrite(fb.depth, sizeof(float), n, df) != n) {
            fprintf(stderr, "depth write failed\n");
            fclose(df);
            free(rgb); cr_fb_free(&fb); posescene_free(&scn);
            return 1;
        }
        fclose(df);
        printf("wrote %s (float32 depth %dx%d)\n", depth_out, W, H);
    }
    free(rgb); cr_fb_free(&fb);
    posescene_free(&scn);
    return 0;
}
