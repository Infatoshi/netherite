#include "entity_witch.h"
#include "game/runtime.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>

static int fail;
#define CHECK(c, m) do { \
    if (!(c)) { fprintf(stderr, "FAIL: %s\n", m); fail = 1; } \
} while (0)

typedef struct {
    int potion;
    int in_water;
    int burning;
    int target;
    float health;
    int pitch_draw;
    uint64_t final_seed48;
} StartCase;

static EwStore *store(GmMobLive *m) {
    return m->current ? &m->b : &m->a;
}

static void set_health(GmMobLive *m, int slot, float health) {
    m->a.health[slot] = health;
    m->b.health[slot] = health;
}

static int held_view(const GmMobLive *m) {
    GmEntityView views[EW_MAX_ENTITIES];
    int count = gm_mobs_fill_views(m, views, EW_MAX_ENTITIES);
    for (int i = 0; i < count; ++i)
        if (views[i].type == GM_MOB_WITCH && views[i].item_id == 373
                && views[i].item_count == 1 && (views[i].flags & 32))
            return 1;
    return 0;
}

int main(void) {
    static const StartCase starts[] = {
        {EWITCH_SELF_WATER_BREATHING, 1, 0, 0, 26.0F, 16532,
            UINT64_C(11718085204285)},
        {EWITCH_SELF_FIRE_RESISTANCE, 0, 1, 0, 26.0F, 698452,
            UINT64_C(49720483695876)},
        {EWITCH_SELF_HEALING, 0, 0, 0, 20.0F, 2963571,
            UINT64_C(102626409374399)},
        {EWITCH_SELF_SWIFTNESS, 0, 0, 1, 26.0F, 6117010,
            UINT64_C(25707281917278)},
    };
    static const int completion_potions[] = {
        EWITCH_SELF_WATER_BREATHING,
        EWITCH_SELF_FIRE_RESISTANCE,
        EWITCH_SELF_HEALING,
        EWITCH_SELF_SWIFTNESS,
    };
    static const int completion_effects[] = {13, 12, 6, 1};
    static const int completion_durations[] = {3600, 3600, 1, 3600};
    GmConfig config;
    GmRuntime runtime;
    char error[256] = {0};
    int healing_slot = -1;

    gm_config_defaults(&config);
    config.seed = 0;
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.mobs = 1;
    config.weather = 0;
    CHECK(gm_runtime_init(&runtime, &config, error, sizeof error), error);
    if (fail) return 1;
    gm_runtime_set_pose(&runtime, 8.5, 5.0, 8.5, 0.0F, 0.0F);

    /* One ordinary runtime tick proves the production path invokes the exact
     * helper, not only the cold fixture entry point. */
    {
        int slot = gm_mobs_spawn_witch(&runtime.mobs, 24.5, 5.0, 24.5);
        int eid = store(&runtime.mobs)->id[slot];
        int drinking = 0, timer = 0, potion = 0;
        set_health(&runtime.mobs, slot, 20.0F);
        CHECK(gm_mobs_set_entity_random_state(
                  &runtime.mobs, eid, 120, 0, 0.0),
              "production Witch RNG fixture stages");
        gm_runtime_tick(&runtime, (GmAction){.hotbar_sel = -1});
        CHECK(gm_mobs_witch_self_potion_state(
                  &runtime.mobs, eid, &drinking, &timer, &potion, NULL)
                  && drinking && timer == 32
                  && potion == EWITCH_SELF_HEALING,
              "ordinary runtime tick starts the Java-selected healing drink");
        CHECK(runtime.mobs.entity_random[slot].random.seed
                  == UINT64_C(233385770279446),
              "ordinary runtime tick preserves the composed Java RNG cursor");
        CHECK(held_view(&runtime.mobs),
              "live renderer view transports the held drinking potion");
        {
            GmRuntimeSoundEvent event;
            CHECK(gm_runtime_sound_event_count(&runtime) == 1
                      && gm_runtime_sound_event_get(&runtime, 0, &event)
                      && event.sound == GM_SOUND_WITCH_DRINK
                      && event.category == GM_SOUND_CATEGORY_HOSTILE
                      && event.eid == eid && event.volume == 1.0F,
                  "ordinary runtime drains Witch drink audio as HOSTILE");
        }
    }

    for (int c = 0; c < (int)(sizeof starts / sizeof starts[0]); ++c) {
        int slot = gm_mobs_spawn_witch(
            &runtime.mobs, 40.5 + c * 2.0, 5.0, 40.5);
        int eid = store(&runtime.mobs)->id[slot];
        int drinking = 0, timer = 0, potion = 0;
        float pitch = 0.0F;
        int event_before = gm_mobs_event_count(&runtime.mobs);
        GmMobEvent event;
        float expected_pitch = 0.8F
            + (float)starts[c].pitch_draw / 16777216.0F * 0.4F;
        set_health(&runtime.mobs, slot, starts[c].health);
        CHECK(gm_mobs_set_entity_random_state(
                  &runtime.mobs, eid, 0, 0, 0.0),
              "Witch start RNG fixture stages");
        CHECK(gm_mobs_witch_self_potion_step(
                  &runtime.mobs, eid, starts[c].in_water,
                  starts[c].burning, starts[c].target,
                  starts[c].target ? 16384.0 : 0.0),
              "Witch start transition accepts a live slot");
        CHECK(gm_mobs_witch_self_potion_state(
                  &runtime.mobs, eid, &drinking, &timer, &potion, &pitch)
                  && drinking && timer == 32
                  && potion == starts[c].potion
                  && pitch == expected_pitch,
              "Witch start state and drink pitch match Java");
        CHECK(gm_mobs_event_count(&runtime.mobs) == event_before + 1
                  && gm_mobs_event_get(
                      &runtime.mobs, event_before, &event)
                  && event.kind == GM_MOB_EVENT_SOUND
                  && event.eid == eid
                  && event.data == GM_MOB_SOUND_WITCH_DRINK
                  && event.volume == 1.0F
                  && event.pitch == expected_pitch,
              "Witch start emits the exact drink sound event");
        CHECK(runtime.mobs.entity_random[slot].random.seed
                  == starts[c].final_seed48,
              "Witch start consumes the exact Java RNG slice");
    }

    /* The final random roll maps to entity status 15 on its rare true edge. */
    {
        int slot = gm_mobs_spawn_witch(&runtime.mobs, 50.5, 5.0, 50.5);
        int eid = store(&runtime.mobs)->id[slot];
        int before = gm_mobs_event_count(&runtime.mobs);
        GmMobEvent event;
        CHECK(gm_mobs_set_entity_random_state(
                  &runtime.mobs, eid, UINT64_C(3846), 0, 0.0)
                  && gm_mobs_witch_self_potion_step(
                      &runtime.mobs, eid, 0, 0, 0, 0.0),
              "Witch particle-status fixture stages");
        CHECK(runtime.mobs.entity_random[slot].random.seed
                  == UINT64_C(202447189805),
              "rare status path consumes the fifth Java float");
        CHECK(gm_mobs_event_count(&runtime.mobs) == before + 1
                  && gm_mobs_event_get(&runtime.mobs, before, &event)
                  && event.kind == GM_MOB_EVENT_ENTITY_STATUS
                  && event.eid == eid && event.data == 15,
              "rare Witch roll emits authoritative entity status 15");
    }

    for (int c = 0; c < 4; ++c) {
        int eid = 7000 + c;
        float health = completion_potions[c] == EWITCH_SELF_HEALING
            ? 20.0F : 26.0F;
        int slot = gm_mobs_spawn_exact(
            &runtime.mobs, GM_MOB_WITCH, eid,
            60.5 + c * 2.0, 5.0, 60.5,
            0.0, 0.0, 0.0, 0.0F, health, 1, 0, 0, 0);
        int drinking = 1, timer = 0, potion = -1;
        PtMobEffect effect;
        runtime.mobs.witch_drinking[slot] = 1;
        runtime.mobs.witch_attack_timer[slot] = 0;
        runtime.mobs.witch_potion[slot] =
            (unsigned char)completion_potions[c];
        CHECK(gm_mobs_set_entity_random_state(
                  &runtime.mobs, eid, UINT64_C(5588), 0, 0.0)
                  && gm_mobs_witch_self_potion_step(
                      &runtime.mobs, eid, 0, 0, 0, 0.0),
              "Witch completion fixture transitions");
        CHECK(gm_mobs_witch_self_potion_state(
                  &runtime.mobs, eid, &drinking, &timer, &potion, NULL)
                  && !drinking && timer == -1
                  && potion == EWITCH_SELF_NONE,
              "Witch completion clears held state and decrements timer");
        CHECK(runtime.mobs.entity_random[slot].random.seed
                  == UINT64_C(140900883088207),
              "Witch completion consumes only the final status float");
        CHECK(gm_mobs_potion_effect_count(&runtime.mobs, slot) == 1
                  && gm_mobs_potion_effect_get(
                      &runtime.mobs, slot, 0, &effect)
                  && effect.id == completion_effects[c]
                  && effect.duration == completion_durations[c]
                  && effect.amplifier == 0,
              "Witch completion queues the exact Java potion effect");
        if (completion_potions[c] == EWITCH_SELF_HEALING)
            healing_slot = slot;
    }

    CHECK(healing_slot > 0, "healing completion slot exists");
    if (healing_slot > 0) {
        uint64_t world_seed = 0, math_seed = 0;
        int next_id = runtime.next_entity_id;
        gm_mobs_tick_controlled(
            &runtime.mobs, runtime.world, NULL,
            (struct PsvPlayer *)&runtime.player,
            runtime.ox, runtime.oz, runtime.dimension, &runtime.clock,
            0, &runtime.entities,
            &world_seed, &math_seed, &next_id);
        CHECK(store(&runtime.mobs)->health[healing_slot] == 24.0F
                  && gm_mobs_potion_effect_count(
                      &runtime.mobs, healing_slot) == 0,
              "queued instant healing applies and expires on the next tick");
    }

    gm_runtime_destroy(&runtime);
    if (fail) return 1;
    puts("witch_self_potion_live: PASS start=4 finish=4 runtime=1 "
         "drink_audio=5 status=15");
    return 0;
}
