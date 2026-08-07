#include "game/runtime.h"

#include <stdio.h>
#include <string.h>

static int fail;
#define CHECK(C, M) do { \
    if (!(C)) { fprintf(stderr, "FAIL: %s\n", M); fail = 1; } \
} while (0)

static void tick(GmRuntime *r, int count)
{
    GmAction idle;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    for (int i = 0; i < count; ++i)
        gm_runtime_tick(r, idle);
}

static void stage_control(GmRuntime *r, int y, int block)
{
    r->scheduled_tick_count = 0;
    r->scheduled_tick_next_order = 0;
    for (int z = 7; z <= 9; ++z)
        for (int x = 9; x <= 12; ++x) {
            gm_world_set_block_meta(r->world, x, y - 2, z, 1, 0);
            gm_world_set_block_meta(r->world, x, y - 1, z, 1, 0);
            gm_world_set_block_meta(r->world, x, y, z, 0, 0);
            gm_world_set_block_meta(r->world, x, y + 1, z, 0, 0);
        }
    gm_world_set_block_meta(r->world, 10, y, 8, block, 5);
    gm_world_set_block_meta(r->world, 11, y, 8, 123, 0);
}

int main(void)
{
    GmConfig cfg;
    GmRuntime r;
    GmRuntimeScheduledTick pending;
    char err[256];

    gm_config_defaults(&cfg);
    cfg.view_distance = 1;
    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err),
          "redstone-use runtime initializes");
    if (fail) return 1;

    int y = gm_world_surface_y(r.world, 8, 8);
    gm_runtime_set_pose(&r, 8.5, (double)y, 8.5, 180.0f, 0.0f);

    stage_control(&r, y, 69);
    CHECK(gm_runtime_use_block(&r, 10, y, 8)
              && gm_world_meta(r.world, 10, y, 8) == 13
              && gm_world_block(r.world, 11, y, 8) == 124,
          "player use toggles a lever and powers its neighbor");
    long long lamp_due = r.clock.total_time + 4;
    CHECK(gm_runtime_use_block(&r, 10, y, 8)
              && gm_world_meta(r.world, 10, y, 8) == 5
              && gm_runtime_scheduled_tick_count(&r) == 1
              && gm_runtime_scheduled_tick_get(&r, 0, &pending)
              && pending.block == 124 && pending.time == lamp_due,
          "second lever use releases power and queues exact lamp delay");
    tick(&r, 4);
    CHECK(gm_world_block(r.world, 11, y, 8) == 123
              && gm_runtime_scheduled_tick_count(&r) == 0,
          "lever-controlled lamp turns off at exact +4");

    stage_control(&r, y, 77);
    CHECK(gm_runtime_use_block(&r, 10, y, 8)
              && gm_world_meta(r.world, 10, y, 8) == 13
              && gm_world_block(r.world, 11, y, 8) == 124
              && gm_runtime_scheduled_tick_count(&r) == 1
              && gm_runtime_scheduled_tick_get(&r, 0, &pending)
              && pending.block == 77
              && pending.time == r.clock.total_time + 20,
          "player use presses stone button and queues exact +20 release");
    CHECK(gm_runtime_use_block(&r, 10, y, 8)
              && gm_runtime_scheduled_tick_count(&r) == 1,
          "using an already-powered stone button is an exact no-op");
    tick(&r, 20);
    CHECK(gm_world_meta(r.world, 10, y, 8) == 5,
          "stone button releases at +20");
    CHECK(gm_world_block(r.world, 11, y, 8) == 124,
          "stone-button lamp remains lit during its handoff delay");
    CHECK(gm_runtime_scheduled_tick_count(&r) == 1
              && gm_runtime_scheduled_tick_get(&r, 0, &pending)
              && pending.block == 124
              && pending.time == r.clock.total_time + 4,
          "stone-button release queues the exact lamp handoff");
    tick(&r, 4);
    CHECK(gm_world_block(r.world, 11, y, 8) == 123
              && gm_runtime_scheduled_tick_count(&r) == 0,
          "stone-button lamp turns off at exact +4");

    stage_control(&r, y, 143);
    CHECK(gm_runtime_use_block(&r, 10, y, 8)
              && gm_world_meta(r.world, 10, y, 8) == 13
              && gm_world_block(r.world, 11, y, 8) == 124
              && gm_runtime_scheduled_tick_count(&r) == 1
              && gm_runtime_scheduled_tick_get(&r, 0, &pending)
              && pending.block == 143
              && pending.time == r.clock.total_time + 30,
          "player use presses wooden button and queues exact +30 poll");
    tick(&r, 30);
    CHECK(gm_world_meta(r.world, 10, y, 8) == 5
              && gm_world_block(r.world, 11, y, 8) == 124
              && gm_runtime_scheduled_tick_count(&r) == 1,
          "arrow-free wooden button releases at +30");
    tick(&r, 4);
    CHECK(gm_world_block(r.world, 11, y, 8) == 123
              && gm_runtime_scheduled_tick_count(&r) == 0,
          "wood-button lamp turns off at exact +4");

    stage_control(&r, y, 0);
    CHECK(gm_runtime_set_block(&r, 10, y, 8, 93, 2)
              && gm_runtime_use_block(&r, 10, y, 8)
              && gm_world_block(r.world, 10, y, 8) == 93
              && gm_world_meta(r.world, 10, y, 8) == 6
              && gm_runtime_scheduled_tick_count(&r) == 0,
          "player use cycles repeater delay one to delay two");
    CHECK(gm_runtime_use_block(&r, 10, y, 8)
              && gm_world_meta(r.world, 10, y, 8) == 10
              && gm_runtime_use_block(&r, 10, y, 8)
              && gm_world_meta(r.world, 10, y, 8) == 14
              && gm_runtime_use_block(&r, 10, y, 8)
              && gm_world_meta(r.world, 10, y, 8) == 2,
          "repeater delay cycles two to three to four to one");

    stage_control(&r, y, 0);
    CHECK(gm_runtime_set_block(&r, 10, y, 8, 149, 2)
              && gm_runtime_use_block(&r, 10, y, 8)
              && gm_world_block(r.world, 10, y, 8) == 149
              && gm_world_meta(r.world, 10, y, 8) == 6
              && gm_runtime_scheduled_tick_count(&r) == 0,
          "player use toggles comparator compare to subtract mode");
    CHECK(gm_runtime_use_block(&r, 10, y, 8)
              && gm_world_meta(r.world, 10, y, 8) == 2,
          "second comparator use toggles subtract back to compare mode");

    stage_control(&r, y, 0);
    gm_runtime_set_time(&r, 6000);
    gm_world_set_block_meta(r.world, 10, y, 8, 151, 15);
    gm_world_set_block_meta(r.world, 11, y, 8, 124, 0);
    CHECK(gm_runtime_load_sky_light_dim(&r, 0, 10, y, 8, 15)
              && gm_runtime_finalize_sky_light_snapshot_dim(&r, 0)
              && gm_runtime_use_block(&r, 10, y, 8)
              && gm_world_block(r.world, 10, y, 8) == 178
              && gm_world_meta(r.world, 10, y, 8) == 0
              && gm_runtime_scheduled_tick_count(&r) == 1,
          "noon detector use inverts 151:15 to 178:0 and releases its lamp");
    stage_control(&r, y, 0);
    gm_world_set_block_meta(r.world, 10, y, 8, 178, 0);
    CHECK(gm_runtime_load_sky_light_dim(&r, 0, 10, y, 8, 15)
              && gm_runtime_finalize_sky_light_snapshot_dim(&r, 0)
              && gm_runtime_use_block(&r, 10, y, 8)
              && gm_world_block(r.world, 10, y, 8) == 151
              && gm_world_meta(r.world, 10, y, 8) == 15
              && gm_world_block(r.world, 11, y, 8) == 124,
          "noon inverted-detector use restores 151:15 and powers its lamp");
    stage_control(&r, y, 0);
    gm_runtime_set_time(&r, 18000);
    gm_world_set_block_meta(r.world, 10, y, 8, 151, 0);
    CHECK(gm_runtime_load_sky_light_dim(&r, 0, 10, y, 8, 15)
              && gm_runtime_finalize_sky_light_snapshot_dim(&r, 0)
              && gm_runtime_use_block(&r, 10, y, 8)
              && gm_world_block(r.world, 10, y, 8) == 178
              && gm_world_meta(r.world, 10, y, 8) == 11
              && gm_world_block(r.world, 11, y, 8) == 124,
          "midnight inversion derives strength 11 from sky subtraction");
    stage_control(&r, y, 0);
    gm_runtime_set_time(&r, 6000);
    gm_runtime_set_total_time(&r, 19);
    CHECK(gm_runtime_set_block(&r, 10, y, 8, 151, 0)
              && gm_runtime_load_sky_light_dim(&r, 0, 10, y, 8, 15)
              && gm_runtime_finalize_sky_light_snapshot_dim(&r, 0),
          "stale noon detector restores with exact saved skylight");
    tick(&r, 1);
    CHECK(gm_world_block(r.world, 10, y, 8) == 151
              && gm_world_meta(r.world, 10, y, 8) == 15
              && gm_world_block(r.world, 11, y, 8) == 124
              && gm_world_block_light(r.world, 10, y, 8) == 14,
          "daylight tile recomputes and lights through its zero-opacity body");
    CHECK(gm_runtime_set_block(&r, 10, y, 8, 0, 0),
          "replacing a daylight detector retires its active tile");

    stage_control(&r, y, 0);
    CHECK(gm_runtime_set_block(&r, 10, y, 8, 92, 0),
          "whole-cake use fixture loads");
    gm_runtime_set_vitals(&r, 20.0f, 18);
    gm_runtime_set_food_stats(&r, 5.0f, 0.05f);
    CHECK(gm_runtime_use_block(&r, 10, y, 8)
              && gm_world_block(r.world, 10, y, 8) == 92
              && gm_world_meta(r.world, 10, y, 8) == 1
              && r.vitals.foodLevel == 20
              && r.vitals.saturation == 5.4f,
          "hungry player takes one cake bite and gains exact food stats");
    CHECK(gm_runtime_use_block(&r, 10, y, 8)
              && gm_world_meta(r.world, 10, y, 8) == 1,
          "full player consumes cake activation without another bite");
    gm_runtime_set_vitals(&r, 20.0f, 18);
    gm_world_set_block_meta(r.world, 10, y, 8, 92, 6);
    CHECK(gm_runtime_use_block(&r, 10, y, 8)
              && gm_world_block(r.world, 10, y, 8) == 0
              && r.vitals.foodLevel == 20,
          "seventh cake serving removes the block after feeding the player");

    {
        static const int pottable[][2] = {
            {6, 0}, {6, 1}, {6, 2}, {6, 3}, {6, 4}, {6, 5},
            {31, 2}, {32, 0}, {37, 0},
            {38, 0}, {38, 1}, {38, 2}, {38, 3}, {38, 4},
            {38, 5}, {38, 6}, {38, 7}, {38, 8},
            {39, 0}, {40, 0}, {81, 0},
        };
        int exact = 1;
        for (size_t i = 0; i < sizeof pottable / sizeof pottable[0]; ++i) {
            GmRuntimeFlowerPot pot;
            stage_control(&r, y, 0);
            gm_world_set_block_meta(r.world, 10, y, 8, 140, 0);
            if (!gm_runtime_flower_pot_set(
                    &r, 0, 10, y, 8, 0, 0))
                exact = 0;
            isr_set_stack(&r.player.inv, 0,
                ic_mk(pottable[i][0], 1, pottable[i][1]));
            r.player.inv.current_item = 0;
            if (!gm_runtime_use_block(&r, 10, y, 8)
                    || !gm_runtime_flower_pot_get(&r, 0, &pot)
                    || pot.item != pottable[i][0]
                    || pot.meta != pottable[i][1]
                    || isr_get_stack(&r.player.inv, 0).count != 0)
                exact = 0;
        }
        CHECK(exact, "all 21 canonical flower-pot contents insert exactly");
    }
    stage_control(&r, y, 0);
    gm_world_set_block_meta(r.world, 10, y, 8, 140, 0);
    CHECK(gm_runtime_flower_pot_set(&r, 0, 10, y, 8, 38, 2),
          "occupied flower-pot refusal fixture loads");
    isr_set_stack(&r.player.inv, 0, ic_mk(37, 1, 0));
    CHECK(!gm_runtime_use_block(&r, 10, y, 8)
              && isr_get_stack(&r.player.inv, 0).count == 1,
          "occupied flower pot refuses another plant without consumption");
    CHECK(gm_runtime_flower_pot_set(&r, 0, 10, y, 8, 0, 0),
          "invalid flower-pot refusal fixture clears");
    isr_set_stack(&r.player.inv, 0, ic_mk(1, 1, 0));
    CHECK(!gm_runtime_use_block(&r, 10, y, 8)
              && isr_get_stack(&r.player.inv, 0).count == 1,
          "empty flower pot refuses a non-pottable block");

    {
        int exact = 1;
        gm_runtime_set_entity_id_cursor(&r, 8000);
        for (int record = 2256; record <= 2267; ++record) {
            GmRuntimeStaticContainer jukebox;
            GmRuntimeWorldEvent world_event;
            GmRuntimeSoundEvent sound_event;
            int before_entities = r.entities.n_active;
            uint64_t before_world_seq = r.world_event_next_seq;
            uint64_t before_sound_seq = r.sound_event_next_seq;
            stage_control(&r, y, 0);
            if (!gm_runtime_set_block(&r, 10, y, 8, 84, 0)
                    || !gm_runtime_static_container_set_slot(
                        &r, 0, 10, y, 8, 0, 0, 0, 0))
                exact = 0;
            isr_set_stack(&r.player.inv, 0, ic_mk(record, 1, 0));
            r.player.inv.current_item = 0;
            if (!gm_runtime_use_block(&r, 10, y, 8)
                    || gm_world_meta(r.world, 10, y, 8) != 1
                    || !gm_runtime_static_container_get(&r, 0, &jukebox)
                    || jukebox.slots[0].item != record
                    || jukebox.slots[0].count != 1
                    || isr_get_stack(&r.player.inv, 0).count != 0
                    || r.world_event_next_seq != before_world_seq + 1
                    || !gm_runtime_world_event_get(
                        &r, gm_runtime_world_event_count(&r) - 1,
                        &world_event)
                    || world_event.seq != before_world_seq
                    || world_event.id != 1010
                    || world_event.data != record
                    || world_event.x != 10 || world_event.y != y
                    || world_event.z != 8
                    || r.sound_event_next_seq != before_sound_seq + 1
                    || !gm_runtime_sound_event_get(
                        &r, gm_runtime_sound_event_count(&r) - 1,
                        &sound_event)
                    || sound_event.seq != before_sound_seq
                    || sound_event.sound
                        != GM_SOUND_RECORD_13 + record - 2256
                    || sound_event.category != GM_SOUND_CATEGORY_RECORDS
                    || sound_event.x != 10.0 || sound_event.y != (double)y
                    || sound_event.z != 8.0
                    || sound_event.volume != 4.0F
                    || sound_event.pitch != 1.0F)
                exact = 0;
            if (!gm_runtime_use_block(&r, 10, y, 8)
                    || gm_world_meta(r.world, 10, y, 8) != 0
                    || !gm_runtime_static_container_get(&r, 0, &jukebox)
                    || !isr_is_empty(&jukebox.slots[0])
                    || r.entities.n_active != before_entities + 1
                    || r.world_event_next_seq != before_world_seq + 2
                    || !gm_runtime_world_event_get(
                        &r, gm_runtime_world_event_count(&r) - 1,
                        &world_event)
                    || world_event.seq != before_world_seq + 1
                    || world_event.id != 1010 || world_event.data != 0
                    || r.sound_event_next_seq != before_sound_seq + 2
                    || !gm_runtime_sound_event_get(
                        &r, gm_runtime_sound_event_count(&r) - 1,
                        &sound_event)
                    || sound_event.seq != before_sound_seq + 1
                    || sound_event.sound != GM_SOUND_RECORD_STOP
                    || sound_event.category != GM_SOUND_CATEGORY_RECORDS
                    || sound_event.x != 10.0 || sound_event.y != (double)y
                    || sound_event.z != 8.0
                    || sound_event.volume != 4.0F
                    || sound_event.pitch != 1.0F)
                exact = 0;
            else {
                int found = 0;
                for (int j = 0; j < GM_LIVE_MAX; ++j)
                    if (r.entities.ents[j].active
                            && r.entities.ents[j].item == record)
                        found++;
                if (found != 1)
                    exact = 0;
            }
        }
        CHECK(exact,
              "all 12 records insert/eject with exact 1010 and audio events");
    }

    {
        static const int wooden_doors[] = {64, 193, 194, 195, 196, 197};
        int exact = 1;
        for (size_t i = 0; i < sizeof wooden_doors / sizeof wooden_doors[0];
                ++i) {
            int door = wooden_doors[i];
            stage_control(&r, y, 0);
            gm_world_set_block_meta(r.world, 10, y, 8, door, 1);
            gm_world_set_block_meta(r.world, 10, y + 1, 8, door, 8);
            if (!gm_runtime_use_block(&r, 10, y + 1, 8)
                    || gm_world_meta(r.world, 10, y, 8) != 5
                    || gm_world_meta(r.world, 10, y + 1, 8) != 8)
                exact = 0;
        }
        CHECK(exact,
              "all upper wooden-door uses toggle the paired lower half");
    }
    gm_world_set_block_meta(r.world, 10, y, 8, 71, 1);
    gm_world_set_block_meta(r.world, 10, y + 1, 8, 71, 8);
    CHECK(!gm_runtime_use_block(&r, 10, y + 1, 8)
              && gm_world_meta(r.world, 10, y, 8) == 1,
          "iron door refuses manual use");

    {
        static const int gates[] = {107, 183, 184, 185, 186, 187};
        int exact = 1;
        for (size_t i = 0; i < sizeof gates / sizeof gates[0]; ++i) {
            stage_control(&r, y, 0);
            gm_world_set_block_meta(r.world, 10, y, 8, gates[i], 0);
            if (!gm_runtime_use_block(&r, 10, y, 8)
                    || gm_world_meta(r.world, 10, y, 8) != 6
                    || !gm_runtime_use_block(&r, 10, y, 8)
                    || gm_world_meta(r.world, 10, y, 8) != 2)
                exact = 0;
        }
        CHECK(exact,
              "all opposite fence gates rotate open then close in place");
    }

    stage_control(&r, y, 0);
    gm_world_set_block_meta(r.world, 10, y, 8, 96, 0);
    CHECK(gm_runtime_use_block(&r, 10, y, 8)
              && gm_world_meta(r.world, 10, y, 8) == 4,
          "wooden trapdoor toggles open on manual use");
    stage_control(&r, y, 0);
    gm_world_set_block_meta(r.world, 10, y, 8, 167, 0);
    CHECK(!gm_runtime_use_block(&r, 10, y, 8)
              && gm_world_meta(r.world, 10, y, 8) == 0,
          "iron trapdoor refuses manual use");

    {
        static const int doors[] = {64, 71, 193, 194, 195, 196, 197};
        int exact = 1;
        for (size_t i = 0; i < sizeof doors / sizeof doors[0]; ++i) {
            stage_control(&r, y, 0);
            gm_world_set_block_meta(r.world, 10, y, 8, doors[i], 1);
            gm_world_set_block_meta(r.world, 10, y + 1, 8, doors[i], 8);
            if (!gm_runtime_set_block(&r, 9, y, 8, 152, 0)
                    || gm_world_meta(r.world, 10, y, 8) != 5
                    || gm_world_meta(r.world, 10, y + 1, 8) != 10
                    || !gm_runtime_set_block(&r, 9, y, 8, 0, 0)
                    || gm_world_meta(r.world, 10, y, 8) != 1
                    || gm_world_meta(r.world, 10, y + 1, 8) != 8)
                exact = 0;
        }
        CHECK(exact, "all door IDs open and close under redstone power");
    }

    {
        static const int gates[] = {107, 183, 184, 185, 186, 187};
        int exact = 1;
        for (size_t i = 0; i < sizeof gates / sizeof gates[0]; ++i) {
            stage_control(&r, y, 0);
            gm_world_set_block_meta(r.world, 10, y, 8, gates[i], 0);
            if (!gm_runtime_set_block(&r, 9, y, 8, 152, 0)
                    || gm_world_meta(r.world, 10, y, 8) != 12
                    || !gm_runtime_set_block(&r, 9, y, 8, 0, 0)
                    || gm_world_meta(r.world, 10, y, 8) != 0)
                exact = 0;
        }
        CHECK(exact,
              "all fence-gate IDs open and close under redstone power");
    }

    {
        static const int trapdoors[] = {96, 167};
        int exact = 1;
        for (size_t i = 0; i < sizeof trapdoors / sizeof trapdoors[0]; ++i) {
            stage_control(&r, y, 0);
            gm_world_set_block_meta(r.world, 10, y, 8, trapdoors[i], 0);
            if (!gm_runtime_set_block(&r, 9, y, 8, 152, 0)
                    || gm_world_meta(r.world, 10, y, 8) != 4
                    || !gm_runtime_set_block(&r, 9, y, 8, 0, 0)
                    || gm_world_meta(r.world, 10, y, 8) != 0)
                exact = 0;
        }
        CHECK(exact,
              "wood and iron trapdoors open and close under redstone power");
    }

    {
        GmRuntimeStaticContainer jukebox;
        uint64_t world_random_seed48;
        uint64_t math_random_seed48;
        int next_entity_id;
        int fixture_eid = 9000;
        stage_control(&r, y, 0);
        CHECK(gm_runtime_set_block(&r, 10, y, 8, 84, 0)
                  && gm_runtime_static_container_set_slot(
                      &r, 0, 10, y, 8, 0, 0, 0, 0),
              "full-pool jukebox refusal fixture loads");
        isr_set_stack(&r.player.inv, 0, ic_mk(2256, 1, 0));
        r.player.inv.current_item = 0;
        CHECK(gm_runtime_use_block(&r, 10, y, 8),
              "full-pool jukebox refusal fixture inserts record");
        while (r.entities.n_active < GM_LIVE_MAX)
            CHECK(gm_runtime_spawn_item_fixture(
                      &r, fixture_eid++, 0.5, 100.0, 0.5,
                      0.0, 0.0, 0.0, 1, 1, 0, 0, 10, 1),
                  "full-pool jukebox refusal fills item capacity");
        world_random_seed48 = r.world_random_seed48;
        math_random_seed48 = r.math_random_seed48;
        next_entity_id = r.next_entity_id;
        CHECK(!gm_runtime_use_block(&r, 10, y, 8)
                  && gm_world_meta(r.world, 10, y, 8) == 1
                  && gm_runtime_static_container_get(&r, 0, &jukebox)
                  && jukebox.slots[0].item == 2256
                  && r.world_random_seed48 == world_random_seed48
                  && r.math_random_seed48 == math_random_seed48
                  && r.next_entity_id == next_entity_id,
              "full item pool rejects jukebox ejection atomically");
    }

    gm_runtime_destroy(&r);
    if (fail) return 1;
    puts("redstone_use: PASS");
    return 0;
}
