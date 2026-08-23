/* item_live.h - EntityItem live tick shared by magma live_sim.c and blaze.
 *
 * Magma magma/game/live_sim.c and blaze-CPU/CUDA compile this one source.
 * Wrappers convert GmLiveEnt / CuItem <-> McItem and stay thin.
 *
 * Include after defining:
 *   IL_W                    world pointer type
 *   il_id(w,x,y,z)          block id
 *   il_meta(w,x,y,z)        meta 0..15
 *
 * Java 1.11.2 (java/oracle-src):
 *   EntityItem.onUpdate              EntityItem.java:97-204
 *     super.onUpdate -> onEntityUpdate fire Entity.java:541-560
 *     delayBeforeCanPickup--         EntityItem.java:108-111
 *     gravity (double)0.04F          EntityItem.java:122
 *     Entity.move                    EntityItem.java:134
 *       isFlammableWithin            Entity.java:1067-1078
 *         dealFireDamage(1)          Entity.java:1069 / EntityItem.java:328-331
 *         ++fire; setFire(8) if 0    Entity.java:1073-1077
 *     lava hop at BlockPos           EntityItem.java:139-145  CLASS C rand
 *     searchForOtherItemsNearby      EntityItem.java:137-150, :209-215
 *       combineItems                 EntityItem.java:221-292
 *     slipperiness * 0.98F           EntityItem.java:153-164
 *     age++; lifespan 6000 setDead   EntityItem.java:171-197
 *     handleWaterMovement            EntityItem.java:176, :306-322
 *   onCollideWithPlayer              EntityItem.java:428-488
 *     delay>0 return :432
 *     owner skip if set :440 (live owner is null)
 *     InventoryPlayer.addItemStackToInventory
 *   setDefaultPickupDelay 10         EntityItem.java:564-566
 *   EntityPlayer.dropItem delay 40   EntityPlayer.java:829
 *
 * Magma extras (M1 is magma==blaze; not live Java rand):
 *   spawnAsEntity xz Math.random     EntityItem.java:59-61 CLASS C
 *     live table zeros mx/my/mz (cu_spawn_item / live_fill_ent)
 *   lava hop nextFloat skipped       EntityItem.java:142-144 CLASS C
 *   EntityItem table cap 48          Java World.spawnEntity has no cap
 */
#ifndef MC_ITEM_LIVE_H
#define MC_ITEM_LIVE_H

#include <math.h>

#include "mc.h"
#include "mc_blocks.h"
#include "mc_math.h"
#include "block_props_table.h"
#include "entity_item.h"
#include "inventory_stack_rules.h"

#ifndef IL_W
#error "item_live.h requires IL_W and il_id/il_meta"
#endif
#ifndef il_id
#error "item_live.h requires il_id(w,x,y,z)"
#endif
#ifndef il_meta
#define il_meta(w, x, y, z) 0
#endif

#ifndef IL_MAX_BLOCKS
#define IL_MAX_BLOCKS 64
#endif

#define IL_FIRE_BLOCK 51 /* BlockFire; Block.java registerBlock fire=51 */

MC_HD static inline int il_solid_id(int id) {
    BptProps p;
    if (id <= 0) return 0;
    p = mc_bpt_props(id);
    return ((p.flags & BF_SOLID) && !(p.flags & BF_LIQUID)) ? 1 : 0;
}

MC_HD static inline int il_flammable_id(int id) {
    /* World.isFlammableWithin: Material.FIRE or Material.LAVA. */
    return id == BLK_FLOWING_LAVA || id == BLK_LAVA || id == IL_FIRE_BLOCK;
}

MC_HD static inline int il_water_id(int id) {
    return id == BLK_FLOWING_WATER || id == BLK_WATER;
}

MC_HD static inline int il_gather(IL_W *w, const McAABB *q, McAABB *out, int cap) {
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
                if (!il_solid_id(il_id(w, x, y, z))) continue;
                if (n == cap) return n;
                out[n++] = mc_aabb_make(x, y, z, x + 1, y + 1, z + 1);
            }
    return n;
}

MC_HD static inline u16 il_under(IL_W *w, const McItem *it) {
    int ux, uy, uz;
    ux = mc_floor(it->posX);
    uy = mc_floor(it->box.minY) - 1;
    uz = mc_floor(it->posZ);
    if (uy < 0) uy = 0;
    return mc_state(il_id(w, ux, uy, uz), il_meta(w, ux, uy, uz) & 15);
}

/* World.isFlammableWithin(box.contract(0.001)) Entity.java:1067.
 * Loop floor(min) .. ceil(max)-1 as isMaterialInBB. */
MC_HD static inline int il_is_flammable(IL_W *w, const McAABB *box) {
    int x0, x1, y0, y1, z0, z1, x, y, z;
    x0 = mc_floor(box->minX + 0.001);
    y0 = mc_floor(box->minY + 0.001);
    z0 = mc_floor(box->minZ + 0.001);
    x1 = (int)ceil(box->maxX - 0.001);
    y1 = (int)ceil(box->maxY - 0.001);
    z1 = (int)ceil(box->maxZ - 0.001);
    if (y0 < 0) y0 = 0;
    if (y1 > 256) y1 = 256;
    for (x = x0; x < x1; ++x)
        for (y = y0; y < y1; ++y)
            for (z = z0; z < z1; ++z)
                if (il_flammable_id(il_id(w, x, y, z))) return 1;
    return 0;
}

