/* world/populate_mc.c - see world/populate_mc.h.
 *
 * View-distance decoration overlay. Wraps mc-sim's position-parametrized populate
 * (`owr_run`, core/overworld_region.h) to decorate ANY chunk, caching each base
 * chunk's populate window and applying the four contributing windows to a chunk.
 * Only this translation unit pulls in the heavy worldgen apparatus, keeping
 * world/light.c lean. */
#include "world/populate_mc.h"
#include "game/caps.h"          /* CrCaps: toroidal owr-pool geometry + cell cap */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <stdlib.h>

/* populate.h / overworld_region.h are big headers of `static` (MC_NOINLINE)
 * worldgen functions; most go unused in this TU. Silence the noise, not real
 * diagnostics. overworld_region.h is the mc-sim VERIFIED, read-only source of the
 * parametrized populate (owr_run) - do NOT edit it. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "overworld_region.h"  /* mc-sim: World, owr_run, st_run, PB_*, pb_opacity, cb_get, w_index */
#pragma GCC diagnostic pop

/* W_X/W_Y/W_Z/W_N come from populate.h (32,256,32). CB_INDEX must match world/light.c. */
#define CB_INDEX_LOCAL(lx, y, lz) (((lx) << 12) | ((lz) << 8) | (y))

/* ---------------- one-time owr_run scratch (allocated once, reused) --------- */
static McSinTable  *g_st;
static CpScratch   *g_sc;
static ChunkPrimer *g_pr;
static FoliageCoord *g_fol;
static u16 *g_owr, *g_stb;                 /* decorated window / its own st base */
static u8  *g_sky, *g_blk, *g_ts, *g_tb;   /* owr_run light scratch */
static u16 *g_skyhm;                       /* populate-time vanilla skylight heightMap */
static u8  *g_skylt;                       /* populate-time vanilla STALE skylight (mushroom gate) */
static u16 *g_cur, *g_tmp, *g_bca;         /* owr_run fluid-CA scratch */
static u8  *g_seeded;                      /* seeded-location mask (window being built) */
static int *g_bio;                         /* base window biome (build_st_base output) */
/* depth-1 nested-cascade buffer set (see ensure_scratch / cascade_hook) */
static FoliageCoord *g2_fol;
static u16 *g2_owr, *g2_stb;
static u8  *g2_sky, *g2_blk, *g2_ts, *g2_tb, *g2_seeded;
static u16 *g2_skyhm;
static u8  *g2_skylt;
static int *g2_bio;

static int ensure_scratch(void) {
    if (g_owr) return 1;
    /* The product requires generated strongholds but explicitly cuts mineshafts.
     * structures.h reserves -1 for that narrow live-world subset. */
    st_map_features_host = -1;
    g_st  = (McSinTable *)malloc(sizeof(McSinTable));
    g_sc  = (CpScratch *)malloc(sizeof(CpScratch));
    g_pr  = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    g_fol = (FoliageCoord *)malloc(sizeof(FoliageCoord) * (size_t)BT_MAX_FOLIAGE);
    g_owr = (u16 *)malloc(sizeof(u16) * (size_t)W_N);
    g_stb = (u16 *)malloc(sizeof(u16) * (size_t)W_N);
    g_sky = (u8 *)malloc((size_t)W_N);
    g_blk = (u8 *)malloc((size_t)W_N);
    g_ts  = (u8 *)malloc((size_t)W_N);
    g_tb  = (u8 *)malloc((size_t)W_N);
    g_skyhm = (u16 *)malloc(sizeof(u16) * (size_t)(W_X * W_Z));
    g_skylt = (u8 *)malloc((size_t)W_N);
    g_cur = (u16 *)malloc(sizeof(u16) * (size_t)W_N);
    g_tmp = (u16 *)malloc(sizeof(u16) * (size_t)W_N);
    g_bca = (u16 *)malloc(sizeof(u16) * (size_t)W_N);
    g_seeded = (u8 *)malloc((size_t)W_N);
    g_bio = (int *)malloc(sizeof(int) * (size_t)(W_X * W_Z));
    /* Second buffer set for ONE level of nested cascade populate (cascade_hook
     * swaps the live-during-populate buffers; g_sc/g_pr/g_cur/g_tmp/g_bca are only
     * used in phases the parent is not inside when the hook fires, so shared). */
    g2_fol = (FoliageCoord *)malloc(sizeof(FoliageCoord) * (size_t)BT_MAX_FOLIAGE);
    g2_owr = (u16 *)malloc(sizeof(u16) * (size_t)W_N);
    g2_stb = (u16 *)malloc(sizeof(u16) * (size_t)W_N);
    g2_sky = (u8 *)malloc((size_t)W_N);
    g2_blk = (u8 *)malloc((size_t)W_N);
    g2_ts  = (u8 *)malloc((size_t)W_N);
    g2_tb  = (u8 *)malloc((size_t)W_N);
    g2_skyhm = (u16 *)malloc(sizeof(u16) * (size_t)(W_X * W_Z));
    g2_skylt = (u8 *)malloc((size_t)W_N);
    g2_seeded = (u8 *)malloc((size_t)W_N);
    g2_bio = (int *)malloc(sizeof(int) * (size_t)(W_X * W_Z));
    if (!g_st || !g_sc || !g_pr || !g_fol || !g_owr || !g_stb || !g_sky ||
        !g_blk || !g_ts || !g_tb || !g_skyhm || !g_skylt || !g_cur || !g_tmp || !g_bca ||
        !g_seeded || !g_bio ||
        !g2_fol || !g2_owr || !g2_stb || !g2_sky || !g2_blk || !g2_ts || !g2_tb ||
        !g2_skyhm || !g2_skylt || !g2_seeded || !g2_bio)
        return 0;
    mc_sin_table_init(g_st);
    return 1;
}

/* ---------------- per-base-chunk populate window cache --------------------- */
/* A decoration cell: a world-coord block owr_run added on top of its base terrain.
 * y in [0,256) fits u16; id is a PB_* code (fits u16). */
typedef struct { int wx, wz; unsigned short y, id; } DecCell;

