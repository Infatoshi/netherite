#include "br_portal_sim.h"

#include <cuda_runtime.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================================
 * Constant Memory Tables (1-cycle broadcast across warps)
 * ============================================================================ */
__constant__ float c_cam_deg[BRC_CAM_BINS];
__constant__ float c_obs_pitch_off[4] = {-24.0f, -8.0f, 8.0f, 24.0f};

static double host_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static uint64_t host_sm64(uint64_t *x) {
    uint64_t z = (*x += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

/* ============================================================================
 * GPU Fleet Struct (SoA Layout for Coalesced Access on Blackwell sm_120a)
 * ============================================================================ */
typedef struct {
    float    spawn_x, spawn_y, spawn_z;
    float    spawn_yaw, spawn_pitch;
    float    lava_x, lava_y, lava_z;
    uint16_t hotbar_item[9];
    uint8_t  hotbar_cnt[9];
    uint8_t  sel_slot;
} d_seed_meta_t;

struct br_portal_sim {
    int n_worlds;
    int num_seeds;
    int max_ticks;
    cudaStream_t st;

    /* Shared read-only CoW terrain (persisting L2 pinned) */
    uint8_t       *d_seed_blocks; /* num_seeds * BRC_BLOCKS */
    uint8_t       *d_seed_meta;   /* num_seeds * BRC_BLOCKS */
    uint32_t      *d_seed_bits;   /* num_seeds * BRC_BIT_WORDS */
    d_seed_meta_t *d_smeta;       /* num_seeds */

    /* Block-local FP32 player position + velocity (SoA) */
    int16_t *d_bx, *d_by, *d_bz;
    float   *d_fx, *d_fy, *d_fz;
    float   *d_vx, *d_vy, *d_vz;
    float   *d_yaw, *d_pitch;
    float   *d_hp;
    float   *d_prev_lava_dist;

    /* Per-world player flags & timers (SoA) */
    uint8_t  *d_ground, *d_sneak, *d_jticks;
    uint8_t  *d_use_cd, *d_mine_cd, *d_hurt_cd;
    uint8_t  *d_sel_slot, *d_portal_lit, *d_in_portal, *d_dead;
    uint16_t *d_ep_tick, *d_obs_placed, *d_portal_blks;

    /* Per-world 9-slot hotbar (SoA: slot * N + i) */
    uint16_t *d_hb_item; /* 9 * N */
    uint8_t  *d_hb_cnt;  /* 9 * N */

    /* Per-world sparse block delta ring (SoA: slot * N + i, BRC_DELTA_CAP=64) */
    uint32_t *d_deltas;  /* 64 * N */
    uint8_t  *d_ndelta;  /* N */

    /* Action / obs / reward / done buffers */
    brc_s1_action_t *d_acts;
    float           *d_obs;   /* N * BRC_OBS_DIM (128) */
    float           *d_rew;   /* N */
    uint8_t         *d_done;  /* N */
};

/* ============================================================================
 * Device Helpers: Block Queries, Delta Ring, AABB Sweep, DDA Raycast, Portal
 * ============================================================================ */
static __device__ __forceinline__ int d_blk_idx(int x, int y, int z) {
    return ((y & 31) << 12) | ((z & 63) << 6) | (x & 63);
}

static __device__ __forceinline__ int d_blk_is_solid(uint8_t b) {
    if (b == 0 || b == 6 || b == 8 || b == 9 || b == 10 || b == 11 ||
        b == 31 || b == 32 || b == 37 || b == 38 || b == 39 || b == 40 ||
        b == 50 || b == 51 || b == 55 || b == 59 || b == 63 || b == 65 ||
        b == 68 || b == 69 || b == 75 || b == 76 || b == 77 || b == 78 ||
        b == 83 || b == 90 || b == 106 || b == 111 || b == 141 || b == 142 || b == 175)
        return 0;
    return 1;
}

static __device__ __forceinline__ float d_blk_class_code(uint8_t b) {
    if (b == BRC_BLK_AIR) return 0.0f;
    if (b == BRC_BLK_LAVA || b == BRC_BLK_FLOW_LAVA) return 0.50f;
    if (b == BRC_BLK_WATER || b == BRC_BLK_FLOW_WATER) return 0.75f;
    if (b == BRC_BLK_OBSIDIAN) return 0.90f;
    if (b == BRC_BLK_PORTAL || b == BRC_BLK_FIRE) return 1.00f;
    return 0.25f;
}

static __device__ __forceinline__ int d_is_water(uint8_t b) {
    return b == BRC_BLK_WATER || b == BRC_BLK_FLOW_WATER;
}

static __device__ __forceinline__ int d_is_lava(uint8_t b) {
    return b == BRC_BLK_LAVA || b == BRC_BLK_FLOW_LAVA;
}

static __device__ __forceinline__ int d_is_replaceable(uint8_t b) {
    return b == BRC_BLK_AIR || d_is_water(b) || d_is_lava(b) || b == BRC_BLK_FIRE;
}

static __device__ __forceinline__ int d_is_build_item(uint16_t item) {
    return item == BRC_ITEM_COBBLE || item == BRC_ITEM_DIRT || item == BRC_ITEM_STONE;
}

static __device__ __forceinline__ uint32_t d_delta_pack(int x, int y, int z, uint8_t id, uint8_t meta) {
    uint32_t key = (uint32_t)(((y & 31) << 12) | ((z & 63) << 6) | (x & 63));
    uint32_t sol = (uint32_t)(d_blk_is_solid(id) ? 1 : 0);
    return key | (sol << 17) | (((uint32_t)id & 0xFFu) << 18) | (((uint32_t)meta & 0xFu) << 26);
}

/* Update existing (x,y,z) delta slot in-place if present; otherwise append to ring */
static __device__ __forceinline__ void d_set_block(
    uint32_t *loc_d, int *nloc_p, int x, int y, int z, uint8_t id, uint8_t meta
) {
    if ((unsigned)x >= BRC_WX || (unsigned)y >= BRC_WY || (unsigned)z >= BRC_WZ) return;
    uint32_t entry = d_delta_pack(x, y, z, id, meta);
    uint32_t key = entry & 0x1FFFFu;
    int nloc = *nloc_p;
    for (int d = 0; d < nloc; d++) {
        if ((loc_d[d] & 0x1FFFFu) == key) {
            loc_d[d] = entry;
            return;
        }
    }
    if (nloc < BRC_DELTA_CAP) {
        loc_d[nloc] = entry;
        *nloc_p = nloc + 1;
    } else {
        loc_d[BRC_DELTA_CAP - 1] = entry;
    }
}

static __device__ __forceinline__ uint8_t d_get_block_meta(
    const uint8_t *__restrict__ blocks,
    const uint8_t *__restrict__ metas,
    const uint32_t *__restrict__ loc_d, int nloc,
    int x, int y, int z, uint8_t *out_meta
) {
    if ((unsigned)x >= BRC_WX || (unsigned)y >= BRC_WY || (unsigned)z >= BRC_WZ) {
        if (out_meta) *out_meta = 0;
        return BRC_BLK_STONE;
    }
    if (nloc > 0) {
        uint32_t key = (uint32_t)((y << 12) | (z << 6) | x);
        for (int d = nloc - 1; d >= 0; d--) {
            uint32_t e = loc_d[d];
            if ((e & 0x1FFFFu) == key) {
                if (out_meta) *out_meta = (uint8_t)((e >> 26) & 0xFu);
                return (uint8_t)((e >> 18) & 0xFFu);
            }
        }
    }
    int idx = d_blk_idx(x, y, z);
    if (out_meta) *out_meta = __ldg(&metas[idx]);
    return __ldg(&blocks[idx]);
}

static __device__ __forceinline__ int d_is_solid(
    const uint32_t *__restrict__ bits,
    const uint32_t *__restrict__ loc_d, int nloc,
    int x, int y, int z
) {
    if ((unsigned)x >= BRC_WX || (unsigned)y >= BRC_WY || (unsigned)z >= BRC_WZ) return 1;
    if (nloc > 0) {
        uint32_t key = (uint32_t)((y << 12) | (z << 6) | x);
        for (int d = nloc - 1; d >= 0; d--) {
            uint32_t e = loc_d[d];
            if ((e & 0x1FFFFu) == key) return (int)((e >> 17) & 1u);
        }
    }
    int idx = d_blk_idx(x, y, z);
    return (int)((__ldg(&bits[idx >> 5]) >> (idx & 31)) & 1u);
}

/* ============================================================================
 * 1.7.10 AABB Collision Helpers (Block-Local Coordinates Relative to bx,by,bz)
 * ============================================================================ */
static __device__ __forceinline__ int d_aabb_collides_any(
    const uint32_t *__restrict__ bits, const uint32_t *__restrict__ loc_d, int nloc,
    int bx, int by, int bz,
    float minx, float miny, float minz, float maxx, float maxy, float maxz
) {
    int x0 = (int)floorf(minx);
    int x1 = (int)floorf(maxx);
    int y0 = (int)floorf(miny);
    int y1 = (int)floorf(maxy);
    int z0 = (int)floorf(minz);
    int z1 = (int)floorf(maxz);
    for (int ox = x0; ox <= x1; ox++) {
        float bx0 = (float)ox, bx1 = bx0 + 1.0f;
        if (!(maxx > bx0 && minx < bx1)) continue;
        for (int oz = z0; oz <= z1; oz++) {
            float bz0 = (float)oz, bz1 = bz0 + 1.0f;
            if (!(maxz > bz0 && minz < bz1)) continue;
            for (int oy = y0; oy <= y1; oy++) {
                float by0 = (float)oy, by1 = by0 + 1.0f;
                if (!(maxy > by0 && miny < by1)) continue;
                if (d_is_solid(bits, loc_d, nloc, bx + ox, by + oy, bz + oz)) return 1;
            }
        }
    }
    return 0;
}

static __device__ __forceinline__ float d_sweep_y(
    const uint32_t *__restrict__ bits, const uint32_t *__restrict__ loc_d, int nloc,
    int bx, int by, int bz,
    float minx, float miny, float minz, float maxx, float maxy, float maxz, float dy
) {
    if (dy == 0.0f) return 0.0f;
    float cdy = dy;
    int x0 = (int)floorf(minx), x1 = (int)floorf(maxx);
    int z0 = (int)floorf(minz), z1 = (int)floorf(maxz);
    int y0 = (int)floorf(fminf(miny, miny + dy));
    int y1 = (int)floorf(fmaxf(maxy, maxy + dy));
    for (int ox = x0; ox <= x1; ox++) {
        float bx0 = (float)ox, bx1 = bx0 + 1.0f;
        if (!(maxx > bx0 && minx < bx1)) continue;
        for (int oz = z0; oz <= z1; oz++) {
            float bz0 = (float)oz, bz1 = bz0 + 1.0f;
            if (!(maxz > bz0 && minz < bz1)) continue;
            for (int oy = y0; oy <= y1; oy++) {
                if (!d_is_solid(bits, loc_d, nloc, bx + ox, by + oy, bz + oz)) continue;
                float by0 = (float)oy, by1 = by0 + 1.0f;
                if (cdy > 0.0f && maxy <= by0) { float d = by0 - maxy; if (d < cdy) cdy = d; }
                if (cdy < 0.0f && miny >= by1) { float d = by1 - miny; if (d > cdy) cdy = d; }
            }
        }
    }
    return cdy;
}

static __device__ __forceinline__ float d_sweep_x(
    const uint32_t *__restrict__ bits, const uint32_t *__restrict__ loc_d, int nloc,
    int bx, int by, int bz,
    float minx, float miny, float minz, float maxx, float maxy, float maxz, float dx
) {
    if (dx == 0.0f) return 0.0f;
    float cdx = dx;
    int x0 = (int)floorf(fminf(minx, minx + dx));
    int x1 = (int)floorf(fmaxf(maxx, maxx + dx));
    int y0 = (int)floorf(miny), y1 = (int)floorf(maxy);
    int z0 = (int)floorf(minz), z1 = (int)floorf(maxz);
    for (int ox = x0; ox <= x1; ox++) {
        float bx0 = (float)ox, bx1 = bx0 + 1.0f;
        for (int oz = z0; oz <= z1; oz++) {
            float bz0 = (float)oz, bz1 = bz0 + 1.0f;
            if (!(maxz > bz0 && minz < bz1)) continue;
            for (int oy = y0; oy <= y1; oy++) {
                float by0 = (float)oy, by1 = by0 + 1.0f;
                if (!(maxy > by0 && miny < by1)) continue;
                if (!d_is_solid(bits, loc_d, nloc, bx + ox, by + oy, bz + oz)) continue;
                if (cdx > 0.0f && maxx <= bx0) { float d = bx0 - maxx; if (d < cdx) cdx = d; }
                if (cdx < 0.0f && minx >= bx1) { float d = bx1 - minx; if (d > cdx) cdx = d; }
            }
        }
    }
    return cdx;
}

static __device__ __forceinline__ float d_sweep_z(
    const uint32_t *__restrict__ bits, const uint32_t *__restrict__ loc_d, int nloc,
    int bx, int by, int bz,
    float minx, float miny, float minz, float maxx, float maxy, float maxz, float dz
) {
    if (dz == 0.0f) return 0.0f;
    float cdz = dz;
    int x0 = (int)floorf(minx), x1 = (int)floorf(maxx);
    int y0 = (int)floorf(miny), y1 = (int)floorf(maxy);
    int z0 = (int)floorf(fminf(minz, minz + dz));
    int z1 = (int)floorf(fmaxf(maxz, maxz + dz));
    for (int ox = x0; ox <= x1; ox++) {
        float bx0 = (float)ox, bx1 = bx0 + 1.0f;
        if (!(maxx > bx0 && minx < bx1)) continue;
        for (int oz = z0; oz <= z1; oz++) {
            float bz0 = (float)oz, bz1 = bz0 + 1.0f;
            for (int oy = y0; oy <= y1; oy++) {
                float by0 = (float)oy, by1 = by0 + 1.0f;
                if (!(maxy > by0 && miny < by1)) continue;
                if (!d_is_solid(bits, loc_d, nloc, bx + ox, by + oy, bz + oz)) continue;
                if (cdz > 0.0f && maxz <= bz0) { float d = bz0 - maxz; if (d < cdz) cdz = d; }
                if (cdz < 0.0f && minz >= bz1) { float d = bz1 - minz; if (d > cdz) cdz = d; }
            }
        }
    }
    return cdz;
}

/* ============================================================================
 * Exact 3D Amanatides-Woo DDA Raycast
 *   mode = 0 : obs / crosshair (stops on ANY non-air block)
 *   mode = 1 : solid interaction (mine / place / full bucket / flint-and-steel)
 *   mode = 2 : empty bucket pickup (stops on solid OR liquid source meta==0)
 * Returns 1 on hit with (hx,hy,hz), entry face normal (nx,ny,nz), id, meta, dist.
 * ============================================================================ */
static __device__ __forceinline__ int d_dda_raycast(
    const uint8_t *__restrict__ blocks,
    const uint8_t *__restrict__ metas,
    const uint32_t *__restrict__ loc_d, int nloc,
    float ox, float oy, float oz,
    float yaw_deg, float pitch_deg,
    float max_dist, int mode,
    int *hx, int *hy, int *hz,
    int *nx, int *ny, int *nz,
    uint8_t *out_id, uint8_t *out_meta, float *out_dist
) {
    const float rad = 0.017453292519943295f;
    float yr = yaw_deg * rad;
    float pr = pitch_deg * rad;
    float cp = cosf(pr);
    float rx = -sinf(yr) * cp;
    float ry = -sinf(pr);
    float rz =  cosf(yr) * cp;

    int ix = (int)floorf(ox);
    int iy = (int)floorf(oy);
    int iz = (int)floorf(oz);

    /* For visual obs rays, check starting cell first (e.g. eye in water/lava) */
    if (mode == 0) {
        uint8_t sm = 0;
        uint8_t sid = d_get_block_meta(blocks, metas, loc_d, nloc, ix, iy, iz, &sm);
        if (sid != BRC_BLK_AIR) {
            if (hx) *hx = ix; if (hy) *hy = iy; if (hz) *hz = iz;
            if (nx) *nx = 0;  if (ny) *ny = 0;  if (nz) *nz = 0;
            if (out_id) *out_id = sid;
            if (out_meta) *out_meta = sm;
            if (out_dist) *out_dist = 0.0f;
            return 1;
        }
    }

    int stepx = (rx > 1e-8f) ? 1 : ((rx < -1e-8f) ? -1 : 0);
    int stepy = (ry > 1e-8f) ? 1 : ((ry < -1e-8f) ? -1 : 0);
    int stepz = (rz > 1e-8f) ? 1 : ((rz < -1e-8f) ? -1 : 0);

    float tdx = (stepx != 0) ? fabsf(1.0f / rx) : 1e30f;
    float tdy = (stepy != 0) ? fabsf(1.0f / ry) : 1e30f;
    float tdz = (stepz != 0) ? fabsf(1.0f / rz) : 1e30f;

    float tmax_x = (stepx > 0) ? ((float)(ix + 1) - ox) * tdx :
                   ((stepx < 0) ? (ox - (float)ix) * tdx : 1e30f);
    float tmax_y = (stepy > 0) ? ((float)(iy + 1) - oy) * tdy :
                   ((stepy < 0) ? (oy - (float)iy) * tdy : 1e30f);
    float tmax_z = (stepz > 0) ? ((float)(iz + 1) - oz) * tdz :
                   ((stepz < 0) ? (oz - (float)iz) * tdz : 1e30f);

    int max_steps = (max_dist > 5.0f) ? 40 : 18;
    float t = 0.0f;
    int fnx = 0, fny = 0, fnz = 0;

    for (int s = 0; s < max_steps; s++) {
        if (tmax_x <= tmax_y && tmax_x <= tmax_z) {
            t = tmax_x;
            ix += stepx;
            tmax_x += tdx;
            fnx = -stepx; fny = 0; fnz = 0;
        } else if (tmax_y <= tmax_z) {
            t = tmax_y;
            iy += stepy;
            tmax_y += tdy;
            fnx = 0; fny = -stepy; fnz = 0;
        } else {
            t = tmax_z;
            iz += stepz;
            tmax_z += tdz;
            fnx = 0; fny = 0; fnz = -stepz;
        }
        if (t > max_dist) break;

        uint8_t bm = 0;
        uint8_t bid = d_get_block_meta(blocks, metas, loc_d, nloc, ix, iy, iz, &bm);
        int hit = 0;
        if (mode == 0) {
            hit = (bid != BRC_BLK_AIR);
        } else if (mode == 1) {
            hit = d_blk_is_solid(bid);
        } else {
            /* mode 2: empty bucket hits solid OR water/lava source (meta==0) */
            hit = d_blk_is_solid(bid) || ((d_is_water(bid) || d_is_lava(bid)) && bm == 0);
        }
        if (hit) {
            if (hx) *hx = ix; if (hy) *hy = iy; if (hz) *hz = iz;
            if (nx) *nx = fnx; if (ny) *ny = fny; if (nz) *nz = fnz;
            if (out_id) *out_id = bid;
            if (out_meta) *out_meta = bm;
            if (out_dist) *out_dist = t;
            return 1;
        }
    }
    if (out_dist) *out_dist = max_dist;
    return 0;
}

/* ============================================================================
 * 1.7.10 Liquid Quench Helpers (Matching csrc/engine/blockcb.c::lava_water_check)
 * ============================================================================ */
static __device__ __forceinline__ void d_quench_lava_at(
    const uint8_t *__restrict__ blocks, const uint8_t *__restrict__ metas,
    uint32_t *loc_d, int *nloc_p,
    int lx, int ly, int lz, uint16_t *obs_placed
) {
    if ((unsigned)lx >= BRC_WX || (unsigned)ly >= BRC_WY || (unsigned)lz >= BRC_WZ) return;
    uint8_t lm = 0;
    uint8_t lid = d_get_block_meta(blocks, metas, loc_d, *nloc_p, lx, ly, lz, &lm);
    if (!d_is_lava(lid)) return;
    if (lm == 0) {
        d_set_block(loc_d, nloc_p, lx, ly, lz, BRC_BLK_OBSIDIAN, 0);
        (*obs_placed)++;
    } else if (lm <= 4) {
        d_set_block(loc_d, nloc_p, lx, ly, lz, BRC_BLK_COBBLE, 0);
    }
}

/* Quench any lava directly touched by water at (wx, wy, wz) (4 horiz + below y-1) */
static __device__ __forceinline__ void d_water_quench_neighbors(
    const uint8_t *__restrict__ blocks, const uint8_t *__restrict__ metas,
    uint32_t *loc_d, int *nloc_p,
    int wx, int wy, int wz, uint16_t *obs_placed
) {
    d_quench_lava_at(blocks, metas, loc_d, nloc_p, wx + 1, wy, wz, obs_placed);
    d_quench_lava_at(blocks, metas, loc_d, nloc_p, wx - 1, wy, wz, obs_placed);
    d_quench_lava_at(blocks, metas, loc_d, nloc_p, wx, wy, wz + 1, obs_placed);
    d_quench_lava_at(blocks, metas, loc_d, nloc_p, wx, wy, wz - 1, obs_placed);
    d_quench_lava_at(blocks, metas, loc_d, nloc_p, wx, wy - 1, wz, obs_placed);
}

/* Full 1.7.10 local liquid quench: direct 6-neighbors + 1-block flow step */
static __device__ __forceinline__ void d_water_place_and_quench(
    const uint8_t *__restrict__ blocks, const uint8_t *__restrict__ metas,
    uint32_t *loc_d, int *nloc_p,
    int wx, int wy, int wz, uint16_t *obs_placed
) {
    d_set_block(loc_d, nloc_p, wx, wy, wz, BRC_BLK_WATER, 0);
    /* Direct neighbors of placed water source */
    d_water_quench_neighbors(blocks, metas, loc_d, nloc_p, wx, wy, wz, obs_placed);

    /* 1-block water flow step: downward if below is air, else horizontal into adjacent air */
    uint8_t bm = 0;
    uint8_t below_id = d_get_block_meta(blocks, metas, loc_d, *nloc_p, wx, wy - 1, wz, &bm);
    if (below_id == BRC_BLK_AIR) {
        d_water_quench_neighbors(blocks, metas, loc_d, nloc_p, wx, wy - 1, wz, obs_placed);
    } else if (d_blk_is_solid(below_id)) {
        const int hdx[4] = {1, -1, 0, 0};
        const int hdz[4] = {0, 0, 1, -1};
        #pragma unroll
        for (int k = 0; k < 4; k++) {
            int fx = wx + hdx[k], fz = wz + hdz[k];
            uint8_t fid = d_get_block_meta(blocks, metas, loc_d, *nloc_p, fx, wy, fz, &bm);
            if (fid == BRC_BLK_AIR) {
                d_water_quench_neighbors(blocks, metas, loc_d, nloc_p, fx, wy, fz, obs_placed);
            }
        }
    }
}

/* ============================================================================
 * Exact 1.7.10 Nether Portal Frame Validator (csrc/engine/blockcb.c:635-805)
 * Validates 2..21 wide x 3..21 tall interior (4x5 minimum = 10 obsidian) along
 * axis 1 (X) or axis 2 (Z), fills interior with BRC_BLK_PORTAL (90), returns
 * portal block count (0 if invalid).
 * ============================================================================ */
static __device__ __forceinline__ int d_portal_interior_ok(uint8_t b) {
    return b == BRC_BLK_AIR || b == BRC_BLK_FIRE || b == BRC_BLK_PORTAL;
}

static __device__ __forceinline__ int d_try_create_portal_axis(
    const uint8_t *__restrict__ blocks, const uint8_t *__restrict__ metas,
    uint32_t *loc_d, int *nloc_p,
    int x, int y, int z, int axis
) {
    /* axis 1: width along +X/-X (dx=1, dz=0); axis 2: width along +Z/-Z (dx=0, dz=1) */
    int dx = (axis == 1) ? 1 : 0;
    int dz = (axis == 2) ? 1 : 0;
    uint8_t bm = 0;

    /* 1. Descend to bottom interior row */
    int bot = y;
    while (bot > 1 && bot > y - 21) {
        uint8_t below = d_get_block_meta(blocks, metas, loc_d, *nloc_p, x, bot - 1, z, &bm);
        if (!d_portal_interior_ok(below)) break;
        bot--;
    }

    /* 2. Walk in negative axis direction to find origin (left/north edge) */
    int neg = 0;
    for (int i = 0; i < 22; i++) {
        int cx = x - dx * i, cz = z - dz * i;
        if (!d_portal_interior_ok(d_get_block_meta(blocks, metas, loc_d, *nloc_p, cx, bot, cz, &bm))) break;
        if (d_get_block_meta(blocks, metas, loc_d, *nloc_p, cx, bot - 1, cz, &bm) != BRC_BLK_OBSIDIAN) break;
        neg = i + 1;
    }
    /* Cell immediately past neg must be OBSIDIAN */
    if (neg == 0) return 0;
    if (d_get_block_meta(blocks, metas, loc_d, *nloc_p, x - dx * neg, bot, z - dz * neg, &bm) != BRC_BLK_OBSIDIAN)
        return 0;

    int ox = x - dx * (neg - 1);
    int oz = z - dz * (neg - 1);
    int oy = bot;

    /* 3. Walk in positive axis direction from (ox, oy, oz) to measure width */
    int width = 0;
    for (int i = 0; i < 22; i++) {
        int cx = ox + dx * i, cz = oz + dz * i;
        if (!d_portal_interior_ok(d_get_block_meta(blocks, metas, loc_d, *nloc_p, cx, oy, cz, &bm))) break;
        if (d_get_block_meta(blocks, metas, loc_d, *nloc_p, cx, oy - 1, cz, &bm) != BRC_BLK_OBSIDIAN) break;
        width = i + 1;
    }
    if (width < 2 || width > 21) return 0;
    if (d_get_block_meta(blocks, metas, loc_d, *nloc_p, ox + dx * width, oy, oz + dz * width, &bm) != BRC_BLK_OBSIDIAN)
        return 0;

    /* 4. Measure interior height and verify side pillars */
    int height = 0;
    for (int g = 0; g < 21; g++) {
        int cy = oy + g;
        int broken = 0;
        for (int i = 0; i < width; i++) {
            int cx = ox + dx * i, cz = oz + dz * i;
            uint8_t b = d_get_block_meta(blocks, metas, loc_d, *nloc_p, cx, cy, cz, &bm);
            if (!d_portal_interior_ok(b)) { broken = 1; break; }
            if (i == 0 && d_get_block_meta(blocks, metas, loc_d, *nloc_p, cx - dx, cy, cz - dz, &bm) != BRC_BLK_OBSIDIAN)
                { broken = 1; break; }
            if (i == width - 1 && d_get_block_meta(blocks, metas, loc_d, *nloc_p, cx + dx, cy, cz + dz, &bm) != BRC_BLK_OBSIDIAN)
                { broken = 1; break; }
        }
        if (broken) break;
        height = g + 1;
    }
    if (height < 3 || height > 21) return 0;

    /* 5. Verify top obsidian bar across full width */
    for (int i = 0; i < width; i++) {
        int cx = ox + dx * i, cz = oz + dz * i;
        if (d_get_block_meta(blocks, metas, loc_d, *nloc_p, cx, oy + height, cz, &bm) != BRC_BLK_OBSIDIAN)
            return 0;
    }

    /* 6. Valid frame! Fill interior with BRC_BLK_PORTAL (90), meta = axis (1 or 2) */
    for (int i = 0; i < width; i++) {
        for (int g = 0; g < height; g++) {
            d_set_block(loc_d, nloc_p, ox + dx * i, oy + g, oz + dz * i, BRC_BLK_PORTAL, (uint8_t)axis);
        }
    }
    return width * height;
}

static __device__ __forceinline__ int d_try_create_portal(
    const uint8_t *__restrict__ blocks, const uint8_t *__restrict__ metas,
    uint32_t *loc_d, int *nloc_p, int x, int y, int z
) {
    int n = d_try_create_portal_axis(blocks, metas, loc_d, nloc_p, x, y, z, 1);
    if (n > 0) return n;
    return d_try_create_portal_axis(blocks, metas, loc_d, nloc_p, x, y, z, 2);
}

/* ============================================================================
 * Shared obs[128] v2 Extractor (Exact Match to br_contract.h:143-172)
 * ============================================================================ */
static __device__ __forceinline__ void d_write_obs_v2(
    float *__restrict__ out,
    const uint8_t *__restrict__ blocks,
    const uint8_t *__restrict__ metas,
    const uint32_t *__restrict__ loc_d, int nloc,
    float px, float py, float pz,
    float vx, float vy, float vz,
    float yaw_deg, float pitch_deg,
    float lava_x, float lava_y, float lava_z,
    uint8_t on_ground, uint8_t sneaking, float hp,
    uint8_t sel_slot, const uint16_t hb_item[9], const uint8_t hb_cnt[9],
    uint16_t obs_placed, uint8_t portal_lit,
    uint16_t ep_tick, int max_ticks
) {
    float eye_y = py + 1.62f;

    /* obs[0..63]: 4 pitch rows (-24, -8, +8, +24) x 16 yaw cols (-60..+60 deg), dist / 12.0f
     * obs[64..79]: 16 block-class codes along row 1 (pitch - 8 deg) */
    for (int r = 0; r < 4; r++) {
        float rp = fminf(fmaxf(pitch_deg + c_obs_pitch_off[r], -90.0f), 90.0f);
        for (int c = 0; c < 16; c++) {
            float ry = yaw_deg + (-60.0f + 8.0f * (float)c);
            uint8_t hid = 0;
            float dist = 12.0f;
            int hit = d_dda_raycast(blocks, metas, loc_d, nloc,
                                    px, eye_y, pz, ry, rp, 12.0f, 0,
                                    NULL, NULL, NULL, NULL, NULL, NULL,
                                    &hid, NULL, &dist);
            out[r * 16 + c] = hit ? fminf(dist * (1.0f / 12.0f), 1.0f) : 1.0f;
            if (r == 1) {
                out[64 + c] = hit ? d_blk_class_code(hid) : 0.0f;
            }
        }
    }

    const float rad = 0.017453292519943295f;
    float yr = yaw_deg * rad;
    float sy = sinf(yr), cy = cosf(yr);
    float fwd_x = -sy, fwd_z = cy;
    float right_x = -cy, right_z = -sy;

    float dlx = lava_x - px;
    float dly = lava_y - py;
    float dlz = lava_z - pz;
    float dist_xz = sqrtf(dlx * dlx + dlz * dlz);
    float dir_x = (dist_xz > 1e-5f) ? (dlx / dist_xz) : 0.0f;
    float dir_z = (dist_xz > 1e-5f) ? (dlz / dist_xz) : 0.0f;
    float dist_3d = sqrtf(dlx * dlx + dly * dly + dlz * dlz);

    out[80] = fwd_x * dir_x + fwd_z * dir_z;
    out[81] = right_x * dir_x + right_z * dir_z;
    out[82] = fminf(fmaxf(dist_3d * (1.0f / 16.0f), 0.0f), 1.0f);
    out[83] = fminf(fmaxf(dly * (1.0f / 8.0f), -1.0f), 1.0f);
    out[84] = sy;
    out[85] = cy;
    out[86] = fminf(fmaxf(pitch_deg * (1.0f / 90.0f), -1.0f), 1.0f);
    out[87] = fminf(fmaxf(vx, -1.0f), 1.0f);
    out[88] = fminf(fmaxf(vy, -1.0f), 1.0f);
    out[89] = fminf(fmaxf(vz, -1.0f), 1.0f);
    out[90] = on_ground ? 1.0f : 0.0f;
    out[91] = sneaking ? 1.0f : 0.0f;
    out[92] = fminf(fmaxf(hp * (1.0f / 20.0f), 0.0f), 1.0f);
    out[93] = (float)sel_slot * (1.0f / 8.0f);

    uint16_t hitem = hb_item[sel_slot];
    uint8_t  hcnt  = hb_cnt[sel_slot];
    out[94] = (hitem == BRC_ITEM_BUCKET_WATER && hcnt > 0) ? 1.0f : 0.0f;
    out[95] = (hitem == BRC_ITEM_BUCKET_LAVA  && hcnt > 0) ? 1.0f : 0.0f;
    out[96] = (hitem == BRC_ITEM_BUCKET_EMPTY && hcnt > 0) ? 1.0f : 0.0f;
    out[97] = (hitem == BRC_ITEM_FLINT_STEEL  && hcnt > 0) ? 1.0f : 0.0f;
    out[98] = (d_is_build_item(hitem) && hcnt > 0) ? 1.0f : 0.0f;

    int build_cnt = 0;
    #pragma unroll
    for (int s = 0; s < 9; s++) {
        if (d_is_build_item(hb_item[s])) build_cnt += (int)hb_cnt[s];
    }
    out[99]  = fminf(fmaxf((float)build_cnt * (1.0f / 64.0f), 0.0f), 1.0f);
    out[100] = fminf(fmaxf((float)obs_placed * (1.0f / 10.0f), 0.0f), 1.0f);
    out[101] = portal_lit ? 1.0f : 0.0f;

    uint8_t ch_id = 0;
    int ch_hit = d_dda_raycast(blocks, metas, loc_d, nloc,
                               px, eye_y, pz, yaw_deg, pitch_deg, 4.5f, 0,
                               NULL, NULL, NULL, NULL, NULL, NULL,
                               &ch_id, NULL, NULL);
    out[102] = ch_hit ? d_blk_class_code(ch_id) : 0.0f;
    out[103] = fminf(fmaxf((float)ep_tick / (float)(max_ticks > 0 ? max_ticks : 1), 0.0f), 1.0f);

    #pragma unroll
    for (int k = 104; k < BRC_OBS_DIM; k++) out[k] = 0.0f;
}

/* ============================================================================
 * KERNEL 1: Reset All Worlds to Assigned Seed State
 * ============================================================================ */
__global__ void k_portal_reset_all(
    const d_seed_meta_t *__restrict__ smeta, int num_seeds,
    int16_t *__restrict__ bxp, int16_t *__restrict__ byp, int16_t *__restrict__ bzp,
    float   *__restrict__ fxp, float   *__restrict__ fyp, float   *__restrict__ fzp,
    float   *__restrict__ vxp, float   *__restrict__ vyp, float   *__restrict__ vzp,
    float   *__restrict__ yawp, float  *__restrict__ pitchp,
    float   *__restrict__ hpp, float   *__restrict__ prev_ldp,
    uint8_t *__restrict__ gndp, uint8_t *__restrict__ snkp, uint8_t *__restrict__ jtp,
    uint8_t *__restrict__ ucdp, uint8_t *__restrict__ mcdp, uint8_t *__restrict__ hcdp,
    uint8_t *__restrict__ selp, uint8_t *__restrict__ plitp, uint8_t *__restrict__ inpp,
    uint8_t *__restrict__ deadp,
    uint16_t *__restrict__ eptp, uint16_t *__restrict__ obsp, uint16_t *__restrict__ pblkp,
    uint16_t *__restrict__ hb_item, uint8_t *__restrict__ hb_cnt,
    uint8_t *__restrict__ ndeltap, int N
) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= N) return;
    d_seed_meta_t sm = smeta[i % num_seeds];

    int bx = (int)floorf(sm.spawn_x);
    int by = (int)floorf(sm.spawn_y);
    int bz = (int)floorf(sm.spawn_z);
    bxp[i] = (int16_t)bx; byp[i] = (int16_t)by; bzp[i] = (int16_t)bz;
    fxp[i] = sm.spawn_x - (float)bx;
    fyp[i] = sm.spawn_y - (float)by;
    fzp[i] = sm.spawn_z - (float)bz;
    vxp[i] = 0.0f; vyp[i] = 0.0f; vzp[i] = 0.0f;
    yawp[i] = sm.spawn_yaw; pitchp[i] = sm.spawn_pitch;
    hpp[i] = 20.0f;

    float dlx = sm.lava_x - sm.spawn_x;
    float dlz = sm.lava_z - sm.spawn_z;
    prev_ldp[i] = sqrtf(dlx * dlx + dlz * dlz);

    gndp[i] = 1; snkp[i] = 0; jtp[i] = 0;
    ucdp[i] = 0; mcdp[i] = 0; hcdp[i] = 0;
    selp[i] = (sm.sel_slot < 9) ? sm.sel_slot : 0;
    plitp[i] = 0; inpp[i] = 0; deadp[i] = 0;
    eptp[i] = 0; obsp[i] = 0; pblkp[i] = 0;
    ndeltap[i] = 0;

    #pragma unroll
    for (int s = 0; s < 9; s++) {
        hb_item[(size_t)s * N + i] = sm.hotbar_item[s];
        hb_cnt[(size_t)s * N + i]  = sm.hotbar_cnt[s];
    }
}

