/* CPU reference: items_tools_armor battery. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/items_tools_armor.h"

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    u64 out[ITA_NOUT];
    ita_run_battery(out);
    for (int i = 0; i < ITA_NOUT; ++i)
        printf("%016llx\n", (unsigned long long)out[i]);
    return 0;
}
