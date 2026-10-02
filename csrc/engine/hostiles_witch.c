#include "hostiles_witch.h"
#include "jmath.h"
#include "world.h"
#include "hostiles.h"
#include "ai.h"
#include "items.h"
#include "potion.h"
#include "projectile.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>

static inline double living_dist_sq(const struct living *a, const struct living *b)
{
    double dx = a->e.pos_x - b->e.pos_x;
    double dy = a->e.pos_y - b->e.pos_y;
    double dz = a->e.pos_z - b->e.pos_z;
    return dx * dx + dy * dy + dz * dz;
}

void witch_construct(struct living *l, det_state *det)
{
    /* EntityMob's constructor: the XP a player kill drops */
    l->experience_value = 5;
    attrs_set_base(&l->attrs.a[ATTR_MAX_HEALTH], 26.0);
    attrs_set_base(&l->attrs.a[ATTR_MOVEMENT_SPEED], 0.25);
    attrs_set_base(&l->attrs.a[ATTR_FOLLOW_RANGE], 16.0);
    attrs_set_base(&l->attrs.a[ATTR_ATTACK_DAMAGE], 2.0);
    living_set_health(l, 26.0f);
    entity_set_size(&l->e, 0.6f, 1.8f);

    ai_setup_kind(l, det);
}

void witch_set_aggressive(struct living *l, int agg)
{
    l->witch_aggressive = agg ? 1 : 0;
}

int witch_get_aggressive(const struct living *l)
{
    return l->witch_aggressive;
}

/* EntityWitch.field_110185_bq: "Drinking speed penalty", -0.25, operation
 * 0, saved false. */
static const struct attr_mod drinking_mod = {
    .name = MODN_DRINKING_SPEED_PENALTY,
    .amount = -0.25,
    .operation = 0,
    .uuid_msb = 6688265815086220243LL,
    .uuid_lsb = -6545541163342161890LL,
    .saved = 0
};

void witch_restore_drinking(struct living *l)
{
    if (witch_get_aggressive(l)) attrs_apply(&l->attrs.a[ATTR_MOVEMENT_SPEED], &drinking_mod);
}

void witch_on_living_update(struct living *l, det_state *det)
{

    if (witch_get_aggressive(l))
    {
        if (l->witch_attack_timer-- <= 0)
        {
            witch_set_aggressive(l, 0);
            int had_potion = (l->equip[0].id == 373);
            int potion_damage = l->equip[0].damage;
            l->equip[0].id = 0;
            l->equip[0].damage = 0;
            l->equip[0].count = 0;

            if (had_potion)
            {
                struct potion_effect effects[16];
                int neff = potion_get_effects_for_damage(potion_damage, effects, 16);
                for (int i = 0; i < neff; ++i)
                {
                    living_add_potion_effect(l, &effects[i], det);
                }
            }

            attrs_remove(&l->attrs.a[ATTR_MOVEMENT_SPEED], &drinking_mod);
        }
    }
    else
    {
        short var5 = -1;

        if (det_rng_float(&l->rand) < 0.15f && living_is_inside_of_material(l, 6) && !living_is_potion_active(l, POT_WATER_BREATHING))
        {
            var5 = 8237;
        }
        else if (det_rng_float(&l->rand) < 0.15f && living_is_burning(l) && !living_is_potion_active(l, POT_FIRE_RESISTANCE))
        {
            var5 = 16307;
        }
        else if (det_rng_float(&l->rand) < 0.05f && l->health < living_max_health(l))
        {
            var5 = 16341;
        }
        else if (det_rng_float(&l->rand) < 0.25f && lv_get(l->attack_target) != NULL && !living_is_potion_active(l, POT_MOVE_SPEED) &&
                 living_dist_sq(l, lv_get(l->attack_target)) > 121.0)
        {
            var5 = 16274;
        }
        else if (det_rng_float(&l->rand) < 0.25f && lv_get(l->attack_target) != NULL && !living_is_potion_active(l, POT_MOVE_SPEED) &&
                 living_dist_sq(l, lv_get(l->attack_target)) > 121.0)
        {
            var5 = 16274;
        }

        if (var5 > -1)
        {
            l->equip[0].id = 373;
            l->equip[0].damage = var5;
            l->equip[0].count = 1;
            l->witch_attack_timer = 32;
            witch_set_aggressive(l, 1);
            attrs_remove(&l->attrs.a[ATTR_MOVEMENT_SPEED], &drinking_mod);
            attrs_apply(&l->attrs.a[ATTR_MOVEMENT_SPEED], &drinking_mod);
        }
    }

    if (det_rng_float(&l->rand) < 7.5e-4f)
    {
        /* particle packet, no-op on server */
    }

    /* super.onLivingUpdate is EntityMob's: getBrightness(1.0F) over half
     * a block ages a mob two more ticks (EntityAIWander gives up at 100) */
    {
        int x = mh_floor(l->e.pos_x);
        int z = mh_floor(l->e.pos_z);
        double v4 = (l->e.bounding_box.max_y - l->e.bounding_box.min_y) * 0.66;
        int y = mh_floor(l->e.pos_y - (double)l->e.y_offset + v4);
        float br = living_light_brightness(l->world, l->an ? l->an->skylight : 0, x, y, z);

        if (br > 0.5f) l->entity_age += 2;
    }

    living_default_on_living_update(l, det);
    hostile_loot_update(l);
}

