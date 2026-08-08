#include "game/runtime.h"
#include "entity_witch.h"

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
        && a->onGround == b->onGround && a->xpOrbAge == b->xpOrbAge
        && a->delayBeforeCanPickup == b->delayBeforeCanPickup
        && a->xpColor == b->xpColor
        && a->xpTargetColor == b->xpTargetColor
        && a->xpValue == b->xpValue && a->health == b->health
        && a->eid == b->eid && a->yaw == b->yaw
        && a->has_closest == b->has_closest && a->dead == b->dead;
}

static int stage_terminal_witch(
        GmMobLive *m, int eid, uint64_t entity_seed48) {
    gm_mobs_init(m, 0);
    m->active_dimension = 0;
    m->next_id = eid;
    int slot = gm_mobs_spawn_witch(m, 24.5, 220.0, 24.5);
    if (slot <= 0) return -1;
    m->a.health[slot] = 0.0F;
    m->b.health[slot] = 0.0F;
    m->entity_dead[slot] = 1;
    m->entity_death_time[slot] = 19;
    m->entity_recently_hit[slot] = 100;
    m->entity_attacking_player[slot] = 1;
    m->controlled_no_ai[slot] = 1;
    if (!gm_mobs_set_entity_random_state(
            m, eid, entity_seed48, 0, 0.0))
        return -1;
    return slot;
}

