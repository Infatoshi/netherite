/* blaze_metal_obs.h - C API for the Metal k_obs (oc_pixel) backend.
 *
 * Host-side twin of blaze_cuda.cu's k_obs camera path: render one OC_W x OC_H
 * cam/depth/edge frame from a dense region tensor + pose. Tick stays on CPU.
 *
 * Allocate-once: blaze_metal_obs_create sizes all MTLBuffers from max_cells;
 * blaze_metal_obs_render never mallocs. Compile host with -ffp-contract=off;
 * the metallib is built with -fno-fast-math -ffp-contract=off.
 *
 * Darwin / Apple silicon only. Linking on other platforms is a hard error at
 * create time (returns NULL).
 */
#ifndef BLAZE_METAL_OBS_API_H
#define BLAZE_METAL_OBS_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BlazeMetalObs BlazeMetalObs;

/* OC_W x OC_H from core/obs_camera.h - kept here so the Python gate need not
 * include the C header. Must stay equal to OC_NPIX. */
#define BLAZE_METAL_OBS_W    64
#define BLAZE_METAL_OBS_H    36
#define BLAZE_METAL_OBS_NPIX (BLAZE_METAL_OBS_W * BLAZE_METAL_OBS_H)

/* max_cells: upper bound on region volume (nx*ny*nz) for the allocate-once
 * cells buffer. metallib_path may be NULL (search paths relative to this
 * .so / cwd / executable). Returns NULL on failure. */
BlazeMetalObs *blaze_metal_obs_create(int max_cells, const char *metallib_path);
void blaze_metal_obs_destroy(BlazeMetalObs *h);

/* Render one frame. cells is host memory of length nx*ny*nz (region_tensor
 * layout). Eye pose is double (sim state); the host narrows to float32 once,
 * matching oc_pixel's entry cast. yaw/pitch are degrees (same as Blaze.pl).
 * cam/depth/edge are caller-owned length BLAZE_METAL_OBS_NPIX.
 * Returns 0 on success, -1 on error. */
int blaze_metal_obs_render(BlazeMetalObs *h,
                           const uint16_t *cells,
                           int x0, int y0, int z0, int nx, int ny, int nz,
                           double ex, double ey, double ez,
                           float yaw_deg, float pitch_deg,
                           uint16_t *cam_out,
                           uint8_t *depth_out,
                           uint8_t *edge_out);

#ifdef __cplusplus
}
#endif

#endif /* BLAZE_METAL_OBS_API_H */
