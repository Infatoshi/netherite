/* living_base.h - EntityLivingBase tick spine for a mob WITHOUT faithful AI (PORT_MATRIX P2).
 *
 * Faithful C port of the generic living-entity per-tick chain: gravity + drag + swept-AABB
 * collision, with the 1.11.2 isServerWorld() NoAI gate. The AI decision (updateEntityActionState)
 * is EXTERNAL: the caller writes moveForward / moveStrafing / isJumping / rotationYaw onto the
 * EbLiving before eb_tick_living, exactly where EntityLiving.updateEntityActionState would set
 * them. Passing intents by value keeps this CUDA-safe (no function pointers).
 *
 * Oracle line ranges ported (java/oracle-src/net/minecraft/entity/EntityLivingBase.java, 1.11.2):
 *   jump               1905-1921  (motionY=0.42; sprint yaw kick)
 *   getJumpUpwardsMotion 1897-1900 (0.42F)
 *   isMovementBlocked  1795-1798  (getHealth()<=0)  -> passed in by caller
 *   moveEntityWithHeading 2015-2103 (non-water/lava/non-elytra/non-ladder branch)
 *   onLivingUpdate     2419-2511  (jumpTicks, isServerWorld drag, 0.003 clamp, AI, jump, travel)
 * EntityLiving.isServerWorld (1537): super.isServerWorld() && !isAIDisabled() -> the NoAI gate.
 *
 * NoAI (isServerWorld==0): moveEntityWithHeading's whole body is gated off (frozen), and
 * onLivingUpdate takes the `else if (!isServerWorld)` motion*=0.98 branch. This replicates the
 * measured 1.11.2 behavior that a NoAI mob does NOT fall server-side (verify/entity_trace
 * RESULTS.md). No current golden exercises this branch; the drop runs with isServerWorld==1.
 */
#ifndef MC_LIVING_BASE_H
#define MC_LIVING_BASE_H

#include "mc.h"
#include "mc_math.h"
#include "entity_base.h"

typedef struct {
    EntBody base;
    /* AI-driven intents (set by the external updateEntityActionState slot each tick) */
    float moveForward, moveStrafing, randomYawVelocity;
    int   isJumping;
    /* movement constants */
    float jumpMovementFactor;   /* 0.02 default (air control) */
    float landMovementFactor;   /* getAIMoveSpeed(); zombie 0.23 */
    int   jumpTicks;
    int   isSprinting;
    int   isServerWorld;        /* super.isServerWorld() && !isAIDisabled(): NoAI gate */
    int   onLadder;             /* EntityLivingBase.isOnLadder; spider = besideClimbableBlock */
} EbLiving;

/* EntityLivingBase.jump (1905-1921). getJumpUpwardsMotion()=0.42F; JUMP_BOOST omitted. */
MC_HD static inline void elb_jump(EbLiving *e, const McSinTable *st) {
    e->base.phys.motionY = 0.41999998688697815; /* (double)0.42F */
    if (e->isSprinting) {
        float f = e->base.rotationYaw * 0.017453292F;
        e->base.phys.motionX -= (double)(mc_sin(st, f) * 0.2F);
        e->base.phys.motionZ += (double)(mc_cos(st, f) * 0.2F);
    }
}

/* EntityLivingBase.moveEntityWithHeading (2015-2103): the on-land/in-air (non-fluid,
 * non-elytra, non-ladder, non-levitation) travel branch. `ground_slip` is the raw block
 * slipperiness under the feet (0.6 default, 0.98 ice) - read only when onGround. Gated on
 * isServerWorld (canPassengerSteer is false for a standalone mob). */
MC_HD static inline void elb_move_with_heading(EbLiving *e, float strafe, float forward,
                                               float ground_slip, const PcfBlock *blocks,
                                               int nblocks, const McSinTable *st) {
    if (!e->isServerWorld) return;   /* frozen: NoAI / not steered */

    int onGround = e->base.phys.onGround;
    float f6 = onGround ? (ground_slip * 0.91F) : 0.91F;
    float f7 = 0.16277136F / (f6 * f6 * f6);
    float f8 = onGround ? (e->landMovementFactor * f7) : e->jumpMovementFactor;

    eb_move_relative(&e->base, strafe, forward, f8, st);

    /* f6 re-read after moveRelative but before move(): posX/posZ unchanged -> same block. */
    f6 = onGround ? (ground_slip * 0.91F) : 0.91F;

    /* EntityLivingBase.moveEntityWithHeading isOnLadder clamp
     * EntityLivingBase.java:2047-2067. Spider isOnLadder is
     * isBesideClimbableBlock (EntitySpider.java:137-140). */
    if (e->onLadder) {
        e->base.phys.motionX = pcf_clampd(e->base.phys.motionX,
                                          -0.15000000596046448,
                                          0.15000000596046448);
        e->base.phys.motionZ = pcf_clampd(e->base.phys.motionZ,
                                          -0.15000000596046448,
                                          0.15000000596046448);
        e->base.fallDistance = 0.0F;
        if (e->base.phys.motionY < -0.15)
            e->base.phys.motionY = -0.15;
    }

    eb_move(&e->base, e->base.phys.motionX, e->base.phys.motionY, e->base.phys.motionZ,
            blocks, nblocks);

    /* EntityLivingBase.java:2069-2071: collidedHorizontally && isOnLadder -> motionY=0.2D */
    if (e->base.phys.collidedHorizontally && e->onLadder)
        e->base.phys.motionY = 0.2;

    /* no levitation potion: apply gravity then drag.
     * EbLiving / living_base.h carry no PotionEffect list
     * (EntitySpider.GroupData HARD roll is consumed, not applied). */
    if (!e->base.hasNoGravity)
        e->base.phys.motionY -= 0.08;
    e->base.phys.motionY *= 0.9800000190734863;
    e->base.phys.motionX *= (double)f6;
    e->base.phys.motionZ *= (double)f6;
}

