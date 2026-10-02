#ifndef NETHERITE_ITEM_ENTITY_H
#define NETHERITE_ITEM_ENTITY_H

#include <stdint.h>

#include "det.h"
#include "entity.h"
#include "arena.h"
#include "envstack.h"

struct drop_ent;   /* drops.h; the *_state spawns read one without owning it */

enum {
    IE_ITEM = 1,
    IE_ORB = 2,
    IE_ARROW = 3,
    IE_SNOWBALL = 4,
    IE_EGG = 5,
    IE_ENDER_PEARL = 6,
    IE_EXP_BOTTLE = 7,
    IE_POTION = 8,
    IE_SMALL_FIREBALL = 9,
    IE_LARGE_FIREBALL = 10,
    IE_ENDER_EYE = 11,
    IE_TNT = 12
};

enum { IE_SRC_CACTUS = 0, IE_SRC_IN_FIRE = 1, IE_SRC_ON_FIRE = 2, IE_SRC_LAVA = 3, IE_SRC_THROWN = 4, IE_SRC_ARROW = 5, IE_SRC_FIREBALL = 6 };

enum {
    IE_REMOVAL_MERGED = 0,
    IE_REMOVAL_BURNED = 1,
    IE_REMOVAL_DESPAWNED = 2,
    IE_REMOVAL_VOID = 3,
    IE_REMOVAL_IMPACT = 4,
    IE_REMOVAL_GROUND_DESPAWN = 5
};

/* Where a shared stack's count lives (Java's one ItemStack held by several
 * entities: an item a mob picked up, the mob, the item its death drops): a
 * living's equipment slot (owner a living.h lref, slot the equipment index)
 * or an item-world entity's own count (owner STACK_ITEM_TAG over its pool
 * slot; the slot keeps the count after the entity is released until the pool
 * hands the slot out again, as the freed record the address named did);
 * owner 0 for none. */
#define STACK_ITEM_TAG 0x4953ull
struct stack_link {
    uint64_t owner;
    int slot;
};

