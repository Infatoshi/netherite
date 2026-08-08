#include "game/runtime.h"
#include "container_click.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(condition, message) do {                                      \
    if (!(condition)) {                                                     \
        fprintf(stderr, "FAIL: %s\n", (message));                         \
        return 0;                                                           \
    }                                                                       \
} while (0)

static uint64_t double_bits(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static int find_drop(const GmLiveSim *drops, int item) {
    for (int slot = 0; slot < GM_LIVE_MAX; ++slot)
        if (drops->ents[slot].active && drops->ents[slot].item == item)
            return slot;
    return -1;
}

static int init_runtime(GmRuntime *runtime) {
    GmConfig config;
    char error[256];
    gm_config_defaults(&config);
    config.seed = 0;
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.mobs = 0;
    config.weather = 0;
    if (!gm_runtime_init(runtime, &config, error, sizeof error)) {
        fprintf(stderr, "FAIL: %s\n", error);
        return 0;
    }
    return 1;
}

static int exact_state_and_inventory(void) {
    GmMobLive mobs;
    GmLlamaState llama;
    gm_mobs_init(&mobs, 0);
    CHECK(gm_mobs_spawn_llama_exact(
              &mobs, 900, 8.5, 100.0, 12.5,
              0.125, -0.25, 0.375, 27.0F, 19.5F, 1,
              23.0, 0.175, 0.5, -1200,
              GM_HORSE_TAME | GM_HORSE_SADDLED | GM_HORSE_BRED,
              17, 3, 2, 5, 1, 1, 1) > 0,
          "spawn exact saved llama");
    CHECK(gm_mobs_get_llama_state(&mobs, 900, &llama)
              && llama.horse.type == GM_MOB_LLAMA
              && llama.horse.eid == 900
              && llama.horse.growing_age == -1200
              && llama.horse.status == (GM_HORSE_TAME | GM_HORSE_BRED)
              && llama.horse.temper == 17
              && llama.horse.health == 19.5F
              && llama.horse.max_health == 23.0
              && llama.horse.movement_speed == 0.175
              && llama.horse.jump_strength == 0.5
              && llama.variant == 3 && llama.strength == 2
              && llama.decor == 5 && llama.horse.chested
              && llama.did_spit && llama.leashed,
          "llama state round-trips without constructor rerolls");
    CHECK(!gm_mobs_set_horse_inventory(
              &mobs, 900, 0, ic_mk(329, 1, 0)),
          "llama saddle slot rejects saddles");
    CHECK(!gm_mobs_set_horse_inventory(
              &mobs, 900, 1, ic_mk(35, 1, 5))
              && !gm_mobs_set_horse_inventory(
                  &mobs, 900, 1, ic_mk(171, 1, 16)),
          "llama decor slot rejects non-carpet and invalid metadata");
    CHECK(gm_mobs_set_horse_inventory(
              &mobs, 900, 1, ic_mk(171, 1, 14))
              && gm_mobs_set_horse_inventory(
                  &mobs, 900, 2, ic_mk(264, 3, 0))
              && gm_mobs_set_horse_inventory(
                  &mobs, 900, 7, ic_mk(260, 7, 0))
              && !gm_mobs_set_horse_inventory(
                  &mobs, 900, 8, ic_mk(260, 1, 0))
              && gm_mobs_get_llama_state(&mobs, 900, &llama)
              && llama.decor == 14
              && llama.horse.inventory[1].item == 171
              && llama.horse.inventory[2].count == 3
              && llama.horse.inventory[7].count == 7,
          "strength two llama exposes exactly six storage slots");
    CHECK(gm_mobs_set_horse_inventory(&mobs, 900, 1, ic_empty())
              && gm_mobs_get_llama_state(&mobs, 900, &llama)
              && llama.decor == -1,
          "removing carpet clears synchronized decor metadata");
    CHECK(gm_mobs_spawn_llama_exact(
              &mobs, 901, 0.5, 100.0, 0.5,
              0.0, 0.0, 0.0, 0.0F, 20.0F, 1,
              20.0, 0.175, 0.5, 0, GM_HORSE_TAME,
              0, 0, 5, -1, 1, 0, 0) > 0
              && gm_mobs_set_horse_inventory(
                  &mobs, 901, 16, ic_mk(1, 64, 0))
              && !gm_mobs_set_horse_inventory(
                  &mobs, 901, 17, ic_mk(1, 1, 0)),
          "strength five llama reaches the bounded seventeen-slot ceiling");
    return 1;
}

static int chest_interaction(void) {
    GmMobLive mobs;
    GmLlamaState llama;
    GmMobEvent sound;
    IsrInv inventory;
    gm_mobs_init(&mobs, 0);
    isr_init(&inventory);
    CHECK(gm_mobs_spawn_llama_exact(
              &mobs, 910, 0.5, 100.0, 0.5,
              0.0, 0.0, 0.0, 0.0F, 20.0F, 1,
              20.0, 0.175, 0.5, 0, GM_HORSE_TAME,
              0, 0, 3, -1, 0, 0, 0) > 0
              && gm_mobs_set_entity_random_state(
                  &mobs, 910, UINT64_C(1234), 0, 0.0),
          "stage tame unchested llama with exact entity RNG");
    isr_set_stack(&inventory, 0, ic_mk(54, 2, 0));
    CHECK(gm_mobs_horse_equip_chest(&mobs, 910, &inventory, 0, 0)
              && gm_mobs_get_llama_state(&mobs, 910, &llama)
              && llama.horse.chested
              && isr_get_stack(&inventory, 0).count == 1
              && gm_mobs_event_count(&mobs) == 1
              && gm_mobs_event_get(&mobs, 0, &sound)
              && sound.kind == GM_MOB_EVENT_SOUND
              && sound.data == GM_MOB_SOUND_LLAMA_CHEST
              && sound.volume == 1.0F,
          "tame llama consumes chest and emits its Java chest sound");
    CHECK(!gm_mobs_horse_equip_chest(&mobs, 910, &inventory, 0, 0)
              && isr_get_stack(&inventory, 0).count == 1,
          "already-chested llama interaction is atomic");
    return 1;
}

static int feeding_and_mount(void) {
    GmMobLive mobs;
    GmLlamaState llama;
    GmMobEvent sound;
    IsrInv inventory;
    int ridden_eid = -1;
    gm_mobs_init(&mobs, 0);
    isr_init(&inventory);
    CHECK(gm_mobs_spawn_llama_exact(
              &mobs, 920, 0.5, 100.0, 0.5,
              0.0, 0.0, 0.0, 0.0F, 10.0F, 1,
              20.0, 0.175, 0.5, -1000, 0,
              25, 1, 1, -1, 0, 0, 0) > 0
              && gm_mobs_set_entity_random_state(
                  &mobs, 920, UINT64_C(987654321), 0, 0.0),
          "stage exact child llama feeding fixture");
    isr_set_stack(&inventory, 0, ic_mk(296, 2, 0));
    CHECK(gm_mobs_horse_feed(&mobs, 920, &inventory, 0, 0)
              && gm_mobs_get_llama_state(&mobs, 920, &llama)
              && llama.horse.health == 12.0F
              && llama.horse.growing_age == -800
              && llama.horse.temper == 28
              && (llama.horse.status & GM_HORSE_MOUTH_OPEN)
              && isr_get_stack(&inventory, 0).count == 1
              && gm_mobs_event_get(&mobs, 0, &sound)
              && sound.data == GM_MOB_SOUND_LLAMA_EAT,
          "llama wheat heals, grows ten seconds, tempers, and sounds");
    CHECK(!gm_mobs_horse_mount(&mobs, 920),
          "child llama cannot be mounted");
    CHECK(gm_mobs_set_growing_age(&mobs, 920, 0),
          "age llama fixture to adult");
    {
        int slot = gm_mobs_find_slot_by_eid(&mobs, 920);
        CHECK(slot > 0, "find adult llama slot");
        mobs.horse_status[slot] |= GM_HORSE_TAME;
        mobs.a.health[slot] = 15.0F;
        mobs.b.health[slot] = 15.0F;
    }
    isr_set_stack(&inventory, 0, ic_mk(170, 1, 0));
    CHECK(gm_mobs_horse_feed(&mobs, 920, &inventory, 0, 0)
              && gm_mobs_get_llama_state(&mobs, 920, &llama)
              && llama.horse.health == 20.0F
              && llama.horse.temper == 30
              && mobs.sheep_in_love[
                    gm_mobs_find_slot_by_eid(&mobs, 920)] == 600
              && isr_get_stack(&inventory, 0).count == 0,
          "hay heals, caps temper, and starts tame-adult love");
    CHECK(gm_mobs_horse_mount(&mobs, 920)
              && gm_mobs_horse_riding(&mobs, &ridden_eid)
              && ridden_eid == 920
              && !gm_mobs_horse_set_jump_power(&mobs, 90),
          "adult llama mounts but cannot expose saddled steering");
    return 1;
}

static int genetics_and_mating(void) {
    GmMobLive mobs;
    GmLlamaState child;
    GmAnimalMateResult birth;
    uint64_t world_seed = UINT64_C(0x123456789abc);
    uint64_t math_seed = UINT64_C(0x456789abcdef);
    int next_eid = 932;
    int delay = 0;
    int result = GM_SHEEP_MATE_NONE;
    gm_mobs_init(&mobs, 0);
    CHECK(gm_mobs_spawn_llama_exact(
              &mobs, 930, 0.5, 100.0, 0.5,
              0.0, 0.0, 0.0, 0.0F, 22.0F, 0,
              22.0, 0.24, 0.4, 0, GM_HORSE_TAME,
              0, 3, 3, -1, 0, 0, 0) > 0
              && gm_mobs_spawn_llama_exact(
                  &mobs, 931, 2.5, 100.0, 0.5,
                  0.0, 0.0, 0.0, 0.0F, 22.0F, 0,
                  22.0, 0.13, 0.54, 0, GM_HORSE_TAME,
                  0, 1, 2, -1, 0, 0, 0) > 0
              && gm_mobs_restore_horse_lifecycle(
                  &mobs, 930, 600, 0, 0, 0, 0, 0, 0, 0, 0,
                  0, 0, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F,
                  0.0F, 0.0F, 0.0F, 0.0F, 0.0F)
              && gm_mobs_restore_horse_lifecycle(
                  &mobs, 931, 600, 0, 0, 0, 0, 0, 0, 0, 0,
                  0, 0, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F,
                  0.0F, 0.0F, 0.0F, 0.0F, 0.0F)
              && gm_mobs_set_entity_random_state(
                  &mobs, 930, UINT64_C(20003954201896), 0, 0.0),
          "stage Java-oracle llama genetics row");
    CHECK(gm_mobs_llama_create_child(&mobs, 930, 931, 932, &child) > 0
              && child.horse.growing_age == -24000
              && child.strength == 2 && child.variant == 1
              && double_bits(child.horse.max_health)
                  == UINT64_C(0x4037000000000000)
              && double_bits(child.horse.jump_strength)
                  == UINT64_C(0x3fe096ac408ac132)
              && double_bits(child.horse.movement_speed)
                  == UINT64_C(0x3fc6d29ae5ba181d),
          "llama child attributes, strength, and variant match Java bits");

    gm_mobs_init(&mobs, 0);
    CHECK(gm_mobs_spawn_llama_exact(
              &mobs, 930, 0.5, 100.0, 0.5,
              0.0, 0.0, 0.0, 0.0F, 22.0F, 0,
              22.0, 0.24, 0.4, 0, GM_HORSE_TAME,
              0, 3, 3, -1, 0, 0, 0) > 0
              && gm_mobs_spawn_llama_exact(
                  &mobs, 931, 2.5, 100.0, 0.5,
                  0.0, 0.0, 0.0, 0.0F, 22.0F, 0,
                  22.0, 0.13, 0.54, 0, GM_HORSE_TAME,
                  0, 1, 2, -1, 0, 0, 0) > 0
              && gm_mobs_restore_horse_lifecycle(
                  &mobs, 930, 600, 0, 0, 0, 0, 0, 0, 0, 0,
                  0, 0, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F,
                  0.0F, 0.0F, 0.0F, 0.0F, 0.0F)
              && gm_mobs_restore_horse_lifecycle(
                  &mobs, 931, 600, 0, 0, 0, 0, 0, 0, 0, 0,
                  0, 0, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F,
                  0.0F, 0.0F, 0.0F, 0.0F, 0.0F)
              && gm_mobs_set_entity_random_state(
                  &mobs, 930, UINT64_C(20003954201896), 0, 0.0),
          "restage llama full mating boundary");
    for (int update = 0; update < 60; ++update) {
        result = gm_mobs_animal_mate_update(
            &mobs, 930, 931, &delay, 0, 1,
            &world_seed, &math_seed, &next_eid, 1, &birth);
        CHECK(result == (update == 59
                  ? GM_SHEEP_MATE_BORN : GM_SHEEP_MATE_WAITING),
              "llama mating waits exactly sixty updates");
    }
    CHECK(birth.child_eid == 932 && birth.child_type == GM_MOB_LLAMA
              && birth.child_slot > 0 && birth.xp_eid == 933
              && birth.xp_slot >= 0 && next_eid == 934
              && gm_mobs_get_llama_state(&mobs, 932, &child)
              && child.strength == 2 && child.variant == 1
              && double_bits(child.horse.jump_strength)
                  == UINT64_C(0x3fe096ac408ac132)
              && gm_mobs_particle_batch_count(&mobs) == 1,
          "llama full mating boundary births exact child, hearts, and XP");
    return 1;
}

static int caravan_semantics(void) {
    GmMobLive mobs;
    GmLlamaState leader, follower;
    double target_x = 0.0, target_y = 0.0, target_z = 0.0, speed = 0.0;
    int leader_slot, follower_slot;
    gm_mobs_init(&mobs, 0);
    CHECK(gm_mobs_spawn_llama_exact(
              &mobs, 940, 0.0, 100.0, 0.0,
              0.0, 0.0, 0.0, 0.0F, 20.0F, 0,
              20.0, 0.175, 0.5, 0, GM_HORSE_TAME,
              0, 0, 3, -1, 0, 0, 1) > 0
              && gm_mobs_spawn_llama_exact(
                  &mobs, 941, 5.0, 100.0, 0.0,
                  0.0, 0.0, 0.0, 0.0F, 20.0F, 0,
                  20.0, 0.175, 0.5, 0, GM_HORSE_TAME,
                  0, 1, 3, -1, 0, 0, 0) > 0,
          "stage leashed caravan leader and free follower");
    CHECK(gm_mobs_llama_caravan_try_join(&mobs, 941)
              && gm_mobs_get_llama_state(&mobs, 940, &leader)
              && gm_mobs_get_llama_state(&mobs, 941, &follower)
              && leader.caravan_tail_eid == 941
              && follower.caravan_head_eid == 940,
          "free llama joins a valid leashed caravan head");
    CHECK(gm_mobs_llama_caravan_step(
              &mobs, 941, &target_x, &target_y, &target_z, &speed)
              && target_x == 2.0 && target_y == 100.0 && target_z == 0.0
              && speed == 2.0999999046325684,
          "caravan update requests the exact two-block trailing target");

    leader_slot = gm_mobs_find_slot_by_eid(&mobs, 940);
    follower_slot = gm_mobs_find_slot_by_eid(&mobs, 941);
    CHECK(leader_slot > 0 && follower_slot > 0,
          "resolve caravan fixture slots");
    mobs.a.x[leader_slot] = mobs.b.x[leader_slot] = 0.0;
    mobs.a.x[follower_slot] = mobs.b.x[follower_slot] = 27.0;
    CHECK(gm_mobs_llama_caravan_step(
              &mobs, 941, &target_x, NULL, NULL, &speed)
              && speed == 2.519999885559082
              && target_x == 2.0
              && gm_mobs_get_llama_state(&mobs, 941, &follower)
              && follower.caravan_dist_counter == 40,
          "distant caravan accelerates once and arms forty-tick grace");
    mobs.a.x[follower_slot] = mobs.b.x[follower_slot] = 5.0;
    CHECK(gm_mobs_llama_caravan_step(
              &mobs, 941, NULL, NULL, NULL, NULL)
              && gm_mobs_get_llama_state(&mobs, 941, &follower)
              && follower.caravan_dist_counter == 39,
          "caravan grace counter ages again after returning inside range");
    mobs.a.x[follower_slot] = mobs.b.x[follower_slot] = 27.0;
    CHECK(gm_mobs_llama_caravan_step(
              &mobs, 941, NULL, NULL, NULL, &speed)
              && speed == 3.0239998626708982
              && gm_mobs_get_llama_state(&mobs, 941, &follower)
              && follower.caravan_dist_counter == 40,
          "second distant update accelerates past three without decrement");
    for (int tick = 0; tick < 40; ++tick)
        CHECK(gm_mobs_llama_caravan_step(
                  &mobs, 941, NULL, NULL, NULL, NULL),
              "overlong caravan remains active through grace countdown");
    CHECK(!gm_mobs_llama_caravan_step(
              &mobs, 941, NULL, NULL, NULL, NULL)
              && gm_mobs_get_llama_state(&mobs, 941, &follower)
              && follower.caravan_head_eid == -1
              && follower.caravan_dist_counter == 0
              && follower.caravan_speed == 2.1,
          "overlong caravan resets immediately after grace expires");
    CHECK(gm_mobs_llama_caravan_join(&mobs, 941, 940),
          "rejoin caravan for reset-state regression");
    mobs.llama_caravan_dist_counter[follower_slot] = 7;
    mobs.llama_leashed[leader_slot] = 0;
    CHECK(!gm_mobs_llama_caravan_step(
              &mobs, 941, NULL, NULL, NULL, NULL)
              && gm_mobs_get_llama_state(&mobs, 941, &follower)
              && follower.caravan_head_eid == -1
              && follower.caravan_dist_counter == 7
              && follower.caravan_speed == 2.1,
          "caravan reset preserves the private grace counter like Java");
    return 1;
}

static int spit_spawn_and_hit(void) {
    GmMobLive mobs;
    GmLlamaSpit expected, observed;
    GmLlamaState llama;
    GmMobEvent sound;
    GmMobParticleBatch particles;
    McSinTable sin_table;
    int target_slot;
    gm_mobs_init(&mobs, 0);
    mc_sin_table_init(&sin_table);
    CHECK(gm_mobs_spawn_llama_exact(
              &mobs, 950, 0.0, 100.0, 0.0,
              0.0, 0.0, 0.0, 0.0F, 20.0F, 0,
              20.0, 0.175, 0.5, 0, GM_HORSE_TAME,
              0, 2, 3, -1, 0, 0, 0) > 0
              && gm_mobs_spawn_exact(
                  &mobs, GM_MOB_COW, 951,
                  8.0, 100.0, 0.0, 0.0, 0.0, 0.0,
                  0.0F, 10.0F, 0, 0, 0, 0) > 0
              && gm_mobs_set_entity_random_state(
                  &mobs, 950, UINT64_C(0x123456789abc), 0, 0.0),
          "stage llama ranged target and exact RNG cursors");
    CHECK(gm_mobs_llama_spit_attack_exact(
              &mobs, 950, 951, 952,
              UINT64_C(0x23456789abcd), 0, 0.0,
              &sin_table, &expected)
              && expected.active && expected.eid == 952
              && expected.owner_eid == 950
              && expected.dimension == 0
              && expected.x == 0.0
              && expected.z == (double)(0.9F + 1.0F) * 0.5D
              && expected.y == 100.0 + (double)1.87F
                    - 0.10000000149011612D
              && gm_mobs_get_llama_state(&mobs, 950, &llama)
              && llama.did_spit
              && gm_mobs_event_count(&mobs) == 1
              && gm_mobs_event_get(&mobs, 0, &sound)
              && sound.data == GM_MOB_SOUND_LLAMA_SPIT
              && gm_mobs_particle_batch_count(&mobs) == 1
              && gm_mobs_particle_batch_get(&mobs, 0, &particles)
              && particles.particle_id == GM_PARTICLE_SPIT
              && particles.count == 7
              && particles.eid == expected.eid
              && double_bits(particles.particles[0].vx)
                  == double_bits(expected.vx * 0.4D)
              && double_bits(particles.particles[0].vy)
                  == double_bits(expected.vy)
              && double_bits(particles.particles[6].vz)
                  == double_bits(expected.vz),
          "llama spit queues launch, sound, and seven client particles");
    CHECK(gm_mobs_take_llama_spit(&mobs, &observed)
              && observed.eid == expected.eid
              && double_bits(observed.vx) == double_bits(expected.vx)
              && double_bits(observed.vy) == double_bits(expected.vy)
              && double_bits(observed.vz) == double_bits(expected.vz)
              && !gm_mobs_take_llama_spit(&mobs, &observed),
          "llama spit queue preserves exact heading and drains once");
    target_slot = gm_mobs_find_slot_by_eid(&mobs, 951);
    CHECK(target_slot > 0
              && gm_mobs_llama_spit_hit(
                  &mobs, target_slot, 950, NULL, NULL)
              && mobs.a.health[target_slot] == 9.0F
              && mobs.b.health[target_slot] == 9.0F,
          "llama spit applies one indirect projectile damage");
    return 1;
}

static int runtime_fixture_checkpoint(void) {
    GmRuntime runtime;
    GmRuntimeProjectile expected;
    GmAction idle;
    char path[160];
    int projectile_slot = -1;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    (void)mkdir(".tmp", 0700);
    snprintf(path, sizeof path,
             ".tmp/test_llama_runtime_checkpoint.%ld.bin", (long)getpid());
    CHECK(init_runtime(&runtime), "initialize llama runtime fixture");
    CHECK(gm_runtime_spawn_llama_fixture(
              &runtime, 960, 8.5, 100.0, 8.5,
              0.0, 0.0, 0.0, 0.0F, 20.0F, 1,
              20.0, 0.175, 0.5, 0, GM_HORSE_TAME,
              0, 2, 4, 12, 1, 0, 0, 0, 0, 0)
              && gm_runtime_set_mob_uuid(
                  &runtime, 960,
                  (int64_t)UINT64_C(0xf123456789abcdef),
                  (int64_t)UINT64_C(0x8123456789abcdef))
              && gm_runtime_spawn_llama_spit_fixture(
                  &runtime, 961, -1, 1,
                  (long long)UINT64_C(0xf123456789abcdef),
                  (long long)UINT64_C(0x8123456789abcdef),
                  10.0, 101.0, 8.5, 0.25, 0.125, -0.5,
                  153.0F, 11.0F, 17, 1)
              && gm_runtime_set_transient_entity_uuid(
                  &runtime, 961,
                  (int64_t)UINT64_C(0x7123456789abcdef),
                  (int64_t)UINT64_C(0x6123456789abcdef))
              && gm_runtime_write_checkpoint(&runtime, path),
          "restore exact llama and owner-pending spit, then checkpoint");
    gm_runtime_tick(&runtime, idle);
    for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i)
        if (runtime.projectiles[i].active
                && runtime.projectiles[i].eid == 961) {
            projectile_slot = i;
            break;
        }
    CHECK(projectile_slot >= 0
              && runtime.projectiles[projectile_slot].shooter_eid == 960
              && runtime.projectiles[projectile_slot].shooting_living
              && !runtime.projectiles[projectile_slot].shooter_uuid_pending
              && runtime.projectiles[projectile_slot].no_gravity
              && runtime.projectiles[projectile_slot].ticks_existed == 18,
          "first update resolves saved owner UUID and preserves no-gravity");
    expected = runtime.projectiles[projectile_slot];
    CHECK(gm_runtime_load_checkpoint(&runtime, path),
          "reload llama projectile checkpoint");
    gm_runtime_tick(&runtime, idle);
    projectile_slot = -1;
    for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i)
        if (runtime.projectiles[i].active
                && runtime.projectiles[i].eid == 961) {
            projectile_slot = i;
            break;
        }
    CHECK(projectile_slot >= 0
              && memcmp(&expected, &runtime.projectiles[projectile_slot],
                        sizeof expected) == 0,
          "native save/load resumes llama-spit continuation byte-exactly");
    (void)remove(path);
    gm_runtime_destroy(&runtime);
    return 1;
}

