/* CPU reference: overworld terrain + BlockPortal frame detect / trySpawnPortal, hex per scenario. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/nether_portal_world.h"

static void emit_u32(u32 v, void *ctx) {
    (void)ctx;
    printf("%08x\n", (unsigned)v);
}

static void run_one(int idx) {
    NpwWorld *w = (NpwWorld *)malloc(sizeof(NpwWorld));
    CpScratch *sc = (CpScratch *)malloc(sizeof(CpScratch));
    McSinTable *st = (McSinTable *)malloc(sizeof(McSinTable));
    NpwResult r;

    mc_sin_table_init(st);
    npw_run_scenario(idx, w, sc, st, &r);
    npw_emit_result(&r, emit_u32, NULL);

    free(st); free(sc); free(w);
}

int main(int argc, char **argv) {
    if (argc > 1) {
        run_one(atoi(argv[1]));
    } else {
        for (int i = 0; i < NPW_NUM_SCENARIOS; ++i) run_one(i);
    }
    return 0;
}
