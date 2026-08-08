#ifndef MAGMA_GAME_FURNACE_LIVE_H
#define MAGMA_GAME_FURNACE_LIVE_H

#include "furnace_full_tick.h"
#include "items_core.h"

enum FurnaceLiveSlot {
    FURNACE_LIVE_SLOT_INPUT = 0,
    FURNACE_LIVE_SLOT_FUEL = 1,
    FURNACE_LIVE_SLOT_OUTPUT = 2,
    FURNACE_LIVE_SLOT_COUNT = 3
};

/* Plain live state. Recipe lookup is a shared constant-time CPU/CUDA function. */
typedef struct {
    SRStack input;
    SRStack fuel;
    SRStack output;
    /* Complete ItemStack payloads are cold sidecars for the compact smelting
     * kernel slots above.  item/count/meta are synchronized by the helpers;
     * arbitrary tags never enlarge the shared CPU/CUDA SRStack. */
    ICStack exact[FURNACE_LIVE_SLOT_COUNT];
    i32 burn_time;
    i32 current_burn_time;
    i32 cook_time;
    i32 total_cook;
} FurnaceLive;

void furnace_live_init(FurnaceLive *furnace);

/* Returns the number of items inserted. The output slot is extraction-only. */
int furnace_live_insert(FurnaceLive *furnace, int slot, SRStack stack);

/* Returns an empty stack for an invalid slot, empty slot, or non-positive amount. */
SRStack furnace_live_extract(FurnaceLive *furnace, int slot, int amount);

ICStack furnace_live_get_ic(const FurnaceLive *furnace, int slot);
void furnace_live_set_ic(FurnaceLive *furnace, int slot, ICStack stack);
int furnace_live_insert_ic(FurnaceLive *furnace, int slot, ICStack stack);
ICStack furnace_live_extract_ic(FurnaceLive *furnace, int slot, int amount);

/* Advances exactly one TileEntityFurnace server tick. */
void furnace_live_tick(FurnaceLive *furnace);

/* Container.calcRedstoneFromInventory for the three furnace slots. */
int furnace_live_comparator_strength(const FurnaceLive *furnace);

#endif /* MAGMA_GAME_FURNACE_LIVE_H */
