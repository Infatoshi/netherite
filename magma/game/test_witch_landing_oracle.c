#include "mc_rng.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void consume_visible_effect_particles(JavaRandom *random) {
    if (jrand_next(random, 1) == 0) return;
    (void)jrand_double(random);
    (void)jrand_double(random);
    (void)jrand_double(random);
}

int main(int argc, char **argv) {
    if (argc != 4) return 2;
    char *end = NULL;
    unsigned long long seed = strtoull(argv[1], &end, 10);
    if (!end || *end || seed >= (1ULL << 48)) return 2;
    int jump = strcmp(argv[2], "jump_safe") == 0;
    int slime = strcmp(argv[2], "slime") == 0;
    int farmland = strcmp(argv[2], "farmland") == 0
        || strcmp(argv[2], "farmland_no_grief") == 0;
    int mob_griefing = strcmp(argv[2], "farmland_no_grief") != 0;
    unsigned long long world_seed = strtoull(argv[3], &end, 10);
    if (!end || *end || world_seed >= (1ULL << 48)) return 2;
    if (!jump && !slime && !farmland) return 2;

    JavaRandom random = {(uint64_t)seed};
    JavaRandom world_random = {(uint64_t)world_seed};
    int trample = farmland
        && jrand_float(&world_random) < 0.100000024F && mob_griefing;
    if (jump) consume_visible_effect_particles(&random);
    (void)jrand_int_bound(&random, 1000);
    int status = jrand_float(&random) < 7.5E-4F;
    printf("{\"ok\":true,\"scenario\":\"%s\","
           "\"health_bits\":\"41a00000\",\"hurt_time\":0,"
           "\"hurt_resistant_time\":0,\"on_ground\":true,"
           "\"fall_distance_bits\":\"00000000\","
           "\"motion_bits\":[\"0000000000000000\",\"%s\","
           "\"0000000000000000\"],"
           "\"position_bits\":[\"0000000000000000\",\"%s\","
           "\"0000000000000000\"],\"jump_duration\":%d,"
           "\"jump_amplifier\":%d,\"event_order\":[",
           argv[2], slime ? "3f941205c28f5c2a" : "bfb41205c28f5c29",
           farmland ? "bfb0000000000000" : "0000000000000000",
           jump ? 19 : 0, jump ? 1 : -1);
    if (status) printf("\"status:15\"");
    printf("],\"landing_particles\":[");
    if (!farmland)
        printf("{\"id\":38,\"count\":%d,\"long_distance\":false,"
               "\"descriptor_bits\":[\"0000000000000000\","
               "\"0000000000000000\",\"0000000000000000\","
               "\"0000000000000000\",\"0000000000000000\","
               "\"0000000000000000\",\"3fc3333340000000\"],"
               "\"parameters\":[%d]}",
               jump ? 50 : 80, jump ? 1 : 165);
    printf("],\"support_block\":%d,\"support_meta\":0,"
           "\"entity_seed48\":%llu",
           farmland ? (trample ? 3 : 60) : jump ? 1 : 165,
           (unsigned long long)random.seed);
    if (farmland)
        printf(",\"world_seed48\":%llu",
               (unsigned long long)world_random.seed);
    puts("}");
    return 0;
}
