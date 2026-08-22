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
 *   any non-air block id despawns (no inGround / inTile / arrowShake)
 *   drag 0.99 and gravity 0.05 as double literals (not 0.99F / 0.05F)
 *   no water 0.6F drag, no pickup, no findEntityOnPath AABB intercept
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

#endif /* MC_PROJECTILE_LIVE_TICK_H */
#endif /* PL_W */
