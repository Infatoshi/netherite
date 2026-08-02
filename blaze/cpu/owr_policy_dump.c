/* owr_policy_dump: blaze-side CPU reference dump of one overworld_region window
 * with explicit policy switches. Reproduces the owr_run sequence from
 * core/overworld_region.h but does NOT call owr_run (which hard-wires fluid ON).
 * Core headers and existing drivers are untouched.
 *
 * Usage:
 *   owr_policy_dump [options] [seed [bcx [bcz]]]
 *
 * Options:
 *   --fluid on|off       run owfl_fluid_pass after populate (default: on, matches owr_run)
 *   --shroomlight stale|ca
 *                        attach vanilla STALE popSkyLight array (stale) or leave NULL
 *                        so SHROOM section uses the light fixpoint CA (ca).
 *                        Default: ca (matches owr_run: PllLight.pop_sky_stale zero-init).
 *                        Magma product default is stale (MAGMA_SHROOMLIGHT unset).
 *   --format sparse|full sparse: one "x,y,z,state" line per non-air cell, sorted
 *                        (default). full: same header+W_N x %04x as overworld_region.
 *   --help
 *
 * World coords: local window index maps to world x = bcx*16+lx, z = bcz*16+lz.
 * state is the compact PB_* model key (same namespace as magma world_dump without --states).
 *
 * Shroomlight note: the region path (pll_biome_decorate2 -> pls SHROOM section) DOES
 * consult popSkyLight for mushroom canBlockStay. owr_run never attaches it, so the
 * reference is the CA model. Magma's wrapper defaults to the stale array.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/overworld_region.h"

typedef struct { int x, y, z; unsigned state; } Cell;

static int cell_cmp(const void *a, const void *b) {
    const Cell *pa = (const Cell *)a, *pb = (const Cell *)b;
    if (pa->x != pb->x) return pa->x - pb->x;
    if (pa->y != pb->y) return pa->y - pb->y;
    if (pa->z != pb->z) return pa->z - pb->z;
    return (int)pa->state - (int)pb->state;
}

static void usage(const char *argv0) {
    fprintf(stderr,
        "usage: %s [--fluid on|off] [--shroomlight stale|ca] "
        "[--format sparse|full] [seed [bcx [bcz]]]\n",
        argv0);
}

/* owr_run body with policy knobs. Identical structure to overworld_region.h:owr_run
 * except (1) optional fluid pass, (2) optional stale skylight for SHROOM. */
