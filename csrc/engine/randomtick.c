/* The random block ticks of 1.7.10, checked against the oracle's TickProbe
 * (oracle/harness/netherite/oracle/TickProbe.java). One function per block class,
 * dispatch by block id; see randomtick.h.
 *
 * Every World.setBlock call keeps the flags the Java source spells: a grass
 * block dying to dirt and a spread both write flags 3 (their notify runs the
 * blockcb callbacks), a crop stage writes the metadata with flags 2, a
 * sapling's own stage bit writes flags 4. The drops a tick performs draw from
 * World.rand even though the entities themselves are not block state: the
 * recorded World.rand state after each case checks the draw count, so the
 * drop helpers here spend exactly the values Block.dropBlockAsItemWithChance
 * and its overrides spend (three nextFloat per item behind the chance roll,
 * the overrides' own rolls in their order). The tick in progress publishes
 * its World.rand stream for the block callbacks (blockcb.c), whose breaking
 * paths draw from it the same way.
 */
#include "randomtick.h"
#include "env.h"
#include "blocks.h"
#include "blockcb.h"
#include "features.h"
#include "features_lakes.h" /* biome_at, the grass flowers' biome */
#include "fire.h"
#include "noise.h"          /* perlin_point, BiomeGenBase.field_150606_ad */
#include "drops.h"
#include "envstack.h"

#include <string.h>

#define randomtick_wr (nw_env->randomtick.env.wr)

void randomtick_tick_rand(jrand *wr)
{
    randomtick_wr = wr;
}

jrand *randomtick_tick_rand_stream(void)
{
    return randomtick_wr;
}

#define rt_skylight (nw_env->randomtick.skylight)

void randomtick_set_skylight(int subtracted)
{
    rt_skylight = subtracted;
}

/* Block ids the ticks name, Block.getIdFromBlock. */
enum {
    RT_GRASS = 2, RT_DIRT = 3, RT_SAPLING = 6, RT_LOG = 17, RT_LEAVES = 18,
    RT_WHEAT = 59, RT_FARMLAND = 60, RT_CARROTS = 141, RT_POTATOES = 142, RT_SOUL_SAND = 88, RT_BROWN_MUSHROOM = 39,
    RT_RED_MUSHROOM = 40, RT_REDSTONE_ORE = 73, RT_LIT_REDSTONE_ORE = 74,
    RT_SNOW_LAYER = 78, RT_ICE = 79, RT_CACTUS = 81, RT_REEDS = 83,
    RT_PUMPKIN = 86, RT_PUMPKIN_STEM = 104, RT_MELON_STEM = 105, RT_VINE = 106,
    RT_MYCELIUM = 110, RT_NETHER_WART = 115, RT_MELON_BLOCK = 103,
    RT_COCOA = 127, RT_LEAVES2 = 161, RT_LOG2 = 162,
    RT_RAIL_POWERED = 27, RT_RAIL_DETECTOR = 28, RT_RAIL = 66, RT_RAIL_ACTIVATOR = 157,
    RT_TORCH = 50,
};

/* World.isAirBlock. */
static int air_at(struct world *w, int x, int y, int z)
{
    return BLOCKS[world_get_block(w, x, y, z) & 4095].material == 0; /* Material.air */
}


/* the materials tested here, by index: looked up by name before main (a
 * regenerated blocks.h cannot shift them), the same for every environment */
static int MATI_AIR = -1, MATI_LEAVES = -1, MATI_WATER = -1;

__attribute__((constructor)) static void mati_init(void)
{
    for (size_t i = 0; i < sizeof MATERIALS / sizeof MATERIALS[0]; ++i)
    {
        if (MATERIALS[i].name == NULL) continue;
        if (strcmp(MATERIALS[i].name, "air") == 0) MATI_AIR = (int)i;
        if (strcmp(MATERIALS[i].name, "leaves") == 0) MATI_LEAVES = (int)i;
        if (strcmp(MATERIALS[i].name, "water") == 0) MATI_WATER = (int)i;
    }
}

static int mat_is(struct world *w, int x, int y, int z, int mati)
{
    return BLOCKS[world_get_block(w, x, y, z) & 4095].material == mati;
}

static int mat_id_is(int id, int mati)
{
    return BLOCKS[id & 4095].material == mati;
}

/* ------------------------------------------------------------- light reads */

/* Block.field_149783_u, set once in Block.registerBlocks: a block whose
 * stored light is not the light the world reports. getBlockLightValue takes
 * the maximum of the five neighbours' stored light for such a block. The
 * terms: render type 10, a BlockSlab, farmland, a material whose
 * getCanBlockGrass is false (the MaterialLogic, MaterialTransparent and
 * MaterialPortal objects: plants, vine, circuits, carpet, field_151597_y,
 * air, fire, Portal), lightOpacity 0. Air itself is forced false. */
static int light_diffuse_of(int id)
{
    const struct block_def *b = &BLOCKS[id & 4095];
    const char *mat = MATERIALS[b->material].name;

    /* an id without a material or class name is no block the world holds */
    if (mat == NULL) return 0;
    if (strcmp(mat, "air") == 0) return 0;
    if (strcmp(mat, "plants") == 0 || strcmp(mat, "vine") == 0 || strcmp(mat, "circuits") == 0 ||
        strcmp(mat, "carpet") == 0 || strcmp(mat, "field_151597_y") == 0 || strcmp(mat, "fire") == 0 ||
        strcmp(mat, "Portal") == 0) return 1;
    if (b->render_type == 10) return 1;
    if (b->class_name == NULL) return 0;
    if (strcmp(b->class_name, "BlockStoneSlab") == 0 || strcmp(b->class_name, "BlockWoodSlab") == 0) return 1;
    if ((id & 4095) == RT_FARMLAND) return 1;
    if (b->opacity == 0) return 1;

    return 0;
}

/* light_diffuse_of by block id, built before main (the same for every
 * environment): the light reads ask it per cell */
static unsigned char rt_diffuse[4096];

__attribute__((constructor)) static void rt_diffuse_init(void)
{
    for (int id = 0; id < 4096; ++id) rt_diffuse[id] = (unsigned char)light_diffuse_of(id);
}

int rt_light_diffuse(int id)
{
    return rt_diffuse[id & 4095];
}

/* The chunk a world-coordinate read lands in. World.getBlockLightValue goes
 * through getChunkFromChunkCoords, which with ChunkProviderServer's
 * loadChunkOnProvideRequest = false is the shared EmptyChunk: a chunk that is
 * not loaded reads air and light 0, not a freshly generated one. */
static struct chunk *rt_chunk(struct world *w, int x, int z)
{
    if (w->no_generate) return world_chunk(w, x >> 4, z >> 4);

    return world_load_chunk(w, x >> 4, z >> 4);
}

/* Chunk.getBlockLightValue: the stored value with the sky part reduced by
 * World.skylightSubtracted and floored at 0 by the max against the block
 * light only when the block light is higher; a section that does not exist
 * answers the sky default minus the subtraction, whatever the height map
 * says. Under a provider with hasNoSky (the Nether, the End) the sky part
 * is 0 and a missing section answers 0. */
static int chunk_block_light(const struct chunk *c, int x, int y, int z, int sub)
{
    if (!(c->mask >> (y >> 4) & 1)) return !c->no_sky && sub < 15 ? 15 - sub : 0;

    int idx = x << 12 | z << 8 | y;
    int sky = (c->no_sky ? 0 : chunk_cell_sky(c, idx)) - sub;
    int bl = chunk_cell_blocklight(c, idx);

    return bl > sky ? bl : sky;
}

/* World.getBlockLightValue_do with check false: the stored value. The check
 * reads its five neighbours through this, so it does not call itself. */
static int block_light_raw(struct world *w, int x, int y, int z)
{
    if (x < -30000000 || z < -30000000 || x >= 30000000 || z >= 30000000) return 15;

    if (y < 0) return 0;
    if (y >= 256) y = 255;

    struct chunk *c = rt_chunk(w, x, z);

    /* EmptyChunk.getBlockLightValue is 0 */
    if (c == NULL) return 0;

    return chunk_block_light(c, x & 15, y, z & 15, rt_skylight);
}

/* World.getBlockLightValue_do(x, y, z, check). */
static int block_light_do(struct world *w, int x, int y, int z, int check)
{
    if (x < -30000000 || z < -30000000 || x >= 30000000 || z >= 30000000) return 15;

    if (check && rt_light_diffuse(world_get_block(w, x, y, z)))
    {
        int var10 = block_light_raw(w, x, y + 1, z);
        int v;

        v = block_light_raw(w, x + 1, y, z);
        if (v > var10) var10 = v;
        v = block_light_raw(w, x - 1, y, z);
        if (v > var10) var10 = v;
        v = block_light_raw(w, x, y, z + 1);
        if (v > var10) var10 = v;
        v = block_light_raw(w, x, y, z - 1);
        if (v > var10) var10 = v;

        return var10;
    }

    return block_light_raw(w, x, y, z);
}

