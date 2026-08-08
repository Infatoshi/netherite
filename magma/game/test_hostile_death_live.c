#include "game/runtime.h"

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

static EwStore *store(GmMobLive *m) {
    return m->current ? &m->b : &m->a;
}

static int exact_item(
        const GmLiveEnt *item, int eid, int expected_item,
        int count, int meta, int potion_type, double x, double y, double z,
        JavaRandom *math) {
    float hover = (float)(jrand_double(math) * (MC_PI * 2.0));
    float yaw = (float)(jrand_double(math) * 360.0);
    double motion_x = (double)(float)(
        jrand_double(math) * 0.20000000298023224
            - 0.10000000149011612);
    double motion_z = (double)(float)(
        jrand_double(math) * 0.20000000298023224
            - 0.10000000149011612);
    return item->active && item->type == 0 && item->eid == eid
        && item->item == expected_item && item->count == count
        && item->meta == meta && item->semantic_potion_type == potion_type
        && item->x == x && item->y == y && item->z == z
        && item->mx == motion_x && item->my == 0.20000000298023224
        && item->mz == motion_z && item->yaw == yaw
        && item->has_hover_start && item->hover_start == hover
        && item->age == 0 && item->pickup_delay == 10
        && item->health == 5 && item->lifespan == 6000;
}

static int spawn_ordinary(
        GmMobLive *m, int type, int size, int eid,
        float health, uint64_t seed48,
        double x, double y, double z) {
    gm_mobs_init(m, 0);
    m->active_dimension = 0;
    m->next_id = eid;
    int slot = gm_mobs_spawn_sized(m, type, x, y, z, size);
    if (slot <= 0 || store(m)->id[slot] != eid) return -1;
    m->a.health[slot] = health;
    m->b.health[slot] = health;
    if (!gm_mobs_set_entity_random_state(m, eid, seed48, 0, 0.0))
        return -1;
    return slot;
}

static void expected_attack_random(
        int type, int size, uint64_t seed48, int looting,
        JavaRandom *random, GmHostileLootOutcome *loot) {
    *random = (JavaRandom){seed48};
    if (type == EW_TYPE_PIGMAN) {
        (void)jrand_int_bound(random, 400);
        (void)jrand_int_bound(random, 40);
    }
    (void)jrand_double(random);
    (void)jrand_double(random);
    (void)jrand_float(random);
    (void)jrand_float(random);
    uint64_t cursor = random->seed;
    CHECK(gm_mobs_generate_hostile_loot(
              type, size, &cursor, looting, 1, loot),
          "hostile table supports live death type");
    random->seed = cursor;
}

