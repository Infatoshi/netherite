/* Falling blocks and hanging entities: EntityFallingBlock, EntityHanging
 * (EntityPainting, EntityItemFrame), the BlockFalling and BlockDragonEgg
 * update ticks, and ItemHangingEntity.onItemUse, ticked the way the server
 * ticks them. The reference is the oracle's FallHangProbe dump; the shape of
 * the code follows item_entity.c, with the kinds pooled in one list the way
 * World.updateEntities walks its entities.
 *
 * The Det draws are the OTHER role's: the entity constructors (the per-role
 * id, the per-entity Random, the UUID) and, for paintings, the art choice
 * from that Random. Item drops (a landing that cannot place, a broken
 * hanging, the frame paths) are built, recorded through fh_env.drop, and
 * dropped: they never join the list, exactly as the probe takes them out.
 */
#ifndef NETHERITE_FALLHANG_H
#define NETHERITE_FALLHANG_H

#include <stdint.h>

#include "det.h"
#include "entity.h"
#include "arena.h"
#include "envstack.h"
#include "nbtjson.h"
#include "world.h"

enum { FH_FALLING = 1, FH_PAINTING = 2, FH_FRAME = 3, FH_KNOT = 4 /* EntityLeashKnot */ };

/* Why a hanging entity left the list: only the failed 100-tick validity
 * check can break one here (the anvil's fall damage reaches it too, through
 * the same setDead and onBroken path). */
enum { FH_REMOVAL_LANDED = 0, FH_REMOVAL_TIMEOUT = 1, FH_REMOVAL_BROKEN = 10 };

/* One item drop, recorded at construction. */
typedef struct fh_drop {
    int entity_id;
    int64_t uuid_msb, uuid_lsb;
    uint64_t rand_state;      /* the Random Det.newRandom made for the item */
    double x, y, z, motion_x, motion_y, motion_z;
    float yaw, hover;
    int item, damage, count, delay, age;
    int tag;                  /* the stack's tag (itemtag.h), 0 for none */
} fh_drop;

/* The hooks the drop and the landing sound reach. A test installs one for
 * the run; NULL members keep the paths silent. */
struct fh_env {
    void *ctx;
    void (*drop)(void *ctx, const fh_drop *d);
    void (*aux_sfx)(void *ctx, int type, int x, int y, int z, int v);
    /* EntityFallingBlock.fall's entity pass over a whole world (every
     * entity kind, not only this pool's): the anvil's DamageSource.anvil
     * hit of amount on each entity its box meets. NULL keeps the pass
     * over this pool alone. */
    void (*hurt)(void *ctx, struct fh_ent *faller, float amount);
};

/* One entity: the shared Entity fields plus what the three kinds tick. */
typedef struct fh_ent {
    struct entity e;
    struct fh_world *fw;      /* the owning list, for the world queries */
    int kind;
    int entity_id;
    int is_dead;
    int spawn_index;

    det_rng rand;             /* the per-entity Random */
    int64_t uuid_msb, uuid_lsb; /* the entity UUID, for the NBT compare */
    int ticks_existed;
    float rotation_yaw, prev_rotation_yaw;
    float rotation_pitch;     /* Entity.rotationPitch, read from NBT, never ticks */
    double last_tick_x, last_tick_y, last_tick_z;

    /* Chunk.entityLists membership, the way Chunk.addEntity records it. */
    int added_to_chunk, chunk_x, chunk_y, chunk_z;

    /* falling */
    int block;                /* field_145811_e, the block id */
    int meta;                 /* field_145814_a, the block data */
    int time;                 /* field_145812_b */
    int drop_item;            /* field_145813_c */
    int hurt_entities;        /* field_145809_g */
    float hurt_amount;        /* field_145816_i */
    int hurt_max;             /* field_145815_h */
    int broke;                /* field_145808_f */
    nbt *tile_entity_data;    /* field_145810_d, an opaque compound or NULL */

    /* hanging */
    int tile_x, tile_y, tile_z, dir;
    int tick_counter1;
    int art;                  /* paintings: the EnumArt ordinal */
    int rot;                  /* frames: the displayed item's rotation */
    int item, item_damage;    /* frames: the displayed item, item 0 is none */
    int item_tag;             /* frames: its tag (itemtag.h), 0 for none */
} fh_ent;

/* One removal, in the order the pass produced it. extra is the Time field
 * for a falling block, tickCounter1 for a hanging entity. */
typedef struct fh_removal {
    int tick, spawn_index, entity_id, reason, extra;
    double pos_y;
} fh_removal;

#define FH_MAX_ENTITIES 2048

