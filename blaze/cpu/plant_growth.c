/* CPU reference: plant_growth battery. seed [nticks] -> probe id/meta lines. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/plant_growth.h"

int main(int argc, char **argv) {
    i64 seed   = (argc > 1) ? strtoll(argv[1], 0, 10) : PG_DEFAULT_SEED;
    int nticks = (argc > 2) ? (int)strtol(argv[2], 0, 10) : PG_NTICKS;
    PgWorld w;
    pg_run(&w, seed, nticks);
    const u16 *b = pg_now(&w);
    PgProbe probes[PG_NPROBES];
    pg_probes(probes);
    for (int i = 0; i < PG_NPROBES; ++i) {
        u16 s = pg_get(b, probes[i].x, probes[i].y, probes[i].z);
        printf("%d %d\n", pg_id(s), pg_meta(s));
    }
    return 0;
}
