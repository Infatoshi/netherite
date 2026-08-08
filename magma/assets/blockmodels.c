/* blockmodels.c - CB_* -> per-face sprite/tint/layer table, atlas UV lookup,
 * and the real-MC atlas as a CrTexture with a gamma-correct mip chain.
 * Owner: ASSETS agent. Sprite indices come from generated assets/atlas_gen.h. */
#include "assets/blockmodels.h"
#include "assets/atlas_gen.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

/* blaze CB_ / PB_ block-state ids (the set our worldgen + populate emit). The
 * feature ids 21+ come from blaze populate.h (PB_ ids); the CBX_ ids at 200+ are
 * magma-local synthetic ids used only by the non-cube mesh unit test (no
 * worldgen emits them). Kept blaze-header-free on purpose. */
enum {
    CB_AIR = 0, CB_STONE = 1, CB_WATER = 2, CB_GRASS = 3, CB_DIRT = 4,
    CB_BEDROCK = 5, CB_GRAVEL = 6, CB_SAND = 7, CB_SANDSTONE = 8,
    CB_RED_SANDSTONE = 9, CB_ICE = 10, CB_LAVA = 11, CB_FLOWING_LAVA = 12,
    CB_FLOWING_WATER = 13, CB_WATER_LILY = 14, CB_MYCELIUM = 15,
    CB_SNOW_LAYER = 16, CB_HARDENED_CLAY = 17, CB_STAINED_HARDENED_CLAY = 18,
    CB_PODZOL = 19, CB_COARSE_DIRT = 20,
    /* populate.h stone variants + ores + clay (share CB/PB numbering) */
    PB_GRANITE = 21, PB_DIORITE = 22, PB_ANDESITE = 23,
    PB_COAL_ORE = 24, PB_IRON_ORE = 25, PB_GOLD_ORE = 26, PB_REDSTONE_ORE = 27,
    PB_DIAMOND_ORE = 28, PB_LAPIS_ORE = 29, PB_CLAY = 30,
    PB_LOG_OAK = 31, PB_LOG_BIRCH = 32, PB_LOG_SPRUCE = 33,
    PB_LEAVES_OAK = 34, PB_LEAVES_BIRCH = 35, PB_LEAVES_SPRUCE = 36,
    PB_LOG_OAK_X = 37, PB_LOG_OAK_Z = 38,
    PB_TALLGRASS = 39, PB_FERN = 40, PB_DEADBUSH = 41,
    PB_BROWN_MUSHROOM = 42, PB_RED_MUSHROOM = 43, PB_REEDS = 44,
    PB_COBBLESTONE = 45, PB_MOSSY_COBBLESTONE = 46,
    PB_MOB_SPAWNER = 47, PB_BONE_BLOCK = 48, PB_CHEST = 49,
    PB_YELLOW_FLOWER = 50, PB_RED_FLOWER_BASE = 51,
    PB_DPLANT_LOWER_BASE = 60, PB_DPLANT_UPPER = 66,
    PB_PUMPKIN_BASE = 67, PB_VINE_BASE = 71,
    PB_EMERALD_ORE = 75, PB_MONSTER_EGG = 76,
    PB_LOG_DARKOAK = 77, PB_LEAVES_DARKOAK = 78,
    PB_BROWN_SHROOM_BLOCK = 79, PB_RED_SHROOM_BLOCK = 80,
    PB_CACTUS = 81, PB_LOG_ACACIA = 82, PB_LEAVES_ACACIA = 83,
    PB_SANDSTONE_SLAB = 84, PB_LOG_JUNGLE = 85, PB_LEAVES_JUNGLE = 86,
    PB_MELON = 87, PB_COCOA = 88, PB_OBSIDIAN = 89,
    PB_SANDSTONE_SMOOTH = 90, PB_SANDSTONE_CHISELED = 91,
    PB_SANDSTONE_STAIRS_E = 92, PB_SANDSTONE_STAIRS_W = 93,
    PB_SANDSTONE_STAIRS_S = 94, PB_SANDSTONE_STAIRS_N = 95,
    PB_STAINED_CLAY_ORANGE = 96, PB_STAINED_CLAY_BLUE = 97,
    PB_STONE_PRESSURE_PLATE = 98, PB_TNT = 99,
    CB_STAINED_CLAY_BASE = 120,
    /* magma-local synthetic ids (mesh unit test only) */
    CBX_GLASS = 200, CBX_STAIRS = 201, CBX_SLAB = 202, CBX_FENCE = 203,
    /* dim / portal blocks: vanilla dumpblocks map into these (not CB/PB collision) */
    CBX_NETHERRACK = 210, CBX_PORTAL = 211, CBX_END_STONE = 212,
    CBX_FIRE = 213, CBX_GLOWSTONE = 214, CBX_SOUL_SAND = 215,
    CBX_END_FRAME = 216, CBX_QUARTZ_ORE = 217,
    CBX_BROWN_MUSHROOM = 218, CBX_RED_MUSHROOM = 219, CBX_MAGMA = 220,
    CBX_IRON_BARS = 221, CBX_TORCH = 222,
    /* player-placeable blocks with multi-face vanilla models */
    CBX_CRAFTING_TABLE = 223,
    /* vanilla planks (all species share the oak sprite for now) */
    CBX_PLANKS = 224,
    /* polished BlockStone metas 2/4/6 (fixed, non-random blockstate models) */
    CBX_GRANITE_SMOOTH = 225, CBX_DIORITE_SMOOTH = 226,
    CBX_ANDESITE_SMOOTH = 227,
    CBX_NETHER_BRICK = 228,
    CBX_SLIME = 229, CBX_WEB = 230, CBX_PACKED_ICE = 231,
    CBX_NETHER_BRICK_FENCE = 232, CBX_COBBLESTONE_WALL = 233,
    CBX_END_PORTAL = 234,
    CBX_RAIL = 235, CBX_TNT = 236,
    /* Canonical stone_slab id 44: model key preserves variant (low 3 bits)
     * and half (bit 3). Keep the 16 rows contiguous for the registry bridge. */
    CBX_STONE_SLAB_BOTTOM_BASE = 237,
    CBX_STONE_SLAB_TOP_BASE = 245,
    CBX_GLASS_PANE = 253,
    CBX_COBBLESTONE_STAIRS = 254,
    CBX_TRAPDOOR = 255,
    CBX_LADDER = 256,
    CBX_STONEBRICK = 257,
    /* Context-derived upper-half models. The saved upper metadata only carries
     * HALF/FACING, so mesh_mc selects one of these from the lower-half variant. */
    CBX_DPLANT_UPPER_SYRINGA = 258,
    CBX_DPLANT_UPPER_GRASS = 259,
    CBX_DPLANT_UPPER_FERN = 260,
    CBX_DPLANT_UPPER_ROSE = 261,
    CBX_DPLANT_UPPER_PAEONIA = 262,
    /* End City blocks, kept separate from canonical ids in runtime state. */
    CBX_PURPUR_BLOCK = 263,
    CBX_PURPUR_PILLAR_Y = 264,
    CBX_PURPUR_PILLAR_X = 265,
    CBX_PURPUR_PILLAR_Z = 266,
    CBX_PURPUR_STAIRS = 267,
    CBX_PURPUR_SLAB_BOTTOM = 268,
    CBX_PURPUR_SLAB_TOP = 269,
    CBX_END_BRICKS = 270,
    CBX_END_ROD = 271,
    CBX_GLASS_MAGENTA = 272,
    CBX_CHORUS_PLANT = 273,
    CBX_CHORUS_FLOWER = 274,
    CBX_CHORUS_FLOWER_DEAD = 275,
    CBX_MAX = 276
};

/* Face order is BM_DOWN, BM_UP, BM_NORTH, BM_SOUTH, BM_WEST, BM_EAST. */
#define FULL6(spr, t) { {spr,t},{spr,t},{spr,t},{spr,t},{spr,t},{spr,t} }

/* Cube with distinct top/bottom/side sprites (sides share one sprite). */
#define TBS(top, bot, side, ttint, stint) { \
    { bot,  BM_TINT_NONE }, /* DOWN  */ \
    { top,  ttint        }, /* UP    */ \
    { side, stint        }, /* NORTH */ \
    { side, stint        }, /* SOUTH */ \
    { side, stint        }, /* WEST  */ \
    { side, stint        }  /* EAST  */ \
}

/* full field order: is_air, is_full_cube, layer, kind, face[6] */
#define CUBE6(spr, t)        { 0, 1, CR_LAYER_SOLID, BM_KIND_CUBE, FULL6(spr, t) }
#define CROSS1(spr, t)       { 0, 0, CR_LAYER_CUTOUT, BM_KIND_CROSS, FULL6(spr, t) }
#define FLUID6(still, flow, t) { \
    {still,t},{still,t},{flow,t},{flow,t},{flow,t},{flow,t} \
}
/* Leaves = Fast graphics (options fancyGraphics:false / Java BlockLeaves
 * !leavesFancy): isOpaqueCube=true, layer=SOLID, shouldSideBeRendered culls vs
 * opaque neighbours. is_full_cube=1 gives the same cull path. Fancy cutout is
 * not the shared perf target. */