/* World.getBlockLightValue. */
static int block_light(struct world *w, int x, int y, int z)
{
    return block_light_do(w, x, y, z, 1);
}

/* World.getFullBlockLightValue: no diffuse check, no subtraction. */
static int full_block_light(struct world *w, int x, int y, int z)
{
    if (y < 0) return 0;
    if (y >= 256) y = 255;

    struct chunk *c = rt_chunk(w, x, z);

    if (c == NULL) return 0;

    return chunk_block_light(c, x & 15, y, z & 15, 0);
}

/* Chunk.getSavedLightValue(EnumSkyBlock.Block), through World.getSavedLightValue. */
static int saved_block_light(struct world *w, int x, int y, int z)
{
    return world_get_light(w, LIGHT_BLOCK, x, y, z);
}

/* ----------------------------------------------------------- spent drops */

/* java.util.Random.nextInt(1) for the quantity overrides that draw even
 * though the bound is 1 (the power-of-two branch still spends next(31)). */
static void ni1(jrand *wr)
{
    jr_next(wr, 31);
}

/* The tick's Det streams and the sink a drop's entities go to. */
#define drop_det (nw_env->randomtick.env.det)
#define drop_role (nw_env->randomtick.env.role)
#define drop_sink (nw_env->randomtick.env.sink)
#define drop_sink_ctx (nw_env->randomtick.env.sink_ctx)

void randomtick_tick_det(det_state *det, int role)
{
    drop_det = det;
    drop_role = role;
}

void randomtick_tick_drop_sink(void (*sink)(void *, double, double, double, int, int, int), void *ctx)
{
    drop_sink = sink;
    drop_sink_ctx = ctx;
}

void randomtick_tick_save(struct randomtick_env *e)
{
    e->wr = randomtick_wr;
    e->det = drop_det;
    e->role = drop_role;
    e->sink = drop_sink;
    e->sink_ctx = drop_sink_ctx;
}

void randomtick_tick_load(const struct randomtick_env *e)
{
    randomtick_wr = e->wr;
    drop_det = e->det;
    drop_role = e->role;
    drop_sink = e->sink;
    drop_sink_ctx = e->sink_ctx;
}

/* Block.dropBlockAsItem_do: the three offsets, then the EntityItem. */
static void drop_do(jrand *wr, int x, int y, int z, int item, int damage, int count)
{
    float f = 0.7F;
    double dx = (double)(jr_float(wr) * f) + (double)(1.0F - f) * 0.5;
    double dy = (double)(jr_float(wr) * f) + (double)(1.0F - f) * 0.5;
    double dz = (double)(jr_float(wr) * f) + (double)(1.0F - f) * 0.5;

    if (drop_sink != NULL) drop_sink(drop_sink_ctx, (double)x + dx, (double)y + dy, (double)z + dz, item, damage, count);
}

/* BlockBush.func_149855_e's drop for the crops, the stems and the nether
 * wart: dropBlockAsItemWithChance and its overrides, in their order.
 * BlockCrops drops the crop at meta 7 (wheat, a carrot, a potato) else the
 * seed item, then 3 seed rolls when grown; BlockPotato adds its poisonous
 * roll; BlockStem's own item is null (the roll only) before its 3 seed
 * rolls; BlockNetherWart overrides the whole method. */
static void crop_drop(jrand *wr, int x, int y, int z, int id, int meta)
{
    int seed = 0, crop = 0;

    switch (id & 4095)
    {
    case RT_WHEAT:    seed = 295; crop = 296; break;
    case RT_CARROTS:  seed = 391; crop = 391; break;
    case RT_POTATOES: seed = 392; crop = 392; break;
    case RT_PUMPKIN_STEM: seed = 361; break;
    case RT_MELON_STEM:   seed = 362; break;
    default: break;
    }

    switch (id & 4095)
    {
    case RT_WHEAT:
    case RT_CARROTS:
    case RT_POTATOES:
        /* super.dropBlockAsItemWithChance at fortune 0: quantity 1, the
         * chance roll, the item at damage 0 */
        jr_float(wr);
        drop_do(wr, x, y, z, meta == 7 ? crop : seed, 0, 1);

        if (meta >= 7)
            for (int i = 0; i < 3; ++i)
                if (jr_int_n(wr, 15) <= meta) drop_do(wr, x, y, z, seed, 0, 1);

        if ((id & 4095) == RT_POTATOES && meta >= 7 && jr_int_n(wr, 50) == 0) drop_do(wr, x, y, z, 394, 0, 1);

        break;

    case RT_PUMPKIN_STEM:
    case RT_MELON_STEM:
        jr_float(wr);

        for (int i = 0; i < 3; ++i)
            if (jr_int_n(wr, 15) <= meta) drop_do(wr, x, y, z, seed, 0, 1);

        break;

    case RT_NETHER_WART:
    {
        int n = meta >= 3 ? 2 + jr_int_n(wr, 3) : 1;

        for (int i = 0; i < n; ++i) drop_do(wr, x, y, z, 372, 0, 1);

        break;
    }

    default:
        break;
    }
}

/* BlockLiquid.func_149799_m. */
void randomtick_fizz(jrand *r, int x, int y, int z)
{
    jr_float(r);
    jr_float(r);

    if (drop_det != NULL)
        for (int i = 0; i < 16; ++i) det_math_random_role(drop_det, drop_role);

    (void)x;
    (void)y;
    (void)z;
}

/* BlockLeaves.dropBlockAsItemWithChance at fortune 0: the sapling roll
 * (func_150123_b, 40 for an old leaf at the jungle variant else 20), the
 * three dropBlockAsItem_do offsets and one item when it hits, then the
 * subclass's apple roll (func_150124_c: old leaves at variant 0, new leaves
 * at variant 1) and its item. BlockOldLeaf's sapling damage is meta & 3,
 * BlockNewLeaf's is (meta & 3) + 4; the apple is damage 0. Used by the decay
 * path (rt_leaves) and by a break (break_draws). */
static void leaves_drop(jrand *wr, int x, int y, int z, int id, int meta)
{
    int old_leaf = (id & 4095) == RT_LEAVES;
    int variant = meta & 3;
    int chance = old_leaf && variant == 3 ? 40 : 20;

    if (jr_int_n(wr, chance) == 0) drop_do(wr, x, y, z, 6, old_leaf ? variant : variant + 4, 1);

    if (variant == (old_leaf ? 0 : 1) && jr_int_n(wr, 200) == 0) drop_do(wr, x, y, z, 260, 0, 1);
}

/* Block.dropBlockAsItem through drops.c's per-block table on the tick's
 * World.rand: the table's entities come out with their dropBlockAsItem_do
 * positions, and the sink builds each EntityItem (its Math.random and Det
 * draws), so the table's own Math.random stream is a scratch one. */
static void table_drop(jrand *wr, int x, int y, int z, int id, int meta)
{
    jrand scratch = {0};
    struct drop_case cs = {id & 4095, meta & 255, 0, x, y, z, 0, &scratch, NULL};
    struct drop_ent *out ENV_LOCAL = envstack_take(DROPS_MAX_ENTS * sizeof *out);
    int n = drops_break_live(&cs, wr, 1.0f, out, DROPS_MAX_ENTS);

    for (int i = 0; i < n; ++i)
        if (out[i].kind == DROP_ITEM && out[i].item > 0 && drop_sink != NULL)
            drop_sink(drop_sink_ctx, out[i].x, out[i].y, out[i].z, out[i].item, out[i].damage, out[i].count);
}

/* The items one break of the block produces, in dropBlockAsItemWithChance's
 * order, with the draws. Each case is one class's quantityDropped(WithBonus),
 * chance roll, getItemDropped and damageDropped, then dropBlockAsItem_do's
 * three offsets. */
