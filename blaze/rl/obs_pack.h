/* Shared observation packing + action decode for ppo.c and eval.c.
 * Must stay identical: a trained policy sees this exact plane/scalar layout. */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "obs_config.h"

enum {
  ENV_ACT = 13,
  ENV_CAM_H = 36,
  ENV_CAM_W = 64,
  ENV_NPIX = ENV_CAM_H * ENV_CAM_W,
  ENV_SCAL = 6,
  ENV_POSE = 5,
  ENV_STATUS = 17,
  ENV_N_PLANES = 9,
  ENV_STACK = 2,
  ENV_N_CH = ENV_N_PLANES * ENV_STACK,
  POL_HEADS = 9,
  POL_SCAL = 27
};

static const double kYaws[3] = {-15.0, 0.0, 15.0};
static const double kPitches[3] = {-10.0, 0.0, 10.0};
static const double kFwd[3] = {-1.0, 0.0, 1.0};
static const int kSelItems[9] = {17, 5, 280, 4, 58, 270, 274, 263, 50};

static void acts_to_rows(const int32_t *acts, int n, double *rows) {
  int i;
  memset(rows, 0, (size_t)n * ENV_ACT * sizeof(double));
  for (i = 0; i < n; ++i) {
    const int32_t *a = acts + (size_t)i * POL_HEADS;
    double *r = rows + (size_t)i * ENV_ACT;
    int y = a[0], p = a[1], f = a[2];
    if (y < 0 || y > 2)
      y = 1;
    if (p < 0 || p > 2)
      p = 1;
    if (f < 0 || f > 2)
      f = 1;
    r[0] = kFwd[f];
    r[2] = kYaws[y];
    r[3] = kPitches[p];
    r[4] = (double)a[3];
    r[7] = (double)a[4];
    r[8] = (double)a[5];
    r[10] = (double)a[6] - 1.0;
    r[11] = (double)a[7];
    r[9] = (double)a[8] - 1.0;
  }
}

static void pack_frame(const unsigned short *cam, const unsigned char *depth,
                       const unsigned char *edge, uint8_t *dst9) {
  int pix;
  for (pix = 0; pix < ENV_NPIX; ++pix) {
    unsigned short id = cam[pix];
    int y = pix / ENV_CAM_W;
    int x = pix % ENV_CAM_W;
    size_t base = (size_t)y * ENV_CAM_W + (size_t)x;
    dst9[0 * ENV_NPIX + base] = (uint8_t)(id == 17);
    dst9[1 * ENV_NPIX + base] = (uint8_t)(id == 18);
    dst9[2 * ENV_NPIX + base] = (uint8_t)(id == 16);
    dst9[3 * ENV_NPIX + base] = (uint8_t)(id == 1 || id == 4);
    dst9[4 * ENV_NPIX + base] = (uint8_t)(id == 2 || id == 3);
    dst9[5 * ENV_NPIX + base] = (uint8_t)(id == 58);
    dst9[6 * ENV_NPIX + base] = (uint8_t)(id != 0);
    dst9[7 * ENV_NPIX + base] = depth[pix];
    dst9[8 * ENV_NPIX + base] = edge[pix];
  }
}

static void pack_obs(const unsigned short *cam, const unsigned char *depth,
                     const unsigned char *edge, const float *scal6,
                     const float *pose, const int *status, const int *ep_dec,
                     int ep_lim, const uint8_t *have_prior,
                     const uint8_t *prior_frame, int n, uint8_t *planes,
                     float *scalars, uint8_t *frame_scratch) {
  int e;
  for (e = 0; e < n; ++e) {
    uint8_t *frame = frame_scratch + (size_t)e * ENV_N_PLANES * ENV_NPIX;
    uint8_t *dst = planes + (size_t)e * ENV_N_CH * ENV_NPIX;
    float *s = scalars + (size_t)e * POL_SCAL;
    const uint8_t *prior = prior_frame + (size_t)e * ENV_N_PLANES * ENV_NPIX;
    int k;

    pack_frame(cam + (size_t)e * ENV_NPIX, depth + (size_t)e * ENV_NPIX,
               edge + (size_t)e * ENV_NPIX, frame);
    if (!have_prior[e]) {
      memcpy(dst, frame, (size_t)ENV_N_PLANES * ENV_NPIX);
      memcpy(dst + ENV_N_PLANES * ENV_NPIX, frame,
             (size_t)ENV_N_PLANES * ENV_NPIX);
    } else {
      memcpy(dst, prior, (size_t)ENV_N_PLANES * ENV_NPIX);
      memcpy(dst + ENV_N_PLANES * ENV_NPIX, frame,
             (size_t)ENV_N_PLANES * ENV_NPIX);
    }

    memcpy(s, scal6 + (size_t)e * ENV_SCAL, ENV_SCAL * sizeof(float));
    for (k = 0; k < 9; ++k) {
      float v = (float)status[(size_t)e * ENV_STATUS + k];
      if (v > 10.f)
        v = 10.f;
      s[6 + k] = v / 10.f;
    }
    s[15] = status[(size_t)e * ENV_STATUS + 11] > 0 ? 1.f : 0.f;
    {
      int held = status[(size_t)e * ENV_STATUS + 10];
      for (k = 0; k < 9; ++k)
        s[16 + k] = (held == kSelItems[k]) ? 1.f : 0.f;
    }
    s[25] = pose[(size_t)e * ENV_POSE + 1] / 64.f;
    s[26] = (float)ep_dec[e] / (float)ep_lim;
  }
}