#define LEAF(spr)            { 0, 1, CR_LAYER_SOLID, BM_KIND_CUBE, \
                               FULL6(spr, BM_TINT_FOLIAGE) }
/* Spruce/birch: fixed pine/birch foliage colors (not biome colormap). */
#define LEAF_PINE(spr)       { 0, 1, CR_LAYER_SOLID, BM_KIND_CUBE, \
                               FULL6(spr, BM_TINT_FOLIAGE_PINE) }
#define LEAF_BIRCH(spr)      { 0, 1, CR_LAYER_SOLID, BM_KIND_CUBE, \
                               FULL6(spr, BM_TINT_FOLIAGE_BIRCH) }
#define LOG6(side, top)      { 0, 1, CR_LAYER_SOLID, BM_KIND_CUBE, \
                               TBS(top, top, side, BM_TINT_NONE, BM_TINT_NONE) }
#define SLAB6(kind, top, bot, side) { 0, 0, CR_LAYER_SOLID, kind, \
                                      TBS(top, bot, side, BM_TINT_NONE, BM_TINT_NONE) }

static const BmBlock g_air = {
    1, 0, CR_LAYER_SOLID, BM_KIND_CUBE, FULL6(CR_SPRITE_STONE, BM_TINT_NONE)
};
static const BmBlock g_stone = { 0, 1, CR_LAYER_SOLID, BM_KIND_CUBE,
                                 FULL6(CR_SPRITE_STONE, BM_TINT_NONE) };

/* Indexed by block-state id, 0..CBX_MAX-1. Unmodeled ids fall through to stone
 * in bm_block(). */
