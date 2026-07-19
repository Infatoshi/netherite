/* CPU reference driver for mob_ai_spider. 64 ticks on synthetic wall world; per tick emits:
 *   state (%08x), x/y/z/yaw (%016llx each), attack_time (%08x), path_idx (%08x),
 *   climbing (%08x), on_ground (%08x), did_leap (%08x). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/mob_ai_spider.h"

static void emit_u32(u32 v) {
    printf("%08x\n", (unsigned)v);
}

static void emit_double(double v) {
    u64 bits;
    memcpy(&bits, &v, 8);
    printf("%016llx\n", (unsigned long long)bits);
}

static void emit_tick(const MasTickOut *o) {
    emit_u32(o->state);
    emit_double(o->x);
    emit_double(o->y);
    emit_double(o->z);
    emit_double(o->yaw);
    emit_u32(o->attack_time);
    emit_u32(o->path_idx);
    emit_u32(o->climbing);
    emit_u32(o->on_ground);
    emit_u32(o->did_leap);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    int nticks = (argc > 2) ? atoi(argv[2]) : MAS_NUM_TICKS;

    PfWork work;
    MasTickOut *out = (MasTickOut *)malloc(sizeof(MasTickOut) * (size_t)nticks);
    mas_run(seed, nticks, out, &work);

    for (int t = 0; t < nticks; ++t)
        emit_tick(&out[t]);

    free(out);
    return 0;
}
