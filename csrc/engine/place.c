/* Block placement from items, ported from oracle/src (ItemStack,
 * ItemBlock and its subclasses, ItemReed, ItemDoor, ItemBed, ItemSign,
 * ItemSeeds, ItemSeedFood, ItemSkull, ItemHoe, ItemRedstone, ItemEnderEye,
 * ItemDye's cocoa) and the placement rules every block carries (onBlockPlaced,
 * onBlockPlacedBy, onPostBlockPlaced, canPlaceBlockAt, canPlaceBlockOnSide).
 * The callbacks the stored block runs (onBlockAdded, the neighbour
 * notification) stay in blockcb.c, which calls back into place.c for the
 * rails and the self-drops.
 *
 * Entities are not here: the probe world holds none and the placer is not one
 * of them, so every World.checkNoEntityCollision answers yes and the placed
 * block's collision box is never consulted (a box is only ever asked about
 * entities). The drop paths a pure placement can fire are BlockReed's and the
 * rails'; they go through place_block_drop against the streams
 * place_set_case owns. */
#include "comparator.h"
#include "place.h"
#include "collide.h"
#include "jmath.h"
#include "env.h"
#include "collide.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "blocks.h"
#include "drops.h"
#include "items.h"
#include "ticks.h"

/* The per-case streams: the world Random (a plain Random the probe reseeds
 * per case), the shared Math.random stream, the shared Item.itemRand split and
 * the Det state whose seeder each spawned entity's rand and UUID spend. */
#define case_rand (nw_env->place.case_rand)
#define case_det (nw_env->place.case_det)
#define case_math (nw_env->place.case_math)
#define case_item (nw_env->place.case_item)
#define case_role (nw_env->place.case_role)

/* The self-drop (BlockReed, the rails), defined at the tail. */
void place_block_drop(struct world *w, int x, int y, int z, int block, int meta);

/* ------------------------------------------------------------ registry reads */

static int block_at(struct world *w, int x, int y, int z)
{
    return world_get_block(w, x, y, z) & 4095;
}

static int meta_at(struct world *w, int x, int y, int z)
{
    return world_get_meta(w, x, y, z);
}

static int mat_named(const char *name)
{
    for (size_t i = 0; i < sizeof MATERIALS / sizeof MATERIALS[0]; ++i)
        if (MATERIALS[i].name != NULL && strcmp(MATERIALS[i].name, name) == 0) return (int)i;

    return -1;
}

static int mat_of(int id)
{
    return BLOCKS[id & 4095].material;
}

static int is_block_class(int id, const char *name)
{
    const char *n = BLOCKS[id & 4095].class_name;
    return n != NULL && strcmp(n, name) == 0;
}

static int is_item_class(int id, const char *name)
{
    const char *n = ITEMS[id & 4095].class_name;
    return n != NULL && strcmp(n, name) == 0;
}

static int is_slab(int id)
{
    return is_block_class(id, "BlockStoneSlab") || is_block_class(id, "BlockWoodSlab");
}

/* The double slab forms (field_150004_a), which ItemSlab's own items place. */
static int is_double_slab(int id)
{
    return id == 43 || id == 125;
}

static int is_replaceable(int id)
{
    return MATERIALS[mat_of(id)].replaceable;
}

static int is_solid_mat(int id)
{
    return MATERIALS[mat_of(id)].is_solid;
}

static int is_circuits(int id)
{
    return mat_of(id) == mat_named("circuits");
}

static int is_air_block(struct world *w, int x, int y, int z)
{
    return mat_of(block_at(w, x, y, z)) == mat_named("air");
}

/* Block.isNormalCube, read through World.getBlock. */
static int normal_cube_at(struct world *w, int x, int y, int z)
{
    return BLOCKS[block_at(w, x, y, z)].normal_cube;
}

/* BlockFire.func_149843_e: the encouragement array func_149844_e reads. Only
 * these blocks burn near a fire; a burnable material alone does not. */
static const int FIRE_ENCOURAGES[] = {5, 125, 126, 85, 53, 134, 135, 136, 17, 162, 18, 161,
                                      47, 46, 31, 175, 37, 38, 35, 106, 173, 170, 171};

