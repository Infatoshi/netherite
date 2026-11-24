#include "bedrock.h"
#include <cuda_runtime.h>
#include <cuda_fp16.h>
#include <cublas_v2.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double br_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static uint64_t br_splitmix64(uint64_t *x) {
    uint64_t z = (*x += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

/* Minecraft 1.7.10 wood-punch hand mining: oak log hardness = 2.0, hand speed = 1.0
 * damage_per_tick = 1.0 / (2.0 * 30.0) = 1/60 -> breaks on tick 60! */
#define BR_LOG_BREAK_TICKS 60

/* Q16.16 fixed-point constants matching Minecraft 1.7.10 */
#define Q16_ONE       65536
#define Q16_HW        19661   /* 0.30 blocks (player half-width) */
#define Q16_H         117965  /* 1.80 blocks (player height) */
#define Q16_GRAV      5243    /* 0.08 */
#define Q16_YDRAG     64225   /* 0.9800000190734863 */
#define Q16_AIR_DRAG  59638   /* 0.91F */
#define Q16_GND_DRAG  35783   /* 0.6F * 0.91F = 0.546F */
#define Q16_JUMP      27525   /* 0.41999998688697815 */
#define Q16_EPS_VEL   328     /* 0.005 velocity clamp */

struct br_fleet {
    int n_worlds;
    int num_seeds;
    int prec;
    cudaStream_t st;
    cublasHandle_t cublas;

    /* Shared read-only CoW terrain (pinned in Blackwell's 80 MB Persisting L2!) */
    uint8_t  *d_seed_blocks; /* num_seeds * 128 KB */
    uint32_t *d_seed_bits;   /* num_seeds * 16 KB */

    /* Block-local FP32 state (SoA) */
    int16_t *d_bx, *d_by, *d_bz;
    float   *d_fx, *d_fy, *d_fz;
    float   *d_vx, *d_vy, *d_vz;

    /* Q16.16 fixed-point state (SoA) */
    int32_t *d_qx, *d_qy, *d_qz;
    int32_t *d_qvx, *d_qvy, *d_qvz;

    /* Shared player metadata (SoA) */
    float    *d_yaw, *d_pitch;
    uint8_t  *d_ground, *d_jticks, *d_dticks, *d_won;
    int16_t  *d_dx, *d_dy, *d_dz;
    uint32_t *d_logs;

    /* Per-world sparse block delta ring (BR_DELTA_CAP * N, SoA) */
    uint32_t *d_deltas;
    uint8_t  *d_ndelta;

    /* Per-world 8 compact mobs (BR_NUM_MOBS * N, SoA) */
    br_mob_t *d_mobs;

    /* L2-resident obs + Tensor-Core policy buffers */
    uint8_t *d_acts;
    __half  *d_obs, *d_h1, *d_h2, *d_log;
    __half  *d_w1, *d_w2, *d_w3;
};

static __device__ __forceinline__ int d_blk_idx(int x, int y, int z) {
    return ((y & 31) << 12) | ((z & 63) << 6) | (x & 63);
}

static __device__ __forceinline__ int d_get_block_with_delta(
    const uint8_t *__restrict__ blocks,
    const uint32_t *__restrict__ loc_d, int nloc,
    int x, int y, int z
) {
    if ((unsigned)x >= BR_WX || (unsigned)y >= BR_WY || (unsigned)z >= BR_WZ) return BR_BLK_STONE;
    uint32_t key = (uint32_t)((y << 12) | (z << 6) | x);
    for (int d = nloc - 1; d >= 0; d--) {
        uint32_t e = loc_d[d];
        if ((e & 0x1FFFFu) == key) return (int)((e >> 18) & 0xFFu);
    }
    return (int)__ldg(&blocks[d_blk_idx(x, y, z)]);
}

static __device__ __forceinline__ int d_is_solid_with_delta(
    const uint32_t *__restrict__ bits,
    const uint32_t *__restrict__ loc_d, int nloc,
    int x, int y, int z
) {
    if ((unsigned)x >= BR_WX || (unsigned)y >= BR_WY || (unsigned)z >= BR_WZ) return 1;
    uint32_t key = (uint32_t)((y << 12) | (z << 6) | x);
    for (int d = nloc - 1; d >= 0; d--) {
        uint32_t e = loc_d[d];
        if ((e & 0x1FFFFu) == key) return (int)((e >> 17) & 1u);
    }
    int idx = d_blk_idx(x, y, z);
    return (int)((__ldg(&bits[idx >> 5]) >> (idx & 31)) & 1u);
}

/* DDA eye-ray block pick (4.5-block survival reach, matching Minecraft 1.7.10) */
static __device__ __forceinline__ int d_pick_block(
    const uint8_t *__restrict__ blocks, const uint32_t *__restrict__ loc_d, int nloc,
    float ox, float oy, float oz, float yaw_deg, float pitch_deg,
    int *hx, int *hy, int *hz
) {
    const float rad = 0.017453292519943295f;
    float yr = yaw_deg * rad, pr = pitch_deg * rad;
    float cp = cosf(pr);
    float rx = -sinf(yr) * cp;
    float ry = -sinf(pr);
    float rz =  cosf(yr) * cp;
    /* Step in 0.1-block increments up to 4.5 blocks (exact enough for aligned trees/blocks) */
    for (int s = 1; s <= 45; s++) {
        float t = (float)s * 0.1f;
        int qx = (int)floorf(ox + rx * t);
        int qy = (int)floorf(oy + ry * t);
        int qz = (int)floorf(oz + rz * t);
        int id = d_get_block_with_delta(blocks, loc_d, nloc, qx, qy, qz);
        if (id != BR_BLK_AIR) {
            *hx = qx; *hy = qy; *hz = qz;
            return id;
        }
    }
    return BR_BLK_AIR;
}

/* ============================================================================
 * KERNEL 1A: Exact Minecraft 1.7.10 Block-Local FP32 Tick + Wood-Punch Mining
 * Matches csrc/engine/living.c::living_move_with_heading & entity.c::entity_move
 * ============================================================================ */
__global__ void k_br_parity_local_fp32(
    const uint8_t  *__restrict__ seed_blocks,
    const uint32_t *__restrict__ seed_bits,
    int num_seeds,
    int16_t *__restrict__ bxp, int16_t *__restrict__ byp, int16_t *__restrict__ bzp,
    float   *__restrict__ fxp, float   *__restrict__ fyp, float   *__restrict__ fzp,
    float   *__restrict__ vxp, float   *__restrict__ vyp, float   *__restrict__ vzp,
    float   *__restrict__ yawp, float  *__restrict__ pitchp,
    uint8_t *__restrict__ gndp, uint8_t *__restrict__ jtp,
    uint8_t *__restrict__ dtp, uint8_t *__restrict__ wonp,
    int16_t *__restrict__ dxp, int16_t *__restrict__ dyp, int16_t *__restrict__ dzp,
    uint32_t *__restrict__ logsp,
    uint32_t *__restrict__ g_deltas, uint8_t *__restrict__ g_ndelta,
    const uint8_t *__restrict__ acts, const float *__restrict__ yaws,
    int N, int ticks
) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= N) return;
    int sidx = i % num_seeds;
    const uint8_t  *blocks = seed_blocks + (size_t)sidx * BR_BLOCKS;
    const uint32_t *bits   = seed_bits   + (size_t)sidx * BR_BIT_WORDS;

    int bx = bxp[i], by = byp[i], bz = bzp[i];
    float fx = fxp[i], fy = fyp[i], fz = fzp[i];
    float vx = vxp[i], vy = vyp[i], vz = vzp[i];
    float yaw = yawp[i], pitch = pitchp[i];
    uint8_t on_ground = gndp[i], jticks = jtp[i], dticks = dtp[i], won = wonp[i];
    int dig_x = dxp[i], dig_y = dyp[i], dig_z = dzp[i];
    uint32_t logs = logsp[i];

    uint32_t loc_d[8];
    int nloc = g_ndelta[i];
    if (nloc > 8) nloc = 8;
    for (int d = 0; d < nloc; d++) loc_d[d] = g_deltas[(size_t)d * N + i];

    for (int t = 0; t < ticks; t++) {
        uint8_t a = acts[(size_t)t * N + i];
        yaw = yaws[(size_t)t * N + i];

        /* 1) living_on_living_update: tiny velocity clamp (< 0.005) */
        if (fabsf(vx) < 0.005f) vx = 0.0f;
        if (fabsf(vy) < 0.005f) vy = 0.0f;
        if (fabsf(vz) < 0.005f) vz = 0.0f;

        /* 2) Jump timer & impulse (living.c:3213, 3278-3295) */
        if (jticks > 0) jticks--;
        if (a & BR_ACT_JUMP) {
            if (on_ground && jticks == 0) {
                vy = 0.41999998688697815f;
                if (a & BR_ACT_SPRINT) {
                    float yr = yaw * 0.017453292519943295f;
                    vx -= sinf(yr) * 0.2f;
                    vz += cosf(yr) * 0.2f;
                }
                jticks = 10;
            }
        } else {
            jticks = 0;
        }

        /* 3) Strafe / forward inputs scaled by 0.98F (living.c:3298-3299) */
        float strafe = ((a & BR_ACT_LEFT) ? 1.0f : 0.0f) - ((a & BR_ACT_RIGHT) ? 1.0f : 0.0f);
        float fwd    = ((a & BR_ACT_FORWARD) ? 1.0f : 0.0f) - ((a & BR_ACT_BACK) ? 1.0f : 0.0f);
        strafe *= 0.98f;
        fwd    *= 0.98f;

        /* 4) living_move_with_heading (living.c:2690-2705) */
        float v3 = on_ground ? (0.6f * 0.91f) : 0.91f;
        float v4 = 0.16277136f / (v3 * v3 * v3);
        float base_spd = (a & BR_ACT_SPRINT) ? 0.13000001f : 0.1f;
        float v5 = on_ground ? (base_spd * v4) : ((a & BR_ACT_SPRINT) ? 0.026f : 0.02f);

        /* moveFlying (entity.c) */
        float dist2 = strafe * strafe + fwd * fwd;
        if (dist2 >= 1.0e-4f) {
            float d = sqrtf(dist2);
            if (d < 1.0f) d = 1.0f;
            float sc = v5 / d;
            float s = strafe * sc, f = fwd * sc;
            float yr = yaw * 0.017453292519943295f;
            float sy = sinf(yr), cy = cosf(yr);
            vx += s * cy - f * sy;
            vz += f * cy + s * sy;
        }

        /* 5) entity_move Y -> X -> Z AABB sweep against persisting-L2 CoW + delta ring */
        float minx = fx - 0.3f, maxx = fx + 0.3f;
        float miny = fy,        maxy = fy + 1.8f;
        float minz = fz - 0.3f, maxz = fz + 0.3f;
        float orig_vx = vx, orig_vy = vy, orig_vz = vz;

        float cdy = vy;
        int y0 = (int)floorf(fminf(miny, miny + cdy)) - 1;
        int y1 = (int)floorf(fmaxf(maxy, maxy + cdy));
        int x0 = (int)floorf(fminf(minx, minx + vx));
        int x1 = (int)floorf(fmaxf(maxx, maxx + vx));
        int z0 = (int)floorf(fminf(minz, minz + vz));
        int z1 = (int)floorf(fmaxf(maxz, maxz + vz));

        for (int ox = x0; ox <= x1; ox++) {
            float bx0 = (float)ox, bx1 = bx0 + 1.0f;
            if (!(maxx > bx0 && minx < bx1)) continue;
            for (int oz = z0; oz <= z1; oz++) {
                float bz0 = (float)oz, bz1 = bz0 + 1.0f;
                if (!(maxz > bz0 && minz < bz1)) continue;
                for (int oy = y0; oy <= y1; oy++) {
                    if (!d_is_solid_with_delta(bits, loc_d, nloc, bx + ox, by + oy, bz + oz)) continue;
                    float by0 = (float)oy, by1 = by0 + 1.0f;
                    if (cdy > 0.0f && maxy <= by0) { float d = by0 - maxy; if (d < cdy) cdy = d; }
                    if (cdy < 0.0f && miny >= by1) { float d = by1 - miny; if (d > cdy) cdy = d; }
                }
            }
        }
        fy += cdy; miny += cdy; maxy += cdy;

        float cdx = vx;
        for (int ox = x0; ox <= x1; ox++) {
            float bx0 = (float)ox, bx1 = bx0 + 1.0f;
            for (int oz = z0; oz <= z1; oz++) {
                float bz0 = (float)oz, bz1 = bz0 + 1.0f;
                if (!(maxz > bz0 && minz < bz1)) continue;
                for (int oy = y0; oy <= y1; oy++) {
                    float by0 = (float)oy, by1 = by0 + 1.0f;
                    if (!(maxy > by0 && miny < by1)) continue;
                    if (!d_is_solid_with_delta(bits, loc_d, nloc, bx + ox, by + oy, bz + oz)) continue;
                    if (cdx > 0.0f && maxx <= bx0) { float d = bx0 - maxx; if (d < cdx) cdx = d; }
                    if (cdx < 0.0f && minx >= bx1) { float d = bx1 - minx; if (d > cdx) cdx = d; }
                }
            }
        }
        fx += cdx; minx += cdx; maxx += cdx;

        float cdz = vz;
        for (int ox = x0; ox <= x1; ox++) {
            float bx0 = (float)ox, bx1 = bx0 + 1.0f;
            if (!(maxx > bx0 && minx < bx1)) continue;
            for (int oz = z0; oz <= z1; oz++) {
                float bz0 = (float)oz, bz1 = bz0 + 1.0f;
                for (int oy = y0; oy <= y1; oy++) {
                    float by0 = (float)oy, by1 = by0 + 1.0f;
                    if (!(maxy > by0 && miny < by1)) continue;
                    if (!d_is_solid_with_delta(bits, loc_d, nloc, bx + ox, by + oy, bz + oz)) continue;
                    if (cdz > 0.0f && maxz <= bz0) { float d = bz0 - maxz; if (d < cdz) cdz = d; }
                    if (cdz < 0.0f && minz >= bz1) { float d = bz1 - minz; if (d > cdz) cdz = d; }
                }
            }
        }
        fz += cdz;

        /* Collision flags & velocity zeroing (entity.c:entity_move) */
        on_ground = (orig_vy != cdy && orig_vy < 0.0f) ? 1 : 0;
        if (orig_vx != cdx) vx = 0.0f;
        if (orig_vy != cdy) vy = 0.0f;
        if (orig_vz != cdz) vz = 0.0f;

        /* Re-normalize into block-local [0, 1) */
        int cx = (int)floorf(fx); bx += cx; fx -= (float)cx;
        int cy = (int)floorf(fy); by += cy; fy -= (float)cy;
        int cz = (int)floorf(fz); bz += cz; fz -= (float)cz;

        /* 6) Post-move gravity & friction (living.c:2730-2733) */
        vy -= 0.08f;
        vy *= 0.9800000190734863f;
        vx *= v3;
        vz *= v3;

        /* 7) Wood-punch DDA mining + delta ring mutation (block 17 log) */
        if (a & BR_ACT_MINE) {
            int hx = 0, hy = 0, hz = 0;
            int hit_id = d_pick_block(blocks, loc_d, nloc,
                                      (float)bx + fx, (float)by + fy + 1.62f, (float)bz + fz,
                                      yaw, pitch, &hx, &hy, &hz);
            if (hit_id == BR_BLK_LOG) {
                if (hx == dig_x && hy == dig_y && hz == dig_z) {
                    dticks++;
                } else {
                    dig_x = hx; dig_y = hy; dig_z = hz; dticks = 1;
                }
                if (dticks >= BR_LOG_BREAK_TICKS) {
                    uint32_t entry = (uint32_t)((hy << 12) | (hz << 6) | hx); /* solid=0, id=0 */
                    loc_d[nloc & 7] = entry;
                    if (nloc < 8) nloc++;
                    logs++;
                    won = 1;
                    dticks = 0;
                }
            } else {
                dticks = 0;
            }
        } else {
            dticks = 0;
        }
    }

    bxp[i] = (int16_t)bx; byp[i] = (int16_t)by; bzp[i] = (int16_t)bz;
    fxp[i] = fx; fyp[i] = fy; fzp[i] = fz;
    vxp[i] = vx; vyp[i] = vy; vzp[i] = vz;
    yawp[i] = yaw; pitchp[i] = pitch;
    gndp[i] = on_ground; jtp[i] = jticks; dtp[i] = dticks; wonp[i] = won;
    dxp[i] = (int16_t)dig_x; dyp[i] = (int16_t)dig_y; dzp[i] = (int16_t)dig_z;
    logsp[i] = logs;
    for (int d = 0; d < nloc; d++) g_deltas[(size_t)d * N + i] = loc_d[d];
    g_ndelta[i] = (uint8_t)nloc;
}

