/* item_block_place: ItemBlock placement orientation metadata (MC 1.11.2).
 *
 * Pure table: (blockId, hitFace, playerYawQuadrant, sneaked [, stackMeta]) -> placed meta.
 * Ports the orientation half of ItemBlock.onItemUse:
 *   Item.getMetadata(stackMeta) -> Block.getStateForPlacement(...) -> getMetaFromState.
 * (No world mayPlace / replaceable / TE / sounds.)
 *
 * Sources (decompiled 1.11.2):
 *   item/ItemBlock.java onItemUse
 *   item/Item.java getMetadata(int)  (default 0)
 *   item/ItemMultiTexture.java getMetadata  (returns damage; logs)
 *   block/BlockStairs, BlockLog/BlockOldLog, BlockFurnace, BlockChest, BlockLadder,
 *   BlockPistonBase, BlockObserver, BlockDispenser, BlockPumpkin, BlockTorch
 *   util/EnumFacing getHorizontal / getDirectionFromEntityLiving
 *   entity/Entity.getHorizontalFacing
 *
 * Inputs:
 *   hitFace   - EnumFacing index D-U-N-S-W-E (0-5); face the player clicked
 *   yawQuad   - player horizontal facing index S-W-N-E (0-3), i.e.
 *               floor(yaw * 4/360 + 0.5) & 3  (Entity.getHorizontalFacing)
 *   sneaked   - 0/1; for stairs: proxies hitY > 0.5 on horizontal faces.
 *               for piston/dispenser/observer: proxies getDirectionFromEntityLiving
 *               vertical (see ibp_dir_from_entity).
 *   stackMeta - ItemStack damage; only logs/multi-texture keep bits via getMetadata.
 *
 * CUT: world collision / canBlockStay / double-chest pairing / powered|triggered|extended
 * bits (always 0 on place) / snow replaceable / Forge hand overload.
 * CPU==CUDA. java golden matches this pure table. */
#ifndef MC_ITEM_BLOCK_PLACE_H
#define MC_ITEM_BLOCK_PLACE_H

#include "mc.h"

/* EnumFacing indices (VALUES order). */
enum {
    IBP_DOWN = 0,
    IBP_UP = 1,
    IBP_NORTH = 2,
    IBP_SOUTH = 3,
    IBP_WEST = 4,
    IBP_EAST = 5
};

/* Vanilla block ids exercised by the battery. */
enum {
    IBP_BLK_LOG       = 17,
    IBP_BLK_DISPENSER = 23,
    IBP_BLK_PISTON    = 33,
    IBP_BLK_TORCH     = 50,
    IBP_BLK_OAK_STAIRS = 53,
    IBP_BLK_CHEST     = 54,
    IBP_BLK_FURNACE   = 61,
    IBP_BLK_LADDER    = 65,
    IBP_BLK_PUMPKIN   = 86,
    IBP_BLK_OBSERVER  = 218
};

#define IBP_NUM_KINDS 10
#define IBP_NUM_FACES 6
#define IBP_NUM_YAWS  4
#define IBP_NUM_SNEAK 2
/* Full cross product: kind x yaw x face x sneak. */
#define IBP_NUM_CASES (IBP_NUM_KINDS * IBP_NUM_YAWS * IBP_NUM_FACES * IBP_NUM_SNEAK)

/* HORIZONTALS[hi] = full EnumFacing index: S W N E. */
MC_HD static inline int ibp_horiz_to_face(int hi) {
    static const int k[] = {IBP_SOUTH, IBP_WEST, IBP_NORTH, IBP_EAST};
    return k[hi & 3];
}

/* Opposite EnumFacing (index). */
MC_HD static inline int ibp_opposite(int face) {
    static const int k[] = {IBP_UP, IBP_DOWN, IBP_SOUTH, IBP_NORTH, IBP_EAST, IBP_WEST};
    return k[face & 7];
}

/* Axis of a face: 0=Y, 1=X, 2=Z. */
MC_HD static inline int ibp_axis(int face) {
    face &= 7;
    if (face <= IBP_UP) return 0;
    if (face <= IBP_SOUTH) return 2;
    return 1;
}

MC_HD static inline int ibp_is_horizontal(int face) {
    face &= 7;
    return face >= IBP_NORTH && face <= IBP_EAST;
}

/* Entity.getHorizontalFacing: already reduced to quadrant. */
MC_HD static inline int ibp_horizontal_facing(int yaw_quad) {
    return ibp_horiz_to_face(yaw_quad & 3);
}

