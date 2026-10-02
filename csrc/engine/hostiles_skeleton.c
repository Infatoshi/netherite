#include "nbtw.h"
#include "hostiles_skeleton.h"
#include "combatench.h"
#include "jmath.h"
#include "world.h"
#include "hostiles.h"
#include "ai.h"
#include "enchant.h"
#include "items.h"
#include "potion.h"
#include "smath.h"
#include <string.h>
#include <math.h>

void skeleton_construct(struct living *l, det_state *det)
{
    /* EntityMob's constructor: the XP a player kill drops */
    l->experience_value = 5;
    attrs_set_base(&l->attrs.a[ATTR_MOVEMENT_SPEED], 0.25);
    attrs_set_base(&l->attrs.a[ATTR_FOLLOW_RANGE], 16.0);
    attrs_set_base(&l->attrs.a[ATTR_ATTACK_DAMAGE], 2.0);
    entity_set_size(&l->e, 0.6f, 1.8f);
    /* EntityMob's constructor: the XP a player kill drops */
    l->experience_value = 5;

    ai_setup_kind(l, det);
    skeleton_set_combat_task(l);
}

void skeleton_set_type(struct living *l, int type)
{
    l->skeleton_type = type;
    l->immune_to_fire = (type == 1);
    if (type == 1)
    {
        living_set_size(l, 0.72f, 2.34f);
    }
    else
    {
        living_set_size(l, 0.6f, 1.8f);
    }
}

/* EntityLivingBase.onUpdate's equipment pass: the held sword's "Weapon
 * modifier" joins attackDamage (an item modifier, left out of the NBT). A
 * stand-in egg may already have put one there: the old one goes first. */
void skeleton_apply_weapon(struct living *l)
{
    struct attr_mod w_mod;
    memset(&w_mod, 0, sizeof w_mod);
    w_mod.uuid_msb = -3799650116634706120LL;
    w_mod.uuid_lsb = -6586616428615387697LL;
    attrs_remove(&l->attrs.a[ATTR_ATTACK_DAMAGE], &w_mod);
    int id = l->equip[0].id;
    if (id <= 0 || id >= 4096 || ITEMS[id].kind != ITEM_SWORD) return;
    w_mod.amount = (double)ITEMS[id].damage;
    w_mod.operation = 0;
    w_mod.from_item = 1;
    attrs_apply(&l->attrs.a[ATTR_ATTACK_DAMAGE], &w_mod);
}

void skeleton_set_combat_task(struct living *l)
{
    ai_remove_task(l, AIC_ATTACK_ON_COLLIDE);
    ai_remove_task(l, AIC_ARROW_ATTACK);
    if (l->equip[0].id == 261)
    {
        ai_add_task_arrow_attack(l, 4, 1.0, 20, 60, 15.0f);
    }
    else
    {
        ai_add_task_attack_on_collide(l, 4, 1.2, 0);
    }
}

void skeleton_on_spawn_with_egg(struct living *l, det_state *det)
{
    (void)det;
    if (l->dimension == -1 && det_rng_int_n(&l->rand, 5) > 0)
    {
        skeleton_set_type(l, 1);
        l->equip[0].id = 272; /* Items.stone_sword */
        l->equip[0].damage = 0;
        l->equip[0].count = 1;
        attrs_set_base(&l->attrs.a[ATTR_ATTACK_DAMAGE], 4.0);
        skeleton_apply_weapon(l);
        skeleton_set_combat_task(l);
    }
    else
    {
        float diff = l->difficulty_factor;
        /* addRandomArmor: super.addRandomArmor */
        living_add_random_armor(l);
        /* addRandomArmor sets slot 0 to bow */
        l->equip[0].id = 261; /* Items.bow */
        l->equip[0].damage = 0;
        l->equip[0].count = 1;
        skeleton_set_combat_task(l);

        /* enchantEquipment */
        if (l->equip[0].id > 0 && det_rng_float(&l->rand) < 0.25f * diff)
        {
            int r18 = det_rng_int_n(&l->rand, 18);
            int budget = (int)(5.0f + diff * (float)r18);
            struct enchant_data list[8];
            int n = build_enchantment_list(&l->rand.r, l->equip[0].id, budget, list, 8);
            l->equip[0].tag = enchant_list_tag(l->equip[0].tag, list, n);
        }
        for (int s = 0; s < 4; ++s)
        {
            if (l->equip[s + 1].id > 0 && det_rng_float(&l->rand) < 0.5f * diff)
            {
                int r18 = det_rng_int_n(&l->rand, 18);
                int budget = (int)(5.0f + diff * (float)r18);
                struct enchant_data list[8];
                int n = build_enchantment_list(&l->rand.r, l->equip[s + 1].id, budget, list, 8);
                l->equip[s + 1].tag = enchant_list_tag(l->equip[s + 1].tag, list, n);
            }
        }
    }

    l->can_pick_up_loot = (det_rng_float(&l->rand) < 0.55f * l->difficulty_factor);
}

