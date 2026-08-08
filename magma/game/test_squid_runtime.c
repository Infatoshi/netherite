#include "game/native_save.h"
#include "game/runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SQUID_EID 2500

#define CHECK(condition, message) do {                                  \
    if (!(condition)) {                                                 \
        fprintf(stderr, "FAIL: %s\n", (message));                    \
        return 0;                                                       \
    }                                                                   \
} while (0)

typedef struct SquidSetup {
    double x, y, z, vx, vy, vz;
    uint64_t seed48;
    float pitch, prev_pitch, yaw, prev_yaw;
    float rotation, prev_rotation, tentacle, last_tentacle;
    float motion_speed, rotation_velocity, rotate_speed;
    float motion_x, motion_y, motion_z, render_yaw;
    float head_yaw, body_prev_head_yaw;
    int body_rotation_tick_counter;
    int in_water, entity_age, persistence_required;
} SquidSetup;

typedef struct SquidRow {
    uint64_t x, y, z, vx, vy, vz;
    uint64_t box_min_x, box_min_y, box_min_z;
    uint64_t box_max_x, box_max_y, box_max_z;
    uint64_t seed48;
    uint32_t yaw, pitch, prev_pitch, squid_yaw, prev_yaw;
    uint32_t rotation, prev_rotation, tentacle, last_tentacle;
    uint32_t motion_speed, rotation_velocity, rotate_speed;
    uint32_t motion_x, motion_y, motion_z, render_yaw;
    uint32_t head_yaw, body_prev_head_yaw;
    int body_rotation_tick_counter;
    int ticks_existed, in_water, entity_age, persistence_required;
} SquidRow;

static GmRuntime runtime;

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

static int init_runtime(GmRuntime *r, GmConfig *config) {
    char error[256];
    gm_config_defaults(config);
    config->seed = 0;
    config->world = GM_WORLD_SUPERFLAT;
    config->view_distance = 1;
    config->mobs = 0;
    config->weather = 0;
    if (!gm_runtime_init(r, config, error, sizeof error)) {
        fprintf(stderr, "FAIL: %s\n", error);
        return 0;
    }
    r->randtick_enabled = 0;
    return 1;
}

static int setup_for(const char *scenario, SquidSetup *setup) {
    memset(setup, 0, sizeof *setup);
    setup->x = !strcmp(scenario, "age_stop") ? 48.5D : 18.5D;
    setup->y = 7.0D;
    setup->z = 8.5D;
    setup->seed48 = !strcmp(scenario, "cycle_refresh")
        ? UINT64_C(2) : UINT64_C(0x123456789abc);
    setup->pitch = 12.0F;
    setup->prev_pitch = 10.0F;
    setup->yaw = 0.3F;
    setup->prev_yaw = 0.2F;
    setup->rotation = !strcmp(scenario, "cycle_refresh") ? 6.25F : 0.25F;
    setup->prev_rotation = setup->rotation - 0.1F;
    setup->tentacle = 0.2F;
    setup->last_tentacle = 0.1F;
    setup->motion_speed = 0.6F;
    setup->rotation_velocity = !strcmp(scenario, "cycle_refresh")
        ? 0.1F : 0.15F;
    setup->rotate_speed = 0.4F;
    setup->render_yaw = 5.0F;
    setup->head_yaw = 7.0F;
    setup->body_prev_head_yaw = 4.0F;
    setup->body_rotation_tick_counter = 13;
    setup->in_water = strcmp(scenario, "dry") != 0;
    setup->entity_age = !strcmp(scenario, "age_stop") ? 100 : 0;
    if (!strcmp(scenario, "water_empty")) {
        setup->motion_x = setup->motion_y = setup->motion_z = 0.0F;
    } else {
        setup->motion_x = 0.1F;
        setup->motion_y = 0.02F;
        setup->motion_z = -0.15F;
    }
    setup->vx = (double)(setup->motion_x * setup->motion_speed);
    setup->vy = (double)(setup->motion_y * setup->motion_speed);
    setup->vz = (double)(setup->motion_z * setup->motion_speed);
    return !strcmp(scenario, "water_empty")
        || !strcmp(scenario, "water_vector")
        || !strcmp(scenario, "cycle_refresh")
        || !strcmp(scenario, "age_stop")
        || !strcmp(scenario, "dry");
}

