#include <stdio.h>

#include "game/furnace_live.h"

static void print_stack(SRStack stack) {
    if (sr_isEmpty(stack)) {
        printf("0:0:0");
    } else {
        printf("%d:%d:%d", stack.item, stack.count, stack.meta);
    }
}

static void print_state(
        const char *tag, const char *phase, const FurnaceLive *furnace) {
    printf("%s %s %d %d %d %d ", tag, phase,
        furnace->burn_time, furnace->current_burn_time,
        furnace->cook_time, furnace->total_cook);
    print_stack(furnace->input);
    putchar(' ');
    print_stack(furnace->fuel);
    putchar(' ');
    print_stack(furnace->output);
    printf(" %d:2 boundary\n", furnace->burn_time > 0 ? 62 : 61);
}

static void set_stack(
        FurnaceLive *furnace, int slot, int item, int count, int meta) {
    furnace_live_set_ic(furnace, slot,
        item > 0 && count > 0 ? ic_mk(item, count, meta) : ic_empty());
}

static void run(
        const char *tag, int burn, int cook,
        int input_item, int input_count, int input_meta,
        int fuel_item, int fuel_count, int fuel_meta,
        int output_item, int output_count, int output_meta) {
    FurnaceLive furnace;
    furnace_live_init(&furnace);
    set_stack(&furnace, 0, input_item, input_count, input_meta);
    set_stack(&furnace, 1, fuel_item, fuel_count, fuel_meta);
    set_stack(&furnace, 2, output_item, output_count, output_meta);
    furnace.burn_time = burn;
    /* TileEntityFurnace does not persist this field. readFromNBT reconstructs
     * it from the fuel stack at exactly this save boundary. */
    furnace.current_burn_time = sr_getItemBurnTime(furnace.fuel);
    furnace.cook_time = cook;
    furnace.total_cook = 200;
    print_state(tag, "B", &furnace);
    furnace_live_tick(&furnace);
    print_state(tag, "A", &furnace);
}

int main(void) {
    run("L0", 0, 0, 15, 1, 0, 263, 1, 0, 0, 0, 0);
    run("L1", 1, 17, 15, 1, 0, 263, 1, 0, 0, 0, 0);
    run("C1", 2, 199, 15, 1, 0, 0, 0, 0, 0, 0, 0);
    run("B1", 2, 199, 15, 1, 0, 0, 0, 0, 265, 64, 0);
    run("D0", 0, 5, 15, 1, 0, 0, 0, 0, 0, 0, 0);
    run("I0", 0, 5, 264, 1, 0, 263, 1, 0, 0, 0, 0);
    run("Z1", 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    run("W1", 2, 199, 19, 1, 1, 325, 1, 0, 0, 0, 0);
    return 0;
}
