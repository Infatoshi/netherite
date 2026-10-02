#include "hostiles_blaze.h"
#include "env.h"
#include "combatench.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "ai.h"
#include "blockcb.h"
#include "det.h"
#include "hostiles.h"
#include "item_entity.h"
#include "items.h"
#include "jmath.h"
#include "living.h"
#include "projectile.h"
#include "raytrace.h"
#include "slimes.h"
#include "smath.h"
#include "world.h"

/* EntityCreature.field_110181_i: "Fleeing speed bonus", 2.0, operation 2. */
static const struct attr_mod fleeing_speed_bonus_modifier = {
    .name = MODN_FLEEING_SPEED_BONUS,
    .amount = 2.0,
    .operation = 2,
    .uuid_msb = (int64_t)0xE199AD21BA8A4C53ULL,
    .uuid_lsb = (int64_t)0x8D136182D5C69D3AULL,
    .saved = 0
};

static double sqrt_double(double v)
{
    return (double)(float)sqrt(v);
}

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

static float get_distance_to_entity(struct living *l, struct living *other)
{
    float dx = (float)(l->e.pos_x - other->e.pos_x);
    float dy = (float)(l->e.pos_y - other->e.pos_y);
    float dz = (float)(l->e.pos_z - other->e.pos_z);
    return sqrt_float(dx * dx + dy * dy + dz * dz);
}

static double get_distance_sq_to_entity(struct living *l, struct living *other)
{
    double dx = l->e.pos_x - other->e.pos_x;
    double dy = l->e.pos_y - other->e.pos_y;
    double dz = l->e.pos_z - other->e.pos_z;
    return dx * dx + dy * dy + dz * dz;
}

static int get_vertical_face_speed(struct living *l)
{
    (void)l;
    return 40;
}

static int can_entity_be_seen(struct living *l, struct living *other)
{
    struct rt_mop hit;
    return raytrace_trace(l->world,
                          l->e.pos_x, l->e.pos_y + (double)living_eye_height(l), l->e.pos_z,
                          other->e.pos_x, other->e.pos_y + (double)living_eye_height(other), other->e.pos_z,
                          &hit) == 0;
}

struct vec3 {
    double x, y, z;
};

static struct vec3 path_position(struct living *l, const struct path_ent *path)
{
    double off = (double)((int)(l->e.width + 1.0F)) * 0.5;
    return (struct vec3){
        (double)path->pts[path->index][0] + off,
        (double)path->pts[path->index][1],
        (double)path->pts[path->index][2] + off
    };
}

static double vec_square_dist_to(struct vec3 a, double x, double y, double z)
{
    double dx = x - a.x;
    double dy = y - a.y;
    double dz = z - a.z;
    return dx * dx + dy * dy + dz * dz;
}

static void blaze_clear_path(struct living *l)
{
    path_drop(l->creature_path);
    l->creature_path = 0;
}

static pathref blaze_path_to_entity(struct living *l, struct living *target, float max_dist)
{
    return ai_creature_path_to_entity(l, target, max_dist, 1, 0, 0, 1);
}

static pathref blaze_path_to_xyz(struct living *l, int x, int y, int z, float max_dist)
{
    return ai_creature_path_to_xyz(l, x, y, z, max_dist, 1, 0, 0, 1);
}

static void blaze_update_wander_path(struct living *l)
{
    int var1 = 0;
    int var2 = -1;
    int var3 = -1;
    int var4 = -1;
    float var5 = -99999.0f;

    for (int var6 = 0; var6 < 10; ++var6)
    {
        int var7 = mh_floor(l->e.pos_x + (double)det_rng_int_n(&l->rand, 13) - 6.0);
        int var8 = mh_floor(l->e.pos_y + (double)det_rng_int_n(&l->rand, 7) - 3.0);
        int var9 = mh_floor(l->e.pos_z + (double)det_rng_int_n(&l->rand, 13) - 6.0);

        float var10 = 0.5f - living_light_brightness(l->world, l->an ? l->an->skylight : 0, var7, var8, var9);

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
        blaze_clear_path(l);
        l->creature_path = blaze_path_to_xyz(l, var2, var3, var4, 10.0f);
    }
}

static struct living *blaze_find_player_to_attack(struct living *l)
{
    struct living *var1 = an_closest_vulnerable_player(l->an, l->e.pos_x, l->e.pos_y, l->e.pos_z, 16.0);
    return (var1 != NULL && can_entity_be_seen(l, var1)) ? var1 : NULL;
}

/* EntityMob.attackEntityAsMob. */
static int attack_entity_as_mob(struct living *l, struct living *target, det_state *det)
{
    return mob_attack_entity_as_mob(l, target, det);
}

