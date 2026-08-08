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
#include "potion_throwable.h"

typedef struct {
    EntBody base;
    /* AI-driven intents (set by the external updateEntityActionState slot each tick) */
    float moveForward, moveStrafing, randomYawVelocity;
    int   isJumping;
    /* movement constants */
    float jumpMovementFactor;   /* 0.02 default (air control) */
    float landMovementFactor;   /* getAIMoveSpeed(); zombie 0.23 */
    int   jumpBoostAmplifier;   /* active MobEffects.JUMP_BOOST amp, or -1 */
    int   levitationAmplifier;  /* active MobEffects.LEVITATION amp, or -1 */
    int   jumpTicks;
    int   isSprinting;
    int   isServerWorld;        /* super.isServerWorld() && !isAIDisabled(): NoAI gate */
} EbLiving;

/* EntityLivingBase.jump (1905-1921). */
MC_HD static inline void elb_jump(EbLiving *e, const McSinTable *st) {
    e->base.phys.motionY = 0.41999998688697815; /* (double)0.42F */
    if (e->jumpBoostAmplifier >= 0)
        e->base.phys.motionY += (double)(
            (float)(e->jumpBoostAmplifier + 1) * 0.1F);
    if (e->isSprinting) {
        float f = e->base.rotationYaw * 0.017453292F;
        e->base.phys.motionX -= (double)(mc_sin(st, f) * 0.2F);
        e->base.phys.motionZ += (double)(mc_cos(st, f) * 0.2F);
    }
}

/* Entity.doBlockCollisions runs inside Entity.move, after the final AABB is
 * resolved but before EntityLivingBase applies gravity and drag. The caller
 * supplies bounded {x,y,z,id} contact cells in Java's x/y/z order. */
MC_HD static inline void elb_apply_block_contacts(
        EbLiving *e, const int (*cells)[4], int ncells) {
    if (!cells || ncells <= 0) return;
    const McAABB *box = &e->base.phys.box;
    int x0 = mc_floor(box->minX + 0.001);
    int x1 = mc_floor(box->maxX - 0.001);
    int y0 = mc_floor(box->minY + 0.001);
    int y1 = mc_floor(box->maxY - 0.001);
    int z0 = mc_floor(box->minZ + 0.001);
    int z1 = mc_floor(box->maxZ - 0.001);
    for (int i = 0; i < ncells; ++i) {
        if (cells[i][0] < x0 || cells[i][0] > x1
                || cells[i][1] < y0 || cells[i][1] > y1
                || cells[i][2] < z0 || cells[i][2] > z1)
            continue;
        if (cells[i][3] == 30) {
            e->base.phys.isInWeb = 1;
            e->base.fallDistance = 0.0F;
        } else if (cells[i][3] == 88) {
            e->base.phys.motionX *= 0.4;
            e->base.phys.motionZ *= 0.4;
        }
    }
}

/* Default Forge ladder selection inspects only the cell containing the
 * living entity's feet, not every cell overlapped by its AABB. */
MC_HD static inline int elb_is_on_ladder_contacts(
        const EbLiving *e, const int (*cells)[4], int ncells) {
    if (!cells || ncells <= 0) return 0;
    int x = mc_floor(e->base.phys.posX);
    int y = mc_floor(e->base.phys.box.minY);
    int z = mc_floor(e->base.phys.posZ);
    for (int i = 0; i < ncells; ++i)
        if (cells[i][0] == x && cells[i][1] == y && cells[i][2] == z
                && cells[i][3] == 65)
            return 1;
    return 0;
}

/* Entity.move selects landing callbacks from the block 0.2 below the final
 * feet position. The exact AABB sweep has already zeroed blocked Y motion, so
 * the caller supplies the pre-sweep value observed by Block.onLanded. */
