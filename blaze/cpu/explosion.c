/* CPU reference driver for explosion. Prints raw-bits hex, one value per line. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/explosion.h"

static void emit_hex(u64 bits, void *ctx) {
    (void)ctx;
    printf("%016llx\n", (unsigned long long)bits);
}

int main(int argc, char **argv) {
    int sel = (argc > 1) ? atoi(argv[1]) : -1;
    u16 grid[EX_VOL];
    u8 bitset[EX_VOL];
    if (sel >= 0 && sel < EX_NUM_SCENARIOS) {
        ex_run_scenario(sel, grid, bitset, emit_hex, NULL);
    } else {
        ex_run_all(emit_hex, NULL);
    }
    return 0;
}