static int fire_flammable_near(struct world *w, int x, int y, int z)
{
    static const int offs[6][3] = {{0, 1, 0}, {0, -1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};

    for (int i = 0; i < 6; ++i)
    {
        int b = block_at(w, x + offs[i][0], y + offs[i][1], z + offs[i][2]);

        for (size_t k = 0; k < sizeof FIRE_ENCOURAGES / sizeof FIRE_ENCOURAGES[0]; ++k)
            if (b == FIRE_ENCOURAGES[k]) return 1;
    }

    return 0;
}

/* World.doesBlockHaveSolidTopSurface. */
static int solid_top(struct world *w, int x, int y, int z)
{
    int b = block_at(w, x, y, z);
    int m = meta_at(w, x, y, z);

    if (MATERIALS[mat_of(b)].is_opaque && BLOCKS[b].normal_block) return 1;
    if (is_block_class(b, "BlockStairs")) return (m & 4) == 4;
    if (is_slab(b)) return (m & 8) == 8;
    if (is_block_class(b, "BlockHopper")) return 1;
    if (is_block_class(b, "BlockSnow")) return (m & 7) == 7;
    return 0;
}

/* World.isBlockNormalCubeDefault: the chunk must already be there (provideChunk
 * does not load it), and the loaded block must be opaque and render normally. */
static int normal_cube_default(struct world *w, int x, int y, int z, int dflt)
{
    if (x < -30000000 || x >= 30000000 || z < -30000000 || z >= 30000000) return dflt;

    if (world_chunk(w, x >> 4, z >> 4) == NULL) return dflt;

    int b = block_at(w, x, y, z);

    /* var6.getMaterial().isOpaque() && var6.renderAsNormalBlock(): the
     * material's opacity, not the block's isOpaqueCube override (the end
     * portal frame's material is rock but isOpaqueCube is false) */
    return MATERIALS[mat_of(b)].is_opaque && BLOCKS[b].normal_block;
}

/* ------------------------------------------------------- the yaw quadrants
 * MathHelper.floor_double(yaw * f / 360 + offset) & mask, per call shape:
 * Java computes `rotationYaw * 4.0F / 360.0F` in float precision and only
 * then widens. mh_floor saturates, so yaw 3e11 or -3e11 floors to
 * Integer.MAX_VALUE and NaN to 0, as the oracle does. */
static int yaw_quad(float yaw, float factor, double offset)
{
    float t = yaw * factor / 360.0F;

    return mh_floor((double)t + offset);
}

/* ------------------------------------------------------- the direction tables
 * util/Direction and Facing. */
static const int DIR_OFF_X[4] = {0, -1, 0, 1};
static const int DIR_OFF_Z[4] = {1, 0, -1, 0};
static const int DIR_FACING_TO_DIRECTION[6] = {-1, -1, 2, 0, 1, 3};
static const int DIR_ROTATE_OPPOSITE[4] = {2, 3, 0, 1};
static const int DIR_ROTATE_RIGHT[4] = {1, 2, 3, 0};
static const int FACING_OPPOSITE_SIDE[6] = {1, 0, 3, 2, 5, 4};

/* World.isBlockIndirectlyGettingPowered: every answer runs through
 * isBlockProvidingPowerTo, which a redstone-off world shorts to 0. */
static int indirectly_powered(struct world *w, int x, int y, int z)
{
    (void)w; (void)x; (void)y; (void)z;
    return 0;
}

/* ------------------------------------------------------------- items and ids */

/* The ids ItemReed's constructor names, by item. */
static int reed_block(int item)
{
    switch (item & 4095)
    {
    case 287: return 132;   /* string -> tripwire */
    case 338: return 83;    /* reeds */
    case 354: return 92;    /* cake */
    case 356: return 93;    /* repeater */
    case 379: return 117;   /* brewing stand */
    case 380: return 118;   /* cauldron */
    case 390: return 140;   /* flower pot */
    case 404: return 149;   /* comparator */
    default: return -1;
    }
}

/* The block and the soil an ItemSeeds / ItemSeedFood plants. */
static int seed_block(int item)
{
    switch (item & 4095)
    {
    case 295: return 59;    /* wheat seeds */
    case 361: return 104;   /* pumpkin seeds */
    case 362: return 105;   /* melon seeds */
    case 372: return 115;   /* nether wart */
    case 391: return 141;   /* carrot */
    case 392: return 142;   /* potato */
    default: return -1;
    }
}

static int seed_soil(int item)
{
    switch (item & 4095)
    {
    case 372: return 88;    /* soul sand */
    default: return 60;     /* farmland */
    }
}

/* Item.getMetadata, by item class: every ItemBlock subclass answers its
 * damage except the two overrides. */
static int item_get_metadata(int item, int damage)
{
    if (is_item_class(item, "ItemLeaves")) return damage | 4;
    if (is_item_class(item, "ItemPiston")) return 7;
    if (is_item_class(item, "ItemAnvilBlock")) return damage << 2;

    return damage;   /* ItemBlock, ItemCloth, ItemColored, ItemMultiTexture,
                      * ItemBlockWithMetadata, ItemDoublePlant (Item's 0), ItemSlab,
                      * ItemSnow, ItemBlockWithoutItem */
}

/* ------------------------------------------------------------ block helpers */

/* BlockTorch.func_150107_m: a solid top surface, or a fence, glass or wall. */
static int torch_can_support(struct world *w, int x, int y, int z)
{
    if (solid_top(w, x, y, z)) return 1;

    int b = block_at(w, x, y, z);

    /* func_150107_m: fence, nether brick fence, glass, cobblestone wall */
    return b == 85 || b == 113 || b == 20 || b == 139;
}

/* BlockReed.canPlaceBlockAt. */
static int reed_can_place(struct world *w, int x, int y, int z)
{
    int below = block_at(w, x, y - 1, z);

    if (below == 83) return 1;
    if (below != 2 && below != 3 && below != 12) return 0;

    int water = mat_named("water");

    return mat_of(block_at(w, x - 1, y - 1, z)) == water || mat_of(block_at(w, x + 1, y - 1, z)) == water ||
           mat_of(block_at(w, x, y - 1, z - 1)) == water || mat_of(block_at(w, x, y - 1, z + 1)) == water;
}

/* World.getFullBlockLightValue: Chunk.getBlockLightValue(x, y, z, 0), the
 * greater of the two saved values. */
static int full_light(struct world *w, int x, int y, int z)
{
    int sky = world_get_light(w, LIGHT_SKY, x, y, z);
    int block = world_get_light(w, LIGHT_BLOCK, x, y, z);

    return sky > block ? sky : block;
}

/* BlockMushroom.canBlockStay. */
static int mushroom_can_stay(struct world *w, int x, int y, int z)
{
    if (y < 0 || y >= 256) return 0;

    int below = block_at(w, x, y - 1, z);

    if (below == 110) return 1;                                   /* mycelium */
    if (below == 3 && meta_at(w, x, y - 1, z) == 2) return 1;     /* podzol dirt */

    return full_light(w, x, y, z) < 13 && BLOCKS[below].opaque_cube;
}

/* BlockBush.func_149854_a, per subclass (blockcb.c's bush_floor_ok, with the
 * crops subclasses the class names split). */
static int bush_floor_ok(int id, int below)
{
    if (is_block_class(id, "BlockDeadBush"))
        return below == 12 || below == 172 || below == 159 || below == 3;

    if (is_block_class(id, "BlockMushroom")) return BLOCKS[below].opaque_cube;

    if (is_block_class(id, "BlockNetherWart")) return below == 88;

    if (is_block_class(id, "BlockCrops") || is_block_class(id, "BlockStem") ||
        is_block_class(id, "BlockCarrot") || is_block_class(id, "BlockPotato"))
        return below == 60;

    /* BlockBush's own: grass, dirt or farmland */
    return below == 2 || below == 3 || below == 60;
}

/* BlockBush.canBlockStay, with BlockLilyPad's and BlockDoublePlant's own. */
static int bush_can_stay(struct world *w, int id, int x, int y, int z)
{
    if (is_block_class(id, "BlockLilyPad"))
        return mat_of(block_at(w, x, y - 1, z)) == mat_named("water") && meta_at(w, x, y - 1, z) == 0;

    int below = block_at(w, x, y - 1, z);

    if (is_block_class(id, "BlockDoublePlant"))
    {
        if ((meta_at(w, x, y, z) & 8) != 0) return below == id;

        return block_at(w, x, y + 1, z) == id && bush_floor_ok(id, below);
    }

    return bush_floor_ok(id, below);
}

/* BlockTrapDoor.func_150119_a: an opaque full cube, glowstone, a slab or a
 * stair can hold a trapdoor. */
static int trapdoor_support(int b)
{
    if (MATERIALS[mat_of(b)].is_opaque && BLOCKS[b].normal_block) return 1;
    if (b == 89) return 1;   /* glowstone */

    return is_slab(b) || is_block_class(b, "BlockStairs");
}

/* BlockVine.func_150093_a: a wall that can hold a vine. */
static int vine_wall_ok(int b)
{
    return BLOCKS[b].normal_block && MATERIALS[mat_of(b)].blocks_movement;
}

/* BlockChest.func_149952_n: the position is a chest that is already half of a
 * double chest. */
static int chest_occupied(struct world *w, int block, int x, int y, int z)
{
    if (block_at(w, x, y, z) != block) return 0;

    return block_at(w, x - 1, y, z) == block || block_at(w, x + 1, y, z) == block ||
           block_at(w, x, y, z - 1) == block || block_at(w, x, y, z + 1) == block;
}

/* ---------------------------------------------- the rail-shape refresh
 * BlockRailBase.Rail: one position, its two connections, and the refresh that
 * recomputes the shape from the neighbouring rails and stores it. */

struct rail {
    struct world *w;
    int x, y, z;
    int powered;                        /* field_150656_f */
    int con_x[2], con_y[2], con_z[2];   /* field_150657_g */
    int ncon;
};

static int is_rail_pos(struct world *w, int x, int y, int z)
{
    int b = block_at(w, x, y, z);

    return is_block_class(b, "BlockRail") || is_block_class(b, "BlockRailPowered") ||
           is_block_class(b, "BlockRailDetector");
}

static int rail_meta_at(const struct rail *r)
{
    return world_get_meta(r->w, r->x, r->y, r->z);
}

/* BlockRailBase.Rail.func_150648_a: the connection table. */
static void rail_set_conns(struct rail *r, int m)
{
    int ax[2] = {r->x, r->x}, ay[2] = {r->y, r->y}, az[2] = {r->z, r->z};

    switch (m)
    {
    case 0: az[0] = r->z - 1; az[1] = r->z + 1; break;
    case 1: ax[0] = r->x - 1; ax[1] = r->x + 1; break;
    case 2: ax[0] = r->x - 1; ax[1] = r->x + 1; ay[1] = r->y + 1; break;
    case 3: ax[0] = r->x - 1; ay[0] = r->y + 1; ax[1] = r->x + 1; break;
    case 4: az[0] = r->z - 1; ay[0] = r->y + 1; az[1] = r->z + 1; break;
    case 5: az[0] = r->z - 1; az[1] = r->z + 1; ay[1] = r->y + 1; break;
    case 6: ax[0] = r->x + 1; az[1] = r->z + 1; break;
    case 7: ax[0] = r->x - 1; az[1] = r->z + 1; break;
    case 8: ax[0] = r->x - 1; az[1] = r->z - 1; break;
    case 9: ax[0] = r->x + 1; az[1] = r->z - 1; break;
    default: r->ncon = 0; return;
    }

    r->con_x[0] = ax[0]; r->con_y[0] = ay[0]; r->con_z[0] = az[0];
    r->con_x[1] = ax[1]; r->con_y[1] = ay[1]; r->con_z[1] = az[1];
    r->ncon = 2;
}

static void rail_init(struct rail *r, struct world *w, int x, int y, int z)
{
    r->w = w;
    r->x = x;
    r->y = y;
    r->z = z;
    int m = world_get_meta(w, x, y, z);
    int id = block_at(w, x, y, z);

    if (is_block_class(id, "BlockRailPowered") || is_block_class(id, "BlockRailDetector"))
    {
        r->powered = 1;
        m &= -9;
    }
    else
    {
        r->powered = 0;
    }

    rail_set_conns(r, m);
}

/* BlockRailBase.Rail.func_150654_a: the rail at the position, at y, y+1 or
 * y-1, NULL when there is none. */
static struct rail *rail_at(struct rail *out, struct world *w, int x, int y, int z)
{
    if (is_rail_pos(w, x, y, z)) { rail_init(out, w, x, y, z); return out; }
    if (is_rail_pos(w, x, y + 1, z)) { rail_init(out, w, x, y + 1, z); return out; }
    if (is_rail_pos(w, x, y - 1, z)) { rail_init(out, w, x, y - 1, z); return out; }

    return NULL;
}

/* BlockRailBase.Rail.func_150651_b: retarget each connection to where its rail
 * now stands, drop the ones the shape can no longer reach. */
static void rail_conn_refresh(struct rail *r)
{
    for (int i = 0; i < r->ncon; ++i)
    {
        struct rail other;

        struct rail *o = rail_at(&other, r->w, r->con_x[i], r->con_y[i], r->con_z[i]);

        int keep = 0;

        if (o != NULL)
        {
            for (int j = 0; j < o->ncon; ++j)
                if (o->con_x[j] == r->x && o->con_z[j] == r->z) keep = 1;

            if (keep)
            {
                r->con_x[i] = o->x;
                r->con_y[i] = o->y;
                r->con_z[i] = o->z;
                continue;
            }
        }

        for (int j = i + 1; j < r->ncon; ++j)
        {
            r->con_x[j - 1] = r->con_x[j];
            r->con_y[j - 1] = r->con_y[j];
            r->con_z[j - 1] = r->con_z[j];
        }

        --r->ncon;
        --i;
    }
}

/* BlockRailBase.Rail.func_150652_b. */
static int rail_has_conn_xz(const struct rail *r, int x, int z)
{
    for (int i = 0; i < r->ncon; ++i)
        if (r->con_x[i] == x && r->con_z[i] == z) return 1;

    return 0;
}

/* BlockRailBase.Rail.func_150650_a: how many horizontal sides lead to a rail
 * at this y, one above or one below. */
static int rail_neighbour_count(const struct rail *r)
{
    int n = 0;

    if (is_rail_pos(r->w, r->x, r->y, r->z - 1)) ++n;
    if (is_rail_pos(r->w, r->x, r->y, r->z + 1)) ++n;
    if (is_rail_pos(r->w, r->x - 1, r->y, r->z)) ++n;
    if (is_rail_pos(r->w, r->x + 1, r->y, r->z)) ++n;

    return n;
}

/* BlockRailBase.Rail.func_150649_b. */
static int rail_wants(const struct rail *r, const struct rail *other)
{
    for (int i = 0; i < r->ncon; ++i)
        if (r->con_x[i] == other->x && r->con_z[i] == other->z) return 1;

    return r->ncon != 2;
}

/* BlockRailBase.Rail.func_150645_c: take the other rail into the shape, then
 * recompute the shape from the four sides and store it. */
static void rail_join(struct rail *r, const struct rail *other)
{
    r->con_x[r->ncon] = other->x;
    r->con_y[r->ncon] = other->y;
    r->con_z[r->ncon] = other->z;
    ++r->ncon;

    int zn = rail_has_conn_xz(r, r->x, r->z - 1);
    int zp = rail_has_conn_xz(r, r->x, r->z + 1);
    int xn = rail_has_conn_xz(r, r->x - 1, r->z);
    int xp = rail_has_conn_xz(r, r->x + 1, r->z);
    int m = -1;

    if (zn || zp) m = 0;
    if (xn || xp) m = 1;

    if (!r->powered)
    {
        if (zp && xp && !zn && !xn) m = 6;
        if (zp && xn && !zn && !xp) m = 7;
        if (zn && xn && !zp && !xp) m = 8;
        if (zn && xp && !zp && !xn) m = 9;
    }

    if (m == 0)
    {
        if (is_rail_pos(r->w, r->x, r->y + 1, r->z - 1)) m = 4;
        if (is_rail_pos(r->w, r->x, r->y + 1, r->z + 1)) m = 5;
    }

    if (m == 1)
    {
        if (is_rail_pos(r->w, r->x + 1, r->y + 1, r->z)) m = 2;
        if (is_rail_pos(r->w, r->x - 1, r->y + 1, r->z)) m = 3;
    }

    if (m < 0) m = 0;

    int store = m;

    if (r->powered) store = (rail_meta_at(r) & 8) | m;

    world_set_meta(r->w, r->x, r->y, r->z, store, 3);
}

/* BlockRailBase.Rail.func_150647_c. */
static int rail_can_reach(struct rail *r, int x, int y, int z)
{
    struct rail other;

    struct rail *o = rail_at(&other, r->w, x, y, z);

    if (o == NULL) return 0;

    rail_conn_refresh(o);
    return rail_wants(o, r);
}

/* BlockRailBase.Rail.func_150655_a: the refresh func_150052_a drives. */
static void rail_refresh(struct rail *r, int from_power, int keep)
{
    int zn = rail_can_reach(r, r->x, r->y, r->z - 1);
    int zp = rail_can_reach(r, r->x, r->y, r->z + 1);
    int xn = rail_can_reach(r, r->x - 1, r->y, r->z);
    int xp = rail_can_reach(r, r->x + 1, r->y, r->z);
    int m = -1;

    if ((zn || zp) && !xn && !xp) m = 0;
    if ((xn || xp) && !zn && !zp) m = 1;

    if (!r->powered)
    {
        if (zp && xp && !zn && !xn) m = 6;
        if (zp && xn && !zn && !xp) m = 7;
        if (zn && xn && !zp && !xp) m = 8;
        if (zn && xp && !zp && !xn) m = 9;
    }

    if (m == -1)
    {
        if (zn || zp) m = 0;
        if (xn || xp) m = 1;

        if (!r->powered)
        {
            if (from_power)
            {
                if (zp && xp) m = 6;
                if (xn && zp) m = 7;
                if (xp && zn) m = 9;
                if (zn && xn) m = 8;
            }
            else
            {
                if (zn && xn) m = 8;
                if (xp && zn) m = 9;
                if (xn && zp) m = 7;
                if (zp && xp) m = 6;
            }
        }
    }

    if (m == 0)
    {
        if (is_rail_pos(r->w, r->x, r->y + 1, r->z - 1)) m = 4;
        if (is_rail_pos(r->w, r->x, r->y + 1, r->z + 1)) m = 5;
    }

    if (m == 1)
    {
        if (is_rail_pos(r->w, r->x + 1, r->y + 1, r->z)) m = 2;
        if (is_rail_pos(r->w, r->x - 1, r->y + 1, r->z)) m = 3;
    }

    if (m < 0) m = 0;

    rail_set_conns(r, m);
    int store = m;

    if (r->powered) store = (rail_meta_at(r) & 8) | m;

    if (keep || rail_meta_at(r) != store)
    {
        world_set_meta(r->w, r->x, r->y, r->z, store, 3);

        for (int i = 0; i < r->ncon; ++i)
        {
            struct rail other;

            struct rail *o = rail_at(&other, r->w, r->con_x[i], r->con_y[i], r->con_z[i]);

            if (o != NULL)
            {
                rail_conn_refresh(o);

                if (rail_wants(o, r)) rail_join(o, r);
            }
        }
    }
}

/* BlockRailBase.func_150052_a. */
static void rail_refresh_at(struct world *w, int x, int y, int z, int keep)
{
    struct rail r;

    rail_init(&r, w, x, y, z);
    rail_refresh(&r, indirectly_powered(w, x, y, z), keep);
}

/* BlockRailBase.onNeighborBlockChange: the support, then func_150048_a. */
static void rail_on_neighbor(struct world *w, int x, int y, int z, int source)
{
    int id = block_at(w, x, y, z);
    int meta = meta_at(w, x, y, z);
    int m7 = meta;

    if (is_block_class(id, "BlockRailPowered") || is_block_class(id, "BlockRailDetector")) m7 = meta & 7;

    if (!solid_top(w, x, y - 1, z) ||
        (m7 == 2 && !solid_top(w, x + 1, y, z)) ||
        (m7 == 3 && !solid_top(w, x - 1, y, z)) ||
        (m7 == 4 && !solid_top(w, x, y, z - 1)) ||
        (m7 == 5 && !solid_top(w, x, y, z + 1)))
    {
        place_block_drop(w, x, y, z, id, meta);
        world_set_block(w, x, y, z, 0, 0, 2);
        return;
    }

    if (is_block_class(id, "BlockRail"))
    {
        /* BlockRail.func_150048_a: a power-providing neighbour beside a rail
         * with three connections re-orients it. */
        struct rail r;

        rail_init(&r, w, x, y, z);

        if (BLOCKS[source & 4095].provides_power && rail_neighbour_count(&r) == 3)
            rail_refresh_at(w, x, y, z, 0);
    }

    /* BlockRailPowered.func_150048_a is the powered bit, whose every power
     * input is zero with redstone off and which a fresh rail never carries;
     * BlockRailDetector has no func_150048_a at all. */
}

/* The rails' two callbacks, which blockcb.c dispatches to. */
void place_rail_on_added(struct world *w, int id, int x, int y, int z)
{
    rail_refresh_at(w, x, y, z, 1);

    if (is_block_class(id, "BlockRailPowered") || is_block_class(id, "BlockRailDetector"))
        rail_on_neighbor(w, x, y, z, id);
}

void place_rail_on_neighbor(struct world *w, int x, int y, int z, int source)
{
    rail_on_neighbor(w, x, y, z, source);
}

/* ------------------------------------------------------- the block rules */

/* BlockPortal.Size: the frame scan. axis is 1 (along x) or 2 (along z);
 * returns func_150860_b(), with field_150864_e in *count and the h*g area in
 * *area. */
static int portal_interior(int b)
{
    return b == 0 || b == 51 || b == 90;
}

static int portal_run(struct world *w, int x, int y, int z, int d)
{
    static const int off_x[4] = {0, -1, 0, 1};
    static const int off_z[4] = {1, 0, -1, 0};
    int i, b;

    for (i = 0; i < 22; ++i)
    {
        if (!portal_interior(block_at(w, x + off_x[d] * i, y, z + off_z[d] * i))) break;
        if (block_at(w, x + off_x[d] * i, y - 1, z + off_z[d] * i) != 49) break;
    }

    b = block_at(w, x + off_x[d] * i, y, z + off_z[d] * i);
    return b == 49 ? i : 0;
}

int place_portal_frame_ok(struct world *w, int x, int y, int z, int meta)
{
    static const int dirs[3][2] = {{0, 0}, {3, 1}, {2, 0}};
    static const int off_x[4] = {0, -1, 0, 1};
    static const int off_z[4] = {1, 0, -1, 0};
    int axis = meta & 3;
    int da = dirs[axis][0], db = dirs[axis][1];
    int y0 = y, run, hrun, g = 0, h, bad = 0, cx, cz;

    /* the ctor's down-slide over interior cells */
    while (y > 0 && y > y0 - 21 && portal_interior(block_at(w, x, y - 1, z))) --y;

    run = portal_run(w, x, y, z, da);

    if (run == 0) return 0;

    cx = x + off_x[da] * (run - 1);
    cz = z + off_z[da] * (run - 1);
    hrun = portal_run(w, cx, y, cz, db);

    if (hrun < 2 || hrun > 21) return 0;

    for (g = 0; g < 21; ++g)
    {
        for (h = 0; h < hrun; ++h)
        {
            int bx = cx + off_x[db] * h;
            int bz = cz + off_z[db] * h;
            int b = block_at(w, bx, y + g, bz);

            if (!portal_interior(b)) { bad = 1; break; }

            (void)0;

            if (h == 0 && block_at(w, bx + off_x[da], y + g, bz + off_z[da]) != 49) { bad = 1; break; }

            if (h == hrun - 1 && block_at(w, bx + off_x[db], y + g, bz + off_z[db]) != 49) { bad = 1; break; }
        }

        if (bad) break;
    }

    /* the top row, at whatever height the scan stopped at */
    for (h = 0; h < hrun; ++h)
        if (block_at(w, cx + off_x[db] * h, y + g, cz + off_z[db] * h) != 49) { g = 0; break; }

    if (g < 3 || g > 21) return 0;

    return 1;
}

/* Block.canPlaceBlockAt, by class; block is the block being placed. */
/* BlockTorch.canPlaceBlockAt, which the redstone torch inherits. */
static int torch_can_place(struct world *w, int x, int y, int z)
{
    return normal_cube_default(w, x - 1, y, z, 1) || normal_cube_default(w, x + 1, y, z, 1) ||
           normal_cube_default(w, x, y, z - 1, 1) || normal_cube_default(w, x, y, z + 1, 1) ||
           torch_can_support(w, x, y - 1, z);
}

/* World.isBlockNormalCubeDefault(..., true), the torch's per-side check. */
static int torch_wall_ok(struct world *w, int x, int y, int z)
{
    int b = block_at(w, x, y, z);

    return b == 0 ? 1 : BLOCKS[b].normal_cube;
}

/* The torch rules, exported for blockcb.c's onBlockAdded and neighbour case. */
int place_torch_can_stay(struct world *w, int x, int y, int z)
{
    return torch_can_place(w, x, y, z);
}

int place_torch_wall_ok(struct world *w, int x, int y, int z)
{
    return torch_wall_ok(w, x, y, z);
}

static int can_place_block_at(struct world *w, int block, int x, int y, int z)
{
    int target = block_at(w, x, y, z);
    int dflt = is_replaceable(target);

    if (is_block_class(block, "BlockTorch") || is_block_class(block, "BlockRedstoneTorch"))
        return torch_can_place(w, x, y, z);

    if (is_block_class(block, "BlockLadder"))
        return normal_cube_at(w, x - 1, y, z) || normal_cube_at(w, x + 1, y, z) ||
               normal_cube_at(w, x, y, z - 1) || normal_cube_at(w, x, y, z + 1);

    if (is_block_class(block, "BlockLever"))
        return normal_cube_at(w, x - 1, y, z) || normal_cube_at(w, x + 1, y, z) ||
               normal_cube_at(w, x, y, z - 1) || normal_cube_at(w, x, y, z + 1) ||
               solid_top(w, x, y - 1, z) || normal_cube_at(w, x, y + 1, z);

    if (is_block_class(block, "BlockButton") || is_block_class(block, "BlockButtonStone") ||
        is_block_class(block, "BlockButtonWood") || is_block_class(block, "BlockTripWireHook"))
        return normal_cube_at(w, x - 1, y, z) || normal_cube_at(w, x + 1, y, z) ||
               normal_cube_at(w, x, y, z - 1) || normal_cube_at(w, x, y, z + 1);

    if (is_block_class(block, "BlockFire"))
        return solid_top(w, x, y - 1, z) || fire_flammable_near(w, x, y, z);

    if (is_block_class(block, "BlockFenceGate"))
        return is_solid_mat(block_at(w, x, y - 1, z)) && dflt;

    if (is_block_class(block, "BlockPressurePlate") || is_block_class(block, "BlockPressurePlateWeighted"))
        return solid_top(w, x, y - 1, z) ||
               block_at(w, x, y - 1, z) == 85 || block_at(w, x, y - 1, z) == 113;   /* the fences */

    if (is_block_class(block, "BlockPumpkin"))
        return dflt && solid_top(w, x, y - 1, z);

    if (is_block_class(block, "BlockChest"))
    {
        int n = 0;

        if (block_at(w, x - 1, y, z) == block) ++n;
        if (block_at(w, x + 1, y, z) == block) ++n;
        if (block_at(w, x, y, z - 1) == block) ++n;
        if (block_at(w, x, y, z + 1) == block) ++n;

        if (n > 1) return 0;

        return !chest_occupied(w, block, x - 1, y, z) && !chest_occupied(w, block, x + 1, y, z) &&
               !chest_occupied(w, block, x, y, z - 1) && !chest_occupied(w, block, x, y, z + 1);
    }

    if (is_block_class(block, "BlockDoor"))
    {
        if (y >= 255) return 0;

        return solid_top(w, x, y - 1, z) && is_replaceable(block_at(w, x, y, z)) &&
               is_replaceable(block_at(w, x, y + 1, z));
    }

    if (is_block_class(block, "BlockRail") || is_block_class(block, "BlockRailPowered") ||
        is_block_class(block, "BlockRailDetector"))
        return solid_top(w, x, y - 1, z);

    if (is_block_class(block, "BlockRedstoneWire"))
    {
        int below = block_at(w, x, y - 1, z);

        return solid_top(w, x, y - 1, z) || below == 89;   /* glowstone */
    }

    if (is_block_class(block, "BlockRedstoneDiode") || is_block_class(block, "BlockRedstoneRepeater") ||
        is_block_class(block, "BlockRedstoneComparator"))
        return solid_top(w, x, y - 1, z) && dflt;

    if (is_block_class(block, "BlockSnow"))
    {
        int below = block_at(w, x, y - 1, z);

        if (below == 79 || below == 174) return 0;   /* ice, packed ice */
        if (mat_of(below) == mat_named("leaves")) return 1;
        if (below == 78 && (meta_at(w, x, y - 1, z) & 7) == 7) return 1;

        return BLOCKS[below].opaque_cube && MATERIALS[mat_of(below)].blocks_movement;
    }

    if (is_block_class(block, "BlockCactus"))
    {
        if (!dflt) return 0;

        /* BlockCactus.canBlockStay */
        if (is_solid_mat(block_at(w, x - 1, y, z)) || is_solid_mat(block_at(w, x + 1, y, z)) ||
            is_solid_mat(block_at(w, x, y, z - 1)) || is_solid_mat(block_at(w, x, y, z + 1)))
            return 0;

        int below = block_at(w, x, y - 1, z);

        return below == 81 || below == 12;
    }

    if (is_block_class(block, "BlockMushroom"))
        return dflt && mushroom_can_stay(w, x, y, z);

    /* BlockDoublePlant.canPlaceBlockAt: the floor check of BlockBush's
     * func_149854_a plus an air cell above; canBlockStay is not called here */
    if (is_block_class(block, "BlockDoublePlant"))
        return dflt && is_air_block(w, x, y + 1, z) && bush_floor_ok(block, block_at(w, x, y - 1, z));

    /* BlockLilyPad's func_149854_a: the block below must be Blocks.water
     * itself (static water, id 9), not just water material; flowing water is
     * refused. The meta-0 rule is only canBlockStay, which the callbacks run */
    if (is_block_class(block, "BlockLilyPad"))
        return dflt && block_at(w, x, y - 1, z) == 9;

    if (is_block_class(block, "BlockFlower") || is_block_class(block, "BlockSapling") ||
        is_block_class(block, "BlockTallGrass") || is_block_class(block, "BlockDeadBush") ||
        is_block_class(block, "BlockCrops") || is_block_class(block, "BlockStem") ||
        is_block_class(block, "BlockNetherWart") || is_block_class(block, "BlockCarrot") ||
        is_block_class(block, "BlockPotato"))
        return dflt && bush_can_stay(w, block, x, y, z);

    if (is_block_class(block, "BlockReed"))
        return reed_can_place(w, x, y, z);

    if (is_block_class(block, "BlockFlowerPot"))
        return dflt && solid_top(w, x, y - 1, z);

    if (is_block_class(block, "BlockCarpet"))
        return dflt && mat_of(block_at(w, x, y - 1, z)) != mat_named("air");

    if (is_block_class(block, "BlockBasePressurePlate") || is_block_class(block, "BlockPressurePlate") ||
        is_block_class(block, "BlockStonePressurePlate") || is_block_class(block, "BlockWoodPressurePlate"))
    {
        int below = block_at(w, x, y - 1, z);

        return solid_top(w, x, y - 1, z) || below == 85 || below == 113;
    }

    if (is_block_class(block, "BlockCake"))
        return dflt && is_solid_mat(block_at(w, x, y - 1, z));

    /* BlockStairs delegates to its model block, whose canPlaceBlockAt is the
     * default body for every stair. */
    return dflt;
}

/* Block.canReplace through canPlaceBlockOnSide, by class. */
static int can_replace(struct world *w, int block, int x, int y, int z, int side, const struct place_stack *stack)
{
    (void)stack;

    if (is_block_class(block, "BlockLever"))
    {
        if (side == 0) return normal_cube_at(w, x, y + 1, z);
        if (side == 1) return solid_top(w, x, y - 1, z);
        if (side == 2) return normal_cube_at(w, x, y, z + 1);
        if (side == 3) return normal_cube_at(w, x, y, z - 1);
        if (side == 4) return normal_cube_at(w, x + 1, y, z);
        return side == 5 && normal_cube_at(w, x - 1, y, z);
    }

    if (is_block_class(block, "BlockButton") || is_block_class(block, "BlockButtonStone") ||
        is_block_class(block, "BlockButtonWood"))
    {
        if (side == 2) return normal_cube_at(w, x, y, z + 1);
        if (side == 3) return normal_cube_at(w, x, y, z - 1);
        if (side == 4) return normal_cube_at(w, x + 1, y, z);
        return side == 5 && normal_cube_at(w, x - 1, y, z);
    }

    if (is_block_class(block, "BlockTrapDoor"))
    {
        if (side == 0 || side == 1) return 0;

        int vx = x, vz = z;

        /* canPlaceBlockOnSide: side 2 attaches at z+1, 3 at z-1, 4 at x+1,
         * 5 at x-1 */
        if (side == 2) vz = z + 1;
        else if (side == 3) vz = z - 1;
        else if (side == 4) vx = x + 1;
        else vx = x - 1;

        return trapdoor_support(block_at(w, vx, y, vz));
    }

    if (is_block_class(block, "BlockVine"))
    {
        int wx = side == 4 ? 1 : side == 5 ? -1 : 0;
        int wz = side == 2 ? 1 : side == 3 ? -1 : 0;

        if (side == 0) return 0;

        return vine_wall_ok(block_at(w, x + wx, y + (side == 1 ? 1 : 0), z + wz));
    }

    if (is_block_class(block, "BlockTripWireHook"))
    {
        /* canPlaceBlockOnSide: the wall the hook hangs from, per side */
        if (side == 2) return normal_cube_at(w, x, y, z + 1);
        if (side == 3) return normal_cube_at(w, x, y, z - 1);
        if (side == 4) return normal_cube_at(w, x + 1, y, z);
        if (side == 5) return normal_cube_at(w, x - 1, y, z);
        return 0;
    }

    /* Everything else: canPlaceBlockAt. */
    return can_place_block_at(w, block, x, y, z);
}

/* The entity half of canPlaceEntityOnSide, see place.h. */
#define entity_hook (nw_env->place.entity_hook)
#define entity_hook_ctx (nw_env->place.entity_hook_ctx)

void place_set_entity_hook(place_entity_hook hook, void *ctx)
{
    entity_hook = hook;
    entity_hook_ctx = ctx;
}

place_entity_hook place_get_entity_hook(void **ctx)
{
    if (ctx) *ctx = entity_hook_ctx;
    return entity_hook;
}

/* The placed block's getCollisionBoundingBoxFromPool at the target cell
 * (collide.c's, null for the blocks with no collision; the shaped ones their
 * own box: the enchanting table's 3/4, the bed's 9/16). */
static int placed_block_box(struct world *w, int block, int x, int y, int z, struct aabb *out)
{
    return collide_pool_box_as(w, block, x, y, z, out);
}

/* World.canPlaceEntityOnSide: a live entity in the placed block's collision
 * box refuses it, then the material checks and canReplace. */
static int can_place_entity_on_side(struct world *w, int block, int x, int y, int z, int side,
                                    const struct place_stack *stack, int skip_placer)
{
    struct aabb box;

    if (entity_hook != NULL && placed_block_box(w, block, x, y, z, &box) &&
        entity_hook(entity_hook_ctx, w, &box, skip_placer))
        return 0;

    int target = block_at(w, x, y, z);

    if (is_circuits(target) && block == 145) return 1;   /* an anvil over a circuits block */

    return is_replaceable(target) && can_replace(w, block, x, y, z, side, stack);
}

/* Block.onBlockPlaced, by class. */
static int on_block_placed(struct world *w, int block, int x, int y, int z, int side,
                           float hx, float hy, float hz, int meta)
{
    (void)hx; (void)hz;

    if (is_block_class(block, "BlockStairs"))
        return side != 0 && (side == 1 || (double)hy <= 0.5) ? meta : meta | 4;

    if (is_slab(block) && !is_double_slab(block))
        return side != 0 && (side == 1 || (double)hy <= 0.5) ? meta : meta | 8;

    if (is_block_class(block, "BlockRotatedPillar") || is_block_class(block, "BlockOldLog") ||
        is_block_class(block, "BlockNewLog") || is_block_class(block, "BlockHay"))
    {
        /* BlockRotatedPillar.onBlockPlaced, which the logs and the hay
         * inherit: the axis bits come from the placement side */
        int base = meta & 3;
        int axis = 0;

        if (side == 2 || side == 3) axis = 8;
        else if (side == 4 || side == 5) axis = 4;

        return base | axis;
    }

    if (is_block_class(block, "BlockQuartz"))
    {
        if (meta == 2)
        {
            if (side == 0 || side == 1) meta = 2;
            else if (side == 2 || side == 3) meta = 4;
            else meta = 3;
        }

        return meta;
    }

    if (is_block_class(block, "BlockTorch") || is_block_class(block, "BlockRedstoneTorch"))
    {
        int m = meta;

        if (side == 1 && torch_can_support(w, x, y - 1, z)) m = 5;
        if (side == 2 && normal_cube_default(w, x, y, z + 1, 1)) m = 4;
        if (side == 3 && normal_cube_default(w, x, y, z - 1, 1)) m = 3;
        if (side == 4 && normal_cube_default(w, x + 1, y, z, 1)) m = 2;
        if (side == 5 && normal_cube_default(w, x - 1, y, z, 1)) m = 1;

        return m;
    }

    if (is_block_class(block, "BlockLadder"))
    {
        int m = meta;

        if ((m == 0 || side == 2) && normal_cube_at(w, x, y, z + 1)) m = 2;
        if ((m == 0 || side == 3) && normal_cube_at(w, x, y, z - 1)) m = 3;
        if ((m == 0 || side == 4) && normal_cube_at(w, x + 1, y, z)) m = 4;
        if ((m == 0 || side == 5) && normal_cube_at(w, x - 1, y, z)) m = 5;

        return m;
    }

    if (is_block_class(block, "BlockLever"))
    {
        int keep = meta & 8;
        int dir = -1;

        if (side == 0 && normal_cube_at(w, x, y + 1, z)) dir = 0;
        if (side == 1 && solid_top(w, x, y - 1, z)) dir = 5;
        if (side == 2 && normal_cube_at(w, x, y, z + 1)) dir = 4;
        if (side == 3 && normal_cube_at(w, x, y, z - 1)) dir = 3;
        if (side == 4 && normal_cube_at(w, x + 1, y, z)) dir = 2;
        if (side == 5 && normal_cube_at(w, x - 1, y, z)) dir = 1;

        return dir + keep;
    }

    if (is_block_class(block, "BlockButton") || is_block_class(block, "BlockButtonStone") ||
        is_block_class(block, "BlockButtonWood"))
    {
        int cur = meta_at(w, x, y, z);
        int m = cur & 7;
        int powered = cur & 8;

        if (side == 2 && normal_cube_at(w, x, y, z + 1)) m = 4;
        else if (side == 3 && normal_cube_at(w, x, y, z - 1)) m = 3;
        else if (side == 4 && normal_cube_at(w, x + 1, y, z)) m = 2;
        else if (side == 5 && normal_cube_at(w, x - 1, y, z)) m = 1;
        else
        {
            /* BlockButton.func_150045_e */
            if (normal_cube_at(w, x - 1, y, z)) m = 1;
            else if (normal_cube_at(w, x + 1, y, z)) m = 2;
            else if (normal_cube_at(w, x, y, z - 1)) m = 3;
            else if (normal_cube_at(w, x, y, z + 1)) m = 4;
            else m = 1;
        }

        return m | powered;
    }

    if (is_block_class(block, "BlockTrapDoor"))
    {
        int m = side == 2 ? 0 : side == 3 ? 1 : side == 4 ? 2 : side == 5 ? 3 : 0;

        if (side != 1 && side != 0 && hy > 0.5F) m |= 8;

        return m;
    }

    if (is_block_class(block, "BlockHopper"))
    {
        int m = FACING_OPPOSITE_SIDE[side];

        if (m == 1) m = 0;

        return m;
    }

    if (is_block_class(block, "BlockVine"))
    {
        int m = side == 2 ? 1 : side == 3 ? 4 : side == 4 ? 8 : side == 5 ? 2 : 0;

        return m != 0 ? m : meta;
    }

    if (is_block_class(block, "BlockCocoa"))
    {
        if (side == 1 || side == 0) side = 2;

        return DIR_ROTATE_OPPOSITE[DIR_FACING_TO_DIRECTION[side]];
    }

    if (is_block_class(block, "BlockTripWireHook"))
    {
        int m = 0;

        if (side == 2 && normal_cube_default(w, x, y, z + 1, 1)) m = 2;
        if (side == 3 && normal_cube_default(w, x, y, z - 1, 1)) m = 0;
        if (side == 4 && normal_cube_default(w, x + 1, y, z, 1)) m = 1;
        if (side == 5 && normal_cube_default(w, x - 1, y, z, 1)) m = 3;

        return m;
    }

    return meta;
}
/* BlockPistonBase.func_150071_a, which the dispensers, the droppers and the
 * pistons read the extension direction from. */
static int func_150071_a(const struct placer *p, int x, int y, int z)
{
    if (fabsf((float)p->px - (float)x) < 2.0F && fabsf((float)p->pz - (float)z) < 2.0F)
    {
        double eye = p->py + 1.82 - p->y_offset;

        if (eye - (double)y > 2.0) return 1;
        if ((double)y - eye > 0.0) return 0;
    }

    int q = yaw_quad(p->yaw, 4.0F, 0.5) & 3;

    return q == 0 ? 2 : (q == 1 ? 5 : (q == 2 ? 3 : (q == 3 ? 4 : 0)));
}

/* BlockTripWireHook.func_150134_a: the hook's neighbours learn it changed. */
static void tripwire_notify(struct world *w, int x, int y, int z, int side)
{
    world_notify_neighbors(w, x, y, z, 131);

    if (side == 3) world_notify_neighbors(w, x - 1, y, z, 131);
    else if (side == 1) world_notify_neighbors(w, x + 1, y, z, 131);
    else if (side == 0) world_notify_neighbors(w, x, y, z - 1, 131);
    else if (side == 2) world_notify_neighbors(w, x, y, z + 1, 131);
}

/* BlockTripWireHook.func_150136_a: the pair and the tripwire learn the hook's
 * state. gone is p5, sched is p7 (always false on the paths a placement
 * reaches), at/atmeta the update that came from the scan. */
static void tripwire_hook_update(struct world *w, int x, int y, int z, int gone, int meta, int sched, int at, int atmeta)
{
    (void)sched;

    int dir = meta & 3;
    int connected = (meta & 4) == 4;
    int attached = !gone;
    int armed_any = 0;
    int floor_gone = !solid_top(w, x, y - 1, z);
    int line[42];
    int pair = 0;

    memset(line, 0, sizeof line);

    for (int i = 1; i < 42; ++i)
    {
        int bx = x + DIR_OFF_X[dir] * i;
        int bz = z + DIR_OFF_Z[dir] * i;
        int b = block_at(w, bx, y, bz);

        if (b == 131)   /* tripwire hook */
        {
            int hm = meta_at(w, bx, y, bz);

            if ((hm & 3) == DIR_ROTATE_OPPOSITE[dir]) pair = i;

            break;
        }

        if (b != 132 && i != at)   /* not tripwire */
        {
            line[i] = -1;
            attached = 0;
        }
        else
        {
            int m = i == at ? atmeta : meta_at(w, bx, y, bz);
            int un_armed = (m & 8) != 8;
            int has_bit1 = (m & 1) == 1;
            int has_bit2 = (m & 2) == 2;

            attached &= (has_bit2 == floor_gone);
            armed_any |= un_armed && has_bit1;
            line[i] = m;

            if (i == at)
            {
                /* func_149738_a: the hook's tick rate */
                ticks_schedule_block_update(w, x, y, z, 131, 10);
                attached &= un_armed;
            }
        }
    }

    attached &= pair > 1;
    armed_any &= attached;
    int bits = (attached ? 4 : 0) | (armed_any ? 8 : 0);
    meta = dir | bits;

    if (pair > 0)
    {
        int bx = x + DIR_OFF_X[dir] * pair;
        int bz = z + DIR_OFF_Z[dir] * pair;
        int om = DIR_ROTATE_OPPOSITE[dir];

        world_set_meta(w, bx, y, bz, om | bits, 3);
        tripwire_notify(w, bx, y, bz, om);
    }

    /* func_150135_a here: its branches only sound, and with sched false none
     * of the four answers, so nothing runs */

    if (!gone)
    {
        world_set_meta(w, x, y, z, meta, 3);

        /* p7 is false on this path, so no func_150134_a */
    }

    if (connected != attached)
    {
        for (int i = 1; i < pair; ++i)
        {
            int m = line[i];

            if (m >= 0)
            {
                if (attached) m |= 4;
                else m &= -5;

                world_set_meta(w, x + DIR_OFF_X[dir] * i, y, z + DIR_OFF_Z[dir] * i, m, 3);
            }
        }
    }
}

/* BlockTripWire.onBlockAdded: the floor bit, then func_150138_a, the hooks
 * the two scan directions face learn the wire. */
void place_tripwire_on_added(struct world *w, int x, int y, int z)
{
    int m = solid_top(w, x, y - 1, z) ? 0 : 2;

    world_set_meta(w, x, y, z, m, 3);

    /* func_150138_a: Direction.offsetX/offsetZ [0] and [1], the scans along
     * +z and -x */
    for (int d = 0; d < 2; ++d)
    {
        int dx = DIR_OFF_X[d], dz = DIR_OFF_Z[d];

        for (int i = 1; i < 42; ++i)
        {
            int b = block_at(w, x + dx * i, y, z + dz * i);

            if (b == 131)   /* tripwire hook */
            {
                int hm = meta_at(w, x + dx * i, y, z + dz * i);

                if ((hm & 3) == DIR_ROTATE_OPPOSITE[d])
                    tripwire_hook_update(w, x + dx * i, y, z + dz * i, 0, hm, 1, i, m);

                break;
            }

            if (b != 132) break;   /* not tripwire */
        }
    }
}

/* Block.onPostBlockPlaced, by class. */
static void on_post_block_placed(struct world *w, int block, int meta, int x, int y, int z)
{
    if (is_block_class(block, "BlockTripWireHook"))
        tripwire_hook_update(w, x, y, z, 0, meta, 0, -1, 0);
}

/* ------------------------------------------------------- the block callbacks
 * Block.onBlockPlacedBy, by class: the facings that read the placer. */

static void on_block_placed_by(struct world *w, int block, int meta, int x, int y, int z,
                               const struct placer *p, const struct place_stack *stack)
{
    (void)stack;

    if (is_block_class(block, "BlockStairs"))
    {
        int q = yaw_quad(p->yaw, 4.0F, 0.5) & 3;
        int keep = meta_at(w, x, y, z) & 4;

        world_set_meta(w, x, y, z, (q == 0 ? 2 : q == 1 ? 1 : q == 2 ? 3 : 0) | keep, 2);
    }
    else if (is_block_class(block, "BlockFenceGate"))
    {
        world_set_meta(w, x, y, z, yaw_quad(p->yaw, 4.0F, 0.5) & 3, 2);
    }
    else if (is_block_class(block, "BlockFurnace"))
    {
        int q = yaw_quad(p->yaw, 4.0F, 0.5) & 3;

        world_set_meta(w, x, y, z, q == 0 ? 2 : q == 1 ? 5 : q == 2 ? 3 : 4, 2);
    }
    else if (is_block_class(block, "BlockDispenser") || is_block_class(block, "BlockDropper") ||
             is_block_class(block, "BlockPistonBase"))
    {
        world_set_meta(w, x, y, z, func_150071_a(p, x, y, z), 2);

        /* BlockPistonBase.func_150078_e: every power input is zero in a
         * redstone-off world, so nothing happens. */
    }
    else if (is_block_class(block, "BlockAnvil"))
    {
        /* BlockAnvil.onBlockPlacedBy: the yaw turns the facing bits, the
         * damage bits the item placed stay */
        int q = (yaw_quad(p->yaw, 4.0F, 0.5) & 3) + 1;
        int q4 = q % 4;
        int keep = meta_at(w, x, y, z) >> 2;

        world_set_meta(w, x, y, z, (q4 == 0 ? 2 : q4 == 1 ? 3 : q4 == 2 ? 0 : 1) | keep << 2, 2);
    }
    else if (is_block_class(block, "BlockPumpkin"))
    {
        world_set_meta(w, x, y, z, yaw_quad(p->yaw, 4.0F, 2.5F) & 3, 2);
    }
    else if (is_block_class(block, "BlockChest"))
    {
        int bzn = block_at(w, x, y, z - 1);
        int bzp = block_at(w, x, y, z + 1);
        int bxn = block_at(w, x - 1, y, z);
        int bxp = block_at(w, x + 1, y, z);
        int q = yaw_quad(p->yaw, 4.0F, 0.5) & 3;
        int facing = q == 0 ? 2 : q == 1 ? 5 : q == 2 ? 3 : 4;

        if (bzn != block && bzp != block && bxn != block && bxp != block)
        {
            world_set_meta(w, x, y, z, facing, 3);
        }
        else
        {
            if ((bzn == block || bzp == block) && (facing == 4 || facing == 5))
            {
                if (bzn == block) world_set_meta(w, x, y, z - 1, facing, 3);
                else world_set_meta(w, x, y, z + 1, facing, 3);

                world_set_meta(w, x, y, z, facing, 3);
            }

            if ((bxn == block || bxp == block) && (facing == 2 || facing == 3))
            {
                if (bxn == block) world_set_meta(w, x - 1, y, z, facing, 3);
                else world_set_meta(w, x + 1, y, z, facing, 3);

                world_set_meta(w, x, y, z, facing, 3);
            }
        }
    }
    else if (is_block_class(block, "BlockEndPortalFrame"))
    {
        world_set_meta(w, x, y, z, ((yaw_quad(p->yaw, 4.0F, 0.5) & 3) + 2) % 4, 2);
    }
    else if (is_block_class(block, "BlockDoublePlant"))
    {
        /* BlockDoublePlant.onBlockPlacedBy grows the upper half from the
         * placer's yaw; the lower half keeps its placed meta */
        int q = ((yaw_quad(p->yaw, 4.0F, 0.5) & 3) + 2) % 4;

        world_set_block(w, x, y + 1, z, block, 8 | q, 2);
    }
    else if (is_block_class(block, "BlockEnderChest"))
    {
        int q = yaw_quad(p->yaw, 4.0F, 0.5) & 3;

        world_set_meta(w, x, y, z, q == 0 ? 2 : q == 1 ? 5 : q == 2 ? 3 : 4, 2);
    }
    else if (is_block_class(block, "BlockRedstoneDiode") || is_block_class(block, "BlockRedstoneRepeater") ||
             is_block_class(block, "BlockRedstoneComparator"))
    {
        int q = ((yaw_quad(p->yaw, 4.0F, 0.5) & 3) + 2) % 4;

        world_set_meta(w, x, y, z, q, 3);
        /* func_149900_a: a repeater's input (World's indirect power, 0 with
         * redstone off, or a wire's strength) is 0; a comparator's reads the
         * container behind it (comparator.c) and, when it should be powered,
         * asks for a tick next time */
        if (comparator_is(block) && comparator_should_power(w, x, y, z, q))
            ticks_schedule_block_update(w, x, y, z, block, 1);
    }
    else if (is_block_class(block, "BlockCocoa"))
    {
        world_set_meta(w, x, y, z, yaw_quad(p->yaw, 4.0F, 0.5) & 3, 2);
    }
    else if (is_block_class(block, "BlockLever"))
    {
        int m = meta_at(w, x, y, z);
        int dir = m & 7;
        int powered = m & 8;
        int q = yaw_quad(p->yaw, 4.0F, 0.5) & 3;

        if (dir == 5)
        {
            world_set_meta(w, x, y, z, ((q & 1) == 0 ? 5 : 6) | powered, 2);
        }
        else if (dir == 0)
        {
            world_set_meta(w, x, y, z, ((q & 1) == 0 ? 7 : 0) | powered, 2);
        }
    }
    else if (is_block_class(block, "BlockSkull"))
    {
        /* not on the item's path; ItemSkull poses its own block */
        world_set_meta(w, x, y, z, yaw_quad(p->yaw, 4.0F, 2.5F) & 3, 2);
    }

    /* the container blocks' onBlockPlacedBy only copy a display name, and the
     * probe's stacks carry no tag, so they end here */
}

/* ------------------------------------------------------------- the item flows */

/* The target cell adjust ItemBlock and ItemReed share: a snow layer thinner
 * than one is a floor, and a vine, tall grass or a dead bush keeps the cell. */
static void item_target_adjust(struct world *w, int *x, int *y, int *z, int *side)
{
    if (block_at(w, *x, *y, *z) == 78 && (meta_at(w, *x, *y, *z) & 7) < 1)
    {
        *side = 1;
        return;
    }

    int id = block_at(w, *x, *y, *z);

    if (id == 106 || id == 31 || id == 32) return;   /* vine, tall grass, dead bush */

    if (*side == 0) --*y;
    if (*side == 1) ++*y;
    if (*side == 2) --*z;
    if (*side == 3) ++*z;
    if (*side == 4) --*x;
    if (*side == 5) ++*x;
}

/* ItemBlock.onItemUse, every ItemBlock subclass's default body. */
static int item_block_on_item_use(struct world *w, const struct placer *p, struct place_stack *stack,
                                  int x, int y, int z, int side, float hx, float hy, float hz)
{
    int item = stack->item & 4095;
    int block = ITEMS[item].block_id;

    item_target_adjust(w, &x, &y, &z, &side);

    if (stack->count == 0) return 0;
    /* canPlayerEdit: capabilities.allowEdit */
    if (y == 255 && is_solid_mat(block)) return 0;

    if (can_place_entity_on_side(w, block, x, y, z, side, stack, 1))
    {
        int meta = item_get_metadata(item, stack->damage);
        int pm = on_block_placed(w, block, x, y, z, side, hx, hy, hz, meta);

        if (world_set_block(w, x, y, z, block, pm, 3))
        {
            if (block_at(w, x, y, z) == block)
            {
                on_block_placed_by(w, block, meta, x, y, z, p, stack);
                on_post_block_placed(w, block, pm, x, y, z);
            }

            /* playSoundEffect: no world effect */
            --stack->count;
        }

        return 1;
    }

    return 0;
}

/* ItemReed.onItemUse: the reeds, the cake, the cauldron, the brewing stand,
 * the flower pot, the repeater, the comparator and the tripwire. Returns true
 * even when the placement was refused. */
static int item_reed_on_item_use(struct world *w, const struct placer *p, struct place_stack *stack,
                                 int x, int y, int z, int side, float hx, float hy, float hz)
{
    int block = reed_block(stack->item);

    if (block < 0)
    {
        fprintf(stderr, "place: item %d has no reed block\n", stack->item);
        exit(2);
    }

    item_target_adjust(w, &x, &y, &z, &side);

    /* canPlayerEdit: allowEdit */
    if (stack->count == 0) return 0;

    if (can_place_entity_on_side(w, block, x, y, z, side, stack, 0))
    {
        int pm = on_block_placed(w, block, x, y, z, side, hx, hy, hz, 0);

        if (world_set_block(w, x, y, z, block, pm, 3))
        {
            if (block_at(w, x, y, z) == block)
            {
                on_block_placed_by(w, block, 0, x, y, z, p, stack);
                on_post_block_placed(w, block, pm, x, y, z);
            }

            /* playSoundEffect: no world effect */
            --stack->count;
        }
    }

    return 1;
}

/* ItemSnow.onItemUse: stack the layer, else the ItemBlock path. */
static int item_snow_on_item_use(struct world *w, const struct placer *p, struct place_stack *stack,
                                 int x, int y, int z, int side, float hx, float hy, float hz)
{
    if (stack->count == 0) return 0;
    /* canPlayerEdit: allowEdit */

    if (block_at(w, x, y, z) == 78)
    {
        int meta = meta_at(w, x, y, z);
        int m7 = meta & 7;

        /* the checkNoEntityCollision over the raised layer is empty */
        if (m7 <= 6 && world_set_meta(w, x, y, z, (m7 + 1) | (meta & -8), 2))
        {
            --stack->count;
            return 1;
        }
    }

    return item_block_on_item_use(w, p, stack, x, y, z, side, hx, hy, hz);
}

/* ItemSlab.onItemUse: the double slab items take the ItemBlock path; the
 * single ones take the merge path first. */
static int item_slab_on_item_use(struct world *w, const struct placer *p, struct place_stack *stack,
                                 int x, int y, int z, int side, float hx, float hy, float hz)
{
    int item = stack->item & 4095;

    if (item == 43 || item == 125)   /* field_150948_b */
        return item_block_on_item_use(w, p, stack, x, y, z, side, hx, hy, hz);

    if (stack->count == 0) return 0;
    /* canPlayerEdit: allowEdit */

    int single = is_double_slab(ITEMS[item].block_id) ? (ITEMS[item].block_id == 43 ? 44 : 126) : ITEMS[item].block_id;
    int dbl = is_double_slab(ITEMS[item].block_id) ? ITEMS[item].block_id : (ITEMS[item].block_id == 44 ? 43 : 125);

    int id = block_at(w, x, y, z);
    int meta = meta_at(w, x, y, z);
    int m7 = meta & 7;
    int top = (meta & 8) != 0;

    if (((side == 1 && !top) || (side == 0 && top)) && id == single && m7 == stack->damage)
    {
        /* the checkNoEntityCollision over the full slab's box is empty */
        if (world_set_block(w, x, y, z, dbl, m7, 3))
        {
            /* playSoundEffect: no world effect */
            --stack->count;
        }

        return 1;
    }

    /* BlockSlab.func_150946_a */
    {
        int ax = x, ay = y, az = z, aside = side;

        if (aside == 0) --ay;
        if (aside == 1) ++ay;
        if (aside == 2) --az;
        if (aside == 3) ++az;
        if (aside == 4) --ax;
        if (aside == 5) ++ax;

        if (block_at(w, ax, ay, az) == single && (meta_at(w, ax, ay, az) & 7) == stack->damage)
        {
            if (world_set_block(w, ax, ay, az, dbl, meta_at(w, ax, ay, az) & 7, 3))
            {
                --stack->count;
            }

            return 1;
        }
    }

    return item_block_on_item_use(w, p, stack, x, y, z, side, hx, hy, hz);
}

/* ItemBed.onItemUse. */
static int item_bed_on_item_use(struct world *w, const struct placer *p, struct place_stack *stack,
                                int x, int y, int z, int side, float hx, float hy, float hz)
{
    (void)hx; (void)hy; (void)hz;

    if (side != 1) return 0;

    ++y;
    int q = yaw_quad(p->yaw, 4.0F, 0.5) & 3;
    int hdx = 0, hdz = 0;

    if (q == 0) hdz = 1;
    if (q == 1) hdx = -1;
    if (q == 2) hdz = -1;
    if (q == 3) hdx = 1;

    /* canPlayerEdit: allowEdit, both cells */
    if (is_air_block(w, x, y, z) && is_air_block(w, x + hdx, y, z + hdz) &&
        solid_top(w, x, y - 1, z) && solid_top(w, x + hdx, y - 1, z + hdz))
    {
        world_set_block(w, x, y, z, 26, q, 3);

        if (block_at(w, x, y, z) == 26) world_set_block(w, x + hdx, y, z + hdz, 26, q + 8, 3);

        --stack->count;
        return 1;
    }

    return 0;
}

/* ItemDoor.func_150924_a: the pair, the hinge from the solid neighbours. */
static void door_place_pair(struct world *w, int x, int y, int z, int dir, int door)
{
    int hdx = 0, hdz = 0;

    if (dir == 0) hdz = 1;
    if (dir == 1) hdx = -1;
    if (dir == 2) hdz = -1;
    if (dir == 3) hdx = 1;

    int left = (normal_cube_at(w, x - hdx, y, z - hdz) ? 1 : 0) + (normal_cube_at(w, x - hdx, y + 1, z - hdz) ? 1 : 0);
    int right = (normal_cube_at(w, x + hdx, y, z + hdz) ? 1 : 0) + (normal_cube_at(w, x + hdx, y + 1, z + hdz) ? 1 : 0);
    int door_before = block_at(w, x - hdx, y, z - hdz) == door || block_at(w, x - hdx, y + 1, z - hdz) == door;
    int door_after = block_at(w, x + hdx, y, z + hdz) == door || block_at(w, x + hdx, y + 1, z + hdz) == door;
    int hinge = 0;

    if (door_before && !door_after) hinge = 1;
    else if (right > left) hinge = 1;

    world_set_block(w, x, y, z, door, dir, 2);
    world_set_block(w, x, y + 1, z, door, 8 | (hinge ? 1 : 0), 2);
    world_notify_neighbors(w, x, y, z, door);
    world_notify_neighbors(w, x, y + 1, z, door);
}

/* ItemDoor.onItemUse. */
static int item_door_on_item_use(struct world *w, const struct placer *p, struct place_stack *stack,
                                 int x, int y, int z, int side, float hx, float hy, float hz)
{
    (void)hx; (void)hy; (void)hz;

    if (side != 1) return 0;

    ++y;
    int door = stack->item == 324 ? 64 : 71;

    /* canPlayerEdit: allowEdit, both cells */
    if (!can_place_block_at(w, door, x, y, z)) return 0;

    int q = yaw_quad(p->yaw + 180.0F, 4.0F, -0.5) & 3;

    door_place_pair(w, x, y, z, q, door);
    --stack->count;
    return 1;
}

/* ItemSign.onItemUse. */
static int item_sign_on_item_use(struct world *w, const struct placer *p, struct place_stack *stack,
                                 int x, int y, int z, int side, float hx, float hy, float hz)
{
    (void)hx; (void)hy; (void)hz;

    if (side == 0) return 0;

    if (!is_solid_mat(block_at(w, x, y, z))) return 0;

    if (side == 1) ++y;
    if (side == 2) --z;
    if (side == 3) ++z;
    if (side == 4) --x;
    if (side == 5) ++x;

    /* canPlayerEdit: allowEdit */

    if (!can_place_block_at(w, 63, x, y, z)) return 0;

    if (side == 1)
    {
        int meta = yaw_quad(p->yaw + 180.0F, 16.0F, 0.5) & 15;

        world_set_block(w, x, y, z, 63, meta, 3);
    }
    else
    {
        world_set_block(w, x, y, z, 68, side, 3);
    }

    /* the sign's tile entity stays blank: sign_editing is off */

    --stack->count;
    return 1;
}

/* ItemSkull.onItemUse: the block with the side as its metadata, then the
 * tile entity's type and rotation. */
static int item_skull_on_item_use(struct world *w, const struct placer *p, struct place_stack *stack,
                                  int x, int y, int z, int side, float hx, float hy, float hz)
{
    (void)hx; (void)hy; (void)hz;

    if (side == 0) return 0;

    if (!is_solid_mat(block_at(w, x, y, z))) return 0;

    if (side == 1) ++y;
    if (side == 2) --z;
    if (side == 3) ++z;
    if (side == 4) --x;
    if (side == 5) ++x;

    world_set_block(w, x, y, z, 144, side, 2);
    int rot = side == 1 ? yaw_quad(p->yaw, 16.0F, 0.5) & 15 : 0;
    struct tile_entity *te = world_tile_entity(w, x, y, z);

    if (te != NULL && te->kind == TE_SKULL)
    {
        /* func_152107_a / func_152106_a(null): the type, no owner */
        te->skull_type = stack->damage;
        te->skull_rot = rot;

        /* BlockSkull.func_149965_a: wither is off */
    }

    --stack->count;
    return 1;
}

/* ItemSeeds.onItemUse and ItemSeedFood.onItemUse. */
static int item_seeds_on_item_use(struct world *w, const struct placer *p, struct place_stack *stack,
                                  int x, int y, int z, int side, float hx, float hy, float hz)
{
    (void)p; (void)hx; (void)hy; (void)hz;

    if (side != 1) return 0;

    /* canPlayerEdit: allowEdit, both cells */

    int block = seed_block(stack->item);
    int soil = seed_soil(stack->item);

    if (block < 0)
    {
        fprintf(stderr, "place: item %d has no seed block\n", stack->item);
        exit(2);
    }

    if (block_at(w, x, y, z) == soil && is_air_block(w, x, y + 1, z))
    {
        world_set_block(w, x, y + 1, z, block, 0, 3);
        --stack->count;
        return 1;
    }

    return 0;
}

/* ItemHoe.onItemUse: the tilling. */
static int item_hoe_on_item_use(struct world *w, const struct placer *p, struct place_stack *stack,
                                int x, int y, int z, int side, float hx, float hy, float hz)
{
    (void)p; (void)hx; (void)hy; (void)hz;

    /* canPlayerEdit: allowEdit */

    int id = block_at(w, x, y, z);

    if (side != 0 && mat_of(block_at(w, x, y + 1, z)) == mat_named("air") && (id == 2 || id == 3))
    {
        world_set_block(w, x, y, z, 60, 0, 3);   /* farmland, default flags */

        /* ItemStack.damageItem(1, placer): no unbreaking, survival */
        stack->damage += 1;

        if (stack->damage >= ITEMS[stack->item & 4095].max_damage)
        {
            --stack->count;

            if (stack->count < 0) stack->count = 0;

            stack->damage = 0;
        }

        return 1;
    }

    return 0;
}

/* ItemRedstone.onItemUse. */
static int item_redstone_on_item_use(struct world *w, const struct placer *p, struct place_stack *stack,
                                     int x, int y, int z, int side, float hx, float hy, float hz)
{
    (void)p; (void)hx; (void)hy; (void)hz;

    if (block_at(w, x, y, z) != 78)
    {
        /* ItemRedstone's own adjust: the plain six-side shift, without the
         * ItemBlock keep of a vine, tall grass or dead bush cell */
        if (side == 0) --y;
        if (side == 1) ++y;
        if (side == 2) --z;
        if (side == 3) ++z;
        if (side == 4) --x;
        if (side == 5) ++x;

        if (!is_air_block(w, x, y, z)) return 0;
    }

    /* canPlayerEdit: allowEdit */

    if (can_place_block_at(w, 55, x, y, z))
    {
        --stack->count;
        world_set_block(w, x, y, z, 55, 0, 3);
    }

    return 1;
}

/* ItemDye.onItemUse, the cocoa branch (15 is bonemeal, another lane's). */
static int item_dye_on_item_use(struct world *w, const struct placer *p, struct place_stack *stack,
                                int x, int y, int z, int side, float hx, float hy, float hz)
{
    (void)p;

    if (stack->damage != 3) return 0;

    int id = block_at(w, x, y, z);
    int meta = meta_at(w, x, y, z);

    /* Blocks.log and BlockLog.func_150165_c(meta) == 3: a jungle log */
    if (id == 17 && (meta & 3) == 3)
    {
        if (side == 0 || side == 1) return 0;

        if (side == 2) --z;
        if (side == 3) ++z;
        if (side == 4) --x;
        if (side == 5) ++x;

        /* canPlayerEdit: allowEdit */

        if (is_air_block(w, x, y, z))
        {
            int m = on_block_placed(w, 127, x, y, z, side, hx, hy, hz, 0);

            world_set_block(w, x, y, z, 127, m, 2);

            /* capabilities.isCreativeMode is false */
            --stack->count;
        }

        return 1;
    }

    return 0;
}

/* ------------------------------------------- the eye of ender's frame fill */

/* World.func_147453_f: the comparators around a filled end portal frame learn
 * it changed (comparator.c). */
static void update_comparators(struct world *w, int x, int y, int z)
{
    comparator_notify(w, x, y, z, 120);
}

/* ItemEnderEye.onItemUse: filling an end portal frame, and the ring check. */
static int item_endereye_on_item_use(struct world *w, const struct placer *p, struct place_stack *stack,
                                     int x, int y, int z, int side, float hx, float hy, float hz)
{
    (void)p; (void)hx; (void)hy; (void)hz; (void)side;

    int id = block_at(w, x, y, z);
    int meta = meta_at(w, x, y, z);

    /* canPlayerEdit: allowEdit */
    if (id != 120 || (meta & 4) == 4) return 0;   /* not an eyeless end portal frame */

    world_set_meta(w, x, y, z, meta + 4, 2);
    update_comparators(w, x, y, z);
    --stack->count;

    /* the 16-particle smoke loop: 32 Item.itemRand draws on the server world */
    if (case_item != NULL)
        for (int i = 0; i < 32; ++i) det_split_float_role(case_det, case_item, DET_OTHER);

    int dir = meta & 3;
    int right = DIR_ROTATE_RIGHT[dir];
    int last = 0, first = 0;
    int seen_first = 0;
    int ring = 1;

    for (int i = -2; i <= 2; ++i)
    {
        int bx = x + DIR_OFF_X[right] * i;
        int bz = z + DIR_OFF_Z[right] * i;

        if (block_at(w, bx, y, bz) == 120)
        {
            if ((meta_at(w, bx, y, bz) & 4) == 0)
            {
                ring = 0;
                break;
            }

            last = i;

            if (!seen_first)
            {
                first = i;
                seen_first = 1;
            }
        }
    }

    if (ring && last == first + 2)
    {
        for (int i = first; i <= last; ++i)
        {
            int bx = x + DIR_OFF_X[right] * i + DIR_OFF_X[dir] * 4;
            int bz = z + DIR_OFF_Z[right] * i + DIR_OFF_Z[dir] * 4;

            if (block_at(w, bx, y, bz) != 120 || (meta_at(w, bx, y, bz) & 4) == 0) ring = 0;
        }

        for (int i = first - 1; i <= last + 1; i += 4)
        {
            for (int k = 1; k <= 3; ++k)
            {
                int bx = x + DIR_OFF_X[right] * i + DIR_OFF_X[dir] * k;
                int bz = z + DIR_OFF_Z[right] * i + DIR_OFF_Z[dir] * k;

                if (block_at(w, bx, y, bz) != 120 || (meta_at(w, bx, y, bz) & 4) == 0) ring = 0;
            }
        }

        if (ring)
        {
            for (int i = first; i <= last; ++i)
            {
                for (int k = 1; k <= 3; ++k)
                {
                    int bx = x + DIR_OFF_X[right] * i + DIR_OFF_X[dir] * k;
                    int bz = z + DIR_OFF_Z[right] * i + DIR_OFF_Z[dir] * k;

                    world_set_block(w, bx, y, bz, 119, 0, 2);
                }
            }
        }
    }

    return 1;
}
/* ------------------------------------------- the case streams and the drops */

void place_set_case(int64_t case_seed, det_state *det, det_rng *math, det_split *item_rand)
{
    case_rand = (jrand){0};
    jr_seed(&case_rand, case_seed);
    case_det = det;
    case_math = math;
    case_item = item_rand;
    /* the thread the case runs on: the role whose Math.random stream the
     * caller handed over (a live server or client placement), else a probe's */
    case_role = DET_OTHER;
    for (int r = 0; det != NULL && math != NULL && r < DET_ROLES; ++r)
        if (math == &det->math[r]) case_role = r;
}

/* BlockLiquid.func_149799_m: the fizz the lava's water contact plays. The
 * sound's pitch draws the world Random twice and the eight smoke particles
 * draw the shared Math.random stream twice each. */
void place_lava_fizz(void)
{
    jr_float(&case_rand);
    jr_float(&case_rand);

    if (case_math != NULL)
        for (int i = 0; i < 16; ++i) (void)jr_double(&case_math->r);
}

/* BlockFire.func_149847_e: a flammable block in one of the six neighbours. */
int place_fire_flammable_near(struct world *w, int x, int y, int z)
{
    return fire_flammable_near(w, x, y, z);
}

/* BlockFire.onBlockAdded's scheduled tick delay: the world Random's
 * nextInt(10) part (the 30 comes from func_149738_a). */
int place_fire_tick_draw(void)
{
    return jr_int_n(&case_rand, 10);
}

/* The dispenser tile entity's field_146021_j is a Det.newRandom(): its
 * constructor spends one seeder long. */
void place_dispenser_seeder(void)
{
    if (case_det != NULL) (void)det_new_random_role(case_det, case_role);
}

det_state *place_case_det_swap(det_state *det)
{
    det_state *old = case_det;
    case_det = det;
    return old;
}

/* The drop of one break against the case's world Random, which the next drop
 * of the same case keeps spending. The Math.random draws the EntityItem
 * constructor makes come out of the shared stream. */
static void block_drop_stream(struct world *w, int x, int y, int z, int block, int meta, int fortune)
{
    if (w->on_drop == NULL) return;

    struct drop_case cs = {block & 4095, meta & 255, fortune, x, y, z, 0, case_math != NULL ? &case_math->r : NULL, NULL};
    struct drop_ent *out ENV_LOCAL = envstack_take(DROPS_MAX_ENTS * sizeof *out);
    int n;

    if (block == 144)
        n = drops_skull_break_stream(&cs, &case_rand, fortune, out, DROPS_MAX_ENTS);
    else
        n = drops_break_stream(&cs, &case_rand, out, DROPS_MAX_ENTS);

    if (n < 0)
    {
        fprintf(stderr, "place: block %d has no drop path\n", block);
        exit(2);
    }

    /* Each EntityItem's constructor draws the seeder: Entity.rand is
     * Det.newRandom (one long) and the UUID is two more (Det.uuid). */
    for (int i = 0; i < n; ++i)
    {
        det_rng entity_rand = det_new_random_role(case_det, DET_OTHER);
        int64_t msb, lsb;

        (void)entity_rand;
        det_uuid_role(case_det, DET_OTHER, &msb, &lsb);
    }

    for (int i = 0; i < n; ++i) w->on_drop(w->on_drop_ctx, &out[i]);
}

void place_block_drop(struct world *w, int x, int y, int z, int block, int meta)
{
    block_drop_stream(w, x, y, z, block, meta, 0);
}

/* BlockSkull.breakBlock: the skull item at the tile entity's type drops
 * directly through dropBlockAsItem_do. */
void place_skull_break_drop(struct world *w, int x, int y, int z, int type)
{
    block_drop_stream(w, x, y, z, 144, 0, type);
}

/* One item entity for a caller that already knows the item and the damage
 * (blockcb's door and string drops when no tick environment is published). */
void place_item_drop(struct world *w, int x, int y, int z, int item, int damage)
{
    if (w->on_drop == NULL) return;


    struct drop_case cs = {0, 0, 0, x, y, z, 0, case_math != NULL ? &case_math->r : NULL, NULL};
    struct drop_ent *out ENV_LOCAL = envstack_take(DROPS_MAX_ENTS * sizeof *out);
    int n = drops_item_break_stream(&cs, &case_rand, item, damage, 1, out, DROPS_MAX_ENTS);

    for (int i = 0; i < n; ++i)
    {
        det_rng entity_rand = det_new_random_role(case_det, DET_OTHER);
        int64_t msb, lsb;

        (void)entity_rand;
        det_uuid_role(case_det, DET_OTHER, &msb, &lsb);
    }

    for (int i = 0; i < n; ++i) w->on_drop(w->on_drop_ctx, &out[i]);
}

/* --------------------------------------------- the entry: onItemUse dispatch */

/* The ItemBlock family place_try hands to item_block_on_item_use. */
static int is_item_block_family(int item)
{
    return is_item_class(item, "ItemBlock") || is_item_class(item, "ItemCloth") || is_item_class(item, "ItemColored") ||
           is_item_class(item, "ItemMultiTexture") || is_item_class(item, "ItemLeaves") ||
           is_item_class(item, "ItemPiston") || is_item_class(item, "ItemAnvilBlock") ||
           is_item_class(item, "ItemBlockWithMetadata") || is_item_class(item, "ItemDoublePlant") ||
           is_item_class(item, "ItemLilyPad");
}

/* ItemBlock.onItemUse's answer without the placement: the checks before the
 * setBlock (whose result the method does not return). */
static int item_block_would_use(struct world *w, const struct place_stack *stack, int x, int y, int z, int side)
{
    int block = ITEMS[stack->item & 4095].block_id;

    item_target_adjust(w, &x, &y, &z, &side);
    if (stack->count == 0) return 0;
    if (y == 255 && is_solid_mat(block)) return 0;
    return can_place_entity_on_side(w, block, x, y, z, side, stack, 1) ? 2 : 0;
}

int place_would_use(struct world *w, const struct placer *p, const struct place_stack *stack,
                    int x, int y, int z, int side)
{
    int item = stack->item & 4095;

    if (is_item_block_family(item)) return item_block_would_use(w, stack, x, y, z, side);

    if (is_item_class(item, "ItemSlab"))
    {
        if (item == 43 || item == 125) return item_block_would_use(w, stack, x, y, z, side);
        if (stack->count == 0) return 0;
        int single = is_double_slab(ITEMS[item].block_id) ? (ITEMS[item].block_id == 43 ? 44 : 126) : ITEMS[item].block_id;
        int meta = meta_at(w, x, y, z);
        int top = (meta & 8) != 0;
        if (((side == 1 && !top) || (side == 0 && top)) && block_at(w, x, y, z) == single && (meta & 7) == stack->damage)
            return 2;
        int ax = x, ay = y, az = z;
        if (side == 0) --ay;
        if (side == 1) ++ay;
        if (side == 2) --az;
        if (side == 3) ++az;
        if (side == 4) --ax;
        if (side == 5) ++ax;
        if (block_at(w, ax, ay, az) == single && (meta_at(w, ax, ay, az) & 7) == stack->damage) return 2;
        return item_block_would_use(w, stack, x, y, z, side);
    }

    if (is_item_class(item, "ItemSnow"))
    {
        if (stack->count == 0) return 0;
        if (block_at(w, x, y, z) == 78 && (meta_at(w, x, y, z) & 7) <= 6) return 2;
        return item_block_would_use(w, stack, x, y, z, side);
    }

    if (is_item_class(item, "ItemReed"))
    {
        /* true once the stack is not empty, placed or not */
        int block = reed_block(stack->item);
        if (block < 0) return -1;
        item_target_adjust(w, &x, &y, &z, &side);
        if (stack->count == 0) return 0;
        return can_place_entity_on_side(w, block, x, y, z, side, stack, 0) ? 2 : 1;
    }

    if (is_item_class(item, "ItemBed"))
    {
        if (side != 1) return 0;
        ++y;
        int q = yaw_quad(p->yaw, 4.0F, 0.5) & 3;
        int hdx = q == 1 ? -1 : q == 3 ? 1 : 0, hdz = q == 0 ? 1 : q == 2 ? -1 : 0;
        return is_air_block(w, x, y, z) && is_air_block(w, x + hdx, y, z + hdz) &&
               solid_top(w, x, y - 1, z) && solid_top(w, x + hdx, y - 1, z + hdz) ? 2 : 0;
    }

    if (is_item_class(item, "ItemDoor"))
        return side == 1 && can_place_block_at(w, stack->item == 324 ? 64 : 71, x, y + 1, z) ? 2 : 0;

    if (is_item_class(item, "ItemSign") || is_item_class(item, "ItemSkull"))
    {
        if (side == 0 || !is_solid_mat(block_at(w, x, y, z))) return 0;
        if (is_item_class(item, "ItemSkull")) return 2;
        if (side == 1) ++y;
        if (side == 2) --z;
        if (side == 3) ++z;
        if (side == 4) --x;
        if (side == 5) ++x;
        return can_place_block_at(w, 63, x, y, z) ? 2 : 0;
    }

    if (is_item_class(item, "ItemSeeds") || is_item_class(item, "ItemSeedFood"))
    {
        int soil = seed_soil(stack->item);
        return side == 1 && seed_block(stack->item) >= 0 && block_at(w, x, y, z) == soil &&
               is_air_block(w, x, y + 1, z) ? 2 : 0;
    }

    if (is_item_class(item, "ItemHoe"))
    {
        int id = block_at(w, x, y, z);
        return side != 0 && mat_of(block_at(w, x, y + 1, z)) == mat_named("air") && (id == 2 || id == 3) ? 1 : 0;
    }

    if (is_item_class(item, "ItemRedstone"))
    {
        if (block_at(w, x, y, z) != 78)
        {
            if (side == 0) --y;
            if (side == 1) ++y;
            if (side == 2) --z;
            if (side == 3) ++z;
            if (side == 4) --x;
            if (side == 5) ++x;
            if (!is_air_block(w, x, y, z)) return 0;
        }
        return can_place_block_at(w, 55, x, y, z) ? 2 : 1;
    }

    return -1;
}

/* ItemBlock.func_150936_a, with ItemSlab's override: the client's test in
 * PlayerControllerMP.onPlayerRightClick before the C08. The target cell
 * moves off the clicked side unless the clicked block is a snow layer (any
 * depth: the side becomes the top), a vine, tall grass or a dead bush; then
 * World.canPlaceEntityOnSide with no entity excluded. ItemSlab first answers
 * true for a matching single slab on the clicked face or in the cell beyond
 * it, then asks ItemBlock's with the clicked cell. An item that is no
 * ItemBlock answers true. */
int place_item_block_precheck(struct world *w, const struct place_stack *stack, int x, int y, int z, int side)
{
    int item = stack->item & 4095;
    int slab = is_item_class(item, "ItemSlab");

    if (!slab && !is_item_block_family(item) && !is_item_class(item, "ItemSnow")) return 1;

    if (slab)
    {
        int single = ITEMS[item].block_id == 43 ? 44 : ITEMS[item].block_id == 125 ? 126 : ITEMS[item].block_id;
        int meta = meta_at(w, x, y, z);
        int top = (meta & 8) != 0;
        if (((side == 1 && !top) || (side == 0 && top)) && block_at(w, x, y, z) == single && (meta & 7) == stack->damage)
            return 1;
        int ax = x, ay = y, az = z;
        if (side == 0) --ay;
        if (side == 1) ++ay;
        if (side == 2) --az;
        if (side == 3) ++az;
        if (side == 4) --ax;
        if (side == 5) ++ax;
        if (block_at(w, ax, ay, az) == single && (meta_at(w, ax, ay, az) & 7) == stack->damage) return 1;
    }

    int id = block_at(w, x, y, z);

    if (id == 78) side = 1;
    else if (id != 106 && id != 31 && id != 32)
    {
        if (side == 0) --y;
        if (side == 1) ++y;
        if (side == 2) --z;
        if (side == 3) ++z;
        if (side == 4) --x;
        if (side == 5) ++x;
    }

    return can_place_entity_on_side(w, ITEMS[item].block_id, x, y, z, side, stack, 0);
}

int place_try(struct world *w, const struct placer *p, struct place_stack *stack,
              int x, int y, int z, int side, float hx, float hy, float hz)
{
    int item = stack->item & 4095;
    int ret;

    if (is_item_class(item, "ItemBlock") || is_item_class(item, "ItemCloth") || is_item_class(item, "ItemColored") ||
        is_item_class(item, "ItemMultiTexture") || is_item_class(item, "ItemLeaves") ||
        is_item_class(item, "ItemPiston") || is_item_class(item, "ItemAnvilBlock") ||
        is_item_class(item, "ItemBlockWithMetadata") || is_item_class(item, "ItemDoublePlant") ||
        is_item_class(item, "ItemLilyPad"))
        ret = item_block_on_item_use(w, p, stack, x, y, z, side, hx, hy, hz);
    else if (is_item_class(item, "ItemSlab"))
        ret = item_slab_on_item_use(w, p, stack, x, y, z, side, hx, hy, hz);
    else if (is_item_class(item, "ItemSnow"))
        ret = item_snow_on_item_use(w, p, stack, x, y, z, side, hx, hy, hz);
    else if (is_item_class(item, "ItemReed"))
        ret = item_reed_on_item_use(w, p, stack, x, y, z, side, hx, hy, hz);
    else if (is_item_class(item, "ItemBed"))
        ret = item_bed_on_item_use(w, p, stack, x, y, z, side, hx, hy, hz);
    else if (is_item_class(item, "ItemDoor"))
        ret = item_door_on_item_use(w, p, stack, x, y, z, side, hx, hy, hz);
    else if (is_item_class(item, "ItemSign"))
        ret = item_sign_on_item_use(w, p, stack, x, y, z, side, hx, hy, hz);
    else if (is_item_class(item, "ItemSkull"))
        ret = item_skull_on_item_use(w, p, stack, x, y, z, side, hx, hy, hz);
    else if (is_item_class(item, "ItemSeeds") || is_item_class(item, "ItemSeedFood"))
        ret = item_seeds_on_item_use(w, p, stack, x, y, z, side, hx, hy, hz);
    else if (is_item_class(item, "ItemHoe"))
        ret = item_hoe_on_item_use(w, p, stack, x, y, z, side, hx, hy, hz);
    else if (is_item_class(item, "ItemRedstone"))
        ret = item_redstone_on_item_use(w, p, stack, x, y, z, side, hx, hy, hz);
    else if (is_item_class(item, "ItemEnderEye"))
        ret = item_endereye_on_item_use(w, p, stack, x, y, z, side, hx, hy, hz);
    else if (is_item_class(item, "ItemDye"))
        ret = item_dye_on_item_use(w, p, stack, x, y, z, side, hx, hy, hz);
    else
    {
        fprintf(stderr, "place: item %d (%s) has no onItemUse\n", stack->item,
                ITEMS[item].class_name != NULL ? ITEMS[item].class_name : "?");
        exit(2);
    }

    /* the use statistic success adds moves no world state */
    return ret;
}

/* The case Random's state after the last call, for the tape replay's live
 * World.rand continuation (place_set_case seeds it per case). */
uint64_t place_world_state(void)
{
    return case_rand.seed;
}

jrand *place_case_rand(void)
{
    return &case_rand;
}

int place_solid_top_surface(struct world *w, int x, int y, int z)
{
    return solid_top(w, x, y, z);
}