static int runtime_link_checkpoint(void) {
    GmRuntime runtime;
    GmAction idle;
    GmLlamaState leader, follower;
    char path[160];
    double player_x, player_y, player_z;
    int leader_slot, saw_lead = 0, saw_loaded_lead = 0;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    (void)mkdir(".tmp", 0700);
    snprintf(path, sizeof path,
             ".tmp/test_llama_links_checkpoint.%ld.bin", (long)getpid());
    CHECK(init_runtime(&runtime), "initialize llama link checkpoint world");
    player_x = runtime.player.ent.posX + (double)runtime.ox;
    player_y = runtime.player.ent.posY;
    player_z = runtime.player.ent.posZ + (double)runtime.oz;
    CHECK(gm_runtime_set_player_entity_id(&runtime, 0)
              && gm_runtime_spawn_llama_fixture(
              &runtime, 962, player_x + 2.0, player_y, player_z,
              0.0, 0.0, 0.0, 0.0F, 20.0F, 1,
              20.0, 0.175, 0.5, 0, GM_HORSE_TAME,
              0, 0, 1, -1, 0, 0, 0, 0, 0, 0)
              && gm_runtime_spawn_llama_fixture(
                  &runtime, 963, player_x + 4.0, player_y, player_z,
                  0.0, 0.0, 0.0, 0.0F, 20.0F, 1,
                  20.0, 0.175, 0.5, 0, GM_HORSE_TAME,
                  0, 0, 1, -1, 0, 0, 0, 0, 0, 0)
              && gm_runtime_restore_llama_links(
                  &runtime, 962, 1, runtime.player_entity_id,
                  -1, 963, 2.0999999046325684, 0)
              && gm_runtime_restore_llama_links(
                  &runtime, 963, 0, -1,
                  962, -1, 2.519999885559082, 17)
              && gm_runtime_write_checkpoint(&runtime, path),
          "restore player leash and reciprocal caravan before checkpoint");
    CHECK(gm_runtime_load_checkpoint(&runtime, path)
              && gm_mobs_get_llama_state(&runtime.mobs, 962, &leader)
              && gm_mobs_get_llama_state(&runtime.mobs, 963, &follower)
              && leader.leashed && leader.leash_holder_kind == 1
              && leader.leash_holder_eid == runtime.player_entity_id
              && leader.caravan_tail_eid == 963
              && follower.caravan_head_eid == 962
              && double_bits(follower.caravan_speed)
                  == double_bits(2.519999885559082)
              && follower.caravan_dist_counter == 17,
          "native save/load preserves exact leash and caravan task state");
    gm_runtime_tick(&runtime, idle);
    CHECK(gm_mobs_get_llama_state(&runtime.mobs, 962, &leader)
              && leader.leashed && leader.caravan_tail_eid == 963,
          "nearby restored player leash survives its first continuation");
    leader_slot = gm_mobs_find_slot_by_eid(&runtime.mobs, 962);
    CHECK(leader_slot > 0
              && gm_runtime_set_entity_id_cursor(&runtime, 5000),
          "prepare deterministic leash-break entity cursor");
    runtime.mobs.a.x[leader_slot] = player_x + 20.0;
    runtime.mobs.b.x[leader_slot] = player_x + 20.0;
    gm_runtime_tick(&runtime, idle);
    for (int item = 0; item < GM_LIVE_MAX; ++item)
        if (runtime.entities.ents[item].active
                && runtime.entities.ents[item].eid == 5000
                && runtime.entities.ents[item].item == 420
                && runtime.entities.ents[item].uuid_present)
            saw_lead = 1;
    for (int order = 0; order < runtime.loaded_entity_order_count; ++order)
        if (runtime.loaded_entity_order[order] == 5000)
            saw_loaded_lead = 1;
    CHECK(gm_mobs_get_llama_state(&runtime.mobs, 962, &leader)
              && !leader.leashed && leader.leash_holder_kind == 0
              && leader.leash_holder_eid == -1
              && saw_lead && saw_loaded_lead,
          "ten-block leash break drops and orders an exact lead entity");
    (void)remove(path);
    gm_runtime_destroy(&runtime);
    return 1;
}

