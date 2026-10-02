/* EntityPig, EntityCow, EntityMooshroom, EntityChicken and EntitySheep, plus the
 * EntityAnimal and EntityAgeable halves of the living tick the farm animals own
 * (love decay, growth, egg laying, wool regrowth, the drops). Ported from
 * oracle/src/entity/passive/Entity{Pig,Cow,Mooshroom,Chicken,Sheep}.java and
 * EntityAnimal.java.
 *
 * The sheep's child colour goes through CraftingManager.findMatchingRecipe with a
 * two-slot inventory of dyes, which in 1.7.10 is RecipesDyes' nine two-ingredient
 * shapeless recipes: the unordered pair to result damage table below, else
 * World.rand.nextBoolean(). */
#include "nbtw.h"
#include "animals.h"
#include "env.h"

#include <math.h>
#include <string.h>

#include "ai.h"
#include "blocks.h"
#include "det.h"
#include "items.h"
#include "jmath.h"
#include "living.h"
#include "smath.h"
#include "trace.h"
#include "world.h"

/* EntityBat.fall, forward-declared below its use in animal_construct. */
static void bat_fall(struct living *l, float distance);

enum { ITEM_LEATHER = 334, ITEM_PORKCHOP = 319, ITEM_COOKED_PORKCHOP = 320, ITEM_SADDLE = 329,
       ITEM_BEEF = 363, ITEM_COOKED_BEEF = 364, ITEM_CHICKEN = 365, ITEM_COOKED_CHICKEN = 366,
       ITEM_FEATHER = 288, ITEM_EGG = 344, ITEM_WOOL = 35, ITEM_DYE = 351,
       ITEM_RED_FLOWER = 38, ITEM_IRON_INGOT = 265 };

/* EntityLiving.applyEntityAttributes: followRange 16, then the kind's bases. */
void animal_attributes(struct living *l)
{
    attrs_set_base(&l->attrs.a[ATTR_FOLLOW_RANGE], 16.0);

    if (l->kind == AK_SQUID)
    {
        /* EntityLivingBase's !isAIEnabled branch sets the movement speed's
         * base to 0.1 (EntitySquid does not override isAIEnabled) */
        attrs_set_base(&l->attrs.a[ATTR_MAX_HEALTH], 10.0);
        attrs_set_base(&l->attrs.a[ATTR_MOVEMENT_SPEED], 0.10000000149011612);
        return;
    }

    if (l->kind == AK_BAT)
    {
        /* EntityBat.applyEntityAttributes sets maxHealth only; the movement
         * speed stays SharedMonsterAttributes' 0.7 default (the bat is
         * AI-enabled, so EntityLivingBase's !isAIEnabled 0.1 does not apply) */
        attrs_set_base(&l->attrs.a[ATTR_MAX_HEALTH], 6.0);
        return;
    }

    switch (l->kind)
    {
        case AK_PIG:
            attrs_set_base(&l->attrs.a[ATTR_MAX_HEALTH], 10.0);
            attrs_set_base(&l->attrs.a[ATTR_MOVEMENT_SPEED], 0.25);
            break;

        case AK_COW:
        case AK_MOOSHROOM:
            attrs_set_base(&l->attrs.a[ATTR_MAX_HEALTH], 10.0);
            attrs_set_base(&l->attrs.a[ATTR_MOVEMENT_SPEED], 0.20000000298023224);
            break;

        case AK_CHICKEN:
            attrs_set_base(&l->attrs.a[ATTR_MAX_HEALTH], 4.0);
            attrs_set_base(&l->attrs.a[ATTR_MOVEMENT_SPEED], 0.25);
            break;

        case AK_SHEEP:
            attrs_set_base(&l->attrs.a[ATTR_MAX_HEALTH], 8.0);
            attrs_set_base(&l->attrs.a[ATTR_MOVEMENT_SPEED], 0.23000000417232513);
            break;

        default:
            break;
    }
}

/* The kind constructor after super(): setSize, the navigator flag, the tasks. */
/* EntityChicken.fall: no fall damage, no sound, nothing at all. */
static void chicken_fall(struct living *l, float distance)
{
    (void)l;
    (void)distance;
}

