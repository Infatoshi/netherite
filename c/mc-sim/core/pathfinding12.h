/* pathfinding12: synthetic grid battery + driver for the verbatim 1.11.2 PathFinder port.
 *
 * Drives pf12_findPath over a battery of hand-built grids exercising every WalkNodeProcessor path:
 * flat, wall/maze detour, step-up, 3- and 4-block drops, fences, doors (open/closed), water pools
 * (canSwim on/off + avoid), cactus/fire penalty avoidance, unreachable (partial path), widths 1 & 2.
 *
 * Emits, per case: resultLen (point count), then x,y,z of each point (start..end), then the IEEE-754
 * bits of end.totalPathDistance. All as %08x. cpu / cuda / Golden.java produce identical streams.
 */
#ifndef MC_PATHFINDING12_H
#define MC_PATHFINDING12_H

#include "path_finder.h"
#include <string.h>

#define PF12_NUM_CASES 17
#define PF12_FLOOR_Y   4     /* solid floor plane; walkable air cell is y=5 */
#define PF12_WALK_Y    5

typedef struct {
    double tx, ty, tz;   /* target block-center coords passed to findPath */
    float maxDistance;
} Pf12Case;

MC_HD static inline void pf12_fill_air(u8 *b) {
    for (int i = 0; i < PNP_VOL; ++i) b[i] = PB_AIR;
}

MC_HD static inline void pf12_floor(u8 *b, int x0, int z0, int x1, int z1, int y, int id) {
    for (int x = x0; x <= x1; ++x)
        for (int z = z0; z <= z1; ++z)
            pnp_setblock(b, x, y, z, id);
}

MC_HD static inline void pf12_box(u8 *b, int x0, int y0, int z0, int x1, int y1, int z1, int id) {
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z)
                pnp_setblock(b, x, y, z, id);
}

/* Standard width-1 mob (zombie-ish): 0.6 x 1.95, stepHeight 0.6, maxFall 3. */
MC_HD static inline void pf12_ent_w1(PnpEntity *e) {
    memset(e, 0, sizeof(*e));
    e->width = 0.6f; e->height = 1.95f; e->stepHeight = 0.6f;
    e->canSwim = 0; e->canEnterDoors = 1; e->canBreakDoors = 0;
    e->maxFallHeight = 3;
    e->onGround = 1; e->inWater = 0;
    pnp_ent_default_priorities(e);
}

/* Wide mob: 1.4 x 1.95 -> entitySize 2 x 2 x 2. */
MC_HD static inline void pf12_ent_w2(PnpEntity *e) {
    pf12_ent_w1(e);
    e->width = 1.4f;
}

MC_HD static inline void pf12_set_start(PnpEntity *e, int bx, int bz) {
    e->posX = (double)bx + 0.5;
    e->posY = (double)PF12_WALK_Y;   /* minY of bounding box == walkable cell */
    e->posZ = (double)bz + 0.5;
}

