#include "game/runtime.h"

#include <math.h>
#include <stdio.h>

static int fail;
#define CHECK(c, m) do { \
    if (!(c)) { fprintf(stderr, "FAIL: %s\n", m); fail = 1; } \
} while (0)

static const EwStore *store(const GmRuntime *r) {
    return r->mobs.current ? &r->mobs.b : &r->mobs.a;
}

static uint64_t mate_seed48(void) {
    for (long long seed = 0; seed < 1000000; ++seed) {
        JavaRandom random;
        jrand_set(&random, seed);
        uint64_t initial = random.seed;
        (void)jrand_int_bound(&random, 1000);
        if (jrand_int_bound(&random, 500) == 0)
            return initial;
    }
    return 0;
}

static uint64_t interact_seed48(void) {
    for (long long seed = 0; seed < 1000000; ++seed) {
        JavaRandom random;
        jrand_set(&random, seed);
        uint64_t initial = random.seed;
        (void)jrand_int_bound(&random, 1000);
        if (jrand_int_bound(&random, 500) == 0) continue;
        (void)jrand_float(&random); /* priority-9 player watch */
        if (jrand_float(&random) < 0.02F) return initial;
    }
    return 0;
}

static void test_cold_reload_entity_id_zero(void) {
    GmConfig config;
    GmRuntime runtime;
    char error[256] = {0};
    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.mobs = 1;
    CHECK(gm_runtime_init(&runtime, &config, error, sizeof error), error);
    if (fail) return;
    CHECK(gm_runtime_spawn_villager_fixture(
              &runtime, 0, 0.5, 64.0, 0.5,
              0.0, 0.0, 0.0, 0.0F, 20.0F,
              0, 0, 0, 1, 0, UINT64_C(0x123456789ABC), 0, 0.0),
          "cold Java entity id zero is a valid exact villager id");
    CHECK(gm_mobs_find_slot_by_eid(&runtime.mobs, 0) > 0
              && gm_mobs_type_by_eid(&runtime.mobs, 0) == GM_MOB_VILLAGER,
          "entity id zero remains addressable after exact spawn");
    CHECK(gm_runtime_set_mob_no_ai(&runtime, 0, 0),
          "entity id zero accepts the cold-reload AI activation");
    CHECK(gm_runtime_set_mob_uuid(
              &runtime, 0, INT64_C(-7), INT64_C(21249)),
          "cold villager UUID is retained after exact spawn");
    {
        int slot = gm_mobs_find_slot_by_eid(&runtime.mobs, 0);
        CHECK(slot > 0 && runtime.mobs.entity_uuid_present[slot]
                  && runtime.mobs.entity_uuid_most[slot] == INT64_C(-7)
                  && runtime.mobs.entity_uuid_least[slot] == INT64_C(21249),
              "cold villager UUID remains addressable by entity id zero");
    }
    CHECK(runtime.loaded_entity_order_count == 1
              && runtime.loaded_entity_order[0] == 0,
          "entity id zero remains in exact loaded order");
    gm_runtime_destroy(&runtime);
}

