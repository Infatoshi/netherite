/* path_node_processor: verbatim MC 1.11.2 WalkNodeProcessor + NodeProcessor + PathPoint + PathNodeType.
 *
 * Port target (all line refs into java/oracle-src/net/minecraft/pathfinding/ unless noted):
 *   PathNodeType.java          - the 17 node types + their priorities.
 *   PathPoint.java             - makeHash, distanceTo, distanceManhattan, isAssigned.
 *   NodeProcessor.java         - initProcessor (entitySize = floor(w+1)), openPoint (pointMap).
 *   WalkNodeProcessor.java     - getPathNodeTypeRaw, getPathNodeType (1-arg + size-sweep),
 *                                getSafePoint, findPathOptions, getStart, getPathPointToCoords.
 *   World.collidesWithAnyBlock -> world/World.java func_191504_a + Block.addCollisionBoxToList
 *                                + AxisAlignedBB.intersects.
 *
 * INTERPRETIVE LAYER (the only piece the java==cpu==cuda gate can NOT catch, since Golden.java is
 * self-authored this round): the synthetic block model getPathNodeTypeRaw classifies. It is a small
 * explicit table (PnpBlockDef below) transcribing block/BlockAir + getPathNodeTypeRaw's block-identity
 * ternary. A future live-game round verifies this table against real MC; keep it auditable.
 *
 * KEY 1.11.2 FACTS baked in (verified against oracle):
 *   - Block.getBoundingBox default = FULL_BLOCK_AABB (block/Block.java:446); BlockAir overrides only
 *     getCollisionBoundingBox, NOT getBoundingBox -> air's getBoundingBox().maxY == 1.0 too. So in a
 *     full-block world d0 = y everywhere and the 1.125 drop threshold in getSafePoint is inert; all
 *     drops come from the OPEN down-scan bounded by getMaxFallHeight (Entity.java:2970 -> 3).
 *   - getPathPriority (entity/EntityLiving.java:138) = override, else nodeType.getPriority().
 *   - closedSet in PathFinder is dead code (cleared, never read); only PathPoint.visited is used.
 */
#ifndef MC_PATH_NODE_PROCESSOR_H
#define MC_PATH_NODE_PROCESSOR_H

#include "mc.h"
#include <math.h>

/* ---- PathNodeType (PathNodeType.java) : ordinal order MUST match (EnumSet iterates by ordinal) ---- */
enum {
    PNT_BLOCKED = 0,          /* -1 */
    PNT_OPEN,                 /*  0 */
    PNT_WALKABLE,             /*  0 */
    PNT_TRAPDOOR,             /*  0 */
    PNT_FENCE,                /* -1 */
    PNT_LAVA,                 /* -1 */
    PNT_WATER,                /*  8 */
    PNT_RAIL,                 /*  0 */
    PNT_DANGER_FIRE,          /*  8 */
    PNT_DAMAGE_FIRE,          /* 16 */
    PNT_DANGER_CACTUS,        /*  8 */
    PNT_DAMAGE_CACTUS,        /* -1 */
    PNT_DANGER_OTHER,         /*  8 */
    PNT_DAMAGE_OTHER,         /* -1 */
    PNT_DOOR_OPEN,            /*  0 */
    PNT_DOOR_WOOD_CLOSED,     /* -1 */
    PNT_DOOR_IRON_CLOSED,     /* -1 */
    PNT_COUNT
};

MC_HD static inline float pnt_priority(int t) {
    switch (t) {
        case PNT_BLOCKED:          return -1.0f;
        case PNT_OPEN:             return 0.0f;
        case PNT_WALKABLE:         return 0.0f;
        case PNT_TRAPDOOR:         return 0.0f;
        case PNT_FENCE:            return -1.0f;
        case PNT_LAVA:             return -1.0f;
        case PNT_WATER:            return 8.0f;
        case PNT_RAIL:             return 0.0f;
        case PNT_DANGER_FIRE:      return 8.0f;
        case PNT_DAMAGE_FIRE:      return 16.0f;
        case PNT_DANGER_CACTUS:    return 8.0f;
        case PNT_DAMAGE_CACTUS:    return -1.0f;
        case PNT_DANGER_OTHER:     return 8.0f;
        case PNT_DAMAGE_OTHER:     return -1.0f;
        case PNT_DOOR_OPEN:        return 0.0f;
        case PNT_DOOR_WOOD_CLOSED: return -1.0f;
        case PNT_DOOR_IRON_CLOSED: return -1.0f;
        default:                   return -1.0f;
    }
}

