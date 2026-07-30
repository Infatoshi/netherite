/* nether_portal_world: BlockPortal.trySpawnPortal / frame detect on obsidian placed in a REAL
 * overworld 3x3 chunk slice (chunk_provider cp_provide_chunk).
 *
 * PORT TARGET: net/minecraft/block/BlockPortal.java trySpawnPortal / Size (MC 1.11.2)
 * INTERNAL verify (CPU==CUDA). Builds terrain like nether_portal_make, extracts a 32^3 window
 * into NpWorld, applies obsidian frame (terrain preserved outside frame), then calls READ-ONLY
 * nether_portal.h (np_size_init, np_try_spawn_portal). READ-ONLY deps: nether_portal.h,
 * chunk_provider.h. CUT: pigman spawn, neighborChanged frame break, Teleporter.makePortal. */
#ifndef MC_NETHER_PORTAL_WORLD_H
#define MC_NETHER_PORTAL_WORLD_H

#include <string.h>
#include "mc.h"
#include "mc_math.h"
#include "mc_blocks.h"
#include "chunk_provider.h"
#include "nether_portal.h"

#define NPW_WX 48
#define NPW_WY 256
#define NPW_WZ 48
#define NPW_VOL (NPW_WX * NPW_WY * NPW_WZ)
#define NPW_NUM_SCENARIOS 6

/* Local frame layout matches nether_portal.h synthetic scenarios. */
#define NPW_FRAME_OX 7
#define NPW_FRAME_OZ 11
#define NPW_FRAME_OY 5

typedef struct {
    u16 blocks[NPW_VOL];
} NpwWorld;

typedef struct {
    i64 terrain_seed;
    int wx, wz;
    int interior_w;
    int interior_h;
    int axis;
    int broken;
    int do_spawn;
} NpwScenario;

typedef struct {
    int detect_valid;
    int detect_w;
    int detect_h;
    int detect_axis;
    int spawn_ok;
    int portal_count;
    int surface_y;
    int extract_y;
} NpwResult;

MC_HD static inline u16 npw_cb_to_vanilla(u16 cb) {
    if (cb_is_stained_clay((int)cb)) return 159;
    switch (cb) {
        case CB_AIR: return 0;
        case CB_STONE: return 1;
        case CB_WATER: return 9;
        case CB_GRASS: return 2;
        case CB_DIRT: return 3;
        case CB_BEDROCK: return 7;
        case CB_GRAVEL: return 13;
        case CB_SAND: return 12;
        case CB_SANDSTONE: return 24;
        case CB_RED_SANDSTONE: return 179;
        case CB_ICE: return 79;
        case CB_LAVA: return 11;
        case CB_FLOWING_LAVA: return 10;
        case CB_FLOWING_WATER: return 8;
        case CB_WATER_LILY: return 111;
        case CB_MYCELIUM: return 110;
        case CB_SNOW_LAYER: return 78;
        case CB_HARDENED_CLAY: return 172;
        case CB_STAINED_HARDENED_CLAY: return 159;
        case CB_PODZOL: return 3;
        case CB_COARSE_DIRT: return 3;
        default: return cb;
    }
}

MC_HD static inline int npw_idx(int x, int y, int z) {
    return (y * NPW_WZ + z) * NPW_WX + x;
}

MC_HD static inline int npw_in(int x, int y, int z) {
    return x >= 0 && x < NPW_WX && y >= 0 && y < NPW_WY && z >= 0 && z < NPW_WZ;
}

MC_HD static inline int npw_block_id(const NpwWorld *w, int x, int y, int z) {
    if (!npw_in(x, y, z)) return BLK_AIR;
    return mc_state_id(w->blocks[npw_idx(x, y, z)]);
}

MC_HD static inline void npw_set(NpwWorld *w, int x, int y, int z, u16 s) {
    if (npw_in(x, y, z)) w->blocks[npw_idx(x, y, z)] = s;
}

MC_HD static inline int npw_is_solid_id(int id) {
    if (id == BLK_AIR) return 0;
    if (id == 8 || id == 9) return 0;
    if (id == 10 || id == 11) return 0;
    return 1;
}

MC_HD static inline int npw_find_surface_y(const NpwWorld *w, int wx, int wz) {
    int y;
    for (y = NPW_WY - 1; y >= 0; --y) {
        if (npw_is_solid_id(npw_block_id(w, wx, y, wz))) return y;
    }
    return CB_SEA_LEVEL;
}

MC_HD static inline void npw_build_terrain(NpwWorld *w, CpScratch *sc, const McSinTable *st,
                                           i64 seed) {
    ChunkPrimer primer;
    int cx, cz, lx, ly, lz, wx, wz, i;
    u16 air = mc_state(BLK_AIR, 0);

    for (i = 0; i < NPW_VOL; ++i) w->blocks[i] = air;
    for (cx = -1; cx <= 1; ++cx) {
        for (cz = -1; cz <= 1; ++cz) {
            cp_provide_chunk(&primer, sc, st, seed, cx, cz);
            for (lx = 0; lx < 16; ++lx) {
                wx = (cx + 1) * 16 + lx;
                for (lz = 0; lz < 16; ++lz) {
                    wz = (cz + 1) * 16 + lz;
                    for (ly = 0; ly < NPW_WY; ++ly) {
                        int vid = (int)npw_cb_to_vanilla((u16)cb_get(&primer, lx, ly, lz));
                        npw_set(w, wx, ly, wz, mc_state(vid, 0));
                    }
                }
            }
        }
    }
}