static void break_draws(jrand *wr, int x, int y, int z, int id, int meta)
{
    switch (id & 4095)
    {
    case 70:  /* the pressure plates (BlockBasePressurePlate.onNeighborBlockChange's
               * dropBlockAsItem): their materials block movement, so the
               * default's flow rule does not cover them; Block's own drop */
    case 72:
    case 147:
    case 148:
        table_drop(wr, x, y, z, id, meta);
        break;

    case 31: /* BlockTallGrass: quantity 1 + nextInt(1), the chance roll, then
              * getItemDropped's nextInt(8) deciding the seeds; the drop's
              * damage is Block's base damageDropped, which is 0 */
        ni1(wr);
        jr_float(wr);

        if (jr_int_n(wr, 8) == 0) drop_do(wr, x, y, z, 295, 0, 1);

        break;

    case RT_SAPLING: /* BlockSapling: item 6, damage meta & 3 */
        jr_float(wr);
        drop_do(wr, x, y, z, 6, meta & 3, 1);
        break;

    case RT_BROWN_MUSHROOM:
    case RT_RED_MUSHROOM:
        jr_float(wr);
        drop_do(wr, x, y, z, id & 4095, 0, 1);
        break;

    case 37: /* BlockFlower: the block's own item, damageDropped is the meta */
    case 38:
        jr_float(wr);
        drop_do(wr, x, y, z, id & 4095, meta, 1);
        break;

    case 83: /* BlockReed: the reed item, 338 */
        jr_float(wr);
        drop_do(wr, x, y, z, 338, 0, 1);
        break;

    case 81: /* BlockCactus */
        jr_float(wr);
        drop_do(wr, x, y, z, 81, 0, 1);
        break;

    case 175: /* BlockDoublePlant: no item for the upper half or the grass and
               * fern variants, else the block's own item at damage meta & 7 */
        jr_float(wr);

        if ((meta & 8) == 0 && (meta & 7) != 2 && (meta & 7) != 3)
            drop_do(wr, x, y, z, 175, meta & 7, 1);

        break;

    case 127: /* BlockCocoa overrides dropBlockAsItemWithChance: no quantity
               * draw and no roll, one item per age step (1 below age 2, else
               * 3), each a dye at damage 3 */
    {
        int age = (meta >> 2) & 3;
        int n = age >= 2 ? 3 : 1;

        for (int i = 0; i < n; ++i) drop_do(wr, x, y, z, 351, 3, 1);

        break;
    }

    case RT_WHEAT:
    case RT_CARROTS:
    case RT_POTATOES:
    case RT_PUMPKIN_STEM:
    case RT_MELON_STEM:
    case RT_NETHER_WART:
        crop_drop(wr, x, y, z, id & 4095, meta);
        break;

    case 32: /* BlockDeadBush: getItemDropped null, the roll is drawn anyway */
        jr_float(wr);
        break;

    case RT_LEAVES: /* BlockLeaves.dropBlockAsItemWithChance, shared with the
                     * decay path */
    case RT_LEAVES2:
        leaves_drop(wr, x, y, z, id & 4095, meta);
        break;

    case RT_RAIL_POWERED: /* BlockRailBase.quantityDropped is the constant 1
                           * (no draw), getItemDropped and damageDropped are
                           * Block's own (the block's item, damage 0) */
    case RT_RAIL_DETECTOR:
    case RT_RAIL:
    case RT_RAIL_ACTIVATOR:
    case RT_TORCH:        /* BlockTorch: Block's own drop, damage 0 */
        /* A rail or a torch is Material.circuits, which World.setBlock's flow
         * destroys: BlockDynamicLiquid.func_149813_h drops it before the write,
         * the same shape as the plants above. */
        jr_float(wr);
        drop_do(wr, x, y, z, id & 4095, 0, 1);
        break;

    case 30: /* BlockWeb: Block's constant quantity 1, getItemDropped the
              * string (287), damage 0; a mineshaft's web a spring's flow
              * replaces during population drops it */
    case 132: /* BlockTripWire (Material.circuits, washed away): the string */
        jr_float(wr);
        drop_do(wr, x, y, z, 287, 0, 1);
        break;

    default:
        /* Every other block a flow can replace (a material that does not
         * block movement: redstone dust, a lever, a button, a redstone
         * torch, a repeater or comparator, tripwire and its hook, a flower
         * pot, a carpet, a lily pad) through drops.c's
         * dropBlockAsItemWithChance table at chance 1.0 and fortune 0; its
         * no-drop rows (a vine, a snow layer, fire, the skull's empty
         * override) spend nothing. The terrain keeps no draws. */
        if (!MATERIALS[BLOCKS[id & 4095].material].blocks_movement) table_drop(wr, x, y, z, id, meta);
        break;
    }
}

void randomtick_drop(struct world *w, int x, int y, int z, int id, int meta)
{
    if (randomtick_wr == NULL) return;

    break_draws(randomtick_wr, x, y, z, id, meta);
}

void randomtick_drop_stack_do(int x, int y, int z, int item, int damage)
{
    if (randomtick_wr == NULL) return;

    drop_do(randomtick_wr, x, y, z, item, damage, 1);
}

/* The same drop over a caller's World.rand into a caller's sink (a block
 * callback inside an entity's update, where no tick stream is published). */
void randomtick_drop_into(jrand *wr, void (*sink)(void *, double, double, double, int, int, int), void *ctx,
                          int x, int y, int z, int id, int meta)
{
    void (*prev_sink)(void *, double, double, double, int, int, int) = drop_sink;
    void *prev_ctx = drop_sink_ctx;

    drop_sink = sink;
    drop_sink_ctx = ctx;
    break_draws(wr, x, y, z, id, meta);
    drop_sink = prev_sink;
    drop_sink_ctx = prev_ctx;
}

/* The draw-count-only entry the probes and the leaf paths use. */
void randomtick_callback_drop(int id, int meta)
{
    randomtick_drop(NULL, 0, 0, 0, id, meta);
}

/* BlockLiquid.func_149799_m from a block callback (blockcb.c's lava-water
 * check): the stream is the tick's published World.rand, which the callback
 * has no jrand of its own for. */
void randomtick_callback_fizz(void)
{
    if (randomtick_wr == NULL) return;

    randomtick_fizz(randomtick_wr, 0, 0, 0);
}

/* ------------------------------------------------------------ the bushes */

/* BlockBush.canBlockStay's floor test, with the subclasses the ticks reach:
 * BlockCrops and BlockStem override func_149854_a to farmland only,
 * BlockNetherWart soul sand, BlockMushroom its own light rule, BlockDeadBush
 * sand, hardened clay, stained clay or dirt, BlockLilyPad's own
 * canBlockStay still water (a source, meta 0) below, BlockSapling the base
 * (grass, dirt or farmland). */
static int bush_floor_ok(int id, int below, struct world *w, int x, int y, int z)
{
    if ((id & 4095) == 32)
        return (below & 4095) == 12 || (below & 4095) == 172 || (below & 4095) == 159 || (below & 4095) == RT_DIRT;

    if ((id & 4095) == 111)
        return y >= 0 && y < 256 && ((below & 4095) == 8 || (below & 4095) == 9) && world_get_meta(w, x, y - 1, z) == 0;

    if ((id & 4095) == RT_NETHER_WART) return (below & 4095) == RT_SOUL_SAND;

    if ((id & 4095) == RT_WHEAT || (id & 4095) == RT_CARROTS || (id & 4095) == RT_POTATOES ||
        (id & 4095) == RT_PUMPKIN_STEM || (id & 4095) == RT_MELON_STEM)
        return (below & 4095) == RT_FARMLAND;

    if ((id & 4095) == RT_BROWN_MUSHROOM || (id & 4095) == RT_RED_MUSHROOM)
    {
        if (y < 0 || y >= 256) return 0;
        if ((below & 4095) == RT_MYCELIUM) return 1;
        if ((below & 4095) == RT_DIRT && world_get_meta(w, x, y - 1, z) == 2) return 1;

        return full_block_light(w, x, y, z) < 13 && BLOCKS[below & 4095].opaque_cube;
    }

    return (below & 4095) == RT_GRASS || (below & 4095) == RT_DIRT || (below & 4095) == RT_FARMLAND;
}

/* BlockBush.updateTick: drop and remove the plant when it cannot stay. The
 * drop's World.rand draws are the same spend the breaking callbacks do, so
 * both go through one function (the tallgrass case draws its nextInt(8)
 * before the roll, the subclasses carry their own). */
static void bush_tick(struct world *w, jrand *wr, int id, int x, int y, int z)
{
    int below = world_get_block(w, x, y - 1, z);

    if (bush_floor_ok(id, below, w, x, y, z)) return;

    int meta = world_get_meta(w, x, y, z);
    randomtick_drop(w, x, y, z, id, meta);
    world_set_block(w, x, y, z, 0, 0, 2);
}

/* BlockDoublePlant.func_149855_e: the same canBlockStay test, with the two
 * halves: the upper half needs the lower one below it, the lower needs the
 * upper above it and the bush's own floor. A removal drops the lower half's
 * item and clears the other half with flags 2. */
static void double_plant_tick(struct world *w, jrand *wr, int x, int y, int z)
{
    int meta = world_get_meta(w, x, y, z);
    int upper = (meta & 8) != 0;
    int below = world_get_block(w, x, y - 1, z) & 4095;
    int above = world_get_block(w, x, y + 1, z) & 4095;

    (void)wr;

    if (upper)
    {
        if (below == 175) return;
    }
    else
    {
        int floor_ok = below == RT_GRASS || below == RT_DIRT || below == RT_FARMLAND;

        if (above == 175 && floor_ok) return;
    }

    if (!upper)
    {
        randomtick_drop(w, x, y, z, 175, meta);

        if (above == 175) world_set_block(w, x, y + 1, z, 0, 0, 2);
    }

    world_set_block(w, x, y, z, 0, 0, 2);
}

