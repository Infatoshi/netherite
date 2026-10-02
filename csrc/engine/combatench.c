/* The enchantment half of melee and projectile hits: see combatench.h.
 *
 * Every draw is on the Random the Java names: the user's getRNG() for thorns,
 * bane of arthropods and unbreaking (the player twin's is the server player's
 * sv.erand, kept equal to the twin's copy around each draw), the live World.rand
 * and the SERVER Math stream for a broken item's effect. */
#include "combatench.h"
#include "env.h"

#include <stddef.h>

#include "blockcb.h"
#include "items.h"
#include "jmath.h"
#include "living.h"
#include "player.h"
#include "potion.h"
#include "survival.h"

enum {
    ENCH_FIRE_PROTECTION = 1, ENCH_THORNS = 7, ENCH_SHARPNESS = 16, ENCH_SMITE = 17,
    ENCH_BANE = 18, ENCH_KNOCKBACK = 19, ENCH_FIRE_ASPECT = 20, ENCH_LOOTING = 21, ENCH_UNBREAKING = 34
};

/* One ItemStack the helper walks: a mob's equipment slot or a player stack. */
struct item_ref {
    struct equip_slot *eq;
    struct surv_stack *st;
};

static int ref_present(const struct item_ref *r)
{
    if (r->eq) return r->eq->id > 0;
    if (r->st) return r->st->count > 0;
    return 0;
}

static int ref_tag(const struct item_ref *r)
{
    return r->eq ? r->eq->tag : r->st ? r->st->tag : 0;
}

static int ref_nench(const struct item_ref *r)
{
    return itag_nench(ref_tag(r));
}

static void ref_ench(const struct item_ref *r, int i, int *id, int *lvl)
{
    struct itag_ench e = itag_ench_at(ref_tag(r), i);
    *id = e.id;
    *lvl = e.lvl;
}

/* EnchantmentHelper.getEnchantmentLevel(id, stack). */
static int ref_level(const struct item_ref *r, int id)
{
    if (!ref_present(r)) return 0;
    for (int i = 0; i < ref_nench(r); ++i)
    {
        int e, lv;
        ref_ench(r, i, &e, &lv);
        if (e == id) return lv;
    }
    return 0;
}

static int is_player(const struct living *l)
{
    return l->kind == HK_PLAYER || l->kind == SK_PLAYER;
}

/* EntityLivingBase.getHeldItem. */
static struct item_ref held_item(const struct living *l)
{
    struct item_ref r = {NULL, NULL};
    if (l->player_sp != NULL)
    {
        int slot = l->player_sp->sv.current_item;
        if (slot >= 0 && slot < 9) r.st = &l->player_sp->sv.inv[slot];
    }
    else if (!is_player(l)) r.eq = (struct equip_slot *)&l->equip[0];
    return r;
}

/* EntityLivingBase.getLastActiveItems: the player's armour inventory (boots
 * first), else the five equipment slots. Returns how many. */
static int last_active_items(const struct living *l, struct item_ref out[5])
{
    if (l->player_sp != NULL)
    {
        for (int i = 0; i < 4; ++i) { out[i].eq = NULL; out[i].st = &l->player_sp->sv.inv[36 + i]; }
        return 4;
    }
    if (is_player(l)) return 0;
    for (int i = 0; i < 5; ++i) { out[i].eq = (struct equip_slot *)&l->equip[i]; out[i].st = NULL; }
    return 5;
}

/* Entity.getRNG. The player twin's is the server player's sv.erand (the
 * stream the survival code draws); the twin's copy follows it. */
static det_rng *user_rng(struct living *l)
{
    if (l->player_sp == NULL) return &l->rand;
    l->rand = l->player_sp->sv.erand;
    return &l->player_sp->sv.erand;
}

static void user_rng_done(struct living *l)
{
    if (l->player_sp != NULL) l->rand = l->player_sp->sv.erand;
}

struct entity *living_fire_entity(struct living *l)
{
    return l->player_sp != NULL ? &l->player_sp->e : &l->e;
}

int living_creature_attribute(const struct living *l)
{
    switch (l->kind)
    {
        case HK_ZOMBIE: case HK_PIGMAN: case HK_SKELETON: return CREATURE_UNDEAD;
        case HK_SPIDER: case HK_CAVE_SPIDER: case HK_SILVERFISH: return CREATURE_ARTHROPOD;
        default: return CREATURE_UNDEFINED;
    }
}

/* EnchantmentHelper.func_152377_a(held, attribute): EnchantmentDamage's
 * func_152376_a summed over the held item's list in order. */
