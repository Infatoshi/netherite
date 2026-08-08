#include "game/native_save.h"
#include "game/runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define BAT_EID 2400
#define BAT_SEED UINT64_C(0x123456789abc)

#define CHECK(condition, message) do {                                  \
    if (!(condition)) {                                                 \
        fprintf(stderr, "FAIL: %s\n", (message));                    \
        return 0;                                                       \
    }                                                                   \
} while (0)

typedef struct BatRow {
    uint64_t x, y, z, vx, vy, vz;
    uint64_t box_min_x, box_min_y, box_min_z;
    uint64_t box_max_x, box_max_y, box_max_z;
    uint64_t seed48;
    uint32_t yaw, pitch, head_yaw, render_yaw, body_prev_head_yaw;
    uint32_t fall_distance;
    int hanging, spawn_valid, spawn_x, spawn_y, spawn_z;
    int body_rotation_tick_counter;
    int ticks_existed, living_sound_time, on_ground;
    int entity_age, persistence_required;
} BatRow;

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

static int stage_bat(
        GmRuntime *r, const char *scenario,
        double player_x, double player_y, double player_z) {
    int center_x = mc_floor(player_x);
    int center_z = mc_floor(player_z);
    int ground_y = mc_floor(player_y) - 1;
    int near = !strcmp(scenario, "near_wake")
        || !strcmp(scenario, "creative_hang")
        || !strcmp(scenario, "near_reset");
    int soft = !strcmp(scenario, "soft_keep")
        || !strcmp(scenario, "soft_drop");
    int hard = !strcmp(scenario, "hard_drop")
        || !strcmp(scenario, "persistent_far");
    int hanging = !strcmp(scenario, "supported")
        || !strcmp(scenario, "unsupported")
        || !strcmp(scenario, "near_wake")
        || !strcmp(scenario, "creative_hang");
    int invalid = !strcmp(scenario, "invalid_target");
    double bat_x = center_x + (near ? 2.5D : 10.5D);
    double bat_y = ground_y + (soft ? 42.0D : hard ? 132.0D
        : near ? 1.1D : hanging ? 6.1D : 6.0D);
    double bat_z = center_z + 0.5D;
    int target_x = (int)bat_x + 5;
    int target_y = (int)bat_y + 2;
    int target_z = (int)bat_z + 4;

    int entity_age = soft || !strcmp(scenario, "near_reset") ? 600
        : !strcmp(scenario, "persistent_far") ? 1000 : 0;
    int persistence_required = !strcmp(scenario, "persistent_far");
    /* Seed 460 yields ambient nextInt(1000)=247 followed by the despawn
     * nextInt(800)=0. */
    uint64_t seed = !strcmp(scenario, "soft_drop") ? 460 : BAT_SEED;

    if (strcmp(scenario, "flight") && strcmp(scenario, "supported")
            && strcmp(scenario, "unsupported") && !near && !soft
            && !hard && !invalid)
        return 0;
    player_x = center_x + 0.5D;
    player_y = ground_y + 1.0D;
    player_z = center_z + 0.5D;
    gm_runtime_set_pose_state(
        r, player_x, player_y, player_z,
        0.0F, 0.0F, 0.0D, 0.0D, 0.0D, 1, 0.0F);
    for (int x = center_x - 3; x <= center_x + 18; ++x)
        for (int z = center_z - 5; z <= center_z + 7; ++z) {
            CHECK(gm_runtime_set_block(r, x, ground_y, z, 1, 0),
                  "stage Bat fixture ground");
            for (int y = ground_y + 1; y <= ground_y + 12; ++y)
                CHECK(gm_runtime_set_block(r, x, y, z, 0, 0),
                      "clear Bat fixture air");
        }
    if (!strcmp(scenario, "supported")
            || !strcmp(scenario, "near_wake")
            || !strcmp(scenario, "creative_hang"))
        CHECK(gm_runtime_set_block(
                  r, mc_floor(bat_x), mc_floor(bat_y) + 1,
                  mc_floor(bat_z), 1, 0),
              "stage Bat ceiling support");
    if (invalid)
        CHECK(gm_runtime_set_block(
                  r, target_x, target_y, target_z, 1, 0),
              "occupy Bat flight target");
    CHECK(gm_runtime_spawn_mob_fixture(
              r, GM_MOB_BAT, BAT_EID,
              bat_x, bat_y, bat_z,
              0.0D, 0.0D, 0.0D, 0.0F, 6.0F, 1, 0, 0, 0),
          "spawn exact Bat fixture");
    CHECK(gm_runtime_restore_no_ai_mob_state(
              r, BAT_EID, 300, -1, 0, 0.0F, 0, 0, 0, 0.0F,
              seed, 0, 0.0D),
          "restore Bat base state before enabling AI");
    CHECK(gm_runtime_set_bat_ai_state(
              r, BAT_EID, hanging, 1,
              target_x, target_y, target_z,
              0.0F, 0.0F, 0, 0.0F,
              entity_age, persistence_required),
          "restore active Bat private AI state");
    CHECK(gm_runtime_set_mob_no_ai(r, BAT_EID, 0),
          "enable Bat AI at the staged boundary");
    if (!strcmp(scenario, "creative_hang")) {
        r->tape_creative = 1;
        r->tape_game_mode = GM_MODE_CREATIVE;
    }
    r->world_event_head = 0;
    r->world_event_count = 0;
    r->sound_event_head = 0;
    r->sound_event_count = 0;
    return 1;
}

