#include "game/runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "furnace output check failed at line %d: %s\n", \
            __LINE__, #condition); \
    exit(1); \
} } while (0)

static void run(
        const char *tag, int item, int count, int meta, int take,
        const char *stat_id, int achievement, int prerequisite) {
    GmConfig cfg;
    GmRuntime r;
    GmRuntimeSmeltEvent event;
    char error[256] = {0};
    const char *statistics = prerequisite
        ? "{\"achievement.buildFurnace\":1}" : "{}";
    int xp;
    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.view_distance = 1;
    cfg.mobs = 0;
    cfg.weather = 0;
    CHECK(gm_runtime_init(&r, &cfg, error, sizeof error));
    CHECK(gm_runtime_restore_player_statistics(
        &r, statistics, strlen(statistics), 0, 0));
    CHECK(gm_runtime_set_math_random_seed48(
        &r, UINT64_C(0x123456789abc)));
    xp = gm_runtime_furnace_output_taken(
        &r, ic_mk(item, count, meta), take);
    CHECK(gm_runtime_smelt_event_count(&r) == 1);
    CHECK(gm_runtime_smelt_event_get(&r, 0, &event));
    CHECK(event.stack.item == item && event.stack.count == take
        && event.stack.meta == meta && event.xp == xp
        && event.achievement == achievement
        && event.achievement_awarded
            == (achievement != GM_SMELT_ACHIEVEMENT_NONE && prerequisite));
    CHECK(item >= 0 && item < GM_RUNTIME_ITEM_STAT_LIMIT
        && r.stat_craft_item_present[item]
        && r.stat_craft_item[item] == take);

    printf("%s S %s %d\n", tag, stat_id, take);
    for (int index = 0; index < GM_XP_ORBS; ++index)
        if (!r.mobs.xp_orbs[index].dead
                && r.mobs.xp_orbs[index].xpValue > 0)
            printf("%s X %d\n", tag, r.mobs.xp_orbs[index].xpValue);
    printf("%s E %d %d %d\n", tag, item, take, meta);
    if (achievement == GM_SMELT_ACHIEVEMENT_ACQUIRE_IRON)
        printf("%s S achievement.acquireIron 1\n", tag);
    else if (achievement == GM_SMELT_ACHIEVEMENT_COOK_FISH)
        printf("%s S achievement.cookFish 1\n", tag);
    printf("R %s %d %d %d %012llx\n",
        tag, r.stat_craft_item[item],
        r.stat_achievement_acquire_iron,
        r.stat_achievement_cook_fish,
        (unsigned long long)r.math_random_seed48);
    gm_runtime_destroy(&r);
}

static char *read_file(const char *path, size_t *length) {
    FILE *stream = fopen(path, "rb");
    long end;
    char *bytes;
    CHECK(stream != NULL);
    CHECK(fseek(stream, 0, SEEK_END) == 0);
    end = ftell(stream);
    CHECK(end >= 0 && fseek(stream, 0, SEEK_SET) == 0);
    bytes = (char *)malloc((size_t)end + 1);
    CHECK(bytes != NULL);
    CHECK(fread(bytes, 1, (size_t)end, stream) == (size_t)end);
    CHECK(fclose(stream) == 0);
    bytes[end] = '\0';
    *length = (size_t)end;
    return bytes;
}

