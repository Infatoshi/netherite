/* EntitySpider, EntityCaveSpider and the spider jockey's rider skeleton,
 * ported from oracle/src/entity/monster/EntitySpider.java, EntityCaveSpider.java,
 * EntitySkeleton.java, EntityMob.java and EntityCreature.java.
 *
 * The spider is an old-AI mob: isAIEnabled stays EntityLiving's false, so it
 * never builds an EntityAITasks list. Its whole behaviour runs through
 * EntityCreature.updateEntityActionState over EntityCreature.pathToEntity
 * (World.getPathEntityToEntity / getEntityPathToXYZ, not the navigator), which
 * is why the base state record's navigator slots stay zero for a spider.
 * EntityCaveSpider only changes the size, the max health and adds the poison on
 * hit. The jockey rider (EntitySpider.onSpawnWithEgg's 1-in-100 skeleton) is a
 * full EntitySkeleton: it is AI-enabled, runs the shared task machinery, and
 * rides the spider only in the Entity pointer sense (the probe's tick walks its
 * list directly, so updateRidden never runs and nothing repositions the rider).
 */
#include "nbtw.h"
#include "hostiles_spider.h"
#include "env.h"
#include "combatench.h"
#include "hostiles.h"
#include "hostiles_skeleton.h"
#include "ai.h"
#include "blocks.h"
#include "enchant.h"
#include "items.h"
#include "jmath.h"
#include "living.h"
#include "pathfind.h"
#include "potion.h"
#include "projectile.h"
#include "raytrace.h"
#include "slimes.h"
#include "smath.h"
#include "trace.h"
#include "world.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* MathHelper.sqrt_float: the root of a float sum. */
static float mh_sqrt_float(float f)
{
    return (float)sqrt((double)f);
}

/* MathHelper.sqrt_double: the root of a double sum, rounded to float. The
 * argument must stay double (the leap's squared distance is one). */
static float mh_sqrt_double(double d)
{
    return (float)sqrt(d);
}

static float wrap_angle_float(float a)
{
    a = fmodf(a, 360.0F);
    if (a >= 180.0F) a -= 360.0F;
    if (a < -180.0F) a += 360.0F;
    return a;
}

/* EntityMob's brightness probe (Entity.getBrightness): the block light at the
 * body's 2/3 height; the arena's chunks are all loaded, so the blockExists
 * gate never fires. */
static float spider_brightness(struct living *l)
{
    int x = mh_floor(l->e.pos_x);
    int z = mh_floor(l->e.pos_z);
    double v4 = (l->e.bounding_box.max_y - l->e.bounding_box.min_y) * 0.66;
    int y = mh_floor(l->e.pos_y - (double)l->e.y_offset + v4);
    return living_light_brightness(l->world, l->an ? l->an->skylight : 0, x, y, z);
}

/* EntityLivingBase.canEntityBeSeen: the eye-to-eye block ray. */
static int spider_can_entity_be_seen(struct living *l, struct living *other)
{
    double sx = l->e.pos_x;
    double sy = l->e.pos_y + (double)living_eye_height(l);
    double sz = l->e.pos_z;
    double ex = other->e.pos_x;
    double ey = other->e.pos_y + (double)living_eye_height(other);
    double ez = other->e.pos_z;

    struct rt_mop mop;
    return raytrace_trace(l->world, sx, sy, sz, ex, ey, ez, &mop) == 0;
}

/* Entity.getDistanceToEntity. */
static float spider_distance_to_entity(struct living *l, struct living *other)
{
    float var2 = (float)(l->e.pos_x - other->e.pos_x);
    float var3 = (float)(l->e.pos_y - other->e.pos_y);
    float var4 = (float)(l->e.pos_z - other->e.pos_z);
    return mh_sqrt_float(var2 * var2 + var3 * var3 + var4 * var4);
}

/* World.getPathEntityToEntity(this, target, 16, true, false, false, true): the
 * flags are (wooden doors allowed, movement blocks allowed, pathing in water,
 * can drown). movement-blocks-allowed false is what lets the old AI path
 * through a closed wooden door. */
static void spider_path_to_target(struct living *l, struct living *target)
{
    path_drop(l->creature_path);
    l->creature_path = ai_creature_path_to_entity(l, target, 16.0F, 1, 0, 0, 1);
}

