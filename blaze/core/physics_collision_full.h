/* physics_collision_full: Entity.move collision extensions deferred from physics_collision_math v1.
 *
 * PORT TARGET: net/minecraft/entity/Entity.java move(MoverType,double,double,double) branches for
 * stepHeight auto-step, EntityPlayer sneak step-back, isInWeb slowdown, MoverType.PISTON axis clamp,
 * and baked block subsets for web (NULL_AABB), ladder (thin AABB), liquid (pass-through +
 * containsAnyLiquid). Reuses AABB math from physics_collision_math.h verbatim.
 *
 * World query is a FIXED baked block list (same pattern as physics_collision_math): collision boxes
 * from block_props_table.h flags + web/ladder shape overrides. READ-ONLY deps:
 * physics_collision_math.h, block_props_table.h.
 *
 * TRIMMED (output-invariant for baked scenes): noClip, entity-entity collision, Forge events,
 * updateFallState, fence/wall feet lookup, walking/swim sounds, doBlockCollisions / setInWeb on
 * entry, slime/bed onLanded bounce. motionY zeroing on vertical hit uses Block.onLanded default
 * (motionY=0) for all landed block types in scope.
 *
 * Verify: 3-way Java golden == CPU == CUDA on PCF_NUM_SCENARIOS baked cases. */
#ifndef MC_PHYSICS_COLLISION_FULL_H
#define MC_PHYSICS_COLLISION_FULL_H

#include "physics_collision_math.h"
#include "block_props_table.h"

#define PCF_MAX_BLOCKS 64

typedef enum {
    PCF_MOVER_SELF   = 0,
    PCF_MOVER_PLAYER = 1,
    PCF_MOVER_PISTON = 2,
} PcfMoverType;

typedef struct {
    int block_id;
    double ox, oy, oz;
    int ladder_facing; /* 2=NORTH 3=SOUTH 4=WEST 5=EAST (EnumFacing index) */
} PcfBlock;

typedef struct {
    McAABB box;
    double posX, posY, posZ;
    double motionX, motionY, motionZ;
    float stepHeight;
    int onGround;
    int isInWeb;
    int isPlayer;
    int isSneaking;
    int collidedHorizontally;
    int collidedVertically;
    int isCollided;
    PcfMoverType moverType;
    i64 worldTime;
    double piston_axis[3];
    i64 piston_axis_tick;
} McPcfEntity;

/* MathHelper.clamp(double) */
MC_HD static inline double pcf_clampd(double num, double min, double max) {
    return num < min ? min : (num > max ? max : num);
}

MC_HD static inline McAABB pcf_ladder_aabb(double ox, double oy, double oz, int facing) {
    if (facing == 2) /* NORTH */
        return mc_aabb_make(ox, oy, oz + 0.8125, ox + 1.0, oy + 1.0, oz + 1.0);
    if (facing == 3) /* SOUTH */
        return mc_aabb_make(ox, oy, oz, ox + 1.0, oy + 1.0, oz + 0.1875);
    if (facing == 4) /* WEST */
        return mc_aabb_make(ox + 0.8125, oy, oz, ox + 1.0, oy + 1.0, oz + 1.0);
    /* EAST */
    return mc_aabb_make(ox, oy, oz, ox + 0.1875, oy + 1.0, oz + 1.0);
}

MC_HD static inline int pcf_block_collision_aabb(const PcfBlock *b, McAABB *out) {
    if (b->block_id == 0) return 0;
    if (b->block_id == 30) return 0; /* BlockWeb NULL_AABB */
    BptProps props = mc_bpt_props(b->block_id);
    if (props.flags & BF_LIQUID) return 0;
    if (b->block_id == 65) {
        *out = pcf_ladder_aabb(b->ox, b->oy, b->oz, b->ladder_facing);
        return 1;
    }
    if (props.flags & BF_SOLID) {
        *out = mc_aabb_make(b->ox, b->oy, b->oz, b->ox + 1.0, b->oy + 1.0, b->oz + 1.0);
        return 1;
    }
    return 0;
}

