#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "metal/raster_metal.h"
#include "metal/raster_metal_shared.h"
#include "metal/raster_metal_source.h"
#include "assets/sky_atlas.h"

#include <stddef.h>
#include <new>
#include <stdio.h>
#include <string.h>

static_assert(sizeof(CrMetalScreenVert) == 56, "Metal vertex ABI");
static_assert(sizeof(CrMetalScreenTri) == 172, "Metal triangle ABI");
static_assert(sizeof(CrMetalScreenVert) == sizeof(CrScreenVert), "C/Metal vertex size");
static_assert(offsetof(CrMetalScreenTri, lod) == sizeof(CrScreenTri), "Metal triangle LOD offset");
static_assert(offsetof(CrMetalScreenVert, invw) == offsetof(CrScreenVert, invw), "invw offset");
static_assert(offsetof(CrMetalScreenVert, uvx) == offsetof(CrScreenVert, uv_w), "uv offset");
static_assert(offsetof(CrMetalScreenVert, eye_dist) == offsetof(CrScreenVert, eye_dist_w), "eye offset");
static_assert(offsetof(CrMetalScreenVert, tint_r) == offsetof(CrScreenVert, tint_r_w), "tint offset");
static_assert(offsetof(CrMetalScreenVert, blk) == offsetof(CrScreenVert, blk_w), "block light offset");
static_assert(sizeof(CrMetalTriBox) == 16, "Metal bbox ABI");
static_assert(sizeof(CrMetalRasterParams) == 16, "Metal params ABI");
static_assert(sizeof(CrMetalTextureDesc) == 196, "Metal texture descriptor ABI");
static_assert(sizeof(CrMetalShadeDesc) == 80, "Metal shade descriptor ABI");
static_assert(sizeof(CrMetalSkyDesc) == 92, "Metal sky descriptor ABI");
static_assert(sizeof(CrMetalSkyParams) == 52, "Metal sky params ABI");

enum { CR_METAL_ATLAS_CACHE = 8 };

struct CrMetalAtlasSlot {
    const void *key;
    id<MTLBuffer> buffer;
    CrMetalTextureDesc desc;
    size_t bytes;
};

struct CrMetalRaster {
    int width, height, max_tris;
    bool frame_open;
    bool atlas_dirty;
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    id<MTLComputePipelineState> bbox_pipeline;
    id<MTLComputePipelineState> raster_pipeline;
    id<MTLComputePipelineState> sky_pipeline;
    id<MTLBuffer> color;
    id<MTLBuffer> depth;
    id<MTLBuffer> triangles;
    id<MTLBuffer> boxes;
    id<MTLBuffer> lightmap;
    id<MTLBuffer> dummy_texture;
    id<MTLBuffer> sky_desc;
    id<MTLBuffer> sky_params;
    id<MTLBuffer> sky_sun;
    id<MTLBuffer> sky_moon;
    CrMetalAtlasSlot atlas[CR_METAL_ATLAS_CACHE];
    int atlas_count;
    uint64_t buffer_bytes;
    uint64_t memory_budget;
};

static int set_error(char *err, int cap, NSString *message) {
    if (err && cap > 0) snprintf(err, (size_t)cap, "%s",
                                 message ? message.UTF8String : "Metal failure");
    return 0;
}

static int set_command_error(char *err, int cap, id<MTLCommandBuffer> command) {
    NSString *message = command.error.localizedDescription;
    return set_error(err, cap, message ?: @"Metal command buffer failed");
}

static id<MTLBuffer> shared_buffer(id<MTLDevice> device, size_t bytes) {
    if (bytes == 0 || bytes > (size_t)device.maxBufferLength) return nil;
    return [device newBufferWithLength:bytes options:MTLResourceStorageModeShared];
}

static bool size_mul(size_t a, size_t b, size_t *out) {
    if (a && b > SIZE_MAX / a) return false;
    *out = a * b;
    return true;
}

static bool size_add(size_t a, size_t b, size_t *out) {
    if (b > SIZE_MAX - a) return false;
    *out = a + b;
    return true;
}

static float host_log2_det(float x) {
    union { float f; uint32_t i; } u;
    u.f = x;
    int e = (int)((u.i >> 23) & 0xffu) - 127;
    u.i = (u.i & 0x007fffffu) | 0x3f800000u;
    float m = u.f;
    float p = -1.7417939f + (2.8212026f + (-1.4699568f +
              (0.4479489f - 0.0563525f * m) * m) * m) * m;
    return (float)e + p;
}

