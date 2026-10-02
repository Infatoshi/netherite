/* BiomeDecorator and the per-biome decorate overrides. The decorator counts
 * are the ones the biome constructors set (BiomeDecorator's defaults are trees
 * 0, flowers 2, grass 1, sand 1, sandPerChunk2 3, clay 1, everything else 0),
 * the tree, flower and grass choices are the biome objects the decorate
 * override passes to func_150513_a, and the generic mutated biomes run their
 * base's decorator on themselves (BiomeGenMutated.decorate) while the forest,
 * hills, snow, savanna and mesa mutations are their own subclasses and follow
 * the code. */
#include "decorator.h"
#include "env.h"

#include "blocks.h"
#include "features.h"
#include "features_lakes.h"
#include "features_springs.h"
#include "populate.h"

#include <string.h>

/* The negative checks test_populate plants (defined below, with the
 * decorator's entry point). */

/* the "leaves" material, by name so a regenerated blocks.h cannot move it */
static int leaves_i;

/* before main: the same for every environment */
__attribute__((constructor)) static void leaves_init(void)
{
    for (int i = 0; i < (int)(sizeof MATERIALS / sizeof MATERIALS[0]); ++i)
        if (strcmp(MATERIALS[i].name, "leaves") == 0) leaves_i = i;
}

static const struct material_def *leaves_mat(void)
{
    return &MATERIALS[leaves_i];
}

/* Block ids the decorator and its choice helpers name. */
enum {
    D_STONE = 1, D_GRASS = 2, D_DIRT = 3, D_SAND = 12, D_GRAVEL = 13, D_WATERLILY = 111,
    D_YELLOW_FLOWER = 37, D_RED_FLOWER = 38, D_BROWN_MUSHROOM = 39, D_RED_MUSHROOM = 40,
    D_TALLGRASS = 31, D_DEADBUSH = 32, D_PUMPKIN = 86, D_CACTUS = 81, D_REEDS = 83,
    D_DOUBLE_PLANT = 175, D_MONSTER_EGG = 97, D_EMERALD_ORE = 129, D_MOSSY = 48,
};

/* WorldGenDoublePlant's meta per call site. */
enum { PLANT_SUNFLOWER = 0, PLANT_FERN = 2, PLANT_ROSE = 3 };

/* ------------------------------------------------------- the per-biome table */

/* The decorator counts one biome object's constructor leaves, in field order:
 * trees, flowers, grass, deadBush, mushrooms, reeds, cacti, waterlily,
 * bigMushrooms, sand, sand2, clay. generateLakes is true for every overworld
 * biome. The generic mutated biomes reuse their base's decorator (these
 * counts), and the two frozen-ocean/river rows never decorate anything but the
 * defaults. */
struct deco { int trees, flowers, grass, deadbush, mushrooms, reeds, cacti, waterlily, bigmushrooms, sand, sand2, clay; };

static struct deco deco_of(int id);   /* below; the mutated ids reuse the base's counts */
static int base_of(int id);

/* WorldGenBigTree's heightLimit, one instance per biome object: the state
 * carries from tree to tree within the same biome */
#define bigtree_state (nw_env->decorator.bigtree_state)

static struct deco deco_of(int id)
{
    /* The mutations whose func_150566_k builds a class with its own decorator
     * object, so the counts are that class's constructor, not the base's. */
    switch (id)
    {
    case 132:           /* Flower Forest: BiomeGenForest(132, 1) */
        return (struct deco){6, 100, 1, 0, 0, 0, 0, 0, 0, 1, 3, 1};
    case 162:           /* Extreme Hills+ M: BiomeGenHills(162, false) */
        return (struct deco){0, 2, 1, 0, 0, 0, 0, 0, 0, 1, 3, 1};
    case 163: case 164: /* BiomeGenSavanna.Mutated's own decorator */
        return (struct deco){2, 2, 5, 0, 0, 0, 0, 0, 0, 1, 3, 1};
    default:
        break;
    }

    if (base_of(id) >= 0) return deco_of(base_of(id));

