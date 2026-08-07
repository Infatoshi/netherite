#include "game/runtime.h"

#include <stdio.h>
#include <string.h>

static int fail;
#define CHECK(C, M) do { \
    if (!(C)) { fprintf(stderr, "FAIL: %s\n", M); fail = 1; } \
} while (0)

static EwStore *store(GmMobLive *m) { return m->current ? &m->b : &m->a; }

static int slot_for(const GmMobLive *m, int eid) {
    const EwStore *s = m->current ? &m->b : &m->a;
    for (int i = 1; i < EW_MAX_ENTITIES; ++i)
        if (s->alive[i] && s->id[i] == eid) return i;
    return -1;
}

static int xp_slot_for(const GmMobLive *m, int eid) {
    for (int slot = 0; slot < GM_XP_ORBS; ++slot)
        if (!m->xp_orbs[slot].dead && m->xp_orbs[slot].eid == eid)
            return slot;
    return -1;
}

static int setup(GmMobLive *m, double distance) {
    gm_mobs_init(m, 123);
    if (gm_mobs_spawn_exact(m, EW_TYPE_SHEEP, 100, 4, 70, 8,
            0, 0, 0, 0, 8, 1, 0, 0, 0) < 0
            || gm_mobs_spawn_exact(m, EW_TYPE_SHEEP, 101, 4 + distance, 70, 8,
                0, 0, 0, 0, 8, 1, 0, 0, 0) < 0)
        return 0;
    return gm_mobs_set_sheep_state(m, 100, 14, 0)
        && gm_mobs_set_sheep_state(m, 101, 11, 0)
        && gm_mobs_set_sheep_breeding_state(m, 100, 600, 0, 0, 1)
        && gm_mobs_set_sheep_breeding_state(m, 101, 600, 0, 0, 1)
        && gm_mobs_set_entity_random_state(m, 100, 0x1234, 0, 0.0);
}

static GmSheepMateResult update(GmMobLive *m, int *delay, int cancelled,
        int child_present, uint64_t *world, uint64_t *math, int *next_id,
        int loot) {
    GmSheepMateResult out;
    memset(&out, 0xa5, sizeof out);
    (void)gm_mobs_sheep_mate_update(m, 100, 101, delay, cancelled,
        child_present, world, math, next_id, loot, &out);
    return out;
}

static int sheep_count(const GmMobLive *m) {
    const EwStore *s = m->current ? &m->b : &m->a;
    int count = 0;
    for (int slot = 1; slot < EW_MAX_ENTITIES; ++slot)
        if (s->alive[slot] && s->type[slot] == EW_TYPE_SHEEP) ++count;
    return count;
}

static int type_count(const GmMobLive *m, int type) {
    const EwStore *s = m->current ? &m->b : &m->a;
    int count = 0;
    for (int slot = 1; slot < EW_MAX_ENTITIES; ++slot)
        if (s->alive[slot] && s->type[slot] == type) ++count;
    return count;
}

static uint64_t advance_math_doubles(uint64_t seed48, int count) {
    const uint64_t mask = (UINT64_C(1) << 48) - 1;
    for (int i = 0; i < count * 2; ++i)
        seed48 = (seed48 * UINT64_C(0x5deece66d) + 11) & mask;
    return seed48;
}

static uint64_t advance_java_steps(uint64_t seed48, int count) {
    const uint64_t mask = (UINT64_C(1) << 48) - 1;
    for (int i = 0; i < count; ++i)
        seed48 = (seed48 * UINT64_C(0x5deece66d) + 11) & mask;
    return seed48;
}

static int fill_living_capacity(GmMobLive *m) {
    for (int i = 0; i < GM_MOB_CAPACITY - 2; ++i)
        if (gm_mobs_spawn_exact(
                m, EW_TYPE_COW, 10000 + i, 32 + i, 70, 32,
                0, 0, 0, 0, 10, 1, 0, 0, 0) < 0)
            return 0;
    return 1;
}

static int fill_xp_capacity(GmMobLive *m) {
    for (int i = 0; i < GM_XP_ORBS; ++i)
        if (!gm_mobs_spawn_xp_exact(
                m, 32 + i, 70, 32, 0, 0, 0, 1,
                1000 + i, 0, 0, 0, 0))
            return 0;
    return 1;
}

static int setup_animal_pair(GmMobLive *m, int first_type, int second_type) {
    float first_health = first_type == EW_TYPE_CHICKEN ? 4.0F : 10.0F;
    float second_health = second_type == EW_TYPE_CHICKEN ? 4.0F : 10.0F;
    gm_mobs_init(m, 321);
    return gm_mobs_spawn_exact(
            m, first_type, 200, 4, 70, 8,
            0, 0, 0, 0, first_health, 1, 0, 0, 0) >= 0
        && gm_mobs_spawn_exact(
            m, second_type, 201, 6, 70, 8,
            0, 0, 0, 0, second_health, 1, 0, 0, 0) >= 0
        && gm_mobs_set_animal_breeding_state(m, 200, 600, 0, 0, 1)
        && gm_mobs_set_animal_breeding_state(m, 201, 600, 0, 0, 1)
        && gm_mobs_set_entity_random_state(m, 200, 0x1234, 0, 0.0);
}

static void test_non_sheep_direct_birth(void) {
    static const int types[] = {
        EW_TYPE_COW, EW_TYPE_PIG, EW_TYPE_CHICKEN
    };
    for (size_t index = 0; index < sizeof types / sizeof types[0]; ++index) {
        GmMobLive mobs;
        uint64_t world = UINT64_C(0x123456789abc);
        uint64_t math = UINT64_C(0x23456789abcd);
        uint64_t expected_math = advance_math_doubles(math, 3);
        int next_id = 8000 + (int)index * 10;
        int delay = 59;
        uint64_t child_seed = UINT64_C(0x3456789abcde) + index;
        int child_egg_timer = 7000 + (int)index;
        GmAnimalMateResult born;
        CHECK(setup_animal_pair(&mobs, types[index], types[index]),
              "non-sheep direct pair initializes");
        CHECK(gm_mobs_set_next_animal_child_state(
                  &mobs, child_seed, 1, 0.125, child_egg_timer),
              "non-sheep newborn continuation state initializes");
        int result = gm_mobs_animal_mate_update(
            &mobs, 200, 201, &delay, 0, 1,
            &world, &math, &next_id, 0, &born);
        int child = slot_for(&mobs, born.child_eid);
        const EwStore *s = store(&mobs);
        CHECK(result == GM_SHEEP_MATE_BORN && born.result == result
                  && born.child_type == types[index]
                  && born.child_fleece == -1 && child >= 0
                  && s->type[child] == types[index]
                  && mobs.growing_age[child] == -24000
                  && mobs.entity_random[child].random.seed == child_seed
                  && mobs.entity_random[child].have_next_next_gaussian
                  && mobs.entity_random[child].next_next_gaussian == 0.125
                  && mobs.chicken_time_until_next_egg[child]
                      == (types[index] == EW_TYPE_CHICKEN
                          ? child_egg_timer : 0)
                  && mobs.animal_child_state_count == 0
                  && world == UINT64_C(0x123456789abc)
                  && math == expected_math && next_id == born.child_eid + 1,
              "non-sheep birth preserves type, state, and shared cursors");
        CHECK(gm_mobs_particle_batch_count(&mobs) == 1,
              "non-sheep birth emits one seven-heart batch");
    }

    GmMobLive mixed;
    uint64_t world = 1, math = 2;
    int next_id = 9000, delay = 59;
    GmAnimalMateResult out;
    CHECK(setup_animal_pair(&mixed, EW_TYPE_COW, EW_TYPE_PIG),
          "mixed-species direct pair initializes");
    CHECK(gm_mobs_animal_mate_update(
              &mixed, 200, 201, &delay, 0, 1,
              &world, &math, &next_id, 1, &out) == GM_SHEEP_MATE_NONE
              && delay == 0 && next_id == 9000,
          "mixed runtime classes cannot mate");
    delay = 59;
    CHECK(gm_mobs_sheep_mate_update(
              &mixed, 200, 201, &delay, 0, 1,
              &world, &math, &next_id, 1, &out) == GM_SHEEP_MATE_NONE
              && delay == 0 && next_id == 9000,
          "sheep compatibility API rejects non-sheep parents");
}

