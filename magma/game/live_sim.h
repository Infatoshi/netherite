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

/* Active EntityItem table. InventoryHelper can split each 64-item stack into
 * seven entities, so a full 27-slot chest has a strict 189-entity upper bound.
 * Keep headroom for the pre-existing world item and concurrent ordinary drops.
 * Overflow remains a bounded recoverable hold for non-exact plain spawns. */
#define GM_LIVE_MAX 256
#define GM_LIVE_OVERFLOW_INITIAL 32
#define GM_LIVE_OVERFLOW_SAFETY_MAX 1048576
#define GM_LIVE_MAX_ENCHANTS 8  /* matches IC_MAX_ENCHANTS / StoredEnchantments cap */
#define GM_LIVE_FALL_UPDATES 128

typedef struct {
    int    active;
    int    type;     /* 0 = EntityItem, 1 = hostile marker, 2 = falling block */
    int    eid;      /* Java Entity.entityId; zero is valid after cold reload */
    long long uuid_most, uuid_least;
    int    uuid_present;
    double x, y, z;
    double mx, my, mz;
    float  yaw, pitch;
    float  fall_distance;
    float  hover_start; /* exact EntityItem bob/spin phase when available */
    int    has_hover_start;
    int    on_ground;
    int    ticks_existed; /* Entity.ticksExisted; starts at zero after NBT load */
    int    age;
    int    health;   /* EntityItem private integer health, initialized to 5 */
    int    item, count, meta;
    int    repair_cost, custom_name, tag_id;
    int    semantic_potion_type; /* fixed loot tag awaiting runtime interning */
    /* StoredEnchantments-equivalent (enchanted books); 0 for ordinary items. */
    int    n_enchants;
    short  ench_id[GM_LIVE_MAX_ENCHANTS];
    short  ench_lvl[GM_LIVE_MAX_ENCHANTS];
    int    pickup_delay;
    int    lifespan;
    int    no_gravity;
    int    no_clip;    /* recomputed by EntityItem.pushOutOfBlocks each tick */
    int    fire;       /* Entity.fire, including the normal dry sentinel -1 */
    int    air;        /* Entity AIR data parameter; defaults to 300 */
    int    portal_cooldown;
    int    in_water;   /* Entity.inWater latch across the two water probes */
    int    first_update;
    unsigned long long random_seed48; /* Entity.rand; no Gaussian user here */
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

/* Cold FIFO admission behind the fixed active EntityItem table. Worlds that
 * never saturate the hot table allocate nothing. */
typedef struct {
    GmLiveEnt entity;
    int occupied;
} GmLiveOverflowEnt;

typedef struct {
    GmLiveEnt ents[GM_LIVE_MAX];
    int       n_active;
    /* Recoverable cold FIFO when ents[] is full. Growth is fail-closed at a
     * corruption/host-safety ceiling rather than at a gameplay-sized bound. */
    GmLiveOverflowEnt *overflow;
    int       n_overflow, overflow_slots, overflow_cap;
    int       spawn_fail_count; /* allocation/safety admissions rejected */
    /* Optional allocation-fault boundary used by atomic saturation tests.
     * Zero is the production default and means grow to host/safety limits. */
    int       item_spawn_limit;
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
void gm_live_destroy(GmLiveSim *s);
/* Process-scoped allocation fault injection for saturation regression
 * binaries whose fixtures deliberately clear the whole simulation struct. */
void gm_live_set_process_spawn_limit(int limit);
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
/* Exact EntityItem state while retaining the complete represented ItemStack
 * payload. Used by player death/toss paths where enchant NBT is observable. */
int  gm_live_spawn_stack_exact_hover(
    GmLiveSim *s, int eid, double x, double y, double z,
    double mx, double my, double mz, float yaw, float hover_start,
    ICStack stack, int age, int pickup_delay, int controlled_stationary);
/* Saved EntityItem state for proof-fenced, tagless air/full-cube continuation. */
int  gm_live_spawn_item_state_exact(
    GmLiveSim *s, int eid, double x, double y, double z,
    double mx, double my, double mz, float yaw, float hover_start,
    int item, int count, int meta, int age, int pickup_delay,
    int health, int lifespan, int on_ground, int no_gravity,
    int ticks_existed);
/* Exact otherwise-unsaved Entity base cursor needed by water entry and lava. */
int  gm_live_set_item_environment_state(
    GmLiveSim *s, int eid, unsigned long long random_seed48,
    int fire, int in_water, int first_update);
/* ItemArmor's 1.11.2 dispenser success bug delegates the now-empty source
 * stack to BehaviorDefaultDispenseItem. Preserve that empty EntityItem until
 * its first onUpdate, which immediately retires it. */
int  gm_live_spawn_empty_item_exact_hover(
    GmLiveSim *s, int eid, double x, double y, double z,
    double mx, double my, double mz, float yaw, float hover_start);
/* EntityItem with full ICStack payload (item/count/meta + StoredEnchantments).
 * Returns 1 if active or held in the cold FIFO; 0 on allocation/safety reject. */
int  gm_live_spawn_stack(GmLiveSim *s, double x, double y, double z,
                         ICStack stack, int pickup_delay);
/* Preallocate enough cold FIFO storage for count subsequent plain stack
 * spawns without changing entity order or draining the FIFO. This is the
 * atomic-admission seam for block callbacks that must not mutate world/RNG
 * state and then discover an allocation failure halfway through their drops. */
int  gm_live_reserve_plain_spawns(GmLiveSim *s, int count);
int  gm_live_entity_slot_count(const GmLiveSim *s);
GmLiveEnt *gm_live_entity_mut(GmLiveSim *s, int slot);
const GmLiveEnt *gm_live_entity_ref(const GmLiveSim *s, int slot);
int  gm_live_retire_entity_slot(GmLiveSim *s, int slot);
/* BlockFalling.onBlockAdded / neighborChanged scheduling seam. Call after a
 * world edit at (x,y,z); the edited block and the block above are notified. */
void gm_live_block_changed(GmLiveSim *s, GmWorld *w,
                           int x, int y, int z);
void gm_live_pre_player_tick(GmLiveSim *s, GmWorld *w);
/* One tick: gravity/friction for live ents (world collision via gm_world_*), wheat growth. */
void gm_live_tick(GmLiveSim *s, GmWorld *w);
/* Runtime variant: update each supplied physical slot exactly once in Java's
 * loadedEntityList order. The caller supplies every represented slot. */
void gm_live_tick_player_ordered(
    GmLiveSim *s, GmWorld *w, struct PsvPlayer *pl,
    int player_ox, int player_oz, const int *slots, int slot_count);
/* Same world tick plus vanilla-style pickup into the supplied local-frame player. */
void gm_live_tick_player(GmLiveSim *s, GmWorld *w, struct PsvPlayer *pl,
                         int player_ox, int player_oz);
/* Direct EntityItem.onCollideWithPlayer boundary for an ordinary survival
 * player. Returns the number transferred, including partial pickups. */
int  gm_live_item_collide_player_exact(
    GmLiveSim *s, int eid, struct PsvPlayer *pl);
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
/* Generic EntityItem.attackEntityFrom damage. Unlike explosion damage this
 * has no Nether Star exemption and adds no motion. */
int  gm_live_apply_damage(GmLiveSim *s, int slot, float damage);
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
