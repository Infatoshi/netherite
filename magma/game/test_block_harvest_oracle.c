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
    int block, meta, tool, silk, fortune;
    int item = 0, count = 0, item_meta = 0;
    uint64_t seed;
    GmRuntime runtime;
    if (argc != 7 || !parse_int(argv[1], &block)
            || !parse_int(argv[2], &meta)
            || !parse_int(argv[3], &tool)
            || !parse_int(argv[4], &silk)
            || !parse_int(argv[5], &fortune)
            || !parse_seed(argv[6], &seed)
            || meta > 15 || silk > 32 || fortune > 32) {
        fprintf(stderr, "invalid block harvest oracle arguments\n");
        return 2;
    }
    memset(&runtime, 0, sizeof runtime);
    runtime.world_random_seed48 = seed;
    if (!gm_runtime_harvest_drop_result(
            &runtime, block, meta, tool, silk, fortune,
            &item, &count, &item_meta)) {
        fprintf(stderr, "unsupported block harvest fixture\n");
        return 2;
    }
    printf("{\"ok\":true,\"drops\":[");
    for (int i = 0; i < count; ++i)
        printf("%s[%d,1,%d]", i ? "," : "", item, item_meta);
    printf("],\"world_seed48\":%" PRIu64 "}\n",
           runtime.world_random_seed48);
    return 0;
}
