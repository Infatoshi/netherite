#include "nbtw.h"
#include "hostiles_pigman.h"
#include "combatench.h"
#include "envstack.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "ai.h"
#include "hostiles.h"
#include "jmath.h"
#include "raytrace.h"
#include "slimes.h"
#include "smath.h"
#include "world.h"

struct vec3 { double x, y, z; };

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
    return (float)sqrt((double)(dx * dx + dy * dy + dz * dz));
}

static double get_distance_sq_to_entity(struct living *l, struct living *other)
{
    double dx = l->e.pos_x - other->e.pos_x;
    double dy = l->e.pos_y - other->e.pos_y;
    double dz = l->e.pos_z - other->e.pos_z;
    return dx * dx + dy * dy + dz * dz;
}

static double vec_square_dist_to(struct vec3 v, double x, double y, double z)
{
    double dx = x - v.x, dy = y - v.y, dz = z - v.z;
    return dx * dx + dy * dy + dz * dz;
}

static int can_entity_be_seen(struct living *l, struct living *other)
{
    struct rt_mop hit;
    return raytrace_trace(l->world,
                          l->e.pos_x, l->e.pos_y + (double)living_eye_height(l), l->e.pos_z,
                          other->e.pos_x, other->e.pos_y + (double)living_eye_height(other), other->e.pos_z,
                          &hit) == 0;
}

static struct vec3 path_position(struct living *l, const struct path_ent *path)
{
    double off = (double)((int)(l->e.width + 1.0F)) * 0.5;
    return (struct vec3){(double)path->pts[path->index][0] + off,
                         (double)path->pts[path->index][1],
                         (double)path->pts[path->index][2] + off};
}

static void pigman_clear_path(struct living *l)
{
    path_drop(l->creature_path);
    l->creature_path = 0;
}

static pathref pigman_path_to_entity(struct living *l, struct living *target, float max_dist)
{
    return ai_creature_path_to_entity(l, target, max_dist, 1, 0, 0, 1);
}

static pathref pigman_path_to_xyz(struct living *l, int x, int y, int z, float max_dist)
{
    return ai_creature_path_to_xyz(l, x, y, z, max_dist, 1, 0, 0, 1);
}

static void pigman_update_wander_path(struct living *l)
{
    int var1 = 0, var2 = -1, var3 = -1, var4 = -1;
    float var5 = -99999.0F;
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
            var1 = 1;
        }
    }
    if (var1)
    {
        pigman_clear_path(l);
        l->creature_path = pigman_path_to_xyz(l, var2, var3, var4, 10.0F);
    }
}

static struct living *closest_vulnerable_player(struct living *l, double range)
{
    return l->an ? an_closest_vulnerable_player(l->an, l->e.pos_x, l->e.pos_y, l->e.pos_z, range) : NULL;
}

static struct living *find_player_to_attack(struct living *l)
{
    if (l->pigman_anger_level == 0) return NULL;
    struct living *target = closest_vulnerable_player(l, 16.0);
    return target != NULL && can_entity_be_seen(l, target) ? target : NULL;
}

/* EntityZombie.attackEntityAsMob: EntityMob's, then the burning tail a
 * pigman (immune to fire, so never burning) never reaches. */
static int attack_entity_as_mob(struct living *l, struct living *target, det_state *det)
{
    return mob_attack_entity_as_mob(l, target, det);
}

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

static const struct attr_mod attacking_speed_boost = {
    .name = MODN_ATTACKING_SPEED_BOOST,
    .amount = 0.45,
    .operation = 0,
    .uuid_msb = (int64_t)0x49455A497EC545BAULL,
    .uuid_lsb = (int64_t)0xB8863B90B23A1718ULL,
    .saved = 0
};

static const struct attr_mod fleeing_speed_bonus = {
    .name = MODN_FLEEING_SPEED_BONUS,
    .amount = 2.0,
    .operation = 2,
    .uuid_msb = (int64_t)0xE199AD21BA8A4C53ULL,
    .uuid_lsb = (int64_t)0x8D136182D5C69D3AULL,
    .saved = 0
};

static void put32(unsigned char *rec, int *p, int v)
{
    for (int k = 0; k < 4; ++k) rec[(*p)++] = (unsigned char)((unsigned)v >> (k * 8));
}

void pigman_construct(struct living *l, det_state *det)
{
    zombie_construct(l, det);
    attrs_set_base(&l->attrs.a[ATTR_SPAWN_REINFORCEMENTS], 0.0);
    attrs_set_base(&l->attrs.a[ATTR_MOVEMENT_SPEED], 0.5);
    attrs_set_base(&l->attrs.a[ATTR_ATTACK_DAMAGE], 5.0);
    l->immune_to_fire = 1;
    living_set_health(l, living_max_health(l));
}

void pigman_add_random_armor(struct living *l)
{
    l->equip[0].id = 283;
    l->equip[0].count = 1;

    struct attr_mod sword;
    memset(&sword, 0, sizeof sword);
    sword.uuid_msb = -3799650116634706120LL;
    sword.uuid_lsb = -6586616428615387697LL;
    sword.amount = 4.0;
    sword.from_item = 1;
    attrs_apply(&l->attrs.a[ATTR_ATTACK_DAMAGE], &sword);
}

void pigman_restore_speed_boost(struct living *l)
{
    attrs_apply(&l->attrs.a[ATTR_MOVEMENT_SPEED], &attacking_speed_boost);
}

