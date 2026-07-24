/* container_click: Container.slotClick deterministic core (synthetic 9-slot + cursor).
 *
 * PORT: net/minecraft/inventory/Container.java slotClick (PICKUP / QUICK_MOVE / THROW)
 *        + Slot get/put/decr, ItemStack split/grow/shrink, Container.mergeItemStack subset.
 *
 * State: 9 inventory stacks + 1 cursor {itemId, count, meta}. No crafting matrix.
 * After each scripted click, emit full inventory+cursor as packed u32s
 * (item,count,meta,n_enchants, then IC_MAX_ENCHANTS x (id,level)).
 *
 * Covered:
 *   PICKUP  (button 0 left / 1 right): pick, place, half-pick, place-one, merge, swap;
 *            slotId -999 drops cursor (all / one).
 *   QUICK_MOVE (shift-click): transfer into other empty/mergeable slots (mergeItemStack-style
 *            excluding source; retry while same item remains).
 *   THROW   (Q / ctrl-Q): drop one/all FROM SLOT when cursor empty (vanilla). Cursor drop is
 *            PICKUP/-999, not THROW.
 *
 * Boundaries (not ported):
 *   QUICK_CRAFT drag painting, CLONE creative middle-click, SWAP hotbar keys, PICKUP_ALL
 *   double-click gather, armor slot limits/validity, crafting matrix / recipe book,
 *   full arbitrary NBT beyond StoredEnchantments on ICStack,
 *   player.dropItem entity spawn (drops are discarded), creative branches.
 * Stack match/split copy ICStack.n_enchants (1.11.2 areItemStackTagsEqual subset).
 *
 * READ-ONLY deps: items_core.h (ICStack + item ids). Stack limits mirror inventory_stack_rules
 * (pickaxes/buckets/enchanted_book max 1, else 64). CPU==CUDA. */
#ifndef MC_CONTAINER_CLICK_H
#define MC_CONTAINER_CLICK_H

#include "mc.h"
#include "items_core.h"

enum {
    CC_SLOTS          = 9,
    CC_INV_LIMIT      = 64,
    CC_CLICK_PICKUP   = 0,  /* ClickType.PICKUP ordinal */
    CC_CLICK_QUICK_MOVE = 1,
    CC_CLICK_THROW    = 4,
    CC_NUM_CLICKS     = 16,
    /* item,count,meta,n_enchants + IC_MAX_ENCHANTS*(id,level). Battery uses n=0. */
    CC_STACK_FIELDS   = (4 + 2 * IC_MAX_ENCHANTS),
    CC_STATE_FIELDS   = ((CC_SLOTS + 1) * CC_STACK_FIELDS), /* 9 slots + cursor */
    CC_OUT            = (CC_NUM_CLICKS * CC_STATE_FIELDS)
};

typedef struct {
    ICStack slots[CC_SLOTS];
    ICStack cursor;
} CcInv;

typedef struct {
    i32 slot_id;   /* 0..8 inventory, -999 outside (cursor drop on PICKUP) */
    i32 button;    /* dragType: 0 left/Q, 1 right/ctrl-Q */
    i32 click_type;
} CcClick;

/* ---- stack helpers (ItemStack subset) ---- */

MC_HD static inline int cc_is_empty(const ICStack *s) {
    return s->item == IC_AIR || s->count <= 0;
}

MC_HD static inline void cc_normalize(ICStack *s) {
    if (s->count <= 0 || s->item == IC_AIR) *s = ic_empty();
}

MC_HD static inline i32 cc_max_stack_size(i32 item, i32 meta) {
    (void)meta;
    if (item == IC_WOODEN_PICKAXE || item == IC_STONE_PICKAXE) return 1;
    if (item == IC_ENCHANTED_BOOK) return 1; /* ItemEnchantedBook 1.11.2 */
    if (item == IC_BUCKET || item == IC_WATER_BUCKET || item == IC_LAVA_BUCKET) return 1;
    return CC_INV_LIMIT;
}

MC_HD static inline int cc_is_stackable(const ICStack *s) {
    if (cc_is_empty(s)) return 0;
    return cc_max_stack_size(s->item, s->meta) > 1;
}

