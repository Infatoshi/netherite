#include <stddef.h>
#include <math.h>

#include "furnace_live.h"
#include "inventory_stack_rules.h"
#include "tile_entity_chest.h"

static SRStack *furnace_live_slot(FurnaceLive *furnace, int slot) {
    if (furnace == NULL) return NULL;
    switch (slot) {
        case FURNACE_LIVE_SLOT_INPUT: return &furnace->input;
        case FURNACE_LIVE_SLOT_FUEL: return &furnace->fuel;
        case FURNACE_LIVE_SLOT_OUTPUT: return &furnace->output;
        default: return NULL;
    }
}

static const SRStack *furnace_live_slot_const(
        const FurnaceLive *furnace, int slot) {
    return furnace_live_slot((FurnaceLive *)furnace, slot);
}

ICStack furnace_live_get_ic(const FurnaceLive *furnace, int slot) {
    const SRStack *compact = furnace_live_slot_const(furnace, slot);
    ICStack result;
    if (!compact || sr_isEmpty(*compact)) return ic_empty();
    result = furnace->exact[slot];
    if (result.item != compact->item || result.meta != compact->meta)
        result = ic_mk(compact->item, compact->count, compact->meta);
    result.item = compact->item;
    result.count = compact->count;
    result.meta = compact->meta;
    return result;
}

void furnace_live_set_ic(FurnaceLive *furnace, int slot, ICStack stack) {
    SRStack *compact = furnace_live_slot(furnace, slot);
    if (!compact) return;
    if (stack.item <= 0 || stack.count <= 0) {
        *compact = sr_empty();
        furnace->exact[slot] = ic_empty();
        return;
    }
    *compact = sr_mk(stack.item, stack.count, stack.meta);
    furnace->exact[slot] = stack;
}

static int furnace_live_stack_limit(SRStack stack) {
    return isr_max_stack_size(stack.item, stack.meta);
}

void furnace_live_init(FurnaceLive *furnace) {
    if (furnace == NULL) return;
    furnace->input = sr_empty();
    furnace->fuel = sr_empty();
    furnace->output = sr_empty();
    for (int slot = 0; slot < FURNACE_LIVE_SLOT_COUNT; ++slot)
        furnace->exact[slot] = ic_empty();
    furnace->burn_time = 0;
    furnace->current_burn_time = 0;
    furnace->cook_time = 0;
    furnace->total_cook = fft_get_cook_time(&furnace->input);
}

int furnace_live_insert(FurnaceLive *furnace, int slot, SRStack stack) {
    return furnace_live_insert_ic(
        furnace, slot,
        sr_isEmpty(stack) ? ic_empty()
                          : ic_mk(stack.item, stack.count, stack.meta));
}

int furnace_live_insert_ic(FurnaceLive *furnace, int slot, ICStack stack) {
    SRStack *dst;
    int limit;
    int moved;

    if (slot == FURNACE_LIVE_SLOT_OUTPUT
            || stack.item <= 0 || stack.count <= 0) return 0;
    dst = furnace_live_slot(furnace, slot);
    if (dst == NULL) return 0;
    if (slot == FURNACE_LIVE_SLOT_FUEL
            && sr_getItemBurnTime(
                sr_mk(stack.item, stack.count, stack.meta)) <= 0) return 0;

    limit = furnace_live_stack_limit(
        sr_mk(stack.item, stack.count, stack.meta));
    if (!sr_isEmpty(*dst)) {
        ICStack existing = furnace_live_get_ic(furnace, slot);
        if (!ic_stack_equal(&existing, &stack)) return 0;
        limit -= dst->count;
    }
    if (limit <= 0) return 0;

    moved = stack.count;
    if (moved > limit) moved = limit;
    if (sr_isEmpty(*dst)) {
        ICStack inserted = stack;
        inserted.count = moved;
        furnace_live_set_ic(furnace, slot, inserted);
    } else {
        dst->count += moved;
        furnace->exact[slot].count = dst->count;
    }
    return moved;
}

