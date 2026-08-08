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

static int fire_immune(int type) {
    return type == EW_TYPE_BLAZE || type == EW_TYPE_GHAST
        || type == EW_TYPE_MAGMA || type == EW_TYPE_PIGMAN
        || type == EW_TYPE_WITHER_SKELETON;
}

static EwStore *store(GmMobLive *m) {
    return m->current ? &m->b : &m->a;
}

static int stage(
        GmRuntime *runtime, int type, int size, int eid,
        float health, uint64_t seed48) {
    GmMobLive *m = &runtime->mobs;
    gm_mobs_init(m, 0);
    m->active_dimension = 0;
    m->next_id = eid;
    int slot = gm_mobs_spawn_sized(m, type, 24.5, 220.0, 24.5, size);
    if (slot <= 0) return -1;
    m->a.health[slot] = health;
    m->b.health[slot] = health;
    m->persistence_required[slot] = 1;
    m->entity_air[slot] = -19;
    if (!gm_mobs_set_entity_random_state(m, eid, seed48, 0, 0.0))
        return -1;
    return slot;
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

static uint64_t enderman_nonteleport_seed(
        int bubble_draws, int set_been_attacked) {
    for (uint64_t seed = 0; seed < UINT64_C(100000); ++seed) {
        JavaRandom random = {seed};
        GmHostileLootOutcome loot;
        for (int draw = 0; draw < bubble_draws; ++draw)
            (void)jrand_float(&random);
        if (set_been_attacked) (void)jrand_double(&random);
        (void)jrand_float(&random);
        (void)jrand_float(&random);
        uint64_t cursor = random.seed;
        if (!gm_mobs_generate_hostile_loot(
                EW_TYPE_ENDERMAN, 1, &cursor, 0, 0, &loot))
            continue;
        random.seed = cursor;
        if (jrand_int_bound(&random, 10) == 0) return seed;
    }
    return UINT64_MAX;
}

static int run_base_source_case(
        GmRuntime *runtime, int type, int size, int index,
        int scenario, uint64_t entity_seed) {
    uint64_t world_seed = UINT64_C(192837465);
    uint64_t math_seed = (uint64_t)(17000 + scenario * 5000 + index * 139);
    int eid = 1700 + scenario * 100 + index;
    int next_id = 4000 + scenario * 200 + index * 8;
    int initial_next_id = next_id;
    GmLiveSim drops;
    GmHostileLootOutcome loot;
    JavaRandom expected_random = {entity_seed};
    JavaRandom expected_math = {math_seed};
    memset(&drops, 0, sizeof drops);
    for (int y = 219; y <= 226; ++y)
        gm_world_set_block(runtime->world, 24, y, 24, 0);
    if (scenario == 1)
        gm_world_set_block(runtime->world, 24, 220, 24, 10);
    int slot = stage(runtime, type, size, eid, 1.0F, entity_seed);
    CHECK(slot > 0, "ordinary base-source fixture initializes");
    if (slot <= 0) return 0;
    int wall_x = 24, wall_z = 24;
    if (scenario == 2) {
        float width, height;
        ehs_size_scaled((u8)type, size, &width, &height);
        (void)height;
        wall_x = mc_floor(24.5 - (double)width * 0.4D);
        wall_z = mc_floor(24.5 - (double)width * 0.4D);
        for (int y = 220; y <= 226; ++y)
            gm_world_set_block(runtime->world, wall_x, y, wall_z, 1);
    }
    if (scenario == 0) runtime->mobs.fire_ticks[slot] = 20;
    if (scenario < 2 && fire_immune(type)) {
        tick(runtime, &world_seed, &math_seed, &next_id, 1, &drops);
        EwStore *immune = store(&runtime->mobs);
        int immune_exact = immune->alive[slot] && immune->health[slot] == 1.0F
                  && !runtime->mobs.entity_dead[slot]
                  && runtime->mobs.entity_hurt_time[slot] == 0
                  && runtime->mobs.entity_hurt_resistant[slot] == 0
                  && drops.n_active == 0
                  && math_seed == expected_math.seed
                  && next_id == initial_next_id
                  && gm_mobs_event_count(&runtime->mobs) == 0;
        if (!immune_exact)
            fprintf(stderr,
                "immune scenario=%d type=%d size=%d alive=%d health=%g "
                "dead=%d hurt=%d resist=%d fire=%d drops=%d "
                "math=%llu/%llu next=%d/%d events=%d\n",
                scenario, type, size, immune->alive[slot],
                (double)immune->health[slot], runtime->mobs.entity_dead[slot],
                runtime->mobs.entity_hurt_time[slot],
                runtime->mobs.entity_hurt_resistant[slot],
                runtime->mobs.fire_ticks[slot], drops.n_active,
                (unsigned long long)math_seed,
                (unsigned long long)expected_math.seed,
                next_id, initial_next_id,
                gm_mobs_event_count(&runtime->mobs));
        CHECK(immune_exact,
              "fire-immune family rejects source without combat state");
        CHECK(scenario != 0 || runtime->mobs.fire_ticks[slot] == 16,
              "fire-immune periodic counter decays by four");
        CHECK(scenario != 1 || runtime->mobs.fire_ticks[slot] == 1,
              "fire-immune lava skips floor but keeps flammable tail");
        return 0;
    }

    (void)jrand_double(&expected_random);
    (void)jrand_double(&expected_math);
    (void)jrand_float(&expected_random);
    (void)jrand_float(&expected_random);
    uint64_t loot_cursor = expected_random.seed;
    CHECK(gm_mobs_generate_hostile_loot(
              type, size, &loot_cursor, 0, 0, &loot),
          "ordinary base source has an exact no-credit loot table");
    expected_random.seed = loot_cursor;
    if (type == EW_TYPE_ENDERMAN && scenario != 1)
        CHECK(jrand_int_bound(&expected_random, 10) == 0,
              "unblockable Enderman source selects no-teleport tail");

    tick(runtime, &world_seed, &math_seed, &next_id, 1, &drops);
    EwStore *s = store(&runtime->mobs);
    int corpse_exact = s->alive[slot] && s->health[slot] == 0.0F
              && runtime->mobs.entity_dead[slot]
              && runtime->mobs.entity_death_time[slot] == 1
              && runtime->mobs.entity_hurt_time[slot] == 9
              && runtime->mobs.entity_hurt_resistant[slot] == 19
              && runtime->mobs.entity_recently_hit[slot] == 0
              && runtime->mobs.entity_attacking_player[slot] == 0;
    if (!corpse_exact)
        fprintf(stderr,
            "scenario=%d type=%d size=%d health=%g dead=%d death=%d "
            "hurt=%d resist=%d fire=%d events=%d drops=%d/%d\n",
            scenario, type, size, (double)s->health[slot],
            runtime->mobs.entity_dead[slot],
            runtime->mobs.entity_death_time[slot],
            runtime->mobs.entity_hurt_time[slot],
            runtime->mobs.entity_hurt_resistant[slot],
            runtime->mobs.fire_ticks[slot],
            gm_mobs_event_count(&runtime->mobs), drops.n_active, loot.count);
    CHECK(corpse_exact,
          "ordinary base source enters exact no-credit corpse window");
    CHECK(scenario != 0 || runtime->mobs.fire_ticks[slot] == 19,
          "periodic ON_FIRE damage precedes signed fire decrement");
    CHECK(scenario != 1 || runtime->mobs.fire_ticks[slot] == 301,
          "lava floor precedes the same-tick flammable contact increment");
    CHECK(drops.n_active == loot.count,
          "ordinary base source emits every exact no-credit loot stack");
    for (int item = 0; item < loot.count; ++item)
        CHECK(exact_item(
                  &drops.ents[item], initial_next_id + item,
                  loot.item[item], loot.quantity[item], loot.meta[item],
                  &expected_math),
              "ordinary base source preserves exact EntityItem state");
    CHECK(runtime->mobs.entity_random[slot].random.seed
                  == expected_random.seed
              && math_seed == expected_math.seed
              && next_id == initial_next_id + loot.count
              && runtime->mobs.next_id == next_id
              && runtime->mobs.next_orb_id == next_id,
          "ordinary base source commits exact private, Math, and ID cursors");
    GmMobEvent event;
    CHECK(gm_mobs_event_count(&runtime->mobs) == 3
              && gm_mobs_event_get(&runtime->mobs, 0, &event)
              && event.kind == GM_MOB_EVENT_ENTITY_STATUS
              && event.eid == eid && event.data == 2
              && gm_mobs_event_get(&runtime->mobs, 1, &event)
              && event.kind == GM_MOB_EVENT_SOUND
              && gm_mobs_event_get(&runtime->mobs, 2, &event)
              && event.kind == GM_MOB_EVENT_ENTITY_STATUS
              && event.eid == eid && event.data == 3,
          "ordinary base source orders hurt, sound, and death status");
    if (scenario == 2)
        for (int y = 220; y <= 226; ++y)
            gm_world_set_block(runtime->world, wall_x, y, wall_z, 0);
    return 1;
}

static int run_drown_case(
        GmRuntime *runtime, int type, int size, int index,
        uint64_t entity_seed) {
    uint64_t world_seed = UINT64_C(987654321);
    uint64_t math_seed = (uint64_t)(7000 + index * 131);
    int eid = 700 + index;
    int next_id = 1000 + index * 8;
    int initial_next_id = next_id;
    GmLiveSim drops;
    GmHostileLootOutcome loot;
    JavaRandom expected_random = {entity_seed};
    JavaRandom expected_math = {math_seed};
    memset(&drops, 0, sizeof drops);
    int slot = stage(runtime, type, size, eid, 2.0F, entity_seed);
    CHECK(slot > 0, "ordinary drowning fixture initializes");
    if (slot <= 0) return 0;

    for (int draw = 0; draw < 48; ++draw)
        (void)jrand_float(&expected_random);
    (void)jrand_double(&expected_math);
    (void)jrand_float(&expected_random);
    (void)jrand_float(&expected_random);
    uint64_t loot_cursor = expected_random.seed;
    CHECK(gm_mobs_generate_hostile_loot(
              type, size, &loot_cursor, 0, 0, &loot),
          "ordinary drowning type has an exact no-credit loot table");
    expected_random.seed = loot_cursor;
    if (type == EW_TYPE_ENDERMAN) {
        CHECK(jrand_int_bound(&expected_random, 10) == 0,
              "Enderman fixture selects the no-teleport override tail");
    }

    tick(runtime, &world_seed, &math_seed, &next_id, 1, &drops);
    EwStore *s = store(&runtime->mobs);
    CHECK(s->alive[slot] && s->health[slot] == 0.0F
              && runtime->mobs.entity_dead[slot]
              && runtime->mobs.entity_death_time[slot] == 1
              && runtime->mobs.entity_air[slot] == 0
              && runtime->mobs.entity_hurt_time[slot] == 9
              && runtime->mobs.entity_hurt_resistant[slot] == 19
              && runtime->mobs.entity_recently_hit[slot] == 0
              && runtime->mobs.entity_attacking_player[slot] == 0,
          "ordinary drowning enters the exact no-credit corpse window");
    CHECK(drops.n_active == loot.count,
          "ordinary drowning emits every exact no-credit loot stack");
    for (int item = 0; item < loot.count; ++item)
        CHECK(exact_item(
                  &drops.ents[item], initial_next_id + item,
                  loot.item[item], loot.quantity[item], loot.meta[item],
                  &expected_math),
              "ordinary drowning preserves exact EntityItem state");
    int cursors_exact = runtime->mobs.entity_random[slot].random.seed
            == expected_random.seed
        && math_seed == expected_math.seed
        && next_id == initial_next_id + loot.count
        && runtime->mobs.next_id == next_id
        && runtime->mobs.next_orb_id == next_id;
    if (!cursors_exact)
        fprintf(stderr,
            "type=%d size=%d seed=%llu/%llu math=%llu/%llu "
            "next=%d/%d mob_next=%d orb_next=%d loot=%d\n",
            type, size,
            (unsigned long long)runtime->mobs.entity_random[slot].random.seed,
            (unsigned long long)expected_random.seed,
            (unsigned long long)math_seed,
            (unsigned long long)expected_math.seed,
            next_id, initial_next_id + loot.count,
            runtime->mobs.next_id, runtime->mobs.next_orb_id, loot.count);
    CHECK(cursors_exact,
          "ordinary drowning commits exact private, Math, and ID cursors");
    GmMobEvent event;
    CHECK(gm_mobs_event_count(&runtime->mobs) == 3
              && gm_mobs_event_get(&runtime->mobs, 0, &event)
              && event.kind == GM_MOB_EVENT_ENTITY_STATUS
              && event.eid == eid && event.data == 2
              && gm_mobs_event_get(&runtime->mobs, 1, &event)
              && event.kind == GM_MOB_EVENT_SOUND
              && gm_mobs_event_get(&runtime->mobs, 2, &event)
              && event.kind == GM_MOB_EVENT_ENTITY_STATUS
              && event.eid == eid && event.data == 3,
          "ordinary drowning orders hurt, sound, and death status");
    return 1;
}

static void run_capacity_case(GmRuntime *runtime) {
    for (int y = 219; y <= 226; ++y)
        gm_world_set_block(runtime->world, 24, y, 24, 0);
    for (int y = 219; y <= 226; ++y)
        gm_world_set_block(runtime->world, 24, y, 24, 9);
    uint64_t seed = 0;
    GmHostileLootOutcome loot = {0};
    for (; seed < UINT64_C(100000) && loot.count == 0; ++seed) {
        JavaRandom random = {seed};
        for (int draw = 0; draw < 48; ++draw) (void)jrand_float(&random);
        (void)jrand_float(&random);
        (void)jrand_float(&random);
        uint64_t cursor = random.seed;
        (void)gm_mobs_generate_hostile_loot(
            EW_TYPE_ZOMBIE, 1, &cursor, 0, 0, &loot);
    }
    uint64_t entity_seed = seed - 1;
    uint64_t world_seed = UINT64_C(12345);
    uint64_t math_seed = UINT64_C(24680);
    int next_id = 3000;
    GmLiveSim drops;
    memset(&drops, 0, sizeof drops);
    for (int item = 0; item < GM_LIVE_MAX; ++item)
        drops.ents[item].active = 1;
    drops.n_active = GM_LIVE_MAX;
    drops.item_spawn_limit = GM_LIVE_MAX;
    int slot = stage(runtime, EW_TYPE_ZOMBIE, 1, 900, 2.0F, entity_seed);
    tick(runtime, &world_seed, &math_seed, &next_id, 1, &drops);
    EwStore *s = store(&runtime->mobs);
    CHECK(slot > 0 && loot.count > 0 && s->health[slot] == 2.0F
              && !runtime->mobs.entity_dead[slot]
              && runtime->mobs.entity_air[slot] == -19
              && runtime->mobs.entity_hurt_time[slot] == 0
              && runtime->mobs.entity_hurt_resistant[slot] == 0
              && runtime->mobs.entity_random[slot].random.seed
                  == entity_seed
              && math_seed == UINT64_C(24680) && next_id == 3000
              && gm_mobs_event_count(&runtime->mobs) == 0,
          "full item pool rejects ordinary drowning atomically");

    /* A due ON_FIRE pulse can survive before same-boundary lava becomes
     * lethal. The combined preview must reject both hits as one boundary. */
    for (int y = 219; y <= 226; ++y)
        gm_world_set_block(runtime->world, 24, y, 24, 0);
    gm_world_set_block(runtime->world, 24, 220, 24, 10);
    world_seed = UINT64_C(54321);
    math_seed = UINT64_C(13579);
    next_id = 5000;
    memset(&drops, 0, sizeof drops);
    for (int item = 0; item < GM_LIVE_MAX; ++item)
        drops.ents[item].active = 1;
    drops.n_active = GM_LIVE_MAX;
    drops.item_spawn_limit = GM_LIVE_MAX;
    slot = stage(runtime, EW_TYPE_ZOMBIE, 1, 901, 3.0F, entity_seed);
    runtime->mobs.fire_ticks[slot] = 20;
    tick(runtime, &world_seed, &math_seed, &next_id, 1, &drops);
    s = store(&runtime->mobs);
    CHECK(slot > 0 && s->health[slot] == 3.0F
              && !runtime->mobs.entity_dead[slot]
              && runtime->mobs.fire_ticks[slot] == 20
              && runtime->mobs.entity_hurt_time[slot] == 0
              && runtime->mobs.entity_hurt_resistant[slot] == 0
              && runtime->mobs.entity_random[slot].random.seed
                  == entity_seed
              && math_seed == UINT64_C(13579) && next_id == 5000
              && gm_mobs_event_count(&runtime->mobs) == 0,
          "full pool rejects combined ON_FIRE-before-lava atomically");
}

static int run_contact_matrix(
        GmRuntime *runtime, const int *types, const int *sizes,
        int count, int block, int fire_contact) {
    for (int x = 23; x <= 25; ++x)
        for (int y = 219; y <= 222; ++y)
            for (int z = 23; z <= 25; ++z)
                gm_world_set_block(runtime->world, x, y, z, block);
    int deaths = 0;
    for (int index = 0; index < count; ++index) {
        uint64_t world_seed = UINT64_C(99887766);
        uint64_t math_seed = (uint64_t)(33000 + block * 97 + index * 17);
        uint64_t initial_math = math_seed;
        int next_id = 7000 + index * 8;
        int initial_next = next_id;
        GmLiveSim drops;
        memset(&drops, 0, sizeof drops);
        int slot = stage(
            runtime, types[index], sizes[index], 2700 + index,
            0.5F, (uint64_t)(4500 + index * 43));
        CHECK(slot > 0, "ordinary contact fixture initializes");
        if (slot <= 0) continue;
        tick(runtime, &world_seed, &math_seed, &next_id, 0, &drops);
        EwStore *s = store(&runtime->mobs);
        int immune = fire_contact && fire_immune(types[index]);
        if (immune) {
            CHECK(s->alive[slot] && s->health[slot] == 0.5F
                      && !runtime->mobs.entity_dead[slot]
                      && runtime->mobs.entity_hurt_time[slot] == 0
                      && runtime->mobs.entity_hurt_resistant[slot] == 0
                      && math_seed == initial_math
                      && gm_mobs_event_count(&runtime->mobs) == 0,
                  "fire-immune contact skips the IN_FIRE source");
        } else {
            ++deaths;
            CHECK(s->alive[slot] && s->health[slot] == 0.0F
                      && runtime->mobs.entity_dead[slot]
                      && runtime->mobs.entity_death_time[slot] == 0
                      && runtime->mobs.entity_hurt_time[slot] == 10
                      && runtime->mobs.entity_hurt_resistant[slot] == 20,
                  "post-move contact enters the exact unaged corpse boundary");
            JavaRandom expected_math = {initial_math};
            (void)jrand_double(&expected_math);
            CHECK(math_seed == expected_math.seed
                      && gm_mobs_event_count(&runtime->mobs) == 3,
                  "post-move contact commits Math yaw and feedback order");
        }
        CHECK(drops.n_active == 0 && next_id == initial_next,
              "doMobLoot=false contact emits no items or IDs");
    }
    for (int x = 23; x <= 25; ++x)
        for (int y = 219; y <= 222; ++y)
            for (int z = 23; z <= 25; ++z)
                gm_world_set_block(runtime->world, x, y, z, 0);
    return deaths;
}

int main(void) {
    static const struct {
        int type;
        int size;
    } cases[] = {
        {EW_TYPE_ZOMBIE, 1}, {EW_TYPE_ZOMBIE_VILLAGER, 1},
        {EW_TYPE_PIGMAN, 1}, {EW_TYPE_SKELETON, 1},
        {EW_TYPE_WITHER_SKELETON, 1}, {EW_TYPE_CREEPER, 1},
        {EW_TYPE_SPIDER, 1}, {EW_TYPE_CAVE_SPIDER, 1},
        {EW_TYPE_ENDERMAN, 1}, {EW_TYPE_BLAZE, 1},
        {EW_TYPE_GHAST, 1}, {EW_TYPE_SILVERFISH, 1},
        {EW_TYPE_VILLAGER, 1}, {EW_TYPE_SLIME, 1},
        {EW_TYPE_SLIME, 2}, {EW_TYPE_SLIME, 4},
        {EW_TYPE_MAGMA, 1}, {EW_TYPE_MAGMA, 2}, {EW_TYPE_MAGMA, 4},
    };
    int types[sizeof cases / sizeof cases[0]];
    int sizes[sizeof cases / sizeof cases[0]];
    for (int index = 0; index < (int)(sizeof cases / sizeof cases[0]); ++index) {
        types[index] = cases[index].type;
        sizes[index] = cases[index].size;
    }
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
    for (int y = 219; y <= 226; ++y)
        gm_world_set_block(runtime.world, 24, y, 24, 9);

    uint64_t enderman_drown_seed = enderman_nonteleport_seed(48, 0);
    uint64_t enderman_source_seed = enderman_nonteleport_seed(0, 1);
    CHECK(enderman_drown_seed != UINT64_MAX
              && enderman_source_seed != UINT64_MAX,
          "Enderman no-teleport fixture seeds exist");
    int completed = 0;
    for (int index = 0; index < (int)(sizeof cases / sizeof cases[0]); ++index) {
        uint64_t seed = cases[index].type == EW_TYPE_ENDERMAN
            ? enderman_drown_seed : (uint64_t)(500 + index * 37);
        completed += run_drown_case(
            &runtime, cases[index].type, cases[index].size, index, seed);
    }
    for (int scenario = 0; scenario < 3; ++scenario)
        for (int index = 0;
                index < (int)(sizeof cases / sizeof cases[0]); ++index) {
            uint64_t seed = cases[index].type == EW_TYPE_ENDERMAN
                    && scenario != 1
                ? enderman_source_seed
                : (uint64_t)(2500 + scenario * 1000 + index * 41);
            completed += run_base_source_case(
                &runtime, cases[index].type, cases[index].size,
                index, scenario, seed);
        }
    run_capacity_case(&runtime);
    int fire_contact_deaths = run_contact_matrix(
        &runtime, types, sizes,
        (int)(sizeof cases / sizeof cases[0]), 51, 1);
    int cactus_deaths = run_contact_matrix(
        &runtime, types, sizes,
        (int)(sizeof cases / sizeof cases[0]), 81, 0);

    gm_runtime_destroy(&runtime);
    if (fail) return 1;
    printf("hostile_environment_death_live: PASS deaths=%d "
           "drown=19 on_fire=12+7immune lava=12+7immune in_wall=19 "
           "in_fire=%d+7immune cactus=%d "
           "loot=exact rng=exact corpse=exact capacity_atomic=2\n",
           completed, fire_contact_deaths, cactus_deaths);
    return 0;
}