/* areItemsEqual + areItemStackTagsEqual (StoredEnchantments must match). */
MC_HD static inline int cc_stack_match(const ICStack *a, const ICStack *b) {
    return ic_stack_equal(a, b);
}

MC_HD static inline i32 cc_slot_stack_limit(void) { return CC_INV_LIMIT; }

/* Slot.getItemStackLimit(stack) -> inventory limit (vanilla Slot default) */
MC_HD static inline i32 cc_item_stack_limit(const ICStack *stack) {
    (void)stack;
    return cc_slot_stack_limit();
}

MC_HD static inline ICStack cc_split_stack(ICStack *src, int amount) {
    int take = amount;
    ICStack out;
    if (take > src->count) take = src->count;
    if (take <= 0 || cc_is_empty(src)) return ic_empty();
    /* ItemStack.splitStack copies NBT/StoredEnchantments onto the taken half. */
    out = ic_with_count(src, take);
    src->count -= take;
    cc_normalize(src);
    return out;
}

MC_HD static inline ICStack cc_decr_slot(ICStack *slot, int amount) {
    return cc_split_stack(slot, amount);
}

MC_HD static inline void cc_grow(ICStack *s, int n) {
    s->count += n;
    cc_normalize(s);
}

MC_HD static inline void cc_shrink(ICStack *s, int n) {
    s->count -= n;
    cc_normalize(s);
}

MC_HD static inline void cc_init(CcInv *inv) {
    for (int i = 0; i < CC_SLOTS; ++i) inv->slots[i] = ic_empty();
    inv->cursor = ic_empty();
}

/* ---- mergeItemStack excluding one source index (synthetic transfer target) ---- */

MC_HD static inline int cc_merge_except(CcInv *inv, ICStack *stack, int exclude) {
    int flag = 0;
    int i;

    if (cc_is_stackable(stack)) {
        for (i = 0; i < CC_SLOTS && !cc_is_empty(stack); ++i) {
            ICStack *slot;
            i32 j, max_size;
            if (i == exclude) continue;
            slot = &inv->slots[i];
            if (cc_is_empty(slot) || !cc_stack_match(slot, stack)) continue;
            j = slot->count + stack->count;
            max_size = cc_slot_stack_limit();
            {
                i32 ms = cc_max_stack_size(stack->item, stack->meta);
                if (ms < max_size) max_size = ms;
            }
            if (j <= max_size) {
                stack->count = 0;
                slot->count = j;
                cc_normalize(stack);
                flag = 1;
            } else if (slot->count < max_size) {
                cc_shrink(stack, max_size - slot->count);
                slot->count = max_size;
                flag = 1;
            }
        }
    }

    if (!cc_is_empty(stack)) {
        for (i = 0; i < CC_SLOTS; ++i) {
            ICStack *slot;
            if (i == exclude) continue;
            slot = &inv->slots[i];
            if (!cc_is_empty(slot)) continue;
            {
                i32 lim = cc_slot_stack_limit();
                if (stack->count > lim)
                    *slot = cc_split_stack(stack, lim);
                else
                    *slot = cc_split_stack(stack, stack->count);
            }
            flag = 1;
            break;
        }
    }
    return flag;
}

/* transferStackInSlot: merge source into other slots; return original copy if moved. */
MC_HD static inline ICStack cc_transfer_stack_in_slot(CcInv *inv, int index) {
    ICStack original;
    ICStack *slot;
    i32 before;
    if (index < 0 || index >= CC_SLOTS) return ic_empty();
    slot = &inv->slots[index];
    if (cc_is_empty(slot)) return ic_empty();
    original = *slot;
    before = slot->count;
    cc_merge_except(inv, slot, index);
    if (slot->count == before) return ic_empty(); /* nothing moved */
    return original;
}

/* ---- slotClick core ---- */

