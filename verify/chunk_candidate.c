/* CANDIDATE (rung 3): render the REAL 3x3-chunk scene through OUR pipeline
 * (cr_transform + cr_raster_cpu) with a per-layer CrShadeCtx matching the
 * golden's GL state, then dump a top-down PPM to diff against chunk_golden.
 *
 * The golden draws with GL culling DISABLED, so for parity we submit each
 * transformed triangle AND its reverse: our raster culls one winding, so exactly
 * one survives and the same visible triangle is rasterized in both paths. */
#include <stdio.h>
#include <stdlib.h>

#include "core/types.h"
#include "../verify/chunk_scene.h"

#define FB_W 512
#define FB_H 512

/* Transform one layer's world verts, then raster both windings of each tri. */
static void render_layer(CrFramebuffer *fb, const ChunkScene *s,
                         int layer, const CrShadeCtx *sh) {
    int nv = s->nverts[layer];
    if (nv < 3) return;
    int max_tris = (nv / 3) * 2;              /* near-clip can split 1 -> 2 */
    CrScreenTri *tris = malloc(sizeof(CrScreenTri) * (size_t)max_tris);
    int n = cr_transform(s->verts[layer], nv, NULL, 0, &s->cam,
                         FB_W, FB_H, tris, max_tris);

    CrScreenTri *both = malloc(sizeof(CrScreenTri) * (size_t)n * 2);
    for (int i = 0; i < n; ++i) {
        both[2*i] = tris[i];
        both[2*i+1] = tris[i];
        CrScreenVert tmp = both[2*i+1].v[1];  /* reverse winding */
        both[2*i+1].v[1] = both[2*i+1].v[2];
        both[2*i+1].v[2] = tmp;
    }
    cr_raster_cpu(fb, both, n * 2, sh);
    free(both);
    free(tris);
}

int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : "/tmp/chunk_candidate.ppm";

    CrFramebuffer fb;
    cr_fb_alloc(&fb, FB_W, FB_H);
    cr_fb_clear(&fb, (CrRgba){135, 206, 235, 255}); /* sky, matches golden */

    ChunkScene scn;
    chunkscene_init(&scn, FB_W, FB_H);
    printf("mesh verts: solid=%d cutout_mipped=%d cutout=%d translucent=%d\n",
           scn.nverts[0], scn.nverts[1], scn.nverts[2], scn.nverts[3]);

    CrRgba fog = {135, 206, 235, 255};

    /* Per-layer CrShadeCtx, designated so a new struct field cannot silently
     * shift the slots (that regression discarded every CUTOUT texel once
     * alpha_ref was inserted). Fog off throughout. */
    CrShadeCtx sh_solid = {
            .atlas = &scn.atlas, .fog_color = fog, .fog_start = 0.f,
            .fog_end = 0.f, .alpha_test = 0, .enable_fog = 0,
            .layer = CR_LAYER_SOLID, .blend = 0, .use_mips = 0,
            .mip_bias = 0.f };
    CrShadeCtx sh_cmip  = {
            .atlas = &scn.atlas, .fog_color = fog, .fog_start = 0.f,
            .fog_end = 0.f, .alpha_test = 1, .enable_fog = 0,
            .layer = CR_LAYER_CUTOUT_MIPPED, .blend = 0, .use_mips = 1,
            .mip_bias = 0.f };
    CrShadeCtx sh_cut   = {
            .atlas = &scn.atlas, .fog_color = fog, .fog_start = 0.f,
            .fog_end = 0.f, .alpha_test = 1, .enable_fog = 0,
            .layer = CR_LAYER_CUTOUT, .blend = 0, .use_mips = 0,
            .mip_bias = 0.f };
    CrShadeCtx sh_trans = {
            .atlas = &scn.atlas, .fog_color = fog, .fog_start = 0.f,
            .fog_end = 0.f, .alpha_test = 0, .enable_fog = 0,
            .layer = CR_LAYER_TRANSLUCENT, .blend = 1, .use_mips = 0,
            .mip_bias = 0.f };

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
