#include "items_tools_armor.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

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
    int block, item, damage, unbreaking;
    uint64_t seed;
    if (argc != 6 || !parse_int(argv[1], &block)
            || !parse_int(argv[2], &item)
            || !parse_int(argv[3], &damage)
            || !parse_int(argv[4], &unbreaking)
            || !parse_seed(argv[5], &seed) || unbreaking > 32) {
        fprintf(stderr, "invalid block tool oracle arguments\n");
        return 2;
    }
    JavaRandom random;
    jrand_set_seed48(&random, seed);
    ITAStack tool = ita_mk(item, damage);
    tool.unbreaking = unbreaking;
    int wear = ita_block_destroy_damage(&tool, block);
    int broke = ita_attempt_damage(&tool, wear, &random);
    if (broke)
        for (int i = 0; i < 15; ++i)
            (void)jrand_float(&random);
    printf("{\"ok\":true,\"item\":%d,\"count\":%d,"
           "\"damage\":%d,\"player_seed48\":%" PRIu64 "}\n",
           broke ? 0 : item, broke ? 0 : 1, broke ? 0 : tool.damage,
           (uint64_t)random.seed);
    return 0;
}
