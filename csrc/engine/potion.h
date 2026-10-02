#ifndef NETHERITE_POTION_H
#define NETHERITE_POTION_H

#include <stdint.h>
#include <stddef.h>
#include "det.h"
#include "jrand.h"

struct living;

struct ie_world;
struct ie_ent;
struct aabb;

/* Vanilla 1.7.10 Potion IDs */
enum {
    POT_MOVE_SPEED = 1,
    POT_MOVE_SLOWDOWN = 2,
    POT_DIG_SPEED = 3,
    POT_DIG_SLOWDOWN = 4,
    POT_DAMAGE_BOOST = 5,
    POT_HEAL = 6,
    POT_HARM = 7,
    POT_JUMP = 8,
    POT_CONFUSION = 9,
    POT_REGENERATION = 10,
    POT_RESISTANCE = 11,
    POT_FIRE_RESISTANCE = 12,
    POT_WATER_BREATHING = 13,
    POT_INVISIBILITY = 14,
    POT_BLINDNESS = 15,
    POT_NIGHT_VISION = 16,
    POT_HUNGER = 17,
    POT_WEAKNESS = 18,
    POT_POISON = 19,
    POT_WITHER = 20,
    POT_HEALTH_BOOST = 21,
    POT_ABSORPTION = 22,
    POT_SATURATION = 23,
    POT_COUNT = 32
};

struct potion_effect {
    uint8_t id;
    int8_t amplifier;
    int duration;
    uint8_t is_splash;
    uint8_t is_ambient;
    /* PotionEffect.isPotionDurationMax: the client sets it from an S1D whose
     * duration is 32767; InventoryEffectRenderer draws it as **:** */
    uint8_t duration_max;
};

/* EntityLivingBase.activePotionsMap, a JDK 8 HashMap<Integer, PotionEffect>
 * (GPU audit L4: order as data, no chains). Integer.hashCode is the id, so
 * the bin is id & (capacity - 1): the table starts at 16 bins and doubles to
 * 32 when a 13th key goes in (it never shrinks), after which every id has a
 * bin of its own. At 16, bin b holds ids b and b + 16 in insertion order. So
 * the effects are held by id, with the insertion stamp of each key, and the
 * iteration order is computed from the ids, the stamps and the capacity
 * (potion_map_order). A value copy is a plain struct copy. */
struct potion_map {
    int size;
    int capacity;           /* 16 or 32 */
    uint32_t held;          /* bit id: the key is in the map */
    uint32_t stamp;         /* the next put's insertion stamp */
    uint32_t seq[POT_COUNT];            /* by id: the key's insertion stamp */
    struct potion_effect eff[POT_COUNT];   /* by id */
};

void potion_map_init(struct potion_map *m);
void potion_map_clear(struct potion_map *m);
struct potion_effect *potion_map_get(struct potion_map *m, int id);
int potion_map_put(struct potion_map *m, const struct potion_effect *eff); /* returns 1 if new, 0 if updated existing */
int potion_map_remove(struct potion_map *m, int id, struct potion_effect *removed);
/* HashMap's iteration order: the held ids into ids (up to POT_COUNT), bins
 * ascending, insertion order inside a bin. Returns the count. */
int potion_map_order(const struct potion_map *m, uint8_t *ids);

/* Potion static metadata */
int potion_is_bad(int id);
int potion_liquid_color(int id);
int potion_is_instant(int id);
double potion_effectiveness(int id);
int potion_is_ready(int id, int duration, int amplifier);

/* Effects application */
void potion_perform_effect(struct living *l, int id, int amplifier, det_state *det);
void potion_affect_entity(struct living *target, int id, int amplifier, double distance_falloff,
                          struct living *source, det_state *det);

/* Living entity hooks */
void living_heal(struct living *l, float amount);
void living_add_potion_effect(struct living *l, const struct potion_effect *eff, det_state *det);
void living_remove_potion_effect(struct living *l, int id, det_state *det);
void living_clear_active_potions(struct living *l, det_state *det);
int living_is_potion_active(const struct living *l, int id);
const struct potion_effect *living_get_potion_effect(const struct living *l, int id);
void living_update_potion_effects(struct living *l, det_state *det);

/* Attribute modifier helpers */
void potion_apply_attributes(struct living *l, int id, int amplifier);
void potion_remove_attributes(struct living *l, int id, int amplifier);

/* PotionHelper / ItemPotion: damage to effects */
int potion_get_effects_for_damage(int damage, struct potion_effect *out, int max_out);
int potion_calc_liquid_color(const struct potion_map *m);
int potion_all_ambient(const struct potion_map *m);

/* ItemFood.onFoodEaten (and ItemAppleGold's, ItemFishFood's): the finished
 * food's effects on the eater, with World.rand's probability draw (NULL: no
 * draw, every effect applies). Returns the addPotionEffect calls. */
int potion_apply_food(struct living *l, int item_id, int item_damage, det_state *det, jrand *world_rand);

#endif