void skeleton_on_living_update(struct living *l, det_state *det)
{
    int x = mh_floor(l->e.pos_x);
    int z = mh_floor(l->e.pos_z);
    double v4 = (l->e.bounding_box.max_y - l->e.bounding_box.min_y) * 0.66;
    int y = mh_floor(l->e.pos_y - (double)l->e.y_offset + v4);
    float br = living_light_brightness(l->world, l->an ? l->an->skylight : 0, x, y, z);

    int is_daytime = (l->an ? l->an->skylight < 4 : 0);
    if (is_daytime)
    {
        if (br > 0.5f && det_rng_float(&l->rand) * 30.0f < (br - 0.4f) * 2.0f &&
            world_can_block_see_the_sky(l->world, x, mh_floor(l->e.pos_y), z))
        {
            int burn = 1;
            if (l->equip[4].id > 0)
            {
                mob_sun_helmet(l, det);
                burn = 0;
            }
            if (burn)
            {
                living_set_fire(l, 8);
            }
        }
    }

    if (br > 0.5f)
    {
        l->entity_age += 2;
    }

    living_default_on_living_update(l, det);
    hostile_loot_update(l);
}

int skeleton_attack_entity_as_mob(struct living *l, struct living *target, det_state *det)
{
    /* EntitySkeleton.attackEntityAsMob: EntityMob's (the mob source), then
     * the wither skeleton's wither */
    int hit = mob_attack_entity_as_mob(l, target, det);
    if (hit)
    {
        if (l->skeleton_type == 1)
        {
            struct potion_effect eff = {
                .id = (uint8_t)POT_WITHER,
                .duration = 200,
                .amplifier = 0,
                .is_splash = 0,
                .is_ambient = 0
            };
            living_add_potion_effect(target, &eff, det);
        }
    }
    return hit;
}

static int get_enchant_level(const struct equip_slot *eq, int effect_id)
{
    return itag_ench_level(eq->tag, effect_id);
}

void skeleton_attack_entity_with_ranged_attack(struct living *l, struct living *target, float power)
{
    float speed = 1.6f;
    int diff_id = l->an != NULL ? l->an->difficulty : 2;   /* World.difficultySetting */
    float inaccuracy = (float)(14 - diff_id * 4);
    ie_ent *arrow = an_spawn_arrow(l->an, l, target, speed, inaccuracy);
    if (!arrow) return;

    int power_lvl = get_enchant_level(&l->equip[0], 48);
    int punch_lvl = get_enchant_level(&l->equip[0], 49);
    int flame_lvl = get_enchant_level(&l->equip[0], 50);

    double dmg = (double)(power * 2.0f) + det_rng_gaussian(&l->rand) * 0.25 + (double)((float)diff_id * 0.11f);
    if (power_lvl > 0)
    {
        dmg += (double)power_lvl * 0.5 + 0.5;
    }
    arrow->arrow_damage = dmg;

    if (punch_lvl > 0)
    {
        arrow->knockback_strength = punch_lvl;
    }

    if (flame_lvl > 0 || l->skeleton_type == 1)
    {
        arrow->e.fire = 2000;
    }

    /* this.playSound("random.bow", 1.0F, 1.0F / (this.getRNG().nextFloat() * 0.4F + 0.8F)); */
    det_rng_float(&l->rand);
}

void skeleton_drop_few_items(struct living *l, int hit_by_player, int looting, det_state *det)
{
    (void)hit_by_player;
    (void)det;
    int var3, var4;

    if (l->skeleton_type == 1)
    {
        var3 = det_rng_int_n(&l->rand, 3 + looting) - 1;
        for (var4 = 0; var4 < var3; ++var4)
        {
            if (l->an) an_spawn_item(l->an, l->an->n, l->e.pos_x, l->e.pos_y, l->e.pos_z, 263, 0, 1, 10);
        }
    }
    else
    {
        var3 = det_rng_int_n(&l->rand, 3 + looting);
        for (var4 = 0; var4 < var3; ++var4)
        {
            if (l->an) an_spawn_item(l->an, l->an->n, l->e.pos_x, l->e.pos_y, l->e.pos_z, 262, 0, 1, 10);
        }
    }

    var3 = det_rng_int_n(&l->rand, 3 + looting);
    for (var4 = 0; var4 < var3; ++var4)
    {
        if (l->an) an_spawn_item(l->an, l->an->n, l->e.pos_x, l->e.pos_y, l->e.pos_z, 352, 0, 1, 10);
    }
}

void skeleton_write_kind_nbt(struct living *l, struct nbtw *w)
{
    nbtw_byte(w, "SkeletonType", (int8_t)l->skeleton_type);
}
