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

/* ItemBlock uses the block's numeric id in 1.11.2. ItemBlockSpecial is the
 * finite exception table registered by Item.registerItems. */
MC_HD static inline int ibp_item_placed_block(int item_id) {
    switch (item_id) {
        case 287: return 132; /* string -> tripwire */
        case 338: return 83;  /* reeds */
        case 354: return 92;  /* cake */
        case 356: return 93;  /* unpowered repeater */
        case 379: return 117; /* brewing stand */
        case 380: return 118; /* cauldron */
        case 390: return 140; /* flower pot */
        case 404: return 149; /* unpowered comparator */
        case 324: return 64;  /* oak door */
        case 330: return 71;  /* iron door */
        case 427: return 193; /* spruce door */
        case 428: return 194; /* birch door */
        case 429: return 195; /* jungle door */
        case 430: return 196; /* acacia door */
        case 431: return 197; /* dark-oak door */
        case 295: return 59;  /* wheat seeds -> wheat crop */
        case 361: return 104; /* pumpkin seeds -> stem */
        case 362: return 105; /* melon seeds -> stem */
        case 372: return 115; /* nether wart */
        case 391: return 141; /* carrot */
        case 392: return 142; /* potato */
        case 435: return 207; /* beetroot seeds */
        case 290: case 291: case 292: case 293: case 294:
            return 60; /* hoes -> farmland */
        case 256: case 269: case 273: case 277: case 284:
            return 208; /* spades -> grass path */
        case 331: return 55; /* redstone dust -> wire */
        default: return item_id > 0 && item_id < 256 ? item_id : 0;
    }
}

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
    switch (block_id) {
        /* ItemMultiTexture / ItemColored / ItemCloth preserve damage. */
        case 1: case 2: case 3: case 5: case 6: case 12: case 17:
        case 19: case 24: case 31: case 35: case 37: case 38:
        case 95: case 97: case 98: case 106: case 139: case 155:
        case 159: case 160: case 162: case 168: case 171: case 175:
        case 179:
            return stack_meta & 15;
        /* ItemLeaves forces CHECK_DECAY while retaining the leaf variant. */
        case 18: case 161:
            return (stack_meta & 3) | 4;
        default:
            return 0;
    }
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
MC_HD static inline int ibp_placed_meta_exact(
        int block_id, int hit_face, int yaw_quad, int hit_y_high,
        int entity_dir, int stack_meta) {
    int meta = ibp_item_metadata(block_id, stack_meta) & 15;
    switch (block_id) {
        /* Every vanilla stair shares BlockStairs.getStateForPlacement. */
        case 53: case 67: case 108: case 109: case 114: case 128:
        case 134: case 135: case 136: case 156: case 163: case 164:
        case 180: case 203:
            return ibp_meta_stairs(hit_face, yaw_quad, hit_y_high);
        case 44: case 126: case 182: case 205: {
            int top = hit_face == IBP_DOWN
                || (hit_face != IBP_UP && hit_y_high);
            return (stack_meta & 7) | (top ? 8 : 0);
        }
        case 17: case 162: case 170: case 202: case 216: {
            int axis = ibp_axis(hit_face);
            return meta | (axis == 1 ? 4 : axis == 2 ? 8 : 0);
        }
        case 54: case 61: case 130: case 146:
            return ibp_meta_chest(yaw_quad);
        case 64: case 71: case 193: case 194: case 195: case 196:
        case 197:
            return (yaw_quad + 1) & 3;
        case 65:
            return ibp_meta_ladder(hit_face);
        case 23: case 29: case 33: case 137: case 158: case 210:
        case 211:
            return entity_dir;
        case 218:
            return ibp_opposite(entity_dir);
        case 86: case 91: case 93: case 120: case 149:
            return ibp_meta_pumpkin(yaw_quad);
        case 50: case 76:
            return ibp_meta_torch(hit_face);
        case 69: /* BlockLever.EnumOrientation.forFacings */
            if (hit_face == IBP_DOWN) return (yaw_quad & 1) ? 0 : 7;
            if (hit_face == IBP_UP) return (yaw_quad & 1) ? 6 : 5;
            return hit_face == IBP_NORTH ? 4
                : hit_face == IBP_SOUTH ? 3
                : hit_face == IBP_WEST ? 2 : 1;
        case 77: case 143: /* stone/wood button */
            return hit_face == IBP_DOWN ? 0 : hit_face == IBP_UP ? 5
                : hit_face == IBP_NORTH ? 4
                : hit_face == IBP_SOUTH ? 3
                : hit_face == IBP_WEST ? 2 : 1;
        case 96: case 167: /* trapdoors */
            if (hit_face <= IBP_UP) {
                int horizontal_meta = yaw_quad == 0 ? 0
                    : yaw_quad == 1 ? 3 : yaw_quad == 2 ? 1 : 2;
                return horizontal_meta | (hit_face == IBP_DOWN ? 8 : 0);
            }
            return (hit_face == IBP_NORTH ? 0 : hit_face == IBP_SOUTH ? 1
                : hit_face == IBP_WEST ? 2 : 3) | (hit_y_high ? 8 : 0);
        case 106: /* vine attachment bit */
            return hit_face == IBP_NORTH ? 1 : hit_face == IBP_SOUTH ? 4
                : hit_face == IBP_WEST ? 8 : hit_face == IBP_EAST ? 2 : 0;
        case 107: case 183: case 184: case 185: case 186: case 187:
            return yaw_quad & 3;
        case 131: /* tripwire hook */
            return hit_face == IBP_SOUTH ? 0 : hit_face == IBP_WEST ? 1
                : hit_face == IBP_EAST ? 3 : 2;
        case 145: /* anvil damage tier plus horizontal facing */
            return ((stack_meta & 3) << 2) | ((yaw_quad + 1) & 3);
        case 154: {
            int facing = ibp_opposite(hit_face);
            return facing == IBP_UP ? IBP_DOWN : facing;
        }
        case 155: /* quartz pillar only for chiseled/pillar damage 2 */
            if ((stack_meta & 15) == 2)
                return hit_face <= IBP_UP ? 2
                    : hit_face <= IBP_SOUTH ? 4 : 3;
            return meta;
        case 198: case 219: case 220: case 221: case 222: case 223:
        case 224: case 225: case 226: case 227: case 228: case 229:
        case 230: case 231: case 232: case 233: case 234:
            return hit_face & 7;
        case 99: case 100:
            return 14;
        case 255:
            return 3;
        default:
            return meta;
    }
}

/* Compatibility surface for the original pure battery. New gameplay callers
 * should pass measured hit height and EnumFacing.getDirectionFromEntityLiving
 * to ibp_placed_meta_exact instead of using this synthetic geometry. */
MC_HD static inline int ibp_placed_meta(int block_id, int hit_face, int yaw_quad,
                                        int hit_y_high, int stack_meta) {
    int entity_dir = ibp_dir_from_entity(yaw_quad, hit_face, hit_y_high);
    return ibp_placed_meta_exact(block_id, hit_face, yaw_quad, hit_y_high,
                                 entity_dir, stack_meta);
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
