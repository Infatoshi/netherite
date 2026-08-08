/* inventory_stack_rules: ItemStack stack limits + InventoryPlayer merge/split/hotbar rules.
 *
 * Subset: ItemStack.getMaxStackSize/isStackable/splitStack; InventoryPlayer.canMergeStacks,
 * storePartialItemStack/addItemStackToInventory, decrStackSize, getBestHotbarSlot, pickItem,
 * changeCurrentItem, getFirstEmptyStack. Main (36) + armor (36..39) + offhand (40).
 *
 * Harness: fixed hex scenario battery (ISR_NUM_SCENARIOS). READ-ONLY: items_core.h (ICStack ids).
 * CPU==CUDA. */
#ifndef MC_INVENTORY_STACK_RULES_H
#define MC_INVENTORY_STACK_RULES_H

#include "mc.h"
#include "items_core.h"

#define ISR_MAIN_SLOTS   36
#define ISR_ARMOR_SLOTS  4
#define ISR_ARMOR0       36  /* feet, legs, chest, head (InventoryPlayer.armorInventory) */
#define ISR_ARMOR_FEET   36
#define ISR_ARMOR_LEGS   37
#define ISR_ARMOR_CHEST  38
#define ISR_ARMOR_HEAD   39
#define ISR_OFFHAND_SLOT 40
#define ISR_INV_LIMIT    64
/* 0..9 ordinary; 10..11 enchanted-book max-stack-1 + StoredEnchantments retention.
 *
 * Per-scenario emit schema (ISR_FIELDS_PER = 10 u32s), all scenarios:
 *   [0] current_item
 *   [1] leftover.count
 *   [2] leftover.item
 *   [3] hotbar_total (main[0..8] counts)
 *   [4] main_total   (main[9..35] counts)
 *   [5] main[0].count
 *   [6] op_ok (1/0)
 *   [7] merge_slot (or sentinel used by the scenario)
 *   [8] main[0].n_enchants
 *   [9] leftover.n_enchants  -- ordinary scenarios 0..9
 *       main[1].n_enchants   -- scenarios 10..11 overwrite field 9 after emit
 *                              so both book payloads are goldened (multi n=2, sharp5 n=1)
 */
#define ISR_NUM_SCENARIOS 12
#define ISR_FIELDS_PER    10
#define ISR_OUT           (ISR_NUM_SCENARIOS * ISR_FIELDS_PER)
#define ISR_ELYTRA_ITEM  443
#define ISR_ELYTRA_MAX_DAMAGE 432

typedef struct IsrInv {
    ICStack main[ISR_MAIN_SLOTS];
    ICStack armor[ISR_ARMOR_SLOTS]; /* tape/Isr indices 36..39 */
    ICStack offhand;
    i32 current_item;
} IsrInv;

MC_HD static inline int isr_is_empty(const ICStack *s) {
    return s->item == IC_AIR || s->count <= 0;
}

MC_HD static inline i32 isr_inv_stack_limit(void) { return ISR_INV_LIMIT; }

/* Leather..diamond armor ids 298..317 and elytra 443 are unstackable. */
MC_HD static inline int isr_is_armor_or_elytra(i32 item) {
    return (item >= 298 && item <= 317) || item == ISR_ELYTRA_ITEM;
}

MC_HD static inline i32 isr_max_stack_size(i32 item, i32 meta) {
    (void)meta;
    if (item == IC_WOODEN_PICKAXE || item == IC_STONE_PICKAXE ||
        item == 257 || item == 278 || /* iron/diamond pickaxe */
        item == 268 || item == 272 || item == 267 || item == 276 || /* swords */
        item == 261 || item == 259 || item == 359 || item == 398 ||
        item == 442 || item == 355) return 1;
    if (item == IC_ENCHANTED_BOOK) return 1; /* ItemEnchantedBook 1.11.2 */
    if (isr_is_armor_or_elytra(item)) return 1;
    if (item == IC_BUCKET) return 16;
    if (item == IC_WATER_BUCKET || item == IC_LAVA_BUCKET || item == 329 ||
        item == 373 || item == 438 || item == 441 ||
        (item >= 417 && item <= 419) ||
        (item >= 2256 && item <= 2267)) return 1;
    return ISR_INV_LIMIT;
}

