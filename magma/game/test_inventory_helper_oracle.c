#include "game/runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "inventory helper check failed at line %d: %s\n", \
            __LINE__, #condition); \
    exit(1); \
} } while (0)

static unsigned long long double_bits(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof bits);
    return (unsigned long long)bits;
}

static void run(const char *tag, int have, double next) {
    GmConfig cfg;
    GmRuntime r;
    char error[256] = {0};
    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.view_distance = 1;
    cfg.mobs = 0;
    cfg.weather = 0;
    CHECK(gm_runtime_init(&r, &cfg, error, sizeof error));
    memset(&r.entities, 0, sizeof r.entities);
    r.loaded_entity_order_count = 0;
    r.next_entity_id = 100;
    CHECK(gm_runtime_set_inventory_helper_random_state(
        &r, UINT64_C(0x123456789abc), have, next));
    CHECK(gm_runtime_set_math_random_seed48(
        &r, UINT64_C(0x0fedcba98765)));
    CHECK(gm_runtime_inventory_helper_drop_fixture(
        &r, 9, 78, 8, ic_mk(1, 64, 0)));
    for (int index = 0; index < GM_LIVE_MAX; ++index) {
        const GmLiveEnt *entity = &r.entities.ents[index];
        if (!entity->active || entity->type != 0) continue;
        printf("E %s %d %016llx %016llx %016llx %016llx %016llx %016llx\n",
            tag, entity->count,
            double_bits(entity->x), double_bits(entity->y),
            double_bits(entity->z), double_bits(entity->mx),
            double_bits(entity->my), double_bits(entity->mz));
        CHECK(entity->pickup_delay == 0 && entity->age == 0
            && entity->first_update == 1);
    }
    printf("R %s %012llx %d %016llx %012llx\n",
        tag,
        (unsigned long long)r.inventory_helper_random_seed48,
        r.inventory_helper_random_have_gaussian,
        double_bits(r.inventory_helper_random_gaussian),
        (unsigned long long)r.math_random_seed48);
    gm_runtime_destroy(&r);
}

static void full_chest_bound(void) {
    GmConfig cfg;
    GmRuntime r;
    char error[256] = {0};
    int total = 0;
    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.view_distance = 1;
    cfg.mobs = 0;
    cfg.weather = 0;
    CHECK(GM_LIVE_MAX >= 27 * 7 + 1);
    CHECK(gm_runtime_init(&r, &cfg, error, sizeof error));
    memset(&r.entities, 0, sizeof r.entities);
    r.loaded_entity_order_count = 0;
    r.next_entity_id = 100;
    CHECK(gm_runtime_set_inventory_helper_random_state(
        &r, UINT64_C(0x102030405060), 0, 0.0));
    for (int slot = 0; slot < 27; ++slot)
        CHECK(gm_runtime_inventory_helper_drop_fixture(
            &r, 9, 78, 8, ic_mk(1, 64, 0)));
    for (int index = 0; index < GM_LIVE_MAX; ++index)
        if (r.entities.ents[index].active
                && r.entities.ents[index].type == 0)
            total += r.entities.ents[index].count;
    CHECK(total == 27 * 64 && r.entities.spawn_fail_count == 0);
    gm_runtime_destroy(&r);
}

int main(void) {
    run("A", 0, 0.0);
    run("B", 1, -0.75);
    full_chest_bound();
    return 0;
}