/* ---- synthetic block model (the interpretive layer) ---- */
/* Materials we distinguish (block/material/Material.java semantics used by the ternary). */
enum { PM_AIR = 0, PM_ROCK, PM_WATER, PM_LAVA, PM_WOOD, PM_IRON, PM_FIRE };

/* Block ids used in the synthetic grids. */
enum {
    PB_AIR = 0,
    PB_STONE,          /* solid full cube, blocks movement -> BLOCKED */
    PB_WATER,          /* material WATER -> WATER */
    PB_LAVA,           /* material LAVA  -> LAVA */
    PB_FENCE,          /* BlockFence (wood) -> FENCE (impassable via priority -1) */
    PB_DOOR_WC,        /* BlockDoor wood closed -> DOOR_WOOD_CLOSED */
    PB_DOOR_WO,        /* BlockDoor wood open   -> DOOR_OPEN */
    PB_RAIL,           /* BlockRailBase -> RAIL */
    PB_CACTUS,         /* Blocks.CACTUS -> DAMAGE_CACTUS */
    PB_FIRE,           /* Blocks.FIRE   -> DAMAGE_FIRE */
    PB_TRAPDOOR,       /* Blocks.TRAPDOOR -> TRAPDOOR */
    PB_COUNT
};

typedef struct {
    int material;      /* PM_* */
    int isPassable;    /* !material.blocksMovement() adjusted per block (Block.isPassable) */
    /* classification flags for the getPathNodeTypeRaw ternary (WalkNodeProcessor.java:447) */
    int isTrapdoor;    /* TRAPDOOR || IRON_TRAPDOOR || WATERLILY */
    int isFire;        /* Blocks.FIRE */
    int isCactus;      /* Blocks.CACTUS */
    int isDoor;        /* instanceof BlockDoor */
    int doorOpen;      /* BlockDoor.OPEN value */
    int isRail;        /* instanceof BlockRailBase */
    int isFenceLike;   /* BlockFence || BlockWall || (BlockFenceGate && !OPEN) */
    int isBurning;     /* block.isBurning() (magma etc.) - none of ours */
    /* collision box (Block.getCollisionBoundingBox); hasCollision=0 means NULL_AABB */
    int hasCollision;
    float cminX, cminY, cminZ, cmaxX, cmaxY, cmaxZ;
} PnpBlockDef;

MC_HD static inline PnpBlockDef pnp_blockdef(int id) {
    PnpBlockDef d;
    d.material = PM_AIR; d.isPassable = 1;
    d.isTrapdoor = d.isFire = d.isCactus = d.isDoor = d.doorOpen = 0;
    d.isRail = d.isFenceLike = d.isBurning = 0;
    d.hasCollision = 0;
    d.cminX = d.cminY = d.cminZ = 0.0f; d.cmaxX = d.cmaxY = d.cmaxZ = 1.0f;
    switch (id) {
        case PB_AIR:
            d.material = PM_AIR; d.isPassable = 1; break;
        case PB_STONE:
            d.material = PM_ROCK; d.isPassable = 0;
            d.hasCollision = 1; break;                 /* full cube */
        case PB_WATER:
            d.material = PM_WATER; d.isPassable = 1; break;   /* liquid: NULL collision */
        case PB_LAVA:
            d.material = PM_LAVA; d.isPassable = 1; break;
        case PB_FENCE:
            d.material = PM_WOOD; d.isPassable = 0;
            d.isFenceLike = 1;
            d.hasCollision = 1; d.cmaxY = 1.5f; break;  /* 1.5 tall collision */
        case PB_DOOR_WC:
            d.material = PM_WOOD; d.isPassable = 0;
            d.isDoor = 1; d.doorOpen = 0; break;
        case PB_DOOR_WO:
            d.material = PM_WOOD; d.isPassable = 1;
            d.isDoor = 1; d.doorOpen = 1; break;
        case PB_RAIL:
            d.material = PM_ROCK; d.isPassable = 1;
            d.isRail = 1; break;
        case PB_CACTUS:
            d.material = PM_ROCK; d.isPassable = 0;
            d.isCactus = 1; d.hasCollision = 1; break;
        case PB_FIRE:
            d.material = PM_FIRE; d.isPassable = 1;      /* Material.FIRE (NOT air), passable, no collision */
            d.isFire = 1; break;
        case PB_TRAPDOOR:
            d.material = PM_WOOD; d.isPassable = 0;
            d.isTrapdoor = 1; break;
        default: break;
    }
    return d;
}

