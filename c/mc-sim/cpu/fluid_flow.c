/* CPU reference: fluid_flow double-buffer CA scenes. Prints packed block-state u16 as %04x lines. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/fluid_flow.h"

static void emit_hex(u16 s, void *ctx) {
    (void)ctx;
    printf("%04x\n", (unsigned)s);
}

static void run_scene(u16 *cur, u16 *tmp, int nx, int ny, int nz, int iters) {
    ff_ca_run(cur, tmp, nx, ny, nz, iters);
    ff_dump(cur, nx, ny, nz, emit_hex, NULL);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    int spring_iters = 5 + (int)(seed % 6);

    {
        int vol = FF_DIM_WB_X * FF_DIM_WB_Y * FF_DIM_WB_Z;
        u16 *cur = (u16 *)malloc(sizeof(u16) * vol);
        u16 *tmp = (u16 *)malloc(sizeof(u16) * vol);
        ff_init_water_bucket(cur);
        run_scene(cur, tmp, FF_DIM_WB_X, FF_DIM_WB_Y, FF_DIM_WB_Z, 64);
        free(cur);
        free(tmp);
    }

    {
        int vol = FF_DIM_SS_X * FF_DIM_SS_Y * FF_DIM_SS_Z;
        u16 *cur = (u16 *)malloc(sizeof(u16) * vol);
        u16 *tmp = (u16 *)malloc(sizeof(u16) * vol);
        ff_init_spring_spread(cur);
        run_scene(cur, tmp, FF_DIM_SS_X, FF_DIM_SS_Y, FF_DIM_SS_Z, spring_iters);
        free(cur);
        free(tmp);
    }

    return 0;
}