/* OOB decoration spill: writes that leave the 32x32 window during owr_populate.
 * Recorded with absolute world coords so later neighbor windows can seed them
 * (donor path) and so apply_window can place them when the target chunk is built. */
#define OOB_SPILL_MAX 65536
static DecCell g_oob_spill[OOB_SPILL_MAX];
static int g_oob_n;
static int g_oob_dropped;

static void oob_spill_reset(void) { g_oob_n = 0; g_oob_dropped = 0; }

static void oob_spill_write(int baseCx, int baseCz, int lx, int y, int lz, int v) {
    if (y < 0 || y >= W_Y) return;
    int wx = baseCx * 16 + lx, wz = baseCz * 16 + lz;
    /* last-write-wins for the same cell within one populate */
    for (int i = g_oob_n - 1; i >= 0 && i >= g_oob_n - 64; --i) {
        if (g_oob_spill[i].wx == wx && g_oob_spill[i].wz == wz &&
            (int)g_oob_spill[i].y == y) {
            g_oob_spill[i].id = (unsigned short)v;
            return;
        }
    }
    if (g_oob_n >= OOB_SPILL_MAX) { ++g_oob_dropped; return; }
    g_oob_spill[g_oob_n].wx = wx;
    g_oob_spill[g_oob_n].wz = wz;
    g_oob_spill[g_oob_n].y = (unsigned short)y;
    g_oob_spill[g_oob_n].id = (unsigned short)v;
    ++g_oob_n;
}

typedef struct {
    long long seed;
    int       bcx, bcz;
    int       valid;      /* slot holds a real built window */
    long      seq;        /* build order (g_builds at build time): donor apply order */
    DecCell  *cells;      /* post-fluid-pass cells, applied to chunks */
    int       ncells;
    DecCell  *pop_cells;  /* PRE-fluid-pass cells, seeded into later windows: vanilla
                           * runs initial-load populates back-to-back with NO world
                           * ticks between them, so a window sees earlier windows'
                           * placed blocks but NOT their fluid spread. */
    int       npop;
} Window;

/* ALLOCATE-ONCE toroidal owr-window pool. A fixed (2R+4)^2 pool indexed by (bcx,bcz)
 * modulo owr_D. The base chunks decorating the lit region span exactly owr_D x owr_D,
 * so each maps to a unique slot; a base chunk scrolling out frees its slot for the
 * incoming one, which RECYCLES it (re-run owr_run). Each slot owns one pre-allocated
 * cells buffer. Window eviction is safe: the decoration a window produces is written
 * into each lit chunk's block store at gen time and persists there; re-running owr_run
 * for a recycled window is deterministic. No malloc/free after init. */
static Window      *g_slots;
static const CrCaps *g_caps;
static int          g_owr_D, g_owr_slots;
static long         g_builds;   /* number of owr_run executions (compute-once metric) */
static int          g_bigtree_carry;   /* WorldGenBigTree.heightLimit session carry (see build_window) */
static const PopmcCascadeEvt *g_casc;  /* cascade RNG-clobber events (popmc_set_cascade) */
static int          g_ncasc;
static int          g_casc_depth;      /* 1 while inside a nested cascade build */
/* Parent window mid-populate, exposed as an extra donor for the nested build:
 * vanilla's cascade-populated chunk sees the parent's PARTIAL decorations. */
static struct {
    int active, bcx, bcz;
    const u16 *owr, *stb;
    const u8 *seeded;
} g_partial;
/* Vanilla chunkPos corruption (cascade_hook): the nested decorate() overwrites the
 * shared BiomeDecorator.chunkPos and vanilla never restores it, so the PARENT's
 * remaining decoration positions are relative to the NESTED chunk's corner. */
static World  *g_live_w;     /* World of the build in progress (redirect target) */
static Window *g_redir_win;  /* non-NULL: parent decoration redirected into this nested window */
static int     g_redir_ncx, g_redir_ncz;   /* nested chunk the corrupted tail decorates */
static int     g_redir_pbcx, g_redir_pbcz; /* parent base chunk (frame restore) */
static int     g_redir_applied;            /* redirect_apply fired for this build */

static int skyhm_scan_col(const u16 *blocks, int x, int z) {
    for (int y = W_Y - 1; y >= 0; --y)
        if (pb_opacity((int)blocks[w_index(x, y, z)]) > 0) return y + 1;
    return 0;
}

static void skyhm_rescan_col(u16 *heightMap, const u16 *blocks, int x, int z) {
    heightMap[w_col_index(x, z)] = (u16)skyhm_scan_col(blocks, x, z);
}

/* stale-skylight mode: default ON; MAGMA_SHROOMLIGHT=ca falls back to the fixpoint CA */
static int popmc_stale_light_enabled(void) {
    static int mode = -1;
    if (mode < 0) {
        const char *s = getenv("MAGMA_SHROOMLIGHT");
        mode = !(s && strcmp(s, "ca") == 0);
    }
    return mode;
}

/* re-seed World.popSecMask from the frame's current blocks (redirect frame swaps) */
static void popmc_sec_mask_seed(World *lw) {
    for (int cxi = 0; cxi < 2; ++cxi)
        for (int czi = 0; czi < 2; ++czi) {
            u16 m = 0;
            for (int sec = 0; sec < 16; ++sec) {
                int found = 0;
                for (int lx = 0; lx < 16 && !found; ++lx)
                    for (int lz = 0; lz < 16 && !found; ++lz)
                        for (int y = sec * 16; y < sec * 16 + 16; ++y)
                            if (lw->blocks[w_index(cxi * 16 + lx, y, czi * 16 + lz)] != PB_AIR) {
                                found = 1;
                                break;
                            }
                if (found) m |= (u16)(1u << sec);
            }
            lw->popSecMask[cxi][czi] = m;
        }
}

