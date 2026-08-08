#include "game/runtime.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s\n", message); \
        ++failures; \
    } \
} while (0)

static void set_window(Chunk *window, int x, int y, int z, int id)
{
    int lx, lz;
    int index = psv_chunk_index(x, z, &lx, &lz);
    if (index >= 0) mc_set(&window[index], lx, y, lz, mc_state(id, 0));
}

static void init_player(PsvPlayer *player, double x, double y, double z)
{
    psv_player_init(player);
    player->ent.posX = x;
    player->ent.posY = y;
    player->ent.posZ = z;
    player->ent.box = psv_player_box(x, y, z);
    player->ent.motionX = player->ent.motionY = player->ent.motionZ = 0.0;
    player->ent.onGround = 1;
}

static int enchanted_break_tick(Chunk *window, const McSinTable *sin_table,
        int aqua, int efficiency)
{
    PsvPlayer player;
    PvStats vitals;
    GmAction action;
    int result = -1;
    init_player(&player, 24.5, 65.0, 24.5);
    player.pitch = 89.0F;
    ICStack pick = ic_mk(257, 1, 0);
    if (efficiency > 0) {
        pick.n_enchants = 1;
        pick.enchants[0].id = 32;
        pick.enchants[0].level = efficiency;
    }
    isr_set_stack(&player.inv, 0, pick);
    if (aqua) {
        ICStack helmet = ic_mk(310, 1, 0);
        helmet.n_enchants = 1;
        helmet.enchants[0].id = 6;
        helmet.enchants[0].level = 1;
        isr_set_stack(&player.inv, ISR_ARMOR_HEAD, helmet);
    }
    pv_init(&vitals);
    memset(&action, 0, sizeof action);
    action.attack = 1;
    action.hotbar_sel = -1;
    gm_player_dig_reset();
    for (int tick = 0; tick < 100 && result < 0; ++tick) {
        GmBlockEdit edits[8];
        int count = 0;
        gm_player_tick(
            (struct Chunk *)window,
            (const struct McSinTable *)sin_table,
            (struct PsvPlayer *)&player, (struct PvStats *)&vitals, action,
            0, 0, 0, edits, &count, 8);
        for (int i = 0; i < count; ++i)
            if (edits[i].id == 0 && edits[i].wy == 64)
                result = tick;
    }
    return result;
}

static void test_player_ctl_enchantments(void)
{
    Chunk *window = (Chunk *)calloc(PSV_NCHUNKS, sizeof *window);
    McSinTable sin_table;
    CHECK(window != NULL, "allocate enchantment player window");
    if (!window) return;
    mc_sin_table_init(&sin_table);

    for (int x = 0; x < PSV_DIM * 16; ++x)
        for (int z = 0; z < PSV_DIM * 16; ++z)
            for (int y = 0; y <= 64; ++y)
                set_window(window, x, y, z, BLK_STONE);
    set_window(window, 24, 66, 24, 9);
    set_window(window, 24, 67, 24, 9);
    int penalized = enchanted_break_tick(window, &sin_table, 0, 0);

    memset(window, 0, PSV_NCHUNKS * sizeof *window);
    for (int x = 0; x < PSV_DIM * 16; ++x)
        for (int z = 0; z < PSV_DIM * 16; ++z)
            for (int y = 0; y <= 64; ++y)
                set_window(window, x, y, z, BLK_STONE);
    set_window(window, 24, 66, 24, 9);
    set_window(window, 24, 67, 24, 9);
    int affinity = enchanted_break_tick(window, &sin_table, 1, 0);
    CHECK(penalized > 25, "underwater dig keeps vanilla penalty without Aqua Affinity");
    CHECK(affinity >= 0 && affinity < 12,
          "Aqua Affinity removes the underwater dig divisor in player_ctl");

    memset(window, 0, PSV_NCHUNKS * sizeof *window);
    for (int x = 0; x < PSV_DIM * 16; ++x)
        for (int z = 0; z < PSV_DIM * 16; ++z)
            for (int y = 0; y <= 64; ++y)
                set_window(window, x, y, z, BLK_STONE);
    int efficient = enchanted_break_tick(window, &sin_table, 0, 5);
    CHECK(efficient >= 0 && efficient < 3,
          "Efficiency V contributes level squared plus one to live dig speed");

    memset(window, 0, PSV_NCHUNKS * sizeof *window);
    for (int x = 3; x <= 13; ++x)
        for (int y = 219; y <= 222; ++y)
            for (int z = 3; z <= 13; ++z)
                set_window(window, x, y, z, 9);
    PsvPlayer actual, expected;
    PvStats vitals;
    GmAction action;
    PsvAction reference;
    McAABB blocks[PSV_MAX_BLOCKS];
    init_player(&actual, 8.5, 220.0, 8.5);
    expected = actual;
    ICStack boots = ic_mk(313, 1, 0);
    boots.n_enchants = 1;
    boots.enchants[0].id = 8;
    boots.enchants[0].level = 3;
    isr_set_stack(&actual.inv, ISR_ARMOR_FEET, boots);
    pv_init(&vitals);
    memset(&action, 0, sizeof action);
    action.forward = 1.0F;
    action.strafe = -0.25F;
    action.hotbar_sel = -1;
    actual.yaw = expected.yaw = 37.0F;
    memset(&reference, 0, sizeof reference);
    reference.forward = 1.0F;
    reference.strafe = 0.25F;
    reference.yaw = 37.0F;
    reference.depth_strider = 3;
    reference.water_ai_speed = 0.1F;
    gm_player_dig_reset();
    GmBlockEdit edits[2];
    int edit_count = 0;
    gm_player_tick(
        (struct Chunk *)window,
        (const struct McSinTable *)&sin_table,
        (struct PsvPlayer *)&actual, (struct PvStats *)&vitals, action,
        0, 0, 0, edits, &edit_count, 2);
    psv_physics_tick(window, &sin_table, &expected, &reference, blocks);
    CHECK(actual.ent.posX == expected.ent.posX
              && actual.ent.posY == expected.ent.posY
              && actual.ent.posZ == expected.ent.posZ
              && actual.ent.motionX == expected.ent.motionX
              && actual.ent.motionY == expected.ent.motionY
              && actual.ent.motionZ == expected.ent.motionZ,
          "player_ctl derives exact Depth Strider travel inputs from boots");
    free(window);
}