/* EnumFacing.getHorizontalIndex for a full face (S=0 W=1 N=2 E=3). Non-horiz -> 0. */
MC_HD static inline int ibp_horizontal_index(int face) {
    switch (face & 7) {
        case IBP_SOUTH: return 0;
        case IBP_WEST:  return 1;
        case IBP_NORTH: return 2;
        case IBP_EAST:  return 3;
        default:        return 0;
    }
}

/* Item.getMetadata: default ItemBlock -> 0; multi-texture (logs) pass damage. */
MC_HD static inline int ibp_item_metadata(int block_id, int stack_meta) {
    if (block_id == IBP_BLK_LOG) return stack_meta & 15;
    return 0;
}

/* Simplified EnumFacing.getDirectionFromEntityLiving without world coords:
 *   sneaked=0 -> horizontalFacing.opposite (player beside block)
 *   sneaked=1, hitFace==DOWN -> DOWN (player under block)
 *   sneaked=1, else          -> UP   (player above block)
 * Covers all 6 facings across the battery while staying table-pure. */
MC_HD static inline int ibp_dir_from_entity(int yaw_quad, int hit_face, int sneaked) {
    if (sneaked) {
        if ((hit_face & 7) == IBP_DOWN) return IBP_DOWN;
        return IBP_UP;
    }
    return ibp_opposite(ibp_horizontal_facing(yaw_quad));
}

/* ---- per-block getStateForPlacement meta (powered/extended/triggered = 0) ---- */

/* BlockStairs: FACING=horizontalFacing, HALF from hitFace/hitY(sneaked).
 * meta = (TOP?4:0) | (5 - facingIndex). */
MC_HD static inline int ibp_meta_stairs(int hit_face, int yaw_quad, int sneaked) {
    int facing = ibp_horizontal_facing(yaw_quad);
    int half_top;
    hit_face &= 7;
    /* facing != DOWN always true for stairs facing (horizontal).
     * half = BOTTOM if hitFace!=DOWN && (hitFace==UP || hitY<=0.5) else TOP.
     * hitY proxy: sneaked==1 means hitY > 0.5 on horizontal clicks. */
    if (hit_face == IBP_DOWN)
        half_top = 1;
    else if (hit_face == IBP_UP)
        half_top = 0;
    else
        half_top = sneaked ? 1 : 0;
    return (half_top ? 4 : 0) | (5 - facing);
}

/* BlockOldLog oak (variant 0) / multi-texture: axis from hit face, low 2 bits = variant.
 * Y=0, X=4, Z=8, NONE=12 (not produced by placement). */
MC_HD static inline int ibp_meta_log(int hit_face, int stack_meta) {
    int meta = ibp_item_metadata(IBP_BLK_LOG, stack_meta) & 3;
    int ax = ibp_axis(hit_face);
    if (ax == 1) meta |= 4;
    else if (ax == 2) meta |= 8;
    return meta;
}

/* BlockFurnace: FACING = horizontalFacing.opposite; meta = face index. */
MC_HD static inline int ibp_meta_furnace(int yaw_quad) {
    return ibp_opposite(ibp_horizontal_facing(yaw_quad));
}

/* BlockChest: getStateForPlacement uses horizontalFacing, but onBlockPlacedBy
 * overwrites with horizontalFacing.opposite (final world meta). We emit FINAL. */
MC_HD static inline int ibp_meta_chest(int yaw_quad) {
    return ibp_opposite(ibp_horizontal_facing(yaw_quad));
}

/* BlockLadder: if hit face horizontal use it, else default NORTH. meta = face index.
 * (CUT canBlockStay / neighbor solid checks.) */
MC_HD static inline int ibp_meta_ladder(int hit_face) {
    if (ibp_is_horizontal(hit_face)) return hit_face & 7;
    return IBP_NORTH;
}

/* BlockPistonBase: FACING = dirFromEntity; EXTENDED=false. meta = face index. */
MC_HD static inline int ibp_meta_piston(int yaw_quad, int hit_face, int sneaked) {
    return ibp_dir_from_entity(yaw_quad, hit_face, sneaked);
}

/* BlockObserver: FACING = dirFromEntity.opposite; POWERED=false. */
MC_HD static inline int ibp_meta_observer(int yaw_quad, int hit_face, int sneaked) {
    return ibp_opposite(ibp_dir_from_entity(yaw_quad, hit_face, sneaked));
}

/* BlockDispenser: FACING = dirFromEntity; TRIGGERED=false. */
MC_HD static inline int ibp_meta_dispenser(int yaw_quad, int hit_face, int sneaked) {
    return ibp_dir_from_entity(yaw_quad, hit_face, sneaked);
}