/* Build case: fills p->blocks and p->ent, returns target + maxDistance. */
MC_HD static inline Pf12Case pf12_build_case(Pf12 *p, int id, i64 seed) {
    u8 *b = p->blocks;
    PnpEntity *e = &p->ent;
    Pf12Case c;
    pf12_fill_air(b);
    pf12_floor(b, 0, 0, 20, 20, PF12_FLOOR_Y, PB_STONE);  /* default full floor */
    pf12_ent_w1(e);
    pf12_set_start(e, 2, 2);
    c.tx = 14.5; c.ty = (double)PF12_WALK_Y + 0.5; c.tz = 8.5;
    c.maxDistance = 64.0f;

    switch (id) {
        case 0: /* flat open */
            break;
        case 1: /* wall at x=8 (z 1..12), detour past z=13 */
            pf12_box(b, 8, 5, 1, 8, 7, 12, PB_STONE);
            break;
        case 2: { /* maze: two offset walls */
            pf12_box(b, 6, 5, 0, 6, 7, 10, PB_STONE);
            pf12_box(b, 10, 5, 4, 10, 7, 14, PB_STONE);
            break;
        }
        case 3: /* 1-block step-up: floor rises to y=5 for x>=9 (walk cell y=6) */
            pf12_floor(b, 9, 0, 20, 20, 5, PB_STONE);
            c.tz = 8.5; c.tx = 14.5; c.ty = 6.5;
            break;
        case 4: /* 3-block drop: floor drops to y=1 for x>=9 (walk cell y=2) */
            pf12_box(b, 9, 2, 0, 20, 4, 20, PB_AIR);   /* carve down */
            pf12_floor(b, 9, 0, 20, 20, 1, PB_STONE);
            c.tx = 14.5; c.ty = 2.5; c.tz = 8.5;
            break;
        case 5: /* 4-block drop: floor drops to y=0 for x>=9 -> exceeds maxFall(3), detour/partial */
            pf12_box(b, 9, 1, 0, 20, 4, 20, PB_AIR);
            pf12_floor(b, 9, 0, 20, 20, 0, PB_STONE);
            c.tx = 14.5; c.ty = 1.5; c.tz = 8.5;
            break;
        case 6: /* fence line at x=8 (z 1..12): impassable despite short box, detour */
            pf12_floor(b, 8, 1, 8, 12, 5, PB_FENCE);
            break;
        case 7: /* closed wood door in a wall; canBreak=false -> blocked, detour */
            pf12_box(b, 8, 5, 1, 8, 7, 12, PB_STONE);
            pnp_setblock(b, 8, 5, 6, PB_DOOR_WC);
            pnp_setblock(b, 8, 6, 6, PB_AIR);
            break;
        case 8: /* open wood door in a wall -> path through the gap */
            pf12_box(b, 8, 5, 1, 8, 7, 12, PB_STONE);
            pnp_setblock(b, 8, 5, 6, PB_DOOR_WO);
            pnp_setblock(b, 8, 6, 6, PB_AIR);
            break;
        case 9: /* full-width water strip at x=8: forced crossing, costMalus 8 in total dist */
            pf12_box(b, 8, 5, 0, 8, 5, 20, PB_WATER);
            break;
        case 10: /* same water strip, canSwim on (land start -> getStart swim branch is a no-op) */
            pf12_box(b, 8, 5, 0, 8, 5, 20, PB_WATER);
            e->canSwim = 1;
            break;
        case 11: /* same water strip, avoid water (priority < 0) -> impassable, partial path */
            pf12_box(b, 8, 5, 0, 8, 5, 20, PB_WATER);
            e->pathPriority[PNT_WATER] = -1.0f;
            break;
        case 12: /* full-width stone wall, single gap at z=6, cactus post at z=5 makes the gap
                     cell DANGER_CACTUS -> forced penalty crossing (costMalus 8) */
            pf12_box(b, 8, 5, 0, 8, 7, 20, PB_STONE);
            pnp_setblock(b, 8, 5, 6, PB_AIR);
            pnp_setblock(b, 8, 6, 6, PB_AIR);
            pnp_setblock(b, 8, 7, 6, PB_AIR);
            pnp_setblock(b, 8, 5, 5, PB_CACTUS);
            break;
        case 13: /* same, fire post at z=5 -> gap cell DANGER_FIRE (costMalus 8) */
            pf12_box(b, 8, 5, 0, 8, 7, 20, PB_STONE);
            pnp_setblock(b, 8, 5, 6, PB_AIR);
            pnp_setblock(b, 8, 6, 6, PB_AIR);
            pnp_setblock(b, 8, 7, 6, PB_AIR);
            pnp_setblock(b, 8, 5, 5, PB_FIRE);
            break;
        case 14: /* unreachable target fully boxed in stone -> partial path to closest */
            pf12_box(b, 12, 5, 6, 16, 8, 10, PB_STONE);   /* solid block enclosing (14,5,8) */
            c.tx = 14.5; c.ty = 5.5; c.tz = 8.5;
            break;
        case 15: /* flat open, wide mob (size 2) */
            pf12_ent_w2(e);
            pf12_set_start(e, 2, 2);
            break;
        case 16: /* wall detour, wide mob (size 2) */
            pf12_ent_w2(e);
            pf12_set_start(e, 2, 2);
            pf12_box(b, 8, 5, 1, 8, 7, 12, PB_STONE);
            break;
        default:
            break;
    }
    (void)seed;
    return c;
}

typedef void (*Pf12EmitFn)(u32 v, void *ctx);

MC_HD static inline u32 pf12_fbits(float f) {
    u32 u;
    memcpy(&u, &f, sizeof(u));
    return u;
}

MC_HD static inline void pf12_run_case(Pf12 *p, int id, i64 seed, Pf12EmitFn emit, void *ctx) {
    Pf12Case c = pf12_build_case(p, id, seed);
    p->overflow = 0;
    int n = pf12_findPath(p, c.tx, c.ty, c.tz, c.maxDistance);
    emit((u32)n, ctx);
    for (int i = 0; i < n; ++i) {
        emit((u32)p->resultPts[i * 3 + 0], ctx);
        emit((u32)p->resultPts[i * 3 + 1], ctx);
        emit((u32)p->resultPts[i * 3 + 2], ctx);
    }
    if (n > 0) emit(pf12_fbits(p->resultDist), ctx);
}

MC_HD static inline void pf12_run_all(i64 seed, Pf12 *p, Pf12EmitFn emit, void *ctx) {
    for (int i = 0; i < PF12_NUM_CASES; ++i)
        pf12_run_case(p, i, seed, emit, ctx);
}

#endif /* MC_PATHFINDING12_H */
