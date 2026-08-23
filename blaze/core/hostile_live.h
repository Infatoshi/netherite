/* hostile_live.h - magma live hostile tick subset (zombie/skeleton/creeper).
 *
 * Magma magma/game/mob_live.c and blaze-CPU/CUDA compile this one source.
 * Magma wrappers stay thin; do not re-derive gm_mobs_tick.
 *
 * Java 1.11.2 (java/oracle-src):
 *   EntityZombie.initEntityAI              EntityZombie.java:77-86
 *   EntityZombie.applyEntityAI             EntityZombie.java:88-95
 *   EntityZombie.applyEntityAttributes     EntityZombie.java:97-105
 *     FOLLOW_RANGE 35, MOVEMENT_SPEED 0.23000000417232513, ATTACK_DAMAGE 3
 *   EntityZombie.attackEntityAsMob         EntityZombie.java:314-328
 *   AbstractSkeleton.initEntityAI          AbstractSkeleton.java:79-91
 *   AbstractSkeleton.applyEntityAttributes AbstractSkeleton.java:93-97
 *     MOVEMENT_SPEED 0.25
 *   EntityCreeper.initEntityAI             EntityCreeper.java:63-74
 *   EntityCreeper.fuseTime/radius         EntityCreeper.java:52-54
 *   EntityAIAttackMelee                    EntityAIAttackMelee.java:27,43-73,111-159
 *     attackInterval 20, mutex 3, look 30/30
 *   EntityAINearestAttackableTarget        EntityAINearestAttackableTarget.java:32-80
 *     chance 10, mutex 1
 *   EntityMob.attackEntityAsMob            EntityMob.java:98-128
 *   EntityLivingBase.attackEntityFrom      EntityLivingBase.java:935-1007
 *     hurtResistantTime > max/2 lastDamage gate; maxHurtResistantTime=20 (:114)
 *   EntityLivingBase.knockBack             EntityLivingBase.java:1296-1316
 *   EntityLivingBase.damageEntity          EntityLivingBase.java after armor
 *   EntityLivingBase.onDeath/dropLoot      EntityLivingBase.java:1224-1279
 *   EntityLiving.despawnEntity             EntityLiving.java:787-831
 *     128^2=16384 hard, age>600 + nextInt(800) and 32^2=1024 soft
 *   WorldEntitySpawner                     (magma simplified; not this header)
 *
 * Magma extras (M1 is magma semantics; do not "fix" to Java here):
 *   det_entity_rng off: generic aggro + straight chase, not EntityAITasks/A*
 *   follow_range zombie 40 (not Java 35) / skeleton+creeper 16
 *   melee reach 2.0 xz, |dy|<3; LOS is 2 samples/block, cap 96
 *   hash wander (purpose 0x57414e44), interval 120, radius 8
 *   player melee: fist 2.0 (SharedMonsterAttributes default), cooldown 10
 *   no knockBack on player or on hit hostiles
 *   persist does not skip despawn on this path (det_entity_rng only)
 *   drops: one item, no loot table / looting / equipment
 *
 * Include after defining ML_BLOCK(w,x,y,z) for the world half.
 */
#ifndef MC_MOB_LIVE_H
#define MC_MOB_LIVE_H

#include <math.h>
#include <string.h>

#include "mc.h"
#include "mc_math.h"
#include "combat_math.h"
#include "entity_hostile_spine.h"
#include "entity_spine.h"
#include "port_parity.h"
#include "../env/blaze_snapshot.h"

#ifndef ML_EYE_HEIGHT
#define ML_EYE_HEIGHT 1.62            /* PSV_EYE_HEIGHT */
#endif

