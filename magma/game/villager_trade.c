/* game/villager_trade.c - see villager_trade.h. */
#include "game/villager_trade.h"

#include <string.h>

enum {
    VT_EMERALD = 388,
    VT_WHEAT = 296,
    VT_POTATO = 392,
    VT_CARROT = 391,
    VT_BREAD = 297,
    VT_STRING = 287,
    VT_COAL = 263,
    VT_FISH = 349,
    VT_COOKED_FISH = 350,
    VT_WOOL = 35,
    VT_SHEARS = 359,
    VT_ARROW = 262,
    VT_PAPER = 339,
    VT_ROTTEN_FLESH = 367,
    VT_GOLD_INGOT = 266,
    VT_IRON_HELMET = 306,
    VT_IRON_AXE = 258,
    VT_PORKCHOP = 319,
    VT_CHICKEN = 365,
    VT_LEATHER = 334,
    VT_LEATHER_LEGGINGS = 300
};

static int price(JavaRandom *random, int lo, int hi)
{
    return lo >= hi ? lo : lo + jrand_int_bound(random, hi - lo + 1);
}

static void add_offer(GmVillagerTrade *trade, ICStack a, ICStack b, ICStack sell)
{
    if (trade->offer_count >= VT_MAX_OFFERS) return;
    GmVillagerOffer *offer = &trade->offers[trade->offer_count++];
    offer->buy_a = a;
    offer->buy_b = b;
    offer->sell = sell;
    offer->uses = 0;
    offer->max_uses = 7;
    offer->rewards_exp = 1;
}

static void emerald_for_items(GmVillagerTrade *trade, JavaRandom *random,
                              int item, int meta, int lo, int hi)
{
    add_offer(trade, ic_mk(item, price(random, lo, hi), meta), ic_empty(),
              ic_mk(VT_EMERALD, 1, 0));
}

static void item_for_emeralds(GmVillagerTrade *trade, JavaRandom *random,
                              int item, int meta, int lo, int hi)
{
    int value = price(random, lo, hi);
    if (value < 0)
        add_offer(trade, ic_mk(VT_EMERALD, 1, 0), ic_empty(),
                  ic_mk(item, -value, meta));
    else
        add_offer(trade, ic_mk(VT_EMERALD, value, 0), ic_empty(),
                  ic_mk(item, 1, meta));
}

static void item_and_emerald(GmVillagerTrade *trade, JavaRandom *random,
                             int input, int input_meta, int input_lo, int input_hi,
                             int output, int output_meta, int output_lo, int output_hi)
{
    int input_count = price(random, input_lo, input_hi);
    int output_count = price(random, output_lo, output_hi);
    add_offer(trade, ic_mk(input, input_count, input_meta),
              ic_mk(VT_EMERALD, 1, 0),
              ic_mk(output, output_count, output_meta));
}

void gm_villager_trade_init(GmVillagerTrade *trade, int profession,
                            JavaRandom *random)
{
    static const unsigned char careers[6] = {4, 2, 1, 3, 2, 1};
    if (!trade) return;
    memset(trade, 0, sizeof *trade);
    trade->profession = profession;
    if (!random || profession < 0 || profession > 5) return;
    trade->career = jrand_int_bound(random, careers[profession]) + 1;
    trade->career_level = 1;
    trade->initialized = 1;

    if (profession == 0 && trade->career == 1) {
        emerald_for_items(trade, random, VT_WHEAT, 0, 18, 22);
        emerald_for_items(trade, random, VT_POTATO, 0, 15, 19);
        emerald_for_items(trade, random, VT_CARROT, 0, 15, 19);
        item_for_emeralds(trade, random, VT_BREAD, 0, -4, -2);
    } else if (profession == 0 && trade->career == 2) {
        emerald_for_items(trade, random, VT_STRING, 0, 15, 20);
        emerald_for_items(trade, random, VT_COAL, 0, 16, 24);
        item_and_emerald(trade, random, VT_FISH, 0, 6, 6,
                         VT_COOKED_FISH, 0, 6, 6);
    } else if (profession == 0 && trade->career == 3) {
        emerald_for_items(trade, random, VT_WOOL, 0, 16, 22);
        item_for_emeralds(trade, random, VT_SHEARS, 0, 3, 4);
    } else if (profession == 0 && trade->career == 4) {
        emerald_for_items(trade, random, VT_STRING, 0, 15, 20);
        item_for_emeralds(trade, random, VT_ARROW, 0, -12, -8);
    } else if (profession == 1) {
        /* Librarian's second initial recipe is an enchanted book.  It is
         * deliberately absent; cartographer's first tier contains only paper. */
        emerald_for_items(trade, random, VT_PAPER, 0, 24, 36);
    } else if (profession == 2) {
        emerald_for_items(trade, random, VT_ROTTEN_FLESH, 0, 36, 40);
        emerald_for_items(trade, random, VT_GOLD_INGOT, 0, 8, 10);
    } else if (profession == 3 && trade->career == 1) {
        emerald_for_items(trade, random, VT_COAL, 0, 16, 24);
        item_for_emeralds(trade, random, VT_IRON_HELMET, 0, 4, 6);
    } else if (profession == 3 && trade->career == 2) {
        emerald_for_items(trade, random, VT_COAL, 0, 16, 24);
        item_for_emeralds(trade, random, VT_IRON_AXE, 0, 6, 8);
    } else if (profession == 3 && trade->career == 3) {
        /* The initial enchanted iron shovel is held for the full enchanted
         * ItemStack payload slice; the ordinary coal recipe is still exact. */
        emerald_for_items(trade, random, VT_COAL, 0, 16, 24);
    } else if (profession == 4 && trade->career == 1) {
        emerald_for_items(trade, random, VT_PORKCHOP, 0, 14, 18);
        emerald_for_items(trade, random, VT_CHICKEN, 0, 14, 18);
    } else if (profession == 4 && trade->career == 2) {
        emerald_for_items(trade, random, VT_LEATHER, 0, 9, 12);
        item_for_emeralds(trade, random, VT_LEATHER_LEGGINGS, 0, 2, 4);
    }
}

