/* Per-block collision boxes: Block.addCollisionBoxesToList and the
 * getCollisionBoundingBoxFromPool / setBlockBoundsBasedOnState overrides the
 * shape table reaches, in world coordinates as double.
 *
 * Every vanilla block object holds its bounds in six fields and
 * addCollisionBoxesToList mutates them through setBlockBounds; this port
 * computes the six floats per call and widens them to double exactly where
 * Java does, at getCollisionBoundingBoxFromPool. The float arithmetic (the
 * 0.0625F / 0.5625F style constants and the (float)(x + 1) style integer
 * conversions) stays float so the extension of a float to double matches. */
#ifndef NETHERITE_COLLIDE_H
#define NETHERITE_COLLIDE_H

#include <stdint.h>
#include "aabb.h"

struct world;

/* World.collidingBoundingBoxes, an ArrayList: the list is only ever
 * appended to, and one world-wide list is walked after every block call. It
 * starts in its own COLLIDE_MAX_BOXES and moves to a slab block that doubles
 * when a sweep reaches more (a fast mover, or a big one inside blocks of
 * several boxes each: a cauldron or a hopper adds five); the block goes
 * back to the environment's slab when the list does (collide_scratch_end:
 * a pool worker's scratch serves every environment it steps). */
#define COLLIDE_MAX_BOXES 512

struct collide_list {
    struct aabb *box;        /* first, or the slab block once grown */
    int n, cap;
    /* set when a block the scan reached reads or writes more than its own
     * cell and column (a shared block object's bounds, a neighbour's id):
     * entity.c's still-move memo keeps no answer over such a scan */
    int impure;
    struct aabb first[COLLIDE_MAX_BOXES];
};

void collide_list_clear(struct collide_list *l);
void collide_list_push(struct collide_list *l, const struct aabb *b);

/* A collision list from the environment's scratch (too large for a stack
 * frame): collide_scratch_begin takes the next free one, and a variable
 * declared COLLIDE_SCRATCH gives it back when it goes out of scope. Up to
 * COLLIDE_SCRATCH_DEPTH at once (a move inside a move). */
#define COLLIDE_SCRATCH_DEPTH 4
struct collide_list *collide_scratch_begin(void);
void collide_scratch_end(struct collide_list **l);
#define COLLIDE_SCRATCH __attribute__((cleanup(collide_scratch_end)))

/* Block.addCollisionBoxesToList for the block at (x, y, z). Reads the block id,
 * its metadata and the six neighbors the fence, pane, wall, stair and vine
 * boxes test, so it needs the world. */
void collide_add_boxes(struct world *w, int x, int y, int z, const struct aabb *query, struct collide_list *l);
/* the same with the cell's block id (world_get_block's & 4095) already read */
void collide_add_boxes_id(struct world *w, int x, int y, int z, int id, const struct aabb *query, struct collide_list *l);

/* Block.onEntityCollidedWithBlock, Entity.func_145775_I's per-cell call. Only
 * the shapes the table places have a body: web sets isInWeb, soul sand damps
 * motion, cactus deals damage (which the probe entity ignores, see the report).
 * Returns the flags the entity has to apply; the caller owns the entity. */
#define COLLIDE_EFFECT_WEB 1
#define COLLIDE_EFFECT_SOUL_SAND 2
#define COLLIDE_EFFECT_CACTUS 4
/* BlockPressurePlate.onEntityCollidedWithBlock: the caller routes it through
 * the world's on_plate hook (the press is world state: the meta write, the
 * neighbor notifies and the scheduled release tick). */
#define COLLIDE_EFFECT_PLATE 8
/* BlockCauldron.onEntityCollidedWithBlock: the server's extinguish and the
 * level drop, applied by the caller (it reads the entity's fire and box). */
#define COLLIDE_EFFECT_CAULDRON 16
int collide_entity_effect(struct world *w, int x, int y, int z, int id, int meta);

/* Block.getCollisionBoundingBoxFromPool for the block at (x, y, z): the one box
 * the override (or the constructor bounds, when nothing is overridden) reports.
 * 0 when the block's pool box is null (air, liquids, web, vine, an open fence
 * gate), otherwise out is filled and the return is 1. World.func_147469_q's
 * solid check and the item's pushOutOfBlocks read it. */
int collide_pool_box(struct world *w, int x, int y, int z, struct aabb *out);
/* BlockChest's (54) and the trapped chest's (146) shared bounds, as the last
 * setBlockBoundsBasedOnState (a ray trace over one) left them */
void collide_chest_bounds_set(int id, const double *b);
/* BlockLadder.func_149797_b over the ladder object's shared bounds: a meta
 * 2..5 sets them, any other leaves the last ladder's (the air a ladder is
 * placed into, a dev setblock's meta 0); b gets them either way */
void collide_ladder_bounds(int meta, float b[6]);
int collide_pool_box_as(struct world *w, int id, int x, int y, int z, struct aabb *out);
/* BlockFence's shared bounds, as its last setBlockBounds left them */
void collide_fence_bounds_set(int id, float x0, float y0, float z0, float x1, float y1, float z1);
/* The fence, chest and ladder objects' bounds as their constructors leave
 * them, for a fresh environment: its own arrays (env_new runs while another
 * environment is current). */
void collide_env_init(float fence[2][6], float chest[2][6], float ladder[6]);

/* A BlockPane object's (iron bars, the glass pane, the stained pane) shared
 * bounds after a setBlockBounds: addCollisionBoxesToList's last box or a ray
 * trace's setBlockBoundsBasedOnState. Block's getCollisionBoundingBoxFromPool
 * reports them (the constructor's full cube before the first). */
void collide_pane_bounds_set(int id, float x0, float y0, float z0, float x1, float y1, float z1);

/* BlockStairs.collisionRayTrace ran on a stair of this id: its shared bounds
 * are octant 7's from now until its next addCollisionBoxesToList. */
void collide_stairs_traced(int id);
/* 1 while that stair object's shared bounds are octant 7's */
int collide_stairs_octant(int id);

#endif