MC_HD static inline int pcf_block_is_liquid(const PcfBlock *b) {
    if (b->block_id == 0) return 0;
    return (mc_bpt_props(b->block_id).flags & BF_LIQUID) ? 1 : 0;
}

MC_HD static inline int pcf_get_collision_boxes(const McAABB *query,
                                                const PcfBlock *blocks, int nblocks,
                                                McAABB *out, int max_out) {
    int n = 0;
    for (int i = 0; i < nblocks; ++i) {
        McAABB bb;
        if (!pcf_block_collision_aabb(&blocks[i], &bb)) continue;
        if (mc_aabb_intersects(query, &bb)) {
            if (n >= max_out) break;
            out[n++] = bb;
        }
    }
    return n;
}

MC_HD static inline int pcf_collision_boxes_empty(const McAABB *query,
                                                  const PcfBlock *blocks, int nblocks) {
    McAABB tmp[PCF_MAX_BLOCKS];
    return pcf_get_collision_boxes(query, blocks, nblocks, tmp, PCF_MAX_BLOCKS) == 0;
}

MC_HD static inline int pcf_contains_any_liquid(const McAABB *bb,
                                                const PcfBlock *blocks, int nblocks) {
    for (int i = 0; i < nblocks; ++i) {
        if (!pcf_block_is_liquid(&blocks[i])) continue;
        McAABB lb = mc_aabb_make(blocks[i].ox, blocks[i].oy, blocks[i].oz,
                                 blocks[i].ox + 1.0, blocks[i].oy + 1.0, blocks[i].oz + 1.0);
        if (mc_aabb_intersects(bb, &lb)) return 1;
    }
    return 0;
}

