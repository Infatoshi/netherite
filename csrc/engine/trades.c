/* EntityVillager.addDefaultEquipmentAndRecipies and the MerchantRecipeList it
 * fills, bit for bit: the two static price tables, the per-profession draw
 * sequences, the librarian's enchanted book (one enchantment picked from
 * Enchantment.enchantmentsBookList, which is enchantmentsList's non-null
 * entries in id order, i.e. ENCHANTS here), the priest's enchanted tools
 * (buildEnchantmentList through EnchantmentHelper.addRandomEnchantment), the
 * Collections.shuffle, and addToListWithCheck's rule that replaces an existing
 * recipe of the same items only when the new one is strictly cheaper. */
#include "nbtw.h"
#include "trades.h"
#include "itemtag.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "enchant.h"
#include "enchants.h"
#include "nbtjson.h"

#define NENCHANTED_BOOKS ((int)(sizeof ENCHANTS / sizeof ENCHANTS[0]))
#define NVILLAGER_STOCK ((int)(sizeof VILLAGER_STOCK / sizeof VILLAGER_STOCK[0]))
#define NBLACKSMITH_STOCK ((int)(sizeof BLACKSMITH_STOCK / sizeof BLACKSMITH_STOCK[0]))

/* villagerStockList: what the villager buys for emeralds. */
static const struct trades_price_row VILLAGER_STOCK[] = {
    {263, 16, 24},  /* coal */
    {265, 8, 10},   /* iron_ingot */
    {266, 8, 10},   /* gold_ingot */
    {264, 4, 6},    /* diamond */
    {339, 24, 36},  /* paper */
    {340, 11, 13},  /* book */
    {387, 1, 1},    /* written_book */
    {368, 3, 4},    /* ender_pearl */
    {381, 2, 3},    /* ender_eye */
    {319, 14, 18},  /* porkchop */
    {363, 14, 18},  /* beef */
    {365, 14, 18},  /* chicken */
    {350, 9, 13},   /* cooked_fished */
    {295, 34, 48},  /* wheat_seeds */
    {362, 30, 38},  /* melon_seeds */
    {361, 30, 38},  /* pumpkin_seeds */
    {296, 18, 22},  /* wheat */
    {35, 14, 22},   /* wool */
    {367, 36, 64},  /* rotten_flesh */
};

/* blacksmithSellingList: what the villager sells for emeralds; a negative
 * price means the trade pays 1 emerald for that many of the item. */
static const struct trades_price_row BLACKSMITH_STOCK[] = {
    {259, 3, 4},    /* flint_and_steel */
    {359, 3, 4},    /* shears */
    {267, 7, 11},   /* iron_sword */
    {276, 12, 14},  /* diamond_sword */
    {258, 6, 8},    /* iron_axe */
    {279, 9, 12},   /* diamond_axe */
    {257, 7, 9},    /* iron_pickaxe */
    {278, 10, 12},  /* diamond_pickaxe */
    {256, 4, 6},    /* iron_shovel */
    {277, 7, 8},    /* diamond_shovel */
    {292, 4, 6},    /* iron_hoe */
    {293, 7, 8},    /* diamond_hoe */
    {309, 4, 6},    /* iron_boots */
    {313, 7, 8},    /* diamond_boots */
    {306, 4, 6},    /* iron_helmet */
    {310, 7, 8},    /* diamond_helmet */
    {307, 10, 14},  /* iron_chestplate */
    {311, 16, 19},  /* diamond_chestplate */
    {308, 8, 10},   /* iron_leggings */
    {312, 11, 14},  /* diamond_leggings */
    {305, 5, 7},    /* chainmail_boots */
    {302, 5, 7},    /* chainmail_helmet */
    {303, 11, 15},  /* chainmail_chestplate */
    {304, 9, 11},   /* chainmail_leggings */
    {297, -4, -2},  /* bread */
    {360, -8, -4},  /* melon */
    {260, -8, -4},  /* apple */
    {357, -10, -7}, /* cookie */
    {20, -5, -3},   /* glass */
    {47, 3, 4},     /* bookshelf */
    {299, 4, 5},    /* leather_chestplate */
    {301, 2, 4},    /* leather_boots */
    {298, 2, 4},    /* leather_helmet */
    {300, 2, 4},    /* leather_leggings */
    {329, 6, 8},    /* saddle */
    {384, -4, -1},  /* experience_bottle */
    {331, -4, -1},  /* redstone */
    {345, 10, 12},  /* compass */
    {347, 10, 12},  /* clock */
    {89, -3, -1},   /* glowstone */
    {320, -7, -5},  /* cooked_porkchop */
    {364, -7, -5},  /* cooked_beef */
    {366, -8, -6},  /* cooked_chicken */
    {381, 7, 11},   /* ender_eye */
    {262, -12, -8}, /* arrow */
};