static void test_capacity_fallbacks(void) {
    GmMobLive mobs;
    uint64_t world, math;
    int next_id, delay;
    GmSheepMateResult born;

    CHECK(setup(&mobs, 0.0) && fill_living_capacity(&mobs),
          "full living store initializes");
    world = 11;
    math = 12;
    uint64_t expected_math = advance_math_doubles(math, 7);
    next_id = 20000;
    delay = 59;
    born = update(&mobs, &delay, 0, 1, &world, &math, &next_id, 1);
    int first = slot_for(&mobs, 100), second = slot_for(&mobs, 101);
    CHECK(born.result == GM_SHEEP_MATE_BORN
              && born.child_eid == 20000 && born.child_slot == -1
              && mobs.sheep_birth_dropped == 1
              && sheep_count(&mobs) == 2
              && born.xp_eid == 20001 && born.xp_slot >= 0
              && next_id == 20002 && mobs.next_orb_id == 20002
              && math == expected_math
              && mobs.xp_orbs[born.xp_slot].eid == 20001
              && mobs.growing_age[first] == 6000
              && mobs.growing_age[second] == 6000
              && gm_mobs_particle_batch_count(&mobs) == 1,
          "full living store drops only the child and preserves birth effects");

    CHECK(setup(&mobs, 0.0) && fill_xp_capacity(&mobs),
          "full XP store initializes");
    int xp_eids[GM_XP_ORBS];
    for (int i = 0; i < GM_XP_ORBS; ++i)
        xp_eids[i] = mobs.xp_orbs[i].eid;
    world = 13;
    math = 14;
    expected_math = advance_math_doubles(math, 7);
    next_id = 30000;
    delay = 59;
    born = update(&mobs, &delay, 0, 1, &world, &math, &next_id, 1);
    CHECK(born.result == GM_SHEEP_MATE_BORN
              && born.child_eid == 30000 && born.child_slot >= 0
              && born.xp_eid == 30001 && born.xp_slot == -1
              && born.xp_value >= 1 && born.xp_value <= 7
              && mobs.sheep_breed_xp_dropped == 1
              && next_id == 30002 && mobs.next_orb_id == 30002
              && math == expected_math,
          "full XP store consumes exact spawn state without overwriting an orb");
    for (int i = 0; i < GM_XP_ORBS; ++i)
        CHECK(mobs.xp_orbs[i].eid == xp_eids[i],
              "full XP store preserves every existing orb slot");
}

static void test_particle_batch_capacity(void) {
    GmMobLive mobs;
    uint64_t world = 17, math = 18;
    int next_id = 50000;
    CHECK(setup(&mobs, 0.0), "particle ring pair initializes");
    for (int i = 0; i < GM_MOB_PARTICLE_BATCH_CAPACITY + 1; ++i) {
        int delay = 59;
        GmSheepMateResult born = update(
            &mobs, &delay, 0, 1, &world, &math, &next_id, 0);
        CHECK(born.result == GM_SHEEP_MATE_BORN && born.child_slot >= 0,
              "particle ring fill birth succeeds");
        if (born.child_slot < 0) break;
        mobs.a.alive[born.child_slot] = 0;
        mobs.b.alive[born.child_slot] = 0;
        mobs.a.type[born.child_slot] = EW_TYPE_NONE;
        mobs.b.type[born.child_slot] = EW_TYPE_NONE;
        CHECK(gm_mobs_set_growing_age(&mobs, 100, 0)
                  && gm_mobs_set_growing_age(&mobs, 101, 0)
                  && gm_mobs_set_sheep_breeding_state(
                      &mobs, 100, 600, 0, 0, 1)
                  && gm_mobs_set_sheep_breeding_state(
                      &mobs, 101, 600, 0, 0, 1),
              "particle ring parents rearm");
    }
    GmMobParticleBatch oldest, newest;
    CHECK(gm_mobs_particle_batch_count(&mobs)
              == GM_MOB_PARTICLE_BATCH_CAPACITY
              && mobs.particle_batch_dropped == 1
              && gm_mobs_particle_batch_get(&mobs, 0, &oldest)
              && gm_mobs_particle_batch_get(
                  &mobs, GM_MOB_PARTICLE_BATCH_CAPACITY - 1, &newest)
              && oldest.seq == 1
              && newest.seq == GM_MOB_PARTICLE_BATCH_CAPACITY,
          "particle batch ring overwrites exactly the oldest birth batch");
}

static void test_reused_child_slot(void) {
    GmMobLive mobs;
    uint64_t world = 19, math = 20;
    int next_id = 60000, delay = 59;
    CHECK(setup(&mobs, 0.0), "reused child-slot pair initializes");
    int stale = gm_mobs_spawn_sized(
        &mobs, EW_TYPE_MAGMA, 16, 70, 16, 4);
    CHECK(stale >= 0, "large stale mob slot initializes");
    if (stale < 0) return;
    mobs.a.alive[stale] = mobs.b.alive[stale] = 0;
    mobs.a.type[stale] = mobs.b.type[stale] = EW_TYPE_NONE;
    GmSheepMateResult born = update(
        &mobs, &delay, 0, 1, &world, &math, &next_id, 0);
    CHECK(born.result == GM_SHEEP_MATE_BORN
              && born.child_slot == stale && mobs.size[stale] == 1,
          "newborn sheep clears a reused large-mob size");
}

