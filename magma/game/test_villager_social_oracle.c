#include "game/mob_live.h"
#include "mc_math.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned long long dbits(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof bits);
    return (unsigned long long)bits;
}

static unsigned fbits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    return (unsigned)bits;
}

static int harvest(GmWorld *world, int crop, int meta, const char *label) {
    GmMobLive mobs;
    GmLiveSim drops;
    JavaRandom entity_random, world_random, block_random, math_random;
    int next_entity_id = 1000;
    gm_mobs_init(&mobs, 0);
    memset(&drops, 0, sizeof drops);
    int slot = gm_mobs_spawn_villager(
        &mobs, 0.5D, 64.0D, 0.5D, 0);
    const EwStore *state = mobs.current ? &mobs.b : &mobs.a;
    if (slot <= 0) return 0;
    int eid = state->id[slot];
    jrand_set(&entity_random, 18);
    jrand_set(&world_random, 1729);
    jrand_set(&block_random, 8191);
    jrand_set(&math_random, 65537);
    gm_world_set_block_meta(world, 0, 63, 0, 60, 0);
    gm_world_set_block_meta(world, 0, 64, 0, crop, meta);
    if (!gm_mobs_set_entity_random_state(
            &mobs, eid, entity_random.seed, 0, 0.0D))
        return 0;
    int started = gm_mobs_villager_harvest_task_exact(
        &mobs, world, eid, 1, 1,
        &world_random.seed, &block_random.seed, &math_random.seed,
        &next_entity_id, &drops);
    printf("D %s %d %d %d %llu %llu %llu %llu\n",
        label, started, gm_world_block(world, 0, 64, 0),
        drops.n_active,
        (unsigned long long)world_random.seed,
        (unsigned long long)block_random.seed,
        (unsigned long long)math_random.seed,
        (unsigned long long)mobs.entity_random[slot].random.seed);
    for (int i = 0; i < GM_LIVE_MAX; ++i) {
        const GmLiveEnt *item = &drops.ents[i];
        if (!item->active) continue;
        printf("E %d %016llx %016llx %016llx %016llx %016llx %016llx %08x %08x\n",
            item->item, dbits(item->x), dbits(item->y), dbits(item->z),
            dbits(item->mx), dbits(item->my), dbits(item->mz),
            fbits(item->yaw), fbits(item->hover_start));
    }
    return 1;
}

static int avoid(GmWorld *world) {
    GmMobLive mobs;
    JavaRandom random;
    gm_mobs_init(&mobs, 0);
    for (int x = -20; x <= 20; ++x)
        for (int z = -20; z <= 20; ++z)
            for (int y = 56; y <= 72; ++y)
                gm_world_load_block_meta(
                    world, x, y, z, y <= 63 ? 1 : 0, 0);
    int villager = gm_mobs_spawn_villager(
        &mobs, 0.5D, 64.0D, 0.5D, 0);
    int zombie = gm_mobs_spawn_sized(
        &mobs, EW_TYPE_ZOMBIE, 4.5D, 64.0D, 0.5D, 1);
    const EwStore *state = mobs.current ? &mobs.b : &mobs.a;
    if (villager <= 0 || zombie <= 0) return 0;
    jrand_set(&random, 1234);
    if (!gm_mobs_set_entity_random_state(
            &mobs, state->id[villager], random.seed, 0, 0.0D))
        return 0;
    int active = gm_mobs_villager_avoid_task(
        &mobs, world, state->id[villager], 1);
    state = mobs.current ? &mobs.b : &mobs.a;
    printf("A %d %016llx %016llx %016llx %llu\n",
        active, dbits(active ? state->path_tx[villager] : 0.0D),
        dbits(active ? state->path_ty[villager] : 0.0D),
        dbits(active ? state->path_tz[villager] : 0.0D),
        (unsigned long long)mobs.entity_random[villager].random.seed);
    return 1;
}

