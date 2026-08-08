#include "entity_witch.h"
#include "game/player_movement_audio.h"
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

static void consume_visible_effect_particles(JavaRandom *random) {
    if (jrand_next(random, 1) == 0) return;
    (void)jrand_double(random);
    (void)jrand_double(random);
    (void)jrand_double(random);
}

static int stage_witch(GmMobLive *m, int eid, uint64_t seed48) {
    gm_mobs_init(m, 0);
    m->active_dimension = 0;
    m->next_id = eid;
    int slot = gm_mobs_spawn_witch(m, 24.5, 220.0, 24.5);
    if (slot <= 0) return -1;
    m->a.health[slot] = 1.0F;
    m->b.health[slot] = 1.0F;
    m->fire_ticks[slot] = 20;
    m->witch_drinking[slot] = 1;
    m->witch_potion[slot] = EWITCH_SELF_HEALING;
    m->witch_attack_timer[slot] = 32;
    if (!gm_mobs_set_entity_random_state(m, eid, seed48, 0, 0.0))
        return -1;
    return slot;
}

static int stage_lava_witch(
        GmRuntime *runtime, int eid, uint64_t seed48) {
    int slot = stage_witch(&runtime->mobs, eid, seed48);
    if (slot <= 0) return slot;
    runtime->mobs.a.health[slot] = 4.0F;
    runtime->mobs.b.health[slot] = 4.0F;
    runtime->mobs.fire_ticks[slot] = 0;
    runtime->mobs.entity_fall_distance[slot] = 2.5F;
    runtime->mobs.persistence_required[slot] = 1;
    gm_world_set_block(runtime->world, 24, 220, 24, 11);
    return slot;
}

static int stage_fire_witch(
        GmRuntime *runtime, int eid, uint64_t seed48) {
    int slot = stage_witch(&runtime->mobs, eid, seed48);
    if (slot <= 0) return slot;
    runtime->mobs.fire_ticks[slot] = -1;
    runtime->mobs.persistence_required[slot] = 1;
    gm_world_set_block(runtime->world, 24, 219, 24, 87);
    gm_world_set_block(runtime->world, 24, 220, 24, 51);
    return slot;
}

static int stage_cactus_witch(
        GmRuntime *runtime, int eid, uint64_t seed48) {
    int slot = stage_witch(&runtime->mobs, eid, seed48);
    if (slot <= 0) return slot;
    runtime->mobs.a.y[slot] = 220.9375;
    runtime->mobs.b.y[slot] = 220.9375;
    runtime->mobs.fire_ticks[slot] = -1;
    runtime->mobs.persistence_required[slot] = 1;
    gm_world_set_block(runtime->world, 24, 219, 24, 12);
    gm_world_set_block(runtime->world, 24, 220, 24, 81);
    return slot;
}

static int stage_water_fire_witch(
        GmRuntime *runtime, int eid, uint64_t seed48) {
    int slot = stage_witch(&runtime->mobs, eid, seed48);
    if (slot <= 0) return slot;
    runtime->mobs.a.x[slot] = 24.8;
    runtime->mobs.b.x[slot] = 24.8;
    runtime->mobs.fire_ticks[slot] = -1;
    runtime->mobs.persistence_required[slot] = 1;
    gm_world_set_block(runtime->world, 24, 220, 24, 9);
    gm_world_set_block(runtime->world, 25, 219, 24, 87);
    gm_world_set_block(runtime->world, 25, 220, 24, 51);
    return slot;
}

static int stage_rain_fire_witch(
        GmRuntime *runtime, int eid, uint64_t seed48, int roofed) {
    int slot = stage_fire_witch(runtime, eid, seed48);
    if (slot <= 0) return slot;
    gm_world_set_block(runtime->world, 24, 222, 24, roofed ? 1 : 0);
    gm_world_clock_set_weather(&runtime->clock, 1, 0, 100, 100);
    return slot;
}

static int stage_flow_water_witch(
        GmRuntime *runtime, int eid, uint64_t seed48) {
    int slot = stage_witch(&runtime->mobs, eid, seed48);
    if (slot <= 0) return slot;
    runtime->mobs.a.x[slot] = 24.8;
    runtime->mobs.b.x[slot] = 24.8;
    for (int x = 23; x <= 25; ++x)
        for (int z = 23; z <= 25; ++z)
            for (int y = 219; y <= 220; ++y)
                gm_world_set_block(
                    runtime->world, x, y, z, y == 219 ? 1 : 0);
    gm_world_set_block_meta(runtime->world, 24, 220, 24, 8, 1);
    gm_world_set_block(runtime->world, 23, 220, 24, 9);
    gm_world_set_block(runtime->world, 25, 220, 24, 11);
    return slot;
}

static int stage_water_entry_witch(
        GmRuntime *runtime, int eid, uint64_t seed48) {
    int slot = stage_witch(&runtime->mobs, eid, seed48);
    if (slot <= 0) return slot;
    runtime->mobs.a.health[slot] = 20.0F;
    runtime->mobs.b.health[slot] = 20.0F;
    runtime->mobs.a.vx[slot] = runtime->mobs.b.vx[slot] = 0.125;
    runtime->mobs.a.vy[slot] = runtime->mobs.b.vy[slot] = -0.25;
    runtime->mobs.a.vz[slot] = runtime->mobs.b.vz[slot] = 0.375;
    runtime->mobs.fire_ticks[slot] = 160;
    runtime->mobs.entity_fall_distance[slot] = 2.5F;
    runtime->mobs.entity_ticks_existed[slot] = 1;
    runtime->mobs.entity_in_water[slot] = 0;
    runtime->mobs.persistence_required[slot] = 1;
    for (int x = 23; x <= 25; ++x)
        for (int z = 23; z <= 25; ++z)
            for (int y = 219; y <= 222; ++y)
                gm_world_set_block(
                    runtime->world, x, y, z, y == 219 ? 1 : 0);
    gm_world_set_block(runtime->world, 24, 220, 24, 9);
    return slot;
}

static int stage_in_wall_witch(
        GmRuntime *runtime, int eid, uint64_t seed48, int block_id) {
    int slot = stage_witch(&runtime->mobs, eid, seed48);
    if (slot <= 0) return slot;
    runtime->mobs.a.x[slot] = runtime->mobs.b.x[slot] = 40.5;
    runtime->mobs.a.z[slot] = runtime->mobs.b.z[slot] = 40.5;
    runtime->mobs.fire_ticks[slot] = -1;
    runtime->mobs.persistence_required[slot] = 1;
    gm_world_set_block(runtime->world, 40, 221, 40, block_id);
    return slot;
}

static int stage_fall_witch(
        GmRuntime *runtime, int eid, uint64_t seed48,
        int support_block, float fall_distance) {
    int slot = stage_witch(&runtime->mobs, eid, seed48);
    if (slot <= 0) return slot;
    runtime->mobs.a.x[slot] = runtime->mobs.b.x[slot] = 8.5;
    runtime->mobs.a.z[slot] = runtime->mobs.b.z[slot] = 24.5;
    runtime->mobs.a.vy[slot] = runtime->mobs.b.vy[slot] = -0.1;
    runtime->mobs.a.on_ground[slot] = runtime->mobs.b.on_ground[slot] = 0;
    runtime->mobs.fire_ticks[slot] = -1;
    runtime->mobs.entity_fall_distance[slot] = fall_distance;
    runtime->mobs.persistence_required[slot] = 1;
    gm_world_set_block(runtime->world, 8, 219, 24, support_block);
    gm_world_set_block(runtime->world, 8, 220, 24, 0);
    gm_world_set_block(runtime->world, 8, 221, 24, 0);
    gm_world_set_block(runtime->world, 8, 222, 24, 0);
    return slot;
}

