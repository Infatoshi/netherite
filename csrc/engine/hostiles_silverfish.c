#include "hostiles_silverfish.h"
#include "env.h"
#include "combatench.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "ai.h"
#include "blocks.h"
#include "hostiles.h"
#include "jmath.h"
#include "raytrace.h"
#include "smath.h"
#include "slimes.h"
#include "world.h"

/* EntitySilverfish uses EntityCreature's old AI path. Its constructor adds no
 * EntityAITasks; hiding and summoning run after the inherited action state. */

static void silverfish_spawn_particles(struct living *l)
{
    for (int i = 0; i < 20; ++i)
    {
        (void)det_rng_gaussian(&l->rand);
        (void)det_rng_gaussian(&l->rand);
        (void)det_rng_gaussian(&l->rand);
        (void)det_rng_float(&l->rand);
        (void)det_rng_float(&l->rand);
        (void)det_rng_float(&l->rand);
    }
}

static void silverfish_spawn_from_egg(struct living *l, int x, int y, int z)
{
    struct an_world *an = l->an;
    if (an == NULL) return;
    struct living *child = living_alloc();
    /* the constructor's draws on the world's own streams (a replay's
     * server), as EntityAIMate.spawnBaby's child takes them */
    if (an->constructor_hook) an->constructor_hook(an, 1, an->constructor_ctx);
    living_init(child, an->w, HK_SILVERFISH, an->det);
    child->an = an;
    child->dimension = an->dimension;
    silverfish_construct(child, an->det);
    if (an->constructor_hook) an->constructor_hook(an, 0, an->constructor_ctx);
    living_set_location_and_angles(child, x + 0.5, y, z + 0.5, 0.0F, 0.0F);
    struct an_ent *en = an_ent_alloc();
    en->used = 1;
    en->is_living = 1;
    en->spawn_index = an->next_spawn_index++;
    en->livh = lv_ref(child);
    child->spawn_index = en->spawn_index;
    an_list_push(an, en);
    an_add(an, en);
    /* spawnEntityInWorld's EntityTracker.trackEntity */
    if (an->track_spawn) an->track_spawn(an, child, an->constructor_ctx);
    silverfish_spawn_particles(child);
}

static const struct attr_mod fleeing_speed_bonus_modifier = {
    .name = MODN_FLEEING_SPEED_BONUS, .amount = 2.0, .operation = 2,
    .uuid_msb = (int64_t)0xE199AD21BA8A4C53ULL,
    .uuid_lsb = (int64_t)0x8D136182D5C69D3AULL, .saved = 0
};

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

static int get_vertical_face_speed(struct living *l)
{
    (void)l;
    return 40;
}

static struct vec3 path_position(struct living *l, const struct path_ent *path)
{
    double off = (double)((int)(l->e.width + 1.0F)) * 0.5;
    return (struct vec3){
        (double)path->pts[path->index][0] + off,
        (double)path->pts[path->index][1],
        (double)path->pts[path->index][2] + off
    };
}

static void silverfish_clear_path(struct living *l)
{
    path_drop(l->creature_path);
    l->creature_path = 0;
}

/* World.getPathEntityToEntity(this, target, 16.0F, true, false, false, true):
 * the four booleans are PathFinder's (canPassOpenWoodenDoors, the movement
 * block, the water pathing, canEntityDrown) in position. */
static pathref silverfish_path_to_entity(struct living *l, struct living *target, float max_dist)
{
    return ai_creature_path_to_entity(l, target, max_dist, 1, 0, 0, 1);
}

static pathref silverfish_path_to_xyz(struct living *l, int x, int y, int z, float max_dist)
{
    return ai_creature_path_to_xyz(l, x, y, z, max_dist, 1, 0, 0, 1);
}

/* EntityCreature.updateWanderPath. */
static void silverfish_update_wander_path(struct living *l)
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

        float var10 = (world_get_block(l->world, var7, var8 - 1, var9) & 4095) == 1
            ? 10.0F : 0.5F - living_light_brightness(l->world, l->an ? l->an->skylight : 0, var7, var8, var9);

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
        silverfish_clear_path(l);
        l->creature_path = silverfish_path_to_xyz(l, var2, var3, var4, 10.0F);
    }
}

