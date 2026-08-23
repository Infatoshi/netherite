/* player_bed: ItemBed place, BlockBed activate, EntityPlayer.trySleep /
 * wakeUpPlayer / getBedSpawnLocation / BlockBed.getSafeExitLocation.
 *
 * Pure rules. Magma live uses them; blaze compiles the same header and
 * gates it with unit tests. The RL action set has no sleep, so a blaze
 * tick never enters the sleep SM and M1 digests stay as they are.
 *
 * ItemBed does NOT call World.mayPlace (ItemBlock.java:49). block_may_place.h
 * ibp_may_place therefore does not fit: two replaceable cells + isFullyOpaque
 * below both, click facing UP only.
 *
 * Java 1.11.2 (java/oracle-src/net/minecraft):
 *   item/ItemBed.java                 onItemUse :28-83
 *   block/BlockBed.java               onBlockActivated :47-121
 *                                     getSafeExitLocation :195-228
 *                                     hasRoomForPlayer :231-234
 *                                     getMetaFromState :326-340
 *   entity/player/EntityPlayer.java    onUpdate sleep :228-257
 *                                     trySleep :1637-1707
 *                                     bedInRange :1710-1720
 *                                     wakeUpPlayer :1732-1767
 *                                     isPlayerFullyAsleep :1841-1843
 *                                     getBedSpawnLocation :1779-1800
 *                                     setSpawnPoint :1867-1886
 *                                     SleepResult :2933-2940
 *   entity/player/EntityPlayerMP.java  trySleep packet :715-728
 *   server/management/PlayerList.java  recreatePlayerEntity bed :538-580
 *   world/WorldServer.java             tick sleep skip :191-200
 *                                     wakeAllPlayers :287-302
 *                                     areAllPlayersAsleep :316-329
 *   world/WorldProvider.java           isDaytime :450-453
 *                                     resetRainAndThunder :584-589
 *   world/World.java                   isDaytime :989-991
 *                                     calculateSkylightSubtracted :1537-1541
 *
 * Nether/End explode (BlockBed.java:108-119) is magma-only: blaze RL never
 * holds a bed in DIM -1/1. Magma already explodes when dimension != 0.
 *
 * CPU==CUDA (MC_HD). No snapshot fields (lane/resumegate owns those). */
#ifndef MC_PLAYER_BED_H
#define MC_PLAYER_BED_H

#include <math.h>
#include "player_survival.h"
#include "block_may_place.h"
#include "block_props_table.h"
#include "world_weather.h" /* ww_next_dawn / ww_tick_gated_sleep */

#define BED_BLK 26
#define BED_ITEM 355
#define BED_META_FACING 3
#define BED_META_OCCUPIED 4
#define BED_META_HEAD 8

/* EntityPlayer.SleepResult ordinals (EntityPlayer.java:2933-2940). */
enum {
    BED_OK = 0,
    BED_NOT_POSSIBLE_HERE = 1,
    BED_NOT_POSSIBLE_NOW = 2,
    BED_TOO_FAR_AWAY = 3,
    BED_OTHER_PROBLEM = 4,
    BED_NOT_SAFE = 5
};

typedef struct {
    int sleeping;
    int sleep_timer;
    int bed_x, bed_y, bed_z;
    int spawn_set;
    int spawn_x, spawn_y, spawn_z;
    int spawn_forced;
    double px, py, pz;
    double mx, my, mz;
} BedSleep;

/* EnumFacing.getHorizontal S=0 W=1 N=2 E=3 (ItemBed.java:49-50). */
MC_HD static inline int bed_facing_dx(int facing) {
    switch (facing & 3) {
    case 0: return 0;
    case 1: return -1;
    case 2: return 0;
    default: return 1;
    }
}
MC_HD static inline int bed_facing_dz(int facing) {
    switch (facing & 3) {
    case 0: return 1;
    case 1: return 0;
    case 2: return -1;
    default: return 0;
    }
}

/* ItemBed.java:49 MathHelper.floor((double)(rotationYaw * 4.0F / 360.0F) + 0.5D) & 3 */
MC_HD static inline int bed_yaw_quad(float yaw) {
    int i = (int)floor((double)(yaw * 4.0f / 360.0f) + 0.5);
    return i & 3;
}

