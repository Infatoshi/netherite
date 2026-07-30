/* Runtime-compiled semantic-camera kernel for the hybrid Blaze backend.
 * blaze_metal_shared.h is prepended by blaze_metal.mm before compilation. */

#define BM_SIN_N 65536
#define BM_FAR 48.0f
#define BM_TANY 0.7002075382097097f
#define BM_TANX (BM_TANY * (64.0f / 36.0f))
#define BM_DEG_TO_RAD 0.01745329238474369049f
#define BM_EDGE_W 0.05f

static inline int bm_f2i(float f) {
    if (isnan(f)) return 0;
    if (f >= 2147483648.0f) return 2147483647;
    if (f <= -2147483648.0f) return (-2147483647 - 1);
    return int(f);
}

static inline int bm_floorf(float f) {
    int i = bm_f2i(f);
    return f < float(i) ? i - 1 : i;
}

static inline float bm_sin(device const float *table, float value) {
    return table[uint(bm_f2i(value * 10430.378f)) & 65535u];
}

static inline float bm_cos(device const float *table, float value) {
    return table[uint(bm_f2i(value * 10430.378f + 16384.0f)) & 65535u];
}

static inline ushort bm_block(device const ushort *cells,
                              thread const BmCameraDesc &r,
                              int wx, int wy, int wz) {
    int ix = wx - r.x0;
    int iy = wy - r.y0;
    int iz = wz - r.z0;
    if (ix < 0 || iy < 0 || iz < 0 || ix >= r.nx || iy >= r.ny || iz >= r.nz)
        return 0;
    ulong idx = (ulong(ix) * ulong(r.ny) + ulong(iy)) * ulong(r.nz) + ulong(iz);
    return cells[r.cell_offset + idx];
}

static inline ushort bm_raycast(device const ushort *cells,
                                thread const BmCameraDesc &r,
                                float ex, float ey, float ez,
                                float dx, float dy, float dz,
                                thread float &t_out, thread int &axis_out) {
    int vx = bm_floorf(ex), vy = bm_floorf(ey), vz = bm_floorf(ez);
    int sx = dx > 0.0f ? 1 : -1;
    int sy = dy > 0.0f ? 1 : -1;
    int sz = dz > 0.0f ? 1 : -1;
    float tdx = dx != 0.0f ? fabs(1.0f / dx) : 1e30f;
    float tdy = dy != 0.0f ? fabs(1.0f / dy) : 1e30f;
    float tdz = dz != 0.0f ? fabs(1.0f / dz) : 1e30f;
    float tmx = dx != 0.0f ? (dx > 0.0f ? float(vx + 1) - ex : ex - float(vx)) * tdx : 1e30f;
    float tmy = dy != 0.0f ? (dy > 0.0f ? float(vy + 1) - ey : ey - float(vy)) * tdy : 1e30f;
    float tmz = dz != 0.0f ? (dz > 0.0f ? float(vz + 1) - ez : ez - float(vz)) * tdz : 1e30f;
    float t = 0.0f;
    int axis = -1;
    while (t < BM_FAR) {
        ushort id = bm_block(cells, r, vx, vy, vz);
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
    t_out = BM_FAR;
    axis_out = -1;
    return 0;
}

static inline uchar bm_edge(float ex, float ey, float ez,
                            float dx, float dy, float dz,
                            float t, int axis) {
    if (axis < 0) return 0;
    for (int a = 0; a < 3; ++a) {
        if (a == axis) continue;
        float h = a == 0 ? ex + t * dx : (a == 1 ? ey + t * dy : ez + t * dz);
        float f = h - float(bm_floorf(h));
        if (f < BM_EDGE_W || f > 1.0f - BM_EDGE_W) return 1;
    }
    return 0;
}

kernel void blaze_camera(device const BmCameraDesc *desc [[buffer(0)]],
                         device const ushort *cells [[buffer(1)]],
                         device const float *sin_table [[buffer(2)]],
                         device ushort *cam [[buffer(3)]],
                         device uchar *depth [[buffer(4)]],
                         device uchar *edge [[buffer(5)]],
                         constant BmCameraParams &params [[buffer(6)]],
                         uint gid [[thread_position_in_grid]]) {
    uint total = params.n_envs * params.npix;
    if (gid >= total) return;
    uint env = gid / params.npix;
    uint pix = gid - env * params.npix;
    BmCameraDesc r = desc[env];
    if (r.active == 0) return;

    float yaw = r.yaw * BM_DEG_TO_RAD;
    float pitch = r.pitch * BM_DEG_TO_RAD;
    float lx = -bm_sin(sin_table, yaw) * bm_cos(sin_table, pitch);
    float ly = -bm_sin(sin_table, pitch);
    float lz = bm_cos(sin_table, yaw) * bm_cos(sin_table, pitch);
    /* Match core/obs_camera.h and the separately gated mc-sim Metal port
     * expression-for-expression. Edge pixels sit on a 0.05 threshold, so a
     * one-ULP direction change from a relaxed sqrt can flip the edge bit. */
    float hn = precise::sqrt(lx * lx + lz * lz + 1.0e-12f);
    float rx = -lz / hn;
    float rz = lx / hn;
    float ux = -rz * ly;
    float uy = rz * lx - rx * lz;
    float uz = rx * ly;
    uint px = pix % BM_CAM_W;
    uint py = pix / BM_CAM_W;
    float ny = 1.0f - 2.0f * (float(py) + 0.5f) / float(BM_CAM_H);
    float nx = 2.0f * (float(px) + 0.5f) / float(BM_CAM_W) - 1.0f;
    float ddx = lx + nx * BM_TANX * rx + ny * BM_TANY * ux;
    float ddy = ly + ny * BM_TANY * uy;
    float ddz = lz + nx * BM_TANX * rz + ny * BM_TANY * uz;
    float nn = precise::sqrt(ddx * ddx + ddy * ddy + ddz * ddz);
    float dx = ddx / nn, dy = ddy / nn, dz = ddz / nn;
    float t;
    int axis;
    ushort id = bm_raycast(cells, r, r.ex, r.ey, r.ez, dx, dy, dz, t, axis);
    int d = int(t * 4.0f);
    ulong oi = ulong(env) * ulong(params.npix) + ulong(pix);
    cam[oi] = id;
    depth[oi] = id != 0 ? uchar(d > 255 ? 255 : d) : uchar(255);
    edge[oi] = id != 0 ? bm_edge(r.ex, r.ey, r.ez, dx, dy, dz, t, axis) : uchar(0);
}
