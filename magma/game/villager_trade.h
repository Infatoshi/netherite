/* game/villager_trade.h - bounded 1.11.2 villager merchant state.
 *
 * PORT: EntityVillager.populateBuyingList, PriceInfo, EmeraldForItems,
 * ListItemForEmeralds, ItemAndEmeraldToItem; MerchantRecipe/List matching;
 * SlotMerchantResult.doTrade.  The initial ordinary offers are exact for all
 * vanilla careers.  Enchanted-book/item and treasure-map recipes are left out
 * until their complete NBT/map payload can be retained by ICStack. */
#ifndef MAGMA_GAME_VILLAGER_TRADE_H
#define MAGMA_GAME_VILLAGER_TRADE_H

#include "items_core.h"
#include "mc_rng.h"

enum { VT_MAX_OFFERS = 8 };

typedef struct {
    ICStack buy_a;
    ICStack buy_b;
    ICStack sell;
    int uses;
    int max_uses;
    int rewards_exp;
} GmVillagerOffer;

typedef struct {
    int initialized;
    int profession;             /* 0..5 */
    int career;                 /* vanilla one-based Career id */
    int career_level;           /* vanilla one-based CareerLevel */
    int offer_count;
    int selected;
    int wealth;
    int time_until_reset;
    int needs_initialization;
    int willing_to_mate;
    GmVillagerOffer offers[VT_MAX_OFFERS];
} GmVillagerTrade;

void gm_villager_trade_init(GmVillagerTrade *trade, int profession,
                            JavaRandom *random);
const GmVillagerOffer *gm_villager_trade_offer(
    const GmVillagerTrade *trade, int index);

/* MerchantRecipeList.canRecipeBeUsed.  Explicit index 0 has vanilla's odd
 * fallback behavior and therefore scans the complete list; only indices > 0
 * select a single recipe. */
int gm_villager_trade_find(const GmVillagerTrade *trade,
                           const ICStack *first, const ICStack *second,
                           int requested_index);

/* SlotMerchantResult.doTrade + MerchantRecipe.incrementToolUses.  Inputs may
 * be reversed, as InventoryMerchant does after its first match fails. */
int gm_villager_trade_execute(GmVillagerTrade *trade, int offer_index,
                              ICStack *first, ICStack *second,
                              ICStack *output);

/* EntityVillager.useRecipe tail after the result slot increments uses. */
int gm_villager_trade_use(GmVillagerTrade *trade, int offer_index,
                          JavaRandom *random, float *sound_pitch,
                          int *xp_value);

#endif