static void blaze_spawn_small_fireball(struct living *l, double accel_x, double accel_y, double accel_z)
{
    struct an_world *an = l->an;
    if (an == NULL) return;
    struct ie_world *iew = &an->iew;
    ie_ent *fb = ie_ent_alloc();
    entity_init(&fb->e, iew->w);
    fb->e.self = fb;
    fb->e.attack_from = NULL;
    fb->e.first_update = 1;
    fb->first_update = 1;
    fb->entity_id = det_next_entity_id_role(iew->det, iew->role);
    fb->rand = det_new_random_role(iew->det, iew->role);
    int64_t msb, lsb;
    det_uuid_role(iew->det, iew->role, &msb, &lsb);
    fb->uuid_msb = msb;
    fb->uuid_lsb = lsb;

    fb->kind = IE_SMALL_FIREBALL;
    fb->shooter = lv_ref(l);
    fb->shooter_is_player = 0;

    double g_fb1 = det_rng_gaussian(&fb->rand);
    double g_fb2 = det_rng_gaussian(&fb->rand);
    double g_fb3 = det_rng_gaussian(&fb->rand);
    double ax = accel_x + g_fb1 * 0.4;
    double ay = accel_y + g_fb2 * 0.4;
    double az = accel_z + g_fb3 * 0.4;
    double len = sqrt_double(ax * ax + ay * ay + az * az);
    fb->accel_x = ax / len * 0.1;
    fb->accel_y = ay / len * 0.1;
    fb->accel_z = az / len * 0.1;

    fb->prev_yaw = fb->rotation_yaw = l->rotation_yaw;
    fb->prev_pitch = fb->rotation_pitch = l->rotation_pitch;
    fb->e.motion_x = fb->e.motion_y = fb->e.motion_z = 0.0;

    entity_set_size(&fb->e, 0.3125f, 0.3125f);

    double spawn_y = l->e.pos_y + (double)(l->e.height / 2.0f) + 0.5;
    entity_set_position(&fb->e, l->e.pos_x, spawn_y, l->e.pos_z);
    /* EntityFireball's constructor placed the 1.0-wide box at the blaze
     * (setLocationAndAngles), EntitySmallFireball's setSize shrank it from
     * its min corner, and attackEntity sets posY alone: the first update's
     * water and lava tests and its entity search read that box until the
     * move's setPosition */
    fb->e.bounding_box.min_x = l->e.pos_x - 0.5;
    fb->e.bounding_box.min_y = l->e.pos_y;
    fb->e.bounding_box.min_z = l->e.pos_z - 0.5;
    fb->e.bounding_box.max_x = fb->e.bounding_box.min_x + (double)0.3125F;
    fb->e.bounding_box.max_y = fb->e.bounding_box.min_y + (double)0.3125F;
    fb->e.bounding_box.max_z = fb->e.bounding_box.min_z + (double)0.3125F;

    fb->tile_x = fb->tile_y = fb->tile_z = -1;
    fb->in_tile = -1;

    ie_list_push(iew, fb);
    ie_added_to_world(iew, fb);

    fb->dimension = an->dimension;
    fb->spawn_index = an->n;

    struct an_ent *en = an_ent_alloc();
    en->used = 1;
    en->is_living = 0;
    en->spawn_index = an->n;
    en->livh = 0;
    en->ieh = ie_ref(fb);
    an_list_push(an, en);
    an_chunk_add(an, en, mh_floor(fb->e.pos_x / 16.0), mh_floor(fb->e.pos_y / 16.0),
                 mh_floor(fb->e.pos_z / 16.0));
}

static void blaze_attack_entity(struct living *l, struct living *target, float dist, det_state *det)
{
    if (l->attack_time <= 0 && dist < 2.0f &&
        target->e.bounding_box.max_y > l->e.bounding_box.min_y &&
        target->e.bounding_box.min_y < l->e.bounding_box.max_y)
    {
        l->attack_time = 20;
        attack_entity_as_mob(l, target, det);
    }
    else if (dist < 30.0f)
    {
        double var3 = target->e.pos_x - l->e.pos_x;
        double var5 = target->e.bounding_box.min_y + (double)(target->e.height / 2.0f) - (l->e.pos_y + (double)(l->e.height / 2.0f));
        double var7 = target->e.pos_z - l->e.pos_z;

        if (l->attack_time == 0)
        {
            ++l->blaze_attack_step;
            if (l->blaze_attack_step == 1)
            {
                l->attack_time = 60;
                blaze_set_on_fire(l, 1);
            }
            else if (l->blaze_attack_step <= 4)
            {
                l->attack_time = 6;
            }
            else
            {
                l->attack_time = 100;
                l->blaze_attack_step = 0;
                blaze_set_on_fire(l, 0);
            }

            if (l->blaze_attack_step > 1)
            {
                float var9 = sqrt_float(dist) * 0.5f;
                env_aux_sfx(l->world, 1009, (int)l->e.pos_x, (int)l->e.pos_y, (int)l->e.pos_z, 0);
                double g1 = det_rng_gaussian(&l->rand);
                double accel_x = var3 + g1 * (double)var9;
                double accel_y = var5;
                double g2 = det_rng_gaussian(&l->rand);
                double accel_z = var7 + g2 * (double)var9;

                blaze_spawn_small_fireball(l, accel_x, accel_y, accel_z);
            }
        }

        l->rotation_yaw = (float)(fd_atan2(var7, var3) * 180.0 / 3.141592653589793) - 90.0f;
        l->has_attacked = 1;
    }
}

