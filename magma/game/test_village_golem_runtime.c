#include "game/runtime.h"

#include <stdio.h>

static int fail;
#define CHECK(c, m) do { \
    if (!(c)) { fprintf(stderr, "FAIL: %s\n", m); fail = 1; } \
} while (0)

static const EwStore *store(const GmRuntime *r) {
    return r->mobs.current ? &r->mobs.b : &r->mobs.a;
}

static int event_count(
        const GmMobLive *m, int kind, int eid, int data) {
    int count = 0;
    for (int index = 0; index < gm_mobs_event_count(m); ++index) {
        GmMobEvent event;
        if (gm_mobs_event_get(m, index, &event)
                && event.kind == kind && event.eid == eid
                && event.data == data)
            ++count;
    }
    return count;
}

int main(void) {
    GmConfig config;
    GmRuntime runtime;
    JavaRandom random;
    char error[256] = {0};
    gm_config_defaults(&config);
    config.seed = 0;
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 3;
    config.mobs = 1;
    config.weather = 0;
    CHECK(gm_runtime_init(&runtime, &config, error, sizeof error), error);
    if (fail) return 1;
    gm_world_ensure(runtime.world, 0, 0, 3);
    gm_runtime_set_pose(&runtime, 0.5, 64.0, 0.5, 0.0F, 0.0F);

    /* Seed 3107 is the direct Village.java golden's accepted first attempt:
     * (6,64,-3). Build its exact ON_GROUND clearance and keep the 21 doors
     * away from that bounded candidate square. */
    for (int x = -10; x <= 10; ++x)
        for (int z = -10; z <= 10; ++z) {
            gm_world_set_block(runtime.world, x, 63, z, 1);
            for (int y = 64; y <= 67; ++y)
                gm_world_set_block(runtime.world, x, y, z, 0);
        }
    CHECK(gm_runtime_village_collection_begin(&runtime, 159, 1),
          "restore one village collection");
    CHECK(gm_runtime_village_state_restore(
              &runtime, 0, 10, 32, 0, 100, 159, 0,
              0, 64, 0, 0, 1344, 0),
          "restore spawn-eligible village state");
    for (int door = 0; door < 21; ++door) {
        int x = 20 + door;
        gm_world_set_block(runtime.world, x, 64, 0, 64);
        CHECK(gm_runtime_village_door_restore(
                  &runtime, 0, x, 64, 0, 2, 0, 100),
              "restore retained village door");
    }
    for (int villager = 0; villager < 10; ++villager)
        CHECK(gm_mobs_spawn_villager(
                  &runtime.mobs, -4.5 + villager, 64.0, 10.5,
                  villager % 5) > 0,
              "spawn counted village resident");
    jrand_set(&random, 3107);
    CHECK(gm_runtime_set_world_random_seed48(&runtime, random.seed),
          "restore world RNG at Village.tick boundary");

    gm_runtime_tick(&runtime, (GmAction){.hotbar_sel = -1});
    CHECK(runtime.village_collection_tick == 160,
          "production hook advances collection to the oracle tick");
    CHECK(runtime.village_state_count == 1
              && runtime.village_states[0].num_villagers == 10
              && runtime.village_states[0].num_golems == 1,
          "production Village.tick admits one golem");
    int golems = 0, golem_slot = -1;
    const EwStore *entities = store(&runtime);
    for (int slot = 1; slot < entities->count; ++slot)
        if (entities->alive[slot]
                && entities->type[slot] == EW_TYPE_IRON_GOLEM) {
            ++golems;
            golem_slot = slot;
        }
    CHECK(golems == 1, "spawn callback creates one live iron golem");
    if (golem_slot > 0) {
        int created = -1;
        CHECK(entities->x[golem_slot] == 6.0
                  && entities->y[golem_slot] == 64.0
                  && entities->z[golem_slot] == -3.0,
              "live golem retains exact Java candidate coordinates");
        CHECK(entities->health[golem_slot] == 100.0F,
              "village golem starts at 100 health");
        CHECK(gm_mobs_get_iron_golem_state(
                  &runtime.mobs, entities->id[golem_slot],
                  &created, NULL, NULL, NULL)
                  && created == 0,
              "village golem is not player-created");
    }
    for (int tick = 0; tick < 20; ++tick)
        gm_runtime_tick(&runtime, (GmAction){.hotbar_sel = -1});
    CHECK(runtime.village_states[0].num_golems == 1,
          "30-tick recount observes the represented golem");

    /* Seed 3 consumes ambient nextInt(1000)=734, DefendVillage's
     * nextInt(20)=0 with no low-reputation player, then the ordinary target
     * task's nextInt(10)=0. The remaining selector/navigation cursor produces
     * the pinned armor-adjusted first hit against the adjacent zombie. */
    entities = store(&runtime);
    golem_slot = gm_mobs_find_slot_by_eid(
        &runtime.mobs, entities->id[golem_slot]);
    int golem_eid = entities->id[golem_slot];
    EwStore *now = runtime.mobs.current
        ? &runtime.mobs.b : &runtime.mobs.a;
    EwStore *other = runtime.mobs.current
        ? &runtime.mobs.a : &runtime.mobs.b;
    now->x[golem_slot] = other->x[golem_slot] = 6.0;
    now->y[golem_slot] = other->y[golem_slot] = 64.0;
    now->z[golem_slot] = other->z[golem_slot] = -3.0;
    now->vx[golem_slot] = other->vx[golem_slot] = 0.0;
    now->vy[golem_slot] = other->vy[golem_slot] = 0.0;
    now->vz[golem_slot] = other->vz[golem_slot] = 0.0;
    int zombie_slot = gm_mobs_spawn(
        &runtime.mobs, EW_TYPE_ZOMBIE, 7.0, 64.0, -3.0);
    CHECK(zombie_slot > 0, "spawn adjacent active hostile");
    int zombie_eid = store(&runtime)->id[zombie_slot];
    jrand_set(&random, 3);
    CHECK(gm_mobs_set_entity_random_state(
              &runtime.mobs, golem_eid, random.seed, 0, 0.0),
          "seed active golem task cursor");
    CHECK(gm_mobs_set_iron_golem_state(
              &runtime.mobs, golem_eid, 0, 100, 0, 0),
          "set active golem timers");
    runtime.mobs.golem_target_task_tick[golem_slot] = 0;
    runtime.mobs.golem_target_eid[golem_slot] = -1;
    runtime.mobs.golem_path_delay[golem_slot] = 0;
    runtime.mobs.golem_path_target_x[golem_slot] = 0.0;
    runtime.mobs.golem_path_target_y[golem_slot] = 0.0;
    runtime.mobs.golem_path_target_z[golem_slot] = 0.0;

    gm_runtime_tick(&runtime, (GmAction){.hotbar_sel = -1});
    entities = store(&runtime);
    golem_slot = gm_mobs_find_slot_by_eid(&runtime.mobs, golem_eid);
    zombie_slot = gm_mobs_find_slot_by_eid(&runtime.mobs, zombie_eid);
    CHECK(runtime.mobs.golem_target_eid[golem_slot] == zombie_eid,
          "active target selector acquires represented hostile");
    if (entities->health[zombie_slot] != 1.30399895F) {
        fprintf(stderr, "golem target health: %.9g\n",
                entities->health[zombie_slot]);
        CHECK(0, "active golem applies exact armor-adjusted seeded hit");
    }
    CHECK(runtime.mobs.golem_attack_timer[golem_slot] == 9,
          "onLivingUpdate decrements attack animation after the hit");
    CHECK(event_count(
              &runtime.mobs, GM_MOB_EVENT_ENTITY_STATUS, golem_eid, 4) == 1,
          "active attack emits golem status 4 once");
    CHECK(event_count(
              &runtime.mobs, GM_MOB_EVENT_SOUND, golem_eid,
              GM_MOB_SOUND_IRON_GOLEM_ATTACK) == 1,
          "active attack emits the exact golem attack sound");
    CHECK(event_count(
              &runtime.mobs, GM_MOB_EVENT_ENTITY_STATUS, zombie_eid, 2) == 1,
          "mob damage enters the ordinary target hurt lifecycle");

    int attack_statuses = event_count(
        &runtime.mobs, GM_MOB_EVENT_ENTITY_STATUS, golem_eid, 4);
    gm_runtime_tick(&runtime, (GmAction){.hotbar_sel = -1});
    CHECK(event_count(
              &runtime.mobs, GM_MOB_EVENT_ENTITY_STATUS, golem_eid, 4)
              == attack_statuses,
          "melee cooldown prevents an immediate second attack");

    gm_runtime_destroy(&runtime);
    if (fail) return 1;
    puts("village_golem_runtime: PASS spawn/count plus active target/attack");
    return 0;
}