#define ML_REACH 2.0                      /* magma GM_MOB_REACH */
#define ML_DESPAWN_SOFT 32.0              /* EntityLiving.java:821 32^2=1024 */
#define ML_DESPAWN_HARD 128.0             /* EntityLiving.java:816 128^2=16384 */
#define ML_DESPAWN_DELAY 600              /* magma; Java age>600 + nextInt(800) */
#define ML_FIRE_TICKS 160                 /* daylight burn */
#define ML_HURT_MAX 20                    /* EntityLivingBase.java:114 */
#define ML_PLAYER_ATK_CD 10
#define ML_WANDER_INTERVAL 120
#define ML_WANDER_RADIUS 8
#define ML_WANDER_PURPOSE 0x57414e44u     /* "WAND" */
#define ML_BLOCKS ESS_MOB_BLOCKS

/* EntityZombie.java:102 ATTACK_DAMAGE=3; AbstractSkeleton uses EntityMob 2
 * then weapon; magma generic skeleton is 4 (melee_damage default). */
MC_HD static inline float ml_melee_damage(int type) {
    if (type == EW_TYPE_ZOMBIE) return 3.0f;          /* EntityZombie.java:102 */
    if (type == EW_TYPE_SKELETON) return 4.0f;
    if (type == EW_TYPE_CREEPER) return 4.0f;
    if (type == EW_TYPE_ENDERMAN) return 7.0f;
    if (type == EW_TYPE_PIGMAN) return 5.0f;
    return 4.0f;
}

MC_HD static inline double ml_follow_range(int type) {
    if (type == EW_TYPE_ZOMBIE) return 40.0;          /* magma; Java 35.0 */
    if (type == EW_TYPE_SKELETON || type == EW_TYPE_CREEPER) return 16.0;
    return 16.0;
}

MC_HD static inline int ml_attack_cooldown(int type) {
    if (type == EW_TYPE_SKELETON) return 40;          /* magma bow reload edge */
    if (type == EW_TYPE_ZOMBIE || type == EW_TYPE_CREEPER) return 20;
    return 20;                                        /* EntityAIAttackMelee.java:27 */
}

/* Item ids: EntityZombie rotten flesh 367, skeleton bone 352, creeper gunpowder 289. */
MC_HD static inline int ml_drop_item(int type) {
    if (type == EW_TYPE_ZOMBIE) return 367;
    if (type == EW_TYPE_SKELETON) return 352;
    if (type == EW_TYPE_CREEPER) return 289;
    return 0;
}

MC_HD static inline int ml_is_roster(int type) {
    return type == EW_TYPE_ZOMBIE || type == EW_TYPE_SKELETON
        || type == EW_TYPE_CREEPER;
}

MC_HD static inline int ml_solid_id(int id) {
    BptProps p;
    if (id == 0) return 0;
    p = mc_bpt_props(id);
    return (p.flags & BF_SOLID) && !(p.flags & BF_LIQUID);
}

/* EntityPlayer.applyEntityAttributes ATTACK_DAMAGE=1 on det path; knob-off
 * live-sim fist is SharedMonsterAttributes default 2 (combat_math weapon 0). */
MC_HD static inline float ml_held_damage(int item) {
    if (item == 268) return mc_combat_weapon_raw(1);
    if (item == 272) return mc_combat_weapon_raw(2);
    if (item == 267) return mc_combat_weapon_raw(3);
    if (item == 276) return mc_combat_weapon_raw(4);
    return mc_combat_weapon_raw(0);
}

/* EntityLivingBase.attackEntityFrom hurtResistantTime/lastDamage gate
 * (EntityLivingBase.java:989-1007). lastDamage is the RAW pre-armor amount. */
MC_HD static inline int ml_hurt_gate(int *hurt_res, float *last_dmg, float amount,
                                    float *applied) {
    if (!hurt_res || !last_dmg || !applied || amount <= 0.0f) return 0;
    if (*hurt_res > ML_HURT_MAX / 2) {
        if (amount <= *last_dmg) return 0;
        *applied = amount - *last_dmg;
        *last_dmg = amount;
    } else {
        *applied = amount;
        *last_dmg = amount;
        *hurt_res = ML_HURT_MAX;
    }
    return 1;
}

