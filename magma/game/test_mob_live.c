#include "game/runtime.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int fail;
#define CHECK(C, M) do { if (!(C)) { fprintf(stderr, "FAIL: %s\n", M); fail=1; } } while (0)

static uint64_t test_double_bits(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static int init_flat_seed(GmRuntime *r, long long seed) {
    GmConfig c; char err[256];
    gm_config_defaults(&c); c.world=GM_WORLD_SUPERFLAT; c.view_distance=1;
    c.seed=seed;
    if(!gm_runtime_init(r,&c,err,sizeof err)){fprintf(stderr,"init: %s\n",err);return 0;}
    /* Unit fixtures opt into natural spawning explicitly.  Exact Java
     * WorldEntitySpawner runs every tick and would otherwise add unrelated
     * entities to AI, collision, and death tests. */
    r->gamerules.doMobSpawning = 0;
    gm_mobs_set_natural_spawning(&r->mobs,0);
    gm_runtime_set_pose(r,8.5,5.0,8.5,0.0f,10.0f);
    return 1;
}

static int init_flat(GmRuntime *r) { return init_flat_seed(r,0); }

typedef struct {
    float x, z, yaw, head_yaw, pitch;
    int eat_time;
} SheepSample;

typedef struct {
    SheepSample samples[512];
    int nsamples;
    int head_onsets[16];
    int nhead_onsets;
    int panic_tick;
    double panic_step;
} SheepTrace;

static EwStore *test_mob_store(GmMobLive *m) {
    return m->current ? &m->b : &m->a;
}

static void test_natural_spawn_shuffle(void) {
    /* Java 8 HashSet<ChunkPos> iteration followed by Collections.shuffle,
     * recorded by NaturalSpawnShuffleGolden.java on OpenJDK 8.  The native
     * seam receives Random's internal 48-bit cursor, after setSeed's xor. */
    static const int expected[25][2] = {
        {-4, 7}, {-1, 5}, {-2, 9}, {-3, 5}, {-5, 5},
        {-5, 7}, {-2, 6}, {-4, 8}, {-1, 8}, {-5, 8},
        {-2, 7}, {-2, 8}, {-5, 9}, {-3, 9}, {-1, 9},
        {-4, 5}, {-5, 6}, {-1, 6}, {-4, 9}, {-4, 6},
        {-1, 7}, {-3, 8}, {-3, 6}, {-3, 7}, {-2, 5},
    };
    GmNaturalSpawnChunk loaded[25];
    int n = 0;
    for (int cx = -5; cx <= -1; ++cx)
        for (int cz = 5; cz <= 9; ++cz) {
            loaded[n].chunk_x = cx;
            loaded[n].chunk_z = cz;
            ++n;
        }
    GmNaturalSpawnContext context;
    memset(&context, 0, sizeof context);
    context.loaded_chunks = loaded;
    context.loaded_chunk_count = n;
    uint64_t cursor =
        (UINT64_C(0x123456789abc) ^ UINT64_C(0x5deece66d))
        & ((UINT64_C(1) << 48) - 1);
    GmNaturalSpawnChunk actual[25];
    int actual_n = gm_mobs_natural_chunk_order(
        -3, 7, &context, &cursor, actual, 25);
    CHECK(actual_n == 25, "natural spawn order contains every loaded inner chunk");
    for (int i = 0; i < actual_n && i < 25; ++i) {
        if (actual[i].chunk_x != expected[i][0]
                || actual[i].chunk_z != expected[i][1]) {
            fprintf(stderr,
                    "FAIL: natural spawn shuffle[%d] got %d,%d expected %d,%d\n",
                    i, actual[i].chunk_x, actual[i].chunk_z,
                    expected[i][0], expected[i][1]);
            fail = 1;
        }
    }
    CHECK(cursor == UINT64_C(43781828811273),
          "Collections.shuffle consumes the exact independent Java Random cursor");
}

static void test_natural_spawn_checkpoint(void) {
    static const char path[] = ".tmp/test_mob_live_collections_checkpoint.bin";
    GmRuntime runtime;
    CHECK(init_flat(&runtime), "initialize natural-spawn checkpoint fixture");
    if (fail) return;
    runtime.collections_random_seed48 = UINT64_C(0x123456789abc);
    CHECK(gm_runtime_write_checkpoint(&runtime, path),
          "checkpoint writes Collections.shuffle RNG cursor");
    runtime.collections_random_seed48 = 7;
    CHECK(gm_runtime_load_checkpoint(&runtime, path),
          "checkpoint reloads Collections.shuffle RNG cursor");
    CHECK(runtime.collections_random_seed48 == UINT64_C(0x123456789abc),
          "checkpoint preserves Collections.shuffle RNG cursor exactly");
    (void)remove(path);
    gm_runtime_destroy(&runtime);
}

static void test_mobs_tick(GmRuntime *r) {
    /* These focused AI schedules historically run without the runtime's
     * collision window; preserve that isolation while passing the newer RNG
     * and entity-cursor arguments explicitly. */
    gm_mobs_tick(&r->mobs, r->world, NULL,
                 (const struct McSinTable *)&r->sin_table,
                 (struct PsvPlayer *)&r->player,
                 (struct PvStats *)&r->vitals,
                 r->ox, r->oz, r->dimension, r->clock.world_time,
                 &r->clock, r->mob_griefing, &r->world_random_seed48,
                 &r->math_random_seed48, &r->next_entity_id,
                 r->do_mob_loot, &r->entities, 0.0f, 0.0f);
}

static void test_shared_living_slime_bounce(void) {
    const int types[] = {EW_TYPE_ZOMBIE, GM_MOB_SHEEP};
    const char *names[] = {"zombie", "sheep"};
    for (int fixture = 0; fixture < 2; ++fixture) {
        GmRuntime r;
        CHECK(init_flat(&r), "initialize shared living slime fixture");
        if (fail) return;
        int slot = gm_mobs_spawn(
            &r.mobs, types[fixture], 8.5, 220.0, 24.5);
        CHECK(slot > 0, "spawn shared living slime fixture");
        if (slot <= 0) {
            gm_runtime_destroy(&r);
            continue;
        }
        r.mobs.persistence_required[slot] = 1;
        r.mobs.a.vy[slot] = r.mobs.b.vy[slot] = -0.1;
        r.mobs.a.on_ground[slot] = r.mobs.b.on_ground[slot] = 0;
        r.mobs.entity_fall_distance[slot] = 8.0F;
        gm_world_set_block(r.world, 8, 219, 24, 165);
        for (int y = 220; y <= 222; ++y)
            gm_world_set_block(r.world, 8, y, 24, 0);
        test_mobs_tick(&r);
        EwStore *state = test_mob_store(&r.mobs);
        GmMobParticleBatch landing;
        int has_landing = gm_mobs_particle_batch_count(&r.mobs) == 1
            && gm_mobs_particle_batch_get(&r.mobs, 0, &landing);
        fprintf(stderr,
                "mob_live: %s slime bounce y=%.17g vy=%.17g\n",
                names[fixture], state->y[slot], state->vy[slot]);
        CHECK(state->alive[slot] && state->on_ground[slot]
                  && state->y[slot] == 220.0
                  && state->vy[slot] == 0.01960000038146973
                  && r.mobs.entity_fall_distance[slot] == 0.0F
                  && has_landing && landing.eid == state->id[slot]
                  && landing.dimension == r.mobs.entity_dimension[slot]
                  && landing.particle_id == 38
                  && landing.descriptor_count == 80
                  && landing.x == state->x[slot]
                  && landing.y == state->y[slot]
                  && landing.z == state->z[slot]
                  && landing.speed == 0.15000000596046448D
                  && landing.parameter_count == 1
                  && landing.parameters[0] == 165,
              "ordinary living mob uses exact slime bounce and landing dust");
        gm_runtime_destroy(&r);
    }
}

static void test_shared_living_nonlethal_fall(void) {
    const int types[] = {
        EW_TYPE_ZOMBIE, EW_TYPE_ZOMBIE_VILLAGER,
        EW_TYPE_SKELETON, EW_TYPE_WITHER_SKELETON,
        EW_TYPE_CREEPER, EW_TYPE_SPIDER, EW_TYPE_CAVE_SPIDER,
        EW_TYPE_PIGMAN, EW_TYPE_SILVERFISH,
        EW_TYPE_ENDERMITE,
        EW_TYPE_SHEEP, EW_TYPE_PIG, EW_TYPE_COW, EW_TYPE_VILLAGER
    };
    const int hurt_sounds[] = {
        GM_MOB_SOUND_ZOMBIE_HURT, GM_MOB_SOUND_ZOMBIE_VILLAGER_HURT,
        GM_MOB_SOUND_SKELETON_HURT,
        GM_MOB_SOUND_WITHER_SKELETON_HURT,
        GM_MOB_SOUND_CREEPER_HURT, GM_MOB_SOUND_SPIDER_HURT,
        GM_MOB_SOUND_SPIDER_HURT, GM_MOB_SOUND_PIGMAN_HURT,
        GM_MOB_SOUND_SILVERFISH_HURT, GM_MOB_SOUND_ENDERMITE_HURT,
        GM_MOB_SOUND_SHEEP_HURT,
        GM_MOB_SOUND_PIG_HURT, GM_MOB_SOUND_COW_HURT,
        GM_MOB_SOUND_VILLAGER_HURT
    };
    const int fall_sounds[] = {
        GM_MOB_SOUND_HOSTILE_SMALL_FALL,
        GM_MOB_SOUND_HOSTILE_SMALL_FALL,
        GM_MOB_SOUND_HOSTILE_SMALL_FALL,
        GM_MOB_SOUND_HOSTILE_SMALL_FALL,
        GM_MOB_SOUND_HOSTILE_SMALL_FALL,
        GM_MOB_SOUND_HOSTILE_SMALL_FALL,
        GM_MOB_SOUND_HOSTILE_SMALL_FALL,
        GM_MOB_SOUND_HOSTILE_SMALL_FALL,
        GM_MOB_SOUND_HOSTILE_SMALL_FALL,
        GM_MOB_SOUND_HOSTILE_SMALL_FALL,
        GM_MOB_SOUND_GENERIC_SMALL_FALL,
        GM_MOB_SOUND_GENERIC_SMALL_FALL,
        GM_MOB_SOUND_GENERIC_SMALL_FALL,
        GM_MOB_SOUND_GENERIC_SMALL_FALL
    };
    const float hurt_volumes[] = {
        1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F,
        1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 0.4F, 1.0F
    };
    const char *names[] = {
        "zombie", "zombie_villager", "skeleton", "wither_skeleton",
        "creeper", "spider", "cave_spider", "pigman", "silverfish",
        "endermite",
        "sheep", "pig", "cow", "villager"
    };
    for (int fixture = 0;
            fixture < (int)(sizeof types / sizeof types[0]); ++fixture) {
        GmRuntime r;
        CHECK(init_flat(&r), "initialize shared hostile fall fixture");
        if (fail) return;
        int slot = gm_mobs_spawn(
            &r.mobs, types[fixture], 8.5, 220.0, 24.5);
        CHECK(slot > 0, "spawn shared hostile fall fixture");
        if (slot <= 0) {
            gm_runtime_destroy(&r);
            continue;
        }
        EwStore *initial = test_mob_store(&r.mobs);
        int eid = initial->id[slot];
        float health_before = initial->health[slot];
        r.mobs.persistence_required[slot] = 1;
        r.mobs.a.vy[slot] = r.mobs.b.vy[slot] = -0.1;
        r.mobs.a.on_ground[slot] = r.mobs.b.on_ground[slot] = 0;
        r.mobs.entity_fall_distance[slot] = 5.0F;
        gm_world_set_block(r.world, 8, 219, 24, 1);
        for (int y = 220; y <= 222; ++y)
            gm_world_set_block(r.world, 8, y, 24, 0);
        test_mobs_tick(&r);

        EwStore *state = test_mob_store(&r.mobs);
        GmMobEvent event[4];
        int events = gm_mobs_event_count(&r.mobs) == 4;
        for (int i = 0; i < 4; ++i)
            events = events && gm_mobs_event_get(
                &r.mobs, i, &event[i]);
        GmMobParticleBatch landing;
        int particle = gm_mobs_particle_batch_count(&r.mobs) == 1
            && gm_mobs_particle_batch_get(&r.mobs, 0, &landing);
        fprintf(stderr,
                "mob_live: %s stone fall health=%.9g events=%d\n",
                names[fixture], state->health[slot],
                gm_mobs_event_count(&r.mobs));
        CHECK(state->alive[slot] && state->on_ground[slot]
                  && state->health[slot] == health_before - 2.0F
                  && r.mobs.entity_hurt_time[slot] == 10
                  && r.mobs.entity_hurt_resistant[slot] == 20
                  && r.mobs.entity_fall_distance[slot] == 0.0F
                  && events
                  && event[0].kind == GM_MOB_EVENT_SOUND
                  && event[0].eid == eid
                  && event[0].data == fall_sounds[fixture]
                  && event[0].volume == 1.0F && event[0].pitch == 1.0F
                  && event[1].kind == GM_MOB_EVENT_ENTITY_STATUS
                  && event[1].eid == eid && event[1].data == 2
                  && event[2].kind == GM_MOB_EVENT_SOUND
                  && event[2].eid == eid
                  && event[2].data == hurt_sounds[fixture]
                  && event[2].volume == hurt_volumes[fixture]
                  && event[3].kind == GM_MOB_EVENT_SOUND
                  && event[3].eid == eid
                  && event[3].data == GM_MOB_SOUND_BLOCK_STONE_FALL
                  && event[3].volume == 0.5F
                  && event[3].pitch == 0.75F
                  && particle && landing.eid == eid
                  && landing.particle_id == 38
                  && landing.descriptor_count == 50
                  && landing.parameters[0] == 1
                  && (types[fixture] != EW_TYPE_CREEPER
                      || r.mobs.creeper_fuse[slot] == 7)
                  && (types[fixture] != EW_TYPE_PIGMAN
                      || r.mobs.anger[slot] == 0)
                  && (fixture < 10 || r.mobs.panic_ticks[slot] == 0),
              "ordinary living mob uses exact nonlethal fall boundary");
        gm_runtime_destroy(&r);
    }
}

static void test_shared_living_fall_variants(void) {
    const struct {
        int type, block, damage, fall_sound, hurt_sound, support_sound;
        float fall_distance;
        int particle_count, jump, fuse;
        const char *name;
    } fixtures[] = {
        {EW_TYPE_ZOMBIE, 1, 5, GM_MOB_SOUND_HOSTILE_BIG_FALL,
         GM_MOB_SOUND_ZOMBIE_HURT, GM_MOB_SOUND_BLOCK_STONE_FALL,
         8.0F, 80, 0, 0, "zombie big stone"},
        {EW_TYPE_SHEEP, 170, 1, GM_MOB_SOUND_GENERIC_SMALL_FALL,
         GM_MOB_SOUND_SHEEP_HURT, GM_MOB_SOUND_BLOCK_GRASS_FALL,
         8.0F, 80, 0, 0, "sheep hay"},
        {EW_TYPE_CREEPER, 1, 0, 0, 0, 0,
         5.0F, 50, 1, 7, "creeper Jump Boost stone"}
    };
    for (int fixture = 0; fixture < 3; ++fixture) {
        GmRuntime r;
        CHECK(init_flat(&r), "initialize shared fall-variant fixture");
        if (fail) return;
        int slot = gm_mobs_spawn(
            &r.mobs, fixtures[fixture].type, 8.5, 220.0, 24.5);
        CHECK(slot > 0, "spawn shared fall-variant fixture");
        if (slot <= 0) {
            gm_runtime_destroy(&r);
            continue;
        }
        EwStore *initial = test_mob_store(&r.mobs);
        float health_before = initial->health[slot];
        r.mobs.persistence_required[slot] = 1;
        r.mobs.a.vy[slot] = r.mobs.b.vy[slot] = -0.1;
        r.mobs.a.on_ground[slot] = r.mobs.b.on_ground[slot] = 0;
        r.mobs.entity_fall_distance[slot] = fixtures[fixture].fall_distance;
        if (fixtures[fixture].jump) {
            r.mobs.entity_effect_count[slot] = 1;
            r.mobs.entity_effects[slot][0] = (PtMobEffect){8, 20, 1};
        }
        gm_world_set_block(
            r.world, 8, 219, 24, fixtures[fixture].block);
        for (int y = 220; y <= 222; ++y)
            gm_world_set_block(r.world, 8, y, 24, 0);
        test_mobs_tick(&r);

        EwStore *state = test_mob_store(&r.mobs);
        GmMobParticleBatch landing;
        int particle = gm_mobs_particle_batch_count(&r.mobs) == 1
            && gm_mobs_particle_batch_get(&r.mobs, 0, &landing);
        int events = gm_mobs_event_count(&r.mobs);
        GmMobEvent event[4];
        int exact_events = events == (fixtures[fixture].damage ? 4 : 0);
        if (fixtures[fixture].damage)
            for (int i = 0; i < 4; ++i)
                exact_events = exact_events && gm_mobs_event_get(
                    &r.mobs, i, &event[i]);
        fprintf(stderr, "mob_live: %s health=%.9g events=%d\n",
                fixtures[fixture].name, state->health[slot], events);
        CHECK(state->alive[slot] && state->on_ground[slot]
                  && state->health[slot]
                      == health_before - (float)fixtures[fixture].damage
                  && r.mobs.entity_fall_distance[slot] == 0.0F
                  && r.mobs.entity_hurt_time[slot]
                      == (fixtures[fixture].damage ? 10 : 0)
                  && r.mobs.entity_hurt_resistant[slot]
                      == (fixtures[fixture].damage ? 20 : 0)
                  && exact_events
                  && (!fixtures[fixture].damage
                      || (event[0].kind == GM_MOB_EVENT_SOUND
                          && event[0].data == fixtures[fixture].fall_sound
                          && event[1].kind == GM_MOB_EVENT_ENTITY_STATUS
                          && event[1].data == 2
                          && event[2].kind == GM_MOB_EVENT_SOUND
                          && event[2].data == fixtures[fixture].hurt_sound
                          && event[3].kind == GM_MOB_EVENT_SOUND
                          && event[3].data == fixtures[fixture].support_sound))
                  && particle
                  && landing.descriptor_count
                      == fixtures[fixture].particle_count
                  && landing.parameters[0] == fixtures[fixture].block
                  && (!fixtures[fixture].jump
                      || (r.mobs.entity_effect_count[slot] == 1
                          && r.mobs.entity_effects[slot][0].duration == 19
                          && r.mobs.entity_effects[slot][0].amplifier == 1))
                  && (!fixtures[fixture].fuse
                      || r.mobs.creeper_fuse[slot] == fixtures[fixture].fuse),
              "ordinary living fall variant uses exact shared boundary");
        gm_runtime_destroy(&r);
    }
}

static void test_enderman_fall_teleport(void) {
    const uint64_t seeds[] = {0, 2};
    for (int fixture = 0; fixture < 2; ++fixture) {
        GmRuntime r;
        CHECK(init_flat(&r), "initialize Enderman fall teleport fixture");
        if (fail) return;
        gm_runtime_set_pose(&r, 508.5, 5.0, -2.5, 0.0F, 10.0F);
        gm_world_ensure(r.world, 31, -1, 4);
        int slot = gm_mobs_spawn(
            &r.mobs, EW_TYPE_ENDERMAN, 508.5, 220.0, -2.5);
        CHECK(slot > 0, "spawn Enderman fall teleport fixture");
        if (slot <= 0) {
            gm_runtime_destroy(&r);
            continue;
        }
        EwStore *initial = test_mob_store(&r.mobs);
        int eid = initial->id[slot];
        r.mobs.persistence_required[slot] = 1;
        r.mobs.a.vy[slot] = r.mobs.b.vy[slot] = -0.1D;
        r.mobs.a.on_ground[slot] = r.mobs.b.on_ground[slot] = 0;
        r.mobs.entity_fall_distance[slot] = 5.0F;
        CHECK(gm_mobs_set_entity_random_state(
                  &r.mobs, eid, seeds[fixture], 0, 0.0D),
              "seed Enderman fall teleport private RNG");
        r.math_random_seed48 = 0;
        gm_world_set_block(r.world, 508, 219, -3, 1);
        for (int y = 220; y <= 222; ++y)
            gm_world_set_block(r.world, 508, y, -3, 0);
        test_mobs_tick(&r);

        EwStore *state = test_mob_store(&r.mobs);
        GmMobEvent event[6];
        int expected_events = fixture == 0 ? 6 : 4;
        int events = gm_mobs_event_count(&r.mobs) == expected_events;
        for (int i = 0; i < expected_events; ++i)
            events = events && gm_mobs_event_get(&r.mobs, i, &event[i]);
        GmMobParticleBatch landing;
        int particle = gm_mobs_particle_batch_count(&r.mobs) == 1
            && gm_mobs_particle_batch_get(&r.mobs, 0, &landing);
        int common = state->alive[slot] && state->on_ground[slot]
            && state->health[slot] == 38.0F
            && r.mobs.entity_hurt_time[slot] == 10
            && r.mobs.entity_hurt_resistant[slot] == 20
            && r.mobs.entity_fall_distance[slot] == 0.0F
            && state->vy[slot] == -0.0784000015258789D
            && r.math_random_seed48 == UINT64_C(277363943098)
            && events
            && event[0].data == GM_MOB_SOUND_HOSTILE_SMALL_FALL
            && event[1].kind == GM_MOB_EVENT_ENTITY_STATUS
            && event[1].data == 2
            && event[2].data == GM_MOB_SOUND_ENDERMAN_HURT
            && particle && landing.eid == eid
            && landing.x == 508.5D && landing.y == 220.0D
            && landing.z == -2.5D && landing.particle_id == 38
            && landing.descriptor_count == 50
            && landing.parameters[0] == 1;
        if (fixture == 0) {
            common = common
                && test_double_bits(state->x[slot] - 508.5D)
                    == UINT64_C(0xc03a27a3ae85f2e0)
                && test_double_bits(state->y[slot] - 220.0D)
                    == UINT64_C(0xc06b000000000000)
                && test_double_bits(state->z[slot] + 2.5D)
                    == UINT64_C(0x3ffb646c7455bfa0)
                && r.mobs.entity_random[slot].random.seed
                    == UINT64_C(247599680335554)
                && event[3].data == GM_MOB_SOUND_ENDERMAN_TELEPORT
                && event[4].data == GM_MOB_SOUND_BLOCK_GRASS_FALL
                && event[5].data == GM_MOB_SOUND_BLOCK_STONE_STEP
                && event[3].x == state->x[slot]
                && event[3].y == state->y[slot]
                && event[3].z == state->z[slot]
                && event[5].volume == 0.15F
                && event[5].pitch == 1.0F;
        } else {
            common = common && state->x[slot] == 508.5D
                && state->y[slot] == 220.0D && state->z[slot] == -2.5D
                && r.mobs.entity_random[slot].random.seed
                    == UINT64_C(63052479226681)
                && event[3].data == GM_MOB_SOUND_BLOCK_STONE_FALL;
        }
        fprintf(stderr,
                "mob_live: Enderman fall seed=%" PRIu64
                " pos=(%.17g,%.17g,%.17g) events=%d\n",
                seeds[fixture], state->x[slot], state->y[slot],
                state->z[slot], gm_mobs_event_count(&r.mobs));
        CHECK(common,
              "Enderman fall uses exact teleport/no-teleport product path");
        gm_runtime_destroy(&r);
    }
}

static void test_enderman_water_damage_teleport(void) {
    const uint64_t seeds[] = {0, 82};
    for (int fixture = 0; fixture < 2; ++fixture) {
        GmRuntime r;
        CHECK(init_flat(&r), "initialize Enderman water-damage fixture");
        if (fail) return;
        gm_runtime_set_pose(&r, 508.5, 5.0, -2.5, 0.0F, 10.0F);
        gm_world_ensure(r.world, 31, -1, 4);
        int slot = gm_mobs_spawn(
            &r.mobs, EW_TYPE_ENDERMAN, 508.5, 220.0, -2.5);
        CHECK(slot > 0, "spawn Enderman water-damage fixture");
        if (slot <= 0) {
            gm_runtime_destroy(&r);
            continue;
        }
        EwStore *state = test_mob_store(&r.mobs);
        int eid = state->id[slot];
        CHECK(gm_mobs_set_entity_random_state(
                  &r.mobs, eid, seeds[fixture], 0, 0.0D),
              "seed Enderman water-damage private RNG");
        r.math_random_seed48 = 0;
        CHECK(gm_mobs_apply_water_potion(
                  &r.mobs, r.world, slot, &r.entities,
                  &r.math_random_seed48) == 2,
              "water potion applies Enderman damage boundary");
        state = test_mob_store(&r.mobs);
        GmMobEvent event[3];
        int expected_events = fixture == 0 ? 3 : 2;
        int events = gm_mobs_event_count(&r.mobs) == expected_events;
        for (int i = 0; i < expected_events; ++i)
            events = events && gm_mobs_event_get(&r.mobs, i, &event[i]);
        int common = state->alive[slot] && state->health[slot] == 39.0F
            && r.mobs.entity_hurt_time[slot] == 10
            && r.mobs.entity_hurt_resistant[slot] == 20
            && r.math_random_seed48 == UINT64_C(277363943098)
            && events
            && event[0].kind == GM_MOB_EVENT_ENTITY_STATUS
            && event[0].data == 2
            && event[1].kind == GM_MOB_EVENT_SOUND
            && event[1].data == GM_MOB_SOUND_ENDERMAN_HURT;
        if (fixture == 0) {
            common = common
                && test_double_bits(state->x[slot] - 508.5D)
                    == UINT64_C(0xc034b1e30a2a96d0)
                && test_double_bits(state->y[slot] - 220.0D)
                    == UINT64_C(0xc06b000000000000)
                && test_double_bits(state->z[slot] + 2.5D)
                    == UINT64_C(0xc03a17cb98345bb8)
                && r.mobs.entity_random[slot].random.seed
                    == UINT64_C(119218865845960)
                && event[2].data == GM_MOB_SOUND_ENDERMAN_TELEPORT;
        } else {
            common = common && state->x[slot] == 508.5D
                && state->y[slot] == 220.0D && state->z[slot] == -2.5D
                && r.mobs.entity_random[slot].random.seed
                    == UINT64_C(39122349677367);
        }
        CHECK(common,
              "water damage reuses exact Enderman teleport RNG boundary");
        gm_runtime_destroy(&r);
    }
}

static int sheep_view(GmRuntime *r, GmEntityView *out) {
    GmEntityView views[EW_MAX_ENTITIES];
    int n=gm_mobs_fill_views(&r->mobs,views,EW_MAX_ENTITIES);
    for(int i=0;i<n;++i)if(views[i].type==GM_MOB_SHEEP){*out=views[i];return 1;}
    return 0;
}

static int run_sheep_trace(SheepTrace *trace) {
    static GmRuntime r;memset(trace,0,sizeof *trace);
    trace->panic_tick=-1;
    if(!init_flat_seed(&r,0))return 0;
    gm_runtime_set_pose(&r,8.5,5.0,8.5,0.0f,10.0f);
    int slot=gm_mobs_spawn(&r.mobs,GM_MOB_SHEEP,8.5,5.0,16.5);
    if(slot<0){gm_runtime_destroy(&r);return 0;}
    GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
    int looking=0;
    for(int tick=0;tick<400;++tick){
        gm_runtime_tick(&r,idle);
        GmEntityView v;if(!sheep_view(&r,&v)){gm_runtime_destroy(&r);return 0;}
        SheepSample *s=&trace->samples[trace->nsamples++];
        s->x=v.x;s->z=v.z;s->yaw=v.yaw;s->head_yaw=v.head_yaw;s->pitch=v.pitch;
        s->eat_time=r.mobs.passive_eat_time[slot];
        int now_looking=fabsf(v.head_yaw-v.yaw)>0.5f||fabsf(v.pitch)>0.5f;
        if(now_looking&&!looking&&trace->nhead_onsets<16)
            trace->head_onsets[trace->nhead_onsets++]=tick;
        looking=now_looking;
    }

    /* Component reset isolates the first post-hit MOVE_TO acceleration from
     * any residual wander momentum; the hit and AI path are still ordinary
     * live-sim code. Keep both double buffers coherent. */
    EwStore *cur=test_mob_store(&r.mobs),*other=r.mobs.current?&r.mobs.a:&r.mobs.b;
    cur->vx[slot]=cur->vz[slot]=0.0;cur->path_len[slot]=0;
    other->vx[slot]=other->vz[slot]=0.0;other->path_len[slot]=0;
    r.mobs.passive_tasks[slot]=0;r.mobs.passive_task_tick[slot]=0;
    GmEntityView before;if(!sheep_view(&r,&before)){gm_runtime_destroy(&r);return 0;}
    if(!gm_mobs_damage_near(&r.mobs,before.x,before.y+0.5,before.z,1.0,1.0f,&r.entities)){
        gm_runtime_destroy(&r);return 0;
    }
    for(int tick=0;tick<60;++tick){
        GmEntityView prev;if(!sheep_view(&r,&prev)){gm_runtime_destroy(&r);return 0;}
        gm_runtime_tick(&r,idle);
        GmEntityView next;if(!sheep_view(&r,&next)){gm_runtime_destroy(&r);return 0;}
        double dx=(double)next.x-prev.x,dz=(double)next.z-prev.z;
        double step=sqrt(dx*dx+dz*dz);
        if(step>1e-6){trace->panic_tick=tick;trace->panic_step=step;break;}
    }
    gm_runtime_destroy(&r);return 1;
}

typedef struct {
    int charged_on, charged_off;
    int transitions[8], transition_state[8], ntransitions;
    int shots[16], nshots;
    int first_100_shots;
} BlazeSchedule;

static int run_blaze_ticks(int ticks, FILE *receipt, BlazeSchedule *schedule) {
    static GmRuntime r;
    if(!init_flat(&r))return 0;
    gm_mobs_set_natural_spawning(&r.mobs,0);
    int slot=gm_mobs_spawn(&r.mobs,GM_MOB_BLAZE,8.5,5.0,20.5);
    if(slot<0){gm_runtime_destroy(&r);return 0;}
    memset(schedule,0,sizeof *schedule);
    if(receipt)
        fprintf(receipt,"tick\tattackStep\tattackTime\tcharged\tfireball_spawn\n");
    int previous=-1;
    for(int tick=0;tick<ticks;++tick){
        gm_runtime_set_pose(&r,8.5,5.0,8.5,0.0f,0.0f);
        test_mobs_tick(&r);
        GmBlazeShot shot;
        int fireball=gm_mobs_take_blaze_shot(&r.mobs,slot,&shot);
        EwStore *store=test_mob_store(&r.mobs);
        GmEntityView views[EW_MAX_ENTITIES];
        int nviews=gm_mobs_fill_views(&r.mobs,views,EW_MAX_ENTITIES),charged=0;
        for(int i=0;i<nviews;++i)
            if(views[i].type==GM_MOB_BLAZE){charged=(views[i].flags&1)!=0;break;}
        if(charged)schedule->charged_on++;
        else schedule->charged_off++;
        if(charged!=previous){
            if(schedule->ntransitions<(int)(sizeof schedule->transitions/sizeof schedule->transitions[0])){
                schedule->transitions[schedule->ntransitions]=tick;
                schedule->transition_state[schedule->ntransitions]=charged;
            }
            schedule->ntransitions++;
            previous=charged;
        }
        if(fireball){
            if(schedule->nshots<(int)(sizeof schedule->shots/sizeof schedule->shots[0]))
                schedule->shots[schedule->nshots]=tick;
            schedule->nshots++;
            if(tick<100)schedule->first_100_shots++;
        }
        if(receipt)
            fprintf(receipt,"%d\t%d\t%d\t%d\t%d\n",tick,r.mobs.charge[slot],
                    store->attack_time[slot],charged,fireball);
    }
    gm_runtime_destroy(&r);
    return 1;
}

static int legacy_flat_shots(int ticks, int *first_100) {
    /* Baseline 99007ad^: mob_live reloaded attack_time=40 whenever <=0,
     * then runtime spawned a blaze fireball on each reload edge. */
    int attack_time=0,shots=0;
    *first_100=0;
    for(int tick=0;tick<ticks;++tick){
        if(attack_time>0)--attack_time;
        if(attack_time<=0)attack_time=40;
        if(attack_time==40){++shots;if(tick<100)++*first_100;}
    }
    return shots;
}

static int blaze_task_reset_test(void) {
    static GmRuntime r;
    if(!init_flat(&r))return 0;
    gm_mobs_set_natural_spawning(&r.mobs,0);
    int slot=gm_mobs_spawn(&r.mobs,GM_MOB_BLAZE,8.5,5.0,20.5);
    if(slot<0){gm_runtime_destroy(&r);return 0;}
    for(int tick=0;tick<10;++tick){
        test_mobs_tick(&r);
        GmBlazeShot shot;
        (void)gm_mobs_take_blaze_shot(&r.mobs,slot,&shot);
    }
    int attack_time=test_mob_store(&r.mobs)->attack_time[slot];
    gm_runtime_set_pose(&r,8.5,5.0,70.5,0.0f,0.0f); /* outside follow range */
    test_mobs_tick(&r);
    int paused=test_mob_store(&r.mobs)->attack_time[slot];
    int reset=!r.mobs.blaze_on_fire[slot]&&r.mobs.charge[slot]==0;
    gm_runtime_set_pose(&r,8.5,5.0,8.5,0.0f,0.0f);
    test_mobs_tick(&r);
    int resumed=test_mob_store(&r.mobs)->attack_time[slot];
    gm_runtime_destroy(&r);
    return attack_time==51&&paused==51&&reset&&resumed==50;
}

static int blaze_schedule_receipt(FILE *receipt) {
    BlazeSchedule two_cycles,long_run;
    CHECK(run_blaze_ticks(356,receipt,&two_cycles),
          "deterministic blaze schedule executes");
    if(fail)return 0;
    CHECK(two_cycles.charged_on==156&&two_cycles.charged_off==200,
          "two blaze cycles reproduce 78 charged / 100 uncharged ticks each");
    CHECK(two_cycles.ntransitions==4&&
          two_cycles.transitions[0]==0&&two_cycles.transition_state[0]==1&&
          two_cycles.transitions[1]==78&&two_cycles.transition_state[1]==0&&
          two_cycles.transitions[2]==178&&two_cycles.transition_state[2]==1&&
          two_cycles.transitions[3]==256&&two_cycles.transition_state[3]==0,
          "blaze charged transitions reproduce the 178-tick vanilla cycle");
    CHECK(two_cycles.nshots==6&&
          two_cycles.shots[0]==60&&two_cycles.shots[1]==66&&two_cycles.shots[2]==72&&
          two_cycles.shots[3]==238&&two_cycles.shots[4]==244&&two_cycles.shots[5]==250,
          "blaze fires three-shot volleys after charge at six-tick spacing");
    CHECK(blaze_task_reset_test(),
          "resetTask clears charge while inactive attackTime stays paused");
    CHECK(run_blaze_ticks(3560,NULL,&long_run),
          "long-run live blaze cadence executes");
    int old_first_100=0,old_shots=legacy_flat_shots(3560,&old_first_100);
    CHECK(old_shots==89&&long_run.nshots==60,
          "old and vanilla live cadence counts match exact common horizon");
    CHECK(old_first_100==3&&long_run.first_100_shots==3,
          "both schedules spawn three fireballs in the first 100 aggro ticks");
    if(receipt){
        fprintf(receipt,"# charged_on=%d charged_off=%d cycles=2 duty=78_on/100_off\n",
                two_cycles.charged_on,two_cycles.charged_off);
        fprintf(receipt,"# transitions=0:on,78:off,178:on,256:off\n");
        fprintf(receipt,"# fireballs=60,66,72,238,244,250\n");
        fprintf(receipt,"# cadence_horizon_ticks=3560 old_flat_shots=%d "
                        "new_vanilla_shots=%d old_per_100=%.9f new_per_100=%.9f\n",
                old_shots,long_run.nshots,old_shots*100.0/3560.0,
                long_run.nshots*100.0/3560.0);
        fprintf(receipt,"# first_100 old_flat_shots=%d new_vanilla_shots=%d\n",
                old_first_100,long_run.first_100_shots);
    }
    fprintf(stderr,"mob_live: blaze duty on=%d off=%d shots=[%d,%d,%d,%d,%d,%d] "
                   "cadence/100 old=%.9f new=%.9f\n",
            two_cycles.charged_on,two_cycles.charged_off,
            two_cycles.shots[0],two_cycles.shots[1],two_cycles.shots[2],
            two_cycles.shots[3],two_cycles.shots[4],two_cycles.shots[5],
            old_shots*100.0/3560.0,long_run.nshots*100.0/3560.0);
    return !fail;
}

int main(int argc, char **argv) {
    {
        GmMobLive mobs;
        PvStats vitals;
        IsrInv inventory;
        gm_mobs_init(&mobs, 0);
        pv_init(&vitals);
        isr_init(&inventory);
        gm_mobs_set_player_disable_damage(&mobs, 1);
        CHECK(gm_mobs_attack_player(
                  &mobs, (struct PvStats *)&vitals, &inventory,
                  4.0F, 1) == 0
                  && vitals.health == 20.0F
                  && mobs.player_hurt_resistant == 0,
              "spectator/creative capability rejects damage without side effects");
        gm_mobs_set_player_disable_damage(&mobs, 0);
        CHECK(gm_mobs_attack_player(
                  &mobs, (struct PvStats *)&vitals, &inventory,
                  4.0F, 1) != 0
                  && vitals.health == 16.0F
                  && mobs.player_hurt_resistant == 20,
              "survival/adventure capability accepts the same damage");
        gm_mobs_destroy(&mobs);
    }
    if (argc == 2 && !strcmp(argv[1], "--capability-receipt")) {
        if (fail) return 1;
        fprintf(stderr, "mob_live capability: PASS\n");
        return 0;
    }
    if(argc==3&&!strcmp(argv[1],"--blaze-receipt")){
        FILE *receipt=fopen(argv[2],"w");
        if(!receipt){perror(argv[2]);return 1;}
        int ok=blaze_schedule_receipt(receipt);
        if(fclose(receipt)!=0){perror(argv[2]);return 1;}
        return ok?0:1;
    }
    if(argc!=1){fprintf(stderr,"usage: %s [--capability-receipt | --blaze-receipt PATH]\n",argv[0]);return 2;}
    test_natural_spawn_shuffle();
    test_natural_spawn_checkpoint();
    test_shared_living_slime_bounce();
    test_shared_living_nonlethal_fall();
    test_shared_living_fall_variants();
    test_enderman_fall_teleport();
    test_enderman_water_damage_teleport();
    static GmRuntime r;
    if(!init_flat(&r))return 1;
    CHECK(gm_mobs_spawn(&r.mobs,EW_TYPE_ZOMBIE,8.5,5.0,14.5)>=0,"spawn component zombie");
    GmEntityView v[EW_MAX_ENTITIES];
    int n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES); double z0=n?v[0].z:0.0;
    GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
    for(int i=0;i<8;++i)gm_runtime_tick(&r,idle);
    n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);
    fprintf(stderr,"mob_live: chase z %.6f -> %.6f y %.6f\n",z0,n?v[0].z:0.0,n?v[0].y:0.0);
    CHECK(n==1 && v[0].z<z0,"hostile chases through living-base physics");
    CHECK(v[0].y>=3.99f,"hostile collision does not fall through superflat floor");
    gm_runtime_destroy(&r);

    /* EntityDragon.collideWithEntities: a recorded expanded wing-part query
     * applies causeMobDamage(5), while a non-overlapping part does nothing.
     * A stronger raw hit inside hurtResistantTime applies only the delta, per
     * EntityLivingBase.attackEntityFrom. */
    if(!init_flat(&r))return 1;
    CHECK(gm_runtime_dragon_contact(&r,7.0,4.0,7.0,10.0,8.0,10.0,5.0f),
          "dragon wing contact query hits player");
    CHECK(r.vitals.health==15.0f,"dragon wing contact subtracts exact 5 hp");
    CHECK(!gm_runtime_dragon_contact(&r,30.0,4.0,30.0,34.0,8.0,34.0,5.0f),
          "non-overlapping dragon part does not damage player");
    CHECK(gm_runtime_dragon_contact(&r,7.0,4.0,7.0,10.0,8.0,10.0,6.0f),
          "stronger contact passes hurt resistance");
    CHECK(r.vitals.health==14.0f,
          "hurt-resistant stronger contact applies lastDamage delta");
    gm_runtime_destroy(&r);

    /* ProjectileHelper includes EntityDragon.getParts() after the parent
     * broadphase. Every part and crystal uses expandXyz(0.30000001192092896),
     * and a small fireball is consumed even when its blaze source cannot
     * damage the parent dragon. */
    GmDragonLive dragon;memset(&dragon,0,sizeof dragon);dragon.initialized=1;
    EdDragon *dg=&dragon.state.arena.dragon;
    dg->alive=1;dg->health=200.0f;dg->x=10.0;dg->y=20.0;dg->z=30.0;
    dg->head_x=10.0;dg->head_y=23.0;dg->head_z=36.0;
    int dragon_target=0;double dragon_dist=0.0;
    CHECK(gm_dragon_projectile_intercept(
              &dragon,10.0,22.0,18.0,10.0,22.0,40.0,
              &dragon_target,&dragon_dist)
              && dragon_target>0&&dragon_dist>0.0,
          "small fireball ray selects the nearest expanded dragon part");
    CHECK(!gm_dragon_projectile_intercept(
              &dragon,17.0,22.0,18.0,17.0,22.0,40.0,0,0),
          "small fireball negative ray misses every multipart box");
    dragon.state.arena.crystals[0].alive=1;
    dragon.state.arena.crystals[0].x=42.5;
    dragon.state.arena.crystals[0].y=78.0;
    dragon.state.arena.crystals[0].z=0.5;
    CHECK(gm_dragon_projectile_intercept(
              &dragon,42.5,79.0,-4.0,42.5,79.0,5.0,
              &dragon_target,&dragon_dist)
              && dragon_target==-1,
          "small fireball ray includes the exact two-block crystal box");
    GmDragonCrystalHit crystal_hit;
    CHECK(gm_dragon_small_fireball_hit(
              &dragon,dragon_target,&crystal_hit)==2
              && !dragon.state.arena.crystals[0].alive
              && crystal_hit.index==0
              && crystal_hit.x==42.5&&crystal_hit.y==78.0
              && crystal_hit.z==0.5,
          "crystal candidate is destroyed and returns its explosion center");

    if(!init_flat(&r))return 1;
    r.dimension=1;r.dragon.initialized=1;
    dg=&r.dragon.state.arena.dragon;
    dg->alive=1;dg->health=200.0f;dg->max_health=200.0f;
    dg->x=8.5;dg->y=20.0;dg->z=8.5;
    dg->target_x=dg->x;dg->target_y=dg->y;dg->target_z=dg->z;
    dg->phase=ED_PHASE_HOVER;
    CHECK(gm_runtime_spawn_small_fireball_fixture(
              &r,9901,8.5,22.0,0.0,0.0,0.0,10.0,0.0,0.0,0.0),
          "spawn controlled dragon multipart fireball");
    gm_runtime_tick(&r,idle);
    int live_fireballs=0;
    for(int i=0;i<GM_RUNTIME_PROJECTILES;++i)
        live_fireballs+=r.projectiles[i].active&&r.projectiles[i].type==3;
    CHECK(live_fireballs==0,
          "live small fireball is consumed by the nearest dragon part");
    CHECK(dg->health==200.0f,
          "non-player small fireball does not damage the parent dragon");
    gm_runtime_destroy(&r);

    /* EntityZombie.applyEntityAttributes ATTACK_DAMAGE=3.0;
     * EntityPlayer.attackEntityFrom leaves it unchanged on NORMAL. After ten
     * FoodStats.onUpdate ticks, saturation regen heals 5/6 exactly, for a net
     * first-hit loss of 3 - 5/6 = 13/6. */
    if(!init_flat(&r))return 1;
    CHECK(gm_mobs_spawn(&r.mobs,EW_TYPE_ZOMBIE,8.5,5.0,10.5)>=0,"spawn damage-model zombie");
    gm_runtime_tick(&r,idle);
    CHECK(r.vitals.health==17.0f,"normal zombie melee subtracts exact 3 hp");
    for(int i=0;i<10;++i)gm_runtime_tick(&r,idle);
    CHECK(r.vitals.health==17.0f+5.0f/6.0f,
          "first zombie hit plus saturation regen loses exact 13/6 hp");
    gm_runtime_destroy(&r);

    /* EntityWitherSkeleton.onInitialSpawn sets base ATTACK_DAMAGE=4, and its
     * stone sword supplies a +4 ItemSword attribute modifier. The freshly
     * applied duration-200 wither tick is rejected by hurtResistantTime; the
     * duration-160 tick drains one hp after the player leaves melee reach. */
    if(!init_flat(&r))return 1;
    r.vitals.foodLevel=0;r.vitals.saturation=0.0f;
    CHECK(gm_mobs_spawn(&r.mobs,GM_MOB_WITHER_SKELETON,8.5,5.0,10.5)>=0,
          "spawn damage-model wither skeleton");
    gm_runtime_tick(&r,idle);
    CHECK(r.vitals.health==12.0f,"wither skeleton melee subtracts exact 4+4 hp");
    CHECK(r.mobs.player_wither_ticks==200,"wither skeleton applies 200-tick wither");
    gm_runtime_set_pose(&r,8.5,5.0,40.5,0.0f,10.0f);
    for(int i=0;i<40;++i)gm_runtime_tick(&r,idle);
    CHECK(r.vitals.health==12.0f,
          "duration-200 wither pulse is blocked by melee hurt resistance");
    gm_runtime_tick(&r,idle);
    CHECK(r.vitals.health==11.0f&&r.mobs.player_wither_ticks==159,
          "MobEffects.WITHER duration-160 pulse drains one hp");
    gm_runtime_destroy(&r);

    /* EntityVindicator has base ATTACK_DAMAGE=5. Its first targetless AI
     * update still equips the iron axe, whose main-hand modifier adds 8.
     * Mansion residents have completed that update before a player arrives. */
    if(!init_flat(&r))return 1;
    r.vitals.foodLevel=0;r.vitals.saturation=0.0f;
    CHECK(gm_mobs_spawn(&r.mobs,EW_TYPE_VINDICATOR,8.5,5.0,10.5)>=0,
          "spawn damage-model vindicator");
    gm_runtime_tick(&r,idle);
    CHECK(r.vitals.health==7.0f,
          "vindicator melee subtracts exact base 5 + iron axe 8");
    gm_runtime_destroy(&r);

    /* EntityEvoker has no melee task. At close range its priority-2 avoid
     * task moves away while the spell tasks own offense. */
    if(!init_flat(&r))return 1;
    r.vitals.foodLevel=0;r.vitals.saturation=0.0f;
    CHECK(gm_mobs_spawn(&r.mobs,EW_TYPE_EVOKER,8.5,5.0,10.5)>=0,
          "spawn non-melee evoker");
    gm_runtime_tick(&r,idle);
    n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);
    CHECK(r.vitals.health==20.0f && n==1
              && (v[0].flags & 512) != 0 && v[0].z==10.5f,
          "evoker summon cast owns movement without inventing melee damage");
    gm_runtime_destroy(&r);

    if(!init_flat(&r))return 1;
    gm_mobs_set_natural_spawning(&r.mobs,0);
    isr_set_stack(&r.player.inv,0,ic_mk(276,1,0));
    CHECK(gm_mobs_spawn(&r.mobs,EW_TYPE_ZOMBIE,8.5,5.0,10.5)>=0,"spawn combat zombie");
    r.vitals.health=20.0F;r.player.health=20.0F;
    /* Repeated physical click pulses. Runtime intentionally consumes the
     * CPacketUseEntity one locked tick after the client press edge; merely
     * holding attack does not synthesize repeated entity attacks. */
    GmAction attack;memset(&attack,0,sizeof attack);
    attack.attack=1;attack.do_break=1;attack.hotbar_sel=0;
    double combat_x=8.5,combat_y=5.0,combat_z=10.5;
    for(int i=0;i<440 && gm_mobs_alive(&r.mobs);++i){
        GmAction step=idle;
        int phase=i%22;
        if(phase<=1){
            n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);
            for(int k=0;k<n;++k)if(v[k].type==EW_TYPE_ZOMBIE){
                combat_x=v[k].x;combat_y=v[k].y;combat_z=v[k].z;
                gm_runtime_set_pose(&r,v[k].x,v[k].y,v[k].z-2.0,0.0f,10.0f);
                if(phase==0)step=attack;
                break;}
        }else gm_runtime_set_pose(&r,8.5,30.0,8.5,0.0f,10.0f);
        gm_runtime_tick(&r,step);
    }
    float post_combat_health=r.vitals.health;
    CHECK(gm_mobs_alive(&r.mobs)==0,"repeated click attacks kill hostile under cooldown");
    int xp_visible=0;
    for(int i=0;i<GM_XP_ORBS;++i)
        xp_visible|=!r.mobs.xp_orbs[i].dead&&r.mobs.xp_orbs[i].xpValue>0;
    CHECK(xp_visible&&r.mobs.xp_total==0,"hostile death creates XP entities before pickup");
    gm_runtime_set_pose(&r,combat_x,combat_y,combat_z,0.0f,10.0f);
    for(int i=0;i<200&&r.mobs.xp_total<5;++i)gm_runtime_tick(&r,idle);
    CHECK(r.mobs.xp_total==5,"XP entities attract, collide, and award route XP");
    int flesh=0;
    for(int i=0;i<GM_LIVE_MAX;++i)if(r.entities.ents[i].active&&r.entities.ents[i].item==367)flesh=1;
    for(int i=0;i<ISR_MAIN_SLOTS;++i)
        if(isr_get_stack(&r.player.inv,i).item==367)flesh=1;
    CHECK(flesh,"zombie death creates rotten-flesh item entity");
    CHECK(post_combat_health<20.0f&&post_combat_health>0.0f,
          "in-reach hostile damages player without killing test");
    gm_runtime_destroy(&r);

    if(!init_flat(&r))return 1;
    r.gamerules.doMobSpawning=0;
    isr_set_stack(&r.player.inv,0,ic_mk(276,1,0));
    CHECK(gm_mobs_spawn(&r.mobs,GM_MOB_BLAZE,8.5,5.0,10.5)>=0,"spawn component blaze");
    r.vitals.health=20.0F;r.player.health=20.0F;
    double blaze_x=8.5,blaze_y=5.0,blaze_z=10.5;
    for(int i=0;i<440&&gm_mobs_alive(&r.mobs);++i){
        GmAction step=idle;
        int phase=i%22;
        if(phase<=1){
            n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);
            for(int k=0;k<n;++k)if(v[k].type==GM_MOB_BLAZE){
                blaze_x=v[k].x;blaze_y=v[k].y;blaze_z=v[k].z;
                gm_runtime_set_pose(&r,v[k].x,v[k].y,v[k].z-2.0,0.0f,10.0f);
                if(phase==0)step=attack;
                break;}
        }else gm_runtime_set_pose(&r,8.5,30.0,8.5,0.0f,10.0f);
        gm_runtime_tick(&r,step);
    }
    int rod=0;for(int i=0;i<GM_LIVE_MAX;++i)if(r.entities.ents[i].active&&r.entities.ents[i].item==369)rod=1;
    for(int i=0;i<ISR_MAIN_SLOTS;++i)
        if(isr_get_stack(&r.player.inv,i).item==369)rod=1;
    gm_runtime_set_pose(&r,blaze_x,blaze_y,blaze_z,0.0f,10.0f);
    for(int i=0;i<200&&r.mobs.xp_total<10;++i)gm_runtime_tick(&r,idle);
    CHECK(gm_mobs_alive(&r.mobs)==0&&r.mobs.xp_total==10,"blaze XP entities reach the player");
    CHECK(rod,"blaze deterministic loot roll can produce a blaze rod");
    gm_runtime_destroy(&r);

    SheepTrace sheep_a,sheep_b;
    CHECK(run_sheep_trace(&sheep_a)&&run_sheep_trace(&sheep_b),
          "deterministic sheep scenarios execute");
    CHECK(!memcmp(&sheep_a,&sheep_b,sizeof sheep_a),
          "same seed and world produce byte-identical passive AI traces");
    fprintf(stderr,"mob_live: sheep head onsets=%d first=[%d,%d,%d] panic_tick=%d step=%.17g\n",
            sheep_a.nhead_onsets,sheep_a.head_onsets[0],sheep_a.head_onsets[1],
            sheep_a.head_onsets[2],sheep_a.panic_tick,sheep_a.panic_step);
    CHECK(sheep_a.nhead_onsets>=2,"idle sheep produces hash-determined visible head-look events");
    CHECK(sheep_a.head_onsets[0]==45&&sheep_a.head_onsets[1]==330,
          "seed-0 sheep head-look onset ticks stay hash-determined");
    CHECK(sheep_a.panic_tick==0&&sheep_a.panic_step>0.05,
          "damage starts deterministic sheep panic movement immediately");

    /* EntityAIEatGrass update edge: timer 5 -> 4 consumes grass, converts it
     * to dirt under mobGriefing, and calls EntitySheep.eatGrassBonus. */
    if(!init_flat(&r))return 1;
    int grazer=gm_mobs_spawn(&r.mobs,GM_MOB_SHEEP,8.5,5.0,10.5);
    CHECK(grazer>=0,"spawn grass-eating sheep");
    for(int i=0;i<4;++i)gm_runtime_tick(&r,idle);
    GmEntityView graze_view;CHECK(sheep_view(&r,&graze_view),"grass-eating sheep is visible");
    int gbx=(int)floor(graze_view.x),gby=(int)floor(graze_view.y),gbz=(int)floor(graze_view.z);
    gm_world_set_block(r.world,gbx,gby-1,gbz,2);
    r.mobs.sheep_eat_timer[grazer]=5;
    r.mobs.sheep_data[grazer]|=16;
    test_mob_store(&r.mobs)->path_len[grazer]=0;
    gm_runtime_tick(&r,idle);
    CHECK(gm_world_block(r.world,gbx,gby-1,gbz)==3,
          "eat-grass timer 4 converts grass block to dirt");
    CHECK((r.mobs.sheep_data[grazer]&16)==0,
          "eatGrassBonus regrows sheep wool");
    CHECK(sheep_view(&r,&graze_view)&&graze_view.graze_y==1.0f,
          "eat-grass timer reaches live sheep render pose");
    gm_runtime_destroy(&r);

    if(!init_flat(&r))return 1;
    r.gamerules.doMobSpawning=0;
    isr_set_stack(&r.player.inv,0,ic_mk(276,1,0));
    CHECK(gm_mobs_spawn(&r.mobs,GM_MOB_SHEEP,8.5,5.0,10.5)>=0,"spawn component sheep");
    /* Sheep panic-flee when hurt now: chase it like kill_hook_mob does. */
    double sheep_z0=10.5,sheep_run=0.0;
    for(int i=0;i<440;++i){
        n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);int mi=-1;
        for(int k=0;k<n;++k)if(v[k].type==GM_MOB_SHEEP)mi=k;
        if(mi<0)break;
        if(v[mi].z-sheep_z0>sheep_run)sheep_run=v[mi].z-sheep_z0;
        gm_runtime_set_pose(&r,v[mi].x,v[mi].y,v[mi].z-2.0,0.0f,10.0f);
        gm_runtime_tick(&r,(i%22)==0?attack:idle);
    }
    CHECK(sheep_run>0.5,"passive panics away from damage source when hurt");
    int wool=0,mutton=0;for(int i=0;i<GM_LIVE_MAX;++i)if(r.entities.ents[i].active){
        wool|=r.entities.ents[i].item==35;mutton|=r.entities.ents[i].item==423;
    }
    for(int i=0;i<ISR_MAIN_SLOTS;++i){
        ICStack collected=isr_get_stack(&r.player.inv,i);
        wool|=collected.item==35;mutton|=collected.item==423;
    }
    for(int i=0;i<200&&r.mobs.xp_total<1;++i)gm_runtime_tick(&r,idle);
    CHECK(wool&&mutton&&r.mobs.xp_total==1,"sheep death creates wool, food, and collectible XP entities");
    gm_runtime_destroy(&r);

    /* (a) hostile beyond follow range ignores the player and wanders. */
    if(!init_flat(&r))return 1;
    CHECK(gm_mobs_spawn(&r.mobs,EW_TYPE_CREEPER,8.5,5.0,45.5)>=0,"spawn far creeper");
    double cw_x0=8.5,cw_z0=45.5;
    double moved=0.0,mind=37.0;int seen=0;
    for(int i=0;i<300;++i){
        gm_runtime_set_pose(&r,8.5,5.0,8.5,0.0f,10.0f);
        gm_runtime_tick(&r,idle);
        n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);
        seen=0;
        for(int k=0;k<n;++k)if(v[k].type==EW_TYPE_CREEPER){
            seen=1;
            double mx=v[k].x-cw_x0,mz=v[k].z-cw_z0;
            double dd=sqrt(mx*mx+mz*mz);if(dd>moved)moved=dd;
            double dp=sqrt((v[k].x-8.5)*(v[k].x-8.5)+(v[k].z-8.5)*(v[k].z-8.5));
            if(dp<mind)mind=dp;
        }
        if(!seen)break;
    }
    fprintf(stderr,"mob_live: wander moved %.3f mind %.3f\n",moved,mind);
    CHECK(seen,"out-of-range hostile neither despawns nor explodes in 300 ticks");
    CHECK(moved>1.0,"out-of-range hostile wanders (position changes)");
    CHECK(mind>16.0,"out-of-range hostile never approaches within follow range");
    gm_runtime_destroy(&r);

    /* (c) zombie under open daytime sky burns and dies with drops. */
    if(!init_flat(&r))return 1;
    int daylight_slot=gm_mobs_spawn(&r.mobs,EW_TYPE_ZOMBIE,8.5,5.0,10.5);
    CHECK(daylight_slot>0,"spawn daylight zombie");
    r.mobs.a.health[daylight_slot]=1.0F;
    r.mobs.b.health[daylight_slot]=1.0F;
    CHECK(gm_mobs_set_entity_random_state(
              &r.mobs, test_mob_store(&r.mobs)->id[daylight_slot],
              0, 0, 0.0),
          "seed daylight-zombie loot cursor");
    gm_runtime_set_time(&r, 1000);
    gm_runtime_set_weather_full(
        &r, 0, 0, 100000, 100000, 100000, 1,
        0.0F, 0.0F, 0.0F, 0.0F);
    int burned_dead=0,burn_flag=0;
    for(int i=0;i<900;++i){
        gm_runtime_set_pose(&r,8.5,30.0,8.5,0.0f,10.0f); /* out of melee, in range */
        gm_runtime_tick(&r,idle);
        n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);
        for(int k=0;k<n;++k)
            if(v[k].type==EW_TYPE_ZOMBIE&&(v[k].flags&1)!=0)burn_flag=1;
        if(gm_mobs_alive(&r.mobs)==0){burned_dead=1;break;}
    }
    CHECK(burn_flag,"surface zombie fire ticks reach the live render flags");
    CHECK(burned_dead,"surface zombie burns to death in daytime");
    int burn_flesh=0;
    for(int i=0;i<GM_LIVE_MAX;++i)if(r.entities.ents[i].active&&r.entities.ents[i].item==367)burn_flesh=1;
    CHECK(burn_flesh,"daylight burn death still drops loot");
    gm_runtime_destroy(&r);

    /* Recorder flags bit 0 follows generic Entity.isBurning for living mobs. */
    if(!init_flat(&r))return 1;
    int burning_zombie=gm_mobs_spawn(&r.mobs,EW_TYPE_ZOMBIE,8.5,5.0,10.5);
    CHECK(burning_zombie>=0,"spawn daylight-burning view zombie");
    gm_runtime_tick(&r,idle);
    n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);
    int burning_view=0;
    for(int k=0;k<n;++k)
        if(v[k].type==EW_TYPE_ZOMBIE&&(v[k].flags&1))burning_view=1;
    CHECK(r.mobs.fire_ticks[burning_zombie]>0&&burning_view,
          "generic live fire_ticks populate recorder-compatible flags bit 0");
    gm_runtime_destroy(&r);

    /* (d) zombie under a stone roof does NOT burn in daytime. */
    if(!init_flat(&r))return 1;
    r.gamerules.doMobSpawning=0;
    for(int x=-12;x<=30;++x)for(int z=-12;z<=30;++z)gm_world_set_block(r.world,x,9,z,1);
    int roofed_slot = gm_mobs_spawn(
        &r.mobs,EW_TYPE_ZOMBIE,8.5,5.0,10.5);
    CHECK(roofed_slot>=0,"spawn roofed zombie");
    if (roofed_slot >= 0) {
        r.mobs.persistence_required[roofed_slot] = 1;
        r.mobs.controlled_no_ai[roofed_slot] = 1;
    }
    for(int i=0;i<900;++i){
        gm_runtime_set_pose(&r,8.5,30.0,8.5,0.0f,10.0f);
        gm_runtime_tick(&r,idle);
    }
    CHECK(gm_mobs_alive(&r.mobs)==1,"roofed zombie survives the whole day loop");
    n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);
    int zi=-1;for(int k=0;k<n;++k)if(v[k].type==EW_TYPE_ZOMBIE)zi=k;
    CHECK(zi>=0&&v[zi].health>=19.0f,"roofed zombie takes no burn damage");
    gm_runtime_destroy(&r);

    /* (e) hostile beyond 128 blocks despawns instantly. */
    if(!init_flat(&r))return 1;
    CHECK(gm_mobs_spawn(&r.mobs,EW_TYPE_ZOMBIE,8.5,5.0,10.5)>=0,"spawn despawn zombie");
    CHECK(gm_mobs_alive(&r.mobs)==1,"despawn zombie starts alive");
    for(int i=0;i<3;++i){
        gm_runtime_set_pose(&r,8.5,5.0,160.5,0.0f,10.0f);
        gm_runtime_tick(&r,idle);
    }
    CHECK(gm_mobs_alive(&r.mobs)==0,"hostile beyond 128 blocks despawns");
    gm_runtime_destroy(&r);

    if(!init_flat(&r))return 1;
    r.gamerules.doMobSpawning=1;
    gm_mobs_set_natural_spawning(&r.mobs,1);
    r.clock.world_time=13000;
    for(int i=0;i<200&&!gm_mobs_alive(&r.mobs);++i)gm_runtime_tick(&r,idle);
    CHECK(gm_mobs_alive(&r.mobs)>0,"night cycle naturally spawns a light-gated hostile");
    gm_runtime_destroy(&r);

    /* ---- New roster types ---- */

    /* Pigman: neutral until hurt, then group anger. */
    if(!init_flat(&r))return 1;
    r.vitals.foodLevel=0;r.vitals.saturation=0.0f;
    CHECK(gm_mobs_spawn(&r.mobs,GM_MOB_PIGMAN,8.5,5.0,14.5)>=0,"spawn pigman A");
    CHECK(gm_mobs_spawn(&r.mobs,GM_MOB_PIGMAN,10.5,5.0,14.5)>=0,"spawn pigman B");
    for(int i=0;i<10;++i)gm_runtime_tick(&r,idle);
    CHECK(r.vitals.health==20.0f,"neutral pigmen do not attack");
    isr_set_stack(&r.player.inv,0,ic_mk(276,1,0));
    GmAction hit;memset(&hit,0,sizeof hit);
    hit.attack=1;hit.do_break=1;hit.hotbar_sel=0;
    n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);
    int pigman_target=-1;
    for(int k=0;k<n;++k)if(v[k].type==GM_MOB_PIGMAN){pigman_target=k;break;}
    CHECK(pigman_target>=0,"find live pigman target after neutral wander");
    if(pigman_target>=0)
        gm_runtime_set_pose(&r,v[pigman_target].x,v[pigman_target].y,
                            v[pigman_target].z-2.0,0.0f,10.0f);
    for(int i=0;i<5;++i)gm_runtime_tick(&r,i==0?hit:idle);
    CHECK(r.mobs.anger[1]>0||r.mobs.anger[2]>0,"hurt pigman becomes angry");
    int both_angry=(r.mobs.anger[1]>0)+(r.mobs.anger[2]>0);
    CHECK(both_angry>=2,"nearby pigman group-angers");
    gm_runtime_set_pose(&r,8.5,5.0,10.5,0.0f,10.0f);
    float hp0=r.vitals.health;
    for(int i=0;i<40;++i)gm_runtime_tick(&r,idle);
    CHECK(r.vitals.health<hp0,"angry pigman melee damages player");
    gm_runtime_destroy(&r);

    /* Ghast: flight + fireball pending. */
    if(!init_flat(&r))return 1;
    CHECK(gm_mobs_spawn(&r.mobs,GM_MOB_GHAST,8.5,10.0,20.5)>=0,"spawn ghast");
    double gy0=0;{GmEntityView vv[EW_MAX_ENTITIES];int nn=gm_mobs_fill_views(&r.mobs,vv,EW_MAX_ENTITIES);
        for(int k=0;k<nn;++k)if(vv[k].type==GM_MOB_GHAST)gy0=vv[k].y;}
    int saw_fireball=0,rendered_fireball=0;
    for(int i=0;i<60;++i){
        gm_runtime_tick(&r,idle);
        for(int k=0;k<GM_RUNTIME_PROJECTILES;++k)
            if(r.projectiles[k].active&&r.projectiles[k].type==5){
                GmEntityView shots[GM_RUNTIME_PROJECTILES];
                int sn=gm_runtime_projectile_views(&r,shots,GM_RUNTIME_PROJECTILES);
                saw_fireball=1;
                for(int q=0;q<sn;++q)
                    if(shots[q].type==GM_VIEW_BILLBOARD&&shots[q].item_id==385)
                        rendered_fireball=1;
            }
    }
    {GmEntityView vv[EW_MAX_ENTITIES];int nn=gm_mobs_fill_views(&r.mobs,vv,EW_MAX_ENTITIES);
        int found=0;for(int k=0;k<nn;++k)if(vv[k].type==GM_MOB_GHAST){
            found=1;CHECK(vv[k].y>4.0f,"ghast remains airborne");}
        CHECK(found,"ghast remains in live entity store after flight ticks");
    }
    CHECK(gm_mobs_alive(&r.mobs)==1,"ghast survives flight ticks");
    CHECK(saw_fireball,"ghast charge produces a live large-fireball projectile");
    CHECK(rendered_fireball,"ghast large fireball uses the live fire-charge render path");
    (void)gy0;
    gm_runtime_destroy(&r);

    /* A saturated hot projectile pool grows without discarding or delaying a
     * pending ghast shot. fireball_pending stores type (5=large). */
    if(!init_flat(&r))return 1;
    r.mobs.fireball_pending=5;
    r.mobs.fireball_x=8.5;r.mobs.fireball_y=8.0;r.mobs.fireball_z=12.5;
    r.mobs.fireball_vz=-0.5;
    for(int k=0;k<GM_RUNTIME_PROJECTILES;++k){
        r.projectiles[k].active=1;r.projectiles[k].type=4;
        r.projectiles[k].x=1000.0+k;r.projectiles[k].y=100.0;
        r.projectiles[k].z=1000.0;r.projectiles[k].vz=0.1;
    }
    gm_runtime_tick(&r,idle);
    CHECK(!r.mobs.fireball_pending
              && r.projectiles_cap > GM_RUNTIME_PROJECTILES
              && r.projectiles[GM_RUNTIME_PROJECTILES].active
              && r.projectiles[GM_RUNTIME_PROJECTILES].type == 5,
          "saturated hot projectile pool grows for pending ghast fireball");
    gm_runtime_destroy(&r);

    /* Magma cube: unlike slime, every size attacks for size + 2. */
    if(!init_flat(&r))return 1;
    r.vitals.foodLevel=0;r.vitals.saturation=0.0f;
    CHECK(gm_mobs_spawn_sized(&r.mobs,GM_MOB_MAGMA,8.5,5.0,9.5,1)>=0,
          "spawn size-1 damage-model magma");
    gm_runtime_tick(&r,idle);
    CHECK(r.vitals.health==17.0f,"size-1 magma melee deals size + 2 damage");
    gm_runtime_destroy(&r);

    /* Magma cube: size, jump, split on death. */
    if(!init_flat(&r))return 1;
    CHECK(gm_mobs_spawn_sized(&r.mobs,GM_MOB_MAGMA,8.5,5.0,10.5,2)>=0,"spawn size-2 magma");
    CHECK(r.mobs.size[1]==2,"magma size stored");
    /* Deterministic death via damage_near (player melee reach is flaky on hoppers). */
    CHECK(gm_mobs_damage_near(&r.mobs,8.5,5.5,10.5,2.0,100.0f,&r.entities),
          "magma takes lethal damage");
    int smalls=0;
    {const EwStore *s=r.mobs.current?&r.mobs.b:&r.mobs.a;
        for(int i=1;i<EW_MAX_ENTITIES;++i)
            if(s->alive[i]&&s->type[i]==GM_MOB_MAGMA&&r.mobs.size[i]==1)++smalls;}
    CHECK(smalls>=2,"magma size-2 death splits into two size-1 cubes");
    gm_runtime_destroy(&r);

    /* Slime: size-1 drop slime ball. */
    if(!init_flat(&r))return 1;
    CHECK(gm_mobs_spawn_sized(&r.mobs,GM_MOB_SLIME,8.5,5.0,10.5,1)>=0,"spawn size-1 slime");
    CHECK(gm_mobs_damage_near(&r.mobs,8.5,5.5,10.5,2.0,100.0f,&r.entities),
          "slime takes lethal damage");
    int ball=0;for(int i=0;i<GM_LIVE_MAX;++i)
        if(r.entities.ents[i].active&&r.entities.ents[i].item==341)ball=1;
    CHECK(gm_mobs_alive(&r.mobs)==0&&ball,"size-1 slime drops slime ball");
    gm_runtime_destroy(&r);

    /* Default-NBT MobSpawnerBaseLogic calls EntitySlime.onInitialSpawn. Its
     * private RNG selects size before the common Living Gaussian/handedness
     * draws; this is the same helper used by periodic natural slimes. */
    if(!init_flat(&r))return 1;
    uint64_t slime_entity_generator=1;
    int expected_natural_slime_size=2;
    for(;slime_entity_generator<1000&&expected_natural_slime_size==2;
            ++slime_entity_generator){
        JavaRandom generator;
        JavaGaussianRandom entity_random;
        jrand_set_seed48(&generator,slime_entity_generator);
        jrand_gaussian_set(&entity_random,jrand_long(&generator));
        int power=jrand_int_bound(&entity_random.random,3);
        if(power<2)(void)jrand_float(&entity_random.random);
        expected_natural_slime_size=1<<power;
    }
    --slime_entity_generator;
    uint64_t slime_world_seed=7,slime_math_seed=11,slime_uuid_seed=13;
    int slime_eid=7001;
    CHECK(expected_natural_slime_size!=2&&
          gm_mobs_spawn_spawner_candidate(
              &r.mobs,r.world,GM_MOB_SLIME,12,4,12,4,6,
              12.5,5.0,12.5,8.5,5.0,8.5,
              &slime_world_seed,&slime_math_seed,
              &slime_entity_generator,&slime_uuid_seed,&slime_eid,
              NULL,1)==1,
          "default slime spawner executes private onInitialSpawn size path");
    int natural_slime_slot=gm_mobs_find_slot_by_eid(&r.mobs,7001);
    CHECK(natural_slime_slot>0&&
          r.mobs.size[natural_slime_slot]==expected_natural_slime_size&&
          test_mob_store(&r.mobs)->health[natural_slime_slot]==
              (float)(expected_natural_slime_size*expected_natural_slime_size),
          "slime private RNG selects exact 1/2/4 size and matching health");
    int skeleton_eid=7002;
    CHECK(gm_mobs_spawn_spawner_candidate(
              &r.mobs,r.world,EW_TYPE_SKELETON,14,4,14,4,6,
              14.5,5.0,14.5,8.5,5.0,8.5,
              &slime_world_seed,&slime_math_seed,
              &slime_entity_generator,&slime_uuid_seed,&skeleton_eid,
              NULL,1)==1,
          "default skeleton spawner executes subtype onInitialSpawn");
    int spawned_skeleton_slot=gm_mobs_find_slot_by_eid(&r.mobs,7002);
    CHECK(spawned_skeleton_slot>0&&
          r.mobs.entity_mainhand[spawned_skeleton_slot].item==261&&
          r.mobs.entity_mainhand[spawned_skeleton_slot].count==1,
          "default skeleton spawner equips its bow");

    /* EntityChicken consumes its egg clock in the subclass constructor,
     * before EntityLiving.onInitialSpawn consumes Gaussian/handedness. */
    JavaRandom chicken_generator;
    JavaGaussianRandom expected_chicken_random;
    jrand_set_seed48(&chicken_generator,slime_entity_generator);
    jrand_gaussian_set(
        &expected_chicken_random,jrand_long(&chicken_generator));
    int expected_egg=jrand_int_bound(
        &expected_chicken_random.random,6000)+6000;
    (void)jrand_gaussian_next(&expected_chicken_random);
    (void)jrand_float(&expected_chicken_random.random);
    int chicken_eid=7003;
    int chicken_spawn_result=gm_mobs_spawn_spawner_candidate(
              &r.mobs,r.world,EW_TYPE_CHICKEN,10,3,14,4,6,
              10.5,4.0,14.5,8.5,5.0,8.5,
              &slime_world_seed,&slime_math_seed,
              &slime_entity_generator,&slime_uuid_seed,&chicken_eid,
              NULL,1);
    if(chicken_spawn_result!=1)
        fprintf(stderr,"mob_live: chicken spawner result=%d below=%d sky=%d block=%d\n",
                chicken_spawn_result,gm_world_block(r.world,10,3,14),
                gm_world_sky_light(r.world,10,4,14),
                gm_world_block_light(r.world,10,4,14));
    CHECK(chicken_spawn_result==1,
          "default chicken spawner executes constructor egg draw");
    int spawned_chicken_slot=gm_mobs_find_slot_by_eid(&r.mobs,7003);
    CHECK(spawned_chicken_slot>0&&
          r.mobs.chicken_time_until_next_egg[spawned_chicken_slot]
              ==expected_egg&&
          r.mobs.entity_random[spawned_chicken_slot].random.seed
              ==expected_chicken_random.random.seed,
          "spawner chicken preserves egg clock and private RNG ordering");

    /* EntitySquid overwrites the SeedHelper constructor cursor with
     * setSeed(1 + entityId), then consumes rotationVelocity before common
     * initial spawn. This unusual source behavior is shared by natural squid. */
    JavaGaussianRandom expected_squid_random;
    jrand_gaussian_set(&expected_squid_random,7005);
    float expected_squid_velocity=1.0F/
        (jrand_float(&expected_squid_random.random)+1.0F)*0.2F;
    (void)jrand_gaussian_next(&expected_squid_random);
    (void)jrand_float(&expected_squid_random.random);
    int squid_eid=7004;
    CHECK(gm_mobs_spawn_spawner_candidate(
              &r.mobs,r.world,EW_TYPE_SQUID,10,4,10,4,6,
              10.5,5.0,10.5,8.5,5.0,8.5,
              &slime_world_seed,&slime_math_seed,
              &slime_entity_generator,&slime_uuid_seed,&squid_eid,
              NULL,1)==1,
          "default squid spawner executes entity-id constructor reseed");
    int spawned_squid_slot=gm_mobs_find_slot_by_eid(&r.mobs,7004);
    CHECK(spawned_squid_slot>0&&
          r.mobs.squid_rotation_velocity[spawned_squid_slot]
              ==expected_squid_velocity&&
          r.mobs.entity_random[spawned_squid_slot].random.seed
              ==expected_squid_random.random.seed&&
          r.mobs.entity_random[spawned_squid_slot].have_next_next_gaussian
              ==expected_squid_random.have_next_next_gaussian&&
          test_double_bits(r.mobs.entity_random[spawned_squid_slot]
              .next_next_gaussian)==test_double_bits(
                  expected_squid_random.next_next_gaussian),
          "spawner squid preserves reseeded velocity and Gaussian cursor");

    /* PolarBear.onInitialSpawn does not call super in 1.11.2: no common
     * Gaussian or handedness draw may leak into its private cursor. */
    JavaRandom bear_generator;
    JavaGaussianRandom expected_bear_random;
    jrand_set_seed48(&bear_generator,slime_entity_generator);
    jrand_gaussian_set(&expected_bear_random,jrand_long(&bear_generator));
    int bear_eid=7005;
    int bear_spawn_result=gm_mobs_spawn_spawner_candidate(
              &r.mobs,r.world,EW_TYPE_POLAR_BEAR,14,3,10,4,6,
              14.5,4.0,10.5,8.5,5.0,8.5,
              &slime_world_seed,&slime_math_seed,
              &slime_entity_generator,&slime_uuid_seed,&bear_eid,
              NULL,1);
    if(bear_spawn_result!=1)
        fprintf(stderr,"mob_live: bear spawner result=%d below=%d sky=%d block=%d\n",
                bear_spawn_result,gm_world_block(r.world,14,3,10),
                gm_world_sky_light(r.world,14,4,10),
                gm_world_block_light(r.world,14,4,10));
    CHECK(bear_spawn_result==1,
          "default polar-bear spawner skips common initial-spawn tail");
    int spawned_bear_slot=gm_mobs_find_slot_by_eid(&r.mobs,7005);
    CHECK(spawned_bear_slot>0&&
          r.mobs.entity_random[spawned_bear_slot].random.seed
              ==expected_bear_random.random.seed&&
          !r.mobs.entity_random[spawned_bear_slot].have_next_next_gaussian&&
          !r.mobs.entity_left_handed[spawned_bear_slot],
          "polar-bear private RNG remains at constructor continuation");
    gm_runtime_destroy(&r);

    /* Silverfish: melee + spawner entity id. */
    if(!init_flat(&r))return 1;
    r.vitals.foodLevel=0;r.vitals.saturation=0.0f;
    CHECK(gm_mobs_spawn(&r.mobs,GM_MOB_SILVERFISH,8.5,5.0,10.5)>=0,"spawn silverfish");
    gm_runtime_tick(&r,idle);
    CHECK(r.vitals.health==19.0f,"silverfish melee deals 1 damage");
    gm_runtime_destroy(&r);

    if(!init_flat(&r))return 1;
    gm_world_set_block(r.world,12,4,12,52);
    CHECK(gm_mobs_register_spawner(&r.mobs,12,4,12,GM_MOB_SILVERFISH)>=0,
          "register silverfish spawner");
    CHECK(r.mobs.spawners[0].entity_type==GM_MOB_SILVERFISH,
          "spawner stores silverfish entity id not blaze");
    r.mobs.spawners[0].delay=0;
    gm_runtime_set_pose(&r,12.5,5.0,12.5,0.0f,10.0f);
    for(int i=0;i<5;++i)gm_runtime_tick(&r,idle);
    int sf=0;{const EwStore *s=r.mobs.current?&r.mobs.b:&r.mobs.a;
        for(int i=1;i<EW_MAX_ENTITIES;++i)
            if(s->alive[i]&&s->type[i]==GM_MOB_SILVERFISH)sf=1;}
    CHECK(sf,"silverfish spawner produces silverfish from stored entity id");
    gm_runtime_destroy(&r);

    /* Blaze spawner still works (not any-block-52=blaze for overworld). */
    if(!init_flat(&r))return 1;
    gm_world_set_block(r.world,14,4,14,52);
    CHECK(gm_mobs_register_spawner(&r.mobs,14,4,14,GM_MOB_BLAZE)>=0,"register blaze spawner");
    r.mobs.spawners[0].delay=0;
    gm_runtime_set_pose(&r,14.5,5.0,14.5,0.0f,10.0f);
    for(int i=0;i<5;++i)gm_runtime_tick(&r,idle);
    int bl=0;{const EwStore *s=r.mobs.current?&r.mobs.b:&r.mobs.a;
        for(int i=1;i<EW_MAX_ENTITIES;++i)
            if(s->alive[i]&&s->type[i]==GM_MOB_BLAZE)bl=1;}
    CHECK(bl,"blaze spawner still selects blaze from stored entity id");
    gm_runtime_destroy(&r);

    /* Boat: place, mount, no-autothrust, controlBoat thrust, water status,
     * deltaRotation turn, land AABB, break drop. */
    if(!init_flat(&r))return 1;
    CHECK(gm_mobs_place_boat(&r.mobs,8.5,5.0,8.5,0.0f)>=0,"place boat");
    gm_runtime_set_pose(&r,8.5,5.0,8.5,0.0f,10.0f);
    CHECK(gm_mobs_boat_mount(&r.mobs,(struct PsvPlayer *)&r.player,r.ox,r.oz),
          "mount boat");
    CHECK(gm_mobs_boat_riding(&r.mobs),"boat riding flag");
    /* Idle: no forced forward thrust while mounted. */
    {
        double bx0=0,bz0=0;
        {const EwStore *s=r.mobs.current?&r.mobs.b:&r.mobs.a;
         for(int i=1;i<EW_MAX_ENTITIES;++i)
             if(s->alive[i]&&s->type[i]==GM_ENTITY_BOAT){bx0=s->x[i];bz0=s->z[i];}}
        for(int i=0;i<20;++i)gm_runtime_tick(&r,idle);
        double bx1=0,bz1=0;
        {const EwStore *s=r.mobs.current?&r.mobs.b:&r.mobs.a;
         for(int i=1;i<EW_MAX_ENTITIES;++i)
             if(s->alive[i]&&s->type[i]==GM_ENTITY_BOAT){bx1=s->x[i];bz1=s->z[i];}}
        double drift=sqrt((bx1-bx0)*(bx1-bx0)+(bz1-bz0)*(bz1-bz0));
        CHECK(drift<0.15,"mounted boat without forward input does not auto-thrust");
    }
    /* Forward input moves the boat along look yaw (controlBoat f+=0.04). */
    {
        double bx0=0,bz0=0;
        {const EwStore *s=r.mobs.current?&r.mobs.b:&r.mobs.a;
         for(int i=1;i<EW_MAX_ENTITIES;++i)
             if(s->alive[i]&&s->type[i]==GM_ENTITY_BOAT){bx0=s->x[i];bz0=s->z[i];}}
        GmAction fwd;memset(&fwd,0,sizeof fwd);fwd.forward=1.0f;fwd.hotbar_sel=-1;
        for(int i=0;i<30;++i)gm_runtime_tick(&r,fwd);
        double bx1=0,bz1=0;
        {const EwStore *s=r.mobs.current?&r.mobs.b:&r.mobs.a;
         for(int i=1;i<EW_MAX_ENTITIES;++i)
             if(s->alive[i]&&s->type[i]==GM_ENTITY_BOAT){bx1=s->x[i];bz1=s->z[i];}}
        double moved=sqrt((bx1-bx0)*(bx1-bx0)+(bz1-bz0)*(bz1-bz0));
        fprintf(stderr,"mob_live: boat land forward travel %.4f\n",moved);
        CHECK(moved>0.3,"mounted boat with forward=1 travels under controlBoat thrust");
    }
    /* Turn-only: left input accumulates deltaRotation (oracle: +/-1 per tick). */
    {
        float yaw0=0;
        {const EwStore *s=r.mobs.current?&r.mobs.b:&r.mobs.a;
         for(int i=1;i<EW_MAX_ENTITIES;++i)
             if(s->alive[i]&&s->type[i]==GM_ENTITY_BOAT)yaw0=s->yaw[i];}
        GmAction left;memset(&left,0,sizeof left);left.strafe=-1.0f;left.hotbar_sel=-1;
        for(int i=0;i<10;++i)gm_runtime_tick(&r,left);
        float yaw1=yaw0;
        {const EwStore *s=r.mobs.current?&r.mobs.b:&r.mobs.a;
         for(int i=1;i<EW_MAX_ENTITIES;++i)
             if(s->alive[i]&&s->type[i]==GM_ENTITY_BOAT)yaw1=s->yaw[i];}
        fprintf(stderr,"mob_live: boat deltaRotation yaw %.3f -> %.3f\n",yaw0,yaw1);
        CHECK(yaw1 < yaw0 - 5.0f,
              "left input applies controlBoat deltaRotation (yaw decreases)");
    }
    gm_mobs_boat_dismount(&r.mobs,(struct PsvPlayer *)&r.player,r.ox,r.oz);
    CHECK(!gm_mobs_boat_riding(&r.mobs),"dismount clears ride");
    /* Break: stand on the boat and hit until hull drops the item. */
    {
        GmEntityView vv[EW_MAX_ENTITIES];
        int nn=gm_mobs_fill_views(&r.mobs,vv,EW_MAX_ENTITIES),bi=-1;
        for(int k=0;k<nn;++k)if(vv[k].type==GM_ENTITY_BOAT)bi=k;
        CHECK(bi>=0,"boat still present after dismount");
        gm_runtime_set_pose(&r,vv[bi].x,vv[bi].y+1.0,vv[bi].z-1.0,0.0f,30.0f);
    }
    isr_set_stack(&r.player.inv,0,ic_mk(276,1,0));
    GmAction boat_hit;memset(&boat_hit,0,sizeof boat_hit);
    boat_hit.attack=1;boat_hit.do_break=1;boat_hit.hotbar_sel=0;
    for(int i=0;i<60;++i){
        GmEntityView vv[EW_MAX_ENTITIES];
        int nn=gm_mobs_fill_views(&r.mobs,vv,EW_MAX_ENTITIES),bi=-1;
        for(int k=0;k<nn;++k)if(vv[k].type==GM_ENTITY_BOAT)bi=k;
        if(bi<0)break;
        gm_runtime_set_pose(&r,vv[bi].x,vv[bi].y+1.0,vv[bi].z-1.0,0.0f,30.0f);
        gm_runtime_tick(&r,(i%11)==0?boat_hit:idle);
    }
    int boat_item=0;for(int i=0;i<GM_LIVE_MAX;++i)
        if(r.entities.ents[i].active&&r.entities.ents[i].item==333)boat_item=1;
    int boat_alive=0;{const EwStore *s=r.mobs.current?&r.mobs.b:&r.mobs.a;
        for(int i=1;i<EW_MAX_ENTITIES;++i)
            if(s->alive[i]&&s->type[i]==GM_ENTITY_BOAT)boat_alive=1;}
    CHECK(boat_item&&!boat_alive,"broken boat drops boat item");
    gm_runtime_destroy(&r);

    /* Every 1.11.2 ItemBoat registration preserves its Type through live
     * placement and returns the matching item when player-broken. */
    for(int variant=1;variant<=5;++variant){
        int expected_item=443+variant,found_item=0,seen_variant=0;
        if(!init_flat(&r))return 1;
        CHECK(gm_mobs_place_boat_type(
                  &r.mobs,8.5,5.0,8.5,0.0f,variant)>=0,
              "place non-oak boat variant");
        {GmEntityView vv[EW_MAX_ENTITIES];
         int nn=gm_mobs_fill_views(&r.mobs,vv,EW_MAX_ENTITIES);
         for(int k=0;k<nn;++k)if(vv[k].type==GM_ENTITY_BOAT){
             seen_variant=vv[k].item_meta==variant;
             gm_runtime_set_pose(
                 &r,vv[k].x,vv[k].y+1.0,vv[k].z-1.0,0.0f,30.0f);
         }}
        CHECK(seen_variant,"placed boat view retains registered variant");
        isr_set_stack(&r.player.inv,0,ic_mk(276,1,0));
        {GmAction hit;memset(&hit,0,sizeof hit);
         hit.attack=1;hit.do_break=1;hit.hotbar_sel=0;
         for(int tick=0;tick<60;++tick){
             GmEntityView vv[EW_MAX_ENTITIES];
             int nn=gm_mobs_fill_views(&r.mobs,vv,EW_MAX_ENTITIES),bi=-1;
             for(int k=0;k<nn;++k)if(vv[k].type==GM_ENTITY_BOAT)bi=k;
             if(bi<0)break;
             gm_runtime_set_pose(&r,vv[bi].x,vv[bi].y+1.0,
                                 vv[bi].z-1.0,0.0f,30.0f);
             gm_runtime_tick(&r,(tick%11)==0?hit:idle);
         }}
        for(int i=0;i<GM_LIVE_MAX;++i)
            if(r.entities.ents[i].active
                    &&r.entities.ents[i].item==expected_item)found_item=1;
        CHECK(found_item,"broken boat returns matching registered item");
        gm_runtime_destroy(&r);
    }

    /* Water boat: IN_WATER momentum 0.9, controlBoat forward, no autothrust. */
    if(!init_flat(&r))return 1;
    /* Large water basin so 20-tick thrust stays inside the pool. */
    for(int x=0;x<=24;++x)for(int z=0;z<=24;++z){
        gm_world_set_block(r.world,x,4,z,1); /* floor */
        gm_world_set_block(r.world,x,5,z,9); /* source water */
        gm_world_set_block(r.world,x,6,z,0);
    }
    CHECK(gm_mobs_place_boat(&r.mobs,12.5,5.2,12.5,0.0f)>=0,"place water boat");
    gm_runtime_set_pose(&r,12.5,5.6,12.5,0.0f,10.0f);
    CHECK(gm_mobs_boat_mount(&r.mobs,(struct PsvPlayer *)&r.player,r.ox,r.oz),
          "mount water boat");
    {
        double bx0=0,bz0=0;
        {const EwStore *s=r.mobs.current?&r.mobs.b:&r.mobs.a;
         for(int i=1;i<EW_MAX_ENTITIES;++i)
             if(s->alive[i]&&s->type[i]==GM_ENTITY_BOAT){bx0=s->x[i];bz0=s->z[i];}}
        for(int i=0;i<20;++i)gm_runtime_tick(&r,idle);
        double bx1=0,bz1=0,by1=0;
        {const EwStore *s=r.mobs.current?&r.mobs.b:&r.mobs.a;
         for(int i=1;i<EW_MAX_ENTITIES;++i)
             if(s->alive[i]&&s->type[i]==GM_ENTITY_BOAT){
                 bx1=s->x[i];by1=s->y[i];bz1=s->z[i];}}
        double drift=sqrt((bx1-bx0)*(bx1-bx0)+(bz1-bz0)*(bz1-bz0));
        CHECK(drift<0.2,"water boat without input does not auto-thrust");
        CHECK(by1>4.5&&by1<7.0,"idle water boat stays near surface (IN_WATER buoyancy)");
    }
    {
        double bx0=0,bz0=0,by0=0;
        {const EwStore *s=r.mobs.current?&r.mobs.b:&r.mobs.a;
         for(int i=1;i<EW_MAX_ENTITIES;++i)
             if(s->alive[i]&&s->type[i]==GM_ENTITY_BOAT){
                 bx0=s->x[i];by0=s->y[i];bz0=s->z[i];}}
        GmAction fwd;memset(&fwd,0,sizeof fwd);fwd.forward=1.0f;fwd.hotbar_sel=-1;
        for(int i=0;i<20;++i)gm_runtime_tick(&r,fwd);
        double bx1=0,bz1=0,by1=0;
        {const EwStore *s=r.mobs.current?&r.mobs.b:&r.mobs.a;
         for(int i=1;i<EW_MAX_ENTITIES;++i)
             if(s->alive[i]&&s->type[i]==GM_ENTITY_BOAT){
                 bx1=s->x[i];by1=s->y[i];bz1=s->z[i];}}
        double moved=sqrt((bx1-bx0)*(bx1-bx0)+(bz1-bz0)*(bz1-bz0));
        fprintf(stderr,"mob_live: water boat forward travel %.4f y %.3f->%.3f\n",
                moved,by0,by1);
        /* Oracle-valued: 20 ticks of 0.04 thrust with 0.9 water momentum is
         * well above 0.3 block horizontal travel; boat stays in the water body. */
        CHECK(moved>0.3,"water boat controlBoat forward travels under 0.9 momentum");
        CHECK(by1>4.5&&by1<7.0,"water boat remains near water surface (buoyancy)");
    }
    gm_runtime_destroy(&r);

    /* Entity ownership is dimension-scoped even for authoritative scripted
     * transfers that do not rebuild the live store. */
    if(!init_flat(&r))return 1;
    CHECK(gm_mobs_spawn(&r.mobs,EW_TYPE_ZOMBIE,8.5,5.0,10.5)>=0,
          "spawn Overworld dimension-owned zombie");
    r.mobs.active_dimension=-1;
    n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);
    CHECK(n==0,"Nether view excludes Overworld-owned entities");
    CHECK(gm_mobs_spawn(&r.mobs,GM_MOB_BLAZE,8.5,5.0,10.5)>=0,
          "spawn Nether dimension-owned blaze");
    n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);
    CHECK(n==1&&v[0].type==GM_MOB_BLAZE,"Nether view contains only Nether entities");
    r.mobs.active_dimension=0;
    n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);
    CHECK(n==1&&v[0].type==EW_TYPE_ZOMBIE,
          "returning Overworld view restores only Overworld entities");
    gm_runtime_destroy(&r);

    /* Wither skeleton: already covered for damage; natural type ok. */
    if(!init_flat(&r))return 1;
    CHECK(gm_mobs_spawn(&r.mobs,GM_MOB_WITHER_SKELETON,8.5,5.0,10.5)>=0,
          "spawn wither skeleton type");
    CHECK(gm_mobs_alive(&r.mobs)==1,"wither skeleton lives");
    gm_runtime_destroy(&r);

    /* Low-profile mobs remain hittable through the runtime attack ray. */
    if(!init_flat(&r))return 1;
    CHECK(gm_mobs_spawn(&r.mobs,EW_TYPE_SPIDER,8.5,5.0,10.5)>=0,
          "spawn low-profile spider");
    gm_runtime_set_pose(&r,8.5,6.0,10.5,0.0f,90.0f);
    isr_set_stack(&r.player.inv,0,ic_mk(276,1,0));
    {GmAction a;memset(&a,0,sizeof a);a.attack=1;a.do_break=1;a.hotbar_sel=0;
     gm_runtime_tick(&r,a);gm_runtime_tick(&r,idle);}
    {const EwStore *s=r.mobs.current?&r.mobs.b:&r.mobs.a;
     CHECK(s->health[1]<20.0f,"runtime attack ray hits low-profile spider");}
    gm_runtime_destroy(&r);

    /* Capacity > 7: spawn 12 zombies. */
    if(!init_flat(&r))return 1;
    int spawned=0;
    for(int i=0;i<12;++i){
        if(gm_mobs_spawn(&r.mobs,EW_TYPE_ZOMBIE,8.5+i*0.01,5.0,14.5+i*0.5)>=0)
            ++spawned;
    }
    CHECK(spawned==12,"capacity allows more than 7 living entities");
    CHECK(gm_mobs_living_count(&r.mobs)==12,"living_count reports 12");
    CHECK(GM_MOB_CAPACITY>7,"product capacity constant exceeds legacy 7");
    gm_runtime_destroy(&r);

    /* Pressure-plate collision enumeration distinguishes a true NoAI fixture
     * (no Entity.move call) from the taskless/gravity-free Java fixture whose
     * ordinary move(0,0,0) still executes doBlockCollisions. */
    if(!init_flat(&r))return 1;
    CHECK(gm_mobs_spawn_exact(
              &r.mobs,EW_TYPE_PIG,4016,12.5,5.0,8.5,
              0.0,0.0,0.0,0.0f,5.0f,1,0,0,0)>=0,
          "spawn true NoAI pressure-plate fixture");
    CHECK(gm_mobs_spawn_exact(
              &r.mobs,EW_TYPE_PIG,4017,16.5,5.0,8.5,
              0.0,0.0,0.0,0.0f,5.0f,0,0,0,0)>=0,
          "spawn collision-enabled pressure-plate fixture");
    {
        McAABB boxes[GM_MOB_CAPACITY];
        int all=gm_mobs_living_boxes(
            &r.mobs,0,boxes,GM_MOB_CAPACITY);
        CHECK(all==2,
              "living-box query includes both represented pigs");
        int controlled=gm_mobs_collision_boxes(
            &r.mobs,0,1,boxes,GM_MOB_CAPACITY);
        CHECK(controlled==1 &&
              fabs(boxes[0].minX-16.05)<1e-6 &&
              fabs(boxes[0].maxX-16.95)<1e-6 &&
              fabs(boxes[0].minY-5.0)<1e-6,
              "controlled collision-box query includes only ordinary mover");
        CHECK(gm_mobs_collision_boxes(
                  &r.mobs,0,0,boxes,GM_MOB_CAPACITY)==2,
              "normal mob pass exposes every living mover");
    }
    gm_runtime_destroy(&r);

    /* EntityBoat is not EntityLivingBase, but its ordinary onUpdate still
     * invokes doBlockCollisions and it inherits the default trigger=true
     * predicate. It therefore activates tripwire and EVERYTHING plates while
     * remaining excluded from a stone plate's MOBS query. */
    if(!init_flat(&r))return 1;
    gm_world_set_block_meta(r.world,9,5,8,1,0);
    gm_world_set_block_meta(r.world,10,5,8,131,3);
    gm_world_set_block_meta(r.world,11,5,8,132,0);
    gm_world_set_block_meta(r.world,12,5,8,132,0);
    gm_world_set_block_meta(r.world,13,5,8,132,0);
    gm_world_set_block_meta(r.world,14,5,8,131,1);
    gm_world_set_block_meta(r.world,15,5,8,1,0);
    CHECK(gm_mobs_place_boat(&r.mobs,12.5,5.0,8.5,0.0f)>=0,
          "spawn boat tripwire trigger");
    {
        McAABB boxes[GM_MOB_CAPACITY];
        CHECK(gm_mobs_collision_boxes(
                  &r.mobs,0,0,boxes,GM_MOB_CAPACITY)==0,
              "living collision query excludes boat");
        CHECK(gm_mobs_trigger_collision_boxes(
                  &r.mobs,0,0,boxes,GM_MOB_CAPACITY)==1,
              "all-entity trigger collision query includes boat");
    }
    gm_runtime_tick(&r,idle);
    CHECK(gm_world_meta(r.world,10,5,8)==15 &&
          gm_world_meta(r.world,12,5,8)==5 &&
          gm_world_meta(r.world,14,5,8)==13,
          "boat collision powers tripwire segment and both hooks");
    gm_runtime_destroy(&r);

    if(!init_flat(&r))return 1;
    gm_runtime_set_pose(&r,20.5,5.0,20.5,0.0f,10.0f);
    gm_world_set_block_meta(r.world,8,4,8,1,0);
    gm_world_set_block_meta(r.world,12,4,8,1,0);
    gm_world_set_block_meta(r.world,8,5,8,70,0);
    gm_world_set_block_meta(r.world,12,5,8,72,0);
    CHECK(gm_mobs_place_boat(&r.mobs,8.5,5.0,8.5,0.0f)>=0 &&
          gm_mobs_place_boat(&r.mobs,12.5,5.0,8.5,0.0f)>=0,
          "spawn stone/wood plate boat controls");
    gm_runtime_tick(&r,idle);
    CHECK(gm_world_meta(r.world,8,5,8)==0,
          "boat does not activate stone MOBS pressure plate");
    CHECK(gm_world_meta(r.world,12,5,8)==1,
          "boat activates wooden EVERYTHING pressure plate");
    gm_runtime_destroy(&r);

    /* EntityXPOrb inherits trigger=true and reaches doBlockCollisions inside
     * Entity.move. It therefore activates tripwire and EVERYTHING plates but
     * remains outside a stone plate's EntityLivingBase-only MOBS query. */
    if(!init_flat(&r))return 1;
    gm_runtime_set_pose(&r,24.5,5.0,20.5,0.0f,10.0f);
    gm_world_set_block_meta(r.world,9,5,8,1,0);
    gm_world_set_block_meta(r.world,10,5,8,131,3);
    gm_world_set_block_meta(r.world,11,5,8,132,0);
    gm_world_set_block_meta(r.world,12,5,8,132,0);
    gm_world_set_block_meta(r.world,13,5,8,132,0);
    gm_world_set_block_meta(r.world,14,5,8,131,1);
    gm_world_set_block_meta(r.world,15,5,8,1,0);
    CHECK(gm_mobs_spawn_xp_exact(
              &r.mobs,12.5,5.2,8.5,0.0,0.0,0.0,1,
              4025,0,32767,0,-100),
          "spawn XP-orb tripwire trigger");
    {
        McAABB trigger=mc_aabb_make(12.0,5.0,8.0,13.0,5.5,9.0);
        CHECK(gm_mobs_xp_count_intersects_aabb(
                  &r.mobs,0,&trigger)==1,
              "XP-orb occupancy query uses exact half-block box");
    }
    gm_runtime_tick(&r,idle);
    CHECK(r.mobs.xp_collision_count==1 &&
          gm_world_meta(r.world,10,5,8)==15 &&
          gm_world_meta(r.world,12,5,8)==5 &&
          gm_world_meta(r.world,14,5,8)==13,
          "XP-orb move powers tripwire segment and both hooks");
    gm_runtime_destroy(&r);

    if(!init_flat(&r))return 1;
    gm_runtime_set_pose(&r,24.5,5.0,20.5,0.0f,10.0f);
    gm_world_set_block_meta(r.world,8,4,8,1,0);
    gm_world_set_block_meta(r.world,12,4,8,1,0);
    gm_world_set_block_meta(r.world,15,4,8,1,0);
    gm_world_set_block_meta(r.world,8,5,8,70,0);
    gm_world_set_block_meta(r.world,12,5,8,72,0);
    gm_world_set_block_meta(r.world,15,5,8,147,0);
    CHECK(gm_mobs_spawn_xp_exact(
              &r.mobs,8.5,5.2,8.5,0.0,0.0,0.0,1,
              4026,0,32767,0,-100) &&
          gm_mobs_spawn_xp_exact(
              &r.mobs,12.5,5.2,8.5,0.0,0.0,0.0,1,
              4027,0,32767,0,-100) &&
          gm_mobs_spawn_xp_exact(
              &r.mobs,15.5,5.2,8.5,0.0,0.0,0.0,1,
              4028,0,32767,0,-100),
          "spawn stone/wood/weighted XP-orb controls");
    gm_runtime_tick(&r,idle);
    CHECK(gm_world_meta(r.world,8,5,8)==0,
          "XP orb does not activate stone MOBS pressure plate");
    CHECK(gm_world_meta(r.world,12,5,8)==1,
          "XP orb activates wooden EVERYTHING pressure plate");
    CHECK(gm_world_meta(r.world,15,5,8)==1,
          "one XP orb gives gold weighted plate exact strength one");
    gm_runtime_destroy(&r);

    /* ---- Mobs-on autonomy: live spawn/AI/combat cadences (Java-derived) ----
     * Not ent_view ghosts and not injected vitals. Pose placement only. */

    /* Zombie melee: EntityZombie ATTACK_DAMAGE=3.0; EntityLiving.attackTime
     * cooldown 20 after a hit (MAZ_ATTACK_COOLDOWN / EW path in magma). */
    if(!init_flat(&r))return 1;
    r.vitals.foodLevel=0;r.vitals.saturation=0.0f;r.player.food=0;
    CHECK(gm_mobs_spawn(&r.mobs,EW_TYPE_ZOMBIE,8.5,5.0,10.5)>=0,
          "autonomy: spawn melee zombie");
    float zhp0=r.vitals.health;
    gm_runtime_tick(&r,idle);
    CHECK(r.vitals.health==zhp0-3.0f,"autonomy: zombie first melee is exactly 3 hp");
    float zhp1=r.vitals.health;
    for(int i=0;i<19;++i)gm_runtime_tick(&r,idle);
    CHECK(r.vitals.health==zhp1,
          "autonomy: zombie melee silent through attack_time 19 of 20");
    gm_runtime_tick(&r,idle);
    CHECK(r.vitals.health==zhp1-3.0f,
          "autonomy: zombie second melee lands on the 20-tick cadence");
    gm_runtime_destroy(&r);

    /* Enderman acquisition: no look-trigger in this sim; revenge sets
     * hurt_aggro, after which follow_range (16) chase engages. */
    if(!init_flat(&r))return 1;
    CHECK(gm_mobs_spawn(&r.mobs,EW_TYPE_ENDERMAN,8.5,5.0,18.5)>=0,
          "autonomy: spawn enderman");
    double ez0=18.5;
    for(int i=0;i<30;++i){
        gm_runtime_set_pose(&r,8.5,5.0,8.5,0.0f,10.0f);
        gm_runtime_tick(&r,idle);
    }
    n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);
    int ei=-1;for(int k=0;k<n;++k)if(v[k].type==EW_TYPE_ENDERMAN)ei=k;
    CHECK(ei>=0&&fabs(v[ei].z-ez0)<1.5,
          "autonomy: unhurt enderman does not acquire the player");
    isr_set_stack(&r.player.inv,0,ic_mk(268,1,0));
    GmAction poke;memset(&poke,0,sizeof poke);
    poke.attack=1;poke.do_break=1;poke.hotbar_sel=0;
    gm_runtime_set_pose(&r,8.5,5.0,16.5,0.0f,10.0f);
    gm_runtime_tick(&r,poke);
    /* Deliver the queued server use-entity packet before moving the fixture
     * player back out of melee range. */
    gm_runtime_tick(&r,idle);
    double ez1=0.0;int acquired=0;
    for(int i=0;i<80;++i){
        gm_runtime_set_pose(&r,8.5,5.0,8.5,0.0f,10.0f);
        gm_runtime_tick(&r,idle);
        n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);
        ei=-1;for(int k=0;k<n;++k)if(v[k].type==EW_TYPE_ENDERMAN)ei=k;
        if(ei<0)break;
        ez1=v[ei].z;
        if(ez1<ez0-1.0){acquired=1;break;}
    }
    fprintf(stderr,"mob_live: enderman acquisition z %.3f -> %.3f\n",ez0,ez1);
    CHECK(acquired,"autonomy: hurt enderman acquires and chases the player");
    gm_runtime_destroy(&r);

    /* EntityBlaze.AIFireballAttack: charge for 60 ticks, fire three shots six
     * ticks apart, then clear the render/on-fire flag for a 100-tick rest. */
    if(!init_flat(&r))return 1;
    r.dimension=-1;r.mobs.active_dimension=-1;
    CHECK(gm_mobs_spawn(&r.mobs,GM_MOB_BLAZE,8.5,5.0,14.5)>=0,"autonomy: spawn blaze");
    r.vitals.health=100.0f;r.player.health=100.0f;
    int fireballs=0,shot_ticks[3]={-1,-1,-1};
    int flags_exact=1;
    int ownership_exact=1;
    int first_fireball_eid=0;
    double first_fireball_speed=-1.0,second_fireball_speed=-1.0;
    double first_fireball_acceleration=-1.0;
    for(int i=0;i<179;++i){
        gm_runtime_set_pose(&r,8.5,5.0,8.5,0.0f,10.0f);
        gm_runtime_tick(&r,idle);
        n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);
        int charged=n>0&&(v[0].flags&1)!=0;
        int expected_charged=i<78||i==178;
        if(charged!=expected_charged)flags_exact=0;
        for(int p=0;p<GM_RUNTIME_PROJECTILES;++p){
            if(r.projectiles[p].active&&r.projectiles[p].type==3){
                if(!r.projectiles[p].shooting_living)ownership_exact=0;
                if(r.projectiles[p].age==1){
                    if(fireballs<3)shot_ticks[fireballs]=i;
                    if(fireballs==0){
                        first_fireball_eid=r.projectiles[p].eid;
                        first_fireball_speed=sqrt(
                            r.projectiles[p].vx*r.projectiles[p].vx+
                            r.projectiles[p].vy*r.projectiles[p].vy+
                            r.projectiles[p].vz*r.projectiles[p].vz);
                        first_fireball_acceleration=sqrt(
                            r.projectiles[p].ax*r.projectiles[p].ax+
                            r.projectiles[p].ay*r.projectiles[p].ay+
                            r.projectiles[p].az*r.projectiles[p].az);
                    }
                    ++fireballs;
                }
                if(r.projectiles[p].eid==first_fireball_eid&&
                   r.projectiles[p].age==2)
                    second_fireball_speed=sqrt(
                        r.projectiles[p].vx*r.projectiles[p].vx+
                        r.projectiles[p].vy*r.projectiles[p].vy+
                        r.projectiles[p].vz*r.projectiles[p].vz);
            }
        }
    }
    fprintf(stderr,"mob_live: blaze shots=%d ticks=%d,%d,%d flags_exact=%d\n",
            fireballs,shot_ticks[0],shot_ticks[1],shot_ticks[2],flags_exact);
    CHECK(flags_exact,"autonomy: blaze charged/on-fire duty cycle is 78 on, 100 off");
    CHECK(ownership_exact,"autonomy: blaze fireballs retain a living shooter");
    CHECK(fireballs==3&&shot_ticks[0]==60&&shot_ticks[1]==66&&shot_ticks[2]==72,
          "autonomy: blaze volley fires at +60/+66/+72");
    fprintf(stderr,"mob_live: fireball |a|=%.17g v1=%.17g v2=%.17g\n",
            first_fireball_acceleration,first_fireball_speed,
            second_fireball_speed);
    CHECK(first_fireball_eid>0&&
          fabs(first_fireball_speed-
               first_fireball_acceleration*0.949999988079071)<1e-8&&
          fabs(second_fireball_speed-
               first_fireball_acceleration*1.8524999654293062)<1e-8,
          "autonomy: small fireball starts at zero motion then accelerates with factor 0.95");
    gm_runtime_set_pose(&r,8.5,5.0,80.5,0.0f,10.0f);
    gm_runtime_tick(&r,idle);
    n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);
    CHECK(n>0&&(v[0].flags&1)==0,
          "autonomy: losing the blaze target clears charged/on-fire immediately");
    gm_runtime_set_pose(&r,8.5,5.0,8.5,0.0f,10.0f);
    gm_runtime_tick(&r,idle);
    n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);
    CHECK(n>0&&(v[0].flags&1)==0,
          "autonomy: reacquiring resets attackStep but preserves the attackTime wait");
    gm_runtime_destroy(&r);

    /* The attack task consumes two Gaussians from EntityBlaze.rand, then the
     * EntitySmallFireball constructor consumes three from its own Random.
     * Both streams begin after Entity's two UUID nextLong calls. The shared
     * feature header is independently bit-checked against Java by the
     * entity_random oracle; this locks its integration into a live volley. */
    if(!init_flat(&r))return 1;
    r.dimension=-1;r.mobs.active_dimension=-1;
    int seeded_blaze_slot=gm_mobs_spawn(
        &r.mobs,GM_MOB_BLAZE,8.5,5.0,14.5);
    CHECK(seeded_blaze_slot>0,"seeded spread: spawn blaze");
    const EwStore *seeded_store=r.mobs.current?&r.mobs.b:&r.mobs.a;
    int seeded_blaze_eid=seeded_store->id[seeded_blaze_slot];
    JavaGaussianRandom expected_blaze_random,expected_fireball_random;
    ebf_entity_random_init(&expected_blaze_random,12345);
    ebf_entity_random_init(&expected_fireball_random,12345);
    CHECK(gm_mobs_set_entity_random_state(
              &r.mobs,seeded_blaze_eid,
              expected_blaze_random.random.seed,0,0.0),
          "seeded spread: restore blaze post-UUID Random cursor");
    CHECK(gm_mobs_set_blaze_height_state(
              &r.mobs,seeded_blaze_eid,100,0.5F),
          "seeded spread: freeze height redraw beyond first volley");
    CHECK(gm_runtime_set_next_fireball_random_state(
              &r,expected_fireball_random.random.seed,0,0.0),
          "seeded spread: restore next fireball post-UUID Random cursor");
    GmRuntimeProjectile *seeded_shot=NULL;
    double pre_shot_x=0.0,pre_shot_y=0.0,pre_shot_z=0.0;
    for(int i=0;i<=60;++i){
        if(i==60){
            const EwStore *pre=r.mobs.current?&r.mobs.b:&r.mobs.a;
            pre_shot_x=pre->x[seeded_blaze_slot];
            pre_shot_y=pre->y[seeded_blaze_slot];
            pre_shot_z=pre->z[seeded_blaze_slot];
        }
        gm_runtime_set_pose(&r,8.5,5.0,8.5,0.0f,10.0f);
        gm_runtime_tick(&r,idle);
    }
    for(int i=0;i<GM_RUNTIME_PROJECTILES;++i)
        if(r.projectiles[i].active&&r.projectiles[i].type==3){
            seeded_shot=&r.projectiles[i];break;
        }
    EbfVector expected_aim=ebf_blaze_fireball_aim(
        &expected_blaze_random,
        8.5-pre_shot_x,5.0-pre_shot_y,8.5-pre_shot_z);
    EbfVector expected_acceleration=ebf_small_fireball_acceleration(
        &expected_fireball_random,
        expected_aim.x,expected_aim.y,expected_aim.z);
    CHECK(seeded_shot&&seeded_shot->age==1,
          "seeded spread: first shot exists on the exact +60 tick");
    if(seeded_shot)
        fprintf(stderr,
                "mob_live: seeded accel got=(%.17g,%.17g,%.17g) expected=(%.17g,%.17g,%.17g) y=%.17g expected_y=%.17g\n",
                seeded_shot->ax,seeded_shot->ay,seeded_shot->az,
                expected_acceleration.x,expected_acceleration.y,
                expected_acceleration.z,seeded_shot->y,
                pre_shot_y+(double)(1.8F/2.0F)+0.5);
    CHECK(seeded_shot&&seeded_shot->ax==expected_acceleration.x&&
          seeded_shot->ay==expected_acceleration.y&&
          seeded_shot->az==expected_acceleration.z,
          "seeded spread: live acceleration matches Java-proven two-stream Gaussian chain");
    CHECK(seeded_shot&&
          (seeded_shot->ax!=0.0||seeded_shot->ay!=0.0),
          "seeded spread negative: acceleration is not the old zero-spread centerline");
    CHECK(seeded_shot&&
          seeded_shot->x==pre_shot_x&&seeded_shot->z==pre_shot_z&&
          seeded_shot->y==pre_shot_y+(double)(1.8F/2.0F)+0.5,
          "seeded spread: projectile starts at the pre-move blaze half-height");
    gm_runtime_destroy(&r);

    /* EntityBlaze.onLivingUpdate damps a falling airborne blaze before AI;
     * updateAITasks then applies the height impulse before generic travel,
     * gravity, and drag. Keep the target far above so the branch is true for
     * every checked tick and compare the complete live order exactly. */
    if(!init_flat(&r))return 1;
    r.dimension=-1;r.mobs.active_dimension=-1;
    int float_slot=gm_mobs_spawn(&r.mobs,GM_MOB_BLAZE,8.5,12.0,14.5);
    CHECK(float_slot>0,"blaze float: spawn airborne blaze");
    const EwStore *float_store=r.mobs.current?&r.mobs.b:&r.mobs.a;
    int float_eid=float_store->id[float_slot];
    CHECK(gm_mobs_set_blaze_height_state(&r.mobs,float_eid,100,0.5F),
          "blaze float: set known height offset state");
    r.mobs.a.vy[float_slot]=r.mobs.b.vy[float_slot]=-0.125;
    r.mobs.a.on_ground[float_slot]=r.mobs.b.on_ground[float_slot]=0;
    double expected_y=12.0,expected_vy=-0.125;
    int float_exact=1;
    for(int i=0;i<5;++i){
        gm_runtime_set_pose(&r,8.5,30.0,14.5,0.0f,10.0f);
        expected_vy=ebf_blaze_fall_damping(0,expected_vy);
        if(fabs(expected_vy)<0.003)expected_vy=0.0;
        expected_vy=ebf_blaze_height_impulse(
            expected_vy,31.6200000047683716,
            expected_y+(double)(1.8F*0.85F),0.5F);
        expected_y+=expected_vy;
        expected_vy=(expected_vy-0.08)*0.9800000190734863;
        gm_runtime_tick(&r,idle);
        const EwStore *actual=r.mobs.current?&r.mobs.b:&r.mobs.a;
        if(actual->y[float_slot]!=expected_y||
           actual->vy[float_slot]!=expected_vy)
            float_exact=0;
    }
    CHECK(float_exact,
          "blaze float: damping, height impulse, travel, gravity, and drag order is exact");
    gm_runtime_destroy(&r);

    /* The same task switches to EntityMob.attackEntityAsMob inside distance
     * squared four; the blaze ATTACK_DAMAGE attribute is six. */
    if(!init_flat(&r))return 1;
    r.vitals.foodLevel=0;r.vitals.saturation=0.0f;
    CHECK(gm_mobs_spawn(&r.mobs,GM_MOB_BLAZE,8.5,5.0,10.0)>=0,
          "autonomy: spawn melee-range blaze");
    gm_runtime_tick(&r,idle);
    CHECK(r.vitals.health==14.0f,
          "autonomy: melee-range blaze deals exact six attack damage");
    gm_runtime_destroy(&r);

    /* EntitySmallFireball entity impact deals five, then setFire(5) only when
     * attackEntityFrom accepted the hit. */
    if(!init_flat(&r))return 1;
    r.vitals.foodLevel=0;r.vitals.saturation=0.0f;
    r.projectiles[0]=(GmRuntimeProjectile){
        .active=1,.type=3,.eid=40,
        .x=7.5,.y=5.9,.z=8.5,.vx=1.0
    };
    gm_runtime_tick(&r,idle);
    CHECK(!r.projectiles[0].active&&r.vitals.health==15.0f&&
          r.player_fire_ticks==100,
          "small-fireball entity impact deals five and ignites for five seconds");
    gm_runtime_destroy(&r);

    /* ProjectileHelper expands the target's real 0.6 x 1.8 AABB by 0.3;
     * it does not use a radius around the entity center. This segment clips
     * the upper corner of that expanded box while staying outside the old
     * 0.75-radius proxy. */
    if(!init_flat(&r))return 1;
    r.vitals.foodLevel=0;r.vitals.saturation=0.0f;
    r.projectiles[0]=(GmRuntimeProjectile){
        .active=1,.type=3,.eid=42,
        .x=7.0,.y=7.09,.z=9.09,.vx=3.0
    };
    gm_runtime_tick(&r,idle);
    CHECK(!r.projectiles[0].active&&r.vitals.health==15.0f&&
          r.player_fire_ticks==100,
          "small-fireball segment hits expanded player AABB at its upper corner");
    gm_runtime_destroy(&r);

    /* World.rayTraceBlocks asks BlockSlab.collisionRayTrace for the shaped
     * lower-half box. A horizontal segment through the upper half must pass. */
    if(!init_flat(&r))return 1;
    gm_world_set_block_meta(r.world,2,5,9,44,0);
    r.projectiles[0]=(GmRuntimeProjectile){
        .active=1,.type=3,.eid=43,
        .x=2.5,.y=5.75,.z=8.25,.vz=1.5
    };
    gm_runtime_tick(&r,idle);
    CHECK(r.projectiles[0].active&&r.projectiles[0].z>9.0&&
          gm_world_block(r.world,2,5,9)==44,
          "small-fireball ray passes above a lower slab without impact");
    gm_runtime_destroy(&r);

    /* BlockStairs overrides collisionRayTrace with its actual-state list of
     * slab/quarter/eighth boxes. Bottom east-facing meta 0 leaves the upper
     * west half empty, so this segment must pass through that half. */
    if(!init_flat(&r))return 1;
    gm_world_set_block_meta(r.world,2,5,9,53,0);
    r.projectiles[0]=(GmRuntimeProjectile){
        .active=1,.type=3,.eid=50,
        .x=2.25,.y=5.75,.z=8.25,.vz=1.5
    };
    gm_runtime_tick(&r,idle);
    CHECK(r.projectiles[0].active&&r.projectiles[0].z>9.0&&
          gm_world_block(r.world,2,5,9)==53,
          "small-fireball ray passes through empty upper half of straight stair");
    gm_runtime_destroy(&r);

    /* BlockPistonMoving.collisionRayTrace returns null unconditionally even
     * while its tile entity exposes physical collision boxes. */
    if(!init_flat(&r))return 1;
    gm_world_set_block_meta(r.world,2,5,9,36,0);
    r.projectiles[0]=(GmRuntimeProjectile){
        .active=1,.type=3,.eid=51,
        .x=2.5,.y=5.5,.z=8.25,.vz=1.5
    };
    gm_runtime_tick(&r,idle);
    CHECK(r.projectiles[0].active&&r.projectiles[0].z>9.0&&
          gm_world_block(r.world,2,5,9)==36,
          "small-fireball ray ignores moving-piston placeholder block");
    gm_runtime_destroy(&r);

    /* EntitySmallFireball.onImpact never substitutes an explosion for its
     * mobGriefing-gated adjacent-fire placement. */
    if(!init_flat(&r))return 1;
    gm_world_set_block(r.world,2,4,8,1);
    gm_world_set_block(r.world,2,5,9,1);
    r.projectiles[0]=(GmRuntimeProjectile){
        .active=1,.type=3,.eid=41,.x=2.5,.y=5.5,.z=8.75,.vz=0.5
    };
    gm_runtime_tick(&r,idle);
    CHECK(!r.projectiles[0].active&&gm_world_block(r.world,2,5,9)==1&&
          gm_world_block(r.world,2,5,8)==51,
          "small-fireball block impact places adjacent fire without an explosion");
    gm_runtime_destroy(&r);

    /* EntitySmallFireball gates block ignition on mobGriefing only when its
     * shootingEntity is living. A blaze-owned shot still dies on impact. */
    if(!init_flat(&r))return 1;
    r.mob_griefing=0;
    gm_world_set_block(r.world,2,4,8,1);
    gm_world_set_block(r.world,2,5,9,1);
    r.projectiles[0]=(GmRuntimeProjectile){
        .active=1,.type=3,.eid=44,.shooting_living=1,
        .x=2.5,.y=5.5,.z=8.75,.vz=0.5
    };
    gm_runtime_tick(&r,idle);
    CHECK(!r.projectiles[0].active&&gm_world_block(r.world,2,5,9)==1&&
          gm_world_block(r.world,2,5,8)==0,
          "mobGriefing off suppresses blaze-owned small-fireball ignition");
    gm_runtime_destroy(&r);

    /* A shooterless small fireball bypasses the living-shooter gamerule gate,
     * matching EntitySmallFireball.onImpact rather than a global switch. */
    if(!init_flat(&r))return 1;
    r.mob_griefing=0;
    gm_world_set_block(r.world,2,4,8,1);
    gm_world_set_block(r.world,2,5,9,1);
    r.projectiles[0]=(GmRuntimeProjectile){
        .active=1,.type=3,.eid=45,.shooting_living=0,
        .x=2.5,.y=5.5,.z=8.75,.vz=0.5
    };
    gm_runtime_tick(&r,idle);
    CHECK(!r.projectiles[0].active&&gm_world_block(r.world,2,5,9)==1&&
          gm_world_block(r.world,2,5,8)==51,
          "shooterless small fireball ignores mobGriefing and ignites");
    gm_runtime_destroy(&r);

    /* ProjectileHelper selects the nearest living entity along the segment,
     * using its real box expanded by 0.30000001192092896. */
    if(!init_flat(&r))return 1;
    r.mobs_enabled=0;
    int fireball_target=gm_mobs_spawn(&r.mobs,EW_TYPE_ZOMBIE,4.5,5.0,8.5);
    CHECK(fireball_target>=0,"small-fireball entity ray spawns target zombie");
    r.projectiles[0]=(GmRuntimeProjectile){
        .active=1,.type=3,.eid=46,.x=2.5,.y=5.9,.z=8.5,.vx=3.0
    };
    gm_runtime_tick(&r,idle);
    n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);
    CHECK(!r.projectiles[0].active&&n==1&&v[0].health==15.0f&&
          r.mobs.fire_ticks[fireball_target]==100,
          "small-fireball segment damages and ignites nearest living mob");
    gm_runtime_destroy(&r);

    /* ProjectileHelper excludes the shooting entity until ticksInAir reaches
     * 25, then admits it to the same nearest-intercept search. */
    if(!init_flat(&r))return 1;
    r.mobs_enabled=0;
    fireball_target=gm_mobs_spawn(&r.mobs,EW_TYPE_ZOMBIE,4.5,5.0,8.5);
    r.projectiles[0]=(GmRuntimeProjectile){
        .active=1,.type=3,.eid=47,.shooting_living=1,.shooter_eid=1,
        .x=3.5,.y=5.9,.z=8.5,.vx=2.0
    };
    gm_runtime_tick(&r,idle);
    n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);
    CHECK(r.projectiles[0].active&&n==1&&v[0].health==20.0f,
          "small-fireball ignores its shooter for the first 24 air ticks");
    r.projectiles[0]=(GmRuntimeProjectile){
        .active=1,.type=3,.age=24,.eid=48,.shooting_living=1,.shooter_eid=1,
        .x=3.5,.y=5.9,.z=8.5,.vx=2.0
    };
    gm_runtime_tick(&r,idle);
    n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);
    CHECK(!r.projectiles[0].active&&n==1&&v[0].health==15.0f&&
          r.mobs.fire_ticks[fireball_target]==100,
          "small-fireball admits its shooter on air tick 25");
    gm_runtime_destroy(&r);

    /* EntitySmallFireball still consumes itself on a collidable target that
     * isImmuneToFire, but skips both attackEntityFrom and setFire. */
    if(!init_flat(&r))return 1;
    r.mobs_enabled=0;
    fireball_target=gm_mobs_spawn(&r.mobs,GM_MOB_BLAZE,4.5,5.0,8.5);
    r.projectiles[0]=(GmRuntimeProjectile){
        .active=1,.type=3,.eid=49,.x=2.5,.y=5.9,.z=8.5,.vx=3.0
    };
    gm_runtime_tick(&r,idle);
    n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);
    CHECK(!r.projectiles[0].active&&n==1&&v[0].health==20.0f&&
          r.mobs.fire_ticks[fireball_target]==0,
          "small-fireball is consumed without damaging a fire-immune blaze");
    gm_runtime_destroy(&r);

    /* Skeleton: type-specific keep-away + ranged (not shared melee stand). */
    if(!init_flat(&r))return 1;
    r.vitals.foodLevel=0;r.vitals.saturation=0.0f;
    CHECK(gm_mobs_spawn(&r.mobs,EW_TYPE_SKELETON,8.5,5.0,10.5)>=0,
          "type AI: spawn skeleton");
    int sk_arrows=0;double sk_z_max=10.5;
    for(int i=0;i<100;++i){
        gm_runtime_set_pose(&r,8.5,5.0,8.5,0.0f,10.0f);
        gm_runtime_tick(&r,idle);
        for(int p=0;p<GM_RUNTIME_PROJECTILES;++p)
            if(r.projectiles[p].active&&r.projectiles[p].type==2&&
               (r.projectiles[p].age==0||r.projectiles[p].age==1))
                ++sk_arrows;
        n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);
        for(int k=0;k<n;++k)
            if(v[k].type==EW_TYPE_SKELETON&&v[k].z>sk_z_max)sk_z_max=v[k].z;
    }
    CHECK(sk_arrows>=1,"type AI: skeleton fires arrows on ranged cadence");
    /* Close skeleton backs off along -player direction (z increases here). */
    CHECK(sk_z_max>11.5,
          "type AI: close skeleton keep-away navigates outward (not zombie melee stand)");
    gm_runtime_destroy(&r);

    /* WorldEntitySpawner traversal plus weighted biome spawn entries:
     * enderman is rare vs zombie/skeleton/creeper/spider in the route roster. */
    if(!init_flat(&r))return 1;
    r.gamerules.doMobSpawning = 1;
    gm_mobs_set_natural_spawning(&r.mobs,1);
    gm_runtime_set_time(&r,14000);
    int counts[32];memset(counts,0,sizeof counts);
    int initialized_natural=0;
    int natural_skeletons=0,natural_skeleton_bows=0;
    int natural_slimes=0,natural_large_slimes=0;
    for(int trial=0;trial<40;++trial){
        /* Clear living hostiles between trials. */
        {EwStore *s=r.mobs.current?&r.mobs.b:&r.mobs.a;
         for(int i=1;i<EW_MAX_ENTITIES;++i)
             if(s->alive[i]){s->alive[i]=0;s->type[i]=EW_TYPE_NONE;}}
        r.mobs.tick=20*(trial+1); /* natural_spawn gates on tick%20==0 */
        for(int i=0;i<25;++i){
            gm_runtime_set_pose(&r,8.5,5.0,8.5,0.0f,10.0f);
            gm_runtime_tick(&r,idle);
        }
        n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);
        for(int k=0;k<n;++k){
            int t=v[k].type;
            if(t>=0&&t<32)counts[t]++;
            int slot=gm_mobs_find_slot_by_eid(&r.mobs,v[k].ent_id);
            if(slot>0&&r.mobs.entity_uuid_present[slot]&&
               r.mobs.entity_random[slot].random.seed!=0)
                ++initialized_natural;
            if(slot>0&&t==EW_TYPE_SKELETON){
                ++natural_skeletons;
                if(r.mobs.entity_mainhand[slot].item==261&&
                   r.mobs.entity_mainhand[slot].count==1)
                    ++natural_skeleton_bows;
            }
            if(slot>0&&t==EW_TYPE_SLIME){
                ++natural_slimes;
                if(r.mobs.size[slot]>1)++natural_large_slimes;
            }
        }
    }
    int common=counts[EW_TYPE_ZOMBIE]+counts[EW_TYPE_SKELETON]+
               counts[EW_TYPE_CREEPER]+counts[EW_TYPE_SPIDER];
    fprintf(stderr,"mob_live: natural spawn common=%d enderman=%d "
                   "slime=%d large=%d\n",
            common,counts[EW_TYPE_ENDERMAN],natural_slimes,
            natural_large_slimes);
    CHECK(common>0,"type AI: night natural spawn produces common hostiles");
    CHECK(counts[EW_TYPE_ENDERMAN]<=common,
          "type AI: enderman weight is not above the common hostiles combined");
    CHECK(initialized_natural>0,
          "natural births retain constructor UUID and private RNG state");
    CHECK(natural_skeletons>0&&natural_skeleton_bows==natural_skeletons,
          "natural skeleton onInitialSpawn equips its bow");
    CHECK(natural_slimes==0||natural_large_slimes>0,
          "natural slime size comes from private onInitialSpawn RNG");
    gm_runtime_destroy(&r);

    /* Real spawner: natural_spawn Nether branch reads block id 52 and emits
     * blazes on a 200-tick cadence when under the alive_count cap. */
    if(!init_flat(&r))return 1;
    r.gamerules.doMobSpawning=1;
    r.dimension=-1;r.mobs.active_dimension=-1;
    for(int x=6;x<=10;++x)for(int z=6;z<=10;++z){
        gm_world_set_block(r.world,x,4,z,1);
        gm_world_set_block(r.world,x,5,z,0);
        gm_world_set_block(r.world,x,6,z,0);
    }
    gm_world_set_block(r.world,8,5,8,52);
    gm_world_set_block(r.world,9,5,8,112);
    gm_runtime_set_pose(&r,8.5,5.0,8.5,0.0f,10.0f);
    int spawns=0;
    for(int i=0;i<450;++i){
        n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);
        int before=0;for(int k=0;k<n;++k)if(v[k].type==GM_MOB_BLAZE)++before;
        gm_runtime_tick(&r,idle);
        n=gm_mobs_fill_views(&r.mobs,v,EW_MAX_ENTITIES);
        int after=0;for(int k=0;k<n;++k)if(v[k].type==GM_MOB_BLAZE)++after;
        if(after>before){
            spawns+=after-before;
        }
    }
    fprintf(stderr,"mob_live: spawner blaze spawns=%d\n",spawns);
    CHECK(spawns>=1,"autonomy: real blaze spawner produces live mobs");
    gm_runtime_destroy(&r);

    CHECK(blaze_schedule_receipt(NULL),
          "AIFireballAttack exact duty cycle and fireball cadence receipt");

    if(fail)return 1;
    fprintf(stderr,"mob_live: PASS\n");
    return 0;
}