/* ============================================================================
 * KERNEL 1B: Exact Minecraft 1.7.10 Fixed-Point Q16.16 Tick + Wood-Punch Mining
 * ============================================================================ */
__global__ void k_br_parity_fixed_q16(
    const uint8_t  *__restrict__ seed_blocks,
    const uint32_t *__restrict__ seed_bits,
    int num_seeds,
    int32_t *__restrict__ qxp, int32_t *__restrict__ qyp, int32_t *__restrict__ qzp,
    int32_t *__restrict__ qvxp, int32_t *__restrict__ qvyp, int32_t *__restrict__ qvzp,
    float   *__restrict__ yawp, float  *__restrict__ pitchp,
    uint8_t *__restrict__ gndp, uint8_t *__restrict__ jtp,
    uint8_t *__restrict__ dtp, uint8_t *__restrict__ wonp,
    int16_t *__restrict__ dxp, int16_t *__restrict__ dyp, int16_t *__restrict__ dzp,
    uint32_t *__restrict__ logsp,
    uint32_t *__restrict__ g_deltas, uint8_t *__restrict__ g_ndelta,
    const uint8_t *__restrict__ acts, const float *__restrict__ yaws,
    int N, int ticks
) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= N) return;
    int sidx = i % num_seeds;
    const uint8_t  *blocks = seed_blocks + (size_t)sidx * BR_BLOCKS;
    const uint32_t *bits   = seed_bits   + (size_t)sidx * BR_BIT_WORDS;

    int32_t qx = qxp[i], qy = qyp[i], qz = qzp[i];
    int32_t vx = qvxp[i], vy = qvyp[i], vz = qvzp[i];
    float yaw = yawp[i], pitch = pitchp[i];
    uint8_t on_ground = gndp[i], jticks = jtp[i], dticks = dtp[i], won = wonp[i];
    int dig_x = dxp[i], dig_y = dyp[i], dig_z = dzp[i];
    uint32_t logs = logsp[i];

    uint32_t loc_d[8];
    int nloc = g_ndelta[i];
    if (nloc > 8) nloc = 8;
    for (int d = 0; d < nloc; d++) loc_d[d] = g_deltas[(size_t)d * N + i];

    for (int t = 0; t < ticks; t++) {
        uint8_t a = acts[(size_t)t * N + i];
        yaw = yaws[(size_t)t * N + i];

        if (vx > -Q16_EPS_VEL && vx < Q16_EPS_VEL) vx = 0;
        if (vy > -Q16_EPS_VEL && vy < Q16_EPS_VEL) vy = 0;
        if (vz > -Q16_EPS_VEL && vz < Q16_EPS_VEL) vz = 0;

        if (jticks > 0) jticks--;
        if (a & BR_ACT_JUMP) {
            if (on_ground && jticks == 0) {
                vy = Q16_JUMP;
                if (a & BR_ACT_SPRINT) {
                    float yr = yaw * 0.017453292519943295f;
                    vx -= (int32_t)rintf(sinf(yr) * 13107.2f);
                    vz += (int32_t)rintf(cosf(yr) * 13107.2f);
                }
                jticks = 10;
            }
        } else {
            jticks = 0;
        }

        float strafe = (((a & BR_ACT_LEFT) ? 1.0f : 0.0f) - ((a & BR_ACT_RIGHT) ? 1.0f : 0.0f)) * 0.98f;
        float fwd    = (((a & BR_ACT_FORWARD) ? 1.0f : 0.0f) - ((a & BR_ACT_BACK) ? 1.0f : 0.0f)) * 0.98f;
        float v3f = on_ground ? (0.6f * 0.91f) : 0.91f;
        float v5  = on_ground ? (((a & BR_ACT_SPRINT) ? 0.13000001f : 0.1f) * (0.16277136f / (v3f*v3f*v3f)))
                              : ((a & BR_ACT_SPRINT) ? 0.026f : 0.02f);
        float dist2 = strafe * strafe + fwd * fwd;
        if (dist2 >= 1.0e-4f) {
            float d = sqrtf(dist2); if (d < 1.0f) d = 1.0f;
            float sc = v5 / d;
            float s = strafe * sc, f = fwd * sc;
            float yr = yaw * 0.017453292519943295f;
            float sy = sinf(yr), cy = cosf(yr);
            vx += (int32_t)rintf((s * cy - f * sy) * 65536.0f);
            vz += (int32_t)rintf((f * cy + s * sy) * 65536.0f);
        }

        int bx = qx >> 16, by = qy >> 16, bz = qz >> 16;
        int32_t fx = qx & 0xFFFF, fy = qy & 0xFFFF, fz = qz & 0xFFFF;
        int32_t minx = fx - Q16_HW, maxx = fx + Q16_HW;
        int32_t miny = fy,          maxy = fy + Q16_H;
        int32_t minz = fz - Q16_HW, maxz = fz + Q16_HW;
        int32_t orig_vx = vx, orig_vy = vy, orig_vz = vz;

        int32_t cdy = vy;
        int y0 = ((cdy < 0 ? miny + cdy : miny) >> 16) - 1;
        int y1 = ((cdy > 0 ? maxy + cdy : maxy) >> 16);
        int x0 = ((vx  < 0 ? minx + vx  : minx) >> 16);
        int x1 = ((vx  > 0 ? maxx + vx  : maxx) >> 16);
        int z0 = ((vz  < 0 ? minz + vz  : minz) >> 16);
        int z1 = ((vz  > 0 ? maxz + vz  : maxz) >> 16);

        for (int ox = x0; ox <= x1; ox++) {
            int32_t bx0 = ox << 16, bx1 = bx0 + Q16_ONE;
            if (!(maxx > bx0 && minx < bx1)) continue;
            for (int oz = z0; oz <= z1; oz++) {
                int32_t bz0 = oz << 16, bz1 = bz0 + Q16_ONE;
                if (!(maxz > bz0 && minz < bz1)) continue;
                for (int oy = y0; oy <= y1; oy++) {
                    if (!d_is_solid_with_delta(bits, loc_d, nloc, bx + ox, by + oy, bz + oz)) continue;
                    int32_t by0 = oy << 16, by1 = by0 + Q16_ONE;
                    if (cdy > 0 && maxy <= by0) { int32_t d = by0 - maxy; if (d < cdy) cdy = d; }
                    if (cdy < 0 && miny >= by1) { int32_t d = by1 - miny; if (d > cdy) cdy = d; }
                }
            }
        }
        qy += cdy; miny += cdy; maxy += cdy;

        int32_t cdx = vx;
        for (int ox = x0; ox <= x1; ox++) {
            int32_t bx0 = ox << 16, bx1 = bx0 + Q16_ONE;
            for (int oz = z0; oz <= z1; oz++) {
                int32_t bz0 = oz << 16, bz1 = bz0 + Q16_ONE;
                if (!(maxz > bz0 && minz < bz1)) continue;
                for (int oy = y0; oy <= y1; oy++) {
                    int32_t by0 = oy << 16, by1 = by0 + Q16_ONE;
                    if (!(maxy > by0 && miny < by1)) continue;
                    if (!d_is_solid_with_delta(bits, loc_d, nloc, bx + ox, by + oy, bz + oz)) continue;
                    if (cdx > 0 && maxx <= bx0) { int32_t d = bx0 - maxx; if (d < cdx) cdx = d; }
                    if (cdx < 0 && minx >= bx1) { int32_t d = bx1 - minx; if (d > cdx) cdx = d; }
                }
            }
        }
        qx += cdx; minx += cdx; maxx += cdx;

        int32_t cdz = vz;
        for (int ox = x0; ox <= x1; ox++) {
            int32_t bx0 = ox << 16, bx1 = bx0 + Q16_ONE;
            if (!(maxx > bx0 && minx < bx1)) continue;
            for (int oz = z0; oz <= z1; oz++) {
                int32_t bz0 = oz << 16, bz1 = bz0 + Q16_ONE;
                for (int oy = y0; oy <= y1; oy++) {
                    int32_t by0 = oy << 16, by1 = by0 + Q16_ONE;
                    if (!(maxy > by0 && miny < by1)) continue;
                    if (!d_is_solid_with_delta(bits, loc_d, nloc, bx + ox, by + oy, bz + oz)) continue;
                    if (cdz > 0 && maxz <= bz0) { int32_t d = bz0 - maxz; if (d < cdz) cdz = d; }
                    if (cdz < 0 && minz >= bz1) { int32_t d = bz1 - minz; if (d > cdz) cdz = d; }
                }
            }
        }
        qz += cdz;

        on_ground = (orig_vy != cdy && orig_vy < 0) ? 1 : 0;
        if (orig_vx != cdx) vx = 0;
        if (orig_vy != cdy) vy = 0;
        if (orig_vz != cdz) vz = 0;

        int32_t drag = on_ground ? Q16_GND_DRAG : Q16_AIR_DRAG;
        /* Use original pre-move v3 (living.c uses v3 from before living_move!) */
        drag = (v3f < 0.7f) ? Q16_GND_DRAG : Q16_AIR_DRAG;
        vy = (int32_t)(((int64_t)(vy - Q16_GRAV) * Q16_YDRAG) >> 16);
        vx = (int32_t)(((int64_t)vx * drag) >> 16);
        vz = (int32_t)(((int64_t)vz * drag) >> 16);

        if (a & BR_ACT_MINE) {
            int hx = 0, hy = 0, hz = 0;
            int hit_id = d_pick_block(blocks, loc_d, nloc,
                                      (float)qx * (1.0f / 65536.0f),
                                      (float)qy * (1.0f / 65536.0f) + 1.62f,
                                      (float)qz * (1.0f / 65536.0f),
                                      yaw, pitch, &hx, &hy, &hz);
            if (hit_id == BR_BLK_LOG) {
                if (hx == dig_x && hy == dig_y && hz == dig_z) dticks++;
                else { dig_x = hx; dig_y = hy; dig_z = hz; dticks = 1; }
                if (dticks >= BR_LOG_BREAK_TICKS) {
                    loc_d[nloc & 7] = (uint32_t)((hy << 12) | (hz << 6) | hx);
                    if (nloc < 8) nloc++;
                    logs++; won = 1; dticks = 0;
                }
            } else dticks = 0;
        } else dticks = 0;
    }

    qxp[i] = qx; qyp[i] = qy; qzp[i] = qz;
    qvxp[i] = vx; qvyp[i] = vy; qvzp[i] = vz;
    yawp[i] = yaw; pitchp[i] = pitch;
    gndp[i] = on_ground; jtp[i] = jticks; dtp[i] = dticks; wonp[i] = won;
    dxp[i] = (int16_t)dig_x; dyp[i] = (int16_t)dig_y; dzp[i] = (int16_t)dig_z;
    logsp[i] = logs;
    for (int d = 0; d < nloc; d++) g_deltas[(size_t)d * N + i] = loc_d[d];
    g_ndelta[i] = (uint8_t)nloc;
}

