/* CPU reference driver for projectile_motion. Synthetic arrow scenes; pos per tick hex. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/projectile_motion.h"

static void emit_hex(u64 bits, void *ctx) {
    (void)ctx;
    printf("%016llx\n", (unsigned long long)bits);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    pm_run_all(seed, emit_hex, NULL);
    return 0;
}