MC_HD static inline void cc_slot_click(CcInv *inv, int slot_id, int drag_type, int click_type) {
    /* dragEvent always 0 in this subset (no QUICK_CRAFT). */

    if ((click_type == CC_CLICK_PICKUP || click_type == CC_CLICK_QUICK_MOVE)
        && (drag_type == 0 || drag_type == 1)) {

        if (slot_id == -999) {
            /* Drop from cursor (outside GUI). */
            if (!cc_is_empty(&inv->cursor)) {
                if (drag_type == 0) {
                    inv->cursor = ic_empty();
                } else {
                    /* dragType 1: splitStack(1) discarded */
                    (void)cc_split_stack(&inv->cursor, 1);
                }
            }
        } else if (click_type == CC_CLICK_QUICK_MOVE) {
            /* transfer + retry while same item remains (vanilla retrySlotClick), loop not recurse */
            if (slot_id < 0 || slot_id >= CC_SLOTS) return;
            for (;;) {
                ICStack moved = cc_transfer_stack_in_slot(inv, slot_id);
                if (cc_is_empty(&moved)) break;
                if (cc_is_empty(&inv->slots[slot_id])
                    || inv->slots[slot_id].item != moved.item) break;
            }
        } else {
            /* PICKUP on a slot */
            ICStack *slot;
            ICStack *cursor;
            if (slot_id < 0 || slot_id >= CC_SLOTS) return;
            slot = &inv->slots[slot_id];
            cursor = &inv->cursor;

            if (cc_is_empty(slot)) {
                if (!cc_is_empty(cursor)) {
                    /* place from cursor into empty slot */
                    int l2 = drag_type == 0 ? cursor->count : 1;
                    i32 lim = cc_item_stack_limit(cursor);
                    if (l2 > lim) l2 = lim;
                    *slot = cc_split_stack(cursor, l2);
                }
            } else {
                /* canTakeStack always true */
                if (cc_is_empty(cursor)) {
                    /* pick up (full or half) */
                    int k2 = drag_type == 0 ? slot->count : (slot->count + 1) / 2;
                    *cursor = cc_decr_slot(slot, k2);
                } else {
                    /* cursor non-empty, slot non-empty */
                    if (cc_stack_match(slot, cursor)) {
                        /* same item: try place into slot */
                        int j2 = drag_type == 0 ? cursor->count : 1;
                        i32 lim = cc_item_stack_limit(cursor);
                        i32 maxs = cc_max_stack_size(cursor->item, cursor->meta);
                        if (j2 > lim - slot->count) j2 = lim - slot->count;
                        if (j2 > maxs - slot->count) j2 = maxs - slot->count;
                        if (j2 > 0) {
                            cc_shrink(cursor, j2);
                            cc_grow(slot, j2);
                        }
                    } else {
                        /* swap if cursor fits slot limit */
                        i32 lim = cc_item_stack_limit(cursor);
                        if (cursor->count <= lim) {
                            ICStack tmp = *slot;
                            *slot = *cursor;
                            *cursor = tmp;
                        }
                    }
                    /* skipped: slot-not-valid-for-cursor but stackable-into-cursor branch
                     * (isItemValid always true here, so that else-if never runs after match fail
                     * once swap condition is checked). Vanilla only hits the take-into-cursor
                     * path when isItemValid is false; we always allow place/swap. */
                }
            }
        }
    } else if (click_type == CC_CLICK_THROW
               && cc_is_empty(&inv->cursor)
               && slot_id >= 0 && slot_id < CC_SLOTS) {
        ICStack *slot = &inv->slots[slot_id];
        if (!cc_is_empty(slot)) {
            int n = drag_type == 0 ? 1 : slot->count;
            (void)cc_decr_slot(slot, n); /* drop discarded */
        }
    }
}

/* ---- emit / battery ---- */

MC_HD static inline void cc_emit_stack(const ICStack *s, u32 *out, int *o) {
    int e, n;
    out[(*o)++] = (u32)s->item;
    out[(*o)++] = (u32)s->count;
    out[(*o)++] = (u32)s->meta;
    n = s->n_enchants;
    if (n < 0) n = 0;
    if (n > IC_MAX_ENCHANTS) n = IC_MAX_ENCHANTS;
    out[(*o)++] = (u32)n;
    for (e = 0; e < IC_MAX_ENCHANTS; ++e) {
        if (e < n) {
            out[(*o)++] = (u32)(u16)s->enchants[e].id;
            out[(*o)++] = (u32)(u16)s->enchants[e].level;
        } else {
            out[(*o)++] = 0u;
            out[(*o)++] = 0u;
        }
    }
}

