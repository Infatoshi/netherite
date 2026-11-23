#include "potion.h"
#include "combatench.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "blockwl.h"
#include "living.h"
#include "det.h"

/* Static potion metadata tables */

static const int POT_BAD[POT_COUNT] = {
    0,
    0, /* 1: moveSpeed */
    1, /* 2: moveSlowdown */
    0, /* 3: digSpeed */
    1, /* 4: digSlowdown */
    0, /* 5: damageBoost */
    0, /* 6: heal */
    1, /* 7: harm */
    0, /* 8: jump */
    1, /* 9: confusion */
    0, /* 10: regeneration */
    0, /* 11: resistance */
    0, /* 12: fireResistance */
    0, /* 13: waterBreathing */
    0, /* 14: invisibility */
    1, /* 15: blindness */
    0, /* 16: nightVision */
    1, /* 17: hunger */
    1, /* 18: weakness */
    1, /* 19: poison */
    1, /* 20: wither */
    0, /* 21: healthBoost */
    0, /* 22: absorption */
    0  /* 23: saturation */
};

static const int POT_COLOR[POT_COUNT] = {
    0,
    8171462,  /* 1: moveSpeed */
    5926017,  /* 2: moveSlowdown */
    14270531, /* 3: digSpeed */
    4866583,  /* 4: digSlowdown */
    9643043,  /* 5: damageBoost */
    16262179, /* 6: heal */
    4393481,  /* 7: harm */
    7889559,  /* 8: jump */
    5578058,  /* 9: confusion */
    13458603, /* 10: regeneration */
    10044730, /* 11: resistance */
    14981690, /* 12: fireResistance */
    3035801,  /* 13: waterBreathing */
    8356754,  /* 14: invisibility */
    2039587,  /* 15: blindness */
    2039713,  /* 16: nightVision */
    5797459,  /* 17: hunger */
    4738376,  /* 18: weakness */
    5149489,  /* 19: poison */
    3484199,  /* 20: wither */
    16284963, /* 21: healthBoost */
    2445989,  /* 22: absorption */
    16262179  /* 23: saturation */
};

static const double POT_EFFECTIVENESS[POT_COUNT] = {
    0.0,
    1.0,  /* 1 */
    0.5,  /* 2 */
    1.5,  /* 3: digSpeed 1.5 */
    0.5,  /* 4 */
    1.0,  /* 5 */
    1.0,  /* 6 */
    0.5,  /* 7 */
    1.0,  /* 8 */
    0.25, /* 9: confusion 0.25 */
    0.25, /* 10: regeneration 0.25 */
    1.0,  /* 11 */
    1.0,  /* 12 */
    1.0,  /* 13 */
    1.0,  /* 14 */
    0.25, /* 15: blindness 0.25 */
    1.0,  /* 16 */
    0.5,  /* 17 */
    0.5,  /* 18 */
    0.25, /* 19: poison 0.25 */
    0.25, /* 20: wither 0.25 */
    1.0,  /* 21 */
    1.0,  /* 22 */
    1.0   /* 23 */
};

int potion_is_bad(int id)
{
    if (id <= 0 || id >= POT_COUNT) return 0;
    return POT_BAD[id];
}

int potion_liquid_color(int id)
{
    if (id <= 0 || id >= POT_COUNT) return 0;
    return POT_COLOR[id];
}

int potion_is_instant(int id)
{
    return id == POT_HEAL || id == POT_HARM || id == POT_SATURATION;
}

double potion_effectiveness(int id)
{
    if (id <= 0 || id >= POT_COUNT) return 1.0;
    return POT_EFFECTIVENESS[id];
}

int potion_is_ready(int id, int duration, int amplifier)
{
    if (id == POT_REGENERATION)
    {
        int k = 50 >> amplifier;
        return k > 0 ? (duration % k == 0) : 1;
    }
    else if (id == POT_POISON)
    {
        int k = 25 >> amplifier;
        return k > 0 ? (duration % k == 0) : 1;
    }
    else if (id == POT_WITHER)
    {
        int k = 40 >> amplifier;
        return k > 0 ? (duration % k == 0) : 1;
    }
    else if (id == POT_HUNGER)
    {
        return 1;
    }
    else if (potion_is_instant(id))
    {
        return duration >= 1;
    }
    return 0;
}

/* Potion Map (Java 8 HashMap<Integer, PotionEffect>) */

void potion_map_init(struct potion_map *m)
{
    memset(m, 0, sizeof *m);
    m->capacity = 16;
}

