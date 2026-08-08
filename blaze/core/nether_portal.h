/* nether_portal: BlockPortal frame detection + Teleporter coordinate transform.
 *
 * PORT TARGETS (MC 1.11.2):
 *   net/minecraft/block/BlockPortal.java (Size, trySpawnPortal, createPatternHelper)
 *   net/minecraft/world/Teleporter.java (placeInExistingPortal)
 *   net/minecraft/entity/Entity.java (setPortal lastPortalVec)
 *
 * INTERNAL verify (CPU==CUDA). Synthetic fixed obsidian frames in a 32^3 grid; no worldgen RNG.
 * chunk_provider_nether.h is READ-ONLY (not included). CUT: pigman spawn, makePortal search,
 * destinationCoordinateCache, end-dimension exit platform. */
#ifndef MC_NETHER_PORTAL_H
#define MC_NETHER_PORTAL_H

#include "mc.h"
#include "mc_math.h"
#include "mc_world.h"
#include "mc_blocks.h"

#define NP_BLK_AIR      BLK_AIR
#define NP_BLK_OBSIDIAN BLK_OBSIDIAN
#define NP_BLK_PORTAL   90
#define NP_BLK_FIRE     51

#define NP_DIM 32
#define NP_VOL (NP_DIM * NP_DIM * NP_DIM)

#define NP_F_DOWN  0
#define NP_F_UP    1
#define NP_F_NORTH 2
#define NP_F_SOUTH 3
#define NP_F_WEST  4
#define NP_F_EAST  5

#define NP_AXIS_X 0
#define NP_AXIS_Z 1

#define NP_NUM_SCENARIOS 8

typedef struct {
    u16 blocks[NP_VOL];
} NpWorld;

typedef struct {
    int valid;
    int axis;
    int width;
    int height;
    int portal_block_count;
    int bottom_x, bottom_y, bottom_z;
    int left_dir;
    int right_dir;
} NpPortalSize;

typedef struct {
    int forwards;
    int front_top_x, front_top_y, front_top_z;
    int width;
    int height;
    int axis;
} NpPattern;

typedef struct {
    double pos_x, pos_y, pos_z;
    float  rotation_yaw;
    double motion_x, motion_y, motion_z;
    double last_portal_u, last_portal_v;
    int    teleport_dir;
} NpEntity;

MC_HD static inline int np_idx(int x, int y, int z) {
    return (y * NP_DIM + z) * NP_DIM + x;
}

MC_HD static inline int np_in(int x, int y, int z) {
    return x >= 0 && x < NP_DIM && y >= 0 && y < NP_DIM && z >= 0 && z < NP_DIM;
}

MC_HD static inline u16 np_get(const NpWorld *w, int x, int y, int z) {
    return np_in(x, y, z) ? w->blocks[np_idx(x, y, z)] : mc_state(NP_BLK_AIR, 0);
}

MC_HD static inline void np_set(NpWorld *w, int x, int y, int z, u16 s) {
    if (np_in(x, y, z)) w->blocks[np_idx(x, y, z)] = s;
}

MC_HD static inline int np_block_id(const NpWorld *w, int x, int y, int z) {
    return mc_state_id(np_get(w, x, y, z));
}

MC_HD static inline void np_offset(int dir, int *x, int *y, int *z) {
    switch (dir) {
        case NP_F_DOWN:  (*y)--; break;
        case NP_F_UP:    (*y)++; break;
        case NP_F_NORTH: (*z)--; break;
        case NP_F_SOUTH: (*z)++; break;
        case NP_F_WEST:  (*x)--; break;
        case NP_F_EAST:  (*x)++; break;
        default: break;
    }
}

MC_HD static inline int np_rotate_y(int dir) {
    switch (dir) {
        case NP_F_NORTH: return NP_F_EAST;
        case NP_F_EAST:  return NP_F_SOUTH;
        case NP_F_SOUTH: return NP_F_WEST;
        case NP_F_WEST:  return NP_F_NORTH;
        default: return dir;
    }
}

