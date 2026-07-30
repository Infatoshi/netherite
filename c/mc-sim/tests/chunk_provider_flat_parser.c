#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../core/chunk_provider_flat.h"

int main(void) {
    CpfLayer layer;

    assert(cpf_parse_layer_token(3, "minecraft:grass", 0, &layer));
    assert(layer.count == 1);
    assert(layer.block_state == CPF_GRASS);
    assert(layer.min_y == 0);

    assert(cpf_parse_layer_token(0, "7", 9, &layer));
    assert(layer.count == 1);
    assert(layer.block_state == CPF_BEDROCK);
    assert(layer.min_y == 9);

    /* The parser's staging buffer accepts 127 bytes while block_part accepts
     * 95.  This no-separator case caught the former block_part[127] write
     * under ASan and keeps the maximum accepted token length covered. */
    char maximum_token[128];
    memset(maximum_token, 'q', sizeof(maximum_token) - 1);
    maximum_token[sizeof(maximum_token) - 1] = '\0';
    assert(cpf_parse_layer_token(3, maximum_token, 17, &layer));
    assert(layer.count == 1);
    assert(layer.block_state == CPF_STONE);
    assert(layer.min_y == 17);

    puts("chunk_provider_flat parser PASS");
    return 0;
}
