#include "game/runtime.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int fail;
#define CHECK(C, M) do { \
    if (!(C)) { fprintf(stderr, "FAIL: %s\n", M); fail = 1; } \
} while (0)

static int mob_slot(const GmRuntime *r, int eid) {
    const EwStore *s = r->mobs.current ? &r->mobs.b : &r->mobs.a;
    for (int slot = 1; slot < EW_MAX_ENTITIES; ++slot)
        if (s->alive[slot] && s->id[slot] == eid) return slot;
    return -1;
}

static int reset_sheep(
        GmRuntime *r, int eid, int x, int y, int z,
        int sheared, int age, uint64_t seed48) {
    gm_mobs_init(&r->mobs, 0);
    r->mobs.active_dimension = r->dimension;
    r->mobs_enabled = 1;
    r->controlled_mobs_enabled = 0;
    r->world_event_head = 0;
    r->world_event_count = 0;
    r->world_event_next_seq = 0;
    r->world_event_dropped = 0;
    gm_world_set_block_meta(r->world, x, y, z, 0, 0);
    gm_world_set_block_meta(r->world, x, y - 1, z, 2, 0);
    r->mobs.next_id = eid;
    if (gm_mobs_spawn(
            &r->mobs, GM_MOB_SHEEP, x + 0.5, y, z + 0.5) < 0)
        return 0;
    return gm_mobs_set_entity_random_state(
            &r->mobs, eid, seed48, 0, 0.0)
        && gm_runtime_set_sheep_state(r, eid, 14, sheared)
        && gm_runtime_set_mob_growing_age(r, eid, age);
}

int main(void) {
    GmConfig cfg;
    GmRuntime r;
    GmAction idle;
    char err[256];
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.view_distance = 1;
    cfg.mobs = 0;
    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err),
          "grazing runtime initializes");
    if (fail) return 1;
    int x = 8, z = 8;
    int y = gm_world_surface_y(r.world, x, z) + 1;
    gm_runtime_set_pose(&r, x + 0.5, y, z - 3.5, 0.0F, 0.0F);

    CHECK(reset_sheep(&r, 650000, x, y, z, 1, 0, 55),
          "natural grazing sheep initializes");
    r.mob_griefing = 1;
    gm_runtime_tick(&r, idle);
    int slot = mob_slot(&r, 650000);
    GmEntityView view;
    CHECK(slot >= 0 && r.mobs.sheep_eat_timer[slot] == 39
              && gm_mobs_event_count(&r.mobs) == 1
              && gm_mobs_fill_views(&r.mobs, &view, 1) == 1
              && (view.flags & 16) && view.graze_y == 0.25F
              && view.graze_x == (float)(MC_PI / 5.0),
          "natural scheduler starts on goal tick zero and exposes timer 39 pose");
    for (int tick = 1; tick < 36; ++tick)
        gm_runtime_tick(&r, idle);
    slot = mob_slot(&r, 650000);
    GmRuntimeWorldEvent event;
    GmRuntimeSoundEvent sound;
    CHECK(slot >= 0 && r.mobs.sheep_eat_timer[slot] == 4
              && !(r.mobs.sheep_data[slot] & 16)
              && gm_world_block(r.world, x, y - 1, z) == 3
              && gm_runtime_world_event_count(&r) == 1
              && gm_runtime_world_event_get(&r, 0, &event)
              && event.id == 2001 && event.dimension == r.dimension
              && event.x == x && event.y == y - 1 && event.z == z
              && event.data == 2
              && gm_runtime_sound_event_count(&r) == 1
              && gm_runtime_sound_event_get(&r, 0, &sound)
              && sound.sound == GM_SOUND_BLOCK_GRASS_BREAK
              && sound.category == GM_SOUND_CATEGORY_BLOCKS
              && sound.x == (double)x + 0.5
              && sound.y == (double)(y - 1) + 0.5
              && sound.z == (double)z + 0.5
              && sound.volume == 1.0F && sound.pitch == 0.8F
              && gm_mobs_fill_views(&r.mobs, &view, 1) >= 1
              && view.graze_y == 1.0F
              && view.graze_x == (float)(MC_PI / 5.0),
          "update 36 regrows wool, mutates grass, routes event, and renders timer 4");

    CHECK(reset_sheep(&r, 650100, x, y, z, 1, -2400, 55),
          "child no-grief sheep initializes");
    r.mob_griefing = 0;
    for (int tick = 0; tick < 36; ++tick)
        gm_runtime_tick(&r, idle);
    slot = mob_slot(&r, 650100);
    CHECK(slot >= 0 && r.mobs.sheep_eat_timer[slot] == 4
              && !(r.mobs.sheep_data[slot] & 16)
              && r.mobs.growing_age[slot] == -1164
              && gm_world_block(r.world, x, y - 1, z) == 2
              && gm_runtime_world_event_count(&r) == 0,
          "mobGriefing false preserves grass but regrows and advances child age");

    CHECK(reset_sheep(&r, 650200, x, y, z, 1, 0, 55),
          "panic interruption sheep initializes");
    r.mob_griefing = 1;
    gm_runtime_tick(&r, idle);
    slot = mob_slot(&r, 650200);
    if (slot >= 0) r.mobs.panic_ticks[slot] = 5;
    gm_runtime_tick(&r, idle);
    slot = mob_slot(&r, 650200);
    CHECK(slot >= 0 && r.mobs.sheep_eat_timer[slot] == 0
              && (r.mobs.sheep_data[slot] & 16)
              && gm_world_block(r.world, x, y - 1, z) == 2
              && gm_runtime_world_event_count(&r) == 0,
          "higher-priority panic interrupts grazing before its effect");

    gm_runtime_destroy(&r);
    if (fail) return 1;
    puts("PASS grazing runtime: scheduler, regrowth, gamerule, event, pose, panic");
    return 0;
}
