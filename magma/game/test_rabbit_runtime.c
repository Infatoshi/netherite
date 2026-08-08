#include "game/native_save.h"
#include "game/runtime.h"

#include <stdio.h>
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
            "%s/rabbit/generation-0000000000000001/%s", root, files[i]);
        (void)remove(path);
    }
    snprintf(path, sizeof path,
        "%s/rabbit/generation-0000000000000001", root);
    (void)rmdir(path);
    snprintf(path, sizeof path, "%s/rabbit/current", root);
    (void)remove(path);
    snprintf(path, sizeof path, "%s/rabbit/write.lock", root);
    (void)remove(path);
    snprintf(path, sizeof path, "%s/rabbit", root);
    (void)rmdir(path);
    (void)rmdir(root);
}

typedef struct {
    int attack_time, rabbit_type, jump_ticks, jump_duration;
    int move_duration, carrot_ticks;
    double move_speed, x, y, z, vx, vy, vz;
    float player_health;
} RabbitState;

static RabbitState snapshot(const GmRuntime *r, int slot) {
    const EwStore *s = r->mobs.current ? &r->mobs.b : &r->mobs.a;
    return (RabbitState){
        s->attack_time[slot], r->mobs.rabbit_type[slot],
        r->mobs.rabbit_jump_ticks[slot],
        r->mobs.rabbit_jump_duration[slot],
        r->mobs.rabbit_move_duration[slot],
        r->mobs.rabbit_carrot_ticks[slot],
        r->mobs.rabbit_move_speed[slot],
        s->x[slot], s->y[slot], s->z[slot],
        s->vx[slot], s->vy[slot], s->vz[slot], r->vitals.health,
    };
}

static int rabbit_state_equal(const RabbitState *a, const RabbitState *b) {
    return a->attack_time == b->attack_time
        && a->rabbit_type == b->rabbit_type
        && a->jump_ticks == b->jump_ticks
        && a->jump_duration == b->jump_duration
        && a->move_duration == b->move_duration
        && a->carrot_ticks == b->carrot_ticks
        && a->move_speed == b->move_speed
        && a->x == b->x && a->y == b->y && a->z == b->z
        && a->vx == b->vx && a->vy == b->vy && a->vz == b->vz
        && a->player_health == b->player_health;
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

    gm_runtime_set_pose(&runtime, 8.5, 4.0, 9.5, 180.0F, 0.0F);
    runtime.vitals.health = runtime.player.health = 20.0F;
    int rabbit = gm_mobs_spawn(
        &runtime.mobs, EW_TYPE_RABBIT, 8.5, 4.0, 8.5);
    CHECK(rabbit > 0 && gm_mobs_set_rabbit_state(
              &runtime.mobs,
              (runtime.mobs.current ? runtime.mobs.b.id[rabbit]
                                    : runtime.mobs.a.id[rabbit]),
              99, 40),
          "spawn and configure Killer Bunny fixture");
    runtime.mobs_enabled = 1;
    runtime.gamerules.doMobSpawning = 0;
    runtime.mobs.player_hurt_resistant = 0;
    GmAction idle = {.hotbar_sel = -1};
    gm_runtime_tick(&runtime, idle);

    GmRuntimeSoundEvent attack = {0};
    CHECK(runtime.vitals.health == 12.0F,
          "Killer Bunny deals exact eight attack damage");
    CHECK(gm_runtime_sound_event_count(&runtime) >= 1
              && gm_runtime_sound_event_get(&runtime, 0, &attack)
              && attack.sound == GM_SOUND_RABBIT_ATTACK,
          "Killer Bunny attack emits the owned sound");

    const EwStore *read = runtime.mobs.current
        ? &runtime.mobs.b : &runtime.mobs.a;
    gm_runtime_set_pose(
        &runtime, read->x[rabbit], read->y[rabbit],
        read->z[rabbit] + 6.0, 180.0F, 0.0F);
    EwStore *store = runtime.mobs.current
        ? &runtime.mobs.b : &runtime.mobs.a;
    store->on_ground[rabbit] = 1;
    runtime.mobs.rabbit_was_on_ground[rabbit] = 1;
    runtime.mobs.rabbit_move_duration[rabbit] = 0;
    gm_runtime_tick(&runtime, idle);
    CHECK(runtime.mobs.rabbit_jump_duration[rabbit] == 10
              && runtime.mobs.rabbit_move_speed[rabbit] == 1.4D,
          "Killer Bunny chase starts an exact ten-tick fast hop");

    char save_root[256];
    snprintf(save_root, sizeof save_root,
        ".tmp/rabbit-runtime-%ld", (long)getpid());
    clean_save(save_root);
    CHECK(gm_native_save_write(
              &runtime, save_root, "rabbit", error, sizeof error),
          "native save records active Rabbit state");
    for (int tick = 0; tick < 5; ++tick) gm_runtime_tick(&runtime, idle);
    RabbitState expected = snapshot(&runtime, rabbit);
    CHECK(gm_native_save_load(
              &runtime, &config, save_root, "rabbit", error, sizeof error),
          "native save restores active Rabbit state");
    for (int tick = 0; tick < 5; ++tick) gm_runtime_tick(&runtime, idle);
    RabbitState actual = snapshot(&runtime, rabbit);
    CHECK(rabbit_state_equal(&actual, &expected),
          "Rabbit hopping and attack continuation is byte-exact");

    store = runtime.mobs.current ? &runtime.mobs.b : &runtime.mobs.a;
    runtime.mobs.rabbit_type[rabbit] = 0;
    runtime.mobs.rabbit_carrot_ticks[rabbit] = 0;
    runtime.mobs.rabbit_raid_valid[rabbit] = 0;
    runtime.mobs.entity_ticks_existed[rabbit] = 0;
    int crop_x = (int)store->x[rabbit];
    int crop_y = (int)store->y[rabbit];
    int crop_z = (int)store->z[rabbit];
    gm_world_set_block_meta(runtime.world, crop_x, crop_y - 1, crop_z, 60, 7);
    gm_world_set_block_meta(runtime.world, crop_x, crop_y, crop_z, 141, 7);
    gm_runtime_tick(&runtime, idle);
    CHECK(gm_world_meta(runtime.world, crop_x, crop_y, crop_z) == 6
              && runtime.mobs.rabbit_carrot_ticks[rabbit] == 40,
          "Rabbit raids one mature carrot stage and starts cooldown");

    clean_save(save_root);
    gm_runtime_destroy(&runtime);
    if (fail) return 1;
    puts("PASS Rabbit runtime: Killer Bunny, hopping, and continuation");
    return 0;
}
