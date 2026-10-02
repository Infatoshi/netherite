/* The enchantment half of melee and projectile hits, ported from
 * oracle/src/enchantment/EnchantmentHelper.java (getEnchantmentModifierLiving,
 * getKnockbackModifier, getFireAspectModifier, func_151384_a, func_151385_b),
 * EnchantmentDamage (func_152376_a, func_151368_a), EnchantmentThorns
 * (func_151367_b), EnchantmentProtection.getFireTimeForEntity and
 * EntityMob.attackEntityAsMob.
 *
 * A living's items are EntityLivingBase.getHeldItem and getLastActiveItems:
 * a mob's are its five equipment slots (the held item is slot 0 and also
 * among the last active items), the player twin's are the server player's
 * current hotbar stack and its four armour slots. */
#ifndef NETHERITE_COMBATENCH_H
#define NETHERITE_COMBATENCH_H

#include "det.h"

struct living;

/* EnumCreatureAttribute. */
enum { CREATURE_UNDEFINED = 0, CREATURE_UNDEAD, CREATURE_ARTHROPOD };
int living_creature_attribute(const struct living *l);

/* EnchantmentHelper.getEnchantmentModifierLiving(attacker, target). */
float ench_modifier_living(const struct living *attacker, const struct living *target);
/* EnchantmentHelper.getKnockbackModifier / getFireAspectModifier. */
int ench_knockback(const struct living *attacker);
int ench_fire_aspect(const struct living *attacker);
/* EntityLivingBase.onDeath's var4: EnchantmentHelper.getLootingModifier of
 * the source's entity when it is a player, else 0. */
int ench_looting(const struct living *killer);

/* EnchantmentHelper.func_151384_a(user, attacker): every enchantment on the
 * user's last active items (and its held item when the attacker is a player)
 * reacts to the hit; thorns is the one that does anything. */
void ench_hurt_iter(struct living *user, struct living *attacker, det_state *det);
/* EnchantmentHelper.func_151385_b(user, target): the same over the attacker's
 * items (and its held item when it is a player); bane of arthropods slows an
 * arthropod target. */
void ench_damage_iter(struct living *user, struct living *target, det_state *det);

/* ItemStack.damageItem(amount, l) on a mob's equipment slot (0 the hand,
 * 1..4 boots to helmet): Unbreaking, the break's effects and draws. */
void ench_damage_equipment(struct living *l, int slot, int amount, det_state *det);

/* EnchantmentHelper.getMaxEnchantmentLevel(id, getLastActiveItems()): the
 * highest level of enchantment id on the living's last active items. */
int ench_max_level(const struct living *l, int id);
/* EntityLivingBase.renderBrokenItemStack on a mob (the break sound's
 * World.rand pitch and the particles' draws). */
void living_render_broken_item_stack(struct living *l, det_state *det);
/* EntityLivingBase.decreaseAirSupply (and EntityIronGolem's no-op). */
int living_decrease_air_supply(struct living *l, int air);

/* EnchantmentProtection.getFireTimeForEntity: fire protection on the last
 * active items shortens a fire of `ticks`. */
int ench_fire_time(const struct living *l, int ticks);

/* EnchantmentProtection.func_92092_a: blast protection on the last active
 * items shortens an explosion's push v (NULL: an entity with no items). */
double ench_blast_protection(const struct living *l, double v);

/* The entity whose `fire` is the living's Entity.fire: the server player's
 * for the player twin, else its own. */
struct entity *living_fire_entity(struct living *l);

/* EntityMob.attackEntityAsMob: attackDamage plus the held item's
 * enchantment damage against the target's creature attribute, the mob
 * source, then knockback, fire aspect, thorns and func_151385_b on a landed
 * hit. The kinds' own tails (the zombie's fire, the cave spider's poison,
 * the wither skeleton's wither) run on its answer. */
int mob_attack_entity_as_mob(struct living *l, struct living *target, det_state *det);

#endif
