/* xp_world_tick.h - EntityXPOrb lava / water / pushOut world queries.
 *
 * Include after defining XL_W, xl_id(w,x,y,z), and optionally xl_meta.
 * Magma: GmWorld + gm_world_block/meta. Blaze: Blaze + cu_world_*.
 *
 * Java 1.11.2:
 *   EntityXPOrb.handleWaterMovement     EntityXPOrb.java:179-182
 *     World.handleMaterialAcceleration  World.java:2333-2398
 *       unexpanded getEntityBoundingBox (not Entity.java:1311 -0.4 contract)
 *       BlockLiquid.modifyAcceleration  BlockLiquid.java:196-198
 *       BlockLiquid.getFlow             BlockLiquid.java:139-194
 *       0.014 * unit                    World.java:2391-2394
 *       isPushedByWater default true    Entity.java:3057-3059
 *   lava at BlockPos(this)              EntityXPOrb.java:105, BlockPos.java:42-44
 *     Material.LAVA = still 11 or flowing 10
 *   pushOutOfBlocks collidesWithAnyBlock Entity.java:2658
 */
#ifndef MC_XP_WORLD_TICK_H
#define MC_XP_WORLD_TICK_H

#ifndef XL_W
#error "xp_world_tick.h requires XL_W and xl_id"
#endif
#ifndef xl_id
#error "xp_world_tick.h requires xl_id(w,x,y,z)"
#endif
#ifndef xl_meta
#define xl_meta(w, x, y, z) 0
#endif

#include <math.h>
#include "block_props_table.h"

#ifndef XL_MAX_BLOCKS
#define XL_MAX_BLOCKS 64
#endif

/* MathHelper.ceil(double) MathHelper.java:113-117. */
MC_HD static inline int xl_ceil(double v) {
    int i = (int)v;
    return v > (double)i ? i + 1 : i;
}

MC_HD static inline int xl_solid_id(int id) {
    BptProps p;
    if (id <= 0) return 0;
    p = mc_bpt_props(id);
    return ((p.flags & BF_SOLID) && !(p.flags & BF_LIQUID)) ? 1 : 0;
}

MC_HD static inline int xl_water_id(int id) {
    return id == BLK_FLOWING_WATER || id == BLK_WATER;
}

MC_HD static inline int xl_lava_id(int id) {
    return id == BLK_FLOWING_LAVA || id == BLK_LAVA;
}

/* BlockLiquid.getLiquidHeightPercent BlockLiquid.java:60-68. */
MC_HD static inline float xl_liquid_height_percent(int meta) {
    if (meta >= 8) meta = 0;
    return (float)(meta + 1) / 9.0f;
}

MC_HD static inline int xl_water_depth(XL_W *w, int x, int y, int z) {
    int id = xl_id(w, x, y, z);
    int m;
    if (!xl_water_id(id)) return -1;
    m = xl_meta(w, x, y, z) & 15;
    return m >= 8 ? 0 : m;
}

/* BlockLiquid.isBlockSolid BlockLiquid.java:102-105 for the falling-current
 * probe: water false, ice false, otherwise solid material. */
MC_HD static inline int xl_flow_side_solid(XL_W *w, int x, int y, int z) {
    int id = xl_id(w, x, y, z);
    if (xl_water_id(id) || id == BLK_ICE) return 0;
    return xl_solid_id(id);
}

/* BlockLiquid.getFlow BlockLiquid.java:139-194. HORIZONTALS S,W,N,E. */
MC_HD static inline void xl_water_cell_flow(XL_W *w, int bx, int by, int bz,
                                            double *fx, double *fy, double *fz) {
    static const int DX[4] = {0, -1, 0, 1}, DZ[4] = {1, 0, -1, 0};
    double d0 = 0.0, d1 = 0.0, d2 = 0.0;
    int i, f, j, k, nx, nz, id;
    i = xl_water_depth(w, bx, by, bz);
    for (f = 0; f < 4; ++f) {
        nx = bx + DX[f];
        nz = bz + DZ[f];
        j = xl_water_depth(w, nx, by, nz);
        if (j < 0) {
            id = xl_id(w, nx, by, nz);
            if (!xl_solid_id(id)) {
                j = xl_water_depth(w, nx, by - 1, nz);
                if (j >= 0) {
                    k = j - (i - 8);
                    d0 += (double)(DX[f] * k);
                    d2 += (double)(DZ[f] * k);
                }
            }
        } else {
            k = j - i;
            d0 += (double)(DX[f] * k);
            d2 += (double)(DZ[f] * k);
        }
    }
    if ((xl_meta(w, bx, by, bz) & 15) >= 8) {
        for (f = 0; f < 4; ++f) {
            nx = bx + DX[f];
            nz = bz + DZ[f];
            if (xl_flow_side_solid(w, nx, by, nz) ||
                xl_flow_side_solid(w, nx, by + 1, nz)) {
                double l = (double)(float)sqrt(d0 * d0 + d1 * d1 + d2 * d2);
                if (l < 1.0e-4) {
                    d0 = d1 = d2 = 0.0;
                } else {
                    d0 /= l;
                    d1 /= l;
                    d2 /= l;
                }
                d1 += -6.0;
                break;
            }
        }
    }
    {
        double l = (double)(float)sqrt(d0 * d0 + d1 * d1 + d2 * d2);
        if (l < 1.0e-4) {
            *fx = 0.0;
            *fy = 0.0;
            *fz = 0.0;
        } else {
            *fx = d0 / l;
            *fy = d1 / l;
            *fz = d2 / l;
        }
    }
}