void animal_construct(struct living *l, det_state *det)
{
    switch (l->kind)
    {
        case AK_PIG:
            living_set_base_size(l, 0.9F, 0.9F);
            l->nav.avoids_water = 1;
            break;

        case AK_COW:
            living_set_base_size(l, 0.9F, 1.3F);
            l->nav.avoids_water = 1;
            break;

        case AK_MOOSHROOM:
            living_set_base_size(l, 0.9F, 1.3F);
            l->nav.avoids_water = 1;
            break;

        case AK_CHICKEN:
            living_set_base_size(l, 0.3F, 0.7F);
            /* no setAvoidsWater on EntityChicken */
            l->kind_fall = chicken_fall;
            break;

        case AK_SHEEP:
            living_set_base_size(l, 0.9F, 1.3F);
            l->nav.avoids_water = 1;
            break;

        case AK_SQUID:
            living_set_base_size(l, 0.95F, 0.95F);
            /* EntitySquid's constructor after setSize */
            l->squid_rotation_velocity = 1.0F / (det_rng_float(&l->rand) + 1.0F) * 0.2F;
            /* EntitySquid.canTriggerWalking is false: the step distance never
             * accumulates and the in-water swim sound never fires */
            l->e.can_trigger_walking = 0;
            break;

        case AK_BAT:
            living_set_base_size(l, 0.5F, 0.9F);
            /* EntityBat's constructor: setIsBatHanging(true) */
            l->data_watcher_16 |= 1;
            /* EntityBat.canTriggerWalking is false */
            l->e.can_trigger_walking = 0;
            l->kind_fall = bat_fall;
            break;

        default:
            break;
    }

    if (l->kind == AK_CHICKEN)
    {
        /* EntityChicken's constructor after setSize */
        l->time_until_next_egg = det_rng_int_n(&l->rand, 6000) + 6000;
    }

    ai_setup_kind(l, det);
}

/* EntityLivingBase.getEyeHeight is height * 0.85F; the sheep's rendering helpers
 * and the chicken's fields are not part of any recorded state. */

/* ----------------------------------------------------------- eating grass */

void animal_eat_grass_bonus(struct living *l)
{
    if (l->kind != AK_SHEEP) return;

    /* EntitySheep.eatGrassBonus */
    l->data_watcher_16 &= ~16;   /* setSheared(false) */

    if (l->growing_age < 0)
    {
        int v = l->growing_age + 60 * 20;   /* addGrowth(60) */

        if (v > 0) v = 0;

        living_set_growing_age(l, v);
    }
}

/* EntitySheep.onSpawnWithEgg: the fleece colour, from World.rand. */
void animal_on_spawn_with_egg(struct living *l, det_state *det)
{
    (void)det;

    if (l->kind != AK_SHEEP) return;

    int var1 = det_rng_int_n(&l->an->iew.world_rand, 100);
    int colour;

    if (var1 < 5) colour = 15;
    else if (var1 < 10) colour = 7;
    else if (var1 < 15) colour = 8;
    else if (var1 < 18) colour = 12;
    else colour = det_rng_int_n(&l->an->iew.world_rand, 500) == 0 ? 6 : 0;

    /* setFleeceColor keeps the sheared bit */
    l->data_watcher_16 = (l->data_watcher_16 & 240) | (colour & 15);
}

/* ---------------------------------------------------------- EntityAnimal */

/* EntityBat.fall and EntityBat.updateFallState, both empty. */
static void bat_fall(struct living *l, float distance)
{
    (void)l;
    (void)distance;
}

