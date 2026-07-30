/* CPU reference: nether terrain + Teleporter.makePortal, raw-bits hex per scenario. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/nether_portal_make.h"

static void emit_u32(u32 v, void *ctx) {
    (void)ctx;
    printf("%08x\n", (unsigned)v);
}

static void run_one(int idx) {
    NpmWorld *w = (NpmWorld *)malloc(sizeof(NpmWorld));
    CpnHellScratch *sc = (CpnHellScratch *)malloc(sizeof(CpnHellScratch));
    CpnHellNoise *noise = (CpnHellNoise *)malloc(sizeof(CpnHellNoise));
    McSinTable *st = (McSinTable *)malloc(sizeof(McSinTable));
    NpmResult r;

    mc_sin_table_init(st);
    npm_run_scenario(idx, w, sc, st, noise, &r);
    npm_emit_result(&r, emit_u32, NULL);

    free(st); free(noise); free(sc); free(w);
}

int main(int argc, char **argv) {
    if (argc > 1) {
        run_one(atoi(argv[1]));
    } else {
        for (int i = 0; i < NPM_NUM_SCENARIOS; ++i) run_one(i);
    }
    return 0;
}
