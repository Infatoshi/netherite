/* tests/test_model_oracle.c - HARSH structural gate: every worldgen/populate
 * block id must have a BmBlock contract matching vanilla model family.
 * See VERIFY.md. */
#include "assets/blockmodels.h"
#include "assets/atlas_gen.h"

#include <stdio.h>

enum {
    CB_AIR = 0, CB_STONE = 1, CB_WATER = 2, CB_GRASS = 3, CB_DIRT = 4,
    CB_BEDROCK = 5, CB_GRAVEL = 6, CB_SAND = 7, CB_SANDSTONE = 8,
    CB_RED_SANDSTONE = 9, CB_ICE = 10, CB_LAVA = 11, CB_FLOWING_LAVA = 12,
    CB_FLOWING_WATER = 13, CB_WATER_LILY = 14, CB_MYCELIUM = 15,
    CB_SNOW_LAYER = 16, CB_HARDENED_CLAY = 17, CB_PODZOL = 19, CB_COARSE_DIRT = 20,
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
    CB_STAINED_CLAY_BASE = 120,
    CBX_NETHERRACK = 210, CBX_PORTAL = 211, CBX_END_STONE = 212,
    CBX_FIRE = 213, CBX_GLOWSTONE = 214, CBX_SOUL_SAND = 215,
    CBX_END_FRAME = 216, CBX_QUARTZ_ORE = 217,
    CBX_BROWN_MUSHROOM = 218, CBX_RED_MUSHROOM = 219, CBX_MAGMA = 220,
    CBX_IRON_BARS = 221, CBX_TORCH = 222,
    CBX_GRANITE_SMOOTH = 225, CBX_DIORITE_SMOOTH = 226,
    CBX_ANDESITE_SMOOTH = 227,
    CBX_DPLANT_UPPER_SYRINGA = 258,
    CBX_DPLANT_UPPER_GRASS = 259,
    CBX_DPLANT_UPPER_FERN = 260,
    CBX_DPLANT_UPPER_ROSE = 261,
    CBX_DPLANT_UPPER_PAEONIA = 262
};

enum { L_SOLID = 0, L_MIPPED = 1, L_CUTOUT = 2, L_TRANSLUCENT = 3 };

typedef struct {
    int id;
    const char *name;
    int kind;
    int is_full_cube;
    int layer;
    int tint_up;
} Expect;