static int llama_inventory_container(void) {
    GmRuntime runtime;
    GmLlamaState llama;
    CHECK(init_runtime(&runtime), "initialize llama inventory world");
    CHECK(gm_runtime_spawn_llama_fixture(
              &runtime, 970, 8.5, 100.0, 8.5,
              0.0, 0.0, 0.0, 0.0F, 20.0F, 0,
              20.0, 0.175, 0.5, 0, GM_HORSE_TAME,
              0, 1, 2, -1, 1, 0, 0, 0, 0, 0)
              && gm_runtime_open_horse_inventory(&runtime, 970)
              && runtime.container == 8,
          "tame adult llama opens ContainerHorseInventory");
    CHECK(gm_runtime_set_inventory(&runtime, 0, 171, 1, 4)
              && gm_container_click(
                  &runtime, 0, 0, CC_CLICK_QUICK_MOVE)
              && gm_runtime_set_inventory(&runtime, 1, 329, 1, 0)
              && gm_container_click(
                  &runtime, 1, 0, CC_CLICK_QUICK_MOVE)
              && gm_runtime_set_inventory(&runtime, 2, 264, 64, 0)
              && gm_container_click(
                  &runtime, 2, 0, CC_CLICK_QUICK_MOVE)
              && gm_mobs_get_llama_state(&runtime.mobs, 970, &llama)
              && llama.decor == 4
              && llama.horse.inventory[1].item == 171
              && llama.horse.inventory[2].item == 329
              && llama.horse.inventory[3].item == 264
              && llama.horse.inventory[3].count == 64,
          "llama shift-click prioritizes carpet then strength-bounded storage");
    gm_player_cursor_set(ic_mk(329, 1, 0));
    CHECK(!gm_container_click(
              &runtime, GMC_HORSE0, 0, CC_CLICK_PICKUP)
              && gm_player_cursor().item == 329
              && gm_mobs_get_llama_state(&runtime.mobs, 970, &llama)
              && isr_is_empty(&llama.horse.inventory[0]),
          "disabled llama saddle slot rejects direct pickup");
    gm_player_cursor_set(ic_mk(1, 1, 0));
    CHECK(!gm_container_click(
              &runtime, GMC_HORSE0 + 8, 0, CC_CLICK_PICKUP)
              && gm_player_cursor().item == 1,
          "strength two llama exposes exactly six chest slots");
    gm_player_cursor_set(ic_empty());
    gm_runtime_destroy(&runtime);
    return 1;
}

static int runtime_sound_mapping(void) {
    GmRuntime runtime;
    GmRuntimeSoundEvent sound;
    GmRuntimeParticleEvent particle;
    GmAction idle;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    CHECK(init_runtime(&runtime), "initialize llama sound world");
    CHECK(gm_runtime_spawn_llama_fixture(
              &runtime, 980, 8.5, 100.0, 8.5,
              0.0, 0.0, 0.0, 0.0F, 20.0F, 0,
              20.0, 0.175, 0.5, 0, GM_HORSE_TAME,
              0, 0, 2, -1, 0, 0, 0, 0, 0, 0)
              && gm_runtime_spawn_mob_fixture(
                  &runtime, GM_MOB_COW, 981, 16.5, 100.0, 8.5,
                  0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0)
              && gm_mobs_llama_spit_attack_exact(
                  &runtime.mobs, 980, 981, 982,
                  UINT64_C(0x23456789abcd), 0, 0.0,
                  &runtime.sin_table, NULL),
          "queue llama spit sound through live runtime");
    gm_runtime_tick(&runtime, idle);
    CHECK(gm_runtime_sound_event_count(&runtime) == 1
              && gm_runtime_sound_event_get(&runtime, 0, &sound)
              && sound.sound == GM_SOUND_LLAMA_SPIT
              && sound.category == GM_SOUND_CATEGORY_NEUTRAL
              && sound.eid == 980
              && gm_runtime_particle_event_count(&runtime) == 7
              && gm_runtime_particle_event_get(&runtime, 0, &particle)
              && particle.kind == GM_PARTICLE_SPIT
              && particle.entity_eid == 982,
          "llama sound and spawn-particle events reach the runtime");
    gm_runtime_destroy(&runtime);
    return 1;
}

static int decor_sound_and_passenger_pose(void) {
    GmMobLive mobs;
    GmMobEvent sound;
    GmRuntime runtime;
    GmAction idle;
    double player_x, player_y, player_z;
    double expected_x, expected_y, expected_z;
    int slot;
    gm_mobs_init(&mobs, 0);
    CHECK(gm_mobs_spawn_llama_exact(
              &mobs, 985, 0.5, 100.0, 0.5,
              0.0, 0.0, 0.0, 0.0F, 20.0F, 1,
              20.0, 0.175, 0.5, 0, GM_HORSE_TAME,
              0, 0, 3, -1, 0, 0, 0) > 0,
          "stage mature llama decor sound fixture");
    slot = gm_mobs_find_slot_by_eid(&mobs, 985);
    CHECK(slot > 0, "resolve mature llama decor slot");
    mobs.entity_ticks_existed[slot] = 21;
    CHECK(gm_mobs_set_horse_inventory(
              &mobs, 985, 1, ic_mk(171, 1, 6))
              && gm_mobs_event_count(&mobs) == 1
              && gm_mobs_event_get(&mobs, 0, &sound)
              && sound.data == GM_MOB_SOUND_LLAMA_SWAG
              && sound.volume == 0.5F && sound.pitch == 1.0F
              && gm_mobs_set_horse_inventory(
                  &mobs, 985, 1, ic_mk(171, 1, 6))
              && gm_mobs_set_horse_inventory(
                  &mobs, 985, 1, ic_empty())
              && gm_mobs_event_count(&mobs) == 1,
          "new mature carpet emits swag; same color and removal stay silent");

    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    CHECK(init_runtime(&runtime), "initialize mounted llama pose world");
    player_x = runtime.player.ent.posX + (double)runtime.ox;
    player_y = runtime.player.ent.posY;
    player_z = runtime.player.ent.posZ + (double)runtime.oz;
    CHECK(gm_runtime_spawn_llama_fixture(
              &runtime, 986, player_x, player_y, player_z,
              0.0, 0.0, 0.0, 90.0F, 20.0F, 0,
              20.0, 0.175, 0.5, 0, GM_HORSE_TAME,
              0, 0, 3, -1, 0, 0, 0, 0, 0, 0)
              && (slot = gm_mobs_find_slot_by_eid(
                  &runtime.mobs, 986)) > 0,
          "restore mature llama before inventory continuation");
    runtime.mobs.entity_ticks_existed[slot] = 21;
    CHECK(gm_runtime_set_horse_inventory(
              &runtime, 986, 1, ic_mk(171, 1, 3))
              && gm_mobs_event_count(&runtime.mobs) == 0,
          "cold inventory continuation suppresses Java change sounds");
    CHECK(gm_runtime_set_horse_inventory(
              &runtime, 986, 1, ic_empty())
              && gm_runtime_set_mob_no_ai(&runtime, 986, 0)
              && gm_mobs_horse_mount(&runtime.mobs, 986),
          "mount normal live llama");
    expected_x = player_x + (double)(0.3F * mc_sin(
        &runtime.sin_table, 90.0F * 0.017453292F));
    expected_y = player_y + (double)1.87F * 0.67D - 0.35D;
    expected_z = player_z - (double)(0.3F * mc_cos(
        &runtime.sin_table, 90.0F * 0.017453292F));
    gm_runtime_tick(&runtime, idle);
    CHECK(double_bits(runtime.player.ent.posX + runtime.ox)
                  == double_bits(expected_x)
              && double_bits(runtime.player.ent.posY)
                  == double_bits(expected_y)
              && double_bits(runtime.player.ent.posZ + runtime.oz)
                  == double_bits(expected_z),
          "llama passenger uses exact lateral and height*0.67 override");
    gm_runtime_destroy(&runtime);
    return 1;
}

static int death_loot_and_fall_audio(void) {
    GmMobLive mobs;
    GmLiveSim drops;
    GmMobDeathContext context;
    uint64_t math_seed;
    int next_eid;
    int slot;
    JavaRandom expected;
    int expected_leather;

    gm_mobs_init(&mobs, 0);
    memset(&drops, 0, sizeof drops);
    math_seed = UINT64_C(0x23456789abcd);
    next_eid = 1100;
    context = (GmMobDeathContext){1, &math_seed, &next_eid};
    slot = gm_mobs_spawn_llama_exact(
        &mobs, 1001, 0.5, 100.0, 0.5,
        0.0, 0.0, 0.0, 0.0F, 2.0F, 1,
        20.0, 0.175, 0.5, 0, GM_HORSE_TAME,
        0, 2, 3, 5, 1, 0, 0);
    CHECK(slot > 0
              && gm_mobs_set_entity_random_state(
                  &mobs, 1001, UINT64_C(0x123456789abc), 0, 0.0)
              && gm_mobs_set_horse_inventory(
                  &mobs, 1001, 1, ic_mk(171, 1, 5))
              && gm_mobs_set_horse_inventory(
                  &mobs, 1001, 2, ic_mk(403, 1, 0))
              && gm_mobs_set_horse_inventory(
                  &mobs, 1001, 7, ic_mk(264, 5, 0)),
          "stage chested strength-two llama death inventory");
    jrand_set_seed48(&expected, UINT64_C(0x123456789abc));
    (void)jrand_double(&expected);
    (void)jrand_double(&expected);
    (void)jrand_float(&expected);
    (void)jrand_float(&expected);
    (void)jrand_int_bound(&expected, 1);
    expected_leather = jrand_int_bound(&expected, 3);
    CHECK(expected_leather == 2
              && gm_mobs_source_arrow_hit(
                  &mobs, NULL, slot, 4.0, 4.0, 20.0F,
                  &drops, &context) == 2,
          "lethal llama arrow follows the Java-verified leather cursor");
    CHECK(drops.n_active == 5
              && find_drop(&drops, 171) >= 0
              && find_drop(&drops, 403) >= 0
              && find_drop(&drops, 264) >= 0
              && find_drop(&drops, 54) >= 0
              && find_drop(&drops, 334) >= 0
              && drops.ents[find_drop(&drops, 334)].count
                  == expected_leather,
          "llama leather, decor, storage, and chest all drop on death");

    gm_mobs_init(&mobs, 0);
    memset(&drops, 0, sizeof drops);
    math_seed = UINT64_C(0x3456789abcde);
    next_eid = 1200;
    context = (GmMobDeathContext){0, &math_seed, &next_eid};
    slot = gm_mobs_spawn_llama_exact(
        &mobs, 1002, 0.5, 100.0, 0.5,
        0.0, 0.0, 0.0, 0.0F, 2.0F, 1,
        20.0, 0.175, 0.5, 0, GM_HORSE_TAME,
        0, 1, 3, 4, 1, 0, 0);
    CHECK(slot > 0
              && gm_mobs_set_horse_inventory(
                  &mobs, 1002, 1, ic_mk(171, 1, 4))
              && gm_mobs_set_horse_inventory(
                  &mobs, 1002, 2, ic_mk(264, 3, 0))
              && gm_mobs_source_arrow_hit(
                  &mobs, NULL, slot, 4.0, 4.0, 20.0F,
                  &drops, &context) == 2,
          "kill a chested llama with doMobLoot disabled");
    CHECK(drops.n_active == 3
              && find_drop(&drops, 171) >= 0
              && find_drop(&drops, 264) >= 0
              && find_drop(&drops, 54) >= 0
              && find_drop(&drops, 334) < 0,
          "inventory and chest remain independent of doMobLoot");

    {
        GmRuntime runtime;
        GmAction idle;
        GmLlamaState llama;
        int saw_block_fall = 0;
        int saw_horse_land = 0;
        memset(&idle, 0, sizeof idle);
        idle.hotbar_sel = -1;
        CHECK(init_runtime(&runtime), "initialize llama fall-audio world");
        runtime.mobs_enabled = 1;
        runtime.controlled_mobs_enabled = 0;
        runtime.mobs.natural_spawning_enabled = 0;
        CHECK(gm_runtime_spawn_llama_fixture(
                  &runtime, 1003, 8.5, 4.25, 8.5,
                  0.0, -0.5, 0.0, 0.0F, 20.0F, 0,
                  20.0, 0.175, 0.5, 0, GM_HORSE_TAME,
                  0, 0, 3, -1, 0, 0, 0, 0, 0, 0)
                  && gm_runtime_set_mob_no_ai(&runtime, 1003, 0),
              "stage live falling llama");
        slot = gm_mobs_find_slot_by_eid(&runtime.mobs, 1003);
        CHECK(slot > 0, "resolve falling llama slot");
        (runtime.mobs.current ? &runtime.mobs.b : &runtime.mobs.a)
            ->on_ground[slot] = 0;
        runtime.mobs.entity_fall_distance[slot] = 10.0F;
        runtime.mobs.entity_box_valid[slot] = 0;
        gm_runtime_tick(&runtime, idle);
        for (int event = 0; event < gm_mobs_event_count(&runtime.mobs);
                ++event) {
            GmMobEvent observed;
            CHECK(gm_mobs_event_get(&runtime.mobs, event, &observed),
                  "read llama landing event");
            if (observed.data == GM_MOB_SOUND_BLOCK_GRASS_FALL)
                saw_block_fall = 1;
            if (observed.data == GM_MOB_SOUND_HORSE_LAND)
                saw_horse_land = 1;
        }
        CHECK(gm_mobs_get_llama_state(&runtime.mobs, 1003, &llama)
                  && llama.horse.health == 18.0F
                  && saw_block_fall && !saw_horse_land,
              "llama fall damages at half distance without horse land audio");
        gm_runtime_destroy(&runtime);
    }
    return 1;
}

