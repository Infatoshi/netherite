/* CPU reference driver for potion_effects_combat. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/potion_effects_combat.h"

static void emit_line(u64 bits, void *ctx) {
    (void)ctx;
    printf("%016llx\n", (unsigned long long)bits);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    pec_run_battery(emit_line, NULL);
    return 0;
}