void pigman_pre_on_update(struct living *l, det_state *det)
{
    if (lv_get(l->pigman_last_entity_to_attack) != lv_get(l->entity_to_attack))
    {
        attrs_remove(&l->attrs.a[ATTR_MOVEMENT_SPEED], &attacking_speed_boost);
        if (lv_get(l->entity_to_attack) != NULL)
            attrs_apply(&l->attrs.a[ATTR_MOVEMENT_SPEED], &attacking_speed_boost);
    }
    l->pigman_last_entity_to_attack = l->entity_to_attack;

    if (l->pigman_random_sound_delay > 0 && --l->pigman_random_sound_delay == 0)
    {
        (void)det_rng_float(&l->rand);
        (void)det_rng_float(&l->rand);
    }
    (void)det;
}

void pigman_on_living_update(struct living *l, det_state *det)
{
    zombie_on_living_update(l, det);
}

static void become_angry_at(struct living *l, struct living *attacker)
{
    l->entity_to_attack = lv_ref(attacker);
    /* 1.7.10 stores this random timer but never decrements it. */
    l->pigman_anger_level = 400 + det_rng_int_n(&l->rand, 400);
    l->pigman_random_sound_delay = det_rng_int_n(&l->rand, 40);
}

int pigman_attack_entity_from(struct living *l, struct living *attacker, int source, float amount, det_state *det)
{
    if (l->invulnerable) return 0;

    if (attacker != NULL && (attacker->kind == HK_PLAYER || attacker->kind == SK_PLAYER))
    {
        struct aabb box = aabb_expand(l->e.bounding_box, 32.0, 32.0, 32.0);
        AN_QUERY_LIST(nearby);
        int n = an_entities_excluding(l->an, NULL, &box, nearby, AN_MAX_ENTITIES);

        for (int i = 0; i < n; ++i)
        {
            struct living *other = nearby[i]->is_living ? lv_get(nearby[i]->livh) : NULL;
            if (other != NULL && other != l && other->kind == HK_PIGMAN) become_angry_at(other, attacker);
        }
        become_angry_at(l, attacker);
    }
    return living_attack_entity_from_attacker_base(l, attacker, source, amount, det);
}

void pigman_drop_few_items(struct living *l, int looting)
{
    int count = det_rng_int_n(&l->rand, 2 + looting);
    for (int i = 0; i < count; ++i)
        an_spawn_item(l->an, l->an->n, l->e.pos_x, l->e.pos_y, l->e.pos_z, 367, 0, 1, 10);
    count = det_rng_int_n(&l->rand, 2 + looting);
    for (int i = 0; i < count; ++i)
        an_spawn_item(l->an, l->an->n, l->e.pos_x, l->e.pos_y, l->e.pos_z, 371, 0, 1, 10);
}

void pigman_write_kind_nbt(struct living *l, struct nbtw *w)
{
    nbtw_short(w, "Anger", (short)l->pigman_anger_level);
}

void pigman_write_extra(struct living *l, unsigned char *rec, int *p)
{
    put32(rec, p, l && l->kind == HK_PIGMAN ? l->pigman_anger_level : 0);
    put32(rec, p, l && l->kind == HK_PIGMAN ? l->pigman_random_sound_delay : 0);
    put32(rec, p, l && l->kind == HK_PIGMAN && lv_get(l->pigman_last_entity_to_attack)
                  ? lv_get(l->pigman_last_entity_to_attack)->spawn_index : -1);
    struct path_ent *path = l && l->kind == HK_PIGMAN ? path_at(l->creature_path) : NULL;
    put32(rec, p, path ? 1 : 0);
    put32(rec, p, path ? path->index : 0);
    put32(rec, p, path ? path->length : 0);
    uint64_t hash = 0xcbf29ce484222325ULL;
    if (path != NULL)
    {
        for (int i = 0; i < path->length; ++i)
        {
            for (int k = 0; k < 3; ++k)
            {
                int v = path->pts[i][k];
                for (int b = 0; b < 4; ++b)
                    hash = (hash ^ (uint64_t)((v >> (8 * b)) & 255)) * 0x100000001b3ULL;
            }
        }
    }
    for (int i = 0; i < 8; ++i) rec[(*p)++] = (unsigned char)((hash >> (8 * i)) & 0xff);
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
        living_face_entity(l, lv_get(l->current_target), 10.0F, (float)40);

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
void pigman_update_entity_action_state(struct living *l, det_state *det)
{
    if (l->fleeing_tick > 0 && --l->fleeing_tick == 0)
    {
        attrs_remove(&l->attrs.a[ATTR_MOVEMENT_SPEED], &fleeing_speed_bonus);
    }

    l->has_attacked = 0;   /* EntityCreature.isMovementCeased */
    float var21 = 16.0F;

    if (lv_get(l->entity_to_attack) == NULL && !l->entity_to_attack_gone)
    {
        l->entity_to_attack = lv_ref(find_player_to_attack(l));

        if (lv_get(l->entity_to_attack) != NULL)
        {
            pigman_clear_path(l);
            l->creature_path = pigman_path_to_entity(l, lv_get(l->entity_to_attack), var21);
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
        pathref p = pigman_path_to_entity(l, lv_get(l->entity_to_attack), var21);
        pigman_clear_path(l);
        l->creature_path = p;
    }
    else if (!l->has_attacked
             && ((l->creature_path == 0 && det_rng_int_n(&l->rand, 180) == 0)
                 || det_rng_int_n(&l->rand, 120) == 0 || l->fleeing_tick > 0)
             && l->entity_age < 100)
    {
        pigman_update_wander_path(l);
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
                pigman_clear_path(l);
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
        pigman_clear_path(l);
    }
}
