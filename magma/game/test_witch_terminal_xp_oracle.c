#include "mc_rng.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    if (argc != 6) return 2;
    char *end = NULL;
    unsigned long long entity_seed = strtoull(argv[1], &end, 10);
    if (!end || *end || entity_seed >= (1ULL << 48)) return 2;
    unsigned long long math_seed = strtoull(argv[2], &end, 10);
    if (!end || *end || math_seed >= (1ULL << 48)) return 2;
    unsigned long long world_seed = strtoull(argv[3], &end, 10);
    if (!end || *end || world_seed >= (1ULL << 48)) return 2;
    long next_id = strtol(argv[4], &end, 10);
    if (!end || *end || next_id <= 0 || next_id > 2147483644L) return 2;
    long enabled = strtol(argv[5], &end, 10);
    if (!end || *end || (enabled != 0 && enabled != 1)) return 2;

    uint64_t math_seed48 = (uint64_t)math_seed;
    JavaGaussianRandom entity_random;
    jrand_gaussian_set_state(
        &entity_random, (uint64_t)entity_seed, 0, 0.0);

    printf("{\"ok\":true,\"do_mob_loot\":%s,\"orbs\":[",
        enabled ? "true" : "false");
    if (enabled) {
        print_orb((int)next_id++, 3, &math_seed48); putchar(',');
        print_orb((int)next_id++, 1, &math_seed48); putchar(',');
        print_orb((int)next_id++, 1, &math_seed48);
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