void potion_map_clear(struct potion_map *m)
{
    potion_map_init(m);
}

static int map_holds(const struct potion_map *m, int id)
{
    return id >= 0 && id < POT_COUNT && (m->held >> id & 1u);
}

struct potion_effect *potion_map_get(struct potion_map *m, int id)
{
    return map_holds(m, id) ? &m->eff[id] : NULL;
}

int potion_map_put(struct potion_map *m, const struct potion_effect *eff)
{
    int id = eff->id;

    if (id >= POT_COUNT) abort();

    if (map_holds(m, id))
    {
        struct potion_effect *n = &m->eff[id];

        if (eff->amplifier > n->amplifier)
        {
            n->amplifier = eff->amplifier;
            n->duration = eff->duration;
        }
        else if (eff->amplifier == n->amplifier && n->duration < eff->duration)
        {
            n->duration = eff->duration;
        }
        else if (!eff->is_ambient && n->is_ambient)
        {
            n->is_ambient = eff->is_ambient;
        }
        return 0;
    }

    /* a new key goes to its bin's tail: the latest stamp */
    m->eff[id] = *eff;
    m->seq[id] = m->stamp++;
    m->held |= 1u << id;
    m->size++;

    /* HashMap.resize at a size over 12: the bins split in order, which the
     * stamps already carry */
    if (m->size > 12 && m->capacity == 16) m->capacity = 32;

    return 1;
}

int potion_map_remove(struct potion_map *m, int id, struct potion_effect *removed)
{
    if (!map_holds(m, id)) return 0;

    if (removed) *removed = m->eff[id];
    m->held &= ~(1u << id);
    m->size--;
    return 1;
}

int potion_map_order(const struct potion_map *m, uint8_t *ids)
{
    int k = 0;

    /* 32 bins: an id per bin, ascending */
    if (m->capacity == 32)
    {
        for (uint32_t h = m->held; h != 0; h &= h - 1) ids[k++] = (uint8_t)__builtin_ctz(h);
        return k;
    }

    /* 16 bins: bin b holds b and b + 16, older first */
    for (uint32_t bins = (m->held | m->held >> 16) & 0xffffu; bins != 0; bins &= bins - 1)
    {
        int b = __builtin_ctz(bins);
        int lo = m->held >> b & 1u, hi = m->held >> (b + 16) & 1u;

        if (lo && hi && m->seq[b + 16] < m->seq[b])
        {
            ids[k++] = (uint8_t)(b + 16);
            ids[k++] = (uint8_t)b;
        }
        else
        {
            if (lo) ids[k++] = (uint8_t)b;
            if (hi) ids[k++] = (uint8_t)(b + 16);
        }
    }

    return k;
}

/* Living entity hooks & helpers */



int living_is_potion_active(const struct living *l, int id)
{
    return map_holds(&l->potions, id);
}

const struct potion_effect *living_get_potion_effect(const struct living *l, int id)
{
    return map_holds(&l->potions, id) ? &l->potions.eff[id] : NULL;
}

/* EntityLivingBase.isPotionApplicable with the per-kind overrides this world's
 * kinds carry: EntitySpider (and so EntityCaveSpider) refuses poison on
 * itself, the undead kinds (getCreatureAttribute UNDEAD: the zombie, the
 * pigman, the skeleton) refuse regeneration and poison. The check is the
 * TARGET's, not the applier's. */
static int potion_applicable_to(const struct living *l, int potion_id)
{
    if (l->kind == HK_SPIDER || l->kind == HK_CAVE_SPIDER) return spider_is_potion_applicable(l, potion_id);

    if (living_creature_attribute(l) == CREATURE_UNDEAD)
        return potion_id != POT_POISON && potion_id != POT_REGENERATION;

    return 1;
}

void living_add_potion_effect(struct living *l, const struct potion_effect *eff, det_state *det)
{
    /* EntityLivingBase.addPotionEffect's isPotionApplicable gate. */
    if (!potion_applicable_to(l, eff->id)) return;

    (void)det;
    int is_new = potion_map_put(&l->potions, eff);
    l->potions_need_update = 1;

    if (is_new)
    {
        potion_apply_attributes(l, eff->id, eff->amplifier);
        /* onNewPotionEffect: EntityPlayerMP sends the S1D */
        if (l->on_potion_event)
            l->on_potion_event(l->on_potion_event_ctx, 0, eff->id, eff->amplifier, eff->duration);
    }
    else
    {
        potion_remove_attributes(l, eff->id, eff->amplifier);
        potion_apply_attributes(l, eff->id, eff->amplifier);
        /* onChangedPotionEffect(existing, true): the S1D rides on the merged
         * effect, whose map row potion_map_put just updated */
        if (l->on_potion_event)
        {
            const struct potion_effect *cur = living_get_potion_effect(l, eff->id);
            if (cur)
                l->on_potion_event(l->on_potion_event_ctx, 1, cur->id, cur->amplifier, cur->duration);
        }
    }
}