static int capture_bat(const GmRuntime *r, BatRow *row) {
    int slot = gm_mobs_find_slot_by_eid(&r->mobs, BAT_EID);
    const EwStore *store = r->mobs.current ? &r->mobs.b : &r->mobs.a;
    if (slot <= 0 || !store->alive[slot]
            || store->type[slot] != GM_MOB_BAT)
        return 0;
    memset(row, 0, sizeof *row);
    row->x = double_bits(store->x[slot]);
    row->y = double_bits(store->y[slot]);
    row->z = double_bits(store->z[slot]);
    row->vx = double_bits(store->vx[slot]);
    row->vy = double_bits(store->vy[slot]);
    row->vz = double_bits(store->vz[slot]);
    row->yaw = float_bits(store->yaw[slot]);
    row->pitch = float_bits(r->mobs.entity_pitch[slot]);
    row->head_yaw = float_bits(r->mobs.passive_head_yaw[slot]);
    row->render_yaw = float_bits(r->mobs.squid_render_yaw_offset[slot]);
    row->body_prev_head_yaw =
        float_bits(r->mobs.body_prev_head_yaw[slot]);
    row->body_rotation_tick_counter =
        r->mobs.body_rotation_tick_counter[slot];
    row->fall_distance = float_bits(r->mobs.entity_fall_distance[slot]);
    row->hanging = r->mobs.bat_hanging[slot];
    row->spawn_valid = r->mobs.bat_spawn_position_valid[slot];
    row->spawn_x = r->mobs.bat_spawn_x[slot];
    row->spawn_y = r->mobs.bat_spawn_y[slot];
    row->spawn_z = r->mobs.bat_spawn_z[slot];
    row->seed48 = r->mobs.entity_random[slot].random.seed;
    row->ticks_existed = r->mobs.entity_ticks_existed[slot];
    row->living_sound_time = r->mobs.entity_living_sound_time[slot];
    row->entity_age = r->mobs.entity_age[slot];
    row->persistence_required = r->mobs.persistence_required[slot];
    row->on_ground = store->on_ground[slot];
    row->box_min_x = double_bits(r->mobs.entity_box_min_x[slot]);
    row->box_min_y = double_bits(r->mobs.entity_box_min_y[slot]);
    row->box_min_z = double_bits(r->mobs.entity_box_min_z[slot]);
    row->box_max_x = double_bits(r->mobs.entity_box_max_x[slot]);
    row->box_max_y = double_bits(r->mobs.entity_box_max_y[slot]);
    row->box_max_z = double_bits(r->mobs.entity_box_max_z[slot]);
    return 1;
}

