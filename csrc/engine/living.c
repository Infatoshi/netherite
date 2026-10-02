/* EntityLivingBase, EntityLiving, EntityCreature, EntityAgeable and EntityAnimal,
 * bit for bit, plus the entity list the probe ticks and the canonical NBT the
 * records carry. See living.h for what is ported and from where.
 *
 * Every Java literal is copied as printed, and every statement that reads state
 * the previous statement wrote stays in vanilla's order: Java evaluates operands
 * left to right, C does not, so the draws and the multi-step expressions are
 * split. */
#include "living.h"
#include "entityquery.h"
#include "env.h"
#include "grave.h"
#include "riding.h"
#include "leash.h"
#include "projectile.h"

#include "ghasts.h"
#include "villagers.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#include "animals.h"
#include "slimes.h"
#include "hostiles_spider.h"
#include "hostiles_creeper.h"
#include "hostiles_enderman.h"
#include "hostiles_pigman.h"
#include "hostiles_silverfish.h"
#include "blockcb.h"
#include "combatench.h"
#include "blocks.h"
#include "biomes.h"
#include "features_lakes.h"
#include "collide.h"
#include "items.h"
#include "jmath.h"
#include "nbtjson.h"
#include "pathfind.h"
#include "portal.h"
#include "projectile.h"
#include "randomtick.h"
#include "nbtw.h"
#include "smath.h"
#include "trace.h"
#include "world.h"

/* --------------------------------------------------------------- helpers */

static void living_collide_with_nearby_entities(struct living *l);
int world_is_client(struct world *w);
static int handle_lava_movement(struct living *l);
static int living_has_living_sound(struct living *l);
static int decrease_air_supply(struct living *l);
static int is_wet(struct living *l);

static double sqrt_double(double v)
{
    return (double)(float)sqrt(v);
}

/* MathHelper.sqrt_float: (float)Math.sqrt((double)f). */
static float sqrt_float(float f)
{
    return (float)sqrt((double)f);
}

static float wrap_angle_float(float a)
{
    /* fmodf is exact: an angle inside (-360, 360) is its own remainder */
    if (!(a > -360.0F && a < 360.0F)) a = fmodf(a, 360.0F);
    if (a >= 180.0F) a -= 360.0F;
    if (a < -180.0F) a += 360.0F;
    return a;
}


static int ceil_float_int(float f)
{
    int i = (int)f;
    return f > (float)i ? i + 1 : i;
}

static double abs_max(double a, double b)
{
    if (a < 0.0) a = -a;
    if (b < 0.0) b = -b;
    return a > b ? a : b;
}

