/* A minimal Entity: the fields moveEntity reads and writes, and moveEntity
 * itself. Nothing here ticks, and only setPosition and moveEntity are ever
 * called, which is exactly what the move probe drives (ProbeEntity). */
#ifndef NETHERITE_ENTITY_H
#define NETHERITE_ENTITY_H

#include <stdint.h>
#include "aabb.h"

struct world;
struct collide_list;

/* The running count Chunk.addEntity stamps entities with. */

struct entity {
    struct world *world;
    /* when the entity last joined a chunk section list (Chunk.addEntity
     * appends): the list's order, which a chunk save follows */
    uint64_t chunk_stamp;

    double pos_x, pos_y, pos_z;
    double prev_pos_x, prev_pos_y, prev_pos_z;
    struct aabb bounding_box;
    double motion_x, motion_y, motion_z;

    float width, height;
    float step_height;
    float y_offset;
    float y_size;

    float fall_distance;
    float distance_walked_modified;
    float distance_walked_on_step_modified;
    int next_step_distance;

    int fire;
    int fire_resistance;
    uint8_t first_update;    /* Entity's constructor sets it true */

    uint8_t on_ground, is_collided_horizontally, is_collided_vertically, is_collided;
    uint8_t is_in_web, in_water, no_clip, is_sneaking;
    uint8_t field_70135_K; /* the "can be pushed / take collision" flag, true in the constructor */
    uint8_t velocity_changed; /* Entity.velocityChanged, setBeenAttacked sets it */
    uint8_t prevent_entity_spawning; /* Entity.preventEntitySpawning, the checkNoEntityCollision flag */

    /* The Entity virtuals moveEntity reaches, at the Entity defaults the move
     * probe drives: canTriggerWalking true (the walking-distance block runs)
     * and attackEntityFrom bodies that leave nothing the move probe records.
     * item_entity.c overrides them per kind; NULL keeps the default. */
    uint8_t can_trigger_walking;
    uint8_t riding; /* ridingEntity != null: moveEntity's walking block skips a rider (the server player sets it) */
    void *self;

    /* World.getCollidingBoundingBoxes' entity half, for an entity that lives in
     * an entity list: Java adds every other entity's box the query touches
     * after the block boxes. NULL for an entity outside any list (the move
     * probe), which is what leaves that path block-only. */
    void (*extra_boxes)(void *self, struct aabb query, struct collide_list *out);
    void (*attack_from)(void *self, int source, float amount);  /* cactus cell and dealFireDamage(1) */
    void (*fizz)(void *self);                                   /* wet and burning: the sound's rand draws */
    int (*fire_time)(void *self, int ticks);                    /* setFire's EnchantmentProtection.getFireTimeForEntity, NULL keeps ticks */
    void (*swim_sound)(void *self);                             /* the in-water step's swim sound rand draws */
    /* Block.onEntityWalking at the step's base cell, only when the step
     * fires (after the swim-sound branch, both in the same gate). The
     * redstone ore's light-up is the one override; NULL keeps the base's
     * empty body, so entities that never walk on one draw nothing. */
    void (*walking_block)(void *self, int x, int y, int z, int id);
    int (*kind_in_water)(void *self);                           /* Entity.isInWater's virtual override (the squid's box test) */
    void (*fall)(void *self, float distance);                   /* Entity.fall on landing, NULL keeps it empty */
    void (*set_in_web)(void *self);                             /* Entity.setInWeb's virtual override (the spider's is empty) */
    void (*set_in_portal)(void *self);                          /* BlockPortal.onEntityCollidedWithBlock */
    void (*end_portal)(void *self);                             /* BlockEndPortal.onEntityCollidedWithBlock */
};

/* Entity's constructor defaults for the fields above, over a world. */
void entity_init(struct entity *e, struct world *w);

/* Entity.setSize, which also rebuilds the box the vanilla constructor leaves. */
void entity_set_size(struct entity *e, float width, float height);

/* Entity.setPosition: writes the position and sets the bounding box, without
 * touching ySize (Java's setPosition does not) and without snapping to the
 * ground. */
void entity_set_position(struct entity *e, double x, double y, double z);

/* Entity.setInWeb, the protected setter BlockWeb.onEntityCollidedWithBlock
 * reaches through func_145775_I. */
void entity_set_in_web(struct entity *e);