/* vanilla generateSkylightMap column walk over a raw buffer (regen keeps deep cells) */
static void skylt_regen_col(u8 *light, const u16 *blocks, int x, int z) {
    int k1 = 15;
    for (int y = W_Y - 1; y >= 0; --y) {
        int j1 = pb_opacity((int)blocks[w_index(x, y, z)]);
        if (j1 == 0 && k1 != 15) j1 = 1;
        k1 -= j1;
        if (k1 <= 0) break;
        light[w_index(x, y, z)] = (u8)k1;
    }
}

static void skylt_copy_col(u8 *dst, const u8 *srcl, int dxi, int dzi, int sxi, int szi) {
    for (int y = 0; y < W_Y; ++y)
        dst[w_index(dxi, y, dzi)] = srcl[w_index(sxi, y, szi)];
}

static void skyhm_copy_or_rescan_redirect_dst(int dx, int dz) {
    for (int lx = 0; lx < W_X; ++lx)
        for (int lz = 0; lz < W_Z; ++lz) {
            int px = lx + dx, pz = lz + dz;   /* nested-local -> parent-local */
            if (px >= 0 && px < W_X && pz >= 0 && pz < W_Z) {
                g2_skyhm[w_col_index(lx, lz)] = g_skyhm[w_col_index(px, pz)];
                skylt_copy_col(g2_skylt, g_skylt, lx, lz, px, pz);
            } else {
                skyhm_rescan_col(g2_skyhm, g2_owr, lx, lz);
                skylt_regen_col(g2_skylt, g2_owr, lx, lz);
            }
        }
}

static void skyhm_copy_nested_col_to_parent(int pbcx, int pbcz, int nbcx, int nbcz,
                                            int wx, int wz) {
    int px = wx - pbcx * 16, pz = wz - pbcz * 16;
    int nx = wx - nbcx * 16, nz = wz - nbcz * 16;
    if (px < 0 || px >= W_X || pz < 0 || pz >= W_Z ||
        nx < 0 || nx >= W_X || nz < 0 || nz >= W_Z)
        return;
    g_skyhm[w_col_index(px, pz)] = g2_skyhm[w_col_index(nx, nz)];
    skylt_copy_col(g_skylt, g2_skylt, px, pz, nx, nz);
}

/* MC_REDIRECT_POLL target: swap the live World to the nested window's buffers at the
 * next decoration position draw. Deferred (not done in cascade_hook) because the
 * feature in flight at clobber time keeps its absolute position in vanilla - it must
 * finish in the PARENT frame; only draws after it return see the corrupted chunkPos. */
static void redirect_apply(struct World *lw) {
    mc_redirect_pending = 0;
    g_redir_applied = 1;
    /* Vanilla has ONE world: everything the parent wrote after the nested snapshot
     * (the in-flight tree's trunk/vines, pre-cascade decorations) must be visible to
     * the corrupted tail. The parent window is ground truth for the overlap (the
     * nested results were already copied back into it) - mirror it into g2. */
    {
        int dx = (g_redir_ncx - g_redir_pbcx) * 16, dz = (g_redir_ncz - g_redir_pbcz) * 16;
        for (int lx = 0; lx < W_X; ++lx)
            for (int lz = 0; lz < W_Z; ++lz) {
                int px = lx + dx, pz = lz + dz;   /* nested-local -> parent-local */
                if (px < 0 || px >= W_X || pz < 0 || pz >= W_Z) continue;
                for (int y = 0; y < W_Y; ++y) {
                    int nidx = w_index(lx, y, lz), pidx = w_index(px, y, pz);
                    g2_owr[nidx] = g_owr[pidx];
                    g2_seeded[nidx] = g_seeded[pidx];
                }
            }
        skyhm_copy_or_rescan_redirect_dst(dx, dz);
    }
    lw->blocks = g2_owr;
    lw->popSkyHeight = g2_skyhm;
    if (lw->popSkyLight) { lw->popSkyLight = g2_skylt; popmc_sec_mask_seed(lw); }
    for (int x = 0; x < W_X; ++x)
        for (int z = 0; z < W_Z; ++z)
            lw->fullBiome[x * W_Z + z] = g2_bio[x * W_Z + z];
    lw->baseCx = g_redir_ncx;
    lw->baseCz = g_redir_ncz;
    fprintf(stderr, "[populate_mc] cascade: chunkPos corruption applied -> tail decorates "
            "(%d,%d) frame\n", g_redir_ncx, g_redir_ncz);
}

/* MC_REDIRECT_RESTORE target: genDecorations returned - the biome decorate() extras
 * (jungle melon/vines etc.) and the populate tail use the uncorrupted pos parameter.
 * Swap the live World back to the parent frame, first copying the corrupted tail's
 * writes that overlap the parent window into it (the extras READ them: vines/reeds
 * are block-conditional). */
static void redirect_restore(struct World *lw) {
    if (!g_redir_applied || lw->blocks != g2_owr) return;
    int dx = (g_redir_ncx - g_redir_pbcx) * 16, dz = (g_redir_ncz - g_redir_pbcz) * 16;
    for (int lx = 0; lx < W_X; ++lx)
        for (int lz = 0; lz < W_Z; ++lz) {
            int px = lx + dx, pz = lz + dz;   /* nested-local -> parent-local */
            if (px < 0 || px >= W_X || pz < 0 || pz >= W_Z) continue;
            for (int y = 0; y < W_Y; ++y) {
                int nidx = w_index(lx, y, lz);
                if (g2_owr[nidx] == g2_stb[nidx] && !g2_seeded[nidx]) continue;
                int pidx = w_index(px, y, pz);
                g_owr[pidx] = g2_owr[nidx];
                g_seeded[pidx] = 1;
            }
            g_skyhm[w_col_index(px, pz)] = g2_skyhm[w_col_index(lx, lz)];
        }
    lw->blocks = g_owr;
    lw->popSkyHeight = g_skyhm;
    if (lw->popSkyLight) { lw->popSkyLight = g_skylt; popmc_sec_mask_seed(lw); }
    for (int x = 0; x < W_X; ++x)
        for (int z = 0; z < W_Z; ++z)
            lw->fullBiome[x * W_Z + z] = g_bio[x * W_Z + z];
    lw->baseCx = g_redir_pbcx;
    lw->baseCz = g_redir_pbcz;
    fprintf(stderr, "[populate_mc] cascade: parent frame (%d,%d) restored post-genDecorations\n",
            g_redir_pbcx, g_redir_pbcz);
}

