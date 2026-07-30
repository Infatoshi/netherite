/* mc_blocks.h - block id enum (vanilla 1.11.2 numeric ids, from block/Block.java registerBlock)
 * + the property-table SCHEMA. TRUNK: the schema is fixed here once; per-block subagents fill ROWS
 * (append-only), they do NOT edit shared logic. CUT blocks (redstone/decorative, see SPEC) are
 * intentionally absent from the KEEP enum but ids are reserved by vanilla numbering - never renumber.
 *
 * Only ids needed by current oracles are enumerated; extend from Block.java as subagents reach them. */
#ifndef MC_BLOCKS_H
#define MC_BLOCKS_H

#include "mc.h"

/* Vanilla numeric block ids (Block.getIdFromBlock). Keep names; values are FIXED by vanilla. */
enum {
    BLK_AIR = 0, BLK_STONE = 1, BLK_GRASS = 2, BLK_DIRT = 3, BLK_COBBLESTONE = 4,
    BLK_PLANKS = 5, BLK_SAPLING = 6, BLK_BEDROCK = 7, BLK_FLOWING_WATER = 8, BLK_WATER = 9,
    BLK_FLOWING_LAVA = 10, BLK_LAVA = 11, BLK_SAND = 12, BLK_GRAVEL = 13, BLK_GOLD_ORE = 14,
    BLK_IRON_ORE = 15, BLK_COAL_ORE = 16, BLK_LOG = 17, BLK_LEAVES = 18, BLK_GLASS = 20,
    BLK_LAPIS_ORE = 21, BLK_SANDSTONE = 24, BLK_WEB = 30, BLK_STONE_SLAB = 44,
    BLK_OBSIDIAN = 49, BLK_TORCH = 50, BLK_OAK_STAIRS = 53,
    BLK_DIAMOND_ORE = 56, BLK_LADDER = 65, BLK_STONE_STAIRS = 67,
    BLK_REDSTONE_ORE = 73,
    BLK_SNOW_LAYER = 78, BLK_ICE = 79, BLK_CACTUS = 81, BLK_CLAY = 82, BLK_FENCE = 85,
    BLK_NETHERRACK = 87, BLK_SOUL_SAND = 88, BLK_GLOWSTONE = 89,
    BLK_TRAPDOOR = 96,
    BLK_NETHER_BRICK_FENCE = 113, BLK_END_STONE = 121, BLK_WOODEN_SLAB = 126,
    BLK_EMERALD_ORE = 129,
    BLK_COBBLESTONE_WALL = 139, BLK_SLIME = 165, BLK_PACKED_ICE = 174,
    BLK_RED_SANDSTONE_SLAB = 182,
    BLK_MAX = 256
};

/* Block property flags (bitset). Extend as needed; keep stable bit positions. */
enum {
    BF_SOLID      = 1 << 0,   /* full opaque cube for collision/occlusion default */
    BF_LIQUID     = 1 << 1,
    BF_REPLACEABLE= 1 << 2,   /* worldgen/placement may overwrite (air, tallgrass, water) */
    BF_FALLING    = 1 << 3,   /* sand/gravel gravity */
    BF_TICK_RANDOM= 1 << 4,   /* receives random ticks */
};

/* One row per block id. Filled by per-block subagents; porting target is the corresponding
 * Block subclass + its setHardness/setLightLevel/setLightOpacity in Block.java registration. */
typedef struct {
    float hardness;     /* setHardness; -1 = unbreakable (bedrock) */
    u8    light_emit;   /* 0..15 (setLightLevel*15) */
    u8    light_opacity;/* setLightOpacity; 255 default opaque, 0 transparent */
    u16   flags;        /* BF_* */
} BlockProps;

/* Starter rows for blocks the worldgen/terrain oracles touch. NOT the full table - subagents add
 * the rest. Index by block id. Verified per-row against Block.java + the block's class. */
MC_HD static inline BlockProps mc_block_props(int id) {
    BlockProps p = (BlockProps){ 1.0f, 0, 255, BF_SOLID };
    switch (id) {
        case BLK_AIR:          p = (BlockProps){ 0.0f, 0, 0, BF_REPLACEABLE }; break;
        case BLK_STONE:        p = (BlockProps){ 1.5f, 0, 255, BF_SOLID }; break;
        case BLK_GRASS:        p = (BlockProps){ 0.6f, 0, 255, BF_SOLID | BF_TICK_RANDOM }; break;
        case BLK_DIRT:         p = (BlockProps){ 0.5f, 0, 255, BF_SOLID }; break;
        case BLK_BEDROCK:      p = (BlockProps){ -1.0f, 0, 255, BF_SOLID }; break;
        case BLK_WATER:
        case BLK_FLOWING_WATER:p = (BlockProps){ 100.0f, 0, 3, BF_LIQUID | BF_REPLACEABLE }; break;
        case BLK_LAVA:
        case BLK_FLOWING_LAVA: p = (BlockProps){ 100.0f, 15, 255, BF_LIQUID | BF_REPLACEABLE }; break;
        case BLK_SAND:         p = (BlockProps){ 0.5f, 0, 255, BF_SOLID | BF_FALLING }; break;
        case BLK_GRAVEL:       p = (BlockProps){ 0.6f, 0, 255, BF_SOLID | BF_FALLING }; break;
        case BLK_GOLD_ORE: case BLK_IRON_ORE: case BLK_COAL_ORE: case BLK_LAPIS_ORE:
        case BLK_DIAMOND_ORE: case BLK_REDSTONE_ORE: case BLK_EMERALD_ORE:
                               p = (BlockProps){ 3.0f, 0, 255, BF_SOLID }; break;
        case BLK_STONE_SLAB:
        case BLK_WOODEN_SLAB:
        case BLK_RED_SANDSTONE_SLAB:
                               p = (BlockProps){ 2.0f, 0, 0, BF_SOLID }; break;
        case BLK_LADDER:       p = (BlockProps){ 0.4f, 0, 0, BF_SOLID }; break;
        default: break;   /* subagents append cases here */
    }
    return p;
}

#endif /* MC_BLOCKS_H */
