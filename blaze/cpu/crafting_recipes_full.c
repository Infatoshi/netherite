/* CPU reference: MC 1.11.2 crafting engine, full KEEP recipe set. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/crafting_recipes_full.h"

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    CRRecipe R[CRF_NRECIPES];
    int n = crf_build(R);
    CRStack grids[CRF_NTESTS][9];
    crf_battery(grids);
    for (int t = 0; t < CRF_NTESTS; ++t) {
        CRStack r = crf_findMatching(R, n, grids[t]);
        printf("%08x\n", (unsigned)r.item);
        printf("%08x\n", (unsigned)r.count);
        printf("%08x\n", (unsigned)r.meta);
    }
    return 0;
}
