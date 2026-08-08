/* game/villager_trade.c - see villager_trade.h. */
#include "game/villager_trade.h"
#include "enchant_table.h"

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
    VT_LEATHER_LEGGINGS = 300,
    VT_BOOK = 340,
    VT_ENCHANTED_BOOK = 403,
    VT_COMPASS = 345,
    VT_FILLED_MAP = 358
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

static ICStack enchanted_stack(
        int item, const EtData *enchants, int count) {
    ICStack stack = ic_mk(item, 1, 0);
    if (count > IC_MAX_ENCHANTS) count = IC_MAX_ENCHANTS;
    stack.n_enchants = count;
    for (int i = 0; i < count; ++i) {
        stack.enchants[i].id = (i16)enchants[i].id;
        stack.enchants[i].level = (i16)enchants[i].level;
    }
    return stack;
}

static void enchanted_book_for_emeralds(
        GmVillagerTrade *trade, JavaRandom *random) {
    int n_defs = 0;
    const EtDef *defs = et_defs(&n_defs);
    const EtDef *enchantment;
    int level, emeralds;
    EtData selected;
    if (n_defs <= 0) return;
    enchantment = &defs[jrand_int_bound(random, n_defs)];
    level = price(random, 1, enchantment->max_level);
    emeralds = 2 + jrand_int_bound(random, 5 + level * 10) + 3 * level;
    if (enchantment->treasure) emeralds *= 2;
    if (emeralds > 64) emeralds = 64;
    selected = (EtData){enchantment->id, level, enchantment->weight};
    add_offer(trade, ic_mk(VT_BOOK, 1, 0),
              ic_mk(VT_EMERALD, emeralds, 0),
              enchanted_stack(VT_ENCHANTED_BOOK, &selected, 1));
}

static void enchanted_item_for_emeralds(
        GmVillagerTrade *trade, JavaRandom *random,
        int item, int lo, int hi) {
    EtData enchants[ET_MAX_LIST];
    int item_kind = et_item_kind_from_id(item);
    int emeralds = price(random, lo, hi);
    int level = 5 + jrand_int_bound(random, 15);
    int count = item_kind < 0 ? 0 : et_build_list(
        random, item_kind, level, 0, enchants, ET_MAX_LIST);
    add_offer(trade, ic_mk(VT_EMERALD, emeralds, 0), ic_empty(),
              enchanted_stack(item, enchants, count));
}

static int career_levels(int profession, int career) {
    static const unsigned char levels[6][4] = {
        {4, 2, 2, 2}, {6, 4, 0, 0}, {4, 0, 0, 0},
        {4, 3, 3, 0}, {2, 3, 0, 0}, {0, 0, 0, 0}
    };
    if (profession < 0 || profession > 5 || career < 1 || career > 4)
        return 0;
    return levels[profession][career - 1];
}

