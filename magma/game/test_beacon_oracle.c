#include "game/runtime.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    int levels, primary, secondary, obstruction;
    double dx, dy, dz;
    int glass_count;
    int glass_meta[4];
    int glass_pane[4];
} BeaconCase;

static const BeaconCase cases[] = {
    {0, 1, 0, 0, 0.5, 0.0, -2.0, 0, {0}, {0}},
    {1, 1, 0, 0, 0.5, 0.0, -2.0, 0, {0}, {0}},
    {2, 3, 0, 0, 0.5, 0.0, -2.0, 0, {0}, {0}},
    {3, 5, 10, 0, 0.5, 0.0, -2.0, 0, {0}, {0}},
    {4, 1, 1, 0, 0.5, 0.0, -2.0, 0, {0}, {0}},
    {4, 5, 10, 0, 0.5, 0.0, -2.0, 0, {0}, {0}},
    {4, 1, 0, 0, 0.5, 0.0, -2.0, 2, {1, 11}, {0, 1}},
    {4, 1, 0, 1, 0.5, 0.0, -2.0, 1, {14}, {0}},
    {4, 11, 0, 7, 0.5, 0.0, -2.0, 1, {15}, {1}},
    {4, 1, 0, 0, 53.0, 0.0, 0.5, 0, {0}, {0}},
    {4, 2, 10, 0, 0.5, 0.0, -2.0, 0, {0}, {0}},
    {4, 3, 0, 0, 0.5, 0.0, -2.0, 3, {14, 14, 0}, {0, 1, 0}},
};

static uint32_t float_bits(float value)
{
    union { float f; uint32_t u; } bits;
    bits.f = value;
    return bits.u;
}

static int build_pyramid(
        GmRuntime *runtime, int x, int y, int z, int levels)
{
    for (int level = 1; level <= levels; ++level)
        for (int bx = x - level; bx <= x + level; ++bx)
            for (int bz = z - level; bz <= z + level; ++bz)
                if (!gm_runtime_set_block(runtime, bx, y - level, bz,
                                          level & 1 ? 42 : 57, 0))
                    return 0;
    return 1;
}

int main(int argc, char **argv)
{
    GmConfig config;
    GmRuntime runtime;
    GmRuntimeStaticContainer beacon;
    char error[256];
    int index;
    const int x = 8, y = 200, z = 8;
    if (argc != 2) return 2;
    index = atoi(argv[1]);
    if (index < 0 || index >= (int)(sizeof cases / sizeof cases[0]))
        return 2;
    const BeaconCase *test = &cases[index];
    gm_config_defaults(&config);
    config.seed = 42;
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.render = GM_RENDER_OFF;
    config.mobs = 0;
    config.weather = 0;
    if (!gm_runtime_init(&runtime, &config, error, sizeof error)) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    runtime.randtick_enabled = 0;
    gm_runtime_set_pose(
        &runtime, x + test->dx, y + test->dy,
        z + test->dz, 180.0F, 0.0F);
    if (!build_pyramid(&runtime, x, y, z, test->levels)
            || !gm_runtime_set_block(&runtime, x, y, z, 138, 0)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    for (int glass = 0; glass < test->glass_count; ++glass)
        if (!gm_runtime_set_block(
                &runtime, x, y + glass + 1, z,
                test->glass_pane[glass] ? 160 : 95,
                test->glass_meta[glass])) {
            gm_runtime_destroy(&runtime);
            return 1;
        }
    if (test->obstruction != 0
            && !gm_runtime_set_block(
                &runtime, x, y + test->glass_count + 1, z,
                test->obstruction, 0)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    if (!gm_runtime_beacon_set_state(
            &runtime, 0, x, y, z, -1,
            gm_runtime_beacon_valid_effect(test->primary)
                ? test->primary : 0,
            gm_runtime_beacon_valid_effect(test->secondary)
                ? test->secondary : 0,
            0)
            || !gm_runtime_beacon_update(&runtime, 0, x, y, z)
            || !gm_runtime_beacon_get(&runtime, 0, x, y, z, &beacon)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    printf("{\"ok\":true,\"activated\":true,\"levels\":%d,\"primary\":%d,"
           "\"secondary\":%d,\"complete\":%s,\"segments\":[",
           beacon.beacon_levels, beacon.beacon_primary,
           beacon.beacon_secondary,
           beacon.beacon_complete ? "true" : "false");
    for (int segment = 0;
            segment < beacon.beacon_segment_count; ++segment) {
        const GmRuntimeBeaconSegment *value =
            &beacon.beacon_segments[segment];
        if (segment) putchar(',');
        printf("{\"red_bits\":\"%08" PRIx32
               "\",\"green_bits\":\"%08" PRIx32
               "\",\"blue_bits\":\"%08" PRIx32
               "\",\"height\":%d}",
               float_bits(value->red), float_bits(value->green),
               float_bits(value->blue), value->height);
    }
    printf("],\"effects\":[");
    int effect_order[GM_MAX_POTION_EFFECTS];
    for (int effect = 0; effect < runtime.potion_count; ++effect)
        effect_order[effect] = effect;
    for (int left = 0; left < runtime.potion_count; ++left)
        for (int right = left + 1; right < runtime.potion_count; ++right)
            if (runtime.potions[effect_order[right]].id
                    < runtime.potions[effect_order[left]].id) {
                int swap = effect_order[left];
                effect_order[left] = effect_order[right];
                effect_order[right] = swap;
            }
    for (int effect = 0; effect < runtime.potion_count; ++effect) {
        const GmPotionEffectView *value =
            &runtime.potions[effect_order[effect]];
        if (effect) putchar(',');
        printf("{\"id\":%d,\"amplifier\":%d,\"duration\":%d}",
               value->id, value->amplifier, value->duration);
    }
    printf("],\"payment_acceptance\":["
           "{\"item\":388,\"accepted\":true},"
           "{\"item\":264,\"accepted\":true},"
           "{\"item\":266,\"accepted\":true},"
           "{\"item\":265,\"accepted\":true},"
           "{\"item\":263,\"accepted\":false}],"
           "\"payment_limit\":1,\"payment_nbt_present\":false,"
           "\"nbt_levels\":%d,\"nbt_primary\":%d,"
           "\"nbt_secondary\":%d}\n",
           beacon.beacon_levels, beacon.beacon_primary,
           beacon.beacon_secondary);
    gm_runtime_destroy(&runtime);
    return 0;
}
