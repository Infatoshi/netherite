/* boat_live.h - magma live EntityBoat.onUpdate subset.
 *
 * Magma magma/game/mob_live.c tick_boat and blaze-CPU/CUDA compile this
 * one source. Magma wrappers stay thin.
 *
 * Java 1.11.2 (java/oracle-src):
 *   EntityBoat.setSize 1.375F x 0.5625F     EntityBoat.java:75
 *   EntityBoat.getMountedYOffset -0.1D      EntityBoat.java:142-145
 *   EntityBoat.onUpdate                     EntityBoat.java:272-367
 *     getBoatStatus IN_WATER/ON_LAND/IN_AIR/UNDER_WATER/UNDER_FLOWING_WATER
 *       EntityBoat.java:400-427
 *     updateMotion gravity -0.03999999910593033D, IN_WATER momentum 0.9F
 *       buoyancy d2*0.06153846016296973 then *0.75
 *       EntityBoat.java:643-701
 *     controlBoat left/right deltaRotation, forward 0.04F, back -0.005F
 *       EntityBoat.java:704-748
 *     move(MoverType.SELF, motion)          EntityBoat.java:322
 *
 * Magma extras (M1 is magma semantics; do not "fix" to Java here):
 *   status is 3-way (IN_WATER/ON_LAND/IN_AIR); UNDER_* collapse to IN_WATER
 *     if feet/head sample id 8/9 (mob_live.c boat_status)
 *   land default boatGlide 0.8F when unset
 *   collision is 4-corner + foot/head solid samples, not Entity.move
 *   ridden player offset y+0.35 (not getMountedYOffset -0.1 + player YOffset)
 *   no paddlePositions / CPacketSteerBoat / lerp / outOfControlTicks
 *
 * Include after defining BL_BLOCK(w,x,y,z) for the world half.
 */
#ifndef MC_BOAT_LIVE_H
#define MC_BOAT_LIVE_H

#include <math.h>

#include "mc.h"
#include "mc_math.h"
#include "block_props_table.h"

#define BL_STATUS_IN_WATER 0
#define BL_STATUS_ON_LAND  1
#define BL_STATUS_IN_AIR   2
#define BL_WIDTH  1.375
#define BL_HEIGHT 0.5625
#define BL_HALF_W (1.375 * 0.5)
#define BL_GRAVITY -0.03999999910593033   /* EntityBoat.java:645 */
#define BL_BUOY    0.06153846016296973    /* EntityBoat.java:697 */
#define BL_RIDE_Y  0.35                   /* magma riding offset */

MC_HD static inline int bl_solid_id(int id) {
    BptProps p;
    if (id == 0) return 0;
    p = mc_bpt_props(id);
    return (p.flags & BF_SOLID) && !(p.flags & BF_LIQUID);
}

/* Magma boat_status: water in body/head cell => IN_WATER; solid below
 * empty feet => ON_LAND; else IN_AIR. */
MC_HD static inline int bl_status(int feet, int below, int head) {
    if (feet == 8 || feet == 9 || head == 8 || head == 9)
        return BL_STATUS_IN_WATER;
    if (bl_solid_id(below) && feet == 0) return BL_STATUS_ON_LAND;
    return BL_STATUS_IN_AIR;
}

typedef struct {
    double x, y, z;
    double vx, vy, vz;
    float yaw;
    int on_ground;
    float delta_rot;
    float glide;
} BlBoat;