/* EntityCreature.updateWanderPath: ten random cells, the light weight picks. */
static void spider_update_wander_path(struct living *l, det_state *det)
{
    int var2 = -1, var3 = -1, var4 = -1;
    float var5 = -99999.0F;
    int found = 0;

    for (int var6 = 0; var6 < 10; ++var6)
    {
        int var7 = mh_floor(l->e.pos_x + (double)det_rng_int_n(&l->rand, 13) - 6.0);
        int var8 = mh_floor(l->e.pos_y + (double)det_rng_int_n(&l->rand, 7) - 3.0);
        int var9 = mh_floor(l->e.pos_z + (double)det_rng_int_n(&l->rand, 13) - 6.0);
        float var10 = 0.5F - living_light_brightness(l->world, l->an ? l->an->skylight : 0, var7, var8, var9);

        if (var10 > var5)
        {
            var5 = var10;
            var2 = var7;
            var3 = var8;
            var4 = var9;
            found = 1;
        }
    }

    if (!found) return;

    /* World.getEntityPathToXYZ(this, x, y, z, 10.0F, true, false, false, true) */
    path_drop(l->creature_path);
    l->creature_path = ai_creature_path_to_xyz(l, var2, var3, var4, 10.0F, 1, 0, 0, 1);
    (void)det;
}

/* ------------------------------------------------------------ the spiders */

/* EntitySpider.setInWeb's empty override: a spider is never "in web", so
 * neither the flag nor the fall-distance reset of Entity.setInWeb lands. */
static void spider_set_in_web(void *self)
{
    (void)self;
}

/* EntitySpider(World) and EntityCaveSpider(World) after super(). The attribute
 * bases are applyEntityAttributes': EntityMob's !isAIEnabled branch set the
 * 0.1 base first and the kind's 0.8 overwrites it. */
void spider_construct(struct living *l, det_state *det)
{
    /* EntityMob's constructor: the XP a player kill drops */
    l->experience_value = 5;
    (void)det;
    attrs_set_base(&l->attrs.a[ATTR_MAX_HEALTH], l->kind == HK_CAVE_SPIDER ? 12.0 : 16.0);
    attrs_set_base(&l->attrs.a[ATTR_MOVEMENT_SPEED], 0.800000011920929);
    attrs_set_base(&l->attrs.a[ATTR_FOLLOW_RANGE], 16.0);
    attrs_set_base(&l->attrs.a[ATTR_ATTACK_DAMAGE], 2.0);   /* EntityMob's registration */
    entity_set_size(&l->e, l->kind == HK_CAVE_SPIDER ? 0.7f : 1.4f, l->kind == HK_CAVE_SPIDER ? 0.5f : 0.9f);
    l->e.set_in_web = spider_set_in_web;
    /* the constructor's setHealth(getMaxHealth()); living_init ran it against
     * the 20 base, before applyEntityAttributes' values landed */
    living_set_health(l, living_max_health(l));
    /* the navigator the spider holds but never drives: PathNavigate's field
     * defaults (canPassOpenWoodenDoors true, the rest false) are what the
     * state record reads */
    l->nav.can_pass_open_doors = 1;
    /* dataWatcher 16 (the climb flag) starts at 0 from living_init */
}

/* EntitySpider.findPlayerToAttack: only in the dark. */
static struct living *spider_find_player_to_attack(struct living *l)
{
    if (spider_brightness(l) >= 0.5F) return NULL;

    return an_closest_vulnerable_player(l->an, l->e.pos_x, l->e.pos_y, l->e.pos_z, 16.0);
}

/* EntityMob.attackEntity: the touch. */
static void spider_touch_attack(struct living *l, struct living *target, float distance, det_state *det)
{
    if (l->attack_time <= 0 && distance < 2.0F &&
        target->e.bounding_box.max_y > l->e.bounding_box.min_y &&
        target->e.bounding_box.min_y < l->e.bounding_box.max_y)
    {
        l->attack_time = 20;
        spider_attack_entity_as_mob(l, target, det);
    }
}

/* EntitySpider.attackEntity: the daylight drop, the leap, else the touch. */
static void spider_attack_entity(struct living *l, struct living *target, float distance, det_state *det)
{
    float br = spider_brightness(l);

    if (br > 0.5F && det_rng_int_n(&l->rand, 100) == 0)
    {
        l->entity_to_attack = 0;
        return;
    }

    if (distance > 2.0F && distance < 6.0F && det_rng_int_n(&l->rand, 10) == 0)
    {
        if (l->e.on_ground)
        {
            double var4 = target->e.pos_x - l->e.pos_x;
            double var6 = target->e.pos_z - l->e.pos_z;
            float var8 = mh_sqrt_double(var4 * var4 + var6 * var6);
            l->e.motion_x = var4 / (double)var8 * 0.5 * 0.800000011920929 + l->e.motion_x * 0.20000000298023224;
            l->e.motion_z = var6 / (double)var8 * 0.5 * 0.800000011920929 + l->e.motion_z * 0.20000000298023224;
            l->e.motion_y = 0.4000000059604645;
        }
    }
    else
    {
        spider_touch_attack(l, target, distance, det);
    }
}

