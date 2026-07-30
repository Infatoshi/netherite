#include "raster/backend.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(MAGMA_METAL)
#include "metal/raster_metal.h"
#endif

#if defined(MAGMA_CUDA)
extern void cr_raster_cuda_pre(int w, int h, int max_tris);
extern void cr_raster_cuda_into(CrFramebuffer *fb, const CrScreenTri *tris,
                                int ntris, const CrShadeCtx *shade);
extern void cr_raster_cuda_frame_begin(const CrFramebuffer *fb);
extern void cr_raster_cuda_frame_end(CrFramebuffer *fb);
extern void cr_raster_cuda_sky(const GmSkyCtx *sky, const float *basis,
                               int width, int height);
extern void cr_raster_cuda_atlas_dirty(void);
extern void cr_raster_cuda_post(void);
#endif

struct CrRasterBackend {
    GmBackend kind;
    unsigned caps;
    int width;
    int height;
    int max_tris;
    int frame_open;
    char error[256];
#if defined(MAGMA_METAL)
    CrMetalRaster *metal;
#endif
};

static int backend_fail(CrRasterBackend *b, const char *fmt, ...) {
    if (b) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(b->error, sizeof b->error, fmt, ap);
        va_end(ap);
    }
    return 0;
}

unsigned cr_backend_compiled_mask(void) {
    unsigned mask = GM_BACKEND_BIT(GM_BACKEND_CPU);
#if defined(MAGMA_CUDA)
    mask |= GM_BACKEND_BIT(GM_BACKEND_CUDA);
#endif
#if defined(MAGMA_METAL)
    mask |= GM_BACKEND_BIT(GM_BACKEND_METAL);
#endif
    return mask;
}

int cr_backend_open(CrRasterBackend **out, GmBackend kind, int width, int height,
                    int max_tris, char *err, int err_cap) {
    CrRasterBackend *b;
    if (out) *out = NULL;
    if (!out || width <= 0 || height <= 0 || max_tris <= 0) {
        if (err && err_cap > 0) snprintf(err, (size_t)err_cap, "invalid raster backend dimensions");
        return 0;
    }
    if ((uint64_t)width * (uint64_t)height > CR_MAX_FRAMEBUFFER_PIXELS) {
        if (err && err_cap > 0)
            snprintf(err, (size_t)err_cap,
                     "raster framebuffer %dx%d exceeds checked %llu-pixel cap",
                     width, height,
                     (unsigned long long)CR_MAX_FRAMEBUFFER_PIXELS);
        return 0;
    }
    if (kind < GM_BACKEND_CPU || kind > GM_BACKEND_METAL ||
        !(cr_backend_compiled_mask() & GM_BACKEND_BIT(kind))) {
        const char *name = kind == GM_BACKEND_METAL ? "Metal" :
                           kind == GM_BACKEND_CUDA ? "CUDA" : "CPU";
        if (err && err_cap > 0) snprintf(err, (size_t)err_cap, "%s backend is not compiled into this binary", name);
        return 0;
    }
    b = (CrRasterBackend *)calloc(1, sizeof *b);
    if (!b) {
        if (err && err_cap > 0) snprintf(err, (size_t)err_cap, "raster backend allocation failed");
        return 0;
    }
    b->kind = kind;
    b->width = width;
    b->height = height;
    b->max_tris = max_tris;
    if (kind == GM_BACKEND_CUDA) {
#if defined(MAGMA_CUDA)
        cr_raster_cuda_pre(width, height, max_tris);
        b->caps = CR_BACKEND_CAP_GPU | CR_BACKEND_CAP_SKY;
#endif
    } else if (kind == GM_BACKEND_METAL) {
#if defined(MAGMA_METAL)
        b->metal = cr_metal_raster_create(width, height, max_tris,
                                           b->error, (int)sizeof b->error);
        if (!b->metal) {
            if (err && err_cap > 0) snprintf(err, (size_t)err_cap, "%s", b->error);
            free(b);
            return 0;
        }
        b->caps = CR_BACKEND_CAP_GPU | CR_BACKEND_CAP_SKY;
#endif
    }
    if (err && err_cap > 0) err[0] = 0;
    *out = b;
    return 1;
}

void cr_backend_close(CrRasterBackend *b) {
    if (!b) return;
    if (b->kind == GM_BACKEND_CUDA) {
#if defined(MAGMA_CUDA)
        cr_raster_cuda_post();
#endif
    } else if (b->kind == GM_BACKEND_METAL) {
#if defined(MAGMA_METAL)
        cr_metal_raster_destroy(b->metal);
#endif
    }
    free(b);
}

unsigned cr_backend_caps(const CrRasterBackend *b) { return b ? b->caps : 0; }

const char *cr_backend_last_error(const CrRasterBackend *b) {
    return b && b->error[0] ? b->error : "raster backend failure";
}

