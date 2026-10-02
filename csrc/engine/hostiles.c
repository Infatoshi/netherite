#include "hostiles.h"
#include "combatench.h"
#include "hostiles_spider.h"
#include "hostiles_creeper.h"
#include "hostiles_enderman.h"
#include "hostiles_skeleton.h"
#include "hostiles_witch.h"
#include "hostiles_blaze.h"
#include "hostiles_pigman.h"
#include "hostiles_silverfish.h"
#include "ai.h"
#include "blocks.h"
#include "enchant.h"
#include "items.h"
#include "jmath.h"
#include "living.h"
#include "slimes.h"
#include "trace.h"
#include "world.h"
#include "survival.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put32(unsigned char *b, int *p, int v)
{
    b[(*p)++] = (unsigned char)(v & 0xff);
    b[(*p)++] = (unsigned char)((v >> 8) & 0xff);
    b[(*p)++] = (unsigned char)((v >> 16) & 0xff);
    b[(*p)++] = (unsigned char)((v >> 24) & 0xff);
}

static void putf(unsigned char *b, int *p, float v)
{
    unsigned int u;
    memcpy(&u, &v, sizeof u);
    put32(b, p, (int)u);
}

static void put64(unsigned char *b, int *p, uint64_t v)
{
    put32(b, p, (int)(v & 0xffffffffULL));
    put32(b, p, (int)(v >> 32));
}

static void putd(unsigned char *b, int *p, double v)
{
    uint64_t u;
    memcpy(&u, &v, sizeof u);
    put64(b, p, u);
}

void zombie_construct(struct living *l, det_state *det)
{
    attrs_set_base(&l->attrs.a[ATTR_FOLLOW_RANGE], 40.0);
    attrs_set_base(&l->attrs.a[ATTR_MOVEMENT_SPEED], 0.23000000417232513);
    attrs_set_base(&l->attrs.a[ATTR_ATTACK_DAMAGE], 3.0);
    attrs_set_base(&l->attrs.a[ATTR_SPAWN_REINFORCEMENTS], det_rng_double(&l->rand) * 0.10000000149011612);
    entity_set_size(&l->e, 0.6f, 1.8f);
    /* EntityMob's constructor: the XP a player kill drops */
    l->experience_value = 5;

    l->nav.can_pass_closed_doors = 1;
    l->nav.can_pass_open_doors = 1;

    ai_setup_kind(l, det);
}

void zombie_set_child(struct living *l, int child)
{
    static const struct attr_mod baby_mod = {
        .name = MODN_BABY_SPEED_BOOST,
        .amount = 0.5,
        .operation = 1,
        .uuid_msb = -5082757096938257406LL,
        .uuid_lsb = -4891139119377885130LL,
        .saved = 1
    };

    l->zombie_is_child = child;
    if (child)
    {
        attrs_apply(&l->attrs.a[ATTR_MOVEMENT_SPEED], &baby_mod);
        living_set_size(l, 0.3f, 0.9f);
    }
    else
    {
        attrs_remove(&l->attrs.a[ATTR_MOVEMENT_SPEED], &baby_mod);
        living_set_size(l, 0.6f, 1.8f);
    }
}

/* World.difficultySetting as the egg paths read it: the living world's
 * (the probes' world is NORMAL). */
static int egg_difficulty(const struct living *l)
{
    return l->an != NULL ? l->an->difficulty : 2;
}

/* EntityLiving.addRandomArmor, the zombie's and the skeleton's super call:
 * the per-slot stop chance is 0.1F on HARD, else 0.25F; a tier past 4 falls
 * through getArmorItemForSlot's switch to null. */
void living_add_random_armor(struct living *l)
{
    float diff = l->difficulty_factor;

    if (det_rng_float(&l->rand) < 0.15f * diff)
    {
        int tier = det_rng_int_n(&l->rand, 2);
        float break_chance = egg_difficulty(l) == 3 ? 0.1f : 0.25f;
        if (det_rng_float(&l->rand) < 0.095f) ++tier;
        if (det_rng_float(&l->rand) < 0.095f) ++tier;
        if (det_rng_float(&l->rand) < 0.095f) ++tier;

        for (int slot = 3; slot >= 0; --slot)
        {
            if (slot < 3 && det_rng_float(&l->rand) < break_chance) break;
            if (l->equip[slot + 1].id <= 0)
            {
                int item_id = 0;
                static const int armor_table[4][5] = {
                    { 301, 317, 305, 309, 313 }, /* boots: leather, gold, chain, iron, diamond */
                    { 300, 316, 304, 308, 312 }, /* legs */
                    { 299, 315, 303, 307, 311 }, /* chest */
                    { 298, 314, 302, 306, 310 }  /* helmet */
                };
                if (tier >= 0 && tier <= 4)
                {
                    item_id = armor_table[slot][tier];
                }
                if (item_id > 0)
                {
                    l->equip[slot + 1].id = item_id;
                    l->equip[slot + 1].damage = 0;
                    l->equip[slot + 1].count = 1;
                }
            }
        }
    }
}

/* The jockey hunt: worldObj.selectEntitiesWithinAABB(EntityChicken.class,
 * boundingBox.expand(5, 3, 5), IEntitySelector.field_152785_b), the first
 * chicken alive, unmounted and carrying nobody, in the chunk walk's order. */
