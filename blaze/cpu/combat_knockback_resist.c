/* CPU reference driver for combat_knockback_resist. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/combat_knockback_resist.h"

static void emit_line(u64 bits, void *ctx) {
    (void)ctx;
    printf("%016llx\n", (unsigned long long)bits);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    mc_ckr_run_battery(emit_line, NULL);
    return 0;
}