MC_HD static inline int bed_is_head_meta(int meta) { return (meta & BED_META_HEAD) != 0; }
MC_HD static inline int bed_facing_meta(int meta) { return meta & BED_META_FACING; }
MC_HD static inline int bed_occupied_meta(int meta) { return (meta & BED_META_OCCUPIED) != 0; }
MC_HD static inline int bed_foot_meta(int facing) { return facing & BED_META_FACING; }
MC_HD static inline int bed_head_meta(int facing) {
    return (facing & BED_META_FACING) | BED_META_HEAD;
}

/* BlockBed.onBlockActivated :54-64: FOOT looks up HEAD along FACING. */
MC_HD static inline void bed_head_pos(int x, int y, int z, int meta,
                                      int *hx, int *hy, int *hz) {
    int f = bed_facing_meta(meta);
    if (bed_is_head_meta(meta)) {
        *hx = x;
        *hy = y;
        *hz = z;
        return;
    }
    *hx = x + bed_facing_dx(f);
    *hy = y;
    *hz = z + bed_facing_dz(f);
}

/* Block.isFullyOpaque: material.isOpaque && isFullCube (Block.java:157-160).
 * Opaque ~ light_opacity 255. Full cube: BF_SOLID minus known non-cubes
 * (slabs/stairs/bed/chest/door/farmland/ladder/snow/wall/anvil/trapdoor/gate). */
MC_HD static inline int bed_fully_opaque(int id) {
    BptProps p;
    if (id <= 0) return 0;
    p = mc_bpt_props(id);
    if (p.light_opacity != 255) return 0;
    if ((p.flags & BF_SOLID) == 0) return 0;
    if (p.flags & BF_LIQUID) return 0;
    if (p.flags & BF_REPLACEABLE) return 0;
    switch (id) {
    case 26:  /* bed, not isFullCube (BlockBed.java:138-140) */
    case 44: case 126: case 182: case 205: /* slabs */
    case 53: case 67: case 108: case 109: case 114: case 128:
    case 134: case 135: case 136: case 156: case 163: case 164:
    case 180: case 203: /* stairs */
    case 54:  /* chest */
    case 60:  /* farmland */
    case 64: case 71: case 193: case 194: case 195: case 196: case 197:
    case 65:  /* ladder */
    case 96: case 167: /* trapdoor */
    case 107: case 183: case 184: case 185: case 186: case 187:
    case 139: /* cobble wall */
    case 145: /* anvil */
        return 0;
    default:
        return 1;
    }
}

/* Material.isSolid && !isLiquid. Air/plants are REPLACEABLE. */
MC_HD static inline int bed_material_solid(int id) {
    BptProps p;
    if (id <= 0) return 0;
    p = mc_bpt_props(id);
    if (p.flags & BF_LIQUID) return 0;
    if (p.flags & BF_REPLACEABLE) return 0;
    return (p.flags & BF_SOLID) != 0;
}

/* Block.canSpawnInBlock (Block.java:937-940): !solid && !liquid. */
MC_HD static inline int bed_can_spawn_in(int id) {
    BptProps p;
    if (id < 0) return 0;
    p = mc_bpt_props(id);
    if (p.flags & BF_LIQUID) return 0;
    if ((p.flags & BF_SOLID) && !(p.flags & BF_REPLACEABLE)) return 0;
    return 1;
}

/* BlockBed.hasRoomForPlayer :231-234. */
MC_HD static inline int bed_has_room_ids(int down_id, int here_id, int up_id) {
    return bed_fully_opaque(down_id) && !bed_material_solid(here_id) &&
           !bed_material_solid(up_id);
}

MC_HD static inline int bed_has_room(const Chunk *w, int x, int y, int z) {
    return bed_has_room_ids(psv_get_block(w, x, y - 1, z),
                            psv_get_block(w, x, y, z),
                            psv_get_block(w, x, y + 1, z));
}

/* BlockBed.getSafeExitLocation :195-228. tries=0 from getBedSpawnPosition. */
MC_HD static inline int bed_safe_exit(const Chunk *w, int x, int y, int z,
                                     int facing, int tries,
                                     int *ox, int *oy, int *oz) {
    int l, i1, j1, k1, l1, i2, j2;
    int dx = bed_facing_dx(facing);
    int dz = bed_facing_dz(facing);
    for (l = 0; l <= 1; ++l) {
        i1 = x - dx * l - 1;
        j1 = z - dz * l - 1;
        k1 = i1 + 2;
        l1 = j1 + 2;
        for (i2 = i1; i2 <= k1; ++i2) {
            for (j2 = j1; j2 <= l1; ++j2) {
                if (bed_has_room(w, i2, y, j2)) {
                    if (tries <= 0) {
                        *ox = i2;
                        *oy = y;
                        *oz = j2;
                        return 1;
                    }
                    --tries;
                }
            }
        }
    }
    return 0;
}