static float host_tri_lod(const CrScreenTri *tri, const CrTexture *tex) {
    if (!tex || tex->w <= 0 || tex->h <= 0) return 0.0f;
    const CrScreenVert *v0 = &tri->v[0], *v1 = &tri->v[1], *v2 = &tri->v[2];
    float x0=v0->spos.x,y0=v0->spos.y,x1=v1->spos.x,y1=v1->spos.y,x2=v2->spos.x,y2=v2->spos.y;
    float area = (x1-x0)*(y2-y0)-(y1-y0)*(x2-x0);
    float iw0=1.0f/v0->invw,iw1=1.0f/v1->invw,iw2=1.0f/v2->invw;
    float u0=v0->uv_w.x*iw0,vv0=v0->uv_w.y*iw0;
    float u1=v1->uv_w.x*iw1,vv1=v1->uv_w.y*iw1;
    float u2=v2->uv_w.x*iw2,vv2=v2->uv_w.y*iw2;
    float tw=(float)tex->w,th=(float)tex->h;
    float ex1=(u1-u0)*tw,ey1=(vv1-vv0)*th;
    float ex2=(u2-u0)*tw,ey2=(vv2-vv0)*th;
    float tex_area = __builtin_fabsf(ex1*ey2-ex2*ey1);
    float pix_area = __builtin_fabsf(area);
    if (tex_area <= 0.0f || pix_area <= 0.0f) return 0.0f;
    return 0.5f * host_log2_det(tex_area / pix_area);
}

static void fill_shade_desc(CrMetalShadeDesc *d, const CrShadeCtx *s) {
    memset(d, 0, sizeof *d);
    d->fog_rgba = (uint32_t)s->fog_color.r |
                  ((uint32_t)s->fog_color.g << 8) |
                  ((uint32_t)s->fog_color.b << 16) |
                  ((uint32_t)s->fog_color.a << 24);
    d->fog_start = s->fog_start; d->fog_end = s->fog_end;
    d->alpha_test = s->alpha_test; d->alpha_ref = s->alpha_ref;
    d->enable_fog = s->enable_fog; d->layer = s->layer;
    d->blend = s->blend; d->use_mips = s->use_mips;
    d->mip_bias = s->mip_bias; d->has_lightmap = s->lightmap != NULL;
    d->depth_lequal = s->depth_lequal;
    d->fog_exp_density = s->fog_exp_density;
    d->alpha_mask = s->alpha_mask;
    d->mask_u_off = s->mask_u_off; d->mask_v_off = s->mask_v_off;
    d->untextured = s->untextured; d->color_trunc = s->color_trunc;
    d->cover_eps = s->cover_eps; d->sample_mode = s->sample_mode;
}

static void fill_sky_desc(CrMetalSkyDesc *d, const GmSkyCtx *s) {
    memset(d, 0, sizeof *d);
    d->sky_top_x=s->sky_top.x;d->sky_top_y=s->sky_top.y;d->sky_top_z=s->sky_top.z;
    d->fog_x=s->fog.x;d->fog_y=s->fog.y;d->fog_z=s->fog.z;
    d->sunset_active=s->sunset_active;
    memcpy(d->sunset,s->sunset,sizeof d->sunset);
    d->sun_h_x=s->sun_h.x;d->sun_h_y=s->sun_h.y;d->sun_h_z=s->sun_h.z;
    d->star_b=s->starB;d->cos_angle=s->cA;d->sin_angle=s->sA;
    d->underwater=s->uw;
    d->underwater_fog_x=s->uw_fog.x;
    d->underwater_fog_y=s->uw_fog.y;
    d->underwater_fog_z=s->uw_fog.z;
    d->underwater_density=s->uw_density;
    d->plane_y=s->plane_y;
}

