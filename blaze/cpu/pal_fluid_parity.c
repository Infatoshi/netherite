/* CPU reference driver for pal_fluid_parity: 16-tick fluid CA, dense vs palette
 * layout. Three hex lines per tick (dense hash, pal hash, mismatch count); exits
 * 1 if any tick disagrees (the layout gate, independent of the CPU==CUDA diff). */
#include <stdio.h>
#include <stdlib.h>
#include "../core/pal_fluid_parity.h"

static int run_seed(u64 seed) {
    static Env e;
    static PalChunk pc;
    static PfpLine lines[TWC_NTICKS];
    u16 cur[TFC_SLICE_VOL], tmp[TFC_SLICE_VOL];
    u16 pcur[TFC_SLICE_VOL], ptmp[TFC_SLICE_VOL];
    int t, bad = 0;
    pfp_run(&e, &pc, seed, cur, tmp, pcur, ptmp, lines);
    for (t = 0; t < TWC_NTICKS; ++t) {
        printf("%016llx\n", (unsigned long long)lines[t].dense_hash);
        printf("%016llx\n", (unsigned long long)lines[t].pal_hash);
        printf("%016llx\n", (unsigned long long)lines[t].mismatches);
        if (lines[t].dense_hash != lines[t].pal_hash || lines[t].mismatches) bad = 1;
    }
    return bad;
}

int main(int argc, char **argv) {
    static const u64 k_seeds[] = {12345ULL, 0ULL, 7ULL};
    int i, bad = 0;

    if (argc > 1) {
        bad = run_seed(strtoull(argv[1], 0, 10));
    } else {
        for (i = 0; i < 3; ++i) bad |= run_seed(k_seeds[i]);
    }
    if (bad) { fprintf(stderr, "pal_fluid_parity: dense != pal\n"); return 1; }
    return 0;
}