static struct living *zombie_find_jockey_chicken(struct living *l)
{
    struct aabb box = aabb_expand(l->e.bounding_box, 5.0, 3.0, 5.0);
    struct an_ent *found[64];
    int n = an_entities_within_aabb(l->an, AK_CHICKEN, &box, found, 64);
    if (n > 64) n = 64;

    for (int i = 0; i < n; ++i)
    {
        struct living *c = lv_get(found[i]->livh);
        if (c->is_dead || c->health <= 0.0f) continue;
        if (lv_get(c->ridden_by_entity) != NULL || lv_get(c->riding_entity) != NULL) continue;
        return c;
    }
    return NULL;
}

/* The jockey chicken: new EntityChicken, setLocationAndAngles at the zombie's
 * pose (pitch 0), its onSpawnWithEgg(null), func_152117_i(true),
 * spawnEntityInWorld, then the zombie's mountEntity. The chicken joins the
 * world inside the zombie's egg path, ahead of the zombie; the caller
 * completes its tick-list insert (egg_take_pending_partner). */
static struct living *zombie_spawn_jockey_chicken(struct living *l, det_state *det)
{
    struct an_world *an = l->an;
    struct living *c = living_alloc();
    if (!c) return NULL;

    living_init(c, an->w, AK_CHICKEN, an->det);
    c->an = an;
    c->dimension = an->dimension;
    c->spawn_index = an->n;
    an_construct_kind(c, det);
    living_set_location_and_angles(c, l->e.pos_x, l->e.pos_y, l->e.pos_z, l->rotation_yaw, 0.0F);
    living_on_spawn_with_egg(c, det);
    c->is_chicken_jockey = 1;
    egg_add_pending_partner(c);
    living_mount(l, c);
    return c;
}

struct living *zombie_on_spawn_with_egg(struct living *l, det_state *det)
{
    float diff = l->difficulty_factor;
    struct living *partner = NULL;

    l->can_pick_up_loot = (det_rng_float(&l->rand) < 0.55f * diff);

    /* GroupData in Java: drawn from worldObj.rand */
    int is_baby_draw = (det_rng_float(&l->an->iew.world_rand) < 0.05f);
    int is_villager_draw = (det_rng_float(&l->an->iew.world_rand) < 0.05f);
    if (is_villager_draw)
    {
        l->zombie_is_villager = 1;
    }
    if (is_baby_draw)
    {
        zombie_set_child(l, 1);
        if ((double)det_rng_float(&l->an->iew.world_rand) < 0.05)
        {
            struct living *c = zombie_find_jockey_chicken(l);
            if (c != NULL)
            {
                c->is_chicken_jockey = 1;
                living_mount(l, c);
            }
        }
        else if ((double)det_rng_float(&l->an->iew.world_rand) < 0.05)
        {
            /* a mob the natural spawner's record path already spawned: its
             * chicken is a record of its own, adopted after it */
            if (!l->an->egg_adopt) partner = zombie_spawn_jockey_chicken(l, det);
        }
    }

    if (det_rng_float(&l->rand) < diff * 0.1f)
    {
        l->zombie_can_break_doors = 1;
        ai_add_break_door(l);
    }

    /* addRandomArmor: super.addRandomArmor */
    if (l->kind == HK_PIGMAN)
    {
        pigman_add_random_armor(l);
    }
    else living_add_random_armor(l);

    /* zombie held item: EntityZombie.addRandomArmor's own roll */
    if (l->kind != HK_PIGMAN && det_rng_float(&l->rand) < (egg_difficulty(l) == 3 ? 0.05f : 0.01f))
    {
        int weapon = det_rng_int_n(&l->rand, 3);
        l->equip[0].id = (weapon == 0) ? 267 : 256;
        l->equip[0].damage = 0;
        l->equip[0].count = 1;
        double dmg = (l->equip[0].id == 267) ? 6.0 : (l->equip[0].id == 256) ? 3.0 : 0.0;
        if (dmg > 0.0)
        {
            struct attr_mod w_mod;
            memset(&w_mod, 0, sizeof w_mod);
            w_mod.uuid_msb = -3799650116634706120LL;
            w_mod.uuid_lsb = -6586616428615387697LL;
            w_mod.amount = dmg;
            w_mod.operation = 0;
            w_mod.saved = 0;
            /* the item's own modifier: removed around the NBT write */
            w_mod.from_item = 1;
            attrs_apply(&l->attrs.a[ATTR_ATTACK_DAMAGE], &w_mod);
        }
    }

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

    /* knockbackResistance modifier */
    struct attr_mod kb_mod;
    memset(&kb_mod, 0, sizeof kb_mod);
    kb_mod.name = MODN_RANDOM_SPAWN_BONUS;
    kb_mod.amount = det_rng_double(&l->rand) * 0.05000000074505806;
    kb_mod.operation = 0;
    det_uuid_role(det, DET_OTHER, &kb_mod.uuid_msb, &kb_mod.uuid_lsb);
    kb_mod.saved = 1;
    attrs_apply(&l->attrs.a[ATTR_KNOCKBACK_RESISTANCE], &kb_mod);

