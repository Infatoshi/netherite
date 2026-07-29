/* CPU reference driver for item_bucket_world. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/item_bucket_world.h"

static void run_seed(u64 seed) {
    World w;
    u16 cur[IBW_SLICE_VOL];
    u16 tmp[IBW_SLICE_VOL];
    u64 out[IBW_OUT];
    ibw_run(seed, out, &w, cur, tmp);
    for (int i = 0; i < IBW_OUT; ++i)
        printf("%016llx\n", (unsigned long long)out[i]);
}

int main(int argc, char **argv) {
    static const u64 k_seeds[] = {12345ULL, 0ULL, 7ULL};
    int i;

    if (argc > 1) {
        run_seed(strtoull(argv[1], 0, 10));
    } else {
        for (i = 0; i < 3; ++i) run_seed(k_seeds[i]);
    }
    return 0;
}