static int interact(void) {
    GmMobLive mobs;
    GmLiveSim drops;
    JavaRandom entity_random, math_random;
    McSinTable sin_table;
    int next_entity_id = 1000;
    gm_mobs_init(&mobs, 0);
    memset(&drops, 0, sizeof drops);
    mc_sin_table_init(&sin_table);
    int giver = gm_mobs_spawn_villager(
        &mobs, 0.5D, 64.0D, 0.5D, 0);
    int receiver = gm_mobs_spawn_villager(
        &mobs, 2.5D, 64.0D, 0.5D, 0);
    const EwStore *state = mobs.current ? &mobs.b : &mobs.a;
    if (giver <= 0 || receiver <= 0) return 0;
    int eid = state->id[giver];
    jrand_set(&entity_random, 5120);
    jrand_set(&math_random, 65537);
    if (!gm_mobs_set_villager_inventory_slot(
            &mobs, eid, 0, ic_mk(297, 8, 0))
            || !gm_mobs_set_entity_random_state(
                &mobs, eid, entity_random.seed, 0, 0.0D))
        return 0;
    int started = gm_mobs_villager_interact_task_exact(
        &mobs, eid, 1, &sin_table, &math_random.seed,
        &next_entity_id, &drops);
    for (int tick = 1; started && tick < 10; ++tick)
        gm_mobs_villager_interact_task_exact(
            &mobs, eid, 0, &sin_table, &math_random.seed,
            &next_entity_id, &drops);
    printf("I %d %d %d %d %08x %08x %llu %llu\n",
        started, mobs.villager_inventory[giver][0].count,
        drops.n_active,
        drops.n_active ? drops.ents[0].count : 0,
        fbits(mobs.passive_head_yaw[giver]),
        fbits(mobs.passive_head_pitch[giver]),
        (unsigned long long)math_random.seed,
        (unsigned long long)mobs.entity_random[giver].random.seed);
    for (int i = 0; i < GM_LIVE_MAX; ++i) {
        const GmLiveEnt *item = &drops.ents[i];
        if (!item->active) continue;
        printf("J %d %016llx %016llx %016llx %016llx %016llx %016llx %08x %08x\n",
            item->item, dbits(item->x), dbits(item->y), dbits(item->z),
            dbits(item->mx), dbits(item->my), dbits(item->mz),
            fbits(item->yaw), fbits(item->hover_start));
    }
    return 1;
}

int main(void) {
    GmMobLive mobs;
    JavaRandom initial;
    gm_mobs_init(&mobs, 0);
    GmWorld *world = gm_world_create(0);
    if (!world) return 1;
    gm_world_ensure(world, 0, 0, 2);
    int first = gm_mobs_spawn_villager(
        &mobs, 0.5D, 64.0D, 0.5D, 0);
    int second = gm_mobs_spawn_villager(
        &mobs, 5.5D, 64.0D, 0.5D, 0);
    const EwStore *state = mobs.current ? &mobs.b : &mobs.a;
    if (first <= 0 || second <= 0
            || !gm_mobs_set_growing_age(
                &mobs, state->id[first], -100)
            || !gm_mobs_set_growing_age(
                &mobs, state->id[second], -100)) {
        gm_world_destroy(world);
        return 1;
    }
    jrand_set(&initial, 465);
    if (!gm_mobs_set_entity_random_state(
            &mobs, state->id[first], initial.seed, 0, 0.0D)) {
        gm_world_destroy(world);
        return 1;
    }
    int started = gm_mobs_villager_play_task(
        &mobs, world, state->id[first], 1);
    state = mobs.current ? &mobs.b : &mobs.a;
    printf("P %d %d %d %d %.1f %.1f %.1f %llu\n",
        started, mobs.villager_playing[first] ? 1 : 0,
        mobs.villager_play_target_eid[first] == state->id[second],
        mobs.villager_play_time[first],
        state->path_tx[first], state->path_ty[first],
        state->path_tz[first],
        (unsigned long long)mobs.entity_random[first].random.seed);

    GmMobLive farmer;
    gm_mobs_init(&farmer, 0);
    int farm_slot = gm_mobs_spawn_villager(
        &farmer, 0.5D, 64.0D, 0.5D, 0);
    const EwStore *farm_state =
        farmer.current ? &farmer.b : &farmer.a;
    if (farm_slot <= 0) {
        gm_world_destroy(world);
        return 1;
    }
    int farm_eid = farm_state->id[farm_slot];
    ICStack seeds = ic_mk(295, 2, 0);
    jrand_set(&initial, 18);
    gm_world_set_block_meta(world, 0, 63, 0, 60, 0);
    gm_world_set_block_meta(world, 0, 64, 0, 0, 0);
    if (!gm_mobs_set_villager_inventory_slot(
            &farmer, farm_eid, 0, seeds)
            || !gm_mobs_set_entity_random_state(
                &farmer, farm_eid, initial.seed, 0, 0.0D)) {
        gm_world_destroy(world);
        return 1;
    }
    int farming = gm_mobs_villager_harvest_task(
        &farmer, world, farm_eid, 1, 1);
    printf("F %d %d %d %d %d %d %llu\n",
        farming, gm_world_block(world, 0, 64, 0),
        farmer.villager_inventory[farm_slot][0].count,
        farmer.villager_harvest_current_task[farm_slot],
        farmer.villager_harvest_run_delay[farm_slot],
        farmer.villager_harvest_timeout[farm_slot],
        (unsigned long long)farmer.entity_random[farm_slot].random.seed);
    if (!avoid(world)
            || !interact()
            || !harvest(world, 59, 7, "W")
            || !harvest(world, 141, 7, "C")
            || !harvest(world, 142, 7, "O")
            || !harvest(world, 207, 3, "B")) {
        gm_world_destroy(world);
        return 1;
    }
    gm_world_destroy(world);
    return 0;
}