    /* followRange bonus */
    double fr_bonus = det_rng_double(&l->rand) * 1.5 * (double)diff;
    if (fr_bonus > 1.0)
    {
        struct attr_mod fr_mod;
        memset(&fr_mod, 0, sizeof fr_mod);
        fr_mod.name = MODN_RANDOM_ZOMBIE_SPAWN_BONUS;
        fr_mod.amount = fr_bonus;
        fr_mod.operation = 2;
        det_uuid_role(det, DET_OTHER, &fr_mod.uuid_msb, &fr_mod.uuid_lsb);
        fr_mod.saved = 1;
        attrs_apply(&l->attrs.a[ATTR_FOLLOW_RANGE], &fr_mod);
    }

    /* leader bonus */
    if (det_rng_float(&l->rand) < diff * 0.05f)
    {
        struct attr_mod sr_mod;
        memset(&sr_mod, 0, sizeof sr_mod);
        sr_mod.name = MODN_LEADER_ZOMBIE_BONUS;
        sr_mod.amount = det_rng_double(&l->rand) * 0.25 + 0.5;
        sr_mod.operation = 0;
        det_uuid_role(det, DET_OTHER, &sr_mod.uuid_msb, &sr_mod.uuid_lsb);
        sr_mod.saved = 1;
        attrs_apply(&l->attrs.a[ATTR_SPAWN_REINFORCEMENTS], &sr_mod);

        struct attr_mod mh_mod;
        memset(&mh_mod, 0, sizeof mh_mod);
        mh_mod.name = MODN_LEADER_ZOMBIE_BONUS;
        mh_mod.amount = det_rng_double(&l->rand) * 3.0 + 1.0;
        mh_mod.operation = 2;
        det_uuid_role(det, DET_OTHER, &mh_mod.uuid_msb, &mh_mod.uuid_lsb);
        mh_mod.saved = 1;
        attrs_apply(&l->attrs.a[ATTR_MAX_HEALTH], &mh_mod);

        l->zombie_can_break_doors = 1;
        ai_add_break_door(l);
    }
    return partner;
}

static int get_armor_position(int item_id)
{
    if (item_id == 86 || item_id == 397) return 4;
    if (item_id >= 0 && item_id < 4096 && ITEMS[item_id].kind == ITEM_ARMOR)
    {
        switch (ITEMS[item_id].armor_type)
        {
            case 0: return 4;
            case 1: return 3;
            case 2: return 2;
            case 3: return 1;
        }
    }
    return 0;
}

/* EntityLivingBase.onUpdate's equipment pass for a held stack that changed:
 * the old stack's attribute modifiers come off and the new one's go on
 * (Item.getItemAttributeModifiers: ItemSword's "Weapon modifier" and
 * ItemTool's "Tool modifier", both attackDamage under Item.field_111210_e;
 * armour and the rest carry none). Java applies it at the next onUpdate's
 * server half, before that tick's AI; nothing reads the attribute in
 * between, and the NBT write leaves item modifiers out either way. */
void living_held_item_modifiers(struct living *l, int item)
{
    struct attr_mod w_mod;
    memset(&w_mod, 0, sizeof w_mod);
    w_mod.uuid_msb = -3799650116634706120LL;
    w_mod.uuid_lsb = -6586616428615387697LL;
    attrs_remove(&l->attrs.a[ATTR_ATTACK_DAMAGE], &w_mod);
    if (item > 0 && item < 4096 && (ITEMS[item].kind == ITEM_SWORD || ITEMS[item].kind == ITEM_TOOL))
    {
        w_mod.amount = (double)ITEMS[item].damage;
        w_mod.operation = 0;
        w_mod.from_item = 1;
        attrs_apply(&l->attrs.a[ATTR_ATTACK_DAMAGE], &w_mod);
    }
}