typedef struct ie_ent {
    struct entity e;

    int spawn_index;            /* the order index the recorder keys on */
    int order_key;              /* the pass order's key, when not its id
                                 * (serverreplay.c sr_order_push) */
    int kind;                   /* IE_ITEM or IE_ORB */
    int dimension;              /* Entity.dimension; only the NBT carries it */
    struct ie_world *iew;       /* the list it lives in, for the collision query */
    det_rng rand;               /* the per-entity Random Det.newRandom made */
    int entity_id;              /* the Det per-role counter */
    int64_t uuid_msb, uuid_lsb; /* Entity.getUniqueID, the NBT UUIDMost/Least */
    int ticks_existed;
    double prev_x, prev_y, prev_z;
    double last_tick_x, last_tick_y, last_tick_z;
    float rotation_yaw, rotation_pitch, prev_yaw, prev_pitch;

    uint8_t is_dead, added_to_chunk, first_update, velocity_changed;
    /* Entity.inPortal, portalCounter, timeUntilPortal, teleportDirection */
    int in_portal, portal_counter, time_until_portal, teleport_direction;
    uint8_t in_water;
    /* setEntityItemStack's setObjectWatched(10): the tracker entry's next
     * pass runs its update block (a merge's survivor) */
    uint8_t stack_watch_dirty;
    int chunk_x, chunk_y, chunk_z;

    /* Entity.air: EntityLivingBase lowers it underwater, and neither kind here
     * is living, so it stays at the constructor's 300. */

    /* EntityItem.age / EntityXPOrb.xpOrbAge; EntityItem.delayBeforeCanPickup /
     * EntityXPOrb.field_70532_c; EntityItem.health / EntityXPOrb.xpOrbHealth. */
    int age, delay, health;

    /* EntityItem only */
    float hover_start;
    int stack_item, stack_damage, stack_count;
    int stack_tag;                /* the stack's tag (itemtag.h), 0 for none */
    const char *owner_name;       /* CommandGive sets EntityItem.Owner */
    /* the count lives in a living's equipment slot this item shares (a mob's
     * dropped equipment: Java's one ItemStack in both); owner 0 for its own */
    struct stack_link stack_count_link;

    /* EntityXPOrb only */
    int xp_color, xp_target_color, xp_value;
    int xp_has_target;          /* closestPlayer cache, one server player */
    int xp_target_left;         /* respawnPlayer replaced the cached
                                 * EntityPlayerMP: the orb still pulls toward
                                 * the old object, frozen where it died */
    double xp_tx, xp_ty, xp_tz;

    /* Projectile shared fields */
    int in_ground;
    int tile_x, tile_y, tile_z;
    int in_tile, in_data;
    int shake;
    int ticks_in_ground, ticks_in_air;

    /* EntityArrow */
    double arrow_damage;
    int knockback_strength;
    int can_be_picked_up;
    int is_critical;
    uint64_t shooting_entity;     /* the shooter or thrower (living.h lref), 0 none */

    /* EntityPotion */
    int potion_damage;
    /* 1 - the potion stack's Count: a witch's is a new ItemStack (Count 1);
     * the player's throw keeps a reference to its own stack, which the
     * throw already shrank (Count 0 for a single potion) */
    int potion_spent;

    /* EntityFireball */
    int ticks_alive;
    double accel_x, accel_y, accel_z;
    int explosion_power;
    /* shootingEntity: the living entity that fired it (the ghast), NULL when
     * the spawner had none; shooter_is_player marks the player tag */
    uint64_t shooter;             /* living.h lref, 0 none */
    int shooter_is_player;
    /* the server player's life (player.h) when it threw: a respawn makes a
     * new EntityPlayerMP, and the thrower stays the old object */
    int shooter_life;
    /* EntityThrowable.throwerName with thrower still null (loaded from
     * NBT): getThrower looks the player up each tick until it is there */
    int owner_pending;

    /* EntityItem's Thrower: named when a player pressed Q (dropOneItem
     * passes func_145799_b(getCommandSenderName())); NULL otherwise. */
    const char *thrower;

    /* EntityEnderEye's transient target and lifetime. */
    double eye_target_x, eye_target_y, eye_target_z;
    int eye_timer, eye_drop;
    /* EntityTNTPrimed: the fuse and tntPlacedBy (1 the player, 0 null);
     * a mob's placing is tnt_placer (living.h lref: the creeper whose blast
     * primed it, kept dead as Java keeps the object) */
    int fuse, tnt_by_player;
    uint64_t tnt_placer;
} ie_ent;

typedef struct ie_removal {
    int tick, spawn_index, entity_id, reason, health, fire, age;
    double pos_y;
    /* its chunk membership as it was released (another list the owner keeps
     * of the same entity, the living world's, reads it) */
    int added_to_chunk, chunk_x, chunk_y, chunk_z;
} ie_removal;

#define IE_SECTIONS 16

typedef struct ie_chunk {
    int cx, cz;
    int used;
    struct sec_list sec[IE_SECTIONS];   /* the ie_ent slots (at most the world's IE_MAX_ENTITIES) */
} ie_chunk;

#define IE_MAX_ENTITIES 8192
/* A query's result list (World.getEntitiesWithinAABB returns every match):
 * as many as an item world holds, from the env's scratch stack for the
 * enclosing scope. */
/* How many collidable other-pool entities (falling blocks, hanging
 * entities, primed TNT, the End's crystals and parts) one projectile step
 * takes from query_other; more stops the program. */
#define IE_OTHER_MAX 1024
#define IE_QUERY_LIST(name) ie_ent **name ENV_LOCAL = envstack_take(IE_MAX_ENTITIES * sizeof *name)

/* One entity Explosion.doExplosionA's pass reaches: the entity the blast reads
 * (box, position, eye height, motion) and whichever owner behind it takes the
 * damage - an ie_ent, or a struct living (living.h is not visible here). */
struct blast_ent {
    struct entity *e;
    ie_ent *ie;
    void *liver;
};

/* The environment's pool (arena.c): a zeroed entity, and its release. */
ie_ent *ie_ent_alloc(void);
void ie_ent_release(ie_ent *en);

/* A reference to an item-world entity: its slot's generation and index under
 * a tag, 0 for none (living.h's lref for this pool). ie_get stops the
 * program on a reference whose entity was released. */
