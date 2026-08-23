/* hostile_live.h - magma live hostile tick subset
 * (zombie/skeleton/creeper/spider/slime/enderman/witch).
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
 *   EntitySpider.setSize 1.4x0.9           EntitySpider.java:45
 *   EntitySpider MAX_HEALTH 16 / SPEED 0.30000001192092896
 *                                          EntitySpider.java:104-105
 *   EntitySpider ATTACK_DAMAGE default 2   SharedMonsterAttributes.java:23
 *     (applyEntityAttributes does not set ATTACK_DAMAGE)
 *   EntitySpider.onUpdate climbing         EntitySpider.java:91-98
 *     setBesideClimbableBlock(isCollidedHorizontally)
 *   EntitySpider.isOnLadder                EntitySpider.java:137-140
 *   EntityLivingBase.travel ladder clamp   EntityLivingBase.java:2047-2071
 *   EntitySpider does NOT override fall; EntityLivingBase.fall applies
 *                                          EntityLivingBase.java:1389-1422
 *   EntitySpider.AISpiderAttack continue   EntitySpider.java:247-260
 *     brightness>=0.5F && nextInt(100)==0 -> drop target
 *   EntitySpider.AISpiderTarget shouldExec EntitySpider.java:278-282
 *     brightness>=0.5F -> do not start
 *   EntitySpider.dropFewItems (1.8 / loot-table counts at looting 0)
 *     string nextInt(3)=0..2; spider eye 1/3 if player kill
 *   EntityMob.experienceValue 5            EntityMob.java:27
 *   EntitySpider.onInitialSpawn jockey     EntitySpider.java:200-207
 *     world.rand.nextInt(100)==0; rider is not representable, draw consumed
 *   EntitySpider.GroupData potion HARD     EntitySpider.java:213-216
 *   EntitySlime.setSlimeSize               EntitySlime.java:69-83
 *   EntitySlime.onInitialSpawn size        EntitySlime.java:406-415
 *   EntitySlime hop / SlimeMoveHelper      EntitySlime.java:574-646
 *   EntitySlime.canDamagePlayer size>1     EntitySlime.java:293-296
 *   EntitySlime.getAttackStrength = size   EntitySlime.java:301-304
 *   EntitySlime.setDead split              EntitySlime.java:217-247
 *   EntitySlime.getDropItem slimeball sz=1 EntitySlime.java:322-324
 *     EntityLiving.dropFewItems nextInt(3) EntityLiving.java:382-397
 *   EntitySlime.experienceValue = size     EntitySlime.java:82
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
 *   WorldEntitySpawner                     hostile_spawn.h
 *
 * Magma extras (M1 is magma semantics; do not "fix" to Java here):
 *   det_entity_rng off: generic aggro + straight chase, not EntityAITasks/A*
 *   follow_range zombie 40 (not Java 35) / skeleton+creeper 16
 *   melee reach 2.0 xz, |dy|<3; LOS is 2 samples/block, cap 96
 *   hash wander (purpose 0x57414e44), interval 120, radius 8
 *   player melee: fist 2.0 (SharedMonsterAttributes default), cooldown 10
 *   knockBack: KR default 0 so nextDouble always applies; player has no
 *     JavaRandom (that stream is not in the sim). Degenerate xz < 1e-4
 *     skips the attackEntityFrom Math.random() jitter.
 *   persist skips despawn (EntityLiving.java:790-793)
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
#include "mc_rng.h"
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
    if (type == EW_TYPE_SPIDER) return 2.0f;          /* SharedMonsterAttributes.java:23 */
    if (type == EW_TYPE_SKELETON) return 4.0f;
    if (type == EW_TYPE_CREEPER) return 4.0f;
    if (type == EW_TYPE_ENDERMAN) return 7.0f;
    if (type == EW_TYPE_WITCH) return 2.0f; /* SharedMonsterAttributes default */
    if (type == EW_TYPE_PIGMAN) return 5.0f;
    return 4.0f;
}

MC_HD static inline double ml_follow_range(int type) {
    if (type == EW_TYPE_ZOMBIE) return 40.0;          /* magma; Java 35.0 */
    if (type == EW_TYPE_SKELETON || type == EW_TYPE_CREEPER) return 16.0;
    if (type == EW_TYPE_ENDERMAN) return 64.0; /* EntityEnderman.java:95 */
    if (type == EW_TYPE_WITCH) return 16.0; /* EntityLiving default */
    return 16.0;
}

MC_HD static inline int ml_attack_cooldown(int type) {
    if (type == EW_TYPE_SKELETON) return 40;          /* magma bow reload edge */
    if (type == EW_TYPE_WITCH) return 60;             /* EntityAIAttackRanged.java:70 */
    if (type == EW_TYPE_ZOMBIE || type == EW_TYPE_CREEPER) return 20;
    return 20;                                        /* EntityAIAttackMelee.java:27 */
}

/* Item ids: EntityZombie rotten flesh 367, skeleton bone 352, creeper gunpowder 289,
 * spider string 287 / eye 375, slimeball 341. */
#define ML_ITEM_STRING 287
#define ML_ITEM_SPIDER_EYE 375
#define ML_ITEM_SLIME_BALL 341

MC_HD static inline int ml_drop_item(int type) {
    if (type == EW_TYPE_ZOMBIE) return 367;
    if (type == EW_TYPE_SKELETON) return 352;
    if (type == EW_TYPE_CREEPER) return 289;
    if (type == EW_TYPE_SPIDER) return ML_ITEM_STRING;
    if (type == EW_TYPE_SLIME) return ML_ITEM_SLIME_BALL;
    if (type == EW_TYPE_ENDERMAN) return 368; /* ender pearl */
    return 0;
}

MC_HD static inline int ml_is_roster(int type) {
    return type == EW_TYPE_ZOMBIE || type == EW_TYPE_SKELETON
        || type == EW_TYPE_CREEPER || type == EW_TYPE_SPIDER
        || type == EW_TYPE_SLIME || type == EW_TYPE_ENDERMAN
        || type == EW_TYPE_WITCH;
}

MC_HD static inline int ml_is_slimey(int type) {
    return type == EW_TYPE_SLIME || type == EW_TYPE_MAGMA;
}

/* Packed into existing RlSnapMob fields so MBM1 / 544-byte layout stays:
 *   slime size        swell
 *   slime jumpDelay   melee_delay
 *   slime wasOnGround see_time
 *   spider climbing   anger bit 0 */