static const short EMERALD = 388;

static int price_of(const struct trades_price_row *table, int n, int item, jrand *rand)
{
    for (int i = 0; i < n; ++i)
    {
        if (table[i].item == item)
        {
            if (table[i].min >= table[i].max) return table[i].min;
            return table[i].min + jr_int_n(rand, table[i].max - table[i].min);
        }
    }

    return 1;   /* func_146092_b / func_146090_c on an unknown item */
}

const struct trades_price_row *trades_villager_stock(int *n)
{
    *n = NVILLAGER_STOCK;
    return VILLAGER_STOCK;
}

const struct trades_price_row *trades_blacksmith_stock(int *n)
{
    *n = NBLACKSMITH_STOCK;
    return BLACKSMITH_STOCK;
}

/* ---- the per-profession draw sequences ---- */

enum op_kind { OP_BUY, OP_SELL, OP_GRAVEL, OP_BOOK, OP_TOOLS };

struct trade_op { unsigned char kind; short item; float prob; };

static const struct trade_op PROF_OPS[5][26] = {
    /* farmer */
    {{OP_BUY, 296, 0.9f}, {OP_BUY, 35, 0.5f}, {OP_BUY, 365, 0.5f}, {OP_BUY, 350, 0.4f},
     {OP_SELL, 297, 0.9f}, {OP_SELL, 360, 0.3f}, {OP_SELL, 260, 0.3f}, {OP_SELL, 357, 0.3f},
     {OP_SELL, 359, 0.3f}, {OP_SELL, 259, 0.3f}, {OP_SELL, 366, 0.3f}, {OP_SELL, 262, 0.5f},
     {OP_GRAVEL, 0, 0.5f}},
    /* librarian */
    {{OP_BUY, 339, 0.8f}, {OP_BUY, 340, 0.8f}, {OP_BUY, 387, 0.3f},
     {OP_SELL, 47, 0.8f}, {OP_SELL, 20, 0.2f}, {OP_SELL, 345, 0.2f}, {OP_SELL, 347, 0.2f},
     {OP_BOOK, 0, 0.07f}},
    /* priest */
    {{OP_SELL, 381, 0.3f}, {OP_SELL, 384, 0.2f}, {OP_SELL, 331, 0.4f}, {OP_SELL, 89, 0.3f},
     {OP_TOOLS, 267, 0.05f}, {OP_TOOLS, 276, 0.05f}, {OP_TOOLS, 307, 0.05f}, {OP_TOOLS, 311, 0.05f},
     {OP_TOOLS, 258, 0.05f}, {OP_TOOLS, 279, 0.05f}, {OP_TOOLS, 257, 0.05f}, {OP_TOOLS, 278, 0.05f}},
    /* blacksmith */
    {{OP_BUY, 263, 0.7f}, {OP_BUY, 265, 0.5f}, {OP_BUY, 266, 0.5f}, {OP_BUY, 264, 0.5f},
     {OP_SELL, 267, 0.5f}, {OP_SELL, 276, 0.5f}, {OP_SELL, 258, 0.3f}, {OP_SELL, 279, 0.3f},
     {OP_SELL, 257, 0.5f}, {OP_SELL, 278, 0.5f}, {OP_SELL, 256, 0.2f}, {OP_SELL, 277, 0.2f},
     {OP_SELL, 292, 0.2f}, {OP_SELL, 293, 0.2f}, {OP_SELL, 309, 0.2f}, {OP_SELL, 313, 0.2f},
     {OP_SELL, 306, 0.2f}, {OP_SELL, 310, 0.2f}, {OP_SELL, 307, 0.2f}, {OP_SELL, 311, 0.2f},
     {OP_SELL, 308, 0.2f}, {OP_SELL, 312, 0.2f}, {OP_SELL, 305, 0.1f}, {OP_SELL, 302, 0.1f},
     {OP_SELL, 303, 0.1f}, {OP_SELL, 304, 0.1f}},
    /* butcher */
    {{OP_BUY, 263, 0.7f}, {OP_BUY, 319, 0.5f}, {OP_BUY, 363, 0.5f},
     {OP_SELL, 329, 0.1f}, {OP_SELL, 299, 0.3f}, {OP_SELL, 301, 0.3f},
     {OP_SELL, 298, 0.3f}, {OP_SELL, 300, 0.3f}, {OP_SELL, 320, 0.3f}, {OP_SELL, 364, 0.3f}},
};

