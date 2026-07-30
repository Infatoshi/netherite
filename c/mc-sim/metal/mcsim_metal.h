/* Native Metal execution surface for the deterministic mc-sim leaf kernels.
 *
 * This header is plain C so callers do not need Objective-C types.  Every
 * descriptor crossing the Metal ABI is fixed-width and pointer-free; bulk
 * arrays are bound as separate buffers by the Objective-C++ implementation. */
#ifndef MCSIM_METAL_H
#define MCSIM_METAL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MCSIM_METAL_ABI_VERSION 1u

typedef struct McSimMetalContext McSimMetalContext;

typedef enum McSimMetalStatus {
    MCSIM_METAL_OK = 0,
    MCSIM_METAL_NO_DEVICE = 1,
    MCSIM_METAL_SHADER_COMPILE = 2,
    MCSIM_METAL_PIPELINE = 3,
    MCSIM_METAL_SIZE_OVERFLOW = 4,
    MCSIM_METAL_ALLOC = 5,
    MCSIM_METAL_COMMAND = 6,
    MCSIM_METAL_UNSUPPORTED_FP64 = 7,
    MCSIM_METAL_INVALID_ARGUMENT = 8,
    MCSIM_METAL_DETERMINISM = 9,
    MCSIM_METAL_LAYOUT = 10
} McSimMetalStatus;

typedef struct McSimMetalError {
    McSimMetalStatus status;
    char message[512];
} McSimMetalError;

typedef struct McSimMetalSmokeParams {
    uint64_t world_seed;
    uint32_t count;
    uint32_t capacity;
} McSimMetalSmokeParams;

typedef struct McSimMetalOcRegionDesc {
    int32_t x0;
    int32_t y0;
    int32_t z0;
    int32_t nx;
    int32_t ny;
    int32_t nz;
} McSimMetalOcRegionDesc;

typedef struct McSimMetalOcCameraDesc {
    float eye_x;
    float eye_y;
    float eye_z;
    uint32_t pose_count;
} McSimMetalOcCameraDesc;

typedef struct McSimMetalOcPose {
    float yaw_deg;
    float pitch_deg;
} McSimMetalOcPose;

typedef struct McSimMetalLayoutProbe {
    uint32_t abi_version;
    uint32_t smoke_params_size;
    uint32_t region_desc_size;
    uint32_t camera_desc_size;
    uint32_t pose_size;
} McSimMetalLayoutProbe;

#if defined(__cplusplus)
#define MCSIM_METAL_STATIC_ASSERT(c, m) static_assert((c), m)
#else
#define MCSIM_METAL_STATIC_ASSERT(c, m) _Static_assert((c), m)
#endif

MCSIM_METAL_STATIC_ASSERT(sizeof(McSimMetalSmokeParams) == 16,
                          "Metal smoke ABI drift");
MCSIM_METAL_STATIC_ASSERT(offsetof(McSimMetalSmokeParams, world_seed) == 0,
                          "Metal smoke seed offset drift");
MCSIM_METAL_STATIC_ASSERT(offsetof(McSimMetalSmokeParams, count) == 8,
                          "Metal smoke count offset drift");
MCSIM_METAL_STATIC_ASSERT(offsetof(McSimMetalSmokeParams, capacity) == 12,
                          "Metal smoke capacity offset drift");
MCSIM_METAL_STATIC_ASSERT(sizeof(McSimMetalOcRegionDesc) == 24,
                          "Metal region ABI drift");
MCSIM_METAL_STATIC_ASSERT(offsetof(McSimMetalOcRegionDesc, x0) == 0 &&
                          offsetof(McSimMetalOcRegionDesc, y0) == 4 &&
                          offsetof(McSimMetalOcRegionDesc, z0) == 8 &&
                          offsetof(McSimMetalOcRegionDesc, nx) == 12 &&
                          offsetof(McSimMetalOcRegionDesc, ny) == 16 &&
                          offsetof(McSimMetalOcRegionDesc, nz) == 20,
                          "Metal region field offset drift");
MCSIM_METAL_STATIC_ASSERT(sizeof(McSimMetalOcCameraDesc) == 16,
                          "Metal camera ABI drift");
MCSIM_METAL_STATIC_ASSERT(offsetof(McSimMetalOcCameraDesc, eye_x) == 0 &&
                          offsetof(McSimMetalOcCameraDesc, eye_y) == 4 &&
                          offsetof(McSimMetalOcCameraDesc, eye_z) == 8 &&
                          offsetof(McSimMetalOcCameraDesc, pose_count) == 12,
                          "Metal camera field offset drift");
MCSIM_METAL_STATIC_ASSERT(sizeof(McSimMetalOcPose) == 8,
                          "Metal pose ABI drift");
MCSIM_METAL_STATIC_ASSERT(offsetof(McSimMetalOcPose, yaw_deg) == 0 &&
                          offsetof(McSimMetalOcPose, pitch_deg) == 4,
                          "Metal pose field offset drift");
MCSIM_METAL_STATIC_ASSERT(sizeof(McSimMetalLayoutProbe) == 20,
                          "Metal layout-probe ABI drift");
#undef MCSIM_METAL_STATIC_ASSERT

McSimMetalStatus mcsim_metal_create(McSimMetalContext **out_context,
                                    McSimMetalError *error);
void mcsim_metal_destroy(McSimMetalContext *context);

/* Compiles and executes a device-side sizeof probe for every shared ABI type. */
McSimMetalStatus mcsim_metal_layout_probe(McSimMetalContext *context,
                                          McSimMetalLayoutProbe *out_probe,
                                          McSimMetalError *error);

/* Computes the same stream as core/smoke_core.h.  Each output is produced by
 * an independent Metal thread that replays the short JavaRandom prefix.  This
 * intentionally exercises non-multiple threadgroup tails without changing
 * the stateful RNG's observable order. */
McSimMetalStatus mcsim_metal_smoke(McSimMetalContext *context,
                                   uint64_t world_seed,
                                   uint64_t *out_values,
                                   size_t count,
                                   unsigned repeat_count,
                                   McSimMetalError *error);

/* Renders pose_count 64x36 observations.  cells uses region_tensor's
 * [(x*ny+y)*nz+z] layout; sin_table contains exactly 65536 host-generated
 * MathHelper entries.  Output arrays contain pose_count*2304 elements. */
McSimMetalStatus mcsim_metal_obs_camera(
    McSimMetalContext *context,
    const uint16_t *cells,
    size_t cell_count,
    const McSimMetalOcRegionDesc *region,
    const float *sin_table,
    size_t sin_table_count,
    const McSimMetalOcCameraDesc *camera,
    const McSimMetalOcPose *poses,
    size_t pose_count,
    uint16_t *out_ids,
    uint8_t *out_depth,
    uint8_t *out_edge,
    unsigned repeat_count,
    McSimMetalError *error);

const char *mcsim_metal_status_string(McSimMetalStatus status);

#ifdef __cplusplus
}
#endif

#endif /* MCSIM_METAL_H */