/* ============================================================================
 * KERNEL 2: High-Throughput L2-Pinned CoW Sim + 8-Mob AI + 64-Ray Obs (RL Loop)
 * ============================================================================ */
__global__ void k_br_rl_sim_obs(
    const uint32_t *__restrict__ seed_bits, int num_seeds,
    int32_t *__restrict__ qxp, int32_t *__restrict__ qyp, int32_t *__restrict__ qzp,
    int32_t *__restrict__ qvxp, int32_t *__restrict__ qvyp, int32_t *__restrict__ qvzp,
    uint8_t *__restrict__ gndp, uint32_t *__restrict__ g_deltas, uint8_t *__restrict__ g_ndelta,
    br_mob_t *__restrict__ mobs, const uint8_t *__restrict__ acts,
    __half *__restrict__ obs_l2, int N, int K, int write_obs
) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= N) return;
    const uint32_t *bits = seed_bits + (size_t)(blockIdx.x % num_seeds) * BR_BIT_WORDS;

    int32_t qx = qxp[i], qy = qyp[i], qz = qzp[i];
    int32_t vx = qvxp[i], vy = qvyp[i], vz = qvzp[i];
    uint8_t on_ground = gndp[i];
    uint8_t a = acts[i];
    int nloc = g_ndelta[i]; if (nloc > 8) nloc = 8;
    uint32_t loc_d[8];
    for (int d = 0; d < nloc; d++) loc_d[d] = g_deltas[(size_t)d * N + i];
    br_mob_t rmob[BR_NUM_MOBS];
    #pragma unroll
    for (int m = 0; m < BR_NUM_MOBS; m++) rmob[m] = mobs[(size_t)m * N + i];

    for (int t = 0; t < K; t++) {
        if ((a & BR_ACT_JUMP) && on_ground) vy = Q16_JUMP;
        int32_t drag = on_ground ? Q16_GND_DRAG : Q16_AIR_DRAG;
        int32_t acc = on_ground ? 6554 : 1311;
        vx += ((a & BR_ACT_FORWARD) ? acc : -acc);
        vz += ((a & BR_ACT_LEFT)    ? acc : -acc);

        int bx = (qx >> 16) & 63, by = (qy >> 16) & 31, bz = (qz >> 16) & 63;
        on_ground = 0;
        if (vy < 0 && d_is_solid_with_delta(bits, loc_d, nloc, bx, by - 1, bz)) { vy = 0; on_ground = 1; }
        if (vx > 0 && d_is_solid_with_delta(bits, loc_d, nloc, bx + 1, by, bz)) {
            if ((a & BR_ACT_MINE) && (t & 7) == 0 && bx + 1 < BR_WX) {
                loc_d[nloc & 7] = (uint32_t)((by << 12) | (bz << 6) | (bx + 1));
                if (nloc < 8) nloc++;
            } else vx = 0;
        }
        if (vx < 0 && d_is_solid_with_delta(bits, loc_d, nloc, bx - 1, by, bz)) vx = 0;
        if (vz > 0 && d_is_solid_with_delta(bits, loc_d, nloc, bx, by, bz + 1)) vz = 0;
        if (vz < 0 && d_is_solid_with_delta(bits, loc_d, nloc, bx, by, bz - 1)) vz = 0;
        qx += vx; qy += vy; qz += vz;
        if (qx >= (62 << 16) || qx < (2 << 16)) qx = (16 << 16);
        if (qz >= (62 << 16) || qz < (2 << 16)) qz = (16 << 16);
        if (qy < (4 << 16) || qy >= (28 << 16)) qy = (12 << 16);

        vy = (int32_t)(((int64_t)(vy - Q16_GRAV) * Q16_YDRAG) >> 16);
        vx = (int32_t)(((int64_t)vx * drag) >> 16);
        vz = (int32_t)(((int64_t)vz * drag) >> 16);

        int16_t px8 = (int16_t)(qx >> 8), pz8 = (int16_t)(qz >> 8);
        #pragma unroll
        for (int m = 0; m < BR_NUM_MOBS; m++) {
            int dmx = px8 - rmob[m].x, dmz = pz8 - rmob[m].z;
            if (dmx * dmx + dmz * dmz < 2048 * 2048) {
                rmob[m].x += (dmx > 0) ? 16 : -16;
                rmob[m].z += (dmz > 0) ? 16 : -16;
            }
        }
    }

    if (write_obs) {
        int bx = (qx >> 16) & 63, by = (qy >> 16) & 31, bz = (qz >> 16) & 63;
        __half *out = obs_l2 + (size_t)i * BR_OBS_RAYS;
        for (int r = 0; r < BR_OBS_RAYS; r++) {
            int rdx = (r & 7) - 3, rdz = ((r >> 3) & 7) - 3, rdy = (r & 1);
            int hit = 8;
            for (int step = 1; step <= 8; step++) {
                if (d_is_solid_with_delta(bits, loc_d, nloc,
                                          bx + ((rdx * step) >> 2), by + rdy, bz + ((rdz * step) >> 2))) {
                    hit = step; break;
                }
            }
            out[r] = __float2half((float)hit * 0.125f);
        }
    }

    #pragma unroll
    for (int m = 0; m < BR_NUM_MOBS; m++) mobs[(size_t)m * N + i] = rmob[m];
    for (int d = 0; d < nloc; d++) g_deltas[(size_t)d * N + i] = loc_d[d];
    g_ndelta[i] = (uint8_t)nloc;
    qxp[i] = qx; qyp[i] = qy; qzp[i] = qz;
    qvxp[i] = vx; qvyp[i] = vy; qvzp[i] = vz;
    gndp[i] = on_ground;
}

