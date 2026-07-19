/* pathfinding: TWO surfaces live here.
 *
 * 1) LEGACY pf_* (below): a custom A* loosely modeled on MC 1.7.10 PathFinder
 *    (ref/netherite-csrc/src/pathfind.cpp). NOT a faithful 1.11.2 port - it is a hand-rolled
 *    grid A* (linear open scan, its own step/drop rules). KEPT because mob_ai_*.h + entities_world.h
 *    depend on pf_find_astar / pf_is_walkable / pf_scene_* / PfWork / PfResult. Do not remove.
 *    INTERNAL verify only (CPU==CUDA), synthetic 16x16x32 grids.
 *
 * 2) VANILLA-FAITHFUL pf12_* (see the includes at the bottom): a verbatim MC 1.11.2 port of
 *    PathFinder + PathHeap + WalkNodeProcessor + PathPoint over an explicit synthetic block model.
 *    Lives in path_node_processor.h / path_finder.h / pathfinding12.h; driven + gated by
 *    cpu|cuda/pathfinding12 (java==cpu==cuda via oracle/goldens/pathfinding12/Golden.java).
 *    This is the correct surface for new 1.11.2 mob navigation work. */
#ifndef MC_PATHFINDING_H
#define MC_PATHFINDING_H

#include "mc.h"
#include "mc_world.h"
#include "mc_blocks.h"
#include "block_props_table.h"

#define PF_DIM_X 16
#define PF_DIM_Y 32
#define PF_DIM_Z 16
#define PF_VOL (PF_DIM_X * PF_DIM_Y * PF_DIM_Z)

#define PF_MAX_OPEN 512
#define PF_MAX_NODES 2048
#define PF_MAX_PATH 64

#define PF_NUM_SCENARIOS 6

typedef struct {
    i16 x, y, z;
    float g, h, f;
    i32 parent_idx;
} PfNode;

typedef struct {
    PfNode nodes[PF_MAX_NODES];
    i32  open_indices[PF_MAX_OPEN];
    u8   node_closed[PF_MAX_NODES];
    i32  node_count;
    i32  open_count;
} PfWork;

typedef struct {
    i16 waypoints[PF_MAX_PATH * 3];
    i32 len;
} PfResult;

MC_HD static inline int pf_idx(int x, int y, int z) {
    return (y * PF_DIM_Z + z) * PF_DIM_X + x;
}

MC_HD static inline int pf_in(int x, int y, int z) {
    return x >= 0 && x < PF_DIM_X && y >= 0 && y < PF_DIM_Y && z >= 0 && z < PF_DIM_Z;
}

MC_HD static inline u16 pf_get(const u16 *grid, int x, int y, int z) {
    return pf_in(x, y, z) ? grid[pf_idx(x, y, z)] : mc_state(BLK_AIR, 0);
}

MC_HD static inline void pf_set(u16 *grid, int x, int y, int z, u16 s) {
    if (pf_in(x, y, z)) grid[pf_idx(x, y, z)] = s;
}

MC_HD static inline int pf_abs_i(int v) { return v < 0 ? -v : v; }

MC_HD static inline int pf_is_solid(u16 s) {
    int id = mc_state_id(s);
    if (id <= 0) return 0;
    return (mc_bpt_props(id).flags & BF_SOLID) != 0;
}

MC_HD static inline int pf_is_walkable(const u16 *grid, int x, int y, int z, int entity_height) {
    u16 floor = pf_get(grid, x, y - 1, z);
    if (!pf_is_solid(floor)) return 0;
    for (int dy = 0; dy < entity_height; ++dy) {
        if (pf_is_solid(pf_get(grid, x, y + dy, z))) return 0;
    }
    return 1;
}

MC_HD static inline float pf_heuristic(int x1, int y1, int z1, int x2, int y2, int z2) {
    return (float)(pf_abs_i(x1 - x2) + pf_abs_i(y1 - y2) + pf_abs_i(z1 - z2));
}

MC_HD static inline void pf_work_init(PfWork *w) {
    w->node_count = 0;
    w->open_count = 0;
    for (int i = 0; i < PF_MAX_NODES; ++i) w->node_closed[i] = 0;
}

