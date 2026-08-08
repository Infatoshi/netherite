#include "game/runtime.h"

#include <stdio.h>
#include <string.h>

#define CHECK(c, m) do { if (!(c)) { \
    fprintf(stderr, "FAIL: %s\n", (m)); return 1; } } while (0)

static const int support_x[4] = {0, 0, -3, 3};
static const int support_z[4] = {-3, 3, 0, 0};

static int setup_finished_fight(GmRuntime *r) {
    GmConfig cfg;
    char err[256];
    gm_config_defaults(&cfg);
    cfg.seed = 0;
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.view_distance = 1;
    cfg.mobs = 0;
    cfg.weather = 0;
    if (!gm_runtime_init(r, &cfg, err, sizeof err)) {
        fprintf(stderr, "FAIL: %s\n", err);
        return 0;
    }
    if (!gm_runtime_set_dimension(r, 1)) return 0;
    gm_world_ensure(r->world, 0, 0, 4);
    gm_dragon_init(&r->dragon, r->world, r->seed);
    r->dragon.state.arena.dragon.alive = 0;
    r->dragon.state.arena.dragon.health = 0.0F;
    r->dragon.state.death_processed = 1;
    r->dragon.state.previously_killed = 1;
    gm_dragon_set_podium(&r->dragon, r->world, 1);
    gm_runtime_set_pose(r, 0.5, 64.0, -8.5, 0.0F, 0.0F);
    return 1;
}

static int place_pattern(GmRuntime *r) {
    for (int i = 0; i < 4; ++i) {
        gm_world_set_block(r->world, support_x[i], 63, support_z[i], 7);
        if (i == 0) {
            gm_world_set_block(r->world, support_x[i], 64, support_z[i], 31);
            gm_world_set_block(r->world, support_x[i], 65, support_z[i], 0);
        } else if (i == 1) {
            gm_world_set_block_meta(
                r->world, support_x[i], 64, support_z[i], 78, 0);
            gm_world_set_block(r->world, support_x[i], 65, support_z[i], 0);
        } else if (i == 2) {
            gm_world_set_block(r->world, support_x[i], 64, support_z[i], 106);
            gm_world_set_block(r->world, support_x[i], 65, support_z[i], 0);
        } else {
            gm_world_set_block_meta(
                r->world, support_x[i], 64, support_z[i], 175, 2);
            gm_world_set_block_meta(
                r->world, support_x[i], 65, support_z[i], 175, 8);
        }
        if (!gm_runtime_place_end_crystal(
                r, support_x[i], 63, support_z[i], -1))
            return 0;
        if (i < 3 && r->dragon_respawn_state != GM_DRAGON_RESPAWN_NONE)
            return 0;
    }
    return 1;
}

