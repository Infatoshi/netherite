/* Item use, see itemuse.h. The Java bodies are spelled out one by one. */
#include "comparator.h"
#include "itemuse.h"
#include "env.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <string.h>

#include "blockcb.h"
#include "blocks.h"
#include "items.h"
#include "jmath.h"
#include "randomtick.h"
#include "raytrace.h"

/* Item ids the bodies switch on, Item.getIdFromItem. */
enum {
    IT_WATERLILY = 111, IT_FLINT = 259, IT_BUCKET = 325, IT_WATER_BUCKET = 326,
    IT_LAVA_BUCKET = 327, IT_DYE = 351, IT_POTION = 373, IT_GLASS_BOTTLE = 374,
    IT_ENDER_EYE = 381, IT_FIRE_CHARGE = 385, IT_RECORD_13 = 2256,
    IT_RECORD_CAT = 2257, IT_WOODEN_HOE = 290, IT_STONE_HOE = 291, IT_IRON_HOE = 292,
    IT_DIAMOND_HOE = 293, IT_GOLDEN_HOE = 294,
};

#define live_det (nw_env->itemuse.live_det)
#define live_role (nw_env->itemuse.live_role)

void itemuse_set_live_det(det_state *det, int role)
{
    live_det = det;
    live_role = role;
}

static void item_sound_random(void)
{
    if (!live_det) return;
    const char *name = "./net/minecraft/item/Item.java:itemRand";
    det_split *stream = det_split_find(live_det, name);
    if (!stream) stream = det_split_random(live_det, name);
    (void)det_split_float_role(live_det, stream, live_role);
}

/* Block ids the bodies switch on. */
enum {
    IU_FLOWING_WATER = 8, IU_WATER = 9, IU_FLOWING_LAVA = 10, IU_LAVA = 11,
    IU_LOG = 17, IU_FIRE = 51, IU_END_PORTAL = 119, IU_END_PORTAL_FRAME = 120,
    IU_COCOA = 127, IU_WATERLILY = 111, IU_JUKEBOX = 84, IU_GRASS = 2, IU_DIRT = 3, IU_FARMLAND = 60,
};

/* net.minecraft.util.Direction. */
static const int OFFSET_X[4] = {0, -1, 0, 1};
static const int OFFSET_Z[4] = {1, 0, -1, 0};
static const int ROTATE_RIGHT[4] = {1, 2, 3, 0};
static const int ROTATE_OPPOSITE[4] = {2, 3, 0, 1};
static const int FACING_TO_DIRECTION[6] = {-1, -1, 2, 0, 1, 3};

static struct iu_stack stack_of(int item)
{
    struct iu_stack s;

    s.item = item;
    s.count = item == 0 ? 0 : 1;
    s.damage = 0;
    return s;
}

/* The Material a block's entry names. */
static const char *mat_name(int id)
{
    return MATERIALS[BLOCKS[id & 4095].material].name;
}

static int mat_is(int id, const char *name)
{
    const char *n = mat_name(id);

    return n != NULL && strcmp(n, name) == 0;
}

/* ItemStack.isItemStackDamageable. */
static int damageable(int item)
{
    return item > 0 && item < 4096 && ITEMS[item].max_damage > 0;
}

/* ItemStack.canEditBlocks -> Item.canItemEditBlocks, true for every item. */
static int stack_can_edit_blocks(const struct iu_stack *held)
{
    (void)held;
    return 1;
}

/* EntityPlayer.canPlayerEdit(x, y, z, side, stack). */
static int can_player_edit(const struct iu_player *p, const struct iu_stack *held)
{
    return p->allow_edit ? 1 : stack_can_edit_blocks(held);
}

/* Item.getMovingObjectPositionFromPlayer: the 5-block look ray, yOffset at the
 * eye. The start Vec3 and its end are the same double arithmetic the Java
 * body does, through MathHelper's tables. */
static int look_ray(struct world *w, const struct iu_player *p, int stop, struct rt_mop *out)
{
    double x = p->pos_x;
    double y = p->pos_y + 1.62 - p->y_offset;
    double z = p->pos_z;
    float var14 = mh_cos(-p->yaw * 0.017453292F - (float)M_PI);
    float var15 = mh_sin(-p->yaw * 0.017453292F - (float)M_PI);
    float var16 = -mh_cos(-p->pitch * 0.017453292F);
    float var17 = mh_sin(-p->pitch * 0.017453292F);
    float var18 = var15 * var16;
    float var20 = var14 * var16;
    double var21 = 5.0;

    return raytrace_blocks(w, x, y, z, x + (double)var18 * var21, y + (double)var17 * var21,
                           z + (double)var20 * var21, stop, !stop, 0, out);
}

