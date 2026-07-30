/* light_propagation: double-buffered fixpoint CA for MC 1.11.2 skylight + block light
 * (World.getRawLight / Chunk.generateSkylightMap / World.checkLightFor semantics).
 *
 * INTERNAL verify (CPU==CUDA). Propagation TIMING differs from vanilla: vanilla schedules
 * checkLightFor on block changes; here every cell recomputes getRawLight from the read
 * buffer each Jacobi iteration until fixpoint (SPEC rules 3+5).
 *
 * Integration shims (documented for full-world wiring):
 *  - Synthetic 16x16x64 chunk; block opacity/emit inlined below (no block_props_table dep).
 *  - heightMap + canSeeSky ported verbatim from Chunk; OOB neighbors read light 0 (unloaded).
 *  - hasSkyLight=true (overworld); no neighbor-chunk gap lighting.
 *
 * Traps: ordered temporaries; no a[i]=i++; -ffp-contract=off/--fmad=false. */
#ifndef MC_LIGHT_PROPAGATION_H
#define MC_LIGHT_PROPAGATION_H

#include "mc.h"
#include "mc_world.h"
#include "mc_blocks.h"

#define LP_NX 16
#define LP_NY 64
#define LP_NZ 16
#define LP_VOL (LP_NX * LP_NY * LP_NZ)

#ifndef LP_BLK_GLASS
#define LP_BLK_GLASS BLK_GLASS
#endif
#ifndef LP_BLK_TORCH
#define LP_BLK_TORCH BLK_TORCH
#endif

MC_HD static inline int lp_idx(int x, int y, int z) {
    return (y * LP_NZ + z) * LP_NX + x;
}

MC_HD static inline int lp_in(int x, int y, int z) {
    return x >= 0 && x < LP_NX && y >= 0 && y < LP_NY && z >= 0 && z < LP_NZ;
}

MC_HD static inline u16 lp_get_block(const u16 *blocks, int x, int y, int z) {
    return lp_in(x, y, z) ? blocks[lp_idx(x, y, z)] : mc_state(BLK_AIR, 0);
}

MC_HD static inline void lp_set_block(u16 *blocks, int x, int y, int z, u16 s) {
    if (lp_in(x, y, z)) blocks[lp_idx(x, y, z)] = s;
}

/* Inline test-table props for scene blocks (vanilla Block.java registration). */
MC_HD static inline int lp_emit(int id) {
    if (id == LP_BLK_TORCH) return 14; /* setLightLevel(0.9375F) -> (int)(15*0.9375) */
    return 0;
}

MC_HD static inline int lp_opacity_raw(int id) {
    if (id == BLK_AIR) return 0;
    if (id == LP_BLK_GLASS) return 0; /* BlockBreakable, not fullBlock */
    if (id == LP_BLK_TORCH) return 0;
    if (id == BLK_STONE) return 255;
    return 255;
}

MC_HD static inline int lp_effective_opacity(int id, int emit) {
    int j = lp_opacity_raw(id);
    if (j >= 15 && emit > 0) j = 1;
    if (j < 1) j = 1;
    return j;
}

MC_HD static inline int lp_height_map_at(const u16 *blocks, int x, int z) {
    int y;
    for (y = LP_NY - 1; y >= 0; --y) {
        int id = mc_state_id(lp_get_block(blocks, x, y, z));
        if (lp_opacity_raw(id) != 0) return y + 1;
    }
    return 0;
}

MC_HD static inline void lp_build_height_map(const u16 *blocks, u8 *hm) {
    int x, z;
    for (z = 0; z < LP_NZ; ++z)
        for (x = 0; x < LP_NX; ++x)
            hm[z * LP_NX + x] = (u8)lp_height_map_at(blocks, x, z);
}

MC_HD static inline int lp_can_see_sky(const u8 *hm, int x, int y, int z) {
    return y >= (int)hm[z * LP_NX + x];
}

MC_HD static inline int lp_neighbor_sky(const u8 *sky, int x, int y, int z, int dx, int dy, int dz) {
    int nx = x + dx;
    int ny = y + dy;
    int nz = z + dz;
    if (!lp_in(nx, ny, nz)) return 0;
    return (int)sky[lp_idx(nx, ny, nz)];
}