MC_HD static inline void cc_emit_state(const CcInv *inv, u32 *out, int base) {
    int i, o = base;
    for (i = 0; i < CC_SLOTS; ++i)
        cc_emit_stack(&inv->slots[i], out, &o);
    cc_emit_stack(&inv->cursor, out, &o);
}

/* Fixed start state + scripted click tape (identical in Golden.java). */
MC_HD static inline void cc_setup_start(CcInv *inv) {
    cc_init(inv);
    inv->slots[0] = ic_mk(IC_APPLE, 32, 0);
    inv->slots[1] = ic_mk(IC_APPLE, 40, 0);
    inv->slots[2] = ic_mk(IC_BREAD, 10, 0);
    inv->slots[3] = ic_empty();
    inv->slots[4] = ic_mk(IC_STONE, 64, 0);
    inv->slots[5] = ic_mk(IC_WOODEN_PICKAXE, 1, 0);
    inv->slots[6] = ic_mk(IC_IRON_ORE, 16, 0);
    inv->slots[7] = ic_empty();
    inv->slots[8] = ic_mk(IC_BREAD, 5, 0);
    inv->cursor = ic_empty();
}

MC_HD static inline void cc_get_click_tape(CcClick *tape) {
    /* 16 clicks covering PICKUP / QUICK_MOVE / THROW (+ cursor drop via PICKUP -999). */
    tape[0]  = (CcClick){0, 0, CC_CLICK_PICKUP};       /* pick 32 apple */
    tape[1]  = (CcClick){3, 0, CC_CLICK_PICKUP};       /* place all into empty 3 */
    tape[2]  = (CcClick){2, 1, CC_CLICK_PICKUP};       /* right-pick half bread(10)->5 cursor */
    tape[3]  = (CcClick){7, 1, CC_CLICK_PICKUP};       /* right-place one bread into empty 7 */
    tape[4]  = (CcClick){7, 0, CC_CLICK_PICKUP};       /* pick the one bread */
    tape[5]  = (CcClick){8, 0, CC_CLICK_PICKUP};       /* merge into bread@8 (5+1=6) */
    tape[6]  = (CcClick){5, 0, CC_CLICK_PICKUP};       /* pick pickaxe */
    tape[7]  = (CcClick){6, 0, CC_CLICK_PICKUP};       /* swap pickaxe <-> iron ore */
    tape[8]  = (CcClick){-999, 1, CC_CLICK_PICKUP};    /* drop one from cursor (ore) */
    tape[9]  = (CcClick){-999, 0, CC_CLICK_PICKUP};    /* drop rest of cursor */
    tape[10] = (CcClick){1, 0, CC_CLICK_PICKUP};       /* pick 40 apple */
    tape[11] = (CcClick){0, 0, CC_CLICK_PICKUP};       /* merge into apple@0 if any / place */
    tape[12] = (CcClick){0, 0, CC_CLICK_QUICK_MOVE};   /* shift-move slot 0 into others */
    tape[13] = (CcClick){4, 0, CC_CLICK_THROW};        /* Q: drop 1 stone from slot 4 */
    tape[14] = (CcClick){4, 1, CC_CLICK_THROW};        /* ctrl-Q: drop rest of stone */
    tape[15] = (CcClick){8, 0, CC_CLICK_QUICK_MOVE};   /* shift-move bread */
}

MC_HD static inline void cc_run_battery(u32 *out) {
    CcInv inv;
    CcClick tape[CC_NUM_CLICKS];
    int c;
    cc_setup_start(&inv);
    cc_get_click_tape(tape);
    for (c = 0; c < CC_NUM_CLICKS; ++c) {
        cc_slot_click(&inv, tape[c].slot_id, tape[c].button, tape[c].click_type);
        cc_emit_state(&inv, out, c * CC_STATE_FIELDS);
    }
}

#endif /* MC_CONTAINER_CLICK_H */
