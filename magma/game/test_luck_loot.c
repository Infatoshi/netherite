/* Direct 1.11.2 LootContext Luck oracle. The synthetic pool is intentionally
 * discriminating: quality changes entry weights and bonus_rolls changes the
 * number of outputs. Built-in non-fishing 1.11.2 tables use neither field. */
#include "stronghold_loot.h"

#include <stdio.h>
#include <string.h>

static void emit(int value)
{
    printf("%08x\n", (unsigned)value);
}

int main(void)
{
    static const float lucks[] = {
        -2.0f, -0.5f, 0.0f, 0.5f, 1.0f, 2.0f, 3.5f, 4.0f
    };
    ShlTable table;
    ShlPool *pool;
    memset(&table, 0, sizeof table);
    table.n_pools = 1;
    pool = &table.pools[0];
    pool->rolls.min = 1.0f;
    pool->rolls.max = 1.0f;
    pool->bonus_rolls.min = 0.25f;
    pool->bonus_rolls.max = 1.25f;
    pool->n_entries = 2;
    pool->entries[0].item = 264;
    pool->entries[0].weight = 1;
    pool->entries[0].quality = 2;
    pool->entries[1].item = 280;
    pool->entries[1].weight = 10;
    pool->entries[1].quality = -1;

    /* LootTableList has 81 rows; gameplay/fishing is the sole built-in table
     * whose entries have nonzero quality, leaving 80 invariant tables. */
    emit(80);
    for (int index = 0; index < 64; ++index) {
        JavaRandom random;
        LtContext context;
        LtStack result[SHL_MAX_STACKS];
        TecStack slots[TEC_SLOTS];
        int count;
        jrand_set(&random, 0x4c55434bLL + (i64)index * 10007LL);
        context.luck = lucks[index & 7];
        context.has_killer = 0;
        context.looting_level = 0;
        count = shl_generate(
            &table, &random, &context, result, SHL_MAX_STACKS);
        emit(index);
        emit(count);
        for (int item = 0; item < 8; ++item)
            emit(item < count ? result[item].item : 0);
        for (int slot = 0; slot < TEC_SLOTS; ++slot)
            slots[slot] = tec_empty();
        shl_fill_inventory_table(
            slots, TEC_SLOTS, &table,
            0x4c55434bLL + (i64)index * 10007LL, &context);
        for (int slot = 0; slot < TEC_SLOTS; ++slot) {
            emit(tec_is_empty(&slots[slot]) ? 0 : slots[slot].item);
            emit(tec_is_empty(&slots[slot]) ? 0 : slots[slot].count);
        }
    }
    return 0;
}
