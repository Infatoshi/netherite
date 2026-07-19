/* CPU reference: ChunkProviderFlat.provideChunk minus structures for chunk (0,0).
 * argv[1]=seed (ignored), argv[2]=flat preset string (null/default if omitted). */
#include <stdio.h>
#include <stdlib.h>
#include "../core/chunk_provider_flat.h"

int main(int argc, char **argv) {
    (void)(argc > 1 ? strtoll(argv[1], 0, 10) : 12345LL);
    const char *preset = (argc > 2) ? argv[2] : NULL;

    CpfPrimer primer;
    cpf_provide_chunk(&primer, preset);

    for (int i = 0; i < 65536; ++i)
        printf("%04x\n", (unsigned)primer.data[i]);

    return 0;
}