/* ============================================================================
 * KERNEL 2: Standalone obs[128] v2 Extractor
 * ============================================================================ */
__global__ void k_portal_extract_obs(
    const uint8_t  *__restrict__ seed_blocks,
    const uint8_t  *__restrict__ seed_meta,
    const d_seed_meta_t *__restrict__ smeta,
    int num_seeds, int max_ticks,
    const int16_t  *__restrict__ bxp, const int16_t *__restrict__ byp, const int16_t *__restrict__ bzp,
    const float    *__restrict__ fxp, const float   *__restrict__ fyp, const float   *__restrict__ fzp,
    const float    *__restrict__ vxp, const float   *__restrict__ vyp, const float   *__restrict__ vzp,
    const float    *__restrict__ yawp, const float  *__restrict__ pitchp,
    const float    *__restrict__ hpp,
    const uint8_t  *__restrict__ gndp, const uint8_t *__restrict__ snkp,
    const uint8_t  *__restrict__ selp, const uint8_t *__restrict__ plitp,
    const uint16_t *__restrict__ eptp, const uint16_t *__restrict__ obsp,
    const uint16_t *__restrict__ hb_item_g, const uint8_t *__restrict__ hb_cnt_g,
    const uint32_t *__restrict__ g_deltas, const uint8_t *__restrict__ g_ndelta,
    float *__restrict__ obs_out, int N
) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= N) return;
    int sidx = i % num_seeds;
    const uint8_t *blocks = seed_blocks + (size_t)sidx * BRC_BLOCKS;
    const uint8_t *metas  = seed_meta   + (size_t)sidx * BRC_BLOCKS;
    d_seed_meta_t sm = smeta[sidx];

    uint32_t loc_d[BRC_DELTA_CAP];
    int nloc = g_ndelta[i];
    if (nloc > BRC_DELTA_CAP) nloc = BRC_DELTA_CAP;
    for (int d = 0; d < nloc; d++) loc_d[d] = g_deltas[(size_t)d * N + i];

    uint16_t hb_item[9];
    uint8_t  hb_cnt[9];
    #pragma unroll
    for (int s = 0; s < 9; s++) {
        hb_item[s] = hb_item_g[(size_t)s * N + i];
        hb_cnt[s]  = hb_cnt_g[(size_t)s * N + i];
    }

    float px = (float)bxp[i] + fxp[i];
    float py = (float)byp[i] + fyp[i];
    float pz = (float)bzp[i] + fzp[i];

    d_write_obs_v2(obs_out + (size_t)i * BRC_OBS_DIM,
                   blocks, metas, loc_d, nloc,
                   px, py, pz, vxp[i], vyp[i], vzp[i],
                   yawp[i], pitchp[i], sm.lava_x, sm.lava_y, sm.lava_z,
                   gndp[i], snkp[i], hpp[i], selp[i], hb_item, hb_cnt,
                   obsp[i], plitp[i], eptp[i], max_ticks);
}

