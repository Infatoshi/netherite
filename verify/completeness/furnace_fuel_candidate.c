#include <stdio.h>

#include "smelting_recipes.h"

static unsigned int float_bits(float value) {
    union {
        float f;
        unsigned int u;
    } bits;
    bits.f = value;
    return bits.u;
}

int main(void) {
    for (int id = 0; id <= 2300; ++id) {
        printf("%d %d %d %d", id,
               sr_getItemBurnTime(sr_mk(id, 1, 0)),
               sr_getItemBurnTime(sr_mk(id, 1, 1)),
               sr_getItemBurnTime(sr_mk(id, 1, 15)));
        for (int meta = 0; meta < 16; ++meta)
            printf(" %u", float_bits(
                sr_getSmeltingExperience(sr_mk(id, 1, meta))));
        putchar('\n');
    }
    return 0;
}
