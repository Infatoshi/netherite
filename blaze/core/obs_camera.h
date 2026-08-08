/* obs_camera: fixed-shape semantic-camera observation for batched RL.
 *
 * One 64x36 pinhole "camera" per env: a DDA voxel raycast (Amanatides-Woo)
 * from the eye through each pixel returns the first non-air block id and its
 * quantized depth. This is the visibility-honest obs (occlusion included)
 * decided 2026-07-13 with the static-shape obs layout; the magma prototype
 * lives in magma/game/rl_mode.c, this is the blaze port that reads a
 * region tensor so the CUDA batch path can emit obs tensors on-device.
 *
 * Determinism (CPU==CUDA bitwise):
 * - yaw/pitch trig uses the MathHelper sin LUT (mc_sin/mc_cos), never libm
 *   sin/cos (host and device libm differ).
 * - tan(fov/2) and the aspect factor are compile-time literals.
 * - the rest is +,-,*,/, IEEE sqrtf and compares only; built with
 *   -ffp-contract=off / --fmad=false per SPEC rule 4.
 *
 * FLOAT DDA (2026-07-19): the whole ray path runs in float32, not double.
 * Consumer Blackwell runs FP64 at 1/64 rate and the DDA loop was ~99% of
 * k_obs; measured against the double version over 719 frames x 2304 rays the
 * ids differ on 1 pixel in 1.66M (isolated grazing ray), inside the obs
 * tolerance. CPU==CUDA stays bitwise because BOTH sides are float with
 * contraction off. The eye pos / pose stay double in the caller (sim state
 * is untouched); they are narrowed once at oc_pixel entry.
 *
 * World access is a dense [nx][ny][nz] u16 region tensor (region_tensor.h
 * layout, cells[(ix*ny+iy)*nz+iz]); outside the AABB reads as air, so the
 * caller must supply a region covering eye + OC_FAR. Depth quantization:
 * (int)(t * 4) clamped to 255; sky = id 0, depth 255. edge = 1 where the
 * hit point lies within OC_EDGE_W of the struck face's block border
 * (world-space width, so edges thin with distance); sky = 0.
 */
#ifndef MC_OBS_CAMERA_H
#define MC_OBS_CAMERA_H

#include <math.h>
#include "mc_math.h"

#define OC_W    64
#define OC_H    36
#define OC_NPIX (OC_W * OC_H)
#define OC_FAR  48.0f
/* tan(70deg / 2) and horizontal = vertical * (64/36), as literals (no libm) */
#define OC_TANY 0.7002075382097097f
#define OC_TANX (OC_TANY * (64.0f / 36.0f))
#define OC_PI   3.14159265358979323846
#define OC_EDGE_W 0.05f /* block-border half width, world units */

typedef struct {
    const u16 *cells;          /* region_tensor layout                  */
    int x0, y0, z0, nx, ny, nz;
} OcRegion;

MC_HD static inline u16 oc_block(const OcRegion *r, int wx, int wy, int wz) {
    int ix = wx - r->x0, iy = wy - r->y0, iz = wz - r->z0;
    if (ix < 0 || iy < 0 || iz < 0 || ix >= r->nx || iy >= r->ny || iz >= r->nz)
        return 0;
    return r->cells[((long)ix * r->ny + iy) * r->nz + iz];
}

/* First non-air block along the unit ray; *t_out = distance (OC_FAR = sky).
 * *axis_out = axis of the face the ray entered through (0/1/2), -1 if the
 * hit block contains the eye (t = 0) or sky. */
MC_HD static inline u16 oc_raycast(const OcRegion *r,
                                   float ex, float ey, float ez,
                                   float dx, float dy, float dz,
                                   float *t_out, int *axis_out) {
    int vx = mc_floorf(ex), vy = mc_floorf(ey), vz = mc_floorf(ez);
    int sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1, sz = dz > 0 ? 1 : -1;
    float tdx = dx != 0 ? fabsf(1.0f / dx) : 1e30f;
    float tdy = dy != 0 ? fabsf(1.0f / dy) : 1e30f;
    float tdz = dz != 0 ? fabsf(1.0f / dz) : 1e30f;
    float tmx = dx != 0 ? (dx > 0 ? (vx + 1 - ex) : (ex - vx)) * tdx : 1e30f;
    float tmy = dy != 0 ? (dy > 0 ? (vy + 1 - ey) : (ey - vy)) * tdy : 1e30f;
    float tmz = dz != 0 ? (dz > 0 ? (vz + 1 - ez) : (ez - vz)) * tdz : 1e30f;
    float t = 0.0f;
    int axis = -1;

    while (t < OC_FAR) {
        u16 id = oc_block(r, vx, vy, vz);
        if (id) { *t_out = t; *axis_out = axis; return id; }
        if (tmx < tmy && tmx < tmz)      { t = tmx; vx += sx; tmx += tdx; axis = 0; }
        else if (tmy < tmz)              { t = tmy; vy += sy; tmy += tdy; axis = 1; }
        else                             { t = tmz; vz += sz; tmz += tdz; axis = 2; }
    }
    *t_out = OC_FAR;
    *axis_out = -1;
    return 0;
}

