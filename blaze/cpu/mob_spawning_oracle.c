/* CPU reference: WorldEntitySpawner hostile spawn DECISION oracle -> hex dump. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/mob_spawning_oracle.h"

int main(int argc, char **argv) {
    MsoOut out;
    int i;
    (void)argc;
    (void)argv;
    mso_run(&out);
    for (i = 0; i < out.n; ++i)
        printf("%016llx\n", (unsigned long long)out.lines[i]);
    return 0;
}
