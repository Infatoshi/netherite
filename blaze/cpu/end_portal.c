/* CPU reference: end portal frame ring + seeded eye insertion -> dump. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/end_portal.h"

int main(int argc, char **argv) {
    u64 seed = (argc > 1) ? (u64)strtoull(argv[1], 0, 10) : 12345ULL;
    EpWorld w;
    w.seed = seed;
    ep_run(&w);
    u64 out[EP_NOUT];
    ep_dump(&w, out);
    for (int i = 0; i < EP_NOUT; ++i)
        printf("%016llx\n", (unsigned long long)out[i]);
    return 0;
}
