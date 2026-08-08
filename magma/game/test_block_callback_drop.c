#include "mc_rng.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    static const char *const owners[] = {
        "BlockJukebox", "BlockMobSpawner",
        "BlockPistonMoving", "BlockSilverfish"
    };
    static const int blocks[] = {84, 52, 36, 97};
    FILE *input;
    char line[256], owner[64];
    long long probe;
    JavaRandom random;
    if (argc != 2 || !(input = fopen(argv[1], "r"))) return 2;
    for (int row = 0; row < 4; ++row) {
        int block;
        if (!fgets(line, sizeof line, input)
                || sscanf(line, "D %63s %d %lld", owner, &block, &probe) != 3
                || strcmp(owner, owners[row]) || block != blocks[row])
            return 1;
        jrand_set(&random, 1234);
        if (row == 0) (void)jrand_float(&random);
        if ((int64_t)probe != jrand_long(&random)) return 1;
    }
    if (fgets(line, sizeof line, input)) return 1;
    fclose(input);
    puts("block callback drops: PASS (four live Java/native RNG controls)");
    return 0;
}