static const int PROF_NOPS[5] = {13, 8, 12, 26, 10};

static const short GRAVEL = 13, FLINT = 318, ENCHANTED_BOOK = 403;

/* ---- recipe helpers ---- */

static void stack_free(struct trade_stack *s)
{
    s->tag = 0;
}

static void stack_set(struct trade_stack *s, short item, int count, short damage)
{
    s->item = item;
    s->count = (unsigned char)count;
    s->damage = damage;
    s->tag = 0;
}

void trades_disable(struct trade_recipe *r)
{
    r->uses = r->max_uses;
}

int trades_is_disabled(const struct trade_recipe *r)
{
    return r->uses >= r->max_uses;
}

void trades_free(struct trade_list *list)
{
    for (int i = 0; i < list->n; ++i)
    {
        stack_free(&list->r[i].buy);
        stack_free(&list->r[i].buy_b);
        stack_free(&list->r[i].sell);
    }

    list->n = 0;
}

/* MerchantRecipe.hasSameIDsAs */
int trades_same_ids(const struct trade_recipe *a, const struct trade_recipe *b)
{
    if (a->buy.item != b->buy.item || a->sell.item != b->sell.item) return 0;
    if (a->has_buy_b != b->has_buy_b) return 0;
    return !a->has_buy_b || a->buy_b.item == b->buy_b.item;
}

/* MerchantRecipe.hasSameItemsAs: same ids and a strictly cheaper price. */
static int same_items(const struct trade_recipe *a, const struct trade_recipe *b)
{
    if (!trades_same_ids(a, b)) return 0;

    if (a->buy.count < b->buy.count) return 1;
    return a->has_buy_b && a->buy_b.count < b->buy_b.count;
}

/* MerchantRecipeList.canRecipeBeUsed: the recipe index the two stacks (an
 * empty one is item < 0) can buy at `index`, or -1 (index outside [1, size)
 * scans the whole list). */
int trades_can_be_used(const struct trade_list *list, int item1, int count1,
                       int item2, int count2, int index)
{
    if (index > 0 && index < list->n)
    {
        const struct trade_recipe *r = &list->r[index];

        if (item1 == r->buy.item && count1 >= r->buy.count &&
            ((!r->has_buy_b && item2 < 0) ||
             (r->has_buy_b && item2 >= 0 && r->buy_b.item == item2 && count2 >= r->buy_b.count)))
            return index;

        return -1;
    }

    for (int i = 0; i < list->n; ++i)
    {
        const struct trade_recipe *r = &list->r[i];

        if (item1 == r->buy.item && count1 >= r->buy.count &&
            ((!r->has_buy_b && item2 < 0) ||
             (r->has_buy_b && item2 >= 0 && r->buy_b.item == item2 && count2 >= r->buy_b.count)))
            return i;
    }

    return -1;
}

