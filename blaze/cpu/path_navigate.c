/* CPU reference: PathNavigateGround path-follow tick dump. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/path_navigate.h"

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    u64 *out = (u64 *)malloc(sizeof(u64) * PN_OUT);
    if (!out) return 1;
    pn_run(out);
    for (int i = 0; i < PN_OUT; ++i)
        printf("%016llx\n", (unsigned long long)out[i]);
    free(out);
    return 0;
}
