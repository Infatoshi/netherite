/* physics_collision_math: exact C port of MC 1.11.2 Entity.move collision core
 * (net/minecraft/entity/Entity.java move(MoverType,double,double,double)) plus the
 * AxisAlignedBB.calculate{X,Y,Z}Offset / offset / addCoord / intersectsWith math it calls
 * (net/minecraft/util/math/AxisAlignedBB.java), and resetPositionToBB.
 *
 * Vanilla-bit-exact double-precision physics: no RNG. Replicate Java operator order and every
 * (int)/(double) cast. Build C with -ffp-contract=off, CUDA with --fmad=false (the runner does).
 *
 * SCOPE v1 (MoverType.SELF, a single deterministic collision step). The world getCollisionBoxes
 * query is reduced to iterating a FIXED baked list of solid block AABBs and collecting those that
 * intersect the motion-expanded entity box - byte-identical logic in golden and candidate
 * (Block.addCollisionBoxToList = entityBox.intersectsWith(blockBox), strict inequalities).
 *
 * Resolution order is vanilla's: Y first, then X, then Z (Entity.java lines 819-859).
 *
 * BRANCHES TRIMMED (all provably inactive for this configuration, output bit-identical):
 *   - noClip          : false.
 *   - MoverType.PISTON: type is SELF.
 *   - isInWeb         : false.
 *   - sneak step-back : requires (this instanceof EntityPlayer); our entity is a generic Entity.
 *   - stepHeight step : CUT only in the legacy mc_entity_move (stepHeight = 0.0F). The full
 *     vanilla auto-step branch lives in mc_entity_move_step(step_height); the player passes
 *     0.6f (EntityLivingBase ctor).
 *   - updateFallState / fence-wall feet lookup / walking + swim sounds / fire / doBlockCollisions:
 *     side effects with no bearing on the emitted resolved state.
 * BRANCHES KEPT (verbatim): the Y/X/Z offset resolution, resetPositionToBB, the collided* flags,
 *   onGround, and the motion zeroing. motionY zeroing on vertical collision is Block.onLanded for a
 *   STANDARD block (entityIn.motionY = 0.0D); slime/bed bounce blocks are CUT (v1 uses solid cubes).
 *
 * Math.min/Math.max in the AABB constructor are no-ops here: offset()/addCoord() preserve min<=max
 * (same delta added to both edges), so direct assignment is bit-identical to `new AxisAlignedBB`. */
#ifndef MC_PHYSICS_COLLISION_MATH_H
#define MC_PHYSICS_COLLISION_MATH_H

#include "mc.h"

#define MC_PCM_MAX_BLOCKS 64

typedef struct {
    double minX, minY, minZ, maxX, maxY, maxZ;
} McAABB;

typedef struct {
    McAABB box;
    double posX, posY, posZ;
    double motionX, motionY, motionZ;
    int onGround;               /* in (initial) / out (resolved) */
    int collidedHorizontally;   /* out */
    int collidedVertically;     /* out */
    int isCollided;             /* out */
} McEntity;

MC_HD static inline McAABB mc_aabb_make(double x0, double y0, double z0,
                                        double x1, double y1, double z1) {
    McAABB r; r.minX = x0; r.minY = y0; r.minZ = z0; r.maxX = x1; r.maxY = y1; r.maxZ = z1;
    return r;
}

/* AxisAlignedBB.offset(x,y,z) */
MC_HD static inline McAABB mc_aabb_offset(const McAABB *bb, double x, double y, double z) {
    return mc_aabb_make(bb->minX + x, bb->minY + y, bb->minZ + z,
                        bb->maxX + x, bb->maxY + y, bb->maxZ + z);
}

/* AxisAlignedBB.addCoord(x,y,z) */
MC_HD static inline McAABB mc_aabb_addcoord(const McAABB *bb, double x, double y, double z) {
    double d0 = bb->minX;
    double d1 = bb->minY;
    double d2 = bb->minZ;
    double d3 = bb->maxX;
    double d4 = bb->maxY;
    double d5 = bb->maxZ;
    if (x < 0.0)      { d0 += x; }
    else if (x > 0.0) { d3 += x; }
    if (y < 0.0)      { d1 += y; }
    else if (y > 0.0) { d4 += y; }
    if (z < 0.0)      { d2 += z; }
    else if (z > 0.0) { d5 += z; }
    return mc_aabb_make(d0, d1, d2, d3, d4, d5);
}

/* AxisAlignedBB.intersectsWith(other) -> intersects(other.min.., other.max..) */
MC_HD static inline int mc_aabb_intersects(const McAABB *a, const McAABB *b) {
    return a->minX < b->maxX && a->maxX > b->minX &&
           a->minY < b->maxY && a->maxY > b->minY &&
           a->minZ < b->maxZ && a->maxZ > b->minZ;
}