static int setup_ordinary_pair(
        GmRuntime *r, int first_eid, double x, double y, double z,
        double distance, int *first_slot, int *second_slot) {
    gm_mobs_init(&r->mobs, 0);
    memset(&r->entities, 0, sizeof r->entities);
    r->mobs.active_dimension = r->dimension;
    r->mobs.next_id = first_eid;
    r->next_entity_id = 0;
    r->mobs_enabled = 1;
    r->controlled_mobs_enabled = 0;
    r->do_mob_loot = 1;
    *first_slot = gm_mobs_spawn(
        &r->mobs, EW_TYPE_SHEEP, x, y, z);
    *second_slot = gm_mobs_spawn(
        &r->mobs, EW_TYPE_SHEEP, x + distance, y, z);
    if (*first_slot < 0 || *second_slot < 0) return 0;
    return gm_mobs_set_sheep_state(&r->mobs, first_eid, 14, 0)
        && gm_mobs_set_sheep_state(&r->mobs, first_eid + 1, 0, 0)
        && gm_mobs_set_sheep_breeding_state(
            &r->mobs, first_eid, 600, 0, 0, 1)
        && gm_mobs_set_sheep_breeding_state(
            &r->mobs, first_eid + 1, 600, 0, 0, 1);
}

static void test_non_sheep_live_scheduler(void) {
    static const int types[] = {
        EW_TYPE_COW, EW_TYPE_PIG, EW_TYPE_CHICKEN
    };
    GmConfig cfg;
    GmRuntime runtime;
    GmAction idle;
    char err[256];
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.view_distance = 1;
    cfg.mobs = 0;
    CHECK(gm_runtime_init(&runtime, &cfg, err, sizeof err),
          "non-sheep live mating runtime initializes");
    if (fail) return;
    gm_runtime_set_pose(&runtime, 8.5, 64.0, 8.5, 0.0F, 0.0F);

    for (size_t index = 0; index < sizeof types / sizeof types[0]; ++index) {
        int base_eid = 7200 + (int)index * 10;
        uint64_t child_seed = UINT64_C(0x56789abcdef0) + index;
        int child_egg_timer = 7000 + (int)index;
        gm_mobs_init(&runtime.mobs, 0);
        runtime.mobs.active_dimension = runtime.dimension;
        runtime.mobs.next_id = base_eid;
        int first = gm_mobs_spawn(
            &runtime.mobs, types[index], 8.0, 220.0, 8.0);
        int second = gm_mobs_spawn(
            &runtime.mobs, types[index], 8.25, 220.0, 8.0);
        runtime.next_entity_id = base_eid + 2;
        runtime.world_random_seed48 = UINT64_C(0x123456789abc);
        runtime.math_random_seed48 = UINT64_C(0x456789abcdef);
        runtime.mobs_enabled = 1;
        runtime.controlled_mobs_enabled = 0;
        runtime.do_mob_loot = 1;
        runtime.mobs.tick = 1;
        CHECK(first >= 0 && second >= 0
                  && gm_mobs_set_animal_breeding_state(
                      &runtime.mobs, base_eid, 600, 0, 0, 0)
                  && gm_mobs_set_animal_breeding_state(
                      &runtime.mobs, base_eid + 1, 600, 0, 0, 0)
                  && gm_mobs_set_entity_random_state(
                      &runtime.mobs, base_eid,
                      UINT64_C(0x23456789abcd), 0, 0.0)
                  && gm_mobs_set_entity_random_state(
                      &runtime.mobs, base_eid + 1,
                      UINT64_C(0x3456789abcde), 0, 0.0)
                  && (types[index] != EW_TYPE_CHICKEN
                      || (gm_mobs_set_chicken_state(
                              &runtime.mobs, base_eid, 7000,
                              0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0)
                          && gm_mobs_set_chicken_state(
                              &runtime.mobs, base_eid + 1, 8000,
                              0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0)))
                  && gm_mobs_set_next_animal_child_state(
                      &runtime.mobs, child_seed, 1, 0.125,
                      child_egg_timer),
              "non-sheep live pair and child state initialize");
        if (first < 0 || second < 0) continue;
        runtime.mobs.a.on_ground[first] = 0;
        runtime.mobs.a.on_ground[second] = 0;
        runtime.mobs.b.on_ground[first] = 0;
        runtime.mobs.b.on_ground[second] = 0;
        runtime.mobs.sheep_mate_active[first] = 1;
        runtime.mobs.sheep_mate_slot[first] = second;
        runtime.mobs.sheep_mate_delay[first] = 59;

        gm_runtime_tick(&runtime, idle);
        int child = slot_for(&runtime.mobs, base_eid + 2);
        int xp = xp_slot_for(&runtime.mobs, base_eid + 3);
        CHECK(child >= 0 && xp >= 0
                  && type_count(&runtime.mobs, types[index]) == 3,
              "non-sheep live birth appends child and XP");
        if (child >= 0) {
            CHECK(runtime.mobs.growing_age[first] == 5999
                      && runtime.mobs.growing_age[second] == 5999
                      && runtime.mobs.growing_age[child] == -23999,
                  "non-sheep live birth advances animal ages");
            CHECK(runtime.mobs.entity_ticks_existed[child] == 1,
                  "non-sheep newborn receives base entity tick");
            CHECK(runtime.mobs.entity_age[child] == 1,
                  "non-sheep newborn receives despawn-age tick");
            CHECK(runtime.mobs.entity_living_sound_time[child] == 1,
                  "non-sheep newborn receives ambient-sound tick");
            CHECK(runtime.mobs.sheep_ai_tick_count[child] == 1,
                  "non-sheep newborn receives AI scheduler tick");
            CHECK(runtime.mobs.entity_random[child].random.seed
                          == advance_java_steps(child_seed, 4)
                      && runtime.mobs.entity_random[child]
                          .have_next_next_gaussian
                      && runtime.mobs.entity_random[child]
                          .next_next_gaussian == 0.125,
                  "non-sheep newborn preserves and advances private RNG");
            CHECK(runtime.mobs.chicken_time_until_next_egg[child]
                          == (types[index] == EW_TYPE_CHICKEN
                              ? child_egg_timer : 0),
                  "non-sheep newborn preserves chicken egg timer");
            if (types[index] == EW_TYPE_CHICKEN) {
                CHECK(runtime.mobs.chicken_time_until_next_egg[first] == 6999,
                      "initiating adult chicken advances egg timer");
                CHECK(runtime.mobs.chicken_time_until_next_egg[second] == 7999,
                      "mate adult chicken advances egg timer");
                CHECK(runtime.mobs.chicken_old_flap[child] == 0.0F
                          && runtime.mobs.chicken_old_flap_speed[child] == 0.0F
                          && runtime.mobs.chicken_dest_pos[child] == 1.0F
                          && runtime.mobs.chicken_wing_rot_delta[child] == 0.9F
                          && runtime.mobs.chicken_wing_rotation[child] == 1.8F,
                      "chicken newborn receives post-move flap update");
            }
        }
        CHECK(runtime.mobs.animal_child_state_count == 0
                  && runtime.next_entity_id == base_eid + 4
                  && runtime.mobs.next_id == base_eid + 4,
              "non-sheep live birth consumes FIFO and event IDs");
        static const int order_offsets[] = {0, 1, 2, 3};
        CHECK(runtime.mobs.tick_update_order_count == 4,
              "non-sheep live birth has four entity updates");
        for (int order = 0; order < 4
                && runtime.mobs.tick_update_order_count == 4; ++order)
            CHECK(runtime.mobs.tick_update_order[order]
                      == base_eid + order_offsets[order],
                  "non-sheep live birth preserves child/XP append order");
    }
    gm_runtime_destroy(&runtime);
}

