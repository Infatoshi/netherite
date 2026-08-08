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

static int find_item(const GmRuntime *r, int item, ICStack *out) {
    for (int slot = 0; slot < ISR_MAIN_SLOTS; ++slot) {
        ICStack stack = isr_get_stack(&r->player.inv, slot);
        if (stack.item == item && stack.count > 0) {
            if (out) *out = stack;
            return 1;
        }
    }
    return 0;
}

int main(void) {
    const uint64_t seed = UINT64_C(0x123456789abc);
    GmRuntime r;
    GmAction idle;
    GmRuntimeFireworkEvent event;
    GmRuntimeSoundEvent sound;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    CHECK(init_flat(&r), "initialize free rocket fixture");
    CHECK(gm_runtime_set_next_firework_random_state(&r, seed, 0, 0.0),
          "inject EntityFireworkRocket cursor");
    CHECK(gm_runtime_spawn_firework(&r, 40.0, 100.0, 40.0, 2, 0, 0) >= 0,
          "spawn free rocket");
    printf("S %016llx %016llx %016llx %d\n",
           dbits(r.fireworks[0].vx), dbits(r.fireworks[0].vy),
           dbits(r.fireworks[0].vz), r.fireworks[0].lifetime);
    gm_runtime_tick_fireworks(&r);
    printf("T %016llx %016llx %016llx %016llx %016llx %016llx %d\n",
           dbits(r.fireworks[0].x), dbits(r.fireworks[0].y),
           dbits(r.fireworks[0].z), dbits(r.fireworks[0].vx),
           dbits(r.fireworks[0].vy), dbits(r.fireworks[0].vz),
           r.fireworks[0].age);
    CHECK(gm_runtime_firework_event_count(&r) == 1
          && gm_runtime_firework_event_get(&r, 0, &event)
          && event.kind == GM_FIREWORK_EVENT_LAUNCH
          && event.volume == 3.0F && event.pitch == 1.0F,
          "first update emits launch sound");
    CHECK(gm_runtime_sound_event_count(&r) == 1
          && gm_runtime_sound_event_get(&r, 0, &sound)
          && sound.sound == GM_SOUND_FIREWORK_LAUNCH
          && sound.category == GM_SOUND_CATEGORY_AMBIENT
          && sound.eid == r.fireworks[0].eid
          && sound.volume == 3.0F && sound.pitch == 1.0F,
          "firework launch reaches ordered sound seam");
    {
        GmEntityView view;
        CHECK(gm_runtime_projectile_views(&r, &view, 1) == 1
              && view.type == GM_VIEW_BILLBOARD && view.item_id == 401,
              "unattached rocket renders as fireworks item");
    }
    while (r.firework_count)
        gm_runtime_tick_fireworks(&r);
    CHECK(gm_runtime_firework_event_count(&r) == 2
          && gm_runtime_firework_event_get(&r, 1, &event)
          && event.kind == GM_FIREWORK_EVENT_EXPLODE,
          "rocket dies after Life exceeds LifeTime and emits status event");
    CHECK(gm_runtime_sound_event_count(&r) == 1,
          "rocket without explosion payload has no client blast sound");
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r), "initialize firework craft fixture");
    CHECK(gm_runtime_set_inventory(&r, 0, 339, 1, 0)
          && gm_runtime_set_inventory(&r, 1, 289, 1, 0)
          && gm_runtime_set_inventory(&r, 2, 289, 1, 0)
          && gm_runtime_set_inventory(
              &r, 3, 402, 1,
              ic_firework_meta_payload(0, 1, 1, 1)),
          "load paper, two gunpowder, and tagged star");
    {
        int grid[9] = {0, 1, -1, 2, 3, -1, -1, -1, -1};
        ICStack output;
        int flight, explosions, large, flicker;
        CHECK(gm_runtime_craft(&r, 2, grid),
              "RecipeFireworks rocket form matches");
        CHECK(find_item(&r, 401, &output)
              && output.count == 3
              && gm_runtime_firework_item_payload(
                  &r, &output, &flight, &explosions, &large, &flicker)
              && flight == 2 && explosions == 1 && large && flicker,
              "rocket craft preserves sound-relevant explosion payload");
    }
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r), "initialize firework star craft fixture");
    CHECK(gm_runtime_set_inventory(&r, 0, 289, 1, 0)
          && gm_runtime_set_inventory(&r, 1, 351, 1, 1)
          && gm_runtime_set_inventory(&r, 2, 385, 1, 0)
          && gm_runtime_set_inventory(&r, 3, 348, 1, 0),
          "load gunpowder, dye, fire charge, and glowstone");
    {
        int grid[9] = {0, 1, -1, 2, 3, -1, -1, -1, -1};
        ICStack output;
        int flight, explosions, large, flicker;
        CHECK(gm_runtime_craft(&r, 2, grid)
              && find_item(&r, 402, &output)
              && gm_runtime_firework_item_payload(
                  &r, &output, &flight, &explosions, &large, &flicker)
              && flight == 0 && explosions == 1 && large && flicker,
              "fire charge and glowstone become large/flicker payload bits");
    }
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r), "initialize elytra boost fixture");
    r.player.elytra_equipped = r.player.elytra_flying = 1;
    r.player.elytra_pose = 1;
    r.server_player.elytra_equipped = r.server_player.elytra_flying = 1;
    r.server_player.elytra_pose = 1;
    CHECK(gm_runtime_set_next_firework_random_state(&r, seed, 0, 0.0),
          "inject attached rocket cursor");
    CHECK(gm_runtime_spawn_firework(
              &r, 8.5, 30.0, 8.5, 1, 1, 1) >= 0,
          "spawn attached elytra rocket");
    r.fireworks[0].age = r.fireworks[0].lifetime;
    gm_runtime_tick_fireworks(&r);
    CHECK(r.firework_count == 0 && r.vitals.health == 13.0F
          && r.mobs.player_hurt_time == 9
          && r.mobs.player_hurt_resistant == 20,
          "attached one-star rocket boosts then applies exact seven damage");
    CHECK(r.player.ent.motionZ > 0.0
          && r.server_player.ent.motionZ == r.player.ent.motionZ,
          "attached rocket applies look-vector elytra acceleration");
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r), "initialize offhand elytra fixture");
    r.player.elytra_equipped = r.player.elytra_flying = 1;
    r.player.elytra_pose = 1;
    r.server_player.elytra_equipped = r.server_player.elytra_flying = 1;
    r.server_player.elytra_pose = 1;
    CHECK(gm_runtime_set_inventory(
              &r, r.player.inv.current_item, 0, 0, 0)
          && gm_runtime_set_inventory(
              &r, ISR_OFFHAND_SLOT, 401, 1,
              ic_firework_meta_payload(1, 1, 0, 0)),
          "stage empty-main offhand rocket");
    {
        GmAction use = idle;
        use.do_place = 1;
        gm_runtime_tick(&r, use);
    }
    CHECK(r.firework_count == 1 && r.fireworks[0].attached_player
          && r.fireworks[0].flight == 1
          && isr_get_stack(
              &r.player.inv, ISR_OFFHAND_SLOT).item == 0,
          "empty-main elytra use launches and consumes offhand rocket");
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r), "initialize main-hand firework priority fixture");
    r.player.elytra_equipped = r.player.elytra_flying = 1;
    r.player.elytra_pose = 1;
    r.server_player.elytra_equipped = r.server_player.elytra_flying = 1;
    r.server_player.elytra_pose = 1;
    CHECK(gm_runtime_set_inventory(
              &r, r.player.inv.current_item, 401, 2,
              ic_firework_meta_payload(3, 1, 0, 0))
          && gm_runtime_set_inventory(
              &r, ISR_OFFHAND_SLOT, 401, 2,
              ic_firework_meta_payload(1, 1, 0, 0)),
          "stage rockets in both hands");
    {
        GmAction use = idle;
        use.do_place = 1;
        gm_runtime_tick(&r, use);
    }
    CHECK(r.firework_count == 1 && r.fireworks[0].flight == 3
          && isr_get_stack(
              &r.player.inv, r.player.inv.current_item).count == 1
          && isr_get_stack(
              &r.player.inv, ISR_OFFHAND_SLOT).count == 2,
          "main-hand successful use preempts offhand rocket");
    gm_runtime_destroy(&r);

    CHECK(!gm_runtime_firework_event_get(NULL, 0, &event),
          "firework event API rejects invalid access");
    puts("firework: PASS (RNG, motion, recipes, hand priority, boost, damage, "
         "render/events)");
    return 0;
}
