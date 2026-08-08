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

static void print_bits(double value) {
    printf("\"%016" PRIx64 "\"", double_bits(value));
}

int main(int argc, char **argv) {
    GmConfig config;
    GmRuntime runtime;
    GmRuntimeProjectile *egg;
    char error[256];
    uint64_t seed48;
    int eid;
    double x, y, z;
    if (argc != 6) return 2;
    seed48 = strtoull(argv[1], NULL, 10);
    eid = atoi(argv[2]);
    x = strtod(argv[3], NULL);
    y = strtod(argv[4], NULL);
    z = strtod(argv[5], NULL);
    if (seed48 >= (UINT64_C(1) << 48) || eid <= 0) return 2;
    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.seed = 0;
    if (!gm_runtime_init(&runtime, &config, error, sizeof error)) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    egg = &runtime.projectiles[0];
    memset(egg, 0, sizeof *egg);
    egg->active = 1;
    egg->type = 7;
    egg->dimension = runtime.dimension;
    egg->eid = eid;
    egg->x = x;
    egg->y = y;
    egg->z = z;
    if (!gm_runtime_egg_client_status_now(&runtime, 0, seed48)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    printf("{\"ok\":true,\"particles\":[");
    for (int i = 0; i < gm_runtime_particle_event_count(&runtime); ++i) {
        GmRuntimeParticleEvent event;
        if (!gm_runtime_particle_event_get(&runtime, i, &event)) continue;
        if (i) putchar(',');
        printf("{\"seq\":%d,\"id\":%d,\"ignore_range\":false,"
               "\"parameters\":[%d],\"payload_bits\":[",
               i, event.kind, event.parameters[0]);
        print_bits(event.x); putchar(',');
        print_bits(event.y); putchar(',');
        print_bits(event.z); putchar(',');
        print_bits(event.motion_x); putchar(',');
        print_bits(event.motion_y); putchar(',');
        print_bits(event.motion_z);
        printf("]}");
    }
    printf("],\"client_seed48\":%" PRIu64 "}\n",
           egg->client_random_seed48);
    gm_runtime_destroy(&runtime);
    return 0;
}
