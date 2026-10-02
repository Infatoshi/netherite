/* EntityEnderman, ported from
 * oracle/src/entity/monster/EntityEnderman.java over EntityMob, EntityCreature,
 * EntityLiving and EntityLivingBase (1.7.10).
 *
 * EntityEnderman does not override isAIEnabled, so EntityLiving's false stands:
 * the enderman never runs EntityAITasks. Every tick EntityLivingBase.onLivingUpdate
 * calls updateEntityActionState instead, which is EntityCreature's body (the
 * findPlayerToAttack stare, the pathToEntity chase and attackEntity) with
 * EntityLiving's old-AI body (despawnEntity, the random target pick, the water
 * jump) in its else branch. EntityMob's own isAIEnabled-free body still runs
 * from EntityMob.onLivingUpdate, and onSpawnWithEgg is EntityLiving's (the
 * random follow-range bonus only: no setCanPickUpLoot, no addRandomArmor).
 *
 * The three data watcher bytes are the block it carries (16), that block's
 * metadata (17) and the screaming flag (18). stepHeight is 1.0F.
 *
 * Where the Java reads a DamageSource the native prototype carries the source
 * family (living.h's DMG_*), so the two DamageSource facts EntityEnderman
 * branches on come in as arguments: `indirect` (an EntityDamageSourceIndirect:
 * an arrow, a thrown item, a fireball, indirect magic: living.h's
 * dmg_is_indirect) and `attacker_is_player` (an EntityDamageSource whose
 * entity, the shooter for an indirect one, is a player).
 *
 * Every literal is copied as printed, and every statement that reads state the
 * previous statement wrote stays in vanilla's order. */
#include "nbtw.h"
#include "hostiles_enderman.h"
#include "combatench.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "ai.h"
#include "blocks.h"
#include "collide.h"
#include "det.h"
#include "hostiles.h"
#include "jmath.h"
#include "living.h"
#include "player.h"
#include "raytrace.h"
#include "slimes.h"
#include "smath.h"
#include "world.h"

/* The probe world's game rules: mobGriefing defaults to true and the probe
 * never changes it. */
#define PROBE_MOB_GRIEFING 1

enum { ITEM_ENDER_PEARL = 368 };

/* EntityEnderman.attackingSpeedBoostModifier: "Attacking speed boost", +6.2,
 * operation 0, saved false. */
static const struct attr_mod attacking_speed_boost_modifier = {
    .name = MODN_ATTACKING_SPEED_BOOST,
    .amount = 6.199999809265137,
    .operation = 0,
    .uuid_msb = (int64_t)0x020E0DFB87AE4653ULL,
    .uuid_lsb = (int64_t)0x9556831010E291A0ULL,
    .saved = 0
};

/* EntityEnderman.setScreaming: DataWatcher.updateObject(18), which marks
 * the watcher changed whenever the value differs (the tracker's next
 * update runs even when a later set in the same tick puts it back) */
static void enderman_set_screaming(struct living *l, int v)
{
    if (l->enderman_screaming != v)
    {
        l->enderman_screaming = v;
        l->dw_touched = 1;
    }
}

void enderman_restore_speed_boost(struct living *l)
{
    attrs_apply(&l->attrs.a[ATTR_MOVEMENT_SPEED], &attacking_speed_boost_modifier);
}

/* EntityCreature.field_110181_i: "Fleeing speed bonus", 2.0, operation 2. */
static const struct attr_mod fleeing_speed_bonus_modifier = {
    .name = MODN_FLEEING_SPEED_BONUS,
    .amount = 2.0,
    .operation = 2,
    .uuid_msb = (int64_t)0xE199AD21BA8A4C53ULL,
    .uuid_lsb = (int64_t)0x8D136182D5C69D3AULL,
    .saved = 0
};

/* ------------------------------------------------------------ the helpers */

/* MathHelper.sqrt_double: the square root rounded to float and widened back. */
static double sqrt_double(double v)
{
    return (double)(float)sqrt(v);
}

/* MathHelper.sqrt_float. */
static float sqrt_float(float f)
{
    return (float)sqrt((double)f);
}

static float wrap_angle_float(float a)
{
    a = fmodf(a, 360.0F);
    if (a >= 180.0F) a -= 360.0F;
    if (a < -180.0F) a += 360.0F;
    return a;
}

struct vec3 {
    double x, y, z;
};

/* Vec3.normalize. */
static struct vec3 vec_normalize(struct vec3 v)
{
    double len = sqrt_double(v.x * v.x + v.y * v.y + v.z * v.z);

    if (len < 1.0E-4) return (struct vec3){ 0.0, 0.0, 0.0 };

    return (struct vec3){ v.x / len, v.y / len, v.z / len };
}