void blaze_construct(struct living *l, det_state *det)
{
    (void)det;
    attrs_set_base(&l->attrs.a[ATTR_MOVEMENT_SPEED], 0.10000000149011612);
    attrs_set_base(&l->attrs.a[ATTR_FOLLOW_RANGE], 16.0);
    l->attrs.a[ATTR_ATTACK_DAMAGE].registered = 1;
    attrs_set_base(&l->attrs.a[ATTR_ATTACK_DAMAGE], 6.0);
    l->nav.can_pass_open_doors = 1;

    entity_set_size(&l->e, 0.6f, 1.8f);
    l->immune_to_fire = 1;
    l->experience_value = 10;
    l->kind_fall = blaze_fall;
    l->blaze_height_offset = 0.5f;
    l->blaze_height_offset_update_time = 0;
    l->blaze_attack_step = 0;
    l->data_watcher_16 = 0;
}

void blaze_set_on_fire(struct living *l, int on)
{
    if (on) l->data_watcher_16 |= 1;
    else l->data_watcher_16 &= ~1;
}

int blaze_is_on_fire(const struct living *l)
{
    return (l->data_watcher_16 & 1) != 0;
}

void blaze_fall(struct living *l, float dist)
{
    (void)l;
    (void)dist;
}

void blaze_drop_few_items(struct living *l, int hit_by_player, int looting, det_state *det)
{
    (void)det;
    if (hit_by_player)
    {
        int count = det_rng_int_n(&l->rand, 2 + looting);
        for (int i = 0; i < count; ++i)
        {
            if (l->an) an_spawn_item(l->an, l->an->n, l->e.pos_x, l->e.pos_y, l->e.pos_z, 369, 0, 1, 10);
        }
    }
}

void blaze_on_living_update(struct living *l, det_state *det)
{
    if (living_is_wet(l))
    {
        living_attack_entity_from(l, DMG_DROWN, 1.0f, det);
    }

    --l->blaze_height_offset_update_time;
    if (l->blaze_height_offset_update_time <= 0)
    {
        l->blaze_height_offset_update_time = 100;
        l->blaze_height_offset = 0.5f + (float)det_rng_gaussian(&l->rand) * 3.0f;
    }

    struct living *target = lv_get(l->entity_to_attack);
    if (target != NULL && (target->e.pos_y + (double)living_eye_height(target) >
                           l->e.pos_y + (double)living_eye_height(l) + (double)l->blaze_height_offset))
    {
        l->e.motion_y += (0.30000001192092896 - l->e.motion_y) * 0.30000001192092896;
    }

    if (det_rng_int_n(&l->rand, 24) == 0)
    {
        (void)det_rng_float(&l->rand);
        (void)det_rng_float(&l->rand);
    }

    if (!l->e.on_ground && l->e.motion_y < 0.0)
    {
        l->e.motion_y *= 0.6;
    }

    for (int var1 = 0; var1 < 2; ++var1)
    {
        (void)det_rng_double(&l->rand);
        (void)det_rng_double(&l->rand);
        (void)det_rng_double(&l->rand);
    }

    l->entity_age += 2;

    living_default_on_living_update(l, det);
    hostile_loot_update(l);
}

