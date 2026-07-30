/* CPU reference: ender_dragon contact damage + block break subset. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/ender_dragon_damage.h"

int main(int argc, char **argv) {
    u64 seed = (argc > 1) ? (u64)strtoull(argv[1], 0, 10) : 12345ULL;
    int nticks = (argc > 2) ? atoi(argv[2]) : EDD_NUM_TICKS;

    McSinTable st;
    mc_sin_table_init(&st);

    EddWorld world;
    u64 *out = (u64 *)malloc(sizeof(u64) * (size_t)nticks * EDD_DUMP_FIELDS);
    edd_run(&world, &st, seed, nticks, out);

    for (int i = 0; i < nticks * EDD_DUMP_FIELDS; ++i)
        printf("%016llx\n", (unsigned long long)out[i]);

    free(out);
    return 0;
}