static int exact_item_at(
        const GmLiveEnt *item, int eid, int expected_item, int count,
        double expected_x, double expected_y, double expected_z,
        JavaRandom *math) {
    float hover = (float)(jrand_double(math) * (MC_PI * 2.0));
    float yaw = (float)(jrand_double(math) * 360.0);
    double motion_x = (double)(float)(jrand_double(math)
        * 0.20000000298023224 - 0.10000000149011612);
    double motion_z = (double)(float)(jrand_double(math)
        * 0.20000000298023224 - 0.10000000149011612);
    return item->active && item->eid == eid
        && item->item == expected_item && item->count == count
        && item->meta == 0
        && item->x == expected_x && item->y == expected_y
        && item->z == expected_z
        && item->mx == motion_x && item->my == 0.20000000298023224
        && item->mz == motion_z && item->yaw == yaw
        && item->has_hover_start && item->hover_start == hover
        && item->age == 0 && item->pickup_delay == 10
        && item->health == 5 && item->lifespan == 6000;
}

static int exact_item(
        const GmLiveEnt *item, int eid, int expected_item, int count,
        JavaRandom *math) {
    return exact_item_at(
        item, eid, expected_item, count, 24.5, 220.0, 24.5, math);
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
        &runtime->clock, runtime->mob_griefing,
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
    int slot = stage_witch(&runtime.mobs, 700, 4);
    memset(&drops, 0, sizeof drops);
    JavaRandom expected_random = {4};
    JavaRandom expected_math = {0};
    EwitchLootOutcome expected_loot;
    (void)jrand_double(&expected_random);
    (void)jrand_double(&expected_math);
    float first = jrand_float(&expected_random);
    float second = jrand_float(&expected_random);
    float expected_pitch = (first - second) * 0.2F + 1.0F;
    ewitch_generate_loot(&expected_random, 0, &expected_loot);
    (void)jrand_float(&expected_random);
    CHECK(slot > 0 && expected_loot.count == 3,
          "burning Witch fixture selects three Looting-0 stacks");

    tick(&runtime, &world_seed, &math_seed, &next_id, 1, &drops);
    EwStore *s = store(&runtime.mobs);
    CHECK(s->alive[slot] && s->health[slot] == 0.0F
              && runtime.mobs.entity_dead[slot]
              && runtime.mobs.entity_death_time[slot] == 1
              && runtime.mobs.entity_hurt_time[slot] == 9
              && runtime.mobs.entity_hurt_resistant[slot] == 19
              && runtime.mobs.fire_ticks[slot] == 19
              && runtime.mobs.entity_recently_hit[slot] == 0
              && runtime.mobs.entity_attacking_player[slot] == 0,
          "ON_FIRE pulse enters the exact no-credit death window");
    CHECK(drops.n_active == 3,
          "burning Witch emits every exact table stack");
    for (int i = 0; i < expected_loot.count; ++i)
        CHECK(exact_item(
                  &drops.ents[i], 1000 + i,
                  expected_loot.item[i], expected_loot.quantity[i],
                  &expected_math)
                  && drops.ents[i].item != 373,
              "burning table drop is exact and excludes held potion");
    CHECK(runtime.mobs.entity_random[slot].random.seed
                  == expected_random.seed
              && math_seed == expected_math.seed && next_id == 1003
              && runtime.mobs.next_id == 1003
              && runtime.mobs.next_orb_id == 1003,
          "burning death commits exact Witch, Math, and ID cursors");
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
          "burning death orders status, sound, and death status");

    double terminal_x = s->x[slot];
    double terminal_y = s->y[slot];
    double terminal_z = s->z[slot];
    runtime.mobs.entity_death_time[slot] = 19;
    uint64_t terminal_math = math_seed;
    int terminal_id = next_id;
    tick(&runtime, &world_seed, &math_seed, &next_id, 1, &drops);
    CHECK(runtime.mobs.xp_orbs[0].xpValue == 0
              && math_seed == terminal_math && next_id == terminal_id,
          "source-less burning terminal boundary emits no XP");
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
                  == expected_terminal.random.seed,
          "burning terminal particles preserve the exact Witch RNG tail");

    /* Fire Resistance suppresses only the periodic attack; fire still ages. */
    math_seed = UINT64_C(24680);
    next_id = 2000;
    slot = stage_witch(&runtime.mobs, 701, 12345);
    runtime.mobs.entity_effect_count[slot] = 1;
    runtime.mobs.entity_effects[slot][0] = (PtMobEffect){12, 2, 0};
    memset(&drops, 0, sizeof drops);
    expected_random = (JavaRandom){12345};
    consume_visible_effect_particles(&expected_random);
    (void)jrand_int_bound(&expected_random, 1000);
    tick(&runtime, &world_seed, &math_seed, &next_id, 1, &drops);
    s = store(&runtime.mobs);
    CHECK(s->health[slot] == 1.0F && !runtime.mobs.entity_dead[slot]
              && runtime.mobs.fire_ticks[slot] == 19
              && runtime.mobs.entity_random[slot].random.seed
                  == expected_random.seed
              && math_seed == UINT64_C(24680) && next_id == 2000
              && drops.n_active == 0 && gm_mobs_event_count(&runtime.mobs) == 0,
          "Fire Resistance suppresses ON_FIRE damage without freezing fire");

    /* Gamerule suppression retains feedback and skips table RNG. */
    math_seed = UINT64_C(86420);
    next_id = 3000;
    slot = stage_witch(&runtime.mobs, 702, 12345);
    memset(&drops, 0, sizeof drops);
    expected_random = (JavaRandom){12345};
    expected_math = (JavaRandom){86420};
    (void)jrand_double(&expected_random);
    (void)jrand_double(&expected_math);
    (void)jrand_float(&expected_random);
    (void)jrand_float(&expected_random);
    (void)jrand_float(&expected_random);
    tick(&runtime, &world_seed, &math_seed, &next_id, 0, &drops);
    CHECK(runtime.mobs.entity_dead[slot] && drops.n_active == 0
              && runtime.mobs.entity_random[slot].random.seed
                  == expected_random.seed
              && math_seed == expected_math.seed && next_id == 3000,
          "doMobLoot=false skips burning table and constructors only");

    /* A full item store leaves the whole periodic boundary retryable. */
    math_seed = UINT64_C(13579);
    next_id = 4000;
    slot = stage_witch(&runtime.mobs, 703, 4);
    memset(&drops, 0, sizeof drops);
    for (int i = 0; i < GM_LIVE_MAX; ++i)
        drops.ents[i].active = 1;
    drops.n_active = GM_LIVE_MAX;
    drops.item_spawn_limit = GM_LIVE_MAX;
    tick(&runtime, &world_seed, &math_seed, &next_id, 1, &drops);
    s = store(&runtime.mobs);
    CHECK(s->health[slot] == 1.0F && !runtime.mobs.entity_dead[slot]
              && runtime.mobs.fire_ticks[slot] == 20
              && runtime.mobs.entity_hurt_time[slot] == 0
              && runtime.mobs.entity_hurt_resistant[slot] == 0
              && runtime.mobs.entity_random[slot].random.seed == 4
              && runtime.mobs.witch_attack_timer[slot] == 32
              && math_seed == UINT64_C(13579) && next_id == 4000
              && gm_mobs_event_count(&runtime.mobs) == 0,
          "full item pool preserves the complete burning boundary");

    /* Actual World.isMaterialInBB lava contact shares the source-less death
     * body, then applies the 300-tick fire floor before timer/death aging. */
    math_seed = 0;
    next_id = 5000;
    slot = stage_lava_witch(&runtime, 704, 4);
    memset(&drops, 0, sizeof drops);
    expected_random = (JavaRandom){4};
    expected_math = (JavaRandom){0};
    (void)jrand_double(&expected_random);
    (void)jrand_double(&expected_math);
    first = jrand_float(&expected_random);
    second = jrand_float(&expected_random);
    expected_pitch = (first - second) * 0.2F + 1.0F;
    ewitch_generate_loot(&expected_random, 0, &expected_loot);
    (void)jrand_float(&expected_random);
    tick(&runtime, &world_seed, &math_seed, &next_id, 1, &drops);
    s = store(&runtime.mobs);
    CHECK(slot > 0 && s->health[slot] == 0.0F
              && runtime.mobs.entity_dead[slot]
              && runtime.mobs.entity_death_time[slot] == 1
              && runtime.mobs.entity_hurt_time[slot] == 9
              && runtime.mobs.entity_hurt_resistant[slot] == 19
              && runtime.mobs.fire_ticks[slot] == 301,
          "lava contact enters the exact no-credit death window");
    CHECK(drops.n_active == expected_loot.count
              && expected_loot.count == 3,
          "lava Witch emits every exact table stack");
    for (int i = 0; i < expected_loot.count; ++i)
        CHECK(exact_item(
                  &drops.ents[i], 5000 + i,
                  expected_loot.item[i], expected_loot.quantity[i],
                  &expected_math)
                  && drops.ents[i].item != 373,
              "lava table drop is exact and excludes held potion");
    CHECK(runtime.mobs.entity_random[slot].random.seed
                  == expected_random.seed
              && math_seed == expected_math.seed && next_id == 5003,
          "lava death commits exact Witch, Math, and ID cursors");
    CHECK(gm_mobs_event_count(&runtime.mobs) == 3
              && gm_mobs_event_get(&runtime.mobs, 0, &event)
              && event.kind == GM_MOB_EVENT_ENTITY_STATUS && event.data == 2
              && gm_mobs_event_get(&runtime.mobs, 1, &event)
              && event.kind == GM_MOB_EVENT_SOUND
              && event.data == GM_MOB_SOUND_WITCH_DEATH
              && event.pitch == expected_pitch
              && gm_mobs_event_get(&runtime.mobs, 2, &event)
              && event.kind == GM_MOB_EVENT_ENTITY_STATUS && event.data == 3,
          "lava death orders status, sound, and death status");

    /* A due ON_FIRE pulse is the fresh hit; LAVA then applies only the raw
     * damage difference under the same immunity window and restores fire 300. */
    math_seed = UINT64_C(86420);
    next_id = 5500;
    slot = stage_lava_witch(&runtime, 707, 12345);
    runtime.mobs.fire_ticks[slot] = 20;
    memset(&drops, 0, sizeof drops);
    expected_random = (JavaRandom){12345};
    expected_math = (JavaRandom){86420};
    (void)jrand_double(&expected_random);
    (void)jrand_double(&expected_math);
    (void)jrand_float(&expected_random);
    (void)jrand_float(&expected_random);
    (void)jrand_float(&expected_random);
    tick(&runtime, &world_seed, &math_seed, &next_id, 0, &drops);
    s = store(&runtime.mobs);
    CHECK(s->health[slot] == 0.0F && runtime.mobs.entity_dead[slot]
              && runtime.mobs.entity_death_time[slot] == 1
              && runtime.mobs.entity_hurt_time[slot] == 9
              && runtime.mobs.entity_hurt_resistant[slot] == 19
              && runtime.mobs.entity_last_damage[slot] == 4.0F
              && runtime.mobs.fire_ticks[slot] == 301
              && runtime.mobs.entity_random[slot].random.seed
                  == expected_random.seed
              && math_seed == expected_math.seed && next_id == 5500
              && drops.n_active == 0
              && gm_mobs_event_count(&runtime.mobs) == 3
              && gm_mobs_event_get(&runtime.mobs, 1, &event)
              && event.kind == GM_MOB_EVENT_SOUND
              && event.data == GM_MOB_SOUND_WITCH_HURT,
          "ON_FIRE precedes LAVA with one fresh hurt feedback sequence");

    /* Fire Resistance rejects LAVA damage but not setFire(15). */
    math_seed = UINT64_C(24680);
    next_id = 6000;
    slot = stage_lava_witch(&runtime, 705, 12345);
    runtime.mobs.entity_effect_count[slot] = 1;
    runtime.mobs.entity_effects[slot][0] = (PtMobEffect){12, 2, 0};
    memset(&drops, 0, sizeof drops);
    tick(&runtime, &world_seed, &math_seed, &next_id, 1, &drops);
    s = store(&runtime.mobs);
    expected_random = (JavaRandom){12345};
    consume_visible_effect_particles(&expected_random);
    (void)jrand_int_bound(&expected_random, 1000);
    (void)jrand_float(&expected_random);
    CHECK(s->health[slot] == 4.0F && !runtime.mobs.entity_dead[slot]
              && runtime.mobs.fire_ticks[slot] == 301
              && runtime.mobs.entity_random[slot].random.seed
                  == expected_random.seed
              && math_seed == UINT64_C(24680) && next_id == 6000
              && drops.n_active == 0 && gm_mobs_event_count(&runtime.mobs) == 0,
          "Fire Resistance suppresses lava damage but retains the fire floor");

    /* Capacity failure precedes lava's fire and fall-distance side effects. */
    math_seed = UINT64_C(13579);
    next_id = 7000;
    slot = stage_lava_witch(&runtime, 706, 4);
    memset(&drops, 0, sizeof drops);
    for (int i = 0; i < GM_LIVE_MAX; ++i)
        drops.ents[i].active = 1;
    drops.n_active = GM_LIVE_MAX;
    drops.item_spawn_limit = GM_LIVE_MAX;
    tick(&runtime, &world_seed, &math_seed, &next_id, 1, &drops);
    s = store(&runtime.mobs);
    CHECK(s->health[slot] == 4.0F && !runtime.mobs.entity_dead[slot]
              && runtime.mobs.fire_ticks[slot] == 0
              && runtime.mobs.entity_fall_distance[slot] == 2.5F
              && runtime.mobs.entity_hurt_time[slot] == 0
              && runtime.mobs.entity_hurt_resistant[slot] == 0
              && runtime.mobs.entity_random[slot].random.seed == 4
              && runtime.mobs.witch_attack_timer[slot] == 32
              && math_seed == UINT64_C(13579) && next_id == 7000
              && gm_mobs_event_count(&runtime.mobs) == 0,
          "full item pool preserves the complete lava boundary");

    math_seed = UINT64_C(97531);
    next_id = 8000;
    slot = stage_lava_witch(&runtime, 708, 4);
    runtime.mobs.fire_ticks[slot] = 20;
    memset(&drops, 0, sizeof drops);
    for (int i = 0; i < GM_LIVE_MAX; ++i)
        drops.ents[i].active = 1;
    drops.n_active = GM_LIVE_MAX;
    drops.item_spawn_limit = GM_LIVE_MAX;
    tick(&runtime, &world_seed, &math_seed, &next_id, 1, &drops);
    s = store(&runtime.mobs);
    CHECK(s->health[slot] == 4.0F && !runtime.mobs.entity_dead[slot]
              && runtime.mobs.fire_ticks[slot] == 20
              && runtime.mobs.entity_fall_distance[slot] == 2.5F
              && runtime.mobs.entity_hurt_time[slot] == 0
              && runtime.mobs.entity_hurt_resistant[slot] == 0
              && runtime.mobs.entity_random[slot].random.seed == 4
              && runtime.mobs.witch_attack_timer[slot] == 32
              && math_seed == UINT64_C(97531) && next_id == 8000
              && gm_mobs_event_count(&runtime.mobs) == 0,
          "full pool preserves the combined ON_FIRE-before-LAVA boundary");

    /* Entity.move checks block fire after the base phase and travel. A fresh
     * lethal contact therefore has not aged death or combat timers yet. */
    math_seed = 0;
    next_id = 9000;
    slot = stage_fire_witch(&runtime, 709, 11);
    memset(&drops, 0, sizeof drops);
    expected_random = (JavaRandom){11};
    expected_math = (JavaRandom){0};
    (void)jrand_int_bound(&expected_random, 1000);
    /* EntityWitch.onLivingUpdate's status-particle trial precedes travel. */
    (void)jrand_float(&expected_random);
    (void)jrand_double(&expected_random);
    (void)jrand_double(&expected_math);
    first = jrand_float(&expected_random);
    second = jrand_float(&expected_random);
    expected_pitch = (first - second) * 0.2F + 1.0F;
    ewitch_generate_loot(&expected_random, 0, &expected_loot);
    tick(&runtime, &world_seed, &math_seed, &next_id, 1, &drops);
    s = store(&runtime.mobs);
    CHECK(slot > 0 && s->health[slot] == 0.0F
              && runtime.mobs.entity_dead[slot]
              && runtime.mobs.entity_death_time[slot] == 0
              && runtime.mobs.entity_hurt_time[slot] == 10
              && runtime.mobs.entity_hurt_resistant[slot] == 20
              && runtime.mobs.entity_last_damage[slot] == 1.0F
              && runtime.mobs.fire_ticks[slot] == 160
              && runtime.mobs.entity_recently_hit[slot] == 0
              && runtime.mobs.entity_attacking_player[slot] == 0,
          "block-fire contact enters the exact post-movement death window");
    CHECK(drops.n_active == expected_loot.count
              && expected_loot.count == 3,
          "block-fire Witch emits every exact table stack");
    for (int i = 0; i < expected_loot.count; ++i)
        CHECK(exact_item(
                  &drops.ents[i], 9000 + i,
                  expected_loot.item[i], expected_loot.quantity[i],
                  &expected_math),
              "block-fire table drop has exact constructor state");
    CHECK(runtime.mobs.entity_random[slot].random.seed
                  == expected_random.seed
              && math_seed == expected_math.seed && next_id == 9003
              && gm_mobs_event_count(&runtime.mobs) == 3
              && gm_mobs_event_get(&runtime.mobs, 0, &event)
              && event.kind == GM_MOB_EVENT_ENTITY_STATUS && event.data == 2
              && gm_mobs_event_get(&runtime.mobs, 1, &event)
              && event.kind == GM_MOB_EVENT_SOUND
              && event.data == GM_MOB_SOUND_WITCH_DEATH
              && event.pitch == expected_pitch
              && gm_mobs_event_get(&runtime.mobs, 2, &event)
              && event.kind == GM_MOB_EVENT_ENTITY_STATUS && event.data == 3,
          "block-fire death commits exact cursors and feedback order");

    uint64_t fire_death_random = runtime.mobs.entity_random[slot].random.seed;
    uint64_t fire_death_math = math_seed;
    int fire_death_id = next_id;
    tick(&runtime, &world_seed, &math_seed, &next_id, 1, &drops);
    JavaRandom expected_dead_random = {fire_death_random};
    (void)jrand_float(&expected_dead_random);
    s = store(&runtime.mobs);
    CHECK(s->health[slot] == 0.0F && runtime.mobs.entity_dead[slot]
              && runtime.mobs.entity_death_time[slot] == 1
              && runtime.mobs.entity_hurt_time[slot] == 9
              && runtime.mobs.entity_hurt_resistant[slot] == 19
              && runtime.mobs.fire_ticks[slot] == 160
              && runtime.mobs.entity_random[slot].random.seed
                  == expected_dead_random.seed
              && math_seed == fire_death_math && next_id == fire_death_id
              && gm_mobs_event_count(&runtime.mobs) == 3,
          "dead Witch repeats contact only after aging its base-phase state");

    /* Fire Resistance suppresses contact damage, but Entity.move still
     * ignites the cold -1 fire counter for eight seconds. */
    math_seed = UINT64_C(24680);
    next_id = 10000;
    slot = stage_fire_witch(&runtime, 710, 12345);
    runtime.mobs.entity_effect_count[slot] = 1;
    runtime.mobs.entity_effects[slot][0] = (PtMobEffect){12, 2, 0};
    memset(&drops, 0, sizeof drops);
    expected_random = (JavaRandom){12345};
    consume_visible_effect_particles(&expected_random);
    (void)jrand_int_bound(&expected_random, 1000);
    (void)jrand_float(&expected_random);
    tick(&runtime, &world_seed, &math_seed, &next_id, 1, &drops);
    s = store(&runtime.mobs);
    CHECK(s->health[slot] == 1.0F && !runtime.mobs.entity_dead[slot]
              && runtime.mobs.fire_ticks[slot] == 160
              && runtime.mobs.entity_random[slot].random.seed
                  == expected_random.seed
              && math_seed == UINT64_C(24680) && next_id == 10000
              && drops.n_active == 0 && gm_mobs_event_count(&runtime.mobs) == 0,
          "Fire Resistance suppresses contact damage without suppressing ignition");

    /* An equal hit inside the active immunity window is rejected before
     * feedback and RNG, while ignition remains an independent move tail. */
    math_seed = UINT64_C(86420);
    next_id = 11000;
    slot = stage_fire_witch(&runtime, 711, 12345);
    runtime.mobs.entity_hurt_resistant[slot] = 20;
    runtime.mobs.entity_last_damage[slot] = 1.0F;
    memset(&drops, 0, sizeof drops);
    expected_random = (JavaRandom){12345};
    (void)jrand_int_bound(&expected_random, 1000);
    (void)jrand_float(&expected_random);
    tick(&runtime, &world_seed, &math_seed, &next_id, 1, &drops);
    s = store(&runtime.mobs);
    CHECK(s->health[slot] == 1.0F && !runtime.mobs.entity_dead[slot]
              && runtime.mobs.entity_hurt_time[slot] == 0
              && runtime.mobs.entity_hurt_resistant[slot] == 19
              && runtime.mobs.fire_ticks[slot] == 160
              && runtime.mobs.entity_random[slot].random.seed
                  == expected_random.seed
              && math_seed == UINT64_C(86420) && next_id == 11000
              && drops.n_active == 0 && gm_mobs_event_count(&runtime.mobs) == 0,
          "hurt immunity rejects contact damage but not ignition");

    /* Capacity rejection occurs at the contact boundary: the already-run
     * base/travel work remains committed, but damage, fire, and cursors do not. */
    math_seed = UINT64_C(97531);
    next_id = 12000;
    slot = stage_fire_witch(&runtime, 712, 12345);
    memset(&drops, 0, sizeof drops);
    for (int i = 0; i < GM_LIVE_MAX; ++i)
        drops.ents[i].active = 1;
    drops.n_active = GM_LIVE_MAX;
    drops.item_spawn_limit = GM_LIVE_MAX;
    expected_random = (JavaRandom){12345};
    (void)jrand_int_bound(&expected_random, 1000);
    (void)jrand_float(&expected_random);
    tick(&runtime, &world_seed, &math_seed, &next_id, 1, &drops);
    s = store(&runtime.mobs);
    CHECK(s->health[slot] == 1.0F && !runtime.mobs.entity_dead[slot]
              && runtime.mobs.fire_ticks[slot] == -1
              && runtime.mobs.entity_hurt_time[slot] == 0
              && runtime.mobs.entity_hurt_resistant[slot] == 0
              && runtime.mobs.entity_random[slot].random.seed
                  == expected_random.seed
              && math_seed == UINT64_C(97531) && next_id == 12000
              && gm_mobs_event_count(&runtime.mobs) == 0,
          "full item pool preserves the block-fire contact boundary");

    /* doBlockCollisions reaches a cactus callback at the inset collision-top
     * boundary before the flammable tail. Like IN_FIRE, it lands after this
     * tick's base timer/death aging. */
    math_seed = 0;
    next_id = 13000;
    slot = stage_cactus_witch(&runtime, 713, 11);
    memset(&drops, 0, sizeof drops);
    expected_random = (JavaRandom){11};
    expected_math = (JavaRandom){0};
    (void)jrand_int_bound(&expected_random, 1000);
    (void)jrand_float(&expected_random);
    (void)jrand_double(&expected_random);
    (void)jrand_double(&expected_math);
    first = jrand_float(&expected_random);
    second = jrand_float(&expected_random);
    expected_pitch = (first - second) * 0.2F + 1.0F;
    ewitch_generate_loot(&expected_random, 0, &expected_loot);
    tick(&runtime, &world_seed, &math_seed, &next_id, 1, &drops);
    s = store(&runtime.mobs);
    CHECK(slot > 0 && s->health[slot] == 0.0F
              && runtime.mobs.entity_dead[slot]
              && runtime.mobs.entity_death_time[slot] == 0
              && runtime.mobs.entity_hurt_time[slot] == 10
              && runtime.mobs.entity_hurt_resistant[slot] == 20
              && runtime.mobs.entity_last_damage[slot] == 1.0F
              && runtime.mobs.fire_ticks[slot] == -1,
          "cactus callback enters the exact post-movement death window");
    CHECK(drops.n_active == expected_loot.count
              && expected_loot.count == 3,
          "cactus-killed Witch emits every exact table stack");
    for (int i = 0; i < expected_loot.count; ++i)
        CHECK(exact_item_at(
                  &drops.ents[i], 13000 + i,
                  expected_loot.item[i], expected_loot.quantity[i],
                  24.5, 220.9375, 24.5, &expected_math),
              "cactus table drop has exact constructor state");
    CHECK(runtime.mobs.entity_random[slot].random.seed
                  == expected_random.seed
              && math_seed == expected_math.seed && next_id == 13003
              && gm_mobs_event_count(&runtime.mobs) == 3
              && gm_mobs_event_get(&runtime.mobs, 0, &event)
              && event.kind == GM_MOB_EVENT_ENTITY_STATUS && event.data == 2
              && gm_mobs_event_get(&runtime.mobs, 1, &event)
              && event.kind == GM_MOB_EVENT_SOUND
              && event.data == GM_MOB_SOUND_WITCH_DEATH
              && event.pitch == expected_pitch
              && gm_mobs_event_get(&runtime.mobs, 2, &event)
              && event.kind == GM_MOB_EVENT_ENTITY_STATUS && event.data == 3,
          "cactus death commits exact cursors and feedback order");

    /* The already-run base/travel prefix remains committed when the fixed
     * item store rejects the lethal callback boundary. */
    math_seed = UINT64_C(24680);
    next_id = 14000;
    slot = stage_cactus_witch(&runtime, 714, 12345);
    memset(&drops, 0, sizeof drops);
    for (int i = 0; i < GM_LIVE_MAX; ++i)
        drops.ents[i].active = 1;
    drops.n_active = GM_LIVE_MAX;
    drops.item_spawn_limit = GM_LIVE_MAX;
    expected_random = (JavaRandom){12345};
    (void)jrand_int_bound(&expected_random, 1000);
    (void)jrand_float(&expected_random);
    tick(&runtime, &world_seed, &math_seed, &next_id, 1, &drops);
    s = store(&runtime.mobs);
    CHECK(s->health[slot] == 1.0F && !runtime.mobs.entity_dead[slot]
              && runtime.mobs.entity_hurt_time[slot] == 0
              && runtime.mobs.entity_hurt_resistant[slot] == 0
              && runtime.mobs.entity_random[slot].random.seed
                  == expected_random.seed
              && math_seed == UINT64_C(24680) && next_id == 14000
              && gm_mobs_event_count(&runtime.mobs) == 0,
          "full item pool preserves the cactus callback boundary");

    /* Entity.onEntityUpdate detects water and extinguishes before travel.
     * The same body can still touch FIRE during move, but wetness suppresses
     * ignition while leaving the raw one-point contact damage intact. */
    math_seed = 0;
    next_id = 15000;
    slot = stage_water_fire_witch(&runtime, 715, 11);
    memset(&drops, 0, sizeof drops);
    expected_random = (JavaRandom){11};
    expected_math = (JavaRandom){0};
    (void)jrand_int_bound(&expected_random, 1000);
    (void)jrand_float(&expected_random);
    (void)jrand_double(&expected_random);
    (void)jrand_double(&expected_math);
    first = jrand_float(&expected_random);
    second = jrand_float(&expected_random);
    expected_pitch = (first - second) * 0.2F + 1.0F;
    ewitch_generate_loot(&expected_random, 0, &expected_loot);
    tick(&runtime, &world_seed, &math_seed, &next_id, 1, &drops);
    s = store(&runtime.mobs);
    CHECK(slot > 0 && s->health[slot] == 0.0F
              && runtime.mobs.entity_dead[slot]
              && runtime.mobs.entity_death_time[slot] == 0
              && runtime.mobs.entity_hurt_time[slot] == 10
              && runtime.mobs.entity_hurt_resistant[slot] == 20
              && runtime.mobs.entity_last_damage[slot] == 1.0F
              && runtime.mobs.fire_ticks[slot] == 0
              && runtime.mobs.entity_in_water[slot]
              && runtime.mobs.entity_fall_distance[slot] == 0.0F,
          "water extinguishes before wet block-fire contact");
    CHECK(drops.n_active == expected_loot.count
              && expected_loot.count == 3,
          "wet block-fire Witch emits every exact table stack");
    for (int i = 0; i < expected_loot.count; ++i)
        CHECK(exact_item_at(
                  &drops.ents[i], 15000 + i,
                  expected_loot.item[i], expected_loot.quantity[i],
                  24.8, 220.0, 24.5, &expected_math),
              "wet block-fire table drop has exact constructor state");
    CHECK(runtime.mobs.entity_random[slot].random.seed
                  == expected_random.seed
              && math_seed == expected_math.seed && next_id == 15003
              && gm_mobs_event_count(&runtime.mobs) == 3
              && gm_mobs_event_get(&runtime.mobs, 0, &event)
              && event.kind == GM_MOB_EVENT_ENTITY_STATUS && event.data == 2
              && gm_mobs_event_get(&runtime.mobs, 1, &event)
              && event.kind == GM_MOB_EVENT_SOUND
              && event.data == GM_MOB_SOUND_WITCH_DEATH
              && event.pitch == expected_pitch
              && gm_mobs_event_get(&runtime.mobs, 2, &event)
              && event.kind == GM_MOB_EVENT_ENTITY_STATUS && event.data == 3,
          "wet block-fire death commits exact cursors and feedback order");

    /* Open-sky rain makes both isWet probes true without setting inWater.
     * Living update extinguishes to zero before contact, which still damages
     * but cannot increment or ignite. */
    math_seed = 0;
    next_id = 16000;
    slot = stage_rain_fire_witch(&runtime, 716, 11, 0);
    memset(&drops, 0, sizeof drops);
    expected_random = (JavaRandom){11};
    expected_math = (JavaRandom){0};
    (void)jrand_int_bound(&expected_random, 1000);
    (void)jrand_float(&expected_random);
    (void)jrand_double(&expected_random);
    (void)jrand_double(&expected_math);
    first = jrand_float(&expected_random);
    second = jrand_float(&expected_random);
    expected_pitch = (first - second) * 0.2F + 1.0F;
    ewitch_generate_loot(&expected_random, 0, &expected_loot);
    tick(&runtime, &world_seed, &math_seed, &next_id, 1, &drops);
    s = store(&runtime.mobs);
    CHECK(slot > 0 && s->health[slot] == 0.0F
              && runtime.mobs.entity_dead[slot]
              && runtime.mobs.entity_death_time[slot] == 0
              && runtime.mobs.entity_hurt_time[slot] == 10
              && runtime.mobs.entity_hurt_resistant[slot] == 20
              && runtime.mobs.entity_last_damage[slot] == 1.0F
              && runtime.mobs.fire_ticks[slot] == 0
              && !runtime.mobs.entity_in_water[slot],
          "open rain suppresses cold block-fire ignition");
    CHECK(drops.n_active == expected_loot.count
              && expected_loot.count == 3,
          "rain-wet block-fire Witch emits every exact table stack");
    for (int i = 0; i < expected_loot.count; ++i)
        CHECK(exact_item(
                  &drops.ents[i], 16000 + i,
                  expected_loot.item[i], expected_loot.quantity[i],
                  &expected_math),
              "rain-wet block-fire table drop has exact constructor state");
    CHECK(runtime.mobs.entity_random[slot].random.seed
                  == expected_random.seed
              && math_seed == expected_math.seed && next_id == 16003
              && gm_mobs_event_count(&runtime.mobs) == 3
              && gm_mobs_event_get(&runtime.mobs, 1, &event)
              && event.kind == GM_MOB_EVENT_SOUND
              && event.data == GM_MOB_SOUND_WITCH_DEATH
              && event.pitch == expected_pitch,
          "rain-wet block-fire death commits exact cursors and feedback");

    /* A solid roof above the Witch makes both rain probes false. The same
     * raining clock must therefore retain the ordinary cold ignition tail. */
    math_seed = UINT64_C(86420);
    next_id = 17000;
    slot = stage_rain_fire_witch(&runtime, 717, 12345, 1);
    memset(&drops, 0, sizeof drops);
    expected_random = (JavaRandom){12345};
    expected_math = (JavaRandom){86420};
    (void)jrand_int_bound(&expected_random, 1000);
    (void)jrand_float(&expected_random);
    (void)jrand_double(&expected_random);
    (void)jrand_double(&expected_math);
    first = jrand_float(&expected_random);
    second = jrand_float(&expected_random);
    expected_pitch = (first - second) * 0.2F + 1.0F;
    tick(&runtime, &world_seed, &math_seed, &next_id, 0, &drops);
    s = store(&runtime.mobs);
    CHECK(slot > 0 && s->health[slot] == 0.0F
              && runtime.mobs.entity_dead[slot]
              && runtime.mobs.fire_ticks[slot] == 160
              && !runtime.mobs.entity_in_water[slot]
              && drops.n_active == 0
              && runtime.mobs.entity_random[slot].random.seed
                  == expected_random.seed
              && math_seed == expected_math.seed && next_id == 17000
              && gm_mobs_event_count(&runtime.mobs) == 3
              && gm_mobs_event_get(&runtime.mobs, 1, &event)
              && event.kind == GM_MOB_EVENT_SOUND
              && event.data == GM_MOB_SOUND_WITCH_DEATH
              && event.pitch == expected_pitch,
          "roofed rain leaves ordinary block-fire ignition exact");

    /* A non-first dry-to-water transition plays hostile splash before the
     * exact 26 bubble/splash particle calls. */
    world_seed = UINT64_C(987654321);
    math_seed = UINT64_C(97531);
    next_id = 18000;
    slot = stage_water_entry_witch(&runtime, 719, 1357);
    memset(&drops, 0, sizeof drops);
    expected_random = (JavaRandom){1357};
    float entry_volume = gm_player_movement_audio_volume(
        GM_PLAYER_MOVEMENT_AUDIO_SPLASH, 0.125, -0.25, 0.375);
    float entry_pitch = gm_player_movement_audio_pitch(
        GM_PLAYER_MOVEMENT_AUDIO_SPLASH, &expected_random);
    GmPlayerSplashParticle
        expected_entry_particles[GM_PLAYER_SPLASH_PARTICLE_CAP];
    int entry_particles = gm_player_splash_particles(
        &expected_random, 24.5, 220.0, 24.5, 0.6F,
        0.125, -0.25, 0.375,
        expected_entry_particles, GM_PLAYER_SPLASH_PARTICLE_CAP);
    (void)jrand_int_bound(&expected_random, 1000);
    (void)jrand_float(&expected_random);
    runtime.mobs_enabled = 1;
    GmAction entry_idle = {0};
    entry_idle.hotbar_sel = -1;
    gm_runtime_tick(&runtime, entry_idle);
    runtime.mobs_enabled = 0;
    s = store(&runtime.mobs);
    CHECK(slot > 0 && runtime.mobs.entity_in_water[slot]
              && runtime.mobs.entity_fall_distance[slot] == 0.0F
              && runtime.mobs.fire_ticks[slot] == -1
              && entry_particles == 26
              && runtime.mobs.entity_random[slot].random.seed
                  == expected_random.seed
              && gm_mobs_event_count(&runtime.mobs) == 1
              && gm_mobs_event_get(&runtime.mobs, 0, &event)
              && event.kind == GM_MOB_EVENT_SOUND
              && event.data == GM_MOB_SOUND_HOSTILE_SPLASH
              && event.eid == 719 && event.x == 24.5
              && event.y == 220.0 && event.z == 24.5
              && event.volume == entry_volume
              && event.pitch == entry_pitch,
          "dry-to-water Witch entry preserves splash audio and RNG tail");
    CHECK(gm_mobs_particle_batch_count(&runtime.mobs) == 4,
          "dry-to-water Witch entry retains four bounded particle batches");
    int entry_seen = 0;
    for (int batch_index = 0; batch_index < 4; ++batch_index) {
        GmMobParticleBatch batch;
        int have_batch = gm_mobs_particle_batch_get(
            &runtime.mobs, batch_index, &batch);
        CHECK(have_batch, "water-entry particle batch remains readable");
        if (!have_batch) continue;
        CHECK(batch.eid == 719 && batch.dimension == 0
                  && batch.particle_id
                      == expected_entry_particles[entry_seen].kind,
              "water-entry particle batch retains source and kind");
        for (int i = 0; i < batch.count; ++i) {
            GmTerminalParticle *actual = &batch.particles[i];
            GmPlayerSplashParticle *expected =
                &expected_entry_particles[entry_seen++];
            CHECK(actual->x == expected->x
                      && actual->y == expected->y
                      && actual->z == expected->z
                      && actual->vx == expected->motion_x
                      && actual->vy == expected->motion_y
                      && actual->vz == expected->motion_z,
                  "water-entry particle payload preserves Java call order");
        }
    }
    CHECK(entry_seen == entry_particles,
          "water-entry particle batches retain all 26 calls");

    /* That same full tick drains the batches into the particle seam consumed
     * by game_main's water renderer. */
    CHECK(gm_runtime_particle_event_count(&runtime) == entry_particles,
          "runtime exports every Witch water-entry particle payload");
    for (int i = 0; i < entry_particles; ++i) {
        GmRuntimeParticleEvent actual;
        GmPlayerSplashParticle *expected = &expected_entry_particles[i];
        CHECK(gm_runtime_particle_event_get(&runtime, i, &actual)
                  && actual.kind == expected->kind
                  && actual.entity_eid == 719 && actual.dimension == 0
                  && actual.x == expected->x && actual.y == expected->y
                  && actual.z == expected->z
                  && actual.motion_x == expected->motion_x
                  && actual.motion_y == expected->motion_y
                  && actual.motion_z == expected->motion_z,
              "runtime Witch particle event preserves exact payload");
    }
    /* Entity.handleMaterialAcceleration is shared with the verified player
     * and pig kernel. A level-one cell beside a west source has a unit-east
     * flow and adds exactly 0.014 to an otherwise stationary Witch. */
    slot = stage_flow_water_witch(&runtime, 718, 777);
    CHECK(slot > 0, "flowing-water Witch fixture spawns");
    if (slot > 0) {
        s = store(&runtime.mobs);
        gm_world_fill_window(
            runtime.world, runtime.ccx, runtime.ccz,
            (struct Chunk *)runtime.window);
        uint64_t flow_seed = runtime.mobs.entity_random[slot].random.seed;
        int in_flow = gm_mobs_handle_water_slot(
            &runtime.mobs, runtime.world,
            (const struct Chunk *)runtime.window,
            runtime.ox, runtime.oz, s, slot);
        CHECK(in_flow
                  && s->vx[slot] == 0.014
                  && s->vy[slot] == 0.0
                  && s->vz[slot] == 0.0
                  && s->x[slot] == 24.8
                  && s->y[slot] == 220.0
                  && s->z[slot] == 24.5
                  && runtime.mobs.entity_random[slot].random.seed == flow_seed,
              "flowing water adds the exact allocation-free Witch current");
    }

    /* EntityLivingBase probes eight eye corners after Entity's environment
     * phase. A captured causesSuffocation state applies IN_WALL before timer
     * aging, while leaves are the canonical non-suffocating solid control. */
    math_seed = 0;
    next_id = 15000;
    slot = stage_in_wall_witch(&runtime, 720, 11, 1);
    memset(&drops, 0, sizeof drops);
    expected_random = (JavaRandom){11};
    expected_math = (JavaRandom){0};
    (void)jrand_double(&expected_random);
    (void)jrand_double(&expected_math);
    first = jrand_float(&expected_random);
    second = jrand_float(&expected_random);
    expected_pitch = (first - second) * 0.2F + 1.0F;
    ewitch_generate_loot(&expected_random, 0, &expected_loot);
    (void)jrand_float(&expected_random);
    tick(&runtime, &world_seed, &math_seed, &next_id, 1, &drops);
    s = store(&runtime.mobs);
    for (int i = 0; i < expected_loot.count; ++i)
        CHECK(exact_item_at(
                  &drops.ents[i], 15000 + i,
                  expected_loot.item[i], expected_loot.quantity[i],
                  40.5, 220.0, 40.5, &expected_math),
              "IN_WALL table drop has exact constructor state");
    CHECK(slot > 0 && s->health[slot] == 0.0F
              && runtime.mobs.entity_dead[slot]
              && runtime.mobs.entity_death_time[slot] == 1
              && runtime.mobs.entity_hurt_time[slot] == 9
              && runtime.mobs.entity_hurt_resistant[slot] == 19
              && runtime.mobs.witch_attack_timer[slot] == 31
              && runtime.mobs.entity_random[slot].random.seed
                  == expected_random.seed
              && math_seed == expected_math.seed
              && next_id == 15000 + expected_loot.count
              && drops.n_active == expected_loot.count,
          "stone eye sample applies exact full-tick IN_WALL death");
    CHECK(gm_mobs_event_count(&runtime.mobs) == 3
              && gm_mobs_event_get(&runtime.mobs, 0, &event)
              && event.kind == GM_MOB_EVENT_ENTITY_STATUS && event.data == 2
              && gm_mobs_event_get(&runtime.mobs, 1, &event)
              && event.kind == GM_MOB_EVENT_SOUND
              && event.data == GM_MOB_SOUND_WITCH_DEATH
              && event.pitch == expected_pitch
              && gm_mobs_event_get(&runtime.mobs, 2, &event)
              && event.kind == GM_MOB_EVENT_ENTITY_STATUS && event.data == 3,
          "IN_WALL death preserves feedback order and post-death status draw");

    slot = stage_in_wall_witch(&runtime, 721, 12345, 18);
    memset(&drops, 0, sizeof drops);
    expected_random = (JavaRandom){12345};
    (void)jrand_int_bound(&expected_random, 1000);
    (void)jrand_float(&expected_random);
    tick(&runtime, &world_seed, &math_seed, &next_id, 1, &drops);
    s = store(&runtime.mobs);
    CHECK(slot > 0 && s->health[slot] == 1.0F
              && !runtime.mobs.entity_dead[slot]
              && runtime.mobs.entity_random[slot].random.seed
                  == expected_random.seed
              && drops.n_active == 0,
          "leaves eye sample does not suffocate an ordinary Witch");

    /* Entity.move dispatches updateFallState before block contacts. The
     * landing phase emits small-fall, lethal FALL feedback, the supporting
     * block sound, and a dust descriptor; damage is too late for deathTime
     * aging. */
    math_seed = 0;
    next_id = 16000;
    slot = stage_fall_witch(&runtime, 722, 3, 1, 4.0F);
    memset(&drops, 0, sizeof drops);
    expected_random = (JavaRandom){3};
    expected_math = (JavaRandom){0};
    (void)jrand_int_bound(&expected_random, 1000);
    int expected_status = jrand_float(&expected_random) < 7.5E-4F;
    (void)jrand_double(&expected_random);
    (void)jrand_double(&expected_math);
    first = jrand_float(&expected_random);
    second = jrand_float(&expected_random);
    expected_pitch = (first - second) * 0.2F + 1.0F;
    ewitch_generate_loot(&expected_random, 0, &expected_loot);
    tick(&runtime, &world_seed, &math_seed, &next_id, 1, &drops);
    s = store(&runtime.mobs);
    CHECK(slot > 0 && !expected_status && s->health[slot] == 0.0F
              && runtime.mobs.entity_dead[slot]
              && runtime.mobs.entity_death_time[slot] == 0
              && runtime.mobs.entity_hurt_time[slot] == 10
              && runtime.mobs.entity_hurt_resistant[slot] == 20
              && runtime.mobs.entity_fall_distance[slot] == 0.0F
              && s->on_ground[slot] && s->y[slot] == 220.0
              && runtime.mobs.witch_attack_timer[slot] == 31
              && runtime.mobs.entity_random[slot].random.seed
                  == expected_random.seed,
          "stone landing applies exact late full-tick FALL death state");
    CHECK(gm_mobs_event_count(&runtime.mobs) == 5
              && gm_mobs_event_get(&runtime.mobs, 0, &event)
              && event.kind == GM_MOB_EVENT_SOUND
              && event.data == GM_MOB_SOUND_HOSTILE_SMALL_FALL
              && event.volume == 1.0F && event.pitch == 1.0F
              && gm_mobs_event_get(&runtime.mobs, 1, &event)
              && event.kind == GM_MOB_EVENT_ENTITY_STATUS && event.data == 2
              && gm_mobs_event_get(&runtime.mobs, 2, &event)
              && event.kind == GM_MOB_EVENT_SOUND
              && event.data == GM_MOB_SOUND_WITCH_DEATH
              && event.pitch == expected_pitch
              && gm_mobs_event_get(&runtime.mobs, 3, &event)
              && event.kind == GM_MOB_EVENT_ENTITY_STATUS && event.data == 3
              && gm_mobs_event_get(&runtime.mobs, 4, &event)
              && event.kind == GM_MOB_EVENT_SOUND
              && event.data == GM_MOB_SOUND_BLOCK_STONE_FALL
              && event.volume == 0.5F && event.pitch == 0.75F,
          "FALL death preserves landing, feedback, and support sound order");
    GmMobParticleBatch landing;
    CHECK(gm_mobs_particle_batch_count(&runtime.mobs) == 1
              && gm_mobs_particle_batch_get(
                  &runtime.mobs, 0, &landing)
              && landing.particle_id == GM_PARTICLE_BLOCK_DUST
              && landing.descriptor_count == 40
              && landing.x == 8.5 && landing.y == 220.0
              && landing.z == 24.5
              && landing.offset_x == 0.0 && landing.offset_y == 0.0
              && landing.offset_z == 0.0
              && landing.speed == 0.15000000596046448
              && landing.parameter_count == 1
              && landing.parameters[0] == 1,
          "stone landing exports exact BLOCK_DUST packet descriptor");

    math_seed = 0;
    next_id = 17000;
    slot = stage_fall_witch(&runtime, 723, 12345, 170, 8.0F);
    memset(&drops, 0, sizeof drops);
    tick(&runtime, &world_seed, &math_seed, &next_id, 0, &drops);
    s = store(&runtime.mobs);
    CHECK(slot > 0 && s->health[slot] == 0.0F
              && runtime.mobs.entity_dead[slot]
              && runtime.mobs.entity_fall_distance[slot] == 0.0F
              && drops.n_active == 0
              && gm_mobs_particle_batch_count(&runtime.mobs) == 1
              && gm_mobs_particle_batch_get(
                  &runtime.mobs, 0, &landing)
              && landing.descriptor_count == 80
              && landing.parameters[0] == 170
              && gm_mobs_event_get(&runtime.mobs, 4, &event)
              && event.kind == GM_MOB_EVENT_SOUND
              && event.data == GM_MOB_SOUND_BLOCK_GRASS_FALL,
          "hay landing applies one-fifth FALL damage and exact dust count");

    math_seed = 0;
    next_id = 17500;
    slot = stage_fall_witch(&runtime, 725, 3, 1, 8.0F);
    memset(&drops, 0, sizeof drops);
    tick(&runtime, &world_seed, &math_seed, &next_id, 0, &drops);
    s = store(&runtime.mobs);
    CHECK(slot > 0 && s->health[slot] == 0.0F
              && runtime.mobs.entity_dead[slot]
              && runtime.mobs.entity_fall_distance[slot] == 0.0F
              && drops.n_active == 0
              && gm_mobs_event_count(&runtime.mobs) == 5
              && gm_mobs_event_get(&runtime.mobs, 0, &event)
              && event.kind == GM_MOB_EVENT_SOUND
              && event.data == GM_MOB_SOUND_HOSTILE_BIG_FALL
              && event.volume == 1.0F && event.pitch == 1.0F
              && gm_mobs_event_get(&runtime.mobs, 4, &event)
              && event.kind == GM_MOB_EVENT_SOUND
              && event.data == GM_MOB_SOUND_BLOCK_STONE_FALL
              && gm_mobs_particle_batch_count(&runtime.mobs) == 1
              && gm_mobs_particle_batch_get(
                  &runtime.mobs, 0, &landing)
              && landing.descriptor_count == 80
              && landing.parameters[0] == 1,
          "large stone landing emits exact hostile big-fall boundary");

    /* Jump Boost II subtracts two blocks before the fall-damage ceil. Its
     * visible-effect particle decision precedes Witch ambient/status RNG. */
    slot = stage_fall_witch(&runtime, 726, 3, 1, 5.0F);
    runtime.mobs.a.health[slot] = runtime.mobs.b.health[slot] = 20.0F;
    runtime.mobs.entity_effect_count[slot] = 1;
    runtime.mobs.entity_effects[slot][0] = (PtMobEffect){8, 20, 1};
    memset(&drops, 0, sizeof drops);
    expected_random = (JavaRandom){3};
    consume_visible_effect_particles(&expected_random);
    (void)jrand_int_bound(&expected_random, 1000);
    (void)jrand_float(&expected_random);
    tick(&runtime, &world_seed, &math_seed, &next_id, 0, &drops);
    s = store(&runtime.mobs);
    CHECK(slot > 0 && s->health[slot] == 20.0F
              && !runtime.mobs.entity_dead[slot]
              && runtime.mobs.entity_hurt_time[slot] == 0
              && runtime.mobs.entity_hurt_resistant[slot] == 0
              && runtime.mobs.entity_fall_distance[slot] == 0.0F
              && s->on_ground[slot]
              && s->vy[slot] == -0.0784000015258789
              && runtime.mobs.entity_effect_count[slot] == 1
              && runtime.mobs.entity_effects[slot][0].duration == 19
              && runtime.mobs.entity_effects[slot][0].amplifier == 1
              && runtime.mobs.entity_random[slot].random.seed
                  == expected_random.seed
              && gm_mobs_event_count(&runtime.mobs) == 0
              && gm_mobs_particle_batch_count(&runtime.mobs) == 1
              && gm_mobs_particle_batch_get(
                  &runtime.mobs, 0, &landing)
              && landing.descriptor_count == 50
              && landing.parameters[0] == 1,
          "Jump Boost II prevents exact five-block landing damage");

    /* A non-sneaking living entity keeps the pre-collision downward motion
     * through BlockSlime.onLanded, then pays the ordinary gravity/drag tail. */
    slot = stage_fall_witch(&runtime, 727, 3, 165, 8.0F);
    runtime.mobs.a.health[slot] = runtime.mobs.b.health[slot] = 20.0F;
    memset(&drops, 0, sizeof drops);
    expected_random = (JavaRandom){3};
    (void)jrand_int_bound(&expected_random, 1000);
    (void)jrand_float(&expected_random);
    tick(&runtime, &world_seed, &math_seed, &next_id, 0, &drops);
    s = store(&runtime.mobs);
    CHECK(slot > 0 && s->health[slot] == 20.0F
              && !runtime.mobs.entity_dead[slot]
              && runtime.mobs.entity_fall_distance[slot] == 0.0F
              && s->on_ground[slot]
              && s->vy[slot] == 0.01960000038146973
              && runtime.mobs.entity_random[slot].random.seed
                  == expected_random.seed
              && gm_mobs_event_count(&runtime.mobs) == 0
              && gm_mobs_particle_batch_count(&runtime.mobs) == 1
              && gm_mobs_particle_batch_get(
                  &runtime.mobs, 0, &landing)
              && landing.descriptor_count == 80
              && landing.parameters[0] == 165,
          "slime landing prevents damage and preserves exact living bounce");

    /* Entity.canTrample consumes World.rand even when the low-fall threshold
     * rejects the mutation. Seed zero accepts; the all-ones state rejects. */
    world_seed = 0;
    slot = stage_fall_witch(&runtime, 728, 3, 60, 0.6F);
    runtime.mobs.a.health[slot] = runtime.mobs.b.health[slot] = 20.0F;
    memset(&drops, 0, sizeof drops);
    tick(&runtime, &world_seed, &math_seed, &next_id, 0, &drops);
    s = store(&runtime.mobs);
    CHECK(slot > 0 && s->health[slot] == 20.0F
              && s->on_ground[slot] && s->y[slot] == 219.9375
              && s->vy[slot] == -0.0784000015258789
              && gm_world_block(runtime.world, 8, 219, 24) == 3
              && world_seed == 11
              && gm_mobs_event_count(&runtime.mobs) == 0
              && gm_mobs_particle_batch_count(&runtime.mobs) == 0,
          "seeded low Witch fall tramples farmland at exact surface height");

    world_seed = UINT64_C(281474976710655);
    slot = stage_fall_witch(&runtime, 729, 3, 60, 0.6F);
    runtime.mobs.a.health[slot] = runtime.mobs.b.health[slot] = 20.0F;
    memset(&drops, 0, sizeof drops);
    tick(&runtime, &world_seed, &math_seed, &next_id, 0, &drops);
    s = store(&runtime.mobs);
    CHECK(slot > 0 && s->health[slot] == 20.0F
              && s->on_ground[slot] && s->y[slot] == 219.9375
              && gm_world_block(runtime.world, 8, 219, 24) == 60
              && world_seed == UINT64_C(281449761806750)
              && gm_mobs_event_count(&runtime.mobs) == 0
              && gm_mobs_particle_batch_count(&runtime.mobs) == 0,
          "seeded low Witch fall can preserve farmland after exact RNG draw");

    runtime.mob_griefing = 0;
    world_seed = 0;
    slot = stage_fall_witch(&runtime, 730, 3, 60, 0.6F);
    runtime.mobs.a.health[slot] = runtime.mobs.b.health[slot] = 20.0F;
    memset(&drops, 0, sizeof drops);
    tick(&runtime, &world_seed, &math_seed, &next_id, 0, &drops);
    s = store(&runtime.mobs);
    CHECK(slot > 0 && s->health[slot] == 20.0F
              && s->on_ground[slot] && s->y[slot] == 219.9375
              && gm_world_block(runtime.world, 8, 219, 24) == 60
              && world_seed == 11
              && gm_mobs_event_count(&runtime.mobs) == 0
              && gm_mobs_particle_batch_count(&runtime.mobs) == 0,
          "mobGriefing suppresses farmland mutation after exact RNG draw");
    runtime.mob_griefing = 1;

    /* The ordinary runtime drains landing sounds and the packet descriptor
     * into the global bounded streams consumed by audio and particles. */
    slot = stage_fall_witch(&runtime, 724, 95, 1, 4.0F);
    runtime.mobs_enabled = 1;
    runtime.sound_event_head = runtime.sound_event_count = 0;
    runtime.sound_mob_next_seq = 0;
    runtime.particle_event_count = 0;
    runtime.particle_mob_next_seq = 0;
    runtime.next_entity_id = 18000;
    GmAction idle;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    gm_runtime_tick(&runtime, idle);
    int small_fall = 0, witch_death = 0, stone_fall = 0;
    for (int i = 0; i < gm_runtime_sound_event_count(&runtime); ++i) {
        GmRuntimeSoundEvent sound;
        if (!gm_runtime_sound_event_get(&runtime, i, &sound)) continue;
        if (sound.sound == GM_SOUND_HOSTILE_SMALL_FALL
                && sound.category == GM_SOUND_CATEGORY_HOSTILE
                && sound.eid == 724)
            ++small_fall;
        if (sound.sound == GM_SOUND_WITCH_DEATH
                && sound.category == GM_SOUND_CATEGORY_HOSTILE
                && sound.eid == 724)
            ++witch_death;
        if (sound.sound == GM_SOUND_BLOCK_STONE_FALL
                && sound.category == GM_SOUND_CATEGORY_HOSTILE
                && sound.eid == 724)
            ++stone_fall;
    }
    GmRuntimeParticleEvent landing_event;
    CHECK(slot > 0 && small_fall == 1 && witch_death == 1
              && stone_fall == 1,
          "runtime exports hostile landing, death, and support audio");
    CHECK(gm_runtime_particle_event_count(&runtime) == 1
              && gm_runtime_particle_event_get(
                  &runtime, 0, &landing_event)
              && landing_event.kind == GM_PARTICLE_BLOCK_DUST
              && landing_event.count == 40
              && landing_event.entity_eid == 724
              && landing_event.speed == 0.15000000596046448
              && landing_event.parameter_count == 1
              && landing_event.parameters[0] == 1,
          "runtime exports exact Witch landing packet descriptor");

    gm_runtime_destroy(&runtime);
    if (fail) return 1;
    puts("witch_burning_death_live: PASS table_loot=3 held_drop=0 xp=0 "
         "product=1 fire_resist=1 particles=20 gamerule=1 capacity_atomic=1 "
         "lava=1 in_fire=1 cactus=1 water_fire=1 rain_fire=1 water_flow=1 "
         "water_entry=1 in_wall=1 fall=1 big_fall=1 hay=1 jump_fall=1 "
         "slime_fall=1 farmland_fall=3 runtime_fall=1");
    return 0;
}
