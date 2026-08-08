#include "game/runtime.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_int(const char *text, int *out)
{
    char *end = NULL;
    long value = strtol(text, &end, 10);
    if (!text[0] || !end || *end || value < -32768 || value > 1000000000L)
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

static uint32_t float_bits(float value)
{
    union { float f; uint32_t u; } bits;
    bits.f = value;
    return bits.u;
}

static uint64_t double_bits(double value)
{
    union { double d; uint64_t u; } bits;
    bits.d = value;
    return bits.u;
}

static void print_stack(const ICStack *stack)
{
    printf("\"item\":%d,\"count\":%d,\"meta\":%d,"
           "\"repair_cost\":%d,\"enchants\":[",
           stack->item, stack->count, stack->meta, stack->repair_cost);
    for (int i = 0; i < stack->n_enchants; ++i)
        printf("%s[%d,%d]", i ? "," : "",
               stack->enchants[i].id, stack->enchants[i].level);
    printf("]");
}

static ICStack live_stack(const GmLiveEnt *entity)
{
    ICStack stack = ic_mk(entity->item, entity->count, entity->meta);
    stack.repair_cost = entity->repair_cost;
    stack.n_enchants = entity->n_enchants;
    for (int i = 0; i < stack.n_enchants && i < IC_MAX_ENCHANTS; ++i) {
        stack.enchants[i].id = entity->ench_id[i];
        stack.enchants[i].level = entity->ench_lvl[i];
    }
    return stack;
}

int main(int argc, char **argv)
{
    uint64_t player_seed, math_seed;
    int next_id, keep, count, arg = 6;
    GmConfig config;
    GmRuntime runtime;
    McGameRules rules;
    char error[256];
    if (argc < 6 || !parse_seed(argv[1], &player_seed)
            || !parse_seed(argv[2], &math_seed)
            || !parse_int(argv[3], &next_id)
            || !parse_int(argv[4], &keep)
            || !parse_int(argv[5], &count)
            || next_id <= 0 || (keep != 0 && keep != 1)
            || count < 0 || count > 41)
        return 2;
    gm_config_defaults(&config);
    config.seed = 42;
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.mobs = 0;
    config.weather = 0;
    if (!gm_runtime_init(&runtime, &config, error, sizeof error)) {
        fprintf(stderr, "%s\n", error);
        return 2;
    }
    memset(&runtime.entities, 0, sizeof runtime.entities);
    isr_init(&runtime.player.inv);
    gm_runtime_set_pose_state(
        &runtime, 8.5, 220.0, 8.5, 23.0F, -17.0F,
        0.0, 0.0, 0.0, 1, 0.0F);
    for (int i = 0; i < count; ++i) {
        int slot, item, stack_count, meta, repair, enchants;
        if (arg + 6 > argc || !parse_int(argv[arg++], &slot)
                || !parse_int(argv[arg++], &item)
                || !parse_int(argv[arg++], &stack_count)
                || !parse_int(argv[arg++], &meta)
                || !parse_int(argv[arg++], &repair)
                || !parse_int(argv[arg++], &enchants)
                || slot < 0 || slot > ISR_OFFHAND_SLOT
                || item <= 0 || stack_count <= 0
                || enchants < 0 || enchants > IC_MAX_ENCHANTS
                || arg + enchants * 2 > argc) {
            gm_runtime_destroy(&runtime);
            return 2;
        }
        ICStack stack = ic_mk(item, stack_count, meta);
        stack.repair_cost = repair;
        stack.n_enchants = enchants;
        for (int j = 0; j < enchants; ++j) {
            int id, level;
            if (!parse_int(argv[arg++], &id)
                    || !parse_int(argv[arg++], &level)) {
                gm_runtime_destroy(&runtime);
                return 2;
            }
            stack.enchants[j].id = (short)id;
            stack.enchants[j].level = (short)level;
        }
        isr_set_stack(&runtime.player.inv, slot, stack);
    }
    if (arg != argc) {
        gm_runtime_destroy(&runtime);
        return 2;
    }
    rules = runtime.gamerules;
    rules.keepInventory = keep;
    gm_runtime_set_gamerules(&runtime, &rules);
    jrand_set_seed48(&runtime.mobs.player_random, player_seed);
    gm_runtime_set_math_random_seed48(&runtime, math_seed);
    runtime.next_entity_id = next_id;
    (void)gm_runtime_player_death_inventory(&runtime);

    printf("{\"ok\":true,\"drops\":[");
    int comma = 0;
    for (int eid = next_id; eid < runtime.next_entity_id; ++eid)
        for (int i = 0; i < GM_LIVE_MAX; ++i) {
            const GmLiveEnt *entity = &runtime.entities.ents[i];
            if (!entity->active || entity->type != 0 || entity->eid != eid)
                continue;
            ICStack stack = live_stack(entity);
            printf("%s{", comma ? "," : "");
            print_stack(&stack);
            printf(",\"eid\":%d,\"position_bits\":["
                   "\"%016" PRIx64 "\",\"%016" PRIx64
                   "\",\"%016" PRIx64 "\"],"
                   "\"motion_bits\":[\"%016" PRIx64
                   "\",\"%016" PRIx64 "\",\"%016" PRIx64
                   "\"],\"yaw_bits\":\"%08" PRIx32
                   "\",\"hover_start_bits\":\"%08" PRIx32
                   "\",\"age\":%d,\"pickup_delay\":%d}",
                   entity->eid,
                   double_bits(entity->x), double_bits(entity->y),
                   double_bits(entity->z), double_bits(entity->mx),
                   double_bits(entity->my), double_bits(entity->mz),
                   float_bits(entity->yaw), float_bits(entity->hover_start),
                   entity->age, entity->pickup_delay);
            comma = 1;
        }
    printf("],\"inventory\":[");
    comma = 0;
    for (int slot = 0; slot <= ISR_OFFHAND_SLOT; ++slot) {
        ICStack stack = isr_get_stack(&runtime.player.inv, slot);
        if (isr_is_empty(&stack)) continue;
        printf("%s{", comma ? "," : "");
        print_stack(&stack);
        printf(",\"slot\":%d}", slot);
        comma = 1;
    }
    printf("],\"player_seed48\":%" PRIu64
           ",\"math_seed48\":%" PRIu64
           ",\"next_entity_id\":%d}\n",
           runtime.mobs.player_random.seed,
           runtime.math_random_seed48, runtime.next_entity_id);
    gm_runtime_destroy(&runtime);
    return 0;
}