/* World.getClosestVulnerablePlayerToEntity(this, range): the probe's player,
 * when the run put it in World.playerEntities. */
static struct living *closest_vulnerable_player(struct living *l, double range)
{
    if (l->an == NULL) return NULL;

    return an_closest_vulnerable_player(l->an, l->e.pos_x, l->e.pos_y, l->e.pos_z, range);
}

static struct living *find_player_to_attack(struct living *l)
{
    return closest_vulnerable_player(l, 8.0);
}

/* EntityMob.attackEntityAsMob. */
static int attack_entity_as_mob(struct living *l, struct living *target, det_state *det)
{
    return mob_attack_entity_as_mob(l, target, det);
}

/* EntityMob.attackEntity. */
static void attack_entity(struct living *l, struct living *target, float dist, det_state *det)
{
    if (l->attack_time <= 0 && dist < 1.2F
        && target->e.bounding_box.max_y > l->e.bounding_box.min_y
        && target->e.bounding_box.min_y < l->e.bounding_box.max_y)
    {
        l->attack_time = 20;
        attack_entity_as_mob(l, target, det);
    }
}

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
         * no vulnerable gate, only Entity.isDead */
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
void silverfish_update_entity_action_state(struct living *l, det_state *det)
{
    if (l->fleeing_tick > 0 && --l->fleeing_tick == 0)
    {
        attrs_remove(&l->attrs.a[ATTR_MOVEMENT_SPEED], &fleeing_speed_bonus_modifier);
    }

    l->has_attacked = 0;   /* EntityCreature.isMovementCeased */
    float var21 = 16.0F;

    if (lv_get(l->entity_to_attack) == NULL && !l->entity_to_attack_gone)
    {
        l->entity_to_attack = lv_ref(find_player_to_attack(l));

        if (lv_get(l->entity_to_attack) != NULL)
        {
            silverfish_clear_path(l);
            l->creature_path = silverfish_path_to_entity(l, lv_get(l->entity_to_attack), var21);
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
    /* a dead non-living entityToAttack (entity_to_attack_gone) took the
     * else branch */
    l->entity_to_attack_gone = 0;

    /* the EntityPlayerMP creative check cannot match the probe's plain player */

    if (!l->has_attacked && lv_get(l->entity_to_attack) != NULL
        && (l->creature_path == 0 || det_rng_int_n(&l->rand, 20) == 0))
    {
        pathref p = silverfish_path_to_entity(l, lv_get(l->entity_to_attack), var21);
        silverfish_clear_path(l);
        l->creature_path = p;
    }
    else if (!l->has_attacked
             && ((l->creature_path == 0 && det_rng_int_n(&l->rand, 180) == 0)
                 || det_rng_int_n(&l->rand, 120) == 0 || l->fleeing_tick > 0)
             && l->entity_age < 100)
    {
        silverfish_update_wander_path(l);
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
                silverfish_clear_path(l);
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
        silverfish_clear_path(l);
    }

    if (l->silverfish_ally_summon_cooldown > 0 && --l->silverfish_ally_summon_cooldown == 0)
    {
        int px = mh_floor(l->e.pos_x);
        int py = mh_floor(l->e.pos_y);
        int pz = mh_floor(l->e.pos_z);
        int stop = 0;
        for (int dy = 0; !stop && dy <= 5 && dy >= -5; dy = dy <= 0 ? 1 - dy : -dy)
            for (int dx = 0; !stop && dx <= 10 && dx >= -10; dx = dx <= 0 ? 1 - dx : -dx)
                for (int dz = 0; !stop && dz <= 10 && dz >= -10; dz = dz <= 0 ? 1 - dz : -dz)
                {
                    int x = px + dx, y = py + dy, z = pz + dz;
                    if ((world_get_block(l->world, x, y, z) & 4095) != 97) continue;
                    /* World.func_147480_a: its 2001 first */
                    env_aux_sfx(l->world, 2001, x, y, z, 97 + (world_get_meta(l->world, x, y, z) << 12));
                    world_set_block(l->world, x, y, z, 0, 0, 3);
                    silverfish_spawn_from_egg(l, x, y, z);
                    if (det_rng_int_n(&l->rand, 2) != 0) stop = 1;
                }
    }

    if (lv_get(l->entity_to_attack) == NULL && l->creature_path == 0)
    {
        int x = mh_floor(l->e.pos_x);
        int y = mh_floor(l->e.pos_y + 0.5);
        int z = mh_floor(l->e.pos_z);
        static const int sx[6] = {0, 0, 0, 0, -1, 1};
        static const int sy[6] = {-1, 1, 0, 0, 0, 0};
        static const int sz[6] = {0, 0, -1, 1, 0, 0};
        int side = det_rng_int_n(&l->rand, 6);
        x += sx[side]; y += sy[side]; z += sz[side];
        int id = world_get_block(l->world, x, y, z) & 4095;
        int meta = world_get_meta(l->world, x, y, z);
        if (id == 1 || id == 4 || id == 98)
        {
            int egg_meta = 0;
            if (id == 4 && meta == 0) egg_meta = 1;
            if (id == 98) egg_meta = meta == 0 ? 2 : meta <= 3 ? meta + 2 : 0;
            world_set_block(l->world, x, y, z, 97, egg_meta, 3);
            silverfish_spawn_particles(l);
            l->is_dead = 1;
        }
        else silverfish_update_wander_path(l);
    }
    else if (lv_get(l->entity_to_attack) != NULL && l->creature_path == 0)
    {
        l->entity_to_attack = 0;
    }
}

void silverfish_hurt(struct living *l, int source, struct living *attacker)
{
    if (l->silverfish_ally_summon_cooldown <= 0 && (attacker != NULL || source == DMG_MAGIC))
        l->silverfish_ally_summon_cooldown = 20;
}

void silverfish_on_living_update(struct living *l, det_state *det)
{
    if (get_brightness(l) > 0.5F) l->entity_age += 2;
    living_default_on_living_update(l, det);
    hostile_loot_update(l);
}

void silverfish_construct(struct living *l, det_state *det)
{
    /* EntityMob's constructor: the XP a player kill drops */
    l->experience_value = 5;
    (void)det;
    attrs_set_base(&l->attrs.a[ATTR_FOLLOW_RANGE], 16.0);
    attrs_set_base(&l->attrs.a[ATTR_MAX_HEALTH], 8.0);
    attrs_set_base(&l->attrs.a[ATTR_MOVEMENT_SPEED], 0.6000000238418579);
    attrs_set_base(&l->attrs.a[ATTR_ATTACK_DAMAGE], 1.0);
    entity_set_size(&l->e, 0.3F, 0.7F);
    l->e.can_trigger_walking = 0;
    l->nav.can_pass_open_doors = 1;
    /* EntityMob's constructor: the XP a player kill drops */
    l->experience_value = 5;
    living_set_health(l, living_max_health(l));
}

void silverfish_write_extra(struct living *l, unsigned char *rec, int *p)
{
    for (int i = 0; i < 6; ++i)
        for (int b = 0; b < 4; ++b) rec[(*p)++] = 0;

    int vals[3] = {
        l->creature_path != 0,
        l->creature_path ? path_at(l->creature_path)->index : 0,
        l->creature_path ? path_at(l->creature_path)->length : 0
    };
    for (int i = 0; i < 3; ++i)
        for (int b = 0; b < 4; ++b) rec[(*p)++] = (unsigned char)((vals[i] >> (8 * b)) & 255);

    uint64_t hash = 0xcbf29ce484222325ULL;
    if (l->creature_path)
        for (int i = 0; i < path_at(l->creature_path)->length; ++i)
            for (int k = 0; k < 3; ++k)
                for (int b = 0; b < 4; ++b)
                    hash = (hash ^ (uint64_t)((path_at(l->creature_path)->pts[i][k] >> (8 * b)) & 255)) * 0x100000001b3ULL;
    for (int b = 0; b < 8; ++b) rec[(*p)++] = (unsigned char)((hash >> (8 * b)) & 255);
}
