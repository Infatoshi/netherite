/* CPU reference driver for difficulty_scale. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/difficulty_scale.h"

static void emit_line(u64 bits, void *ctx) {
    (void)ctx;
    printf("%016llx\n", (unsigned long long)bits);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    ds_run_battery(emit_line, NULL);
    return 0;
}