/* The DamageSource families Entity.moveEntity reaches, for the attack_from
 * callback: the cactus cell in the block-collision pass and the
 * dealFireDamage(1) in the standing-in-fire block. */
enum { ENTITY_SRC_CACTUS = 0, ENTITY_SRC_IN_FIRE = 1 };

/* Entity.moveEntity. */
void entity_move(struct entity *e, double dx, double dy, double dz);

/* Entity.moveEntity with the two EntityPlayer-only pieces the probe world
 * never exercises:
 *   - sneak_player: the var20 "on the ground, sneaking, and an EntityPlayer"
 *     clamp, which slows the sneak walk and drops the step-height half;
 *   - fall_state: this.updateFallState is virtual; EntityLivingBase overrides
 *     it (the client player re-checks water and lands on blocks inside the
 *     move) and EntityPlayerMP empties it (the server handler calls
 *     handleFalling instead). NULL keeps Entity's own body, which is what the
 *     probe entity runs. */
typedef void entity_fall_state_fn(struct entity *e, double dy, int on_ground);

/* World.isRaining for Entity.isWet's rain half in moveEntity, per world tick. */
void entity_set_raining(int raining);
void entity_move_ex(struct entity *e, double dx, double dy, double dz, int sneak_player,
                    entity_fall_state_fn *fall_state);

/* What the cells around an entity's box answered, while they stay the same
 * (lane/mobtick). The box tests of a tick (Entity.handleWaterMovement's and
 * handleLavaMovement's material scans, moveEntity's collision boxes, its
 * per-cell callback pass and its fire test) are functions of the box, their
 * other inputs and the cells they read, and read only the chunk columns
 * within a block of the box. A chunk's wseq moves on every block, metadata or
 * light write and on its insertion, and the world's map_ver on every chunk
 * that joins or leaves the map; so the same world, map_ver, box and summed
 * wseq of those chunks (stamp) mean the same cells and the same answers. The
 * memo keeps only answers whose test had no effect (no water, no lava, a
 * still move), so skipping one changes nothing Java would do: no random draw,
 * no write, no call.
 *   known: the box tests answered no (CM_*) since the key was made;
 *   move: a still move from bb: the move asked (d, after the web), the one the
 *     collisions left (o) and the step's inputs; calm: its callback pass met no
 *     block with a body; fire: its fire test's answer, 2 unknown. */
struct cell_memo {
    const struct world *w;
    uint64_t stamp, ws;          /* the chunks' summed wseq; the world's wseq when last seen to hold */
    struct aabb bb;
    uint32_t map_ver;
    uint16_t known;
    uint8_t move, on_ground, calm, fire;
    float y_size, step_height;
    double d[3], o[3];
    double pos[3];               /* the position the CM_AT answers were made at */
};

#define CM_DRY 1        /* the living's water test (World.handleMaterialAcceleration) found no water */
#define CM_NO_LAVA 2    /* handleLavaMovement's box holds no lava */
/* the tests of cells around the position (its eyes), answered at pos */
#define CM_OPEN 4       /* EntityLivingBase's isEntityInsideOpaqueBlock found no normal cube */
#define CM_EYE_DRY 8    /* isInsideOfMaterial(water) answered no */
#define CM_AT (CM_OPEN | CM_EYE_DRY)

/* 1 when m is keyed to (w, bb) over the cells as they are now: the key held,
 * or it did not and m was keyed again (known and move cleared). 0 when no key
 * can be made (a chunk within a block of the box is not loaded, or the box
 * spans more than two chunks a side): m holds nothing. */
int cell_memo_hold(struct cell_memo *m, struct world *w, const struct aabb *bb);

/* cell_memo_hold for the tests around a position (x, y, z: its cells within
 * 1 of it sideways: width at most 2.5 and the position inside the box): the
 * CM_AT answers hold when they were made at this same position. */
int cell_memo_hold_at(struct cell_memo *m, struct world *w, const struct aabb *bb, double x, double y, double z,
                      float width);

/* entity_move_ex with the entity's cell memo (NULL: none). */
void entity_move_memo(struct entity *e, double dx, double dy, double dz, int sneak_player,
                      entity_fall_state_fn *fall_state, struct cell_memo *memo);



/* Entity.isWet for a client copy at (x, y, z) that is not a struct entity:
 * its inWater, or an open column in the rain at the feet or the head. */
int entity_copy_is_wet(struct world *w, int in_water, double x, double y, double z, float height);

#endif
