#include "game/runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "no-AI mob check failed at line %d: %s\n", \
            __LINE__, #c); return 1; } } while (0)

typedef struct {
    const char *name;
    int type;
    float health;
    int fire;
} Fixture;

static const Fixture fixtures[] = {
    {"Z", GM_MOB_ZOMBIE, 20.0F, -1},
    {"K", GM_MOB_SKELETON, 20.0F, -1},
    {"W", GM_MOB_WITHER_SKELETON, 20.0F, 0},
    {"C", GM_MOB_CREEPER, 20.0F, -1},
    {"S", GM_MOB_SPIDER, 16.0F, -1},
    {"V", GM_MOB_CAVE_SPIDER, 12.0F, -1},
    {"E", GM_MOB_ENDERMAN, 40.0F, -1},
    {"B", GM_MOB_BLAZE, 20.0F, 0},
    {"H", GM_MOB_GHAST, 10.0F, -1},
    {"T", GM_MOB_WITCH, 26.0F, -1},
    {"I", GM_MOB_VINDICATOR, 24.0F, -1},
    {"A", GM_MOB_EVOKER, 24.0F, -1},
    {"X", GM_MOB_VEX, 14.0F, 0},
    {"G", GM_MOB_GUARDIAN, 30.0F, -1},
    {"L", GM_MOB_ELDER_GUARDIAN, 80.0F, -1},
    {"R", GM_MOB_ZOMBIE_VILLAGER, 20.0F, -1},
    {"P", GM_MOB_PIGMAN, 20.0F, 0},
    {"F", GM_MOB_SILVERFISH, 8.0F, -1},
    {"O", GM_MOB_COW, 10.0F, -1},
};

static unsigned fbits(float value) {
    union { float f; uint32_t u; } bits = {value};
    return bits.u;
}

static unsigned long long dbits(double value) {
    union { double d; uint64_t u; } bits = {value};
    return (unsigned long long)bits.u;
}

static int run(const Fixture *fixture, int living_sound_time) {
    GmRuntime runtime;
    GmConfig config;
    GmAction idle;
    char error[256];
    gm_config_defaults(&config);
    config.seed = 0;
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.mobs = 0;
    config.weather = 0;
    CHECK(gm_runtime_init(&runtime, &config, error, sizeof error));
    gm_runtime_set_time(&runtime, 18000);
    gm_runtime_set_pose(&runtime, 100.5, 220.0, 100.5, 0.0F, 0.0F);
    CHECK(gm_runtime_spawn_mob_fixture(
        &runtime, fixture->type, 101, 0.5, 220.0, 0.5,
        0.125, fixture->type == GM_MOB_BLAZE ? -0.25 : 0.25,
        -0.0625, 37.0F, fixture->health, 1, 0, 0, 0));
    CHECK(gm_runtime_restore_no_ai_mob_state(
        &runtime, 101, 300, fixture->fire, 0, 1.25F, 0, 19,
        living_sound_time, 0.0F, UINT64_C(0x123456789abc), 0, 0.0));
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    for (int tick = 0; tick < 3; ++tick) gm_runtime_tick(&runtime, idle);
    int slot = gm_mobs_find_slot_by_eid(&runtime.mobs, 101);
    CHECK(slot > 0);
    const EwStore *state = runtime.mobs.current
        ? &runtime.mobs.b : &runtime.mobs.a;
    printf("N%s%d %d %d %012llx %016llx %016llx %016llx %d %d %08x %d %08x\n",
        fixture->name, living_sound_time == 1000 ? 1 : 0,
        runtime.mobs.entity_ticks_existed[slot],
        runtime.mobs.entity_living_sound_time[slot],
        (unsigned long long)runtime.mobs.entity_random[slot].random.seed,
        dbits(state->vx[slot]), dbits(state->vy[slot]),
        dbits(state->vz[slot]), runtime.mobs.entity_air[slot],
        runtime.mobs.fire_ticks[slot],
        fbits(runtime.mobs.entity_fall_distance[slot]),
        state->on_ground[slot] ? 1 : 0, fbits(state->health[slot]));
    gm_runtime_destroy(&runtime);
    return 0;
}

int main(void) {
    for (size_t index = 0;
            index < sizeof fixtures / sizeof fixtures[0]; ++index) {
        CHECK(run(&fixtures[index], 0) == 0);
        CHECK(run(&fixtures[index], 1000) == 0);
    }
    return 0;
}
