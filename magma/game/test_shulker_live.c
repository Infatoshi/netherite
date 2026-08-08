#include "game/runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(c, m) do { if (!(c)) { \
    fprintf(stderr, "FAIL: %s\n", (m)); return 0; } } while (0)

static unsigned int float_bits(float value) {
    unsigned int bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static unsigned long long double_bits(double value) {
    unsigned long long bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static int init_runtime(GmRuntime *r) {
    GmConfig cfg;
    char err[256];
    gm_config_defaults(&cfg);
    cfg.seed = 0;
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.view_distance = 1;
    cfg.mobs = 0;
    cfg.weather = 0;
    if (!gm_runtime_init(r, &cfg, err, sizeof err)) {
        fprintf(stderr, "FAIL: %s\n", err);
        return 0;
    }
    return 1;
}

static int attack_trace(void) {
    GmRuntime r;
    GmRuntimeShulker shulker;
    CHECK(init_runtime(&r), "initialize attack trace");
    gm_world_set_block(r.world, 0, 63, 0, 201);
    gm_runtime_set_pose(&r, 8.5, 64.0, 0.5, 0.0F, 0.0F);
    CHECK(gm_runtime_spawn_shulker_fixture(
              &r, 1, 0, 64, 0, 0, UINT64_C(0x123456789ab)),
          "spawn attack shulker");
    for (int tick = 1; tick <= 80; ++tick) {
        int bullets_before = gm_runtime_shulker_bullet_count(&r);
        gm_runtime_tick_shulkers_fixture(&r);
        CHECK(gm_runtime_shulker_get(&r, 0, &shulker),
              "attack shulker remains live");
        printf("A %d %d %08x %08x %012llx\n", tick,
               shulker.peek_tick, float_bits(shulker.peek_amount),
               float_bits(shulker.health),
               (unsigned long long)shulker.random_seed48);
        if (gm_runtime_shulker_bullet_count(&r) > bullets_before)
            printf("F %d\n", tick);
    }
    gm_runtime_destroy(&r);
    return 1;
}

static int bullet_trace(void) {
    GmRuntime r;
    GmRuntimeShulkerBullet bullet;
    CHECK(init_runtime(&r), "initialize bullet trace");
    gm_runtime_set_pose(&r, 6.5, 66.0, 4.5, 0.0F, 0.0F);
    CHECK(gm_runtime_spawn_shulker_fixture(
              &r, 1, 0, 64, 0, 0, UINT64_C(1)),
          "spawn bullet owner");
    CHECK(gm_runtime_spawn_shulker_bullet_fixture(
              &r, 2, 1, UINT64_C(0x102030405060)),
          "spawn deterministic bullet");
    for (int tick = 1; tick <= 24; ++tick) {
        gm_runtime_tick_shulkers_fixture(&r);
        CHECK(gm_runtime_shulker_bullet_get(&r, 0, &bullet),
              "guided bullet remains live");
        printf("B %d %016llx %016llx %016llx %016llx %016llx %016llx %d %d\n",
               tick, double_bits(bullet.x), double_bits(bullet.y),
               double_bits(bullet.z), double_bits(bullet.vx),
               double_bits(bullet.vy), double_bits(bullet.vz),
               bullet.steps, bullet.direction);
    }
    gm_runtime_destroy(&r);
    return 1;
}

static int teleport_trace(void) {
    GmRuntime r;
    GmRuntimeShulker shulker;
    CHECK(init_runtime(&r), "initialize teleport trace");
    for (int x = -9; x <= 9; ++x)
        for (int y = 55; y <= 72; ++y)
            for (int z = -9; z <= 9; ++z)
                gm_world_set_block(r.world, x, y, z, 0);
    for (int x = -9; x <= 9; ++x)
        for (int z = -9; z <= 9; ++z)
            gm_world_set_block(r.world, x, 62, z, 121);
    CHECK(gm_runtime_spawn_shulker_fixture(
              &r, 1, 0, 64, 0, 0, UINT64_C(0x314159265358)),
          "spawn teleport shulker");
    gm_runtime_tick_shulkers_fixture(&r);
    CHECK(gm_runtime_shulker_get(&r, 0, &shulker),
          "teleported shulker remains live");
    printf("T %d %d %d %d %d %012llx\n",
           shulker.x, shulker.y, shulker.z, shulker.face,
           shulker.peek_tick,
           (unsigned long long)shulker.random_seed48);
    gm_runtime_destroy(&r);
    return 1;
}

int main(void) {
    if (!attack_trace() || !bullet_trace() || !teleport_trace()) return 1;
    return 0;
}
