/* CPU reference: furnace smelt/fuel progress dump each tick. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/furnace_full_tick.h"

int main(int argc, char **argv) {
    u64 seed = (argc > 1) ? (u64)strtoull(argv[1], 0, 10) : 12345ULL;
    int nticks = (argc > 2) ? atoi(argv[2]) : FFT_NUM_TICKS;
    FftFurnace f;
    u64 *out = (u64 *)malloc(sizeof(u64) * (size_t)nticks * FFT_DUMP_FIELDS);
    int i;

    fft_run(&f, seed, nticks, out);
    for (i = 0; i < nticks * FFT_DUMP_FIELDS; ++i)
        printf("%016llx\n", (unsigned long long)out[i]);
    free(out);
    return 0;
}
