/* tile_entity_chest: 27-slot chest inventory insert/extract (TileEntityChest + InventoryBasic.addItem).
 * ItemStack = (item,count,meta); areItemsEqual = item+meta match. Stack limit 64.
 * CUT: double chest, loot tables, NBT, sounds, player proximity sync. Lid animation from update() kept.
 * Deterministic op battery; CPU==CUDA. */
#ifndef MC_TILE_ENTITY_CHEST_H
#define MC_TILE_ENTITY_CHEST_H

#include "mc.h"

enum {
    TEC_SLOTS          = 27,
    TEC_STACK_LIMIT    = 64,
    TEC_MAX_ENCHANTS   = 8,  /* StoredEnchantments list cap for enchanted books */
    TEC_APPLE          = 260,
    TEC_BREAD          = 297,
    TEC_COAL           = 263,
    TEC_IRON_INGOT     = 265
};

#define TEC_LID_STEP 0.1f

/* StoredEnchantments-equivalent payload (id,level pairs). Ordinary items use n=0. */
typedef struct { i16 id; i16 level; } TecEnch;

typedef struct {
    i32 item;
    i32 count;
    i32 meta;
    i32 n_enchants;
    TecEnch enchants[TEC_MAX_ENCHANTS];
} TecStack;

typedef struct {
    TecStack slots[TEC_SLOTS];
    float lid_angle;
    float prev_lid_angle;
    int num_players_using;
    int ticks_since_sync;
} TeChest;

MC_HD static inline TecStack tec_empty(void) {
    TecStack s;
    int i;
    s.item = 0; s.count = 0; s.meta = 0; s.n_enchants = 0;
    for (i = 0; i < TEC_MAX_ENCHANTS; ++i) { s.enchants[i].id = 0; s.enchants[i].level = 0; }
    return s;
}

MC_HD static inline TecStack tec_mk(i32 item, i32 count, i32 meta) {
    TecStack s = tec_empty();
    s.item = item; s.count = count; s.meta = meta;
    return s;
}

MC_HD static inline int tec_is_empty(const TecStack *s) {
    return s->count <= 0 || s->item == 0;
}

MC_HD static inline int tec_enchants_equal(const TecStack *a, const TecStack *b) {
    int i;
    if (a->n_enchants != b->n_enchants) return 0;
    for (i = 0; i < a->n_enchants; ++i)
        if (a->enchants[i].id != b->enchants[i].id ||
            a->enchants[i].level != b->enchants[i].level)
            return 0;
    return 1;
}

MC_HD static inline int tec_are_items_equal(const TecStack *a, const TecStack *b) {
    if (tec_is_empty(a) || tec_is_empty(b)) return 0;
    if (a->item != b->item || a->meta != b->meta) return 0;
    return tec_enchants_equal(a, b);
}

MC_HD static inline i32 tec_max_stack_size(i32 item) {
    (void)item;
    return TEC_STACK_LIMIT;
}

MC_HD static inline void tec_init(TeChest *c) {
    for (int i = 0; i < TEC_SLOTS; ++i) c->slots[i] = tec_empty();
    c->lid_angle = 0.0f;
    c->prev_lid_angle = 0.0f;
    c->num_players_using = 0;
    c->ticks_since_sync = 0;
}

MC_HD static inline TecStack tec_get_stack(const TeChest *c, int index) {
    if (index < 0 || index >= TEC_SLOTS) return tec_empty();
    return c->slots[index];
}

MC_HD static inline TecStack tec_get_and_split(TeChest *c, int index, int amount) {
    if (index < 0 || index >= TEC_SLOTS || amount <= 0) return tec_empty();
    TecStack *slot = &c->slots[index];
    if (tec_is_empty(slot)) return tec_empty();
    int take = amount;
    int e;
    if (take > slot->count) take = slot->count;
    TecStack out = tec_mk(slot->item, take, slot->meta);
    out.n_enchants = slot->n_enchants;
    for (e = 0; e < slot->n_enchants && e < TEC_MAX_ENCHANTS; ++e)
        out.enchants[e] = slot->enchants[e];
    slot->count -= take;
    if (slot->count <= 0) *slot = tec_empty();
    return out;
}

MC_HD static inline void tec_set_slot(TeChest *c, int index, TecStack stack) {
    if (index < 0 || index >= TEC_SLOTS) return;
    if (!tec_is_empty(&stack) && stack.count > TEC_STACK_LIMIT)
        stack.count = TEC_STACK_LIMIT;
    c->slots[index] = stack;
}

MC_HD static inline TecStack tec_decr_stack_size(TeChest *c, int index, int count) {
    return tec_get_and_split(c, index, count);
}

MC_HD static inline TecStack tec_remove_stack_from_slot(TeChest *c, int index) {
    if (index < 0 || index >= TEC_SLOTS) return tec_empty();
    TecStack out = c->slots[index];
    c->slots[index] = tec_empty();
    if (tec_is_empty(&out)) return tec_empty();
    return out;
}