/* EntityPlayer.getBedSpawnLocation :1779-1800. force = spawnForced. */
MC_HD static inline int bed_spawn_location(const Chunk *w, int bx, int by,
                                           int bz, int force,
                                           int *ox, int *oy, int *oz) {
    int id = psv_get_block(w, bx, by, bz);
    int meta, facing;
    if (id != BED_BLK) {
        if (!force) return 0;
        if (bed_can_spawn_in(id) &&
            bed_can_spawn_in(psv_get_block(w, bx, by + 1, bz))) {
            *ox = bx;
            *oy = by;
            *oz = bz;
            return 1;
        }
        return 0;
    }
    meta = psv_get_meta(w, bx, by, bz);
    facing = bed_facing_meta(meta);
    return bed_safe_exit(w, bx, by, bz, facing, 0, ox, oy, oz);
}

/* Wake / respawn feet: PlayerList.java:574 and EntityPlayer.java:1748.
 * (float)x + 0.5F, (float)y + 0.1F, (float)z + 0.5F. */
MC_HD static inline void bed_exit_pose(int x, int y, int z,
                                       double *px, double *py, double *pz) {
    *px = (double)((float)x + 0.5f);
    *py = (double)((float)y + 0.1f);
    *pz = (double)((float)z + 0.5f);
}

/* EntityPlayer.trySleep setSize(0.2F, 0.2F) :1680. */
MC_HD static inline McAABB bed_sleep_box(double px, double py, double pz) {
    double hw = (double)(0.2f * 0.5f);
    double hh = (double)0.2f;
    return mc_aabb_make(px - hw, py, pz - hw, px + hw, py + hh, pz + hw);
}

/* ItemBed.onItemUse cells after UP + replaceable/up rewrite.
 * Java :61: (replaceable||air) both halves, isFullyOpaque below both. */
MC_HD static inline int bed_item_can_place(const Chunk *w, int fx, int fy,
                                           int fz, int facing) {
    int hx = fx + bed_facing_dx(facing);
    int hz = fz + bed_facing_dz(facing);
    if (!ibp_is_replaceable(psv_get_block(w, fx, fy, fz))) return 0;
    if (!ibp_is_replaceable(psv_get_block(w, hx, fy, hz))) return 0;
    if (!bed_fully_opaque(psv_get_block(w, fx, fy - 1, fz))) return 0;
    if (!bed_fully_opaque(psv_get_block(w, hx, fy - 1, hz))) return 0;
    return 1;
}

/* EntityPlayer.trySleep pose :1685-1688. XZ 0.5F + facingOffset * 0.4F;
 * Y (float)y + 0.6875F (= 0.5 + 0.1875). Float casts are the Java setPosition. */
MC_HD static inline void bed_sleep_pose(int bx, int by, int bz, int facing,
                                        double *px, double *py, double *pz) {
    float f1 = 0.5f + (float)bed_facing_dx(facing) * 0.4f;
    float f = 0.5f + (float)bed_facing_dz(facing) * 0.4f;
    *px = (double)((float)bx + f1);
    *py = (double)((float)by + 0.6875f);
    *pz = (double)((float)bz + f);
}

/* EntityPlayer.bedInRange :1710-1720. Head or foot, |dx|<=3 |dy|<=2 |dz|<=3. */
MC_HD static inline int bed_in_range(double px, double py, double pz,
                                     int bx, int by, int bz, int facing) {
    double dx, dy, dz;
    int fx, fz;
    dx = px - (double)bx;
    dy = py - (double)by;
    dz = pz - (double)bz;
    if (dx < 0.0) dx = -dx;
    if (dy < 0.0) dy = -dy;
    if (dz < 0.0) dz = -dz;
    if (dx <= 3.0 && dy <= 2.0 && dz <= 3.0) return 1;
    fx = bx + bed_facing_dx(facing) * -1;
    fz = bz + bed_facing_dz(facing) * -1;
    dx = px - (double)fx;
    dy = py - (double)by;
    dz = pz - (double)fz;
    if (dx < 0.0) dx = -dx;
    if (dy < 0.0) dy = -dy;
    if (dz < 0.0) dz = -dz;
    return dx <= 3.0 && dy <= 2.0 && dz <= 3.0;
}

