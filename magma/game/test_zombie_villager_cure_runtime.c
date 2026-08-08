#include "game/runtime.h"

#include <stdio.h>

static int fail;
#define CHECK(C, M) do { \
    if (!(C)) { fprintf(stderr, "FAIL: %s\n", M); fail = 1; } \
} while (0)

static EwStore *store(GmMobLive *m) {
    return m->current ? &m->b : &m->a;
}

static int init_flat(GmRuntime *r) {
    GmConfig config;
    char error[256];
    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.seed = 0;
    if (!gm_runtime_init(r, &config, error, sizeof error)) {
        fprintf(stderr, "init: %s\n", error);
        return 0;
    }
    gm_runtime_set_pose(r, 8.5, 5.0, 8.5, 0.0F, 24.0F);
    return 1;
}

int main(void) {
    enum { OLD_EID = 673000, NEW_EID = 673001 };
    GmRuntime runtime;
    CHECK(init_flat(&runtime), "initialize cure runtime");
    if (fail) return 1;
    runtime.mobs_enabled = 0;
    runtime.controlled_mobs_enabled = 0;
    CHECK(gm_runtime_spawn_mob_fixture(
              &runtime, EW_TYPE_ZOMBIE_VILLAGER, OLD_EID,
              8.5, 5.0, 10.5, 0.0, 0.0, 0.0, 0.0F,
              20.0F, 1, 0, 0, 0),
          "spawn clickable zombie-villager fixture");
    EwStore *state = store(&runtime.mobs);
    int slot = -1;
    for (int i = 1; i < EW_MAX_ENTITIES; ++i)
        if (state->alive[i] && state->id[i] == OLD_EID) {
            slot = i;
            break;
        }
    CHECK(slot > 0, "locate zombie-villager fixture slot");
    if (slot <= 0) return 1;
    runtime.mobs.villager_profession[slot] = 3;
    CHECK(gm_mobs_set_entity_random_state(
              &runtime.mobs, OLD_EID, 0, 0, 0.0D)
              && gm_mobs_apply_potion_effect(
                  &runtime.mobs, slot, 18, 0, 100),
          "seed cure RNG and apply Weakness");
    isr_init(&runtime.player.inv);
    runtime.player.inv.current_item = 0;
    isr_set_stack(&runtime.player.inv, 0, ic_mk(322, 2, 0));

    GmAction use = {0}, idle = {0};
    use.hotbar_sel = idle.hotbar_sel = -1;
    use.use = use.do_place = 1;
    gm_runtime_tick(&runtime, use);
    CHECK(runtime.server_feed_animal_pending == 5
              && runtime.server_feed_animal_eid == OLD_EID
              && runtime.server_feed_animal_hand == 0
              && isr_get_stack(&runtime.player.inv, 0).count == 2,
          "client entity use queues delayed main-hand cure");

    gm_runtime_tick(&runtime, idle);
    int conversion = -1;
    PtMobEffect strength;
    int cure_exact = !runtime.server_feed_animal_pending
              && isr_get_stack(&runtime.player.inv, 0).count == 1
              && gm_mobs_zombie_villager_conversion_state(
                  &runtime.mobs, OLD_EID, &conversion)
              && conversion >= 3600 && conversion <= 6000
              && gm_mobs_potion_effect_count(&runtime.mobs, slot) == 1
              && gm_mobs_potion_effect_get(
                  &runtime.mobs, slot, 0, &strength)
              && strength.id == 5 && strength.duration == conversion
              && strength.amplifier == 0;
    if (!cure_exact) {
        PtMobEffect row = {0};
        int got_effect = gm_mobs_potion_effect_get(
            &runtime.mobs, slot, 0, &row);
        fprintf(stderr,
            "cure diagnostic pending=%d count=%d converting=%d time=%d "
            "effects=%d got=%d effect=%d/%d/%d seed=%llu\n",
            runtime.server_feed_animal_pending,
            isr_get_stack(&runtime.player.inv, 0).count,
            gm_mobs_zombie_villager_conversion_state(
                &runtime.mobs, OLD_EID, NULL), conversion,
            gm_mobs_potion_effect_count(&runtime.mobs, slot), got_effect,
            row.id, row.duration, row.amplifier,
            (unsigned long long)runtime.mobs.entity_random[slot].random.seed);
    }
    CHECK(cure_exact,
          "server cure consumes apple and starts exact Strength timer");
    GmMobEvent status;
    CHECK(gm_mobs_event_count(&runtime.mobs) == 1
              && gm_mobs_event_get(&runtime.mobs, 0, &status)
              && status.kind == GM_MOB_EVENT_ENTITY_STATUS
              && status.eid == OLD_EID && status.data == 16,
          "server cure emits entity status 16");
    int cure_sound_found = 0;
    for (int i = 0; i < gm_runtime_sound_event_count(&runtime); ++i) {
        GmRuntimeSoundEvent sound;
        if (gm_runtime_sound_event_get(&runtime, i, &sound)
                && sound.sound == GM_SOUND_ZOMBIE_VILLAGER_CURE
                && sound.category == GM_SOUND_CATEGORY_HOSTILE
                && sound.eid == OLD_EID
                && sound.x == 9.0 && sound.y == 5.5 && sound.z == 11.0
                && sound.volume >= 1.0F && sound.volume < 2.0F
                && sound.pitch >= 0.3F && sound.pitch < 1.0F) {
            cure_sound_found = 1;
            break;
        }
    }
    CHECK(cure_sound_found,
          "client status 16 produces the positioned hostile cure sound");

    runtime.mobs.zombie_villager_conversion_time[slot] = 1;
    runtime.mobs.entity_effects[slot][0].duration = 1;
    runtime.next_entity_id = NEW_EID;
    runtime.controlled_mobs_enabled = 1;
    gm_runtime_tick(&runtime, idle);
    state = store(&runtime.mobs);
    PtMobEffect nausea;
    CHECK(state->alive[slot] && state->id[slot] == NEW_EID
              && state->type[slot] == EW_TYPE_VILLAGER
              && state->health[slot] == 20.0F
              && runtime.mobs.villager_profession[slot] == 3
              && runtime.mobs.controlled_no_ai[slot]
              && runtime.mobs.growing_age[slot] == 0
              && gm_mobs_potion_effect_count(&runtime.mobs, slot) == 1
              && gm_mobs_potion_effect_get(
                  &runtime.mobs, slot, 0, &nausea)
              && nausea.id == 9 && nausea.duration == 200
              && nausea.amplifier == 0,
          "conversion tick replaces zombie with exact villager state");
    GmRuntimeWorldEvent world_event;
    CHECK(gm_runtime_world_event_count(&runtime) == 1
              && gm_runtime_world_event_get(&runtime, 0, &world_event)
              && world_event.id == 1027 && world_event.data == 0
              && world_event.x == 8 && world_event.y == 5
              && world_event.z == 10,
          "terminal conversion drains world event 1027 into runtime audio");

    gm_runtime_destroy(&runtime);
    if (fail) return 1;
    puts("zombie_villager_cure_runtime: PASS");
    return 0;
}
