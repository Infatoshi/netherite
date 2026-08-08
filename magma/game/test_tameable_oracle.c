#include "game/mob_live.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "tameable check failed at line %d: %s\n", \
            __LINE__, #c); return 1; } } while (0)

static unsigned fbits(float value) {
    union { float f; uint32_t u; } bits = {value};
    return bits.u;
}

static int emit(const char *name, GmMobLive *m, int eid,
                IsrInv *inventory, int handled, int creative,
                const JavaRandom *world_random) {
    int tamed, sitting, owner, variant;
    float health;
    int status = -1, particles = 0;
    GmMobEvent event;
    GmMobParticleBatch batch;
    CHECK(gm_mobs_get_tameable_state(
        m, eid, &tamed, &sitting, &owner, &variant, &health));
    if (gm_mobs_event_count(m) > 0
            && gm_mobs_event_get(
                m, gm_mobs_event_count(m) - 1, &event))
        status = event.data;
    if (gm_mobs_particle_batch_count(m) > 0
            && gm_mobs_particle_batch_get(
                m, gm_mobs_particle_batch_count(m) - 1, &batch)) {
        particles = batch.count;
        CHECK(batch.particle_id == (status == 7 ? 34 : 11));
    }
    ICStack held = isr_get_stack(inventory, 0);
    int slot = 1;
    printf("%s %d %d %d %d %d %08x %d %d %d %012llx %012llx\n",
        name, handled, held.count > 0 ? held.count : 0,
        tamed, sitting, variant, fbits(health), status, particles, creative,
        (unsigned long long)m->entity_random[slot].random.seed,
        (unsigned long long)world_random->seed);
    return 0;
}

static int wolf_tame(const char *name, uint64_t seed48, int creative) {
    GmMobLive mobs;
    IsrInv inventory;
    JavaRandom world_random;
    memset(&inventory, 0, sizeof inventory);
    gm_mobs_init(&mobs, 0);
    int slot = gm_mobs_spawn(&mobs, EW_TYPE_WOLF, 1.0, 64.0, 1.0);
    CHECK(slot == 1);
    int eid = mobs.a.id[slot];
    CHECK(gm_mobs_set_entity_random_state(&mobs, eid, seed48, 0, 0.0));
    jrand_set_seed48(&world_random, UINT64_C(0x123456789abc));
    isr_set_stack(&inventory, 0, ic_mk(352, 2, 0));
    int handled = gm_mobs_tameable_interact(
        &mobs, eid, &inventory, 0, creative, 0, &world_random);
    CHECK(emit(name, &mobs, eid, &inventory,
               handled, creative, &world_random) == 0);
    return 0;
}

static int wolf_owned(void) {
    GmMobLive mobs;
    IsrInv inventory;
    JavaRandom world_random;
    memset(&inventory, 0, sizeof inventory);
    gm_mobs_init(&mobs, 0);
    int slot = gm_mobs_spawn(&mobs, EW_TYPE_WOLF, 0.0, 0.0, 0.0);
    CHECK(slot == 1);
    int eid = mobs.a.id[slot];
    CHECK(gm_mobs_set_tameable_state(
        &mobs, eid, 1, 0, 1, 1, 11.0F));
    CHECK(gm_mobs_set_entity_random_state(
        &mobs, eid, UINT64_C(0x102030405060), 0, 0.0));
    jrand_set_seed48(&world_random, UINT64_C(0x123456789abc));

    isr_set_stack(&inventory, 0, ic_mk(319, 2, 0));
    int handled = gm_mobs_tameable_interact(
        &mobs, eid, &inventory, 0, 0, 1, &world_random);
    CHECK(emit("WH", &mobs, eid, &inventory,
               handled, 0, &world_random) == 0);

    isr_set_stack(&inventory, 0, ic_mk(351, 2, 4));
    handled = gm_mobs_tameable_interact(
        &mobs, eid, &inventory, 0, 0, 1, &world_random);
    CHECK(emit("WD", &mobs, eid, &inventory,
               handled, 0, &world_random) == 0);

    isr_set_stack(&inventory, 0, ic_mk(280, 2, 0));
    handled = gm_mobs_tameable_interact(
        &mobs, eid, &inventory, 0, 0, 1, &world_random);
    CHECK(emit("WS", &mobs, eid, &inventory,
               handled, 0, &world_random) == 0);
    return 0;
}