/* ============================================================================
 * KERNEL 3: Rung 3 Portal-Casting Physics + Mechanics + Optional Obs v2 Step
 *   tape_mode == 0: actions[i] held for all `ticks` (RL K-tick step)
 *   tape_mode == 1: actions[t * N + i] per tick (deterministic replay parity)
 * ============================================================================ */
__global__ void k_portal_step(
    const uint8_t  *__restrict__ seed_blocks,
    const uint8_t  *__restrict__ seed_meta,
    const uint32_t *__restrict__ seed_bits,
    const d_seed_meta_t *__restrict__ smeta,
    int num_seeds, int max_ticks,
    int16_t  *__restrict__ bxp, int16_t *__restrict__ byp, int16_t *__restrict__ bzp,
    float    *__restrict__ fxp, float   *__restrict__ fyp, float   *__restrict__ fzp,
    float    *__restrict__ vxp, float   *__restrict__ vyp, float   *__restrict__ vzp,
    float    *__restrict__ yawp, float  *__restrict__ pitchp,
    float    *__restrict__ hpp, float   *__restrict__ prev_ldp,
    uint8_t  *__restrict__ gndp, uint8_t *__restrict__ snkp, uint8_t *__restrict__ jtp,
    uint8_t  *__restrict__ ucdp, uint8_t *__restrict__ mcdp, uint8_t *__restrict__ hcdp,
    uint8_t  *__restrict__ selp, uint8_t *__restrict__ plitp, uint8_t *__restrict__ inpp,
    uint8_t  *__restrict__ deadp,
    uint16_t *__restrict__ eptp, uint16_t *__restrict__ obsp, uint16_t *__restrict__ pblkp,
    uint16_t *__restrict__ hb_item_g, uint8_t *__restrict__ hb_cnt_g,
    uint32_t *__restrict__ g_deltas, uint8_t *__restrict__ g_ndelta,
    const brc_s1_action_t *__restrict__ acts,
    float *__restrict__ obs_out, float *__restrict__ rew_out, uint8_t *__restrict__ done_out,
    int N, int ticks, int tape_mode, int auto_reset, int write_obs
) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= N) return;

    int sidx = i % num_seeds;
    const uint8_t  *blocks = seed_blocks + (size_t)sidx * BRC_BLOCKS;
    const uint8_t  *metas  = seed_meta   + (size_t)sidx * BRC_BLOCKS;
    const uint32_t *bits   = seed_bits   + (size_t)sidx * BRC_BIT_WORDS;
    d_seed_meta_t sm = smeta[sidx];

    int bx = bxp[i], by = byp[i], bz = bzp[i];
    float fx = fxp[i], fy = fyp[i], fz = fzp[i];
    float vx = vxp[i], vy = vyp[i], vz = vzp[i];
    float yaw = yawp[i], pitch = pitchp[i];
    float hp = hpp[i];
    float prev_ld = prev_ldp[i];

    uint8_t on_ground = gndp[i], sneaking = snkp[i], jticks = jtp[i];
    uint8_t use_cd = ucdp[i], mine_cd = mcdp[i], hurt_cd = hcdp[i];
    uint8_t sel_slot = selp[i], portal_lit = plitp[i], in_portal = inpp[i], dead = deadp[i];
    uint16_t ep_tick = eptp[i], obs_placed = obsp[i], portal_blks = pblkp[i];

    uint16_t hb_item[9];
    uint8_t  hb_cnt[9];
    #pragma unroll
    for (int s = 0; s < 9; s++) {
        hb_item[s] = hb_item_g[(size_t)s * N + i];
        hb_cnt[s]  = hb_cnt_g[(size_t)s * N + i];
    }

    uint32_t loc_d[BRC_DELTA_CAP];
    int nloc = g_ndelta[i];
    if (nloc > BRC_DELTA_CAP) nloc = BRC_DELTA_CAP;
    for (int d = 0; d < nloc; d++) loc_d[d] = g_deltas[(size_t)d * N + i];

    float total_rew = 0.0f;
    uint8_t step_done = 0;

    for (int t = 0; t < ticks; t++) {
        const brc_s1_action_t act = tape_mode ? acts[(size_t)t * N + i] : acts[i];

        /* 1) Camera update via exact 41-bin mu-law table */
        int pbin = act.a[0];
        int ybin = act.a[1];
        if (pbin < 0) pbin = 0; else if (pbin >= BRC_CAM_BINS) pbin = BRC_CAM_BINS - 1;
        if (ybin < 0) ybin = 0; else if (ybin >= BRC_CAM_BINS) ybin = BRC_CAM_BINS - 1;
        /* In RL multi-tick step, apply camera delta once per step (t==0) to avoid Kx overspin */
        if (tape_mode || t == 0) {
            pitch = fminf(fmaxf(pitch + c_cam_deg[pbin], -90.0f), 90.0f);
            yaw   = yaw + c_cam_deg[ybin];
            if (yaw >= 180.0f) yaw -= 360.0f;
            else if (yaw < -180.0f) yaw += 360.0f;
        }

        /* 2) Hotbar selection (0 = keep, 1..9 = slot 0..8) */
        int hb_act = act.a[16];
        if (hb_act >= 1 && hb_act <= 9) {
            sel_slot = (uint8_t)(hb_act - 1);
        }

        int k_fwd    = act.a[2 + BRC_KEY_FORWARD] != 0;
        int k_back   = act.a[2 + BRC_KEY_BACK]    != 0;
        int k_left   = act.a[2 + BRC_KEY_LEFT]    != 0;
        int k_right  = act.a[2 + BRC_KEY_RIGHT]   != 0;
        int k_sprint = act.a[2 + BRC_KEY_SPRINT]  != 0;
        int k_jump   = act.a[2 + BRC_KEY_JUMP]    != 0;
        int k_sneak  = act.a[2 + BRC_KEY_SNEAK]   != 0;
        int k_mine   = act.a[2 + BRC_KEY_MINE]    != 0;
        int k_use    = act.a[2 + BRC_KEY_USE]     != 0;

        sneaking = (uint8_t)k_sneak;
        int eff_sprint = (k_sprint && !k_sneak && k_fwd) ? 1 : 0;

        /* 3) Tiny velocity clamp (living_on_living_update) */
        if (fabsf(vx) < 0.005f) vx = 0.0f;
        if (fabsf(vy) < 0.005f) vy = 0.0f;
        if (fabsf(vz) < 0.005f) vz = 0.0f;

        /* 4) Check lava / water / fire / portal overlap at current AABB */
        float minx = fx - 0.3f, maxx = fx + 0.3f;
        float miny = fy,        maxy = fy + 1.8f;
        float minz = fz - 0.3f, maxz = fz + 0.3f;

        int in_lava = 0, in_water = 0, in_fire = 0;
        {
            int lx0 = (int)floorf(minx + 0.1f), lx1 = (int)floorf(maxx - 0.1f);
            int ly0 = (int)floorf(miny + 0.01f), ly1 = (int)floorf(maxy - 0.4f);
            int lz0 = (int)floorf(minz + 0.1f), lz1 = (int)floorf(maxz - 0.1f);
            for (int ox = lx0; ox <= lx1; ox++) {
                for (int oz = lz0; oz <= lz1; oz++) {
                    for (int oy = ly0; oy <= ly1; oy++) {
                        uint8_t bid = d_get_block_meta(blocks, metas, loc_d, nloc, bx + ox, by + oy, bz + oz, NULL);
                        if (d_is_lava(bid))  in_lava = 1;
                        if (d_is_water(bid)) in_water = 1;
                        if (bid == BRC_BLK_FIRE) in_fire = 1;
                    }
                }
            }
        }

        /* 5) Jump timer & impulse */
        if (jticks > 0) jticks--;
        if (k_jump) {
            if (in_lava || in_water) {
                vy += 0.03999999910593033f;
            } else if (on_ground && jticks == 0) {
                vy = 0.41999998688697815f;
                if (eff_sprint) {
                    float yr = yaw * 0.017453292519943295f;
                    vx -= sinf(yr) * 0.2f;
                    vz += cosf(yr) * 0.2f;
                }
                jticks = 10;
            }
        } else {
            jticks = 0;
        }

        /* 6) Strafe / forward inputs (1.7.10 sneak * 0.3, living * 0.98) */
        float strafe = (float)(k_left - k_right);
        float fwd    = (float)(k_fwd - k_back);
        if (k_sneak) {
            strafe *= 0.3f;
            fwd    *= 0.3f;
        }
        strafe *= 0.98f;
        fwd    *= 0.98f;

        /* 7) living_move_with_heading acceleration */
        float v3 = on_ground ? (0.6f * 0.91f) : 0.91f;
        float v5;
        if (in_lava || in_water) {
            v5 = 0.02f;
        } else {
            float v4 = 0.16277136f / (v3 * v3 * v3);
            float base_spd = eff_sprint ? 0.13000001f : 0.1f;
            v5 = on_ground ? (base_spd * v4) : (eff_sprint ? 0.026f : 0.02f);
        }

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

        /* 8) Exact 1.7.10 entity_move: sneak edge-clamping + Y/X/Z sweep + 0.5 step_height retry */
        float dx = vx, dy = vy, dz = vz;
        float orig_vx = dx, orig_vy = dy, orig_vz = dz;
        int var20 = (k_sneak && on_ground) ? 1 : 0;

        if (var20) {
            const float step = 0.05f;
            while (dx != 0.0f && !d_aabb_collides_any(bits, loc_d, nloc, bx, by, bz,
                                                      minx + dx, miny - 1.0f, minz,
                                                      maxx + dx, maxy - 1.0f, maxz)) {
                if (dx < step && dx >= -step) dx = 0.0f;
                else if (dx > 0.0f) dx -= step;
                else dx += step;
                orig_vx = dx;
            }
            while (dz != 0.0f && !d_aabb_collides_any(bits, loc_d, nloc, bx, by, bz,
                                                      minx, miny - 1.0f, minz + dz,
                                                      maxx, maxy - 1.0f, maxz + dz)) {
                if (dz < step && dz >= -step) dz = 0.0f;
                else if (dz > 0.0f) dz -= step;
                else dz += step;
                orig_vz = dz;
            }
            while (dx != 0.0f && dz != 0.0f && !d_aabb_collides_any(bits, loc_d, nloc, bx, by, bz,
                                                                     minx + dx, miny - 1.0f, minz + dz,
                                                                     maxx + dx, maxy - 1.0f, maxz + dz)) {
                if (dx < step && dx >= -step) dx = 0.0f;
                else if (dx > 0.0f) dx -= step;
                else dx += step;
                if (dz < step && dz >= -step) dz = 0.0f;
                else if (dz > 0.0f) dz -= step;
                else dz += step;
                orig_vx = dx;
                orig_vz = dz;
            }
        }

        /* Save original box for step-up retry (var19 in entity.c) */
        float o_minx = minx, o_maxx = maxx;
        float o_miny = miny, o_maxy = maxy;
        float o_minz = minz, o_maxz = maxz;

        /* Primary sweep: Y -> X -> Z */
        float cdy = d_sweep_y(bits, loc_d, nloc, bx, by, bz, minx, miny, minz, maxx, maxy, maxz, dy);
        miny += cdy; maxy += cdy;
        int var37 = (on_ground != 0) || (orig_vy != cdy && orig_vy < 0.0f);

        float cdx = d_sweep_x(bits, loc_d, nloc, bx, by, bz, minx, miny, minz, maxx, maxy, maxz, dx);
        minx += cdx; maxx += cdx;

        float cdz = d_sweep_z(bits, loc_d, nloc, bx, by, bz, minx, miny, minz, maxx, maxy, maxz, dz);
        minz += cdz; maxz += cdz;

        /* 0.5-block step_height retry (exact entity.c:457-530) */
        if (var37 && (orig_vx != cdx || orig_vz != cdz)) {
            float s_minx = o_minx, s_maxx = o_maxx;
            float s_miny = o_miny, s_maxy = o_maxy;
            float s_minz = o_minz, s_maxz = o_maxz;

            float s_dy = d_sweep_y(bits, loc_d, nloc, bx, by, bz,
                                   s_minx, s_miny, s_minz, s_maxx, s_maxy, s_maxz, 0.5f);
            s_miny += s_dy; s_maxy += s_dy;

            float s_dx = d_sweep_x(bits, loc_d, nloc, bx, by, bz,
                                   s_minx, s_miny, s_minz, s_maxx, s_maxy, s_maxz, orig_vx);
            s_minx += s_dx; s_maxx += s_dx;

            float s_dz = d_sweep_z(bits, loc_d, nloc, bx, by, bz,
                                   s_minx, s_miny, s_minz, s_maxx, s_maxy, s_maxz, orig_vz);
            s_minz += s_dz; s_maxz += s_dz;

            float s_dy2 = d_sweep_y(bits, loc_d, nloc, bx, by, bz,
                                    s_minx, s_miny, s_minz, s_maxx, s_maxy, s_maxz, -0.5f);
            s_miny += s_dy2; s_maxy += s_dy2;

            if (s_dx * s_dx + s_dz * s_dz > cdx * cdx + cdz * cdz) {
                cdx = s_dx;
                cdy = s_dy2;
                cdz = s_dz;
                minx = s_minx; maxx = s_maxx;
                miny = s_miny; maxy = s_maxy;
                minz = s_minz; maxz = s_maxz;
            }
        }

        fx = 0.5f * (minx + maxx);
        fy = miny;
        fz = 0.5f * (minz + maxz);

        on_ground = (orig_vy != cdy && orig_vy < 0.0f) ? 1 : 0;
        if (orig_vx != cdx) vx = 0.0f;
        if (orig_vy != cdy) vy = 0.0f;
        if (orig_vz != cdz) vz = 0.0f;

        /* Re-normalize into block-local [0, 1) */
        int cx = (int)floorf(fx); bx += cx; fx -= (float)cx;
        int cy = (int)floorf(fy); by += cy; fy -= (float)cy;
        int cz = (int)floorf(fz); bz += cz; fz -= (float)cz;

        /* 9) Post-move drag & gravity */
        if (in_lava) {
            vx *= 0.5f;
            vy *= 0.5f;
            vz *= 0.5f;
            vy -= 0.02f;
        } else if (in_water) {
            vx *= 0.8f;
            vy *= 0.8f;
            vz *= 0.8f;
            vy -= 0.02f;
        } else {
            vy -= 0.08f;
            vy *= 0.9800000190734863f;
            vx *= v3;
            vz *= v3;
        }

        /* 10) Lava / Fire HP damage & portal entry check */
        float px = (float)bx + fx;
        float py = (float)by + fy;
        float pz = (float)bz + fz;

        if (hurt_cd > 0) hurt_cd--;
        if (in_lava) {
            if (hurt_cd == 0) {
                hp -= 4.0f;
                hurt_cd = 10;
            }
        } else if (in_fire) {
            if (hurt_cd == 0) {
                hp -= 1.0f;
                hurt_cd = 20;
            }
        }
        if (hp <= 0.0f) {
            hp = 0.0f;
            dead = 1;
            total_rew -= 2.0f;
        }

        /* Check if player body overlaps a lit BRC_BLK_PORTAL block */
        if (portal_lit && !in_portal) {
            int pbx0 = (int)floorf(px - 0.299f), pbx1 = (int)floorf(px + 0.299f);
            int pby0 = (int)floorf(py + 0.001f), pby1 = (int)floorf(py + 1.799f);
            int pbz0 = (int)floorf(pz - 0.299f), pbz1 = (int)floorf(pz + 0.299f);
            for (int qx = pbx0; qx <= pbx1 && !in_portal; qx++) {
                for (int qz = pbz0; qz <= pbz1 && !in_portal; qz++) {
                    for (int qy = pby0; qy <= pby1 && !in_portal; qy++) {
                        if (d_get_block_meta(blocks, metas, loc_d, nloc, qx, qy, qz, NULL) == BRC_BLK_PORTAL) {
                            in_portal = 1;
                            total_rew += 5.0f;
                        }
                    }
                }
            }
        }

        /* 11) BRC_KEY_MINE: break targeted solid block into AIR in delta ring */
        if (mine_cd > 0) mine_cd--;
        if (!k_mine) {
            mine_cd = 0;
        } else if (mine_cd == 0) {
            int hx = 0, hy = 0, hz = 0;
            uint8_t hid = 0;
            if (d_dda_raycast(blocks, metas, loc_d, nloc,
                              px, py + 1.62f, pz, yaw, pitch, 4.5f, 1,
                              &hx, &hy, &hz, NULL, NULL, NULL, &hid, NULL, NULL)) {
                if (hx > 0 && hx < BRC_WX - 1 && hy > 0 && hy < BRC_WY - 1 && hz > 0 && hz < BRC_WZ - 1) {
                    d_set_block(loc_d, &nloc, hx, hy, hz, BRC_BLK_AIR, 0);
                    mine_cd = 4;
                }
            }
        }

        /* 12) BRC_KEY_USE: build blocks, water/empty/lava bucket + quench, flint-and-steel portal */
        if (use_cd > 0) use_cd--;
        if (!k_use) {
            use_cd = 0;
        } else if (use_cd == 0) {
            uint16_t item = hb_item[sel_slot];
            uint8_t  cnt  = hb_cnt[sel_slot];
            uint16_t prev_obs = obs_placed;
            uint8_t  prev_lit = portal_lit;

            if (item == BRC_ITEM_BUCKET_EMPTY && cnt > 0) {
                int hx = 0, hy = 0, hz = 0;
                uint8_t hid = 0, hm = 0;
                if (d_dda_raycast(blocks, metas, loc_d, nloc,
                                  px, py + 1.62f, pz, yaw, pitch, 4.5f, 2,
                                  &hx, &hy, &hz, NULL, NULL, NULL, &hid, &hm, NULL)) {
                    if (d_is_water(hid) && hm == 0) {
                        d_set_block(loc_d, &nloc, hx, hy, hz, BRC_BLK_AIR, 0);
                        hb_item[sel_slot] = BRC_ITEM_BUCKET_WATER;
                        hb_cnt[sel_slot]  = 1;
                        use_cd = 4;
                    } else if (d_is_lava(hid) && hm == 0) {
                        d_set_block(loc_d, &nloc, hx, hy, hz, BRC_BLK_AIR, 0);
                        hb_item[sel_slot] = BRC_ITEM_BUCKET_LAVA;
                        hb_cnt[sel_slot]  = 1;
                        use_cd = 4;
                    }
                }
            } else if (item != BRC_ITEM_NONE && cnt > 0) {
                int hx = 0, hy = 0, hz = 0, nx = 0, ny = 0, nz = 0;
                if (d_dda_raycast(blocks, metas, loc_d, nloc,
                                  px, py + 1.62f, pz, yaw, pitch, 4.5f, 1,
                                  &hx, &hy, &hz, &nx, &ny, &nz, NULL, NULL, NULL)) {
                    int tx = hx + nx, ty = hy + ny, tz = hz + nz;
                    if (tx > 0 && tx < BRC_WX - 1 && ty > 0 && ty < BRC_WY - 1 && tz > 0 && tz < BRC_WZ - 1) {
                        uint8_t tid = d_get_block_meta(blocks, metas, loc_d, nloc, tx, ty, tz, NULL);

                        if (d_is_build_item(item)) {
                            /* Check replaceable & no overlap with player AABB */
                            int overlap = (px + 0.3f > (float)tx && px - 0.3f < (float)(tx + 1) &&
                                           py + 1.8f > (float)ty && py        < (float)(ty + 1) &&
                                           pz + 0.3f > (float)tz && pz - 0.3f < (float)(tz + 1));
                            if (d_is_replaceable(tid) && !overlap) {
                                d_set_block(loc_d, &nloc, tx, ty, tz, (uint8_t)item, 0);
                                hb_cnt[sel_slot] = (uint8_t)(cnt - 1);
                                if (hb_cnt[sel_slot] == 0) hb_item[sel_slot] = BRC_ITEM_NONE;
                                use_cd = 4;
                            }
                        } else if (item == BRC_ITEM_BUCKET_WATER) {
                            if (d_is_replaceable(tid)) {
                                hb_item[sel_slot] = BRC_ITEM_BUCKET_EMPTY;
                                hb_cnt[sel_slot]  = 1;
                                d_water_place_and_quench(blocks, metas, loc_d, &nloc,
                                                         tx, ty, tz, &obs_placed);
                                use_cd = 4;
                            }
                        } else if (item == BRC_ITEM_BUCKET_LAVA) {
                            if (d_is_replaceable(tid)) {
                                hb_item[sel_slot] = BRC_ITEM_BUCKET_EMPTY;
                                hb_cnt[sel_slot]  = 1;
                                /* Check 5 water-quench neighbors (x+-1, z+-1, y+1) matching blockcb.c */
                                int has_water =
                                    d_is_water(d_get_block_meta(blocks, metas, loc_d, nloc, tx + 1, ty, tz, NULL)) ||
                                    d_is_water(d_get_block_meta(blocks, metas, loc_d, nloc, tx - 1, ty, tz, NULL)) ||
                                    d_is_water(d_get_block_meta(blocks, metas, loc_d, nloc, tx, ty, tz + 1, NULL)) ||
                                    d_is_water(d_get_block_meta(blocks, metas, loc_d, nloc, tx, ty, tz - 1, NULL)) ||
                                    d_is_water(d_get_block_meta(blocks, metas, loc_d, nloc, tx, ty + 1, tz, NULL));
                                if (has_water) {
                                    d_set_block(loc_d, &nloc, tx, ty, tz, BRC_BLK_OBSIDIAN, 0);
                                    obs_placed++;
                                } else {
                                    d_set_block(loc_d, &nloc, tx, ty, tz, BRC_BLK_LAVA, 0);
                                }
                                use_cd = 4;
                            }
                        } else if (item == BRC_ITEM_FLINT_STEEL) {
                            if (tid == BRC_BLK_AIR) {
                                int np = d_try_create_portal(blocks, metas, loc_d, &nloc, tx, ty, tz);
                                if (np > 0) {
                                    portal_lit = 1;
                                    portal_blks = (uint16_t)np;
                                } else {
                                    d_set_block(loc_d, &nloc, tx, ty, tz, BRC_BLK_FIRE, 0);
                                }
                                use_cd = 4;
                            }
                        }
                    }
                }
            }

            if (obs_placed > prev_obs) {
                int gained = (int)(obs_placed - prev_obs);
                int capped = (obs_placed <= 10) ? gained : ((prev_obs < 10) ? (10 - (int)prev_obs) : 0);
                total_rew += (float)capped * 1.0f;
            }
            if (!prev_lit && portal_lit) {
                total_rew += 10.0f;
            }
        }

        /* 13) Shaping reward toward lava pool + episode tick & termination */
        float dlx = sm.lava_x - px;
        float dlz = sm.lava_z - pz;
        float cur_ld = sqrtf(dlx * dlx + dlz * dlz);
        if (obs_placed == 0) {
            total_rew += fminf(fmaxf(prev_ld - cur_ld, -0.5f), 0.5f) * 0.05f;
        }
        prev_ld = cur_ld;

        ep_tick++;
        int ep_done = (portal_lit != 0) || (dead != 0) || (ep_tick >= (uint16_t)max_ticks);
        if (ep_done) {
            step_done = 1;
            if (auto_reset) {
                bx = (int)floorf(sm.spawn_x);
                by = (int)floorf(sm.spawn_y);
                bz = (int)floorf(sm.spawn_z);
                fx = sm.spawn_x - (float)bx;
                fy = sm.spawn_y - (float)by;
                fz = sm.spawn_z - (float)bz;
                vx = 0.0f; vy = 0.0f; vz = 0.0f;
                yaw = sm.spawn_yaw; pitch = sm.spawn_pitch;
                hp = 20.0f;
                on_ground = 1; sneaking = 0; jticks = 0;
                use_cd = 0; mine_cd = 0; hurt_cd = 0;
                sel_slot = (sm.sel_slot < 9) ? sm.sel_slot : 0;
                portal_lit = 0; in_portal = 0; dead = 0;
                ep_tick = 0; obs_placed = 0; portal_blks = 0;
                nloc = 0;
                #pragma unroll
                for (int s = 0; s < 9; s++) {
                    hb_item[s] = sm.hotbar_item[s];
                    hb_cnt[s]  = sm.hotbar_cnt[s];
                }
                float rdx = sm.lava_x - sm.spawn_x;
                float rdz = sm.lava_z - sm.spawn_z;
                prev_ld = sqrtf(rdx * rdx + rdz * rdz);
                break;
            }
        }
    }

    if (write_obs && obs_out) {
        d_write_obs_v2(obs_out + (size_t)i * BRC_OBS_DIM,
                       blocks, metas, loc_d, nloc,
                       (float)bx + fx, (float)by + fy, (float)bz + fz,
                       vx, vy, vz, yaw, pitch,
                       sm.lava_x, sm.lava_y, sm.lava_z,
                       on_ground, sneaking, hp, sel_slot, hb_item, hb_cnt,
                       obs_placed, portal_lit, ep_tick, max_ticks);
    }
    if (rew_out)  rew_out[i]  = total_rew;
    if (done_out) done_out[i] = step_done;

    bxp[i] = (int16_t)bx; byp[i] = (int16_t)by; bzp[i] = (int16_t)bz;
    fxp[i] = fx; fyp[i] = fy; fzp[i] = fz;
    vxp[i] = vx; vyp[i] = vy; vzp[i] = vz;
    yawp[i] = yaw; pitchp[i] = pitch;
    hpp[i] = hp; prev_ldp[i] = prev_ld;
    gndp[i] = on_ground; snkp[i] = sneaking; jtp[i] = jticks;
    ucdp[i] = use_cd; mcdp[i] = mine_cd; hcdp[i] = hurt_cd;
    selp[i] = sel_slot; plitp[i] = portal_lit; inpp[i] = in_portal; deadp[i] = dead;
    eptp[i] = ep_tick; obsp[i] = obs_placed; pblkp[i] = portal_blks;

    #pragma unroll
    for (int s = 0; s < 9; s++) {
        hb_item_g[(size_t)s * N + i] = hb_item[s];
        hb_cnt_g[(size_t)s * N + i]  = hb_cnt[s];
    }
    for (int d = 0; d < nloc; d++) g_deltas[(size_t)d * N + i] = loc_d[d];
    g_ndelta[i] = (uint8_t)nloc;
}

