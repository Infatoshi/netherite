/* CPU driver for entity_spine - same core/entity_spine.h as CUDA. */
#include <stdio.h>
#include "../core/entity_spine.h"

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    u64 out[ES_OUT];
    es_run(out);
    for (int i = 0; i < ES_OUT; ++i)
        printf("%016llx\n", (unsigned long long)out[i]);
    return 0;
}
