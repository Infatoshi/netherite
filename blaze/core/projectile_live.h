/* projectile_live.h - magma live arrow tick (bow + skeleton).
 *
 * Magma magma/game/runtime.c and blaze-CPU/CUDA compile this one source.
 * Magma wrappers stay thin; do not re-derive runtime.c semantics.
 *
 * Java 1.11.2 (java/oracle-src):
 *   ItemBow.getArrowVelocity          ItemBow.java:167-177
 *     f = charge/20F; (f*f + f*2F)/3F; clamp 1.0F
 *   ItemBow.onPlayerStoppedUsing      ItemBow.java:81-161
 *     fire if (double)f >= 0.1D (:102); setAim velocity f*3.0F (:110)
 *   EntityArrow.setAim                EntityArrow.java:120-133
 *     heading -sin(yaw)*cos(pitch), -sin(pitch), cos(yaw)*cos(pitch)
 *   EntityArrow.onUpdate              EntityArrow.java:196-357
 *     in-air: rayTraceBlocks + findEntityOnPath, then pos += motion,
 *     drag (double)0.99F (0.6F in water), gravity 0.05000000074505806D
 *     inGround: ticksInGround >= 1200 setDead (:245-248)
 *     arrowShake-- (:223-226); onHit block sets inGround + shake=7
 *       EntityArrow.java:454-478
 *   EntityArrow.onHit entity          EntityArrow.java:366-452
 *     damage ceil(speed * this.damage); attackEntityFrom
 *   EntityLivingBase.attackEntityFrom EntityLivingBase.java:935-1093
 *     knockBack(entity, 0.4F, d1, d0) (:1067)
 *   EntityLivingBase.knockBack        EntityLivingBase.java:1296-1309
 *
 * Magma extras (M1 is magma semantics; do not "fix" to Java here):
 *   stepped 0.25-block substeps, not World.rayTraceBlocks
 *   in-air pl_tick_arrow still deactivates on any non-air id; magma
 *     runtime.c:460-477 and blaze restick with inGround + arrowShake=7
 *     (EntityArrow.java:471-472). Sidecars sit beside PlProj.
 *   drag 0.99 and gravity 0.05 as double literals (not 0.99F / 0.05F)
 *   no water 0.6F drag, no findEntityOnPath AABB intercept
 *   pickup: EntityArrow.setSize(0.5F, 0.5F) java:78 + onCollideWithPlayer
 *     java:604-618 (magma runtime.c:302-331)
 *   inGround life 1200 EntityArrow.java:245-248 (magma runtime.c:450-456)
 *   damage = speed * 2.0 sphere r=0.75 about (x, y+0.9, z)
 *   spawn heading uses libm sin/cos of yaw*PI/180, offset 0.2 along aim,
 *     eye = feetY + PSV_EYE_HEIGHT (1.62); no shooter motion add
 *   type 1 hits mobs/dragon; type 2 hits the player (4.0F)
 *
 * Include after defining:
 *   PL_W                  world/runtime pointer type
 *   PL_BLOCK(w,x,y,z)     block id at world cell
 * Optional:
 *   PL_HIT_MOB / PL_HIT_DRAGON / PL_HIT_PLAYER  -> 0/1
 *   PL_NOTE_HIT(w)        collision/despawn evidence
 *   PL_EYE_HEIGHT         default 1.62 (PSV_EYE_HEIGHT)
 */
#ifndef MC_PROJECTILE_LIVE_H
#define MC_PROJECTILE_LIVE_H

#include <math.h>

#include "mc.h"
#include "mc_math.h"

#ifndef PL_EYE_HEIGHT
#define PL_EYE_HEIGHT 1.62
#endif

#ifndef PL_MAX
#define PL_MAX 32
#endif

typedef struct {
    int active, type, age;
    double x, y, z, vx, vy, vz;
} PlProj;

