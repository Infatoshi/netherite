/* CPU reference: MC 1.11.2 crafting engine over a fixed battery of 3x3 grids. For each grid runs
 * findMatchingRecipe and prints the resulting ItemStack as three %08x lines: itemId, count, meta.
 * A no-match prints itemId=0xffffffff, count=0, meta=0. Matches cuda/crafting_recipes.cu + golden. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/crafting_recipes.h"

int main(int argc, char **argv) {
    (void)argc; (void)argv;   /* pure logic: no seed */
    CRRecipe R[CR_NRECIPES];
    int n = cr_build(R);
    CRStack grids[CR_NTESTS][9];
    cr_battery(grids);
    for (int t = 0; t < CR_NTESTS; ++t) {
        CRStack r = cr_findMatching(R, n, grids[t]);
        printf("%08x\n", (unsigned)r.item);
        printf("%08x\n", (unsigned)r.count);
        printf("%08x\n", (unsigned)r.meta);
    }
    return 0;
}