static const BmBlock g_blocks[CBX_MAX] = {
    [CB_AIR]   = { 1, 0, CR_LAYER_SOLID, BM_KIND_CUBE, FULL6(CR_SPRITE_STONE, BM_TINT_NONE) },
    [CB_STONE] = CUBE6(CR_SPRITE_STONE, BM_TINT_NONE),
    /* BlockLiquid is neither a full cube nor opaque; adjacent solid faces stay. */
    [CB_WATER] = { 0, 0, CR_LAYER_TRANSLUCENT, BM_KIND_FLUID,
                   FLUID6(CR_SPRITE_WATER_STILL, CR_SPRITE_WATER_FLOW,
                          BM_TINT_WATER) },
    [CB_GRASS] = { 0, 1, CR_LAYER_SOLID, BM_KIND_CUBE,
                   TBS(CR_SPRITE_GRASS_TOP, CR_SPRITE_DIRT, CR_SPRITE_GRASS_SIDE,
                       BM_TINT_GRASS, BM_TINT_NONE) },
    [CB_DIRT]    = CUBE6(CR_SPRITE_DIRT, BM_TINT_NONE),
    [CB_BEDROCK] = CUBE6(CR_SPRITE_BEDROCK, BM_TINT_NONE),
    [CB_GRAVEL]  = CUBE6(CR_SPRITE_GRAVEL, BM_TINT_NONE),
    [CB_SAND]    = CUBE6(CR_SPRITE_SAND, BM_TINT_NONE),
    [CB_SANDSTONE] = { 0, 1, CR_LAYER_SOLID, BM_KIND_CUBE,
                       TBS(CR_SPRITE_SANDSTONE_TOP, CR_SPRITE_SANDSTONE_BOTTOM,
                           CR_SPRITE_SANDSTONE_NORMAL, BM_TINT_NONE, BM_TINT_NONE) },
    [CB_RED_SANDSTONE] = { 0, 1, CR_LAYER_SOLID, BM_KIND_CUBE,
                       TBS(CR_SPRITE_RED_SANDSTONE_TOP, CR_SPRITE_RED_SANDSTONE_BOTTOM,
                           CR_SPRITE_RED_SANDSTONE_NORMAL, BM_TINT_NONE, BM_TINT_NONE) },
    [CB_ICE]     = { 0, 1, CR_LAYER_TRANSLUCENT, BM_KIND_CUBE,
                     FULL6(CR_SPRITE_ICE, BM_TINT_NONE) },
    [CB_LAVA]         = { 0, 0, CR_LAYER_SOLID, BM_KIND_FLUID,
                          FLUID6(CR_SPRITE_LAVA_STILL, CR_SPRITE_LAVA_FLOW,
                                 BM_TINT_NONE) },
    [CB_FLOWING_LAVA] = { 0, 0, CR_LAYER_SOLID, BM_KIND_FLUID,
                          FLUID6(CR_SPRITE_LAVA_STILL, CR_SPRITE_LAVA_FLOW,
                                 BM_TINT_NONE) },
    [CB_FLOWING_WATER]= { 0, 0, CR_LAYER_TRANSLUCENT, BM_KIND_FLUID,
                          FLUID6(CR_SPRITE_WATER_STILL, CR_SPRITE_WATER_FLOW,
                                 BM_TINT_WATER) },
    /* Vanilla waterlily.json: plane at y=0.25, up+down only, fixed 0x208030. */
    [CB_WATER_LILY] = { 0, 0, CR_LAYER_CUTOUT, BM_KIND_LILY,
                        FULL6(CR_SPRITE_WATERLILY, BM_TINT_LILY) },
    [CB_MYCELIUM] = { 0, 1, CR_LAYER_SOLID, BM_KIND_CUBE,
                      TBS(CR_SPRITE_MYCELIUM_TOP, CR_SPRITE_DIRT, CR_SPRITE_MYCELIUM_SIDE,
                          BM_TINT_NONE, BM_TINT_NONE) },
    /* snow_height2.json: y 0..2 box, not a full cube. */
    [CB_SNOW_LAYER] = { 0, 0, CR_LAYER_SOLID, BM_KIND_SNOW_LAYER,
                        FULL6(CR_SPRITE_SNOW, BM_TINT_NONE) },
    [CB_HARDENED_CLAY] = CUBE6(CR_SPRITE_HARDENED_CLAY, BM_TINT_NONE),
    [CB_STAINED_HARDENED_CLAY] = CUBE6(CR_SPRITE_HARDENED_CLAY_STAINED_WHITE, BM_TINT_NONE),
    [CB_STAINED_CLAY_BASE ... CB_STAINED_CLAY_BASE + 15] =
        CUBE6(CR_SPRITE_HARDENED_CLAY_STAINED_WHITE, BM_TINT_NONE),
    [CB_PODZOL] = { 0, 1, CR_LAYER_SOLID, BM_KIND_CUBE,
                    TBS(CR_SPRITE_DIRT_PODZOL_TOP, CR_SPRITE_DIRT, CR_SPRITE_DIRT_PODZOL_SIDE,
                        BM_TINT_NONE, BM_TINT_NONE) },
    [CBX_IRON_BARS] = { 0, 0, CR_LAYER_CUTOUT_MIPPED, BM_KIND_IRON_BARS,
                        FULL6(CR_SPRITE_IRON_BARS, BM_TINT_NONE) },
    [CBX_GLASS_PANE] = { 0, 0, CR_LAYER_CUTOUT_MIPPED, BM_KIND_GLASS_PANE, {
        { CR_SPRITE_GLASS_PANE_TOP, BM_TINT_NONE },
        { CR_SPRITE_GLASS_PANE_TOP, BM_TINT_NONE },
        { CR_SPRITE_GLASS, BM_TINT_NONE },
        { CR_SPRITE_GLASS, BM_TINT_NONE },
        { CR_SPRITE_GLASS, BM_TINT_NONE },
        { CR_SPRITE_GLASS, BM_TINT_NONE },
    } },
    [CBX_TORCH] = { 0, 0, CR_LAYER_CUTOUT, BM_KIND_TORCH,
                    FULL6(CR_SPRITE_TORCH_ON, BM_TINT_NONE) },
    [CBX_RAIL] = { 0, 0, CR_LAYER_CUTOUT, BM_KIND_RAIL,
                   FULL6(CR_SPRITE_RAIL_NORMAL, BM_TINT_NONE) },
    [CBX_TRAPDOOR] = { 0, 0, CR_LAYER_CUTOUT, BM_KIND_TRAPDOOR,
                       FULL6(CR_SPRITE_TRAPDOOR, BM_TINT_NONE) },
    [CBX_LADDER] = { 0, 0, CR_LAYER_CUTOUT, BM_KIND_LADDER,
                     FULL6(CR_SPRITE_LADDER, BM_TINT_NONE) },
    [CBX_STONEBRICK] = CUBE6(CR_SPRITE_STONEBRICK, BM_TINT_NONE),
    [CB_COARSE_DIRT] = CUBE6(CR_SPRITE_COARSE_DIRT, BM_TINT_NONE),

    /* ---- stone variants + ores + clay ---- */
    [PB_GRANITE]      = CUBE6(CR_SPRITE_STONE_GRANITE, BM_TINT_NONE),
    [PB_DIORITE]      = CUBE6(CR_SPRITE_STONE_DIORITE, BM_TINT_NONE),
    [PB_ANDESITE]     = CUBE6(CR_SPRITE_STONE_ANDESITE, BM_TINT_NONE),
    [CBX_GRANITE_SMOOTH]  = CUBE6(CR_SPRITE_STONE_GRANITE_SMOOTH, BM_TINT_NONE),
    [CBX_DIORITE_SMOOTH]  = CUBE6(CR_SPRITE_STONE_DIORITE_SMOOTH, BM_TINT_NONE),
    [CBX_ANDESITE_SMOOTH] = CUBE6(CR_SPRITE_STONE_ANDESITE_SMOOTH, BM_TINT_NONE),
    [PB_COAL_ORE]     = CUBE6(CR_SPRITE_COAL_ORE, BM_TINT_NONE),
    [PB_IRON_ORE]     = CUBE6(CR_SPRITE_IRON_ORE, BM_TINT_NONE),
    [PB_GOLD_ORE]     = CUBE6(CR_SPRITE_GOLD_ORE, BM_TINT_NONE),
    [PB_REDSTONE_ORE] = CUBE6(CR_SPRITE_REDSTONE_ORE, BM_TINT_NONE),
    [PB_DIAMOND_ORE]  = CUBE6(CR_SPRITE_DIAMOND_ORE, BM_TINT_NONE),
    [PB_LAPIS_ORE]    = CUBE6(CR_SPRITE_LAPIS_ORE, BM_TINT_NONE),
    [PB_CLAY]         = CUBE6(CR_SPRITE_CLAY, BM_TINT_NONE),
    [PB_EMERALD_ORE]  = CUBE6(CR_SPRITE_EMERALD_ORE, BM_TINT_NONE),
    [PB_MONSTER_EGG]  = CUBE6(CR_SPRITE_STONE, BM_TINT_NONE),
    [PB_OBSIDIAN]     = CUBE6(CR_SPRITE_OBSIDIAN, BM_TINT_NONE),
    [PB_SANDSTONE_SMOOTH] = CUBE6(CR_SPRITE_SANDSTONE_SMOOTH, BM_TINT_NONE),
    [PB_SANDSTONE_CHISELED] = { 0, 1, CR_LAYER_SOLID, BM_KIND_CUBE,
        TBS(CR_SPRITE_SANDSTONE_TOP, CR_SPRITE_SANDSTONE_BOTTOM,
            CR_SPRITE_SANDSTONE_CARVED, BM_TINT_NONE, BM_TINT_NONE) },
    [PB_SANDSTONE_STAIRS_E ... PB_SANDSTONE_STAIRS_N] =
        { 0, 0, CR_LAYER_SOLID, BM_KIND_STAIRS,
          FULL6(CR_SPRITE_SANDSTONE_NORMAL, BM_TINT_NONE) },
    [PB_STAINED_CLAY_ORANGE] =
        CUBE6(CR_SPRITE_HARDENED_CLAY_STAINED_ORANGE, BM_TINT_NONE),
    [PB_STAINED_CLAY_BLUE] =
        CUBE6(CR_SPRITE_HARDENED_CLAY_STAINED_BLUE, BM_TINT_NONE),
    [PB_STONE_PRESSURE_PLATE] =
        { 0, 0, CR_LAYER_SOLID, BM_KIND_PRESSURE_PLATE,
          FULL6(CR_SPRITE_STONE, BM_TINT_NONE) },
    [PB_TNT] = { 0, 1, CR_LAYER_SOLID, BM_KIND_CUBE,
        TBS(CR_SPRITE_TNT_TOP, CR_SPRITE_TNT_BOTTOM, CR_SPRITE_TNT_SIDE,
            BM_TINT_NONE, BM_TINT_NONE) },

    /* ---- trees ---- */
    [PB_LOG_OAK]    = LOG6(CR_SPRITE_LOG_OAK, CR_SPRITE_LOG_OAK_TOP),
    [PB_LOG_OAK_X]  = LOG6(CR_SPRITE_LOG_OAK, CR_SPRITE_LOG_OAK_TOP),
    [PB_LOG_OAK_Z]  = LOG6(CR_SPRITE_LOG_OAK, CR_SPRITE_LOG_OAK_TOP),
    [PB_LOG_BIRCH]  = LOG6(CR_SPRITE_LOG_BIRCH, CR_SPRITE_LOG_BIRCH_TOP),
    [PB_LOG_SPRUCE] = LOG6(CR_SPRITE_LOG_SPRUCE, CR_SPRITE_LOG_SPRUCE_TOP),
    [PB_LOG_DARKOAK]= LOG6(CR_SPRITE_LOG_BIG_OAK, CR_SPRITE_LOG_BIG_OAK_TOP),
    [PB_LOG_ACACIA] = LOG6(CR_SPRITE_LOG_ACACIA, CR_SPRITE_LOG_ACACIA_TOP),
    [PB_LOG_JUNGLE] = LOG6(CR_SPRITE_LOG_JUNGLE, CR_SPRITE_LOG_JUNGLE_TOP),

    [PB_LEAVES_OAK]     = LEAF(CR_SPRITE_LEAVES_OAK),
    [PB_LEAVES_BIRCH]   = LEAF_BIRCH(CR_SPRITE_LEAVES_BIRCH),
    [PB_LEAVES_SPRUCE]  = LEAF_PINE(CR_SPRITE_LEAVES_SPRUCE),
    [PB_LEAVES_DARKOAK] = LEAF(CR_SPRITE_LEAVES_BIG_OAK),
    [PB_LEAVES_ACACIA]  = LEAF(CR_SPRITE_LEAVES_ACACIA),
    [PB_LEAVES_JUNGLE]  = LEAF(CR_SPRITE_LEAVES_JUNGLE),

    /* ---- cross-plants ---- */
    [PB_TALLGRASS] = CROSS1(CR_SPRITE_TALLGRASS, BM_TINT_GRASS),
    [PB_FERN]      = CROSS1(CR_SPRITE_FERN,      BM_TINT_GRASS),
    [PB_DEADBUSH]  = CROSS1(CR_SPRITE_DEADBUSH,  BM_TINT_NONE),
    [PB_REEDS]     = CROSS1(CR_SPRITE_REEDS,     BM_TINT_FOLIAGE),
    [PB_BROWN_MUSHROOM] = CROSS1(CR_SPRITE_MUSHROOM_BROWN, BM_TINT_NONE),
    [PB_RED_MUSHROOM]   = CROSS1(CR_SPRITE_MUSHROOM_RED,   BM_TINT_NONE),
    /* BlockFlower: id 37 = dandelion only; id 38 meta 0..8 = poppy..oxeye daisy.
     * Model keys PB_RED_FLOWER_BASE + meta; textures match EnumFlowerType names. */
    [PB_YELLOW_FLOWER]     = CROSS1(CR_SPRITE_FLOWER_DANDELION, BM_TINT_NONE),
    [PB_RED_FLOWER_BASE + 0] = CROSS1(CR_SPRITE_FLOWER_ROSE, BM_TINT_NONE),
    [PB_RED_FLOWER_BASE + 1] = CROSS1(CR_SPRITE_FLOWER_BLUE_ORCHID, BM_TINT_NONE),
    [PB_RED_FLOWER_BASE + 2] = CROSS1(CR_SPRITE_FLOWER_ALLIUM, BM_TINT_NONE),
    [PB_RED_FLOWER_BASE + 3] = CROSS1(CR_SPRITE_FLOWER_HOUSTONIA, BM_TINT_NONE),
    [PB_RED_FLOWER_BASE + 4] = CROSS1(CR_SPRITE_FLOWER_TULIP_RED, BM_TINT_NONE),
    [PB_RED_FLOWER_BASE + 5] = CROSS1(CR_SPRITE_FLOWER_TULIP_ORANGE, BM_TINT_NONE),
    [PB_RED_FLOWER_BASE + 6] = CROSS1(CR_SPRITE_FLOWER_TULIP_WHITE, BM_TINT_NONE),
    [PB_RED_FLOWER_BASE + 7] = CROSS1(CR_SPRITE_FLOWER_TULIP_PINK, BM_TINT_NONE),
    [PB_RED_FLOWER_BASE + 8] = CROSS1(CR_SPRITE_FLOWER_OXEYE_DAISY, BM_TINT_NONE),
    /* BlockDoublePlant: half=lower meta 0..5 = EnumPlantType. Grass/fern use
     * tinted_cross + biome grass color; flowers use plain cross (no tint).
     * Upper half meta carries only facing - VARIANT comes from the block below
     * (getActualState); mesh_mc resolves that and calls bm_dplant_upper. */
    [PB_DPLANT_LOWER_BASE + 0] = CROSS1(CR_SPRITE_DOUBLE_PLANT_SUNFLOWER_BOTTOM, BM_TINT_NONE),
    [PB_DPLANT_LOWER_BASE + 1] = CROSS1(CR_SPRITE_DOUBLE_PLANT_SYRINGA_BOTTOM,   BM_TINT_NONE),
    [PB_DPLANT_LOWER_BASE + 2] = CROSS1(CR_SPRITE_DOUBLE_PLANT_GRASS_BOTTOM,     BM_TINT_GRASS),
    [PB_DPLANT_LOWER_BASE + 3] = CROSS1(CR_SPRITE_DOUBLE_PLANT_FERN_BOTTOM,      BM_TINT_GRASS),
    [PB_DPLANT_LOWER_BASE + 4] = CROSS1(CR_SPRITE_DOUBLE_PLANT_ROSE_BOTTOM,      BM_TINT_NONE),
    [PB_DPLANT_LOWER_BASE + 5] = CROSS1(CR_SPRITE_DOUBLE_PLANT_PAEONIA_BOTTOM,   BM_TINT_NONE),
    /* Default upper (worldgen writes PB_DPLANT_UPPER without type). Mesh replaces
     * this via bm_dplant_upper after reading the lower half. */
    [PB_DPLANT_UPPER] = { 0, 0, CR_LAYER_CUTOUT, BM_KIND_DPLANT_SUNFLOWER_TOP,
        { {CR_SPRITE_DOUBLE_PLANT_SUNFLOWER_TOP, BM_TINT_NONE},
          {CR_SPRITE_DOUBLE_PLANT_SUNFLOWER_TOP, BM_TINT_NONE},
          {CR_SPRITE_DOUBLE_PLANT_SUNFLOWER_TOP, BM_TINT_NONE},
          {CR_SPRITE_DOUBLE_PLANT_SUNFLOWER_TOP, BM_TINT_NONE},
          {CR_SPRITE_DOUBLE_PLANT_SUNFLOWER_BACK, BM_TINT_NONE},
          {CR_SPRITE_DOUBLE_PLANT_SUNFLOWER_FRONT, BM_TINT_NONE} } },
    [CBX_DPLANT_UPPER_SYRINGA] = CROSS1(CR_SPRITE_DOUBLE_PLANT_SYRINGA_TOP, BM_TINT_NONE),
    [CBX_DPLANT_UPPER_GRASS]   = CROSS1(CR_SPRITE_DOUBLE_PLANT_GRASS_TOP,   BM_TINT_GRASS),
    [CBX_DPLANT_UPPER_FERN]    = CROSS1(CR_SPRITE_DOUBLE_PLANT_FERN_TOP,    BM_TINT_GRASS),
    [CBX_DPLANT_UPPER_ROSE]    = CROSS1(CR_SPRITE_DOUBLE_PLANT_ROSE_TOP,    BM_TINT_NONE),
    [CBX_DPLANT_UPPER_PAEONIA] = CROSS1(CR_SPRITE_DOUBLE_PLANT_PAEONIA_TOP, BM_TINT_NONE),
    /* cocoa: cross-ish cutout (stage0 texture stand-in uses brown mushroom). */
    [PB_COCOA] = CROSS1(CR_SPRITE_MUSHROOM_BROWN, BM_TINT_NONE),

    [PB_COBBLESTONE]       = CUBE6(CR_SPRITE_COBBLESTONE, BM_TINT_NONE),
    [PB_MOSSY_COBBLESTONE] = CUBE6(CR_SPRITE_COBBLESTONE_MOSSY, BM_TINT_NONE),
    [PB_MOB_SPAWNER] = { 0, 0, CR_LAYER_CUTOUT, BM_KIND_CUBE,
                         FULL6(CR_SPRITE_MOB_SPAWNER, BM_TINT_NONE) },
    [PB_BONE_BLOCK]  = { 0, 1, CR_LAYER_SOLID, BM_KIND_CUBE,
                         TBS(CR_SPRITE_BONE_BLOCK_TOP, CR_SPRITE_BONE_BLOCK_TOP,
                             CR_SPRITE_BONE_BLOCK_SIDE, BM_TINT_NONE, BM_TINT_NONE) },
    /* chest: inset body + front knob (TileEntityChestRenderer ModelChest
     * proportions; oak planks sprite stand-in for entity/chest/normal.png).
     * Lid animation is not meshed (TESR cut; TE lid_angle still ticks). */
    [PB_CHEST] = { 0, 0, CR_LAYER_SOLID, BM_KIND_CHEST,
                   FULL6(CR_SPRITE_PLANKS_OAK, BM_TINT_NONE) },
    [CBX_PLANKS] = CUBE6(CR_SPRITE_PLANKS_OAK, BM_TINT_NONE),

    [PB_PUMPKIN_BASE] = { 0, 1, CR_LAYER_SOLID, BM_KIND_CUBE,
                          TBS(CR_SPRITE_PUMPKIN_TOP, CR_SPRITE_PUMPKIN_TOP,
                              CR_SPRITE_PUMPKIN_SIDE, BM_TINT_NONE, BM_TINT_NONE) },
    [PB_PUMPKIN_BASE+1] = { 0, 1, CR_LAYER_SOLID, BM_KIND_CUBE,
                          TBS(CR_SPRITE_PUMPKIN_TOP, CR_SPRITE_PUMPKIN_TOP,
                              CR_SPRITE_PUMPKIN_SIDE, BM_TINT_NONE, BM_TINT_NONE) },
    [PB_PUMPKIN_BASE+2] = { 0, 1, CR_LAYER_SOLID, BM_KIND_CUBE,
                          TBS(CR_SPRITE_PUMPKIN_TOP, CR_SPRITE_PUMPKIN_TOP,
                              CR_SPRITE_PUMPKIN_SIDE, BM_TINT_NONE, BM_TINT_NONE) },
    [PB_PUMPKIN_BASE+3] = { 0, 1, CR_LAYER_SOLID, BM_KIND_CUBE,
                          TBS(CR_SPRITE_PUMPKIN_TOP, CR_SPRITE_PUMPKIN_TOP,
                              CR_SPRITE_PUMPKIN_SIDE, BM_TINT_NONE, BM_TINT_NONE) },

    /* vines: thin plane, foliage tint (all 4 facing ids share one model) */
    [PB_VINE_BASE]   = { 0, 0, CR_LAYER_CUTOUT, BM_KIND_VINE,
                         FULL6(CR_SPRITE_VINE, BM_TINT_FOLIAGE) },
    [PB_VINE_BASE+1] = { 0, 0, CR_LAYER_CUTOUT, BM_KIND_VINE,
                         FULL6(CR_SPRITE_VINE, BM_TINT_FOLIAGE) },
    [PB_VINE_BASE+2] = { 0, 0, CR_LAYER_CUTOUT, BM_KIND_VINE,
                         FULL6(CR_SPRITE_VINE, BM_TINT_FOLIAGE) },
    [PB_VINE_BASE+3] = { 0, 0, CR_LAYER_CUTOUT, BM_KIND_VINE,
                         FULL6(CR_SPRITE_VINE, BM_TINT_FOLIAGE) },

    [PB_BROWN_SHROOM_BLOCK] = CUBE6(CR_SPRITE_MUSHROOM_BLOCK_SKIN_BROWN, BM_TINT_NONE),
    [PB_RED_SHROOM_BLOCK]   = CUBE6(CR_SPRITE_MUSHROOM_BLOCK_SKIN_RED, BM_TINT_NONE),
    [PB_CACTUS] = { 0, 0, CR_LAYER_CUTOUT, BM_KIND_CACTUS,
                    TBS(CR_SPRITE_CACTUS_TOP, CR_SPRITE_CACTUS_BOTTOM,
                        CR_SPRITE_CACTUS_SIDE, BM_TINT_NONE, BM_TINT_NONE) },
    [PB_SANDSTONE_SLAB] = { 0, 0, CR_LAYER_SOLID, BM_KIND_SLAB_BOTTOM,
                            FULL6(CR_SPRITE_SANDSTONE_NORMAL, BM_TINT_NONE) },
    [PB_MELON] = { 0, 1, CR_LAYER_SOLID, BM_KIND_CUBE,
                   TBS(CR_SPRITE_MELON_TOP, CR_SPRITE_MELON_TOP,
                       CR_SPRITE_MELON_SIDE, BM_TINT_NONE, BM_TINT_NONE) },

    /* ---- synthetic ids for the non-cube mesh unit test ---- */
    [CBX_GLASS]  = { 0, 0, CR_LAYER_CUTOUT, BM_KIND_CUBE,
                     FULL6(CR_SPRITE_GLASS, BM_TINT_NONE) },
    [CBX_STAIRS] = { 0, 0, CR_LAYER_SOLID, BM_KIND_STAIRS,
                     FULL6(CR_SPRITE_PLANKS_OAK, BM_TINT_NONE) },
    [CBX_COBBLESTONE_STAIRS] = { 0, 0, CR_LAYER_SOLID, BM_KIND_STAIRS,
                                 FULL6(CR_SPRITE_COBBLESTONE, BM_TINT_NONE) },
    [CBX_SLAB]   = { 0, 0, CR_LAYER_SOLID, BM_KIND_SLAB_BOTTOM,
                     FULL6(CR_SPRITE_PLANKS_OAK, BM_TINT_NONE) },
    [CBX_FENCE]  = { 0, 0, CR_LAYER_SOLID, BM_KIND_FENCE,
                     FULL6(CR_SPRITE_PLANKS_OAK, BM_TINT_NONE) },

    /* ---- nether / end / portal (vanilla dumpblocks mapped into CBX_*) ---- */
    [CBX_NETHERRACK] = CUBE6(CR_SPRITE_NETHERRACK, BM_TINT_NONE),
    [CBX_NETHER_BRICK] = CUBE6(CR_SPRITE_NETHER_BRICK, BM_TINT_NONE),
    /* BlockSlime.getBlockLayer = TRANSLUCENT. Verified models/block/slime.json
     * (no cullface on either element):
     *   0: from [3,3,3] to [13,13,13], uv [3,3,13,13] all 6 faces
     *   1: from [0,0,0] to [16,16,16], uv [0,0,16,16] all 6 faces
     * mesh_mc emits both in that order (BmBlock is single-box). */
    [CBX_SLIME] = { 0, 1, CR_LAYER_TRANSLUCENT, BM_KIND_CUBE,
                    FULL6(CR_SPRITE_SLIME, BM_TINT_NONE) },
    [CBX_WEB] = CROSS1(CR_SPRITE_WEB, BM_TINT_NONE),
    [CBX_PACKED_ICE] = CUBE6(CR_SPRITE_ICE_PACKED, BM_TINT_NONE),
    [CBX_NETHER_BRICK_FENCE] = { 0, 0, CR_LAYER_SOLID, BM_KIND_FENCE,
                                 FULL6(CR_SPRITE_NETHER_BRICK, BM_TINT_NONE) },
    [CBX_COBBLESTONE_WALL] = { 0, 0, CR_LAYER_SOLID, BM_KIND_WALL,
                               FULL6(CR_SPRITE_COBBLESTONE, BM_TINT_NONE) },
    [CBX_PORTAL]     = { 0, 0, CR_LAYER_TRANSLUCENT, BM_KIND_PORTAL,
                         FULL6(CR_SPRITE_PORTAL, BM_TINT_NONE) },
    /* Active End portal (vanilla id 119): TileEntityEndPortalRenderer UP-face
     * only at y=0.75; end_portal.png multi-layer tints, not stone fallback. */
    [CBX_END_PORTAL] = { 0, 0, CR_LAYER_TRANSLUCENT, BM_KIND_END_PORTAL,
                         FULL6(CR_SPRITE_END_PORTAL, BM_TINT_NONE) },
    [CBX_TNT] = { 0, 1, CR_LAYER_SOLID, BM_KIND_CUBE,
                  TBS(CR_SPRITE_TNT_TOP, CR_SPRITE_TNT_BOTTOM,
                      CR_SPRITE_TNT_SIDE, BM_TINT_NONE, BM_TINT_NONE) },
    [CBX_STONE_SLAB_BOTTOM_BASE + 0] =
        SLAB6(BM_KIND_SLAB_BOTTOM, CR_SPRITE_STONE_SLAB_TOP,
             CR_SPRITE_STONE_SLAB_TOP, CR_SPRITE_STONE_SLAB_SIDE),
    [CBX_STONE_SLAB_BOTTOM_BASE + 1] =
        SLAB6(BM_KIND_SLAB_BOTTOM, CR_SPRITE_SANDSTONE_TOP,
             CR_SPRITE_SANDSTONE_BOTTOM, CR_SPRITE_SANDSTONE_NORMAL),
    [CBX_STONE_SLAB_BOTTOM_BASE + 2] =
        SLAB6(BM_KIND_SLAB_BOTTOM, CR_SPRITE_PLANKS_OAK,
             CR_SPRITE_PLANKS_OAK, CR_SPRITE_PLANKS_OAK),
    [CBX_STONE_SLAB_BOTTOM_BASE + 3] =
        SLAB6(BM_KIND_SLAB_BOTTOM, CR_SPRITE_COBBLESTONE,
             CR_SPRITE_COBBLESTONE, CR_SPRITE_COBBLESTONE),
    [CBX_STONE_SLAB_BOTTOM_BASE + 4] =
        SLAB6(BM_KIND_SLAB_BOTTOM, CR_SPRITE_BRICK,
             CR_SPRITE_BRICK, CR_SPRITE_BRICK),
    [CBX_STONE_SLAB_BOTTOM_BASE + 5] =
        SLAB6(BM_KIND_SLAB_BOTTOM, CR_SPRITE_STONEBRICK,
             CR_SPRITE_STONEBRICK, CR_SPRITE_STONEBRICK),
    [CBX_STONE_SLAB_BOTTOM_BASE + 6] =
        SLAB6(BM_KIND_SLAB_BOTTOM, CR_SPRITE_NETHER_BRICK,
             CR_SPRITE_NETHER_BRICK, CR_SPRITE_NETHER_BRICK),
    [CBX_STONE_SLAB_BOTTOM_BASE + 7] =
        SLAB6(BM_KIND_SLAB_BOTTOM, CR_SPRITE_QUARTZ_BLOCK_TOP,
             CR_SPRITE_QUARTZ_BLOCK_BOTTOM, CR_SPRITE_QUARTZ_BLOCK_SIDE),
    [CBX_STONE_SLAB_TOP_BASE + 0] =
        SLAB6(BM_KIND_SLAB_TOP, CR_SPRITE_STONE_SLAB_TOP,
             CR_SPRITE_STONE_SLAB_TOP, CR_SPRITE_STONE_SLAB_SIDE),
    [CBX_STONE_SLAB_TOP_BASE + 1] =
        SLAB6(BM_KIND_SLAB_TOP, CR_SPRITE_SANDSTONE_TOP,
             CR_SPRITE_SANDSTONE_BOTTOM, CR_SPRITE_SANDSTONE_NORMAL),
    [CBX_STONE_SLAB_TOP_BASE + 2] =
        SLAB6(BM_KIND_SLAB_TOP, CR_SPRITE_PLANKS_OAK,
             CR_SPRITE_PLANKS_OAK, CR_SPRITE_PLANKS_OAK),
    [CBX_STONE_SLAB_TOP_BASE + 3] =
        SLAB6(BM_KIND_SLAB_TOP, CR_SPRITE_COBBLESTONE,
             CR_SPRITE_COBBLESTONE, CR_SPRITE_COBBLESTONE),
    [CBX_STONE_SLAB_TOP_BASE + 4] =
        SLAB6(BM_KIND_SLAB_TOP, CR_SPRITE_BRICK,
             CR_SPRITE_BRICK, CR_SPRITE_BRICK),
    [CBX_STONE_SLAB_TOP_BASE + 5] =
        SLAB6(BM_KIND_SLAB_TOP, CR_SPRITE_STONEBRICK,
             CR_SPRITE_STONEBRICK, CR_SPRITE_STONEBRICK),
    [CBX_STONE_SLAB_TOP_BASE + 6] =
        SLAB6(BM_KIND_SLAB_TOP, CR_SPRITE_NETHER_BRICK,
             CR_SPRITE_NETHER_BRICK, CR_SPRITE_NETHER_BRICK),
    [CBX_STONE_SLAB_TOP_BASE + 7] =
        SLAB6(BM_KIND_SLAB_TOP, CR_SPRITE_QUARTZ_BLOCK_TOP,
             CR_SPRITE_QUARTZ_BLOCK_BOTTOM, CR_SPRITE_QUARTZ_BLOCK_SIDE),
    [CBX_END_STONE]  = CUBE6(CR_SPRITE_END_STONE, BM_TINT_NONE),
    /* fire face slots carry both animated sprite families: NORTH=layer0,
     * SOUTH=layer1. Geometry selection lives in mesh_mc.c BM_KIND_FIRE. */
    [CBX_FIRE]       = { 0, 0, CR_LAYER_CUTOUT, BM_KIND_FIRE,
                         { {CR_SPRITE_FIRE_LAYER_0, BM_TINT_NONE},
                           {CR_SPRITE_FIRE_LAYER_1, BM_TINT_NONE},
                           {CR_SPRITE_FIRE_LAYER_0, BM_TINT_NONE},
                           {CR_SPRITE_FIRE_LAYER_1, BM_TINT_NONE},
                           {CR_SPRITE_FIRE_LAYER_0, BM_TINT_NONE},
                           {CR_SPRITE_FIRE_LAYER_1, BM_TINT_NONE} } },
    [CBX_GLOWSTONE]  = CUBE6(CR_SPRITE_GLOWSTONE, BM_TINT_NONE),
    [CBX_SOUL_SAND]  = CUBE6(CR_SPRITE_SOUL_SAND, BM_TINT_NONE),
    /* Special face storage for BM_KIND_END_FRAME: DOWN=end stone, UP=top,
     * NORTH=base side, EAST=eye. The mesher expands the exact JSON boxes/UVs. */
    [CBX_END_FRAME]  = { 0, 0, CR_LAYER_SOLID, BM_KIND_END_FRAME,
                         { {CR_SPRITE_END_STONE, BM_TINT_NONE},
                           {CR_SPRITE_ENDFRAME_TOP, BM_TINT_NONE},
                           {CR_SPRITE_ENDFRAME_SIDE, BM_TINT_NONE},
                           {CR_SPRITE_ENDFRAME_SIDE, BM_TINT_NONE},
                           {CR_SPRITE_ENDFRAME_SIDE, BM_TINT_NONE},
                           {CR_SPRITE_ENDFRAME_EYE, BM_TINT_NONE} } },
    [CBX_QUARTZ_ORE] = CUBE6(CR_SPRITE_QUARTZ_ORE, BM_TINT_NONE),
    [CBX_BROWN_MUSHROOM] = CROSS1(CR_SPRITE_MUSHROOM_BROWN, BM_TINT_NONE),
    [CBX_RED_MUSHROOM]   = CROSS1(CR_SPRITE_MUSHROOM_RED, BM_TINT_NONE),
    [CBX_MAGMA]          = CUBE6(CR_SPRITE_MAGMA, BM_TINT_NONE),
    /* models/block/crafting_table.json: planks bottom, grid top, front/side. */
    [CBX_CRAFTING_TABLE] = { 0, 1, CR_LAYER_SOLID, BM_KIND_CUBE, {
        { CR_SPRITE_PLANKS_OAK,            BM_TINT_NONE }, /* DOWN  */
        { CR_SPRITE_CRAFTING_TABLE_TOP,    BM_TINT_NONE }, /* UP    */
        { CR_SPRITE_CRAFTING_TABLE_FRONT,  BM_TINT_NONE }, /* NORTH */
        { CR_SPRITE_CRAFTING_TABLE_SIDE,   BM_TINT_NONE }, /* SOUTH */
        { CR_SPRITE_CRAFTING_TABLE_FRONT,  BM_TINT_NONE }, /* WEST  */
        { CR_SPRITE_CRAFTING_TABLE_SIDE,   BM_TINT_NONE }  /* EAST  */
    }},

    /* ---- End City palette (real 1.11.2 templates and textures) ---- */
    [CBX_PURPUR_BLOCK] = CUBE6(CR_SPRITE_PURPUR_BLOCK, BM_TINT_NONE),
    [CBX_PURPUR_PILLAR_Y] = LOG6(CR_SPRITE_PURPUR_PILLAR,
                                  CR_SPRITE_PURPUR_PILLAR_TOP),
    [CBX_PURPUR_PILLAR_X] = { 0, 1, CR_LAYER_SOLID, BM_KIND_CUBE, {
        {CR_SPRITE_PURPUR_PILLAR, BM_TINT_NONE},
        {CR_SPRITE_PURPUR_PILLAR, BM_TINT_NONE},
        {CR_SPRITE_PURPUR_PILLAR, BM_TINT_NONE},
        {CR_SPRITE_PURPUR_PILLAR, BM_TINT_NONE},
        {CR_SPRITE_PURPUR_PILLAR_TOP, BM_TINT_NONE},
        {CR_SPRITE_PURPUR_PILLAR_TOP, BM_TINT_NONE}
    }},
    [CBX_PURPUR_PILLAR_Z] = { 0, 1, CR_LAYER_SOLID, BM_KIND_CUBE, {
        {CR_SPRITE_PURPUR_PILLAR, BM_TINT_NONE},
        {CR_SPRITE_PURPUR_PILLAR, BM_TINT_NONE},
        {CR_SPRITE_PURPUR_PILLAR_TOP, BM_TINT_NONE},
        {CR_SPRITE_PURPUR_PILLAR_TOP, BM_TINT_NONE},
        {CR_SPRITE_PURPUR_PILLAR, BM_TINT_NONE},
        {CR_SPRITE_PURPUR_PILLAR, BM_TINT_NONE}
    }},
    [CBX_PURPUR_STAIRS] = { 0, 0, CR_LAYER_SOLID, BM_KIND_STAIRS,
                            FULL6(CR_SPRITE_PURPUR_BLOCK, BM_TINT_NONE) },
    [CBX_PURPUR_SLAB_BOTTOM] = { 0, 0, CR_LAYER_SOLID, BM_KIND_SLAB_BOTTOM,
                                 FULL6(CR_SPRITE_PURPUR_BLOCK, BM_TINT_NONE) },
    [CBX_PURPUR_SLAB_TOP] = { 0, 0, CR_LAYER_SOLID, BM_KIND_SLAB_TOP,
                              FULL6(CR_SPRITE_PURPUR_BLOCK, BM_TINT_NONE) },
    [CBX_END_BRICKS] = CUBE6(CR_SPRITE_END_BRICKS, BM_TINT_NONE),
    [CBX_END_ROD] = { 0, 0, CR_LAYER_CUTOUT, BM_KIND_END_ROD,
                      FULL6(CR_SPRITE_END_ROD, BM_TINT_NONE) },
    [CBX_GLASS_MAGENTA] = { 0, 0, CR_LAYER_CUTOUT, BM_KIND_CUBE,
                            FULL6(CR_SPRITE_GLASS_MAGENTA, BM_TINT_NONE) },
    [CBX_CHORUS_PLANT] = { 0, 0, CR_LAYER_CUTOUT, BM_KIND_CHORUS_PLANT,
                           FULL6(CR_SPRITE_CHORUS_PLANT, BM_TINT_NONE) },
    [CBX_CHORUS_FLOWER] = { 0, 0, CR_LAYER_CUTOUT, BM_KIND_CHORUS_FLOWER, {
        {CR_SPRITE_CHORUS_PLANT, BM_TINT_NONE},
        {CR_SPRITE_CHORUS_FLOWER, BM_TINT_NONE},
        {CR_SPRITE_CHORUS_FLOWER, BM_TINT_NONE},
        {CR_SPRITE_CHORUS_FLOWER, BM_TINT_NONE},
        {CR_SPRITE_CHORUS_FLOWER, BM_TINT_NONE},
        {CR_SPRITE_CHORUS_FLOWER, BM_TINT_NONE}
    }},
    [CBX_CHORUS_FLOWER_DEAD] = { 0, 0, CR_LAYER_CUTOUT,
                                 BM_KIND_CHORUS_FLOWER, {
        {CR_SPRITE_CHORUS_PLANT, BM_TINT_NONE},
        {CR_SPRITE_CHORUS_FLOWER_DEAD, BM_TINT_NONE},
        {CR_SPRITE_CHORUS_FLOWER_DEAD, BM_TINT_NONE},
        {CR_SPRITE_CHORUS_FLOWER_DEAD, BM_TINT_NONE},
        {CR_SPRITE_CHORUS_FLOWER_DEAD, BM_TINT_NONE},
        {CR_SPRITE_CHORUS_FLOWER_DEAD, BM_TINT_NONE}
    }},
};

