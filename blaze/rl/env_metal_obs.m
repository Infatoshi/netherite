/* env_metal_obs.m - thin Objective-C adapter: one Metal obs handle, per-lane
 * overwrite after the CPU env step. No allocation in overwrite. */
#import "env_metal_obs.h"

#include "blaze_metal_obs.h"

#include <stdio.h>
#include <string.h>

enum {
  ENV_METAL_CAM_H = 36,
  ENV_METAL_CAM_W = 64,
  ENV_METAL_NPIX = ENV_METAL_CAM_H * ENV_METAL_CAM_W
};

int env_metal_obs_create(EnvMetalObs *s, int n, int max_cells,
                         const char *metallib_path,
                         BlazeObsCamInputsFn cam_inputs) {
  if (!s || n <= 0 || max_cells <= 0 || !metallib_path || !metallib_path[0] ||
      !cam_inputs) {
    fprintf(stderr, "env_metal_obs: bad create args\n");
    return -1;
  }
  memset(s, 0, sizeof(*s));
  s->n = n;
  s->max_cells = max_cells;
  s->cam_inputs = cam_inputs;
  s->metal = blaze_metal_obs_create(max_cells, metallib_path);
  if (!s->metal) {
    fprintf(stderr,
            "env_metal_obs: blaze_metal_obs_create failed (metallib='%s', "
            "max_cells=%d); no fallback\n",
            metallib_path, max_cells);
    return -1;
  }
  return 0;
}

void env_metal_obs_destroy(EnvMetalObs *s) {
  if (!s)
    return;
  if (s->metal)
    blaze_metal_obs_destroy((BlazeMetalObs *)s->metal);
  memset(s, 0, sizeof(*s));
}

int env_metal_obs_overwrite(EnvMetalObs *s, void *cpu_env, unsigned short *cam,
                            unsigned char *depth, unsigned char *edge) {
  int e;
  if (!s || !s->metal || !s->cam_inputs || !cpu_env || !cam || !depth || !edge)
    return -1;

  for (e = 0; e < s->n; ++e) {
    double ex = 0.0, ey = 0.0, ez = 0.0;
    float yaw = 0.f, pitch = 0.f;
    int x0 = 0, y0 = 0, z0 = 0, nx = 0, ny = 0, nz = 0;
    const unsigned short *cells = NULL;
    unsigned short *cam_e = cam + (size_t)e * ENV_METAL_NPIX;
    unsigned char *depth_e = depth + (size_t)e * ENV_METAL_NPIX;
    unsigned char *edge_e = edge + (size_t)e * ENV_METAL_NPIX;

    if (s->cam_inputs(cpu_env, e, &ex, &ey, &ez, &yaw, &pitch, &x0, &y0, &z0,
                      &nx, &ny, &nz, &cells) != 0) {
      fprintf(stderr, "env_metal_obs: blaze_obs_cam_inputs failed env=%d\n", e);
      return -1;
    }
    if (!cells || nx <= 0 || ny <= 0 || nz <= 0) {
      fprintf(stderr, "env_metal_obs: empty region env=%d\n", e);
      return -1;
    }
    if (blaze_metal_obs_render((BlazeMetalObs *)s->metal, cells, x0, y0, z0, nx,
                               ny, nz, ex, ey, ez, yaw, pitch, cam_e, depth_e,
                               edge_e) != 0) {
      fprintf(stderr, "env_metal_obs: blaze_metal_obs_render failed env=%d\n",
              e);
      return -1;
    }
  }
  return 0;
}
