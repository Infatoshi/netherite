#include "game/runtime.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int fail;
#define CHECK(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s\n", message); \
        fail = 1; \
    } \
} while (0)

enum { EXPECTED_MAX = 2 };
typedef struct {
    int count;
    int item[EXPECTED_MAX];
    int quantity[EXPECTED_MAX];
    int meta[EXPECTED_MAX];
} ExpectedLoot;

static EwStore *store(GmMobLive *m) {
    return m->current ? &m->b : &m->a;
}

static int stage(
        GmRuntime *runtime, int type, int eid,
        float health, uint64_t seed48) {
    GmMobLive *m = &runtime->mobs;
    gm_mobs_init(m, 0);
    m->active_dimension = 0;
    m->next_id = eid;
    int slot = gm_mobs_spawn(m, type, 24.5, 220.0, 24.5);
    if (slot <= 0) return -1;
    m->a.health[slot] = health;
    m->b.health[slot] = health;
    m->persistence_required[slot] = 1;
    m->entity_air[slot] = -19;
    if (!gm_mobs_set_entity_random_state(m, eid, seed48, 0, 0.0))
        return -1;
    return slot;
}

static void tick(
        GmRuntime *runtime, uint64_t *world_seed, uint64_t *math_seed,
        int *next_id, int do_mob_loot, GmLiveSim *drops) {
    gm_mobs_tick(
        &runtime->mobs, runtime->world, NULL,
        (const struct McSinTable *)&runtime->sin_table,
        (struct PsvPlayer *)&runtime->player,
        (struct PvStats *)&runtime->vitals,
        runtime->ox, runtime->oz, runtime->dimension, 12000,
        &runtime->clock, 0, world_seed, math_seed, next_id,
        do_mob_loot, drops, 0.0F, 0.0F);
}

static void append_expected(
        ExpectedLoot *loot, int item, int count, int meta) {
    if (count <= 0 || loot->count >= EXPECTED_MAX) return;
    int index = loot->count++;
    loot->item[index] = item;
    loot->quantity[index] = count;
    loot->meta[index] = meta;
}

static ExpectedLoot generate_expected(
        int type, JavaRandom *random, int cooked) {
    ExpectedLoot loot = {0};
    if (type == EW_TYPE_CHICKEN) {
        (void)jrand_int_bound(random, 1);
        int feathers = jrand_int_bound(random, 3);
        (void)jrand_int_bound(random, 1);
        append_expected(&loot, 288, feathers, 0);
        append_expected(&loot, cooked ? 366 : 365, 1, 0);
    } else if (type == EW_TYPE_PIG) {
        (void)jrand_int_bound(random, 1);
        append_expected(
            &loot, cooked ? 320 : 319,
            1 + jrand_int_bound(random, 3), 0);
    } else if (type == EW_TYPE_COW) {
        (void)jrand_int_bound(random, 1);
        int leather = jrand_int_bound(random, 3);
        (void)jrand_int_bound(random, 1);
        int beef = 1 + jrand_int_bound(random, 3);
        append_expected(&loot, 334, leather, 0);
        append_expected(&loot, cooked ? 364 : 363, beef, 0);
    } else {
        (void)jrand_int_bound(random, 1);
        (void)jrand_int_bound(random, 1);
        (void)jrand_int_bound(random, 1);
        append_expected(&loot, 35, 1, 0);
        append_expected(
            &loot, cooked ? 424 : 423,
            1 + jrand_int_bound(random, 2), 0);
    }
    return loot;
}

static int exact_item(
        const GmLiveEnt *item, int eid, int expected_item,
        int count, int meta, JavaRandom *math) {
    float hover = (float)(jrand_double(math) * (MC_PI * 2.0));
    float yaw = (float)(jrand_double(math) * 360.0);
    double motion_x = (double)(float)(jrand_double(math)
        * 0.20000000298023224 - 0.10000000149011612);
    double motion_z = (double)(float)(jrand_double(math)
        * 0.20000000298023224 - 0.10000000149011612);
    return item->active && item->eid == eid
        && item->item == expected_item && item->count == count
        && item->meta == meta
        && item->x == 24.5 && item->y == 220.0 && item->z == 24.5
        && item->mx == motion_x && item->my == 0.20000000298023224
        && item->mz == motion_z && item->yaw == yaw
        && item->has_hover_start && item->hover_start == hover
        && item->age == 0 && item->pickup_delay == 10
        && item->health == 5 && item->lifespan == 6000;
}

static void clear_fixture(GmRuntime *runtime) {
    for (int x = 23; x <= 25; ++x)
        for (int y = 219; y <= 226; ++y)
            for (int z = 23; z <= 25; ++z)
                gm_world_set_block(runtime->world, x, y, z, 0);
}

