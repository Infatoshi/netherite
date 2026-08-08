#include "game/runtime.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define CHECK(condition, message) do {                                      \
    if (!(condition)) {                                                     \
        fprintf(stderr, "FAIL: %s\n", (message));                         \
        return 0;                                                           \
    }                                                                       \
} while (0)

static int init_runtime(GmRuntime *runtime) {
    GmConfig config;
    char error[256];
    gm_config_defaults(&config);
    config.seed = 0;
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.mobs = 0;
    config.weather = 0;
    if (!gm_runtime_init(runtime, &config, error, sizeof error)) {
        fprintf(stderr, "FAIL: %s\n", error);
        return 0;
    }
    return 1;
}

static int same_double(double left, double right) {
    uint64_t left_bits, right_bits;
    memcpy(&left_bits, &left, sizeof left_bits);
    memcpy(&right_bits, &right, sizeof right_bits);
    return left_bits == right_bits;
}

static int close_float(float left, float right) {
    return fabsf(left - right) <= 1.0e-5F;
}

static int no_ai_java_trace(void) {
    GmRuntime runtime;
    GmRuntimeWither wither;
    GmAction idle;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    CHECK(init_runtime(&runtime), "initialize NoAI Wither trace");
    CHECK(gm_runtime_spawn_wither_fixture(
              &runtime, 77, 0.5, 100.0, 0.5,
              0.1, 0.2, -0.05, 12.0F, 3.0F, 7.0F,
              100.0F, 8, 4, 2, 0, 3, 0,
              UINT64_C(1234567), 0, 0.0),
          "spawn exact NoAI Wither fixture");
    CHECK(gm_runtime_restore_wither_base_state(
              &runtime, 77, 1, 1, 300, -1, 0, 0.0F, 0,
              0, 0.0F, 0, 0),
          "restore exact NoAI Wither base state");
    CHECK(gm_runtime_set_wither_head_state(
              &runtime, 77, 1, 0, 0, 0, 0,
              1.0F, 2.0F, 3.0F, 4.0F)
          && gm_runtime_set_wither_head_state(
              &runtime, 77, 2, 0, 0, 0, 0,
              5.0F, 6.0F, 7.0F, 8.0F),
          "restore exact NoAI Wither side heads");

    gm_runtime_tick(&runtime, idle);
    CHECK(gm_runtime_wither_get(&runtime, 0, &wither),
          "NoAI Wither remains loaded after tick one");
    CHECK(same_double(wither.x, 0.5)
              && same_double(wither.y, 100.0)
              && same_double(wither.z, 0.5),
          "NoAI Wither position is frozen by EntityLiving.isServerWorld");
    CHECK(same_double(wither.vx, 0.098)
              && same_double(wither.vy, 0.11760000467300415)
              && same_double(wither.vz, -0.049),
          "NoAI Wither tick-one motion matches the Java trace");
    CHECK(wither.random_seed48 == UINT64_C(221291318361155)
              && !wither.random_have_gaussian
              && same_double(wither.random_gaussian,
                             1.1645162897206938),
          "NoAI Wither tick-one entity RNG matches the Java trace");
    CHECK(wither.ticks_existed == 5 && wither.invul_time == 8
              && wither.living_sound_time == 1
              && wither.fire == 0
              && wither.head_y_rotation[0] == 7.0F
              && wither.head_x_rotation[0] == 2.0F
              && wither.head_y_rotation_prev[0] == 1.0F
              && wither.head_x_rotation_prev[0] == 2.0F,
          "NoAI Wither tick-one counters and head interpolation match Java");

    gm_runtime_tick(&runtime, idle);
    CHECK(gm_runtime_wither_get(&runtime, 0, &wither),
          "NoAI Wither remains loaded after tick two");
    CHECK(same_double(wither.vx, 0.09604)
              && same_double(wither.vy, 0.06914880549545298)
              && same_double(wither.vz, -0.04802),
          "NoAI Wither tick-two motion matches the Java trace");
    CHECK(wither.random_seed48 == UINT64_C(134900806957195)
              && wither.random_have_gaussian
              && same_double(wither.random_gaussian,
                             1.5917078533739135),
          "NoAI Wither tick-two entity RNG matches the Java trace");
    CHECK(wither.ticks_existed == 6 && wither.invul_time == 8
              && wither.living_sound_time == 2
              && wither.head_y_rotation_prev[0] == 7.0F,
          "NoAI Wither tick-two counters match Java");
    gm_runtime_destroy(&runtime);
    return 1;
}

