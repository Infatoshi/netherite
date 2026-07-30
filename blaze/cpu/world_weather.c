/* CPU reference driver for world_weather. Args: [seed [nticks]].
 * Dumps WW_FIELDS u64s per tick as %016llx (totalTime worldTime rainTime
 * thunderTime raining thundering). */
#include <stdio.h>
#include <stdlib.h>
#include "../core/world_weather.h"

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    i32 nticks = (argc > 2) ? (i32)strtol(argv[2], 0, 10) : WW_NTICKS;
    WwState s;
    u64 *out;
    i32 i, n;

    if (nticks < 1) nticks = WW_NTICKS;
    n = nticks * WW_FIELDS;
    out = (u64 *)malloc(sizeof(u64) * (size_t)n);
    if (!out) return 1;

    ww_run(&s, seed, nticks, out);
    for (i = 0; i < n; ++i)
        printf("%016llx\n", (unsigned long long)out[i]);
    free(out);
    return 0;
}
