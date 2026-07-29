/* fluid_flow: double-buffered cellular-automaton port of MC 1.11.2 BlockDynamicLiquid /
 * BlockStaticLiquid flow (ref/netherite-csrc/src/block_tick.cpp, block/BlockDynamicLiquid.java).
 *
 * INTERNAL verify (CPU==CUDA). Propagation TIMING differs from vanilla: vanilla uses scheduled
 * block ticks (water every 5 game ticks, lava overworld every 30); here one CA iteration applies
 * updateTick-equivalent logic to every dynamic liquid cell synchronously until fixpoint or a fixed
 * iteration budget. Flow geometry, levels, and lava/water reactions follow the netherite port.
 *
 * SPEC rules 3+5: read buffer cur, write buffer next; no in-place neighbor-visible writes.
 * Deterministic scenes only (no hash RNG). Block ids inlined below (same as mc_blocks.h). */
#ifndef MC_FLUID_FLOW_H
#define MC_FLUID_FLOW_H

#include "mc.h"
#include "mc_world.h"
#include "mc_blocks.h"

/* Inline block ids (mirror mc_blocks.h; do not edit trunk). */
#ifndef FF_BLK_AIR
#define FF_BLK_AIR             BLK_AIR
#define FF_BLK_STONE           BLK_STONE
#define FF_BLK_COBBLESTONE     BLK_COBBLESTONE
#define FF_BLK_FLOWING_WATER   BLK_FLOWING_WATER
#define FF_BLK_WATER           BLK_WATER
#define FF_BLK_FLOWING_LAVA    BLK_FLOWING_LAVA
#define FF_BLK_LAVA            BLK_LAVA
#define FF_BLK_OBSIDIAN        BLK_OBSIDIAN
#endif

#define FF_DIM_WB_X 17
#define FF_DIM_WB_Y 4
#define FF_DIM_WB_Z 17

#define FF_DIM_SS_X 5
#define FF_DIM_SS_Y 3
#define FF_DIM_SS_Z 5

MC_HD static inline int ff_idx(int nx, int ny, int nz, int x, int y, int z) {
    return (y * nz + z) * nx + x;
}

MC_HD static inline int ff_in(int nx, int ny, int nz, int x, int y, int z) {
    return x >= 0 && x < nx && y >= 0 && y < ny && z >= 0 && z < nz;
}

MC_HD static inline u16 ff_get(const u16 *buf, int nx, int ny, int nz, int x, int y, int z) {
    return ff_in(nx, ny, nz, x, y, z) ? buf[ff_idx(nx, ny, nz, x, y, z)] : mc_state(FF_BLK_AIR, 0);
}

MC_HD static inline void ff_set(u16 *buf, int nx, int ny, int nz, int x, int y, int z, u16 s) {
    if (ff_in(nx, ny, nz, x, y, z)) buf[ff_idx(nx, ny, nz, x, y, z)] = s;
}

MC_HD static inline int ff_is_water_id(int id) {
    return id == FF_BLK_FLOWING_WATER || id == FF_BLK_WATER;
}

MC_HD static inline int ff_is_lava_id(int id) {
    return id == FF_BLK_FLOWING_LAVA || id == FF_BLK_LAVA;
}

MC_HD static inline int ff_is_water(u16 s) { return ff_is_water_id(mc_state_id(s)); }
MC_HD static inline int ff_is_lava(u16 s)  { return ff_is_lava_id(mc_state_id(s)); }

MC_HD static inline int ff_is_blocking(u16 s) {
    int id = mc_state_id(s);
    if (id == FF_BLK_AIR) return 0;
    if (ff_is_water_id(id) || ff_is_lava_id(id)) return 0;
    return 1;
}

MC_HD static inline int ff_can_displace(const u16 *cur, int nx, int ny, int nz,
                                        int x, int y, int z, int src_is_water) {
    u16 s = ff_get(cur, nx, ny, nz, x, y, z);
    int id = mc_state_id(s);
    if (src_is_water && ff_is_water_id(id)) return 0;
    if (!src_is_water && ff_is_lava_id(id)) return 0;
    if (ff_is_lava_id(id)) return 0;
    return !ff_is_blocking(s);
}