MC_HD static inline int lp_neighbor_block(const u8 *blk, int x, int y, int z, int dx, int dy, int dz) {
    int nx = x + dx;
    int ny = y + dy;
    int nz = z + dz;
    if (!lp_in(nx, ny, nz)) return 0;
    return (int)blk[lp_idx(nx, ny, nz)];
}

/* World.getRawLight(..., SKY) with reads from sky[] and fixed blocks[]. */
MC_HD static inline int lp_raw_sky(const u8 *sky, const u16 *blocks, const u8 *hm,
                                   int x, int y, int z) {
    if (lp_can_see_sky(hm, x, y, z)) return 15;
    {
        int id = mc_state_id(lp_get_block(blocks, x, y, z));
        int j = lp_effective_opacity(id, 0);
        if (j >= 15) return 0;
        {
            int i = 0;
            int k;
            k = lp_neighbor_sky(sky, x, y, z, 1, 0, 0) - j;
            if (k > i) i = k;
            k = lp_neighbor_sky(sky, x, y, z, -1, 0, 0) - j;
            if (k > i) i = k;
            k = lp_neighbor_sky(sky, x, y, z, 0, 1, 0) - j;
            if (k > i) i = k;
            k = lp_neighbor_sky(sky, x, y, z, 0, -1, 0) - j;
            if (k > i) i = k;
            k = lp_neighbor_sky(sky, x, y, z, 0, 0, 1) - j;
            if (k > i) i = k;
            k = lp_neighbor_sky(sky, x, y, z, 0, 0, -1) - j;
            if (k > i) i = k;
            return i;
        }
    }
}

/* World.getRawLight(..., BLOCK). */
MC_HD static inline int lp_raw_block(const u8 *blk, const u16 *blocks, int x, int y, int z) {
    int id = mc_state_id(lp_get_block(blocks, x, y, z));
    int emit = lp_emit(id);
    int j = lp_effective_opacity(id, emit);
    if (j >= 15) return 0;
    if (emit >= 14) return emit;
    {
        int i = emit;
        int k;
        k = lp_neighbor_block(blk, x, y, z, 1, 0, 0) - j;
        if (k > i) i = k;
        k = lp_neighbor_block(blk, x, y, z, -1, 0, 0) - j;
        if (k > i) i = k;
        k = lp_neighbor_block(blk, x, y, z, 0, 1, 0) - j;
        if (k > i) i = k;
        k = lp_neighbor_block(blk, x, y, z, 0, -1, 0) - j;
        if (k > i) i = k;
        k = lp_neighbor_block(blk, x, y, z, 0, 0, 1) - j;
        if (k > i) i = k;
        k = lp_neighbor_block(blk, x, y, z, 0, 0, -1) - j;
        if (k > i) i = k;
        return i;
    }
}

MC_HD static inline int lp_packed_equal(const u8 *a_sky, const u8 *a_blk,
                                        const u8 *b_sky, const u8 *b_blk, int n) {
    int i;
    for (i = 0; i < n; ++i) {
        u8 pa = mc_light(a_sky[i], a_blk[i]);
        u8 pb = mc_light(b_sky[i], b_blk[i]);
        if (pa != pb) return 0;
    }
    return 1;
}

MC_HD static inline void lp_ca_step(const u8 *cur_sky, const u8 *cur_blk, u8 *next_sky, u8 *next_blk,
                                    const u16 *blocks, const u8 *hm) {
    int x, y, z;
    for (y = 0; y < LP_NY; ++y)
        for (z = 0; z < LP_NZ; ++z)
            for (x = 0; x < LP_NX; ++x) {
                int i = lp_idx(x, y, z);
                next_sky[i] = (u8)lp_raw_sky(cur_sky, blocks, hm, x, y, z);
                next_blk[i] = (u8)lp_raw_block(cur_blk, blocks, x, y, z);
            }
}

