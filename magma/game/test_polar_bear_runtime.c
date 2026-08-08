#include "game/native_save.h"
#include "game/runtime.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int fail;
#define CHECK(C, M) do { \
    if (!(C)) { fprintf(stderr, "FAIL: %s\n", M); fail = 1; } \
} while (0)

static void clean_save(const char *root) {
    char path[512];
    static const char *files[] = {
        "runtime.bin", "player_statistics.json", "manifest.bin",
        "world_dim-1.bin", "world_dim0.bin", "world_dim1.bin",
    };
    for (size_t i = 0; i < sizeof files / sizeof files[0]; ++i) {
        snprintf(path, sizeof path,
            "%s/polar/generation-0000000000000001/%s", root, files[i]);
        (void)remove(path);
    }
    snprintf(path, sizeof path,
        "%s/polar/generation-0000000000000001", root);
    (void)rmdir(path);
    snprintf(path, sizeof path, "%s/polar/current", root);
    (void)remove(path);
    snprintf(path, sizeof path, "%s/polar/write.lock", root);
    (void)remove(path);
    snprintf(path, sizeof path, "%s/polar", root);
    (void)rmdir(path);
    (void)rmdir(root);
}

typedef struct {
    int attack_time, warning_ticks, standing;
    float stand0, stand;
    float player_health;
    double x, y, z;
} PolarState;

static PolarState snapshot(const GmRuntime *r, int slot) {
    const EwStore *s = r->mobs.current ? &r->mobs.b : &r->mobs.a;
    return (PolarState){
        s->attack_time[slot], r->mobs.polar_warning_sound_ticks[slot],
        r->mobs.polar_standing[slot],
        r->mobs.polar_stand_animation0[slot],
        r->mobs.polar_stand_animation[slot], r->vitals.health,
        s->x[slot], s->y[slot], s->z[slot],
    };
}

int main(void) {
    static GmRuntime runtime;
    GmConfig config;
    char error[256] = {0};
    gm_config_defaults(&config);
    config.seed = 0;
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.mobs = 0;
    config.weather = 0;
    CHECK(gm_runtime_init(&runtime, &config, error, sizeof error), error);
    if (fail) return 1;

    gm_runtime_set_pose(&runtime, 8.5, 4.0, 11.3, 180.0F, 0.0F);
    runtime.vitals.health = runtime.player.health = 20.0F;
    int adult = gm_mobs_spawn(
        &runtime.mobs, EW_TYPE_POLAR_BEAR, 8.5, 4.0, 8.5);
    int child = gm_mobs_spawn(
        &runtime.mobs, EW_TYPE_POLAR_BEAR, 9.5, 4.0, 8.5);
    CHECK(adult > 0 && child > 0, "spawn Polar Bear family fixture");
    runtime.mobs.growing_age[child] = -24000;
    runtime.mobs.polar_warning_sound_ticks[adult] = 0;
    EwStore *store = runtime.mobs.current
        ? &runtime.mobs.b : &runtime.mobs.a;
    store->attack_time[adult] = 10;
    runtime.mobs_enabled = 1;
    runtime.gamerules.doMobSpawning = 0;
    GmAction idle = {.hotbar_sel = -1};
    gm_runtime_tick(&runtime, idle);

    GmRuntimeSoundEvent warning = {0};
    CHECK(runtime.mobs.polar_player_target[adult]
              && runtime.mobs.polar_standing[adult]
              && runtime.mobs.polar_warning_sound_ticks[adult] == 40,
          "adult acquires player near child and enters warning stand");
    CHECK(gm_runtime_sound_event_count(&runtime) == 1
              && gm_runtime_sound_event_get(&runtime, 0, &warning)
              && warning.sound == GM_SOUND_POLAR_BEAR_WARNING,
          "warning edge emits the owned Polar Bear sound once");

    store = runtime.mobs.current ? &runtime.mobs.b : &runtime.mobs.a;
    gm_runtime_set_pose(
        &runtime, store->x[adult], store->y[adult],
        store->z[adult] + 1.0, 180.0F, 0.0F);
    store->attack_time[adult] = 0;
    runtime.mobs.player_hurt_resistant = 0;
    gm_runtime_tick(&runtime, idle);
    CHECK(runtime.vitals.health == 14.0F
              && !runtime.mobs.polar_standing[adult]
              && (runtime.mobs.current ? runtime.mobs.b.attack_time[adult]
                                      : runtime.mobs.a.attack_time[adult]) == 20,
          "reach edge deals exact six damage and lowers the bear");

    char save_root[256];
    snprintf(save_root, sizeof save_root,
        ".tmp/polar-runtime-%ld", (long)getpid());
    clean_save(save_root);
    CHECK(gm_native_save_write(
              &runtime, save_root, "polar", error, sizeof error),
          "native save records active Polar Bear state");
    for (int tick = 0; tick < 5; ++tick) gm_runtime_tick(&runtime, idle);
    PolarState expected = snapshot(&runtime, adult);
    CHECK(gm_native_save_load(
              &runtime, &config, save_root, "polar", error, sizeof error),
          "native save restores active Polar Bear state");
    for (int tick = 0; tick < 5; ++tick) gm_runtime_tick(&runtime, idle);
    PolarState actual = snapshot(&runtime, adult);
    CHECK(memcmp(&actual, &expected, sizeof actual) == 0,
          "Polar Bear melee and stand continuation is byte-exact");

    clean_save(save_root);
    gm_runtime_destroy(&runtime);
    if (fail) return 1;
    puts("PASS Polar Bear runtime: child defense, warning/melee, and continuation");
    return 0;
}