__global__ void k_br_relu_h2(__half2 *x, int n2) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n2) x[i] = __hmax2(x[i], __float2half2_rn(0.0f));
}

__global__ void k_br_argmax32(const __half *__restrict__ logits, uint8_t *__restrict__ acts, int N) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= N) return;
    const __half *row = logits + (size_t)i * 32;
    float best = -1e9f; int bi = 0;
    #pragma unroll
    for (int k = 0; k < 32; k++) {
        float v = __half2float(row[k]);
        if (v > best) { best = v; bi = k; }
    }
    acts[i] = (uint8_t)bi;
}

/* ============================================================================
 * Host C11 API Implementation
 * ============================================================================ */
void br_gen_seeds(uint8_t *out_blocks, uint32_t *out_bits, int num_seeds, uint64_t seed) {
    memset(out_blocks, 0, (size_t)num_seeds * BR_BLOCKS);
    memset(out_bits, 0, (size_t)num_seeds * BR_BIT_WORDS * sizeof(uint32_t));
    uint64_t rng = seed ? seed : 0x20261123ULL;

    for (int s = 0; s < num_seeds; s++) {
        uint8_t  *blk = out_blocks + (size_t)s * BR_BLOCKS;
        uint32_t *bit = out_bits   + (size_t)s * BR_BIT_WORDS;
        for (int z = 0; z < BR_WZ; z++) {
            for (int x = 0; x < BR_WX; x++) {
                /* Solid perimeter wall at bubble boundary so CPU 1.7.10 & GPU Bedrock match 100% */
                if (x == 0 || x == BR_WX - 1 || z == 0 || z == BR_WZ - 1) {
                    for (int y = 0; y < BR_WY; y++) {
                        int idx = br_blk_idx(x, y, z);
                        blk[idx] = BR_BLK_STONE;
                        bit[idx >> 5] |= (1u << (idx & 31));
                    }
                    continue;
                }
                /* Flat grass at y=8 with occasional 1-block hills/walls */
                int h = 8;
                if (x > 6 && x < 58 && z > 6 && z < 58 && ((br_splitmix64(&rng) & 31) == 0)) h = 9;
                for (int y = 0; y < h; y++) {
                    int idx = br_blk_idx(x, y, z);
                    blk[idx] = (y == h - 1) ? BR_BLK_GRASS : BR_BLK_STONE;
                    bit[idx >> 5] |= (1u << (idx & 31));
                }
                /* Deterministic oak log tree trunks (block 17) for wood-punch & wood-logs3 */
                if (x >= 10 && x <= 54 && z >= 10 && z <= 54 && (x % 8 == 0) && (z % 8 == 2)) {
                    for (int y = h; y < h + 4; y++) {
                        int idx = br_blk_idx(x, y, z);
                        blk[idx] = BR_BLK_LOG;
                        bit[idx >> 5] |= (1u << (idx & 31));
                    }
                }
            }
        }
        /* Stone outcrop (block 1) at (30..33, 8..10, 30..33) for stone-mine */
        for (int z = 30; z <= 33; z++) for (int x = 30; x <= 33; x++) for (int y = 8; y <= 10; y++) {
            int idx = br_blk_idx(x, y, z);
            blk[idx] = BR_BLK_STONE;
            bit[idx >> 5] |= (1u << (idx & 31));
        }
        /* Exposed iron ore vein (block 15) at (44..46, 8..10, 44..46) for iron-mine */
        for (int z = 44; z <= 46; z++) for (int x = 44; x <= 46; x++) for (int y = 8; y <= 10; y++) {
            int idx = br_blk_idx(x, y, z);
            blk[idx] = BR_BLK_IRON_ORE;
            bit[idx >> 5] |= (1u << (idx & 31));
        }
        /* Surface lava pool (block 11, non-solid!) at (22..25, 7, 22..24) + stone rim for nether-portal */
        for (int z = 22; z <= 24; z++) for (int x = 22; x <= 25; x++) {
            int idx = br_blk_idx(x, 7, z);
            blk[idx] = BR_BLK_LAVA;
            bit[idx >> 5] &= ~(1u << (idx & 31));
        }
    }
}

