/* falling_live.h - EntityFallingBlock / BlockFalling live tick.
 *
 * Magma magma/game/live_sim.c and blaze-CPU/CUDA compile this one source.
 * Magma wrappers stay thin; do not re-derive lane/fallt46 semantics.
 *
 * Include after defining:
 *   FL_W                    world pointer type
 *   fl_id(w,x,y,z)
 *   fl_meta(w,x,y,z)        meta 0..15
 *   fl_set(w,x,y,z,id,meta) canonical world write
 *   FL_STORE                store pointer type with:
 *     ents[], fall_updates[], fall_landings[], n_active, ticks
 * Optional overrides: fl_ents, fl_upd, fl_land, fl_n_active, fl_ticks,
 * FL_MAX, FL_UPDATES.
 *
 * Java 1.11.2 (java/oracle-src):
 *   BlockFalling.onBlockAdded       BlockFalling.java:33-36
 *   BlockFalling.neighborChanged    BlockFalling.java:43-46
 *   BlockFalling.updateTick         BlockFalling.java:48-54  checkFallable
 *   BlockFalling.checkFallable      BlockFalling.java:56-88
 *   BlockFalling.tickRate           BlockFalling.java:97-100 return 2
 *   BlockFalling.canFallThrough     BlockFalling.java:102-107 FIRE/AIR/WATER/LAVA
 *   EntityFallingBlock ctor         EntityFallingBlock.java:50-64
 *     setSize(0.98F, 0.98F); y + (1.0F - height) / 2; motion 0
 *   EntityFallingBlock.onUpdate     EntityFallingBlock.java:102-218
 *     fallTime++ == 0: setBlockToAir or setDead (:116-128)
 *     gravity 0.03999999910593033D unless hasNoGravity (:131-134)
 *     move then drag 0.9800000190734863D (:136-139)
 *     onGround: canFallThrough(y - 0.009999999776482582D) clears onGround
 *       (:149-154)
 *     land: motionX/Z * 0.699999988079071D, motionY * -0.5D, setBlockState
 *       (:156-166)
 *     else fallTime > 100 && y out of [1,256] or fallTime > 600 -> die
 *       (:207-215)
 *   World.handleMaterialAcceleration is not involved.
 *
 * Magma extras (M1 is magma semantics; tape 151855Z world_hash 310/310):
 *   schedule delay = tickRate 2; landing packet +1 then neighbor schedule 3
 *   custom collision tops (not Entity.move)
 *   sand/gravel only (not anvil / dragon egg)
 *   failed mayPlace ends as no block (no EntityItem)
 */
#ifndef MC_FALLING_LIVE_H
#define MC_FALLING_LIVE_H

#include <math.h>
#include <string.h>

#include "mc.h"
#include "mc_blocks.h"
#include "block_props_table.h"

#ifndef FL_W
#error "falling_live.h requires FL_W and fl_id/fl_meta/fl_set"
#endif
#ifndef FL_STORE
#error "falling_live.h requires FL_STORE"
#endif

#ifndef fl_ents
#define fl_ents(s) ((s)->ents)
#endif
#ifndef fl_upd
#define fl_upd(s) ((s)->fall_updates)
#endif
#ifndef fl_land
#define fl_land(s) ((s)->fall_landings)
#endif
#ifndef fl_n_active
#define fl_n_active(s) ((s)->n_active)
#endif
#ifndef fl_ticks
#define fl_ticks(s) ((s)->ticks)
#endif
#ifndef FL_MAX
#define FL_MAX 48
#endif
#ifndef FL_UPDATES
#define FL_UPDATES 128
#endif

/* BlockFalling subclasses ported here: sand 12, gravel 13. */
MC_HD static inline int fl_is_gravity(int id) {
    return id == BLK_SAND || id == BLK_GRAVEL;
}

/* BlockFalling.canFallThrough :102-107. Narrower than isReplaceable:
 * plants/snow/circuits do not support a gravity block, but they are not
 * AIR/FIRE/WATER/LAVA and therefore do not trigger checkFallable. */