/* MerchantRecipeList.addToListWithCheck */
static void add_with_check(struct trade_list *list, struct trade_recipe *r)
{
    for (int i = 0; i < list->n; ++i)
    {
        if (trades_same_ids(r, &list->r[i]))
        {
            if (same_items(r, &list->r[i]))
            {
                stack_free(&list->r[i].buy);
                stack_free(&list->r[i].buy_b);
                stack_free(&list->r[i].sell);
                list->r[i] = *r;
            }
            else
            {
                stack_free(&r->buy);
                stack_free(&r->buy_b);
                stack_free(&r->sell);
            }

            return;
        }
    }

    list->r[list->n++] = *r;
}

void trades_unlock_boost(jrand *rand, struct trade_list *list)
{
    if (list->n > 1)
    {
        for (int i = 0; i < list->n; ++i)
        {
            if (trades_is_disabled(&list->r[i]))
            {
                int a = jr_int_n(rand, 6);
                int b = jr_int_n(rand, 6);
                list->r[i].max_uses += a + b + 2;
            }
        }
    }
}

/* adjustProbability: field_82191_bN is sqrt_float(buyingList.size()) * 0.2F at
 * the start of the call, so a grown list pays for its size with a lower
 * chance per trade. */
static float adjust_probability(float p, float field)
{
    float v = p + field;
    return v > 0.9f ? 0.9f - (v - 0.9f) : v;
}

/* func_146091_a: the villager buys `item` and pays one emerald. */
static void op_buy(struct trade_list *out, int item, jrand *rand, float prob)
{
    if (jr_float(rand) >= prob) return;

    struct trade_recipe r;
    memset(&r, 0, sizeof r);
    r.max_uses = 7;
    stack_set(&r.buy, (short)item, price_of(VILLAGER_STOCK, NVILLAGER_STOCK, item, rand), 0);
    stack_set(&r.sell, EMERALD, 1, 0);
    out->r[out->n++] = r;
}

/* func_146089_b: the villager sells `item` for `price` emeralds; a negative
 * price trades that many of the item for one emerald. */
static void op_sell(struct trade_list *out, int item, jrand *rand, float prob)
{
    if (jr_float(rand) >= prob) return;

    int price = price_of(BLACKSMITH_STOCK, NBLACKSMITH_STOCK, item, rand);
    struct trade_recipe r;
    memset(&r, 0, sizeof r);
    r.max_uses = 7;

    if (price < 0)
    {
        stack_set(&r.buy, EMERALD, 1, 0);
        stack_set(&r.sell, (short)item, -price, 0);
    }
    else
    {
        stack_set(&r.buy, EMERALD, price, 0);
        stack_set(&r.sell, (short)item, 1, 0);
    }

    out->r[out->n++] = r;
}

/* The farmer's gravel trade: 10 gravel and an emerald for 4 or 5 flint. */
static void op_gravel(struct trade_list *out, jrand *rand, float prob)
{
    if (jr_float(rand) >= prob) return;

    struct trade_recipe r;
    memset(&r, 0, sizeof r);
    r.max_uses = 7;
    r.has_buy_b = 1;
    stack_set(&r.buy, GRAVEL, 10, 0);
    stack_set(&r.buy_b, EMERALD, 1, 0);
    stack_set(&r.sell, FLINT, 4 + jr_int_n(rand, 2), 0);
    out->r[out->n++] = r;
}

/* The librarian's enchanted book: one enchantment of
 * Enchantment.enchantmentsBookList at a level in its window, bought for a book
 * and a price that grows with the level. */