static int run_attack_case(int type, int size, int index) {
    const uint64_t entity_seed = (uint64_t)(402 + index * 17);
    const uint64_t math_seed = (uint64_t)(67890 + index * 101);
    GmMobLive mobs;
    GmLiveSim drops;
    PsvPlayer player;
    McSinTable sin_table;
    GmMobEvent event;
    JavaRandom expected_random, expected_math;
    GmHostileLootOutcome loot;
    int eid = 700 + index;
    int next_id = 1000 + index * 8;
    int initial_next = next_id;
    uint64_t math_cursor = math_seed;
    float target_width, target_height;
    ehs_size_scaled((u8)type, size, &target_width, &target_height);
    (void)target_width;
    double target_y = 19.28 + (double)PSV_EYE_HEIGHT
        - (double)target_height * 0.5;
    int slot = spawn_ordinary(
        &mobs, type, size, eid, 1.0F, entity_seed,
        10.5, target_y, 30.5);
    GmMobDeathContext context = {1, &math_cursor, &next_id};

    memset(&drops, 0, sizeof drops);
    memset(&player, 0, sizeof player);
    isr_init(&player.inv);
    player.ent.posX = 10.5;
    player.ent.posY = 19.28;
    player.ent.posZ = 32.5;
    player.ent.onGround = 1;
    player.yaw = 180.0F;
    player.movement_speed_multiplier = 1.0;
    ICStack weapon = ic_mk(276, 1, 0);
    weapon.n_enchants = 1;
    weapon.enchants[0].id = 21;
    weapon.enchants[0].level = 3;
    isr_set_stack(&player.inv, 0, weapon);
    mc_sin_table_init(&sin_table);
    mobs.player_ticks_since_last_swing = 5;
    expected_attack_random(
        type, size, entity_seed, 3, &expected_random, &loot);
    expected_math = (JavaRandom){math_seed};

    int attack_result = slot > 0 ? gm_mobs_player_attack(
        &mobs, (const struct PsvPlayer *)&player, 0, 0,
        (const struct McSinTable *)&sin_table, &drops,
        0.0F, 1.0, 0, 0, 0, &context,
        0.0F, NULL) : 0;
    if (attack_result != 2 || !mobs.entity_dead[slot]
            || drops.n_active != loot.count)
        fprintf(stderr,
            "type=%d attack=%d dead=%d health=%g drops=%d/%d "
            "seed=%llu/%llu math=%llu/%llu next=%d/%d\n",
            type, attack_result, slot > 0 ? mobs.entity_dead[slot] : -1,
            slot > 0 ? (double)store(&mobs)->health[slot] : -1.0,
            drops.n_active, loot.count,
            (unsigned long long)(slot > 0
                ? mobs.entity_random[slot].random.seed : 0),
            (unsigned long long)expected_random.seed,
            (unsigned long long)math_cursor,
            (unsigned long long)expected_math.seed,
            next_id, initial_next + loot.count);
    CHECK(slot > 0 && attack_result == 2,
          "product player attack accepts lethal hostile hit");
    CHECK(store(&mobs)->alive[slot] && mobs.entity_dead[slot]
              && store(&mobs)->health[slot] == 0.0F
              && mobs.entity_death_time[slot] == 0
              && mobs.entity_recently_hit[slot] == 100
              && mobs.entity_attacking_player[slot],
          "hostile remains credited in Java death-update window");
    CHECK(drops.n_active == loot.count,
          "product hostile death emits every exact loot stack");
    for (int i = 0; i < loot.count; ++i) {
        int item_exact = exact_item(
            &drops.ents[i], initial_next + i,
            loot.item[i], loot.quantity[i],
            loot.potion_type[i] == GM_HOSTILE_LOOT_POTION_SLOWNESS
                ? 17 : loot.meta[i],
            loot.potion_type[i],
            10.5, target_y, 30.5, &expected_math);
        if (!item_exact)
            fprintf(stderr,
                "type=%d item[%d] actual=%d:%d:%d eid=%d "
                "xyz=%.17g,%.17g,%.17g expected=%d:%d:%d eid=%d "
                "xyz=%.17g,%.17g,%.17g\n",
                type, i, drops.ents[i].item, drops.ents[i].count,
                drops.ents[i].meta, drops.ents[i].eid,
                drops.ents[i].x, drops.ents[i].y, drops.ents[i].z,
                loot.item[i], loot.quantity[i], loot.meta[i],
                initial_next + i, 10.5, target_y, 30.5);
        CHECK(item_exact,
              "hostile drop preserves exact EntityItem constructor state");
    }
    CHECK(mobs.entity_random[slot].random.seed == expected_random.seed
              && math_cursor == expected_math.seed
              && next_id == initial_next + loot.count
              && mobs.next_id == next_id && mobs.next_orb_id == next_id,
          "hostile private, Math, and entity-ID cursors commit exactly");
    int event_count = gm_mobs_event_count(&mobs);
    CHECK(event_count == (type == EW_TYPE_GIANT ? 2 : 3)
              && gm_mobs_event_get(&mobs, 0, &event)
              && event.kind == GM_MOB_EVENT_ENTITY_STATUS
              && event.eid == eid && event.data == 2
              && (type == EW_TYPE_GIANT
                  || (gm_mobs_event_get(&mobs, 1, &event)
                      && event.kind == GM_MOB_EVENT_SOUND))
              && gm_mobs_event_get(
                  &mobs, type == EW_TYPE_GIANT ? 1 : 2, &event)
              && event.kind == GM_MOB_EVENT_ENTITY_STATUS
              && event.eid == eid && event.data == 3,
          "hostile death orders exact hurt, optional sound, and death status");
    return 0;
}

static void construct_orb(
        McOrb *orb, int eid, int value, double x, double y, double z,
        JavaRandom *math) {
    memset(orb, 0, sizeof *orb);
    orb->yaw = (float)(jrand_double(math) * 360.0);
    float motion_x = (float)(jrand_double(math)
        * 0.20000000298023224 - 0.10000000149011612);
    float motion_y = (float)(jrand_double(math) * 0.2);
    float motion_z = (float)(jrand_double(math)
        * 0.20000000298023224 - 0.10000000149011612);
    orb->motionX = (double)(motion_x * 2.0F);
    orb->motionY = (double)(motion_y * 2.0F);
    orb->motionZ = (double)(motion_z * 2.0F);
    orb->xpValue = value;
    orb->health = 5;
    orb->eid = eid;
    eo_set_position(orb, x, y, z);
}

