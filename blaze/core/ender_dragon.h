/* ender_dragon: Ender Dragon phase state machine + crystal heal subset on a fixed End arena.
 * PORT TARGET: ref/netherite-csrc/src/dragon.cpp (read-only reference; internal CPU==CUDA).
 * Behavioral ref: entity/boss/EntityDragon fight loop (phase 0 circle / 1 strafe / 2 hover).
 *
 * Fixed arena: 10 obsidian-pillar ender crystals (netherite PILLARS layout), one player, one
 * dragon. No block destruction or contact-damage side effects (deferred to full-world wiring).
 * Deterministic per-tick dump over ED_NUM_TICKS; seed varies initial HP, phase_ticks offset, and
 * a mid-run crystal destruction event. Build with -ffp-contract=off / CUDA --fmad=false. */
#ifndef MC_ENDER_DRAGON_H
#define MC_ENDER_DRAGON_H

#include <math.h>
#include <string.h>
#include "mc.h"
#include "mc_math.h"
#include "mc_rng.h"

#define ED_NUM_TICKS       200
#define ED_NUM_CRYSTALS    10
#define ED_DRAGON_MAX_HP   200.0f
#define ED_STRAFE_RANGE_SQ (40.0 * 40.0)
#define ED_DUMP_FIELDS     10

enum {
    ED_PHASE_CIRCLE = 0,
    ED_PHASE_STRAFE = 1,
    ED_PHASE_HOVER  = 2
};

typedef struct {
    double x, y, z;
    u8     alive;
} EdCrystal;

typedef struct {
    double x, y, z;
    u8     alive;
} EdPlayer;

typedef struct {
    double x, y, z;
    double vx, vy, vz;
    double target_x, target_y, target_z;
    double head_x, head_y, head_z;
    float  health;
    float  max_health;
    float  yaw;
    i32    phase;
    i32    phase_ticks;
    i32    ticks_existed;
    i32    death_ticks;
    i32    heal_crystal_idx;
    JavaRandom rand;
    u8     alive;
} EdDragon;

typedef struct {
    EdDragon  dragon;
    EdCrystal crystals[ED_NUM_CRYSTALS];
    EdPlayer  player;
    i64       tick;
    u64       seed;
} EdArena;

/* Netherite End pillar layout (crystal y = height + 2). */
MC_HD static inline void ed_pillar_crystal_pos(int idx, double *cx, double *cy, double *cz) {
    static const int px[] = { 42,  0, -42,  0,  30, -30, -30,  30,  21, -21 };
    static const int pz[] = {  0, 42,   0, -42,  30,  30, -30, -30,  21,  21 };
    static const int ph[] = { 76, 79,  82,  85,  88,  91,  94,  97, 100, 103 };
    *cx = (double)px[idx] + 0.5;
    *cy = (double)(ph[idx] + 2);
    *cz = (double)pz[idx] + 0.5;
}

MC_HD static inline double ed_closest_player_dist_sq(const EdArena *a, double x, double y, double z) {
    if (!a->player.alive) return 1.0e18;
    double dx = a->player.x - x;
    double dy = a->player.y - y;
    double dz = a->player.z - z;
    return dx * dx + dy * dy + dz * dz;
}

/* World.getEntitiesWithinAABB(EntityEnderCrystal.class,
 * dragonBB.expandXyz(32)). EntityDragon is 16x8 and EntityEnderCrystal is
 * 2x2, so intersecting center bounds are x/z strictly within +/-41 and y
 * strictly within (-34,+40). Index order is arena/spawn order; Java keeps the
 * first crystal on an exact distance tie. */
MC_HD static inline int ed_crystal_in_heal_query(
        const EdDragon *d, const EdCrystal *c) {
    double x=c->x-d->x,y=c->y-d->y,z=c->z-d->z;
    return x>-41.0&&x<41.0&&y>-34.0&&y<40.0&&z>-41.0&&z<41.0;
}

MC_HD static inline int ed_find_nearest_healing_crystal(const EdArena *a) {
    const EdDragon *d=&a->dragon;int best=-1;double best_d2=1.0e18;
    for(int i=0;i<ED_NUM_CRYSTALS;++i){
        const EdCrystal *c=&a->crystals[i];
        if(!c->alive||!ed_crystal_in_heal_query(d,c))continue;
        double x=c->x-d->x,y=c->y-d->y,z=c->z-d->z;
        double d2=x*x+y*y+z*z;
        if(d2<best_d2){best=i;best_d2=d2;}
    }
    return best;
}

