#ifndef MAGMA_RASTER_METAL_SHARED_H
#define MAGMA_RASTER_METAL_SHARED_H

#include <stdint.h>

#define CR_METAL_MAX_LEVELS 16

/* These structs contain no host pointers. The Objective-C++ bridge marshals
 * the public C structs into this stable 4-byte-aligned shader ABI. */
typedef struct {
    float sx, sy, sz, invw;
    float uvx, uvy, light, ao;
    float eye_dist;
    float tint_r, tint_g, tint_b, tint_a;
    float blk;
} CrMetalScreenVert;

typedef struct {
    CrMetalScreenVert v[3];
    float lod;
} CrMetalScreenTri;

typedef struct {
    int32_t minx, miny, maxx, maxy;
} CrMetalTriBox;

typedef struct {
    uint32_t width, height, ntris, _pad;
} CrMetalRasterParams;

typedef struct {
    uint32_t level_count;
    uint32_t level_offset[CR_METAL_MAX_LEVELS];
    uint32_t level_width[CR_METAL_MAX_LEVELS];
    uint32_t level_height[CR_METAL_MAX_LEVELS];
} CrMetalTextureDesc;

typedef struct {
    uint32_t fog_rgba;
    float fog_start, fog_end;
    int32_t alpha_test;
    float alpha_ref;
    int32_t enable_fog, layer, blend, use_mips;
    float mip_bias;
    int32_t has_lightmap, depth_lequal;
    float fog_exp_density;
    int32_t alpha_mask;
    float mask_u_off, mask_v_off;
    int32_t untextured, color_trunc;
    float cover_eps;
    int32_t sample_mode;
} CrMetalShadeDesc;

/* Pointer-free host/MSL sky ABI. Frame-level celestial math remains in
 * gm_sky_frame_args; Metal owns only the dominant per-pixel ray stage. */
typedef struct {
    float sky_top_x, sky_top_y, sky_top_z;
    float fog_x, fog_y, fog_z;
    int32_t sunset_active;
    float sunset[4];
    float sun_h_x, sun_h_y, sun_h_z;
    float star_b, cos_angle, sin_angle;
    int32_t underwater;
    float underwater_fog_x, underwater_fog_y, underwater_fog_z;
    float underwater_density;
    float plane_y;
} CrMetalSkyDesc;

typedef struct {
    float basis[11];
    uint32_t width, height;
} CrMetalSkyParams;

#endif