void hostile_loot_update(struct living *l)
{
    if (l->an == NULL || !l->can_pick_up_loot || l->dead || l->is_dead) return;

    /* getEntitiesWithinAABB(EntityItem.class, ...): the player's and the
     * blocks' drops (the peer pool) as well as the mobs' own */
    struct aabb box = aabb_expand(l->e.bounding_box, 1.0, 0.0, 1.0);
    IE_QUERY_LIST(items);
    int n_items = ie_items_within_aabb(&l->an->iew, &box, items, IE_MAX_ENTITIES);

    for (int i = 0; i < n_items; ++i)
    {
        ie_ent *item_ent = items[i];
        if (item_ent->is_dead || item_ent->kind != IE_ITEM) continue;

        int item_id = item_ent->stack_item;
        int slot = get_armor_position(item_id);
        int take = 1;

        if (l->equip[slot].id > 0)
        {
            if (slot == 0)
            {
                int new_is_sword = (ITEMS[item_id].kind == ITEM_SWORD);
                int cur_is_sword = (ITEMS[l->equip[0].id].kind == ITEM_SWORD);
                if (new_is_sword && !cur_is_sword)
                {
                    take = 1;
                }
                else if (new_is_sword && cur_is_sword)
                {
                    float new_dmg = ITEMS[item_id].damage;
                    float cur_dmg = ITEMS[l->equip[0].id].damage;
                    if (new_dmg == cur_dmg)
                    {
                        take = (item_ent->stack_damage > l->equip[0].damage) || (item_ent->stack_tag != 0 && l->equip[0].tag == 0);
                    }
                    else
                    {
                        take = (new_dmg > cur_dmg);
                    }
                }
                else
                {
                    take = 0;
                }
            }
            else if (ITEMS[item_id].kind == ITEM_ARMOR && ITEMS[l->equip[slot].id].kind != ITEM_ARMOR)
            {
                take = 1;
            }
            else if (ITEMS[item_id].kind == ITEM_ARMOR && ITEMS[l->equip[slot].id].kind == ITEM_ARMOR)
            {
                int new_dr = ITEMS[item_id].damage_reduce;
                int cur_dr = ITEMS[l->equip[slot].id].damage_reduce;
                if (new_dr == cur_dr)
                {
                    take = (item_ent->stack_damage > l->equip[slot].damage) || (item_ent->stack_tag != 0 && l->equip[slot].tag == 0);
                }
                else
                {
                    take = (new_dr > cur_dr);
                }
            }
            else
            {
                take = 0;
            }
        }

        if (take)
        {
            if (l->equip[slot].id > 0 && det_rng_float(&l->rand) - 0.1f < l->equipment_drop_chances[slot])
            {
                ie_ent *old = an_spawn_item(l->an, l->an->n, l->e.pos_x, l->e.pos_y, l->e.pos_z,
                              l->equip[slot].id, l->equip[slot].damage, l->equip[slot].count ? l->equip[slot].count : 1, 10);
                if (old) old->stack_tag = l->equip[slot].tag;
            }

            /* diamondsToYou: a diamond a player threw (func_145800_j names
             * the thrower; the only thrower here is this run's player) */
            if (item_id == 264 && item_ent->thrower != NULL && l->an->achievement_hook != NULL)
                l->an->achievement_hook(l->an, ACH_DIAMONDS_TO_YOU, l->an->bred_ctx);

            int old_id = l->equip[slot].id;
            l->equip[slot].id = item_id;
            l->equip[slot].damage = item_ent->stack_damage;
            l->equip[slot].count = item_ent->stack_count;
            l->equip[slot].tag = item_ent->stack_tag;
            if (item_ent->stack_count_link.owner && item_ent->stack_count_link.owner != stack_link_item(item_ent).owner)
            {
                /* a stack another living's slot shares (a drop of its
                 * equipment): the slot keeps sharing it */
                l->equip[slot].count_link = item_ent->stack_count_link;
            }
            else
            {
                /* the item's own stack: this slot takes it over, and the
                 * item (dead now, its pool slot soon another entity's) names
                 * the slot, as both held the one ItemStack */
                l->equip[slot].count_link = (struct stack_link){0};
                item_ent->stack_count_link = (struct stack_link){lv_ref(l), slot};
            }
            l->equipment_drop_chances[slot] = 2.0f;
            l->persistence_required = 1;
            if (l->an->collect_hook != NULL) l->an->collect_hook(l->an, item_ent->entity_id, l->an->bred_ctx);
            item_ent->is_dead = 1;
            if (slot == 0 && old_id != item_id) living_held_item_modifiers(l, item_id);
            if (slot == 0 && l->kind == HK_SKELETON)
            {
                skeleton_set_combat_task(l);
            }
        }
    }
}


/* EntityZombie/EntitySkeleton.onLivingUpdate's helmet in the sun: a
 * damageable helmet (isItemStackDamageable: a pumpkin or a skull is not, and
 * draws nothing) takes nextInt(2) damage; at its max it breaks with
 * renderBrokenItemStack's draws and the slot empties. */
void mob_sun_helmet(struct living *l, det_state *det)
{
    struct equip_slot *h = &l->equip[4];
    if (h->id <= 0 || h->id >= 4096 || ITEMS[h->id].max_damage <= 0) return;
    h->damage += det_rng_int_n(&l->rand, 2);
    if (h->damage >= ITEMS[h->id].max_damage)
    {
        living_render_broken_item_stack(l, det);
        memset(h, 0, sizeof *h);
    }
}

void zombie_on_living_update(struct living *l, det_state *det)
{
    int x = mh_floor(l->e.pos_x);
    int z = mh_floor(l->e.pos_z);
    double v4 = (l->e.bounding_box.max_y - l->e.bounding_box.min_y) * 0.66;
    int y = mh_floor(l->e.pos_y - (double)l->e.y_offset + v4);
    float br = living_light_brightness(l->world, l->an ? l->an->skylight : 0, x, y, z);

    int is_daytime = (l->an ? l->an->skylight < 4 : 0);
    if (is_daytime && !l->zombie_is_child)
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

    /* the chicken jockey: a riding zombie with an attack target hands its
     * own PathEntity to the chicken's navigator at speed 1.5 */
    if (lv_get(l->riding_entity) != NULL && lv_get(l->attack_target) != NULL && lv_get(l->riding_entity)->kind == AK_CHICKEN)
        nav_set_path_shared(lv_get(l->riding_entity), l->nav.path, 1.5);

    if (br > 0.5f)
    {
        l->entity_age += 2;
    }

    living_default_on_living_update(l, det);
    hostile_loot_update(l);
}

