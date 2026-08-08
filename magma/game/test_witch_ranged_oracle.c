#include "entity_blaze_fireball.h"
#include "entity_witch.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WITCH_PI 3.14159265358979323846

static void print_double_bits(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof bits);
    printf("\"%016llx\"", (unsigned long long)bits);
}

static void print_float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    printf("\"%08x\"", (unsigned)bits);
}

static void print_vec_bits(double x, double y, double z) {
    putchar('[');
    print_double_bits(x); putchar(',');
    print_double_bits(y); putchar(',');
    print_double_bits(z); putchar(']');
}

static const char *potion_name(int potion) {
    switch (potion) {
    case EWITCH_THROW_HARMING: return "minecraft:harming";
    case EWITCH_THROW_SLOWNESS: return "minecraft:slowness";
    case EWITCH_THROW_POISON: return "minecraft:poison";
    case EWITCH_THROW_WEAKNESS: return "minecraft:weakness";
    default: return "";
    }
}

/* Minecraft 1.11.2 MathHelper.atan2, including its table approximation. */
static double java_math_atan2(double y, double x) {
    static double asine[257], cosine[257];
    static int initialized;
    const double frac_bias = 17592186044416.0;
    double squared = x * x + y * y;
    uint64_t bits;
    if (isnan(squared)) return NAN;
    if (!initialized) {
        for (int i = 0; i <= 256; ++i) {
            double value = asin((double)i / 256.0);
            asine[i] = value;
            cosine[i] = cos(value);
        }
        initialized = 1;
    }
    int negative_y = y < 0.0;
    if (negative_y) y = -y;
    int negative_x = x < 0.0;
    if (negative_x) x = -x;
    int swapped = y > x;
    if (swapped) {
        double hold = x;
        x = y;
        y = hold;
    }
    double half = 0.5 * squared;
    memcpy(&bits, &squared, sizeof bits);
    bits = UINT64_C(6910469410427058090) - (bits >> 1);
    double inv;
    memcpy(&inv, &bits, sizeof inv);
    inv = inv * (1.5 - half * inv * inv);
    x *= inv;
    y *= inv;
    double biased = frac_bias + y;
    memcpy(&bits, &biased, sizeof bits);
    int index = (int)(uint32_t)bits;
    double table_value = asine[index];
    double quantized = biased - frac_bias;
    double error = y * cosine[index] - x * quantized;
    table_value += (6.0 + error * error) * error
        * 0.16666666666666666;
    if (swapped) table_value = WITCH_PI / 2.0 - table_value;
    if (negative_x) table_value = WITCH_PI - table_value;
    if (negative_y) table_value = -table_value;
    return table_value;
}