static int runtime_ranged_ai(void) {
    GmRuntime runtime;
    GmAction idle;
    GmLlamaState llama;
    double player_x, player_y, player_z;
    int llama_slot;
    int spit_tick = -1;
    int spit_eid = -1;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    CHECK(init_runtime(&runtime), "initialize ordinary llama AI world");
    player_x = runtime.player.ent.posX + (double)runtime.ox;
    player_y = runtime.player.ent.posY;
    player_z = runtime.player.ent.posZ + (double)runtime.oz;
    CHECK(gm_runtime_spawn_llama_fixture(
              &runtime, 990, player_x + 8.0, player_y, player_z,
              0.0, 0.0, 0.0, 90.0F, 20.0F, 0,
              20.0, 0.175, 0.5, 0, 0,
              0, 0, 3, -1, 0, 0, 0, 0, 0, 0)
              && gm_runtime_set_mob_no_ai(&runtime, 990, 0),
          "spawn normal-AI llama near the player");
    llama_slot = gm_mobs_find_slot_by_eid(&runtime.mobs, 990);
    CHECK(llama_slot > 0 && runtime.restored_active_mobs_enabled,
          "normal llama fixture selects the ordinary live tick path");
    runtime.mobs.llama_attack_target_eid[llama_slot] = 0;
    for (int tick = 1; tick <= 45 && spit_tick < 0; ++tick) {
        gm_runtime_tick(&runtime, idle);
        for (int event = 0;
                event < gm_runtime_sound_event_count(&runtime); ++event) {
            GmRuntimeSoundEvent sound;
            if (gm_runtime_sound_event_get(&runtime, event, &sound)
                    && sound.sound == GM_SOUND_LLAMA_SPIT) {
                spit_tick = tick;
                break;
            }
        }
    }
    for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i)
        if (runtime.projectiles[i].active
                && runtime.projectiles[i].type == 11
                && runtime.projectiles[i].shooter_eid == 990) {
            spit_eid = runtime.projectiles[i].eid;
            break;
        }
    CHECK(spit_tick == 41
              && gm_mobs_get_llama_state(&runtime.mobs, 990, &llama)
              && llama.did_spit
              && spit_eid > 0,
          "ordinary ranged AI waits forty ticks then creates live spit");
    gm_runtime_tick(&runtime, idle);
    CHECK(gm_mobs_get_llama_state(&runtime.mobs, 990, &llama)
              && !llama.did_spit
              && runtime.mobs.llama_attack_target_eid[llama_slot] == -1,
          "didSpit clears the retaliatory target on the following tick");
    gm_runtime_destroy(&runtime);
    return 1;
}

static int runtime_wolf_defense_ai(void) {
    GmRuntime runtime;
    GmAction idle;
    double player_x, player_y, player_z;
    int llama_slot, tame_slot, wild_slot;
    int acquired_tick = -1;
    int spit_tick = -1;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    CHECK(init_runtime(&runtime), "initialize llama wolf-defense world");
    player_x = runtime.player.ent.posX + (double)runtime.ox;
    player_y = runtime.player.ent.posY;
    player_z = runtime.player.ent.posZ + (double)runtime.oz;
    for (int x = mc_floor(player_x + 7.0);
            x <= mc_floor(player_x + 13.0); ++x)
        gm_world_set_block(
            runtime.world, x, mc_floor(player_y - 0.2D),
            mc_floor(player_z), 2);
    CHECK(gm_runtime_spawn_llama_fixture(
              &runtime, 991, player_x + 8.0, player_y, player_z,
              0.0, 0.0, 0.0, 90.0F, 20.0F, 0,
              20.0, 0.175, 0.5, 0, 0,
              0, 0, 3, -1, 0, 0, 0, 0, 0, 0)
              && gm_runtime_spawn_mob_fixture(
                  &runtime, EW_TYPE_WOLF, 992,
                  player_x + 9.5, player_y, player_z,
                  0.0, 0.0, 0.0, 0.0F, 8.0F, 1, 0, 0, 0)
              && gm_runtime_spawn_mob_fixture(
                  &runtime, EW_TYPE_WOLF, 993,
                  player_x + 12.0, player_y, player_z,
                  0.0, 0.0, 0.0, 0.0F, 8.0F, 1, 0, 0, 0)
              && gm_runtime_set_mob_no_ai(&runtime, 991, 0),
          "spawn ordinary llama beside tame and wild wolves");
    llama_slot = gm_mobs_find_slot_by_eid(&runtime.mobs, 991);
    tame_slot = gm_mobs_find_slot_by_eid(&runtime.mobs, 992);
    wild_slot = gm_mobs_find_slot_by_eid(&runtime.mobs, 993);
    CHECK(llama_slot > 0 && tame_slot > 0 && wild_slot > 0,
          "resolve wolf-defense fixtures");
    runtime.mobs.tameable_tamed[tame_slot] = 1;
    for (int tick = 1; tick <= 300 && acquired_tick < 0; ++tick) {
        gm_runtime_tick(&runtime, idle);
        if (runtime.mobs.llama_attack_target_eid[llama_slot] > 0) {
            acquired_tick = tick;
            CHECK(runtime.mobs.llama_attack_target_eid[llama_slot] == 993
                      && runtime.mobs.llama_attack_target_kind[llama_slot]
                          == 2,
                  "defend task ignores the nearer tamed wolf");
        }
    }
    if (acquired_tick < 0) {
        const EwStore *store = runtime.mobs.current
            ? &runtime.mobs.b : &runtime.mobs.a;
        int wx = mc_floor(store->x[wild_slot]);
        int wy = mc_floor(store->y[wild_slot]);
        int wz = mc_floor(store->z[wild_slot]);
        fprintf(stderr,
            "wolf-defense diagnostic: llama=(%.3f,%.3f,%.3f) "
            "wild=(%.3f,%.3f,%.3f) blocks=%d/%d/%d ai=%d setup=%d\n",
            store->x[llama_slot], store->y[llama_slot],
            store->z[llama_slot], store->x[wild_slot],
            store->y[wild_slot], store->z[wild_slot],
            gm_world_block(runtime.world, wx, wy - 1, wz),
            gm_world_block(runtime.world, wx, wy, wz),
            gm_world_block(runtime.world, wx, wy + 1, wz),
            runtime.mobs.controlled_no_ai[llama_slot],
            runtime.mobs.sheep_ai_tick_count[llama_slot]);
    }
    CHECK(acquired_tick > 0,
          "defend task deterministically acquires the untamed wolf");
    for (int tick = 1; tick <= 60 && spit_tick < 0; ++tick) {
        gm_runtime_tick(&runtime, idle);
        for (int event = 0;
                event < gm_runtime_sound_event_count(&runtime); ++event) {
            GmRuntimeSoundEvent sound;
            if (gm_runtime_sound_event_get(&runtime, event, &sound)
                    && sound.sound == GM_SOUND_LLAMA_SPIT
                    && sound.eid == 991) {
                spit_tick = tick;
                break;
            }
        }
    }
    CHECK(spit_tick > 0
              && runtime.mobs.llama_attack_target_eid[llama_slot] == 993
              && runtime.mobs.llama_attack_target_kind[llama_slot] == 2,
          "wolf-defense target survives the llama didSpit reset boundary");
    runtime.mobs.tameable_tamed[wild_slot] = 1;
    gm_runtime_tick(&runtime, idle);
    CHECK(runtime.mobs.llama_attack_target_eid[llama_slot] == -1
              && runtime.mobs.llama_attack_target_kind[llama_slot] == 0,
          "defend task clears a wolf that becomes tamed");
    gm_runtime_destroy(&runtime);
    return 1;
}

static int projectile_material_boundaries(void) {
    GmRuntime runtime;
    GmAction idle;
    GmRuntimeProjectile *spit = NULL;
    int x, y, z;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    CHECK(init_runtime(&runtime), "initialize llama-spit material world");
    x = mc_floor(runtime.player.ent.posX + (double)runtime.ox) + 12;
    y = mc_floor(runtime.player.ent.posY) + 4;
    z = mc_floor(runtime.player.ent.posZ + (double)runtime.oz);
    gm_world_set_block_meta(runtime.world, x, y, z, 1, 0);
    CHECK(gm_runtime_spawn_llama_spit_fixture(
              &runtime, 999, -1, 0, 0, 0,
              (double)x + 0.05D, (double)y + 0.25D,
              (double)z + 0.5D, 0.0, 0.0, 0.0,
              0.0F, 0.0F, 0, 1),
          "stage a stationary spit straddling stone and air");
    gm_runtime_tick(&runtime, idle);
    for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i)
        if (runtime.projectiles[i].eid == 999) {
            spit = &runtime.projectiles[i];
            break;
        }
    CHECK(spit && spit->active,
          "Material.AIR scan preserves a spit with any overlapped air cell");
    spit->active = 0;
    gm_world_set_block_meta(runtime.world, x, y, z, 8, 0);
    CHECK(gm_runtime_spawn_llama_spit_fixture(
              &runtime, 1000, -1, 0, 0, 0,
              (double)x + 0.05D, (double)y + 0.25D,
              (double)z + 0.5D, 0.0, 0.0, 0.0,
              0.0F, 0.0F, 0, 1),
          "stage a spit straddling water and air");
    gm_runtime_tick(&runtime, idle);
    spit = NULL;
    for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i)
        if (runtime.projectiles[i].eid == 1000) {
            spit = &runtime.projectiles[i];
            break;
        }
    CHECK(spit && !spit->active,
          "pre-move handleWaterMovement retires a water-overlapping spit");
    gm_runtime_destroy(&runtime);
    return 1;
}

static GmRuntimeProjectile *runtime_projectile_by_eid(
        GmRuntime *runtime, int eid) {
    for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i)
        if (runtime->projectiles[i].eid == eid)
            return &runtime->projectiles[i];
    return NULL;
}

static GmLiveEnt *runtime_item_by_eid(GmRuntime *runtime, int eid) {
    for (int i = 0; i < GM_LIVE_MAX; ++i)
        if (runtime->entities.ents[i].eid == eid)
            return &runtime->entities.ents[i];
    return NULL;
}

static int projectile_entity_boundaries(void) {
    GmRuntime runtime;
    GmAction idle;
    double x, y, z;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;

    CHECK(init_runtime(&runtime), "initialize llama-spit item world");
    x = runtime.player.ent.posX + (double)runtime.ox + 12.0;
    y = runtime.player.ent.posY + 4.0;
    z = runtime.player.ent.posZ + (double)runtime.oz;
    CHECK(gm_runtime_spawn_item_fixture(
              &runtime, 1001, x, y, z,
              0.0, 0.0, 0.0, 1, 1, 0, 0, 32767, 1)
              && gm_runtime_spawn_llama_spit_fixture(
                  &runtime, 1002, 1200, 0, 0, 0,
                  x - 2.0, y + 0.1, z, 3.0, 0.0, 0.0,
                  90.0F, 0.0F, 0, 1),
          "stage owner-backed spit crossing an EntityItem");
    gm_runtime_tick(&runtime, idle);
    CHECK(runtime_projectile_by_eid(&runtime, 1002)
              && !runtime_projectile_by_eid(&runtime, 1002)->active
              && runtime_item_by_eid(&runtime, 1001)
              && runtime_item_by_eid(&runtime, 1001)->health == 4,
          "spit retires on and applies generic damage to EntityItem");
    gm_runtime_destroy(&runtime);

    CHECK(init_runtime(&runtime), "initialize llama-spit XP world");
    x = runtime.player.ent.posX + (double)runtime.ox + 12.0;
    y = runtime.player.ent.posY + 4.0;
    z = runtime.player.ent.posZ + (double)runtime.oz;
    CHECK(gm_runtime_spawn_xp_fixture(
              &runtime, x, y, z, 0.0, 0.0, 0.0,
              3, 1003, 0, 32767, 0, 0)
              && gm_runtime_spawn_llama_spit_fixture(
                  &runtime, 1004, 1200, 0, 0, 0,
                  x - 2.0, y + 0.1, z, 3.0, 0.0, 0.0,
                  90.0F, 0.0F, 0, 1),
          "stage owner-backed spit crossing an XP orb");
    gm_runtime_tick(&runtime, idle);
    CHECK(runtime_projectile_by_eid(&runtime, 1004)
              && !runtime_projectile_by_eid(&runtime, 1004)->active
              && runtime.mobs.xp_orbs[0].health == 4,
          "spit retires on and applies generic damage to XP orb");
    gm_runtime_destroy(&runtime);

    CHECK(init_runtime(&runtime), "initialize llama-spit armor-stand world");
    x = runtime.player.ent.posX + (double)runtime.ox + 12.0;
    y = runtime.player.ent.posY + 4.0;
    z = runtime.player.ent.posZ + (double)runtime.oz;
    CHECK(gm_runtime_spawn_armor_stand_fixture(
              &runtime, 1005, x, y, z,
              0.0, 0.0, 0.0, 0.0F, 0.0F, 20.0F,
              0, 1, 0, 0, 0, 0, 0, 0)
              && gm_runtime_spawn_llama_spit_fixture(
                  &runtime, 1006, 1200, 0, 0, 0,
                  x - 2.0, y + 0.5, z, 3.0, 0.0, 0.0,
                  90.0F, 0.0F, 0, 1),
          "stage owner-backed spit crossing an armor stand");
    gm_runtime_tick(&runtime, idle);
    CHECK(runtime_projectile_by_eid(&runtime, 1006)
              && !runtime_projectile_by_eid(&runtime, 1006)->active
              && runtime.armor_stands[0].active
              && runtime.armor_stands[0].health == 20.0F,
          "spit retires on armor stand while indirect-mob damage is rejected");
    gm_runtime_destroy(&runtime);

    CHECK(init_runtime(&runtime), "initialize llama-spit minecart world");
    x = runtime.player.ent.posX + (double)runtime.ox + 12.0;
    y = runtime.player.ent.posY + 4.0;
    z = runtime.player.ent.posZ + (double)runtime.oz;
    CHECK(gm_runtime_spawn_minecart_fixture(
              &runtime, GM_MINECART_RIDEABLE, 1007,
              x, y, z, 0.0, 0.0, 0.0, 0.0F)
              && gm_runtime_spawn_llama_spit_fixture(
                  &runtime, 1008, 1200, 0, 0, 0,
                  x - 2.0, y + 0.2, z, 3.0, 0.0, 0.0,
                  90.0F, 0.0F, 0, 1),
          "stage owner-backed spit crossing a minecart");
    gm_runtime_tick(&runtime, idle);
    CHECK(runtime_projectile_by_eid(&runtime, 1008)
              && !runtime_projectile_by_eid(&runtime, 1008)->active
              && runtime.minecarts[0].damage == 10.0F
              && runtime.minecarts[0].rolling_amplitude == 10
              && runtime.minecarts[0].rolling_direction == -1,
          "spit retires on minecart and applies its generic hurt state");
    gm_runtime_destroy(&runtime);

    CHECK(init_runtime(&runtime), "initialize llama-spit loaded-order world");
    x = runtime.player.ent.posX + (double)runtime.ox + 12.0;
    y = runtime.player.ent.posY + 4.0;
    z = runtime.player.ent.posZ + (double)runtime.oz;
    CHECK(gm_runtime_spawn_item_fixture(
              &runtime, 1009, x, y, z,
              0.0, 0.0, 0.0, 1, 1, 0, 0, 32767, 1)
              && gm_runtime_spawn_item_fixture(
                  &runtime, 1010, x, y, z,
                  0.0, 0.0, 0.0, 2, 1, 0, 0, 32767, 1),
          "stage coincident non-merging item targets");
    runtime.loaded_entity_order_count = 0;
    CHECK(gm_runtime_restore_loaded_entity_order(&runtime, 0, 1010)
              && gm_runtime_restore_loaded_entity_order(&runtime, 1, 1009)
              && gm_runtime_spawn_llama_spit_fixture(
                  &runtime, 1011, 1200, 0, 0, 0,
                  x - 2.0, y + 0.1, z, 3.0, 0.0, 0.0,
                  90.0F, 0.0F, 0, 1),
          "restore item order opposite the fixed pool slot order");
    gm_runtime_tick(&runtime, idle);
    CHECK(runtime_item_by_eid(&runtime, 1009)->health == 5
              && runtime_item_by_eid(&runtime, 1010)->health == 4,
          "equal-distance spit target follows restored loaded-entity order");
    gm_runtime_destroy(&runtime);
    return 1;
}