static inline int owr_tor(int v, int D) { int m = v % D; if (m < 0) m += D; return m; }

/* base-chunk memo storage (see stb_chunk below find_window) */
typedef struct { long long seed; int cx, cz, valid; } StbKey;
static StbKey *g_stb_keys;
static u16    *g_stb_pool;   /* g_stb_D^2 x 65536 block ids */
static int     g_stb_D;

static int owr_pool_init(void) {
    if (g_slots) return 1;
    g_caps      = cr_caps();
    g_owr_D     = g_caps->owr_D;
    g_owr_slots = g_caps->owr_slots;
    g_slots = (Window *)calloc((size_t)g_owr_slots, sizeof(Window));
    if (!g_slots) return 0;
    for (int i = 0; i < g_owr_slots; ++i) {
        g_slots[i].cells = (DecCell *)malloc((size_t)g_caps->owr_cells_max * sizeof(DecCell));
        g_slots[i].pop_cells = (DecCell *)malloc((size_t)g_caps->owr_cells_max * sizeof(DecCell));
        if (!g_slots[i].cells || !g_slots[i].pop_cells) return 0;
    }
    /* base-chunk memo: window bcx spans g_owr_D, base chunks reach bcx+1 -> +2
     * margin keeps live windows collision-free. Failure just disables the memo. */
    g_stb_D = g_owr_D + 2;
    g_stb_keys = (StbKey *)calloc((size_t)g_stb_D * g_stb_D, sizeof(StbKey));
    g_stb_pool = (u16 *)malloc((size_t)g_stb_D * g_stb_D * 65536 * sizeof(u16));
    if (!g_stb_keys || !g_stb_pool) {
        free(g_stb_keys); free(g_stb_pool);
        g_stb_keys = NULL; g_stb_pool = NULL;
    }
    return 1;
}

static Window *find_window(long long seed, int bcx, int bcz) {
    if (!g_slots) return NULL;
    Window *w = &g_slots[owr_tor(bcx, g_owr_D) * g_owr_D + owr_tor(bcz, g_owr_D)];
    if (w->valid && w->seed == seed && w->bcx == bcx && w->bcz == bcz) return w;
    return NULL;
}

/* Base-chunk memo: neighboring windows share base chunks, so build_st_base used
 * to regenerate each one up to 4x (~21% of a 12k replay). Cache the primer bytes
 * per (seed,cx,cz), toroidal + allocate-once like the owr pool. Because this TU
 * runs st_map_features_host = -1, the bytes are IDENTICAL to the live gen_chunk
 * product, so the gen_prefetch worker's copy (weak symbols; may be unlinked in
 * unit tests) can seed the memo for free. */
extern int genpf_take(int cx, int cz, unsigned short *out) __attribute__((weak));
extern int genpf_active(long long seed) __attribute__((weak));

static const u16 *stb_chunk(long long seed, int cx, int cz) {
    if (!g_stb_keys) {                       /* memo alloc failed: old path */
        st_run_features(g_pr, g_sc, g_st, (i64)seed, cx, cz, ST_MAP_FEATURES);
        return g_pr->data;
    }
    int si = owr_tor(cx, g_stb_D) * g_stb_D + owr_tor(cz, g_stb_D);
    StbKey *k = &g_stb_keys[si];
    u16 *d = g_stb_pool + (size_t)si * 65536;
    if (k->valid && k->seed == seed && k->cx == cx && k->cz == cz) return d;
    if (!(genpf_take && genpf_active && genpf_active(seed) &&
          genpf_take(cx, cz, d))) {
        st_run_features(g_pr, g_sc, g_st, (i64)seed, cx, cz, ST_MAP_FEATURES);
        memcpy(d, g_pr->data, sizeof g_pr->data);
    }
    k->valid = 1; k->seed = seed; k->cx = cx; k->cz = cz;
    return d;
}

/* Reconstruct owr_run's OWN base terrain (st_run x4) into g_stb, mirroring the exact
 * base-construction prefix of owr_run (biome voronoi at the same world offset, same
 * arena reset, same st_run coords) so g_stb equals owr_run's pre-populate blocks.
 * Diffing against this isolates DECORATION cells only. */
static void build_st_base(long long seed, int bcx, int bcz) {
    World wb;
    wb.st = g_st;
    wb.blocks = g_stb;
    for (int i = 0; i < W_N; ++i) g_stb[i] = (u16)PB_AIR;
    wb.bigtree_heightLimit = 0;
    w_reset_loaded_chunks(&wb, seed, bcx, bcz);
    {
        GLNode nodes[GL_MAX_NODES];
        int voronoi;
        gl_build(nodes, (i64)seed, &voronoi);
        g_sc->arena.off = 0;
        int *fb = gl_getInts(nodes, &g_sc->arena, voronoi, bcx * 16, bcz * 16, W_X, W_Z);
        /* GenLayer.getInts layout is fb[dx + dz*width] for world (areaX+dx, areaZ+dz);
         * fullBiome storage is [x*W_Z + z]. A transposed read here was invisible to the
         * decoration biome (diagonal (16,16)) but broke every off-diagonal per-column
         * consumer (ice/snow temperatures). */
        for (int x = 0; x < W_X; ++x)
            for (int z = 0; z < W_Z; ++z) {
                wb.fullBiome[x * W_Z + z] = fb[x + z * W_X];
                g_bio[x * W_Z + z] = fb[x + z * W_X];
            }
    }
    for (int cx = 0; cx < 2; ++cx)
        for (int cz = 0; cz < 2; ++cz) {
            const u16 *cp = stb_chunk(seed, bcx + cx, bcz + cz);
            /* Both layouts are y-fastest (cb_index = x<<12|z<<8|y, w_index ends
             * *W_Y+y), and w_set is pure blocks[] stores here (activeRand and
             * popSkyHeight are NULL after w_reset_loaded_chunks), so a straight
             * column memcpy is byte-identical to the old per-cell w_set loop. */
            for (int lx = 0; lx < 16; ++lx)
                for (int lz = 0; lz < 16; ++lz)
                    memcpy(&wb.blocks[w_index(cx * 16 + lx, 0, cz * 16 + lz)],
                           &cp[(lx << 12) | (lz << 8)], 256 * sizeof(u16));
        }
}

