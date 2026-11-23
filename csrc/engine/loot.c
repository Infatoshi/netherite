#include "loot.h"
#include "itemtag.h"

#include <stdlib.h>
#include <string.h>

#include "enchant.h"
#include "items.h"
#include "jrand.h"
#include "loot_tables.h"
#include "nbtjson.h"

/* Every table, in the order the probe writes them, NULL-terminated. */
const struct loot_table *const loot_tables[] = { LOOT_TABLES_ALL };

#define NTABLES ((int)(sizeof LOOT_TABLES / sizeof LOOT_TABLES[0]))

const struct loot_table *loot_table_by_name(const char *name)
{
    for (int i = 0; i < NTABLES; ++i)
        if (!strcmp(LOOT_TABLES[i].name, name)) return &LOOT_TABLES[i];
    return NULL;
}

int loot_count(jrand *rand, enum loot_count_kind kind)
{
    switch (kind)
    {
        case LOOT_COUNT_8: return 8;
        case LOOT_COUNT_10: return 10;
        case LOOT_COUNT_2: return 2;
        case LOOT_COUNT_3_4: return 3 + jr_int_n(rand, 4);
        case LOOT_COUNT_2_2: return 2 + jr_int_n(rand, 2);
        case LOOT_COUNT_1_4: return 1 + jr_int_n(rand, 4);
        case LOOT_COUNT_2_5: return 2 + jr_int_n(rand, 5);
        case LOOT_COUNT_3_6: return 3 + jr_int_n(rand, 6);
        case LOOT_COUNT_2_4: return 2 + jr_int_n(rand, 4);
    }
    return 0;
}

/* WeightedRandom.getTotalWeight over the entries. */
static int total_weight(const struct loot_entry *e, int n)
{
    int t = 0;
    for (int i = 0; i < n; ++i) t += e[i].weight;
    return t;
}

/* WeightedRandom.getRandomItem over the array: draw nextInt(total), then walk
 * the array subtracting each weight until the running value goes negative. */
static int pick_entry(jrand *rand, const struct loot_entry *e, int n)
{
    int k = jr_int_n(rand, total_weight(e, n));
    for (int i = 0; i < n; ++i)
    {
        k -= e[i].weight;
        if (k < 0) return i;
    }
    return -1;
}

/* The maximum stack size of an entry's item: Item.getItemStackLimit, which is
 * Item.maxStackSize (items.h). */
static int max_stack_size(int item)
{
    return ITEMS[item].max_stack_size;
}

/* One ItemStack.copy() landing in a random slot. `tag` is the stack's tag in
 * the item tag store, 0 for none: every slot the entry lands in carries an
 * equal compound. */

static void put_entry(jrand *rand, struct loot_stack *contents, int slots,
                      const struct loot_entry *e, int size, int tag)
{
    if (max_stack_size(e->item) >= size)
    {
        int slot = jr_int_n(rand, slots);
        contents[slot].item = e->item;
        contents[slot].damage = e->damage;
        contents[slot].count = size;
        contents[slot].tag = tag;
    }
    else
    {
        for (int i = 0; i < size; ++i)
        {
            int slot = jr_int_n(rand, slots);
            contents[slot].item = e->item;
            contents[slot].damage = e->damage;
            contents[slot].count = 1;
            contents[slot].tag = tag;
        }
    }
}

/* WeightedRandomChestContent.generateChestContents. */
void loot_generate_contents(jrand *rand, const struct loot_table *table,
                            const struct loot_book *book,
                            struct loot_stack *contents, int slots, int count)
{
    for (int pass = 0; pass < count; ++pass)
    {
        int idx = pick_entry(rand, table->entries, table->n);
        if (idx < 0) break;

        const struct loot_entry *e = &table->entries[idx];

        /* the count draw comes before the stack is built */
        int size = e->min_count + jr_int_n(rand, e->max_count - e->min_count + 1);

        /* A book row's stack was enchanted when the structure built its table,
         * from the structure's own RNG, not from the chest's: `book` carries
         * that stack (loot_book_build) and the chest RNG does not move for it. */
        int tag = e->book_enchantability >= 0 && book ? book->tag : 0;
        put_entry(rand, contents, slots, e, size, tag);
    }
}

/* WeightedRandomChestContent.func_150706_a over a dispenser's slots. */
void loot_generate_dispenser(jrand *rand, const struct loot_table *table,
                             const struct loot_book *book,
                             struct loot_stack *contents, int slots, int count)
{
    loot_generate_contents(rand, table, book, contents, slots, count);
}

void loot_book_build(jrand *rand, const struct loot_entry *entry, struct loot_book *out)
{
    out->tag = 0;
    if (!entry || entry->book_enchantability < 0) return;
    char *text = add_random_enchantment_tag(rand, entry->book_enchantability);
    out->tag = itag_from_text(text);
    free(text);
}

void loot_book_free(struct loot_book *b)
{
    b->tag = 0;
}

void loot_free(struct loot_stack *contents, int slots)
{
    for (int i = 0; i < slots; ++i)
    {
        contents[i].tag = 0;
    }
}