static int stage_owner_spit(
        GmRuntime *runtime, int spit_eid,
        double x, double y, double z) {
    return gm_runtime_spawn_llama_fixture(
            runtime, 7000, x - 20.0, y, z,
            0.0, 0.0, 0.0, 90.0F, 20.0F, 1,
            20.0, 0.175, 0.5, 0, GM_HORSE_TAME,
            0, 0, 3, -1, 0, 0, 0, 0, 0, 0)
        && gm_runtime_spawn_llama_spit_fixture(
            runtime, spit_eid, 7000, 0, 0, 0,
            x - 2.0, y, z, 3.0, 0.0, 0.0,
            90.0F, 0.0F, 0, 1);
}

static int represented_entity_collision_tail(void) {
    GmRuntime runtime;
    GmAction idle;
    GmRuntimeProjectile *target;
    double x, y, z;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;

    CHECK(init_runtime(&runtime), "initialize projectile-target spit world");
    x = runtime.player.ent.posX + runtime.ox + 12.0;
    y = runtime.player.ent.posY + 4.0;
    z = runtime.player.ent.posZ + runtime.oz;
    CHECK(gm_runtime_spawn_arrow_fixture(
              &runtime, 7002, x, y, z, 0.0, 0.0, 0.0, 1, 0)
              && stage_owner_spit(&runtime, 7001, x, y + 0.1, z),
          "stage llama spit crossing a generic projectile");
    gm_runtime_tick(&runtime, idle);
    CHECK((target = runtime_projectile_by_eid(&runtime, 7002))
              && target->active
              && !runtime_projectile_by_eid(&runtime, 7001)->active,
          "spit retires on arrow while Entity base damage leaves it active");
    gm_runtime_destroy(&runtime);

    CHECK(init_runtime(&runtime), "initialize fireball-target spit world");
    x = runtime.player.ent.posX + runtime.ox + 12.0;
    y = runtime.player.ent.posY + 4.0;
    z = runtime.player.ent.posZ + runtime.oz;
    CHECK(gm_runtime_spawn_small_fireball_fixture(
              &runtime, 7002, x, y, z,
              0.0, 0.0, 0.0, 0.0, 0.0, 0.0)
              && stage_owner_spit(&runtime, 7001, x, y + 0.1, z),
          "stage llama-owned spit crossing a small fireball");
    gm_runtime_tick(&runtime, idle);
    target = runtime_projectile_by_eid(&runtime, 7002);
    CHECK(target && target->active && target->shooter_eid == 0
              && !target->shooting_living
              && target->vx == 0.0 && target->vy == 0.0
              && target->vz == 0.0
              && !runtime_projectile_by_eid(&runtime, 7001)->active,
          "small fireball override rejects indirect projectile damage");
    gm_runtime_destroy(&runtime);

    CHECK(init_runtime(&runtime), "initialize wither-target spit world");
    x = runtime.player.ent.posX + runtime.ox + 12.0;
    y = runtime.player.ent.posY + 4.0;
    z = runtime.player.ent.posZ + runtime.oz;
    CHECK(gm_runtime_spawn_wither_fixture(
              &runtime, 7002, x, y, z,
              0.0, 0.0, 0.0, 0.0F, 0.0F, 0.0F,
              300.0F, 0, 0, 0, 0, 0, 0,
              UINT64_C(0x123456789abc), 0, 0.0)
              && stage_owner_spit(&runtime, 7001, x, y + 0.5, z),
          "stage llama-owned spit crossing a wither");
    runtime.withers[0].no_ai = 1;
    runtime.withers[0].no_gravity = 1;
    gm_runtime_tick(&runtime, idle);
    CHECK(runtime.withers[0].health < 300.0F
              && runtime.withers[0].revenge_eid == 7000
              && !runtime.withers[0].revenge_is_player
              && !runtime.withers[0].attacking_player
              && !runtime_projectile_by_eid(&runtime, 7001)->active,
          "spit applies armored living damage and llama revenge attribution");
    gm_runtime_destroy(&runtime);

    CHECK(init_runtime(&runtime), "initialize passive-entity spit world");
    x = runtime.player.ent.posX + runtime.ox + 12.0;
    y = runtime.player.ent.posY + 4.0;
    z = runtime.player.ent.posZ + runtime.oz;
    CHECK(gm_runtime_spawn_area_effect_cloud_fixture(
              &runtime, 7002, 0, x, y, z,
              0, 600, 10, 20, 0.5F, 0.0F, 0.0F, 0)
              && stage_owner_spit(&runtime, 7001, x, y + 0.1, z),
          "stage spit crossing an area-effect cloud");
    gm_runtime_tick(&runtime, idle);
    CHECK(runtime.area_effect_clouds[0].state.active
              && !runtime_projectile_by_eid(&runtime, 7001)->active,
          "spit retires on cloud without mutating base Entity damage state");
    gm_runtime_destroy(&runtime);

    CHECK(init_runtime(&runtime), "initialize falling-entity spit world");
    x = runtime.player.ent.posX + runtime.ox + 12.0;
    y = runtime.player.ent.posY + 4.0;
    z = runtime.player.ent.posZ + runtime.oz;
    CHECK(gm_runtime_spawn_falling_fixture(
              &runtime, 7002, 12, 0, 1, x, y, z,
              0.0, 0.0, 0.0, 1, 1)
              && stage_owner_spit(&runtime, 7001, x, y + 0.1, z),
          "stage spit crossing a falling block");
    gm_runtime_tick(&runtime, idle);
    CHECK(runtime.falling_blocks[0].active
              && !runtime_projectile_by_eid(&runtime, 7001)->active,
          "spit retires on falling block without destroying it");
    gm_runtime_destroy(&runtime);

    CHECK(init_runtime(&runtime), "initialize TNT-entity spit world");
    x = runtime.player.ent.posX + runtime.ox + 12.0;
    y = runtime.player.ent.posY + 4.0;
    z = runtime.player.ent.posZ + runtime.oz;
    CHECK(gm_runtime_spawn_primed_tnt_fixture(
              &runtime, 7002, x, y, z, 0.0, 0.0, 0.0, 100)
              && stage_owner_spit(&runtime, 7001, x, y + 0.1, z),
          "stage spit crossing primed TNT");
    gm_runtime_tick(&runtime, idle);
    CHECK(runtime.primed_tnt[0].active && runtime.primed_tnt[0].fuse == 99
              && !runtime_projectile_by_eid(&runtime, 7001)->active,
          "spit retires on TNT while its ordinary fuse tick continues");
    gm_runtime_destroy(&runtime);

    CHECK(init_runtime(&runtime), "initialize crystal-target spit world");
    x = runtime.player.ent.posX + runtime.ox + 18.0;
    y = runtime.player.ent.posY + 8.0;
    z = runtime.player.ent.posZ + runtime.oz;
    CHECK(gm_runtime_spawn_end_crystal_fixture(
              &runtime, 7002, x, y, z, 0, 1, 0, 0, 0, 0)
              && stage_owner_spit(&runtime, 7001, x, y + 0.5, z),
          "stage llama-owned spit crossing an End crystal");
    gm_runtime_tick(&runtime, idle);
    CHECK(!runtime.end_crystals[0].active
              && runtime.end_crystal_count == 0
              && !runtime_projectile_by_eid(&runtime, 7001)->active,
          "spit destroys End crystal and runs its synchronous explosion");
    gm_runtime_destroy(&runtime);

    CHECK(init_runtime(&runtime), "initialize firework-target spit world");
    x = runtime.player.ent.posX + runtime.ox + 12.0;
    y = runtime.player.ent.posY + 4.0;
    z = runtime.player.ent.posZ + runtime.oz;
    runtime.next_entity_id = 7002;
    CHECK(gm_runtime_spawn_firework_payload(
              &runtime, x, y, z, 0, 0, 0, 0, 0) == 7002
              && stage_owner_spit(&runtime, 7001, x, y + 0.1, z),
          "stage spit crossing a firework rocket");
    runtime.fireworks[0].vx = 0.0;
    runtime.fireworks[0].vy = 0.0;
    runtime.fireworks[0].vz = 0.0;
    gm_runtime_tick(&runtime, idle);
    CHECK(runtime.fireworks[0].active
              && !runtime_projectile_by_eid(&runtime, 7001)->active,
          "spit retires on firework without damaging the rocket");
    gm_runtime_destroy(&runtime);

    CHECK(init_runtime(&runtime), "initialize fish-hook-target spit world");
    x = runtime.player.ent.posX + runtime.ox + 12.0;
    y = runtime.player.ent.posY + 4.0;
    z = runtime.player.ent.posZ + runtime.oz;
    isr_set_stack(
        &runtime.player.inv, runtime.player.inv.current_item,
        ic_mk(346, 1, 0));
    CHECK(gm_runtime_spawn_fish_hook_fixture(
              &runtime, 7002, x, y, z,
              0.0, 0.0, 0.0, 0.0F, 0.0F,
              0, 1, 0, 0, 0, 0, 0, 0.0F, 0, 0, 0,
              UINT64_C(0x123456789abc), 0, 0.0)
              && stage_owner_spit(&runtime, 7001, x, y + 0.05, z),
          "stage spit crossing a fishing hook");
    gm_runtime_tick(&runtime, idle);
    CHECK(runtime.fish_hook.active
              && !runtime_projectile_by_eid(&runtime, 7001)->active,
          "spit retires on fishing hook without retracting it");
    gm_runtime_destroy(&runtime);

    CHECK(init_runtime(&runtime), "initialize shulker-target spit world");
    x = floor(runtime.player.ent.posX + runtime.ox) + 12.5;
    y = floor(runtime.player.ent.posY) + 4.0;
    z = floor(runtime.player.ent.posZ + runtime.oz) + 0.5;
    gm_world_set_block(
        runtime.world, (int)floor(x), (int)y - 1, (int)floor(z), 1);
    CHECK(gm_runtime_spawn_shulker_fixture(
              &runtime, 7002, (int)floor(x), (int)y,
              (int)floor(z), 0, UINT64_C(0x123456789abc))
              && stage_owner_spit(&runtime, 7001, x, y + 0.5, z),
          "stage llama-owned spit crossing a shulker");
    runtime.shulkers[0].no_ai = 1;
    gm_runtime_tick(&runtime, idle);
    CHECK(runtime.shulkers[0].health < 30.0F
              && !runtime_projectile_by_eid(&runtime, 7001)->active,
          "spit applies closed-shell armor damage to shulker");
    gm_runtime_destroy(&runtime);

    CHECK(init_runtime(&runtime), "initialize shulker-bullet spit world");
    x = floor(runtime.player.ent.posX + runtime.ox) + 12.5;
    y = floor(runtime.player.ent.posY) + 4.0;
    z = floor(runtime.player.ent.posZ + runtime.oz) + 0.5;
    gm_world_set_block(
        runtime.world, (int)floor(x) - 8, (int)y - 1,
        (int)floor(z), 1);
    CHECK(gm_runtime_spawn_shulker_fixture(
              &runtime, 7003, (int)floor(x) - 8, (int)y,
              (int)floor(z), 0, UINT64_C(0x23456789abcd))
              && gm_runtime_spawn_shulker_bullet_state_fixture(
                  &runtime, 7002, 7003, 1, 20, 0,
                  x, y, z, 0.0, 0.0, 0.0,
                  0.0, 0.0, 0.0, 0.0F, 0.0F,
                  UINT64_C(0x123456789abc))
              && stage_owner_spit(&runtime, 7001, x, y + 0.1, z),
          "stage llama-owned spit crossing a shulker bullet");
    runtime.shulkers[0].no_ai = 1;
    gm_runtime_tick(&runtime, idle);
    CHECK(runtime.shulker_bullet_count == 0
              && !runtime.shulker_bullets[0].active
              && !runtime_projectile_by_eid(&runtime, 7001)->active,
          "spit destroys shulker bullet through its hurt event path");
    gm_runtime_destroy(&runtime);

    CHECK(init_runtime(&runtime), "initialize item-frame-target spit world");
    x = floor(runtime.player.ent.posX + runtime.ox) + 12.0;
    y = floor(runtime.player.ent.posY) + 4.5;
    z = floor(runtime.player.ent.posZ + runtime.oz) + 0.5;
    gm_world_set_block(
        runtime.world, (int)x + 1, (int)floor(y), (int)floor(z), 1);
    CHECK(gm_runtime_item_frame_set(
              &runtime, 0, 7002, x + 0.96875, y, z,
              (int)x, (int)floor(y), (int)floor(z), 4,
              1, 1, 0, 0)
              && stage_owner_spit(
                  &runtime, 7001, x + 0.96875, y, z),
          "stage llama-owned spit crossing a filled item frame");
    gm_runtime_tick(&runtime, idle);
    CHECK(runtime.item_frames && runtime.item_frames[0].active
              && runtime.item_frames[0].item == 0
              && !runtime_projectile_by_eid(&runtime, 7001)->active,
          "spit removes a displayed item without breaking its frame");
    gm_runtime_destroy(&runtime);
    return 1;
}