static long long g_build_seed;   /* seed of the build in progress (cascade_hook) */

/* Re-snapshot a window's cell lists from the given buffers (chunkPos-corruption
 * resnap: the parent's post-resume writes landed in the nested window's buffers
 * AFTER its cells were recorded). Collapses pop_cells==cells - fine while the
 * fluid CA is off (default); the CA distinction only matters with MAGMA_FLUID_CA. */
typedef unsigned long long PmAu64 __attribute__((may_alias));

static void record_window_cells(Window *win, const u16 *owr, const u16 *stb,
                                const u8 *seeded) {
    /* One pass, 64-cell stride: most of the window is undecorated, so compare
     * 16 u64 words of blocks + 8 of seeded and skip clean runs. Cell order is
     * unchanged - the old lx,lz,y nesting IS ascending w_index order. */
    int n = 0, cap = g_caps->owr_cells_max;
    const PmAu64 *o64 = (const PmAu64 *)owr;
    const PmAu64 *s64 = (const PmAu64 *)stb;
    for (int base = 0; base < W_N; base += 64) {
        PmAu64 d = 0;
        int q = base >> 2;
        for (int j = 0; j < 16; ++j) d |= o64[q + j] ^ s64[q + j];
        const PmAu64 *e = (const PmAu64 *)(seeded + base);
        for (int j = 0; j < 8; ++j) d |= e[j];
        if (!d) continue;
        for (int i = base; i < base + 64; ++i) {
            if (owr[i] == stb[i] && !seeded[i]) continue;
            if (n < cap) {
                int lx = i / (W_Z * W_Y), lz = (i >> 8) & (W_Z - 1), y = i & (W_Y - 1);
                win->cells[n].wx = win->bcx * 16 + lx;
                win->cells[n].wz = win->bcz * 16 + lz;
                win->cells[n].y  = (unsigned short)y;
                win->cells[n].id = (unsigned short)owr[i];
                win->pop_cells[n] = win->cells[n];
            }
            ++n;
        }
    }
    if (n > cap) {
        fprintf(stderr,
            "[populate_mc] FATAL: window (%d,%d) resnap has %d cells > cap %d "
            "(raise owr_cells_max in magma.conf / caps.h)\n",
            win->bcx, win->bcz, n, cap);
        assert(0 && "owr_cells_max exceeded");
        abort();
    }
    win->ncells = win->npop = n;
}