static int run_base_case(
        GmRuntime *runtime, int type, int index, int scenario) {
    clear_fixture(runtime);
    if (scenario == 0)
        for (int y = 219; y <= 226; ++y)
            gm_world_set_block(runtime->world, 24, y, 24, 9);
    else if (scenario == 2)
        gm_world_set_block(runtime->world, 24, 220, 24, 10);
    uint64_t entity_seed = (uint64_t)(8000 + scenario * 1000 + index * 53);
    uint64_t world_seed = UINT64_C(192837465);
    uint64_t math_seed = (uint64_t)(12000 + scenario * 2000 + index * 97);
    int next_id = 3000 + scenario * 100 + index * 8;
    int initial_next = next_id;
    GmLiveSim drops;
    memset(&drops, 0, sizeof drops);
    float health = scenario == 0 ? 2.0F : 1.0F;
    int slot = stage(runtime, type, 900 + scenario * 10 + index,
        health, entity_seed);
    CHECK(slot > 0, "passive base fixture initializes");
    if (slot <= 0) return 0;
    if (scenario == 1) runtime->mobs.fire_ticks[slot] = 20;
    if (scenario == 3) {
        float width, height;
        ehs_size((u8)type, &width, &height);
        (void)height;
        int x = mc_floor(24.5 - (double)width * 0.4D);
        for (int y = 220; y <= 222; ++y)
            gm_world_set_block(runtime->world, x, y, x, 1);
    }

    JavaRandom expected_random = {entity_seed};
    if (scenario == 0)
        for (int draw = 0; draw < 48; ++draw)
            (void)jrand_float(&expected_random);
    else
        (void)jrand_double(&expected_random);
    JavaRandom expected_math = {math_seed};
    (void)jrand_double(&expected_math);
    (void)jrand_float(&expected_random);
    (void)jrand_float(&expected_random);
    ExpectedLoot loot = generate_expected(
        type, &expected_random, scenario == 1);

    tick(runtime, &world_seed, &math_seed, &next_id, 1, &drops);
    EwStore *s = store(&runtime->mobs);
    CHECK(s->alive[slot] && s->health[slot] == 0.0F
              && runtime->mobs.entity_dead[slot]
              && runtime->mobs.entity_death_time[slot] == 1
              && runtime->mobs.entity_hurt_time[slot] == 9
              && runtime->mobs.entity_hurt_resistant[slot] == 19
              && runtime->mobs.entity_recently_hit[slot] == 0
              && runtime->mobs.entity_attacking_player[slot] == 0,
          "passive base source retains an exact no-credit corpse");
    CHECK(drops.n_active == loot.count,
          "passive base source emits exact loot stack count");
    for (int item = 0; item < loot.count; ++item)
        CHECK(exact_item(
                  &drops.ents[item], initial_next + item,
                  loot.item[item], loot.quantity[item], loot.meta[item],
                  &expected_math),
              "passive base source preserves EntityItem constructor state");
    CHECK(runtime->mobs.entity_random[slot].random.seed
                  == expected_random.seed
              && math_seed == expected_math.seed
              && next_id == initial_next + loot.count
              && runtime->mobs.next_id == next_id
              && runtime->mobs.next_orb_id == next_id,
          "passive base source commits exact private, Math, and ID cursors");
    GmMobEvent event;
    CHECK(gm_mobs_event_count(&runtime->mobs) == 3
              && gm_mobs_event_get(&runtime->mobs, 0, &event)
              && event.kind == GM_MOB_EVENT_ENTITY_STATUS
              && event.data == 2
              && gm_mobs_event_get(&runtime->mobs, 1, &event)
              && event.kind == GM_MOB_EVENT_SOUND
              && gm_mobs_event_get(&runtime->mobs, 2, &event)
              && event.kind == GM_MOB_EVENT_ENTITY_STATUS
              && event.data == 3,
          "passive base source orders hurt, sound, and death status");
    return 1;
}

static int run_contact_case(
        GmRuntime *runtime, int type, int index, int block) {
    clear_fixture(runtime);
    for (int x = 23; x <= 25; ++x)
        for (int y = 219; y <= 222; ++y)
            for (int z = 23; z <= 25; ++z)
                gm_world_set_block(runtime->world, x, y, z, block);
    uint64_t world_seed = UINT64_C(77665544);
    uint64_t math_seed = (uint64_t)(22000 + block * 31 + index * 17);
    uint64_t initial_math = math_seed;
    int next_id = 5000 + index * 8;
    GmLiveSim drops;
    memset(&drops, 0, sizeof drops);
    int slot = stage(runtime, type, 1700 + index, 0.5F,
        (uint64_t)(14000 + index * 43));
    CHECK(slot > 0, "passive contact fixture initializes");
    if (slot <= 0) return 0;
    tick(runtime, &world_seed, &math_seed, &next_id, 0, &drops);
    EwStore *s = store(&runtime->mobs);
    JavaRandom expected_math = {initial_math};
    (void)jrand_double(&expected_math);
    CHECK(s->alive[slot] && s->health[slot] == 0.0F
              && runtime->mobs.entity_dead[slot]
              && runtime->mobs.entity_death_time[slot] == 0
              && runtime->mobs.entity_hurt_time[slot] == 10
              && runtime->mobs.entity_hurt_resistant[slot] == 20
              && drops.n_active == 0 && math_seed == expected_math.seed
              && gm_mobs_event_count(&runtime->mobs) == 3,
          "passive contact retains exact unaged no-loot corpse boundary");
    return 1;
}