static int stage_squid(
        GmRuntime *r, const char *scenario, SquidSetup *setup) {
    CHECK(setup_for(scenario, setup), "known Squid scenario");
    gm_runtime_set_pose_state(
        r, 8.5D, 5.0D, 8.5D, 0.0F, 0.0F,
        0.0D, 0.0D, 0.0D, 1, 0.0F);
    int center_x = (int)setup->x;
    for (int x = center_x - 3; x <= center_x + 3; ++x)
        for (int z = 5; z <= 12; ++z) {
            CHECK(gm_runtime_set_block(r, x, 4, z, 1, 0),
                  "stage Squid pool floor");
            for (int y = 5; y <= 11; ++y)
                CHECK(gm_runtime_set_block(
                          r, x, y, z, setup->in_water ? 9 : 0, 0),
                      "stage Squid water or dry volume");
        }
    CHECK(gm_runtime_spawn_mob_fixture(
              r, GM_MOB_SQUID, SQUID_EID,
              setup->x, setup->y, setup->z,
              setup->vx, setup->vy, setup->vz,
              setup->yaw, 10.0F, 1, 0, 0, 0),
          "spawn controlled Squid fixture");
    CHECK(gm_runtime_restore_no_ai_mob_state(
              r, SQUID_EID, 300, -1, 0, 0.0F, setup->in_water,
              0, 0, 0.0F, setup->seed48, 0, 0.0D),
          "restore Squid base state");
    CHECK(gm_runtime_restore_squid_state(
              r, SQUID_EID,
              setup->pitch, setup->prev_pitch,
              setup->yaw, setup->prev_yaw,
              setup->rotation, setup->prev_rotation,
              setup->tentacle, setup->last_tentacle,
              setup->motion_speed, setup->rotation_velocity,
              setup->rotate_speed, setup->motion_x,
              setup->motion_y, setup->motion_z, setup->render_yaw,
              setup->head_yaw, setup->body_rotation_tick_counter,
              setup->body_prev_head_yaw),
          "restore Squid private animation and motion state");
    CHECK(gm_runtime_set_squid_ai_state(
              r, SQUID_EID, setup->entity_age,
              setup->persistence_required),
          "restore active Squid lifetime state");
    CHECK(gm_runtime_set_mob_no_ai(r, SQUID_EID, 0),
          "enable Squid AI at staged boundary");
    return 1;
}

static int capture_squid(const GmRuntime *r, SquidRow *row) {
    int slot = gm_mobs_find_slot_by_eid(&r->mobs, SQUID_EID);
    const EwStore *store = r->mobs.current ? &r->mobs.b : &r->mobs.a;
    if (slot <= 0 || !store->alive[slot]
            || store->type[slot] != GM_MOB_SQUID)
        return 0;
    memset(row, 0, sizeof *row);
    row->x = double_bits(store->x[slot]);
    row->y = double_bits(store->y[slot]);
    row->z = double_bits(store->z[slot]);
    row->vx = double_bits(store->vx[slot]);
    row->vy = double_bits(store->vy[slot]);
    row->vz = double_bits(store->vz[slot]);
    row->yaw = float_bits(store->yaw[slot]);
    row->pitch = float_bits(r->mobs.squid_pitch[slot]);
    row->prev_pitch = float_bits(r->mobs.squid_prev_pitch[slot]);
    row->squid_yaw = float_bits(r->mobs.squid_yaw[slot]);
    row->prev_yaw = float_bits(r->mobs.squid_prev_yaw[slot]);
    row->rotation = float_bits(r->mobs.squid_rotation[slot]);
    row->prev_rotation = float_bits(r->mobs.squid_prev_rotation[slot]);
    row->tentacle = float_bits(r->mobs.squid_tentacle_angle[slot]);
    row->last_tentacle =
        float_bits(r->mobs.squid_last_tentacle_angle[slot]);
    row->motion_speed = float_bits(r->mobs.squid_random_motion_speed[slot]);
    row->rotation_velocity =
        float_bits(r->mobs.squid_rotation_velocity[slot]);
    row->rotate_speed = float_bits(r->mobs.squid_rotate_speed[slot]);
    row->motion_x = float_bits(r->mobs.squid_random_motion_x[slot]);
    row->motion_y = float_bits(r->mobs.squid_random_motion_y[slot]);
    row->motion_z = float_bits(r->mobs.squid_random_motion_z[slot]);
    row->render_yaw = float_bits(r->mobs.squid_render_yaw_offset[slot]);
    row->head_yaw = float_bits(r->mobs.passive_head_yaw[slot]);
    row->body_prev_head_yaw =
        float_bits(r->mobs.body_prev_head_yaw[slot]);
    row->body_rotation_tick_counter =
        r->mobs.body_rotation_tick_counter[slot];
    row->seed48 = r->mobs.entity_random[slot].random.seed;
    row->ticks_existed = r->mobs.entity_ticks_existed[slot];
    row->in_water = r->mobs.entity_in_water[slot];
    row->entity_age = r->mobs.entity_age[slot];
    row->persistence_required = r->mobs.persistence_required[slot];
    row->box_min_x = double_bits(r->mobs.entity_box_min_x[slot]);
    row->box_min_y = double_bits(r->mobs.entity_box_min_y[slot]);
    row->box_min_z = double_bits(r->mobs.entity_box_min_z[slot]);
    row->box_max_x = double_bits(r->mobs.entity_box_max_x[slot]);
    row->box_max_y = double_bits(r->mobs.entity_box_max_y[slot]);
    row->box_max_z = double_bits(r->mobs.entity_box_max_z[slot]);
    return 1;
}