    switch (id)
    {
    case 1: case 129:   /* Plains, Sunflower Plains: the constructor counts; the
                         * decorate override sets flowers and grass per chunk */
        return (struct deco){-999, 4, 10, 0, 0, 0, 0, 0, 0, 1, 3, 1};
    case 2: case 17:    /* Desert, DesertHills */
        return (struct deco){-999, 2, 1, 2, 0, 50, 10, 0, 0, 1, 3, 1};
    case 4: case 18:    /* Forest, ForestHills */
        return (struct deco){10, 2, 2, 0, 0, 0, 0, 0, 0, 1, 3, 1};
    case 5: case 19:    /* Taiga, TaigaHills */
        return (struct deco){10, 2, 1, 0, 1, 0, 0, 0, 0, 1, 3, 1};
    case 6:             /* Swampland */
        return (struct deco){2, 1, 5, 1, 8, 10, 0, 4, 0, 0, 0, 1};
    case 14: case 15:   /* MushroomIsland, MushroomIslandShore */
        return (struct deco){-100, -100, -100, 0, 1, 0, 0, 0, 1, 1, 3, 1};
    case 16: case 26:   /* Beach, Cold Beach */
        return (struct deco){-999, 2, 1, 0, 0, 0, 0, 0, 0, 1, 3, 1};
    case 20: case 34:   /* Extreme Hills Edge, Extreme Hills+ */
        return (struct deco){3, 2, 1, 0, 0, 0, 0, 0, 0, 1, 3, 1};
    case 21: case 22:   /* Jungle, JungleHills */
        return (struct deco){50, 4, 25, 0, 0, 0, 0, 0, 0, 1, 3, 1};
    case 23:            /* JungleEdge */
        return (struct deco){2, 4, 25, 0, 0, 0, 0, 0, 0, 1, 3, 1};
    case 25:            /* Stone Beach */
        return (struct deco){-999, 2, 1, 0, 0, 0, 0, 0, 0, 1, 3, 1};
    case 27: case 28:   /* Birch Forest, Birch Forest Hills */
        return (struct deco){10, 2, 2, 0, 0, 0, 0, 0, 0, 1, 3, 1};
    case 29:            /* Roofed Forest: BiomeGenForest(29, 3), grassPerChunk 2 */
        return (struct deco){-999, 2, 2, 0, 0, 0, 0, 0, 0, 1, 3, 1};
    case 30: case 31:   /* Cold Taiga, Cold Taiga Hills */
        return (struct deco){10, 2, 1, 0, 1, 0, 0, 0, 0, 1, 3, 1};
    case 32: case 33:   /* Mega Taiga, Mega Taiga Hills */
        return (struct deco){10, 2, 7, 1, 3, 0, 0, 0, 0, 1, 3, 1};
    case 35: case 36:   /* Savanna, Savanna Plateau */
        return (struct deco){1, 4, 20, 0, 0, 0, 0, 0, 0, 1, 3, 1};
    case 37: case 39:   /* Mesa, Mesa Plateau */
        return (struct deco){-999, 0, 1, 20, 0, 3, 5, 0, 0, 1, 3, 1};
    case 38:            /* Mesa Plateau F: field_150620_aI sets trees 5 */
        return (struct deco){5, 0, 1, 20, 0, 3, 5, 0, 0, 1, 3, 1};
    default:
        return (struct deco){0, 2, 1, 0, 0, 0, 0, 0, 0, 1, 3, 1};
    }
}

/* The tree choice, func_150567_a of the object the tree loop asks: which kind
 * and, for the typed biomes, the type (the class's variant field) and the
 * jungle edge flag. The plain BiomeGenMutated mutations delegate to the base
 * object's own func_150567_a, so they take the base's kind. */
enum { TR_DEFAULT, TR_HILLS, TR_FOREST, TR_TAIGA, TR_TAIGA2, TR_JUNGLE, TR_SAVANNA, TR_MESA, TR_SWAMP,
       TR_MUTBIRCH };

/* The flower choice, func_150572_a of the biome the decorator passes on:
 * BiomeGenBase's default, BiomeGenPlains' (200) and BiomeGenForest's aF == 1
 * (48) noises, or BiomeGenSwamp's fixed blue orchid. */
enum { F_DEFAULT, F_PLAINS, F_SWAMP, F_FOREST };

/* getRandomWorldGenForGrass: the tall grass meta 2 draw of BiomeGenJungle and
 * BiomeGenTaiga, none for every other biome. */
enum { G_NONE, G_JUNGLE, G_TAIGA };

/* The decorate override of one class, around the shared body. */
enum { S_BODY, S_PLAINS, S_TAIGA, S_SNOW, S_FOREST, S_DESERT, S_JUNGLE, S_HILLS, S_SAVANNA };

/* The mutations whose func_150566_k builds a plain BiomeGenMutated (or leaves
 * func_150567_a alone): their func_150567_a runs on the base object, so the
 * tree kind is the base's and the WorldGenBigTree whose heightLimit carries is
 * the base's. 157 is BiomeGenForest's anonymous class, which calls the base's
 * own decorate. */
static int delegates_to_base(int id)
{
    switch (id)
    {
    case 130: case 133: case 134: case 149: case 151: case 155: case 156: case 157: case 158:
    case 163: case 164:
        return 1;
    default:
        return 0;
    }
}

static void choice_of(int id, int *kind, int *type, int *flag)
{
    *type = 0;
    *flag = 0;

    if (id == 155 || id == 156)   /* the birch mutations' tree pick */
    {
        *kind = TR_MUTBIRCH;
        return;
    }

    if (delegates_to_base(id)) id = base_of(id);

    switch (id)
    {
    case 3: case 20: case 34: case 131: case 162:
        *kind = TR_HILLS;
        return;
    case 4: case 18:
        *kind = TR_FOREST;
        return;
    case 27: case 28:
        *kind = TR_FOREST;
        *type = 2;
        return;
    case 29:
        *kind = TR_FOREST;
        *type = 3;
        return;
    case 132:
        *kind = TR_FOREST;
        *type = 1;
        return;
    case 5: case 19: case 30: case 31:
        *kind = TR_TAIGA;
        return;
    case 32: case 33:
        *kind = TR_TAIGA;
        *type = 1;
        return;
    case 160: case 161:
        *kind = TR_TAIGA;
        *type = 2;
        return;
    case 21: case 22:
        *kind = TR_JUNGLE;
        return;
    case 23:
        *kind = TR_JUNGLE;
        *flag = 1;
        return;
    case 35: case 36:
        *kind = TR_SAVANNA;
        return;
    case 37: case 38: case 39: case 165: case 166: case 167:
        *kind = TR_MESA;
        return;
    case 6:
        *kind = TR_SWAMP;
        return;
    case 12: case 13: case 140:
        *kind = TR_TAIGA2;   /* BiomeGenSnow.func_150567_a: a fresh taiga2 */
        return;
    default:
        *kind = TR_DEFAULT;
        return;
    }
}