MC_HD static inline int np_rotate_y_ccw(int dir) {
    switch (dir) {
        case NP_F_NORTH: return NP_F_WEST;
        case NP_F_WEST:  return NP_F_SOUTH;
        case NP_F_SOUTH: return NP_F_EAST;
        case NP_F_EAST:  return NP_F_NORTH;
        default: return dir;
    }
}

MC_HD static inline int np_opposite(int dir) {
    switch (dir) {
        case NP_F_DOWN:  return NP_F_UP;
        case NP_F_UP:    return NP_F_DOWN;
        case NP_F_NORTH: return NP_F_SOUTH;
        case NP_F_SOUTH: return NP_F_NORTH;
        case NP_F_WEST:  return NP_F_EAST;
        case NP_F_EAST:  return NP_F_WEST;
        default: return dir;
    }
}

MC_HD static inline int np_axis(int dir) {
    if (dir == NP_F_EAST || dir == NP_F_WEST) return NP_AXIS_X;
    if (dir == NP_F_NORTH || dir == NP_F_SOUTH) return NP_AXIS_Z;
    return -1;
}

MC_HD static inline int np_axis_dir(int dir) {
    return (dir == NP_F_SOUTH || dir == NP_F_EAST) ? 1 : -1;
}

MC_HD static inline int np_horizontal_index(int dir) {
    switch (dir) {
        case NP_F_SOUTH: return 0;
        case NP_F_WEST:  return 1;
        case NP_F_NORTH: return 2;
        case NP_F_EAST:  return 3;
        default: return 0;
    }
}

MC_HD static inline int np_meta_for_axis(int axis) {
    return axis == NP_AXIS_X ? 1 : 2;
}

MC_HD static inline int np_is_empty_block(int id) {
    return id == NP_BLK_AIR || id == NP_BLK_FIRE || id == NP_BLK_PORTAL;
}

MC_HD static inline int np_distance_until_edge(const NpWorld *w, int x, int y, int z, int dir) {
    int i;
    for (i = 0; i < 22; ++i) {
        int cx = x, cy = y, cz = z;
        int j;
        for (j = 0; j < i; ++j) np_offset(dir, &cx, &cy, &cz);
        if (!np_is_empty_block(np_block_id(w, cx, cy, cz))) break;
        if (np_block_id(w, cx, cy - 1, cz) != NP_BLK_OBSIDIAN) break;
    }
    {
        int cx = x, cy = y, cz = z;
        int j;
        for (j = 0; j < i; ++j) np_offset(dir, &cx, &cy, &cz);
        return np_block_id(w, cx, cy, cz) == NP_BLK_OBSIDIAN ? i : 0;
    }
}

MC_HD static inline int np_calculate_portal_height(NpPortalSize *sz, const NpWorld *w) {
    int i, j;
    sz->height = 0;
    sz->portal_block_count = 0;
    for (sz->height = 0; sz->height < 21; ++sz->height) {
        for (i = 0; i < sz->width; ++i) {
            int cx = sz->bottom_x, cy = sz->bottom_y, cz = sz->bottom_z;
            int k;
            for (k = 0; k < i; ++k) np_offset(sz->right_dir, &cx, &cy, &cz);
            cy += sz->height;
            if (!np_is_empty_block(np_block_id(w, cx, cy, cz))) goto done_height;
            if (np_block_id(w, cx, cy, cz) == NP_BLK_PORTAL) sz->portal_block_count++;
            if (i == 0) {
                int lx = cx, ly = cy, lz = cz;
                np_offset(sz->left_dir, &lx, &ly, &lz);
                if (np_block_id(w, lx, ly, lz) != NP_BLK_OBSIDIAN) goto done_height;
            } else if (i == sz->width - 1) {
                int rx = cx, ry = cy, rz = cz;
                np_offset(sz->right_dir, &rx, &ry, &rz);
                if (np_block_id(w, rx, ry, rz) != NP_BLK_OBSIDIAN) goto done_height;
            }
        }
    }
done_height:
    for (j = 0; j < sz->width; ++j) {
        int cx = sz->bottom_x, cy = sz->bottom_y + sz->height, cz = sz->bottom_z;
        int k;
        for (k = 0; k < j; ++k) np_offset(sz->right_dir, &cx, &cy, &cz);
        if (np_block_id(w, cx, cy, cz) != NP_BLK_OBSIDIAN) {
            sz->height = 0;
            break;
        }
    }
    if (sz->height <= 21 && sz->height >= 3) return sz->height;
    sz->bottom_x = sz->bottom_y = sz->bottom_z = 0;
    sz->width = 0;
    sz->height = 0;
    return 0;
}

