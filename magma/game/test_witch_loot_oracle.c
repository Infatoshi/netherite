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

int main(int argc, char **argv) {
    if (argc != 6) return 2;
    char *end = NULL;
    unsigned long long entitySeed = strtoull(argv[1], &end, 10);
    if (!end || *end || entitySeed >= (1ULL << 48)) return 2;
    unsigned long long mathSeed = strtoull(argv[2], &end, 10);
    if (!end || *end || mathSeed >= (1ULL << 48)) return 2;
    long nextId = strtol(argv[3], &end, 10);
    if (!end || *end || nextId <= 0 || nextId > 2147483644L) return 2;
    long looting = strtol(argv[4], &end, 10);
    if (!end || *end || looting < 0 || looting > 3) return 2;
    long enabled = strtol(argv[5], &end, 10);
    if (!end || *end || (enabled != 0 && enabled != 1)) return 2;

    JavaRandom random = {(uint64_t)entitySeed};
    EwitchLootOutcome loot = {0};
    uint64_t mathSeed48 = (uint64_t)mathSeed;
    if (enabled)
        ewitch_generate_loot(&random, (int)looting, &loot);

    printf("{\"ok\":true,\"looting\":%ld,\"do_mob_loot\":%s,"
           "\"drops\":[", looting, enabled ? "true" : "false");
    for (int i = 0; i < loot.count; ++i) {
        if (i) putchar(',');
        float hover = (float)(math_next_double(&mathSeed48)
            * (WITCH_PI * 2.0));
        float yaw = (float)(math_next_double(&mathSeed48) * 360.0);
        double motionX = (double)(float)(math_next_double(&mathSeed48)
            * 0.20000000298023224 - 0.10000000149011612);
        double motionZ = (double)(float)(math_next_double(&mathSeed48)
            * 0.20000000298023224 - 0.10000000149011612);
        printf("{\"eid\":%ld,\"item\":%d,\"count\":%d,\"meta\":0,"
               "\"position_bits\":[\"0000000000000000\","
               "\"0000000000000000\",\"0000000000000000\"],"
               "\"motion_bits\":[", nextId++, loot.item[i],
               loot.quantity[i]);
        print_double_bits(motionX); putchar(',');
        print_double_bits(0.20000000298023224); putchar(',');
        print_double_bits(motionZ);
        printf("],\"yaw_bits\":"); print_float_bits(yaw);
        printf(",\"hover_bits\":"); print_float_bits(hover);
        printf(",\"pickup_delay\":10,\"age\":0}");
    }
    printf("],\"entity_seed48\":%llu,\"math_seed48\":%llu,"
           "\"next_entity_id\":%ld}\n",
           (unsigned long long)random.seed,
           (unsigned long long)mathSeed48, nextId);
    return 0;
}