static float clamp_float(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static float mh_sin_deg(float deg)
{
    return mh_sin(deg * 3.1415927F / 180.0F);
}

static float mh_cos_deg(float deg)
{
    return mh_cos(deg * 3.1415927F / 180.0F);
}


/* MathHelper.abs is used on the motion in a few places. */
static double j_abs(double v)
{
    return v < 0.0 ? -v : v;
}

/* ------------------------------------------------------------ attributes */

static const char *ATTR_NAME[ATTR_COUNT] = {
    "generic.maxHealth", "generic.knockbackResistance", "generic.movementSpeed", "generic.followRange", "generic.attackDamage", "zombie.spawnReinforcements"
};
static const double ATTR_DEFAULT[ATTR_COUNT] = {20.0, 0.0, 0.699999988079071, 32.0, 2.0, 0.0};
static const double ATTR_MIN[ATTR_COUNT] = {0.0, 0.0, 0.0, 0.0};
static const double ATTR_MAXV[ATTR_COUNT] = {1.7976931348623157e308, 1.0, 1.7976931348623157e308, 2048.0, 2048.0, 1.0};

static void attrs_instance_init(struct attr_instance *a, int which)
{
    memset(a, 0, sizeof *a);
    a->which = which;
    a->registered = (which < 4);
    a->base = ATTR_DEFAULT[which];
    a->cached = ATTR_DEFAULT[which];
    a->needs_update = 1;
}

/* LowerStringMap's get: both sides lower-cased (String.toLowerCase; every
 * attribute name and description is ASCII). */
static int lower_equal(const char *a, const char *b)
{
    for (; *a && *b; ++a, ++b)
        if ((*a >= 'A' && *a <= 'Z' ? *a + 32 : *a) != (*b >= 'A' && *b <= 'Z' ? *b + 32 : *b)) return 0;
    return *a == *b;
}

/* ServersideAttributeMap.getAttributeInstanceByName: attributesByName (the
 * unlocalized names), then descriptionToAttributeInstanceMap (the
 * RangedAttribute descriptions; attackDamage has none), both LowerStringMaps
 * keyed at registerAttribute. */
int attrs_index_by_name(const char *name)
{
    static const char *const desc[ATTR_COUNT] = {
        "Max Health", "Knockback Resistance", "Movement Speed", "Follow Range", NULL, "Spawn Reinforcements Chance"
    };
    if (name == NULL) return -1;
    for (int j = 0; j < ATTR_COUNT; ++j)
        if (lower_equal(ATTR_NAME[j], name)) return j;
    for (int j = 0; j < ATTR_COUNT; ++j)
        if (desc[j] != NULL && lower_equal(desc[j], name)) return j;
    return -1;
}

static const char *const ATTR_NAMES[ATTR_NAMES_FIXED] = {
    [MODN_NONE] = "",
    [MODN_RANDOM_SPAWN_BONUS] = "Random spawn bonus",
    [MODN_RANDOM_ZOMBIE_SPAWN_BONUS] = "Random zombie-spawn bonus",
    [MODN_LEADER_ZOMBIE_BONUS] = "Leader zombie bonus",
    [MODN_ZOMBIE_CALLER_CHARGE] = "Zombie reinforcement caller charge",
    [MODN_ZOMBIE_CALLEE_CHARGE] = "Zombie reinforcement callee charge",
    [MODN_FLEEING_SPEED_BONUS] = "Fleeing speed bonus",
    [MODN_ATTACKING_SPEED_BOOST] = "Attacking speed boost",
    [MODN_DRINKING_SPEED_PENALTY] = "Drinking speed penalty",
    [MODN_BABY_SPEED_BOOST] = "Baby speed boost",
    [MODN_WEAPON_MODIFIER] = "Weapon modifier",
    [MODN_TOOL_MODIFIER] = "Tool modifier",
};

uint16_t attr_name_id(const char *name)
{
    char cut[ATTR_NAME_LEN];

    /* the 40-byte field the name used to be held in kept its first 39 */
    snprintf(cut, sizeof cut, "%s", name ? name : "");
    for (int i = 0; i < ATTR_NAMES_FIXED; ++i)
        if (!strcmp(ATTR_NAMES[i], cut)) return (uint16_t)i;

    int n = nw_env->attr_names.n;
    for (int i = 0; i < n; ++i)
        if (!strcmp(nw_env->attr_names.s[i], cut)) return (uint16_t)(ATTR_NAMES_FIXED + i);
    if (n == ATTR_NAMES_ENV) list_full("attribute modifier names", ATTR_NAMES_ENV);
    memcpy(nw_env->attr_names.s[n], cut, sizeof cut);
    nw_env->attr_names.n = n + 1;
    return (uint16_t)(ATTR_NAMES_FIXED + n);
}

const char *attr_name(uint16_t id)
{
    if (id < ATTR_NAMES_FIXED) return ATTR_NAMES[id];
    if (id - ATTR_NAMES_FIXED >= nw_env->attr_names.n) return "";
    return nw_env->attr_names.s[id - ATTR_NAMES_FIXED];
}

void attrs_init(struct attr_map *m)
{
    for (int i = 0; i < ATTR_COUNT; ++i) attrs_instance_init(&m->a[i], i);
}

struct attr_instance *attrs_get(struct attr_map *m, int attr)
{
    return &m->a[attr];
}

static double attrs_clamp(const struct attr_instance *a, double v)
{
    if (v < ATTR_MIN[a->which]) v = ATTR_MIN[a->which];
    if (v > ATTR_MAXV[a->which]) v = ATTR_MAXV[a->which];
    return v;
}

/* ModifiableAttributeInstance.computeValue: operation 0 adds to the base, 1 adds
 * base * amount, 2 multiplies by 1 + amount, then the attribute clamps. */
static double attrs_compute(const struct attr_instance *a)
{
    double v = a->base;

    for (int i = 0; i < a->nmods; ++i)
        if (a->mods[i].operation == 0) v += a->mods[i].amount;

    double r = v;

    for (int i = 0; i < a->nmods; ++i)
        if (a->mods[i].operation == 1) r += v * a->mods[i].amount;

    for (int i = 0; i < a->nmods; ++i)
        if (a->mods[i].operation == 2) r *= 1.0 + a->mods[i].amount;

    return attrs_clamp(a, r);
}

double attrs_value(struct attr_instance *a)
{
    if (a->needs_update)
    {
        a->cached = attrs_compute(a);
        a->needs_update = 0;
    }

    return a->cached;
}

void attrs_set_base(struct attr_instance *a, double v)
{
    a->registered = 1;
    if (v != a->base)
    {
        a->base = v;
        a->needs_update = 1;
    }
}

static int mod_same(const struct attr_mod *a, const struct attr_mod *b)
{
    return a->uuid_msb == b->uuid_msb && a->uuid_lsb == b->uuid_lsb;
}

void attrs_apply(struct attr_instance *a, const struct attr_mod *mod)
{
    for (int i = 0; i < a->nmods; ++i)
        if (mod_same(&a->mods[i], mod)) return;   /* IllegalArgumentException in Java */

    if (a->nmods < ATTR_MAX_MODS) a->mods[a->nmods++] = *mod;
    a->needs_update = 1;
}

/* ModifiableAttributeInstance.removeModifier: out of all three operation sets,
 * the name map and the UUID map, which for one flat list is every entry whose
 * UUID matches. */
void attrs_remove(struct attr_instance *a, const struct attr_mod *mod)
{
    for (int i = 0; i < a->nmods; ++i)
    {
        if (mod_same(&a->mods[i], mod))
        {
            memmove(&a->mods[i], &a->mods[i + 1], (size_t)(a->nmods - i - 1) * sizeof a->mods[0]);
            --a->nmods;
            --i;
        }
    }

    a->needs_update = 1;
}

void attrs_remove_all(struct attr_instance *a)
{
    a->nmods = 0;
    a->needs_update = 1;
}


/* The paths a living holds (its navigator's, its old-AI path, its tasks'),
 * let go when the living itself is released. */
void living_drop_paths(struct living *l)
{
    path_drop(l->nav.path);
    path_drop(l->creature_path);
    l->nav.path = l->creature_path = 0;
    struct living_ai *ai = lv_ai_peek(l);
    if (ai == NULL) return;
    struct ai_tasks *lists[2] = { &ai->tasks, &ai->target_tasks };
    for (int k = 0; k < 2; ++k)
        for (int i = 0; i < lists[k]->n; ++i)
        {
            path_drop(lists[k]->entries[i].t.path);
            path_drop(lists[k]->entries[i].t.mtv_path);
            lists[k]->entries[i].t.path = lists[k]->entries[i].t.mtv_path = 0;
        }
}

int *stack_link_count(struct stack_link k)
{
    if (k.owner == 0) return NULL;
    if ((k.owner >> 48) == STACK_ITEM_TAG) return &ie_ent_at((int32_t)(uint32_t)k.owner)->stack_count;
    return &lv_get(k.owner)->equip[k.slot].count;
}

struct stack_link stack_link_item(const ie_ent *en)
{
    return (struct stack_link){STACK_ITEM_TAG << 48 | (uint32_t)ie_ent_index(en), 0};
}

/* ------------------------------------------------------- the entity lists */

static inline uint32_t an_chunk_hash(int cx, int cz)
{
    return ((uint32_t)cx * 0x9E3779B1u) ^ ((uint32_t)cz * 0x85EBCA77u);
}

struct an_chunk *an_chunk_find(struct an_world *an, int cx, int cz)
{
    if (an->chunk_index_cap == 0) return NULL;

    uint32_t mask = (uint32_t)an->chunk_index_cap - 1;

    for (uint32_t h = an_chunk_hash(cx, cz) & mask;; h = (h + 1) & mask)
    {
        int32_t i = an->chunk_index[h];

        if (i == 0) return NULL;
        if (an->chunks[i - 1].cx == cx && an->chunks[i - 1].cz == cz) return &an->chunks[i - 1];
    }
}

/* the index grown to hold nchunks + 1 at half load, every chunk put back */
static void an_chunk_index_room(struct an_world *an)
{
    if (2 * (an->nchunks + 1) <= an->chunk_index_cap) return;

    struct slab *s = &nw_arena()->slab;
    int cap = an->chunk_index_cap ? an->chunk_index_cap * 2 : 64;

    if (an->chunk_index)
        slab_free(s, (int64_t)((unsigned char *)an->chunk_index - s->base), (size_t)an->chunk_index_cap * sizeof(int32_t));
    an->chunk_index = slab_ptr(s, slab_alloc(s, (size_t)cap * sizeof(int32_t), 1));
    an->chunk_index_cap = cap;

    uint32_t mask = (uint32_t)cap - 1;

    for (int i = 0; i < an->nchunks; ++i)
    {
        uint32_t h = an_chunk_hash(an->chunks[i].cx, an->chunks[i].cz) & mask;

        while (an->chunk_index[h] != 0) h = (h + 1) & mask;
        an->chunk_index[h] = i + 1;
    }
}

static struct an_chunk *an_chunk_get(struct an_world *an, int cx, int cz)
{
    struct an_chunk *c = an_chunk_find(an, cx, cz);

    if (c) return c;

    an->chunks = slab_table_room(an->chunks, an->nchunks, &an->capchunks, sizeof *an->chunks);
    an_chunk_index_room(an);

    uint32_t mask = (uint32_t)an->chunk_index_cap - 1;
    uint32_t h = an_chunk_hash(cx, cz) & mask;

    while (an->chunk_index[h] != 0) h = (h + 1) & mask;
    an->chunk_index[h] = an->nchunks + 1;

    c = &an->chunks[an->nchunks++];
    /* the section lists are made as they fill (sec_push) */
    memset(c, 0, sizeof *c);
    c->cx = cx;
    c->cz = cz;
    return c;
}

static int an_collect(struct an_world *an, int kind, const struct aabb *box, const struct an_ent *exclude,
                      struct an_ent **out, int max_out, int propose);

static void an_potion_splash_cb(struct ie_world *iew, struct ie_ent *potion, void *hit_entity)
{
    struct an_world *an = (struct an_world *)iew->user_data;
    if (!an) return;

    struct potion_effect effects[16];
    int neff = potion_get_effects_for_damage(potion->potion_damage, effects, 16);
    if (neff <= 0) return;

    struct aabb box = aabb_expand(potion->e.bounding_box, 4.0, 2.0, 4.0);
    AN_QUERY_LIST(found);
    int nfound = an_collect(an, -2, &box, NULL, found, AN_MAX_ENTITIES, 0);

    for (int i = 0; i < nfound; ++i)
    {
        if (!found[i]->is_living || !found[i]->livh) continue;
        struct living *target = lv_get(found[i]->livh);

        double dx = potion->e.pos_x - target->e.pos_x;
        double dy = potion->e.pos_y - target->e.pos_y;
        double dz = potion->e.pos_z - target->e.pos_z;
        double dist_sq = dx * dx + dy * dy + dz * dz;

        if (dist_sq < 16.0)
        {
            double falloff = 1.0 - sqrt(dist_sq) / 4.0;
            if (hit_entity && ((struct an_ent *)hit_entity)->is_living && lv_get(((struct an_ent *)hit_entity)->livh) == target)
            {
                falloff = 1.0;
            }

            for (int e = 0; e < neff; ++e)
            {
                int id = effects[e].id;
                int amp = effects[e].amplifier;

                if (potion_is_instant(id))
                {
                    struct living *thrower = lv_get(potion->shooting_entity);
                    potion_affect_entity(target, id, amp, falloff, thrower, an->det);
                }
                else
                {
                    int dur = (int)(falloff * (double)effects[e].duration + 0.5);
                    if (dur > 20)
                    {
                        struct potion_effect eff = {
                            .id = (uint8_t)id,
                            .duration = dur,
                            .amplifier = (int8_t)amp,
                            .is_splash = 0,
                            .is_ambient = 0
                        };
                        living_add_potion_effect(target, &eff, an->det);
                    }
                }
            }
        }
    }
}

/* EntityExpBottle.onImpact's orbs join the living world's list. */
static struct ie_ent *an_exp_bottle_orb_cb(struct ie_world *iew, double x, double y, double z, int xp)
{
    struct an_world *an = (struct an_world *)iew->user_data;
    return an ? an_spawn_orb(an, xp, x, y, z) : NULL;
}

static void an_arrow_hit_cb(struct ie_world *iew, struct ie_ent *arrow, void *hit_entity, int is_living)
{
    if (is_living && hit_entity)
    {
        struct an_ent *ae = (struct an_ent *)hit_entity;
        if (!ae->is_living || !ae->livh)
        {
            arrow->is_dead = 1;
            return;
        }
        struct living *target = lv_get(ae->livh);
        double speed = sqrt(arrow->e.motion_x * arrow->e.motion_x +
                            arrow->e.motion_y * arrow->e.motion_y +
                            arrow->e.motion_z * arrow->e.motion_z);
        double dmg_val = speed * arrow->arrow_damage;
        int var21 = (int)dmg_val;
        if (dmg_val > (double)var21) var21++;
        if (arrow->is_critical)
        {
            var21 += det_rng_int_n(&arrow->rand, var21 / 2 + 2);
        }
        /* isBurning: an arrow is not immune to fire; the enderman is spared */
        if (arrow->e.fire > 0 && target->kind != HK_ENDERMAN)
        {
            living_set_fire(target, 5);
        }
        struct living *shooter = lv_get(arrow->shooting_entity);
        det_state *det = iew->det;
        /* DamageSource.causeArrowDamage(this, shootingEntity): getEntity()
         * is the shooter; onDeath reads whether it is an arrow the player shot */
        target->player_arrow_hit = arrow->shooter_is_player;
        /* a reloaded arrow has no shootingEntity (it is not saved):
         * causeArrowDamage(this, this), the arrow is the source's entity */
        target->hit_src_arrow = arrow->shooting_entity == 0;
        target->hit_src_x = arrow->e.pos_x;
        target->hit_src_z = arrow->e.pos_z;
        int landed = living_attack_entity_from_attacker(target, shooter, DMG_ARROW, (float)var21, det);
        target->player_arrow_hit = 0;
        target->hit_src_arrow = 0;
        if (landed)
        {
            target->arrow_count_in_entity++;
            if (arrow->knockback_strength > 0)
            {
                float var26 = (float)sqrt(arrow->e.motion_x * arrow->e.motion_x + arrow->e.motion_z * arrow->e.motion_z);
                if (var26 > 0.0f)
                {
                    /* addVelocity(motionX * knockbackStrength * 0.6 / var26, 0.1, ...):
                     * Java's left-to-right order, one rounding per step */
                    target->e.motion_x += arrow->e.motion_x * (double)arrow->knockback_strength * 0.6000000238418579 / (double)var26;
                    target->e.motion_y += 0.1;
                    target->e.motion_z += arrow->e.motion_z * (double)arrow->knockback_strength * 0.6000000238418579 / (double)var26;
                }
            }
            /* a living shooter's enchantments react: thorns on the target's
             * armour, bane of arthropods on the shooter's */
            if (shooter != NULL)
            {
                ench_hurt_iter(target, shooter, det);
                ench_damage_iter(shooter, target, det);
            }
            det_rng_float(&arrow->rand);
            /* an arrow that hit an enderman (which teleported away) flies on */
            if (target->kind != HK_ENDERMAN) arrow->is_dead = 1;
        }
        else
        {
            arrow->e.motion_x *= -0.10000000149011612;
            arrow->e.motion_y *= -0.10000000149011612;
            arrow->e.motion_z *= -0.10000000149011612;
            arrow->rotation_yaw += 180.0f;
            arrow->prev_yaw += 180.0f;
            arrow->ticks_in_air = 0;
        }
    }
    else if (hit_entity)
    {
        /* an entity of the arrow's own pool (a large fireball): the hit on a
         * non-living */
        proj_arrow_hit_nonliving(arrow, (struct ie_ent *)hit_entity);
    }
    else
    {
        arrow->is_dead = 1;
    }
}

void an_fireball_impact(struct ie_world *iew, struct ie_ent *en, void *hit, int hit_kind)
{
    if (hit_kind == 1 && hit != NULL)
    {
        struct an_ent *ae = (struct an_ent *)hit;
        struct living *victim = lv_get(ae->livh);
        if (victim != NULL)
        {
            if (en->kind == IE_SMALL_FIREBALL)
            {
                if (!victim->immune_to_fire)
                {
                    if (living_attack_entity_from_attacker(victim, lv_get(en->shooter), DMG_FIREBALL, 5.0f, iew->det))
                    {
                        /* setFire(5); the player's twin hands it on */
                        if (victim->external_set_fire != NULL)
                            victim->external_set_fire(victim->external_attack_ctx, 100);
                        else if (victim->e.fire < 100) victim->e.fire = 100;
                    }
                }
            }
        }
    }
    else if (hit_kind == 2)
    {
        struct an_world *an = (struct an_world *)iew->user_data;
        if (an != NULL && an->playerh != 0)
        {
            struct living *victim = lv_get(an->playerh);
            if (en->kind == IE_SMALL_FIREBALL)
            {
                if (!victim->immune_to_fire)
                {
                    if (living_attack_entity_from_attacker(victim, lv_get(en->shooter), DMG_FIREBALL, 5.0f, iew->det))
                    {
                        /* setFire(5) */
                        if (victim->external_set_fire != NULL)
                            victim->external_set_fire(victim->external_attack_ctx, 100);
                        else if (victim->e.fire < 100) victim->e.fire = 100;
                    }
                }
            }
        }
    }
}

ie_ent *an_spawn_arrow(struct an_world *an, struct living *shooter, struct living *target,
                       float speed, float inaccuracy)
{
    ie_ent *ie = proj_spawn_arrow_target(&an->iew, shooter, target, speed, inaccuracy);
    if (!ie) return NULL;

    ie_added_to_world(&an->iew, ie);
    ie->dimension = an->dimension;
    ie->spawn_index = an->n;

    struct an_ent *en = an_ent_alloc();
    en->used = 1;
    en->is_living = 0;
    en->spawn_index = an->n;
    en->livh = 0;
    en->ieh = ie_ref(ie);
    an_list_push(an, en);
    an_chunk_add(an, en, mh_floor(ie->e.pos_x / 16.0), mh_floor(ie->e.pos_y / 16.0),
                 mh_floor(ie->e.pos_z / 16.0));
    return ie;
}

/* World.spawnEntityInWorld for a projectile already in an->iew's list (a
 * player's arrow, throwable or eye of ender): the chunk add and the living
 * world's own list entry, at its tail. */
void an_adopt_projectile(struct an_world *an, ie_ent *ie)
{
    if (!ie->added_to_chunk) ie_added_to_world(&an->iew, ie);
    ie->dimension = an->dimension;
    ie->spawn_index = an->n;

    struct an_ent *en = an_ent_alloc();
    en->used = 1;
    en->is_living = 0;
    en->spawn_index = an->n;
    en->livh = 0;
    en->ieh = ie_ref(ie);
    an_list_push(an, en);
    an_chunk_add(an, en, mh_floor(ie->e.pos_x / 16.0), mh_floor(ie->e.pos_y / 16.0),
                 mh_floor(ie->e.pos_z / 16.0));
}

ie_ent *an_spawn_potion(struct an_world *an, int spawn_index, double x, double y, double z,
                        double mx, double my, double mz, int damage)
{
    ie_ent *ie = ie_ent_alloc();
    entity_init(&ie->e, an->w);
    ie->e.self = ie;
    ie->e.first_update = 1;
    ie->entity_id = det_next_entity_id_role(an->det, DET_OTHER);
    ie->rand = det_new_random_role(an->det, DET_OTHER);
    det_uuid_role(an->det, DET_OTHER, &ie->uuid_msb, &ie->uuid_lsb);

    ie->kind = IE_POTION;
    entity_set_size(&ie->e, 0.25F, 0.25F);
    ie->e.y_offset = 0.0F;
    entity_set_position(&ie->e, x, y, z);
    ie->tile_x = ie->tile_y = ie->tile_z = -1;
    ie->in_tile = -1;
    ie->potion_damage = damage;
    ie->e.motion_x = mx;
    ie->e.motion_y = my;
    ie->e.motion_z = mz;



    ie_list_push(&an->iew, ie);
    ie_added_to_world(&an->iew, ie);
    ie->dimension = an->dimension;
    ie->spawn_index = spawn_index;

    struct an_ent *en = an_ent_alloc();
    en->used = 1;
    en->is_living = 0;
    en->spawn_index = spawn_index;
    en->livh = 0;
    en->ieh = ie_ref(ie);
    an_list_push(an, en);
    an_chunk_add(an, en, mh_floor(ie->e.pos_x / 16.0), mh_floor(ie->e.pos_y / 16.0),
                 mh_floor(ie->e.pos_z / 16.0));
    return ie;
}

static int an_query_living_cb(struct ie_world *iew, struct aabb box, const void *exclude, void **out, int cap);
static int an_get_living_bb_cb(struct ie_world *iew, void *ent, struct aabb *out_bb);

/* Explosion.doExplosionA's entity pass over the arena: World's
 * getEntitiesWithinAABBExcludingEntity walks the chunk sections of the one
 * loadedEntityList the arena's tick list mirrors, items and livings together.
 * an_collect (-1) is that walk: cx then cz, then the y sections, each in
 * insertion order. */
static int an_blast_entities_cb(struct ie_world *iew, struct aabb box, const struct entity *exclude,
                               struct blast_ent *out, int cap)
{
    struct an_world *an = (struct an_world *)iew->user_data;

    if (an == NULL) return 0;

    struct an_ent *ex = NULL;
    /* the explosion in progress holds the frame (expl_run_extras) */
    struct an_ent **found = nw_scratch->expl[nw_scratch->expl_depth - 1].an_found;

    for (int i = 0; i < an->n; ++i)
    {
        struct an_ent *en = an_ent_at(an->slot[i]);

        if (!en->used) continue;

        struct entity *e = en->is_living ? &lv_get(en->livh)->e : &ie_get(en->ieh)->e;

        if (e == exclude)
        {
            ex = en;
            break;
        }
    }

    /* the item pool this list shares its world with (the whole-server
     * replay's: a player's throws and block drops live there) is in the same
     * loadedEntityList: each section's entries of both, in the order they
     * joined it (chunk_stamp) */
    ie_world *peer = an->iew.peer != NULL && an->iew.peer->w == an->w ? an->iew.peer : NULL;

    if (peer == NULL)
    {
        int n = an_collect(an, -1, &box, ex, found, IE_MAX_ENTITIES, 0);
        int lim = n < cap ? n : cap;

        for (int i = 0; i < lim; ++i)
        {
            out[i].e = found[i]->is_living ? &lv_get(found[i]->livh)->e : &ie_get(found[i]->ieh)->e;
            out[i].ie = found[i]->is_living ? NULL : ie_get(found[i]->ieh);
            out[i].liver = found[i]->is_living ? lv_get(found[i]->livh) : NULL;
        }

        return n;
    }

    int cx0 = mh_floor((box.min_x - 2.0) / 16.0), cx1 = mh_floor((box.max_x + 2.0) / 16.0);
    int cz0 = mh_floor((box.min_z - 2.0) / 16.0), cz1 = mh_floor((box.max_z + 2.0) / 16.0);
    int y0 = mh_floor((box.min_y - 2.0) / 16.0), y1 = mh_floor((box.max_y + 2.0) / 16.0);
    ie_ent **items = (ie_ent **)found;   /* the frame's scratch, one section at a time */
    int n = 0;

    if (y0 < 0) y0 = 0;
    if (y0 > 15) y0 = 15;
    if (y1 < 0) y1 = 0;
    if (y1 > 15) y1 = 15;

    for (int cx = cx0; cx <= cx1; ++cx)
        for (int cz = cz0; cz <= cz1; ++cz)
        {
            if (!world_chunk_loaded(an->w, cx, cz)) continue;

            struct an_chunk *c = an_chunk_find(an, cx, cz);

            for (int y = y0; y <= y1; ++y)
            {
                int first = n;

                for (int i = 0; c != NULL && i < c->sec[y].n; ++i)
                {
                    struct an_ent *e = an_ent_at(sec_items(&c->sec[y])[i]);

                    if (!e->used || e == ex) continue;

                    struct entity *ee = e->is_living ? &lv_get(e->livh)->e : &ie_get(e->ieh)->e;

                    if (!aabb_intersects(&ee->bounding_box, &box)) continue;
                    if (n < cap)
                    {
                        out[n].e = ee;
                        out[n].ie = e->is_living ? NULL : ie_get(e->ieh);
                        out[n].liver = e->is_living ? lv_get(e->livh) : NULL;
                    }
                    ++n;
                }

                int np = entityquery_section(peer, cx, cz, y, box, NULL, items, IE_MAX_ENTITIES);

                for (int i = 0; i < np && i < IE_MAX_ENTITIES; ++i)
                {
                    if (n < cap) out[n] = (struct blast_ent){&items[i]->e, items[i], NULL};
                    ++n;
                }

                /* the section's list order: insertion sort by chunk_stamp */
                int last = n < cap ? n : cap;

                for (int i = first + 1; i < last; ++i)
                {
                    struct blast_ent t = out[i];
                    int j = i;

                    for (; j > first && out[j - 1].e->chunk_stamp > t.e->chunk_stamp; --j) out[j] = out[j - 1];
                    out[j] = t;
                }
            }
        }

    return n;
}

void an_init(struct an_world *an, struct world *w, det_state *det)
{
    memset(an, 0, sizeof *an);
    an->w = w;
    an->det = det;
    an->difficulty = 2;
    ie_init(&an->iew, w, det);
    an->iew.user_data = an;
    an->iew.on_splash = an_potion_splash_cb;
    an->iew.spawn_orb = an_exp_bottle_orb_cb;
    an->iew.on_arrow_hit = an_arrow_hit_cb;
    an->iew.on_fireball_impact = an_fireball_impact;
    an->iew.query_living = an_query_living_cb;
    an->iew.get_living_bb = an_get_living_bb_cb;
    an->iew.blast_entities = an_blast_entities_cb;
}

/* EntityEgg.onImpact: the child is constructed and added to the same world's
 * live entity list. The projectile probe may call this through another item
 * pool, so the callback is exported rather than tied to an->iew. */
void an_egg_chicken(struct an_world *an, struct ie_ent *egg)
{
    struct living *l = living_alloc();
    living_init(l, an->w, AK_CHICKEN, an->det);
    l->an = an;
    l->dimension = an->dimension;
    animal_construct(l, an->det);
    /* EntityEgg sets the age before the location. EntityAgeable.setSize
     * changes the bounding box relative to its old origin, so this order
     * affects the final position by 0.075 for a baby chicken. */
    living_set_growing_age(l, -24000);
    living_set_location_and_angles(l, egg->e.pos_x, egg->e.pos_y, egg->e.pos_z,
                                   egg->rotation_yaw, 0.0F);
    an_add_living(an, l, an->n);
}

int an_chunk_drop_empty(struct an_world *an, int cx, int cz)
{
    struct an_chunk *c = an_chunk_find(an, cx, cz);

    if (c == NULL) return 0;
    for (int s = 0; s < 16; ++s)
        if (c->sec[s].n) return 0;
    for (int s = 0; s < 16; ++s) sec_release(&c->sec[s]);

    /* out of the index (linear probing, backward shift), and the last
     * entry moved into its place */
    uint32_t mask = (uint32_t)an->chunk_index_cap - 1;
    int32_t at = (int32_t)(c - an->chunks), last = an->nchunks - 1;
    uint32_t h = an_chunk_hash(cx, cz) & mask;

    while (an->chunk_index[h] != at + 1) h = (h + 1) & mask;
    an->chunk_index[h] = 0;
    for (uint32_t j = (h + 1) & mask; an->chunk_index[j] != 0; j = (j + 1) & mask)
    {
        const struct an_chunk *o = &an->chunks[an->chunk_index[j] - 1];
        uint32_t k = an_chunk_hash(o->cx, o->cz) & mask;

        if (j > h ? k <= h || k > j : k <= h && k > j)
        {
            an->chunk_index[h] = an->chunk_index[j];
            an->chunk_index[j] = 0;
            h = j;
        }
    }
    if (at != last)
    {
        an->chunks[at] = an->chunks[last];
        for (h = an_chunk_hash(an->chunks[at].cx, an->chunks[at].cz) & mask; an->chunk_index[h] != last + 1;
             h = (h + 1) & mask)
            ;
        an->chunk_index[h] = at + 1;
    }
    --an->nchunks;
    return 1;
}

void an_free(struct an_world *an)
{
    ie_free(&an->iew);
    for (int i = 0; i < an->nchunks; ++i)
        for (int s = 0; s < 16; ++s) sec_release(&an->chunks[i].sec[s]);
    slab_table_free(an->chunks, an->capchunks, sizeof *an->chunks);
    an->chunks = NULL;
    an->nchunks = an->capchunks = 0;
    if (an->chunk_index) slab_table_free(an->chunk_index, an->chunk_index_cap, sizeof(int32_t));
    an->chunk_index = NULL;
    an->chunk_index_cap = 0;
}

/* Chunk.addEntity: into the y section (clamped), in insertion order. */
void an_chunk_add(struct an_world *an, struct an_ent *en, int cx, int cy, int cz)
{
    struct an_chunk *c = an_chunk_get(an, cx, cz);

    if (cy < 0) cy = 0;
    if (cy >= 16) cy = 15;

    if (en->is_living) lv_get(en->livh)->e.chunk_stamp = ++entity_chunk_stamp;
    else ie_get(en->ieh)->e.chunk_stamp = ++entity_chunk_stamp;

    if (en->is_living)
    {
        lv_get(en->livh)->added_to_chunk = 1;
        lv_get(en->livh)->chunk_coord_x = cx;
        lv_get(en->livh)->chunk_coord_y = cy;
        lv_get(en->livh)->chunk_coord_z = cz;
    }
    else
    {
        ie_get(en->ieh)->added_to_chunk = 1;
        ie_get(en->ieh)->chunk_x = cx;
        ie_get(en->ieh)->chunk_y = cy;
        ie_get(en->ieh)->chunk_z = cz;
    }

    sec_push(&c->sec[cy], an_ent_index(en), AN_MAX_ENTITIES);
}

void an_chunk_restamp(struct an_world *an, struct an_ent *en, uint64_t stamp)
{
    if (!en->is_living || stamp == 0) return;

    struct living *l = lv_get(en->livh);

    l->e.chunk_stamp = stamp;
    if (!l->added_to_chunk) return;

    struct an_chunk *c = an_chunk_find(an, l->chunk_coord_x, l->chunk_coord_z);
    int cy = l->chunk_coord_y < 0 ? 0 : l->chunk_coord_y > 15 ? 15 : l->chunk_coord_y;

    if (!c) return;

    int32_t *it = sec_items(&c->sec[cy]);
    int at = -1;

    for (int i = c->sec[cy].n - 1; i >= 0; --i)
        if (it[i] == an_ent_index(en)) { at = i; break; }
    for (; at > 0; --at)
    {
        const struct an_ent *p = an_ent_at(it[at - 1]);
        uint64_t ps = p->is_living ? lv_get(p->livh)->e.chunk_stamp : ie_get(p->ieh)->e.chunk_stamp;

        if (ps < stamp) break;
        it[at] = it[at - 1];
        it[at - 1] = an_ent_index(en);
    }
}

/* Chunk.removeEntityAtIndex: the first occurrence out of the y section. */
static void an_chunk_remove_coords(struct an_world *an, struct an_ent *en, int cx, int cy, int cz)
{
    if (cy < 0) cy = 0;
    if (cy >= 16) cy = 15;
    struct an_chunk *c = an_chunk_find(an, cx, cz);

    if (!c) return;

    sec_remove_first(&c->sec[cy], an_ent_index(en));
}

void an_chunk_remove(struct an_world *an, struct an_ent *en, int cy)
{
    int cx = en->is_living ? lv_get(en->livh)->chunk_coord_x : ie_get(en->ieh)->chunk_x;
    int cz = en->is_living ? lv_get(en->livh)->chunk_coord_z : ie_get(en->ieh)->chunk_z;
    an_chunk_remove_coords(an, en, cx, cy, cz);
}

/* Chunk.getEntitiesOfTypeWithinAAAB (kind >= 0) and getEntitiesWithinAABBForEntity
 * (kind -1, minus one entity): World.getEntitiesWithinAABB walks the chunk
 * coords in cx, then cz order, and each chunk walks its y sections from the
 * box's minY - 2 to maxY + 2. */
static int an_collect(struct an_world *an, int kind, const struct aabb *box, const struct an_ent *exclude,
                      struct an_ent **out, int max_out, int propose)
{
    int cx0 = mh_floor((box->min_x - 2.0) / 16.0);
    int cx1 = mh_floor((box->max_x + 2.0) / 16.0);
    int cz0 = mh_floor((box->min_z - 2.0) / 16.0);
    int cz1 = mh_floor((box->max_z + 2.0) / 16.0);
    int y0 = mh_floor((box->min_y - 2.0) / 16.0);
    int y1 = mh_floor((box->max_y + 2.0) / 16.0);

    if (y0 < 0) y0 = 0;
    if (y0 > 15) y0 = 15;
    if (y1 < 0) y1 = 0;
    if (y1 > 15) y1 = 15;

    int count = 0;

    for (int cx = cx0; cx <= cx1; ++cx)
    {
        for (int cz = cz0; cz <= cz1; ++cz)
        {
            if (!world_chunk_loaded(an->w, cx, cz)) continue;

            struct an_chunk *c = an_chunk_find(an, cx, cz);

            if (!c) continue;

            for (int y = y0; y <= y1; ++y)
            {
                for (int i = 0; i < c->sec[y].n; ++i)
                {
                    struct an_ent *e = an_ent_at(sec_items(&c->sec[y])[i]);

                    if (!e->used) continue;   /* left the list; the entry is pooled */
                    if (e == exclude) continue;
                    if (kind == -2)
                    {
                        if (!e->is_living) continue;
                    }
                    else if (kind >= 0)
                    {
                        if (!(e->is_living && living_kind_in_class(lv_get(e->livh)->kind, kind))) continue;
                    }

                    struct aabb bb = e->is_living ? lv_get(e->livh)->e.bounding_box : ie_get(e->ieh)->e.bounding_box;


                    if (!aabb_intersects(&bb, box)) continue;

                    if (out && count < max_out) out[count] = e;
                    ++count;
                    (void)propose;
                }
            }
        }
    }



    return count;
}

int living_kind_in_class(int kind, int class_kind)
{
    if (kind == class_kind) return 1;
    if (class_kind < 0) return 1;

    /* the subclasses: EntityMooshroom, EntityPigZombie, EntityCaveSpider and
     * EntityMagmaCube extend the classes a query can name */
    return (class_kind == AK_COW && kind == AK_MOOSHROOM) ||
           (class_kind == HK_ZOMBIE && kind == HK_PIGMAN) ||
           (class_kind == HK_SPIDER && kind == HK_CAVE_SPIDER) ||
           (class_kind == SK_SLIME && kind == SK_MAGMA_CUBE);
}

/* The count of matches, or with OUT the count written (at most MAX_OUT: a
 * list of AN_QUERY_LIST's size holds every match). */
int an_entities_within_aabb(struct an_world *an, int kind, const struct aabb *box, struct an_ent **out, int max_out)
{
    int n = an_collect(an, kind, box, NULL, out, max_out, 0);
    return out != NULL && n > max_out ? max_out : n;
}

int an_entities_excluding(struct an_world *an, const struct an_ent *exclude, const struct aabb *box,
                          struct an_ent **out, int max_out)
{
    int n = an_collect(an, -1, box, exclude, out, max_out, 0);
    return out != NULL && n > max_out ? max_out : n;
}

static int an_query_living_cb(struct ie_world *iew, struct aabb box, const void *exclude, void **out, int cap)
{
    struct an_world *an = (struct an_world *)iew->user_data;
    if (!an) return 0;
    int n = an_collect(an, -2, &box, (const struct an_ent *)exclude, (struct an_ent **)out, cap, 0);
    return n > cap ? cap : n;
}

static int an_get_living_bb_cb(struct ie_world *iew, void *ent, struct aabb *out_bb)
{
    (void)iew;
    struct an_ent *ae = (struct an_ent *)ent;
    if (!ae || !ae->used || !ae->is_living || !ae->livh) return 0;
    if (lv_get(ae->livh)->is_dead) return 0;
    *out_bb = lv_get(ae->livh)->e.bounding_box;
    return 1;
}

/* -------------------------------------------------------- entity basics */

static void living_set_position(struct living *l, double x, double y, double z)
{
    entity_set_position(&l->e, x, y, z);
}


void living_set_location_and_angles(struct living *l, double x, double y, double z, float yaw, float pitch)
{
    l->last_tick_x = l->e.prev_pos_x = l->e.pos_x = x;
    l->last_tick_y = l->e.prev_pos_y = l->e.pos_y = y + (double)l->e.y_offset;
    l->last_tick_z = l->e.prev_pos_z = l->e.pos_z = z;
    l->rotation_yaw = yaw;
    l->rotation_pitch = pitch;
    living_set_position(l, l->e.pos_x, l->e.pos_y, l->e.pos_z);
}

/* Entity.setSize, with EntityAgeable's wrapper handled by the caller. */
static void entity_size_apply(struct living *l, float w, float h)
{
    if (w != l->e.width || h != l->e.height)
    {
        float old = l->e.width;
        l->e.width = w;
        l->e.height = h;
        l->e.bounding_box.max_x = l->e.bounding_box.min_x + (double)w;
        l->e.bounding_box.max_z = l->e.bounding_box.min_z + (double)w;
        l->e.bounding_box.max_y = l->e.bounding_box.min_y + (double)h;

        if (w > old && !l->e.first_update)
        {
            living_move(l, (double)(old - w), 0.0, (double)(old - w));
        }
    }
}

void living_set_size(struct living *l, float w, float h)
{
    entity_size_apply(l, w, h);
}

/* EntityAgeable.setSize: record the base and scale to 1 the first time. */

static void ageable_set_scale(struct living *l, float s)
{
    entity_size_apply(l, l->base_width * s, l->base_height * s);
}

/* EntityAgeable.setSize: record the base size and scale to 1 the first time. */
void living_set_base_size(struct living *l, float w, float h)
{
    int had = l->base_width > 0.0F;
    l->base_width = w;
    l->base_height = h;

    if (!had) entity_size_apply(l, w, h);
}



void living_set_growing_age(struct living *l, int v)
{
    l->growing_age = v;
    ageable_set_scale(l, v < 0 ? 0.5F : 1.0F);
}

int living_is_alive(struct living *l)
{
    return !l->is_dead && l->health > 0.0F;
}

void living_set_dead(struct living *l)
{
    l->is_dead = 1;
}

/* Entity.getFlag / setFlag over dataWatcher 0. */

static void set_flag(struct living *l, int bit, int on)
{
    if (on) l->flags0 = (uint8_t)(l->flags0 | (1 << bit));
    else l->flags0 = (uint8_t)(l->flags0 & ~(1 << bit));
}

int living_is_burning(struct living *l)
{
    if (l->kind == HK_BLAZE) return (l->data_watcher_16 & 1) != 0;
    return !l->immune_to_fire && l->e.fire > 0;
}

/* --------------------------------------------------------- the world reads */

static int light_diffuse(int id)
{
    return rt_light_diffuse(id);
}


/* Chunk.getSavedLightValue. */

/* Chunk.getBlockLightValue(x, y, z, subtracted). */
static int chunk_light_value(struct world *w, int x, int y, int z, int subtracted)
{
    struct chunk *c = world_chunk(w, x >> 4, z >> 4);

    if (!c || (c->mask & (1 << (y >> 4))) == 0)
    {
        /* hasNoSky (the Nether, the End): an absent section is dark */
        if (w->dim != 0) return 0;
        /* In the overworld, an absent section has sky above the height map. */
        if (!c) return subtracted < 15 ? (15 - subtracted) : 0;
        int see = y >= c->height[((z & 15) << 4) | (x & 15)];
        if (!see) return 0;
        return subtracted < 15 ? 15 - subtracted : 0;
    }

    int sky = chunk_cell_sky(c, (x & 15) << 12 | (z & 15) << 8 | y) - subtracted;
    int blk = chunk_cell_blocklight(c, (x & 15) << 12 | (z & 15) << 8 | y);

    if (blk > sky) sky = blk;

    return sky;
}

/* World.getBlockLightValue_do with check false: the stored value. The check
 * reads its five neighbours through this, so it does not call itself. */
static int block_light_value_raw(struct world *w, int subtracted, int x, int y, int z)
{
    if (x < -30000000 || z < -30000000 || x >= 30000000 || z >= 30000000) return 15;

    if (y < 0) return 0;
    if (y >= 256) y = 255;
    /* getChunkFromChunkCoords: a chunk that is not loaded is provided (a
     * neighbour of a neighbour-brightness block across a chunk edge loads
     * it), unless generation is off (the EmptyChunk's light is 0) */
    if ((w->no_generate ? world_chunk(w, x >> 4, z >> 4) : world_load_chunk(w, x >> 4, z >> 4)) == NULL) return 0;

    return chunk_light_value(w, x, y, z, subtracted);
}

/* World.getBlockLightValue_do(x, y, z, true), with its five-neighbour recursion
 * for a block whose stored light is not the light the world reports. */
static int block_light_value_do(struct world *w, int subtracted, int x, int y, int z, int do_check)
{
    if (x < -30000000 || z < -30000000 || x >= 30000000 || z >= 30000000) return 15;

    if (do_check && light_diffuse(world_get_block(w, x, y, z)))
    {
        int v10 = block_light_value_raw(w, subtracted, x, y + 1, z);
        int v6 = block_light_value_raw(w, subtracted, x + 1, y, z);
        int v7 = block_light_value_raw(w, subtracted, x - 1, y, z);
        int v8 = block_light_value_raw(w, subtracted, x, y, z + 1);
        int v9 = block_light_value_raw(w, subtracted, x, y, z - 1);

        if (v6 > v10) v10 = v6;
        if (v7 > v10) v10 = v7;
        if (v8 > v10) v10 = v8;
        if (v9 > v10) v10 = v9;

        return v10;
    }

    return block_light_value_raw(w, subtracted, x, y, z);
}

static float light_brightness_table[16];
static float nether_light_brightness_table[16];

/* before main: the same for every environment */
__attribute__((constructor)) static void build_light_table(void)
{
    float v1 = 0.0F;
    float hell_ambient = 0.1F;

    for (int i = 0; i <= 15; ++i)
    {
        float v3 = 1.0F - (float)i / 15.0F;
        light_brightness_table[i] = (1.0F - v3) / (v3 * 3.0F + 1.0F) * (1.0F - v1) + v1;
        nether_light_brightness_table[i] = (1.0F - v3) / (v3 * 3.0F + 1.0F) * (1.0F - hell_ambient) + hell_ambient;
    }
}

/* World.getLightBrightness. */
static float light_brightness(struct world *w, int subtracted, int x, int y, int z)
{
    int level = block_light_value_do(w, subtracted, x, y, z, 1);
    return w->dim == -1 ? nether_light_brightness_table[level] : light_brightness_table[level];
}

/* World.getFullBlockLightValue. */
static int full_block_light_value(struct world *w, int subtracted, int x, int y, int z)
{
    return world_get_full_block_light_value(w, x, y, z, subtracted);
}

/* World.isAnyLiquid. */
static int is_any_liquid(struct world *w, struct aabb box)
{
    int x0 = mh_floor(box.min_x);
    int x1 = mh_floor(box.max_x + 1.0);
    int y0 = mh_floor(box.min_y);
    int y1 = mh_floor(box.max_y + 1.0);
    int z0 = mh_floor(box.min_z);
    int z1 = mh_floor(box.max_z + 1.0);

    if (box.min_x < 0.0) --x0;
    if (box.min_y < 0.0) --y0;
    if (box.min_z < 0.0) --z0;

    for (int x = x0; x < x1; ++x)
    {
        for (int y = y0; y < y1; ++y)
        {
            for (int z = z0; z < z1; ++z)
            {
                if (MATERIALS[BLOCKS[world_get_block(w, x, y, z) & 4095].material].is_liquid) return 1;
            }
        }
    }

    return 0;
}

/* Entity.handleWaterMovement. */
static int handle_water_movement(struct living *l)
{
    if (ie_water_accelerate_memo(l->world, &l->e, &l->cmemo))
    {
        if (!l->is_in_water && !l->e.first_update)
        {
            float v1 = sqrt_float((float)(l->e.motion_x * l->e.motion_x * 0.20000000298023224
                                           + l->e.motion_y * l->e.motion_y
                                           + l->e.motion_z * l->e.motion_z * 0.20000000298023224)) * 0.2F;

            if (v1 > 1.0F) v1 = 1.0F;

            /* playSound(getSplashSound(), v1, 1.0F + (rand.nextFloat() - rand.nextFloat()) * 0.4F) */
            float a = det_rng_float(&l->rand);
            float b = det_rng_float(&l->rand);
            (void)v1;
            (void)a;
            (void)b;

            float v2 = (float)mh_floor(l->e.bounding_box.min_y);

            for (int i = 0; (float)i < 1.0F + l->e.width * 20.0F; ++i)
            {
                (void)((det_rng_float(&l->rand) * 2.0F - 1.0F) * l->e.width);
                (void)((det_rng_float(&l->rand) * 2.0F - 1.0F) * l->e.width);
                (void)det_rng_float(&l->rand);
                (void)v2;
            }

            for (int i = 0; (float)i < 1.0F + l->e.width * 20.0F; ++i)
            {
                (void)((det_rng_float(&l->rand) * 2.0F - 1.0F) * l->e.width);
                (void)((det_rng_float(&l->rand) * 2.0F - 1.0F) * l->e.width);
            }
        }

        l->e.fall_distance = 0.0F;
        l->is_in_water = 1;
        l->e.in_water = 1;   /* the base entity's copy: moveEntity's swim sound reads it */
        l->e.fire = 0;
    }
    else
    {
        l->is_in_water = 0;
        l->e.in_water = 0;
    }

    return l->is_in_water;
}

/* Entity.isInsideOfMaterial. */
static int is_inside_of_material_scan(struct living *l, int material);

/* the water case (the air supply's, each tick) over the cell memo: a no
 * stands while the cells and the position do */
static int is_inside_of_material(struct living *l, int material)
{
    int held = material == 6 && cell_memo_hold_at(&l->cmemo, l->world, &l->e.bounding_box, l->e.pos_x, l->e.pos_y, l->e.pos_z,
                                    l->e.width);

    if (held && (l->cmemo.known & CM_EYE_DRY))
    {
#ifdef NETHERITE_MOVE_MEMO_CHECK
        if (is_inside_of_material_scan(l, material)) { fprintf(stderr, "cell memo: the eye test differs\n"); abort(); }
#endif
        return 0;
    }

    int r = is_inside_of_material_scan(l, material);
    if (held && !r) l->cmemo.known |= CM_EYE_DRY;
    return r;
}

static int is_inside_of_material_scan(struct living *l, int material)
{
    double y = l->e.pos_y + (double)living_eye_height(l);
    int bx = mh_floor(l->e.pos_x);
    int by = j_f2i((float)mh_floor(y));
    int bz = mh_floor(l->e.pos_z);
    int id = world_get_block(l->world, bx, by, bz) & 4095;

    if (BLOCKS[id].material == material)
    {
        float v8 = ie_liquid_height_meta(world_get_meta(l->world, bx, by, bz)) - 0.11111111F;
        float v9 = (float)(by + 1) - v8;
        return y < (double)v9;
    }

    return 0;
}

/* Entity.isEntityInsideOpaqueBlock, a no answered from the cell memo while
 * the cells and the position stand */
static int is_entity_inside_opaque_block_scan(struct living *l);

static int is_entity_inside_opaque_block(struct living *l)
{
    int held = cell_memo_hold_at(&l->cmemo, l->world, &l->e.bounding_box, l->e.pos_x, l->e.pos_y, l->e.pos_z,
                                    l->e.width);

    if (held && (l->cmemo.known & CM_OPEN))
    {
#ifdef NETHERITE_MOVE_MEMO_CHECK
        if (is_entity_inside_opaque_block_scan(l)) { fprintf(stderr, "cell memo: the wall test differs\n"); abort(); }
#endif
        return 0;
    }

    int r = is_entity_inside_opaque_block_scan(l);
    if (held && !r) l->cmemo.known |= CM_OPEN;
    return r;
}

static int is_entity_inside_opaque_block_scan(struct living *l)
{
    for (int i = 0; i < 8; ++i)
    {
        float v2 = ((float)((i >> 0) % 2) - 0.5F) * l->e.width * 0.8F;
        float v3 = ((float)((i >> 1) % 2) - 0.5F) * 0.1F;
        float v4 = ((float)((i >> 2) % 2) - 0.5F) * l->e.width * 0.8F;
        int bx = mh_floor(l->e.pos_x + (double)v2);
        int by = mh_floor(l->e.pos_y + (double)living_eye_height(l) + (double)v3);
        int bz = mh_floor(l->e.pos_z + (double)v4);

        if (BLOCKS[world_get_block(l->world, bx, by, bz) & 4095].normal_cube) return 1;
    }

    return 0;
}

/* Entity.isOnLadder. */
static int is_on_ladder(struct living *l)
{
    /* EntitySpider.isOnLadder's override: the dataWatcher 16 climb flag, not
     * the block at the feet */
    if (l->kind == HK_SPIDER || l->kind == HK_CAVE_SPIDER) return (l->data_watcher_16 & 1) != 0;

    int bx = mh_floor(l->e.pos_x);
    int by = mh_floor(l->e.bounding_box.min_y);
    int bz = mh_floor(l->e.pos_z);
    int id = world_get_block(l->world, bx, by, bz) & 4095;

    return id == 65 || id == 106;
}

/* Entity.isOffsetPositionInLiquid. */
static int is_offset_position_in_liquid(struct living *l, double x, double y, double z)
{
    struct aabb box = aabb_offset(l->e.bounding_box, x, y, z);
    struct collide_list *list COLLIDE_SCRATCH = collide_scratch_begin();
    collide_list_clear(list);
    world_get_colliding_bounding_boxes(l->world, box, list);

    if (list->n > 0) return 0;

    return !is_any_liquid(l->world, box);
}

/* Entity.handleLavaMovement. */
static int handle_lava_movement(struct living *l)
{
    /* EntityMagmaCube.handleLavaMovement is an unconditional false */
    if (l->kind == SK_MAGMA_CUBE) return 0;

    return ie_lava_memo(l->world, &l->e.bounding_box, &l->cmemo);
}

/* Entity.setFire, through EnchantmentProtection.getFireTimeForEntity. The
 * player twin's fire is the server player's. */
void living_set_fire(struct living *l, int seconds)
{
    int v2 = ench_fire_time(l, seconds * 20);
    struct entity *e = living_fire_entity(l);
    if (e->fire < v2) e->fire = v2;
}

/* Entity.setBeenAttacked (EntityLivingBase's override). */
void living_set_been_attacked(struct living *l)
{
    l->e.velocity_changed = det_rng_double(&l->rand) >= attrs_value(&l->attrs.a[ATTR_KNOCKBACK_RESISTANCE]);
}

/* EntityLivingBase.setBeenAttacked, the exposed entry the slime's attack path
 * and the player's damage path both reach. */
void living_mark_attacked(struct living *l)
{
    living_set_been_attacked(l);
}

/* EntityLivingBase.knockBack. */
static void living_knock_back(struct living *l, float amount, double dx, double dz)
{
    (void)amount;

    if (det_rng_double(&l->rand) >= attrs_value(&l->attrs.a[ATTR_KNOCKBACK_RESISTANCE]))
    {
        l->is_air_borne = 1;
        float var7 = (float)sqrt_double(dx * dx + dz * dz);
        float var8 = 0.4F;
        l->e.motion_x /= 2.0;
        l->e.motion_y /= 2.0;
        l->e.motion_z /= 2.0;
        l->e.motion_x -= dx / (double)var7 * (double)var8;
        l->e.motion_y += (double)var8;
        l->e.motion_z -= dz / (double)var7 * (double)var8;

        if (l->e.motion_y > 0.4000000059604645)
        {
            l->e.motion_y = 0.4000000059604645;
        }
    }
}

/* EntityLivingBase.damageEntity, exposed for the player's own override. */
void living_damage_entity_apply(struct living *l, int source, float amount, det_state *det)
{
    living_damage_entity(l, source, amount, det);
}

/* EntityPlayer.damageEntity is EntityLivingBase's plus the hunger cost, so the
 * player's damage goes through slimes.c. */
static void living_damage_dispatch(struct living *l, int source, float amount, det_state *det)
{
    if (l->kind == SK_PLAYER || l->kind == HK_PLAYER) player_damage_entity(l, source, amount, det);
    else living_damage_entity(l, source, amount, det);
}

/* Entity.addVelocity. */
static void living_add_velocity(struct living *l, double x, double y, double z)
{
    l->e.motion_x += x;
    l->e.motion_y += y;
    l->e.motion_z += z;
    l->is_air_borne = 1;
}

/* Entity.applyEntityCollision. */
static void living_apply_entity_collision(struct living *l, struct living *other)
{
    /* Entity.applyEntityCollision's guard: the receiver is not pushed by the
     * entity it rides, nor by the entity that rides it. The spider jockey's
     * rider and its vehicle would otherwise push each other. */
    if (lv_get(other->ridden_by_entity) == l || lv_get(other->riding_entity) == l) return;

    double v2 = other->e.pos_x - l->e.pos_x;
    double v4 = other->e.pos_z - l->e.pos_z;
    double v6 = abs_max(v2, v4);

    if (v6 >= 0.009999999776482582)
    {
        v6 = sqrt_double(v6);
        v2 /= v6;
        v4 /= v6;
        double v8 = 1.0 / v6;

        if (v8 > 1.0) v8 = 1.0;

        v2 *= v8;
        v4 *= v8;
        v2 *= 0.05000000074505806;
        v4 *= 0.05000000074505806;
        v2 *= (double)(1.0F - 0.0F);
        v4 *= (double)(1.0F - 0.0F);
        living_add_velocity(l, -v2, 0.0, -v4);
        living_add_velocity(other, v2, 0.0, v4);
    }
}

/* --------------------------------------------------------- Entity damage */

static float living_max_health_impl(struct living *l)
{
    return (float)attrs_value(&l->attrs.a[ATTR_MAX_HEALTH]);
}

float living_max_health(struct living *l)
{
    return living_max_health_impl(l);
}

void living_set_health(struct living *l, float v)
{
    l->health = clamp_float(v, 0.0F, living_max_health_impl(l));
}

/* EntityLivingBase.setAbsorptionAmount: below zero is zero. */
void living_set_absorption(struct living *l, float v)
{
    if (v < 0.0F) v = 0.0F;

    l->absorption = v;
}

void living_set_invisible(struct living *l, int inv)
{
    set_flag(l, 5, inv);
}

int living_is_invisible(const struct living *l)
{
    return (l->flags0 & (1 << 5)) != 0;
}

static void living_on_death(struct living *l, struct living *attacker, int source, det_state *det);
static void zombie_conversion_tick(struct living *l);

static int dmg_unblockable(int source)
{
    /* The vanilla DamageSource instances that call setDamageBypassesArmor. */
    return source == DMG_IN_WALL || source == DMG_DROWN || source == DMG_OUT_OF_WORLD ||
           source == DMG_MAGIC || source == DMG_WITHER || source == DMG_GENERIC || source == DMG_STARVE ||
           source == DMG_ON_FIRE || source == DMG_FALL || source == DMG_INDIRECT_MAGIC;
}

static int dmg_absolute(int source)
{
    /* DamageSource.starve alone calls setDamageIsAbsolute (outOfWorld only
     * bypasses armour and is allowed in creative) */
    return source == DMG_STARVE;
}

/* DamageSource.getHungerDamage: 0.0F for every source whose chain runs
 * setDamageBypassesArmor or setDamageIsAbsolute (inFire and the EntityDamageSources
 * keep the 0.3F default). The player's damageEntity adds it to the exhaustion. */
float living_dmg_hunger(int source)
{
    switch (source)
    {
        case DMG_IN_WALL:
        case DMG_DROWN:
        case DMG_STARVE:
        case DMG_FALL:
        case DMG_OUT_OF_WORLD:
        case DMG_GENERIC:
        case DMG_MAGIC:
        case DMG_INDIRECT_MAGIC:
        case DMG_WITHER:
        case DMG_ON_FIRE:
            return 0.0F;

        default:
            return 0.3F;
    }
}

int living_dmg_absolute(int source)
{
    return dmg_absolute(source);
}

/* EntityLivingBase.getTotalArmorValue over getLastActiveItems (the five
 * equipment slots, ItemArmor.damageReduceAmount each), as the kinds override
 * it: EntityZombie adds two and caps at 20, EntityMagmaCube is size * 3. */
static int living_total_armor(const struct living *l)
{
    if (l->kind == SK_MAGMA_CUBE) return l->slime_size * 3;

    int v = 0;

    for (int i = 0; i < 5; ++i)
    {
        int id = l->equip[i].id;

        if (id > 0 && id < 4096) v += ITEMS[id].damage_reduce;
    }

    if (l->kind == HK_ZOMBIE || l->kind == HK_PIGMAN)
    {
        v += 2;
        if (v > 20) v = 20;
    }

    return v;
}

/* EnchantmentHelper.getEnchantmentModifierDamage: EnchantmentProtection's
 * calcModifierDamage summed over the equipment, capped at 25, then half of it
 * plus one enchantmentRand draw. Every other enchantment adds nothing. */
static int living_enchantment_modifier_damage(const struct living *l, int source, det_state *det)
{
    int dm = 0;
    int fire = source == DMG_IN_FIRE || source == DMG_ON_FIRE || source == DMG_LAVA || source == DMG_FIREBALL;
    int explosion = source == DMG_EXPLOSION;
    int projectile = dmg_is_projectile(source);

    /* canHarmInCreative: outOfWorld only */
    if (source != DMG_OUT_OF_WORLD)
        for (int i = 0; i < 5; ++i)
        {
            if (l->equip[i].id <= 0) continue;

            for (int e = 0; e < itag_nench(l->equip[i].tag); ++e)
            {
                int id = itag_ench_at(l->equip[i].tag, e).id, lv = itag_ench_at(l->equip[i].tag, e).lvl;
                float f = (float)(6 + lv * lv) / 3.0F;

                if (id == 0) dm += mh_floor((double)(f * 0.75F));
                else if (id == 1 && fire) dm += mh_floor((double)(f * 1.25F));
                else if (id == 2 && source == DMG_FALL) dm += mh_floor((double)(f * 2.5F));
                else if (id == 3 && explosion) dm += mh_floor((double)(f * 1.5F));
                else if (id == 4 && projectile) dm += mh_floor((double)(f * 1.5F));
            }
        }

    if (dm > 25) dm = 25;

    const char *name = "./net/minecraft/enchantment/EnchantmentHelper.java:enchantmentRand";
    int draw = 0;

    if (det != NULL)
    {
        struct det_split *sp = det_split_find(det, name);
        if (sp == NULL) sp = det_split_random(det, name);
        draw = det_split_int_n(det, sp, (dm >> 1) + 1);
    }

    return ((dm + 1) >> 1) + draw;
}

void living_damage_entity(struct living *l, int source, float amount, det_state *det)
{
    if (l->invulnerable) return;
    if (det == NULL && l->an != NULL) det = l->an->det;

    int armour = living_total_armor(l);

    if (!dmg_unblockable(source))
    {
        int v3 = 25 - armour;
        float v4 = amount * (float)v3;
        amount = v4 / 25.0F;
    }

    /* applyPotionDamageCalculations: resistance reduces damage by (25 - (amp+1)*5)/25 */
    if (!dmg_absolute(source))
    {
        if (living_is_potion_active(l, POT_RESISTANCE) && source != DMG_OUT_OF_WORLD)
        {
            const struct potion_effect *eff = living_get_potion_effect(l, POT_RESISTANCE);
            int rv3 = (eff->amplifier + 1) * 5;
            int rv4 = 25 - rv3;
            float rv5 = amount * (float)rv4;
            amount = rv5 / 25.0F;
        }

        if (amount > 0.0F)
        {
            int v3 = living_enchantment_modifier_damage(l, source, det);
            if (v3 > 20) v3 = 20;
            if (v3 > 0 && v3 <= 20)
            {
                int v4 = 25 - v3;
                float v5 = amount * (float)v4;
                amount = v5 / 25.0F;
            }
        }
        else
        {
            return;   /* the absolute/zero guard returns before the armour step */
        }

        if (l->kind == HK_WITCH)
        {
            if (lv_get(l->damage_attacker) == l) amount = 0.0F;
            if (dmg_is_magic(source)) amount = (float)((double)amount * 0.15);
            if (amount <= 0.0F) return;
        }
    }

    float v3 = amount;
    amount = amount - l->absorption;
    if (amount < 0.0F) amount = 0.0F;
    living_set_absorption(l, l->absorption - (v3 - amount));

    if (amount != 0.0F)
    {
        float v4 = l->health;
        living_set_health(l, v4 - amount);
        /* CombatTracker records the hit; at a death it holds only the
         * killing blow (living_on_death reads the source's entity) */
        living_set_absorption(l, l->absorption - amount);
    }
}

/* EntityLivingBase.onUpdate's arrows: with n stuck, arrowHitTimer restarts
 * at 20 * (30 - n) and one falls out when it runs down (the count is
 * DataWatcher 9, the tracker's S1C). */
void living_arrow_decay(struct living *l)
{
    int n = l->arrow_count_in_entity;
    if (n <= 0) return;
    if (l->arrow_hit_timer <= 0) l->arrow_hit_timer = 20 * (30 - n);
    --l->arrow_hit_timer;
    if (l->arrow_hit_timer <= 0) l->arrow_count_in_entity = (int)(int8_t)(n - 1);
}

static void living_on_death_update(struct living *l, det_state *det)
{
    ++l->death_time;

    if (l->death_time == 20)
    {
        if (l->an != NULL && l->recently_hit > 0 && l->growing_age >= 0 &&
            living_mob_loot(l->world))
        {
            /* EntityZombie.getExperiencePoints: a child scales the value
             * itself by 2.5 before EntityLiving's equipment bonus */
            if ((l->kind == HK_ZOMBIE || l->kind == HK_PIGMAN) && l->zombie_is_child)
                l->experience_value = (int)((float)l->experience_value * 2.5F);
            int xp = l->experience_value;
            /* EntityChicken.getExperiencePoints: a jockey's chicken gives 10 */
            if (l->kind == AK_CHICKEN && l->is_chicken_jockey) xp = 10;
            else if (l->kind == AK_PIG || l->kind == AK_COW ||
                l->kind == AK_CHICKEN || l->kind == AK_SHEEP ||
                l->kind == AK_MOOSHROOM || l->kind == AK_SQUID)
                /* EntityAnimal's and EntityWaterMob's getExperiencePoints */
                xp = 1 + jr_int_n(&l->an->iew.world_rand.r, 3);
            else if (xp > 0)
            {
                for (int i = 0; i < 5; ++i)
                    if (l->equip[i].id > 0 && l->equipment_drop_chances[i] <= 1.0F)
                        xp += 1 + det_rng_int_n(&l->rand, 3);
            }
            while (xp > 0)
            {
                int split = xp >= 2477 ? 2477 : xp >= 1237 ? 1237 :
                    xp >= 617 ? 617 : xp >= 307 ? 307 : xp >= 149 ? 149 :
                    xp >= 73 ? 73 : xp >= 37 ? 37 : xp >= 17 ? 17 :
                    xp >= 7 ? 7 : xp >= 3 ? 3 : 1;
                xp -= split;
                an_spawn_orb(l->an, split, l->e.pos_x, l->e.pos_y, l->e.pos_z);
            }
        }

        if (IS_SLIME_KIND(l->kind)) slime_set_dead(l, det);   /* EntitySlime.setDead: the split */
        else living_set_dead(l);

        for (int i = 0; i < 20; ++i)
        {
            (void)det_rng_gaussian(&l->rand);
            (void)det_rng_gaussian(&l->rand);
            (void)det_rng_gaussian(&l->rand);
            (void)det_rng_float(&l->rand);
            (void)det_rng_float(&l->rand);
            (void)det_rng_float(&l->rand);
        }

        (void)det;
    }
}

/* EntityLiving.playLivingSound evaluates getSoundPitch (two draws) only when
 * getLivingSound is not null: EntityZombie, EntitySkeleton and EntitySpider
 * return a sound, the animals and the villager do, and EntitySlime and
 * EntityCreeper (EntityLiving's null) do not. */
/* BlockPortal.onEntityCollidedWithBlock (neither riding nor ridden), then
 * Entity.setInPortal: a cooling entity restarts getPortalCooldown's 300,
 * else the first touch takes Direction.getMovementDirection of the step. */
static void living_set_in_portal_cb(void *self)
{
    struct living *l = self;

    if (lv_get(l->riding_entity) != NULL || lv_get(l->ridden_by_entity) != NULL) return;

    if (l->time_until_portal > 0)
    {
        l->time_until_portal = 300;
        return;
    }

    if (!l->in_portal)
        l->teleport_direction = direction_get_movement(l->e.prev_pos_x - l->e.pos_x,
                                                       l->e.prev_pos_z - l->e.pos_z);
    l->in_portal = 1;
}

/* BlockEndPortal.onEntityCollidedWithBlock (neither riding nor ridden):
 * Entity.travelToDimension(1) at once, inside the move's block walk. */
static void living_end_portal_cb(void *self)
{
    struct living *l = self;

    if (lv_get(l->riding_entity) != NULL || lv_get(l->ridden_by_entity) != NULL) return;
    if (l->is_dead || l->e.world->is_remote) return;
    if (l->an != NULL && l->an->portal_travel != NULL) l->an->portal_travel(l->an->portal_travel_ctx, l, 1);
}

/* EntityLivingBase.onEntityUpdate. */
static void living_base_entity_update(struct living *l, det_state *det)
{
    /* EntityWaterMob.onEntityUpdate's `int var1 = this.getAir()`: the air the
     * whole of super.onEntityUpdate runs against, which its own pass reads
     * after (EntityLivingBase resets the field to 300 on its way through) */
    int air_before = l->air;

    l->prev_swing_progress = l->swing_progress;

    /* Entity.onEntityUpdate */
    l->prev_distance_walked_modified = l->e.distance_walked_modified;
    l->e.prev_pos_x = l->e.pos_x;
    l->e.prev_pos_y = l->e.pos_y;
    l->e.prev_pos_z = l->e.pos_z;
    l->prev_rotation_pitch = l->rotation_pitch;
    l->prev_rotation_yaw = l->rotation_yaw;

    /* the portal half, on the server: Entity.getMaxInPortalTime is 0, so an
     * entity travels on the first update after it entered a portal, and the
     * new copy keeps getPortalCooldown's 300 */
    if (l->in_portal)
    {
        if (lv_get(l->riding_entity) == NULL && l->portal_counter++ >= 0)
        {
            l->portal_counter = 0;
            l->time_until_portal = 300;
            int to = l->world->dim == -1 ? 0 : -1;
            if (l->an != NULL && l->an->portal_travel != NULL)
                l->an->portal_travel(l->an->portal_travel_ctx, l, to);
        }
        l->in_portal = 0;
    }
    else
    {
        if (l->portal_counter > 0) l->portal_counter -= 4;
        if (l->portal_counter < 0) l->portal_counter = 0;
    }
    if (l->time_until_portal > 0) --l->time_until_portal;

    /* isSprinting() is false for every animal: the blockcrack branch is dead */
    handle_water_movement(l);

    if (l->e.fire > 0)
    {
        if (l->immune_to_fire)
        {
            l->e.fire -= 4;
            if (l->e.fire < 0) l->e.fire = 0;
        }
        else
        {
            if (l->e.fire % 20 == 0) living_attack_entity_from(l, DMG_ON_FIRE, 1.0F, det);

            --l->e.fire;
        }
    }

    if (handle_lava_movement(l))
    {
        /* Entity.setOnFireFromLava */
        if (!l->immune_to_fire)
        {
            living_attack_entity_from(l, DMG_LAVA, 4.0F, det);
            living_set_fire(l, 15);
        }

        l->e.fall_distance *= 0.5F;
    }

    if (l->e.pos_y < -64.0) living_attack_entity_from(l, DMG_OUT_OF_WORLD, 4.0F, det);

    set_flag(l, 0, l->e.fire > 0);

    l->e.first_update = 0;

    /* EntityLivingBase.onEntityUpdate */
    if (living_is_alive(l) && is_entity_inside_opaque_block(l)) living_attack_entity_from(l, DMG_IN_WALL, 1.0F, det);

    if (l->immune_to_fire) l->e.fire = 0;

    if (l->kind != AK_SQUID && living_is_alive(l) && is_inside_of_material(l, 6))
    {
        if (!living_is_potion_active(l, POT_WATER_BREATHING))
        {
            l->air = decrease_air_supply(l);
        (void)det;

        if (l->air == -20)
        {
            l->air = 0;

            for (int i = 0; i < 8; ++i)
            {
                (void)det_rng_float(&l->rand);
                (void)det_rng_float(&l->rand);
                (void)det_rng_float(&l->rand);
                (void)det_rng_float(&l->rand);
                (void)det_rng_float(&l->rand);
                (void)det_rng_float(&l->rand);
            }

            living_attack_entity_from(l, DMG_DROWN, 2.0F, det);
        }
        }

        /* !worldObj.isClient && isRiding() && ridingEntity instanceof
         * EntityLivingBase: the rider dismounts when its vehicle is a living
         * entity and it goes under. */
        if (lv_get(l->riding_entity) != NULL)
        {
            living_dismount(l);
        }
    }
    else
    {
        l->air = 300;
    }

    if (living_is_alive(l) && is_wet(l)) l->e.fire = 0;

    l->prev_camera_pitch = l->camera_pitch;

    if (l->attack_time > 0) --l->attack_time;
    if (l->hurt_time > 0) --l->hurt_time;
    if (l->hurt_resistant_time > 0) --l->hurt_resistant_time;

    if (l->health <= 0.0F) living_on_death_update(l, det);
    if (l->recently_hit > 0) --l->recently_hit;
    else l->attacking_player = 0;

    if (lv_get(l->last_attacker) != NULL && !living_is_alive(lv_get(l->last_attacker))) l->last_attacker = 0;

    if (lv_get(l->entity_living_to_attack) != NULL)
    {
        if (!living_is_alive(lv_get(l->entity_living_to_attack))) living_set_revenge_target(l, NULL);
        else if (l->ticks_existed - l->revenge_timer > 100) living_set_revenge_target(l, NULL);
    }

    living_update_potion_effects(l, det);
    l->prev_render_yaw_offset = l->render_yaw_offset;
    l->prev_rotation_yaw_head = l->rotation_yaw_head;
    l->prev_rotation_yaw = l->rotation_yaw;
    l->prev_rotation_pitch = l->rotation_pitch;

    /* EntityLiving.onEntityUpdate: the idle sound roll, which draws every tick.
     * It is EntityLiving's override, so the probe's player (EntityLivingBase)
     * never runs it. */
    if (l->kind != SK_PLAYER && l->kind != HK_PLAYER && living_is_alive(l) && det_rng_int_n(&l->rand, 1000) < l->living_sound_time++)
    {
        /* EntityLiving.getTalkInterval is 80 for the hostiles, the ghast, the
         * bat and the villager; EntityWaterMob (the squid), EntityAnimal (the
         * animals) and EntityGolem (the iron golem) return 120. */
        int talk_interval = (l->kind == VK_VILLAGER || IS_SLIME_KIND(l->kind) ||
                             (l->kind >= HK_ZOMBIE && l->kind != AK_SQUID)) ? 80 : 120;
        l->living_sound_time = -talk_interval;

        /* playLivingSound draws only when getLivingSound is not null;
         * getSoundPitch then draws the two floats. EntitySquid's,
         * EntityCreeper's and EntitySlime's names are null, the bat's is
         * conditional on its hanging flag. */
        if (living_has_living_sound(l))
        {
            (void)det_rng_float(&l->rand);
            (void)det_rng_float(&l->rand);
        }
    }

    if (0)
    {
    }

    /* EntityWaterMob.onEntityUpdate's own air pass runs after the whole of
     * super.onEntityUpdate (Entity, EntityLivingBase and EntityLiving, the
     * hurt-time decrement and the idle sound roll included): the drown damage
     * it can deal sets hurtTime = 10 with no decrement in the same tick.
     * canBreatheUnderwater is true for the squid, so EntityLivingBase's own
     * decay never runs and this pass reads the squid's isInWater (which pushes
     * the 0.014 flow) instead. */
    if (l->kind == AK_SQUID)
    {
        if (living_is_alive(l) && !squid_is_in_water(l))
        {
            l->air = air_before - 1;

            if (l->air == -20)
            {
                l->air = 0;

                /* EntityWaterMob.onEntityUpdate has no particle loop: only
                 * EntityLivingBase's branch draws the 8 pairs of floats */
                living_attack_entity_from(l, DMG_DROWN, 2.0F, det);
            }
        }
        else
        {
            l->air = 300;
        }
    }
}

/* EntityLiving.getLivingSound, null for the kinds that do not override it:
 * EntitySquid, EntityCreeper and the slimes. EntityBat's answer is
 * `hanging && rand.nextInt(4) != 0 ? null : "mob.bat.idle"`, which draws. */
static int living_has_living_sound(struct living *l)
{
    if (l->kind == AK_SQUID || l->kind == HK_CREEPER || IS_SLIME_KIND(l->kind)) return 0;

    if (l->kind == AK_BAT && (l->data_watcher_16 & 1) && det_rng_int_n(&l->rand, 4) != 0) return 0;

    return 1;
}

/* EntityLivingBase.getHurtSound / getDeathSound: every kind this lane carries
 * overrides them with a name except the squid, whose two answers are null (so
 * EntityLivingBase.attackEntityFrom's playSound, and with it the two
 * getSoundPitch draws, never runs for it). */
static int living_has_hurt_sound(struct living *l)
{
    return l->kind != AK_SQUID;
}

static int living_has_death_sound(struct living *l)
{
    return l->kind != AK_SQUID;
}

static int decrease_air_supply(struct living *l)
{
    return living_decrease_air_supply(l, l->air);
}

/* World.canLightningStrikeAt over the living's world: the rain half of
 * Entity.isWet (the an_world's raining is World.isRaining). */
static int can_lightning_strike_at(struct living *l, int x, int y, int z)
{
    if (l->an == NULL || !l->an->raining) return 0;
    if (!world_can_block_see_the_sky(l->world, x, y, z)) return 0;
    if (world_get_precipitation_height(l->world, x, z) > y) return 0;

    int biome = biome_at(l->world, x, z);

    if (BIOMES[biome].snow) return 0;
    if (world_can_snow_at(l->world, x, y, z, 0)) return 0;

    return BIOMES[biome].lightning;
}

/* Entity.isWet: inWater, or rain on the feet cell or the head cell. */
static int is_wet(struct living *l)
{
    if (l->is_in_water) return 1;
    /* can_lightning_strike_at's first test, before the floors it takes */
    if (l->an == NULL || !l->an->raining) return 0;

    int x = mh_floor(l->e.pos_x);
    int z = mh_floor(l->e.pos_z);

    return can_lightning_strike_at(l, x, mh_floor(l->e.pos_y), z) ||
           can_lightning_strike_at(l, x, mh_floor(l->e.pos_y + (double)l->e.height), z);
}

float living_eye_height(struct living *l)
{
    /* EntityPlayer.getEyeHeight's 0.12F; EntityPlayerMP overrides it with
     * 1.62F (the probes' player is a plain EntityPlayer) */
    if (l->kind == SK_PLAYER || l->kind == HK_PLAYER) return l->player_mp ? 1.62F : 0.12F;

    return l->e.height * 0.85F;
}

/* ------------------------------------------------------- attack and death */

struct dmg_source {
    int entity_less;   /* every source the environment reaches has no entity */
};


/* EntityLivingBase.attackEntityFrom. kind is the DamageSource family; attacker
 * is DamageSource.getEntity() (NULL when the source carries none). This is the
 * base body; living_attack_entity_from_attacker is the virtual dispatch. */
int living_attack_entity_from_attacker_base(struct living *l, struct living *attacker, int source, float amount,
                                            det_state *det)
{
    if (l->external_attack != NULL)
    {
        /* the external body reads the attacker (DamageSource.getEntity())
         * off damage_attacker for the knockback */
        l->damage_attacker = lv_ref(attacker);
        int landed = l->external_attack(l->external_attack_ctx, source, amount);
        l->damage_attacker = 0;
        return landed;
    }
    if (det == NULL && l->an != NULL) det = l->an->det;
    if (l->invulnerable) return 0;

    l->entity_age = 0;

    if (l->health <= 0.0F) return 0;

    if ((source == DMG_IN_FIRE || source == DMG_ON_FIRE || source == DMG_LAVA) &&
        (l->immune_to_fire || living_is_potion_active(l, POT_FIRE_RESISTANCE)))
    {
        return 0;
    }

    l->entity_age = 0;
    if (l->kind == HK_PLAYER && amount == 0.0F) return 0;

    /* DamageSource.anvil on a worn helmet: the helmet takes
     * (int)(amount * 4 + nextFloat() * amount * 2) through damageItem and
     * the hit keeps three quarters */
    if (source == DMG_ANVIL && l->equip[4].id > 0)
    {
        float f = det_rng_float(&l->rand);
        ench_damage_equipment(l, 4, (int)(amount * 4.0F + f * amount * 2.0F), det);
        amount *= 0.75F;
    }

    l->limb_swing_amount = 1.5F;
    int var3 = 1;

    l->damage_attacker = lv_ref(attacker);
    if ((float)l->hurt_resistant_time > (float)l->max_hurt_resistant_time / 2.0F)
    {
        if (amount <= l->last_damage) { l->damage_attacker = 0; return 0; }

        living_damage_dispatch(l, source, amount - l->last_damage, det);
        l->last_damage = amount;
        var3 = 0;
    }
    else
    {
        l->last_damage = amount;
        l->prev_health = l->health;
        l->hurt_resistant_time = l->max_hurt_resistant_time;
        living_damage_dispatch(l, source, amount, det);
        l->max_hurt_time = 10;
        l->hurt_time = 10;
    }
    l->damage_attacker = 0;

    l->attacked_at_yaw = 0.0F;

    if (attacker != NULL)
    {
        living_set_revenge_target(l, attacker);
        if (attacker->kind == SK_PLAYER || attacker->kind == HK_PLAYER)
        {
            l->recently_hit = 100;
            l->attacking_player = lv_ref(attacker);
        }
    }

    if (var3)
    {
        if (source != DMG_DROWN) living_mark_attacked(l);
        if (attacker != NULL)
        {
            double var9 = attacker->e.pos_x - l->e.pos_x;
            double var7 = attacker->e.pos_z - l->e.pos_z;

            while (var9 * var9 + var7 * var7 < 1.0E-4)
            {
                double x1 = det_math_random_role(det, det_role(det));
                double x2 = det_math_random_role(det, det_role(det));
                var9 = (x1 - x2) * 0.01;
                double z1 = det_math_random_role(det, det_role(det));
                double z2 = det_math_random_role(det, det_role(det));
                var7 = (z1 - z2) * 0.01;
            }

            l->attacked_at_yaw = (float)(fd_atan2(var7, var9) * 180.0 / 3.141592653589793) - l->rotation_yaw;
            living_knock_back(l, amount, var9, var7);
        }
        else if (l->hit_src_arrow)
        {
            /* a shooterless arrow is its own source entity: the same
             * knockback from the arrow's position */
            double var9 = l->hit_src_x - l->e.pos_x;
            double var7 = l->hit_src_z - l->e.pos_z;

            while (var9 * var9 + var7 * var7 < 1.0E-4)
            {
                double x1 = det_math_random_role(det, det_role(det));
                double x2 = det_math_random_role(det, det_role(det));
                var9 = (x1 - x2) * 0.01;
                double z1 = det_math_random_role(det, det_role(det));
                double z2 = det_math_random_role(det, det_role(det));
                var7 = (z1 - z2) * 0.01;
            }

            l->attacked_at_yaw = (float)(fd_atan2(var7, var9) * 180.0 / 3.141592653589793) - l->rotation_yaw;
            living_knock_back(l, amount, var9, var7);
        }
        else
        {
            /* every source the environment reaches has no entity, so the knockback
             * is not taken and the yaw is the random one */
            l->attacked_at_yaw = (float)((int)(det_math_random_role(det, det_role(det)) * 2.0) * 180);
        }
    }

    if (l->health <= 0.0F)
    {
        /* the death sound's two pitch draws, only when getDeathSound is not
         * null (the squid's is) */
        if (var3 && living_has_death_sound(l))
        {
            (void)det_rng_float(&l->rand);
            (void)det_rng_float(&l->rand);
        }

        living_on_death(l, attacker, source, det);
    }
    else if (var3 && living_has_hurt_sound(l))
    {
        /* the hurt sound's two pitch draws: playSound is skipped entirely when
         * getHurtSound is null, which is the squid's answer */
        (void)det_rng_float(&l->rand);
        (void)det_rng_float(&l->rand);
    }
    /* EntityMob.attackEntityFrom's tail. The enderman overrides
     * attackEntityFrom, but its direct branch still reaches EntityMob's. */
if ((l->kind >= HK_ZOMBIE && l->kind <= HK_SPIDER) || l->kind == HK_CAVE_SPIDER || l->kind == HK_ENDERMAN || l->kind == HK_WITCH || l->kind == HK_SILVERFISH || l->kind == HK_PIGMAN || l->kind == HK_BLAZE)
    {
        /* EntityMob.attackEntityFrom's tail: never its own rider or
         * vehicle */
        if (attacker != NULL && attacker != l && attacker != lv_get(l->ridden_by_entity) &&
            attacker != lv_get(l->riding_entity))
        {
            l->entity_to_attack = lv_ref(attacker);
            l->entity_to_attack_gone = 0;
        }
        else if (attacker == NULL && l->hit_src_arrow)
        {
            /* entityToAttack becomes the arrow, which dies hitting */
            l->entity_to_attack = 0;
            l->entity_to_attack_gone = 1;
        }
    }

    return 1;
}

/* EntityZombie.attackEntityFrom's tail (the pigman's too, through its
 * super): the target is the attack target, else the entity to attack, else
 * the source's living; on HARD, a nextFloat under spawnReinforcements calls
 * one in (the replay's on_reinforce). */
static void zombie_reinforce(struct living *l, struct living *attacker, det_state *det)
{
    struct living *target = lv_get(l->attack_target);
    if (target == NULL) target = lv_get(l->entity_to_attack);
    if (target == NULL) target = attacker;
    if (target == NULL || l->an == NULL || l->an->difficulty != 3) return;
    if (!((double)det_rng_float(&l->rand) < attrs_value(&l->attrs.a[ATTR_SPAWN_REINFORCEMENTS]))) return;
    if (l->an->on_reinforce != NULL) l->an->on_reinforce(l->an, l, target, det, l->an->constructor_ctx);
}

/* EntityLivingBase.attackEntityFrom, the virtual dispatch: EntityEnderman
 * overrides it (the screaming flag, the aggressive flag, the teleports an
 * EntityDamageSourceIndirect triggers: arrows, thrown items, fireballs and
 * indirect magic); a player attacker is DamageSource.getEntity() being an
 * EntityPlayer, which for an indirect source is the shooter. */
int living_attack_entity_from_attacker(struct living *l, struct living *attacker, int source, float amount,
                                       det_state *det)
{
    /* EntityBat.attackEntityFrom: a server bat lets go of its ceiling before
     * super runs (the client copy is the server's watcher) */
    if (l->kind == AK_BAT && !l->invulnerable && !world_is_client(l->world)) l->data_watcher_16 &= ~1;
    if (l->kind == HK_SILVERFISH && !l->invulnerable) silverfish_hurt(l, source, attacker);
    /* EntityBat.attackEntityFrom: a hit wakes a hanging bat first */
    if (l->kind == AK_BAT && !l->invulnerable && !world_is_client(l->world)) l->data_watcher_16 &= ~1;
    if (l->kind == HK_ENDERMAN)
    {
        int attacker_is_player = attacker != NULL && (attacker->kind == HK_PLAYER || attacker->kind == SK_PLAYER);
        return enderman_attack_entity_from(l, attacker, source, amount, dmg_is_indirect(source),
                                           attacker_is_player, det);
    }

    /* EntityAnimal.attackEntityFrom's override body (the five farm animals),
     * after its invulnerability test, whoever the attacker (a player's hit
     * ends the love a feeding started); isAIEnabled is true for all of
     * them, so the movement-speed bonus is skipped */
    if (l->kind < AK_KINDS && !l->invulnerable)
    {
        l->fleeing_tick = 60;
        l->entity_to_attack = 0;
        l->in_love = 0;
    }

    if (l->kind == HK_PIGMAN)
    {
        int landed = pigman_attack_entity_from(l, attacker, source, amount, det);
        if (landed) zombie_reinforce(l, attacker, det);
        return landed;
    }

    int landed = living_attack_entity_from_attacker_base(l, attacker, source, amount, det);
    if (landed && l->kind == HK_ZOMBIE) zombie_reinforce(l, attacker, det);
    return landed;
}

/* EntityLivingBase.attackEntityFrom. kind is the DamageSource family. */
void living_attack_entity_from(struct living *l, int source, float amount, det_state *det)
{
    if (l->external_attack != NULL)
    {
        l->external_attack(l->external_attack_ctx, source, amount);
        return;
    }
    if (l->kind == HK_ENDERMAN)
    {
        (void)enderman_attack_entity_from(l, NULL, source, amount, dmg_is_indirect(source), 0, det);
        return;
    }

    living_attack_entity_from_attacker(l, NULL, source, amount, det);
}

/* EntityLivingBase.onDeath. */
static void living_on_death(struct living *l, struct living *attacker, int source, det_state *det)
{
    /* EntityVillager.onDeath and EntityIronGolem.onDeath before super */
    if (l->kind == VK_VILLAGER) villager_death_village(l, attacker);
    else if (l->kind == VK_IRON_GOLEM) iron_golem_death_village(l);

    /* EntityLivingBase.onDeath: var2.onKillEntity(this) before dead is set.
     * EntityZombie's (a pigman is one too): a villager killed at NORMAL is
     * infected on the zombie's nextBoolean false, at HARD always */
    if (attacker != NULL && (attacker->kind == HK_ZOMBIE || attacker->kind == HK_PIGMAN) && l->kind == VK_VILLAGER &&
        attacker->an != NULL && !world_is_client(l->world))
    {
        int diff = attacker->an->difficulty;
        if ((diff == 2 || diff == 3) && !(diff != 3 && det_rng_bool(&attacker->rand)) && attacker->an->on_infect != NULL)
        {
            attacker->an->on_infect(attacker->an, attacker, l, attacker->an->constructor_ctx);
            /* the infection's 1016 */
            env_aux_sfx(attacker->world, 1016, (int)attacker->e.pos_x, (int)attacker->e.pos_y, (int)attacker->e.pos_z, 0);
        }
    }

    l->dead = 1;

    /* EntityLivingBase.onDeath's var3 (func_94060_bK) and var2
     * (DamageSource.getEntity(), attacker here): var3.addToPlayerScore
     * first (scoreValue is never assigned, so 0), then
     * var2.onKillEntity. Only the player's hooks change state. var3 is the
     * CombatTracker's best attacker (func_94550_c) when it has one, else
     * attackingPlayer, else the revenge target. At a death the tracker
     * holds only the killing blow: damageEntity's setHealth runs before
     * func_94547_a, whose func_94549_h sees the fighter dead and clears
     * every earlier entry. So the tracker's answer is the killing source's
     * entity when that is a living (the player, a mob, a creeper's own
     * blast), and none for a source without one (fire, lava, a fall). */
    {
        struct living *credit = attacker != NULL ? attacker
                              : lv_get(l->attacking_player) != NULL ? lv_get(l->attacking_player)
                              : lv_get(l->entity_living_to_attack);
        if (credit != NULL && credit->on_death != NULL)
            credit->on_death(credit->on_death_ctx, l, source, 1, credit == attacker);
        if (attacker != NULL && attacker != credit && attacker->on_death != NULL)
            attacker->on_death(attacker->on_death_ctx, l, source, 0, 1);
    }

    if (!world_is_client(l->world))
    {
        int looting = ench_looting(attacker);
        int can_drop = (l->kind >= HK_ZOMBIE) || (l->growing_age >= 0);

        if (can_drop && living_mob_loot(l->world))
        {
            living_drop_few_items(l, l->recently_hit > 0, looting, det);
            living_drop_equipment(l, l->recently_hit > 0, looting, det);

            if (l->recently_hit > 0)
            {
                int var5 = det_rng_int_n(&l->rand, 200) - looting;
                if (var5 < 5)
                {
                    if (l->kind == HK_ZOMBIE)
                    {
                        int rare = det_rng_int_n(&l->rand, 3);
                        int item_id = (rare == 0) ? 265 : (rare == 1) ? 391 : 392;
                        if (l->an)
                        {
                            an_spawn_item(l->an, l->an->n, l->e.pos_x, l->e.pos_y, l->e.pos_z, item_id, 0, 1, 10);
                        }
                    }
                    else if (l->kind == HK_PIGMAN)
                    {
                        /* EntityPigZombie.dropRareDrop: the gold ingot, no draw */
                        if (l->an)
                        {
                            an_spawn_item(l->an, l->an->n, l->e.pos_x, l->e.pos_y, l->e.pos_z, 266, 0, 1, 10);
                        }
                    }
                    else if (l->kind == HK_SKELETON)
                    {

                        if (l->skeleton_type == 1 && l->an)
                        {
                            an_spawn_item(l->an, l->an->n, l->e.pos_x, l->e.pos_y, l->e.pos_z, 397, 1, 1, 10);
                        }
                    }
                }
            }
        }
    }

    /* EntityCreeper.onDeath after super: a skeleton's kill (the arrow's
     * getEntity() is the shooter) drops one of the twelve records,
     * func_145779_a -> entityDropItem at offset 0 */
    if (l->kind == HK_CREEPER && attacker != NULL && attacker->kind == HK_SKELETON && l->an)
    {
        int record = 2256 + det_rng_int_n(&l->rand, 2267 - 2256 + 1);
        an_spawn_item(l->an, l->an->n, l->e.pos_x, l->e.pos_y, l->e.pos_z, record, 0, 1, 10);
    }
}

/* EntityLivingBase.kill. */

/* ------------------------------------------------------------- the jumps */

static void living_jump(struct living *l)
{
    if (l->kind == SK_MAGMA_CUBE)
    {
        slime_jump_override(l);   /* EntityMagmaCube.jump */
        return;
    }

    l->e.motion_y = 0.41999998688697815;

    if (living_is_potion_active(l, POT_JUMP))
    {
        const struct potion_effect *eff = living_get_potion_effect(l, POT_JUMP);
        l->e.motion_y += (double)((float)(eff->amplifier + 1) * 0.1F);
    }

    l->is_air_borne = 1;
}

/* Entity.moveFlying. */
static void living_move_flying(struct living *l, float strafe, float forward, float friction)
{
    float v4 = strafe * strafe + forward * forward;

    if (v4 >= 1.0E-4F)
    {
        v4 = sqrt_float(v4);

        if (v4 < 1.0F) v4 = 1.0F;

        v4 = friction / v4;
        strafe *= v4;
        forward *= v4;
        float v5 = mh_sin_deg(l->rotation_yaw);
        float v6 = mh_cos_deg(l->rotation_yaw);
        l->e.motion_x += (double)(strafe * v6 - forward * v5);
        l->e.motion_z += (double)(forward * v6 + strafe * v5);
    }
}

/* EntityLivingBase.moveEntityWithHeading. */

static void living_move_entity_with_heading_base(struct living *l, float strafe, float forward, det_state *det)
{
    double v8;
    double start_x = l->e.pos_x;
    double start_y = l->e.pos_y;
    double start_z = l->e.pos_z;

    if (l->is_in_water)
    {
        v8 = l->e.pos_y;
        living_move_flying(l, strafe, forward, living_is_ai_enabled(l) ? 0.04F : 0.02F);
        living_move(l, l->e.motion_x, l->e.motion_y, l->e.motion_z);
        l->e.motion_x *= 0.800000011920929;
        l->e.motion_y *= 0.800000011920929;
        l->e.motion_z *= 0.800000011920929;
        l->e.motion_y -= 0.02;

        if (l->e.is_collided_horizontally
            && is_offset_position_in_liquid(l, l->e.motion_x, l->e.motion_y + 0.6000000238418579 - l->e.pos_y + v8,
                                            l->e.motion_z))
        {
            l->e.motion_y = 0.30000001192092896;
        }
    }
    else if (handle_lava_movement(l))
    {
        v8 = l->e.pos_y;
        living_move_flying(l, strafe, forward, 0.02F);
        living_move(l, l->e.motion_x, l->e.motion_y, l->e.motion_z);
        l->e.motion_x *= 0.5;
        l->e.motion_y *= 0.5;
        l->e.motion_z *= 0.5;
        l->e.motion_y -= 0.02;

        if (l->e.is_collided_horizontally
            && is_offset_position_in_liquid(l, l->e.motion_x, l->e.motion_y + 0.6000000238418579 - l->e.pos_y + v8,
                                            l->e.motion_z))
        {
            l->e.motion_y = 0.30000001192092896;
        }
    }
    else
    {
        float v3 = 0.91F;

        if (l->e.on_ground)
        {
            int bx = mh_floor(l->e.pos_x);
            int by = mh_floor(l->e.bounding_box.min_y) - 1;
            int bz = mh_floor(l->e.pos_z);
            v3 = BLOCKS[world_get_block(l->world, bx, by, bz) & 4095].slipperiness * 0.91F;
        }

        float v4 = 0.16277136F / (v3 * v3 * v3);
        float v5;

        if (l->e.on_ground) v5 = living_get_ai_move_speed(l) * v4;
        else v5 = 0.02F;   /* jumpMovementFactor */

        living_move_flying(l, strafe, forward, v5);
        v3 = 0.91F;

        if (l->e.on_ground)
        {
            int bx = mh_floor(l->e.pos_x);
            int by = mh_floor(l->e.bounding_box.min_y) - 1;
            int bz = mh_floor(l->e.pos_z);
            v3 = BLOCKS[world_get_block(l->world, bx, by, bz) & 4095].slipperiness * 0.91F;
        }

        if (is_on_ladder(l))
        {
            float v6 = 0.15F;

            if (l->e.motion_x < (double)(-v6)) l->e.motion_x = (double)(-v6);
            if (l->e.motion_x > (double)v6) l->e.motion_x = (double)v6;
            if (l->e.motion_z < (double)(-v6)) l->e.motion_z = (double)(-v6);
            if (l->e.motion_z > (double)v6) l->e.motion_z = (double)v6;
            l->e.fall_distance = 0.0F;

            if (l->e.motion_y < -0.15) l->e.motion_y = -0.15;

            /* a sneaking player only */
        }

        living_move(l, l->e.motion_x, l->e.motion_y, l->e.motion_z);

        if (l->e.is_collided_horizontally && is_on_ladder(l)) l->e.motion_y = 0.2;

        l->e.motion_y -= 0.08;
        l->e.motion_y *= 0.9800000190734863;
        l->e.motion_x *= (double)v3;
        l->e.motion_z *= (double)v3;
    }

    l->prev_limb_swing_amount = l->limb_swing_amount;
    v8 = l->e.pos_x - l->e.prev_pos_x;
    double v9 = l->e.pos_z - l->e.prev_pos_z;
    float v10 = (float)sqrt_double(v8 * v8 + v9 * v9) * 4.0F;

    if (v10 > 1.0F) v10 = 1.0F;

    l->limb_swing_amount += (v10 - l->limb_swing_amount) * 0.4F;
    l->limb_swing += l->limb_swing_amount;

    if (l->kind == HK_PLAYER)
    {
        double dx = l->e.pos_x - start_x;
        double dy = l->e.pos_y - start_y;
        double dz = l->e.pos_z - start_z;
        int var7;

        if (is_inside_of_material(l, 6))
        {
            float d = (float)sqrt(dx * dx + dy * dy + dz * dz);
            var7 = (int)floorf(d * 100.0f + 0.5f);
            if (var7 > 0)
            {
                player_add_exhaustion(l, 0.015f * (float)var7 * 0.01f);
            }
        }
        else if (l->is_in_water)
        {
            float d = (float)sqrt(dx * dx + dz * dz);
            var7 = (int)floorf(d * 100.0f + 0.5f);
            if (var7 > 0)
            {
                player_add_exhaustion(l, 0.015f * (float)var7 * 0.01f);
            }
        }
        else if (is_on_ladder(l))
        {
            /* distanceClimbedStat: no exhaustion */
        }
        else if (l->e.on_ground)
        {
            float d = (float)sqrt(dx * dx + dz * dz);
            var7 = (int)floorf(d * 100.0f + 0.5f);
            if (var7 > 0)
            {
                player_add_exhaustion(l, 0.01f * (float)var7 * 0.01f);
            }
        }

        if (l->food_exhaustion > 40.0f) l->food_exhaustion = 40.0f;
    }

    (void)det;
}

/* EntityFlying.moveEntityWithHeading. The water and lava arms are
 * EntityLivingBase's own (the drag constants and the float 0.02 friction are
 * the same); the air arm drops the gravity step (motionY stays) and the ladder
 * clamp, and the 0.91 drag multiplies all three motion parts. */
void living_move_entity_with_heading_flying(struct living *l, float strafe, float forward)
{
    if (l->is_in_water)
    {
        living_move_flying(l, strafe, forward, 0.02F);
        living_move(l, l->e.motion_x, l->e.motion_y, l->e.motion_z);
        l->e.motion_x *= 0.800000011920929;
        l->e.motion_y *= 0.800000011920929;
        l->e.motion_z *= 0.800000011920929;
    }
    else if (handle_lava_movement(l))
    {
        living_move_flying(l, strafe, forward, 0.02F);
        living_move(l, l->e.motion_x, l->e.motion_y, l->e.motion_z);
        l->e.motion_x *= 0.5;
        l->e.motion_y *= 0.5;
        l->e.motion_z *= 0.5;
    }
    else
    {
        float v3 = 0.91F;

        if (l->e.on_ground)
        {
            int bx = mh_floor(l->e.pos_x);
            int by = mh_floor(l->e.bounding_box.min_y) - 1;
            int bz = mh_floor(l->e.pos_z);
            v3 = BLOCKS[world_get_block(l->world, bx, by, bz) & 4095].slipperiness * 0.91F;
        }

        float v4 = 0.16277136F / (v3 * v3 * v3);
        living_move_flying(l, strafe, forward, l->e.on_ground ? 0.1F * v4 : 0.02F);
        v3 = 0.91F;

        if (l->e.on_ground)
        {
            int bx = mh_floor(l->e.pos_x);
            int by = mh_floor(l->e.bounding_box.min_y) - 1;
            int bz = mh_floor(l->e.pos_z);
            v3 = BLOCKS[world_get_block(l->world, bx, by, bz) & 4095].slipperiness * 0.91F;
        }

        living_move(l, l->e.motion_x, l->e.motion_y, l->e.motion_z);

        l->e.motion_x *= (double)v3;
        l->e.motion_y *= (double)v3;
        l->e.motion_z *= (double)v3;
    }

    l->prev_limb_swing_amount = l->limb_swing_amount;
    double v8 = l->e.pos_x - l->e.prev_pos_x;
    double v9 = l->e.pos_z - l->e.prev_pos_z;
    float v10 = (float)sqrt(v8 * v8 + v9 * v9) * 4.0F;   /* MathHelper.sqrt_double */

    if (v10 > 1.0F) v10 = 1.0F;

    l->limb_swing_amount += (v10 - l->limb_swing_amount) * 0.4F;
    l->limb_swing += l->limb_swing_amount;
}

/* EntityPlayer.moveEntityWithHeading: the base body, then addMovementStat.
 * capabilities.isFlying is false for the probe's player, so only the else branch
 * of vanilla's override applies. */
void living_move_entity_with_heading(struct living *l, float strafe, float forward, det_state *det)
{
    if (l->kind != SK_PLAYER)
    {
        living_move_entity_with_heading_base(l, strafe, forward, det);
        return;
    }

    double var3 = l->e.pos_x;
    double var5 = l->e.pos_y;
    double var7 = l->e.pos_z;
    living_move_entity_with_heading_base(l, strafe, forward, det);
    player_add_movement_stat(l, l->e.pos_x - var3, l->e.pos_y - var5, l->e.pos_z - var7);
}

/* EntityLivingBase.fall (the fall damage). */
void living_fall(struct living *l, float distance)
{
    /* EntityMagmaCube.fall and EntityGolem.fall are empty: no fall damage
     * and no rider fall */
    if (l->kind == SK_MAGMA_CUBE || l->kind == VK_IRON_GOLEM) return;

    /* super.fall (Entity.fall): the rider falls with its vehicle, first */
    if (lv_get(l->ridden_by_entity) != NULL)
    {
        struct living *r = lv_get(l->ridden_by_entity);
        if (r->player_mp) ride_player_fall(l, distance);
        else if (r->kind_fall != NULL) r->kind_fall(r, distance);
        else living_fall(r, distance);
    }

    float var3 = 0.0F;
    if (living_is_potion_active(l, POT_JUMP))
    {
        const struct potion_effect *eff = living_get_potion_effect(l, POT_JUMP);
        var3 = (float)(eff->amplifier + 1);
    }
    int var4 = ceil_float_int(distance - 3.0F - var3);

    if (var4 > 0)
    {
        /* playSound(func_146067_o(var4), 1.0F, 1.0F) draws nothing */
        living_attack_entity_from(l, DMG_FALL, (float)var4, NULL);
    }

    /* EntityPig.fall: a player rider off more than 5 blocks earns flyPig */
    if (l->kind == AK_PIG && distance > 5.0F) ride_pig_fly(l);
}

/* the world Random of the world ticking the entities (living_set_walking_world) */
#define living_walking_world (nw_env->living.walking_world)

/* EntityLivingBase.updateFallState. */
static void living_fall_state(struct entity *e, double dy, int on_ground)
{
    struct living *l = (struct living *)e->self;

    /* EntityFlying.updateFallState and fall are both empty: no fall distance
     * and no fall damage */
    if (l->kind == GK_GHAST) return;

    /* EntityBat.updateFallState is a no-op: the bat's fall distance never
     * accumulates and the landing effects never fire for it */
    if (l->kind == AK_BAT) return;

    /* isInWater() is virtual: the squid's own box test, which pushes the
     * flow on its own, else the field */
    if (!(l->kind == AK_SQUID ? squid_is_in_water(l) : l->is_in_water)) handle_water_movement(l);

    if (on_ground && l->e.fall_distance > 0.0F)
    {
        int v4 = mh_floor(l->e.pos_x);
        int v5 = mh_floor(l->e.pos_y - 0.20000000298023224 - (double)l->e.y_offset);
        int v6 = mh_floor(l->e.pos_z);
        int bid = world_get_block(l->world, v4, v5, v6) & 4095;

        if (BLOCKS[bid].material == 0)
        {
            int v8 = BLOCKS[world_get_block(l->world, v4, v5 - 1, v6) & 4095].render_type;

            if (v8 == 11 || v8 == 32 || v8 == 21) bid = world_get_block(l->world, v4, v5 - 1, v6) & 4095;
        }
        else if (l->e.fall_distance > 3.0F)
        {
            /* the fall's dust */
            env_aux_sfx(l->world, 2006, v4, v5, v6, ceil_float_int(l->e.fall_distance - 3.0F));
        }

        /* Block.onFallenUpon: BlockFarmland's is the only body that is not
         * empty. It draws World.rand for any fall distance, and mobGriefing
         * (never changed on these tapes) lets every living trample; a
         * context with no world Random (the probes) has no World.rand. */
        if (bid == 60 && living_walking_world != NULL &&
            jr_float(living_walking_world) < l->e.fall_distance - 0.5F)
            world_set_block(l->world, v4, v5, v6, 3, 0, 3);
    }

    /* Entity.updateFallState */
    if (on_ground)
    {
        if (l->e.fall_distance > 0.0F)
        {
            if (l->kind_fall != NULL) l->kind_fall(l, l->e.fall_distance);
            else living_fall(l, l->e.fall_distance);
            l->e.fall_distance = 0.0F;
        }
    }
    else if (dy < 0.0)
    {
        l->e.fall_distance = (float)((double)l->e.fall_distance - dy);
    }
}

/* ------------------------------------------------------------- the heading */

/* EntityLivingBase.func_110146_f: the body helper on the AI path. */
static float living_func_110146_f(struct living *l, float target, float speed, det_state *det)
{
    if (living_is_ai_enabled(l))
    {
        body_helper_update(l);
        return speed;
    }

    float var3 = wrap_angle_float(target - l->render_yaw_offset);
    l->render_yaw_offset += var3 * 0.3F;
    float var4 = wrap_angle_float(l->rotation_yaw - l->render_yaw_offset);
    int var5 = var4 < -90.0F || var4 >= 90.0F;

    if (var4 < -75.0F) var4 = -75.0F;
    if (var4 >= 75.0F) var4 = 75.0F;

    l->render_yaw_offset = l->rotation_yaw - var4;

    if (var4 * var4 > 2500.0F) l->render_yaw_offset += var4 * 0.2F;

    if (var5) speed *= -1.0F;

    (void)det;
    return speed;
}

/* instanceof EntityMob: the ported kinds under it (the slimes and the ghast
 * are not mobs; each has its own PEACEFUL check). */
static int living_is_entity_mob(const struct living *l)
{
    switch (l->kind)
    {
    case HK_ZOMBIE: case HK_SKELETON: case HK_CREEPER: case HK_SPIDER: case HK_CAVE_SPIDER:
    case HK_ENDERMAN: case HK_WITCH: case HK_SILVERFISH: case HK_PIGMAN: case HK_BLAZE:
        return 1;
    default:
        return 0;
    }
}

/* EntityLivingBase.onUpdate. */
static void living_on_update(struct living *l, det_state *det)
{
    if (l->kind == HK_SILVERFISH) l->render_yaw_offset = l->rotation_yaw;
    /* EntitySlime.onUpdate's head: the squish bookkeeping, before super */
    if (IS_SLIME_KIND(l->kind)) slime_pre_on_update(l, det);

    /* EntityCreeper.onUpdate's head: the fuse, before super */
    if (l->kind == HK_CREEPER) creeper_pre_on_update(l, det);
    if (l->kind == HK_PIGMAN) pigman_pre_on_update(l, det);
    if ((l->kind == HK_ZOMBIE || l->kind == HK_PIGMAN) && !world_is_client(l->world) && l->zombie_is_converting)
        zombie_conversion_tick(l);

    /* Entity.onUpdate is onEntityUpdate */
    living_base_entity_update(l, det);

    /* EntityLivingBase.onUpdate's server half: the arrows stuck in the body
     * and the equipment pass (applied where the equipment changes: see
     * living_held_item_modifiers; its S04s leave the slots as this pass saw
     * them for the client copy, equip_sent); the CombatTracker's periodic
     * reset changes nothing a death reads */
    if (!world_is_client(l->world))
    {
        living_arrow_decay(l);
        /* stored only when it changed: a write dirties five lines of
         * every updated living each tick (lane/villscale) */
        if (memcmp(l->equip_sent, l->equip, sizeof l->equip_sent)) memcpy(l->equip_sent, l->equip, sizeof l->equip_sent);
        if (!l->equip_sent_valid) l->equip_sent_valid = 1;
    }

    /* EntityLivingBase.onUpdate's tail */
    l->on_living_update(l, det);

    /* EntitySpider.onUpdate's tail after super(): the climb flag */
    if (l->kind == HK_SPIDER || l->kind == HK_CAVE_SPIDER) spider_on_update_tail(l);

    double v9 = l->e.pos_x - l->e.prev_pos_x;
    double v10 = l->e.pos_z - l->e.prev_pos_z;
    float v5 = (float)(v9 * v9 + v10 * v10);
    float v6 = l->render_yaw_offset;
    float v7 = 0.0F;
    float v8 = 0.0F;

    if (v5 > 0.0025000002F)
    {
        v8 = 1.0F;
        v7 = (float)sqrt((double)v5) * 3.0F;
        /* the determinized Java runs this in float: (float)StrictMath.atan2(...) * 180.0F / (float)Math.PI - 90.0F */
        v6 = (float)fd_atan2(v10, v9) * 180.0F / 3.1415927F - 90.0F;
    }

    if (l->swing_progress > 0.0F) v6 = l->rotation_yaw;

    if (!l->e.on_ground) v8 = 0.0F;

    l->field_110154_aX += (v8 - l->field_110154_aX) * 0.3F;
    v7 = living_func_110146_f(l, v6, v7, det);

    while (l->rotation_yaw - l->prev_rotation_yaw < -180.0F) l->prev_rotation_yaw -= 360.0F;
    while (l->rotation_yaw - l->prev_rotation_yaw >= 180.0F) l->prev_rotation_yaw += 360.0F;
    while (l->render_yaw_offset - l->prev_render_yaw_offset < -180.0F) l->prev_render_yaw_offset -= 360.0F;
    while (l->render_yaw_offset - l->prev_render_yaw_offset >= 180.0F) l->prev_render_yaw_offset += 360.0F;
    while (l->rotation_pitch - l->prev_rotation_pitch < -180.0F) l->prev_rotation_pitch -= 360.0F;
    while (l->rotation_pitch - l->prev_rotation_pitch >= 180.0F) l->prev_rotation_pitch += 360.0F;
    while (l->rotation_yaw_head - l->prev_rotation_yaw_head < -180.0F) l->prev_rotation_yaw_head -= 360.0F;
    while (l->rotation_yaw_head - l->prev_rotation_yaw_head >= 180.0F) l->prev_rotation_yaw_head += 360.0F;

    l->field_70764_aw += v7;

    /* EntityLiving.onUpdate's server tail: updateLeashedState */
    leash_update(l);

    /* EntityMob.onUpdate after super: setDead on a server world at PEACEFUL */
    if (living_is_entity_mob(l) && living_server_peaceful(l)) l->is_dead = 1;

    /* EntitySlime.onUpdate's tail (the landing particles and squish) and the
     * player's EntityPlayer.onUpdate tail (the food stats; the hostiles probe's
     * player runs its own copy in hostiles.c) */
    if (IS_SLIME_KIND(l->kind)) slime_post_on_update(l, det);
    if (l->kind == SK_PLAYER) player_after_update(l, det);

    /* EntityBat.onUpdate after super: the hanging pin and the 0.6 motion
     * damping come after the whole of super.onUpdate, the travel included */
    if (l->kind == AK_BAT) bat_on_base_update(l);
}

/* EntityLivingBase.onLivingUpdate, the base half. */
void living_on_living_update(struct living *l, det_state *det)
{
    if (l->jump_ticks > 0) --l->jump_ticks;

    /* newPosRotationIncrements is 0: no client packets reach the probe */

    if (!living_is_client_world(l))
    {
        l->e.motion_x *= 0.98;
        l->e.motion_y *= 0.98;
        l->e.motion_z *= 0.98;
    }

    if (j_abs(l->e.motion_x) < 0.005) l->e.motion_x = 0.0;
    if (j_abs(l->e.motion_y) < 0.005) l->e.motion_y = 0.0;
    if (j_abs(l->e.motion_z) < 0.005) l->e.motion_z = 0.0;

    if (living_is_movement_blocked(l))
    {
        l->is_jumping = 0;
        l->move_strafing = 0.0F;
        l->move_forward = 0.0F;
        l->random_yaw_velocity = 0.0F;
    }
    else if (living_is_client_world(l))
    {
        if (l->kind == AK_BAT)
        {
            living_update_ai_tasks(l, det);
            bat_update_ai_tasks(l, det);
        }
        else if (l->kind == AK_SQUID)
        {
            /* the old-AI branch: updateEntityActionState, then the yaw-head
             * copy the else branch makes */
            squid_update_entity_action_state(l);
            l->rotation_yaw_head = l->rotation_yaw;
        }
        else if (living_is_ai_enabled(l))
        {
            living_update_ai_tasks(l, det);
        }
        else
        {
            if (IS_SLIME_KIND(l->kind))
            {
                slime_action_state(l, det);   /* EntitySlime.updateEntityActionState */
            }
            else if (l->kind == HK_SPIDER || l->kind == HK_CAVE_SPIDER)
            {
                spider_update_entity_action_state(l, det);   /* EntityCreature.updateEntityActionState */
            }
            else if (l->kind == SK_PLAYER || l->kind == HK_PLAYER)
            {
                /* EntityPlayer.updateEntityActionState: the base bump and the
                 * arm swing (nothing swings in this world) */
                ++l->entity_age;
            }
            else
            {
                living_update_entity_action_state(l);
            }

            l->rotation_yaw_head = l->rotation_yaw;
        }
    }

    if (l->is_jumping)
    {
        if (!l->is_in_water && !handle_lava_movement(l))
        {
            if (l->e.on_ground && l->jump_ticks == 0)
            {
                living_jump(l);
                l->jump_ticks = 10;
            }
        }
        else
        {
            l->e.motion_y += 0.03999999910593033;
        }
    }
    else
    {
        l->jump_ticks = 0;
    }

    l->move_strafing *= 0.98F;
    l->move_forward *= 0.98F;
    l->random_yaw_velocity *= 0.9F;

    /* EntityFlying overrides moveEntityWithHeading; EntityLivingBase's is the
     * base every other kind runs, and EntitySquid's the water one */
    if (l->kind == GK_GHAST) living_move_entity_with_heading_flying(l, l->move_strafing, l->move_forward);
    else if (l->kind == AK_SQUID) squid_move_entity_with_heading(l, l->move_strafing, l->move_forward, det);
    else living_move_entity_with_heading(l, l->move_strafing, l->move_forward, det);

    /* EntityBat.collideWithNearbyEntities is empty */
    if (l->kind != AK_BAT && !world_is_client(l->world)) living_collide_with_nearby_entities(l);

}

/* EntityLivingBase.onLivingUpdate plus EntityLiving's looting half and the kind's
 * override, called from living_on_update. */
void living_default_on_living_update(struct living *l, det_state *det)
{
    living_on_living_update(l, det);
}

/* EntityLivingBase.collideWithNearbyEntities. The ghast's queries reach every
 * entity in the world (the items and fireballs the an_world's own walk misses,
 * and the player), so its kind takes the ghasts.c extension; the animals keep
 * the an_world walk, which is Java's list for a probe with no other pool. */
static void living_collide_with_nearby_entities(struct living *l)
{
    struct aabb box = aabb_expand(l->e.bounding_box, 0.20000000298023224, 0.0, 0.20000000298023224);

    if (l->kind == GK_GHAST)
    {
        ghast_collide_with_nearby_entities(l, &box);
        return;
    }

    AN_QUERY_LIST(found);
    int n = an_entities_excluding(l->an, NULL, &box, found, AN_MAX_ENTITIES);
    int extra = l->an != NULL && l->an->collide_extra != NULL;

    for (int i = 0; i < n; ++i)
    {
        if (!found[i]->is_living) continue;
        /* the dragon at its place in the chunk order */
        if (extra && l->an->collide_extra_before != NULL && l->an->collide_extra_before(l->an->collide_extra_ctx, lv_get(found[i]->livh)))
        {
            l->an->collide_extra(l->an->collide_extra_ctx, l, &box);
            extra = 0;
        }
        if (lv_get(found[i]->livh) == l) continue;   /* World.getEntitiesWithinAABBExcludingEntity excludes it */
        if (!living_can_be_pushed(lv_get(found[i]->livh))) continue;

        /* EntityIronGolem.collideWithEntity: an IMob it touches becomes its
         * target one time in 20 */
        if (l->kind == VK_IRON_GOLEM && ai_is_imob_kind(lv_get(found[i]->livh)->kind) && det_rng_int_n(&l->rand, 20) == 0)
            l->attack_target = lv_ref(lv_get(found[i]->livh));

        /* collideWithEntity: other.applyEntityCollision(this) */
        living_apply_entity_collision(lv_get(found[i]->livh), l);
    }

    if (extra) l->an->collide_extra(l->an->collide_extra_ctx, l, &box);
}

/* EntityLiving.canDespawn. EntityAnimal answers false for the pig, cow,
 * mooshroom and sheep; the chicken's is its jockey flag and no rider
 * (EntityChicken.canDespawn: func_152116_bZ() && riddenByEntity == null).
 * EntityZombie (and the pigman under it) answers !isConverting(). */
static int living_can_despawn(struct living *l)
{
    if (IS_SLIME_KIND(l->kind)) return 1;   /* EntityLiving.canDespawn */
    if (l->kind == AK_CHICKEN) return l->is_chicken_jockey > 0 && lv_get(l->ridden_by_entity) == NULL;
    if (l->kind == AK_SQUID || l->kind == AK_BAT) return 1;
    if ((l->kind == HK_ZOMBIE || l->kind == HK_PIGMAN) && l->zombie_is_converting) return 0;
    if (l->kind >= HK_ZOMBIE && l->kind < HK_KINDS && l->kind != HK_PLAYER) return 1;
    if (l->kind == GK_GHAST) return 1;                            /* EntityLiving's default */
    return 0;
}

/* EntityLiving.despawnEntity. The probe's world holds one player, and the
 * probe's region sits tens of thousands of blocks from it, so the distance
 * squared (var8) is always far above 16384: the near-player reset never runs
 * and the setDead lands are unreachable for a farm animal, whose canDespawn is
 * false. What is left is the rand.nextInt(800) the entityAge > 600 test spends
 * before canDespawn is consulted, which is why every animal starts drawing one
 * extra step per tick once its age passes 600. */
/* The despawn's getClosestPlayer distance squared, -1.0 with no player. */
double living_player_distance_squared(struct living *l)
{
    struct an_world *an = l->an;

    if (an == NULL || !an->has_player) return -1.0;

    double dx = an->player_x - l->e.pos_x;
    double dy = an->player_y - l->e.pos_y;
    double dz = an->player_z - l->e.pos_z;

    return dx * dx + dy * dy + dz * dz;
}

int living_closest_player_within(struct living *l, double maxd)
{
    double d = living_player_distance_squared(l);

    return d >= 0.0 && (maxd < 0.0 || d < maxd * maxd);
}

void living_despawn_entity(struct living *l)
{
    if (l->persistence_required)
    {
        l->entity_age = 0;
        return;
    }


    /* World.getClosestPlayerToEntity found nobody: a world whose
     * playerEntities is empty leaves the age and the entity alone */
    if (l->an != NULL && l->an->no_players) return;

    double var8 = 3.3642e9;

    if (l->an != NULL && l->an->playerh != 0)
    {
        double dx = lv_get(l->an->playerh)->e.pos_x - l->e.pos_x;
        double dy = lv_get(l->an->playerh)->e.pos_y - l->e.pos_y;
        double dz = lv_get(l->an->playerh)->e.pos_z - l->e.pos_z;
        var8 = dx * dx + dy * dy + dz * dz;
    }
    else if (l->an != NULL && l->an->has_player)
    {
        double dx = l->an->player_x - l->e.pos_x;
        double dy = l->an->player_y - l->e.pos_y;
        double dz = l->an->player_z - l->e.pos_z;
        var8 = dx * dx + dy * dy + dz * dz;
    }

    /* with no player entity the distance stays at its sentinel, the way the
     * world's far-away parked player leaves it */
    int can = living_can_despawn(l);

    if (can && var8 > 16384.0) l->is_dead = 1;

    if (l->entity_age > 600 && det_rng_int_n(&l->rand, 800) == 0 && var8 > 1024.0 && can)
    {
        l->is_dead = 1;
    }
    else if (var8 < 1024.0)
    {
        l->entity_age = 0;
    }
}

/* EntityLiving.despawnEntity, exposed for the slime's own action state. */
void living_despawn(struct living *l)
{
    living_despawn_entity(l);
}

/* EntityLiving.faceEntity. */
void living_face_entity(struct living *l, struct living *other, float yaw_speed, float pitch_speed)
{
    double v4 = other->e.pos_x - l->e.pos_x;
    double v8 = other->e.pos_z - l->e.pos_z;
    double v6 = other->e.pos_y + (double)living_eye_height(other) - (l->e.pos_y + (double)living_eye_height(l));
    double v14 = (double)sqrt_double(v4 * v4 + v8 * v8);
    float v12 = (float)(fd_atan2(v8, v4) * 180.0 / 3.141592653589793) - 90.0F;
    float v13 = (float)(-(fd_atan2(v6, v14) * 180.0 / 3.141592653589793));
    l->rotation_pitch = living_update_rotation(l->rotation_pitch, v13, pitch_speed);
    l->rotation_yaw = living_update_rotation(l->rotation_yaw, v12, yaw_speed);
}

float living_update_rotation(float cur, float want, float max_inc)
{
    float var4 = wrap_angle_float(want - cur);

    if (var4 > max_inc) var4 = max_inc;
    if (var4 < -max_inc) var4 = -max_inc;

    return cur + var4;
}

/* EntityLiving.getMaxSafePointTries: no attack target, three tries.
 * EntityCreeper overrides it with 3 + (int)(health - 1). */
int living_max_safe_point_tries(struct living *l)
{
    if (l->kind == HK_CREEPER) return creeper_max_safe_point_tries(l);

    if (lv_get(l->attack_target) == NULL) return 3;

    int v1 = (int)(l->health - living_max_health_impl(l) * 0.33F);
    /* World.difficultySetting.getDifficultyId(); the probes' world is NORMAL */
    v1 -= (3 - (l->an != NULL ? l->an->difficulty : 2)) * 4;

    if (v1 < 0) v1 = 0;

    return v1 + 3;
}

/* EntityLiving.onSpawnWithEgg. EntityCaveSpider's override returns the data
 * untouched without calling super, so for a cave spider the whole body below
 * (the follow-range modifier and the kind's own setup) never runs. Returns the
 * entity a spawn-time construct added to the world (the spider jockey's
 * rider), which the caller list-adds after the spawned mob itself. */
/* EntityLiving.onSpawnWithEgg's own body: the "Random spawn bonus"
 * follow-range modifier (one nextGaussian, the modifier's Det UUID). */
static void spawn_bonus(struct living *l, det_state *det)
{
    struct attr_mod mod;
    memset(&mod, 0, sizeof mod);
    mod.name = MODN_RANDOM_SPAWN_BONUS;
    double amount = det_rng_gaussian(&l->rand) * 0.05;
    int64_t msb, lsb;
    det_uuid_role(det, DET_OTHER, &msb, &lsb);
    mod.amount = amount;
    mod.operation = 1;
    mod.uuid_msb = msb;
    mod.uuid_lsb = lsb;
    mod.saved = 1;

    attrs_apply(&l->attrs.a[ATTR_FOLLOW_RANGE], &mod);
}

/* EntitySkeleton.onSpawnWithEgg, what living_on_spawn_with_egg runs for a
 * skeleton: the spider jockey's rider calls it directly, so the spider's
 * onSpawnWithEgg does not call back into the dispatch. */
void living_skeleton_on_spawn_with_egg(struct living *l, det_state *det)
{
    spawn_bonus(l, det);
    skeleton_on_spawn_with_egg(l, det);
}

struct living *living_on_spawn_with_egg_ret(struct living *l, det_state *det)
{
    if (l->kind == HK_CAVE_SPIDER) return NULL;

    spawn_bonus(l, det);

    if (l->kind == VK_VILLAGER)
    {
        villager_on_spawn_with_egg(l, det);
    }
    else if (l->kind == HK_ZOMBIE || l->kind == HK_PIGMAN)
    {
        /* the jockey chicken, spawned ahead of the zombie */
        struct living *partner = zombie_on_spawn_with_egg(l, det);
        if (l->kind == HK_PIGMAN) l->zombie_is_villager = 0;
        return partner;
    }
    else if (l->kind == HK_SPIDER)
    {
        /* the rider stays pending: its chunk insert happened, its tick-list
         * insert waits for the caller (the probe's list order) */
        return spider_jockey_check(l, det);
    }
    else if (l->kind == HK_SKELETON)
    {
        skeleton_on_spawn_with_egg(l, det);
    }
    else if (l->kind == HK_CREEPER || l->kind == HK_ENDERMAN || l->kind == HK_WITCH || l->kind == HK_SILVERFISH || l->kind == HK_BLAZE)
    {
        /* Neither EntityCreeper, EntityEnderman, EntityWitch, EntitySilverfish nor EntityBlaze overrides onSpawnWithEgg:
         * EntityLiving's body above (the "Random spawn bonus" follow-range
         * modifier) is all of it, so nothing here draws (no canPickUpLoot, no
         * addRandomArmor) */
    }
    else if (l->kind != VK_IRON_GOLEM)
    {
        animal_on_spawn_with_egg(l, det);
    }

    return NULL;
}

/* The void form every existing spawn path calls. */
void living_on_spawn_with_egg(struct living *l, det_state *det)
{
    /* a partner the egg path spawned joins the list ahead of the mob */
    if (living_on_spawn_with_egg_ret(l, det) != NULL) egg_take_pending_partner();
}

/* EntityLivingBase.getExperiencePoints / EntityLiving's override. */

/* EntityLivingBase.dropRareDrop: the empty body for every animal. */

/* -------------------------------------------------- the tick entry points */

void living_move(struct living *l, double dx, double dy, double dz)
{
    entity_move_memo(&l->e, dx, dy, dz, 0, living_fall_state, &l->cmemo);
}

/* EntityLivingBase.setJumping / getJumpHelper()->setJumping */
void living_set_jumping(struct living *l, int jumping)
{
    l->is_jumping = (uint8_t)(jumping ? 1 : 0);
}

/* World.updateEntityWithOptionalForce(e, true) for a living entity. */
void living_update_entity(struct living *l, det_state *det)
{
    l->last_tick_x = l->e.pos_x;
    l->last_tick_y = l->e.pos_y;
    l->last_tick_z = l->e.pos_z;
    l->prev_rotation_yaw = l->rotation_yaw;
    l->prev_rotation_pitch = l->rotation_pitch;

    if (l->added_to_chunk)
    {
        ++l->ticks_existed;
        if (l->an && l->an->update_ridden && lv_get(l->riding_entity) != NULL && lv_get(l->riding_entity)->is_dead)
        {
            /* Entity.updateRidden on a dead vehicle: the link goes and nothing
             * else runs this tick; EntityLivingBase's tail zeroes the fall */
            l->riding_entity = 0;
            l->e.fall_distance = 0.0F;
            return;
        }
        if (l->an && l->an->update_ridden && lv_get(l->riding_entity) != NULL && !lv_get(l->riding_entity)->is_dead)
            l->e.motion_x = l->e.motion_y = l->e.motion_z = 0.0;
        living_on_update(l, det);
        if (l->an && l->an->update_ridden && lv_get(l->riding_entity) != NULL && !lv_get(l->riding_entity)->is_dead)
        {
            struct living *v = lv_get(l->riding_entity);
            if (v->kind == AK_CHICKEN)
            {
                /* EntityChicken.updateRiderPosition: 0.1 ahead along the
                 * body's yaw, half the chicken's height up, and the rider
                 * takes the chicken's body yaw */
                float s1 = mh_sin(v->render_yaw_offset * 3.1415927F / 180.0F);
                float c1 = mh_cos(v->render_yaw_offset * 3.1415927F / 180.0F);
                entity_set_position(&l->e, v->e.pos_x + (double)(0.1F * s1),
                                    v->e.pos_y + (double)(v->e.height * 0.5F) + (double)l->e.y_offset
                                        - (l->kind == HK_SKELETON ? 0.5 : 0.0) + (double)0.0F,
                                    v->e.pos_z - (double)(0.1F * c1));
                l->render_yaw_offset = v->render_yaw_offset;
            }
            else
                entity_set_position(&l->e, v->e.pos_x,
                                    v->e.pos_y + (double)v->e.height * 0.75 + (double)l->e.y_offset
                                        - (l->kind == HK_SKELETON ? 0.5 : 0.0),
                                    v->e.pos_z);
            /* EntityLivingBase.updateRidden's tail (field_70768_au, which
             * nothing reads, is not kept) */
            l->field_110154_aX = 0.0F;
            l->e.fall_distance = 0.0F;
        }
    }

    if (l->e.pos_x != l->e.pos_x) l->e.pos_x = l->last_tick_x;
    if (l->e.pos_y != l->e.pos_y) l->e.pos_y = l->last_tick_y;
    if (l->e.pos_z != l->e.pos_z) l->e.pos_z = l->last_tick_z;
    if (l->rotation_pitch != l->rotation_pitch) l->rotation_pitch = l->prev_rotation_pitch;
    if (l->rotation_yaw != l->rotation_yaw) l->rotation_yaw = l->prev_rotation_yaw;
}

/* --------------------------------------------------------------- the tick */

void an_add(struct an_world *an, struct an_ent *en)
{
    struct living *l = lv_get(en->livh);

    if (en->is_living)
    {
        l->an = an;
        int cx = mh_floor(l->e.pos_x / 16.0);
        int cz = mh_floor(l->e.pos_z / 16.0);

        an_chunk_add(an, en, cx, mh_floor(l->e.pos_y / 16.0), cz);
    }
}

static void an_chunk_remove_coords(struct an_world *an, struct an_ent *en, int cx, int cy, int cz);

void an_remove(struct an_world *an, struct an_ent *en)
{
    if (en->is_living)
    {
        struct living *l = lv_get(en->livh);

        if (l->added_to_chunk && world_chunk_loaded(an->w, l->chunk_coord_x, l->chunk_coord_z))
        {
            an_chunk_remove(an, en, l->chunk_coord_y);
        }

        l->added_to_chunk = 0;
    }
    else if (en->ieh != 0)
    {
        if (ie_get(en->ieh)->added_to_chunk && world_chunk_loaded(an->w, ie_get(en->ieh)->chunk_x, ie_get(en->ieh)->chunk_z))
        {
            an_chunk_remove(an, en, ie_get(en->ieh)->chunk_y);
        }

        ie_get(en->ieh)->added_to_chunk = 0;
    }
}

/* an_remove for an item-world entry whose entity its own world already
 * released: the membership it had then (ie_removal) */
void an_remove_released(struct an_world *an, struct an_ent *en, const ie_removal *r)
{
    if (r->added_to_chunk && world_chunk_loaded(an->w, r->chunk_x, r->chunk_z))
        an_chunk_remove_coords(an, en, r->chunk_x, r->chunk_y, r->chunk_z);
    en->ieh = 0;
}

/* The chunk membership World.updateEntityWithOptionalForce keeps. */
void an_chunk_membership(struct an_world *an, struct an_ent *en)
{
    struct living *l = lv_get(en->livh);
    int v6 = mh_floor(l->e.pos_x / 16.0);
    int v7 = mh_floor(l->e.pos_y / 16.0);
    int v8 = mh_floor(l->e.pos_z / 16.0);

    if (!l->added_to_chunk || l->chunk_coord_x != v6 || l->chunk_coord_y != v7 || l->chunk_coord_z != v8)
    {
        if (l->added_to_chunk && world_chunk_loaded(an->w, l->chunk_coord_x, l->chunk_coord_z))
        {
            an_chunk_remove(an, en, l->chunk_coord_y);
        }

        if (world_chunk_loaded(an->w, v6, v8)) an_chunk_add(an, en, v6, v7, v8);
        else l->added_to_chunk = 0;
    }
}


int an_tick_one(struct an_world *an, struct an_ent *en, int tick)
{
    double ex = en->is_living ? lv_get(en->livh)->e.pos_x : ie_get(en->ieh)->e.pos_x;
    double ez = en->is_living ? lv_get(en->livh)->e.pos_z : ie_get(en->ieh)->e.pos_z;
    int bx = mh_floor(ex), bz = mh_floor(ez);

    /* World.updateEntityWithOptionalForce leaves an entity alone unless its
     * entire 32-block neighborhood is loaded. */
    int already_dead = en->is_living ? lv_get(en->livh)->is_dead : ie_get(en->ieh)->is_dead;
    if (already_dead && !an->process_dead) return 0;
    if (!already_dead &&
        !world_entity_area_loaded(an->w, bx, bz)) return 0;

    int dead;
    int saved_id = 0, saved_added = 0, saved_cx = 0, saved_cy = 0, saved_cz = 0;
    if (en->is_living)
    {
        struct living *l = lv_get(en->livh);
        if (!l->is_dead)
        {
            living_update_entity(l, an->det);
            if (an->post_update != NULL) an->post_update(an->portal_travel_ctx);
            an_chunk_membership(an, en);
        }
        dead = l->is_dead;
    }
    else
    {
        ie_removal r;
        int n_rem = 0;
        saved_id = ie_get(en->ieh)->entity_id;
        saved_added = ie_get(en->ieh)->added_to_chunk;
        saved_cx = ie_get(en->ieh)->chunk_x;
        saved_cy = ie_get(en->ieh)->chunk_y;
        saved_cz = ie_get(en->ieh)->chunk_z;
        dead = ie_tick_one(&an->iew, ie_get(en->ieh), tick, &r, 1, &n_rem);
        /* the item's own move to another chunk (updateEntityWithOptionalForce's
         * membership, which ie_tick_one keeps on the ie side): the chunk's
         * entity lists here follow it, so the chunk save and the AABB walks
         * find it where it is */
        if (!dead && saved_added &&
            (!ie_get(en->ieh)->added_to_chunk || ie_get(en->ieh)->chunk_x != saved_cx || ie_get(en->ieh)->chunk_y != saved_cy ||
             ie_get(en->ieh)->chunk_z != saved_cz))
        {
            an_chunk_remove_coords(an, en, saved_cx, saved_cy, saved_cz);
            if (ie_get(en->ieh)->added_to_chunk)
            {
                struct an_chunk *c = an_chunk_get(an, ie_get(en->ieh)->chunk_x, ie_get(en->ieh)->chunk_z);
                int cy = ie_get(en->ieh)->chunk_y < 0 ? 0 : ie_get(en->ieh)->chunk_y > 15 ? 15 : ie_get(en->ieh)->chunk_y;
                sec_push(&c->sec[cy], an_ent_index(en), AN_MAX_ENTITIES);
            }
        }
    }

    if (!dead) return 0;
    if (an->on_remove_cb)
        an->on_remove_cb(en->spawn_index, en->is_living ? lv_get(en->livh)->entity_id : saved_id,
                         an->on_remove_ctx);
    if (en->is_living) an_remove(an, en);
    else if (saved_added)
        an_chunk_remove_coords(an, en, saved_cx, saved_cy, saved_cz);
    /* a ghost leaves the list but stays in its chunk's (serverreplay.c
     * sr_living_portal_travel), its entry and object kept till the unload */
    if (!en->ghost) en->used = 0;
    for (int i = 0; i < an->n; ++i)
        if (an_ent_at(an->slot[i]) == en)
        {
            memmove(&an->slot[i], &an->slot[i + 1], (size_t)(an->n - i - 1) * sizeof an->slot[0]);
            --an->n;
            an->slot[an->n] = -1;
            break;
        }
    if (!en->is_living) en->ieh = 0;
    else if (!en->ghost) grave_bury(lv_get(en->livh), en);
    return 1;
}

/* World.updateEntity for a rider its vehicle's update reaches: the same
 * update, but an entity that dies there stays in the list until the loop
 * itself removes it (the rider's own turn skips it while it still rides). */
void an_update_one(struct an_world *an, struct an_ent *en, int tick)
{
    (void)tick;
    if (!en->is_living || lv_get(en->livh)->is_dead) return;

    struct living *l = lv_get(en->livh);
    int bx = mh_floor(l->e.pos_x), bz = mh_floor(l->e.pos_z);

    if (!world_entity_area_loaded(an->w, bx, bz)) return;

    living_update_entity(l, an->det);
    an_chunk_membership(an, en);
}

void an_tick(struct an_world *an, int tick)
{
    trace("tick", "i", tick);
    struct blockcb_env previous = nw_env->blockcb.env;
    nw_env->blockcb.env.world_rand = &an->iew.world_rand.r;
    nw_env->blockcb.env.det = an->det;

    for (int i = 0; i < an->n; ++i)
        if (an_tick_one(an, an_ent_at(an->slot[i]), tick)) --i;

    nw_env->blockcb.env = previous;
}

/* ------------------------------------------------------------- the state */


/* --------------------------------------------------- the world-side hooks */

int world_is_client(struct world *w)
{
    (void)w;
    return 0;   /* the probe world is the integrated server world */
}

int living_server_peaceful(struct living *l)
{
    return !world_is_client(l->world) && l->an != NULL && l->an->difficulty == 0;
}

int living_mob_loot(struct world *w)
{
    /* World.getGameRules().getGameRuleBooleanValue("doMobLoot"): the default
     * rule is true and nothing in the probe turns it off. */
    (void)w;
    return 1;
}

int living_world_full_block_light_value(struct world *w, int subtracted, int x, int y, int z)
{
    return full_block_light_value(w, subtracted, x, y, z);
}

float living_light_brightness(struct world *w, int subtracted, int x, int y, int z)
{
    return light_brightness(w, subtracted, x, y, z);
}

int living_is_ai_enabled(struct living *l)
{
    /* EntityPig, EntityCow, EntityMooshroom, EntityChicken, EntitySheep and
     * EntityBat all return true; EntitySquid (EntityWaterMob), the slimes, the
     * ghast and EntityEnderman inherit EntityLiving's false, and the player is
     * not an EntityLiving at all. EntitySpider and EntityCaveSpider never
     * override it either, so they run the old AI. */
    if (IS_SLIME_KIND(l->kind) || l->kind == SK_PLAYER || l->kind == HK_PLAYER ||
        l->kind == GK_GHAST || l->kind == AK_SQUID) return 0;
if (l->kind == HK_ENDERMAN || l->kind == HK_SILVERFISH || l->kind == HK_PIGMAN || l->kind == HK_BLAZE) return 0;
    if (l->kind == HK_SPIDER || l->kind == HK_CAVE_SPIDER) return 0;
    return 1;
}

float living_get_ai_move_speed(struct living *l)
{
    /* EntityLivingBase.getAIMoveSpeed: the land factor only when AI is enabled,
     * else the 0.1 the probe's player and the slimes use */
    return living_is_ai_enabled(l) ? l->land_movement_factor : 0.1F;
}

void living_set_ai_move_speed(struct living *l, float v)
{
    l->land_movement_factor = v;
    l->move_forward = v;
}

int living_is_client_world(struct living *l)
{
    /* EntityLivingBase.isClientWorld is misnamed: it returns !worldObj.isClient,
     * which is true on the server */
    (void)l;
    return 1;
}

int living_is_movement_blocked(struct living *l)
{
    return l->health <= 0.0F;
}

int living_handle_lava_movement(struct living *l)
{
    return handle_lava_movement(l);
}

int living_is_in_water(struct living *l)
{
    return l->is_in_water;
}

int living_is_wet(struct living *l)
{
    return is_wet(l);
}

int living_world_is_any_liquid(struct world *w, struct aabb box)
{
    return is_any_liquid(w, box);
}

int living_is_inside_of_material(struct living *l, int material)
{
    return is_inside_of_material(l, material);
}

int living_is_on_ladder(struct living *l)
{
    return is_on_ladder(l);
}

void living_heal(struct living *l, float v)
{
    float cur = l->health;

    if (cur > 0.0F)
    {
        living_set_health(l, cur + v);
        if (l->external_heal) l->external_heal(l->external_attack_ctx, l->health);
    }
}

int living_can_be_pushed(struct living *l)
{
    return !l->is_dead && l->kind != AK_BAT;
}

/* The protected bodies the ghast's own attack path runs. */
void living_damage_entity_pub(struct living *l, int source, float amount)
{
    living_damage_entity(l, source, amount, l->an ? l->an->det : NULL);
}

void living_set_been_attacked_pub(struct living *l)
{
    living_set_been_attacked(l);
}

void living_despawn_entity_pub(struct living *l)
{
    living_despawn_entity(l);
}

int living_dmg_unblockable(int source)
{
    return dmg_unblockable(source);
}

void living_apply_entity_collision_pub(struct living *l, struct living *other)
{
    living_apply_entity_collision(l, other);
}

void living_set_revenge_target(struct living *l, struct living *target)
{
    l->entity_living_to_attack = lv_ref(target);
    l->revenge_timer = l->ticks_existed;
    /* EntityVillager.setRevengeTarget after super */
    if (l->kind == VK_VILLAGER) villager_revenge_village(l, target);
}

/* EntityZombie.startConversion. setEntityState(this, 16) is the client's
 * remedy sound. */
void zombie_start_conversion(struct living *l, int ticks, det_state *det)
{
    l->zombie_conversion_time = ticks;
    l->zombie_is_converting = 1;
    living_remove_potion_effect(l, POT_WEAKNESS, det);
    int diff = l->an != NULL ? l->an->difficulty : 2;
    struct potion_effect eff = {
        .id = POT_DAMAGE_BOOST,
        .amplifier = diff - 1 < 0 ? diff - 1 : 0,
        .duration = ticks,
    };
    living_add_potion_effect(l, &eff, det);
}

/* EntityZombie.onUpdate's head: a converting zombie's countdown
 * (getConversionTimeBoost: a 1% roll, then iron bars and beds around the
 * truncated position, 14 at most, each a 30% boost), then convertToVillager. */
static void zombie_conversion_tick(struct living *l)
{
    int boost = 1;
    if (det_rng_float(&l->rand) < 0.01F)
    {
        int found = 0;
        int px = (int)l->e.pos_x, py = (int)l->e.pos_y, pz = (int)l->e.pos_z;
        for (int x = px - 4; x < px + 4 && found < 14; ++x)
            for (int y = py - 4; y < py + 4 && found < 14; ++y)
                for (int z = pz - 4; z < pz + 4 && found < 14; ++z)
                {
                    int id = world_get_block(l->world, x, y, z) & 4095;
                    if (id == 101 || id == 26)
                    {
                        if (det_rng_float(&l->rand) < 0.3F) ++boost;
                        ++found;
                    }
                }
    }
    l->zombie_conversion_time -= boost;
    if (l->zombie_conversion_time <= 0 && l->an != NULL && l->an->on_convert != NULL)
    {
        l->an->on_convert(l->an, l, l->an->constructor_ctx);
        /* convertToVillager's 1017 */
        env_aux_sfx(l->world, 1017, (int)l->e.pos_x, (int)l->e.pos_y, (int)l->e.pos_z, 0);
    }
}

/* Entity.mountEntity(null): the rider steps off at its vehicle's top. */
void living_dismount(struct living *l)
{
    if (lv_get(l->riding_entity) == NULL) return;

    living_set_location_and_angles(l, lv_get(l->riding_entity)->e.pos_x,
                                   lv_get(l->riding_entity)->e.bounding_box.min_y + (double)lv_get(l->riding_entity)->e.height,
                                   lv_get(l->riding_entity)->e.pos_z, l->rotation_yaw, l->rotation_pitch);
    lv_get(l->riding_entity)->ridden_by_entity = 0;
    l->riding_entity = 0;
}

/* Entity.mountEntity(vehicle): the pointers only, no position change; a cycle
 * in the mount chain is refused. */
void living_mount(struct living *l, struct living *vehicle)
{
    if (vehicle == NULL)
    {
        living_dismount(l);
        return;
    }

    if (lv_get(l->riding_entity) != NULL) lv_get(l->riding_entity)->ridden_by_entity = 0;

    for (struct living *p = lv_get(vehicle->riding_entity); p != NULL; p = lv_get(p->riding_entity))
    {
        if (p == l) return;
    }

    l->riding_entity = lv_ref(vehicle);
    vehicle->ridden_by_entity = lv_ref(l);
}

/* The cacti and the fire the move sweep reaches: EntityLivingBase.attackEntityFrom. */
static void living_attack_cb(void *self, int source, float amount)
{
    struct living *l = (struct living *)self;
    if (source == ENTITY_SRC_IN_FIRE && l->immune_to_fire) return;
    living_attack_entity_from(l, source == ENTITY_SRC_CACTUS ? DMG_CACTUS : DMG_IN_FIRE, amount, l->an ? l->an->det : NULL);
}

/* the in-water step's swim sound pitch: two floats from the entity's Random */
/* Entity.isInWater's virtual call from Entity.moveEntity's in-water step: the
 * squid's own box test (which pushes the flow), every other kind the field. */
static int living_kind_in_water_cb(void *self)
{
    struct living *l = (struct living *)self;

    if (l->kind == AK_SQUID) return squid_is_in_water(l);

    return l->e.in_water;
}

static void living_swim_sound_cb(void *self)
{
    struct living *l = (struct living *)self;

    (void)det_rng_float(&l->rand);
    (void)det_rng_float(&l->rand);
}

/* Block.onEntityWalking: the redstone ore's light-up draws from the world
 * Random (BlockRedstoneOre draws World.rand, not the entity's), which the
 * whole-server replay points at its world tick's rand. */
void living_set_walking_world(jrand *wr)
{
    living_walking_world = wr;
}

static void living_walking_block_cb(void *self, int x, int y, int z, int id)
{
    struct living *l = (struct living *)self;

    /* the lit ore is the same class: its particle draws run too, only the
     * unlit one is rewritten */
    if ((id != 73 && id != 74) || living_walking_world == NULL) return;

    for (int i = 0; i < 6; ++i)
    {
        jr_float(living_walking_world);
        jr_float(living_walking_world);
        jr_float(living_walking_world);
    }

    if (id == 73) world_set_block(l->world, x, y, z, 74, 0, 3);
}

/* Entity.setFire's getFireTimeForEntity, for Entity.moveEntity's
 * standing-in-fire setFire(8). */
static int living_fire_time_cb(void *self, int ticks)
{
    return ench_fire_time((struct living *)self, ticks);
}

static void living_fizz_cb(void *self)
{
    struct living *l = (struct living *)self;
    (void)det_rng_float(&l->rand);
    (void)det_rng_float(&l->rand);
}

/* ---------------------------------------- EntityLiving's AI tick machinery */

static struct ai_task *find_task(struct living *l, int cls)
{
    for (int i = 0; i < lv_ai(l)->tasks.n; ++i)
    {
        if (lv_ai(l)->tasks.entries[i].t.cls == cls) return &lv_ai(l)->tasks.entries[i].t;
    }

    return NULL;
}

void living_update_ai_tasks(struct living *l, det_state *det)
{
    if (l->kind == AK_SHEEP)
    {
        struct ai_task *e = find_task(l, AIC_EAT_GRASS);
        l->sheep_timer = e ? e->eat_grass_timer : 0;
    }

    /* EntityLiving.updateAITasks' body */
    ++l->entity_age;
    living_despawn_entity(l);
    senses_clear(l);

    if (lv_ai(l)->target_tasks.n > 0)
    {
        ai_tasks_update(l, &lv_ai(l)->target_tasks);
    }

    ai_tasks_update(l, &lv_ai(l)->tasks);
    nav_update(l);

    if (l->kind == VK_VILLAGER)
    {
        villager_update_ai_tick(l, det);
    }
    else if (l->kind == VK_IRON_GOLEM)
    {
        iron_golem_update_ai_tick(l, det);
    }
    else
    {
        if (l->growing_age != 0) l->in_love = 0;
    }

    move_helper_update(l);
    look_helper_update(l);
    jump_helper_do(l);
    (void)det;
}

void living_update_entity_action_state(struct living *l)
{
    /* EntityGhast.updateEntityActionState: no super call, so entityAge stays
     * where the damage paths left it */
    if (l->kind == GK_GHAST)
    {
        ghast_update_entity_action_state(l);
        return;
    }

    /* EntityLivingBase.updateEntityActionState. EntityLiving's override adds
     * the movement reset, despawnEntity and the random target pick, and
     * EntityCreature overrides the whole body again: the enderman (AI disabled)
     * runs that chain. The probe's player is an EntityLivingBase's body. */
    if (l->kind == HK_PIGMAN)
    {
        pigman_update_entity_action_state(l, l->an ? l->an->det : NULL);
        return;
    }
    if (l->kind == HK_ENDERMAN)
    {
        enderman_update_entity_action_state(l, l->an ? l->an->det : NULL);
        return;
    }
    if (l->kind == HK_SILVERFISH)
    {
        silverfish_update_entity_action_state(l, l->an ? l->an->det : NULL);
        return;
    }
    if (l->kind == HK_BLAZE)
    {
        blaze_update_entity_action_state(l, l->an ? l->an->det : NULL);
        return;
    }
    ++l->entity_age;
    l->move_strafing = 0.0F;
    l->move_forward = 0.0F;
    living_despawn_entity(l);

    float var1 = 8.0F;

    if (det_rng_float(&l->rand) < 0.02F)
    {
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
        living_face_entity(l, lv_get(l->current_target), 10.0F, 40.0F);

        double cdx = lv_get(l->current_target)->e.pos_x - l->e.pos_x;
        double cdy = lv_get(l->current_target)->e.pos_y - l->e.pos_y;
        double cdz = lv_get(l->current_target)->e.pos_z - l->e.pos_z;
        double cd = cdx * cdx + cdy * cdy + cdz * cdz;

        if (l->num_ticks_to_chase_target-- <= 0 || lv_get(l->current_target)->is_dead ||
            cd > (double)(var1 * var1))
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

    int var4 = l->is_in_water;
    int var3 = handle_lava_movement(l);

    if (var4 || var3)
    {
        living_set_jumping(l, det_rng_float(&l->rand) < 0.8F);
    }
}

void living_update_rotation_head(struct living *l)
{
    l->rotation_yaw_head = l->rotation_yaw;
}

/* EntityLivingBase.onUpdate for a living entity is living_on_update above. */

/* EntityLivingBase.collideWithNearbyEntities uses living_apply_entity_collision;
 * Entity.collideWithEntity -> other.applyEntityCollision(this). */

/* --------------------------------------------------------- EntityLiving's */

/* EntityLiving.dropFewItems per kind lives in animals.c. */
void living_drop_few_items(struct living *l, int hit_by_player, int looting, det_state *det)
{
    if (IS_SLIME_KIND(l->kind))
    {
        slime_drop_few_items(l, hit_by_player, looting, det);
        return;
    }
    if (l->kind == HK_PIGMAN)
    {
        pigman_drop_few_items(l, looting);
        return;
    }
    if (l->kind == HK_ZOMBIE)
    {
        zombie_drop_few_items(l, hit_by_player, looting, det);
        return;
    }
    if (l->kind == HK_SPIDER || l->kind == HK_CAVE_SPIDER)
    {
        spider_drop_few_items(l, hit_by_player, looting, det);
        return;
    }
    if (l->kind == HK_SKELETON)
    {
        skeleton_drop_few_items(l, hit_by_player, looting, det);
        return;
    }
    if (l->kind == GK_GHAST)
    {
        ghast_drop_few_items(l, looting);
        return;
    }
    if (l->kind == HK_CREEPER)
    {
        creeper_drop_few_items(l, hit_by_player, looting, det);
        return;
    }
    if (l->kind == HK_ENDERMAN)
    {
        enderman_drop_few_items(l, hit_by_player, looting, det);
        return;
    }
    if (l->kind == HK_WITCH)
    {
        witch_drop_few_items(l, hit_by_player, looting, det);
        return;
    }
    if (l->kind == HK_BLAZE)
    {
        blaze_drop_few_items(l, hit_by_player, looting, det);
        return;
    }
    animal_drop_few_items(l, hit_by_player, looting, det);
}

void living_drop_equipment(struct living *l, int hit_by_player, int looting, det_state *det)
{
    (void)det;
    for (int s = 0; s < 5; ++s)
    {
        struct equip_slot *eq = &l->equip[s];
        if (eq->id <= 0) continue;

        int var5 = l->equipment_drop_chances[s] > 1.0f;
        if ((hit_by_player || var5) && det_rng_float(&l->rand) - (float)looting * 0.01f < l->equipment_drop_chances[s])
        {
            int damage = eq->damage;
            if (!var5 && ITEMS[eq->id].max_damage > 0)
            {
                int max_dmg = ITEMS[eq->id].max_damage;
                int var6 = max_dmg - 25;
                if (var6 < 1) var6 = 1;
                int r1 = det_rng_int_n(&l->rand, var6);
                int r2 = det_rng_int_n(&l->rand, r1 + 1);
                int var7 = max_dmg - r2;
                if (var7 > var6) var7 = var6;
                if (var7 < 1) var7 = 1;
                /* var4.setItemDamage: the mob's own stack, which the drop
                 * then shares */
                eq->damage = var7;
                damage = var7;
            }
            if (l->an)
            {
                ie_ent *ie = an_spawn_item(l->an, l->an->n, l->e.pos_x, l->e.pos_y, l->e.pos_z,
                                           eq->id, damage, eq->count ? eq->count : 1, 10);
                if (ie)
                {
                    ie->stack_count_link = eq->count_link.owner ? eq->count_link
                                                                : (struct stack_link){lv_ref(l), s};
                    ie->stack_tag = eq->tag;
                }
            }
        }
    }
}

/* -------------------------------------------------------------- the NBT */

struct nbt *nbt_double_list3(double a, double b, double c)
{
    struct nbt *list = nbt_new_list();
    nbt_list_add(list, nbt_new_double(a));
    nbt_list_add(list, nbt_new_double(b));
    nbt_list_add(list, nbt_new_double(c));
    return list;
}

struct nbt *nbt_float_list2(float a, float b)
{
    struct nbt *list = nbt_new_list();
    nbt_list_add(list, nbt_new_float(a));
    nbt_list_add(list, nbt_new_float(b));
    return list;
}

/* The bucket java.util.HashMap puts a modifier in: AttributeInstance holds them
 * in a map keyed by the UUID, so writeAttributeInstanceToNBT's iteration order
 * is the bucket order, and the ring-buffer order inside a bucket (Java 8's
 * putVal appends). UUID.hashCode is the XOR of the two halves, spread by the
 * map's h ^ (h >>> 16). */
static inline int attr_mod_bucket(const struct attr_mod *m)
{
    int64_t hilo = m->uuid_msb ^ m->uuid_lsb;
    int h = (int)(hilo >> 32) ^ (int)hilo;
    h = h ^ ((unsigned int)h >> 16);
    return h & 15;
}

/* SharedMonsterAttributes.writeAttributeInstanceToNBT: Name, Base and, when the
 * instance holds modifiers, the saved ones in the map's order. */
/* each attribute name's interned form, as nbtw_site_ holds a literal's */
static struct nbt_put_site attr_name_site[ATTR_COUNT];

static void attr_instance_w(struct nbtw *w, const struct attr_instance *a)
{
    nbtw_add_comp(w);
    nbtw_string_key(w, "Name", nbt_site_key_fast(&attr_name_site[a->which], ATTR_NAME[a->which]));
    nbtw_double(w, "Base", a->base);

    /* SharedMonsterAttributes.writeAttributeInstanceToNBT writes the list when
     * func_111122_c() (every applied modifier) is not empty, and skips the
     * unsaved ones inside: an instance whose modifiers are all unsaved still
     * carries an empty "Modifiers" list, which the enderman's attacking speed
     * boost produces. The held item's modifiers are out of the set at that
     * point (see attr_mod.from_item), so they neither appear nor keep the tag
     * alive. */
    int any = 0;

    for (int i = 0; i < a->nmods; ++i)
    {
        if (!a->mods[i].from_item)
        {
            any = 1;
            break;
        }
    }

    if (any)
    {
        nbtw_list(w, "Modifiers");

        /* The saved modifiers in the map's order: a stable sort by bucket, so
         * two modifiers in one bucket keep the order they were applied. The
         * instance that needs it is the witch's movementSpeed, which holds the
         * drinking speed penalty (operation 0) beside the saved half of the
         * random spawn bonus (operation 1); the enderman's, creeper's and the
         * rest hold one saved modifier each. */
        const struct attr_mod *saved_mods[ATTR_MAX_MODS];
        int nsaved = 0;
        for (int i = 0; i < a->nmods; ++i)
        {
            if (a->mods[i].saved && !a->mods[i].from_item)
            {
                saved_mods[nsaved++] = &a->mods[i];
            }
        }

        for (int i = 1; i < nsaved; ++i)
        {
            const struct attr_mod *key = saved_mods[i];
            int j = i - 1;
            while (j >= 0 && attr_mod_bucket(saved_mods[j]) > attr_mod_bucket(key))
            {
                saved_mods[j + 1] = saved_mods[j];
                --j;
            }
            saved_mods[j + 1] = key;
        }

        for (int i = 0; i < nsaved; ++i)
        {
            const struct attr_mod *m = saved_mods[i];
            nbtw_add_comp(w);
            nbtw_string(w, "Name", attr_name(m->name));
            nbtw_double(w, "Amount", m->amount);
            nbtw_int(w, "Operation", m->operation);
            nbtw_long(w, "UUIDMost", m->uuid_msb);
            nbtw_long(w, "UUIDLeast", m->uuid_lsb);
            nbtw_end(w);
        }

        nbtw_end(w);
    }

    nbtw_end(w);
}

/* living_write_nbt for one entity, without its vehicle. */
static void write_own_nbt(struct living *l, struct nbtw *w)
{
    nbtw_double3(w, NBTW_K("Pos"), l->e.pos_x, l->e.pos_y + (double)l->e.y_size, l->e.pos_z);
    nbtw_double3(w, NBTW_K("Motion"), l->e.motion_x, l->e.motion_y, l->e.motion_z);
    nbtw_float2(w, NBTW_K("Rotation"), l->rotation_yaw, l->rotation_pitch);
    nbtw_float(w, "FallDistance", l->e.fall_distance);
    nbtw_short(w, "Fire", l->e.fire);
    nbtw_short(w, "Air", l->air);
    nbtw_byte(w, "OnGround", l->e.on_ground ? 1 : 0);
    nbtw_int(w, "Dimension", l->dimension);
    nbtw_byte(w, "Invulnerable", l->invulnerable ? 1 : 0);
    nbtw_int(w, "PortalCooldown", l->time_until_portal);
    nbtw_long(w, "UUIDMost", l->uuid_msb);
    nbtw_long(w, "UUIDLeast", l->uuid_lsb);

    /* EntityLivingBase */
    nbtw_float(w, "HealF", l->health);
    nbtw_short(w, "Health", (short)(int)ceil((double)l->health));
    nbtw_short(w, "HurtTime", l->hurt_time);
    nbtw_short(w, "DeathTime", l->death_time);
    nbtw_short(w, "AttackTime", l->attack_time);
    nbtw_float(w, "AbsorptionAmount", l->absorption);

    nbtw_list(w, "Attributes");

    for (int i = 0; i < ATTR_COUNT; ++i)
    {
        if (l->attrs.a[i].registered)
        {
            attr_instance_w(w, &l->attrs.a[i]);
        }
    }

    nbtw_end(w);

    if (l->potions.size > 0)
    {
        nbtw_list(w, "ActiveEffects");
        uint8_t ids[POT_COUNT];
        int n = potion_map_order(&l->potions, ids);
        for (int i = 0; i < n; i++)
        {
            const struct potion_effect *e = &l->potions.eff[ids[i]];
            nbtw_add_comp(w);
            nbtw_byte(w, "Id", (int8_t)e->id);
            nbtw_byte(w, "Amplifier", e->amplifier);
            nbtw_int(w, "Duration", e->duration);
            nbtw_byte(w, "Ambient", e->is_ambient ? 1 : 0);
            nbtw_end(w);
        }
        nbtw_end(w);
    }

    if (l->kind == HK_PLAYER)
    {
        nbtw_int(w, "Score", 0);
        nbtw_int(w, "SelectedItemSlot", 0);
        nbtw_byte(w, "Sleeping", 0);
        nbtw_short(w, "SleepTimer", 0);
        nbtw_int(w, "XpLevel", 0);
        nbtw_float(w, "XpP", 0.0F);
        nbtw_int(w, "XpTotal", 0);
        nbtw_list(w, "Inventory");
        for (int i = 0; i < 36; ++i)
        {
            if (lv_player(l)->player_inv[i].id > 0 && lv_player(l)->player_inv[i].count > 0)
            {
                nbtw_add_comp(w);
                nbtw_byte(w, "Slot", (int8_t)i);
                nbtw_short(w, "id", (short)lv_player(l)->player_inv[i].id);
                nbtw_byte(w, "Count", (int8_t)lv_player(l)->player_inv[i].count);
                nbtw_short(w, "Damage", (short)lv_player(l)->player_inv[i].damage);
                itag_w(w, lv_player(l)->player_inv[i].tag);
                nbtw_end(w);
            }
        }
        nbtw_end(w);
        nbtw_list(w, "EnderItems");
        nbtw_end(w);

        nbtw_comp(w, "abilities");
        nbtw_byte(w, "invulnerable", 0);
        nbtw_byte(w, "flying", 0);
        nbtw_byte(w, "mayfly", 0);
        nbtw_byte(w, "instabuild", 0);
        nbtw_byte(w, "mayBuild", 1);
        nbtw_float(w, "flySpeed", 0.05F);
        nbtw_float(w, "walkSpeed", 0.1F);
        nbtw_end(w);

        nbtw_int(w, "foodLevel", l->food_level);
        nbtw_int(w, "foodTickTimer", l->food_timer);
        nbtw_float(w, "foodSaturationLevel", l->food_saturation);
        nbtw_float(w, "foodExhaustionLevel", l->food_exhaustion);
        return;
    }

    /* EntityLiving */
    /* EntityLiving; the probe's player is an EntityLivingBase, not an
     * EntityLiving, and writes the player NBT this lane does not model */
    if (l->kind != SK_PLAYER)
    {
    nbtw_byte(w, "CanPickUpLoot", l->can_pick_up_loot ? 1 : 0);
    nbtw_byte(w, "PersistenceRequired", l->persistence_required ? 1 : 0);

    nbtw_list(w, "Equipment");

    for (int i = 0; i < 5; ++i)
    {
        nbtw_add_comp(w);
        if (l->equip[i].id > 0)
        {
            int cnt = l->equip[i].count_link.owner ? *stack_link_count(l->equip[i].count_link) : l->equip[i].count;
            nbtw_short(w, "id", (short)l->equip[i].id);
            nbtw_byte(w, "Count", (int8_t)cnt);
            nbtw_short(w, "Damage", (short)l->equip[i].damage);
            itag_w(w, l->equip[i].tag);
        }
        nbtw_end(w);
    }

    nbtw_end(w);

    nbtw_list(w, "DropChances");

    for (int i = 0; i < 5; ++i) nbtw_add_float(w, l->equipment_drop_chances[i]);

    nbtw_end(w);
    nbtw_string(w, "CustomName", l->custom_name);
    nbtw_byte(w, "CustomNameVisible", 0);
    nbtw_byte(w, "Leashed", l->is_leashed ? 1 : 0);
    leash_write_nbt(l, w);
    }


    /* EntityAgeable: the animals and the villager; a slime is not ageable and the
     * probe's player writes its own player NBT (which this lane does not) */
    if (l->kind <= AK_SHEEP || l->kind == VK_VILLAGER)
    {
        nbtw_int(w, "Age", l->growing_age);
    }

    /* EntityAnimal */
    if (l->kind <= AK_SHEEP)
    {
        nbtw_int(w, "InLove", l->in_love);
    }

    /* EntitySlime: Size = getSlimeSize() - 1 */
    if (IS_SLIME_KIND(l->kind))
    {
        nbtw_int(w, "Size", l->slime_size - 1);
    }

    /* EntityVillager */
    if (l->kind == VK_VILLAGER)
    {
        nbtw_int(w, "Profession", l->profession);
        nbtw_int(w, "Riches", l->wealth);
        if (l->has_recipes)
        {
            nbtw_comp(w, "Offers");
            trades_w(w, &lv_villager(l)->recipes);
            nbtw_end(w);
        }
    }

    /* EntityIronGolem */
    if (l->kind == VK_IRON_GOLEM)
    {
        nbtw_byte(w, "PlayerCreated", l->is_player_created ? 1 : 0);
    }

    /* the kind's own */
    if (l->kind == GK_GHAST)
    {
        nbtw_int(w, "ExplosionPower", l->explosion_power);
    }
    else if (l->kind == HK_ZOMBIE || l->kind == HK_PIGMAN)
    {
        if (l->zombie_is_child) nbtw_byte(w, "IsBaby", 1);
        if (l->zombie_is_villager) nbtw_byte(w, "IsVillager", 1);
        nbtw_int(w, "ConversionTime", l->zombie_is_converting ? l->zombie_conversion_time : -1);
        nbtw_byte(w, "CanBreakDoors", l->zombie_can_break_doors ? 1 : 0);
        if (l->kind == HK_PIGMAN) pigman_write_kind_nbt(l, w);
    }
    else if (l->kind == HK_SKELETON)
    {
        skeleton_write_kind_nbt(l, w);
    }
    else if (l->kind == HK_SPIDER || l->kind == HK_CAVE_SPIDER)
    {
        spider_write_kind_nbt(l, w);
    }
    else if (l->kind == HK_CREEPER)
    {
        creeper_write_kind_nbt(l, w);
    }
    else if (l->kind == HK_ENDERMAN)
    {
        enderman_write_kind_nbt(l, w);
    }
    else
    {
        animal_write_kind_nbt(l, w);
    }

}

/* Entity.writeToNBT's tail: an entity carries ITS OWN vehicle (the thing it
 * rides, writeMountToNBT = the "id" string plus the whole NBT), which carries
 * its own, and so on. Java recurses down the chain; this walks it, making the
 * same tags in the same order: each "Riding" opens after its rider's own
 * fields (the last key the rider gets) and they close innermost first, as the
 * recursion's returns did. The spider jockey's rider is the only mount the
 * worlds hold (a chain of two). */
#define RIDING_CHAIN 16

void living_write_w(struct living *l, struct nbtw *w)
{
    int n = 0;

    for (;;)
    {
        write_own_nbt(l, w);

        if (l->riding_entity == 0) break;

        if (n >= RIDING_CHAIN)
        {
            fprintf(stderr, "living: a riding chain longer than %d\n", RIDING_CHAIN);
            abort();
        }

        nbtw_comp(w, "Riding");
        nbtw_string(w, "id", living_kind_name(lv_get(l->riding_entity)->kind));
        ++n;
        WL_DEPTH_NOTE(riding, n + 1);
        l = lv_get(l->riding_entity);
    }

    while (n-- > 0) nbtw_end(w);
}

void living_write_nbt(struct living *l, struct nbt *tag)
{
    struct nbtw w;
    nbtw_tree(&w, tag);
    living_write_w(l, &w);
}

/* EntityItem and EntityXPOrb writeEntityToNBT over the item world's record. */
/* EntityList.getEntityString: the name the canonical NBT and writeMountToNBT
 * carry. The kinds the probe can hold. */
const char *living_kind_name(int kind)
{
    switch (kind)
    {
        case HK_ZOMBIE: return "Zombie";
        case HK_SKELETON: return "Skeleton";
        case HK_CREEPER: return "Creeper";
        case HK_SPIDER: return "Spider";
        case HK_CAVE_SPIDER: return "CaveSpider";
        case HK_PIGMAN: return "PigZombie";
        case HK_BLAZE: return "Blaze";
        case HK_ENDERMAN: return "Enderman";
        case HK_WITCH: return "Witch";
        case HK_SILVERFISH: return "Silverfish";
        case GK_GHAST: return "Ghast";
        case SK_SLIME: return "Slime";
        case SK_MAGMA_CUBE: return "LavaSlime";
        case AK_PIG: return "Pig";
        case AK_COW: return "Cow";
        case AK_MOOSHROOM: return "MushroomCow";
        case AK_CHICKEN: return "Chicken";
        case AK_SHEEP: return "Sheep";
        case AK_BAT: return "Bat";
        case AK_SQUID: return "Squid";
        case VK_VILLAGER: return "Villager";
        case VK_IRON_GOLEM: return "VillagerGolem";
        default: return NULL;
    }
}

void item_entity_write_nbt(ie_ent *en, struct nbt *tag)
{
    nbt_put(tag, "Pos", nbt_double_list3(en->e.pos_x, en->e.pos_y + (double)en->e.y_size, en->e.pos_z));
    nbt_put(tag, "Motion", nbt_double_list3(en->e.motion_x, en->e.motion_y, en->e.motion_z));
    nbt_put(tag, "Rotation", nbt_float_list2(en->rotation_yaw, en->rotation_pitch));
    nbt_put(tag, "FallDistance", nbt_new_float(en->e.fall_distance));
    nbt_put(tag, "Fire", nbt_new_short(en->e.fire));
    /* neither kind is living, so Entity.air never leaves the constructor's 300 */
    nbt_put(tag, "Air", nbt_new_short(300));
    nbt_put(tag, "OnGround", nbt_new_byte(en->e.on_ground ? 1 : 0));
    nbt_put(tag, "Dimension", nbt_new_int(en->dimension));
    nbt_put(tag, "Invulnerable", nbt_new_byte(0));
    nbt_put(tag, "PortalCooldown", nbt_new_int(0));
    nbt_put(tag, "UUIDMost", nbt_new_long(en->uuid_msb));
    nbt_put(tag, "UUIDLeast", nbt_new_long(en->uuid_lsb));

    if (en->kind == IE_ITEM)
    {
        nbt_put(tag, "Health", nbt_new_short((short)(int8_t)en->health));
        nbt_put(tag, "Age", nbt_new_short(en->age));

        struct nbt *stack = nbt_new_compound();
        nbt_put(stack, "id", nbt_new_short(en->stack_item));
        /* an item a mob picked up is dead and its ItemStack is the mob's
         * slot's (hostile_loot_update), whose count later merges move */
        int count = en->is_dead && en->stack_count_link.owner && en->stack_count_link.owner != stack_link_item(en).owner
                        ? *stack_link_count(en->stack_count_link)
                        : en->stack_count;
        nbt_put(stack, "Count", nbt_new_byte((int8_t)count));
        nbt_put(stack, "Damage", nbt_new_short(en->stack_damage));
        itag_put(stack, en->stack_tag);
        nbt_put(tag, "Item", stack);
    }
    else if (en->kind == IE_ORB)
    {
        nbt_put(tag, "Health", nbt_new_short((short)(int8_t)en->health));
        nbt_put(tag, "Age", nbt_new_short(en->age));
        nbt_put(tag, "Value", nbt_new_short(en->xp_value));
    }
    else if (en->kind == IE_POTION)
    {
        nbt_put(tag, "xTile", nbt_new_short((short)en->tile_x));
        nbt_put(tag, "yTile", nbt_new_short((short)en->tile_y));
        nbt_put(tag, "zTile", nbt_new_short((short)en->tile_z));
        nbt_put(tag, "inTile", nbt_new_byte((int8_t)en->in_tile));
        nbt_put(tag, "shake", nbt_new_byte((int8_t)en->shake));
        nbt_put(tag, "inGround", nbt_new_byte(en->in_ground ? 1 : 0));
        nbt_put(tag, "ownerName", nbt_new_string(""));

        struct nbt *stack = nbt_new_compound();
        nbt_put(stack, "id", nbt_new_short(373));
        nbt_put(stack, "Count", nbt_new_byte((int8_t)(1 - en->potion_spent)));
        nbt_put(stack, "Damage", nbt_new_short((short)en->potion_damage));
        nbt_put(tag, "Potion", stack);
    }
    else if (en->kind == IE_ARROW)
    {
        nbt_put(tag, "xTile", nbt_new_short((short)en->tile_x));
        nbt_put(tag, "yTile", nbt_new_short((short)en->tile_y));
        nbt_put(tag, "zTile", nbt_new_short((short)en->tile_z));
        nbt_put(tag, "life", nbt_new_short((short)en->ticks_in_ground));
        nbt_put(tag, "inTile", nbt_new_byte((int8_t)en->in_tile));
        nbt_put(tag, "inData", nbt_new_byte((int8_t)en->in_data));
        nbt_put(tag, "shake", nbt_new_byte((int8_t)en->shake));
        nbt_put(tag, "inGround", nbt_new_byte(en->in_ground ? 1 : 0));
        nbt_put(tag, "pickup", nbt_new_byte((int8_t)en->can_be_picked_up));
        nbt_put(tag, "damage", nbt_new_double(en->arrow_damage));
    }
    else if (en->kind == IE_SNOWBALL || en->kind == IE_EGG || en->kind == IE_ENDER_PEARL || en->kind == IE_EXP_BOTTLE)
    {
        nbt_put(tag, "xTile", nbt_new_short((short)en->tile_x));
        nbt_put(tag, "yTile", nbt_new_short((short)en->tile_y));
        nbt_put(tag, "zTile", nbt_new_short((short)en->tile_z));
        nbt_put(tag, "inTile", nbt_new_byte((int8_t)en->in_tile));
        nbt_put(tag, "shake", nbt_new_byte((int8_t)en->shake));
        nbt_put(tag, "inGround", nbt_new_byte(en->in_ground ? 1 : 0));
        nbt_put(tag, "ownerName", nbt_new_string(""));
    }
    else if (en->kind == IE_SMALL_FIREBALL || en->kind == IE_LARGE_FIREBALL)
    {
        nbt_put(tag, "xTile", nbt_new_short((short)en->tile_x));
        nbt_put(tag, "yTile", nbt_new_short((short)en->tile_y));
        nbt_put(tag, "zTile", nbt_new_short((short)en->tile_z));
        nbt_put(tag, "inTile", nbt_new_byte((int8_t)en->in_tile));
        nbt_put(tag, "inGround", nbt_new_byte(en->in_ground ? 1 : 0));
        nbt_put(tag, "direction", nbt_double_list3(en->e.motion_x, en->e.motion_y, en->e.motion_z));
        if (en->kind == IE_LARGE_FIREBALL)
        {
            nbt_put(tag, "ExplosionPower", nbt_new_int(en->explosion_power));
        }
    }
}

/* ------------------------------------------------------------- the spawns */

/* The entity-list insert every spawn path shares. The chunk insert is
 * separable: the spider jockey's rider joins the chunk before its vehicle but
 * the tick list after it (the probe's two lists see the two in opposite
 * orders). */
struct an_ent *an_add_living_list(struct an_world *an, struct living *l, int spawn_index)
{
    struct an_ent *en = an_ent_alloc();
    en->used = 1;
    en->is_living = 1;
    en->spawn_index = spawn_index;
    en->livh = lv_ref(l);
    en->ieh = 0;
    an_list_push(an, en);
    return en;
}

void an_add_living_chunk(struct an_world *an, struct an_ent *en, struct living *l)
{
    an_chunk_add(an, en, mh_floor(l->e.pos_x / 16.0), mh_floor(l->e.pos_y / 16.0),
                 mh_floor(l->e.pos_z / 16.0));
}

void an_add_living(struct an_world *an, struct living *l, int spawn_index)
{
    struct an_ent *en = an_add_living_list(an, l, spawn_index);
    an_add_living_chunk(an, en, l);
}

struct living *an_spawn_living(struct an_world *an, int kind, int spawn_index, double x, double y, double z,
                               float yaw, float pitch, int growing_age, int in_love, int saddled, int egg_timer,
                               int sheared, int with_egg)
{
    struct living *l = living_alloc();

    living_init(l, an->w, kind, an->det);
    l->an = an;
    l->dimension = an->dimension;
    an_construct_kind(l, an->det);
    living_set_location_and_angles(l, x, y, z, yaw, pitch);

    if (growing_age != 0) living_set_growing_age(l, growing_age);
    if (in_love) l->in_love = 600;
    if (saddled) l->data_watcher_16 = 1;
    if (sheared) l->data_watcher_16 |= 16;
    if (kind == AK_CHICKEN && egg_timer != 0) l->time_until_next_egg = egg_timer;

    /* onSpawnWithEgg is the spawn-list path's, not the breeding path's: a child
     * from createChild never gets the random follow-range bonus */
    l->difficulty_factor = an->spawn_diff_factor;
    if (with_egg) living_on_spawn_with_egg(l, an->det);

    an_add_living(an, l, spawn_index);
    return l;
}

ie_ent *an_spawn_orb(struct an_world *an, int xp, double x, double y, double z)
{
    ie_ent *ie = ie_spawn_orb(&an->iew, x, y, z, xp);

    if (!ie) return NULL;

    /* the item world's chunk sections need the orb too: its AABB searches
     * (searchForOtherItemsNearby and the player pull) walk them */
    ie_added_to_world(&an->iew, ie);
    ie->dimension = an->dimension;
    /* the probe's spawn index is the list position the entity takes */
    ie->spawn_index = an->n;

    struct an_ent *en = an_ent_alloc();
    en->used = 1;
    en->is_living = 0;
    en->spawn_index = ie->spawn_index;
    en->livh = 0;
    en->ieh = ie_ref(ie);
    an_list_push(an, en);
    an_chunk_add(an, en, mh_floor(ie->e.pos_x / 16.0), mh_floor(ie->e.pos_y / 16.0),
                 mh_floor(ie->e.pos_z / 16.0));
    return ie;
}

ie_ent *an_spawn_item(struct an_world *an, int spawn_index, double x, double y, double z, int item, int damage,
                      int count, int delay)
{
    ie_ent *ie = ie_spawn_item(&an->iew, x, y, z, item, damage, count);

    if (!ie) return NULL;

    ie_added_to_world(&an->iew, ie);
    ie->dimension = an->dimension;
    ie->spawn_index = spawn_index;
    ie->delay = delay;

    struct an_ent *en = an_ent_alloc();
    en->used = 1;
    en->is_living = 0;
    en->spawn_index = spawn_index;
    en->livh = 0;
    en->ieh = ie_ref(ie);
    an_list_push(an, en);
    an_chunk_add(an, en, mh_floor(ie->e.pos_x / 16.0), mh_floor(ie->e.pos_y / 16.0),
                 mh_floor(ie->e.pos_z / 16.0));
    return ie;
}

/* --------------------------------------------------------- the state record */

static void put32(unsigned char *b, int *p, int v)
{
    uint32_t u = (uint32_t)v;
    b[(*p)++] = (unsigned char)u;
    b[(*p)++] = (unsigned char)(u >> 8);
    b[(*p)++] = (unsigned char)(u >> 16);
    b[(*p)++] = (unsigned char)(u >> 24);
}

static void put64(unsigned char *b, int *p, uint64_t v)
{
    for (int i = 0; i < 8; ++i) b[(*p)++] = (unsigned char)(v >> (8 * i));
}

static void putf(unsigned char *b, int *p, float v)
{
    uint32_t u;
    memcpy(&u, &v, 4);
    put32(b, p, (int)u);
}

static void putd(unsigned char *b, int *p, double v)
{
    uint64_t u;
    memcpy(&u, &v, 8);
    put64(b, p, u);
}

uint64_t fnv_text(const char *text)
{
    uint64_t h = 0xcbf29ce484222325ULL;

    for (const char *s = text; *s; ++s) h = (h ^ (unsigned char)*s) * 0x100000001b3ULL;

    return h;
}

/* The canonical NBT hash the probe records: FNV-1a 64 over the rendered text. */
uint64_t living_nbt_hash(struct living *l)
{
    struct nbt *tag = nbt_new_compound();
    living_write_nbt(l, tag);
    char *text = nbt_render(tag);
    uint64_t h = fnv_text(text);
    free(text);
    nbt_free(tag);
    return h;
}

uint64_t item_entity_nbt_hash(ie_ent *en)
{
    struct nbt *tag = nbt_new_compound();
    item_entity_write_nbt(en, tag);
    char *text = nbt_render(tag);
    uint64_t h = fnv_text(text);
    free(text);
    nbt_free(tag);
    return h;
}

void living_nbt_text(struct living *l, char **out)
{
    struct nbt *tag = nbt_new_compound();
    living_write_nbt(l, tag);
    *out = nbt_render(tag);
    nbt_free(tag);
}

void item_entity_nbt_text(ie_ent *en, char **out)
{
    struct nbt *tag = nbt_new_compound();
    item_entity_write_nbt(en, tag);
    *out = nbt_render(tag);
    nbt_free(tag);
}

static uint64_t nav_hash(const struct path_ent *p)
{
    uint64_t h = 0xcbf29ce484222325ULL;

    if (!p) return h;

    int n = p->n_points > 0 ? p->n_points : p->length;
    for (int i = 0; i < n; ++i)
    {
        for (int k = 0; k < 3; ++k)
        {
            int v = p->pts[i][k];

            for (int j = 0; j < 4; ++j) h = (h ^ (unsigned char)((v >> (8 * j)) & 255)) * 0x100000001b3ULL;
        }
    }

    return h;
}

void an_write_state(struct an_world *an, const struct an_ent *en, unsigned char *rec)
{
    int p = 0;
    put32(rec, &p, en->spawn_index);

    if (!en->is_living)
    {
        ie_ent *ie = ie_get(en->ieh);
        put32(rec, &p, ie->entity_id);
        put32(rec, &p, -1);
        put64(rec, &p, item_entity_nbt_hash(ie));
        put32(rec, &p, 0);       /* entity_age */
        put32(rec, &p, ie->ticks_existed);
        put32(rec, &p, -1);      /* ai tick count */
        put32(rec, &p, 0);       /* ai executing */

        for (int i = 0; i < 10; ++i) put32(rec, &p, -1);

        put32(rec, &p, 0);
        put32(rec, &p, 0);
        put32(rec, &p, 0);
        put32(rec, &p, 0);
        put32(rec, &p, 0);
        putd(rec, &p, 0.0);
        putd(rec, &p, 0.0);
        putd(rec, &p, 0.0);
        putd(rec, &p, 0.0);
        put64(rec, &p, 0xcbf29ce484222325ULL);

        put32(rec, &p, 0);
        putd(rec, &p, 0.0);
        putd(rec, &p, 0.0);
        putd(rec, &p, 0.0);
        putd(rec, &p, 0.0);

        put32(rec, &p, 0);
        putd(rec, &p, 0.0);
        putd(rec, &p, 0.0);
        putd(rec, &p, 0.0);
        putf(rec, &p, 0.0F);
        putf(rec, &p, 0.0F);

        put32(rec, &p, 0);
        putf(rec, &p, 0.0F);
        putf(rec, &p, 0.0F);
        putf(rec, &p, 0.0F);
        putf(rec, &p, 0.0F);

        put32(rec, &p, 0);
        put32(rec, &p, 0);
        putf(rec, &p, 0.0F);
        put32(rec, &p, -1);
        put32(rec, &p, -1);
        put32(rec, &p, 0);
        put32(rec, &p, 0);
        put32(rec, &p, (ie->added_to_chunk ? 1 : 0) | (ie->e.on_ground ? 2 : 0));
        put64(rec, &p, (uint64_t)ie->rand.r.seed);
        put32(rec, &p, 0);
        return;
    }

    struct living *l = lv_get(en->livh);
    put32(rec, &p, l->entity_id);

    int pk = l->kind;
    if (l->kind == HK_ZOMBIE) pk = 0;
    else if (l->kind == HK_SKELETON) pk = 1;
    else if (l->kind == HK_CREEPER) pk = 2;
    else if (l->kind == HK_SPIDER) pk = 3;
    else if (l->kind == HK_CAVE_SPIDER) pk = 10;
    else if (l->kind == HK_PLAYER) pk = -1;
    else if (l->kind == HK_ENDERMAN) pk = 5;
    else if (l->kind == HK_WITCH) pk = 6;
    else if (l->kind == HK_SILVERFISH) pk = 7;
    else if (l->kind == HK_PIGMAN) pk = 8;
    else if (l->kind == HK_BLAZE) pk = 9;
    /* AnimalProbe's kindOf: -1 for the squid and the bat */
    else if (l->kind == AK_SQUID || l->kind == AK_BAT) pk = -1;
    put32(rec, &p, pk);

    put64(rec, &p, living_nbt_hash(l));
    put32(rec, &p, l->entity_age);
    put32(rec, &p, l->ticks_existed);

    if (l->kind == HK_PLAYER)
    {
        put32(rec, &p, -1);
        put32(rec, &p, 0);
        for (int i = 0; i < 10; ++i) put32(rec, &p, -1);
        put32(rec, &p, 0);
        put32(rec, &p, 0);
        put32(rec, &p, 0);
        put32(rec, &p, 0);
        put32(rec, &p, 0);
        putd(rec, &p, 0.0);
        putd(rec, &p, 0.0);
        putd(rec, &p, 0.0);
        putd(rec, &p, 0.0);
        put64(rec, &p, 0xcbf29ce484222325ULL);
        put32(rec, &p, 0);
        putd(rec, &p, 0.0);
        putd(rec, &p, 0.0);
        putd(rec, &p, 0.0);
        putd(rec, &p, 0.0);
        put32(rec, &p, 0);
        putd(rec, &p, 0.0);
        putd(rec, &p, 0.0);
        putd(rec, &p, 0.0);
        putf(rec, &p, 0.0F);
        putf(rec, &p, 0.0F);
        put32(rec, &p, 0);
        putf(rec, &p, 0.0F);
        putf(rec, &p, 0.0F);
        putf(rec, &p, l->rotation_yaw_head);
        putf(rec, &p, l->render_yaw_offset);
        put32(rec, &p, 0);
        put32(rec, &p, 0);
        putf(rec, &p, 0.0F);
        put32(rec, &p, -1);
        put32(rec, &p, -1);
        put32(rec, &p, 0);
        put32(rec, &p, l->revenge_timer);
        put32(rec, &p, (l->added_to_chunk ? 1 : 0) | (l->e.on_ground ? 2 : 0));
        put64(rec, &p, (uint64_t)l->rand.r.seed);
        put32(rec, &p, 0);
        return;
    }

    put32(rec, &p, lv_ai(l)->tasks.tick_count);

    int mask = 0;

    for (int i = 0; i < lv_ai(l)->tasks.nexec; ++i) mask |= 1 << lv_ai(l)->tasks.executing[i];

    put32(rec, &p, mask);

    for (int i = 0; i < 10; ++i)
    {
        int v = -1;

        if (i < lv_ai(l)->tasks.n)
        {
            struct ai_task *t = &lv_ai(l)->tasks.entries[i].t;

            switch (t->cls)
            {
                case AIC_MATE: v = t->spawn_baby_delay; break;
                case AIC_TEMPT: v = t->delay_tempt_counter; break;
                case AIC_FOLLOW_PARENT: v = t->follow_parent_delay; break;
                case AIC_WATCH_CLOSEST: v = t->look_time; break;
                case AIC_LOOK_IDLE: v = t->idle_time; break;
                case AIC_EAT_GRASS: v = t->eat_grass_timer; break;
                default: v = -1; break;
            }
        }

        put32(rec, &p, v);
    }

    const struct path_ent *path = path_at(l->nav.path);
    put32(rec, &p, path ? 1 : 0);
    put32(rec, &p, path ? path->index : 0);
    put32(rec, &p, path ? path->length : 0);
    put32(rec, &p, l->nav.total_ticks);
    put32(rec, &p, l->nav.ticks_at_last_pos);
    putd(rec, &p, l->nav.speed);
    putd(rec, &p, l->nav.lx);
    putd(rec, &p, l->nav.ly);
    putd(rec, &p, l->nav.lz);
    put64(rec, &p, nav_hash(path));

    put32(rec, &p, l->move.update);
    putd(rec, &p, l->move.x);
    putd(rec, &p, l->move.y);
    putd(rec, &p, l->move.z);
    putd(rec, &p, l->move.speed);

    put32(rec, &p, l->look.is_looking);
    putd(rec, &p, l->look.x);
    putd(rec, &p, l->look.y);
    putd(rec, &p, l->look.z);
    putf(rec, &p, l->look.delta_look_yaw);
    putf(rec, &p, l->look.delta_look_pitch);

    put32(rec, &p, l->jump.is_jumping);
    putf(rec, &p, l->move_forward);
    putf(rec, &p, l->move_strafing);
    putf(rec, &p, l->rotation_yaw_head);
    putf(rec, &p, l->render_yaw_offset);

    put32(rec, &p, l->living_sound_time);
    put32(rec, &p, l->body.counter);
    putf(rec, &p, l->body.yaw);
    put32(rec, &p, l->kind == AK_SHEEP ? l->sheep_timer : -1);
    put32(rec, &p, l->kind == AK_CHICKEN ? l->time_until_next_egg : -1);
    put32(rec, &p, l->breeding);
    put32(rec, &p, l->revenge_timer);
    put32(rec, &p, (l->added_to_chunk ? 1 : 0) | (l->e.on_ground ? 2 : 0));
    put64(rec, &p, (uint64_t)l->rand.r.seed);
    put32(rec, &p, 0);
}

/* ------------------------------------------------------- the constructor */

/* Entity, EntityLivingBase and EntityLiving's constructors, in order, then the
 * kind's (animal_construct). The Det draws are the probe's OTHER role, and the
 * order is Java's: the entity id, the per-entity Random, the UUID, the three
 * Math.random values EntityLivingBase draws, and the kind's own. */
void living_init(struct living *l, struct world *w, int kind, det_state *det)
{
    /* the record and its parts start over; the slot stays */
    int32_t part = l->part;
    if (part > 0) living_parts_release(l);
    memset(l, 0, sizeof *l);
    l->part = part;
    l->maximum_home_distance = -1.0F;
    l->world = w;
    l->kind = kind;

    /* Entity(World) */
    entity_init(&l->e, w);
    l->e.self = l;
    l->e.attack_from = living_attack_cb;
    l->e.fizz = living_fizz_cb;
    l->e.fire_time = living_fire_time_cb;
    l->e.swim_sound = living_swim_sound_cb;
    l->e.walking_block = living_walking_block_cb;
    l->e.kind_in_water = living_kind_in_water_cb;
    if (kind != HK_PLAYER)
    {
        l->e.set_in_portal = living_set_in_portal_cb;
        l->e.end_portal = living_end_portal_cb;
    }
    l->entity_id = det_next_entity_id_role(det, DET_OTHER);
    l->rand = det_new_random_role(det, DET_OTHER);
    det_uuid_role(det, DET_OTHER, &l->uuid_msb, &l->uuid_lsb);
    entity_set_position(&l->e, 0.0, 0.0, 0.0);
    l->dimension = 0;

    /* dataWatcher: 0 byte, 1 short 300, then entityInit's objects */
    l->flags0 = 0;
    l->air = 300;
    l->health = 1.0F;          /* dataWatcher 6 */
    l->growing_age = 0;        /* dataWatcher 12 */
    l->data_watcher_16 = 0;    /* dataWatcher 16, the pig's saddle and the sheep's colour */

    /* EntityLiving(World) */
    l->max_hurt_resistant_time = 20;

    if (kind == VK_VILLAGER)
    {
        l->on_living_update = villager_on_living_update;
    }
    else if (kind == VK_IRON_GOLEM)
    {
        l->on_living_update = iron_golem_on_living_update;
    }
    else if (kind == GK_GHAST)
    {
        /* EntityGhast leaves onLivingUpdate, the old-AI branch inside, and the
         * EntityLiving looting half all at the EntityLivingBase defaults */
        l->on_living_update = living_default_on_living_update;
    }
    else if (kind == HK_ZOMBIE)
    {
        l->on_living_update = zombie_on_living_update;
    }
else if (kind == HK_SPIDER || kind == HK_CAVE_SPIDER)
    {
        l->on_living_update = spider_on_living_update;
    }
    else if (kind == HK_PIGMAN)
    {
        l->on_living_update = pigman_on_living_update;
    }
    else if (kind == HK_SKELETON)
    {
        l->on_living_update = skeleton_on_living_update;
    }
    else if (kind == HK_CREEPER)
    {
        l->on_living_update = creeper_on_living_update;
    }
    else if (kind == HK_WITCH)
    {
        l->on_living_update = witch_on_living_update;
    }
    else if (kind == HK_BLAZE)
    {
        l->on_living_update = blaze_on_living_update;
    }
    else if (kind == HK_PLAYER)
    {
        l->on_living_update = player_on_living_update;
    }
    else if (kind == AK_SQUID)
    {
        l->on_living_update = squid_on_living_update;

    }
    else if (kind == AK_BAT)
    {
        l->on_living_update = squidbat_bat_on_living_update;
    }
    else if (kind == HK_ENDERMAN)
    {
        l->on_living_update = enderman_on_living_update;
    }
    else if (kind == HK_SILVERFISH)
    {
        l->on_living_update = silverfish_on_living_update;
    }
    else
    {
        l->on_living_update = animal_on_living_update;
    }

    for (int i = 0; i < 5; ++i) l->equipment_drop_chances[i] = 0.085F;

    /* EntityLivingBase(World) */
    attrs_init(&l->attrs);
    if (kind == VK_VILLAGER)
    {
        attrs_set_base(&l->attrs.a[ATTR_MOVEMENT_SPEED], 0.5);
        attrs_set_base(&l->attrs.a[ATTR_MAX_HEALTH], 20.0);
        attrs_set_base(&l->attrs.a[ATTR_FOLLOW_RANGE], 16.0);
    }
    else if (kind == VK_IRON_GOLEM)
    {
        attrs_set_base(&l->attrs.a[ATTR_MOVEMENT_SPEED], 0.25);
        attrs_set_base(&l->attrs.a[ATTR_MAX_HEALTH], 100.0);
        attrs_set_base(&l->attrs.a[ATTR_FOLLOW_RANGE], 16.0);
    }
    else if (kind == GK_GHAST)
    {
        /* EntityLivingBase.applyEntityAttributes registers maxHealth,
         * knockbackResistance and movementSpeed; !isAIEnabled drops the speed
         * base to 0.1; EntityGhast.applyEntityAttributes sets maxHealth 10 */
        attrs_set_base(&l->attrs.a[ATTR_MOVEMENT_SPEED], 0.10000000149011612);
        attrs_set_base(&l->attrs.a[ATTR_MAX_HEALTH], 10.0);
        attrs_set_base(&l->attrs.a[ATTR_FOLLOW_RANGE], 16.0);
    }
    else if (kind == HK_ZOMBIE || kind == HK_PLAYER || kind == HK_SKELETON || kind == HK_CREEPER
|| kind == HK_ENDERMAN || kind == HK_SPIDER || kind == HK_CAVE_SPIDER || kind == HK_WITCH || kind == HK_SILVERFISH || kind == HK_PIGMAN || kind == HK_BLAZE)
    {
        /* handled by zombie_construct / player_construct / skeleton_construct /
         * creeper_construct / enderman_construct / spider_construct */
    }
    else
    {
        animal_attributes(l);
    }
    living_set_health(l, living_max_health(l));

    potion_map_init(&l->potions);
    l->potions_need_update = 0;
    l->potion_liquid_color = 0;
    l->potion_is_ambient = 0;
    l->e.prevent_entity_spawning = 1;

    (void)det_math_random_role(det, DET_OTHER);   /* field_70770_ap */
    (void)det_math_random_role(det, DET_OTHER);   /* field_70769_ao */
    entity_set_position(&l->e, l->e.pos_x, l->e.pos_y, l->e.pos_z);
    float yaw = (float)(det_math_random_role(det, DET_OTHER) * 3.141592653589793 * 2.0);
    l->rotation_yaw = yaw;
    l->rotation_yaw_head = yaw;
    l->e.step_height = 0.5F;
}