/* True when g_blocks[id] was explicitly modeled above (vs a zeroed gap). */
static int bm_is_modeled(int id)
{
    switch (id) {
    case CB_STONE: case CB_WATER: case CB_GRASS: case CB_DIRT: case CB_BEDROCK:
    case CB_GRAVEL: case CB_SAND: case CB_SANDSTONE: case CB_RED_SANDSTONE:
    case CB_ICE: case CB_LAVA: case CB_FLOWING_LAVA: case CB_FLOWING_WATER:
    case CB_WATER_LILY: case CB_MYCELIUM: case CB_SNOW_LAYER:
    case CB_HARDENED_CLAY: case CB_STAINED_HARDENED_CLAY: case CB_PODZOL:
    case CB_COARSE_DIRT:
    case PB_GRANITE: case PB_DIORITE: case PB_ANDESITE:
    case PB_COAL_ORE: case PB_IRON_ORE: case PB_GOLD_ORE: case PB_REDSTONE_ORE:
    case PB_DIAMOND_ORE: case PB_LAPIS_ORE: case PB_CLAY:
    case PB_LOG_OAK: case PB_LOG_BIRCH: case PB_LOG_SPRUCE:
    case PB_LOG_OAK_X: case PB_LOG_OAK_Z:
    case PB_LOG_DARKOAK: case PB_LOG_ACACIA: case PB_LOG_JUNGLE:
    case PB_LEAVES_OAK: case PB_LEAVES_BIRCH: case PB_LEAVES_SPRUCE:
    case PB_LEAVES_DARKOAK: case PB_LEAVES_ACACIA: case PB_LEAVES_JUNGLE:
    case PB_TALLGRASS: case PB_FERN: case PB_DEADBUSH: case PB_REEDS:
    case PB_BROWN_MUSHROOM: case PB_RED_MUSHROOM:
    case PB_YELLOW_FLOWER:
    case PB_COBBLESTONE: case PB_MOSSY_COBBLESTONE:
    case PB_MOB_SPAWNER: case PB_BONE_BLOCK: case PB_CHEST:
    case PB_EMERALD_ORE: case PB_MONSTER_EGG: case PB_OBSIDIAN:
    case PB_SANDSTONE_SMOOTH: case PB_SANDSTONE_CHISELED:
    case PB_SANDSTONE_STAIRS_E: case PB_SANDSTONE_STAIRS_W:
    case PB_SANDSTONE_STAIRS_S: case PB_SANDSTONE_STAIRS_N:
    case PB_STAINED_CLAY_ORANGE: case PB_STAINED_CLAY_BLUE:
    case PB_STONE_PRESSURE_PLATE: case PB_TNT:
    case PB_BROWN_SHROOM_BLOCK: case PB_RED_SHROOM_BLOCK:
    case PB_CACTUS: case PB_SANDSTONE_SLAB: case PB_MELON: case PB_COCOA:
    case CBX_GLASS: case CBX_STAIRS: case CBX_SLAB: case CBX_FENCE:
    case CBX_NETHERRACK: case CBX_PORTAL: case CBX_END_STONE: case CBX_FIRE:
    case CBX_GLOWSTONE: case CBX_SOUL_SAND: case CBX_END_FRAME: case CBX_QUARTZ_ORE:
    case CBX_BROWN_MUSHROOM: case CBX_RED_MUSHROOM: case CBX_MAGMA:
    case CBX_IRON_BARS: case CBX_GLASS_PANE: case CBX_TRAPDOOR:
    case CBX_TORCH: case CBX_CRAFTING_TABLE:
    case CBX_PLANKS: case CBX_GRANITE_SMOOTH: case CBX_DIORITE_SMOOTH:
    case CBX_ANDESITE_SMOOTH: case CBX_NETHER_BRICK:
    case CBX_SLIME: case CBX_WEB: case CBX_PACKED_ICE:
    case CBX_NETHER_BRICK_FENCE: case CBX_COBBLESTONE_WALL:
    case CBX_END_PORTAL: case CBX_RAIL: case CBX_TNT:
    case CBX_COBBLESTONE_STAIRS: case CBX_LADDER: case CBX_STONEBRICK:
    case CBX_PURPUR_BLOCK: case CBX_PURPUR_PILLAR_Y:
    case CBX_PURPUR_PILLAR_X: case CBX_PURPUR_PILLAR_Z:
    case CBX_PURPUR_STAIRS: case CBX_PURPUR_SLAB_BOTTOM:
    case CBX_PURPUR_SLAB_TOP: case CBX_END_BRICKS: case CBX_END_ROD:
    case CBX_GLASS_MAGENTA: case CBX_CHORUS_PLANT:
    case CBX_CHORUS_FLOWER: case CBX_CHORUS_FLOWER_DEAD:
        return 1;
    default:
        if (id >= CBX_STONE_SLAB_BOTTOM_BASE && id < CBX_MAX) return 1;
        if (id >= CB_STAINED_CLAY_BASE && id < CB_STAINED_CLAY_BASE + 16) return 1;
        if (id >= PB_RED_FLOWER_BASE && id <= PB_RED_FLOWER_BASE + 8) return 1;
        if (id >= PB_DPLANT_LOWER_BASE && id <= PB_DPLANT_UPPER) return 1;
        if (id >= CBX_DPLANT_UPPER_SYRINGA && id <= CBX_DPLANT_UPPER_PAEONIA) return 1;
        if (id >= PB_PUMPKIN_BASE && id < PB_PUMPKIN_BASE + 4) return 1;
        if (id >= PB_VINE_BASE && id < PB_VINE_BASE + 4) return 1;
        return 0;
    }
}