MC_HD static inline int ff_liquid_level(const u16 *cur, int nx, int ny, int nz,
                                        int x, int y, int z, int is_water_type) {
    u16 s = ff_get(cur, nx, ny, nz, x, y, z);
    int id = mc_state_id(s);
    int same = is_water_type ? ff_is_water_id(id) : ff_is_lava_id(id);
    if (!same) return -1;
    return mc_state_meta(s);
}

MC_HD static inline int ff_check_neighbor(const u16 *cur, int nx, int ny, int nz,
                                          int x, int y, int z, int current_min,
                                          int is_water_type, int *source_count) {
    int level = ff_liquid_level(cur, nx, ny, nz, x, y, z, is_water_type);
    if (level < 0) return current_min;
    if (level == 0) (*source_count)++;
    if (level >= 8) level = 0;
    if (current_min >= 0 && level >= current_min) return current_min;
    return level;
}

MC_HD static inline int ff_flow_distance(const u16 *cur, int nx, int ny, int nz,
                                         int x, int y, int z, int depth, int from_dir,
                                         int is_water_type) {
    int best = 1000;
    int dir;
    for (dir = 0; dir < 4; ++dir) {
        if ((dir == 0 && from_dir == 1) || (dir == 1 && from_dir == 0) ||
            (dir == 2 && from_dir == 3) || (dir == 3 && from_dir == 2))
            continue;
        int nxp = x, nzp = z;
        if (dir == 0) nxp--;
        if (dir == 1) nxp++;
        if (dir == 2) nzp--;
        if (dir == 3) nzp++;
        u16 nb = ff_get(cur, nx, ny, nz, nxp, y, nzp);
        if (ff_is_blocking(nb)) continue;
        if ((is_water_type ? ff_is_water(nb) : ff_is_lava(nb)) && mc_state_meta(nb) == 0)
            continue;
        if (!ff_is_blocking(ff_get(cur, nx, ny, nz, nxp, y - 1, nzp)))
            return depth;
        if (depth < 4) {
            int d = ff_flow_distance(cur, nx, ny, nz, nxp, y, nzp, depth + 1, dir, is_water_type);
            if (d < best) best = d;
        }
    }
    return best;
}

MC_HD static inline void ff_calc_flow_dirs(const u16 *cur, int nx, int ny, int nz,
                                           int x, int y, int z, int is_water_type,
                                           int flow_dirs[4]) {
    int distances[4];
    int dir;
    for (dir = 0; dir < 4; ++dir) {
        distances[dir] = 1000;
        int nxp = x, nzp = z;
        if (dir == 0) nxp--;
        if (dir == 1) nxp++;
        if (dir == 2) nzp--;
        if (dir == 3) nzp++;
        u16 nb = ff_get(cur, nx, ny, nz, nxp, y, nzp);
        if (ff_is_blocking(nb)) continue;
        if ((is_water_type ? ff_is_water(nb) : ff_is_lava(nb)) && mc_state_meta(nb) == 0)
            continue;
        if (!ff_is_blocking(ff_get(cur, nx, ny, nz, nxp, y - 1, nzp)))
            distances[dir] = 0;
        else
            distances[dir] = ff_flow_distance(cur, nx, ny, nz, nxp, y, nzp, 1, dir, is_water_type);
    }
    {
        int min_dist = distances[0];
        int i;
        for (i = 1; i < 4; ++i)
            if (distances[i] < min_dist) min_dist = distances[i];
        for (i = 0; i < 4; ++i)
            flow_dirs[i] = (distances[i] == min_dist);
    }
}

MC_HD static inline void ff_try_spread(u16 *next, int nx, int ny, int nz,
                                      int x, int y, int z, u16 state) {
    if (!ff_in(nx, ny, nz, x, y, z)) return;
    u16 existing = ff_get(next, nx, ny, nz, x, y, z);
    int ex_id = mc_state_id(existing);
    int st_id = mc_state_id(state);
    int st_meta = mc_state_meta(state);
    if (ex_id == FF_BLK_AIR) {
        ff_set(next, nx, ny, nz, x, y, z, state);
        return;
    }
    if (st_id == ex_id) {
        int ex_meta = mc_state_meta(existing);
        if (st_meta < ex_meta) ff_set(next, nx, ny, nz, x, y, z, state);
    }
}

