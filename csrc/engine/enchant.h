/* EnchantmentHelper.addRandomEnchantment and buildEnchantmentList, the path a
 * chest's enchanted book entry takes. ItemEnchantedBook.func_92112_a makes a
 * book ItemStack and calls addRandomEnchantment(rand, stack, 30); with
 * ItemBook.getItemEnchantability() == 1 that is a fixed list of one to a few
 * enchantments, picked from the enchantments whose level window contains the
 * drawn enchantability level.
 *
 * The candidate set of mapEnchantmentData is a Java HashMap keyed by effectId,
 * and WeightedRandom.getRandomItem walks its values(); both the selection and
 * the order depend on the map's bucket layout, so the port rebuilds the same
 * table in java_hashmap.c and iterates it the same way (bucket index ascending,
 * insertion order inside a bucket). Each draw is therefore bit for bit the
 * oracle's.
 *
 * The picked enchantment is written into the stack's StoredEnchantments list
 * the way ItemEnchantedBook.addEnchantment does: a list of {id,lvl} compounds
 * in pick order, with a repeat of an id raising its level in place. */
#ifndef NETHERITE_ENCHANT_H
#define NETHERITE_ENCHANT_H

#include <stdint.h>

#include "jrand.h"
#include "nbtjson.h"

/* EnchantmentData as the port carries it: which enchantment and its level. */
struct enchant_data {
    int effect_id;
    int level;
};

/* buildEnchantmentList: the enchantments a stack of `item` gets from `rand`
 * at the level budget `level` (addRandomEnchantment's third argument; for the
 * chest-loot books that is the book entry's 30 or func_92112_a's drawn level).
 * Writes at most `max` picks into `out` and returns how many (0 when Java's
 * buildEnchantmentList returns null). `item` is an item id from items.h; the
 * item's own getItemEnchantability is resolved here (the tool material for
 * tools and swords, the armor material per item id, 1 for the book), which the
 * priest's enchanted tool trades use; for chest loot item 340 (book) keeps the
 * every-enchantment special case in mapEnchantmentData. */
int build_enchantment_list(jrand *rand, int item, int level,
                           struct enchant_data *out, int max);

/* The enchantability level a book gets: ItemBook.getItemEnchantability() == 1. */
#define BOOK_ENCHANTABILITY 1

/* add_random_enchantment: the fresh ItemStack a loot book entry carries, as
 * canonical NBT, exactly what ItemStack.writeToNBT writes for an enchanted
 * book: id 403, Count 1, Damage 0, tag.StoredEnchantments. malloc'd text. */
char *add_random_enchantment_book(jrand *rand, int enchantability);

/* The same stack's tag compound alone ("{\"StoredEnchantments\":[...]}"), which
 * is what a chest slot carries in its loot_stack.tag. malloc'd text. */
char *add_random_enchantment_tag(jrand *rand, int enchantability);

/* ItemStack.addEnchantment for each pick in order onto the stack's tag
 * (addRandomEnchantment on anything but a book): the new tag's index. */
int enchant_list_tag(int tag, const struct enchant_data *list, int n);

/* One enchantment's canApplyTogether(other), bit direction as the game calls it
 * (existing.canApplyTogether(candidate)). */
int can_apply_together(int effect_id, int other_effect_id);

#endif