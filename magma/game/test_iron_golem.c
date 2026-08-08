#include "game/mob_live.h"
#include "entity_hostile_spine.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t fbits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static uint64_t dbits(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static EwStore *store(GmMobLive *m) {
    return m->current ? &m->b : &m->a;
}

int main(void) {
    GmMobLive mobs;
    JavaRandom random;
    int player_created, home_timer, attack_timer, rose_timer;
    gm_mobs_init(&mobs, 0);
    int golem = gm_mobs_spawn_iron_golem(&mobs, 0.5, 1.0, 0.5, 0);
    if (golem <= 0) return 1;
    int golem_eid = store(&mobs)->id[golem];
    float width, height;
    ehs_size(EW_TYPE_IRON_GOLEM, &width, &height);
    printf("A %08" PRIx32 " %08" PRIx32 " %08" PRIx32
           " %016" PRIx64 " %016" PRIx64 " %016" PRIx64 "\n",
        fbits(width), fbits(height), fbits(height * 0.85F),
        dbits((double)gm_mobs_max_health(&mobs, golem)),
        dbits((double)ehs_land_speed(EW_TYPE_IRON_GOLEM)), dbits(1.0));

    if (!gm_mobs_set_iron_golem_state(&mobs, golem_eid, 1, 0, 0, 0)
            || !gm_mobs_get_iron_golem_state(
                &mobs, golem_eid, &player_created, &home_timer,
                &attack_timer, &rose_timer))
        return 1;
    printf("N %d\n", player_created);

    jrand_set(&random, 3107);
    if (!gm_mobs_set_entity_random_state(
            &mobs, golem_eid, random.seed, 0, 0.0)
            || !gm_mobs_iron_golem_state_tick(&mobs, golem_eid)
            || !gm_mobs_get_iron_golem_state(
                &mobs, golem_eid, NULL, &home_timer, NULL, NULL))
        return 1;
    printf("H %d\n", home_timer);

    if (!gm_mobs_iron_golem_status(&mobs, golem_eid, 11)) return 1;
    if (!gm_mobs_get_iron_golem_state(
            &mobs, golem_eid, NULL, NULL, &attack_timer, &rose_timer))
        return 1;
    printf("R %d %d\n", attack_timer, rose_timer);

    int target = gm_mobs_spawn_iron_golem(&mobs, 1.5, 1.0, 0.5, 0);
    if (target <= 0) return 1;
    int target_eid = store(&mobs)->id[target];
    jrand_set(&random, 3107);
    if (!gm_mobs_set_entity_random_state(
            &mobs, golem_eid, random.seed, 0, 0.0))
        return 1;
    for (int hit = 0; hit < 6; ++hit) {
        float damage;
        int accepted = gm_mobs_iron_golem_attack(
            &mobs, golem_eid, target_eid, &damage);
        (void)damage;
        if (!gm_mobs_get_iron_golem_state(
                &mobs, golem_eid, NULL, NULL, &attack_timer, NULL))
            return 1;
        int last_status = 0;
        for (int event = 0; event < gm_mobs_event_count(&mobs); ++event) {
            GmMobEvent value;
            if (!gm_mobs_event_get(&mobs, event, &value)) return 1;
            if (value.kind == GM_MOB_EVENT_ENTITY_STATUS)
                last_status = value.data;
        }
        printf("T %d %08" PRIx32 " %016" PRIx64 " %d %d %d\n",
            hit, fbits(store(&mobs)->health[target]),
            dbits(store(&mobs)->vy[target]), accepted,
            attack_timer, last_status);
    }
    int statuses = 0;
    for (int event = 0; event < gm_mobs_event_count(&mobs); ++event) {
        GmMobEvent value;
        if (!gm_mobs_event_get(&mobs, event, &value)) return 1;
        if (value.kind == GM_MOB_EVENT_ENTITY_STATUS)
            ++statuses;
    }
    printf("C %d %" PRIu64 "\n", statuses,
        mobs.entity_random[golem].random.seed);
    return 0;
}