MC_HD static inline void ff_spread_to(const u16 *cur, u16 *next, int nx, int ny, int nz,
                                      int x, int y, int z, int level, int dynamic_id,
                                      int is_water_type) {
    if (!ff_can_displace(cur, nx, ny, nz, x, y, z, is_water_type)) return;
    ff_try_spread(next, nx, ny, nz, x, y, z, mc_state(dynamic_id, level));
}

MC_HD static inline void ff_react_lava(const u16 *cur, u16 *next, int nx, int ny, int nz,
                                       int x, int y, int z) {
    u16 s = ff_get(cur, nx, ny, nz, x, y, z);
    if (!ff_is_lava(s)) return;
    int has_water = 0;
    if (ff_is_water(ff_get(cur, nx, ny, nz, x, y, z - 1))) has_water = 1;
    if (!has_water && ff_is_water(ff_get(cur, nx, ny, nz, x, y, z + 1))) has_water = 1;
    if (!has_water && ff_is_water(ff_get(cur, nx, ny, nz, x - 1, y, z))) has_water = 1;
    if (!has_water && ff_is_water(ff_get(cur, nx, ny, nz, x + 1, y, z))) has_water = 1;
    if (!has_water && ff_is_water(ff_get(cur, nx, ny, nz, x, y + 1, z))) has_water = 1;
    if (has_water) {
        int meta = mc_state_meta(s);
        if (meta == 0)
            ff_set(next, nx, ny, nz, x, y, z, mc_state(FF_BLK_OBSIDIAN, 0));
        else if (meta <= 4)
            ff_set(next, nx, ny, nz, x, y, z, mc_state(FF_BLK_COBBLESTONE, 0));
    }
}

MC_HD static inline void ff_static_to_dynamic(u16 *next, int nx, int ny, int nz, int x, int y, int z) {
    u16 s = ff_get(next, nx, ny, nz, x, y, z);
    int id = mc_state_id(s);
    int meta = mc_state_meta(s);
    if (id == FF_BLK_WATER)
        ff_set(next, nx, ny, nz, x, y, z, mc_state(FF_BLK_FLOWING_WATER, meta));
    else if (id == FF_BLK_LAVA)
        ff_set(next, nx, ny, nz, x, y, z, mc_state(FF_BLK_FLOWING_LAVA, meta));
}

/* lava_cost: BlockDynamicLiquid level decay for lava; 2 in the overworld/end,
 * 1 in the nether (world.provider.doesWaterVaporize()). Water is always 1. */
