/* CPU reference: items_core battery. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/items_core.h"

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    u64 out[9];
    ic_run_battery(out);
    for (int i = 0; i < 9; ++i) printf("%016llx\n", (unsigned long long)out[i]);
    return 0;
}
