/* CPU reference driver for the smoke kernel. Prints N hex lines to stdout. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/smoke_core.h"

int main(int argc, char **argv) {
    u64 seed = (argc > 1) ? strtoull(argv[1], 0, 10) : 12345ULL;
    int n    = (argc > 2) ? atoi(argv[2]) : 256;
    u64 *out = (u64 *)malloc(sizeof(u64) * n);
    smoke_kernel(seed, out, n);
    for (int i = 0; i < n; i++) printf("%016llx\n", (unsigned long long)out[i]);
    free(out);
    return 0;
}
