/* CPU trunk smoke: fill a chunk via the trunk headers, print the rolling hash. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/trunk_core.h"

int main(int argc, char **argv) {
    u64 seed = (argc > 1) ? strtoull(argv[1], 0, 10) : 12345ULL;
    Chunk *c = (Chunk *)malloc(sizeof(Chunk));
    u64 h = mc_trunk_fill(c, seed);
    printf("%016llx\n", (unsigned long long)h);
    free(c);
    return 0;
}