int zombie_attack_entity_as_mob(struct living *l, struct living *target, det_state *det)
{
    /* EntityMob.attackEntityAsMob (DamageSource.causeMobDamage: blockable, and
     * it costs the player the 0.3F hunger damage), then EntityZombie's tail:
     * a burning zombie with nothing in hand sets the target alight */
    int hit = mob_attack_entity_as_mob(l, target, det);
    if (hit && l->kind == HK_ZOMBIE)
    {
        int diff = l->an != NULL ? l->an->difficulty : 2;
        if (l->equip[0].id <= 0 && !l->immune_to_fire && l->e.fire > 0 &&
            det_rng_float(&l->rand) < (float)diff * 0.3F)
        {
            living_set_fire(target, 2 * diff);
        }
    }
    return hit;
}

void zombie_drop_few_items(struct living *l, int hit_by_player, int looting, det_state *det)
{
    (void)hit_by_player;
    (void)det;
    int count = det_rng_int_n(&l->rand, 3);
    if (looting > 0) count += det_rng_int_n(&l->rand, looting + 1);
    for (int i = 0; i < count; ++i)
    {
        an_spawn_item(l->an, l->an->n, l->e.pos_x, l->e.pos_y, l->e.pos_z, 367, 0, 1, 10);
    }
}

static void hostile_player_on_living_update(struct living *l, det_state *det);

void player_construct(struct living *l, det_state *det)
{
    (void)det;
    l->uuid_msb = -787650594748288006LL;
    l->uuid_lsb = -8775792892390264609LL;
    attrs_set_base(&l->attrs.a[ATTR_MAX_HEALTH], 2000.0);
    living_set_health(l, 2000.0f);
    attrs_set_base(&l->attrs.a[ATTR_MOVEMENT_SPEED], 0.10000000149011612);
    l->attrs.a[ATTR_FOLLOW_RANGE].registered = 0;
    l->attrs.a[ATTR_ATTACK_DAMAGE].registered = 1;
    attrs_set_base(&l->attrs.a[ATTR_ATTACK_DAMAGE], 1.0);
    l->food_level = 20;
    l->food_saturation = 5.0f;
    l->food_exhaustion = 0.0f;
    l->food_timer = 0;
    entity_set_size(&l->e, 0.6f, 1.8f);
    /* EntityPlayer(World, GameProfile) sets yOffset 1.62; setLocationAndAngles
     * adds it to posY, so the box rides 1.62 below the recorded position */
    l->e.y_offset = 1.62f;
    l->e.fire_resistance = 20;
    l->e.fire = 0;
    l->on_living_update = hostile_player_on_living_update;
}

static void player_food_update(struct living *l)
{
    if (l->food_exhaustion > 4.0f)
    {
        l->food_exhaustion -= 4.0f;
        if (l->food_saturation > 0.0f)
        {
            l->food_saturation -= 1.0f;
            if (l->food_saturation < 0.0f) l->food_saturation = 0.0f;
        }
        else
        {
            if (l->food_level > 0) l->food_level--;
        }
    }

    if (l->food_level >= 18 && l->health > 0.0f && l->health < living_max_health(l))
    {
        ++l->food_timer;
        if (l->food_timer >= 80)
        {
            living_heal(l, 1.0f);
            player_add_exhaustion(l, 3.0f);
            l->food_timer = 0;
        }
    }
    else if (l->food_level <= 0)
    {
        ++l->food_timer;
        if (l->food_timer >= 80)
        {
            if (l->health > 1.0f)
            {
                /* EntityPlayer.onLivingUpdate's starve tick:
                 * attackEntityFrom(DamageSource.starve, 1.0F) */
                living_attack_entity_from(l, DMG_STARVE, 1.0f, l->an ? l->an->det : NULL);
            }
            l->food_timer = 0;
        }
    }
    else
    {
        l->food_timer = 0;
    }
}

static void player_loot_update(struct living *l)
{
    if (l->an == NULL || l->health <= 0.0f) return;

    struct aabb box = aabb_expand(l->e.bounding_box, 1.0, 0.5, 1.0);
    IE_QUERY_LIST(items);
    int n_items = ie_get_entities_within_aabb(&l->an->iew, &box, NULL, items, IE_MAX_ENTITIES);

    for (int i = 0; i < n_items; ++i)
    {
        ie_ent *item_ent = items[i];
        if (item_ent->is_dead || item_ent->kind != IE_ITEM) continue;
        if (item_ent->delay > 0) continue;

        int item = item_ent->stack_item;
        int damage = item_ent->stack_damage;
        int count = item_ent->stack_count;
        int max_stack = (item >= 0 && item < 32000 && ITEMS[item].max_stack_size > 0) ? ITEMS[item].max_stack_size : 64;

        int orig_count = count;
        for (int s = 0; s < 36; ++s)
        {
            if (lv_player(l)->player_inv[s].id == item && lv_player(l)->player_inv[s].damage == damage && lv_player(l)->player_inv[s].count < max_stack
                && lv_player(l)->player_inv[s].tag == item_ent->stack_tag)
            {
                int take = max_stack - lv_player(l)->player_inv[s].count;
                if (take > count) take = count;
                lv_player(l)->player_inv[s].count += take;
                count -= take;
                if (count <= 0) break;
            }
        }
        if (count > 0)
        {
            for (int s = 0; s < 36; ++s)
            {
                if (lv_player(l)->player_inv[s].id == 0 || lv_player(l)->player_inv[s].count == 0)
                {
                    int take = max_stack;
                    if (take > count) take = count;
                    lv_player(l)->player_inv[s].id = item;
                    lv_player(l)->player_inv[s].damage = damage;
                    lv_player(l)->player_inv[s].count = take;
                    lv_player(l)->player_inv[s].tag = item_ent->stack_tag;
                    count -= take;
                    if (count <= 0) break;
                }
            }
        }

        if (count < orig_count)
        {
            item_ent->stack_count = count;
            (void)det_rng_float(&item_ent->rand);
            (void)det_rng_float(&item_ent->rand);
            if (item_ent->stack_count <= 0)
            {
                item_ent->is_dead = 1;
            }
        }
    }
}