typedef uint64_t ieref;
#define IEREF_TAG 0x4945ull
void ieref_stale(ieref h);
static inline ieref ie_ref(const ie_ent *en)
{
    if (en == NULL) return 0;
    uint32_t i = (uint32_t)ie_ent_index(en);
    return IEREF_TAG << 48 | (uint64_t)(nw_arena()->ie_ents.gen[i] & 0xFFFFu) << 32 | i;
}
static inline ie_ent *ie_get(ieref h)
{
    if (h == 0) return NULL;
    uint32_t i = (uint32_t)h;
    if (__builtin_expect(((h >> 32) & 0xFFFFu) != (nw_arena()->ie_ents.gen[i] & 0xFFFFu), 0)) ieref_stale(h);
    return ie_ent_at((int32_t)i);
}

typedef struct ie_world {
    struct world *w;
    det_state *det;
    int role;
    det_rng world_rand;
    const struct entity *player; /* World.getClosestPlayerToEntity for XP orbs */
    int player_gone;             /* player is out of playerEntities (the End's
                                  * exit): an orb's cached closestPlayer still
                                  * pulls it, a fresh query finds nobody */
    int next_spawn_index;

    int32_t slot[IE_MAX_ENTITIES];   /* the entities' pool slots (ie_ent_at) */
    int n;

    /* World.getCollidingBoundingBoxes' entity half (the boxes of the other
     * entities in the list). The item and orb recordings were made with items
     * whose boxes never overlap, so their runs leave this off; the tape replay,
     * where items and the player share the world, turns it on. */
    int collide_entities;

    /* The entities outside this list that the collision query must still see:
     * the server player. Java's getCollidingBoundingBoxes adds every entity's
     * box, players included, and the player is not an ie_ent. */
    const struct aabb *other_boxes[2];
    int nother;

    struct ie_chunk *chunks;
    int nchunks, capchunks;

    /* the other item list over the same world (the tape replay keeps the
     * living world's drops in its own): EntityItem.searchForOtherItemsNearby
     * sees one chunk list, so it walks both, each section in chunk-list
     * (stamp) order; NULL when there is none */
    struct ie_world *peer;

    void *user_data;
    void (*on_splash)(struct ie_world *iew, struct ie_ent *potion, void *hit_entity);
    /* spawnEntityInWorld(new EntityXPOrb) for a world that keeps its own
     * entity list (the living world); NULL spawns into the item list */
    struct ie_ent *(*spawn_orb)(struct ie_world *iew, double x, double y, double z, int xp);
    void (*on_arrow_hit)(struct ie_world *iew, struct ie_ent *arrow, void *hit_entity, int is_living);
    /* EntityLargeFireball.onImpact: the 6.0F damage to the hit entity (hit 0
     * ie_ent, 1 living, 2 the player) and the explosion, both on the caller's
     * pools. NULL keeps the old behaviour (the projectile dies quietly). */
    void (*on_fireball_impact)(struct ie_world *iew, struct ie_ent *en,
                               void *hit, int hit_kind);
    void (*on_egg_chicken)(struct ie_world *iew, struct ie_ent *egg);
    /* BlockTNT.onEntityCollidedWithBlock for a burning arrow in flight
     * (EntityArrow.onUpdate's func_145775_I): func_150114_a's primed TNT on
     * the owner's pools, then setBlockToAir. NULL: not reached. */
    void (*on_arrow_tnt)(struct ie_world *iew, struct ie_ent *arrow, int x, int y, int z);
    /* BlockButton.onEntityCollidedWithBlock for an arrow in flight: the
     * wooden button (143) runs func_150046_n while up. NULL: not reached. */
    void (*on_arrow_button)(struct ie_world *iew, struct ie_ent *arrow, int x, int y, int z);
    int (*query_living)(struct ie_world *iew, struct aabb box, const void *exclude, void **out, int cap);
    /* The owner's other collidable entities (the End's ender crystals and the
     * dragon's parts): up to cap in the box, with their boxes; and a
     * projectile's hit on one (the attackEntityFrom half; an arrow's whole
     * entity hit). NULL: none. */
    int (*query_other)(struct ie_world *iew, struct aabb box, void **out, struct aabb *boxes, int cap);
    void (*on_other_hit)(struct ie_world *iew, struct ie_ent *projectile, void *other);
    int (*get_living_bb)(struct ie_world *iew, void *ent, struct aabb *out_bb);
    /* Explosion.doExplosionA's entity pass. World.getEntitiesWithinAABBExcludingEntity
     * walks one list of items and livings together (the chunk sections of
     * World.loadedEntityList); a pool that is only the item half of such a world
     * (the hostile arena) installs this so the blast reaches both, in that
     * order, instead of this pool's own chunk sections. exclude is the
     * exploder's entity, which the pass drops. out holds at most cap entries
     * and the return is the total found. */
    int (*blast_entities)(struct ie_world *iew, struct aabb box, const struct entity *exclude,
                          struct blast_ent *out, int cap);
    /* EntityTNTPrimed.explode: the fuse ran out (the entity is already dead);
     * the owner runs World.createExplosion over its own pools. */
    void (*on_tnt_explode)(void *ctx, struct ie_ent *tnt);
    void *tnt_ctx;
    /* EntityEnderPearl.onImpact's server half once the particle draws ran:
     * the thrower's teleport and its 5.0F fall damage. NULL when the pool has
     * no player thrower (the pearl just dies). */
    void (*on_pearl_impact)(struct ie_world *iew, struct ie_ent *pearl);
    /* EntityThrowable.getThrower for a thrower known by name only
     * (owner_pending): the owner sets shooter when the named player is in
     * this world. NULL: never found. */
    void (*resolve_thrower)(struct ie_world *iew, struct ie_ent *en);
    /* The item an entity of this pool spawns (the eye of ender's drop) joins
     * the caller's entity lists through this; NULL keeps it in this list. */
    ie_ent *(*spawn_item)(struct ie_world *iew, double x, double y, double z,
                          int item, int damage, int count);
    /* Entity.travelToDimension once Entity.onEntityUpdate's portal counter
     * ran out (to is 0 or -1): the owner moves the entity between its
     * per-world pools; the rest of the update runs on, dead, in the
     * destination world, and post_update follows it (before the chunk
     * bookkeeping). NULL: the entity stays. */
    void (*portal_travel)(struct ie_world *iew, struct ie_ent *en, int to);
    void (*post_update)(struct ie_world *iew);
    void *portal_ctx;
} ie_world;