/* ItemBow.java:167-177 without the 1.0F clamp (magma applies clamp after
 * the 0.1F reject). */
MC_HD static inline float pl_bow_curve(int charge) {
    float f = (float)charge / 20.0f;
    return (f * f + f * 2.0f) / 3.0f;
}

/* ItemBow.getArrowVelocity :167-177. */
MC_HD static inline float pl_bow_charge(int charge) {
    float f = pl_bow_curve(charge);
    if (f > 1.0f) f = 1.0f;
    return f;
}

MC_HD static inline int pl_spawn_arrow(PlProj *slots, int cap,
                                       double px, double py, double pz,
                                       float yaw, float pitch, float charge) {
    double yr, pr, dx, dy, dz;
    int i;
    if (!slots || cap <= 0 || charge < 0.1f) return 0;
    yr = (double)yaw * MC_PI / 180.0;
    pr = (double)pitch * MC_PI / 180.0;
    dx = -sin(yr) * cos(pr);
    dy = -sin(pr);
    dz = cos(yr) * cos(pr);
    for (i = 0; i < cap; ++i) {
        if (slots[i].active) continue;
        slots[i].active = 1;
        slots[i].type = 1;
        slots[i].age = 0;
        slots[i].x = px + dx * 0.2;
        slots[i].y = py + PL_EYE_HEIGHT + dy * 0.2;
        slots[i].z = pz + dz * 0.2;
        slots[i].vx = dx * (double)(charge * 3.0f);
        slots[i].vy = dy * (double)(charge * 3.0f);
        slots[i].vz = dz * (double)(charge * 3.0f);
        return 1;
    }
    return 0;
}

/* magma runtime.c spawn_bow_arrow :386-389. pickup_status is
 * EntityArrow.PickupStatus from isr_try_fire_bow (1 ALLOWED, 2 CREATIVE_ONLY). */
MC_HD static inline void pl_arrow_life_on_spawn(int *in_ground, int *shake,
                                                int *pickup, int *ground_ticks,
                                                int pickup_status) {
    *in_ground = 0;
    *shake = 0;
    *pickup = pickup_status;
    *ground_ticks = 0;
}

/* EntityArrow.onHit block EntityArrow.java:471-472
 * magma runtime.c:467-475. */
MC_HD static inline void pl_stick_in_ground(PlProj *p, int *in_ground,
                                            int *shake, int *ground_ticks) {
    if (!p || !in_ground || !shake || !ground_ticks) return;
    p->active = 1;
    p->vx = 0.0;
    p->vy = 0.0;
    p->vz = 0.0;
    *in_ground = 1;
    *shake = 7;
    *ground_ticks = 0;
}

/* EntityArrow.onUpdate inGround EntityArrow.java:223-248
 * magma runtime.c:450-456. Returns 1 if the arrow stays alive. */
MC_HD static inline int pl_tick_in_ground(PlProj *p, int *shake, int *ground_ticks) {
    if (!p || !shake || !ground_ticks || !p->active) return 0;
    if (*shake > 0) --(*shake);
    ++(*ground_ticks);
    if (*ground_ticks >= 1200) {
        p->active = 0;
        return 0;
    }
    return 1;
}

/* EntityArrow.setSize(0.5F, 0.5F) EntityArrow.java:78
 * magma runtime.c:309-311. Player box is already in world coords. */
MC_HD static inline int pl_arrow_touches_player(
    double ax, double ay, double az,
    double pminx, double pminy, double pminz,
    double pmaxx, double pmaxy, double pmaxz) {
    double aminX = ax - 0.25, aminY = ay, aminZ = az - 0.25;
    double amaxX = ax + 0.25, amaxY = ay + 0.5, amaxZ = az + 0.25;
    return aminX < pmaxx && amaxX > pminx &&
           aminY < pmaxy && amaxY > pminy &&
           aminZ < pmaxz && amaxZ > pminz;
}

