#include <stddef.h>
#include <math.h>

#include "furnace_live.h"
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

static int furnace_live_stack_limit(SRStack stack) {
    /* Lava buckets are the only non-stackable item in smelting_recipes' KEEP set. */
    return stack.item == SR_LAVA_BUCKET ? 1 : FFT_STACK_LIMIT;
}

void furnace_live_init(FurnaceLive *furnace) {
    if (furnace == NULL) return;
    furnace->input = sr_empty();
    furnace->fuel = sr_empty();
    furnace->output = sr_empty();
    furnace->burn_time = 0;
    furnace->current_burn_time = 0;
    furnace->cook_time = 0;
    furnace->total_cook = fft_get_cook_time(&furnace->input);
}

int furnace_live_insert(FurnaceLive *furnace, int slot, SRStack stack) {
    SRStack *dst;
    int limit;
    int moved;

    if (slot == FURNACE_LIVE_SLOT_OUTPUT || sr_isEmpty(stack)) return 0;
    dst = furnace_live_slot(furnace, slot);
    if (dst == NULL) return 0;
    if (slot == FURNACE_LIVE_SLOT_FUEL && sr_getItemBurnTime(stack) <= 0) return 0;

    limit = furnace_live_stack_limit(stack);
    if (!sr_isEmpty(*dst)) {
        if (dst->item != stack.item || dst->meta != stack.meta) return 0;
        limit -= dst->count;
    }
    if (limit <= 0) return 0;

    moved = stack.count;
    if (moved > limit) moved = limit;
    if (sr_isEmpty(*dst)) *dst = sr_mk(stack.item, moved, stack.meta);
    else dst->count += moved;
    return moved;
}

SRStack furnace_live_extract(FurnaceLive *furnace, int slot, int amount) {
    SRStack *src = furnace_live_slot(furnace, slot);
    SRStack result;
    int taken;

    if (src == NULL || amount <= 0 || sr_isEmpty(*src)) return sr_empty();
    taken = amount;
    if (taken > src->count) taken = src->count;
    result = sr_mk(src->item, taken, src->meta);
    src->count -= taken;
    if (src->count <= 0) *src = sr_empty();
    return result;
}

void furnace_live_tick(FurnaceLive *furnace) {
    FftFurnace kernel;

    if (furnace == NULL) return;
    kernel.slot0 = furnace->input;
    kernel.slot1 = furnace->fuel;
    kernel.slot2 = furnace->output;
    kernel.burn_time = furnace->burn_time;
    kernel.current_burn_time = furnace->current_burn_time;
    kernel.cook_time = furnace->cook_time;
    kernel.total_cook = furnace->total_cook;
    kernel.nrecipes = sr_build(kernel.recipes);

    fft_tick(&kernel);

    furnace->input = kernel.slot0;
    furnace->fuel = kernel.slot1;
    furnace->output = kernel.slot2;
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
