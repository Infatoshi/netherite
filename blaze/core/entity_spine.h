/* entity_spine.h - living Entity.move / travel spine without AI.
 *
 * Magma's move_mob land path (mob_live.c) and blaze-CPU/CUDA compile this
 * one source. Intents stay zero: no pathfinding, no combat. M1 is
 * blaze-vs-magma on --mobs off (RL still ticks this spine for loaded
 * snapshot mobs).
 *
 * Oracle 1.11.2 cites (java/oracle-src):
 *   Entity.setSize                 Entity.java:376-399
 *   Entity.setPosition             Entity.java:413-424
 *   Entity.onEntityUpdate          Entity.java:460-477 (prevPos)
 *   Entity.move                    Entity.java:668 (pcf_entity_move)
 *   Entity.updateFallState         Entity.java:1214-1228
 *   Entity.moveRelative            Entity.java:1424-1445
 *   EntityLivingBase ctor          EntityLivingBase.java:207 stepHeight=0.6F
 *   EntityLivingBase.jump          EntityLivingBase.java:1897-1921
 *   EntityLivingBase.moveEntityWithHeading
 *                                  EntityLivingBase.java:2015-2103
 *     0.91F air, slip*0.91F ground, 0.16277136F, gravity 0.08D,
 *     vertical drag (double)0.98F = 0.9800000190734863D
 *   EntityLivingBase.onLivingUpdate
 *                                  EntityLivingBase.java:2419-2511
 *     0.003D clamp, moveStrafing/moveForward *= 0.98F
 * Magma land extras (M1 is magma semantics):
 *   slip table ice 79/174/212 = 0.98F, water 8/9 = 0.8F, else 0.6F
 *     (mob_live.c:2397-2403)
 *   persistent AABB when box_on (mob_live.c:2313-2315, Entity.java:129)
 */
#ifndef MC_ENTITY_SPINE_H
#define MC_ENTITY_SPINE_H

#include "entity_hostile_spine.h"
#include "living_base.h"
#include "../env/blaze_snapshot.h"

#define ESS_MOB_BLOCKS 256

MC_HD static inline int ess_is_spine_type(int type) {
    return type != EW_TYPE_NONE && type != EW_TYPE_PLAYER
        && type != EW_TYPE_BOAT && type != EW_TYPE_GHAST;
}

MC_HD static inline int ess_solid_id(int id) {
    BptProps p;
    if (id == 0) return 0;
    p = mc_bpt_props(id);
    return (p.flags & BF_SOLID) && !(p.flags & BF_LIQUID);
}

/* Block.slipperiness under the feet. Ice family 0.98F (BlockIce);
 * water 0.8F matches magma move_mob; default 0.6F. */
MC_HD static inline float ess_ground_slip(int id) {
    if (id == 79 || id == 174 || id == 212) return 0.98f;
    if (id == 8 || id == 9) return 0.8f;
    return 0.6f;
}

MC_HD static inline void ess_load_pose(EbLiving *liv, int type,
                                       double x, double y, double z,
                                       double mx, double my, double mz,
                                       int on_ground, float yaw,
                                       int box_on,
                                       double box_minx, double box_miny,
                                       double box_minz, double box_maxx,
                                       double box_maxy, double box_maxz) {
    float w, h;
    ehs_size((u8)type, &w, &h);
    elb_init(liv, w, h, x, y, z);
    liv->base.phys.motionX = mx;
    liv->base.phys.motionY = my;
    liv->base.phys.motionZ = mz;
    liv->base.phys.onGround = on_ground ? 1 : 0;
    liv->base.rotationYaw = yaw;
    liv->landMovementFactor = ehs_land_speed((u8)type);
    liv->jumpMovementFactor = 0.02f;
    liv->isServerWorld = 1;
    liv->moveForward = 0.0f;
    liv->moveStrafing = 0.0f;
    liv->isJumping = 0;
    liv->jumpTicks = 0;
    if (box_on) {
        liv->base.phys.box.minX = box_minx;
        liv->base.phys.box.minY = box_miny;
        liv->base.phys.box.minZ = box_minz;
        liv->base.phys.box.maxX = box_maxx;
        liv->base.phys.box.maxY = box_maxy;
        liv->base.phys.box.maxZ = box_maxz;
    }
}

MC_HD static inline void ess_load_snap(EbLiving *liv, const RlSnapMob *m) {
    ess_load_pose(liv, m->type, m->x, m->y, m->z, m->mx, m->my, m->mz,
                  m->on_ground, m->yaw, m->box_on,
                  m->box_minx, m->box_miny, m->box_minz,
                  m->box_maxx, m->box_maxy, m->box_maxz);
}

MC_HD static inline void ess_store_snap(RlSnapMob *m, const EbLiving *liv) {
    m->x = liv->base.phys.posX;
    m->y = liv->base.phys.posY;
    m->z = liv->base.phys.posZ;
    m->mx = liv->base.phys.motionX;
    m->my = liv->base.phys.motionY;
    m->mz = liv->base.phys.motionZ;
    m->on_ground = liv->base.phys.onGround ? 1 : 0;
    m->yaw = liv->base.rotationYaw;
    m->box_on = 1;
    m->box_minx = liv->base.phys.box.minX;
    m->box_miny = liv->base.phys.box.minY;
    m->box_minz = liv->base.phys.box.minZ;
    m->box_maxx = liv->base.phys.box.maxX;
    m->box_maxy = liv->base.phys.box.maxY;
    m->box_maxz = liv->base.phys.box.maxZ;
}

/* Broadphase solids. Same loops as magma/game/mob_live.c:83-101. */
MC_HD static inline int ess_collect_push(PcfBlock *out, int n, int cap,
                                         int id, int x, int y, int z) {
    if (!ess_solid_id(id) || n == cap) return n;
    out[n].block_id = id;
    out[n].ox = (double)x;
    out[n].oy = (double)y;
    out[n].oz = (double)z;
    out[n].ladder_facing = 0;
    return n + 1;
}

MC_HD static inline void ess_query_box(const EbLiving *liv, McAABB *q) {
    *q = mc_aabb_addcoord(&liv->base.phys.box, liv->base.phys.motionX,
                          liv->base.phys.motionY, liv->base.phys.motionZ);
    q->minY -= (double)liv->base.phys.stepHeight;
    q->maxY += (double)liv->base.phys.stepHeight;
}

MC_HD static inline float ess_slip_on_ground(const EbLiving *liv, int under_id) {
    if (!liv->base.phys.onGround) return 0.6f;
    return ess_ground_slip(under_id);
}

/* EntityLivingBase.onLivingUpdate + moveEntityWithHeading land branch,
 * intents already zero. */
MC_HD static inline void ess_tick_living(EbLiving *liv, float slip,
                                         const PcfBlock *blocks, int n,
                                         const McSinTable *st) {
    eb_tick_living(liv, slip, 0, blocks, n, st);
}

#endif /* MC_ENTITY_SPINE_H */