int cr_backend_frame_begin(CrRasterBackend *b, const CrFramebuffer *fb) {
    if (!b || !fb || !fb->color || !fb->depth)
        return backend_fail(b, "invalid framebuffer at frame begin");
    if (fb->w != b->width || fb->h != b->height)
        return backend_fail(b, "framebuffer %dx%d does not match backend %dx%d",
                            fb->w, fb->h, b->width, b->height);
    if (b->frame_open) return backend_fail(b, "raster frame is already open");
    if (b->kind == GM_BACKEND_CUDA) {
#if defined(MAGMA_CUDA)
        cr_raster_cuda_frame_begin(fb);
#endif
    } else if (b->kind == GM_BACKEND_METAL) {
#if defined(MAGMA_METAL)
        if (!cr_metal_raster_frame_begin(b->metal, fb, b->error, (int)sizeof b->error)) return 0;
#endif
    }
    b->frame_open = 1;
    return 1;
}

int cr_backend_raster(CrRasterBackend *b, CrFramebuffer *fb,
                      const CrScreenTri *tris, int ntris,
                      const CrShadeCtx *shade) {
    if (!b || !fb || !fb->color || !fb->depth || !shade || ntris < 0 ||
        (ntris && !tris))
        return backend_fail(b, "invalid raster call");
    if (fb->w != b->width || fb->h != b->height)
        return backend_fail(b, "raster framebuffer %dx%d does not match backend %dx%d",
                            fb->w, fb->h, b->width, b->height);
    if (ntris > b->max_tris)
        return backend_fail(b, "triangle count %d exceeds backend cap %d",
                            ntris, b->max_tris);
    if (ntris == 0) return 1;
    if (b->kind == GM_BACKEND_CPU) {
        cr_raster_cpu(fb, tris, ntris, shade);
        return 1;
    }
    if (!b->frame_open)
        return backend_fail(b, "accelerated raster call outside frame begin/end");
    if (b->kind == GM_BACKEND_CUDA) {
#if defined(MAGMA_CUDA)
        cr_raster_cuda_into(fb, tris, ntris, shade);
        return 1;
#endif
    }
    if (b->kind == GM_BACKEND_METAL) {
#if defined(MAGMA_METAL)
        return cr_metal_raster_draw(b->metal, tris, ntris, shade,
                                    b->error, (int)sizeof b->error);
#endif
    }
    return backend_fail(b, "unknown raster backend");
}

int cr_backend_sky(CrRasterBackend *b, const GmSkyCtx *sky,
                   const float basis[11], int width, int height) {
    if (!b || !(b->caps & CR_BACKEND_CAP_SKY))
        return backend_fail(b, "backend does not implement GPU sky");
    if (!sky || !basis || !b->frame_open)
        return backend_fail(b, "invalid GPU sky call outside an open frame");
    if (width != b->width || height != b->height)
        return backend_fail(b, "sky dimensions %dx%d do not match backend %dx%d",
                            width, height, b->width, b->height);
    if (b->kind == GM_BACKEND_CUDA) {
#if defined(MAGMA_CUDA)
        cr_raster_cuda_sky(sky, basis, width, height);
        return 1;
#endif
    }
    if (b->kind == GM_BACKEND_METAL) {
#if defined(MAGMA_METAL)
        return cr_metal_raster_sky(b->metal, sky, basis, width, height,
                                   b->error, (int)sizeof b->error);
#endif
    }
    (void)sky; (void)basis; (void)width; (void)height;
    return backend_fail(b, "backend does not implement GPU sky");
}

int cr_backend_frame_end(CrRasterBackend *b, CrFramebuffer *fb) {
    if (!b || !b->frame_open) return backend_fail(b, "raster frame is not open");
    if (!fb || !fb->color || !fb->depth ||
        fb->w != b->width || fb->h != b->height)
        return backend_fail(b, "frame-end framebuffer does not match backend dimensions");
    if (b->kind == GM_BACKEND_CUDA) {
#if defined(MAGMA_CUDA)
        cr_raster_cuda_frame_end(fb);
#endif
    } else if (b->kind == GM_BACKEND_METAL) {
#if defined(MAGMA_METAL)
        if (!cr_metal_raster_frame_end(b->metal, fb, b->error, (int)sizeof b->error)) return 0;
#endif
    }
    b->frame_open = 0;
    return 1;
}

void cr_backend_atlas_dirty(CrRasterBackend *b) {
    if (!b) return;
    if (b->kind == GM_BACKEND_CUDA) {
#if defined(MAGMA_CUDA)
        cr_raster_cuda_atlas_dirty();
#endif
    } else if (b->kind == GM_BACKEND_METAL) {
#if defined(MAGMA_METAL)
        cr_metal_raster_atlas_dirty(b->metal);
#endif
    }
}
