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
        int eid, int item, int count, int meta, uint64_t *math_seed48) {
    float hover = (float)(math_next_double(math_seed48) * (WITCH_PI * 2.0));
    float yaw = (float)(math_next_double(math_seed48) * 360.0);
    double motion_x = (double)(float)(math_next_double(math_seed48)
        * 0.20000000298023224 - 0.10000000149011612);
    double motion_z = (double)(float)(math_next_double(math_seed48)
        * 0.20000000298023224 - 0.10000000149011612);
    printf("{\"eid\":%d,\"item\":%d,\"count\":%d,\"meta\":%d,"
           "\"position_bits\":[\"0000000000000000\","
           "\"0000000000000000\",\"0000000000000000\"],"
           "\"motion_bits\":[", eid, item, count, meta);
    print_double_bits(motion_x); putchar(',');
    print_double_bits(0.20000000298023224); putchar(',');
    print_double_bits(motion_z);
    printf("],\"yaw_bits\":"); print_float_bits(yaw);
    printf(",\"hover_bits\":"); print_float_bits(hover);
    printf(",\"pickup_delay\":10,\"age\":0}");
}

static int xp_split(int value) {
    static const int thresholds[] = {
        2477, 1237, 617, 307, 149, 73, 37, 17, 7, 3, 1
    };
    for (int i = 0;
            i < (int)(sizeof thresholds / sizeof thresholds[0]); ++i)
        if (value >= thresholds[i]) return thresholds[i];
    return 0;
}

static void print_orb(int eid, int value, uint64_t *math_seed48) {
    float yaw = (float)(math_next_double(math_seed48) * 360.0);
    float motion_x = (float)(math_next_double(math_seed48)
        * 0.20000000298023224 - 0.10000000149011612);
    float motion_y = (float)(math_next_double(math_seed48) * 0.2);
    float motion_z = (float)(math_next_double(math_seed48)
        * 0.20000000298023224 - 0.10000000149011612);
    printf("{\"eid\":%d,\"value\":%d,\"position_bits\":["
           "\"0000000000000000\",\"0000000000000000\","
           "\"0000000000000000\"],\"motion_bits\":[", eid, value);
    print_double_bits((double)(motion_x * 2.0F)); putchar(',');
    print_double_bits((double)(motion_y * 2.0F)); putchar(',');
    print_double_bits((double)(motion_z * 2.0F));
    printf("],\"yaw_bits\":"); print_float_bits(yaw);
    printf(",\"age\":0,\"pickup_delay\":0,\"health\":5,"
           "\"color\":0,\"target_color\":0}");
}

int main(int argc, char **argv) {
    if (argc != 7) return 2;
    char *end = NULL;
    unsigned long long entity_seed = strtoull(argv[1], &end, 10);
    if (!end || *end || entity_seed >= (1ULL << 48)) return 2;
    unsigned long long math_seed = strtoull(argv[2], &end, 10);
    if (!end || *end || math_seed >= (1ULL << 48)) return 2;
    unsigned long long world_seed = strtoull(argv[3], &end, 10);
    if (!end || *end || world_seed >= (1ULL << 48)) return 2;
    long next_id = strtol(argv[4], &end, 10);
    if (!end || *end || next_id <= 0 || next_id > 2147483641L) return 2;
    long looting = strtol(argv[5], &end, 10);
    if (!end || *end || looting < 0 || looting > 3) return 2;
    long enabled = strtol(argv[6], &end, 10);
    if (!end || *end || (enabled != 0 && enabled != 1)) return 2;

    JavaGaussianRandom entity_random;
    jrand_gaussian_set_state(
        &entity_random, (uint64_t)entity_seed, 0, 0.0);
    uint64_t math_seed48 = (uint64_t)math_seed;
    EwitchLootOutcome loot = {0};
    int equipment_drop = 0;
    int total_xp = 0;
    if (enabled) {
        ewitch_generate_loot(
            &entity_random.random, (int)looting, &loot);
        equipment_drop = ewitch_equipped_drop(
            &entity_random.random, (int)looting);
    }

    printf("{\"ok\":true,\"looting\":%ld,\"do_mob_loot\":%s,"
           "\"equipment_drop\":%s,\"drops\":[",
           looting, enabled ? "true" : "false",
           equipment_drop ? "true" : "false");
    int emitted = 0;
    if (enabled) {
        for (int i = 0; i < loot.count; ++i) {
            if (emitted++) putchar(',');
            print_item(
                (int)next_id++, loot.item[i], loot.quantity[i], 0,
                &math_seed48);
        }
        if (equipment_drop) {
            if (emitted++) putchar(',');
            /* PotionType registry id 21 is minecraft:healing. */
            print_item((int)next_id++, 373, 1, 21, &math_seed48);
        }
        total_xp = ewitch_experience_points(&entity_random.random, 1);
    }
    printf("],\"total_xp\":%d,\"orbs\":[", total_xp);
    int remaining = total_xp;
    int orb_count = 0;
    while (remaining > 0) {
        int value = xp_split(remaining);
        remaining -= value;
        if (orb_count++) putchar(',');
        print_orb((int)next_id++, value, &math_seed48);
    }
    for (int particle = 0; particle < 20; ++particle) {
        (void)jrand_gaussian_next(&entity_random);
        (void)jrand_gaussian_next(&entity_random);
        (void)jrand_gaussian_next(&entity_random);
        (void)jrand_float(&entity_random.random);
        (void)jrand_float(&entity_random.random);
        (void)jrand_float(&entity_random.random);
    }
    printf("],\"death_time\":20,\"entity_dead\":true,"
           "\"entity_seed48\":%llu,\"entity_have_gaussian\":%s,"
           "\"entity_next_gaussian_bits\":",
           (unsigned long long)entity_random.random.seed,
           entity_random.have_next_next_gaussian ? "true" : "false");
    print_double_bits(entity_random.next_next_gaussian);
    printf(",\"world_seed48\":%llu,\"math_seed48\":%llu,"
           "\"next_entity_id\":%ld}\n",
           world_seed, (unsigned long long)math_seed48, next_id);
    return 0;
}
