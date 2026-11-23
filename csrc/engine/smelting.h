#ifndef NETHERITE_SMELTING_H
#define NETHERITE_SMELTING_H

#include "crafting.h"

/* FurnaceRecipes.smelting().getSmeltingResult: walk smeltingList in the order
 * the oracle holds it (recipes.h) and take the first entry whose input stack is
 * the same item with a damage that is either the wildcard 32767 or the queried
 * damage. 1 and out set on a hit, 0 when nothing matches (Java's null). */
int smelt_result(int item, int damage, struct craft_stack *out);

/* FurnaceRecipes.smelting().getSmeltingExperience: Java walks experienceList
 * and compares the queried stack against each entry's key, which is a *result*
 * stack, not an input. So an ordinary ore finds nothing and returns 0.0F, and
 * only an item that is also a result (coal damage 1, say) returns that result's
 * experience. SMELT_EXPERIENCE holds those entries in the map's own order. */
float smelt_experience(int item, int damage);

#endif