#include "game/village_live.h"

#include <inttypes.h>
#include <stdio.h>

typedef struct {
    int villager_count;
    int golem_count;
    int keep_doors;
    int spawned;
    int spawn_x, spawn_y, spawn_z;
} Fixture;

static int is_wood_door(void *opaque, int x, int y, int z) {
    (void)x; (void)y; (void)z;
    return ((Fixture *)opaque)->keep_doors;
}

static int count_villagers(
        void *opaque, int x, int y, int z, int radius) {
    (void)x; (void)y; (void)z; (void)radius;
    return ((Fixture *)opaque)->villager_count;
}

static int count_golems(
        void *opaque, int x, int y, int z, int radius) {
    (void)x; (void)y; (void)z; (void)radius;
    return ((Fixture *)opaque)->golem_count;
}

static int area_clear(
        void *opaque, int x, int y, int z, int sx, int sy, int sz) {
    (void)opaque; (void)x; (void)z; (void)sx; (void)sy; (void)sz;
    return y == 64;
}

static void spawn_golem(void *opaque, int x, int y, int z) {
    Fixture *fixture = opaque;
    ++fixture->spawned;
    fixture->spawn_x = x;
    fixture->spawn_y = y;
    fixture->spawn_z = z;
}

int main(void) {
    const uint64_t first_most = UINT64_C(0x0123456789abcdef);
    const uint64_t first_least = UINT64_C(0x0fedcba987654321);
    const uint64_t second_most = UINT64_C(0x1111222233334444);
    const uint64_t second_least = UINT64_C(0x5555666677778888);
    GmVillageState state, saved;
    Fixture fixture = {.villager_count = 20, .keep_doors = 1};
    GmVillageStateAccess access = {
        &fixture, is_wood_door, count_villagers, count_golems,
        area_clear, spawn_golem, NULL
    };
    JavaRandom random;
    gm_village_state_init(&state);
    for (int i = 0; i < 21; ++i)
        if (!gm_village_state_add_door(
                &state, i % 7 - 3, 64, i / 7 - 1,
                (i & 1) == 0 ? 2 : -2, 0, 100))
            return 1;
    state.doors[0].restriction = 1;
    (void)gm_village_state_modify_reputation(
        &state, first_most, first_least, -40);
    (void)gm_village_state_modify_reputation(
        &state, first_most, first_least, 7);
    (void)gm_village_state_modify_reputation(
        &state, second_most, second_least, 12);
    gm_village_state_default_reputation(&state, 5);
    state.tick_counter = 100;
    if (!gm_village_state_add_or_renew_aggressor(&state, 41)
            || !gm_village_state_add_or_renew_aggressor(&state, 42))
        return 1;
    gm_village_state_end_mating(&state);
    if (!gm_village_state_persist(&saved, &state)) return 1;
    if (saved.aggressor_count != 0) return 1;
    state = saved;
    printf("S %d %d %d %d %d %d %d %d %d %d %d\n",
        state.center_x, state.center_y, state.center_z, state.radius,
        state.door_count,
        state.tick_counter - state.last_add_door_timestamp,
        gm_village_state_reputation(&state, first_most, first_least),
        gm_village_state_reputation(&state, second_most, second_least),
        gm_village_state_reputation_too_low(
            &state, first_most, first_least),
        gm_village_state_is_mating(&state), state.doors[0].restriction);

    jrand_set(&random, 3107);
    if (gm_village_state_tick(&state, 160, &random, &access) != 1
            || fixture.spawned != 1)
        return 1;
    printf("T %d %d %d %d %d %" PRIu64 " %d\n",
        state.num_villagers, state.num_golems,
        fixture.spawn_x, fixture.spawn_y, fixture.spawn_z,
        random.seed, gm_village_state_is_mating(&state));

    fixture.keep_doors = 0;
    (void)gm_village_state_tick(&state, 1301, &random, &access);
    fixture.villager_count = 0;
    (void)gm_village_state_tick(&state, 1320, &random, &access);
    printf("R %d %d %d %d %d %d %d\n",
        state.door_count, state.center_x, state.center_y, state.center_z,
        state.radius,
        gm_village_state_reputation(&state, first_most, first_least),
        state.reputation_count);
    (void)gm_village_state_tick(&state, 3700, &random, &access);
    printf("M %d %" PRIu64 "\n",
        gm_village_state_is_mating(&state), random.seed);

    GmVillageState live, live_saved;
    gm_village_state_init(&live);
    JavaRandom live_random;
    jrand_set(&live_random, 0);
    (void)gm_village_state_tick(&live, 100, &live_random, &access);
    if (!gm_village_state_add_or_renew_aggressor(&live, 41)
            || !gm_village_state_add_or_renew_aggressor(&live, 42))
        return 1;
    int before = gm_village_state_aggressor_count(&live);
    (void)gm_village_state_tick(&live, 350, &live_random, &access);
    if (!gm_village_state_add_or_renew_aggressor(&live, 41)) return 1;
    (void)gm_village_state_tick(&live, 401, &live_random, &access);
    if (!gm_village_state_persist(&live_saved, &live)) return 1;
    printf("A %d %d %d\n", before,
        gm_village_state_aggressor_count(&live),
        gm_village_state_aggressor_count(&live_saved));
    return 0;
}