/* EntityCreature.updateEntityActionState, the spider's whole AI tick. */
void spider_update_entity_action_state(struct living *l, det_state *det)
{
    /* the fleeingTick speed-bonus removal: the modifier is never applied here */
    if (l->fleeing_tick > 0 && --l->fleeing_tick == 0)
    {
    }

    l->has_attacked = 0;   /* isMovementCeased() */

    if (lv_get(l->entity_to_attack) == NULL && !l->entity_to_attack_gone)
    {
        l->entity_to_attack = lv_ref(spider_find_player_to_attack(l));

        if (lv_get(l->entity_to_attack) != NULL)
        {
            spider_path_to_target(l, lv_get(l->entity_to_attack));
        }
    }
    else if (living_is_alive(lv_get(l->entity_to_attack)))
    {
        float var2 = spider_distance_to_entity(l, lv_get(l->entity_to_attack));

        if (spider_can_entity_be_seen(l, lv_get(l->entity_to_attack)))
        {
            spider_attack_entity(l, lv_get(l->entity_to_attack), var2, det);
        }
    }
    else
    {
        l->entity_to_attack = 0;
    }
    /* a dead non-living entityToAttack (entity_to_attack_gone) took the
     * else branch */
    l->entity_to_attack_gone = 0;

    /* entityToAttack instanceof EntityPlayerMP: the probe's player is a plain
     * EntityPlayer, so the creative-mode drop never runs */

    if (!l->has_attacked && lv_get(l->entity_to_attack) != NULL &&
        (l->creature_path == 0 || det_rng_int_n(&l->rand, 20) == 0))
    {
        spider_path_to_target(l, lv_get(l->entity_to_attack));
    }
    else if (!l->has_attacked &&
             ((l->creature_path == 0 && det_rng_int_n(&l->rand, 180) == 0) ||
              det_rng_int_n(&l->rand, 120) == 0 || l->fleeing_tick > 0) &&
             l->entity_age < 100)
    {
        spider_update_wander_path(l, det);
    }

    int var22 = mh_floor(l->e.bounding_box.min_y + 0.5);
    int var3 = l->is_in_water;
    int var4 = living_handle_lava_movement(l);
    l->rotation_pitch = 0.0F;

    if (l->creature_path != 0 && det_rng_int_n(&l->rand, 100) != 0)
    {
        struct path_ent *path = path_at(l->creature_path);
        double var5x = (double)path->pts[path->index][0] + (double)((int)(l->e.width + 1.0F)) * 0.5;
        double var5y = (double)path->pts[path->index][1];
        double var5z = (double)path->pts[path->index][2] + (double)((int)(l->e.width + 1.0F)) * 0.5;
        double var6 = (double)(l->e.width * 2.0F);

        /* Vec3.squareDistanceTo(posX, var5.yCoord, posZ) < var6 * var6: the y
         * argument is the point's own y, so the y term cancels out and the test
         * is on x and z only. */
        while (path != NULL &&
               (var5x - l->e.pos_x) * (var5x - l->e.pos_x) +
               (var5z - l->e.pos_z) * (var5z - l->e.pos_z) < var6 * var6)
        {
            ++path->index;

            if (path->index >= path->length)
            {
                path = NULL;
                path_drop(l->creature_path);
                l->creature_path = 0;
            }
            else
            {
                var5x = (double)path->pts[path->index][0] + (double)((int)(l->e.width + 1.0F)) * 0.5;
                var5y = (double)path->pts[path->index][1];
                var5z = (double)path->pts[path->index][2] + (double)((int)(l->e.width + 1.0F)) * 0.5;
            }
        }

        l->is_jumping = 0;

        if (path != NULL)
        {
            double var8 = var5x - l->e.pos_x;
            double var10 = var5z - l->e.pos_z;
            double var12 = var5y - (double)var22;
            float var14 = (float)(fd_atan2(var10, var8) * 180.0 / 3.141592653589793) - 90.0F;
            float var15 = wrap_angle_float(var14 - l->rotation_yaw);
            l->move_forward = (float)attrs_value(&l->attrs.a[ATTR_MOVEMENT_SPEED]);

            if (var15 > 30.0F) var15 = 30.0F;
            if (var15 < -30.0F) var15 = -30.0F;

            l->rotation_yaw += var15;

            if (var12 > 0.0) l->is_jumping = 1;
        }

        if (lv_get(l->entity_to_attack) != NULL)
        {
            living_face_entity(l, lv_get(l->entity_to_attack), 30.0F, 30.0F);
        }

        if (l->e.is_collided_horizontally && l->creature_path == 0)
        {
            l->is_jumping = 1;
        }

        if (det_rng_float(&l->rand) < 0.8F && (var3 || var4))
        {
            l->is_jumping = 1;
        }
    }
    else
    {
        /* EntityLiving.updateEntityActionState through super: the age bump,
         * the input reset, despawnEntity and the idle look tail */
        living_update_entity_action_state(l);

        if (l->creature_path)
        {
            path_drop(l->creature_path);
            l->creature_path = 0;
        }
    }
}