static int same_orb(const McOrb *a, const McOrb *b) {
    return a->posX == b->posX && a->posY == b->posY
        && a->posZ == b->posZ && a->motionX == b->motionX
        && a->motionY == b->motionY && a->motionZ == b->motionZ
        && a->box.minX == b->box.minX && a->box.minY == b->box.minY
        && a->box.minZ == b->box.minZ && a->box.maxX == b->box.maxX
        && a->box.maxY == b->box.maxY && a->box.maxZ == b->box.maxZ
        && a->xpValue == b->xpValue && a->health == b->health
        && a->eid == b->eid && a->yaw == b->yaw;
}

static int run_terminal_case(
        GmRuntime *runtime, int type, int size, int index) {
    const double x = 24.5, y = 220.0, z = 24.5;
    const uint64_t entity_seed = (uint64_t)(900 + index * 31);
    const uint64_t math_seed = (uint64_t)(3000 + index * 29);
    uint64_t world_seed = UINT64_C(987654321);
    uint64_t math_cursor = math_seed;
    int eid = 800 + index;
    int next_id = 2000 + index * 8;
    int initial_next = next_id;
    int slot = spawn_ordinary(
        &runtime->mobs, type, size, eid, 0.0F, entity_seed, x, y, z);
    CHECK(slot > 0, "terminal hostile fixture initializes");
    if (slot <= 0) return 1;
    runtime->mobs.entity_dead[slot] = 1;
    runtime->mobs.entity_death_time[slot] = 19;
    runtime->mobs.entity_recently_hit[slot] = 100;
    runtime->mobs.entity_attacking_player[slot] = 1;
    memset(&runtime->entities, 0, sizeof runtime->entities);

    gm_mobs_tick(
        &runtime->mobs, runtime->world, NULL,
        (const struct McSinTable *)&runtime->sin_table,
        (struct PsvPlayer *)&runtime->player,
        (struct PvStats *)&runtime->vitals,
        runtime->ox, runtime->oz, runtime->dimension, 12000,
        &runtime->clock, 0,
        &world_seed, &math_cursor, &next_id, 1,
        &runtime->entities, 0.0F, 0.0F);

    static const int one_xp[] = {1};
    static const int two_xp[] = {1, 1};
    static const int three_xp[] = {3};
    static const int four_xp[] = {3, 1};
    static const int five_xp[] = {3, 1, 1};
    static const int ten_xp[] = {7, 3};
    JavaRandom expected_world = {UINT64_C(987654321)};
    int passive_xp[] = {1};
    if (type == EW_TYPE_POLAR_BEAR || type == EW_TYPE_RABBIT)
        passive_xp[0] = 1 + jrand_int_bound(&expected_world, 3);
    const int *values = (type == EW_TYPE_POLAR_BEAR || type == EW_TYPE_RABBIT)
        ? passive_xp
        : type == EW_TYPE_BLAZE ? ten_xp
        : type == EW_TYPE_VILLAGER ? NULL
        : type == EW_TYPE_ENDERMITE ? three_xp
        : type == EW_TYPE_SLIME || type == EW_TYPE_MAGMA
            ? size == 1 ? one_xp : size == 2 ? two_xp : four_xp
            : five_xp;
    int count = (type == EW_TYPE_POLAR_BEAR || type == EW_TYPE_RABBIT) ? 1
        : type == EW_TYPE_BLAZE ? 2
        : type == EW_TYPE_VILLAGER ? 0
        : type == EW_TYPE_ENDERMITE ? 1
        : type == EW_TYPE_SLIME || type == EW_TYPE_MAGMA
            ? size == 1 ? 1 : 2
            : 3;
    JavaRandom expected_math = {math_seed};
    for (int i = 0; i < count; ++i) {
        McOrb expected;
        construct_orb(
            &expected, initial_next + i, values[i], x, y, z,
            &expected_math);
        eo_tick(
            &expected,
            runtime->player.ent.posX + runtime->ox,
            runtime->player.ent.posY,
            runtime->player.ent.posZ + runtime->oz,
            PSV_EYE_HEIGHT, 0, NULL, 0, 0, 0);
        CHECK(same_orb(&runtime->mobs.xp_orbs[i], &expected),
              "hostile terminal XP constructor and same-tick state are exact");
    }
    JavaGaussianRandom expected_entity;
    jrand_gaussian_set_state(&expected_entity, entity_seed, 0, 0.0);
    int child_count = 0;
    if ((type == EW_TYPE_SLIME || type == EW_TYPE_MAGMA) && size > 1) {
        child_count = 2 + jrand_int_bound(&expected_entity.random, 3);
        for (int k = 0; k < child_count; ++k) {
            float offset_x = ((float)(k % 2) - 0.5F)
                * (float)size / 4.0F;
            float offset_z = ((float)(k / 2) - 0.5F)
                * (float)size / 4.0F;
            float yaw = jrand_float(&expected_entity.random) * 360.0F;
            int child_eid = initial_next + count + k;
            int child_slot = -1;
            EwStore *children = store(&runtime->mobs);
            for (int i = 1; i < EW_MAX_ENTITIES; ++i)
                if (children->alive[i]
                        && children->id[i] == child_eid) {
                    child_slot = i;
                    break;
                }
            CHECK(child_slot > 0
                      && children->type[child_slot] == type
                      && runtime->mobs.size[child_slot] == size / 2
                      && children->health[child_slot]
                          == (float)((size / 2) * (size / 2))
                      && children->x[child_slot]
                          == x + (double)offset_x
                      && children->y[child_slot] == y + 0.5D
                      && children->z[child_slot]
                          == z + (double)offset_z
                      && children->yaw[child_slot] == yaw,
                  "Slime terminal child state is exact");
        }
    }
    CHECK(!store(&runtime->mobs)->alive[slot]
              && runtime->mobs.entity_death_time[slot] == 20
              && math_cursor == expected_math.seed
              && next_id == initial_next + count + child_count,
          "hostile retires after exact split-XP terminal boundary");
    CHECK(runtime->mobs.tick_update_order_count == 1 + count
              && runtime->mobs.tick_update_order[0] == eid,
          "hostile and split XP preserve loaded-list update order");

    GmMobTerminalParticles particles;
    int exact = gm_mobs_terminal_particle_get(
        &runtime->mobs, 0, &particles);
    float width, height;
    ehs_size_scaled((u8)type, size, &width, &height);
    for (int i = 0; i < 20 && exact; ++i) {
        double vx = jrand_gaussian_next(&expected_entity) * 0.02;
        double vy = jrand_gaussian_next(&expected_entity) * 0.02;
        double vz = jrand_gaussian_next(&expected_entity) * 0.02;
        float dx = jrand_float(&expected_entity.random) * width * 2.0F;
        float dy = jrand_float(&expected_entity.random) * height;
        float dz = jrand_float(&expected_entity.random) * width * 2.0F;
        const GmTerminalParticle *actual = &particles.particles[i];
        exact = actual->x == x + (double)dx - (double)width
            && actual->y == y + (double)dy
            && actual->z == z + (double)dz - (double)width
            && actual->vx == vx && actual->vy == vy && actual->vz == vz;
    }
    CHECK(exact && particles.eid == eid && particles.dimension == 0
              && runtime->mobs.entity_random[slot].random.seed
                  == expected_entity.random.seed,
          "hostile terminal particles consume exact entity RNG");
    return 0;
}