static void test_chicken_egg_threshold(void) {
    for (int full = 0; full <= 1; ++full) {
        GmConfig cfg;
        GmRuntime runtime;
        GmAction idle;
        char err[256];
        int base_eid = 7350 + full * 20;
        uint64_t math_seed = UINT64_C(0x456789abcdef) + (uint64_t)full;
        memset(&idle, 0, sizeof idle);
        idle.hotbar_sel = -1;
        gm_config_defaults(&cfg);
        cfg.world = GM_WORLD_SUPERFLAT;
        cfg.view_distance = 1;
        cfg.mobs = 0;
        CHECK(gm_runtime_init(&runtime, &cfg, err, sizeof err),
              "chicken egg threshold runtime initializes");
        if (fail) return;
        gm_runtime_set_pose(&runtime, 8.5, 64.0, 8.5, 0.0F, 0.0F);
        gm_mobs_init(&runtime.mobs, 0);
        runtime.mobs.active_dimension = runtime.dimension;
        runtime.mobs.next_id = base_eid;
        int first = gm_mobs_spawn(
            &runtime.mobs, EW_TYPE_CHICKEN, 8.0, 220.0, 8.0);
        int second = gm_mobs_spawn(
            &runtime.mobs, EW_TYPE_CHICKEN, 8.25, 220.0, 8.0);
        runtime.next_entity_id = base_eid + 2;
        runtime.world_random_seed48 = UINT64_C(0x123456789abc);
        runtime.math_random_seed48 = math_seed;
        runtime.mobs_enabled = 1;
        runtime.controlled_mobs_enabled = 0;
        runtime.do_mob_loot = 0;
        runtime.mobs.tick = 1;
        CHECK(first >= 0 && second >= 0
                  && gm_mobs_set_animal_breeding_state(
                      &runtime.mobs, base_eid, 600, 0, 0, 0)
                  && gm_mobs_set_animal_breeding_state(
                      &runtime.mobs, base_eid + 1, 600, 0, 0, 0)
                  && gm_mobs_set_entity_random_state(
                      &runtime.mobs, base_eid,
                      UINT64_C(0x23456789abcd), 0, 0.0)
                  && gm_mobs_set_entity_random_state(
                      &runtime.mobs, base_eid + 1,
                      UINT64_C(0x3456789abcde), 0, 0.0)
                  && gm_mobs_set_chicken_state(
                      &runtime.mobs, base_eid, 1,
                      0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0)
                  && gm_mobs_set_chicken_state(
                      &runtime.mobs, base_eid + 1, 8000,
                      0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0)
                  && gm_mobs_set_next_animal_child_state(
                      &runtime.mobs, UINT64_C(0x56789abcdef0),
                      0, 0.0, 7000),
              "chicken egg threshold state initializes");
        if (full) for (int slot = 0; slot < GM_LIVE_MAX; ++slot)
            CHECK(gm_live_spawn_item_exact(
                      &runtime.entities, 20000 + slot,
                      128.0, 220.0, 128.0,
                      0.0, 0.0, 0.0, 0.0F,
                      1, 1, 0, 0, 10, 1),
                  "chicken egg capacity fixture fills exact item store");
        runtime.mobs.sheep_mate_active[first] = 1;
        runtime.mobs.sheep_mate_slot[first] = second;
        runtime.mobs.sheep_mate_delay[first] = 59;

        gm_runtime_tick(&runtime, idle);
        const GmLiveEnt *egg = NULL;
        for (int slot = 0; slot < GM_LIVE_MAX; ++slot)
            if (runtime.entities.ents[slot].active
                    && runtime.entities.ents[slot].eid == base_eid + 3)
                egg = &runtime.entities.ents[slot];
        GmMobEvent event;
        CHECK(gm_mobs_event_count(&runtime.mobs) == 1
                  && gm_mobs_event_get(&runtime.mobs, 0, &event)
                  && event.kind == GM_MOB_EVENT_SOUND
                  && event.eid == base_eid
                  && event.data == GM_MOB_SOUND_CHICKEN_EGG
                  && event.volume == 1.0F,
              "chicken egg threshold emits one exact causal sound");
        CHECK(runtime.math_random_seed48
                  == advance_math_doubles(math_seed, 7)
                  && runtime.next_entity_id == base_eid + 4
                  && runtime.mobs.next_id == base_eid + 4
                  && runtime.mobs.next_orb_id == base_eid + 4,
              "chicken egg threshold consumes constructor Math and global ID");
        CHECK(runtime.mobs.chicken_time_until_next_egg[first] >= 6000
                  && runtime.mobs.chicken_time_until_next_egg[first] < 12000
                  && runtime.mobs.chicken_time_until_next_egg[second] == 7999,
              "chicken egg threshold resets only the laying parent timer");
        if (!full) {
            CHECK(egg && egg->item == 344 && egg->count == 1
                      && egg->meta == 0 && egg->age == 1
                      && egg->pickup_delay == 9 && egg->health == 5
                      && egg->lifespan == 6000,
                  "new egg item receives its same-boundary first tick");
            int next_slot = gm_mobs_spawn(
                &runtime.mobs, EW_TYPE_COW, 16.0, 220.0, 16.0);
            CHECK(next_slot >= 0
                      && store(&runtime.mobs)->id[next_slot] == base_eid + 4,
                  "post-egg living allocation cannot reuse the egg ID");
        } else {
            CHECK(!egg && runtime.entities.spawn_fail_count == 1,
                  "full exact item store records bounded egg loss");
        }
        gm_runtime_destroy(&runtime);
    }
}