MC_HD static inline void ff_flow_cell_ex(const u16 *cur, u16 *next, int nx, int ny, int nz,
                                         int x, int y, int z, int lava_cost) {
    u16 self = ff_get(cur, nx, ny, nz, x, y, z);
    int block_id = mc_state_id(self);
    int is_water_type = ff_is_water_id(block_id);
    if (!is_water_type && !ff_is_lava_id(block_id)) return;
    if (block_id == FF_BLK_WATER || block_id == FF_BLK_LAVA) return;

    int dynamic_id = is_water_type ? FF_BLK_FLOWING_WATER : FF_BLK_FLOWING_LAVA;
    int l = mc_state_meta(self);
    int spread_cost = is_water_type ? 1 : lava_cost;
    int new_level;

    if (l > 0) {
        int source_count = 0;
        int min_neighbor = -100;
        min_neighbor = ff_check_neighbor(cur, nx, ny, nz, x - 1, y, z, min_neighbor, is_water_type, &source_count);
        min_neighbor = ff_check_neighbor(cur, nx, ny, nz, x + 1, y, z, min_neighbor, is_water_type, &source_count);
        min_neighbor = ff_check_neighbor(cur, nx, ny, nz, x, y, z - 1, min_neighbor, is_water_type, &source_count);
        min_neighbor = ff_check_neighbor(cur, nx, ny, nz, x, y, z + 1, min_neighbor, is_water_type, &source_count);

        new_level = min_neighbor + spread_cost;
        if (new_level >= 8 || min_neighbor < 0) new_level = -1;

        {
            int above = ff_liquid_level(cur, nx, ny, nz, x, y + 1, z, is_water_type);
            if (above >= 0) {
                if (above >= 8) new_level = above;
                else new_level = above + 8;
            }
        }

        if (source_count >= 2 && is_water_type) {
            u16 below = ff_get(cur, nx, ny, nz, x, y - 1, z);
            int bid = mc_state_id(below);
            if (ff_is_blocking(below) && bid != FF_BLK_AIR) {
                new_level = 0;
            } else if (ff_is_water_id(bid) && mc_state_meta(below) == 0) {
                new_level = 0;
            }
        }

        if (new_level == l) {
            ff_set(next, nx, ny, nz, x, y, z, mc_state(dynamic_id + 1, l));
        } else {
            l = new_level;
            if (new_level < 0)
                ff_set(next, nx, ny, nz, x, y, z, mc_state(FF_BLK_AIR, 0));
            else
                ff_set(next, nx, ny, nz, x, y, z, mc_state(dynamic_id, new_level));
        }
    } else {
        ff_set(next, nx, ny, nz, x, y, z, mc_state(dynamic_id + 1, 0));
    }

    if (l < 0) return;

    if (ff_can_displace(cur, nx, ny, nz, x, y - 1, z, is_water_type)) {
        if (!is_water_type && ff_is_water(ff_get(cur, nx, ny, nz, x, y - 1, z))) {
            ff_set(next, nx, ny, nz, x, y - 1, z, mc_state(FF_BLK_STONE, 0));
            return;
        }
        {
            int down_meta = (l >= 8) ? l : l + 8;
            ff_spread_to(cur, next, nx, ny, nz, x, y - 1, z, down_meta, dynamic_id, is_water_type);
        }
    } else if (l >= 0 && (l == 0 || ff_is_blocking(ff_get(cur, nx, ny, nz, x, y - 1, z)))) {
        int flow_dirs[4];
        ff_calc_flow_dirs(cur, nx, ny, nz, x, y, z, is_water_type, flow_dirs);
        new_level = l + spread_cost;
        if (l >= 8) new_level = 1;
        if (new_level >= 8) return;
        if (flow_dirs[0]) ff_spread_to(cur, next, nx, ny, nz, x - 1, y, z, new_level, dynamic_id, is_water_type);
        if (flow_dirs[1]) ff_spread_to(cur, next, nx, ny, nz, x + 1, y, z, new_level, dynamic_id, is_water_type);
        if (flow_dirs[2]) ff_spread_to(cur, next, nx, ny, nz, x, y, z - 1, new_level, dynamic_id, is_water_type);
        if (flow_dirs[3]) ff_spread_to(cur, next, nx, ny, nz, x, y, z + 1, new_level, dynamic_id, is_water_type);
    }
}

MC_HD static inline void ff_ca_step_ex(const u16 *cur, u16 *next, int nx, int ny, int nz,
                                       int lava_cost) {
    int x, y, z;
    int vol = nx * ny * nz;
    for (x = 0; x < vol; ++x) next[x] = cur[x];
    for (y = 0; y < ny; ++y)
        for (z = 0; z < nz; ++z)
            for (x = 0; x < nx; ++x)
                ff_react_lava(cur, next, nx, ny, nz, x, y, z);
    for (y = 0; y < ny; ++y)
        for (z = 0; z < nz; ++z)
            for (x = 0; x < nx; ++x)
                ff_static_to_dynamic(next, nx, ny, nz, x, y, z);
    for (y = 0; y < ny; ++y)
        for (z = 0; z < nz; ++z)
            for (x = 0; x < nx; ++x)
                ff_flow_cell_ex(cur, next, nx, ny, nz, x, y, z, lava_cost);
}

/* legacy entry points: overworld lava decay (2). Every pre-existing caller and
 * golden keeps byte-identical behavior. */
MC_HD static inline void ff_flow_cell(const u16 *cur, u16 *next, int nx, int ny, int nz,
                                      int x, int y, int z) {
    ff_flow_cell_ex(cur, next, nx, ny, nz, x, y, z, 2);
}