/* EntitySpider.onUpdate's tail: the climb flag follows the horizontal
 * collision. */
void spider_on_update_tail(struct living *l)
{
    spider_set_beside_climbable_block(l, l->e.is_collided_horizontally);
}

/* EntitySpider.setBesideClimbableBlock: dataWatcher 16's bit 0. */
void spider_set_beside_climbable_block(struct living *l, int beside)
{
    if (beside) l->data_watcher_16 |= 1;
    else l->data_watcher_16 &= ~1;
}

/* EntitySpider.onLivingUpdate: EntityMob.onLivingUpdate, then the base. */
void spider_on_living_update(struct living *l, det_state *det)
{
    /* updateArmSwingProgress: isSwingInProgress is always false for a spider */
    l->swing_progress = 0.0F;

    if (spider_brightness(l) > 0.5F)
    {
        l->entity_age += 2;
    }

    living_default_on_living_update(l, det);
}

/* EntityMob.attackEntityAsMob, plus EntityCaveSpider's poison. */
int spider_attack_entity_as_mob(struct living *l, struct living *target, det_state *det)
{
    /* EntityMob.attackEntityAsMob: the mob source keeps the 0.3F hunger
     * damage the player's damageEntity adds (only a bypasses-armour or
     * absolute source clears it) */
    int hit = mob_attack_entity_as_mob(l, target, det);
    int diff = l->an != NULL ? l->an->difficulty : 2;
    int seconds = diff == 2 ? 7 : diff == 3 ? 15 : 0;

    if (hit && l->kind == HK_CAVE_SPIDER && seconds > 0)
    {
        /* EntityCaveSpider.attackEntityAsMob: 7 seconds on NORMAL, 15 on HARD,
         * none below; the spider's own isPotionApplicable does not apply, this
         * is the target's */
        struct potion_effect eff = {
            .id = (uint8_t)POT_POISON,
            .duration = seconds * 20,
            .amplifier = 0,
            .is_splash = 0,
            .is_ambient = 0
        };
        living_add_potion_effect(target, &eff, det);
    }

    return hit;
}

/* EntitySpider.dropFewItems: EntityLiving's string drop, then the eye. */
void spider_drop_few_items(struct living *l, int hit_by_player, int looting, det_state *det)
{
    /* super.dropFewItems: func_146068_u() = Items.string (287) */
    int var4 = det_rng_int_n(&l->rand, 3);
    if (looting > 0) var4 += det_rng_int_n(&l->rand, looting + 1);

    for (int var5 = 0; var5 < var4; ++var5)
    {
        if (l->an) an_spawn_item(l->an, l->an->n, l->e.pos_x, l->e.pos_y, l->e.pos_z, 287, 0, 1, 10);
    }

    if (hit_by_player && (det_rng_int_n(&l->rand, 3) == 0 || det_rng_int_n(&l->rand, 1 + looting) > 0))
    {
        if (l->an) an_spawn_item(l->an, l->an->n, l->e.pos_x, l->e.pos_y, l->e.pos_z, 375, 0, 1, 10);
    }
}

/* EntitySpider.writeEntityToNBT has no tags of its own: dataWatcher 16 (the
 * climb flag) never reaches the NBT. */
void spider_write_kind_nbt(struct living *l, struct nbtw *w)
{
    (void)l;
    (void)w;
}

/* EntitySpider.isPotionApplicable: poison only. */
int spider_is_potion_applicable(const struct living *l, int potion_id)
{
    (void)l;
    return potion_id != POT_POISON;
}

/* The partner a pending jockey built (one spawn at a time in this world): the
 * spider's skeleton rider or the baby zombie's chicken. */