/* AxisAlignedBB.calculateXOffset(other, offsetX): a = this (block box), b = other (entity box). */
MC_HD static inline double mc_aabb_calcXOffset(const McAABB *a, const McAABB *b, double offsetX) {
    if (b->maxY > a->minY && b->minY < a->maxY && b->maxZ > a->minZ && b->minZ < a->maxZ) {
        if (offsetX > 0.0 && b->maxX <= a->minX) {
            double d1 = a->minX - b->maxX;
            if (d1 < offsetX) { offsetX = d1; }
        } else if (offsetX < 0.0 && b->minX >= a->maxX) {
            double d0 = a->maxX - b->minX;
            if (d0 > offsetX) { offsetX = d0; }
        }
        return offsetX;
    } else {
        return offsetX;
    }
}

/* AxisAlignedBB.calculateYOffset(other, offsetY). */
MC_HD static inline double mc_aabb_calcYOffset(const McAABB *a, const McAABB *b, double offsetY) {
    if (b->maxX > a->minX && b->minX < a->maxX && b->maxZ > a->minZ && b->minZ < a->maxZ) {
        if (offsetY > 0.0 && b->maxY <= a->minY) {
            double d1 = a->minY - b->maxY;
            if (d1 < offsetY) { offsetY = d1; }
        } else if (offsetY < 0.0 && b->minY >= a->maxY) {
            double d0 = a->maxY - b->minY;
            if (d0 > offsetY) { offsetY = d0; }
        }
        return offsetY;
    } else {
        return offsetY;
    }
}

/* AxisAlignedBB.calculateZOffset(other, offsetZ). */
MC_HD static inline double mc_aabb_calcZOffset(const McAABB *a, const McAABB *b, double offsetZ) {
    if (b->maxX > a->minX && b->minX < a->maxX && b->maxY > a->minY && b->minY < a->maxY) {
        if (offsetZ > 0.0 && b->maxZ <= a->minZ) {
            double d1 = a->minZ - b->maxZ;
            if (d1 < offsetZ) { offsetZ = d1; }
        } else if (offsetZ < 0.0 && b->minZ >= a->maxZ) {
            double d0 = a->maxZ - b->minZ;
            if (d0 > offsetZ) { offsetZ = d0; }
        }
        return offsetZ;
    } else {
        return offsetZ;
    }
}

/* Entity.move(MoverType.SELF, x, y, z) collision slice, WITH the stepHeight auto-step
 * branch (Entity.java "if (this.stepHeight > 0.0F && flag && ...)", verbatim port).
 * step_height = 0 reproduces the old trimmed behavior bit-for-bit (branch never taken);
 * the PLAYER passes 0.6f (EntityLivingBase ctor: this.stepHeight = 0.6F).
 * `blocks` must cover the step query too: box.addCoord(x, step_height, z) - callers
 * widen their broadphase collect by +step_height upward. Both step collision lists are
 * re-filtered here with the exact vanilla per-call query (intersectsWith, strict), so a
 * superset broadphase cannot change results.
 * Found at t417 of the fresh-world tape 20260712T055346Z: walking off a ledge corner,
 * vanilla's step retry (gated on the PREVIOUS tick's onGround) climbs 0.6, clears the
 * corner in x/z, and lands back on the ledge top - the trimmed move clamped z at the
 * ledge face and fell. */