void living_remove_potion_effect(struct living *l, int id, det_state *det)
{
    (void)det;
    struct potion_effect removed;
    if (potion_map_remove(&l->potions, id, &removed))
    {
        l->potions_need_update = 1;
        potion_remove_attributes(l, removed.id, removed.amplifier);
        /* onFinishedPotionEffect: EntityPlayerMP sends the S1E */
        if (l->on_potion_event)
            l->on_potion_event(l->on_potion_event_ctx, 2, removed.id, removed.amplifier, removed.duration);
    }
}

void living_clear_active_potions(struct living *l, det_state *det)
{
    (void)det;
    uint8_t ids[POT_COUNT];
    int n = potion_map_order(&l->potions, ids);

    for (int i = 0; i < n; i++)
    {
        const struct potion_effect *e = &l->potions.eff[ids[i]];

        potion_remove_attributes(l, e->id, e->amplifier);
        if (l->on_potion_event)
            l->on_potion_event(l->on_potion_event_ctx, 2, e->id, e->amplifier, e->duration);
    }
    potion_map_init(&l->potions);
    l->potions_need_update = 1;
}

void potion_perform_effect(struct living *l, int id, int amplifier, det_state *det)
{
    if (id == POT_REGENERATION)
    {
        if (l->health < living_max_health(l))
        {
            living_heal(l, 1.0f);
        }
    }
    else if (id == POT_POISON)
    {
        if (l->health > 1.0f)
        {
            living_attack_entity_from(l, DMG_MAGIC, 1.0f, det);
        }
    }
    else if (id == POT_WITHER)
    {
        living_attack_entity_from(l, DMG_WITHER, 1.0f, det);
    }
    else if ((id == POT_HEAL || id == POT_HARM) && (id == POT_HEAL) != (living_creature_attribute(l) == CREATURE_UNDEAD))
    {
        /* Potion.performEffect: heal heals, harm hurts; an undead target
         * swaps them (isEntityUndead) */
        living_heal(l, (float)(4 << amplifier));
    }
    else if (id == POT_HEAL || id == POT_HARM)
    {
        living_attack_entity_from(l, DMG_MAGIC, (float)(6 << amplifier), det);
    }
}

void potion_affect_entity(struct living *target, int id, int amplifier, double distance_falloff,
                          struct living *source, det_state *det)
{
    /* EntityLivingBase.isEntityUndead: getCreatureAttribute is UNDEAD */
    int is_undead = living_creature_attribute(target) == CREATURE_UNDEAD;
    if ((id != POT_HEAL || is_undead) && (id != POT_HARM || !is_undead))
    {
        if ((id == POT_HARM && !is_undead) || (id == POT_HEAL && is_undead))
        {
            int amt = (int)(distance_falloff * (double)(6 << amplifier) + 0.5);
            /* DamageSource.magic without a thrower, else
             * causeIndirectMagicDamage (getEntity() is the thrower) */
            living_attack_entity_from_attacker(target, source, source != NULL ? DMG_INDIRECT_MAGIC : DMG_MAGIC,
                                               (float)amt, det);
        }
    }
    else
    {
        int amt = (int)(distance_falloff * (double)(4 << amplifier) + 0.5);
        living_heal(target, (float)amt);
    }
}

/* Attribute modifiers application and removal */

