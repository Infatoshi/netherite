#include "player_survival.h"

#include "game/mob_live.h"
#include "game/underwater.h"

#include "combat_math.h"
#include "enchant_damage_full.h"
#include "game/block_normal_cube_1_11_2.h"
#include "items_tools_armor.h"
#include "inventory_stack_rules.h"
#include "mc_rng.h"
#include "potion_effects_combat.h"
#include "potion_throwable.h"
#include "player_vitals.h"
#include "projectile_motion.h"
#include "world/populate_mc.h"

#include <math.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define GM_MOB_REACH 2.0
#define GM_MOB_BLOCKS 256
#define GM_MOB_WANDER_INTERVAL 120
#define GM_MOB_WANDER_RADIUS 8
#define GM_MOB_REVENGE_TICKS 101
#define GM_MOB_DESPAWN_SOFT 32.0
#define GM_MOB_DESPAWN_HARD 128.0
#define GM_MOB_DESPAWN_DELAY 600
#define GM_MOB_FIRE_TICKS 160
#define GM_NATURAL_HOSTILE_CAP 70
#define GM_NATURAL_PASSIVE_CAP 10
#define GM_PIGMAN_ANGER_BASE 400
#define GM_PIGMAN_ANGER_RANGE 400
#define GM_PIGMAN_HELP_RANGE 32.0
#define B_SWAMP 6

/* MathHelper.atan2 in 1.11.2 is a table approximation, not libc atan2. Keep
 * this local to the CPU live game: the CUDA entity spine does not navigate
 * mating animals, and inactive mating never pays the table initialization. */
static double mob_java_math_atan2(double y, double x) {
    static double asine[257];
    static double cosine[257];
    static int initialized;
    const double frac_bias = 17592186044416.0;
    double squared = x * x + y * y;
    uint64_t bits;
    if (isnan(squared)) return NAN;
    if (!initialized) {
        for (int i = 0; i <= 256; ++i) {
            double value = asin((double)i / 256.0);
            asine[i] = value;
            cosine[i] = cos(value);
        }
        initialized = 1;
    }
    int negative_y = y < 0.0;
    if (negative_y) y = -y;
    int negative_x = x < 0.0;
    if (negative_x) x = -x;
    int swapped = y > x;
    if (swapped) {
        double hold = x;
        x = y;
        y = hold;
    }
    double half = 0.5 * squared;
    memcpy(&bits, &squared, sizeof bits);
    bits = UINT64_C(6910469410427058090) - (bits >> 1);
    double inv;
    memcpy(&inv, &bits, sizeof inv);
    inv = inv * (1.5 - half * inv * inv);
    x *= inv;
    y *= inv;
    double biased = frac_bias + y;
    memcpy(&bits, &biased, sizeof bits);
    int index = (int)(uint32_t)bits;
    double table_value = asine[index];
    double quantized = biased - frac_bias;
    double error = y * cosine[index] - x * quantized;
    table_value += (6.0 + error * error) * error
        * 0.16666666666666666;
    if (swapped) table_value = MC_PI / 2.0 - table_value;
    if (negative_x) table_value = MC_PI - table_value;
    if (negative_y) table_value = -table_value;
    return table_value;
}

static float mob_move_helper_yaw(
        float current, double dx, double dz) {
    float target = (float)(
        mob_java_math_atan2(dz, dx) * (180.0 / MC_PI)) - 90.0F;
    float delta = fmodf(target - current, 360.0F);
    if (delta >= 180.0F) delta -= 360.0F;
    if (delta < -180.0F) delta += 360.0F;
    if (delta > 90.0F) delta = 90.0F;
    if (delta < -90.0F) delta = -90.0F;
    float result = current + delta;
    if (result < 0.0F) result += 360.0F;
    else if (result > 360.0F) result -= 360.0F;
    return result;
}

static float animal_mating_movement_speed(int type) {
    if (type == EW_TYPE_COW) return 0.20000000298023224F;
    if (type == EW_TYPE_PIG || type == EW_TYPE_CHICKEN) return 0.25F;
    return 0.23000000417232513F;
}

static EwStore *now_store(GmMobLive *m) { return m->current ? &m->b : &m->a; }
static EwStore *next_store(GmMobLive *m) { return m->current ? &m->a : &m->b; }
static const EwStore *const_store(const GmMobLive *m) { return m->current ? &m->b : &m->a; }

static unsigned int loaded_next_generation(unsigned int value) {
    ++value;
    return value ? value : 1U;
}

static int loaded_ref_valid_any(
        const GmMobLive *m, const GmMobLoadedRef *ref) {
    int slot;
    if (!m || !ref) return 0;
    slot = ref->slot;
    if (ref->kind == GM_MOB_LOADED_LIVING) {
        if (slot <= 0 || slot >= EW_MAX_ENTITIES
                || ref->generation != m->living_loaded_generation[slot])
            return 0;
        return (m->a.alive[slot] && m->a.id[slot] == ref->eid)
            || (m->b.alive[slot] && m->b.id[slot] == ref->eid);
    }
    if (ref->kind == GM_MOB_LOADED_XP) {
        if (slot < 0 || slot >= GM_XP_ORBS
                || ref->generation != m->xp_loaded_generation[slot])
            return 0;
        return !m->xp_orbs[slot].dead
            && m->xp_orbs[slot].xpValue > 0
            && m->xp_orbs[slot].eid == ref->eid;
    }
    return 0;
}

static void loaded_order_compact(GmMobLive *m) {
    int out = 0;
    if (!m) return;
    for (int index = 0; index < m->loaded_order_count; ++index)
        if (loaded_ref_valid_any(m, &m->loaded_order[index]))
            m->loaded_order[out++] = m->loaded_order[index];
    m->loaded_order_count = out;
}

static int loaded_append_living(GmMobLive *m, const EwStore *s, int slot) {
    if (!m || !s || slot <= 0 || slot >= EW_MAX_ENTITIES
            || !s->alive[slot])
        return 0;
    if (m->loaded_order_count >= GM_MOB_LOADED_ORDER_CAPACITY)
        loaded_order_compact(m);
    if (m->loaded_order_count >= GM_MOB_LOADED_ORDER_CAPACITY) return 0;
    m->living_loaded_generation[slot] = loaded_next_generation(
        m->living_loaded_generation[slot]);
    m->loaded_order[m->loaded_order_count++] = (GmMobLoadedRef){
        .eid = s->id[slot],
        .generation = m->living_loaded_generation[slot],
        .kind = GM_MOB_LOADED_LIVING,
        .slot = (unsigned char)slot
    };
    return 1;
}

static int loaded_append_xp(GmMobLive *m, int slot) {
    if (!m || slot < 0 || slot >= GM_XP_ORBS
            || m->xp_orbs[slot].dead || m->xp_orbs[slot].xpValue <= 0)
        return 0;
    if (m->loaded_order_count >= GM_MOB_LOADED_ORDER_CAPACITY)
        loaded_order_compact(m);
    if (m->loaded_order_count >= GM_MOB_LOADED_ORDER_CAPACITY) return 0;
    m->xp_loaded_generation[slot] = loaded_next_generation(
        m->xp_loaded_generation[slot]);
    m->loaded_order[m->loaded_order_count++] = (GmMobLoadedRef){
        .eid = m->xp_orbs[slot].eid,
        .generation = m->xp_loaded_generation[slot],
        .kind = GM_MOB_LOADED_XP,
        .slot = (unsigned char)slot
    };
    return 1;
}

static void loaded_invalidate_living(GmMobLive *m, int slot) {
    if (m && slot > 0 && slot < EW_MAX_ENTITIES)
        m->living_loaded_generation[slot] = loaded_next_generation(
            m->living_loaded_generation[slot]);
}

static void loaded_invalidate_xp(GmMobLive *m, int slot) {
    if (m && slot >= 0 && slot < GM_XP_ORBS)
        m->xp_loaded_generation[slot] = loaded_next_generation(
            m->xp_loaded_generation[slot]);
}

static void loaded_order_prepare(GmMobLive *m) {
    if (!m) return;
    loaded_order_compact(m);
    if (m->loaded_order_count != 0) return;
    /* Legacy/cold callers that populate stores without the spawn APIs have no
     * captured Java loaded-list order. Preserve the former deterministic
     * living-then-XP fallback; imported parity states must restore this list. */
    const EwStore *s = const_store(m);
    for (int slot = 1; slot < EW_MAX_ENTITIES; ++slot)
        if (s->alive[slot]) (void)loaded_append_living(m, s, slot);
    for (int slot = 0; slot < GM_XP_ORBS; ++slot)
        if (!m->xp_orbs[slot].dead && m->xp_orbs[slot].xpValue > 0)
            (void)loaded_append_xp(m, slot);
}

int gm_mobs_loaded_order_count(const GmMobLive *m) {
    return m ? m->loaded_order_count : 0;
}

int gm_mobs_loaded_order_get(
        const GmMobLive *m, int index, int *eid, int *kind) {
    if (!m || index < 0 || index >= m->loaded_order_count) return 0;
    if (eid) *eid = m->loaded_order[index].eid;
    if (kind) *kind = m->loaded_order[index].kind;
    return 1;
}

static double mob_math_random_next_double(uint64_t *seed48);
static void mob_event_append(
    GmMobLive *m, int kind, int eid, int data,
    double x, double y, double z, float volume, float pitch);
static void mob_drop(
    GmMobLive *m, EwStore *s, int slot, GmLiveSim *drops);
static int mob_live_free_item_slots(const GmLiveSim *drops);
static int mob_player_damage_pig_slot_exact(
    GmMobLive *m, EwStore *s, int slot,
    double attacker_x, double attacker_z, float damage,
    GmLiveSim *drops, const GmMobDeathContext *death_context);
static int sheep_graze_begin_slot(
    GmMobLive *m, GmWorld *w, EwStore *s, int slot);
static int sheep_graze_update_slot(
    GmMobLive *m, GmWorld *w, EwStore *s, int slot, int mob_griefing);

static int gm_hostile(int type){return ehs_is_hostile((u8)type);}
static int gm_passive(int type){
    return type==EW_TYPE_SHEEP||type==EW_TYPE_PIG||type==EW_TYPE_COW||type==EW_TYPE_CHICKEN;
}
static int gm_npc(int type){return type==EW_TYPE_VILLAGER;}
static int gm_living(int type){
    return gm_hostile(type)||gm_passive(type)||gm_npc(type)||type==EW_TYPE_BOAT;
}
static int gm_spider(int type){
    return type==EW_TYPE_SPIDER||type==EW_TYPE_CAVE_SPIDER;
}
static int gm_is_slimey(int type){return type==EW_TYPE_SLIME||type==EW_TYPE_MAGMA;}

int gm_mobs_random_sheep_color(JavaRandom *world_random) {
    if (!world_random) return -1;
    int roll = jrand_int_bound(world_random, 100);
    if (roll < 5) return 15; /* BLACK */
    if (roll < 10) return 7; /* GRAY */
    if (roll < 15) return 8; /* SILVER */
    if (roll < 18) return 12; /* BROWN */
    return jrand_int_bound(world_random, 500) == 0 ? 6 : 0;
}

int gm_mobs_sheep_child_color(
        JavaRandom *world_random, int first_fleece, int second_fleece) {
    static const unsigned char mixes[][3] = {
        {14, 0, 6},   /* red + white -> pink */
        {14, 4, 1},   /* red + yellow -> orange */
        {13, 0, 5},   /* green + white -> lime */
        {15, 0, 7},   /* black + white -> gray */
        {7, 0, 8},    /* gray + white -> silver */
        {11, 0, 3},   /* blue + white -> light blue */
        {11, 13, 9},  /* blue + green -> cyan */
        {11, 14, 10}, /* blue + red -> purple */
        {10, 6, 2},   /* purple + pink -> magenta */
    };
    if (!world_random || first_fleece < 0 || first_fleece > 15
            || second_fleece < 0 || second_fleece > 15)
        return -1;
    for (unsigned i = 0; i < sizeof mixes / sizeof mixes[0]; ++i) {
        int a = mixes[i][0], b = mixes[i][1];
        if ((first_fleece == a && second_fleece == b)
                || (first_fleece == b && second_fleece == a))
            return mixes[i][2];
    }
    return jrand_next(world_random, 1) != 0
        ? first_fleece : second_fleece;
}

static int solid_id(int id) {
    if (id == 0) return 0;
    BptProps p = mc_bpt_props(id);
    return (p.flags & BF_SOLID) && !(p.flags & BF_LIQUID);
}

static int collect_blocks(GmWorld *w, const McAABB *q, PcfBlock *out, int cap) {
    int n = 0;
    int x0 = mc_floor(q->minX) - 1, x1 = mc_floor(q->maxX) + 1;
    int y0 = mc_floor(q->minY) - 1, y1 = mc_floor(q->maxY) + 1;
    int z0 = mc_floor(q->minZ) - 1, z1 = mc_floor(q->maxZ) + 1;
    if (y0 < 0) y0 = 0;
    if (y1 > 255) y1 = 255;
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z) {
                int id = gm_world_block(w, x, y, z);
                if (!solid_id(id)) continue;
                if (n == cap) return n;
                out[n].block_id = id;
                out[n].ox = x; out[n].oy = y; out[n].oz = z;
                out[n].ladder_facing = 0;
                ++n;
            }
    return n;
}

static float max_health(int type, int size) {
    if (type == EW_TYPE_ENDERMAN) return 40.0f;
    if (type == EW_TYPE_GHAST) return 10.0f;
    if (type == EW_TYPE_SILVERFISH) return 8.0f;
    if (type == EW_TYPE_CAVE_SPIDER) return 12.0f;
    if (type == EW_TYPE_SHEEP) return 8.0f;
    if (type == EW_TYPE_CHICKEN) return 4.0f;
    if (gm_is_slimey(type)) {
        int s = size > 0 ? size : 2;
        return (float)(s * s);
    }
    if (type == EW_TYPE_SHEEP) return 8.0f;
    if (type == EW_TYPE_CHICKEN) return 4.0f;
    if (type == EW_TYPE_PIG || type == EW_TYPE_COW) return 10.0f;
    if (type == EW_TYPE_BOAT) return 40.0f;
    return 20.0f;
}

/* EntityZombie ATTACK_DAMAGE=3; pigman=5; wither skeleton base 4 + stone sword 4;
 * blaze=6 (EntityBlaze.applyEntityAttributes); silverfish base 1; slime damages
 * when size > 1 for size; magma is size + 2. */
static float melee_damage(int type, int size) {
    if (type == EW_TYPE_ENDERMAN) return 7.0f;
    if (type == EW_TYPE_BLAZE) return 6.0f;
    if (type == EW_TYPE_ZOMBIE) return 3.0f;
    if (type == EW_TYPE_PIGMAN) return 5.0f;
    if (type == EW_TYPE_WITHER_SKELETON) return 8.0f;
    if (type == EW_TYPE_BLAZE) return 6.0f;
    if (type == EW_TYPE_SILVERFISH) return 1.0f;
    if (type == EW_TYPE_CAVE_SPIDER) return 2.0f;
    if (type == EW_TYPE_MAGMA) {
        int s = size > 0 ? size : 1;
        return (float)(s + 2);
    }
    if (type == EW_TYPE_SLIME) {
        int s = size > 0 ? size : 1;
        return s > 1 ? (float)s : 0.0f;
    }
    return 4.0f;
}

static int mob_effect_amplifier(
        const GmMobLive *m, int slot, int potion_id) {
    int count = m->entity_effect_count[slot];
    for (int i = 0; i < count; ++i)
        if (m->entity_effects[slot][i].id == potion_id)
            return m->entity_effects[slot][i].amplifier;
    return -1;
}

static float mob_resistance_damage(
        const GmMobLive *m, int slot, float damage) {
    int amplifier = mob_effect_amplifier(m, slot, 11);
    return amplifier >= 0
        ? pt_effect_resistance_damage(damage, amplifier) : damage;
}

static float mob_absorb_damage(
        GmMobLive *m, int slot, float damage) {
    return pt_effect_absorb_damage(
        damage, &m->entity_absorption[slot]);
}

static float mob_preview_absorbed_damage(
        const GmMobLive *m, int slot, float damage) {
    float absorption = m->entity_absorption[slot];
    return pt_effect_absorb_damage(damage, &absorption);
}

static float mob_max_health(const GmMobLive *m, int slot, int type) {
    float result = max_health(type, m->size[slot]);
    int amplifier = mob_effect_amplifier(m, slot, 21);
    return amplifier >= 0
        ? pt_effect_health_boost(result, amplifier) : result;
}

static double mob_movement_effect_multiplier(
        const GmMobLive *m, int slot) {
    double multiplier = 1.0;
    int count = m->entity_effect_count[slot];
    for (int i = 0; i < count; ++i)
        multiplier *= pt_effect_movement_multiplier(
            m->entity_effects[slot][i].id,
            m->entity_effects[slot][i].amplifier);
    return multiplier;
}

static float mob_melee_damage(
        const GmMobLive *m, int slot, int type, int size) {
    double value = (double)melee_damage(type, size);
    int count = m->entity_effect_count[slot];
    for (int i = 0; i < count; ++i)
        value += pt_effect_attack_bonus(
            m->entity_effects[slot][i].id,
            m->entity_effects[slot][i].amplifier);
    if (value < 0.0) value = 0.0;
    if (value > 2048.0) value = 2048.0;
    return (float)value;
}

static ITAStack mob_ita_stack(ICStack stack)
{
    ITAStack out = ita_mk(stack.item, stack.meta);
    out.count = stack.count;
    for (int i = 0; i < stack.n_enchants; ++i) {
        int level = stack.enchants[i].level;
        switch (stack.enchants[i].id) {
        case 0: out.prot_all = level; break;
        case 1: out.prot_fire = level; break;
        case 2: out.prot_fall = level; break;
        case 3: out.prot_blast = level; break;
        case 4: out.prot_projectile = level; break;
        case 32: out.efficiency = level; break;
        case 34: out.unbreaking = level; break;
        default: break;
        }
    }
    return out;
}

/* Apply armor absorb + durability. Returns residual damage to health and the
 * post-durability EnchantmentHelper protection modifier for this source. */
static float mob_apply_armor(IsrInv *inv, float amount, int bypass_armor,
                             int source_flags, int *protection)
{
    ITAStack slots[4];
    if (protection) *protection = 0;
    if (!inv || bypass_armor || amount <= 0.0f) return amount;
    for (int i = 0; i < 4; ++i) {
        ICStack s = isr_get_stack(inv, ISR_ARMOR0 + i);
        slots[i] = mob_ita_stack(s);
    }
    ita_damage_armor_set(slots, amount);
    for (int i = 0; i < 4; ++i) {
        ICStack stack = isr_get_stack(inv, ISR_ARMOR0 + i);
        if (slots[i].item <= 0 || slots[i].count <= 0)
            isr_set_stack(inv, ISR_ARMOR0 + i, ic_empty());
        else {
            stack.item = slots[i].item;
            stack.count = slots[i].count;
            stack.meta = slots[i].damage;
            isr_set_stack(inv, ISR_ARMOR0 + i, stack);
        }
    }
    if (protection)
        *protection = ita_enchant_prot_modifier(slots, source_flags);
    return ita_apply_armor_absorb(amount, slots, 0);
}

float gm_mobs_anvil_helmet_pre_damage(GmMobLive *m, struct IsrInv *player_inv,
                                      float amount)
{
    IsrInv *inv = (IsrInv *)player_inv;
    ICStack stack;
    ITAStack head;
    int durability;
    if (!m || !inv || amount <= 0.0f) return amount;
    stack = isr_get_stack(inv, ISR_ARMOR0 + 3);
    if (stack.item <= 0 || stack.count <= 0) return amount;
    durability = (int)(amount * 4.0f
        + jrand_float(&m->player_random) * amount * 2.0f);
    head = mob_ita_stack(stack);
    if (ita_attempt_damage(&head, durability, &m->player_random)) {
        --stack.count;
        stack.meta = 0;
        isr_set_stack(inv, ISR_ARMOR0 + 3,
            stack.count > 0 ? stack : ic_empty());
    } else {
        stack.meta = head.damage;
        isr_set_stack(inv, ISR_ARMOR0 + 3, stack);
    }
    return amount * 0.75f;
}

float gm_mobs_player_resistance_damage(const GmMobLive *m, float amount)
{
    if (!m || amount <= 0.0f || m->player_resistance_amplifier < 0)
        return amount;
    int scale = 25 - 5 * (m->player_resistance_amplifier + 1);
    return scale > 0 ? amount * (float)scale / 25.0f : 0.0f;
}

float gm_mobs_player_absorb_damage(GmMobLive *m, float amount)
{
    if (!m || amount <= 0.0f || m->player_absorption <= 0.0f)
        return amount;
    float absorbed = amount < m->player_absorption
        ? amount : m->player_absorption;
    m->player_absorption -= absorbed;
    return amount - absorbed;
}

/* EntityLivingBase.attackEntityFrom hurtResistantTime/lastDamage gate. Returns
 * whether the attack was accepted, as EntityWitherSkeleton.attackEntityAsMob
 * uses that result before adding PotionEffect(WITHER, 200, 0). lastDamage is
 * the RAW pre-armor amount; armor runs in damageEntity after the gate. */
int gm_mobs_attack_player_source(GmMobLive *m, struct PvStats *vitals_,
                                 struct IsrInv *player_inv, float amount,
                                 int bypass_armor, int source_flags) {
    PvStats *v=(PvStats *)vitals_;
    float applied;
    int protection = 0;
    if (!m || !v || amount <= 0.0f) return 0;
    if (m->player_hurt_resistant > 10) {
        if (amount <= m->player_last_damage) return 0;
        applied = amount - m->player_last_damage;
        m->player_last_damage = amount;
    } else {
        applied = amount;
        m->player_last_damage = amount;
        m->player_hurt_resistant = 20;
        m->player_hurt_time = 10;
    }
    applied = mob_apply_armor((IsrInv *)player_inv, applied, bypass_armor,
                              source_flags, &protection);
    applied = gm_mobs_player_resistance_damage(m, applied);
    if (applied > 0.0f && protection > 0)
        applied = ita_damage_after_magic_absorb(
            applied, (float)protection);
    applied = gm_mobs_player_absorb_damage(m, applied);
    if (applied > 0.0f) pv_attack(v, applied);
    return applied > 0.0f ? 2 : 1;
}

int gm_mobs_attack_player(GmMobLive *m, struct PvStats *vitals_,
                          struct IsrInv *player_inv, float amount,
                          int bypass_armor) {
    return gm_mobs_attack_player_source(
        m, vitals_, player_inv, amount, bypass_armor,
        GM_DAMAGE_SOURCE_GENERIC);
}

void gm_mobs_player_hurt_tick(GmMobLive *m) {
    if (!m) return;
    if (m->player_hurt_resistant > 0) --m->player_hurt_resistant;
    if (m->player_hurt_time > 0) --m->player_hurt_time;
}

static double follow_range(int type) {
    if (type == EW_TYPE_ZOMBIE) return 40.0;
    if (type == EW_TYPE_GHAST) return 64.0;
    if (type == EW_TYPE_BLAZE) return 48.0;
    if (type == EW_TYPE_ENDERMAN) return 64.0;
    if (type == EW_TYPE_SKELETON || type == EW_TYPE_WITHER_SKELETON) return 16.0;
    if (gm_spider(type)) return 16.0;
    if (type == EW_TYPE_CREEPER) return 16.0;
    if (type == EW_TYPE_SILVERFISH) return 8.0;
    return 16.0;
}

/* EntityLiving.attackTime / AI attack cooldowns (ticks) after a landed hit
 * or ranged release. Route-roster values from 1.11.2 Entity* classes.
 * Skeleton uses 40 so spawn_hostile_projectiles (reload edge) stays aligned
 * with the arrow emit path. Blaze uses AIFireballAttack (60/6/100), not this. */
static int attack_cooldown_ticks(int type) {
    if (type == EW_TYPE_BLAZE) return 20; /* AIFireballAttack melee branch */
    if (type == EW_TYPE_SKELETON) return 40;
    if (type == EW_TYPE_WITHER_SKELETON) return 20;
    if (type == EW_TYPE_GHAST) return 40;
    if (type == EW_TYPE_SILVERFISH) return 20;
    if (gm_spider(type)) return 20;
    if (type == EW_TYPE_ZOMBIE || type == EW_TYPE_PIGMAN) return 20;
    if (type == EW_TYPE_ENDERMAN) return 20;
    return 20;
}

/* EntityBoat.deltaRotation + land boatGlide (per-slot; not in EwStore). */
static float s_boat_delta_rot[EW_MAX_ENTITIES];
static float s_boat_glide[EW_MAX_ENTITIES];

static void tick_boat(GmMobLive *m, GmWorld *w, EwStore *nx, int i,
                      PsvPlayer *p, int ox, int oz, float forward,
                      float strafe, int no_gravity);

static i64 mob_entity_constructor_seed(
        const GmMobLive *m, const EwStore *s, int slot) {
    u64 key = (u64)m->seed
        ^ (u64)(u32)s->id[slot] * UINT64_C(0x9E3779B97F4A7C15)
        ^ (u64)s->type[slot] * UINT64_C(0xD1B54A32D192ED03);
    return (i64)mc_hash64(key);
}

static void reset_slot_state_s(GmMobLive *m, EwStore *s, int slot) {
    if (slot < 0 || slot >= EW_MAX_ENTITIES) return;
    if (s) s->repath_timer[slot] = GM_MOB_WANDER_INTERVAL;
    m->creeper_fuse[slot] = 0;
    m->hurt_aggro[slot] = 0;
    m->panic_ticks[slot] = 0;
    m->passive_tasks[slot] = 0;
    m->passive_task_tick[slot] = 0;
    m->passive_watch_time[slot] = 0;
    m->passive_idle_time[slot] = 0;
    m->passive_eat_time[slot] = 0;
    m->passive_idle_x[slot] = 0.0;
    m->passive_idle_z[slot] = 0.0;
    m->passive_nav_speed[slot] = 0.0;
    m->passive_head_yaw[slot] = s ? s->yaw[slot] : 0.0f;
    m->passive_head_pitch[slot] = 0.0f;
    m->passive_sheared[slot] = 0;
    m->fire_ticks[slot] = 0;
    m->despawn_ticks[slot] = 0;
    m->anger[slot] = 0;
    m->blaze_attack_step[slot] = 0;
    m->blaze_attacking[slot] = 0;
    m->blaze_charged[slot] = 0;
    m->blaze_shots_pending[slot] = 0;
    m->blaze_shot_head[slot] = 0;
    memset(m->blaze_shots[slot], 0, sizeof m->blaze_shots[slot]);
    m->blaze_height_offset_update_time[slot] = 0;
    m->blaze_height_offset[slot] = 0.5F;
    m->sheep_data[slot] = 0;
    m->villager_profession[slot] = 0;
    m->pig_saddled[slot] = 0;
    m->pig_boosting[slot] = 0;
    m->pig_boost_time[slot] = 0;
    m->pig_boost_total[slot] = 0;
    m->pig_pitch[slot] = 0.0F;
    m->pig_prev_yaw[slot] = 0.0F;
    m->pig_render_yaw[slot] = 0.0F;
    m->pig_head_yaw[slot] = 0.0F;
    m->pig_step_height[slot] = 0.6F;
    m->pig_jump_factor[slot] = 0.02F;
    m->pig_ai_speed[slot] = 0.0F;
    m->pig_prev_limb_amount[slot] = 0.0F;
    m->pig_limb_amount[slot] = 0.0F;
    m->pig_limb_swing[slot] = 0.0F;
    m->growing_age[slot] = 0;
    m->chicken_time_until_next_egg[slot] = 0;
    m->chicken_wing_rotation[slot] = 0.0F;
    m->chicken_dest_pos[slot] = 0.0F;
    m->chicken_old_flap_speed[slot] = 0.0F;
    m->chicken_old_flap[slot] = 0.0F;
    m->chicken_wing_rot_delta[slot] = 1.0F;
    m->chicken_jockey[slot] = 0;
    m->sheep_in_love[slot] = 0;
    m->sheep_forced_age[slot] = 0;
    m->sheep_forced_age_timer[slot] = 0;
    m->sheep_bred_by_player[slot] = 0;
    m->sheep_mate_slot[slot] = -1;
    m->sheep_mate_delay[slot] = 0;
    m->sheep_mate_active[slot] = 0;
    m->sheep_eat_timer[slot] = 0;
    m->sheep_ai_tick_count[slot] = 0;
    m->sheep_world_event_pending[slot] = 0;
    m->sheep_world_event_x[slot] = 0;
    m->sheep_world_event_y[slot] = 0;
    m->sheep_world_event_z[slot] = 0;
    m->sheep_world_event_data[slot] = 0;
    if (s) {
        ebf_entity_random_init(
            &m->entity_random[slot], mob_entity_constructor_seed(m, s, slot));
        ebf_entity_random_init(
            &m->entity_server_random[slot],
            mob_entity_constructor_seed(m, s, slot));
        if (s->type[slot] == EW_TYPE_CHICKEN)
            m->chicken_time_until_next_egg[slot] =
                jrand_int_bound(&m->entity_random[slot].random, 6000) + 6000;
    }
    m->entity_ticks_existed[slot] = 0;
    m->entity_age[slot] = 0;
    m->entity_living_sound_time[slot] = 0;
    m->entity_server_living_sound_time[slot] = 0;
    m->entity_last_tick_x[slot] = s ? s->x[slot] : 0.0;
    m->entity_last_tick_y[slot] = s ? s->y[slot] : 0.0;
    m->entity_last_tick_z[slot] = s ? s->z[slot] : 0.0;
    m->entity_prev_x[slot] = s ? s->x[slot] : 0.0;
    m->entity_prev_y[slot] = s ? s->y[slot] : 0.0;
    m->entity_prev_z[slot] = s ? s->z[slot] : 0.0;
    m->entity_box_valid[slot] = 0;
    m->entity_fall_distance[slot] = 0.0F;
    m->entity_server_fall_distance[slot] = 0.0F;
    m->entity_collided_horizontal[slot] = 0;
    m->entity_collided_vertical[slot] = 0;
    m->entity_in_water[slot] = 0;
    m->entity_in_lava[slot] = 0;
    m->entity_server_in_water[slot] = 0;
    m->entity_server_in_lava[slot] = 0;
    m->entity_server_fire_resistance_ticks[slot] = 0;
    m->entity_in_web[slot] = 0;
    m->jump_delay[slot] = 0;
    m->charge[slot] = 0;
    m->blaze_on_fire[slot] = 0;
    m->boat_damage[slot] = 0.0F;
    m->controlled_no_ai[slot] = 0;
    m->controlled_block_collisions[slot] = 0;
    m->entity_hurt_resistant[slot] = 0;
    m->entity_hurt_time[slot] = 0;
    m->entity_death_time[slot] = 0;
    m->entity_dead[slot] = 0;
    m->entity_last_damage[slot] = 0.0f;
    m->entity_absorption[slot] = 0.0F;
    m->entity_air[slot] = 300;
    m->entity_recently_hit[slot] = 0;
    m->entity_attacking_player[slot] = 0;
    m->entity_effect_count[slot] = 0;
    m->entity_fire_resistance_this_tick[slot] = 0;
    m->creeper_powered[slot] = 0;
    memset(m->entity_effects[slot], 0, sizeof m->entity_effects[slot]);
    s_boat_delta_rot[slot] = 0.0f;
    s_boat_glide[slot] = 0.8f;
    if (!m->size[slot]) m->size[slot] = gm_is_slimey(s ? s->type[slot] : 0) ? 2 : 1;
}

/* Chunk.getRandomWithSeed(987234911).nextInt(10)==0 slime-chunk test. */
static int is_slime_chunk(long long world_seed, int cx, int cz) {
    i64 seed = (i64)world_seed
        + (i64)cx * (i64)cx * 4987142LL
        + (i64)cx * 5947611LL
        + (i64)cz * (i64)cz * 4392871LL
        + (i64)cz * 389711LL;
    seed ^= 987234911LL;
    u64 s = (u64)(seed ^ 0x5DEECE66DLL) & ((1ULL << 48) - 1ULL);
    for (;;) {
        s = (s * 0x5DEECE66DULL + 0xBULL) & ((1ULL << 48) - 1ULL);
        int bits = (int)(s >> 17);
        int val = bits % 10;
        if (bits - val + 9 >= 0) return val == 0;
    }
}

static void mark_hurt(GmMobLive *m, EwStore *s, int slot) {
    m->hurt_aggro[slot] = 1;
    if (gm_passive(s->type[slot])) {
        m->sheep_in_love[slot] = 0;
        m->sheep_bred_by_player[slot] = 0;
        m->panic_ticks[slot] = GM_MOB_REVENGE_TICKS;
    }
    /* EntityPigZombie.becomeAngryAt + AIHurtByAggressor group help. */
    if (s->type[slot] == EW_TYPE_PIGMAN) {
        u64 h = mc_hash_seed((u64)m->seed, m->tick, slot, s->id[slot], 0, 0x414E4752u);
        m->anger[slot] = GM_PIGMAN_ANGER_BASE + (int)mc_hash_bound(h, GM_PIGMAN_ANGER_RANGE);
        for (int j = 1; j < EW_MAX_ENTITIES; ++j) {
            if (j == slot || !s->alive[j] || s->type[j] != EW_TYPE_PIGMAN) continue;
            if (m->entity_dimension[j] != m->entity_dimension[slot]) continue;
            double dx = s->x[j] - s->x[slot], dy = s->y[j] - s->y[slot], dz = s->z[j] - s->z[slot];
            if (dx * dx + dy * dy + dz * dz > GM_PIGMAN_HELP_RANGE * GM_PIGMAN_HELP_RANGE) continue;
            u64 h2 = mc_hash_seed((u64)m->seed, m->tick, j, s->id[j], 0, 0x414E4752u);
            m->anger[j] = GM_PIGMAN_ANGER_BASE + (int)mc_hash_bound(h2, GM_PIGMAN_ANGER_RANGE);
            m->hurt_aggro[j] = 1;
        }
    }
}

static int mob_effect_magic_damage(
        GmMobLive *m, EwStore *s, int slot, float damage) {
    float applied;
    if (damage <= 0.0F || s->health[slot] <= 0.0F
            || m->entity_dead[slot])
        return 0;
    if (m->entity_hurt_resistant[slot] > 10) {
        if (damage <= m->entity_last_damage[slot]) return 0;
        applied = damage - m->entity_last_damage[slot];
        m->entity_last_damage[slot] = damage;
    } else {
        applied = damage;
        m->entity_last_damage[slot] = damage;
        m->entity_hurt_resistant[slot] = 20;
        m->entity_hurt_time[slot] = 10;
    }
    applied = mob_resistance_damage(m, slot, applied);
    applied = mob_absorb_damage(m, slot, applied);
    s->health[slot] -= applied;
    if (s->health[slot] < 0.0F) s->health[slot] = 0.0F;
    mark_hurt(m, s, slot);
    return 1;
}

static float mob_eye_height(
        const GmMobLive *m, const EwStore *s, int slot) {
    float width, height;
    ehs_size_scaled(s->type[slot], m->size[slot], &width, &height);
    (void)width;
    switch (s->type[slot]) {
    case EW_TYPE_ZOMBIE:
    case EW_TYPE_PIGMAN:
    case EW_TYPE_SKELETON:
    case EW_TYPE_WITHER_SKELETON:
        return 1.74F;
    case EW_TYPE_SPIDER: return 0.65F;
    case EW_TYPE_CAVE_SPIDER: return 0.45F;
    case EW_TYPE_ENDERMAN: return 2.55F;
    case EW_TYPE_GHAST: return 2.6F;
    case EW_TYPE_SILVERFISH: return 0.1F;
    case EW_TYPE_SLIME:
    case EW_TYPE_MAGMA:
        return 0.625F * height;
    case EW_TYPE_SHEEP: return 0.95F * height;
    case EW_TYPE_COW: return m->growing_age[slot] < 0 ? height : 1.3F;
    case EW_TYPE_CHICKEN: return height;
    case EW_TYPE_VILLAGER: return 1.62F;
    default: return height * 0.85F;
    }
}

/* EntityLivingBase.onEntityUpdate air branch. This precedes combat-timer and
 * potion-duration aging. Bubble particles are not rendered here, but their
 * 48 Entity.rand.nextFloat calls are observable by later mob behavior. */
static int tick_mob_air(
        GmMobLive *m, GmWorld *w, EwStore *s, int slot,
        GmLiveSim *drops) {
    int drown_pulse;
    if (s->health[slot] <= 0.0F || m->entity_dead[slot]) return 0;
    m->entity_air[slot] = pt_effect_air_step(
        m->entity_air[slot],
        gm_uw_eye_inside_water(
            w, s->x[slot], s->y[slot], s->z[slot],
            mob_eye_height(m, s, slot)),
        mob_effect_amplifier(m, slot, 13) >= 0, &drown_pulse);
    if (!drown_pulse) return 0;
    for (int draw = 0; draw < 48; ++draw)
        (void)jrand_float(&m->entity_random[slot].random);
    (void)mob_effect_magic_damage(m, s, slot, 2.0F);
    if (s->health[slot] > 0.0F) return 0;
    if (m->controlled_no_ai[slot]) {
        m->entity_dead[slot] = 1;
        return 0;
    }
    mob_drop(m, s, slot, drops);
    return 1;
}

/* EntityLivingBase.updatePotionEffects. Attribute-only effects remain in the
 * same store and age exactly; the promoted periodic brewing effects execute
 * before duration decrement. */
static void tick_mob_potion_effects(
        GmMobLive *m, EwStore *s, int slot, GmLiveSim *drops) {
    int index = 0;
    m->entity_fire_resistance_this_tick[slot] = 0;
    while (index < m->entity_effect_count[slot]) {
        PtMobEffect *effect = &m->entity_effects[slot][index];
        if (effect->id == 12 && effect->duration > 0)
            m->entity_fire_resistance_this_tick[slot] = 1;
        if (effect->duration > 0
                && pt_effect_is_ready(
                    effect->id, effect->duration, effect->amplifier)) {
            if (effect->id == 10 && s->health[slot] > 0.0F) {
                float limit = mob_max_health(m, slot, s->type[slot]);
                if (s->health[slot] < limit) {
                    s->health[slot] += 1.0F;
                    if (s->health[slot] > limit) s->health[slot] = limit;
                }
            } else if (effect->id == 19 && s->health[slot] > 1.0F) {
                (void)mob_effect_magic_damage(m, s, slot, 1.0F);
            } else if (effect->id == 20 && s->health[slot] > 0.0F) {
                (void)mob_effect_magic_damage(m, s, slot, 1.0F);
                if (s->health[slot] <= 0.0F) {
                    if (m->controlled_no_ai[slot]) {
                        m->entity_dead[slot] = 1;
                    } else {
                        mob_drop(m, s, slot, drops);
                        return;
                    }
                }
            }
        }
        --effect->duration;
        if (effect->duration <= 0) {
            int removed_id = effect->id;
            int removed_amplifier = effect->amplifier;
            int remaining = m->entity_effect_count[slot] - index - 1;
            if (remaining > 0)
                memmove(effect, effect + 1,
                        (size_t)remaining * sizeof *effect);
            --m->entity_effect_count[slot];
            memset(&m->entity_effects[slot]
                       [m->entity_effect_count[slot]],
                   0, sizeof m->entity_effects[slot][0]);
            if (removed_id == 21) {
                float limit = mob_max_health(m, slot, s->type[slot]);
                if (s->health[slot] > limit) s->health[slot] = limit;
            } else if (removed_id == 22) {
                m->entity_absorption[slot] -=
                    pt_effect_health_boost(0.0F, removed_amplifier);
                if (m->entity_absorption[slot] < 0.0F)
                    m->entity_absorption[slot] = 0.0F;
            }
            continue;
        }
        ++index;
    }
}

static int los_clear(GmWorld *w, double x0, double y0, double z0,
                     double x1, double y1, double z1) {
    double dx = x1 - x0, dy = y1 - y0, dz = z1 - z0;
    double d = sqrt(dx * dx + dy * dy + dz * dz);
    int steps = (int)(d * 2.0);
    if (steps < 1) return 1;
    if (steps > 96) steps = 96;
    for (int s = 1; s < steps; ++s) {
        double t = (double)s / (double)steps;
        if (solid_id(gm_world_block(w, mc_floor(x0 + dx * t),
                                    mc_floor(y0 + dy * t),
                                    mc_floor(z0 + dz * t)))) return 0;
    }
    return 1;
}

static int wander_ground_y(GmWorld *w, int x, int y0, int z) {
    for (int yy = y0 + 1; yy >= y0 - 3; --yy) {
        if (yy < 1) break;
        if (solid_id(gm_world_block(w, x, yy - 1, z)) &&
            !solid_id(gm_world_block(w, x, yy, z)) &&
            !solid_id(gm_world_block(w, x, yy + 1, z))) return yy;
    }
    return -1000;
}

/* Passive AI is a direct port of the 1.11.2 EntityAITasks goal lists. Mate,
 * tempt, and follow-parent are deliberately absent from the runnable set:
 * live animals have no growingAge/inLove state and breeding items/interact are
 * a product cut, so each oracle shouldExecute is currently always false. */
enum {
    PAI_SWIM = 0,
    PAI_PANIC,
    PAI_EAT,
    PAI_WANDER,
    PAI_WATCH,
    PAI_IDLE,
    PAI_NTASKS
};

#define PAI_BIT(t) (1u << (t))
#define PAI_RNG 0x50414952u

static int pai_priority(int type, int task) {
    if (task == PAI_SWIM) return 0;
    if (task == PAI_PANIC) return 1;
    if (type == EW_TYPE_SHEEP) {
        if (task == PAI_EAT) return 5;
        if (task == PAI_WANDER) return 6;
        if (task == PAI_WATCH) return 7;
        if (task == PAI_IDLE) return 8;
    } else if (type == EW_TYPE_PIG) {
        if (task == PAI_WANDER) return 6;
        if (task == PAI_WATCH) return 7;
        if (task == PAI_IDLE) return 8;
    } else if (type == EW_TYPE_COW || type == EW_TYPE_CHICKEN) {
        if (task == PAI_WANDER) return 5;
        if (task == PAI_WATCH) return 6;
        if (task == PAI_IDLE) return 7;
    }
    return 99;
}

static int pai_mutex(int task) {
    if (task == PAI_SWIM) return 4;
    if (task == PAI_PANIC || task == PAI_WANDER) return 1;
    if (task == PAI_WATCH) return 2;
    if (task == PAI_IDLE) return 3;
    if (task == PAI_EAT) return 7;
    return 0;
}

static double pai_attribute_speed(int type) {
    if (type == EW_TYPE_SHEEP) return 0.23000000417232513;
    if (type == EW_TYPE_PIG || type == EW_TYPE_CHICKEN) return 0.25;
    if (type == EW_TYPE_COW) return 0.20000000298023224;
    return 0.23000000417232513;
}

static double pai_panic_multiplier(int type) {
    if (type == EW_TYPE_COW) return 2.0;
    if (type == EW_TYPE_CHICKEN) return 1.4;
    return 1.25; /* sheep, pig */
}

static void pai_size(int type, float *width, float *height) {
    *width = 0.9f;
    if (type == EW_TYPE_SHEEP) *height = 1.3f;
    else if (type == EW_TYPE_PIG) *height = 0.9f;
    else if (type == EW_TYPE_COW) *height = 1.4f;
    else { *width = 0.4f; *height = 0.7f; }
}

static double pai_eye_height(int type) {
    float width, height;
    pai_size(type, &width, &height);
    (void)width;
    if (type == EW_TYPE_SHEEP) return (double)(0.95f * height);
    if (type == EW_TYPE_COW) return 1.3;
    if (type == EW_TYPE_CHICKEN) return (double)height;
    return (double)(height * 0.85f);
}

static u64 pai_rng_start(const GmMobLive *m, const EwStore *s, int i, int task) {
    return mc_hash_seed((u64)m->seed, m->tick, i, s->id[i], task, PAI_RNG);
}

static u64 pai_rng_next(u64 *stream) {
    *stream = mc_hash64(*stream + 0x9E3779B97F4A7C15ULL);
    return *stream;
}

static int pai_rng_bound(u64 *stream, int bound) {
    return mc_hash_bound(pai_rng_next(stream), bound);
}

static float pai_rng_float(u64 *stream) {
    return mc_hash_f01(pai_rng_next(stream));
}

/* java.util.Random.nextDouble consumes 26 then 27 bits. The runtime uses the
 * mandated hash stream, but preserves that two-draw shape. */
static double pai_rng_double(u64 *stream) {
    u64 a = pai_rng_next(stream), b = pai_rng_next(stream);
    u64 hi = (u64)(mc_hash_u32(a) >> 6);
    u64 lo = (u64)(mc_hash_u32(b) >> 5);
    return (double)((hi << 27) + lo) * (1.0 / 9007199254740992.0);
}

static int pai_in_material(GmWorld *w, const EwStore *s, int i, int lava) {
    float width, height;
    pai_size(s->type[i], &width, &height);
    double inset = lava ? 0.10000000149011612 : 0.001;
    int x0 = mc_floor(s->x[i] - width * 0.5 + inset);
    int x1 = mc_floor(s->x[i] + width * 0.5 - inset);
    int z0 = mc_floor(s->z[i] - width * 0.5 + inset);
    int z1 = mc_floor(s->z[i] + width * 0.5 - inset);
    int y0 = mc_floor(s->y[i] - 0.4000000059604645 + (lava ? 0.0 : 0.001));
    int y1 = mc_floor(s->y[i] + height - (lava ? 0.0 : 0.001));
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z) {
                int id = gm_world_block(w, x, y, z);
                if ((!lava && (id == 8 || id == 9)) ||
                    (lava && (id == 10 || id == 11))) return 1;
            }
    return 0;
}

/* EntityAIPanic.getRandPos: burning creatures choose the closest water block
 * in the inclusive 5 x 4 x 5 search before RandomPositionGenerator. */
static int pai_nearest_water(GmWorld *w, const EwStore *s, int i,
                             double *out_x, double *out_y, double *out_z) {
    int ox = mc_floor(s->x[i]), oy = mc_floor(s->y[i]), oz = mc_floor(s->z[i]);
    float best = 5.0f * 5.0f * 4.0f * 2.0f;
    int found = 0;
    for (int x = ox - 5; x <= ox + 5; ++x) {
        for (int y = oy - 4; y <= oy + 4; ++y) {
            for (int z = oz - 5; z <= oz + 5; ++z) {
                int id = gm_world_block(w, x, y, z);
                if (id != 8 && id != 9) continue;
                float dx = (float)(x - ox), dy = (float)(y - oy), dz = (float)(z - oz);
                float dist = dx * dx + dy * dy + dz * dz;
                if (dist < best) {
                    best = dist;
                    *out_x = x; *out_y = y; *out_z = z;
                    found = 1;
                }
            }
        }
    }
    return found;
}

static float pai_brightness(GmWorld *w, int x, int y, int z) {
    int light = gm_world_sky_light(w, x, y, z);
    int block = gm_world_block_light(w, x, y, z);
    if (block > light) light = block;
    if (light < 0) light = 0;
    if (light > 15) light = 15;
    float f1 = 1.0f - (float)light / 15.0f;
    return (1.0f - f1) / (f1 * 3.0f + 1.0f);
}

/* RandomPositionGenerator.generateRandomPos: ten triples, strict-greater best
 * weight, animal grass preference, and getLandPos water rejection. Returned
 * coordinates intentionally use the original offsets, as the Java method
 * does even after moveAboveSolid is used only for validation/scoring. */
static int pai_random_position(GmWorld *w, const EwStore *s, int i,
                               int xz, int yrange, int land, u64 *stream,
                               double *out_x, double *out_y, double *out_z) {
    int found = 0, best_dx = 0, best_dy = 0, best_dz = 0;
    float best = -99999.0f;
    for (int k = 0; k < 10; ++k) {
        int dx = pai_rng_bound(stream, 2 * xz + 1) - xz;
        int dy = pai_rng_bound(stream, 2 * yrange + 1) - yrange;
        int dz = pai_rng_bound(stream, 2 * xz + 1) - xz;
        int bx = mc_floor(s->x[i] + dx);
        int by = mc_floor(s->y[i] + dy);
        int bz = mc_floor(s->z[i] + dz);
        if (by <= 0 || !solid_id(gm_world_block(w, bx, by - 1, bz))) continue;
        int score_y = by;
        if (land && solid_id(gm_world_block(w, bx, score_y, bz))) {
            while (score_y < 256 && solid_id(gm_world_block(w, bx, score_y, bz)))
                ++score_y;
        }
        if (land) {
            int id = gm_world_block(w, bx, score_y, bz);
            if (id == 8 || id == 9) continue;
        }
        float score = gm_world_block(w, bx, score_y - 1, bz) == 2
                    ? 10.0f : pai_brightness(w, bx, score_y, bz) - 0.5f;
        if (score > best) {
            best = score;
            best_dx = dx; best_dy = dy; best_dz = dz;
            found = 1;
        }
    }
    if (!found) return 0;
    *out_x = s->x[i] + best_dx;
    *out_y = s->y[i] + best_dy;
    *out_z = s->z[i] + best_dz;
    return 1;
}

static void pai_set_path(GmMobLive *m, GmWorld *w, EwStore *s, int i,
                         double x, double y, double z, double speed) {
    int bx = mc_floor(x), by = mc_floor(y), bz = mc_floor(z);
    s->path_tx[i] = x; s->path_ty[i] = y; s->path_tz[i] = z;
    /* PathNavigate.tryMoveToXYZ returns false for an unreachable/solid goal.
     * The live navigator remains direct, but it applies the same goal-cell
     * standability gate before exposing a MOVE_TO intent. */
    s->path_len[i] = by > 0 && solid_id(gm_world_block(w, bx, by - 1, bz)) &&
                      !solid_id(gm_world_block(w, bx, by, bz)) &&
                      !solid_id(gm_world_block(w, bx, by + 1, bz));
    m->passive_nav_speed[i] = speed;
}

static int pai_path_done(GmWorld *w, const EwStore *s, int i) {
    float width, height;
    pai_size(s->type[i], &width, &height);
    (void)height;
    float waypoint = width > 0.75f ? width * 0.5f : 0.75f - width * 0.5f;
    if (fabs(s->x[i] - s->path_tx[i]) < waypoint &&
        fabs(s->z[i] - s->path_tz[i]) < waypoint &&
        fabs(s->y[i] - s->path_ty[i]) < 1.0) return 1;
    int bx = mc_floor(s->path_tx[i]), by = mc_floor(s->path_ty[i]);
    int bz = mc_floor(s->path_tz[i]);
    return !solid_id(gm_world_block(w, bx, by - 1, bz)) ||
           solid_id(gm_world_block(w, bx, by, bz));
}

static float pai_wrap_degrees(float v) {
    v = fmodf(v, 360.0f);
    if (v >= 180.0f) v -= 360.0f;
    if (v < -180.0f) v += 360.0f;
    return v;
}

static float pai_update_rotation(float current, float target, float max_delta) {
    float d = pai_wrap_degrees(target - current);
    if (d > max_delta) d = max_delta;
    if (d < -max_delta) d = -max_delta;
    return current + d;
}

static int pai_can_use(const GmMobLive *m, int type, int i, int task) {
    int pri = pai_priority(type, task), mutex = pai_mutex(task);
    for (int other = 0; other < PAI_NTASKS; ++other) {
        if (other == task || !(m->passive_tasks[i] & PAI_BIT(other))) continue;
        if (pri >= pai_priority(type, other) && (mutex & pai_mutex(other))) return 0;
        /* All vanilla tasks in these four lists are interruptible. */
    }
    return 1;
}

static int pai_continue(const GmMobLive *m, GmWorld *w, const EwStore *s, int i,
                        int task, double px, double py, double pz) {
    if (task == PAI_SWIM) return pai_in_material(w, s, i, 0) || pai_in_material(w, s, i, 1);
    if (task == PAI_PANIC || task == PAI_WANDER) return s->path_len[i] != 0;
    if (task == PAI_EAT) return m->passive_eat_time[i] > 0;
    if (task == PAI_WATCH) {
        double dx = px - s->x[i], dy = py - s->y[i], dz = pz - s->z[i];
        return dx * dx + dy * dy + dz * dz <= 36.0 && m->passive_watch_time[i] > 0;
    }
    if (task == PAI_IDLE) return m->passive_idle_time[i] >= 0;
    return 0;
}

static void pai_reset(GmMobLive *m, int i, int task) {
    m->passive_tasks[i] &= ~PAI_BIT(task);
    if (task == PAI_EAT) m->passive_eat_time[i] = 0;
    if (task == PAI_WATCH) m->passive_watch_time[i] = 0;
}

static int pai_try_start(GmMobLive *m, GmWorld *w, EwStore *s, int i, int task,
                         double px, double py, double pz) {
    u64 stream = pai_rng_start(m, s, i, task);
    double x, y, z;
    if (task == PAI_SWIM) {
        if (!pai_in_material(w, s, i, 0) && !pai_in_material(w, s, i, 1)) return 0;
    } else if (task == PAI_PANIC) {
        int burning = m->fire_ticks[i] > 0;
        if (m->panic_ticks[i] <= 0 && !burning) return 0;
        int found = burning && pai_nearest_water(w, s, i, &x, &y, &z);
        if (!found)
            found = pai_random_position(w, s, i, 5, 4, 0, &stream, &x, &y, &z);
        if (!found) return 0;
        pai_set_path(m, w, s, i, x, y, z, pai_panic_multiplier(s->type[i]));
    } else if (task == PAI_EAT) {
        if (pai_rng_bound(&stream, 1000) != 0) return 0;
        int bx = mc_floor(s->x[i]), by = mc_floor(s->y[i]), bz = mc_floor(s->z[i]);
        int tall_grass = gm_world_block(w, bx, by, bz) == 31 &&
                         gm_world_meta(w, bx, by, bz) == 1;
        if (!tall_grass && gm_world_block(w, bx, by - 1, bz) != 2) return 0;
        m->passive_eat_time[i] = 40;
        s->path_len[i] = 0;
    } else if (task == PAI_WANDER) {
        if (pai_rng_bound(&stream, 120) != 0) return 0;
        int ok;
        if (pai_in_material(w, s, i, 0)) {
            ok = pai_random_position(w, s, i, 15, 7, 1, &stream, &x, &y, &z);
            if (!ok) ok = pai_random_position(w, s, i, 10, 7, 0, &stream, &x, &y, &z);
        } else {
            int land = pai_rng_float(&stream) >= 0.001f;
            ok = pai_random_position(w, s, i, 10, 7, land, &stream, &x, &y, &z);
        }
        if (!ok) return 0;
        pai_set_path(m, w, s, i, x, y, z, 1.0);
    } else if (task == PAI_WATCH) {
        if (pai_rng_float(&stream) >= 0.02f) return 0;
        double dx = px - s->x[i], dy = py - s->y[i], dz = pz - s->z[i];
        if (dx * dx + dy * dy + dz * dz > 36.0) return 0;
        m->passive_watch_time[i] = 40 + pai_rng_bound(&stream, 40);
    } else if (task == PAI_IDLE) {
        if (pai_rng_float(&stream) >= 0.02f) return 0;
        double angle = 2.0 * MC_PI * pai_rng_double(&stream);
        m->passive_idle_x[i] = cos(angle);
        m->passive_idle_z[i] = sin(angle);
        m->passive_idle_time[i] = 20 + pai_rng_bound(&stream, 20);
    } else return 0;
    m->passive_tasks[i] |= PAI_BIT(task);
    return 1;
}

static void pai_look_update(GmMobLive *m, const EwStore *s, int i, int looking,
                            double look_x, double look_y, double look_z) {
    float pitch = 0.0f;
    float head = m->passive_head_yaw[i];
    if (looking) {
        double dx = look_x - s->x[i];
        double dy = look_y - (s->y[i] + pai_eye_height(s->type[i]));
        double dz = look_z - s->z[i];
        double horiz = sqrt(dx * dx + dz * dz);
        float target_yaw = (float)(atan2(dz, dx) * (180.0 / MC_PI)) - 90.0f;
        float target_pitch = (float)(-(atan2(dy, horiz) * (180.0 / MC_PI)));
        pitch = pai_update_rotation(0.0f, target_pitch, 40.0f);
        head = pai_update_rotation(head, target_yaw, 10.0f);
    } else {
        head = pai_update_rotation(head, s->yaw[i], 10.0f);
    }
    if (s->path_len[i]) {
        float rel = pai_wrap_degrees(head - s->yaw[i]);
        if (rel < -75.0f) head = s->yaw[i] - 75.0f;
        if (rel > 75.0f) head = s->yaw[i] + 75.0f;
    }
    m->passive_head_yaw[i] = head;
    m->passive_head_pitch[i] = pitch;
}

static void pai_apply_current_look(GmMobLive *m, const EwStore *s, int i,
                                   double px, double py, double pz) {
    if (m->passive_tasks[i] & PAI_BIT(PAI_WATCH)) {
        pai_look_update(m, s, i, 1, px, py + PSV_EYE_HEIGHT, pz);
    } else if (m->passive_tasks[i] & PAI_BIT(PAI_IDLE)) {
        pai_look_update(m, s, i, 1,
                        s->x[i] + m->passive_idle_x[i],
                        s->y[i] + pai_eye_height(s->type[i]),
                        s->z[i] + m->passive_idle_z[i]);
    } else {
        pai_look_update(m, s, i, 0, 0.0, 0.0, 0.0);
    }
}

/* Returns movement/jump intents after an EntityAITasks.onUpdateTasks pass. */
static void pai_tick(GmMobLive *m, GmWorld *w, EwStore *s, int i,
                     double px, double py, double pz, int mob_griefing,
                     int *moving, int *jump, int *wandering, int *swim_jump,
                     double *nav_speed) {
    int type = s->type[i];
    if (s->path_len[i] && pai_path_done(w, s, i)) s->path_len[i] = 0;
    int setup = (m->passive_task_tick[i]++ % 3) == 0;
    for (int task = 0; task < PAI_NTASKS; ++task) {
        if (pai_priority(type, task) >= 99) continue;
        int using_task = (m->passive_tasks[i] & PAI_BIT(task)) != 0;
        if (setup) {
            if (using_task) {
                if (!pai_can_use(m, type, i, task) ||
                    !pai_continue(m, w, s, i, task, px, py, pz))
                    pai_reset(m, i, task);
            } else if (pai_can_use(m, type, i, task)) {
                (void)pai_try_start(m, w, s, i, task, px, py, pz);
            }
        } else if (using_task && !pai_continue(m, w, s, i, task, px, py, pz)) {
            pai_reset(m, i, task);
        }
    }

    for (int task = 0; task < PAI_NTASKS; ++task) {
        if (!(m->passive_tasks[i] & PAI_BIT(task))) continue;
        if (task == PAI_SWIM) {
            u64 stream = pai_rng_start(m, s, i, PAI_SWIM + 16);
            if (pai_rng_float(&stream) < 0.8f) *swim_jump = 1;
        } else if (task == PAI_EAT) {
            if (m->passive_eat_time[i] > 0) --m->passive_eat_time[i];
            if (m->passive_eat_time[i] == 4) {
                int bx = mc_floor(s->x[i]), by = mc_floor(s->y[i]), bz = mc_floor(s->z[i]);
                if (gm_world_block(w, bx, by, bz) == 31 &&
                    gm_world_meta(w, bx, by, bz) == 1) {
                    if (mob_griefing) gm_world_set_block(w, bx, by, bz, 0);
                    m->passive_sheared[i] = 0; /* EntitySheep.eatGrassBonus */
                } else if (gm_world_block(w, bx, by - 1, bz) == 2) {
                    if (mob_griefing) gm_world_set_block(w, bx, by - 1, bz, 3);
                    m->passive_sheared[i] = 0;
                }
            }
        } else if (task == PAI_WATCH) {
            --m->passive_watch_time[i];
        } else if (task == PAI_IDLE) {
            --m->passive_idle_time[i];
        }
    }

    *moving = s->path_len[i] != 0;
    *wandering = *moving && (m->passive_tasks[i] & PAI_BIT(PAI_WANDER));
    *jump = 0;
    *nav_speed = *moving ? m->passive_nav_speed[i] : 0.0;
    s->ai_state[i] = EW_AI_IDLE;
    if (m->panic_ticks[i] > 0) --m->panic_ticks[i];
}

static int sky_exposed(GmWorld *w, double x, double y, double z) {
    int bx = mc_floor(x), bz = mc_floor(z);
    int by = mc_floor(y + 1.8);
    int feet = gm_world_block(w, bx, mc_floor(y), bz);
    if (feet && (mc_bpt_props(feet).flags & BF_LIQUID)) return 0;
    for (int yy = by; yy < 256; ++yy)
        if (solid_id(gm_world_block(w, bx, yy, bz))) return 0;
    return 1;
}

static int collect_block_contact_cells(
        const struct Chunk *window, const McAABB *query, int ox, int oz,
        int (*out)[4], int capacity) {
    int count = 0;
    int x0 = mc_floor(query->minX), x1 = mc_floor(query->maxX);
    int y0 = mc_floor(query->minY), y1 = mc_floor(query->maxY);
    int z0 = mc_floor(query->minZ), z1 = mc_floor(query->maxZ);
    if (y0 < 0) y0 = 0;
    if (y1 > 255) y1 = 255;
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z) {
                int id = psv_get_block(
                    (const Chunk *)window, x - ox, y, z - oz);
                if (id != 30 && id != 65 && id != 88 && id != 165)
                    continue;
                if (count >= capacity) return count;
                out[count][0] = x;
                out[count][1] = y;
                out[count][2] = z;
                out[count][3] = id;
                ++count;
            }
    return count;
}

void gm_mobs_init(GmMobLive *m, long long seed) {
    memset(m, 0, offsetof(GmMobLive, pig_collision_scratch));
    ew_store_clear(&m->a); ew_store_clear(&m->b);
    m->seed = seed; m->next_id = 1; m->next_orb_id=1000;
    jrand_set(&m->player_random, seed ^ INT64_C(0x504C41594552));
    m->player_ticks_since_last_swing = 1000;
    m->player_resistance_amplifier = -1;
    m->active_dimension = 0;
    m->boat_ride = -1;
    m->pig_ride = -1;
}

static int xp_split(int value){
    static const int split[]={2477,1237,617,307,149,73,37,17,7,3,1};
    for(unsigned i=0;i<sizeof split/sizeof split[0];++i)if(value>=split[i])return split[i];
    return 1;
}

void gm_mobs_spawn_xp(GmMobLive *m,double x,double y,double z,int value){
    if(!m||value<=0)return;
    while(value>0){
        int slot=-1;for(int i=0;i<GM_XP_ORBS;++i)if(m->xp_orbs[i].dead||m->xp_orbs[i].xpValue<=0){slot=i;break;}
        if(slot<0)return;
        int amount=xp_split(value);value-=amount;
        McOrb *o=&m->xp_orbs[slot];memset(o,0,sizeof *o);
        o->xpValue=amount;o->health=5;o->eid=m->next_orb_id++;o->motionY=0.2;
        o->yaw=0.0F;
        u64 h=mc_hash64((u64)m->seed^(u64)o->eid);
        double angle=(double)(h&0xffffu)*(2.0*MC_PI/65536.0);
        double speed=(double)((h>>16)&0xffffu)*(0.2/65535.0);
        o->motionX=-sin(angle)*speed;o->motionZ=cos(angle)*speed;
        m->orb_dimension[slot]=(signed char)m->active_dimension;
        eo_set_position(o,x,y,z);
        (void)loaded_append_xp(m, slot);
    }
}

int gm_mobs_spawn_xp_exact(GmMobLive *m, double x, double y, double z,
                           double vx, double vy, double vz, int value,
                           int eid, int age, int pickup_delay, int color,
                           int target_color) {
    if (!m || value <= 0 || eid <= 0 || age < 0 || age >= 6000
            || pickup_delay < 0 || color < 0)
        return 0;
    int slot = -1;
    for (int i = 0; i < GM_XP_ORBS; ++i)
        if (m->xp_orbs[i].dead || m->xp_orbs[i].xpValue <= 0) {
            slot = i;
            break;
        }
    if (slot < 0) return 0;
    McOrb *orb = &m->xp_orbs[slot];
    memset(orb, 0, sizeof *orb);
    orb->motionX = vx;
    orb->motionY = vy;
    orb->motionZ = vz;
    orb->xpValue = value;
    orb->health = 5;
    orb->eid = eid;
    orb->yaw = 0.0F;
    orb->xpOrbAge = age;
    orb->delayBeforeCanPickup = pickup_delay;
    orb->xpColor = color;
    orb->xpTargetColor = target_color;
    m->orb_dimension[slot] = (signed char)m->active_dimension;
    if (m->next_orb_id <= eid) m->next_orb_id = eid + 1;
    eo_set_position(orb, x, y, z);
    (void)loaded_append_xp(m, slot);
    return 1;
}

int gm_mobs_spawn_sized(GmMobLive *m, int type, double x, double y, double z, int size) {
    if (!m || !gm_living(type)) return -1;
    EwStore *s = now_store(m);
    float hp = max_health(type, size);
    int slot = ew_store_spawn(s, (u8)type, m->next_id++, x, y, z, hp);
    if (slot >= 0) {
        m->entity_dimension[slot]=(signed char)m->active_dimension;
        m->size[slot] = (unsigned char)(size > 0 ? size : (gm_is_slimey(type) ? 2 : 1));
        s->health[slot] = max_health(type, m->size[slot]);
        reset_slot_state_s(m, s, slot);
        (void)loaded_append_living(m, s, slot);
        ew_store_copy(next_store(m), s);
    }
    return slot;
}

int gm_mobs_spawn(GmMobLive *m, int type, double x, double y, double z) {
    int sz = 1;
    if (type == EW_TYPE_SLIME || type == EW_TYPE_MAGMA) sz = 2;
    return gm_mobs_spawn_sized(m, type, x, y, z, sz);
}

int gm_mobs_spawn_villager(GmMobLive *m, double x, double y, double z,
                           int profession) {
    if (!m || profession < 0 || profession > 5) return -1;
    int slot = gm_mobs_spawn(m, EW_TYPE_VILLAGER, x, y, z);
    if (slot < 0) return -1;
    m->villager_profession[slot] = (unsigned char)profession;
    return slot;
}

int gm_mobs_spawn_exact(GmMobLive *m, int type, int eid,
                        double x, double y, double z,
                        double vx, double vy, double vz,
                        float yaw, float health, int no_ai,
                        int hurt_time, int death_time,
                        int hurt_resistant_time) {
    if (!m || (type != EW_TYPE_SHEEP && type != EW_TYPE_PIG
                && type != EW_TYPE_COW && type != EW_TYPE_CHICKEN
                && type != EW_TYPE_VILLAGER)
            || eid <= 0 || health < 0.0f
            || health > max_health(type, 1)
            || (no_ai != 0 && no_ai != 1)
            || hurt_time < 0 || hurt_time > 10
            || death_time < 0 || death_time >= 20
            || hurt_resistant_time < 0 || hurt_resistant_time > 20)
        return -1;
    EwStore *s = now_store(m);
    int slot = ew_store_spawn(s, (u8)type, eid, x, y, z, health);
    if (slot < 0) return -1;
    s->vx[slot] = vx;
    s->vy[slot] = vy;
    s->vz[slot] = vz;
    s->yaw[slot] = yaw;
    m->entity_dimension[slot] = (signed char)m->active_dimension;
    reset_slot_state_s(m, s, slot);
    /* Both oracle variants are externally locked stationary fixtures. The
     * no_ai=false variant keeps Java's ordinary move/block-collision path but
     * has no tasks or gravity; magma advances its timers in this same bounded
     * controlled list. */
    m->controlled_no_ai[slot] = 1;
    m->controlled_block_collisions[slot] = (unsigned char)!no_ai;
    m->entity_hurt_time[slot] = hurt_time;
    m->entity_death_time[slot] = death_time;
    m->entity_hurt_resistant[slot] = hurt_resistant_time;
    if (m->next_id <= eid) m->next_id = eid + 1;
    (void)loaded_append_living(m, s, slot);
    ew_store_copy(next_store(m), s);
    return slot;
}

int gm_mobs_place_boat(GmMobLive *m, double x, double y, double z, float yaw) {
    int slot = gm_mobs_spawn(m, EW_TYPE_BOAT, x, y, z);
    if (slot < 0) return -1;
    now_store(m)->yaw[slot] = yaw;
    next_store(m)->yaw[slot] = yaw;
    if (slot >= 0 && slot < EW_MAX_ENTITIES) {
        s_boat_delta_rot[slot] = 0.0f;
        s_boat_glide[slot] = 0.8f;
    }
    return slot;
}

int gm_mobs_spawn_boat_exact(GmMobLive *m, int eid,
                             double x, double y, double z, float yaw) {
    if (!m || eid <= 0 || !isfinite(x) || !isfinite(y) || !isfinite(z)
            || !isfinite(yaw))
        return -1;
    EwStore *s = now_store(m);
    int slot = ew_store_spawn(s, EW_TYPE_BOAT, eid, x, y, z, 40.0f);
    if (slot < 0) return -1;
    s->yaw[slot] = yaw;
    m->entity_dimension[slot] = (signed char)m->active_dimension;
    reset_slot_state_s(m, s, slot);
    m->controlled_no_ai[slot] = 1;
    m->controlled_block_collisions[slot] = 1;
    if (m->next_id <= eid) m->next_id = eid + 1;
    (void)loaded_append_living(m, s, slot);
    ew_store_copy(next_store(m), s);
    return slot;
}

int gm_mobs_boat_riding(const GmMobLive *m) {
    return m && m->boat_ride >= 0;
}

int gm_mobs_boat_mount(GmMobLive *m, struct PsvPlayer *player_, int ox, int oz) {
    if (!m || !player_) return 0;
    PsvPlayer *p = (PsvPlayer *)player_;
    if (m->boat_ride >= 0) return 1;
    EwStore *s = now_store(m);
    double px = p->ent.posX + ox, py = p->ent.posY, pz = p->ent.posZ + oz;
    int best = -1; double bd = 2.5 * 2.5;
    for (int i = 1; i < EW_MAX_ENTITIES; ++i) {
        if (!s->alive[i] || s->type[i] != EW_TYPE_BOAT) continue;
        if (m->entity_dimension[i] != m->active_dimension) continue;
        double dx = s->x[i] - px, dy = s->y[i] - py, dz = s->z[i] - pz;
        double d = dx * dx + dy * dy + dz * dz;
        if (d < bd) { bd = d; best = i; }
    }
    if (best < 0) return 0;
    m->boat_ride = best;
    p->ent.posX = s->x[best] - ox;
    p->ent.posY = s->y[best] + 0.3;
    p->ent.posZ = s->z[best] - oz;
    p->ent.motionX = p->ent.motionY = p->ent.motionZ = 0.0;
    return 1;
}

void gm_mobs_boat_dismount(GmMobLive *m, struct PsvPlayer *player_, int ox, int oz) {
    if (!m || m->boat_ride < 0) return;
    PsvPlayer *p = (PsvPlayer *)player_;
    EwStore *s = now_store(m);
    int i = m->boat_ride;
    if (p && s->alive[i] && s->type[i] == EW_TYPE_BOAT) {
        double yaw = s->yaw[i] * MC_PI / 180.0;
        p->ent.posX = s->x[i] - ox - sin(yaw) * 1.0;
        p->ent.posY = s->y[i] + 0.5;
        p->ent.posZ = s->z[i] - oz + cos(yaw) * 1.0;
    }
    m->boat_ride = -1;
}

static float held_damage(const PsvPlayer *p) {
    int id = isr_get_stack(&p->inv, p->inv.current_item).item;
    if (id == 268 || id == 283) return mc_combat_weapon_raw(1);
    if (id == 272) return mc_combat_weapon_raw(2);
    if (id == 267) return mc_combat_weapon_raw(3);
    if (id == 276) return mc_combat_weapon_raw(4);
    if (id == 271 || id == 286) return 7.0f;
    if (id == 275 || id == 258 || id == 279) return 9.0f;
    if (id == 270 || id == 285) return 2.0f;
    if (id == 274) return 3.0f;
    if (id == 257) return 4.0f;
    if (id == 278) return 5.0f;
    if (id == 269 || id == 284) return 2.5f;
    if (id == 273) return 3.5f;
    if (id == 256) return 4.5f;
    if (id == 277) return 5.5f;
    return mc_combat_weapon_raw(0);
}

static double held_attack_speed(const PsvPlayer *p) {
    int id = isr_get_stack(&p->inv, p->inv.current_item).item;
    if (id == 268 || id == 272 || id == 267 || id == 276 || id == 283)
        return 1.5999999046325684;
    if (id == 271 || id == 275) return 0.7999999523162842;
    if (id == 258) return 0.9000000953674316;
    if (id == 279 || id == 286 || id == 269 || id == 273 || id == 256 || id == 277
            || id == 284)
        return 1.0;
    if (id == 270 || id == 274 || id == 257 || id == 278 || id == 285)
        return 1.2000000476837158;
    if (id == 290 || id == 294) return 1.0;
    if (id == 291) return 2.0;
    if (id == 292) return 3.0;
    return 4.0;
}

static float held_attack_strength(
        const PsvPlayer *p, int ticks, float adjust,
        double attack_speed_multiplier) {
    double speed = held_attack_speed(p) * attack_speed_multiplier;
    if (speed <= 0.0) return 0.0f;
    float period = (float)(1.0 / speed * 20.0);
    float value = ((float)ticks + adjust) / period;
    if (value < 0.0f) return 0.0f;
    return value > 1.0f ? 1.0f : value;
}

static int player_attack_creature(int type) {
    switch (type) {
    case EW_TYPE_ZOMBIE:
    case EW_TYPE_SKELETON:
    case EW_TYPE_PIGMAN:
    case EW_TYPE_WITHER_SKELETON:
        return EDF_CREATURE_UNDEAD;
    case EW_TYPE_SPIDER:
    case EW_TYPE_CAVE_SPIDER:
    case EW_TYPE_SILVERFISH:
        return EDF_CREATURE_ARTHROPOD;
    default:
        return EDF_CREATURE_UNDEFINED;
    }
}

static float held_enchantment_damage(const PsvPlayer *p, int target_type) {
    ICStack held = isr_get_stack(&p->inv, p->inv.current_item);
    int creature = player_attack_creature(target_type);
    float result = 0.0f;
    for (int i = 0; i < held.n_enchants; ++i) {
        int type = held.enchants[i].id == 16 ? EDF_TYPE_SHARPNESS
            : held.enchants[i].id == 17 ? EDF_TYPE_SMITE
            : held.enchants[i].id == 18 ? EDF_TYPE_BANE : -1;
        if (type >= 0)
            result += edf_calc_damage_by_creature(
                type, held.enchants[i].level, creature);
    }
    return result;
}

static int held_enchantment_level(const PsvPlayer *p, int enchantment_id) {
    ICStack held = isr_get_stack(&p->inv, p->inv.current_item);
    int level = 0;
    for (int i = 0; i < held.n_enchants; ++i)
        if (held.enchants[i].id == enchantment_id
                && held.enchants[i].level > level)
            level = held.enchants[i].level;
    return level;
}

static void player_attack_knockback(
        GmMobLive *m, EwStore *s, int slot, float strength,
        double ratio_x, double ratio_z) {
    /* All currently represented ordinary living targets retain the vanilla
     * zero knockback-resistance attribute. EntityLivingBase.knockBack still
     * consumes its resistance trial before changing motion. */
    (void)jrand_double(&m->entity_random[slot].random);
    float length = (float)sqrt(ratio_x * ratio_x + ratio_z * ratio_z);
    if (length <= 0.0F) return;
    s->vx[slot] /= 2.0D;
    s->vz[slot] /= 2.0D;
    s->vx[slot] -= ratio_x / (double)length * (double)strength;
    s->vz[slot] -= ratio_z / (double)length * (double)strength;
    if (s->on_ground[slot]) {
        s->vy[slot] /= 2.0D;
        s->vy[slot] += (double)strength;
        if (s->vy[slot] > 0.4000000059604645D)
            s->vy[slot] = 0.4000000059604645D;
    }
}

static void player_attack_primary_knockback(
        GmMobLive *m, EwStore *s, int slot,
        double attacker_x, double attacker_z,
        const GmMobDeathContext *death_context) {
    /* EntityLivingBase.setBeenAttacked performs its own resistance trial
     * before attackEntityFrom calls knockBack for the fresh hurt edge. */
    (void)jrand_double(&m->entity_random[slot].random);
    double ratio_x = attacker_x - s->x[slot];
    double ratio_z = attacker_z - s->z[slot];
    while (ratio_x * ratio_x + ratio_z * ratio_z < 1.0E-4D) {
        if (!death_context || !death_context->math_random_seed48) return;
        ratio_x = (mob_math_random_next_double(
                death_context->math_random_seed48)
            - mob_math_random_next_double(
                death_context->math_random_seed48)) * 0.01D;
        ratio_z = (mob_math_random_next_double(
                death_context->math_random_seed48)
            - mob_math_random_next_double(
                death_context->math_random_seed48)) * 0.01D;
    }
    player_attack_knockback(m, s, slot, 0.4F, ratio_x, ratio_z);
    /* getSoundPitch follows the primary knockback inside attackEntityFrom;
     * preserve its two random floats even while audio emission stays open. */
    (void)jrand_float(&m->entity_random[slot].random);
    (void)jrand_float(&m->entity_random[slot].random);
}

static void player_attack_extra_knockback(
        GmMobLive *m, EwStore *s, int slot, PsvPlayer *p,
        const McSinTable *sin_table, int level) {
    if (level <= 0 || !sin_table) return;
    float yaw = p->yaw * 0.017453292F;
    player_attack_knockback(
        m, s, slot, (float)level * 0.5F,
        (double)mc_sin(sin_table, yaw),
        (double)-mc_cos(sin_table, yaw));
    p->ent.motionX *= 0.6D;
    p->ent.motionZ *= 0.6D;
    p->sprinting = 0;
}

static float player_attack_after_mob_armor(
        const GmMobLive *m, int slot, int type, float amount) {
    int armor = type == EW_TYPE_ZOMBIE || type == EW_TYPE_PIGMAN ? 2
        : type == EW_TYPE_MAGMA ? (int)m->size[slot] * 3 : 0;
    return armor > 0
        ? mc_combat_damage_after_absorb(amount, (float)armor, 0.0f)
        : amount;
}

static int held_hit_durability(int id) {
    if (id == 268 || id == 272 || id == 267 || id == 276 || id == 283
            || (id >= 290 && id <= 294))
        return 1;
    if (id == 256 || id == 257 || id == 258
            || id == 269 || id == 270 || id == 271
            || id == 273 || id == 274 || id == 275
            || id == 277 || id == 278 || id == 279
            || id == 284 || id == 285 || id == 286)
        return 2;
    return 0;
}

static void damage_held_weapon(GmMobLive *m, PsvPlayer *p) {
    int slot = p->inv.current_item;
    ICStack held = isr_get_stack(&p->inv, slot);
    int amount = held_hit_durability(held.item);
    if (amount <= 0) return;
    ITAStack tool = mob_ita_stack(held);
    if (ita_attempt_damage(&tool, amount, &m->player_random)) {
        (void)isr_decr_stack_size(&p->inv, slot, 1);
    } else {
        held.meta = tool.damage;
        isr_set_stack(&p->inv, slot, held);
    }
}

static void slime_split(GmMobLive *m, EwStore *s, int i) {
    int sz = m->size[i];
    if (sz <= 1) return;
    int child = sz / 2;
    for (int k = 0; k < 2; ++k) {
        u64 h = mc_hash_seed((u64)m->seed, m->tick, i, k, s->id[i], 0x53504C54u);
        double ox = ((double)mc_hash_bound(h, 1000) / 1000.0 - 0.5) * (double)sz * 0.5;
        double oz = ((double)mc_hash_bound(mc_hash64(h), 1000) / 1000.0 - 0.5) * (double)sz * 0.5;
        int slot = ew_store_spawn(s, s->type[i], m->next_id++,
                                  s->x[i] + ox, s->y[i] + 0.5, s->z[i] + oz,
                                  max_health(s->type[i], child));
        if (slot < 0) break;
        m->entity_dimension[slot]=m->entity_dimension[i];
        m->size[slot] = (unsigned char)child;
        reset_slot_state_s(m, s, slot);
        (void)loaded_append_living(m, s, slot);
    }
}

static void mob_drop(GmMobLive *m, EwStore *s, int i, GmLiveSim *drops) {
    int item = 0, count = 1, xp = 5;
    int type = s->type[i];
    /* EntityPig.onDeath drops its ordinary loot and optional saddle now, but
     * EntityLivingBase remains in the world until deathTime reaches 20.  Keep
     * the represented rider association and loaded slot during that window;
     * the ordinary product tick owns terminal XP, particles, dismount, and
     * retirement.  Controlled passive deaths already have an exact separate
     * loot boundary and never enter mob_drop. */
    if (type == EW_TYPE_PIG && !m->controlled_no_ai[i]) {
        if (m->entity_dead[i]) return;
        gm_live_spawn_item(
            drops, s->x[i], s->y[i] + 0.25, s->z[i], 319, 1, 0, 10);
        if (m->pig_saddled[i])
            gm_live_spawn_item(
                drops, s->x[i], s->y[i] + 0.25, s->z[i], 329, 1, 0, 10);
        s->health[i] = 0.0F;
        m->entity_dead[i] = 1;
        m->entity_death_time[i] = 0;
        return;
    }
    switch (type) {
    case EW_TYPE_ZOMBIE: item = 367; break;
    case EW_TYPE_SKELETON: item = 352; break;
    case EW_TYPE_WITHER_SKELETON:
        item = 352;
        /* wither skeleton skull rare drop skipped; coal possible */
        if ((mc_hash64((u64)m->seed ^ (u64)s->id[i] ^ 0x57495448ULL) & 3ULL) == 0)
            gm_live_spawn_item(drops, s->x[i], s->y[i] + 0.25, s->z[i], 263, 1, 0, 10);
        break;
    case EW_TYPE_CREEPER: item = 289; break;
    case EW_TYPE_SPIDER:
    case EW_TYPE_CAVE_SPIDER: item = 287; break;
    case EW_TYPE_ENDERMAN:
        if ((mc_hash64((u64)m->seed ^ (u64)s->id[i]) & 1ULL) != 0) item = 368;
        break;
    case EW_TYPE_BLAZE:
        if ((mc_hash64((u64)m->seed ^ (u64)s->id[i]) & 1ULL) != 0) item = 369;
        xp = 10; break;
    case EW_TYPE_PIGMAN:
        item = 367; /* rotten flesh */
        if ((mc_hash64((u64)m->seed ^ (u64)s->id[i] ^ 0x474F4C44ULL) & 3ULL) == 0)
            gm_live_spawn_item(drops, s->x[i], s->y[i] + 0.25, s->z[i], 266, 1, 0, 10);
        break;
    case EW_TYPE_GHAST:
        item = 289; /* gunpowder */
        if ((mc_hash64((u64)m->seed ^ (u64)s->id[i] ^ 0x54454152ULL) & 1ULL) != 0)
            gm_live_spawn_item(drops, s->x[i], s->y[i] + 0.25, s->z[i], 370, 1, 0, 10);
        break;
    case EW_TYPE_MAGMA:
        if (m->size[i] > 1 &&
            (mc_hash64((u64)m->seed ^ (u64)s->id[i]) & 1ULL) != 0) item = 378;
        xp = m->size[i];
        slime_split(m, s, i);
        break;
    case EW_TYPE_SLIME:
        if (m->size[i] == 1) item = 341; /* slime ball */
        xp = m->size[i];
        slime_split(m, s, i);
        break;
    case EW_TYPE_SILVERFISH:
        item = 0; xp = 5; break;
    case EW_TYPE_SHEEP:
        item = 35; xp = 1;
        gm_live_spawn_item(drops, s->x[i], s->y[i] + 0.25, s->z[i], 423, 1, 0, 10); break;
    case EW_TYPE_PIG: item = 319; xp = 1; break;
    case EW_TYPE_COW:
        item = 363; xp = 1;
        gm_live_spawn_item(drops, s->x[i], s->y[i] + 0.25, s->z[i], 334, 1, 0, 10); break;
    case EW_TYPE_CHICKEN:
        item = 365; xp = 1;
        gm_live_spawn_item(drops, s->x[i], s->y[i] + 0.25, s->z[i], 288, 1, 0, 10); break;
    case EW_TYPE_BOAT:
        item = 333; xp = 0; break;
    default: break;
    }
    if (item) gm_live_spawn_item(drops, s->x[i], s->y[i] + 0.25, s->z[i], item, count, 0, 10);
    /* EntityPig.onDeath appends the saddle after ordinary pork loot.  This
     * tail drop is outside the superclass doMobLoot path in Java 1.11.2. */
    if (type == EW_TYPE_PIG && m->pig_saddled[i])
        gm_live_spawn_item(
            drops, s->x[i], s->y[i] + 0.25, s->z[i], 329, 1, 0, 10);
    if (xp > 0) gm_mobs_spawn_xp(m, s->x[i], s->y[i] + 0.25, s->z[i], xp);
    if (m->boat_ride == i) m->boat_ride = -1;
    if (m->pig_ride == i) {
        m->pig_ride = -1;
        m->pig_vehicle_server.valid = 0;
    }
    loaded_invalidate_living(m, i);
    s->alive[i] = 0;
    s->type[i] = EW_TYPE_NONE;
}

int gm_mobs_player_attack(GmMobLive *m, const struct PsvPlayer *player_,
                          int ox, int oz,
                          const struct McSinTable *sin_table_,
                          GmLiveSim *drops,
                          float attack_damage_bonus,
                          double attack_speed_multiplier,
                          int on_ladder, int in_water, int riding,
                          const GmMobDeathContext *death_context) {
    if (!m || !player_) return 0;
    const PsvPlayer *p = (const PsvPlayer *)player_;
    PsvPlayer *mutable_player = (PsvPlayer *)player_;
    const McSinTable *sin_table = (const McSinTable *)sin_table_;
    EwStore *s = now_store(m);
    double px = p->ent.posX + ox, py = p->ent.posY + PSV_EYE_HEIGHT, pz = p->ent.posZ + oz;
    double yr = p->yaw * MC_PI / 180.0, pr = p->pitch * MC_PI / 180.0;
    double dx = -sin(yr) * cos(pr), dy = -sin(pr), dz = cos(yr) * cos(pr);
    int best = -1; double best_t = 3.0;
    for (int i = 1; i < EW_MAX_ENTITIES; ++i) if (s->alive[i]
            && !m->entity_dead[i] && s->health[i] > 0.0F
            && gm_living(s->type[i])) {
        if (m->entity_dimension[i] != m->active_dimension) continue;
        float width, height;
        ehs_size_scaled(s->type[i], m->size[i], &width, &height);
        double cx = s->x[i], cy = s->y[i] + height * 0.5, cz = s->z[i];
        double vx = cx-px, vy = cy-py, vz = cz-pz;
        double t = vx*dx + vy*dy + vz*dz;
        if (t < 0.0 || t > best_t) continue;
        double ex=vx-t*dx, ey=vy-t*dy, ez=vz-t*dz;
        double radius = width * 0.5 + 0.25;
        double vtol = height * 0.5 + 0.25;
        if (s->type[i] == EW_TYPE_BOAT) vtol = 2.0; /* low hull, aim from standing */
        if (ex*ex + ez*ez <= radius*radius && fabs(ey) <= vtol) {
            best=i; best_t=t;
        }
    }
    if (best < 0) return 0;
    float strength = held_attack_strength(
        p, m->player_ticks_since_last_swing, 0.5f,
        attack_speed_multiplier);
    m->player_ticks_since_last_swing = 0;
    float base_damage = held_damage(p) + attack_damage_bonus;
    if (base_damage < 0.0f) base_damage = 0.0f;
    float amount = base_damage *
        (0.2f + strength * strength * 0.8f);
    float enchantment = held_enchantment_damage(p, s->type[best]) * strength;
    int knockback = held_enchantment_level(p, 19);
    int fire_aspect = held_enchantment_level(p, 20);
    if (p->sprinting && strength > 0.9F) ++knockback;
    int critical = strength > 0.9f && p->fall_distance > 0.0f
        && !p->ent.onGround && !on_ladder && !in_water && !p->blindness
        && !riding && !p->sprinting && gm_living(s->type[best]);
    if (critical) amount *= 1.5f;
    amount += enchantment;
    if (s->type[best] == EW_TYPE_BOAT) {
        /* EntityBoat.attackEntityFrom stores the actual player attack amount
         * multiplied by ten and breaks only when DAMAGE_TAKEN exceeds 40. */
        m->boat_damage[best] += amount * 10.0F;
        if (m->boat_damage[best] > 40.0F)
            mob_drop(m, s, best, drops);
        ew_store_copy(next_store(m), s);
        return 1;
    }
    if (amount <= 0.0f) {
        ew_store_copy(next_store(m), s);
        return 1;
    }
    int fire_preignited = 0;
    if (fire_aspect > 0 && m->fire_ticks[best] <= 0) {
        m->fire_ticks[best] = 20;
        fire_preignited = 1;
    }
    if (s->type[best] == EW_TYPE_PIG && !m->controlled_no_ai[best]
            && death_context) {
        int result = mob_player_damage_pig_slot_exact(
            m, s, best, p->ent.posX + ox, p->ent.posZ + oz,
            amount, drops, death_context);
        if (result == 2) {
            if (fire_aspect > 0) {
                int final_fire = fire_aspect * 4 * 20;
                if (m->fire_ticks[best] < final_fire)
                    m->fire_ticks[best] = final_fire;
            }
            player_attack_extra_knockback(
                m, s, best, mutable_player, sin_table, knockback);
            damage_held_weapon(m, mutable_player);
        } else if (fire_preignited) m->fire_ticks[best] = 0;
        ew_store_copy(next_store(m), s);
        return result;
    }
    float applied = amount;
    int accepted = 1;
    int fresh = m->entity_hurt_resistant[best] <= 10;
    if (m->entity_hurt_resistant[best] > 10) {
        if (amount <= m->entity_last_damage[best]) {
            accepted = 0;
        } else {
            applied = amount - m->entity_last_damage[best];
            m->entity_last_damage[best] = amount;
        }
    } else {
        m->entity_last_damage[best] = amount;
        m->entity_hurt_resistant[best] = 20;
        m->entity_hurt_time[best] = 10;
    }
    if (accepted) {
        applied = player_attack_after_mob_armor(
            m, best, s->type[best], applied);
        applied = mob_resistance_damage(m, best, applied);
        applied = mob_absorb_damage(m, best, applied);
        s->health[best] -= applied;
        if (s->health[best] < 0.0f) s->health[best] = 0.0f;
        mark_hurt(m, s, best);
        if (fresh)
            player_attack_primary_knockback(
                m, s, best, p->ent.posX + ox, p->ent.posZ + oz,
                death_context);
        player_attack_extra_knockback(
            m, s, best, mutable_player, sin_table, knockback);
        if (fire_aspect > 0) {
            int final_fire = fire_aspect * 4 * 20;
            if (m->fire_ticks[best] < final_fire)
                m->fire_ticks[best] = final_fire;
        }
        if (s->type[best] == EW_TYPE_PIG) {
            m->entity_recently_hit[best] = 100;
            m->entity_attacking_player[best] = 1;
        }
        damage_held_weapon(m, mutable_player);
        if (s->health[best] <= 0.0f && !m->controlled_no_ai[best])
            mob_drop(m, s, best, drops);
    }
    else if (fire_preignited) m->fire_ticks[best] = 0;
    ew_store_copy(next_store(m), s);
    return accepted ? 2 : 1;
}

void gm_mobs_player_swing(GmMobLive *m) {
    if (m) m->player_ticks_since_last_swing = 0;
}

float gm_mobs_player_attack_strength(
        const GmMobLive *m, const struct PsvPlayer *player_,
        double attack_speed_multiplier) {
    if (!m) return 1.0f;
    const PsvPlayer *p = (const PsvPlayer *)player_;
    if (!p) {
        double speed = 4.0 * attack_speed_multiplier;
        if (speed <= 0.0) return 0.0f;
        float period = (float)(1.0 / speed * 20.0);
        float value = (float)m->player_ticks_since_last_swing / period;
        if (value < 0.0f) return 0.0f;
        return value > 1.0f ? 1.0f : value;
    }
    return held_attack_strength(
        p, m->player_ticks_since_last_swing, 0.0f,
        attack_speed_multiplier);
}

static McEntity ridden_pig_local_entity(
        const EbLiving *liv, int ox, int oz) {
    McEntity local;
    local.box = liv->base.phys.box;
    local.box.minX -= ox;
    local.box.maxX -= ox;
    local.box.minZ -= oz;
    local.box.maxZ -= oz;
    local.posX = liv->base.phys.posX - ox;
    local.posY = liv->base.phys.posY;
    local.posZ = liv->base.phys.posZ - oz;
    local.motionX = liv->base.phys.motionX;
    local.motionY = liv->base.phys.motionY;
    local.motionZ = liv->base.phys.motionZ;
    local.onGround = liv->base.phys.onGround;
    local.collidedHorizontally = liv->base.phys.collidedHorizontally;
    local.collidedVertically = liv->base.phys.collidedVertically;
    local.isCollided = liv->base.phys.isCollided;
    return local;
}

/* Entity.handleWaterMovement runs before the client-side NoAI .98 damping.
 * Reuse the verified player liquid/current probe in chunk-local coordinates;
 * the material semantics are Entity-level and are identical for a pig. */
static int ridden_pig_handle_water(
        const struct Chunk *window, int ox, int oz, EbLiving *liv) {
    McEntity local = ridden_pig_local_entity(liv, ox, oz);
    if (!psv_in_liquid((const Chunk *)window, &local, 1)) return 0;
    int in_water = psv_handle_water((const Chunk *)window, &local);
    liv->base.phys.motionX = local.motionX;
    liv->base.phys.motionY = local.motionY;
    liv->base.phys.motionZ = local.motionZ;
    if (in_water) liv->base.fallDistance = 0.0F;
    return in_water;
}

/* Entity.isInLava uses a different contracted AABB from water and does not
 * apply a current. Keep water precedence at the caller, matching travel(). */
static int ridden_pig_in_lava(
        const struct Chunk *window, int ox, int oz, const EbLiving *liv) {
    McEntity local = ridden_pig_local_entity(liv, ox, oz);
    return psv_in_liquid((const Chunk *)window, &local, 0);
}

static int mob_slot_world_box(
        const GmMobLive *m, const EwStore *s, int slot, McAABB *out) {
    if (!m || !s || !out || slot <= 0 || slot >= EW_MAX_ENTITIES
            || !s->alive[slot])
        return 0;
    if (m->entity_box_valid[slot]) {
        *out = mc_aabb_make(
            m->entity_box_min_x[slot], m->entity_box_min_y[slot],
            m->entity_box_min_z[slot], m->entity_box_max_x[slot],
            m->entity_box_max_y[slot], m->entity_box_max_z[slot]);
        return 1;
    }
    float width, height;
    ehs_size_scaled(s->type[slot], m->size[slot], &width, &height);
    if (gm_passive(s->type[slot]) && m->growing_age[slot] < 0) {
        width *= 0.5F;
        height *= 0.5F;
    }
    double half = (double)width * 0.5;
    *out = mc_aabb_make(
        s->x[slot] - half, s->y[slot], s->z[slot] - half,
        s->x[slot] + half, s->y[slot] + height,
        s->z[slot] + half);
    return 1;
}

/* Server-side EntityLivingBase.attackEntityFrom for environment fire/lava.
 * The mounted client has a separate Entity instance and RNG stream, so these
 * sound draws stay on the server cursor. */
static int ridden_pig_environment_damage(
        GmMobLive *m, EwStore *s, int slot, float damage, int is_fire_damage,
        double event_x, double event_y, double event_z,
        uint64_t *math_random_seed48) {
    if (!m || !s || slot <= 0 || slot >= EW_MAX_ENTITIES
            || !s->alive[slot] || s->health[slot] <= 0.0F
            || damage <= 0.0F)
        return 0;
    m->entity_age[slot] = 0;
    if (!pec_fire_resist_blocks(
            is_fire_damage,
            m->entity_server_fire_resistance_ticks[slot] > 0))
        return 0;
    int fresh = m->entity_hurt_resistant[slot] <= 10;
    if (!fresh && damage <= m->entity_last_damage[slot]) return 0;
    float applied = fresh
        ? damage : damage - m->entity_last_damage[slot];
    m->entity_last_damage[slot] = damage;
    if (fresh) {
        m->entity_hurt_resistant[slot] = 20;
        m->entity_hurt_time[slot] = 10;
    }
    s->health[slot] -= applied;
    if (s->health[slot] < 0.0F) s->health[slot] = 0.0F;
    mark_hurt(m, s, slot);
    if (fresh) {
        mob_event_append(
            m, GM_MOB_EVENT_ENTITY_STATUS, s->id[slot], 2,
            event_x, event_y, event_z, 0.0F, 0.0F);
        (void)jrand_double(&m->entity_server_random[slot].random);
        if (math_random_seed48)
            (void)mob_math_random_next_double(math_random_seed48);
        m->entity_server_living_sound_time[slot] = -80;
        (void)jrand_float(&m->entity_server_random[slot].random);
        (void)jrand_float(&m->entity_server_random[slot].random);
    }
    return 1;
}

/* Entity.onEntityUpdate fire phase followed by lava contact. This is the
 * authoritative-server half of an active ridden pig tick. */
static void tick_ridden_pig_server_fire_lava(
        GmMobLive *m, EwStore *s, int slot, int in_lava,
        double event_x, double event_y, double event_z,
        uint64_t *math_random_seed48) {
    if (m->fire_ticks[slot] > 0) {
        if (m->fire_ticks[slot] % 20 == 0)
            (void)ridden_pig_environment_damage(
                m, s, slot, 1.0F, 1,
                event_x, event_y, event_z, math_random_seed48);
        --m->fire_ticks[slot];
    }
    if (in_lava && s->health[slot] > 0.0F) {
        (void)ridden_pig_environment_damage(
            m, s, slot, 4.0F, 1,
            event_x, event_y, event_z, math_random_seed48);
        if (m->fire_ticks[slot] < 15 * 20)
            m->fire_ticks[slot] = 15 * 20;
        m->entity_server_fall_distance[slot] *= 0.5F;
    }
}

static void tick_ridden_pig_server_ambient(
        GmMobLive *m, const EwStore *s, int slot) {
    if (s->health[slot] > 0.0F
            && jrand_int_bound(
                &m->entity_server_random[slot].random, 1000)
                < m->entity_server_living_sound_time[slot]++) {
        m->entity_server_living_sound_time[slot] = -80;
        (void)jrand_float(&m->entity_server_random[slot].random);
        (void)jrand_float(&m->entity_server_random[slot].random);
    }
}

static int pig_vehicle_box_in_liquid(
        const struct Chunk *window, int ox, int oz,
        const McAABB *box, int water) {
    if (!window || !box) return 0;
    McEntity local;
    memset(&local, 0, sizeof local);
    local.box = *box;
    local.box.minX -= ox;
    local.box.maxX -= ox;
    local.box.minZ -= oz;
    local.box.maxZ -= oz;
    return psv_in_liquid((const Chunk *)window, &local, water ? 1 : 0);
}

static int pig_vehicle_server_in_liquid(
        const struct Chunk *window, int ox, int oz,
        const GmPigVehicleServerState *server, int water) {
    return server && server->valid && pig_vehicle_box_in_liquid(
        window, ox, oz, &server->box, water);
}

static void ridden_pig_water_entry_rng(JavaRandom *random, float width);

static float ridden_pig_slot_width(
        const GmMobLive *m, const EwStore *s, int slot) {
    float width, height;
    ehs_size_scaled(s->type[slot], m->size[slot], &width, &height);
    if (gm_passive(s->type[slot]) && m->growing_age[slot] < 0)
        width *= 0.5F;
    return width;
}

/* Authoritative EntityPig base phase after processVehicleMove. The controlling
 * EntityPlayerMP can hold the steering item but is never the local user, so
 * the ridden branch aligns rotation and zeros server motion without running
 * client-predicted travel. Environment probes use the independent server AABB. */
static void tick_ridden_pig_vehicle_server_base(
        GmMobLive *m, EwStore *s, int slot,
        const struct Chunk *window, int ox, int oz,
        int can_be_steered, float rider_yaw, float rider_pitch,
        uint64_t *math_random_seed48) {
    GmPigVehicleServerState *server = &m->pig_vehicle_server;
    if (!server->valid || server->eid != s->id[slot]) return;
    int was_in_water = m->entity_server_in_water[slot] ? 1 : 0;
    m->entity_server_in_water[slot] = (unsigned char)(
        pig_vehicle_server_in_liquid(window, ox, oz, server, 1) ? 1 : 0);
    if (!was_in_water && m->entity_server_in_water[slot]
            && !server->first_update) {
        ridden_pig_water_entry_rng(
            &m->entity_server_random[slot].random,
            ridden_pig_slot_width(m, s, slot));
    }
    if (m->entity_server_in_water[slot]) {
        server->fall_distance = 0.0F;
        m->fire_ticks[slot] = 0;
    }
    m->entity_server_in_lava[slot] = (unsigned char)(
        pig_vehicle_server_in_liquid(window, ox, oz, server, 0) ? 1 : 0);
    m->entity_server_fall_distance[slot] = server->fall_distance;
    tick_ridden_pig_server_fire_lava(
        m, s, slot, m->entity_server_in_lava[slot],
        server->x, server->y, server->z, math_random_seed48);
    server->fall_distance = m->entity_server_fall_distance[slot];
    if (can_be_steered) {
        server->yaw = rider_yaw;
        server->pitch = rider_pitch * 0.5F;
        server->vx = 0.0;
        server->vy = 0.0;
        server->vz = 0.0;
    }
    server->first_update = 0;
    /* NetHandlerPlayServer.update re-seeds both vehicle baselines once per
     * network tick, after the ordinary world entity pass. Packets received
     * during the next tick retain this common epoch origin while accepted
     * packets advance only the secondary baseline. */
    server->lowest_x = server->lowest_x1 = server->x;
    server->lowest_y = server->lowest_y1 = server->y;
    server->lowest_z = server->lowest_z1 = server->z;
}

/* Entity.resetHeight consumes the pig's private RNG when a dry EntityLivingBase
 * enters water during move().  Particle output is outside the simulation
 * contract, but preserving these draws keeps every later gameplay RNG choice
 * aligned with the Java entity. */
static void ridden_pig_water_entry_rng(
        JavaRandom *random, float width) {
    float particle_count = 1.0F + width * 20.0F;
    (void)jrand_float(random);
    (void)jrand_float(random);
    for (int i = 0; (float)i < particle_count; ++i) {
        (void)jrand_float(random);
        (void)jrand_float(random);
        (void)jrand_float(random);
    }
    for (int i = 0; (float)i < particle_count; ++i) {
        (void)jrand_float(random);
        (void)jrand_float(random);
    }
}

/* Entity.move without updateFallState.  EntityLivingBase interposes its
 * post-move water probe before delegating to Entity.updateFallState, so the
 * active ridden land path needs this exact split point. */
static double ridden_pig_move_aabb_before_fall(
        EntBody *body, double x, double y, double z,
        const McAABB *blocks, int nblocks) {
    if (body->phys.isInWeb) {
        body->phys.isInWeb = 0;
        x *= 0.25;
        y *= 0.05000000074505806;
        z *= 0.25;
        body->phys.motionX = 0.0;
        body->phys.motionY = 0.0;
        body->phys.motionZ = 0.0;
    }
    McEntity exact;
    exact.box = body->phys.box;
    exact.posX = body->phys.posX;
    exact.posY = body->phys.posY;
    exact.posZ = body->phys.posZ;
    exact.motionX = body->phys.motionX;
    exact.motionY = body->phys.motionY;
    exact.motionZ = body->phys.motionZ;
    exact.onGround = body->phys.onGround;
    exact.collidedHorizontally = body->phys.collidedHorizontally;
    exact.collidedVertically = body->phys.collidedVertically;
    exact.isCollided = body->phys.isCollided;
    double before_y = exact.posY;
    mc_entity_move_step(
        &exact, x, y, z, blocks, nblocks, body->phys.stepHeight);
    body->phys.box = exact.box;
    body->phys.posX = exact.posX;
    body->phys.posY = exact.posY;
    body->phys.posZ = exact.posZ;
    body->phys.motionX = exact.motionX;
    body->phys.motionY = exact.motionY;
    body->phys.motionZ = exact.motionZ;
    body->phys.onGround = exact.onGround;
    body->phys.collidedHorizontally = exact.collidedHorizontally;
    body->phys.collidedVertically = exact.collidedVertically;
    body->phys.isCollided = exact.isCollided;
    return exact.posY - before_y;
}

/* Bounded client-controlled pig land branch with the
 * EntityLivingBase.updateFallState water interposition point. */
static void tick_ridden_pig_land_water_entry(
        const struct Chunk *window, int ox, int oz,
        const McSinTable *st, GmMobLive *m, int slot, EbLiving *liv,
        float ground_slip, int movement_blocked,
        McAABB *collision_boxes, int nboxes,
        int (*contact_cells)[4], int ncontacts) {
    eb_on_entity_update(&liv->base);
    if (liv->jumpTicks > 0) --liv->jumpTicks;
    if (!liv->isServerWorld) {
        liv->base.phys.motionX *= 0.98;
        liv->base.phys.motionY *= 0.98;
        liv->base.phys.motionZ *= 0.98;
    }
    if (fabs(liv->base.phys.motionX) < 0.003)
        liv->base.phys.motionX = 0.0;
    if (fabs(liv->base.phys.motionY) < 0.003)
        liv->base.phys.motionY = 0.0;
    if (fabs(liv->base.phys.motionZ) < 0.003)
        liv->base.phys.motionZ = 0.0;
    if (movement_blocked) {
        liv->isJumping = 0;
        liv->moveStrafing = 0.0F;
        liv->moveForward = 0.0F;
        liv->randomYawVelocity = 0.0F;
    }
    if (liv->isJumping) {
        if (liv->base.phys.onGround && liv->jumpTicks == 0) {
            elb_jump(liv, st);
            liv->jumpTicks = 10;
        }
    } else {
        liv->jumpTicks = 0;
    }
    liv->moveStrafing *= 0.98F;
    liv->moveForward *= 0.98F;
    liv->randomYawVelocity *= 0.9F;
    if (!liv->isServerWorld) {
        ++liv->base.ticksExisted;
        return;
    }

    int on_ground = liv->base.phys.onGround;
    float f6 = on_ground ? (ground_slip * 0.91F) : 0.91F;
    float f7 = 0.16277136F / (f6 * f6 * f6);
    float f8 = on_ground
        ? liv->landMovementFactor * f7 : liv->jumpMovementFactor;
    eb_move_relative(
        &liv->base, liv->moveStrafing, liv->moveForward, f8, st);
    if (elb_is_on_ladder_contacts(liv, contact_cells, ncontacts)) {
        const double ladder_speed = 0.15000000596046448;
        if (liv->base.phys.motionX < -ladder_speed)
            liv->base.phys.motionX = -ladder_speed;
        if (liv->base.phys.motionX > ladder_speed)
            liv->base.phys.motionX = ladder_speed;
        if (liv->base.phys.motionZ < -ladder_speed)
            liv->base.phys.motionZ = -ladder_speed;
        if (liv->base.phys.motionZ > ladder_speed)
            liv->base.phys.motionZ = ladder_speed;
        liv->base.fallDistance = 0.0F;
        if (liv->base.phys.motionY < -0.15)
            liv->base.phys.motionY = -0.15;
    }
    f6 = on_ground ? (ground_slip * 0.91F) : 0.91F;
    double landing_motion_y = liv->base.phys.isInWeb
        ? 0.0 : liv->base.phys.motionY;
    double resolved_y = ridden_pig_move_aabb_before_fall(
        &liv->base, liv->base.phys.motionX, liv->base.phys.motionY,
        liv->base.phys.motionZ, collision_boxes, nboxes);
    int entered_water = ridden_pig_handle_water(window, ox, oz, liv);
    eb_update_fall_state(&liv->base, resolved_y);
    if (entered_water) {
        m->entity_in_water[slot] = 1;
        ridden_pig_water_entry_rng(
            &m->entity_random[slot].random, liv->base.width);
    }
    elb_apply_landing_contact(
        liv, landing_motion_y, contact_cells, ncontacts);
    elb_apply_block_contacts(liv, contact_cells, ncontacts);
    if (liv->base.phys.collidedHorizontally
            && elb_is_on_ladder_contacts(
                liv, contact_cells, ncontacts))
        liv->base.phys.motionY = 0.2;
    if (!liv->base.hasNoGravity) liv->base.phys.motionY -= 0.08;
    liv->base.phys.motionY *= 0.9800000190734863;
    liv->base.phys.motionX *= (double)f6;
    liv->base.phys.motionZ *= (double)f6;
    ++liv->base.ticksExisted;
}

/* Bounded client-controlled pig water branch. AI swimming and particles are
 * outside this path; collision, callbacks, drag, gravity, and edge climb keep
 * the exact 1.11.2 EntityLivingBase ordering. */
static void tick_ridden_pig_water(
        const struct Chunk *window, int ox, int oz,
        const McSinTable *st, EbLiving *liv, int movement_blocked,
        McAABB *collision_boxes, int (*contact_cells)[4]) {
    eb_on_entity_update(&liv->base);
    if (liv->jumpTicks > 0) --liv->jumpTicks;

    if (fabs(liv->base.phys.motionX) < 0.003)
        liv->base.phys.motionX = 0.0;
    if (fabs(liv->base.phys.motionY) < 0.003)
        liv->base.phys.motionY = 0.0;
    if (fabs(liv->base.phys.motionZ) < 0.003)
        liv->base.phys.motionZ = 0.0;

    if (movement_blocked) {
        liv->isJumping = 0;
        liv->moveStrafing = 0.0F;
        liv->moveForward = 0.0F;
        liv->randomYawVelocity = 0.0F;
    }
    if (liv->isJumping)
        liv->base.phys.motionY += 0.03999999910593033;
    else
        liv->jumpTicks = 0;

    liv->moveStrafing *= 0.98F;
    liv->moveForward *= 0.98F;
    liv->randomYawVelocity *= 0.9F;
    double old_y = liv->base.phys.posY;
    eb_move_relative(
        &liv->base, liv->moveStrafing, liv->moveForward, 0.02F, st);

    McAABB query = mc_aabb_addcoord(
        &liv->base.phys.box, liv->base.phys.motionX,
        liv->base.phys.motionY, liv->base.phys.motionZ);
    query.minY -= liv->base.phys.stepHeight;
    query.maxY += liv->base.phys.stepHeight;
    McAABB local_query = query;
    local_query.minX -= ox;
    local_query.maxX -= ox;
    local_query.minZ -= oz;
    local_query.maxZ -= oz;
    int n = psv_collect_blocks(
        (const Chunk *)window, &local_query,
        collision_boxes, GM_PIG_COLLISION_BOXES);
    for (int box = 0; box < n; ++box) {
        collision_boxes[box].minX += ox;
        collision_boxes[box].maxX += ox;
        collision_boxes[box].minZ += oz;
        collision_boxes[box].maxZ += oz;
    }
    int ncontacts = collect_block_contact_cells(
        window, &query, ox, oz, contact_cells, GM_PIG_COLLISION_BOXES);
    double landing_motion_y = liv->base.phys.isInWeb
        ? 0.0 : liv->base.phys.motionY;
    eb_move_aabb(
        &liv->base, liv->base.phys.motionX, liv->base.phys.motionY,
        liv->base.phys.motionZ, collision_boxes, n);
    elb_apply_landing_contact(
        liv, landing_motion_y, contact_cells, ncontacts);
    elb_apply_block_contacts(liv, contact_cells, ncontacts);

    liv->base.phys.motionX *= (double)0.8F;
    liv->base.phys.motionY *= 0.800000011920929;
    liv->base.phys.motionZ *= (double)0.8F;
    if (!liv->base.hasNoGravity) liv->base.phys.motionY -= 0.02;
    if (liv->base.phys.collidedHorizontally) {
        McEntity local = ridden_pig_local_entity(liv, ox, oz);
        if (psv_offset_in_liquid(
                (const Chunk *)window, &local,
                local.motionX,
                local.motionY + 0.6000000238418579
                    - local.posY + old_y,
                local.motionZ, collision_boxes))
            liv->base.phys.motionY = 0.30000001192092896;
    }
    ++liv->base.ticksExisted;
}

/* Bounded client-controlled pig lava branch. Lava has no current, uses 0.5
 * drag on all axes, and otherwise shares the liquid collision ordering. */
static void tick_ridden_pig_lava(
        const struct Chunk *window, int ox, int oz,
        const McSinTable *st, EbLiving *liv, int movement_blocked,
        McAABB *collision_boxes, int (*contact_cells)[4]) {
    eb_on_entity_update(&liv->base);
    if (liv->jumpTicks > 0) --liv->jumpTicks;

    if (fabs(liv->base.phys.motionX) < 0.003)
        liv->base.phys.motionX = 0.0;
    if (fabs(liv->base.phys.motionY) < 0.003)
        liv->base.phys.motionY = 0.0;
    if (fabs(liv->base.phys.motionZ) < 0.003)
        liv->base.phys.motionZ = 0.0;

    if (movement_blocked) {
        liv->isJumping = 0;
        liv->moveStrafing = 0.0F;
        liv->moveForward = 0.0F;
        liv->randomYawVelocity = 0.0F;
    }
    if (liv->isJumping)
        liv->base.phys.motionY += 0.03999999910593033;
    else
        liv->jumpTicks = 0;

    liv->moveStrafing *= 0.98F;
    liv->moveForward *= 0.98F;
    liv->randomYawVelocity *= 0.9F;
    double old_y = liv->base.phys.posY;
    eb_move_relative(
        &liv->base, liv->moveStrafing, liv->moveForward, 0.02F, st);

    McAABB query = mc_aabb_addcoord(
        &liv->base.phys.box, liv->base.phys.motionX,
        liv->base.phys.motionY, liv->base.phys.motionZ);
    query.minY -= liv->base.phys.stepHeight;
    query.maxY += liv->base.phys.stepHeight;
    McAABB local_query = query;
    local_query.minX -= ox;
    local_query.maxX -= ox;
    local_query.minZ -= oz;
    local_query.maxZ -= oz;
    int n = psv_collect_blocks(
        (const Chunk *)window, &local_query,
        collision_boxes, GM_PIG_COLLISION_BOXES);
    for (int box = 0; box < n; ++box) {
        collision_boxes[box].minX += ox;
        collision_boxes[box].maxX += ox;
        collision_boxes[box].minZ += oz;
        collision_boxes[box].maxZ += oz;
    }
    int ncontacts = collect_block_contact_cells(
        window, &query, ox, oz, contact_cells, GM_PIG_COLLISION_BOXES);
    double landing_motion_y = liv->base.phys.isInWeb
        ? 0.0 : liv->base.phys.motionY;
    eb_move_aabb(
        &liv->base, liv->base.phys.motionX, liv->base.phys.motionY,
        liv->base.phys.motionZ, collision_boxes, n);
    elb_apply_landing_contact(
        liv, landing_motion_y, contact_cells, ncontacts);
    elb_apply_block_contacts(liv, contact_cells, ncontacts);

    liv->base.phys.motionX *= 0.5;
    liv->base.phys.motionY *= 0.5;
    liv->base.phys.motionZ *= 0.5;
    if (!liv->base.hasNoGravity) liv->base.phys.motionY -= 0.02;
    if (liv->base.phys.collidedHorizontally) {
        McEntity local = ridden_pig_local_entity(liv, ox, oz);
        if (psv_offset_in_liquid(
                (const Chunk *)window, &local,
                local.motionX,
                local.motionY + 0.6000000238418579
                    - local.posY + old_y,
                local.motionZ, collision_boxes))
            liv->base.phys.motionY = 0.30000001192092896;
    }
    ++liv->base.ticksExisted;
}

static void move_mob(
        GmWorld *w, const struct Chunk *window, int ox, int oz,
        const McSinTable *st, GmMobLive *m, EwStore *s,
        int i, int moving, int jump, int mating_move_helper,
        int ridden_pig, int steerable,
        float rider_yaw, float rider_pitch, int movement_blocked,
        int swim_jump, double nav_speed) {
    EhsIntent intent;
    EbLiving liv;
    PcfBlock blocks[GM_MOB_BLOCKS];
    double old_x = s->x[i], old_z = s->z[i];
    double movement_multiplier = mob_movement_effect_multiplier(m, i);
    ehs_intent_from_ai(s->type[i], s->ai_state[i], moving, s->x[i], s->z[i],
                       s->path_tx[i], s->path_tz[i], s->path_tx[i], s->path_tz[i], &intent);
    if (moving && mating_move_helper) {
        /* EntityMoveHelper.setAIMoveSpeed writes both landMovementFactor and
         * moveForward. With EntityAIMate speed=1 these are the animal's exact
         * MOVEMENT_SPEED attribute, rather than the old unit-forward proxy. */
        intent.moveForward = (float)(
            (double)animal_mating_movement_speed(s->type[i])
            * movement_multiplier);
        intent.yaw = mob_move_helper_yaw(
            s->yaw[i], s->path_tx[i] - s->x[i],
            s->path_tz[i] - s->z[i]);
    }
    if (!moving) intent.yaw = s->yaw[i];
    if (gm_passive(s->type[i]) && moving)
        intent.yaw = pai_update_rotation(s->yaw[i], intent.yaw, 90.0f);
    if (moving && jump) intent.isJumping = 1;
    if (ridden_pig) {
        /* EntityLivingBase damps its AI inputs by .98 before virtual travel,
         * but EntityPig's steerable override ignores those arguments and
         * calls super.moveEntityWithHeading(0, 1).  Seed the shared spine so
         * its damping produces that exact unit forward input. */
        intent.moveForward = steerable ? (1.0F / 0.98F) : 0.0F;
        intent.moveStrafing = 0.0F;
        intent.isJumping = 0;
        intent.yaw = steerable ? rider_yaw : s->yaw[i];
    }
    ehs_load_living(&liv, s, i, &intent);
    liv.jumpBoostAmplifier = mob_effect_amplifier(m, i, 8);
    liv.levitationAmplifier = mob_effect_amplifier(m, i, 25);
    if (ridden_pig && m->entity_box_valid[i]) {
        liv.base.phys.box = mc_aabb_make(
            m->entity_box_min_x[i], m->entity_box_min_y[i],
            m->entity_box_min_z[i], m->entity_box_max_x[i],
            m->entity_box_max_y[i], m->entity_box_max_z[i]);
    }
    liv.base.fallDistance = m->entity_fall_distance[i];
    liv.base.phys.collidedHorizontally =
        m->entity_collided_horizontal[i] ? 1 : 0;
    liv.base.phys.collidedVertically =
        m->entity_collided_vertical[i] ? 1 : 0;
    liv.base.phys.isInWeb = m->entity_in_web[i] ? 1 : 0;
    int ridden_in_water = 0;
    int ridden_in_lava = 0;
    if (ridden_pig && window) {
        ridden_in_water = ridden_pig_handle_water(
            window, ox, oz, &liv);
        int is_in_lava = ridden_pig_in_lava(window, ox, oz, &liv);
        ridden_in_lava = !ridden_in_water && is_in_lava;
        m->entity_in_water[i] = (unsigned char)(ridden_in_water ? 1 : 0);
        m->entity_in_lava[i] = (unsigned char)(is_in_lava ? 1 : 0);
    }
    if (moving && mating_move_helper)
        liv.landMovementFactor = intent.moveForward;
    if (ridden_pig) {
        m->pig_step_height[i] = steerable ? 1.0F : 0.5F;
        m->pig_jump_factor[i] = steerable
            ? m->pig_ai_speed[i] * 0.1F : 0.02F;
        liv.base.phys.stepHeight = m->pig_step_height[i];
        liv.jumpMovementFactor = m->pig_jump_factor[i];
        if (steerable) {
            /* The local EntityPlayerSP owns vehicle travel in 1.11.2. Its
             * remote EntityLivingBase branch damps existing motion before
             * EntityPig invokes the steerable super travel call. */
            liv.base.phys.motionX *= 0.98D;
            liv.base.phys.motionY *= 0.98D;
            liv.base.phys.motionZ *= 0.98D;
            float speed = 0.25F * 0.225F;
            m->pig_prev_yaw[i] = rider_yaw;
            m->pig_pitch[i] = rider_pitch * 0.5F;
            m->pig_render_yaw[i] = rider_yaw;
            m->pig_head_yaw[i] = rider_yaw;
            if (m->pig_boosting[i]
                    && m->pig_boost_time[i]++ > m->pig_boost_total[i])
                m->pig_boosting[i] = 0;
            if (m->pig_boosting[i]) {
                float phase = (float)m->pig_boost_time[i]
                    / (float)m->pig_boost_total[i] * (float)MC_PI;
                speed += speed * 1.15F * mc_sin(st, phase);
            }
            m->pig_ai_speed[i] = speed;
            liv.landMovementFactor = speed;
        } else {
            /* Without a carrot-on-a-stick the client-side NoAI pig cannot
             * passenger-steer.  Vanilla only applies the remote .98 motion
             * damping; moveEntityWithHeading returns without travel. */
            liv.isServerWorld = 0;
        }
    }
    if (movement_blocked && s->type[i] == EW_TYPE_PIG) {
        /* EntityPig's non-steerable branch restores these values before its
         * server-side super travel, even for a health-zero body. */
        m->pig_step_height[i] = 0.5F;
        m->pig_jump_factor[i] = 0.02F;
        liv.base.phys.stepHeight = 0.5F;
        liv.jumpMovementFactor = 0.02F;
        liv.isServerWorld = 1;
    }
    if (gm_passive(s->type[i]) && m->growing_age[i] < 0) {
        float w, h;
        ehs_size_scaled(s->type[i], m->size[i], &w, &h);
        w *= 0.5F;
        h *= 0.5F;
        liv.base.width = w;
        liv.base.height = h;
        liv.base.phys.box = mc_aabb_make(
            s->x[i] - w * 0.5, s->y[i], s->z[i] - w * 0.5,
            s->x[i] + w * 0.5, s->y[i] + h, s->z[i] + w * 0.5);
    }
    /* Override size for slime/magma. */
    if (gm_is_slimey(s->type[i])) {
        float w, h; ehs_size_scaled(s->type[i], m->size[i], &w, &h);
        liv.base.width = w; liv.base.height = h;
        liv.base.phys.box = mc_aabb_make(s->x[i] - w * 0.5, s->y[i], s->z[i] - w * 0.5,
                                         s->x[i] + w * 0.5, s->y[i] + h, s->z[i] + w * 0.5);
    }
    if (gm_passive(s->type[i]) && !ridden_pig) {
        /* EntityMoveHelper MOVE_TO:
         *   setAIMoveSpeed((float)(navigatorSpeed * MOVEMENT_SPEED attr));
         * EntityLiving.setAIMoveSpeed writes that same value to moveForward.
         * EntityLivingBase then damps moveForward by 0.98 before travel. */
        float ai_speed = (float)(nav_speed * pai_attribute_speed(s->type[i]));
        liv.landMovementFactor = ai_speed;
        liv.moveForward = moving ? ai_speed : 0.0f;
        liv.moveStrafing = 0.0f;
        if (moving) {
            double dx = s->path_tx[i] - s->x[i];
            double dy = s->path_ty[i] - s->y[i];
            double dz = s->path_tz[i] - s->z[i];
            if (dy > liv.base.phys.stepHeight && dx * dx + dz * dz < fmax(1.0, liv.base.width))
                liv.isJumping = 1;
        }
    }
    /* Ghast: no gravity; fly toward path. */
    if (s->type[i] == EW_TYPE_GHAST) {
        double dx = s->path_tx[i] - s->x[i];
        double dy = s->path_ty[i] - s->y[i];
        double dz = s->path_tz[i] - s->z[i];
        double len = sqrt(dx * dx + dy * dy + dz * dz);
        if (moving && len > 0.01) {
            double sp = 0.1;
            s->vx[i] = dx / len * sp;
            s->vy[i] = dy / len * sp;
            s->vz[i] = dz / len * sp;
        } else {
            s->vx[i] *= 0.9; s->vy[i] *= 0.9; s->vz[i] *= 0.9;
        }
        s->x[i] += s->vx[i]; s->y[i] += s->vy[i]; s->z[i] += s->vz[i];
        s->yaw[i] = intent.yaw;
        s->on_ground[i] = 0;
        return;
    }
    /* EntityBlaze.onLivingUpdate: slow fall (motionY *= 0.6 when falling).
     * Full heightOffset hover from updateAITasks is not ported (needs its own
     * randomized heightOffset timer); fall damping alone is the small half. */
    if (s->type[i] == EW_TYPE_BLAZE && !liv.base.phys.onGround &&
        liv.base.phys.motionY < 0.0)
        liv.base.phys.motionY *= 0.6;

    /* EntityAISwimming only requests a jump; the actual water/lava travel is
     * EntityLivingBase's fluid branch. Keep it here so a passive does not run
     * the land gravity branch while submerged. */
    if (gm_passive(s->type[i]) && !ridden_pig &&
        (pai_in_material(w, s, i, 0) || pai_in_material(w, s, i, 1))) {
        int in_water = pai_in_material(w, s, i, 0);
        eb_on_entity_update(&liv.base);
        if (fabs(liv.base.phys.motionX) < 0.003) liv.base.phys.motionX = 0.0;
        if (fabs(liv.base.phys.motionY) < 0.003) liv.base.phys.motionY = 0.0;
        if (fabs(liv.base.phys.motionZ) < 0.003) liv.base.phys.motionZ = 0.0;
        if (swim_jump) liv.base.phys.motionY += 0.03999999910593033;
        liv.moveStrafing *= 0.98f;
        liv.moveForward *= 0.98f;
        eb_move_relative(&liv.base, liv.moveStrafing, liv.moveForward, 0.02f, st);
        McAABB fq = mc_aabb_addcoord(&liv.base.phys.box, liv.base.phys.motionX,
                                     liv.base.phys.motionY, liv.base.phys.motionZ);
        fq.minY -= liv.base.phys.stepHeight; fq.maxY += liv.base.phys.stepHeight;
        int fn = collect_blocks(w, &fq, blocks, GM_MOB_BLOCKS);
        eb_move(&liv.base, liv.base.phys.motionX, liv.base.phys.motionY,
                liv.base.phys.motionZ, blocks, fn);
        double drag = in_water ? 0.800000011920929 : 0.5;
        liv.base.phys.motionX *= drag;
        liv.base.phys.motionY *= drag;
        liv.base.phys.motionZ *= drag;
        if (!liv.base.hasNoGravity) liv.base.phys.motionY -= 0.02;
        ehs_store_living(s, i, &liv);
        return;
    }
    float slip = 0.6f;
    if (liv.base.phys.onGround) {
        int id = gm_world_block(w, mc_floor(liv.base.phys.posX),
                                mc_floor(liv.base.phys.box.minY)-1,
                                mc_floor(liv.base.phys.posZ));
        if (id == 79 || id == 174 || id == 212) slip = 0.98f;
        if (id == 165) slip = 0.8f;
        if (id == 8 || id == 9) slip = 0.8f; /* water friction for boat handled below */
    }
    /* Angry pigman speed boost +0.05 (AttributeModifier). */
    if (s->type[i] == EW_TYPE_PIGMAN && m->anger[i] > 0)
        liv.landMovementFactor += 0.05f;
    if (!mating_move_helper) {
        double adjusted = (double)liv.landMovementFactor
            * movement_multiplier;
        liv.landMovementFactor = adjusted > 0.0 ? (float)adjusted : 0.0F;
    }
    McAABB q = mc_aabb_addcoord(&liv.base.phys.box, liv.base.phys.motionX,
                                liv.base.phys.motionY, liv.base.phys.motionZ);
    q.minY -= liv.base.phys.stepHeight; q.maxY += liv.base.phys.stepHeight;
    if (ridden_pig && window) {
        McAABB local_query = q;
        McAABB *collision_boxes = m->pig_collision_scratch;
        int (*contact_cells)[4] = m->pig_block_contact_scratch;
        local_query.minX -= ox;
        local_query.maxX -= ox;
        local_query.minZ -= oz;
        local_query.maxZ -= oz;
        int n = psv_collect_blocks(
            (const Chunk *)window, &local_query,
            collision_boxes, GM_PIG_COLLISION_BOXES);
        for (int box = 0; box < n; ++box) {
            collision_boxes[box].minX += ox;
            collision_boxes[box].maxX += ox;
            collision_boxes[box].minZ += oz;
            collision_boxes[box].maxZ += oz;
        }
        int ncontacts = collect_block_contact_cells(
            window, &q, ox, oz, contact_cells, GM_PIG_COLLISION_BOXES);
        if (ridden_in_water && steerable)
            tick_ridden_pig_water(
                window, ox, oz, st, &liv, movement_blocked,
                collision_boxes, contact_cells);
        else if (ridden_in_lava && steerable)
            tick_ridden_pig_lava(
                window, ox, oz, st, &liv, movement_blocked,
                collision_boxes, contact_cells);
        else if (steerable) {
            tick_ridden_pig_land_water_entry(
                window, ox, oz, st, m, i, &liv, slip,
                movement_blocked, collision_boxes, n,
                contact_cells, ncontacts);
        } else {
            eb_tick_living_aabb_contacts(
                &liv, slip, movement_blocked, collision_boxes, n,
                contact_cells, ncontacts, st);
        }
    } else {
        int n = collect_blocks(w, &q, blocks, GM_MOB_BLOCKS);
        eb_tick_living(&liv, slip, movement_blocked, blocks, n, st);
    }
    ehs_store_living(s, i, &liv);
    if (ridden_pig) {
        m->entity_box_min_x[i] = liv.base.phys.box.minX;
        m->entity_box_min_y[i] = liv.base.phys.box.minY;
        m->entity_box_min_z[i] = liv.base.phys.box.minZ;
        m->entity_box_max_x[i] = liv.base.phys.box.maxX;
        m->entity_box_max_y[i] = liv.base.phys.box.maxY;
        m->entity_box_max_z[i] = liv.base.phys.box.maxZ;
        m->entity_box_valid[i] = 1;
    }
    m->entity_fall_distance[i] = liv.base.fallDistance;
    m->entity_collided_horizontal[i] =
        (unsigned char)(liv.base.phys.collidedHorizontally ? 1 : 0);
    m->entity_collided_vertical[i] =
        (unsigned char)(liv.base.phys.collidedVertically ? 1 : 0);
    m->entity_in_web[i] = (unsigned char)(liv.base.phys.isInWeb ? 1 : 0);
    if (ridden_pig && window)
        m->entity_in_lava[i] = (unsigned char)(
            ridden_pig_in_lava(window, ox, oz, &liv) ? 1 : 0);
    if (ridden_pig) {
        float moved = (float)sqrt(
            (s->x[i] - old_x) * (s->x[i] - old_x)
                + (s->z[i] - old_z) * (s->z[i] - old_z)) * 4.0F;
        if (moved > 1.0F) moved = 1.0F;
        m->pig_prev_limb_amount[i] = m->pig_limb_amount[i];
        m->pig_limb_amount[i] +=
            (moved - m->pig_limb_amount[i]) * 0.4F;
        m->pig_limb_swing[i] += m->pig_limb_amount[i];
        if (steerable) {
            m->pig_prev_limb_amount[i] = m->pig_limb_amount[i];
            m->pig_limb_amount[i] +=
                (moved - m->pig_limb_amount[i]) * 0.4F;
            m->pig_limb_swing[i] += m->pig_limb_amount[i];
        }
    }
}

static int alive_count(const GmMobLive *m,const EwStore *s) {
    int n=0;
    for (int i=1;i<EW_MAX_ENTITIES;++i)
        if (s->alive[i] && m->entity_dimension[i]==m->active_dimension &&
            gm_hostile(s->type[i])) ++n;
    return n;
}
static int living_count(const GmMobLive *m,const EwStore *s) {
    int n=0;
    for(int i=1;i<EW_MAX_ENTITIES;++i)
        if(s->alive[i]&&m->entity_dimension[i]==m->active_dimension&&
           gm_living(s->type[i]))++n;
    return n;
}
static int passive_count(const GmMobLive *m,const EwStore *s) {
    int n=0;
    for(int i=1;i<EW_MAX_ENTITIES;++i)
        if(s->alive[i]&&m->entity_dimension[i]==m->active_dimension&&
           gm_passive(s->type[i]))++n;
    return n;
}

int gm_mobs_living_count(const GmMobLive *m){
    return m?living_count(m,const_store(m)):0;
}

static int collect_orb_blocks(GmWorld *w,const McAABB *q,McAABB *out,int cap){
    int n=0,x0=mc_floor(q->minX)-1,x1=mc_floor(q->maxX)+1;
    int y0=mc_floor(q->minY)-1,y1=mc_floor(q->maxY)+1;
    int z0=mc_floor(q->minZ)-1,z1=mc_floor(q->maxZ)+1;
    if(y0<0)y0=0;
    if(y1>255)y1=255;
    for(int x=x0;x<=x1;++x)for(int y=y0;y<=y1;++y)for(int z=z0;z<=z1;++z){
        if(!solid_id(gm_world_block(w,x,y,z)))continue;
        if(n==cap)return n;
        out[n++]=mc_aabb_make(x,y,z,x+1,y+1,z+1);
    }return n;
}

static McAABB xp_player_collision_box(PsvPlayer *p, int ox, int oz) {
    /* EntityPlayer.onLivingUpdate queries onCollideWithPlayer over
     * playerBox.expand(1.0, 0.5, 1.0). In 1.11.2 expand is directional:
     * positive arguments extend maxX/maxY/maxZ only. */
    McAABB player = mc_aabb_addcoord(&p->ent.box, 1.0, 0.5, 1.0);
    player.minX += ox;
    player.maxX += ox;
    player.minZ += oz;
    player.maxZ += oz;
    return player;
}

static void tick_xp_orb_slot(
        GmMobLive *m, GmWorld *w, PsvPlayer *p, int ox, int oz,
        const McAABB *player, int i) {
    McOrb *o = &m->xp_orbs[i];
    if (o->dead || o->xpValue <= 0
            || m->orb_dimension[i] != m->active_dimension)
        return;
    McAABB q = mc_aabb_addcoord(
        &o->box, o->motionX, o->motionY, o->motionZ);
    McAABB blocks[64];
    int nb = collect_orb_blocks(w, &q, blocks, 64);
    int ux = mc_floor(o->posX);
    int uy = mc_floor(o->box.minY) - 1;
    int uz = mc_floor(o->posZ);
    if (uy < 0) uy = 0;
    u16 under = mc_state(
        gm_world_block(w, ux, uy, uz), gm_world_meta(w, ux, uy, uz));
    eo_tick(o, p->ent.posX + ox, p->ent.posY, p->ent.posZ + oz,
        PSV_EYE_HEIGHT, 0, blocks, nb, under, 0);
    if (m->xp_collision_count < GM_XP_ORBS)
        m->xp_collision_boxes[m->xp_collision_count++] = o->box;
    if (!o->dead && o->delayBeforeCanPickup <= 0
            && mc_aabb_intersects(&o->box, player)) {
        m->xp_total += o->xpValue;
        o->dead = 1;
    }
    if (o->dead) loaded_invalidate_xp(m, i);
}

static void tick_xp_orbs(GmMobLive *m,GmWorld *w,PsvPlayer *p,int ox,int oz){
    McAABB player = xp_player_collision_box(p, ox, oz);
    m->xp_collision_count = 0;
    for (int i = 0; i < GM_XP_ORBS; ++i)
        tick_xp_orb_slot(m, w, p, ox, oz, &player, i);
}

void gm_mobs_tick_xp(GmMobLive *m, GmWorld *w, struct PsvPlayer *player,
                     int ox, int oz) {
    if (!m || !w || !player) return;
    loaded_order_prepare(m);
    tick_xp_orbs(m, w, (PsvPlayer *)player, ox, oz);
    loaded_order_compact(m);
    if (m->player_ticks_since_last_swing < 1000000000)
        ++m->player_ticks_since_last_swing;
}

static void tick_controlled_dead_fire(
        GmMobLive *m, const EwStore *s, int slot) {
    if (!m || !s || slot <= 0 || slot >= EW_MAX_ENTITIES
            || !m->entity_dead[slot]
            || s->health[slot] > 0.0F)
        return;
    /* Entity.onEntityUpdate processes the signed fire counter before
     * EntityLivingBase ages combat/death state. A dead target rejects the
     * periodic ON_FIRE attack, so only the counter changes here. */
    if (m->fire_ticks[slot] > 0)
        --m->fire_ticks[slot];
}

static void terminal_particle_append(
        GmMobLive *m, GmMobTerminalParticles *batch) {
    int index;
    if (m->terminal_particle_count < GM_MOB_TERMINAL_PARTICLE_CAPACITY) {
        index = (m->terminal_particle_head + m->terminal_particle_count)
            % GM_MOB_TERMINAL_PARTICLE_CAPACITY;
        ++m->terminal_particle_count;
    } else {
        index = m->terminal_particle_head;
        m->terminal_particle_head = (m->terminal_particle_head + 1)
            % GM_MOB_TERMINAL_PARTICLE_CAPACITY;
        ++m->terminal_particle_dropped;
    }
    batch->seq = m->terminal_particle_next_seq++;
    m->terminal_particles[index] = *batch;
}

int gm_mobs_terminal_particle_count(const GmMobLive *m) {
    return m ? m->terminal_particle_count : 0;
}

int gm_mobs_terminal_particle_get(
        const GmMobLive *m, int index, GmMobTerminalParticles *out) {
    if (!m || !out || index < 0 || index >= m->terminal_particle_count)
        return 0;
    int slot = (m->terminal_particle_head + index)
        % GM_MOB_TERMINAL_PARTICLE_CAPACITY;
    *out = m->terminal_particles[slot];
    return 1;
}

static void mob_particle_batch_append(
        GmMobLive *m, const GmMobParticleBatch *batch) {
    int index;
    if (!m || !batch || batch->count < 1
            || batch->count > GM_MOB_PARTICLE_BATCH_MAX)
        return;
    if (m->particle_batch_count < GM_MOB_PARTICLE_BATCH_CAPACITY) {
        index = (m->particle_batch_head + m->particle_batch_count)
            % GM_MOB_PARTICLE_BATCH_CAPACITY;
        ++m->particle_batch_count;
    } else {
        index = m->particle_batch_head;
        m->particle_batch_head = (m->particle_batch_head + 1)
            % GM_MOB_PARTICLE_BATCH_CAPACITY;
        ++m->particle_batch_dropped;
    }
    m->particle_batches[index] = *batch;
    m->particle_batches[index].seq = m->particle_batch_next_seq++;
}

int gm_mobs_particle_batch_count(const GmMobLive *m) {
    return m ? m->particle_batch_count : 0;
}

int gm_mobs_particle_batch_get(
        const GmMobLive *m, int index, GmMobParticleBatch *out) {
    if (!m || !out || index < 0 || index >= m->particle_batch_count)
        return 0;
    *out = m->particle_batches[
        (m->particle_batch_head + index) % GM_MOB_PARTICLE_BATCH_CAPACITY];
    return 1;
}

static int terminal_xp_spawn(
        GmMobLive *m, double x, double y, double z, int dimension,
        uint64_t *world_random_seed48,
        uint64_t *math_random_seed48, int *next_entity_id) {
    if (!m || !world_random_seed48 || !math_random_seed48
            || !next_entity_id || *next_entity_id <= 0
            || *next_entity_id == INT_MAX)
        return 0;
    int free_slot = -1;
    for (int slot = 0; slot < GM_XP_ORBS; ++slot)
        if (m->xp_orbs[slot].dead || m->xp_orbs[slot].xpValue <= 0) {
            free_slot = slot;
            break;
        }
    if (free_slot < 0)
        return 0;

    uint64_t world_cursor = *world_random_seed48;
    uint64_t math_cursor = *math_random_seed48;
    JavaRandom world_random;
    jrand_set_seed48(&world_random, world_cursor);
    int value = 1 + jrand_int_bound(&world_random, 3);
    float yaw = (float)(mob_math_random_next_double(&math_cursor) * 360.0);
    float motion_x_float = (float)(
        mob_math_random_next_double(&math_cursor)
            * 0.20000000298023224 - 0.10000000149011612);
    float motion_y_float = (float)(
        mob_math_random_next_double(&math_cursor) * 0.2);
    float motion_z_float = (float)(
        mob_math_random_next_double(&math_cursor)
            * 0.20000000298023224 - 0.10000000149011612);

    McOrb *orb = &m->xp_orbs[free_slot];
    memset(orb, 0, sizeof *orb);
    orb->motionX = (double)(motion_x_float * 2.0F);
    orb->motionY = (double)(motion_y_float * 2.0F);
    orb->motionZ = (double)(motion_z_float * 2.0F);
    orb->xpValue = value;
    orb->health = 5;
    orb->eid = *next_entity_id;
    orb->yaw = yaw;
    m->orb_dimension[free_slot] = (signed char)dimension;
    eo_set_position(orb, x, y, z);
    if (m->next_orb_id <= orb->eid)
        m->next_orb_id = orb->eid + 1;
    (void)loaded_append_xp(m, free_slot);
    *world_random_seed48 = world_random.seed;
    *math_random_seed48 = math_cursor;
    ++*next_entity_id;
    return 1;
}

static int breeding_xp_spawn(
        GmMobLive *m, double x, double y, double z, int dimension,
        int value, uint64_t *math_random_seed48, int *next_entity_id,
        int *out_slot, int *out_eid) {
    int free_slot = -1;
    uint64_t math_cursor;
    int eid;
    if (!m || value <= 0 || !math_random_seed48 || !next_entity_id
            || *next_entity_id <= 0 || *next_entity_id == INT_MAX)
        return 0;
    for (int slot = 0; slot < GM_XP_ORBS; ++slot)
        if (m->xp_orbs[slot].dead || m->xp_orbs[slot].xpValue <= 0) {
            free_slot = slot;
            break;
        }
    eid = (*next_entity_id)++;
    if (m->next_id <= eid) m->next_id = eid + 1;
    if (m->next_orb_id <= eid) m->next_orb_id = eid + 1;
    math_cursor = *math_random_seed48;
    float yaw = (float)(mob_math_random_next_double(&math_cursor) * 360.0);
    float motion_x_float = (float)(
        mob_math_random_next_double(&math_cursor)
            * 0.20000000298023224 - 0.10000000149011612);
    float motion_y_float = (float)(
        mob_math_random_next_double(&math_cursor) * 0.2);
    float motion_z_float = (float)(
        mob_math_random_next_double(&math_cursor)
            * 0.20000000298023224 - 0.10000000149011612);
    *math_random_seed48 = math_cursor;
    if (out_eid) *out_eid = eid;
    if (out_slot) *out_slot = free_slot;
    if (free_slot < 0) {
        ++m->sheep_breed_xp_dropped;
        return -1;
    }
    McOrb *orb = &m->xp_orbs[free_slot];
    memset(orb, 0, sizeof *orb);
    orb->motionX = (double)(motion_x_float * 2.0F);
    orb->motionY = (double)(motion_y_float * 2.0F);
    orb->motionZ = (double)(motion_z_float * 2.0F);
    orb->xpValue = value;
    orb->health = 5;
    orb->eid = eid;
    orb->yaw = yaw;
    m->orb_dimension[free_slot] = (signed char)dimension;
    eo_set_position(orb, x, y, z);
    if (m->next_orb_id <= eid) m->next_orb_id = eid + 1;
    (void)loaded_append_xp(m, free_slot);
    return 1;
}

static int animal_breed_slots(
        GmMobLive *m, EwStore *s, int initiator_slot, int mate_slot,
        int event_cancelled, int event_child_present,
        uint64_t *world_random_seed48, uint64_t *math_random_seed48,
        int *next_entity_id, int do_mob_loot, GmSheepMateResult *out) {
    JavaRandom world_random;
    int animal_type, child_eid, child_fleece = -1, child_slot = -1;
    int first_fleece, second_fleece;
    if (!m || !s || initiator_slot <= 0 || mate_slot <= 0
            || initiator_slot >= EW_MAX_ENTITIES
            || mate_slot >= EW_MAX_ENTITIES
            || initiator_slot == mate_slot
            || !s->alive[initiator_slot] || !s->alive[mate_slot]
            || !gm_passive(s->type[initiator_slot])
            || s->type[mate_slot] != s->type[initiator_slot]
            || !world_random_seed48 || !math_random_seed48
            || !next_entity_id || *next_entity_id <= 0
            || *next_entity_id == INT_MAX
            || (event_cancelled != 0 && event_cancelled != 1)
            || (event_child_present != 0 && event_child_present != 1)
            || (do_mob_loot != 0 && do_mob_loot != 1))
        return GM_SHEEP_MATE_NONE;

    animal_type = s->type[initiator_slot];
    /* createChild constructs the child and takes its global ID before the
     * EntityLivingBase constructor's three Math.random draws. Sheep alone
     * then performs the crafting/fallback color lookup before the event. */
    child_eid = (*next_entity_id)++;
    if (m->next_id <= child_eid) m->next_id = child_eid + 1;
    if (m->next_orb_id <= child_eid) m->next_orb_id = child_eid + 1;
    (void)mob_math_random_next_double(math_random_seed48);
    (void)mob_math_random_next_double(math_random_seed48);
    (void)mob_math_random_next_double(math_random_seed48);
    if (animal_type == EW_TYPE_SHEEP) {
        first_fleece = m->sheep_data[initiator_slot] & 15;
        second_fleece = m->sheep_data[mate_slot] & 15;
        jrand_set_seed48(&world_random, *world_random_seed48);
        child_fleece = gm_mobs_sheep_child_color(
            &world_random, first_fleece, second_fleece);
        *world_random_seed48 = world_random.seed;
    }
    if (out) {
        out->child_eid = child_eid;
        out->child_type = animal_type;
        out->child_fleece = child_fleece;
        out->child_slot = -1;
        out->xp_eid = -1;
        out->xp_slot = -1;
        out->xp_value = 0;
    }

    if (event_cancelled) {
        m->growing_age[initiator_slot] = 6000;
        m->growing_age[mate_slot] = 6000;
        m->sheep_in_love[initiator_slot] = 0;
        m->sheep_in_love[mate_slot] = 0;
        m->sheep_bred_by_player[initiator_slot] = 0;
        m->sheep_bred_by_player[mate_slot] = 0;
        return GM_SHEEP_MATE_CANCELLED;
    }
    if (!event_child_present)
        return GM_SHEEP_MATE_NULL_CHILD;

    m->growing_age[initiator_slot] = 6000;
    m->growing_age[mate_slot] = 6000;
    m->sheep_in_love[initiator_slot] = 0;
    m->sheep_in_love[mate_slot] = 0;
    m->sheep_bred_by_player[initiator_slot] = 0;
    m->sheep_bred_by_player[mate_slot] = 0;

    child_slot = ew_store_spawn(
        s, animal_type, child_eid,
        s->x[initiator_slot], s->y[initiator_slot], s->z[initiator_slot],
        max_health(animal_type, 1));
    if (child_slot >= 0) {
        m->entity_dimension[child_slot] = m->entity_dimension[initiator_slot];
        m->size[child_slot] = 1;
        reset_slot_state_s(m, s, child_slot);
        (void)loaded_append_living(m, s, child_slot);
        /* A freshly constructed Java entity is not grounded until its own
         * movement collision resolves a supporting block. */
        s->on_ground[child_slot] = 0;
        if (m->animal_child_state_count > 0) {
            int random_index = m->animal_child_state_head;
            m->entity_random[child_slot] =
                m->animal_child_random_queue[random_index];
            if (animal_type == EW_TYPE_CHICKEN)
                m->chicken_time_until_next_egg[child_slot] =
                    m->animal_child_chicken_egg_queue[random_index];
            m->animal_child_state_head = (unsigned char)(
                (random_index + 1) % EW_MAX_ENTITIES);
            --m->animal_child_state_count;
        }
        m->growing_age[child_slot] = -24000;
        if (animal_type == EW_TYPE_SHEEP)
            m->sheep_data[child_slot] = (unsigned char)child_fleece;
        s->yaw[child_slot] = 0.0F;
    } else {
        ++m->sheep_birth_dropped;
    }
    if (out) out->child_slot = child_slot;

    GmMobParticleBatch hearts = {
        .eid = s->id[initiator_slot],
        .dimension = m->entity_dimension[initiator_slot],
        .particle_id = 34,
        .count = 7
    };
    float width, height;
    ehs_size_scaled(animal_type, 1, &width, &height);
    for (int particle_index = 0; particle_index < 7; ++particle_index) {
        GmTerminalParticle *particle = &hearts.particles[particle_index];
        particle->vx = jrand_gaussian_next(
            &m->entity_random[initiator_slot]) * 0.02;
        particle->vy = jrand_gaussian_next(
            &m->entity_random[initiator_slot]) * 0.02;
        particle->vz = jrand_gaussian_next(
            &m->entity_random[initiator_slot]) * 0.02;
        double offset_x = jrand_double(
            &m->entity_random[initiator_slot].random)
                * (double)width * 2.0 - (double)width;
        double offset_y = 0.5 + jrand_double(
            &m->entity_random[initiator_slot].random) * (double)height;
        double offset_z = jrand_double(
            &m->entity_random[initiator_slot].random)
                * (double)width * 2.0 - (double)width;
        particle->x = s->x[initiator_slot] + offset_x;
        particle->y = s->y[initiator_slot] + offset_y;
        particle->z = s->z[initiator_slot] + offset_z;
    }
    mob_particle_batch_append(m, &hearts);

    if (do_mob_loot) {
        int value = jrand_int_bound(
            &m->entity_random[initiator_slot].random, 7) + 1;
        int xp_slot = -1, xp_eid = -1;
        (void)breeding_xp_spawn(
            m, s->x[initiator_slot], s->y[initiator_slot],
            s->z[initiator_slot], m->entity_dimension[initiator_slot],
            value, math_random_seed48, next_entity_id,
            &xp_slot, &xp_eid);
        if (out) {
            out->xp_slot = xp_slot;
            out->xp_eid = xp_eid;
            out->xp_value = value;
        }
    }
    return GM_SHEEP_MATE_BORN;
}

static int tick_living_death_progress(
        GmMobLive *m, EwStore *s, int slot, int do_mob_loot,
        uint64_t *world_random_seed48,
        uint64_t *math_random_seed48, int *next_entity_id) {
    if (!m || !s || slot <= 0 || slot >= EW_MAX_ENTITIES
            || !m->entity_dead[slot] || s->health[slot] > 0.0F)
        return 0;
    ++m->entity_death_time[slot];
    if (m->entity_death_time[slot] >= 20) {
        /* EntityLivingBase.onDeathUpdate creates XP before setDead and before
         * terminal particles. Adult EntityAnimal returns
         * 1 + World.rand.nextInt(3), which is one unsplit orb. */
        if (do_mob_loot && m->entity_recently_hit[slot] > 0
                && gm_passive(s->type[slot]))
            (void)terminal_xp_spawn(
                m, s->x[slot], s->y[slot], s->z[slot],
                m->entity_dimension[slot], world_random_seed48,
                math_random_seed48, next_entity_id);
        GmMobTerminalParticles batch = {
            .eid = s->id[slot],
            .dimension = m->entity_dimension[slot],
            .particle_id = 0,
            .ignore_range = 1,
            .parameter_count = 0
        };
        float width, height;
        ehs_size_scaled(s->type[slot], m->size[slot], &width, &height);
        /* EntityLivingBase.onDeathUpdate: three double Gaussian velocities,
         * then three float position draws widened only after multiplication. */
        for (int particle = 0; particle < 20; ++particle) {
            GmTerminalParticle *out = &batch.particles[particle];
            out->vx = jrand_gaussian_next(
                &m->entity_random[slot]) * 0.02;
            out->vy = jrand_gaussian_next(
                &m->entity_random[slot]) * 0.02;
            out->vz = jrand_gaussian_next(
                &m->entity_random[slot]) * 0.02;
            float offset_x = jrand_float(
                &m->entity_random[slot].random) * width * 2.0F;
            float offset_y = jrand_float(
                &m->entity_random[slot].random) * height;
            float offset_z = jrand_float(
                &m->entity_random[slot].random) * width * 2.0F;
            out->x = s->x[slot] + (double)offset_x - (double)width;
            out->y = s->y[slot] + (double)offset_y;
            out->z = s->z[slot] + (double)offset_z - (double)width;
        }
        terminal_particle_append(m, &batch);
        return 2;
    }
    return 1;
}

static int tick_controlled_death(
        GmMobLive *m, EwStore *s, int slot, int do_mob_loot,
        uint64_t *world_random_seed48,
        uint64_t *math_random_seed48, int *next_entity_id) {
    if (!m || !s || slot <= 0 || slot >= EW_MAX_ENTITIES
            || !m->controlled_no_ai[slot] || s->health[slot] > 0.0F)
        return 0;
    m->entity_dead[slot] = 1;
    return tick_living_death_progress(
        m, s, slot, do_mob_loot, world_random_seed48,
        math_random_seed48, next_entity_id);
}

static void retire_terminal_living(GmMobLive *m, EwStore *s, int slot) {
    if (m->pig_ride == slot) {
        m->pig_ride = -1;
        m->pig_vehicle_server.valid = 0;
    }
    /* Entity.setDead removes the entity from World but does not zero its data
     * manager or pig fields.  Slot reset clears these when a later entity
     * actually reuses the storage. */
    loaded_invalidate_living(m, slot);
    s->alive[slot] = 0;
    s->type[slot] = EW_TYPE_NONE;
    m->controlled_no_ai[slot] = 0;
    m->controlled_block_collisions[slot] = 0;
}

static void pig_update_passenger_pose(
        const EwStore *s, int slot, PsvPlayer *player, int ox, int oz) {
    /* Entity.updatePassenger after the ridden player's update:
     * (double)pig.height*.75 + EntityPlayer.getYOffset. */
    player->ent.posX = s->x[slot] - ox;
    player->ent.posY = s->y[slot] + (double)0.9F * 0.75D - 0.35D;
    player->ent.posZ = s->z[slot] - oz;
    player->ent.box = psv_player_box(
        player->ent.posX, player->ent.posY, player->ent.posZ);
    player->ent.motionX = 0.0;
    player->ent.motionY = 0.0;
    player->ent.motionZ = 0.0;
    player->fall_distance = 0.0F;
}

static void animal_entity_base_tick(
    GmMobLive *m, const EwStore *s, int slot);
static void chicken_living_tail(
    GmMobLive *m, EwStore *s, int slot,
    uint64_t *math_random_seed48, int *next_entity_id,
    GmLiveSim *drops);

static void animal_age_love_tail(
        GmMobLive *m, EwStore *s, int slot) {
    if (m->growing_age[slot] < 0) ++m->growing_age[slot];
    else if (m->growing_age[slot] > 0) --m->growing_age[slot];
    /* EntityAnimal.onLivingUpdate follows EntityAgeable's server age step.
     * NoAI suppresses tasks and travel, not these lifecycle fields. */
    if (m->growing_age[slot] != 0) {
        m->sheep_in_love[slot] = 0;
        m->sheep_bred_by_player[slot] = 0;
    } else if (m->sheep_in_love[slot] > 0) {
        --m->sheep_in_love[slot];
        if (m->sheep_in_love[slot] % 10 == 0) {
            GmMobParticleBatch batch = {
                .eid = s->id[slot],
                .dimension = m->entity_dimension[slot],
                .particle_id = 34,
                .count = 1
            };
            GmTerminalParticle *particle = &batch.particles[0];
            float width, height;
            ehs_size_scaled(s->type[slot], m->size[slot], &width, &height);
            particle->vx = jrand_gaussian_next(
                &m->entity_random[slot]) * 0.02;
            particle->vy = jrand_gaussian_next(
                &m->entity_random[slot]) * 0.02;
            particle->vz = jrand_gaussian_next(
                &m->entity_random[slot]) * 0.02;
            particle->x = s->x[slot] + (double)(
                jrand_float(&m->entity_random[slot].random)
                    * width * 2.0F) - (double)width;
            particle->y = s->y[slot] + 0.5 + (double)(
                jrand_float(&m->entity_random[slot].random) * height);
            particle->z = s->z[slot] + (double)(
                jrand_float(&m->entity_random[slot].random)
                    * width * 2.0F) - (double)width;
            mob_particle_batch_append(m, &batch);
        }
    }
}

void gm_mobs_tick_controlled(GmMobLive *m, GmWorld *w,
                             const struct Chunk *window,
                             struct PsvPlayer *player, int ox, int oz,
                             int dimension, int do_mob_loot,
                             uint64_t *world_random_seed48,
                             uint64_t *math_random_seed48,
                             int *next_entity_id) {
    if (!m || !w || !player) return;
    m->active_dimension = dimension;
    EwStore *now = now_store(m), *next = next_store(m);
    ew_store_copy(next, now);
    loaded_order_prepare(m);
    unsigned char ticked_xp[GM_XP_ORBS] = {0};
    McAABB xp_player = xp_player_collision_box(
        (PsvPlayer *)player, ox, oz);
    m->xp_collision_count = 0;
    m->tick_update_order_count = 0;
    int initial_loaded_count = m->loaded_order_count;
    for (int loaded_index = 0;
            loaded_index < initial_loaded_count; ++loaded_index) {
        GmMobLoadedRef ref = m->loaded_order[loaded_index];
        if (ref.kind == GM_MOB_LOADED_XP) {
            int xp_slot = ref.slot;
            if (xp_slot < 0 || xp_slot >= GM_XP_ORBS
                    || ref.generation != m->xp_loaded_generation[xp_slot]
                    || m->xp_orbs[xp_slot].dead
                    || m->xp_orbs[xp_slot].xpValue <= 0
                    || m->xp_orbs[xp_slot].eid != ref.eid
                    || m->orb_dimension[xp_slot] != dimension)
                continue;
            if (m->tick_update_order_count < GM_MOB_UPDATE_ORDER_CAPACITY)
                m->tick_update_order[m->tick_update_order_count++] = ref.eid;
            ticked_xp[xp_slot] = 1;
            tick_xp_orb_slot(
                m, w, (PsvPlayer *)player, ox, oz, &xp_player, xp_slot);
            continue;
        }
        int i = ref.slot;
        if (ref.kind != GM_MOB_LOADED_LIVING
                || i <= 0 || i >= EW_MAX_ENTITIES
                || ref.generation != m->living_loaded_generation[i]
                || !m->controlled_no_ai[i] || !next->alive[i]
                || next->id[i] != ref.eid
                || m->entity_dimension[i] != dimension)
            continue;
        if (m->tick_update_order_count < GM_MOB_UPDATE_ORDER_CAPACITY)
            m->tick_update_order[m->tick_update_order_count++] = ref.eid;
        tick_controlled_dead_fire(m, next, i);
        (void)tick_mob_air(m, w, next, i, NULL);
        if (gm_passive(next->type[i]) || gm_npc(next->type[i]))
            animal_entity_base_tick(m, next, i);
        if (m->entity_hurt_time[i] > 0) --m->entity_hurt_time[i];
        if (m->entity_hurt_resistant[i] > 0)
            --m->entity_hurt_resistant[i];
        if (next->type[i] == EW_TYPE_BOAT) {
            if (m->boat_damage[i] > 0.0F) m->boat_damage[i] -= 1.0F;
            tick_boat(
                m, w, next, i, (PsvPlayer *)player, ox, oz,
                0.0F, 0.0F, 1);
            continue;
        }
        if (!m->controlled_block_collisions[i]) {
            /* EntityLiving with NoAI reports !isServerWorld: onLivingUpdate
             * damps existing motion but its travel branch does not move. */
            next->vx[i] *= 0.98D;
            next->vy[i] *= 0.98D;
            next->vz[i] *= 0.98D;
            if (fabs(next->vx[i]) < 0.003D) next->vx[i] = 0.0D;
            if (fabs(next->vy[i]) < 0.003D) next->vy[i] = 0.0D;
            if (fabs(next->vz[i]) < 0.003D) next->vz[i] = 0.0D;
        }
        int death_state = tick_controlled_death(
            m, next, i, do_mob_loot, world_random_seed48,
            math_random_seed48, next_entity_id);
        if (m->entity_recently_hit[i] > 0)
            --m->entity_recently_hit[i];
        else
            m->entity_attacking_player[i] = 0;
        if (m->entity_effect_count[i] > 0)
            tick_mob_potion_effects(m, next, i, NULL);
        if (death_state == 2) {
            if (next->type[i] == EW_TYPE_PIG && m->pig_ride == i) {
                gm_mobs_pig_dismount_explicit(
                    m, w, window, player, ox, oz);
                ((PsvPlayer *)player)->fall_distance = 0.0F;
            }
            retire_terminal_living(m, next, i);
        } else if (death_state == 1
                && next->type[i] == EW_TYPE_PIG && m->pig_ride == i) {
            pig_update_passenger_pose(
                next, i, (PsvPlayer *)player, ox, oz);
        } else if (death_state == 0) {
            if (gm_passive(next->type[i]))
                animal_age_love_tail(m, next, i);
            if (next->type[i] == EW_TYPE_CHICKEN)
                chicken_living_tail(
                    m, next, i, math_random_seed48,
                    next_entity_id, NULL);
        }
    }
    for (int xp_slot = 0; xp_slot < GM_XP_ORBS; ++xp_slot)
        if (!ticked_xp[xp_slot]
                && !m->xp_orbs[xp_slot].dead
                && m->xp_orbs[xp_slot].xpValue > 0
                && m->orb_dimension[xp_slot] == dimension) {
            if (m->tick_update_order_count < GM_MOB_UPDATE_ORDER_CAPACITY)
                m->tick_update_order[m->tick_update_order_count++] =
                    m->xp_orbs[xp_slot].eid;
            tick_xp_orb_slot(
                m, w, (PsvPlayer *)player, ox, oz, &xp_player, xp_slot);
        }
    if (m->player_ticks_since_last_swing < 1000000000)
        ++m->player_ticks_since_last_swing;
    ++m->tick;
    m->current ^= 1;
    loaded_order_compact(m);
}

int gm_mobs_register_spawner(GmMobLive *m, int x, int y, int z, int entity_type) {
    if (!m || !entity_type) return -1;
    for (int i = 0; i < GM_SPAWNERS; ++i) {
        if (m->spawners[i].active && m->spawners[i].x == x &&
            m->spawners[i].y == y && m->spawners[i].z == z &&
            m->spawners[i].dimension == m->active_dimension) {
            m->spawners[i].entity_type = entity_type;
            return i;
        }
    }
    for (int i = 0; i < GM_SPAWNERS; ++i) {
        if (m->spawners[i].active) continue;
        m->spawners[i].active = 1;
        m->spawners[i].dimension = m->active_dimension;
        m->spawners[i].x = x; m->spawners[i].y = y; m->spawners[i].z = z;
        m->spawners[i].entity_type = entity_type;
        m->spawners[i].delay = 20;
        m->spawners[i].min_delay = 200;
        m->spawners[i].max_delay = 800;
        m->spawners[i].spawn_count = 4;
        m->spawners[i].max_nearby = 6;
        m->spawners[i].spawn_range = 4;
        m->spawners[i].activate_range = 16;
        return i;
    }
    return -1;
}

static int count_type_near(const GmMobLive *m,const EwStore *s, int type,
                           double x, double y, double z, double r) {
    int n = 0; double r2 = r * r;
    for (int i = 1; i < EW_MAX_ENTITIES; ++i) {
        if (!s->alive[i] || s->type[i] != type) continue;
        if (m->entity_dimension[i] != m->active_dimension) continue;
        double dx = s->x[i] - x, dy = s->y[i] - y, dz = s->z[i] - z;
        if (dx * dx + dy * dy + dz * dz <= r2) ++n;
    }
    return n;
}

/* Discover block-52 spawners and stamp entity id from dimension/structure. */
static void discover_spawners(GmMobLive *m, GmWorld *w, double px, double py, double pz,
                              int dimension) {
    int pcx = mc_floor(px), pcy = mc_floor(py), pcz = mc_floor(pz);
    for (int x = pcx - 24; x <= pcx + 24; ++x)
        for (int y = pcy - 12; y <= pcy + 12; ++y)
            for (int z = pcz - 24; z <= pcz + 24; ++z) {
                if (y < 0 || y > 255) continue;
                if (gm_world_block(w, x, y, z) != 52) continue;
                int known = 0;
                for (int i = 0; i < GM_SPAWNERS; ++i)
                    if (m->spawners[i].active && m->spawners[i].x == x &&
                        m->spawners[i].y == y && m->spawners[i].z == z &&
                        m->spawners[i].dimension == dimension) {
                        known = 1; break;
                    }
                if (known) continue;
                int et = 0;
                if (dimension == -1) {
                    /* Fortress spawners are blaze; detect nearby nether brick. */
                    int bricks = 0;
                    for (int dx = -4; dx <= 4 && !bricks; ++dx)
                        for (int dy = -2; dy <= 2 && !bricks; ++dy)
                            for (int dz = -4; dz <= 4; ++dz)
                                if (gm_world_block(w, x + dx, y + dy, z + dz) == 112) {
                                    bricks = 1; break;
                                }
                    et = bricks ? EW_TYPE_BLAZE : 0;
                } else if (dimension == 0) {
                    int dungeon_kind = 0;
                    if (popmc_dungeon_spawner_info(
                            m->seed, x, y, z, &dungeon_kind)) {
                        et = dungeon_kind == POPMC_DUNGEON_MOB_SKELETON
                            ? EW_TYPE_SKELETON
                            : dungeon_kind == POPMC_DUNGEON_MOB_SPIDER
                                ? EW_TYPE_SPIDER : EW_TYPE_ZOMBIE;
                    } else if (popmc_mineshaft_spawner_info(
                                   m->seed, x, y, z)) {
                        et = EW_TYPE_CAVE_SPIDER;
                    } else {
                        /* Stronghold portal-room silverfish spawner. */
                        int frames = 0;
                        for (int dx = -8; dx <= 8; ++dx)
                            for (int dy = -4; dy <= 4; ++dy)
                                for (int dz = -8; dz <= 8; ++dz)
                                    if (gm_world_block(w, x + dx, y + dy, z + dz) == 120) ++frames;
                        et = frames >= 4 ? EW_TYPE_SILVERFISH : EW_TYPE_ZOMBIE;
                    }
                }
                if (et) gm_mobs_register_spawner(m, x, y, z, et);
            }
}

static void tick_spawners(GmMobLive *m, GmWorld *w, EwStore *s,
                          double px, double py, double pz) {
    for (int i = 0; i < GM_SPAWNERS; ++i) {
        GmSpawnerTE *sp = &m->spawners[i];
        if (!sp->active || sp->dimension != m->active_dimension) continue;
        if (gm_world_block(w, sp->x, sp->y, sp->z) != 52) {
            sp->active = 0; continue;
        }
        double dx = px - (sp->x + 0.5), dy = py - (sp->y + 0.5), dz = pz - (sp->z + 0.5);
        double r = (double)sp->activate_range;
        if (dx * dx + dy * dy + dz * dz >= r * r) continue; /* strict < range */
        if (sp->delay > 0) { --sp->delay; continue; }
        int nearby = count_type_near(m,s, sp->entity_type, sp->x + 0.5, sp->y + 0.5,
                                     sp->z + 0.5, 16.0);
        if (nearby >= sp->max_nearby) {
            u64 h = mc_hash_seed((u64)m->seed, m->tick, sp->x, sp->y, sp->z, 0x52455345u);
            int span = sp->max_delay - sp->min_delay;
            sp->delay = sp->min_delay + (span > 0 ? (int)mc_hash_bound(h, span) : 0);
            continue;
        }
        int spawned = 0;
        for (int k = 0; k < sp->spawn_count && living_count(m,s) < GM_MOB_CAPACITY; ++k) {
            u64 h = mc_hash_seed((u64)m->seed, m->tick, sp->x, sp->y, sp->z, 0x53504157u + k);
            double r1 = (double)mc_hash_f01(h);
            double r2 = (double)mc_hash_f01(mc_hash64(h));
            double sx = sp->x + (r1 - r2) * sp->spawn_range + 0.5;
            int yoff = (int)mc_hash_bound(mc_hash64(h + 2), 3) - 1;
            double sy = sp->y + yoff;
            double r3 = (double)mc_hash_f01(mc_hash64(h + 3));
            double r4 = (double)mc_hash_f01(mc_hash64(h + 4));
            double sz = sp->z + (r3 - r4) * sp->spawn_range + 0.5;
            int bx = mc_floor(sx), by = mc_floor(sy), bz = mc_floor(sz);
            if (solid_id(gm_world_block(w, bx, by, bz)) ||
                solid_id(gm_world_block(w, bx, by + 1, bz))) continue;
            int sz_slime = 2;
            if (sp->entity_type == EW_TYPE_MAGMA || sp->entity_type == EW_TYPE_SLIME)
                sz_slime = (int[]){1, 2, 4}[mc_hash_bound(mc_hash64(h + 5), 3)];
            int slot = ew_store_spawn(s, (u8)sp->entity_type, m->next_id++,
                                      sx, sy, sz,
                                      max_health(sp->entity_type, sz_slime));
            if (slot < 0) break;
            m->entity_dimension[slot]=(signed char)m->active_dimension;
            m->size[slot] = (unsigned char)sz_slime;
            reset_slot_state_s(m, s, slot);
            (void)loaded_append_living(m, s, slot);
            spawned = 1;
        }
        if (spawned || nearby >= sp->max_nearby) {
            u64 h = mc_hash_seed((u64)m->seed, m->tick, sp->x, sp->y, sp->z, 0x52455345u);
            int span = sp->max_delay - sp->min_delay;
            sp->delay = sp->min_delay + (span > 0 ? (int)mc_hash_bound(h, span) : 0);
        } else {
            sp->delay = 20; /* retry soon if no space/block */
        }
    }
}

static int in_fortress_bricks(GmWorld *w, int x, int y, int z) {
    for (int dx = -8; dx <= 8; ++dx)
        for (int dy = -4; dy <= 4; ++dy)
            for (int dz = -8; dz <= 8; ++dz)
                if (gm_world_block(w, x + dx, y + dy, z + dz) == 112) return 1;
    return 0;
}

static void passive_spawn(GmMobLive *m, GmWorld *w, EwStore *s,
                          double px, double py, double pz,
                          uint64_t *world_random_seed48) {
    if (m->tick == 0 || (m->tick % 200) ||
        passive_count(m,s) >= GM_NATURAL_PASSIVE_CAP) return;
    for (int a = 0; a < 8; ++a) {
        u64 h = mc_hash_seed((u64)m->seed, m->tick, a, 0, 0, 0x50415353u);
        int dx = 12 + mc_hash_bound(h, 9), dz = mc_hash_bound(mc_hash64(h), 17) - 8;
        if (h & 1ULL) dx = -dx;
        int x = mc_floor(px) + dx, z = mc_floor(pz) + dz;
        gm_world_ensure(w, x >> 4, z >> 4, 0);
        int y = gm_world_surface_y(w, x, z);
        double vx = x + 0.5 - px, vy = y - py, vz = z + 0.5 - pz;
        if (vx * vx + vy * vy + vz * vz > 24.0 * 24.0 || gm_world_block(w, x, y - 1, z) != 2 ||
            gm_world_block(w, x, y, z) || gm_world_block(w, x, y + 1, z)) continue;
        int types[4] = {EW_TYPE_SHEEP, EW_TYPE_PIG, EW_TYPE_COW, EW_TYPE_CHICKEN};
        int type = types[mc_hash_bound(mc_hash64(h + 1), 4)];
        int slot = ew_store_spawn(s, (u8)type, m->next_id++, x + 0.5, y, z + 0.5,
                                  max_health(type, 1));
        if (slot >= 0) {
            m->entity_dimension[slot]=(signed char)m->active_dimension;
            reset_slot_state_s(m, s, slot);
            (void)loaded_append_living(m, s, slot);
            if (type == EW_TYPE_SHEEP && world_random_seed48) {
                JavaRandom world_random;
                jrand_set_seed48(&world_random, *world_random_seed48);
                m->sheep_data[slot] = (unsigned char)
                    gm_mobs_random_sheep_color(&world_random);
                *world_random_seed48 = world_random.seed;
            }
        }
        return;
    }
}

static void nether_natural_spawn(GmMobLive *m, GmWorld *w, EwStore *s,
                                 double px, double py, double pz) {
    if ((m->tick % 40) || alive_count(m,s) >= GM_NATURAL_HOSTILE_CAP) return;
    for (int a = 0; a < 6; ++a) {
        u64 h = mc_hash_seed((u64)m->seed, m->tick, a, 0, 0, 0x4E455448u);
        int dx = 16 + mc_hash_bound(h, 17), dz = mc_hash_bound(mc_hash64(h), 33) - 16;
        if (h & 1ULL) dx = -dx;
        int x = mc_floor(px) + dx, z = mc_floor(pz) + dz;
        gm_world_ensure(w, x >> 4, z >> 4, 0);
        int y = mc_floor(py) + (int)mc_hash_bound(mc_hash64(h + 2), 9) - 4;
        if (y < 5) y = 5;
        if (y > 120) y = 120;
        if (solid_id(gm_world_block(w, x, y, z)) || solid_id(gm_world_block(w, x, y + 1, z)))
            continue;
        if (!solid_id(gm_world_block(w, x, y - 1, z))) continue;
        double ddx = x + 0.5 - px, ddy = y - py, ddz = z + 0.5 - pz;
        double ds = ddx * ddx + ddy * ddy + ddz * ddz;
        if (ds < 16.0 * 16.0 || ds > 48.0 * 48.0) continue;

        int type;
        int fortress = in_fortress_bricks(w, x, y, z);
        if (fortress) {
            /* MapGenNetherBridge spawn list weights: blaze 10, pigman 5,
             * wither skeleton 8, skeleton 2, magma 3. Total 28. */
            int roll = (int)mc_hash_bound(mc_hash64(h + 3), 28);
            if (roll < 10) type = EW_TYPE_BLAZE;
            else if (roll < 15) type = EW_TYPE_PIGMAN;
            else if (roll < 23) type = EW_TYPE_WITHER_SKELETON;
            else if (roll < 25) type = EW_TYPE_SKELETON;
            else type = EW_TYPE_MAGMA;
        } else {
            /* BiomeHell: ghast 50, pigman 100, magma 2, enderman 1. Total 153. */
            int roll = (int)mc_hash_bound(mc_hash64(h + 3), 153);
            if (roll < 50) type = EW_TYPE_GHAST;
            else if (roll < 150) type = EW_TYPE_PIGMAN;
            else if (roll < 152) type = EW_TYPE_MAGMA;
            else type = EW_TYPE_ENDERMAN;
        }
        int sz = 2;
        if (type == EW_TYPE_MAGMA)
            sz = (int[]){1, 2, 4}[mc_hash_bound(mc_hash64(h + 4), 3)];
        if (type == EW_TYPE_GHAST) y += 4; /* fly above terrain */
        int slot = ew_store_spawn(s, (u8)type, m->next_id++, x + 0.5, y, z + 0.5,
                                  max_health(type, sz));
        if (slot >= 0) {
            m->entity_dimension[slot]=(signed char)m->active_dimension;
            m->size[slot] = (unsigned char)sz;
            reset_slot_state_s(m, s, slot);
            (void)loaded_append_living(m, s, slot);
        }
        return;
    }
}

static void slime_spawn(GmMobLive *m, GmWorld *w, EwStore *s,
                        double px, double py, double pz) {
    if ((m->tick % 80) || alive_count(m,s) >= GM_NATURAL_HOSTILE_CAP) return;
    for (int a = 0; a < 4; ++a) {
        u64 h = mc_hash_seed((u64)m->seed, m->tick, a, 0, 0, 0x534C494Du);
        int dx = 16 + mc_hash_bound(h, 17), dz = mc_hash_bound(mc_hash64(h), 33) - 16;
        if (h & 1ULL) dx = -dx;
        int x = mc_floor(px) + dx, z = mc_floor(pz) + dz;
        int cx = x >> 4, cz = z >> 4;
        gm_world_ensure(w, cx, cz, 0);
        int biome = gm_world_biome(w, x, z);
        int y;
        int ok = 0;
        if (biome == B_SWAMP) {
            /* Swamp slime: y 50..70, light-gated. */
            y = 50 + (int)mc_hash_bound(mc_hash64(h + 1), 21);
            if (y < 50 || y > 70) continue;
            if (gm_world_block_light(w, x, y, z) > 7) continue;
            if (!solid_id(gm_world_block(w, x, y - 1, z))) continue;
            if (solid_id(gm_world_block(w, x, y, z))) continue;
            ok = 1;
        } else if (is_slime_chunk(m->seed, cx, cz)) {
            /* Slime chunk: y < 40, 1/10 of spawn attempts already in vanilla
             * via chunk RNG; we already gated by is_slime_chunk. */
            y = 5 + (int)mc_hash_bound(mc_hash64(h + 1), 35);
            if (y >= 40) continue;
            if (!solid_id(gm_world_block(w, x, y - 1, z))) continue;
            if (solid_id(gm_world_block(w, x, y, z)) || solid_id(gm_world_block(w, x, y + 1, z)))
                continue;
            ok = 1;
        }
        if (!ok) continue;
        int sz = (int[]){1, 2, 4}[mc_hash_bound(mc_hash64(h + 2), 3)];
        int slot = ew_store_spawn(s, EW_TYPE_SLIME, m->next_id++, x + 0.5, y, z + 0.5,
                                  max_health(EW_TYPE_SLIME, sz));
        if (slot >= 0) {
            m->entity_dimension[slot]=(signed char)m->active_dimension;
            m->size[slot] = (unsigned char)sz;
            reset_slot_state_s(m, s, slot);
            (void)loaded_append_living(m, s, slot);
        }
        return;
    }
}

/* Route-roster weighted pick — NOT Java-exact WorldEntitySpawner pack loop.
 * Approximate biome monster weights for supported types only (zombie 95,
 * skeleton/creeper/spider 100, enderman 10). Full pack enumeration, chunk
 * RNG order, and type-specific EntityAITasks remain open (OPEN_DIVERGENCES). */
static int overworld_hostile_weighted(JavaRandom *r) {
    int roll = jrand_int_bound(r, 405);
    if (roll < 95) return EW_TYPE_ZOMBIE;
    if (roll < 195) return EW_TYPE_SKELETON;
    if (roll < 295) return EW_TYPE_CREEPER;
    if (roll < 395) return EW_TYPE_SPIDER;
    return EW_TYPE_ENDERMAN;
}

static void natural_spawn(GmMobLive *m, GmWorld *w, EwStore *s,
                          double px, double py, double pz, int dimension,
                          long long world_time,
                          uint64_t *world_random_seed48) {
    discover_spawners(m, w, px, py, pz, dimension);
    tick_spawners(m, w, s, px, py, pz);

    if (dimension == -1) {
        nether_natural_spawn(m, w, s, px, py, pz);
        return;
    }
    if (dimension != 0) return;

    /* Simplified natural spawn (not Java WorldEntitySpawner call-order parity):
     * slime pocket, then day creature / night monster attempt. */
    slime_spawn(m, w, s, px, py, pz);

    int tod = (int)(world_time % 24000LL); if (tod < 0) tod += 24000;
    if (tod < 12000) {
        passive_spawn(m, w, s, px, py, pz, world_random_seed48);
        return;
    }
    if (tod < 13000 || tod > 23000 || (m->tick % 20) ||
        alive_count(m,s) >= GM_NATURAL_HOSTILE_CAP) return;
    JavaRandom rng;
    jrand_set(&rng, (i64)m->seed ^ ((i64)m->tick * 6364136223846793005LL) ^ 0x4d4f4253LL);
    for (int a = 0; a < 8; ++a) {
        int dx = 24 + jrand_int_bound(&rng, 9);
        int dz = jrand_int_bound(&rng, 17) - 8;
        if (jrand_int_bound(&rng, 2) == 0) dx = -dx;
        int x = mc_floor(px) + dx, z = mc_floor(pz) + dz;
        gm_world_ensure(w, x >> 4, z >> 4, 0);
        int y = gm_world_surface_y(w, x, z);
        double ddx = (x + 0.5) - px, ddy = y - py, ddz = (z + 0.5) - pz;
        double ds = ddx * ddx + ddy * ddy + ddz * ddz;
        if (ds < 24.0 * 24.0 || ds > 32.0 * 32.0 || !solid_id(gm_world_block(w, x, y - 1, z)) ||
            gm_world_block(w, x, y, z) != 0 || gm_world_block(w, x, y + 1, z) != 0 ||
            gm_world_block_light(w, x, y, z) > 7) continue;
        int type = overworld_hostile_weighted(&rng);
        int slot = ew_store_spawn(s, (u8)type, m->next_id++, x + 0.5, y, z + 0.5,
                                  max_health(type, 1));
        if (slot >= 0) {
            m->entity_dimension[slot]=(signed char)m->active_dimension;
            s->cx[slot] = x >> 4; s->cz[slot] = z >> 4;
            reset_slot_state_s(m, s, slot);
            (void)loaded_append_living(m, s, slot);
        }
        return;
    }
}

/* Status: 0 IN_WATER, 1 ON_LAND, 2 IN_AIR (subset of EntityBoat.Status). */
static int boat_status(GmWorld *w, double x, double y, double z) {
    /* Feet sample: water in body cell => IN_WATER; solid below empty feet => ON_LAND. */
    int bx = mc_floor(x), by = mc_floor(y), bz = mc_floor(z);
    int feet = gm_world_block(w, bx, by, bz);
    int below = gm_world_block(w, bx, by - 1, bz);
    int head = gm_world_block(w, bx, mc_floor(y + 0.5625), bz);
    if (feet == 8 || feet == 9 || head == 8 || head == 9) return 0; /* IN_WATER */
    if (solid_id(below) && feet == 0) return 1; /* ON_LAND */
    return 2; /* IN_AIR */
}

static void tick_boat(GmMobLive *m, GmWorld *w, EwStore *nx, int i,
                      PsvPlayer *p, int ox, int oz, float forward,
                      float strafe, int no_gravity) {
    /* Boat half-extents for AABB collision (1.375 wide, 0.5625 tall). */
    const double half_w = 1.375 * 0.5;
    const double height = 0.5625;
    int status = boat_status(w, nx->x[i], nx->y[i], nx->z[i]);
    float momentum = 0.05f;
    double d1 = no_gravity ? 0.0 : -0.03999999910593033; /* gravity */
    double d2 = 0.0;                  /* buoyancy factor */

    if (status == 0) { /* IN_WATER */
        momentum = 0.9f;
        /* Approximate waterLevel - minY: partial submersion buoyancy. */
        {
            int by = mc_floor(nx->y[i]);
            double water_level = (double)by + 1.0;
            d2 = (water_level - nx->y[i]) / height;
            if (d2 < 0.0) d2 = 0.0;
            if (d2 > 1.0) d2 = 1.0;
        }
    } else if (status == 1) { /* ON_LAND */
        if (s_boat_glide[i] <= 0.0f) s_boat_glide[i] = 0.8f; /* default land glide */
        momentum = s_boat_glide[i];
        if (m->boat_ride == i)
            s_boat_glide[i] *= 0.5f; /* player-controlled land glide halves each tick */
    } else { /* IN_AIR */
        momentum = 0.9f;
    }

    /* updateMotion: apply momentum then gravity/buoyancy. */
    nx->vx[i] *= (double)momentum;
    nx->vz[i] *= (double)momentum;
    s_boat_delta_rot[i] *= momentum;
    nx->vy[i] += d1;
    if (d2 > 0.0) {
        nx->vy[i] += d2 * 0.06153846016296973;
        nx->vy[i] *= 0.75;
    }

    /* controlBoat when ridden: no auto-thrust without forward/back input. */
    if (m->boat_ride == i && p) {
        int left = strafe < -0.01f, right = strafe > 0.01f;
        int fwd = forward > 0.01f, back = forward < -0.01f;
        float f = 0.0f;
        if (left) s_boat_delta_rot[i] += -1.0f;
        if (right) s_boat_delta_rot[i] += 1.0f;
        if (left != right && !fwd && !back) f += 0.005f;
        nx->yaw[i] += s_boat_delta_rot[i];
        if (fwd) f += 0.04f;
        if (back) f -= 0.005f;
        {
            double yr = (double)nx->yaw[i] * 0.017453292;
            nx->vx[i] += -sin(yr) * (double)f;
            nx->vz[i] += cos(yr) * (double)f;
        }
        p->yaw = nx->yaw[i];
        p->ent.posX = nx->x[i] - ox;
        p->ent.posY = nx->y[i] + 0.35;
        p->ent.posZ = nx->z[i] - oz;
        p->ent.motionX = p->ent.motionY = p->ent.motionZ = 0.0;
        p->ent.onGround = (status == 1);
    }

    /* AABB-style collide: sample corners of the 1.375 x 0.5625 box on XZ and Y. */
    {
        double try_x = nx->x[i] + nx->vx[i];
        double try_z = nx->z[i] + nx->vz[i];
        double try_y = nx->y[i] + nx->vy[i];
        int blocked_xz = 0;
        int mid_y = mc_floor(nx->y[i] + height * 0.5);
        double corners[4][2] = {
            { try_x - half_w, try_z - half_w },
            { try_x + half_w, try_z - half_w },
            { try_x - half_w, try_z + half_w },
            { try_x + half_w, try_z + half_w }
        };
        for (int c = 0; c < 4; ++c) {
            if (solid_id(gm_world_block(w, mc_floor(corners[c][0]), mid_y,
                                        mc_floor(corners[c][1])))) {
                blocked_xz = 1; break;
            }
        }
        if (!blocked_xz) {
            nx->x[i] = try_x;
            nx->z[i] = try_z;
        } else {
            nx->vx[i] = nx->vz[i] = 0.0;
        }
        {
            int foot = mc_floor(try_y);
            int head = mc_floor(try_y + height);
            int bx = mc_floor(nx->x[i]), bz = mc_floor(nx->z[i]);
            if (!solid_id(gm_world_block(w, bx, foot, bz)) &&
                !solid_id(gm_world_block(w, bx, head, bz))) {
                nx->y[i] = try_y;
                nx->on_ground[i] = 0;
            } else if (nx->vy[i] < 0) {
                nx->vy[i] = 0;
                nx->on_ground[i] = 1;
                nx->y[i] = (double)foot + 1.0;
            } else {
                nx->vy[i] = 0;
            }
        }
    }
}

static int animal_nearest_mate_slot(
        const GmMobLive *m, const EwStore *s, int initiator, int dimension) {
    int best = -1;
    double best_distance = HUGE_VAL;
    if (!m || !s || initiator <= 0 || initiator >= EW_MAX_ENTITIES
            || !s->alive[initiator] || !gm_passive(s->type[initiator])
            || m->sheep_in_love[initiator] <= 0)
        return -1;
    int animal_type = s->type[initiator];
    for (int slot = 1; slot < EW_MAX_ENTITIES; ++slot) {
        if (slot == initiator || !s->alive[slot]
                || s->type[slot] != animal_type
                || m->entity_dimension[slot] != dimension
                || m->sheep_in_love[slot] <= 0)
            continue;
        double dx = s->x[slot] - s->x[initiator];
        double dy = s->y[slot] - s->y[initiator];
        double dz = s->z[slot] - s->z[initiator];
        if (fabs(dx) >= 8.9 || fabs(dy) >= 9.3 || fabs(dz) >= 8.9)
            continue;
        double distance = dx * dx + dy * dy + dz * dz;
        if (distance < best_distance) {
            best = slot;
            best_distance = distance;
        }
    }
    return best;
}

static void animal_entity_base_tick(GmMobLive *m, const EwStore *s, int slot) {
    JavaRandom *random = &m->entity_random[slot].random;
    m->entity_last_tick_x[slot] = s->x[slot];
    m->entity_last_tick_y[slot] = s->y[slot];
    m->entity_last_tick_z[slot] = s->z[slot];
    m->entity_prev_x[slot] = s->x[slot];
    m->entity_prev_y[slot] = s->y[slot];
    m->entity_prev_z[slot] = s->z[slot];
    ++m->entity_ticks_existed[slot];
    /* EntityLiving.onEntityUpdate short-circuits the ambient-sound roll when
     * isEntityAlive is false.  A health-zero corpse still advances the base
     * entity tick and deathTime, but its private RNG is reserved for the
     * terminal particles. */
    if (s->health[slot] > 0.0F && jrand_int_bound(random, 1000)
            < m->entity_living_sound_time[slot]++) {
        m->entity_living_sound_time[slot] =
            s->type[slot] == EW_TYPE_VILLAGER ? -80 : -120;
        (void)jrand_float(random);
        (void)jrand_float(random);
    }
}

/* Negative lower-priority animal task path. EntityAITasks evaluates wander,
 * watch-player, and idle-look in that order after mate/follow decline (and,
 * for sheep, after grazing also declines).
 * The accepted task effects remain owned by their later feature slices; this
 * helper preserves the exact cursor whenever those rolls decline. */
static void animal_declined_lower_task_rng(
        GmMobLive *m, int slot, double player_distance_sq) {
    JavaRandom *random = &m->entity_random[slot].random;
    if (jrand_int_bound(random, 120) == 0)
        return;
    if (jrand_float(random) < 0.02F && player_distance_sq <= 36.0) {
        (void)jrand_int_bound(random, 40);
        return;
    }
    if (jrand_float(random) < 0.02F) {
        (void)jrand_double(random);
        (void)jrand_int_bound(random, 20);
    }
}

/* EntityChicken.onLivingUpdate runs after the complete EntityAnimal tail, so
 * it observes post-collision onGround/motionY and the already-updated age. */
static void chicken_living_tail(
        GmMobLive *m, EwStore *s, int slot,
        uint64_t *math_random_seed48, int *next_entity_id,
        GmLiveSim *drops) {
    m->chicken_old_flap[slot] = m->chicken_wing_rotation[slot];
    m->chicken_old_flap_speed[slot] = m->chicken_dest_pos[slot];
    m->chicken_dest_pos[slot] = (float)(
        (double)m->chicken_dest_pos[slot]
            + (double)(s->on_ground[slot] ? -1 : 4) * 0.3);
    if (m->chicken_dest_pos[slot] < 0.0F)
        m->chicken_dest_pos[slot] = 0.0F;
    else if (m->chicken_dest_pos[slot] > 1.0F)
        m->chicken_dest_pos[slot] = 1.0F;
    if (!s->on_ground[slot] && m->chicken_wing_rot_delta[slot] < 1.0F)
        m->chicken_wing_rot_delta[slot] = 1.0F;
    m->chicken_wing_rot_delta[slot] = (float)(
        (double)m->chicken_wing_rot_delta[slot] * 0.9);
    if (!s->on_ground[slot] && s->vy[slot] < 0.0)
        s->vy[slot] *= 0.6;
    m->chicken_wing_rotation[slot] +=
        m->chicken_wing_rot_delta[slot] * 2.0F;

    if (m->growing_age[slot] >= 0 && !m->chicken_jockey[slot]
            && --m->chicken_time_until_next_egg[slot] <= 0) {
        float pitch_first = jrand_float(&m->entity_random[slot].random);
        float pitch_second = jrand_float(&m->entity_random[slot].random);
        float pitch = (pitch_first - pitch_second) * 0.2F + 1.0F;
        mob_event_append(
            m, GM_MOB_EVENT_SOUND, s->id[slot],
            GM_MOB_SOUND_CHICKEN_EGG,
            s->x[slot], s->y[slot], s->z[slot], 1.0F, pitch);
        if (math_random_seed48 && next_entity_id && drops
                && *next_entity_id > 0 && *next_entity_id < INT_MAX) {
            int eid = (*next_entity_id)++;
            if (m->next_id <= eid) m->next_id = eid + 1;
            if (m->next_orb_id <= eid) m->next_orb_id = eid + 1;
            float hover_start = (float)(
                mob_math_random_next_double(math_random_seed48)
                    * (MC_PI * 2.0));
            float yaw = (float)(
                mob_math_random_next_double(math_random_seed48) * 360.0);
            double motion_x = (double)(float)(
                mob_math_random_next_double(math_random_seed48)
                    * 0.20000000298023224 - 0.10000000149011612);
            double motion_z = (double)(float)(
                mob_math_random_next_double(math_random_seed48)
                    * 0.20000000298023224 - 0.10000000149011612);
            /* Items.EGG is numeric item id 344 in the 1.11.2 registry. */
            if (!gm_live_spawn_item_exact_hover(
                drops, eid, s->x[slot], s->y[slot], s->z[slot],
                motion_x, 0.20000000298023224, motion_z,
                yaw, hover_start, 344, 1, 0, 0, 10, 0))
                ++drops->spawn_fail_count;
        }
        m->chicken_time_until_next_egg[slot] =
            jrand_int_bound(&m->entity_random[slot].random, 6000) + 6000;
    }
}

static void living_entity_size(
        const GmMobLive *m, const EwStore *s, int slot,
        double *width, double *height) {
    float w, h;
    ehs_size_scaled(s->type[slot], m->size[slot], &w, &h);
    if (gm_passive(s->type[slot]) && m->growing_age[slot] < 0) {
        w *= 0.5F;
        h *= 0.5F;
    }
    *width = (double)w;
    *height = (double)h;
}

/* The push portion of EntityLivingBase.collideWithNearbyEntities followed by
 * Entity.applyEntityCollision.  GmMobLive has a fixed small entity pool, so
 * the slot scan is bounded and adds no allocation or idle no-mob cost.
 * maxEntityCramming damage remains a separate unrepresented boundary. */
static void collide_living_entities(
        GmMobLive *m, EwStore *s, int self, int dimension) {
    double self_width, self_height;
    living_entity_size(m, s, self, &self_width, &self_height);
    double self_half = self_width * 0.5;
    double self_min_x = s->x[self] - self_half;
    double self_max_x = s->x[self] + self_half;
    double self_min_y = s->y[self];
    double self_max_y = s->y[self] + self_height;
    double self_min_z = s->z[self] - self_half;
    double self_max_z = s->z[self] + self_half;

    for (int other = 1; other < EW_MAX_ENTITIES; ++other) {
        if (other == self || !s->alive[other]
                || m->entity_dimension[other] != dimension
                || !gm_living(s->type[other])
                || m->entity_dead[other] || s->health[other] <= 0.0F)
            continue;
        double other_width, other_height;
        living_entity_size(m, s, other, &other_width, &other_height);
        double other_half = other_width * 0.5;
        if (!(s->x[other] + other_half > self_min_x
                && s->x[other] - other_half < self_max_x
                && s->y[other] + other_height > self_min_y
                && s->y[other] < self_max_y
                && s->z[other] + other_half > self_min_z
                && s->z[other] - other_half < self_max_z))
            continue;

        double dx = s->x[self] - s->x[other];
        double dz = s->z[self] - s->z[other];
        double magnitude = fmax(fabs(dx), fabs(dz));
        if (magnitude < 0.009999999776482582) continue;
        double root = (double)(float)sqrt(magnitude);
        dx /= root;
        dz /= root;
        double scale = 1.0 / root;
        if (scale > 1.0) scale = 1.0;
        dx *= scale * 0.05000000074505806;
        dz *= scale * 0.05000000074505806;
        s->vx[other] -= dx;
        s->vz[other] -= dz;
        s->vx[self] += dx;
        s->vz[self] += dz;
    }
}

static void animal_tick_newborn_same_boundary(
        GmMobLive *m, GmWorld *w, const McSinTable *st, EwStore *s,
        int slot, double player_x, double player_y, double player_z,
        int mob_griefing) {
    if (slot <= 0 || slot >= EW_MAX_ENTITIES || !s->alive[slot]
            || !gm_passive(s->type[slot]))
        return;
    animal_entity_base_tick(m, s, slot);
    double dx = player_x - s->x[slot];
    double dy = player_y - s->y[slot];
    double dz = player_z - s->z[slot];
    double player_distance_sq = dx * dx + dy * dy + dz * dz;
    ++m->entity_age[slot];
    if (player_distance_sq < 1024.0)
        m->entity_age[slot] = 0;
    int setup_tick = m->sheep_ai_tick_count[slot]++ % 3 == 0;
    if (setup_tick) {
        if (s->type[slot] == EW_TYPE_SHEEP
                && sheep_graze_begin_slot(m, w, s, slot)) {
            (void)sheep_graze_update_slot(
                m, w, s, slot, mob_griefing != 0);
        } else {
            animal_declined_lower_task_rng(
                m, slot, player_distance_sq);
        }
    }
    move_mob(
        w, NULL, 0, 0, st, m, s, slot,
        0, 0, 0, 0, 0, 0.0F, 0.0F, 0, 0, 1.0);
    collide_living_entities(m, s, slot, m->active_dimension);
    if (m->growing_age[slot] < 0) ++m->growing_age[slot];
    else if (m->growing_age[slot] > 0) --m->growing_age[slot];
    if (m->growing_age[slot] != 0) {
        m->sheep_in_love[slot] = 0;
        m->sheep_bred_by_player[slot] = 0;
    }
    if (s->type[slot] == EW_TYPE_CHICKEN)
        chicken_living_tail(m, s, slot, NULL, NULL, NULL);
}

enum {
    GM_SAME_TICK_ANIMAL = 1,
    GM_SAME_TICK_XP = 2
};

typedef struct {
    unsigned char kind;
    int slot;
} GmSameTickSpawn;

void gm_mobs_tick(GmMobLive *m, GmWorld *w, const struct Chunk *window,
                  const struct McSinTable *st_,
                  struct PsvPlayer *player_, struct PvStats *vitals_,
                  int ox, int oz, int dimension, long long world_time,
                  int mob_griefing, uint64_t *world_random_seed48,
                  uint64_t *math_random_seed48, int *next_entity_id,
                  int do_mob_loot,
                  GmLiveSim *drops,
                  float boat_forward, float boat_strafe) {
    if (!m || !w || !player_ || !vitals_) return;
    m->active_dimension=dimension;
    PsvPlayer *p=(PsvPlayer *)player_; PvStats *v=(PvStats *)vitals_;
    const McSinTable *st=(const McSinTable *)st_;
    EwStore *now=now_store(m), *nx=next_store(m); ew_store_copy(nx,now);
    loaded_order_prepare(m);
    if (m->player_wither_ticks > 0) {
        /* DamageSource.WITHER is unblockable. */
        if (m->player_wither_ticks % 40 == 0)
            (void)gm_mobs_attack_player(m, (struct PvStats *)v,
                                        &p->inv, 1.0f, 1);
        --m->player_wither_ticks;
        p->health = v->health;
    }
    if (m->player_ticks_since_last_swing < 1000000000)
        ++m->player_ticks_since_last_swing;
    double px=p->ent.posX+ox, py=p->ent.posY, pz=p->ent.posZ+oz;
    natural_spawn(
        m,w,nx,px,py,pz,dimension,world_time,world_random_seed48);
    int tod=(int)(world_time%24000LL); if(tod<0)tod+=24000;
    int day=dimension==0&&tod<12000;
    float boat_fwd = m->boat_ride >= 0 ? boat_forward : 0.0f;
    float boat_str = m->boat_ride >= 0 ? boat_strafe : 0.0f;
    GmSameTickSpawn same_tick_spawns[EW_MAX_ENTITIES * 2];
    int same_tick_spawn_count = 0;
    unsigned char spawned_xp[GM_XP_ORBS] = {0};
    unsigned char ticked_xp[GM_XP_ORBS] = {0};
    McAABB xp_player = xp_player_collision_box(p, ox, oz);
    m->xp_collision_count = 0;
    m->tick_update_order_count = 0;

    int initial_loaded_count = m->loaded_order_count;
    for (int loaded_index = 0;
            loaded_index < initial_loaded_count; ++loaded_index) {
        GmMobLoadedRef ref = m->loaded_order[loaded_index];
        if (ref.kind == GM_MOB_LOADED_XP) {
            int xp_slot = ref.slot;
            if (xp_slot < 0 || xp_slot >= GM_XP_ORBS
                    || ref.generation != m->xp_loaded_generation[xp_slot]
                    || m->xp_orbs[xp_slot].dead
                    || m->xp_orbs[xp_slot].xpValue <= 0
                    || m->xp_orbs[xp_slot].eid != ref.eid
                    || m->orb_dimension[xp_slot] != dimension)
                continue;
            if (m->tick_update_order_count < GM_MOB_UPDATE_ORDER_CAPACITY)
                m->tick_update_order[m->tick_update_order_count++] = ref.eid;
            ticked_xp[xp_slot] = 1;
            tick_xp_orb_slot(m, w, p, ox, oz, &xp_player, xp_slot);
            continue;
        }
        int i = ref.slot;
        if (ref.kind != GM_MOB_LOADED_LIVING
                || i <= 0 || i >= EW_MAX_ENTITIES
                || ref.generation != m->living_loaded_generation[i]
                || !now->alive[i] || now->id[i] != ref.eid
                || m->entity_dimension[i] != dimension
                || !gm_living(now->type[i]))
            continue;
        if (m->tick_update_order_count < GM_MOB_UPDATE_ORDER_CAPACITY)
            m->tick_update_order[m->tick_update_order_count++] = now->id[i];
        int type=now->type[i];
        int passive = gm_passive(type);
        int sheep_grazing = 0, animal_mating = 0, sheep_lower_rng = 0;
        int ridden_pig = type == EW_TYPE_PIG && m->pig_ride == i;
        int exact_ridden_server_tick = ridden_pig && window;
        ICStack pig_main = isr_get_stack(
            &p->inv, p->inv.current_item);
        ICStack pig_off = isr_get_stack(&p->inv, ISR_OFFHAND_SLOT);
        int pig_steerable = ridden_pig
            && ((pig_main.item == 398 && pig_main.count > 0)
                || (pig_off.item == 398 && pig_off.count > 0));
        if (exact_ridden_server_tick) {
            tick_ridden_pig_vehicle_server_base(
                m, nx, i, window, ox, oz, pig_steerable,
                p->yaw, p->pitch, math_random_seed48);
        }
        if (passive || gm_npc(type))
            animal_entity_base_tick(m, now, i);
        tick_controlled_dead_fire(m, nx, i);
        if (tick_mob_air(m, w, nx, i, drops)) continue;
        if (type == EW_TYPE_BLAZE)
            nx->vy[i] = ebf_blaze_fall_damping(
                now->on_ground[i] ? 1 : 0, nx->vy[i]);
        /* EntityLivingBase.onUpdate ages both combat timers for every living
         * entity, not only deterministic NoAI oracle fixtures. Without this,
         * a normal mob's first accepted hit left hurtResistantTime at 20
         * forever and all equal-strength follow-up hits were rejected. */
        if (m->entity_hurt_time[i] > 0) --m->entity_hurt_time[i];
        if (m->entity_hurt_resistant[i] > 0)
            --m->entity_hurt_resistant[i];
        if (exact_ridden_server_tick) {
            if (m->entity_server_fire_resistance_ticks[i] > 0)
                --m->entity_server_fire_resistance_ticks[i];
            tick_ridden_pig_server_ambient(m, nx, i);
        }
        /* A controlled living target can be killed by an earlier entity in
         * this same loaded-entity boundary (for example, a falling anvil).
         * EntityLivingBase.onUpdate then advances onDeathUpdate before any
         * living AI/travel work. Loot is an earlier onDeath concern and is
         * intentionally outside this no-loot controlled path. */
        int controlled_death_state = tick_controlled_death(
            m, nx, i, 0, NULL, NULL, NULL);
        int ordinary_pig_death_state = 0;
        if (!controlled_death_state && type == EW_TYPE_PIG
                && !m->controlled_no_ai[i] && m->entity_dead[i]
                && nx->health[i] <= 0.0F)
            ordinary_pig_death_state = tick_living_death_progress(
                m, nx, i, do_mob_loot, world_random_seed48,
                math_random_seed48, next_entity_id);
        if (m->entity_recently_hit[i] > 0)
            --m->entity_recently_hit[i];
        else
            m->entity_attacking_player[i] = 0;
        if (m->entity_effect_count[i] > 0)
            tick_mob_potion_effects(m, nx, i, drops);
        if (!nx->alive[i]) continue;
        if (!ordinary_pig_death_state && type == EW_TYPE_PIG
                && !m->controlled_no_ai[i] && m->entity_dead[i]
                && nx->health[i] <= 0.0F)
            ordinary_pig_death_state = 1;
        int death_state = controlled_death_state
            ? controlled_death_state : ordinary_pig_death_state;
        if (ordinary_pig_death_state) {
            /* onDeathUpdate runs before onLivingUpdate. A normal-AI dead pig
             * blocks AI inputs but still executes the ordinary server travel
             * branch, including gravity, collision, and friction. */
            move_mob(
                w, window, ox, oz, st, m, nx, i, 0, 0, 0,
                0, 0, p->yaw, p->pitch, 1, 0, 1.0);
            collide_living_entities(m, nx, i, dimension);
        }
        if (death_state == 2) {
            if (ridden_pig) {
                gm_mobs_pig_dismount_explicit(
                    m, w, window, player_, ox, oz);
                p->fall_distance = 0.0F;
            }
            retire_terminal_living(m, nx, i);
            continue;
        }
        if (death_state == 1) {
            if (ridden_pig) pig_update_passenger_pose(nx, i, p, ox, oz);
            continue;
        }
        if (m->controlled_no_ai[i]
                && !m->controlled_block_collisions[i]) {
            /* NoAI suppresses travel and tasks, not Entity.onEntityUpdate.
             * Keep the signed fire-counter phase active for controlled
             * fixtures; the potion pass above latched Fire Resistance before
             * decrementing its duration. */
            if (m->fire_ticks[i] > 0) {
                --m->fire_ticks[i];
                if (m->fire_ticks[i] % 20 == 0
                        && !m->entity_fire_resistance_this_tick[i]) {
                    float applied = mob_resistance_damage(m, i, 1.0F);
                    applied = mob_absorb_damage(m, i, applied);
                    mark_hurt(m, nx, i);
                    nx->health[i] -= applied;
                    if (nx->health[i] <= 0.0F)
                        m->entity_dead[i] = 1;
                }
            }
            /* EntityLiving reports !isServerWorld while NoAI is set. Its
             * travel branch damps motion but does not call move(). */
            nx->vx[i] *= 0.98D;
            nx->vy[i] *= 0.98D;
            nx->vz[i] *= 0.98D;
            if (fabs(nx->vx[i]) < 0.003D) nx->vx[i] = 0.0D;
            if (fabs(nx->vy[i]) < 0.003D) nx->vy[i] = 0.0D;
            if (fabs(nx->vz[i]) < 0.003D) nx->vz[i] = 0.0D;
            if (passive) animal_age_love_tail(m, nx, i);
            if (type == EW_TYPE_CHICKEN)
                chicken_living_tail(
                    m, nx, i, math_random_seed48, next_entity_id, drops);
            continue;
        }
        if (passive && !m->controlled_no_ai[i]) {
            double animal_dx = px - now->x[i];
            double animal_dy = py - now->y[i];
            double animal_dz = pz - now->z[i];
            double animal_player_distance_sq =
                animal_dx * animal_dx + animal_dy * animal_dy
                    + animal_dz * animal_dz;
            ++m->entity_age[i];
            if (animal_player_distance_sq < 1024.0)
                m->entity_age[i] = 0;
            int setup_tick = m->sheep_ai_tick_count[i]++ % 3 == 0;
            int mate_reset_this_tick = 0;
            if (m->panic_ticks[i] > 0) {
                m->sheep_mate_active[i] = 0;
                m->sheep_mate_slot[i] = -1;
                m->sheep_mate_delay[i] = 0;
                if (m->sheep_eat_timer[i] > 0)
                    m->sheep_eat_timer[i] = 0;
            } else {
                int mate_slot = m->sheep_mate_slot[i];
                if (m->sheep_mate_active[i]
                        && (mate_slot <= 0 || mate_slot >= EW_MAX_ENTITIES
                            || !nx->alive[mate_slot]
                            || nx->type[mate_slot] != type
                            || m->entity_dimension[mate_slot] != dimension
                            || m->sheep_in_love[mate_slot] <= 0
                            || m->sheep_mate_delay[i] >= 60)) {
                    m->sheep_mate_active[i] = 0;
                    m->sheep_mate_slot[i] = -1;
                    m->sheep_mate_delay[i] = 0;
                    mate_reset_this_tick = 1;
                }
                if (!m->sheep_mate_active[i] && !mate_reset_this_tick
                        && setup_tick
                        && m->sheep_in_love[i] > 0) {
                    mate_slot = animal_nearest_mate_slot(m, nx, i, dimension);
                    if (mate_slot >= 0) {
                        m->sheep_mate_active[i] = 1;
                        m->sheep_mate_slot[i] = mate_slot;
                        m->sheep_mate_delay[i] = 0;
                        /* Higher-priority EntityAIMate conflicts with and
                         * resets a running EntityAIEatGrass task. */
                        if (type == EW_TYPE_SHEEP)
                            m->sheep_eat_timer[i] = 0;
                    }
                }
                if (m->sheep_mate_active[i]) {
                    mate_slot = m->sheep_mate_slot[i];
                    animal_mating = 1;
                    ++m->sheep_mate_delay[i];
                    double mdx = nx->x[i] - nx->x[mate_slot];
                    double mdy = nx->y[i] - nx->y[mate_slot];
                    double mdz = nx->z[i] - nx->z[mate_slot];
                    if (m->sheep_mate_delay[i] >= 60
                            && mdx * mdx + mdy * mdy + mdz * mdz < 9.0) {
                        /* The standalone runtime's event cursor is lazy.
                         * Reconcile it only on this causal spawn boundary, so
                         * inactive breeding adds no global tick mutation. */
                        if (next_entity_id
                                && *next_entity_id < m->next_id)
                            *next_entity_id = m->next_id;
                        GmSheepMateResult birth;
                        int birth_result = animal_breed_slots(
                            m, nx, i, mate_slot, 0, 1,
                            world_random_seed48, math_random_seed48,
                            next_entity_id, do_mob_loot, &birth);
                        if (birth_result == GM_SHEEP_MATE_BORN) {
                            if (birth.child_slot >= 0
                                    && same_tick_spawn_count
                                        < EW_MAX_ENTITIES * 2)
                                same_tick_spawns[same_tick_spawn_count++] =
                                    (GmSameTickSpawn){
                                        GM_SAME_TICK_ANIMAL, birth.child_slot};
                            if (birth.xp_slot >= 0
                                    && birth.xp_slot < GM_XP_ORBS
                                    && same_tick_spawn_count
                                        < EW_MAX_ENTITIES * 2) {
                                spawned_xp[birth.xp_slot] = 1;
                                same_tick_spawns[same_tick_spawn_count++] =
                                    (GmSameTickSpawn){
                                        GM_SAME_TICK_XP, birth.xp_slot};
                            }
                        }
                    }
                } else if (type == EW_TYPE_SHEEP
                        && m->sheep_eat_timer[i] > 0) {
                    (void)sheep_graze_update_slot(
                        m, w, nx, i, mob_griefing != 0);
                    sheep_grazing = 1;
                } else if (type == EW_TYPE_SHEEP && setup_tick) {
                    if (sheep_graze_begin_slot(m, w, nx, i)) {
                        /* EntityAITasks calls updateTask on the start tick. */
                        (void)sheep_graze_update_slot(
                            m, w, nx, i, mob_griefing != 0);
                        sheep_grazing = 1;
                    } else {
                        sheep_lower_rng = 1;
                    }
                }
                if (sheep_lower_rng)
                    animal_declined_lower_task_rng(
                        m, i, animal_player_distance_sq);
                else if (type != EW_TYPE_SHEEP && setup_tick
                        && !m->sheep_mate_active[i])
                    animal_declined_lower_task_rng(
                        m, i, animal_player_distance_sq);
            }
        }
        if (type == EW_TYPE_BOAT) {
            if (m->boat_damage[i] > 0.0F) m->boat_damage[i] -= 1.0F;
            tick_boat(
                m, w, nx, i, p, ox, oz, boat_fwd, boat_str, 0);
            continue;
        }
        int hostile=gm_hostile(type);
        if(type!=EW_TYPE_BLAZE&&nx->attack_time[i]>0)--nx->attack_time[i];
        if (type == EW_TYPE_PIGMAN && m->anger[i] > 0) --m->anger[i];
        double dx=px-now->x[i],dy=py-now->y[i],dz=pz-now->z[i];
        double d=sqrt(dx*dx+dy*dy+dz*dz), xz=sqrt(dx*dx+dz*dz);
        if(hostile){
            if(d>GM_MOB_DESPAWN_HARD){
                loaded_invalidate_living(m, i);
                nx->alive[i]=0;nx->type[i]=EW_TYPE_NONE;continue;
            }
            if(d>GM_MOB_DESPAWN_SOFT){
                if(++m->despawn_ticks[i]>=GM_MOB_DESPAWN_DELAY){
                    loaded_invalidate_living(m, i);
                    nx->alive[i]=0;nx->type[i]=EW_TYPE_NONE;continue;
                }
            }else m->despawn_ticks[i]=0;
        }
        if(day&&(type==EW_TYPE_ZOMBIE||type==EW_TYPE_SKELETON)&&
           m->fire_ticks[i]<=0&&sky_exposed(w,now->x[i],now->y[i],now->z[i]))
            m->fire_ticks[i]=GM_MOB_FIRE_TICKS;
        /* Entity.setOnFireFromLava calls setFire(15). The live damage model is
         * coarser, but preserving the 300-tick burning state is what makes
         * EntityAIPanic's burning trigger and water search reachable. */
        if(passive&&pai_in_material(w,now,i,1)&&m->fire_ticks[i]<300)
            m->fire_ticks[i]=300;
        if(!exact_ridden_server_tick&&m->fire_ticks[i]>0){
            --m->fire_ticks[i];
            if(m->fire_ticks[i]%20==0
                    && !m->entity_fire_resistance_this_tick[i]){
                float applied = mob_resistance_damage(m, i, 1.0F);
                applied = mob_absorb_damage(m, i, applied);
                mark_hurt(m, nx, i);
                nx->health[i]-=applied;
                if(nx->health[i]<=0.0f){mob_drop(m,nx,i,drops);continue;}
            }
        }
        int aggro=0;
        if(hostile){
            int wants=1;
            if(type==EW_TYPE_ENDERMAN)wants=m->hurt_aggro[i];
            else if(gm_spider(type))wants=!day||m->hurt_aggro[i];
            else if(type==EW_TYPE_PIGMAN)wants=m->anger[i]>0;
            else if(type==EW_TYPE_SLIME)wants=m->size[i]>1;
            if(wants&&d<=follow_range(type)){
                float mw,mh;ehs_size_scaled((u8)type,m->size[i],&mw,&mh);
                if(type==EW_TYPE_GHAST)
                    aggro=1; /* ghast doesn't need tight LOS for targeting */
                else
                    aggro=los_clear(w,now->x[i],now->y[i]+mh*0.85,now->z[i],
                                    px,py+PSV_EYE_HEIGHT,pz);
            }
        }
        int moving=0,jump=0,wandering=0,swim_jump=0;
        double nav_speed=1.0;
        /* AIFireballAttack.resetTask: clear ON_FIRE when no target. */
        if(!aggro&&type==EW_TYPE_BLAZE){
            m->blaze_on_fire[i]=0;
            m->charge[i]=0;
            m->blaze_attacking[i]=0;
            m->blaze_charged[i]=0;
        }

        if(passive && m->panic_ticks[i]<=0
                && !animal_mating && !sheep_grazing){
            pai_tick(m,w,nx,i,px,py,pz,mob_griefing,
                     &moving,&jump,&wandering,&swim_jump,&nav_speed);
        /* Ghast AIFireballAttack: charge then fire large fireball. */
        }else if(aggro&&type==EW_TYPE_GHAST){
            nx->path_tx[i]=px;nx->path_ty[i]=py+8.0;nx->path_tz[i]=pz;
            nx->yaw[i]=ehs_yaw_toward(dx,dz);
            nx->ai_state[i]=EW_AI_ATTACK;
            if(d>16.0){moving=1;nx->ai_state[i]=EW_AI_CHASE;}
            ++m->charge[i];
            /* Charge 20 ticks, then reset through a 40-tick cooldown. */
            if(m->charge[i]>=20 && !m->fireball_pending){
                double len=d>0.01?d:1.0;
                m->fireball_pending=5; /* EntityLargeFireball */
                m->fireball_x=now->x[i];m->fireball_y=now->y[i]+1.5;m->fireball_z=now->z[i];
                m->fireball_vx=dx/len*0.5;m->fireball_vy=dy/len*0.5;m->fireball_vz=dz/len*0.5;
                m->charge[i]=-40;
            }
        }else if(aggro&&type==EW_TYPE_BLAZE){
            if(!m->blaze_attacking[i]){
                m->blaze_attacking[i]=1;
                m->blaze_attack_step[i]=0;
            }
            --nx->attack_time[i];
            nx->yaw[i]=ehs_yaw_toward(dx,dz);
            nx->path_tx[i]=px;nx->path_ty[i]=py;nx->path_tz[i]=pz;
            nx->path_len[i]=0;nx->ai_state[i]=EW_AI_ATTACK;
            if(d*d<4.0){
                moving=1;nx->ai_state[i]=EW_AI_CHASE;
                if(nx->attack_time[i]<=0){
                    (void)gm_mobs_attack_player(m,(struct PvStats *)v,
                                                &p->inv,
                                                mob_melee_damage(
                                                    m, i, type, m->size[i]),
                                                0);
                    p->health=v->health;
                    nx->attack_time[i]=20;
                }
            }else if(nx->attack_time[i]<=0){
                int step=++m->blaze_attack_step[i];
                if(step==1){
                    nx->attack_time[i]=60;
                    m->blaze_charged[i]=1;
                    m->blaze_on_fire[i]=1;
                }else if(step<=4){
                    nx->attack_time[i]=6;
                    EbfVector aim = ebf_blaze_fireball_aim(
                        &m->entity_random[i], dx, dy, dz);
                    if(m->blaze_shots_pending[i]<GM_BLAZE_SHOT_QUEUE){
                        int tail = (m->blaze_shot_head[i]
                            + m->blaze_shots_pending[i])
                            % GM_BLAZE_SHOT_QUEUE;
                        m->blaze_shots[i][tail] = (GmBlazeShot){
                            now->x[i],
                            now->y[i] + (double)(1.8F / 2.0F) + 0.5,
                            now->z[i],
                            aim.x, aim.y, aim.z
                        };
                        ++m->blaze_shots_pending[i];
                    }
                }else{
                    nx->attack_time[i]=100;
                    m->blaze_attack_step[i]=0;
                    m->blaze_charged[i]=0;
                    m->blaze_on_fire[i]=0;
                }
            }
        }else if(aggro&&type==EW_TYPE_SKELETON){
            /* EntityAIAttackRangedBow: hold range, strafe away inside 6. */
            nx->yaw[i]=ehs_yaw_toward(dx,dz);
            if(xz<6.0&&xz>0.01){
                double ux=dx/xz, uz=dz/xz;
                nx->path_tx[i]=now->x[i]-ux*4.0;nx->path_ty[i]=now->y[i];
                nx->path_tz[i]=now->z[i]-uz*4.0;nx->path_len[i]=0;
                nx->ai_state[i]=EW_AI_CHASE;moving=1;
            }else if(xz>14.0){
                nx->path_tx[i]=px;nx->path_ty[i]=py;nx->path_tz[i]=pz;
                nx->path_len[i]=0;nx->ai_state[i]=EW_AI_CHASE;moving=1;
            }else{
                nx->path_tx[i]=px;nx->path_ty[i]=py;nx->path_tz[i]=pz;nx->path_len[i]=0;
                nx->ai_state[i]=EW_AI_ATTACK;
            }
            if(nx->attack_time[i]<=0)
                nx->attack_time[i]=attack_cooldown_ticks(type);
        }else if(aggro&&type==EW_TYPE_CREEPER&&xz<=3.0&&fabs(dy)<3.0){
            nx->path_tx[i]=px;nx->path_ty[i]=py;nx->path_tz[i]=pz;nx->path_len[i]=0;
            nx->ai_state[i]=EW_AI_ATTACK;nx->yaw[i]=ehs_yaw_toward(dx,dz);
            if(++m->creeper_fuse[i]>=30){
                loaded_invalidate_living(m, i);
                nx->alive[i]=0;nx->type[i]=EW_TYPE_NONE;m->creeper_fuse[i]=0;m->explosion_pending=1;
                m->explosion_x=now->x[i];m->explosion_y=now->y[i]+0.5;m->explosion_z=now->z[i];
            }
        }else if(aggro&&gm_is_slimey(type)){
            /* Slime hop toward player. Squish edges use wasOnGround after
             * move_mob (EntitySlime.onUpdate order); AI only triggers jumps. */
            nx->path_tx[i]=px;nx->path_ty[i]=py;nx->path_tz[i]=pz;
            nx->yaw[i]=ehs_yaw_toward(dx,dz);
            if(m->jump_delay[i]>0)--m->jump_delay[i];
            if(now->on_ground[i]&&m->jump_delay[i]<=0){
                jump=1;moving=1;
                m->jump_delay[i]=10+m->size[i]*5;
                nx->ai_state[i]=EW_AI_CHASE;
            }else if(!now->on_ground[i]){
                moving=1;nx->ai_state[i]=EW_AI_CHASE;
            }else {
                nx->ai_state[i]=EW_AI_IDLE;
            }
            if(xz<=GM_MOB_REACH*(0.5+m->size[i]*0.25)&&fabs(dy)<(double)m->size[i]+1.0&&
               nx->attack_time[i]<=0){
                float dmg=mob_melee_damage(m,i,type,m->size[i]);
                if(dmg>0.0f){
                    (void)gm_mobs_attack_player(m,(struct PvStats *)v,
                                                &p->inv,dmg,0);
                    p->health=v->health;
                }
                nx->attack_time[i]=attack_cooldown_ticks(type);
            }
        }else if(aggro&&xz<=GM_MOB_REACH&&fabs(dy)<3.0){
            nx->path_tx[i]=px;nx->path_ty[i]=py;nx->path_tz[i]=pz;nx->path_len[i]=0;
            nx->ai_state[i]=EW_AI_ATTACK;nx->yaw[i]=ehs_yaw_toward(dx,dz);
            if(nx->attack_time[i]<=0){
                int hit=gm_mobs_attack_player(m,(struct PvStats *)v,
                                              &p->inv,
                                              mob_melee_damage(
                                                  m,i,type,m->size[i]),0);
                p->health=v->health;
                if(hit&&type==EW_TYPE_WITHER_SKELETON)
                    m->player_wither_ticks=200;
                nx->attack_time[i]=attack_cooldown_ticks(type);
            }
            /* Spider leap (EntityAILeapAtTarget): periodic, not every tick. */
            if(gm_spider(type)&&now->on_ground[i]&&xz>1.5&&xz<8.0&&
               (m->tick % 20)==0)jump=1;
        }else if(aggro){
            nx->path_tx[i]=px;nx->path_ty[i]=py;nx->path_tz[i]=pz;nx->path_len[i]=0;
            nx->ai_state[i]=EW_AI_CHASE;moving=1;
            if(type==EW_TYPE_CREEPER&&m->creeper_fuse[i]>0)--m->creeper_fuse[i];
            if(gm_spider(type)&&now->on_ground[i]&&xz>2.0&&xz<10.0&&
               (m->tick % 20)==0)jump=1;
            /* Silverfish short follow; hop on a 10-tick cadence. */
            if(type==EW_TYPE_SILVERFISH&&now->on_ground[i]&&(m->tick % 10)==0)jump=1;
        }else if(passive&&m->panic_ticks[i]>0){
            --m->panic_ticks[i];
            double ux=xz>0.01?dx/xz:1.0, uz=xz>0.01?dz/xz:0.0;
            nx->path_tx[i]=now->x[i]-ux*8.0;nx->path_ty[i]=now->y[i];
            nx->path_tz[i]=now->z[i]-uz*8.0;nx->path_len[i]=0;
            nx->ai_state[i]=EW_AI_IDLE;moving=1;
        }else if(animal_mating){
            int mate_slot = m->sheep_mate_slot[i];
            if (mate_slot > 0 && mate_slot < EW_MAX_ENTITIES
                    && nx->alive[mate_slot]) {
                /* Path.getVectorFromIndex centers the integer PathPoint by
                 * (int)(width + 1) * 0.5. Adult sheep width 0.9 gives 0.5. */
                nx->path_tx[i] = mc_floor(nx->x[mate_slot]) + 0.5;
                nx->path_ty[i] = nx->y[mate_slot];
                nx->path_tz[i] = mc_floor(nx->z[mate_slot]) + 0.5;
                nx->path_len[i] = 0;
                nx->ai_state[i] = EW_AI_CHASE;
                double mate_dx = nx->x[mate_slot] - now->x[i];
                double mate_dz = nx->z[mate_slot] - now->z[i];
                /* PathNavigateGround refuses a new path while airborne. */
                if (now->on_ground[i]
                        && mate_dx * mate_dx + mate_dz * mate_dz > 1.0e-12) {
                    moving = 1;
                }
            }
        }else if(sheep_grazing){
            nx->path_len[i]=0;
            nx->ai_state[i]=EW_AI_IDLE;
        }else{
            nx->ai_state[i]=EW_AI_IDLE;wandering=1;
            if(nx->repath_timer[i]>0)--nx->repath_timer[i];
            int has=nx->path_len[i]==1;
            if(has){
                double wx2=nx->path_tx[i]-now->x[i],wz2=nx->path_tz[i]-now->z[i];
                if(wx2*wx2+wz2*wz2<1.0){nx->path_len[i]=0;has=0;}
            }
            if(!has&&nx->repath_timer[i]<=0){
                nx->repath_timer[i]=GM_MOB_WANDER_INTERVAL;
                u64 h=mc_hash_seed((u64)m->seed,m->tick,i,0,0,0x57414e44u);
                int ddx=mc_hash_bound(h,2*GM_MOB_WANDER_RADIUS+1)-GM_MOB_WANDER_RADIUS;
                int ddz=mc_hash_bound(mc_hash64(h),2*GM_MOB_WANDER_RADIUS+1)-GM_MOB_WANDER_RADIUS;
                if(ddx||ddz){
                    int txc=mc_floor(now->x[i])+ddx,tzc=mc_floor(now->z[i])+ddz;
                    int ty=wander_ground_y(w,txc,mc_floor(now->y[i]),tzc);
                    if(ty>-999){
                        nx->path_tx[i]=txc+0.5;nx->path_ty[i]=ty;nx->path_tz[i]=tzc+0.5;
                        nx->path_len[i]=1;has=1;
                    }
                }
            }
            if(has)moving=1;
            /* Neutral pigman / small slime idle hop. */
            if(gm_is_slimey(type)&&now->on_ground[i]){
                if(m->jump_delay[i]>0)--m->jump_delay[i];
                else if(moving){
                    jump=1;m->jump_delay[i]=20+m->size[i]*10;
                }
            }
        }
        /* EntitySlime.onUpdate: lerp factor first (before physics). */
        if(gm_is_slimey(type)){
            m->squish_factor[i] += (m->squish_amount[i] - m->squish_factor[i]) * 0.5f;
        }
        if(moving&&type!=EW_TYPE_GHAST){
            double mvx=nx->path_tx[i]-now->x[i],mvz=nx->path_tz[i]-now->z[i];
            double len=sqrt(mvx*mvx+mvz*mvz);
            if(len>0.01){
                int ax=mc_floor(now->x[i]+mvx/len*0.9),az=mc_floor(now->z[i]+mvz/len*0.9);
                int fy=mc_floor(now->y[i]);
                if(solid_id(gm_world_block(w,ax,fy,az))&&
                   !solid_id(gm_world_block(w,ax,fy+1,az))&&
                   !solid_id(gm_world_block(w,ax,fy+2,az)))jump=1;
                else if(wandering&&!solid_id(gm_world_block(w,ax,fy,az))&&
                        !solid_id(gm_world_block(w,ax,fy-1,az))&&
                        !solid_id(gm_world_block(w,ax,fy-2,az))){
                    moving=0;nx->path_len[i]=0;
                }
            }
        }
        /* EntityBlaze.updateAITasks runs after goalSelector (including the
         * fireball task) and before travel. Its RNG call therefore follows a
         * same-tick shot's two aim Gaussians. */
        if(type==EW_TYPE_BLAZE){
            if(--m->blaze_height_offset_update_time[i]<=0){
                m->blaze_height_offset_update_time[i]=100;
                m->blaze_height_offset[i]=ebf_blaze_height_offset(
                    &m->entity_random[i]);
            }
            if(aggro){
                float blaze_eye = 1.8F * 0.85F;
                nx->vy[i]=ebf_blaze_height_impulse(
                    nx->vy[i], py + (double)1.62F,
                    now->y[i] + (double)blaze_eye,
                    m->blaze_height_offset[i]);
            }
        }
        {
            move_mob(
                w, window, ox, oz, st, m, nx, i,
                moving, jump, animal_mating,
                ridden_pig, pig_steerable, p->yaw, p->pitch, 0,
                swim_jump, nav_speed);
        }
        if(passive)pai_apply_current_look(m,nx,i,px,py,pz);
        collide_living_entities(m, nx, i, dimension);
        if (passive) {
            animal_age_love_tail(m, nx, i);
            /* forcedAgeTimer is client-only. The authoritative server state
             * stays at 40; a later visual-client slice owns its countdown. */
        }
        if (type == EW_TYPE_CHICKEN)
            chicken_living_tail(
                m, nx, i, math_random_seed48, next_entity_id, drops);
        if (type == EW_TYPE_PIG && m->pig_ride == i) {
            pig_update_passenger_pose(nx, i, p, ox, oz);
        }
        /* After super.onUpdate/move: wasOnGround edges then alterSquishAmount. */
        if(gm_is_slimey(type)){
            int on = nx->on_ground[i] ? 1 : 0;
            if(on && !m->was_on_ground[i]) m->squish_amount[i] = -0.5f;
            else if(!on && m->was_on_ground[i]) m->squish_amount[i] = 1.0f;
            m->was_on_ground[i] = (unsigned char)on;
            float damp = (type == EW_TYPE_MAGMA) ? 0.9f : 0.6f;
            m->squish_amount[i] *= damp; /* alterSquishAmount */
        }
    }
    /* Catch non-breeding XP spawned by an initial living update. Breeding
     * children and XP retain their exact paired append order in the event tail. */
    for (int xp_slot = 0; xp_slot < GM_XP_ORBS; ++xp_slot)
        if (!ticked_xp[xp_slot] && !spawned_xp[xp_slot]
                && !m->xp_orbs[xp_slot].dead
                && m->xp_orbs[xp_slot].xpValue > 0
                && m->orb_dimension[xp_slot] == m->active_dimension) {
            if (m->tick_update_order_count < GM_MOB_UPDATE_ORDER_CAPACITY)
                m->tick_update_order[m->tick_update_order_count++] =
                    m->xp_orbs[xp_slot].eid;
            tick_xp_orb_slot(m, w, p, ox, oz, &xp_player, xp_slot);
        }
    for (int event = 0; event < same_tick_spawn_count; ++event) {
        if (same_tick_spawns[event].kind == GM_SAME_TICK_ANIMAL) {
            int slot = same_tick_spawns[event].slot;
            if (m->tick_update_order_count < GM_MOB_UPDATE_ORDER_CAPACITY)
                m->tick_update_order[m->tick_update_order_count++] =
                    nx->id[slot];
            animal_tick_newborn_same_boundary(
                m, w, st, nx, slot,
                px, py, pz, mob_griefing);
        } else {
            int slot = same_tick_spawns[event].slot;
            if (m->tick_update_order_count < GM_MOB_UPDATE_ORDER_CAPACITY)
                m->tick_update_order[m->tick_update_order_count++] =
                    m->xp_orbs[slot].eid;
            tick_xp_orb_slot(
                m, w, p, ox, oz, &xp_player,
                slot);
        }
    }
    ++m->tick;m->current^=1;
    loaded_order_compact(m);
}

int gm_mobs_fill_views(const GmMobLive *m, GmEntityView *out, int max) {
    if(!m||!out||max<=0)return 0;
    const EwStore *s=const_store(m);int n=0;
    for(int i=1;i<EW_MAX_ENTITIES&&n<max;++i)
        if(s->alive[i]&&m->entity_dimension[i]==m->active_dimension&&gm_living(s->type[i])){
        out[n]=(GmEntityView){0};
        out[n].type=s->type[i];
        if(s->type[i]==EW_TYPE_CAVE_SPIDER)
            out[n].skin=4; /* CR_MOB_CAVE_SPIDER + 1 */
        /* Live pigman uses type 15; render as zombie silhouette + pigman skin.
         * Keep type 15 so callers can identify pigmen; entity_render maps it. */
        if(s->type[i]==EW_TYPE_PIGMAN)
            out[n].skin = 18; /* CR_MOB_PIGMAN+1 (atlas index 17) */
        out[n].x=(float)s->x[i];out[n].y=(float)s->y[i];
        out[n].z=(float)s->z[i];out[n].yaw=s->yaw[i];
        out[n].health=s->type[i]==EW_TYPE_BOAT?-1.0f:s->health[i];
        if(gm_passive(s->type[i])){
            out[n].head_yaw=m->passive_head_yaw[i];
            out[n].pitch=m->passive_head_pitch[i];
        }
        if (s->type[i] == EW_TYPE_VILLAGER)
            out[n].item_id = m->villager_profession[i];
        if (s->type[i] == EW_TYPE_PIG) {
            out[n].pitch = m->pig_pitch[i];
            out[n].head_yaw = m->pig_head_yaw[i] - m->pig_render_yaw[i];
            out[n].limb_swing = m->pig_limb_swing[i];
            out[n].limb_swing_amount = m->pig_limb_amount[i];
        }
        if((s->type[i]==EW_TYPE_BLAZE&&m->blaze_charged[i])||
           (s->type[i]!=EW_TYPE_BLAZE&&m->fire_ticks[i]>0))
            out[n].flags|=1; /* EntityLivingBase.isBurning */
        if (mob_effect_amplifier(m, i, 14) >= 0)
            out[n].flags |= 4; /* EntityLivingBase.isInvisible */
        out[n].item_meta=m->size[i]; /* slime/magma size for render scale */
        if (s->type[i] == EW_TYPE_SHEEP) {
            int timer = m->sheep_eat_timer[i];
            out[n].fleece_color = m->sheep_data[i] & 15;
            out[n].sheared = (m->sheep_data[i] & 16) != 0;
            out[n].flags |= 16; /* live sheep graze fields are authoritative */
            if (timer <= 0) {
                out[n].graze_y = 0.0F;
                out[n].graze_x = 0.0F;
            } else {
                if (timer >= 4 && timer <= 36)
                    out[n].graze_y = 1.0F;
                else if (timer < 4)
                    out[n].graze_y = (float)timer / 4.0F;
                else
                    out[n].graze_y = -(float)(timer - 40) / 4.0F;
                if (timer > 4 && timer <= 36) {
                    float phase = (float)(timer - 4) / 32.0F;
                    out[n].graze_x = (float)(MC_PI / 5.0)
                        + (float)(MC_PI * 7.0 / 100.0)
                            * sinf(phase * 28.7F);
                } else {
                    out[n].graze_x = (float)(MC_PI / 5.0);
                }
            }
        }
        if (m->growing_age[i] < 0)
            out[n].flags |= 8;
        out[n].squish=m->squish_factor[i]; /* EntitySlime.squishFactor */
        out[n].creeper_fuse=m->creeper_fuse[i];
        /* Recorder flags bit 0 is EntityLivingBase.isBurning(). Live state
         * tracks only generic fire ticks and EntityBlaze's charged override. */
        if(m->fire_ticks[i]>0 ||
           (s->type[i]==EW_TYPE_BLAZE && m->blaze_on_fire[i]))
            out[n].flags |= 1;
        ++n;
    }
    for(int i=0;i<GM_XP_ORBS&&n<max;++i){const McOrb *o=&m->xp_orbs[i];
        if(o->dead||o->xpValue<=0||m->orb_dimension[i]!=m->active_dimension)continue;
        GmEntityView v; memset(&v,0,sizeof v);
        v.type=GM_ENTITY_XP_ORB;
        v.x=(float)o->posX;v.y=(float)o->posY;v.z=(float)o->posZ;
        v.health=(float)o->xpValue;   /* legacy field */
        v.item_id=o->xpValue;         /* getTextureByXP */
        v.item_meta=o->xpColor;       /* RenderXPOrb colour phase */
        v.age=o->xpOrbAge;
        out[n++]=v;
    }return n;
}

int gm_mobs_alive(const GmMobLive *m){return m?alive_count(m,const_store(m)):0;}

static int mob_slot_by_eid(const GmMobLive *m, int eid) {
    if (!m || eid <= 0) return -1;
    const EwStore *s = const_store(m);
    for (int i = 1; i < EW_MAX_ENTITIES; ++i)
        if (s->alive[i] && s->id[i] == eid) return i;
    return -1;
}

int gm_mobs_find_slot_by_eid(const GmMobLive *m, int eid) {
    return mob_slot_by_eid(m, eid);
}

static void pig_packet_contact_transition_exact(
        GmMobLive *m, EwStore *s, int slot, int eid,
        int cactus_contact, int flammable_contact, int wet_contact,
        double event_x, double event_y, double event_z, int clear_fall,
        uint64_t *math_random_seed48) {
    if (clear_fall) {
        m->entity_server_fall_distance[slot] = 0.0F;
        if (m->pig_vehicle_server.valid
                && m->pig_vehicle_server.eid == eid)
            m->pig_vehicle_server.fall_distance = 0.0F;
    }
    if (cactus_contact)
        (void)ridden_pig_environment_damage(
            m, s, slot, 1.0F, 0,
            event_x, event_y, event_z, math_random_seed48);
    if (flammable_contact) {
        (void)ridden_pig_environment_damage(
            m, s, slot, 1.0F, 1,
            event_x, event_y, event_z, math_random_seed48);
        if (!wet_contact) {
            ++m->fire_ticks[slot];
            if (m->fire_ticks[slot] == 0)
                m->fire_ticks[slot] = 8 * 20;
        }
    } else if (m->fire_ticks[slot] <= 0) {
        m->fire_ticks[slot] = -1;
    }
    if (wet_contact && m->fire_ticks[slot] > 0) {
        (void)jrand_float(&m->entity_server_random[slot].random);
        (void)jrand_float(&m->entity_server_random[slot].random);
        m->fire_ticks[slot] = -1;
    }
}

static void pig_packet_contact_checkpoint_at(
        GmMobLive *m, const EwStore *s, int slot, int eid,
        double x, double y, double z, const McAABB *box,
        float fall_distance, int in_water, int in_lava,
        const uint64_t *math_random_seed48) {
    uint64_t seq = m->pig_packet_contact_checkpoint.seq + 1;
    m->pig_packet_contact_checkpoint =
        (GmPigPacketContactCheckpoint){
            .seq = seq,
            .valid = 1,
            .eid = eid,
            .x = x,
            .y = y,
            .z = z,
            .box = *box,
            .fall_distance = fall_distance,
            .is_in_water = in_water ? 1 : 0,
            .is_in_lava = in_lava ? 1 : 0,
            .health = s->health[slot],
            .fire_ticks = m->fire_ticks[slot],
            .hurt_time = m->entity_hurt_time[slot],
            .hurt_resistant_time = m->entity_hurt_resistant[slot],
            .fire_resistance_ticks =
                m->entity_server_fire_resistance_ticks[slot],
            .last_damage = m->entity_last_damage[slot],
            .alive = s->alive[slot] ? 1 : 0,
            .entity_seed48 =
                m->entity_server_random[slot].random.seed,
            .math_seed48 = math_random_seed48
                ? *math_random_seed48 : 0
        };
}

int gm_mobs_pig_packet_contact_exact(
        GmMobLive *m, int eid, int cactus_contact, int flammable_contact,
        int wet_contact,
        uint64_t *math_random_seed48) {
    int slot = m ? m->pig_ride : -1;
    if (slot <= 0 || slot >= EW_MAX_ENTITIES) return 0;
    EwStore *s = now_store(m);
    if (!s->alive[slot] || s->id[slot] != eid
            || s->type[slot] != EW_TYPE_PIG)
        return 0;
    /* NetHandlerPlayServer subtracts 1e-6 from an otherwise stationary packet
     * move. The support collision resolves it to zero and updateFallState
     * clears the authoritative fall ledger before block callbacks. */
    pig_packet_contact_transition_exact(
        m, s, slot, eid, cactus_contact, flammable_contact, wet_contact,
        s->x[slot], s->y[slot], s->z[slot], 1, math_random_seed48);
    McAABB box;
    (void)mob_slot_world_box(m, s, slot, &box);
    pig_packet_contact_checkpoint_at(
        m, s, slot, eid, s->x[slot], s->y[slot], s->z[slot], &box,
        m->entity_server_fall_distance[slot],
        m->entity_server_in_water[slot], m->entity_server_in_lava[slot],
        math_random_seed48);
    return 1;
}

static void pig_packet_contact_flags_at_box(
        const struct Chunk *window, int ox, int oz, const McAABB *box,
        int *cactus_contact, int *flammable_contact) {
    int cactus = 0;
    int x0 = mc_floor(box->minX + 0.001);
    int x1 = mc_floor(box->maxX - 0.001);
    int y0 = mc_floor(box->minY + 0.001);
    int y1 = mc_floor(box->maxY - 0.001);
    int z0 = mc_floor(box->minZ + 0.001);
    int z1 = mc_floor(box->maxZ - 0.001);
    for (int x = x0; x <= x1 && !cactus; ++x)
        for (int y = y0; y <= y1 && !cactus; ++y)
            for (int z = z0; z <= z1; ++z)
                if (psv_get_block(
                        (const Chunk *)window, x - ox, y, z - oz) == 81) {
                    cactus = 1;
                    break;
                }

    int flammable = 0;
    x0 = mc_floor(box->minX + 0.001);
    x1 = psv_ceil(box->maxX - 0.001);
    y0 = mc_floor(box->minY + 0.001);
    y1 = psv_ceil(box->maxY - 0.001);
    z0 = mc_floor(box->minZ + 0.001);
    z1 = psv_ceil(box->maxZ - 0.001);
    for (int x = x0; x < x1 && !flammable; ++x)
        for (int y = y0; y < y1 && !flammable; ++y)
            for (int z = z0; z < z1; ++z) {
                int id = psv_get_block(
                    (const Chunk *)window, x - ox, y, z - oz);
                if (id == 51 || id == 10 || id == 11) {
                    flammable = 1;
                    break;
                }
            }
    *cactus_contact = cactus;
    *flammable_contact = flammable;
}

int gm_mobs_pig_packet_contact_world_exact(
        GmMobLive *m, const struct Chunk *window, int ox, int oz, int eid,
        uint64_t *math_random_seed48) {
    int slot = m ? m->pig_ride : -1;
    if (!m || !window || slot <= 0 || slot >= EW_MAX_ENTITIES)
        return 0;
    const EwStore *s = const_store(m);
    if (!s->alive[slot] || s->id[slot] != eid
            || s->type[slot] != EW_TYPE_PIG)
        return 0;

    McAABB box;
    if (!mob_slot_world_box(m, s, slot, &box)) return 0;

    int cactus_contact, flammable_contact;
    pig_packet_contact_flags_at_box(
        window, ox, oz, &box, &cactus_contact, &flammable_contact);

    return gm_mobs_pig_packet_contact_exact(
        m, eid, cactus_contact, flammable_contact,
        m->entity_server_in_water[slot] ? 1 : 0,
        math_random_seed48);
}

static int pig_packet_box_clear(
        const struct Chunk *window, int ox, int oz, const McAABB *world_box,
        McAABB *scratch, int capacity) {
    McAABB local = *world_box;
    local.minX -= ox;
    local.maxX -= ox;
    local.minZ -= oz;
    local.maxZ -= oz;
    int count = psv_collect_blocks(
        (const Chunk *)window, &local, scratch, capacity);
    for (int i = 0; i < count; ++i)
        if (mc_aabb_intersects(&local, &scratch[i])) return 0;
    return 1;
}

static McAABB pig_packet_target_box(
        const GmMobLive *m, const EwStore *s, int slot,
        double x, double y, double z) {
    float width, height;
    ehs_size_scaled(s->type[slot], m->size[slot], &width, &height);
    if (gm_passive(s->type[slot]) && m->growing_age[slot] < 0) {
        width *= 0.5F;
        height *= 0.5F;
    }
    double half = (double)width * 0.5;
    return mc_aabb_make(
        x - half, y, z - half, x + half, y + height, z + half);
}

static void pig_packet_store_pose(
        GmMobLive *m, EwStore *s, int slot,
        double x, double y, double z, const McAABB *box,
        float yaw, float pitch) {
    s->x[slot] = x;
    s->y[slot] = y;
    s->z[slot] = z;
    s->yaw[slot] = yaw;
    m->pig_pitch[slot] = pitch;
    m->entity_box_min_x[slot] = box->minX;
    m->entity_box_min_y[slot] = box->minY;
    m->entity_box_min_z[slot] = box->minZ;
    m->entity_box_max_x[slot] = box->maxX;
    m->entity_box_max_y[slot] = box->maxY;
    m->entity_box_max_z[slot] = box->maxZ;
    m->entity_box_valid[slot] = 1;
}

int gm_mobs_pig_packet_move_dry_exact(
        GmMobLive *m, const struct Chunk *window, int ox, int oz, int eid,
        double target_x, double target_y, double target_z,
        float target_yaw, float target_pitch,
        GmPigVehicleMoveResult *out) {
    int slot = m ? m->pig_ride : -1;
    if (!m || !window || !out || slot <= 0 || slot >= EW_MAX_ENTITIES
            || !isfinite(target_x) || !isfinite(target_y)
            || !isfinite(target_z) || !isfinite(target_yaw)
            || !isfinite(target_pitch))
        return 0;
    EwStore *s = now_store(m);
    if (!s->alive[slot] || s->id[slot] != eid
            || s->type[slot] != EW_TYPE_PIG)
        return 0;

    memset(out, 0, sizeof *out);
    double old_x = s->x[slot], old_y = s->y[slot], old_z = s->z[slot];
    float old_yaw = s->yaw[slot], old_pitch = m->pig_pitch[slot];
    McAABB old_box;
    if (!mob_slot_world_box(m, s, slot, &old_box)) return 0;
    out->lowest_x = out->lowest_x1 = old_x;
    out->lowest_y = out->lowest_y1 = old_y;
    out->lowest_z = out->lowest_z1 = old_z;

    double dx = target_x - old_x;
    double dy = target_y - old_y;
    double dz = target_z - old_z;
    double motion_sq = s->vx[slot] * s->vx[slot]
        + s->vy[slot] * s->vy[slot]
        + s->vz[slot] * s->vz[slot];
    if (dx * dx + dy * dy + dz * dz - motion_sq > 100.0) {
        out->result = GM_PIG_VEHICLE_MOVE_CORRECTED_SPEED;
        out->correction_count = 1;
        out->correction_x = old_x;
        out->correction_y = old_y;
        out->correction_z = old_z;
        out->correction_yaw = old_yaw;
        out->correction_pitch = old_pitch;
        return 1;
    }

    McAABB contracted_old = mc_aabb_make(
        old_box.minX + 0.0625, old_box.minY + 0.0625,
        old_box.minZ + 0.0625, old_box.maxX - 0.0625,
        old_box.maxY - 0.0625, old_box.maxZ - 0.0625);
    int old_clear = pig_packet_box_clear(
        window, ox, oz, &contracted_old,
        m->pig_collision_scratch, GM_PIG_COLLISION_BOXES);

    McEntity entity;
    memset(&entity, 0, sizeof entity);
    entity.box = old_box;
    entity.posX = old_x;
    entity.posY = old_y;
    entity.posZ = old_z;
    entity.motionX = s->vx[slot];
    entity.motionY = s->vy[slot];
    entity.motionZ = s->vz[slot];
    entity.onGround = s->on_ground[slot] ? 1 : 0;
    double requested_y = dy - 1.0e-6;
    McAABB query = mc_aabb_addcoord(&old_box, dx, requested_y, dz);
    if (old_box.maxY + 0.6 > query.maxY)
        query.maxY = old_box.maxY + 0.6;
    McAABB local_query = query;
    local_query.minX -= ox;
    local_query.maxX -= ox;
    local_query.minZ -= oz;
    local_query.maxZ -= oz;
    int boxes = psv_collect_blocks(
        (const Chunk *)window, &local_query,
        m->pig_collision_scratch, GM_PIG_COLLISION_BOXES);
    for (int i = 0; i < boxes; ++i) {
        m->pig_collision_scratch[i].minX += ox;
        m->pig_collision_scratch[i].maxX += ox;
        m->pig_collision_scratch[i].minZ += oz;
        m->pig_collision_scratch[i].maxZ += oz;
    }
    mc_entity_move_step(
        &entity, dx, requested_y, dz,
        m->pig_collision_scratch, boxes, 0.6F);
    double moved_y = entity.posY - old_y;
    if (entity.onGround)
        m->entity_server_fall_distance[slot] = 0.0F;
    else if (moved_y < 0.0)
        m->entity_server_fall_distance[slot] -= (float)moved_y;
    s->vx[slot] = entity.motionX;
    s->vy[slot] = entity.motionY;
    s->vz[slot] = entity.motionZ;
    s->on_ground[slot] = (unsigned char)(entity.onGround ? 1 : 0);

    double residual_x = target_x - entity.posX;
    double residual_z = target_z - entity.posZ;
    int wrong = residual_x * residual_x + residual_z * residual_z > 0.0625;
    McAABB target_box = pig_packet_target_box(
        m, s, slot, target_x, target_y, target_z);
    McAABB contracted_target = mc_aabb_make(
        target_box.minX + 0.0625, target_box.minY + 0.0625,
        target_box.minZ + 0.0625, target_box.maxX - 0.0625,
        target_box.maxY - 0.0625, target_box.maxZ - 0.0625);
    int target_clear = pig_packet_box_clear(
        window, ox, oz, &contracted_target,
        m->pig_collision_scratch, GM_PIG_COLLISION_BOXES);
    if (old_clear && (wrong || !target_clear)) {
        pig_packet_store_pose(
            m, s, slot, old_x, old_y, old_z, &old_box,
            target_yaw, target_pitch);
        out->result = GM_PIG_VEHICLE_MOVE_CORRECTED_COLLISION;
        out->correction_count = 1;
        out->correction_x = old_x;
        out->correction_y = old_y;
        out->correction_z = old_z;
        out->correction_yaw = target_yaw;
        out->correction_pitch = target_pitch;
        return 1;
    }

    pig_packet_store_pose(
        m, s, slot, target_x, target_y, target_z, &target_box,
        target_yaw, target_pitch);
    out->result = GM_PIG_VEHICLE_MOVE_ACCEPTED;
    out->lowest_x1 = target_x;
    out->lowest_y1 = target_y;
    out->lowest_z1 = target_z;
    return 1;
}

static void pig_vehicle_runtime_checkpoint(
        GmMobLive *m, double target_x, double target_y, double target_z,
        float target_yaw, float target_pitch,
        uint64_t math_random_seed48) {
    const GmPigVehicleServerState *server = &m->pig_vehicle_server;
    int slot = m->pig_ride;
    const EwStore *client = const_store(m);
    McAABB client_box;
    (void)mob_slot_world_box(m, client, slot, &client_box);
    m->pig_vehicle_move_checkpoint = (GmPigVehicleMoveCheckpoint){
        .seq = server->packet_seq,
        .valid = 1,
        .eid = server->eid,
        .target_x = target_x,
        .target_y = target_y,
        .target_z = target_z,
        .target_yaw = target_yaw,
        .target_pitch = target_pitch,
        .x = server->x,
        .y = server->y,
        .z = server->z,
        .vx = server->vx,
        .vy = server->vy,
        .vz = server->vz,
        .box = server->box,
        .yaw = server->yaw,
        .pitch = server->pitch,
        .on_ground = server->on_ground,
        .fall_distance = server->fall_distance,
        .is_in_water = m->entity_server_in_water[slot] ? 1 : 0,
        .is_in_lava = m->entity_server_in_lava[slot] ? 1 : 0,
        .health = client->health[slot],
        .fire_ticks = m->fire_ticks[slot],
        .hurt_time = m->entity_hurt_time[slot],
        .hurt_resistant_time = m->entity_hurt_resistant[slot],
        .fire_resistance_ticks =
            m->entity_server_fire_resistance_ticks[slot],
        .last_damage = m->entity_last_damage[slot],
        .alive = client->alive[slot] ? 1 : 0,
        .entity_seed48 = m->entity_server_random[slot].random.seed,
        .math_seed48 = math_random_seed48,
        .client_x = client->x[slot],
        .client_y = client->y[slot],
        .client_z = client->z[slot],
        .client_box = client_box,
        .client_yaw = client->yaw[slot],
        .client_pitch = m->pig_pitch[slot],
        .move = server->last_move
    };
}

int gm_mobs_pig_packet_move_runtime_dry_exact(
        GmMobLive *m, const struct Chunk *window, int ox, int oz, int eid,
        double target_x, double target_y, double target_z,
        float target_yaw, float target_pitch,
        uint64_t *math_random_seed48) {
    int slot = m ? m->pig_ride : -1;
    if (!m || !window || slot <= 0 || slot >= EW_MAX_ENTITIES
            || !isfinite(target_x) || !isfinite(target_y)
            || !isfinite(target_z) || !isfinite(target_yaw)
            || !isfinite(target_pitch))
        return 0;
    EwStore *shared = now_store(m);
    const EwStore *client = shared;
    GmPigVehicleServerState *server = &m->pig_vehicle_server;
    if (!client->alive[slot] || client->id[slot] != eid
            || client->type[slot] != EW_TYPE_PIG
            || !server->valid || server->eid != eid)
        return 0;

    double old_x = server->x, old_y = server->y, old_z = server->z;
    float old_yaw = server->yaw, old_pitch = server->pitch;
    McAABB old_box = server->box;
    GmPigVehicleMoveResult move;
    memset(&move, 0, sizeof move);
    move.lowest_x = server->lowest_x;
    move.lowest_y = server->lowest_y;
    move.lowest_z = server->lowest_z;
    move.lowest_x1 = server->lowest_x1;
    move.lowest_y1 = server->lowest_y1;
    move.lowest_z1 = server->lowest_z1;
    ++server->packet_seq;

    double dx = target_x - server->lowest_x1;
    double dy = target_y - server->lowest_y1;
    double dz = target_z - server->lowest_z1;
    double speed_dx = target_x - server->lowest_x;
    double speed_dy = target_y - server->lowest_y;
    double speed_dz = target_z - server->lowest_z;
    double motion_sq = server->vx * server->vx
        + server->vy * server->vy + server->vz * server->vz;
    if (speed_dx * speed_dx + speed_dy * speed_dy
            + speed_dz * speed_dz - motion_sq > 100.0) {
        move.result = GM_PIG_VEHICLE_MOVE_CORRECTED_SPEED;
        move.correction_count = 1;
        move.correction_x = old_x;
        move.correction_y = old_y;
        move.correction_z = old_z;
        move.correction_yaw = old_yaw;
        move.correction_pitch = old_pitch;
        server->last_move = move;
        pig_vehicle_runtime_checkpoint(
            m, target_x, target_y, target_z, target_yaw, target_pitch,
            math_random_seed48 ? *math_random_seed48 : 0);
        return 1;
    }

    McAABB contracted_old = mc_aabb_make(
        old_box.minX + 0.0625, old_box.minY + 0.0625,
        old_box.minZ + 0.0625, old_box.maxX - 0.0625,
        old_box.maxY - 0.0625, old_box.maxZ - 0.0625);
    int old_clear = pig_packet_box_clear(
        window, ox, oz, &contracted_old,
        m->pig_collision_scratch, GM_PIG_COLLISION_BOXES);

    McEntity entity;
    memset(&entity, 0, sizeof entity);
    entity.box = old_box;
    entity.posX = old_x;
    entity.posY = old_y;
    entity.posZ = old_z;
    entity.motionX = server->vx;
    entity.motionY = server->vy;
    entity.motionZ = server->vz;
    entity.onGround = server->on_ground;
    double requested_y = dy - 1.0e-6;
    McAABB query = mc_aabb_addcoord(&old_box, dx, requested_y, dz);
    if (old_box.maxY + 0.6 > query.maxY)
        query.maxY = old_box.maxY + 0.6;
    McAABB local_query = query;
    local_query.minX -= ox;
    local_query.maxX -= ox;
    local_query.minZ -= oz;
    local_query.maxZ -= oz;
    int boxes = psv_collect_blocks(
        (const Chunk *)window, &local_query,
        m->pig_collision_scratch, GM_PIG_COLLISION_BOXES);
    for (int i = 0; i < boxes; ++i) {
        m->pig_collision_scratch[i].minX += ox;
        m->pig_collision_scratch[i].maxX += ox;
        m->pig_collision_scratch[i].minZ += oz;
        m->pig_collision_scratch[i].maxZ += oz;
    }
    mc_entity_move_step(
        &entity, dx, requested_y, dz,
        m->pig_collision_scratch, boxes, 0.6F);
    double moved_y = entity.posY - old_y;
    if (entity.onGround)
        server->fall_distance = 0.0F;
    else if (moved_y < 0.0)
        server->fall_distance -= (float)moved_y;
    server->vx = entity.motionX;
    server->vy = entity.motionY;
    server->vz = entity.motionZ;
    server->on_ground = entity.onGround ? 1 : 0;

    /* EntityLivingBase.updateFallState refreshes water only when the prior
     * inWater field is false. Entity.move then dispatches block collisions
     * and its flammable check at this temporary resolved AABB. All of those
     * side effects survive a later processVehicleMove rollback. */
    int persistent_water = m->entity_server_in_water[slot] ? 1 : 0;
    if (!persistent_water && pig_vehicle_box_in_liquid(
            window, ox, oz, &entity.box, 1)) {
        if (!server->first_update) {
            ridden_pig_water_entry_rng(
                &m->entity_server_random[slot].random,
                ridden_pig_slot_width(m, shared, slot));
        }
        persistent_water = 1;
        m->entity_server_in_water[slot] = 1;
        server->fall_distance = 0.0F;
        m->fire_ticks[slot] = 0;
    }
    int cactus_contact, flammable_contact;
    pig_packet_contact_flags_at_box(
        window, ox, oz, &entity.box,
        &cactus_contact, &flammable_contact);
    int temporary_lava = pig_vehicle_box_in_liquid(
        window, ox, oz, &entity.box, 0) ? 1 : 0;
    pig_packet_contact_transition_exact(
        m, shared, slot, eid, cactus_contact, flammable_contact,
        persistent_water, entity.posX, entity.posY, entity.posZ, 0,
        math_random_seed48);
    pig_packet_contact_checkpoint_at(
        m, shared, slot, eid,
        entity.posX, entity.posY, entity.posZ, &entity.box,
        server->fall_distance, persistent_water, temporary_lava,
        math_random_seed48);

    double residual_x = target_x - entity.posX;
    double residual_z = target_z - entity.posZ;
    int wrong = residual_x * residual_x + residual_z * residual_z > 0.0625;
    McAABB target_box = pig_packet_target_box(
        m, client, slot, target_x, target_y, target_z);
    McAABB contracted_target = mc_aabb_make(
        target_box.minX + 0.0625, target_box.minY + 0.0625,
        target_box.minZ + 0.0625, target_box.maxX - 0.0625,
        target_box.maxY - 0.0625, target_box.maxZ - 0.0625);
    int target_clear = pig_packet_box_clear(
        window, ox, oz, &contracted_target,
        m->pig_collision_scratch, GM_PIG_COLLISION_BOXES);
    if (old_clear && (wrong || !target_clear)) {
        server->x = old_x;
        server->y = old_y;
        server->z = old_z;
        server->box = old_box;
        server->yaw = target_yaw;
        server->pitch = target_pitch;
        move.result = GM_PIG_VEHICLE_MOVE_CORRECTED_COLLISION;
        move.correction_count = 1;
        move.correction_x = old_x;
        move.correction_y = old_y;
        move.correction_z = old_z;
        move.correction_yaw = target_yaw;
        move.correction_pitch = target_pitch;
    } else {
        server->x = target_x;
        server->y = target_y;
        server->z = target_z;
        server->box = target_box;
        server->yaw = target_yaw;
        server->pitch = target_pitch;
        server->lowest_x1 = target_x;
        server->lowest_y1 = target_y;
        server->lowest_z1 = target_z;
        move.result = GM_PIG_VEHICLE_MOVE_ACCEPTED;
        move.lowest_x1 = target_x;
        move.lowest_y1 = target_y;
        move.lowest_z1 = target_z;
    }
    m->entity_server_fall_distance[slot] = server->fall_distance;
    m->entity_server_in_lava[slot] = (unsigned char)(
        pig_vehicle_box_in_liquid(
            window, ox, oz, &server->box, 0) ? 1 : 0);
    server->last_move = move;
    pig_vehicle_runtime_checkpoint(
        m, target_x, target_y, target_z, target_yaw, target_pitch,
        math_random_seed48 ? *math_random_seed48 : 0);
    return 1;
}

int gm_mobs_get_pig_client_packet_pose(
        const GmMobLive *m, int *eid, double *x, double *y, double *z,
        float *yaw, float *pitch) {
    int mounted_eid;
    if (!gm_mobs_pig_riding(m, &mounted_eid)) return 0;
    int slot = m->pig_ride;
    const EwStore *client = const_store(m);
    if (eid) *eid = mounted_eid;
    if (x) *x = client->x[slot];
    if (y) *y = client->y[slot];
    if (z) *z = client->z[slot];
    if (yaw) *yaw = client->yaw[slot];
    if (pitch) *pitch = m->pig_pitch[slot];
    return 1;
}

int gm_mobs_pig_apply_client_vehicle_correction(
        GmMobLive *m, int eid, double x, double y, double z,
        float yaw, float pitch) {
    int slot = m ? m->pig_ride : -1;
    if (!m || slot <= 0 || slot >= EW_MAX_ENTITIES
            || !isfinite(x) || !isfinite(y) || !isfinite(z)
            || !isfinite(yaw) || !isfinite(pitch))
        return 0;
    EwStore *client = now_store(m);
    if (!client->alive[slot] || client->id[slot] != eid
            || client->type[slot] != EW_TYPE_PIG)
        return 0;
    McAABB box = pig_packet_target_box(m, client, slot, x, y, z);
    pig_packet_store_pose(
        m, client, slot, x, y, z, &box, yaw, pitch);
    return 1;
}

int gm_mobs_get_pig_vehicle_server_state(
        const GmMobLive *m, GmPigVehicleServerState *out) {
    if (!m || !out || !m->pig_vehicle_server.valid) return 0;
    *out = m->pig_vehicle_server;
    return 1;
}

int gm_mobs_get_pig_vehicle_move_checkpoint(
        const GmMobLive *m, GmPigVehicleMoveCheckpoint *out) {
    if (!m || !out || !m->pig_vehicle_move_checkpoint.valid) return 0;
    *out = m->pig_vehicle_move_checkpoint;
    return 1;
}

int gm_mobs_get_pig_packet_contact_checkpoint(
        const GmMobLive *m, GmPigPacketContactCheckpoint *out) {
    if (!m || !out || !m->pig_packet_contact_checkpoint.valid) return 0;
    *out = m->pig_packet_contact_checkpoint;
    return 1;
}

int gm_mobs_take_blaze_shot(GmMobLive *m, int slot, GmBlazeShot *shot) {
    if (!m || !shot || slot <= 0 || slot >= EW_MAX_ENTITIES
            || m->blaze_shots_pending[slot] == 0)
        return 0;
    int head = m->blaze_shot_head[slot] % GM_BLAZE_SHOT_QUEUE;
    *shot = m->blaze_shots[slot][head];
    memset(&m->blaze_shots[slot][head], 0,
           sizeof m->blaze_shots[slot][head]);
    m->blaze_shot_head[slot] =
        (unsigned char)((head + 1) % GM_BLAZE_SHOT_QUEUE);
    --m->blaze_shots_pending[slot];
    return 1;
}

int gm_mobs_set_entity_random_state(
        GmMobLive *m, int eid, uint64_t seed48,
        int have_next_gaussian, double next_gaussian) {
    int slot = mob_slot_by_eid(m, eid);
    if (slot < 0 || (have_next_gaussian != 0 && have_next_gaussian != 1))
        return 0;
    jrand_gaussian_set_state(
        &m->entity_random[slot], (u64)seed48,
        have_next_gaussian, next_gaussian);
    jrand_gaussian_set_state(
        &m->entity_server_random[slot], (u64)seed48,
        have_next_gaussian, next_gaussian);
    return 1;
}

int gm_mobs_set_chicken_state(
        GmMobLive *m, int eid, int time_until_next_egg,
        float wing_rotation, float dest_pos, float old_flap_speed,
        float old_flap, float wing_rot_delta, int chicken_jockey) {
    int slot = mob_slot_by_eid(m, eid);
    const EwStore *s = m ? const_store(m) : NULL;
    if (slot < 0 || !s || s->type[slot] != EW_TYPE_CHICKEN
            || !isfinite(wing_rotation) || !isfinite(dest_pos)
            || !isfinite(old_flap_speed) || !isfinite(old_flap)
            || !isfinite(wing_rot_delta)
            || (chicken_jockey != 0 && chicken_jockey != 1))
        return 0;
    m->chicken_time_until_next_egg[slot] = time_until_next_egg;
    m->chicken_wing_rotation[slot] = wing_rotation;
    m->chicken_dest_pos[slot] = dest_pos;
    m->chicken_old_flap_speed[slot] = old_flap_speed;
    m->chicken_old_flap[slot] = old_flap;
    m->chicken_wing_rot_delta[slot] = wing_rot_delta;
    m->chicken_jockey[slot] = (unsigned char)chicken_jockey;
    return 1;
}

int gm_mobs_set_next_animal_child_state(
        GmMobLive *m, uint64_t seed48,
        int have_next_gaussian, double next_gaussian,
        int chicken_time_until_next_egg) {
    if (!m || seed48 >= (UINT64_C(1) << 48)
            || (have_next_gaussian != 0 && have_next_gaussian != 1)
            || !isfinite(next_gaussian))
        return 0;
    m->animal_child_state_head = 0;
    m->animal_child_state_count = 1;
    jrand_gaussian_set_state(
        &m->animal_child_random_queue[0], (u64)seed48,
        have_next_gaussian, next_gaussian);
    m->animal_child_chicken_egg_queue[0] = chicken_time_until_next_egg;
    return 1;
}

int gm_mobs_queue_animal_child_state(
        GmMobLive *m, uint64_t seed48,
        int have_next_gaussian, double next_gaussian,
        int chicken_time_until_next_egg) {
    if (!m || seed48 >= (UINT64_C(1) << 48)
            || (have_next_gaussian != 0 && have_next_gaussian != 1)
            || !isfinite(next_gaussian)
            || m->animal_child_state_count >= EW_MAX_ENTITIES)
        return 0;
    int index = (m->animal_child_state_head
        + m->animal_child_state_count) % EW_MAX_ENTITIES;
    jrand_gaussian_set_state(
        &m->animal_child_random_queue[index], (u64)seed48,
        have_next_gaussian, next_gaussian);
    m->animal_child_chicken_egg_queue[index] = chicken_time_until_next_egg;
    ++m->animal_child_state_count;
    return 1;
}

int gm_mobs_set_next_sheep_child_random_state(
        GmMobLive *m, uint64_t seed48,
        int have_next_gaussian, double next_gaussian) {
    return gm_mobs_set_next_animal_child_state(
        m, seed48, have_next_gaussian, next_gaussian, 0);
}

int gm_mobs_queue_sheep_child_random_state(
        GmMobLive *m, uint64_t seed48,
        int have_next_gaussian, double next_gaussian) {
    return gm_mobs_queue_animal_child_state(
        m, seed48, have_next_gaussian, next_gaussian, 0);
}

int gm_mobs_set_sheep_state(
        GmMobLive *m, int eid, int fleece_color, int sheared) {
    int slot = mob_slot_by_eid(m, eid);
    const EwStore *s = m ? const_store(m) : NULL;
    if (slot < 0 || s->type[slot] != EW_TYPE_SHEEP
            || fleece_color < 0 || fleece_color > 15
            || (sheared != 0 && sheared != 1))
        return 0;
    m->sheep_data[slot] = (unsigned char)(fleece_color | (sheared << 4));
    return 1;
}

int gm_mobs_sheep_on_initial_spawn(
        GmMobLive *m, int eid, JavaRandom *world_random) {
    int slot = mob_slot_by_eid(m, eid);
    const EwStore *s = m ? const_store(m) : NULL;
    if (slot < 0 || s->type[slot] != EW_TYPE_SHEEP || !world_random)
        return 0;
    int color = gm_mobs_random_sheep_color(world_random);
    if (color < 0) return 0;
    m->sheep_data[slot] = (unsigned char)(
        (m->sheep_data[slot] & 0xF0u) | (unsigned char)color);
    return 1;
}

int gm_mobs_set_growing_age(
        GmMobLive *m, int eid, int growing_age) {
    int slot = mob_slot_by_eid(m, eid);
    const EwStore *s = m ? const_store(m) : NULL;
    if (slot < 0 || !gm_passive(s->type[slot]))
        return 0;
    m->growing_age[slot] = growing_age;
    return 1;
}

int gm_mobs_set_pig_saddled(GmMobLive *m, int eid, int saddled) {
    int slot = mob_slot_by_eid(m, eid);
    const EwStore *s = m ? const_store(m) : NULL;
    if (slot < 0 || s->type[slot] != EW_TYPE_PIG
            || (saddled != 0 && saddled != 1))
        return 0;
    m->pig_saddled[slot] = (unsigned char)saddled;
    return 1;
}

int gm_mobs_get_pig_saddled(
        const GmMobLive *m, int eid, int *saddled) {
    int slot = mob_slot_by_eid(m, eid);
    const EwStore *s = m ? const_store(m) : NULL;
    if (slot < 0 || s->type[slot] != EW_TYPE_PIG) return 0;
    if (saddled) *saddled = m->pig_saddled[slot] ? 1 : 0;
    return 1;
}

int gm_mobs_pig_mount(GmMobLive *m, int eid) {
    int slot = mob_slot_by_eid(m, eid);
    const EwStore *s = m ? const_store(m) : NULL;
    if (slot < 0 || s->type[slot] != EW_TYPE_PIG
            || !m->pig_saddled[slot] || m->pig_ride >= 0)
        return 0;
    /* startRiding first dismounts the player's old vehicle. */
    m->boat_ride = -1;
    m->pig_ride = slot;
    m->entity_box_valid[slot] = 0;
    m->entity_server_fall_distance[slot] =
        m->entity_fall_distance[slot];
    McAABB box;
    if (!mob_slot_world_box(m, s, slot, &box)) {
        m->pig_ride = -1;
        return 0;
    }
    m->pig_vehicle_server = (GmPigVehicleServerState){
        .valid = 1,
        .eid = eid,
        .on_ground = s->on_ground[slot] ? 1 : 0,
        /* Entity.firstUpdate is construction state, not mount state. */
        .first_update = m->entity_ticks_existed[slot] == 0,
        .x = s->x[slot],
        .y = s->y[slot],
        .z = s->z[slot],
        .vx = s->vx[slot],
        .vy = s->vy[slot],
        .vz = s->vz[slot],
        .box = box,
        .yaw = s->yaw[slot],
        .pitch = m->pig_pitch[slot],
        .fall_distance = m->entity_server_fall_distance[slot],
        .lowest_x = s->x[slot],
        .lowest_y = s->y[slot],
        .lowest_z = s->z[slot],
        .lowest_x1 = s->x[slot],
        .lowest_y1 = s->y[slot],
        .lowest_z1 = s->z[slot]
    };
    m->pig_vehicle_move_checkpoint.valid = 0;
    return 1;
}

static int pig_dismount_block_supports(GmWorld *world, int x, int y, int z) {
    int id;
    int meta;
    if (y < 0 || y > 255) return 1;
    id = gm_world_block(world, x, y, z);
    meta = gm_world_meta(world, x, y, z);
    if (gm_block_is_fully_opaque_1_11_2(id, meta)) return 1;
    if (id == 43 || id == 125 || id == 181 || id == 204) return 1;
    if (id == 44 || id == 126 || id == 182 || id == 205)
        return (meta & 8) != 0;
    if (id == 60) return 0;
    if (id == 53 || id == 67 || id == 108 || id == 109 || id == 114
            || id == 128 || id == 134 || id == 135 || id == 136
            || id == 156 || id == 163 || id == 164 || id == 180)
        return (meta & 4) != 0;
    if (id == 78) return (meta & 7) == 7;
    if (id == 154 || id == 152) return 1;
    return gm_block_is_normal_cube_1_11_2(id, meta);
}

static int pig_dismount_block_is_water(GmWorld *world, int x, int y, int z) {
    int id = gm_world_block(world, x, y, z);
    return id == 8 || id == 9;
}

static int pig_dismount_box_clear(
        GmWorld *world, const struct Chunk *window_, const McAABB *world_box,
        int ox, int oz) {
    if (window_) {
        const Chunk *window = (const Chunk *)window_;
        McAABB box = mc_aabb_offset(
            world_box, -(double)ox, 0.0, -(double)oz);
        McAABB blocks[PSV_MAX_BLOCKS];
        int n = psv_collect_blocks(window, &box, blocks, PSV_MAX_BLOCKS);
        for (int i = 0; i < n; ++i)
            if (mc_aabb_intersects(&box, &blocks[i])) return 0;
        return 1;
    }
    PcfBlock blocks[GM_MOB_BLOCKS];
    int n = collect_blocks(world, world_box, blocks, GM_MOB_BLOCKS);
    return pcf_collision_boxes_empty(world_box, blocks, n);
}

/* Entity.setPosition: update only the position and bounding box. */
static void pig_dismount_set_position(
        PsvPlayer *player, double x, double y, double z) {
    McEntity *e = &player->ent;
    double half_x = (e->box.maxX - e->box.minX) * 0.5;
    double half_z = (e->box.maxZ - e->box.minZ) * 0.5;
    double height = e->box.maxY - e->box.minY;
    e->posX = x;
    e->posY = y;
    e->posZ = z;
    e->box = mc_aabb_make(
        x - half_x, y, z - half_z, x + half_x, y + height, z + half_z);
}

void gm_mobs_pig_dismount_explicit(
        GmMobLive *m, GmWorld *world, const struct Chunk *window,
        struct PsvPlayer *player_, int ox, int oz) {
    static const signed char candidates[9][2] = {
        {0, 1}, {0, -1}, {-1, 1}, {-1, -1}, {1, 1},
        {1, -1}, {-1, 0}, {1, 0}, {0, 1}
    };
    PsvPlayer *player = (PsvPlayer *)player_;
    EwStore *s;
    int slot;
    if (!m || !world || !player || !gm_mobs_pig_riding(m, NULL)) return;
    slot = m->pig_ride;
    s = now_store(m);

    /* EntityLivingBase.dismountEntity, non-boat/non-horse branch. */
    double fallback_x = s->x[slot];
    double fallback_y = s->y[slot] + (double)0.9F;
    double fallback_z = s->z[slot];
    int facing = ((int)floor(
        (double)(s->yaw[slot] * 4.0F / 360.0F) + 0.5)) & 3;
    static const signed char forward[4][2] = {
        {0, 1}, {-1, 0}, {0, -1}, {1, 0}
    };
    int fx = forward[facing][0], fz = forward[facing][1];
    int rx = -fz, rz = fx; /* EnumFacing.rotateY() */
    double px = player->ent.posX + (double)ox;
    double pz = player->ent.posZ + (double)oz;
    double center_x = floor(px) + 0.5;
    double center_z = floor(pz) + 0.5;
    double width_x = player->ent.box.maxX - player->ent.box.minX;
    double width_z = player->ent.box.maxZ - player->ent.box.minZ;
    McAABB rider_box = mc_aabb_make(
        center_x - width_x * 0.5,
        player->ent.box.minY - (double)0.9F,
        center_z - width_z * 0.5,
        center_x + width_x * 0.5,
        player->ent.box.maxY - (double)0.9F,
        center_z + width_z * 0.5);

    for (int k = 0; k < 9; ++k) {
        double dx = (double)(fx * candidates[k][0] + rx * candidates[k][1]);
        double dz = (double)(fz * candidates[k][0] + rz * candidates[k][1]);
        double x = center_x + dx, z = center_z + dz;
        McAABB candidate = mc_aabb_offset(&rider_box, dx, 1.0, dz);
        int by = mc_floor(player->ent.posY);
        int bx = mc_floor(x), bz = mc_floor(z);
        if (pig_dismount_box_clear(world, window, &candidate, ox, oz)) {
            if (pig_dismount_block_supports(world, bx, by, bz)) {
                pig_dismount_set_position(
                    player, x - (double)ox, player->ent.posY + 1.0,
                    z - (double)oz);
                m->pig_ride = -1;
                m->pig_vehicle_server.valid = 0;
                return;
            }
            if (pig_dismount_block_supports(world, bx, by - 1, bz)
                    || pig_dismount_block_is_water(world, bx, by - 1, bz)) {
                fallback_x = x;
                fallback_y = player->ent.posY + 1.0;
                fallback_z = z;
            }
        } else {
            McAABB raised = mc_aabb_offset(&candidate, 0.0, 1.0, 0.0);
            if (pig_dismount_box_clear(world, window, &raised, ox, oz)
                    && pig_dismount_block_supports(world, bx, by + 1, bz)) {
                fallback_x = x;
                fallback_y = player->ent.posY + 2.0;
                fallback_z = z;
            }
        }
    }
    pig_dismount_set_position(
        player, fallback_x - (double)ox, fallback_y,
        fallback_z - (double)oz);
    m->pig_ride = -1;
    m->pig_vehicle_server.valid = 0;
}

void gm_mobs_pig_dismount(GmMobLive *m) {
    if (m) {
        m->pig_ride = -1;
        m->pig_vehicle_server.valid = 0;
    }
}

int gm_mobs_pig_riding(const GmMobLive *m, int *eid) {
    const EwStore *s;
    int slot;
    if (!m || m->pig_ride < 0) return 0;
    s = const_store(m);
    slot = m->pig_ride;
    if (slot <= 0 || slot >= EW_MAX_ENTITIES || !s->alive[slot]
            || s->type[slot] != EW_TYPE_PIG || !m->pig_saddled[slot])
        return 0;
    if (eid) *eid = s->id[slot];
    return 1;
}

int gm_mobs_pig_boost(
        GmMobLive *m, IsrInv *inventory, int hand_slot, int creative) {
    const EwStore *s;
    ICStack held;
    int slot;
    if (!m || !inventory || (creative != 0 && creative != 1)
            || !gm_mobs_pig_riding(m, NULL))
        return 0;
    slot = m->pig_ride;
    s = const_store(m);
    held = isr_get_stack(inventory, hand_slot);
    if (!s->alive[slot] || s->type[slot] != EW_TYPE_PIG
            || held.item != 398 || held.count <= 0
            || held.meta < 0 || 25 - held.meta < 7
            || m->pig_boosting[slot])
        return 0;
    m->pig_boosting[slot] = 1;
    m->pig_boost_time[slot] = 0;
    m->pig_boost_total[slot] =
        jrand_int_bound(&m->entity_random[slot].random, 841) + 140;
    /* Ordinary 1.11.2 sticks have no Unbreaking. The <=18 use gate plus
     * seven damage reaches at most metadata 25, while ItemStack breaks only
     * above maxDamage, so the defensive fishing-rod branch is unreachable. */
    if (!creative) {
        held.meta += 7;
        isr_set_stack(inventory, hand_slot, held);
    }
    return 1;
}

int gm_mobs_set_pig_boost_state(
        GmMobLive *m, int eid,
        int boosting, int boost_time, int boost_total) {
    int slot = mob_slot_by_eid(m, eid);
    const EwStore *s = m ? const_store(m) : NULL;
    if (slot < 0 || !s || s->type[slot] != EW_TYPE_PIG
            || (boosting != 0 && boosting != 1)
            || boost_time < 0 || boost_total < 0
            || (boosting && boost_total <= 0))
        return 0;
    m->pig_boosting[slot] = (unsigned char)boosting;
    m->pig_boost_time[slot] = boost_time;
    m->pig_boost_total[slot] = boost_total;
    return 1;
}

int gm_mobs_get_pig_boost_state(
        const GmMobLive *m, int eid,
        int *boosting, int *boost_time, int *boost_total) {
    int slot = mob_slot_by_eid(m, eid);
    const EwStore *s = m ? const_store(m) : NULL;
    if (slot < 0 || !s || s->type[slot] != EW_TYPE_PIG) return 0;
    if (boosting) *boosting = m->pig_boosting[slot] ? 1 : 0;
    if (boost_time) *boost_time = m->pig_boost_time[slot];
    if (boost_total) *boost_total = m->pig_boost_total[slot];
    return 1;
}

int gm_mobs_set_animal_breeding_state(
        GmMobLive *m, int eid, int in_love, int forced_age,
        int forced_age_timer, int bred_by_player) {
    int slot = mob_slot_by_eid(m, eid);
    const EwStore *s = m ? const_store(m) : NULL;
    if (slot < 0 || !gm_passive(s->type[slot])
            || in_love < 0 || in_love > 600
            || forced_age_timer < 0 || forced_age_timer > 40
            || (bred_by_player != 0 && bred_by_player != 1)
            || (in_love == 0 && bred_by_player))
        return 0;
    m->sheep_in_love[slot] = in_love;
    m->sheep_forced_age[slot] = forced_age;
    m->sheep_forced_age_timer[slot] = forced_age_timer;
    m->sheep_bred_by_player[slot] = (unsigned char)bred_by_player;
    return 1;
}

int gm_mobs_get_animal_breeding_state(
        const GmMobLive *m, int eid, int *growing_age, int *in_love,
        int *forced_age, int *forced_age_timer, int *bred_by_player) {
    int slot = mob_slot_by_eid(m, eid);
    const EwStore *s = m ? const_store(m) : NULL;
    if (slot < 0 || !gm_passive(s->type[slot]))
        return 0;
    if (growing_age) *growing_age = m->growing_age[slot];
    if (in_love) *in_love = m->sheep_in_love[slot];
    if (forced_age) *forced_age = m->sheep_forced_age[slot];
    if (forced_age_timer)
        *forced_age_timer = m->sheep_forced_age_timer[slot];
    if (bred_by_player)
        *bred_by_player = m->sheep_bred_by_player[slot] ? 1 : 0;
    return 1;
}

int gm_mobs_set_sheep_breeding_state(
        GmMobLive *m, int eid, int in_love, int forced_age,
        int forced_age_timer, int bred_by_player) {
    int slot = mob_slot_by_eid(m, eid);
    const EwStore *s = m ? const_store(m) : NULL;
    if (slot < 0 || s->type[slot] != EW_TYPE_SHEEP)
        return 0;
    return gm_mobs_set_animal_breeding_state(
        m, eid, in_love, forced_age, forced_age_timer, bred_by_player);
}

int gm_mobs_get_sheep_breeding_state(
        const GmMobLive *m, int eid, int *growing_age, int *in_love,
        int *forced_age, int *forced_age_timer, int *bred_by_player) {
    int slot = mob_slot_by_eid(m, eid);
    const EwStore *s = m ? const_store(m) : NULL;
    if (slot < 0 || s->type[slot] != EW_TYPE_SHEEP)
        return 0;
    return gm_mobs_get_animal_breeding_state(
        m, eid, growing_age, in_love, forced_age,
        forced_age_timer, bred_by_player);
}

int gm_mobs_animal_mate_update(
        GmMobLive *m, int initiator_eid, int mate_eid, int *delay,
        int event_cancelled, int event_child_present,
        uint64_t *world_random_seed48, uint64_t *math_random_seed48,
        int *next_entity_id, int do_mob_loot, GmSheepMateResult *out) {
    int initiator_slot, mate_slot, result;
    EwStore *s;
    if (out) memset(out, 0, sizeof *out);
    if (!m || !delay || *delay < 0 || *delay > 60)
        return GM_SHEEP_MATE_NONE;
    initiator_slot = mob_slot_by_eid(m, initiator_eid);
    mate_slot = mob_slot_by_eid(m, mate_eid);
    if (initiator_slot < 0 || mate_slot < 0
            || initiator_slot == mate_slot) {
        *delay = 0;
        return GM_SHEEP_MATE_NONE;
    }
    s = now_store(m);
    if (!s->alive[mate_slot] || !gm_passive(s->type[initiator_slot])
            || s->type[mate_slot] != s->type[initiator_slot]
            || m->entity_dimension[initiator_slot]
                != m->entity_dimension[mate_slot]
            || m->sheep_in_love[mate_slot] <= 0 || *delay >= 60) {
        *delay = 0;
        return GM_SHEEP_MATE_NONE;
    }
    ++*delay;
    result = GM_SHEEP_MATE_WAITING;
    double dx = s->x[initiator_slot] - s->x[mate_slot];
    double dy = s->y[initiator_slot] - s->y[mate_slot];
    double dz = s->z[initiator_slot] - s->z[mate_slot];
    if (*delay >= 60 && dx * dx + dy * dy + dz * dz < 9.0) {
        result = animal_breed_slots(
            m, s, initiator_slot, mate_slot,
            event_cancelled, event_child_present,
            world_random_seed48, math_random_seed48,
            next_entity_id, do_mob_loot, out);
        if (result != GM_SHEEP_MATE_NONE)
            ew_store_copy(next_store(m), s);
    }
    if (out) {
        out->result = result;
        out->delay = *delay;
    }
    return result;
}

int gm_mobs_sheep_mate_update(
        GmMobLive *m, int initiator_eid, int mate_eid, int *delay,
        int event_cancelled, int event_child_present,
        uint64_t *world_random_seed48, uint64_t *math_random_seed48,
        int *next_entity_id, int do_mob_loot, GmSheepMateResult *out) {
    int initiator_slot = mob_slot_by_eid(m, initiator_eid);
    int mate_slot = mob_slot_by_eid(m, mate_eid);
    const EwStore *s = m ? const_store(m) : NULL;
    if (initiator_slot < 0 || mate_slot < 0
            || s->type[initiator_slot] != EW_TYPE_SHEEP
            || s->type[mate_slot] != EW_TYPE_SHEEP) {
        if (out) memset(out, 0, sizeof *out);
        if (delay) *delay = 0;
        return GM_SHEEP_MATE_NONE;
    }
    return gm_mobs_animal_mate_update(
        m, initiator_eid, mate_eid, delay,
        event_cancelled, event_child_present,
        world_random_seed48, math_random_seed48,
        next_entity_id, do_mob_loot, out);
}

int gm_mobs_set_recent_hit_state(
        GmMobLive *m, int eid, int recently_hit, int attacking_player) {
    int slot = mob_slot_by_eid(m, eid);
    if (slot < 0 || recently_hit < 0 || recently_hit > 100
            || (attacking_player != 0 && attacking_player != 1)
            || (recently_hit == 0 && attacking_player))
        return 0;
    m->entity_recently_hit[slot] = recently_hit;
    m->entity_attacking_player[slot] =
        (unsigned char)attacking_player;
    return 1;
}

int gm_mobs_set_entity_fire_ticks(
        GmMobLive *m, int eid, int fire_ticks) {
    int slot = mob_slot_by_eid(m, eid);
    if (slot < 0 || fire_ticks < -20 || fire_ticks > 32767)
        return 0;
    m->fire_ticks[slot] = fire_ticks;
    return 1;
}

int gm_mobs_set_pig_server_fire_resistance(
        GmMobLive *m, int eid, int duration) {
    int slot = mob_slot_by_eid(m, eid);
    const EwStore *s = m ? const_store(m) : NULL;
    if (slot <= 0 || !s || s->type[slot] != EW_TYPE_PIG
            || m->pig_ride != slot || duration < 0 || duration > 32767)
        return 0;
    m->entity_server_fire_resistance_ticks[slot] = duration;
    return 1;
}

int gm_mobs_set_blaze_height_state(
        GmMobLive *m, int eid, int update_time, float height_offset) {
    int slot = mob_slot_by_eid(m, eid);
    const EwStore *s = m ? const_store(m) : NULL;
    if (slot < 0 || !s || s->type[slot] != EW_TYPE_BLAZE
            || update_time < 0 || update_time > 100)
        return 0;
    m->blaze_height_offset_update_time[slot] = update_time;
    m->blaze_height_offset[slot] = height_offset;
    return 1;
}

static int living_box_at(
        const GmMobLive *m, const EwStore *s, int dimension,
        int slot, McAABB *out) {
    if (!s->alive[slot] || m->entity_dimension[slot] != dimension
            || !gm_living(s->type[slot])
            || s->type[slot] == EW_TYPE_BOAT)
        return 0;
    float width, height;
    ehs_size_scaled(s->type[slot], m->size[slot], &width, &height);
    double half = (double)width * 0.5;
    *out = mc_aabb_make(
        s->x[slot] - half, s->y[slot], s->z[slot] - half,
        s->x[slot] + half, s->y[slot] + (double)height,
        s->z[slot] + half);
    return 1;
}

static double mob_math_random_next_double(uint64_t *seed48) {
    const uint64_t mask = (UINT64_C(1) << 48) - UINT64_C(1);
    uint64_t high, low;
    if (!seed48) return 0.0;
    *seed48 = (*seed48 * UINT64_C(0x5DEECE66D) + UINT64_C(0xB)) & mask;
    high = *seed48 >> (48 - 26);
    *seed48 = (*seed48 * UINT64_C(0x5DEECE66D) + UINT64_C(0xB)) & mask;
    low = *seed48 >> (48 - 27);
    return (double)((high << 27) + low)
        / (double)(UINT64_C(1) << 53);
}

static void mob_event_append(
        GmMobLive *m, int kind, int eid, int data,
        double x, double y, double z, float volume, float pitch) {
    int index;
    if (!m) return;
    if (m->event_count < GM_MOB_EVENT_CAPACITY) {
        index = (m->event_head + m->event_count)
            % GM_MOB_EVENT_CAPACITY;
        ++m->event_count;
    } else {
        index = m->event_head;
        m->event_head = (m->event_head + 1) % GM_MOB_EVENT_CAPACITY;
        ++m->event_dropped;
    }
    m->events[index] = (GmMobEvent){
        m->event_next_seq++, kind, eid, data,
        x, y, z, volume, pitch
    };
}

static int animal_breeding_item(int type, int item) {
    switch (type) {
    case EW_TYPE_SHEEP:
    case EW_TYPE_COW:
        return item == 296;
    case EW_TYPE_PIG:
        return item == 391 || item == 392 || item == 434;
    case EW_TYPE_CHICKEN:
        return item == 295 || item == 361 || item == 362 || item == 435;
    default:
        return 0;
    }
}

int gm_mobs_animal_can_feed(const GmMobLive *m, int eid, int item) {
    int slot = mob_slot_by_eid(m, eid);
    const EwStore *s = m ? const_store(m) : NULL;
    if (slot < 0 || !animal_breeding_item(s->type[slot], item))
        return 0;
    return m->growing_age[slot] < 0
        || (m->growing_age[slot] == 0 && m->sheep_in_love[slot] <= 0);
}

int gm_mobs_sheep_can_feed(const GmMobLive *m, int eid) {
    return gm_mobs_animal_can_feed(m, eid, 296);
}

static int jls_int_from_bits(uint32_t bits) {
    int32_t value;
    memcpy(&value, &bits, sizeof value);
    return (int)value;
}

static int jls_int_add(int left, int right) {
    return jls_int_from_bits(
        (uint32_t)(int32_t)left + (uint32_t)(int32_t)right);
}

static int jls_int_sub(int left, int right) {
    return jls_int_from_bits(
        (uint32_t)(int32_t)left - (uint32_t)(int32_t)right);
}

static int jls_int_mul(int left, int right) {
    return jls_int_from_bits(
        (uint32_t)(int32_t)left * (uint32_t)(int32_t)right);
}

static int jls_int_neg(int value) {
    return jls_int_from_bits(UINT32_C(0) - (uint32_t)(int32_t)value);
}

int gm_mobs_feed_animal(
        GmMobLive *m, int eid, IsrInv *inventory,
        int hand_slot, int creative) {
    int slot, age, old_age, growth_seconds, new_age, delta;
    ICStack held;
    EwStore *s;
    if (!m || !inventory || (creative != 0 && creative != 1))
        return 0;
    slot = mob_slot_by_eid(m, eid);
    if (slot < 0) return 0;
    s = now_store(m);
    held = isr_get_stack(inventory, hand_slot);
    if (!animal_breeding_item(s->type[slot], held.item) || held.count <= 0)
        return 0;

    age = m->growing_age[slot];
    if (age == 0 && m->sheep_in_love[slot] <= 0) {
        if (!creative)
            (void)isr_decr_stack_size(inventory, hand_slot, 1);
        m->sheep_in_love[slot] = 600;
        m->sheep_bred_by_player[slot] = 1;
        mob_event_append(
            m, GM_MOB_EVENT_ENTITY_STATUS, eid, 18,
            s->x[slot], s->y[slot], s->z[slot], 0.0F, 0.0F);
        return 1;
    }
    if (age >= 0)
        return 0;

    if (!creative)
        (void)isr_decr_stack_size(inventory, hand_slot, 1);
    /* Java evaluates integer division before widening to float. ageUp then
     * stores the actual tick delta in forcedAge, not the seconds argument. */
    growth_seconds = (int)((float)(jls_int_neg(age) / 20) * 0.1F);
    old_age = age;
    new_age = jls_int_add(old_age, jls_int_mul(growth_seconds, 20));
    if (new_age > 0) new_age = 0;
    delta = jls_int_sub(new_age, old_age);
    m->growing_age[slot] = new_age;
    m->sheep_forced_age[slot] = jls_int_add(
        m->sheep_forced_age[slot], delta);
    if (m->sheep_forced_age_timer[slot] == 0)
        m->sheep_forced_age_timer[slot] = 40;
    if (m->growing_age[slot] == 0)
        m->growing_age[slot] = m->sheep_forced_age[slot];
    return 1;
}

int gm_mobs_saddle_pig(
        GmMobLive *m, int eid, IsrInv *inventory,
        int hand_slot, int creative) {
    int slot;
    ICStack held;
    EwStore *s;
    if (!m || !inventory || (creative != 0 && creative != 1)) return 0;
    slot = mob_slot_by_eid(m, eid);
    if (slot < 0) return 0;
    s = now_store(m);
    held = isr_get_stack(inventory, hand_slot);
    if (s->type[slot] != EW_TYPE_PIG || held.item != 329
            || held.count <= 0)
        return 0;
    /* ItemSaddle returns true for every pig. Mutation is limited to an adult
     * unsaddled pig; creative restoration is performed by interactOn. */
    if (!m->pig_saddled[slot] && m->growing_age[slot] >= 0) {
        m->pig_saddled[slot] = 1;
        mob_event_append(
            m, GM_MOB_EVENT_SOUND, 0, GM_MOB_SOUND_PIG_SADDLE,
            s->x[slot], s->y[slot], s->z[slot], 0.5F, 1.0F);
        if (!creative)
            (void)isr_decr_stack_size(inventory, hand_slot, 1);
    }
    return 1;
}

int gm_mobs_milk_cow(
        GmMobLive *m, int eid, IsrInv *inventory,
        int hand_slot, int creative,
        double player_x, double player_y, double player_z,
        float player_yaw, float player_pitch, double player_eye_height,
        const McSinTable *sin_table, uint64_t *math_random_seed48,
        GmLiveSim *drops, int *next_entity_id) {
    EwStore *s;
    ICStack held;
    int slot, empty_slot = -1, needs_drop;
    if (!m || !inventory || (creative != 0 && creative != 1)) return 0;
    slot = mob_slot_by_eid(m, eid);
    if (slot < 0) return 0;
    s = now_store(m);
    held = isr_get_stack(inventory, hand_slot);
    if (s->type[slot] != EW_TYPE_COW || held.item != 325
            || held.count <= 0 || creative || m->growing_age[slot] < 0)
        return 0;

    if (held.count > 1)
        empty_slot = isr_get_first_empty_stack(inventory);
    needs_drop = held.count > 1 && empty_slot < 0;
    if (needs_drop && (!sin_table || !math_random_seed48 || !drops
            || !next_entity_id || *next_entity_id <= 0
            || *next_entity_id >= INT_MAX
            || mob_live_free_item_slots(drops) < 1))
        return -1;

    /* EntityCow calls EntityPlayer.playSound before shrinking the bucket. */
    mob_event_append(
        m, GM_MOB_EVENT_SOUND, 0, GM_MOB_SOUND_COW_MILK,
        player_x, player_y, player_z, 1.0F, 1.0F);
    --held.count;
    if (held.count <= 0) {
        isr_set_stack(inventory, hand_slot, ic_mk(335, 1, 0));
        /* Forge's held-slot replacement resolves one equip sound after the
         * bucket is destroyed, at the same player source and position. */
        mob_event_append(
            m, GM_MOB_EVENT_SOUND, 0,
            GM_MOB_SOUND_ITEM_ARMOR_EQUIP_GENERIC,
            player_x, player_y, player_z, 1.0F, 1.0F);
        return 1;
    }
    isr_set_stack(inventory, hand_slot, held);
    if (empty_slot >= 0) {
        /* Milk buckets are unstackable and InventoryPlayer creates them only
         * in the first empty main-inventory slot, never an empty offhand. */
        isr_set_stack(inventory, empty_slot, ic_mk(335, 1, 0));
        return 1;
    }

    {
        int item_eid = (*next_entity_id)++;
        float hover_start = (float)(
            mob_math_random_next_double(math_random_seed48)
                * (MC_PI * 2.0));
        float item_yaw = (float)(
            mob_math_random_next_double(math_random_seed48) * 360.0);
        /* The constructor consumes these two Math.random values even though
         * EntityPlayer.dropItem(false) immediately overwrites its motion. */
        (void)mob_math_random_next_double(math_random_seed48);
        (void)mob_math_random_next_double(math_random_seed48);
        float yaw = player_yaw * 0.017453292F;
        float pitch = player_pitch * 0.017453292F;
        float speed = 0.3F;
        double motion_x = (double)(
            -mc_sin(sin_table, yaw) * mc_cos(sin_table, pitch) * speed);
        double motion_z = (double)(
            mc_cos(sin_table, yaw) * mc_cos(sin_table, pitch) * speed);
        double motion_y = (double)(-mc_sin(sin_table, pitch) * speed + 0.1F);
        float spread_angle = jrand_float(&m->player_random)
            * ((float)MC_PI * 2.0F);
        float spread = 0.02F * jrand_float(&m->player_random);
        motion_x += cos((double)spread_angle) * (double)spread;
        motion_y += (double)((jrand_float(&m->player_random)
            - jrand_float(&m->player_random)) * 0.1F);
        motion_z += sin((double)spread_angle) * (double)spread;
        if (!gm_live_spawn_item_exact_hover(
                drops, item_eid, player_x,
                player_y - 0.30000001192092896 + player_eye_height,
                player_z, motion_x, motion_y, motion_z,
                item_yaw, hover_start, 335, 1, 0, 0, 40, 0))
            return -1;
        if (m->next_id <= item_eid) m->next_id = item_eid + 1;
        if (m->next_orb_id <= item_eid) m->next_orb_id = item_eid + 1;
    }
    return 1;
}

int gm_mobs_feed_sheep(
        GmMobLive *m, int eid, IsrInv *inventory,
        int hand_slot, int creative) {
    int slot = mob_slot_by_eid(m, eid);
    const EwStore *s = m ? const_store(m) : NULL;
    if (slot < 0 || s->type[slot] != EW_TYPE_SHEEP)
        return 0;
    return gm_mobs_feed_animal(m, eid, inventory, hand_slot, creative);
}

static int sheep_graze_begin_slot(
        GmMobLive *m, GmWorld *w, EwStore *s, int slot) {
    int x, y, z, bound, roll;
    if (!m || !w || !s || slot <= 0 || slot >= EW_MAX_ENTITIES
            || !s->alive[slot] || s->type[slot] != EW_TYPE_SHEEP)
        return 0;
    bound = m->growing_age[slot] < 0 ? 50 : 1000;
    roll = jrand_int_bound(&m->entity_random[slot].random, bound);
    if (roll != 0) return 0;
    x = mc_floor(s->x[slot]);
    y = mc_floor(s->y[slot]);
    z = mc_floor(s->z[slot]);
    if (!((gm_world_block(w, x, y, z) == 31
                && gm_world_meta(w, x, y, z) == 1)
            || gm_world_block(w, x, y - 1, z) == 2))
        return 0;
    m->sheep_eat_timer[slot] = 40;
    mob_event_append(
        m, GM_MOB_EVENT_ENTITY_STATUS, s->id[slot], 10,
        s->x[slot], s->y[slot], s->z[slot], 0.0F, 0.0F);
    return 1;
}

static int sheep_graze_update_slot(
        GmMobLive *m, GmWorld *w, EwStore *s, int slot,
        int mob_griefing) {
    int x, y, z, event_y, event_data;
    if (!m || !w || !s || slot <= 0 || slot >= EW_MAX_ENTITIES
            || !s->alive[slot] || s->type[slot] != EW_TYPE_SHEEP
            || m->sheep_eat_timer[slot] <= 0)
        return 0;
    if (--m->sheep_eat_timer[slot] != 4) return 0;
    x = mc_floor(s->x[slot]);
    y = mc_floor(s->y[slot]);
    z = mc_floor(s->z[slot]);
    event_y = y;
    event_data = 0;
    if (gm_world_block(w, x, y, z) == 31
            && gm_world_meta(w, x, y, z) == 1) {
        if (mob_griefing) {
            /* Block.getStateId(tallgrass[grass]) = id + (meta << 12). */
            event_data = 31 + (1 << 12);
            gm_world_set_block_meta(w, x, y, z, 0, 0);
        }
    } else if (gm_world_block(w, x, y - 1, z) == 2) {
        event_y = y - 1;
        if (mob_griefing) {
            event_data = 2;
            gm_world_set_block_meta(w, x, y - 1, z, 3, 0);
        }
    } else {
        return 0;
    }
    m->sheep_data[slot] &= (unsigned char)~16u;
    if (m->growing_age[slot] < 0) {
        if (m->growing_age[slot] > -1200)
            m->growing_age[slot] = 0;
        else
            m->growing_age[slot] += 1200;
    }
    if (mob_griefing) {
        m->sheep_world_event_pending[slot] = 1;
        m->sheep_world_event_x[slot] = x;
        m->sheep_world_event_y[slot] = event_y;
        m->sheep_world_event_z[slot] = z;
        m->sheep_world_event_data[slot] = event_data;
    }
    return 1;
}

int gm_mobs_sheep_graze_begin(GmMobLive *m, GmWorld *w, int eid) {
    int slot = mob_slot_by_eid(m, eid);
    if (slot < 0) return 0;
    return sheep_graze_begin_slot(m, w, now_store(m), slot);
}

int gm_mobs_sheep_graze_update(
        GmMobLive *m, GmWorld *w, int eid, int mob_griefing) {
    int slot = mob_slot_by_eid(m, eid);
    if (slot < 0) return 0;
    return sheep_graze_update_slot(
        m, w, now_store(m), slot, mob_griefing != 0);
}

int gm_mobs_sheep_eat_timer(const GmMobLive *m, int eid) {
    int slot = mob_slot_by_eid(m, eid);
    return slot < 0 ? -1 : m->sheep_eat_timer[slot];
}

int gm_mobs_take_sheep_world_event(
        GmMobLive *m, int *x, int *y, int *z, int *data) {
    if (!m) return 0;
    for (int slot = 1; slot < EW_MAX_ENTITIES; ++slot) {
        if (!m->sheep_world_event_pending[slot]) continue;
        m->sheep_world_event_pending[slot] = 0;
        if (x) *x = m->sheep_world_event_x[slot];
        if (y) *y = m->sheep_world_event_y[slot];
        if (z) *z = m->sheep_world_event_z[slot];
        if (data) *data = m->sheep_world_event_data[slot];
        return 1;
    }
    return 0;
}

int gm_mobs_event_count(const GmMobLive *m) {
    return m ? m->event_count : 0;
}

int gm_mobs_event_get(
        const GmMobLive *m, int index, GmMobEvent *out) {
    if (!m || !out || index < 0 || index >= m->event_count)
        return 0;
    *out = m->events[(m->event_head + index) % GM_MOB_EVENT_CAPACITY];
    return 1;
}

static double mob_ray_box_hit(
        double ex, double ey, double ez, double dx, double dy, double dz,
        const McAABB *box) {
    const double origin[3] = {ex, ey, ez};
    const double direction[3] = {dx, dy, dz};
    const double low[3] = {box->minX, box->minY, box->minZ};
    const double high[3] = {box->maxX, box->maxY, box->maxZ};
    double enter = 0.0, exit = 1.0e30;
    int inside = 1;
    for (int axis = 0; axis < 3; ++axis) {
        if (!(origin[axis] > low[axis] && origin[axis] < high[axis]))
            inside = 0;
        if (fabs(direction[axis]) < 1.0e-12) {
            if (origin[axis] < low[axis] || origin[axis] > high[axis])
                return -1.0;
            continue;
        }
        double near = (low[axis] - origin[axis]) / direction[axis];
        double far = (high[axis] - origin[axis]) / direction[axis];
        if (near > far) {
            double swap = near;
            near = far;
            far = swap;
        }
        if (near > enter) enter = near;
        if (far < exit) exit = far;
        if (enter > exit) return -1.0;
    }
    if (inside) return 0.0;
    return enter >= 0.0 ? enter : -1.0;
}

int gm_mobs_raycast_entity(
        const GmMobLive *m, int dimension,
        double ex, double ey, double ez, double dx, double dy, double dz,
        double max_distance, int *eid, int *type, double *distance) {
    const EwStore *s;
    double norm, best;
    int best_slot = -1;
    if (!m || max_distance < 0.0) return 0;
    s = const_store(m);
    norm = sqrt(dx * dx + dy * dy + dz * dz);
    if (norm <= 0.0) return 0;
    best = max_distance;
    for (int slot = 1; slot < EW_MAX_ENTITIES; ++slot) {
        float width, height;
        double half, hit, hit_distance;
        McAABB box;
        if (!s->alive[slot] || !gm_living(s->type[slot])
                || m->entity_dimension[slot] != dimension)
            continue;
        ehs_size_scaled(s->type[slot], m->size[slot], &width, &height);
        if (m->growing_age[slot] < 0 && gm_passive(s->type[slot])) {
            width *= 0.5F;
            height *= 0.5F;
        }
        half = (double)width * 0.5;
        box = mc_aabb_make(
            s->x[slot] - half - 0.1, s->y[slot] - 0.1,
            s->z[slot] - half - 0.1,
            s->x[slot] + half + 0.1,
            s->y[slot] + (double)height + 0.1,
            s->z[slot] + half + 0.1);
        hit = mob_ray_box_hit(ex, ey, ez, dx, dy, dz, &box);
        if (hit < 0.0) continue;
        hit_distance = hit * norm;
        if (hit_distance <= max_distance
                && (best_slot < 0 || hit_distance < best || best == 0.0)) {
            best = hit_distance;
            best_slot = slot;
        }
    }
    if (best_slot < 0) return 0;
    if (eid) *eid = s->id[best_slot];
    if (type) *type = s->type[best_slot];
    if (distance) *distance = best;
    return 1;
}

static int mob_live_free_item_slots(const GmLiveSim *drops) {
    int free_slots = 0;
    if (!drops) return 0;
    for (int i = 0; i < GM_LIVE_MAX; ++i)
        if (!drops->ents[i].active)
            ++free_slots;
    return free_slots;
}

int gm_mobs_shear_sheep(
        GmMobLive *m, int eid, IsrInv *inventory, int hand_slot,
        uint64_t *shear_random_seed48, uint64_t *math_random_seed48,
        GmLiveSim *drops, int *next_entity_id) {
    EwStore *s;
    ICStack held;
    JavaRandom preview, shear_random;
    int slot, count;
    if (!m || !inventory) return 0;
    slot = mob_slot_by_eid(m, eid);
    if (slot < 0) return 0;
    s = now_store(m);
    held = isr_get_stack(inventory, hand_slot);
    if (s->type[slot] != EW_TYPE_SHEEP
            || held.item != 359 || held.count <= 0)
        return 0;
    /* IShearable is handled even when its eligibility check is false. */
    if ((m->sheep_data[slot] & 16) || m->growing_age[slot] < 0)
        return 1;
    if (!shear_random_seed48 || !math_random_seed48 || !drops
            || !next_entity_id || *next_entity_id <= 0)
        return -1;

    preview = m->entity_random[slot].random;
    count = 1 + jrand_int_bound(&preview, 3);
    if (*next_entity_id > INT_MAX - count
            || mob_live_free_item_slots(drops) < count)
        return -1;

    count = 1 + jrand_int_bound(
        &m->entity_random[slot].random, 3);
    m->sheep_data[slot] |= 16;
    mob_event_append(
        m, GM_MOB_EVENT_SOUND, eid, GM_MOB_SOUND_SHEEP_SHEAR,
        s->x[slot], s->y[slot], s->z[slot], 1.0F, 1.0F);
    jrand_set_seed48(&shear_random, *shear_random_seed48);
    for (int wool = 0; wool < count; ++wool) {
        float hover_start = (float)(
            mob_math_random_next_double(math_random_seed48)
                * (MC_PI * 2.0));
        float yaw = (float)(
            mob_math_random_next_double(math_random_seed48) * 360.0);
        double motion_x = (double)(float)(
            mob_math_random_next_double(math_random_seed48)
                * 0.20000000298023224 - 0.10000000149011612);
        double motion_y = 0.20000000298023224;
        double motion_z = (double)(float)(
            mob_math_random_next_double(math_random_seed48)
                * 0.20000000298023224 - 0.10000000149011612);
        motion_y += (double)(jrand_float(&shear_random) * 0.05F);
        motion_x += (double)(
            (jrand_float(&shear_random) - jrand_float(&shear_random))
                * 0.1F);
        motion_z += (double)(
            (jrand_float(&shear_random) - jrand_float(&shear_random))
                * 0.1F);
        if (!gm_live_spawn_item_exact_hover(
                drops, (*next_entity_id)++,
                s->x[slot], s->y[slot] + 1.0, s->z[slot],
                motion_x, motion_y, motion_z, yaw, hover_start,
                35, 1, m->sheep_data[slot] & 15, 0, 10, 0))
            return -1;
    }
    *shear_random_seed48 = shear_random.seed;
    {
        ITAStack tool = mob_ita_stack(held);
        if (ita_attempt_damage(
                &tool, 1, &m->entity_random[slot].random)) {
            (void)isr_decr_stack_size(inventory, hand_slot, 1);
        } else {
            held.meta = tool.damage;
            isr_set_stack(inventory, hand_slot, held);
        }
    }
    ew_store_copy(next_store(m), s);
    return 2;
}

static int passive_sound_event(int type, int lethal) {
    if (type == EW_TYPE_CHICKEN)
        return lethal ? GM_MOB_SOUND_CHICKEN_DEATH
            : GM_MOB_SOUND_CHICKEN_HURT;
    if (type == EW_TYPE_PIG)
        return lethal ? GM_MOB_SOUND_PIG_DEATH : GM_MOB_SOUND_PIG_HURT;
    if (type == EW_TYPE_COW)
        return lethal ? GM_MOB_SOUND_COW_DEATH : GM_MOB_SOUND_COW_HURT;
    return lethal ? GM_MOB_SOUND_SHEEP_DEATH : GM_MOB_SOUND_SHEEP_HURT;
}

static float passive_sound_volume(int type) {
    return type == EW_TYPE_COW ? 0.4F : 1.0F;
}

static int mob_spawn_passive_loot_item(
        GmLiveSim *drops, int *next_entity_id,
        uint64_t *math_random_seed48,
        double x, double y, double z, int item, int count, int meta) {
    if (!drops || !next_entity_id || !math_random_seed48
            || *next_entity_id <= 0 || *next_entity_id >= INT_MAX)
        return 0;
    uint64_t staged_math_seed48 = *math_random_seed48;
    int eid = *next_entity_id;
    float hover_start = (float)(
        mob_math_random_next_double(&staged_math_seed48) * (MC_PI * 2.0));
    float yaw = (float)(
        mob_math_random_next_double(&staged_math_seed48) * 360.0);
    double motion_x = (double)(float)(
        mob_math_random_next_double(&staged_math_seed48)
            * 0.20000000298023224 - 0.10000000149011612);
    double motion_z = (double)(float)(
        mob_math_random_next_double(&staged_math_seed48)
            * 0.20000000298023224 - 0.10000000149011612);
    if (!gm_live_spawn_item_exact_hover(
        drops, eid, x, y, z,
        motion_x, 0.20000000298023224, motion_z, yaw, hover_start,
        item, count, meta, 0, 10, 0))
        return 0;
    *math_random_seed48 = staged_math_seed48;
    *next_entity_id = eid + 1;
    return 1;
}

static int mob_player_damage_pig_slot_exact(
        GmMobLive *m, EwStore *s, int slot,
        double attacker_x, double attacker_z, float damage,
        GmLiveSim *drops, const GmMobDeathContext *death_context) {
    float applied;
    int fresh;
    int lethal;
    int required_items;
    double ratio_x;
    double ratio_z;
    if (!m || !s || !death_context || slot <= 0
            || slot >= EW_MAX_ENTITIES || !s->alive[slot]
            || s->type[slot] != EW_TYPE_PIG
            || m->controlled_no_ai[slot] || m->entity_dead[slot]
            || s->health[slot] <= 0.0F || damage <= 0.0F)
        return 1;
    fresh = m->entity_hurt_resistant[slot] <= 10;
    if (!fresh && damage <= m->entity_last_damage[slot])
        return 1;
    if (fresh && !death_context->math_random_seed48)
        return 1;
    applied = fresh ? damage : damage - m->entity_last_damage[slot];
    applied = mob_resistance_damage(m, slot, applied);
    lethal = s->health[slot]
        <= mob_preview_absorbed_damage(m, slot, applied);
    required_items = lethal
        ? (death_context->do_mob_loot != 0) + (m->pig_saddled[slot] != 0)
        : 0;
    if (required_items > 0
            && (!drops || !death_context->math_random_seed48
                || !death_context->next_entity_id
                || *death_context->next_entity_id <= 0
                || *death_context->next_entity_id
                    > INT_MAX - required_items
                || mob_live_free_item_slots(drops) < required_items))
        return 1;

    applied = mob_absorb_damage(m, slot, applied);
    m->entity_last_damage[slot] = damage;
    if (fresh) {
        m->entity_hurt_resistant[slot] = 20;
        m->entity_hurt_time[slot] = 10;
    }
    s->health[slot] -= applied;
    if (s->health[slot] < 0.0F) s->health[slot] = 0.0F;
    mark_hurt(m, s, slot);
    m->entity_recently_hit[slot] = 100;
    m->entity_attacking_player[slot] = 1;

    if (fresh) {
        float sound_pitch;
        mob_event_append(
            m, GM_MOB_EVENT_ENTITY_STATUS, s->id[slot], 2,
            s->x[slot], s->y[slot], s->z[slot], 0.0F, 0.0F);
        /* EntityLivingBase.setBeenAttacked then knockBack each test the
         * passive's zero knockback-resistance attribute. */
        (void)jrand_double(&m->entity_random[slot].random);
        ratio_x = attacker_x - s->x[slot];
        ratio_z = attacker_z - s->z[slot];
        while (ratio_x * ratio_x + ratio_z * ratio_z < 1.0E-4D) {
            ratio_x = (mob_math_random_next_double(
                    death_context->math_random_seed48)
                - mob_math_random_next_double(
                    death_context->math_random_seed48)) * 0.01D;
            ratio_z = (mob_math_random_next_double(
                    death_context->math_random_seed48)
                - mob_math_random_next_double(
                    death_context->math_random_seed48)) * 0.01D;
        }
        (void)jrand_double(&m->entity_random[slot].random);
        {
            float length = (float)sqrt(
                ratio_x * ratio_x + ratio_z * ratio_z);
            s->vx[slot] /= 2.0D;
            s->vz[slot] /= 2.0D;
            s->vx[slot] -= ratio_x / (double)length
                * (double)0.4F;
            s->vz[slot] -= ratio_z / (double)length
                * (double)0.4F;
            if (s->on_ground[slot]) {
                s->vy[slot] /= 2.0D;
                s->vy[slot] += (double)0.4F;
                if (s->vy[slot] > 0.4000000059604645D)
                    s->vy[slot] = 0.4000000059604645D;
            }
        }
        {
            float first = jrand_float(&m->entity_random[slot].random);
            float second = jrand_float(&m->entity_random[slot].random);
            sound_pitch = (first - second) * 0.2F + 1.0F;
        }
        mob_event_append(
            m, GM_MOB_EVENT_SOUND, s->id[slot],
            lethal ? GM_MOB_SOUND_PIG_DEATH : GM_MOB_SOUND_PIG_HURT,
            s->x[slot], s->y[slot], s->z[slot], 1.0F, sound_pitch);
    }

    if (lethal) {
        m->entity_dead[slot] = 1;
        m->entity_death_time[slot] = 0;
        if (death_context->do_mob_loot) {
            int pork_count;
            (void)jrand_int_bound(&m->entity_random[slot].random, 1);
            pork_count = 1 + jrand_int_bound(
                &m->entity_random[slot].random, 3);
            (void)mob_spawn_passive_loot_item(
                drops, death_context->next_entity_id,
                death_context->math_random_seed48,
                s->x[slot], s->y[slot], s->z[slot],
                m->fire_ticks[slot] > 0 ? 320 : 319,
                pork_count, 0);
        }
        if (m->pig_saddled[slot])
            (void)mob_spawn_passive_loot_item(
                drops, death_context->next_entity_id,
                death_context->math_random_seed48,
                s->x[slot], s->y[slot], s->z[slot], 329, 1, 0);
        if (death_context->next_entity_id) {
            if (m->next_id < *death_context->next_entity_id)
                m->next_id = *death_context->next_entity_id;
            if (m->next_orb_id < *death_context->next_entity_id)
                m->next_orb_id = *death_context->next_entity_id;
        }
        mob_event_append(
            m, GM_MOB_EVENT_ENTITY_STATUS, s->id[slot], 3,
            s->x[slot], s->y[slot], s->z[slot], 0.0F, 0.0F);
    }
    return 2;
}

int gm_mobs_player_damage_pig_exact(
        GmMobLive *m, int eid, double attacker_x, double attacker_z,
        float damage, GmLiveSim *drops,
        const GmMobDeathContext *death_context) {
    if (!m || eid <= 0) return 0;
    EwStore *s = now_store(m);
    int slot = mob_slot_by_eid(m, eid);
    if (slot < 0 || s->type[slot] != EW_TYPE_PIG) return 0;
    int result = mob_player_damage_pig_slot_exact(
        m, s, slot, attacker_x, attacker_z,
        damage, drops, death_context);
    ew_store_copy(next_store(m), s);
    return result;
}

int gm_mobs_falling_anvil_damage_controlled_passives(
        GmMobLive *m, int dimension, const McAABB *falling_box,
        float damage, uint64_t *math_random_seed48, GmLiveSim *drops,
        int *next_entity_id, int do_mob_loot) {
    EwStore *s;
    int accepted_count = 0;
    if (!m || !falling_box || !math_random_seed48 || damage <= 0.0F
            || (do_mob_loot && (!drops || !next_entity_id
                || *next_entity_id <= 0)))
        return 0;
    s = now_store(m);
    for (int slot = 1; slot < EW_MAX_ENTITIES; ++slot) {
        McAABB target_box;
        int fresh;
        int lethal;
        float applied;
        float sound_pitch = 1.0F;
        if (!m->controlled_no_ai[slot]
                || (s->type[slot] != EW_TYPE_SHEEP
                    && s->type[slot] != EW_TYPE_PIG
                    && s->type[slot] != EW_TYPE_COW
                    && s->type[slot] != EW_TYPE_CHICKEN)
                || m->entity_dead[slot] || s->health[slot] <= 0.0F
                || !living_box_at(m, s, dimension, slot, &target_box)
                || !mc_aabb_intersects(falling_box, &target_box))
            continue;
        fresh = m->entity_hurt_resistant[slot] <= 10;
        if (!fresh && damage <= m->entity_last_damage[slot])
            continue;
        applied = fresh ? damage : damage - m->entity_last_damage[slot];
        applied = mob_resistance_damage(m, slot, applied);
        lethal = s->health[slot]
            <= mob_preview_absorbed_damage(m, slot, applied);
        if (lethal && (do_mob_loot
                    || (s->type[slot] == EW_TYPE_PIG
                        && m->pig_saddled[slot]))) {
            JavaRandom preview = m->entity_random[slot].random;
            int required_slots =
                s->type[slot] == EW_TYPE_PIG && m->pig_saddled[slot];
            if (do_mob_loot) {
                if (fresh) {
                    (void)jrand_double(&preview);
                    (void)jrand_float(&preview);
                    (void)jrand_float(&preview);
                }
                (void)jrand_int_bound(&preview, 1);
                if (s->type[slot] == EW_TYPE_CHICKEN) {
                    int feather_count = jrand_int_bound(&preview, 3);
                    (void)jrand_int_bound(&preview, 1);
                    required_slots += 1 + (feather_count > 0);
                } else if (s->type[slot] == EW_TYPE_PIG) {
                    (void)jrand_int_bound(&preview, 3);
                    required_slots += 1;
                } else if (s->type[slot] == EW_TYPE_COW) {
                    int leather_count = jrand_int_bound(&preview, 3);
                    (void)jrand_int_bound(&preview, 1);
                    (void)jrand_int_bound(&preview, 3);
                    required_slots += 1 + (leather_count > 0);
                } else {
                    if (!(m->sheep_data[slot] & 16)) {
                        (void)jrand_int_bound(&preview, 1);
                        (void)jrand_int_bound(&preview, 1);
                    }
                    (void)jrand_int_bound(&preview, 2);
                    required_slots +=
                        (m->sheep_data[slot] & 16) ? 1 : 2;
                }
            }
            if (!drops || !next_entity_id || *next_entity_id <= 0
                    || *next_entity_id > INT_MAX - required_slots
                    || mob_live_free_item_slots(drops) < required_slots)
                continue;
        }
        applied = mob_absorb_damage(m, slot, applied);
        if (fresh) {
            if (gm_passive(s->type[slot])) {
                m->sheep_in_love[slot] = 0;
                m->sheep_bred_by_player[slot] = 0;
            }
            mob_event_append(
                m, GM_MOB_EVENT_ENTITY_STATUS, s->id[slot], 2,
                s->x[slot], s->y[slot], s->z[slot], 0.0F, 0.0F);
            m->entity_last_damage[slot] = damage;
            m->entity_hurt_resistant[slot] = 20;
            m->entity_hurt_time[slot] = 10;
            /* setBeenAttacked reads one nextDouble before the no-attacker
             * yaw choice; the later sound pitch reads two nextFloat calls. */
            (void)jrand_double(&m->entity_random[slot].random);
            (void)mob_math_random_next_double(math_random_seed48);
            {
                float a = jrand_float(&m->entity_random[slot].random);
                float b = jrand_float(&m->entity_random[slot].random);
                sound_pitch = (a - b) * 0.2F + 1.0F;
            }
            mob_event_append(
                m, GM_MOB_EVENT_SOUND, s->id[slot],
                passive_sound_event(s->type[slot], lethal),
                s->x[slot], s->y[slot], s->z[slot],
                passive_sound_volume(s->type[slot]), sound_pitch);
        } else {
            m->entity_last_damage[slot] = damage;
        }
        s->health[slot] -= applied;
        if (s->health[slot] < 0.0F) s->health[slot] = 0.0F;
        if (s->health[slot] <= 0.0F) {
            m->entity_dead[slot] = 1;
            if (do_mob_loot && s->type[slot] == EW_TYPE_CHICKEN) {
                int feather_count;
                /* Pool order is feather, then raw chicken. Constant rolls do
                 * not draw; each one-entry pool still consumes nextInt(1). */
                (void)jrand_int_bound(
                    &m->entity_random[slot].random, 1);
                feather_count = jrand_int_bound(
                    &m->entity_random[slot].random, 3);
                (void)jrand_int_bound(
                    &m->entity_random[slot].random, 1);
                if (feather_count > 0)
                    (void)mob_spawn_passive_loot_item(
                        drops, next_entity_id, math_random_seed48,
                        s->x[slot], s->y[slot], s->z[slot],
                        288, feather_count, 0);
                (void)mob_spawn_passive_loot_item(
                    drops, next_entity_id, math_random_seed48,
                    s->x[slot], s->y[slot], s->z[slot],
                    m->fire_ticks[slot] > 0 ? 366 : 365, 1, 0);
            } else if (do_mob_loot && s->type[slot] == EW_TYPE_PIG) {
                int pork_count;
                /* Pig has one fixed-roll, one-entry pool. The entry choice
                 * consumes nextInt(1), then set_count consumes nextInt(3). */
                (void)jrand_int_bound(
                    &m->entity_random[slot].random, 1);
                pork_count = 1 + jrand_int_bound(
                    &m->entity_random[slot].random, 3);
                (void)mob_spawn_passive_loot_item(
                    drops, next_entity_id, math_random_seed48,
                    s->x[slot], s->y[slot], s->z[slot],
                    m->fire_ticks[slot] > 0 ? 320 : 319, pork_count, 0);
            } else if (do_mob_loot && s->type[slot] == EW_TYPE_COW) {
                int leather_count;
                int beef_count;
                /* Pool order is leather, then beef. Both fixed-roll,
                 * one-entry pools consume nextInt(1) before set_count. */
                (void)jrand_int_bound(
                    &m->entity_random[slot].random, 1);
                leather_count = jrand_int_bound(
                    &m->entity_random[slot].random, 3);
                (void)jrand_int_bound(
                    &m->entity_random[slot].random, 1);
                beef_count = 1 + jrand_int_bound(
                    &m->entity_random[slot].random, 3);
                if (leather_count > 0)
                    (void)mob_spawn_passive_loot_item(
                        drops, next_entity_id, math_random_seed48,
                        s->x[slot], s->y[slot], s->z[slot],
                        334, leather_count, 0);
                (void)mob_spawn_passive_loot_item(
                    drops, next_entity_id, math_random_seed48,
                    s->x[slot], s->y[slot], s->z[slot],
                    m->fire_ticks[slot] > 0 ? 364 : 363, beef_count, 0);
            } else if (do_mob_loot && s->type[slot] == EW_TYPE_SHEEP) {
                int mutton_count;
                /* Unsheared color tables emit wool before entering the base
                 * sheep table. Sheared sheep select that base table directly. */
                if (!(m->sheep_data[slot] & 16)) {
                    (void)jrand_int_bound(
                        &m->entity_random[slot].random, 1);
                    (void)jrand_int_bound(
                        &m->entity_random[slot].random, 1);
                }
                (void)jrand_int_bound(
                    &m->entity_random[slot].random, 1);
                mutton_count = 1 + jrand_int_bound(
                    &m->entity_random[slot].random, 2);
                if (!(m->sheep_data[slot] & 16))
                    (void)mob_spawn_passive_loot_item(
                        drops, next_entity_id, math_random_seed48,
                        s->x[slot], s->y[slot], s->z[slot],
                        35, 1, m->sheep_data[slot] & 15);
                (void)mob_spawn_passive_loot_item(
                    drops, next_entity_id, math_random_seed48,
                    s->x[slot], s->y[slot], s->z[slot],
                    m->fire_ticks[slot] > 0 ? 424 : 423, mutton_count, 0);
            }
            if (s->type[slot] == EW_TYPE_PIG && m->pig_saddled[slot])
                (void)mob_spawn_passive_loot_item(
                    drops, next_entity_id, math_random_seed48,
                    s->x[slot], s->y[slot], s->z[slot], 329, 1, 0);
            mob_event_append(
                m, GM_MOB_EVENT_ENTITY_STATUS, s->id[slot], 3,
                s->x[slot], s->y[slot], s->z[slot], 0.0F, 0.0F);
        }
        ++accepted_count;
    }
    return accepted_count;
}

static int boat_box_at(
        const GmMobLive *m, const EwStore *s, int dimension,
        int slot, McAABB *out) {
    if (!s->alive[slot] || m->entity_dimension[slot] != dimension
            || s->type[slot] != EW_TYPE_BOAT)
        return 0;
    float width, height;
    ehs_size(EW_TYPE_BOAT, &width, &height);
    double half = (double)width * 0.5;
    *out = mc_aabb_make(
        s->x[slot] - half, s->y[slot], s->z[slot] - half,
        s->x[slot] + half, s->y[slot] + (double)height,
        s->z[slot] + half);
    return 1;
}

int gm_mobs_living_boxes(
        const GmMobLive *m, int dimension, McAABB *out, int capacity) {
    if (!m || !out || capacity <= 0) return 0;
    const EwStore *s = const_store(m);
    int count = 0;
    for (int i = 1; i < EW_MAX_ENTITIES && count < capacity; ++i)
        if (living_box_at(m, s, dimension, i, &out[count]))
            ++count;
    return count;
}

int gm_mobs_explosion_targets(
        const GmMobLive *m, int dimension,
        GmMobExplosionTarget *out, int capacity) {
    if (!m || !out || capacity <= 0) return 0;
    const EwStore *s = const_store(m);
    int count = 0;
    for (int slot = 1;
            slot < EW_MAX_ENTITIES && count < capacity; ++slot) {
        McAABB box;
        float width, height;
        GmMobExplosionTarget *target;
        if (!s->alive[slot] || m->entity_dimension[slot] != dimension
                || !gm_living(s->type[slot]))
            continue;
        if (s->type[slot] == EW_TYPE_BOAT) {
            if (!boat_box_at(m, s, dimension, slot, &box)) continue;
            ehs_size(EW_TYPE_BOAT, &width, &height);
        } else {
            if (!living_box_at(m, s, dimension, slot, &box)) continue;
            ehs_size_scaled(
                s->type[slot], m->size[slot], &width, &height);
        }
        (void)width;
        target = &out[count++];
        memset(target, 0, sizeof *target);
        target->slot = slot;
        target->eid = s->id[slot];
        target->type = s->type[slot];
        target->x = s->x[slot];
        target->y = s->y[slot];
        target->z = s->z[slot];
        target->vx = s->vx[slot];
        target->vy = s->vy[slot];
        target->vz = s->vz[slot];
        target->health = s->health[slot];
        target->eye_height = height * 0.85F;
        target->hurt_time = m->entity_hurt_time[slot];
        target->hurt_resistant_time = m->entity_hurt_resistant[slot];
        target->box = box;
    }
    return count;
}

int gm_mobs_apply_explosion(
        GmMobLive *m, int slot, float damage,
        double impulse_x, double impulse_y, double impulse_z,
        GmLiveSim *drops) {
    EwStore *s;
    float applied;
    int accepted = 1;
    if (!m || slot <= 0 || slot >= EW_MAX_ENTITIES || damage <= 0.0F
            || !isfinite(impulse_x) || !isfinite(impulse_y)
            || !isfinite(impulse_z))
        return 0;
    s = now_store(m);
    if (!s->alive[slot] || m->entity_dead[slot]
            || s->health[slot] <= 0.0F || !gm_living(s->type[slot]))
        return 0;
    if (s->type[slot] == EW_TYPE_BOAT) {
        m->boat_damage[slot] += damage * 10.0F;
        if (m->boat_damage[slot] > 40.0F)
            mob_drop(m, s, slot, drops);
        if (s->alive[slot]) {
            s->vx[slot] += impulse_x;
            s->vy[slot] += impulse_y;
            s->vz[slot] += impulse_z;
        }
        ew_store_copy(next_store(m), s);
        return 2;
    }
    if (m->entity_hurt_resistant[slot] > 10) {
        if (damage <= m->entity_last_damage[slot]) {
            accepted = 0;
            applied = 0.0F;
        } else {
            applied = damage - m->entity_last_damage[slot];
            m->entity_last_damage[slot] = damage;
        }
    } else {
        applied = damage;
        m->entity_last_damage[slot] = damage;
        m->entity_hurt_resistant[slot] = 20;
        m->entity_hurt_time[slot] = 10;
    }
    if (accepted) {
        applied = mob_resistance_damage(m, slot, applied);
        applied = mob_absorb_damage(m, slot, applied);
        s->health[slot] -= applied;
        if (s->health[slot] < 0.0F) s->health[slot] = 0.0F;
        mark_hurt(m, s, slot);
        if (s->health[slot] <= 0.0F && !m->controlled_no_ai[slot])
            mob_drop(m, s, slot, drops);
    }
    if (s->alive[slot]) {
        s->vx[slot] += impulse_x;
        s->vy[slot] += impulse_y;
        s->vz[slot] += impulse_z;
    }
    ew_store_copy(next_store(m), s);
    return accepted ? 2 : 1;
}

int gm_mobs_creeper_is_powered(
        const GmMobLive *m, int eid, int *powered) {
    int slot = mob_slot_by_eid(m, eid);
    const EwStore *s = const_store(m);
    if (slot < 0 || s->type[slot] != EW_TYPE_CREEPER)
        return 0;
    if (powered) *powered = m->creeper_powered[slot] ? 1 : 0;
    return 1;
}

int gm_mobs_lightning_strike(
        GmMobLive *m, int dimension, const McAABB *box,
        GmLiveSim *drops, int *next_entity_id) {
    int slots[EW_MAX_ENTITIES];
    int count = 0;
    EwStore *s;
    if (!m || !box || !next_entity_id)
        return 0;
    s = now_store(m);
    /* World.getEntitiesWithinAABBExcludingEntity snapshots before callbacks;
     * a pigman created by a pig callback is not struck by this same flash. */
    for (int slot = 1; slot < EW_MAX_ENTITIES; ++slot) {
        McAABB entity_box;
        if (living_box_at(m, s, dimension, slot, &entity_box)
                && mc_aabb_intersects(&entity_box, box))
            slots[count++] = slot;
    }
    for (int index = 0; index < count; ++index) {
        int slot = slots[index];
        int type;
        if (!s->alive[slot]) continue;
        type = s->type[slot];
        if (type == EW_TYPE_PIG) {
            double x = s->x[slot], y = s->y[slot], z = s->z[slot];
            float yaw = s->yaw[slot];
            int no_ai = m->controlled_no_ai[slot];
            int new_eid = (*next_entity_id)++;
            int new_slot = ew_store_spawn(
                s, EW_TYPE_PIGMAN, new_eid, x, y, z,
                max_health(EW_TYPE_PIGMAN, 1));
            if (new_slot >= 0) {
                s->yaw[new_slot] = yaw;
                m->entity_dimension[new_slot] = (signed char)dimension;
                m->size[new_slot] = 1;
                reset_slot_state_s(m, s, new_slot);
                m->controlled_no_ai[new_slot] = (unsigned char)no_ai;
                (void)loaded_append_living(m, s, new_slot);
                if (m->next_id <= new_eid) m->next_id = new_eid + 1;
            }
            retire_terminal_living(m, s, slot);
            continue;
        }
        if (gm_living(type) && !m->entity_dead[slot]
                && s->health[slot] > 0.0F) {
            float raw = 5.0F;
            float applied;
            int accepted = 1;
            if (m->entity_hurt_resistant[slot] > 10) {
                if (raw <= m->entity_last_damage[slot]) {
                    accepted = 0;
                    applied = 0.0F;
                } else {
                    applied = raw - m->entity_last_damage[slot];
                    m->entity_last_damage[slot] = raw;
                }
            } else {
                applied = raw;
                m->entity_last_damage[slot] = raw;
                m->entity_hurt_resistant[slot] = 20;
                m->entity_hurt_time[slot] = 10;
            }
            if (accepted) {
                applied = player_attack_after_mob_armor(
                    m, slot, type, applied);
                applied = mob_resistance_damage(m, slot, applied);
                applied = mob_absorb_damage(m, slot, applied);
                s->health[slot] -= applied;
                if (s->health[slot] < 0.0F) s->health[slot] = 0.0F;
                mark_hurt(m, s, slot);
                if (s->health[slot] <= 0.0F
                        && !m->controlled_no_ai[slot])
                    mob_drop(m, s, slot, drops);
            }
            if (s->alive[slot]) {
                ++m->fire_ticks[slot];
                if (m->fire_ticks[slot] == 0)
                    m->fire_ticks[slot] = 8 * 20;
                if (type == EW_TYPE_CREEPER)
                    m->creeper_powered[slot] = 1;
            }
        }
    }
    ew_store_copy(next_store(m), s);
    return count;
}

static int mob_is_undead(int type) {
    return type == EW_TYPE_ZOMBIE || type == EW_TYPE_SKELETON
        || type == EW_TYPE_PIGMAN || type == EW_TYPE_WITHER_SKELETON;
}

int gm_mobs_apply_potion_effect(
        GmMobLive *m, int slot, int potion_id,
        int amplifier, int duration) {
    const EwStore *s;
    int count;
    if (!m || slot <= 0 || slot >= EW_MAX_ENTITIES
            || potion_id <= 0 || potion_id > 27
            || potion_id == 6 || potion_id == 7
            || amplifier < 0 || amplifier > 255 || duration <= 0)
        return 0;
    s = const_store(m);
    if (!s->alive[slot] || m->entity_dead[slot]
            || s->health[slot] <= 0.0F || !gm_living(s->type[slot])
            || s->type[slot] == EW_TYPE_BOAT)
        return 0;
    if (mob_is_undead(s->type[slot])
            && (potion_id == 10 || potion_id == 19))
        return 0;
    count = m->entity_effect_count[slot];
    for (int i = 0; i < count; ++i)
        if (m->entity_effects[slot][i].id == potion_id) {
            pt_effect_combine(
                &m->entity_effects[slot][i], duration, amplifier);
            if (potion_id == 22) {
                float grant = pt_effect_health_boost(
                    0.0F, m->entity_effects[slot][i].amplifier);
                m->entity_absorption[slot] -= grant;
                if (m->entity_absorption[slot] < 0.0F)
                    m->entity_absorption[slot] = 0.0F;
                m->entity_absorption[slot] += grant;
            }
            return 1;
        }
    if (count >= GM_MOB_EFFECT_CAPACITY) return 0;
    m->entity_effects[slot][count].id = potion_id;
    m->entity_effects[slot][count].duration = duration;
    m->entity_effects[slot][count].amplifier = amplifier;
    m->entity_effect_count[slot] = (unsigned char)(count + 1);
    if (potion_id == 22)
        m->entity_absorption[slot] +=
            pt_effect_health_boost(0.0F, amplifier);
    return 1;
}

int gm_mobs_potion_effect_count(const GmMobLive *m, int slot) {
    if (!m || slot <= 0 || slot >= EW_MAX_ENTITIES) return 0;
    return m->entity_effect_count[slot];
}

int gm_mobs_potion_effect_get(
        const GmMobLive *m, int slot, int index, PtMobEffect *out) {
    if (!m || !out || slot <= 0 || slot >= EW_MAX_ENTITIES
            || index < 0 || index >= m->entity_effect_count[slot])
        return 0;
    *out = m->entity_effects[slot][index];
    return 1;
}

float gm_mobs_max_health(const GmMobLive *m, int slot) {
    const EwStore *s;
    if (!m || slot <= 0 || slot >= EW_MAX_ENTITIES) return 0.0F;
    s = const_store(m);
    if (!s->alive[slot] || !gm_living(s->type[slot])
            || s->type[slot] == EW_TYPE_BOAT)
        return 0.0F;
    return mob_max_health(m, slot, s->type[slot]);
}

float gm_mobs_absorption(const GmMobLive *m, int slot) {
    if (!m || slot <= 0 || slot >= EW_MAX_ENTITIES) return 0.0F;
    return m->entity_absorption[slot];
}

int gm_mobs_air(const GmMobLive *m, int slot) {
    const EwStore *s;
    if (!m || slot <= 0 || slot >= EW_MAX_ENTITIES) return 0;
    s = const_store(m);
    if (!s->alive[slot] || !gm_living(s->type[slot])
            || s->type[slot] == EW_TYPE_BOAT)
        return 0;
    return m->entity_air[slot];
}

int gm_mobs_set_air(GmMobLive *m, int eid, int air) {
    int slot = mob_slot_by_eid(m, eid);
    const EwStore *s = m ? const_store(m) : NULL;
    if (slot <= 0 || !s || !gm_living(s->type[slot])
            || s->type[slot] == EW_TYPE_BOAT
            || air < -20 || air > 300)
        return 0;
    m->entity_air[slot] = air;
    return 1;
}

int gm_mobs_apply_instant_potion(
        GmMobLive *m, int slot, int potion_id, int amplifier,
        double factor, GmLiveSim *drops) {
    EwStore *s;
    int delta;
    if (!m || slot <= 0 || slot >= EW_MAX_ENTITIES
            || amplifier < 0 || factor < 0.0 || !isfinite(factor))
        return 0;
    s = now_store(m);
    if (!s->alive[slot] || m->entity_dead[slot]
            || s->health[slot] <= 0.0F || !gm_living(s->type[slot])
            || s->type[slot] == EW_TYPE_BOAT)
        return 0;
    delta = pt_instant_health_delta(
        potion_id, amplifier, factor, mob_is_undead(s->type[slot]));
    if (delta < 0)
        return gm_mobs_apply_explosion(
            m, slot, (float)-delta, 0.0, 0.0, 0.0, drops);
    if (delta > 0) {
        s->health[slot] += (float)delta;
        if (s->health[slot] > mob_max_health(m, slot, s->type[slot]))
            s->health[slot] = mob_max_health(m, slot, s->type[slot]);
        ew_store_copy(next_store(m), s);
    }
    return 2;
}

int gm_mobs_apply_water_potion(
        GmMobLive *m, int slot, GmLiveSim *drops) {
    const EwStore *s;
    if (!m || slot <= 0 || slot >= EW_MAX_ENTITIES) return 0;
    s = const_store(m);
    if (!s->alive[slot] || (s->type[slot] != EW_TYPE_BLAZE
            && s->type[slot] != EW_TYPE_ENDERMAN))
        return 0;
    return gm_mobs_apply_explosion(
        m, slot, 1.0F, 0.0, 0.0, 0.0, drops);
}

int gm_mobs_collision_boxes(
        const GmMobLive *m, int dimension, int controlled_only,
        McAABB *out, int capacity) {
    if (!m || !out || capacity <= 0) return 0;
    const EwStore *s = const_store(m);
    int count = 0;
    for (int i = 1; i < EW_MAX_ENTITIES && count < capacity; ++i) {
        if (controlled_only && !m->controlled_block_collisions[i])
            continue;
        if (living_box_at(m, s, dimension, i, &out[count]))
            ++count;
    }
    return count;
}

int gm_mobs_trigger_collision_boxes(
        const GmMobLive *m, int dimension, int controlled_only,
        McAABB *out, int capacity) {
    if (!m || !out || capacity <= 0) return 0;
    const EwStore *s = const_store(m);
    int count = 0;
    for (int i = 1; i < EW_MAX_ENTITIES && count < capacity; ++i) {
        if (controlled_only && !m->controlled_block_collisions[i])
            continue;
        if (living_box_at(m, s, dimension, i, &out[count])
                || boat_box_at(m, s, dimension, i, &out[count]))
            ++count;
    }
    return count;
}

int gm_mobs_living_intersects_aabb(
        const GmMobLive *m, int dimension, const McAABB *box) {
    return gm_mobs_living_count_intersects_aabb(m, dimension, box) > 0;
}

int gm_mobs_living_count_intersects_aabb(
        const GmMobLive *m, int dimension, const McAABB *box) {
    if (!m || !box) return 0;
    const EwStore *s = const_store(m);
    int count = 0;
    for (int i = 1; i < EW_MAX_ENTITIES; ++i) {
        McAABB entity;
        if (!living_box_at(m, s, dimension, i, &entity))
            continue;
        if (entity.maxX > box->minX && entity.minX < box->maxX
                && entity.maxY > box->minY && entity.minY < box->maxY
                && entity.maxZ > box->minZ && entity.minZ < box->maxZ)
            ++count;
    }
    return count;
}

int gm_mobs_boat_count_intersects_aabb(
        const GmMobLive *m, int dimension, const McAABB *box) {
    if (!m || !box) return 0;
    const EwStore *s = const_store(m);
    int count = 0;
    for (int i = 1; i < EW_MAX_ENTITIES; ++i) {
        McAABB entity;
        if (!boat_box_at(m, s, dimension, i, &entity))
            continue;
        if (entity.maxX > box->minX && entity.minX < box->maxX
                && entity.maxY > box->minY && entity.minY < box->maxY
                && entity.maxZ > box->minZ && entity.minZ < box->maxZ)
            ++count;
    }
    return count;
}

int gm_mobs_xp_collision_boxes(
        const GmMobLive *m, McAABB *out, int capacity) {
    if (!m || !out || capacity <= 0) return 0;
    int count = m->xp_collision_count;
    if (count > capacity) count = capacity;
    memcpy(out, m->xp_collision_boxes, (size_t)count * sizeof *out);
    return count;
}

int gm_mobs_xp_count_intersects_aabb(
        const GmMobLive *m, int dimension, const McAABB *box) {
    if (!m || !box) return 0;
    int count = 0;
    for (int i = 0; i < GM_XP_ORBS; ++i) {
        const McOrb *orb = &m->xp_orbs[i];
        if (orb->dead || orb->xpValue <= 0
                || m->orb_dimension[i] != dimension)
            continue;
        if (orb->box.maxX > box->minX && orb->box.minX < box->maxX
                && orb->box.maxY > box->minY && orb->box.minY < box->maxY
                && orb->box.maxZ > box->minZ && orb->box.minZ < box->maxZ)
            ++count;
    }
    return count;
}

int gm_mobs_projectile_intercept(
        const GmMobLive *m, int dimension, int shooter_eid,
        int include_shooter, double sx, double sy, double sz,
        double ex, double ey, double ez, int *slot, double *distance_sq) {
    if (!m) return 0;
    const EwStore *s = const_store(m);
    const double expand = 0.30000001192092896;
    int best_slot = -1;
    double best_dist = 0.0;
    for (int i = 1; i < EW_MAX_ENTITIES; ++i) {
        float width, height;
        double half, hx, hy, hz, dx, dy, dz, dist;
        int side;
        McAABB box;
        if (!s->alive[i] || m->entity_dimension[i] != dimension
                || !gm_living(s->type[i]))
            continue;
        if (!include_shooter && shooter_eid > 0 && s->id[i] == shooter_eid)
            continue;
        ehs_size_scaled(s->type[i], m->size[i], &width, &height);
        half = (double)width * 0.5;
        box = mc_aabb_make(
            s->x[i] - half - expand, s->y[i] - expand,
            s->z[i] - half - expand, s->x[i] + half + expand,
            s->y[i] + (double)height + expand,
            s->z[i] + half + expand);
        if (!pm_aabb_intercept(
                &box, sx, sy, sz, ex, ey, ez,
                &hx, &hy, &hz, &side))
            continue;
        dx = hx - sx; dy = hy - sy; dz = hz - sz;
        dist = dx * dx + dy * dy + dz * dz;
        if (best_slot >= 0 && !(dist < best_dist))
            continue;
        best_slot = i;
        best_dist = dist;
    }
    if (best_slot < 0) return 0;
    if (slot) *slot = best_slot;
    if (distance_sq) *distance_sq = best_dist;
    return 1;
}

int gm_mobs_fishing_target_position(
        const GmMobLive *m, int slot, int dimension,
        double *x, double *y, double *z) {
    const EwStore *s;
    float width, height;
    if (!m || slot <= 0 || slot >= EW_MAX_ENTITIES)
        return 0;
    s = const_store(m);
    if (!s->alive[slot] || m->entity_dimension[slot] != dimension
            || !gm_living(s->type[slot]))
        return 0;
    ehs_size_scaled(s->type[slot], m->size[slot], &width, &height);
    (void)width;
    if (x) *x = s->x[slot];
    if (y) *y = s->y[slot] + (double)height * 0.8;
    if (z) *z = s->z[slot];
    return 1;
}

int gm_mobs_fishing_reel(
        GmMobLive *m, int slot, int dimension,
        double angler_x, double angler_y, double angler_z) {
    EwStore *s;
    double dx, dy, dz, distance;
    if (!m || slot <= 0 || slot >= EW_MAX_ENTITIES)
        return 0;
    s = now_store(m);
    if (!s->alive[slot] || m->entity_dimension[slot] != dimension
            || !gm_living(s->type[slot]))
        return 0;
    dx = angler_x - s->x[slot];
    dy = angler_y - s->y[slot];
    dz = angler_z - s->z[slot];
    distance = sqrt(dx * dx + dy * dy + dz * dz);
    s->vx[slot] += dx * 0.1;
    s->vy[slot] += dy * 0.1 + sqrt(distance) * 0.08;
    s->vz[slot] += dz * 0.1;
    return 1;
}

static int small_fireball_immune(int type) {
    return type == EW_TYPE_BLAZE || type == EW_TYPE_GHAST
        || type == EW_TYPE_MAGMA || type == EW_TYPE_PIGMAN
        || type == EW_TYPE_WITHER_SKELETON;
}

int gm_mobs_small_fireball_hit(
        GmMobLive *m, int slot, float damage, GmLiveSim *drops) {
    if (!m || slot <= 0 || slot >= EW_MAX_ENTITIES || damage <= 0.0f)
        return 0;
    EwStore *s = now_store(m);
    if (!s->alive[slot] || m->entity_dead[slot]
            || s->health[slot] <= 0.0F || !gm_living(s->type[slot]))
        return 0;
    if (small_fireball_immune(s->type[slot])) return 1;
    if (s->type[slot] == EW_TYPE_BOAT) {
        m->boat_damage[slot] += damage * 10.0F;
        if (m->boat_damage[slot] > 40.0F) mob_drop(m, s, slot, drops);
        ew_store_copy(next_store(m), s);
        return 1;
    }
    float applied;
    int accepted = 1;
    if (m->entity_hurt_resistant[slot] > 10) {
        if (damage <= m->entity_last_damage[slot]) {
            accepted = 0;
            applied = 0.0f;
        } else {
            applied = damage - m->entity_last_damage[slot];
            m->entity_last_damage[slot] = damage;
        }
    } else {
        applied = damage;
        m->entity_last_damage[slot] = damage;
        m->entity_hurt_resistant[slot] = 20;
        m->entity_hurt_time[slot] = 10;
    }
    if (accepted) {
        applied = mob_resistance_damage(m, slot, applied);
        applied = mob_absorb_damage(m, slot, applied);
        s->health[slot] -= applied;
        mark_hurt(m, s, slot);
        if (m->fire_ticks[slot] < 5 * 20)
            m->fire_ticks[slot] = 5 * 20;
        if (s->health[slot] <= 0.0f) mob_drop(m, s, slot, drops);
    }
    ew_store_copy(next_store(m), s);
    return 1;
}

int gm_mobs_damage_near(GmMobLive *m,double x,double y,double z,double radius,
                        float damage,GmLiveSim *drops){
    if(!m)return 0;
    EwStore *s=now_store(m);int best=-1;double bd=radius*radius;
    for(int i=1;i<EW_MAX_ENTITIES;++i)
        if(s->alive[i] && !m->entity_dead[i] && s->health[i] > 0.0F
                && m->entity_dimension[i]==m->active_dimension&&gm_living(s->type[i])&&
                                         s->type[i]!=EW_TYPE_BOAT){
        double dx=s->x[i]-x,dy=(s->y[i]+0.9)-y,dz=s->z[i]-z,d=dx*dx+dy*dy+dz*dz;
        if(d<=bd){bd=d;best=i;}
    }
    if(best<0)return 0;
    {
        float applied = mob_resistance_damage(m, best, damage);
        applied = mob_absorb_damage(m, best, applied);
        s->health[best] -= applied;
    }
    mark_hurt(m,s,best);
    if(s->health[best]<=0)mob_drop(m,s,best,drops);
    ew_store_copy(next_store(m),s);return 1;
}

int gm_mobs_take_explosion(GmMobLive *m,double *x,double *y,double *z){
    if(!m||!m->explosion_pending)return 0;
    if(x)*x=m->explosion_x;
    if(y)*y=m->explosion_y;
    if(z)*z=m->explosion_z;
    m->explosion_pending=0;return 1;
}

int gm_mobs_take_fireball(GmMobLive *m,double *x,double *y,double *z,
                          double *vx,double *vy,double *vz){
    /* Returns pending kind: 0=none, 3=small fireball, 5=large fireball. */
    if(!m||!m->fireball_pending)return 0;
    int kind=m->fireball_pending;
    if(x)*x=m->fireball_x;if(y)*y=m->fireball_y;if(z)*z=m->fireball_z;
    if(vx)*vx=m->fireball_vx;if(vy)*vy=m->fireball_vy;if(vz)*vz=m->fireball_vz;
    m->fireball_pending=0;return kind;
}
