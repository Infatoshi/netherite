/* dim_region_dump.c - multi-chunk Nether / End terrain dump for census.
 *
 * Nether path: blaze nether_full.h nf_run (terrain + hell caves + fortress).
 * End path:    blaze chunk_provider_end.h cpe_provide_chunk (main island;
 *              no MapGenEndSpike / end-city populate - those are not in the
 *              provideChunk pipeline; pillar crystal layout is fixed in
 *              ender_dragon.h and is not seed-dependent terrain).
 *
 * Output (default sparse): one "x,y,z,id" line per non-air cell, world coords,
 * vanilla numeric block ids (End: CE_END_STONE -> 121). Sorted by the caller.
 *
 * Build (from repo root):
 *   cc -O2 -ffp-contract=off -Iblaze/core verify/worldgen/dim_region_dump.c \
 *      -o verify/worldgen/dim_region_dump -lm
 *
 * Usage:
 *   dim_region_dump nether <seed> <cx0> <cz0> <ncx> <ncz> [-o out.txt]
 *   dim_region_dump end    <seed> <cx0> <cz0> <ncx> <ncz> [-o out.txt]
 *   dim_region_dump find-fortress <seed> <scan_radius_chunks>
 *   dim_region_dump list-fortress <seed> <scan_radius_chunks>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nether_full.h"
#include "chunk_provider_end.h"
#include "map_gen_fortress.h"

static int ce_to_van(int v) {
    if (v == CE_AIR) return 0;
    if (v == CE_END_STONE) return 121;
    if (v == CE_STONE) return 1;
    return v;
}

static FILE *open_out(const char *path) {
    if (!path || !path[0] || !strcmp(path, "-")) return stdout;
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        exit(2);
    }
    return f;
}

static int dump_nether(i64 seed, int cx0, int cz0, int ncx, int ncz, FILE *out) {
    McSinTable *st = (McSinTable *)malloc(sizeof(McSinTable));
    CpnPrimer *primer = (CpnPrimer *)malloc(sizeof(CpnPrimer));
    CpnHellScratch *sc = (CpnHellScratch *)malloc(sizeof(CpnHellScratch));
    CpnHellNoise *noise = (CpnHellNoise *)malloc(sizeof(CpnHellNoise));
    if (!st || !primer || !sc || !noise) return 2;
    mc_sin_table_init(st);
    cpn_noise_init(noise, seed);
    long long non_air = 0;
    for (int iz = 0; iz < ncz; ++iz) {
        for (int ix = 0; ix < ncx; ++ix) {
            int cx = cx0 + ix, cz = cz0 + iz;
            nf_run(primer, sc, st, noise, seed, cx, cz);
            for (int lx = 0; lx < 16; ++lx) {
                for (int lz = 0; lz < 16; ++lz) {
                    for (int y = 0; y < 256; ++y) {
                        int id = (int)primer->data[cpn_idx(lx, y, lz)];
                        if (!id) continue;
                        fprintf(out, "%d,%d,%d,%d\n",
                                cx * 16 + lx, y, cz * 16 + lz, id);
                        ++non_air;
                    }
                }
            }
        }
    }
    fprintf(stderr,
            "dim_region_dump nether seed=%lld chunks=(%d,%d)+%dx%d non_air=%lld\n",
            (long long)seed, cx0, cz0, ncx, ncz, non_air);
    free(noise);
    free(sc);
    free(primer);
    free(st);
    return 0;
}

static int dump_end(i64 seed, int cx0, int cz0, int ncx, int ncz, FILE *out) {
    CpePrimer *primer = (CpePrimer *)malloc(sizeof(CpePrimer));
    CpeScratch *sc = (CpeScratch *)malloc(sizeof(CpeScratch));
    if (!primer || !sc) return 2;
    long long non_air = 0;
    for (int iz = 0; iz < ncz; ++iz) {
        for (int ix = 0; ix < ncx; ++ix) {
            int cx = cx0 + ix, cz = cz0 + iz;
            cpe_provide_chunk(primer, sc, seed, cx, cz);
            for (int lx = 0; lx < 16; ++lx) {
                for (int lz = 0; lz < 16; ++lz) {
                    for (int y = 0; y < 256; ++y) {
                        int id = ce_to_van(cpe_get(primer, lx, y, lz));
                        if (!id) continue;
                        fprintf(out, "%d,%d,%d,%d\n",
                                cx * 16 + lx, y, cz * 16 + lz, id);
                        ++non_air;
                    }
                }
            }
        }
    }
    fprintf(stderr,
            "dim_region_dump end seed=%lld chunks=(%d,%d)+%dx%d non_air=%lld\n",
            (long long)seed, cx0, cz0, ncx, ncz, non_air);
    free(sc);
    free(primer);
    return 0;
}

static int find_fortress(i64 seed, int radius, int list_all) {
    int found = 0;
    for (int cz = -radius; cz <= radius; ++cz) {
        for (int cx = -radius; cx <= radius; ++cx) {
            if (!ft_can_spawn(seed, cx, cz)) continue;
            printf("%d %d\n", cx, cz);
            ++found;
            if (!list_all) return 0;
        }
    }
    if (!found) {
        fprintf(stderr, "no fortress spawn in cx,cz in [-%d,%d]^2 for seed %lld\n",
                radius, radius, (long long)seed);
        return 1;
    }
    fprintf(stderr, "fortress starts: %d (seed %lld radius %d)\n",
            found, (long long)seed, radius);
    return 0;
}

static void usage(const char *argv0) {
    fprintf(stderr,
            "usage:\n"
            "  %s nether|end <seed> <cx0> <cz0> <ncx> <ncz> [-o out.txt]\n"
            "  %s find-fortress <seed> <scan_radius_chunks>\n"
            "  %s list-fortress <seed> <scan_radius_chunks>\n",
            argv0, argv0, argv0);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 2;
    }
    const char *mode = argv[1];
    if (!strcmp(mode, "find-fortress") || !strcmp(mode, "list-fortress")) {
        if (argc < 4) {
            usage(argv[0]);
            return 2;
        }
        i64 seed = strtoll(argv[2], 0, 10);
        int radius = atoi(argv[3]);
        return find_fortress(seed, radius, !strcmp(mode, "list-fortress"));
    }
    if (strcmp(mode, "nether") && strcmp(mode, "end")) {
        usage(argv[0]);
        return 2;
    }
    if (argc < 7) {
        usage(argv[0]);
        return 2;
    }
    i64 seed = strtoll(argv[2], 0, 10);
    int cx0 = atoi(argv[3]), cz0 = atoi(argv[4]);
    int ncx = atoi(argv[5]), ncz = atoi(argv[6]);
    const char *out_path = "-";
    for (int i = 7; i < argc; ++i) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) out_path = argv[++i];
        else {
            fprintf(stderr, "bad arg %s\n", argv[i]);
            return 2;
        }
    }
    if (ncx <= 0 || ncz <= 0) {
        fprintf(stderr, "bad ncx/ncz\n");
        return 2;
    }
    FILE *out = open_out(out_path);
    int rc;
    if (!strcmp(mode, "nether"))
        rc = dump_nether(seed, cx0, cz0, ncx, ncz, out);
    else
        rc = dump_end(seed, cx0, cz0, ncx, ncz, out);
    if (out != stdout) fclose(out);
    return rc;
}