MC_HD static inline void elb_apply_landing_contact(
        EbLiving *e, double landing_motion_y,
        const int (*cells)[4], int ncells) {
    if (!e->base.phys.collidedVertically || !cells || ncells <= 0) return;
    int x = mc_floor(e->base.phys.posX);
    int y = mc_floor(e->base.phys.posY - 0.20000000298023224);
    int z = mc_floor(e->base.phys.posZ);
    for (int i = 0; i < ncells; ++i) {
        if (cells[i][0] != x || cells[i][1] != y || cells[i][2] != z)
            continue;
        if (cells[i][3] == 165 && !e->base.phys.isSneaking) {
            if (landing_motion_y < 0.0)
                e->base.phys.motionY = -landing_motion_y;
            if (e->base.phys.onGround
                    && fabs(e->base.phys.motionY) < 0.1) {
                double damping = 0.4
                    + fabs(e->base.phys.motionY) * 0.2;
                e->base.phys.motionX *= damping;
                e->base.phys.motionZ *= damping;
            }
        }
        return;
    }
}

/* EntityLivingBase.moveEntityWithHeading (2015-2103): the on-land/in-air (non-fluid,
 * non-elytra, non-ladder, non-levitation) travel branch. `ground_slip` is the raw block
 * slipperiness under the feet (0.6 default, 0.98 ice) - read only when onGround. Gated on
 * isServerWorld (canPassengerSteer is false for a standalone mob). */
MC_HD static inline void elb_move_with_heading_collision(
        EbLiving *e, float strafe, float forward, float ground_slip,
        const PcfBlock *pcf_blocks, int npcf,
        const McAABB *aabb_blocks, int naabb, int use_aabb,
        const int (*contact_cells)[4], int ncontacts,
        const McSinTable *st) {
    if (!e->isServerWorld) return;   /* frozen: NoAI / not steered */

    int onGround = e->base.phys.onGround;
    float f6 = onGround ? (ground_slip * 0.91F) : 0.91F;
    float f7 = 0.16277136F / (f6 * f6 * f6);
    float f8 = onGround ? (e->landMovementFactor * f7) : e->jumpMovementFactor;

    eb_move_relative(&e->base, strafe, forward, f8, st);

    if (elb_is_on_ladder_contacts(e, contact_cells, ncontacts)) {
        const double ladder_speed = 0.15000000596046448;
        if (e->base.phys.motionX < -ladder_speed)
            e->base.phys.motionX = -ladder_speed;
        if (e->base.phys.motionX > ladder_speed)
            e->base.phys.motionX = ladder_speed;
        if (e->base.phys.motionZ < -ladder_speed)
            e->base.phys.motionZ = -ladder_speed;
        if (e->base.phys.motionZ > ladder_speed)
            e->base.phys.motionZ = ladder_speed;
        e->base.fallDistance = 0.0F;
        if (e->base.phys.motionY < -0.15)
            e->base.phys.motionY = -0.15;
    }

    /* f6 re-read after moveRelative but before move(): posX/posZ unchanged -> same block. */
    f6 = onGround ? (ground_slip * 0.91F) : 0.91F;

    double landing_motion_y = e->base.phys.isInWeb
        ? 0.0 : e->base.phys.motionY;
    if (use_aabb)
        eb_move_aabb(
            &e->base, e->base.phys.motionX, e->base.phys.motionY,
            e->base.phys.motionZ, aabb_blocks, naabb);
    else
        eb_move(
            &e->base, e->base.phys.motionX, e->base.phys.motionY,
            e->base.phys.motionZ, pcf_blocks, npcf);

    elb_apply_landing_contact(
        e, landing_motion_y, contact_cells, ncontacts);
    elb_apply_block_contacts(e, contact_cells, ncontacts);

    if (e->base.phys.collidedHorizontally
            && elb_is_on_ladder_contacts(e, contact_cells, ncontacts))
        e->base.phys.motionY = 0.2;

    if (e->levitationAmplifier >= 0)
        e->base.phys.motionY = pt_effect_levitation_motion(
            e->base.phys.motionY, e->levitationAmplifier);
    else {
        if (!e->base.hasNoGravity)
            e->base.phys.motionY -= 0.08;
        e->base.phys.motionY *= 0.9800000190734863;
    }
    e->base.phys.motionX *= (double)f6;
    e->base.phys.motionZ *= (double)f6;
}

MC_HD static inline void elb_move_with_heading(
        EbLiving *e, float strafe, float forward, float ground_slip,
        const PcfBlock *blocks, int nblocks, const McSinTable *st) {
    elb_move_with_heading_collision(
        e, strafe, forward, ground_slip,
        blocks, nblocks, NULL, 0, 0, NULL, 0, st);
}