MC_HD static inline void np_size_init(NpPortalSize *sz, const NpWorld *w,
                                      int px, int py, int pz, int axis) {
    int x = px, y = py, z = pz;
    int i, edge;
    sz->valid = 0;
    sz->axis = axis;
    sz->width = 0;
    sz->height = 0;
    sz->portal_block_count = 0;
    sz->bottom_x = sz->bottom_y = sz->bottom_z = 0;

    if (axis == NP_AXIS_X) {
        sz->left_dir = NP_F_EAST;
        sz->right_dir = NP_F_WEST;
    } else {
        sz->left_dir = NP_F_NORTH;
        sz->right_dir = NP_F_SOUTH;
    }

    while (y > 0 && np_is_empty_block(np_block_id(w, x, y - 1, z))) {
        y--;
    }

    edge = np_distance_until_edge(w, x, y, z, sz->left_dir) - 1;
    if (edge < 0) return;

    sz->bottom_x = x; sz->bottom_y = y; sz->bottom_z = z;
    for (i = 0; i < edge; ++i) np_offset(sz->left_dir, &sz->bottom_x, &sz->bottom_y, &sz->bottom_z);

    sz->width = np_distance_until_edge(w, sz->bottom_x, sz->bottom_y, sz->bottom_z, sz->right_dir);
    if (sz->width < 2 || sz->width > 21) {
        sz->bottom_x = sz->bottom_y = sz->bottom_z = 0;
        sz->width = 0;
        return;
    }

    if (np_calculate_portal_height(sz, w) >= 3)
        sz->valid = 1;
}

MC_HD static inline int np_size_is_valid(const NpPortalSize *sz) {
    return sz->valid && sz->width >= 2 && sz->width <= 21 &&
           sz->height >= 3 && sz->height <= 21;
}

MC_HD static inline int np_place_portal_blocks(NpWorld *w, const NpPortalSize *sz) {
    int placed = 0;
    int i, j;
    u16 portal = mc_state(NP_BLK_PORTAL, np_meta_for_axis(sz->axis));
    for (i = 0; i < sz->width; ++i) {
        int bx = sz->bottom_x, by = sz->bottom_y, bz = sz->bottom_z;
        int k;
        for (k = 0; k < i; ++k) np_offset(sz->right_dir, &bx, &by, &bz);
        for (j = 0; j < sz->height; ++j) {
            np_set(w, bx, by + j, bz, portal);
            placed++;
        }
    }
    return placed;
}

MC_HD static inline int np_try_spawn_portal(NpWorld *w, int px, int py, int pz) {
    NpPortalSize sz;
    np_size_init(&sz, w, px, py, pz, NP_AXIS_X);
    if (np_size_is_valid(&sz) && sz.portal_block_count == 0) {
        np_place_portal_blocks(w, &sz);
        return 1;
    }
    np_size_init(&sz, w, px, py, pz, NP_AXIS_Z);
    if (np_size_is_valid(&sz) && sz.portal_block_count == 0) {
        np_place_portal_blocks(w, &sz);
        return 1;
    }
    return 0;
}

