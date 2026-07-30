/* CPU reference driver for nether_portal. Emits scenario outputs as raw-bits hex. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/nether_portal.h"

static void emit_u32(u32 v, void *ctx) {
    (void)ctx;
    printf("%08x\n", (unsigned)v);
}

static void emit_f32(float v, void *ctx) {
    u32 bits;
    (void)ctx;
    memcpy(&bits, &v, 4);
    printf("%08x\n", (unsigned)bits);
}

static void emit_f64(double v, void *ctx) {
    u64 bits;
    (void)ctx;
    memcpy(&bits, &v, 8);
    printf("%016llx\n", (unsigned long long)bits);
}

static void run_one(int idx) {
    NpScenarioResult r;
    np_run_scenario(idx, &r);
    np_emit_scenario(idx, &r, emit_u32, emit_f32, emit_f64, NULL);
}

int main(int argc, char **argv) {
    if (argc > 1) {
        run_one(atoi(argv[1]));
    } else {
        for (int i = 0; i < NP_NUM_SCENARIOS; ++i) run_one(i);
    }
    return 0;
}
