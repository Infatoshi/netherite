/* CPU reference: 17x17 flat world hostile spawn cycle -> hex spawn decisions. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/mob_spawning_world.h"

int main(int argc, char **argv) {
    u64 seed = (argc > 1) ? (u64)strtoull(argv[1], 0, 10) : 12345ULL;
    i64 tick = (argc > 2) ? (i64)strtoll(argv[2], 0, 10) : 100LL;
    MswScene *scene = (MswScene *)malloc(sizeof(MswScene));
    u8 *tmp_sky = (u8 *)malloc(MSW_VOL);
    u8 *tmp_blk = (u8 *)malloc(MSW_VOL);
    int i;

    if (!scene || !tmp_sky || !tmp_blk) return 1;

    msw_init(scene, seed, tmp_sky, tmp_blk);
    msw_run(scene, tick, tmp_sky, tmp_blk);

    for (i = 0; i < scene->n_decisions; ++i)
        printf("%016llx\n", (unsigned long long)scene->decisions[i]);

    free(tmp_blk);
    free(tmp_sky);
    free(scene);
    return 0;
}
