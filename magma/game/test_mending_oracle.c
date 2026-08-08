#include "game/runtime.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_int(const char *text, int *out)
{
    char *end = NULL;
    long value = strtol(text, &end, 10);
    if (!text[0] || !end || *end || value < 0 || value > 32767)
        return 0;
    *out = (int)value;
    return 1;
}

static int parse_seed(const char *text, uint64_t *out)
{
    char *end = NULL;
    unsigned long long value = strtoull(text, &end, 10);
    if (!text[0] || !end || *end || value >= (UINT64_C(1) << 48))
        return 0;
    *out = (uint64_t)value;
    return 1;
}

int main(int argc, char **argv)
{
    static const int slots[6] = {
        0, ISR_OFFHAND_SLOT, ISR_ARMOR_FEET, ISR_ARMOR_LEGS,
        ISR_ARMOR_CHEST, ISR_ARMOR_HEAD
    };
    static const char *names[6] = {
        "mainhand", "offhand", "feet", "legs", "chest", "head"
    };
    GmConfig cfg;
    GmRuntime runtime;
    GmAction idle;
    char error[256];
    uint64_t seed;
    int value, cooldown, delay, at = 1;
    int items[6], metas[6], mending[6];
    if (argc != 23 || !parse_seed(argv[at++], &seed)
            || !parse_int(argv[at++], &value)
            || !parse_int(argv[at++], &cooldown)
            || !parse_int(argv[at++], &delay)
            || value <= 0 || cooldown > 100 || delay > 100) {
        fprintf(stderr, "invalid Mending oracle arguments\n");
        return 2;
    }
    for (int i = 0; i < 6; ++i)
        if (!parse_int(argv[at++], &items[i])
                || !parse_int(argv[at++], &metas[i])
                || !parse_int(argv[at++], &mending[i])
                || items[i] > 4095 || (mending[i] != 0 && mending[i] != 1)) {
            fprintf(stderr, "invalid Mending equipment\n");
            return 2;
        }
    gm_config_defaults(&cfg);
    cfg.seed = 42;
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.view_distance = 1;
    cfg.mobs = 0;
    cfg.weather = 0;
    if (!gm_runtime_init(&runtime, &cfg, error, sizeof error)) {
        fprintf(stderr, "%s\n", error);
        return 2;
    }
    gm_runtime_set_pose(&runtime, 8.5, 5.0, 8.5, 0.0F, 0.0F);
    runtime.player.inv.current_item = 0;
    for (int i = 0; i < 6; ++i) {
        ICStack stack = items[i] == 0
            ? ic_empty() : ic_mk(items[i], 1, metas[i]);
        if (mending[i]) {
            stack.n_enchants = 1;
            stack.enchants[0].id = 70;
            stack.enchants[0].level = 1;
        }
        isr_set_stack(&runtime.player.inv, slots[i], stack);
    }
    jrand_set_seed48(&runtime.mobs.player_random, seed);
    /* The live tick decrements EntityPlayer.xpCooldown before entity
     * collisions. The CLI argument names the value at collision time. */
    runtime.mobs.player_xp_cooldown = cooldown > 0 ? cooldown + 1 : 0;
    runtime.mobs.xp_total = 0;
    runtime.player_xp_total = 0;
    runtime.player_xp_level = 0;
    runtime.player_xp_frac = 0.0F;
    if (!gm_runtime_spawn_xp_fixture(
            &runtime, 8.5, 5.0, 8.5, 0.0, 0.0, 0.0,
            value, 88001, 0, delay > 0 ? delay + 1 : 0, 0, 0)) {
        gm_runtime_destroy(&runtime);
        fprintf(stderr, "could not spawn XP fixture\n");
        return 2;
    }
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    gm_runtime_tick(&runtime, idle);
    printf("{\"ok\":true,\"picked\":%s,\"orb_value\":%d,"
           "\"xp_total\":%d,\"xp_cooldown\":%d,"
           "\"player_seed48\":%" PRIu64 ",\"equipment\":{",
           runtime.mobs.xp_orbs[0].dead ? "true" : "false",
           runtime.mobs.xp_orbs[0].xpValue,
           runtime.player_xp_total, runtime.mobs.player_xp_cooldown,
           runtime.mobs.player_random.seed);
    for (int i = 0; i < 6; ++i) {
        ICStack stack = isr_get_stack(&runtime.player.inv, slots[i]);
        printf("%s\"%s\":{\"item\":%d,\"meta\":%d}",
               i ? "," : "", names[i], stack.item, stack.meta);
    }
    puts("}}");
    gm_runtime_destroy(&runtime);
    return 0;
}