void animal_on_living_update(struct living *l, det_state *det)
{
    /* EntityLivingBase.onLivingUpdate */
    living_default_on_living_update(l, det);

    /* EntityLiving.onLivingUpdate's looting block: canPickUpLoot is false */

    /* EntityAgeable.onLivingUpdate */
    int v1 = l->growing_age;

    if (v1 < 0)
    {
        ++v1;
        living_set_growing_age(l, v1);
    }
    else if (v1 > 0)
    {
        --v1;
        living_set_growing_age(l, v1);
    }

    /* EntityAnimal.onLivingUpdate */
    if (l->growing_age != 0) l->in_love = 0;

    if (l->in_love > 0)
    {
        --l->in_love;

        if (l->in_love % 10 == 0)
        {
            (void)det_rng_gaussian(&l->rand);
            (void)det_rng_gaussian(&l->rand);
            (void)det_rng_gaussian(&l->rand);
            (void)det_rng_float(&l->rand);
            (void)det_rng_float(&l->rand);
            (void)det_rng_float(&l->rand);
        }
    }
    else
    {
        l->breeding = 0;
    }

    /* the kind's own onLivingUpdate */
    if (l->kind == AK_CHICKEN)
    {
        /* EntityChicken.onLivingUpdate after super */
        l->chicken_field_70888_h = l->chicken_field_70886_e;
        l->chicken_field_70884_g = l->chicken_dest_pos;
        l->chicken_dest_pos = (float)((double)l->chicken_dest_pos + (double)(l->e.on_ground ? -1 : 4) * 0.3);

        if (l->chicken_dest_pos < 0.0F) l->chicken_dest_pos = 0.0F;
        if (l->chicken_dest_pos > 1.0F) l->chicken_dest_pos = 1.0F;

        if (!l->e.on_ground && l->chicken_field_70889_i < 1.0F) l->chicken_field_70889_i = 1.0F;

        l->chicken_field_70889_i = (float)((double)l->chicken_field_70889_i * 0.9);

        if (!l->e.on_ground && l->e.motion_y < 0.0) l->e.motion_y *= 0.6;

        l->chicken_field_70886_e += l->chicken_field_70889_i * 2.0F;

        trace("egg", "iii", l->entity_id, l->time_until_next_egg, l->growing_age < 0 ? 1 : 0);

        if (l->growing_age >= 0 && !l->is_chicken_jockey)
        {
            --l->time_until_next_egg;

            if (l->time_until_next_egg <= 0)
            {
                /* playSound("mob.chicken.plop", 1.0F, (rand.nextFloat() - rand.nextFloat())
                 * * 0.2F + 1.0F) draws the two pitch floats */
                (void)det_rng_float(&l->rand);
                (void)det_rng_float(&l->rand);
                an_spawn_item(l->an, l->an->n, l->e.pos_x, l->e.pos_y, l->e.pos_z, ITEM_EGG, 0, 1, 10);
                l->time_until_next_egg = det_rng_int_n(&l->rand, 6000) + 6000;
            }
        }
    }
    else if (l->kind == AK_SHEEP)
    {
        /* the client half of EntitySheep.onLivingUpdate is dead on the server */
    }
}

/* ------------------------------------------------------------- the drops */

static void drop_item(struct living *l, int item, int count)
{
    /* Entity.entityDropItem(new ItemStack(item, count, 0), 0.0F) */
    an_spawn_item(l->an, l->an->n, l->e.pos_x, l->e.pos_y + 0.0, l->e.pos_z, item, 0, count, 10);
}

/* The same drop with a stack damage: EntitySheep.dropFewItems' wool carries
 * getFleeceColor(). */
static void drop_item_damage(struct living *l, int item, int count, int damage)
{
    an_spawn_item(l->an, l->an->n, l->e.pos_x, l->e.pos_y + 0.0, l->e.pos_z, item, damage, count, 10);
}

