/* ew_entity_store: a CROSS-CHUNK entity store for the entities_world driver.
 *
 * This is my OWN header (NOT mc_world.h, mc_entity.h, or any verified kernel). It is a flat SoA of
 * entities whose positions are WORLD coordinates (double), so an entity can walk continuously from
 * one chunk into a neighbour - the chunk index is DERIVED from the world x/z, never stored as the
 * primary key. Double-buffered by the driver (SPEC rule 3): the tick reads the `now` store and
 * writes the `next` store, so no entity observes another's mid-tick write and CPU==CUDA holds under
 * the single-thread-per-env execution the oracle uses.
 *
 * Slot 0 is reserved for the scripted player target; slots 1.. are hostile mobs (zombies here).
 * Everything is POD, fixed-size, zero pointers -> lives happily on the CUDA device heap. */
#ifndef MC_EW_ENTITY_STORE_H
#define MC_EW_ENTITY_STORE_H

#include "mc.h"

/* Product live roster needs more than the original 7-mob arena. Fixed SoA size
 * stays deterministic; slot 0 remains the reserved player/target slot. 96
 * leaves room for the 70-hostile and 10-passive natural caps plus
 * route/spawner encounters (measured against route_e2e). */
#define EW_MAX_ENTITIES 96

/* entity type tags */
enum { EW_TYPE_NONE = 0, EW_TYPE_PLAYER = 1, EW_TYPE_ZOMBIE = 2 };
/* AI phases (mirrors mob_ai_zombie MAZ_STATE_*) */
enum { EW_AI_IDLE = 0, EW_AI_CHASE = 1, EW_AI_ATTACK = 2 };

typedef struct {
    int    count;                          /* number of used slots (contiguous from 0) */
    u8     type[EW_MAX_ENTITIES];
    u8     alive[EW_MAX_ENTITIES];
    i32    id[EW_MAX_ENTITIES];

    double x[EW_MAX_ENTITIES];             /* WORLD coordinates */
    double y[EW_MAX_ENTITIES];
    double z[EW_MAX_ENTITIES];
    double vx[EW_MAX_ENTITIES];
    double vy[EW_MAX_ENTITIES];
    double vz[EW_MAX_ENTITIES];
    float  yaw[EW_MAX_ENTITIES];
    float  health[EW_MAX_ENTITIES];
    u8     on_ground[EW_MAX_ENTITIES];

    u32    ai_state[EW_MAX_ENTITIES];
    i32    attack_time[EW_MAX_ENTITIES];   /* melee cooldown */
    i32    repath_timer[EW_MAX_ENTITIES];

    /* current A* waypoint target, WORLD coordinates */
    double path_tx[EW_MAX_ENTITIES];
    double path_ty[EW_MAX_ENTITIES];
    double path_tz[EW_MAX_ENTITIES];
    u32    path_len[EW_MAX_ENTITIES];

    /* last observed chunk index of this entity (for cross-chunk-crossing evidence) */
    i32    cx[EW_MAX_ENTITIES];
    i32    cz[EW_MAX_ENTITIES];
} EwStore;

MC_HD static inline void ew_store_clear(EwStore *s) {
    int i;
    s->count = 0;
    for (i = 0; i < EW_MAX_ENTITIES; ++i) {
        s->type[i] = EW_TYPE_NONE;
        s->alive[i] = 0;
        s->id[i] = -1;
        s->x[i] = s->y[i] = s->z[i] = 0.0;
        s->vx[i] = s->vy[i] = s->vz[i] = 0.0;
        s->yaw[i] = 0.0f;
        s->health[i] = 0.0f;
        s->on_ground[i] = 0;
        s->ai_state[i] = EW_AI_IDLE;
        s->attack_time[i] = 0;
        s->repath_timer[i] = 0;
        s->path_tx[i] = s->path_ty[i] = s->path_tz[i] = 0.0;
        s->path_len[i] = 0;
        s->cx[i] = 0;
        s->cz[i] = 0;
    }
}

/* deep copy now -> next (double-buffer baseline for a tick) */
MC_HD static inline void ew_store_copy(EwStore *dst, const EwStore *src) {
    int i;
    dst->count = src->count;
    for (i = 0; i < EW_MAX_ENTITIES; ++i) {
        dst->type[i] = src->type[i];
        dst->alive[i] = src->alive[i];
        dst->id[i] = src->id[i];
        dst->x[i] = src->x[i];  dst->y[i] = src->y[i];  dst->z[i] = src->z[i];
        dst->vx[i] = src->vx[i]; dst->vy[i] = src->vy[i]; dst->vz[i] = src->vz[i];
        dst->yaw[i] = src->yaw[i];
        dst->health[i] = src->health[i];
        dst->on_ground[i] = src->on_ground[i];
        dst->ai_state[i] = src->ai_state[i];
        dst->attack_time[i] = src->attack_time[i];
        dst->repath_timer[i] = src->repath_timer[i];
        dst->path_tx[i] = src->path_tx[i];
        dst->path_ty[i] = src->path_ty[i];
        dst->path_tz[i] = src->path_tz[i];
        dst->path_len[i] = src->path_len[i];
        dst->cx[i] = src->cx[i];
        dst->cz[i] = src->cz[i];
    }
}

/* spawn into the first free slot >= 1; returns the slot or -1 if the store is full. */
MC_HD static inline int ew_store_spawn(EwStore *s, u8 type, i32 id,
                                       double x, double y, double z, float health) {
    int i;
    for (i = 1; i < EW_MAX_ENTITIES; ++i) {
        if (s->type[i] == EW_TYPE_NONE) {
            s->type[i] = type;
            s->alive[i] = 1;
            s->id[i] = id;
            s->x[i] = x; s->y[i] = y; s->z[i] = z;
            s->vx[i] = s->vy[i] = s->vz[i] = 0.0;
            s->yaw[i] = 0.0f;
            s->health[i] = health;
            s->on_ground[i] = 1;
            s->ai_state[i] = EW_AI_IDLE;
            s->attack_time[i] = 0;
            s->repath_timer[i] = 0;
            s->path_tx[i] = x; s->path_ty[i] = y; s->path_tz[i] = z;
            s->path_len[i] = 0;
            if (i + 1 > s->count) s->count = i + 1;
            return i;
        }
    }
    return -1;
}

#endif /* MC_EW_ENTITY_STORE_H */
