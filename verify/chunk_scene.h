/* Shared REAL-CHUNK scene for the end-to-end golden test (verification rung 3).
 *
 * Both the GL golden (chunk_golden.c) and our candidate (chunk_candidate.c)
 * consume this identically, so the ONLY variable between them is the rasterizer
 * (real Mesa GL vs our cr_transform + cr_raster_cpu). It:
 *   - creates a CrWorldMC(seed 0), ensures a radius-2 neighbourhood of chunk
 *     (0,0) (so face culling and lighting are correct), and meshes the 3x3
 *     chunks around the origin,
 *   - concatenates the per-CrRenderLayer CrVertex triangle lists across those 9
 *     chunks into 4 combined world-space vertex arrays,
 *   - exposes the real stitched MC atlas (incl. its gamma-correct mip chain), and
 *   - derives a fixed CrCamera above the terrain looking down at an oblique tilt.
 *
 * The camera matrices are built with the SAME core/math.c helpers cr_transform
 * uses (cr_perspective + cr_look_yaw_pitch), so the golden's glLoadMatrixf path
 * and our transform produce bit-identical clip coordinates; only rasterization
 * differs.  Colour folding, draw order, viewport, clear colour and depth func are
 * matched at the two call sites. */
#ifndef MAGMA_VERIFY_CHUNK_SCENE_H
#define MAGMA_VERIFY_CHUNK_SCENE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "core/types.h"
#include "core/frustum.h"
#include "core/config.h"   /* cr_cfg()->no_cull */
#include "world/mesh_mc.h"

/* VIEW-DISTANCE meshing. We mesh a square Chebyshev-radius neighbourhood of chunks
 * around the CAMERA's chunk and frustum-cull the ones fully outside the view, so
 * the rendered scene fills the frame out to the horizon instead of a small island.
 * Radius is in chunks (16 blocks). 12 ~= MC's default render distance; each chunk
 * needs its 8 neighbours generated for correct edge meshing (ensure radius+1). */
/* Pin to Java renderDistance=8 (fast.yaml). */
#ifndef SCN_VIEW_RADIUS
#define SCN_VIEW_RADIUS 8
#endif

/* Chunk AABB Y span for the frustum test. Full column [0,256] is conservative:
 * it never culls a chunk whose terrain is inside the frustum (no holes). */
#define SCN_AABB_Y_MIN 0.0
#define SCN_AABB_Y_MAX 256.0

typedef struct {
    CrWorldMC *world;
    CrVertex  *verts[4];   /* per CrRenderLayer, concatenated over the kept chunks */
    int        nverts[4];
    CrTexture  atlas;      /* == bm_atlas() (level 0 + mip chain) */
    CrCamera   cam;        /* fixed, derived from the meshed terrain bounds */
    /* view-distance / culling bookkeeping (for reporting + the cull test) */
    int        center_cx, center_cz;
    int        view_radius;
    int        n_kept, n_culled;
} ChunkScene;

/* floor-division of a block coord to its chunk coord (handles negatives). */
static inline int scn__floordiv16(int a) {
    int q = a >> 4;      /* a/16 rounded toward -inf for 16 (power of two) */
    return q;
}

/* Grow a per-layer vertex buffer and append n verts. */
static inline void scn__append(CrVertex **dst, int *n, int *cap,
                               const CrVertex *src, int add) {
    if (add <= 0) return;
    if (*n + add > *cap) {
        int nc = *cap ? *cap : 4096;
        while (*n + add > nc) nc *= 2;
        *dst = (CrVertex *)realloc(*dst, (size_t)nc * sizeof(CrVertex));
        *cap = nc;
    }
    memcpy(*dst + *n, src, (size_t)add * sizeof(CrVertex));
    *n += add;
}