MC_HD static inline int np_count_portal_blocks(const NpWorld *w) {
    int n = 0, i;
    for (i = 0; i < NP_VOL; ++i)
        if (mc_state_id(w->blocks[i]) == NP_BLK_PORTAL) n++;
    return n;
}

/* BlockPortal.createPatternHelper (valid portal only; symmetric scene => POSITIVE side). */
MC_HD static inline int np_create_pattern(const NpWorld *w, int px, int py, int pz, NpPattern *out) {
    NpPortalSize sz;
    int forwards, top_x, top_y, top_z;
    np_size_init(&sz, w, px, py, pz, NP_AXIS_X);
    if (!np_size_is_valid(&sz))
        np_size_init(&sz, w, px, py, pz, NP_AXIS_Z);
    if (!np_size_is_valid(&sz)) return 0;

    forwards = np_rotate_y_ccw(sz.right_dir);
    top_x = sz.bottom_x; top_y = sz.bottom_y + sz.height - 1; top_z = sz.bottom_z;

    out->forwards = forwards;
    out->front_top_x = top_x;
    out->front_top_y = top_y;
    out->front_top_z = top_z;
    out->width = sz.width;
    out->height = sz.height;
    out->axis = sz.axis;
    return 1;
}

MC_HD static inline double np_pct(double num, double start, double end) {
    return (num - start) / (end - start);
}

/* Entity.setPortal lastPortalVec (server branch). */
MC_HD static inline void np_set_portal_vec(NpEntity *ent, const NpPattern *pat) {
    double d0, d1, d2;
    int neg = np_axis_dir(np_rotate_y(pat->forwards)) < 0;
    if (pat->axis == NP_AXIS_X)
        d0 = (double)pat->front_top_z;
    else
        d0 = (double)pat->front_top_x;
    if (pat->axis == NP_AXIS_X)
        d1 = ent->pos_z;
    else
        d1 = ent->pos_x;
    d1 = np_pct(d1 - (double)(neg ? 1 : 0), d0, d0 - (double)pat->width);
    if (d1 < 0.0) d1 = -d1;
    if (d1 > 1.0) d1 = 1.0;
    d2 = np_pct(ent->pos_y - 1.0, (double)pat->front_top_y,
                (double)(pat->front_top_y - pat->height));
    ent->last_portal_u = d1;
    ent->last_portal_v = d2;
    ent->teleport_dir = pat->forwards;
}

/* Teleporter.placeInExistingPortal coordinate transform. Returns 1 on success. */
MC_HD static inline int np_place_in_existing_portal(NpEntity *ent, const NpPattern *pat,
                                                    float rotation_yaw) {
    double d5, d7, d2, d6, d3, d4;
    int flag1, axis;
    float f = 0.0f, f1 = 0.0f, f2 = 0.0f, f3 = 0.0f;
    int opp = np_opposite(ent->teleport_dir);

    d5 = 0.0; /* blockpos x set below */
    d7 = 0.0;
    flag1 = np_axis_dir(np_rotate_y(pat->forwards)) < 0;
    axis = np_axis(pat->forwards);
    d2 = (axis == NP_AXIS_X) ? (double)pat->front_top_z : (double)pat->front_top_x;
    d6 = (double)(pat->front_top_y + 1) - ent->last_portal_v * (double)pat->height;
    if (flag1) d2 += 1.0;
    if (axis == NP_AXIS_X) {
        d5 = (double)pat->front_top_x + 0.5;
        d7 = d2 + (1.0 - ent->last_portal_u) * (double)pat->width *
            (double)np_axis_dir(np_rotate_y(pat->forwards));
    } else {
        d7 = (double)pat->front_top_z + 0.5;
        d5 = d2 + (1.0 - ent->last_portal_u) * (double)pat->width *
            (double)np_axis_dir(np_rotate_y(pat->forwards));
    }

    if (np_opposite(pat->forwards) == ent->teleport_dir) { f = 1.0f; f1 = 1.0f; }
    else if (pat->forwards == ent->teleport_dir) { f = -1.0f; f1 = -1.0f; }
    else if (np_opposite(pat->forwards) == np_rotate_y(ent->teleport_dir)) { f2 = 1.0f; f3 = -1.0f; }
    else { f2 = -1.0f; f3 = 1.0f; }

    d3 = ent->motion_x;
    d4 = ent->motion_z;
    ent->motion_x = d3 * (double)f + d4 * (double)f3;
    ent->motion_z = d3 * (double)f2 + d4 * (double)f1;
    ent->rotation_yaw = rotation_yaw -
        (float)(np_horizontal_index(opp) * 90) +
        (float)(np_horizontal_index(pat->forwards) * 90);
    ent->pos_x = d5;
    ent->pos_y = d6;
    ent->pos_z = d7;
    return 1;
}