/* BiomeGenMutated.func_150567_a: the tree choice delegates to the base object. */
static int base_of(int id)
{
    switch (id)
    {
    case 129: return 1;
    case 130: return 2;
    case 131: return 3;
    case 132: return 4;
    case 133: return 5;
    case 134: return 6;
    case 140: return 12;
    case 149: return 21;
    case 151: return 23;
    case 155: return 27;
    case 156: return 28;
    case 157: return 29;
    case 158: return 30;
    case 160: case 161: return 32;
    case 162: return 34;
    case 163: return 35;
    case 164: return 36;
    case 165: return 37;
    case 166: return 38;
    case 167: return 39;
    default: return -1;
    }
}

/* The biome object whose func_150567_a the tree loop runs, and so whose single
 * WorldGenBigTree carries heightLimit between calls: the object itself, or the
 * base for the mutations that delegate. */
static int slot_of(int id)
{
    return delegates_to_base(id) ? base_of(id) : id;
}

/* One biome object's decorate: the override around the shared body and the
 * fields its pre steps read. */
struct bshape
{
    int kind;   /* S_* */
    int aF;     /* BiomeGenForest.field_150632_aF: 3 the 4x4 patch, 1 the +2 flower loop */
    int aC;     /* the plains' sunflower patch, the snow's ice spikes */
    int mega;   /* BiomeGenTaiga.field_150644_aH 1 or 2: the block blob pre */
};

static struct bshape shape_of(int id)
{
    struct bshape s = {S_BODY, 0, 0, 0};

    switch (id)
    {
    case 1: case 129:                             /* BiomeGenPlains.decorate */
        s.kind = S_PLAINS;
        s.aC = id == 129;
        return s;
    case 2: case 17:                              /* BiomeGenDesert.decorate */
        s.kind = S_DESERT;
        return s;
    case 3: case 20: case 34: case 131: case 162: /* BiomeGenHills.decorate */
        s.kind = S_HILLS;
        return s;
    case 4: case 18:                              /* BiomeGenForest, aF 0 */
        s.kind = S_FOREST;
        return s;
    case 27: case 28:                             /* the birch forests, aF 2 */
        s.kind = S_FOREST;
        s.aF = 2;
        return s;
    case 29: case 157:                            /* the roofed forest, aF 3,
                                                   * and its mutation, which
                                                   * runs the base's decorate */
        s.kind = S_FOREST;
        s.aF = 3;
        return s;
    case 132:                                     /* Flower Forest, aF 1 */
        s.kind = S_FOREST;
        s.aF = 1;
        return s;
    case 5: case 19: case 30: case 31:            /* BiomeGenTaiga, aH 0 */
        s.kind = S_TAIGA;
        return s;
    case 32: case 33: case 160: case 161:         /* the mega types, aH 1, 2 */
        s.kind = S_TAIGA;
        s.mega = 1;
        return s;
    case 140:                                     /* Ice Plains Spikes */
        s.kind = S_SNOW;
        s.aC = 1;
        return s;
    case 21: case 22: case 23:                    /* BiomeGenJungle.decorate */
        s.kind = S_JUNGLE;
        return s;
    case 35: case 36:                             /* BiomeGenSavanna.decorate */
        s.kind = S_SAVANNA;
        return s;
    default:
        return s;
    }
}

/* ------------------------------------------------------------ world helpers */

static int get_height(struct populate *p, int x, int z)
{
    return world_get_height_value(&p->world, x, z);
}

/* World.getTopSolidOrLiquidBlock: the y above the top block that blocks
 * movement and is not a leaf, -1 when the column has none. */
static int get_top_solid(struct populate *p, int x, int z)
{
    struct chunk *c = world_load_chunk(&p->world, x >> 4, z >> 4);
    int lx = x & 15, lz = z & 15;
    int y = populate_top_filled_segment(c) + 15;

    for (; y > 0; --y)
    {
        const struct material_def *m = &MATERIALS[BLOCKS[(c->mask >> (y >> 4)) & 1 ? chunk_cell_id(c, lx << 12 | lz << 8 | y) : 0].material];

        if (m->blocks_movement && m != leaves_mat()) return y + 1;
    }

    return -1;
}

/* World.isAirBlock. */
static int is_air(struct populate *p, int x, int y, int z)
{
    return (world_get_block(&p->world, x, y, z) & 4095) == 0;
}

/* ---------------------------------------------------------------- the choices */

/* The tree loop places one tree at (x, y, z) and runs func_150524_b after a
 * grow; every generator's own func_150524_b is empty but the mega pine's,
 * which feature_megapine folds into its generate. */