/* ============================================================================
 * Host C-Callable API
 * ============================================================================ */
static void init_cam_table(void) {
    static int inited = 0;
    if (inited) return;
    float h_cam[BRC_CAM_BINS];
    for (int b = 0; b < BRC_CAM_BINS; b++) h_cam[b] = brc_cam_deg(b);
    cudaMemcpyToSymbol(c_cam_deg, h_cam, sizeof(h_cam));
    inited = 1;
}

br_portal_sim_t *br_portal_sim_create(int n_worlds, const brc_seed_entry_t *seeds,
                                      int num_seeds, int max_ticks) {
    init_cam_table();
    br_portal_sim_t *sim = (br_portal_sim_t *)calloc(1, sizeof(br_portal_sim_t));
    sim->n_worlds  = n_worlds;
    sim->num_seeds = num_seeds;
    sim->max_ticks = (max_ticks > 0) ? max_ticks : 600;
    cudaStreamCreate(&sim->st);

    size_t blk_bytes = (size_t)num_seeds * BRC_BLOCKS;
    size_t bit_bytes = (size_t)num_seeds * BRC_BIT_WORDS * sizeof(uint32_t);
    uint8_t       *h_blks = (uint8_t *)malloc(blk_bytes);
    uint8_t       *h_meta = (uint8_t *)malloc(blk_bytes);
    uint32_t      *h_bits = (uint32_t *)malloc(bit_bytes);
    d_seed_meta_t *h_sm   = (d_seed_meta_t *)calloc((size_t)num_seeds, sizeof(d_seed_meta_t));

    for (int s = 0; s < num_seeds; s++) {
        memcpy(h_blks + (size_t)s * BRC_BLOCKS, seeds[s].blocks, BRC_BLOCKS);
        memcpy(h_meta + (size_t)s * BRC_BLOCKS, seeds[s].meta, BRC_BLOCKS);
        memcpy(h_bits + (size_t)s * BRC_BIT_WORDS, seeds[s].solid_bits, BRC_BIT_WORDS * sizeof(uint32_t));
        h_sm[s].spawn_x     = seeds[s].spawn_x;
        h_sm[s].spawn_y     = seeds[s].spawn_y;
        h_sm[s].spawn_z     = seeds[s].spawn_z;
        h_sm[s].spawn_yaw   = seeds[s].spawn_yaw;
        h_sm[s].spawn_pitch = seeds[s].spawn_pitch;
        h_sm[s].lava_x      = seeds[s].lava_x;
        h_sm[s].lava_y      = seeds[s].lava_y;
        h_sm[s].lava_z      = seeds[s].lava_z;
        h_sm[s].sel_slot    = seeds[s].sel_slot;
        for (int k = 0; k < 9; k++) {
            h_sm[s].hotbar_item[k] = seeds[s].hotbar_item[k];
            h_sm[s].hotbar_cnt[k]  = seeds[s].hotbar_cnt[k];
        }
    }

    cudaMalloc(&sim->d_seed_blocks, blk_bytes);
    cudaMalloc(&sim->d_seed_meta,   blk_bytes);
    cudaMalloc(&sim->d_seed_bits,   bit_bytes);
    cudaMalloc(&sim->d_smeta, (size_t)num_seeds * sizeof(d_seed_meta_t));

    cudaMemcpy(sim->d_seed_blocks, h_blks, blk_bytes, cudaMemcpyHostToDevice);
    cudaMemcpy(sim->d_seed_meta,   h_meta, blk_bytes, cudaMemcpyHostToDevice);
    cudaMemcpy(sim->d_seed_bits,   h_bits, bit_bytes, cudaMemcpyHostToDevice);
    cudaMemcpy(sim->d_smeta,       h_sm,   (size_t)num_seeds * sizeof(d_seed_meta_t), cudaMemcpyHostToDevice);

    free(h_blks); free(h_meta); free(h_bits); free(h_sm);

    /* Pin 1-bit solid collision maps in Blackwell's persisting L2 cache */
    cudaDeviceSetLimit(cudaLimitPersistingL2CacheSize, 32 * 1024 * 1024);
    cudaStreamAttrValue attr;
    memset(&attr, 0, sizeof(attr));
    attr.accessPolicyWindow.base_ptr  = sim->d_seed_bits;
    attr.accessPolicyWindow.num_bytes = bit_bytes;
    attr.accessPolicyWindow.hitRatio  = 1.0f;
    attr.accessPolicyWindow.hitProp   = cudaAccessPropertyPersisting;
    attr.accessPolicyWindow.missProp  = cudaAccessPropertyStreaming;
    cudaStreamSetAttribute(sim->st, cudaStreamAttributeAccessPolicyWindow, &attr);

    size_t N = (size_t)n_worlds;
    cudaMalloc(&sim->d_bx, N * sizeof(int16_t));
    cudaMalloc(&sim->d_by, N * sizeof(int16_t));
    cudaMalloc(&sim->d_bz, N * sizeof(int16_t));
    cudaMalloc(&sim->d_fx, N * sizeof(float));
    cudaMalloc(&sim->d_fy, N * sizeof(float));
    cudaMalloc(&sim->d_fz, N * sizeof(float));
    cudaMalloc(&sim->d_vx, N * sizeof(float));
    cudaMalloc(&sim->d_vy, N * sizeof(float));
    cudaMalloc(&sim->d_vz, N * sizeof(float));
    cudaMalloc(&sim->d_yaw, N * sizeof(float));
    cudaMalloc(&sim->d_pitch, N * sizeof(float));
    cudaMalloc(&sim->d_hp, N * sizeof(float));
    cudaMalloc(&sim->d_prev_lava_dist, N * sizeof(float));

    cudaMalloc(&sim->d_ground, N);
    cudaMalloc(&sim->d_sneak, N);
    cudaMalloc(&sim->d_jticks, N);
    cudaMalloc(&sim->d_use_cd, N);
    cudaMalloc(&sim->d_mine_cd, N);
    cudaMalloc(&sim->d_hurt_cd, N);
    cudaMalloc(&sim->d_sel_slot, N);
    cudaMalloc(&sim->d_portal_lit, N);
    cudaMalloc(&sim->d_in_portal, N);
    cudaMalloc(&sim->d_dead, N);

    cudaMalloc(&sim->d_ep_tick, N * sizeof(uint16_t));
    cudaMalloc(&sim->d_obs_placed, N * sizeof(uint16_t));
    cudaMalloc(&sim->d_portal_blks, N * sizeof(uint16_t));

    cudaMalloc(&sim->d_hb_item, 9 * N * sizeof(uint16_t));
    cudaMalloc(&sim->d_hb_cnt,  9 * N);
    cudaMalloc(&sim->d_deltas,  (size_t)BRC_DELTA_CAP * N * sizeof(uint32_t));
    cudaMalloc(&sim->d_ndelta,  N);

    cudaMalloc(&sim->d_acts, N * sizeof(brc_s1_action_t));
    cudaMalloc(&sim->d_obs,  N * BRC_OBS_DIM * sizeof(float));
    cudaMalloc(&sim->d_rew,  N * sizeof(float));
    cudaMalloc(&sim->d_done, N);

    br_portal_sim_reset_all(sim);
    return sim;
}