static bool describe_texture(const CrTexture *texture,
                             CrMetalTextureDesc *desc, size_t *bytes) {
    memset(desc, 0, sizeof *desc);
    if (!texture || !texture->texels || texture->w <= 0 || texture->h <= 0) {
        desc->level_count = 1;
        desc->level_width[0] = desc->level_height[0] = 1;
        *bytes = sizeof(CrRgba);
        return true;
    }
    size_t count;
    if (!size_mul((size_t)texture->w, (size_t)texture->h, &count) ||
        !size_mul(count, sizeof(CrRgba), bytes)) return false;
    desc->level_count = 1;
    desc->level_width[0] = (uint32_t)texture->w;
    desc->level_height[0] = (uint32_t)texture->h;
    int levels = texture->mip_levels;
    if (levels < 0) levels = 0;
    if (levels > CR_METAL_MAX_LEVELS - 1) levels = CR_METAL_MAX_LEVELS - 1;
    for (int i = 0; i < levels; ++i) {
        unsigned level = (unsigned)i + 1;
        desc->level_count = level + 1;
        if (!texture->mip[i] || texture->mipw[i] <= 0 || texture->miph[i] <= 0) {
            desc->level_offset[level] = 0;
            desc->level_width[level] = (uint32_t)texture->w;
            desc->level_height[level] = (uint32_t)texture->h;
            continue;
        }
        size_t pixels, level_bytes;
        if (!size_mul((size_t)texture->mipw[i], (size_t)texture->miph[i], &pixels) ||
            !size_mul(pixels, sizeof(CrRgba), &level_bytes) ||
            *bytes > SIZE_MAX - level_bytes) return false;
        desc->level_offset[level] = (uint32_t)(*bytes / sizeof(CrRgba));
        desc->level_width[level] = (uint32_t)texture->mipw[i];
        desc->level_height[level] = (uint32_t)texture->miph[i];
        *bytes += level_bytes;
    }
    return true;
}

static void copy_texture(void *destination, const CrTexture *texture,
                         const CrMetalTextureDesc *desc) {
    CrRgba black = {0, 0, 0, 255};
    if (!texture || !texture->texels || texture->w <= 0 || texture->h <= 0) {
        memcpy(destination, &black, sizeof black);
        return;
    }
    size_t base = (size_t)texture->w * (size_t)texture->h * sizeof(CrRgba);
    memcpy(destination, texture->texels, base);
    for (unsigned level = 1; level < desc->level_count; ++level) {
        int i = (int)level - 1;
        if (desc->level_offset[level] == 0 || !texture->mip[i]) continue;
        size_t bytes = (size_t)texture->mipw[i] * (size_t)texture->miph[i] * sizeof(CrRgba);
        memcpy((unsigned char *)destination +
               (size_t)desc->level_offset[level] * sizeof(CrRgba),
               texture->mip[i], bytes);
    }
}

static CrMetalAtlasSlot *sync_atlas(CrMetalRaster *r, const CrTexture *texture,
                                    char *err, int cap) {
    const void *key = texture ? (const void *)texture->texels : NULL;
    CrMetalTextureDesc desc;
    size_t bytes;
    if (!describe_texture(texture, &desc, &bytes)) {
        set_error(err, cap, @"Metal atlas size overflow");
        return NULL;
    }
    for (int i = 0; i < r->atlas_count; ++i) {
        if (r->atlas[i].key != key ||
            memcmp(&r->atlas[i].desc, &desc, sizeof desc) != 0) continue;
        if (r->atlas_dirty) copy_texture(r->atlas[i].buffer.contents, texture,
                                         &r->atlas[i].desc);
        r->atlas_dirty = false;
        return &r->atlas[i];
    }
    if (r->atlas_count == CR_METAL_ATLAS_CACHE) {
        set_error(err, cap, @"Metal atlas cache exhausted");
        return NULL;
    }
    if (r->buffer_bytes > r->memory_budget ||
        (uint64_t)bytes > r->memory_budget - r->buffer_bytes) {
        NSString *message = [NSString stringWithFormat:
            @"Metal atlas requires %.2f MiB total shared buffers, above %.2f MiB budget",
            (double)(r->buffer_bytes + (uint64_t)bytes) / 1048576.0,
            (double)r->memory_budget / 1048576.0];
        set_error(err, cap, message);
        return NULL;
    }
    id<MTLBuffer> buffer = shared_buffer(r->device, bytes);
    if (!buffer) {
        set_error(err, cap, @"Metal atlas allocation failed");
        return NULL;
    }
    CrMetalAtlasSlot *slot = &r->atlas[r->atlas_count++];
    slot->key = key; slot->buffer = buffer; slot->desc = desc; slot->bytes = bytes;
    r->buffer_bytes += (uint64_t)bytes;
    copy_texture(buffer.contents, texture, &slot->desc);
    r->atlas_dirty = false;
    return slot;
}

