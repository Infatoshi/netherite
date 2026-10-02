/* WeightedRandomChestContent and every structure loot table, ported from
 * net.minecraft.util.WeightedRandomChestContent and the tables the structures
 * hold. One row per entry: the item id from items.h, its damage, the minimum
 * and maximum count, and the weight. The enchanted book entries are not in these
 * tables: each structure appends one through func_92080_a at generation time,
 * with an ItemStack whose StoredEnchantments come from the structure's own RNG,
 * so the caller appends it (loot_table_with_book) the same way.
 *
 * The count each chest draws and the order the draws happen in belong to the
 * call site, not this table: see loot_count_kind and loot_generate. */
#ifndef NETHERITE_LOOT_H
#define NETHERITE_LOOT_H

#include "itemtag.h"
#include <stdint.h>

#include "enchant.h"
#include "jrand.h"
#include "nbtjson.h"

/* One WeightedRandomChestContent. */
struct loot_entry {
    short item;      /* item id from items.h */
    short damage;
    unsigned char min_count, max_count;
    unsigned char weight;
    /* The book entry a structure appends through func_92080_a, if any: the
     * enchantability addRandomEnchantment runs at (-1 for a plain entry). */
    signed char book_enchantability;
    /* that book entry's func_92112_a arguments (1,1,1 for func_92114_b) */
    unsigned char book_min, book_max, book_weight;
};

/* One named table. A table with a book entry has one extra row at the end
 * (book_enchantability >= 0) built the way its structure builds it. */
struct loot_table {
    const char *name;
    const struct loot_entry *entries;
    int n;
};

/* The count argument each structure's call site passes. The count is drawn from
 * the structure's Random before generateChestContents runs, so the call site
 * order is: draw the count, then generate. FIXED_* take no draw. */
enum loot_count_kind {
    LOOT_COUNT_8, LOOT_COUNT_10, LOOT_COUNT_2, LOOT_COUNT_3_4, LOOT_COUNT_2_2, LOOT_COUNT_1_4,
    LOOT_COUNT_2_5, LOOT_COUNT_3_6, LOOT_COUNT_2_4
};

/* Draws the count the structure's call site draws, moving the RNG exactly as
 * the structure does. */
int loot_count(jrand *rand, enum loot_count_kind kind);

/* The named table, or NULL. */
const struct loot_table *loot_table_by_name(const char *name);

/* Every table, NULL-terminated, in a fixed order (one line each in the probe's
 * draws.jsonl). */
extern const struct loot_table *const loot_tables[];

/* One slot of the inventory the contents land in. */
struct loot_stack {
    int item;
    int damage;
    int count;
    int tag;         /* the stack's tag in the item tag store, 0 for none */
};

/* The enchanted book stack a structure appends to its table: the structure
 * builds it once, from its own RNG, while generating (ItemEnchantedBook
 * .func_92114_b/.func_92112_a -> EnchantmentHelper.addRandomEnchantment over an
 * Items.book stack), and generateChestContents later copies that fixed stack
 * into whatever slots the entry lands in. `tag` is the canonical NBT of that
 * stack's tag compound, which is what each slot carries. */
struct loot_book {
    int tag;
};

/* Builds the book stack for `entry` (its last row, book_enchantability >= 0)
 * out of `rand`, the structure's generation RNG, exactly as
 * func_92114_b/func_92112_a does. A table whose last row is not a book leaves
 * `out->tag` NULL. */
void loot_book_build(jrand *rand, const struct loot_entry *entry, struct loot_book *out);

/* Releases the stack. */
void loot_book_free(struct loot_book *b);

/* WeightedRandomChestContent.generateChestContents: `count` draws from `table`
 * into `contents`, the inventory's `slots` slots. A slot is {item,damage,count}
 * plus `tag`, owned canonical NBT text of the stack's tag compound or NULL
 * (the book's StoredEnchantments). `book` is the table's appended book stack
 * when it has one (loot_book_build), NULL otherwise; the chest loop copies it
 * without moving `rand`. */
void loot_generate_contents(jrand *rand, const struct loot_table *table,
                            const struct loot_book *book,
                            struct loot_stack *contents, int slots, int count);

/* The same over a dispenser's 9 slots (WeightedRandomChestContent.func_150706_a). */
void loot_generate_dispenser(jrand *rand, const struct loot_table *table,
                             const struct loot_book *book,
                             struct loot_stack *contents, int slots, int count);

/* Releases every tag a generated slot owns. */
void loot_free(struct loot_stack *contents, int slots);

#endif