MC_HD static inline int ml_slime_size(const RlSnapMob *s) {
    if (!s || !ml_is_slimey(s->type)) return 1;
    return s->swell > 0 ? s->swell : 1;
}

MC_HD static inline void ml_set_slime_size(RlSnapMob *s, int size) {
    if (!s) return;
    if (size < 1) size = 1;
    s->swell = size;
}

MC_HD static inline int ml_spider_climbing(const RlSnapMob *s) {
    return s && s->type == EW_TYPE_SPIDER && (s->anger & 1);
}

MC_HD static inline void ml_spider_set_climbing(RlSnapMob *s, int v) {
    if (!s) return;
    if (v) s->anger |= 1;
    else s->anger &= ~1;
}

/* WorldProvider.generateLightBrightnessTable overworld (WorldProvider.java:56-64):
 * f=0 so (1-f1)/(f1*3+1) with f1=1-light/15. Light 12 is exactly 0.5F. */
MC_HD static inline float ml_light_brightness(int light) {
    float f1;
    if (light < 0) light = 0;
    if (light > 15) light = 15;
    f1 = 1.0f - (float)light / 15.0f;
    return (1.0f - f1) / (f1 * 3.0f + 1.0f);
}

typedef struct {
    int item, count, meta;
} MlDrop;

/* 1.8 EntitySpider.dropFewItems / loot ENTITIES_SPIDER at looting 0:
 * string this.rand.nextInt(2 + looting) -> nextInt(3) = 0..2;
 * if player kill: nextInt(3)==0 drops eye, else nextInt(1+looting)>0
 * (looting 0: nextInt(1) is 0, always false; Java || short-circuits). */
MC_HD static inline int ml_spider_drop(JavaRandom *er, int player_kill,
                                       MlDrop *out, int cap) {
    int n = 0, str, a;
    if (!er || !out || cap <= 0) return 0;
    str = jrand_int_bound(er, 3);
    if (str > 0 && n < cap) {
        out[n].item = ML_ITEM_STRING;
        out[n].count = str;
        out[n].meta = 0;
        ++n;
    }
    if (player_kill) {
        a = jrand_int_bound(er, 3);
        if (a == 0) {
            if (n < cap) {
                out[n].item = ML_ITEM_SPIDER_EYE;
                out[n].count = 1;
                out[n].meta = 0;
                ++n;
            }
        } else {
            (void)jrand_int_bound(er, 1); /* looting 0: always 0, never >0 */
        }
    }
    return n;
}

/* EntityLiving.dropFewItems EntityLiving.java:382-397 when getDropItem is slimeball. */
MC_HD static inline int ml_slime_drop(JavaRandom *er, int size,
                                      MlDrop *out, int cap) {
    int n, c;
    if (!er || !out || cap <= 0) return 0;
    if (size != 1) return 0;                  /* EntitySlime.java:322-324 */
    n = 0;
    c = jrand_int_bound(er, 3);
    if (c > 0) {
        out[0].item = ML_ITEM_SLIME_BALL;
        out[0].count = c;
        out[0].meta = 0;
        n = 1;
    }
    return n;
}

MC_HD static inline int ml_xp_points(int type, int slime_size) {
    if (type == EW_TYPE_SLIME) return slime_size > 0 ? slime_size : 1;
    if (type == EW_TYPE_SPIDER) return 5;     /* EntityMob.java:27 */
    if (type == EW_TYPE_ENDERMAN) return 5;   /* EntityMob.java:27 */
    if (type == EW_TYPE_WITCH) return 5;      /* EntityMob.java:27 */
    return 5;
}

/* EntityLivingBase.onDeathUpdate EntityLivingBase.java:419-437:
 * ++deathTime; at deathTime==20 XP (recentlyHit/doMobLoot) then setDead.
 * EntitySlime.setDead EntitySlime.java:217-247 (split) runs at that setDead,
 * not on the health-hit / onDeath drop tick (EntityLivingBase.java:1224-1271).
 * Returns 1 while dying (keep the slot), 0 when setDead fires this tick. */
MC_HD static inline int ml_on_death_update(RlSnapMob *s) {
    if (!s) return 0;
    ++s->death_time;
    return s->death_time < 20 ? 1 : 0;
}

/* EntitySlime.setDead EntitySlime.java:223,227-228. */
MC_HD static inline int ml_slime_split_n(JavaRandom *er) {
    if (!er) return 2;
    return 2 + jrand_int_bound(er, 3);
}