MC_HD static inline void mc_entity_move_step(McEntity *e, double x, double y, double z,
                                             const McAABB *blocks, int nblocks,
                                             float step_height) {
    double d2 = x;
    double d3 = y;
    double d4 = z;
    int entry_on_ground = e->onGround;   /* this.onGround: previous move's result */

    /* list1 = world.getCollisionBoxes(this, this.getEntityBoundingBox().addCoord(x, y, z)),
     * reduced to the baked block list: keep boxes intersecting the motion-expanded entity box. */
    McAABB query = mc_aabb_addcoord(&e->box, x, y, z);
    McAABB list1[MC_PCM_MAX_BLOCKS];
    int n1 = 0;
    for (int i = 0; i < nblocks; ++i) {
        if (mc_aabb_intersects(&query, &blocks[i])) {
            if (n1 >= MC_PCM_MAX_BLOCKS) break;   /* guard the fixed buffer; real wiring must size for max hits */
            list1[n1] = blocks[i];
            ++n1;
        }
    }
    McAABB axisalignedbb = e->box;   /* AxisAlignedBB axisalignedbb = this.getEntityBoundingBox(); */

    if (y != 0.0) {
        for (int k = 0; k < n1; ++k) {
            y = mc_aabb_calcYOffset(&list1[k], &e->box, y);
        }
        e->box = mc_aabb_offset(&e->box, 0.0, y, 0.0);
    }

    if (x != 0.0) {
        for (int k = 0; k < n1; ++k) {
            x = mc_aabb_calcXOffset(&list1[k], &e->box, x);
        }
        if (x != 0.0) {
            e->box = mc_aabb_offset(&e->box, x, 0.0, 0.0);
        }
    }

    if (z != 0.0) {
        for (int k = 0; k < n1; ++k) {
            z = mc_aabb_calcZOffset(&list1[k], &e->box, z);
        }
        if (z != 0.0) {
            e->box = mc_aabb_offset(&e->box, 0.0, 0.0, z);
        }
    }

    /* boolean flag = this.onGround || d3 != y && d3 < 0.0D; */
    {
        int flag = (entry_on_ground || (d3 != y && d3 < 0.0)) ? 1 : 0;

        if (step_height > 0.0f && flag && (d2 != x || d4 != z)) {
            double d14 = x;
            double d6 = y;
            double d7 = z;
            McAABB axisalignedbb1 = e->box;
            e->box = axisalignedbb;
            y = (double)step_height;

            /* list = world.getCollisionBoxes(this, this.getEntityBoundingBox().addCoord(d2, y, d4)) */
            McAABB q2 = mc_aabb_addcoord(&e->box, d2, y, d4);
            McAABB list[MC_PCM_MAX_BLOCKS];
            int nl = 0;
            for (int i = 0; i < nblocks; ++i) {
                if (mc_aabb_intersects(&q2, &blocks[i])) {
                    if (nl >= MC_PCM_MAX_BLOCKS) break;
                    list[nl] = blocks[i];
                    ++nl;
                }
            }

            McAABB axisalignedbb2 = e->box;
            McAABB axisalignedbb3 = mc_aabb_addcoord(&axisalignedbb2, d2, 0.0, d4);
            double d8 = y;
            for (int k = 0; k < nl; ++k) d8 = mc_aabb_calcYOffset(&list[k], &axisalignedbb3, d8);
            axisalignedbb2 = mc_aabb_offset(&axisalignedbb2, 0.0, d8, 0.0);
            double d18 = d2;
            for (int k = 0; k < nl; ++k) d18 = mc_aabb_calcXOffset(&list[k], &axisalignedbb2, d18);
            axisalignedbb2 = mc_aabb_offset(&axisalignedbb2, d18, 0.0, 0.0);
            double d19 = d4;
            for (int k = 0; k < nl; ++k) d19 = mc_aabb_calcZOffset(&list[k], &axisalignedbb2, d19);
            axisalignedbb2 = mc_aabb_offset(&axisalignedbb2, 0.0, 0.0, d19);

            McAABB axisalignedbb4 = e->box;
            double d20 = y;
            for (int k = 0; k < nl; ++k) d20 = mc_aabb_calcYOffset(&list[k], &axisalignedbb4, d20);
            axisalignedbb4 = mc_aabb_offset(&axisalignedbb4, 0.0, d20, 0.0);
            double d21 = d2;
            for (int k = 0; k < nl; ++k) d21 = mc_aabb_calcXOffset(&list[k], &axisalignedbb4, d21);
            axisalignedbb4 = mc_aabb_offset(&axisalignedbb4, d21, 0.0, 0.0);
            double d22 = d4;
            for (int k = 0; k < nl; ++k) d22 = mc_aabb_calcZOffset(&list[k], &axisalignedbb4, d22);
            axisalignedbb4 = mc_aabb_offset(&axisalignedbb4, 0.0, 0.0, d22);

            double d23 = d18 * d18 + d19 * d19;
            double d9 = d21 * d21 + d22 * d22;

            if (d23 > d9) {
                x = d18;
                z = d19;
                y = -d8;
                e->box = axisalignedbb2;
            } else {
                x = d21;
                z = d22;
                y = -d20;
                e->box = axisalignedbb4;
            }

            for (int k = 0; k < nl; ++k) y = mc_aabb_calcYOffset(&list[k], &e->box, y);
            e->box = mc_aabb_offset(&e->box, 0.0, y, 0.0);

            if (d14 * d14 + d7 * d7 >= x * x + z * z) {
                x = d14;
                y = d6;
                z = d7;
                e->box = axisalignedbb1;
            }
        }
    }

    /* resetPositionToBB() */
    e->posX = (e->box.minX + e->box.maxX) / 2.0;
    e->posY = e->box.minY;
    e->posZ = (e->box.minZ + e->box.maxZ) / 2.0;

    e->collidedHorizontally = (d2 != x || d4 != z) ? 1 : 0;
    e->collidedVertically = (d3 != y) ? 1 : 0;
    e->onGround = (e->collidedVertically && d3 < 0.0) ? 1 : 0;
    e->isCollided = (e->collidedHorizontally || e->collidedVertically) ? 1 : 0;

    if (d2 != x) { e->motionX = 0.0; }
    if (d4 != z) { e->motionZ = 0.0; }
    if (d3 != y) { e->motionY = 0.0; }   /* Block.onLanded (standard block): motionY = 0.0D */
}

