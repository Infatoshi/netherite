/* CPU reference: fixed End arena, ED_NUM_TICKS dragon ticks, dump state each tick. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/ender_dragon.h"

int main(int argc, char **argv) {
    u64 seed = (argc > 1) ? (u64)strtoull(argv[1], 0, 10) : 12345ULL;
    int nticks = (argc > 2) ? atoi(argv[2]) : ED_NUM_TICKS;

    McSinTable st;
    mc_sin_table_init(&st);

    EdArena arena;
    u64 *out = (u64 *)malloc(sizeof(u64) * (size_t)nticks * ED_DUMP_FIELDS);
    ed_run(&arena, &st, seed, nticks, out);

    for (int i = 0; i < nticks * ED_DUMP_FIELDS; ++i)
        printf("%016llx\n", (unsigned long long)out[i]);

    free(out);
    return 0;
}