int gm_villager_trade_add_level(
        GmVillagerTrade *trade, JavaRandom *random) {
    int p, c, level, before;
    if (!trade || !random || !trade->initialized) return 0;
    p = trade->profession;
    c = trade->career;
    if (trade->career_level >= career_levels(p, c)) return 0;
    level = ++trade->career_level;
    before = trade->offer_count;

    if (p == 0 && c == 1) {
        if (level == 1) {
            emerald_for_items(trade, random, 296, 0, 18, 22);
            emerald_for_items(trade, random, 392, 0, 15, 19);
            emerald_for_items(trade, random, 391, 0, 15, 19);
            item_for_emeralds(trade, random, 297, 0, -4, -2);
        } else if (level == 2) {
            emerald_for_items(trade, random, 86, 0, 8, 13);
            item_for_emeralds(trade, random, 400, 0, -3, -2);
        } else if (level == 3) {
            emerald_for_items(trade, random, 103, 0, 7, 12);
            item_for_emeralds(trade, random, 260, 0, -7, -5);
        } else {
            item_for_emeralds(trade, random, 357, 0, -10, -6);
            item_for_emeralds(trade, random, 354, 0, 1, 1);
        }
    } else if (p == 0 && c == 2) {
        if (level == 1) {
            emerald_for_items(trade, random, 287, 0, 15, 20);
            emerald_for_items(trade, random, 263, 0, 16, 24);
            item_and_emerald(trade, random, 349, 0, 6, 6, 350, 0, 6, 6);
        } else {
            enchanted_item_for_emeralds(trade, random, 346, 7, 8);
        }
    } else if (p == 0 && c == 3) {
        if (level == 1) {
            emerald_for_items(trade, random, 35, 0, 16, 22);
            item_for_emeralds(trade, random, 359, 0, 3, 4);
        } else {
            for (int meta = 0; meta < 16; ++meta)
                item_for_emeralds(trade, random, 35, meta, 1, 2);
        }
    } else if (p == 0 && c == 4) {
        if (level == 1) {
            emerald_for_items(trade, random, 287, 0, 15, 20);
            item_for_emeralds(trade, random, 262, 0, -12, -8);
        } else {
            item_for_emeralds(trade, random, 261, 0, 2, 3);
            item_and_emerald(trade, random, 13, 0, 10, 10, 318, 0, 6, 10);
        }
    } else if (p == 1 && c == 1) {
        if (level == 1) {
            emerald_for_items(trade, random, 339, 0, 24, 36);
            enchanted_book_for_emeralds(trade, random);
        } else if (level == 2) {
            emerald_for_items(trade, random, 340, 0, 8, 10);
            item_for_emeralds(trade, random, 345, 0, 10, 12);
            item_for_emeralds(trade, random, 47, 0, 3, 4);
        } else if (level == 3) {
            emerald_for_items(trade, random, 387, 0, 2, 2);
            item_for_emeralds(trade, random, 347, 0, 10, 12);
            item_for_emeralds(trade, random, 20, 0, -5, -3);
        } else if (level == 6) {
            item_for_emeralds(trade, random, 421, 0, 20, 22);
        } else {
            enchanted_book_for_emeralds(trade, random);
        }
    } else if (p == 1 && c == 2) {
        if (level == 1)
            emerald_for_items(trade, random, 339, 0, 24, 36);
        else if (level == 2)
            emerald_for_items(trade, random, 345, 0, 1, 1);
        else if (level == 3)
            item_for_emeralds(trade, random, 395, 0, 7, 11);
        else {
            /* TreasureMapForEmeralds rolls before its world search. Preserve
             * the exact cursor here and let the runtime resolve structures. */
            trade->monument_map_price = price(random, 12, 20);
            trade->mansion_map_price = price(random, 16, 28);
            trade->explorer_maps_pending = 1;
        }
    } else if (p == 2 && c == 1) {
        if (level == 1) {
            emerald_for_items(trade, random, 367, 0, 36, 40);
            emerald_for_items(trade, random, 266, 0, 8, 10);
        } else if (level == 2) {
            item_for_emeralds(trade, random, 331, 0, -4, -1);
            item_for_emeralds(trade, random, 351, 4, -2, -1);
        } else if (level == 3) {
            item_for_emeralds(trade, random, 368, 0, 4, 7);
            item_for_emeralds(trade, random, 89, 0, -3, -1);
        } else {
            item_for_emeralds(trade, random, 384, 0, 3, 11);
        }
    } else if (p == 3 && c == 1) {
        if (level == 1) {
            emerald_for_items(trade, random, 263, 0, 16, 24);
            item_for_emeralds(trade, random, 306, 0, 4, 6);
        } else if (level == 2) {
            emerald_for_items(trade, random, 265, 0, 7, 9);
            item_for_emeralds(trade, random, 307, 0, 10, 14);
        } else if (level == 3) {
            emerald_for_items(trade, random, 264, 0, 3, 4);
            enchanted_item_for_emeralds(trade, random, 311, 16, 19);
        } else {
            item_for_emeralds(trade, random, 305, 0, 5, 7);
            item_for_emeralds(trade, random, 304, 0, 9, 11);
            item_for_emeralds(trade, random, 302, 0, 5, 7);
            item_for_emeralds(trade, random, 303, 0, 11, 15);
        }
    } else if (p == 3 && c == 2) {
        if (level == 1) {
            emerald_for_items(trade, random, 263, 0, 16, 24);
            item_for_emeralds(trade, random, 258, 0, 6, 8);
        } else if (level == 2) {
            emerald_for_items(trade, random, 265, 0, 7, 9);
            enchanted_item_for_emeralds(trade, random, 267, 9, 10);
        } else {
            emerald_for_items(trade, random, 264, 0, 3, 4);
            enchanted_item_for_emeralds(trade, random, 276, 12, 15);
            enchanted_item_for_emeralds(trade, random, 279, 9, 12);
        }
    } else if (p == 3 && c == 3) {
        if (level == 1) {
            emerald_for_items(trade, random, 263, 0, 16, 24);
            enchanted_item_for_emeralds(trade, random, 256, 5, 7);
        } else if (level == 2) {
            emerald_for_items(trade, random, 265, 0, 7, 9);
            enchanted_item_for_emeralds(trade, random, 257, 9, 11);
        } else {
            emerald_for_items(trade, random, 264, 0, 3, 4);
            enchanted_item_for_emeralds(trade, random, 278, 12, 15);
        }
    } else if (p == 4 && c == 1) {
        if (level == 1) {
            emerald_for_items(trade, random, 319, 0, 14, 18);
            emerald_for_items(trade, random, 365, 0, 14, 18);
        } else {
            emerald_for_items(trade, random, 263, 0, 16, 24);
            item_for_emeralds(trade, random, 320, 0, -7, -5);
            item_for_emeralds(trade, random, 366, 0, -8, -6);
        }
    } else if (p == 4 && c == 2) {
        if (level == 1) {
            emerald_for_items(trade, random, 334, 0, 9, 12);
            item_for_emeralds(trade, random, 300, 0, 2, 4);
        } else if (level == 2) {
            enchanted_item_for_emeralds(trade, random, 299, 7, 12);
        } else {
            item_for_emeralds(trade, random, 329, 0, 8, 10);
        }
    }
    return trade->offer_count - before;
}

