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
    int block, tool, silk, fortune, xp = 0;
    uint64_t world_seed, block_seed;
    GmRuntime runtime;
    if (argc != 7 || !parse_int(argv[1], &block)
            || !parse_int(argv[2], &tool)
            || !parse_int(argv[3], &silk)
            || !parse_int(argv[4], &fortune)
            || !parse_seed(argv[5], &world_seed)
            || !parse_seed(argv[6], &block_seed)
            || silk > 32 || fortune > 32) {
        fprintf(stderr, "invalid block XP oracle arguments\n");
        return 2;
    }
    memset(&runtime, 0, sizeof runtime);
    runtime.world_random_seed48 = world_seed;
    runtime.block_random_seed48 = block_seed;
    if (!gm_runtime_harvest_xp_result(
            &runtime, block, tool, silk, fortune, &xp)) {
        fprintf(stderr, "unsupported block XP fixture\n");
        return 2;
    }
    printf("{\"ok\":true,\"xp\":%d,\"world_seed48\":%" PRIu64
           ",\"block_seed48\":%" PRIu64 "}\n",
           xp, runtime.world_random_seed48, runtime.block_random_seed48);
    return 0;
}