int itemuse_look_block(struct world *w, const struct iu_player *p, int *x, int *y, int *z)
{
    struct rt_mop m;
    if (!look_ray(w, p, 0, &m)) return 0;
    *x = m.x;
    *y = m.y;
    *z = m.z;
    return 1;
}

/* World.func_147480_a(x, y, z, true): the break with its drops. */
static void break_with_drops(struct world *w, jrand *wr, int x, int y, int z)
{
    int id = world_get_block(w, x, y, z) & 4095;

    if (mat_is(id, "air")) return;

    int meta = world_get_meta(w, x, y, z);

    /* its 2001 (the hook answers the server's worlds only) */
    env_aux_sfx(w, 2001, x, y, z, id + (meta << 12));
    /* Block.dropBlockAsItemWithChance draws only when !world.isClient */
    if (!(live_det && live_role == DET_CLIENT)) randomtick_drop(w, x, y, z, id, meta);
    world_set_block(w, x, y, z, 0, 0, 3);
}

/* ItemBucket.tryPlaceContainedLiquid. is_full is the bucket's block (0 for the
 * empty bucket, which never reaches here). */
static int try_place_contained_liquid(struct world *w, int is_full, int x, int y, int z, jrand *wr)
{
    if (is_full == 0) return 0;

    int id = world_get_block(w, x, y, z) & 4095;
    int var6 = mat_is(id, "solid") ? 0 : (MATERIALS[BLOCKS[id & 4095].material].is_solid ? 0 : 1);

    if (id != 0 && !var6) return 0;

    if (w->dim == -1 && is_full == IU_FLOWING_WATER)
    {
        /* random.fizz: two World.rand floats; then the eight largesmoke
         * particles, x, y and z each a Math.random on the side that runs
         * the use (the client's prediction draws the client's) */
        (void)jr_float(wr);
        (void)jr_float(wr);
        for (int i = 0; live_det && i < 8 * 3; ++i) (void)det_math_random_role(live_det, live_role);
    }
    else
    {
        if (var6 && !MATERIALS[BLOCKS[id & 4095].material].is_liquid) break_with_drops(w, wr, x, y, z);

        world_set_block(w, x, y, z, is_full, 0, 3);
    }

    return 1;
}

/* ItemBucket.func_150910_a: one bucket out of the stack, the full one back in.
 * replaced is 1 when Java returns a different stack object (the caller's
 * identity test in tryUseItem). A stack that stays non-empty hands the full
 * bucket to inventory.addItemStackToInventory (dropPlayerItemWithRandomChoice
 * when it does not fit): *give, which the caller stores or drops. */
static int fill_result(const struct iu_player *p, struct iu_stack *held, int filled, struct iu_stack *ret,
                       int *replaced, struct iu_stack *give)
{
    if (p->creative)
    {
        *ret = *held;
        *replaced = 0;
        return 0;
    }

    if (--held->count <= 0)
    {
        *ret = stack_of(filled);
        *replaced = 1;
        return 1;
    }

    *give = stack_of(filled);
    *ret = *held;
    *replaced = 0;
    return 0;
}