static int place_tree(struct populate *p, int id, int x, int y, int z)
{
    struct world *w = &p->world;
    jrand *r = &p->rand;
    int kind, type, flag;

    choice_of(id, &kind, &type, &flag);

    switch (kind)
    {
    case TR_HILLS:
        if (jr_int_n(r, 3) > 0) return feature_taiga2(w, r, x, y, z, 0, 0);
        return jr_int_n(r, 10) == 0 ? feature_bigtree(w, r, x, y, z, 0, 0)
                                    : feature_trees(w, r, x, y, z, 0, 4);
    case TR_FOREST:
        if (type == 3 && jr_int_n(r, 3) > 0) return feature_canopy(w, r, x, y, z, 0, 0);
        if (type != 2 && jr_int_n(r, 5) != 0) return feature_trees(w, r, x, y, z, 0, 4);
        return feature_forest(w, r, x, y, z, 0, 0);
    case TR_TAIGA:
        if ((type == 1 || type == 2) && jr_int_n(r, 3) == 0)
            return (type != 2 && jr_int_n(r, 13) != 0) ? feature_megapine(w, r, x, y, z, 0, 0)
                                                       : feature_megapine(w, r, x, y, z, 0, 1);
        return jr_int_n(r, 3) == 0 ? feature_taiga1(w, r, x, y, z, 0, 0)
                                   : feature_taiga2(w, r, x, y, z, 0, 0);
    case TR_JUNGLE:
        if (jr_int_n(r, 10) == 0) return feature_bigtree(w, r, x, y, z, 0, 0);
        if (jr_int_n(r, 2) == 0) return feature_shrub(w, r, x, y, z, 3, 0);
        if (!flag && jr_int_n(r, 3) == 0) return feature_megajungle(w, r, x, y, z, 0, 0);
        return feature_jungle_tree(w, r, x, y, z, 0, 4 + jr_int_n(r, 7));
    case TR_SAVANNA:
        return jr_int_n(r, 5) > 0 ? feature_savanna(w, r, x, y, z, 0, 0)
                                  : feature_trees(w, r, x, y, z, 0, 4);
    case TR_MESA:
        return feature_trees(w, r, x, y, z, 0, 4);
    case TR_SWAMP:
        return feature_swamp(w, r, x, y, z, 0, 0);
    case TR_MUTBIRCH:
        /* BiomeGenForest's birch mutations: nextBoolean between the vined
         * forest and the plain birch */
        if (nw_env->cfg.decorator_negative_muttree) return feature_forest(w, r, x, y, z, 0, 1);

        return jr_next(r, 1) != 0 ? feature_forest(w, r, x, y, z, 0, 1)
                                  : feature_forest(w, r, x, y, z, 0, 0);
    case TR_TAIGA2:
        return feature_taiga2(w, r, x, y, z, 0, 0);
    default:
        return jr_int_n(r, 10) == 0 ? feature_bigtree(w, r, x, y, z, 0, 0)
                                    : feature_trees(w, r, x, y, z, 0, 4);
    }
}

/* The flower choice, func_150572_a on the choice object: which block and meta
 * field_150514_p carries for this draw. kind 2 is the swamp's fixed blueOrchid
 * (red flower meta 1), kind 1 the plains /200 noise, kind 3 the flower
 * forest's /48 noise. */
static void flower_choice(struct populate *p, int id, int x, int z, int *block, int *meta)
{
    jrand *r = &p->rand;

    switch (id)
    {
    case 1: case 129:
    {
        double v = perlin_point(&p->flower_noise, (double)x / 200.0, (double)z / 200.0);
        int n;

        if (v < -0.8)
        {
            n = jr_int_n(r, 4);
            *block = D_RED_FLOWER;
            *meta = 4 + n;
        }
        else if (jr_int_n(r, 3) > 0)
        {
            n = jr_int_n(r, 3);
            *block = D_RED_FLOWER;
            *meta = n == 0 ? 0 : (n == 1 ? 3 : 8);
        }
        else
        {
            *block = D_YELLOW_FLOWER;
            *meta = 0;
        }

        return;
    }
    case 132:
    {
        /* clamp_double((1.0 + noise) / 2.0, 0.0, 0.9999) */
        double v = (1.0 + perlin_point(&p->flower_noise, (double)x / 48.0, (double)z / 48.0)) / 2.0;
        double cl = v < 0.0 ? 0.0 : (v > 0.9999 ? 0.9999 : v);
        int n = (int)(cl * (double)9);   /* field_149859_a.length */

        if (n == 1) n = 0;

        *block = D_RED_FLOWER;
        *meta = n;
        return;
    }
    case 6:
        *block = D_RED_FLOWER;
        *meta = 1;
        return;
    default:
        if (jr_int_n(r, 3) > 0)
        {
            *block = D_YELLOW_FLOWER;
            *meta = 0;
        }
        else
        {
            *block = D_RED_FLOWER;
            *meta = 0;
        }
        return;
    }
}

/* getRandomWorldGenForGrass on the biome object the decorator passes on: the
 * jungle draws nextInt(4) == 0 for the meta 2 tall grass, the taiga (every
 * object of the class, the mega types included) draws nextInt(5) > 0 for it;
 * every other biome returns the meta 1 one with no draw. */
static int grass_choice(struct populate *p, int id)
{
    if (id == 21 || id == 22 || id == 23) return jr_int_n(&p->rand, 4) == 0 ? 1 : 0;

    if (id == 5 || id == 19 || id == 30 || id == 31 || id == 32 || id == 33 || id == 160 || id == 161)
        return jr_int_n(&p->rand, 5) > 0 ? 1 : 0;

    return 0;
}

static void fire_stage(struct populate *p, int stage)
{
    if (p->stage.fn != NULL) p->stage.fn(p->stage.ctx, stage, (uint64_t)p->rand.seed);
}

/* ------------------------------------------------------------ the decorator body */

/* One decorated position: chunk_X + nextInt(16) + 8. */
static int spot_x(struct populate *p, int cx) { return cx * 16 + jr_int_n(&p->rand, 16) + 8; }