void br_portal_sim_destroy(br_portal_sim_t *sim) {
    if (!sim) return;
    cudaFree(sim->d_seed_blocks); cudaFree(sim->d_seed_meta);
    cudaFree(sim->d_seed_bits);   cudaFree(sim->d_smeta);
    cudaFree(sim->d_bx); cudaFree(sim->d_by); cudaFree(sim->d_bz);
    cudaFree(sim->d_fx); cudaFree(sim->d_fy); cudaFree(sim->d_fz);
    cudaFree(sim->d_vx); cudaFree(sim->d_vy); cudaFree(sim->d_vz);
    cudaFree(sim->d_yaw); cudaFree(sim->d_pitch);
    cudaFree(sim->d_hp);  cudaFree(sim->d_prev_lava_dist);
    cudaFree(sim->d_ground); cudaFree(sim->d_sneak); cudaFree(sim->d_jticks);
    cudaFree(sim->d_use_cd); cudaFree(sim->d_mine_cd); cudaFree(sim->d_hurt_cd);
    cudaFree(sim->d_sel_slot); cudaFree(sim->d_portal_lit);
    cudaFree(sim->d_in_portal); cudaFree(sim->d_dead);
    cudaFree(sim->d_ep_tick); cudaFree(sim->d_obs_placed); cudaFree(sim->d_portal_blks);
    cudaFree(sim->d_hb_item); cudaFree(sim->d_hb_cnt);
    cudaFree(sim->d_deltas);  cudaFree(sim->d_ndelta);
    cudaFree(sim->d_acts); cudaFree(sim->d_obs);
    cudaFree(sim->d_rew);  cudaFree(sim->d_done);
    cudaStreamDestroy(sim->st);
    free(sim);
}

