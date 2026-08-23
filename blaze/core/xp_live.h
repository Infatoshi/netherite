/* xp_live.h - EntityXPOrb lifecycle + EntityPlayer addExperience.
 *
 * Magma magma/game/mob_live.c and blaze-CPU/CUDA compile this one source.
 * Magma wrappers stay thin; do not re-derive tick_xp_orbs.
 *
 * Java 1.11.2 (java/oracle-src):
 *   EntityXPOrb.getXPSplit                 EntityXPOrb.java:298-301
 *   EntityXPOrb.onUpdate                   EntityXPOrb.java:87-174
 *     delayBeforeCanPickup-- (:91-94)
 *     gravity (double)0.03F = 0.029999999329447746 (:100-103)
 *     attraction 8-block / eid color gate (:114-146)
 *     move + drag 0.98F / ground bounce -0.9F (:148-165)
 *     xpOrbAge >= 6000 setDead (:167-173)
 *   EntityXPOrb.onCollideWithPlayer        EntityXPOrb.java:239-265
 *     delay==0 && xpCooldown==0; xpCooldown=2; addExperience; setDead
 *   EntityPlayer.onUpdate xpCooldown--     EntityPlayer.java:223-226
 *   EntityPlayer.addExperience             EntityPlayer.java:2145-2162
 *   EntityPlayer.addExperienceLevel        EntityPlayer.java:2186-2203
 *   EntityPlayer.xpBarCap                  EntityPlayer.java:2209-2211
 *     >=30: 112+(level-30)*9; >=15: 37+(level-15)*5; else 7+level*2
 *   EntityLiving.onDeathUpdate XP split    EntityLivingBase.java:419-434
 *   EntityLiving.getExperiencePoints       EntityLiving.java:274-300
 *   EntityMob.experienceValue              EntityMob.java:27  (=5)
 *   Block.dropXpOnBlockBreak               Block.java:731-741
 *   BlockOre.getExpDrop                    BlockOre.java:76-99
 *
 * Magma extras (M1 is magma semantics; do not "fix" to Java here):
 *   spawn motion is hash(seed^eid), not Math.random (EntityXPOrb.java:40-43)
 *   no lava branch, no pushOutOfBlocks, no handleWaterMovement
 *     (EntityXPOrb.java:105-113, :179-182 via Entity.onEntityUpdate)
 *   no Mending drain (EntityXPOrb.java:248-255)
 *   no addScore (EntityPlayer.java:2147)
 *   collision is AABB intersect after eo_tick, not World entity list
 *   block-break fortune rand is hash(seed^pos), not World.rand
 *
 * Include after player_survival.h / entity_xp_orb.h.
 */
#ifndef MC_XP_LIVE_H
#define MC_XP_LIVE_H

#include <string.h>

#include "mc.h"
#include "mc_math.h"
#ifndef XL_NO_ORBS
#include "entity_xp_orb.h"
#endif

typedef struct {
    int xpCooldown;
    float experience;
    int experienceLevel;
    int experienceTotal;
} XlPlayer;

#ifndef XL_MAX
#define XL_MAX 64
#endif

#define XL_ATTRACT 8.0                    /* EntityXPOrb.java:114 */
#define XL_DESPAWN 6000                   /* EntityXPOrb.java:170 */
#define XL_COOLDOWN 2                     /* EntityXPOrb.java:246 */
#define XL_MOB_XP 5                       /* EntityMob.java:27 */
#define XL_SPLIT_N 11

/* EntityXPOrb.getXPSplit EntityXPOrb.java:298-301. Function not array:
 * nvcc rejects host static const tables in device code. */
MC_HD static inline int xl_split_at(int i) {
    if (i == 0) return 2477;
    if (i == 1) return 1237;
    if (i == 2) return 617;
    if (i == 3) return 307;
    if (i == 4) return 149;
    if (i == 5) return 73;
    if (i == 6) return 37;
    if (i == 7) return 17;
    if (i == 8) return 7;
    if (i == 9) return 3;
    return 1;
}

MC_HD static inline int xl_xp_split(int value) {
    int i;
    if (value <= 0) return 1;
    for (i = 0; i < XL_SPLIT_N; ++i)
        if (value >= xl_split_at(i)) return xl_split_at(i);
    return 1;
}

/* EntityPlayer.xpBarCap EntityPlayer.java:2209-2211. */
MC_HD static inline int xl_xp_bar_cap(int level) {
    if (level >= 30) return 112 + (level - 30) * 9;
    if (level >= 15) return 37 + (level - 15) * 5;
    return 7 + level * 2;
}

/* EntityMob.java:27. Magma mob_drop keeps per-type overrides. */
MC_HD static inline int xl_mob_xp(int type) {
    (void)type;
    return XL_MOB_XP;
}

/* BlockOre.getExpDrop BlockOre.java:83-99. Magma extra: hash not World.rand.
 * coal 0..2, diamond/emerald 3..7, lapis 2..5, quartz 2..5, redstone 1..5. */