/* Build the scene. W/H are the framebuffer dims (drive aspect + camera). */
static inline void chunkscene_init(ChunkScene *s, int W, int H) {
    memset(s, 0, sizeof(*s));
    s->world = worldmc_create(0);

    /* FROZEN pose (set FIRST so the frustum can be built from it). The rung-4
     * golden (mc_capture/mc_frame.png) was captured with the live MC camera
     * teleported to magma's PRINTED pose, which at capture time was the
     * terrain-derived eye (XZ centroid 8.2994, terrain maxY 79 + 16 = 95, z 40).
     * That pose MUST stay constant across mesh changes: once wave-6 added tree
     * decoration, a mesh-derived `maxY` jumped to the tallest LOG/leaf (~82 ->
     * eye 98), sliding the whole frame up over more sky and desyncing from the
     * fixed golden. Freezing the eye keeps the candidate registered to the golden
     * regardless of what geometry the mesher now emits (and a future re-capture
     * reads back this same printed pose, so it stays self-consistent). rung-3
     * (golden GL vs candidate) uses this same cam, so it is unaffected. Widening
     * the meshed region below does NOT move the eye, so registration holds. */
    const float pi = 3.14159265358979323846f;
    s->cam.pos.x  = 8.2994f;
    s->cam.pos.y  = 95.0f;
    s->cam.pos.z  = 40.0f;
    s->cam.yaw    = 0.0f;
    s->cam.pitch  = -35.0f * (pi / 180.0f); /* MC: negative pitch tilts view down */
    s->cam.fov_deg = 70.0f;
    s->cam.aspect  = (float)W / (float)H;   /* transform re-derives from fb dims */
    s->cam.znear   = 0.05f;
    s->cam.zfar    = 600.0f;

    /* Mesh a VIEW-DISTANCE radius of chunks around the camera's chunk, so the
     * scene fills the frame instead of a small island floating in sky-void. */
    const int R = SCN_VIEW_RADIUS;
    const int ccx = scn__floordiv16((int)floorf(s->cam.pos.x));
    const int ccz = scn__floordiv16((int)floorf(s->cam.pos.z));
    s->center_cx = ccx;
    s->center_cz = ccz;
    s->view_radius = R;

    /* Generate + light every chunk in radius R plus a 1-chunk apron so each meshed
     * chunk has all 8 neighbours for correct face culling / lighting. */
    worldmc_ensure(s->world, ccx, ccz, R + 1);
    s->atlas = worldmc_atlas(s->world);

    /* Build the 6 frustum planes from the SAME matrices cr_transform uses (proj
     * from fb aspect, view from the frozen pose): bit-faithful to MC's
     * ClippingHelperImpl. Chunks whose full-column AABB is outside are skipped. */
    CrMat4 proj = cr_perspective(s->cam.fov_deg, (float)W / (float)H,
                                 s->cam.znear, s->cam.zfar);
    CrMat4 view = cr_look_yaw_pitch(s->cam.pos, s->cam.yaw, s->cam.pitch);
    float planes[6][4];
    cr_frustum_extract(proj.m, view.m, planes);

    /* no_cull=1 meshes EVERY chunk in the radius (frustum test disabled).
     * Used by the cull-correctness test: a culled render must be pixel-identical
     * to the no-cull render over the visible region (culled chunks add no pixels). */
    const int cull_off = cr_cfg()->no_cull;

    int cap[4] = {0, 0, 0, 0};
    for (int cx = ccx - R; cx <= ccx + R; ++cx) {
        for (int cz = ccz - R; cz <= ccz + R; ++cz) {
            double minx = (double)(cx * 16),      maxx = (double)(cx * 16 + 16);
            double minz = (double)(cz * 16),      maxz = (double)(cz * 16 + 16);
            int inside = cull_off ||
                cr_aabb_in_frustum(planes, minx, SCN_AABB_Y_MIN, minz,
                                   maxx, SCN_AABB_Y_MAX, maxz);
            if (!inside) { s->n_culled++; continue; }
            s->n_kept++;

            CrChunkMeshMC m;
            worldmc_mesh_chunk(s->world, cx, cz, &m);
            for (int l = 0; l < 4; ++l)
                scn__append(&s->verts[l], &s->nverts[l], &cap[l],
                            m.verts[l], m.nverts[l]);
            worldmc_free_mesh(&m);
        }
    }
}

static inline void chunkscene_free(ChunkScene *s) {
    for (int l = 0; l < 4; ++l) free(s->verts[l]);
    worldmc_destroy(s->world);
    memset(s, 0, sizeof(*s));
}

/* Camera matrices, built exactly as cr_transform builds them (same math, same
 * aspect from fb dims) so the golden's GL matrices match our transform. */
static inline CrMat4 scn_proj(const ChunkScene *s, int W, int H) {
    return cr_perspective(s->cam.fov_deg, (float)W / (float)H,
                          s->cam.znear, s->cam.zfar);
}
static inline CrMat4 scn_view(const ChunkScene *s) {
    return cr_look_yaw_pitch(s->cam.pos, s->cam.yaw, s->cam.pitch);
}

/* Write an RGB PPM (P6), top-down (row 0 = top). */
static inline int scn_write_ppm(const char *path, const unsigned char *rgb,
                                int w, int h) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    fwrite(rgb, 1, (size_t)w * h * 3, f);
    fclose(f);
    return 0;
}

#endif /* MAGMA_VERIFY_CHUNK_SCENE_H */