static void print_row(int tick, const SquidRow *row) {
    printf("{\"tick\":%d,\"alive\":1,\"x\":\"%016llx\","
           "\"y\":\"%016llx\",\"z\":\"%016llx\","
           "\"vx\":\"%016llx\",\"vy\":\"%016llx\","
           "\"vz\":\"%016llx\",\"yaw\":\"%08x\","
           "\"pitch\":\"%08x\",\"prev_pitch\":\"%08x\","
           "\"squid_yaw\":\"%08x\",\"prev_yaw\":\"%08x\","
           "\"rotation\":\"%08x\",\"prev_rotation\":\"%08x\","
           "\"tentacle\":\"%08x\",\"last_tentacle\":\"%08x\","
           "\"motion_speed\":\"%08x\","
           "\"rotation_velocity\":\"%08x\","
           "\"rotate_speed\":\"%08x\",\"motion_x\":\"%08x\","
           "\"motion_y\":\"%08x\",\"motion_z\":\"%08x\","
           "\"render_yaw\":\"%08x\",\"head_yaw\":\"%08x\","
           "\"body_prev_head_yaw\":\"%08x\","
           "\"body_rotation_tick_counter\":%d,\"seed48\":%llu,"
           "\"ticks_existed\":%d,\"in_water\":%d,"
           "\"entity_age\":%d,\"persistence_required\":%d,"
           "\"box\":[\"%016llx\",\"%016llx\",\"%016llx\","
           "\"%016llx\",\"%016llx\",\"%016llx\"]}\n",
           tick,
           (unsigned long long)row->x,
           (unsigned long long)row->y,
           (unsigned long long)row->z,
           (unsigned long long)row->vx,
           (unsigned long long)row->vy,
           (unsigned long long)row->vz,
           row->yaw, row->pitch, row->prev_pitch,
           row->squid_yaw, row->prev_yaw,
           row->rotation, row->prev_rotation,
           row->tentacle, row->last_tentacle,
           row->motion_speed, row->rotation_velocity, row->rotate_speed,
           row->motion_x, row->motion_y, row->motion_z, row->render_yaw,
           row->head_yaw, row->body_prev_head_yaw,
           row->body_rotation_tick_counter,
           (unsigned long long)row->seed48,
           row->ticks_existed, row->in_water,
           row->entity_age, row->persistence_required,
           (unsigned long long)row->box_min_x,
           (unsigned long long)row->box_min_y,
           (unsigned long long)row->box_min_z,
           (unsigned long long)row->box_max_x,
           (unsigned long long)row->box_max_y,
           (unsigned long long)row->box_max_z);
}

static void tick_once(GmRuntime *r) {
    GmAction action;
    memset(&action, 0, sizeof action);
    action.hotbar_sel = -1;
    gm_runtime_tick(r, action);
}

static int oracle_rows(const char *scenario, int ticks) {
    GmConfig config;
    SquidSetup setup;
    if (ticks <= 0 || ticks > 200 || !init_runtime(&runtime, &config)
            || !stage_squid(&runtime, scenario, &setup))
        return 0;
    for (int tick = 0; tick < ticks; ++tick) {
        SquidRow row;
        tick_once(&runtime);
        if (capture_squid(&runtime, &row)) print_row(tick, &row);
        else printf("{\"tick\":%d,\"alive\":0}\n", tick);
    }
    gm_runtime_destroy(&runtime);
    return 1;
}