static int damage_and_terminal(void) {
    GmRuntime runtime;
    GmRuntimeWither wither;
    GmRuntimeSoundEvent sound;
    JavaRandom expected_sound;
    GmAction idle;
    int star = -1;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    CHECK(init_runtime(&runtime), "initialize Wither damage fixture");
    CHECK(gm_runtime_spawn_wither_fixture(
              &runtime, 90, 40.5, 100.0, 40.5,
              0.0, 0.0, 0.0, 0.0F, 0.0F, 0.0F,
              100.0F, 1, 0, 0, 0, 0, 0,
              UINT64_C(9), 0, 0.0),
          "spawn Wither damage fixture");
    CHECK(!gm_runtime_wither_damage_fixture(&runtime, 90, 20.0F, 0),
          "birth invulnerability rejects ordinary damage");
    jrand_set_seed48(&expected_sound, UINT64_C(9));
    float death_first = jrand_float(&expected_sound);
    float death_second = jrand_float(&expected_sound);
    CHECK(gm_runtime_wither_damage_fixture(&runtime, 90, 200.0F, 3),
          "out-of-world damage bypasses birth invulnerability");
    CHECK(gm_runtime_sound_event_count(&runtime) == 1
              && gm_runtime_sound_event_get(&runtime, 0, &sound)
              && sound.sound == GM_SOUND_WITHER_DEATH
              && sound.category == GM_SOUND_CATEGORY_HOSTILE
              && sound.eid == 90 && sound.dimension == 0
              && same_double(sound.x, 40.5)
              && same_double(sound.y, 100.0)
              && same_double(sound.z, 40.5)
              && sound.volume == 1.0F
              && close_float(sound.pitch,
                  (death_first - death_second) * 0.2F + 1.0F),
          "lethal Wither damage emits the exact hostile death sound event");
    CHECK(gm_runtime_wither_get(&runtime, 0, &wither)
              && wither.health == 0.0F
              && wither.nether_star_dropped,
          "lethal damage immediately enters death and drops the Nether Star");
    for (int index = 0; index < GM_LIVE_MAX; ++index)
        if (runtime.entities.ents[index].active
                && runtime.entities.ents[index].item == 399) {
            star = index;
            break;
        }
    CHECK(star >= 0 && runtime.entities.ents[star].age == -6000,
          "Nether Star uses EntityItem.setNoDespawn age");
    for (int tick = 0; tick < 19; ++tick)
        gm_runtime_tick(&runtime, idle);
    CHECK(gm_runtime_wither_get(&runtime, 0, &wither)
              && wither.death_time == 19,
          "Wither remains loaded for the first nineteen death ticks");
    gm_runtime_tick(&runtime, idle);
    CHECK(gm_runtime_wither_count(&runtime) == 0
              && runtime.particle_event_count >= 23
              && runtime.particle_events[0].kind
                    == GM_PARTICLE_EXPLOSION_NORMAL
              && runtime.particle_events[19].kind
                    == GM_PARTICLE_EXPLOSION_NORMAL
              && runtime.particle_events[20].kind
                    == GM_PARTICLE_SMOKE_NORMAL,
          "death tick twenty emits its full particle tail before removal");
    gm_runtime_destroy(&runtime);
    return 1;
}

static int damage_window_uses_raw_amount(void) {
    GmRuntime runtime;
    GmRuntimeWither wither;
    GmRuntimeSoundEvent sound;
    CHECK(init_runtime(&runtime), "initialize Wither damage-window fixture");
    CHECK(gm_runtime_spawn_wither_fixture(
              &runtime, 91, 40.5, 100.0, 40.5,
              0.0, 0.0, 0.0, 0.0F, 0.0F, 0.0F,
              300.0F, 0, 0, 0, 0, 0, 0,
              UINT64_C(19), 0, 0.0),
          "spawn Wither damage-window fixture");
    CHECK(gm_runtime_wither_damage_fixture(&runtime, 91, 4.0F, 0),
          "fresh raw damage is accepted");
    CHECK(gm_runtime_wither_get(&runtime, 0, &wither)
              && close_float(wither.health, 296.32F)
              && wither.last_damage == 4.0F,
          "fresh hit stores raw lastDamage after armor");
    CHECK(gm_runtime_sound_event_count(&runtime) == 1
              && gm_runtime_sound_event_get(&runtime, 0, &sound)
              && sound.sound == GM_SOUND_WITHER_HURT
              && sound.category == GM_SOUND_CATEGORY_HOSTILE
              && sound.eid == 91 && sound.volume == 1.0F
              && sound.pitch >= 0.8F && sound.pitch <= 1.2F,
          "fresh surviving hit emits the hostile Wither hurt sound");
    CHECK(gm_runtime_wither_damage_fixture(&runtime, 91, 6.0F, 0),
          "larger raw damage enters resistant-window delta path");
    CHECK(gm_runtime_wither_get(&runtime, 0, &wither)
              && close_float(wither.health, 294.56F)
              && wither.last_damage == 6.0F,
          "resistant-window hit armors the raw delta, not two armored totals");
    CHECK(!gm_runtime_wither_damage_fixture(&runtime, 91, 5.0F, 0),
          "smaller raw damage is rejected inside the resistant window");
    gm_runtime_destroy(&runtime);
    return 1;
}