MC_HD static inline int fl_can_fall_through(int id) {
    return id == BLK_AIR || id == 51 ||
           id == BLK_FLOWING_WATER || id == BLK_WATER ||
           id == BLK_FLOWING_LAVA || id == BLK_LAVA;
}

MC_HD static inline int fl_replaceable(FL_W *w, int x, int y, int z) {
    int id = fl_id(w, x, y, z);
    return (mc_bpt_props(id).flags & BF_REPLACEABLE) != 0;
}

/* Highest collision surface in a cell at the falling entity's centered X/Z.
 * Mirrors magma/game/live_sim.c fall_collision_top (player_survival shapes).
 * Non-solid partials return no box. */
MC_HD static inline double fl_collision_top(FL_W *w, int x, int y, int z) {
    int id = fl_id(w, x, y, z);
    int meta = fl_meta(w, x, y, z);
    if (id == BLK_WEB || !(mc_bpt_props(id).flags & BF_SOLID)) return -1.0;
    if (id == BLK_STONE_SLAB || id == BLK_WOODEN_SLAB ||
        id == BLK_RED_SANDSTONE_SLAB)
        return (double)y + ((meta & 8) ? 1.0 : 0.5);
    if (id == BLK_SOUL_SAND) return (double)y + 0.875;
    if (id == BLK_CACTUS) return (double)y + 0.9375;
    if (id == BLK_FENCE || id == BLK_NETHER_BRICK_FENCE ||
        id == BLK_COBBLESTONE_WALL)
        return (double)y + 1.5;
    if (id == BLK_TRAPDOOR && !(meta & 4))
        return (double)y + ((meta & 8) ? 1.0 : 0.1875);
    return (double)y + 1.0;
}

MC_HD static inline void fl_schedule_delay(FL_STORE *s, FL_W *w,
                                           int x, int y, int z, int delay) {
    int id, i;
    if (!s || !w || y < 0 || y > 255) return;
    id = fl_id(w, x, y, z);
    if (!fl_is_gravity(id)) return;
    for (i = 0; i < FL_UPDATES; ++i) {
        if (fl_upd(s)[i].active && fl_upd(s)[i].x == x &&
            fl_upd(s)[i].y == y && fl_upd(s)[i].z == z &&
            fl_upd(s)[i].block_id == id)
            return;
    }
    for (i = 0; i < FL_UPDATES; ++i) {
        if (fl_upd(s)[i].active) continue;
        fl_upd(s)[i].active = 1;
        fl_upd(s)[i].x = x;
        fl_upd(s)[i].y = y;
        fl_upd(s)[i].z = z;
        fl_upd(s)[i].block_id = id;
        fl_upd(s)[i].due_tick = (long long)fl_ticks(s) + delay;
        return;
    }
}

/* setBlockState: placed block onBlockAdded, then neighbors. Only the
 * vertical-above notification can make sand/gravel newly unsupported.
 * tickRate = 2 (BlockFalling.java:97-100). */
MC_HD static inline void fl_block_changed(FL_STORE *s, FL_W *w,
                                          int x, int y, int z) {
    fl_schedule_delay(s, w, x, y, z, 2);
    fl_schedule_delay(s, w, x, y + 1, z, 2);
}

MC_HD static inline int fl_spawn(FL_STORE *s, int x, int y, int z,
                                 int id, int meta) {
    int i;
    for (i = 0; i < FL_MAX; ++i) {
        if (fl_ents(s)[i].active) continue;
        memset(&fl_ents(s)[i], 0, sizeof fl_ents(s)[i]);
        fl_ents(s)[i].active = 1;
        fl_ents(s)[i].type = 2;
        fl_ents(s)[i].x = (double)x + 0.5;
        /* EntityFallingBlock ctor :56: y + (1.0F - height) / 2, height=.98F. */
        fl_ents(s)[i].y = (double)y + (double)((1.0f - 0.98f) / 2.0f);
        fl_ents(s)[i].z = (double)z + 0.5;
        fl_ents(s)[i].item = id;
        fl_ents(s)[i].meta = meta;
        fl_ents(s)[i].lifespan = 600;
        fl_n_active(s)++;
        return 1;
    }
    return 0;
}