MC_HD static inline int isr_is_damageable(i32 item) {
    return item == IC_WOODEN_PICKAXE || item == IC_STONE_PICKAXE || item == 257 ||
        item == 278 || item == 268 || item == 272 || item == 267 || item == 276 ||
        item == 261 || item == 259 || item == 359 || item == 398 || item == 442 ||
        isr_is_armor_or_elytra(item);
}

/* ItemElytra.isBroken naming is inverted: true means still usable for flight. */
MC_HD static inline int isr_elytra_usable(const ICStack *s) {
    if (!s || s->item != ISR_ELYTRA_ITEM || s->count <= 0) return 0;
    return s->meta < ISR_ELYTRA_MAX_DAMAGE - 1;
}

MC_HD static inline int isr_is_armor_index(int index) {
    return index >= ISR_ARMOR0 && index < ISR_ARMOR0 + ISR_ARMOR_SLOTS;
}

MC_HD static inline int isr_is_stackable(i32 item, i32 meta) {
    i32 max = isr_max_stack_size(item, meta);
    if (max <= 1) return 0;
    if (isr_is_damageable(item) && meta > 0) return 0;
    return 1;
}

MC_HD static inline int isr_stack_equal_exact(const ICStack *a, const ICStack *b) {
    /* 1.11.2: item + damage + NBT tags (StoredEnchantments list). */
    return ic_stack_equal(a, b);
}

MC_HD static inline void isr_init(IsrInv *inv) {
    for (int i = 0; i < ISR_MAIN_SLOTS; ++i) inv->main[i] = ic_empty();
    for (int i = 0; i < ISR_ARMOR_SLOTS; ++i) inv->armor[i] = ic_empty();
    inv->offhand = ic_empty();
    inv->current_item = 0;
}

MC_HD static inline ICStack isr_get_stack(const IsrInv *inv, int index) {
    if (index >= 0 && index < ISR_MAIN_SLOTS) return inv->main[index];
    if (isr_is_armor_index(index)) return inv->armor[index - ISR_ARMOR0];
    if (index == ISR_OFFHAND_SLOT) return inv->offhand;
    return ic_empty();
}

MC_HD static inline void isr_set_stack(IsrInv *inv, int index, ICStack stack) {
    if (index >= 0 && index < ISR_MAIN_SLOTS) inv->main[index] = stack;
    else if (isr_is_armor_index(index)) inv->armor[index - ISR_ARMOR0] = stack;
    else if (index == ISR_OFFHAND_SLOT) inv->offhand = stack;
}

MC_HD static inline int isr_can_merge(const IsrInv *inv, const ICStack *stack1, const ICStack *stack2) {
    (void)inv;
    if (isr_is_empty(stack1)) return 0;
    if (!isr_stack_equal_exact(stack1, stack2)) return 0;
    if (!isr_is_stackable(stack1->item, stack1->meta)) return 0;
    {
        i32 max = isr_max_stack_size(stack1->item, stack1->meta);
        if (stack1->count >= max) return 0;
    }
    if (stack1->count >= isr_inv_stack_limit()) return 0;
    return 1;
}

MC_HD static inline int isr_get_first_empty_stack(const IsrInv *inv) {
    for (int i = 0; i < ISR_MAIN_SLOTS; ++i)
        if (isr_is_empty(&inv->main[i])) return i;
    return -1;
}

MC_HD static inline int isr_is_hotbar(int index) {
    return index >= 0 && index < 9;
}

MC_HD static inline int isr_get_best_hotbar_slot(const IsrInv *inv) {
    int i;
    for (i = 0; i < 9; ++i) {
        int j = (inv->current_item + i) % 9;
        if (isr_is_empty(&inv->main[j])) return j;
    }
    for (i = 0; i < 9; ++i) {
        int j = (inv->current_item + i) % 9;
        /* Vanilla prefers unenchanted hotbar slots; subset has no item-enchant
         * flag beyond StoredEnchantments-on-books (always max-stack 1). */
        (void)j;
        return j;
    }
    return inv->current_item;
}