int main(void) {
    static const struct { int type, size; } cases[] = {
        {EW_TYPE_ZOMBIE, 1}, {EW_TYPE_ZOMBIE_VILLAGER, 1},
        {EW_TYPE_HUSK, 1},
        {EW_TYPE_STRAY, 1},
        {EW_TYPE_PIGMAN, 1}, {EW_TYPE_SKELETON, 1},
        {EW_TYPE_WITHER_SKELETON, 1}, {EW_TYPE_CREEPER, 1},
        {EW_TYPE_SPIDER, 1}, {EW_TYPE_CAVE_SPIDER, 1},
        {EW_TYPE_ENDERMAN, 1}, {EW_TYPE_BLAZE, 1},
        {EW_TYPE_GHAST, 1}, {EW_TYPE_SILVERFISH, 1},
        {EW_TYPE_ENDERMITE, 1},
        {EW_TYPE_GIANT, 1},
        {EW_TYPE_POLAR_BEAR, 1},
        {EW_TYPE_RABBIT, 1},
        {EW_TYPE_VILLAGER, 1},
        {EW_TYPE_SLIME, 1}, {EW_TYPE_SLIME, 2},
        {EW_TYPE_SLIME, 4}, {EW_TYPE_MAGMA, 1},
        {EW_TYPE_MAGMA, 2}, {EW_TYPE_MAGMA, 4}
    };
    GmConfig config;
    GmRuntime runtime;
    char error[256] = {0};

    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i)
        (void)run_attack_case(cases[i].type, cases[i].size, (int)i);

    gm_config_defaults(&config);
    config.seed = 0;
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.mobs = 0;
    config.weather = 0;
    CHECK(gm_runtime_init(&runtime, &config, error, sizeof error), error);
    if (fail) return 1;
    gm_runtime_set_pose(&runtime, 8.5, 5.0, 8.5, 0.0F, 0.0F);
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i)
        (void)run_terminal_case(
            &runtime, cases[i].type, cases[i].size, (int)i);
    gm_runtime_destroy(&runtime);

    if (fail) return 1;
    puts("hostile_death_live: PASS attacks=25 terminal=25 items=exact "
         "xp=exact slime_children=exact particles=500");
    return 0;
}