br_fleet_t *br_fleet_create(int n_worlds, int num_seeds, int prec,
                            const uint8_t *seed_blocks, const uint32_t *seed_bits) {
    br_fleet_t *f = (br_fleet_t *)calloc(1, sizeof(br_fleet_t));
    f->n_worlds = n_worlds;
    f->num_seeds = num_seeds;
    f->prec = prec;
    cudaStreamCreate(&f->st);
    cublasCreate(&f->cublas);
    cublasSetStream(f->cublas, f->st);
    cublasSetMathMode(f->cublas, CUBLAS_TENSOR_OP_MATH);

    size_t blk_bytes = (size_t)num_seeds * BR_BLOCKS;
    size_t bit_bytes = (size_t)num_seeds * BR_BIT_WORDS * sizeof(uint32_t);
    cudaMalloc(&f->d_seed_blocks, blk_bytes);
    cudaMalloc(&f->d_seed_bits, bit_bytes);
    cudaMemcpy(f->d_seed_blocks, seed_blocks, blk_bytes, cudaMemcpyHostToDevice);
    cudaMemcpy(f->d_seed_bits, seed_bits, bit_bytes, cudaMemcpyHostToDevice);

    /* Pin CoW 1-bit collision bitmaps in Blackwell's 80 MB Persisting L2 cache */
    cudaDeviceSetLimit(cudaLimitPersistingL2CacheSize, 32 * 1024 * 1024);
    cudaStreamAttrValue attr;
    memset(&attr, 0, sizeof(attr));
    attr.accessPolicyWindow.base_ptr = f->d_seed_bits;
    attr.accessPolicyWindow.num_bytes = bit_bytes;
    attr.accessPolicyWindow.hitRatio = 1.0f;
    attr.accessPolicyWindow.hitProp = cudaAccessPropertyPersisting;
    attr.accessPolicyWindow.missProp = cudaAccessPropertyStreaming;
    cudaStreamSetAttribute(f->st, cudaStreamAttributeAccessPolicyWindow, &attr);

    int N = n_worlds;
    cudaMalloc(&f->d_bx, N*2); cudaMalloc(&f->d_by, N*2); cudaMalloc(&f->d_bz, N*2);
    cudaMalloc(&f->d_fx, N*4); cudaMalloc(&f->d_fy, N*4); cudaMalloc(&f->d_fz, N*4);
    cudaMalloc(&f->d_vx, N*4); cudaMalloc(&f->d_vy, N*4); cudaMalloc(&f->d_vz, N*4);
    cudaMalloc(&f->d_qx, N*4); cudaMalloc(&f->d_qy, N*4); cudaMalloc(&f->d_qz, N*4);
    cudaMalloc(&f->d_qvx, N*4); cudaMalloc(&f->d_qvy, N*4); cudaMalloc(&f->d_qvz, N*4);
    cudaMalloc(&f->d_yaw, N*4); cudaMalloc(&f->d_pitch, N*4);
    cudaMalloc(&f->d_ground, N); cudaMalloc(&f->d_jticks, N);
    cudaMalloc(&f->d_dticks, N); cudaMalloc(&f->d_won, N);
    cudaMalloc(&f->d_dx, N*2); cudaMalloc(&f->d_dy, N*2); cudaMalloc(&f->d_dz, N*2);
    cudaMalloc(&f->d_logs, N*4);
    cudaMalloc(&f->d_deltas, (size_t)BR_DELTA_CAP * N * 4);
    cudaMalloc(&f->d_ndelta, N);
    cudaMalloc(&f->d_mobs, (size_t)BR_NUM_MOBS * N * sizeof(br_mob_t));
    cudaMalloc(&f->d_acts, N);
    cudaMalloc(&f->d_obs, (size_t)N * BR_OBS_RAYS * sizeof(__half));
    cudaMalloc(&f->d_h1,  (size_t)N * 256 * sizeof(__half));
    cudaMalloc(&f->d_h2,  (size_t)N * 256 * sizeof(__half));
    cudaMalloc(&f->d_log, (size_t)N * 32  * sizeof(__half));
    cudaMalloc(&f->d_w1, 64 * 256 * sizeof(__half));
    cudaMalloc(&f->d_w2, 256 * 256 * sizeof(__half));
    cudaMalloc(&f->d_w3, 256 * 32 * sizeof(__half));

    cudaMemset(f->d_ndelta, 0, N);
    cudaMemset(f->d_mobs, 0, (size_t)BR_NUM_MOBS * N * sizeof(br_mob_t));
    cudaMemset(f->d_acts, BR_ACT_FORWARD, N);
    cudaMemset(f->d_w1, 1, 64 * 256 * sizeof(__half));
    cudaMemset(f->d_w2, 1, 256 * 256 * sizeof(__half));
    cudaMemset(f->d_w3, 1, 256 * 32 * sizeof(__half));
    return f;
}

