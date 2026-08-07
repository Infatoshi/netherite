#include "game/runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(c, m) do { if (!(c)) { \
    fprintf(stderr, "FAIL: %s\n", (m)); return 1; } } while (0)

static unsigned long long dbits(double value) {
    union { double d; uint64_t u; } bits;
    bits.d = value;
    return (unsigned long long)bits.u;
}

static unsigned fbits(float value) {
    union { float f; uint32_t u; } bits;
    bits.f = value;
    return bits.u;
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
    gm_runtime_set_pose(r, 8.5, 30.0, 8.5, 0.0F, 0.0F);
    return 1;
}

static void set_random(JavaGaussianRandom *random, uint64_t seed48) {
    jrand_gaussian_set_state(random, seed48, 0, 0.0);
}

int main(void) {
    const uint64_t raw = UINT64_C(0x123456789abc);
    JavaGaussianRandom random;
    FishCatchState s;
    int event;

    memset(&s, 0, sizeof s); s.lure = 2; set_random(&random, raw);
    event = fish_catch_tick(&s, &random, 0, 1, 1, 1);
    printf("W %d %d\n", s.ticks_caught_delay, event);

    memset(&s, 0, sizeof s); s.ticks_catchable = 2; set_random(&random, raw);
    event = fish_catch_tick(&s, &random, 1, 0, 1, 1);
    printf("C %d %016llx %d\n", s.ticks_catchable, dbits(s.motion_y), event);

    memset(&s, 0, sizeof s); s.ticks_catchable_delay = 1;
    set_random(&random, raw);
    event = fish_catch_tick(&s, &random, 0, 1, 1, 1);
    printf("B %d %016llx %08x %d\n", s.ticks_catchable,
           dbits(s.motion_y), fbits(s.bite_pitch), event);

    memset(&s, 0, sizeof s); s.ticks_caught_delay = 1;
    set_random(&random, raw);
    event = fish_catch_tick(&s, &random, 0, 1, 1, 1);
    { union { float f; uint32_t u; } angle; angle.f = s.approach_angle;
      printf("A %d %d %08x %d\n", s.ticks_caught_delay,
             s.ticks_catchable_delay, angle.u, event); }

    {
        GmRuntime r;
        GmRuntimeFishEvent fish_event;
        GmEntityView view;
        ICStack rod = ic_mk(346, 1, 0);
        rod.n_enchants = 2;
        rod.enchants[0] = (IcEnch){62, 3};
        rod.enchants[1] = (IcEnch){61, 2};
        CHECK(init_flat(&r), "initialize fishing fixture");
        CHECK(gm_runtime_set_inventory_stack(&r, 0, rod), "load enchanted rod");
        CHECK(gm_runtime_set_next_fishing_random_state(&r, raw, 0, 0.0),
              "inject EntityFishHook cursor");
        CHECK(gm_runtime_cast_fishing_rod(&r, 3, 2) >= 0,
              "cast fishing hook");
        CHECK(r.fish_hook.active && r.fish_hook.catch_state.lure == 3
              && r.fish_hook.catch_state.luck == 2,
              "cast preserves Lure and Luck of the Sea");
        CHECK(gm_runtime_projectile_views(&r, &view, 1) == 1
              && view.ent_id == r.fish_hook.eid
              && view.type == GM_VIEW_BILLBOARD && view.item_id == 9004,
              "active hook uses exact fishing-particle sprite");
        CHECK(gm_runtime_fish_event_get(&r, 0, &fish_event)
              && fish_event.kind == GM_FISH_EVENT_THROW,
              "cast emits throw event");
        r.fish_hook.catch_state.ticks_catchable = 10;
        CHECK(gm_runtime_retract_fishing_rod(&r) == 1,
              "catch retract damages rod by one");
        rod = isr_get_stack(&r.player.inv, 0);
        CHECK(!r.fish_hook.active && rod.item == 346 && rod.meta == 1,
              "retract kills hook and applies rod durability");
        CHECK(gm_runtime_fish_event_count(&r) == 3
              && gm_runtime_fish_event_get(&r, 1, &fish_event)
              && fish_event.kind == GM_FISH_EVENT_CATCH
              && fish_event.item != 0 && fish_event.xp >= 1
              && fish_event.xp <= 6,
              "catch emits weighted item and XP event");
        gm_runtime_destroy(&r);
    }
    {
        GmRuntime r;
        GmRuntimeSoundEvent sound;
        CHECK(init_flat(&r), "initialize fishing sound fixture");
        CHECK(gm_runtime_set_inventory(&r, 0, 346, 1, 0)
              && gm_runtime_set_block(&r, 8, 30, 8, 8, 0),
              "load rod and fishing water");
        CHECK(gm_runtime_spawn_fish_hook_fixture(
                  &r, 92, 8.5, 30.5, 8.5, 0.0, 0.0, 0.0,
                  0.0F, 0.0F, 2, 0,
                  0, 0, 0, 0, 1, 0.0F, 0, 0, 0,
                  raw, 0, 0.0),
              "restore pre-bite bobber state");
        gm_runtime_tick_fishing(&r);
        CHECK(gm_runtime_sound_event_count(&r) == 1
              && gm_runtime_sound_event_get(&r, 0, &sound)
              && sound.sound == GM_SOUND_BOBBER_SPLASH
              && sound.category == GM_SOUND_CATEGORY_NEUTRAL
              && sound.eid == 92 && sound.volume == 0.25F
              && sound.pitch == r.fish_hook.catch_state.bite_pitch,
              "bite emits exact resolved bobber splash sound");
        gm_runtime_destroy(&r);
    }
    {
        GmRuntime r;
        ICStack rod = ic_mk(346, 1, 7);
        CHECK(init_flat(&r), "initialize saved fishing-hook fixture");
        CHECK(gm_runtime_set_inventory_stack(&r, 0, rod),
              "load rod for saved hook");
        CHECK(gm_runtime_spawn_mob_fixture(
                  &r, GM_MOB_PIG, 91, 9.5, 30.0, 8.5,
                  0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0),
              "restore exact hooked pig before bobber");
        CHECK(gm_runtime_spawn_fish_hook_fixture(
                  &r, 90, 9.5, 30.8, 8.5, 0.125, -0.25, 0.375,
                  15.0F, -5.0F, 0, 0, 0, 8, 0, 123, 0,
                  37.5F, 2, 3, 91, raw, 1, 0.75),
              "restore complete fishing-hook state");
        CHECK(r.fish_hook.active && r.fish_hook.caught_kind == 1
              && r.fish_hook.caught_eid == 91
              && r.fish_hook.catch_state.ticks_caught_delay == 123
              && r.fish_hook.random.random.seed == raw
              && r.fish_hook.random.have_next_next_gaussian
              && r.fish_hook.random.next_next_gaussian == 0.75,
              "saved hook retains timers, target, and RNG cursor");
        gm_runtime_tick_fishing(&r);
        CHECK(r.fish_hook.state == GM_FISH_STATE_HOOKED
              && r.fish_hook.vx == 0.0 && r.fish_hook.vy == 0.0
              && r.fish_hook.vz == 0.0,
              "flying hook with saved target enters hooked state");
        gm_runtime_tick_fishing(&r);
        CHECK(r.fish_hook.active && r.fish_hook.x == 9.5
              && r.fish_hook.z == 8.5,
              "hooked bobber follows restored target");
        gm_runtime_destroy(&r);
    }
    puts("fishing: PASS (timers, loot, hook/retract/events, persistence)");
    return 0;
}