/* EntityMob AABB vs bed box :1665-1667 radius 8.0 / 5.0 / 8.0. */
MC_HD static inline int bed_mob_hits_sleep_box(double mx, double my, double mz,
                                               float width, float height,
                                               int bx, int by, int bz) {
    double hw = (double)width * 0.5;
    double qminx = (double)bx - 8.0;
    double qminy = (double)by - 5.0;
    double qminz = (double)bz - 8.0;
    double qmaxx = (double)bx + 8.0;
    double qmaxy = (double)by + 5.0;
    double qmaxz = (double)bz + 8.0;
    double eminX = mx - hw, emaxX = mx + hw;
    double eminY = my, emaxY = my + (double)height;
    double eminZ = mz - hw, emaxZ = mz + hw;
    return eminX < qmaxx && emaxX > qminx && eminY < qmaxy && emaxY > qminy &&
           eminZ < qmaxz && emaxZ > qminz;
}

/* WorldProvider.isDaytime :450-453 getSkylightSubtracted < 4.
 * Formula matches hs_skylight_sub (rain=thunder=0). */
MC_HD static inline int bed_skylight_sub(i64 world_time) {
    i32 i;
    float f, f1, ang;
    i = (i32)(world_time % 24000LL);
    if (i < 0) i += 24000;
    f = ((float)i + 1.0f) / 24000.0f - 0.25f;
    if (f < 0.0f) f += 1.0f;
    if (f > 1.0f) f -= 1.0f;
    f1 = 1.0f - (float)((cos((double)f * MC_PI) + 1.0) / 2.0);
    ang = f + (f1 - f) / 3.0f;
    f1 = 1.0f - (float)(cos((double)ang * (double)MC_PI * 2.0) * 2.0 + 0.5);
    if (f1 < 0.0f) f1 = 0.0f;
    if (f1 > 1.0f) f1 = 1.0f;
    f1 = 1.0f - f1;
    f1 = 1.0f - f1;
    return (int)(f1 * 11.0f);
}
MC_HD static inline int bed_is_daytime_time(i64 world_time) {
    return bed_skylight_sub(world_time) < 4;
}

/* WorldServer.java:195-196. Same formula as ww_next_dawn. */
MC_HD static inline i64 bed_time_skip(i64 world_time) {
    return ww_next_dawn(world_time);
}

MC_HD static inline int bed_fully_asleep(const BedSleep *p) {
    return p->sleeping && p->sleep_timer >= 100;
}

MC_HD static inline void bed_wake(BedSleep *p, int immediately, int set_spawn) {
    p->sleeping = 0;
    p->sleep_timer = immediately ? 0 : 100;
    if (set_spawn) {
        p->spawn_set = 1;
        p->spawn_x = p->bed_x;
        p->spawn_y = p->bed_y;
        p->spawn_z = p->bed_z;
        p->spawn_forced = 0;
    }
}

/* trySleep server checks in Java order :1645-1672, then pose :1680-1700. */
MC_HD static inline int bed_try_sleep(BedSleep *p, int bx, int by, int bz,
                                      int facing, int alive, int is_surface,
                                      int is_daytime, int mob_near) {
    if (p->sleeping || !alive) return BED_OTHER_PROBLEM;
    if (!is_surface) return BED_NOT_POSSIBLE_HERE;
    if (is_daytime) return BED_NOT_POSSIBLE_NOW;
    if (!bed_in_range(p->px, p->py, p->pz, bx, by, bz, facing))
        return BED_TOO_FAR_AWAY;
    if (mob_near) return BED_NOT_SAFE;
    p->bed_x = bx;
    p->bed_y = by;
    p->bed_z = bz;
    bed_sleep_pose(bx, by, bz, facing, &p->px, &p->py, &p->pz);
    p->sleeping = 1;
    p->sleep_timer = 0;
    p->mx = 0.0;
    p->my = 0.0;
    p->mz = 0.0;
    return BED_OK;
}

/* EntityPlayer.onUpdate sleep branch :228-257. Caller runs this AFTER
 * WorldServer.tick (areAllPlayersAsleep), matching Java updateEntities. */
MC_HD static inline void bed_player_on_update(BedSleep *p, int bed_still_there,
                                              int is_daytime) {
    if (p->sleeping) {
        ++p->sleep_timer;
        if (p->sleep_timer > 100) p->sleep_timer = 100;
        if (!bed_still_there)
            bed_wake(p, 1, 0);
        else if (is_daytime)
            bed_wake(p, 0, 1);
    } else if (p->sleep_timer > 0) {
        ++p->sleep_timer;
        if (p->sleep_timer >= 110) p->sleep_timer = 0;
    }
}

#endif /* MC_PLAYER_BED_H */