static int runtime_caravan_ai(void) {
    GmRuntime runtime;
    GmAction idle;
    GmLlamaState leader, follower;
    const EwStore *store;
    double player_x, player_y, player_z;
    double follower_x;
    int follower_slot;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    CHECK(init_runtime(&runtime), "initialize ordinary caravan AI world");
    player_x = runtime.player.ent.posX + (double)runtime.ox;
    player_y = runtime.player.ent.posY;
    player_z = runtime.player.ent.posZ + (double)runtime.oz;
    follower_x = player_x + 17.0;
    for (int x = mc_floor(follower_x) - 2;
            x <= mc_floor(follower_x) + 1; ++x)
        gm_world_set_block(
            runtime.world, x, mc_floor(player_y - 0.2D),
            mc_floor(player_z), 2);
    CHECK(gm_runtime_spawn_llama_fixture(
              &runtime, 995, player_x + 12.0, player_y, player_z,
              0.0, 0.0, 0.0, 0.0F, 20.0F, 0,
              20.0, 0.175, 0.5, 0, 0,
              0, 0, 3, -1, 0, 0, 1, 0, 0, 0)
              && gm_runtime_spawn_llama_fixture(
                  &runtime, 996, follower_x, player_y, player_z,
                  0.0, 0.0, 0.0, 0.0F, 20.0F, 0,
                  20.0, 0.175, 0.5, 0, 0,
                  0, 1, 3, -1, 0, 0, 0, 0, 0, 0)
              && gm_runtime_set_mob_no_ai(&runtime, 995, 0)
              && gm_runtime_set_mob_no_ai(&runtime, 996, 0),
          "spawn leashed head and normal-AI follower");
    follower_slot = gm_mobs_find_slot_by_eid(&runtime.mobs, 996);
    CHECK(follower_slot > 0, "resolve caravan follower before update");
    runtime.mobs.llama_step_distance[follower_slot] = 0.99F;
    gm_runtime_tick(&runtime, idle);
    follower_slot = gm_mobs_find_slot_by_eid(&runtime.mobs, 996);
    store = runtime.mobs.current ? &runtime.mobs.b : &runtime.mobs.a;
    CHECK(gm_mobs_get_llama_state(&runtime.mobs, 995, &leader)
              && gm_mobs_get_llama_state(&runtime.mobs, 996, &follower)
              && leader.caravan_tail_eid == 996
              && follower.caravan_head_eid == 995
              && follower_slot > 0 && store->x[follower_slot] < follower_x,
          "ordinary tick joins and advances a valid llama caravan");
    {
        int saw_step = 0;
        for (int tick = 0; tick < 5 && !saw_step; ++tick) {
            for (int event = 0;
                    event < gm_runtime_sound_event_count(&runtime); ++event) {
                GmRuntimeSoundEvent sound;
                if (gm_runtime_sound_event_get(&runtime, event, &sound)
                        && sound.sound == GM_SOUND_LLAMA_STEP
                        && sound.eid == 996
                        && sound.volume == 0.15F && sound.pitch == 1.0F)
                    saw_step = 1;
            }
            if (!saw_step) gm_runtime_tick(&runtime, idle);
        }
        CHECK(saw_step,
              "ordinary grounded movement emits exact llama step audio");
    }
    gm_runtime_destroy(&runtime);

    CHECK(init_runtime(&runtime),
          "initialize obstacle-aware caravan navigation world");
    player_x = floor(runtime.player.ent.posX + (double)runtime.ox) + 0.5D;
    player_y = floor(runtime.player.ent.posY);
    player_z = floor(runtime.player.ent.posZ + (double)runtime.oz) + 0.5D;
    for (int x = mc_floor(player_x) + 4;
            x <= mc_floor(player_x) + 26; ++x)
        for (int z = mc_floor(player_z) - 8;
                z <= mc_floor(player_z) + 8; ++z) {
            gm_world_load_block_meta(
                runtime.world, x, mc_floor(player_y) - 1, z, 1, 0);
            for (int y = mc_floor(player_y);
                    y <= mc_floor(player_y) + 3; ++y)
                gm_world_load_block_meta(runtime.world, x, y, z, 0, 0);
        }
    for (int z = mc_floor(player_z) - 2;
            z <= mc_floor(player_z) + 2; ++z)
        for (int y = mc_floor(player_y);
                y <= mc_floor(player_y) + 1; ++y)
            gm_world_load_block_meta(
                runtime.world, mc_floor(player_x) + 16, y, z, 1, 0);
    follower_x = player_x + 18.0D;
    CHECK(gm_runtime_spawn_llama_fixture(
              &runtime, 997, player_x + 12.0D, player_y, player_z,
              0.0, 0.0, 0.0, 0.0F, 20.0F, 0,
              20.0, 0.175, 0.5, 0, 0,
              0, 0, 3, -1, 0, 0, 1, 0, 0, 0)
              && gm_runtime_spawn_llama_fixture(
                  &runtime, 998, follower_x, player_y, player_z,
                  0.0, 0.0, 0.0, 0.0F, 20.0F, 0,
                  20.0, 0.175, 0.5, 0, 0,
                  0, 1, 3, -1, 0, 0, 0, 0, 0, 0)
              && gm_runtime_set_mob_no_ai(&runtime, 997, 0)
              && gm_runtime_set_mob_no_ai(&runtime, 998, 0),
          "spawn caravan separated by a two-high five-wide wall");
    gm_runtime_tick(&runtime, idle);
    follower_slot = gm_mobs_find_slot_by_eid(&runtime.mobs, 998);
    store = runtime.mobs.current ? &runtime.mobs.b : &runtime.mobs.a;
    CHECK(follower_slot > 0
              && runtime.mobs.llama_caravan_head_eid[follower_slot] > 0
              && store->path_len[follower_slot] == 1
              && fabs(store->path_tz[follower_slot] - player_z) > 1.0D,
          "caravan navigator selects a measured lateral waypoint around wall");
    for (int tick = 0; tick < 20; ++tick)
        gm_runtime_tick(&runtime, idle);
    follower_slot = gm_mobs_find_slot_by_eid(&runtime.mobs, 998);
    store = runtime.mobs.current ? &runtime.mobs.b : &runtime.mobs.a;
    CHECK(follower_slot > 0 && store->x[follower_slot] < follower_x
              && fabs(store->z[follower_slot] - player_z) > 0.1D,
          "caravan follower advances along the obstacle detour");
    gm_runtime_destroy(&runtime);
    return 1;
}

static int caravan_oracle_row(
        const char *name, double distance, int leader_leashed,
        double speed, int counter) {
    GmMobLive mobs;
    GmLlamaState follower;
    int active;
    gm_mobs_init(&mobs, 0);
    if (gm_mobs_spawn_llama_exact(
            &mobs, 940, 0.0, 100.0, 0.0,
            0.0, 0.0, 0.0, 0.0F, 20.0F, 0,
            20.0, 0.175, 0.5, 0, GM_HORSE_TAME,
            0, 0, 3, -1, 0, 0, leader_leashed) <= 0
            || gm_mobs_spawn_llama_exact(
                &mobs, 941, distance, 100.0, 0.0,
                0.0, 0.0, 0.0, 0.0F, 20.0F, 0,
                20.0, 0.175, 0.5, 0, GM_HORSE_TAME,
                0, 1, 3, -1, 0, 0, 0) <= 0
            || !gm_mobs_llama_caravan_join(&mobs, 941, 940))
        return 0;
    {
        int slot = gm_mobs_find_slot_by_eid(&mobs, 941);
        mobs.llama_caravan_speed[slot] = speed;
        mobs.llama_caravan_dist_counter[slot] = counter;
    }
    active = gm_mobs_llama_caravan_step(
        &mobs, 941, NULL, NULL, NULL, NULL);
    if (!gm_mobs_get_llama_state(&mobs, 941, &follower)) return 0;
    printf("%s %d %d %016llx %d\n", name, active,
           follower.caravan_head_eid,
           (unsigned long long)double_bits(follower.caravan_speed),
           follower.caravan_dist_counter);
    return 1;
}

static int caravan_oracle(void) {
    return caravan_oracle_row(
               "inside", 5.0, 1, 3.0239998626708982, 7)
        && caravan_oracle_row(
               "far_accel", 27.0, 1, 2.519999885559082, 5)
        && caravan_oracle_row(
               "far_grace", 27.0, 1, 3.0239998626708982, 1)
        && caravan_oracle_row(
               "far_expired", 27.0, 1, 3.0239998626708982, 0)
        && caravan_oracle_row(
               "reset_preserve", 5.0, 0, 3.0239998626708982, 7);
}

static int caravan_terrain_oracle(
        double player_x, double player_y, double player_z, int ticks,
        int dynamic_obstacles, int follow_parent) {
    GmRuntime runtime;
    GmAction idle;
    int center_x = mc_floor(player_x);
    int center_z = mc_floor(player_z);
    int ground_y = mc_floor(player_y) - 1;
    if (ticks <= 0 || ticks > 200 || !init_runtime(&runtime)) return 0;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    gm_runtime_set_pose_state(
        &runtime, player_x, player_y, player_z,
        0.0F, 0.0F, 0.0D, 0.0D, 0.0D, 1, 0.0F);
    runtime.randtick_enabled = 0;
    if (dynamic_obstacles)
        gm_runtime_set_total_time(&runtime, 100);
    for (int x = center_x + 2; x <= center_x + 20; ++x)
        for (int z = center_z - 8; z <= center_z + 8; ++z) {
            gm_world_load_block_meta(
                runtime.world, x, ground_y, z, 1, 0);
            for (int y = ground_y + 1; y <= ground_y + 4; ++y)
                gm_world_load_block_meta(runtime.world, x, y, z, 0, 0);
        }
    for (int z = center_z - 1; z <= center_z + 1; ++z)
        for (int y = ground_y + 1; y <= ground_y + 2; ++y)
            gm_world_load_block_meta(
                runtime.world, center_x + 12, y, z, 1, 0);
    CHECK(gm_runtime_spawn_llama_fixture(
            &runtime, 995,
            (double)center_x + 8.5D, (double)ground_y + 1.0D,
            (double)center_z + 0.5D,
            0.0D, 0.0D, 0.0D, 0.0F, 20.0F, 1,
            20.0D, 0.175D, 0.5D, 0, 0,
            0, 0, 1, -1, 0, 0, follow_parent ? 0 : 1, 0, 0, 0),
          "spawn terrain-oracle caravan leader");
    CHECK(gm_runtime_spawn_llama_fixture(
                &runtime, 996,
                (double)center_x + (follow_parent ? 17.0D : 14.5D),
                (double)ground_y + 1.0D,
                (double)center_z + 0.5D,
                0.0D, 0.0D, 0.0D, 0.0F, 20.0F, 0,
                20.0D, 0.175D, 0.5D, follow_parent ? -100 : 0, 0,
                0, 0, 1, -1, 0, 0, 0, 0, 0, 0),
          "spawn terrain-oracle caravan follower");
    if (!follow_parent) {
        CHECK(gm_runtime_restore_llama_links(
                    &runtime, 995, 1, runtime.player_entity_id,
                    -1, 996, 2.0999999046325684D, 0),
              "restore terrain-oracle caravan leader links");
        CHECK(gm_runtime_restore_llama_links(
                    &runtime, 996, 0, -1,
                    995, -1, 2.0999999046325684D, 0),
              "restore terrain-oracle caravan follower links");
    }
    CHECK(gm_mobs_set_entity_random_state(
              &runtime.mobs, 995, UINT64_C(0x123456789abc), 0, 0.0D),
          "seed terrain-oracle caravan leader");
    CHECK(gm_mobs_set_entity_random_state(
              &runtime.mobs, 996, UINT64_C(0x123456789abd), 0, 0.0D),
          "seed terrain-oracle caravan follower");
    CHECK(gm_runtime_set_mob_no_ai(&runtime, 995, 1)
              && gm_runtime_set_mob_no_ai(&runtime, 996, 0),
          "stage terrain-oracle leader NoAI and active follower");
    {
        int leader = gm_mobs_find_slot_by_eid(&runtime.mobs, 995);
        int follower = gm_mobs_find_slot_by_eid(&runtime.mobs, 996);
        if (leader <= 0 || follower <= 0) {
            gm_runtime_destroy(&runtime);
            return 0;
        }
        runtime.mobs.a.on_ground[leader] = 1;
        runtime.mobs.b.on_ground[leader] = 1;
        runtime.mobs.a.on_ground[follower] = 1;
        runtime.mobs.b.on_ground[follower] = 1;
        CHECK(runtime.mobs.controlled_no_ai[leader]
                  && !runtime.mobs.controlled_no_ai[follower],
              "retain terrain-oracle leader NoAI and active follower");
    }
    for (int tick = 0; tick < ticks; ++tick) {
        GmLlamaNavigationState navigation;
        const EwStore *store;
        int slot;
        if (dynamic_obstacles && tick == 3)
            gm_world_set_block(
                runtime.world, center_x + 20, ground_y + 1,
                center_z + 8, 1);
        if (dynamic_obstacles && tick == 4)
            gm_world_set_block(
                runtime.world, center_x + 12, ground_y + 2,
                center_z, 0);
        gm_runtime_tick(&runtime, idle);
        slot = gm_mobs_find_slot_by_eid(&runtime.mobs, 996);
        store = runtime.mobs.current ? &runtime.mobs.b : &runtime.mobs.a;
        if (slot <= 0) {
            fprintf(stderr,
                    "FAIL: terrain-oracle caravan follower missing at tick %d\n",
                    tick);
            gm_runtime_destroy(&runtime);
            return 0;
        }
        if (!gm_mobs_llama_navigation_state(
                &runtime.mobs, 996, &navigation)) {
            GmLlamaState leader_state, follower_state;
            int have_leader = gm_mobs_get_llama_state(
                &runtime.mobs, 995, &leader_state);
            int have_follower = gm_mobs_get_llama_state(
                &runtime.mobs, 996, &follower_state);
            fprintf(stderr,
                    "FAIL: terrain-oracle navigation missing at tick %d "
                    "leader=%d leash=%d follower=%d head=%d\n",
                    tick, have_leader,
                    have_leader ? leader_state.leashed : -1,
                    have_follower,
                    have_follower ? follower_state.caravan_head_eid : -2);
            gm_runtime_destroy(&runtime);
            return 0;
        }
        printf("{\"tick\":%d,\"target\":[%d,%d,%d],",
               tick, navigation.target_x, navigation.target_y,
               navigation.target_z);
        if (navigation.path_len <= 0) {
            printf("\"index\":null,\"points\":null");
        } else {
            printf("\"index\":%d,\"points\":[", navigation.path_index);
            for (int point = 0; point < navigation.path_len; ++point) {
                if (point) putchar(',');
                printf("[%d,%d,%d]",
                       navigation.points[point * 3],
                       navigation.points[point * 3 + 1],
                       navigation.points[point * 3 + 2]);
            }
            putchar(']');
        }
        printf(",\"task_mask\":%u,\"entity_seed48\":%llu,"
               "\"intent\":%d,\"nav_goal\":%d,"
               "\"on_ground\":%d,\"x\":\"%016llx\",\"y\":\"%016llx\","
               "\"z\":\"%016llx\",\"vx\":\"%016llx\","
               "\"vy\":\"%016llx\",\"vz\":\"%016llx\"}\n",
               gm_mobs_llama_task_mask(&runtime.mobs, 996),
               (unsigned long long)
                   runtime.mobs.entity_random[slot].random.seed,
               store->path_len[slot],
               runtime.mobs.llama_nav_goal_valid[slot] ? 1 : 0,
               store->on_ground[slot] ? 1 : 0,
               (unsigned long long)double_bits(store->x[slot]),
               (unsigned long long)double_bits(store->y[slot]),
               (unsigned long long)double_bits(store->z[slot]),
               (unsigned long long)double_bits(store->vx[slot]),
               (unsigned long long)double_bits(store->vy[slot]),
               (unsigned long long)double_bits(store->vz[slot]));
    }
    gm_runtime_destroy(&runtime);
    return 1;
}