MC_HD static inline void elb_move_with_heading_aabb(
        EbLiving *e, float strafe, float forward, float ground_slip,
        const McAABB *blocks, int nblocks, const McSinTable *st) {
    elb_move_with_heading_collision(
        e, strafe, forward, ground_slip,
        NULL, 0, blocks, nblocks, 1, NULL, 0, st);
}

MC_HD static inline void elb_move_with_heading_aabb_contacts(
        EbLiving *e, float strafe, float forward, float ground_slip,
        const McAABB *blocks, int nblocks,
        const int (*contact_cells)[4], int ncontacts,
        const McSinTable *st) {
    elb_move_with_heading_collision(
        e, strafe, forward, ground_slip,
        NULL, 0, blocks, nblocks, 1,
        contact_cells, ncontacts, st);
}

/* EntityLivingBase.onLivingUpdate (2419-2511). AI intents (moveForward/moveStrafing/isJumping/
 * rotationYaw) are assumed already set by the caller at the updateEntityActionState point when
 * (isServerWorld && !isMovementBlocked). Order matches the oracle: jumpTicks--, NoAI drag,
 * 0.003 clamp, movement-blocked zeroing, jump, moveStrafing/moveForward*=0.98, travel. */
MC_HD static inline void elb_on_living_update_collision(
        EbLiving *e, float ground_slip, int isMovementBlocked,
        const PcfBlock *pcf_blocks, int npcf,
        const McAABB *aabb_blocks, int naabb, int use_aabb,
        const int (*contact_cells)[4], int ncontacts,
        const McSinTable *st) {
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
    elb_move_with_heading_collision(
        e, e->moveStrafing, e->moveForward, ground_slip,
        pcf_blocks, npcf, aabb_blocks, naabb, use_aabb,
        contact_cells, ncontacts, st);
}

MC_HD static inline void elb_on_living_update(
        EbLiving *e, float ground_slip, int isMovementBlocked,
        const PcfBlock *blocks, int nblocks, const McSinTable *st) {
    elb_on_living_update_collision(
        e, ground_slip, isMovementBlocked,
        blocks, nblocks, NULL, 0, 0, NULL, 0, st);
}

MC_HD static inline void elb_on_living_update_aabb(
        EbLiving *e, float ground_slip, int isMovementBlocked,
        const McAABB *blocks, int nblocks, const McSinTable *st) {
    elb_on_living_update_collision(
        e, ground_slip, isMovementBlocked,
        NULL, 0, blocks, nblocks, 1, NULL, 0, st);
}

MC_HD static inline void elb_on_living_update_aabb_contacts(
        EbLiving *e, float ground_slip, int isMovementBlocked,
        const McAABB *blocks, int nblocks,
        const int (*contact_cells)[4], int ncontacts,
        const McSinTable *st) {
    elb_on_living_update_collision(
        e, ground_slip, isMovementBlocked,
        NULL, 0, blocks, nblocks, 1,
        contact_cells, ncontacts, st);
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

MC_HD static inline void eb_tick_living_aabb(
        EbLiving *e, float ground_slip, int isMovementBlocked,
        const McAABB *blocks, int nblocks, const McSinTable *st) {
    eb_on_entity_update(&e->base);
    elb_on_living_update_aabb(
        e, ground_slip, isMovementBlocked, blocks, nblocks, st);
    ++e->base.ticksExisted;
}

MC_HD static inline void eb_tick_living_aabb_contacts(
        EbLiving *e, float ground_slip, int isMovementBlocked,
        const McAABB *blocks, int nblocks,
        const int (*contact_cells)[4], int ncontacts,
        const McSinTable *st) {
    eb_on_entity_update(&e->base);
    elb_on_living_update_aabb_contacts(
        e, ground_slip, isMovementBlocked, blocks, nblocks,
        contact_cells, ncontacts, st);
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
    e->jumpBoostAmplifier = -1;
    e->levitationAmplifier = -1;
    e->jumpTicks = 0;
    e->isSprinting = 0;
    e->isServerWorld = 1;             /* AI-enabled mob on the server */
}

#endif /* MC_LIVING_BASE_H */