static void test_collection_discovery(void) {
    GmConfig config;
    GmRuntime runtime;
    char error[256] = {0};
    gm_config_defaults(&config);
    config.seed = 0;
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 3;
    config.mobs = 1;
    config.villages = 1;
    config.weather = 0;
    CHECK(gm_runtime_init(&runtime, &config, error, sizeof error), error);
    if (fail) return;
    gm_world_ensure(runtime.world, 0, 0, 3);
    for (int x = -16; x <= 16; ++x)
        for (int z = -16; z <= 16; ++z)
            gm_world_set_block(runtime.world, x, 63, z, 1);
    gm_runtime_set_pose(&runtime, 8.5, 64.0, 8.5, 0.0F, 0.0F);
    for (int x = 1; x <= 5; ++x)
        gm_world_set_block(runtime.world, x, 71, 0, 1);
    gm_world_set_block_meta(runtime.world, 0, 70, 0, 64, 0);
    gm_world_set_block_meta(runtime.world, 0, 71, 0, 64, 8);
    int villager = gm_mobs_spawn_villager(
        &runtime.mobs, 0.5, 70.0, 0.5, 0);
    CHECK(villager > 0, "spawn door-discovery villager");
    runtime.mobs.villager_random_tick_divider[villager] = 0;

    gm_runtime_tick(&runtime, (GmAction){.hotbar_sel = -1});
    CHECK(runtime.village_position_count == 1,
          "first villager update queues its block position");
    gm_runtime_tick(&runtime, (GmAction){.hotbar_sel = -1});
    CHECK(runtime.village_position_count == 0,
          "collection scans one queued position per tick");
    CHECK(runtime.village_state_count == 1
              && runtime.village_states[0].door_count == 1,
          "ordinary collection tick creates a village from a live door");
    if (runtime.village_state_count == 1
            && runtime.village_states[0].door_count == 1) {
        const GmVillageDoorState *door =
            &runtime.village_states[0].doors[0];
        CHECK(door->x == 0 && door->y == 70 && door->z == 0,
              "door scan keeps the lower-half position and loop order");
        CHECK(door->inside_dx == 2 && door->inside_dz == 0,
              "lower-sky east side becomes the exact inside direction");
        CHECK(door->timestamp == runtime.village_collection_tick,
              "new door records the current collection timestamp");
    }
    gm_runtime_destroy(&runtime);
}