/* EntityLivingBase.onLivingUpdate (2419-2511). AI intents (moveForward/moveStrafing/isJumping/
 * rotationYaw) are assumed already set by the caller at the updateEntityActionState point when
 * (isServerWorld && !isMovementBlocked). Order matches the oracle: jumpTicks--, NoAI drag,
 * 0.003 clamp, movement-blocked zeroing, jump, moveStrafing/moveForward*=0.98, travel. */
MC_HD static inline void elb_on_living_update(EbLiving *e, float ground_slip,
                                              int isMovementBlocked, const PcfBlock *blocks,
                                              int nblocks, const McSinTable *st) {
    if (e->jumpTicks > 0) --e->jumpTicks;

    /* newPosRotationIncrements path is client interp (not server) -> else-if branch. */
    if (!e->isServerWorld) {
        e->base.phys.motionX *= 0.98;
        e->base.phys.motionY *= 0.98;
        e->base.phys.motionZ *= 0.98;
    }

    if (fabs(e->base.phys.motionX) < 0.003) e->base.phys.motionX = 0.0;
    if (fabs(e->base.phys.motionY) < 0.003) e->base.phys.motionY = 0.0;
    if (fabs(e->base.phys.motionZ) < 0.003) e->base.phys.motionZ = 0.0;

    if (isMovementBlocked) {
        e->isJumping = 0;
        e->moveStrafing = 0.0F;
        e->moveForward = 0.0F;
        e->randomYawVelocity = 0.0F;
    }
    /* else if (isServerWorld) updateEntityActionState(): the AI slot the caller filled. */

    if (e->isJumping) {
        if (e->base.phys.onGround && e->jumpTicks == 0) {
            elb_jump(e, st);
            e->jumpTicks = 10;
        }
    } else {
        e->jumpTicks = 0;
    }

    e->moveStrafing *= 0.98F;
    e->moveForward *= 0.98F;
    e->randomYawVelocity *= 0.9F;
    elb_move_with_heading(e, e->moveStrafing, e->moveForward, ground_slip, blocks, nblocks, st);
}

/* Full mob tick: Entity.onUpdate -> onEntityUpdate (prev-pos) then
 * EntityLivingBase.onUpdate -> onLivingUpdate. One tick, no faithful AI. */
MC_HD static inline void eb_tick_living(EbLiving *e, float ground_slip, int isMovementBlocked,
                                        const PcfBlock *blocks, int nblocks,
                                        const McSinTable *st) {
    eb_on_entity_update(&e->base);
    elb_on_living_update(e, ground_slip, isMovementBlocked, blocks, nblocks, st);
    ++e->base.ticksExisted;
}

/* Convenience initializer: place a living entity of the given size at (x,y,z), at rest. */
MC_HD static inline void elb_init(EbLiving *e, float width, float height,
                                  double x, double y, double z) {
    pcf_init_entity(&e->base.phys);
    e->base.phys.isPlayer = 0;
    e->base.phys.isSneaking = 0;
    e->base.phys.stepHeight = 0.6f;   /* EntityLivingBase ctor (line 207): stepHeight=0.6F */
    e->base.width = (double)width;
    e->base.height = (double)height;
    e->base.rotationYaw = 0.0F;
    e->base.rotationPitch = 0.0F;
    e->base.fallDistance = 0.0F;
    e->base.hasNoGravity = 0;
    e->base.ticksExisted = 0;
    eb_set_position(&e->base, x, y, z);
    e->base.prevPosX = x; e->base.prevPosY = y; e->base.prevPosZ = z;
    e->moveForward = e->moveStrafing = e->randomYawVelocity = 0.0F;
    e->isJumping = 0;
    e->jumpMovementFactor = 0.02F;
    e->landMovementFactor = 0.23F;    /* zombie default AI move speed */
    e->jumpTicks = 0;
    e->isSprinting = 0;
    e->isServerWorld = 1;             /* AI-enabled mob on the server */
    e->onLadder = 0;
}

#endif /* MC_LIVING_BASE_H */
