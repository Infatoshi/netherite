#include "game/runtime.h"
#include "assets/item_name_manifest.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static int failed;
#define CHECK(C, M) do { if (!(C)) { \
    fprintf(stderr, "FAIL: %s\n", M); failed = 1; \
} } while (0)

int main(void) {
    GmConfig cfg;
    static GmRuntime r;
    GmRuntimeCommandBlock command;
    GmRuntimeComparator comparator;
    GmRuntimeScheduledTick pending;
    GmAction idle;
    char error[256];
    gm_config_defaults(&cfg);
    cfg.seed = 42;
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.view_distance = 2;
    cfg.mobs = 0;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    CHECK(gm_runtime_init(&r, &cfg, error, sizeof error),
          "command callback runtime initializes");
    if (failed) return 1;
    r.randtick_enabled = 0;
    gm_runtime_set_total_time(&r, 42);

    CHECK(gm_runtime_load_block(&r, 8, 200, 8, 137, 2)
              && gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8, "Searge", NULL, 5)
              && gm_runtime_command_block_set_execution_state(
                  &r, 0, 8, 200, 8, 0, 0, 0)
              && gm_runtime_schedule_tick(
                  &r, 8, 200, 8, 137, 43, 0, 1),
          "impulse command pending callback stages");
    gm_runtime_tick(&r, idle);
    CHECK(gm_runtime_command_block_get(&r, 0, &command)
              && command.success_count == 0
              && command.condition_met == 1,
          "first unconditional impulse callback primes condition without firing");
    CHECK(gm_runtime_command_block_set_execution_state(
              &r, 0, 8, 200, 8, 1, 0, 1)
              && gm_runtime_schedule_tick(
                  &r, 8, 200, 8, 137, 44, 0, 2),
          "armed impulse callback stages");
    gm_runtime_tick(&r, idle);
    CHECK(gm_runtime_command_block_get(&r, 0, &command)
              && command.success_count == 1
              && command.last_output_tag_id > 0
              && command.powered == 1
              && command.condition_met == 1,
          "armed impulse callback executes and retains tile flags");

    gm_runtime_destroy(&r);
    CHECK(gm_runtime_init(&r, &cfg, error, sizeof error),
          "command-chain runtime initializes");
    if (!failed) {
        r.randtick_enabled = 0;
        gm_runtime_set_total_time(&r, 42);
        CHECK(gm_runtime_load_block(&r, 8, 200, 8, 210, 5)
                  && gm_runtime_load_block(&r, 9, 200, 8, 211, 5)
                  && gm_runtime_command_block_set_state(
                      &r, 0, 8, 200, 8, "Searge", NULL, 0)
                  && gm_runtime_command_block_set_execution_state(
                      &r, 0, 8, 200, 8, 0, 1, 1)
                  && gm_runtime_command_block_set_state(
                      &r, 0, 9, 200, 8, "Searge", NULL, 0)
                  && gm_runtime_schedule_tick(
                      &r, 8, 200, 8, 210, 43, 0, 10),
              "automatic repeating-to-chain fixture stages");
        gm_runtime_tick(&r, idle);
        CHECK(gm_runtime_scheduled_tick_count(&r) == 2,
              "repeating callback schedules itself and its chain successor");
        CHECK(gm_runtime_scheduled_tick_get(&r, 0, &pending)
                  && pending.block == 210 && pending.time == 44
                  && gm_runtime_scheduled_tick_get(&r, 1, &pending)
                  && pending.block == 211 && pending.time == 44,
              "repeat and chain callbacks retain Java insertion order");
        gm_runtime_tick(&r, idle);
        CHECK(gm_runtime_command_block_get(&r, 0, &command)
                  && command.block == 210 && command.success_count == 1,
              "automatic repeating callback executes on its next edge");
        CHECK(gm_runtime_command_block_get(&r, 1, &command)
                  && command.block == 211 && command.success_count == 1
                  && command.automatic == 1,
              "propagated chain callback executes and retains automatic state");
        CHECK(gm_runtime_scheduled_tick_count(&r) == 1
                  && gm_runtime_scheduled_tick_get(&r, 0, &pending)
                  && pending.block == 210 && pending.time == 45,
              "only the repeating callback remains pending");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r, &cfg, error, sizeof error),
          "gamerule command runtime initializes");
    if (!failed) {
        static const struct {
            const char *command, *name, *value;
            size_t offset;
            int expected;
        } cases[] = {
            {"gamerule doDaylightCycle false", "doDaylightCycle", "false",
             offsetof(McGameRules, doDaylightCycle), 0},
            {"gamerule doMobSpawning false", "doMobSpawning", "false",
             offsetof(McGameRules, doMobSpawning), 0},
            {"gamerule doFireTick false", "doFireTick", "false",
             offsetof(McGameRules, doFireTick), 0},
            {"gamerule randomTickSpeed 17", "randomTickSpeed", "17",
             offsetof(McGameRules, randomTickSpeed), 17},
            {"gamerule mobGriefing false", "mobGriefing", "false",
             offsetof(McGameRules, mobGriefing), 0},
            {"gamerule keepInventory true", "keepInventory", "true",
             offsetof(McGameRules, keepInventory), 1},
            {"gamerule doTileDrops false", "doTileDrops", "false",
             offsetof(McGameRules, doTileDrops), 0},
            {"gamerule naturalRegeneration false", "naturalRegeneration", "false",
             offsetof(McGameRules, naturalRegeneration), 0},
            {"gamerule doWeatherCycle false", "doWeatherCycle", "false",
             offsetof(McGameRules, doWeatherCycle), 0},
            {"gamerule maxEntityCramming -4", "maxEntityCramming", "-4",
             offsetof(McGameRules, maxEntityCramming), -4},
        };
        CHECK(gm_runtime_load_block(&r, 8, 200, 8, 137, 2),
              "gamerule command tile loads");
        for (size_t index = 0; index < sizeof cases / sizeof cases[0]; ++index) {
            const unsigned char *output = NULL;
            size_t output_length = 0;
            char expected_output[192];
            CHECK(gm_runtime_command_block_set_state(
                      &r, 0, 8, 200, 8, cases[index].command, NULL, 0)
                      && gm_runtime_command_block_trigger_at_clock(
                          &r, 0, 8, 200, 8, 1, 2, 3),
                  "represented gamerule setter executes");
            CHECK(*(const int *)((const unsigned char *)&r.gamerules
                      + cases[index].offset) == cases[index].expected,
                  "represented gamerule mutates its exact runtime field");
            CHECK(gm_runtime_command_block_get(&r, 0, &command)
                      && command.success_count == 1
                      && gm_runtime_armor_stand_string(
                          &r, command.last_output_tag_id,
                          &output, &output_length),
                  "gamerule command retains success and output");
            snprintf(expected_output, sizeof expected_output,
                "{\"extra\":[{\"translate\":\"commands.gamerule.success\","
                "\"with\":[\"%s\",\"%s\"]}],\"text\":\"[01:02:03] \"}",
                cases[index].name, cases[index].value);
            CHECK(output_length == strlen(expected_output)
                      && !memcmp(output, expected_output, output_length),
                  "gamerule command output matches Java translation JSON");
        }
        CHECK(r.clock.freeze_daylight && r.clock.freeze_weather,
              "cycle gamerules update the live clock immediately");
        CHECK(!gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8,
                  "gamerule doFireTick TRUE", NULL, 0)
                  && !gm_runtime_command_block_set_state(
                      &r, 0, 8, 200, 8,
                      "gamerule unknownRule true", NULL, 0),
              "invalid boolean spelling and unsupported rules fail closed");
        gm_world_clock_set_weather_full(
            &r.clock, 0, 0, 31, 47, 59, 1, 0.0f, 0.0f, 0.0f, 0.0f);
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8, "toggledownfall", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 3)
                  && r.clock.raining && !r.clock.thundering
                  && r.clock.rain_time == 31 && r.clock.thunder_time == 47
                  && r.clock.clean_weather_time == 59,
              "toggledownfall sets rain without changing weather timers");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8, "toggledownfall", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 4)
                  && !r.clock.raining && !r.clock.thundering,
              "toggledownfall clears an existing rainfall state");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8, "seed", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 5),
              "seed command executes");
        {
            GmRuntimeCommandBlock command;
            const unsigned char *output = NULL;
            size_t output_length = 0;
            char expected[192];
            snprintf(expected, sizeof expected,
                "{\"extra\":[{\"translate\":\"commands.seed.success\","
                "\"with\":[\"%lld\"]}],\"text\":\"[01:02:05] \"}",
                r.seed);
            CHECK(gm_runtime_command_block_get(&r, 0, &command)
                      && gm_runtime_armor_stand_string(
                          &r, command.last_output_tag_id,
                          &output, &output_length)
                      && output_length == strlen(expected)
                      && !memcmp(output, expected, output_length),
                  "seed command output matches Java translation JSON");
        }
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8,
                  "summon minecraft:lightning_bolt 14 78 8", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 36)
                  && r.lightning_count == 1
                  && r.lightning[0].active
                  && r.lightning[0].x == 14.5
                  && r.lightning[0].y == 78.0
                  && r.lightning[0].z == 8.5,
              "summon creates a live lightning weather entity");
        {
            const unsigned char *output = NULL;
            size_t output_length = 0;
            static const char expected[] =
                "{\"extra\":[{\"translate\":"
                "\"commands.summon.success\"}],"
                "\"text\":\"[01:02:36] \"}";
            CHECK(gm_runtime_command_block_get(&r, 0, &command)
                      && command.success_count == 1
                      && gm_runtime_armor_stand_string(
                          &r, command.last_output_tag_id,
                          &output, &output_length)
                      && output_length == sizeof expected - 1u
                      && !memcmp(output, expected, output_length),
                  "summon output matches Java translation JSON");
        }
        CHECK(gm_runtime_load_block(&r, 12, 78, 8, 149, 1)
                  && gm_runtime_comparator_set_output(
                      &r, 0, 12, 78, 8, 0)
                  && gm_runtime_command_block_set_state(
                      &r, 0, 8, 200, 8,
                      "blockdata 12 78 8 {OutputSignal:7}", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 37)
                  && gm_runtime_comparator_get(&r, 0, &comparator)
                  && comparator.output_signal == 7,
              "blockdata merges OutputSignal into the comparator tile NBT");
        {
            const unsigned char *output = NULL;
            size_t output_length = 0;
            static const char expected[] =
                "{\"extra\":[{\"translate\":"
                "\"commands.blockdata.success\",\"with\":["
                "\"{x:12,y:78,z:8,id:\\\"minecraft:comparator\\\","
                "OutputSignal:7}\"]}],"
                "\"text\":\"[01:02:37] \"}";
            CHECK(gm_runtime_command_block_get(&r, 0, &command)
                      && command.success_count == 1
                      && gm_runtime_armor_stand_string(
                          &r, command.last_output_tag_id,
                          &output, &output_length)
                      && output_length == sizeof expected - 1u
                      && !memcmp(output, expected, output_length),
                  "blockdata output matches Java's merged comparator NBT");
        }
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8, "spawnpoint @p 8 78 8", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 38)
                  && r.player_spawn_present && r.player_spawn_x == 8
                  && r.player_spawn_y == 78 && r.player_spawn_z == 8
                  && r.player_spawn_forced,
              "spawnpoint changes the persisted forced player spawn");
        {
            const unsigned char *output = NULL;
            size_t output_length = 0;
            char expected[224];
            snprintf(expected, sizeof expected,
                "{\"extra\":[{\"translate\":"
                "\"commands.spawnpoint.success\","
                "\"with\":[\"%s\",\"8\",\"78\",\"8\"]}],"
                "\"text\":\"[01:02:38] \"}", r.player_name);
            CHECK(gm_runtime_command_block_get(&r, 0, &command)
                      && command.success_count == 1
                      && gm_runtime_armor_stand_string(
                          &r, command.last_output_tag_id,
                          &output, &output_length)
                      && output_length == strlen(expected)
                      && !memcmp(output, expected, output_length),
                  "spawnpoint output matches Java translation JSON");
        }
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8, "setworldspawn 8 78 8", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 39)
                  && r.world_spawn_x == 8 && r.world_spawn_y == 78
                  && r.world_spawn_z == 8,
              "setworldspawn changes the persisted world spawn");
        {
            const unsigned char *output = NULL;
            size_t output_length = 0;
            static const char expected[] =
                "{\"extra\":[{\"translate\":"
                "\"commands.setworldspawn.success\","
                "\"with\":[\"8\",\"78\",\"8\"]}],"
                "\"text\":\"[01:02:39] \"}";
            CHECK(gm_runtime_command_block_get(&r, 0, &command)
                      && command.success_count == 1
                      && gm_runtime_armor_stand_string(
                          &r, command.last_output_tag_id,
                          &output, &output_length)
                      && output_length == sizeof expected - 1u
                      && !memcmp(output, expected, output_length),
                  "setworldspawn output matches Java translation JSON");
        }
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8, "spawnpoint @p ~-3 ~-80 ~5", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 40)
                  && r.player_spawn_x == 5 && r.player_spawn_y == 120
                  && r.player_spawn_z == 13,
              "spawnpoint accepts arbitrary relative block coordinates");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8, "setworldspawn -123 64 456", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 41)
                  && r.world_spawn_x == -123 && r.world_spawn_y == 64
                  && r.world_spawn_z == 456,
              "setworldspawn accepts arbitrary absolute block coordinates");
        CHECK(!gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8, "spawnpoint @p 0 256 0", NULL, 0)
                  && !gm_runtime_command_block_set_state(
                      &r, 0, 8, 200, 8, "setworldspawn 0 -1 0", NULL, 0),
              "spawn commands reject out-of-world Y atomically");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8,
                  "setblock ~3 ~-1 ~ minecraft:gold_block 0 replace",
                  NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 6)
                  && gm_world_block(r.world, 11, 199, 8) == 41
                  && gm_world_meta(r.world, 11, 199, 8) == 0,
              "setblock resolves command-sender-relative coordinates");
        {
            const unsigned char *output = NULL;
            size_t output_length = 0;
            static const char expected[] =
                "{\"extra\":[{\"translate\":"
                "\"commands.setblock.success\"}],"
                "\"text\":\"[01:02:06] \"}";
            CHECK(gm_runtime_command_block_get(&r, 0, &command)
                      && command.success_count == 1
                      && gm_runtime_armor_stand_string(
                          &r, command.last_output_tag_id,
                          &output, &output_length)
                      && output_length == sizeof expected - 1u
                      && !memcmp(output, expected, output_length),
                  "setblock output matches Java translation JSON");
        }
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8,
                  "setblock 12 199 8 minecraft:flowing_water 7",
                  NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 7)
                  && gm_world_block(r.world, 12, 199, 8) == 8
                  && gm_world_meta(r.world, 12, 199, 8) == 7,
              "setblock maps non-item registry blocks and legacy metadata");
        CHECK(!gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8,
                  "setblock 12 199 8 minecraft:apple 0", NULL, 0)
                  && !gm_runtime_command_block_set_state(
                      &r, 0, 8, 200, 8,
                      "setblock 12 199 8 minecraft:not_a_block 0", NULL, 0)
                  && !gm_runtime_command_block_set_state(
                      &r, 0, 8, 200, 8,
                      "setblock 12 199 8 minecraft:gold_block 16", NULL, 0),
              "setblock unknown names, items, and metadata fail closed");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8,
                  "setblock 12 199 8 minecraft:structure_block 0", NULL, 0),
              "setblock accepts the registered id-255 structure block");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8,
                  "testforblock ~3 ~-1 ~ minecraft:gold_block 0",
                  NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 8),
              "testforblock matches an exact relative block state");
        {
            const unsigned char *output = NULL;
            size_t output_length = 0;
            static const char expected[] =
                "{\"extra\":[{\"translate\":"
                "\"commands.testforblock.success\","
                "\"with\":[\"11\",\"199\",\"8\"]}],"
                "\"text\":\"[01:02:08] \"}";
            CHECK(gm_runtime_command_block_get(&r, 0, &command)
                      && command.success_count == 1
                      && gm_runtime_armor_stand_string(
                          &r, command.last_output_tag_id,
                          &output, &output_length)
                      && output_length == sizeof expected - 1u
                      && !memcmp(output, expected, output_length),
                  "testforblock output matches Java translation JSON");
        }
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8,
                  "testforblock 11 199 8 minecraft:gold_block *",
                  NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 9),
              "testforblock wildcard metadata matches the registered block");
        CHECK(gm_runtime_load_block(&r, 20, 199, 8, 1, 3)
                  && gm_runtime_load_block(&r, 21, 199, 8, 1, 3)
                  && gm_runtime_command_block_set_state(
                      &r, 0, 8, 200, 8,
                      "testforblocks 20 199 8 20 199 8 21 199 8 all",
                      NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 10),
              "testforblocks compares exact raw block state");
        {
            const unsigned char *output = NULL;
            size_t output_length = 0;
            static const char expected[] =
                "{\"extra\":[{\"translate\":"
                "\"commands.compare.success\",\"with\":[\"1\"]}],"
                "\"text\":\"[01:02:10] \"}";
            CHECK(gm_runtime_command_block_get(&r, 0, &command)
                      && command.success_count == 1
                      && gm_runtime_armor_stand_string(
                          &r, command.last_output_tag_id,
                          &output, &output_length)
                      && output_length == sizeof expected - 1u
                      && !memcmp(output, expected, output_length),
                  "testforblocks output matches Java translation JSON");
        }
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8,
                  "fill 22 199 8 23 199 8 minecraft:gold_block 0 replace",
                  NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 11)
                  && gm_world_block(r.world, 22, 199, 8) == 41
                  && gm_world_block(r.world, 23, 199, 8) == 41,
              "fill replace mutates every declared cell");
        {
            const unsigned char *output = NULL;
            size_t output_length = 0;
            static const char expected[] =
                "{\"extra\":[{\"translate\":"
                "\"commands.fill.success\",\"with\":[\"2\"]}],"
                "\"text\":\"[01:02:11] \"}";
            CHECK(gm_runtime_command_block_get(&r, 0, &command)
                      && command.success_count == 1
                      && gm_runtime_armor_stand_string(
                          &r, command.last_output_tag_id,
                          &output, &output_length)
                      && output_length == sizeof expected - 1u
                      && !memcmp(output, expected, output_length),
                  "fill output matches Java translation JSON");
        }
        CHECK(gm_runtime_load_block(&r, 24, 199, 8, 1, 3)
                  && gm_runtime_command_block_set_state(
                      &r, 0, 8, 200, 8,
                      "clone 24 199 8 24 199 8 25 199 8 replace",
                      NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 12)
                  && gm_world_block(r.world, 25, 199, 8) == 1
                  && gm_world_meta(r.world, 25, 199, 8) == 3,
              "clone replace copies exact non-tile raw block state");
        {
            const unsigned char *output = NULL;
            size_t output_length = 0;
            static const char expected[] =
                "{\"extra\":[{\"translate\":"
                "\"commands.clone.success\",\"with\":[\"1\"]}],"
                "\"text\":\"[01:02:12] \"}";
            CHECK(gm_runtime_command_block_get(&r, 0, &command)
                      && command.success_count == 1
                      && gm_runtime_armor_stand_string(
                          &r, command.last_output_tag_id,
                          &output, &output_length)
                      && output_length == sizeof expected - 1u
                      && !memcmp(output, expected, output_length),
                  "clone output matches Java translation JSON");
        }
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8, "say bounded literal", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 13)
                  && gm_runtime_command_block_get(&r, 0, &command)
                  && command.success_count == 1
                  && command.last_output_tag_id == 0,
              "say literal succeeds without command-block feedback output");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8, "me bounded literal", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 14)
                  && gm_runtime_command_block_get(&r, 0, &command)
                  && command.success_count == 1
                  && command.last_output_tag_id == 0
                  && !gm_runtime_command_block_set_state(
                      &r, 0, 8, 200, 8, "say @a", NULL, 0),
              "me literal succeeds and selector chat fails closed");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8,
                  "particle smoke ~ ~ ~ 0 0 0 0 1 normal", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 15),
              "bounded smoke particle command succeeds");
        {
            const unsigned char *output = NULL;
            size_t output_length = 0;
            static const char expected[] =
                "{\"extra\":[{\"translate\":"
                "\"commands.particle.success\","
                "\"with\":[\"smoke\",\"1\"]}],"
                "\"text\":\"[01:02:15] \"}";
            CHECK(gm_runtime_command_block_get(&r, 0, &command)
                      && command.success_count == 1
                      && gm_runtime_armor_stand_string(
                          &r, command.last_output_tag_id,
                          &output, &output_length)
                      && output_length == sizeof expected - 1u
                      && !memcmp(output, expected, output_length),
                  "particle output matches Java translation JSON");
        }
        CHECK(gm_runtime_player_name_restore(&r, "PoolPlayer0")
                  && gm_runtime_set_player_xp(&r, 0, 0.0F, 0)
                  && gm_runtime_command_block_set_state(
                      &r, 0, 8, 200, 8, "xp 10 @p", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 16)
                  && r.player_xp_level == 1
                  && fabsf(r.player_xp_frac - 0.333333373F) < 1e-8F
                  && r.player_xp_total == 10,
              "xp points applies Java's iterative level-bar arithmetic");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8, "xp 2L @p", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 17)
                  && r.player_xp_level == 3
                  && fabsf(r.player_xp_frac - 0.333333373F) < 1e-8F
                  && r.player_xp_total == 10,
              "xp levels preserves fractional progress and total points");
        {
            const unsigned char *output = NULL;
            size_t output_length = 0;
            static const char expected[] =
                "{\"extra\":[{\"translate\":"
                "\"commands.xp.success.levels\","
                "\"with\":[\"2\",\"PoolPlayer0\"]}],"
                "\"text\":\"[01:02:17] \"}";
            CHECK(gm_runtime_command_block_get(&r, 0, &command)
                      && command.success_count == 1
                      && gm_runtime_armor_stand_string(
                          &r, command.last_output_tag_id,
                          &output, &output_length)
                      && output_length == sizeof expected - 1u
                      && !memcmp(output, expected, output_length),
                  "xp target output uses the captured player identity");
        }
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8, "xp -2L @p", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 18)
                  && r.player_xp_level == 1
                  && fabsf(r.player_xp_frac - 0.333333373F) < 1e-8F
                  && r.player_xp_total == 10,
              "negative xp levels preserve fractional progress and total points");
        {
            const unsigned char *output = NULL;
            size_t output_length = 0;
            static const char expected[] =
                "{\"extra\":[{\"translate\":"
                "\"commands.xp.success.negative.levels\","
                "\"with\":[\"2\",\"PoolPlayer0\"]}],"
                "\"text\":\"[01:02:18] \"}";
            CHECK(gm_runtime_command_block_get(&r, 0, &command)
                      && command.success_count == 1
                      && gm_runtime_armor_stand_string(
                          &r, command.last_output_tag_id,
                          &output, &output_length)
                      && output_length == sizeof expected - 1u
                      && !memcmp(output, expected, output_length),
                  "negative xp level output matches Java translation JSON");
        }
        CHECK(!gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8, "xp -1 @p", NULL, 0),
              "negative xp points fail closed like Java withdrawal rejection");
        CHECK(gm_runtime_set_inventory(&r, 0, 1, 3, 0)
                  && gm_runtime_command_block_set_state(
                      &r, 0, 8, 200, 8,
                      "clear @p minecraft:stone -1 -1", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 18)
                  && isr_get_stack(&r.player.inv, 0).count == 0,
              "clear removes every matching item from the singleton player");
        {
            const unsigned char *output = NULL;
            size_t output_length = 0;
            static const char expected[] =
                "{\"extra\":[{\"translate\":"
                "\"commands.clear.success\","
                "\"with\":[\"PoolPlayer0\",\"3\"]}],"
                "\"text\":\"[01:02:18] \"}";
            CHECK(gm_runtime_command_block_get(&r, 0, &command)
                      && command.success_count == 1
                      && gm_runtime_armor_stand_string(
                          &r, command.last_output_tag_id,
                          &output, &output_length)
                      && output_length == sizeof expected - 1u
                      && !memcmp(output, expected, output_length),
                  "clear output uses exact captured player identity/count");
        }
        {
            char text[128];
            int registry_ok = 1;
            for (size_t index = 0;
                    index < sizeof gm_item_name_ids / sizeof gm_item_name_ids[0]
                    && registry_ok; ++index) {
                int item = gm_item_name_ids[index].id;
                if (item == 0) continue;
                snprintf(text, sizeof text, "clear @p %s",
                    gm_item_name_ids[index].name);
                registry_ok = gm_runtime_set_inventory(&r, 0, item, 2, 7)
                    && gm_runtime_command_block_set_state(
                        &r, 0, 8, 200, 8, text, NULL, 0)
                    && gm_runtime_command_block_trigger_at_clock(
                        &r, 0, 8, 200, 8, 1, 2, 18)
                    && isr_get_stack(&r.player.inv, 0).item == 0;
            }
            CHECK(registry_ok,
                  "clear executes every non-air 1.11.2 registry item");
            CHECK(gm_runtime_set_inventory(&r, 0, 368, 5, 4)
                      && gm_runtime_command_block_set_state(
                          &r, 0, 8, 200, 8,
                          "clear @p minecraft:ender_pearl 4 2", NULL, 0)
                      && gm_runtime_command_block_trigger_at_clock(
                          &r, 0, 8, 200, 8, 1, 2, 18)
                      && isr_get_stack(&r.player.inv, 0).count == 3,
                  "clear applies exact metadata and maximum-removal bounds");
            CHECK(gm_runtime_set_inventory(&r, 0, 368, 5, 4)
                      && gm_runtime_command_block_set_state(
                          &r, 0, 8, 200, 8,
                          "clear @p minecraft:ender_pearl 4 0", NULL, 0)
                      && gm_runtime_command_block_trigger_at_clock(
                          &r, 0, 8, 200, 8, 1, 2, 18)
                      && isr_get_stack(&r.player.inv, 0).count == 5,
                  "clear zero-count test reports matches without mutation");
            CHECK(gm_runtime_set_inventory(&r, 0, 368, 5, 4)
                      && gm_runtime_set_inventory(&r, 1, 1, 3, 0)
                      && gm_runtime_command_block_set_state(
                          &r, 0, 8, 200, 8, "clear @p", NULL, 0)
                      && gm_runtime_command_block_trigger_at_clock(
                          &r, 0, 8, 200, 8, 1, 2, 18)
                      && isr_get_stack(&r.player.inv, 0).item == 0
                      && isr_get_stack(&r.player.inv, 1).item == 0,
                  "clear without an item removes the entire player inventory");
        }
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8,
                  "gamemode survival @p", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 19),
              "gamemode survival resolves the singleton player");
        CHECK(gm_runtime_command_block_get(&r, 0, &command)
                  && command.success_count == 1
                  && command.last_output_tag_id == 0,
              "gamemode command preserves Java's empty block feedback");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8,
                  "gamemode creative @p", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 19)
                  && r.tape_creative == 1,
              "gamemode creative updates the persisted player mode");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8,
                  "gamemode survival @p", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 19)
                  && r.tape_creative == 0,
              "gamemode survival clears the persisted creative mode");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8,
                  "gamemode adventure @p", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 19)
                  && r.tape_game_mode == 2 && r.tape_creative == 0,
              "gamemode adventure updates the persisted player mode");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8,
                  "gamemode spectator @p", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 19)
                  && r.tape_game_mode == 3 && r.tape_creative == 0,
              "gamemode spectator updates the persisted player mode");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8, "testfor @p", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 20),
              "testfor resolves the singleton player");
        {
            const unsigned char *output = NULL;
            size_t output_length = 0;
            static const char expected[] =
                "{\"extra\":[{\"translate\":"
                "\"commands.testfor.success\","
                "\"with\":[\"PoolPlayer0\"]}],"
                "\"text\":\"[01:02:20] \"}";
            CHECK(gm_runtime_command_block_get(&r, 0, &command)
                      && command.success_count == 1
                      && gm_runtime_armor_stand_string(
                          &r, command.last_output_tag_id,
                          &output, &output_length)
                      && output_length == sizeof expected - 1u
                      && !memcmp(output, expected, output_length),
                  "testfor output uses exact captured player identity");
        }
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8, "title @p clear", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 21),
              "title clear resolves the singleton player");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8, "stopsound @p", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 22),
              "stopsound resolves the singleton player");
        {
            const unsigned char *output = NULL;
            size_t output_length = 0;
            static const char expected[] =
                "{\"extra\":[{\"translate\":"
                "\"commands.stopsound.success.all\","
                "\"with\":[\"PoolPlayer0\"]}],"
                "\"text\":\"[01:02:22] \"}";
            CHECK(gm_runtime_command_block_get(&r, 0, &command)
                      && command.success_count == 1
                      && gm_runtime_armor_stand_string(
                          &r, command.last_output_tag_id,
                          &output, &output_length)
                      && output_length == sizeof expected - 1u
                      && !memcmp(output, expected, output_length),
                  "stopsound output uses exact captured player identity");
        }
        {
            static const char *const title_commands[] = {
                "title @p clear", "title @p reset",
                "title @p times 10 70 20", "title @p times -1 -2 -3",
                "title @p title {\"text\":\"bounded\"}",
                "title @p subtitle {\"text\":\"bounded\"}",
                "title @p actionbar {\"text\":\"bounded\"}"
            };
            for (size_t index = 0;
                    index < sizeof title_commands / sizeof title_commands[0];
                    ++index)
                CHECK(gm_runtime_command_block_set_state(
                          &r, 0, 8, 200, 8, title_commands[index], NULL, 0)
                          && gm_runtime_command_block_trigger_at_clock(
                              &r, 0, 8, 200, 8, 1, 2, 22),
                      "all bounded title actions execute");
        }
        {
            static const char *const categories[] = {
                "master", "music", "record", "weather", "block",
                "hostile", "neutral", "player", "ambient", "voice"
            };
            char text[192];
            for (size_t index = 0;
                    index < sizeof categories / sizeof categories[0];
                    ++index) {
                snprintf(text, sizeof text, "stopsound @p %s",
                    categories[index]);
                CHECK(gm_runtime_command_block_set_state(
                          &r, 0, 8, 200, 8, text, NULL, 0)
                          && gm_runtime_command_block_trigger_at_clock(
                              &r, 0, 8, 200, 8, 1, 2, 22),
                      "every 1.11.2 sound category executes");
                snprintf(text, sizeof text,
                    "stopsound @p %s minecraft:block.note.harp",
                    categories[index]);
                CHECK(gm_runtime_command_block_set_state(
                          &r, 0, 8, 200, 8, text, NULL, 0)
                          && gm_runtime_command_block_trigger_at_clock(
                              &r, 0, 8, 200, 8, 1, 2, 22),
                      "every sound category accepts an individual sound");
            }
        }
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8,
                  "effect @p minecraft:speed 10 0 true", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 23)
                  && r.potion_count == 1
                  && r.potions[0].id == 1
                  && r.potions[0].amplifier == 0
                  && r.potions[0].duration == 200
                  && r.potions[0].hide_particles,
              "effect applies hidden speed to the singleton player");
        {
            static const char *const names[28] = {
                NULL, "speed", "slowness", "haste", "mining_fatigue",
                "strength", "instant_health", "instant_damage",
                "jump_boost", "nausea", "regeneration", "resistance",
                "fire_resistance", "water_breathing", "invisibility",
                "blindness", "night_vision", "hunger", "weakness",
                "poison", "wither", "health_boost", "absorption",
                "saturation", "glowing", "levitation", "luck", "unluck"
            };
            char text[96];
            gm_runtime_potions_clear(&r);
            for (int id = 1; id <= 27; ++id) {
                int instant = id == 6 || id == 7 || id == 23;
                snprintf(text, sizeof text,
                    "effect @p minecraft:%s 2 3 true", names[id]);
                CHECK(gm_runtime_command_block_set_state(
                          &r, 0, 8, 200, 8, text, NULL, 0)
                      && gm_runtime_command_block_trigger_at_clock(
                          &r, 0, 8, 200, 8, 1, 3, id)
                      && r.potion_count == 1
                      && r.potions[0].id == id
                      && r.potions[0].amplifier == 3
                      && r.potions[0].duration == (instant ? 2 : 40)
                      && r.potions[0].hide_particles,
                      "effect name registry applies exact duration and flags");
                snprintf(text, sizeof text, "effect @p %d 0", id);
                CHECK(gm_runtime_command_block_set_state(
                          &r, 0, 8, 200, 8, text, NULL, 0)
                      && gm_runtime_command_block_trigger_at_clock(
                          &r, 0, 8, 200, 8, 1, 4, id)
                      && r.potion_count == 0,
                      "effect numeric registry removes one active identity");
            }
            CHECK(gm_runtime_command_block_set_state(
                      &r, 0, 8, 200, 8,
                      "effect @p speed", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 5, 1)
                  && r.potions[0].duration == 600
                  && !r.potions[0].hide_particles
                  && gm_runtime_command_block_set_state(
                      &r, 0, 8, 200, 8,
                      "effect @p luck 1", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 5, 2)
                  && r.potion_count == 2
                  && gm_runtime_command_block_set_state(
                      &r, 0, 8, 200, 8,
                      "effect @p clear", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 5, 3)
                  && r.potion_count == 0,
                  "effect defaults and clear-all match the full command family");
        }
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8,
                  "playsound minecraft:block.note.harp master @p", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 24),
              "playsound resolves the singleton player");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8,
                  "tellraw @p {\"text\":\"bounded\"}", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 25)
                  && gm_runtime_command_block_get(&r, 0, &command)
                  && command.success_count == 1
                  && command.last_output_tag_id == 0,
              "tellraw preserves Java's empty command-block feedback");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8, "tell @p bounded", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 26)
                  && gm_runtime_command_block_get(&r, 0, &command)
                  && command.success_count == 1
                  && command.last_output_tag_id != 0,
              "tell records the styled outgoing message component");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8, "locate Stronghold", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 27)
                  && gm_runtime_command_block_get(&r, 0, &command)
                  && command.success_count == 1
                  && command.last_output_tag_id != 0,
              "locate resolves the nearest stronghold from the sender");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8, "locate Village", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 27)
                  && gm_runtime_command_block_get(&r, 0, &command)
                  && command.success_count == 1
                  && command.last_output_tag_id != 0,
              "locate resolves the nearest village from the sender");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8, "locate Temple", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 27)
                  && gm_runtime_command_block_get(&r, 0, &command)
                  && command.success_count == 1
                  && command.last_output_tag_id != 0,
              "locate resolves the nearest temple from the sender");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8, "locate Mineshaft", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 27)
                  && gm_runtime_command_block_get(&r, 0, &command)
                  && command.success_count == 1
                  && command.last_output_tag_id != 0,
              "locate resolves the nearest mineshaft from the sender");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8, "locate Mansion", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 27)
                  && gm_runtime_command_block_get(&r, 0, &command)
                  && command.success_count == 1
                  && command.last_output_tag_id != 0,
              "locate resolves the nearest mansion from the sender");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8, "locate Monument", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 27)
                  && gm_runtime_command_block_get(&r, 0, &command)
                  && command.success_count == 1
                  && command.last_output_tag_id != 0,
              "locate resolves the nearest monument from the sender");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8, "difficulty peaceful", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 28)
                  && r.difficulty == 0,
              "difficulty peaceful updates saved global state");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8, "difficulty easy", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 28)
                  && r.difficulty == 1,
              "difficulty easy updates saved global state");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8, "difficulty normal", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 28)
                  && r.difficulty == 2,
              "difficulty normal updates saved global state");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8, "difficulty hard", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 28)
                  && r.difficulty == 3,
              "difficulty hard updates saved global state");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8, "difficulty h", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 28)
                  && r.difficulty == 3,
              "difficulty short alias updates saved global state");
        {
            McGameRules rules=mc_gamerules_default();
            for(int difficulty=0;difficulty<4;++difficulty){
                PvStats stats;pv_init(&stats);
                stats.foodLevel=10;stats.saturation=0.0F;
                stats.exhaustion=5.0F;
                pv_on_update_difficulty_gr(&stats,&rules,difficulty);
                CHECK(stats.foodLevel==(difficulty==0?10:9),
                      "difficulty gates exhaustion hunger loss");
            }
            {
                const struct {
                    int difficulty;float health;float expected;
                } cases[]={
                    {0,11.0F,10.0F},{0,10.0F,10.0F},
                    {1,11.0F,10.0F},{1,10.0F,10.0F},
                    {2,2.0F,1.0F},{2,1.0F,1.0F},
                    {3,1.0F,0.0F}
                };
                for(size_t i=0;i<sizeof cases/sizeof cases[0];++i){
                    PvStats stats;pv_init(&stats);
                    stats.foodLevel=0;stats.saturation=0.0F;
                    stats.foodTimer=79;stats.health=cases[i].health;
                    pv_on_update_difficulty_gr(
                        &stats,&rules,cases[i].difficulty);
                    CHECK(stats.health==cases[i].expected,
                          "difficulty selects exact starvation floor");
                }
            }
        }
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8,
                  "defaultgamemode survival", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 29),
              "defaultgamemode survival preserves the represented default");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8,
                  "defaultgamemode creative", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 29)
                  && r.default_game_mode == 1,
              "defaultgamemode creative updates saved global state");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8,
                  "defaultgamemode spectator", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 29)
                  && r.default_game_mode == 3,
              "defaultgamemode spectator updates saved global state");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8,
                  "defaultgamemode 1", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 29)
                  && r.default_game_mode == 1,
              "defaultgamemode numeric alias updates saved global state");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8, "worldborder get", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 30)
                  && gm_runtime_command_block_get(&r, 0, &command)
                  && command.success_count == 1
                  && command.last_output_tag_id != 0,
              "worldborder get reports the represented default diameter");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8, "worldborder set 1000", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 30)
                  && r.border_diameter == 1000.0
                  && r.border_target_diameter == 1000.0
                  && r.border_time_until_target == 0,
              "worldborder set updates stationary saved diameter");
        r.border_diameter = r.border_target_diameter = 60000000.0;
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8, "worldborder add -100", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 30)
                  && r.border_diameter == 59999900.0
                  && r.border_target_diameter == 59999900.0,
              "worldborder add updates stationary saved diameter");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8,
                  "worldborder center 12.5 -7.5", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 30)
                  && r.border_center_x == 12.5
                  && r.border_center_z == -7.5,
              "worldborder center updates saved center");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8,
                  "worldborder damage buffer 3", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 30)
                  && r.border_damage_buffer == 3.0,
              "worldborder damage buffer updates saved state");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8,
                  "worldborder damage amount 0.5", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 30)
                  && r.border_damage_amount == 0.5,
              "worldborder damage amount updates saved state");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8,
                  "worldborder warning time 20", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 30)
                  && r.border_warning_time == 20,
              "worldborder warning time updates saved state");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8,
                  "worldborder warning distance 7", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 30)
                  && r.border_warning_distance == 7,
              "worldborder warning distance updates saved state");
        r.border_diameter = 100.0;
        r.border_target_diameter = 200.0;
        r.border_time_until_target = 100;
        gm_runtime_tick(&r, (GmAction){.hotbar_sel = -1});
        CHECK(r.border_diameter == 150.0
                  && r.border_target_diameter == 200.0
                  && r.border_time_until_target == 50,
              "loaded timed worldborder advances one exact 50 ms quantum");
        gm_runtime_tick(&r, (GmAction){.hotbar_sel = -1});
        CHECK(r.border_diameter == 200.0
                  && r.border_time_until_target == 0,
              "loaded timed worldborder settles exactly at its target");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8, "worldborder set 400 2", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 30)
                  && r.border_diameter == 200.0
                  && r.border_target_diameter == 400.0
                  && r.border_time_until_target == 2000,
              "worldborder timed set retains the exact transition");
        gm_runtime_tick(&r, (GmAction){.hotbar_sel = -1});
        CHECK(fabs(r.border_diameter - 205.0) < 0.0001
                  && r.border_time_until_target == 1950,
              "worldborder timed command advances from its command start");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8, "worldborder add -100 1", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 30)
                  && fabs(r.border_diameter - 205.0) < 0.0001
                  && fabs(r.border_target_diameter - 105.0) < 0.0001
                  && r.border_time_until_target == 2950,
              "worldborder timed add extends the active transition duration");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8, "worldborder center ~2 ~-3", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 30)
                  && r.border_center_x == 10.5
                  && r.border_center_z == 5.5,
              "worldborder center resolves command-source-relative coordinates");
        CHECK(!gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8, "worldborder set 0", NULL, 0)
                  && !gm_runtime_command_block_set_state(
                      &r, 0, 8, 200, 8, "worldborder warning time -1", NULL, 0),
              "worldborder rejects Java-invalid numeric bounds");
        {
            int items_before = r.entities.n_active;
            CHECK(gm_runtime_load_block(&r, 14, 200, 8, 13, 0)
                      && gm_runtime_command_block_set_state(
                          &r, 0, 8, 200, 8,
                          "setblock 14 200 8 minecraft:gold_block 0 destroy",
                          NULL, 0)
                      && gm_runtime_command_block_trigger_at_clock(
                          &r, 0, 8, 200, 8, 1, 2, 30)
                      && gm_world_block(r.world, 14, 200, 8) == 41
                      && r.entities.n_active == items_before + 1,
                  "setblock destroy uses the ordinary solid drop table");
            items_before = r.entities.n_active;
            CHECK(gm_runtime_load_block(&r, 14, 200, 8, 78, 0)
                      && gm_runtime_command_block_set_state(
                          &r, 0, 8, 200, 8,
                          "setblock 14 200 8 minecraft:gold_block 0 destroy",
                          NULL, 0)
                      && gm_runtime_command_block_trigger_at_clock(
                          &r, 0, 8, 200, 8, 1, 2, 30)
                      && gm_world_block(r.world, 14, 200, 8) == 41
                      && r.entities.n_active == items_before + 2,
                  "setblock destroy applies snow's positive chance path");
        }
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8,
                  "give @p minecraft:stone 1 0", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 31)
                  && isr_get_stack(&r.player.inv, 0).item == 1
                  && isr_get_stack(&r.player.inv, 0).count == 1,
              "give inserts one stone into the singleton player inventory");
        CHECK(gm_runtime_set_inventory(&r, 0, 0, 0, 0),
              "give test inventory reset");
        {
            char text[128];
            int registry_ok = 1;
            for (size_t index = 0;
                    index < sizeof gm_item_name_ids / sizeof gm_item_name_ids[0]
                    && registry_ok; ++index) {
                int item = gm_item_name_ids[index].id;
                if (item == 0) continue;
                snprintf(text, sizeof text, "give @p %s",
                    gm_item_name_ids[index].name);
                registry_ok = gm_runtime_set_inventory(&r, 0, 0, 0, 0)
                    && gm_runtime_command_block_set_state(
                        &r, 0, 8, 200, 8, text, NULL, 0)
                    && gm_runtime_command_block_trigger_at_clock(
                        &r, 0, 8, 200, 8, 1, 2, 31)
                    && isr_get_stack(&r.player.inv, 0).item == item
                    && isr_get_stack(&r.player.inv, 0).count == 1
                    && isr_get_stack(&r.player.inv, 0).meta == 0;
            }
            CHECK(registry_ok,
                  "give executes every non-air 1.11.2 registry item");
            CHECK(gm_runtime_set_inventory(&r, 0, 0, 0, 0)
                      && gm_runtime_command_block_set_state(
                          &r, 0, 8, 200, 8,
                          "give @p minecraft:diamond_sword 3 17", NULL, 0)
                      && gm_runtime_command_block_trigger_at_clock(
                          &r, 0, 8, 200, 8, 1, 2, 31)
                      && isr_get_stack(&r.player.inv, 0).item == 276
                      && isr_get_stack(&r.player.inv, 0).count == 3
                      && isr_get_stack(&r.player.inv, 0).meta == 17
                      && isr_get_stack(&r.player.inv, 1).item == 0,
                  "give honors count, metadata, and Java synthetic overstack insertion");
            CHECK(!gm_runtime_command_block_set_state(
                      &r, 0, 8, 200, 8, "give @p minecraft:not_real", NULL, 0)
                      && !gm_runtime_command_block_set_state(
                          &r, 0, 8, 200, 8,
                          "give @p minecraft:stone 65", NULL, 0),
                  "give rejects unknown items and Java-invalid counts");
        }
        CHECK(gm_runtime_set_inventory(&r, 0, 0, 0, 0),
              "general give test inventory reset");
        CHECK(gm_runtime_set_inventory(&r, 0, 276, 1, 0)
                  && gm_runtime_command_block_set_state(
                      &r, 0, 8, 200, 8,
                      "enchant @p minecraft:sharpness 1", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 32)
                  && isr_get_stack(&r.player.inv, 0).n_enchants == 1
                  && isr_get_stack(&r.player.inv, 0).enchants[0].id == 16
                  && isr_get_stack(&r.player.inv, 0).enchants[0].level == 1,
              "enchant adds sharpness one to the held diamond sword");
        {
            static const int enchant_ids[30] = {
                0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 16, 17, 18, 19,
                20, 21, 22, 32, 33, 34, 35, 48, 49, 50, 51, 61, 62,
                70, 71
            };
            static const char *const enchant_names[30] = {
                "protection", "fire_protection", "feather_falling",
                "blast_protection", "projectile_protection", "respiration",
                "aqua_affinity", "thorns", "depth_strider", "frost_walker",
                "binding_curse", "sharpness", "smite", "bane_of_arthropods",
                "knockback", "fire_aspect", "looting", "sweeping",
                "efficiency", "silk_touch", "unbreaking", "fortune",
                "power", "punch", "flame", "infinity", "luck_of_the_sea",
                "lure", "mending", "vanishing_curse"
            };
            static const int enchant_items[30] = {
                311, 311, 313, 311, 311, 310, 310, 311, 313, 313, 310,
                276, 276, 276, 276, 276, 276, 276, 278, 278, 276, 278,
                261, 261, 261, 261, 346, 346, 276, 276
            };
            char text[96];
            int registry_ok = 1;
            for (int index = 0; index < 30 && registry_ok; ++index) {
                snprintf(text, sizeof text, "enchant @p minecraft:%s 1",
                    enchant_names[index]);
                registry_ok = gm_runtime_set_inventory(
                        &r, 0, enchant_items[index], 1, 0)
                    && gm_runtime_command_block_set_state(
                        &r, 0, 8, 200, 8, text, NULL, 0)
                    && gm_runtime_command_block_trigger_at_clock(
                        &r, 0, 8, 200, 8, 1, 2, 32)
                    && isr_get_stack(&r.player.inv, 0).n_enchants == 1
                    && isr_get_stack(&r.player.inv, 0).enchants[0].id
                        == enchant_ids[index]
                    && isr_get_stack(&r.player.inv, 0).enchants[0].level == 1;
                snprintf(text, sizeof text, "enchant @p %d",
                    enchant_ids[index]);
                registry_ok = registry_ok
                    && gm_runtime_set_inventory(
                        &r, 0, enchant_items[index], 1, 0)
                    && gm_runtime_command_block_set_state(
                        &r, 0, 8, 200, 8, text, NULL, 0)
                    && gm_runtime_command_block_trigger_at_clock(
                        &r, 0, 8, 200, 8, 1, 2, 32)
                    && isr_get_stack(&r.player.inv, 0).n_enchants == 1
                    && isr_get_stack(&r.player.inv, 0).enchants[0].id
                        == enchant_ids[index];
            }
            CHECK(registry_ok,
                  "enchant accepts every 1.11.2 registry name and numeric id");
            CHECK(!gm_runtime_command_block_set_state(
                      &r, 0, 8, 200, 8, "enchant @p sharpness 6", NULL, 0)
                      && !gm_runtime_command_block_set_state(
                          &r, 0, 8, 200, 8, "enchant @p not_real 1", NULL, 0),
                  "enchant rejects out-of-range levels and unknown identities");
            CHECK(gm_runtime_set_inventory(&r, 0, 276, 1, 0)
                      && gm_runtime_command_block_set_state(
                          &r, 0, 8, 200, 8, "enchant @p sharpness 1", NULL, 0)
                      && gm_runtime_command_block_trigger_at_clock(
                          &r, 0, 8, 200, 8, 1, 2, 32)
                      && gm_runtime_command_block_set_state(
                          &r, 0, 8, 200, 8, "enchant @p smite 1", NULL, 0)
                      && !gm_runtime_command_block_trigger_at_clock(
                          &r, 0, 8, 200, 8, 1, 2, 32),
                  "enchant rejects incompatible enchantment pairs");
        }
        CHECK(gm_runtime_set_inventory(&r, 0, 0, 0, 0),
              "enchant test inventory reset");
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8,
                  "replaceitem entity @p slot.hotbar.0 minecraft:stone 1 0",
                  NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 33)
                  && isr_get_stack(&r.player.inv, 0).item == 1
                  && isr_get_stack(&r.player.inv, 0).count == 1,
              "replaceitem sets singleton player hotbar slot zero");
        {
            char text[160];
            int registry_ok = 1;
            for (size_t index = 0;
                    index < sizeof gm_item_name_ids / sizeof gm_item_name_ids[0]
                    && registry_ok; ++index) {
                int item = gm_item_name_ids[index].id;
                if (item == 0) continue;
                snprintf(text, sizeof text,
                    "replaceitem entity @p slot.hotbar.0 %s",
                    gm_item_name_ids[index].name);
                {
                    int set_ok = gm_runtime_command_block_set_state(
                        &r, 0, 8, 200, 8, text, NULL, 0);
                    int trigger_ok = set_ok
                        && gm_runtime_command_block_trigger_at_clock(
                            &r, 0, 8, 200, 8, 1, 2, 33);
                    ICStack got = isr_get_stack(&r.player.inv, 0);
                    registry_ok = trigger_ok && got.item == item
                        && got.count == 1;
                    if (!registry_ok)
                        fprintf(stderr, "replaceitem registry failure: %s "
                            "id=%d set=%d trigger=%d got=%d/%d\n",
                            gm_item_name_ids[index].name, item, set_ok,
                            trigger_ok, got.item, got.count);
                }
            }
            CHECK(registry_ok,
                  "replaceitem executes every non-air 1.11.2 registry item");
            CHECK(gm_runtime_command_block_set_state(
                      &r, 0, 8, 200, 8,
                      "replaceitem entity @p slot.hotbar.8 minecraft:apple 7 4",
                      NULL, 0)
                      && gm_runtime_command_block_trigger_at_clock(
                          &r, 0, 8, 200, 8, 1, 2, 33)
                      && isr_get_stack(&r.player.inv, 8).item == 260
                      && isr_get_stack(&r.player.inv, 8).count == 7
                      && isr_get_stack(&r.player.inv, 8).meta == 4
                      && gm_runtime_command_block_set_state(
                          &r, 0, 8, 200, 8,
                          "replaceitem entity @p slot.inventory.26 minecraft:stone",
                          NULL, 0)
                      && gm_runtime_command_block_trigger_at_clock(
                          &r, 0, 8, 200, 8, 1, 2, 33)
                      && isr_get_stack(&r.player.inv, 35).item == 1
                      && gm_runtime_command_block_set_state(
                          &r, 0, 8, 200, 8,
                          "replaceitem entity @p slot.armor.head minecraft:diamond_helmet",
                          NULL, 0)
                      && gm_runtime_command_block_trigger_at_clock(
                          &r, 0, 8, 200, 8, 1, 2, 33)
                      && isr_get_stack(&r.player.inv, ISR_ARMOR_HEAD).item == 310
                      && gm_runtime_command_block_set_state(
                          &r, 0, 8, 200, 8,
                          "replaceitem entity @p slot.weapon.offhand minecraft:shield",
                          NULL, 0)
                      && gm_runtime_command_block_trigger_at_clock(
                          &r, 0, 8, 200, 8, 1, 2, 33)
                      && isr_get_stack(&r.player.inv, ISR_OFFHAND_SLOT).item == 442,
                  "replaceitem maps hotbar, inventory, armor, and offhand slots");
            CHECK(!gm_runtime_command_block_set_state(
                      &r, 0, 8, 200, 8,
                      "replaceitem entity @p slot.hotbar.9 minecraft:stone",
                      NULL, 0),
                  "replaceitem rejects out-of-range player slots");
        }
        CHECK(gm_runtime_set_inventory(&r, 0, 0, 0, 0),
              "replaceitem test inventory reset");
        gm_runtime_set_pose(&r, 8.5, 78.0, 8.5, -180.0F, 0.0F);
        CHECK(gm_runtime_command_block_set_state(
                      &r, 0, 8, 200, 8, "tp @p 8.5 78 8.5", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 34)
                  && fabs(r.player.ent.posX - 8.5) < 1e-12
                  && fabs(r.player.ent.posY - 78.0) < 1e-12
                  && fabs(r.player.ent.posZ - 8.5) < 1e-12
                  && r.server_player.ent.onGround,
              "tp relocates and grounds the authoritative player");
        gm_runtime_set_pose(&r, 8.5, 78.0, 8.5, -180.0F, 0.0F);
        CHECK(gm_runtime_command_block_set_state(
                      &r, 0, 8, 200, 8,
                      "teleport @p 8.5 78 8.5", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 35)
                  && fabs(r.player.ent.posX - 8.5) < 1e-12,
              "teleport relocates the singleton player");
        CHECK(gm_runtime_command_block_set_state(
                      &r, 0, 8, 200, 8,
                      "tp @p 12 80 -4", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 36)
                  && fabs(r.server_player.ent.posX + r.ox - 12.5) < 1e-12
                  && fabs(r.server_player.ent.posY - 80.0) < 1e-12
                  && fabs(r.server_player.ent.posZ + r.oz + 3.5) < 1e-12
                  && fabs(r.player.ent.posX + r.ox - 8.5) < 1e-12,
              "tp centers integral absolute horizontal coordinates");
        gm_runtime_set_pose(&r, 8.5, 78.0, 8.5, -180.0F, 0.0F);
        CHECK(gm_runtime_command_block_set_state(
                      &r, 0, 8, 200, 8,
                      "tp @p ~1 ~2 ~-3 450 -100", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 37)
                  && fabs(r.server_player.ent.posX + r.ox - 9.5) < 1e-12
                  && fabs(r.server_player.ent.posY - 80.0) < 1e-12
                  && fabs(r.server_player.ent.posZ + r.oz - 5.5) < 1e-12
                  && r.server_player.yaw == 90.0F
                  && r.server_player.pitch == -100.0F,
              "tp resolves relative coordinates and wraps absolute rotation");
        CHECK(gm_runtime_command_block_set_state(
                      &r, 0, 8, 200, 8,
                      "teleport @p ~1 ~-120 ~2", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 38)
                  && fabs(r.server_player.ent.posX + r.ox - 9.5) < 1e-12
                  && fabs(r.server_player.ent.posY - 80.5) < 1e-12
                  && fabs(r.server_player.ent.posZ + r.oz - 10.5) < 1e-12,
              "teleport resolves relative coordinates from command source");
        CHECK(!gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8, "tp @p 0 5000 0", NULL, 0),
              "tp rejects Java-out-of-range vertical coordinates");
        gm_runtime_set_pose(&r, 8.5, 78.0, 8.5, -180.0F, 0.0F);
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8,
                  "execute @p ~ ~ ~ setblock ~6 ~ ~ "
                  "minecraft:gold_block 0 replace",
                  NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 40)
                  && gm_world_block(r.world, 14, 78, 8) == 41
                  && gm_world_meta(r.world, 14, 78, 8) == 0,
              "execute uses the player as nested setblock coordinate context");
        {
            const unsigned char *output = NULL;
            size_t output_length = 0;
            static const char expected[] =
                "{\"extra\":[{\"translate\":"
                "\"commands.setblock.success\"}],"
                "\"text\":\"[01:02:40] \"}";
            CHECK(gm_runtime_command_block_get(&r, 0, &command)
                      && command.success_count == 1
                      && gm_runtime_armor_stand_string(
                          &r, command.last_output_tag_id,
                          &output, &output_length)
                      && output_length == sizeof expected - 1u
                      && !memcmp(output, expected, output_length),
                  "execute forwards the nested setblock output exactly");
        }
        CHECK(gm_runtime_command_block_set_state(
                  &r, 0, 8, 200, 8, "kill @p", NULL, 0)
                  && gm_runtime_command_block_trigger_at_clock(
                      &r, 0, 8, 200, 8, 1, 2, 41)
                  && r.vitals.health == 0.0F
                  && r.mobs.player_hurt_time == 10
                  && r.mobs.player_hurt_resistant == 20
                  && !r.dead && !r.player_dying,
              "kill applies the measured late out-of-world damage boundary");
        {
            GmAction idle = {.hotbar_sel = -1};
            long long total_time = r.clock.total_time;
            GmPlayerView view;
            gm_runtime_tick(&r, idle);
            gm_runtime_view(&r, &view);
            CHECK(r.player_dying && !r.dead && r.player_death_time == 1
                      && view.dead && r.clock.total_time == total_time + 1,
                  "death opens the GUI while Entity.isDead stays false and world ticks");
            for (int tick = 0; tick < 18; ++tick)
                gm_runtime_tick(&r, idle);
            CHECK(!r.dead && r.player_death_time == 19,
                  "Entity.isDead remains false through deathTime nineteen");
            gm_runtime_tick(&r, idle);
            CHECK(r.dead && r.player_death_time == 20
                      && r.clock.total_time == total_time + 20,
                  "deathTime twenty retires the entity without freezing the world");
        }
    }
    gm_runtime_destroy(&r);
    if (failed) return 1;
    fprintf(stderr, "command_block_tick: PASS\n");
    return 0;
}
