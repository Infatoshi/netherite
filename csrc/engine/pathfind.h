/* The pathfinder: World.getEntityPathToXYZ and getPathEntityToEntity,
 * PathFinder (the A* over PathPoints with the Path heap and the point table
 * keyed by PathPoint.makeHash), getSafePoint, findPathOptions and
 * getVerticalOffset with every block rule, PathEntity and PathPoint. Ported
 * from oracle/src/pathfinding/PathFinder.java, Path.java, PathPoint.java,
 * PathEntity.java and the two World methods (World.java lines 3461 and 3480).
 *
 * Pathfinding reads blocks and writes nothing, so it is a pure function of
 * the world, the entity's geometry and the flags. The block rules are the
 * overrides getVerticalOffset reaches: BlockDoor (open bit from the lower
 * half's metadata), BlockFence and BlockWall (always block), BlockFenceGate
 * and BlockTrapDoor (their open bits), the two liquid classes (lava blocks,
 * water does not), the sign and pressure plate classes (never block) and the
 * default (the material's blocksMovement).
 *
 * Vanilla quirks kept: the point table is keyed by PathPoint.makeHash alone,
 * so two coordinates that hash the same share one PathPoint; the heap's
 * sortBack keeps a child on a tie (only a strictly smaller key rises); and
 * findPathOptions' candidate filter compares the candidate's float distance
 * to the target against maxDistance. */
#ifndef NETHERITE_PATHFIND_H
#define NETHERITE_PATHFIND_H

#include <stdint.h>
#include "aabb.h"

struct world;

/* One PathPoint (PathPoint.java): the coordinates, the hash, the heap index
 * (-1 when not in the heap), the three float distances, the predecessor and
 * the isFirst flag. */
struct pf_point {
    int x, y, z;
    int32_t hash;
    int index;
    float total, next, target;
    int prev;        /* pool index, -1 for none */
    int is_first;
    int slot;        /* its key's slot in the finder's map (pf_clear_map) */
};

/* The fields of an Entity the pathfinder reads: the position, the size, the
 * bounding box as setPosition built it (minY == posY when yOffset and ySize
 * are 0), Entity.isInWater and Entity.getMaxSafePointTries(). */
struct pf_entity {
    double pos_x, pos_y, pos_z;
    float width, height;
    struct aabb box;
    int in_water;
    int max_safe_point_tries;
    float y_offset, y_size;
};

/* Entity.setPosition's bounding box for a probe-style entity (never moved). */
void pf_entity_set_position(struct pf_entity *e, double x, double y, double z);

/* The most points one search opens (a search past it stops the program). */
#define PF_MAX_POINTS 262144

/* The finder's arrays, one set per environment (arena.h). */
struct pf_store {
    struct pf_point *pts;
    int *heap;
    int32_t *keys;
    int *vals;
    int *out;                 /* a path's points, x y z each: a path has at most one per point opened */
};

struct pf {
    struct world *w;
    /* the searching entity's worldObj, which func_82565_a reads (the
     * ChunkCache, w, only backs the start's water walk); NULL: w */
    struct world *entity_w;

    /* The four PathFinder flags and the search parameters. */
    int wooden_door_allowed;
    int movement_block_allowed;
    int pathing_in_water;
    int can_entity_drown;
    float max_dist;

    /* The entity in the search: the fields getVerticalOffset and the heap
     * reads (position, size, box, water, the safe-point tries). */
    double pos_x, pos_y, pos_z;
    float width, height;
    struct aabb box;
    int in_water;
    int max_safe_tries;

    /* The search size (PathPoint(floor(width + 1), floor(height + 1), ...)),
     * the third argument of createEntityPathTo. */
    int size_x, size_y, size_z;
    /* this search's stamp in the environment's vertical_offset memo */
    uint32_t vstamp;

    /* PathFinder state: the point pool (PathPoint + the IntHashMap), the
     * Path heap and the 32-slot options array. The arrays are the
     * environment's (arena.h pf_store, PF_MAX_POINTS points), bound at the
     * first search. */
    struct pf_point *pts;
    int npts, pcap;
    int *heap;
    int nheap, hcap;
    int32_t *keys;
    int *vals;
    int mcap;
    int options[32];

    /* Where build_path emits (the caller's output buffer). */
    int *out;
    int out_cap;
};

void pf_init(struct pf *f, struct world *w);
void pf_free(struct pf *f);

/* World.getEntityPathToXYZ. Returns the number of path points (0 for a
 * one-point path), or -1 for a null path; the points go into out (x, y, z
 * per point) in path order. */
int pf_get_entity_path_to_xyz(struct pf *f, const struct pf_entity *e, int x, int y, int z, float max_dist,
                              int door_open, int door_closed, int avoid_water, int can_swim, int *out, int out_cap);

/* World.getPathEntityToEntity: the target entity's posX, boundingBox.minY and
 * posZ are the target coordinates. */
int pf_get_path_entity_to_entity(struct pf *f, const struct pf_entity *e, double tx, double ty, double tz,
                                 float max_dist, int door_open, int door_closed, int avoid_water, int can_swim,
                                 int *out, int out_cap);

/* PathFinder.func_82565_a(entity, x, y, z, size, pathInWater,
 * movementBlockAllowed, woodenDoorAllowed) for an entity at pos with box,
 * outside a search (EntityAIControlledByPlayer's step test). */
int pf_vertical_offset_at(struct world *w, double pos_x, double pos_y, double pos_z, struct aabb box,
                          int x, int y, int z, int sx, int sy, int sz,
                          int pathing_in_water, int movement_block_allowed, int wooden_door_allowed);

#endif