/* Legacy entry point: stepHeight = 0 (generic Entity - items, xp orbs, the baked
 * mc_pcm scenarios and their Golden.java mirror). Bit-identical to the pre-step code. */
MC_HD static inline void mc_entity_move(McEntity *e, double x, double y, double z,
                                        const McAABB *blocks, int nblocks) {
    mc_entity_move_step(e, x, y, z, blocks, nblocks, 0.0f);
}

/* ---- baked scenarios (shared by cpu + cuda; Golden.java mirrors these exactly) ----
 * Entity is a player-sized box: half-extent 0.3 (double), height 1.8. Returns the block count;
 * fills *e (initial box from pos, initial motion = the move delta, onGround as noted) and the
 * move delta (*dx,*dy,*dz). Scenes use plain double literals so all three programs are identical. */
MC_HD static inline McAABB mc_pcm_player_box(double px, double py, double pz) {
    return mc_aabb_make(px - 0.3, py, pz - 0.3, px + 0.3, py + 1.8, pz + 0.3);
}

MC_HD static inline int mc_pcm_scenario(int idx, McEntity *e,
                                        double *dx, double *dy, double *dz,
                                        McAABB *blocks) {
    int n = 0;
    e->motionX = 0.0; e->motionY = 0.0; e->motionZ = 0.0;
    e->onGround = 0; e->collidedHorizontally = 0; e->collidedVertically = 0; e->isCollided = 0;

    if (idx == 0) {
        /* (a) free fall onto a floor at block-y=0 (top surface y=1). */
        e->posX = 0.0; e->posY = 5.0; e->posZ = 0.0;
        e->box = mc_pcm_player_box(e->posX, e->posY, e->posZ);
        *dx = 0.0; *dy = -10.0; *dz = 0.0;
        e->motionY = -10.0;
        for (int bx = -1; bx <= 1; ++bx)
            for (int bz = -1; bz <= 1; ++bz)
                blocks[n++] = mc_aabb_make(bx, 0.0, bz, bx + 1, 1.0, bz + 1);
    } else if (idx == 1) {
        /* (b) walk horizontally into a wall at x=2. */
        e->posX = 0.0; e->posY = 1.0; e->posZ = 0.0;
        e->box = mc_pcm_player_box(e->posX, e->posY, e->posZ);
        *dx = 5.0; *dy = 0.0; *dz = 0.0;
        e->motionX = 5.0;
        e->onGround = 1;
        for (int by = 1; by <= 2; ++by)
            for (int bz = -1; bz <= 0; ++bz)
                blocks[n++] = mc_aabb_make(2.0, by, bz, 3.0, by + 1, bz + 1);
    } else if (idx == 2) {
        /* (c) diagonal motion into a corner: wall at x=2 and wall at z=2. */
        e->posX = 0.0; e->posY = 1.0; e->posZ = 0.0;
        e->box = mc_pcm_player_box(e->posX, e->posY, e->posZ);
        *dx = 5.0; *dy = 0.0; *dz = 5.0;
        e->motionX = 5.0; e->motionZ = 5.0;
        e->onGround = 1;
        for (int by = 1; by <= 2; ++by)
            for (int bz = -1; bz <= 1; ++bz)
                blocks[n++] = mc_aabb_make(2.0, by, bz, 3.0, by + 1, bz + 1);
        for (int by = 1; by <= 2; ++by)
            for (int bx = -1; bx <= 1; ++bx)
                blocks[n++] = mc_aabb_make(bx, by, 2.0, bx + 1, by + 1, 3.0);
    } else {
        /* (d) motion with no obstacles. */
        e->posX = 0.0; e->posY = 5.0; e->posZ = 0.0;
        e->box = mc_pcm_player_box(e->posX, e->posY, e->posZ);
        *dx = 1.0; *dy = -1.0; *dz = 1.0;
        e->motionX = 1.0; e->motionY = -1.0; e->motionZ = 1.0;
    }
    return n;
}

#define MC_PCM_NUM_SCENARIOS 4

#endif /* MC_PHYSICS_COLLISION_MATH_H */