void br_portal_sim_reset_all(br_portal_sim_t *sim) {
    int grid = (sim->n_worlds + 255) / 256;
    k_portal_reset_all<<<grid, 256, 0, sim->st>>>(
        sim->d_smeta, sim->num_seeds,
        sim->d_bx, sim->d_by, sim->d_bz,
        sim->d_fx, sim->d_fy, sim->d_fz,
        sim->d_vx, sim->d_vy, sim->d_vz,
        sim->d_yaw, sim->d_pitch, sim->d_hp, sim->d_prev_lava_dist,
        sim->d_ground, sim->d_sneak, sim->d_jticks,
        sim->d_use_cd, sim->d_mine_cd, sim->d_hurt_cd,
        sim->d_sel_slot, sim->d_portal_lit, sim->d_in_portal, sim->d_dead,
        sim->d_ep_tick, sim->d_obs_placed, sim->d_portal_blks,
        sim->d_hb_item, sim->d_hb_cnt, sim->d_ndelta, sim->n_worlds
    );
    cudaStreamSynchronize(sim->st);
}

void br_portal_sim_upload_states(br_portal_sim_t *sim, const br_portal_state_t *st, int n) {
    int N = sim->n_worlds;
    if (n > N) n = N;
    int16_t *bx = (int16_t *)malloc(n * sizeof(int16_t));
    int16_t *by = (int16_t *)malloc(n * sizeof(int16_t));
    int16_t *bz = (int16_t *)malloc(n * sizeof(int16_t));
    float *fx = (float *)malloc(n * sizeof(float));
    float *fy = (float *)malloc(n * sizeof(float));
    float *fz = (float *)malloc(n * sizeof(float));
    float *vx = (float *)malloc(n * sizeof(float));
    float *vy = (float *)malloc(n * sizeof(float));
    float *vz = (float *)malloc(n * sizeof(float));
    float *yaw = (float *)malloc(n * sizeof(float));
    float *pitch = (float *)malloc(n * sizeof(float));
    float *hp = (float *)malloc(n * sizeof(float));
    uint8_t *gnd = (uint8_t *)malloc(n), *snk = (uint8_t *)malloc(n), *jt = (uint8_t *)malloc(n);
    uint8_t *ucd = (uint8_t *)malloc(n), *mcd = (uint8_t *)malloc(n), *hcd = (uint8_t *)malloc(n);
    uint8_t *sel = (uint8_t *)malloc(n), *plit = (uint8_t *)malloc(n), *inp = (uint8_t *)malloc(n);
    uint8_t *dead = (uint8_t *)malloc(n), *nd = (uint8_t *)malloc(n);
    uint16_t *ept = (uint16_t *)malloc(n * 2), *obsp = (uint16_t *)malloc(n * 2), *pbl = (uint16_t *)malloc(n * 2);
    uint16_t *hbi = (uint16_t *)malloc(9 * (size_t)N * sizeof(uint16_t));
    uint8_t  *hbc = (uint8_t *)malloc(9 * (size_t)N);
    uint32_t *del = (uint32_t *)calloc((size_t)BRC_DELTA_CAP * (size_t)N, sizeof(uint32_t));

    for (int i = 0; i < n; i++) {
        bx[i] = (int16_t)floor(st[i].x); by[i] = (int16_t)floor(st[i].y); bz[i] = (int16_t)floor(st[i].z);
        fx[i] = (float)(st[i].x - floor(st[i].x));
        fy[i] = (float)(st[i].y - floor(st[i].y));
        fz[i] = (float)(st[i].z - floor(st[i].z));
        vx[i] = (float)st[i].vx; vy[i] = (float)st[i].vy; vz[i] = (float)st[i].vz;
        yaw[i] = st[i].yaw; pitch[i] = st[i].pitch; hp[i] = st[i].hp;
        gnd[i] = st[i].on_ground; snk[i] = st[i].sneaking; jt[i] = st[i].jump_ticks;
        ucd[i] = st[i].use_cd; mcd[i] = st[i].mine_cd; hcd[i] = st[i].hurt_cd;
        sel[i] = st[i].sel_slot; plit[i] = st[i].portal_lit; inp[i] = st[i].in_portal; dead[i] = st[i].dead;
        ept[i] = st[i].ep_tick; obsp[i] = st[i].obsidian_placed; pbl[i] = st[i].portal_blocks;
        nd[i]  = (st[i].ndelta <= BRC_DELTA_CAP) ? st[i].ndelta : BRC_DELTA_CAP;
        for (int s = 0; s < 9; s++) {
            hbi[(size_t)s * N + i] = st[i].hotbar_item[s];
            hbc[(size_t)s * N + i] = st[i].hotbar_cnt[s];
        }
        for (int d = 0; d < nd[i]; d++) del[(size_t)d * N + i] = st[i].deltas[d];
    }

    cudaMemcpy(sim->d_bx, bx, n * 2, cudaMemcpyHostToDevice);
    cudaMemcpy(sim->d_by, by, n * 2, cudaMemcpyHostToDevice);
    cudaMemcpy(sim->d_bz, bz, n * 2, cudaMemcpyHostToDevice);
    cudaMemcpy(sim->d_fx, fx, n * 4, cudaMemcpyHostToDevice);
    cudaMemcpy(sim->d_fy, fy, n * 4, cudaMemcpyHostToDevice);
    cudaMemcpy(sim->d_fz, fz, n * 4, cudaMemcpyHostToDevice);
    cudaMemcpy(sim->d_vx, vx, n * 4, cudaMemcpyHostToDevice);
    cudaMemcpy(sim->d_vy, vy, n * 4, cudaMemcpyHostToDevice);
    cudaMemcpy(sim->d_vz, vz, n * 4, cudaMemcpyHostToDevice);
    cudaMemcpy(sim->d_yaw, yaw, n * 4, cudaMemcpyHostToDevice);
    cudaMemcpy(sim->d_pitch, pitch, n * 4, cudaMemcpyHostToDevice);
    cudaMemcpy(sim->d_hp, hp, n * 4, cudaMemcpyHostToDevice);
    cudaMemcpy(sim->d_ground, gnd, n, cudaMemcpyHostToDevice);
    cudaMemcpy(sim->d_sneak, snk, n, cudaMemcpyHostToDevice);
    cudaMemcpy(sim->d_jticks, jt, n, cudaMemcpyHostToDevice);
    cudaMemcpy(sim->d_use_cd, ucd, n, cudaMemcpyHostToDevice);
    cudaMemcpy(sim->d_mine_cd, mcd, n, cudaMemcpyHostToDevice);
    cudaMemcpy(sim->d_hurt_cd, hcd, n, cudaMemcpyHostToDevice);
    cudaMemcpy(sim->d_sel_slot, sel, n, cudaMemcpyHostToDevice);
    cudaMemcpy(sim->d_portal_lit, plit, n, cudaMemcpyHostToDevice);
    cudaMemcpy(sim->d_in_portal, inp, n, cudaMemcpyHostToDevice);
    cudaMemcpy(sim->d_dead, dead, n, cudaMemcpyHostToDevice);
    cudaMemcpy(sim->d_ep_tick, ept, n * 2, cudaMemcpyHostToDevice);
    cudaMemcpy(sim->d_obs_placed, obsp, n * 2, cudaMemcpyHostToDevice);
    cudaMemcpy(sim->d_portal_blks, pbl, n * 2, cudaMemcpyHostToDevice);
    cudaMemcpy(sim->d_ndelta, nd, n, cudaMemcpyHostToDevice);
    cudaMemcpy(sim->d_hb_item, hbi, 9 * (size_t)N * sizeof(uint16_t), cudaMemcpyHostToDevice);
    cudaMemcpy(sim->d_hb_cnt,  hbc, 9 * (size_t)N, cudaMemcpyHostToDevice);
    cudaMemcpy(sim->d_deltas,  del, (size_t)BRC_DELTA_CAP * (size_t)N * sizeof(uint32_t), cudaMemcpyHostToDevice);

    free(bx); free(by); free(bz); free(fx); free(fy); free(fz);
    free(vx); free(vy); free(vz); free(yaw); free(pitch); free(hp);
    free(gnd); free(snk); free(jt); free(ucd); free(mcd); free(hcd);
    free(sel); free(plit); free(inp); free(dead); free(nd);
    free(ept); free(obsp); free(pbl); free(hbi); free(hbc); free(del);
}

