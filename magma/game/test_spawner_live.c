#include "game/runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define CHECK(c, m) do { if (!(c)) { \
    fprintf(stderr, "FAIL: %s\n", (m)); return 1; } } while (0)

static int init_fixture(GmRuntime *runtime) {
    GmConfig config;
    char error[256];
    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.mobs = 0;
    config.weather = 0;
    if (!gm_runtime_init(runtime, &config, error, sizeof error)) {
        fprintf(stderr, "FAIL: %s\n", error);
        return 0;
    }
    runtime->mobs.active_dimension = 0;
    return gm_runtime_load_block(runtime, 12, 78, 8, 52, 0);
}

static int configure(GmRuntime *runtime, int delay, int max_nearby) {
    return gm_mobs_spawner_set_state(
        &runtime->mobs, 12, 78, 8, GM_MOB_ZOMBIE,
        delay, 7, 11, 1, max_nearby, 16, 4);
}

static int tick(GmRuntime *runtime, double player_x) {
    return gm_mobs_tick_spawner_at(
        &runtime->mobs, runtime->world, 12, 78, 8,
        player_x, 78.5, 8.5,
        &runtime->world_random_seed48, &runtime->math_random_seed48,
        &runtime->entity_seed_generator_seed48,
        &runtime->server_uuid_random_seed48,
        &runtime->next_entity_id, NULL);
}

static int alive(const GmRuntime *runtime) {
    const EwStore *store = runtime->mobs.current
        ? &runtime->mobs.b : &runtime->mobs.a;
    int count = 0;
    for (int slot = 1; slot < EW_MAX_ENTITIES; ++slot)
        if (store->alive[slot]) ++count;
    return count;
}