/* Packed living slot plus generic-path AI that RlSnapMob already carries. */
typedef struct {
    RlSnapMob snap;
    int repath_timer;
    int despawn_ticks;
    int fire_ticks;
    int size;
    int exploded;
} MlMob;

MC_HD static inline void ml_from_snap(MlMob *o, const RlSnapMob *s) {
    if (!o) return;
    memset(o, 0, sizeof *o);
    if (s) o->snap = *s;
}

MC_HD static inline void ml_store_ai(RlSnapMob *s, int aggro, unsigned ai_state,
                                     double path_tx, double path_tz, int path_len) {
    if (!s) return;
    s->target_idx = aggro ? 1 : 0;
    s->task_bits = ai_state;
    s->wander_x = path_tx;
    s->wander_z = path_tz;
    s->panic = path_len;
}

/* Player ray vs living AABB (magma gm_mobs_player_attack). Reach 3.0. */
MC_HD static inline int ml_player_pick(const RlSnapMob *mobs, unsigned n,
                                       double px, double py, double pz,
                                       float yaw, float pitch) {
    double yr, pr, dx, dy, dz;
    int best = -1;
    double best_t = 3.0;
    unsigned i;
    yr = (double)yaw * MC_PI / 180.0;
    pr = (double)pitch * MC_PI / 180.0;
    dx = -sin(yr) * cos(pr);
    dy = -sin(pr);
    dz = cos(yr) * cos(pr);
    py += ML_EYE_HEIGHT;
    for (i = 0; i < n; ++i) {
        const RlSnapMob *m = &mobs[i];
        float width, height;
        double cx, cy, cz, vx, vy, vz, t, ex, ey, ez, radius, vtol;
        if (!m->alive || m->type == EW_TYPE_NONE || m->type == EW_TYPE_PLAYER
            || m->type == EW_TYPE_BOAT)
            continue;
        ehs_size((u8)m->type, &width, &height);
        cx = m->x;
        cy = m->y + (double)height * 0.5;
        cz = m->z;
        vx = cx - px;
        vy = cy - py;
        vz = cz - pz;
        t = vx * dx + vy * dy + vz * dz;
        if (t < 0.0 || t > best_t) continue;
        ex = vx - t * dx;
        ey = vy - t * dy;
        ez = vz - t * dz;
        radius = (double)width * 0.5 + 0.25;
        vtol = (double)height * 0.5 + 0.25;
        if (ex * ex + ez * ez <= radius * radius && fabs(ey) <= vtol) {
            best = (int)i;
            best_t = t;
        }
    }
    return best;
}

#endif /* MC_MOB_LIVE_H */

#ifdef ML_W
#ifndef MC_MOB_LIVE_WORLD_H
#define MC_MOB_LIVE_WORLD_H

#include "mc_rng.h"

#ifndef ML_BLOCK
#error "hostile_live.h world half requires ML_BLOCK(w,x,y,z)"
#endif

MC_HD static inline int ml_los_clear(ML_W *w, double x0, double y0, double z0,
                                     double x1, double y1, double z1) {
    double dx = x1 - x0, dy = y1 - y0, dz = z1 - z0;
    double d = sqrt(dx * dx + dy * dy + dz * dz);
    int steps = (int)(d * 2.0);
    int s;
    if (steps < 1) return 1;
    if (steps > 96) steps = 96;
    for (s = 1; s < steps; ++s) {
        double t = (double)s / (double)steps;
        if (ml_solid_id(ML_BLOCK(w, mc_floor(x0 + dx * t),
                                 mc_floor(y0 + dy * t),
                                 mc_floor(z0 + dz * t))))
            return 0;
    }
    return 1;
}

MC_HD static inline int ml_wander_ground_y(ML_W *w, int x, int y0, int z) {
    int yy;
    for (yy = y0 + 1; yy >= y0 - 3; --yy) {
        if (yy < 1) break;
        if (ml_solid_id(ML_BLOCK(w, x, yy - 1, z)) &&
            !ml_solid_id(ML_BLOCK(w, x, yy, z)) &&
            !ml_solid_id(ML_BLOCK(w, x, yy + 1, z)))
            return yy;
    }
    return -1000;
}

