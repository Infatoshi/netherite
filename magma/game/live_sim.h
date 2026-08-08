/* game/live_sim.h - minimal live entity store + plant plot for the playable seam.
 *
 * Ticked each sim frame from app/game_main.c so the world has measurable side
 * effects beyond weather/worldTime: falling item entities and wheat growth.
 */
#ifndef MAGMA_GAME_LIVE_SIM_H
#define MAGMA_GAME_LIVE_SIM_H

#include "game/game.h"
#include "items_core.h"  /* ICStack for gm_live_spawn_stack */
#include "physics_collision_math.h"  /* McAABB */

#ifdef __cplusplus
extern "C" {
#endif

/* Active EntityItem table. Must hold a full 27-slot chest break plus concurrent
 * drops (mobs, player throw). Overflow is a bounded recoverable hold when the
 * table is full - items are not silently discarded until overflow is also full. */
#define GM_LIVE_MAX 48
#define GM_LIVE_OVERFLOW_MAX 32
#define GM_LIVE_MAX_ENCHANTS 8  /* matches IC_MAX_ENCHANTS / StoredEnchantments cap */
#define GM_LIVE_FALL_UPDATES 128

typedef struct {
    int    active;
    int    type;     /* 0 = EntityItem, 1 = hostile marker, 2 = falling block */
    int    eid;      /* authoritative fixture id; 0 uses the local fallback */
    double x, y, z;
    double mx, my, mz;
    float  yaw;
    float  hover_start; /* exact EntityItem bob/spin phase when available */
    int    has_hover_start;
    int    on_ground;
    int    age;
    int    health;   /* EntityItem private integer health, initialized to 5 */
    int    item, count, meta;
    /* StoredEnchantments-equivalent (enchanted books); 0 for ordinary items. */
    int    n_enchants;
    short  ench_id[GM_LIVE_MAX_ENCHANTS];
    short  ench_lvl[GM_LIVE_MAX_ENCHANTS];
    int    pickup_delay;
    int    lifespan;
    /* Harness-only stationary EntityItem: Java has no gravity and zero
     * motion, but still runs move(0,0,0)/doBlockCollisions each tick. */
    int    controlled_stationary;
} GmLiveEnt;

typedef struct {
    int active;
    int x, y, z;
    int block_id;
    long long due_tick;
} GmLiveFallUpdate;

typedef struct {
    int active;
    int x, y, z;
    int block_id, block_meta;
    long long due_tick;
} GmLiveFallLanding;

typedef struct {
    GmLiveEnt ents[GM_LIVE_MAX];
    int       n_active;
    /* Recoverable hold when ents[] is full (chest break under pressure). */
    ICStack   overflow[GM_LIVE_OVERFLOW_MAX];
    double    overflow_x[GM_LIVE_OVERFLOW_MAX];
    double    overflow_y[GM_LIVE_OVERFLOW_MAX];
    double    overflow_z[GM_LIVE_OVERFLOW_MAX];
    int       overflow_delay[GM_LIVE_OVERFLOW_MAX];
    int       n_overflow;
    int       spawn_fail_count; /* times both table and overflow rejected */
    /* BlockFalling scheduled updates. World.scheduleUpdate deduplicates an
     * already-pending block/position pair; this bounded table does the same. */
    GmLiveFallUpdate fall_updates[GM_LIVE_FALL_UPDATES];
    GmLiveFallLanding fall_landings[GM_LIVE_MAX];
    /* wheat plot (world block coords) advanced with plant_growth-style rolls */
    int       plant_wx, plant_wy, plant_wz;
    int       plant_age;     /* 0..7 wheat meta */
    int       plant_active;
    unsigned  plant_rng;     /* simple LCG for growth rolls */
    int       ticks;
} GmLiveSim;

typedef struct {
    int slot;
    int eid;
    int item;
    double x, y, z;
    McAABB box;
} GmLiveExplosionTarget;

void gm_live_init(GmLiveSim *s, long long seed, int surface_y);
/* Plain spawn (no enchant payload). Prefer gm_live_spawn_stack for books. */
int  gm_live_spawn_item(GmLiveSim *s, double x, double y, double z,
                        int item, int count, int meta, int pickup_delay);
int  gm_live_spawn_item_exact(
    GmLiveSim *s, int eid, double x, double y, double z,
    double mx, double my, double mz, float yaw,
    int item, int count, int meta,
    int age, int pickup_delay, int controlled_stationary);
/* Exact constructor variant that preserves EntityItem.hoverStart. */
int  gm_live_spawn_item_exact_hover(
    GmLiveSim *s, int eid, double x, double y, double z,
    double mx, double my, double mz, float yaw, float hover_start,
    int item, int count, int meta,
    int age, int pickup_delay, int controlled_stationary);
/* EntityItem with full ICStack payload (item/count/meta + StoredEnchantments).
 * Returns 1 if active or held in overflow; 0 only if both caps are exhausted. */
int  gm_live_spawn_stack(GmLiveSim *s, double x, double y, double z,
                         ICStack stack, int pickup_delay);
/* BlockFalling.onBlockAdded / neighborChanged scheduling seam. Call after a
 * world edit at (x,y,z); the edited block and the block above are notified. */
void gm_live_block_changed(GmLiveSim *s, GmWorld *w,
                           int x, int y, int z);
void gm_live_pre_player_tick(GmLiveSim *s, GmWorld *w);
/* One tick: gravity/friction for live ents (world collision via gm_world_*), wheat growth. */
void gm_live_tick(GmLiveSim *s, GmWorld *w);
/* Same world tick plus vanilla-style pickup into the supplied local-frame player. */
void gm_live_tick_player(GmLiveSim *s, GmWorld *w, struct PsvPlayer *pl,
                         int player_ox, int player_oz);
/* Fill GmEntityView list for rendering; returns count. */
int  gm_live_fill_views(const GmLiveSim *s, GmEntityView *out, int max);
/* Replay variant: oracle EntityFallingBlock ghosts own render pose, while the
 * local falling entities remain active as world-truth simulation. */
int  gm_live_fill_views_filtered(const GmLiveSim *s, GmEntityView *out,
                                 int max, int suppress_falling);
/* Enumerate active EntityItem AABBs (width/height 0.25 in Java 1.11.2).
 * Returns the number written, bounded by capacity. */
int  gm_live_item_boxes(
    const GmLiveSim *s, McAABB *out, int capacity);
/* Snapshot EntityItems for Explosion.doExplosionA, then apply its damage and
 * raw velocity addition by slot. The snapshot keeps density reads stable when
 * an earlier target dies during the same blast. */
int  gm_live_explosion_targets(
    const GmLiveSim *s, GmLiveExplosionTarget *out, int capacity);
int  gm_live_apply_explosion(
    GmLiveSim *s, int slot, float damage,
    double impulse_x, double impulse_y, double impulse_z);
/* BlockPressurePlate.Sensitivity.EVERYTHING query over represented items. */
int  gm_live_items_intersects_aabb(
    const GmLiveSim *s, const McAABB *box);
/* Entity-count form used by weighted pressure plates. Stack count does not
 * affect the result: each active EntityItem contributes at most one. */
int  gm_live_items_count_intersects_aabb(
    const GmLiveSim *s, const McAABB *box);
/* Debug counters for harness / logs. */
int  gm_live_entity_moved(const GmLiveSim *s); /* 1 if any ent pos changed last tick */
int  gm_live_plant_age(const GmLiveSim *s);
int  gm_live_overflow_count(const GmLiveSim *s);
int  gm_live_spawn_fail_count(const GmLiveSim *s);

#ifdef __cplusplus
}
#endif
#endif
