/* Runtime-compiled MSL.  Keeping the complete source in the executable makes
 * the normal macOS build independent of the optional Xcode Metal CLI
 * component.  The CPU oracle remains the behavioral specification. */
#ifndef MCSIM_KERNELS_SOURCE_H
#define MCSIM_KERNELS_SOURCE_H

static const char kMcsimMetalSource[] = R"MCSIM(
#include <metal_stdlib>
using namespace metal;

#pragma clang fp contract(off)

constant uint MCSIM_ABI_VERSION = 1u;
constant uint OC_W = 64u;
constant uint OC_H = 36u;
constant uint OC_NPIX = OC_W * OC_H;
constant float OC_FAR = 48.0f;
constant float OC_TANY = 0x1.66819ap-1f;
constant float OC_TANX = 0x1.3eac18p+0f;
constant float OC_DEG_TO_RAD = 0x1.1df46ap-6f;
constant float OC_EDGE_W = 0.05f;

struct SmokeParams {
    ulong world_seed;
    uint count;
    uint capacity;
};

struct OcRegionDesc {
    int x0;
    int y0;
    int z0;
    int nx;
    int ny;
    int nz;
};

struct OcCameraDesc {
    float eye_x;
    float eye_y;
    float eye_z;
    uint pose_count;
};

struct OcPose {
    float yaw_deg;
    float pitch_deg;
};

struct LayoutProbe {
    uint abi_version;
    uint smoke_params_size;
    uint region_desc_size;
    uint camera_desc_size;
    uint pose_size;
};

static_assert(sizeof(SmokeParams) == 16, "SmokeParams ABI");
static_assert(sizeof(OcRegionDesc) == 24, "OcRegionDesc ABI");
static_assert(sizeof(OcCameraDesc) == 16, "OcCameraDesc ABI");
static_assert(sizeof(OcPose) == 8, "OcPose ABI");
static_assert(sizeof(LayoutProbe) == 20, "LayoutProbe ABI");

/* java.util.Random and stateless runtime hash, mirrored from core/mc_rng.h. */
constant ulong JR_MULT = 0x5DEECE66Dul;
constant ulong JR_ADD = 0xBul;
constant ulong JR_MASK = (1ul << 48) - 1ul;

struct JavaRandom {
    ulong seed;
};

inline void jr_set(thread JavaRandom &r, long seed) {
    r.seed = (ulong(seed) ^ JR_MULT) & JR_MASK;
}

inline int jr_next(thread JavaRandom &r, int bits) {
    r.seed = (r.seed * JR_MULT + JR_ADD) & JR_MASK;
    return int(r.seed >> (48 - bits));
}

inline int jr_int_bound(thread JavaRandom &r, int bound) {
    if ((bound & -bound) == bound)
        return int((long(bound) * long(jr_next(r, 31))) >> 31);
    int bits;
    int value;
    do {
        bits = jr_next(r, 31);
        value = bits % bound;
        uint wrapped = uint(bits) - uint(value) + uint(bound - 1);
        if (as_type<int>(wrapped) >= 0) break;
    } while (true);
    return value;
}

inline float jr_float(thread JavaRandom &r) {
    return float(jr_next(r, 24)) * (1.0f / 16777216.0f);
}

inline ulong hash64(ulong x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ul;
    x ^= x >> 27; x *= 0x94d049bb133111ebul;
    x ^= x >> 31;
    return x;
}

inline ulong hash_seed(ulong world_seed, long tick, int x, int y, int z,
                       uint purpose) {
    ulong h = world_seed;
    h = hash64(h ^ ulong(tick) * 0x9E3779B97F4A7C15ul);
    h = hash64(h ^ ulong(uint(x)) * 0xC2B2AE3D27D4EB4Ful);
    h = hash64(h ^ ulong(uint(y)) * 0x165667B19E3779F9ul);
    h = hash64(h ^ ulong(uint(z)) * 0xD6E8FEB86659FD93ul);
    h = hash64(h ^ ulong(purpose) * 0x2545F4914F6CDD1Dul);
    return h;
}

inline int hash_bound(ulong h, int bound) {
    uint upper = uint(hash64(h) >> 32);
    return int((ulong(upper) * ulong(bound)) >> 32);
}

