#include "game/mob_live.h"

#include <stdio.h>

static int move_indoors(
        const char *name, GmWorld *world,
        int door_x, double villager_x) {
    GmMobLive mobs;
    JavaRandom initial;
    gm_mobs_init(&mobs, 0);
    int slot = gm_mobs_spawn_villager(
        &mobs, villager_x, 64.0D, 0.5D, 0);
    if (slot <= 0) return 0;
    const EwStore *state = mobs.current ? &mobs.b : &mobs.a;
    int eid = state->id[slot];
    jrand_set(&initial, 18);
    if (!gm_mobs_set_entity_random_state(
            &mobs, eid, initial.seed, 0, 0.0D)
            || !gm_mobs_set_villager_indoors_context(
                &mobs, eid, 1, door_x, 64, 0, 2, 0,
                1, door_x, 64, 0, 32))
        return 0;
    int started = gm_mobs_villager_move_indoors_task(
        &mobs, world, eid, 1, 1);
    state = mobs.current ? &mobs.b : &mobs.a;
    printf("I %s %d %.1f %.1f %.1f %d %llu\n",
        name, started, state->path_tx[slot], state->path_ty[slot],
        state->path_tz[slot],
        started && state->path_len[slot] != 0,
        (unsigned long long)mobs.entity_random[slot].random.seed);
    return 1;
}

static int move_restriction(GmWorld *world) {
    GmMobLive mobs;
    JavaRandom initial;
    gm_mobs_init(&mobs, 0);
    int slot = gm_mobs_spawn_villager(
        &mobs, 80.5D, 64.0D, 0.5D, 0);
    if (slot <= 0) return 0;
    const EwStore *state = mobs.current ? &mobs.b : &mobs.a;
    int eid = state->id[slot];
    jrand_set(&initial, 18);
    if (!gm_mobs_set_entity_random_state(
            &mobs, eid, initial.seed, 0, 0.0D)
            || !gm_mobs_set_villager_indoors_context(
                &mobs, eid, 0, 0, 0, 0, 0, 0,
                1, 30, 64, 0, 32))
        return 0;
    int started = gm_mobs_villager_move_restriction_task(
        &mobs, world, eid, 1);
    state = mobs.current ? &mobs.b : &mobs.a;
    printf("H %d %.1f %.1f %.1f %d %llu\n",
        started, state->path_tx[slot], state->path_ty[slot],
        state->path_tz[slot],
        started && state->path_len[slot] != 0,
        (unsigned long long)mobs.entity_random[slot].random.seed);
    return 1;
}

int main(void) {
    GmMobLive mobs;
    gm_mobs_init(&mobs, 0);
    int slot = gm_mobs_spawn_villager(&mobs, 1.5D, 64.0D, 0.5D, 0);
    if (slot <= 0) return 1;
    EwStore *now = mobs.current ? &mobs.b : &mobs.a;
    EwStore *next = mobs.current ? &mobs.a : &mobs.b;
    int eid = now->id[slot];
    if (!gm_mobs_set_villager_front_door(
            &mobs, eid, 1, 0, 0, 0, 64, 0, 2, 0, 0))
        return 1;
    int started = gm_mobs_villager_restrict_door_task(
        &mobs, eid, 0, 1);
    printf("S %d %d %d %d\n",
        started,
        mobs.villager_door_restriction_pending[slot] ? 1 : 0,
        mobs.villager_restrict_door_active[slot] ? 1 : 0,
        mobs.villager_enter_doors[slot] ? 1 : 0);

    now->x[slot] = next->x[slot] = -0.5D;
    int continued = gm_mobs_villager_restrict_door_task(
        &mobs, eid, 0, 0);
    printf("X %d %d\n",
        continued, mobs.villager_enter_doors[slot] ? 1 : 0);
    printf("D %d\n",
        gm_mobs_villager_restrict_door_task(&mobs, eid, 1, 1));

    GmWorld *world = gm_world_create(0);
    if (!world) return 1;
    gm_world_ensure(world, 0, 0, 5);
    gm_world_set_block_meta(world, 0, 64, 0, 64, 0);
    gm_world_set_block_meta(world, 0, 65, 0, 64, 8);
    now->path_tx[slot] = next->path_tx[slot] = 0.5D;
    now->path_ty[slot] = next->path_ty[slot] = 64.0D;
    now->path_tz[slot] = next->path_tz[slot] = 0.5D;
    now->path_len[slot] = next->path_len[slot] = 1;
    mobs.entity_collided_horizontal[slot] = 1;
    int open_continued = gm_mobs_villager_open_door_task(
        &mobs, world, eid, 1);
    GmMobEvent event = {0};
    int event_count = gm_mobs_event_count(&mobs);
    int world_event = event_count > 0
        && gm_mobs_event_get(&mobs, event_count - 1, &event)
        ? event.data : -1;
    printf("O %d %d %d %d\n",
        gm_world_meta(world, 0, 64, 0),
        mobs.villager_close_door_timer[slot],
        open_continued, world_event);
    for (int tick = 1; tick < 20; ++tick)
        open_continued = gm_mobs_villager_open_door_task(
            &mobs, world, eid, 0);
    (void)gm_mobs_villager_open_door_task(&mobs, world, eid, 0);
    event_count = gm_mobs_event_count(&mobs);
    world_event = event_count > 0
        && gm_mobs_event_get(&mobs, event_count - 1, &event)
        ? event.data : -1;
    printf("C %d %d %d %d\n",
        gm_world_meta(world, 0, 64, 0),
        mobs.villager_close_door_timer[slot],
        open_continued, world_event);
    /* Match MemoryWorld's air column and its first accepted stone supports. */
    gm_world_set_block(world, 6, 64, 3, 0);
    gm_world_set_block(world, 15, 60, -1, 1);
    if (!move_indoors("N", world, 0, 1.5D)
            || !move_indoors("F", world, 30, 0.5D)) {
        gm_world_destroy(world);
        return 1;
    }
    gm_world_set_block(world, 73, 58, 4, 1);
    if (!move_restriction(world)) {
        gm_world_destroy(world);
        return 1;
    }
    gm_world_destroy(world);
    return 0;
}