static const Expect EXPECT[] = {
    { CB_STONE, "stone", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { CB_WATER, "water", BM_KIND_FLUID, 0, L_TRANSLUCENT, BM_TINT_WATER },
    { CB_FLOWING_WATER, "flowing_water", BM_KIND_FLUID, 0, L_TRANSLUCENT, BM_TINT_WATER },
    { CB_GRASS, "grass", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_GRASS },
    { CB_DIRT, "dirt", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { CB_BEDROCK, "bedrock", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { CB_GRAVEL, "gravel", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { CB_SAND, "sand", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { CB_SANDSTONE, "sandstone", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { CB_RED_SANDSTONE, "red_sandstone", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { CB_ICE, "ice", BM_KIND_CUBE, 1, L_TRANSLUCENT, BM_TINT_NONE },
    { CB_LAVA, "lava", BM_KIND_FLUID, 0, L_SOLID, BM_TINT_NONE },
    { CB_FLOWING_LAVA, "flowing_lava", BM_KIND_FLUID, 0, L_SOLID, BM_TINT_NONE },
    { CB_WATER_LILY, "waterlily", BM_KIND_LILY, 0, L_CUTOUT, BM_TINT_LILY },
    { CB_MYCELIUM, "mycelium", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { CB_SNOW_LAYER, "snow_layer", BM_KIND_SNOW_LAYER, 0, L_SOLID, BM_TINT_NONE },
    { CB_HARDENED_CLAY, "hardened_clay", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { CB_PODZOL, "podzol", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { CB_COARSE_DIRT, "coarse_dirt", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { CB_STAINED_CLAY_BASE, "stained_clay", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },

    { PB_GRANITE, "granite", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { PB_DIORITE, "diorite", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { PB_ANDESITE, "andesite", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { CBX_GRANITE_SMOOTH, "smooth_granite", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { CBX_DIORITE_SMOOTH, "smooth_diorite", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { CBX_ANDESITE_SMOOTH, "smooth_andesite", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { PB_COAL_ORE, "coal_ore", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { PB_IRON_ORE, "iron_ore", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { PB_GOLD_ORE, "gold_ore", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { PB_REDSTONE_ORE, "redstone_ore", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { PB_DIAMOND_ORE, "diamond_ore", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { PB_LAPIS_ORE, "lapis_ore", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { PB_CLAY, "clay", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { PB_EMERALD_ORE, "emerald_ore", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { PB_MONSTER_EGG, "monster_egg", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { PB_OBSIDIAN, "obsidian", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },

    { PB_LOG_OAK, "log_oak", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { PB_LOG_BIRCH, "log_birch", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { PB_LOG_SPRUCE, "log_spruce", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { PB_LOG_OAK_X, "log_oak_x", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { PB_LOG_OAK_Z, "log_oak_z", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { PB_LOG_DARKOAK, "log_darkoak", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { PB_LOG_ACACIA, "log_acacia", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { PB_LOG_JUNGLE, "log_jungle", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },

    /* Capture options pin fancyGraphics=false: BlockLeaves uses opaque SOLID
     * cubes, with fixed birch/pine tint specializations. */
    { PB_LEAVES_OAK, "leaves_oak", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_FOLIAGE },
    { PB_LEAVES_BIRCH, "leaves_birch", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_FOLIAGE_BIRCH },
    { PB_LEAVES_SPRUCE, "leaves_spruce", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_FOLIAGE_PINE },
    { PB_LEAVES_DARKOAK, "leaves_darkoak", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_FOLIAGE },
    { PB_LEAVES_ACACIA, "leaves_acacia", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_FOLIAGE },
    { PB_LEAVES_JUNGLE, "leaves_jungle", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_FOLIAGE },

    { PB_TALLGRASS, "tallgrass", BM_KIND_CROSS, 0, L_CUTOUT, BM_TINT_GRASS },
    { PB_FERN, "fern", BM_KIND_CROSS, 0, L_CUTOUT, BM_TINT_GRASS },
    { PB_DEADBUSH, "deadbush", BM_KIND_CROSS, 0, L_CUTOUT, BM_TINT_NONE },
    { PB_REEDS, "reeds", BM_KIND_CROSS, 0, L_CUTOUT, BM_TINT_FOLIAGE },
    { PB_BROWN_MUSHROOM, "brown_mushroom", BM_KIND_CROSS, 0, L_CUTOUT, BM_TINT_NONE },
    { PB_RED_MUSHROOM, "red_mushroom", BM_KIND_CROSS, 0, L_CUTOUT, BM_TINT_NONE },
    { PB_YELLOW_FLOWER, "dandelion", BM_KIND_CROSS, 0, L_CUTOUT, BM_TINT_NONE },
    { PB_RED_FLOWER_BASE, "poppy", BM_KIND_CROSS, 0, L_CUTOUT, BM_TINT_NONE },
    { PB_RED_FLOWER_BASE + 8, "oxeye_daisy", BM_KIND_CROSS, 0, L_CUTOUT, BM_TINT_NONE },
    { PB_DPLANT_LOWER_BASE + 0, "sunflower_lower", BM_KIND_CROSS, 0, L_CUTOUT, BM_TINT_NONE },
    { PB_DPLANT_LOWER_BASE + 1, "syringa_lower", BM_KIND_CROSS, 0, L_CUTOUT, BM_TINT_NONE },
    { PB_DPLANT_LOWER_BASE + 2, "double_grass_lower", BM_KIND_CROSS, 0, L_CUTOUT, BM_TINT_GRASS },
    { PB_DPLANT_LOWER_BASE + 3, "double_fern_lower", BM_KIND_CROSS, 0, L_CUTOUT, BM_TINT_GRASS },
    { PB_DPLANT_LOWER_BASE + 4, "double_rose_lower", BM_KIND_CROSS, 0, L_CUTOUT, BM_TINT_NONE },
    { PB_DPLANT_LOWER_BASE + 5, "paeonia_lower", BM_KIND_CROSS, 0, L_CUTOUT, BM_TINT_NONE },
    { 223, "crafting_table", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { PB_DPLANT_UPPER, "sunflower_upper", BM_KIND_DPLANT_SUNFLOWER_TOP, 0, L_CUTOUT, BM_TINT_NONE },
    { CBX_DPLANT_UPPER_SYRINGA, "syringa_upper", BM_KIND_CROSS, 0, L_CUTOUT, BM_TINT_NONE },
    { CBX_DPLANT_UPPER_GRASS, "double_grass_upper", BM_KIND_CROSS, 0, L_CUTOUT, BM_TINT_GRASS },
    { CBX_DPLANT_UPPER_FERN, "double_fern_upper", BM_KIND_CROSS, 0, L_CUTOUT, BM_TINT_GRASS },
    { CBX_DPLANT_UPPER_ROSE, "double_rose_upper", BM_KIND_CROSS, 0, L_CUTOUT, BM_TINT_NONE },
    { CBX_DPLANT_UPPER_PAEONIA, "paeonia_upper", BM_KIND_CROSS, 0, L_CUTOUT, BM_TINT_NONE },
    { PB_COCOA, "cocoa", BM_KIND_CROSS, 0, L_CUTOUT, BM_TINT_NONE },

    { PB_COBBLESTONE, "cobblestone", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { PB_MOSSY_COBBLESTONE, "mossy_cobble", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { PB_MOB_SPAWNER, "mob_spawner", BM_KIND_CUBE, 0, L_CUTOUT, BM_TINT_NONE },
    { PB_BONE_BLOCK, "bone_block", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { PB_CHEST, "chest", BM_KIND_CHEST, 0, L_SOLID, BM_TINT_NONE },
    { PB_PUMPKIN_BASE, "pumpkin", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { PB_VINE_BASE, "vine", BM_KIND_VINE, 0, L_CUTOUT, BM_TINT_FOLIAGE },
    { PB_BROWN_SHROOM_BLOCK, "brown_shroom_block", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { PB_RED_SHROOM_BLOCK, "red_shroom_block", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { PB_CACTUS, "cactus", BM_KIND_CACTUS, 0, L_CUTOUT, BM_TINT_NONE },
    { PB_SANDSTONE_SLAB, "sandstone_slab", BM_KIND_SLAB_BOTTOM, 0, L_SOLID, BM_TINT_NONE },
    { PB_MELON, "melon", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { CBX_NETHERRACK, "netherrack", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { CBX_PORTAL, "portal", BM_KIND_PORTAL, 0, L_TRANSLUCENT, BM_TINT_NONE },
    { CBX_END_STONE, "end_stone", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { CBX_FIRE, "fire", BM_KIND_FIRE, 0, L_CUTOUT, BM_TINT_NONE },
    { CBX_GLOWSTONE, "glowstone", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { CBX_SOUL_SAND, "soul_sand", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { CBX_END_FRAME, "end_frame", BM_KIND_END_FRAME, 0, L_SOLID, BM_TINT_NONE },
    { CBX_QUARTZ_ORE, "quartz_ore", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { CBX_BROWN_MUSHROOM, "nether_brown_mushroom", BM_KIND_CROSS, 0, L_CUTOUT, BM_TINT_NONE },
    { CBX_RED_MUSHROOM, "nether_red_mushroom", BM_KIND_CROSS, 0, L_CUTOUT, BM_TINT_NONE },
    { CBX_MAGMA, "magma", BM_KIND_CUBE, 1, L_SOLID, BM_TINT_NONE },
    { CBX_IRON_BARS, "iron_bars", BM_KIND_IRON_BARS, 0, L_MIPPED, BM_TINT_NONE },
    { CBX_TORCH, "torch", BM_KIND_TORCH, 0, L_CUTOUT, BM_TINT_NONE },
};

static int g_fail = 0;
#define CHECK(cond, ...) do { if (!(cond)) { \
    printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); g_fail = 1; } } while (0)

static const char *kind_name(int k) {
    switch (k) {
        case BM_KIND_CUBE: return "CUBE";
        case BM_KIND_CROSS: return "CROSS";
        case BM_KIND_SLAB_BOTTOM: return "SLAB_BOTTOM";
        case BM_KIND_SLAB_TOP: return "SLAB_TOP";
        case BM_KIND_STAIRS: return "STAIRS";
        case BM_KIND_FENCE: return "FENCE";
        case BM_KIND_FLUID: return "FLUID";
        case BM_KIND_LILY: return "LILY";
        case BM_KIND_SNOW_LAYER: return "SNOW_LAYER";
        case BM_KIND_VINE: return "VINE";
        case BM_KIND_CACTUS: return "CACTUS";
        case BM_KIND_FIRE: return "FIRE";
        case BM_KIND_PORTAL: return "PORTAL";
        case BM_KIND_END_FRAME: return "END_FRAME";
        case BM_KIND_IRON_BARS: return "IRON_BARS";
        case BM_KIND_TORCH: return "TORCH";
        case BM_KIND_DPLANT_SUNFLOWER_TOP: return "DPLANT_SUNFLOWER_TOP";
        default: return "?";
    }
}

int main(void) {
    int n = (int)(sizeof(EXPECT) / sizeof(EXPECT[0]));
    printf("model-oracle: checking %d worldgen block contracts vs BmBlock table\n", n);

    for (int i = 0; i < n; ++i) {
        const Expect *e = &EXPECT[i];
        const BmBlock *m = bm_block(e->id);
        CHECK(m != NULL, "%s: bm_block NULL", e->name);
        if (!m) continue;
        CHECK(m->kind == e->kind,
              "%s (id=%d): kind=%s want %s",
              e->name, e->id, kind_name(m->kind), kind_name(e->kind));
        CHECK(m->is_full_cube == e->is_full_cube,
              "%s: is_full_cube=%d want %d", e->name, m->is_full_cube, e->is_full_cube);
        CHECK(m->layer == e->layer,
              "%s: layer=%d want %d", e->name, m->layer, e->layer);
        CHECK(m->face[BM_UP].tint == e->tint_up,
              "%s: up tint=%d want %d", e->name, m->face[BM_UP].tint, e->tint_up);
    }

    /* Anti-regression: lily must not be full cube; snow must not be full cube. */
    CHECK(bm_block(CB_WATER_LILY)->kind != BM_KIND_CUBE,
          "waterlily must not be BM_KIND_CUBE");
    CHECK(bm_block(CB_WATER_LILY)->face[BM_UP].tint != BM_TINT_FOLIAGE,
          "waterlily must not use foliage tint");
    CHECK(bm_block(CB_SNOW_LAYER)->kind == BM_KIND_SNOW_LAYER,
          "snow_layer must be BM_KIND_SNOW_LAYER (thin y=0..2)");
    CHECK(bm_block(CB_SNOW_LAYER)->is_full_cube == 0,
          "snow_layer is_full_cube must be 0");

    /* Flower meta must not collapse to poppy; craft table not stone fallback. */
    {
        const BmBlock *poppy = bm_block(PB_RED_FLOWER_BASE);
        const BmBlock *daisy = bm_block(PB_RED_FLOWER_BASE + 8);
        const BmBlock *dand  = bm_block(PB_YELLOW_FLOWER);
        const BmBlock *tg    = bm_block(PB_TALLGRASS);
        const BmBlock *ct    = bm_block(223);
        CHECK(daisy->face[BM_UP].sprite != poppy->face[BM_UP].sprite,
              "oxeye daisy must not share poppy sprite");
        CHECK(tg->face[BM_UP].sprite != dand->face[BM_UP].sprite,
              "tallgrass must not share dandelion sprite");
        CHECK(ct->face[BM_UP].sprite != bm_block(CB_STONE)->face[BM_UP].sprite,
              "crafting table top must not be stone");
        CHECK(ct->face[BM_DOWN].sprite != ct->face[BM_UP].sprite,
              "crafting table bottom (planks) != top (grid)");
        CHECK(ct->face[BM_NORTH].sprite != ct->face[BM_SOUTH].sprite ||
              ct->face[BM_NORTH].sprite != ct->face[BM_UP].sprite,
              "crafting table has distinct face textures");
    }

    /* Every double-plant species keeps its jar-selected lower and upper sprite.
     * Grass/fern alone receive BlockColors' grass tint. */
    {
        const int lower_keys[6] = {60, 61, 62, 63, 64, 65};
        const int upper_keys[6] = {66, 258, 259, 260, 261, 262};
        const int lower_sprites[6] = {
            CR_SPRITE_DOUBLE_PLANT_SUNFLOWER_BOTTOM,
            CR_SPRITE_DOUBLE_PLANT_SYRINGA_BOTTOM,
            CR_SPRITE_DOUBLE_PLANT_GRASS_BOTTOM,
            CR_SPRITE_DOUBLE_PLANT_FERN_BOTTOM,
            CR_SPRITE_DOUBLE_PLANT_ROSE_BOTTOM,
            CR_SPRITE_DOUBLE_PLANT_PAEONIA_BOTTOM,
        };
        const int upper_sprites[6] = {
            CR_SPRITE_DOUBLE_PLANT_SUNFLOWER_TOP,
            CR_SPRITE_DOUBLE_PLANT_SYRINGA_TOP,
            CR_SPRITE_DOUBLE_PLANT_GRASS_TOP,
            CR_SPRITE_DOUBLE_PLANT_FERN_TOP,
            CR_SPRITE_DOUBLE_PLANT_ROSE_TOP,
            CR_SPRITE_DOUBLE_PLANT_PAEONIA_TOP,
        };
        for (int i = 0; i < 6; ++i) {
            CHECK(bm_block(lower_keys[i])->face[BM_NORTH].sprite == lower_sprites[i],
                  "double plant variant %d lower sprite", i);
            CHECK(bm_block(upper_keys[i])->face[BM_NORTH].sprite == upper_sprites[i],
                  "double plant variant %d upper sprite", i);
        }
        CHECK(bm_block(66)->face[BM_WEST].sprite == CR_SPRITE_DOUBLE_PLANT_SUNFLOWER_BACK,
              "sunflower upper back sprite");
        CHECK(bm_block(66)->face[BM_EAST].sprite == CR_SPRITE_DOUBLE_PLANT_SUNFLOWER_FRONT,
              "sunflower upper front sprite");
    }

    if (g_fail) {
        printf("MODEL_ORACLE FAIL\n");
        return 1;
    }
    printf("MODEL_ORACLE PASS (%d contracts)\n", n);
    return 0;
}
