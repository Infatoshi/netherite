/* CANDIDATE: rasterize the SAME shared scene with our cr_raster_cpu, applying the
 * identical OpenGL viewport mapping the golden uses, then dump a top-down PPM to
 * diff against the OSMesa golden. Isolates the triangle->pixel kernel.
 *
 * Now multi-layer: SOLID -> CUTOUT (alpha test) -> TRANSLUCENT (src-over blend,
 * no depth write) -> mipped SOLID, driven with a matching CrShadeCtx per layer,
 * in the same draw order and sharing the same atlas + mip chain as the golden. */
#include <stdio.h>
#include <stdlib.h>
#include "core/types.h"
#include "../verify/scene.h"

/* GL viewport map: clip -> our top-down pixel space + depth [0,1]. */
static CrScreenVert to_screen(const ScnVert *v) {
    float invw = 1.0f / v->w;
    float nx = v->x * invw, ny = v->y * invw, nz = v->z * invw;
    CrScreenVert s;
    s.spos.x = (nx * 0.5f + 0.5f) * (float)SCN_W;
    s.spos.y = (0.5f - 0.5f * ny) * (float)SCN_H; /* GL bottom-up -> our top-down */
    s.spos.z = nz * 0.5f + 0.5f;
    s.invw   = invw;
    s.uv_w.x = v->u * invw;
    s.uv_w.y = v->v * invw;
    s.light_w = v->light * invw;
    s.ao_w    = 1.0f * invw;
    s.eye_dist_w = v->w * invw;
    /* CrScreenVert tint is perspective-correct attr*invw (not CrRgba). */
    s.tint_r_w = 255.0f * invw;
    s.tint_g_w = 255.0f * invw;
    s.tint_b_w = 255.0f * invw;
    s.tint_a_w = 255.0f * invw;
    s.blk_w = 0.0f;
    return s;
}

/* Rasterize one vertex group. GL draws both windings (cull disabled); our raster
 * culls one, so emit each triangle AND its reverse so exactly one survives. */
static void draw_group(CrFramebuffer *fb, const ScnVert *verts, int nverts,
                       const CrShadeCtx *sh) {
    int ntri = nverts / 3;
    CrScreenTri *tris = malloc(sizeof(CrScreenTri) * (size_t)ntri * 2);
    int n = 0;
    for (int t = 0; t < ntri; ++t) {
        CrScreenVert a = to_screen(&verts[t*3+0]);
        CrScreenVert b = to_screen(&verts[t*3+1]);
        CrScreenVert c = to_screen(&verts[t*3+2]);
        tris[n].v[0]=a; tris[n].v[1]=b; tris[n].v[2]=c; n++;
        tris[n].v[0]=a; tris[n].v[1]=c; tris[n].v[2]=b; n++;
    }
    cr_raster_cpu(fb, tris, n, sh);
    free(tris);
}

int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : "/tmp/raster_candidate.ppm";

    CrFramebuffer fb;
    cr_fb_alloc(&fb, SCN_W, SCN_H);
    cr_fb_clear(&fb, (CrRgba){0, 0, 0, 255});

    /* atlases (level 0) */
    uint8_t atlas_bytes[ATLAS_W * ATLAS_H * 4];
    uint8_t cut_bytes[ATLAS_W * ATLAS_H * 4];
    uint8_t trans_bytes[ATLAS_W * ATLAS_H * 4];
    scn_fill_atlas(atlas_bytes);
    scn_fill_cutout(cut_bytes);
    scn_fill_translucent(trans_bytes);

    /* shared gamma-correct mip chain for the mipped quad */
    uint8_t m1[8*8*4], m2[4*4*4], m3[2*2*4], m4[1*1*4];
    scn_build_mips(atlas_bytes, m1, m2, m3, m4);

    CrTexture tex_solid = { ATLAS_W, ATLAS_H, (const CrRgba *)atlas_bytes, ATLAS_W, 0, {0}, {0}, {0} };
    CrTexture tex_cut   = { ATLAS_W, ATLAS_H, (const CrRgba *)cut_bytes,   ATLAS_W, 0, {0}, {0}, {0} };
    CrTexture tex_trans = { ATLAS_W, ATLAS_H, (const CrRgba *)trans_bytes, ATLAS_W, 0, {0}, {0}, {0} };
    CrTexture tex_mip   = { 16, 16, (const CrRgba *)atlas_bytes, 16, 4,
                            { (const CrRgba *)m1, (const CrRgba *)m2,
                              (const CrRgba *)m3, (const CrRgba *)m4 },
                            { 8, 4, 2, 1 }, { 8, 4, 2, 1 } };

    CrRgba fog = {135,206,235,255};

    /* 1) SOLID */
    CrShadeCtx sh_solid = {
            .atlas = &tex_solid, .fog_color = fog, .fog_start = 0.f,
            .fog_end = 0.f, .alpha_test = 0, .enable_fog = 0,
            .layer = CR_LAYER_SOLID, .blend = 0, .use_mips = 0,
            .mip_bias = 0.f };
    draw_group(&fb, SCN_VERTS, SCN_NVERTS, &sh_solid);

    /* 2) CUTOUT (alpha test) */
    CrShadeCtx sh_cut = {
            .atlas = &tex_cut, .fog_color = fog, .fog_start = 0.f,
            .fog_end = 0.f, .alpha_test = 0, .enable_fog = 0,
            .layer = CR_LAYER_CUTOUT, .blend = 0, .use_mips = 0,
            .mip_bias = 0.f };
    draw_group(&fb, SCN_CUT_VERTS, SCN_CUT_NVERTS, &sh_cut);

    /* 3) TRANSLUCENT (src-over blend, no depth write) */
    CrShadeCtx sh_trans = {
            .atlas = &tex_trans, .fog_color = fog, .fog_start = 0.f,
            .fog_end = 0.f, .alpha_test = 0, .enable_fog = 0,
            .layer = CR_LAYER_TRANSLUCENT, .blend = 1, .use_mips = 0,
            .mip_bias = 0.f };
    draw_group(&fb, SCN_TRANS_VERTS, SCN_TRANS_NVERTS, &sh_trans);

    /* 4) mipped SOLID */
    CrShadeCtx sh_mip = {
            .atlas = &tex_mip, .fog_color = fog, .fog_start = 0.f,
            .fog_end = 0.f, .alpha_test = 0, .enable_fog = 0,
            .layer = CR_LAYER_SOLID, .blend = 0, .use_mips = 1,
            .mip_bias = 0.f };
    draw_group(&fb, SCN_MIP_VERTS, SCN_MIP_NVERTS, &sh_mip);

    uint8_t *rgb = malloc((size_t)SCN_W * SCN_H * 3);
    for (int i = 0; i < SCN_W * SCN_H; ++i) {
        rgb[i*3+0] = fb.color[i].r;
        rgb[i*3+1] = fb.color[i].g;
        rgb[i*3+2] = fb.color[i].b;
    }
    if (scn_write_ppm(out, rgb, SCN_W, SCN_H)) { fprintf(stderr, "write failed\n"); return 1; }
    printf("wrote %s (%dx%d), layers: solid+cutout+translucent+mipped\n", out, SCN_W, SCN_H);
    free(rgb); cr_fb_free(&fb);
    return 0;
}