/* ---- entity config (EntityLiving path fields, explicit per battery case) ---- */
typedef struct {
    float width, height, stepHeight;
    int canSwim, canEnterDoors, canBreakDoors;
    int maxFallHeight;
    float pathPriority[PNT_COUNT];   /* getPathPriority: per-type override table */
    /* start pose */
    double posX, posY, posZ;         /* posY == getEntityBoundingBox().minY */
    int onGround;
    int inWater;                     /* isInWater() */
    /* derived by initProcessor */
    int sizeX, sizeY, sizeZ;
} PnpEntity;

MC_HD static inline float pnp_getPathPriority(const PnpEntity *e, int t) {
    return e->pathPriority[t];   /* pre-seeded with pnt_priority(t), then overridden */
}

/* ---- world grid ---- */
#define PNP_DX 32
#define PNP_DY 24
#define PNP_DZ 32
#define PNP_VOL (PNP_DX * PNP_DY * PNP_DZ)

MC_HD static inline int pnp_in(int x, int y, int z) {
    return x >= 0 && x < PNP_DX && y >= 0 && y < PNP_DY && z >= 0 && z < PNP_DZ;
}
MC_HD static inline int pnp_gidx(int x, int y, int z) {
    return (y * PNP_DZ + z) * PNP_DX + x;
}
MC_HD static inline int pnp_getblock(const u8 *blocks, int x, int y, int z) {
    return pnp_in(x, y, z) ? (int)blocks[pnp_gidx(x, y, z)] : PB_AIR; /* out-of-world -> air */
}
MC_HD static inline void pnp_setblock(u8 *blocks, int x, int y, int z, int id) {
    if (pnp_in(x, y, z)) blocks[pnp_gidx(x, y, z)] = (u8)id;
}

/* ---- PathPoint (PathPoint.java) ---- */
typedef struct {
    int x, y, z;
    int hash;
    int index;                 /* heap index; -1 = not assigned (isAssigned == index>=0) */
    float totalPathDistance;
    float distanceToNext;
    float distanceToTarget;
    int previous;              /* point index, -1 */
    int visited;
    float distanceFromOrigin;
    float cost;
    float costMalus;
    int nodeType;
} PfPoint;

MC_HD static inline int pnp_makeHash(int x, int y, int z) {
    /* PathPoint.java:59 */
    return (y & 255) | ((x & 32767) << 8) | ((z & 32767) << 24)
         | (x < 0 ? (int)0x80000000 : 0) | (z < 0 ? 32768 : 0);
}

MC_HD static inline float pnp_distanceManhattan(const PfPoint *a, const PfPoint *b) {
    float f  = (float)(b->x - a->x < 0 ? a->x - b->x : b->x - a->x);
    float f1 = (float)(b->y - a->y < 0 ? a->y - b->y : b->y - a->y);
    float f2 = (float)(b->z - a->z < 0 ? a->z - b->z : b->z - a->z);
    return f + f1 + f2;
}

MC_HD static inline float pnp_distanceTo(const PfPoint *a, const PfPoint *b) {
    float f  = (float)(b->x - a->x);
    float f1 = (float)(b->y - a->y);
    float f2 = (float)(b->z - a->z);
    return (float)sqrt((double)(f * f + f1 * f1 + f2 * f2)); /* MathHelper.sqrt */
}

/* ---- MathHelper.floor (util/math/MathHelper.java) ---- */
MC_HD static inline int pnp_floor_d(double v) {
    int i = (int)v;
    return v < (double)i ? i - 1 : i;
}

