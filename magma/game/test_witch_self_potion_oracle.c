#include "entity_witch.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    printf("\"%08x\"", bits);
}

static const char *potion_name(int potion) {
    switch (potion) {
    case EWITCH_SELF_WATER_BREATHING:
        return "minecraft:water_breathing";
    case EWITCH_SELF_FIRE_RESISTANCE:
        return "minecraft:fire_resistance";
    case EWITCH_SELF_HEALING:
        return "minecraft:healing";
    case EWITCH_SELF_SWIFTNESS:
        return "minecraft:swiftness";
    default:
        return "";
    }
}

int main(int argc, char **argv) {
    if (argc != 4) return 2;
    const char *phase = argv[1];
    const char *scenario = argv[2];
    char *end = NULL;
    unsigned long long seed48 = strtoull(argv[3], &end, 10);
    if (!end || *end || seed48 >= (1ULL << 48)) return 2;
    int requested = !strcmp(scenario, "water")
        ? EWITCH_SELF_WATER_BREATHING
        : !strcmp(scenario, "fire") ? EWITCH_SELF_FIRE_RESISTANCE
        : !strcmp(scenario, "heal") ? EWITCH_SELF_HEALING
        : !strcmp(scenario, "speed") ? EWITCH_SELF_SWIFTNESS
        : !strcmp(scenario, "none") ? EWITCH_SELF_NONE : -1;
    if (requested < 0
            || (strcmp(phase, "start") && strcmp(phase, "finish"))
            || (!strcmp(phase, "finish")
                && requested == EWITCH_SELF_NONE))
        return 2;

    JavaRandom random;
    jrand_set(&random, (i64)seed48);
    random.seed = (u64)seed48;
    EwitchSelfState state = {0};
    EwitchSelfConditions conditions = {0};
    EwitchSelfOutcome outcome;
    EwitchRngTrace trace;
    conditions.health = !strcmp(scenario, "heal") ? 20.0F : 26.0F;
    conditions.max_health = 26.0F;
    if (!strcmp(phase, "start")) {
        conditions.in_water = requested == EWITCH_SELF_WATER_BREATHING;
        conditions.burning = requested == EWITCH_SELF_FIRE_RESISTANCE;
        conditions.has_target = requested == EWITCH_SELF_SWIFTNESS;
        conditions.target_distance_sq = conditions.has_target ? 16384.0 : 0.0;
    } else {
        state.drinking = 1;
        state.timer = 0;
        state.potion = requested;
    }
    ewitch_self_potion_step(
        &random, &state, &conditions, &outcome, &trace);

    int effect_count = outcome.effect_id ? 1 : 0;
    int effect_id = outcome.effect_id ? outcome.effect_id : -1;
    int effect_amplifier = outcome.effect_id
        ? outcome.effect_amplifier : -1;
    double movement_speed = state.drinking ? 0.0 : 0.25;
    if (outcome.effect_id == 1)
        movement_speed *= 1.0 + 0.20000000298023224;
    printf("{\"ok\":true,\"phase\":\"%s\",\"scenario\":\"%s\","
           "\"drinking\":%s,\"timer\":%d,\"mainhand_item\":%d,"
           "\"mainhand_potion\":\"%s\",\"health\":%.9g,"
           "\"movement_speed\":%.17g,\"effect_count\":%d,"
           "\"effect_id\":%d,\"effect_duration\":%d,"
           "\"effect_amplifier\":%d,\"sounds\":[",
           phase, scenario, state.drinking ? "true" : "false", state.timer,
           state.potion ? 373 : 0, potion_name(state.potion),
           conditions.health, movement_speed, effect_count, effect_id,
           outcome.effect_duration, effect_amplifier);
    if (outcome.started) {
        printf("{\"sound\":\"minecraft:entity.witch.drink\","
               "\"category\":\"hostile\",\"position_bits\":["
               "\"0000000000000000\",\"0000000000000000\","
               "\"0000000000000000\"],"
               "\"volume_bits\":\"3f800000\",\"pitch_bits\":");
        print_float_bits(outcome.drink_pitch);
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
