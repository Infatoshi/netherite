#include "game/mob_live.h"

#include <stdio.h>

static EwStore *store(GmMobLive *m) {
    return m->current ? &m->b : &m->a;
}

int main(void) {
    GmMobLive mobs;
    JavaRandom random, world_random, math_random;
    gm_mobs_init(&mobs, 0);
    int first = gm_mobs_spawn_villager(&mobs, 0.5, 64.0, 0.5, 0);
    int second = gm_mobs_spawn_villager(&mobs, 1.5, 64.0, 0.5, 1);
    if (first <= 0 || second <= 0) return 1;
    int first_eid = store(&mobs)->id[first];
    int second_eid = store(&mobs)->id[second];
    if (!gm_mobs_set_villager_village_state(
            &mobs, first_eid, 1, 21, 2, 1)
            || !gm_mobs_set_villager_village_state(
                &mobs, second_eid, 1, 21, 2, 1)
            || !gm_mobs_set_villager_inventory_slot(
                &mobs, first_eid, 0, ic_mk(297, 3, 0))
            || !gm_mobs_set_villager_inventory_slot(
                &mobs, second_eid, 0, ic_mk(297, 3, 0)))
        return 1;
    jrand_set(&random, 61);
    if (!gm_mobs_set_entity_random_state(
            &mobs, first_eid, random.seed, 0, 0.0)
            || !gm_mobs_set_entity_random_state(
                &mobs, second_eid, random.seed, 0, 0.0))
        return 1;
    int first_started = gm_mobs_villager_mate_start(&mobs, first_eid);
    int second_started = gm_mobs_villager_mate_start(&mobs, second_eid);
    printf("S %d %d %d %d %d %d %d %d\n",
        first_started, second_started,
        mobs.villager_willing[first], mobs.villager_willing[second],
        mobs.villager_mating[first], mobs.villager_mating[second],
        mobs.villager_inventory[first][0].count,
        mobs.villager_inventory[second][0].count);

    jrand_set(&world_random, 1234);
    jrand_set(&math_random, 1234);
    int next_entity_id = mobs.next_id;
    for (int tick = 0; tick < 300; ++tick) {
        if (first_started && !gm_mobs_villager_mate_update(
                &mobs, first_eid, &world_random.seed,
                &math_random.seed, &next_entity_id))
            first_started = 0;
        if (second_started && !gm_mobs_villager_mate_update(
                &mobs, second_eid, &world_random.seed,
                &math_random.seed, &next_entity_id))
            second_started = 0;
    }
    EwStore *entities = store(&mobs);
    int child = -1;
    for (int slot = 1; slot < entities->count; ++slot)
        if (entities->alive[slot]
                && entities->type[slot] == EW_TYPE_VILLAGER
                && entities->id[slot] != first_eid
                && entities->id[slot] != second_eid) {
            child = slot;
            break;
        }
    int status12 = 0, status18 = 0;
    for (int event = 0; event < gm_mobs_event_count(&mobs); ++event) {
        GmMobEvent value;
        if (!gm_mobs_event_get(&mobs, event, &value)) return 1;
        if (value.kind != GM_MOB_EVENT_ENTITY_STATUS) continue;
        if (value.data == 12) ++status12;
        if (value.data == 18) ++status18;
    }
    printf("B %d %d %d %d %d %d %d %d\n",
        mobs.growing_age[first], mobs.growing_age[second],
        mobs.villager_willing[first], mobs.villager_willing[second],
        child > 0 ? mobs.growing_age[child] : 0,
        child > 0 ? mobs.villager_profession[child] : -1,
        status12, status18);
    return child > 0 ? 0 : 1;
}
