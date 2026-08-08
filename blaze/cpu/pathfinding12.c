/* CPU reference driver for the verbatim 1.11.2 pathfinding port. Emits the battery as %08x hex. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/pathfinding12.h"

static void emit_hex(u32 v, void *ctx) {
    (void)ctx;
    printf("%08x\n", (unsigned)v);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    Pf12 *p = (Pf12 *)malloc(sizeof(Pf12));
    if (!p) { fprintf(stderr, "alloc failed\n"); return 1; }
    pf12_run_all(seed, p, emit_hex, NULL);
    free(p);
    return 0;
}
