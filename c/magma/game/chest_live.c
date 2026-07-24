/* game/chest_live.c - see chest_live.h. */
#include "game/chest_live.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "stronghold_loot.h"
#pragma GCC diagnostic pop

#include <string.h>

void chest_live_init(ChestLive *c)
{
    if (!c) return;
    tec_init(&c->te);
    c->loot_table = CHEST_LOOT_NONE;
    c->loot_seed = 0;
    c->loot_filled = 1; /* no pending loot */
}

void chest_live_set_loot(ChestLive *c, int table, long long seed)
{
    if (!c) return;
    c->loot_table = table;
    c->loot_seed = seed;
    c->loot_filled = (table < 0) ? 1 : 0;
}

void chest_live_ensure_loot(ChestLive *c)
{
    if (!c || c->loot_filled) return;
    if (c->loot_table >= 0)
        shl_fill_chest(&c->te, c->loot_table, (i64)c->loot_seed);
    c->loot_filled = 1;
}

ICStack chest_live_get(const ChestLive *c, int slot)
{
    if (!c) return ic_empty();
    /* const-cast ensure: callers that need loot must call ensure first.
     * get on unfilled chest returns empty (vanilla fills on openInventory). */
    TecStack t = tec_get_stack(&c->te, slot);
    if (tec_is_empty(&t)) return ic_empty();
    return ic_mk(t.item, t.count, t.meta);
}

void chest_live_set(ChestLive *c, int slot, ICStack stack)
{
    if (!c) return;
    chest_live_ensure_loot(c);
    if (stack.item <= 0 || stack.count <= 0)
        tec_set_slot(&c->te, slot, tec_empty());
    else
        tec_set_slot(&c->te, slot, tec_mk(stack.item, stack.count, stack.meta));
}

ICStack chest_live_extract(ChestLive *c, int slot, int amount)
{
    if (!c || amount <= 0) return ic_empty();
    chest_live_ensure_loot(c);
    TecStack got = tec_get_and_split(&c->te, slot, amount);
    if (tec_is_empty(&got)) return ic_empty();
    return ic_mk(got.item, got.count, got.meta);
}

int chest_live_insert(ChestLive *c, int slot, ICStack stack)
{
    if (!c || stack.item <= 0 || stack.count <= 0) return 0;
    chest_live_ensure_loot(c);
    if (slot < 0 || slot >= CHEST_LIVE_SLOTS) return 0;
    TecStack cur = tec_get_stack(&c->te, slot);
    if (tec_is_empty(&cur)) {
        i32 n = stack.count;
        if (n > TEC_STACK_LIMIT) n = TEC_STACK_LIMIT;
        tec_set_slot(&c->te, slot, tec_mk(stack.item, n, stack.meta));
        return (int)n;
    }
    if (cur.item != stack.item || cur.meta != stack.meta) return 0;
    i32 room = TEC_STACK_LIMIT - cur.count;
    if (room <= 0) return 0;
    i32 n = stack.count < room ? stack.count : room;
    cur.count += n;
    tec_set_slot(&c->te, slot, cur);
    return (int)n;
}

void chest_live_open(ChestLive *c)
{
    if (!c) return;
    chest_live_ensure_loot(c);
    tec_open(&c->te);
}

void chest_live_close(ChestLive *c)
{
    if (!c) return;
    tec_close(&c->te);
}

void chest_live_tick(ChestLive *c)
{
    if (!c) return;
    tec_tick(&c->te);
}

int chest_live_total_items(const ChestLive *c)
{
    if (!c) return 0;
    return tec_total_items(&c->te);
}
