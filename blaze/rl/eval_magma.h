/* Magma --rl-bin subprocess: one env, BOLR in, JSON actions out.
 * Camera is oc_pixel (OC_W x OC_H). Not the window raster. Size is
 * compile-time (obs_camera.h); not a Magma width/height or --rl-bin flag. */
#pragma once

#include "obs_camera.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  EM_NBLOCKS = 256,
  EM_NLOGS = 64,
  EM_NCOAL = 32,
  EM_NPIX = OC_NPIX,
  EM_ACT = 13,
  EM_INV = 9
};

#define EM_MAGIC 0x524c4f42u /* "BOLR" */

#pragma pack(push, 1)
typedef struct EvalMagmaObs {
  unsigned magic;
  long long tick;
  double x, y, z;
  float yaw, pitch;
  int dead;
  int hotbar_ids[EM_INV];
  int hotbar_counts[EM_INV];
  int hotbar_sel;
  int container;
  int inv_counts[EM_INV];
  int blocks[EM_NBLOCKS][4];
  int logs[EM_NLOGS][3];
  int coal[EM_NCOAL][3];
  unsigned short cam[EM_NPIX];
  unsigned char depth[EM_NPIX];
  unsigned char edge[EM_NPIX];
} EvalMagmaObs;
#pragma pack(pop)

_Static_assert(OC_W == 64 && OC_H == 36, "policy camera is frozen 64x36");
_Static_assert(EM_NPIX == OC_NPIX, "EvalMagmaObs cam != oc_pixel");
_Static_assert(sizeof(EvalMagmaObs) == 14628u, "BOLR layout");

typedef struct EvalMagma EvalMagma;

/* Spawn magma_game --rl-bin --mobs off --snapshot-in SNAP. Reads the first
 * BOLR. bin/snap are paths from the caller's cwd (repo root). */
EvalMagma *eval_magma_open(const char *bin, const char *snap, int seed,
                           char *err, int err_cap);
void eval_magma_close(EvalMagma *m);

/* Write act13 (blaze_step layout) as JSON, `repeat` times. Last BOLR kept.
 * Tick 0 of the decision carries dyaw/dpitch/craft/interact/smelt. Later
 * ticks zero look and the pre-tick primitives (blaze_step_full). */
int eval_magma_step(EvalMagma *m, const double *act13, int repeat);

const EvalMagmaObs *eval_magma_obs(const EvalMagma *m);

/* Policy tensors from one BOLR. scal6 may be NULL. status is CU_STATUS_K-wide
 * but only [0..11] are filled (dig/iron stay 0; pack_obs does not read them). */
void eval_magma_fill_policy(const EvalMagmaObs *o, unsigned short *cam,
                            unsigned char *dep, unsigned char *edg, float *pose,
                            int *status, float *scal6);

/* Gated M1 fields: pose, inv, hotbar, coal, cam, depth, edge, dead,
 * container. blocks/logs/tick are excluded. Returns 0 if equal. */
int eval_magma_cmp_gated(const EvalMagmaObs *a, const EvalMagmaObs *b,
                         char *why, int why_cap);

#ifdef __cplusplus
}
#endif
