/* CPU reference driver for physics_collision_full. 12 doubles + 4 flags per scenario. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/physics_collision_full.h"

static void emit_double(double v) {
    u64 bits; memcpy(&bits, &v, 8);
    printf("%016llx\n", (unsigned long long)bits);
}

static void emit_flag(int v) {
    printf("%08x\n", (unsigned)v);
}

static void run_scenario(int idx) {
    McPcfEntity e;
    pcf_run_scenario(idx, &e);
    emit_double(e.posX);    emit_double(e.posY);    emit_double(e.posZ);
    emit_double(e.motionX); emit_double(e.motionY); emit_double(e.motionZ);
    emit_double(e.box.minX); emit_double(e.box.minY); emit_double(e.box.minZ);
    emit_double(e.box.maxX); emit_double(e.box.maxY); emit_double(e.box.maxZ);
    emit_flag(e.collidedHorizontally);
    emit_flag(e.collidedVertically);
    emit_flag(e.onGround);
    emit_flag(e.isInWeb);
}

int main(int argc, char **argv) {
    if (argc > 1) {
        run_scenario(atoi(argv[1]));
    } else {
        for (int i = 0; i < PCF_NUM_SCENARIOS; ++i) run_scenario(i);
    }
    return 0;
}