void animal_drop_few_items(struct living *l, int hit_by_player, int looting, det_state *det)
{
    (void)hit_by_player;
    (void)det;

    switch (l->kind)
    {
        case AK_PIG:
        {
            int var3 = det_rng_int_n(&l->rand, 3) + 1;

            if (looting > 0) var3 += det_rng_int_n(&l->rand, looting + 1); else (void)det_rng_int_n(&l->rand, 1);

            for (int i = 0; i < var3; ++i)
                drop_item(l, living_is_burning(l) ? ITEM_COOKED_PORKCHOP : ITEM_PORKCHOP, 1);

            if (l->data_watcher_16 & 1) drop_item(l, ITEM_SADDLE, 1);
            break;
        }

        case AK_COW:
        case AK_MOOSHROOM:
        {
            int var3 = det_rng_int_n(&l->rand, 3);

            if (looting > 0) var3 += det_rng_int_n(&l->rand, looting + 1); else (void)det_rng_int_n(&l->rand, 1);

            for (int i = 0; i < var3; ++i) drop_item(l, ITEM_LEATHER, 1);

            var3 = det_rng_int_n(&l->rand, 3) + 1;

            if (looting > 0) var3 += det_rng_int_n(&l->rand, looting + 1); else (void)det_rng_int_n(&l->rand, 1);

            for (int i = 0; i < var3; ++i)
                drop_item(l, living_is_burning(l) ? ITEM_COOKED_BEEF : ITEM_BEEF, 1);

            break;
        }

        case AK_CHICKEN:
        {
            int var3 = det_rng_int_n(&l->rand, 3);

            if (looting > 0) var3 += det_rng_int_n(&l->rand, looting + 1); else (void)det_rng_int_n(&l->rand, 1);

            for (int i = 0; i < var3; ++i) drop_item(l, ITEM_FEATHER, 1);

            drop_item(l, living_is_burning(l) ? ITEM_COOKED_CHICKEN : ITEM_CHICKEN, 1);
            break;
        }

        case AK_SHEEP:
            /* EntitySheep.dropFewItems: one wool of the fleece colour when it
             * is not sheared (dataWatcher 16 is colour | 16 when sheared) */
            if (!(l->data_watcher_16 & 16)) drop_item_damage(l, ITEM_WOOL, 1, l->data_watcher_16 & 15);
            break;

        case AK_SQUID:
        {
            /* EntitySquid.dropFewItems: 1 + nextInt(3 + looting) dye 0
             * items, each a count-1 stack */
            int var3 = det_rng_int_n(&l->rand, 3 + looting) + 1;

            for (int i = 0; i < var3; ++i) drop_item(l, ITEM_DYE, 1);

            break;
        }

        case VK_IRON_GOLEM:
        {
            /* EntityIronGolem.dropFewItems: 0-2 poppies (func_145778_a at
             * offset 0), then 3-5 iron ingots; no looting term */
            int var3 = det_rng_int_n(&l->rand, 3);

            for (int i = 0; i < var3; ++i) drop_item(l, ITEM_RED_FLOWER, 1);

            int var4 = 3 + det_rng_int_n(&l->rand, 3);

            for (int i = 0; i < var4; ++i) drop_item(l, ITEM_IRON_INGOT, 1);

            break;
        }

        default:
            break;
    }
}

/* ------------------------------------------------------------ the children */

/* RecipesDyes' two-ingredient shapeless recipes: the unordered dye pair to the
 * result damage, in the order CraftingManager.findMatchingRecipe matches. */
static int dye_recipe(int a, int b)
{
    int lo = a < b ? a : b, hi = a < b ? b : a;

    if (lo == 1 && hi == 15) return 9;
    if (lo == 1 && hi == 11) return 14;
    if (lo == 2 && hi == 15) return 10;
    if (lo == 0 && hi == 15) return 8;
    if (lo == 8 && hi == 15) return 7;
    if (lo == 4 && hi == 15) return 12;
    if (lo == 2 && hi == 4) return 6;
    if (lo == 1 && hi == 4) return 5;
    if (lo == 5 && hi == 9) return 13;

    return -1;
}

/* EntitySheep.func_90014_a: the two fleece colours through the crafting table. */
static int sheep_mix_colour(struct living *l, struct living *mate)
{
    int var3 = 15 - (l->data_watcher_16 & 15);
    int var4 = 15 - (mate->data_watcher_16 & 15);
    int recipe = dye_recipe(var3, var4);

    if (recipe >= 0) return recipe;

    return det_rng_bool(&l->an->iew.world_rand) ? var3 : var4;
}

struct living *animal_create_child(struct living *l, struct living *mate, det_state *det)
{
    int kind = l->kind;
    int colour = 0;

    if (kind == AK_SHEEP) colour = 15 - sheep_mix_colour(l, mate);

    struct living *child = an_spawn_living(l->an, kind, l->an->n, l->e.pos_x, l->e.pos_y, l->e.pos_z,
                                           l->rotation_yaw, l->rotation_pitch, 0, 0, 0, 0, 0, 0);

    if (!child) return NULL;

    if (kind == AK_SHEEP) child->data_watcher_16 = (child->data_watcher_16 & ~15) | (colour & 15);

    /* EntityAIMate.spawnBaby's order: the child's age and position after the
     * constructor, then the spawn */
    living_set_growing_age(child, -24000);
    living_set_location_and_angles(child, l->e.pos_x, l->e.pos_y, l->e.pos_z, 0.0F, 0.0F);
    return child;
}

/* --------------------------------------------------------------- the NBT */

/* EntityBat.writeEntityToNBT's extra: BatFlags. */
void bat_write_kind_nbt(struct living *l, struct nbtw *w)
{
    nbtw_byte(w, "BatFlags", l->data_watcher_16 & 1);
}

