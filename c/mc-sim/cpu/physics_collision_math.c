/* CPU reference driver for physics_collision_math. Runs the baked Entity.move collision scenarios
 * and prints the resolved state as raw bits: 12 doubles (%016llx) then 3 flags (%08x) per scenario.
 * No arg = all scenarios in fixed order; argv[1] = a single scenario index. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/physics_collision_math.h"

static void emit_double(double v) {
    u64 bits; memcpy(&bits, &v, 8);
    printf("%016llx\n", (unsigned long long)bits);
}

static void emit_flag(int v) {
    printf("%08x\n", (unsigned)v);
}

static void run_scenario(int idx) {
    McEntity e;
    double dx, dy, dz;
    McAABB blocks[MC_PCM_MAX_BLOCKS];
    int n = mc_pcm_scenario(idx, &e, &dx, &dy, &dz, blocks);
    mc_entity_move(&e, dx, dy, dz, blocks, n);
    emit_double(e.posX);    emit_double(e.posY);    emit_double(e.posZ);
    emit_double(e.motionX); emit_double(e.motionY); emit_double(e.motionZ);
    emit_double(e.box.minX); emit_double(e.box.minY); emit_double(e.box.minZ);
    emit_double(e.box.maxX); emit_double(e.box.maxY); emit_double(e.box.maxZ);
    emit_flag(e.collidedHorizontally);
    emit_flag(e.collidedVertically);
    emit_flag(e.onGround);
}

int main(int argc, char **argv) {
    if (argc > 1) {
        run_scenario(atoi(argv[1]));
    } else {
        for (int i = 0; i < MC_PCM_NUM_SCENARIOS; ++i) run_scenario(i);
    }
    return 0;
}