int main(void) {
    GmRuntime runtime;

    {
        GmNbtBlob spawn = {0}, effect = {0};
        int eid, slot;
        CHECK(init_fixture(&runtime), "custom-effect spawner fixture");
        CHECK(gm_runtime_load_block(&runtime, 14, 78, 8, 2, 0),
              "custom-effect pig spawn grass");
        CHECK(gm_nbt_blob_make_empty(&spawn)
                  && gm_nbt_blob_make_empty(&effect)
                  && gm_nbt_blob_set_byte(&effect, "Id", 19)
                  && gm_nbt_blob_set_byte(&effect, "Amplifier", 2)
                  && gm_nbt_blob_set_int(&effect, "Duration", 321)
                  && gm_nbt_blob_set_byte(&effect, "Ambient", 1)
                  && gm_nbt_blob_set_byte(&effect, "ShowParticles", 0)
                  && gm_nbt_blob_append_compound_list(
                      &spawn, "ActiveEffects", &effect)
                  && gm_nbt_blob_set_byte(&spawn, "Color", 14)
                  && gm_nbt_blob_set_byte(&spawn, "Sheared", 1),
              "construct custom spawner ActiveEffects payload");
        eid = runtime.next_entity_id;
        CHECK(gm_mobs_spawn_spawner_candidate(
                  &runtime.mobs, runtime.world, GM_MOB_SHEEP,
                  12, 78, 8, 4, 6, 14.5, 79.0, 8.5,
                  0.5, 78.0, 0.5,
                  &runtime.world_random_seed48,
                  &runtime.math_random_seed48,
                  &runtime.entity_seed_generator_seed48,
                  &runtime.server_uuid_random_seed48,
                  &runtime.next_entity_id, &spawn, 0) == 1,
              "spawn custom-effect spawner candidate");
        slot = gm_mobs_find_slot_by_eid(&runtime.mobs, eid);
        CHECK(slot > 0 && runtime.mobs.entity_effect_count[slot] == 1
                  && runtime.mobs.entity_effects[slot][0].id == 19
                  && runtime.mobs.entity_effects[slot][0].amplifier == 2
                  && runtime.mobs.entity_effects[slot][0].duration == 321
                  && runtime.mobs.entity_effect_flags[slot][0] == 3
                  && runtime.mobs.sheep_data[slot] == (14 | 16)
                  && runtime.mobs.passive_sheared[slot] == 1,
              "custom spawner restores effects and subclass payload");
        gm_nbt_blob_clear(&effect);
        gm_nbt_blob_clear(&spawn);
        CHECK(gm_runtime_load_block(&runtime, 16, 78, 8, 2, 0)
                  && gm_nbt_blob_make_empty(&spawn)
                  && gm_nbt_blob_set_int(&spawn, "Size", 3),
              "construct custom slime spawner payload");
        eid = runtime.next_entity_id;
        CHECK(gm_mobs_spawn_spawner_candidate(
                  &runtime.mobs, runtime.world, GM_MOB_SLIME,
                  12, 78, 8, 4, 6, 16.5, 79.0, 8.5,
                  0.5, 78.0, 0.5,
                  &runtime.world_random_seed48,
                  &runtime.math_random_seed48,
                  &runtime.entity_seed_generator_seed48,
                  &runtime.server_uuid_random_seed48,
                  &runtime.next_entity_id, &spawn, 0) == 1,
              "spawn custom slime candidate");
        slot = gm_mobs_find_slot_by_eid(&runtime.mobs, eid);
        CHECK(slot > 0 && runtime.mobs.size[slot] == 4,
              "custom spawner restores serialized slime size plus one");
        gm_nbt_blob_clear(&spawn);
        CHECK(gm_runtime_load_block(&runtime, 19, 78, 8, 2, 0)
                  && gm_nbt_blob_make_empty(&spawn)
                  && gm_nbt_blob_set_string(
                      &spawn, "OwnerUUID",
                      "00000000-0000-0000-0000-000000000001")
                  && gm_nbt_blob_set_byte(&spawn, "Sitting", 1)
                  && gm_nbt_blob_set_byte(&spawn, "Angry", 1)
                  && gm_nbt_blob_set_byte(&spawn, "CollarColor", 5),
              "construct custom wolf spawner payload");
        eid = runtime.next_entity_id;
        CHECK(gm_mobs_spawn_spawner_candidate(
                  &runtime.mobs, runtime.world, GM_MOB_WOLF,
                  12, 78, 8, 8, 6, 19.5, 79.0, 8.5,
                  0.5, 78.0, 0.5,
                  &runtime.world_random_seed48,
                  &runtime.math_random_seed48,
                  &runtime.entity_seed_generator_seed48,
                  &runtime.server_uuid_random_seed48,
                  &runtime.next_entity_id, &spawn, 0) == 1,
              "spawn custom wolf candidate");
        slot = gm_mobs_find_slot_by_eid(&runtime.mobs, eid);
        CHECK(slot > 0 && runtime.mobs.tameable_tamed[slot]
                  && runtime.mobs.tameable_owner[slot]
                  && runtime.mobs.tameable_sitting[slot]
                  && runtime.mobs.tameable_sit_requested[slot]
                  && runtime.mobs.wolf_angry[slot]
                  && runtime.mobs.tameable_variant[slot] == 5,
              "custom spawner restores tameable and wolf payload");
        gm_nbt_blob_clear(&spawn);
        CHECK(gm_runtime_load_block(&runtime, 22, 78, 8, 2, 0)
                  && gm_nbt_blob_make_empty(&spawn)
                  && gm_nbt_blob_set_byte(&spawn, "Tame", 1)
                  && gm_nbt_blob_set_byte(&spawn, "EatingHaystack", 1)
                  && gm_nbt_blob_set_int(&spawn, "Temper", 73)
                  && gm_nbt_blob_set_int(&spawn, "Variant", 1284),
              "construct custom horse spawner payload");
        eid = runtime.next_entity_id;
        CHECK(gm_mobs_spawn_spawner_candidate(
                  &runtime.mobs, runtime.world, GM_MOB_HORSE,
                  12, 78, 8, 12, 6, 22.5, 79.0, 8.5,
                  0.5, 78.0, 0.5,
                  &runtime.world_random_seed48,
                  &runtime.math_random_seed48,
                  &runtime.entity_seed_generator_seed48,
                  &runtime.server_uuid_random_seed48,
                  &runtime.next_entity_id, &spawn, 0) == 1,
              "spawn custom horse candidate");
        slot = gm_mobs_find_slot_by_eid(&runtime.mobs, eid);
        CHECK(slot > 0
                  && (runtime.mobs.horse_status[slot] & GM_HORSE_TAME)
                  && (runtime.mobs.horse_status[slot] & GM_HORSE_EATING)
                  && runtime.mobs.horse_temper[slot] == 73
                  && runtime.mobs.horse_variant[slot] == 1284,
              "custom spawner restores common horse payload");
        gm_nbt_blob_clear(&spawn);
        gm_runtime_destroy(&runtime);
    }

    CHECK(init_fixture(&runtime) && configure(&runtime, 20, 6),
          "inactive spawner fixture");
    CHECK(gm_runtime_set_world_random_seed48(&runtime, 0),
          "inactive RNG cursor");
    CHECK(tick(&runtime, 100.0) == 0, "inactive spawner tick");
    printf("T0 %d %012llx\n", runtime.mobs.spawners[0].delay,
           (unsigned long long)runtime.world_random_seed48);
    gm_runtime_destroy(&runtime);

    CHECK(init_fixture(&runtime) && configure(&runtime, 20, 6),
          "countdown spawner fixture");
    CHECK(gm_runtime_set_world_random_seed48(&runtime, 0),
          "countdown RNG cursor");
    CHECK(tick(&runtime, 12.5) == 0, "countdown spawner tick");
    printf("T1 %d %012llx\n", runtime.mobs.spawners[0].delay,
           (unsigned long long)runtime.world_random_seed48);
    gm_runtime_destroy(&runtime);

    for (int saved = 0; saved <= 1; ++saved) {
        CHECK(init_fixture(&runtime) && configure(&runtime, 0, 1),
              "nearby-cap spawner fixture");
        CHECK(gm_mobs_spawn(
                  &runtime.mobs, GM_MOB_ZOMBIE, 12.5, 78.0, 8.5) > 0,
              "nearby zombie");
        runtime.next_entity_id = runtime.mobs.next_id;
        if (saved)
            CHECK(gm_mobs_spawner_add_potential(
                      &runtime.mobs, 12, 78, 8, GM_MOB_ZOMBIE, 1),
                  "saved weight-one SpawnPotentials row");
        CHECK(gm_runtime_set_world_random_seed48(&runtime, 0),
              "nearby-cap RNG cursor");
        CHECK(tick(&runtime, 12.5) == 0, "nearby-cap spawner tick");
        printf("T%c %d %012llx %d\n", saved ? 'R' : 'C',
               runtime.mobs.spawners[0].delay,
               (unsigned long long)runtime.world_random_seed48,
               alive(&runtime));
        if (saved) {
            char checkpoint[] = "game/.block-spawner-XXXXXX";
            int fd = mkstemp(checkpoint);
            CHECK(fd >= 0, "create block spawner checkpoint");
            CHECK(close(fd) == 0, "close block spawner checkpoint");
            CHECK(gm_runtime_write_checkpoint(&runtime, checkpoint),
                  "write block spawner checkpoint");
            CHECK(gm_runtime_load_checkpoint(&runtime, checkpoint),
                  "load block spawner checkpoint");
            CHECK(unlink(checkpoint) == 0,
                  "remove block spawner checkpoint");
            CHECK(runtime.mobs.spawners[0].active
                      && runtime.mobs.spawners[0].entity_type
                          == GM_MOB_ZOMBIE
                      && runtime.mobs.spawners[0].delay == 8
                      && runtime.mobs.spawners[0].potential_count == 1
                      && runtime.mobs.spawners[0].potentials[0].type
                          == GM_MOB_ZOMBIE
                      && runtime.mobs.spawners[0].potentials[0].weight == 1
                      && runtime.world_random_seed48
                          == UINT64_C(0x3bb194f24a25),
                  "checkpoint retains block spawner state/RNG");
        }
        gm_runtime_destroy(&runtime);
    }

    CHECK(init_fixture(&runtime) && configure(&runtime, 20, 6),
          "gamerule spawner fixture");
    {
        McGameRules rules = mc_gamerules_default();
        GmAction idle = {0};
        rules.doMobSpawning = 0;
        runtime.mobs_enabled = 1;
        gm_runtime_set_gamerules(&runtime, &rules);
        gm_runtime_set_pose(&runtime, 12.5, 78.0, 8.5, 0.0F, 0.0F);
        idle.hotbar_sel = -1;
        gm_runtime_tick(&runtime, idle);
        CHECK(!runtime.mobs.natural_spawning_enabled,
              "doMobSpawning false suppresses WorldEntitySpawner");
        CHECK(runtime.mobs.spawners[0].delay == 19,
              "doMobSpawning false does not suppress block spawners");
    }
    gm_runtime_destroy(&runtime);

    CHECK(init_fixture(&runtime), "spawner-capacity fixture");
    for (int index = 0; index <= GM_SPAWNERS; ++index)
        CHECK(gm_mobs_register_spawner(
                  &runtime.mobs, 100 + index, 78, 100, GM_MOB_ZOMBIE)
                  == index,
              "block-spawner store grows beyond former 64-tile limit");
    CHECK(runtime.mobs.spawners_cap > GM_SPAWNERS
              && runtime.mobs.spawners[GM_SPAWNERS].active,
          "grown block-spawner payload remains addressable");
    for (int index = 0; index < 17; ++index)
        CHECK(gm_mobs_spawner_add_potential(
                  &runtime.mobs, 100, 78, 100,
                  index & 1 ? GM_MOB_SKELETON : GM_MOB_ZOMBIE,
                  index + 1),
              "block-spawner potentials grow beyond former 16-row limit");
    CHECK(runtime.mobs.spawners[0].potential_count == 17
              && runtime.mobs.spawners[0].potential_cap > 16,
          "grown block-spawner potential payload remains addressable");
    {
        char checkpoint[] = "game/.block-spawner-capacity-XXXXXX";
        int fd = mkstemp(checkpoint);
        CHECK(fd >= 0 && close(fd) == 0,
              "create block-spawner capacity checkpoint");
        CHECK(gm_runtime_write_checkpoint(&runtime, checkpoint)
                  && gm_runtime_load_checkpoint(&runtime, checkpoint)
                  && unlink(checkpoint) == 0
                  && runtime.mobs.spawners_cap > GM_SPAWNERS
                  && runtime.mobs.spawners[GM_SPAWNERS].active
                  && runtime.mobs.spawners[GM_SPAWNERS].x
                      == 100 + GM_SPAWNERS
                  && runtime.mobs.spawners[0].potential_count == 17
                  && runtime.mobs.spawners[0].potentials[16].weight == 17,
              "grown block-spawner stores and potentials survive checkpoint reload");
    }
    gm_runtime_destroy(&runtime);

    puts("spawner_live: PASS");
    return 0;
}