MC_HD static inline int xl_gather(XL_W *w, const McAABB *q, McAABB *out, int cap) {
    int n = 0, x0, x1, y0, y1, z0, z1, x, y, z;
    if (!q || !out || cap <= 0) return 0;
    x0 = mc_floor(q->minX) - 1;
    x1 = mc_floor(q->maxX) + 1;
    y0 = mc_floor(q->minY) - 1;
    y1 = mc_floor(q->maxY) + 1;
    z0 = mc_floor(q->minZ) - 1;
    z1 = mc_floor(q->maxZ) + 1;
    if (y0 < 0) y0 = 0;
    if (y1 > 255) y1 = 255;
    for (x = x0; x <= x1; ++x)
        for (y = y0; y <= y1; ++y)
            for (z = z0; z <= z1; ++z) {
                if (!xl_solid_id(xl_id(w, x, y, z))) continue;
                if (n == cap) return n;
                out[n++] = mc_aabb_make(x, y, z, x + 1, y + 1, z + 1);
            }
    return n;
}

/* World.handleMaterialAcceleration(box, WATER) World.java:2333-2398.
 * No expand(-0.4)/contract(0.001): EntityXPOrb.java:181 uses the raw box.
 * Liquid-height test :2374-2378 vs ceil(maxY). Forge isEntityInsideMaterial
 * is null for vanilla water. */
MC_HD static inline void xl_handle_water(XL_W *w, McOrb *o) {
    double x0, x1, y0, y1, z0, z1, sx, sy, sz;
    int bx, by, bz, l;
    if (!w || !o) return;
    x0 = o->box.minX;
    x1 = o->box.maxX;
    y0 = o->box.minY;
    y1 = o->box.maxY;
    z0 = o->box.minZ;
    z1 = o->box.maxZ;
    l = xl_ceil(y1);
    sx = sy = sz = 0.0;
    for (bx = mc_floor(x0); bx < xl_ceil(x1); ++bx)
        for (by = mc_floor(y0); by < xl_ceil(y1); ++by)
            for (bz = mc_floor(z0); bz < xl_ceil(z1); ++bz) {
                int id = xl_id(w, bx, by, bz);
                int meta;
                double d0, fx, fy, fz;
                if (!xl_water_id(id)) continue;
                meta = xl_meta(w, bx, by, bz) & 15;
                d0 = (double)((float)(by + 1) - xl_liquid_height_percent(meta));
                if ((double)l < d0) continue;
                xl_water_cell_flow(w, bx, by, bz, &fx, &fy, &fz);
                sx += fx;
                sy += fy;
                sz += fz;
            }
    eo_apply_water(o, sx, sy, sz);
}

/* One EntityXPOrb.onUpdate: water in super.onUpdate, then eo_tick. */
MC_HD static inline void xl_tick_orb(XL_W *w, McOrb *o,
                                     double px, double py, double pz, float eye,
                                     int spectator) {
    McAABB q, blocks[XL_MAX_BLOCKS];
    int nb, ux, uy, uz, bx, by, bz, in_lava, colliding;
    u16 under;
    if (!w || !o) return;
    xl_handle_water(w, o);
    q = mc_aabb_addcoord(&o->box, o->motionX, o->motionY, o->motionZ);
    nb = xl_gather(w, &q, blocks, XL_MAX_BLOCKS);
    colliding = eo_collides_with_any(o, blocks, nb);
    bx = mc_floor(o->posX);
    by = mc_floor(o->posY);
    bz = mc_floor(o->posZ);
    in_lava = xl_lava_id(xl_id(w, bx, by, bz));
    ux = mc_floor(o->posX);
    uy = mc_floor(o->box.minY) - 1;
    uz = mc_floor(o->posZ);
    if (uy < 0) uy = 0;
    under = mc_state(xl_id(w, ux, uy, uz), xl_meta(w, ux, uy, uz) & 15);
    eo_tick(o, px, py, pz, eye, spectator, blocks, nb, under, colliding, in_lava);
}

#endif /* MC_XP_WORLD_TICK_H */
