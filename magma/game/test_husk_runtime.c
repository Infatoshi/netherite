#include "game/native_save.h"
#include "game/runtime.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fail;
#define CHECK(C, M) do { \
    if (!(C)) { fprintf(stderr, "FAIL: %s\n", M); fail = 1; } \
} while (0)

static int mob_slot(const GmMobLive *m, int eid) {
    const EwStore *s = m->current ? &m->b : &m->a;
    for (int slot = 1; slot < EW_MAX_ENTITIES; ++slot)
        if (s->alive[slot] && s->id[slot] == eid) return slot;
    return -1;
}

static void clean_save(const char *root) {
    char path[512];
    static const char *files[] = {
        "runtime.bin", "player_statistics.json", "manifest.bin",
        "world_dim-1.bin", "world_dim0.bin", "world_dim1.bin",
    };
    for (size_t i = 0; i < sizeof files / sizeof files[0]; ++i) {
        snprintf(path, sizeof path,
                 "%s/husk/generation-0000000000000001/%s", root, files[i]);
        (void)remove(path);
    }
    snprintf(path, sizeof path, "%s/husk/generation-0000000000000001", root);
    (void)rmdir(path);
    snprintf(path, sizeof path, "%s/husk/current", root);
    (void)remove(path);
    snprintf(path, sizeof path, "%s/husk/write.lock", root);
    (void)remove(path);
    snprintf(path, sizeof path, "%s/husk", root);
    (void)rmdir(path);
    (void)rmdir(root);
}

static int init_runtime(GmRuntime *r, GmConfig *config) {
    char error[256] = {0};
    gm_config_defaults(config);
    config->seed = 0;
    config->view_distance = 1;
    config->mobs = 0;
    config->weather = 0;
    if (!gm_runtime_init(r, config, error, sizeof error)) {
        fprintf(stderr, "FAIL: %s\n", error);
        return 0;
    }
    return 1;
}

int main(void) {
    static GmRuntime runtime;
    GmConfig config;
    GmAction idle = {.hotbar_sel = -1};

    if (!init_runtime(&runtime, &config)) return 1;
    double py = (double)gm_world_surface_y(runtime.world, 8, 8) + 1.0;
    gm_runtime_set_pose(&runtime, 8.5, py, 8.5, 0.0F, 0.0F);
    gm_runtime_set_time(&runtime, 1000);
    const int husk_day = 689000, zombie_day = 689001, stray_day = 689002;
    CHECK(gm_runtime_spawn_mob_fixture(
              &runtime, EW_TYPE_HUSK, husk_day, 8.5, py, 24.5,
              0, 0, 0, 0, 20, 0, 0, 0, 0), "spawn exposed Husk");
    CHECK(gm_runtime_spawn_mob_fixture(
              &runtime, EW_TYPE_ZOMBIE, zombie_day, 10.5, py, 24.5,
              0, 0, 0, 0, 20, 0, 0, 0, 0), "spawn exposed Zombie control");
    CHECK(gm_runtime_spawn_mob_fixture(
              &runtime, EW_TYPE_STRAY, stray_day, 12.5, py, 24.5,
              0, 0, 0, 0, 20, 0, 0, 0, 0), "spawn exposed Stray control");
    runtime.restored_active_mobs_enabled = 1;
    gm_runtime_tick(&runtime, idle);
    int hs = mob_slot(&runtime.mobs, husk_day);
    int zs = mob_slot(&runtime.mobs, zombie_day);
    int ss = mob_slot(&runtime.mobs, stray_day);
    CHECK(hs > 0 && runtime.mobs.fire_ticks[hs] <= 0,
          "Husk is immune to daylight ignition");
    CHECK(zs > 0 && runtime.mobs.fire_ticks[zs] > 0,
          "exposed Zombie daylight control ignites");
    CHECK(ss > 0 && runtime.mobs.fire_ticks[ss] > 0,
          "exposed Stray daylight control ignites");
    gm_runtime_destroy(&runtime);

    if (!init_runtime(&runtime, &config)) return 1;
    py = (double)gm_world_surface_y(runtime.world, 8, 8) + 1.0;
    gm_runtime_set_pose(&runtime, 8.5, py, 8.5, 0.0F, 0.0F);
    gm_runtime_set_time(&runtime, 13000);
    runtime.vitals.health = runtime.player.health = 20;
    runtime.vitals.exhaustion = 0.0F;
    const int attacker = 689100;
    CHECK(gm_runtime_spawn_mob_fixture(
              &runtime, EW_TYPE_HUSK, attacker, 8.5, py, 9.5,
              0, 0, 0, 180, 20, 0, 0, 0, 0), "spawn melee Husk");
    runtime.restored_active_mobs_enabled = 1;
    gm_runtime_tick(&runtime, idle);
    CHECK(runtime.vitals.health < 20
              && runtime.mobs.player_hunger_ticks == 140
              && runtime.vitals.exhaustion == 0.0F,
          "successful empty-hand Husk melee applies Hunger after player tick");
    gm_runtime_tick(&runtime, idle);
    CHECK(runtime.mobs.player_hunger_ticks == 139
              && fabsf(runtime.vitals.exhaustion - 0.005F) < 1.0e-7F,
          "Hunger begins exact per-tick exhaustion on following boundary");

    char save_root[256], error[256] = {0};
    snprintf(save_root, sizeof save_root, ".tmp/husk-runtime-%ld", (long)getpid());
    clean_save(save_root);
    CHECK(gm_native_save_write(&runtime, save_root, "husk", error, sizeof error),
          "native save records active Husk Hunger state");
    for (int i = 0; i < 20; ++i) gm_runtime_tick(&runtime, idle);
    int expected_ticks = runtime.mobs.player_hunger_ticks;
    float expected_exhaustion = runtime.vitals.exhaustion;
    CHECK(gm_native_save_load(&runtime, &config, save_root, "husk",
                              error, sizeof error),
          "native save restores active Husk Hunger state");
    for (int i = 0; i < 20; ++i) gm_runtime_tick(&runtime, idle);
    CHECK(runtime.mobs.player_hunger_ticks == expected_ticks
              && runtime.vitals.exhaustion == expected_exhaustion,
          "save/reload preserves exact Hunger duration and exhaustion continuation");

    clean_save(save_root);
    gm_runtime_destroy(&runtime);

    CHECK(init_runtime(&runtime, &config),
          "initialize aged-world Husk fixture");
    py = (double)gm_world_surface_y(runtime.world, 8, 8) + 1.0;
    gm_runtime_set_pose(&runtime, 8.5, py, 8.5, 0.0F, 0.0F);
    gm_runtime_set_time(&runtime, 1512000);
    runtime.vitals.health = runtime.player.health = 20;
    CHECK(gm_runtime_spawn_mob_fixture(
              &runtime, EW_TYPE_HUSK, attacker + 1, 8.5, py, 9.5,
              0, 0, 0, 180, 20, 0, 0, 0, 0), "spawn aged-world melee Husk");
    runtime.restored_active_mobs_enabled = 1;
    gm_runtime_tick(&runtime, idle);
    CHECK(runtime.mobs.player_hunger_ticks == 280,
          "NORMAL local difficulty preserves Java's 280-tick aged-world cast");
    gm_runtime_destroy(&runtime);

    if (fail) return 1;
    puts("PASS Husk runtime: daylight immunity, local-difficulty Hunger, and native continuation");
    return 0;
}