/* ItemBucket.onItemRightClick. */
static int bucket_rclick(struct world *w, const struct iu_player *p, int is_full, struct iu_stack *held,
                         jrand *wr, struct iu_stack *ret, int *replaced, struct iu_stack *give)
{
    int var4 = is_full == 0; /* this.isFull == Blocks.air */
    struct rt_mop mop;

    if (!look_ray(w, p, var4, &mop))
    {
        *ret = *held;
        *replaced = 0;
        return 0;
    }

    /* World.canMineBlock is true in 1.7.10 */

    int bx = mop.x, by = mop.y, bz = mop.z;

    if (var4)
    {
        if (!can_player_edit(p, held))
        {
            *ret = *held;
            *replaced = 0;
            return 0;
        }

        int id = world_get_block(w, bx, by, bz) & 4095;
        int meta = world_get_meta(w, bx, by, bz);

        if (mat_is(id, "water") && meta == 0)
        {
            world_set_block(w, bx, by, bz, 0, 0, 3);
            return fill_result(p, held, IT_WATER_BUCKET, ret, replaced, give);
        }

        if (mat_is(id, "lava") && meta == 0)
        {
            world_set_block(w, bx, by, bz, 0, 0, 3);
            return fill_result(p, held, IT_LAVA_BUCKET, ret, replaced, give);
        }
    }
    else
    {
        if (is_full == 0)
        {
            *ret = stack_of(IT_BUCKET);
            *replaced = 1;
            return 1;
        }

        if (mop.type == 0) --by;
        if (mop.type == 1) ++by;
        if (mop.type == 2) --bz;
        if (mop.type == 3) ++bz;
        if (mop.type == 4) --bx;
        if (mop.type == 5) ++bx;

        if (!can_player_edit(p, held))
        {
            *ret = *held;
            *replaced = 0;
            return 0;
        }

        if (try_place_contained_liquid(w, is_full, bx, by, bz, wr) && !p->creative)
        {
            *ret = stack_of(IT_BUCKET);
            *replaced = 1;
            return 1;
        }
    }

    *ret = *held;
    *replaced = 0;
    return 0;
}

/* ItemLilyPad.onItemRightClick. */
static int lily_rclick(struct world *w, const struct iu_player *p, struct iu_stack *held, struct iu_stack *ret,
                       int *replaced)
{
    struct rt_mop mop;

    *replaced = 0;

    if (!look_ray(w, p, 1, &mop))
    {
        *ret = *held;
        return 0;
    }

    int bx = mop.x, by = mop.y, bz = mop.z;
    int id = world_get_block(w, bx, by, bz) & 4095;

    if (mat_is(id, "water") && world_get_meta(w, bx, by, bz) == 0 && world_get_block(w, bx, by + 1, bz) == 0)
    {
        world_set_block(w, bx, by + 1, bz, IU_WATERLILY, 0, 3);

        if (!p->creative) --held->count;
    }

    *ret = *held;
    return 0;
}

/* ItemGlassBottle.onItemRightClick. */
static int bottle_rclick(struct world *w, const struct iu_player *p, struct iu_stack *held, struct iu_stack *ret,
                         int *replaced, struct iu_stack *give)
{
    struct rt_mop mop;

    *replaced = 0;

    if (!look_ray(w, p, 1, &mop))
    {
        *ret = *held;
        return 0;
    }

    int id = world_get_block(w, mop.x, mop.y, mop.z) & 4095;

    if (mat_is(id, "water"))
    {
        if (--held->count <= 0)
        {
            *ret = stack_of(IT_POTION);
            *replaced = 1;
            return 1;
        }

        /* inventory.addItemStackToInventory, else dropPlayerItemWithRandomChoice:
         * the caller's */
        *give = stack_of(IT_POTION);
    }

    *ret = *held;
    return 0;
}

/* ItemDye.func_150919_a. */
static int bonemeal(struct world *w, struct iu_stack *held, int x, int y, int z, jrand *wr)
{
    int id = world_get_block(w, x, y, z) & 4095;

    if (!randomtick_is_igrowable(id)) return 0;

    if (!randomtick_bonemeal_can(w, id, x, y, z)) return 0;

    /* ItemDye.func_150919_a returns true on WorldClient but leaves the
     * stack and world growth to the server. */
    if (live_det && live_role == DET_CLIENT) return 1;

    if (randomtick_bonemeal_chance(w, id, wr, x, y, z)) randomtick_bonemeal_grow(w, id, wr, x, y, z);

    --held->count;
    return 1;
}

/* BlockCocoa.onBlockPlaced. */
static int cocoa_on_block_placed(int side)
{
    if (side == 0 || side == 1) side = 2;

    return ROTATE_OPPOSITE[FACING_TO_DIRECTION[side]];
}