/* Verbatim Entity.move(MoverType, x, y, z) collision slice (extensions enabled; trims above). */
MC_HD static inline void pcf_entity_move(McPcfEntity *e, double x, double y, double z,
                                         const PcfBlock *blocks, int nblocks) {
    double d2 = x;
    double d3 = y;
    double d4 = z;

    if (e->moverType == PCF_MOVER_PISTON) {
        i64 i = e->worldTime;
        if (i != e->piston_axis_tick) {
            e->piston_axis[0] = 0.0;
            e->piston_axis[1] = 0.0;
            e->piston_axis[2] = 0.0;
            e->piston_axis_tick = i;
        }
        if (x != 0.0) {
            int j = 0;
            double d0 = pcf_clampd(x + e->piston_axis[j], -0.51, 0.51);
            x = d0 - e->piston_axis[j];
            e->piston_axis[j] = d0;
            if (x > -9.999999747378752E-6 && x < 9.999999747378752E-6) return;
        } else if (y != 0.0) {
            int l4 = 1;
            double d12 = pcf_clampd(y + e->piston_axis[l4], -0.51, 0.51);
            y = d12 - e->piston_axis[l4];
            e->piston_axis[l4] = d12;
            if (y > -9.999999747378752E-6 && y < 9.999999747378752E-6) return;
        } else {
            if (z == 0.0) return;
            int i5 = 2;
            double d13 = pcf_clampd(z + e->piston_axis[i5], -0.51, 0.51);
            z = d13 - e->piston_axis[i5];
            e->piston_axis[i5] = d13;
            if (z > -9.999999747378752E-6 && z < 9.999999747378752E-6) return;
        }
    }

    if (e->isInWeb) {
        e->isInWeb = 0;
        x *= 0.25;
        y *= 0.05000000074505806;
        z *= 0.25;
        e->motionX = 0.0;
        e->motionY = 0.0;
        e->motionZ = 0.0;
    }

    d2 = x;
    d3 = y;
    d4 = z;

    if ((e->moverType == PCF_MOVER_SELF || e->moverType == PCF_MOVER_PLAYER) &&
        e->onGround && e->isSneaking && e->isPlayer) {
        McAABB sneak_q;
        for (; x != 0.0; d2 = x) {
            sneak_q = mc_aabb_offset(&e->box, x, (double)(-e->stepHeight), 0.0);
            if (!pcf_collision_boxes_empty(&sneak_q, blocks, nblocks)) break;
            if (x < 0.05 && x >= -0.05) x = 0.0;
            else if (x > 0.0) x -= 0.05;
            else x += 0.05;
        }
        for (; z != 0.0; d4 = z) {
            sneak_q = mc_aabb_offset(&e->box, 0.0, (double)(-e->stepHeight), z);
            if (!pcf_collision_boxes_empty(&sneak_q, blocks, nblocks)) break;
            if (z < 0.05 && z >= -0.05) z = 0.0;
            else if (z > 0.0) z -= 0.05;
            else z += 0.05;
        }
        for (; x != 0.0 && z != 0.0; d4 = z) {
            sneak_q = mc_aabb_offset(&e->box, x, (double)(-e->stepHeight), z);
            if (!pcf_collision_boxes_empty(&sneak_q, blocks, nblocks)) break;
            if (x < 0.05 && x >= -0.05) x = 0.0;
            else if (x > 0.0) x -= 0.05;
            else x += 0.05;
            d2 = x;
            if (z < 0.05 && z >= -0.05) z = 0.0;
            else if (z > 0.0) z -= 0.05;
            else z += 0.05;
        }
    }

    McAABB list1[PCF_MAX_BLOCKS];
    McAABB query_box = mc_aabb_addcoord(&e->box, x, y, z);
    int n1 = pcf_get_collision_boxes(&query_box, blocks, nblocks, list1, PCF_MAX_BLOCKS);
    McAABB axisalignedbb = e->box;

    if (y != 0.0) {
        for (int k = 0; k < n1; ++k)
            y = mc_aabb_calcYOffset(&list1[k], &e->box, y);
        e->box = mc_aabb_offset(&e->box, 0.0, y, 0.0);
    }

    if (x != 0.0) {
        for (int j5 = 0; j5 < n1; ++j5)
            x = mc_aabb_calcXOffset(&list1[j5], &e->box, x);
        if (x != 0.0)
            e->box = mc_aabb_offset(&e->box, x, 0.0, 0.0);
    }

    if (z != 0.0) {
        for (int k5 = 0; k5 < n1; ++k5)
            z = mc_aabb_calcZOffset(&list1[k5], &e->box, z);
        if (z != 0.0)
            e->box = mc_aabb_offset(&e->box, 0.0, 0.0, z);
    }

    int flag = e->onGround || (d3 != y && d3 < 0.0);

    if (e->stepHeight > 0.0f && flag && (d2 != x || d4 != z)) {
        double d14 = x;
        double d6 = y;
        double d7 = z;
        McAABB axisalignedbb1 = e->box;
        e->box = axisalignedbb;
        y = (double)e->stepHeight;
        McAABB list[PCF_MAX_BLOCKS];
        McAABB step_query = mc_aabb_addcoord(&e->box, d2, y, d4);
        int nlist = pcf_get_collision_boxes(&step_query, blocks, nblocks, list, PCF_MAX_BLOCKS);
        McAABB axisalignedbb2 = e->box;
        McAABB axisalignedbb3 = mc_aabb_addcoord(&axisalignedbb2, d2, 0.0, d4);
        double d8 = y;
        for (int j1 = 0; j1 < nlist; ++j1)
            d8 = mc_aabb_calcYOffset(&list[j1], &axisalignedbb3, d8);
        axisalignedbb2 = mc_aabb_offset(&axisalignedbb2, 0.0, d8, 0.0);
        double d18 = d2;
        for (int l1 = 0; l1 < nlist; ++l1)
            d18 = mc_aabb_calcXOffset(&list[l1], &axisalignedbb2, d18);
        axisalignedbb2 = mc_aabb_offset(&axisalignedbb2, d18, 0.0, 0.0);
        double d19 = d4;
        for (int j2 = 0; j2 < nlist; ++j2)
            d19 = mc_aabb_calcZOffset(&list[j2], &axisalignedbb2, d19);
        axisalignedbb2 = mc_aabb_offset(&axisalignedbb2, 0.0, 0.0, d19);
        McAABB axisalignedbb4 = e->box;
        double d20 = y;
        for (int l2 = 0; l2 < nlist; ++l2)
            d20 = mc_aabb_calcYOffset(&list[l2], &axisalignedbb4, d20);
        axisalignedbb4 = mc_aabb_offset(&axisalignedbb4, 0.0, d20, 0.0);
        double d21 = d2;
        for (int j3 = 0; j3 < nlist; ++j3)
            d21 = mc_aabb_calcXOffset(&list[j3], &axisalignedbb4, d21);
        axisalignedbb4 = mc_aabb_offset(&axisalignedbb4, d21, 0.0, 0.0);
        double d22 = d4;
        for (int l3 = 0; l3 < nlist; ++l3)
            d22 = mc_aabb_calcZOffset(&list[l3], &axisalignedbb4, d22);
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
        for (int j4 = 0; j4 < nlist; ++j4)
            y = mc_aabb_calcYOffset(&list[j4], &e->box, y);
        e->box = mc_aabb_offset(&e->box, 0.0, y, 0.0);
        if (d14 * d14 + d7 * d7 >= x * x + z * z) {
            x = d14;
            y = d6;
            z = d7;
            e->box = axisalignedbb1;
        }
    }

    e->posX = (e->box.minX + e->box.maxX) / 2.0;
    e->posY = e->box.minY;
    e->posZ = (e->box.minZ + e->box.maxZ) / 2.0;

    e->collidedHorizontally = (d2 != x || d4 != z) ? 1 : 0;
    e->collidedVertically = (d3 != y) ? 1 : 0;
    e->onGround = (e->collidedVertically && d3 < 0.0) ? 1 : 0;
    e->isCollided = (e->collidedHorizontally || e->collidedVertically) ? 1 : 0;

    if (d2 != x) e->motionX = 0.0;
    if (d4 != z) e->motionZ = 0.0;
    if (d3 != y) e->motionY = 0.0;
}

