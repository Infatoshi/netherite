#include <stdio.h>
#include <stdlib.h>
#include "../core/map_gen_stronghold.h"

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    ChunkPrimer *p = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    sh_run(p, seed, 0, 0);
    for (int i = 0; i < 65536; ++i)
        printf("%04x\n", (unsigned)p->data[i]);
    free(p);
    return 0;
}