/* Exact bounded EntityDragon.updateDragonEnderCrystal transition. The
 * current reference heals before the one-in-ten reselection draw; it is not
 * range-cleared between draws, only death-cleared. The caller owns the shared
 * Entity rand ordering around this transition. */
MC_HD static inline void ed_update_healing_crystal(EdArena *a) {
    EdDragon *d=&a->dragon;int i=d->heal_crystal_idx;
    if(i>=0&&i<ED_NUM_CRYSTALS){
        if(!a->crystals[i].alive)d->heal_crystal_idx=-1;
        else if(d->ticks_existed%10==0&&d->health<d->max_health){
            d->health+=1.0f;
            if(d->health>d->max_health)d->health=d->max_health;
        }
    }else d->heal_crystal_idx=-1;
    if(jrand_int_bound(&d->rand,10)==0)
        d->heal_crystal_idx=ed_find_nearest_healing_crystal(a);
}

/* EntityEnderCrystal.attackEntityFrom marks the crystal dead and runs its
 * size-six explosion before DragonFightManager.onCrystalDestroyed reaches
 * EntityDragon. Keep the two operations separate so product callers can
 * preserve that synchronous ordering around their world explosion.
 *
 * The represented player is either the attacking player itself or the
 * nearest attackable-player result used for a non-player damage source.
 * Creative/spectator filtering is supplied by the product caller. */
MC_HD static inline int ed_mark_crystal_destroyed(EdArena *a, int index) {
    if (!a || index < 0 || index >= ED_NUM_CRYSTALS
            || !a->crystals[index].alive)
        return 0;
    a->crystals[index].alive = 0;
    return 1;
}

MC_HD static inline void ed_on_crystal_destroyed(
        EdArena *a, int index, int source_is_player,
        int player_can_be_targeted) {
    if (!a || index < 0 || index >= ED_NUM_CRYSTALS) return;
    EdDragon *d = &a->dragon;
    const EdCrystal *c = &a->crystals[index];
    if (d->alive && d->death_ticks == 0 && d->heal_crystal_idx == index) {
        d->health -= 10.0f;
        if (d->health < 0.0f) d->health = 0.0f;
    }
    if (d->alive && d->death_ticks == 0 && d->phase == ED_PHASE_CIRCLE
            && a->player.alive && player_can_be_targeted) {
        double dx = a->player.x - c->x;
        double dy = a->player.y - c->y;
        double dz = a->player.z - c->z;
        /* A player damage source is used directly. Otherwise vanilla asks
         * for the nearest attackable player within 64 horizontal/vertical. */
        if (source_is_player
                || (dx * dx + dz * dz <= 64.0 * 64.0 && fabs(dy) <= 64.0)) {
            d->phase = ED_PHASE_STRAFE;
            d->phase_ticks = 0;
            d->target_x = a->player.x;
            d->target_y = a->player.y + 10.0;
            d->target_z = a->player.z;
        }
    }
}

MC_HD static inline void ed_tick_dragon(EdArena *a, const McSinTable *st) {
    EdDragon *d = &a->dragon;
    if (!d->alive) return;

    if (d->death_ticks > 0) {
        d->death_ticks++;
        if (d->death_ticks >= 200)
            d->alive = 0;
        return;
    }

    if (d->health <= 0.0f) {
        d->death_ticks = 1;
        return;
    }

    d->ticks_existed++;
    d->phase_ticks++;
    ed_update_healing_crystal(a);
    int crystal_idx=d->heal_crystal_idx;
    EdCrystal *crystal=(crystal_idx>=0&&crystal_idx<ED_NUM_CRYSTALS
                        &&a->crystals[crystal_idx].alive)
                       ?&a->crystals[crystal_idx]:NULL;

    switch (d->phase) {
    case ED_PHASE_CIRCLE:
        if (d->phase_ticks > 200) {
            double pd2 = ed_closest_player_dist_sq(a, d->x, d->y, d->z);
            if (pd2 < ED_STRAFE_RANGE_SQ) {
                d->phase = ED_PHASE_STRAFE;
                d->phase_ticks = 0;
            } else {
                double angle = (double)(d->phase_ticks % 360) * 0.017453292;
                d->target_x = cos(angle) * 60.0;
                d->target_y = 70.0 + sin(angle * 0.5) * 20.0;
                d->target_z = sin(angle) * 60.0;
                d->phase_ticks = 0;
            }
        }
        break;

    case ED_PHASE_STRAFE:
        if (d->phase_ticks > 80) {
            d->phase = ED_PHASE_CIRCLE;
            d->phase_ticks = 0;
        } else if (a->player.alive) {
            d->target_x = a->player.x;
            d->target_y = a->player.y + 10.0;
            d->target_z = a->player.z;
        }
        break;

    case ED_PHASE_HOVER:
        if (d->phase_ticks > 100 || !crystal) {
            d->phase = ED_PHASE_CIRCLE;
            d->phase_ticks = 0;
        }
        break;
    }

    double mdx = d->target_x - d->x;
    double mdy = d->target_y - d->y;
    double mdz = d->target_z - d->z;
    double dist = sqrt(mdx * mdx + mdy * mdy + mdz * mdz);

    if (dist > 1.0) {
        double speed = 0.6;
        if (dist < speed) speed = dist;
        d->vx = (mdx / dist) * speed;
        d->vy = (mdy / dist) * speed;
        d->vz = (mdz / dist) * speed;
    } else {
        d->vx *= 0.9;
        d->vy *= 0.9;
        d->vz *= 0.9;
    }

    d->x += d->vx;
    d->y += d->vy;
    d->z += d->vz;

    float yaw_rad = d->yaw * 0.017453292f;
    d->head_x = d->x - (double)mc_sin(st, yaw_rad) * 6.0;
    d->head_y = d->y + 3.0;
    d->head_z = d->z + (double)mc_cos(st, yaw_rad) * 6.0;

    if (fabs(d->vx) > 0.01 || fabs(d->vz) > 0.01)
        d->yaw = (float)(atan2(-d->vx, d->vz) * 180.0 / MC_PI);
}

