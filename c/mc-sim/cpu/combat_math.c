/* CPU reference driver for combat_math. Emits weapon x armor damage matrix as float raw-bits hex. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/combat_math.h"

static void emit_float(float v) {
    u32 bits; memcpy(&bits, &v, 4);
    printf("%08x\n", (unsigned)bits);
}

static void run_scenario(int idx) {
    emit_float(mc_combat_scenario_damage(idx));
}

int main(int argc, char **argv) {
    if (argc > 1) {
        run_scenario(atoi(argv[1]));
    } else {
        for (int i = 0; i < MC_CM_NUM_SCENARIOS; ++i) run_scenario(i);
    }
    return 0;
}