MC_HD static inline int xl_ore_xp(int id, u64 h) {
    unsigned r = (unsigned)(h & 0xffffu);
    if (id == 16) return (int)(r % 3u);                 /* coal 0..2 */
    if (id == 56 || id == 129) return 3 + (int)(r % 5u); /* diamond/emerald */
    if (id == 21 || id == 153) return 2 + (int)(r % 4u); /* lapis/quartz */
    if (id == 73 || id == 74) return 1 + (int)(r % 5u);  /* redstone */
    return 0;
}

MC_HD static inline void xl_add_experience_level(XlPlayer *p, int levels) {
    if (!p) return;
    p->experienceLevel += levels;                       /* java:2188 */
    if (p->experienceLevel < 0) {                       /* java:2190-2195 */
        p->experienceLevel = 0;
        p->experience = 0.0f;
        p->experienceTotal = 0;
    }
}

/* EntityPlayer.addExperience EntityPlayer.java:2145-2162. */
MC_HD static inline void xl_add_experience(XlPlayer *p, int amount) {
    int cap, room;
    if (!p || amount <= 0) return;
    room = 2147483647 - p->experienceTotal;             /* java:2148 */
    if (amount > room) amount = room;                   /* java:2150-2153 */
    cap = xl_xp_bar_cap(p->experienceLevel);
    if (cap <= 0) cap = 1;
    p->experience += (float)amount / (float)cap;        /* java:2155 */
    p->experienceTotal += amount;                       /* java:2157 init */
    while (p->experience >= 1.0f) {                     /* java:2157 cond */
        cap = xl_xp_bar_cap(p->experienceLevel);
        p->experience = (p->experience - 1.0f) * (float)cap; /* java:2159 */
        xl_add_experience_level(p, 1);                  /* java:2160 */
        cap = xl_xp_bar_cap(p->experienceLevel);
        if (cap <= 0) cap = 1;
        p->experience /= (float)cap;                    /* java:2157 incr */
    }
}

/* EntityPlayer.java:223-226. */
MC_HD static inline void xl_player_tick(XlPlayer *p) {
    if (p && p->xpCooldown > 0) --p->xpCooldown;
}

#ifndef XL_NO_ORBS
MC_HD static inline void xl_clear_orb(McOrb *o) {
    memset(o, 0, sizeof *o);
    o->dead = 1;
}

/* Magma gm_mobs_spawn_xp motion: hash(seed^eid), motionY=0.2. Caller
 * fills motion after this if it wants a different draw. */
MC_HD static inline int xl_spawn(McOrb *orbs, int cap, int *next_id,
                                 signed char *dim, signed char cur_dim,
                                 double x, double y, double z, int value,
                                 double mx, double my, double mz) {
    int spawned = 0;
    if (!orbs || cap <= 0 || value <= 0) return 0;
    while (value > 0) {
        int slot = -1, i, amount;
        McOrb *o;
        for (i = 0; i < cap; ++i)
            if (orbs[i].dead || orbs[i].xpValue <= 0) { slot = i; break; }
        if (slot < 0) return spawned;
        amount = xl_xp_split(value);
        value -= amount;
        o = &orbs[slot];
        memset(o, 0, sizeof *o);
        o->xpValue = amount;
        o->eid = next_id ? (*next_id)++ : 1000;
        o->delayBeforeCanPickup = 0;
        o->motionX = mx;
        o->motionY = my;
        o->motionZ = mz;
        if (dim) dim[slot] = cur_dim;
        eo_set_position(o, x, y, z);
        o->motionX = mx;
        o->motionY = my;
        o->motionZ = mz;
        ++spawned;
    }
    return spawned;
}

/* Fixture plant: explicit bits, no hash. delay 10 evidences the countdown. */
MC_HD static inline int xl_plant(McOrb *orbs, int cap, signed char *dim,
                                 signed char cur_dim,
                                 double x, double y, double z,
                                 int value, int delay, int eid) {
    int slot = -1, i;
    McOrb *o;
    if (!orbs || cap <= 0 || value <= 0) return -1;
    for (i = 0; i < cap; ++i)
        if (orbs[i].dead || orbs[i].xpValue <= 0) { slot = i; break; }
    if (slot < 0) return -1;
    o = &orbs[slot];
    memset(o, 0, sizeof *o);
    o->xpValue = value;
    o->eid = eid;
    o->delayBeforeCanPickup = delay;
    o->motionY = 0.2;
    if (dim) dim[slot] = cur_dim;
    eo_set_position(o, x, y, z);
    return slot;
}

/* EntityXPOrb.onCollideWithPlayer EntityXPOrb.java:239-265 minus Mending. */
MC_HD static inline int xl_try_pickup(McOrb *o, XlPlayer *p,
                                      const McAABB *player) {
    if (!o || !p || o->dead || o->xpValue <= 0) return 0;
    if (o->delayBeforeCanPickup > 0 || p->xpCooldown > 0) return 0;
    if (!mc_aabb_intersects(&o->box, player)) return 0;
    p->xpCooldown = XL_COOLDOWN;
    xl_add_experience(p, o->xpValue);
    o->dead = 1;
    o->xpValue = 0;
    return 1;
}
#endif /* XL_NO_ORBS */

#endif /* MC_XP_LIVE_H */