/* ================= NodeProcessor state (pointMap) ================= */
#define PF12_MAX_POINTS 16384
#define PF12_HASH       32768   /* power of two, > 2*MAX_POINTS */

typedef struct {
    u8 blocks[PNP_VOL];
    PnpEntity ent;
    PfPoint points[PF12_MAX_POINTS];
    int npoints;
    int hashtab[PF12_HASH];      /* stores point index + 1; 0 = empty */
    /* heap + finder scratch (defined/used in path_finder.h) */
    int heap[PF12_MAX_POINTS];
    int heapCount;
    int pathOptions[32];
    /* result */
    int resultLen;
    int resultPts[3 * 512];
    float resultDist;
    int overflow;
} Pf12;

MC_HD static inline void pf12_reset_points(Pf12 *p) {
    p->npoints = 0;
    for (int i = 0; i < PF12_HASH; ++i) p->hashtab[i] = 0;
}

/* NodeProcessor.openPoint (NodeProcessor.java:44): mapped point or create+add. */
MC_HD static inline int pnp_openPoint(Pf12 *p, int x, int y, int z) {
    int h = pnp_makeHash(x, y, z);
    unsigned mask = PF12_HASH - 1;
    unsigned slot = (unsigned)h & mask;
    for (;;) {
        int stored = p->hashtab[slot];
        if (stored == 0) {
            if (p->npoints >= PF12_MAX_POINTS) { p->overflow = 1; return 0; }
            int idx = p->npoints++;
            PfPoint *pt = &p->points[idx];
            pt->x = x; pt->y = y; pt->z = z; pt->hash = h;
            pt->index = -1;
            pt->totalPathDistance = 0.0f;
            pt->distanceToNext = 0.0f;
            pt->distanceToTarget = 0.0f;
            pt->previous = -1;
            pt->visited = 0;
            pt->distanceFromOrigin = 0.0f;
            pt->cost = 0.0f;
            pt->costMalus = 0.0f;
            pt->nodeType = PNT_BLOCKED;
            p->hashtab[slot] = idx + 1;
            return idx;
        }
        if (p->points[stored - 1].hash == h) return stored - 1;
        slot = (slot + 1) & mask;
    }
}

/* NodeProcessor.initProcessor: entitySize = floor(w+1) / floor(h+1) (NodeProcessor.java:25). */
MC_HD static inline void pnp_initProcessor(Pf12 *p) {
    pf12_reset_points(p);
    PnpEntity *e = &p->ent;
    e->sizeX = pnp_floor_d((double)(e->width + 1.0f));
    e->sizeY = pnp_floor_d((double)(e->height + 1.0f));
    e->sizeZ = pnp_floor_d((double)(e->width + 1.0f));
}

/* ---- collidesWithAnyBlock (World.func_191504_a, ignoreBlocksWithoutBoundingBox=true) ----
 * For a finite grid we iterate the integer cell range spanning the query box; AABB.intersects is
 * face-exclusive (World.java AxisAlignedBB.intersects: strict <,>), so a slightly wide integer
 * range yields identical results (non-overlapping cells simply fail intersects). */
MC_HD static inline int pnp_collidesWithAnyBlock(const Pf12 *p,
        double minX, double minY, double minZ, double maxX, double maxY, double maxZ) {
    int x0 = pnp_floor_d(minX) - 1, x1 = pnp_floor_d(maxX) + 1;
    int y0 = pnp_floor_d(minY) - 1, y1 = pnp_floor_d(maxY) + 1;
    int z0 = pnp_floor_d(minZ) - 1, z1 = pnp_floor_d(maxZ) + 1;
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z) {
                int id = pnp_getblock(p->blocks, x, y, z);
                PnpBlockDef d = pnp_blockdef(id);
                if (!d.hasCollision) continue;
                double bminX = d.cminX + x, bminY = d.cminY + y, bminZ = d.cminZ + z;
                double bmaxX = d.cmaxX + x, bmaxY = d.cmaxY + y, bmaxZ = d.cmaxZ + z;
                /* AxisAlignedBB.intersects(query, box) */
                if (minX < bmaxX && maxX > bminX &&
                    minY < bmaxY && maxY > bminY &&
                    minZ < bmaxZ && maxZ > bminZ)
                    return 1;
            }
    return 0;
}