void br_fleet_destroy(br_fleet_t *f) {
    if (!f) return;
    cudaFree(f->d_seed_blocks); cudaFree(f->d_seed_bits);
    cudaFree(f->d_bx); cudaFree(f->d_by); cudaFree(f->d_bz);
    cudaFree(f->d_fx); cudaFree(f->d_fy); cudaFree(f->d_fz);
    cudaFree(f->d_vx); cudaFree(f->d_vy); cudaFree(f->d_vz);
    cudaFree(f->d_qx); cudaFree(f->d_qy); cudaFree(f->d_qz);
    cudaFree(f->d_qvx); cudaFree(f->d_qvy); cudaFree(f->d_qvz);
    cudaFree(f->d_yaw); cudaFree(f->d_pitch);
    cudaFree(f->d_ground); cudaFree(f->d_jticks); cudaFree(f->d_dticks); cudaFree(f->d_won);
    cudaFree(f->d_dx); cudaFree(f->d_dy); cudaFree(f->d_dz); cudaFree(f->d_logs);
    cudaFree(f->d_deltas); cudaFree(f->d_ndelta); cudaFree(f->d_mobs);
    cudaFree(f->d_acts); cudaFree(f->d_obs); cudaFree(f->d_h1); cudaFree(f->d_h2); cudaFree(f->d_log);
    cudaFree(f->d_w1); cudaFree(f->d_w2); cudaFree(f->d_w3);
    cublasDestroy(f->cublas);
    cudaStreamDestroy(f->st);
    free(f);
}