void animal_write_kind_nbt(struct living *l, struct nbtw *w)
{
    switch (l->kind)
    {
        case AK_PIG:
            nbtw_byte(w, "Saddle", (l->data_watcher_16 & 1) != 0 ? 1 : 0);
            break;

        case AK_SHEEP:
            nbtw_byte(w, "Sheared", (l->data_watcher_16 & 16) != 0 ? 1 : 0);
            nbtw_byte(w, "Color", l->data_watcher_16 & 15);
            break;

        case AK_CHICKEN:
            nbtw_byte(w, "IsChickenJockey", l->is_chicken_jockey ? 1 : 0);
            break;

        case AK_BAT:
            bat_write_kind_nbt(l, w);
            break;

        default:
            break;
    }
}



/* Block.isNormalCube: the material is opaque, the block renders as a normal
 * cube and it provides no power. The registry's precomputed flags carry the
 * first two; the tables' provide-power flag sits in the same entry. */
static int block_normal_cube_at(struct world *w, int x, int y, int z)
{
    const struct block_def *b = &BLOCKS[world_get_block(w, x, y, z) & 4095];

    return MATERIALS[b->material].is_opaque && b->normal_block && !b->provides_power;
}

static double squidbat_abs(double v)
{
    return v < 0.0 ? -v : v;
}

/* MathHelper.wrapAngleTo180_float. */
static float squidbat_wrap_180(float a)
{
    /* fmod is exact: an angle inside (-360, 360) is its own remainder */
    if (!(a > -360.0F && a < 360.0F)) a = (float)fmod((double)a, 360.0);

    if (a >= 180.0F) a -= 360.0F;
    if (a < -180.0F) a += 360.0F;

    return a;
}

/* Math.signum for doubles. */
static double squidbat_signum(double v)
{
    return v > 0.0 ? 1.0 : (v < 0.0 ? -1.0 : v);
}

/* ChunkCoordinates.getDistanceSquared. */
static float bat_spawn_distance_squared(struct living *l)
{
    float dx = (float)(l->bat_spawn_x - (int)l->e.pos_x);
    float dy = (float)(l->bat_spawn_y - (int)l->e.pos_y);
    float dz = (float)(l->bat_spawn_z - (int)l->e.pos_z);

    return dx * dx + dy * dy + dz * dz;
}

/* ------------------------------------------------------------ EntitySquid */

/* EntitySquid.isInWater: World.handleMaterialAcceleration over the box
 * expanded 0.6 down, which both checks the water and pushes the motion with
 * the 0.014 flow when the squid is pushed by water. */
int squid_is_in_water(struct living *l)
{
    struct aabb box = aabb_expand(l->e.bounding_box, 0.0, -0.6000000238418579, 0.0);
    return ie_water_accelerate_box(l->world, box, &l->e);
}

/* EntitySquid.moveEntityWithHeading. */
void squid_move_entity_with_heading(struct living *l, float strafe, float forward, det_state *det)
{
    (void)strafe;
    (void)forward;
    (void)det;
    living_move(l, l->e.motion_x, l->e.motion_y, l->e.motion_z);
}

/* EntitySquid.updateEntityActionState, the old-AI branch's body. */
void squid_update_entity_action_state(struct living *l)
{
    ++l->entity_age;

    if (l->entity_age > 100)
    {
        l->squid_random_motion_vec_x = l->squid_random_motion_vec_y = l->squid_random_motion_vec_z = 0.0F;
    }
    else if (det_rng_int_n(&l->rand, 50) == 0 || !l->e.in_water ||
             (l->squid_random_motion_vec_x == 0.0F && l->squid_random_motion_vec_y == 0.0F &&
              l->squid_random_motion_vec_z == 0.0F))
    {
        float var1 = det_rng_float(&l->rand) * (float)3.14159274101257324 * 2.0F;
        l->squid_random_motion_vec_x = mh_cos(var1) * 0.2F;
        l->squid_random_motion_vec_y = -0.1F + det_rng_float(&l->rand) * 0.2F;
        l->squid_random_motion_vec_z = mh_sin(var1) * 0.2F;
    }

    living_despawn_entity(l);
}

