#ifndef MAGMA_GAME_BREWING_LIVE_H
#define MAGMA_GAME_BREWING_LIVE_H

#include "items_core.h"

enum {
    BREWING_LIVE_SLOTS = 5,
    BREWING_LIVE_POTION0 = 0,
    BREWING_LIVE_INGREDIENT = 3,
    BREWING_LIVE_FUEL = 4
};

typedef struct {
    int brew_time;
    int fuel;
    int ingredient_id;
    int bottle_bits;
} BrewingLiveState;

void brewing_live_init(ICStack slots[BREWING_LIVE_SLOTS],
                       BrewingLiveState *state);

/* SlotBrewingStand validity. Potion slots accept one potion/splash/lingering
 * stack, ingredient accepts every PotionHelper reagent, fuel accepts blaze
 * powder. Returns the count moved. */
int brewing_live_insert(ICStack slots[BREWING_LIVE_SLOTS], int slot,
                        ICStack stack);
ICStack brewing_live_extract(ICStack slots[BREWING_LIVE_SLOTS], int slot,
                             int amount);

/* Advance one server tile tick. Returns TB_TICK_* flags. A stacked dragon
 * breath brew reports the number of glass-bottle EntityItems to spawn. */
int brewing_live_tick(ICStack slots[BREWING_LIVE_SLOTS],
                      BrewingLiveState *state, int *glass_bottle_drops);

int brewing_live_bottle_bits(const ICStack slots[BREWING_LIVE_SLOTS]);
int brewing_live_comparator_strength(
    const ICStack slots[BREWING_LIVE_SLOTS]);
int brewing_live_slot_valid(int slot, const ICStack *stack);

#endif /* MAGMA_GAME_BREWING_LIVE_H */