void br_portal_sim_download_states(br_portal_sim_t *sim, br_portal_state_t *st, int n) {
    int N = sim->n_worlds;
    if (n > N) n = N;
    int16_t *bx = (int16_t *)malloc(n * sizeof(int16_t));
    int16_t *by = (int16_t *)malloc(n * sizeof(int16_t));
    int16_t *bz = (int16_t *)malloc(n * sizeof(int16_t));
    float *fx = (float *)malloc(n * sizeof(float));
    float *fy = (float *)malloc(n * sizeof(float));
    float *fz = (float *)malloc(n * sizeof(float));
    float *vx = (float *)malloc(n * sizeof(float));
    float *vy = (float *)malloc(n * sizeof(float));
    float *vz = (float *)malloc(n * sizeof(float));
    float *yaw = (float *)malloc(n * sizeof(float));
    float *pitch = (float *)malloc(n * sizeof(float));
    float *hp = (float *)malloc(n * sizeof(float));
    uint8_t *gnd = (uint8_t *)malloc(n), *snk = (uint8_t *)malloc(n), *jt = (uint8_t *)malloc(n);
    uint8_t *ucd = (uint8_t *)malloc(n), *mcd = (uint8_t *)malloc(n), *hcd = (uint8_t *)malloc(n);
    uint8_t *sel = (uint8_t *)malloc(n), *plit = (uint8_t *)malloc(n), *inp = (uint8_t *)malloc(n);
    uint8_t *dead = (uint8_t *)malloc(n), *nd = (uint8_t *)malloc(n);
    uint16_t *ept = (uint16_t *)malloc(n * 2), *obsp = (uint16_t *)malloc(n * 2), *pbl = (uint16_t *)malloc(n * 2);
    uint16_t *hbi = (uint16_t *)malloc(9 * (size_t)N * sizeof(uint16_t));
    uint8_t  *hbc = (uint8_t *)malloc(9 * (size_t)N);
    uint32_t *del = (uint32_t *)malloc((size_t)BRC_DELTA_CAP * (size_t)N * sizeof(uint32_t));

    cudaStreamSynchronize(sim->st);
    cudaMemcpy(bx, sim->d_bx, n * 2, cudaMemcpyDeviceToHost);
    cudaMemcpy(by, sim->d_by, n * 2, cudaMemcpyDeviceToHost);
    cudaMemcpy(bz, sim->d_bz, n * 2, cudaMemcpyDeviceToHost);
    cudaMemcpy(fx, sim->d_fx, n * 4, cudaMemcpyDeviceToHost);
    cudaMemcpy(fy, sim->d_fy, n * 4, cudaMemcpyDeviceToHost);
    cudaMemcpy(fz, sim->d_fz, n * 4, cudaMemcpyDeviceToHost);
    cudaMemcpy(vx, sim->d_vx, n * 4, cudaMemcpyDeviceToHost);
    cudaMemcpy(vy, sim->d_vy, n * 4, cudaMemcpyDeviceToHost);
    cudaMemcpy(vz, sim->d_vz, n * 4, cudaMemcpyDeviceToHost);
    cudaMemcpy(yaw, sim->d_yaw, n * 4, cudaMemcpyDeviceToHost);
    cudaMemcpy(pitch, sim->d_pitch, n * 4, cudaMemcpyDeviceToHost);
    cudaMemcpy(hp, sim->d_hp, n * 4, cudaMemcpyDeviceToHost);
    cudaMemcpy(gnd, sim->d_ground, n, cudaMemcpyDeviceToHost);
    cudaMemcpy(snk, sim->d_sneak, n, cudaMemcpyDeviceToHost);
    cudaMemcpy(jt,  sim->d_jticks, n, cudaMemcpyDeviceToHost);
    cudaMemcpy(ucd, sim->d_use_cd, n, cudaMemcpyDeviceToHost);
    cudaMemcpy(mcd, sim->d_mine_cd, n, cudaMemcpyDeviceToHost);
    cudaMemcpy(hcd, sim->d_hurt_cd, n, cudaMemcpyDeviceToHost);
    cudaMemcpy(sel, sim->d_sel_slot, n, cudaMemcpyDeviceToHost);
    cudaMemcpy(plit, sim->d_portal_lit, n, cudaMemcpyDeviceToHost);
    cudaMemcpy(inp,  sim->d_in_portal, n, cudaMemcpyDeviceToHost);
    cudaMemcpy(dead, sim->d_dead, n, cudaMemcpyDeviceToHost);
    cudaMemcpy(ept,  sim->d_ep_tick, n * 2, cudaMemcpyDeviceToHost);
    cudaMemcpy(obsp, sim->d_obs_placed, n * 2, cudaMemcpyDeviceToHost);
    cudaMemcpy(pbl,  sim->d_portal_blks, n * 2, cudaMemcpyDeviceToHost);
    cudaMemcpy(nd,   sim->d_ndelta, n, cudaMemcpyDeviceToHost);
    cudaMemcpy(hbi,  sim->d_hb_item, 9 * (size_t)N * sizeof(uint16_t), cudaMemcpyDeviceToHost);
    cudaMemcpy(hbc,  sim->d_hb_cnt,  9 * (size_t)N, cudaMemcpyDeviceToHost);
    cudaMemcpy(del,  sim->d_deltas,  (size_t)BRC_DELTA_CAP * (size_t)N * sizeof(uint32_t), cudaMemcpyDeviceToHost);

    for (int i = 0; i < n; i++) {
        memset(&st[i], 0, sizeof(st[i]));
        st[i].x = (double)bx[i] + (double)fx[i];
        st[i].y = (double)by[i] + (double)fy[i];
        st[i].z = (double)bz[i] + (double)fz[i];
        st[i].vx = (double)vx[i]; st[i].vy = (double)vy[i]; st[i].vz = (double)vz[i];
        st[i].yaw = yaw[i]; st[i].pitch = pitch[i]; st[i].hp = hp[i];
        st[i].on_ground = gnd[i]; st[i].sneaking = snk[i]; st[i].jump_ticks = jt[i];
        st[i].use_cd = ucd[i]; st[i].mine_cd = mcd[i]; st[i].hurt_cd = hcd[i];
        st[i].sel_slot = sel[i]; st[i].portal_lit = plit[i]; st[i].in_portal = inp[i]; st[i].dead = dead[i];
        st[i].ep_tick = ept[i]; st[i].obsidian_placed = obsp[i]; st[i].portal_blocks = pbl[i];
        st[i].ndelta = (nd[i] <= BRC_DELTA_CAP) ? nd[i] : BRC_DELTA_CAP;
        for (int s = 0; s < 9; s++) {
            st[i].hotbar_item[s] = hbi[(size_t)s * N + i];
            st[i].hotbar_cnt[s]  = hbc[(size_t)s * N + i];
        }
        for (int d = 0; d < st[i].ndelta; d++) st[i].deltas[d] = del[(size_t)d * N + i];
    }

    free(bx); free(by); free(bz); free(fx); free(fy); free(fz);
    free(vx); free(vy); free(vz); free(yaw); free(pitch); free(hp);
    free(gnd); free(snk); free(jt); free(ucd); free(mcd); free(hcd);
    free(sel); free(plit); free(inp); free(dead); free(nd);
    free(ept); free(obsp); free(pbl); free(hbi); free(hbc); free(del);
}