/* ------------------------------------------------------- grass, mycelium */

static void grass_like(struct world *w, jrand *cr, int x, int y, int z, int self)
{
    int above = world_get_block(w, x, y + 1, z) & 4095;
    int light = block_light(w, x, y + 1, z);

    if (light < 4 && BLOCKS[above].opacity > 2)
    {
        world_set_block(w, x, y, z, RT_DIRT, 0, 3);
    }
    else if (light >= 9)
    {
        for (int i = 0; i < 4; ++i)
        {
            int dx = jr_int_n(cr, 3) - 1;
            int dy = jr_int_n(cr, 5) - 3;
            int dz = jr_int_n(cr, 3) - 1;
            int tb = world_get_block(w, x + dx, y + dy + 1, z + dz) & 4095;

            if (world_get_block(w, x + dx, y + dy, z + dz) == RT_DIRT &&
                world_get_meta(w, x + dx, y + dy, z + dz) == 0 &&
                block_light(w, x + dx, y + dy + 1, z + dz) >= 4 && BLOCKS[tb].opacity <= 2)
            {
                world_set_block(w, x + dx, y + dy, z + dz, self, 0, 3);
            }
        }
    }
}

/* ------------------------------------------------------------- the leaves */

/* BlockLeaves.field_150128_a: the decay search's distance array, one int per
 * cell of the 32^3 box, kept on the block object in vanilla (one array for
 * Blocks.leaves, one for Blocks.leaves2, allocated zeroed on the first
 * updateTick). A search rewrites the +-4 cube before reading it and never
 * writes the +-5 shell it also reads (always 0), so of the array only the
 * centre cell outlives the call: updateTick reads it even when
 * checkChunksExist failed and nothing was searched. That is game state, per
 * leaf block, in nw_env->leaves; rt_leaves computes the centre's value
 * without the rest of the array. */
/* a cell of the +-4 cube around the leaf */
#define LEAF_NEAR(dx, dy, dz) (((dx) + 4) * 81 + ((dy) + 4) * 9 + (dz) + 4)