int main(void) {
    GmConfig config;
    GmRuntime runtime;
    char error[256] = {0};
    const uint64_t entity_seed = 0;
    const uint64_t math_seed = 0;
    uint64_t world_seed = UINT64_C(987654321);
    uint64_t math_cursor = math_seed;
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

    int slot = stage_terminal_witch(&runtime.mobs, 700, entity_seed);
    CHECK(slot > 0, "terminal Witch fixture initializes");
    gm_mobs_tick_controlled(
        &runtime.mobs, runtime.world, NULL,
        (struct PsvPlayer *)&runtime.player,
        runtime.ox, runtime.oz, runtime.dimension, &runtime.clock,
        1, NULL,
        &world_seed, &math_cursor, &next_id);

    JavaRandom expected_math = {math_seed};
    static const int values[] = {3, 1, 1};
    for (int i = 0; i < 3; ++i) {
        McOrb expected;
        construct_orb(
            &expected, 1000 + i, values[i], 24.5, 220.0, 24.5,
            &expected_math);
        eo_tick(
            &expected,
            runtime.player.ent.posX + runtime.ox,
            runtime.player.ent.posY,
            runtime.player.ent.posZ + runtime.oz,
            PSV_EYE_HEIGHT, 0, NULL, 0, 0, 0);
        CHECK(same_orb(&runtime.mobs.xp_orbs[i], &expected),
              "Witch terminal XP preserves constructor and same-tick state");
    }
    CHECK(!store(&runtime.mobs)->alive[slot]
              && store(&runtime.mobs)->type[slot] == EW_TYPE_NONE
              && runtime.mobs.entity_death_time[slot] == 20,
          "Witch retires after the exact terminal death boundary");
    CHECK(math_cursor == expected_math.seed && next_id == 1003
              && runtime.mobs.next_id == 1003
              && runtime.mobs.next_orb_id == 1003
              && world_seed == UINT64_C(987654321),
          "Witch terminal XP commits Math and IDs but not World.rand");
    CHECK(runtime.mobs.tick_update_order_count == 4
              && runtime.mobs.tick_update_order[0] == 700
              && runtime.mobs.tick_update_order[1] == 1000
              && runtime.mobs.tick_update_order[2] == 1001
              && runtime.mobs.tick_update_order[3] == 1002,
          "Witch and split XP preserve dynamic loaded-list update order");

    JavaGaussianRandom expected_entity;
    jrand_gaussian_set_state(&expected_entity, entity_seed, 0, 0.0);
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
    /* EntityLivingBase.onEntityUpdate emits the terminal particles, then
     * EntityLivingBase.onUpdate still dispatches EntityWitch.onLivingUpdate.
     * Account for that subclass's exact self-potion/status draws before
     * comparing the final private cursor. */
    EwitchSelfState expected_witch = {0};
    EwitchSelfConditions expected_conditions = {0};
    EwitchSelfOutcome expected_outcome;
    expected_conditions.health = 0.0F;
    expected_conditions.max_health = 26.0F;
    ewitch_self_potion_step(
        &expected_entity.random, &expected_witch,
        &expected_conditions, &expected_outcome, NULL);
    CHECK(particles_exact && particles.eid == 700
              && particles.dimension == 0
              && runtime.mobs.entity_random[slot].random.seed
                  == expected_entity.random.seed
              && runtime.mobs.entity_random[slot].have_next_next_gaussian
                  == expected_entity.have_next_next_gaussian
              && runtime.mobs.entity_random[slot].next_next_gaussian
                  == expected_entity.next_next_gaussian,
          "Witch terminal particles follow all XP constructors exactly");

    world_seed = UINT64_C(7777777);
    math_cursor = UINT64_C(424242);
    next_id = 1500;
    slot = stage_terminal_witch(&runtime.mobs, 702, UINT64_C(2));
    if (slot > 0) runtime.mobs.controlled_no_ai[slot] = 0;
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
    CHECK(slot > 0 && !store(&runtime.mobs)->alive[slot]
              && runtime.mobs.xp_orbs[0].xpValue == 3
              && runtime.mobs.xp_orbs[1].xpValue == 1
              && runtime.mobs.xp_orbs[2].xpValue == 1
              && runtime.mobs.xp_orbs[0].xpOrbAge == 1
              && runtime.mobs.xp_orbs[1].xpOrbAge == 1
              && runtime.mobs.xp_orbs[2].xpOrbAge == 1
              && next_id == 1503,
          "ordinary product Witch death reaches the exact terminal XP path");

    world_seed = UINT64_C(13579);
    math_cursor = UINT64_C(24680);
    next_id = 2000;
    slot = stage_terminal_witch(&runtime.mobs, 701, UINT64_C(12345));
    gm_mobs_tick_controlled(
        &runtime.mobs, runtime.world, NULL,
        (struct PsvPlayer *)&runtime.player,
        runtime.ox, runtime.oz, runtime.dimension, &runtime.clock,
        0, NULL,
        &world_seed, &math_cursor, &next_id);
    int xp_count = 0;
    for (int i = 0; i < GM_XP_ORBS; ++i)
        if (!runtime.mobs.xp_orbs[i].dead
                && runtime.mobs.xp_orbs[i].xpValue > 0)
            ++xp_count;
    CHECK(slot > 0 && xp_count == 0 && math_cursor == UINT64_C(24680)
              && next_id == 2000 && world_seed == UINT64_C(13579)
              && runtime.mobs.entity_death_time[slot] == 20,
          "doMobLoot false suppresses all Witch XP constructor cursors");

    world_seed = UINT64_C(97531);
    math_cursor = UINT64_C(86420);
    next_id = 3000;
    slot = stage_terminal_witch(&runtime.mobs, 703, UINT64_C(67890));
    for (int i = 0; i < GM_XP_ORBS; ++i) {
        McOrb *orb = &runtime.mobs.xp_orbs[i];
        memset(orb, 0, sizeof *orb);
        orb->eid = 5000 + i;
        orb->xpValue = 1;
        orb->health = 5;
        runtime.mobs.orb_dimension[i] = 0;
        eo_set_position(
            orb, 64.5 + (double)(i % 16) * 2.0, 220.0,
            64.5 + (double)(i / 16) * 2.0);
    }
    gm_mobs_tick_controlled(
        &runtime.mobs, runtime.world, NULL,
        (struct PsvPlayer *)&runtime.player,
        runtime.ox, runtime.oz, runtime.dimension, &runtime.clock,
        1, NULL,
        &world_seed, &math_cursor, &next_id);
    xp_count = 0;
    for (int i = 0; i < gm_mobs_xp_slot_count(&runtime.mobs); ++i) {
        const McOrb *orb = gm_mobs_xp_orb_ref(&runtime.mobs, i);
        if (orb && !orb->dead && orb->xpValue > 0)
            ++xp_count;
    }
    JavaRandom expected_full = {UINT64_C(86420)};
    int cold_exact = 1;
    for (int i = 0; i < 3; ++i) {
        McOrb expected;
        const McOrb *actual = gm_mobs_xp_orb_ref(
            &runtime.mobs, GM_XP_ORBS + i);
        construct_orb(
            &expected, 3000 + i, values[i], 24.5, 220.0, 24.5,
            &expected_full);
        eo_tick(
            &expected,
            runtime.player.ent.posX + runtime.ox,
            runtime.player.ent.posY,
            runtime.player.ent.posZ + runtime.oz,
            PSV_EYE_HEIGHT, 0, NULL, 0, 0, 0);
        cold_exact &= actual && same_orb(actual, &expected);
    }
    CHECK(slot > 0 && xp_count == GM_XP_ORBS + 3 && cold_exact
              && math_cursor == expected_full.seed && next_id == 3003
              && world_seed == UINT64_C(97531)
              && runtime.mobs.entity_death_time[slot] == 20,
          "full XP hot pool grows for exact three-orb terminal state");

    gm_runtime_destroy(&runtime);
    if (fail) return 1;
    puts("witch_terminal_xp_live: PASS exact=3 product=1 order=4 "
         "particles=20 gamerule=1 capacity_growth=1");
    return 0;
}