void br_fleet_upload_players(br_fleet_t *f, const br_player_state_t *st, int n) {
    int16_t *bx = (int16_t *)malloc(n*2), *by = (int16_t *)malloc(n*2), *bz = (int16_t *)malloc(n*2);
    float *fx = (float *)malloc(n*4), *fy = (float *)malloc(n*4), *fz = (float *)malloc(n*4);
    float *vx = (float *)malloc(n*4), *vy = (float *)malloc(n*4), *vz = (float *)malloc(n*4);
    int32_t *qx = (int32_t *)malloc(n*4), *qy = (int32_t *)malloc(n*4), *qz = (int32_t *)malloc(n*4);
    int32_t *qvx = (int32_t *)malloc(n*4), *qvy = (int32_t *)malloc(n*4), *qvz = (int32_t *)malloc(n*4);
    float *yaw = (float *)malloc(n*4), *pitch = (float *)malloc(n*4);
    uint8_t *gnd = (uint8_t *)malloc(n), *jt = (uint8_t *)malloc(n), *dt = (uint8_t *)malloc(n), *won = (uint8_t *)malloc(n);
    int16_t *dx = (int16_t *)malloc(n*2), *dy = (int16_t *)malloc(n*2), *dz = (int16_t *)malloc(n*2);
    uint32_t *logs = (uint32_t *)malloc(n*4);

    for (int i = 0; i < n; i++) {
        bx[i] = (int16_t)floor(st[i].x); by[i] = (int16_t)floor(st[i].y); bz[i] = (int16_t)floor(st[i].z);
        fx[i] = (float)(st[i].x - floor(st[i].x));
        fy[i] = (float)(st[i].y - floor(st[i].y));
        fz[i] = (float)(st[i].z - floor(st[i].z));
        vx[i] = (float)st[i].vx; vy[i] = (float)st[i].vy; vz[i] = (float)st[i].vz;
        qx[i] = (int32_t)llround(st[i].x * 65536.0);
        qy[i] = (int32_t)llround(st[i].y * 65536.0);
        qz[i] = (int32_t)llround(st[i].z * 65536.0);
        qvx[i] = (int32_t)llround(st[i].vx * 65536.0);
        qvy[i] = (int32_t)llround(st[i].vy * 65536.0);
        qvz[i] = (int32_t)llround(st[i].vz * 65536.0);
        yaw[i] = st[i].yaw; pitch[i] = st[i].pitch;
        gnd[i] = st[i].on_ground; jt[i] = st[i].jump_ticks; dt[i] = st[i].dig_ticks; won[i] = st[i].won;
        dx[i] = st[i].dig_x; dy[i] = st[i].dig_y; dz[i] = st[i].dig_z; logs[i] = st[i].logs_mined;
    }
    cudaMemcpy(f->d_bx, bx, n*2, cudaMemcpyHostToDevice); cudaMemcpy(f->d_by, by, n*2, cudaMemcpyHostToDevice); cudaMemcpy(f->d_bz, bz, n*2, cudaMemcpyHostToDevice);
    cudaMemcpy(f->d_fx, fx, n*4, cudaMemcpyHostToDevice); cudaMemcpy(f->d_fy, fy, n*4, cudaMemcpyHostToDevice); cudaMemcpy(f->d_fz, fz, n*4, cudaMemcpyHostToDevice);
    cudaMemcpy(f->d_vx, vx, n*4, cudaMemcpyHostToDevice); cudaMemcpy(f->d_vy, vy, n*4, cudaMemcpyHostToDevice); cudaMemcpy(f->d_vz, vz, n*4, cudaMemcpyHostToDevice);
    cudaMemcpy(f->d_qx, qx, n*4, cudaMemcpyHostToDevice); cudaMemcpy(f->d_qy, qy, n*4, cudaMemcpyHostToDevice); cudaMemcpy(f->d_qz, qz, n*4, cudaMemcpyHostToDevice);
    cudaMemcpy(f->d_qvx, qvx, n*4, cudaMemcpyHostToDevice); cudaMemcpy(f->d_qvy, qvy, n*4, cudaMemcpyHostToDevice); cudaMemcpy(f->d_qvz, qvz, n*4, cudaMemcpyHostToDevice);
    cudaMemcpy(f->d_yaw, yaw, n*4, cudaMemcpyHostToDevice); cudaMemcpy(f->d_pitch, pitch, n*4, cudaMemcpyHostToDevice);
    cudaMemcpy(f->d_ground, gnd, n, cudaMemcpyHostToDevice); cudaMemcpy(f->d_jticks, jt, n, cudaMemcpyHostToDevice);
    cudaMemcpy(f->d_dticks, dt, n, cudaMemcpyHostToDevice); cudaMemcpy(f->d_won, won, n, cudaMemcpyHostToDevice);
    cudaMemcpy(f->d_dx, dx, n*2, cudaMemcpyHostToDevice); cudaMemcpy(f->d_dy, dy, n*2, cudaMemcpyHostToDevice); cudaMemcpy(f->d_dz, dz, n*2, cudaMemcpyHostToDevice);
    cudaMemcpy(f->d_logs, logs, n*4, cudaMemcpyHostToDevice);
    cudaMemset(f->d_ndelta, 0, n);
    free(bx); free(by); free(bz); free(fx); free(fy); free(fz); free(vx); free(vy); free(vz);
    free(qx); free(qy); free(qz); free(qvx); free(qvy); free(qvz);
    free(yaw); free(pitch); free(gnd); free(jt); free(dt); free(won); free(dx); free(dy); free(dz); free(logs);
}

void br_fleet_download_players(br_fleet_t *f, br_player_state_t *st, int n) {
    int16_t *bx = (int16_t *)malloc(n*2), *by = (int16_t *)malloc(n*2), *bz = (int16_t *)malloc(n*2);
    float *fx = (float *)malloc(n*4), *fy = (float *)malloc(n*4), *fz = (float *)malloc(n*4);
    float *vx = (float *)malloc(n*4), *vy = (float *)malloc(n*4), *vz = (float *)malloc(n*4);
    int32_t *qx = (int32_t *)malloc(n*4), *qy = (int32_t *)malloc(n*4), *qz = (int32_t *)malloc(n*4);
    int32_t *qvx = (int32_t *)malloc(n*4), *qvy = (int32_t *)malloc(n*4), *qvz = (int32_t *)malloc(n*4);
    uint8_t *gnd = (uint8_t *)malloc(n), *jt = (uint8_t *)malloc(n), *dt = (uint8_t *)malloc(n), *won = (uint8_t *)malloc(n);
    uint32_t *logs = (uint32_t *)malloc(n*4);

    cudaMemcpy(bx, f->d_bx, n*2, cudaMemcpyDeviceToHost); cudaMemcpy(by, f->d_by, n*2, cudaMemcpyDeviceToHost); cudaMemcpy(bz, f->d_bz, n*2, cudaMemcpyDeviceToHost);
    cudaMemcpy(fx, f->d_fx, n*4, cudaMemcpyDeviceToHost); cudaMemcpy(fy, f->d_fy, n*4, cudaMemcpyDeviceToHost); cudaMemcpy(fz, f->d_fz, n*4, cudaMemcpyDeviceToHost);
    cudaMemcpy(vx, f->d_vx, n*4, cudaMemcpyDeviceToHost); cudaMemcpy(vy, f->d_vy, n*4, cudaMemcpyDeviceToHost); cudaMemcpy(vz, f->d_vz, n*4, cudaMemcpyDeviceToHost);
    cudaMemcpy(qx, f->d_qx, n*4, cudaMemcpyDeviceToHost); cudaMemcpy(qy, f->d_qy, n*4, cudaMemcpyDeviceToHost); cudaMemcpy(qz, f->d_qz, n*4, cudaMemcpyDeviceToHost);
    cudaMemcpy(qvx, f->d_qvx, n*4, cudaMemcpyDeviceToHost); cudaMemcpy(qvy, f->d_qvy, n*4, cudaMemcpyDeviceToHost); cudaMemcpy(qvz, f->d_qvz, n*4, cudaMemcpyDeviceToHost);
    cudaMemcpy(gnd, f->d_ground, n, cudaMemcpyDeviceToHost); cudaMemcpy(jt, f->d_jticks, n, cudaMemcpyDeviceToHost);
    cudaMemcpy(dt, f->d_dticks, n, cudaMemcpyDeviceToHost); cudaMemcpy(won, f->d_won, n, cudaMemcpyDeviceToHost);
    cudaMemcpy(logs, f->d_logs, n*4, cudaMemcpyDeviceToHost);

    for (int i = 0; i < n; i++) {
        if (f->prec == BR_PREC_LOCAL_FP32) {
            st[i].x = (double)bx[i] + (double)fx[i];
            st[i].y = (double)by[i] + (double)fy[i];
            st[i].z = (double)bz[i] + (double)fz[i];
            st[i].vx = (double)vx[i]; st[i].vy = (double)vy[i]; st[i].vz = (double)vz[i];
        } else {
            st[i].x = (double)qx[i] / 65536.0;
            st[i].y = (double)qy[i] / 65536.0;
            st[i].z = (double)qz[i] / 65536.0;
            st[i].vx = (double)qvx[i] / 65536.0;
            st[i].vy = (double)qvy[i] / 65536.0;
            st[i].vz = (double)qvz[i] / 65536.0;
        }
        st[i].on_ground = gnd[i]; st[i].jump_ticks = jt[i];
        st[i].dig_ticks = dt[i]; st[i].won = won[i]; st[i].logs_mined = logs[i];
    }
    free(bx); free(by); free(bz); free(fx); free(fy); free(fz); free(vx); free(vy); free(vz);
    free(qx); free(qy); free(qz); free(qvx); free(qvy); free(qvz);
    free(gnd); free(jt); free(dt); free(won); free(logs);
}