static int summon_pattern(void) {
    GmRuntime runtime;
    GmRuntimeWither wither;
    JavaRandom expected_math;
    GmAction idle;
    float constructor_head;
    const int center_x = 20, top_y = 100, z = 24;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    CHECK(init_runtime(&runtime), "initialize Wither summon fixture");
    CHECK(gm_runtime_set_entity_id_cursor(&runtime, 7001)
              && gm_runtime_set_world_random_seed48(&runtime, UINT64_C(0))
              && gm_runtime_set_math_random_seed48(&runtime, UINT64_C(0)),
          "pin Wither summon cursors");
    jrand_set_seed48(&expected_math, UINT64_C(0));
    (void)jrand_double(&expected_math);
    (void)jrand_double(&expected_math);
    constructor_head = (float)(jrand_double(&expected_math)
        * 6.28318530717958647692);
    for (int offset = -1; offset <= 1; ++offset) {
        gm_world_set_block_meta(
            runtime.world, center_x + offset, top_y - 1, z, 88, 0);
        gm_world_set_block_meta(
            runtime.world, center_x + offset, top_y, z, 144, 1);
        CHECK(gm_runtime_skull_set(
                  &runtime, 0, center_x + offset, top_y, z, 1, 0),
              "stage Wither skull tile");
    }
    gm_world_set_block_meta(runtime.world, center_x, top_y - 2, z, 88, 0);
    gm_world_set_block_meta(runtime.world, center_x - 1, top_y - 2, z, 0, 0);
    gm_world_set_block_meta(runtime.world, center_x + 1, top_y - 2, z, 0, 0);
    CHECK(!gm_runtime_check_wither_spawn(
              &runtime, center_x - 2, top_y, z),
          "unrelated position cannot consume a complete pattern");
    CHECK(gm_runtime_check_wither_spawn(
              &runtime, center_x + 1, top_y, z),
          "third live skull completes the X-axis Wither pattern");
    CHECK(gm_runtime_wither_count(&runtime) == 1
              && gm_runtime_wither_get(&runtime, 0, &wither)
              && wither.eid == 7001
              && same_double(wither.x, 20.5)
              && same_double(wither.y, 98.55)
              && same_double(wither.z, 24.5)
              && wither.yaw == 90.0F
              && wither.render_yaw_offset == 90.0F
              && wither.prev_render_yaw_offset == 0.0F
              && wither.rotation_yaw_head == constructor_head
              && wither.prev_rotation_yaw_head == 0.0F
              && wither.body_rotation_tick_counter == 0
              && wither.body_prev_render_yaw_head == 0.0F
              && wither.health == 100.0F
              && wither.invul_time == 220
              && runtime.math_random_seed48 == expected_math.seed,
          "summoned Wither starts at Java ignite position and state");
    for (int offset = -1; offset <= 1; ++offset)
        for (int row = 0; row < 3; ++row)
            CHECK(gm_world_block(
                      runtime.world, center_x + offset, top_y - row, z) == 0,
                  "summon consumes every pattern cell");
    CHECK(gm_runtime_skull_count(&runtime) == 0,
          "summon removes all three skull tile entities");
    CHECK(runtime.particle_event_count == 120
              && runtime.particle_events[0].kind == GM_PARTICLE_SNOWBALL
              && runtime.particle_events[119].kind == GM_PARTICLE_SNOWBALL,
          "summon emits all 120 Java snowball particles");
    CHECK(!gm_runtime_check_wither_spawn(
              &runtime, center_x + 1, top_y, z),
          "consumed pattern cannot summon a duplicate Wither");
    gm_runtime_tick(&runtime, idle);
    CHECK(gm_runtime_wither_get(&runtime, 0, &wither)
              && wither.ticks_existed == 1
              && wither.invul_time == 219
              && wither.goal_task_tick == 1
              && wither.target_task_tick == 1
              && wither.invul_task_active
              && wither.rotation_yaw_head == constructor_head + 10.0F
              && wither.prev_rotation_yaw_head == constructor_head
              && wither.render_yaw_offset
                    == constructor_head + 85.0F
              && wither.prev_render_yaw_offset == 90.0F
              && wither.body_rotation_tick_counter == 1
              && same_double(wither.vy, -0.0784000015258789),
          "summoned Wither first tick preserves constructor head/body state");
    gm_runtime_destroy(&runtime);
    return 1;
}