void potion_apply_attributes(struct living *l, int id, int amplifier)
{
    /* applyModifier flags the instance for the tracker's S20 */
    if (id == POT_MOVE_SPEED || id == POT_MOVE_SLOWDOWN || id == POT_HEALTH_BOOST) l->attr_watch_dirty = 1;
    if (id == POT_MOVE_SPEED)
    {
        struct attr_mod m;
        memset(&m, 0, sizeof m);
        m.uuid_msb = (int64_t)0x91aeaa56376b4498ULL;
        m.uuid_lsb = (int64_t)0x935b2f7f68070635ULL;
        { char nm[ATTR_NAME_LEN]; snprintf(nm, sizeof nm, "potion.moveSpeed %d", amplifier); m.name = attr_name_id(nm); }
        m.amount = 0.20000000298023224 * (double)(amplifier + 1);
        m.operation = 2;
        m.saved = 1;
        attrs_remove(&l->attrs.a[ATTR_MOVEMENT_SPEED], &m);
        attrs_apply(&l->attrs.a[ATTR_MOVEMENT_SPEED], &m);
    }
    else if (id == POT_MOVE_SLOWDOWN)
    {
        struct attr_mod m;
        memset(&m, 0, sizeof m);
        m.uuid_msb = (int64_t)0x7107de5e7ce84030ULL;
        m.uuid_lsb = (int64_t)0x940e514c1f160890ULL;
        { char nm[ATTR_NAME_LEN]; snprintf(nm, sizeof nm, "potion.moveSlowdown %d", amplifier); m.name = attr_name_id(nm); }
        m.amount = -0.15000000596046448 * (double)(amplifier + 1);
        m.operation = 2;
        m.saved = 1;
        attrs_remove(&l->attrs.a[ATTR_MOVEMENT_SPEED], &m);
        attrs_apply(&l->attrs.a[ATTR_MOVEMENT_SPEED], &m);
    }
    else if (id == POT_HEALTH_BOOST)
    {
        struct attr_mod m;
        memset(&m, 0, sizeof m);
        m.uuid_msb = (int64_t)0x5d6f0ba2118646acULL;
        m.uuid_lsb = (int64_t)0xb896c61c5cee99ccULL;
        { char nm[ATTR_NAME_LEN]; snprintf(nm, sizeof nm, "potion.healthBoost %d", amplifier); m.name = attr_name_id(nm); }
        m.amount = 4.0 * (double)(amplifier + 1);
        m.operation = 0;
        m.saved = 1;
        attrs_remove(&l->attrs.a[ATTR_MAX_HEALTH], &m);
        attrs_apply(&l->attrs.a[ATTR_MAX_HEALTH], &m);
    }
    else if (id == POT_ABSORPTION)
    {
        living_set_absorption(l, l->absorption + (float)(4 * (amplifier + 1)));
    }
    else if (id == POT_DAMAGE_BOOST)
    {
        if (l->attrs.a[ATTR_ATTACK_DAMAGE].registered)
        {
            struct attr_mod m;
            memset(&m, 0, sizeof m);
            m.uuid_msb = (int64_t)0x648d70646a604f59ULL;
            m.uuid_lsb = (int64_t)0x8abec2c23a6dd7a9ULL;
            { char nm[ATTR_NAME_LEN]; snprintf(nm, sizeof nm, "potion.damageBoost %d", amplifier); m.name = attr_name_id(nm); }
            m.amount = 1.3 * (double)(amplifier + 1);
            m.operation = 2;
            m.saved = 1;
            attrs_remove(&l->attrs.a[ATTR_ATTACK_DAMAGE], &m);
            attrs_apply(&l->attrs.a[ATTR_ATTACK_DAMAGE], &m);
        }
    }
    else if (id == POT_WEAKNESS)
    {
        if (l->attrs.a[ATTR_ATTACK_DAMAGE].registered)
        {
            struct attr_mod m;
            memset(&m, 0, sizeof m);
            m.uuid_msb = (int64_t)0x22653b89116e49dcULL;
            m.uuid_lsb = (int64_t)0x9b6b9971489b5be5ULL;
            { char nm[ATTR_NAME_LEN]; snprintf(nm, sizeof nm, "potion.weakness %d", amplifier); m.name = attr_name_id(nm); }
            m.amount = (double)(-0.5f * (float)(amplifier + 1));
            m.operation = 0;
            m.saved = 1;
            attrs_remove(&l->attrs.a[ATTR_ATTACK_DAMAGE], &m);
            attrs_apply(&l->attrs.a[ATTR_ATTACK_DAMAGE], &m);
        }
    }
}

