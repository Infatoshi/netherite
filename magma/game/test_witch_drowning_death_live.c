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
        GmRuntime *runtime, int eid, uint64_t seed48) {
    GmMobLive *m = &runtime->mobs;
    gm_mobs_init(m, 0);
    m->active_dimension = 0;
    m->next_id = eid;
    int slot = gm_mobs_spawn_witch(m, 24.5, 220.0, 24.5);
    if (slot <= 0) return -1;
    m->a.health[slot] = 2.0F;
    m->b.health[slot] = 2.0F;
    m->entity_air[slot] = -19;
    m->witch_drinking[slot] = 1;
    m->witch_potion[slot] = EWITCH_SELF_HEALING;
    m->witch_attack_timer[slot] = 32;
    if (!gm_mobs_set_entity_random_state(m, eid, seed48, 0, 0.0))
        return -1;
    gm_world_set_block(runtime->world, 24, 220, 24, 9);
    gm_world_set_block(runtime->world, 24, 221, 24, 9);
    return slot;
}

static int exact_item(
        const GmLiveEnt *item, int eid, int expected_item, int count,
        double x, double y, double z, JavaRandom *math) {
    float hover = (float)(jrand_double(math) * (MC_PI * 2.0));
    float yaw = (float)(jrand_double(math) * 360.0);
    double motion_x = (double)(float)(jrand_double(math)
        * 0.20000000298023224 - 0.10000000149011612);
    double motion_z = (double)(float)(jrand_double(math)
        * 0.20000000298023224 - 0.10000000149011612);
    return item->active && item->eid == eid
        && item->item == expected_item && item->count == count
        && item->meta == 0 && item->x == x && item->y == y && item->z == z
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
        &runtime->clock, 0,
        world_seed, math_seed, next_id, do_mob_loot, drops, 0.0F, 0.0F);
}

