#include <stdio.h>

#include "furnace_live.h"

static int checks;
static int failures;

#define CHECK(condition) do {                                                       \
    ++checks;                                                                       \
    if (!(condition)) {                                                             \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,          \
                #condition);                                                        \
        ++failures;                                                                 \
    }                                                                               \
} while (0)

static int stack_is(SRStack stack, i32 item, i32 count, i32 meta) {
    return stack.item == item && stack.count == count && stack.meta == meta;
}

static void load_iron_and_coal(FurnaceLive *furnace, int ore_count, int coal_count) {
    furnace_live_init(furnace);
    CHECK(furnace_live_insert(furnace, FURNACE_LIVE_SLOT_INPUT,
                              sr_mk(BLK_IRON_ORE, ore_count, 7)) == ore_count);
    CHECK(furnace_live_insert(furnace, FURNACE_LIVE_SLOT_FUEL,
                              sr_mk(SR_COAL, coal_count, 0)) == coal_count);
}

static void test_init_and_slot_rules(void) {
    FurnaceLive furnace;
    SRStack extracted;

    furnace_live_init(&furnace);
    CHECK(stack_is(furnace.input, SR_AIR, 0, 0));
    CHECK(stack_is(furnace.fuel, SR_AIR, 0, 0));
    CHECK(stack_is(furnace.output, SR_AIR, 0, 0));
    CHECK(furnace.burn_time == 0);
    CHECK(furnace.current_burn_time == 0);
    CHECK(furnace.cook_time == 0);
    CHECK(furnace.total_cook == TE_COOK_TICKS);

    CHECK(furnace_live_insert(&furnace, -1, sr_mk(BLK_IRON_ORE, 1, 0)) == 0);
    CHECK(furnace_live_insert(&furnace, FURNACE_LIVE_SLOT_COUNT,
                              sr_mk(BLK_IRON_ORE, 1, 0)) == 0);
    CHECK(furnace_live_insert(&furnace, FURNACE_LIVE_SLOT_OUTPUT,
                              sr_mk(SR_IRON_INGOT, 1, 0)) == 0);
    extracted = furnace_live_extract(&furnace, FURNACE_LIVE_SLOT_COUNT, 1);
    CHECK(stack_is(extracted, SR_AIR, 0, 0));
}

static void test_insert_limits_and_metadata(void) {
    FurnaceLive furnace;

    furnace_live_init(&furnace);
    CHECK(furnace_live_insert(&furnace, FURNACE_LIVE_SLOT_INPUT,
                              sr_mk(BLK_IRON_ORE, 80, 7)) == FFT_STACK_LIMIT);
    CHECK(stack_is(furnace.input, BLK_IRON_ORE, FFT_STACK_LIMIT, 7));
    CHECK(furnace_live_insert(&furnace, FURNACE_LIVE_SLOT_INPUT,
                              sr_mk(BLK_IRON_ORE, 1, 7)) == 0);
    CHECK(furnace_live_insert(&furnace, FURNACE_LIVE_SLOT_INPUT,
                              sr_mk(BLK_GOLD_ORE, 1, 7)) == 0);

    CHECK(furnace_live_insert(&furnace, FURNACE_LIVE_SLOT_FUEL,
                              sr_mk(SR_LAVA_BUCKET, 2, 4)) == 1);
    CHECK(stack_is(furnace.fuel, SR_LAVA_BUCKET, 1, 4));
    CHECK(furnace_live_insert(&furnace, FURNACE_LIVE_SLOT_FUEL,
                              sr_mk(SR_LAVA_BUCKET, 1, 4)) == 0);
}

static void test_exact_iron_smelt_ticks(void) {
    FurnaceLive furnace;
    int tick;

    load_iron_and_coal(&furnace, 1, 1);
    for (tick = 1; tick <= TE_COOK_TICKS; ++tick) {
        furnace_live_tick(&furnace);
        CHECK(furnace.burn_time == TE_COAL_BURN + 1 - tick);
        CHECK(furnace.current_burn_time == TE_COAL_BURN);
        CHECK(furnace.total_cook == TE_COOK_TICKS);
        CHECK(stack_is(furnace.fuel, SR_AIR, 0, 0));
        if (tick < TE_COOK_TICKS) {
            CHECK(furnace.cook_time == tick);
            CHECK(stack_is(furnace.input, BLK_IRON_ORE, 1, 7));
            CHECK(stack_is(furnace.output, SR_AIR, 0, 0));
        } else {
            CHECK(furnace.cook_time == 0);
            CHECK(stack_is(furnace.input, SR_AIR, 0, 0));
            CHECK(stack_is(furnace.output, SR_IRON_INGOT, 1, 0));
        }
    }
}