static void op_book(struct trade_list *out, jrand *rand, float prob)
{
    if (jr_float(rand) >= prob) return;

    const struct enchantment_def *e = &ENCHANTS[jr_int_n(rand, NENCHANTED_BOOKS)];
    int lvl = e->min_level >= e->max_level ? e->min_level
        : jr_int_n(rand, e->max_level - e->min_level + 1) + e->min_level;

    struct trade_recipe r;
    memset(&r, 0, sizeof r);
    r.max_uses = 7;
    r.has_buy_b = 1;
    stack_set(&r.buy, 340, 1, 0);
    stack_set(&r.buy_b, EMERALD, 2 + jr_int_n(rand, 5 + lvl * 10) + 3 * lvl, 0);
    stack_set(&r.sell, ENCHANTED_BOOK, 1, 0);

    nbt *stored = nbt_new_list();
    nbt *e1 = nbt_new_compound();
    nbt_put(e1, "id", nbt_new_short(e->effect_id));
    nbt_put(e1, "lvl", nbt_new_short(lvl));
    nbt_list_add(stored, e1);
    nbt *tag = nbt_new_compound();
    nbt_put(tag, "StoredEnchantments", stored);
    r.sell.tag = itag_from_tree(tag);
    nbt_free(tag);
    out->r[out->n++] = r;
}

/* The priest's enchanted tools: buildEnchantmentList through
 * EnchantmentHelper.addRandomEnchantment at the level budget 5 + nextInt(15). */
static void op_tools(struct trade_list *out, int item, jrand *rand, float prob)
{
    if (jr_float(rand) >= prob) return;

    int price = 2 + jr_int_n(rand, 3);
    int budget = 5 + jr_int_n(rand, 15);
    struct enchant_data list[16];
    int ne = build_enchantment_list(rand, item, budget, list, 16);

    struct trade_recipe r;
    memset(&r, 0, sizeof r);
    r.max_uses = 7;
    r.has_buy_b = 1;
    stack_set(&r.buy, (short)item, 1, 0);
    stack_set(&r.buy_b, EMERALD, price, 0);
    stack_set(&r.sell, (short)item, 1, 0);

    if (ne > 0)
    {
        nbt *ench = nbt_new_list();

        for (int i = 0; i < ne; ++i)
        {
            nbt *e1 = nbt_new_compound();
            /* ItemStack.addEnchantment stores the level through a byte */
            nbt_put(e1, "id", nbt_new_short(list[i].effect_id));
            nbt_put(e1, "lvl", nbt_new_short((short)(signed char)list[i].level));
            nbt_list_add(ench, e1);
        }

        nbt *tag = nbt_new_compound();
        nbt_put(tag, "ench", ench);
        r.sell.tag = itag_from_tree(tag);
        nbt_free(tag);
    }

    out->r[out->n++] = r;
}

void trades_add_default(jrand *rand, jrand *shuf, int profession, int count, struct trade_list *list)
{
    /* field_82191_bN at entry: the adjust probability grows with the list the
     * new trades go into. */
    float field = list->n > 0 ? (float)sqrt((double)(float)list->n) * 0.2f : 0.0f;

    struct trade_list fresh;
    memset(&fresh, 0, sizeof fresh);
    const struct trade_op *ops = PROF_OPS[profession];
    int nops = PROF_NOPS[profession];

    for (int i = 0; i < nops; ++i)
    {
        const struct trade_op *op = &ops[i];

        switch (op->kind)
        {
            case OP_BUY: op_buy(&fresh, op->item, rand, adjust_probability(op->prob, field)); break;
            case OP_SELL: op_sell(&fresh, op->item, rand, adjust_probability(op->prob, field)); break;
            case OP_GRAVEL: op_gravel(&fresh, rand, adjust_probability(op->prob, field)); break;
            case OP_BOOK: op_book(&fresh, rand, adjust_probability(op->prob, field)); break;
            case OP_TOOLS: op_tools(&fresh, op->item, rand, adjust_probability(op->prob, field)); break;
        }
    }

    if (fresh.n == 0)
    {
        /* the empty list's gold ingot, a func_146091_a at probability 1.0: the
         * float draw still happens */
        jr_float(rand);
        struct trade_recipe r;
        memset(&r, 0, sizeof r);
        r.max_uses = 7;
        stack_set(&r.buy, 266, price_of(VILLAGER_STOCK, NVILLAGER_STOCK, 266, rand), 0);
        stack_set(&r.sell, EMERALD, 1, 0);
        fresh.r[fresh.n++] = r;
    }

    /* Collections.shuffle(list): for i from size down to 2,
     * swap(i-1, rnd.nextInt(i)), on the shared static Random, which is not
     * the villager's own */
    if (fresh.n > 1)
    {
        for (int i = fresh.n; i > 1; --i)
        {
            int j = jr_int_n(shuf, i);
            struct trade_recipe t = fresh.r[i - 1];
            fresh.r[i - 1] = fresh.r[j];
            fresh.r[j] = t;
        }
    }

    for (int i = 0; i < count && i < fresh.n; ++i) add_with_check(list, &fresh.r[i]);
    for (int i = count < fresh.n ? count : fresh.n; i < fresh.n; ++i)
    {
        stack_free(&fresh.r[i].buy);
        stack_free(&fresh.r[i].buy_b);
        stack_free(&fresh.r[i].sell);
    }
}