MC_HD static inline int ml_sky_exposed(ML_W *w, double x, double y, double z) {
    int bx = mc_floor(x), bz = mc_floor(z);
    int by = mc_floor(y + 1.8);
    int feet = ML_BLOCK(w, bx, mc_floor(y), bz);
    int yy;
    if (feet && (mc_bpt_props(feet).flags & BF_LIQUID)) return 0;
    for (yy = by; yy < 256; ++yy)
        if (ml_solid_id(ML_BLOCK(w, bx, yy, bz))) return 0;
    return 1;
}

/* Despawn clocks + daylight burn. Magma gm_mobs_tick hostile preamble
 * (mob_live.c:3064-3091) for det_entity_rng-off hostiles.
 * Returns 1 alive, 0 despawned (no drop), -1 fire death (drop). */
MC_HD static inline int ml_hostile_pre(MlMob *m, ML_W *w,
                                       double px, double py, double pz,
                                       int day) {
    RlSnapMob *s;
    double dx, dy, dz, d;
    int type;
    if (!m || !m->snap.alive) return 0;
    s = &m->snap;
    type = s->type;
    dx = px - s->x;
    dy = py - s->y;
    dz = pz - s->z;
    d = sqrt(dx * dx + dy * dy + dz * dz);
    if (d > ML_DESPAWN_HARD) {
        s->alive = 0;
        s->type = EW_TYPE_NONE;
        return 0;
    }
    if (d > ML_DESPAWN_SOFT) {
        if (++m->despawn_ticks >= ML_DESPAWN_DELAY) {
            s->alive = 0;
            s->type = EW_TYPE_NONE;
            return 0;
        }
    } else {
        m->despawn_ticks = 0;
    }
    if (day && (type == EW_TYPE_ZOMBIE || type == EW_TYPE_SKELETON) &&
        m->fire_ticks <= 0 && ml_sky_exposed(w, s->x, s->y, s->z))
        m->fire_ticks = ML_FIRE_TICKS;
    if (m->fire_ticks > 0) {
        --m->fire_ticks;
        if (m->fire_ticks % 20 == 0) {
            s->health -= 1.0f;
            if (s->health <= 0.0f) {
                s->alive = 0;
                s->type = EW_TYPE_NONE;
                return -1;
            }
        }
    }
    return 1;
}

typedef struct {
    int moving, jump, wandering;
    int hit_player;
    float hit_dmg;
    int skel_fire;
} MlAiOut;

/* Generic (det_entity_rng off) hostile body for zombie/skeleton/creeper.
 * Magma gm_mobs_tick inner branch, magma/game/mob_live.c. */