const BmBlock *bm_block(int cb_id)
{
    if (cb_id == CB_AIR) return &g_air;
    if (cb_id < 0 || cb_id >= CBX_MAX) return &g_stone;
    if (!bm_is_modeled(cb_id)) return &g_stone;
    /* Stained-clay / pumpkin / vine variants share one entry. Red flowers and
     * double plants keep distinct sprites. */
    if (cb_id >= CB_STAINED_CLAY_BASE && cb_id < CB_STAINED_CLAY_BASE + 16)
        return &g_blocks[CB_STAINED_CLAY_BASE];
    /* Double-plant lowers 60..65 each have their own sprite/tint - do not collapse. */
    if (cb_id > PB_PUMPKIN_BASE && cb_id < PB_PUMPKIN_BASE + 4)
        return &g_blocks[PB_PUMPKIN_BASE];
    if (cb_id > PB_VINE_BASE && cb_id < PB_VINE_BASE + 4)
        return &g_blocks[PB_VINE_BASE];
    return &g_blocks[cb_id];
}

/* Upper-half models per EnumPlantType (0 sunflower .. 5 paeonia). Worldgen and
 * the state->key map only store PB_DPLANT_UPPER; the mesher picks the row after
 * getActualState-style lookup of the lower half. */
static const BmBlock g_dplant_upper[6] = {
    { 0, 0, CR_LAYER_CUTOUT, BM_KIND_DPLANT_SUNFLOWER_TOP,
      { {CR_SPRITE_DOUBLE_PLANT_SUNFLOWER_TOP, BM_TINT_NONE},
        {CR_SPRITE_DOUBLE_PLANT_SUNFLOWER_TOP, BM_TINT_NONE},
        {CR_SPRITE_DOUBLE_PLANT_SUNFLOWER_TOP, BM_TINT_NONE},
        {CR_SPRITE_DOUBLE_PLANT_SUNFLOWER_TOP, BM_TINT_NONE},
        {CR_SPRITE_DOUBLE_PLANT_SUNFLOWER_BACK, BM_TINT_NONE},
        {CR_SPRITE_DOUBLE_PLANT_SUNFLOWER_FRONT, BM_TINT_NONE} } },
    CROSS1(CR_SPRITE_DOUBLE_PLANT_SYRINGA_TOP,   BM_TINT_NONE),
    CROSS1(CR_SPRITE_DOUBLE_PLANT_GRASS_TOP,     BM_TINT_GRASS),
    CROSS1(CR_SPRITE_DOUBLE_PLANT_FERN_TOP,      BM_TINT_GRASS),
    CROSS1(CR_SPRITE_DOUBLE_PLANT_ROSE_TOP,      BM_TINT_NONE),
    CROSS1(CR_SPRITE_DOUBLE_PLANT_PAEONIA_TOP,   BM_TINT_NONE),
};