/* ---- the list's NBT ---- */

static void stack_w(struct nbtw *w, const struct nbt_key *k, const struct trade_stack *s)
{
    nbtw_comp_k(w, k);
    nbtw_short(w, "id", s->item);
    nbtw_byte(w, "Count", s->count);
    nbtw_short(w, "Damage", s->damage);

    itag_w(w, s->tag);

    nbtw_end(w);
}

void trades_w(struct nbtw *w, const struct trade_list *list)
{
    nbtw_list(w, "Recipes");

    for (int i = 0; i < list->n; ++i)
    {
        const struct trade_recipe *r = &list->r[i];
        nbtw_add_comp(w);
        stack_w(w, NBTW_K("buy"), &r->buy);

        if (r->has_buy_b) stack_w(w, NBTW_K("buyB"), &r->buy_b);

        nbtw_int(w, "maxUses", r->max_uses);
        stack_w(w, NBTW_K("sell"), &r->sell);
        nbtw_int(w, "uses", r->uses);
        nbtw_end(w);
    }

    nbtw_end(w);
}

nbt *trades_nbt(const struct trade_list *list)
{
    nbt *root = nbt_new_compound();
    struct nbtw w;
    nbtw_tree(&w, root);
    trades_w(&w, list);
    return root;
}

static void stack_from_nbt(struct trade_stack *dst, const nbt *c)
{
    memset(dst, 0, sizeof *dst);
    if (c == NULL) { dst->item = -1; return; }
    dst->item = (short)nbt_int_value(nbt_get(c, "id"));
    dst->count = (unsigned char)nbt_int_value(nbt_get(c, "Count"));
    dst->damage = (short)nbt_int_value(nbt_get(c, "Damage"));
    dst->tag = itag_from_item(c);
}

void trades_from_nbt(struct trade_list *list, const nbt *offers)
{
    const nbt *recipes = offers ? nbt_get(offers, "Recipes") : NULL;
    int nn = recipes ? nbt_list_size(recipes) : 0;
    if (nn > TRADES_MAX) nn = TRADES_MAX;
    list->n = 0;
    for (int i = 0; i < nn; ++i)
    {
        const nbt *c = nbt_list_get(recipes, i);
        struct trade_recipe *r = &list->r[list->n++];
        memset(r, 0, sizeof *r);
        stack_from_nbt(&r->buy, nbt_get(c, "buy"));
        stack_from_nbt(&r->sell, nbt_get(c, "sell"));
        const nbt *bb = nbt_get(c, "buyB");
        if (bb) { r->has_buy_b = 1; stack_from_nbt(&r->buy_b, bb); }
        if (nbt_get(c, "uses") != NULL) r->uses = (int)nbt_int_value(nbt_get(c, "uses"));
        r->max_uses = nbt_get(c, "maxUses") != NULL ? (int)nbt_int_value(nbt_get(c, "maxUses")) : 7;
    }
}

char *trades_render(const struct trade_list *list)
{
    nbt *root = trades_nbt(list);
    char *s = nbt_render(root);
    nbt_free(root);
    return s;
}