kernel void mcsim_smoke(constant SmokeParams &params [[buffer(0)]],
                        device ulong *out_values [[buffer(1)]],
                        uint gid [[thread_position_in_grid]]) {
    if (gid >= params.count || gid >= params.capacity) return;

    JavaRandom jr;
    jr_set(jr, long(params.world_seed));
    ulong result = 0;
    for (uint i = 0; i <= gid; ++i) {
        ulong a = ulong(uint(jr_int_bound(jr, 1000000)));
        /* Preserve smoke_core.h's cast-before-multiply expression exactly. */
        ulong b = ulong(jr_float(jr)) * 1000000ul;
        int ii = int(i);
        ulong h = hash_seed(params.world_seed, long(i), ii * 7 - 3, 64,
                            ii * 13 + 5, i & 7u);
        ulong c = ulong(hash_bound(h, 4096));
        result = hash64(a * 0x100000001ul ^ (b << 20) ^ (c << 40));
    }
    out_values[gid] = result;
}

inline int mc_f2i(float value) {
    if (isnan(value)) return 0;
    if (value >= 2147483648.0f) return 2147483647;
    if (value <= -2147483648.0f) return (-2147483647 - 1);
    return int(value);
}

inline int mc_floorf_exact(float value) {
    int i = mc_f2i(value);
    return value < float(i) ? i - 1 : i;
}

inline float mc_sin_lut(device const float *table, float value) {
    return table[uint(mc_f2i(value * 10430.378f)) & 65535u];
}

inline float mc_cos_lut(device const float *table, float value) {
    return table[uint(mc_f2i(value * 10430.378f + 16384.0f)) & 65535u];
}

inline ushort oc_block(device const ushort *cells,
                       constant OcRegionDesc &region,
                       int wx, int wy, int wz) {
    int ix = wx - region.x0;
    int iy = wy - region.y0;
    int iz = wz - region.z0;
    if (ix < 0 || iy < 0 || iz < 0 ||
        ix >= region.nx || iy >= region.ny || iz >= region.nz)
        return 0;
    uint index = (uint(ix) * uint(region.ny) + uint(iy)) * uint(region.nz)
                 + uint(iz);
    return cells[index];
}

inline ushort oc_raycast(device const ushort *cells,
                         constant OcRegionDesc &region,
                         float ex, float ey, float ez,
                         float dx, float dy, float dz,
                         thread float &t_out, thread int &axis_out) {
    int vx = mc_floorf_exact(ex);
    int vy = mc_floorf_exact(ey);
    int vz = mc_floorf_exact(ez);
    int sx = dx > 0.0f ? 1 : -1;
    int sy = dy > 0.0f ? 1 : -1;
    int sz = dz > 0.0f ? 1 : -1;
    float tdx = dx != 0.0f ? fabs(1.0f / dx) : 1.0e30f;
    float tdy = dy != 0.0f ? fabs(1.0f / dy) : 1.0e30f;
    float tdz = dz != 0.0f ? fabs(1.0f / dz) : 1.0e30f;
    float tmx = dx != 0.0f
        ? (dx > 0.0f ? (float(vx + 1) - ex) : (ex - float(vx))) * tdx
        : 1.0e30f;
    float tmy = dy != 0.0f
        ? (dy > 0.0f ? (float(vy + 1) - ey) : (ey - float(vy))) * tdy
        : 1.0e30f;
    float tmz = dz != 0.0f
        ? (dz > 0.0f ? (float(vz + 1) - ez) : (ez - float(vz))) * tdz
        : 1.0e30f;
    float t = 0.0f;
    int axis = -1;

    while (t < OC_FAR) {
        ushort id = oc_block(cells, region, vx, vy, vz);
        if (id != 0) {
            t_out = t;
            axis_out = axis;
            return id;
        }
        if (tmx < tmy && tmx < tmz) {
            t = tmx; vx += sx; tmx += tdx; axis = 0;
        } else if (tmy < tmz) {
            t = tmy; vy += sy; tmy += tdy; axis = 1;
        } else {
            t = tmz; vz += sz; tmz += tdz; axis = 2;
        }
    }
    t_out = OC_FAR;
    axis_out = -1;
    return 0;
}

