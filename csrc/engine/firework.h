/* ItemFireworkCharge's client half and the tooltip lines it shares with
 * ItemFirework: getColorFromItemStack's overlay colour (render pass 1) and
 * func_150902_a's description of one Explosion compound, as the en_US
 * language file words it. The stacks' tags are the item tag store's
 * (itemtag.h). */
#ifndef NETHERITE_FIREWORK_H
#define NETHERITE_FIREWORK_H

#include <stddef.h>

/* ItemDye.field_150922_c[damage], -1 outside 0..15 (crafting.c). */
int craft_dye_firework_color(int damage);

/* ItemFireworkCharge.getColorFromItemStack(stack, 1): the Explosion's one
 * colour, the channel means of several, 9079434 without an int array. */
int firework_charge_color(int tag);

/* The lines addInformation adds under the name, each into lines + i * stride
 * (at most max): ItemFireworkCharge's for a charge (id 402), ItemFirework's
 * (the flight, then each explosion's lines, the later ones indented) for a
 * rocket (401); 0 for anything else or no tag. */
int firework_info_lines(int id, int tag, char *lines, size_t stride, int max);

#endif
