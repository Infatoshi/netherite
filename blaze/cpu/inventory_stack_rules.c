/* CPU reference: inventory_stack_rules hex scenario battery. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/inventory_stack_rules.h"

static void emit_u32(u32 v) {
    printf("%08x\n", (unsigned)v);
}

static void run_one(int idx) {
    u32 out[ISR_FIELDS_PER];
    isr_run_scenario(idx, out);
    for (int i = 0; i < ISR_FIELDS_PER; ++i) emit_u32(out[i]);
}

int main(int argc, char **argv) {
    if (argc > 1) {
        run_one(atoi(argv[1]));
    } else {
        u32 out[ISR_OUT];
        isr_run_battery(out);
        for (int i = 0; i < ISR_OUT; ++i) emit_u32(out[i]);
    }
    return 0;
}
