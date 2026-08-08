#include "game/runtime.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(c, m) do { if (!(c)) { \
    fprintf(stderr, "FAIL: %s\n", (m)); return 1; } } while (0)

int main(void) {
    const char *checkpoint = "specialized-mob-capacity.bin";
    GmRuntime runtime;
    GmConfig config;
    GmLlamaSpit spit;
    GmSnowmanShot shot;
    char error[256] = {0};
    uint64_t entity_seed = UINT64_C(0x123456789abc);
    uint64_t uuid_seed = UINT64_C(0x23456789abcd);
    int next_eid = 20000;

    gm_config_defaults(&config);
    config.seed = 0;
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.mobs = 0;
    config.weather = 0;
    CHECK(gm_runtime_init(&runtime, &config, error, sizeof error), error);
    for (int x = -4; x <= 32; ++x)
        for (int z = -8; z <= 5; ++z) {
            gm_world_set_block(runtime.world, x, 0, z, 1);
            for (int y = 1; y <= 8; ++y)
                gm_world_set_block(runtime.world, x, y, z, 0);
        }

    int evoker = gm_mobs_spawn(
        &runtime.mobs, EW_TYPE_EVOKER, 0.5, 1.0, 0.5);
    CHECK(evoker > 0 && gm_mobs_evoker_cast_attack(
              &runtime.mobs, runtime.world,
              (const struct McSinTable *)&runtime.sin_table,
              (runtime.mobs.current ? runtime.mobs.b.id
                                    : runtime.mobs.a.id)[evoker],
              20.5, 1.0, 0.5) == 16,
          "initialize the evoker-fang registry through a real cast");
    runtime.mobs.evoker_fang_count = GM_EVOKER_FANGS;
    CHECK(gm_mobs_evoker_cast_attack(
              &runtime.mobs, runtime.world,
              (const struct McSinTable *)&runtime.sin_table,
              (runtime.mobs.current ? runtime.mobs.b.id
                                    : runtime.mobs.a.id)[evoker],
              20.5, 1.0, 0.5) == 16
              && runtime.mobs.evoker_fang_count == GM_EVOKER_FANGS + 16
              && runtime.mobs.evoker_fangs_cap > GM_EVOKER_FANGS,
          "evoker casts grow beyond the former fang limit");

    CHECK(gm_mobs_spawn_llama_exact(
              &runtime.mobs, 10000, 0.0, 1.0, 0.0,
              0.0, 0.0, 0.0, 0.0F, 20.0F, 0,
              20.0, 0.175, 0.5, 0, GM_HORSE_TAME,
              0, 0, 3, -1, 0, 0, 0) > 0
              && gm_mobs_spawn_exact(
                  &runtime.mobs, GM_MOB_COW, 10001,
                  8.0, 1.0, 0.0, 0.0, 0.0, 0.0,
                  0.0F, 10.0F, 0, 0, 0, 0) > 0
              && gm_mobs_llama_spit_attack_exact(
                  &runtime.mobs, 10000, 10001, 10002,
                  UINT64_C(0x3456789abcde), 0, 0.0,
                  &runtime.sin_table, &spit),
          "initialize the llama-spit registry through a real attack");
    runtime.mobs.llama_spit_count = GM_LLAMA_SPITS;
    runtime.mobs.event_count = GM_MOB_EVENT_CAPACITY;
    runtime.mobs.particle_batch_count = GM_MOB_PARTICLE_BATCH_CAPACITY;
    CHECK(gm_mobs_llama_spit_attack_exact(
              &runtime.mobs, 10000, 10001, 10003,
              UINT64_C(0x456789abcdef), 0, 0.0,
              &runtime.sin_table, &spit)
              && runtime.mobs.llama_spit_count == GM_LLAMA_SPITS + 1
              && runtime.mobs.llama_spits_cap > GM_LLAMA_SPITS
              && runtime.mobs.event_count == GM_MOB_EVENT_CAPACITY + 1
              && runtime.mobs.events_cap > GM_MOB_EVENT_CAPACITY
              && runtime.mobs.event_dropped == 0
              && runtime.mobs.particle_batch_count
                  == GM_MOB_PARTICLE_BATCH_CAPACITY + 1
              && runtime.mobs.particle_batches_cap
                  > GM_MOB_PARTICLE_BATCH_CAPACITY
              && runtime.mobs.particle_batch_dropped == 0
              && spit.eid == 10003,
          "llama attacks grow spit and ordered event streams together");

    CHECK(gm_mobs_spawn_exact(
              &runtime.mobs, EW_TYPE_SNOWMAN, 11000,
              0.0, 1.0, 4.0, 0.0, 0.0, 0.0,
              0.0F, 4.0F, 0, 0, 0, 0) > 0
              && gm_mobs_spawn_exact(
                  &runtime.mobs, EW_TYPE_ZOMBIE, 11001,
                  8.0, 1.0, 4.0, 0.0, 0.0, 0.0,
                  0.0F, 20.0F, 0, 0, 0, 0) > 0
              && gm_mobs_snowman_attack_exact(
                  &runtime.mobs, 11000, 11001,
                  &entity_seed, &uuid_seed, &next_eid, &shot),
          "initialize the snowball launch registry through a real attack");
    runtime.mobs.snowman_shot_count = GM_SNOWMAN_SHOTS;
    CHECK(gm_mobs_snowman_attack_exact(
              &runtime.mobs, 11000, 11001,
              &entity_seed, &uuid_seed, &next_eid, &shot)
              && runtime.mobs.snowman_shot_count == GM_SNOWMAN_SHOTS + 1
              && runtime.mobs.snowman_shots_cap > GM_SNOWMAN_SHOTS,
          "Snow Golem attacks grow beyond the former launch limit");

    CHECK(gm_mobs_spawn_exact(
              &runtime.mobs, EW_TYPE_SKELETON, 11500,
              0.5, 1.0, 6.5, 0.0, 0.0, 0.0,
              0.0F, 20.0F, 1, 0, 0, 0) > 0
              && gm_mobs_skeleton_trap_shoot_exact(
                  &runtime.mobs, 11500, 8.5, 1.0, 6.5, 1.8F),
          "initialize the skeleton-trap shot registry through a real shot");
    runtime.mobs.skeleton_trap_shot_count = GM_SKELETON_TRAP_SHOTS;
    CHECK(gm_mobs_skeleton_trap_shoot_exact(
              &runtime.mobs, 11500, 8.5, 1.0, 6.5, 1.8F)
              && runtime.mobs.skeleton_trap_shot_count
                  == GM_SKELETON_TRAP_SHOTS + 1
              && runtime.mobs.skeleton_trap_shots_cap
                  > GM_SKELETON_TRAP_SHOTS,
          "skeleton-trap shots grow beyond the four-rider burst limit");

    runtime.mobs.terminal_particles = calloc(
        GM_MOB_TERMINAL_PARTICLE_CAPACITY,
        sizeof *runtime.mobs.terminal_particles);
    CHECK(runtime.mobs.terminal_particles,
          "allocate the former terminal-particle boundary fixture");
    runtime.mobs.terminal_particles_cap =
        GM_MOB_TERMINAL_PARTICLE_CAPACITY;
    runtime.mobs.terminal_particle_count =
        GM_MOB_TERMINAL_PARTICLE_CAPACITY;
    int chicken = gm_mobs_spawn_exact(
        &runtime.mobs, GM_MOB_CHICKEN, 12000,
        0.5, 1.0, 8.5, 0.0, 0.0, 0.0,
        0.0F, 1.0F, 1, 0, 19, 0);
    CHECK(chicken > 0, "stage a production terminal-particle append");
    runtime.mobs.a.health[chicken] = runtime.mobs.b.health[chicken] = 0.0F;
    runtime.mobs.entity_dead[chicken] = 1;
    gm_mobs_tick_controlled(
        &runtime.mobs, runtime.world, NULL,
        (struct PsvPlayer *)&runtime.player,
        runtime.ox, runtime.oz, 0, NULL, 0,
        NULL, NULL, NULL, NULL);
    CHECK(runtime.mobs.terminal_particle_count
              == GM_MOB_TERMINAL_PARTICLE_CAPACITY + 1
              && runtime.mobs.terminal_particles_cap
                  > GM_MOB_TERMINAL_PARTICLE_CAPACITY
              && runtime.mobs.terminal_particle_dropped == 0,
          "terminal-particle stream grows without losing a death batch");

    CHECK(gm_runtime_write_checkpoint(&runtime, checkpoint)
              && gm_runtime_load_checkpoint(&runtime, checkpoint)
              && runtime.mobs.evoker_fang_count == GM_EVOKER_FANGS + 16
              && runtime.mobs.evoker_fangs_cap > GM_EVOKER_FANGS
              && runtime.mobs.llama_spit_count == GM_LLAMA_SPITS + 1
              && runtime.mobs.llama_spits_cap > GM_LLAMA_SPITS
              && runtime.mobs.snowman_shot_count == GM_SNOWMAN_SHOTS + 1
              && runtime.mobs.snowman_shots_cap > GM_SNOWMAN_SHOTS
              && runtime.mobs.skeleton_trap_shot_count
                  == GM_SKELETON_TRAP_SHOTS + 1
              && runtime.mobs.skeleton_trap_shots_cap
                  > GM_SKELETON_TRAP_SHOTS
              && runtime.mobs.event_count == GM_MOB_EVENT_CAPACITY + 3
              && runtime.mobs.particle_batch_count
                  == GM_MOB_PARTICLE_BATCH_CAPACITY + 1
              && runtime.mobs.terminal_particle_count
                  == GM_MOB_TERMINAL_PARTICLE_CAPACITY + 1,
          "all grown specialized-mob registries survive checkpoint reload");
    (void)remove(checkpoint);
    gm_runtime_destroy(&runtime);
    puts("specialized_mob_capacity: PASS");
    return 0;
}