#define pending_rider_ent (nw_env->hostiles_spider.pending_rider_ent)

/* spawnEntityInWorld(partner) inside an onSpawnWithEgg: the partner joins the
 * world's chunk lists now, BEFORE the mob whose egg path spawned it; the
 * caller completes the tick-list insert with egg_take_pending_partner. */
void egg_add_pending_partner(struct living *partner)
{
    struct an_ent *en = an_ent_alloc();
    en->used = 1;
    en->is_living = 1;
    en->spawn_index = -1;
    en->livh = lv_ref(partner);
    en->ieh = 0;
    an_add_living_chunk(partner->an, en, partner);
    pending_rider_ent = an_ref(en);
}

/* EntitySpider.GroupData.func_111104_a: nextInt(5), 0-1 moveSpeed, 2
 * damageBoost, 3 regeneration, 4 invisibility. */
int spider_group_potion(int roll)
{
    return roll <= 1 ? POT_MOVE_SPEED : (roll <= 2 ? POT_DAMAGE_BOOST : (roll <= 3 ? POT_REGENERATION : POT_INVISIBILITY));
}

/* EntitySpider.onSpawnWithEgg: the 1-in-100 spider jockey, then the
 * GroupData (onSpawnWithEgg(null) makes a new one): on HARD,
 * worldObj.rand.nextFloat() < 0.1F * func_147462_b rolls the pack's effect,
 * applied as addPotionEffect(new PotionEffect(id, Integer.MAX_VALUE)).
 * spawnEntityInWorld(rider) runs during onSpawnWithEgg, so the rider joins
 * the world's chunk lists BEFORE the spider that carries it; the caller
 * completes the tick-list insert (after the spider) with
 * egg_take_pending_partner. A spider the natural spawner's record path
 * already spawned (an->egg_adopt) takes its pack's effect from the record,
 * with no draw. */
struct living *spider_jockey_check(struct living *l, det_state *det)
{
    struct living *rider = NULL;
    pending_rider_ent = 0;

    if (det_rng_int_n(&l->an->iew.world_rand, 100) == 0)
    {
        rider = spider_spawn_jockey_skeleton(l->an, l, det);
        if (rider)
        {
            living_mount(rider, l);
            egg_add_pending_partner(rider);
        }
    }

    int potion = 0;
    if (l->an->egg_adopt)
        potion = l->an->egg_spider_potion;
    else if (l->an->difficulty == 3 && det_rng_float(&l->an->iew.world_rand) < 0.1F * l->difficulty_factor)
        potion = spider_group_potion(det_rng_int_n(&l->an->iew.world_rand, 5));
    if (potion > 0)
    {
        struct potion_effect eff = { .id = (uint8_t)potion, .amplifier = 0, .duration = INT_MAX };
        living_add_potion_effect(l, &eff, det);
    }
    return rider;
}

/* The caller's half of a jockey spawn: the partner's tick-list insert, at the
 * index after the mob whose egg path spawned it. */
void egg_take_pending_partner(void)
{
    struct an_ent *en = an_deref(pending_rider_ent);
    pending_rider_ent = 0;

    if (!en) return;

    struct an_world *an = lv_get(en->livh)->an;
    en->spawn_index = an->n;
    lv_get(en->livh)->spawn_index = an->n;
    an_list_push(an, en);
}

/* The jockey's rider: new EntitySkeleton(worldObj), setLocationAndAngles at the
 * spider's pose, onSpawnWithEgg(null). The EntityList id draw and the
 * per-entity Random come through living_init on the shared DET_OTHER streams,
 * in the spawn order. It takes no persistence (only the setup's own mobs do),
 * so it despawns on the vanilla rule. */
struct living *spider_spawn_jockey_skeleton(struct an_world *an, struct living *l, det_state *det)
{
    struct living *rider = living_alloc();
    if (!rider) return NULL;

    living_init(rider, an->w, HK_SKELETON, an->det);
    rider->an = an;
    rider->spawn_index = an->n;
    /* func_147462_b at the spider's pose: the spider's own */
    rider->difficulty_factor = l->difficulty_factor;
    skeleton_construct(rider, det);
    living_set_location_and_angles(rider, l->e.pos_x, l->e.pos_y, l->e.pos_z, l->rotation_yaw, 0.0F);
    /* the rider's own onSpawnWithEgg: EntitySkeleton's body over
     * EntityLiving's (the follow-range spawn bonus, then the skeleton's own) */
    living_skeleton_on_spawn_with_egg(rider, det);
    return rider;
}