static void test_ordinary_scheduler(void) {
    GmConfig cfg;
    GmRuntime runtime;
    GmAction idle;
    char err[256];
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.view_distance = 1;
    cfg.mobs = 0;
    CHECK(gm_runtime_init(&runtime, &cfg, err, sizeof err),
          "ordinary mating runtime initializes");
    if (fail) return;

    int x = 8, z = 8;
    int y = gm_world_surface_y(runtime.world, x, z) + 1;
    gm_runtime_set_pose(&runtime, x + 0.5, y, z - 4.5, 0.0F, 0.0F);
    int first, second;
    CHECK(setup_ordinary_pair(
              &runtime, 7000, x + 0.5, y, z + 0.5, 1.0,
              &first, &second),
          "ordinary loving pair initializes");
    uint64_t child_seed = UINT64_C(0x56789abcdef0);
    CHECK(gm_mobs_set_next_sheep_child_random_state(
              &runtime.mobs, child_seed, 0, 0.0),
          "ordinary newborn RNG fixture initializes");
    runtime.mobs.sheep_eat_timer[first] = 20;
    gm_runtime_tick(&runtime, idle);
    CHECK(runtime.mobs.sheep_mate_active[first]
              && runtime.mobs.sheep_mate_delay[first] == 1
              && runtime.mobs.sheep_eat_timer[first] == 0,
          "mate goal starts on setup tick and resets lower-priority grazing");
    for (int tick = 1; tick < 59; ++tick)
        gm_runtime_tick(&runtime, idle);
    CHECK(sheep_count(&runtime.mobs) == 2
              && runtime.mobs.sheep_mate_delay[first] == 59,
          "ordinary scheduler has no child before update 60");
    gm_runtime_tick(&runtime, idle);
    int child = slot_for(&runtime.mobs, 7002);
    CHECK(sheep_count(&runtime.mobs) == 3 && child >= 0
              && runtime.mobs.growing_age[first] == 5999
              && runtime.mobs.growing_age[second] == 5999
              && runtime.mobs.growing_age[child] == -23999
              && runtime.mobs.entity_ticks_existed[child] == 1
              && runtime.mobs.entity_age[child] == 0
              && runtime.mobs.entity_living_sound_time[child] == 1
              && runtime.mobs.sheep_ai_tick_count[child] == 1
              && runtime.mobs.entity_random[child].random.seed
                  == advance_java_steps(child_seed, 5)
              && !runtime.mobs.entity_random[child]
                      .have_next_next_gaussian
              && (runtime.mobs.sheep_data[child] & 15) == 6
              && runtime.next_entity_id == 7004
              && runtime.mobs.next_id == 7004,
          "ordinary update 60 creates one child and one XP with shared IDs");

    CHECK(setup_ordinary_pair(
              &runtime, 7100, x + 0.5, y, z + 0.5, 4.0,
              &first, &second),
          "ordinary reset pair initializes");
    runtime.mobs.controlled_no_ai[second] = 1;
    runtime.mobs.sheep_mate_active[first] = 1;
    runtime.mobs.sheep_mate_slot[first] = second;
    runtime.mobs.sheep_mate_delay[first] = 60;
    runtime.mobs.sheep_ai_tick_count[first] = 0;
    gm_runtime_tick(&runtime, idle);
    CHECK(!runtime.mobs.sheep_mate_active[first]
              && runtime.mobs.sheep_mate_delay[first] == 0,
          "failed continuation does not restart on the same setup tick");
    gm_runtime_tick(&runtime, idle);
    gm_runtime_tick(&runtime, idle);
    CHECK(!runtime.mobs.sheep_mate_active[first],
          "reset mate waits through non-setup ticks");
    gm_runtime_tick(&runtime, idle);
    CHECK(runtime.mobs.sheep_mate_active[first]
              && runtime.mobs.sheep_mate_delay[first] == 1,
          "reset mate may restart at the next three-tick setup boundary");

    gm_runtime_destroy(&runtime);
}

static void test_simultaneous_birth_order(void) {
    GmConfig cfg;
    GmRuntime runtime;
    GmAction idle;
    char err[256];
    const uint64_t child_a_seed = UINT64_C(0x123456789abc);
    const uint64_t child_b_seed = UINT64_C(0x23456789abcd);
    const uint64_t math_seed = UINT64_C(0x456789abcdef);
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.view_distance = 1;
    cfg.mobs = 0;
    CHECK(gm_runtime_init(&runtime, &cfg, err, sizeof err),
          "simultaneous-birth runtime initializes");
    if (fail) return;
    gm_runtime_set_pose(&runtime, 8.5, 64.0, 8.5, 0.0F, 0.0F);
    gm_mobs_init(&runtime.mobs, 0);
    runtime.mobs.active_dimension = runtime.dimension;
    runtime.mobs.next_id = 8000;
    int parent[4];
    parent[0] = gm_mobs_spawn(
        &runtime.mobs, EW_TYPE_SHEEP, 8.0, 220.0, 8.0);
    parent[1] = gm_mobs_spawn(
        &runtime.mobs, EW_TYPE_SHEEP, 8.25, 220.0, 8.0);
    parent[2] = gm_mobs_spawn(
        &runtime.mobs, EW_TYPE_SHEEP, 20.0, 220.0, 8.0);
    parent[3] = gm_mobs_spawn(
        &runtime.mobs, EW_TYPE_SHEEP, 20.25, 220.0, 8.0);
    runtime.next_entity_id = 8004;
    runtime.world_random_seed48 = UINT64_C(0x3456789abcde);
    runtime.math_random_seed48 = math_seed;
    runtime.mobs_enabled = 1;
    runtime.controlled_mobs_enabled = 0;
    runtime.do_mob_loot = 1;
    runtime.mobs.tick = 1;
    for (int index = 0; index < 4; ++index) {
        CHECK(parent[index] >= 0
                  && gm_mobs_set_sheep_state(
                      &runtime.mobs, 8000 + index,
                      index == 0 || index == 2 ? 14 : 0, 0)
                  && gm_mobs_set_sheep_breeding_state(
                      &runtime.mobs, 8000 + index, 600, 0, 0, 0)
                  && gm_mobs_set_entity_random_state(
                      &runtime.mobs, 8000 + index,
                      UINT64_C(0x111111111111) + (uint64_t)index,
                      0, 0.0),
              "simultaneous-birth parent state initializes");
        if (parent[index] >= 0) {
            runtime.mobs.a.on_ground[parent[index]] = 0;
            runtime.mobs.b.on_ground[parent[index]] = 0;
        }
    }
    CHECK(gm_mobs_set_next_sheep_child_random_state(
              &runtime.mobs, child_a_seed, 0, 0.0)
              && gm_mobs_queue_sheep_child_random_state(
                  &runtime.mobs, child_b_seed, 0, 0.0),
          "simultaneous-birth child RNG FIFO initializes");
    runtime.mobs.sheep_mate_active[parent[0]] = 1;
    runtime.mobs.sheep_mate_slot[parent[0]] = parent[1];
    runtime.mobs.sheep_mate_delay[parent[0]] = 59;
    runtime.mobs.sheep_mate_active[parent[2]] = 1;
    runtime.mobs.sheep_mate_slot[parent[2]] = parent[3];
    runtime.mobs.sheep_mate_delay[parent[2]] = 59;

    gm_runtime_tick(&runtime, idle);
    int child_a = slot_for(&runtime.mobs, 8004);
    int child_b = slot_for(&runtime.mobs, 8006);
    int xp_a = xp_slot_for(&runtime.mobs, 8005);
    int xp_b = xp_slot_for(&runtime.mobs, 8007);
    CHECK(child_a >= 0 && child_b >= 0 && sheep_count(&runtime.mobs) == 6
              && runtime.mobs.entity_random[child_a].random.seed
                  == advance_java_steps(child_a_seed, 5)
              && runtime.mobs.entity_random[child_b].random.seed
                  == advance_java_steps(child_b_seed, 5)
              && runtime.mobs.animal_child_state_count == 0,
          "simultaneous births consume pinned child RNG states FIFO");
    CHECK(xp_a >= 0 && xp_b >= 0
              && runtime.mobs.xp_orbs[xp_a].xpColor == 1
              && runtime.mobs.xp_orbs[xp_b].xpColor == 1
              && runtime.mobs.xp_orbs[xp_a].xpOrbAge == 1
              && runtime.mobs.xp_orbs[xp_b].xpOrbAge == 1
              && runtime.mobs.xp_orbs[xp_a].health == 5
              && runtime.mobs.xp_orbs[xp_b].health == 5
              && runtime.next_entity_id == 8008
              && runtime.mobs.next_id == 8008
              && runtime.math_random_seed48
                  == advance_math_doubles(math_seed, 14),
          "simultaneous births preserve both XP states and shared cursors");
    static const int expected_order[] = {
        8000, 8001, 8002, 8003, 8004, 8005, 8006, 8007
    };
    CHECK(runtime.mobs.tick_update_order_count
              == (int)(sizeof expected_order / sizeof expected_order[0]),
          "simultaneous-birth update order has every parent and append");
    for (int index = 0;
            index < runtime.mobs.tick_update_order_count
                && index < (int)(sizeof expected_order / sizeof expected_order[0]);
            ++index)
        CHECK(runtime.mobs.tick_update_order[index] == expected_order[index],
              "simultaneous births preserve child/XP dynamic append order");
    gm_runtime_destroy(&runtime);
}

