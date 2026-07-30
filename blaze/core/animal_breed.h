/* animal_breed: passive animal age / love / breed TIMERS (not full AI), ported bit-for-bit from
 * decompiled MC 1.11.2. One MC_HD source, CPU==CUDA, verbatim-Java golden.
 *
 * Sources (java/oracle-src/net/minecraft):
 *   entity/EntityAgeable.java
 *     growingAge tick in onLivingUpdate server branch (210-226): i<0 -> ++i / onGrowingAdult at 0;
 *       i>0 -> --i; setGrowingAge (151-156); isChild = age<0 (241-244);
 *       setScaleForAge child?0.5F:1.0F (249-252); createChild baby age -24000 (49).
 *   entity/passive/EntityAnimal.java
 *     inLove field (22); onLivingUpdate: if age!=0 inLove=0, else if inLove>0 --inLove (44-65);
 *     setInLove -> inLove=600 (194-199); resetInLove (214-217); isInLove = inLove>0 (209-212);
 *     processInteract adult feed: age==0 && inLove<=0 -> setInLove (165-169);
 *     canMateWith: other!=self, same class, both isInLove (222-225).
 *   entity/ai/EntityAIMate.java
 *     spawnBabyDelay >= 60 && distSq < 9.0D -> spawnBaby (82-85);
 *     spawnBaby: parents setGrowingAge(6000), resetInLove, child setGrowingAge(-24000) (148-153).
 *
 * Scope: two fixed-position animals + optional child slot. No pathfinding / navigator / particles /
 * XP / player stats. Scripted tape drives feed events; mate delay counts while both in love and
 * close enough. Emit age/inLove/isChild per animal (3 slots) per tick.
 *
 * All arithmetic is int/float exactly as Java; -ffp-contract=off / --fmad=false. */
#ifndef MC_ANIMAL_BREED_H
#define MC_ANIMAL_BREED_H

#include "mc.h"

/* EntityAnimal.setInLove (196): this.inLove = 600 */
#define AB_IN_LOVE_TICKS     600
/* EntityAIMate.spawnBaby parents (148-149): setGrowingAge(6000) */
#define AB_BREED_COOLDOWN    6000
/* EntityAIMate.spawnBaby / EntityAgeable spawn-egg child (152 / 49): setGrowingAge(-24000) */
#define AB_CHILD_AGE        (-24000)
/* EntityAIMate.updateTask (82): spawnBabyDelay >= 60 */
#define AB_MATE_DELAY        60
/* EntityAIMate.updateTask (82): getDistanceSqToEntity < 9.0D */
#define AB_MATE_DIST_SQ      9.0f
/* Slots: 0/1 parents, 2 child (spawned on breed). */
#define AB_SLOTS             3

typedef struct {
    i32 present;      /* 1 if entity exists this tick */
    i32 growingAge;   /* EntityAgeable.growingAge */
    i32 inLove;       /* EntityAnimal.inLove */
    float scale;      /* setScaleForAge: child 0.5F else 1.0F */
    float x, y, z;    /* fixed position (no pathfinding) */
} AbAnimal;

typedef struct {
    AbAnimal a[AB_SLOTS];
    i32 spawn_baby_delay; /* EntityAIMate.spawnBabyDelay */
} AbState;

/* EntityAgeable.isChild (241-244). */
MC_HD static inline int ab_is_child(const AbAnimal *e) {
    return e->growingAge < 0;
}

/* EntityAgeable.setScaleForAge (249-252). */
MC_HD static inline void ab_set_scale_for_age(AbAnimal *e, int child) {
    e->scale = child ? 0.5f : 1.0f;
}

/* EntityAgeable.setGrowingAge (151-156): BABY flag + growingAge + setScaleForAge(isChild). */
MC_HD static inline void ab_set_growing_age(AbAnimal *e, i32 age) {
    e->growingAge = age;
    ab_set_scale_for_age(e, age < 0);
}

/* EntityAgeable.onGrowingAdult (234-236): empty base; hook retained for fidelity. */
MC_HD static inline void ab_on_growing_adult(AbAnimal *e) {
    (void)e;
}

/* EntityAnimal.resetInLove (214-217). */
MC_HD static inline void ab_reset_in_love(AbAnimal *e) {
    e->inLove = 0;
}

/* EntityAnimal.isInLove (209-212). */
MC_HD static inline int ab_is_in_love(const AbAnimal *e) {
    return e->inLove > 0;
}

/* EntityAnimal.setInLove (194-199): inLove = 600 (player ref / particles out of scope). */
MC_HD static inline void ab_set_in_love(AbAnimal *e) {
    e->inLove = AB_IN_LOVE_TICKS;
}

/* Adult food right-click (EntityAnimal.processInteract 165-169): age==0 && inLove<=0 -> setInLove.
 * Returns 1 if love was set. Child growth-via-food path is out of scope for this timer kernel. */
MC_HD static inline int ab_try_feed(AbAnimal *e) {
    if (!e->present) return 0;
    if (e->growingAge == 0 && e->inLove <= 0) {
        ab_set_in_love(e);
        return 1;
    }
    return 0;
}

/* EntityAgeable.onLivingUpdate server branch (210-226). */
MC_HD static inline void ab_age_tick(AbAnimal *e) {
    i32 i = e->growingAge;
    if (i < 0) {
        ++i;
        ab_set_growing_age(e, i);
        if (i == 0)
            ab_on_growing_adult(e);
    } else if (i > 0) {
        --i;
        ab_set_growing_age(e, i);
    }
}

