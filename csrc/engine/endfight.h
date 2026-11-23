/* The End's boss fight as the whole-server replay carries it: the
 * EntityDragon and the EntityEnderCrystals BiomeEndDecorator spawns, kept in
 * dragon.c's state (the crystals live in the dragon's array, which is also
 * what its healing search walks) and entered in the End's loadedEntityList
 * (the replay's pass order) as pools SR_POOL_DRAGON and SR_POOL_CRYSTAL. This
 * file owns their construction and their NBT for the entity digest;
 * serverreplay.c ticks them at their places in the pass. */
#ifndef NETHERITE_ENDFIGHT_H
#define NETHERITE_ENDFIGHT_H

#include "det.h"
#include "dragon.h"
#include "nbtjson.h"

struct world;
struct aabb;

enum { SR_POOL_DRAGON = 4, SR_POOL_CRYSTAL = 5 };

struct sr_end {
    struct dragon_state d;      /* the dragon; every crystal is in d.crystals */
    int dragon_spawned;         /* BiomeEndDecorator made it at chunk 0,0 */
    int dragon_listed;          /* it is in the End's loadedEntityList */
    /* the dragon as the End's livings see it (a bitten enderman's
     * entityToAttack): its position, box and health, kept in step after
     * every update; attacks on it land nowhere (EntityDragon.attackEntityFrom
     * is false) */
    uint64_t shadowh;           /* living.h lref */
};

/* new EntityDragon(world) then setLocationAndAngles(0, 128, 0, yaw, 0): the
 * constructor's Det draws in det's current role (dragon_init). The crystals
 * spawned before it stay. */
void endfight_spawn_dragon(struct sr_end *e, struct world *w, det_state *det, float yaw);

/* new EntityEnderCrystal(world) then setLocationAndAngles(x, y, z, yaw, 0),
 * y already holding the yOffset: its index in d.crystals, or -1. */
int endfight_spawn_crystal(struct sr_end *e, det_state *det, double x, double y, double z, float yaw);

/* The dragon's chunk after a move (updateEntityWithOptionalForce's tail): a
 * new chunk or section stamps its place at the end of that list. 1 when it
 * changed (or force). */
int endfight_dragon_chunk(struct dragon_state *d, int force);

/* World.getEntitiesWithinAABBExcludingEntity reaches an entity through the
 * chunk and section it was added to: the chunks 2 blocks past the box, the
 * sections 2 blocks past it (clamped to 0..15). The dragon (and its parts,
 * which only it carries) is 16 wide, so its box can meet the searched one
 * from a chunk the search never reads. 1 when the search reads its list. */
int endfight_dragon_listed_near(const struct dragon_state *d, const struct aabb *box);

/* EntityList.createEntityFromNBT for a saved dragon (o, as it was saved): the
 * constructor's draws (dragon_init, det's current role), then readFromNBT's
 * fields; the flight state starts over. */
void endfight_reload_dragon(struct sr_end *e, struct world *w, det_state *det, const struct dragon_state *o);

/* A v2 snapshot's EntityDragon (its NBT and Snapshot.dragonState, which
 * the entity must carry) and EntityEnderCrystal: no constructor draws, the
 * snapshot holds the streams after them. The crystal's index, or -1. The
 * healing crystal is named by entity id and resolved once every crystal is
 * in (endfight_resolve_heal). */
struct snap_entity;
void endfight_snapshot_dragon(struct sr_end *e, struct world *w, det_state *det, const struct snap_entity *se);
int endfight_snapshot_crystal(struct sr_end *e, const struct snap_entity *se);
void endfight_resolve_heal(struct sr_end *e, int crystal_id);

/* Entity.writeToNBT for the two kinds, in Java's key order. */
nbt *endfight_dragon_nbt(const struct dragon_state *d);
nbt *endfight_crystal_nbt(const struct dragon_crystal_state *c);
/* The same two through a writer (nbtw.h), into the open compound. */
struct nbtw;
void endfight_dragon_w(struct nbtw *w, const struct dragon_state *d);
void endfight_crystal_w(struct nbtw *w, const struct dragon_crystal_state *c);

/* The loadedEntityList entries the End holds: the dragon while listed and
 * every crystal in the world. */
int endfight_count(const struct sr_end *e);

#endif