void potion_remove_attributes(struct living *l, int id, int amplifier)
{
    /* removeModifier flags the instance too */
    if (id == POT_MOVE_SPEED || id == POT_MOVE_SLOWDOWN || id == POT_HEALTH_BOOST) l->attr_watch_dirty = 1;
    if (id == POT_MOVE_SPEED)
    {
        struct attr_mod m;
        memset(&m, 0, sizeof m);
        m.uuid_msb = (int64_t)0x91aeaa56376b4498ULL;
        m.uuid_lsb = (int64_t)0x935b2f7f68070635ULL;
        attrs_remove(&l->attrs.a[ATTR_MOVEMENT_SPEED], &m);
    }
    else if (id == POT_MOVE_SLOWDOWN)
    {
        struct attr_mod m;
        memset(&m, 0, sizeof m);
        m.uuid_msb = (int64_t)0x7107de5e7ce84030ULL;
        m.uuid_lsb = (int64_t)0x940e514c1f160890ULL;
        attrs_remove(&l->attrs.a[ATTR_MOVEMENT_SPEED], &m);
    }
    else if (id == POT_HEALTH_BOOST)
    {
        struct attr_mod m;
        memset(&m, 0, sizeof m);
        m.uuid_msb = (int64_t)0x5d6f0ba2118646acULL;
        m.uuid_lsb = (int64_t)0xb896c61c5cee99ccULL;
        attrs_remove(&l->attrs.a[ATTR_MAX_HEALTH], &m);
        if (l->health > living_max_health(l))
        {
            living_set_health(l, living_max_health(l));
        }
    }
    else if (id == POT_ABSORPTION)
    {
        living_set_absorption(l, l->absorption - (float)(4 * (amplifier + 1)));
    }
    else if (id == POT_DAMAGE_BOOST)
    {
        if (l->attrs.a[ATTR_ATTACK_DAMAGE].registered)
        {
            struct attr_mod m;
            memset(&m, 0, sizeof m);
            m.uuid_msb = (int64_t)0x648d70646a604f59ULL;
            m.uuid_lsb = (int64_t)0x8abec2c23a6dd7a9ULL;
            attrs_remove(&l->attrs.a[ATTR_ATTACK_DAMAGE], &m);
        }
    }
    else if (id == POT_WEAKNESS)
    {
        if (l->attrs.a[ATTR_ATTACK_DAMAGE].registered)
        {
            struct attr_mod m;
            memset(&m, 0, sizeof m);
            m.uuid_msb = (int64_t)0x22653b89116e49dcULL;
            m.uuid_lsb = (int64_t)0x9b6b9971489b5be5ULL;
            attrs_remove(&l->attrs.a[ATTR_ATTACK_DAMAGE], &m);
        }
    }
}

void living_update_potion_effects(struct living *l, det_state *det)
{
    /* the keySet iterator's walk; an expired effect leaves through
     * iterator.remove, which does not disturb the rest of the walk */
    uint8_t ids[POT_COUNT];
    int n = potion_map_order(&l->potions, ids);

    for (int i = 0; i < n; i++)
    {
        if (!map_holds(&l->potions, ids[i])) continue;

        struct potion_effect *node = &l->potions.eff[ids[i]];
        int still_active = 0;

        if (node->duration > 0)
        {
            if (potion_is_ready(node->id, node->duration, node->amplifier))
            {
                potion_perform_effect(l, node->id, node->amplifier, det);
            }
            --node->duration;
            if (node->duration > 0) still_active = 1;
        }

        if (!still_active)
        {
            struct potion_effect finished = *node;
            potion_map_remove(&l->potions, finished.id, NULL);

            l->potions_need_update = 1;
            potion_remove_attributes(l, finished.id, finished.amplifier);
            /* onFinishedPotionEffect: EntityPlayerMP sends the S1E */
            if (l->on_potion_event)
                l->on_potion_event(l->on_potion_event_ctx, 2, finished.id, finished.amplifier, finished.duration);
        }
        else if (node->duration % 600 == 0)
        {
            l->potions_need_update = 1;
            /* onChangedPotionEffect(effect, false): the S1D rides with
             * the current remaining duration */
            if (l->on_potion_event)
                l->on_potion_event(l->on_potion_event_ctx, 1, node->id, node->amplifier, node->duration);
        }
    }

    if (l->potions_need_update)
    {
        if (l->potions.size == 0)
        {
            l->potion_is_ambient = 0;
            l->potion_liquid_color = 0;
            living_set_invisible(l, 0);
        }
        else
        {
            l->potion_liquid_color = potion_calc_liquid_color(&l->potions);
            l->potion_is_ambient = (uint8_t)potion_all_ambient(&l->potions);
            living_set_invisible(l, living_is_potion_active(l, POT_INVISIBILITY));
        }
        l->potions_need_update = 0;
    }

    int color = l->potion_liquid_color;
    int is_ambient = l->potion_is_ambient;

    if (color > 0)
    {
        int draw = 0;
        if (!living_is_invisible(l))
        {
            draw = det_rng_bool(&l->rand);
        }
        else
        {
            draw = (det_rng_int_n(&l->rand, 15) == 0);
        }

        if (is_ambient)
        {
            draw = draw && (det_rng_int_n(&l->rand, 5) == 0);
        }

        if (draw && color > 0)
        {
            det_rng_double(&l->rand);
            det_rng_double(&l->rand);
            det_rng_double(&l->rand);
        }
    }
}