static void test_mating_birth(void) {
    GmConfig config;
    GmRuntime runtime;
    char error[256] = {0};
    gm_config_defaults(&config);
    config.seed = 0;
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 3;
    config.mobs = 1;
    config.villages = 1;
    config.weather = 0;
    CHECK(gm_runtime_init(&runtime, &config, error, sizeof error), error);
    if (fail) return;
    gm_world_ensure(runtime.world, 0, 0, 3);
    for (int x = -16; x <= 16; ++x)
        for (int z = -16; z <= 16; ++z)
            gm_world_set_block(runtime.world, x, 63, z, 1);
    gm_runtime_set_pose(&runtime, 8.5, 64.0, 8.5, 0.0F, 0.0F);
    CHECK(gm_runtime_village_collection_begin(&runtime, 1, 1),
          "restore mating village collection");
    CHECK(gm_runtime_village_state_restore(
              &runtime, 0, 2, 32, 0, 1, 1, 0,
              0, 64, 0, 0, 1344, 0),
          "restore mating village core");
    for (int door = 0; door < 21; ++door) {
        int x = door - 10;
        gm_world_set_block_meta(runtime.world, x, 64, 10, 64, 0);
        CHECK(gm_runtime_village_door_restore(
                  &runtime, 0, x, 64, 10, 2, 0, 1),
              "restore mating village door");
    }
    int first = gm_mobs_spawn_villager(
        &runtime.mobs, 0.5, 64.0, 0.5, 0);
    int second = gm_mobs_spawn_villager(
        &runtime.mobs, 1.5, 64.0, 0.5, 1);
    CHECK(first > 0 && second > 0, "spawn mating pair");
    const EwStore *entities = store(&runtime);
    int first_eid = entities->id[first];
    int second_eid = entities->id[second];
    ICStack bread = ic_mk(297, 3, 0);
    CHECK(gm_mobs_set_villager_inventory_slot(
              &runtime.mobs, first_eid, 0, bread)
              && gm_mobs_set_villager_inventory_slot(
                  &runtime.mobs, second_eid, 0, bread),
          "give both villagers one exact willingness ration");
    uint64_t seed48 = mate_seed48();
    CHECK(seed48 != 0
              && gm_mobs_set_entity_random_state(
                  &runtime.mobs, first_eid, seed48, 0, 0.0)
              && gm_mobs_set_entity_random_state(
                  &runtime.mobs, second_eid, seed48, 0, 0.0),
          "select cursors whose post-ambient mate roll is zero");
    runtime.mobs.villager_ai_tick_count[first] = 0;
    runtime.mobs.villager_ai_tick_count[second] = 0;
    runtime.mobs.villager_random_tick_divider[first] = 1000;
    runtime.mobs.villager_random_tick_divider[second] = 1000;

    gm_runtime_tick(&runtime, (GmAction){.hotbar_sel = -1});
    int willing = 0, mating = 0, mate = -1, timeout = -1;
    CHECK(gm_mobs_get_villager_mating_state(
              &runtime.mobs, first_eid,
              &willing, &mating, &mate, &timeout)
              && willing && mating && mate == second_eid
              && timeout == 299,
          "priority-6 mate task consumes food and starts at timeout 300");
    for (int tick = 1; tick < 300; ++tick)
        gm_runtime_tick(&runtime, (GmAction){.hotbar_sel = -1});

    entities = store(&runtime);
    int villagers = 0, child_slot = -1;
    for (int slot = 1; slot < entities->count; ++slot)
        if (entities->alive[slot]
                && entities->type[slot] == EW_TYPE_VILLAGER) {
            ++villagers;
            if (entities->id[slot] != first_eid
                    && entities->id[slot] != second_eid)
                child_slot = slot;
        }
    CHECK(villagers == 3 && child_slot > 0,
          "300 courtship updates append exactly one villager child");
    first = gm_mobs_find_slot_by_eid(&runtime.mobs, first_eid);
    second = gm_mobs_find_slot_by_eid(&runtime.mobs, second_eid);
    CHECK(runtime.mobs.growing_age[first] == 5999
              && runtime.mobs.growing_age[second] == 5999,
          "both parents enter the exact post-birth cooldown age");
    CHECK(child_slot > 0
              && runtime.mobs.growing_age[child_slot] == -23999,
          "new child receives its same-loaded-boundary age update");
    if (child_slot > 0) {
        McAABB boxes[GM_MOB_CAPACITY];
        int count = gm_mobs_living_boxes(
            &runtime.mobs, 0, boxes, GM_MOB_CAPACITY);
        int found_child_box = 0;
        for (int box = 0; box < count; ++box) {
            double center_x = (boxes[box].minX + boxes[box].maxX) * 0.5;
            double center_z = (boxes[box].minZ + boxes[box].maxZ) * 0.5;
            if (fabs(center_x - entities->x[child_slot]) < 1.0e-9
                    && fabs(center_z - entities->z[child_slot]) < 1.0e-9
                    && fabs((boxes[box].maxX - boxes[box].minX)
                        - 0.30000001192092896) < 1.0e-6
                    && fabs((boxes[box].maxY - boxes[box].minY)
                        - 0.97500002384185791) < 1.0e-6)
                found_child_box = 1;
        }
        CHECK(found_child_box,
              "child villager uses EntityAgeable half-size collision box");
    }
    CHECK(!runtime.mobs.villager_willing[first]
              && !runtime.mobs.villager_willing[second],
          "birth clears both parents' willingness");
    CHECK(runtime.village_resident_count == 1
              && runtime.village_residents[0].eid
                  == entities->id[child_slot]
              && runtime.village_residents[0].trade.initialized,
          "birth carries initialized profession/trade state into runtime");
    gm_runtime_destroy(&runtime);
}