/* EntityAnimal.onLivingUpdate love half AFTER super age tick (48-65):
 * if age != 0: inLove = 0; if inLove > 0: --inLove. Particles omitted. */
MC_HD static inline void ab_love_tick(AbAnimal *e) {
    if (e->growingAge != 0)
        e->inLove = 0;
    if (e->inLove > 0)
        --e->inLove;
}

/* One living-update for a present animal: age then love (super first, then EntityAnimal). */
MC_HD static inline void ab_living_update(AbAnimal *e) {
    if (!e->present) return;
    ab_age_tick(e);
    ab_love_tick(e);
}

/* Entity.getDistanceSqToEntity: (dx*dx + dy*dy + dz*dz). */
MC_HD static inline float ab_dist_sq(const AbAnimal *a, const AbAnimal *b) {
    float dx = a->x - b->x;
    float dy = a->y - b->y;
    float dz = a->z - b->z;
    return dx * dx + dy * dy + dz * dz;
}

/* EntityAnimal.canMateWith (222-225): other!=self, same class (always here), both isInLove. */
MC_HD static inline int ab_can_mate_with(const AbAnimal *self, const AbAnimal *other) {
    if (self == other) return 0;
    if (!self->present || !other->present) return 0;
    return ab_is_in_love(self) && ab_is_in_love(other);
}

/* EntityAIMate.spawnBaby (148-153), forge event not cancelled, XP/particles omitted. */
MC_HD static inline void ab_spawn_baby(AbState *s) {
    AbAnimal *p0 = &s->a[0];
    AbAnimal *p1 = &s->a[1];
    AbAnimal *ch = &s->a[2];
    ab_set_growing_age(p0, AB_BREED_COOLDOWN);
    ab_set_growing_age(p1, AB_BREED_COOLDOWN);
    ab_reset_in_love(p0);
    ab_reset_in_love(p1);
    ch->present = 1;
    ch->x = p0->x;
    ch->y = p0->y;
    ch->z = p0->z;
    ch->inLove = 0;
    ab_set_growing_age(ch, AB_CHILD_AGE);
}

/* Mate progress after both living updates (EntityAIMate.updateTask 76-85, no navigator).
 * Counts spawnBabyDelay while both in love and close; at delay>=60 && distSq<9 spawn. */
MC_HD static inline void ab_mate_tick(AbState *s) {
    AbAnimal *p0 = &s->a[0];
    AbAnimal *p1 = &s->a[1];
    if (!ab_can_mate_with(p0, p1)) {
        s->spawn_baby_delay = 0;
        return;
    }
    float dsq = ab_dist_sq(p0, p1);
    /* continueExecuting needs mate in love; delay advances only while task active. Distance
     * gate for spawn is checked at delay>=60 (EntityAIMate:82). We still require closeness
     * to keep counting so a far pair never creeps toward breed without pathfinding. */
    if (dsq >= AB_MATE_DIST_SQ) {
        s->spawn_baby_delay = 0;
        return;
    }
    ++s->spawn_baby_delay;
    if (s->spawn_baby_delay >= AB_MATE_DELAY && dsq < AB_MATE_DIST_SQ) {
        ab_spawn_baby(s);
        s->spawn_baby_delay = 0;
    }
}

/* Full world tick for the pair (+ child aging if present): living updates then mate. */
MC_HD static inline void ab_world_tick(AbState *s) {
    for (i32 i = 0; i < AB_SLOTS; ++i)
        ab_living_update(&s->a[i]);
    ab_mate_tick(s);
}

/* Initial scene: A0 child age=-40 at (0,64,0); A1 adult at (1,64,0) distSq=1 < 9; no child yet. */
MC_HD static inline void ab_init(AbState *s) {
    for (i32 i = 0; i < AB_SLOTS; ++i) {
        s->a[i].present = 0;
        s->a[i].growingAge = 0;
        s->a[i].inLove = 0;
        s->a[i].scale = 1.0f;
        s->a[i].x = 0.0f;
        s->a[i].y = 64.0f;
        s->a[i].z = 0.0f;
    }
    s->spawn_baby_delay = 0;

    s->a[0].present = 1;
    ab_set_growing_age(&s->a[0], -40);   /* child; becomes adult after 40 age ticks */
    s->a[0].x = 0.0f;
    s->a[0].y = 64.0f;
    s->a[0].z = 0.0f;

    s->a[1].present = 1;
    ab_set_growing_age(&s->a[1], 0);     /* adult ready to love */
    s->a[1].x = 1.0f;
    s->a[1].y = 64.0f;
    s->a[1].z = 0.0f;
}

/* Scripted tape (no RNG): fixed feed schedule so child growth, failed child-feed, adult love,
 * mate delay, breed, parent cooldown, and child age countdown all appear in one short run.
 *   tick 5:  try feed A0 while child  -> no-op (age != 0)
 *   tick 50: try feed A0 after adult  -> inLove=600 (age hit 0 at living-update of tick 39)
 *   tick 55: try feed A1              -> inLove=600
 * Then mate delay runs while both love + close; breed at delay 60.
 * seed is accepted for driver signature parity (schedule is pure in tick). */
MC_HD static inline void ab_tape_tick(AbState *s, i64 seed, i32 tick) {
    (void)seed;
    if (tick == 5)
        ab_try_feed(&s->a[0]);
    if (tick == 50)
        ab_try_feed(&s->a[0]);
    if (tick == 55)
        ab_try_feed(&s->a[1]);
    ab_world_tick(s);
}

#endif /* MC_ANIMAL_BREED_H */
