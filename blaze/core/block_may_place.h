/* World.mayPlace + BlockTNT flint-and-steel ignite (MC 1.11.2).
 *
 * ItemBlock.onItemUse calls world.mayPlace(block, pos, false, facing, null)
 * (ItemBlock.java:49). EntityTNTPrimed flint path is BlockTNT.onBlockActivated
 * (BlockTNT.java:105-119) then explode (BlockTNT.java:85-96).
 * CPU==CUDA. No world.rand draw: EntityTNTPrimed ctor xz is Math.random CUT
 * (EntityTNTPrimed.java:34-37); flint fuse is 80 not chain nextInt. */
#ifndef MC_BLOCK_MAY_PLACE_H
#define MC_BLOCK_MAY_PLACE_H

#include "item_block_place.h"
#include "player_survival.h"

#define IBP_ITEM_FLINT_AND_STEEL 259 /* Items.FLINT_AND_STEEL */
#define IBP_FLINT_MAX_DAMAGE 64      /* ItemFlintAndSteel.java:20 */
#define IBP_TNT_FUSE 80              /* EntityTNTPrimed.java:25,38 */
#define IBP_BLK_SAPLING 6
#define IBP_BLK_TALLGRASS 31
#define IBP_BLK_DEADBUSH 32
#define IBP_BLK_YELLOW_FLOWER 37
#define IBP_BLK_RED_FLOWER 38
#define IBP_BLK_FARMLAND 60
#define IBP_BLK_LADDER 65
#define IBP_BLK_CACTUS 81
#define IBP_BLK_WOODEN_DOOR 64
#define IBP_BLK_IRON_DOOR 71
#define IBP_BLK_SPRUCE_DOOR 193
#define IBP_BLK_BIRCH_DOOR 194
#define IBP_BLK_JUNGLE_DOOR 195
#define IBP_BLK_ACACIA_DOOR 196
#define IBP_BLK_DARK_OAK_DOOR 197

/* Block.isReplaceable used by magma psv_replaceable: air, liquids,
 * tallgrass/deadbush, fire, snow layer. */
MC_HD static inline int ibp_is_replaceable(int id) {
    return id == 0 || (id >= 8 && id <= 11) || id == 31 || id == 32 ||
           id == 51 || id == 78;
}

/* Block.getCollisionBoundingBox == NULL_AABB: no entity gate in mayPlace
 * (World.java:3366 axisalignedbb != NULL_AABB). Torch BlockTorch.java:66-70;
 * BlockBush.java:93-97 (sapling/plants). Ladder/cactus/door HAVE boxes. */
MC_HD static inline int ibp_null_collision(int block_id) {
    return block_id == IBP_BLK_TORCH || block_id == 76 ||
           block_id == IBP_BLK_SAPLING || block_id == IBP_BLK_TALLGRASS ||
           block_id == IBP_BLK_DEADBUSH || block_id == IBP_BLK_YELLOW_FLOWER ||
           block_id == IBP_BLK_RED_FLOWER;
}

MC_HD static inline int ibp_aabb_hits_block(const McAABB *pb, int x, int y,
                                            int z) {
    if (!pb) return 0;
    return pb->minX < (double)x + 1.0 && pb->maxX > (double)x &&
           pb->minY < (double)y + 1.0 && pb->maxY > (double)y &&
           pb->minZ < (double)z + 1.0 && pb->maxZ > (double)z;
}

/* BlockTorch.canPlaceAt (BlockTorch.java:111-116). face is the torch FACING
 * (clicked EnumFacing). Horizontal: isSideSolid of the opposite neighbour.
 * UP: canPlaceOn = isSideSolid UP / canPlaceTorchOnTop (psv_solid). */
MC_HD static inline int ibp_torch_can_place_at(const Chunk *w, int x, int y,
                                               int z, int face) {
    switch (face & 7) {
    case IBP_UP:
        return psv_solid(psv_get_block(w, x, y - 1, z));
    case IBP_NORTH:
        return psv_solid(psv_get_block(w, x, y, z + 1));
    case IBP_SOUTH:
        return psv_solid(psv_get_block(w, x, y, z - 1));
    case IBP_WEST:
        return psv_solid(psv_get_block(w, x + 1, y, z));
    case IBP_EAST:
        return psv_solid(psv_get_block(w, x - 1, y, z));
    default:
        return 0; /* DOWN is not in FACING allowed values */
    }
}

/* BlockTorch.canPlaceBlockAt (BlockTorch.java:98-109). */
MC_HD static inline int ibp_torch_can_place_block_at(const Chunk *w, int x,
                                                     int y, int z) {
    return ibp_torch_can_place_at(w, x, y, z, IBP_UP) ||
           ibp_torch_can_place_at(w, x, y, z, IBP_NORTH) ||
           ibp_torch_can_place_at(w, x, y, z, IBP_SOUTH) ||
           ibp_torch_can_place_at(w, x, y, z, IBP_WEST) ||
           ibp_torch_can_place_at(w, x, y, z, IBP_EAST);
}

/* BlockTorch.getStateForPlacement (BlockTorch.java:122-140) then meta.
 * Magma extra vs Java: if no valid facing including UP fallback, return -1
 * (Java would still emit default UP then checkForDrop pops it). */
