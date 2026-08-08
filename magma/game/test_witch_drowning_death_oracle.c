#include "entity_witch.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WITCH_PI 3.14159265358979323846264338327950288

static void print_float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    printf("\"%08x\"", bits);
}

static void print_double_bits(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof bits);
    printf("\"%016llx\"", (unsigned long long)bits);
}

static double math_next_double(uint64_t *seed48) {
    JavaRandom random = {*seed48};
    double value = jrand_double(&random);
    *seed48 = random.seed;
    return value;
}

static void print_item(
        int eid, int item, int count, uint64_t *math_seed48) {
    float hover = (float)(math_next_double(math_seed48) * (WITCH_PI * 2.0));
    float yaw = (float)(math_next_double(math_seed48) * 360.0);
    double motion_x = (double)(float)(math_next_double(math_seed48)
        * 0.20000000298023224 - 0.10000000149011612);
    double motion_z = (double)(float)(math_next_double(math_seed48)
        * 0.20000000298023224 - 0.10000000149011612);
    printf("{\"eid\":%d,\"item\":%d,\"count\":%d,\"meta\":0,"
           "\"position_bits\":[\"0000000000000000\","
           "\"0000000000000000\",\"0000000000000000\"],"
           "\"motion_bits\":[", eid, item, count);
    print_double_bits(motion_x); putchar(',');
    print_double_bits(0.20000000298023224); putchar(',');
    print_double_bits(motion_z);
    printf("],\"yaw_bits\":"); print_float_bits(yaw);
    printf(",\"hover_bits\":"); print_float_bits(hover);
    printf(",\"pickup_delay\":10,\"age\":0}");
}

int main(int argc, char **argv) {
    if (argc != 6) return 2;
    char *end = NULL;
    unsigned long long entitySeed = strtoull(argv[1], &end, 10);
    if (!end || *end || entitySeed >= (1ULL << 48)) return 2;
    unsigned long long mathSeed = strtoull(argv[2], &end, 10);
    if (!end || *end || mathSeed >= (1ULL << 48)) return 2;
    unsigned long long worldSeed = strtoull(argv[3], &end, 10);
    if (!end || *end || worldSeed >= (1ULL << 48)) return 2;
    long nextId = strtol(argv[4], &end, 10);
    if (!end || *end || nextId <= 0 || nextId > 2147483643L) return 2;
    long enabled = strtol(argv[5], &end, 10);
    if (!end || *end || (enabled != 0 && enabled != 1)) return 2;

    JavaGaussianRandom entityRandom;
    jrand_gaussian_set_state(
        &entityRandom, (uint64_t)entitySeed, 0, 0.0);
    uint64_t mathSeed48 = (uint64_t)mathSeed;

    /* EntityLivingBase's drowning pulse emits eight bubbles with six float
     * draws each before attackEntityFrom. DROWN skips setBeenAttacked, but a
     * fresh no-attacker hit still consumes Math.random for attackedAtYaw and
     * two entity floats for the Witch death-sound pitch. */
    for (int draw = 0; draw < 48; ++draw)
        (void)jrand_float(&entityRandom.random);
    (void)math_next_double(&mathSeed48);
    float first = jrand_float(&entityRandom.random);
    float second = jrand_float(&entityRandom.random);
    float deathPitch = (first - second) * 0.2F + 1.0F;

    EwitchLootOutcome loot = {0};
    if (enabled)
        ewitch_generate_loot(&entityRandom.random, 0, &loot);
    uint64_t seedAfterDrown = entityRandom.random.seed;
    uint64_t mathAfterDrown = mathSeed48;
    for (int item = 0; item < (enabled ? loot.count : 0); ++item)
        for (int draw = 0; draw < 4; ++draw)
            (void)math_next_double(&mathAfterDrown);
    long idAfterDrown = nextId + (enabled ? loot.count : 0);

    printf("{\"ok\":true,\"do_mob_loot\":%s,"
           "\"air_after_drown\":0,"
           "\"health_bits_after_drown\":\"00000000\","
           "\"death_time_after_drown\":1,"
           "\"hurt_time_after_drown\":9,"
           "\"hurt_resistant_after_drown\":19,"
           "\"entity_dead_after_drown\":false,"
           "\"living_dead\":true,"
           "\"recently_hit_after_drown\":0,"
           "\"first_status\":2,"
           "\"death_sound\":\"minecraft:entity.witch.death\","
           "\"death_pitch_bits\":",
           enabled ? "true" : "false");
    print_float_bits(deathPitch);
    printf(",\"second_status\":3,\"equipment_drop\":false,"
           "\"drops\":[");
    uint64_t itemMath = mathSeed48;
    for (int i = 0; i < (enabled ? loot.count : 0); ++i) {
        if (i) putchar(',');
        print_item(
            (int)nextId + i, loot.item[i], loot.quantity[i], &itemMath);
    }
    printf("],\"total_xp\":0,\"orbs\":[],"
           "\"entity_seed_after_drown\":%llu,"
           "\"math_seed_after_drown\":%llu,"
           "\"next_id_after_drown\":%ld,"
           "\"death_time\":20,\"entity_dead\":true,"
           "\"entity_seed48\":",
           (unsigned long long)seedAfterDrown,
           (unsigned long long)mathAfterDrown, idAfterDrown);

    for (int particle = 0; particle < 20; ++particle) {
        (void)jrand_gaussian_next(&entityRandom);
        (void)jrand_gaussian_next(&entityRandom);
        (void)jrand_gaussian_next(&entityRandom);
        (void)jrand_float(&entityRandom.random);
        (void)jrand_float(&entityRandom.random);
        (void)jrand_float(&entityRandom.random);
    }
    printf("%llu,\"entity_have_gaussian\":%s,"
           "\"entity_next_gaussian_bits\":",
           (unsigned long long)entityRandom.random.seed,
           entityRandom.have_next_next_gaussian ? "true" : "false");
    print_double_bits(entityRandom.next_next_gaussian);
    printf(",\"world_seed48\":%llu,\"math_seed48\":%llu,"
           "\"next_entity_id\":%ld}\n",
           worldSeed, (unsigned long long)itemMath, idAfterDrown);
    return 0;
}