static int init_runtime(GmRuntime *runtime)
{
    GmConfig config;
    char error[256];
    gm_config_defaults(&config);
    config.seed = 42;
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.mobs = 0;
    config.weather = 0;
    if (gm_runtime_init(runtime, &config, error, sizeof error)) return 1;
    fprintf(stderr, "FAIL: runtime init: %s\n", error);
    ++failures;
    return 0;
}

static void test_runtime_respiration(void)
{
    GmRuntime runtime;
    GmAction idle;
    JavaRandom expected;
    if (!init_runtime(&runtime)) return;
    gm_runtime_set_pose_state(
        &runtime, 8.5, 10.0, 8.5, 0.0F, 0.0F,
        0.0, 0.0, 0.0, 1, 0.0F);
    gm_world_set_block_meta(runtime.world, 8, 9, 8, 1, 0);
    gm_world_set_block_meta(runtime.world, 8, 11, 8, 9, 0);
    ICStack helmet = ic_mk(310, 1, 0);
    helmet.n_enchants = 1;
    helmet.enchants[0].id = 5;
    helmet.enchants[0].level = 3;
    isr_set_stack(&runtime.player.inv, ISR_ARMOR_HEAD, helmet);
    gm_runtime_set_air(&runtime, 10);
    jrand_set_seed48(&runtime.mobs.player_random, 1);
    expected = runtime.mobs.player_random;
    int expected_air =
        jrand_int_bound(&expected, 4) > 0 ? 10 : 9;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    gm_runtime_tick(&runtime, idle);
    CHECK(runtime.player_air == expected_air,
          "live submerged tick applies exact Respiration keep/decrement result");
    CHECK(runtime.mobs.player_random.seed == expected.seed,
          "live Respiration consumes exactly one EntityPlayerMP RNG draw");
    gm_runtime_destroy(&runtime);
}