static void test_preexisting_xp_slot_reuse_order(void) {
    GmConfig cfg;
    GmRuntime runtime;
    GmAction idle;
    char err[256];
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.view_distance = 1;
    cfg.mobs = 0;
    CHECK(gm_runtime_init(&runtime, &cfg, err, sizeof err),
          "preexisting-XP runtime initializes");
    if (fail) return;
    gm_runtime_set_pose(&runtime, 8.5, 64.0, 8.5, 0.0F, 0.0F);
    gm_mobs_init(&runtime.mobs, 0);
    runtime.mobs.active_dimension = runtime.dimension;
    CHECK(gm_mobs_spawn_xp_exact(
              &runtime.mobs, 4.0, 220.0, 8.0,
              0.0, 0.0, 0.0, 5, 8100, 5999, 5, 20, 0),
          "preexisting expiring XP initializes before parents");
    int old_xp_slot = xp_slot_for(&runtime.mobs, 8100);
    runtime.mobs.next_id = 8101;
    int first = gm_mobs_spawn(
        &runtime.mobs, EW_TYPE_SHEEP, 8.0, 220.0, 8.0);
    int second = gm_mobs_spawn(
        &runtime.mobs, EW_TYPE_SHEEP, 8.25, 220.0, 8.0);
    CHECK(first >= 0 && second >= 0
              && gm_mobs_set_sheep_state(&runtime.mobs, 8101, 14, 0)
              && gm_mobs_set_sheep_state(&runtime.mobs, 8102, 0, 0)
              && gm_mobs_set_sheep_breeding_state(
                  &runtime.mobs, 8101, 600, 0, 0, 0)
              && gm_mobs_set_sheep_breeding_state(
                  &runtime.mobs, 8102, 600, 0, 0, 0)
              && gm_mobs_set_entity_random_state(
                  &runtime.mobs, 8101, UINT64_C(0x111111111111), 0, 0.0)
              && gm_mobs_set_entity_random_state(
                  &runtime.mobs, 8102, UINT64_C(0x222222222222), 0, 0.0)
              && gm_mobs_set_next_sheep_child_random_state(
                  &runtime.mobs, UINT64_C(0x333333333333), 0, 0.0),
          "preexisting-XP mating pair initializes");
    if (first >= 0 && second >= 0) {
        runtime.mobs.a.on_ground[first] = 0;
        runtime.mobs.b.on_ground[first] = 0;
        runtime.mobs.a.on_ground[second] = 0;
        runtime.mobs.b.on_ground[second] = 0;
        runtime.mobs.sheep_mate_active[first] = 1;
        runtime.mobs.sheep_mate_slot[first] = second;
        runtime.mobs.sheep_mate_delay[first] = 59;
    }
    runtime.next_entity_id = 8103;
    runtime.world_random_seed48 = UINT64_C(0x444444444444);
    runtime.math_random_seed48 = UINT64_C(0x555555555555);
    runtime.mobs_enabled = 1;
    runtime.controlled_mobs_enabled = 0;
    runtime.do_mob_loot = 1;
    runtime.mobs.tick = 1;

    gm_runtime_tick(&runtime, idle);
    int birth_xp_slot = xp_slot_for(&runtime.mobs, 8104);
    static const int expected_updates[] = {8100, 8101, 8102, 8103, 8104};
    CHECK(old_xp_slot >= 0 && birth_xp_slot == old_xp_slot
              && xp_slot_for(&runtime.mobs, 8100) < 0,
          "birth XP reuses the expired preexisting XP slot");
    CHECK(runtime.mobs.tick_update_order_count
              == (int)(sizeof expected_updates / sizeof expected_updates[0]),
          "slot-reuse update trace has old XP, parents, child and birth XP");
    for (int index = 0;
            index < runtime.mobs.tick_update_order_count
                && index < (int)(sizeof expected_updates / sizeof expected_updates[0]);
            ++index)
        CHECK(runtime.mobs.tick_update_order[index] == expected_updates[index],
              "persistent dispatch preserves old-XP and birth append order");
    static const int expected_loaded[] = {8101, 8102, 8103, 8104};
    static const int expected_kinds[] = {
        GM_MOB_LOADED_LIVING, GM_MOB_LOADED_LIVING,
        GM_MOB_LOADED_LIVING, GM_MOB_LOADED_XP
    };
    CHECK(gm_mobs_loaded_order_count(&runtime.mobs)
              == (int)(sizeof expected_loaded / sizeof expected_loaded[0]),
          "expired XP leaves one compact persistent-order entry");
    for (int index = 0;
            index < gm_mobs_loaded_order_count(&runtime.mobs)
                && index < (int)(sizeof expected_loaded / sizeof expected_loaded[0]);
            ++index) {
        int eid = 0, kind = 0;
        CHECK(gm_mobs_loaded_order_get(
                  &runtime.mobs, index, &eid, &kind)
                  && eid == expected_loaded[index]
                  && kind == expected_kinds[index],
              "generation guards remove stale reused-slot references");
    }
    gm_runtime_destroy(&runtime);
}