/* Vec3.lengthVector. */
static double vec_length(struct vec3 v)
{
    return sqrt_double(v.x * v.x + v.y * v.y + v.z * v.z);
}

/* Vec3.dotProduct. */
static double vec_dot(struct vec3 a, struct vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

/* Vec3.squareDistanceTo(double, double, double). */
static double vec_square_dist_to(struct vec3 v, double x, double y, double z)
{
    double dx = x - v.x;
    double dy = y - v.y;
    double dz = z - v.z;
    return dx * dx + dy * dy + dz * dz;
}

/* Entity.getDistanceToEntity: the differences widen to float, the squares and
 * the sum stay float, the sqrt widens back. */
static float get_distance_to_entity(struct living *l, struct living *other)
{
    float dx = (float)(l->e.pos_x - other->e.pos_x);
    float dy = (float)(l->e.pos_y - other->e.pos_y);
    float dz = (float)(l->e.pos_z - other->e.pos_z);
    return sqrt_float(dx * dx + dy * dy + dz * dz);
}

/* Entity.getDistanceSqToEntity. */
static double get_distance_sq_to_entity(struct living *l, struct living *other)
{
    double dx = l->e.pos_x - other->e.pos_x;
    double dy = l->e.pos_y - other->e.pos_y;
    double dz = l->e.pos_z - other->e.pos_z;
    return dx * dx + dy * dy + dz * dz;
}

/* EntityLiving.getBrightness(1.0F). */
static float get_brightness(struct living *l)
{
    int x = mh_floor(l->e.pos_x);
    int z = mh_floor(l->e.pos_z);

    if (world_chunk_loaded(l->world, x >> 4, z >> 4))
    {
        double v4 = (l->e.bounding_box.max_y - l->e.bounding_box.min_y) * 0.66;
        int y = mh_floor(l->e.pos_y - (double)l->e.y_offset + v4);
        return living_light_brightness(l->world, l->an ? l->an->skylight : 0, x, y, z);
    }

    return 0.0F;
}

/* EntityLiving.canEntityBeSeen. */
static int can_entity_be_seen(struct living *l, struct living *other)
{
    struct rt_mop hit;
    return raytrace_trace(l->world,
                          l->e.pos_x, l->e.pos_y + (double)living_eye_height(l), l->e.pos_z,
                          other->e.pos_x, other->e.pos_y + (double)living_eye_height(other), other->e.pos_z,
                          &hit) == 0;
}

/* EntityLivingBase.getLook(1.0F): shouldAttackPlayer is the only caller and it
 * always passes 1.0F, so only that branch is here. */
static struct vec3 entity_get_look(struct living *e)
{
    float var2 = mh_cos(-e->rotation_yaw * 0.017453292F - 3.1415927F);
    float var3 = mh_sin(-e->rotation_yaw * 0.017453292F - 3.1415927F);
    float var4 = -mh_cos(-e->rotation_pitch * 0.017453292F);
    float var5 = mh_sin(-e->rotation_pitch * 0.017453292F);
    return (struct vec3){ (double)(var3 * var4), (double)var5, (double)(var2 * var4) };
}

/* EntityLiving.getVerticalFaceSpeed: EntityEnderman does not override it. */
static int get_vertical_face_speed(struct living *l)
{
    (void)l;
    return 40;
}

/* World.blockExists. */
static int block_exists(struct world *w, int x, int y, int z)
{
    return y >= 0 && y < 256 && world_chunk_loaded(w, x >> 4, z >> 4);
}

/* Material.air: Blocks.air is the only registered block with that material. */
static int material_is_air(int id)
{
    return BLOCKS[id & 4095].material == 0;
}

/* Block.getMaterial().blocksMovement() for the block at (x, y, z). */
static int block_blocks_movement(struct world *w, int x, int y, int z)
{
    int id = world_get_block(w, x, y, z) & 4095;
    return MATERIALS[BLOCKS[id].material].blocks_movement;
}

/* EntityEnderman's static carriableBlocks table: grass, dirt, sand, gravel,
 * yellow_flower, red_flower, brown_mushroom, red_mushroom, tnt, cactus, clay,
 * pumpkin, melon_block, mycelium. */
static int is_carriable(int id)
{
    static const unsigned char carriable[256] = {
        [2] = 1, [3] = 1, [12] = 1, [13] = 1, [37] = 1, [38] = 1, [39] = 1, [40] = 1,
        [46] = 1, [81] = 1, [82] = 1, [86] = 1, [103] = 1, [110] = 1
    };

    return id >= 0 && id < 256 && carriable[id] != 0;
}

/* World.getCollidingBoundingBoxes(this, boundingBox).isEmpty(). The entity half
 * of that method adds nothing here: it tests ((Entity)e).getBoundingBox(),
 * which is Entity's null for every living entity (only the item, boat and
 * minecart classes override it), and Entity.getCollisionBox, the same null. So
 * only the block boxes are left, which is what the shared world query returns. */
static int colliding_boxes_empty(struct living *l)
{
    struct collide_list *list COLLIDE_SCRATCH = collide_scratch_begin();
    collide_list_clear(list);
    world_get_colliding_bounding_boxes(l->world, l->e.bounding_box, list);

    return list->n == 0;
}

/* ------------------------------------------------------- EntityCreature's path */

/* PathEntity.getVectorFromIndex(entity, index). */
static struct vec3 path_position(struct living *l, const struct path_ent *path)
{
    double off = (double)((int)(l->e.width + 1.0F)) * 0.5;
    return (struct vec3){
        (double)path->pts[path->index][0] + off,
        (double)path->pts[path->index][1],
        (double)path->pts[path->index][2] + off
    };
}

static void enderman_clear_path(struct living *l)
{
    path_drop(l->creature_path);
    l->creature_path = 0;
}

/* World.getPathEntityToEntity(this, target, 16.0F, true, false, false, true):
 * the four booleans are PathFinder's (canPassOpenWoodenDoors, the movement
 * block, the water pathing, canEntityDrown) in position. */
static pathref enderman_path_to_entity(struct living *l, struct living *target, float max_dist)
{
    return ai_creature_path_to_entity(l, target, max_dist, 1, 0, 0, 1);
}

static pathref enderman_path_to_xyz(struct living *l, int x, int y, int z, float max_dist)
{
    return ai_creature_path_to_xyz(l, x, y, z, max_dist, 1, 0, 0, 1);
}

/* EntityCreature.updateWanderPath. */
static void enderman_update_wander_path(struct living *l)
{
    int var1 = 0;
    int var2 = -1;
    int var3 = -1;
    int var4 = -1;
    float var5 = -99999.0F;

    for (int var6 = 0; var6 < 10; ++var6)
    {
        int var7 = mh_floor(l->e.pos_x + (double)det_rng_int_n(&l->rand, 13) - 6.0);
        int var8 = mh_floor(l->e.pos_y + (double)det_rng_int_n(&l->rand, 7) - 3.0);
        int var9 = mh_floor(l->e.pos_z + (double)det_rng_int_n(&l->rand, 13) - 6.0);

        /* EntityMob.getBlockPathWeight: 0.5F - getLightBrightness(x, y, z) */
        float var10 = 0.5F - living_light_brightness(l->world, l->an ? l->an->skylight : 0, var7, var8, var9);

        if (var10 > var5)
        {
            var5 = var10;
            var2 = var7;
            var3 = var8;
            var4 = var9;
            var1 = 1;
        }
    }

    if (var1)
    {
        enderman_clear_path(l);
        l->creature_path = enderman_path_to_xyz(l, var2, var3, var4, 10.0F);
    }
}

/* World.getClosestVulnerablePlayerToEntity(this, range): the probe's player,
 * when the run put it in World.playerEntities. */
static struct living *closest_vulnerable_player(struct living *l, double range)
{
    if (l->an == NULL) return NULL;

    return an_closest_vulnerable_player(l->an, l->e.pos_x, l->e.pos_y, l->e.pos_z, range);
}

/* EntityEnderman.shouldAttackPlayer. */
static int should_attack_player(struct living *l, struct living *player)
{
    /* armorInventory[3] a pumpkin: the player is never stared at */
    if (player->player_sp != NULL)
    {
        const struct surv_stack *head = &player->player_sp->sv.inv[39];

        if (head->count > 0 && head->item == 86) return 0;   /* Blocks.pumpkin */
    }

    struct vec3 var3 = vec_normalize(entity_get_look(player));
    struct vec3 var4;
    var4.x = l->e.pos_x - player->e.pos_x;
    var4.y = l->e.bounding_box.min_y + (double)(l->e.height / 2.0F)
             - (player->e.pos_y + (double)living_eye_height(player));
    var4.z = l->e.pos_z - player->e.pos_z;

    double var5 = vec_length(var4);
    var4 = vec_normalize(var4);
    double var7 = vec_dot(var3, var4);

    return var7 > 1.0 - 0.025 / var5 && can_entity_be_seen(player, l);
}

/* EntityEnderman.findPlayerToAttack. */
static struct living *find_player_to_attack(struct living *l)
{
    struct living *var1 = closest_vulnerable_player(l, 64.0);

    if (var1 != NULL)
    {
        if (should_attack_player(l, var1))
        {
            l->enderman_is_aggressive = 1;

            if (l->enderman_stare_timer == 0)
            {
                /* World.playSoundEffect(posX, posY, posZ, "mob.endermen.stare", 1.0F, 1.0F):
                 * no state and no draws */
            }

            if (l->enderman_stare_timer++ == 5)
            {
                l->enderman_stare_timer = 0;
                enderman_set_screaming(l, 1);
                return var1;
            }
        }
        else
        {
            l->enderman_stare_timer = 0;
        }
    }

    return NULL;
}

/* ---------------------------------------------------------- the teleports */

static void teleport_particles(struct living *l, double from_x, double from_y, double from_z)
{
    const short var30 = 128;

    for (int var31 = 0; var31 < var30; ++var31)
    {
        double var19 = (double)var31 / ((double)var30 - 1.0);
        float var21 = (det_rng_float(&l->rand) - 0.5F) * 0.2F;
        float var22 = (det_rng_float(&l->rand) - 0.5F) * 0.2F;
        float var23 = (det_rng_float(&l->rand) - 0.5F) * 0.2F;
        double var24 = from_x + (l->e.pos_x - from_x) * var19
                       + (det_rng_double(&l->rand) - 0.5) * (double)l->e.width * 2.0;
        double var26 = from_y + (l->e.pos_y - from_y) * var19 + det_rng_double(&l->rand) * (double)l->e.height;
        double var28 = from_z + (l->e.pos_z - from_z) * var19
                       + (det_rng_double(&l->rand) - 0.5) * (double)l->e.width * 2.0;

        /* World.spawnParticle("portal", ...) is the whole server-side cost of
         * the draws above: the particle itself is a client effect */
        (void)var21;
        (void)var22;
        (void)var23;
        (void)var24;
        (void)var26;
        (void)var28;
    }

    /* World.playSoundEffect(from, "mob.endermen.portal", 1.0F, 1.0F) and
     * Entity.playSound: no state and no draws */
}

/* EntityEnderman.teleportTo. */
static int teleport_to(struct living *l, double x, double y, double z)
{
    double var7 = l->e.pos_x;
    double var9 = l->e.pos_y;
    double var11 = l->e.pos_z;
    l->e.pos_x = x;
    l->e.pos_y = y;
    l->e.pos_z = z;
    int var13 = 0;
    int var14 = mh_floor(l->e.pos_x);
    int var15 = mh_floor(l->e.pos_y);
    int var16 = mh_floor(l->e.pos_z);

    if (block_exists(l->world, var14, var15, var16))
    {
        int var17 = 0;

        while (!var17 && var15 > 0)
        {
            if (block_blocks_movement(l->world, var14, var15 - 1, var16))
            {
                var17 = 1;
            }
            else
            {
                l->e.pos_y -= 1.0;
                --var15;
            }
        }

        if (var17)
        {
            entity_set_position(&l->e, l->e.pos_x, l->e.pos_y, l->e.pos_z);

            if (colliding_boxes_empty(l) && !living_world_is_any_liquid(l->world, l->e.bounding_box)) var13 = 1;
        }
    }

    if (!var13)
    {
        entity_set_position(&l->e, var7, var9, var11);
        return 0;
    }
    else
    {
        teleport_particles(l, var7, var9, var11);
        return 1;
    }
}

/* EntityEnderman.teleportRandomly. */
static int teleport_randomly(struct living *l)
{
    double var1 = l->e.pos_x + (det_rng_double(&l->rand) - 0.5) * 64.0;
    double var3 = l->e.pos_y + (double)(det_rng_int_n(&l->rand, 64) - 32);
    double var5 = l->e.pos_z + (det_rng_double(&l->rand) - 0.5) * 64.0;
    return teleport_to(l, var1, var3, var5);
}

/* EntityEnderman.teleportToEntity. */
static int teleport_to_entity(struct living *l, struct living *other)
{
    struct vec3 var2 = vec_normalize((struct vec3){
        l->e.pos_x - other->e.pos_x,
        l->e.bounding_box.min_y + (double)(l->e.height / 2.0F) - other->e.pos_y + (double)living_eye_height(other),
        l->e.pos_z - other->e.pos_z
    });
    double var3 = 16.0;
    double var5 = l->e.pos_x + (det_rng_double(&l->rand) - 0.5) * 8.0 - var2.x * var3;
    double var7 = l->e.pos_y + (double)(det_rng_int_n(&l->rand, 16) - 8) - var2.y * var3;
    double var9 = l->e.pos_z + (det_rng_double(&l->rand) - 0.5) * 8.0 - var2.z * var3;
    return teleport_to(l, var5, var7, var9);
}

/* ------------------------------------------------------ the attack path */

/* EntityMob.attackEntityAsMob (the thorns on the target's armour still
 * answer). */
static int attack_entity_as_mob(struct living *l, struct living *target, det_state *det)
{
    return mob_attack_entity_as_mob(l, target, det);
}

/* EntityMob.attackEntity. */
static void attack_entity(struct living *l, struct living *target, float dist, det_state *det)
{
    if (l->attack_time <= 0 && dist < 2.0F
        && target->e.bounding_box.max_y > l->e.bounding_box.min_y
        && target->e.bounding_box.min_y < l->e.bounding_box.max_y)
    {
        l->attack_time = 20;
        attack_entity_as_mob(l, target, det);
    }
}

/* ------------------------------------------------------- the tick halves */

/* EntityEnderman.attackEntityFrom. */
int enderman_attack_entity_from(struct living *l, struct living *attacker, int source, float amount,
                                int indirect, int attacker_is_player, det_state *det)
{
    if (l->invulnerable) return 0;   /* isEntityInvulnerable */

    enderman_set_screaming(l, 1);

    if (attacker_is_player)
    {
        l->enderman_is_aggressive = 1;
    }

    if (indirect)
    {
        l->enderman_is_aggressive = 0;

        for (int var3 = 0; var3 < 64; ++var3)
        {
            if (teleport_randomly(l)) return 1;
        }

        return 0;
    }

    return living_attack_entity_from_attacker_base(l, attacker, source, amount, det);
}

/* EntityLiving.updateEntityActionState, the old AI's body (EntityCreature's
 * else branch). */
static void living_update_entity_action_state_old(struct living *l, det_state *det)
{
    /* EntityLivingBase.updateEntityActionState */
    ++l->entity_age;

    l->move_strafing = 0.0F;
    l->move_forward = 0.0F;
    living_despawn(l);
    float var1 = 8.0F;

    if (det_rng_float(&l->rand) < 0.02F)
    {
        /* EntityLiving.updateEntityActionState's World.getClosestPlayerToEntity:
         * no vulnerable gate, only Entity.isDead (the dead player is targetable
         * until its deathTime 20 setDead) */
        struct living *var2 = an_closest_player(l->an, l->e.pos_x, l->e.pos_y, l->e.pos_z, (double)var1);

        if (var2 != NULL)
        {
            l->current_target = lv_ref(var2);
            l->num_ticks_to_chase_target = 10 + det_rng_int_n(&l->rand, 20);
        }
        else
        {
            l->random_yaw_velocity = (det_rng_float(&l->rand) - 0.5F) * 20.0F;
        }
    }

    if (lv_get(l->current_target) != NULL)
    {
        living_face_entity(l, lv_get(l->current_target), 10.0F, (float)get_vertical_face_speed(l));

        if ((l->num_ticks_to_chase_target-- <= 0) || lv_get(l->current_target)->is_dead
            || get_distance_sq_to_entity(l, lv_get(l->current_target)) > (double)(var1 * var1))
        {
            l->current_target = 0;
        }
    }
    else
    {
        if (det_rng_float(&l->rand) < 0.05F)
        {
            l->random_yaw_velocity = (det_rng_float(&l->rand) - 0.5F) * 20.0F;
        }

        l->rotation_yaw += l->random_yaw_velocity;
        l->rotation_pitch = l->default_pitch;
    }

    int var4 = living_is_in_water(l);
    int var3 = living_handle_lava_movement(l);

    if (var4 || var3)
    {
        l->is_jumping = det_rng_float(&l->rand) < 0.8F;
    }

    (void)det;
}

/* EntityCreature.updateEntityActionState. */
void enderman_update_entity_action_state(struct living *l, det_state *det)
{
    if (l->fleeing_tick > 0 && --l->fleeing_tick == 0)
    {
        attrs_remove(&l->attrs.a[ATTR_MOVEMENT_SPEED], &fleeing_speed_bonus_modifier);
    }

    l->has_attacked = 0;   /* EntityCreature.isMovementCeased */
    float var21 = 16.0F;

    if (lv_get(l->entity_to_attack) == NULL)
    {
        l->entity_to_attack = lv_ref(find_player_to_attack(l));

        if (lv_get(l->entity_to_attack) != NULL)
        {
            enderman_clear_path(l);
            l->creature_path = enderman_path_to_entity(l, lv_get(l->entity_to_attack), var21);
        }
    }
    else if (living_is_alive(lv_get(l->entity_to_attack)))
    {
        float var2 = get_distance_to_entity(l, lv_get(l->entity_to_attack));

        if (can_entity_be_seen(l, lv_get(l->entity_to_attack)))
        {
            attack_entity(l, lv_get(l->entity_to_attack), var2, det);
        }
    }
    else
    {
        l->entity_to_attack = 0;
    }

    /* the EntityPlayerMP creative check cannot match the probe's plain player */

    if (!l->has_attacked && lv_get(l->entity_to_attack) != NULL
        && (l->creature_path == 0 || det_rng_int_n(&l->rand, 20) == 0))
    {
        pathref p = enderman_path_to_entity(l, lv_get(l->entity_to_attack), var21);
        enderman_clear_path(l);
        l->creature_path = p;
    }
    else if (!l->has_attacked
             && ((l->creature_path == 0 && det_rng_int_n(&l->rand, 180) == 0)
                 || det_rng_int_n(&l->rand, 120) == 0 || l->fleeing_tick > 0)
             && l->entity_age < 100)
    {
        enderman_update_wander_path(l);
    }

    int var22 = mh_floor(l->e.bounding_box.min_y + 0.5);
    int var3 = living_is_in_water(l);
    int var4 = living_handle_lava_movement(l);
    l->rotation_pitch = 0.0F;

    if (l->creature_path != 0 && det_rng_int_n(&l->rand, 100) != 0)
    {
        /* EntityCreature's "followpath" section */
        double var6 = (double)(l->e.width * 2.0F);
        int have = 1;
        struct vec3 var5 = path_position(l, path_at(l->creature_path));

        while (have && vec_square_dist_to(var5, l->e.pos_x, var5.y, l->e.pos_z) < var6 * var6)
        {
            ++path_at(l->creature_path)->index;

            if (path_at(l->creature_path)->index >= path_at(l->creature_path)->length)
            {
                have = 0;
                enderman_clear_path(l);
            }
            else
            {
                var5 = path_position(l, path_at(l->creature_path));
            }
        }

        l->is_jumping = 0;

        if (have)
        {
            double var8 = var5.x - l->e.pos_x;
            double var10 = var5.z - l->e.pos_z;
            double var12 = var5.y - (double)var22;
            float var14 = (float)(fd_atan2(var10, var8) * 180.0 / 3.141592653589793) - 90.0F;
            float var15 = wrap_angle_float(var14 - l->rotation_yaw);
            l->move_forward = (float)attrs_value(&l->attrs.a[ATTR_MOVEMENT_SPEED]);

            if (var15 > 30.0F) var15 = 30.0F;
            if (var15 < -30.0F) var15 = -30.0F;

            l->rotation_yaw += var15;

            if (l->has_attacked && lv_get(l->entity_to_attack) != NULL)
            {
                double var16 = lv_get(l->entity_to_attack)->e.pos_x - l->e.pos_x;
                double var18 = lv_get(l->entity_to_attack)->e.pos_z - l->e.pos_z;
                float var20 = l->rotation_yaw;
                l->rotation_yaw = (float)(fd_atan2(var18, var16) * 180.0 / 3.141592653589793) - 90.0F;
                var15 = (var20 - l->rotation_yaw + 90.0F) * 3.1415927F / 180.0F;
                l->move_strafing = -mh_sin(var15) * l->move_forward * 1.0F;
                l->move_forward = mh_cos(var15) * l->move_forward * 1.0F;
            }

            if (var12 > 0.0) l->is_jumping = 1;
        }

        if (lv_get(l->entity_to_attack) != NULL)
        {
            living_face_entity(l, lv_get(l->entity_to_attack), 30.0F, 30.0F);
        }

        if (l->e.is_collided_horizontally && l->creature_path == 0) l->is_jumping = 1;

        if (det_rng_float(&l->rand) < 0.8F && (var3 || var4)) l->is_jumping = 1;
    }
    else
    {
        living_update_entity_action_state_old(l, det);
        enderman_clear_path(l);
    }
}

/* EntityEnderman.onLivingUpdate. */
void enderman_on_living_update(struct living *l, det_state *det)
{
    if (living_is_wet(l))
    {
        enderman_attack_entity_from(l, NULL, DMG_DROWN, 1.0F, 0, 0, det);
    }

    if (lv_get(l->enderman_last_entity_to_attack) != lv_get(l->entity_to_attack))
    {
        struct attr_instance *var1 = &l->attrs.a[ATTR_MOVEMENT_SPEED];
        attrs_remove(var1, &attacking_speed_boost_modifier);

        if (lv_get(l->entity_to_attack) != NULL)
        {
            attrs_apply(var1, &attacking_speed_boost_modifier);
        }
    }

    l->enderman_last_entity_to_attack = l->entity_to_attack;

    if (living_is_client_world(l) && PROBE_MOB_GRIEFING)
    {
        if (material_is_air(l->enderman_carried_block))
        {
            if (det_rng_int_n(&l->rand, 20) == 0)
            {
                int var6 = mh_floor(l->e.pos_x - 2.0 + det_rng_double(&l->rand) * 4.0);
                int var2 = mh_floor(l->e.pos_y + det_rng_double(&l->rand) * 3.0);
                int var3 = mh_floor(l->e.pos_z - 2.0 + det_rng_double(&l->rand) * 4.0);
                int var4 = world_get_block(l->world, var6, var2, var3) & 4095;
                if (is_carriable(var4))
                {
                    l->enderman_carried_block = var4;
                    l->enderman_carrying_data = world_get_meta(l->world, var6, var2, var3);
                    world_set_block(l->world, var6, var2, var3, 0, 0, 3);
                }
            }
        }
        else if (det_rng_int_n(&l->rand, 2000) == 0)
        {
            int var6 = mh_floor(l->e.pos_x - 1.0 + det_rng_double(&l->rand) * 2.0);
            int var2 = mh_floor(l->e.pos_y + det_rng_double(&l->rand) * 2.0);
            int var3 = mh_floor(l->e.pos_z - 1.0 + det_rng_double(&l->rand) * 2.0);
            int var4 = world_get_block(l->world, var6, var2, var3) & 4095;
            int var5 = world_get_block(l->world, var6, var2 - 1, var3) & 4095;
            if (material_is_air(var4) && !material_is_air(var5) && BLOCKS[var5].normal_block)
            {
                world_set_block(l->world, var6, var2, var3, l->enderman_carried_block, l->enderman_carrying_data, 3);
                l->enderman_carried_block = 0;
            }
        }
    }

    for (int var6 = 0; var6 < 2; ++var6)
    {
        /* World.spawnParticle("portal", ...) is a client-side effect: the six
         * draws per particle are the whole server-side cost */
        (void)(l->e.pos_x + (det_rng_double(&l->rand) - 0.5) * (double)l->e.width);
        (void)(l->e.pos_y + det_rng_double(&l->rand) * (double)l->e.height - 0.25);
        (void)(l->e.pos_z + (det_rng_double(&l->rand) - 0.5) * (double)l->e.width);
        (void)((det_rng_double(&l->rand) - 0.5) * 2.0);
        (void)(-det_rng_double(&l->rand));
        (void)((det_rng_double(&l->rand) - 0.5) * 2.0);
    }

    if ((l->an ? l->an->skylight < 4 : 0) && living_is_client_world(l))
    {
        float var7 = get_brightness(l);
        if (var7 > 0.5F
            && world_can_block_see_the_sky(l->world, mh_floor(l->e.pos_x), mh_floor(l->e.pos_y),
                                           mh_floor(l->e.pos_z))
            && det_rng_float(&l->rand) * 30.0F < (var7 - 0.4F) * 2.0F)
        {
            l->entity_to_attack = 0;
            enderman_set_screaming(l, 0);
            l->enderman_is_aggressive = 0;
            teleport_randomly(l);
        }
    }

    if (living_is_wet(l) || living_is_burning(l))
    {
        l->entity_to_attack = 0;
        enderman_set_screaming(l, 0);
        l->enderman_is_aggressive = 0;
        teleport_randomly(l);
    }

    if (l->enderman_screaming && !l->enderman_is_aggressive && det_rng_int_n(&l->rand, 100) == 0)
    {
        enderman_set_screaming(l, 0);
    }

    l->is_jumping = 0;

    if (lv_get(l->entity_to_attack) != NULL)
    {
        living_face_entity(l, lv_get(l->entity_to_attack), 100.0F, 100.0F);
    }

    if (living_is_client_world(l) && living_is_alive(l))
    {
        if (lv_get(l->entity_to_attack) != NULL)
        {
            if (lv_get(l->entity_to_attack)->kind == HK_PLAYER && should_attack_player(l, lv_get(l->entity_to_attack)))
            {
                if (get_distance_sq_to_entity(l, lv_get(l->entity_to_attack)) < 16.0)
                {
                    teleport_randomly(l);
                }

                l->enderman_teleport_delay = 0;
            }
            else if (get_distance_sq_to_entity(l, lv_get(l->entity_to_attack)) > 256.0
                     && l->enderman_teleport_delay++ >= 30
                     && teleport_to_entity(l, lv_get(l->entity_to_attack)))
            {
                l->enderman_teleport_delay = 0;
            }
        }
        else
        {
            enderman_set_screaming(l, 0);
            l->enderman_teleport_delay = 0;
        }
    }

    /* super.onLivingUpdate: EntityMob's body (updateArmSwingProgress has
     * nothing to do, the enderman never swings) */
    float var1 = get_brightness(l);

    if (var1 > 0.5F)
    {
        l->entity_age += 2;
    }

    living_default_on_living_update(l, det);
    hostile_loot_update(l);
}

/* --------------------------------------------------------- the constructor */

void enderman_construct(struct living *l, det_state *det)
{
    /* EntityMob's constructor: the XP a player kill drops */
    l->experience_value = 5;
    (void)det;

    /* EntityLivingBase.applyEntityAttributes registers maxHealth,
     * knockbackResistance and movementSpeed -- and leaves movementSpeed at 0.1
     * because isAIEnabled is false -- EntityLiving's registers followRange at 16
     * and EntityMob's registers attackDamage, then EntityEnderman's own sets
     * three bases */
    attrs_set_base(&l->attrs.a[ATTR_FOLLOW_RANGE], 16.0);
    attrs_set_base(&l->attrs.a[ATTR_MAX_HEALTH], 40.0);
    attrs_set_base(&l->attrs.a[ATTR_MOVEMENT_SPEED], 0.30000001192092896);
    attrs_set_base(&l->attrs.a[ATTR_ATTACK_DAMAGE], 7.0);

    entity_set_size(&l->e, 0.6F, 2.9F);
    l->e.step_height = 1.0F;

    /* PathNavigate's own defaults: canPassOpenWoodenDoors is true, avoidsWater,
     * canPassClosedWoodenDoors and canSwim are false. The enderman never adds
     * EntityAISwimming, so canSwim stays false. */
    l->nav.can_pass_open_doors = 1;

    /* EntityLivingBase's constructor set the health before EntityEnderman's
     * applyEntityAttributes raised the maximum */
    living_set_health(l, living_max_health(l));
}

/* --------------------------------------------------------------- the NBT */

void enderman_write_kind_nbt(struct living *l, struct nbtw *w)
{
    nbtw_short(w, "carried", (short)l->enderman_carried_block);
    nbtw_short(w, "carriedData", (short)l->enderman_carrying_data);
}

/* -------------------------------------------------------- the drop table */

void enderman_drop_few_items(struct living *l, int hit_by_player, int looting, det_state *det)
{
    (void)hit_by_player;
    (void)det;
    int var4 = det_rng_int_n(&l->rand, 2 + looting);

    for (int var5 = 0; var5 < var4; ++var5)
    {
        an_spawn_item(l->an, l->an->n, l->e.pos_x, l->e.pos_y, l->e.pos_z, ITEM_ENDER_PEARL, 0, 1, 10);
    }
}

/* --------------------------------------------------------------- the record */

/* EntityEnderman's block: 44 bytes at the tail of the hostile record. Every
 * other kind (and a NULL living) writes zeros. */
void enderman_write_extra(struct living *l, unsigned char *rec, int *p)
{
    if (l == NULL || l->kind != HK_ENDERMAN)
    {
        for (int i = 0; i < 11; ++i)
        {
            rec[(*p)++] = 0;
            rec[(*p)++] = 0;
            rec[(*p)++] = 0;
            rec[(*p)++] = 0;
        }

        return;
    }

    int32_t vals[9];
    vals[0] = l->enderman_carried_block;
    vals[1] = l->enderman_carrying_data;
    vals[2] = l->enderman_stare_timer;
    vals[3] = l->enderman_teleport_delay;
    vals[4] = (l->enderman_is_aggressive ? 1 : 0) | (l->enderman_screaming ? 2 : 0);
    vals[5] = lv_get(l->enderman_last_entity_to_attack) ? lv_get(l->enderman_last_entity_to_attack)->spawn_index : -1;
    vals[6] = l->creature_path ? 1 : 0;
    vals[7] = l->creature_path ? path_at(l->creature_path)->index : 0;
    vals[8] = l->creature_path ? path_at(l->creature_path)->length : 0;

    for (int i = 0; i < 9; ++i)
    {
        rec[(*p)++] = (unsigned char)(vals[i] & 0xff);
        rec[(*p)++] = (unsigned char)((vals[i] >> 8) & 0xff);
        rec[(*p)++] = (unsigned char)((vals[i] >> 16) & 0xff);
        rec[(*p)++] = (unsigned char)((vals[i] >> 24) & 0xff);
    }

    uint64_t hash = 0xcbf29ce484222325ULL;

    if (l->creature_path != 0)
    {
        for (int i = 0; i < path_at(l->creature_path)->length; ++i)
        {
            for (int k = 0; k < 3; ++k)
            {
                int v = path_at(l->creature_path)->pts[i][k];

                for (int b = 0; b < 4; ++b) hash = (hash ^ (uint64_t)((v >> (8 * b)) & 255)) * 0x100000001b3ULL;
            }
        }
    }

    for (int i = 0; i < 8; ++i) rec[(*p)++] = (unsigned char)((hash >> (8 * i)) & 0xff);
}