MC_HD static inline void isr_change_current_item(IsrInv *inv, int direction) {
    if (direction > 0) direction = 1;
    if (direction < 0) direction = -1;
    inv->current_item -= direction;
    while (inv->current_item < 0) inv->current_item += 9;
    while (inv->current_item >= 9) inv->current_item -= 9;
}

MC_HD static inline void isr_pick_item(IsrInv *inv, int index) {
    ICStack tmp;
    if (index < 0 || index >= ISR_MAIN_SLOTS) return;
    inv->current_item = isr_get_best_hotbar_slot(inv);
    tmp = inv->main[inv->current_item];
    inv->main[inv->current_item] = inv->main[index];
    inv->main[index] = tmp;
}

MC_HD static inline ICStack isr_split_stack(ICStack *src, int amount) {
    int take = amount;
    ICStack out;
    if (!src || take <= 0 || isr_is_empty(src)) return ic_empty();
    if (take > src->count) take = src->count;
    out = ic_with_count(src, take);
    src->count -= take;
    if (src->count <= 0) *src = ic_empty();
    return out;
}

MC_HD static inline ICStack isr_decr_stack_size(IsrInv *inv, int index, int count) {
    ICStack *slot;
    if (index >= 0 && index < ISR_MAIN_SLOTS) slot = &inv->main[index];
    else if (isr_is_armor_index(index)) slot = &inv->armor[index - ISR_ARMOR0];
    else if (index == ISR_OFFHAND_SLOT) slot = &inv->offhand;
    else return ic_empty();
    if (isr_is_empty(slot) || count <= 0) return ic_empty();
    return isr_split_stack(slot, count);
}

MC_HD static inline int isr_store_item_stack(const IsrInv *inv, const ICStack *incoming) {
    ICStack cur = isr_get_stack(inv, inv->current_item);
    if (isr_can_merge(inv, &cur, incoming)) return inv->current_item;
    cur = inv->offhand;
    if (isr_can_merge(inv, &cur, incoming)) return ISR_OFFHAND_SLOT;
    for (int i = 0; i < ISR_MAIN_SLOTS; ++i) {
        cur = inv->main[i];
        if (isr_can_merge(inv, &cur, incoming)) return i;
    }
    return -1;
}

MC_HD static inline i32 isr_store_partial_item_stack(IsrInv *inv, ICStack *incoming) {
    i32 i = incoming->count;
    int j = isr_store_item_stack(inv, incoming);
    ICStack slot;

    if (j == -1) j = isr_get_first_empty_stack(inv);
    if (j == -1) return i;

    slot = isr_get_stack(inv, j);
    if (isr_is_empty(&slot)) {
        slot = *incoming;
        slot.count = 0;
        isr_set_stack(inv, j, slot);
        slot = isr_get_stack(inv, j);
    }

    {
        i32 k = i;
        i32 max = isr_max_stack_size(slot.item, slot.meta);
        if (i > max - slot.count) k = max - slot.count;
        if (k > isr_inv_stack_limit() - slot.count) k = isr_inv_stack_limit() - slot.count;
        if (k == 0) return i;
        i = i - k;
        slot.count += k;
        isr_set_stack(inv, j, slot);
    }
    return i;
}

MC_HD static inline int isr_add_item_stack_to_inventory(IsrInv *inv, ICStack *incoming) {
    i32 prev;
    if (isr_is_empty(incoming)) return 0;
    if (isr_is_damageable(incoming->item) && incoming->meta > 0) {
        int j = isr_get_first_empty_stack(inv);
        if (j >= 0) {
            isr_set_stack(inv, j, *incoming);
            incoming->count = 0;
            return 1;
        }
        return 0;
    }
    while (1) {
        prev = incoming->count;
        incoming->count = isr_store_partial_item_stack(inv, incoming);
        if (isr_is_empty(incoming) || incoming->count >= prev) break;
    }
    return incoming->count < prev ? 1 : 0;
}