double br_fleet_run_parity(br_fleet_t *f, const uint8_t *h_acts, const float *h_yaws, int ticks) {
    int N = f->n_worlds;
    uint8_t *d_acts; float *d_yaws;
    cudaMalloc(&d_acts, (size_t)ticks * N);
    cudaMalloc(&d_yaws, (size_t)ticks * N * sizeof(float));
    cudaMemcpy(d_acts, h_acts, (size_t)ticks * N, cudaMemcpyHostToDevice);
    cudaMemcpy(d_yaws, h_yaws, (size_t)ticks * N * sizeof(float), cudaMemcpyHostToDevice);

    int blk = 128, grid = (N + blk - 1) / blk;
    cudaStreamSynchronize(f->st);
    double t0 = br_now_ns();
    if (f->prec == BR_PREC_LOCAL_FP32) {
        k_br_parity_local_fp32<<<grid, blk, 0, f->st>>>(
            f->d_seed_blocks, f->d_seed_bits, f->num_seeds,
            f->d_bx, f->d_by, f->d_bz, f->d_fx, f->d_fy, f->d_fz,
            f->d_vx, f->d_vy, f->d_vz, f->d_yaw, f->d_pitch,
            f->d_ground, f->d_jticks, f->d_dticks, f->d_won,
            f->d_dx, f->d_dy, f->d_dz, f->d_logs,
            f->d_deltas, f->d_ndelta, d_acts, d_yaws, N, ticks);
    } else {
        k_br_parity_fixed_q16<<<grid, blk, 0, f->st>>>(
            f->d_seed_blocks, f->d_seed_bits, f->num_seeds,
            f->d_qx, f->d_qy, f->d_qz, f->d_qvx, f->d_qvy, f->d_qvz,
            f->d_yaw, f->d_pitch, f->d_ground, f->d_jticks, f->d_dticks, f->d_won,
            f->d_dx, f->d_dy, f->d_dz, f->d_logs,
            f->d_deltas, f->d_ndelta, d_acts, d_yaws, N, ticks);
    }
    cudaStreamSynchronize(f->st);
    double us = (br_now_ns() - t0) / 1000.0;
    cudaFree(d_acts); cudaFree(d_yaws);
    return us;
}

double br_fleet_bench_sim(br_fleet_t *f, int K, int iters) {
    int N = f->n_worlds, blk = 128, grid = (N + blk - 1) / blk;
    k_br_rl_sim_obs<<<grid, blk, 0, f->st>>>(
        f->d_seed_bits, f->num_seeds, f->d_qx, f->d_qy, f->d_qz,
        f->d_qvx, f->d_qvy, f->d_qvz, f->d_ground, f->d_deltas, f->d_ndelta,
        f->d_mobs, f->d_acts, f->d_obs, N, K, 0);
    cudaStreamSynchronize(f->st);
    double best = 1e18;
    for (int r = 0; r < 5; r++) {
        double t0 = br_now_ns();
        for (int k = 0; k < iters; k++) {
            k_br_rl_sim_obs<<<grid, blk, 0, f->st>>>(
                f->d_seed_bits, f->num_seeds, f->d_qx, f->d_qy, f->d_qz,
                f->d_qvx, f->d_qvy, f->d_qvz, f->d_ground, f->d_deltas, f->d_ndelta,
                f->d_mobs, f->d_acts, f->d_obs, N, K, 0);
        }
        cudaStreamSynchronize(f->st);
        double us = (br_now_ns() - t0) / (1000.0 * iters);
        if (us < best) best = us;
    }
    return best;
}

double br_fleet_bench_rl(br_fleet_t *f, int K, int iters) {
    int N = f->n_worlds, blk = 128, grid = (N + blk - 1) / blk;
    int g_relu = ((N * 256 / 2) + 255) / 256;
    int g_arg  = (N + 255) / 256;
    const __half alpha = __float2half(1.0f), beta = __float2half(0.0f);

    auto step_fn = [&]() {
        k_br_rl_sim_obs<<<grid, blk, 0, f->st>>>(
            f->d_seed_bits, f->num_seeds, f->d_qx, f->d_qy, f->d_qz,
            f->d_qvx, f->d_qvy, f->d_qvz, f->d_ground, f->d_deltas, f->d_ndelta,
            f->d_mobs, f->d_acts, f->d_obs, N, K, 1);
        cublasGemmEx(f->cublas, CUBLAS_OP_N, CUBLAS_OP_N, 256, N, 64, &alpha,
                     f->d_w1, CUDA_R_16F, 256, f->d_obs, CUDA_R_16F, 64, &beta,
                     f->d_h1, CUDA_R_16F, 256, CUBLAS_COMPUTE_16F, CUBLAS_GEMM_DEFAULT_TENSOR_OP);
        k_br_relu_h2<<<g_relu, 256, 0, f->st>>>((__half2 *)f->d_h1, N * 128);
        cublasGemmEx(f->cublas, CUBLAS_OP_N, CUBLAS_OP_N, 256, N, 256, &alpha,
                     f->d_w2, CUDA_R_16F, 256, f->d_h1, CUDA_R_16F, 256, &beta,
                     f->d_h2, CUDA_R_16F, 256, CUBLAS_COMPUTE_16F, CUBLAS_GEMM_DEFAULT_TENSOR_OP);
        k_br_relu_h2<<<g_relu, 256, 0, f->st>>>((__half2 *)f->d_h2, N * 128);
        cublasGemmEx(f->cublas, CUBLAS_OP_N, CUBLAS_OP_N, 32, N, 256, &alpha,
                     f->d_w3, CUDA_R_16F, 32, f->d_h2, CUDA_R_16F, 256, &beta,
                     f->d_log, CUDA_R_16F, 32, CUBLAS_COMPUTE_16F, CUBLAS_GEMM_DEFAULT_TENSOR_OP);
        k_br_argmax32<<<g_arg, 256, 0, f->st>>>(f->d_log, f->d_acts, N);
    };

    step_fn();
    cudaStreamSynchronize(f->st);
    cudaGraph_t g; cudaGraphExec_t e;
    cudaStreamBeginCapture(f->st, cudaStreamCaptureModeGlobal);
    step_fn();
    cudaStreamEndCapture(f->st, &g);
    cudaGraphInstantiate(&e, g, 0);

    double best = 1e18;
    for (int r = 0; r < 5; r++) {
        double t0 = br_now_ns();
        for (int k = 0; k < iters; k++) cudaGraphLaunch(e, f->st);
        cudaStreamSynchronize(f->st);
        double us = (br_now_ns() - t0) / (1000.0 * iters);
        if (us < best) best = us;
    }
    cudaGraphExecDestroy(e);
    cudaGraphDestroy(g);
    return best;
}
