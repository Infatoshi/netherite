/* metal_api_decls.h - full cr_raster_metal_* prototype surface for the
 * parity test. core/types.h deliberately declares only the shared subset
 * (cr_raster_metal + atlas_dirty, matching the cuda pattern); the remaining
 * entry points are declared at their call sites (game_main.c strong,
 * frame_capture.c weak). The parity test drives pre/into/sky/post directly,
 * so it needs the full set - exact mirrors of the extern "C" entries of
 * cuda/raster_cuda.cu with cuda -> metal renamed. Duplicate compatible
 * declarations with types.h are harmless C.
 */
#ifndef MAGMA_TESTS_METAL_API_DECLS_H
#define MAGMA_TESTS_METAL_API_DECLS_H

#include <stddef.h>
#include "core/types.h"   /* CrFramebuffer, CrScreenTri, CrShadeCtx, CrVertex,
                             CrCamera, CrRgba */
#include "game/sky.h"     /* GmSkyCtx (typedef of an anonymous struct, so it
                             cannot be forward-declared compatibly) */

#ifdef __cplusplus
extern "C" {
#endif

/* Alloc-once lifetime: pre() creates the device framebuffer, triangle buffer,
 * and shade-ctx ring; post() frees them. Mirrors cr_raster_cuda_pre/post. */
void cr_raster_metal_pre(int w, int h, int max_tris);
void cr_raster_metal_post(void);

/* Host mutated atlas texels (water_still animation): force device re-upload. */
void cr_raster_metal_atlas_dirty(void);

/* Async upload fencing (CUDA: event mark/wait on the upload stream). */
void cr_raster_metal_uploads_mark(void);
void cr_raster_metal_uploads_wait(void);

/* Host-buffer pinning hints (CUDA: cudaHostRegister). May be no-ops on
 * unified-memory Apple silicon; kept for API parity. */
void cr_raster_metal_pin(void *p, size_t bytes);
void cr_raster_metal_unpin(void *p);

/* Frame residency: begin uploads the fb once, end downloads it once; layer
 * calls in between skip the per-call fb round trips. */
void cr_raster_metal_frame_begin(const CrFramebuffer *fb);
void cr_raster_metal_sky(const GmSkyCtx *sc, const float *b, int W, int H);
void cr_raster_metal_frame_end(CrFramebuffer *fb);
int  cr_raster_metal_frame_end_async(CrFramebuffer *fb, CrRgba *dst_color);
void cr_raster_metal_frame_wait(void);

/* Per-call raster, alloc-once fast path. Matches the cr_raster_cpu signature:
 * rasterize `tris` into `fb` (host fb up, raster, host fb back unless a frame
 * is open). This is the entry the parity gate exercises. */
void cr_raster_metal_into(CrFramebuffer *fb, const CrScreenTri *tris,
                          int ntris, const CrShadeCtx *sh);

/* On-device transform + raster of a flat vertex stream. */
void cr_raster_metal_render_layer(CrFramebuffer *fb,
                                  const CrVertex *verts, int nverts,
                                  const CrCamera *cam, const CrShadeCtx *sh);

/* Resident vertex slab pool (chunk geometry cached on-device). */
int  cr_raster_metal_slab_pool(int nslots, int slab_verts);
void cr_raster_metal_slab_sync(int slot, int builds,
                               const void *host, int used_verts);
void cr_raster_metal_slabs_reset(void);

/* Gathered render from resident slabs (entities / terrain layer batches). */
void cr_raster_metal_render_gather(CrFramebuffer *fb,
                                   const int *src_vert, const int *nvert,
                                   int nents, int total_verts,
                                   const CrCamera *cam, const CrShadeCtx *sh);
void cr_raster_metal_render_terrain(CrFramebuffer *fb,
                                    const int *src_vert, const int *nvert,
                                    int nents, const int lay_verts[4],
                                    const CrCamera *cam,
                                    const CrShadeCtx sh[4]);

#ifdef __cplusplus
}
#endif
#endif /* MAGMA_TESTS_METAL_API_DECLS_H */
