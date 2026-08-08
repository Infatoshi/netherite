/* CPU reference: FurnaceRecipes smelt outputs + getItemBurnTime for KEEP ores/food/fuel battery. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/smelting_recipes.h"

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    u32 out[SR_OUT];
    sr_run_dump(out);
    for (int i = 0; i < SR_OUT; ++i)
        printf("%08x\n", (unsigned)out[i]);
    return 0;
}