static int checkpoint_continuation(void) {
    GmRuntime runtime;
    GmRuntimeWither expected_wither, actual_wither;
    GmRuntimeProjectile expected_skull;
    char path[160];
    int skull_slot = -1;
    snprintf(path, sizeof path,
             ".tmp/test_wither_runtime_checkpoint.%ld.bin", (long)getpid());
    CHECK(init_runtime(&runtime), "initialize Wither checkpoint fixture");
    CHECK(gm_runtime_spawn_wither_fixture(
              &runtime, 7101, 8.5, 90.0, 12.5,
              0.125, 0.25, -0.0625, 30.0F, 4.0F, 7.0F,
              149.0F, 17, 9, 3, 0, 6, 11,
              UINT64_C(0x123456789abc), 1, -0.75)
              && gm_runtime_restore_wither_base_state(
                  &runtime, 7101, 0, 1, 299, 24, 0, 1.25F, 0,
                  -7, 3.5F, 5, 1)
              && gm_runtime_restore_wither_ai_state(
                  &runtime, 7101, 0, 0, 0, 0, 13,
                  0, 0, 0, 0, 0, 0,
                  8, 11, 1, 0, -1, 0)
              && gm_runtime_restore_wither_rotation_state(
                  &runtime, 7101, 22.5F, 21.5F, 6.5F,
                  4, 18.25F)
              && gm_runtime_set_wither_head_state(
                  &runtime, 7101, 1, 0, 0, 12, 7,
                  13.0F, -4.0F, 12.0F, -3.0F)
              && gm_runtime_set_wither_head_state(
                  &runtime, 7101, 2, 0, 0, 14, 8,
                  15.0F, -6.0F, 14.0F, -5.0F),
          "stage every Wither continuation field");
    CHECK(gm_runtime_spawn_wither_skull_fixture(
              &runtime, 7102, 7101, 9.0, 92.0, 12.0,
              0.1, 0.2, 0.3, -0.01, 0.02, -0.03,
              40.0F, -10.0F, 1, 6, 7),
          "stage a mid-flight Wither skull");
    CHECK(gm_runtime_wither_get(&runtime, 0, &expected_wither),
          "read staged Wither before checkpoint");
    for (int slot = 0; slot < GM_RUNTIME_PROJECTILES; ++slot)
        if (runtime.projectiles[slot].active
                && runtime.projectiles[slot].eid == 7102) {
            skull_slot = slot;
            expected_skull = runtime.projectiles[slot];
            break;
        }
    CHECK(skull_slot >= 0
              && gm_runtime_write_checkpoint(&runtime, path),
          "write Wither lifecycle checkpoint");
    memset(&runtime.withers[0], 0, sizeof runtime.withers[0]);
    memset(&runtime.projectiles[skull_slot], 0,
           sizeof runtime.projectiles[skull_slot]);
    runtime.wither_count = 0;
    CHECK(gm_runtime_load_checkpoint(&runtime, path),
          "reload Wither lifecycle checkpoint");
    CHECK(gm_runtime_wither_get(&runtime, 0, &actual_wither)
              && memcmp(&expected_wither, &actual_wither,
                        sizeof expected_wither) == 0,
          "checkpoint preserves every Wither continuation byte");
    CHECK(memcmp(&expected_skull, &runtime.projectiles[skull_slot],
                 sizeof expected_skull) == 0
              && runtime.loaded_entity_order_count == 2
              && runtime.loaded_entity_order[0] == 7101
              && runtime.loaded_entity_order[1] == 7102,
          "checkpoint preserves Wither-skull state and causal order");
    (void)remove(path);
    gm_runtime_destroy(&runtime);
    return 1;
}