static int ocelot_tame(const char *name, uint64_t seed48, int creative) {
    GmMobLive mobs;
    IsrInv inventory;
    JavaRandom world_random;
    memset(&inventory, 0, sizeof inventory);
    gm_mobs_init(&mobs, 0);
    int slot = gm_mobs_spawn(&mobs, EW_TYPE_OCELOT, 1.0, 64.0, 1.0);
    CHECK(slot == 1);
    int eid = mobs.a.id[slot];
    CHECK(gm_mobs_set_entity_random_state(&mobs, eid, seed48, 0, 0.0));
    jrand_set_seed48(&world_random, UINT64_C(0x23456789abcd));
    isr_set_stack(&inventory, 0, ic_mk(349, 2, 0));
    int handled = gm_mobs_tameable_interact(
        &mobs, eid, &inventory, 0, creative, 0, &world_random);
    CHECK(emit(name, &mobs, eid, &inventory,
               handled, creative, &world_random) == 0);
    return 0;
}

static int emit_breed(
        const char *name, GmMobLive *m, int eid, IsrInv *inventory,
        int handled) {
    int age, in_love, status = -1;
    GmMobEvent event;
    int slot = 1;
    CHECK(gm_mobs_get_animal_breeding_state(
        m, eid, &age, &in_love, NULL, NULL, NULL));
    if (gm_mobs_event_count(m) > 0
            && gm_mobs_event_get(
                m, gm_mobs_event_count(m) - 1, &event))
        status = event.data;
    ICStack held = isr_get_stack(inventory, 0);
    printf("%s %d %d %d %d %d %012llx %08x %08x\n",
        name, handled, held.count > 0 ? held.count : 0,
        age, in_love, status,
        (unsigned long long)m->entity_random[slot].random.seed,
        fbits((m->current ? &m->b : &m->a)->health[slot]),
        fbits(gm_mobs_max_health(m, slot)));
    return 0;
}

static int breeding(void) {
    GmMobLive mobs;
    IsrInv inventory;
    JavaRandom world_random;
    int slot, eid, handled;

    memset(&inventory, 0, sizeof inventory);
    gm_mobs_init(&mobs, 0);
    slot = gm_mobs_spawn(&mobs, EW_TYPE_WOLF, 0.0, 0.0, 0.0);
    CHECK(slot == 1);
    eid = mobs.a.id[slot];
    CHECK(gm_mobs_set_tameable_state(
        &mobs, eid, 1, 0, 1, 1, 20.0F));
    CHECK(gm_mobs_set_entity_random_state(
        &mobs, eid, UINT64_C(0x102030405060), 0, 0.0));
    jrand_set_seed48(&world_random, UINT64_C(0x123456789abc));
    isr_set_stack(&inventory, 0, ic_mk(319, 2, 0));
    handled = gm_mobs_tameable_interact(
        &mobs, eid, &inventory, 0, 0, 1, &world_random);
    CHECK(emit_breed("WB", &mobs, eid, &inventory, handled) == 0);

    memset(&inventory, 0, sizeof inventory);
    gm_mobs_init(&mobs, 0);
    slot = gm_mobs_spawn(&mobs, EW_TYPE_OCELOT, 0.0, 0.0, 0.0);
    CHECK(slot == 1);
    eid = mobs.a.id[slot];
    CHECK(gm_mobs_set_tameable_state(
        &mobs, eid, 1, 0, 1, 2, 10.0F));
    CHECK(gm_mobs_set_entity_random_state(
        &mobs, eid, UINT64_C(0x102030405060), 0, 0.0));
    jrand_set_seed48(&world_random, UINT64_C(0x23456789abcd));
    isr_set_stack(&inventory, 0, ic_mk(349, 2, 0));
    handled = gm_mobs_tameable_interact(
        &mobs, eid, &inventory, 0, 0, 1, &world_random);
    if (!handled)
        handled = gm_mobs_feed_animal(
            &mobs, eid, &inventory, 0, 0);
    CHECK(emit_breed("OB", &mobs, eid, &inventory, handled) == 0);
    return 0;
}

int main(void) {
    CHECK(wolf_tame("W0", 0, 0) == 0);
    CHECK(wolf_tame("W1", 1, 0) == 0);
    CHECK(wolf_tame("WC", 0, 1) == 0);
    CHECK(wolf_owned() == 0);
    CHECK(ocelot_tame("O0", 0, 0) == 0);
    CHECK(ocelot_tame("O1", 1, 0) == 0);
    CHECK(ocelot_tame("OC", 0, 1) == 0);
    CHECK(breeding() == 0);
    return 0;
}