static void test_night_door_restriction(void) {
    GmConfig config;
    GmRuntime runtime;
    char error[256] = {0};
    gm_config_defaults(&config);
    config.seed = 0;
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 3;
    config.mobs = 1;
    config.villages = 1;
    config.weather = 0;
    CHECK(gm_runtime_init(&runtime, &config, error, sizeof error), error);
    if (fail) return;
    gm_world_ensure(runtime.world, 0, 0, 3);
    CHECK(gm_runtime_village_collection_begin(&runtime, 1, 1)
              && gm_runtime_village_state_restore(
                  &runtime, 0, 1, 32, 0, 1, 1, 0,
                  0, 64, 0, 0, 0, 0)
              && gm_runtime_village_door_restore(
                  &runtime, 0, 0, 64, 0, 2, 0, 1),
          "restore nighttime restriction village and door");
    gm_world_set_block_meta(runtime.world, 0, 64, 0, 64, 0);
    gm_world_set_block_meta(runtime.world, 0, 65, 0, 64, 8);
    int slot = gm_mobs_spawn_villager(
        &runtime.mobs, 1.5D, 64.0D, 0.5D, 0);
    CHECK(slot > 0, "spawn nighttime door villager");
    runtime.mobs.villager_ai_tick_count[slot] = 0;
    runtime.mobs.villager_random_tick_divider[slot] = 1000;
    runtime.clock.world_time = 13000;
    gm_runtime_tick(&runtime, (GmAction){.hotbar_sel = -1});
    CHECK(runtime.mobs.villager_restrict_door_active[slot]
              && !runtime.mobs.villager_enter_doors[slot]
              && !runtime.mobs.villager_break_doors[slot],
          "nighttime inside villager starts door restriction task");
    CHECK(runtime.village_states[0].doors[0].restriction == 1,
          "active restriction task increments the live VillageDoorInfo counter");

    EwStore *a = &runtime.mobs.a;
    EwStore *b = &runtime.mobs.b;
    a->x[slot] = b->x[slot] = -0.5D;
    gm_runtime_tick(&runtime, (GmAction){.hotbar_sel = -1});
    CHECK(!runtime.mobs.villager_restrict_door_active[slot]
              && runtime.mobs.villager_enter_doors[slot]
              && runtime.mobs.villager_break_doors[slot],
          "crossing outside resets both ground-navigator door flags");
    CHECK(runtime.village_states[0].doors[0].restriction == 1,
          "reset task does not add another restriction increment");
    gm_runtime_destroy(&runtime);
}

static void test_farmer_harvest(void) {
    GmConfig config;
    GmRuntime runtime;
    JavaRandom random;
    char error[256] = {0};
    gm_config_defaults(&config);
    config.seed = 0;
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 3;
    config.mobs = 1;
    config.villages = 0;
    config.weather = 0;
    config.daylight = 0;
    config.mob_griefing = 1;
    CHECK(gm_runtime_init(&runtime, &config, error, sizeof error), error);
    if (fail) return;
    gm_world_ensure(runtime.world, 0, 0, 3);
    gm_runtime_set_pose(&runtime, 8.5, 64.0, 8.5, 0.0F, 0.0F);
    gm_world_set_block_meta(runtime.world, 0, 63, 0, 60, 0);
    gm_world_set_block_meta(runtime.world, 0, 64, 0, 59, 7);
    int slot = gm_mobs_spawn_villager(
        &runtime.mobs, 0.5D, 64.0D, 0.5D, 0);
    CHECK(slot > 0, "spawn farmer villager");
    const EwStore *entities = store(&runtime);
    jrand_set(&random, 18);
    CHECK(gm_mobs_set_entity_random_state(
              &runtime.mobs, entities->id[slot], random.seed, 0, 0.0D),
          "set farmer entity RNG");
    runtime.mobs.villager_ai_tick_count[slot] = 0;
    runtime.mobs.villager_random_tick_divider[slot] = 1000;
    int item_count = runtime.entities.n_active;
    int event_count = gm_runtime_world_event_count(&runtime);

    gm_runtime_tick(&runtime, (GmAction){.hotbar_sel = -1});
    CHECK(gm_world_block(runtime.world, 0, 64, 0) == 0,
          "priority-6 farmer destroys a mature crop on its start tick");
    CHECK(runtime.entities.n_active > item_count,
          "farmer harvest routes exact crop drops into the live item store");
    int wheat = 0, seeds = 0;
    for (int i = 0; i < GM_LIVE_MAX; ++i)
        if (runtime.entities.ents[i].active) {
            wheat += runtime.entities.ents[i].item == 296;
            seeds += runtime.entities.ents[i].item == 295;
        }
    CHECK(wheat == 1 && seeds >= 1 && seeds <= 3,
          "mature wheat yields one crop and the oracle-bounded seed rolls");
    GmRuntimeWorldEvent event;
    CHECK(gm_runtime_world_event_count(&runtime) == event_count + 1
              && gm_runtime_world_event_get(
                  &runtime, event_count, &event)
              && event.id == 2001 && event.x == 0
              && event.y == 64 && event.z == 0
              && event.data == (59 | (7 << 12)),
          "crop destruction publishes the exact Block.getStateId world event");
    CHECK(runtime.mobs.villager_harvest_current_task[slot] == -1
              && runtime.mobs.villager_harvest_run_delay[slot] == 10,
          "farmer resets its task and installs the exact short retry delay");
    gm_runtime_destroy(&runtime);
}