void witch_attack_entity_with_ranged_attack(struct living *l, struct living *target, float power)
{
    (void)power;
    if (witch_get_aggressive(l)) return;

    ie_world *iew = &l->an->iew;
    double px = l->e.pos_x - (double)(mh_cos(l->rotation_yaw / 180.0f * 3.1415927f) * 0.16f);
    double py = l->e.pos_y + (double)living_eye_height(l) - 0.10000000149011612;
    double pz = l->e.pos_z - (double)(mh_sin(l->rotation_yaw / 180.0f * 3.1415927f) * 0.16f);

    float var3_f = 0.4f;
    double init_mx = (double)(-mh_sin(l->rotation_yaw / 180.0f * 3.1415927f) * mh_cos(l->rotation_pitch / 180.0f * 3.1415927f) * var3_f);
    double init_mz = (double)(mh_cos(l->rotation_yaw / 180.0f * 3.1415927f) * mh_cos(l->rotation_pitch / 180.0f * 3.1415927f) * var3_f);
    double init_my = (double)(-mh_sin((l->rotation_pitch - 20.0f) / 180.0f * 3.1415927f) * var3_f);

    ie_ent *potion = ie_ent_alloc();
    entity_init(&potion->e, iew->w);
    potion->e.self = potion;
    potion->e.attack_from = NULL;
    potion->e.first_update = 1;
    potion->first_update = 1;
    potion->entity_id = det_next_entity_id_role(iew->det, iew->role);
    potion->rand = det_new_random_role(iew->det, iew->role);
    int64_t msb, lsb;
    det_uuid_role(iew->det, iew->role, &msb, &lsb);
    potion->uuid_msb = msb;
    potion->uuid_lsb = lsb;

    potion->kind = IE_POTION;
    entity_set_size(&potion->e, 0.25f, 0.25f);
    potion->e.y_offset = 0.0f;
    entity_set_position(&potion->e, px, py, pz);
    potion->tile_x = potion->tile_y = potion->tile_z = -1;
    potion->in_tile = -1;
    potion->shooting_entity = lv_ref(l);

    proj_throwable_heading(potion, init_mx, init_my, init_mz, 0.5f, 1.0f);

    potion->rotation_pitch += 20.0f;

    int potion_damage = 32732;

    double var4 = target->e.pos_x + target->e.motion_x - l->e.pos_x;
    double var6 = target->e.pos_y + (double)living_eye_height(target) - 1.100000023841858 - l->e.pos_y;
    double var8 = target->e.pos_z + target->e.motion_z - l->e.pos_z;
    float var10 = (float)sqrt(var4 * var4 + var8 * var8);

    if (var10 >= 8.0f && !living_is_potion_active(target, POT_MOVE_SLOWDOWN))
    {
        potion_damage = 32698;
    }
    else if (target->health >= 8.0f && !living_is_potion_active(target, POT_POISON))
    {
        potion_damage = 32660;
    }
    else if (var10 <= 3.0f && !living_is_potion_active(target, POT_WEAKNESS) && det_rng_float(&l->rand) < 0.25f)
    {
        potion_damage = 32696;
    }

    potion->potion_damage = potion_damage;

    proj_throwable_heading(potion, var4, var6 + (double)(var10 * 0.2f), var8, 0.75f, 8.0f);

    ie_list_push(iew, potion);
    ie_added_to_world(iew, potion);
    potion->dimension = l->an->dimension;
    potion->spawn_index = l->an->n;

    struct an_ent *aent = an_ent_alloc();
    aent->used = 1;
    aent->is_living = 0;
    aent->spawn_index = l->an->n;
    aent->livh = 0;
    aent->ieh = ie_ref(potion);
    an_list_push(l->an, aent);
    an_chunk_add(l->an, aent, mh_floor(potion->e.pos_x / 16.0), mh_floor(potion->e.pos_y / 16.0),
                 mh_floor(potion->e.pos_z / 16.0));
}

void witch_drop_few_items(struct living *l, int hit_by_player, int looting, det_state *det)
{
    (void)hit_by_player;
    (void)det;
    static const int witch_drops[8] = { 348, 353, 331, 375, 374, 289, 280, 280 };
    int count = det_rng_int_n(&l->rand, 3) + 1;
    for (int i = 0; i < count; ++i)
    {
        int num = det_rng_int_n(&l->rand, 3);
        int item = witch_drops[det_rng_int_n(&l->rand, 8)];
        if (looting > 0)
        {
            num += det_rng_int_n(&l->rand, looting + 1);
        }
        for (int j = 0; j < num; ++j)
        {
            if (l->an) an_spawn_item(l->an, l->an->n, l->e.pos_x, l->e.pos_y, l->e.pos_z, item, 0, 1, 10);
        }
    }
}