float ench_modifier_living(const struct living *attacker, const struct living *target)
{
    struct item_ref held = held_item(attacker);
    float m = 0.0F;
    if (!ref_present(&held)) return m;
    int attr = living_creature_attribute(target);
    for (int i = 0; i < ref_nench(&held); ++i)
    {
        int id, lv;
        ref_ench(&held, i, &id, &lv);
        if (id == ENCH_SHARPNESS) m += (float)lv * 1.25F;
        else if (id == ENCH_SMITE && attr == CREATURE_UNDEAD) m += (float)lv * 2.5F;
        else if (id == ENCH_BANE && attr == CREATURE_ARTHROPOD) m += (float)lv * 2.5F;
    }
    return m;
}

int ench_knockback(const struct living *attacker)
{
    struct item_ref held = held_item(attacker);
    return ref_level(&held, ENCH_KNOCKBACK);
}

int ench_fire_aspect(const struct living *attacker)
{
    struct item_ref held = held_item(attacker);
    return ref_level(&held, ENCH_FIRE_ASPECT);
}

int ench_looting(const struct living *killer)
{
    if (killer == NULL || !is_player(killer)) return 0;
    struct item_ref held = held_item(killer);
    return ref_level(&held, ENCH_LOOTING);
}

int ench_max_level(const struct living *l, int id)
{
    struct item_ref items[5];
    int n = last_active_items(l, items), best = 0;
    for (int i = 0; i < n; ++i)
    {
        int lv = ref_level(&items[i], id);
        if (lv > best) best = lv;
    }
    return best;
}

int ench_fire_time(const struct living *l, int ticks)
{
    int best = ench_max_level(l, ENCH_FIRE_PROTECTION);
    if (best > 0) ticks -= mh_floor((double)((float)ticks * (float)best * 0.15F));
    return ticks;
}

double ench_blast_protection(const struct living *l, double v)
{
    int best = l != NULL ? ench_max_level(l, 3) : 0;
    if (best > 0) v -= (double)mh_floor(v * (double)((float)best * 0.15F));
    return v;
}

/* EntityLivingBase.renderBrokenItemStack on a mob: the break sound's
 * World.rand pitch, then five particles' vectors (the entity's Random and
 * the role's Math.random). */
void living_render_broken_item_stack(struct living *l, det_state *det)
{
    jrand *wr = nw_env->blockcb.env.world_rand != NULL ? nw_env->blockcb.env.world_rand
              : l->an != NULL ? &l->an->iew.world_rand.r : NULL;
    if (wr) (void)jr_float(wr);
    for (int i = 0; i < 5; ++i)
    {
        (void)det_rng_float(&l->rand);
        if (det) (void)det_math_random_role(det, det_role(det));
        (void)det_rng_float(&l->rand);
        (void)det_rng_float(&l->rand);
    }
}

/* EntityLivingBase.decreaseAirSupply: a respiration level keeps the air on
 * the user's nextInt(level + 1) > 0; EntityIronGolem's override keeps it
 * always, with no draw. */
int living_decrease_air_supply(struct living *l, int air)
{
    if (l->kind == VK_IRON_GOLEM) return air;
    int lv = ench_max_level(l, 5);   /* EnchantmentHelper.getRespiration */
    if (lv > 0)
    {
        int keep = det_rng_int_n(user_rng(l), lv + 1) > 0;
        user_rng_done(l);
        if (keep) return air;
    }
    return air - 1;
}

/* ItemStack.damageItem(amount, user). A mob's broken stack stays in its slot
 * at count 0, as Java's does. */
static void damage_item(struct living *user, struct item_ref *r, int amount, det_state *det)
{
    if (r->st != NULL)
    {
        user_rng(user);
        (void)surv_damage_stack(user->player_sp, r->st, amount);
        user_rng_done(user);
        return;
    }
    struct equip_slot *eq = r->eq;
    if (eq == NULL || eq->id <= 0 || eq->id >= 4096 || ITEMS[eq->id].max_damage <= 0) return;
    if (amount > 0)
    {
        int level = ref_level(r, ENCH_UNBREAKING), negated = 0;
        for (int i = 0; level > 0 && i < amount; ++i)
        {
            if (ITEMS[eq->id].kind == ITEM_ARMOR && det_rng_float(&user->rand) < 0.6F) continue;
            if (det_rng_int_n(&user->rand, level + 1) > 0) ++negated;
        }
        amount -= negated;
        if (amount <= 0) return;
    }
    eq->damage += amount;
    if (eq->damage <= ITEMS[eq->id].max_damage) return;

    living_render_broken_item_stack(user, det);
    --eq->count;
    if (eq->count < 0) eq->count = 0;
    eq->damage = 0;
}