/* ItemDye.onItemUse. */
static int dye_use(struct world *w, const struct iu_player *p, struct iu_stack *held, int x, int y, int z,
                   int side, float hx, float hy, float hz, jrand *wr)
{
    (void)hy;

    if (!can_player_edit(p, held)) return 0;

    if (held->damage == 15)
    {
        if (bonemeal(w, held, x, y, z, wr))
        {
            /* the server's World.playAuxSFX(2005, ...): the client's S28
             * builds the happy villager particles */
            if (!(live_det && live_role == DET_CLIENT)) env_aux_sfx(w, 2005, x, y, z, 0);
            return 1;
        }
    }
    else if (held->damage == 3)
    {
        int id = world_get_block(w, x, y, z) & 4095;
        int meta = world_get_meta(w, x, y, z);

        if (id == IU_LOG && (meta & 3) == 3)
        {
            if (side == 0 || side == 1) return 0;

            if (side == 2) --z;
            if (side == 3) ++z;
            if (side == 4) --x;
            if (side == 5) ++x;

            if (world_get_block(w, x, y, z) == 0)
            {
                int var13 = cocoa_on_block_placed(side);

                world_set_block(w, x, y, z, IU_COCOA, var13, 2);

                if (!p->creative) --held->count;
            }

            return 1;
        }
    }

    return 0;
}

/* ItemFlintAndSteel.onItemUse: the fire on an air cell, then true; its
 * caller runs the stack's damageItem(1, player) (survival.c: the Unbreaking
 * rolls, the break draws and stat are the player's, like the hoe's). */
static int flint_use(struct world *w, const struct iu_player *p, struct iu_stack *held, int x, int y, int z,
                     int side, jrand *wr)
{
    (void)wr;

    if (side == 0) --y;
    if (side == 1) ++y;
    if (side == 2) --z;
    if (side == 3) ++z;
    if (side == 4) --x;
    if (side == 5) ++x;

    if (!can_player_edit(p, held)) return 0;

    if (mat_is(world_get_block(w, x, y, z), "air"))
    {
        item_sound_random();
        world_set_block(w, x, y, z, IU_FIRE, 0, 3);
    }

    return 1;
}

/* ItemFireball.onItemUse. */
static int fireball_use(struct world *w, const struct iu_player *p, struct iu_stack *held, int x, int y, int z,
                        int side, jrand *wr)
{
    (void)wr;

    /* world.isClient: the client's own call returns true untouched (no
     * fire, no decrement); the server's fire and slot update reach it later */
    if (live_det && live_role == DET_CLIENT) return 1;

    if (side == 0) --y;
    if (side == 1) ++y;
    if (side == 2) --z;
    if (side == 3) ++z;
    if (side == 4) --x;
    if (side == 5) ++x;

    if (!can_player_edit(p, held)) return 0;

    if (mat_is(world_get_block(w, x, y, z), "air"))
    {
        item_sound_random();
        world_set_block(w, x, y, z, IU_FIRE, 0, 3);
    }

    if (!p->creative) --held->count;

    return 1;
}

/* ItemHoe.onItemUse: grass or dirt with air above, clicked on any side but
 * the bottom, becomes farmland. The till sound draws nothing. The client's
 * own call returns true there untouched; the server writes the farmland and
 * returns true, and its caller runs the stack's damageItem(1, player)
 * (survival.c: the break draws and stat are the player's). */
static int hoe_use(struct world *w, const struct iu_player *p, struct iu_stack *held, int x, int y, int z,
                   int side)
{
    if (!can_player_edit(p, held)) return 0;

    int id = world_get_block(w, x, y, z) & 4095;

    if (side == 0 || !mat_is(world_get_block(w, x, y + 1, z) & 4095, "air") || (id != IU_GRASS && id != IU_DIRT))
        return 0;

    if (live_det && live_role == DET_CLIENT) return 1;

    world_set_block(w, x, y, z, IU_FARMLAND, 0, 3);
    return 1;
}

/* BlockEndPortalFrame.func_150020_b. */
static int frame_has_eye(struct world *w, int x, int y, int z)
{
    return (world_get_meta(w, x, y, z) & 4) != 0;
}

