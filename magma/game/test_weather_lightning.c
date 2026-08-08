#include "game/runtime.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(c, m) do { if (!(c)) { \
    fprintf(stderr, "FAIL: %s\n", (m)); return 1; } } while (0)

static const EwStore *mobs_now(const GmRuntime *r) {
    return r->mobs.current ? &r->mobs.b : &r->mobs.a;
}

static int init_flat(GmRuntime *r) {
    GmConfig cfg;
    char err[256];
    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.view_distance = 1;
    cfg.mobs = 0;
    cfg.weather = 0;
    if (!gm_runtime_init(r, &cfg, err, sizeof err)) {
        fprintf(stderr, "FAIL: %s\n", err);
        return 0;
    }
    gm_runtime_set_pose(r, 8.5, 5.0, 8.5, 0.0f, 0.0f);
    return 1;
}

int main(void) {
    GmRuntime r;
    GmAction idle;
    JavaRandom expected;
    GmRuntimeWeatherEvent event;
    GmRuntimeSoundEvent sound;
    uint64_t seed = UINT64_C(0x123456789abc);
    long long vertex;
    int living;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    CHECK(init_flat(&r), "initialize flat lightning fixture");

    jrand_set_seed48(&expected, seed);
    vertex = jrand_long(&expected);
    living = jrand_int_bound(&expected, 3) + 1;
    CHECK(gm_runtime_set_next_lightning_random_seed48(&r, seed),
          "inject lightning entity cursor");
    CHECK(gm_runtime_spawn_lightning(&r, 20.0, 4.0, 20.0, 1) >= 0,
          "spawn effect-only lightning");
    CHECK(r.lightning_count == 1
          && r.lightning[0].lightning_state == 2
          && r.lightning[0].living_time == living
          && r.lightning[0].bolt_vertex == vertex
          && r.lightning[0].random_seed48 == expected.seed,
          "constructor state and nextLong/nextInt cursor match Java");
    {
        float thunder_pitch = 0.8F + jrand_float(&expected) * 0.2F;
        float impact_pitch = 0.5F + jrand_float(&expected) * 0.2F;
        gm_runtime_tick(&r, idle);
        CHECK(r.lightning[0].lightning_state == 1
              && r.lightning[0].random_seed48 == expected.seed,
              "first update state and two sound draws match Java");
        CHECK(gm_runtime_weather_event_count(&r) == 2
              && gm_runtime_weather_event_get(&r, 0, &event)
              && event.kind == GM_WEATHER_EVENT_THUNDER
              && event.volume == 10000.0F
              && fabsf(event.pitch - thunder_pitch) < 1.0e-7F,
              "thunder event payload matches Java");
        CHECK(gm_runtime_weather_event_get(&r, 1, &event)
              && event.kind == GM_WEATHER_EVENT_IMPACT
              && event.volume == 2.0F
              && fabsf(event.pitch - impact_pitch) < 1.0e-7F,
              "impact event payload matches Java");
        CHECK(gm_runtime_sound_event_count(&r) == 2
              && gm_runtime_sound_event_get(&r, 0, &sound)
              && sound.sound == GM_SOUND_LIGHTNING_THUNDER
              && sound.category == GM_SOUND_CATEGORY_WEATHER
              && sound.volume == 10000.0F
              && fabsf(sound.pitch - thunder_pitch) < 1.0e-7F
              && gm_runtime_sound_event_get(&r, 1, &sound)
              && sound.sound == GM_SOUND_LIGHTNING_IMPACT
              && sound.volume == 2.0F
              && fabsf(sound.pitch - impact_pitch) < 1.0e-7F,
              "ordered sound seam preserves both lightning sounds");
    }
    for (int ticks = 0; ticks < 64 && r.lightning_count; ++ticks)
        gm_runtime_tick(&r, idle);
    CHECK(r.lightning_count == 0, "lightning lifetime terminates");
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r), "initialize strike fixture");
    {
        int creeper_slot = gm_mobs_spawn(
            &r.mobs, EW_TYPE_CREEPER, 8.0, 4.0, 8.0);
        int pig_slot = gm_mobs_spawn(
            &r.mobs, EW_TYPE_PIG, 9.0, 4.0, 8.0);
        int creeper_eid;
        int powered = 0;
        const EwStore *store;
        int pigs = 0, pigmen = 0;
        CHECK(creeper_slot >= 0 && pig_slot >= 0,
              "spawn creeper and pig strike targets");
        store = mobs_now(&r);
        creeper_eid = store->id[creeper_slot];
        CHECK(gm_runtime_set_next_lightning_random_seed48(&r, seed),
              "inject non-effect lightning cursor");
        CHECK(gm_runtime_spawn_lightning(&r, 8.0, 4.0, 8.0, 0) >= 0,
              "spawn damaging lightning");
        gm_runtime_tick(&r, idle);
        CHECK(r.vitals.health == 15.0F && r.player_fire_ticks == -19,
              "lightning applies raw five damage and Entity.fire increment");
        CHECK(gm_mobs_creeper_is_powered(
                  &r.mobs, creeper_eid, &powered) && powered,
              "lightning powers creeper");
        store = mobs_now(&r);
        for (int slot = 1; slot < EW_MAX_ENTITIES; ++slot) {
            if (!store->alive[slot]) continue;
            pigs += store->type[slot] == EW_TYPE_PIG;
            pigmen += store->type[slot] == EW_TYPE_PIGMAN;
        }
        CHECK(pigs == 0 && pigmen == 1,
              "lightning replaces pig with one zombie pigman");
        CHECK(gm_world_block(r.world, 8, 4, 8) == 51,
              "normal-difficulty constructor places supported fire");
    }
    gm_runtime_destroy(&r);
    CHECK(!gm_runtime_weather_event_get(NULL, 0, &event)
          && !gm_runtime_weather_event_get(&r, -1, &event),
          "weather event API rejects invalid access");
    puts("weather_lightning: PASS (RNG, lifecycle, sound, fire, strikes, conversion)");
    return 0;
}
