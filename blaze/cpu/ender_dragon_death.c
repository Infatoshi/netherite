/* CPU reference: dragon death animation + exit portal/egg on kill. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/ender_dragon_death.h"

static void run_one(int idx, int nticks) {
    McSinTable st;
    mc_sin_table_init(&st);

    EdeWorld world;
    u64 *out = (u64 *)malloc(sizeof(u64) * (size_t)nticks * EDE_DUMP_FIELDS);
    ede_run_scenario(idx, &st, &world, nticks, out);

    for (int i = 0; i < nticks * EDE_DUMP_FIELDS; ++i)
        printf("%016llx\n", (unsigned long long)out[i]);

    free(out);
}

int main(int argc, char **argv) {
    int nticks = (argc > 2) ? atoi(argv[2]) : EDE_NUM_TICKS;

    if (argc > 1) {
        run_one(atoi(argv[1]), nticks);
    } else {
        for (int i = 0; i < EDE_NUM_SCENARIOS; ++i)
            run_one(i, nticks);
    }
    return 0;
}