static Window *build_window(long long seed, int bcx, int bcz) {
    if (!ensure_scratch() || !owr_pool_init()) return NULL;
    g_build_seed = seed;

    /* owr_run's own base terrain (also fills g_bio with the window biome map). */
    build_st_base(seed, bcx, bcz);

    /* CUMULATIVE populate: vanilla populates into one SHARED world in load order,
     * so window (bcx,bcz) READS the decorations that populate-order-earlier windows
     * spilled into its 2x2-chunk footprint (trees test isReplaceable/heights, lakes
     * and dungeons test world blocks). Reproduce that by starting from the pure
     * st base and SEEDING the cells of the four earlier overlapping windows -
     * (bcx-1,bcz-1), (bcx-1,bcz), (bcx-1,bcz+1), (bcx,bcz-1), applied in vanilla
     * populate (raster) order so later writes win - before running owr_populate.
     * Windows absent from the pool contribute nothing (the sweep anchor: vanilla
     * never populated them, or best-effort in the live streaming path). */
    World w;
    w.st = g_st;
    w.blocks = g_owr;
    w_reset_loaded_chunks(&w, seed, bcx, bcz);
    for (int i = 0; i < W_N; ++i) {
        g_owr[i] = g_stb[i];
        g_sky[i] = 0;
        g_blk[i] = 0;
        g_seeded[i] = 0;
    }
    /* WorldGenBigTree.heightLimit is SESSION-GLOBAL in vanilla (Biome.BIG_TREE_FEATURE
     * singleton field): the first big oak of the session draws 5+nextInt(12) from its
     * private rand, every later one reuses / checkBlockLine-clamps the carried value.
     * Thread that carry across window builds in populate order (g_bigtree_carry). */
    w.bigtree_heightLimit = g_bigtree_carry;
    for (int x = 0; x < W_X; ++x)
        for (int z = 0; z < W_Z; ++z)
            w.fullBiome[x * W_Z + z] = g_bio[x * W_Z + z];

    {
        /* Donors: neighbor windows built EARLIER (recorded populate order). Include
         * ±2 so OOB canopy spills from a base two chunks away still seed plant-Y. */
        int wx0 = bcx * 16, wz0 = bcz * 16;
        Window *don[24];
        int nd = 0;
        for (int dx = -2; dx <= 2; ++dx)
            for (int dz = -2; dz <= 2; ++dz) {
                if (!dx && !dz) continue;
                Window *nb = find_window(seed, bcx + dx, bcz + dz);
                if (nb) don[nd++] = nb;
            }
        for (int a = 1; a < nd; ++a) {          /* insertion sort by build seq */
            Window *k = don[a];
            int b = a - 1;
            while (b >= 0 && don[b]->seq > k->seq) { don[b + 1] = don[b]; --b; }
            don[b + 1] = k;
        }
        for (int n = 0; n < nd; ++n) {
            Window *nb = don[n];
            for (int i = 0; i < nb->npop; ++i) {
                const DecCell *c = &nb->pop_cells[i];
                int lx = c->wx - wx0, lz = c->wz - wz0;
                if (lx < 0 || lx >= W_X || lz < 0 || lz >= W_Z) continue;
                int idx = w_index(lx, c->y, lz);
                g_owr[idx] = c->id;
                g_seeded[idx] = 1;
            }
        }
        /* Nested cascade build: overlay the interrupted parent's PARTIAL window
         * last (it is the latest writer in vanilla's shared world). */
        if (g_partial.active) {
            int px0 = g_partial.bcx * 16, pz0 = g_partial.bcz * 16;
            for (int lx = 0; lx < W_X; ++lx)
                for (int lz = 0; lz < W_Z; ++lz) {
                    int mx = px0 + lx - wx0, mz = pz0 + lz - wz0;
                    if (mx < 0 || mx >= W_X || mz < 0 || mz >= W_Z) continue;
                    for (int y = 0; y < W_Y; ++y) {
                        int pidx = w_index(lx, y, lz);
                        if (g_partial.owr[pidx] == g_partial.stb[pidx] &&
                            !g_partial.seeded[pidx]) continue;
                        int idx = w_index(mx, y, mz);
                        g_owr[idx] = g_partial.owr[pidx];
                        g_seeded[idx] = 1;
                    }
                }
        }
    }

    JavaRandom r;
    {
        PllLight lt = { g_sky, g_blk, g_ts, g_tb, g_skyhm,
                        popmc_stale_light_enabled() ? g_skylt : NULL };
        mc_probe_cx = bcx;
        mc_probe_cz = bcz;
        /* Arm the cascade RNG-clobber watch (mc_rng.h) with this base's events. */
        mc_jr_watch_n = 0;
        mc_jr_watch_fired = 0;
        for (int i = 0; i < g_ncasc && mc_jr_watch_n < MC_JR_WATCH_MAX; ++i)
            if (g_casc[i].bcx == bcx && g_casc[i].bcz == bcz) {
                mc_jr_watch_before[mc_jr_watch_n] = g_casc[i].before;
                mc_jr_watch_after[mc_jr_watch_n] = g_casc[i].after;
                ++mc_jr_watch_n;
            }
        int armed = mc_jr_watch_n;
        g_live_w = &w;
        oob_spill_reset();
        g_w_oob_write = oob_spill_write;
        owr_populate(&w, &r, (i64)seed, g_fol, &lt, bcx, bcz);
        g_w_oob_write = 0;
        g_live_w = NULL;
        if (armed)
            fprintf(stderr, "[populate_mc] cascade (%d,%d): %d/%d clobber jumps fired\n",
                    bcx, bcz, mc_jr_watch_fired, armed);
        if (g_oob_n && getenv("MAGMA_DEBUG_TREES"))
            fprintf(stderr, "[populate_mc] oob_spill (%d,%d): %d cells%s\n",
                    bcx, bcz, g_oob_n, g_oob_dropped ? " (cap hit)" : "");
        mc_jr_watch_n = 0;
    }
    mc_redirect_pending = 0;   /* armed but never polled: populate ended first */
    if (g_redir_win && g_redir_applied) {
        /* chunkPos corruption: the parent's post-resume writes went into the nested
         * window's g2 buffers (see cascade_hook). Re-snapshot the nested slot so those
         * writes apply/donate with its cells, then refresh the parent-window copy-back
         * so the parent snapshot below (the later writer) carries them too. */
        record_window_cells(g_redir_win, g2_owr, g2_stb, g2_seeded);
        int wx0 = bcx * 16, wz0 = bcz * 16;
        for (int i = 0; i < g_redir_win->npop; ++i) {
            const DecCell *c = &g_redir_win->pop_cells[i];
            int lx = c->wx - wx0, lz = c->wz - wz0;
            if (lx < 0 || lx >= W_X || lz < 0 || lz >= W_Z) continue;
            int idx = w_index(lx, c->y, lz);
            g_owr[idx] = c->id;
            g_seeded[idx] = 1;
            skyhm_copy_nested_col_to_parent(bcx, bcz, g_redir_win->bcx, g_redir_win->bcz,
                                            c->wx, c->wz);
        }
        fprintf(stderr, "[populate_mc] cascade (%d,%d): chunkPos-corrupted tail resnapped "
                "into (%d,%d)\n", bcx, bcz, g_redir_win->bcx, g_redir_win->bcz);
    }
    g_redir_win = NULL;
    g_redir_applied = 0;
    g_bigtree_carry = w.bigtree_heightLimit;
    g_builds++;

    /* RECYCLE the toroidal slot now; record the PRE-fluid cells (seed set), then
     * run the fluid pass and record the post-fluid cells (chunk-apply set). */
    Window *win = &g_slots[owr_tor(bcx, g_owr_D) * g_owr_D + owr_tor(bcz, g_owr_D)];
    win->seed = seed; win->bcx = bcx; win->bcz = bcz; win->valid = 1; win->seq = g_builds;
    {
        int npop = 0;
        for (int i = 0; i < W_N; ++i) if (g_owr[i] != g_stb[i] || g_seeded[i]) npop++;
        npop += g_oob_n;
        { static int g_maxpop = 0;
          if (getenv("MAGMA_DEBUG_CAPS") && npop > g_maxpop) {
              g_maxpop = npop;
              fprintf(stderr, "[owrcaps] max_pre_fluid_cells=%d\n", npop);
          } }
        if (npop > g_caps->owr_cells_max) {
            fprintf(stderr,
                "[populate_mc] FATAL: window (%d,%d) has %d pre-fluid cells > cap %d "
                "(raise owr_cells_max in magma.conf / caps.h)\n",
                bcx, bcz, npop, g_caps->owr_cells_max);
            assert(0 && "owr_cells_max exceeded");
            abort();
        }
        win->npop = npop;
        int k = 0;
        for (int lx = 0; lx < W_X; ++lx)
            for (int lz = 0; lz < W_Z; ++lz)
                for (int y = 0; y < W_Y; ++y) {
                    int idx = w_index(lx, y, lz);
                    if (g_owr[idx] != g_stb[idx] || g_seeded[idx]) {
                        win->pop_cells[k].wx = bcx * 16 + lx;
                        win->pop_cells[k].wz = bcz * 16 + lz;
                        win->pop_cells[k].y  = (unsigned short)y;
                        win->pop_cells[k].id = (unsigned short)g_owr[idx];
                        ++k;
                    }
                }
        for (int i = 0; i < g_oob_n; ++i)
            win->pop_cells[k++] = g_oob_spill[i];
    }

    /* Fluid CA default OFF: vanilla populate runs NO fluid ticks - the save's spread
     * water/lava is live-tick evolution. The baked CA (pfs_steps = 16 + seed%17 from the
     * internal stress scene) re-tags sources as flowing en masse (seed 7 swamp: 92k cells
     * java WATER -> magma FLOWING_WATER; disabling: 157k -> 14.6k mismatches, seed 0
     * roughly neutral at +712). Opt back in with MAGMA_FLUID_CA=1. */
    if (getenv("MAGMA_FLUID_CA"))
        owfl_fluid_pass(&w, (i64)seed, g_cur, g_tmp, g_bca);

    /* Record cells = (window != its own pure base) UNION seeded locations UNION
     * OOB spills (absolute world coords outside this 32x32). Spills let neighbor
     * windows seed edge canopies and let apply_window place them on the right chunk. */
    int ndec = 0;
    for (int i = 0; i < W_N; ++i) if (g_owr[i] != g_stb[i] || g_seeded[i]) ndec++;
    ndec += g_oob_n;
    if (getenv("MAGMA_DEBUG_TREES")) {
        int nlog = 0, nleaf = 0, ndecdiff = 0;
        for (int i = 0; i < W_N; ++i) {
            if (pb_isLog((int)g_owr[i])) nlog++;
            if (pb_isLeaves((int)g_owr[i])) nleaf++;
            if (g_owr[i] != g_stb[i]) ndecdiff++;
        }
        int biome_center = (int)w.fullBiome[16 * W_Z + 16];
        fprintf(stderr, "[trees] window base(%d,%d) biome_center=%d logs=%d leaves=%d dec=%d oob=%d\n",
                bcx, bcz, biome_center, nlog, nleaf, ndecdiff, g_oob_n);
    }
    { static int g_maxdec = 0; if (getenv("MAGMA_DEBUG_CAPS") && ndec > g_maxdec) {
        g_maxdec = ndec; fprintf(stderr, "[owrcaps] max_window_cells=%d\n", ndec); } }

    if (ndec > g_caps->owr_cells_max) {
        fprintf(stderr,
            "[populate_mc] FATAL: window (%d,%d) has %d decoration cells > cap %d "
            "(raise owr_cells_max in magma.conf / caps.h)\n",
            bcx, bcz, ndec, g_caps->owr_cells_max);
        assert(0 && "owr_cells_max exceeded");
        abort();
    }

    win->ncells = ndec;

    int k = 0;
    for (int lx = 0; lx < W_X; ++lx)
        for (int lz = 0; lz < W_Z; ++lz)
            for (int y = 0; y < W_Y; ++y) {
                int idx = w_index(lx, y, lz);
                if (g_owr[idx] != g_stb[idx] || g_seeded[idx]) {
                    win->cells[k].wx = bcx * 16 + lx;
                    win->cells[k].wz = bcz * 16 + lz;
                    win->cells[k].y  = (unsigned short)y;
                    win->cells[k].id = (unsigned short)g_owr[idx];
                    ++k;
                }
            }
    for (int i = 0; i < g_oob_n; ++i)
        win->cells[k++] = g_oob_spill[i];
    return win;
}

