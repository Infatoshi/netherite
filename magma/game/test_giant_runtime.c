#include "game/native_save.h"
#include "game/runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fail;
#define CHECK(C, M) do { \
    if (!(C)) { fprintf(stderr, "FAIL: %s\n", M); fail = 1; } \
} while (0)

typedef struct {
    double x, y, z, mx, my, mz;
    float health, yaw;
    int path_len, ai_state, attack_time, ticks_existed;
    int player_health;
} GiantState;

static int mob_slot(const GmMobLive *m, int eid) {
    const EwStore *s = m->current ? &m->b : &m->a;
    for (int slot = 1; slot < EW_MAX_ENTITIES; ++slot)
        if (s->alive[slot] && s->id[slot] == eid)
            return slot;
    return -1;
}

static int snapshot(GmRuntime *r, int eid, GiantState *out) {
    const EwStore *s = r->mobs.current ? &r->mobs.b : &r->mobs.a;
    int slot = mob_slot(&r->mobs, eid);
    if (slot < 0) return 0;
    memset(out, 0, sizeof *out);
    *out = (GiantState){
        s->x[slot], s->y[slot], s->z[slot],
        s->vx[slot], s->vy[slot], s->vz[slot],
        s->health[slot], s->yaw[slot], s->path_len[slot],
        (int)s->ai_state[slot], s->attack_time[slot],
        r->mobs.entity_ticks_existed[slot], r->vitals.health,
    };
    return 1;
}

static void clean_save(const char *root) {
    char path[512];
    static const char *files[] = {
        "runtime.bin", "player_statistics.json", "manifest.bin",
        "world_dim-1.bin", "world_dim0.bin", "world_dim1.bin",
    };
    for (size_t i = 0; i < sizeof files / sizeof files[0]; ++i) {
        snprintf(path, sizeof path,
                 "%s/giant/generation-0000000000000001/%s",
                 root, files[i]);
        (void)remove(path);
    }
    snprintf(path, sizeof path,
             "%s/giant/generation-0000000000000001", root);
    (void)rmdir(path);
    snprintf(path, sizeof path, "%s/giant/current", root);
    (void)remove(path);
    snprintf(path, sizeof path, "%s/giant/write.lock", root);
    (void)remove(path);
    snprintf(path, sizeof path, "%s/giant", root);
    (void)rmdir(path);
    (void)rmdir(root);
}

int main(void) {
    static GmRuntime runtime;
    GmConfig config;
    char error[256] = {0};
    const int eid = 688000;
    gm_config_defaults(&config);
    config.seed = 0;
    config.view_distance = 1;
    config.mobs = 0;
    config.weather = 0;
    CHECK(gm_runtime_init(&runtime, &config, error, sizeof error), error);
    if (fail) return 1;

    double y = (double)gm_world_surface_y(runtime.world, 8, 8) + 1.0;
    gm_runtime_set_pose(&runtime, 8.5, y, 8.5, 0.0F, 0.0F);
    runtime.vitals.health = 20;
    runtime.player.health = 20;
    CHECK(gm_runtime_spawn_mob_fixture(
              &runtime, EW_TYPE_GIANT, eid, 8.5, y, 20.5,
              0.0, 0.0, 0.0, 37.0F, 100.0F, 0, 0, 0, 0),
          "active Giant fixture initializes");

    GmAction idle = {.hotbar_sel = -1};
    for (int tick = 0; tick < 20; ++tick)
        gm_runtime_tick(&runtime, idle);
    GiantState middle;
    CHECK(snapshot(&runtime, eid, &middle), "Giant remains loaded");
    CHECK(middle.x == 8.5 && middle.z == 20.5
              && middle.mx == 0.0 && middle.mz == 0.0
              && middle.path_len == 0 && middle.ai_state == EW_AI_IDLE
              && middle.attack_time == 0 && middle.player_health == 20,
          "goal-less Giant neither targets, navigates, wanders, nor attacks");

    char save_root[256];
    snprintf(save_root, sizeof save_root,
             ".tmp/giant-runtime-%ld", (long)getpid());
    clean_save(save_root);
    CHECK(gm_native_save_write(
              &runtime, save_root, "giant", error, sizeof error),
          "native save records the active goal-less Giant boundary");
    for (int tick = 0; tick < 20; ++tick)
        gm_runtime_tick(&runtime, idle);
    GiantState expected, actual;
    CHECK(snapshot(&runtime, eid, &expected),
          "uninterrupted Giant continuation remains loaded");
    CHECK(gm_native_save_load(
              &runtime, &config, save_root, "giant", error, sizeof error),
          "native save restores the active Giant boundary");
    for (int tick = 0; tick < 20; ++tick)
        gm_runtime_tick(&runtime, idle);
    CHECK(snapshot(&runtime, eid, &actual)
              && memcmp(&actual, &expected, sizeof actual) == 0,
          "save/reload preserves exact active Giant continuation");
    CHECK(actual.ticks_existed == middle.ticks_existed + 20
              && actual.player_health == 20,
          "Giant continuation advances without attacking the nearby player");

    clean_save(save_root);
    gm_runtime_destroy(&runtime);
    if (fail) return 1;
    puts("PASS Giant runtime: goal-less active ticks and native save continuation");
    return 0;
}