MC_HD static inline i32 isr_hotbar_total(const IsrInv *inv) {
    i32 sum = 0;
    for (int i = 0; i < 9; ++i) sum += inv->main[i].count;
    return sum;
}

MC_HD static inline i32 isr_main_total(const IsrInv *inv) {
    i32 sum = 0;
    for (int i = 9; i < ISR_MAIN_SLOTS; ++i) sum += inv->main[i].count;
    return sum;
}

MC_HD static inline void isr_emit_scenario(const IsrInv *inv, const ICStack *leftover, int op_ok,
                                           i32 merge_slot, u32 *out, int base) {
    out[base + 0] = (u32)inv->current_item;
    out[base + 1] = leftover ? (u32)leftover->count : 0u;
    out[base + 2] = leftover ? (u32)leftover->item : 0u;
    out[base + 3] = (u32)isr_hotbar_total(inv);
    out[base + 4] = (u32)isr_main_total(inv);
    out[base + 5] = (u32)inv->main[0].count;
    out[base + 6] = (u32)(op_ok ? 1 : 0);
    out[base + 7] = (u32)merge_slot;
    out[base + 8] = (u32)inv->main[0].n_enchants;
    out[base + 9] = leftover ? (u32)leftover->n_enchants : 0u;
}

/* Sharpness III + Unbreaking I book (matches container_click multi book). */
MC_HD static inline ICStack isr_mk_book_multi(void) {
    ICStack s = ic_mk(IC_ENCHANTED_BOOK, 1, 0);
    IcEnch e[2];
    e[0].id = 16; e[0].level = 3;
    e[1].id = 34; e[1].level = 1;
    ic_copy_enchants(&s, e, 2);
    return s;
}

MC_HD static inline ICStack isr_mk_book_sharp5(void) {
    ICStack s = ic_mk(IC_ENCHANTED_BOOK, 1, 0);
    IcEnch e[1];
    e[0].id = 16; e[0].level = 5;
    ic_copy_enchants(&s, e, 1);
    return s;
}