static int spot_z(struct populate *p, int cz) { return cz * 16 + jr_int_n(&p->rand, 16) + 8; }

/* genStandardOre1(count, gen, y0, y1): the chunk's own columns, y in [y0, y1). */
static void ore_band(struct populate *p, int cx, int cz, int count, int block, int count_size, int y0, int y1)
{
    struct world *w = &p->world;
    jrand *r = &p->rand;

    for (int i = 0; i < count; ++i)
    {
        int x = cx * 16 + jr_int_n(r, 16);
        int y = jr_int_n(r, y1 - y0) + y0;
        int z = cz * 16 + jr_int_n(r, 16);
        feature_minable(w, r, x, y, z, block, count_size);
    }
}

/* genStandardOre2(1, lapis, 16, 16): y is two draws of 16. */
static void ore_lapis(struct populate *p, int cx, int cz)
{
    int x = cx * 16 + jr_int_n(&p->rand, 16);
    int y = jr_int_n(&p->rand, 16) + jr_int_n(&p->rand, 16);
    int z = cz * 16 + jr_int_n(&p->rand, 16);
    feature_minable(&p->world, &p->rand, x, y, z, D_MONSTER_EGG == 0 ? 21 : 21, 6);
}

/* func_150513_a over one chunk: the decorator steps in vanilla order, stage
 * hook at each marker. biome is the object the decorator's biome argument
 * carries, which the tree, flower and grass choices read; counts may have been
 * overridden by the decorate override (plains). */