CrMetalRaster *cr_metal_raster_create(int width, int height, int max_tris,
                                      char *err, int err_cap) {
    @autoreleasepool {
        if (width <= 0 || height <= 0 || max_tris <= 0) {
            set_error(err, err_cap, @"invalid Metal raster dimensions");
            return NULL;
        }
        if ((uint64_t)width * (uint64_t)height > CR_MAX_FRAMEBUFFER_PIXELS) {
            NSString *message = [NSString stringWithFormat:
                @"Metal framebuffer %dx%d exceeds checked %llu-pixel cap",
                width, height,
                (unsigned long long)CR_MAX_FRAMEBUFFER_PIXELS];
            set_error(err, err_cap, message);
            return NULL;
        }
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            set_error(err, err_cap, @"Metal is unavailable: no default MTLDevice");
            return NULL;
        }
        MTLCompileOptions *options = [MTLCompileOptions new];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        options.fastMathEnabled = NO;
#pragma clang diagnostic pop
        if (@available(macOS 15.0, *)) options.mathMode = MTLMathModeSafe;
        NSError *compile_error = nil;
        NSString *source = [NSString stringWithUTF8String:CR_METAL_SHADER_SOURCE];
        id<MTLLibrary> library = [device newLibraryWithSource:source
                                                      options:options
                                                        error:&compile_error];
        if (!library) {
            set_error(err, err_cap, compile_error.localizedDescription ?: @"Metal shader compilation failed");
            return NULL;
        }
        NSError *pipeline_error = nil;
        id<MTLFunction> bbox_fn = [library newFunctionWithName:@"cr_metal_bbox"];
        id<MTLFunction> raster_fn = [library newFunctionWithName:@"cr_metal_raster"];
        id<MTLFunction> sky_fn = [library newFunctionWithName:@"cr_metal_sky"];
        id<MTLComputePipelineState> bbox = [device newComputePipelineStateWithFunction:bbox_fn
                                                                                 error:&pipeline_error];
        if (!bbox) {
            set_error(err, err_cap, pipeline_error.localizedDescription ?: @"Metal bbox pipeline failed");
            return NULL;
        }
        pipeline_error = nil;
        id<MTLComputePipelineState> raster = [device newComputePipelineStateWithFunction:raster_fn
                                                                                   error:&pipeline_error];
        if (!raster) {
            set_error(err, err_cap, pipeline_error.localizedDescription ?: @"Metal raster pipeline failed");
            return NULL;
        }
        pipeline_error = nil;
        id<MTLComputePipelineState> sky = [device newComputePipelineStateWithFunction:sky_fn
                                                                                error:&pipeline_error];
        if (!sky) {
            set_error(err, err_cap, pipeline_error.localizedDescription ?: @"Metal sky pipeline failed");
            return NULL;
        }
        size_t npixels, color_bytes, depth_bytes, tri_bytes, box_bytes;
        size_t persistent_bytes = 0;
        if (!size_mul((size_t)width, (size_t)height, &npixels) ||
            !size_mul(npixels, sizeof(CrRgba), &color_bytes) ||
            !size_mul(npixels, sizeof(float), &depth_bytes) ||
            !size_mul((size_t)max_tris, sizeof(CrMetalScreenTri), &tri_bytes) ||
            !size_mul((size_t)max_tris, sizeof(CrMetalTriBox), &box_bytes) ||
            !size_add(color_bytes, depth_bytes, &persistent_bytes) ||
            !size_add(persistent_bytes, tri_bytes, &persistent_bytes) ||
            !size_add(persistent_bytes, box_bytes, &persistent_bytes) ||
            !size_add(persistent_bytes, 257 * sizeof(CrRgba),
                      &persistent_bytes) ||
            !size_add(persistent_bytes, sizeof(CrMetalSkyDesc),
                      &persistent_bytes) ||
            !size_add(persistent_bytes, sizeof(CrMetalSkyParams),
                      &persistent_bytes) ||
            !size_add(persistent_bytes, sizeof CR_SUN_RGBA,
                      &persistent_bytes) ||
            !size_add(persistent_bytes, sizeof CR_MOON_RGBA,
                      &persistent_bytes)) {
            set_error(err, err_cap, @"Metal persistent buffer size overflow");
            return NULL;
        }
        uint64_t recommended = (uint64_t)device.recommendedMaxWorkingSetSize;
        uint64_t memory_budget = recommended
            ? (recommended / 100u) * 45u
            : (uint64_t)device.maxBufferLength;
        if ((uint64_t)persistent_bytes > memory_budget) {
            NSString *message = [NSString stringWithFormat:
                @"Metal raster requires %.2f MiB persistent shared buffers, above %.2f MiB budget; reduce framebuffer dimensions or max_tris",
                (double)persistent_bytes / 1048576.0,
                (double)memory_budget / 1048576.0];
            set_error(err, err_cap, message);
            return NULL;
        }
        CrMetalRaster *r = new (std::nothrow) CrMetalRaster{};
        if (!r) {
            set_error(err, err_cap, @"Metal raster host-state allocation failed");
            return NULL;
        }
        r->width = width; r->height = height; r->max_tris = max_tris;
        r->device = device; r->queue = [device newCommandQueue];
        r->buffer_bytes = (uint64_t)persistent_bytes;
        r->memory_budget = memory_budget;
        r->bbox_pipeline = bbox; r->raster_pipeline = raster;
        r->sky_pipeline = sky;
        r->color = shared_buffer(device, color_bytes);
        r->depth = shared_buffer(device, depth_bytes);
        r->triangles = shared_buffer(device, tri_bytes);
        r->boxes = shared_buffer(device, box_bytes);
        r->lightmap = shared_buffer(device, 256 * sizeof(CrRgba));
        r->dummy_texture = shared_buffer(device, sizeof(CrRgba));
        r->sky_desc = shared_buffer(device, sizeof(CrMetalSkyDesc));
        r->sky_params = shared_buffer(device, sizeof(CrMetalSkyParams));
        r->sky_sun = shared_buffer(device, sizeof CR_SUN_RGBA);
        r->sky_moon = shared_buffer(device, sizeof CR_MOON_RGBA);
        if (!r->queue || !r->color || !r->depth || !r->triangles || !r->boxes ||
            !r->lightmap || !r->dummy_texture || !r->sky_desc ||
            !r->sky_params || !r->sky_sun || !r->sky_moon) {
            delete r;
            set_error(err, err_cap, @"Metal persistent buffer allocation failed");
            return NULL;
        }
        CrRgba black = {0, 0, 0, 255};
        memcpy(r->dummy_texture.contents, &black, sizeof black);
        memcpy(r->sky_sun.contents, CR_SUN_RGBA, sizeof CR_SUN_RGBA);
        memcpy(r->sky_moon.contents, CR_MOON_RGBA, sizeof CR_MOON_RGBA);
        if (err && err_cap > 0) err[0] = 0;
        return r;
    }
}