MC_HD static inline int il_water_depth(IL_W *w, int x, int y, int z) {
    int id = il_id(w, x, y, z);
    int m;
    if (!il_water_id(id)) return -1;
    m = il_meta(w, x, y, z) & 15;
    return m >= 8 ? 0 : m;
}

/* BlockLiquid.getFlow one cell, same as psv_water_cell_flow. */
MC_HD static inline void il_water_cell_flow(IL_W *w, int bx, int by, int bz,
                                            double *fx, double *fy, double *fz) {
    static const int DX[4] = {0, -1, 0, 1}, DZ[4] = {1, 0, -1, 0};
    double d0 = 0.0, d1 = 0.0, d2 = 0.0;
    int i, f, j, k, nx, nz, id;
    i = il_water_depth(w, bx, by, bz);
    for (f = 0; f < 4; ++f) {
        nx = bx + DX[f];
        nz = bz + DZ[f];
        j = il_water_depth(w, nx, by, nz);
        if (j < 0) {
            id = il_id(w, nx, by, nz);
            if (!il_solid_id(id)) {
                j = il_water_depth(w, nx, by - 1, nz);
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

/* EntityItem.handleWaterMovement EntityItem.java:306-322 ->
 * World.handleMaterialAcceleration 0.014 * unit getFlow. Still source = 0. */
MC_HD static inline void il_handle_water(IL_W *w, McItem *it) {
    double x0, x1, y0, y1, z0, z1, sx, sy, sz, l;
    int bx, by, bz;
    x0 = it->box.minX + 0.001;
    x1 = it->box.maxX - 0.001;
    z0 = it->box.minZ + 0.001;
    z1 = it->box.maxZ - 0.001;
    y0 = it->box.minY + 0.4 + 0.001;
    y1 = it->box.maxY - 0.4 - 0.001;
    if (y0 > y1) {
        double t = y0;
        y0 = y1;
        y1 = t;
    }
    sx = sy = sz = 0.0;
    for (bx = mc_floor(x0); bx < (int)ceil(x1); ++bx)
        for (by = mc_floor(y0); by < (int)ceil(y1); ++by)
            for (bz = mc_floor(z0); bz < (int)ceil(z1); ++bz) {
                double fx, fy, fz;
                if (!il_water_id(il_id(w, bx, by, bz))) continue;
                il_water_cell_flow(w, bx, by, bz, &fx, &fy, &fz);
                sx += fx;
                sy += fy;
                sz += fz;
            }
    l = (double)(float)sqrt(sx * sx + sy * sy + sz * sz);
    if (l > 0.0) {
        sx /= l;
        sy /= l;
        sz /= l;
        it->motionX += sx * 0.014;
        it->motionY += sy * 0.014;
        it->motionZ += sz * 0.014;
    }
}

/* One EntityItem.onUpdate after World.ticksExisted++. */
MC_HD MC_NOINLINE static void il_tick_item(IL_W *w, McItem *items, int n, int i) {
    McItem *it;
    McAABB q, blocks[IL_MAX_BLOCKS];
    int nb, flag;
    if (!items || i < 0 || i >= n) return;
    it = &items[i];
    if (it->dead) return;
    if (it->count <= 0) {
        it->dead = 1;
        return;
    }
    if (it->health <= 0)
        it->health = EI_HEALTH;
    it->ticksExisted++;
    ei_fire_pulse(it);
    if (it->dead) return;
    q = mc_aabb_addcoord(&it->box, it->motionX, it->motionY, it->motionZ);
    nb = il_gather(w, &q, blocks, IL_MAX_BLOCKS);
    flag = ei_pre(it, blocks, nb, 0);
    if (it->count <= 0) {
        it->dead = 1;
        return;
    }
    /* Entity.move isFlammableWithin Entity.java:1067-1078. */
    if (il_is_flammable(w, &it->box)) {
        ei_attack(it, 1.0f); /* dealFireDamage(1) Entity.java:1069 */
        if (!it->dead) {
            ++it->fire;
            if (it->fire == 0)
                ei_set_fire(it, EI_FLAMMABLE_FIRE_SEC);
        }
    } else if (it->fire <= 0) {
        it->fire = -EI_FIRE_IMMUNE_TICKS;
    }
    if (it->dead) return;
    /* lava hop EntityItem.java:139-145 skipped: rand.nextFloat CLASS C */
    if (flag || it->ticksExisted % 25 == 0)
        ei_search(items, n, i);
    if (it->dead) return;
    ei_post(it, il_under(w, it));
    if (it->dead) return;
    il_handle_water(w, it);
}

/* EntityItem.onCollideWithPlayer EntityItem.java:428-488 minus stats/achievements.
 * owner is null in the live table. addItemStackToInventory mutates leftover. */
MC_HD static inline int il_try_pickup(McItem *it, IsrInv *inv, const McAABB *player) {
    ICStack incoming;
    if (!it || !inv || !player || it->dead) return 0;
    if (it->delayBeforeCanPickup > 0) return 0;
    if (!mc_aabb_intersects(&it->box, player)) return 0;
    incoming = ic_mk(it->item, it->count, it->meta);
    isr_add_item_stack_to_inventory(inv, &incoming);
    it->count = incoming.count;
    if (it->count <= 0) {
        it->dead = 1;
        return 1;
    }
    return 0;
}

#endif /* MC_ITEM_LIVE_H */