static void decorator_body(struct populate *p, int biome, struct deco d, int cx, int cz)
{
    struct world *w = &p->world;
    jrand *r = &p->rand;
    const struct feature *rows;
    int n;

    /* ores: dirt, gravel, coal, iron, gold, redstone, diamond, lapis */
    ore_band(p, cx, cz, 20, D_DIRT, 32, 0, 256);
    ore_band(p, cx, cz, 10, 13, 32, 0, 256);
    ore_band(p, cx, cz, 20, 16, 16, 0, 128);
    ore_band(p, cx, cz, 20, 15, 8, 0, 64);
    ore_band(p, cx, cz, 2, 14, 8, 0, 32);
    ore_band(p, cx, cz, 8, 73, 7, 0, 16);
    ore_band(p, cx, cz, 1, 56, 7, 0, 16);
    ore_lapis(p, cx, cz);
    fire_stage(p, POP_ORES);
    fire_stage(p, POP_SAND);

    for (int i = 0; i < d.sand2; ++i)
    {
        int x = spot_x(p, cx), z = spot_z(p, cz);
        rows = features_plants_for("sand", &n);
        rows->generate(w, r, x, get_top_solid(p, x, z), z, rows->block, rows->count);
    }
    fire_stage(p, POP_CLAY);

    for (int i = 0; i < d.clay; ++i)
    {
        int x = spot_x(p, cx), z = spot_z(p, cz);
        rows = features_plants_for("clay", &n);
        rows->generate(w, r, x, get_top_solid(p, x, z), z, rows->block, rows->count);
    }
    fire_stage(p, POP_GRAVEL);

    for (int i = 0; i < d.sand; ++i)
    {
        int x = spot_x(p, cx), z = spot_z(p, cz);
        rows = features_plants_for("sand", &n);
        rows[1].generate(w, r, x, get_top_solid(p, x, z), z, rows[1].block, rows[1].count);
    }
    int trees = d.trees;

    fire_stage(p, POP_TREES);

    if (jr_int_n(r, 10) == 0) ++trees;

    /* WorldGenBigTree carries its heightLimit on the instance, and every biome
     * object owns one, so the state is per biome */
    int slot = slot_of(biome);

    feature_bigtree_state_set(bigtree_state[slot]);

    for (int i = 0; i < trees; ++i)
    {
        int x = spot_x(p, cx), z = spot_z(p, cz);
        int y = get_height(p, x, z);
        place_tree(p, biome, x, y, z);
    }

    bigtree_state[slot] = feature_bigtree_state_get();
    fire_stage(p, POP_BIGMUSHROOMS);

    for (int i = 0; i < d.bigmushrooms; ++i)
    {
        int x = spot_x(p, cx), z = spot_z(p, cz);
        rows = features_lakes_for("bigmushroom", &n);
        rows[2].generate(w, r, x, get_height(p, x, z), z, rows[2].block, rows[2].count);
    }
    fire_stage(p, POP_FLOWERS);

    for (int i = 0; i < d.flowers; ++i)
    {
        int x = spot_x(p, cx), z = spot_z(p, cz);
        int y = jr_int_n(r, get_height(p, x, z) + 32);
        int block, meta;

        flower_choice(p, biome, x, z, &block, &meta);

        rows = features_plants_for("flowers", &n);
        rows[0].generate(w, r, x, y, z, block, meta);
    }
    fire_stage(p, POP_GRASS);

    for (int i = 0; i < d.grass; ++i)
    {
        int x = spot_x(p, cx), z = spot_z(p, cz);
        int y = jr_int_n(r, get_height(p, x, z) * 2);
        rows = features_plants_for("tallgrass", &n);
        int g = grass_choice(p, biome);
        rows[g].generate(w, r, x, y, z, rows[g].block, rows[g].count);
    }
    fire_stage(p, POP_DEADBUSH);

    for (int i = 0; i < d.deadbush; ++i)
    {
        int x = spot_x(p, cx), z = spot_z(p, cz);
        int y = jr_int_n(r, get_height(p, x, z) * 2);
        rows = features_plants_for("deadbush", &n);
        rows->generate(w, r, x, y, z, rows->block, rows->count);
    }
    fire_stage(p, POP_WATERLILY);

    for (int i = 0; i < d.waterlily; ++i)
    {
        int x = spot_x(p, cx), z = spot_z(p, cz);
        int y = jr_int_n(r, get_height(p, x, z) * 2);

        while (y > 0 && is_air(p, x, y - 1, z)) --y;

        rows = features_plants_for("waterlily", &n);
        rows->generate(w, r, x, y, z, rows->block, rows->count);
    }
    fire_stage(p, POP_MUSHROOMS);

    for (int i = 0; i < d.mushrooms; ++i)
    {
        if (jr_int_n(r, 4) == 0)
        {
            int x = spot_x(p, cx), z = spot_z(p, cz);
            int y = get_height(p, x, z);
            rows = features_plants_for("flowers", &n);
            rows[3].generate(w, r, x, y, z, rows[3].block, rows[3].count);
        }

        if (jr_int_n(r, 8) == 0)
        {
            int x = spot_x(p, cx), z = spot_z(p, cz);
            int y = jr_int_n(r, get_height(p, x, z) * 2);
            rows = features_plants_for("flowers", &n);
            rows[4].generate(w, r, x, y, z, rows[4].block, rows[4].count);
        }
    }
    if (jr_int_n(r, 4) == 0)
    {
        int x = spot_x(p, cx), z = spot_z(p, cz);
        int y = jr_int_n(r, get_height(p, x, z) * 2);
        rows = features_plants_for("flowers", &n);
        rows[3].generate(w, r, x, y, z, rows[3].block, rows[3].count);
    }
    fire_stage(p, POP_MUSHROOM1);

    if (jr_int_n(r, 8) == 0)
    {
        int x = spot_x(p, cx), z = spot_z(p, cz);
        int y = jr_int_n(r, get_height(p, x, z) * 2);
        rows = features_plants_for("flowers", &n);
        rows[4].generate(w, r, x, y, z, rows[4].block, rows[4].count);
    }
    fire_stage(p, POP_MUSHROOM2);
    fire_stage(p, POP_REEDS);

    for (int i = 0; i < d.reeds; ++i)
    {
        int x = spot_x(p, cx), z = spot_z(p, cz);
        int y = jr_int_n(r, get_height(p, x, z) * 2);
        rows = features_plants_for("reed", &n);
        rows->generate(w, r, x, y, z, rows->block, rows->count);
    }
    fire_stage(p, POP_REEDS10);

    for (int i = 0; i < 10; ++i)
    {
        int x = spot_x(p, cx), z = spot_z(p, cz);
        int y = jr_int_n(r, get_height(p, x, z) * 2);
        rows = features_plants_for("reed", &n);
        rows->generate(w, r, x, y, z, rows->block, rows->count);
    }
    fire_stage(p, POP_PUMPKIN);

    if (jr_int_n(r, 32) == 0)
    {
        int x = spot_x(p, cx), z = spot_z(p, cz);
        int y = jr_int_n(r, get_height(p, x, z) * 2);
        rows = features_plants_for("pumpkin", &n);
        rows->generate(w, r, x, y, z, rows->block, rows->count);
    }
    fire_stage(p, POP_CACTUS);

    for (int i = 0; i < d.cacti; ++i)
    {
        int x = spot_x(p, cx), z = spot_z(p, cz);
        int y = jr_int_n(r, get_height(p, x, z) * 2);
        rows = features_plants_for("cactus", &n);
        rows->generate(w, r, x, y, z, rows->block, rows->count);
    }
    fire_stage(p, POP_SPRINGS);

    for (int i = 0; i < 50; ++i)
    {
        int x = spot_x(p, cx);
        int y = jr_int_n(r, jr_int_n(r, 248) + 8);
        int z = spot_z(p, cz);
        rows = features_springs_for("springwater", &n);
        rows->generate(w, r, x, y, z, rows->block, rows->count);
    }
    fire_stage(p, POP_SPRINGSLAVA);

    for (int i = 0; i < 20; ++i)
    {
        int x = spot_x(p, cx);
        int y = jr_int_n(r, jr_int_n(r, jr_int_n(r, 240) + 8) + 8);
        int z = spot_z(p, cz);
        rows = features_springs_for("springlava", &n);
        rows->generate(w, r, x, y, z, rows->block, rows->count);
    }
}
/* --------------------------------------------------------- WorldGenBlockBlob */

/* WorldGenBlockBlob.generate with (mossy_cobblestone, 0), the mega taiga
 * decorator's field_150643_aG. Every write is a flag-4 setBlock. */