MC_HD static inline void ff_ca_step(const u16 *cur, u16 *next, int nx, int ny, int nz) {
    ff_ca_step_ex(cur, next, nx, ny, nz, 2);
}

MC_HD static inline int ff_bufs_equal(const u16 *a, const u16 *b, int n) {
    int i;
    for (i = 0; i < n; ++i)
        if (a[i] != b[i]) return 0;
    return 1;
}

MC_HD static inline void ff_ca_run(u16 *cur, u16 *tmp, int nx, int ny, int nz, int max_iters) {
    int vol = nx * ny * nz;
    int i;
    for (i = 0; i < max_iters; ++i) {
        ff_ca_step(cur, tmp, nx, ny, nz);
        if (ff_bufs_equal(cur, tmp, vol)) break;
        {
            int j;
            for (j = 0; j < vol; ++j) cur[j] = tmp[j];
        }
    }
}

MC_HD static inline void ff_init_water_bucket(u16 *buf) {
    int nx = FF_DIM_WB_X, ny = FF_DIM_WB_Y, nz = FF_DIM_WB_Z;
    int x, y, z;
    u16 stone = mc_state(FF_BLK_STONE, 0);
    u16 air   = mc_state(FF_BLK_AIR, 0);
    for (y = 0; y < ny; ++y)
        for (z = 0; z < nz; ++z)
            for (x = 0; x < nx; ++x)
                ff_set(buf, nx, ny, nz, x, y, z, air);
    for (z = 0; z < nz; ++z)
        for (x = 0; x < nx; ++x) {
            ff_set(buf, nx, ny, nz, x, 1, z, stone);
            ff_set(buf, nx, ny, nz, x, 2, z, air);
            ff_set(buf, nx, ny, nz, x, 3, z, air);
        }
    /* world (4,63,0) -> cube (12,1,8); world (4,64,0) -> (12,2,8) */
    ff_set(buf, nx, ny, nz, 12, 1, 8, mc_state(FF_BLK_LAVA, 0));
    ff_set(buf, nx, ny, nz, 12, 2, 8, mc_state(FF_BLK_WATER, 0));
}

MC_HD static inline void ff_init_spring_spread(u16 *buf) {
    int nx = FF_DIM_SS_X, ny = FF_DIM_SS_Y, nz = FF_DIM_SS_Z;
    int x, y, z;
    u16 stone = mc_state(FF_BLK_STONE, 0);
    u16 air   = mc_state(FF_BLK_AIR, 0);
    for (y = 0; y < ny; ++y)
        for (z = 0; z < nz; ++z)
            for (x = 0; x < nx; ++x)
                ff_set(buf, nx, ny, nz, x, y, z, air);
    for (z = 0; z < nz; ++z)
        for (x = 0; x < nx; ++x)
            ff_set(buf, nx, ny, nz, x, 0, z, stone);
    /* world (0,64,0) on 5x5 centered at (-2..2) -> cube (2,1,2) flowing source meta 0 */
    ff_set(buf, nx, ny, nz, 2, 1, 2, mc_state(FF_BLK_FLOWING_WATER, 0));
}

MC_HD static inline void ff_dump(const u16 *buf, int nx, int ny, int nz,
                                 void (*emit)(u16, void *), void *ctx) {
    int y, z, x;
    for (y = 0; y < ny; ++y)
        for (z = 0; z < nz; ++z)
            for (x = 0; x < nx; ++x)
                emit(ff_get(buf, nx, ny, nz, x, y, z), ctx);
}