MC_HD static inline int ibp_torch_placement_meta(const Chunk *w, int x, int y,
                                                 int z, int face) {
    if (ibp_torch_can_place_at(w, x, y, z, face))
        return ibp_meta_torch(face);
    if (ibp_torch_can_place_at(w, x, y, z, IBP_NORTH))
        return ibp_meta_torch(IBP_NORTH);
    if (ibp_torch_can_place_at(w, x, y, z, IBP_SOUTH))
        return ibp_meta_torch(IBP_SOUTH);
    if (ibp_torch_can_place_at(w, x, y, z, IBP_WEST))
        return ibp_meta_torch(IBP_WEST);
    if (ibp_torch_can_place_at(w, x, y, z, IBP_EAST))
        return ibp_meta_torch(IBP_EAST);
    if (ibp_torch_can_place_at(w, x, y, z, IBP_UP))
        return ibp_meta_torch(IBP_UP);
    return -1;
}

MC_HD static inline int ibp_is_bush(int block_id) {
    return block_id == IBP_BLK_SAPLING || block_id == IBP_BLK_TALLGRASS ||
           block_id == IBP_BLK_YELLOW_FLOWER || block_id == IBP_BLK_RED_FLOWER;
}

/* BlockBush.canSustainBush (BlockBush.java:48-51) / Plains canSustainPlant
 * (Block.java:1890): grass, dirt, farmland. Mushrooms/deadbush/reeds CUT. */
MC_HD static inline int ibp_bush_soil(int soil) {
    return soil == BLK_GRASS || soil == BLK_DIRT || soil == IBP_BLK_FARMLAND;
}

MC_HD static inline int ibp_is_door_block(int block_id) {
    return block_id == IBP_BLK_WOODEN_DOOR || block_id == IBP_BLK_IRON_DOOR ||
           (block_id >= IBP_BLK_SPRUCE_DOOR && block_id <= IBP_BLK_DARK_OAK_DOOR);
}

/* Per-block canPlaceBlockAt. Default Block.java:802-805 is dest replaceable
 * (already required by mayPlace). */
MC_HD static inline int ibp_can_place_block_at(const Chunk *w, int block_id,
                                               int x, int y, int z) {
    if (block_id == IBP_BLK_TORCH)
        return ibp_torch_can_place_block_at(w, x, y, z);
    if (ibp_is_bush(block_id))
        return ibp_bush_soil(psv_get_block(w, x, y - 1, z));
    if (block_id == IBP_BLK_CACTUS) {
        int n;
        if (psv_get_block(w, x, y - 1, z) != BLK_SAND) return 0;
        n = psv_get_block(w, x, y + 1, z);
        if (n >= 8 && n <= 11) return 0;
        n = psv_get_block(w, x, y, z + 1);
        if (psv_solid(n) || n == 10 || n == 11) return 0;
        n = psv_get_block(w, x, y, z - 1);
        if (psv_solid(n) || n == 10 || n == 11) return 0;
        n = psv_get_block(w, x - 1, y, z);
        if (psv_solid(n) || n == 10 || n == 11) return 0;
        n = psv_get_block(w, x + 1, y, z);
        if (psv_solid(n) || n == 10 || n == 11) return 0;
        return 1;
    }
    if (block_id == IBP_BLK_LADDER)
        return psv_solid(psv_get_block(w, x - 1, y, z)) ||
               psv_solid(psv_get_block(w, x + 1, y, z)) ||
               psv_solid(psv_get_block(w, x, y, z - 1)) ||
               psv_solid(psv_get_block(w, x, y, z + 1));
    if (ibp_is_door_block(block_id)) {
        /* BlockDoor.java:240-242. ItemDoor.onItemUse stays out. */
        if (y >= 255) return 0;
        if (!psv_solid(psv_get_block(w, x, y - 1, z))) return 0;
        if (!ibp_is_replaceable(psv_get_block(w, x, y + 1, z))) return 0;
        return 1;
    }
    return 1;
}

/* World.mayPlace (World.java:3363-3368), skipCollisionCheck=false,
 * exclude-entity null so the player AABB is checked. Anvil-on-circuits CUT. */
MC_HD static inline int ibp_may_place(const Chunk *w, int block_id, int x,
                                      int y, int z, int face,
                                      const McAABB *player_bb) {
    int dest;
    (void)face;
    if (!ibp_null_collision(block_id) &&
        ibp_aabb_hits_block(player_bb, x, y, z))
        return 0;
    dest = psv_get_block(w, x, y, z);
    if (!ibp_is_replaceable(dest)) return 0;
    return ibp_can_place_block_at(w, block_id, x, y, z);
}

/* BlockTNT.onBlockActivated flint-and-steel (BlockTNT.java:105-107).
 * Fire charge (385) CUT. */
MC_HD static inline int ibp_tnt_flint_activate(int hit_id, int held_item) {
    return hit_id == BLK_TNT && held_item == IBP_ITEM_FLINT_AND_STEEL;
}

/* Item.damageItem(1) without Unbreaking (ItemStack.java:351-370):
 * itemDamage += 1, break when itemDamage > maxDamage 64. */
MC_HD static inline int ibp_flint_broke(int meta, int *out_meta) {
    int m = meta + 1;
    if (m > IBP_FLINT_MAX_DAMAGE) {
        *out_meta = 0;
        return 1;
    }
    *out_meta = m;
    return 0;
}

/* BlockTNT.explode spawn pos (BlockTNT.java:91):
 * (float)x + 0.5F, y, (float)z + 0.5F. Fuse 80. No world.rand. */
MC_HD static inline void ibp_tnt_primed_pos(int x, int y, int z, double *px,
                                            double *py, double *pz) {
    *px = (double)((float)x + 0.5f);
    *py = (double)y;
    *pz = (double)((float)z + 0.5f);
}

#endif /* MC_BLOCK_MAY_PLACE_H */