int main(void) {
    GmConfig config;
    GmRuntime runtime;
    GmLiveSim drops;
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

    uint64_t world_seed = UINT64_C(987654321);
    uint64_t math_seed = 0;
    int next_id = 1000;
    int slot = stage_witch(&runtime, 700, 0);
    memset(&drops, 0, sizeof drops);
    JavaRandom expected_random = {0};
    JavaRandom expected_math = {0};
    EwitchLootOutcome expected_loot;
    for (int draw = 0; draw < 48; ++draw)
        (void)jrand_float(&expected_random);
    (void)jrand_double(&expected_math);
    float first = jrand_float(&expected_random);
    float second = jrand_float(&expected_random);
    float expected_pitch = (first - second) * 0.2F + 1.0F;
    ewitch_generate_loot(&expected_random, 0, &expected_loot);
    (void)jrand_float(&expected_random);
    CHECK(slot > 0 && expected_loot.count == 1,
          "drowning Witch fixture selects one Looting-0 stack");

    tick(&runtime, &world_seed, &math_seed, &next_id, 1, &drops);
    EwStore *s = store(&runtime.mobs);
    CHECK(s->alive[slot] && s->health[slot] == 0.0F
              && runtime.mobs.entity_dead[slot]
              && runtime.mobs.entity_death_time[slot] == 1
              && runtime.mobs.entity_air[slot] == 0
              && runtime.mobs.entity_hurt_time[slot] == 9
              && runtime.mobs.entity_hurt_resistant[slot] == 19
              && runtime.mobs.entity_recently_hit[slot] == 0
              && runtime.mobs.entity_attacking_player[slot] == 0,
          "ordinary drowning enters the exact no-credit death window");
    CHECK(drops.n_active == 1
              && exact_item(
                  &drops.ents[0], 1000, expected_loot.item[0],
                  expected_loot.quantity[0], 24.5, 220.0, 24.5,
                  &expected_math)
              && drops.ents[0].item != 373,
          "drowning emits exact table loot but never the held potion");
    CHECK(runtime.mobs.entity_random[slot].random.seed
                  == expected_random.seed
              && math_seed == expected_math.seed && next_id == 1001
              && runtime.mobs.next_id == 1001
              && runtime.mobs.next_orb_id == 1001,
          "drowning commits exact entity, Math, and global-ID cursors");
    GmMobEvent event;
    CHECK(gm_mobs_event_count(&runtime.mobs) == 3
              && gm_mobs_event_get(&runtime.mobs, 0, &event)
              && event.kind == GM_MOB_EVENT_ENTITY_STATUS
              && event.eid == 700 && event.data == 2
              && gm_mobs_event_get(&runtime.mobs, 1, &event)
              && event.kind == GM_MOB_EVENT_SOUND
              && event.data == GM_MOB_SOUND_WITCH_DEATH
              && event.pitch == expected_pitch
              && gm_mobs_event_get(&runtime.mobs, 2, &event)
              && event.kind == GM_MOB_EVENT_ENTITY_STATUS
              && event.data == 3,
          "drowning orders hurt status, death sound, and death status");

    double terminal_x = s->x[slot];
    double terminal_y = s->y[slot];
    double terminal_z = s->z[slot];
    runtime.mobs.entity_death_time[slot] = 19;
    uint64_t terminal_math = math_seed;
    int terminal_id = next_id;
    tick(&runtime, &world_seed, &math_seed, &next_id, 1, &drops);
    CHECK(runtime.mobs.xp_orbs[0].xpValue == 0
              && math_seed == terminal_math
              && next_id == terminal_id,
          "no-credit terminal boundary emits no XP or constructor draws");
    JavaGaussianRandom expected_terminal;
    jrand_gaussian_set_state(
        &expected_terminal, expected_random.seed, 0, 0.0);
    GmMobTerminalParticles particles;
    int particles_exact = gm_mobs_terminal_particle_get(
        &runtime.mobs, 0, &particles);
    for (int i = 0; i < 20 && particles_exact; ++i) {
        double vx = jrand_gaussian_next(&expected_terminal) * 0.02;
        double vy = jrand_gaussian_next(&expected_terminal) * 0.02;
        double vz = jrand_gaussian_next(&expected_terminal) * 0.02;
        float dx = jrand_float(&expected_terminal.random) * 0.6F * 2.0F;
        float dy = jrand_float(&expected_terminal.random) * 1.95F;
        float dz = jrand_float(&expected_terminal.random) * 0.6F * 2.0F;
        const GmTerminalParticle *actual = &particles.particles[i];
        particles_exact = actual->x
                == terminal_x + (double)dx - (double)0.6F
            && actual->y == terminal_y + (double)dy
            && actual->z == terminal_z + (double)dz - (double)0.6F
            && actual->vx == vx && actual->vy == vy && actual->vz == vz;
    }
    (void)jrand_float(&expected_terminal.random);
    CHECK(particles_exact
              && runtime.mobs.entity_random[slot].random.seed
                  == expected_terminal.random.seed
              && runtime.mobs.entity_random[slot].have_next_next_gaussian
                  == expected_terminal.have_next_next_gaussian
              && runtime.mobs.entity_random[slot].next_next_gaussian
                  == expected_terminal.next_next_gaussian,
          "no-credit terminal particles preserve exact Gaussian state");

    /* doMobLoot=false skips the whole table while retaining drowning feedback. */
    world_seed = UINT64_C(12345);
    math_seed = UINT64_C(24680);
    next_id = 2000;
    slot = stage_witch(&runtime, 701, 12345);
    memset(&drops, 0, sizeof drops);
    expected_random = (JavaRandom){12345};
    expected_math = (JavaRandom){24680};
    for (int draw = 0; draw < 48; ++draw)
        (void)jrand_float(&expected_random);
    (void)jrand_double(&expected_math);
    (void)jrand_float(&expected_random);
    (void)jrand_float(&expected_random);
    (void)jrand_float(&expected_random);
    tick(&runtime, &world_seed, &math_seed, &next_id, 0, &drops);
    CHECK(slot > 0 && runtime.mobs.entity_dead[slot]
              && runtime.mobs.entity_death_time[slot] == 1
              && drops.n_active == 0
              && runtime.mobs.entity_random[slot].random.seed
                  == expected_random.seed
              && math_seed == expected_math.seed && next_id == 2000,
          "doMobLoot=false skips table and constructors, not death feedback");

    /* A full fixed item pool rejects the causal pulse without partial state. */
    world_seed = UINT64_C(54321);
    math_seed = UINT64_C(13579);
    next_id = 3000;
    slot = stage_witch(&runtime, 702, 0);
    memset(&drops, 0, sizeof drops);
    for (int i = 0; i < GM_LIVE_MAX; ++i)
        drops.ents[i].active = 1;
    drops.n_active = GM_LIVE_MAX;
    drops.item_spawn_limit = GM_LIVE_MAX;
    tick(&runtime, &world_seed, &math_seed, &next_id, 1, &drops);
    s = store(&runtime.mobs);
    CHECK(slot > 0 && s->health[slot] == 2.0F
              && !runtime.mobs.entity_dead[slot]
              && runtime.mobs.entity_air[slot] == -19
              && runtime.mobs.witch_attack_timer[slot] == 32
              && runtime.mobs.entity_random[slot].random.seed == 0
              && math_seed == UINT64_C(13579) && next_id == 3000
              && gm_mobs_event_count(&runtime.mobs) == 0,
          "full item pool rejects the drowning death atomically");

    gm_runtime_destroy(&runtime);
    if (fail) return 1;
    puts("witch_drowning_death_live: PASS table_loot=1 held_drop=0 "
         "xp=0 product=1 particles=20 gamerule=1 capacity_atomic=1");
    return 0;
}