/* The environment's pool (arena.c): a zeroed entity, and its release. */
fh_ent *fh_ent_alloc(void);
void fh_ent_release(fh_ent *en);

/* A reference to a falling or hanging entity (a leash's knot): its slot's
 * generation and index under a tag, 0 for none; fh_get stops the program on
 * a reference whose entity was released. */
typedef uint64_t fhref;
#define FHREF_TAG 0x4648ull
void fhref_stale(fhref h);
static inline fhref fh_ref(const fh_ent *en)
{
    if (en == NULL) return 0;
    uint32_t i = (uint32_t)fh_ent_index(en);
    return FHREF_TAG << 48 | (uint64_t)(nw_arena()->fh_ents.gen[i] & 0xFFFFu) << 32 | i;
}
static inline fh_ent *fh_get(fhref h)
{
    if (h == 0) return NULL;
    uint32_t i = (uint32_t)h;
    if (__builtin_expect(((h >> 32) & 0xFFFFu) != (nw_arena()->fh_ents.gen[i] & 0xFFFFu), 0)) fhref_stale(h);
    return fh_ent_at((int32_t)i);
}
/* The reference's entity was released (removed from its world): a holder
 * keeping it sees a dead entity, as Java's kept reference would. */
static inline int fh_ref_released(fhref h)
{
    uint32_t i = (uint32_t)h;
    return h != 0 && ((h >> 32) & 0xFFFFu) != (nw_arena()->fh_ents.gen[i] & 0xFFFFu);
}
#define FH_SECTIONS 16

typedef struct fh_chunk {
    int used, cx, cz;
    struct sec_list sec[FH_SECTIONS];   /* the fh_ent slots (at most the world's FH_MAX_ENTITIES) */
} fh_chunk;

typedef struct fh_world {
    struct world *w;
    det_state *det;
    int role;
    int32_t slot[FH_MAX_ENTITIES];   /* the entities' pool slots (fh_ent_at) */
    int n;
    fh_chunk *chunks;
    int nchunks, capchunks;
    /* onValidSurface's EntityHanging test over hanging entities outside
     * this pool (the client's copies, for its own onItemUse): nonzero when
     * one meets box. NULL: none. */
    int (*other_hanging)(void *ctx, const struct aabb *box);
    void *other_ctx;
    /* BlockEndPortal.onEntityCollidedWithBlock's Entity.travelToDimension
     * for a falling block (the replay's; NULL none), and the hook after an
     * entity's update (the traveller's destination left again) */
    void (*portal_travel)(struct fh_world *fw, struct fh_ent *en, int to);
    void (*post_update)(struct fh_world *fw);
    void *portal_ctx;
} fh_world;

/* A query's result list: every falling or hanging entity a world can hold
 * (FH_MAX_ENTITIES), from the env's scratch stack for the enclosing scope. */
#define FH_QUERY_LIST(name) fh_ent **name ENV_LOCAL = envstack_take(FH_MAX_ENTITIES * sizeof *name)

/* World.loadedEntityList.add for falling and hanging entities:
 * FH_MAX_ENTITIES, then a loud stop (never a silent drop) */
static inline void fh_list_push(fh_world *fw, const struct fh_ent *en)
{
    if (fw->n >= FH_MAX_ENTITIES) list_full("falling and hanging entities in one world", FH_MAX_ENTITIES);
    fw->slot[fw->n++] = fh_ent_index(en);
}

void fh_init(fh_world *fw, struct world *w, det_state *det);
void fh_free(fh_world *fw);
/* Chunk (cx, cz)'s lists gone when every one is empty (living.h
 * an_chunk_drop_empty); 1 when they went. */
int fh_chunk_drop_empty(fh_world *fw, int cx, int cz);

/* BlockFalling.field_149832_M, the fallInstantly population flag. */
void fh_set_fall_instantly(int on);

/* The EntityFallingBlock(World, x, y, z, block, meta) constructor, Det draws
 * included. The caller spawns it with fh_added_to_world, as
 * spawnEntityInWorld does. */
fh_ent *fh_spawn_falling(fh_world *fw, double x, double y, double z, int block, int meta);

/* The tape replay's alternative to the constructor: a falling block a world
 * tick already constructed, whose Det draws (the per-role id, the per-entity
 * Random and the UUID) the tick's recorder spent. Nothing is drawn here, so
 * the streams stay in the tick's order. */
fh_ent *fh_adopt_falling(fh_world *fw, int entity_id, int64_t uuid_msb, int64_t uuid_lsb,
                         uint64_t rand_state, double x, double y, double z, int block, int meta);

/* The NBT load path: the bare World constructor, then Entity.readFromNBT
 * over the parsed tag, then spawnEntityInWorld. */