static void hostile_player_on_living_update(struct living *l, det_state *det)
{
    living_default_on_living_update(l, det);
    player_food_update(l);
    player_loot_update(l);
}

struct living *hostile_spawn(struct an_world *an, int kind, int spawn_index, double x, double y, double z,
                             float yaw, float pitch, double mx, double my, double mz,
                             int child, int villager, int wither, int charged, float diff_factor)
{
    (void)wither;

    struct living *l = living_alloc();
    if (!l) return NULL;

    living_init(l, an->w, kind, an->det);
    l->dimension = an->dimension;
    l->an = an;
    l->spawn_index = spawn_index;
    l->difficulty_factor = diff_factor;

    /* the spider joins the list before onSpawnWithEgg: the jockey rider its
     * setup spawns is absorbed after it, exactly the probe's list order */
    int appended = 0;

    if (kind == HK_PLAYER)
    {
        player_construct(l, an->det);
        l->added_to_chunk = 1;
        living_set_location_and_angles(l, x, y - (double)l->e.y_offset, z, yaw, pitch);
        /* World.playerEntities: getClosestVulnerablePlayerToEntity (the old
         * AI's findPlayerToAttack) walks it; the AABB queries reach the player
         * through the chunk lists either way */
        an->playerh = lv_ref(l);
        an->has_player = 1;
    }
    else if (kind == HK_ZOMBIE)
    {
        zombie_construct(l, an->det);
        living_set_location_and_angles(l, x, y, z, yaw, pitch);
        if (child) zombie_set_child(l, 1);
        if (villager) l->zombie_is_villager = 1;
        /* a jockey chicken the egg path spawned joins the list ahead of it */
        if (living_on_spawn_with_egg_ret(l, an->det) != NULL) egg_take_pending_partner();
        l->persistence_required = 1;
        l->added_to_chunk = 1;
    }
    else if (kind == HK_SPIDER || kind == HK_CAVE_SPIDER)
    {
        spider_construct(l, an->det);
        living_set_location_and_angles(l, x, y, z, yaw, pitch);
        /* onSpawnWithEgg runs while the spider is not yet in the tick list:
         * a jockey rider it spawns chunk-adds first, and both tick-list adds
         * land after it in the probe's order (spider, rider) */
        struct living *rider = living_on_spawn_with_egg_ret(l, an->det);

        struct an_ent *sp = an_add_living_list(an, l, spawn_index);
        an_add_living_chunk(an, sp, l);
        appended = 1;

        if (rider) egg_take_pending_partner();

        l->persistence_required = 1;
        l->added_to_chunk = 1;
    }    else if (kind == HK_SKELETON)
    {
        skeleton_construct(l, an->det);
        living_set_location_and_angles(l, x, y, z, yaw, pitch);
        if (wither) skeleton_set_type(l, 1);
        living_on_spawn_with_egg(l, an->det);
        l->persistence_required = 1;
        l->added_to_chunk = 1;
    }
    else if (kind == HK_CREEPER)
    {
        creeper_construct(l, an->det);
        living_set_location_and_angles(l, x, y, z, yaw, pitch);

        /* The probe's charged creeper takes onStruckByLightning with a bolt
         * built at the spawn point, before onSpawnWithEgg */
        if (charged) creeper_struck_by_lightning(l, an->det, x, y, z);
        living_on_spawn_with_egg(l, an->det);
        l->persistence_required = 1;
        l->added_to_chunk = 1;
    }
    else if (kind == HK_ENDERMAN)
    {
        enderman_construct(l, an->det);
        living_set_location_and_angles(l, x, y, z, yaw, pitch);
        living_on_spawn_with_egg(l, an->det);
        l->persistence_required = 1;
        l->added_to_chunk = 1;
    }
    else if (kind == HK_WITCH)
    {
        witch_construct(l, an->det);
        living_set_location_and_angles(l, x, y, z, yaw, pitch);
        living_on_spawn_with_egg(l, an->det);
        l->persistence_required = 1;
        l->added_to_chunk = 1;
    }
    else if (kind == HK_BLAZE)
    {
        blaze_construct(l, an->det);
        living_set_location_and_angles(l, x, y, z, yaw, pitch);
        living_on_spawn_with_egg(l, an->det);
        l->persistence_required = 1;
        l->added_to_chunk = 1;
    }
    else if (kind == HK_PIGMAN)
    {
        pigman_construct(l, an->det);
        living_set_location_and_angles(l, x, y, z, yaw, pitch);
        living_on_spawn_with_egg(l, an->det);
        l->persistence_required = 1;
        l->added_to_chunk = 1;
    }
    else if (kind == HK_SILVERFISH)
    {
        silverfish_construct(l, an->det);
        living_set_location_and_angles(l, x, y, z, yaw, pitch);
        living_on_spawn_with_egg(l, an->det);
        l->persistence_required = 1;
        l->added_to_chunk = 1;
    }
    else
    {
        living_set_location_and_angles(l, x, y, z, yaw, pitch);
    }
    l->e.motion_x = mx;
    l->e.motion_y = my;
    l->e.motion_z = mz;

    if (!appended)
    {
        struct an_ent *en = an_ent_alloc();
        en->used = 1;
        en->is_living = 1;
        en->spawn_index = spawn_index;
        en->livh = lv_ref(l);
        en->ieh = 0;
        an_list_push(an, en);
        an_chunk_add(an, en, mh_floor(l->e.pos_x / 16.0), mh_floor(l->e.pos_y / 16.0),
                     mh_floor(l->e.pos_z / 16.0));
    }

    return l;
}