MC_HD static inline void fl_queue_landing(FL_STORE *s, int x, int y, int z,
                                          int id, int meta) {
    int i;
    for (i = 0; i < FL_MAX; ++i) {
        if (fl_land(s)[i].active) continue;
        fl_land(s)[i].active = 1;
        fl_land(s)[i].x = x;
        fl_land(s)[i].y = y;
        fl_land(s)[i].z = z;
        fl_land(s)[i].block_id = id;
        fl_land(s)[i].block_meta = meta;
        fl_land(s)[i].due_tick = (long long)fl_ticks(s) + 1;
        return;
    }
}

/* EntityFallingBlock places on the integrated server. The client observes
 * that block through the next tick's server packet, before click handling. */
MC_HD static inline void fl_pre_player_tick(FL_STORE *s, FL_W *w) {
    int i;
    if (!s || !w) return;
    for (i = 0; i < FL_MAX; ++i) {
        if (!fl_land(s)[i].active ||
            fl_land(s)[i].due_tick > (long long)fl_ticks(s))
            continue;
        fl_set(w, fl_land(s)[i].x, fl_land(s)[i].y, fl_land(s)[i].z,
               fl_land(s)[i].block_id, fl_land(s)[i].block_meta);
        /* Packet is one client tick after the server-side placement that
         * scheduled BlockFalling.updateTick. Subsequent source-removal is
         * observed one tick after that, so the client-world transition is
         * three ticks from this placement view. */
        fl_schedule_delay(s, w, fl_land(s)[i].x, fl_land(s)[i].y,
                          fl_land(s)[i].z, 3);
        fl_schedule_delay(s, w, fl_land(s)[i].x, fl_land(s)[i].y + 1,
                          fl_land(s)[i].z, 3);
        fl_land(s)[i].active = 0;
    }
}

