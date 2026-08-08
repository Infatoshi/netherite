/* world_dump.c - dump magma's decorated world blocks + biome for a chunk range,
 * for the worldgen INTEGRATION verifier (trace/world_diff.py). Ground truth is the
 * REAL Java game's saved .mca; this emits magma's side the way magma RENDERS it:
 * blaze cp_provide_chunk base terrain + owr_run decoration (world/light.c gen_chunk
 * -> world/populate_mc.c popmc_decorate_chunk), read back via light_block/light_biome.
 *
 * Emits raw ids (PB and CB small-int codes, NOT vanilla numeric); world_diff.py maps
 * them to vanilla ids. Output is a flat binary:
 *   magic "CRWD"  (4 bytes)
 *   int32 seed_lo, seed_hi (little endian split of seed)   -- actually i64 seed
 *   int32 cx0, cz0, ncx, ncz   (chunk range, ncx*ncz chunks, x-major)
 *   then per chunk (x-major over cx then cz):
 *     uint16 block[16*256*16] in index lx*4096 + lz*256 + y   (CB_INDEX order)
 *     int32  biome[16*16]     in index lx*16 + lz
 * Usage: world_dump --seed S --cx0 X --cz0 Z --ncx N --ncz M --out file.bin
 *                   [--prep-list bases.txt] [--states] [--world-type 0|1|2|3]
 * --states changes magic to CRWS and writes canonical packed vanilla state
 * (id<<4|meta) instead of compact renderer block ids. Biomes are unchanged.
 *
 * --prep-list: text file of "bcx bcz" base-chunk pairs in VANILLA POPULATE ORDER
 * (spawn-square raster first, then player-loaded chunks). Windows are pre-built
 * cumulatively in that order (popmc_prepare), each seeded with the earlier
 * overlapping windows' decorations - reproducing vanilla's shared-world populate
 * cascade. The owr pool is enlarged to span the whole prep region + dump range so
 * nothing is evicted.
 */
#include "world/light.h"
#include "world/populate_mc.h"
#include "game/caps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* RNG-cursor probe sink (MAGMA_GENPROBE=path): same line format as the live-game
 * qrl/WorldGenProbe.java log, so trace/genprobe_diff.py can pair them. */
static FILE *g_probe_file;
static void probe_cb(int cx, int cz, const char *tag, const char *type,
                     unsigned long long cursor) {
    fprintf(g_probe_file, "%s %d %d %s %llu\n", tag, cx, cz, type, cursor);
}