MC_HD static inline void npw_extract_npworld(const NpwWorld *src, int ex, int ey, int ez,
                                              NpWorld *dst) {
    int x, y, z;
    u16 air = mc_state(BLK_AIR, 0);
    for (x = 0; x < NP_DIM; ++x)
        for (y = 0; y < NP_DIM; ++y)
            for (z = 0; z < NP_DIM; ++z) {
                if (npw_in(ex + x, ey + y, ez + z))
                    np_set(dst, x, y, z, src->blocks[npw_idx(ex + x, ey + y, ez + z)]);
                else
                    np_set(dst, x, y, z, air);
            }
}

/* np_build_frame without np_clear - overwrites frame voxels only. */
MC_HD static inline void npw_apply_frame(NpWorld *w, int ox, int oy, int oz,
                                         int interior_w, int interior_h, int axis) {
    u16 obs = mc_state(NP_BLK_OBSIDIAN, 0);
    u16 air = mc_state(NP_BLK_AIR, 0);
    int i, j;

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

MC_HD static inline void npw_apply_broken_frame(NpWorld *w, int ox, int oy, int oz) {
    npw_apply_frame(w, ox, oy, oz, 2, 3, NP_AXIS_X);
    np_set(w, ox + 3, oy + 3, oz, mc_state(NP_BLK_AIR, 0));
}

MC_HD static inline void npw_scenario_params(int idx, NpwScenario *sc) {
    switch (idx) {
    case 0:
        sc->terrain_seed = 12345LL; sc->wx = 24; sc->wz = 24;
        sc->interior_w = 2; sc->interior_h = 3; sc->axis = NP_AXIS_X;
        sc->broken = 0; sc->do_spawn = 0; break;
    case 1:
        sc->terrain_seed = 0LL; sc->wx = 10; sc->wz = 30;
        sc->interior_w = 2; sc->interior_h = 3; sc->axis = NP_AXIS_Z;
        sc->broken = 0; sc->do_spawn = 0; break;
    case 2:
        sc->terrain_seed = 7LL; sc->wx = 20; sc->wz = 20;
        sc->interior_w = 2; sc->interior_h = 3; sc->axis = NP_AXIS_X;
        sc->broken = 1; sc->do_spawn = 0; break;
    case 3:
        sc->terrain_seed = 999LL; sc->wx = 24; sc->wz = 16;
        sc->interior_w = 2; sc->interior_h = 3; sc->axis = NP_AXIS_X;
        sc->broken = 0; sc->do_spawn = 1; break;
    case 4:
        sc->terrain_seed = 424242LL; sc->wx = 32; sc->wz = 32;
        sc->interior_w = 2; sc->interior_h = 3; sc->axis = NP_AXIS_Z;
        sc->broken = 0; sc->do_spawn = 1; break;
    case 5:
        sc->terrain_seed = 12345LL; sc->wx = 16; sc->wz = 16;
        sc->interior_w = 4; sc->interior_h = 5; sc->axis = NP_AXIS_Z;
        sc->broken = 0; sc->do_spawn = 1; break;
    default:
        sc->terrain_seed = 12345LL; sc->wx = 24; sc->wz = 24;
        sc->interior_w = 2; sc->interior_h = 3; sc->axis = NP_AXIS_X;
        sc->broken = 0; sc->do_spawn = 0; break;
    }
}

MC_HD static inline void npw_run_scenario(int idx, NpwWorld *big, CpScratch *sc,
                                          const McSinTable *st, NpwResult *r) {
    NpwScenario scn;
    NpWorld local;
    NpPortalSize sz;
    int sy, ex, ey, ez;
    int ax, ay, az;

    npw_scenario_params(idx, &scn);
    npw_build_terrain(big, sc, st, scn.terrain_seed);

    sy = npw_find_surface_y(big, scn.wx, scn.wz);
    ex = scn.wx - NPW_FRAME_OX;
    ey = sy + 1 - NPW_FRAME_OY;
    ez = scn.wz - NPW_FRAME_OZ;

    npw_extract_npworld(big, ex, ey, ez, &local);

    if (scn.broken)
        npw_apply_broken_frame(&local, NPW_FRAME_OX, NPW_FRAME_OY, NPW_FRAME_OZ);
    else
        npw_apply_frame(&local, NPW_FRAME_OX, NPW_FRAME_OY, NPW_FRAME_OZ,
                        scn.interior_w, scn.interior_h, scn.axis);

    ax = NPW_FRAME_OX + 1;
    ay = NPW_FRAME_OY;
    az = (scn.axis == NP_AXIS_X) ? NPW_FRAME_OZ : NPW_FRAME_OZ + 1;

    np_size_init(&sz, &local, ax, ay, az, scn.axis);
    r->detect_valid = np_size_is_valid(&sz);
    r->detect_w = sz.width;
    r->detect_h = sz.height;
    r->detect_axis = sz.axis;
    r->surface_y = sy;
    r->extract_y = ey;

    if (scn.do_spawn) {
        r->spawn_ok = np_try_spawn_portal(&local, ax, ay, az);
        r->portal_count = np_count_portal_blocks(&local);
    } else {
        r->spawn_ok = 0;
        r->portal_count = 0;
    }
}

MC_HD static inline int npw_emit_result(const NpwResult *r,
                                        void (*emit_u32)(u32, void *), void *ctx) {
    emit_u32((u32)r->detect_valid, ctx);
    emit_u32((u32)r->detect_w, ctx);
    emit_u32((u32)r->detect_h, ctx);
    emit_u32((u32)r->detect_axis, ctx);
    emit_u32((u32)r->spawn_ok, ctx);
    emit_u32((u32)r->portal_count, ctx);
    emit_u32((u32)r->surface_y, ctx);
    emit_u32((u32)r->extract_y, ctx);
    return 8;
}

#endif /* MC_NETHER_PORTAL_WORLD_H */
