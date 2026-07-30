/* CPU reference driver for pathfinding. Synthetic 16x16x32 scenes; emits waypoints as %08x hex. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/pathfinding.h"

static void emit_hex(u32 v, void *ctx) {
    (void)ctx;
    printf("%08x\n", (unsigned)v);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    PfWork work;
    pf_run_all(seed, &work, emit_hex, NULL);
    return 0;
}
