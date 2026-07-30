#ifndef MAGMA_RASTER_METAL_H
#define MAGMA_RASTER_METAL_H

#include "core/types.h"
#include "game/sky.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CrMetalRaster CrMetalRaster;

CrMetalRaster *cr_metal_raster_create(int width, int height, int max_tris,
                                      char *err, int err_cap);
void cr_metal_raster_destroy(CrMetalRaster *raster);
int cr_metal_raster_frame_begin(CrMetalRaster *raster,
                                const CrFramebuffer *fb,
                                char *err, int err_cap);
int cr_metal_raster_draw(CrMetalRaster *raster, const CrScreenTri *tris,
                         int ntris, const CrShadeCtx *shade,
                         char *err, int err_cap);
int cr_metal_raster_sky(CrMetalRaster *raster, const GmSkyCtx *sky,
                        const float basis[11], int width, int height,
                        char *err, int err_cap);
int cr_metal_raster_frame_end(CrMetalRaster *raster, CrFramebuffer *fb,
                              char *err, int err_cap);
void cr_metal_raster_atlas_dirty(CrMetalRaster *raster);

#ifdef __cplusplus
}
#endif
#endif