static void run_capacity_cases(GmRuntime *runtime) {
    clear_fixture(runtime);
    gm_world_set_block(runtime->world, 24, 220, 24, 10);
    uint64_t world_seed = UINT64_C(12345);
    uint64_t math_seed = UINT64_C(24680);
    int next_id = 7000;
    GmLiveSim drops;
    memset(&drops, 0, sizeof drops);
    for (int item = 0; item < GM_LIVE_MAX; ++item)
        drops.ents[item].active = 1;
    drops.n_active = GM_LIVE_MAX;
    drops.item_spawn_limit = GM_LIVE_MAX;
    int slot = stage(runtime, EW_TYPE_COW, 2100, 3.0F, 77);
    runtime->mobs.fire_ticks[slot] = 20;
    runtime->mobs.sheep_in_love[slot] = 600;
    runtime->mobs.sheep_bred_by_player[slot] = 1;
    tick(runtime, &world_seed, &math_seed, &next_id, 1, &drops);
    EwStore *s = store(&runtime->mobs);
    CHECK(slot > 0 && s->health[slot] == 3.0F
              && !runtime->mobs.entity_dead[slot]
              && runtime->mobs.fire_ticks[slot] == 20
              && runtime->mobs.sheep_in_love[slot] == 600
              && runtime->mobs.sheep_bred_by_player[slot] == 1
              && runtime->mobs.entity_random[slot].random.seed == 77
              && math_seed == UINT64_C(24680) && next_id == 7000
              && gm_mobs_event_count(&runtime->mobs) == 0,
          "full pool rejects passive ON_FIRE-before-lava atomically");

    clear_fixture(runtime);
    for (int y = 219; y <= 226; ++y)
        gm_world_set_block(runtime->world, 24, y, 24, 9);
    world_seed = UINT64_C(54321);
    math_seed = UINT64_C(13579);
    next_id = 8000;
    memset(&drops, 0, sizeof drops);
    for (int item = 0; item < GM_LIVE_MAX; ++item)
        drops.ents[item].active = 1;
    drops.n_active = GM_LIVE_MAX;
    drops.item_spawn_limit = GM_LIVE_MAX;
    slot = stage(runtime, EW_TYPE_PIG, 2101, 2.0F, 88);
    runtime->mobs.pig_saddled[slot] = 1;
    runtime->mobs.sheep_in_love[slot] = 600;
    runtime->mobs.sheep_bred_by_player[slot] = 1;
    tick(runtime, &world_seed, &math_seed, &next_id, 0, &drops);
    s = store(&runtime->mobs);
    CHECK(slot > 0 && s->health[slot] == 2.0F
              && !runtime->mobs.entity_dead[slot]
              && runtime->mobs.entity_air[slot] == -19
              && runtime->mobs.sheep_in_love[slot] == 600
              && runtime->mobs.sheep_bred_by_player[slot] == 1
              && runtime->mobs.entity_random[slot].random.seed == 88
              && math_seed == UINT64_C(13579) && next_id == 8000
              && gm_mobs_event_count(&runtime->mobs) == 0,
          "full pool rejects gamerule-independent saddle drop atomically");
}

int main(void) {
    static const int types[] = {
        EW_TYPE_SHEEP, EW_TYPE_PIG, EW_TYPE_COW, EW_TYPE_CHICKEN
    };
    GmConfig config;
    GmRuntime runtime;
    char error[256] = {0};
    gm_config_defaults(&config);
    config.seed = 0;
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.mobs = 0;
    config.weather = 0;
    CHECK(gm_runtime_init(&runtime, &config, error, sizeof error), error);
    if (fail) return 1;
    gm_runtime_set_pose(&runtime, 8.5, 5.0, 8.5, 0.0F, 0.0F);
    int deaths = 0;
    for (int scenario = 0; scenario < 4; ++scenario)
        for (int index = 0; index < 4; ++index)
            deaths += run_base_case(
                &runtime, types[index], index, scenario);
    for (int index = 0; index < 4; ++index) {
        deaths += run_contact_case(&runtime, types[index], index, 51);
        deaths += run_contact_case(&runtime, types[index], index, 81);
    }
    run_capacity_cases(&runtime);
    gm_runtime_destroy(&runtime);
    if (fail) return 1;
    printf("passive_environment_death_live: PASS deaths=%d "
           "drown=4 on_fire=4 lava=4 in_wall=4 in_fire=4 cactus=4 "
           "loot=exact rng=exact corpse=exact capacity_atomic=2\n",
           deaths);
    return 0;
}