static void test_preexisting_living_slot_reuse_order(void) {
    GmConfig cfg;
    GmRuntime runtime;
    GmAction idle;
    GmMobTerminalParticles terminal;
    char err[256];
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.view_distance = 1;
    cfg.mobs = 0;
    CHECK(gm_runtime_init(&runtime, &cfg, err, sizeof err),
          "preexisting-living runtime initializes");
    if (fail) return;
    gm_runtime_set_pose(&runtime, 8.5, 64.0, 8.5, 0.0F, 0.0F);
    gm_mobs_init(&runtime.mobs, 0);
    runtime.mobs.active_dimension = runtime.dimension;
    int dying = gm_mobs_spawn_exact(
        &runtime.mobs, EW_TYPE_COW, 8300,
        -8.0, 220.0, 8.0, 0.0, 0.0, 0.0,
        0.0F, 0.0F, 1, 0, 19, 0);
    runtime.mobs.next_id = 8301;
    int first = gm_mobs_spawn(
        &runtime.mobs, EW_TYPE_SHEEP, 8.0, 220.0, 8.0);
    int second = gm_mobs_spawn(
        &runtime.mobs, EW_TYPE_SHEEP, 8.25, 220.0, 8.0);
    CHECK(dying >= 0 && first >= 0 && second >= 0
              && gm_mobs_set_entity_random_state(
                  &runtime.mobs, 8300,
                  UINT64_C(0x13579bdf2468), 0, 0.0)
              && gm_mobs_set_sheep_state(&runtime.mobs, 8301, 14, 0)
              && gm_mobs_set_sheep_state(&runtime.mobs, 8302, 0, 0)
              && gm_mobs_set_sheep_breeding_state(
                  &runtime.mobs, 8301, 600, 0, 0, 0)
              && gm_mobs_set_sheep_breeding_state(
                  &runtime.mobs, 8302, 600, 0, 0, 0)
              && gm_mobs_set_entity_random_state(
                  &runtime.mobs, 8301, UINT64_C(0x111111111111), 0, 0.0)
              && gm_mobs_set_entity_random_state(
                  &runtime.mobs, 8302, UINT64_C(0x222222222222), 0, 0.0)
              && gm_mobs_set_next_sheep_child_random_state(
                  &runtime.mobs, UINT64_C(0x333333333333), 0, 0.0),
          "dying cow and later mating pair initialize");
    if (first >= 0 && second >= 0) {
        runtime.mobs.a.on_ground[first] = 0;
        runtime.mobs.b.on_ground[first] = 0;
        runtime.mobs.a.on_ground[second] = 0;
        runtime.mobs.b.on_ground[second] = 0;
        runtime.mobs.sheep_mate_active[first] = 1;
        runtime.mobs.sheep_mate_slot[first] = second;
        runtime.mobs.sheep_mate_delay[first] = 59;
    }
    runtime.next_entity_id = 8303;
    runtime.world_random_seed48 = UINT64_C(0x444444444444);
    runtime.math_random_seed48 = UINT64_C(0x555555555555);
    runtime.mobs_enabled = 1;
    runtime.controlled_mobs_enabled = 0;
    runtime.do_mob_loot = 0;
    runtime.mobs.tick = 1;

    gm_runtime_tick(&runtime, idle);
    int child = slot_for(&runtime.mobs, 8303);
    static const int expected_updates[] = {8300, 8301, 8302, 8303};
    static const int expected_loaded[] = {8301, 8302, 8303};
    CHECK(dying >= 0 && child == dying && slot_for(&runtime.mobs, 8300) < 0
              && sheep_count(&runtime.mobs) == 3,
          "newborn reuses the terminal living slot without the old entity");
    CHECK(runtime.mobs.tick_update_order_count
              == (int)(sizeof expected_updates / sizeof expected_updates[0]),
          "living-slot reuse trace has dying cow, parents and child");
    for (int index = 0;
            index < (int)(sizeof expected_updates / sizeof expected_updates[0]);
            ++index)
        CHECK(runtime.mobs.tick_update_order[index] == expected_updates[index],
              "living-slot reuse preserves dispatch append order");
    CHECK(gm_mobs_loaded_order_count(&runtime.mobs)
              == (int)(sizeof expected_loaded / sizeof expected_loaded[0]),
          "terminal living reference is compacted after slot reuse");
    for (int index = 0;
            index < (int)(sizeof expected_loaded / sizeof expected_loaded[0]);
            ++index) {
        int eid = 0, kind = 0;
        CHECK(gm_mobs_loaded_order_get(
                  &runtime.mobs, index, &eid, &kind)
                  && eid == expected_loaded[index]
                  && kind == GM_MOB_LOADED_LIVING,
              "reused living slot retains only its new generation");
    }
    CHECK(gm_mobs_terminal_particle_count(&runtime.mobs) == 1
              && gm_mobs_terminal_particle_get(
                  &runtime.mobs, 0, &terminal)
              && terminal.eid == 8300 && terminal.particle_id == 0,
          "terminal living update remains complete before slot reuse");
    gm_runtime_destroy(&runtime);
}

static void test_persistent_order_two_ticks(int controlled) {
    GmConfig cfg;
    GmRuntime runtime;
    GmAction idle;
    char err[256];
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.view_distance = 1;
    cfg.mobs = 0;
    CHECK(gm_runtime_init(&runtime, &cfg, err, sizeof err),
          "two-tick persistent-order runtime initializes");
    if (fail) return;
    gm_runtime_set_pose(&runtime, 8.5, 64.0, 8.5, 0.0F, 0.0F);
    gm_mobs_init(&runtime.mobs, 0);
    runtime.mobs.active_dimension = runtime.dimension;
    CHECK(gm_mobs_spawn_xp_exact(
              &runtime.mobs, 4.0, 220.0, 8.0,
              0.0, 0.0, 0.0, 5, 8202, 5998, 5, 20, 0)
              && gm_mobs_spawn_exact(
                  &runtime.mobs, EW_TYPE_PIG, 8200,
                  8.0, 220.0, 8.0, 0.0, 0.0, 0.0,
                  0.0F, 10.0F, 1, 0, 0, 0) >= 0,
          "two-tick non-EID XP-before-pig order initializes");
    runtime.mobs_enabled = !controlled;
    runtime.controlled_mobs_enabled = controlled;
    runtime.do_mob_loot = 1;
    static const int expected[] = {8202, 8200};
    static const int expected_kinds[] = {
        GM_MOB_LOADED_XP, GM_MOB_LOADED_LIVING
    };
    for (int tick = 0; tick < 2; ++tick) {
        gm_runtime_tick(&runtime, idle);
        CHECK(runtime.mobs.tick_update_order_count
                  == (int)(sizeof expected / sizeof expected[0]),
              "persistent-order scheduler dispatches all fixtures");
        int loaded_count = tick == 0 ? 2 : 1;
        CHECK(gm_mobs_loaded_order_count(&runtime.mobs) == loaded_count,
              "persistent loaded order survives then expires XP exactly");
        for (int index = 0;
                index < (int)(sizeof expected / sizeof expected[0]); ++index) {
            CHECK(runtime.mobs.tick_update_order[index] == expected[index],
                  "XP-before-pig update order is independent of EID order");
        }
        for (int index = 0; index < loaded_count; ++index) {
            int eid = 0, kind = 0;
            int expected_index = index + (tick == 0 ? 0 : 1);
            CHECK(gm_mobs_loaded_order_get(
                      &runtime.mobs, index, &eid, &kind)
                      && eid == expected[expected_index]
                      && kind == expected_kinds[expected_index],
                  "XP expiry compacts the imported loaded order");
        }
        CHECK((tick == 0 && xp_slot_for(&runtime.mobs, 8202) >= 0
                      && runtime.mobs.xp_orbs[
                          xp_slot_for(&runtime.mobs, 8202)].xpOrbAge == 5999)
                  || (tick == 1 && xp_slot_for(&runtime.mobs, 8202) < 0),
              "imported XP reaches its exact two-tick expiry boundary");
    }
    gm_runtime_destroy(&runtime);
}