static int place_block_blob(struct populate *p, int x, int y, int z)
{
    struct world *w = &p->world;
    jrand *r = &p->rand;

    for (;;)
    {
        if (y > 3)
        {
            int hold = 0;

            if (!is_air(p, x, y - 1, z))
            {
                int below = world_get_block(w, x, y - 1, z) & 4095;

                if (below == D_GRASS || below == D_DIRT || below == 1 /* stone */) hold = 1;
            }

            if (hold) break;

            --y;
            continue;
        }

        return 0;
    }

    int var18 = 0;

    for (int i = 0; var18 >= 0 && i < 3; ++i)
    {
        /* the three radius draws are x, y, z in that order */
        int rx = var18 + jr_int_n(r, 2);
        int ry = var18 + jr_int_n(r, 2);
        int rz = var18 + jr_int_n(r, 2);
        float rad = (float)(rx + ry + rz) * 0.333F + 0.5F;

        for (int bx = x - rx; bx <= x + rx; ++bx)
        {
            for (int bz = z - rz; bz <= z + rz; ++bz)
            {
                for (int by = y - ry; by <= y + ry; ++by)
                {
                    float fx = (float)(bx - x);
                    float fz = (float)(bz - z);
                    float fy = (float)(by - y);

                    if (fx * fx + fz * fz + fy * fy <= rad * rad)
                        world_set_block(w, bx, by, bz, D_MOSSY, 0, 4);
                }
            }
        }

        x += -(var18 + 1) + jr_int_n(r, 2 + var18 * 2);
        z += -(var18 + 1) + jr_int_n(r, 2 + var18 * 2);
        y += -jr_int_n(r, 2);
    }

    return 1;
}

/* -------------------------------------------------------- per-biome overrides */

/* WorldGenDoublePlant at UP32, meta by the call site. */
static void place_double_plant(struct populate *p, int cx, int cz, int meta)
{
    struct world *w = &p->world;
    jrand *r = &p->rand;
    int n;
    int x = spot_x(p, cx), z = spot_z(p, cz);
    int y = jr_int_n(r, get_height(p, x, z) + 32);
    const struct feature *rows = features_plants_for("doubleplant", &n);
    rows[meta].generate(w, r, x, y, z, rows[meta].block, rows[meta].count);
}

/* BiomeGenPlains.decorate's noise branch: the sunflower patch sets 15/5,
 * otherwise 4/10 and seven ferns; field_150628_aC (aC, Sunflower Plains) adds
 * ten at meta 0. */
static void plains_decorate(struct populate *p, int aC, struct deco *d, int cx, int cz)
{
    double var5 = perlin_point(&p->flower_noise, (double)(cx * 16 + 8) / 200.0, (double)(cz * 16 + 8) / 200.0);

    if (var5 < -0.8)
    {
        d->flowers = 15;
        d->grass = 5;
    }
    else
    {
        d->flowers = 4;
        d->grass = 10;

        for (int i = 0; i < 7; ++i) place_double_plant(p, cx, cz, PLANT_FERN);
    }

    if (aC)
    {
        for (int i = 0; i < 10; ++i) place_double_plant(p, cx, cz, PLANT_SUNFLOWER);
    }
}

/* The savanna seven, also the Savanna.Mutated pre. */
static void savanna_pre(struct populate *p, int cx, int cz)
{
    for (int i = 0; i < 7; ++i) place_double_plant(p, cx, cz, PLANT_FERN);
}

/* BiomeGenTaiga.decorate's pre: the seven rose double plants every taiga
 * object runs; the two mega types also draw WorldGenBlockBlob first. */
static void taiga_pre(struct populate *p, int cx, int cz, int mega)
{
    if (mega)
    {
        int blobs = jr_int_n(&p->rand, 3);

        for (int i = 0; i < blobs; ++i)
        {
            int x = spot_x(p, cx), z = spot_z(p, cz);
            int y = get_height(p, x, z);
            place_block_blob(p, x, y, z);
        }
    }

    for (int i = 0; i < 7; ++i) place_double_plant(p, cx, cz, PLANT_ROSE);
}

/* WorldGenIceSpike and WorldGenIcePath, the Ice Plains Spikes pre. */
static void spikes_pre(struct populate *p, int cx, int cz)
{
    struct world *w = &p->world;
    jrand *r = &p->rand;
    const struct feature *rows;
    int n;

    for (int i = 0; i < 3; ++i)
    {
        int x = spot_x(p, cx), z = spot_z(p, cz);
        rows = features_lakes_for("icespike", &n);
        rows->generate(w, r, x, get_height(p, x, z), z, rows->block, rows->count);
    }

    for (int i = 0; i < 2; ++i)
    {
        int x = spot_x(p, cx), z = spot_z(p, cz);
        int y = get_height(p, x, z);
        rows = features_lakes_for("icepath", &n);
        rows->generate(w, r, x, y, z, rows->block, rows->count);
    }
}

/* BiomeGenForest.decorate's 4x4 patch loop (the roofed forest's, run with the
 * choice object's own tree instance and a fresh big mushroom each spot). */
static void forest_patch_pre(struct populate *p, int cx, int cz)
{
    struct world *w = &p->world;
    jrand *r = &p->rand;
    const struct feature *rows;
    int n;

    for (int i = 0; i < 4; ++i)
    {
        for (int j = 0; j < 4; ++j)
        {
            int x = cx * 16 + i * 4 + 1 + 8 + jr_int_n(r, 3);
            int z = cz * 16 + j * 4 + 1 + 8 + jr_int_n(r, 3);
            int y = get_height(p, x, z);

            if (jr_int_n(r, 20) == 0)
            {
                rows = features_lakes_for("bigmushroom", &n);
                rows[2].generate(w, r, x, y, z, rows[2].block, rows[2].count);
            }
            else
            {
                place_tree(p, 29, x, y, z);   /* func_150567_a on the roofed forest */
            }
        }
    }
}

