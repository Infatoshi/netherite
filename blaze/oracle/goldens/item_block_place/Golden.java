// Verbatim MC 1.11.2 ItemBlock placement orientation metadata (vanilla ground truth).
// Sources (logic inlined; no game launch):
//   net/minecraft/item/ItemBlock.java onItemUse (meta path only)
//   net/minecraft/item/Item.java getMetadata(int) default 0
//   net/minecraft/item/ItemMultiTexture.java getMetadata returns damage (logs)
//   net/minecraft/block/BlockStairs|BlockOldLog|BlockFurnace|BlockChest|BlockLadder
//     |BlockPistonBase|BlockObserver|BlockDispenser|BlockPumpkin|BlockTorch
//     getStateForPlacement + getMetaFromState
//   net/minecraft/util/EnumFacing getHorizontal / getDirectionFromEntityLiving (folded)
//   net/minecraft/entity/Entity.getHorizontalFacing
//
// Table inputs match core/item_block_place.h: (blockId, hitFace, yawQuad, sneaked) -> meta.
// getDirectionFromEntityLiving without coords: sneaked=0 horizontalFacing.opposite;
// sneaked=1+hitFace==DOWN -> DOWN; sneaked=1 else -> UP.
// Chest emits FINAL meta after onBlockPlacedBy (opposite of look).
// Output: packed u32 %08x  (blockId<<16)|(hitFace<<12)|(yawQuad<<8)|(sneaked<<4)|meta
public class Golden {
    static final int DOWN = 0, UP = 1, NORTH = 2, SOUTH = 3, WEST = 4, EAST = 5;
    static final int LOG = 17, DISPENSER = 23, PISTON = 33, TORCH = 50;
    static final int OAK_STAIRS = 53, CHEST = 54, FURNACE = 61, LADDER = 65;
    static final int PUMPKIN = 86, OBSERVER = 218;
    static final int NUM_KINDS = 10, NUM_FACES = 6, NUM_YAWS = 4, NUM_SNEAK = 2;
    static final int NUM_CASES = NUM_KINDS * NUM_YAWS * NUM_FACES * NUM_SNEAK;
    static final int[] KINDS = {
        OAK_STAIRS, LOG, FURNACE, CHEST, LADDER, PISTON, OBSERVER, DISPENSER, PUMPKIN, TORCH
    };

    // HORIZONTALS: S W N E
    static int horizToFace(int hi) {
        int[] k = {SOUTH, WEST, NORTH, EAST};
        return k[hi & 3];
    }
    static int opposite(int face) {
        int[] k = {UP, DOWN, SOUTH, NORTH, EAST, WEST};
        return k[face & 7];
    }
    static int axis(int face) {
        face &= 7;
        if (face <= UP) return 0;
        if (face <= SOUTH) return 2;
        return 1;
    }
    static boolean isHorizontal(int face) {
        face &= 7;
        return face >= NORTH && face <= EAST;
    }
    static int horizontalFacing(int yawQuad) { return horizToFace(yawQuad & 3); }
    static int horizontalIndex(int face) {
        switch (face & 7) {
            case SOUTH: return 0;
            case WEST:  return 1;
            case NORTH: return 2;
            case EAST:  return 3;
            default:    return 0;
        }
    }

    // Item.getMetadata: multi-texture log passes damage; default ItemBlock -> 0.
    static int itemMetadata(int blockId, int stackMeta) {
        if (blockId == LOG) return stackMeta & 15;
        return 0;
    }

    // Folded EnumFacing.getDirectionFromEntityLiving (see header).
    static int dirFromEntity(int yawQuad, int hitFace, int sneaked) {
        if (sneaked != 0) {
            if ((hitFace & 7) == DOWN) return DOWN;
            return UP;
        }
        return opposite(horizontalFacing(yawQuad));
    }