static int caravan_task_conflict_row(
        const char *name, int ranged, int mating, int swimming,
        int follow_parent) {
    GmRuntime runtime;
    GmAction idle;
    double player_x, player_y, player_z;
    int follower_slot;
    if (!init_runtime(&runtime)) return 0;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    player_x = floor(runtime.player.ent.posX + (double)runtime.ox) + 0.5D;
    player_y = floor(runtime.player.ent.posY);
    player_z = floor(runtime.player.ent.posZ + (double)runtime.oz) + 0.5D;
    for (int x = mc_floor(player_x) + 4;
            x <= mc_floor(player_x) + 18; ++x)
        for (int z = mc_floor(player_z) - 3;
                z <= mc_floor(player_z) + 3; ++z)
            gm_world_load_block_meta(
                runtime.world, x, mc_floor(player_y) - 1, z, 1, 0);
    CHECK(gm_runtime_spawn_llama_fixture(
              &runtime, 1100, player_x + 8.0D, player_y, player_z,
              0.0D, 0.0D, 0.0D, 0.0F, 20.0F, 1,
              20.0D, 0.175D, 0.5D, 0, 0,
              0, 0, 1, -1, 0, 0, 1, 0, 0, 0)
              && gm_runtime_spawn_llama_fixture(
                  &runtime, 1101, player_x + 14.0D, player_y, player_z,
                  0.0D, 0.0D, 0.0D, 0.0F, 20.0F, 0,
                  20.0D, 0.175D, 0.5D, follow_parent ? -100 : 0, 0,
                  0, 0, 1, -1, 0, 0, 0, 0, 0, 0)
              && gm_runtime_restore_llama_links(
                  &runtime, 1100, 1, runtime.player_entity_id,
                  -1, 1101, 2.0999999046325684D, 0)
              && gm_runtime_restore_llama_links(
                  &runtime, 1101, 0, -1,
                  1100, -1, 2.0999999046325684D, 0)
              && gm_runtime_set_mob_no_ai(&runtime, 1100, 1)
              && gm_runtime_set_mob_no_ai(&runtime, 1101, 0),
          "stage native llama task-conflict caravan");
    follower_slot = gm_mobs_find_slot_by_eid(&runtime.mobs, 1101);
    CHECK(follower_slot > 0, "resolve native task-conflict follower");
    if (ranged) {
        runtime.mobs.llama_attack_target_eid[follower_slot] = 0;
        runtime.mobs.llama_attack_target_kind[follower_slot] = 1;
    }
    if (mating) {
        int mate_slot;
        CHECK(gm_runtime_spawn_llama_fixture(
                  &runtime, 1102, player_x + 15.5D, player_y, player_z,
                  0.0D, 0.0D, 0.0D, 0.0F, 20.0F, 1,
                  20.0D, 0.175D, 0.5D, 0, 0,
                  0, 0, 1, -1, 0, 0, 0, 0, 0, 0),
              "stage native task-conflict mate");
        mate_slot = gm_mobs_find_slot_by_eid(&runtime.mobs, 1102);
        CHECK(mate_slot > 0, "resolve native task-conflict mate");
        runtime.mobs.sheep_in_love[follower_slot] = 600;
        runtime.mobs.sheep_in_love[mate_slot] = 600;
    }
    if (swimming)
        gm_world_load_block_meta(
            runtime.world, mc_floor(player_x + 14.0D),
            mc_floor(player_y), mc_floor(player_z), 9, 0);
    gm_runtime_tick(&runtime, idle);
    printf("%s %u\n", name,
           gm_mobs_llama_task_mask(&runtime.mobs, 1101));
    gm_runtime_destroy(&runtime);
    return 1;
}

static int caravan_task_conflict_oracle(void) {
    return caravan_task_conflict_row(
               "caravan_ranged", 1, 0, 0, 0)
        && caravan_task_conflict_row(
               "caravan_mate", 0, 1, 0, 0)
        && caravan_task_conflict_row(
               "caravan_swim", 0, 0, 1, 0)
        && caravan_task_conflict_row(
               "caravan_follow_parent", 0, 0, 0, 1);
}

static int llama_lower_task_row(
        const char *name, uint64_t seed48, double offset_x,
        double player_x, double player_y, double player_z,
        unsigned int expected_mask) {
    GmRuntime runtime;
    GmAction idle;
    const EwStore *store;
    int slot;
    if (!init_runtime(&runtime)) return 0;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    player_x = floor(player_x) + 0.5D;
    player_y = floor(player_y);
    player_z = floor(player_z) + 0.5D;
    gm_runtime_set_pose_state(
        &runtime, player_x, player_y, player_z,
        0.0F, 0.0F, 0.0D, 0.0D, 0.0D, 1, 0.0F);
    for (int x = mc_floor(player_x) - 16;
            x <= mc_floor(player_x) + 28; ++x)
        for (int z = mc_floor(player_z) - 20;
                z <= mc_floor(player_z) + 20; ++z) {
            gm_world_load_block_meta(
                runtime.world, x, mc_floor(player_y) - 1, z, 1, 0);
            for (int y = mc_floor(player_y);
                    y <= mc_floor(player_y) + 4; ++y)
                gm_world_load_block_meta(runtime.world, x, y, z, 0, 0);
        }
    CHECK(gm_runtime_spawn_llama_fixture(
              &runtime, 1200, player_x + offset_x, player_y, player_z,
              0.0D, 0.0D, 0.0D, 0.0F, 20.0F, 0,
              20.0D, 0.175D, 0.5D, 0, 0,
              0, 0, 1, -1, 0, 0, 0, 0, 0, 0)
              && gm_mobs_set_entity_random_state(
                  &runtime.mobs, 1200, seed48, 0, 0.0D)
              && gm_runtime_set_mob_no_ai(&runtime, 1200, 0),
          "stage native exact llama lower task");
    slot = gm_mobs_find_slot_by_eid(&runtime.mobs, 1200);
    CHECK(slot > 0, "resolve native exact llama lower task");
    runtime.mobs.a.on_ground[slot] = 1;
    runtime.mobs.b.on_ground[slot] = 1;
    gm_runtime_tick(&runtime, idle);
    slot = gm_mobs_find_slot_by_eid(&runtime.mobs, 1200);
    store = runtime.mobs.current ? &runtime.mobs.b : &runtime.mobs.a;
    CHECK(slot > 0, "retain native exact llama lower task");
    CHECK(gm_mobs_llama_task_mask(&runtime.mobs, 1200) == expected_mask,
          "start native exact llama lower task");
    printf("%s %u %llu %016llx %016llx %016llx %016llx %016llx %016llx\n",
           name, gm_mobs_llama_task_mask(&runtime.mobs, 1200),
           (unsigned long long)
               runtime.mobs.entity_random[slot].random.seed,
           (unsigned long long)double_bits(store->x[slot]),
           (unsigned long long)double_bits(store->y[slot]),
           (unsigned long long)double_bits(store->z[slot]),
           (unsigned long long)double_bits(store->vx[slot]),
           (unsigned long long)double_bits(store->vy[slot]),
           (unsigned long long)double_bits(store->vz[slot]));
    gm_runtime_destroy(&runtime);
    return 1;
}

static int llama_lower_task_oracle(
        double player_x, double player_y, double player_z) {
    /* These raw java.util.Random cursors make exactly one lower-priority
     * goal start after EntityLiving's ambient roll, AbstractHorse's tail
     * roll, and AIDefendTarget's setup roll. */
    return llama_lower_task_row(
               "wander", UINT64_C(160), 8.0D,
               player_x, player_y, player_z, GM_LLAMA_TASK_WANDER)
        && llama_lower_task_row(
               "watch", UINT64_C(62), 4.0D,
               player_x, player_y, player_z, GM_LLAMA_TASK_WATCH)
        && llama_lower_task_row(
               "idle", UINT64_C(7), 8.0D,
               player_x, player_y, player_z, GM_LLAMA_TASK_IDLE);
}

static int llama_panic_terrain_oracle(
        double player_x, double player_y, double player_z,
        int ticks, int water_target) {
    GmRuntime runtime;
    GmAction idle;
    int center_x = mc_floor(player_x);
    int center_z = mc_floor(player_z);
    int ground_y = mc_floor(player_y) - 1;
    int slot;
    if (ticks <= 0 || ticks > 100 || !init_runtime(&runtime)) return 0;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    gm_runtime_set_pose_state(
        &runtime, center_x + 0.5D, ground_y + 1.0D, center_z + 0.5D,
        0.0F, 0.0F, 0.0D, 0.0D, 0.0D, 1, 0.0F);
    runtime.randtick_enabled = 0;
    for (int x = center_x - 16; x <= center_x + 28; ++x)
        for (int z = center_z - 20; z <= center_z + 20; ++z) {
            for (int y = ground_y - 4; y < ground_y; ++y)
                if (y > 0)
                    gm_world_load_block_meta(runtime.world, x, y, z, 0, 0);
            gm_world_load_block_meta(runtime.world, x, ground_y, z, 1, 0);
            for (int y = ground_y + 1; y <= ground_y + 5; ++y)
                gm_world_load_block_meta(runtime.world, x, y, z, 0, 0);
        }
    if (water_target)
        gm_world_load_block_meta(
            runtime.world, center_x + 13, ground_y + 1, center_z, 9, 0);
    CHECK(gm_runtime_spawn_llama_fixture(
              &runtime, 1300,
              center_x + 8.5D, ground_y + 1.0D, center_z + 0.5D,
              0.0D, 0.0D, 0.0D, 0.0F, 20.0F, 0,
              20.0D, 0.175D, 0.5D, 0, 0,
              0, 0, 1, -1, 0, 0, 0, 0, 0, 0)
              && gm_mobs_set_entity_random_state(
                  &runtime.mobs, 1300,
                  UINT64_C(0x123456789abd), 0, 0.0D)
              && gm_runtime_set_mob_no_ai(&runtime, 1300, 0),
          "stage native exact llama panic");
    slot = gm_mobs_find_slot_by_eid(&runtime.mobs, 1300);
    CHECK(slot > 0, "resolve native exact llama panic");
    runtime.mobs.a.on_ground[slot] = 1;
    runtime.mobs.b.on_ground[slot] = 1;
    /* Avoid the fire-damage modulo boundary so this fixture isolates panic
     * selection/navigation rather than hurt-sound RNG. */
    runtime.mobs.fire_ticks[slot] = 99;
    for (int tick = 0; tick < ticks; ++tick) {
        GmLlamaNavigationState navigation;
        const EwStore *store;
        gm_runtime_tick(&runtime, idle);
        slot = gm_mobs_find_slot_by_eid(&runtime.mobs, 1300);
        store = runtime.mobs.current ? &runtime.mobs.b : &runtime.mobs.a;
        CHECK(slot > 0 && gm_mobs_llama_navigation_state(
                  &runtime.mobs, 1300, &navigation),
              "retain native exact llama panic navigation");
        printf("{\"tick\":%d,\"target\":[%d,%d,%d],"
               "\"index\":%d,\"points\":[",
               tick, navigation.target_x, navigation.target_y,
               navigation.target_z, navigation.path_index);
        for (int point = 0; point < navigation.path_len; ++point) {
            if (point) putchar(',');
            printf("[%d,%d,%d]",
                   navigation.points[point * 3],
                   navigation.points[point * 3 + 1],
                   navigation.points[point * 3 + 2]);
        }
        printf("],\"task_mask\":%u,\"entity_seed48\":%llu,"
               "\"on_ground\":%d,\"x\":\"%016llx\","
               "\"y\":\"%016llx\",\"z\":\"%016llx\","
               "\"vx\":\"%016llx\",\"vy\":\"%016llx\","
               "\"vz\":\"%016llx\"}\n",
               gm_mobs_llama_task_mask(&runtime.mobs, 1300),
               (unsigned long long)
                   runtime.mobs.entity_random[slot].random.seed,
               store->on_ground[slot] ? 1 : 0,
               (unsigned long long)double_bits(store->x[slot]),
               (unsigned long long)double_bits(store->y[slot]),
               (unsigned long long)double_bits(store->z[slot]),
               (unsigned long long)double_bits(store->vx[slot]),
               (unsigned long long)double_bits(store->vy[slot]),
               (unsigned long long)double_bits(store->vz[slot]));
    }
    gm_runtime_destroy(&runtime);
    return 1;
}