static int autonomous_targeting(void) {
    GmRuntime runtime;
    GmRuntimeWither wither;
    GmAction idle;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    CHECK(init_runtime(&runtime), "initialize Wither targeting fixture");
    CHECK(gm_runtime_spawn_wither_fixture(
              &runtime, 7201, 40.5, 100.0, 40.5,
              0.0, 0.0, 0.0, 0.0F, 0.0F, 0.0F,
              300.0F, 0, 0, 0, 0, 0, 0,
              UINT64_C(0x23456789abcd), 0, 0.0)
              && gm_runtime_restore_wither_base_state(
                  &runtime, 7201, 0, 1, 300, -1, 0, 0.0F, 0,
                  0, 0.0F, 0, 0)
              && gm_runtime_spawn_mob_fixture(
                  &runtime, GM_MOB_ZOMBIE, 7202,
                  41.5, 100.0, 40.5, 0.0, 0.0, 0.0,
                  0.0F, 20.0F, 1, 0, 0, 0)
              && gm_runtime_spawn_mob_fixture(
                  &runtime, GM_MOB_GHAST, 7203,
                  42.5, 100.0, 40.5, 0.0, 0.0, 0.0,
                  0.0F, 10.0F, 1, 0, 0, 0)
              && gm_runtime_spawn_mob_fixture(
                  &runtime, GM_MOB_PIG, 7204,
                  44.5, 100.0, 40.5, 0.0, 0.0, 0.0,
                  0.0F, 10.0F, 1, 0, 0, 0),
          "stage undead, forbidden-class, and eligible targets");
    gm_runtime_tick(&runtime, idle);
    CHECK(gm_runtime_wither_get(&runtime, 0, &wither)
              && wither.nearest_target_task_active
              && !wither.hurt_target_task_active
              && wither.target_eid == 7204
              && !wither.target_is_player
              && wither.ranged_task_active
              && wither.watched_target[0] == 7204,
          "main target task chooses the nearest eligible EntityLiving");
    CHECK((wither.watched_target[1] == 7203
                  || wither.watched_target[1] == 7204)
              && (wither.watched_target[2] == 7203
                  || wither.watched_target[2] == 7204)
              && !wither.watched_target_is_player[1]
              && !wither.watched_target_is_player[2],
          "side heads accept non-undead classes the main goal forbids");
    gm_runtime_destroy(&runtime);

    CHECK(init_runtime(&runtime),
          "initialize Wither player-side targeting fixture");
    runtime.server_player.ent.posX = 44.5 - (double)runtime.ox;
    runtime.server_player.ent.posY = 100.0;
    runtime.server_player.ent.posZ = 40.5 - (double)runtime.oz;
    CHECK(gm_runtime_set_player_entity_id(&runtime, 7302)
              && gm_runtime_spawn_wither_fixture(
                  &runtime, 7301, 40.5, 100.0, 40.5,
                  0.0, 0.0, 0.0, 0.0F, 0.0F, 0.0F,
                  300.0F, 0, 0, 0, 0, 0, 0,
                  UINT64_C(0x3456789abcde), 0, 0.0)
              && gm_runtime_restore_wither_base_state(
                  &runtime, 7301, 0, 1, 300, -1, 0, 0.0F, 0,
                  0, 0.0F, 0, 0),
          "stage a player-only side-head target");
    gm_runtime_tick(&runtime, idle);
    CHECK(gm_runtime_wither_get(&runtime, 0, &wither)
              && wither.target_eid == 0
              && !wither.nearest_target_task_active
              && wither.watched_target[0] == 0
              && wither.watched_target[1] == 7302
              && wither.watched_target[2] == 7302
              && wither.watched_target_is_player[1]
              && wither.watched_target_is_player[2],
          "players are side-head candidates but not main nearest targets");
    gm_runtime_destroy(&runtime);
    return 1;
}

static int skull_impact_feedback(void) {
    GmRuntime runtime;
    GmMobEvent event;
    JavaRandom expected;
    int slot;
    CHECK(init_runtime(&runtime), "initialize Wither-skull impact fixture");
    CHECK(gm_runtime_spawn_mob_fixture(
              &runtime, GM_MOB_PIG, 7401,
              40.5, 100.0, 40.5, 0.0, 0.0, 0.0,
              0.0F, 10.0F, 1, 0, 0, 0)
              && gm_mobs_set_entity_random_state(
                  &runtime.mobs, 7401, UINT64_C(0x456789abcdef), 0, 0.0),
          "stage an adult Pig with a pinned Entity.rand cursor");
    slot = gm_mobs_find_slot_by_eid(&runtime.mobs, 7401);
    CHECK(slot > 0, "resolve staged Pig slot");
    runtime.mobs.entity_living_sound_time[slot] = -106;
    jrand_set_seed48(&expected, UINT64_C(0x456789abcdef));
    (void)jrand_double(&expected);
    (void)jrand_double(&expected);
    float first = jrand_float(&expected);
    float second = jrand_float(&expected);
    CHECK(gm_mobs_wither_skull_hit(
              &runtime.mobs, slot, 8.0F, &runtime.entities) == 1,
          "surviving Pig accepts Wither-skull damage");
    EwStore *store = runtime.mobs.current
        ? &runtime.mobs.b : &runtime.mobs.a;
    CHECK(store->health[slot] == 2.0F
              && runtime.mobs.entity_random[slot].random.seed
                    == expected.seed
              && runtime.mobs.entity_living_sound_time[slot] == -120,
          "skull impact preserves Java hurt RNG and sound clock");
    CHECK(gm_mobs_event_count(&runtime.mobs) == 2
              && gm_mobs_event_get(&runtime.mobs, 0, &event)
              && event.kind == GM_MOB_EVENT_ENTITY_STATUS
              && event.eid == 7401 && event.data == 2
              && gm_mobs_event_get(&runtime.mobs, 1, &event)
              && event.kind == GM_MOB_EVENT_SOUND
              && event.eid == 7401 && event.data == GM_MOB_SOUND_PIG_HURT
              && close_float(event.pitch,
                  (first - second) * 0.2F + 1.0F),
          "skull impact emits Java status and Pig hurt sound");
    gm_runtime_destroy(&runtime);
    return 1;
}

