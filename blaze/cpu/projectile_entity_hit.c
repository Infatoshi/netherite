/* CPU reference driver for projectile_entity_hit. Emits per-scenario hit outcome hex. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/projectile_entity_hit.h"

static void emit_hex(u64 bits, void *ctx) {
    (void)ctx;
    printf("%016llx\n", (unsigned long long)bits);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    if (argc > 2) {
        peh_run_scenario(atoi(argv[2]), seed, emit_hex, NULL);
    } else {
        peh_run_all(seed, emit_hex, NULL);
    }
    return 0;
}
