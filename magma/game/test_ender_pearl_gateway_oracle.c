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

static void print_double_bits3(double x, double y, double z) {
    printf("[\"%016" PRIx64 "\",\"%016" PRIx64
           "\",\"%016" PRIx64 "\"]",
           double_bits(x), double_bits(y), double_bits(z));
}

int main(int argc, char **argv) {
    GmConfig config;
    GmRuntime runtime;
    GmRuntimeProjectile *pearl;
    GmRuntimeEndGateway *gateway = NULL;
    char error[256];
    uint64_t pearl_seed;
    int exit_x, exit_y, exit_z, cooldown;
    if (argc != 6) return 2;
    pearl_seed = strtoull(argv[1], NULL, 10);
    exit_x = atoi(argv[2]);
    exit_y = atoi(argv[3]);
    exit_z = atoi(argv[4]);
    cooldown = atoi(argv[5]);
    if (pearl_seed >= (UINT64_C(1) << 48)
            || exit_y < 1 || exit_y > 254
            || cooldown < 0 || cooldown > 100)
        return 2;
    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.mobs = 0;
    config.weather = 0;
    if (!gm_runtime_init(&runtime, &config, error, sizeof error)
            || !gm_runtime_set_dimension(&runtime, 1)) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    gm_runtime_set_pose_state(
        &runtime, 24.5, 220.0, 24.5, 37.5F, -15.0F,
        0.25, -0.5, 0.75, 1, 7.0F);
    if (!gm_runtime_spawn_end_gateway(
            &runtime, 8, 230, 8,
            1, exit_x, exit_y, exit_z, 1)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    for (int i = 0; i < GM_RUNTIME_END_GATEWAYS; ++i)
        if (runtime.end_gateways[i].active
                && runtime.end_gateways[i].x == 8
                && runtime.end_gateways[i].y == 230
                && runtime.end_gateways[i].z == 8) {
            gateway = &runtime.end_gateways[i];
            break;
        }
    if (!gateway) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    gateway->teleport_cooldown = cooldown;
    pearl = &runtime.projectiles[0];
    memset(pearl, 0, sizeof *pearl);
    pearl->active = 1;
    pearl->type = 12;
    pearl->dimension = 1;
    pearl->eid = 4999;
    pearl->player_thrower = 1;
    pearl->x = 8.5;
    pearl->y = 230.5;
    pearl->z = 8.5;
    pearl->yaw = 12.5F;
    pearl->pitch = 3.0F;
    pearl->random_seed48 = pearl_seed;
    if (!gm_runtime_ender_pearl_gateway_impact_now(
            &runtime, 0, 8, 230, 8)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    printf("{\"ok\":true,\"pearl_dead\":%s"
           ",\"pearl_seed48\":%" PRIu64
           ",\"pearl_have_gaussian\":%s"
           ",\"pearl_gaussian_bits\":\"%016" PRIx64
           "\",\"player_position_bits\":",
           pearl->active ? "false" : "true", pearl->random_seed48,
           pearl->random_have_gaussian ? "true" : "false",
           double_bits(pearl->random_next_gaussian));
    print_double_bits3(
        runtime.player.ent.posX + (double)runtime.ox,
        runtime.player.ent.posY,
        runtime.player.ent.posZ + (double)runtime.oz);
    printf(",\"player_motion_bits\":");
    print_double_bits3(
        runtime.player.ent.motionX, runtime.player.ent.motionY,
        runtime.player.ent.motionZ);
    printf(",\"player_on_ground\":%s"
           ",\"player_fall_distance_bits\":\"%08" PRIx32
           "\",\"gateway_cooldown\":%d}\n",
           runtime.player.ent.onGround ? "true" : "false",
           float_bits(runtime.player.fall_distance),
           gateway->teleport_cooldown);
    gm_runtime_destroy(&runtime);
    return 0;
}