MC_HD static inline void lp_propagate(u8 *sky, u8 *blk, u8 *tmp_sky, u8 *tmp_blk,
                                      const u16 *blocks, int max_iters) {
    u8 hm[LP_NX * LP_NZ];
    int i;
    lp_build_height_map(blocks, hm);
    for (i = 0; i < max_iters; ++i) {
        lp_ca_step(sky, blk, tmp_sky, tmp_blk, blocks, hm);
        if (lp_packed_equal(sky, blk, tmp_sky, tmp_blk, LP_VOL)) break;
        {
            int j;
            for (j = 0; j < LP_VOL; ++j) {
                sky[j] = tmp_sky[j];
                blk[j] = tmp_blk[j];
            }
        }
    }
}

/* Deterministic test scene: stone y<40, air above, glass shaft, seed-varied cave + torch. */
MC_HD static inline void lp_init_scene(u16 *blocks, i64 seed) {
    u16 stone = mc_state(BLK_STONE, 0);
    u16 air   = mc_state(BLK_AIR, 0);
    u16 glass = mc_state(LP_BLK_GLASS, 0);
    u16 torch = mc_state(LP_BLK_TORCH, 0);
    int x, y, z;
    int cx = 5 + (int)(seed % 6);
    int cz = 5 + (int)((seed / 6) % 6);
    int variant = (int)(seed % 3);

    for (y = 0; y < LP_NY; ++y) {
        u16 fill = (y < 40) ? stone : air;
        for (z = 0; z < LP_NZ; ++z)
            for (x = 0; x < LP_NX; ++x)
                lp_set_block(blocks, x, y, z, fill);
    }

    for (y = 40; y <= 55; ++y)
        lp_set_block(blocks, 8, y, 8, glass);

    if (variant != 1) {
        int dx, dz, dy;
        for (dy = 0; dy <= 3; ++dy)
            for (dz = -2; dz <= 2; ++dz)
                for (dx = -2; dx <= 2; ++dx)
                    lp_set_block(blocks, cx + dx, 41 + dy, cz + dz, air);
    }

    if (variant == 2) {
        for (y = 41; y <= 45; ++y)
            lp_set_block(blocks, cx, y, cz - 3, glass);
    }

    lp_set_block(blocks, cx, 41, cz, torch);
}

MC_HD static inline void lp_dump_light(const u8 *sky, const u8 *blk,
                                       void (*emit)(u8 packed, void *ctx), void *ctx) {
    int y, z, x;
    for (y = 0; y < LP_NY; ++y)
        for (z = 0; z < LP_NZ; ++z)
            for (x = 0; x < LP_NX; ++x) {
                int i = lp_idx(x, y, z);
                emit(mc_light(sky[i], blk[i]), ctx);
            }
}

/* ==================================================================================
 * HALO-AWARE (cross-chunk) light propagation.  ADDED for tick_world_halo (Wave 15+).
 *
 * lp_propagate above is hard-wired to one 16x64x16 chunk with SEALED borders (OOB
 * neighbors read light 0), so a torch never lights the neighbor chunk. These variants
 * are DIM-parameterized copies of the same World.getRawLight fixpoint, run over a
 * contiguous (gdim*16) x LP_NY x (gdim*16) REGION buffer extracted from a grid of Chunks
 * (grid position (gx,gz) -> chunks[gz*gdim+gx], matching tick_world_multi). Interior chunk
 * borders are adjacent cells -> light crosses them; only the OUTER region edge is sealed
 * (unloaded world = light 0). Light is a pure GATHER CA (no RNG, no scatter), so a
 * region-wide Jacobi-to-fixpoint is exactly the multi-chunk semantics. SPEC rules 3+5.
 * ================================================================================== */

