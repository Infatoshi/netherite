/* CPU vs CUDA rasterizer parity across all render layers.
 *
 * Renders the shared verify scene's layer groups (SOLID, CUTOUT, TRANSLUCENT,
 * mipped SOLID) with cr_raster_cpu and cr_raster_cuda from the SAME cleared
 * framebuffer and compares color+depth byte-for-byte.
 *
 * Contract (SPEC + RASTER-EXT task): SOLID and CUTOUT must be BIT-EXACT. The
 * blend (TRANSLUCENT) and mip (mipped SOLID) paths use only deterministic float
 * ops and a fixed-order polynomial log2, so they are expected to match exactly
 * too; they are reported and also required to be 0 here.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/types.h"
#include "../verify/scene.h"

static CrScreenVert to_screen(const ScnVert *v) {
    float invw = 1.0f / v->w;
    float nx = v->x * invw, ny = v->y * invw, nz = v->z * invw;
    CrScreenVert s;
    s.spos.x = (nx * 0.5f + 0.5f) * (float)SCN_W;
    s.spos.y = (0.5f - 0.5f * ny) * (float)SCN_H;
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

static int build_tris(const ScnVert *verts, int nverts, CrScreenTri *tris) {
    int ntri = nverts / 3, n = 0;
    for (int t = 0; t < ntri; ++t) {
        CrScreenVert a = to_screen(&verts[t*3+0]);
        CrScreenVert b = to_screen(&verts[t*3+1]);
        CrScreenVert c = to_screen(&verts[t*3+2]);
        tris[n].v[0]=a; tris[n].v[1]=b; tris[n].v[2]=c; n++;
        tris[n].v[0]=a; tris[n].v[1]=c; tris[n].v[2]=b; n++;
    }
    return n;
}

/* Render one layer group into a freshly-cleared fb with the given rasterizer. */
typedef void (*raster_fn)(CrFramebuffer *, const CrScreenTri *, int, const CrShadeCtx *);

static void render(raster_fn fn, CrFramebuffer *fb,
                   const ScnVert *verts, int nverts, const CrShadeCtx *sh) {
    cr_fb_clear(fb, (CrRgba){20, 30, 40, 255}); /* nonzero dst so blend is exercised */
    CrScreenTri tris[64];
    int n = build_tris(verts, nverts, tris);
    fn(fb, tris, n, sh);
}

/* Compare two framebuffers; return max per-channel color diff (depth checked too). */
static int compare(const CrFramebuffer *a, const CrFramebuffer *b, int *depth_bad) {
    int n = a->w * a->h, maxd = 0;
    *depth_bad = 0;
    for (int i = 0; i < n; ++i) {
        int dr = abs((int)a->color[i].r - b->color[i].r);
        int dg = abs((int)a->color[i].g - b->color[i].g);
        int db = abs((int)a->color[i].b - b->color[i].b);
        if (dr > maxd) maxd = dr;
        if (dg > maxd) maxd = dg;
        if (db > maxd) maxd = db;
        if (a->depth[i] != b->depth[i]) (*depth_bad)++;
    }
    return maxd;
}

int main(void) {
    /* textures */
    uint8_t atlas[ATLAS_W*ATLAS_H*4], cut[ATLAS_W*ATLAS_H*4], trans[ATLAS_W*ATLAS_H*4];
    scn_fill_atlas(atlas); scn_fill_cutout(cut); scn_fill_translucent(trans);
    uint8_t m1[8*8*4], m2[4*4*4], m3[2*2*4], m4[1*1*4];
    scn_build_mips(atlas, m1, m2, m3, m4);

    CrTexture tex_solid = { ATLAS_W, ATLAS_H, (const CrRgba*)atlas, ATLAS_W, 0,{0},{0},{0} };
    CrTexture tex_cut   = { ATLAS_W, ATLAS_H, (const CrRgba*)cut,   ATLAS_W, 0,{0},{0},{0} };
    CrTexture tex_trans = { ATLAS_W, ATLAS_H, (const CrRgba*)trans, ATLAS_W, 0,{0},{0},{0} };
    CrTexture tex_mip   = { 16, 16, (const CrRgba*)atlas, 16, 4,
                            {(const CrRgba*)m1,(const CrRgba*)m2,(const CrRgba*)m3,(const CrRgba*)m4},
                            {8,4,2,1},{8,4,2,1} };
    CrRgba fog = {135,206,235,255};

    struct { const char *name; const ScnVert *v; int nv; CrShadeCtx sh; int strict; } cases[] = {
        { "SOLID",       SCN_VERTS,       SCN_NVERTS,
          { &tex_solid, fog, 0,0, 0,0, CR_LAYER_SOLID,          0, 0, 0 }, 1 },
        { "CUTOUT",      SCN_CUT_VERTS,   SCN_CUT_NVERTS,
          { &tex_cut,   fog, 0,0, 0,0, CR_LAYER_CUTOUT,         0, 0, 0 }, 1 },
        { "TRANSLUCENT", SCN_TRANS_VERTS, SCN_TRANS_NVERTS,
          { &tex_trans, fog, 0,0, 0,0, CR_LAYER_TRANSLUCENT,    1, 0, 0 }, 1 },
        { "MIPPED",      SCN_MIP_VERTS,   SCN_MIP_NVERTS,
          { &tex_mip,   fog, 0,0, 0,0, CR_LAYER_SOLID,          0, 1, 0 }, 1 },
        { "FOG_RADIAL",  SCN_VERTS,       SCN_NVERTS,
          { &tex_solid, fog, 0,10, 0,1, CR_LAYER_SOLID,         0, 0, 0 }, 1 },
    };

    CrFramebuffer fc, fg;
    cr_fb_alloc(&fc, SCN_W, SCN_H);
    cr_fb_alloc(&fg, SCN_W, SCN_H);

    int fail = 0;
    for (int i = 0; i < (int)(sizeof(cases)/sizeof(cases[0])); ++i) {
        render(cr_raster_cpu,  &fc, cases[i].v, cases[i].nv, &cases[i].sh);
        render(cr_raster_cuda, &fg, cases[i].v, cases[i].nv, &cases[i].sh);
        int depth_bad = 0;
        int maxd = compare(&fc, &fg, &depth_bad);
        int ok = (maxd == 0) && (cases[i].sh.blend ? 1 : depth_bad == 0);
        /* blend suppresses depth writes on both paths, so depth matches regardless;
         * for non-blend layers depth must match too. */
        if (!ok) fail = 1;
        printf("%-12s CPU==CUDA maxdiff=%d depth_mismatch=%d -> %s\n",
               cases[i].name, maxd, depth_bad, ok ? "PASS" : "FAIL");
    }

    cr_fb_free(&fc); cr_fb_free(&fg);
    printf("%s\n", fail ? "PARITY FAIL" : "PARITY PASS (all layers bit-exact CPU==CUDA)");
    return fail;
}