static void run_policy(i64 seed, int bcx, int bcz, McSinTable *st,
                       int fluid_on, int stale_light, int fmt_full) {
    World w;
    w.st = st;
    w.blocks = (u16 *)malloc(sizeof(u16) * (size_t)W_N);
    u8 *sky = (u8 *)malloc(W_N);
    u8 *blk = (u8 *)malloc(W_N);
    u8 *tmp_sky = (u8 *)malloc(W_N);
    u8 *tmp_blk = (u8 *)malloc(W_N);
    u8 *pop_sky_stale = stale_light ? (u8 *)malloc(W_N) : NULL;
    CpScratch *sc = (CpScratch *)malloc(sizeof(CpScratch));
    ChunkPrimer *primer = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    FoliageCoord *fol = (FoliageCoord *)malloc(sizeof(FoliageCoord) * (size_t)BT_MAX_FOLIAGE);
    u16 *mc_cur = (u16 *)malloc(sizeof(u16) * (size_t)PFS_N);
    u16 *mc_tmp = (u16 *)malloc(sizeof(u16) * (size_t)PFS_N);
    u16 *before_ca = (u16 *)malloc(sizeof(u16) * (size_t)PFS_N);
    u16 pop_sky_height[W_X * W_Z];
    JavaRandom r;
    int i;

    if (!w.blocks || !sky || !blk || !tmp_sky || !tmp_blk || !sc || !primer || !fol ||
        !mc_cur || !mc_tmp || !before_ca || (stale_light && !pop_sky_stale)) {
        fprintf(stderr, "owr_policy_dump: OOM\n");
        exit(1);
    }

    /* --- owr_run prefix (terrain + biome), then owr_populate, optional fluid --- */
    {
        PllLight lt = { sky, blk, tmp_sky, tmp_blk, pop_sky_height, pop_sky_stale };

        for (i = 0; i < W_N; ++i) {
            w.blocks[i] = (u16)PB_AIR;
            sky[i] = 0;
            blk[i] = 0;
        }
        w.bigtree_heightLimit = 0;
        w_reset_loaded_chunks(&w, seed, bcx, bcz);

        {
            GLNode nodes[GL_MAX_NODES];
            int voronoi;
            gl_build(nodes, seed, &voronoi);
            {
                sc->arena.off = 0;
                int *fb = gl_getInts(nodes, &sc->arena, voronoi, bcx * 16, bcz * 16, W_X, W_Z);
                int x, z;
                for (x = 0; x < W_X; ++x)
                    for (z = 0; z < W_Z; ++z)
                        w.fullBiome[x * W_Z + z] = fb[x + z * W_X];
            }
        }

        {
            int cx, cz;
            for (cx = 0; cx < 2; ++cx) {
                for (cz = 0; cz < 2; ++cz) {
                    st_run_features(primer, sc, w.st, seed, bcx + cx, bcz + cz, ST_MAP_FEATURES);
                    {
                        int lx, lz, y;
                        for (lx = 0; lx < 16; ++lx)
                            for (lz = 0; lz < 16; ++lz)
                                for (y = 0; y < 256; ++y)
                                    w_set(&w, cx * 16 + lx, y, cz * 16 + lz,
                                          cb_get(primer, lx, y, lz));
                    }
                }
            }
        }

        owr_populate(&w, &r, seed, fol, &lt, bcx, bcz);
        if (fluid_on)
            owfl_fluid_pass(&w, seed, mc_cur, mc_tmp, before_ca);
    }

    if (fmt_full) {
        printf("owr seed=%lld bcx=%d bcz=%d\n", (long long)seed, bcx, bcz);
        for (i = 0; i < W_N; ++i)
            printf("%04x\n", (unsigned)w.blocks[i]);
    } else {
        /* sparse: collect non-air, sort by (x,y,z) for stable diffs */
        Cell *cells = (Cell *)malloc(sizeof(Cell) * (size_t)W_N);
        int n = 0;
        int lx, lz, y;
        if (!cells) { fprintf(stderr, "owr_policy_dump: OOM cells\n"); exit(1); }
        for (lx = 0; lx < W_X; ++lx)
            for (lz = 0; lz < W_Z; ++lz)
                for (y = 0; y < W_Y; ++y) {
                    unsigned stt = (unsigned)w.blocks[w_index(lx, y, lz)];
                    if (stt == (unsigned)PB_AIR) continue;
                    cells[n].x = bcx * 16 + lx;
                    cells[n].y = y;
                    cells[n].z = bcz * 16 + lz;
                    cells[n].state = stt;
                    ++n;
                }
        qsort(cells, (size_t)n, sizeof(Cell), cell_cmp);
        for (i = 0; i < n; ++i)
            printf("%d,%d,%d,%u\n", cells[i].x, cells[i].y, cells[i].z, cells[i].state);
        free(cells);
    }

    free(before_ca);
    free(mc_tmp);
    free(mc_cur);
    free(fol);
    free(primer);
    free(sc);
    free(pop_sky_stale);
    free(tmp_blk);
    free(tmp_sky);
    free(blk);
    free(sky);
    free(w.blocks);
}

int main(int argc, char **argv) {
    i64 seed = 0;
    int bcx = 0, bcz = 0;
    int fluid_on = 1;
    int stale_light = 0;   /* ca model: matches owr_run */
    int fmt_full = 0;
    int positional = 0;
    McSinTable *st;
    int i;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            usage(argv[0]);
            return 0;
        }
        if (!strcmp(argv[i], "--fluid") && i + 1 < argc) {
            const char *v = argv[++i];
            if (!strcmp(v, "on") || !strcmp(v, "1")) fluid_on = 1;
            else if (!strcmp(v, "off") || !strcmp(v, "0")) fluid_on = 0;
            else { fprintf(stderr, "bad --fluid %s\n", v); return 2; }
            continue;
        }
        if (!strcmp(argv[i], "--shroomlight") && i + 1 < argc) {
            const char *v = argv[++i];
            if (!strcmp(v, "stale")) stale_light = 1;
            else if (!strcmp(v, "ca")) stale_light = 0;
            else { fprintf(stderr, "bad --shroomlight %s\n", v); return 2; }
            continue;
        }
        if (!strcmp(argv[i], "--format") && i + 1 < argc) {
            const char *v = argv[++i];
            if (!strcmp(v, "full")) fmt_full = 1;
            else if (!strcmp(v, "sparse")) fmt_full = 0;
            else { fprintf(stderr, "bad --format %s\n", v); return 2; }
            continue;
        }
        if (argv[i][0] == '-') {
            fprintf(stderr, "unknown flag %s\n", argv[i]);
            usage(argv[0]);
            return 2;
        }
        if (positional == 0) seed = strtoll(argv[i], 0, 10);
        else if (positional == 1) bcx = (int)strtol(argv[i], 0, 10);
        else if (positional == 2) bcz = (int)strtol(argv[i], 0, 10);
        else {
            fprintf(stderr, "extra arg %s\n", argv[i]);
            return 2;
        }
        ++positional;
    }

    st = (McSinTable *)malloc(sizeof(McSinTable));
    if (!st) { fprintf(stderr, "owr_policy_dump: OOM st\n"); return 1; }
    mc_sin_table_init(st);
    run_policy(seed, bcx, bcz, st, fluid_on, stale_light, fmt_full);
    free(st);
    return 0;
}