/* EntitySquid.onLivingUpdate after super. */
void squid_on_living_update(struct living *l, det_state *det)
{
    /* EntityLivingBase.onLivingUpdate (EntityLiving's looting is empty:
     * canPickUpLoot is false; EntityWaterMob layers nothing) */
    living_on_living_update(l, det);

    l->prev_squid_pitch = l->squid_pitch;
    l->prev_squid_yaw = l->squid_yaw;
    l->prev_squid_rotation = l->squid_rotation;
    l->last_tentacle_angle = l->tentacle_angle;
    l->squid_rotation += l->squid_rotation_velocity;

    if (l->squid_rotation > ((float)3.14159274101257324 * 2.0F))
    {
        l->squid_rotation -= (float)3.14159274101257324 * 2.0F;

        if (det_rng_int_n(&l->rand, 10) == 0)
        {
            l->squid_rotation_velocity = 1.0F / (det_rng_float(&l->rand) + 1.0F) * 0.2F;
        }
    }

    if (squid_is_in_water(l))
    {
        float var1;

        if (l->squid_rotation < (float)3.14159274101257324)
        {
            var1 = l->squid_rotation / (float)3.14159274101257324;
            l->tentacle_angle = mh_sin(var1 * var1 * (float)3.14159274101257324) * (float)3.14159274101257324 * 0.25F;

            if ((double)var1 > 0.75)
            {
                l->squid_random_motion_speed = 1.0F;
                l->squid_field_70871_bB = 1.0F;
            }
            else
            {
                l->squid_field_70871_bB *= 0.8F;
            }
        }
        else
        {
            l->tentacle_angle = 0.0F;
            l->squid_random_motion_speed *= 0.9F;
            l->squid_field_70871_bB *= 0.99F;
        }

        l->e.motion_x = (double)(l->squid_random_motion_vec_x * l->squid_random_motion_speed);
        l->e.motion_y = (double)(l->squid_random_motion_vec_y * l->squid_random_motion_speed);
        l->e.motion_z = (double)(l->squid_random_motion_vec_z * l->squid_random_motion_speed);

        var1 = (float)sqrt(l->e.motion_x * l->e.motion_x + l->e.motion_z * l->e.motion_z);
        l->render_yaw_offset += (-((float)fd_atan2(l->e.motion_x, l->e.motion_z)) * 180.0F / (float)3.14159274101257324 - l->render_yaw_offset) * 0.1F;
        l->rotation_yaw = l->render_yaw_offset;
        l->squid_yaw += (float)3.14159274101257324 * l->squid_field_70871_bB * 1.5F;
        l->squid_pitch += (-((float)fd_atan2((double)var1, l->e.motion_y)) * 180.0F / (float)3.14159274101257324 - l->squid_pitch) * 0.1F;
    }
    else
    {
        l->tentacle_angle = squidbat_abs(mh_sin(l->squid_rotation)) * (float)3.14159274101257324 * 0.25F;

        l->e.motion_x = 0.0;
        l->e.motion_y -= 0.08;
        l->e.motion_y *= 0.9800000190734863;
        l->e.motion_z = 0.0;

        l->squid_pitch = (float)((double)l->squid_pitch + (double)(-90.0F - l->squid_pitch) * 0.02);
    }
}

/* -------------------------------------------------------------- EntityBat */

/* EntityBat.onUpdate after super. */
void bat_on_base_update(struct living *l)
{
    if (l->data_watcher_16 & 1)
    {
        l->e.motion_x = l->e.motion_y = l->e.motion_z = 0.0;
        /* EntityBat.onUpdate's pin uses the entity's height, not Entity.ySize */
        l->e.pos_y = (double)mh_floor(l->e.pos_y) + 1.0 - (double)l->e.height;
    }
    else
    {
        l->e.motion_y *= 0.6000000238418579;
    }
}

