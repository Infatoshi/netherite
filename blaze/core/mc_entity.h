/* mc_entity.h - entity model as flat SoA + switch dispatch (NOT the Java OOP tree). TRUNK schema:
 * fixed here; per-mob subagents add a type enum value + a case in the update switch + (if needed)
 * SoA columns. No virtual dispatch, no inheritance - that is the whole point for GPU batching.
 *
 * MC's Entity->EntityLivingBase->EntityMob->EntityZombie hierarchy collapses to: a type tag, shared
 * columns (pos/vel/health/...), and behavior selected by switch(type). AI goal/task lists collapse
 * to small explicit state machines (see mc_ai.h, wave 3). */
#ifndef MC_ENTITY_H
#define MC_ENTITY_H

#include "mc.h"

/* KEEP roster is finalized at oracle 6 (mob AI). Values are ours (internal fidelity), not vanilla
 * network ids - but keep them stable once assigned. */
enum {
    ENT_NONE = 0,
    ENT_PLAYER = 1,
    ENT_ITEM = 2,
    ENT_ZOMBIE = 3, ENT_SKELETON = 4, ENT_CREEPER = 5, ENT_SPIDER = 6, ENT_ENDERMAN = 7,
    ENT_BLAZE = 8, ENT_GHAST = 9, ENT_ZOMBIE_PIGMAN = 10, ENT_SILVERFISH = 11,
    ENT_ENDER_DRAGON = 12, ENT_ENDER_CRYSTAL = 13,
    /* passive + projectiles + remaining mobs appended at oracle 6/7 */
    ENT_TYPE_MAX = 64
};

#ifndef MC_MAX_ENTITIES
#define MC_MAX_ENTITIES 256        /* per env; tune per memory budget */
#endif

/* SoA: column-major over entities. Add columns as subsystems need them. */
typedef struct {
    int    count;
    u8     type[MC_MAX_ENTITIES];
    u8     alive[MC_MAX_ENTITIES];
    double x[MC_MAX_ENTITIES], y[MC_MAX_ENTITIES], z[MC_MAX_ENTITIES];
    double vx[MC_MAX_ENTITIES], vy[MC_MAX_ENTITIES], vz[MC_MAX_ENTITIES];
    float  yaw[MC_MAX_ENTITIES], pitch[MC_MAX_ENTITIES];
    float  health[MC_MAX_ENTITIES];
    i32    age[MC_MAX_ENTITIES];
    u8     on_ground[MC_MAX_ENTITIES];
    /* TODO subagents: aiState, target, path columns, item id/count, etc. */
} Entities;

/* Per-entity update dispatch skeleton. Pure data-in/data-out; world is read from 'now', written to
 * 'next' (double buffer). Subagents add cases. */
struct World; /* fwd */
MC_HD static inline void mc_entity_update(Entities *e, int i /*, const World *now, World *next, i64 tick */) {
    switch (e->type[i]) {
        case ENT_NONE: return;
        /* case ENT_ZOMBIE: mc_zombie_update(e, i, now, next, tick); break;  (oracle 6/7) */
        default: return;
    }
}

#endif /* MC_ENTITY_H */