int potion_calc_liquid_color(const struct potion_map *m)
{
    if (m->size == 0) return 3694022;
    float r = 0.0f, g = 0.0f, b = 0.0f, count = 0.0f;
    uint8_t ids[POT_COUNT];
    int n = potion_map_order(m, ids);
    for (int i = 0; i < n; i++)
    {
        const struct potion_effect *e = &m->eff[ids[i]];
        int c = potion_liquid_color(e->id);
        for (int a = 0; a <= e->amplifier; a++)
        {
            r += (float)((c >> 16) & 255) / 255.0f;
            g += (float)((c >> 8) & 255) / 255.0f;
            b += (float)(c & 255) / 255.0f;
            count += 1.0f;
        }
    }
    r = r / count * 255.0f;
    g = g / count * 255.0f;
    b = b / count * 255.0f;
    return ((int)r << 16) | ((int)g << 8) | (int)b;
}

int potion_all_ambient(const struct potion_map *m)
{
    if (m->size == 0) return 1;
    for (int id = 0; id < POT_COUNT; id++)
        if ((m->held >> id & 1u) && !m->eff[id].is_ambient) return 0;
    return 1;
}

/* PotionHelper parser */

static int check_flag(int damage, int bit)
{
    return (damage & (1 << bit)) != 0;
}

static int is_flag_set(int damage, int bit)
{
    return check_flag(damage, bit) ? 1 : 0;
}

static int is_flag_unset(int damage, int bit)
{
    return check_flag(damage, bit) ? 0 : 1;
}

static int count_set_flags(int v)
{
    int count = 0;
    for (; v > 0; count++)
    {
        v &= v - 1;
    }
    return count;
}

static int helper_eval(int is_unset, int is_multiplier, int is_negative, int cmp_op, int count_val, int mult_val, int damage)
{
    int v = 0;
    if (is_unset)
    {
        v = is_flag_unset(damage, count_val);
    }
    else if (cmp_op != -1)
    {
        int set_count = count_set_flags(damage);
        if (cmp_op == 0 && set_count == count_val) v = 1;
        else if (cmp_op == 1 && set_count > count_val) v = 1;
        else if (cmp_op == 2 && set_count < count_val) v = 1;
    }
    else
    {
        v = is_flag_set(damage, count_val);
    }
    if (is_multiplier) v *= mult_val;
    if (is_negative) v *= -1;
    return v;
}

/* PotionHelper.parsePotionEffects' term, a range with no '|' or '&'. */
static int parse_potion_term(const char *s, int start, int end, int damage)
{
    int var6 = 0, var7 = 0, var8 = 0, var9 = 0, var10 = 0;
    int var11 = -1, var12 = 0, var13 = 0, var14 = 0;

    for (int i = start; i < end; i++)
    {
        char ch = s[i];
        if (ch >= '0' && ch <= '9')
        {
            if (var6)
            {
                var13 = ch - '0';
                var7 = 1;
            }
            else
            {
                var12 = var12 * 10 + (ch - '0');
                var8 = 1;
            }
        }
        else if (ch == '*')
        {
            var6 = 1;
        }
        else if (ch == '!')
        {
            if (var8)
            {
                var14 += helper_eval(var9, var7, var10, var11, var12, var13, damage);
                var9 = var10 = var6 = var7 = var8 = 0;
                var13 = var12 = 0;
                var11 = -1;
            }
            var9 = 1;
        }
        else if (ch == '-')
        {
            if (var8)
            {
                var14 += helper_eval(var9, var7, var10, var11, var12, var13, damage);
                var9 = var10 = var6 = var7 = var8 = 0;
                var13 = var12 = 0;
                var11 = -1;
            }
            var10 = 1;
        }
        else if (ch != '=' && ch != '<' && ch != '>')
        {
            if (ch == '+' && var8)
            {
                var14 += helper_eval(var9, var7, var10, var11, var12, var13, damage);
                var9 = var10 = var6 = var7 = var8 = 0;
                var13 = var12 = 0;
                var11 = -1;
            }
        }
        else
        {
            if (var8)
            {
                var14 += helper_eval(var9, var7, var10, var11, var12, var13, damage);
                var9 = var10 = var6 = var7 = var8 = 0;
                var13 = var12 = 0;
                var11 = -1;
            }
            if (ch == '=') var11 = 0;
            else if (ch == '<') var11 = 2;
            else if (ch == '>') var11 = 1;
        }
    }
    if (var8)
    {
        var14 += helper_eval(var9, var7, var10, var11, var12, var13, damage);
    }
    return var14;
}