void blaze_update_entity_action_state(struct living *l, det_state *det)
{
    if (l->fleeing_tick > 0 && --l->fleeing_tick == 0)
    {
        attrs_remove(&l->attrs.a[ATTR_MOVEMENT_SPEED], &fleeing_speed_bonus_modifier);
    }

    l->has_attacked = 0;
    float max_dist = 16.0f;

    if (lv_get(l->entity_to_attack) == NULL && !l->entity_to_attack_gone)
    {
        l->entity_to_attack = lv_ref(blaze_find_player_to_attack(l));
        if (lv_get(l->entity_to_attack) != NULL)
        {
            blaze_clear_path(l);
            l->creature_path = blaze_path_to_entity(l, lv_get(l->entity_to_attack), max_dist);
        }
    }
    else if (living_is_alive(lv_get(l->entity_to_attack)))
    {
        float d = get_distance_to_entity(l, lv_get(l->entity_to_attack));
        if (can_entity_be_seen(l, lv_get(l->entity_to_attack)))
        {
            blaze_attack_entity(l, lv_get(l->entity_to_attack), d, det);
        }
    }
    else
    {
        l->entity_to_attack = 0;
    }
    /* a dead non-living entityToAttack (entity_to_attack_gone) took the
     * else branch */
    l->entity_to_attack_gone = 0;

    if (!l->has_attacked && lv_get(l->entity_to_attack) != NULL &&
        (l->creature_path == 0 || det_rng_int_n(&l->rand, 20) == 0))
    {
        pathref p = blaze_path_to_entity(l, lv_get(l->entity_to_attack), max_dist);
        blaze_clear_path(l);
        l->creature_path = p;
    }
    else if (!l->has_attacked &&
             ((l->creature_path == 0 && det_rng_int_n(&l->rand, 180) == 0) ||
              det_rng_int_n(&l->rand, 120) == 0 || l->fleeing_tick > 0) &&
             l->entity_age < 100)
    {
        blaze_update_wander_path(l);
    }

    int var22 = mh_floor(l->e.bounding_box.min_y + 0.5);
    int var3 = living_is_in_water(l);
    int var4 = living_handle_lava_movement(l);
    l->rotation_pitch = 0.0f;

    if (l->creature_path != 0 && det_rng_int_n(&l->rand, 100) != 0)
    {
        double var6 = (double)(l->e.width * 2.0f);
        int have = 1;
        struct vec3 var5 = path_position(l, path_at(l->creature_path));

        while (have && vec_square_dist_to(var5, l->e.pos_x, var5.y, l->e.pos_z) < var6 * var6)
        {
            ++path_at(l->creature_path)->index;
            if (path_at(l->creature_path)->index >= path_at(l->creature_path)->length)
            {
                have = 0;
                blaze_clear_path(l);
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
            float var14 = (float)(fd_atan2(var10, var8) * 180.0 / 3.141592653589793) - 90.0f;
            float var15 = wrap_angle_float(var14 - l->rotation_yaw);
            l->move_forward = (float)attrs_value(&l->attrs.a[ATTR_MOVEMENT_SPEED]);

            if (var15 > 30.0f) var15 = 30.0f;
            if (var15 < -30.0f) var15 = -30.0f;

            l->rotation_yaw += var15;

            if (l->has_attacked && lv_get(l->entity_to_attack) != NULL)
            {
                double var16 = lv_get(l->entity_to_attack)->e.pos_x - l->e.pos_x;
                double var18 = lv_get(l->entity_to_attack)->e.pos_z - l->e.pos_z;
                float var20 = l->rotation_yaw;
                l->rotation_yaw = (float)(fd_atan2(var18, var16) * 180.0 / 3.141592653589793) - 90.0f;
                var15 = (var20 - l->rotation_yaw + 90.0f) * 3.1415927f / 180.0f;
                l->move_strafing = -mh_sin(var15) * l->move_forward * 1.0f;
                l->move_forward = mh_cos(var15) * l->move_forward * 1.0f;
            }

            if (var12 > 0.0) l->is_jumping = 1;
        }

        if (lv_get(l->entity_to_attack) != NULL)
        {
            living_face_entity(l, lv_get(l->entity_to_attack), 30.0f, 30.0f);
        }

        if (l->e.is_collided_horizontally && l->creature_path == 0) l->is_jumping = 1;

        if (det_rng_float(&l->rand) < 0.8f && (var3 || var4)) l->is_jumping = 1;
    }
    else
    {
        ++l->entity_age;
        l->move_strafing = 0.0f;
        l->move_forward = 0.0f;
        living_despawn(l);
        float var1 = 8.0f;

        if (det_rng_float(&l->rand) < 0.02f)
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
                l->random_yaw_velocity = (det_rng_float(&l->rand) - 0.5f) * 20.0f;
            }
        }

        if (lv_get(l->current_target) != NULL)
        {
            living_face_entity(l, lv_get(l->current_target), 10.0f, (float)get_vertical_face_speed(l));
            if ((l->num_ticks_to_chase_target-- <= 0) || lv_get(l->current_target)->is_dead ||
                get_distance_sq_to_entity(l, lv_get(l->current_target)) > (double)(var1 * var1))
            {
                l->current_target = 0;
            }
        }
        else
        {
            if (det_rng_float(&l->rand) < 0.05f)
            {
                l->random_yaw_velocity = (det_rng_float(&l->rand) - 0.5f) * 20.0f;
            }
            l->rotation_yaw += l->random_yaw_velocity;
            l->rotation_pitch = l->default_pitch;
        }

        int var4_in_water = living_is_in_water(l);
        int var3_in_lava = living_handle_lava_movement(l);
        if (var4_in_water || var3_in_lava)
        {
            l->is_jumping = det_rng_float(&l->rand) < 0.8f;
        }

        blaze_clear_path(l);
    }
}