/* ItemEnderEye.onItemUse. */
static int eye_use(struct world *w, const struct iu_player *p, struct iu_stack *held, int x, int y, int z,
                   jrand *wr)
{
    (void)wr;

    int id = world_get_block(w, x, y, z) & 4095;
    int meta = world_get_meta(w, x, y, z);

    if (!can_player_edit(p, held) || id != IU_END_PORTAL_FRAME || (meta & 4) != 0) return 0;

    /* the client's own call (a live client role) returns true untouched; the
     * server's frame write and slot update reach it later */
    if (live_det && live_role == DET_CLIENT) return 1;

    world_set_meta(w, x, y, z, meta + 4, 2);
    /* World.func_147453_f (comparator.c) */
    comparator_notify(w, x, y, z, IU_END_PORTAL_FRAME);
    if (!p->creative) --held->count;

    /* the sixteen smoke particles: the server's own branch draws their
     * positions from Item.itemRand (two floats each); the particles
     * themselves go nowhere on a server */
    if (live_det)
    {
        const char *name = "./net/minecraft/item/Item.java:itemRand";
        det_split *stream = det_split_find(live_det, name);
        if (!stream) stream = det_split_random(live_det, name);
        for (int i = 0; i < 32; ++i) (void)det_split_float_role(live_det, stream, live_role);
    }

    int var13 = meta & 3;
    int var26 = 0, var15 = 0, var27 = 0, var17 = 1;
    int var28 = ROTATE_RIGHT[var13];

    for (int var19 = -2; var19 <= 2; ++var19)
    {
        int var29 = x + OFFSET_X[var28] * var19;
        int var21 = z + OFFSET_Z[var28] * var19;

        if ((world_get_block(w, var29, y, var21) & 4095) == IU_END_PORTAL_FRAME)
        {
            if (!frame_has_eye(w, var29, y, var21))
            {
                var17 = 0;
                break;
            }

            var15 = var19;

            if (!var27)
            {
                var26 = var19;
                var27 = 1;
            }
        }
    }

    if (var17 && var15 == var26 + 2)
    {
        for (int var19 = var26; var19 <= var15; ++var19)
        {
            int var29 = x + OFFSET_X[var28] * var19 + OFFSET_X[var13] * 4;
            int var21 = z + OFFSET_Z[var28] * var19 + OFFSET_Z[var13] * 4;

            if ((world_get_block(w, var29, y, var21) & 4095) != IU_END_PORTAL_FRAME ||
                !frame_has_eye(w, var29, y, var21))
            {
                var17 = 0;
                break;
            }
        }

        for (int var19 = var26 - 1; var19 <= var15 + 1; var19 += 4)
        {
            for (int var29 = 1; var29 <= 3; ++var29)
            {
                int var21 = x + OFFSET_X[var28] * var19 + OFFSET_X[var13] * var29;
                int var30 = z + OFFSET_Z[var28] * var19 + OFFSET_Z[var13] * var29;

                if ((world_get_block(w, var21, y, var30) & 4095) != IU_END_PORTAL_FRAME ||
                    !frame_has_eye(w, var21, y, var30))
                {
                    var17 = 0;
                    break;
                }
            }
        }

        if (var17)
        {
            for (int var19 = var26; var19 <= var15; ++var19)
            {
                for (int var29 = 1; var29 <= 3; ++var29)
                {
                    int var21 = x + OFFSET_X[var28] * var19 + OFFSET_X[var13] * var29;
                    int var30 = z + OFFSET_Z[var28] * var19 + OFFSET_Z[var13] * var29;

                    world_set_block(w, var21, y, var30, IU_END_PORTAL, 0, 2);
                }
            }
        }
    }

    return 1;
}

/* Item.onItemUse for the items this file carries; every other item is the base
 * body, false. */
static int on_item_use(struct world *w, const struct iu_player *p, struct iu_stack *held, int x, int y, int z,
                       int side, float hx, float hy, float hz, jrand *wr)
{
    switch (held->item)
    {
    case IT_DYE:
        return dye_use(w, p, held, x, y, z, side, hx, hy, hz, wr);

    case IT_FLINT:
        return flint_use(w, p, held, x, y, z, side, wr);

    case IT_FIRE_CHARGE:
        return fireball_use(w, p, held, x, y, z, side, wr);

    case IT_WOODEN_HOE:
    case IT_STONE_HOE:
    case IT_IRON_HOE:
    case IT_DIAMOND_HOE:
    case IT_GOLDEN_HOE:
        return hoe_use(w, p, held, x, y, z, side);

    case IT_ENDER_EYE:
        return eye_use(w, p, held, x, y, z, wr);

    case IT_RECORD_13:
    case IT_RECORD_CAT:
    {
        /* ItemRecord.onItemUse: the insert into a meta-0 jukebox. The tile
         * entity's stack copy, func_149926_b's meta write (flag 2) and the
         * --stackSize tail (no null-at-zero: the count 0 stack stays). */
        if ((world_get_block(w, x, y, z) & 4095) != IU_JUKEBOX ||
            world_get_meta(w, x, y, z) != 0)
            return 0;

        /* the client's own call (a live client role) returns true untouched;
         * the server's insert reaches it later */
        if (live_det && live_role == DET_CLIENT) return 1;

        struct tile_entity *te = world_tile_entity(w, x, y, z);

        if (te == NULL) return 0;

        te->u.jukebox.item = held->item;
        te->u.jukebox.damage = held->damage;
        te->u.jukebox.count = held->count;
        /* TileEntityJukebox.func_145857_a's markDirty: World.func_147453_f */
        comparator_notify(w, x, y, z, IU_JUKEBOX);
        world_set_meta(w, x, y, z, 1, 2);
        --held->count;

        return 1;
    }

    default:
        return 0;
    }
}