const BmBlock *bm_dplant_upper(int type)
{
    if (type < 0 || type > 5) type = 3; /* BlockDoublePlant.getType fallback: FERN */
    return &g_dplant_upper[type];
}

int bm_dplant_sunflower_front_sprite(void) { return CR_SPRITE_DOUBLE_PLANT_SUNFLOWER_FRONT; }
int bm_dplant_sunflower_back_sprite(void)  { return CR_SPRITE_DOUBLE_PLANT_SUNFLOWER_BACK; }

int bm_grass_side_overlay_sprite(void) { return CR_SPRITE_GRASS_SIDE_OVERLAY; }

/* Model "particle" texture (ParticleDigging / BlockModelShapes.getTexture).
 * cube_all: #all (any face). cube_column / cube_bottom_top: #side. grass /
 * mycelium / podzol: dirt-like bottom. Fallback prefers DOWN when all faces
 * match, else NORTH (side). */
int bm_particle_sprite(int cb_id)
{
    const BmBlock *m = bm_block(cb_id);
    int down = m->face[BM_DOWN].sprite;
    int up   = m->face[BM_UP].sprite;
    int side = m->face[BM_NORTH].sprite;
    if (cb_id == CB_GRASS || cb_id == CB_MYCELIUM || cb_id == CB_PODZOL)
        return down; /* grass_normal.json particle = blocks/dirt */
    if (side != up || side != down)
        return side; /* column / bottom_top particle = #side */
    return down;
}