fh_ent *fh_spawn_falling_nbt(fh_world *fw, const nbt *tag);

/* The EntityPainting / EntityItemFrame(World, tileX, tileY, tileZ, dir)
 * constructors: the painting's art loop runs here, drawing the choice from
 * the entity's Random. The tile coords are the clicked block's, the
 * direction the hangingDirection. */
fh_ent *fh_spawn_painting(fh_world *fw, int tile_x, int tile_y, int tile_z, int dir);
fh_ent *fh_spawn_frame(fh_world *fw, int tile_x, int tile_y, int tile_z, int dir);

/* The hanging NBT load path (kind FH_PAINTING or FH_FRAME): the World
 * constructor, then readEntityFromNBT's direction, tile, art or displayed
 * item, and setDirection. The caller spawns it with fh_added_to_world. */
fh_ent *fh_spawn_hanging_nbt(fh_world *fw, int kind, const nbt *tag);

/* The EntityLeashKnot(World, x, y, z) constructor: EntityHanging's with
 * direction 0 (the knot's setDirection is empty), then setPosition at the
 * block's centre. The caller spawns it (EntityLeashKnot.func_110129_a). */
fh_ent *fh_spawn_knot(fh_world *fw, int tile_x, int tile_y, int tile_z);

/* EntityHanging.setDirection: the position, the yaw and the bounding box
 * from the tile coords, the direction and the current art. */
void fh_set_direction(fh_ent *en, int dir);

/* World.getEntitiesWithinAABBExcludingEntity over this pool: the chunk walk,
 * the entities whose box meets box; returns how many (at most max_out
 * written). */
int fh_entities_in_box(fh_world *fw, struct aabb box, fh_ent **out, int max_out);
/* The attackEntityFrom a falling anvil's hit reaches on this pool's own
 * kinds: a falling block's setBeenAttacked, a hanging entity's break (an
 * item frame first lets its item go). */
void fh_attacked(fh_world *fw, fh_ent *en);
/* EntityHanging.attackEntityFrom from a survival player's attack (its
 * hitByEntity): setDead, setBeenAttacked and the onBroken drops. */
void fallhang_player_break(fh_ent *en);
/* EntityHanging.onValidSurface. */
int fh_valid_surface(fh_world *fw, fh_ent *en);
/* EntityItemFrame.interactFirst's server half; count is the held stack's
 * size, shrunk by an insert. */
void fh_frame_interact(fh_ent *en, int item, int damage, int tag, int *count);

/* ItemHangingEntity.onItemUse's body: build the entity, check
 * onValidSurface, spawn it when it holds. canPlayerEdit is true (the probe's
 * player can edit). item, damage and rot are the frame's displayed item the
 * probe applies after the spawn; item 0 leaves the frame empty. Returns the
 * spawned entity, NULL when the surface refused or the kind never spawns. */
fh_ent *fh_place_hanging(fh_world *fw, int kind, int tile_x, int tile_y, int tile_z,
                         int dir, int item, int damage, int rot);

/* World.spawnEntityInWorld for the entity just constructed. */
/* Chunk.addEntity from a chunk reload, whether or not the chunk is in the
 * world yet. */
void fh_add_to_chunk(fh_world *fw, fh_ent *en);
void fh_added_to_world(fh_world *fw, fh_ent *en);
/* World.removeEntity's chunk half for a traveller: out of its chunk's list */
void fh_chunk_leave(fh_world *fw, fh_ent *en);

/* BlockFalling.func_149830_m and BlockDragonEgg.func_150018_e, the
 * updateTick bodies ticks_tick_updates dispatches here. */
void fallhang_update_tick(fh_world *fw, int id, int x, int y, int z);

/* World.updateEntities' entity pass for one tick. */
void fh_tick(fh_world *fw, int tick, fh_removal *out, int max_out, int *n_out);

/* The same pass for one entity, in the caller's own order over a pool that
 * also holds other entity kinds (the tape replay: Java's World.updateEntities
 * walks one loadedEntityList in global spawn order). */
int fh_tick_one(fh_world *fw, fh_ent *en, int tick, fh_removal *out, int max_out, int *n_out);

/* Chunk.onChunkUnload for this pool, see ie_unload_chunk. */
int fh_unload_chunk(fh_world *fw, int cx, int cz, fh_ent **out, int cap);

/* Entity.writeToNBT, the canonical NBT the probe records. */
nbt *fh_write_nbt(const fh_ent *en);
/* The same through a writer (nbtw.h), into the open compound. */
struct nbtw;
void fh_write_w(struct nbtw *w, const fh_ent *en);

#endif