/* BiomeGenForest.decorate's double-plant sweep: var5 = nextInt(5) - 3 (+2 for
 * the flower forest), each spot a meta pick and up to five position tries that
 * count on the first success. */
static void forest_flower_loop(struct populate *p, int type, int cx, int cz)
{
    struct world *w = &p->world;
    jrand *r = &p->rand;
    int n;
    const struct feature *rows = features_plants_for("doubleplant", &n);

    int count = jr_int_n(r, 5) - 3;

    if (type == 1) count += 2;

    for (int done = 0; done < count; )
    {
        int pick = jr_int_n(r, 3);
        int meta = pick == 0 ? 1 : pick == 1 ? 4 : 5;

        for (int tries = 0; tries < 5; ++tries)
        {
            int x = spot_x(p, cx), z = spot_z(p, cz);
            int y = jr_int_n(r, get_height(p, x, z) + 32);

            if (rows[meta].generate(w, r, x, y, z, rows[meta].block, rows[meta].count))
                break;
        }

        ++done;   /* the vanilla ++var6 runs whether a plant went in or not */
    }
}

static void hills_post(struct populate *p, int cx, int cz);
static void desert_post(struct populate *p, int cx, int cz);
static void jungle_post(struct populate *p, int cx, int cz);
static void forest_patch_pre(struct populate *p, int cx, int cz);
static void forest_flower_loop(struct populate *p, int type, int cx, int cz);

/* The negative checks test_populate plants: one biome's tree count (the taiga,
 * the first biome of pop2a that decorates trees) and one mutated biome's tree
 * pick (the birch mutation's nextBoolean, both instances the vined one). */

void decorator_decorate(struct populate *p, int biome, int cx, int cz)
{
    struct deco d = deco_of(biome);
    struct bshape s = shape_of(biome);

    if (nw_env->cfg.decorator_negative_count && biome == 5) ++d.trees;

    /* the pre steps the class's decorate runs before the shared body. The
     * mutations that are a plain BiomeGenMutated (or BiomeGenForest's
     * anonymous class, for the roofed forest's 157) run the base's decorate or
     * the body alone, which shape_of already carries. */
    switch (s.kind)
    {
    case S_PLAINS:
        plains_decorate(p, s.aC, &d, cx, cz);
        break;

    case S_TAIGA:
        taiga_pre(p, cx, cz, s.mega);
        break;

    case S_SNOW:
        spikes_pre(p, cx, cz);
        break;

    case S_SAVANNA:
        savanna_pre(p, cx, cz);
        break;

    case S_FOREST:
        if (s.aF == 3) forest_patch_pre(p, cx, cz);

        forest_flower_loop(p, s.aF, cx, cz);
        break;

    default:
        break;
    }

    decorator_body(p, biome, d, cx, cz);

    switch (s.kind)
    {
    case S_DESERT:
        desert_post(p, cx, cz);
        break;

    case S_JUNGLE:
        jungle_post(p, cx, cz);
        break;

    case S_HILLS:
        hills_post(p, cx, cz);
        break;

    default:
        break;
    }
}

/* --------------------------------------------------------------------- posts */

/* BiomeGenHills.decorate's tail: emerald ore in stone, then seven monster egg
 * veins (WorldGenMinable(monster_egg, 8)). */
static void hills_post(struct populate *p, int cx, int cz)
{
    struct world *w = &p->world;
    jrand *r = &p->rand;
    int veins = 3 + jr_int_n(r, 6);

    for (int i = 0; i < veins; ++i)
    {
        int x = cx * 16 + jr_int_n(r, 16);
        int y = jr_int_n(r, 28) + 4;
        int z = cz * 16 + jr_int_n(r, 16);

        if (world_get_block(w, x, y, z) == 1 /* Blocks.stone */)
            world_set_block(w, x, y, z, D_EMERALD_ORE, 0, 2);
    }

    for (int i = 0; i < 7; ++i)
    {
        int x = cx * 16 + jr_int_n(r, 16);
        int y = jr_int_n(r, 64);
        int z = cz * 16 + jr_int_n(r, 16);
        feature_minable(w, r, x, y, z, D_MONSTER_EGG, 8);
    }
}

/* BiomeGenDesert.decorate's tail: one desert well per 1000. */
static void desert_post(struct populate *p, int cx, int cz)
{
    struct world *w = &p->world;
    jrand *r = &p->rand;
    int n;

    if (jr_int_n(r, 1000) != 0) return;

    int x = spot_x(p, cx), z = spot_z(p, cz);
    const struct feature *rows = features_lakes_for("desertwell", &n);
    rows->generate(w, r, x, get_height(p, x, z) + 1, z, rows->block, rows->count);
}

/* BiomeGenJungle.decorate's tail: one melon, then 50 vine spots at y 128. */
static void jungle_post(struct populate *p, int cx, int cz)
{
    struct world *w = &p->world;
    jrand *r = &p->rand;
    const struct feature *rows;
    int n;

    {
        int x = spot_x(p, cx), z = spot_z(p, cz);
        int y = jr_int_n(r, get_height(p, x, z) * 2);
        rows = features_plants_for("melon", &n);
        rows->generate(w, r, x, y, z, rows->block, rows->count);
    }

    rows = features_plants_for("vines", &n);

    for (int i = 0; i < 50; ++i)
    {
        int x = spot_x(p, cx), z = spot_z(p, cz);
        rows->generate(w, r, x, 128, z, rows->block, rows->count);
    }
}