/* ParticleDigging color class: any face's BM_TINT_* (block-level colorMultiplier
 * is not face-specific), except Blocks.GRASS which never multiplies color. */
int bm_particle_tint(int cb_id)
{
    if (cb_id == CB_GRASS)
        return BM_TINT_NONE;
    const BmBlock *m = bm_block(cb_id);
    for (int f = 0; f < 6; ++f) {
        if (m->face[f].tint != BM_TINT_NONE)
            return m->face[f].tint;
    }
    return BM_TINT_NONE;
}

void bm_sprite_uv(int sprite, float *u0, float *v0, float *u1, float *v1)
{
    CrAtlasSprite s;
    if (sprite < 0 || sprite >= CR_ATLAS_SPRITE_COUNT)
        sprite = CR_SPRITE_STONE;
    s = CR_ATLAS_SPRITES[sprite];
    if (u0) *u0 = (float)s.x0 / (float)CR_ATLAS_W;
    if (v0) *v0 = (float)s.y0 / (float)CR_ATLAS_H;
    if (u1) *u1 = (float)s.x1 / (float)CR_ATLAS_W;
    if (v1) *v1 = (float)s.y1 / (float)CR_ATLAS_H;
}

/* ---- gamma-correct mip chain (box filter in linear space) ---- */
/* Supports atlas up to 256x256 (current worldgen sprite set). */

static float srgb_to_linear(unsigned char c)
{
    float s = (float)c / 255.0f;
    return s <= 0.04045f ? s / 12.92f : powf((s + 0.055f) / 1.055f, 2.4f);
}

static unsigned char linear_to_srgb(float l)
{
    float s;
    if (l <= 0.0f) l = 0.0f;
    if (l >= 1.0f) l = 1.0f;
    s = l <= 0.0031308f ? l * 12.92f
                        : 1.055f * powf(l, 1.0f / 2.4f) - 0.055f;
    s = s * 255.0f + 0.5f;
    if (s < 0.0f) s = 0.0f;
    if (s > 255.0f) s = 255.0f;
    return (unsigned char)s;
}

#define MIP_MAX 15
/* 256->128->64->32->16->8->4->2->1 */
static CrRgba g_mip1[128 * 128];
static CrRgba g_mip2[64 * 64];
static CrRgba g_mip3[32 * 32];
static CrRgba g_mip4[16 * 16];
static CrRgba g_mip5[8 * 8];
static CrRgba g_mip6[4 * 4];
static CrRgba g_mip7[2 * 2];
static CrRgba g_mip8[1 * 1];
static CrRgba *const g_mip_bufs[MIP_MAX] = {
    g_mip1, g_mip2, g_mip3, g_mip4, g_mip5, g_mip6, g_mip7, g_mip8,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL
};
static int g_mip_levels;
static int g_mipw[MIP_MAX], g_miph[MIP_MAX];
static int g_atlas_init;

