/* CANDIDATE (rung 4): render the SAME elevated/angled 3x3-chunk seed-0 scene the
 * rung-3 OSMesa verifier uses (raster/verify/chunk_scene.h), but at the REAL MC
 * client window resolution (854x480) and dump a frame to pixel-diff against a
 * captured Minecraft frame (mc_frame.png) at the MATCHING pose.
 *
 * This is deliberately the rung-3 ChunkScene camera (elevated 16 blocks above the
 * terrain, south of centre, yaw 0 / pitch -35deg looking down toward -Z over open
 * ground) so the magma frame and the MC frame are directly comparable. The one
 * job here is to (a) print the resolved WORLD pose so mc_capture/capture.sh can
 * teleport the live MC camera to the identical spot, and (b) emit a PPM the
 * whole-frame diff harness consumes.
 *
 * MC vs magma camera convention (see core/math.c header + capture.sh):
 *   magma forward = (-sin(yaw)cos(pitch), sin(pitch), -cos(yaw)cos(pitch));
 *   yaw 0 looks toward -Z, POSITIVE pitch looks UP. The scene uses yaw 0,
 *   pitch -35deg (toward -Z, tilted down). The MC look vector
 *   x=-sin(Y)cos(P), y=-sin(P), z=cos(Y)cos(P) matches that forward at
 *   MC yaw 180, MC pitch +35 (MC: yaw 180 faces -Z, positive pitch looks down).
 *
 * Like chunk_candidate.c the golden GL path culls nothing, so we submit each
 * transformed tri and its reverse; our raster keeps exactly one winding.
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "core/types.h"
#include "raster/verify/chunk_scene.h"

/* Real MC client window content region (see mc_capture/pose.json width/height). */
#define FB_W 854
#define FB_H 480

static void render_layer(CrFramebuffer *fb, const ChunkScene *s,
                         int layer, const CrShadeCtx *sh) {
    int nv = s->nverts[layer];
    if (nv < 3) return;
    int max_tris = (nv / 3) * 2;
    CrScreenTri *tris = malloc(sizeof(CrScreenTri) * (size_t)max_tris);
    int n = cr_transform(s->verts[layer], nv, NULL, 0, &s->cam,
                         FB_W, FB_H, tris, max_tris);
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
    free(tris);
}

int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : "/tmp/rung4_candidate.ppm";

    CrFramebuffer fb;
    cr_fb_alloc(&fb, FB_W, FB_H);
    /* Sky-blue clear so the void beyond the 3x3 island roughly reads as MC sky. */
    cr_fb_clear(&fb, (CrRgba){135, 206, 235, 255});

    ChunkScene scn;
    chunkscene_init(&scn, FB_W, FB_H);

    /* Resolved world pose + the MC-convention yaw/pitch that reproduces it. */
    const float pi = 3.14159265358979323846f;
    float mc_yaw   = 180.0f;                        /* -Z heading in MC */
    float mc_pitch = -scn.cam.pitch * (180.0f/pi);  /* magma -35 -> MC +35 (down) */
    printf("POSE x=%.4f y=%.4f z=%.4f "
           "magma_yaw_deg=%.4f magma_pitch_deg=%.4f "
           "mc_yaw=%.4f mc_pitch=%.4f fov=%.1f w=%d h=%d\n",
           scn.cam.pos.x, scn.cam.pos.y, scn.cam.pos.z,
           scn.cam.yaw * (180.0f/pi), scn.cam.pitch * (180.0f/pi),
           mc_yaw, mc_pitch, scn.cam.fov_deg, FB_W, FB_H);
    printf("mesh verts: solid=%d cutout_mipped=%d cutout=%d translucent=%d\n",
           scn.nverts[0], scn.nverts[1], scn.nverts[2], scn.nverts[3]);
    printf("view-distance: center_chunk=(%d,%d) radius=%d chunks | "
           "frustum kept=%d culled=%d of %d\n",
           scn.center_cx, scn.center_cz, scn.view_radius,
           scn.n_kept, scn.n_culled, scn.n_kept + scn.n_culled);

    CrRgba fog = {135, 206, 235, 255};
    CrShadeCtx sh_solid = { &scn.atlas, fog, 0.f, 0.f, 0, 0,
                            CR_LAYER_SOLID, 0, 0, 0.f };
    CrShadeCtx sh_cmip  = { &scn.atlas, fog, 0.f, 0.f, 1, 0,
                            CR_LAYER_CUTOUT_MIPPED, 0, 1, 0.f };
    CrShadeCtx sh_cut   = { &scn.atlas, fog, 0.f, 0.f, 1, 0,
                            CR_LAYER_CUTOUT, 0, 0, 0.f };
    CrShadeCtx sh_trans = { &scn.atlas, fog, 0.f, 0.f, 0, 0,
                            CR_LAYER_TRANSLUCENT, 1, 0, 0.f };

    render_layer(&fb, &scn, CR_LAYER_SOLID,          &sh_solid);
    render_layer(&fb, &scn, CR_LAYER_CUTOUT_MIPPED,  &sh_cmip);
    render_layer(&fb, &scn, CR_LAYER_CUTOUT,         &sh_cut);
    render_layer(&fb, &scn, CR_LAYER_TRANSLUCENT,    &sh_trans);

    unsigned char *rgb = malloc((size_t)FB_W * FB_H * 3);
    for (int i = 0; i < FB_W * FB_H; ++i) {
        rgb[i*3+0] = fb.color[i].r;
        rgb[i*3+1] = fb.color[i].g;
        rgb[i*3+2] = fb.color[i].b;
    }
    if (scn_write_ppm(out, rgb, FB_W, FB_H)) {
        fprintf(stderr, "write failed\n"); return 1;
    }
    printf("wrote %s (%dx%d)\n", out, FB_W, FB_H);
    free(rgb); cr_fb_free(&fb);
    chunkscene_free(&scn);
    return 0;
}
