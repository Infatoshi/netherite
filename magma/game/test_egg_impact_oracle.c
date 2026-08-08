#include "game/runtime.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t double_bits(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static uint32_t float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static void print_double_bits3(double x, double y, double z) {
    printf("[\"%016" PRIx64 "\",\"%016" PRIx64
           "\",\"%016" PRIx64 "\"]",
           double_bits(x), double_bits(y), double_bits(z));
}

int main(int argc, char **argv) {
    GmConfig config;
    GmRuntime runtime;
    GmRuntimeProjectile *egg;
    const EwStore *store;
    char error[256];
    uint64_t egg_seed, entity_seed, uuid_seed;
    double x, y, z;
    float yaw;
    int next_id, hatch_count;
    if (argc != 9) return 2;
    egg_seed = strtoull(argv[1], NULL, 10);
    entity_seed = strtoull(argv[2], NULL, 10);
    uuid_seed = strtoull(argv[3], NULL, 10);
    next_id = atoi(argv[4]);
    x = strtod(argv[5], NULL);
    y = strtod(argv[6], NULL);
    z = strtod(argv[7], NULL);
    yaw = strtof(argv[8], NULL);
    if (egg_seed >= (UINT64_C(1) << 48)
            || entity_seed >= (UINT64_C(1) << 48)
            || uuid_seed >= (UINT64_C(1) << 48)
            || next_id <= 0 || next_id > INT32_MAX - 4)
        return 2;
    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.seed = 0;
    if (!gm_runtime_init(&runtime, &config, error, sizeof error)) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    gm_runtime_set_entity_id_cursor(&runtime, next_id);
    gm_runtime_set_entity_seed_generator_seed48(&runtime, entity_seed);
    gm_runtime_set_server_uuid_random_seed48(&runtime, uuid_seed);
    egg = &runtime.projectiles[0];
    memset(egg, 0, sizeof *egg);
    egg->active = 1;
    egg->type = 7;
    egg->dimension = runtime.dimension;
    egg->eid = next_id - 1;
    egg->x = x;
    egg->y = y;
    egg->z = z;
    egg->yaw = yaw;
    egg->random_seed48 = egg_seed;
    hatch_count = gm_runtime_egg_impact_now(&runtime, 0);
    if (hatch_count < 0) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    store = runtime.mobs.current ? &runtime.mobs.b : &runtime.mobs.a;
    printf("{\"ok\":true,\"egg_dead\":%s,\"egg_seed48\":%" PRIu64
           ",\"chickens\":[",
           egg->active ? "false" : "true", egg->random_seed48);
    int emitted = 0;
    for (int order = 0;
            order < runtime.loaded_entity_order_count; ++order) {
        int eid = runtime.loaded_entity_order[order];
        int slot = gm_mobs_find_slot_by_eid(&runtime.mobs, eid);
        if (slot <= 0 || !store->alive[slot]
                || store->type[slot] != EW_TYPE_CHICKEN)
            continue;
        if (emitted++) putchar(',');
        printf("{\"eid\":%d,\"position_bits\":", eid);
        print_double_bits3(
            store->x[slot], store->y[slot], store->z[slot]);
        printf(",\"motion_bits\":");
        print_double_bits3(
            store->vx[slot], store->vy[slot], store->vz[slot]);
        printf(",\"yaw_bits\":\"%08" PRIx32
               "\",\"pitch_bits\":\"00000000\""
               ",\"health_bits\":\"%08" PRIx32
               "\",\"growing_age\":%d"
               ",\"time_until_next_egg\":%d"
               ",\"seed48\":%" PRIu64
               ",\"uuid_most\":%" PRId64
               ",\"uuid_least\":%" PRId64 "}",
               float_bits(store->yaw[slot]),
               float_bits(store->health[slot]),
               runtime.mobs.growing_age[slot],
               runtime.mobs.chicken_time_until_next_egg[slot],
               runtime.mobs.entity_random[slot].random.seed,
               runtime.mobs.entity_uuid_most[slot],
               runtime.mobs.entity_uuid_least[slot]);
    }
    printf("],\"loaded_order\":[");
    int loaded = 0;
    for (int order = 0;
            order < runtime.loaded_entity_order_count; ++order) {
        int eid = runtime.loaded_entity_order[order];
        int slot = gm_mobs_find_slot_by_eid(&runtime.mobs, eid);
        if (slot <= 0 || !store->alive[slot]
                || store->type[slot] != EW_TYPE_CHICKEN)
            continue;
        if (loaded++) putchar(',');
        printf("%d", eid);
    }
    printf("],\"entity_seed48\":%" PRIu64
           ",\"server_uuid_seed48\":%" PRIu64
           ",\"next_entity_id\":%d}\n",
           runtime.entity_seed_generator_seed48,
           runtime.server_uuid_random_seed48,
           runtime.next_entity_id);
    gm_runtime_destroy(&runtime);
    return emitted == hatch_count && loaded == hatch_count ? 0 : 1;
}