static int test_full_lifecycle(void) {
    GmRuntime r;
    CHECK(setup_finished_fight(&r), "initialize completed End fight");
    CHECK(place_pattern(&r), "place the four-cardinal crystal pattern");
    CHECK(r.end_crystal_count == 4
          && r.dragon_respawn_state == GM_DRAGON_RESPAWN_START
          && __builtin_popcountll(r.dragon_respawn_crystal_mask) == 4,
          "fourth placement starts resummoning immediately");
    CHECK(gm_world_block(r.world, 1, 63, 0) == 0,
          "resummoning removes the active exit portal");

    gm_runtime_tick_dragon_respawn_fixture(&r);
    CHECK(r.dragon_respawn_state == GM_DRAGON_RESPAWN_PREPARING
          && r.dragon_respawn_ticks == 0,
          "START lasts exactly one manager tick");
    for (int i = 0; i < GM_RUNTIME_END_CRYSTALS; ++i)
        if (r.dragon_respawn_crystal_mask & (UINT64_C(1) << i))
            CHECK(r.end_crystals[i].has_beam
                  && r.end_crystals[i].beam_x == 0
                  && r.end_crystals[i].beam_y == 128
                  && r.end_crystals[i].beam_z == 0,
                  "START aims all summoning beams at the origin");

    for (int i = 0; i < 101; ++i)
        gm_runtime_tick_dragon_respawn_fixture(&r);
    CHECK(r.dragon_respawn_state == GM_DRAGON_RESPAWN_PILLARS
          && r.dragon_respawn_ticks == 0,
          "PREPARING consumes ticks 0 through 100");
    CHECK(gm_runtime_world_event_count(&r) == 9,
          "PREPARING emits the exact nine event-3001 pulses");

    for (int tick = 0; tick <= 400; ++tick) {
        int index = tick / 40;
        if (index < ED_NUM_CRYSTALS && tick % 40 == 20) {
            int cx, cz, height;
            ed_pillar_spec((u64)r.seed, index, &cx, &cz, NULL, &height, NULL);
            gm_world_set_block(r.world, cx, height, cz, 0);
        }
        gm_runtime_tick_dragon_respawn_fixture(&r);
        if (index < ED_NUM_CRYSTALS && tick % 40 == 0) {
            int cx, cz, height;
            ed_pillar_spec((u64)r.seed, index, &cx, &cz, NULL, &height, NULL);
            for (int i = 0; i < GM_RUNTIME_END_CRYSTALS; ++i)
                if (r.dragon_respawn_crystal_mask & (UINT64_C(1) << i))
                    CHECK(r.end_crystals[i].beam_x == cx
                          && r.end_crystals[i].beam_y == height + 1
                          && r.end_crystals[i].beam_z == cz,
                          "pillar phase retargets all four beams");
        }
        if (index < ED_NUM_CRYSTALS && tick % 40 == 39) {
            int cx, cz, radius, height, guarded;
            const EdCrystal *crystal;
            ed_pillar_spec((u64)r.seed, index, &cx, &cz,
                           &radius, &height, &guarded);
            crystal = &r.dragon.state.arena.crystals[index];
            CHECK(gm_world_block(r.world, cx, height, cz) == 7
                  && gm_world_block(r.world, cx, height - 1, cz) == 49,
                  "pillar regeneration restores cap and obsidian body");
            CHECK(crystal->alive && crystal->invulnerable
                  && crystal->has_beam
                  && crystal->x == cx + 0.5
                  && crystal->y == height + 1.0
                  && crystal->z == cz + 0.5,
                  "pillar regeneration replaces its invulnerable crystal");
            if (guarded)
                CHECK(gm_world_block(r.world, cx + 2, height, cz) == 101
                      && gm_world_block(r.world, cx, height + 3, cz) == 101,
                      "guarded descriptors generate the iron-bar cage");
            (void)radius;
        }
    }
    CHECK(r.dragon_respawn_state == GM_DRAGON_RESPAWN_DRAGON
          && r.dragon_respawn_ticks == 0,
          "PILLARS consumes ticks 0 through 400");

    for (int i = 0; i < 101; ++i)
        gm_runtime_tick_dragon_respawn_fixture(&r);
    CHECK(r.dragon_respawn_state == GM_DRAGON_RESPAWN_NONE
          && r.dragon_respawn_crystal_mask == 0
          && r.end_crystal_count == 0,
          "DRAGON tick 100 consumes all four summoning crystals");
    CHECK(r.dragon.state.arena.dragon.alive
          && r.dragon.state.arena.dragon.health == 200.0F
          && r.dragon.state.arena.dragon.x == 0.0
          && r.dragon.state.arena.dragon.y == 128.0
          && r.dragon.state.arena.dragon.z == 0.0
          && !r.dragon.state.death_processed,
          "completion creates a fresh full-health dragon at the origin");
    CHECK(r.world_event_next_seq == 33
          && gm_runtime_world_event_count(&r)
              == GM_RUNTIME_WORLD_EVENT_CAPACITY
          && r.world_event_dropped == 17,
          "full sequence emits the exact thirty-three event-3001 pulses");
    for (int i = 0; i < ED_NUM_CRYSTALS; ++i)
        CHECK(!r.dragon.state.arena.crystals[i].invulnerable
              && !r.dragon.state.arena.crystals[i].has_beam,
              "completion clears spike-crystal invulnerability and beams");
    gm_runtime_destroy(&r);
    return 0;
}

static int test_attack_abort(void) {
    GmRuntime r;
    GmAction idle, attack;
    memset(&idle, 0, sizeof idle);
    memset(&attack, 0, sizeof attack);
    idle.hotbar_sel = attack.hotbar_sel = -1;
    attack.do_break = 1;
    attack.attack_entity = 1;
    CHECK(setup_finished_fight(&r), "initialize abort fixture");
    CHECK(place_pattern(&r), "start abort fixture");
    gm_runtime_set_pose(&r, 0.5, 64.0, -7.0, 0.0F, 0.0F);
    gm_runtime_tick(&r, attack);
    gm_runtime_tick(&r, idle);
    CHECK(r.dragon_respawn_state == GM_DRAGON_RESPAWN_NONE
          && r.dragon_respawn_crystal_mask == 0,
          "player attack on a summoning crystal aborts the sequence");
    CHECK(gm_world_block(r.world, 1, 63, 0) == 119,
          "abort restores the active exit portal");
    for (int i = 0; i < ED_NUM_CRYSTALS; ++i)
        CHECK(!r.dragon.state.arena.crystals[i].invulnerable
              && !r.dragon.state.arena.crystals[i].has_beam,
              "abort resets generated spike crystals");
    gm_runtime_destroy(&r);
    return 0;
}

int main(void) {
    if (test_full_lifecycle() || test_attack_abort()) return 1;
    puts("dragon_respawn: PASS (placement, 604-tick lifecycle, spikes, completion, abort)");
    return 0;
}
