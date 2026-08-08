#include "game/runtime.h"
#include "container_click.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
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

static uint32_t float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static uint64_t double_bits(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static int find_drop(const GmLiveSim *drops, int item) {
    for (int slot = 0; slot < GM_LIVE_MAX; ++slot)
        if (drops->ents[slot].active && drops->ents[slot].item == item)
            return slot;
    return -1;
}

static int exact_family_and_inventory(void) {
    static const int types[] = {
        GM_MOB_HORSE, GM_MOB_DONKEY, GM_MOB_MULE,
        GM_MOB_SKELETON_HORSE, GM_MOB_ZOMBIE_HORSE,
    };
    GmMobLive mobs;
    gm_mobs_init(&mobs, 0);
    for (int index = 0; index < 5; ++index) {
        int eid = 100 + index;
        int chested = index == 1 || index == 2;
        int trap = index == 3;
        GmHorseState state;
        CHECK(gm_mobs_spawn_horse_exact(
                  &mobs, types[index], eid,
                  8.5 + index, 100.0, 12.5,
                  0.125, -0.25, 0.375, 27.0F, 17.0F, 1,
                  23.0 + index, 0.20 + index * 0.01,
                  0.55 + index * 0.02, -1200 + index,
                  GM_HORSE_TAME | GM_HORSE_BRED,
                  41 + index, index == 0 ? 0x304 : 0,
                  0, chested, trap, trap ? 17999 : 0) > 0,
              "spawn every exact horse-family subtype");
        CHECK(gm_mobs_get_horse_state(&mobs, eid, &state)
                  && state.type == types[index] && state.eid == eid
                  && state.growing_age == -1200 + index
                  && state.status == (GM_HORSE_TAME | GM_HORSE_BRED)
                  && state.temper == 41 + index
                  && state.chested == chested && state.trap == trap
                  && state.max_health == 23.0 + index
                  && state.movement_speed == 0.20 + index * 0.01
                  && state.jump_strength == 0.55 + index * 0.02,
              "exact horse state round-trips without constructor rerolls");
    }

    CHECK(gm_mobs_set_horse_inventory(
              &mobs, 100, 0, ic_mk(329, 1, 0))
              && gm_mobs_set_horse_inventory(
                  &mobs, 100, 1, ic_mk(419, 1, 7)),
          "horse accepts its saddle and armor slots");
    CHECK(!gm_mobs_set_horse_inventory(
              &mobs, 100, 2, ic_mk(1, 1, 0))
              && !gm_mobs_set_horse_inventory(
                  &mobs, 101, 1, ic_mk(417, 1, 0))
              && !gm_mobs_set_horse_inventory(
                  &mobs, 101, 0, ic_mk(1, 1, 0)),
          "horse inventory rejects absent and subtype-invalid slots");
    CHECK(gm_mobs_set_horse_inventory(
              &mobs, 101, 0, ic_mk(329, 1, 0))
              && gm_mobs_set_horse_inventory(
                  &mobs, 101, 2, ic_mk(264, 3, 0))
              && gm_mobs_set_horse_inventory(
                  &mobs, 101, 16, ic_mk(260, 7, 0)),
          "chested donkey exposes all fifteen storage slots");
    {
        GmHorseState horse, donkey;
        CHECK(gm_mobs_get_horse_state(&mobs, 100, &horse)
                  && horse.armor == 3
                  && (horse.status & GM_HORSE_SADDLED)
                  && horse.inventory[1].item == 419
                  && gm_mobs_get_horse_state(&mobs, 101, &donkey)
                  && donkey.inventory[2].item == 264
                  && donkey.inventory[16].count == 7,
              "inventory writes update observable horse metadata");
    }
    return 1;
}

static int horse_inventory_container(void) {
    GmRuntime runtime;
    GmHorseState horse;
    ICStack cursor;
    CHECK(init_runtime(&runtime), "initialize horse inventory container world");
    CHECK(gm_runtime_spawn_horse_fixture(
              &runtime, GM_MOB_HORSE, 180,
              8.5, 4.0, 8.5, 0.0, 0.0, 0.0,
              0.0F, 20.0F, 0, 20.0, 0.225, 0.7,
              0, GM_HORSE_TAME, 0, 6 | (4 << 8), 0, 0, 0, 0,
              0, 0, 0)
              && gm_runtime_open_horse_inventory(&runtime, 180)
              && runtime.container == 8
              && runtime.active_horse_eid == 180,
          "tame adult horse opens ContainerHorseInventory");
    CHECK(gm_runtime_set_inventory(&runtime, 0, 329, 1, 0)
              && gm_container_click(
                  &runtime, 0, 0, CC_CLICK_QUICK_MOVE)
              && gm_runtime_set_inventory(&runtime, 1, 419, 1, 0)
              && gm_container_click(
                  &runtime, 1, 0, CC_CLICK_QUICK_MOVE)
              && gm_mobs_get_horse_state(&runtime.mobs, 180, &horse)
              && horse.inventory[0].item == 329
              && horse.inventory[1].item == 419
              && horse.armor == 3
              && (horse.status & GM_HORSE_SADDLED),
          "shift-click routes armor before saddle into exact horse slots");
    CHECK(gm_runtime_set_inventory(&runtime, 2, 1, 64, 0)
              && gm_container_click(
                  &runtime, 2, 0, CC_CLICK_QUICK_MOVE)
              && isr_get_stack(&runtime.player.inv, 2).count == 64,
          "unchested horse rejects shift-click storage items");
    CHECK(gm_container_click(
              &runtime, GMC_HORSE0 + 1, 0, CC_CLICK_PICKUP),
          "horse armor slot accepts pickup");
    cursor = gm_player_cursor();
    CHECK(cursor.item == 419 && cursor.count == 1
              && gm_mobs_get_horse_state(&runtime.mobs, 180, &horse)
              && horse.armor == 0,
          "taking armor clears synchronized horse armor metadata");
    gm_player_cursor_set(ic_empty());
    gm_runtime_destroy(&runtime);

    CHECK(init_runtime(&runtime), "initialize donkey inventory container world");
    CHECK(gm_runtime_spawn_horse_fixture(
              &runtime, GM_MOB_DONKEY, 181,
              8.5, 4.0, 8.5, 0.0, 0.0, 0.0,
              0.0F, 15.0F, 0, 15.0, 0.175, 0.5,
              0, GM_HORSE_TAME, 0, 0, 0, 1, 0, 0,
              0, 0, 0)
              && gm_runtime_open_horse_inventory(&runtime, 181)
              && gm_runtime_set_inventory(&runtime, 0, 264, 3, 0)
              && gm_container_click(
                  &runtime, 0, 0, CC_CLICK_QUICK_MOVE)
              && gm_mobs_get_horse_state(&runtime.mobs, 181, &horse)
              && horse.inventory[2].item == 264
              && horse.inventory[2].count == 3,
          "chested donkey shift-click fills first storage slot");
    CHECK(gm_container_click(
              &runtime, GMC_HORSE0 + 2, 0, CC_CLICK_QUICK_MOVE)
              && gm_mobs_get_horse_state(&runtime.mobs, 181, &horse)
              && isr_is_empty(&horse.inventory[2])
              && isr_get_stack(&runtime.player.inv, 8).item == 264,
          "horse storage shift-click returns through reverse player order");
    gm_runtime_destroy(&runtime);
    return 1;
}

static int donkey_chest_interaction(void) {
    GmMobLive mobs;
    IsrInv inventory;
    GmHorseState horse;
    GmMobEvent sound;
    gm_mobs_init(&mobs, 0);
    isr_init(&inventory);
    CHECK(gm_mobs_spawn_horse_exact(
              &mobs, GM_MOB_DONKEY, 182,
              0.5, 100.0, 0.5, 0.0, 0.0, 0.0,
              0.0F, 15.0F, 0, 15.0, 0.175, 0.5,
              0, GM_HORSE_TAME, 0, 0, 0, 0, 0, 0) > 0
              && gm_mobs_set_entity_random_state(
                  &mobs, 182, UINT64_C(1234), 0, 0.0),
          "stage tame unchested donkey with exact entity RNG");
    isr_set_stack(&inventory, 0, ic_mk(54, 2, 0));
    CHECK(gm_mobs_horse_equip_chest(&mobs, 182, &inventory, 0, 0)
              && gm_mobs_get_horse_state(&mobs, 182, &horse)
              && horse.chested
              && isr_get_stack(&inventory, 0).count == 1
              && gm_mobs_event_count(&mobs) == 1
              && gm_mobs_event_get(&mobs, 0, &sound)
              && sound.kind == GM_MOB_EVENT_SOUND
              && sound.data == GM_MOB_SOUND_DONKEY_CHEST
              && sound.volume == 1.0F,
          "tame donkey consumes chest and emits Java chest sound");
    CHECK(!gm_mobs_horse_equip_chest(&mobs, 182, &inventory, 0, 0)
              && isr_get_stack(&inventory, 0).count == 1,
          "already-chested donkey interaction is atomic");
    return 1;
}

static int feeding_semantics(void) {
    GmMobLive mobs;
    GmHorseState state;
    IsrInv inventory;
    gm_mobs_init(&mobs, 0);
    isr_init(&inventory);
    CHECK(gm_mobs_spawn_horse_exact(
              &mobs, GM_MOB_HORSE, 200,
              0.5, 100.0, 0.5, 0.0, 0.0, 0.0,
              0.0F, 10.0F, 1, 20.0, 0.225, 0.7,
              -1000, 0, 90, 0, 0, 0, 0, 0) > 0,
          "spawn feed fixture");
    isr_set_stack(&inventory, 0, ic_mk(296, 2, 0));
    CHECK(gm_mobs_horse_feed(&mobs, 200, &inventory, 0, 0)
              && gm_mobs_get_horse_state(&mobs, 200, &state)
              && state.health == 12.0F && state.growing_age == -600
              && state.temper == 93
              && isr_get_stack(&inventory, 0).count == 1
              && (state.status & GM_HORSE_MOUTH_OPEN),
          "wheat heals, grows, tempers, animates, and consumes exactly one");

    isr_set_stack(&inventory, 0, ic_mk(322, 1, 0));
    CHECK(gm_mobs_horse_feed(&mobs, 200, &inventory, 0, 1)
              && gm_mobs_get_horse_state(&mobs, 200, &state)
              && state.health == 20.0F && state.growing_age == 0
              && state.temper == 100
              && isr_get_stack(&inventory, 0).count == 1,
          "creative golden apple preserves the held stack and clamps state");
    mobs.horse_status[gm_mobs_find_slot_by_eid(&mobs, 200)] |= GM_HORSE_TAME;
    isr_set_stack(&inventory, 0, ic_mk(396, 1, 0));
    CHECK(gm_mobs_horse_feed(&mobs, 200, &inventory, 0, 0)
              && mobs.sheep_in_love[
                    gm_mobs_find_slot_by_eid(&mobs, 200)] == 600
              && isr_get_stack(&inventory, 0).count == 0,
          "golden carrot starts the exact tame-adult love window");
    isr_set_stack(&inventory, 0, ic_mk(296, 1, 0));
    CHECK(!gm_mobs_horse_feed(&mobs, 200, &inventory, 0, 0)
              && isr_get_stack(&inventory, 0).count == 1,
          "irrelevant feeding leaves a full tame adult and inventory unchanged");
    return 1;
}

static int taming_attempt_semantics(void) {
    static const int types[] = {
        GM_MOB_HORSE, GM_MOB_DONKEY, GM_MOB_MULE,
    };
    const uint64_t owner_most = UINT64_C(0x4675d88ca2a73c16);
    const uint64_t owner_least = UINT64_C(0xbf9c70b5a7108798);
    for (int index = 0; index < 3; ++index) {
        GmMobLive mobs;
        GmHorseState state;
        GmMobEvent first, second;
        int eid = 210 + index;
        int angry = types[index] == GM_MOB_HORSE
            ? GM_MOB_SOUND_HORSE_ANGRY : GM_MOB_SOUND_DONKEY_ANGRY;

        gm_mobs_init(&mobs, 0);
        gm_mobs_set_represented_player_uuid(
            &mobs, owner_most, owner_least);
        CHECK(gm_mobs_spawn_horse_exact(
                  &mobs, types[index], eid,
                  0.5, 100.0, 0.5, 0.0, 0.0, 0.0,
                  0.0F, 20.0F, 0, 20.0, 0.225, 0.7,
                  0, 0, 0, 0, 0, 0, 0, 0) > 0
                  && gm_mobs_horse_mount(&mobs, eid)
                  && gm_mobs_set_entity_random_state(
                      &mobs, eid, UINT64_C(1), 0, 0.0)
                  && gm_mobs_horse_crazy_attempt(&mobs, eid)
                      == GM_HORSE_TAME_NO_TRIGGER
                  && gm_mobs_get_horse_state(&mobs, eid, &state)
                  && state.ridden && !state.owner_present
                  && !(state.status & GM_HORSE_TAME)
                  && state.temper == 0
                  && mobs.entity_random[
                      gm_mobs_find_slot_by_eid(&mobs, eid)].random.seed
                      == UINT64_C(25214903928)
                  && gm_mobs_event_count(&mobs) == 0,
              "horse tame non-trigger consumes one exact Java draw");

        gm_mobs_init(&mobs, 0);
        gm_mobs_set_represented_player_uuid(
            &mobs, owner_most, owner_least);
        CHECK(gm_mobs_spawn_horse_exact(
                  &mobs, types[index], eid,
                  0.5, 100.0, 0.5, 0.0, 0.0, 0.0,
                  0.0F, 20.0F, 0, 20.0, 0.225, 0.7,
                  0, 0, 99, 0, 0, 0, 0, 0) > 0
                  && gm_mobs_horse_mount(&mobs, eid)
                  && gm_mobs_set_entity_random_state(
                      &mobs, eid, UINT64_C(1000), 0, 0.0)
                  && gm_mobs_horse_crazy_attempt(&mobs, eid)
                      == GM_HORSE_TAME_FAILED
                  && gm_mobs_get_horse_state(&mobs, eid, &state)
                  && !state.ridden && !state.owner_present
                  && !(state.status & (GM_HORSE_TAME | GM_HORSE_REARING))
                  && state.temper == 100
                  && mobs.entity_random[
                      gm_mobs_find_slot_by_eid(&mobs, eid)].random.seed
                      == UINT64_C(23098218260332)
                  && gm_mobs_event_count(&mobs) == 2
                  && gm_mobs_event_get(&mobs, 0, &first)
                  && gm_mobs_event_get(&mobs, 1, &second)
                  && first.kind == GM_MOB_EVENT_SOUND
                  && first.data == angry && first.volume == 0.8F
                  && float_bits(first.pitch) == UINT32_C(0x3f8aa0e1)
                  && second.kind == GM_MOB_EVENT_ENTITY_STATUS
                  && second.data == 6,
              "temper-99 equality ejects before exact angry sound/status");

        gm_mobs_init(&mobs, 0);
        gm_mobs_set_represented_player_uuid(
            &mobs, owner_most, owner_least);
        CHECK(gm_mobs_spawn_horse_exact(
                  &mobs, types[index], eid,
                  0.5, 100.0, 0.5, 0.0, 0.0, 0.0,
                  0.0F, 20.0F, 0, 20.0, 0.225, 0.7,
                  0, 0, 100, 0, 0, 0, 0, 0) > 0
                  && gm_mobs_horse_mount(&mobs, eid)
                  && gm_mobs_set_entity_random_state(
                      &mobs, eid, UINT64_C(0), 0, 0.0)
                  && gm_mobs_horse_crazy_attempt(&mobs, eid)
                      == GM_HORSE_TAME_SUCCEEDED
                  && gm_mobs_get_horse_state(&mobs, eid, &state)
                  && state.ridden && state.owner_present
                  && state.owner_uuid_most == owner_most
                  && state.owner_uuid_least == owner_least
                  && (state.status & GM_HORSE_TAME)
                  && mobs.entity_random[
                      gm_mobs_find_slot_by_eid(&mobs, eid)].random.seed
                      == UINT64_C(277363943098)
                  && gm_mobs_event_count(&mobs) == 1
                  && gm_mobs_event_get(&mobs, 0, &first)
                  && first.kind == GM_MOB_EVENT_ENTITY_STATUS
                  && first.data == 7,
              "temper-100 success keeps rider and assigns exact player UUID");
    }
    return 1;
}

static int automatic_taming_scheduler(void) {
    GmRuntime runtime;
    GmAction idle;
    GmHorseState state;
    GmMobEvent event;
    GmRuntimeParticleEvent particle;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;

    CHECK(init_runtime(&runtime), "initialize automatic horse taming world");
    runtime.mobs_enabled = 1;
    runtime.controlled_mobs_enabled = 0;
    runtime.mobs.natural_spawning_enabled = 0;
    runtime.gamerules.doMobSpawning = 0;
    CHECK(gm_runtime_spawn_horse_fixture(
              &runtime, GM_MOB_HORSE, 280,
              8.5, 4.0, 8.5, 0.0, 0.0, 0.0,
              0.0F, 20.0F, 0, 20.0, 0.225, 0.7,
              0, 0, 100, 0, 0, 0, 0, 0,
              0, 0, 0)
              && gm_runtime_set_mob_no_ai(&runtime, 280, 0)
              && gm_mobs_horse_mount(&runtime.mobs, 280)
              && gm_mobs_set_entity_random_state(
                  &runtime.mobs, 280,
                  UINT64_C(174426972345687), 0, 0.0),
          "stage live temper-100 untamed rider");
    gm_runtime_tick(&runtime, idle);
    CHECK(gm_mobs_get_horse_state(&runtime.mobs, 280, &state)
              && state.ridden && state.owner_present
              && state.owner_uuid_most == runtime.player_uuid_most
              && state.owner_uuid_least == runtime.player_uuid_least
              && (state.status & GM_HORSE_TAME)
              && runtime.mobs.horse_crazy_active[
                  gm_mobs_find_slot_by_eid(&runtime.mobs, 280)]
              && gm_mobs_event_count(&runtime.mobs) == 1
              && gm_mobs_event_get(&runtime.mobs, 0, &event)
              && event.kind == GM_MOB_EVENT_ENTITY_STATUS
              && event.data == 7
              && gm_runtime_particle_event_count(&runtime) == 1
              && gm_runtime_particle_event_get(&runtime, 0, &particle)
              && particle.kind == GM_PARTICLE_HEART
              && particle.count == 7 && particle.entity_eid == 280
              && float_bits(particle.entity_width)
                  == float_bits(1.3964844F)
              && float_bits(particle.entity_height) == float_bits(1.6F),
          "live task starts, rolls, tames, owns, and keeps rider in one tick");
    gm_runtime_destroy(&runtime);

    CHECK(init_runtime(&runtime), "reinitialize automatic horse failure world");
    runtime.mobs_enabled = 1;
    runtime.controlled_mobs_enabled = 0;
    runtime.mobs.natural_spawning_enabled = 0;
    runtime.gamerules.doMobSpawning = 0;
    CHECK(gm_runtime_spawn_horse_fixture(
              &runtime, GM_MOB_DONKEY, 281,
              8.5, 4.0, 8.5, 0.0, 0.0, 0.0,
              0.0F, 20.0F, 0, 20.0, 0.175, 0.5,
              0, 0, 99, 0, 0, 0, 0, 0,
              0, 0, 0)
              && gm_runtime_set_mob_no_ai(&runtime, 281, 0)
              && gm_mobs_horse_mount(&runtime.mobs, 281)
              && gm_mobs_set_entity_random_state(
                  &runtime.mobs, 281,
                  UINT64_C(101450675431589), 0, 0.0),
          "stage live temper-99 equality rider");
    gm_runtime_tick(&runtime, idle);
    CHECK(gm_mobs_get_horse_state(&runtime.mobs, 281, &state)
              && !state.ridden && !state.owner_present
              && state.temper == 100
              && !(state.status & (GM_HORSE_TAME | GM_HORSE_REARING))
              && gm_mobs_event_count(&runtime.mobs) == 2
              && gm_mobs_event_get(&runtime.mobs, 0, &event)
              && event.kind == GM_MOB_EVENT_SOUND
              && event.data == GM_MOB_SOUND_DONKEY_ANGRY
              && gm_mobs_event_get(&runtime.mobs, 1, &event)
              && event.kind == GM_MOB_EVENT_ENTITY_STATUS
              && event.data == 6
              && gm_runtime_particle_event_count(&runtime) == 1
              && gm_runtime_particle_event_get(&runtime, 0, &particle)
              && particle.kind == GM_PARTICLE_SMOKE_NORMAL
              && particle.count == 7 && particle.entity_eid == 281
              && float_bits(particle.entity_width)
                  == float_bits(1.3964844F)
              && float_bits(particle.entity_height) == float_bits(1.6F),
          "live equality failure tempers, ejects, sounds, then statuses");
    gm_runtime_destroy(&runtime);
    return 1;
}

static int genetics_case(
        int first_type, int second_type, int child_type,
        uint64_t expected_parent_seed, uint64_t expected_health,
        uint64_t expected_jump, uint64_t expected_speed,
        int expected_variant) {
    GmMobLive mobs;
    GmHorseState child;
    int first_slot;
    gm_mobs_init(&mobs, 0);
    CHECK(gm_mobs_spawn_horse_exact(
              &mobs, first_type, 220,
              0.5, 100.0, 0.5, 0.0, 0.0, 0.0,
              0.0F, 25.0F, 0, 25.0, 0.25, 0.75,
              0, GM_HORSE_TAME, 0,
              first_type == GM_MOB_HORSE ? 0x304 : 0,
              0, 0, 0, 0) > 0
              && gm_mobs_spawn_horse_exact(
                  &mobs, second_type, 221,
                  2.5, 100.0, 0.5, 0.0, 0.0, 0.0,
                  0.0F, 20.0F, 0, 20.0, 0.20, 0.60,
                  0, GM_HORSE_TAME, 0,
                  second_type == GM_MOB_HORSE ? 0x102 : 0,
                  0, 0, 0, 0) > 0,
          "stage exact horse genetics parents");
    CHECK(gm_mobs_restore_horse_lifecycle(
              &mobs, 220, 600, 0, 0, 0, 0, 0, 0, 0, 0,
              0, 0, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F,
              0.0F, 0.0F, 0.0F, 0.0F, 0.0F)
              && gm_mobs_restore_horse_lifecycle(
                  &mobs, 221, 600, 0, 0, 0, 0, 0, 0, 0, 0,
                  0, 0, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F,
                  0.0F, 0.0F, 0.0F, 0.0F, 0.0F)
              && gm_mobs_set_entity_random_state(
                  &mobs, 220, UINT64_C(0x23456789abcd), 0, 0.0),
          "stage exact horse love and initiating RNG cursor");
    CHECK(gm_mobs_horse_create_child(&mobs, 220, 221, 222, &child) > 0
              && child.type == child_type && child.growing_age == -24000
              && child.variant == expected_variant
              && double_bits(child.max_health) == expected_health
              && double_bits(child.jump_strength) == expected_jump
              && double_bits(child.movement_speed) == expected_speed,
          "horse child subtype, genetics, and attribute bits match Java");
    first_slot = gm_mobs_find_slot_by_eid(&mobs, 220);
    CHECK(first_slot > 0
              && mobs.entity_random[first_slot].random.seed
                  == expected_parent_seed,
          "horse genetics consumes the exact initiating-parent RNG draws");
    return 1;
}

static int genetics_semantics(void) {
    CHECK(genetics_case(
              GM_MOB_HORSE, GM_MOB_HORSE, GM_MOB_HORSE,
              UINT64_C(160092359067293), UINT64_C(0x4038000000000000),
              UINT64_C(0x3fe66b729b7922eb),
              UINT64_C(0x3fcc6b881390fcb3), 0x102),
          "horse/horse Java genetics row");
    CHECK(genetics_case(
              GM_MOB_HORSE, GM_MOB_DONKEY, GM_MOB_MULE,
              UINT64_C(30191428589163), UINT64_C(0x4037aaaaaaaaaaab),
              UINT64_C(0x3fe5a0ca3a888378),
              UINT64_C(0x3fcf4013612bce21), 0),
          "horse/donkey Java genetics row");
    CHECK(genetics_case(
              GM_MOB_DONKEY, GM_MOB_HORSE, GM_MOB_MULE,
              UINT64_C(30191428589163), UINT64_C(0x4037aaaaaaaaaaab),
              UINT64_C(0x3fe5a0ca3a888378),
              UINT64_C(0x3fcf4013612bce21), 0),
          "donkey/horse Java genetics row");
    CHECK(genetics_case(
              GM_MOB_DONKEY, GM_MOB_DONKEY, GM_MOB_DONKEY,
              UINT64_C(30191428589163), UINT64_C(0x4037aaaaaaaaaaab),
              UINT64_C(0x3fe5a0ca3a888378),
              UINT64_C(0x3fcf4013612bce21), 0),
          "donkey/donkey Java genetics row");
    return 1;
}

static int mating_boundary_case(
        int first_type, int second_type, int child_type,
        uint64_t expected_parent_seed, int expected_xp) {
    GmMobLive mobs;
    GmAnimalMateResult birth;
    GmHorseState child;
    uint64_t world_seed = UINT64_C(0x123456789abc);
    uint64_t math_seed = UINT64_C(0x456789abcdef);
    int next_eid = 780002;
    int delay = 0;
    int result = GM_SHEEP_MATE_NONE;
    int first_slot;
    gm_mobs_init(&mobs, 0);
    CHECK(gm_mobs_spawn_horse_exact(
              &mobs, first_type, 780000,
              0.5, 100.0, 0.5, 0.0, 0.0, 0.0,
              0.0F, 25.0F, 0, 25.0, 0.25, 0.75,
              0, GM_HORSE_TAME, 0,
              first_type == GM_MOB_HORSE ? 0x304 : 0,
              0, 0, 0, 0) > 0
              && gm_mobs_spawn_horse_exact(
                  &mobs, second_type, 780001,
                  2.5, 100.0, 0.5, 0.0, 0.0, 0.0,
                  0.0F, 20.0F, 0, 20.0, 0.20, 0.60,
                  0, GM_HORSE_TAME, 0,
                  second_type == GM_MOB_HORSE ? 0x102 : 0,
                  0, 0, 0, 0) > 0
              && gm_mobs_restore_horse_lifecycle(
                  &mobs, 780000, 600, 0, 0, 0, 0, 0, 0, 0, 0,
                  0, 0, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F,
                  0.0F, 0.0F, 0.0F, 0.0F, 0.0F)
              && gm_mobs_restore_horse_lifecycle(
                  &mobs, 780001, 600, 0, 0, 0, 0, 0, 0, 0, 0,
                  0, 0, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F,
                  0.0F, 0.0F, 0.0F, 0.0F, 0.0F)
              && gm_mobs_set_entity_random_state(
                  &mobs, 780000, UINT64_C(0x23456789abcd), 0, 0.0)
              && gm_mobs_set_entity_random_state(
                  &mobs, 780001, UINT64_C(0x3456789abcde), 0, 0.0),
          "stage direct horse mating boundary");
    for (int update = 0; update < 60; ++update) {
        result = gm_mobs_animal_mate_update(
            &mobs, 780000, 780001, &delay, 0, 1,
            &world_seed, &math_seed, &next_eid, 1, &birth);
        CHECK(result == (update == 59
                  ? GM_SHEEP_MATE_BORN : GM_SHEEP_MATE_WAITING),
              "horse mating waits exactly sixty updates before birth");
    }
    CHECK(birth.child_eid == 780002 && birth.child_type == child_type
              && birth.child_slot > 0 && birth.xp_eid == 780003
              && birth.xp_slot >= 0 && birth.xp_value == expected_xp
              && next_eid == 780004
              && gm_mobs_get_horse_state(&mobs, 780002, &child)
              && child.type == child_type && child.growing_age == -24000
              && gm_mobs_particle_batch_count(&mobs) == 1
              && mobs.xp_orbs[birth.xp_slot].xpValue == expected_xp,
          "horse mating births child, seven-heart batch, and exact XP order");
    first_slot = gm_mobs_find_slot_by_eid(&mobs, 780000);
    CHECK(first_slot > 0
              && mobs.entity_random[first_slot].random.seed
                  == expected_parent_seed
              && mobs.growing_age[first_slot] == 6000
              && mobs.sheep_in_love[first_slot] == 0,
          "horse mating closes parent lifecycle and full RNG cursor exactly");
    return 1;
}

static int mating_boundary_semantics(void) {
    CHECK(mating_boundary_case(
              GM_MOB_HORSE, GM_MOB_HORSE, GM_MOB_HORSE,
              UINT64_C(156360562369922), 4),
          "horse/horse full Java mating boundary");
    CHECK(mating_boundary_case(
              GM_MOB_HORSE, GM_MOB_DONKEY, GM_MOB_MULE,
              UINT64_C(14448659193736), 6),
          "horse/donkey full Java mating boundary");
    CHECK(mating_boundary_case(
              GM_MOB_DONKEY, GM_MOB_HORSE, GM_MOB_MULE,
              UINT64_C(14448659193736), 6),
          "donkey/horse full Java mating boundary");
    CHECK(mating_boundary_case(
              GM_MOB_DONKEY, GM_MOB_DONKEY, GM_MOB_DONKEY,
              UINT64_C(14448659193736), 6),
          "donkey/donkey full Java mating boundary");
    return 1;
}

static int automatic_crossbreed_scheduler(void) {
    GmRuntime runtime;
    GmAction idle;
    GmHorseState child;
    int birth_tick = -1;
    int child_count = 0;
    int birth_particles = -1;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    CHECK(init_runtime(&runtime), "initialize automatic horse breeding world");
    runtime.mobs_enabled = 1;
    runtime.controlled_mobs_enabled = 0;
    runtime.mobs.natural_spawning_enabled = 0;
    runtime.gamerules.doMobSpawning = 0;
    CHECK(gm_runtime_spawn_horse_fixture(
              &runtime, GM_MOB_HORSE, 790000,
              8.5, 4.0, 8.5, 0.0, 0.0, 0.0,
              0.0F, 25.0F, 0, 25.0, 0.25, 0.75,
              0, GM_HORSE_TAME, 0, 0x304, 0, 0, 0, 0,
              0, 0, 0)
              && gm_runtime_spawn_horse_fixture(
                  &runtime, GM_MOB_DONKEY, 790001,
                  8.75, 4.0, 8.5, 0.0, 0.0, 0.0,
                  0.0F, 20.0F, 0, 20.0, 0.20, 0.60,
                  0, GM_HORSE_TAME, 0, 0, 0, 0, 0, 0,
                  0, 0, 0)
              && gm_runtime_restore_horse_lifecycle(
                  &runtime, 790000, 600, 0, 0, 0, 0, 0, 0, 0, 0,
                  0, 0, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F,
                  0.0F, 0.0F, 0.0F, 0.0F, 0.0F)
              && gm_runtime_restore_horse_lifecycle(
                  &runtime, 790001, 600, 0, 0, 0, 0, 0, 0, 0, 0,
                  0, 0, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F,
                  0.0F, 0.0F, 0.0F, 0.0F, 0.0F)
              && gm_runtime_set_mob_no_ai(&runtime, 790000, 0)
              && gm_runtime_set_mob_no_ai(&runtime, 790001, 0)
              && gm_runtime_set_entity_id_cursor(&runtime, 790002)
              && gm_runtime_set_world_random_seed48(
                  &runtime, UINT64_C(0x123456789abc))
              && gm_runtime_set_math_random_seed48(
                  &runtime, UINT64_C(0x456789abcdef)),
          "stage automatic horse/donkey love pair");
    for (int tick = 1; tick <= 100; ++tick) {
        gm_runtime_tick(&runtime, idle);
        if (birth_tick < 0
                && gm_mobs_get_horse_state(
                    &runtime.mobs, 790002, &child)) {
            birth_tick = tick;
            birth_particles = gm_runtime_particle_event_count(&runtime);
            GmMobParticleBatch batch;
            int batches = gm_mobs_particle_batch_count(&runtime.mobs);
            CHECK(birth_particles >= 7 && batches > 0
                      && gm_mobs_particle_batch_get(
                          &runtime.mobs, batches - 1, &batch)
                      && batch.particle_id == GM_PARTICLE_HEART
                      && batch.count == 7,
                  "horse birth drains its seven-heart batch to runtime");
            for (int particle = 0; particle < 7; ++particle) {
                GmRuntimeParticleEvent event;
                GmTerminalParticle *source = &batch.particles[particle];
                CHECK(gm_runtime_particle_event_get(
                          &runtime, birth_particles - 7 + particle, &event)
                          && event.kind == GM_PARTICLE_HEART
                          && event.entity_eid == 790000
                          && double_bits(event.x) == double_bits(source->x)
                          && double_bits(event.y) == double_bits(source->y)
                          && double_bits(event.z) == double_bits(source->z)
                          && double_bits(event.motion_x)
                              == double_bits(source->vx)
                          && double_bits(event.motion_y)
                              == double_bits(source->vy)
                          && double_bits(event.motion_z)
                              == double_bits(source->vz),
                      "horse birth preserves exact heart payload ordering");
            }
        }
    }
    for (int eid = 790002; eid < runtime.next_entity_id; ++eid)
        if (gm_mobs_get_horse_state(&runtime.mobs, eid, &child)) {
            CHECK(child.type == GM_MOB_MULE,
                  "automatic horse/donkey birth produces only a mule");
            ++child_count;
        }
    CHECK(birth_tick == 60 && birth_particles >= 7 && child_count == 1
              && runtime.next_entity_id == 790004,
          "live scheduler births exactly once on the sixtieth mate update");
    gm_runtime_destroy(&runtime);
    return 1;
}

static int mount_jump_and_fall_edges(void) {
    GmMobLive mobs;
    GmHorseState state;
    int ridden_eid = -1;
    gm_mobs_init(&mobs, 0);
    CHECK(gm_mobs_spawn_horse_exact(
              &mobs, GM_MOB_HORSE, 290,
              8.5, 4.0, 8.5, 0.0, 0.0, 0.0,
              0.0F, 20.0F, 1, 20.0, 0.225, 0.7,
              0, GM_HORSE_TAME, 0, 0, 0, 0, 0, 0) > 0
              && gm_mobs_spawn_horse_exact(
                  &mobs, GM_MOB_HORSE, 291,
                  10.5, 4.0, 8.5, 0.0, 0.0, 0.0,
                  0.0F, 20.0F, 1, 20.0, 0.225, 0.7,
                  -100, GM_HORSE_TAME, 0, 0, 0, 0, 0, 0) > 0
              && gm_mobs_spawn_horse_exact(
                  &mobs, GM_MOB_SKELETON_HORSE, 292,
                  12.5, 4.0, 8.5, 0.0, 0.0, 0.0,
                  0.0F, 15.0F, 1, 15.0, 0.2, 0.6,
                  0, 0, 0, 0, 0, 0, 0, 0) > 0,
          "stage adult, child, and untamed undead mount edges");
    CHECK(!gm_mobs_horse_mount(&mobs, 291)
              && !gm_mobs_horse_mount(&mobs, 292),
          "children and untamed undead horses reject mounting");
    mobs.boat_ride_eid = 7;
    mobs.pig_ride_eid = 8;
    CHECK(gm_mobs_horse_mount(&mobs, 290)
              && mobs.boat_ride_eid == -1 && mobs.pig_ride_eid == -1
              && gm_mobs_horse_riding(&mobs, &ridden_eid)
              && ridden_eid == 290,
          "horse mount atomically replaces other represented vehicles");
    CHECK(!gm_mobs_horse_set_jump_power(&mobs, 45),
          "unsaddled horse rejects jump power");
    CHECK(gm_mobs_set_horse_inventory(
              &mobs, 290, 0, ic_mk(329, 1, 0))
              && gm_mobs_horse_set_jump_power(&mobs, -1)
              && gm_mobs_get_horse_state(&mobs, 290, &state)
              && float_bits(state.jump_power) == UINT32_C(0x3ecccccd)
              && (state.status & GM_HORSE_REARING),
          "negative charge clamps to Java's 0.4 jump power and rears");
    CHECK(gm_mobs_horse_set_jump_power(&mobs, 45)
              && gm_mobs_get_horse_state(&mobs, 290, &state)
              && float_bits(state.jump_power) == UINT32_C(0x3f19999a),
          "half charge maps to Java's exact 0.6 jump power");
    CHECK(gm_mobs_horse_set_jump_power(&mobs, 90)
              && gm_mobs_get_horse_state(&mobs, 290, &state)
              && float_bits(state.jump_power) == UINT32_C(0x3f800000),
          "ninety charge saturates to one");
    gm_mobs_horse_dismount(&mobs);
    CHECK(!gm_mobs_horse_riding(&mobs, NULL),
          "explicit horse dismount removes both graph directions");

    {
        GmRuntime runtime;
        GmAction idle;
        EwStore *store;
        int slot;
        memset(&idle, 0, sizeof idle);
        idle.hotbar_sel = -1;
        CHECK(init_runtime(&runtime), "initialize mounted horse fall world");
        runtime.mobs_enabled = 1;
        runtime.controlled_mobs_enabled = 0;
        runtime.mobs.natural_spawning_enabled = 0;
        runtime.gamerules.doMobSpawning = 0;
        for (int x = 7; x <= 9; ++x)
            for (int z = 7; z <= 9; ++z)
                gm_world_set_block(runtime.world, x, 3, z, 1);
        CHECK(gm_runtime_spawn_horse_fixture(
                  &runtime, GM_MOB_HORSE, 293,
                  8.5, 4.25, 8.5, 0.0, -0.5, 0.0,
                  0.0F, 20.0F, 0, 20.0, 0.225, 0.7,
                  0, GM_HORSE_TAME | GM_HORSE_SADDLED, 0,
                  0, 0, 0, 0, 0, 0, 0, 0)
                  && gm_runtime_set_mob_no_ai(&runtime, 293, 0)
                  && gm_mobs_set_horse_inventory(
                      &runtime.mobs, 293, 0, ic_mk(329, 1, 0))
                  && gm_mobs_horse_mount(&runtime.mobs, 293),
              "stage saddle, rider, and live horse movement");
        slot = gm_mobs_find_slot_by_eid(&runtime.mobs, 293);
        store = runtime.mobs.current
            ? &runtime.mobs.b : &runtime.mobs.a;
        store->on_ground[slot] = 0;
        runtime.mobs.entity_fall_distance[slot] = 10.0F;
        runtime.mobs.entity_box_valid[slot] = 0;
        runtime.vitals.health = 20.0F;
        runtime.player.health = 20.0F;
        runtime.mobs.player_hurt_resistant = 0;
        gm_runtime_tick(&runtime, idle);
        CHECK(gm_mobs_get_horse_state(&runtime.mobs, 293, &state)
                  && state.health == 18.0F
                  && runtime.vitals.health == 18.0F
                  && runtime.player.health == 18.0F
                  && runtime.player.fall_distance == 0.0F,
              "horse fall damages horse and recursive player passenger equally");
        CHECK(gm_mobs_horse_riding(&runtime.mobs, &ridden_eid)
                  && ridden_eid == 293
                  && runtime.player.ent.motionX == 0.0
                  && runtime.player.ent.motionY == 0.0
                  && runtime.player.ent.motionZ == 0.0,
              "mounted landing retains graph and refreshes passenger pose");
        gm_runtime_destroy(&runtime);
    }
    return 1;
}

static int explicit_dismount_edges(void) {
    for (int layout = 0; layout < 3; ++layout) {
        GmRuntime runtime;
        GmAction idle;
        double horse_x = 8.5, horse_y = 4.0, horse_z = 8.5;
        memset(&idle, 0, sizeof idle);
        idle.hotbar_sel = -1;
        CHECK(init_runtime(&runtime), "initialize horse dismount world");
        runtime.mobs_enabled = 1;
        runtime.controlled_mobs_enabled = 0;
        runtime.mobs.natural_spawning_enabled = 0;
        runtime.gamerules.doMobSpawning = 0;
        CHECK(gm_runtime_spawn_horse_fixture(
                  &runtime, GM_MOB_HORSE, 294 + layout,
                  horse_x, horse_y, horse_z, 0.0, 0.0, 0.0,
                  0.0F, 20.0F, 1, 20.0, 0.225, 0.7,
                  0, GM_HORSE_TAME | GM_HORSE_SADDLED, 0,
                  0, 0, 0, 0, 0, 0, 0, 0)
                  && gm_mobs_set_horse_inventory(
                      &runtime.mobs, 294 + layout, 0,
                      ic_mk(329, 1, 0))
                  && gm_mobs_horse_mount(
                      &runtime.mobs, 294 + layout),
              "stage mounted horse dismount fixture");
        runtime.player.yaw = 0.0F;
        runtime.player.pitch = 17.0F;
        runtime.player.ent.posX = horse_x - runtime.ox;
        runtime.player.ent.posY = horse_y
            + (double)1.6F * 0.75D - 0.35D;
        runtime.player.ent.posZ = horse_z - runtime.oz;
        runtime.player.ent.box = psv_player_box(
            runtime.player.ent.posX, runtime.player.ent.posY,
            runtime.player.ent.posZ);
        runtime.player.ent.motionX = 0.125D;
        runtime.player.ent.motionY = -0.25D;
        runtime.player.ent.motionZ = 0.375D;
        runtime.player.fall_distance = 4.5F;
        if (layout >= 1) gm_world_set_block(runtime.world, 7, 5, 8, 1);
        if (layout >= 2) gm_world_set_block(runtime.world, 7, 6, 8, 1);

        float side = ((float)MC_PI / 2.0F) * -1.0F;
        float angle = -runtime.player.yaw * 0.017453292F
            - (float)MC_PI + side;
        float offset_x = -mc_sin(&runtime.sin_table, angle);
        float offset_z = -mc_cos(&runtime.sin_table, angle);
        double distance =
            (double)(0.6F / 2.0F + 1.3964844F / 2.0F) + 0.4D;
        double divisor = fabsf(offset_x) > fabsf(offset_z)
            ? (double)fabsf(offset_x) : (double)fabsf(offset_z);
        double scale = distance / divisor;
        double expected_x = horse_x + (double)offset_x * scale;
        double expected_y = horse_y + (double)1.6F + 0.001D
            + (layout == 1 ? 1.0D : 0.0D);
        double expected_z = horse_z + (double)offset_z * scale;
        if (layout == 2) {
            expected_x = horse_x;
            expected_y = horse_y + (double)1.8F + 0.001D;
            expected_z = horse_z;
        }
        gm_mobs_horse_dismount_explicit(
            &runtime.mobs, runtime.world, NULL,
            (const struct McSinTable *)&runtime.sin_table,
            (struct PsvPlayer *)&runtime.player,
            runtime.ox, runtime.oz);
        CHECK(!gm_mobs_horse_riding(&runtime.mobs, NULL)
                  && double_bits(runtime.player.ent.posX)
                      == double_bits(expected_x - runtime.ox)
                  && double_bits(runtime.player.ent.posY)
                      == double_bits(expected_y)
                  && double_bits(runtime.player.ent.posZ)
                      == double_bits(expected_z - runtime.oz)
                  && runtime.player.ent.motionX == 0.125D
                  && runtime.player.ent.motionY == -0.25D
                  && runtime.player.ent.motionZ == 0.375D
                  && runtime.player.fall_distance == 4.5F
                  && runtime.player.yaw == 0.0F
                  && runtime.player.pitch == 17.0F,
              layout == 0
                  ? "horse dismount uses exact left-side target"
                  : layout == 1
                      ? "blocked horse dismount raises target one block"
                      : "twice-blocked horse dismount falls back over horse");
        gm_runtime_destroy(&runtime);
    }
    return 1;
}

static int armor_and_death_inventory(void) {
    GmMobLive mobs;
    GmLiveSim drops;
    GmHorseState state;
    uint64_t math_seed = UINT64_C(0x123456789abc);
    int next_eid = 5000;
    GmMobDeathContext context = {0, &math_seed, &next_eid};
    int slot;
    gm_mobs_init(&mobs, 0);
    memset(&drops, 0, sizeof drops);
    slot = gm_mobs_spawn_horse_exact(
        &mobs, GM_MOB_HORSE, 300,
        0.5, 100.0, 0.5, 0.0, 0.0, 0.0,
        0.0F, 20.0F, 1, 20.0, 0.225, 0.7,
        0, GM_HORSE_TAME, 0, 0, 3, 0, 0, 0);
    CHECK(slot > 0
              && gm_mobs_set_horse_inventory(
                  &mobs, 300, 1, ic_mk(419, 1, 7)),
          "stage diamond-armored horse");
    CHECK(gm_mobs_source_arrow_hit(
              &mobs, NULL, slot, 4.0, 4.0, 10.0F,
              &drops, &context) == 2
              && gm_mobs_get_horse_state(&mobs, 300, &state)
              && fabsf(state.health - 12.4F) < 1.0e-5F,
          "diamond horse armor applies Java combat reduction before health");
    CHECK(gm_mobs_event_count(&mobs) == 2,
          "surviving arrow emits status and horse hurt sound");
    {
        GmMobEvent event;
        CHECK(gm_mobs_event_get(&mobs, 1, &event)
                  && event.kind == GM_MOB_EVENT_SOUND
                  && event.data == GM_MOB_SOUND_HORSE_HURT
                  && event.volume == 0.8F,
              "horse hurt feedback uses the horse-family event and volume");
    }

    gm_mobs_init(&mobs, 1);
    memset(&drops, 0, sizeof drops);
    math_seed = UINT64_C(0x23456789abcd);
    next_eid = 6000;
    slot = gm_mobs_spawn_horse_exact(
        &mobs, GM_MOB_DONKEY, 301,
        0.5, 100.0, 0.5, 0.0, 0.0, 0.0,
        0.0F, 2.0F, 1, 20.0, 0.175, 0.5,
        0, GM_HORSE_TAME, 0, 0, 0, 1, 0, 0);
    CHECK(slot > 0
              && gm_mobs_set_horse_inventory(
                  &mobs, 301, 0, ic_mk(329, 1, 0)),
          "stage chested donkey death inventory");
    {
        ICStack book = ic_mk(403, 1, 0);
        book.repair_cost = 9;
        book.custom_name = 77;
        book.tag_id = 81;
        book.n_enchants = 2;
        book.enchants[0].id = 16;
        book.enchants[0].level = 4;
        book.enchants[1].id = 34;
        book.enchants[1].level = 3;
        CHECK(gm_mobs_set_horse_inventory(&mobs, 301, 2, book)
                  && gm_mobs_set_horse_inventory(
                      &mobs, 301, 16, ic_mk(264, 5, 0)),
              "stage full-payload donkey storage");
    }
    CHECK(gm_mobs_source_arrow_hit(
              &mobs, NULL, slot, 4.0, 4.0, 20.0F,
              &drops, &context) == 2,
          "lethal donkey arrow is accepted with doMobLoot disabled");
    CHECK(drops.n_active == 4
              && find_drop(&drops, 329) >= 0
              && find_drop(&drops, 403) >= 0
              && find_drop(&drops, 264) >= 0
              && find_drop(&drops, 54) >= 0
              && find_drop(&drops, 334) < 0,
          "inventory, saddle, and chest drop independently of doMobLoot");
    {
        int book_slot = find_drop(&drops, 403);
        const GmLiveEnt *book = &drops.ents[book_slot];
        CHECK(book->repair_cost == 9 && book->custom_name == 77
                  && book->tag_id == 81 && book->n_enchants == 2
                  && book->ench_id[0] == 16 && book->ench_lvl[0] == 4
                  && book->ench_id[1] == 34 && book->ench_lvl[1] == 3,
              "horse inventory death preserves the complete ItemStack payload");
    }
    return 1;
}

static int capacity_rejection_is_atomic(void) {
    GmMobLive mobs;
    GmLiveSim drops;
    GmHorseState before, after;
    uint64_t math_seed = UINT64_C(0x3456789abcde);
    uint64_t entity_seed;
    int next_eid = 7000;
    int slot;
    GmMobDeathContext context = {0, &math_seed, &next_eid};
    gm_mobs_init(&mobs, 2);
    memset(&drops, 0, sizeof drops);
    slot = gm_mobs_spawn_horse_exact(
        &mobs, GM_MOB_DONKEY, 302,
        0.5, 100.0, 0.5, 0.0, 0.0, 0.0,
        0.0F, 1.0F, 1, 20.0, 0.175, 0.5,
        0, GM_HORSE_TAME, 0, 0, 0, 1, 0, 0);
    CHECK(slot > 0
              && gm_mobs_set_horse_inventory(
                  &mobs, 302, 0, ic_mk(329, 1, 0))
              && gm_mobs_set_horse_inventory(
                  &mobs, 302, 2, ic_mk(264, 1, 0)),
          "stage atomic-capacity donkey");
    for (int index = 0; index < GM_LIVE_MAX; ++index)
        CHECK(gm_live_spawn_stack(
                  &drops, 0.5, 100.0, 0.5,
                  ic_mk(1, 1, index & 15), 10),
              "fill every exact EntityItem slot");
    drops.item_spawn_limit = GM_LIVE_MAX;
    CHECK(gm_mobs_get_horse_state(&mobs, 302, &before),
          "snapshot donkey before rejected death");
    entity_seed = mobs.entity_random[slot].random.seed;
    CHECK(gm_mobs_source_arrow_hit(
              &mobs, NULL, slot, 4.0, 4.0, 20.0F,
              &drops, &context) == 0
              && gm_mobs_get_horse_state(&mobs, 302, &after)
              && memcmp(&before, &after, sizeof before) == 0
              && mobs.entity_random[slot].random.seed == entity_seed
              && math_seed == UINT64_C(0x3456789abcde)
              && next_eid == 7000 && drops.n_active == GM_LIVE_MAX
              && gm_mobs_event_count(&mobs) == 0,
          "insufficient drop capacity rejects death, RNG, state, and events atomically");
    return 1;
}

static int checkpoint_continuation(void) {
    GmRuntime runtime;
    GmHorseState expected, actual;
    GmAction idle;
    char path[160];
    int slot;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    (void)mkdir(".tmp", 0700);
    snprintf(path, sizeof path,
             ".tmp/test_horse_runtime_checkpoint.%ld.bin", (long)getpid());
    CHECK(init_runtime(&runtime), "initialize horse checkpoint fixture");
    CHECK(gm_runtime_spawn_horse_fixture(
        &runtime, GM_MOB_SKELETON_HORSE, 8001,
        8.5, 100.0, 12.5, 0.125, -0.25, 0.375,
        33.0F, 13.0F, 1, 15.0, 0.2, 0.61,
        -2345, GM_HORSE_TAME | GM_HORSE_BRED | GM_HORSE_MOUTH_OPEN,
        57, 0, 0, 0, 1, 17777, 0, 0, 0),
          "spawn controlled horse continuation fixture");
    slot = gm_mobs_find_slot_by_eid(&runtime.mobs, 8001);
    CHECK(slot > 0 && gm_mobs_set_entity_random_state(
                  &runtime.mobs, 8001, UINT64_C(0x456789abcdef),
                  1, -0.375)
              && gm_runtime_restore_horse_owner(
                  &runtime, 8001, 1,
                  UINT64_C(0x4675d88ca2a73c16),
                  UINT64_C(0xbf9c70b5a7108798))
              && gm_mobs_set_horse_inventory(
                  &runtime.mobs, 8001, 0, ic_mk(329, 1, 0)),
          "stage horse continuation state");
    runtime.mobs.horse_eating_counter[slot] = 37;
    runtime.mobs.horse_open_mouth_counter[slot] = 14;
    runtime.mobs.horse_jump_rearing_counter[slot] = 5;
    runtime.mobs.horse_tail_counter[slot] = 1;
    runtime.mobs.horse_sprint_counter[slot] = 22;
    runtime.mobs.horse_gallop_time[slot] = 6;
    runtime.mobs.horse_head_lean[slot] = 0.25F;
    runtime.mobs.horse_prev_head_lean[slot] = 0.125F;
    runtime.mobs.horse_rearing_amount[slot] = 0.5F;
    runtime.mobs.horse_prev_rearing_amount[slot] = 0.375F;
    runtime.mobs.horse_mouth_openness[slot] = 0.75F;
    runtime.mobs.horse_prev_mouth_openness[slot] = 0.625F;
    CHECK(gm_runtime_write_checkpoint(&runtime, path),
          "write horse lifecycle checkpoint");
    gm_runtime_tick(&runtime, idle);
    CHECK(float_bits(runtime.mobs.horse_head_lean[slot]) == UINT32_C(0x3dcccccc),
          "horse animation compound assignment preserves Java float grouping");
    for (int tick = 1; tick < 40; ++tick)
        gm_runtime_tick(&runtime, idle);
    CHECK(gm_mobs_get_horse_state(&runtime.mobs, 8001, &expected),
          "capture uninterrupted horse continuation");
    CHECK(gm_runtime_load_checkpoint(&runtime, path),
          "reload horse lifecycle checkpoint");
    for (int tick = 0; tick < 40; ++tick)
        gm_runtime_tick(&runtime, idle);
    CHECK(gm_mobs_get_horse_state(&runtime.mobs, 8001, &actual)
              && memcmp(&expected, &actual, sizeof expected) == 0
              && actual.owner_present
              && actual.owner_uuid_most
                  == UINT64_C(0x4675d88ca2a73c16)
              && actual.owner_uuid_least
                  == UINT64_C(0xbf9c70b5a7108798),
          "checkpoint resumes horse lifecycle, owner UUID, and RNG exactly");
    (void)remove(path);
    gm_runtime_destroy(&runtime);
    return 1;
}

static int skeleton_trap_construction_boundary(void) {
    static const int expected_eids[] = {
        8400, 8401, 8402, 8403, 8404, 8405, 8406,
    };
    static const int expected_types[] = {
        GM_MOB_SKELETON, GM_MOB_SKELETON_HORSE,
        GM_MOB_SKELETON, GM_MOB_SKELETON_HORSE,
        GM_MOB_SKELETON, GM_MOB_SKELETON_HORSE,
        GM_MOB_SKELETON,
    };
    static const uint64_t expected_uuid[][2] = {
        {UINT64_C(0xb4d199207f4f4e6c), UINT64_C(0x8809083b0ee319f1)},
        {UINT64_C(0xa8d401838b2f44fd), UINT64_C(0x81f116895715fb5c)},
        {UINT64_C(0xe5827ac533b1435f), UINT64_C(0xb0089a24a4c5f2b4)},
        {UINT64_C(0x7e33b0eb293745b8), UINT64_C(0x9a30599ddf472e71)},
        {UINT64_C(0x211b6f0cb4a74bb1), UINT64_C(0xa7b1a2b6b2d82632)},
        {UINT64_C(0xb31fedda13c444f7), UINT64_C(0x8ecc7ce59f8c1b39)},
        {UINT64_C(0xba9a721319954dbb), UINT64_C(0x93e9abd49dd528ec)},
    };
    static const uint64_t expected_jump_bits[] = {
        UINT64_C(0x3fe61fa40f03ce5e),
        UINT64_C(0x3fe45e5805502458),
        UINT64_C(0x3fe3707194d685ba),
    };
    static const int expected_bow_count[] = {1, 2, 2, 1};
    static const int expected_bow_ids[][2] = {
        {48, 0}, {34, 48}, {48, 34}, {34, 0},
    };
    static const int expected_head_ids[] = {34, 0, 0, 34};
    GmMobLive mobs;
    GmHorseState original, horse;
    GmSkeletonTrapEntityState entity;
    GmSkeletonTrapLightning lightning;
    uint64_t entity_seed = UINT64_C(0x123456789abc);
    uint64_t uuid_seed = UINT64_C(0x3456789abcde);
    uint64_t math_seed = 0;
    int next_eid = 8400;
    int kind, eid;
    gm_mobs_init(&mobs, 0);
    CHECK(gm_mobs_spawn_horse_exact(
              &mobs, GM_MOB_SKELETON_HORSE, 8399,
              987.5, 4.0, -98.5, 0.0, 0.0, 0.0,
              0.0F, 15.0F, 0, 15.0, 0.20000000298023224,
              0.5, 0, 0, 0, 0, 0, 0, 1, 123) > 0
              && gm_mobs_set_entity_random_state(
                  &mobs, 8399, UINT64_C(0x23456789abcd), 0, 0.0),
          "stage measured Java skeleton-trap fixture");
    CHECK(gm_mobs_skeleton_trap_activate(
              &mobs, 8399, 0.0F, &entity_seed, &uuid_seed,
              &math_seed, &next_eid) == 7,
          "skeleton trap atomically constructs seven loaded riders");
    CHECK(entity_seed == UINT64_C(224432795029324)
              && uuid_seed == UINT64_C(278761879117434)
              && math_seed == UINT64_C(1753435247202)
              && next_eid == 8407,
          "trap consumes exact entity, UUID, Math, and ID cursors");
    CHECK(gm_mobs_get_horse_state(&mobs, 8399, &original)
              && !original.trap && original.trap_time == 123
              && (original.status & GM_HORSE_TAME)
              && original.growing_age == 0,
          "trap clears and tames the original horse without clearing time");
    CHECK(gm_mobs_loaded_order_count(&mobs) == 8,
          "trap appends seven entities to loaded order");
    for (int row = 0; row < 7; ++row) {
        CHECK(gm_mobs_loaded_order_get(&mobs, row + 1, &eid, &kind)
                  && eid == expected_eids[row]
                  && kind == GM_MOB_LOADED_LIVING,
              "trap loaded order is rider then three horse/rider pairs");
        CHECK(gm_mobs_skeleton_trap_entity_state(
                  &mobs, expected_eids[row], &entity)
                  && entity.type == expected_types[row]
                  && entity.persistent && entity.hurt_resistant_time == 60
                  && entity.uuid_present
                  && (uint64_t)entity.uuid_most == expected_uuid[row][0]
                  && (uint64_t)entity.uuid_least == expected_uuid[row][1],
              "trap entity state and every-other modifier UUID are exact");
    }
    CHECK(gm_mobs_skeleton_trap_entity_state(&mobs, 8400, &entity)
              && entity.vehicle_eid == 8399
              && gm_mobs_skeleton_trap_entity_state(&mobs, 8399, &entity)
              && entity.rider_eid == 8400,
          "original horse and first skeleton have reciprocal mount edges");
    for (int pair = 0; pair < 3; ++pair) {
        int horse_eid = 8401 + pair * 2;
        int rider_eid = horse_eid + 1;
        CHECK(gm_mobs_get_horse_state(&mobs, horse_eid, &horse)
                  && double_bits(horse.jump_strength)
                      == expected_jump_bits[pair]
                  && double_bits(horse.max_health)
                      == UINT64_C(0x402e000000000000)
                  && double_bits(horse.movement_speed)
                      == UINT64_C(0x3fc99999a0000000)
                  && horse.growing_age == 0
                  && (horse.status & GM_HORSE_TAME),
              "spawned skeleton-horse attributes match Java constructor");
        CHECK(gm_mobs_skeleton_trap_entity_state(
                  &mobs, horse_eid, &entity)
                  && entity.rider_eid == rider_eid
                  && gm_mobs_skeleton_trap_entity_state(
                      &mobs, rider_eid, &entity)
                  && entity.vehicle_eid == horse_eid,
              "each generated horse/rider pair has reciprocal mount edges");
    }
    for (int rider = 0; rider < 4; ++rider) {
        int rider_eid = 8400 + rider * 2;
        CHECK(gm_mobs_skeleton_trap_entity_state(
                  &mobs, rider_eid, &entity)
                  && !entity.left_handed && !entity.can_pick_up_loot
                  && entity.mainhand.item == 261
                  && entity.mainhand.n_enchants
                      == expected_bow_count[rider]
                  && entity.head.item == 306
                  && entity.head.n_enchants == 1
                  && entity.head.enchants[0].id
                      == expected_head_ids[rider],
              "trap riders receive exact bow and iron-helmet enchant payloads");
        for (int enchant = 0;
                enchant < expected_bow_count[rider]; ++enchant)
            CHECK(entity.mainhand.enchants[enchant].id
                      == expected_bow_ids[rider][enchant]
                      && entity.mainhand.enchants[enchant].level == 1,
                  "trap bow enchant order and levels are exact");
    }
    CHECK(gm_mobs_take_skeleton_trap_lightning(&mobs, &lightning)
              && lightning.eid == 8400
              && lightning.x == 987.5 && lightning.y == 4.0
              && lightning.z == -98.5
              && !gm_mobs_take_skeleton_trap_lightning(
                  &mobs, &lightning),
          "effect-only trap lightning is emitted once without consuming ID");
    return 1;
}

static int skeleton_trap_live_scheduler(void) {
    static const uint64_t expected_seed48[8] = {
        UINT64_C(44530734743327), UINT64_C(49113809480923),
        UINT64_C(241174341205032), UINT64_C(122712437562185),
        UINT64_C(7177024197119), UINT64_C(139364202302837),
        UINT64_C(68896053086376), UINT64_C(261538962464342),
    };
    static const uint64_t expected_gaussian_bits[8] = {
        UINT64_C(0x3fc5017518693fda), UINT64_C(0x3fd17773f7c6c520),
        UINT64_C(0xbfd369488826de15), UINT64_C(0x3feff52cb8be16ad),
        UINT64_C(0xbfadc21ba1b2a721), UINT64_C(0xbfc2add87bb6d65b),
        UINT64_C(0xbfcc446480a22150), UINT64_C(0xbff66d60695ad9a1),
    };
    static const double expected_position[8][3] = {
        {12.5, 4.0, 8.5},
        {12.5, 4.4125000178813938, 8.5},
        {12.095907199713338, 4.0, 9.347273226228495},
        {12.095907199713338, 4.4125000178813938,
            9.347273226228495},
        {13.576345981211034, 4.0, 7.7593038349115204},
        {13.576345981211034, 4.4125000178813938,
            7.7593038349115204},
        {12.380469510802527, 4.0, 8.931174581966161},
        {12.380469510802527, 4.4125000178813938,
            8.931174581966161},
    };
    static const double expected_motion[8][3] = {
        {0.0, -0.078400001525878907, 0.0},
        {0.026592936847828617, -0.078400001525878907,
            -0.078855665570597783},
        {-0.68542466332718521, -0.078400001525878907,
            0.45331845361999357},
        {-0.022057142165580086, -0.078400001525878907,
            0.032252854976718356},
        {0.97947487113031673, -0.078400001525878907,
            -0.67403352965604857},
        {0.0, -0.078400001525878907, 0.0},
        {-0.42647295277305164, -0.078400001525878907,
            0.074668676428644387},
        {0.0, -0.078400001525878907, 0.0},
    };
    static const uint64_t expected_tick20_seed48[8] = {
        UINT64_C(259295328368560), UINT64_C(173747895963683),
        UINT64_C(52322668426925), UINT64_C(261492796558774),
        UINT64_C(80537129409552), UINT64_C(139080146787337),
        UINT64_C(86829507140909), UINT64_C(250534207664253),
    };
    static const double expected_tick20_position[8][3] = {
        {9.25224988339562, 4.0, 8.963865105733362},
        {9.25224988339562, 4.412500017881394, 8.963865105733362},
        {10.042538366580061, 4.0, 10.705307207131753},
        {10.042538366580061, 4.412500017881394, 10.705307207131753},
        {16.514462984849956, 4.0, 5.740060121730579},
        {16.514462984849956, 4.412500017881394, 5.740060121730579},
        {11.102858248395364, 4.0, 9.151544235256353},
        {11.102858248395364, 4.412500017881394, 9.151544235256353},
    };
    static const double expected_tick20_motion[8][3] = {
        {-0.10499801802207834, -0.0784000015258789,
            0.015050923299771854},
        {0.011835522918264285, -0.0784000015258789,
            -0.00435681752378303},
        {0.0, -0.0784000015258789, 0.0},
        {0.0, -0.0784000015258789, 0.0},
        {0.0, -0.0784000015258789, 0.0},
        {0.0, -0.0784000015258789, 0.0},
        {0.0, -0.0784000015258789, 0.0},
        {0.0, -0.0784000015258789, 0.0},
    };
    GmRuntime runtime;
    GmAction idle;
    GmHorseState original;
    GmSkeletonTrapEntityState horse, rider;
    int eid;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    idle.server_only = 1;
    CHECK(init_runtime(&runtime), "initialize live skeleton-trap world");
    gm_runtime_set_pose(&runtime, 8.5, 4.0, 8.5, 0.0F, 0.0F);
    CHECK(gm_runtime_spawn_horse_fixture(
              &runtime, GM_MOB_SKELETON_HORSE, 8399,
              12.5, 4.0, 8.5, 0.0, 0.0, 0.0,
              0.0F, 15.0F, 0, 15.0, 0.20000000298023224,
              0.5, 0, 0, 0, 0, 0, 0, 1, 123, 0, 0, 0)
              && gm_runtime_restore_no_ai_mob_state(
                  &runtime, 8399, 300, -1, 0, 0.0F, 0, 0, 0, 0.0F,
                  UINT64_C(0x23456789abcd), 0, 0.0)
              && gm_runtime_set_mob_no_ai(&runtime, 8399, 0)
              && gm_runtime_set_entity_id_cursor(&runtime, 8400)
              && gm_runtime_set_entity_seed_generator_seed48(
                  &runtime, UINT64_C(0x123456789abc))
              && gm_runtime_set_server_uuid_random_seed48(
                  &runtime, UINT64_C(0x3456789abcde))
              && gm_runtime_set_math_random_seed48(&runtime, 0),
          "stage live skeleton-trap RNG boundary");
    runtime.mobs_enabled = 1;
    runtime.controlled_mobs_enabled = 0;
    runtime.mobs.natural_spawning_enabled = 0;
    runtime.gamerules.doMobSpawning = 0;
    gm_runtime_tick(&runtime, idle);
    if (!(gm_mobs_living_count(&runtime.mobs) == 8
              && runtime.next_entity_id == 8407
              && runtime.entity_seed_generator_seed48
                  == UINT64_C(224432795029324)
              && runtime.server_uuid_random_seed48
                  == UINT64_C(278761879117434))) {
        fprintf(stderr,
                "trap debug: living=%d next=%d entity=%llu uuid=%llu "
                "cold=%d loaded=%d\n",
                gm_mobs_living_count(&runtime.mobs), runtime.next_entity_id,
                (unsigned long long)runtime.entity_seed_generator_seed48,
                (unsigned long long)runtime.server_uuid_random_seed48,
                runtime.mobs.living_cold_count,
                runtime.mobs.loaded_order_count);
        CHECK(0, "live trap activation constructs the complete group once");
    }
    CHECK(runtime.lightning_count == 1
              && runtime.lightning[0].active
              && runtime.lightning[0].effect_only
              && runtime.lightning[0].eid == 8400
              && runtime.lightning[0].lightning_state == 2,
          "live trap installs an unticked effect-only weather entity");
    CHECK(runtime.loaded_entity_order_count == 8,
          "runtime loaded-entity order includes all trap births");
    for (int order = 0; order < 8; ++order)
        CHECK(gm_runtime_loaded_entity_order_get(
                  &runtime, order, &eid)
                  && eid == 8399 + order,
              "runtime trap loaded order is contiguous and exact");
    for (int order = 0; order < 8; ++order) {
        CHECK(gm_mobs_skeleton_trap_entity_state(
                  &runtime.mobs, 8399 + order, &rider)
                  && rider.entity_seed48 == expected_seed48[order]
                  && rider.entity_have_gaussian == (order != 0)
                  && double_bits(rider.entity_gaussian)
                      == expected_gaussian_bits[order],
              "same-tick trap entity RNG and Gaussian caches match Java");
        CHECK(fabs(rider.x - expected_position[order][0]) < 5.0E-13
                  && fabs(rider.y - expected_position[order][1]) < 5.0E-13
                  && fabs(rider.z - expected_position[order][2]) < 5.0E-13
                  && fabs(rider.vx - expected_motion[order][0]) < 5.0E-14
                  && fabs(rider.vy - expected_motion[order][1]) < 5.0E-14
                  && fabs(rider.vz - expected_motion[order][2]) < 5.0E-14,
              "same-tick trap positions and motion match translated Java");
    }
    CHECK(runtime.mobs.tick_update_order_count == 8,
          "new trap entities update in their spawn tick");
    for (int order = 0; order < 8; ++order)
        CHECK(runtime.mobs.tick_update_order[order] == 8399 + order,
              "same-tick trap update order follows Java loaded list");
    CHECK(gm_mobs_get_horse_state(&runtime.mobs, 8399, &original)
              && !original.trap && original.trap_time == 123,
          "live original horse keeps the measured trap lifecycle state");
    for (int pair = 0; pair < 4; ++pair) {
        int horse_eid = pair ? 8399 + pair * 2 : 8399;
        int rider_eid = 8400 + pair * 2;
        CHECK(gm_mobs_skeleton_trap_entity_state(
                  &runtime.mobs, horse_eid, &horse)
                  && gm_mobs_skeleton_trap_entity_state(
                      &runtime.mobs, rider_eid, &rider)
                  && horse.rider_eid == rider_eid
                  && rider.vehicle_eid == horse_eid
                  && rider.hurt_resistant_time == 59
                  && (pair == 0 || horse.hurt_resistant_time == 59)
                  && rider.x == horse.x && rider.z == horse.z
                  && rider.y == horse.y
                      + (double)1.6F * 0.75D - 0.1875D - 0.6D,
              "live trap pairs retain mount pose and same-tick immunity age");
    }
    {
        int trace = getenv("GM_TRACE_TRAP") != NULL;
        for (int tick = 1; tick <= 20; ++tick) {
            for (int order = 0; order < 8; ++order) {
                CHECK(gm_mobs_skeleton_trap_entity_state(
                          &runtime.mobs, 8399 + order, &rider),
                      "twenty-tick trap entity remains represented");
                if (trace)
                    fprintf(stderr,
                        "TRAP,%d,%d,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%llu,%d,%d,%d,%d,%d,%u,%.9g,%.9g,%.9g,%d,%.17g,%.17g,%.17g,%d,%u,%u\n",
                        tick, 8399 + order, rider.x, rider.y, rider.z,
                        rider.vx, rider.vy, rider.vz,
                        (unsigned long long)rider.entity_seed48,
                        runtime.mobs.witch_see_time[
                            gm_mobs_find_slot_by_eid(
                                &runtime.mobs, 8399 + order)],
                        runtime.mobs.passive_eat_time[
                            gm_mobs_find_slot_by_eid(
                                &runtime.mobs, 8399 + order)],
                        runtime.mobs.passive_task_tick[
                            gm_mobs_find_slot_by_eid(
                                &runtime.mobs, 8399 + order)],
                        runtime.mobs.skeleton_trap_target_player[
                            gm_mobs_find_slot_by_eid(
                                &runtime.mobs, 8399 + order)],
                        (runtime.mobs.current
                            ? runtime.mobs.b.attack_time
                            : runtime.mobs.a.attack_time)[
                                gm_mobs_find_slot_by_eid(
                                    &runtime.mobs, 8399 + order)],
                        runtime.mobs.sheep_ai_tick_count[
                            gm_mobs_find_slot_by_eid(
                                &runtime.mobs, 8399 + order)],
                        (runtime.mobs.current
                            ? runtime.mobs.b.yaw : runtime.mobs.a.yaw)[
                                gm_mobs_find_slot_by_eid(
                                    &runtime.mobs, 8399 + order)],
                        runtime.mobs.passive_move_forward[
                            gm_mobs_find_slot_by_eid(
                                &runtime.mobs, 8399 + order)],
                        runtime.mobs.passive_move_strafe[
                            gm_mobs_find_slot_by_eid(
                                &runtime.mobs, 8399 + order)],
                        (runtime.mobs.current
                            ? runtime.mobs.b.path_len
                            : runtime.mobs.a.path_len)[
                                gm_mobs_find_slot_by_eid(
                                    &runtime.mobs, 8399 + order)],
                        (runtime.mobs.current
                            ? runtime.mobs.b.path_tx
                            : runtime.mobs.a.path_tx)[
                                gm_mobs_find_slot_by_eid(
                                    &runtime.mobs, 8399 + order)],
                        (runtime.mobs.current
                            ? runtime.mobs.b.path_ty
                            : runtime.mobs.a.path_ty)[
                                gm_mobs_find_slot_by_eid(
                                    &runtime.mobs, 8399 + order)],
                        (runtime.mobs.current
                            ? runtime.mobs.b.path_tz
                            : runtime.mobs.a.path_tz)[
                                gm_mobs_find_slot_by_eid(
                                    &runtime.mobs, 8399 + order)],
                        runtime.mobs.skeleton_trap_move_action[
                            gm_mobs_find_slot_by_eid(
                                &runtime.mobs, 8399 + order)],
                        runtime.mobs.skeleton_trap_move_terminal_ticks[
                            gm_mobs_find_slot_by_eid(
                                &runtime.mobs, 8399 + order)],
                        runtime.mobs.skeleton_trap_move_wait_latched[
                            gm_mobs_find_slot_by_eid(
                                &runtime.mobs, 8399 + order)]);
            }
            if (trace)
                fprintf(stderr,
                    "PLAYER,%d,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g\n",
                    tick,
                    runtime.player.ent.posX, runtime.player.ent.posY,
                    runtime.player.ent.posZ, runtime.player.ent.motionX,
                    runtime.player.ent.motionY, runtime.player.ent.motionZ);
            if (tick < 20) gm_runtime_tick(&runtime, idle);
        }
    }
    for (int order = 0; order < 8; ++order) {
        CHECK(gm_mobs_skeleton_trap_entity_state(
                  &runtime.mobs, 8399 + order, &rider)
                  && rider.entity_seed48 == expected_tick20_seed48[order],
              "twenty-tick trap RNG cursors match Java");
        CHECK(fabs(rider.x - expected_tick20_position[order][0]) < 5.0E-13
                  && fabs(rider.y - expected_tick20_position[order][1])
                      < 5.0E-13
                  && fabs(rider.z - expected_tick20_position[order][2])
                      < 5.0E-13
                  && fabs(rider.vx - expected_tick20_motion[order][0])
                      < 5.0E-14
                  && fabs(rider.vy - expected_tick20_motion[order][1])
                      < 5.0E-14
                  && fabs(rider.vz - expected_tick20_motion[order][2])
                      < 5.0E-14,
              "twenty-tick trap position and motion match translated Java");
    }
    {
        const char *trace_ticks_env = getenv("GM_TRACE_TRAP_TICKS");
        int trace_ticks = trace_ticks_env ? atoi(trace_ticks_env) : 20;
        if (trace_ticks > 1200) trace_ticks = 1200;
        for (int tick = 21; tick <= trace_ticks; ++tick) {
            gm_runtime_tick(&runtime, idle);
            for (int order = 0; order < 8; ++order) {
                CHECK(gm_mobs_skeleton_trap_entity_state(
                          &runtime.mobs, 8399 + order, &rider),
                      "extended trap trace entity remains represented");
                fprintf(stderr,
                    "TRAP,%d,%d,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%llu,%d,%d,%d,%d,%d,%u,%.9g,%.9g,%.9g,%d,%.17g,%.17g,%.17g,%d,%u,%u\n",
                    tick, 8399 + order, rider.x, rider.y, rider.z,
                    rider.vx, rider.vy, rider.vz,
                    (unsigned long long)rider.entity_seed48,
                    runtime.mobs.witch_see_time[
                        gm_mobs_find_slot_by_eid(
                            &runtime.mobs, 8399 + order)],
                    runtime.mobs.passive_eat_time[
                        gm_mobs_find_slot_by_eid(
                            &runtime.mobs, 8399 + order)],
                    runtime.mobs.passive_task_tick[
                        gm_mobs_find_slot_by_eid(
                            &runtime.mobs, 8399 + order)],
                    runtime.mobs.skeleton_trap_target_player[
                        gm_mobs_find_slot_by_eid(
                            &runtime.mobs, 8399 + order)],
                    (runtime.mobs.current
                        ? runtime.mobs.b.attack_time
                        : runtime.mobs.a.attack_time)[
                            gm_mobs_find_slot_by_eid(
                                &runtime.mobs, 8399 + order)],
                    runtime.mobs.sheep_ai_tick_count[
                        gm_mobs_find_slot_by_eid(
                            &runtime.mobs, 8399 + order)],
                    (runtime.mobs.current
                        ? runtime.mobs.b.yaw : runtime.mobs.a.yaw)[
                            gm_mobs_find_slot_by_eid(
                                &runtime.mobs, 8399 + order)],
                    runtime.mobs.passive_move_forward[
                        gm_mobs_find_slot_by_eid(
                            &runtime.mobs, 8399 + order)],
                    runtime.mobs.passive_move_strafe[
                        gm_mobs_find_slot_by_eid(
                            &runtime.mobs, 8399 + order)],
                    (runtime.mobs.current
                        ? runtime.mobs.b.path_len
                        : runtime.mobs.a.path_len)[
                            gm_mobs_find_slot_by_eid(
                                &runtime.mobs, 8399 + order)],
                    (runtime.mobs.current
                        ? runtime.mobs.b.path_tx
                        : runtime.mobs.a.path_tx)[
                            gm_mobs_find_slot_by_eid(
                                &runtime.mobs, 8399 + order)],
                    (runtime.mobs.current
                        ? runtime.mobs.b.path_ty
                        : runtime.mobs.a.path_ty)[
                            gm_mobs_find_slot_by_eid(
                                &runtime.mobs, 8399 + order)],
                    (runtime.mobs.current
                        ? runtime.mobs.b.path_tz
                        : runtime.mobs.a.path_tz)[
                            gm_mobs_find_slot_by_eid(
                                &runtime.mobs, 8399 + order)],
                    runtime.mobs.skeleton_trap_move_action[
                        gm_mobs_find_slot_by_eid(
                            &runtime.mobs, 8399 + order)],
                    runtime.mobs.skeleton_trap_move_terminal_ticks[
                        gm_mobs_find_slot_by_eid(
                            &runtime.mobs, 8399 + order)],
                    runtime.mobs.skeleton_trap_move_wait_latched[
                        gm_mobs_find_slot_by_eid(
                            &runtime.mobs, 8399 + order)]);
            }
            fprintf(stderr,
                "PLAYER,%d,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g\n",
                tick,
                runtime.player.ent.posX, runtime.player.ent.posY,
                runtime.player.ent.posZ, runtime.player.ent.motionX,
                runtime.player.ent.motionY, runtime.player.ent.motionZ);
            for (int projectile = 0;
                    projectile < GM_RUNTIME_PROJECTILES; ++projectile) {
                const GmRuntimeProjectile *arrow =
                    &runtime.projectiles[projectile];
                if (arrow->active && arrow->type == 2)
                    fprintf(stderr,
                        "ARROW,%d,%d,%d,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%d,%llu\n",
                        tick, arrow->eid, arrow->shooter_eid,
                        arrow->x, arrow->y, arrow->z,
                        arrow->vx, arrow->vy, arrow->vz, arrow->age,
                        (unsigned long long)arrow->random_seed48);
            }
        }
    }
    gm_runtime_destroy(&runtime);
    return 1;
}

static int trap_expiry_removes_loaded_order(void) {
    GmRuntime runtime;
    GmAction idle;
    int eid = -1;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    CHECK(init_runtime(&runtime), "initialize skeleton trap fixture");
    CHECK(gm_runtime_spawn_horse_fixture(
              &runtime, GM_MOB_SKELETON_HORSE, 8101,
              8.5, 100.0, 12.5, 0.0, 0.0, 0.0,
              0.0F, 15.0F, 1, 15.0, 0.2, 0.5,
              0, GM_HORSE_TAME, 0, 0, 0, 0, 1, 18000,
              0, 0, 0)
              && gm_runtime_loaded_entity_order_get(&runtime, 0, &eid)
              && eid == 8101,
          "stage skeleton trap in loaded-entity order");
    gm_runtime_tick(&runtime, idle);
    CHECK(gm_mobs_living_count(&runtime.mobs) == 0
              && !gm_runtime_loaded_entity_order_get(&runtime, 0, &eid),
          "skeleton trap expiry removes its loaded-entity order row");
    gm_runtime_destroy(&runtime);
    return 1;
}

int main(void) {
    if (!exact_family_and_inventory()) return 1;
    if (!horse_inventory_container()) return 1;
    if (!donkey_chest_interaction()) return 1;
    if (!feeding_semantics()) return 1;
    if (!taming_attempt_semantics()) return 1;
    if (!automatic_taming_scheduler()) return 1;
    if (!genetics_semantics()) return 1;
    if (!mating_boundary_semantics()) return 1;
    if (!automatic_crossbreed_scheduler()) return 1;
    if (!mount_jump_and_fall_edges()) return 1;
    if (!explicit_dismount_edges()) return 1;
    if (!armor_and_death_inventory()) return 1;
    if (!capacity_rejection_is_atomic()) return 1;
    if (!checkpoint_continuation()) return 1;
    if (!skeleton_trap_construction_boundary()) return 1;
    if (!skeleton_trap_live_scheduler()) return 1;
    if (!trap_expiry_removes_loaded_order()) return 1;
    puts("horse runtime: PASS");
    return 0;
}