/* PotionHelper.parsePotionEffects: split at the first '|' (the left side if
 * it is positive, else the right), else at the first '&' (0 unless both sides
 * are, then the larger), else the term. Java recurses; this evaluates the
 * same splits in the same order on a stack, one frame per open split (a
 * requirement string has a handful). */
#define POTION_SPLITS 32

static int parse_potion_effects(const char *s, int start, int end, int damage)
{
    struct { int start, end, split; char op; int stage, left; } st[POTION_SPLITS];
    int sp = 0, ret = 0, len = (int)strlen(s);

    st[0].start = start;
    st[0].end = end;
    st[0].stage = 0;

    for (;;)
    {
        int f = sp;

        if (st[f].stage == 0)
        {
            int a = st[f].start, b = st[f].end, at = -1;

            if (a >= len || b < 0 || a >= b)
            {
                ret = 0;
                goto done;
            }

            for (int i = a; i < b && at < 0; i++) if (s[i] == '|') { at = i; st[f].op = '|'; }
            for (int i = a; i < b && at < 0; i++) if (s[i] == '&') { at = i; st[f].op = '&'; }

            if (at < 0)
            {
                ret = parse_potion_term(s, a, b, damage);
                goto done;
            }

            /* the left side first */
            if (sp + 1 >= POTION_SPLITS) { fprintf(stderr, "potion: requirement nests too deep\n"); abort(); }
            st[f].split = at;
            st[f].stage = 1;
            ++sp;
            WL_DEPTH_NOTE(potion, sp + 1);
            st[sp].start = a;
            st[sp].end = at - 1;
            st[sp].stage = 0;
            continue;
        }

        if (st[f].stage == 1)
        {
            /* ret is the left side's */
            if (st[f].op == '|' && ret > 0) goto done;
            if (st[f].op == '&' && ret <= 0) { ret = 0; goto done; }

            st[f].left = ret;
            st[f].stage = 2;
            ++sp;
            st[sp].start = st[f].split + 1;
            st[sp].end = st[f].end;
            st[sp].stage = 0;
            continue;
        }

        /* ret is the right side's */
        if (st[f].op == '|') ret = ret > 0 ? ret : 0;
        else ret = ret <= 0 ? 0 : (st[f].left > ret ? st[f].left : ret);

    done:
        if (sp == 0) return ret;
        --sp;
    }
}

struct potion_req {
    int id;
    const char *req;
    const char *amp;
    int is_instant;
    double effectiveness;
};

static const struct potion_req REQS[] = {
    {POT_MOVE_SPEED, "!0 & 1 & !2 & !3 & 1+6", "5", 0, 1.0},
    {POT_MOVE_SLOWDOWN, "!0 & 1 & !2 & 3 & 3+6", NULL, 0, 0.5},
    {POT_DAMAGE_BOOST, "0 & !1 & !2 & 3 & 3+6", "5", 0, 1.0},
    {POT_HEAL, "0 & !1 & 2 & !3", "5", 1, 1.0},
    {POT_HARM, "!0 & !1 & 2 & 3", "5", 1, 0.5},
    {POT_REGENERATION, "0 & !1 & !2 & !3 & 0+6", "5", 0, 0.25},
    {POT_RESISTANCE, NULL, "5", 0, 1.0},
    {POT_FIRE_RESISTANCE, "0 & 1 & !2 & !3 & 0+6", NULL, 0, 1.0},
    {POT_WATER_BREATHING, "0 & !1 & 2 & 3 & 2+6", NULL, 0, 1.0},
    {POT_INVISIBILITY, "!0 & 1 & 2 & 3 & 2+6", NULL, 0, 1.0},
    {POT_NIGHT_VISION, "!0 & 1 & 2 & !3 & 2+6", NULL, 0, 1.0},
    {POT_WEAKNESS, "!0 & !1 & !2 & 3 & 3+6", NULL, 0, 0.5},
    {POT_POISON, "!0 & !1 & 2 & !3 & 2+6", "5", 0, 0.25}
};

