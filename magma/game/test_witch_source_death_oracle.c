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
    if (argc != 6 && argc != 7) return 2;
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
    int lava_burning = argc == 7 && strcmp(argv[6], "lava_burning") == 0;
    int lava_water_flow = argc == 7
        && strcmp(argv[6], "lava_water_flow") == 0;
    int burning = argc == 7
        && (strcmp(argv[6], "on_fire") == 0 || lava_burning);
    int lava = argc == 7
        && (strcmp(argv[6], "lava") == 0 || lava_burning
            || lava_water_flow);
    int in_fire_water_tick = argc == 7
        && strcmp(argv[6], "in_fire_water_tick") == 0;
    int in_fire_rain_tick = argc == 7
        && strcmp(argv[6], "in_fire_rain_tick") == 0;
    int in_fire_rain_roof_tick = argc == 7
        && strcmp(argv[6], "in_fire_rain_roof_tick") == 0;
    int in_fire_tick = argc == 7
        && (strcmp(argv[6], "in_fire_tick") == 0 || in_fire_water_tick
            || in_fire_rain_tick || in_fire_rain_roof_tick);
    int in_fire = argc == 7
        && (strcmp(argv[6], "in_fire") == 0 || in_fire_tick);
    int cactus_tick = argc == 7
        && strcmp(argv[6], "cactus_tick") == 0;
    int cactus = argc == 7
        && (strcmp(argv[6], "cactus") == 0 || cactus_tick);
    int in_wall_tick = argc == 7
        && strcmp(argv[6], "in_wall_tick") == 0;
    int fall_tick = argc == 7
        && (strcmp(argv[6], "fall_tick") == 0
            || strcmp(argv[6], "fall_big_tick") == 0
            || strcmp(argv[6], "fall_hay_tick") == 0);
    int fall_big_tick = argc == 7
        && strcmp(argv[6], "fall_big_tick") == 0;
    int fall_hay_tick = argc == 7
        && strcmp(argv[6], "fall_hay_tick") == 0;
    int product_tick = in_fire_tick || cactus_tick || in_wall_tick
        || fall_tick;
    if (argc == 7 && !burning && !lava && !in_fire && !cactus
            && !in_wall_tick && !fall_tick)
        return 2;

    JavaGaussianRandom entityRandom;
    jrand_gaussian_set_state(
        &entityRandom, (uint64_t)entitySeed, 0, 0.0);
    uint64_t mathSeed48 = (uint64_t)mathSeed;
    if (product_tick && !in_wall_tick)
        (void)jrand_int_bound(&entityRandom.random, 1000);
    int status_particles = product_tick && !in_wall_tick
        && jrand_float(&entityRandom.random) < 7.5E-4F;

    /* A fresh source-less hit performs setBeenAttacked, the global
     * no-attacker yaw choice, and two adult death-sound pitch floats. */
    (void)jrand_double(&entityRandom.random);
    (void)math_next_double(&mathSeed48);
    float first = jrand_float(&entityRandom.random);
    float second = jrand_float(&entityRandom.random);
    float deathPitch = (first - second) * 0.2F + 1.0F;

    EwitchLootOutcome loot = {0};
    if (enabled)
        ewitch_generate_loot(&entityRandom.random, 0, &loot);
    if (in_wall_tick)
        status_particles =
            jrand_float(&entityRandom.random) < 7.5E-4F;
    uint64_t seedAfterHit = entityRandom.random.seed;
    uint64_t mathAfterHit = mathSeed48;
    for (int item = 0; item < (enabled ? loot.count : 0); ++item)
        for (int draw = 0; draw < 4; ++draw)
            (void)math_next_double(&mathAfterHit);
    long idAfterHit = nextId + (enabled ? loot.count : 0);

    printf("{\"ok\":true,\"do_mob_loot\":%s,"
           "\"health_bits_after_hit\":\"00000000\","
           "\"death_time_after_hit\":%d,\"hurt_time_after_hit\":%d,"
           "\"hurt_resistant_after_hit\":%d,"
           "\"entity_dead_after_hit\":false,\"living_dead\":true,"
           "\"recently_hit_after_hit\":0,",
           enabled ? "true" : "false",
           (burning || lava || in_wall_tick) ? 1 : 0,
           (burning || lava || in_wall_tick) ? 9 : 10,
           (burning || lava || in_wall_tick) ? 19 : 20);
    if (product_tick)
        printf("\"status_particles\":%s,",
               status_particles ? "true" : "false");
    if (burning && !lava) printf("\"fire_after_hit\":19,");
    if (lava) {
        printf("\"fire_after_hit\":300,");
        printf("\"fall_distance_bits_after_hit\":\"%s\",",
            lava_water_flow ? "00000000" : "3fa00000");
        if (lava_water_flow) {
            printf("\"in_water_after_hit\":true,");
            printf("\"motion_bits_after_hit\":[");
            print_double_bits(0.014); putchar(',');
            print_double_bits(0.0); putchar(',');
            print_double_bits(0.0); printf("],");
        }
    }
    if (in_fire)
        printf("\"fire_after_hit\":%d,",
            in_fire_water_tick || in_fire_rain_tick ? 0 : 160);
    if (fall_tick) {
        const char *support = fall_hay_tick
            ? "minecraft:block.grass.fall" : "minecraft:block.stone.fall";
        int dust_count = fall_hay_tick || fall_big_tick ? 80 : 40;
        int state_id = fall_hay_tick ? 170 : 1;
        printf("\"fall_distance_bits_after_hit\":\"00000000\","
               "\"on_ground_after_hit\":true,"
               "\"motion_bits_after_hit\":["
               "\"0000000000000000\",\"bfb41205c28f5c29\","
               "\"0000000000000000\"],"
               "\"fall_sound\":\"minecraft:entity.hostile.%s_fall\","
               "\"fall_volume_bits\":\"3f800000\","
               "\"fall_pitch_bits\":\"3f800000\","
               "\"block_fall_sound\":\"%s\","
               "\"block_fall_volume_bits\":\"3f000000\","
               "\"block_fall_pitch_bits\":\"3f400000\","
               "\"event_order\":[",
               fall_big_tick ? "big" : "small", support);
        if (status_particles) printf("\"status:15\",");
        printf("\"minecraft:entity.hostile.%s_fall\","
               "\"status:2\",\"minecraft:entity.witch.death\","
               "\"status:3\",\"%s\"],"
               "\"landing_particles\":[{\"id\":38,\"count\":%d,"
               "\"long_distance\":false,\"descriptor_bits\":["
               "\"0000000000000000\",\"0000000000000000\","
               "\"0000000000000000\",\"0000000000000000\","
               "\"0000000000000000\",\"0000000000000000\","
               "\"3fc3333340000000\"],\"parameters\":[%d]}],",
               fall_big_tick ? "big" : "small",
               support, dust_count, state_id);
    }
    printf("\"first_status\":2,"
           "\"death_sound\":\"minecraft:entity.witch.%s\","
           "\"death_pitch_bits\":",
           lava_burning ? "hurt" : "death");
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
           "\"entity_seed_after_hit\":%llu,"
           "\"math_seed_after_hit\":%llu,\"next_id_after_hit\":%ld,"
           "\"death_time\":20,\"entity_dead\":true,"
           "\"entity_seed48\":",
           (unsigned long long)seedAfterHit,
           (unsigned long long)mathAfterHit, idAfterHit);

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
           worldSeed, (unsigned long long)itemMath, idAfterHit);
    return 0;
}