/* EntityBat.updateAITasks after super's body. */
void bat_update_ai_tasks(struct living *l, det_state *det)
{
    (void)det;

    if (l->data_watcher_16 & 1)
    {
        /* the block read is MathHelper.floor_double(posX/Z) with (int)posY + 1 */
        if (!block_normal_cube_at(l->world, mh_floor(l->e.pos_x), (int)l->e.pos_y + 1,
                                  mh_floor(l->e.pos_z)))
        {
            l->data_watcher_16 &= ~1;   /* setIsBatHanging(false) */
            env_aux_sfx(l->world, 1015, (int)l->e.pos_x, (int)l->e.pos_y, (int)l->e.pos_z, 0);
        }
        else
        {
            if (det_rng_int_n(&l->rand, 200) == 0)
            {
                l->rotation_yaw_head = (float)det_rng_int_n(&l->rand, 360);
            }

            if (living_closest_player_within(l, 4.0))
            {
                l->data_watcher_16 &= ~1;   /* setIsBatHanging(false) */
                env_aux_sfx(l->world, 1015, (int)l->e.pos_x, (int)l->e.pos_y, (int)l->e.pos_z, 0);
            }
        }
    }
    else
    {
        if (l->bat_has_spawn_pos &&
            ((world_get_block(l->world, l->bat_spawn_x, l->bat_spawn_y, l->bat_spawn_z) & 4095) != 0 || l->bat_spawn_y < 1))
        {
            l->bat_has_spawn_pos = 0;
        }

        if (!l->bat_has_spawn_pos || det_rng_int_n(&l->rand, 30) == 0 ||
            bat_spawn_distance_squared(l) < 4.0F)
        {
            /* the two draws per axis split: C does not fix the order of the
             * operands of a subtraction, Java draws left to right */
            int dxa = det_rng_int_n(&l->rand, 7);
            int dxb = det_rng_int_n(&l->rand, 7);
            int dy = det_rng_int_n(&l->rand, 6);
            int dza = det_rng_int_n(&l->rand, 7);
            int dzb = det_rng_int_n(&l->rand, 7);
            l->bat_spawn_x = (int)l->e.pos_x + dxa - dxb;
            l->bat_spawn_y = (int)l->e.pos_y + dy - 2;
            l->bat_spawn_z = (int)l->e.pos_z + dza - dzb;
            l->bat_has_spawn_pos = 1;
        }

        double var1 = (double)l->bat_spawn_x + 0.5 - l->e.pos_x;
        double var3 = (double)l->bat_spawn_y + 0.1 - l->e.pos_y;
        double var5 = (double)l->bat_spawn_z + 0.5 - l->e.pos_z;
        l->e.motion_x += (squidbat_signum(var1) * 0.5 - l->e.motion_x) * 0.10000000149011612;
        l->e.motion_y += (squidbat_signum(var3) * 0.699999988079071 - l->e.motion_y) * 0.10000000149011612;
        l->e.motion_z += (squidbat_signum(var5) * 0.5 - l->e.motion_z) * 0.10000000149011612;
        float var7 = (float)(fd_atan2(l->e.motion_z, l->e.motion_x) * 180.0 / 3.14159265358979323846) - 90.0F;
        float var8 = squidbat_wrap_180(var7 - l->rotation_yaw);
        l->move_forward = 0.5F;
        l->rotation_yaw += var8;

        if (det_rng_int_n(&l->rand, 100) == 0 &&
            block_normal_cube_at(l->world, mh_floor(l->e.pos_x), (int)l->e.pos_y + 1,
                                 mh_floor(l->e.pos_z)))
        {
            l->data_watcher_16 |= 1;
        }
    }
}

/* EntityBat.onLivingUpdate: EntityLivingBase.onLivingUpdate (the new-AI
 * branch runs the bat's updateAITicks inside), then the bat's layers, which
 * are empty: EntityAmbientCreature adds nothing. The scene's hanging pin sits
 * in living_on_update (the Entity layer). */
void squidbat_bat_on_living_update(struct living *l, det_state *det)
{
    living_on_living_update(l, det);
}

/* The kind tick the scene drives: the squid's onLivingUpdate and the bat's
 * updateAITicks body. */
void squidbat_kind_tick(struct living *l, det_state *det)
{
    if (l->kind == AK_SQUID) squid_on_living_update(l, det);
}

/* --------------------------------------------------------- block weights */

/* EntityAnimal.getBlockPathWeight. */
float animal_get_block_path_weight(struct living *l, int x, int y, int z)
{
    float w;

    if ((world_get_block(l->world, x, y - 1, z) & 4095) == 2) w = 10.0F;
    else w = living_light_brightness(l->world, l->an ? l->an->skylight : 0, x, y, z) - 0.5F;

    trace("pathweight", "iiiif", l->entity_id, x, y, z, (double)w);
    return w;
}