MC_HD static inline void np_clear(NpWorld *w) {
    u16 air = mc_state(NP_BLK_AIR, 0);
    int i;
    for (i = 0; i < NP_VOL; ++i) w->blocks[i] = air;
}

MC_HD static inline void np_fill_obsidian(NpWorld *w, int x0, int y0, int z0,
                                          int x1, int y1, int z1) {
    u16 obs = mc_state(NP_BLK_OBSIDIAN, 0);
    int x, y, z;
    for (x = x0; x <= x1; ++x)
        for (y = y0; y <= y1; ++y)
            for (z = z0; z <= z1; ++z)
                np_set(w, x, y, z, obs);
}

/* Standard interior_w x interior_h frame. axis X: wall at fixed z, width along x. axis Z: wall at fixed x. */
MC_HD static inline void np_build_frame(NpWorld *w, int ox, int oy, int oz,
                                        int interior_w, int interior_h, int axis) {
    u16 obs = mc_state(NP_BLK_OBSIDIAN, 0);
    u16 air = mc_state(NP_BLK_AIR, 0);
    int i, j;
    np_clear(w);
    if (axis == NP_AXIS_X) {
        for (i = 0; i < interior_w; ++i)
            for (j = 0; j < interior_h; ++j)
                np_set(w, ox + 1 + i, oy + j, oz, air);
        for (i = 0; i < interior_w; ++i)
            np_set(w, ox + 1 + i, oy - 1, oz, obs);
        for (i = 0; i < interior_w; ++i)
            np_set(w, ox + 1 + i, oy + interior_h, oz, obs);
        for (j = -1; j <= interior_h; ++j) {
            np_set(w, ox, oy + j, oz, obs);
            np_set(w, ox + interior_w + 1, oy + j, oz, obs);
        }
    } else {
        for (i = 0; i < interior_w; ++i)
            for (j = 0; j < interior_h; ++j)
                np_set(w, ox, oy + j, oz + 1 + i, air);
        for (i = 0; i < interior_w; ++i)
            np_set(w, ox, oy - 1, oz + 1 + i, obs);
        for (i = 0; i < interior_w; ++i)
            np_set(w, ox, oy + interior_h, oz + 1 + i, obs);
        for (j = -1; j <= interior_h; ++j) {
            np_set(w, ox, oy + j, oz, obs);
            np_set(w, ox, oy + j, oz + interior_w + 1, obs);
        }
    }
}

MC_HD static inline void np_build_broken_frame(NpWorld *w, int ox, int oy, int oz) {
    np_build_frame(w, ox, oy, oz, 2, 3, NP_AXIS_X);
    np_set(w, ox + 3, oy + 3, oz, mc_state(NP_BLK_AIR, 0));
}

typedef struct {
    int detect_valid;
    int detect_w;
    int detect_h;
    int detect_axis;
    int spawn_ok;
    int portal_count;
    double out_x, out_y, out_z;
    float  out_yaw;
    double out_mx, out_mz;
    double last_u, last_v;
} NpScenarioResult;

