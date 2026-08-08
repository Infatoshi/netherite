#include "game/runtime.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t double_bits(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static uint32_t float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static void add_enchantment(ICStack *stack, int id, int level) {
    if (level <= 0 || stack->n_enchants >= IC_MAX_ENCHANTS) return;
    int at = stack->n_enchants++;
    stack->enchants[at].id = id;
    stack->enchants[at].level = level;
}

int main(int argc, char **argv) {
    if (argc != 16) return 2;
    int draw = atoi(argv[1]);
    int bow_damage = atoi(argv[2]);
    int arrows = atoi(argv[3]);
    int power = atoi(argv[4]);
    int punch = atoi(argv[5]);
    int flame = atoi(argv[6]);
    int infinity = atoi(argv[7]);
    int unbreaking = atoi(argv[8]);
    uint64_t player_seed = strtoull(argv[9], NULL, 10);
    uint64_t arrow_seed = strtoull(argv[10], NULL, 10);
    int arrow_have = atoi(argv[11]);
    double arrow_gaussian = strtod(argv[12], NULL);
    uint64_t item_seed = strtoull(argv[13], NULL, 10);
    float yaw = strtof(argv[14], NULL);
    float pitch = strtof(argv[15], NULL);
    if (draw < 0 || draw > 72000 || bow_damage < 0 || bow_damage > 384
            || arrows < 0 || arrows > 64 || power < 0 || power > 32
            || punch < 0 || punch > 32 || flame < 0 || flame > 32
            || infinity < 0 || infinity > 32
            || unbreaking < 0 || unbreaking > 32
            || player_seed >= (UINT64_C(1) << 48)
            || arrow_seed >= (UINT64_C(1) << 48)
            || item_seed >= (UINT64_C(1) << 48)
            || (arrow_have != 0 && arrow_have != 1))
        return 2;

    GmConfig config;
    GmRuntime runtime;
    char error[256];
    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.seed = 0;
    if (!gm_runtime_init(&runtime, &config, error, sizeof error)) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    gm_runtime_set_pose(&runtime, 24.5, 220.0, 24.5, yaw, pitch);
    runtime.player.ent.motionX = 0.0;
    runtime.player.ent.motionY = 0.0;
    runtime.player.ent.motionZ = 0.0;
    runtime.player.ent.onGround = 1;
    runtime.player.inv.current_item = 0;
    ICStack bow = ic_mk(261, 1, bow_damage);
    add_enchantment(&bow, 48, power);
    add_enchantment(&bow, 49, punch);
    add_enchantment(&bow, 50, flame);
    add_enchantment(&bow, 51, infinity);
    add_enchantment(&bow, 34, unbreaking);
    isr_set_stack(&runtime.player.inv, 0, bow);
    if (arrows > 0)
        isr_set_stack(&runtime.player.inv, 1, ic_mk(262, arrows, 0));
    if (!gm_runtime_set_player_random_seed48(&runtime, player_seed)
            || !gm_runtime_set_next_arrow_random_state(
                &runtime, arrow_seed, arrow_have, arrow_gaussian)
            || !gm_runtime_set_sound_random_seed48(&runtime, item_seed)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    int spawned = gm_runtime_release_bow_now(&runtime, draw);
    ICStack after_bow = isr_get_stack(&runtime.player.inv, 0);
    ICStack after_arrow = isr_get_stack(&runtime.player.inv, 1);
    GmRuntimeSoundEvent sound;
    float shoot_pitch = gm_runtime_sound_event_count(&runtime) == 1
            && gm_runtime_sound_event_get(&runtime, 0, &sound)
        ? sound.pitch : 0.0F;
    printf("{\"ok\":true,\"spawned\":%s,"
           "\"bow_count\":%d,\"bow_damage\":%d,\"arrows\":%d,"
           "\"player_seed48\":%" PRIu64 ","
           "\"item_seed48\":%" PRIu64 ","
           "\"shoot_pitch_bits\":\"%08" PRIx32 "\"",
           spawned ? "true" : "false",
           after_bow.count, after_bow.meta,
           after_arrow.item == 262 ? after_arrow.count : 0,
           runtime.mobs.player_random.seed,
           runtime.sound_random_seed48, float_bits(shoot_pitch));
    if (spawned) {
        const GmRuntimeProjectile *arrow = NULL;
        for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i)
            if (runtime.projectiles[i].active
                    && runtime.projectiles[i].type == 1) {
                arrow = &runtime.projectiles[i];
                break;
            }
        if (!arrow) {
            gm_runtime_destroy(&runtime);
            return 1;
        }
        printf(",\"x_bits\":\"%016" PRIx64 "\","
               "\"y_bits\":\"%016" PRIx64 "\","
               "\"z_bits\":\"%016" PRIx64 "\","
               "\"vx_bits\":\"%016" PRIx64 "\","
               "\"vy_bits\":\"%016" PRIx64 "\","
               "\"vz_bits\":\"%016" PRIx64 "\","
               "\"yaw_bits\":\"%08" PRIx32 "\","
               "\"pitch_bits\":\"%08" PRIx32 "\","
               "\"damage_bits\":\"%016" PRIx64 "\","
               "\"knockback\":%d,\"critical\":%s,"
               "\"fire_ticks\":%d,\"pickup\":%d,"
               "\"arrow_seed48\":%" PRIu64 ","
               "\"arrow_have_gaussian\":%s,"
               "\"arrow_next_gaussian_bits\":\"%016" PRIx64 "\"",
               double_bits(arrow->x), double_bits(arrow->y),
               double_bits(arrow->z), double_bits(arrow->vx),
               double_bits(arrow->vy), double_bits(arrow->vz),
               float_bits(arrow->yaw), float_bits(arrow->pitch),
               double_bits(arrow->arrow_damage), arrow->arrow_knockback,
               arrow->arrow_critical ? "true" : "false",
               arrow->fire_ticks, arrow->arrow_pickup_status,
               arrow->random_seed48,
               arrow->random_have_gaussian ? "true" : "false",
               double_bits(arrow->random_next_gaussian));
    }
    puts("}");
    gm_runtime_destroy(&runtime);
    return 0;
}