MC_HD static inline void ml_hostile_ai(MlMob *m, ML_W *w,
                                       double px, double py, double pz,
                                       int day, long long seed, long long tick,
                                       MlAiOut *out) {
    RlSnapMob *s;
    int type, aggro = 0, moving = 0, jump = 0, wandering = 0;
    double dx, dy, dz, d, xz;
    float mw, mh;
    if (out) {
        out->moving = 0;
        out->jump = 0;
        out->wandering = 0;
        out->hit_player = 0;
        out->hit_dmg = 0.0f;
        out->skel_fire = 0;
    }
    if (!m || !m->snap.alive) return;
    s = &m->snap;
    type = s->type;
    if (!ml_is_roster(type)) return;
    (void)day;
    dx = px - s->x;
    dy = py - s->y;
    dz = pz - s->z;
    d = sqrt(dx * dx + dy * dy + dz * dz);
    xz = sqrt(dx * dx + dz * dz);
    ehs_size((u8)type, &mw, &mh);
    if (d <= ml_follow_range(type))
        aggro = ml_los_clear(w, s->x, s->y + (double)mh * 0.85, s->z,
                             px, py + ML_EYE_HEIGHT, pz);
    if (aggro && type == EW_TYPE_SKELETON) {
        s->yaw = ehs_yaw_toward(dx, dz);
        if (xz < 6.0 && xz > 0.01) {
            double ux = dx / xz, uz = dz / xz;
            s->wander_x = s->x - ux * 4.0;
            s->wander_z = s->z - uz * 4.0;
            s->panic = 0;
            s->task_bits = EW_AI_CHASE;
            moving = 1;
        } else if (xz > 14.0) {
            s->wander_x = px;
            s->wander_z = pz;
            s->panic = 0;
            s->task_bits = EW_AI_CHASE;
            moving = 1;
        } else {
            s->wander_x = px;
            s->wander_z = pz;
            s->panic = 0;
            s->task_bits = EW_AI_ATTACK;
        }
        if (s->attack_time <= 0) {
            s->attack_time = ml_attack_cooldown(type);
            if (out) out->skel_fire = (s->attack_time == 40);
        }
    } else if (aggro && type == EW_TYPE_CREEPER && xz <= 3.0 && fabs(dy) < 3.0) {
        s->wander_x = px;
        s->wander_z = pz;
        s->panic = 0;
        s->task_bits = EW_AI_ATTACK;
        s->yaw = ehs_yaw_toward(dx, dz);
        if (++s->swell >= 30) {
            s->alive = 0;
            s->type = EW_TYPE_NONE;
            s->swell = 0;
            m->exploded = 1;
        }
    } else if (aggro && xz <= ML_REACH && fabs(dy) < 3.0) {
        s->wander_x = px;
        s->wander_z = pz;
        s->panic = 0;
        s->task_bits = EW_AI_ATTACK;
        s->yaw = ehs_yaw_toward(dx, dz);
        if (s->attack_time <= 0) {
            if (out) {
                out->hit_player = 1;
                out->hit_dmg = ml_melee_damage(type);
            }
            s->attack_time = ml_attack_cooldown(type);
        }
    } else if (aggro) {
        s->wander_x = px;
        s->wander_z = pz;
        s->panic = 0;
        s->task_bits = EW_AI_CHASE;
        moving = 1;
        if (type == EW_TYPE_CREEPER && s->swell > 0) --s->swell;
    } else {
        int has;
        s->task_bits = EW_AI_IDLE;
        wandering = 1;
        if (m->repath_timer > 0) --m->repath_timer;
        has = s->panic == 1;
        if (has) {
            double wx2 = s->wander_x - s->x, wz2 = s->wander_z - s->z;
            if (wx2 * wx2 + wz2 * wz2 < 1.0) {
                s->panic = 0;
                has = 0;
            }
        }
        if (!has && m->repath_timer <= 0) {
            u64 h;
            int ddx, ddz;
            m->repath_timer = ML_WANDER_INTERVAL;
            h = mc_hash_seed((u64)seed, tick, s->slot, 0, 0, ML_WANDER_PURPOSE);
            ddx = mc_hash_bound(h, 2 * ML_WANDER_RADIUS + 1) - ML_WANDER_RADIUS;
            ddz = mc_hash_bound(mc_hash64(h), 2 * ML_WANDER_RADIUS + 1)
                - ML_WANDER_RADIUS;
            if (ddx || ddz) {
                int txc = mc_floor(s->x) + ddx, tzc = mc_floor(s->z) + ddz;
                int ty = ml_wander_ground_y(w, txc, mc_floor(s->y), tzc);
                if (ty > -999) {
                    s->wander_x = txc + 0.5;
                    s->wander_z = tzc + 0.5;
                    s->panic = 1;
                    has = 1;
                }
            }
        }
        if (has) moving = 1;
    }
    s->target_idx = aggro ? 1 : 0;
    if (moving && type != EW_TYPE_GHAST) {
        double mvx = s->wander_x - s->x, mvz = s->wander_z - s->z;
        double len = sqrt(mvx * mvx + mvz * mvz);
        if (len > 0.01) {
            int ax = mc_floor(s->x + mvx / len * 0.9);
            int az = mc_floor(s->z + mvz / len * 0.9);
            int fy = mc_floor(s->y);
            if (ml_solid_id(ML_BLOCK(w, ax, fy, az)) &&
                !ml_solid_id(ML_BLOCK(w, ax, fy + 1, az)) &&
                !ml_solid_id(ML_BLOCK(w, ax, fy + 2, az)))
                jump = 1;
            else if (wandering && !ml_solid_id(ML_BLOCK(w, ax, fy, az)) &&
                     !ml_solid_id(ML_BLOCK(w, ax, fy - 1, az)) &&
                     !ml_solid_id(ML_BLOCK(w, ax, fy - 2, az))) {
                moving = 0;
                s->panic = 0;
            }
        }
    }
    if (out) {
        out->moving = moving;
        out->jump = jump;
        out->wandering = wandering;
    }
}

