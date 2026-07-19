/* CPU reference: block_tickers_crops harness, seed argv -> dump final world u16 states. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/block_tickers_crops.h"

int main(int argc, char **argv) {
    u64 seed = (argc > 1) ? (u64)strtoull(argv[1], 0, 10) : 12345ULL;
    BtcWorld w;
    w.seed = seed;
    btc_run(&w);
    const u16 *b = btc_now(&w);
    for (int y = 0; y < BTC_H; ++y)
        for (int z = 0; z < BTC_W; ++z)
            for (int x = 0; x < BTC_W; ++x)
                printf("%016llx\n", (unsigned long long)btc_get(b, x, y, z));
    return 0;
}
