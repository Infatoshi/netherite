/* CPU reference: MOB_SPAWNER tick loop, dump delay/entity_id/spawn_count each tick. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/tile_entity_spawner.h"

int main(int argc, char **argv) {
    u64 seed = (argc > 1) ? (u64)strtoull(argv[1], 0, 10) : 12345ULL;
    int nticks = (argc > 2) ? atoi(argv[2]) : TES_NUM_TICKS;
    TeSpawnerScene scene;
    u64 *out = (u64 *)malloc(sizeof(u64) * (size_t)nticks * TES_DUMP_FIELDS);
    int i;

    tes_run(&scene, seed, nticks, out);
    for (i = 0; i < nticks * TES_DUMP_FIELDS; ++i)
        printf("%016llx\n", (unsigned long long)out[i]);
    free(out);
    return 0;
}
