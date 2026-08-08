#include "entity_witch.h"
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

static int stage_witch(
        GmMobLive *m, int eid, uint64_t seed48, float health) {
    gm_mobs_init(m, 0);
    m->active_dimension = 0;
    m->next_id = eid;
    int slot = gm_mobs_spawn_witch(m, 24.5, 220.0, 24.5);
    if (slot <= 0) return -1;
    store(m)->health[slot] = health;
    (m->current ? &m->a : &m->b)->health[slot] = health;
    m->witch_drinking[slot] = 1;
    m->witch_potion[slot] = EWITCH_SELF_HEALING;
    m->witch_attack_timer[slot] = 32;
    if (!gm_mobs_set_entity_random_state(m, eid, seed48, 0, 0.0))
        return -1;
    return slot;
}

static void consume_fresh_feedback(JavaRandom *random) {
    (void)jrand_double(random);
    (void)jrand_double(random);
    (void)jrand_float(random);
    (void)jrand_float(random);
}

static int exact_item(
        const GmLiveEnt *item, int eid, int expected_item, int meta,
        double x, double y, double z, JavaRandom *math) {
    float hover = (float)(jrand_double(math) * (MC_PI * 2.0));
    float yaw = (float)(jrand_double(math) * 360.0);
    double motion_x = (double)(float)(jrand_double(math)
        * 0.20000000298023224 - 0.10000000149011612);
    double motion_z = (double)(float)(jrand_double(math)
        * 0.20000000298023224 - 0.10000000149011612);
    return item->active && item->eid == eid && item->item == expected_item
        && item->count == 1 && item->meta == meta
        && item->x == x && item->y == y && item->z == z
        && item->mx == motion_x && item->my == 0.20000000298023224
        && item->mz == motion_z && item->yaw == yaw
        && item->has_hover_start && item->hover_start == hover
        && item->age == 0 && item->pickup_delay == 10
        && item->health == 5 && item->lifespan == 6000;
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
        && a->xpOrbAge == b->xpOrbAge && a->xpValue == b->xpValue
        && a->health == b->health && a->eid == b->eid
        && a->yaw == b->yaw && a->dead == b->dead;
}