const GmVillagerOffer *gm_villager_trade_offer(
    const GmVillagerTrade *trade, int index)
{
    if (!trade || index < 0 || index >= trade->offer_count) return NULL;
    return &trade->offers[index];
}

static int recipe_stack_matches(const ICStack *actual, const ICStack *required)
{
    if (!actual || !required || actual->item != required->item
            || actual->meta != required->meta)
        return 0;
    /* MerchantRecipeList requires the recipe's NBT, but accepts extra input
     * NBT when the recipe has none. */
    if (required->n_enchants > 0
            && !ic_enchants_equal(actual, required))
        return 0;
    return actual->count >= required->count;
}

static int offer_matches(const GmVillagerOffer *offer,
                         const ICStack *first, const ICStack *second)
{
    int needs_second = offer->buy_b.item != 0 && offer->buy_b.count > 0;
    int has_second = second && second->item != 0 && second->count > 0;
    return offer->uses < offer->max_uses
        && recipe_stack_matches(first, &offer->buy_a)
        && ((!needs_second && !has_second)
            || (needs_second && has_second
                && recipe_stack_matches(second, &offer->buy_b)));
}

int gm_villager_trade_find(const GmVillagerTrade *trade,
                           const ICStack *first, const ICStack *second,
                           int requested_index)
{
    if (!trade || !trade->initialized || !first || !second) return -1;
    if (requested_index > 0 && requested_index < trade->offer_count)
        return offer_matches(&trade->offers[requested_index], first, second)
            ? requested_index : -1;
    for (int i = 0; i < trade->offer_count; ++i)
        if (offer_matches(&trade->offers[i], first, second)) return i;
    return -1;
}

static void shrink(ICStack *stack, int count)
{
    stack->count -= count;
    if (stack->count <= 0) *stack = ic_empty();
}

static int consume(const GmVillagerOffer *offer, ICStack *first, ICStack *second)
{
    if (!offer_matches(offer, first, second)) return 0;
    shrink(first, offer->buy_a.count);
    if (offer->buy_b.count > 0) shrink(second, offer->buy_b.count);
    return 1;
}

int gm_villager_trade_execute(GmVillagerTrade *trade, int offer_index,
                              ICStack *first, ICStack *second,
                              ICStack *output)
{
    GmVillagerOffer *offer;
    if (!trade || offer_index < 0 || offer_index >= trade->offer_count
            || !first || !second || !output)
        return 0;
    offer = &trade->offers[offer_index];
    if (!consume(offer, first, second)
            && !consume(offer, second, first))
        return 0;
    *output = offer->sell;
    ++offer->uses;
    if (offer->buy_a.item == VT_EMERALD)
        trade->wealth += offer->buy_a.count;
    return 1;
}

int gm_villager_trade_use(GmVillagerTrade *trade, int offer_index,
                          JavaRandom *random, float *sound_pitch,
                          int *xp_value)
{
    int xp, reset;
    float pitch;
    if (!trade || !random || offer_index < 0
            || offer_index >= trade->offer_count
            || trade->offers[offer_index].uses <= 0)
        return 0;
    pitch = (jrand_float(random) - jrand_float(random)) * 0.2F + 1.0F;
    xp = 3 + jrand_int_bound(random, 4);
    reset = trade->offers[offer_index].uses == 1
        || jrand_int_bound(random, 5) == 0;
    if (reset) {
        trade->time_until_reset = 40;
        trade->needs_initialization = 1;
        trade->willing_to_mate = 1;
        xp += 5;
    }
    if (sound_pitch) *sound_pitch = pitch;
    if (xp_value) *xp_value = xp;
    return reset ? 2 : 1;
}
