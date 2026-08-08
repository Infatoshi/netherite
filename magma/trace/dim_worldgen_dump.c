/* dim_worldgen_dump.c - multi-chunk nether / end primers for REAL-MC Anvil diff.
 *
 * Emits packed little-endian u16 (vanilla_id << 4) in dumpblocks order:
 *   for cz in [cz0..cz1]:
 *     for cx in [cx0..cx1]:
 *       for y,z,x: write u16
 *
 * Nether ids are already vanilla (87 netherrack, ...). End CE_* is mapped:
 *   CE_AIR=0 -> 0, CE_END_STONE=1 -> 121, CE_STONE=2 -> 1.
 *
 * Build (from magma):
 *   cc -O2 -ffp-contract=off -I../blaze/core trace/dim_worldgen_dump.c -o /tmp/dim_worldgen_dump -lm
 *
 * Usage:
 *   dim_worldgen_dump nether <seed> <cx0> <cz0> <cx1> <cz1> > out.mcbd
 *   dim_worldgen_dump end    <seed> <cx0> <cz0> <cx1> <cz1> > out.mcbd
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chunk_provider_nether.h"
#include "chunk_provider_end.h"

static int ce_to_van(int v) {
    if (v == CE_AIR) return 0;
    if (v == CE_END_STONE) return 121; /* Blocks.END_STONE */
    if (v == CE_STONE) return 1;
    return v;
}

static void write_packed_id(int id) {
    u16 packed = (u16)((id & 0xfff) << 4);
    fwrite(&packed, 2, 1, stdout);
}

static int dump_nether(i64 seed, int cx0, int cz0, int cx1, int cz1) {
    McSinTable *st = (McSinTable *)malloc(sizeof(McSinTable));
    CpnPrimer *primer = (CpnPrimer *)malloc(sizeof(CpnPrimer));
    CpnHellScratch *sc = (CpnHellScratch *)malloc(sizeof(CpnHellScratch));
    CpnHellNoise *noise = (CpnHellNoise *)malloc(sizeof(CpnHellNoise));
    if (!st || !primer || !sc || !noise) return 2;
    mc_sin_table_init(st);
    cpn_noise_init(noise, seed);
    for (int cz = cz0; cz <= cz1; ++cz) {
        for (int cx = cx0; cx <= cx1; ++cx) {
            cpn_provide_chunk(primer, sc, st, noise, seed, cx, cz);
            for (int y = 0; y < 256; ++y)
                for (int z = 0; z < 16; ++z)
                    for (int x = 0; x < 16; ++x)
                        write_packed_id((int)primer->data[cpn_idx(x, y, z)]);
        }
    }
    free(noise); free(sc); free(primer); free(st);
    return 0;
}

static int dump_end(i64 seed, int cx0, int cz0, int cx1, int cz1) {
    CpePrimer *primer = (CpePrimer *)malloc(sizeof(CpePrimer));
    CpeScratch *sc = (CpeScratch *)malloc(sizeof(CpeScratch));
    if (!primer || !sc) return 2;
    for (int cz = cz0; cz <= cz1; ++cz) {
        for (int cx = cx0; cx <= cx1; ++cx) {
            cpe_provide_chunk(primer, sc, seed, cx, cz);
            for (int y = 0; y < 256; ++y)
                for (int z = 0; z < 16; ++z)
                    for (int x = 0; x < 16; ++x)
                        write_packed_id(ce_to_van(cpe_get(primer, x, y, z)));
        }
    }
    free(sc); free(primer);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 6) {
        fprintf(stderr,
                "usage: %s nether|end <seed> <cx0> <cz0> <cx1> <cz1>\n",
                argv[0]);
        return 2;
    }
    const char *dim = argv[1];
    i64 seed = strtoll(argv[2], 0, 10);
    int cx0 = atoi(argv[3]), cz0 = atoi(argv[4]);
    int cx1 = atoi(argv[5]), cz1 = atoi(argv[6]);
    if (cx1 < cx0 || cz1 < cz0) {
        fprintf(stderr, "bad chunk range\n");
        return 2;
    }
    if (!strcmp(dim, "nether") || !strcmp(dim, "DIM-1") || !strcmp(dim, "-1"))
        return dump_nether(seed, cx0, cz0, cx1, cz1);
    if (!strcmp(dim, "end") || !strcmp(dim, "DIM1") || !strcmp(dim, "1"))
        return dump_end(seed, cx0, cz0, cx1, cz1);
    fprintf(stderr, "unknown dim %s (want nether|end)\n", dim);
    return 2;
}