static int block_break_drop_constructor(void) {
    GmRuntime runtime;
    GmRuntimeWither wither;
    GmAction idle;
    JavaRandom generator, entity_random, server_uuid;
    JavaRandom expected_sound;
    GmRuntimeSoundEvent sound;
    int64_t uuid_most, uuid_least;
    int item_slot = -1;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    CHECK(init_runtime(&runtime), "initialize Wither block-break fixture");
    CHECK(gm_runtime_set_entity_id_cursor(&runtime, 7502)
              && gm_runtime_set_sound_random_seed48(
                  &runtime, UINT64_C(0x3456789abcde))
              && gm_runtime_set_entity_seed_generator_seed48(
                  &runtime, UINT64_C(0x123456789abc))
              && gm_runtime_set_server_uuid_random_seed48(
                  &runtime, UINT64_C(0x23456789abcd))
              && gm_runtime_spawn_wither_fixture(
                  &runtime, 7501, 40.5, 100.0, 40.5,
                  0.0, 0.0, 0.0, 0.0F, 0.0F, 0.0F,
                  300.0F, 0, 0, 0, 0, 0, 1,
                  UINT64_C(0x56789abcdef0), 0, 0.0)
              && gm_runtime_restore_wither_base_state(
                  &runtime, 7501, 0, 1, 300, -1, 0, 0.0F, 0,
                  0, 0.0F, 0, 0),
          "stage a due Wither block-break clock");
    gm_world_set_block(runtime.world, 40, 101, 40, 1);
    gm_world_set_block(runtime.world, 41, 101, 41, 7);
    jrand_set_seed48(&generator, UINT64_C(0x123456789abc));
    jrand_set(&entity_random, jrand_long(&generator));
    jrand_set_seed48(&server_uuid, UINT64_C(0x23456789abcd));
    uuid_most = (jrand_long(&server_uuid) & INT64_C(-61441))
        | INT64_C(16384);
    uuid_least = (jrand_long(&server_uuid)
        & INT64_C(4611686018427387903)) | INT64_MIN;
    jrand_set_seed48(&expected_sound, UINT64_C(0x3456789abcde));
    float break_first = jrand_float(&expected_sound);
    float break_second = jrand_float(&expected_sound);
    gm_runtime_tick(&runtime, idle);
    CHECK(gm_runtime_wither_get(&runtime, 0, &wither)
              && wither.block_break_counter == 0
              && gm_world_block(runtime.world, 40, 101, 40) == 0
              && gm_world_block(runtime.world, 41, 101, 41) == 7,
          "due block break destroys stone and retains bedrock");
    CHECK(gm_runtime_sound_event_count(&runtime) == 2
              && gm_runtime_sound_event_get(&runtime, 1, &sound)
              && sound.sound == GM_SOUND_WITHER_BREAK_BLOCK
              && sound.category == GM_SOUND_CATEGORY_HOSTILE
              && sound.eid == 0 && sound.volume == 2.0F
              && close_float(sound.pitch,
                  1.0F + (break_first - break_second) * 0.2F),
          "successful griefing emits exact Wither block-break audio");
    for (int slot = 0; slot < GM_LIVE_MAX; ++slot)
        if (runtime.entities.ents[slot].active
                && runtime.entities.ents[slot].eid == 7502) {
            item_slot = slot;
            break;
        }
    CHECK(item_slot >= 0
              && runtime.entities.ents[item_slot].item == 4
              && runtime.entities.ents[item_slot].random_seed48
                    == entity_random.seed
              && runtime.entity_seed_generator_seed48 == generator.seed
              && runtime.entities.ents[item_slot].uuid_present
              && runtime.entities.ents[item_slot].uuid_most == uuid_most
              && runtime.entities.ents[item_slot].uuid_least == uuid_least
              && runtime.server_uuid_random_seed48 == server_uuid.seed
              && runtime.loaded_entity_order_count == 2
              && runtime.loaded_entity_order[1] == 7502,
          "block drop preserves Entity random and UUID constructor streams");
    gm_runtime_destroy(&runtime);
    return 1;
}