MC_HD static inline int lp_ridx(int rnx, int rnz, int x, int y, int z) {
    return (y * rnz + z) * rnx + x;
}
MC_HD static inline int lp_rin(int rnx, int rny, int rnz, int x, int y, int z) {
    return x >= 0 && x < rnx && y >= 0 && y < rny && z >= 0 && z < rnz;
}
MC_HD static inline u16 lp_rget_block(const u16 *b, int rnx, int rny, int rnz, int x, int y, int z) {
    return lp_rin(rnx, rny, rnz, x, y, z) ? b[lp_ridx(rnx, rnz, x, y, z)] : mc_state(BLK_AIR, 0);
}
MC_HD static inline int lp_rheight_at(const u16 *b, int rnx, int rny, int rnz, int x, int z) {
    int y;
    for (y = rny - 1; y >= 0; --y) {
        int id = mc_state_id(lp_rget_block(b, rnx, rny, rnz, x, y, z));
        if (lp_opacity_raw(id) != 0) return y + 1;
    }
    return 0;
}
MC_HD static inline void lp_rbuild_hm(const u16 *b, int rnx, int rny, int rnz, u8 *hm) {
    int x, z;
    for (z = 0; z < rnz; ++z)
        for (x = 0; x < rnx; ++x) {
            int h = lp_rheight_at(b, rnx, rny, rnz, x, z);
            hm[z * rnx + x] = (u8)(h > 255 ? 255 : h);
        }
}
MC_HD static inline int lp_rcan_see_sky(const u8 *hm, int rnx, int x, int y, int z) {
    return y >= (int)hm[z * rnx + x];
}
MC_HD static inline int lp_rneighbor_sky(const u8 *sky, int rnx, int rny, int rnz,
                                         int x, int y, int z, int dx, int dy, int dz) {
    int nx = x + dx, ny = y + dy, nz = z + dz;
    if (!lp_rin(rnx, rny, rnz, nx, ny, nz)) return 0;
    return (int)sky[lp_ridx(rnx, rnz, nx, ny, nz)];
}
MC_HD static inline int lp_rneighbor_block(const u8 *blk, int rnx, int rny, int rnz,
                                           int x, int y, int z, int dx, int dy, int dz) {
    int nx = x + dx, ny = y + dy, nz = z + dz;
    if (!lp_rin(rnx, rny, rnz, nx, ny, nz)) return 0;
    return (int)blk[lp_ridx(rnx, rnz, nx, ny, nz)];
}
MC_HD static inline int lp_rraw_sky(const u8 *sky, const u16 *blocks, const u8 *hm,
                                    int rnx, int rny, int rnz, int x, int y, int z) {
    if (lp_rcan_see_sky(hm, rnx, x, y, z)) return 15;
    {
        int id = mc_state_id(lp_rget_block(blocks, rnx, rny, rnz, x, y, z));
        int j = lp_effective_opacity(id, 0);
        if (j >= 15) return 0;
        {
            int i = 0, k;
            k = lp_rneighbor_sky(sky, rnx, rny, rnz, x, y, z, 1, 0, 0) - j;  if (k > i) i = k;
            k = lp_rneighbor_sky(sky, rnx, rny, rnz, x, y, z, -1, 0, 0) - j; if (k > i) i = k;
            k = lp_rneighbor_sky(sky, rnx, rny, rnz, x, y, z, 0, 1, 0) - j;  if (k > i) i = k;
            k = lp_rneighbor_sky(sky, rnx, rny, rnz, x, y, z, 0, -1, 0) - j; if (k > i) i = k;
            k = lp_rneighbor_sky(sky, rnx, rny, rnz, x, y, z, 0, 0, 1) - j;  if (k > i) i = k;
            k = lp_rneighbor_sky(sky, rnx, rny, rnz, x, y, z, 0, 0, -1) - j; if (k > i) i = k;
            return i;
        }
    }
}
MC_HD static inline int lp_rraw_block(const u8 *blk, const u16 *blocks,
                                      int rnx, int rny, int rnz, int x, int y, int z) {
    int id = mc_state_id(lp_rget_block(blocks, rnx, rny, rnz, x, y, z));
    int emit = lp_emit(id);
    int j = lp_effective_opacity(id, emit);
    if (j >= 15) return 0;
    if (emit >= 14) return emit;
    {
        int i = emit, k;
        k = lp_rneighbor_block(blk, rnx, rny, rnz, x, y, z, 1, 0, 0) - j;  if (k > i) i = k;
        k = lp_rneighbor_block(blk, rnx, rny, rnz, x, y, z, -1, 0, 0) - j; if (k > i) i = k;
        k = lp_rneighbor_block(blk, rnx, rny, rnz, x, y, z, 0, 1, 0) - j;  if (k > i) i = k;
        k = lp_rneighbor_block(blk, rnx, rny, rnz, x, y, z, 0, -1, 0) - j; if (k > i) i = k;
        k = lp_rneighbor_block(blk, rnx, rny, rnz, x, y, z, 0, 0, 1) - j;  if (k > i) i = k;
        k = lp_rneighbor_block(blk, rnx, rny, rnz, x, y, z, 0, 0, -1) - j; if (k > i) i = k;
        return i;
    }
}
MC_HD static inline void lp_rca_step(const u8 *cur_sky, const u8 *cur_blk, u8 *next_sky, u8 *next_blk,
                                     const u16 *blocks, const u8 *hm, int rnx, int rny, int rnz) {
    int x, y, z;
    for (y = 0; y < rny; ++y)
        for (z = 0; z < rnz; ++z)
            for (x = 0; x < rnx; ++x) {
                int i = lp_ridx(rnx, rnz, x, y, z);
                next_sky[i] = (u8)lp_rraw_sky(cur_sky, blocks, hm, rnx, rny, rnz, x, y, z);
                next_blk[i] = (u8)lp_rraw_block(cur_blk, blocks, rnx, rny, rnz, x, y, z);
            }
}