MC_HD static inline void bl_tick(BlBoat *b, int status, int ridden,
                                 float forward, float strafe) {
    float momentum = 0.05f;
    double d1 = BL_GRAVITY;
    double d2 = 0.0;
    const double height = BL_HEIGHT;

    if (status == BL_STATUS_IN_WATER) {
        momentum = 0.9f;                                    /* java:663 */
        {
            int by = mc_floor(b->y);
            double water_level = (double)by + 1.0;
            d2 = (water_level - b->y) / height;
            if (d2 < 0.0) d2 = 0.0;
            if (d2 > 1.0) d2 = 1.0;
        }
    } else if (status == BL_STATUS_ON_LAND) {
        if (b->glide <= 0.0f) b->glide = 0.8f;
        momentum = b->glide;                                /* java:681 */
        if (ridden) b->glide *= 0.5f;                       /* java:685 */
    } else {
        momentum = 0.9f;                                    /* java:677 */
    }

    b->vx *= (double)momentum;                              /* java:689 */
    b->vz *= (double)momentum;
    b->delta_rot *= momentum;                               /* java:691 */
    b->vy += d1;                                            /* java:692 */
    if (d2 > 0.0) {
        b->vy += d2 * BL_BUOY;                              /* java:697 */
        b->vy *= 0.75;                                      /* java:699 */
    }

    if (ridden) {
        int left = strafe < -0.01f, right = strafe > 0.01f;
        int fwd = forward > 0.01f, back = forward < -0.01f;
        float f = 0.0f;
        if (left) b->delta_rot += -1.0f;                    /* java:712 */
        if (right) b->delta_rot += 1.0f;                    /* java:717 */
        if (left != right && !fwd && !back) f += 0.005f;
        b->yaw += b->delta_rot;                             /* java:722 */
        if (fwd) f += 0.04f;                                /* java:726 */
        if (back) f -= 0.005f;                              /* java:728 */
        {
            double yr = (double)b->yaw * 0.017453292;
            b->vx += -sin(yr) * (double)f;
            b->vz += cos(yr) * (double)f;
        }
    }
}

#endif /* MC_BOAT_LIVE_H */

#ifdef BL_W
#ifndef MC_BOAT_LIVE_TICK_H
#define MC_BOAT_LIVE_TICK_H
#ifndef BL_BLOCK
#error "boat_live.h tick requires BL_BLOCK(w,x,y,z)"
#endif

MC_HD static inline int bl_world_status(BL_W *w, double x, double y, double z) {
    int bx = mc_floor(x), by = mc_floor(y), bz = mc_floor(z);
    int feet = BL_BLOCK(w, bx, by, bz);
    int below = BL_BLOCK(w, bx, by - 1, bz);
    int head = BL_BLOCK(w, bx, mc_floor(y + BL_HEIGHT), bz);
    return bl_status(feet, below, head);
}

MC_HD static inline void bl_collide_move(BlBoat *b, BL_W *w) {
    const double half_w = BL_HALF_W;
    const double height = BL_HEIGHT;
    double try_x = b->x + b->vx;
    double try_z = b->z + b->vz;
    double try_y = b->y + b->vy;
    int blocked_xz = 0;
    int mid_y = mc_floor(b->y + height * 0.5);
    int c;
    double corners[4][2];
    corners[0][0] = try_x - half_w; corners[0][1] = try_z - half_w;
    corners[1][0] = try_x + half_w; corners[1][1] = try_z - half_w;
    corners[2][0] = try_x - half_w; corners[2][1] = try_z + half_w;
    corners[3][0] = try_x + half_w; corners[3][1] = try_z + half_w;
    for (c = 0; c < 4; ++c) {
        if (bl_solid_id(BL_BLOCK(w, mc_floor(corners[c][0]), mid_y,
                                 mc_floor(corners[c][1])))) {
            blocked_xz = 1;
            break;
        }
    }
    if (!blocked_xz) {
        b->x = try_x;
        b->z = try_z;
    } else {
        b->vx = b->vz = 0.0;
    }
    {
        int foot = mc_floor(try_y);
        int head = mc_floor(try_y + height);
        int bx = mc_floor(b->x), bz = mc_floor(b->z);
        if (!bl_solid_id(BL_BLOCK(w, bx, foot, bz)) &&
            !bl_solid_id(BL_BLOCK(w, bx, head, bz))) {
            b->y = try_y;
            b->on_ground = 0;
        } else if (b->vy < 0) {
            b->vy = 0;
            b->on_ground = 1;
            b->y = (double)foot + 1.0;
        } else {
            b->vy = 0;
        }
    }
}

MC_HD static inline int bl_tick_world(BlBoat *b, BL_W *w, int ridden,
                                      float forward, float strafe) {
    int status = bl_world_status(w, b->x, b->y, b->z);
    bl_tick(b, status, ridden, forward, strafe);
    bl_collide_move(b, w);
    return status;
}
#endif
#endif /* BL_W */