MC_HD static inline int pf_find_astar(const u16 *grid, int sx, int sy, int sz,
                                      int gx, int gy, int gz,
                                      int entity_height, int max_range,
                                      PfWork *work, PfResult *out) {
    out->len = 0;
    if (!pf_in(sx, sy, sz) || !pf_in(gx, gy, gz)) return 0;
    if (entity_height < 2) entity_height = 2;
    if (max_range < 16) max_range = 16;

    pf_work_init(work);

    PfNode *start = &work->nodes[work->node_count];
    start->x = (i16)sx;
    start->y = (i16)sy;
    start->z = (i16)sz;
    start->g = 0.0f;
    start->h = pf_heuristic(sx, sy, sz, gx, gy, gz);
    start->f = start->h;
    start->parent_idx = -1;
    work->open_indices[work->open_count++] = work->node_count;
    work->node_count++;

    i32 best_idx = 0;
    float best_h = start->h;

    static const int ddx[] = {1, -1, 0, 0};
    static const int ddz[] = {0, 0, 1, -1};

    int max_iterations = max_range * max_range;
    if (max_iterations > 2000) max_iterations = 2000;

    for (int iterations = 0; iterations < max_iterations &&
         work->open_count > 0 && work->node_count < PF_MAX_NODES - 8; ++iterations) {
        int best_open = 0;
        for (int i = 1; i < work->open_count; ++i) {
            if (work->nodes[work->open_indices[i]].f < work->nodes[work->open_indices[best_open]].f)
                best_open = i;
        }
        i32 cur_idx = work->open_indices[best_open];
        work->open_indices[best_open] = work->open_indices[--work->open_count];

        PfNode *cur = &work->nodes[cur_idx];
        if (work->node_closed[cur_idx]) continue;
        work->node_closed[cur_idx] = 1;

        if (cur->x == gx && cur->y == gy && cur->z == gz) {
            best_idx = cur_idx;
            best_h = 0.0f;
            break;
        }

        if (cur->h < best_h) {
            best_h = cur->h;
            best_idx = cur_idx;
        }

        if (pf_abs_i(cur->x - sx) > max_range || pf_abs_i(cur->z - sz) > max_range) continue;

        for (int d = 0; d < 4; ++d) {
            int nx = cur->x + ddx[d];
            int nz = cur->z + ddz[d];

            for (int step_y = 1; step_y >= -3; --step_y) {
                int ny = cur->y + step_y;
                if (ny < 1 || ny > PF_DIM_Y - 2) continue;

                if (pf_is_walkable(grid, nx, ny, nz, entity_height)) {
                    float tent_g = cur->g + 1.0f + (step_y > 0 ? 0.5f : 0.0f);

                    int skip = 0;
                    for (i32 i = 0; i < work->node_count; ++i) {
                        PfNode *n = &work->nodes[i];
                        if (n->x == nx && n->y == ny && n->z == nz) {
                            if (n->g <= tent_g) skip = 1;
                            break;
                        }
                    }
                    if (skip) break;

                    i32 ni = work->node_count++;
                    PfNode *nn = &work->nodes[ni];
                    nn->x = (i16)nx;
                    nn->y = (i16)ny;
                    nn->z = (i16)nz;
                    nn->g = tent_g;
                    nn->h = pf_heuristic(nx, ny, nz, gx, gy, gz);
                    nn->f = tent_g + nn->h;
                    nn->parent_idx = cur_idx;
                    work->node_closed[ni] = 0;

                    if (work->open_count < PF_MAX_OPEN)
                        work->open_indices[work->open_count++] = ni;
                    break;
                }
            }
        }
    }

    i16 path_buf[PF_MAX_PATH * 3];
    int path_len = 0;
    i32 idx = best_idx;
    while (idx >= 0 && path_len < PF_MAX_PATH) {
        path_buf[path_len * 3 + 0] = work->nodes[idx].x;
        path_buf[path_len * 3 + 1] = work->nodes[idx].y;
        path_buf[path_len * 3 + 2] = work->nodes[idx].z;
        path_len++;
        idx = work->nodes[idx].parent_idx;
    }

    if (path_len <= 1) {
        out->len = 0;
        return 0;
    }

    int stored = 0;
    for (int i = path_len - 2; i >= 0 && stored < PF_MAX_PATH; --i) {
        out->waypoints[stored * 3 + 0] = path_buf[i * 3 + 0];
        out->waypoints[stored * 3 + 1] = path_buf[i * 3 + 1];
        out->waypoints[stored * 3 + 2] = path_buf[i * 3 + 2];
        stored++;
    }
    out->len = stored;
    (void)best_h;
    return stored;
}

MC_HD static inline void pf_fill_air(u16 *grid) {
    u16 air = mc_state(BLK_AIR, 0);
    for (int i = 0; i < PF_VOL; ++i) grid[i] = air;
}

MC_HD static inline void pf_fill_floor(u16 *grid, int y) {
    u16 stone = mc_state(BLK_STONE, 0);
    for (int x = 0; x < PF_DIM_X; ++x)
        for (int z = 0; z < PF_DIM_Z; ++z)
            pf_set(grid, x, y, z, stone);
}

MC_HD static inline void pf_fill_box(u16 *grid, int x0, int y0, int z0,
                                     int x1, int y1, int z1, u16 s) {
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z)
                pf_set(grid, x, y, z, s);
}

/* Scenario 0: flat corridor, straight path (1,2,1) -> (14,2,8). */
MC_HD static inline void pf_scene_flat(u16 *grid) {
    pf_fill_air(grid);
    pf_fill_floor(grid, 1);
}