/* Apply the window's decoration cells that fall inside chunk (cx,cz). */
static void apply_window(const Window *win, int cx, int cz, u16 *chunk_block) {
    if (!win) return;
    int bx = cx * 16, bz = cz * 16;
    for (int i = 0; i < win->ncells; ++i) {
        const DecCell *c = &win->cells[i];
        if (c->wx < bx || c->wx >= bx + 16 || c->wz < bz || c->wz >= bz + 16) continue;
        int lx = c->wx - bx, lz = c->wz - bz;
        chunk_block[CB_INDEX_LOCAL(lx, c->y, lz)] = c->id;
    }
}

/* =============================== public API ============================== */

void popmc_decorate_chunk(long long seed, int cx, int cz, unsigned short *chunk_block) {
    if (!chunk_block) return;
    /* Chunk (cx,cz) is decorated by the four populate windows whose +8 footprint
     * overlaps it: base chunks (bcx,bcz) in {cx-1,cx} x {cz-1,cz}. OOB spills from
     * earlier neighbors arrive via donor seeding into those windows' cell lists. */
    for (int bcx = cx - 1; bcx <= cx; ++bcx)
        for (int bcz = cz - 1; bcz <= cz; ++bcz) {
            Window *win = find_window(seed, bcx, bcz);
            if (!win) win = build_window(seed, bcx, bcz);
            apply_window(win, cx, cz, chunk_block);
        }
}

long popmc_window_builds(void) { return g_builds; }

void popmc_set_probe(void (*fn)(int, int, const char *, const char *,
                                unsigned long long)) {
    mc_probe_fn = fn;
}

/* Nested cascade populate (fires from jrand_next via mc_jr_watch_hookfn at the
 * EXACT interrupted draw): vanilla generated the touched chunk and then cascade-
 * populated a newly-2x2-complete neighbor right here, mid-parent-populate. Build
 * that neighbor's window NOW - seeded with the parent's PARTIAL window (vanilla's
 * shared world) - and copy its cells back so the parent's remaining block-
 * conditional draws (jungle vines, reeds, cocoa) see them. Depth 1 only: a
 * deeper cascade keeps the cursor jump but skips block emulation. */