#include "assets/water_frames.h"
#include "assets/portal_tex.h"

/* Runtime TextureMap mirror.  Animated sprites mutate level zero; the mip
 * chain is rebuilt lazily by bm_atlas() before the texture is consumed. */
static CrRgba g_atlas_live[CR_ATLAS_W * CR_ATLAS_H];
static int g_atlas_live_ready;
static int g_anim_frames[6] = {-1, -1, -1, -1, -1, -1};
static int g_portal_frame = -1;

static void atlas_live_init(void)
{
    if (g_atlas_live_ready) return;
    memcpy(g_atlas_live, CR_ATLAS_RGBA, sizeof g_atlas_live);
    g_atlas_live_ready = 1;
}

static void mip_down(const CrRgba *src, int sw, int sh, CrRgba *dst)
{
    int dw = sw / 2, dh = sh / 2, x, y;
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    for (y = 0; y < dh; ++y) {
        for (x = 0; x < dw; ++x) {
            int x0 = x * 2, y0 = y * 2;
            int x1 = (x0 + 1 < sw) ? x0 + 1 : x0;
            int y1 = (y0 + 1 < sh) ? y0 + 1 : y0;
            const CrRgba *p00 = &src[y0 * sw + x0];
            const CrRgba *p10 = &src[y0 * sw + x1];
            const CrRgba *p01 = &src[y1 * sw + x0];
            const CrRgba *p11 = &src[y1 * sw + x1];
            float r = 0.25f * (srgb_to_linear(p00->r) + srgb_to_linear(p10->r) +
                               srgb_to_linear(p01->r) + srgb_to_linear(p11->r));
            float g = 0.25f * (srgb_to_linear(p00->g) + srgb_to_linear(p10->g) +
                               srgb_to_linear(p01->g) + srgb_to_linear(p11->g));
            float b = 0.25f * (srgb_to_linear(p00->b) + srgb_to_linear(p10->b) +
                               srgb_to_linear(p01->b) + srgb_to_linear(p11->b));
            float a = 0.25f * ((float)p00->a + p10->a + p01->a + p11->a);
            CrRgba o;
            o.r = linear_to_srgb(r);
            o.g = linear_to_srgb(g);
            o.b = linear_to_srgb(b);
            o.a = (unsigned char)(a + 0.5f);
            dst[y * dw + x] = o;
        }
    }
}

static void atlas_init(void)
{
    const CrRgba *prev;
    int pw = CR_ATLAS_W, ph = CR_ATLAS_H, l = 0;
    if (g_atlas_init) return;
    atlas_live_init();
    prev = g_atlas_live;
    while ((pw > 1 || ph > 1) && l < MIP_MAX && g_mip_bufs[l]) {
        int nw = pw > 1 ? pw / 2 : 1;
        int nh = ph > 1 ? ph / 2 : 1;
        mip_down(prev, pw, ph, g_mip_bufs[l]);
        g_mipw[l] = nw;
        g_miph[l] = nh;
        prev = g_mip_bufs[l];
        pw = nw; ph = nh;
        ++l;
    }
    g_mip_levels = l;
    g_atlas_init = 1;
}

static void atlas_set_sprite(int sprite, const unsigned char *src)
{
    CrAtlasSprite s = CR_ATLAS_SPRITES[sprite];
    int w = s.x1 - s.x0, h = s.y1 - s.y0;
    for (int row = 0; row < h; ++row) {
        CrRgba *dst = g_atlas_live + (s.y0 + row) * CR_ATLAS_W + s.x0;
        const unsigned char *p = src + row * w * 4;
        for (int col = 0; col < w; ++col) {
            dst[col].r = p[col * 4 + 0];
            dst[col].g = p[col * 4 + 1];
            dst[col].b = p[col * 4 + 2];
            dst[col].a = p[col * 4 + 3];
        }
    }
    g_atlas_init = 0;
}

static int animation_frame(long long client_tick, int frametime,
                           int sequence_len, const unsigned char *sequence)
{
    long long logical;
    if (client_tick < 0) client_tick = 0;
    logical = (client_tick / frametime) % sequence_len;
    return sequence[(int)logical];
}

void bm_atlas_set_animation_tick(long long client_tick)
{
    const int frames[6] = {
        animation_frame(client_tick, CR_WATER_STILL_FRAMETIME,
                        CR_WATER_STILL_SEQUENCE_LEN, CR_WATER_STILL_SEQUENCE),
        animation_frame(client_tick, CR_WATER_FLOW_FRAMETIME,
                        CR_WATER_FLOW_SEQUENCE_LEN, CR_WATER_FLOW_SEQUENCE),
        animation_frame(client_tick, CR_LAVA_STILL_FRAMETIME,
                        CR_LAVA_STILL_SEQUENCE_LEN, CR_LAVA_STILL_SEQUENCE),
        animation_frame(client_tick, CR_LAVA_FLOW_FRAMETIME,
                        CR_LAVA_FLOW_SEQUENCE_LEN, CR_LAVA_FLOW_SEQUENCE),
        animation_frame(client_tick, CR_FIRE_LAYER_0_FRAMETIME,
                        CR_FIRE_LAYER_0_SEQUENCE_LEN, CR_FIRE_LAYER_0_SEQUENCE),
        animation_frame(client_tick, CR_FIRE_LAYER_1_FRAMETIME,
                        CR_FIRE_LAYER_1_SEQUENCE_LEN, CR_FIRE_LAYER_1_SEQUENCE),
    };
    const int sprites[6] = {
        CR_SPRITE_WATER_STILL, CR_SPRITE_WATER_FLOW,
        CR_SPRITE_LAVA_STILL, CR_SPRITE_LAVA_FLOW,
        CR_SPRITE_FIRE_LAYER_0, CR_SPRITE_FIRE_LAYER_1,
    };
    const unsigned char *rgba[6] = {
        CR_WATER_STILL_RGBA[frames[0]], CR_WATER_FLOW_RGBA[frames[1]],
        CR_LAVA_STILL_RGBA[frames[2]], CR_LAVA_FLOW_RGBA[frames[3]],
        CR_FIRE_LAYER_0_RGBA[frames[4]], CR_FIRE_LAYER_1_RGBA[frames[5]],
    };
    atlas_live_init();
    for (int i = 0; i < 6; ++i) {
        if (frames[i] == g_anim_frames[i]) continue;
        atlas_set_sprite(sprites[i], rgba[i]);
        g_anim_frames[i] = frames[i];
    }
}

void bm_atlas_set_animation_physical_zero(void)
{
    const int sprites[6] = {
        CR_SPRITE_WATER_STILL, CR_SPRITE_WATER_FLOW,
        CR_SPRITE_LAVA_STILL, CR_SPRITE_LAVA_FLOW,
        CR_SPRITE_FIRE_LAYER_0, CR_SPRITE_FIRE_LAYER_1,
    };
    const unsigned char *rgba[6] = {
        CR_WATER_STILL_RGBA[0], CR_WATER_FLOW_RGBA[0],
        CR_LAVA_STILL_RGBA[0], CR_LAVA_FLOW_RGBA[0],
        CR_FIRE_LAYER_0_RGBA[0], CR_FIRE_LAYER_1_RGBA[0],
    };
    atlas_live_init();
    for (int i = 0; i < 6; ++i) {
        if (g_anim_frames[i] == 0) continue;
        atlas_set_sprite(sprites[i], rgba[i]);
        g_anim_frames[i] = 0;
    }
}

void bm_atlas_set_portal_frame(int frame)
{
    if (frame < 0) return;
    frame %= CR_PORTAL_TEX_FRAMES;
    if (frame == g_portal_frame) return;
    atlas_live_init();
    CrAtlasSprite s = CR_ATLAS_SPRITES[CR_SPRITE_PORTAL];
    const unsigned char *src = CR_PORTAL_TEX[frame];
    for (int row = 0; row < CR_PORTAL_TEX_H; ++row) {
        CrRgba *dst = g_atlas_live + (s.y0 + row) * CR_ATLAS_W + s.x0;
        const unsigned char *p = src + row * CR_PORTAL_TEX_W * 4;
        for (int col = 0; col < CR_PORTAL_TEX_W; ++col) {
            dst[col].r = p[col * 4 + 0];
            dst[col].g = p[col * 4 + 1];
            dst[col].b = p[col * 4 + 2];
            dst[col].a = p[col * 4 + 3];
        }
    }
    g_portal_frame = frame;
    g_atlas_init = 0;
}

CrTexture bm_atlas(void)
{
    CrTexture t;
    int i;
    atlas_live_init();
    atlas_init();
    t.w = CR_ATLAS_W;
    t.h = CR_ATLAS_H;
    t.texels = g_atlas_live;
    t.tile = CR_ATLAS_TILE;
    t.mip_levels = g_mip_levels;
    for (i = 0; i < 15; ++i) {
        if (i < g_mip_levels) {
            t.mip[i] = g_mip_bufs[i];
            t.mipw[i] = g_mipw[i];
            t.miph[i] = g_miph[i];
        } else {
            t.mip[i] = NULL;
            t.mipw[i] = 0;
            t.miph[i] = 0;
        }
    }
    return t;
}