MC_HD static inline void ml_slime_split_off(int i, int size, float *ox, float *oz) {
    *ox = ((float)(i % 2) - 0.5f) * (float)size / 4.0f;
    *oz = ((float)(i / 2) - 0.5f) * (float)size / 4.0f;
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
 * (EntityLivingBase.java:989-1007). lastDamage is the RAW pre-armor amount.
 * Returns 1 when flag1 (knockBack runs), 2 on i-frame overflow (flag1=false). */
MC_HD static inline int ml_hurt_gate(int *hurt_res, float *last_dmg, float amount,
                                    float *applied) {
    if (!hurt_res || !last_dmg || !applied || amount <= 0.0f) return 0;
    if (*hurt_res > ML_HURT_MAX / 2) {
        if (amount <= *last_dmg) return 0;
        *applied = amount - *last_dmg;
        *last_dmg = amount;
        return 2;
    }
    *applied = amount;
    *last_dmg = amount;
    *hurt_res = ML_HURT_MAX;
    return 1;
}

/* EntityLivingBase.knockBack EntityLivingBase.java:1296-1316 after the
 * nextDouble() >= KNOCKBACK_RESISTANCE gate (default KR=0, always applies).
 * Caller consumes entity.rand.nextDouble() when the entity has a Random. */
MC_HD static inline int ml_knockback(double *mx, double *my, double *mz,
                                     int on_ground, float strength,
                                     double x_ratio, double z_ratio) {
    float f;
    if (!mx || !my || !mz) return 0;
    f = (float)sqrt(x_ratio * x_ratio + z_ratio * z_ratio);
    if (f == 0.0f) return 0;
    *mx /= 2.0;
    *mz /= 2.0;
    *mx -= x_ratio / (double)f * (double)strength;
    *mz -= z_ratio / (double)f * (double)strength;
    if (on_ground) {
        *my /= 2.0;
        *my += (double)strength;
        if (*my > 0.4000000059604645)
            *my = 0.4000000059604645;
    }
    return 1;
}

/* EntityPlayer.attackTargetEntityWithCurrentItem sprint/enchant extra
 * (EntityPlayer.java:1423-1432) and EntityMob.attackEntityAsMob i>0
 * (EntityMob.java:115-117): knockBack(this, i*0.5F, sin(yaw), -cos(yaw)).
 * Magma extra: libm sinf/cosf, not MathHelper SIN_TABLE. */
MC_HD static inline void ml_knockback_yaw(double *mx, double *my, double *mz,
                                          int on_ground, float strength,
                                          float yaw_deg) {
    float r = yaw_deg * 0.017453292f;
    ml_knockback(mx, my, mz, on_ground, strength,
                 (double)sinf(r), (double)(-cosf(r)));
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
        ehs_size_scaled((u8)m->type, ml_slime_size(m), &width, &height);
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

#define ML_ITEM_ENDER_PEARL 368
#define ML_ENDER_EYE 2.55f            /* EntityEnderman.java:221-224 */
#define ML_PUMPKIN_ITEM 86            /* Blocks.PUMPKIN item */

typedef struct {
    float yaw, pitch;
    int helmet;
    int raining;
    int griefing;
    long long world_time;
    float player_health;
    double pmx, pmz;
} MlEndCtx;

#define ML_ITEM_GLOWSTONE 348
#define ML_ITEM_SUGAR 353
#define ML_ITEM_REDSTONE 331
#define ML_ITEM_GLASS_BOTTLE 374
#define ML_ITEM_GUNPOWDER 289
#define ML_ITEM_STICK 280
#define ML_POTION_SPEED 1              /* Potion.java:399 */
#define ML_POTION_FIRE_RESISTANCE 12   /* Potion.java:410 */
#define ML_POTION_WATER_BREATHING 13   /* Potion.java:411 */
#define ML_WITCH_DRINK_NONE 0
#define ML_WITCH_DRINK_WATER_BREATHING 1
#define ML_WITCH_DRINK_FIRE_RESISTANCE 2
#define ML_WITCH_DRINK_HEALING 3
#define ML_WITCH_DRINK_SWIFTNESS 4
#define ML_WITCH_DRINK_TICKS 32         /* ItemPotion.java:90 */
#define ML_WITCH_THROW_RANGE 10.0       /* EntityAIAttackRanged.java:70 10.0F */

/* ENTITIES_WITCH looting 0: rolls min 1 max 3 (nextInt(3)+1), then
 * WeightedRandom over 7 weight-1 entries, set_count 0..2 = nextInt(3). */
MC_HD static inline int ml_witch_drop(JavaRandom *er, MlDrop *out, int cap) {
    static const int table[7] = {
        ML_ITEM_GLOWSTONE, ML_ITEM_SUGAR, ML_ITEM_REDSTONE,
        ML_ITEM_SPIDER_EYE, ML_ITEM_GLASS_BOTTLE, ML_ITEM_GUNPOWDER,
        ML_ITEM_STICK
    };
    int rolls, i, n = 0;
    if (!er || !out || cap <= 0) return 0;
    rolls = 1 + jrand_int_bound(er, 3);
    for (i = 0; i < rolls; ++i) {
        int k = jrand_int_bound(er, 7);
        int c = jrand_int_bound(er, 3);
        if (c > 0 && n < cap) {
            out[n].item = table[k];
            out[n].count = c;
            out[n].meta = 0;
            ++n;
        }
    }
    return n;
}

MC_HD static inline int ml_witch_has_effect(const RlSnapMob *s, int id) {
    return s && s->effect_id == id && s->effect_duration > 0;
}

/* EntityEnderman.dropFewItems / loot ENTITIES_ENDERMAN at looting 0:
 * nextInt(2 + looting) = nextInt(2) = 0..1 pearls. */
MC_HD static inline int ml_enderman_drop(JavaRandom *er, MlDrop *out, int cap) {
    int n, c;
    if (!er || !out || cap <= 0) return 0;
    n = 0;
    c = jrand_int_bound(er, 2);
    if (c > 0) {
        out[0].item = ML_ITEM_ENDER_PEARL;
        out[0].count = c;
        out[0].meta = 0;
        n = 1;
    }
    return n;
}

/* EntityEnderman.java:413-430 CARRIABLE_BLOCKS. */
MC_HD static inline int ml_enderman_carriable(int id) {
    return id == 2 || id == 3 || id == 12 || id == 13
        || id == 37 || id == 38 || id == 39 || id == 40
        || id == 46 || id == 81 || id == 82 || id == 86
        || id == 103 || id == 110 || id == 87;
}

/* Entity.getVectorForRotation Entity.java:1707-1714. Magma extra: libm. */
MC_HD static inline void ml_look_vec(float pitch, float yaw,
                                     double *lx, double *ly, double *lz) {
    float r = 0.017453292f;
    float f = cosf(-yaw * r - (float)MC_PI);
    float f1 = sinf(-yaw * r - (float)MC_PI);
    float f2 = -cosf(-pitch * r);
    float f3 = sinf(-pitch * r);
    *lx = (double)(f1 * f2);
    *ly = (double)f3;
    *lz = (double)(f * f2);
}

#endif /* MC_MOB_LIVE_H */

#ifdef ML_W
#ifndef MC_MOB_LIVE_WORLD_H
#define MC_MOB_LIVE_WORLD_H

#include "mc_rng.h"

#ifndef ML_BLOCK
#error "hostile_live.h world half requires ML_BLOCK(w,x,y,z)"
#endif
#ifndef ML_SKY
#define ML_SKY(w, x, y, z) 0
#endif
#ifndef ML_BLK
#define ML_BLK(w, x, y, z) 0
#endif
#ifndef ML_SET_BLOCK
#define ML_SET_BLOCK(w, x, y, z, id) ((void)0)
#endif
#ifndef ML_BLOCK_META
#define ML_BLOCK_META(w, x, y, z) 0
#endif

MC_HD static inline float ml_entity_brightness(ML_W *w, double x, double y, double z,
                                               float eye) {
    int lx = mc_floor(x);
    int ly = mc_floor(y + (double)eye);
    int lz = mc_floor(z);
    int sky = ML_SKY(w, lx, ly, lz);
    int blk = ML_BLK(w, lx, ly, lz);
    int light = blk > sky ? blk : sky;
    return ml_light_brightness(light);
}

MC_HD static inline int ml_block_is_fluid(int id) {
    return id == 8 || id == 9 || id == 10 || id == 11;
}

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

/* EntityLiving.despawnEntity EntityLiving.java:787-831 + daylight burn.
 * persist => age=0, no nextInt. despawn_ticks is entityAge.
 * Returns 1 alive, 0 despawned (no drop), -1 fire death (drop). */
MC_HD static inline int ml_hostile_pre(MlMob *m, ML_W *w,
                                       double px, double py, double pz,
                                       int day) {
    RlSnapMob *s;
    double dx, dy, dz, d, d3;
    int type;
    JavaRandom er;
    if (!m || !m->snap.alive) return 0;
    s = &m->snap;
    type = s->type;
    if (s->health <= 0.0f) return 1;          /* dying: onDeathUpdate, no despawn */
    dx = px - s->x;
    dy = py - s->y;
    dz = pz - s->z;
    d = sqrt(dx * dx + dy * dy + dz * dz);
    d3 = d * d;
    /* EntityLiving.updateEntityActionState :835 ++entityAge then despawn.
     * Java && at :821 draws nextInt(800) whenever age>600, then d3>1024. */
    if (s->persist) {
        m->despawn_ticks = 0;                 /* EntityLiving.java:790-793 */
    } else {
        ++m->despawn_ticks;
        if (d3 > 16384.0) {                   /* :816 128^2 */
            s->alive = 0;
            s->type = EW_TYPE_NONE;
            return 0;
        }
        if (m->despawn_ticks > 600) {
            er.seed = s->seed48;
            if (jrand_int_bound(&er, 800) == 0 && d3 > 1024.0) {
                s->seed48 = er.seed;
                s->alive = 0;
                s->type = EW_TYPE_NONE;
                return 0;
            }
            s->seed48 = er.seed;
        }
        if (d3 < 1024.0)                      /* :825 */
            m->despawn_ticks = 0;
    }
    if (day && (type == EW_TYPE_ZOMBIE || type == EW_TYPE_SKELETON) &&
        m->fire_ticks <= 0 && ml_sky_exposed(w, s->x, s->y, s->z))
        m->fire_ticks = ML_FIRE_TICKS;
    if (m->fire_ticks > 0) {
        --m->fire_ticks;
        if (m->fire_ticks % 20 == 0) {
            s->health -= 1.0f;
            if (s->health <= 0.0f)
                s->health = 0.0f; /* onDeathUpdate in the tick loop */
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

/* EntitySlime hop + attack. Magma extra: no EntityAITasks mutex
 * (GPU_MOB_AI.md). Float (water) beats hop; Attack faces the player and
 * sets aggressive jumpDelay/3; FaceRandom consumes the cited draws when
 * idle. SlimeMoveHelper EntitySlime.java:600-646. */
MC_HD static inline void ml_slime_ai(MlMob *m, ML_W *w,
                                     double px, double py, double pz,
                                     MlAiOut *out) {
    RlSnapMob *s;
    JavaRandom er;
    int size, was, on, jump_delay, aggressive, moving, jump;
    int in_fluid, can_dmg, see;
    double dx, dy, dz, d;
    float mw, mh;
    int feet, k;
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
    size = ml_slime_size(s);
    ehs_size_scaled((u8)s->type, size, &mw, &mh);
    er.seed = s->seed48;
    was = s->see_time ? 1 : 0;
    on = s->on_ground ? 1 : 0;
    jump_delay = s->melee_delay;
    moving = 0;
    jump = 0;
    aggressive = 0;
    see = 0;
    /* EntitySlime.onUpdate landing particles EntitySlime.java:149-167:
     * size*8 pairs of nextFloat plus two nextFloat on the squish sound. */
    if (on && !was) {
        for (k = 0; k < size * 8; ++k) {
            (void)jrand_float(&er);
            (void)jrand_float(&er);
        }
        (void)jrand_float(&er);
        (void)jrand_float(&er);
    }
    s->see_time = on;
    feet = ML_BLOCK(w, mc_floor(s->x), mc_floor(s->y), mc_floor(s->z));
    in_fluid = ml_block_is_fluid(feet);
    dx = px - s->x;
    dy = py - s->y;
    dz = pz - s->z;
    d = sqrt(dx * dx + dy * dy + dz * dz);
    can_dmg = size > 1;                       /* EntitySlime.java:293-296 */
    if (can_dmg && d <= ml_follow_range(s->type))
        see = ml_los_clear(w, s->x, s->y + (double)mh * 0.85, s->z,
                           px, py + ML_EYE_HEIGHT, pz);
    if (in_fluid) {
        /* AISlimeFloat.updateTask EntitySlime.java:536-544 */
        if (jrand_float(&er) < 0.8f)
            jump = 1;
        moving = 1;
        if (see)
            s->yaw = ehs_yaw_toward(dx, dz);
    } else {
        if (see) {
            s->yaw = ehs_yaw_toward(dx, dz);
            aggressive = 1;                   /* setDirection(..., canDamagePlayer) */
        } else if (on) {
            /* AISlimeFaceRandom.updateTask EntitySlime.java:502-511 */
            if (--s->stime <= 0) {
                s->stime = 40 + jrand_int_bound(&er, 60);
                s->yaw = (float)jrand_int_bound(&er, 360);
            }
        }
        /* SlimeMoveHelper.onUpdateMoveHelper EntitySlime.java:614-644 */
        if (on) {
            if (--jump_delay <= 0) {
                jump_delay = jrand_int_bound(&er, 20) + 10; /* getJumpDelay :186-188 */
                if (aggressive)
                    jump_delay /= 3;
                jump = 1;
                if (size > 0) {               /* makesSoundOnJump :385-388 */
                    (void)jrand_float(&er);
                    (void)jrand_float(&er);
                }
                moving = 1;
            } else {
                moving = 0;
            }
        } else {
            moving = 1;
        }
    }
    s->melee_delay = jump_delay;
    if (see && can_dmg) {
        double reach = 0.6 * (double)size;
        if (d < reach) {
            if (out) {
                out->hit_player = 1;
                out->hit_dmg = (float)size;   /* getAttackStrength :301-304 */
            }
        }
    }
    s->seed48 = er.seed;
    s->target_idx = see ? 1 : 0;
    s->task_bits = see ? (unsigned)EW_AI_ATTACK : (unsigned)EW_AI_IDLE;
    s->wander_x = see ? px : s->x + sin((double)s->yaw * MC_PI / 180.0);
    s->wander_z = see ? pz : s->z + cos((double)s->yaw * MC_PI / 180.0);
    m->size = size;
    (void)dy;
    if (out) {
        out->moving = moving;
        out->jump = jump;
        out->wandering = !see;
    }
}

/* EntityLivingBase.attemptTeleport EntityLivingBase.java:3033-3105.
 * Particle loop 128 x (3 nextFloat + 3 nextDouble) on success only. */
MC_HD static inline int ml_attempt_teleport(MlMob *m, ML_W *w,
                                            JavaRandom *er,
                                            double x, double y, double z) {
    RlSnapMob *s;
    double ox, oy, oz;
    float mw, mh;
    int flag1 = 0;
    if (!m || !er) return 0;
    s = &m->snap;
    ox = s->x; oy = s->y; oz = s->z;
    s->x = x; s->y = y; s->z = z;
    ehs_size((u8)s->type, &mw, &mh);
    while (!flag1 && s->y > 0.0) {
        int bx = mc_floor(s->x), by = mc_floor(s->y) - 1, bz = mc_floor(s->z);
        if (ml_solid_id(ML_BLOCK(w, bx, by, bz)))
            flag1 = 1;
        else
            s->y -= 1.0;
    }
    if (flag1) {
        double x0 = s->x - (double)mw * 0.5, y0 = s->y;
        double z0 = s->z - (double)mw * 0.5;
        double x1 = s->x + (double)mw * 0.5, y1 = s->y + (double)mh;
        double z1 = s->z + (double)mw * 0.5;
        int ax, ay, az, id, hit = 0;
        BptProps p;
        int xa = mc_floor(x0), ya = mc_floor(y0), za = mc_floor(z0);
        int xb = mc_floor(x1), yb = mc_floor(y1), zb = mc_floor(z1);
        if (ya < 0) ya = 0;
        if (yb > 255) yb = 255;
        for (ax = xa; ax <= xb && !hit; ++ax)
            for (ay = ya; ay <= yb && !hit; ++ay)
                for (az = za; az <= zb; ++az) {
                    id = ML_BLOCK(w, ax, ay, az);
                    if (id == 0) continue;
                    p = mc_bpt_props(id);
                    if (p.flags & BF_LIQUID) { hit = 1; break; }
                    if ((p.flags & BF_SOLID) && p.light_opacity == 255) {
                        hit = 1; break;
                    }
                }
        if (!hit) {
            int j;
            for (j = 0; j < 128; ++j) {
                (void)jrand_float(er);
                (void)jrand_float(er);
                (void)jrand_float(er);
                (void)jrand_double(er);
                (void)jrand_double(er);
                (void)jrand_double(er);
            }
            return 1;
        }
    }
    s->x = ox; s->y = oy; s->z = oz;
    (void)mh;
    return 0;
}

/* EntityEnderman.teleportTo EntityEnderman.java:293-306. Forge event skipped. */
MC_HD static inline int ml_enderman_teleport_to(MlMob *m, ML_W *w,
                                                JavaRandom *er,
                                                double x, double y, double z) {
    return ml_attempt_teleport(m, w, er, x, y, z);
}

/* EntityEnderman.teleportRandomly EntityEnderman.java:268-274. */
MC_HD static inline int ml_enderman_teleport_randomly(MlMob *m, ML_W *w,
                                                      JavaRandom *er) {
    double d0, d1, d2;
    if (!m || !er) return 0;
    d0 = m->snap.x + (jrand_double(er) - 0.5) * 64.0;
    d1 = m->snap.y + (double)(jrand_int_bound(er, 64) - 32);
    d2 = m->snap.z + (jrand_double(er) - 0.5) * 64.0;
    return ml_enderman_teleport_to(m, w, er, d0, d1, d2);
}

/* EntityEnderman.teleportToEntity EntityEnderman.java:279-288. */
MC_HD static inline int ml_enderman_teleport_to_entity(MlMob *m, ML_W *w,
                                                       JavaRandom *er,
                                                       double px, double py,
                                                       double pz) {
    double vx, vy, vz, len, d1, d2, d3;
    float mh;
    if (!m || !er) return 0;
    {
        float mw;
        ehs_size((u8)m->snap.type, &mw, &mh);
        (void)mw;
    }
    vx = m->snap.x - px;
    vy = m->snap.y + (double)(mh / 2.0f) - (py + ML_EYE_HEIGHT);
    vz = m->snap.z - pz;
    len = sqrt(vx * vx + vy * vy + vz * vz);
    if (len > 0.0) { vx /= len; vy /= len; vz /= len; }
    d1 = m->snap.x + (jrand_double(er) - 0.5) * 8.0 - vx * 16.0;
    d2 = m->snap.y + (double)(jrand_int_bound(er, 16) - 8) - vy * 16.0;
    d3 = m->snap.z + (jrand_double(er) - 0.5) * 8.0 - vz * 16.0;
    return ml_enderman_teleport_to(m, w, er, d1, d2, d3);
}

/* EntityEnderman.shouldAttackPlayer EntityEnderman.java:202-218. */
MC_HD static inline int ml_enderman_should_attack(ML_W *w, const RlSnapMob *s,
                                                  double px, double py, double pz,
                                                  float pyaw, float ppitch,
                                                  int helmet) {
    double lx, ly, lz, vx, vy, vz, d0, d1;
    if (!s) return 0;
    if (helmet == ML_PUMPKIN_ITEM) return 0;
    ml_look_vec(ppitch, pyaw, &lx, &ly, &lz);
    vx = s->x - px;
    vy = s->y + (double)ML_ENDER_EYE - (py + ML_EYE_HEIGHT);
    vz = s->z - pz;
    d0 = sqrt(vx * vx + vy * vy + vz * vz);
    if (d0 <= 0.0) return 0;
    vx /= d0; vy /= d0; vz /= d0;
    d1 = lx * vx + ly * vy + lz * vz;
    if (d1 > 1.0 - 0.025 / d0)
        return ml_los_clear(w, px, py + ML_EYE_HEIGHT, pz,
                            s->x, s->y + (double)ML_ENDER_EYE, s->z);
    return 0;
}

/* EntityEnderman.attackEntityFrom indirect (arrow) EntityEnderman.java:371-381. */
MC_HD static inline int ml_enderman_arrow_hit(MlMob *m, ML_W *w) {
    JavaRandom er;
    int i;
    if (!m) return 0;
    er.seed = m->snap.seed48;
    for (i = 0; i < 64; ++i) {
        if (ml_enderman_teleport_randomly(m, w, &er)) {
            m->snap.seed48 = er.seed;
            return 1;
        }
    }
    m->snap.seed48 = er.seed;
    return 0;
}

MC_HD static inline void ml_enderman_set_target(RlSnapMob *s, int on,
                                                int ticks_existed) {
    if (!s) return;
    if (!on) {
        s->target_change_time = 0;
        s->screaming = 0;
        s->target_idx = 0;
    } else {
        s->target_change_time = ticks_existed;
        s->screaming = 1;
        s->target_idx = 1;
    }
}

/* EntityEnderman live tick. PathNavigateGround A* is a design gap
 * (GPU_MOB_AI.md): WanderAvoidWater / melee path consume no navigator
 * draws; chase is the generic straight line. */
MC_HD static inline void ml_enderman_ai(MlMob *m, ML_W *w,
                                        double px, double py, double pz,
                                        const MlEndCtx *ctx, MlAiOut *out) {
    RlSnapMob *s;
    JavaRandom er;
    int aggro = 0, moving = 0, jump = 0, wandering = 0;
    double dx, dy, dz, d, xz, dsq;
    float mw, mh;
    int wet, sub, daytime, feet;
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
    ++s->ticks_existed;
    er.seed = s->seed48;
    ehs_size((u8)s->type, &mw, &mh);
    dx = px - s->x;
    dy = py - s->y;
    dz = pz - s->z;
    d = sqrt(dx * dx + dy * dy + dz * dz);
    xz = sqrt(dx * dx + dz * dz);
    dsq = d * d;
    feet = ML_BLOCK(w, mc_floor(s->x), mc_floor(s->y), mc_floor(s->z));
    wet = ml_block_is_fluid(feet) || (ctx && ctx->raining);
    /* EntityEnderman.updateAITasks EntityEnderman.java:246-249:
     * isWet -> attackEntityFrom DROWN 1.0F. DROWN is unblockable so
     * attackEntityFrom.java:386-390 nextInt(10)!=0 then teleportRandomly. */
    if (wet) {
        s->health -= 1.0f;
        if (s->health < 0.0f) s->health = 0.0f;
        if (jrand_int_bound(&er, 10) != 0)
            (void)ml_enderman_teleport_randomly(m, w, &er);
    }
    /* World.calculateSkylightSubtracted then WorldProvider.java:450-453. */
    {
        i32 ti;
        float tf, tf1, ang;
        long long wt = ctx ? ctx->world_time : 0;
        ti = (i32)(wt % 24000LL);
        if (ti < 0) ti += 24000;
        tf = ((float)ti + 1.0f) / 24000.0f - 0.25f;
        if (tf < 0.0f) tf += 1.0f;
        if (tf > 1.0f) tf -= 1.0f;
        tf1 = 1.0f - (float)((cos((double)tf * MC_PI) + 1.0) / 2.0);
        ang = tf + (tf1 - tf) / 3.0f;
        tf1 = 1.0f - (float)(cos((double)ang * (double)MC_PI * 2.0) * 2.0 + 0.5);
        if (tf1 < 0.0f) tf1 = 0.0f;
        if (tf1 > 1.0f) tf1 = 1.0f;
        tf1 = 1.0f - tf1;
        tf1 = 1.0f - tf1;
        sub = (int)(tf1 * 11.0f);
    }
    daytime = sub < 4;                    /* WorldProvider.java:450-453 */
    /* EntityEnderman.java:251-260 daytime + brightness teleport. */
    if (daytime && s->ticks_existed >= s->target_change_time + 600) {
        float f = ml_entity_brightness(w, s->x, s->y, s->z, ML_ENDER_EYE);
        if (f > 0.5f && ml_sky_exposed(w, s->x, s->y, s->z) &&
            jrand_float(&er) * 30.0f < (f - 0.4f) * 2.0f) {
            ml_enderman_set_target(s, 0, s->ticks_existed);
            (void)ml_enderman_teleport_randomly(m, w, &er);
        }
    }
    /* AIFindPlayer EntityEnderman.java:432-538. No NAT nextInt(10).
     * PathNavigateGround A* out: after stare, chase is a straight line. */
    if (ctx && ml_enderman_should_attack(w, s, px, py, pz,
                                         ctx->yaw, ctx->pitch, ctx->helmet)) {
        if (s->find_aggro <= 0 && !s->screaming)
            s->find_aggro = 5;
        if (s->find_aggro > 0) --s->find_aggro;
        if (s->find_aggro <= 0)
            ml_enderman_set_target(s, 1, s->ticks_existed);
        if (s->screaming) {
            if (dsq < 16.0)
                (void)ml_enderman_teleport_randomly(m, w, &er);
            s->teleport_time = 0;
            aggro = 1;
        }
    } else if (s->screaming) {
        if (dsq > 256.0 && ++s->teleport_time >= 30) {
            if (ml_enderman_teleport_to_entity(m, w, &er, px, py, pz))
                s->teleport_time = 0;
        }
        aggro = 1;
    } else {
        s->find_aggro = 0;
        aggro = 0;
    }
    /* AITakeBlock / AIPlaceBlock EntityEnderman.java:541-625.
     * World write exists on magma and blaze (ML_SET_BLOCK). */
    if (ctx && ctx->griefing) {
        if (s->carried != 0) {
            if (jrand_int_bound(&er, 2000) == 0) {
                int i, j, k, here, down;
                i = mc_floor(s->x - 1.0 + jrand_double(&er) * 2.0);
                j = mc_floor(s->y + jrand_double(&er) * 2.0);
                k = mc_floor(s->z - 1.0 + jrand_double(&er) * 2.0);
                here = ML_BLOCK(w, i, j, k);
                down = ML_BLOCK(w, i, j - 1, k);
                if (here == 0 && down != 0 && ml_solid_id(down)) {
                    ML_SET_BLOCK(w, i, j, k, s->carried);
                    s->carried = 0;
                    s->carried_meta = 0;
                }
            }
        } else if (jrand_int_bound(&er, 20) == 0) {
            int i, j, k, id;
            i = mc_floor(s->x - 2.0 + jrand_double(&er) * 4.0);
            j = mc_floor(s->y + jrand_double(&er) * 3.0);
            k = mc_floor(s->z - 2.0 + jrand_double(&er) * 4.0);
            id = ML_BLOCK(w, i, j, k);
            if (ml_enderman_carriable(id) &&
                ml_los_clear(w,
                             (double)mc_floor(s->x) + 0.5, (double)j + 0.5,
                             (double)mc_floor(s->z) + 0.5,
                             (double)i + 0.5, (double)j + 0.5,
                             (double)k + 0.5)) {
                s->carried = id;
                s->carried_meta = ML_BLOCK_META(w, i, j, k);
                ML_SET_BLOCK(w, i, j, k, 0);
            }
        }
    }
    s->seed48 = er.seed;
    if (aggro && xz <= ML_REACH && fabs(dy) < 3.0) {
        s->wander_x = px;
        s->wander_z = pz;
        s->panic = 0;
        s->task_bits = EW_AI_ATTACK;
        s->yaw = ehs_yaw_toward(dx, dz);
        if (s->attack_time <= 0) {
            if (out) {
                out->hit_player = 1;
                out->hit_dmg = ml_melee_damage(EW_TYPE_ENDERMAN);
            }
            s->attack_time = ml_attack_cooldown(EW_TYPE_ENDERMAN);
        }
    } else if (aggro) {
        s->wander_x = px;
        s->wander_z = pz;
        s->panic = 0;
        s->task_bits = EW_AI_CHASE;
        moving = 1;
        s->yaw = ehs_yaw_toward(dx, dz);
    } else {
        s->task_bits = EW_AI_IDLE;
        wandering = 1;
        if (m->repath_timer > 0) --m->repath_timer;
        if (s->panic == 1) {
            double wx2 = s->wander_x - s->x, wz2 = s->wander_z - s->z;
            if (wx2 * wx2 + wz2 * wz2 < 1.0) s->panic = 0;
        }
        if (s->panic != 1 && m->repath_timer <= 0) {
            /* WanderAvoidWater A* out (GPU_MOB_AI.md). Hash wander like
             * the other generic hostiles. */
            u64 h;
            int ddx, ddz;
            m->repath_timer = ML_WANDER_INTERVAL;
            h = mc_hash_seed((u64)(ctx ? ctx->world_time : 0), s->ticks_existed,
                             s->slot, 0, 0, ML_WANDER_PURPOSE);
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
                }
            }
        }
        if (s->panic == 1) moving = 1;
    }
    s->target_idx = aggro ? 1 : 0;
    if (out) {
        out->moving = moving;
        out->jump = jump;
        out->wandering = wandering;
    }
}

/* EntityWitch.onLivingUpdate drink + EntityAIAttackRanged. PathNavigateGround
 * A* is a design gap (GPU_MOB_AI.md): chase is the generic straight line.
 * Splash EntityPotion is not a PlProj type (projectile_live.h type 1/2 only);
 * consume throw RNG on the witch stream, do not spawn a potion entity. */
MC_HD static inline void ml_witch_ai(MlMob *m, ML_W *w,
                                    double px, double py, double pz,
                                    const MlEndCtx *ctx, MlAiOut *out) {
    RlSnapMob *s;
    JavaRandom er;
    int aggro = 0, moving = 0, jump = 0, wandering = 0;
    int drinking, feet, eye, in_water, burning;
    double dx, dy, dz, d, xz, dsq;
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
    ++s->ticks_existed;
    er.seed = s->seed48;
    ehs_size((u8)s->type, &mw, &mh);
    dx = px - s->x;
    dy = py - s->y;
    dz = pz - s->z;
    d = sqrt(dx * dx + dy * dy + dz * dz);
    xz = sqrt(dx * dx + dz * dz);
    dsq = d * d;
    feet = ML_BLOCK(w, mc_floor(s->x), mc_floor(s->y), mc_floor(s->z));
    eye = ML_BLOCK(w, mc_floor(s->x), mc_floor(s->y + 1.62), mc_floor(s->z));
    in_water = ml_block_is_fluid(feet) || ml_block_is_fluid(eye);
    burning = m->fire_ticks > 0;
    drinking = s->witch_drink != ML_WITCH_DRINK_NONE;
    /* EntityWitch.onLivingUpdate EntityWitch.java:123-191. */
    if (drinking) {
        if (s->witch_attack_timer-- <= 0) {
            int kind = s->witch_drink;
            s->witch_drink = ML_WITCH_DRINK_NONE;
            s->witch_attack_timer = 0;
            if (kind == ML_WITCH_DRINK_WATER_BREATHING) {
                s->effect_id = ML_POTION_WATER_BREATHING;
                s->effect_duration = 3600; /* PotionType.java:72 */
                s->effect_amplifier = 0;
            } else if (kind == ML_WITCH_DRINK_FIRE_RESISTANCE) {
                s->effect_id = ML_POTION_FIRE_RESISTANCE;
                s->effect_duration = 3600; /* PotionType.java:65 */
                s->effect_amplifier = 0;
            } else if (kind == ML_WITCH_DRINK_SWIFTNESS) {
                s->effect_id = ML_POTION_SPEED;
                s->effect_duration = 3600; /* PotionType.java:67 */
                s->effect_amplifier = 0;
            } else if (kind == ML_WITCH_DRINK_HEALING) {
                /* Potion.affectEntity Instant Health I Potion.java:153:
                 * heal((int)(1.0 * (4 << 0) + 0.5)) = 4. */
                s->health += 4.0f;
                if (s->health > 26.0f) s->health = 26.0f;
            }
        }
    } else {
        int pick = ML_WITCH_DRINK_NONE;
        if (jrand_float(&er) < 0.15f && in_water &&
            !ml_witch_has_effect(s, ML_POTION_WATER_BREATHING))
            pick = ML_WITCH_DRINK_WATER_BREATHING; /* EntityWitch.java:155 */
        else if (jrand_float(&er) < 0.15f && burning &&
                 !ml_witch_has_effect(s, ML_POTION_FIRE_RESISTANCE))
            pick = ML_WITCH_DRINK_FIRE_RESISTANCE; /* :159 */
        else if (jrand_float(&er) < 0.05f && s->health < 26.0f)
            pick = ML_WITCH_DRINK_HEALING; /* :163 */
        else if (jrand_float(&er) < 0.5f && s->target_idx &&
                 !ml_witch_has_effect(s, ML_POTION_SPEED) && dsq > 121.0)
            pick = ML_WITCH_DRINK_SWIFTNESS; /* :167 */
        if (pick != ML_WITCH_DRINK_NONE) {
            s->witch_drink = pick;
            s->witch_attack_timer = ML_WITCH_DRINK_TICKS;
            (void)jrand_float(&er); /* drink sound :177 */
        }
    }
    (void)jrand_float(&er); /* 7.5E-4F setEntityState :184 */
    if (s->effect_duration > 0) {
        --s->effect_duration;
        if (s->effect_duration <= 0) {
            s->effect_id = 0;
            s->effect_amplifier = 0;
        }
    }
    drinking = s->witch_drink != ML_WITCH_DRINK_NONE;
    if (d <= ml_follow_range(EW_TYPE_WITCH))
        aggro = ml_los_clear(w, s->x, s->y + (double)mh * 0.85, s->z,
                             px, py + ML_EYE_HEIGHT, pz);
    if (aggro && xz <= ML_WITCH_THROW_RANGE && fabs(dy) < 3.0) {
        s->wander_x = px;
        s->wander_z = pz;
        s->panic = 0;
        s->task_bits = EW_AI_ATTACK;
        s->yaw = ehs_yaw_toward(dx, dz);
        if (!drinking && s->attack_time <= 0) {
            /* EntityWitch.attackEntityWithRangedAttack EntityWitch.java:238-267.
             * EntityPotion is not representable on PlProj; consume witch
             * draws only. EntityThrowable.setThrowableHeading gaussians are
             * on the new entity's rand (Entity.java unseeded) - skip. */
            double d1, d3, f;
            float php = ctx ? ctx->player_health : 20.0f;
            d1 = px + (ctx ? ctx->pmx : 0.0) - s->x;
            d3 = pz + (ctx ? ctx->pmz : 0.0) - s->z;
            f = (float)sqrt(d1 * d1 + d3 * d3);
            if (f >= 8.0f) {
                /* slowness :249; player has no effect table */
            } else if (php >= 8.0f) {
                /* poison :253 */
            } else if (f <= 3.0f && jrand_float(&er) < 0.25f) {
                /* weakness :257 */
            }
            (void)jrand_float(&er); /* throw sound :265 */
            s->attack_time = ml_attack_cooldown(EW_TYPE_WITCH);
        }
    } else if (aggro) {
        s->wander_x = px;
        s->wander_z = pz;
        s->panic = 0;
        s->task_bits = EW_AI_CHASE;
        moving = drinking ? 0 : 1;
        s->yaw = ehs_yaw_toward(dx, dz);
    } else {
        s->task_bits = EW_AI_IDLE;
        wandering = 1;
        if (m->repath_timer > 0) --m->repath_timer;
        if (s->panic == 1) {
            double wx2 = s->wander_x - s->x, wz2 = s->wander_z - s->z;
            if (wx2 * wx2 + wz2 * wz2 < 1.0) s->panic = 0;
        }
        if (s->panic != 1 && m->repath_timer <= 0) {
            u64 h;
            int ddx, ddz;
            m->repath_timer = ML_WANDER_INTERVAL;
            h = mc_hash_seed((u64)(ctx ? ctx->world_time : 0), s->ticks_existed,
                             s->slot, 0, 0, ML_WANDER_PURPOSE);
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
                }
            }
        }
        if (s->panic == 1 && !drinking) moving = 1;
    }
    s->target_idx = aggro ? 1 : 0;
    s->seed48 = er.seed;
    if (out) {
        out->moving = moving;
        out->jump = jump;
        out->wandering = wandering;
    }
}

/* Generic (det_entity_rng off) hostile body for zombie/skeleton/creeper/spider/enderman/witch.
 * Magma gm_mobs_tick inner branch, magma/game/mob_live.c. */
MC_HD static inline void ml_hostile_ai(MlMob *m, ML_W *w,
                                       double px, double py, double pz,
                                       int day, long long seed, long long tick,
                                       const MlEndCtx *ectx, MlAiOut *out) {
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
    if (type == EW_TYPE_SLIME) {
        ml_slime_ai(m, w, px, py, pz, out);
        return;
    }
    if (type == EW_TYPE_ENDERMAN) {
        ml_enderman_ai(m, w, px, py, pz, ectx, out);
        return;
    }
    if (type == EW_TYPE_WITCH) {
        ml_witch_ai(m, w, px, py, pz, ectx, out);
        return;
    }
    (void)day;
    dx = px - s->x;
    dy = py - s->y;
    dz = pz - s->z;
    d = sqrt(dx * dx + dy * dy + dz * dz);
    xz = sqrt(dx * dx + dz * dz);
    ehs_size_scaled((u8)type, ml_slime_size(s), &mw, &mh);
    if (d <= ml_follow_range(type))
        aggro = ml_los_clear(w, s->x, s->y + (double)mh * 0.85, s->z,
                             px, py + ML_EYE_HEIGHT, pz);
    /* EntitySpider.AISpiderTarget shouldExecute EntitySpider.java:278-282:
     * brightness>=0.5F -> do not start. AISpiderAttack.continueExecuting
     * :247-260: brightness>=0.5F && nextInt(100)==0 drop target. */
    if (type == EW_TYPE_SPIDER) {
        float br = ml_entity_brightness(w, s->x, s->y, s->z, 0.65f);
        if (br >= 0.5f) {
            if (aggro && s->target_idx) {
                JavaRandom er;
                er.seed = s->seed48;
                if (jrand_int_bound(&er, 100) == 0)
                    aggro = 0;
                s->seed48 = er.seed;
            } else {
                aggro = 0;
            }
        }
    }
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
    if (ml_is_slimey(s->type)) {
        float sw, sh;
        ehs_size_scaled((u8)s->type, ml_slime_size(s), &sw, &sh);
        elb_init(&liv, sw, sh, s->x, s->y, s->z);
        liv.base.phys.motionX = s->mx;
        liv.base.phys.motionY = s->my;
        liv.base.phys.motionZ = s->mz;
        liv.base.phys.onGround = s->on_ground ? 1 : 0;
        liv.base.rotationYaw = intent.yaw;
        liv.isServerWorld = 1;
    }
    liv.moveForward = intent.moveForward;
    liv.moveStrafing = intent.moveStrafing;
    liv.isJumping = intent.isJumping;
    liv.landMovementFactor = ehs_land_speed_of((u8)s->type, ml_slime_size(s));
    if (s->type == EW_TYPE_ENDERMAN && s->screaming)
        liv.landMovementFactor += 0.15000000596046448f; /* ATTACKING_SPEED_BOOST */
    if (s->type == EW_TYPE_WITCH && s->witch_drink)
        liv.landMovementFactor = 0.0f; /* EntityWitch.java:48 MODIFIER -0.25 */
    if (s->type == EW_TYPE_SLIME && !moving)
        liv.landMovementFactor = 0.0f;
    liv.onLadder = ml_spider_climbing(s) ? 1 : 0;
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
    /* EntitySpider.onUpdate after super: setBesideClimbableBlock(collidedHorizontally)
     * EntitySpider.java:91-98. Used next tick as isOnLadder. */
    if (s->type == EW_TYPE_SPIDER)
        ml_spider_set_climbing(s, liv.base.phys.collidedHorizontally);
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