static void test_villager_interaction(void) {
    GmConfig config;
    GmRuntime runtime;
    char error[256] = {0};
    gm_config_defaults(&config);
    config.seed = 0;
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 3;
    config.mobs = 1;
    config.villages = 0;
    config.weather = 0;
    config.daylight = 0;
    CHECK(gm_runtime_init(&runtime, &config, error, sizeof error), error);
    if (fail) return;
    gm_world_ensure(runtime.world, 0, 0, 3);
    for (int x = -4; x <= 12; ++x)
        for (int z = -4; z <= 12; ++z)
            gm_world_load_block_meta(runtime.world, x, 63, z, 1, 0);
    gm_runtime_set_pose(&runtime, 8.5, 64.0, 8.5, 0.0F, 0.0F);
    int giver = gm_mobs_spawn_villager(
        &runtime.mobs, 0.5D, 64.0D, 0.5D, 1);
    int receiver = gm_mobs_spawn_villager(
        &runtime.mobs, 2.5D, 64.0D, 0.5D, 1);
    CHECK(giver > 0 && receiver > 0,
          "spawn villager interaction pair");
    const EwStore *entities = store(&runtime);
    int giver_eid = entities->id[giver];
    uint64_t seed48 = interact_seed48();
    CHECK(seed48 != 0
              && gm_mobs_set_villager_inventory_slot(
                  &runtime.mobs, giver_eid, 0, ic_mk(297, 8, 0))
              && gm_mobs_set_entity_random_state(
                  &runtime.mobs, giver_eid, seed48, 0, 0.0D),
          "seed priority-9 villager interaction and inventory");
    runtime.mobs.controlled_no_ai[receiver] = 1;
    runtime.mobs.villager_ai_tick_count[giver] = 0;
    runtime.mobs.villager_random_tick_divider[giver] = 1000;
    int before = runtime.entities.n_active;
    for (int tick = 0; tick < 10; ++tick)
        gm_runtime_tick(&runtime, (GmAction){.hotbar_sel = -1});
    CHECK(runtime.mobs.villager_interact_active[giver]
              && runtime.mobs.villager_interact_target_eid[giver]
                  == entities->id[receiver],
          "priority-9 interaction remains focused on its nearest villager");
    CHECK(runtime.mobs.villager_inventory[giver][0].count == 4,
          "interaction halves the giver's bread stack after ten updates");
    int shared = 0;
    for (int item = 0; item < GM_LIVE_MAX; ++item)
        if (runtime.entities.ents[item].active
                && runtime.entities.ents[item].item == 297
                && runtime.entities.ents[item].count == 4)
            ++shared;
    CHECK(runtime.entities.n_active == before + 1 && shared == 1,
          "interaction appends exactly one four-bread EntityItem");
    gm_runtime_destroy(&runtime);
}

int main(void) {
    test_cold_reload_entity_id_zero();
    test_collection_discovery();
    test_mating_birth();
    test_night_door_restriction();
    test_farmer_harvest();
    test_villager_interaction();
    if (fail) return 1;
    puts("villager_ai_runtime: PASS doors, mating, play, farming, and sharing");
    return 0;
}