/* Grid <-> region-buffer transfer (region y range is [0, LP_NY)). */
MC_HD static inline void lp_region_extract(const Chunk *grid, int gdim, u8 *sky, u8 *blk, u16 *blocks) {
    int rnx = gdim * 16, rnz = gdim * 16;
    int rx, rz, y;
    for (y = 0; y < LP_NY; ++y)
        for (rz = 0; rz < rnz; ++rz)
            for (rx = 0; rx < rnx; ++rx) {
                const Chunk *c = &grid[(rz >> 4) * gdim + (rx >> 4)];
                int wi = mc_idx(rx & 15, y, rz & 15);
                int li = lp_ridx(rnx, rnz, rx, y, rz);
                u8 packed = c->light[wi];
                sky[li] = (u8)mc_light_sky(packed);
                blk[li] = (u8)mc_light_block(packed);
                blocks[li] = mc_get(c, rx & 15, y, rz & 15);
            }
}
MC_HD static inline void lp_region_merge(Chunk *grid, int gdim, const u8 *sky, const u8 *blk) {
    int rnx = gdim * 16, rnz = gdim * 16;
    int rx, rz, y;
    for (y = 0; y < LP_NY; ++y)
        for (rz = 0; rz < rnz; ++rz)
            for (rx = 0; rx < rnx; ++rx) {
                Chunk *c = &grid[(rz >> 4) * gdim + (rx >> 4)];
                int wi = mc_idx(rx & 15, y, rz & 15);
                int li = lp_ridx(rnx, rnz, rx, y, rz);
                c->light[wi] = mc_light(sky[li], blk[li]);
            }
}

/* Halo-aware region light fixpoint: read grid's y=[0,LP_NY) light+blocks (crossing chunk
 * borders), Jacobi-iterate to fixpoint, write back. Caller supplies region scratch:
 * sky/blk/tsky/tblk/blocks of size (gdim*16)*LP_NY*(gdim*16), hm of size (gdim*16)^2. */
MC_HD static inline void lp_propagate_halo(Chunk *grid, int gdim,
                                           u8 *sky, u8 *blk, u8 *tsky, u8 *tblk, u16 *blocks,
                                           u8 *hm, int max_iters) {
    int rnx = gdim * 16, rnz = gdim * 16, rvol = rnx * LP_NY * rnz;
    int it;
    lp_region_extract(grid, gdim, sky, blk, blocks);
    lp_rbuild_hm(blocks, rnx, LP_NY, rnz, hm);
    for (it = 0; it < max_iters; ++it) {
        lp_rca_step(sky, blk, tsky, tblk, blocks, hm, rnx, LP_NY, rnz);
        if (lp_packed_equal(sky, blk, tsky, tblk, rvol)) break;
        {
            int j;
            for (j = 0; j < rvol; ++j) { sky[j] = tsky[j]; blk[j] = tblk[j]; }
        }
    }
    lp_region_merge(grid, gdim, sky, blk);
}

#endif /* MC_LIGHT_PROPAGATION_H */
