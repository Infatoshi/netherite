/* CPU reference driver for mob_ai_zombie_astar. 64 ticks on synthetic flat world; per tick emits:
 *   state (%08x), x/y/z/yaw (%016llx each), attack_time (%08x), path_idx (%08x). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/mob_ai_zombie_astar.h"

static void emit_u32(u32 v) {
    printf("%08x\n", (unsigned)v);
}

static void emit_double(double v) {
    u64 bits;
    memcpy(&bits, &v, 8);
    printf("%016llx\n", (unsigned long long)bits);
}

static void emit_tick(const MazTickOut *o) {
    emit_u32(o->state);
    emit_double(o->x);
    emit_double(o->y);
    emit_double(o->z);
    emit_double(o->yaw);
    emit_u32(o->attack_time);
    emit_u32(o->path_idx);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    int nticks = (argc > 2) ? atoi(argv[2]) : MAZ_NUM_TICKS;

    PfWork work;
    MazTickOut *out = (MazTickOut *)malloc(sizeof(MazTickOut) * (size_t)nticks);
    maz_run(seed, nticks, out, &work);

    for (int t = 0; t < nticks; ++t)
        emit_tick(&out[t]);

    free(out);
    return 0;
}
