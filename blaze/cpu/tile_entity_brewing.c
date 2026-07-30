/* CPU reference: brewing stand dump at tick marks. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/tile_entity_brewing.h"

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    TeBrewing b;
    u64 out[TB_OUT];
    tb_run_dump(&b, out);
    for (int i = 0; i < TB_OUT; ++i) printf("%016llx\n", (unsigned long long)out[i]);
    return 0;
}