/* 1 if the hit point sits within OC_EDGE_W of the struck face's block
 * border, tested on the two axes tangent to the entry face (the normal
 * coordinate is on a voxel plane by construction). +,-,* and mc_floorf
 * only, so CPU==CUDA bitwise holds. */
MC_HD static inline int oc_edge(float ex, float ey, float ez,
                                float dx, float dy, float dz,
                                float t, int axis) {
    float h, f;
    int a;
    if (axis < 0) return 0;
    for (a = 0; a < 3; ++a) {
        if (a == axis) continue;
        h = (a == 0 ? ex + t * dx : a == 1 ? ey + t * dy : ez + t * dz);
        f = h - (float)mc_floorf(h);
        if (f < OC_EDGE_W || f > 1.0f - OC_EDGE_W) return 1;
    }
    return 0;
}

/* Render one pixel (px,py) of the OC_W x OC_H frame. Split out so the CUDA
 * batch driver can assign one PIXEL per thread. Row 0 = top. */
MC_HD static inline void oc_pixel(const OcRegion *r, const McSinTable *st,
                                  double ex, double ey, double ez,
                                  float yaw_deg, float pitch_deg,
                                  int px, int py, u16 *id_out, u8 *depth_out,
                                  u8 *edge_out) {
    float fex = (float)ex, fey = (float)ey, fez = (float)ez;
    float yaw = yaw_deg * (float)(OC_PI / 180.0);
    float pitch = pitch_deg * (float)(OC_PI / 180.0);
    float lx = -mc_sin(st, yaw) * mc_cos(st, pitch);
    float ly = -mc_sin(st, pitch);
    float lz = mc_cos(st, yaw) * mc_cos(st, pitch);
    float hn = sqrtf(lx * lx + lz * lz + 1e-12f);
    float rx = -lz / hn, rz = lx / hn;                  /* right (ry = 0)   */
    float ux = -rz * ly, uy = rz * lx - rx * lz, uz = rx * ly; /* cross(r,l) */
    float ny = 1.0f - 2.0f * (py + 0.5f) / OC_H;
    float nx = 2.0f * (px + 0.5f) / OC_W - 1.0f;
    float ddx = lx + nx * OC_TANX * rx + ny * OC_TANY * ux;
    float ddy = ly + ny * OC_TANY * uy;
    float ddz = lz + nx * OC_TANX * rz + ny * OC_TANY * uz;
    float n = sqrtf(ddx * ddx + ddy * ddy + ddz * ddz), t;
    int axis;
    u16 id = oc_raycast(r, fex, fey, fez, ddx / n, ddy / n, ddz / n,
                        &t, &axis);
    int d = (int)(t * 4.0f);
    *id_out = id;
    *depth_out = (u8)(id ? (d > 255 ? 255 : d) : 255);
    *edge_out = (u8)(id ? oc_edge(fex, fey, fez, ddx / n, ddy / n, ddz / n,
                                  t, axis) : 0);
}

/* Full frame into ids/depth/edge[OC_NPIX], row-major. */
MC_HD static inline void oc_render(const OcRegion *r, const McSinTable *st,
                                   double ex, double ey, double ez,
                                   float yaw_deg, float pitch_deg,
                                   u16 *ids, u8 *depth, u8 *edge) {
    int px, py;
    for (py = 0; py < OC_H; ++py)
        for (px = 0; px < OC_W; ++px)
            oc_pixel(r, st, ex, ey, ez, yaw_deg, pitch_deg, px, py,
                     &ids[py * OC_W + px], &depth[py * OC_W + px],
                     &edge[py * OC_W + px]);
}

#endif
