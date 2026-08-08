#include "game/runtime.h"

#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t float_bits(float value) {
    union { float f; uint32_t u; } bits;
    bits.f = value;
    return bits.u;
}

int main(int argc, char **argv) {
    static const int items[4] = {313, 312, 443, 310};
    if (argc != 15) return 2;
    float amount = strtof(argv[1], NULL);
    float health = strtof(argv[2], NULL);
    float absorption = strtof(argv[3], NULL);
    int hurt_resistant = atoi(argv[4]);
    float last_damage = strtof(argv[5], NULL);
    int resistance = atoi(argv[6]);
    int protection[4], damage[4];
    for (int i = 0; i < 4; ++i) protection[i] = atoi(argv[7 + i]);
    for (int i = 0; i < 4; ++i) damage[i] = atoi(argv[11 + i]);
    if (!isfinite(amount) || amount <= 0.0F || amount > 1024.0F
            || !isfinite(health) || health <= 0.0F || health > 20.0F
            || !isfinite(absorption) || absorption < 0.0F
            || absorption > 1024.0F || hurt_resistant < 0
            || hurt_resistant > 20 || !isfinite(last_damage)
            || last_damage < 0.0F || resistance < -1 || resistance > 4)
        return 2;
    GmConfig config;
    GmRuntime runtime;
    char error[256];
    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.mobs = 0;
    config.weather = 0;
    if (!gm_runtime_init(&runtime, &config, error, sizeof error)) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    isr_init(&runtime.player.inv);
    for (int i = 0; i < 4; ++i) {
        if (protection[i] < 0 || protection[i] > 32
                || damage[i] < 0 || damage[i] > 4096) {
            gm_runtime_destroy(&runtime);
            return 2;
        }
        ICStack stack = ic_mk(items[i], 1, damage[i]);
        if (protection[i] > 0)
            stack.enchants[stack.n_enchants++] =
                (IcEnch){0, (i16)protection[i]};
        isr_set_stack(&runtime.player.inv, ISR_ARMOR0 + i, stack);
    }
    runtime.vitals.health = health;
    runtime.player.health = health;
    runtime.server_player.health = health;
    runtime.mobs.player_absorption = absorption;
    runtime.mobs.player_hurt_resistant = hurt_resistant;
    runtime.mobs.player_hurt_time = 0;
    runtime.mobs.player_last_damage = last_damage;
    runtime.mobs.player_resistance_amplifier = resistance;
    int result = gm_runtime_elytra_wall_damage_now(&runtime, amount);

    printf("{\"ok\":true,\"accepted\":%s,"
           "\"health_bits\":\"%08" PRIx32 "\","
           "\"absorption_bits\":\"%08" PRIx32 "\","
           "\"hurt_time\":%d,\"hurt_resistant_time\":%d,"
           "\"last_damage_bits\":\"%08" PRIx32 "\","
           "\"equipment\":[",
           result ? "true" : "false", float_bits(runtime.vitals.health),
           float_bits(runtime.mobs.player_absorption),
           runtime.mobs.player_hurt_time,
           runtime.mobs.player_hurt_resistant,
           float_bits(runtime.mobs.player_last_damage));
    for (int i = 0; i < 4; ++i) {
        ICStack stack = isr_get_stack(
            &runtime.player.inv, ISR_ARMOR0 + i);
        printf("%s[%d,%d,%d]", i ? "," : "",
            stack.item, stack.count, stack.meta);
    }
    puts("]}");
    gm_runtime_destroy(&runtime);
    return 0;
}
