/* entity_xp_orb: EntityXPOrb.onUpdate physics + player attraction (MC 1.11.2
 * net/minecraft/entity/item/EntityXPOrb.java), a bit-faithful C port.
 *
 * Verified vs the live Java game (verify/entity_trace xp-orb scenario, item_trace_verify.c):
 * per-tick posX/Y/Z + motionX/Y/Z on raw Double.doubleToRawLongBits bits, up to the pickup event.
 *
 * RAND-FREEDOM: onUpdate's motion path draws NO rand for a sealed dry arena - rand appears only in
 * the LAVA branch (absent) and Entity.pushOutOfBlocks (the orb rests ON a block; collidesWithAnyBlock
 * is false via the strict-'<' intersectsWith, so no draw). Attraction reads the player's position
 * deterministically. => orb motion is fully deterministic given (summon state, player pos, entity id).
 *
 * ATTRACTION GATE (motion-relevant, must be verbatim): closestPlayer starts null -> the orb is in
 * pure free-fall until the color gate first fires:
 *     xpTargetColor < xpColor - 20 + getEntityId() % 100
 * With both color counters starting at 0, the first acquisition tick is n > 21 - (eid % 100). So the
 * length of the initial free-fall depends on the world-assigned entity id; the real eid is fed into
 * the C sim (eo_tick's `eid`) rather than assumed. Once a player within 8 blocks is acquired it
 * persists (re-acquired only when the gate re-fires AND the held player is now > 8 away).
 *
 * CONSTANTS (read exactly off the oracle): gravity 0.029999999329447746 = (double)0.03f;
 * horizontal/air drag 0.98f; vertical drag 0.9800000190734863 = (double)0.98f; ground bounce
 * -0.8999999761581421 = -(double)0.9f; attraction pull scale 0.1; range 8.0.
 *
 * TRIMMED (no motion bearing): xpColor render tint is kept only as the gate counter; getTextureByXP
 * / getBrightnessForRender (render-only); the Mending-enchant durability drain + addExperience on
 * pickup (pickup is a boundary event, verified by disappearance tick, not per-tick motion). */
#ifndef MC_ENTITY_XP_ORB_H
#define MC_ENTITY_XP_ORB_H

#include "mc.h"
#include "mc_world.h"
#include "mc_blocks.h"
#include "mc_math.h"
#include <math.h>
#include "physics_collision_math.h"

typedef struct {
    double posX, posY, posZ;
    double motionX, motionY, motionZ;
    McAABB box;
    int onGround;
    int xpOrbAge;
    int delayBeforeCanPickup;
    int xpColor;
    int xpTargetColor;
    int xpValue;
    int health;
    int eid;
    float yaw;
    int has_closest;   /* closestPlayer != null */
    int dead;
} McOrb;

/* Block.getSlipperiness: ice family 0.98F, else 0.6F. */
MC_HD static inline float eo_slipperiness(u16 under_state) {
    int id = mc_state_id(under_state);
    if (id == BLK_ICE || id == 174 || id == 212) return 0.98f;
    return 0.6f;
}

/* setSize(0.5,0.5) + setPosition: box centered on (x,z), feet at posY, height 0.5. */
MC_HD static inline void eo_set_position(McOrb *o, double x, double y, double z) {
    o->posX = x; o->posY = y; o->posZ = z;
    float f = 0.5f / 2.0f, f1 = 0.5f;
    o->box = mc_aabb_make(x - (double)f, y, z - (double)f,
                          x + (double)f, y + (double)f1, z + (double)f);
}

/* EntityPlayer.getDistanceSq(x,y,z): (px-x)^2 + (py-y)^2 + (pz-z)^2. */
MC_HD static inline double eo_player_distsq(double px, double py, double pz,
                                            double x, double y, double z) {
    double dx = px - x, dy = py - y, dz = pz - z;
    return dx * dx + dy * dy + dz * dz;
}

MC_HD static inline void eo_move(McOrb *o, double dx, double dy, double dz,
                                 const McAABB *blocks, int nblocks) {
    McEntity e;
    e.box = o->box;
    e.posX = o->posX; e.posY = o->posY; e.posZ = o->posZ;
    e.motionX = o->motionX; e.motionY = o->motionY; e.motionZ = o->motionZ;
    e.onGround = o->onGround;
    e.collidedHorizontally = e.collidedVertically = e.isCollided = 0;
    mc_entity_move(&e, dx, dy, dz, blocks, nblocks);
    o->box = e.box;
    o->posX = e.posX; o->posY = e.posY; o->posZ = e.posZ;
    o->motionX = e.motionX; o->motionY = e.motionY; o->motionZ = e.motionZ;
    o->onGround = e.onGround;
}

/* One EntityXPOrb.onUpdate tick. Player state (px,py,pz,eye,spectator) is constant while the player
 * is parked; eye is EntityPlayer.getEyeHeight() (float). colliding_push must be 0 (see header). */
MC_HD static inline void eo_tick(McOrb *o, double px, double py, double pz, float eye,
                                 int spectator, const McAABB *blocks, int nblocks,
                                 u16 under_state, int colliding_push) {
    float f;
    (void)colliding_push;   /* pushOutOfBlocks inert (no collide -> no rand) */

    if (o->delayBeforeCanPickup > 0)
        --o->delayBeforeCanPickup;

    o->motionY -= (double)0.029999999329447746;   /* (double)0.03f */

    /* lava branch CUT (no lava). pushOutOfBlocks: inert. */

    if (o->xpTargetColor < o->xpColor - 20 + o->eid % 100) {
        if (!o->has_closest ||
            eo_player_distsq(px, py, pz, o->posX, o->posY, o->posZ) > 64.0) {
            /* getClosestPlayerToEntity(8.0): the single player if within 8 blocks & not spectating */
            o->has_closest = (!spectator &&
                eo_player_distsq(px, py, pz, o->posX, o->posY, o->posZ) < 64.0) ? 1 : 0;
        }
        o->xpTargetColor = o->xpColor;
    }

    if (o->has_closest && spectator)
        o->has_closest = 0;

    if (o->has_closest) {
        double d1 = (px - o->posX) / 8.0;
        double d2 = (py + (double)eye / 2.0 - o->posY) / 8.0;
        double d3 = (pz - o->posZ) / 8.0;
        double d4 = sqrt(d1 * d1 + d2 * d2 + d3 * d3);
        double d5 = 1.0 - d4;
        if (d5 > 0.0) {
            d5 = d5 * d5;
            o->motionX += d1 / d4 * d5 * 0.1;
            o->motionY += d2 / d4 * d5 * 0.1;
            o->motionZ += d3 / d4 * d5 * 0.1;
        }
    }

    eo_move(o, o->motionX, o->motionY, o->motionZ, blocks, nblocks);

    f = 0.98f;
    if (o->onGround)
        f = eo_slipperiness(under_state) * 0.98f;
    o->motionX *= (double)f;
    o->motionY *= 0.9800000190734863;   /* (double)0.98f */
    o->motionZ *= (double)f;
    if (o->onGround)
        o->motionY *= -0.8999999761581421;   /* -(double)0.9f */

    ++o->xpColor;
    ++o->xpOrbAge;
    if (o->xpOrbAge >= 6000)
        o->dead = 1;
}

#endif /* MC_ENTITY_XP_ORB_H */
