/* CPU vs Metal rasterizer parity across all render layers.
 *
 * Metal analog of tests/test_raster_parity.c (the CPU-vs-CUDA rung-1 gate):
 * renders the shared verify scene's layer groups (SOLID, CUTOUT, TRANSLUCENT,
 * mipped SOLID, radial fog) with cr_raster_cpu and the Metal backend from the
 * SAME cleared framebuffer and compares color+depth byte-for-byte.
 *
 * Contract: identical to the CUDA gate. SOLID and CUTOUT must be BIT-EXACT.
 * TRANSLUCENT (blend) and MIPPED use only deterministic float ops and the
 * fixed-order polynomial log2, so they are required to be 0 too. Host C is
 * compiled with -ffp-contract=off (the clang analog of nvcc --fmad=false, see
 * make/metal.mk); the shader side must be written to the same no-fma,
 * fixed-order discipline or this gate is expected to fail.
 *
 * Entry-point note: the CUDA gate calls the bare per-call cr_raster_cuda().
 * The Metal backend exposes only the cr_raster_metal_* set (team contract:
 * mirrors of cuda/raster_cuda.cu lines 595-1262, which have no bare per-call
 * twin), so this gate drives the alloc-once path the game itself uses:
 * cr_raster_metal_pre() once, cr_raster_metal_into() per layer render (host
 * fb up, raster, host fb back - no frame open), cr_raster_metal_post() at
 * exit. Semantics of into() outside frame_begin/frame_end are a full fb round
 * trip per call, exactly like cr_raster_cuda_into(), so the comparison is the
 * same as the CUDA gate's.
 *
 * UNVERIFIED until it passes on the MacBook (scripts/mac_metal_verify.sh);
 * on Linux this file is only compile-checked.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/types.h"
#include "../verify/scene.h"
/* Full cr_raster_metal_* surface (types.h carries only the shared subset). */
#include "metal_api_decls.h"

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

    /* Alloc-once device state; 64 matches the tris[] scratch in render(). */
    cr_raster_metal_pre(SCN_W, SCN_H, 64);

    int fail = 0;
    for (int i = 0; i < (int)(sizeof(cases)/sizeof(cases[0])); ++i) {
        render(cr_raster_cpu,        &fc, cases[i].v, cases[i].nv, &cases[i].sh);
        render(cr_raster_metal_into, &fg, cases[i].v, cases[i].nv, &cases[i].sh);
        int depth_bad = 0;
        int maxd = compare(&fc, &fg, &depth_bad);
        int ok = (maxd == 0) && (cases[i].sh.blend ? 1 : depth_bad == 0);
        /* blend suppresses depth writes on both paths, so depth matches regardless;
         * for non-blend layers depth must match too. */
        if (!ok) fail = 1;
        printf("%-12s CPU==METAL maxdiff=%d depth_mismatch=%d -> %s\n",
               cases[i].name, maxd, depth_bad, ok ? "PASS" : "FAIL");
    }

    cr_raster_metal_post();
    cr_fb_free(&fc); cr_fb_free(&fg);
    printf("%s\n", fail ? "PARITY FAIL" : "PARITY PASS (all layers bit-exact CPU==METAL)");
    return fail;
}
