#include "game/mob_live.h"

#include <stdint.h>
#include <stdio.h>

int main(void) {
    GmMobLive mobs;
    gm_mobs_init(&mobs, 0);
    int villager = gm_mobs_spawn_villager(&mobs, 0.5D, 64.0D, 0.5D, 0);
    int first = gm_mobs_spawn_iron_golem(&mobs, 1.5D, 64.0D, 0.5D, 0);
    int second = gm_mobs_spawn_iron_golem(&mobs, 1.5D, 64.0D, 0.5D, 0);
    if (villager <= 0 || first <= 0 || second <= 0) return 1;
    const EwStore *store = mobs.current ? &mobs.b : &mobs.a;
    int villager_eid = store->id[villager];
    int second_eid = store->id[second];
    if (!gm_mobs_set_growing_age(&mobs, villager_eid, -100)
            || !gm_mobs_set_iron_golem_state(
                &mobs, store->id[first], 0, 1, 0, 0)
            || !gm_mobs_set_iron_golem_state(
                &mobs, second_eid, 0, 1, 0, 400)
            || !gm_mobs_set_entity_random_state(
                &mobs, villager_eid,
                ((uint64_t)3107 ^ UINT64_C(0x5DEECE66D))
                    & ((UINT64_C(1) << 48) - 1),
                0, 0.0))
        return 1;

    int started = gm_mobs_villager_follow_golem_task(
        &mobs, villager_eid, 1, 1);
    printf("S %d %d %d %llu\n",
        started, mobs.villager_follow_golem_eid[villager] == second_eid,
        mobs.villager_take_golem_rose_tick[villager],
        (unsigned long long)mobs.entity_random[villager].random.seed);

    mobs.villager_took_golem_rose[villager] = 1;
    int active = gm_mobs_villager_follow_golem_task(
        &mobs, villager_eid, 1, 0);
    GmMobEvent event = {0};
    int status = gm_mobs_event_count(&mobs) > 0
        && gm_mobs_event_get(&mobs, gm_mobs_event_count(&mobs) - 1, &event)
        ? event.data : -1;
    printf("R %d %d %d\n",
        mobs.golem_hold_rose_tick[second], status, active);

    (void)gm_mobs_villager_follow_golem_task(
        &mobs, villager_eid, 1, 0);
    printf("X %d\n",
        mobs.villager_follow_golem_eid[villager] == -1 ? 1 : 0);
    return 0;
}