/* getBoundingBox().maxY for the block-below drop math. Full-block world => 1.0 for all. */
MC_HD static inline double pnp_boundingMaxY(int id) { (void)id; return 1.0; }

/* ---- getPathNodeTypeRaw (WalkNodeProcessor.java:439) ---- */
MC_HD static inline int pnp_getPathNodeTypeRaw(const Pf12 *p, int x, int y, int z) {
    int id = pnp_getblock(p->blocks, x, y, z);
    PnpBlockDef d = pnp_blockdef(id);
    /* getAiPathNodeType is null for all our blocks. Ternary order per WalkNodeProcessor.java:447:
     * material==AIR first, then trapdoor/waterlily, fire, cactus, doors, rail, fence-else. */
    if (d.material == PM_AIR) return PNT_OPEN;
    if (d.isTrapdoor) return PNT_TRAPDOOR;
    if (d.isFire)     return PNT_DAMAGE_FIRE;
    if (d.isCactus)   return PNT_DAMAGE_CACTUS;
    if (d.isDoor && d.material == PM_WOOD && !d.doorOpen) return PNT_DOOR_WOOD_CLOSED;
    if (d.isDoor && d.material == PM_IRON && !d.doorOpen) return PNT_DOOR_IRON_CLOSED;
    if (d.isDoor && d.doorOpen) return PNT_DOOR_OPEN;
    if (d.isRail) return PNT_RAIL;
    if (!d.isFenceLike) {
        if (d.material == PM_WATER) return PNT_WATER;
        if (d.material == PM_LAVA)  return PNT_LAVA;
        return d.isPassable ? PNT_OPEN : PNT_BLOCKED;
    }
    return PNT_FENCE;
}

/* ---- getPathNodeType 1-arg wrapper (WalkNodeProcessor.java:388) ---- */
MC_HD static inline int pnp_getPathNodeType1(const Pf12 *p, int x, int y, int z) {
    int t = pnp_getPathNodeTypeRaw(p, x, y, z);
    if (t == PNT_OPEN && y >= 1) {
        int belowId = pnp_getblock(p->blocks, x, y - 1, z);
        int t1 = pnp_getPathNodeTypeRaw(p, x, y - 1, z);
        t = (t1 != PNT_WALKABLE && t1 != PNT_OPEN && t1 != PNT_WATER && t1 != PNT_LAVA)
            ? PNT_WALKABLE : PNT_OPEN;
        (void)belowId;   /* no MAGMA block in this model */
        if (t1 == PNT_DAMAGE_FIRE) t = PNT_DAMAGE_FIRE;
        if (t1 == PNT_DAMAGE_CACTUS) t = PNT_DAMAGE_CACTUS;
    }
    if (t == PNT_WALKABLE) {
        for (int j = -1; j <= 1; ++j)
            for (int i = -1; i <= 1; ++i) {
                if (j != 0 || i != 0) {
                    int nid = pnp_getblock(p->blocks, j + x, y, i + z);
                    PnpBlockDef nd = pnp_blockdef(nid);
                    if (nid == PB_CACTUS) t = PNT_DANGER_CACTUS;
                    else if (nid == PB_FIRE) t = PNT_DANGER_FIRE;
                    else if (nd.isBurning) t = PNT_DAMAGE_FIRE;
                }
            }
    }
    return t;
}