static void persistence(void) {
    static const char source[] =
        "{\n"
        " \"unknown\":{\"nested\":[1,{\"x\":\"a\\\\\\\"b\"}]},"
        " \"stat.craftItem.minecraft.iron_ingot\" : 7,"
        " \"achievement.buildFurnace\":1,"
        " \"achievement.acquireIron\":0,"
        " \"negative\":-2\n"
        "}\n";
    static const char expected[] =
        "{\n"
        " \"unknown\":{\"nested\":[1,{\"x\":\"a\\\\\\\"b\"}]},"
        " \"stat.craftItem.minecraft.iron_ingot\" : 10,"
        " \"achievement.buildFurnace\":1,"
        " \"achievement.acquireIron\":1,"
        " \"negative\":-2\n"
        ",\"stat.playOneMinute\":12"
        ",\"stat.timeSinceDeath\":34"
        ",\"achievement.cookFish\":1"
        ",\"stat.craftItem.minecraft.cooked_fish\":2}\n";
    const char *tmpdir = getenv("TMPDIR");
    char statistics_path[512], checkpoint_path[512];
    char error[256] = {0};
    char *written;
    size_t written_len;
    GmConfig cfg;
    GmRuntime r;
    GmRuntimeSmeltEvent event;
    if (!tmpdir || !*tmpdir) tmpdir = ".tmp";
    CHECK(snprintf(statistics_path, sizeof statistics_path,
        "%s/furnace-output-statistics.%ld.json", tmpdir, (long)getpid())
        < (int)sizeof statistics_path);
    CHECK(snprintf(checkpoint_path, sizeof checkpoint_path,
        "%s/furnace-output-checkpoint.%ld.bin", tmpdir, (long)getpid())
        < (int)sizeof checkpoint_path);
    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.view_distance = 1;
    cfg.mobs = 0;
    cfg.weather = 0;
    CHECK(gm_runtime_init(&r, &cfg, error, sizeof error));
    CHECK(gm_runtime_restore_player_statistics(
        &r, source, strlen(source), 12, 34));
    CHECK(gm_runtime_set_math_random_seed48(
        &r, UINT64_C(0x123456789abc)));
    CHECK(gm_runtime_furnace_output_taken(
        &r, ic_mk(265, 5, 0), 3) == 3);
    CHECK(gm_runtime_furnace_output_taken(
        &r, ic_mk(350, 4, 1), 2) == 1);
    CHECK(r.stat_craft_item[265] == 10
        && r.stat_craft_item[350] == 2
        && r.stat_achievement_acquire_iron == 1
        && r.stat_achievement_cook_fish == 1
        && gm_runtime_smelt_event_count(&r) == 2);
    CHECK(gm_runtime_write_player_statistics(&r, statistics_path));
    written = read_file(statistics_path, &written_len);
    CHECK(written_len == strlen(expected)
        && memcmp(written, expected, written_len) == 0);
    free(written);

    CHECK(gm_runtime_write_checkpoint(&r, checkpoint_path));
    r.stat_craft_item[265] = -100;
    r.stat_achievement_acquire_iron = -100;
    r.smelt_event_count = 0;
    CHECK(gm_runtime_load_checkpoint(&r, checkpoint_path));
    CHECK(r.stat_play_one_minute == 12
        && r.stat_time_since_death == 34
        && r.stat_craft_item_present[265]
        && r.stat_craft_item[265] == 10
        && r.stat_craft_item_present[350]
        && r.stat_craft_item[350] == 2
        && r.stat_achievement_build_furnace_present
        && r.stat_achievement_build_furnace == 1
        && r.stat_achievement_acquire_iron_present
        && r.stat_achievement_acquire_iron == 1
        && r.stat_achievement_cook_fish_present
        && r.stat_achievement_cook_fish == 1
        && gm_runtime_smelt_event_count(&r) == 2);
    CHECK(gm_runtime_smelt_event_get(&r, 0, &event)
        && event.stack.item == 265 && event.stack.count == 3
        && event.stack.meta == 0 && event.xp == 3
        && event.achievement == GM_SMELT_ACHIEVEMENT_ACQUIRE_IRON
        && event.achievement_awarded);
    CHECK(gm_runtime_smelt_event_get(&r, 1, &event)
        && event.stack.item == 350 && event.stack.count == 2
        && event.stack.meta == 1 && event.xp == 1
        && event.achievement == GM_SMELT_ACHIEVEMENT_COOK_FISH
        && event.achievement_awarded);
    for (int index = 2; index <= GM_RUNTIME_SMELT_EVENTS; ++index)
        CHECK(gm_runtime_furnace_output_taken(
            &r, ic_mk(3, 1, 0), 1) == 0);
    CHECK(r.smelt_event_count == GM_RUNTIME_SMELT_EVENTS + 1
        && r.smelt_events_cap > GM_RUNTIME_SMELT_EVENTS
        && r.smelt_event_dropped == 0
        && gm_runtime_smelt_event_get(
            &r, GM_RUNTIME_SMELT_EVENTS, &event)
        && event.seq == GM_RUNTIME_SMELT_EVENTS
        && event.stack.item == 3 && event.stack.count == 1);
    CHECK(gm_runtime_write_checkpoint(&r, checkpoint_path));
    CHECK(gm_runtime_load_checkpoint(&r, checkpoint_path)
        && r.smelt_event_count == GM_RUNTIME_SMELT_EVENTS + 1
        && r.smelt_events_cap > GM_RUNTIME_SMELT_EVENTS
        && r.smelt_event_dropped == 0);
    gm_runtime_destroy(&r);
    CHECK(remove(statistics_path) == 0);
    CHECK(remove(checkpoint_path) == 0);
}

int main(void) {
    run("I0", 265, 5, 0, 3,
        "stat.craftItem.minecraft.iron_ingot",
        GM_SMELT_ACHIEVEMENT_ACQUIRE_IRON, 0);
    run("I1", 265, 5, 0, 3,
        "stat.craftItem.minecraft.iron_ingot",
        GM_SMELT_ACHIEVEMENT_ACQUIRE_IRON, 1);
    run("F1", 350, 4, 1, 2,
        "stat.craftItem.minecraft.cooked_fish",
        GM_SMELT_ACHIEVEMENT_COOK_FISH, 1);
    run("S1", 1, 4, 0, 4,
        "stat.craftItem.minecraft.stone",
        GM_SMELT_ACHIEVEMENT_NONE, 1);
    persistence();
    return 0;
}
