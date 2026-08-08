#include "game/runtime.h"

#include <inttypes.h>
#include <math.h>
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

int main(int argc, char **argv) {
    if (argc != 7) return 2;
    uint64_t world_seed = strtoull(argv[1], NULL, 10);
    uint64_t math_seed = strtoull(argv[2], NULL, 10);
    int next_id = atoi(argv[3]);
    double x = strtod(argv[4], NULL);
    double y = strtod(argv[5], NULL);
    double z = strtod(argv[6], NULL);
    if (world_seed >= (UINT64_C(1) << 48)
            || math_seed >= (UINT64_C(1) << 48)
            || next_id <= 0 || next_id > INT32_MAX - 4)
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
    gm_runtime_set_world_random_seed48(&runtime, world_seed);
    gm_runtime_set_math_random_seed48(&runtime, math_seed);
    gm_runtime_set_entity_id_cursor(&runtime, next_id);
    GmRuntimeProjectile *bottle = &runtime.projectiles[0];
    memset(bottle, 0, sizeof *bottle);
    bottle->active = 1;
    bottle->type = 9;
    bottle->x = x;
    bottle->y = y;
    bottle->z = z;
    int count = gm_runtime_xp_bottle_impact_now(&runtime, 0);
    if (count <= 0) {
        gm_runtime_destroy(&runtime);
        return 1;
    }

    int total = 0;
    printf("{\"ok\":true,\"bottle_dead\":%s,\"total_xp\":",
        bottle->active ? "false" : "true");
    for (int i = 0; i < GM_XP_ORBS; ++i)
        if (!runtime.mobs.xp_orbs[i].dead
                && runtime.mobs.xp_orbs[i].xpValue > 0)
            total += runtime.mobs.xp_orbs[i].xpValue;
    printf("%d,\"orbs\":[", total);
    int emitted = 0;
    for (int order = 0;
            order < gm_mobs_loaded_order_count(&runtime.mobs); ++order) {
        int eid = 0, kind = 0;
        if (!gm_mobs_loaded_order_get(
                &runtime.mobs, order, &eid, &kind)
                || kind != GM_MOB_LOADED_XP)
            continue;
        McOrb *orb = NULL;
        for (int slot = 0; slot < GM_XP_ORBS; ++slot)
            if (!runtime.mobs.xp_orbs[slot].dead
                    && runtime.mobs.xp_orbs[slot].xpValue > 0
                    && runtime.mobs.xp_orbs[slot].eid == eid) {
                orb = &runtime.mobs.xp_orbs[slot];
                break;
            }
        if (!orb) continue;
        if (emitted++) putchar(',');
        printf("{\"eid\":%d,\"value\":%d,"
               "\"position_bits\":[\"%016" PRIx64
               "\",\"%016" PRIx64 "\",\"%016" PRIx64 "\"],"
               "\"motion_bits\":[\"%016" PRIx64
               "\",\"%016" PRIx64 "\",\"%016" PRIx64 "\"],"
               "\"yaw_bits\":\"%08" PRIx32 "\","
               "\"age\":%d,\"pickup_delay\":%d,"
               "\"health\":%d,\"color\":%d,\"target_color\":%d}",
               orb->eid, orb->xpValue,
               double_bits(orb->posX - x), double_bits(orb->posY - y),
               double_bits(orb->posZ - z), double_bits(orb->motionX),
               double_bits(orb->motionY), double_bits(orb->motionZ),
               float_bits(orb->yaw), orb->xpOrbAge,
               orb->delayBeforeCanPickup, orb->health,
               orb->xpColor, orb->xpTargetColor);
    }
    printf("],\"world_events\":[");
    for (int index = 0; index < gm_runtime_world_event_count(&runtime);
            ++index) {
        GmRuntimeWorldEvent event;
        if (!gm_runtime_world_event_get(&runtime, index, &event)) continue;
        if (index) putchar(',');
        printf("{\"seq\":%d,\"id\":%d,\"x\":%d,\"y\":%d,"
               "\"z\":%d,\"data\":%d}",
               index, event.id, event.x - (int)floor(x),
               event.y - (int)floor(y), event.z - (int)floor(z),
               event.data);
    }
    printf("],\"world_seed48\":%" PRIu64
           ",\"math_seed48\":%" PRIu64
           ",\"next_entity_id\":%d}\n",
           runtime.world_random_seed48, runtime.math_random_seed48,
           runtime.next_entity_id);
    gm_runtime_destroy(&runtime);
    return emitted == count ? 0 : 1;
}
