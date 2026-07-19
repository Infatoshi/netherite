/* CPU reference: progressive block break battery. Args: [nticks] (default PB_NTICKS=160).
 * Dumps raw hex u64 lines: for each of 12 scenarios x nticks: progress_float_bits, harvest. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/player_break.h"

int main(int argc, char **argv) {
    int nticks = (argc > 1) ? atoi(argv[1]) : PB_NTICKS;
    if (nticks < 1) nticks = PB_NTICKS;
    int nout = PB_NOUT(nticks);
    u64 *out = (u64 *)malloc(sizeof(u64) * (size_t)nout);
    if (!out) return 1;
    pb_run_battery(nticks, out);
    for (int i = 0; i < nout; ++i)
        printf("%016llx\n", (unsigned long long)out[i]);
    free(out);
    return 0;
}