static void print_row(int tick, const BatRow *row) {
    printf("{\"tick\":%d,\"alive\":1,\"x\":\"%016llx\","
           "\"y\":\"%016llx\",\"z\":\"%016llx\","
           "\"vx\":\"%016llx\",\"vy\":\"%016llx\","
           "\"vz\":\"%016llx\",\"yaw\":\"%08x\","
           "\"pitch\":\"%08x\",\"head_yaw\":\"%08x\","
           "\"render_yaw\":\"%08x\","
           "\"body_prev_head_yaw\":\"%08x\","
           "\"body_tick\":%d,\"fall\":\"%08x\","
           "\"hanging\":%d,\"spawn_valid\":%d,"
           "\"spawn\":[%d,%d,%d],\"seed48\":%llu,"
           "\"ticks_existed\":%d,\"living_sound_time\":%d,"
           "\"on_ground\":%d,\"entity_age\":%d,"
           "\"persistence_required\":%d,"
           "\"box\":[\"%016llx\",\"%016llx\",\"%016llx\","
           "\"%016llx\",\"%016llx\",\"%016llx\"]}\n",
           tick,
           (unsigned long long)row->x,
           (unsigned long long)row->y,
           (unsigned long long)row->z,
           (unsigned long long)row->vx,
           (unsigned long long)row->vy,
           (unsigned long long)row->vz,
           row->yaw, row->pitch, row->head_yaw, row->render_yaw,
           row->body_prev_head_yaw, row->body_rotation_tick_counter,
           row->fall_distance, row->hanging, row->spawn_valid,
           row->spawn_x, row->spawn_y, row->spawn_z,
           (unsigned long long)row->seed48,
           row->ticks_existed, row->living_sound_time, row->on_ground,
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

static int oracle_rows(
        const char *scenario, double x, double y, double z, int ticks) {
    GmConfig config;
    if (ticks <= 0 || ticks > 200 || !init_runtime(&runtime, &config)
            || !stage_bat(&runtime, scenario, x, y, z))
        return 0;
    for (int tick = 0; tick < ticks; ++tick) {
        BatRow row;
        tick_once(&runtime);
        if (capture_bat(&runtime, &row))
            print_row(tick, &row);
        else
            printf("{\"tick\":%d,\"alive\":0}\n", tick);
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
                 "%s/bat/generation-0000000000000001/%s",
                 root, files[index]);
        (void)remove(path);
    }
    snprintf(path, sizeof path,
             "%s/bat/generation-0000000000000001", root);
    (void)rmdir(path);
    snprintf(path, sizeof path, "%s/bat/current", root);
    (void)remove(path);
    snprintf(path, sizeof path, "%s/bat/write.lock", root);
    (void)remove(path);
    snprintf(path, sizeof path, "%s/bat", root);
    (void)rmdir(path);
    (void)rmdir(root);
}

static int native_regression(void) {
    GmConfig config;
    BatRow uninterrupted[12], restored;
    GmEntityView views[4];
    GmRuntimeWorldEvent event;
    char save_root[256], error[256];

    CHECK(init_runtime(&runtime, &config)
              && stage_bat(&runtime, "unsupported", 8.5D, 5.0D, 8.5D),
          "stage unsupported hanging Bat");
    int view_count = gm_mobs_fill_views(&runtime.mobs, views, 4);
    int bat_view = -1;
    for (int i = 0; i < view_count; ++i)
        if (views[i].ent_id == BAT_EID) bat_view = i;
    CHECK(bat_view >= 0
              && views[bat_view].ticks_existed == 0
              && (views[bat_view].flags & GM_ENTITY_FLAG_BAT_HANGING),
          "live Bat view carries client age and hanging render bit");
    tick_once(&runtime);
    CHECK(capture_bat(&runtime, &restored) && !restored.hanging
              && gm_runtime_world_event_count(&runtime) == 1
              && gm_runtime_world_event_get(&runtime, 0, &event)
              && event.id == 1025,
          "missing ceiling wakes Bat and emits takeoff event 1025");
    view_count = gm_mobs_fill_views(&runtime.mobs, views, 4);
    bat_view = -1;
    for (int i = 0; i < view_count; ++i)
        if (views[i].ent_id == BAT_EID) bat_view = i;
    CHECK(bat_view >= 0
              && views[bat_view].ticks_existed == 1
              && !(views[bat_view].flags & GM_ENTITY_FLAG_BAT_HANGING),
          "woken Bat view clears hanging bit without losing render age");
    gm_runtime_destroy(&runtime);

    CHECK(init_runtime(&runtime, &config)
              && stage_bat(&runtime, "creative_hang", 8.5D, 5.0D, 8.5D),
          "stage creative-player hanging Bat");
    tick_once(&runtime);
    CHECK(capture_bat(&runtime, &restored) && restored.hanging
              && gm_runtime_world_event_count(&runtime) == 0,
          "near creative player leaves supported Bat hanging");
    gm_runtime_destroy(&runtime);

    CHECK(init_runtime(&runtime, &config)
              && stage_bat(&runtime, "near_wake", 8.5D, 5.0D, 8.5D),
          "stage player-proximity hanging Bat");
    tick_once(&runtime);
    CHECK(capture_bat(&runtime, &restored) && !restored.hanging
              && gm_runtime_world_event_count(&runtime) == 1
              && gm_runtime_world_event_get(&runtime, 0, &event)
              && event.id == 1025,
          "near survival player wakes supported Bat");
    gm_runtime_destroy(&runtime);

    CHECK(init_runtime(&runtime, &config)
              && stage_bat(&runtime, "flight", 8.5D, 5.0D, 8.5D),
          "stage flight checkpoint fixture");
    (void)mkdir(".tmp", 0700);
    snprintf(save_root, sizeof save_root,
             ".tmp/bat-runtime-%ld", (long)getpid());
    clean_save(save_root);
    for (int tick = 0; tick < 12; ++tick) {
        tick_once(&runtime);
        CHECK(capture_bat(&runtime, &uninterrupted[tick]),
              "capture uninterrupted Bat flight");
        if (tick == 5)
            CHECK(gm_native_save_write(
                      &runtime, save_root, "bat", error, sizeof error),
                  error);
    }
    CHECK(gm_native_save_load(
              &runtime, &config, save_root, "bat", error, sizeof error),
          error);
    for (int tick = 6; tick < 12; ++tick) {
        tick_once(&runtime);
        CHECK(capture_bat(&runtime, &restored)
                  && !memcmp(&restored, &uninterrupted[tick], sizeof restored),
              "native save resumes exact Bat private state and RNG");
    }
    gm_runtime_destroy(&runtime);

    CHECK(init_runtime(&runtime, &config)
              && stage_bat(&runtime, "near_reset", 8.5D, 5.0D, 8.5D),
          "stage near Bat age reset");
    tick_once(&runtime);
    CHECK(capture_bat(&runtime, &restored)
              && restored.entity_age == 0,
          "near Bat consumes the soft draw then resets entity age");
    gm_runtime_destroy(&runtime);

    CHECK(init_runtime(&runtime, &config)
              && stage_bat(&runtime, "soft_keep", 8.5D, 5.0D, 8.5D),
          "stage soft Bat despawn miss");
    tick_once(&runtime);
    CHECK(capture_bat(&runtime, &restored)
              && restored.entity_age == 601,
          "soft Bat miss retains the entity and incremented age");
    gm_runtime_destroy(&runtime);

    CHECK(init_runtime(&runtime, &config)
              && stage_bat(&runtime, "soft_drop", 8.5D, 5.0D, 8.5D),
          "stage soft Bat despawn hit");
    tick_once(&runtime);
    CHECK(!capture_bat(&runtime, &restored),
          "soft Bat one-in-800 hit retires the entity");
    gm_runtime_destroy(&runtime);

    CHECK(init_runtime(&runtime, &config)
              && stage_bat(&runtime, "hard_drop", 8.5D, 5.0D, 8.5D),
          "stage hard Bat despawn");
    tick_once(&runtime);
    CHECK(!capture_bat(&runtime, &restored),
          "Bat beyond 128 blocks retires immediately");
    gm_runtime_destroy(&runtime);

    CHECK(init_runtime(&runtime, &config)
              && stage_bat(&runtime, "persistent_far", 8.5D, 5.0D, 8.5D),
          "stage persistent far Bat");
    tick_once(&runtime);
    CHECK(capture_bat(&runtime, &restored)
              && restored.entity_age == 0
              && restored.persistence_required,
          "persistent Bat resets age and ignores the hard cutoff");
    gm_runtime_destroy(&runtime);

    clean_save(save_root);
    puts("PASS Bat: hanging/wake/flight/despawn/persistence/"
         "private-RNG/checkpoint");
    return 1;
}

int main(int argc, char **argv) {
    if (argc == 7 && !strcmp(argv[1], "--oracle"))
        return oracle_rows(
            argv[2], strtod(argv[3], NULL), strtod(argv[4], NULL),
            strtod(argv[5], NULL), atoi(argv[6])) ? 0 : 1;
    if (argc != 1) {
        fprintf(stderr,
                "usage: %s [--oracle SCENARIO PLAYER_X PLAYER_Y PLAYER_Z TICKS]\n",
                argv[0]);
        return 2;
    }
    return native_regression() ? 0 : 1;
}