static int llama_mate_terrain_oracle(
        double player_x, double player_y, double player_z, int ticks) {
    GmRuntime runtime;
    GmAction idle;
    int center_x = mc_floor(player_x);
    int center_z = mc_floor(player_z);
    int ground_y = mc_floor(player_y) - 1;
    int active_slot, mate_slot;
    if (ticks <= 0 || ticks > 100 || !init_runtime(&runtime)) return 0;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    gm_runtime_set_pose_state(
        &runtime, center_x + 0.5D, ground_y + 1.0D, center_z + 0.5D,
        0.0F, 0.0F, 0.0D, 0.0D, 0.0D, 1, 0.0F);
    runtime.randtick_enabled = 0;
    for (int x = center_x + 2; x <= center_x + 20; ++x)
        for (int z = center_z - 8; z <= center_z + 8; ++z) {
            gm_world_load_block_meta(runtime.world, x, ground_y, z, 1, 0);
            for (int y = ground_y + 1; y <= ground_y + 4; ++y)
                gm_world_load_block_meta(runtime.world, x, y, z, 0, 0);
        }
    for (int z = center_z - 1; z <= center_z + 1; ++z)
        for (int y = ground_y + 1; y <= ground_y + 2; ++y)
            gm_world_load_block_meta(
                runtime.world, center_x + 12, y, z, 1, 0);
    CHECK(gm_runtime_spawn_llama_fixture(
              &runtime, 1400,
              center_x + 8.5D, ground_y + 1.0D, center_z + 0.5D,
              0.0D, 0.0D, 0.0D, 0.0F, 20.0F, 1,
              20.0D, 0.175D, 0.5D, 0, GM_HORSE_TAME,
              0, 0, 1, -1, 0, 0, 0, 0, 0, 0)
              && gm_runtime_spawn_llama_fixture(
                  &runtime, 1401,
                  center_x + 17.0D, ground_y + 1.0D, center_z + 0.5D,
                  0.0D, 0.0D, 0.0D, 0.0F, 20.0F, 0,
                  20.0D, 0.175D, 0.5D, 0, GM_HORSE_TAME,
                  0, 0, 1, -1, 0, 0, 0, 0, 0, 0)
              && gm_mobs_set_entity_random_state(
                  &runtime.mobs, 1400,
                  UINT64_C(0x123456789abc), 0, 0.0D)
              && gm_mobs_set_entity_random_state(
                  &runtime.mobs, 1401,
                  UINT64_C(0x123456789abd), 0, 0.0D)
              && gm_runtime_set_mob_no_ai(&runtime, 1400, 1)
              && gm_runtime_set_mob_no_ai(&runtime, 1401, 0),
          "stage native exact llama mating");
    mate_slot = gm_mobs_find_slot_by_eid(&runtime.mobs, 1400);
    active_slot = gm_mobs_find_slot_by_eid(&runtime.mobs, 1401);
    CHECK(mate_slot > 0 && active_slot > 0,
          "resolve native exact llama mating");
    runtime.mobs.sheep_in_love[mate_slot] = 600;
    runtime.mobs.sheep_in_love[active_slot] = 600;
    runtime.mobs.a.on_ground[mate_slot] = 1;
    runtime.mobs.b.on_ground[mate_slot] = 1;
    runtime.mobs.a.on_ground[active_slot] = 1;
    runtime.mobs.b.on_ground[active_slot] = 1;
    for (int tick = 0; tick < ticks; ++tick) {
        GmLlamaNavigationState navigation;
        const EwStore *store;
        gm_runtime_tick(&runtime, idle);
        active_slot = gm_mobs_find_slot_by_eid(&runtime.mobs, 1401);
        store = runtime.mobs.current ? &runtime.mobs.b : &runtime.mobs.a;
        CHECK(active_slot > 0 && gm_mobs_llama_navigation_state(
                  &runtime.mobs, 1401, &navigation),
              "retain native exact llama mating navigation");
        printf("{\"tick\":%d,\"target\":[%d,%d,%d],"
               "\"index\":%d,\"points\":[",
               tick, navigation.target_x, navigation.target_y,
               navigation.target_z, navigation.path_index);
        for (int point = 0; point < navigation.path_len; ++point) {
            if (point) putchar(',');
            printf("[%d,%d,%d]",
                   navigation.points[point * 3],
                   navigation.points[point * 3 + 1],
                   navigation.points[point * 3 + 2]);
        }
        printf("],\"task_mask\":%u,\"entity_seed48\":%llu,"
               "\"on_ground\":%d,\"x\":\"%016llx\","
               "\"y\":\"%016llx\",\"z\":\"%016llx\","
               "\"vx\":\"%016llx\",\"vy\":\"%016llx\","
               "\"vz\":\"%016llx\"}\n",
               gm_mobs_llama_task_mask(&runtime.mobs, 1401),
               (unsigned long long)
                   runtime.mobs.entity_random[active_slot].random.seed,
               store->on_ground[active_slot] ? 1 : 0,
               (unsigned long long)double_bits(store->x[active_slot]),
               (unsigned long long)double_bits(store->y[active_slot]),
               (unsigned long long)double_bits(store->z[active_slot]),
               (unsigned long long)double_bits(store->vx[active_slot]),
               (unsigned long long)double_bits(store->vy[active_slot]),
               (unsigned long long)double_bits(store->vz[active_slot]));
    }
    gm_runtime_destroy(&runtime);
    return 1;
}

static int llama_ranged_terrain_oracle(
        double player_x, double player_y, double player_z, int ticks) {
    GmRuntime runtime;
    GmAction idle;
    int center_x = mc_floor(player_x);
    int center_z = mc_floor(player_z);
    int ground_y = mc_floor(player_y) - 1;
    int llama_slot, wolf_slot;
    if (ticks <= 0 || ticks > 100 || !init_runtime(&runtime)) return 0;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    gm_runtime_set_pose_state(
        &runtime, center_x + 0.5D, ground_y + 1.0D, center_z + 0.5D,
        0.0F, 0.0F, 0.0D, 0.0D, 0.0D, 1, 0.0F);
    runtime.randtick_enabled = 0;
    for (int x = center_x - 4; x <= center_x + 28; ++x)
        for (int z = center_z - 10; z <= center_z + 10; ++z) {
            for (int y = ground_y - 4; y <= ground_y + 5; ++y)
                if (y > 0)
                    gm_world_load_block_meta(runtime.world, x, y, z, 0, 0);
            if ((x != center_x + 5
                    || z < center_z - 1 || z > center_z + 1)
                    && (x != center_x + 1 || z != center_z + 1))
                gm_world_load_block_meta(
                    runtime.world, x, ground_y, z, 1, 0);
        }
    CHECK(gm_runtime_spawn_mob_fixture(
              &runtime, EW_TYPE_WOLF, 1500,
              center_x + 1.5D, ground_y + 1.0D, center_z + 0.5D,
              0.0D, 0.0D, 0.0D, 0.0F, 8.0F, 1, 0, 0, 0)
              && gm_runtime_spawn_llama_fixture(
                  &runtime, 1501,
                  center_x + 9.5D, ground_y + 1.0D, center_z + 0.5D,
                  0.0D, 0.0D, 0.0D, 0.0F, 20.0F, 0,
                  20.0D, 0.175D, 0.5D, 0, 0,
                  0, 0, 1, -1, 0, 0, 0, 0, 0, 0)
              && gm_mobs_set_entity_random_state(
                  &runtime.mobs, 1500,
                  UINT64_C(0x123456789abc), 0, 0.0D)
              && gm_mobs_set_entity_random_state(
                  &runtime.mobs, 1501, UINT64_C(6), 0, 0.0D)
              && gm_runtime_set_mob_no_ai(&runtime, 1500, 1)
              && gm_runtime_set_mob_no_ai(&runtime, 1501, 0),
          "stage native exact llama ranged navigation");
    wolf_slot = gm_mobs_find_slot_by_eid(&runtime.mobs, 1500);
    llama_slot = gm_mobs_find_slot_by_eid(&runtime.mobs, 1501);
    CHECK(wolf_slot > 0 && llama_slot > 0,
          "resolve native exact llama ranged navigation");
    runtime.mobs.a.on_ground[wolf_slot] = 1;
    runtime.mobs.b.on_ground[wolf_slot] = 1;
    runtime.mobs.a.on_ground[llama_slot] = 1;
    runtime.mobs.b.on_ground[llama_slot] = 1;
    for (int tick = 0; tick < ticks; ++tick) {
        GmLlamaNavigationState navigation;
        const EwStore *store;
        gm_runtime_tick(&runtime, idle);
        llama_slot = gm_mobs_find_slot_by_eid(&runtime.mobs, 1501);
        store = runtime.mobs.current ? &runtime.mobs.b : &runtime.mobs.a;
        CHECK(llama_slot > 0 && gm_mobs_llama_navigation_state(
                  &runtime.mobs, 1501, &navigation),
              "retain native exact llama ranged navigation");
        printf("{\"tick\":%d,\"target\":[%d,%d,%d],",
               tick, navigation.target_x, navigation.target_y,
               navigation.target_z);
        if (navigation.path_len <= 0) {
            printf("\"index\":null,\"points\":null");
        } else {
            printf("\"index\":%d,\"points\":[", navigation.path_index);
            for (int point = 0; point < navigation.path_len; ++point) {
                if (point) putchar(',');
                printf("[%d,%d,%d]",
                       navigation.points[point * 3],
                       navigation.points[point * 3 + 1],
                       navigation.points[point * 3 + 2]);
            }
            putchar(']');
        }
        printf(",\"task_mask\":%u,\"entity_seed48\":%llu,"
               "\"attack_time\":%d,\"see_time\":%d,"
               "\"did_spit\":%d,\"on_ground\":%d,"
               "\"x\":\"%016llx\",\"y\":\"%016llx\","
               "\"z\":\"%016llx\",\"vx\":\"%016llx\","
               "\"vy\":\"%016llx\",\"vz\":\"%016llx\"}\n",
               gm_mobs_llama_task_mask(&runtime.mobs, 1501),
               (unsigned long long)
                   runtime.mobs.entity_random[llama_slot].random.seed,
               runtime.mobs.llama_ranged_attack_time[llama_slot],
               runtime.mobs.llama_ranged_see_time[llama_slot],
               runtime.mobs.llama_did_spit[llama_slot] ? 1 : 0,
               store->on_ground[llama_slot] ? 1 : 0,
               (unsigned long long)double_bits(store->x[llama_slot]),
               (unsigned long long)double_bits(store->y[llama_slot]),
               (unsigned long long)double_bits(store->z[llama_slot]),
               (unsigned long long)double_bits(store->vx[llama_slot]),
               (unsigned long long)double_bits(store->vy[llama_slot]),
               (unsigned long long)double_bits(store->vz[llama_slot]));
    }
    gm_runtime_destroy(&runtime);
    return 1;
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--caravan-oracle"))
        return caravan_oracle() ? 0 : 1;
    if (argc == 6 && !strcmp(argv[1], "--caravan-terrain-oracle"))
        return caravan_terrain_oracle(
            strtod(argv[2], NULL), strtod(argv[3], NULL),
            strtod(argv[4], NULL), (int)strtol(argv[5], NULL, 10), 0, 0)
            ? 0 : 1;
    if (argc == 6 && !strcmp(argv[1], "--caravan-dynamic-oracle"))
        return caravan_terrain_oracle(
            strtod(argv[2], NULL), strtod(argv[3], NULL),
            strtod(argv[4], NULL), (int)strtol(argv[5], NULL, 10), 1, 0)
            ? 0 : 1;
    if (argc == 6 && !strcmp(argv[1], "--follow-parent-terrain-oracle"))
        return caravan_terrain_oracle(
            strtod(argv[2], NULL), strtod(argv[3], NULL),
            strtod(argv[4], NULL), (int)strtol(argv[5], NULL, 10), 0, 1)
            ? 0 : 1;
    if (argc == 2 && !strcmp(argv[1], "--caravan-task-conflicts"))
        return caravan_task_conflict_oracle() ? 0 : 1;
    if (argc == 5 && !strcmp(argv[1], "--llama-lower-tasks"))
        return llama_lower_task_oracle(
            strtod(argv[2], NULL), strtod(argv[3], NULL),
            strtod(argv[4], NULL)) ? 0 : 1;
    if (argc == 7 && !strcmp(argv[1], "--llama-panic-terrain"))
        return llama_panic_terrain_oracle(
            strtod(argv[2], NULL), strtod(argv[3], NULL),
            strtod(argv[4], NULL), (int)strtol(argv[5], NULL, 10),
            (int)strtol(argv[6], NULL, 10)) ? 0 : 1;
    if (argc == 6 && !strcmp(argv[1], "--llama-mate-terrain"))
        return llama_mate_terrain_oracle(
            strtod(argv[2], NULL), strtod(argv[3], NULL),
            strtod(argv[4], NULL), (int)strtol(argv[5], NULL, 10))
            ? 0 : 1;
    if (argc == 6 && !strcmp(argv[1], "--llama-ranged-terrain"))
        return llama_ranged_terrain_oracle(
            strtod(argv[2], NULL), strtod(argv[3], NULL),
            strtod(argv[4], NULL), (int)strtol(argv[5], NULL, 10))
            ? 0 : 1;
    if (!exact_state_and_inventory()) return 1;
    if (!chest_interaction()) return 1;
    if (!feeding_and_mount()) return 1;
    if (!genetics_and_mating()) return 1;
    if (!caravan_semantics()) return 1;
    if (!spit_spawn_and_hit()) return 1;
    if (!runtime_fixture_checkpoint()) return 1;
    if (!runtime_link_checkpoint()) return 1;
    if (!llama_inventory_container()) return 1;
    if (!runtime_sound_mapping()) return 1;
    if (!decor_sound_and_passenger_pose()) return 1;
    if (!death_loot_and_fall_audio()) return 1;
    if (!runtime_ranged_ai()) return 1;
    if (!runtime_wolf_defense_ai()) return 1;
    if (!runtime_caravan_ai()) return 1;
    if (!projectile_material_boundaries()) return 1;
    if (!projectile_entity_boundaries()) return 1;
    if (!represented_entity_collision_tail()) return 1;
    puts("llama runtime: PASS");
    return 0;
}
