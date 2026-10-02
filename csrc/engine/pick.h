/* The entity half of Minecraft's pick: EntityRenderer.getMouseOver's pass
 * over World.getEntitiesWithinAABBExcludingEntity, with
 * AxisAlignedBB.calculateIntercept, isVecInside and Vec3.distanceTo exactly
 * as vanilla computes them (distanceTo is MathHelper.sqrt_double, a float).
 * The candidates are whatever the caller's entity list holds, in the list
 * order the chunk walk gives (pick_order), each with its bounding box, its
 * canBeCollidedWith and its getCollisionBorderSize. The client pick
 * (survival.c) and the probe check (tests/test_raypick.c) share it.
 */
#ifndef NETHERITE_PICK_H
#define NETHERITE_PICK_H

#include <stdint.h>

#include "aabb.h"

/* One entity as the pass sees it. */
struct pick_cand {
    struct aabb box;       /* Entity.boundingBox */
    float border;          /* getCollisionBorderSize() */
    int collidable;        /* canBeCollidedWith() */
    int riding;            /* the viewer's ridingEntity */
    int id;                /* the caller's handle, returned as chosen */
};

/* The pass's answer. */
struct pick_result {
    int chosen;            /* index into the candidates, -1 for none */
    double hx, hy, hz;     /* var9, the hit vector (the eye when inside and no intercept) */
    double dist;           /* var12 at the end */
    int replaced;          /* pointedEntity != null && (var12 < var4 || objectMouseOver == null) */
};

/* The kinds whose size, yOffset, canBeCollidedWith and border the pick reads
 * (the entity kinds test_raypick numbers: RayProbe.K_*). */
enum {
    PK_ZOMBIE = 1, PK_LARGE_FIREBALL = 2, PK_SMALL_FIREBALL = 3, PK_FALLING = 4, PK_TNT = 5,
    PK_CRYSTAL = 6, PK_PAINTING = 7, PK_FRAME = 8, PK_DRAGON = 9, PK_PART = 10, PK_ITEM = 11,
    PK_ORB = 12, PK_ARROW = 13, PK_KNOT = 14,
    /* the tracked kinds the pick never tests: EntityThrowable's snowball,
     * egg, pearl, bottle and potion, and EntityEnderEye */
    PK_THROWN = 15, PK_EYE = 16
};

/* setSize and yOffset of a kind (part: the dragon part's index 0..6, head,
 * body, three tails, two wings), and the box Entity.setPosition gives it at
 * (x, y, z) with ySize 0. */
void pick_kind_size(int kind, int part, float *width, float *height, float *y_offset);
struct aabb pick_kind_box(int kind, int part, double x, double y, double z);
/* canBeCollidedWith() of a live entity of the kind, and its border. */
int pick_kind_collidable(int kind);
float pick_kind_border(int kind);

/* AxisAlignedBB.calculateIntercept(start, end): 1 with the hit vector and
 * the face (0..5, the MovingObjectPosition sideHit) when the segment meets
 * the box, 0 for null. */
int pick_intercept(const struct aabb *b, double sx, double sy, double sz,
                   double ex, double ey, double ez,
                   double *hx, double *hy, double *hz, int *side);

/* Vec3.distanceTo: (double)(float)sqrt. */
double pick_distance(double ax, double ay, double az, double bx, double by, double bz);

/* The pass over n candidates in list order, from the eye along look * reach
 * (var2), with the block distance var4 and whether objectMouseOver was set. */
struct pick_result pick_entity_pass(const struct pick_cand *c, int n,
                                    double ex, double ey, double ez,
                                    double lx, double ly, double lz,
                                    double reach, double block_dist, int have_mouse_over);

/* World.getEntitiesWithinAABBExcludingEntity's order over entities that sit
 * in chunk lists: each entry names its chunk (cx, cz), its slice (the
 * Chunk.addEntity y, clamped to 0..15) and its box; parts follow their owner
 * (owner >= 0 names the owner's index; a part only lists when its owner
 * does). seq orders entries within one slice (the list's insertion order).
 * Writes the listed indices to out and returns how many. */
/* The client's object copies (pickobj.c: falling blocks, hanging entities,
 * the dragon, the crystals) */
#define PICKOBJ_MAX_OBJS 4096
/* One pick's candidates: every living copy (clientworld.h CW_MAX_ENTITIES),
 * every object copy and the dragon's seven parts */
#define PICK_MAX_CANDIDATES(cw_max) ((cw_max) + PICKOBJ_MAX_OBJS + 8)

struct pick_entry {
    int cx, cy, cz;
    uint64_t seq;
    int owner;             /* -1, or the index of the entity this is a part of */
    struct aabb box;
};
int pick_order(const struct pick_entry *e, int n, const struct aabb *search, int *out, int cap);

#endif