/* InventoryBasic.addItem: first empty slot, else merge equal stacks. Returns leftover. */
MC_HD static inline TecStack tec_add_item(TeChest *c, TecStack stack) {
    if (tec_is_empty(&stack)) return tec_empty();
    TecStack rem = stack;
    for (int i = 0; i < TEC_SLOTS; ++i) {
        TecStack slot = c->slots[i];
        if (tec_is_empty(&slot)) {
            /* InventoryBasic.setInventorySlotContents clamps to the inventory stack limit,
             * silently dropping any excess (vanilla addItem returns EMPTY here). */
            if (rem.count > TEC_STACK_LIMIT) rem.count = TEC_STACK_LIMIT;
            c->slots[i] = rem;
            return tec_empty();
        }
        if (tec_are_items_equal(&slot, &rem)) {
            i32 limit = TEC_STACK_LIMIT;
            if (tec_max_stack_size(slot.item) < limit) limit = tec_max_stack_size(slot.item);
            i32 room = limit - slot.count;
            i32 move = rem.count;
            if (move > room) move = room;
            if (move > 0) {
                c->slots[i].count += move;
                rem.count -= move;
                if (rem.count <= 0) return tec_empty();
            }
        }
    }
    return rem;
}

MC_HD static inline void tec_open(TeChest *c) {
    if (c->num_players_using < 0) c->num_players_using = 0;
    c->num_players_using++;
}

MC_HD static inline void tec_close(TeChest *c) {
    if (c->num_players_using > 0) c->num_players_using--;
}

/* TileEntityChest.update lid subset (no world/adjacent chest/sounds). */
MC_HD static inline void tec_tick(TeChest *c) {
    c->ticks_since_sync++;
    c->prev_lid_angle = c->lid_angle;
    if (c->num_players_using > 0 && c->lid_angle < 1.0f) {
        c->lid_angle += TEC_LID_STEP;
        if (c->lid_angle > 1.0f) c->lid_angle = 1.0f;
    } else if (c->num_players_using == 0 && c->lid_angle > 0.0f) {
        c->lid_angle -= TEC_LID_STEP;
        if (c->lid_angle < 0.0f) c->lid_angle = 0.0f;
    }
}

MC_HD static inline int tec_total_items(const TeChest *c) {
    int sum = 0;
    for (int i = 0; i < TEC_SLOTS; ++i) sum += c->slots[i].count;
    return sum;
}

#define TEC_NMARKS 6
#define TEC_PER_MARK (TEC_SLOTS + 4) /* counts + lid + players + total + leftover */
#define TEC_OUT (TEC_NMARKS * TEC_PER_MARK)

MC_HD static inline void tec_dump_mark(const TeChest *c, TecStack leftover, u64 *out, int *o) {
    for (int i = 0; i < TEC_SLOTS; ++i)
        out[(*o)++] = (u64)(u32)c->slots[i].count;
    union { float f; u32 u; } lid;
    lid.f = c->lid_angle;
    out[(*o)++] = lid.u;
    out[(*o)++] = (u64)(u32)c->num_players_using;
    out[(*o)++] = (u64)(u32)tec_total_items(c);
    out[(*o)++] = (u64)(u32)leftover.count;
}

MC_HD static inline void tec_run_battery(u64 *out) {
    TeChest c;
    tec_init(&c);
    TecStack leftover = tec_empty();
    int o = 0;

    leftover = tec_add_item(&c, tec_mk(TEC_APPLE, 20, 0));
    tec_dump_mark(&c, leftover, out, &o);

    leftover = tec_add_item(&c, tec_mk(TEC_APPLE, 50, 0));
    tec_dump_mark(&c, leftover, out, &o);

    leftover = tec_add_item(&c, tec_mk(TEC_BREAD, 30, 0));
    tec_dump_mark(&c, leftover, out, &o);

    tec_open(&c);
    for (int t = 0; t < 5; ++t) tec_tick(&c);
    leftover = tec_add_item(&c, tec_mk(TEC_IRON_INGOT, 10, 0));
    tec_dump_mark(&c, leftover, out, &o);

    {
        TecStack got = tec_decr_stack_size(&c, 0, 15);
        leftover = got;
    }
    tec_set_slot(&c, 5, tec_mk(TEC_COAL, 40, 0));
    leftover = tec_add_item(&c, tec_mk(TEC_APPLE, 6, 0));
    tec_close(&c);
    for (int t = 0; t < 12; ++t) tec_tick(&c);
    tec_dump_mark(&c, leftover, out, &o);

    /* overflow: chest nearly full; leftover must be non-zero */
    leftover = tec_add_item(&c, tec_mk(TEC_BREAD, 200, 0));
    tec_dump_mark(&c, leftover, out, &o);
}

#endif /* MC_TILE_ENTITY_CHEST_H */