/* Callers initialize and validate config once before rollout/evaluation.
 * NULL selects the unchanged historical defaults. */
static inline void acts_to_rows_config(const PolicyIoConfig *c, const int32_t *acts,
                                int n, double *rows) {
  acts_to_rows(acts, n, rows);
  if (!c) return;
  static const int column[POL_HEADS] = {2,3,0,4,7,8,10,11,9};
  for (int i = 0; i < n; i++) {
    double *r = rows + (size_t)i * ENV_ACT;
    r[2] = r[2] < 0 ? -c->action_yaw_degrees : r[2] > 0 ? c->action_yaw_degrees : 0;
    r[3] = r[3] < 0 ? -c->action_pitch_degrees : r[3] > 0 ? c->action_pitch_degrees : 0;
    for (int h = 0; h < POL_HEADS; h++)
      if (!(c->action_heads & (1 << h))) r[column[h]] = h == 6 || h == 8 ? -1 : 0;
  }
}

static inline void policy_frame_config(const PolicyIoConfig *c, uint8_t *frame) {
  int stride = c->obs_pixel_stride;
  for (int p = 0; p < ENV_N_PLANES; p++) {
    uint8_t *plane = frame + (size_t)p * ENV_NPIX;
    int enabled = p < 7 ? (c->obs_semantic_mask & (1 << p)) :
                  p == 7 ? c->obs_depth : c->obs_edges;
    if (!enabled) memset(plane, 0, ENV_NPIX);
    else if (stride != 1)
      for (int y = 0; y < ENV_CAM_H; y++) for (int x = 0; x < ENV_CAM_W; x++)
        plane[y * ENV_CAM_W + x] = plane[(y / stride * stride) * ENV_CAM_W + x / stride * stride];
  }
}

static inline void pack_obs_config(const PolicyIoConfig *c,
                     const unsigned short *cam, const unsigned char *depth,
                     const unsigned char *edge, const float *scal6,
                     const float *pose, const int *status, const int *ep_dec,
                     int ep_lim, const uint8_t *have_prior,
                     const uint8_t *prior_frame, int n, uint8_t *planes,
                     float *scalars, uint8_t *frame_scratch) {
  pack_obs(cam, depth, edge, scal6, pose, status, ep_dec, ep_lim,
           have_prior, prior_frame, n, planes, scalars, frame_scratch);
  if (!c) return;
  for (int e = 0; e < n; e++) {
    uint8_t *dst = planes + (size_t)e * ENV_N_CH * ENV_NPIX;
    float *s = scalars + (size_t)e * POL_SCAL;
    policy_frame_config(c, frame_scratch + (size_t)e * ENV_N_PLANES * ENV_NPIX);
    policy_frame_config(c, dst);
    policy_frame_config(c, dst + ENV_N_PLANES * ENV_NPIX);
    if (c->obs_history == 1)
      memcpy(dst, dst + ENV_N_PLANES * ENV_NPIX, ENV_N_PLANES * ENV_NPIX);
    if (!c->obs_base_scalars) memset(s, 0, ENV_SCAL * sizeof *s);
    if (!c->obs_inventory) memset(s + 6, 0, 19 * sizeof *s);
    if (!c->obs_pose) s[25] = 0;
    if (!c->obs_clock) s[26] = 0;
  }
}
