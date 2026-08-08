#include "mc_rng.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static uint64_t double_bits(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static void print_orb(int eid, int value, JavaRandom *math) {
    float yaw = (float)(jrand_double(math) * 360.0D);
    float mx = (float)(jrand_double(math)
        * 0.20000000298023224D - 0.10000000149011612D);
    float my = (float)(jrand_double(math) * 0.2D);
    float mz = (float)(jrand_double(math)
        * 0.20000000298023224D - 0.10000000149011612D);
    printf("{\"eid\":%d,\"value\":%d,\"position_bits\":["
           "\"0000000000000000\",\"0000000000000000\","
           "\"0000000000000000\"],\"motion_bits\":["
           "\"%016" PRIx64 "\",\"%016" PRIx64 "\","
           "\"%016" PRIx64 "\"],\"yaw_bits\":\"%08" PRIx32 "\","
           "\"age\":0,\"pickup_delay\":0,\"health\":5,"
           "\"color\":0,\"target_color\":0}",
           eid, value,
           double_bits((double)(mx * 2.0F)),
           double_bits((double)(my * 2.0F)),
           double_bits((double)(mz * 2.0F)), float_bits(yaw));
}

int main(int argc, char **argv) {
    if (argc != 7) return 2;
    const char *type = argv[1];
    int size = atoi(argv[2]);
    uint64_t entity_seed = strtoull(argv[3], NULL, 10);
    uint64_t math_seed = strtoull(argv[4], NULL, 10);
    int next_id = atoi(argv[5]);
    int do_mob_loot = atoi(argv[6]) != 0;
    if ((strcmp(type, "slime") && strcmp(type, "magma_cube"))
            || (size != 1 && size != 2 && size != 4)
            || entity_seed >= (UINT64_C(1) << 48)
            || math_seed >= (UINT64_C(1) << 48)
            || next_id <= 0)
        return 2;

    JavaGaussianRandom entity;
    JavaRandom math = {math_seed};
    jrand_gaussian_set_state(&entity, entity_seed, 0, 0.0D);
    printf("{\"ok\":true,\"type\":\"%s\",\"size\":%d,"
           "\"do_mob_loot\":%s,\"orbs\":[",
           type, size, do_mob_loot ? "true" : "false");
    if (do_mob_loot) {
        int remaining = size;
        int first = 1;
        while (remaining > 0) {
            int value = remaining >= 3 ? 3 : 1;
            remaining -= value;
            if (!first) putchar(',');
            print_orb(next_id++, value, &math);
            first = 0;
        }
    }
    printf("],\"children\":[");
    if (size > 1) {
        int count = 2 + jrand_int_bound(&entity.random, 3);
        for (int k = 0; k < count; ++k) {
            float offset_x = ((float)(k % 2) - 0.5F)
                * (float)size / 4.0F;
            float offset_z = ((float)(k / 2) - 0.5F)
                * (float)size / 4.0F;
            int child_size = size / 2;
            float yaw = jrand_float(&entity.random) * 360.0F;
            float dimension = 0.51000005F * (float)child_size;
            if (k) putchar(',');
            printf("{\"eid\":%d,\"size\":%d,"
                   "\"health_bits\":\"%08" PRIx32 "\","
                   "\"position_bits\":[\"%016" PRIx64 "\","
                   "\"3fe0000000000000\",\"%016" PRIx64 "\"],"
                   "\"motion_bits\":[\"0000000000000000\","
                   "\"0000000000000000\",\"0000000000000000\"],"
                   "\"rotation_bits\":[\"%08" PRIx32 "\","
                   "\"00000000\"],\"size_bits\":["
                   "\"%08" PRIx32 "\",\"%08" PRIx32 "\"],"
                   "\"is_dead\":false}",
                   next_id++, child_size,
                   float_bits((float)(child_size * child_size)),
                   double_bits((double)offset_x),
                   double_bits((double)offset_z), float_bits(yaw),
                   float_bits(dimension), float_bits(dimension));
        }
    }
    for (int particle = 0; particle < 20; ++particle) {
        (void)jrand_gaussian_next(&entity);
        (void)jrand_gaussian_next(&entity);
        (void)jrand_gaussian_next(&entity);
        (void)jrand_float(&entity.random);
        (void)jrand_float(&entity.random);
        (void)jrand_float(&entity.random);
    }
    printf("],\"death_time\":20,\"entity_dead\":true,"
           "\"entity_seed48\":%" PRIu64 ","
           "\"entity_have_gaussian\":%s,"
           "\"entity_next_gaussian_bits\":\"%016" PRIx64 "\","
           "\"math_seed48\":%" PRIu64 ","
           "\"next_entity_id\":%d}\n",
           entity.random.seed,
           entity.have_next_next_gaussian ? "true" : "false",
           double_bits(entity.next_next_gaussian), math.seed, next_id);
    return 0;
}
