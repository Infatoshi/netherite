#include "game/brewing_live.h"

#include "inventory_stack_rules.h"
#include "tile_entity_brewing.h"

#include <math.h>

static TbStack bl_to_tb(const ICStack *stack) {
    if (!stack || isr_is_empty(stack)) return tb_empty();
    return tb_stack(stack->item, stack->count, stack->meta);
}

static int bl_limit(const ICStack *stack) {
    if (!stack || isr_is_empty(stack)) return 64;
    if (tb_is_potion_item(stack->item)) return 1;
    return isr_max_stack_size(stack->item, stack->meta);
}

int brewing_live_slot_valid(int slot, const ICStack *stack) {
    TbStack value = bl_to_tb(stack);
    if (slot < 0 || slot >= BREWING_LIVE_SLOTS || tb_stack_empty(value))
        return 0;
    if (slot <= 2) return tb_is_valid_input(value);
    if (slot == BREWING_LIVE_INGREDIENT) return tb_is_reagent(value);
    return slot == BREWING_LIVE_FUEL && value.item == TB_BLAZE_POWDER;
}

void brewing_live_init(ICStack slots[BREWING_LIVE_SLOTS],
                       BrewingLiveState *state) {
    if (!slots || !state) return;
    for (int i = 0; i < BREWING_LIVE_SLOTS; ++i) slots[i] = ic_empty();
    state->brew_time = 0;
    state->fuel = 0;
    state->ingredient_id = 0;
    state->bottle_bits = 0;
}

int brewing_live_insert(ICStack slots[BREWING_LIVE_SLOTS], int slot,
                        ICStack stack) {
    ICStack *dst;
    int limit, moved;
    if (!slots || !brewing_live_slot_valid(slot, &stack)) return 0;
    dst = &slots[slot];
    limit = bl_limit(&stack);
    if (!isr_is_empty(dst)) {
        if (!ic_stack_equal(dst, &stack)) return 0;
        limit -= dst->count;
    }
    if (limit <= 0) return 0;
    moved = stack.count < limit ? stack.count : limit;
    if (isr_is_empty(dst)) *dst = ic_with_count(&stack, moved);
    else dst->count += moved;
    return moved;
}

ICStack brewing_live_extract(ICStack slots[BREWING_LIVE_SLOTS], int slot,
                             int amount) {
    ICStack out;
    if (!slots || slot < 0 || slot >= BREWING_LIVE_SLOTS || amount <= 0
            || isr_is_empty(&slots[slot]))
        return ic_empty();
    out = ic_with_count(&slots[slot], amount);
    slots[slot].count -= out.count;
    if (slots[slot].count <= 0) slots[slot] = ic_empty();
    return out;
}

int brewing_live_bottle_bits(const ICStack slots[BREWING_LIVE_SLOTS]) {
    int bits = 0;
    if (!slots) return 0;
    if (!isr_is_empty(&slots[0])) bits |= 1;
    if (!isr_is_empty(&slots[1])) bits |= 2;
    if (!isr_is_empty(&slots[2])) bits |= 4;
    return bits;
}

int brewing_live_tick(ICStack slots[BREWING_LIVE_SLOTS],
                      BrewingLiveState *state, int *glass_bottle_drops) {
    TeBrewing kernel;
    ICStack before[BREWING_LIVE_SLOTS];
    int flags;
    if (glass_bottle_drops) *glass_bottle_drops = 0;
    if (!slots || !state) return 0;
    tb_init_empty(&kernel);
    for (int i = 0; i < BREWING_LIVE_SLOTS; ++i) {
        before[i] = slots[i];
        kernel.slots[i] = bl_to_tb(&slots[i]);
    }
    kernel.brew_time = state->brew_time;
    kernel.fuel = state->fuel;
    kernel.ingredient_id = state->ingredient_id;
    kernel.bottle_bits = state->bottle_bits;
    flags = tb_tick(&kernel);
    state->brew_time = kernel.brew_time;
    state->fuel = kernel.fuel;
    state->ingredient_id = kernel.ingredient_id;
    state->bottle_bits = kernel.bottle_bits;
    if (glass_bottle_drops) *glass_bottle_drops = kernel.container_drops;
    for (int i = 0; i < BREWING_LIVE_SLOTS; ++i) {
        TbStack after = kernel.slots[i];
        if (tb_stack_empty(after)) {
            slots[i] = ic_empty();
        } else if (!isr_is_empty(&before[i])
                && before[i].item == after.item
                && before[i].meta == after.meta) {
            /* shrink/grow retains the original ItemStack tag */
            slots[i] = before[i];
            slots[i].count = after.count;
        } else {
            /* PotionHelper constructs a fresh result stack. */
            slots[i] = ic_mk(after.item, after.count, after.meta);
        }
    }
    return flags;
}

int brewing_live_comparator_strength(
        const ICStack slots[BREWING_LIVE_SLOTS]) {
    float fullness = 0.0f;
    int occupied = 0;
    if (!slots) return 0;
    for (int i = 0; i < BREWING_LIVE_SLOTS; ++i) {
        if (isr_is_empty(&slots[i])) continue;
        int limit = bl_limit(&slots[i]);
        if (limit < 1) limit = 1;
        fullness += (float)slots[i].count / (float)limit;
        occupied++;
    }
    fullness /= (float)BREWING_LIVE_SLOTS;
    return (int)floorf(fullness * 14.0f) + (occupied > 0 ? 1 : 0);
}