/* World.loadedEntityList.add for the item world: IE_MAX_ENTITIES, then a
 * loud stop (Java's list grows; never a silent drop) */
static inline void ie_list_push(ie_world *iew, const struct ie_ent *en)
{
    if (iew->n >= IE_MAX_ENTITIES) list_full("entities in one item world", IE_MAX_ENTITIES);
    iew->slot[iew->n++] = ie_ent_index(en);
}

void ie_init(ie_world *iew, struct world *w, det_state *det);
void ie_free(ie_world *iew);
/* Chunk (cx, cz)'s lists gone when every one is empty (living.h
 * an_chunk_drop_empty); 1 when they went. */
int ie_chunk_drop_empty(ie_world *iew, int cx, int cz);

ie_ent *ie_spawn_item(ie_world *iew, double x, double y, double z, int item, int damage, int count);
ie_ent *ie_spawn_orb(ie_world *iew, double x, double y, double z, int xp);
/* EntityTNTPrimed(World, x, y, z, placedBy): the Entity draws on the pool's
 * role, then the fuse angle's Math.random. */
ie_ent *ie_spawn_tnt(ie_world *iew, double x, double y, double z, int by_player);
void ie_added_to_world(ie_world *iew, ie_ent *en);
/* Chunk.addEntity from a chunk reload, whether or not the chunk is in the
 * world yet. */
void ie_add_to_chunk(ie_world *iew, ie_ent *en);

ie_ent *ie_spawn_item_state(ie_world *iew, const struct drop_ent *ent);
ie_ent *ie_spawn_orb_state(ie_world *iew, const struct drop_ent *ent);

void ie_tick(ie_world *iew, int tick, ie_removal *out, int max_out, int *n_out);

/* World.isMaterialInBB over one material index, and BlockLiquid.func_149801_b,
 * shared with the living entity's water paths. */
int ie_material_in_bb(struct world *w, struct aabb box, int material);
float ie_liquid_height_meta(int meta);