int main(void) {
    GmMobLive mobs;
    uint64_t world = 0x23456789abULL, math = 0x3456789abcULL;
    int next_id = 1000, delay = 0;
    CHECK(setup(&mobs, 2.999), "mating pair initializes");
    for (int i = 0; i < 59; ++i) {
        GmSheepMateResult out = update(&mobs, &delay, 0, 1,
            &world, &math, &next_id, 1);
        CHECK(out.result == GM_SHEEP_MATE_WAITING && delay == i + 1,
              "updates 1 through 59 wait exactly");
    }
    int first = slot_for(&mobs, 100), second = slot_for(&mobs, 101);
    JavaGaussianRandom expected = mobs.entity_random[first];
    GmSheepMateResult born = update(&mobs, &delay, 0, 1,
        &world, &math, &next_id, 1);
    CHECK(born.result == GM_SHEEP_MATE_BORN && born.delay == 60 && delay == 60,
          "update 60 is the birth boundary");
    int child = slot_for(&mobs, born.child_eid);
    EwStore *s = store(&mobs);
    CHECK(born.child_eid == 1000 && born.child_slot == child && child >= 0
              && mobs.growing_age[child] == -24000
              && mobs.sheep_data[child] == born.child_fleece
              && s->x[child] == s->x[first] && s->y[child] == s->y[first]
              && s->z[child] == s->z[first] && s->yaw[child] == 0.0F,
          "birth creates the child age, color, position, and neutral pose");
    CHECK(mobs.growing_age[first] == 6000 && mobs.growing_age[second] == 6000
              && mobs.sheep_in_love[first] == 0 && mobs.sheep_in_love[second] == 0
              && !mobs.sheep_bred_by_player[first] && !mobs.sheep_bred_by_player[second],
          "birth cools parents and clears love and player breeding state");
    GmMobParticleBatch hearts;
    CHECK(gm_mobs_particle_batch_count(&mobs) == 1
              && gm_mobs_particle_batch_get(&mobs, 0, &hearts)
              && hearts.seq == 0 && hearts.eid == 100 && hearts.particle_id == 34
              && hearts.count == 7,
          "birth emits one ordered batch of seven heart particles");
    for (int i = 0; i < 7; ++i) {
        double vx = jrand_gaussian_next(&expected) * 0.02;
        double vy = jrand_gaussian_next(&expected) * 0.02;
        double vz = jrand_gaussian_next(&expected) * 0.02;
        float width = 0.9F, height = 1.3F;
        double x = jrand_double(&expected.random) * (double)width * 2.0
            - (double)width;
        double y = 0.5 + jrand_double(&expected.random) * (double)height;
        double z = jrand_double(&expected.random) * (double)width * 2.0
            - (double)width;
        CHECK(hearts.particles[i].vx == vx && hearts.particles[i].vy == vy
                  && hearts.particles[i].vz == vz
                  && hearts.particles[i].x == 4 + x && hearts.particles[i].y == 70 + y
                  && hearts.particles[i].z == 8 + z,
              "heart payload follows Entity.rand Gaussian/cache cursor exactly");
    }
    int expected_xp = jrand_int_bound(&expected.random, 7) + 1;
    int xp = born.xp_slot;
    CHECK(born.xp_eid == 1001 && xp >= 0 && mobs.xp_orbs[xp].eid == 1001
              && born.xp_value == expected_xp
              && mobs.xp_orbs[xp].xpValue == born.xp_value && next_id == 1002
              && mobs.entity_random[first].random.seed == expected.random.seed
              && mobs.entity_random[first].have_next_next_gaussian
                  == expected.have_next_next_gaussian
              && mobs.entity_random[first].next_next_gaussian == expected.next_next_gaussian,
          "loot XP uses the global IDs and leaves the exact Gaussian cursor");

    CHECK(setup(&mobs, 3.0), "strict-distance pair initializes");
    delay = 59; world = 7; math = 8; next_id = 2000;
    CHECK(update(&mobs, &delay, 0, 1, &world, &math, &next_id, 0).result
              == GM_SHEEP_MATE_WAITING && delay == 60 && next_id == 2000,
          "distance squared equal to nine is strictly outside mating range");
    CHECK(setup(&mobs, 0.0), "no-loot pair initializes");
    delay = 59; world = 7; math = 8; next_id = 3000;
    born = update(&mobs, &delay, 0, 1, &world, &math, &next_id, 0);
    CHECK(born.result == GM_SHEEP_MATE_BORN && born.xp_slot == -1
              && born.xp_eid == -1 && born.xp_value == 0 && next_id == 3001,
          "doMobLoot false suppresses XP and its global ID");

    CHECK(setup(&mobs, 0.0), "Forge cancellation pair initializes");
    delay = 59; world = 9; math = 10; next_id = 4000;
    born = update(&mobs, &delay, 1, 1, &world, &math, &next_id, 1);
    first = slot_for(&mobs, 100); second = slot_for(&mobs, 101);
    CHECK(born.result == GM_SHEEP_MATE_CANCELLED && born.child_slot == -1
              && next_id == 4001 && mobs.growing_age[first] == 6000
              && mobs.growing_age[second] == 6000 && !mobs.sheep_in_love[first]
              && !mobs.sheep_in_love[second],
          "Forge cancellation consumes the child ID and resets both parents");
    CHECK(setup(&mobs, 0.0), "null-child pair initializes");
    delay = 59; world = 9; math = 10; next_id = 5000;
    born = update(&mobs, &delay, 0, 0, &world, &math, &next_id, 1);
    first = slot_for(&mobs, 100); second = slot_for(&mobs, 101);
    CHECK(born.result == GM_SHEEP_MATE_NULL_CHILD && next_id == 5001
              && mobs.growing_age[first] == 0 && mobs.growing_age[second] == 0
              && mobs.sheep_in_love[first] == 600 && mobs.sheep_in_love[second] == 600,
          "null Forge child consumes construction state without resetting parents");

    CHECK(setup(&mobs, 0.0), "invalid reset pair initializes");
    delay = 17; world = 1; math = 2; next_id = 6000;
    CHECK(gm_mobs_sheep_mate_update(&mobs, 100, 999, &delay, 0, 1,
              &world, &math, &next_id, 1, NULL) == GM_SHEEP_MATE_NONE && delay == 0,
          "missing mate resets a running task");
    delay = -1;
    CHECK(gm_mobs_sheep_mate_update(&mobs, 100, 101, &delay, 0, 1,
              &world, &math, &next_id, 1, NULL) == GM_SHEEP_MATE_NONE && delay == -1,
          "invalid delay is rejected without mutation");

    test_non_sheep_direct_birth();
    test_capacity_fallbacks();
    test_particle_batch_capacity();
    test_reused_child_slot();
    test_non_sheep_live_scheduler();
    test_chicken_egg_threshold();
    test_ordinary_scheduler();
    test_simultaneous_birth_order();
    test_preexisting_xp_slot_reuse_order();
    test_preexisting_living_slot_reuse_order();
    test_persistent_order_two_ticks(0);
    test_persistent_order_two_ticks(1);

    if (fail) return 1;
    puts("PASS animal mating runtime: direct/live species birth plus order");
    return 0;
}