/* EntityArrow.onCollideWithPlayer EntityArrow.java:604-618
 * magma runtime.c:322-331. add_ok is addItemStackToInventory when
 * pickup_status==1; ignored otherwise. */
MC_HD static inline int pl_pickup_kills_arrow(int pickup_status, int creative,
                                              int add_ok) {
    int flag = (pickup_status == 1) || (pickup_status == 2 && creative);
    if (pickup_status == 1 && !add_ok) flag = 0;
    return flag;
}

#endif /* MC_PROJECTILE_LIVE_H */

#ifdef PL_W
#ifndef MC_PROJECTILE_LIVE_TICK_H
#define MC_PROJECTILE_LIVE_TICK_H

#ifndef PL_BLOCK
#error "projectile_live.h tick requires PL_BLOCK(w,x,y,z)"
#endif
#ifndef PL_HIT_MOB
#define PL_HIT_MOB(w, x, y, z, rad, dmg) ((void)(dmg), 0)
#endif
#ifndef PL_HIT_DRAGON
#define PL_HIT_DRAGON(w, x, y, z, rad, dmg) ((void)(dmg), 0)
#endif
#ifndef PL_HIT_PLAYER
#define PL_HIT_PLAYER(w, x, y, z, rad, dmg) ((void)(dmg), 0)
#endif
#ifndef PL_NOTE_HIT
#define PL_NOTE_HIT(w) ((void)0)
#endif

/* Magma runtime.c tick_projectiles type 1 (bow) and 2 (skeleton). */
MC_HD MC_NOINLINE static void pl_tick_arrow(PlProj *p, PL_W *w) {
    double speed;
    int steps, s;
    float damage;
    if (!p || !w || !p->active) return;
    if (p->type != 1 && p->type != 2) return;

    speed = sqrt(p->vx * p->vx + p->vy * p->vy + p->vz * p->vz);
    steps = (int)ceil(speed / 0.25);
    if (steps < 1) steps = 1;
    damage = (float)(speed * 2.0);
    for (s = 0; s < steps && p->active; ++s) {
        int block;
        p->x += p->vx / (double)steps;
        p->y += p->vy / (double)steps;
        p->z += p->vz / (double)steps;
        block = PL_BLOCK(w, (int)floor(p->x), (int)floor(p->y),
                         (int)floor(p->z));
        if (p->type == 1) {
            if (block ||
                PL_HIT_DRAGON(w, p->x, p->y, p->z, 0.75, damage) ||
                PL_HIT_MOB(w, p->x, p->y, p->z, 0.75, damage)) {
                p->active = 0;
                PL_NOTE_HIT(w);
            }
        } else if (PL_HIT_PLAYER(w, p->x, p->y, p->z, 0.75, 4.0f)) {
            p->active = 0;
            PL_NOTE_HIT(w);
        } else if (block) {
            p->active = 0;
            PL_NOTE_HIT(w);
        }
    }
    ++p->age;
    if (!p->active || p->age > 1200) {
        p->active = 0;
        return;
    }
    p->vy -= 0.05;
    p->vx *= 0.99;
    p->vy *= 0.99;
    p->vz *= 0.99;
}

/* magma runtime.c:460-477: after pl_tick_arrow deactivates, restick if the
 * landing cell is non-air. An entity hit in air stays dead. */
MC_HD static inline void pl_restick_if_block(PlProj *p, PL_W *w, int was,
                                             int *in_ground, int *shake,
                                             int *ground_ticks) {
    int bx, by, bz;
    if (!p || !w || !was || p->active) return;
    bx = (int)floor(p->x);
    by = (int)floor(p->y);
    bz = (int)floor(p->z);
    if (PL_BLOCK(w, bx, by, bz))
        pl_stick_in_ground(p, in_ground, shake, ground_ticks);
}

#endif /* MC_PROJECTILE_LIVE_TICK_H */
#endif /* PL_W */