MC_HD static inline void ed_init(EdArena *a, u64 seed) {
    a->seed = seed;
    a->tick = 0;

    EdDragon *d = &a->dragon;
    d->x = 0.0;
    d->y = 100.0;
    d->z = 0.0;
    d->vx = d->vy = d->vz = 0.0;
    d->target_x = 0.0;
    d->target_y = 80.0;
    d->target_z = 50.0;
    d->head_x = d->head_y = d->head_z = 0.0;
    d->max_health = ED_DRAGON_MAX_HP;
    d->health = ED_DRAGON_MAX_HP - (float)(seed % 41u);
    d->yaw = 0.0f;
    d->phase = ED_PHASE_CIRCLE;
    d->phase_ticks = (i32)(seed % 180u);
    d->ticks_existed = 0;
    d->death_ticks = 0;
    d->heal_crystal_idx = -1;
    jrand_set(&d->rand,(i64)seed);
    d->alive = 1;

    for (int i = 0; i < ED_NUM_CRYSTALS; ++i) {
        ed_pillar_crystal_pos(i, &a->crystals[i].x, &a->crystals[i].y, &a->crystals[i].z);
        a->crystals[i].alive = 1;
    }

    a->player.alive = 1;
    a->player.x = 10.0 + (double)(mc_hash_bound(seed, 7));
    a->player.y = 64.0;
    a->player.z = 10.0 + (double)(mc_hash_bound(mc_hash64(seed + 1), 7));
}

MC_HD static inline u64 ed_bitcopy64(const void *p) {
    u64 bits;
    memcpy(&bits, p, 8);
    return bits;
}

MC_HD static inline void ed_pack_tick(const EdDragon *d, u64 *out) {
    out[0] = ed_bitcopy64(&d->health);
    out[1] = (u64)(u32)d->phase;
    out[2] = (u64)(u32)d->phase_ticks;
    out[3] = (u64)(u32)d->heal_crystal_idx;
    out[4] = ed_bitcopy64(&d->x);
    out[5] = ed_bitcopy64(&d->y);
    out[6] = ed_bitcopy64(&d->z);
    out[7] = (u64)(u32)d->death_ticks;
    out[8] = (u64)(u32)d->alive;
    out[9] = ed_bitcopy64(&d->yaw);
}

MC_HD static inline void ed_run(EdArena *a, const McSinTable *st, u64 seed, int nticks, u64 *out) {
    ed_init(a, seed);
    int destroy_tick = 50 + (int)(a->seed % 30u);
    int destroy_idx  = (int)(a->seed % (u64)ED_NUM_CRYSTALS);

    for (int t = 0; t < nticks; ++t) {
        a->tick = t;
        if (t == destroy_tick && ed_mark_crystal_destroyed(a, destroy_idx))
            ed_on_crystal_destroyed(a, destroy_idx, 1, 1);
        ed_tick_dragon(a, st);
        ed_pack_tick(&a->dragon, out + (size_t)t * ED_DUMP_FIELDS);
    }
}

#endif /* MC_ENDER_DRAGON_H */