/* ---- getPathNodeType size-sweep (WalkNodeProcessor.java:303) ---- */
MC_HD static inline int pnp_getPathNodeTypeSize(const Pf12 *p, int x, int y, int z,
        int xSize, int ySize, int zSize, int canBreakDoors, int canEnterDoors) {
    unsigned enumset = 0u;
    int center = PNT_BLOCKED;
    const PnpEntity *e = &p->ent;
    /* BlockPos of entity for the RAIL-below special case (WalkNodeProcessor.java:331). */
    int ebx = pnp_floor_d(e->posX);
    int eby = pnp_floor_d(e->posY);
    int ebz = pnp_floor_d(e->posZ);
    int entOnRail = pnp_blockdef(pnp_getblock(p->blocks, ebx, eby, ebz)).isRail
                 || pnp_blockdef(pnp_getblock(p->blocks, ebx, eby - 1, ebz)).isRail;

    for (int i = 0; i < xSize; ++i)
        for (int j = 0; j < ySize; ++j)
            for (int k = 0; k < zSize; ++k) {
                int l = i + x, i1 = j + y, j1 = k + z;
                int t1 = pnp_getPathNodeType1(p, l, i1, j1);
                if (t1 == PNT_DOOR_WOOD_CLOSED && canBreakDoors && canEnterDoors) t1 = PNT_WALKABLE;
                if (t1 == PNT_DOOR_OPEN && !canEnterDoors) t1 = PNT_BLOCKED;
                if (t1 == PNT_RAIL && !entOnRail) t1 = PNT_FENCE;
                if (i == 0 && j == 0 && k == 0) center = t1;
                enumset |= (1u << t1);
            }

    if (enumset & (1u << PNT_FENCE)) return PNT_FENCE;

    int best = PNT_BLOCKED;
    for (int t = 0; t < PNT_COUNT; ++t) {        /* EnumSet iterates in ordinal order */
        if (!(enumset & (1u << t))) continue;
        if (pnp_getPathPriority(e, t) < 0.0f) return t;
        if (pnp_getPathPriority(e, t) >= pnp_getPathPriority(e, best)) best = t;
    }
    if (center == PNT_OPEN && pnp_getPathPriority(e, best) == 0.0f) return PNT_OPEN;
    return best;
}

/* getPathNodeType(entity, x,y,z) -> size-sweep with entity's sizes + door flags (line 383). */
MC_HD static inline int pnp_getPathNodeTypeEnt(const Pf12 *p, int x, int y, int z) {
    const PnpEntity *e = &p->ent;
    return pnp_getPathNodeTypeSize(p, x, y, z, e->sizeX, e->sizeY, e->sizeZ,
                                   e->canBreakDoors, e->canEnterDoors);
}

/* ---- getSafePoint (WalkNodeProcessor.java:198) ---- */
MC_HD static inline int pnp_getSafePoint(Pf12 *p, int x, int y, int z,
        int step, double p5, int facingX, int facingZ) {
    int pathpoint = -1;
    int belowId = pnp_getblock(p->blocks, x, y - 1, z);
    double d0 = (double)y - (1.0 - pnp_boundingMaxY(belowId));
    if (d0 - p5 > 1.125) return -1;

    const PnpEntity *e = &p->ent;
    int nodetype = pnp_getPathNodeTypeEnt(p, x, y, z);
    float f = pnp_getPathPriority(e, nodetype);
    double d1 = (double)e->width / 2.0;

    if (f >= 0.0f) {
        pathpoint = pnp_openPoint(p, x, y, z);
        p->points[pathpoint].nodeType = nodetype;
        float cm = p->points[pathpoint].costMalus;
        p->points[pathpoint].costMalus = cm > f ? cm : f;
    }

    if (nodetype == PNT_WALKABLE) return pathpoint;

    if (pathpoint == -1 && step > 0 && nodetype != PNT_FENCE && nodetype != PNT_TRAPDOOR) {
        pathpoint = pnp_getSafePoint(p, x, y + 1, z, step - 1, p5, facingX, facingZ);
        if (pathpoint != -1 &&
            (p->points[pathpoint].nodeType == PNT_OPEN || p->points[pathpoint].nodeType == PNT_WALKABLE) &&
            e->width < 1.0f) {
            double d2 = (double)(x - facingX) + 0.5;
            double d3 = (double)(z - facingZ) + 0.5;
            /* AxisAlignedBB axisalignedbb around the step (line 236) */
            double aMinX = d2 - d1, aMinY = (double)y + 0.001, aMinZ = d3 - d1;
            double aMaxX = d2 + d1, aMaxY = (double)((float)y + e->height), aMaxZ = d3 + d1;
            /* axisalignedbb1 = getBoundingBox(x,y,z) = FULL_BLOCK_AABB; maxY = 1.0 */
            double bb1maxY = pnp_boundingMaxY(pnp_getblock(p->blocks, x, y, z));
            /* addCoord(0, bb1.maxY - 0.002, 0): bb1.maxY - 0.002 > 0 => extend aMaxY */
            double ext = bb1maxY - 0.002;
            double a2MaxY = aMaxY, a2MinY = aMinY;
            if (ext < 0.0) a2MinY += ext; else a2MaxY += ext;
            if (pnp_collidesWithAnyBlock(p, aMinX, a2MinY, aMinZ, aMaxX, a2MaxY, aMaxZ))
                pathpoint = -1;
        }
    }

    if (nodetype == PNT_OPEN) {
        double aMinX = (double)x - d1 + 0.5, aMinY = (double)y + 0.001, aMinZ = (double)z - d1 + 0.5;
        double aMaxX = (double)x + d1 + 0.5, aMaxY = (double)((float)y + e->height), aMaxZ = (double)z + d1 + 0.5;
        if (pnp_collidesWithAnyBlock(p, aMinX, aMinY, aMinZ, aMaxX, aMaxY, aMaxZ)) return -1;

        if (e->width >= 1.0f) {
            int t1 = pnp_getPathNodeTypeEnt(p, x, y - 1, z);
            if (t1 == PNT_BLOCKED) {
                pathpoint = pnp_openPoint(p, x, y, z);
                p->points[pathpoint].nodeType = PNT_WALKABLE;
                float cm = p->points[pathpoint].costMalus;
                p->points[pathpoint].costMalus = cm > f ? cm : f;
                return pathpoint;
            }
        }

        int i = 0;
        while (y > 0 && nodetype == PNT_OPEN) {
            --y;
            if (i++ >= e->maxFallHeight) return -1;
            nodetype = pnp_getPathNodeTypeEnt(p, x, y, z);
            f = pnp_getPathPriority(e, nodetype);
            if (nodetype != PNT_OPEN && f >= 0.0f) {
                pathpoint = pnp_openPoint(p, x, y, z);
                p->points[pathpoint].nodeType = nodetype;
                float cm = p->points[pathpoint].costMalus;
                p->points[pathpoint].costMalus = cm > f ? cm : f;
                break;
            }
            if (f < 0.0f) return -1;
        }
    }

    return pathpoint;
}