static int audio_lifecycle(void) {
    GmRuntime runtime;
    GmRuntimeSoundEvent sound;
    GmRuntimeWorldEvent world_event;
    GmAction idle;
    JavaRandom expected;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;

    CHECK(init_runtime(&runtime), "initialize Wither ambient-audio fixture");
    CHECK(gm_runtime_spawn_wither_fixture(
              &runtime, 7701, 40.5, 100.0, 40.5,
              0.0, 0.0, 0.0, 0.0F, 0.0F, 0.0F,
              300.0F, 0, 0, 0, 0, 0, 0,
              UINT64_C(0x456789abcdef), 0, 0.0)
              && gm_runtime_restore_wither_base_state(
                  &runtime, 7701, 1, 1, 300, -1, 0, 0.0F, 0,
                  1000, 0.0F, 0, 0),
          "stage a due Wither living-sound clock");
    jrand_set_seed48(&expected, UINT64_C(0x456789abcdef));
    (void)jrand_int_bound(&expected, 1000);
    float ambient_first = jrand_float(&expected);
    float ambient_second = jrand_float(&expected);
    gm_runtime_tick(&runtime, idle);
    CHECK(gm_runtime_sound_event_count(&runtime) == 1
              && gm_runtime_sound_event_get(&runtime, 0, &sound)
              && sound.sound == GM_SOUND_WITHER_AMBIENT
              && sound.category == GM_SOUND_CATEGORY_HOSTILE
              && sound.eid == 7701 && sound.volume == 1.0F
              && close_float(sound.pitch,
                  (ambient_first - ambient_second) * 0.2F + 1.0F),
          "living-sound scheduler emits exact Wither ambient audio");
    gm_runtime_destroy(&runtime);

    CHECK(init_runtime(&runtime), "initialize Wither spawn-audio fixture");
    CHECK(gm_runtime_spawn_wither_fixture(
              &runtime, 7801, 40.5, 100.0, 40.5,
              0.0, 0.0, 0.0, 0.0F, 0.0F, 0.0F,
              300.0F, 1, 0, 0, 0, 0, 0,
              UINT64_C(0x56789abcdef0), 0, 0.0),
          "stage the last Wither invulnerability tick");
    gm_runtime_tick(&runtime, idle);
    CHECK(gm_runtime_world_event_count(&runtime) == 1
              && gm_runtime_world_event_get(&runtime, 0, &world_event)
              && world_event.id == 1023
              && gm_runtime_sound_event_count(&runtime) == 1
              && gm_runtime_sound_event_get(&runtime, 0, &sound)
              && sound.sound == GM_SOUND_WITHER_SPAWN
              && sound.category == GM_SOUND_CATEGORY_HOSTILE
              && sound.eid == 0 && sound.volume == 1.0F
              && sound.pitch == 1.0F
              && same_double(sound.x, 40.5)
              && same_double(sound.y, 100.5)
              && same_double(sound.z, 40.5),
          "birth explosion emits world event 1023 and exact spawn audio");
    gm_runtime_destroy(&runtime);

    CHECK(init_runtime(&runtime), "initialize Wither shoot-audio fixture");
    CHECK(gm_runtime_set_sound_random_seed48(
              &runtime, UINT64_C(0x6789abcdef01))
              && gm_runtime_spawn_wither_fixture(
                  &runtime, 7901, 40.5, 100.0, 40.5,
                  0.0, 0.0, 0.0, 0.0F, 0.0F, 0.0F,
                  300.0F, 0, 0, 0, 0, 0, 0,
                  UINT64_C(0x789abcdef012), 0, 0.0)
              && gm_runtime_restore_wither_base_state(
                  &runtime, 7901, 0, 1, 300, -1, 0, 0.0F, 0,
                  0, 0.0F, 0, 0)
              && gm_runtime_restore_wither_ai_state(
                  &runtime, 7901, 7902, 0, 0, 0, 0,
                  0, 0, 0, 0, 0, 1,
                  1, 1, 0, 1, 1, 0)
              && gm_runtime_set_wither_head_state(
                  &runtime, 7901, 1, 0, 0, 100, 0,
                  0.0F, 0.0F, 0.0F, 0.0F)
              && gm_runtime_set_wither_head_state(
                  &runtime, 7901, 2, 0, 0, 100, 0,
                  0.0F, 0.0F, 0.0F, 0.0F)
              && gm_runtime_spawn_mob_fixture(
                  &runtime, GM_MOB_PIG, 7902,
                  44.5, 100.0, 40.5, 0.0, 0.0, 0.0,
                  0.0F, 10.0F, 1, 0, 0, 0),
          "stage a due visible main-head ranged attack");
    jrand_set_seed48(&expected, UINT64_C(0x6789abcdef01));
    float shoot_first = jrand_float(&expected);
    float shoot_second = jrand_float(&expected);
    gm_runtime_tick(&runtime, idle);
    CHECK(gm_runtime_world_event_count(&runtime) == 1
              && gm_runtime_world_event_get(&runtime, 0, &world_event)
              && world_event.id == 1024
              && gm_runtime_sound_event_count(&runtime) == 1
              && gm_runtime_sound_event_get(&runtime, 0, &sound)
              && sound.sound == GM_SOUND_WITHER_SHOOT
              && sound.category == GM_SOUND_CATEGORY_HOSTILE
              && sound.eid == 0 && sound.volume == 2.0F
              && close_float(sound.pitch,
                  1.0F + (shoot_first - shoot_second) * 0.2F),
          "main-head launch emits world event 1024 and exact shoot audio");
    gm_runtime_destroy(&runtime);
    return 1;
}