/* EnchantmentThorns.func_151367_b(user, attacker, level). */
void ench_damage_equipment(struct living *l, int slot, int amount, det_state *det)
{
    struct item_ref r = {(struct equip_slot *)&l->equip[slot], NULL};
    damage_item(l, &r, amount, det);
}

static void thorns(struct living *user, struct living *attacker, int level, det_state *det)
{
    /* EnchantmentHelper.func_92099_a: the first last-active item with thorns */
    struct item_ref items[5], *worn = NULL;
    int n = last_active_items(user, items);
    for (int i = 0; i < n && worn == NULL; ++i)
        if (ref_level(&items[i], ENCH_THORNS) > 0) worn = &items[i];

    det_rng *r = user_rng(user);
    if (level > 0 && det_rng_float(r) < 0.15F * (float)level)
    {
        float amount = (float)(level > 10 ? level - 10 : 1 + det_rng_int_n(r, 4));
        user_rng_done(user);
        /* DamageSource.causeThornsDamage(user); the sound draws nothing */
        living_attack_entity_from_attacker(attacker, user, DMG_THORNS, amount, det);
        if (worn != NULL) damage_item(user, worn, 3, det);
    }
    else
    {
        user_rng_done(user);
        if (worn != NULL) damage_item(user, worn, 1, det);
    }
}

void ench_hurt_iter(struct living *user, struct living *attacker, det_state *det)
{
    struct item_ref items[6];
    int n = last_active_items(user, items);
    if (attacker != NULL && is_player(attacker)) items[n++] = held_item(user);

    for (int i = 0; i < n; ++i)
    {
        if (!ref_present(&items[i])) continue;
        for (int e = 0; e < ref_nench(&items[i]); ++e)
        {
            int id, lv;
            ref_ench(&items[i], e, &id, &lv);
            if (id == ENCH_THORNS) thorns(user, attacker, lv, det);
        }
    }
}

void ench_damage_iter(struct living *user, struct living *target, det_state *det)
{
    struct item_ref items[6];
    int n = last_active_items(user, items);
    if (is_player(user)) items[n++] = held_item(user);
    if (target == NULL) return;
    int arthropod = living_creature_attribute(target) == CREATURE_ARTHROPOD;

    for (int i = 0; i < n; ++i)
    {
        if (!ref_present(&items[i])) continue;
        for (int e = 0; e < ref_nench(&items[i]); ++e)
        {
            int id, lv;
            ref_ench(&items[i], e, &id, &lv);
            /* EnchantmentDamage.func_151368_a: bane of arthropods */
            if (id != ENCH_BANE || !arthropod) continue;
            det_rng *r = user_rng(user);
            int duration = 20 + det_rng_int_n(r, 10 * lv);
            user_rng_done(user);
            struct potion_effect eff = {
                .id = (uint8_t)POT_MOVE_SLOWDOWN,
                .duration = duration,
                .amplifier = 3,
                .is_splash = 0,
                .is_ambient = 0
            };
            living_add_potion_effect(target, &eff, det);
        }
    }
}

int mob_attack_entity_as_mob(struct living *l, struct living *target, det_state *det)
{
    float damage = (float)attrs_value(&l->attrs.a[ATTR_ATTACK_DAMAGE]);
    int knockback = 0;

    damage += ench_modifier_living(l, target);
    knockback += ench_knockback(l);

    /* DamageSource.causeMobDamage(this) */
    int hit = living_attack_entity_from_attacker(target, l, DMG_MOB, damage, det);
    if (!hit) return 0;

    if (knockback > 0)
    {
        /* Entity.addVelocity */
        target->e.motion_x += (double)(-mh_sin(l->rotation_yaw * 3.1415927F / 180.0F) * (float)knockback * 0.5F);
        target->e.motion_y += 0.1;
        target->e.motion_z += (double)(mh_cos(l->rotation_yaw * 3.1415927F / 180.0F) * (float)knockback * 0.5F);
        target->is_air_borne = 1;
        l->e.motion_x *= 0.6;
        l->e.motion_z *= 0.6;
    }

    int fire = ench_fire_aspect(l);
    if (fire > 0) living_set_fire(target, fire * 4);

    ench_hurt_iter(target, l, det);
    ench_damage_iter(l, target, det);
    return 1;
}