SRStack furnace_live_extract(FurnaceLive *furnace, int slot, int amount) {
    ICStack exact = furnace_live_extract_ic(furnace, slot, amount);
    return exact.item <= 0 || exact.count <= 0
        ? sr_empty() : sr_mk(exact.item, exact.count, exact.meta);
}

ICStack furnace_live_extract_ic(FurnaceLive *furnace, int slot, int amount) {
    SRStack *src = furnace_live_slot(furnace, slot);
    ICStack result;
    int taken;

    if (src == NULL || amount <= 0 || sr_isEmpty(*src)) return ic_empty();
    taken = amount;
    if (taken > src->count) taken = src->count;
    result = furnace_live_get_ic(furnace, slot);
    result.count = taken;
    src->count -= taken;
    if (src->count <= 0) furnace_live_set_ic(furnace, slot, ic_empty());
    else furnace->exact[slot].count = src->count;
    return result;
}

void furnace_live_tick(FurnaceLive *furnace) {
    FftFurnace kernel;
    ICStack before[FURNACE_LIVE_SLOT_COUNT];

    if (furnace == NULL) return;
    for (int slot = 0; slot < FURNACE_LIVE_SLOT_COUNT; ++slot)
        before[slot] = furnace_live_get_ic(furnace, slot);
    kernel.slot0 = furnace->input;
    kernel.slot1 = furnace->fuel;
    kernel.slot2 = furnace->output;
    kernel.burn_time = furnace->burn_time;
    kernel.current_burn_time = furnace->current_burn_time;
    kernel.cook_time = furnace->cook_time;
    kernel.total_cook = furnace->total_cook;
    fft_tick(&kernel);

    furnace->input = kernel.slot0;
    furnace->fuel = kernel.slot1;
    furnace->output = kernel.slot2;
    {
        SRStack *after[FURNACE_LIVE_SLOT_COUNT] = {
            &furnace->input, &furnace->fuel, &furnace->output,
        };
        for (int slot = 0; slot < FURNACE_LIVE_SLOT_COUNT; ++slot) {
            if (sr_isEmpty(*after[slot])) {
                furnace->exact[slot] = ic_empty();
            } else if (before[slot].item == after[slot]->item
                    && before[slot].meta == after[slot]->meta) {
                before[slot].count = after[slot]->count;
                furnace->exact[slot] = before[slot];
            } else {
                furnace->exact[slot] = ic_mk(
                    after[slot]->item, after[slot]->count,
                    after[slot]->meta);
            }
        }
    }
    furnace->burn_time = kernel.burn_time;
    furnace->current_burn_time = kernel.current_burn_time;
    furnace->cook_time = kernel.cook_time;
    furnace->total_cook = kernel.total_cook;
}

int furnace_live_comparator_strength(const FurnaceLive *furnace) {
    const SRStack *slots[FURNACE_LIVE_SLOT_COUNT];
    float fullness = 0.0f;
    int occupied = 0;
    if (furnace == NULL) return 0;
    slots[0] = &furnace->input;
    slots[1] = &furnace->fuel;
    slots[2] = &furnace->output;
    for (int slot = 0; slot < FURNACE_LIVE_SLOT_COUNT; ++slot) {
        const SRStack *stack = slots[slot];
        int limit;
        if (sr_isEmpty(*stack)) continue;
        limit = tec_max_stack_size(stack->item);
        if (limit > FFT_STACK_LIMIT) limit = FFT_STACK_LIMIT;
        if (limit < 1) limit = 1;
        fullness += (float)stack->count / (float)limit;
        occupied++;
    }
    fullness /= (float)FURNACE_LIVE_SLOT_COUNT;
    return (int)floorf(fullness * 14.0f) + (occupied > 0 ? 1 : 0);
}