/* World.handleMaterialAcceleration (the flow push) over an entity: is_item_or_orb
 * picks the EntityItem/EntityXPOrb box over Entity.handleWaterMovement's. */
int ie_water_accelerate(struct world *w, struct entity *e, int is_item_or_orb);
int ie_water_accelerate_box(struct world *w, struct aabb box, struct entity *e);
/* ie_water_accelerate's living form (Entity.handleWaterMovement) and the
 * living's handleLavaMovement (World.isMaterialInBB over bb shrunk 0.1
 * sideways and 0.4 at the bottom), answered from the entity's cell memo
 * (entity.h) when its cells are the same and the last answer was none; m NULL
 * runs the test. */
int ie_water_accelerate_memo(struct world *w, struct entity *e, struct cell_memo *m);
int ie_lava_memo(struct world *w, const struct aabb *bb, struct cell_memo *m);

/* The same pass for one entity, in the caller's own order over a pool that
 * also holds other entity kinds: updateEntityWithOptionalForce, then the dead
 * entity out of the pool's list and the chunk. The tape replay drives this
 * because Java's World.updateEntities walks one loadedEntityList whose order
 * is global spawn order, so items and falling blocks interleave. */
int ie_tick_one(ie_world *iew, ie_ent *en, int tick, ie_removal *out, int max_out, int *n_out);

/* Chunk.onChunkUnload for this pool: every entity in the unloaded chunk comes
 * out of the list and the chunk, into the caller's array (which must hold
 * iew->n entries). Nothing is ticked and nothing is freed here. */
int ie_unload_chunk(ie_world *iew, int cx, int cz, ie_ent **out, int cap);

/* The tape replay's alternative to the constructors: an entity a world tick
 * already constructed, whose Det draws (the per-role id, the per-entity Random
 * and the UUID) the tick's recorder spent. The fields come from that record,
 * so nothing is drawn here and the streams stay in the tick's order. */
ie_ent *ie_adopt_item(ie_world *iew, int id, int64_t uuid_msb, int64_t uuid_lsb, uint64_t rand_state,
                      double x, double y, double z, double mx, double my, double mz, float yaw, float hover,
                      int item, int damage, int count, int tag);
ie_ent *ie_adopt_orb(ie_world *iew, int id, int64_t uuid_msb, int64_t uuid_lsb, uint64_t rand_state,
                     double x, double y, double z, double mx, double my, double mz, float yaw, int xp);
/* EntityXPOrb(World), the constructor EntityList.createEntityFromNBT uses:
 * 0.25 by 0.25 (the spawning constructor's is 0.5), then readFromNBT's
 * setPosition. An orb loaded from NBT keeps the smaller box. */
void ie_orb_nbt_size(ie_ent *en);
ie_ent *ie_adopt_tnt(ie_world *iew, int id, int64_t uuid_msb, int64_t uuid_lsb, uint64_t rand_state,
                     double x, double y, double z, double mx, double my, double mz, int fuse, int by_player);
/* World.getEntitiesWithinAABB(EntityItem.class, box) over the pool and its
 * peer in chunk-list order (a mob's loot pickup). */
int ie_items_within_aabb(ie_world *iew, const struct aabb *box, ie_ent **out, int max_out);
int ie_entities_within_aabb_peer(ie_world *iew, const struct aabb *box, ie_ent **out, int max_out);
int ie_get_entities_within_aabb(ie_world *iew, const struct aabb *box, const ie_ent *exclude, ie_ent **out, int max_out);
int ie_can_be_collided_with(const ie_ent *en);
/* BlockPortal.onEntityCollidedWithBlock, then Entity.setInPortal. */
void ie_set_in_portal(void *self);
/* respawnPlayer moved the old EntityPlayerMP to the fresh player's
 * placement: the orbs holding it as closestPlayer keep that position. */
void ie_player_left(ie_world *iew, const struct entity *p);

/* BlockEndPortal.onEntityCollidedWithBlock: travelToDimension(1) at once (the
 * item pool's portal_travel), for an entity not dead in a server world. */
void ie_end_portal(void *self);
/* The entity out of this pool's chunk lists (its list entry stays). */
void ie_chunk_leave(ie_world *iew, ie_ent *en);

#endif
