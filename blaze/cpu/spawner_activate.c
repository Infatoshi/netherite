/* CPU reference: spawner isActivated + delay-countdown (RNG-free) dump. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/spawner_activate.h"

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    TeSpawnerScene *s = (TeSpawnerScene *)malloc(sizeof(TeSpawnerScene));
    u64 out[SA_OUT];
    sa_run(s, out);
    for (int i = 0; i < SA_OUT; ++i)
        printf("%016llx\n", (unsigned long long)out[i]);
    free(s);
    return 0;
}