void cr_metal_raster_destroy(CrMetalRaster *r) { delete r; }

int cr_metal_raster_frame_begin(CrMetalRaster *r, const CrFramebuffer *fb,
                                char *err, int err_cap) {
    if (!r || !fb || !fb->color || !fb->depth || fb->w != r->width || fb->h != r->height)
        return set_error(err, err_cap, @"Metal frame begin framebuffer mismatch");
    if (r->frame_open) return set_error(err, err_cap, @"Metal frame already open");
    size_t npixels = (size_t)r->width * (size_t)r->height;
    memcpy(r->color.contents, fb->color, npixels * sizeof(CrRgba));
    memcpy(r->depth.contents, fb->depth, npixels * sizeof(float));
    r->frame_open = true;
    return 1;
}

int cr_metal_raster_draw(CrMetalRaster *r, const CrScreenTri *tris,
                         int ntris, const CrShadeCtx *shade,
                         char *err, int err_cap) {
    @autoreleasepool {
        if (!r || !r->frame_open || !shade || ntris < 0 || (ntris && !tris))
            return set_error(err, err_cap, @"invalid Metal raster draw");
        if (ntris == 0) return 1;
        if (ntris > r->max_tris)
            return set_error(err, err_cap, @"Metal triangle count exceeds persistent cap");
        CrMetalScreenTri *metal_tris = (CrMetalScreenTri *)r->triangles.contents;
        for (int i = 0; i < ntris; ++i) {
            memcpy(metal_tris[i].v, tris[i].v, sizeof tris[i].v);
            metal_tris[i].lod = shade->use_mips ? host_tri_lod(&tris[i], shade->atlas) : 0.0f;
        }
        CrMetalAtlasSlot *atlas = sync_atlas(r, shade->atlas, err, err_cap);
        if (!atlas) return 0;
        if (shade->lightmap) memcpy(r->lightmap.contents, shade->lightmap,
                                    256 * sizeof(CrRgba));
        CrMetalShadeDesc sd; fill_shade_desc(&sd, shade);
        CrMetalRasterParams params = {(uint32_t)r->width, (uint32_t)r->height,
                                      (uint32_t)ntris, 0};
        id<MTLCommandBuffer> command = [r->queue commandBuffer];
        if (!command) return set_error(err, err_cap, @"cannot create Metal command buffer");
        id<MTLComputeCommandEncoder> enc = [command computeCommandEncoder];
        if (!enc) return set_error(err, err_cap, @"cannot create Metal bbox encoder");
        [enc setComputePipelineState:r->bbox_pipeline];
        [enc setBuffer:r->triangles offset:0 atIndex:0];
        [enc setBuffer:r->boxes offset:0 atIndex:1];
        [enc setBytes:&params length:sizeof params atIndex:2];
        [enc dispatchThreads:MTLSizeMake((NSUInteger)ntris, 1, 1)
             threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
        [enc endEncoding];

        enc = [command computeCommandEncoder];
        if (!enc) return set_error(err, err_cap, @"cannot create Metal raster encoder");
        [enc setComputePipelineState:r->raster_pipeline];
        [enc setBuffer:r->color offset:0 atIndex:0];
        [enc setBuffer:r->depth offset:0 atIndex:1];
        [enc setBuffer:r->triangles offset:0 atIndex:2];
        [enc setBuffer:r->boxes offset:0 atIndex:3];
        [enc setBuffer:atlas->buffer ?: r->dummy_texture offset:0 atIndex:4];
        [enc setBuffer:r->lightmap offset:0 atIndex:5];
        [enc setBytes:&atlas->desc length:sizeof atlas->desc atIndex:6];
        [enc setBytes:&sd length:sizeof sd atIndex:7];
        [enc setBytes:&params length:sizeof params atIndex:8];
        /* Full 16x16 groups are required: tail lanes still participate in the
         * ordered prefix scan even when their pixel is outside the framebuffer. */
        [enc dispatchThreadgroups:MTLSizeMake((NSUInteger)(r->width + 15) / 16,
                                               (NSUInteger)(r->height + 15) / 16, 1)
             threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
        [enc endEncoding];
        [command commit];
        [command waitUntilCompleted];
        if (command.status != MTLCommandBufferStatusCompleted)
            return set_command_error(err, err_cap, command);
        return 1;
    }
}

int cr_metal_raster_sky(CrMetalRaster *r, const GmSkyCtx *sky,
                        const float basis[11], int width, int height,
                        char *err, int err_cap) {
    @autoreleasepool {
        if (!r || !r->frame_open || !sky || !basis ||
            width != r->width || height != r->height)
            return set_error(err, err_cap, @"invalid Metal sky dispatch");
        CrMetalSkyDesc desc;
        CrMetalSkyParams params;
        fill_sky_desc(&desc, sky);
        memcpy(params.basis, basis, sizeof params.basis);
        params.width = (uint32_t)width;
        params.height = (uint32_t)height;
        memcpy(r->sky_desc.contents, &desc, sizeof desc);
        memcpy(r->sky_params.contents, &params, sizeof params);
        id<MTLCommandBuffer> command = [r->queue commandBuffer];
        if (!command) return set_error(err, err_cap, @"cannot create Metal sky command buffer");
        id<MTLComputeCommandEncoder> enc = [command computeCommandEncoder];
        if (!enc) return set_error(err, err_cap, @"cannot create Metal sky encoder");
        [enc setComputePipelineState:r->sky_pipeline];
        [enc setBuffer:r->color offset:0 atIndex:0];
        [enc setBuffer:r->sky_desc offset:0 atIndex:1];
        [enc setBuffer:r->sky_params offset:0 atIndex:2];
        [enc setBuffer:r->sky_sun offset:0 atIndex:3];
        [enc setBuffer:r->sky_moon offset:0 atIndex:4];
        [enc dispatchThreads:MTLSizeMake((NSUInteger)width, (NSUInteger)height, 1)
             threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
        [enc endEncoding];
        [command commit];
        [command waitUntilCompleted];
        if (command.status != MTLCommandBufferStatusCompleted)
            return set_command_error(err, err_cap, command);
        return 1;
    }
}

int cr_metal_raster_frame_end(CrMetalRaster *r, CrFramebuffer *fb,
                              char *err, int err_cap) {
    if (!r || !r->frame_open || !fb || !fb->color || !fb->depth ||
        fb->w != r->width || fb->h != r->height)
        return set_error(err, err_cap, @"Metal frame end framebuffer mismatch");
    size_t npixels = (size_t)r->width * (size_t)r->height;
    memcpy(fb->color, r->color.contents, npixels * sizeof(CrRgba));
    memcpy(fb->depth, r->depth.contents, npixels * sizeof(float));
    r->frame_open = false;
    return 1;
}

void cr_metal_raster_atlas_dirty(CrMetalRaster *r) {
    if (r) r->atlas_dirty = true;
}