static void cascade_hook(unsigned long long before) {
    if (g_casc_depth) return;
    if (g_redir_win) return;  /* already redirected: g2 buffers are LIVE (parent tail is
                               * writing them) - keep the cursor jump, skip block emulation */
    int pbcx = mc_probe_cx, pbcz = mc_probe_cz;
    const PopmcCascadeEvt *e = NULL;
    for (int i = 0; i < g_ncasc; ++i)
        if (g_casc[i].bcx == pbcx && g_casc[i].bcz == pbcz &&
            g_casc[i].before == before) { e = &g_casc[i]; break; }
    if (!e || e->ncx == POPMC_CASCADE_NONE) return;

    /* save parent watch state (nested build re-arms for its own events) */
    int save_n = mc_jr_watch_n, save_fired = mc_jr_watch_fired;
    u64 save_b[MC_JR_WATCH_MAX], save_a[MC_JR_WATCH_MAX];
    memcpy(save_b, mc_jr_watch_before, sizeof(save_b));
    memcpy(save_a, mc_jr_watch_after, sizeof(save_a));

    /* expose the parent's in-progress window as the latest-writer donor */
    g_partial.active = 1;
    g_partial.bcx = pbcx; g_partial.bcz = pbcz;
    g_partial.owr = g_owr; g_partial.stb = g_stb; g_partial.seeded = g_seeded;

    /* swap the live-during-populate buffers to the depth-1 set */
    FoliageCoord *s_fol = g_fol;
    u16 *s_owr = g_owr, *s_stb = g_stb;
    u8 *s_sky = g_sky, *s_blk = g_blk, *s_ts = g_ts, *s_tb = g_tb, *s_seeded = g_seeded;
    u16 *s_skyhm = g_skyhm;
    u8 *s_skylt = g_skylt;
    int *s_bio = g_bio;
    g_fol = g2_fol; g_owr = g2_owr; g_stb = g2_stb; g_sky = g2_sky; g_blk = g2_blk;
    g_ts = g2_ts; g_tb = g2_tb; g_skyhm = g2_skyhm; g_skylt = g2_skylt;
    g_seeded = g2_seeded; g_bio = g2_bio;

    g_casc_depth = 1;
    World *s_live_w = g_live_w;
    /* WorldGenBigTree.heightLimit is a SESSION singleton in vanilla: the nested
     * populate must see the parent's current value and the parent's remaining
     * big trees must see the nested evolution (foliage geometry depends on it;
     * the main RNG stream does not, so only blocks betray a stale carry). */
    if (g_live_w) g_bigtree_carry = g_live_w->bigtree_heightLimit;
    Window *nb = build_window(g_build_seed, e->ncx, e->ncz);
    g_live_w = s_live_w;
    if (g_live_w) g_live_w->bigtree_heightLimit = g_bigtree_carry;
    g_casc_depth = 0;

    g_fol = s_fol; g_owr = s_owr; g_stb = s_stb; g_sky = s_sky; g_blk = s_blk;
    g_ts = s_ts; g_tb = s_tb; g_skyhm = s_skyhm; g_skylt = s_skylt;
    g_seeded = s_seeded; g_bio = s_bio;
    g_partial.active = 0;
    mc_probe_cx = pbcx;
    mc_probe_cz = pbcz;
    mc_jr_watch_n = save_n;
    mc_jr_watch_fired = save_fired;
    memcpy(mc_jr_watch_before, save_b, sizeof(save_b));
    memcpy(mc_jr_watch_after, save_a, sizeof(save_a));

    if (nb) {   /* copy nested cells overlapping the parent window back in */
        int wx0 = pbcx * 16, wz0 = pbcz * 16;
        for (int i = 0; i < nb->npop; ++i) {
            const DecCell *c = &nb->pop_cells[i];
            int lx = c->wx - wx0, lz = c->wz - wz0;
            if (lx < 0 || lx >= W_X || lz < 0 || lz >= W_Z) continue;
            int idx = w_index(lx, c->y, lz);
            g_owr[idx] = c->id;
            g_seeded[idx] = 1;
            skyhm_copy_nested_col_to_parent(pbcx, pbcz, e->ncx, e->ncz, c->wx, c->wz);
        }
        fprintf(stderr, "[populate_mc] cascade (%d,%d): nested populate (%d,%d) emulated\n",
                pbcx, pbcz, e->ncx, e->ncz);
        /* Vanilla chunkPos corruption: the nested decorate() overwrote the shared
         * BiomeDecorator.chunkPos and it is NEVER restored - the parent's remaining
         * decoration positions are relative to the NESTED chunk's corner (proven by
         * seed-1 (9,5) TreeProbeDecorator trace: post-resume attempts log under
         * (10,5) with (10,5)-frame heights). Only the SAME Biome instance shares the
         * decorator, so arm the deferred swap only when the nested decorate biome
         * matches the parent's (window-center voronoi ids). redirect_apply fires at
         * the next position draw (MC_REDIRECT_POLL); the nested slot is then
         * re-snapshotted when the parent's populate returns (build_window). Foliage
         * coords and the light windows keep the parent frame - cosmetic only. */
        if (g_live_w && g2_bio[16 * W_Z + 16] == g_bio[16 * W_Z + 16]) {
            g_redir_ncx = e->ncx;
            g_redir_ncz = e->ncz;
            g_redir_pbcx = pbcx;
            g_redir_pbcz = pbcz;
            g_redir_win = nb;
            mc_redirect_pending = 1;
        }
    }
    /* Tell the probe log the parent's populate stream resumes here (the C log has
     * no POST lines, so genprobe_diff needs this to re-attribute what follows). */
    if (mc_probe_fn) mc_probe_fn(pbcx, pbcz, "RESUME", "-", 0ULL);
}

void popmc_set_cascade(const PopmcCascadeEvt *evts, int n) {
    g_casc = evts;
    g_ncasc = n;
    mc_jr_watch_hookfn = n ? cascade_hook : (void (*)(unsigned long long))0;
    mc_redirect_apply = n ? redirect_apply : (void (*)(struct World *))0;
    mc_redirect_restore = n ? redirect_restore : (void (*)(struct World *))0;
}

void popmc_prepare(long long seed, const int *bases_xz, int nbases) {
    for (int i = 0; i < nbases; ++i) {
        int bcx = bases_xz[i * 2], bcz = bases_xz[i * 2 + 1];
        if (!find_window(seed, bcx, bcz)) build_window(seed, bcx, bcz);
    }
}

int popmc_opacity(int id) { return pb_opacity(id); }

int popmc_emission(int id) {
    return pb_isLava(id) ? 15 : 0;   /* only lava emits in this block set */
}