/* ==================================================================================
 * HALO-AWARE (cross-chunk) fluid CA.  ADDED for tick_world_halo (Wave 15+).
 *
 * The existing ff_ca_step above runs on a chunk-local buffer with SEALED borders
 * (ff_get returns AIR out of bounds), so fluid cannot leave the chunk. These variants
 * operate over a WHOLE gdim x gdim grid of Chunks (as laid out by tick_world_multi:
 * grid position (gx,gz) -> chunks[gz*gdim+gx]) by extracting the entire region's
 * y-window into ONE contiguous region buffer, running the (position-independent) CA on
 * it, and merging back. Because the region buffer is contiguous, every INTERIOR chunk
 * border is just an adjacent cell -> fluid crosses chunk boundaries for free; only the
 * OUTER region edge stays sealed (unloaded world = air), which is correct. The fluid CA
 * draws NO runtime RNG, so it is purely a function of cell values and the world origin is
 * unused (documented below for API symmetry with the RNG-keyed subsystems).
 *
 * SPEC rule 3: caller passes a read grid; ff_region_extract snapshots into rcur before
 * any write, so there is no in-place neighbor-visible mutation mid-step. ==================================================================================
 *
 * These helpers reference mc_world.h's Chunk/mc_get/mc_set, so they are OPT-IN behind
 * MC_FLUID_HALO: consumers that operate on real Chunk grids (tick_world_halo.h,
 * world_step.h) #define MC_FLUID_HALO before including this header. The populate_fluid_shim
 * chain pre-defines MC_WORLD_H to SKIP mc_world.h (avoiding a `World` typedef clash), so
 * Chunk is absent there; it never uses these helpers, and without MC_FLUID_HALO they are
 * simply not compiled - so fluid_flow.h parses cleanly in that chain (fixes the HEAD
 * breakage of overworld_full_live / populate_fluid_*). */
#ifdef MC_FLUID_HALO

/* region cell (rx, y-in-window, rz) -> which Chunk in the grid + local coords. */
MC_HD static inline u16 ff_region_get(const Chunk *grid, int gdim, int oy, int rx, int y, int rz) {
    int gx, gz;
    if (rx < 0 || rz < 0) return mc_state(FF_BLK_AIR, 0);
    gx = rx >> 4; gz = rz >> 4;
    if (gx >= gdim || gz >= gdim) return mc_state(FF_BLK_AIR, 0);
    return mc_get(&grid[gz * gdim + gx], rx & 15, oy + y, rz & 15);
}

MC_HD static inline void ff_region_extract(const Chunk *grid, int gdim, int oy, int ny, u16 *buf) {
    int rnx = gdim * 16, rnz = gdim * 16;
    int rx, rz, y;
    for (y = 0; y < ny; ++y)
        for (rz = 0; rz < rnz; ++rz)
            for (rx = 0; rx < rnx; ++rx)
                buf[ff_idx(rnx, ny, rnz, rx, y, rz)] = ff_region_get(grid, gdim, oy, rx, y, rz);
}

MC_HD static inline void ff_region_merge(Chunk *grid, int gdim, int oy, int ny, const u16 *buf) {
    int rnx = gdim * 16, rnz = gdim * 16;
    int rx, rz, y;
    for (y = 0; y < ny; ++y)
        for (rz = 0; rz < rnz; ++rz)
            for (rx = 0; rx < rnx; ++rx) {
                int gx = rx >> 4, gz = rz >> 4;
                mc_set(&grid[gz * gdim + gx], rx & 15, oy + y, rz & 15,
                       buf[ff_idx(rnx, ny, rnz, rx, y, rz)]);
            }
}

/* One halo-aware fluid CA step: read now_grid's y-window (crossing chunk borders),
 * step, write next_grid. world_ox/oy/oz document the region origin (unused - RNG-free).
 * rcur/rtmp are caller-provided region scratch of size (gdim*16)*ny*(gdim*16). */
MC_HD static inline void ff_ca_step_halo(const Chunk *now_grid, Chunk *next_grid, int gdim,
                                         int oy, int ny, int world_ox, int world_oy, int world_oz,
                                         u16 *rcur, u16 *rtmp) {
    int rnx = gdim * 16, rnz = gdim * 16;
    (void)world_ox; (void)world_oy; (void)world_oz;
    ff_region_extract(now_grid, gdim, oy, ny, rcur);
    ff_ca_step(rcur, rtmp, rnx, ny, rnz);   /* contiguous region -> borders are interior cells */
    ff_region_merge(next_grid, gdim, oy, ny, rtmp);
}

#endif /* MC_FLUID_HALO */

#endif /* MC_FLUID_FLOW_H */