MC_HD static inline void np_run_scenario(int idx, NpScenarioResult *r) {
    NpWorld w;
    NpPortalSize sz;
    NpPattern src, dst;
    NpEntity ent;
    (void)idx;

    r->detect_valid = 0;
    r->detect_w = r->detect_h = r->detect_axis = 0;
    r->spawn_ok = 0;
    r->portal_count = 0;
    r->out_x = r->out_y = r->out_z = 0.0;
    r->out_yaw = 0.0f;
    r->out_mx = r->out_mz = 0.0;
    r->last_u = r->last_v = 0.0;

    switch (idx) {
    case 0: /* X-axis frame detect at interior */
        np_build_frame(&w, 7, 5, 11, 2, 3, NP_AXIS_X);
        np_size_init(&sz, &w, 8, 6, 11, NP_AXIS_X);
        r->detect_valid = np_size_is_valid(&sz);
        r->detect_w = sz.width;
        r->detect_h = sz.height;
        r->detect_axis = sz.axis;
        break;
    case 1: /* Z-axis frame detect */
        np_build_frame(&w, 10, 5, 7, 2, 3, NP_AXIS_Z);
        np_size_init(&sz, &w, 10, 6, 8, NP_AXIS_Z);
        r->detect_valid = np_size_is_valid(&sz);
        r->detect_w = sz.width;
        r->detect_h = sz.height;
        r->detect_axis = sz.axis;
        break;
    case 2: /* broken frame */
        np_build_broken_frame(&w, 7, 5, 11);
        np_size_init(&sz, &w, 8, 6, 11, NP_AXIS_X);
        r->detect_valid = np_size_is_valid(&sz);
        break;
    case 3: /* try_spawn fills portal blocks */
        np_build_frame(&w, 7, 4, 10, 2, 3, NP_AXIS_X);
        r->spawn_ok = np_try_spawn_portal(&w, 8, 5, 10);
        r->portal_count = np_count_portal_blocks(&w);
        break;
    case 4: /* overworld X -> nether Z teleport (fixed frames) */
        np_build_frame(&w, 6, 4, 10, 2, 3, NP_AXIS_X);
        np_try_spawn_portal(&w, 7, 5, 10);
        ent.pos_x = 7.5; ent.pos_y = 5.0; ent.pos_z = 10.3;
        ent.motion_x = 0.4; ent.motion_y = 0.0; ent.motion_z = -0.2;
        ent.rotation_yaw = 90.0f;
        np_create_pattern(&w, 7, 5, 10, &src);
        np_set_portal_vec(&ent, &src);
        r->last_u = ent.last_portal_u;
        r->last_v = ent.last_portal_v;
        np_build_frame(&w, 14, 20, 6, 2, 3, NP_AXIS_Z);
        np_try_spawn_portal(&w, 14, 21, 7);
        np_create_pattern(&w, 14, 21, 7, &dst);
        np_place_in_existing_portal(&ent, &dst, ent.rotation_yaw);
        r->out_x = ent.pos_x; r->out_y = ent.pos_y; r->out_z = ent.pos_z;
        r->out_yaw = ent.rotation_yaw;
        r->out_mx = ent.motion_x; r->out_mz = ent.motion_z;
        break;
    case 5: /* entry height fraction (tall portal) */
        np_build_frame(&w, 6, 4, 10, 2, 4, NP_AXIS_X);
        np_try_spawn_portal(&w, 7, 5, 10);
        ent.pos_x = 7.5; ent.pos_y = 8.0; ent.pos_z = 10.0;
        ent.motion_x = 0.0; ent.motion_y = 0.0; ent.motion_z = 1.0;
        ent.rotation_yaw = 0.0f;
        np_create_pattern(&w, 7, 5, 10, &src);
        np_set_portal_vec(&ent, &src);
        r->last_v = ent.last_portal_v;
        np_build_frame(&w, 14, 20, 6, 2, 4, NP_AXIS_Z);
        np_try_spawn_portal(&w, 14, 21, 7);
        np_create_pattern(&w, 14, 21, 7, &dst);
        np_place_in_existing_portal(&ent, &dst, 0.0f);
        r->out_y = ent.pos_y;
        r->out_mz = ent.motion_z;
        break;
    case 6: /* wide 4x5 frame */
        np_build_frame(&w, 4, 3, 4, 4, 5, NP_AXIS_Z);
        np_size_init(&sz, &w, 4, 5, 5, NP_AXIS_Z);
        r->detect_valid = np_size_is_valid(&sz);
        r->detect_w = sz.width;
        r->detect_h = sz.height;
        r->spawn_ok = np_try_spawn_portal(&w, 4, 5, 5);
        r->portal_count = np_count_portal_blocks(&w);
        break;
    case 7: /* motion/yaw remap entering from opposite facing */
        np_build_frame(&w, 6, 4, 10, 2, 3, NP_AXIS_X);
        np_try_spawn_portal(&w, 7, 5, 10);
        ent.pos_x = 7.5; ent.pos_y = 5.5; ent.pos_z = 10.0;
        ent.motion_x = 0.3; ent.motion_y = 0.0; ent.motion_z = 0.7;
        ent.rotation_yaw = 180.0f;
        np_create_pattern(&w, 7, 5, 10, &src);
        ent.teleport_dir = np_opposite(src.forwards);
        ent.last_portal_u = 0.25;
        ent.last_portal_v = 0.5;
        np_build_frame(&w, 14, 20, 6, 2, 3, NP_AXIS_Z);
        np_create_pattern(&w, 14, 21, 7, &dst);
        np_place_in_existing_portal(&ent, &dst, 180.0f);
        r->out_yaw = ent.rotation_yaw;
        r->out_mx = ent.motion_x;
        r->out_mz = ent.motion_z;
        break;
    default:
        break;
    }
}