static void clean_save(const char *root) {
    char path[512];
    static const char *files[] = {
        "runtime.bin", "player_statistics.json", "manifest.bin",
        "world_dim-1.bin", "world_dim0.bin", "world_dim1.bin",
    };
    for (size_t index = 0; index < sizeof files / sizeof files[0]; ++index) {
        snprintf(path, sizeof path,
                 "%s/squid/generation-0000000000000001/%s",
                 root, files[index]);
        (void)remove(path);
    }
    snprintf(path, sizeof path,
             "%s/squid/generation-0000000000000001", root);
    (void)rmdir(path);
    snprintf(path, sizeof path, "%s/squid/current", root);
    (void)remove(path);
    snprintf(path, sizeof path, "%s/squid/write.lock", root);
    (void)remove(path);
    snprintf(path, sizeof path, "%s/squid", root);
    (void)rmdir(path);
    (void)rmdir(root);
}

static int native_regression(void) {
    static const char *const scenarios[] = {
        "water_empty", "water_vector", "cycle_refresh", "age_stop", "dry"
    };
    for (size_t i = 0; i < sizeof scenarios / sizeof scenarios[0]; ++i) {
        GmConfig config;
        SquidSetup setup;
        SquidRow before, after;
        CHECK(init_runtime(&runtime, &config)
                  && stage_squid(&runtime, scenarios[i], &setup)
                  && capture_squid(&runtime, &before),
              "stage active Squid unit fixture");
        tick_once(&runtime);
        CHECK(capture_squid(&runtime, &after)
                  && after.ticks_existed == before.ticks_existed + 1,
              "active Squid advances a complete living tick");
        {
            GmEntityView views[8];
            int count = gm_mobs_fill_views(&runtime.mobs, views, 8);
            int found = 0;
            for (int view = 0; view < count; ++view)
                if (views[view].ent_id == SQUID_EID) {
                    int slot = gm_mobs_find_slot_by_eid(
                        &runtime.mobs, SQUID_EID);
                    CHECK(float_bits(views[view].yaw) == float_bits(
                              runtime.mobs.squid_render_yaw_offset[slot])
                              && float_bits(views[view].pitch) == float_bits(
                                  runtime.mobs.squid_pitch[slot])
                              && float_bits(views[view].head_yaw) == float_bits(
                                  runtime.mobs.squid_yaw[slot])
                              && float_bits(views[view].anim_time) == float_bits(
                                  runtime.mobs.squid_tentacle_angle[slot]),
                          "live Squid pose reaches the render view bit-exactly");
                    found = 1;
                }
            CHECK(found, "active Squid has a live render view");
        }
        if (!strcmp(scenarios[i], "water_empty"))
            CHECK(after.motion_x || after.motion_y || after.motion_z,
                  "empty Squid vector consumes RNG and initializes motion");
        if (!strcmp(scenarios[i], "age_stop"))
            CHECK(after.motion_x == 0 && after.motion_y == 0
                      && after.motion_z == 0,
                  "old far Squid clears its random motion vector");
        gm_runtime_destroy(&runtime);
    }

    GmConfig config;
    SquidSetup setup;
    SquidRow uninterrupted[12], restored;
    char save_root[256], error[256];
    CHECK(init_runtime(&runtime, &config)
              && stage_squid(&runtime, "water_empty", &setup),
          "stage active Squid checkpoint fixture");
    (void)mkdir(".tmp", 0700);
    snprintf(save_root, sizeof save_root,
             ".tmp/squid-runtime-%ld", (long)getpid());
    clean_save(save_root);
    for (int tick = 0; tick < 12; ++tick) {
        tick_once(&runtime);
        CHECK(capture_squid(&runtime, &uninterrupted[tick]),
              "capture uninterrupted active Squid");
        if (tick == 5)
            CHECK(gm_native_save_write(
                      &runtime, save_root, "squid", error, sizeof error),
                  error);
    }
    CHECK(gm_native_save_load(
              &runtime, &config, save_root, "squid", error, sizeof error),
          error);
    for (int tick = 6; tick < 12; ++tick) {
        tick_once(&runtime);
        CHECK(capture_squid(&runtime, &restored)
                  && !memcmp(&restored, &uninterrupted[tick],
                             sizeof restored),
              "native save resumes exact Squid private state and RNG");
    }
    gm_runtime_destroy(&runtime);
    clean_save(save_root);
    puts("PASS Squid: active random-swim, age, dry, capsule/checkpoint");
    return 1;
}

int main(int argc, char **argv) {
    if (argc == 4 && !strcmp(argv[1], "--oracle"))
        return oracle_rows(argv[2], atoi(argv[3])) ? 0 : 1;
    if (argc != 1) {
        fprintf(stderr, "usage: %s [--oracle SCENARIO TICKS]\n", argv[0]);
        return 2;
    }
    return native_regression() ? 0 : 1;
}
