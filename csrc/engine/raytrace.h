/* World.func_147447_a, rayTraceBlocks' engine, and Block.collisionRayTrace's
 * box walk: the ray the explosion's getBlockDensity spends per sample, and
 * the one the player's block targeting, buckets and lily pads reuse.
 *
 * Java mutates the start Vec3 in place as the walk advances; the C side
 * carries the point in locals. The per-block bounds are Block's six fields
 * after setBlockBoundsBasedOnState: blocks.h carries the constructor bounds,
 * and raytrace.c computes every state-dependent shape vanilla's setters and
 * collisionRayTrace overrides give (slabs, doors, buttons, fences, walls,
 * torches, rails, vines, ladders, trapdoors, panes and iron bars, the portal,
 * wall signs, skulls, snow layers, chest insets, cocoa, levers, pressure
 * plates, anvils, cakes, stems, the tripwire and its hook, beds, carpets,
 * fence gates, pistons, the daylight detector, the hopper, the end portal
 * and the double plant), plus BlockStairs' eight-octant trace.
 *
 * Block.canCollideCheck: Block's isCollidable() answer (blocks.h collidable),
 * with the two overrides that matter: BlockAir never and BlockLiquid only
 * when stopOnLiquid and the metadata is 0. BlockStairs' delegation is its
 * base block's own answer.
 */
#ifndef NETHERITE_RAYTRACE_H
#define NETHERITE_RAYTRACE_H

#include "../engine/world.h"

/* MovingObjectPosition's block form. type is the face hit, 0..5 (DOWN, UP,
 * NORTH, SOUTH, WEST, EAST); MISS marks the uncollidable stop
 * func_147447_a's var40 carries, which only returnLast ever hands back. */
enum { RT_MISS = -1 };

struct rt_mop {
    int hit;             /* 1 when the Java call returns a position */
    int type;            /* RT_MISS or 0..5 */
    int face;            /* sideHit: type for a hit, the crossing's face for a MISS */
    int x, y, z;
    double hx, hy, hz;   /* hitVec, world coordinates */
};

/* func_147447_a(start, end, stopOnLiquid, ignoreBlockWithoutBoundingBox,
 * returnLastUncollidableBlock). Returns 1 with *out filled, 0 when the Java
 * call returns null. */
int raytrace_blocks(struct world *w, double sx, double sy, double sz,
                    double ex, double ey, double ez,
                    int stop_on_liquid, int ignore_no_bbox, int return_last,
                    struct rt_mop *out);

/* The two entry points World offers, spelled the way callers spell them. */
int raytrace_trace(struct world *w, double sx, double sy, double sz,
                   double ex, double ey, double ez, struct rt_mop *out);

/* Block.collisionRayTrace(world, x, y, z, start, end) for the block at
 * (x, y, z), whatever its id (air too: Block's trace over its bounds), with
 * BlockStairs' override for the stairs. Returns 1 with *out filled, 0 for
 * null. */
int raytrace_collision(struct world *w, int x, int y, int z,
                       double sx, double sy, double sz,
                       double ex, double ey, double ez,
                       struct rt_mop *out);

/* test_raypick's negative check: the stairs pick the octant hit nearest the
 * end instead of the farthest. nw_env->cfg.raytrace_negative_stairs_nearest
 * (env.h). */

/* The block's ray bounds at (x, y, z), after setBlockBoundsBasedOnState (the
 * stairs answer their whole cube), as min x, y, z, max x, y, z in b[0..5].
 * These are the fields collisionRayTrace reads, not the collision pool. */
void raytrace_block_bounds(struct world *w, int x, int y, int z, double *b);

#endif