int main(void) {
    GmConfig config;
    GmRuntime runtime;
    GmLiveSim drops;
    char error[256] = {0};
    uint64_t world_seed = UINT64_C(987654321);
    uint64_t math_cursor = UINT64_C(67890);
    int next_id = 1000;

    gm_config_defaults(&config);
    config.seed = 0;
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.mobs = 0;
    config.weather = 0;
    CHECK(gm_runtime_init(&runtime, &config, error, sizeof error), error);
    if (fail) return 1;
    gm_runtime_set_pose(&runtime, 8.5, 5.0, 8.5, 0.0F, 0.0F);

    /* Seed 82 reaches an empty loot table, successful hand-drop roll, and
     * total XP eight after the six fresh-hit Random calls. */
    int slot = stage_witch(&runtime.mobs, 700, 82, 1.0F);
    GmMobDeathContext context = {1, &math_cursor, &next_id};
    memset(&drops, 0, sizeof drops);
    JavaRandom expected_random = {82};
    JavaRandom expected_math = {UINT64_C(67890)};
    EwitchLootOutcome expected_loot;
    consume_fresh_feedback(&expected_random);
    ewitch_generate_loot(&expected_random, 0, &expected_loot);
    int equipment_drop = ewitch_equipped_drop(&expected_random, 0);
    CHECK(slot > 0 && expected_loot.count == 0 && equipment_drop,
          "equipped Witch fixture selects only the healing potion drop");
    CHECK(gm_mobs_player_damage_witch_exact(
              &runtime.mobs, 700, 23.5, 24.5, 2.0F, 0,
              &drops, &context) == 2,
          "lethal equipped Witch hit enters the death window");
    CHECK(drops.n_active == 1 && exact_item(
              &drops.ents[0], 1000, 373, 21,
              24.5, 220.0, 24.5, &expected_math),
          "healing potion drop preserves exact EntityItem state and metadata");
    CHECK(runtime.mobs.entity_random[slot].random.seed
              == expected_random.seed
              && math_cursor == expected_math.seed && next_id == 1001,
          "lethal boundary commits loot, hand-roll, Math, and ID cursors");

    runtime.mobs.entity_death_time[slot] = 19;
    runtime.mobs.controlled_no_ai[slot] = 1;
    int total_xp = ewitch_experience_points(&expected_random, 1);
    CHECK(total_xp == 8, "equipped Witch fixture selects total XP eight");
    gm_mobs_tick_controlled(
        &runtime.mobs, runtime.world, NULL,
        (struct PsvPlayer *)&runtime.player,
        runtime.ox, runtime.oz, runtime.dimension, &runtime.clock,
        1, &drops,
        &world_seed, &math_cursor, &next_id);

    static const int values[] = {7, 1};
    for (int i = 0; i < 2; ++i) {
        McOrb expected;
        construct_orb(
            &expected, 1001 + i, values[i], 24.5, 220.0, 24.5,
            &expected_math);
        eo_tick(
            &expected,
            runtime.player.ent.posX + runtime.ox,
            runtime.player.ent.posY,
            runtime.player.ent.posZ + runtime.oz,
            PSV_EYE_HEIGHT, 0, NULL, 0, 0, 0);
        CHECK(same_orb(&runtime.mobs.xp_orbs[i], &expected),
              "equipped Witch split XP preserves constructor and first tick");
    }
    CHECK(runtime.mobs.tick_update_order_count == 3
              && runtime.mobs.tick_update_order[0] == 700
              && runtime.mobs.tick_update_order[1] == 1001
              && runtime.mobs.tick_update_order[2] == 1002
              && next_id == 1003 && math_cursor == expected_math.seed,
          "equipped Witch and XP preserve loaded order and global cursors");

    JavaGaussianRandom expected_entity;
    jrand_gaussian_set_state(&expected_entity, expected_random.seed, 0, 0.0);
    GmMobTerminalParticles particles;
    int particles_exact = gm_mobs_terminal_particle_get(
        &runtime.mobs, 0, &particles);
    for (int i = 0; i < 20 && particles_exact; ++i) {
        double vx = jrand_gaussian_next(&expected_entity) * 0.02;
        double vy = jrand_gaussian_next(&expected_entity) * 0.02;
        double vz = jrand_gaussian_next(&expected_entity) * 0.02;
        float dx = jrand_float(&expected_entity.random) * 0.6F * 2.0F;
        float dy = jrand_float(&expected_entity.random) * 1.95F;
        float dz = jrand_float(&expected_entity.random) * 0.6F * 2.0F;
        const GmTerminalParticle *actual = &particles.particles[i];
        particles_exact = actual->x == 24.5 + (double)dx - (double)0.6F
            && actual->y == 220.0 + (double)dy
            && actual->z == 24.5 + (double)dz - (double)0.6F
            && actual->vx == vx && actual->vy == vy && actual->vz == vz;
    }
    /* The stored drinking Witch decrements its timer after onDeathUpdate,
     * then consumes the final status-particle float. */
    (void)jrand_float(&expected_entity.random);
    CHECK(particles_exact
              && runtime.mobs.entity_random[slot].random.seed
                  == expected_entity.random.seed
              && runtime.mobs.entity_random[slot].have_next_next_gaussian
                  == expected_entity.have_next_next_gaussian
              && runtime.mobs.entity_random[slot].next_next_gaussian
                  == expected_entity.next_next_gaussian,
          "equipped XP draw precedes the exact terminal particle RNG tail");

    /* Seed 32 covers the independent no-drop hand roll and one-orb XP seven. */
    world_seed = UINT64_C(12345);
    math_cursor = UINT64_C(24680);
    next_id = 2000;
    slot = stage_witch(&runtime.mobs, 701, 32, 1.0F);
    context = (GmMobDeathContext){1, &math_cursor, &next_id};
    memset(&drops, 0, sizeof drops);
    expected_random = (JavaRandom){32};
    consume_fresh_feedback(&expected_random);
    ewitch_generate_loot(&expected_random, 0, &expected_loot);
    equipment_drop = ewitch_equipped_drop(&expected_random, 0);
    CHECK(slot > 0 && expected_loot.count == 0 && !equipment_drop
              && gm_mobs_player_damage_witch_exact(
                  &runtime.mobs, 701, 23.5, 24.5, 2.0F, 0,
                  &drops, &context) == 2
              && drops.n_active == 0 && next_id == 2000,
          "failed hand roll emits no potion and consumes no constructor cursor");
    runtime.mobs.entity_death_time[slot] = 19;
    total_xp = ewitch_experience_points(&expected_random, 1);
    memset(&runtime.entities, 0, sizeof runtime.entities);
    gm_mobs_tick(
        &runtime.mobs, runtime.world, NULL,
        (const struct McSinTable *)&runtime.sin_table,
        (struct PsvPlayer *)&runtime.player,
        (struct PvStats *)&runtime.vitals,
        runtime.ox, runtime.oz, runtime.dimension, 12000,
        &runtime.clock, 0,
        &world_seed, &math_cursor, &next_id, 1,
        &runtime.entities, 0.0F, 0.0F);
    CHECK(total_xp == 7 && runtime.mobs.xp_orbs[0].xpValue == 7
              && next_id == 2001,
          "equipped XP bonus remains independent of potion drop success");

    /* The fixed item pool includes the possible equipment stack in its
     * preflight and rejects the lethal boundary without partial state. */
    math_cursor = UINT64_C(13579);
    next_id = 3000;
    slot = stage_witch(&runtime.mobs, 702, 82, 1.0F);
    context = (GmMobDeathContext){1, &math_cursor, &next_id};
    memset(&drops, 0, sizeof drops);
    for (int i = 0; i < GM_LIVE_MAX; ++i) drops.ents[i].active = 1;
    drops.n_active = GM_LIVE_MAX;
    drops.item_spawn_limit = GM_LIVE_MAX;
    CHECK(slot > 0 && gm_mobs_player_damage_witch_exact(
              &runtime.mobs, 702, 23.5, 24.5, 2.0F, 0,
              &drops, &context) == 1
              && store(&runtime.mobs)->health[slot] == 1.0F
              && !runtime.mobs.entity_dead[slot]
              && runtime.mobs.entity_random[slot].random.seed == 82
              && math_cursor == UINT64_C(13579) && next_id == 3000,
          "potion-only item-capacity rejection is atomic");

    gm_runtime_destroy(&runtime);
    if (fail) return 1;
    puts("witch_equipped_death_live: PASS potion_drop=1 no_drop=1 "
         "xp=8/7 product=1 order=3 particles=20 capacity_atomic=1");
    return 0;
}
