/* CPU reference: synthetic 16x16x64 light scene, fixpoint CA, dump packed light u8 lines. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/light_propagation.h"

static void emit_hex(u8 packed, void *ctx) {
    (void)ctx;
    u64 bits = (u64)packed;
    printf("%016llx\n", (unsigned long long)bits);
}

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    u16 *blocks = (u16 *)malloc(sizeof(u16) * LP_VOL);
    u8 *sky = (u8 *)malloc(LP_VOL);
    u8 *blk = (u8 *)malloc(LP_VOL);
    u8 *tmp_sky = (u8 *)malloc(LP_VOL);
    u8 *tmp_blk = (u8 *)malloc(LP_VOL);

    lp_init_scene(blocks, seed);
    memset(sky, 0, LP_VOL);
    memset(blk, 0, LP_VOL);
    lp_propagate(sky, blk, tmp_sky, tmp_blk, blocks, 128);
    lp_dump_light(sky, blk, emit_hex, NULL);

    free(tmp_blk);
    free(tmp_sky);
    free(blk);
    free(sky);
    free(blocks);
    return 0;
}
