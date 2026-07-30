#ifndef MAGMA_RASTER_BACKEND_H
#define MAGMA_RASTER_BACKEND_H

#include "core/types.h"
#include "game/config.h"
#include "game/sky.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CrRasterBackend CrRasterBackend;

enum {
    CR_BACKEND_CAP_GPU = 1u << 0,
    CR_BACKEND_CAP_SKY = 1u << 1
};

/* Backends compiled into this binary. CPU is present in every magma game. */
unsigned cr_backend_compiled_mask(void);

/* Create one process-owned backend with persistent storage sized for the game. */
int cr_backend_open(CrRasterBackend **out, GmBackend kind, int width, int height,
                    int max_tris, char *err, int err_cap);
void cr_backend_close(CrRasterBackend *backend);

unsigned cr_backend_caps(const CrRasterBackend *backend);
const char *cr_backend_last_error(const CrRasterBackend *backend);

/* A frame keeps accelerated color/depth resident between begin and end. */
int cr_backend_frame_begin(CrRasterBackend *backend, const CrFramebuffer *fb);
int cr_backend_raster(CrRasterBackend *backend, CrFramebuffer *fb,
                      const CrScreenTri *tris, int ntris,
                      const CrShadeCtx *shade);
int cr_backend_sky(CrRasterBackend *backend, const GmSkyCtx *sky,
                   const float basis[11], int width, int height);
int cr_backend_frame_end(CrRasterBackend *backend, CrFramebuffer *fb);
void cr_backend_atlas_dirty(CrRasterBackend *backend);

#ifdef __cplusplus
}
#endif
#endif