MC_HD static inline void pcf_init_entity(McPcfEntity *e) {
    e->motionX = e->motionY = e->motionZ = 0.0;
    e->onGround = 0;
    e->isInWeb = 0;
    e->isPlayer = 0;
    e->isSneaking = 0;
    e->collidedHorizontally = 0;
    e->collidedVertically = 0;
    e->isCollided = 0;
    e->moverType = PCF_MOVER_SELF;
    e->worldTime = 0;
    e->piston_axis[0] = e->piston_axis[1] = e->piston_axis[2] = 0.0;
    e->piston_axis_tick = -1;
    e->stepHeight = 0.0f;
}

MC_HD static inline int pcf_scenario(int idx, McPcfEntity *e,
                                     double *dx, double *dy, double *dz,
                                     PcfBlock *blocks) {
    int n = 0;
    pcf_init_entity(e);

    if (idx == 0) {
        /* stepHeight: 1-block step with stepHeight=1.0 (golem-scale; same branch as player 0.6F). */
        e->posX = 0.5; e->posY = 1.0; e->posZ = 0.5;
        e->box = mc_pcm_player_box(e->posX, e->posY, e->posZ);
        e->stepHeight = 1.0f;
        e->onGround = 1;
        *dx = 0.8; *dy = 0.0; *dz = 0.0;
        e->motionX = 0.8;
        blocks[n++] = (PcfBlock){ 1, 0.0, 0.0, 0.0, 0 };
        blocks[n++] = (PcfBlock){ 1, 1.0, 1.0, 0.0, 0 };
    } else if (idx == 1) {
        /* sneak step-back: player sneaking toward platform edge. */
        e->posX = 1.7; e->posY = 1.0; e->posZ = 0.5;
        e->box = mc_pcm_player_box(e->posX, e->posY, e->posZ);
        e->stepHeight = 0.6f;
        e->onGround = 1;
        e->isPlayer = 1;
        e->isSneaking = 1;
        e->moverType = PCF_MOVER_PLAYER;
        *dx = 0.5; *dy = 0.0; *dz = 0.0;
        e->motionX = 0.5;
        blocks[n++] = (PcfBlock){ 1, 0.0, 0.0, 0.0, 0 };
        blocks[n++] = (PcfBlock){ 1, 1.0, 0.0, 0.0, 0 };
    } else if (idx == 2) {
        /* web: isInWeb slowdown at move start. */
        e->posX = 0.0; e->posY = 1.0; e->posZ = 0.0;
        e->box = mc_pcm_player_box(e->posX, e->posY, e->posZ);
        e->isInWeb = 1;
        *dx = 1.0; *dy = 0.0; *dz = 0.0;
        e->motionX = 1.0;
        blocks[n++] = (PcfBlock){ 30, 0.0, 0.0, 0.0, 0 };
        blocks[n++] = (PcfBlock){ 1, -1.0, 0.0, -1.0, 0 };
    } else if (idx == 3) {
        /* ladder: thin collision box (NORTH-facing at x=1). */
        e->posX = 0.0; e->posY = 1.0; e->posZ = 0.5;
        e->box = mc_pcm_player_box(e->posX, e->posY, e->posZ);
        e->onGround = 1;
        *dx = 1.5; *dy = 0.0; *dz = 0.0;
        e->motionX = 1.5;
        blocks[n++] = (PcfBlock){ 1, 0.0, 0.0, 0.0, 0 };
        blocks[n++] = (PcfBlock){ 65, 1.0, 1.0, 0.0, 2 };
    } else if (idx == 4) {
        /* liquid: pass-through (no collision box); fall onto floor below. */
        e->posX = 0.0; e->posY = 3.0; e->posZ = 0.0;
        e->box = mc_pcm_player_box(e->posX, e->posY, e->posZ);
        *dx = 0.0; *dy = -2.0; *dz = 0.0;
        e->motionY = -2.0;
        blocks[n++] = (PcfBlock){ 8, 0.0, 1.0, 0.0, 0 };
        blocks[n++] = (PcfBlock){ 1, 0.0, 0.0, 0.0, 0 };
    } else if (idx == 5) {
        /* piston: MoverType.PISTON axis clamp; two X segments same world tick. */
        e->posX = 0.0; e->posY = 1.0; e->posZ = 0.0;
        e->box = mc_pcm_player_box(e->posX, e->posY, e->posZ);
        e->moverType = PCF_MOVER_PISTON;
        e->worldTime = 100;
        *dx = 0.4; *dy = 0.0; *dz = 0.0;
        blocks[n++] = (PcfBlock){ 1, -1.0, 0.0, -1.0, 0 };
    } else {
        return 0;
    }
    return n;
}

#define PCF_NUM_SCENARIOS 6

MC_HD static inline void pcf_run_scenario(int idx, McPcfEntity *e) {
    double dx, dy, dz;
    PcfBlock blocks[PCF_MAX_BLOCKS];
    int n = pcf_scenario(idx, e, &dx, &dy, &dz, blocks);
    if (idx == 5) {
        pcf_entity_move(e, dx, dy, dz, blocks, n);
        pcf_entity_move(e, dx, dy, dz, blocks, n);
    } else {
        pcf_entity_move(e, dx, dy, dz, blocks, n);
    }
}

#endif /* MC_PHYSICS_COLLISION_FULL_H */
