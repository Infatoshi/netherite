#include "mc_rng.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    char *end = NULL;
    unsigned long long seed48 = strtoull(argv[1], &end, 10);
    if (!end || *end || seed48 >= (1ULL << 48)) return 2;
    JavaGaussianRandom random;
    jrand_gaussian_set_state(&random, (u64)seed48, 0, 0.0);
    double follow_bonus = jrand_gaussian_next(&random) * 0.05;
    int left_handed = jrand_float(&random.random) < 0.05F;
    printf("{\"ok\":true,\"x\":10.5,\"y\":200,\"z\":-3.5,"
           "\"vx\":0,\"vy\":0,\"vz\":0,"
           "\"health\":26,\"max_health\":26,"
           "\"yaw\":0,\"pitch\":0,"
           "\"width\":%.9g,\"height\":%.9g,\"eye_height\":%.9g,"
           "\"movement_speed\":%.17g,\"follow_range\":%.17g,"
           "\"fire\":-1,\"air\":300,\"persistence\":true,"
           "\"on_ground\":false,\"left_handed\":%s,"
           "\"drinking\":false,\"mainhand_empty\":true,"
           "\"entity_seed48\":%llu,\"entity_have_gaussian\":%s,"
           "\"entity_next_gaussian\":%.17g}\n",
           0.6F, 1.95F, 1.62F, 0.25,
           16.0 + 16.0 * follow_bonus,
           left_handed ? "true" : "false",
           (unsigned long long)random.random.seed,
           random.have_next_next_gaussian ? "true" : "false",
           random.next_next_gaussian);
    return 0;
}
