#include "game/mob_live.h"

#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_int(const char *text, int *out) {
    char *end = NULL;
    long value = strtol(text, &end, 0);
    if (!text[0] || !end || *end || value < INT_MIN || value > INT_MAX)
        return 0;
    *out = (int)value;
    return 1;
}

static int parse_species(const char *text, int *type) {
    if (!strcmp(text, "sheep")) *type = EW_TYPE_SHEEP;
    else if (!strcmp(text, "cow")) *type = EW_TYPE_COW;
    else if (!strcmp(text, "pig")) *type = EW_TYPE_PIG;
    else if (!strcmp(text, "chicken")) *type = EW_TYPE_CHICKEN;
    else return 0;
    return 1;
}

static int parse_u48(const char *text, uint64_t *out) {
    char *end = NULL;
    unsigned long long value = strtoull(text, &end, 0);
    if (!text[0] || !end || *end || value >= (UINT64_C(1) << 48))
        return 0;
    *out = (uint64_t)value;
    return 1;
}

static int parse_double(const char *text, double *out) {
    char *end = NULL;
    double value = strtod(text, &end);
    if (!text[0] || !end || *end || !isfinite(value)) return 0;
    *out = value;
    return 1;
}

static uint64_t double_bits(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static uint32_t float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static const EwStore *store(const GmMobLive *m) {
    return m->current ? &m->b : &m->a;
}

int main(int argc, char **argv) {
    int first_color, second_color, first_age, second_age;
    int type, first_love, second_love, updates, next_entity_id, do_mob_loot;
    double distance, x, y, z;
    int child_have_next_gaussian, child_time_until_next_egg;
    double child_next_gaussian;
    uint64_t world_seed48, first_seed48, second_seed48, math_seed48;
    uint64_t child_seed48;
    if (argc != 23 || !parse_species(argv[1], &type)
            || !parse_int(argv[2], &first_color)
            || !parse_int(argv[3], &second_color)
            || !parse_int(argv[4], &first_age)
            || !parse_int(argv[5], &second_age)
            || !parse_int(argv[6], &first_love)
            || !parse_int(argv[7], &second_love)
            || !parse_int(argv[8], &updates)
            || !parse_double(argv[9], &distance)
            || !parse_double(argv[10], &x)
            || !parse_double(argv[11], &y)
            || !parse_double(argv[12], &z)
            || !parse_u48(argv[13], &world_seed48)
            || !parse_u48(argv[14], &first_seed48)
            || !parse_u48(argv[15], &second_seed48)
            || !parse_u48(argv[16], &math_seed48)
            || !parse_int(argv[17], &next_entity_id)
            || !parse_int(argv[18], &do_mob_loot)
            || !parse_u48(argv[19], &child_seed48)
            || !parse_int(argv[20], &child_have_next_gaussian)
            || !parse_double(argv[21], &child_next_gaussian)
            || !parse_int(argv[22], &child_time_until_next_egg)
            || first_color < 0 || first_color > 15
            || second_color < 0 || second_color > 15
            || first_love < 0 || first_love > 600
            || second_love < 0 || second_love > 600
            || updates < 0 || updates > 60
            || distance < 0.0 || distance > 8.0
            || y < 1.0 || y > 254.0
            || next_entity_id <= 0 || next_entity_id >= INT_MAX - 4
            || (do_mob_loot != 0 && do_mob_loot != 1)
            || (child_have_next_gaussian != 0
                && child_have_next_gaussian != 1)) {
        fprintf(stderr, "usage: %s sheep|cow|pig|chicken "
            "FIRST_COLOR SECOND_COLOR FIRST_AGE "
            "SECOND_AGE FIRST_LOVE SECOND_LOVE UPDATES DISTANCE X Y Z "
            "WORLD_SEED48 FIRST_SEED48 SECOND_SEED48 MATH_SEED48 "
            "NEXT_ENTITY_ID DO_MOB_LOOT CHILD_SEED48 "
            "CHILD_HAVE_NEXT_GAUSSIAN CHILD_NEXT_GAUSSIAN "
            "CHILD_TIME_UNTIL_NEXT_EGG\n", argv[0]);
        return 2;
    }

    int first_eid = next_entity_id++;
    int second_eid = next_entity_id++;
    GmMobLive mobs;
    gm_mobs_init(&mobs, 0);
    int first_slot = gm_mobs_spawn_exact(
        &mobs, type, first_eid, x, y, z,
        0.0, 0.0, 0.0, 0.0F,
        type == EW_TYPE_SHEEP ? 8.0F
            : type == EW_TYPE_CHICKEN ? 4.0F : 10.0F,
        1, 0, 0, 0);
    int second_slot = gm_mobs_spawn_exact(
        &mobs, type, second_eid, x + distance, y, z,
        0.0, 0.0, 0.0, 0.0F,
        type == EW_TYPE_SHEEP ? 8.0F
            : type == EW_TYPE_CHICKEN ? 4.0F : 10.0F,
        1, 0, 0, 0);
    if (first_slot < 0 || second_slot < 0
            || (type == EW_TYPE_SHEEP
                && (!gm_mobs_set_sheep_state(
                        &mobs, first_eid, first_color, 0)
                    || !gm_mobs_set_sheep_state(
                        &mobs, second_eid, second_color, 0)))
            || !gm_mobs_set_growing_age(&mobs, first_eid, first_age)
            || !gm_mobs_set_growing_age(&mobs, second_eid, second_age)
            || !gm_mobs_set_animal_breeding_state(
                &mobs, first_eid, first_love, 0, 0, 0)
            || !gm_mobs_set_animal_breeding_state(
                &mobs, second_eid, second_love, 0, 0, 0)
            || !gm_mobs_set_entity_random_state(
                &mobs, first_eid, first_seed48, 0, 0.0)
            || !gm_mobs_set_entity_random_state(
                &mobs, second_eid, second_seed48, 0, 0.0)
            || !gm_mobs_set_next_animal_child_state(
                &mobs, child_seed48, child_have_next_gaussian,
                child_next_gaussian, child_time_until_next_egg))
        return 1;

    int should_execute = first_love > 0 && second_love > 0;
    int delay = 0;
    if (should_execute) {
        for (int update = 0; update < updates; ++update) {
            GmSheepMateResult result;
            (void)gm_mobs_animal_mate_update(
                &mobs, first_eid, second_eid, &delay, 0, 1,
                &world_seed48, &math_seed48, &next_entity_id,
                do_mob_loot, &result);
        }
    }

    int got_first_age, got_second_age, got_first_love, got_second_love;
    if (!gm_mobs_get_animal_breeding_state(
            &mobs, first_eid, &got_first_age, &got_first_love,
            NULL, NULL, NULL)
            || !gm_mobs_get_animal_breeding_state(
                &mobs, second_eid, &got_second_age, &got_second_love,
                NULL, NULL, NULL))
        return 1;
    const EwStore *s = store(&mobs);

    printf("{\"ok\":true,\"species\":\"%s\","
           "\"should_execute\":%s,\"updates\":%d,"
           "\"first_eid\":%d,\"second_eid\":%d,"
           "\"first_growing_age\":%d,\"second_growing_age\":%d,"
           "\"first_in_love\":%d,\"second_in_love\":%d,"
           "\"first_entity_seed48\":%" PRIu64 ","
           "\"second_entity_seed48\":%" PRIu64 ","
           "\"first_entity_have_next_gaussian\":%s,"
           "\"first_entity_next_gaussian_bits\":\"%016" PRIx64 "\","
           "\"children\":[",
        argv[1], should_execute ? "true" : "false", updates,
        first_eid, second_eid, got_first_age, got_second_age,
        got_first_love, got_second_love,
        (uint64_t)mobs.entity_random[first_slot].random.seed,
        (uint64_t)mobs.entity_random[second_slot].random.seed,
        mobs.entity_random[first_slot].have_next_next_gaussian
            ? "true" : "false",
        double_bits(mobs.entity_random[first_slot].next_next_gaussian));

    int child_count = 0;
    for (int slot = 1; slot < EW_MAX_ENTITIES; ++slot) {
        if (!s->alive[slot] || s->type[slot] != type
                || s->id[slot] == first_eid || s->id[slot] == second_eid)
            continue;
        if (child_count++) putchar(',');
        printf("{\"eid\":%d,\"species\":\"%s\","
               "\"growing_age\":%d,\"fleece\":%d,"
               "\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
               "\"yaw_bits\":\"%08" PRIx32 "\","
               "\"entity_seed48\":%" PRIu64 ","
               "\"entity_have_next_gaussian\":%s,"
               "\"entity_next_gaussian_bits\":\"%016" PRIx64 "\","
               "\"time_until_next_egg\":%d}",
            s->id[slot], argv[1], mobs.growing_age[slot],
            type == EW_TYPE_SHEEP ? mobs.sheep_data[slot] & 15 : -1,
            s->x[slot], s->y[slot], s->z[slot], float_bits(s->yaw[slot]),
            (uint64_t)mobs.entity_random[slot].random.seed,
            mobs.entity_random[slot].have_next_next_gaussian
                ? "true" : "false",
            double_bits(mobs.entity_random[slot].next_next_gaussian),
            type == EW_TYPE_CHICKEN
                ? mobs.chicken_time_until_next_egg[slot] : -1);
    }

    printf("],\"xp_orbs\":[");
    int orb_count = 0;
    for (int slot = 0; slot < GM_XP_ORBS; ++slot) {
        const McOrb *orb = &mobs.xp_orbs[slot];
        if (orb->dead || orb->xpValue <= 0) continue;
        if (orb_count++) putchar(',');
        printf("{\"eid\":%d,\"value\":%d,"
               "\"yaw_bits\":\"%08" PRIx32 "\","
               "\"payload_bits\":["
               "\"%016" PRIx64 "\",\"%016" PRIx64 "\","
               "\"%016" PRIx64 "\",\"%016" PRIx64 "\","
               "\"%016" PRIx64 "\",\"%016" PRIx64 "\"]}",
            orb->eid, orb->xpValue, float_bits(orb->yaw),
            double_bits(orb->posX), double_bits(orb->posY),
            double_bits(orb->posZ), double_bits(orb->motionX),
            double_bits(orb->motionY), double_bits(orb->motionZ));
    }

    printf("],\"particles\":[");
    int particle_count = 0;
    uint64_t particle_seq = 0;
    for (int batch_index = 0;
            batch_index < gm_mobs_particle_batch_count(&mobs);
            ++batch_index) {
        GmMobParticleBatch batch;
        if (!gm_mobs_particle_batch_get(&mobs, batch_index, &batch))
            return 1;
        for (int index = 0; index < batch.count; ++index) {
            const GmTerminalParticle *particle = &batch.particles[index];
            if (particle_count++) putchar(',');
            printf("{\"seq\":%" PRIu64 ",\"id\":%d,"
                   "\"ignore_range\":false,\"parameters\":[],"
                   "\"payload_bits\":["
                   "\"%016" PRIx64 "\",\"%016" PRIx64 "\","
                   "\"%016" PRIx64 "\",\"%016" PRIx64 "\","
                   "\"%016" PRIx64 "\",\"%016" PRIx64 "\"]}",
                particle_seq++, batch.particle_id,
                double_bits(particle->x), double_bits(particle->y),
                double_bits(particle->z), double_bits(particle->vx),
                double_bits(particle->vy), double_bits(particle->vz));
        }
    }
    printf("],\"world_seed48\":%" PRIu64 ","
           "\"math_seed48\":%" PRIu64 ","
           "\"next_entity_id\":%d}\n",
        world_seed48, math_seed48, next_entity_id);
    return 0;
}