/* BlockLeaves.updateTick. */
static void rt_leaves(struct world *w, jrand *wr, int id, int x, int y, int z)
{
    int meta = world_get_meta(w, x, y, z);

    if ((meta & 8) == 0 || (meta & 4) != 0) return;

    /* BlockLeaves.updateTick's var7 is 4 and its checkChunksExist box is
     * var7 + 1 = 5: a log up to four blocks away keeps the leaf alive, so the
     * search box is the +-4 cube of the 32^3 array. */
    int var7 = 4;
    int32_t *centre = &nw_env->leaves.decay_centre[(id & 4095) == RT_LEAVES2];

    if (world_check_chunks_exist(w, x - var7 - 1, y - var7 - 1, z - var7 - 1,
                                 x + var7 + 1, y + var7 + 1, z + var7 + 1))
    {
        /* Java fills the cube (a log 0, leaves -2, the rest -1) and floods
         * it four times from the logs: the centre ends at the length of the
         * shortest chain from a log to it through leaves, at most 4 (else
         * -2), and nothing else of the array is read again. Every cell of
         * such a chain is within 4 steps of the centre, inside the cube, so
         * the same length comes from a breadth-first search out from the
         * centre through leaves, which reads only the cells it reaches. */
        uint8_t seen[9 * 9 * 9];
        int16_t q[64];
        int qh = 0, qt = 0, found = -2;

        memset(seen, 0, sizeof seen);
        seen[LEAF_NEAR(0, 0, 0)] = 1;
        q[qt++] = (int16_t)LEAF_NEAR(0, 0, 0);
        for (int d = 1; d <= 4 && found < 0; ++d)
        {
            for (int end = qt; qh < end && found < 0; ++qh)
            {
                static const int off[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
                int cx = q[qh] / 81 - 4, cy = q[qh] / 9 % 9 - 4, cz = q[qh] % 9 - 4;

                for (int i = 0; i < 6; ++i)
                {
                    int nx = cx + off[i][0], ny = cy + off[i][1], nz = cz + off[i][2];
                    int k = LEAF_NEAR(nx, ny, nz);

                    if (seen[k]) continue;
                    seen[k] = 1;

                    int b = world_get_block(w, x + nx, y + ny, z + nz) & 4095;

                    if (b == RT_LOG || b == RT_LOG2) { found = d; break; }
                    if (d < 4 && mat_id_is(b, MATI_LEAVES)) q[qt++] = (int16_t)k;
                }
            }
        }

        *centre = found;
    }

    if (*centre >= 0)
    {
        world_set_meta(w, x, y, z, meta & -9, 4);
    }
    else
    {
        leaves_drop(wr, x, y, z, id, meta);
        world_set_block(w, x, y, z, 0, 0, 3);
    }
}

/* -------------------------------------------- crops and stems: growth rate */

/* BlockCrops.func_149864_n and BlockStem.func_149875_n, the same float loop
 * over the farmland ring below and the neighbour rows. */
static float growth_rate(struct world *w, int x, int y, int z, int self)
{
    float var5 = 1.0F;
    int var6 = world_get_block(w, x, y, z - 1) & 4095;
    int var7 = world_get_block(w, x, y, z + 1) & 4095;
    int var8 = world_get_block(w, x - 1, y, z) & 4095;
    int var9 = world_get_block(w, x + 1, y, z) & 4095;
    int var10 = world_get_block(w, x - 1, y, z - 1) & 4095;
    int var11 = world_get_block(w, x + 1, y, z - 1) & 4095;
    int var12 = world_get_block(w, x + 1, y, z + 1) & 4095;
    int var13 = world_get_block(w, x - 1, y, z + 1) & 4095;
    int var14 = var8 == self || var9 == self;
    int var15 = var6 == self || var7 == self;
    int var16 = var10 == self || var11 == self || var12 == self || var13 == self;

    for (int var17 = x - 1; var17 <= x + 1; ++var17)
    {
        for (int var18 = z - 1; var18 <= z + 1; ++var18)
        {
            float var19 = 0.0F;

            if (world_get_block(w, var17, y - 1, var18) == RT_FARMLAND)
            {
                var19 = 1.0F;

                if (world_get_meta(w, var17, y - 1, var18) > 0) var19 = 3.0F;
            }

            if (var17 != x || var18 != z) var19 /= 4.0F;

            var5 += var19;
        }
    }

    if (var16 || (var14 && var15)) var5 /= 2.0F;

    return var5;
}

/* BlockCrops.updateTick. */
static void rt_crops(struct world *w, jrand *wr, jrand *cr, int x, int y, int z)
{
    /* BlockCarrot and BlockPotato extend BlockCrops and share its updateTick,
     * but func_149864_n's neighbour test is `var8 == this`: the ticked block's
     * own id, not wheat's. */
    int self = world_get_block(w, x, y, z) & 4095;

    bush_tick(w, wr, self, x, y, z);

    if (block_light(w, x, y + 1, z) >= 9)
    {
        int meta = world_get_meta(w, x, y, z);

        if (meta < 7)
        {
            float var7 = growth_rate(w, x, y, z, self);

            if (jr_int_n(cr, (int)(25.0F / var7) + 1) == 0)
                world_set_meta(w, x, y, z, meta + 1, 2);
        }
    }
}

/* BlockStem.updateTick, fruit is the stem's field_149877_a. */
static void rt_stem(struct world *w, jrand *wr, jrand *cr, int x, int y, int z, int fruit)
{
    bush_tick(w, wr, world_get_block(w, x, y, z) & 4095, x, y, z);

    if (block_light(w, x, y + 1, z) >= 9)
    {
        float var6 = growth_rate(w, x, y, z, world_get_block(w, x, y, z) & 4095);

        if (jr_int_n(cr, (int)(25.0F / var6) + 1) == 0)
        {
            int meta = world_get_meta(w, x, y, z);

            if (meta < 7)
            {
                world_set_meta(w, x, y, z, meta + 1, 2);
            }
            else
            {
                if (world_get_block(w, x - 1, y, z) == fruit) return;
                if (world_get_block(w, x + 1, y, z) == fruit) return;
                if (world_get_block(w, x, y, z - 1) == fruit) return;
                if (world_get_block(w, x, y, z + 1) == fruit) return;

                int var8 = jr_int_n(cr, 4);
                int var9 = x, var10 = z;

                if (var8 == 0) var9 = x - 1;
                if (var8 == 1) ++var9;
                if (var8 == 2) var10 = z - 1;
                if (var8 == 3) ++var10;

                int below = world_get_block(w, var9, y - 1, var10) & 4095;

                if (mat_id_is(world_get_block(w, var9, y, var10), MATI_AIR) &&
                    (below == RT_FARMLAND || below == RT_DIRT || below == RT_GRASS))
                {
                    world_set_block(w, var9, y, var10, fruit, 0, 3);
                }
            }
        }
    }
}

/* --------------------------------------------------------- the sapling */

/* BlockSapling.func_149880_a. */
static int sapling_of_type(struct world *w, int x, int y, int z, int type)
{
    return world_get_block(w, x, y, z) == RT_SAPLING && (world_get_meta(w, x, y, z) & 7) == type;
}

/* BlockSapling.func_149878_d: pick the generator for the sapling type, clear
 * the sapling or the 2x2 (flag 4), grow the tree, restore on a false. The
 * notify flag follows each generator's doBlockNotify: WorldGenMegaPineTree is
 * built with false, the rest with true. */
static void sapling_grow(struct world *w, jrand *cr, int x, int y, int z)
{
    int meta = world_get_meta(w, x, y, z);
    int type = meta & 7;
    int big = jr_int_n(cr, 10) == 0;
    int dx = 0, dz = 0, matched = 0;

    /* new WorldGenBigTree(true) at once, whatever the type: its rand field
     * is a Det.newRandom (one seeder long); generate reseeds it */
    if (big && drop_det != NULL) (void)det_new_random_role(drop_det, drop_role);
    enum { G_BIG, G_TREES, G_TAIGA2, G_MEGAPINE, G_FOREST, G_MEGAJUNGLE, G_SMALL_JUNGLE,
           G_SAVANNA, G_CANOPY, G_RETURN } gen = big ? G_BIG : G_TREES;

    switch (type)
    {
    case 0:
    default:
        break;

    case 1:
        for (dx = 0; dx >= -1; --dx)
        {
            for (dz = 0; dz >= -1; --dz)
            {
                if (sapling_of_type(w, x + dx, y, z + dz, 1) && sapling_of_type(w, x + dx + 1, y, z + dz, 1) &&
                    sapling_of_type(w, x + dx, y, z + dz + 1, 1) && sapling_of_type(w, x + dx + 1, y, z + dz + 1, 1))
                {
                    gen = G_MEGAPINE;
                    matched = 1;
                    break;
                }
            }

            if (matched) break;
        }

        if (!matched)
        {
            dz = 0;
            dx = 0;
            gen = G_TAIGA2;
        }

        break;

    case 2:
        gen = G_FOREST;
        break;

    case 3:
        for (dx = 0; dx >= -1; --dx)
        {
            for (dz = 0; dz >= -1; --dz)
            {
                if (sapling_of_type(w, x + dx, y, z + dz, 3) && sapling_of_type(w, x + dx + 1, y, z + dz, 3) &&
                    sapling_of_type(w, x + dx, y, z + dz + 1, 3) && sapling_of_type(w, x + dx + 1, y, z + dz + 1, 3))
                {
                    gen = G_MEGAJUNGLE;
                    matched = 1;
                    break;
                }
            }

            if (matched) break;
        }

        if (!matched)
        {
            dz = 0;
            dx = 0;
            gen = G_SMALL_JUNGLE;
        }

        break;

    case 4:
        gen = G_SAVANNA;
        break;

    case 5:
        for (dx = 0; dx >= -1; --dx)
        {
            for (dz = 0; dz >= -1; --dz)
            {
                if (sapling_of_type(w, x + dx, y, z + dz, 5) && sapling_of_type(w, x + dx + 1, y, z + dz, 5) &&
                    sapling_of_type(w, x + dx, y, z + dz + 1, 5) && sapling_of_type(w, x + dx + 1, y, z + dz + 1, 5))
                {
                    gen = G_CANOPY;
                    matched = 1;
                    break;
                }
            }

            if (matched) break;
        }

        if (!matched) return;
        break;
    }

    if (matched)
    {
        world_set_block(w, x + dx, y, z + dz, 0, 0, 4);
        world_set_block(w, x + dx + 1, y, z + dz, 0, 0, 4);
        world_set_block(w, x + dx, y, z + dz + 1, 0, 0, 4);
        world_set_block(w, x + dx + 1, y, z + dz + 1, 0, 0, 4);
    }
    else
    {
        world_set_block(w, x, y, z, 0, 0, 4);
    }

    int ok = 0;

    if (gen == G_BIG)
    {
        features_tree_notify(1);
        features_bigtree_scaled(0);
        features_bigtree_reset();
        ok = feature_bigtree(w, cr, x, y, z, 0, 0);
    }
    else if (gen == G_TREES)
    {
        features_tree_notify(1);
        ok = feature_trees(w, cr, x, y, z, 0, 4);
    }
    else if (gen == G_TAIGA2)
    {
        features_tree_notify(1);
        ok = feature_taiga2(w, cr, x, y, z, 0, 0);
    }
    else if (gen == G_MEGAPINE)
    {
        /* WorldGenMegaPineTree is built with doBlockNotify false */
        features_tree_notify(0);
        int tall = jr_bool(cr);
        ok = feature_megapine_grow(w, cr, x + dx, y, z + dz, tall);
    }
    else if (gen == G_FOREST)
    {
        features_tree_notify(1);
        ok = feature_forest(w, cr, x, y, z, 0, 0);
    }
    else if (gen == G_MEGAJUNGLE)
    {
        features_tree_notify(1);
        ok = feature_megajungle(w, cr, x + dx, y, z + dz, 0, 0);
    }
    else if (gen == G_SMALL_JUNGLE)
    {
        features_tree_notify(1);
        int h = 4 + jr_int_n(cr, 7);
        ok = features_sapling_jungle(w, cr, x, y, z, h);
    }
    else if (gen == G_SAVANNA)
    {
        features_tree_notify(1);
        ok = feature_savanna(w, cr, x, y, z, 0, 0);
    }
    else if (gen == G_CANOPY)
    {
        features_tree_notify(1);
        ok = feature_canopy(w, cr, x + dx, y, z + dz, 0, 0);
    }

    features_tree_notify(0);

    if (!ok)
    {
        if (matched)
        {
            world_set_block(w, x + dx, y, z + dz, RT_SAPLING, type, 4);
            world_set_block(w, x + dx + 1, y, z + dz, RT_SAPLING, type, 4);
            world_set_block(w, x + dx, y, z + dz + 1, RT_SAPLING, type, 4);
            world_set_block(w, x + dx + 1, y, z + dz + 1, RT_SAPLING, type, 4);
        }
        else
        {
            world_set_block(w, x, y, z, RT_SAPLING, type, 4);
        }
    }
}

/* BlockSapling.updateTick. */
static void rt_sapling(struct world *w, jrand *wr, jrand *cr, int x, int y, int z)
{
    bush_tick(w, wr, RT_SAPLING, x, y, z);

    if (block_light(w, x, y + 1, z) >= 9 && jr_int_n(cr, 7) == 0)
    {
        int meta = world_get_meta(w, x, y, z);

        if ((meta & 8) == 0) world_set_meta(w, x, y, z, meta | 8, 4);
        else sapling_grow(w, cr, x, y, z);
    }
}

/* --------------------------------------------------- cactus and reeds */

/* BlockCactus.updateTick. */
static void rt_cactus(struct world *w, jrand *wr, int x, int y, int z)
{
    if (!air_at(w, x, y + 1, z)) return;

    int var6 = 1;

    while (world_get_block(w, x, y - var6, z) == RT_CACTUS) ++var6;

    if (var6 >= 3) return;

    int var7 = world_get_meta(w, x, y, z);

    if (var7 == 15)
    {
        /* setBlock(x, y + 1, z, this) (meta 0, flags 3), then
         * this.onNeighborBlockChange on the new cell: a solid block beside
         * it pops the new cactus at once (func_147480_a with its drop).
         * When setBlock's own notifications already popped it (the cactus
         * under it could not stay), func_147480_a finds air and does
         * nothing, so the call is skipped */
        world_set_block(w, x, y + 1, z, RT_CACTUS, 0, 3);
        world_set_meta(w, x, y, z, 0, 4);
        if (!air_at(w, x, y + 1, z))
            block_on_neighbor_changed(w, RT_CACTUS, world_get_meta(w, x, y + 1, z), x, y + 1, z, RT_CACTUS);
    }
    else
    {
        world_set_meta(w, x, y, z, var7 + 1, 4);
    }
}

/* BlockReed.canPlaceBlockAt, which canBlockStay delegates to. */
static int reed_can_stay(struct world *w, int x, int y, int z)
{
    int below = world_get_block(w, x, y - 1, z) & 4095;

    if (below == RT_REEDS) return 1;
    if (below != RT_GRASS && below != RT_DIRT && below != 12) return 0;

    return mat_is(w, x - 1, y - 1, z, MATI_WATER) || mat_is(w, x + 1, y - 1, z, MATI_WATER) ||
           mat_is(w, x, y - 1, z - 1, MATI_WATER) || mat_is(w, x, y - 1, z + 1, MATI_WATER);
}

/* BlockReed.func_150170_e: drop and remove the reed when it cannot stay. */
static int reed_check(struct world *w, int x, int y, int z)
{
    if (reed_can_stay(w, x, y, z)) return 1;

    /* BlockReed.func_150170_e: checkAndDropBlock's order, the reed item's
     * dropBlockAsItem (and the item) before the flags-3 setBlockToAir; the
     * drop draws from the stream randomtick_update_tick published */
    randomtick_drop(w, x, y, z, RT_REEDS, world_get_meta(w, x, y, z));
    world_set_block(w, x, y, z, 0, 0, 3);
    return 0;
}

/* BlockReed.updateTick. */
static void rt_reed(struct world *w, jrand *wr, int x, int y, int z)
{
    if (world_get_block(w, x, y - 1, z) != RT_REEDS && !reed_check(w, x, y, z)) return;

    if (!air_at(w, x, y + 1, z)) return;

    int var6 = 1;

    while (world_get_block(w, x, y - var6, z) == RT_REEDS) ++var6;

    if (var6 >= 3) return;

    int var7 = world_get_meta(w, x, y, z);

    if (var7 == 15)
    {
        world_set_block(w, x, y + 1, z, RT_REEDS, 0, 3);
        world_set_meta(w, x, y, z, 0, 4);
    }
    else
    {
        world_set_meta(w, x, y, z, var7 + 1, 4);
    }
}

/* ------------------------------------------------------------ the vine */

/* BlockVine.func_150093_a. */
static int vine_attaches(int id)
{
    return BLOCKS[id & 4095].normal_block && MATERIALS[BLOCKS[id & 4095].material].blocks_movement;
}

/* Direction.offsetX and offsetZ, bits 0..3. */
static const int RT_VINE_DX[4] = {0, -1, 0, 1};
static const int RT_VINE_DZ[4] = {1, 0, -1, 0};

/* Direction.facingToDirection. */
static const int RT_FACING_DIR[6] = {-1, -1, 2, 0, 1, 3};

/* BlockVine.updateTick. The gate and every roll draw from World.rand. */
static void rt_vine(struct world *w, jrand *wr, int x, int y, int z)
{
    if (jr_int_n(wr, 4) != 0) return;

    int var7 = 5;
    int crowded = 0;

    for (int vx = x - 4; vx <= x + 4 && !crowded; ++vx)
        for (int vz = z - 4; vz <= z + 4 && !crowded; ++vz)
            for (int vy = y - 1; vy <= y + 1; ++vy)
                if (world_get_block(w, vx, vy, vz) == RT_VINE)
                {
                    if (--var7 <= 0)
                    {
                        crowded = 1;
                        break;
                    }
                }

    int meta = world_get_meta(w, x, y, z);
    int facing = jr_int_n(wr, 6);
    int dir = RT_FACING_DIR[facing];

    if (facing == 1 && y < 255 && air_at(w, x, y + 1, z))
    {
        if (crowded) return;

        int var15 = jr_int_n(wr, 16) & meta;

        if (var15 > 0)
        {
            for (int i = 0; i <= 3; ++i)
                if (!vine_attaches(world_get_block(w, x + RT_VINE_DX[i], y + 1, z + RT_VINE_DZ[i])))
                    var15 &= ~(1 << i);

            if (var15 > 0) world_set_block(w, x, y + 1, z, RT_VINE, var15, 2);
        }
    }
    else if (facing >= 2 && facing <= 5 && (meta & 1 << dir) == 0)
    {
        if (crowded) return;

        int other = world_get_block(w, x + RT_VINE_DX[dir], y, z + RT_VINE_DZ[dir]);

        if (mat_id_is(other, MATI_AIR))
        {
            int d1 = (dir + 1) & 3;
            int d2 = (dir + 3) & 3;

            if ((meta & 1 << d1) != 0 &&
                vine_attaches(world_get_block(w, x + RT_VINE_DX[dir] + RT_VINE_DX[d1], y, z + RT_VINE_DZ[dir] + RT_VINE_DZ[d1])))
            {
                world_set_block(w, x + RT_VINE_DX[dir], y, z + RT_VINE_DZ[dir], RT_VINE, 1 << d1, 2);
            }
            else if ((meta & 1 << d2) != 0 && vine_attaches(world_get_block(w, x + RT_VINE_DX[dir] + RT_VINE_DX[d2], y, z + RT_VINE_DZ[dir] + RT_VINE_DZ[d2])))
            {
                world_set_block(w, x + RT_VINE_DX[dir], y, z + RT_VINE_DZ[dir], RT_VINE, 1 << d2, 2);
            }
            else if ((meta & 1 << d1) != 0 &&
                     air_at(w, x + RT_VINE_DX[dir] + RT_VINE_DX[d1], y, z + RT_VINE_DZ[dir] + RT_VINE_DZ[d1]) &&
                     vine_attaches(world_get_block(w, x + RT_VINE_DX[d1], y, z + RT_VINE_DZ[d1])))
            {
                world_set_block(w, x + RT_VINE_DX[dir] + RT_VINE_DX[d1], y, z + RT_VINE_DZ[dir] + RT_VINE_DZ[d1],
                                RT_VINE, 1 << ((dir + 2) & 3), 2);
            }
            else if ((meta & 1 << d2) != 0 &&
                     air_at(w, x + RT_VINE_DX[dir] + RT_VINE_DX[d2], y, z + RT_VINE_DZ[dir] + RT_VINE_DZ[d2]) &&
                     vine_attaches(world_get_block(w, x + RT_VINE_DX[d2], y, z + RT_VINE_DZ[d2])))
            {
                world_set_block(w, x + RT_VINE_DX[dir] + RT_VINE_DX[d2], y, z + RT_VINE_DZ[dir] + RT_VINE_DZ[d2], RT_VINE,
                                1 << ((dir + 2) & 3), 2);
            }
            else if (vine_attaches(world_get_block(w, x + RT_VINE_DX[dir], y + 1, z + RT_VINE_DZ[dir])))
            {
                world_set_block(w, x + RT_VINE_DX[dir], y, z + RT_VINE_DZ[dir], RT_VINE, 0, 2);
            }
        }
        else if (MATERIALS[BLOCKS[other & 4095].material].is_opaque && BLOCKS[other & 4095].normal_block)
        {
            world_set_meta(w, x, y, z, meta | 1 << dir, 2);
        }
    }
    else if (y > 1)
    {
        int below = world_get_block(w, x, y - 1, z);

        if (mat_id_is(below, MATI_AIR))
        {
            int var13 = jr_int_n(wr, 16) & meta;

            if (var13 > 0) world_set_block(w, x, y - 1, z, RT_VINE, var13, 2);
        }
        else if ((below & 4095) == RT_VINE)
        {
            int var13 = jr_int_n(wr, 16) & meta;
            int var14 = world_get_meta(w, x, y - 1, z);

            if (var14 != (var14 | var13)) world_set_meta(w, x, y - 1, z, var14 | var13, 2);
        }
    }
}

/* ----------------------------------------------------------- the rest */

/* BlockCocoa.dropBlockAsItemWithChance: one dye at damage 3 below age 2,
 * three at age 2, no chance roll. */
static void spend_cocoa_drop(jrand *wr, int x, int y, int z, int meta)
{
    for (int i = 0, n = ((meta & 12) >> 2) >= 2 ? 3 : 1; i < n; ++i) drop_do(wr, x, y, z, 351, 3, 1);
}

/* BlockCocoa.updateTick. */
static void rt_cocoa(struct world *w, jrand *wr, int x, int y, int z)
{
    int meta = world_get_meta(w, x, y, z);
    int dir = meta & 3;
    int host = world_get_block(w, x + RT_VINE_DX[dir], y, z + RT_VINE_DZ[dir]) & 4095;

    if (host != RT_LOG || (world_get_meta(w, x + RT_VINE_DX[dir], y, z + RT_VINE_DZ[dir]) & 3) != 3)
    {
        spend_cocoa_drop(wr, x, y, z, meta);
        world_set_block(w, x, y, z, 0, 0, 2);
    }
    else if (jr_int_n(wr, 5) == 0)
    {
        int age = (meta & 12) >> 2;

        if (age < 2) world_set_meta(w, x, y, z, (age + 1) << 2 | dir, 2);
    }
}

/* BlockNetherWart.updateTick. */
static void rt_netherwart(struct world *w, jrand *wr, jrand *cr, int x, int y, int z)
{
    int meta = world_get_meta(w, x, y, z);

    if (meta < 3 && jr_int_n(cr, 10) == 0) world_set_meta(w, x, y, z, meta + 1, 2);

    bush_tick(w, wr, RT_NETHER_WART, x, y, z);
}

/* BlockMushroom.updateTick. */
static void rt_mushroom(struct world *w, jrand *wr, jrand *cr, int x, int y, int z)
{
    (void)wr;

    if (jr_int_n(cr, 25) != 0) return;

    int self = world_get_block(w, x, y, z) & 4095;
    int var7 = 5;

    for (int mx = x - 4; mx <= x + 4; ++mx)
        for (int mz = z - 4; mz <= z + 4; ++mz)
            for (int my = y - 1; my <= y + 1; ++my)
                if (world_get_block(w, mx, my, mz) == self)
                {
                    if (--var7 <= 0) return;
                }

    int tx = x + jr_int_n(cr, 3) - 1;
    int ty = y + jr_int_n(cr, 2) - jr_int_n(cr, 2);
    int tz = z + jr_int_n(cr, 3) - 1;

    for (int i = 0; i < 4; ++i)
    {
        if (air_at(w, tx, ty, tz) && bush_floor_ok(self, world_get_block(w, tx, ty - 1, tz), w, tx, ty, tz))
        {
            x = tx;
            y = ty;
            z = tz;
        }

        tx = x + jr_int_n(cr, 3) - 1;
        ty = y + jr_int_n(cr, 2) - jr_int_n(cr, 2);
        tz = z + jr_int_n(cr, 3) - 1;
    }

    if (air_at(w, tx, ty, tz) && bush_floor_ok(self, world_get_block(w, tx, ty - 1, tz), w, tx, ty, tz))
        world_set_block(w, tx, ty, tz, self, 0, 2);
}

/* BlockIce.updateTick: melt above block light 11 - the light opacity. */
static void rt_ice(struct world *w, int x, int y, int z)
{
    if (saved_block_light(w, x, y, z) <= 11 - BLOCKS[RT_ICE].opacity) return;

    /* isHellWorld: the Nether melts it to air, with no drop */
    if (w->dim == -1)
    {
        world_set_block(w, x, y, z, 0, 0, 3);
        return;
    }

    /* the drop draws nothing (quantityDropped 0) */
    world_set_block(w, x, y, z, 9, 0, 3); /* Blocks.water, the static form */
}

/* BlockSnow.updateTick: melt above block light 11. quantityDropped is 0, so
 * the drop draws nothing. */
static void rt_snow(struct world *w, int x, int y, int z)
{
    if (saved_block_light(w, x, y, z) <= 11) return;

    world_set_block(w, x, y, z, 0, 0, 3);
}

/* BlockFarmland.func_149821_m: water anywhere in the 9x2x9 box. */
static int farmland_water(struct world *w, int x, int y, int z)
{
    for (int vx = x - 4; vx <= x + 4; ++vx)
        for (int vy = y; vy <= y + 1; ++vy)
            for (int vz = z - 4; vz <= z + 4; ++vz)
                if (mat_is(w, vx, vy, vz, MATI_WATER)) return 1;

    return 0;
}

/* BlockFarmland.func_149822_e: a crop directly above keeps dry farmland. */
static int farmland_crop(struct world *w, int x, int y, int z)
{
    int above = world_get_block(w, x, y + 1, z) & 4095;

    return above == RT_WHEAT || above == RT_MELON_STEM || above == RT_PUMPKIN_STEM || above == 141 || above == 142;
}

/* BlockFarmland.updateTick: water within 4, or rain falling on the block
 * above (World.canLightningStrikeAt), keeps it wet. */
static void rt_farmland(struct world *w, int x, int y, int z)
{
    if (farmland_water(w, x, y, z) || fire_can_lightning_strike_at(w, x, y + 1, z))
    {
        world_set_meta(w, x, y, z, 7, 2);
        return;
    }

    int meta = world_get_meta(w, x, y, z);

    if (meta > 0) world_set_meta(w, x, y, z, meta - 1, 2);
    else if (!farmland_crop(w, x, y, z)) world_set_block(w, x, y, z, RT_DIRT, 0, 3);
}

/* BlockRedstoneOre.updateTick: only the lit block ticks, back to unlit. */
static void rt_redstone(struct world *w, int id, int x, int y, int z)
{
    if ((id & 4095) != RT_LIT_REDSTONE_ORE) return;

    world_set_block(w, x, y, z, RT_REDSTONE_ORE, 0, 3);
}

/* ------------------------------------------------------- the dispatch */

void randomtick_update_tick(struct world *w, int id, jrand *wr, jrand *cr, int x, int y, int z)
{
    jrand *previous = randomtick_wr;
    randomtick_tick_rand(wr);

    switch (id & 4095)
    {
    case RT_GRASS:
        grass_like(w, cr, x, y, z, RT_GRASS);
        break;

    case RT_MYCELIUM:
        grass_like(w, cr, x, y, z, RT_MYCELIUM);
        break;

    case RT_LEAVES:
    case RT_LEAVES2:
        rt_leaves(w, wr, id, x, y, z);
        break;

    case RT_WHEAT:
    case RT_CARROTS:
    case RT_POTATOES:
        rt_crops(w, wr, cr, x, y, z);
        break;

    case RT_SAPLING:
        rt_sapling(w, wr, cr, x, y, z);
        break;

    case RT_CACTUS:
        rt_cactus(w, wr, x, y, z);
        break;

    case RT_REEDS:
        rt_reed(w, wr, x, y, z);
        break;

    case RT_VINE:
        rt_vine(w, wr, x, y, z);
        break;

    case RT_PUMPKIN_STEM:
        rt_stem(w, wr, cr, x, y, z, RT_PUMPKIN);
        break;

    case RT_MELON_STEM:
        rt_stem(w, wr, cr, x, y, z, RT_MELON_BLOCK);
        break;

    case RT_COCOA:
        rt_cocoa(w, wr, x, y, z);
        break;

    case RT_NETHER_WART:
        rt_netherwart(w, wr, cr, x, y, z);
        break;

    case 31: /* BlockTallGrass: BlockBush's canBlockStay check */
    case 32: /* BlockDeadBush */
    case 37: /* BlockFlower */
    case 38:
    case 111: /* BlockLilyPad */
        bush_tick(w, wr, id & 4095, x, y, z);
        break;

    case 175: /* BlockDoublePlant */
        double_plant_tick(w, wr, x, y, z);
        break;

    case RT_BROWN_MUSHROOM:
    case RT_RED_MUSHROOM:
        rt_mushroom(w, wr, cr, x, y, z);
        break;

    case RT_ICE:
        rt_ice(w, x, y, z);
        break;

    case RT_SNOW_LAYER:
        rt_snow(w, x, y, z);
        break;

    case RT_FARMLAND:
        rt_farmland(w, x, y, z);
        break;

    case RT_REDSTONE_ORE:
    case RT_LIT_REDSTONE_ORE:
        rt_redstone(w, id, x, y, z);
        break;

    case FIRE_BLOCK:
        fire_update_tick(w, wr, x, y, z);
        break;

    default:
        /* Block's empty updateTick */
        break;
    }

    randomtick_tick_rand(previous);
}

/* --------------------------------------------------------------- bonemeal */

/* Block ids the bonemeal bodies name that the random ticks above do not. */
enum {
    BM_TALLGRASS = 31, BM_YELLOW_FLOWER = 37, BM_RED_FLOWER = 38,
    BM_CARROTS = 141, BM_POTATOES = 142, BM_DOUBLE_PLANT = 175,
};

int randomtick_is_igrowable(int id)
{
    switch (id & 4095)
    {
    case RT_GRASS:
    case RT_SAPLING:
    case BM_TALLGRASS:
    case RT_WHEAT:
    case BM_CARROTS:   /* BlockCarrot extends BlockCrops */
    case BM_POTATOES:  /* BlockPotato extends BlockCrops */
    case RT_PUMPKIN_STEM:
    case RT_MELON_STEM:
    case RT_BROWN_MUSHROOM:
    case RT_RED_MUSHROOM:
    case RT_COCOA:
    case BM_DOUBLE_PLANT:
        return 1;

    default:
        return 0;
    }
}

/* BlockDoublePlant.func_149885_e: the variant, from the lower half. */
static int double_plant_variant(struct world *w, int x, int y, int z)
{
    int meta = world_get_meta(w, x, y, z);

    return (meta & 8) == 0 ? meta & 7 : world_get_meta(w, x, y - 1, z) & 7;
}

/* IGrowable.func_149851_a. */
int randomtick_bonemeal_can(struct world *w, int id, int x, int y, int z)
{
    switch (id & 4095)
    {
    case BM_TALLGRASS:
        return world_get_meta(w, x, y, z) != 0;

    case RT_WHEAT:
    case BM_CARROTS:
    case BM_POTATOES:
    case RT_PUMPKIN_STEM:
    case RT_MELON_STEM:
        return world_get_meta(w, x, y, z) != 7;

    case RT_COCOA:
        return ((world_get_meta(w, x, y, z) & 12) >> 2) < 2;

    case BM_DOUBLE_PLANT:
    {
        int v = double_plant_variant(w, x, y, z);

        return v != 2 && v != 3;
    }

    default:
        /* BlockGrass, BlockSapling and BlockMushroom answer true */
        return 1;
    }
}

/* IGrowable.func_149852_a, one World.rand draw for the blocks that roll. */
int randomtick_bonemeal_chance(struct world *w, int id, jrand *wr, int x, int y, int z)
{
    (void)w;
    (void)x;
    (void)y;
    (void)z;

    switch (id & 4095)
    {
    case RT_SAPLING:
        return (double)jr_float(wr) < 0.45;

    case RT_BROWN_MUSHROOM:
    case RT_RED_MUSHROOM:
        return (double)jr_float(wr) < 0.4;

    default:
        return 1;
    }
}

/* BlockFlower.field_149859_a[index]: func_149857_e finds the name in the red
 * flower's list and func_149856_f makes its index the metadata (the
 * dandelion, field_149858_b, is the callers' own branch). */
static void flower_block(int index, int *block, int *meta)
{
    *block = BM_RED_FLOWER;
    *meta = index;
}

/* BiomeGenBase.field_150606_ad: NoiseGeneratorPerlin(new Random(2345L), 1). */
static struct perlin flower_noise;

/* before main: the same for every environment */
__attribute__((constructor)) static void flower_noise_init(void)
{
    jrand t;

    jr_seed(&t, 2345L);
    perlin_init(&flower_noise, &t, 1);
}

/* BiomeGenBase.func_150572_a and the three overrides (Plains, Forest,
 * Swamp); every other biome, including the mutated ones, keeps the base
 * body. Draws in the same order the Java bodies do. */
static void flower_pick(struct world *w, jrand *r, int x, int y, int z, int *block, int *meta)
{
    (void)y;

    int biome = biome_at(w, x, z);

    if (biome == 6) /* BiomeGenSwamp: field_149859_a[1] */
    {
        *block = BM_RED_FLOWER;
        *meta = 1;
        return;
    }

    if (biome == 1) /* BiomeGenPlains */
    {
        double v = perlin_point(&flower_noise, (double)x / 200.0, (double)z / 200.0);

        if (v < -0.8)
        {
            flower_block(4 + jr_int_n(r, 4), block, meta);
            return;
        }

        if (jr_int_n(r, 3) > 0)
        {
            int k = jr_int_n(r, 3);
            flower_block(k == 0 ? 0 : (k == 1 ? 3 : 8), block, meta);
            return;
        }

        *block = BM_YELLOW_FLOWER;
        *meta = 0;
        return;
    }

    if (biome == 132) /* BiomeGenForest with field_150632_aF == 1, Flower Forest */
    {
        double v = (1.0 + perlin_point(&flower_noise, (double)x / 48.0, (double)z / 48.0)) / 2.0;

        if (v < 0.0) v = 0.0;
        if (v > 0.9999) v = 0.9999;

        int k = (int)(v * 9.0);

        if (k == 1) k = 0;
        flower_block(k, block, meta);
        return;
    }

    /* BiomeGenBase.func_150572_a */
    if (jr_int_n(r, 3) > 0)
    {
        *block = BM_YELLOW_FLOWER;
        *meta = 0;
    }
    else
    {
        *block = BM_RED_FLOWER;
        *meta = 0;
    }
}

/* BlockGrass.func_149853_b: 128 tries, a random walk while the cell below is
 * grass, then a tall grass or a biome flower. */
static void grass_bonemeal(struct world *w, jrand *r, int x, int y, int z)
{
    for (int n = 0; n < 128; ++n)
    {
        int var7 = x, var8 = y + 1, var9 = z, var10 = 0;

        for (;;)
        {
            if (var10 < n / 16)
            {
                var7 += jr_int_n(r, 3) - 1;

                /* Java evaluates (nextInt(3) - 1) * nextInt(3) / 2 left to
                 * right; the two draws stay separate statements. */
                int a = jr_int_n(r, 3) - 1;
                int b = jr_int_n(r, 3);

                var8 += a * b / 2;
                var9 += jr_int_n(r, 3) - 1;

                if (world_get_block(w, var7, var8 - 1, var9) == RT_GRASS &&
                    !BLOCKS[world_get_block(w, var7, var8, var9) & 4095].normal_cube)
                {
                    ++var10;
                    continue;
                }
            }
            else if (world_get_block(w, var7, var8, var9) == 0)
            {
                if (jr_int_n(r, 8) != 0)
                {
                    if (bush_floor_ok(BM_TALLGRASS, world_get_block(w, var7, var8 - 1, var9), w, var7, var8, var9))
                        world_set_block(w, var7, var8, var9, BM_TALLGRASS, 1, 3);
                }
                else
                {
                    int fb, fm;

                    flower_pick(w, r, var7, var8, var9, &fb, &fm);

                    if (bush_floor_ok(fb, world_get_block(w, var7, var8 - 1, var9), w, var7, var8, var9))
                        world_set_block(w, var7, var8, var9, fb, fm, 3);
                }
            }

            break;
        }
    }
}

/* Block.canPlaceBlockAt over a registered block: its Material is replaceable. */
static int replaceable_at(struct world *w, int x, int y, int z)
{
    return MATERIALS[BLOCKS[world_get_block(w, x, y, z) & 4095].material].replaceable;
}

/* BlockDoublePlant.func_149889_c: the lower and upper halves. */
static void double_plant_place(struct world *w, int x, int y, int z, int variant, int flags)
{
    world_set_block(w, x, y, z, BM_DOUBLE_PLANT, variant, flags);
    world_set_block(w, x, y + 1, z, BM_DOUBLE_PLANT, 8, flags);
}

/* BlockTallGrass.func_149853_b: a double plant where tall grass will take it
 * (BlockDoublePlant.canPlaceBlockAt: replaceable here, a bush floor below, air
 * above). */
static void tallgrass_bonemeal(struct world *w, int x, int y, int z)
{
    int meta = world_get_meta(w, x, y, z);
    int variant = meta == 2 ? 3 : 2;

    if (replaceable_at(w, x, y, z) &&
        bush_floor_ok(BM_DOUBLE_PLANT, world_get_block(w, x, y - 1, z), w, x, y, z) &&
        world_get_block(w, x, y + 1, z) == 0)
        double_plant_place(w, x, y, z, variant, 2);
}

/* BlockSapling.func_149853_b -> func_149879_c: the stage bit, then the tree. */
static void sapling_bonemeal(struct world *w, jrand *wr, int x, int y, int z)
{
    int meta = world_get_meta(w, x, y, z);

    if ((meta & 8) == 0) world_set_meta(w, x, y, z, meta | 8, 4);
    else sapling_grow(w, wr, x, y, z);
}

/* BlockMushroom.func_149853_b -> func_149884_c: clear the mushroom, try the
 * giant one for its colour, put the mushroom back when that fails. */
static void mushroom_bonemeal(struct world *w, jrand *wr, int x, int y, int z)
{
    int meta = world_get_meta(w, x, y, z);
    int self = world_get_block(w, x, y, z) & 4095;
    int type = self == RT_BROWN_MUSHROOM ? 0 : (self == RT_RED_MUSHROOM ? 1 : -1);
    int ok = 0;

    world_set_block(w, x, y, z, 0, 0, 3);

    if (type >= 0)
    {
        int n;
        const struct feature *rows = features_for("bigmushroom", &n);

        for (int i = 0; i < n; ++i)
        {
            if (rows[i].block != type) continue;

            ok = rows[i].generate(w, wr, x, y, z, rows[i].block, rows[i].count);
            break;
        }
    }

    if (!ok) world_set_block(w, x, y, z, self, meta, 3);
}

/* IGrowable.func_149853_b. */
void randomtick_bonemeal_grow(struct world *w, int id, jrand *wr, int x, int y, int z)
{
    switch (id & 4095)
    {
    case RT_GRASS:
        grass_bonemeal(w, wr, x, y, z);
        break;

    case BM_TALLGRASS:
        tallgrass_bonemeal(w, x, y, z);
        break;

    case RT_SAPLING:
        sapling_bonemeal(w, wr, x, y, z);
        break;

    case RT_WHEAT:
    case BM_CARROTS:
    case BM_POTATOES:
    case RT_PUMPKIN_STEM:
    case RT_MELON_STEM:
    {
        /* BlockCrops.func_149863_m / BlockStem.func_149874_m */
        int meta = world_get_meta(w, x, y, z) + jr_int_n(wr, 4) + 2;

        if (meta > 7) meta = 7;
        world_set_meta(w, x, y, z, meta, 2);
        break;
    }

    case RT_COCOA:
    {
        int meta = world_get_meta(w, x, y, z);
        int dir = meta & 3;
        int age = ((meta & 12) >> 2) + 1;

        world_set_meta(w, x, y, z, age << 2 | dir, 2);
        break;
    }

    case RT_BROWN_MUSHROOM:
    case RT_RED_MUSHROOM:
        mushroom_bonemeal(w, wr, x, y, z);
        break;

    case BM_DOUBLE_PLANT:
    {
        int v = double_plant_variant(w, x, y, z);

        drop_do(wr, x, y, z, BM_DOUBLE_PLANT, v, 1);
        break;
    }

    default:
        break;
    }
}