MC_HD MC_NOINLINE static void fl_tick_entity(FL_STORE *s, FL_W *w, int ei) {
    int bx, by, bz, y;
    if (!s || !w) return;
    if (fl_ents(s)[ei].age == 0) {
        bx = (int)floor(fl_ents(s)[ei].x);
        by = (int)floor(fl_ents(s)[ei].y);
        bz = (int)floor(fl_ents(s)[ei].z);
        if (fl_id(w, bx, by, bz) != fl_ents(s)[ei].item) {
            fl_ents(s)[ei].active = 0;
            if (fl_n_active(s) > 0) fl_n_active(s)--;
            return;
        }
        fl_set(w, bx, by, bz, BLK_AIR, 0);
        fl_block_changed(s, w, bx, by, bz);
    }

    fl_ents(s)[ei].my -= 0.03999999910593033; /* EntityFallingBlock.java:133 */
    {
        double old_y = fl_ents(s)[ei].y;
        double new_y = old_y + fl_ents(s)[ei].my;
        double hit_top = -1.0;
        bx = (int)floor(fl_ents(s)[ei].x);
        bz = (int)floor(fl_ents(s)[ei].z);
        for (y = (int)floor(old_y); y >= (int)floor(new_y) - 1; --y) {
            double top = fl_collision_top(w, bx, y, bz);
            if (top >= 0.0 && top <= old_y && top > new_y && top > hit_top)
                hit_top = top;
        }
        if (hit_top >= 0.0) {
            fl_ents(s)[ei].y = hit_top;
            fl_ents(s)[ei].on_ground = 1;
        } else {
            fl_ents(s)[ei].y = new_y;
            fl_ents(s)[ei].on_ground = 0;
        }
    }
    fl_ents(s)[ei].x += fl_ents(s)[ei].mx;
    fl_ents(s)[ei].z += fl_ents(s)[ei].mz;
    fl_ents(s)[ei].mx *= 0.9800000190734863; /* EntityFallingBlock.java:137 */
    fl_ents(s)[ei].my *= 0.9800000190734863;
    fl_ents(s)[ei].mz *= 0.9800000190734863;
    fl_ents(s)[ei].age++;

    if (fl_ents(s)[ei].on_ground) {
        int below;
        bx = (int)floor(fl_ents(s)[ei].x);
        by = (int)floor(fl_ents(s)[ei].y);
        bz = (int)floor(fl_ents(s)[ei].z);
        below = fl_id(w, bx,
                      (int)floor(fl_ents(s)[ei].y - 0.009999999776482582),
                      bz); /* EntityFallingBlock.java:149 */
        if (fl_can_fall_through(below)) {
            fl_ents(s)[ei].on_ground = 0;
            return;
        }
        fl_ents(s)[ei].mx *= 0.699999988079071; /* :156 */
        fl_ents(s)[ei].mz *= 0.699999988079071;
        fl_ents(s)[ei].my *= -0.5;              /* :158 */
        fl_ents(s)[ei].active = 0;
        if (fl_n_active(s) > 0) fl_n_active(s)--;
        if (by >= 0 && by <= 255 && fl_replaceable(w, bx, by, bz) &&
            !fl_can_fall_through(fl_id(w, bx, by - 1, bz))) {
            fl_queue_landing(s, bx, by, bz, fl_ents(s)[ei].item,
                             fl_ents(s)[ei].meta);
        }
        /* Vanilla otherwise converts to an EntityItem. Netherite's world
         * truth has no item digest, so a failed mayPlace ends as no block. */
    } else {
        by = (int)floor(fl_ents(s)[ei].y);
        if (!((fl_ents(s)[ei].age > 100 && (by < 1 || by > 256)) ||
              fl_ents(s)[ei].age > 600)) /* EntityFallingBlock.java:207 */
            return;
        fl_ents(s)[ei].active = 0;
        if (fl_n_active(s) > 0) fl_n_active(s)--;
    }
}

/* WorldServer scheduled block ticks run before the entity update pass.
 * A newly spawned EntityFallingBlock therefore removes its source and
 * takes its first gravity step in this same runtime tick. */
MC_HD MC_NOINLINE static void fl_tick_scheduled(FL_STORE *s, FL_W *w) {
    int i;
    if (!s || !w) return;
    for (i = 0; i < FL_UPDATES; ++i) {
        if (!fl_upd(s)[i].active ||
            fl_upd(s)[i].due_tick > (long long)fl_ticks(s))
            continue;
        if (fl_id(w, fl_upd(s)[i].x, fl_upd(s)[i].y, fl_upd(s)[i].z) ==
                fl_upd(s)[i].block_id &&
            fl_upd(s)[i].y >= 0 &&
            fl_can_fall_through(fl_id(w, fl_upd(s)[i].x,
                                      fl_upd(s)[i].y - 1,
                                      fl_upd(s)[i].z))) {
            if (!fl_spawn(s, fl_upd(s)[i].x, fl_upd(s)[i].y, fl_upd(s)[i].z,
                          fl_upd(s)[i].block_id,
                          fl_meta(w, fl_upd(s)[i].x, fl_upd(s)[i].y,
                                  fl_upd(s)[i].z))) {
                fl_upd(s)[i].due_tick++;
                continue;
            }
        }
        fl_upd(s)[i].active = 0;
    }
}

MC_HD static inline void fl_tick_falling_ents(FL_STORE *s, FL_W *w) {
    int i;
    if (!s || !w) return;
    for (i = 0; i < FL_MAX; ++i) {
        if (!fl_ents(s)[i].active || fl_ents(s)[i].type != 2) continue;
        fl_tick_entity(s, w, i);
    }
}

#endif /* MC_FALLING_LIVE_H */
