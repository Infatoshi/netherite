/* CPU reference: block ticker harness, seed argv -> dump final world u16 states. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/block_tickers.h"

int main(int argc, char **argv) {
    u64 seed = (argc > 1) ? (u64)strtoull(argv[1], 0, 10) : 12345ULL;
    BtWorld w;
    w.seed = seed;
    bt_run(&w);
    const u16 *b = bt_now(&w);
    for (int y = 0; y < BT_H; ++y)
        for (int z = 0; z < BT_W; ++z)
            for (int x = 0; x < BT_W; ++x)
                printf("%016llx\n", (unsigned long long)bt_get(b, x, y, z));
    return 0;
}