static void test_invalid_fuel(void) {
    FurnaceLive furnace;

    furnace_live_init(&furnace);
    CHECK(furnace_live_insert(&furnace, FURNACE_LIVE_SLOT_INPUT,
                              sr_mk(BLK_IRON_ORE, 1, 2)) == 1);
    CHECK(furnace_live_insert(&furnace, FURNACE_LIVE_SLOT_FUEL,
                              sr_mk(SR_DIAMOND, 1, 9)) == 0);
    furnace.fuel = sr_mk(SR_DIAMOND, 1, 9);
    furnace_live_tick(&furnace);
    CHECK(stack_is(furnace.fuel, SR_DIAMOND, 1, 9));
    CHECK(furnace.burn_time == 0);
    CHECK(furnace.current_burn_time == 0);
    CHECK(furnace.cook_time == 0);
    CHECK(stack_is(furnace.input, BLK_IRON_ORE, 1, 2));
}

static void test_blocked_output(void) {
    FurnaceLive furnace;

    load_iron_and_coal(&furnace, 1, 1);
    furnace.output = sr_mk(SR_GOLD_INGOT, 1, 3);
    furnace_live_tick(&furnace);
    CHECK(stack_is(furnace.input, BLK_IRON_ORE, 1, 7));
    CHECK(stack_is(furnace.fuel, SR_COAL, 1, 0));
    CHECK(stack_is(furnace.output, SR_GOLD_INGOT, 1, 3));
    CHECK(furnace.burn_time == 0);
    CHECK(furnace.cook_time == 0);

    furnace.output = sr_mk(SR_IRON_INGOT, FFT_STACK_LIMIT, 0);
    furnace_live_tick(&furnace);
    CHECK(stack_is(furnace.input, BLK_IRON_ORE, 1, 7));
    CHECK(stack_is(furnace.fuel, SR_COAL, 1, 0));
    CHECK(stack_is(furnace.output, SR_IRON_INGOT, FFT_STACK_LIMIT, 0));
    CHECK(furnace.burn_time == 0);
    CHECK(furnace.cook_time == 0);
}

static void test_fuel_consumption_and_extraction(void) {
    FurnaceLive furnace;
    SRStack extracted;

    load_iron_and_coal(&furnace, 2, 2);
    furnace_live_tick(&furnace);
    CHECK(stack_is(furnace.fuel, SR_COAL, 1, 0));
    CHECK(furnace.burn_time == TE_COAL_BURN);
    CHECK(furnace.current_burn_time == TE_COAL_BURN);
    CHECK(furnace.cook_time == 1);

    extracted = furnace_live_extract(&furnace, FURNACE_LIVE_SLOT_INPUT, 1);
    CHECK(stack_is(extracted, BLK_IRON_ORE, 1, 7));
    CHECK(stack_is(furnace.input, BLK_IRON_ORE, 1, 7));
    extracted = furnace_live_extract(&furnace, FURNACE_LIVE_SLOT_FUEL, 5);
    CHECK(stack_is(extracted, SR_COAL, 1, 0));
    CHECK(stack_is(furnace.fuel, SR_AIR, 0, 0));

    while (furnace.output.count == 0) furnace_live_tick(&furnace);
    extracted = furnace_live_extract(&furnace, FURNACE_LIVE_SLOT_OUTPUT, 1);
    CHECK(stack_is(extracted, SR_IRON_INGOT, 1, 0));
    CHECK(stack_is(furnace.output, SR_AIR, 0, 0));
    CHECK(stack_is(furnace_live_extract(&furnace, FURNACE_LIVE_SLOT_INPUT, 0),
                   SR_AIR, 0, 0));
}

int main(void) {
    test_init_and_slot_rules();
    test_insert_limits_and_metadata();
    test_exact_iron_smelt_ticks();
    test_invalid_fuel();
    test_blocked_output();
    test_fuel_consumption_and_extraction();

    if (failures != 0) {
        fprintf(stderr, "furnace_live: FAIL (%d/%d checks failed)\n", failures, checks);
        return 1;
    }
    printf("furnace_live: PASS (%d checks)\n", checks);
    return 0;
}
