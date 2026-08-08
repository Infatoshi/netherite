/* game/chest_live.c - see chest_live.h. */
#include "game/chest_live.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "stronghold_loot.h"
#pragma GCC diagnostic pop

#include <math.h>
#include <string.h>

static ICStack tec_to_ic(TecStack t)
{
    ICStack s = ic_mk(t.item, t.count, t.meta);
    int i, n = t.n_enchants;
    if (n > IC_MAX_ENCHANTS) n = IC_MAX_ENCHANTS;
    s.n_enchants = n;
    for (i = 0; i < n; ++i) {
        s.enchants[i].id = t.enchants[i].id;
        s.enchants[i].level = t.enchants[i].level;
    }
    return s;
}

static TecStack ic_to_tec(ICStack s)
{
    TecStack t = tec_mk(s.item, s.count, s.meta);
    int i, n = s.n_enchants;
    if (n > TEC_MAX_ENCHANTS) n = TEC_MAX_ENCHANTS;
    t.n_enchants = n;
    for (i = 0; i < n; ++i) {
        t.enchants[i].id = s.enchants[i].id;
        t.enchants[i].level = s.enchants[i].level;
    }
    return t;
}

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
    return tec_to_ic(t);
}

void chest_live_set(ChestLive *c, int slot, ICStack stack)
{
    if (!c) return;
    chest_live_ensure_loot(c);
    if (stack.item <= 0 || stack.count <= 0)
        tec_set_slot(&c->te, slot, tec_empty());
    else
        tec_set_slot(&c->te, slot, ic_to_tec(stack));
}

ICStack chest_live_extract(ChestLive *c, int slot, int amount)
{
    if (!c || amount <= 0) return ic_empty();
    chest_live_ensure_loot(c);
    TecStack got = tec_get_and_split(&c->te, slot, amount);
    if (tec_is_empty(&got)) return ic_empty();
    return tec_to_ic(got);
}

int chest_live_insert(ChestLive *c, int slot, ICStack stack)
{
    if (!c || stack.item <= 0 || stack.count <= 0) return 0;
    chest_live_ensure_loot(c);
    if (slot < 0 || slot >= CHEST_LIVE_SLOTS) return 0;
    TecStack cur = tec_get_stack(&c->te, slot);
    TecStack in = ic_to_tec(stack);
    i32 item_lim = tec_max_stack_size(stack.item);
    if (item_lim > TEC_STACK_LIMIT) item_lim = TEC_STACK_LIMIT;
    if (tec_is_empty(&cur)) {
        i32 n = stack.count;
        if (n > item_lim) n = item_lim;
        in.count = n;
        tec_set_slot(&c->te, slot, in);
        return (int)n;
    }
    if (!tec_are_items_equal(&cur, &in)) return 0;
    i32 room = item_lim - cur.count;
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

int chest_live_comparator_strength(const ChestLive *c)
{
    float fullness = 0.0f;
    int occupied = 0;
    if (!c) return 0;
    for (int slot = 0; slot < CHEST_LIVE_SLOTS; ++slot) {
        ICStack stack = chest_live_get(c, slot);
        if (stack.item <= 0 || stack.count <= 0) continue;
        int limit = tec_max_stack_size(stack.item);
        if (limit > TEC_STACK_LIMIT) limit = TEC_STACK_LIMIT;
        if (limit < 1) limit = 1;
        fullness += (float)stack.count / (float)limit;
        occupied++;
    }
    fullness /= (float)CHEST_LIVE_SLOTS;
    return (int)floorf(fullness * 14.0f) + (occupied > 0 ? 1 : 0);
}

int chest_live_double_comparator_strength(
        const ChestLive *first, const ChestLive *second)
{
    const ChestLive *halves[2] = {first, second};
    float fullness = 0.0f;
    int occupied = 0;
    if (!first || !second) return 0;
    for (int half = 0; half < 2; ++half) {
        for (int slot = 0; slot < CHEST_LIVE_SLOTS; ++slot) {
            ICStack stack = chest_live_get(halves[half], slot);
            if (stack.item <= 0 || stack.count <= 0) continue;
            int limit = tec_max_stack_size(stack.item);
            if (limit > TEC_STACK_LIMIT) limit = TEC_STACK_LIMIT;
            if (limit < 1) limit = 1;
            fullness += (float)stack.count / (float)limit;
            occupied++;
        }
    }
    fullness /= (float)(CHEST_LIVE_SLOTS * 2);
    return (int)floorf(fullness * 14.0f) + (occupied > 0 ? 1 : 0);
}