/* ---- findPathOptions (WalkNodeProcessor.java:108) ---- */
MC_HD static inline int pnp_findPathOptions(Pf12 *p, int *opts, int curIdx, int targetIdx,
                                            float maxDistance) {
    int i = 0, j = 0;
    const PnpEntity *e = &p->ent;
    PfPoint cur = p->points[curIdx];       /* snapshot coords (openPoint may realloc via index) */
    int cx = cur.x, cy = cur.y, cz = cur.z;

    int headType = pnp_getPathNodeTypeEnt(p, cx, cy + 1, cz);
    if (pnp_getPathPriority(e, headType) >= 0.0f) {
        float sh = e->stepHeight > 1.0f ? e->stepHeight : 1.0f;
        j = pnp_floor_d((double)sh);
    }

    int belowId = pnp_getblock(p->blocks, cx, cy - 1, cz);
    double d0 = (double)cy - (1.0 - pnp_boundingMaxY(belowId));

    int pS = pnp_getSafePoint(p, cx, cy, cz + 1, j, d0, 0, 1);   /* SOUTH z+ */
    int pW = pnp_getSafePoint(p, cx - 1, cy, cz, j, d0, -1, 0);  /* WEST  x- */
    int pE = pnp_getSafePoint(p, cx + 1, cy, cz, j, d0, 1, 0);   /* EAST  x+ */
    int pN = pnp_getSafePoint(p, cx, cy, cz - 1, j, d0, 0, -1);  /* NORTH z- */

    PfPoint *tp = &p->points[targetIdx];

#define PNP_TRY(pp) do { \
        if ((pp) != -1 && !p->points[pp].visited && \
            pnp_distanceTo(&p->points[pp], tp) < maxDistance) opts[i++] = (pp); \
    } while (0)
    PNP_TRY(pS);
    PNP_TRY(pW);
    PNP_TRY(pE);
    PNP_TRY(pN);

    int flag  = (pN == -1 || p->points[pN].nodeType == PNT_OPEN || p->points[pN].costMalus != 0.0f);
    int flag1 = (pS == -1 || p->points[pS].nodeType == PNT_OPEN || p->points[pS].costMalus != 0.0f);
    int flag2 = (pE == -1 || p->points[pE].nodeType == PNT_OPEN || p->points[pE].costMalus != 0.0f);
    int flag3 = (pW == -1 || p->points[pW].nodeType == PNT_OPEN || p->points[pW].costMalus != 0.0f);

    if (flag && flag3) { int q = pnp_getSafePoint(p, cx - 1, cy, cz - 1, j, d0, 0, -1); PNP_TRY(q); }
    if (flag && flag2) { int q = pnp_getSafePoint(p, cx + 1, cy, cz - 1, j, d0, 0, -1); PNP_TRY(q); }
    if (flag1 && flag3){ int q = pnp_getSafePoint(p, cx - 1, cy, cz + 1, j, d0, 0, 1);  PNP_TRY(q); }
    if (flag1 && flag2){ int q = pnp_getSafePoint(p, cx + 1, cy, cz + 1, j, d0, 0, 1);  PNP_TRY(q); }