static void test_runtime_frost(void)
{
    GmRuntime runtime;
    GmAction idle;
    if (!init_runtime(&runtime)) return;
    gm_runtime_set_pose_state(
        &runtime, 8.5, 11.0, 8.5, 0.0F, 0.0F,
        0.0, 0.0, 0.0, 1, 0.0F);
    for (int z = 4; z <= 12; ++z)
        for (int x = 4; x <= 12; ++x) {
            gm_world_set_block_meta(runtime.world, x, 9, z, 1, 0);
            gm_world_set_block_meta(runtime.world, x, 10, z, 9, 0);
            gm_world_set_block_meta(runtime.world, x, 11, z, 0, 0);
        }
    ICStack boots = ic_mk(313, 1, 0);
    boots.n_enchants = 1;
    boots.enchants[0].id = 9;
    boots.enchants[0].level = 2;
    isr_set_stack(&runtime.player.inv, ISR_ARMOR_FEET, boots);
    jrand_set_seed48(&runtime.mobs.player_random, 0);
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    gm_runtime_tick(&runtime, idle);
    int frozen = 0;
    for (int z = 4; z <= 12; ++z)
        for (int x = 4; x <= 12; ++x)
            if (gm_world_block(runtime.world, x, 10, z) == 212)
                ++frozen;
    CHECK(frozen == 45,
          "authoritative first block-position tick freezes exact level-2 disk");
    CHECK(gm_runtime_scheduled_tick_count(&runtime) == 112,
          "Frost Walker keeps 67 water wakes plus 45 ice melt schedules");
    uint64_t seed_after = runtime.mobs.player_random.seed;
    int schedules_after = gm_runtime_scheduled_tick_count(&runtime);
    gm_runtime_tick(&runtime, idle);
    CHECK(runtime.mobs.player_random.seed == seed_after
              && gm_runtime_scheduled_tick_count(&runtime) == schedules_after,
          "unchanged authoritative BlockPos does not rerun Frost Walker");
    gm_runtime_destroy(&runtime);
}

static void test_runtime_thorns_melee(void)
{
    GmRuntime runtime;
    GmAction idle;
    JavaRandom expected;
    if (!init_runtime(&runtime)) return;
    gm_runtime_set_pose_state(
        &runtime, 8.5, 5.0, 8.5, 0.0F, 0.0F,
        0.0, 0.0, 0.0, 1, 0.0F);
    int slot = gm_mobs_spawn(
        &runtime.mobs, GM_MOB_ZOMBIE, 9.25, 5.0, 8.5);
    CHECK(slot > 0, "spawn live Thorns melee zombie");
    if (slot <= 0) {
        gm_runtime_destroy(&runtime);
        return;
    }
    runtime.mobs.persistence_required[slot] = 1;
    runtime.mobs.a.attack_time[slot] = 0;
    runtime.mobs.b.attack_time[slot] = 0;
    int eid = runtime.mobs.a.id[slot];
    ICStack boots = ic_mk(313, 1, 0);
    boots.n_enchants = 1;
    boots.enchants[0].id = 7;
    boots.enchants[0].level = 20;
    isr_set_stack(&runtime.player.inv, ISR_ARMOR_FEET, boots);
    jrand_set_seed48(&runtime.mobs.player_random, 0);
    expected = runtime.mobs.player_random;
    (void)jrand_int_bound(&expected, 1);
    (void)jrand_float(&expected);
    runtime.mobs_enabled = 1;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    gm_runtime_tick(&runtime, idle);

    ICStack after = isr_get_stack(
        &runtime.player.inv, ISR_ARMOR_FEET);
    CHECK(after.item == 313 && after.meta == 2,
          "accepted zombie hit damages armor, then live Thorns selection");
    CHECK(runtime.mobs.player_random.seed == expected.seed,
          "live melee consumes the exact Thorns player RNG sequence");
    EwStore *state = runtime.mobs.current
        ? &runtime.mobs.b : &runtime.mobs.a;
    CHECK(state->health[slot] < 20.0F,
          "live Thorns retaliation damages its melee attacker");
    int thorns_status = 0;
    for (int i = 0; i < gm_mobs_event_count(&runtime.mobs); ++i) {
        GmMobEvent event;
        if (gm_mobs_event_get(&runtime.mobs, i, &event)
                && event.kind == GM_MOB_EVENT_ENTITY_STATUS
                && event.eid == eid && event.data == 33)
            thorns_status = 1;
    }
    CHECK(thorns_status,
          "live retaliation emits the Thorns-specific entity status");
    gm_runtime_destroy(&runtime);
}

int main(void)
{
    test_player_ctl_enchantments();
    test_runtime_respiration();
    test_runtime_frost();
    test_runtime_thorns_melee();
    if (failures) {
        fprintf(stderr, "equipment enchantment live: %d failure(s)\n", failures);
        return 1;
    }
    puts("PASS equipment enchantment live: Aqua/Depth/Respiration/Frost/Thorns wiring");
    return 0;
}
