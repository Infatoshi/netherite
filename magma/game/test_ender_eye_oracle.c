#include "game/runtime.h"
#include "fdlibm_trig.h"

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
    GmRuntimeProjectile *eye;
    char error[256];
    uint64_t seed48, math_seed48;
    int next_id, ticks, target_x, target_y, target_z;
    double x, y, z;
    int water;
    if (argc != 12) return 2;
    seed48 = strtoull(argv[1], NULL, 10);
    math_seed48 = strtoull(argv[2], NULL, 10);
    next_id = atoi(argv[3]);
    ticks = atoi(argv[4]);
    water = atoi(argv[5]);
    x = strtod(argv[6], NULL);
    y = strtod(argv[7], NULL);
    z = strtod(argv[8], NULL);
    target_x = atoi(argv[9]);
    target_y = atoi(argv[10]);
    target_z = atoi(argv[11]);
    if (seed48 >= (UINT64_C(1) << 48)
            || math_seed48 >= (UINT64_C(1) << 48)
            || next_id <= 0 || ticks < 1 || ticks > 81
            || (water != 0 && water != 1))
        return 2;
    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.seed = 0;
    if (!gm_runtime_init(&runtime, &config, error, sizeof error)) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    gm_runtime_set_math_random_seed48(&runtime, math_seed48);
    gm_runtime_set_entity_id_cursor(&runtime, next_id);
    if (water)
        gm_world_set_block(runtime.world,
            (int)floor(x), (int)floor(y), (int)floor(z), 9);
    if (gm_runtime_spawn_ender_eye_fixture(
            &runtime, next_id, x, y, z,
            target_x, target_y, target_z, seed48) != 1) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    eye = &runtime.projectiles[0];
    printf("{\"ok\":true,\"eid\":%d,\"target_bits\":",
           eye->eid);
    print_double_bits3(
        eye->eye_target_x, eye->eye_target_y, eye->eye_target_z);
    printf(",\"shatter_or_drop\":%s,\"rows\":[",
           eye->eye_shatter_or_drop ? "true" : "false");
    for (int tick = 1; tick <= ticks; ++tick) {
        double next_x = eye->x + eye->vx;
        double next_z = eye->z + eye->vz;
        double steer_dx = eye->eye_target_x - next_x;
        double steer_dz = eye->eye_target_z - next_z;
        float steer_horizontal = (float)sqrt(
            eye->vx * eye->vx + eye->vz * eye->vz);
        float steer_target_horizontal = (float)sqrt(
            steer_dx * steer_dx + steer_dz * steer_dz);
        float steer_angle = (float)gm_runtime_mathhelper_atan2(
            steer_dz, steer_dx);
        double steer_speed = (double)steer_horizontal
            + (double)(steer_target_horizontal - steer_horizontal) * 0.0025;
        double steer_cos = mc_java_math_cos_small((double)steer_angle);
        double steer_sin = mc_java_math_sin_small((double)steer_angle);
        int particle_before = gm_runtime_particle_event_count(&runtime);
        if (!gm_runtime_tick_ender_eye_now(&runtime, 0)) {
            gm_runtime_destroy(&runtime);
            return 1;
        }
        if (tick > 1) putchar(',');
        printf("{\"tick\":%d,\"alive\":%s,"
               "\"ticks_existed\":%d,\"despawn_timer\":%d,"
               "\"position_bits\":",
               tick, eye->active ? "true" : "false",
               eye->ticks_existed, eye->age);
        print_double_bits3(eye->x, eye->y, eye->z);
        printf(",\"motion_bits\":");
        print_double_bits3(eye->vx, eye->vy, eye->vz);
        printf(",\"steering_bits\":[\"%08" PRIx32
               "\",\"%08" PRIx32 "\",\"%08" PRIx32
               "\",\"%016" PRIx64 "\",\"%016" PRIx64
               "\",\"%016" PRIx64 "\",\"%016" PRIx64
               "\",\"%016" PRIx64 "\"]",
               float_bits(steer_horizontal),
               float_bits(steer_target_horizontal),
               float_bits(steer_angle), double_bits(steer_speed),
               double_bits(steer_cos), double_bits(steer_sin),
               double_bits(steer_cos * steer_speed),
               double_bits(steer_sin * steer_speed));
        printf(",\"yaw_bits\":\"%08" PRIx32
               "\",\"pitch_bits\":\"%08" PRIx32
               "\",\"seed48\":%" PRIu64 ",\"particles\":[",
               float_bits(eye->yaw), float_bits(eye->pitch),
               eye->random_seed48);
        int emitted = 0;
        for (int index = particle_before;
                index < gm_runtime_particle_event_count(&runtime); ++index) {
            GmRuntimeParticleEvent particle;
            if (!gm_runtime_particle_event_get(
                    &runtime, index, &particle))
                continue;
            if (emitted++) putchar(',');
            printf("{\"id\":%d,\"payload_bits\":["
                   "\"%016" PRIx64 "\",\"%016" PRIx64
                   "\",\"%016" PRIx64 "\",\"%016" PRIx64
                   "\",\"%016" PRIx64 "\",\"%016" PRIx64
                   "\"]}",
                   particle.kind,
                   double_bits(particle.x), double_bits(particle.y),
                   double_bits(particle.z),
                   double_bits(particle.motion_x),
                   double_bits(particle.motion_y),
                   double_bits(particle.motion_z));
        }
        printf("]}");
    }
    printf("],\"items\":[");
    int emitted_items = 0;
    for (int i = 0; i < GM_LIVE_MAX; ++i) {
        const GmLiveEnt *item = &runtime.entities.ents[i];
        if (!item->active || item->type != 0 || item->item != 381)
            continue;
        if (emitted_items++) putchar(',');
        printf("{\"eid\":%d,\"item\":%d,\"count\":%d,"
               "\"meta\":%d,\"position_bits\":",
               item->eid, item->item, item->count, item->meta);
        print_double_bits3(item->x, item->y, item->z);
        printf(",\"motion_bits\":");
        print_double_bits3(item->mx, item->my, item->mz);
        printf(",\"yaw_bits\":\"%08" PRIx32
               "\",\"hover_bits\":\"%08" PRIx32
               "\",\"pickup_delay\":%d}",
               float_bits(item->yaw), float_bits(item->hover_start),
               item->pickup_delay);
    }
    printf("],\"world_events\":[");
    for (int index = 0;
            index < gm_runtime_world_event_count(&runtime); ++index) {
        GmRuntimeWorldEvent event;
        if (!gm_runtime_world_event_get(&runtime, index, &event))
            continue;
        if (index) putchar(',');
        printf("{\"seq\":%d,\"id\":%d,\"x\":%d,"
               "\"y\":%d,\"z\":%d,\"data\":%d}",
               index, event.id, event.x, event.y, event.z, event.data);
    }
    printf("],\"math_seed48\":%" PRIu64 "}\n",
           runtime.math_random_seed48);
    gm_runtime_destroy(&runtime);
    return 0;
}