inline uchar oc_edge(float ex, float ey, float ez,
                     float dx, float dy, float dz,
                     float t, int axis) {
    if (axis < 0) return 0;
    for (int a = 0; a < 3; ++a) {
        if (a == axis) continue;
        float h = a == 0 ? ex + t * dx
                         : a == 1 ? ey + t * dy : ez + t * dz;
        float f = h - float(mc_floorf_exact(h));
        if (f < OC_EDGE_W || f > 1.0f - OC_EDGE_W) return 1;
    }
    return 0;
}

inline void oc_pixel(device const ushort *cells,
                     device const float *sin_table,
                     constant OcRegionDesc &region,
                     constant OcCameraDesc &camera,
                     thread const OcPose &pose,
                     uint px, uint py,
                     thread ushort &id_out,
                     thread uchar &depth_out,
                     thread uchar &edge_out) {
    float yaw = pose.yaw_deg * OC_DEG_TO_RAD;
    float pitch = pose.pitch_deg * OC_DEG_TO_RAD;
    float lx = -mc_sin_lut(sin_table, yaw) * mc_cos_lut(sin_table, pitch);
    float ly = -mc_sin_lut(sin_table, pitch);
    float lz = mc_cos_lut(sin_table, yaw) * mc_cos_lut(sin_table, pitch);
    float hn = precise::sqrt(lx * lx + lz * lz + 1.0e-12f);
    float rx = -lz / hn;
    float rz = lx / hn;
    float ux = -rz * ly;
    float uy = rz * lx - rx * lz;
    float uz = rx * ly;
    float ny = 1.0f - 2.0f * (float(py) + 0.5f) / float(OC_H);
    float nx = 2.0f * (float(px) + 0.5f) / float(OC_W) - 1.0f;
    float ddx = lx + nx * OC_TANX * rx + ny * OC_TANY * ux;
    float ddy = ly + ny * OC_TANY * uy;
    float ddz = lz + nx * OC_TANX * rz + ny * OC_TANY * uz;
    float norm = precise::sqrt(ddx * ddx + ddy * ddy + ddz * ddz);
    float ndx = ddx / norm;
    float ndy = ddy / norm;
    float ndz = ddz / norm;
    float t;
    int axis;
    ushort id = oc_raycast(cells, region, camera.eye_x, camera.eye_y,
                           camera.eye_z, ndx, ndy, ndz, t, axis);
    int d = int(t * 4.0f);
    id_out = id;
    depth_out = id != 0 ? uchar(d > 255 ? 255 : d) : uchar(255);
    edge_out = id != 0
        ? oc_edge(camera.eye_x, camera.eye_y, camera.eye_z,
                  ndx, ndy, ndz, t, axis)
        : uchar(0);
}

kernel void mcsim_obs_camera(
    device const ushort *cells [[buffer(0)]],
    device const float *sin_table [[buffer(1)]],
    constant OcRegionDesc &region [[buffer(2)]],
    constant OcCameraDesc &camera [[buffer(3)]],
    device const OcPose *poses [[buffer(4)]],
    device ushort *ids [[buffer(5)]],
    device uchar *depth [[buffer(6)]],
    device uchar *edge [[buffer(7)]],
    uint gid [[thread_position_in_grid]]) {
    uint total = camera.pose_count * OC_NPIX;
    if (gid >= total) return;
    uint pose_index = gid / OC_NPIX;
    uint pixel = gid - pose_index * OC_NPIX;
    OcPose pose = poses[pose_index];
    ushort out_id;
    uchar out_depth;
    uchar out_edge;
    oc_pixel(cells, sin_table, region, camera, pose,
             pixel % OC_W, pixel / OC_W, out_id, out_depth, out_edge);
    ids[gid] = out_id;
    depth[gid] = out_depth;
    edge[gid] = out_edge;
}

kernel void mcsim_layout_probe(device LayoutProbe *out_probe [[buffer(0)]],
                               uint gid [[thread_position_in_grid]]) {
    if (gid != 0) return;
    out_probe->abi_version = MCSIM_ABI_VERSION;
    out_probe->smoke_params_size = uint(sizeof(SmokeParams));
    out_probe->region_desc_size = uint(sizeof(OcRegionDesc));
    out_probe->camera_desc_size = uint(sizeof(OcCameraDesc));
    out_probe->pose_size = uint(sizeof(OcPose));
}
)MCSIM";

#endif /* MCSIM_KERNELS_SOURCE_H */
