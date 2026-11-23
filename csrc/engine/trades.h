/* EntityVillager.addDefaultEquipmentAndRecipies and MerchantRecipeList, the
 * trades every village villager builds (and rebuilds when a trade of its last
 * recipe unlocks the next tier). */
#ifndef NETHERITE_TRADES_H
#define NETHERITE_TRADES_H

#include "itemtag.h"
#include <stdint.h>

#include "jrand.h"

/* One ItemStack of a trade. `tag` is the stack's tag compound in the item tag
 * store (an enchanted tool's "ench" list, an enchanted book's
 * "StoredEnchantments"), 0 for none. */
struct trade_stack {
    short item;
    unsigned char count;
    short damage;
    int tag;
};

struct trade_recipe {
    struct trade_stack buy, buy_b, sell;
    int has_buy_b;
    int uses, max_uses;
};

/* MerchantRecipeList: the trades grow a few per unlock, a full list stays
 * under a dozen; the fresh list one addDefault call builds runs the whole
 * profession table, up to 26 rows for the blacksmith. */
#define TRADES_MAX 32

struct trade_list {
    struct trade_recipe r[TRADES_MAX];
    int n;
};

/* One row of the two static price tables. */
struct trades_price_row { short item; short min, max; };

/* villagerStockList / blacksmithSellingList, for a check against the probe's
 * manifest. */
const struct trades_price_row *trades_villager_stock(int *n);
const struct trades_price_row *trades_blacksmith_stock(int *n);

/* addDefaultEquipmentAndRecipies(count): the full draw sequence of the
 * profession, the shuffle, then the first `count` recipes of the fresh list
 * through addToListWithCheck into `list`. The adjust probability reads
 * list->n the way field_82191_bN does. `shuf` is Collections.shuffle(List)'s
 * shared static Random (it is not the villager's own Random); its draws happen
 * only when the fresh list holds at least two recipes. */
void trades_add_default(jrand *rand, jrand *shuf, int profession, int count, struct trade_list *list);

/* updateAITick's unlock block: when the list holds more than one recipe, every
 * disabled recipe gains maxUses += nextInt(6) + nextInt(6) + 2, in list
 * order. */
void trades_unlock_boost(jrand *rand, struct trade_list *list);

/* MerchantRecipe.func_82785_h: a fully used recipe. */
void trades_disable(struct trade_recipe *r);

/* MerchantRecipe.hasSameIDsAs. */
int trades_same_ids(const struct trade_recipe *a, const struct trade_recipe *b);

/* MerchantRecipe.isRecipeDisabled. */
int trades_is_disabled(const struct trade_recipe *r);

/* MerchantRecipeList.canRecipeBeUsed: the recipe index the two stacks (an
 * empty one is item < 0) can buy at `index`, or -1 (index outside [1, size)
 * scans the whole list). */
int trades_can_be_used(const struct trade_list *list, int item1, int count1,
                       int item2, int count2, int index);

struct nbt;
struct nbt *trades_nbt(const struct trade_list *list);
/* The same compound's fields (the Recipes list) through a writer (nbtw.h). */
struct nbtw;
void trades_w(struct nbtw *w, const struct trade_list *list);


/* getRecipiesAsTags() as canonical NBT text, malloc'd. */
/* MerchantRecipeList(NBTTagCompound): the Recipes list's compounds, buy/buyB/
 * sell stacks and the uses/maxUses ints as MerchantRecipe.readFromTags reads
 * them (maxUses 7 when the tag lacks it). */
void trades_from_nbt(struct trade_list *list, const struct nbt *offers);
char *trades_render(const struct trade_list *list);

void trades_free(struct trade_list *list);

#endif