/* Scenario 1: wall at x=8 blocks direct route; must detour. */
MC_HD static inline void pf_scene_wall(u16 *grid) {
    u16 stone = mc_state(BLK_STONE, 0);
    pf_scene_flat(grid);
    pf_fill_box(grid, 8, 2, 2, 8, 3, 13, stone);
}

/* Scenario 2: 1-block step up mid-path (floor rises at x>=9). */
MC_HD static inline void pf_scene_step_up(u16 *grid) {
    pf_scene_flat(grid);
    pf_fill_floor(grid, 2);
    for (int x = 9; x < PF_DIM_X; ++x)
        for (int z = 0; z < PF_DIM_Z; ++z)
            pf_set(grid, x, 1, z, mc_state(BLK_AIR, 0));
}

/* Scenario 3: 1-block step down (floor drops at x>=9). */
MC_HD static inline void pf_scene_step_down(u16 *grid) {
    pf_scene_flat(grid);
    for (int x = 9; x < PF_DIM_X; ++x)
        for (int z = 0; z < PF_DIM_Z; ++z)
            pf_set(grid, x, 1, z, mc_state(BLK_AIR, 0));
}

/* Scenario 4: goal boxed in; partial path toward closest reachable point. */
MC_HD static inline void pf_scene_blocked(u16 *grid) {
    u16 stone = mc_state(BLK_STONE, 0);
    pf_scene_flat(grid);
    pf_fill_box(grid, 12, 2, 6, 14, 4, 8, stone);
    pf_set(grid, 13, 2, 7, mc_state(BLK_AIR, 0));
}

/* Scenario 5: seed-modulated pillar maze (deterministic from seed). */
MC_HD static inline void pf_scene_pillars(u16 *grid, i64 seed) {
    u16 stone = mc_state(BLK_STONE, 0);
    pf_scene_flat(grid);
    for (int x = 3; x < PF_DIM_X - 3; x += 3) {
        for (int z = 3; z < PF_DIM_Z - 3; z += 3) {
            u64 h = (u64)(seed ^ ((i64)x * 7157 + (i64)z * 8976890 + 981131));
            h ^= h >> 33;
            h *= 0xff51afd7ed558ccdULL;
            h ^= h >> 33;
            if ((h & 3u) == 0u)
                pf_fill_box(grid, x, 2, z, x, 3, z, stone);
        }
    }
}

typedef void (*PfEmitFn)(u32 v, void *ctx);

MC_HD static inline void pf_emit_result(const PfResult *r, PfEmitFn emit, void *ctx) {
    emit((u32)r->len, ctx);
    for (i32 i = 0; i < r->len; ++i) {
        emit((u32)(u16)r->waypoints[i * 3 + 0], ctx);
        emit((u32)(u16)r->waypoints[i * 3 + 1], ctx);
        emit((u32)(u16)r->waypoints[i * 3 + 2], ctx);
    }
}

MC_HD static inline void pf_run_scenario(int idx, i64 seed, u16 *grid, PfWork *work,
                                           PfResult *res) {
    static const struct { int sx, sy, sz, gx, gy, gz; } goals[PF_NUM_SCENARIOS] = {
        {1, 2, 1,  14, 2, 8},
        {1, 2, 1,  14, 2, 8},
        {1, 2, 1,  14, 3, 8},
        {1, 3, 8,  14, 2, 8},
        {1, 2, 1,  13, 2, 7},
        {1, 2, 1,  14, 2, 14},
    };

    pf_fill_air(grid);
    switch (idx) {
        case 0: pf_scene_flat(grid); break;
        case 1: pf_scene_wall(grid); break;
        case 2: pf_scene_step_up(grid); break;
        case 3: pf_scene_step_down(grid); break;
        case 4: pf_scene_blocked(grid); break;
        case 5: pf_scene_pillars(grid, seed); break;
        default: pf_scene_flat(grid); break;
    }

    const int *g = &goals[idx >= 0 && idx < PF_NUM_SCENARIOS ? idx : 0].sx;
    pf_find_astar(grid, g[0], g[1], g[2], g[3], g[4], g[5], 2, 16, work, res);
}

MC_HD static inline void pf_run_all(i64 seed, PfWork *work, PfEmitFn emit, void *ctx) {
    u16 grid[PF_VOL];
    PfResult res;
    for (int i = 0; i < PF_NUM_SCENARIOS; ++i) {
        pf_run_scenario(i, seed, grid, work, &res);
        pf_emit_result(&res, emit, ctx);
    }
}

/* Vanilla-faithful 1.11.2 surface (pf12_*). Included last so the legacy pf_* API above is
 * unaffected; all pf12/pnp/pfh symbols are separately namespaced (no clash with pf_*). */
#include "pathfinding12.h"

#endif /* MC_PATHFINDING_H */
