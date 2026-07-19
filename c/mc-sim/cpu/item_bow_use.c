/* CPU reference driver for item_bow_use. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/item_bow_use.h"

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    McSinTable st;
    u64 out[IBU_OUT];
    mc_sin_table_init(&st);
    ibu_run(seed, &st, out);
    for (int i = 0; i < IBU_OUT; ++i)
        printf("%016llx\n", (unsigned long long)out[i]);
    return 0;
}