/* BlockPumpkin: FACING = horizontalFacing.opposite; meta = horizontalIndex. */
MC_HD static inline int ibp_meta_pumpkin(int yaw_quad) {
    int face = ibp_opposite(ibp_horizontal_facing(yaw_quad));
    return ibp_horizontal_index(face);
}

/* BlockTorch: if placeable on hitFace use it; else default UP.
 * meta: E=1 W=2 S=3 N=4 U/D=5. (CUT canPlaceAt solid checks - assume valid.) */
MC_HD static inline int ibp_meta_torch(int hit_face) {
    switch (hit_face & 7) {
        case IBP_EAST:  return 1;
        case IBP_WEST:  return 2;
        case IBP_SOUTH: return 3;
        case IBP_NORTH: return 4;
        case IBP_UP:    return 5;
        case IBP_DOWN:  /* cannot attach to ceiling in vanilla simple path -> default UP */
        default:        return 5;
    }
}

/* Kind table order (stable for battery index). */
MC_HD static inline int ibp_kind_block_id(int kind) {
    static const int k[IBP_NUM_KINDS] = {
        IBP_BLK_OAK_STAIRS, IBP_BLK_LOG, IBP_BLK_FURNACE, IBP_BLK_CHEST,
        IBP_BLK_LADDER, IBP_BLK_PISTON, IBP_BLK_OBSERVER, IBP_BLK_DISPENSER,
        IBP_BLK_PUMPKIN, IBP_BLK_TORCH
    };
    return k[kind % IBP_NUM_KINDS];
}

/* Core: placed meta for one (blockId, hitFace, yawQuad, sneaked, stackMeta). */
MC_HD static inline int ibp_placed_meta(int block_id, int hit_face, int yaw_quad,
                                        int sneaked, int stack_meta) {
    switch (block_id) {
        case IBP_BLK_OAK_STAIRS:
            return ibp_meta_stairs(hit_face, yaw_quad, sneaked);
        case IBP_BLK_LOG:
            return ibp_meta_log(hit_face, stack_meta);
        case IBP_BLK_FURNACE:
            return ibp_meta_furnace(yaw_quad);
        case IBP_BLK_CHEST:
            return ibp_meta_chest(yaw_quad);
        case IBP_BLK_LADDER:
            return ibp_meta_ladder(hit_face);
        case IBP_BLK_PISTON:
            return ibp_meta_piston(yaw_quad, hit_face, sneaked);
        case IBP_BLK_OBSERVER:
            return ibp_meta_observer(yaw_quad, hit_face, sneaked);
        case IBP_BLK_DISPENSER:
            return ibp_meta_dispenser(yaw_quad, hit_face, sneaked);
        case IBP_BLK_PUMPKIN:
            return ibp_meta_pumpkin(yaw_quad);
        case IBP_BLK_TORCH:
            return ibp_meta_torch(hit_face);
        default:
            /* Item.getMetadata default + Block.getStateFromMeta identity for plain blocks. */
            return ibp_item_metadata(block_id, stack_meta) & 15;
    }
}

/* Decode battery case index -> inputs. stack_meta fixed 0 (oak / no subtype). */
MC_HD static inline void ibp_case(int idx, int *block_id, int *hit_face, int *yaw_quad,
                                  int *sneaked) {
    int sneak = idx % IBP_NUM_SNEAK;
    int t = idx / IBP_NUM_SNEAK;
    int face = t % IBP_NUM_FACES;
    t /= IBP_NUM_FACES;
    int yaw = t % IBP_NUM_YAWS;
    int kind = t / IBP_NUM_YAWS;
    *block_id = ibp_kind_block_id(kind);
    *hit_face = face;
    *yaw_quad = yaw;
    *sneaked = sneak;
}

/* One output word: (blockId << 16) | (hitFace << 12) | (yawQuad << 8) | (sneaked << 4) | meta
 * so the dump is self-describing and any mismatch is easy to read. */
MC_HD static inline u32 ibp_case_word(int idx) {
    int block_id, hit_face, yaw_quad, sneaked, meta;
    ibp_case(idx, &block_id, &hit_face, &yaw_quad, &sneaked);
    meta = ibp_placed_meta(block_id, hit_face, yaw_quad, sneaked, 0) & 15;
    return ((u32)block_id << 16) | ((u32)hit_face << 12) | ((u32)yaw_quad << 8)
         | ((u32)sneaked << 4) | (u32)meta;
}

MC_HD static inline void ibp_run(u32 *out) {
    int i;
    for (i = 0; i < IBP_NUM_CASES; ++i)
        out[i] = ibp_case_word(i);
}

#endif /* MC_ITEM_BLOCK_PLACE_H */