#undef PNP_TRY

    return i;
}

/* ---- getPathPointToCoords (WalkNodeProcessor.java:103) ---- */
MC_HD static inline int pnp_getPathPointToCoords(Pf12 *p, double x, double y, double z) {
    return pnp_openPoint(p, pnp_floor_d(x), pnp_floor_d(y), pnp_floor_d(z));
}

/* ---- getStart (WalkNodeProcessor.java:44). Battery entities are onGround and not swimming;
 * we transcribe the onGround branch and the priority<0 corner fallback. */
MC_HD static inline int pnp_getStart(Pf12 *p) {
    const PnpEntity *e = &p->ent;
    int i;
    double minY = e->posY;

    if (e->canSwim && e->inWater) {
        i = (int)minY;
        for (int b = pnp_getblock(p->blocks, pnp_floor_d(e->posX), i, pnp_floor_d(e->posZ));
             b == PB_WATER; b = pnp_getblock(p->blocks, pnp_floor_d(e->posX), i, pnp_floor_d(e->posZ)))
            ++i;
    } else if (e->onGround) {
        i = pnp_floor_d(minY + 0.5);
    } else {
        int bx = pnp_floor_d(e->posX), by = pnp_floor_d(minY), bz = pnp_floor_d(e->posZ);
        while ((pnp_blockdef(pnp_getblock(p->blocks, bx, by, bz)).material == PM_AIR ||
                pnp_blockdef(pnp_getblock(p->blocks, bx, by, bz)).isPassable) && by > 0)
            --by;
        i = by + 1;
    }

    int bx = pnp_floor_d(e->posX), bz = pnp_floor_d(e->posZ);
    int t1 = pnp_getPathNodeTypeEnt(p, bx, i, bz);
    if (pnp_getPathPriority(e, t1) < 0.0f) {
        double minX = e->posX - (double)e->width / 2.0, maxX = e->posX + (double)e->width / 2.0;
        double minZ = e->posZ - (double)e->width / 2.0, maxZ = e->posZ + (double)e->width / 2.0;
        double corners[4][2] = { {minX, minZ}, {minX, maxZ}, {maxX, minZ}, {maxX, maxZ} };
        /* Sets.newHashSet -> iteration order is not defined; but corner dedup + the first
         * priority>=0 wins. For battery we ensure start priority>=0 so this path is unused. */
        for (int c = 0; c < 4; ++c) {
            int cxx = pnp_floor_d(corners[c][0]), czz = pnp_floor_d(corners[c][1]);
            int tt = pnp_getPathNodeTypeEnt(p, cxx, i, czz);
            if (pnp_getPathPriority(e, tt) >= 0.0f)
                return pnp_openPoint(p, cxx, i, czz);
        }
    }
    return pnp_openPoint(p, bx, i, bz);
}

/* Seed the entity path-priority table with enum defaults (call before any overrides). */
MC_HD static inline void pnp_ent_default_priorities(PnpEntity *e) {
    for (int t = 0; t < PNT_COUNT; ++t) e->pathPriority[t] = pnt_priority(t);
}

#endif /* MC_PATH_NODE_PROCESSOR_H */
