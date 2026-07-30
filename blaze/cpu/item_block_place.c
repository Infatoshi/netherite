/* CPU reference driver for item_block_place. Emits packed case words as %08x. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/item_block_place.h"

int main(int argc, char **argv) {
    u32 out[IBP_NUM_CASES];
    int i;

    if (argc > 1) {
        int idx = atoi(argv[1]);
        if (idx < 0 || idx >= IBP_NUM_CASES) return 1;
        printf("%08x\n", (unsigned)ibp_case_word(idx));
        return 0;
    }

    ibp_run(out);
    for (i = 0; i < IBP_NUM_CASES; ++i)
        printf("%08x\n", (unsigned)out[i]);
    return 0;
}