int main(int argc, char **argv) {
    if (argc != 4) return 2;
    const char *scenario = argv[1];
    char *end = NULL;
    unsigned long long seed48 = strtoull(argv[2], &end, 10);
    if (!end || *end || seed48 >= (1ULL << 48)) return 2;
    end = NULL;
    unsigned long long projectile_seed48 = strtoull(argv[3], &end, 10);
    if (!end || *end || projectile_seed48 >= (1ULL << 48)) return 2;

    double offset = 5.0, motion_x = 0.0;
    float health = 7.0F;
    int slowness = 0, poison = 0, weakness = 0, drinking = 0;
    if (!strcmp(scenario, "far_slowness")) {
        offset = 9.0;
    } else if (!strcmp(scenario, "moving_slowness")) {
        offset = 7.75; motion_x = 0.5;
    } else if (!strcmp(scenario, "far_slow_poison")) {
        offset = 9.0; health = 20.0F; slowness = 1;
    } else if (!strcmp(scenario, "mid_poison")) {
        health = 20.0F;
    } else if (!strcmp(scenario, "mid_poison_active")) {
        health = 20.0F; poison = 1;
    } else if (!strcmp(scenario, "close_weakness")
            || !strcmp(scenario, "close_weakness_fail")) {
        offset = 2.0;
    } else if (!strcmp(scenario, "close_weakness_active")) {
        offset = 2.0; weakness = 1;
    } else if (!strcmp(scenario, "drinking")) {
        offset = 9.0; drinking = 1;
    } else {
        return 2;
    }

    const double witch_x = 40.5, witch_y = 220.0, witch_z = 0.5;
    EwitchRangedConditions conditions = {
        drinking, slowness, poison, weakness,
        witch_x, witch_y, witch_z,
        witch_x + offset, witch_y, witch_z,
        motion_x, 0.0, 1.62F, health
    };
    JavaRandom witch_random;
    jrand_set_seed48(&witch_random, (u64)seed48);
    EwitchRangedOutcome outcome;
    EwitchRngTrace trace;
    ewitch_ranged_attack(
        &witch_random, &conditions, &outcome, &trace);

    double aim_x = conditions.target_x + conditions.target_motion_x - witch_x;
    double aim_y = conditions.target_y + (double)conditions.target_eye_height
        - 1.100000023841858 - witch_y;
    double aim_z = conditions.target_z + conditions.target_motion_z - witch_z;
    float horizontal = (float)sqrt(aim_x * aim_x + aim_z * aim_z);
    aim_y += (double)(horizontal * 0.2F);
    JavaGaussianRandom projectile_random;
    jrand_gaussian_set_state(
        &projectile_random, (u64)projectile_seed48, 0, 0.0);
    EbfVector heading = ebf_throwable_heading(
        &projectile_random, aim_x, aim_y, aim_z, 0.75F, 8.0F);
    float horizontal_motion = (float)sqrt(
        heading.x * heading.x + heading.z * heading.z);
    float yaw = (float)(java_math_atan2(heading.x, heading.z)
        * (180.0 / WITCH_PI));
    float pitch = (float)(java_math_atan2(
        heading.y, (double)horizontal_motion) * (180.0 / WITCH_PI));

    printf("{\"ok\":true,\"scenario\":\"%s\",\"thrown\":%s,"
           "\"potion\":\"%s\",\"spawn_bits\":",
           scenario, outcome.thrown ? "true" : "false",
           potion_name(outcome.potion));
    if (outcome.thrown)
        print_vec_bits(outcome.spawn_x - witch_x,
            outcome.spawn_y - witch_y, outcome.spawn_z - witch_z);
    else
        print_vec_bits(0.0, 0.0, 0.0);
    printf(",\"size_bits\":[");
    print_float_bits(outcome.thrown ? 0.25F : 0.0F); putchar(',');
    print_float_bits(outcome.thrown ? 0.25F : 0.0F);
    printf("],\"aim_bits\":");
    print_vec_bits(aim_x, aim_y, aim_z);
    printf(",\"horizontal_bits\":");
    print_float_bits(horizontal);
    printf(",\"heading_motion_bits\":");
    print_vec_bits(heading.x, heading.y, heading.z);
    printf(",\"heading_rotation_bits\":[");
    print_float_bits(yaw); putchar(','); print_float_bits(pitch);
    printf("],\"projectile_seed48\":%llu,"
           "\"projectile_have_gaussian\":%s,"
           "\"projectile_next_gaussian_bits\":",
           (unsigned long long)projectile_random.random.seed,
           projectile_random.have_next_next_gaussian ? "true" : "false");
    print_double_bits(projectile_random.next_next_gaussian);
    printf(",\"sounds\":[");
    if (outcome.thrown) {
        printf("{\"sound\":\"minecraft:entity.witch.throw\","
               "\"category\":\"hostile\",\"position_bits\":");
        print_vec_bits(0.0, 0.0, 0.0);
        printf(",\"volume_bits\":\"3f800000\",\"pitch_bits\":");
        print_float_bits(outcome.sound_pitch);
        putchar('}');
    }
    printf("],\"direct_random_calls\":[");
    for (int i = 0; i < trace.count; ++i) {
        if (i) putchar(',');
        printf("{\"bits\":24,\"value\":%d,\"seed48\":%llu}",
            trace.value[i], (unsigned long long)trace.seed48[i]);
    }
    puts("]}");
    return 0;
}