int potion_get_effects_for_damage(int damage, struct potion_effect *out, int max_out)
{
    int nout = 0;
    int is_splash = (damage & 16384) != 0;

    for (int i = 0; i < (int)(sizeof(REQS)/sizeof(REQS[0])); i++)
    {
        if (!REQS[i].req) continue;
        if (nout >= max_out) break;

        int req_len = (int)strlen(REQS[i].req);
        int v8 = parse_potion_effects(REQS[i].req, 0, req_len, damage);
        if (v8 > 0)
        {
            int v9 = 0;
            if (REQS[i].amp)
            {
                int amp_len = (int)strlen(REQS[i].amp);
                v9 = parse_potion_effects(REQS[i].amp, 0, amp_len, damage);
                if (v9 < 0) v9 = 0;
            }
            if (REQS[i].is_instant)
            {
                v8 = 1;
            }
            else
            {
                v8 = 1200 * (v8 * 3 + (v8 - 1) * 2);
                v8 >>= v9;
                v8 = (int)round((double)v8 * REQS[i].effectiveness);
                if (is_splash)
                {
                    v8 = (int)round((double)v8 * 0.75 + 0.5);
                }
            }
            out[nout].id = (uint8_t)REQS[i].id;
            out[nout].duration = v8;
            out[nout].amplifier = (int8_t)v9;
            out[nout].is_splash = (uint8_t)is_splash;
            out[nout].is_ambient = 0;
            nout++;
        }
    }
    return nout;
}

/* ItemFood.onFoodEaten and its overrides (ItemAppleGold, ItemFishFood), the
 * server half: the effects a finished food gives the eater, in Java's
 * addPotionEffect order. world_rand is World.rand: ItemFood's
 * rand.nextFloat() < potionEffectProbability roll is drawn whenever the
 * item's potionId > 0 (the plain apple's regeneration, raw chicken, rotten
 * flesh, spider eye, poisonous potato); NULL applies every effect without a
 * draw (PotionProbe.applyFoodToEntity). Returns the addPotionEffect calls. */
static int food_effect(struct living *l, int id, int duration, int amplifier, det_state *det)
{
    struct potion_effect eff = { .id = id, .duration = duration, .amplifier = amplifier, .is_splash = 0,
                                 .is_ambient = 0 };
    living_add_potion_effect(l, &eff, det);
    return 1;
}

int potion_apply_food(struct living *l, int item_id, int item_damage, det_state *det, jrand *world_rand)
{
    int n = 0;
    int potion_id = 0, duration = 0, amplifier = 0;
    float probability = 1.0F;

    if (item_id == 322) /* ItemAppleGold */
    {
        n += food_effect(l, POT_ABSORPTION, 2400, 0, det);

        if (item_damage > 0) /* enchanted: no super call, no draw */
        {
            n += food_effect(l, POT_REGENERATION, 600, 4, det);
            n += food_effect(l, POT_RESISTANCE, 6000, 0, det);
            n += food_effect(l, POT_FIRE_RESISTANCE, 6000, 0, det);
            return n;
        }

        /* super.onFoodEaten: setPotionEffect(regeneration, 5, 1, 1.0F) */
        potion_id = POT_REGENERATION; duration = 100; amplifier = 1; probability = 1.0F;
    }
    else if (item_id == 349 && item_damage == 3) /* ItemFishFood's pufferfish; potionId 0 */
    {
        n += food_effect(l, POT_POISON, 1200, 3, det);
        n += food_effect(l, POT_HUNGER, 300, 2, det);
        n += food_effect(l, POT_CONFUSION, 300, 1, det);
        return n;
    }
    /* Item.java's setPotionEffect(id, seconds, amplifier, probability) */
    else if (item_id == 365) { potion_id = POT_HUNGER; duration = 600; amplifier = 0; probability = 0.3F; }
    else if (item_id == 367) { potion_id = POT_HUNGER; duration = 600; amplifier = 0; probability = 0.8F; }
    else if (item_id == 375) { potion_id = POT_POISON; duration = 100; amplifier = 0; probability = 1.0F; }
    else if (item_id == 394) { potion_id = POT_POISON; duration = 100; amplifier = 0; probability = 0.6F; }
    else return n;

    if (world_rand != NULL && !(jr_float(world_rand) < probability)) return n;

    return n + food_effect(l, potion_id, duration, amplifier, det);
}