MC_HD static inline void isr_run_scenario(int idx, u32 *out) {
    IsrInv inv;
    ICStack incoming, leftover, split;
    int op_ok;
    i32 merge_slot;
    int base = idx * ISR_FIELDS_PER;

    isr_init(&inv);

    switch (idx) {
    case 0:
        inv.current_item = 0;
        incoming = ic_mk(IC_APPLE, 30, 0);
        leftover = incoming;
        op_ok = isr_add_item_stack_to_inventory(&inv, &leftover);
        merge_slot = isr_store_item_stack(&inv, &incoming);
        isr_emit_scenario(&inv, &leftover, op_ok, merge_slot, out, base);
        break;
    case 1:
        inv.main[0] = ic_mk(IC_APPLE, 40, 0);
        incoming = ic_mk(IC_APPLE, 30, 0);
        leftover = incoming;
        op_ok = isr_add_item_stack_to_inventory(&inv, &leftover);
        merge_slot = 0;
        isr_emit_scenario(&inv, &leftover, op_ok, merge_slot, out, base);
        break;
    case 2:
        inv.current_item = 3;
        inv.main[3] = ic_mk(IC_BREAD, 20, 0);
        incoming = ic_mk(IC_BREAD, 10, 0);
        leftover = incoming;
        op_ok = isr_add_item_stack_to_inventory(&inv, &leftover);
        merge_slot = 3;
        isr_emit_scenario(&inv, &leftover, op_ok, merge_slot, out, base);
        break;
    case 3:
        inv.main[5] = ic_mk(IC_APPLE, 50, 0);
        split = isr_decr_stack_size(&inv, 5, 32);
        leftover = split;
        op_ok = split.count == 32 ? 1 : 0;
        merge_slot = 5;
        isr_emit_scenario(&inv, &leftover, op_ok, merge_slot, out, base);
        break;
    case 4:
        inv.current_item = 2;
        merge_slot = isr_get_best_hotbar_slot(&inv);
        leftover = ic_empty();
        isr_emit_scenario(&inv, &leftover, 1, merge_slot, out, base);
        break;
    case 5:
        inv.current_item = 4;
        for (int i = 0; i < 9; ++i)
            inv.main[i] = ic_mk(IC_APPLE, 8, 0);
        merge_slot = isr_get_best_hotbar_slot(&inv);
        leftover = ic_empty();
        isr_emit_scenario(&inv, &leftover, 1, merge_slot, out, base);
        break;
    case 6:
        inv.current_item = 8;
        isr_change_current_item(&inv, 1);
        merge_slot = inv.current_item;
        isr_change_current_item(&inv, -1);
        leftover = ic_empty();
        isr_emit_scenario(&inv, &leftover, 1, merge_slot, out, base);
        break;
    case 7:
        inv.current_item = 1;
        inv.main[15] = ic_mk(IC_STONE, 10, 0);
        inv.main[1] = ic_mk(IC_IRON_ORE, 5, 0);
        isr_pick_item(&inv, 15);
        leftover = ic_empty();
        merge_slot = inv.current_item;
        isr_emit_scenario(&inv, &leftover, 1, merge_slot, out, base);
        break;
    case 8:
        incoming = ic_mk(IC_WOODEN_PICKAXE, 1, 0);
        leftover = incoming;
        op_ok = isr_add_item_stack_to_inventory(&inv, &leftover);
        incoming = ic_mk(IC_WOODEN_PICKAXE, 1, 0);
        op_ok &= isr_add_item_stack_to_inventory(&inv, &incoming);
        leftover = incoming;
        merge_slot = isr_get_first_empty_stack(&inv);
        isr_emit_scenario(&inv, &leftover, op_ok, merge_slot, out, base);
        break;
    case 9:
        inv.current_item = 0;
        inv.main[0] = ic_mk(IC_APPLE, 64, 0);
        inv.offhand = ic_mk(IC_APPLE, 10, 0);
        incoming = ic_mk(IC_APPLE, 60, 0);
        leftover = incoming;
        op_ok = isr_add_item_stack_to_inventory(&inv, &leftover);
        merge_slot = ISR_OFFHAND_SLOT;
        isr_emit_scenario(&inv, &leftover, op_ok, merge_slot, out, base);
        break;
    case 10:
        /* Enchanted books max stack 1: two equal-tag books occupy two slots
         * (ground-pickup path via addItemStackToInventory). Tags retained. */
        {
            ICStack b1 = isr_mk_book_multi();
            ICStack b2 = isr_mk_book_multi();
            leftover = b1;
            op_ok = isr_add_item_stack_to_inventory(&inv, &leftover);
            leftover = b2;
            op_ok &= isr_add_item_stack_to_inventory(&inv, &leftover);
            /* After both placed, first empty is slot 2; store_item sees no merge. */
            merge_slot = isr_get_first_empty_stack(&inv);
            /* leftover emptied; emit n_enchants from main[0] via field 8. */
            leftover = ic_empty();
            isr_emit_scenario(&inv, &leftover, op_ok, merge_slot, out, base);
            /* Overwrite field 9 with main[1].n_enchants so both payloads are checked. */
            out[base + 9] = (u32)inv.main[1].n_enchants;
        }
        break;
    case 11:
        /* Mismatched StoredEnchantments never merge (Sharpness V next to multi). */
        inv.main[0] = isr_mk_book_multi();
        leftover = isr_mk_book_sharp5();
        op_ok = isr_add_item_stack_to_inventory(&inv, &leftover);
        merge_slot = isr_store_item_stack(&inv, &inv.main[0]); /* -1: cannot merge self full */
        leftover = ic_empty();
        isr_emit_scenario(&inv, &leftover, op_ok, merge_slot, out, base);
        out[base + 9] = (u32)inv.main[1].n_enchants; /* Sharpness V has n=1 */
        break;
    default:
        leftover = ic_empty();
        isr_emit_scenario(&inv, &leftover, 0, -1, out, base);
        break;
    }
}

MC_HD static inline void isr_run_battery(u32 *out) {
    for (int i = 0; i < ISR_NUM_SCENARIOS; ++i)
        isr_run_scenario(i, out);
}

#endif /* MC_INVENTORY_STACK_RULES_H */