/* Item.onItemRightClick. *replaced is set when the Java body returns a stack
 * other than the one it was handed (a new ItemStack), which is what
 * tryUseItem's identity test distinguishes. */
static int on_item_right_click(struct world *w, const struct iu_player *p, struct iu_stack *held, jrand *wr,
                               struct iu_stack *ret, int *replaced, struct iu_stack *give)
{
    struct iu_stack before = *held;

    switch (held->item)
    {
    case IT_BUCKET:
        return bucket_rclick(w, p, 0, held, wr, ret, replaced, give);

    case IT_WATER_BUCKET:
        return bucket_rclick(w, p, IU_FLOWING_WATER, held, wr, ret, replaced, give);

    case IT_LAVA_BUCKET:
        return bucket_rclick(w, p, IU_FLOWING_LAVA, held, wr, ret, replaced, give);

    case IT_WATERLILY:
        return lily_rclick(w, p, held, ret, replaced);

    case IT_GLASS_BOTTLE:
        return bottle_rclick(w, p, held, ret, replaced, give);

    default:
        *ret = before;
        *replaced = 0;
        return 0;
    }
}

/* The World.rand stream the block callbacks' own draws (a fire's re-schedule,
 * a lava fizz) must come from, for the length of one use. */
struct blockcb_env *iu_install_rand(jrand *wr, jrand **prev_rand)
{
    *prev_rand = nw_env->blockcb.env.world_rand;
    nw_env->blockcb.env.world_rand = wr;
    return &nw_env->blockcb.env;
}

void iu_restore_rand(jrand *prev)
{
    nw_env->blockcb.env.world_rand = prev;
}

int itemuse_try_use_item(struct world *w, const struct iu_player *p, struct iu_stack *held, jrand *wr,
                         struct iu_stack *give)
{
    struct iu_stack give_none;

    if (give == NULL) give = &give_none;
    give->item = give->count = give->damage = 0;

    int var4 = held->count;
    int var5 = held->damage;
    struct iu_stack ret;
    int replaced;
    jrand *prev;

    iu_install_rand(wr, &prev);

    on_item_right_click(w, p, held, wr, &ret, &replaced, give);

    /* A different object came back (replaced), or the same one was mutated. */
    if (!replaced && ret.item == held->item && ret.count == var4 && ret.damage == var5)
    {
        iu_restore_rand(prev);
        return 0;
    }

    *held = ret;

    if (p->creative)
    {
        held->count = var4;

        if (damageable(held->item)) held->damage = var5;
    }

    if (held->count == 0)
    {
        held->item = 0;
        held->count = 0;
        held->damage = 0;
    }

    iu_restore_rand(prev);
    return 1;
}

int itemuse_activate_block_or_use_item(struct world *w, const struct iu_player *p, struct iu_stack *held,
                                       int x, int y, int z, int side, float hx, float hy, float hz,
                                       jrand *wr)
{
    /* Block.onBlockActivated: false for every block a probe scene holds */
    if (held->item == 0) return 0;

    jrand *prev;
    iu_install_rand(wr, &prev);

    if (p->creative)
    {
        int var11 = held->damage;
        int var12 = held->count;
        int var13 = on_item_use(w, p, held, x, y, z, side, hx, hy, hz, wr);

        held->damage = var11;
        held->count = var12;
        iu_restore_rand(prev);
        return var13;
    }

    int var13 = on_item_use(w, p, held, x, y, z, side, hx, hy, hz, wr);

    iu_restore_rand(prev);
    return var13;
}