MC_HD static inline int np_scenario_line_count(int idx) {
    switch (idx) {
    case 0: case 1: return 4;
    case 2: return 1;
    case 3: return 2;
    case 4: return 8;
    case 5: return 3;
    case 6: return 5;
    case 7: return 3;
    default: return 0;
    }
}

MC_HD static inline void np_emit_scenario(int idx, NpScenarioResult *r,
                                          void (*emit_u32)(u32, void *), void (*emit_f32)(float, void *),
                                          void (*emit_f64)(double, void *), void *ctx) {
    switch (idx) {
    case 0: case 1:
        emit_u32((u32)r->detect_valid, ctx);
        emit_u32((u32)r->detect_w, ctx);
        emit_u32((u32)r->detect_h, ctx);
        emit_u32((u32)r->detect_axis, ctx);
        break;
    case 2:
        emit_u32((u32)r->detect_valid, ctx);
        break;
    case 3:
        emit_u32((u32)r->spawn_ok, ctx);
        emit_u32((u32)r->portal_count, ctx);
        break;
    case 4:
        emit_f64(r->last_u, ctx);
        emit_f64(r->last_v, ctx);
        emit_f64(r->out_x, ctx);
        emit_f64(r->out_y, ctx);
        emit_f64(r->out_z, ctx);
        emit_f32(r->out_yaw, ctx);
        emit_f64(r->out_mx, ctx);
        emit_f64(r->out_mz, ctx);
        break;
    case 5:
        emit_f64(r->last_v, ctx);
        emit_f64(r->out_y, ctx);
        emit_f64(r->out_mz, ctx);
        break;
    case 6:
        emit_u32((u32)r->detect_valid, ctx);
        emit_u32((u32)r->detect_w, ctx);
        emit_u32((u32)r->detect_h, ctx);
        emit_u32((u32)r->spawn_ok, ctx);
        emit_u32((u32)r->portal_count, ctx);
        break;
    case 7:
        emit_f32(r->out_yaw, ctx);
        emit_f64(r->out_mx, ctx);
        emit_f64(r->out_mz, ctx);
        break;
    default:
        break;
    }
}

#endif /* MC_NETHER_PORTAL_H */