static int player_attack_death_path(void) {
    GmRuntime runtime;
    GmRuntimeWither wither;
    GmAction attack, idle;
    JavaRandom math_random, generator, entity_random, server_uuid;
    int64_t uuid_most, uuid_least;
    int item_slot = -1;
    memset(&attack, 0, sizeof attack);
    memset(&idle, 0, sizeof idle);
    attack.hotbar_sel = idle.hotbar_sel = -1;
    attack.attack = 1;
    attack.do_break = 1;
    CHECK(init_runtime(&runtime), "initialize playable Wither attack fixture");
    gm_runtime_set_pose_state(
        &runtime, 40.5, 4.0, 38.5, 0.0F, 0.0F,
        0.0, 0.0, 0.0, 1, 0.0F);
    runtime.mobs.player_ticks_since_last_swing = 100;
    CHECK(gm_runtime_set_entity_id_cursor(&runtime, 7602)
              && gm_runtime_spawn_wither_fixture(
                  &runtime, 7601, 40.5, 4.0, 40.5,
                  0.0, 0.0, 0.0, 0.0F, 0.0F, 0.0F,
                  0.5F, 0, 0, 0, 0, 0, 0,
                  UINT64_C(0x456789abcdef), 0, 0.0),
          "stage a lethal Wither in ordinary player reach");
    gm_runtime_tick(&runtime, attack);
    CHECK(gm_runtime_wither_get(&runtime, 0, &wither)
              && wither.health == 0.5F,
          "physical press queues the delayed server attack");
    CHECK(gm_runtime_set_math_random_seed48(
                  &runtime, UINT64_C(0x0123456789ab))
              && gm_runtime_set_entity_seed_generator_seed48(
                  &runtime, UINT64_C(0x123456789abc))
              && gm_runtime_set_server_uuid_random_seed48(
                  &runtime, UINT64_C(0x23456789abcd)),
          "pin lethal item constructor cursors");
    jrand_set_seed48(&math_random, UINT64_C(0x0123456789ab));
    for (int draw = 0; draw < 4; ++draw) (void)jrand_double(&math_random);
    jrand_set_seed48(&generator, UINT64_C(0x123456789abc));
    jrand_set(&entity_random, jrand_long(&generator));
    jrand_set_seed48(&server_uuid, UINT64_C(0x23456789abcd));
    uuid_most = (jrand_long(&server_uuid) & INT64_C(-61441))
        | INT64_C(16384);
    uuid_least = (jrand_long(&server_uuid)
        & INT64_C(4611686018427387903)) | INT64_MIN;
    gm_runtime_tick(&runtime, idle);
    CHECK(gm_runtime_wither_get(&runtime, 0, &wither)
              && wither.health == 0.0F && wither.death_time == 1
              && wither.recently_hit == 99 && wither.attacking_player,
          "ordinary CPacketUseEntity path starts credited Wither death");
    for (int slot = 0; slot < GM_LIVE_MAX; ++slot)
        if (runtime.entities.ents[slot].active
                && runtime.entities.ents[slot].eid == 7602) {
            item_slot = slot;
            break;
        }
    CHECK(item_slot >= 0
              && runtime.entities.ents[item_slot].item == 399
              && runtime.entities.ents[item_slot].age == -5999
              && runtime.entities.ents[item_slot].random_seed48
                    == entity_random.seed
              && runtime.entities.ents[item_slot].uuid_present
              && runtime.entities.ents[item_slot].uuid_most == uuid_most
              && runtime.entities.ents[item_slot].uuid_least == uuid_least
              && runtime.math_random_seed48 == math_random.seed
              && runtime.entity_seed_generator_seed48 == generator.seed
              && runtime.server_uuid_random_seed48 == server_uuid.seed,
          "playable lethal hit constructs and same-tick updates the Nether Star");
    gm_runtime_destroy(&runtime);
    return 1;
}

int main(void) {
    if (!no_ai_java_trace() || !damage_and_terminal()
            || !damage_window_uses_raw_amount() || !summon_pattern()
            || !checkpoint_continuation() || !autonomous_targeting()
            || !skull_impact_feedback() || !block_break_drop_constructor()
            || !audio_lifecycle() || !player_attack_death_path())
        return 1;
    fprintf(stderr, "wither runtime: PASS\n");
    return 0;
}
