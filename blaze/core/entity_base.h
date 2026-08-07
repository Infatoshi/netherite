/* entity_base.h - generic Entity tick spine (PORT_MATRIX P2).
 *
 * Faithful C port of the Entity-level tick primitives shared by every non-player entity,
 * SoA-compatible with mc_entity.h. The mechanical AABB-sweep body (Entity.move) is REUSED
 * verbatim from physics_collision_full.h (McPcfEntity / pcf_entity_move), which is the
 * 3-way Java==CPU==CUDA-verified collision core - this header does NOT reimplement it.
 *
 * Oracle line ranges ported (java/oracle-src/net/minecraft/entity/Entity.java, 1.11.2):
 *   setSize            376-399   (box recenter on width/height change)
 *   setPosition        413-424   (box = [x-w/2, y, z-w/2 .. x+w/2, y+h, z+w/2])
 *   moveRelative      1424-1445  (strafe/forward -> motion via sin/cos of rotationYaw)
 *   move               668-1056  (REUSED: physics_collision_full.h::pcf_entity_move)
 *   updateFallState   1214-1253  (onGround -> fallDistance=0; else y<0 -> fallDistance -= y)
 *   onEntityUpdate     460-...   (prevPosX/Y/Z <- posX/Y/Z at tick start; water/fire trimmed)
 *
 * CPU==CUDA one-source: header-only, MC_HD on every function, no malloc, no function pointers.
 */
#ifndef MC_ENTITY_BASE_H
#define MC_ENTITY_BASE_H

#include <math.h>
#include "mc.h"
#include "mc_math.h"                 /* McSinTable, mc_sin, mc_cos */
#include "physics_collision_math.h"  /* McAABB, mc_aabb_make */
#include "physics_collision_full.h"  /* McPcfEntity, pcf_entity_move, PcfBlock */

/* Entity-level state. `phys` carries the box + pos + motion + onGround/collided flags that
 * pcf_entity_move (Entity.move) reads and writes; the extra fields are the Entity members the
 * living tick chain needs (rotation, fall bookkeeping, prev-pos, size, gravity flag). */
typedef struct {
    McPcfEntity phys;        /* box/posX/Y/Z/motionX/Y/Z/onGround/collided* (Entity.move core) */
    double prevPosX, prevPosY, prevPosZ;
    float  rotationYaw, rotationPitch;
    float  fallDistance;
    double width, height;
    int    hasNoGravity;
    int    ticksExisted;
} EntBody;

/* Entity.setPosition (413-424): recompute the AABB around a new feet position. */
MC_HD static inline void eb_set_position(EntBody *e, double x, double y, double z) {
    double f = e->width / 2.0;
    e->phys.posX = x;
    e->phys.posY = y;
    e->phys.posZ = z;
    e->phys.box = mc_aabb_make(x - f, y, z - f, x + f, y + e->height, z + f);
}

/* Entity.setSize (376-399), shrink-only fast path (width unchanged for our mobs at spawn).
 * Recenters the box on the current feet position; the width>oldWidth re-collide branch is not
 * exercised by a fixed-size mob and is omitted (documented boundary). */
MC_HD static inline void eb_set_size(EntBody *e, float width, float height) {
    e->width = (double)width;
    e->height = (double)height;
    eb_set_position(e, e->phys.posX, e->phys.posY, e->phys.posZ);
}

/* Entity.moveRelative (1424-1445): apply strafe/forward acceleration in the yaw frame.
 * MathHelper.sqrt(f) == (float)Math.sqrt((double)f); sin/cos come from the shared table. */
MC_HD static inline void eb_move_relative(EntBody *e, float strafe, float forward,
                                          float friction, const McSinTable *st) {
    float f = strafe * strafe + forward * forward;
    if (f >= 1.0e-4F) {
        f = (float)sqrt((double)f);
        if (f < 1.0F) f = 1.0F;
        f = friction / f;
        strafe *= f;
        forward *= f;
        float f1 = mc_sin(st, e->rotationYaw * 0.017453292F);
        float f2 = mc_cos(st, e->rotationYaw * 0.017453292F);
        e->phys.motionX += (double)(strafe * f2 - forward * f1);
        e->phys.motionZ += (double)(forward * f2 + strafe * f1);
    }
}

/* Entity.updateFallState (1214-1253), the pos/onGround-relevant part. y = resolved move dy.
 * fallDistance is not in the verify compare set (health/damage not checked) - tracked for
 * structural faithfulness only. */
MC_HD static inline void eb_update_fall_state(EntBody *e, double y) {
    if (e->phys.onGround) {
        e->fallDistance = 0.0F;
    } else if (y < 0.0) {
        e->fallDistance = (float)((double)e->fallDistance - y);
    }
}

/* Entity.move wrapper: run the verified AABB sweep, then updateFallState on the resolved dy.
 * pcf_entity_move sets posX/Y/Z, onGround, collided* and zeroes blocked motion components. */
MC_HD static inline void eb_move(EntBody *e, double x, double y, double z,
                                 const PcfBlock *blocks, int nblocks) {
    double beforeY = e->phys.posY;
    pcf_entity_move(&e->phys, x, y, z, blocks, nblocks);
    eb_update_fall_state(e, e->phys.posY - beforeY);
}

/* Same Entity.move spine over already-resolved world collision AABBs. This
 * keeps state-aware block geometry out of the legacy baked PcfBlock path. */
MC_HD static inline void eb_move_aabb(
        EntBody *e, double x, double y, double z,
        const McAABB *blocks, int nblocks) {
    if (e->phys.isInWeb) {
        e->phys.isInWeb = 0;
        x *= 0.25;
        y *= 0.05000000074505806;
        z *= 0.25;
        e->phys.motionX = 0.0;
        e->phys.motionY = 0.0;
        e->phys.motionZ = 0.0;
    }
    McEntity exact;
    exact.box = e->phys.box;
    exact.posX = e->phys.posX;
    exact.posY = e->phys.posY;
    exact.posZ = e->phys.posZ;
    exact.motionX = e->phys.motionX;
    exact.motionY = e->phys.motionY;
    exact.motionZ = e->phys.motionZ;
    exact.onGround = e->phys.onGround;
    exact.collidedHorizontally = e->phys.collidedHorizontally;
    exact.collidedVertically = e->phys.collidedVertically;
    exact.isCollided = e->phys.isCollided;
    double beforeY = exact.posY;
    mc_entity_move_step(
        &exact, x, y, z, blocks, nblocks, e->phys.stepHeight);
    e->phys.box = exact.box;
    e->phys.posX = exact.posX;
    e->phys.posY = exact.posY;
    e->phys.posZ = exact.posZ;
    e->phys.motionX = exact.motionX;
    e->phys.motionY = exact.motionY;
    e->phys.motionZ = exact.motionZ;
    e->phys.onGround = exact.onGround;
    e->phys.collidedHorizontally = exact.collidedHorizontally;
    e->phys.collidedVertically = exact.collidedVertically;
    e->phys.isCollided = exact.isCollided;
    eb_update_fall_state(e, exact.posY - beforeY);
}

/* Entity.onEntityUpdate (460-...): snapshot prev-pos at the start of the tick. handleWater /
 * fire / air are trimmed (dry sealed arena; not part of the pos/motion compare set). */
MC_HD static inline void eb_on_entity_update(EntBody *e) {
    e->prevPosX = e->phys.posX;
    e->prevPosY = e->phys.posY;
    e->prevPosZ = e->phys.posZ;
}

#endif /* MC_ENTITY_BASE_H */
