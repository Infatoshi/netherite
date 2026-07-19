/* CPU reference: loot_table fixed-table roll battery. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/loot_table.h"

static void emit_u32(u32 v) {
    printf("%08x\n", (unsigned)v);
}

int main(int argc, char **argv) {
    u32 out[LT_OUT];
    int i;
    if (argc > 2) {
        /* optional: table_id roll_idx */
        int t = atoi(argv[1]);
        int r = atoi(argv[2]);
        u32 one[LT_FIELDS_PER];
        lt_run_one(t, r, one);
        for (i = 0; i < LT_FIELDS_PER; ++i)
            emit_u32(one[i]);
        return 0;
    }
    lt_run_battery(out);
    for (i = 0; i < LT_OUT; ++i)
        emit_u32(out[i]);
    return 0;
}