MC_HD static inline void ml_move_hostile(MlMob *m, ML_W *w, const McSinTable *st,
                                         int moving, int jump) {
    EbLiving liv;
    EhsIntent intent;
    PcfBlock blocks[ML_BLOCKS];
    McAABB q;
    int n = 0, x, y, z, x0, x1, y0, y1, z0, z1, under;
    float slip;
    RlSnapMob *s;
    if (!m || !m->snap.alive || !ess_is_spine_type(m->snap.type)) return;
    s = &m->snap;
    ehs_intent_from_ai((u8)s->type, (u32)s->task_bits, moving,
                       s->x, s->z, s->wander_x, s->wander_z,
                       s->wander_x, s->wander_z, &intent);
    if (!moving) intent.yaw = s->yaw;
    if (moving && jump) intent.isJumping = 1;
    /* Generic path rebuilds AABB from pos (mob_live.c ehs_load_living). */
    ess_load_pose(&liv, s->type, s->x, s->y, s->z, s->mx, s->my, s->mz,
                  s->on_ground, intent.yaw, 0, 0, 0, 0, 0, 0, 0);
    liv.moveForward = intent.moveForward;
    liv.moveStrafing = intent.moveStrafing;
    liv.isJumping = intent.isJumping;
    liv.landMovementFactor = ehs_land_speed((u8)s->type);
    ess_query_box(&liv, &q);
    x0 = mc_floor(q.minX) - 1; x1 = mc_floor(q.maxX) + 1;
    y0 = mc_floor(q.minY) - 1; y1 = mc_floor(q.maxY) + 1;
    z0 = mc_floor(q.minZ) - 1; z1 = mc_floor(q.maxZ) + 1;
    if (y0 < 0) y0 = 0;
    if (y1 > 255) y1 = 255;
    for (x = x0; x <= x1; ++x)
        for (y = y0; y <= y1; ++y)
            for (z = z0; z <= z1; ++z) {
                n = ess_collect_push(blocks, n, ML_BLOCKS,
                                     ML_BLOCK(w, x, y, z), x, y, z);
                if (n == ML_BLOCKS) goto collected;
            }
collected:
    under = ML_BLOCK(w, mc_floor(liv.base.phys.posX),
                     mc_floor(liv.base.phys.box.minY) - 1,
                     mc_floor(liv.base.phys.posZ));
    slip = ess_slip_on_ground(&liv, under);
    ess_tick_living(&liv, slip, blocks, n, st);
    {
        /* Generic magma path does not refresh det_box (pai_det off). */
        unsigned char box_on = s->box_on;
        double minx = s->box_minx, miny = s->box_miny, minz = s->box_minz;
        double maxx = s->box_maxx, maxy = s->box_maxy, maxz = s->box_maxz;
        ess_store_snap(s, &liv);
        s->box_on = box_on;
        s->box_minx = minx; s->box_miny = miny; s->box_minz = minz;
        s->box_maxx = maxx; s->box_maxy = maxy; s->box_maxz = maxz;
    }
}

#endif /* MC_MOB_LIVE_WORLD_H */
#endif /* ML_W */