int main(int argc, char **argv) {
    long long seed = 0;
    int cx0 = 0, cz0 = 0, ncx = 3, ncz = 3;
    const char *out = "trace/out/magma_world.bin";
    const char *preplist = NULL;
    const char *cascpath = NULL;
    int states = 0;
    int world_type = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = atoll(argv[++i]);
        else if (!strcmp(argv[i], "--cx0") && i + 1 < argc) cx0 = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--cz0") && i + 1 < argc) cz0 = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--ncx") && i + 1 < argc) ncx = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--ncz") && i + 1 < argc) ncz = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--prep-list") && i + 1 < argc) preplist = argv[++i];
        else if (!strcmp(argv[i], "--cascade") && i + 1 < argc) cascpath = argv[++i];
        else if (!strcmp(argv[i], "--states")) states = 1;
        else if (!strcmp(argv[i], "--world-type") && i + 1 < argc)
            world_type = atoi(argv[++i]);
        else { fprintf(stderr, "bad arg %s\n", argv[i]); return 2; }
    }
    if (world_type < 0 || world_type > 3) {
        fprintf(stderr, "bad world type %d\n", world_type); return 2;
    }

    /* --cascade: "bcx bcz cursorBefore cursorAfter" provider-rand clobber events
     * mined from the live game's GEN log lines (see populate_mc.h PopmcCascadeEvt). */
    if (cascpath) {
        FILE *cf = fopen(cascpath, "r");
        if (!cf) { fprintf(stderr, "cannot open cascade file %s\n", cascpath); return 1; }
        int cap = 64, n = 0;
        PopmcCascadeEvt *ev = (PopmcCascadeEvt *)malloc((size_t)cap * sizeof(*ev));
        int bx, bz, nx, nz; unsigned long long cb, ca;
        while (fscanf(cf, "%d %d %llu %llu %d %d", &bx, &bz, &cb, &ca, &nx, &nz) == 6) {
            if (n == cap) { cap *= 2; ev = (PopmcCascadeEvt *)realloc(ev, (size_t)cap * sizeof(*ev)); }
            ev[n].bcx = bx; ev[n].bcz = bz; ev[n].before = cb; ev[n].after = ca;
            ev[n].ncx = nx; ev[n].ncz = nz;
            ++n;
        }
        fclose(cf);
        popmc_set_cascade(ev, n);   /* leaked deliberately: must outlive all builds */
        fprintf(stderr, "world_dump: %d cascade clobber events armed\n", n);
    }

    int *bases = NULL, nbases = 0, prep_rad = 0;
    if (preplist) {
        FILE *pf = fopen(preplist, "r");
        if (!pf) { fprintf(stderr, "cannot open prep list %s\n", preplist); return 1; }
        int cap = 4096;
        bases = (int *)malloc((size_t)cap * 2 * sizeof(int));
        int bx, bz;
        while (fscanf(pf, "%d %d", &bx, &bz) == 2) {
            if (nbases == cap) {
                cap *= 2;
                bases = (int *)realloc(bases, (size_t)cap * 2 * sizeof(int));
            }
            bases[nbases * 2] = bx;
            bases[nbases * 2 + 1] = bz;
            ++nbases;
        }
        fclose(pf);
        /* Size the owr pool to hold every prep window PLUS the dump range's
         * on-demand windows simultaneously (no toroidal eviction mid-run). */
        int mnx = cx0 - 1, mxx = cx0 + ncx - 1, mnz = cz0 - 1, mxz = cz0 + ncz - 1;
        for (int i = 0; i < nbases; ++i) {
            if (bases[i * 2] < mnx) mnx = bases[i * 2];
            if (bases[i * 2] > mxx) mxx = bases[i * 2];
            if (bases[i * 2 + 1] < mnz) mnz = bases[i * 2 + 1];
            if (bases[i * 2 + 1] > mxz) mxz = bases[i * 2 + 1];
        }
        int spanx = mxx - mnx + 1, spanz = mxz - mnz + 1;
        int span = spanx > spanz ? spanx : spanz;
        /* Single-invocation dump: minimal square ensure radius covering the range. */
        int ccx = cx0 + ncx / 2, ccz = cz0 + ncz / 2;
        int radx = (ccx - cx0 > cx0 + ncx - 1 - ccx) ? ccx - cx0 : cx0 + ncx - 1 - ccx;
        int radz = (ccz - cz0 > cz0 + ncz - 1 - ccz) ? ccz - cz0 : cz0 + ncz - 1 - ccz;
        prep_rad = (radx > radz ? radx : radz) + 1;
        /* Light pool holds the whole ensure window; owr pool holds every prep
         * window PLUS the ensure window's on-demand windows - NO eviction, ever
         * (an evicted cumulative window would rebuild without its donors). */
        cr_caps_override("view_radius", prep_rad + 1);
        int owr_need = 2 * prep_rad + 4;
        if (owr_need < span + 3) owr_need = span + 3;
        cr_caps_override("owr_d_min", owr_need);
        /* Cumulative windows carry inherited donor cells on top of their own
         * decorations; give them ~3x the single-window cap. */
        cr_caps_override("owr_cells_max", 49152);
    }

    const char *probe_path = getenv("MAGMA_GENPROBE");
    if (probe_path) {
        g_probe_file = fopen(probe_path, "w");
        if (!g_probe_file) { fprintf(stderr, "cannot open %s\n", probe_path); return 1; }
        popmc_set_probe(probe_cb);
        fprintf(stderr, "world_dump: genprobe -> %s\n", probe_path);
    }

    CrLight *L = light_create_type(seed, world_type);
    if (!L) { fprintf(stderr, "light_create failed\n"); return 1; }
    if (nbases) {
        popmc_prepare(seed, bases, nbases);
        fprintf(stderr, "world_dump: prepared %d cumulative windows (%ld builds)\n",
                nbases, popmc_window_builds());
    }

    /* ensure the whole requested block loaded. center + radius covering the range.
     * light_ensure loads [ccx-r..ccx+r] x [ccz-r..ccz+r]; pick center/radius to span. */
    int ccx = cx0 + ncx / 2, ccz = cz0 + ncz / 2;
    int rad = (ncx > ncz ? ncx : ncz);   /* generous; populate windows are internal */
    if (nbases) rad = prep_rad;          /* big single dump: exactly cover the range */
    light_ensure(L, ccx, ccz, rad);

    FILE *f = fopen(out, "wb");
    if (!f) { fprintf(stderr, "cannot open %s\n", out); return 1; }
    fwrite(states ? "CRWS" : "CRWD", 1, 4, f);
    fwrite(&seed, sizeof(long long), 1, f);
    int32_t hdr[4] = { cx0, cz0, ncx, ncz };
    fwrite(hdr, sizeof(int32_t), 4, f);

    static unsigned short blk[16 * 256 * 16];
    static int32_t biome[16 * 16];
    for (int ix = 0; ix < ncx; ++ix)
        for (int iz = 0; iz < ncz; ++iz) {
            int cx = cx0 + ix, cz = cz0 + iz;
            for (int lx = 0; lx < 16; ++lx)
                for (int lz = 0; lz < 16; ++lz) {
                    biome[lx * 16 + lz] = light_biome(L, cx * 16 + lx, cz * 16 + lz);
                    for (int y = 0; y < 256; ++y)
                        blk[lx * 4096 + lz * 256 + y] = states
                            ? light_state(L, cx * 16 + lx, y, cz * 16 + lz)
                            : (unsigned short)light_block(L, cx * 16 + lx, y, cz * 16 + lz);
                }
            fwrite(blk, sizeof(unsigned short), 16 * 256 * 16, f);
            fwrite(biome, sizeof(int32_t), 16 * 16, f);
        }
    fclose(f);
    fprintf(stderr, "world_dump: wrote %d chunks (%d,%d)..(%d,%d) seed %lld -> %s\n",
            ncx * ncz, cx0, cz0, cx0 + ncx - 1, cz0 + ncz - 1, seed, out);
    return 0;
}