int gm_villager_trade_add_explorer_offer(
        GmVillagerTrade *trade, int emerald_price, int map_id, int tag_id,
        int custom_name_id) {
    ICStack map;
    int before;
    if (!trade || !trade->initialized || emerald_price < 1
            || emerald_price > 64 || map_id < 0 || map_id > 32767
            || tag_id < 0 || custom_name_id < 0)
        return 0;
    before = trade->offer_count;
    map = ic_mk(VT_FILLED_MAP, 1, map_id);
    map.tag_id = tag_id;
    map.custom_name = custom_name_id;
    add_offer(trade, ic_mk(VT_EMERALD, emerald_price, 0),
              ic_mk(VT_COMPASS, 1, 0), map);
    return trade->offer_count == before + 1;
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
    trade->initialized = 1;
    (void)gm_villager_trade_add_level(trade, random);
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

int gm_villager_trade_tick(GmVillagerTrade *trade, JavaRandom *random)
{
    if (!trade || !random || !trade->initialized
            || trade->time_until_reset <= 0)
        return 0;
    if (--trade->time_until_reset > 0) return 0;
    if (trade->needs_initialization) {
        for (int i = 0; i < trade->offer_count; ++i) {
            GmVillagerOffer *offer = &trade->offers[i];
            if (offer->uses >= offer->max_uses)
                offer->max_uses += jrand_int_bound(random, 6)
                    + jrand_int_bound(random, 6) + 2;
        }
        (void)gm_villager_trade_add_level(trade, random);
        trade->needs_initialization = 0;
    }
    return 1;
}