void hostile_write_state(struct an_world *an, const struct an_ent *en, unsigned char *rec, int tick)
{
    (void)tick;
    an_write_state(an, en, rec);

    int p = 272;
    struct living *l = en->is_living ? lv_get(en->livh) : NULL;

    put32(rec, &p, l ? l->attack_time : 0);
    put32(rec, &p, l ? l->hurt_time : 0);
    put32(rec, &p, l ? l->hurt_resistant_time : 0);
    put32(rec, &p, l ? l->max_hurt_time : 0);
    put32(rec, &p, l ? l->recently_hit : 0);
    putf(rec, &p, l ? l->health : 0.0f);

    put32(rec, &p, l && lv_get(l->entity_to_attack) ? lv_get(l->entity_to_attack)->spawn_index : -1);
    put32(rec, &p, l && lv_get(l->attack_target) ? lv_get(l->attack_target)->spawn_index : -1);
    put32(rec, &p, l && lv_get(l->last_attacker) ? lv_get(l->last_attacker)->spawn_index : -1);
    put32(rec, &p, l && lv_get(l->current_target) ? lv_get(l->current_target)->spawn_index : -1);
    put32(rec, &p, l ? l->num_ticks_to_chase_target : 0);
    put32(rec, &p, l ? l->fleeing_tick : 0);
    put32(rec, &p, l ? l->has_attacked : 0);

    int nav_flags = 0;
    if (l && l->kind != HK_PLAYER)
    {
        if (l->nav.avoids_water) nav_flags |= 1;
        if (l->nav.can_swim) nav_flags |= 2;
        if (l->nav.can_pass_open_doors) nav_flags |= 4;
        if (l->nav.can_pass_closed_doors) nav_flags |= 8;
        if (l->nav.no_sun_pathfind) nav_flags |= 16;
    }
    put32(rec, &p, nav_flags);

    for (int s = 0; s < 5; ++s)
    {
        if (l && l->equip[s].id > 0)
        {
            int cnt = l->equip[s].count_link.owner ? *stack_link_count(l->equip[s].count_link) : l->equip[s].count;
            put32(rec, &p, l->equip[s].id);
            put32(rec, &p, l->equip[s].damage);
            put32(rec, &p, cnt ? cnt : 1);
        }
        else
        {
            put32(rec, &p, -1);
            put32(rec, &p, 0);
            put32(rec, &p, 0);
        }
    }

    put32(rec, &p, l && (l->kind == HK_ZOMBIE || l->kind == HK_PIGMAN) ? l->zombie_conversion_time : 0);
    put32(rec, &p, l && (l->kind == HK_ZOMBIE || l->kind == HK_PIGMAN) && l->zombie_is_child ? 1 : 0);
    put32(rec, &p, l && (l->kind == HK_ZOMBIE || l->kind == HK_PIGMAN) && l->zombie_is_villager ? 1 : 0);
    put32(rec, &p, l && (l->kind == HK_ZOMBIE || l->kind == HK_PIGMAN) && l->zombie_can_break_doors ? 1 : 0);
    put32(rec, &p, l && (l->kind == HK_ZOMBIE || l->kind == HK_PIGMAN) && l->zombie_is_converting ? 1 : 0);
    int sk_type = -1;
    int sk_ranged_attack_time = 0;
    int sk_field_75318_f = 0;
    if (l && (l->kind == HK_SKELETON || l->kind == HK_WITCH))
    {
        if (l->kind == HK_SKELETON) sk_type = l->skeleton_type;
        for (int i = 0; i < lv_ai(l)->tasks.n; ++i)
        {
            if (lv_ai(l)->tasks.entries[i].t.cls == AIC_ARROW_ATTACK)
            {
                sk_ranged_attack_time = lv_ai(l)->tasks.entries[i].t.ranged_attack_time;
                sk_field_75318_f = lv_ai(l)->tasks.entries[i].t.field_75318_f;
                break;
            }
        }
    }
    put32(rec, &p, sk_type);
    put32(rec, &p, sk_ranged_attack_time);
    put32(rec, &p, sk_field_75318_f);

    put32(rec, &p, l && l->kind == HK_CREEPER ? l->creeper_last_active_time : 0);
    put32(rec, &p, l && l->kind == HK_CREEPER ? l->creeper_time_since_ignited : 0);
    put32(rec, &p, l && l->kind == HK_CREEPER ? l->creeper_fuse_time : 0);
    put32(rec, &p, l && l->kind == HK_CREEPER ? l->creeper_explosion_radius : 0);
    put32(rec, &p, l && l->kind == HK_CREEPER ? l->creeper_state : 0);
    put32(rec, &p, l && l->kind == HK_CREEPER ? l->creeper_powered : 0);
    put32(rec, &p, l && l->kind == HK_CREEPER ? l->creeper_ignited : 0);
    put32(rec, &p, l && (l->kind == HK_SPIDER || l->kind == HK_CAVE_SPIDER)
              ? (l->data_watcher_16 & 1) : 0);   /* spider_climb */
    int col_attack_tick = 0;
    int col_cooldown = 0;
    double col_px = 0.0, col_py = 0.0, col_pz = 0.0;
    if (l)
    {
        for (int i = 0; i < lv_ai(l)->tasks.n; ++i)
        {
            if (lv_ai(l)->tasks.entries[i].t.cls == AIC_ATTACK_ON_COLLIDE)
            {
                col_attack_tick = lv_ai(l)->tasks.entries[i].t.attack_tick;
                col_cooldown = lv_ai(l)->tasks.entries[i].t.collide_cooldown;
                col_px = lv_ai(l)->tasks.entries[i].t.collide_px;
                col_py = lv_ai(l)->tasks.entries[i].t.collide_py;
                col_pz = lv_ai(l)->tasks.entries[i].t.collide_pz;
                break;
            }
        }
    }
    put32(rec, &p, col_attack_tick);
    put32(rec, &p, col_cooldown);
    putd(rec, &p, col_px);
    putd(rec, &p, col_py);
    putd(rec, &p, col_pz);

    put32(rec, &p, l ? l->air : 0);
    put32(rec, &p, l ? l->e.fire : (ie_get(en->ieh) ? ie_get(en->ieh)->e.fire : 0));
    put32(rec, &p, l ? l->death_time : 0);
    putf(rec, &p, l ? l->field_70764_aw : 0.0f);
    putf(rec, &p, l ? l->limb_swing : 0.0f);
    putf(rec, &p, l ? l->limb_swing_amount : 0.0f);

    putd(rec, &p, (l && l->attrs.a[ATTR_MAX_HEALTH].registered) ? attrs_value(&l->attrs.a[ATTR_MAX_HEALTH]) : 0.0);
    putd(rec, &p, (l && l->attrs.a[ATTR_MOVEMENT_SPEED].registered) ? attrs_value(&l->attrs.a[ATTR_MOVEMENT_SPEED]) : 0.0);
    putd(rec, &p, (l && l->attrs.a[ATTR_FOLLOW_RANGE].registered) ? attrs_value(&l->attrs.a[ATTR_FOLLOW_RANGE]) : 0.0);
    putd(rec, &p, (l && l->attrs.a[ATTR_ATTACK_DAMAGE].registered) ? attrs_value(&l->attrs.a[ATTR_ATTACK_DAMAGE]) : 0.0);

    putd(rec, &p, l ? l->e.pos_x : (ie_get(en->ieh) ? ie_get(en->ieh)->e.pos_x : 0.0));
    putd(rec, &p, l ? l->e.pos_y : (ie_get(en->ieh) ? ie_get(en->ieh)->e.pos_y : 0.0));
    putd(rec, &p, l ? l->e.pos_z : (ie_get(en->ieh) ? ie_get(en->ieh)->e.pos_z : 0.0));

    putd(rec, &p, l ? l->e.motion_x : (ie_get(en->ieh) ? ie_get(en->ieh)->e.motion_x : 0.0));
    putd(rec, &p, l ? l->e.motion_y : (ie_get(en->ieh) ? ie_get(en->ieh)->e.motion_y : 0.0));
    putd(rec, &p, l ? l->e.motion_z : (ie_get(en->ieh) ? ie_get(en->ieh)->e.motion_z : 0.0));

    putf(rec, &p, l ? l->rotation_yaw : (ie_get(en->ieh) ? ie_get(en->ieh)->rotation_yaw : 0.0f));
    putf(rec, &p, l ? l->rotation_pitch : (ie_get(en->ieh) ? ie_get(en->ieh)->rotation_pitch : 0.0f));
    put32(rec, &p, l ? (l->e.on_ground ? 1 : 0) : (ie_get(en->ieh) ? (ie_get(en->ieh)->e.on_ground ? 1 : 0) : 0));
    put32(rec, &p, l && l->kind == HK_SILVERFISH ? l->silverfish_ally_summon_cooldown : 0);

    /* the old-AI creature path occupies the tail for enderman and silverfish. */
    if (l && l->kind == HK_SILVERFISH) silverfish_write_extra(l, rec, &p);
    else enderman_write_extra(l, rec, &p);
    pigman_write_extra(l, rec, &p);

    assert(p == HOSTILE_STATE_BYTES);
}