    // BlockStairs.getStateForPlacement + getMetaFromState
    static int metaStairs(int hitFace, int yawQuad, int sneaked) {
        int facing = horizontalFacing(yawQuad);
        int halfTop;
        hitFace &= 7;
        if (hitFace == DOWN) halfTop = 1;
        else if (hitFace == UP) halfTop = 0;
        else halfTop = sneaked != 0 ? 1 : 0;
        return (halfTop != 0 ? 4 : 0) | (5 - facing);
    }

    // BlockOldLog: axis from hit face + variant low bits
    static int metaLog(int hitFace, int stackMeta) {
        int meta = itemMetadata(LOG, stackMeta) & 3;
        int ax = axis(hitFace);
        if (ax == 1) meta |= 4;
        else if (ax == 2) meta |= 8;
        return meta;
    }

    static int metaFurnace(int yawQuad) {
        return opposite(horizontalFacing(yawQuad));
    }
    // Final chest meta after onBlockPlacedBy (opposite of look).
    static int metaChest(int yawQuad) {
        return opposite(horizontalFacing(yawQuad));
    }
    static int metaLadder(int hitFace) {
        if (isHorizontal(hitFace)) return hitFace & 7;
        return NORTH;
    }
    static int metaPiston(int yawQuad, int hitFace, int sneaked) {
        return dirFromEntity(yawQuad, hitFace, sneaked);
    }
    static int metaObserver(int yawQuad, int hitFace, int sneaked) {
        return opposite(dirFromEntity(yawQuad, hitFace, sneaked));
    }
    static int metaDispenser(int yawQuad, int hitFace, int sneaked) {
        return dirFromEntity(yawQuad, hitFace, sneaked);
    }
    static int metaPumpkin(int yawQuad) {
        return horizontalIndex(opposite(horizontalFacing(yawQuad)));
    }
    static int metaTorch(int hitFace) {
        switch (hitFace & 7) {
            case EAST:  return 1;
            case WEST:  return 2;
            case SOUTH: return 3;
            case NORTH: return 4;
            case UP:    return 5;
            case DOWN:
            default:    return 5;
        }
    }

    static int placedMeta(int blockId, int hitFace, int yawQuad, int sneaked, int stackMeta) {
        switch (blockId) {
            case OAK_STAIRS: return metaStairs(hitFace, yawQuad, sneaked);
            case LOG:        return metaLog(hitFace, stackMeta);
            case FURNACE:    return metaFurnace(yawQuad);
            case CHEST:      return metaChest(yawQuad);
            case LADDER:     return metaLadder(hitFace);
            case PISTON:     return metaPiston(yawQuad, hitFace, sneaked);
            case OBSERVER:   return metaObserver(yawQuad, hitFace, sneaked);
            case DISPENSER:  return metaDispenser(yawQuad, hitFace, sneaked);
            case PUMPKIN:    return metaPumpkin(yawQuad);
            case TORCH:      return metaTorch(hitFace);
            default:         return itemMetadata(blockId, stackMeta) & 15;
        }
    }

    static int kindBlockId(int kind) { return KINDS[kind % NUM_KINDS]; }

    static int caseWord(int idx) {
        int sneak = idx % NUM_SNEAK;
        int t = idx / NUM_SNEAK;
        int face = t % NUM_FACES;
        t /= NUM_FACES;
        int yaw = t % NUM_YAWS;
        int kind = t / NUM_YAWS;
        int blockId = kindBlockId(kind);
        int meta = placedMeta(blockId, face, yaw, sneak, 0) & 15;
        return (blockId << 16) | (face << 12) | (yaw << 8) | (sneak << 4) | meta;
    }

    public static void main(String[] args) {
        if (args.length > 0) {
            int idx = Integer.parseInt(args[0]);
            System.out.printf("%08x%n", caseWord(idx));
            return;
        }
        for (int i = 0; i < NUM_CASES; ++i)
            System.out.printf("%08x%n", caseWord(i));
    }
}