void br_portal_sim_extract_obs(br_portal_sim_t *sim, float *d_obs_out) {
    float *dst = d_obs_out ? d_obs_out : sim->d_obs;
    int grid = (sim->n_worlds + 255) / 256;
    k_portal_extract_obs<<<grid, 256, 0, sim->st>>>(
        sim->d_seed_blocks, sim->d_seed_meta, sim->d_smeta,
        sim->num_seeds, sim->max_ticks,
        sim->d_bx, sim->d_by, sim->d_bz,
        sim->d_fx, sim->d_fy, sim->d_fz,
        sim->d_vx, sim->d_vy, sim->d_vz,
        sim->d_yaw, sim->d_pitch, sim->d_hp,
        sim->d_ground, sim->d_sneak, sim->d_sel_slot, sim->d_portal_lit,
        sim->d_ep_tick, sim->d_obs_placed,
        sim->d_hb_item, sim->d_hb_cnt,
        sim->d_deltas, sim->d_ndelta,
        dst, sim->n_worlds
    );
}

void br_portal_sim_download_obs(br_portal_sim_t *sim, float *h_obs, int n) {
    if (n > sim->n_worlds) n = sim->n_worlds;
    cudaStreamSynchronize(sim->st);
    cudaMemcpy(h_obs, sim->d_obs, (size_t)n * BRC_OBS_DIM * sizeof(float), cudaMemcpyDeviceToHost);
}

void br_portal_sim_step(br_portal_sim_t *sim,
                        const brc_s1_action_t *h_or_d_acts, int acts_on_device,
                        int ticks_per_step, int auto_reset,
                        float *d_obs_out, float *d_rew_out, uint8_t *d_done_out) {
    const brc_s1_action_t *d_acts = acts_on_device ? h_or_d_acts : sim->d_acts;
    if (!acts_on_device) {
        cudaMemcpyAsync(sim->d_acts, h_or_d_acts,
                        (size_t)sim->n_worlds * sizeof(brc_s1_action_t),
                        cudaMemcpyHostToDevice, sim->st);
    }
    float   *obs_dst  = d_obs_out  ? d_obs_out  : sim->d_obs;
    float   *rew_dst  = d_rew_out  ? d_rew_out  : sim->d_rew;
    uint8_t *done_dst = d_done_out ? d_done_out : sim->d_done;

    int grid = (sim->n_worlds + 255) / 256;
    k_portal_step<<<grid, 256, 0, sim->st>>>(
        sim->d_seed_blocks, sim->d_seed_meta, sim->d_seed_bits, sim->d_smeta,
        sim->num_seeds, sim->max_ticks,
        sim->d_bx, sim->d_by, sim->d_bz,
        sim->d_fx, sim->d_fy, sim->d_fz,
        sim->d_vx, sim->d_vy, sim->d_vz,
        sim->d_yaw, sim->d_pitch, sim->d_hp, sim->d_prev_lava_dist,
        sim->d_ground, sim->d_sneak, sim->d_jticks,
        sim->d_use_cd, sim->d_mine_cd, sim->d_hurt_cd,
        sim->d_sel_slot, sim->d_portal_lit, sim->d_in_portal, sim->d_dead,
        sim->d_ep_tick, sim->d_obs_placed, sim->d_portal_blks,
        sim->d_hb_item, sim->d_hb_cnt, sim->d_deltas, sim->d_ndelta,
        d_acts, obs_dst, rew_dst, done_dst,
        sim->n_worlds, ticks_per_step, 0, auto_reset, 1
    );
}

double br_portal_sim_replay(br_portal_sim_t *sim, const brc_s1_action_t *h_acts_tn,
                            int ticks, int auto_reset) {
    size_t act_bytes = (size_t)ticks * (size_t)sim->n_worlds * sizeof(brc_s1_action_t);
    brc_s1_action_t *d_tape = NULL;
    cudaMalloc(&d_tape, act_bytes);
    cudaMemcpy(d_tape, h_acts_tn, act_bytes, cudaMemcpyHostToDevice);

    int grid = (sim->n_worlds + 255) / 256;
    cudaStreamSynchronize(sim->st);
    double t0 = host_now_ns();
    k_portal_step<<<grid, 256, 0, sim->st>>>(
        sim->d_seed_blocks, sim->d_seed_meta, sim->d_seed_bits, sim->d_smeta,
        sim->num_seeds, sim->max_ticks,
        sim->d_bx, sim->d_by, sim->d_bz,
        sim->d_fx, sim->d_fy, sim->d_fz,
        sim->d_vx, sim->d_vy, sim->d_vz,
        sim->d_yaw, sim->d_pitch, sim->d_hp, sim->d_prev_lava_dist,
        sim->d_ground, sim->d_sneak, sim->d_jticks,
        sim->d_use_cd, sim->d_mine_cd, sim->d_hurt_cd,
        sim->d_sel_slot, sim->d_portal_lit, sim->d_in_portal, sim->d_dead,
        sim->d_ep_tick, sim->d_obs_placed, sim->d_portal_blks,
        sim->d_hb_item, sim->d_hb_cnt, sim->d_deltas, sim->d_ndelta,
        d_tape, sim->d_obs, sim->d_rew, sim->d_done,
        sim->n_worlds, ticks, 1, auto_reset, 1
    );
    cudaStreamSynchronize(sim->st);
    double us = (host_now_ns() - t0) / 1000.0;
    cudaFree(d_tape);
    return us;
}

double br_portal_sim_bench(br_portal_sim_t *sim, int ticks_per_step, int iters, int extract_obs) {
    brc_s1_action_t *h_acts = (brc_s1_action_t *)calloc((size_t)sim->n_worlds, sizeof(brc_s1_action_t));
    for (int i = 0; i < sim->n_worlds; i++) {
        h_acts[i].a[0] = 20; /* 0 deg pitch */
        h_acts[i].a[1] = 20 + ((i & 3) - 1);
        h_acts[i].a[2 + BRC_KEY_FORWARD] = 1;
        h_acts[i].a[2 + BRC_KEY_SPRINT]  = (i & 1);
        h_acts[i].a[2 + BRC_KEY_JUMP]    = ((i & 7) == 0);
        h_acts[i].a[16] = 0;
    }
    cudaMemcpy(sim->d_acts, h_acts, (size_t)sim->n_worlds * sizeof(brc_s1_action_t), cudaMemcpyHostToDevice);
    free(h_acts);

    int grid = (sim->n_worlds + 255) / 256;
    /* Warmup */
    for (int w = 0; w < 5; w++) {
        k_portal_step<<<grid, 256, 0, sim->st>>>(
            sim->d_seed_blocks, sim->d_seed_meta, sim->d_seed_bits, sim->d_smeta,
            sim->num_seeds, sim->max_ticks,
            sim->d_bx, sim->d_by, sim->d_bz,
            sim->d_fx, sim->d_fy, sim->d_fz,
            sim->d_vx, sim->d_vy, sim->d_vz,
            sim->d_yaw, sim->d_pitch, sim->d_hp, sim->d_prev_lava_dist,
            sim->d_ground, sim->d_sneak, sim->d_jticks,
            sim->d_use_cd, sim->d_mine_cd, sim->d_hurt_cd,
            sim->d_sel_slot, sim->d_portal_lit, sim->d_in_portal, sim->d_dead,
            sim->d_ep_tick, sim->d_obs_placed, sim->d_portal_blks,
            sim->d_hb_item, sim->d_hb_cnt, sim->d_deltas, sim->d_ndelta,
            sim->d_acts, sim->d_obs, sim->d_rew, sim->d_done,
            sim->n_worlds, ticks_per_step, 0, 1, extract_obs
        );
    }
    cudaStreamSynchronize(sim->st);

    double t0 = host_now_ns();
    for (int it = 0; it < iters; it++) {
        k_portal_step<<<grid, 256, 0, sim->st>>>(
            sim->d_seed_blocks, sim->d_seed_meta, sim->d_seed_bits, sim->d_smeta,
            sim->num_seeds, sim->max_ticks,
            sim->d_bx, sim->d_by, sim->d_bz,
            sim->d_fx, sim->d_fy, sim->d_fz,
            sim->d_vx, sim->d_vy, sim->d_vz,
            sim->d_yaw, sim->d_pitch, sim->d_hp, sim->d_prev_lava_dist,
            sim->d_ground, sim->d_sneak, sim->d_jticks,
            sim->d_use_cd, sim->d_mine_cd, sim->d_hurt_cd,
            sim->d_sel_slot, sim->d_portal_lit, sim->d_in_portal, sim->d_dead,
            sim->d_ep_tick, sim->d_obs_placed, sim->d_portal_blks,
            sim->d_hb_item, sim->d_hb_cnt, sim->d_deltas, sim->d_ndelta,
            sim->d_acts, sim->d_obs, sim->d_rew, sim->d_done,
            sim->n_worlds, ticks_per_step, 0, 1, extract_obs
        );
    }
    cudaStreamSynchronize(sim->st);
    double us = (host_now_ns() - t0) / 1000.0;
    return ((double)sim->n_worlds * (double)ticks_per_step * (double)iters) / us;
}

float   *br_portal_sim_obs_ptr(br_portal_sim_t *sim)  { return sim->d_obs; }
float   *br_portal_sim_rew_ptr(br_portal_sim_t *sim)  { return sim->d_rew; }
uint8_t *br_portal_sim_done_ptr(br_portal_sim_t *sim) { return sim->d_done; }
int      br_portal_sim_n_worlds(const br_portal_sim_t *sim) { return sim->n_worlds; }

/* ============================================================================
 * Host Helpers: Seed Synthesis & Delta Query
 * ============================================================================ */
static void set_seed_blk(brc_seed_entry_t *s, int x, int y, int z, uint8_t id, uint8_t meta) {
    if ((unsigned)x >= BRC_WX || (unsigned)y >= BRC_WY || (unsigned)z >= BRC_WZ) return;
    int idx = brc_blk_idx(x, y, z);
    s->blocks[idx] = id;
    s->meta[idx]   = meta;
    if (brc_blk_is_solid(id)) s->solid_bits[idx >> 5] |= (1u << (idx & 31));
    else                      s->solid_bits[idx >> 5] &= ~(1u << (idx & 31));
}

void br_portal_make_test_seed(brc_seed_entry_t *out, uint64_t rng_seed) {
    memset(out, 0, sizeof(*out));
    uint64_t rng = rng_seed ? rng_seed : 0x20261124ULL;
    snprintf(out->source_name, sizeof(out->source_name), "synth_lava_%llu", (unsigned long long)(rng & 0xFFFF));

    for (int z = 0; z < BRC_WZ; z++) {
        for (int x = 0; x < BRC_WX; x++) {
            if (x == 0 || x == BRC_WX - 1 || z == 0 || z == BRC_WZ - 1) {
                for (int y = 0; y < BRC_WY; y++) set_seed_blk(out, x, y, z, BRC_BLK_STONE, 0);
                continue;
            }
            for (int y = 0; y < 8; y++) {
                set_seed_blk(out, x, y, z, (y == 7) ? BRC_BLK_GRASS : BRC_BLK_STONE, 0);
            }
            /* Occasional 1-block hills away from the lava pool area */
            if (x > 34 && x < 56 && z > 34 && z < 56 && ((host_sm64(&rng) & 15) == 0)) {
                set_seed_blk(out, x, 8, z, BRC_BLK_GRASS, 0);
            }
        }
    }

    /* Surface lava pool: 6x6 source lava at y=7, x=19..24, z=22..27 (36 sources) */
    int lava_cnt = 0;
    for (int z = 22; z <= 27; z++) {
        for (int x = 19; x <= 24; x++) {
            set_seed_blk(out, x, 7, z, BRC_BLK_LAVA, 0);
            lava_cnt++;
        }
    }
    out->lava_x = 22.0f;
    out->lava_y = 7.5f;
    out->lava_z = 24.5f;
    out->lava_sources = lava_cnt;

    out->spawn_x = 21.5f;
    out->spawn_y = 8.0f;
    out->spawn_z = 20.5f;
    out->spawn_yaw = 0.0f;   /* facing +Z toward lava pool */
    out->spawn_pitch = 15.0f;

    out->hotbar_item[0] = BRC_ITEM_COBBLE;       out->hotbar_cnt[0] = 64;
    out->hotbar_item[1] = BRC_ITEM_BUCKET_WATER; out->hotbar_cnt[1] = 1;
    out->hotbar_item[2] = BRC_ITEM_FLINT_STEEL;  out->hotbar_cnt[2] = 1;
    out->hotbar_item[3] = BRC_ITEM_IRON_PICK;    out->hotbar_cnt[3] = 1;
    out->sel_slot = 0;
}

uint8_t br_portal_query_block(const brc_seed_entry_t *seed, const br_portal_state_t *st,
                              int x, int y, int z, uint8_t *out_meta) {
    if ((unsigned)x >= BRC_WX || (unsigned)y >= BRC_WY || (unsigned)z >= BRC_WZ) {
        if (out_meta) *out_meta = 0;
        return BRC_BLK_STONE;
    }
    uint32_t key = (uint32_t)brc_blk_idx(x, y, z);
    for (int d = (int)st->ndelta - 1; d >= 0; d--) {
        uint32_t e = st->deltas[d];
        if ((e & 0x1FFFFu) == key) {
            if (out_meta) *out_meta = brc_delta_meta(e);
            return brc_delta_id(e);
        }
    }
    if (out_meta) *out_meta = seed->meta[key];
    return seed->blocks[key];
}
