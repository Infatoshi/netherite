/* blaze_metal_obs.metal - Metal compute k_obs for blaze observation camera.
 *
 * Expression-for-expression port of core/obs_camera.h's float DDA path
 * (oc_block / oc_raycast / oc_edge / oc_pixel body after the double->float
 * eye cast). The cells buffer holds PACKED STATES ((id<<4)|meta) and
 * oc_block extracts the id, same as the C twin. MSL cannot include
 * obs_camera.h verbatim:
 *   - no stdint.h / math.h in the Metal front end
 *   - every pointer needs an explicit address space (device/thread/...)
 *   - double is not supported in MSL (eye is float here; host narrows)
 *   - McSinTable is 256 KiB, over the 64 KiB constant-buffer limit, so the
 *     sin LUT lives in a device buffer
 *
 * BUILD (see magma/Makefile game-metal for the same flags):
 *   xcrun -sdk macosx metal -fno-fast-math -ffp-contract=off \
 *       blaze_metal_obs.metal -o blaze_metal_obs.air
 *   xcrun -sdk macosx metallib blaze_metal_obs.air -o blaze_metal_obs.metallib
 *
 * File-scope #pragma clang fp contract(off) is the load-bearing guard
 * (matches CUDA --fmad=false / CPU -ffp-contract=off).
 */
#include <metal_stdlib>
using namespace metal;

#pragma clang fp contract(off)

typedef uchar  u8;
typedef ushort u16;
typedef int    i32;

#define MC_SIN_TABLE_LEN 65536
typedef struct { float sin_table[MC_SIN_TABLE_LEN]; } McSinTable;

/* precise:: for IEEE-correct sqrt/fabs matching host libm under -ffp-contract. */
inline float fabsf(float x) { return metal::precise::fabs(x); }
inline float sqrtf(float x) { return metal::precise::sqrt(x); }

/* ---- mc_math.h (device-pointer form; bit-identical index math) ----------- */

static inline i32 mc_f2i(float f) {
    if (f != f) return 0;
    if (f >=  2147483648.0f) return  2147483647;
    if (f <= -2147483648.0f) return (i32)(-2147483647 - 1);
    return (i32)f;
}

static inline float mc_sin(device const McSinTable *t, float value) {
    return t->sin_table[mc_f2i(value * 10430.378f) & 65535];
}
static inline float mc_cos(device const McSinTable *t, float value) {
    return t->sin_table[mc_f2i(value * 10430.378f + 16384.0f) & 65535];
}
static inline int mc_floorf(float value) {
    int i = (int)mc_f2i(value);
    return value < (float)i ? i - 1 : i;
}

/* ---- obs_camera.h float body (address-space annotated) ------------------ */

#define OC_W    64
#define OC_H    36
#define OC_NPIX (OC_W * OC_H)
#define OC_FAR  48.0f
#define OC_TANY 0.7002075382097097f
#define OC_TANX (OC_TANY * (64.0f / 36.0f))
#define OC_PI   3.14159265358979323846
#define OC_EDGE_W 0.05f

typedef struct {
    device const u16 *cells;   /* region_tensor layout, (id<<4)|meta    */
    int x0, y0, z0, nx, ny, nz;
} OcRegion;

/* Block id at a world cell; 0 (air) outside the region. The >>4 is
 * mc_state_id: the tensor holds packed states, the camera wants plain ids. */
static inline u16 oc_block(thread const OcRegion *r, int wx, int wy, int wz) {
    int ix = wx - r->x0, iy = wy - r->y0, iz = wz - r->z0;
    if (ix < 0 || iy < 0 || iz < 0 || ix >= r->nx || iy >= r->ny || iz >= r->nz)
        return 0;
    return (u16)(r->cells[((long)ix * r->ny + iy) * r->nz + iz] >> 4);
}

static inline u16 oc_raycast(thread const OcRegion *r,
                             float ex, float ey, float ez,
                             float dx, float dy, float dz,
                             thread float *t_out, thread int *axis_out) {
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

static inline int oc_edge(float ex, float ey, float ez,
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

/* Float-eye form of oc_pixel (obs_camera.h:111-139 after the double cast). */
static inline void oc_pixel_f(thread const OcRegion *r,
                              device const McSinTable *st,
                              float fex, float fey, float fez,
                              float yaw_deg, float pitch_deg,
                              int px, int py,
                              device u16 *id_out,
                              device u8 *depth_out,
                              device u8 *edge_out) {
    float yaw = yaw_deg * (float)(OC_PI / 180.0);
    float pitch = pitch_deg * (float)(OC_PI / 180.0);
    float lx = -mc_sin(st, yaw) * mc_cos(st, pitch);
    float ly = -mc_sin(st, pitch);
    float lz = mc_cos(st, yaw) * mc_cos(st, pitch);
    float hn = sqrtf(lx * lx + lz * lz + 1e-12f);
    float rx = -lz / hn, rz = lx / hn;
    float ux = -rz * ly, uy = rz * lx - rx * lz, uz = rx * ly;
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

/* Host-uploaded parameter blocks (byte layout mirrored in the ObjC host). */
struct BlazeMetalReg {
    int x0, y0, z0, nx, ny, nz;
};
struct BlazeMetalPose {
    float ex, ey, ez, yaw, pitch;
};

/* One thread per pixel - same geometry as CUDA k_obs for batch-of-1. */
kernel void k_obs(
    device const u16 *cells [[buffer(0)]],
    device const McSinTable *st [[buffer(1)]],
    constant BlazeMetalReg *reg [[buffer(2)]],
    constant BlazeMetalPose *pose [[buffer(3)]],
    device u16 *ids [[buffer(4)]],
    device u8 *depth [[buffer(5)]],
    device u8 *edge [[buffer(6)]],
    uint gid [[thread_position_in_grid]])
{
    if (gid >= (uint)OC_NPIX) return;
    OcRegion r;
    r.cells = cells;
    r.x0 = reg->x0; r.y0 = reg->y0; r.z0 = reg->z0;
    r.nx = reg->nx; r.ny = reg->ny; r.nz = reg->nz;
    int px = (int)(gid % (uint)OC_W);
    int py = (int)(gid / (uint)OC_W);
    oc_pixel_f(&r, st, pose->ex, pose->ey, pose->ez, pose->yaw, pose->pitch,
               px, py, &ids[gid], &depth[gid], &edge[gid]);
}
