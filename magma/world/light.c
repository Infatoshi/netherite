/* world/light.c - LIGHT module for magma (see world/light.h).
 *
 * Sky light, block light, and biome tint over the blaze voxel world.
 *
 * Blocks come from blaze's cp_provide_chunk as compact renderer model keys. A
 * parallel packed-vanilla state array is authoritative for gameplay and lighting;
 * game/block_registry.h performs the explicit conversion at chunk generation.
 *
 * Sky light   : per column, Chunk.generateSkylightMap ladder (cr_k17_...).
 *               We drive it over the full 0..255 column with every 16-block
 *               storage section treated as non-null (nn=1). Vanilla instead
 *               leaves all-air sections NULL and lets getLightFor default them
 *               to 15; the *returned* light value above terrain is 15 either
 *               way, so this reproduces vanilla light values while being
 *               self-contained. Documented approximation: storage null-ness is
 *               not modelled; it only affects storage layout, not light values.
 *
 * Block light : global BFS over all loaded chunks. Seed every emitter cell with
 *               its emission, then flood outward; each step subtracts the target
 *               cell's clamped light-opacity, exactly matching World.getRawLight
 *               (opacity>=15 with own emission -> 1; opacity<1 -> 1). For a pure
 *               brightening field this equals running checkLightFor() from every
 *               source. The getRawLight formula is bit-verified in the test via
 *               the k16 golden replay.
 *
 * Biome tint  : real per-column biome id from the blaze GenLayer stack
 *               (gl_build/gl_getInts, biomeIndexLayer/voronoi full-res), then the
 *               REAL Minecraft colouring: the biome's temperature+rainfall index
 *               the actual 256x256 grass/foliage colormap PNGs (assets/
 *               colormap_gen.h), with the exact BiomeSwamp/BiomeForest-ROOFED/
 *               BiomeMesa colour overrides and per-biome waterColor, then the real
 *               BiomeColorHelper k18 3x3 channel-average blend over the 9
 *               neighbouring columns. Temperature uses Biome.getFloatTemperature
 *               (base + y>64 elevation/TEMPERATURE_NOISE). Verified bit-exact vs
 *               verbatim-Java golden by game/test_biome_color.sh (wy<=64 path).
 */
#include <stdlib.h>
#include <string.h>

#include <assert.h>
#include "core/config.h"   /* cr_cfg()->no_decor / no_skyspread */
#include <stdio.h>

#include "chunk_provider.h"      /* cp_provide_chunk, ChunkPrimer, CB_*, CpScratch */
#include "structures.h"          /* mandatory stronghold-only live terrain subset */
#include "nether_full.h"
#include "chunk_provider_end.h"
#include "chunk_provider_flat.h" /* verified vanilla-default superflat provider */
#include "block_props_table.h"   /* mc_bpt_props */
#include "genlayer_biomes.h"     /* gl_build, gl_getInts */
#include "mc_math.h"             /* McSinTable, mc_sin_table_init */

/* Single background base-terrain prefetch worker (world/gen_prefetch.c).
 * WEAK: unit-test binaries link light.o without gen_prefetch.o (prefetch
 * simply off); game binaries pick it up via the world/*.c wildcard. The
 * worker runs generation in its OWN translation unit, so the per-TU statics
 * in chunk_provider.h never cross threads. Bytes are identical either way. */
extern void genpf_start(long long seed, int radius) __attribute__((weak));
extern void genpf_hint(int ccx, int ccz) __attribute__((weak));
extern int  genpf_take(int cx, int cz, unsigned short *out) __attribute__((weak));

#include "game/caps.h"           /* CrCaps: toroidal light-pool geometry */
#include "game/block_registry.h" /* PB model key <-> canonical vanilla state */
#include "world/light.h"
#include "world/populate_mc.h"      /* decoration overlay (trees/foliage/plants) */

/* ============================ pure reference kernels ====================== */

int cr_k17_skylight_column(int topSeg, int hasSky,
                           const int *op, const int *nn, int *sky) {
    int nY = topSeg + 16;
    int heightMap = 0;
    for (int l = topSeg + 16; l > 0; --l) {
        if (op[l - 1] != 0) { heightMap = l; break; }
    }
    for (int y = 0; y < nY; ++y) sky[y] = 0;
    if (hasSky) {
        int k1 = 15, i1 = nY - 1;
        while (1) {
            int j1 = op[i1];
            if (j1 == 0 && k1 != 15) j1 = 1;
            k1 -= j1;
            if (k1 > 0 && nn[i1]) sky[i1] = k1;
            --i1;
            if (i1 <= 0 || k1 <= 0) break;
        }
    }
    return heightMap;
}

int cr_k18_blend3x3(const int c[9]) {
    int i = 0, j = 0, k = 0;
    for (int n = 0; n < 9; ++n) {
        int l = c[n];
        i += (l & 16711680) >> 16;
        j += (l & 65280) >> 8;
        k += l & 255;
    }
    return (i / 9 & 255) << 16 | (j / 9 & 255) << 8 | (k / 9 & 255);
}

int cr_k14_light_query(int nb, int up, int east, int west,
                       int south, int north, int own) {
    if (!nb) return own;
    int m = up;
    if (east  > m) m = east;
    if (west  > m) m = west;
    if (south > m) m = south;
    if (north > m) m = north;
    return m;
}

int cr_k15_combine(int sky, int block, int override_val) {
    if (block < override_val) block = override_val;
    unsigned int packed = ((unsigned int)sky << 20) | ((unsigned int)block << 4);
    return (int)packed;
}

static int state_opacity(u16 state) {
    int id = gm_state_id(state);
    /* Promoted dynamic blocks omitted from the old KEEP table retain their
     * registered vanilla opacity here. Double plants and redstone controls do
     * not attenuate light; BlockHopper's constructor likewise records zero. */
    if (id == 29 || id == 33 || id == 34 || id == 36 || id == 66
            || id == 55 || id == 69 || id == 75 || id == 76 || id == 77
            || id == 131 || id == 132 || id == 151 || id == 154
            || id == 178
            || id == 175)
        return 0;
    return mc_bpt_props(id).light_opacity;
}
static int state_emission(u16 state) {
    int id = gm_state_id(state);
    /* Lit redstone lamp is the first promoted redstone block; the legacy KEEP
     * property table intentionally omitted the whole redstone ID range. */
    if (id == 76) return 7;
    if (id == 124) return 15;
    if (id == 150) return 9;
    return mc_bpt_props(id).light_emit;
}

/* =============================== chunk store ============================== */

#define WY 256
#define CB_INDEX(x, y, z) ((x) << 12 | (z) << 8 | (y)) /* matches cb_index() */

typedef struct {
    int cx, cz;
    int valid;        /* slot holds a real generated+lit chunk */
    int sky_dirty;    /* this chunk's cells changed since the last sky spread:
                         only dirty chunks (and their 4 face neighbours) can
                         hold NEW spread seeds - the rest of the pool is
                         already at the flood's fixed point (monotonic raise,
                         order-independent), so the seed scan skips it. */
    u16 block[65536]; /* CB ids, index CB_INDEX(x,y,z) */
    u16 state[65536]; /* authoritative packed vanilla state for gameplay */
    u8  meta[65536];  /* legacy meta 0..15 (doors/facing); gen leaves 0 */
    u8  sky[65536];   /* 0..15 */
    u8  blk[65536];   /* 0..15 */
    u16 height[256];   /* Chunk.heightMap, indexed x + z*16, range 0..256 */
    int biome[256];   /* full-res biome id, index x + z*16 */
} LChunk;

/* BFS cell for the block-light flood; queue is owned by CrLight (allocated once
 * in light_create, reused every recompute) instead of malloc/free per call. */
typedef struct { int wx, wy, wz; } LCell;

/* ALLOCATE-ONCE toroidal light pool. A fixed (2R+3)^2 pool (view region + 1-chunk
 * apron) indexed by (cx,cz) modulo light_D. Each in-region chunk owns a unique slot;
 * a chunk scrolling out frees its slot for the incoming one, which RECYCLES it
 * (re-generate + re-light). Block light is local (radius <= 15 = one chunk), so every
 * VIEWED chunk (within R) keeps correct block light as long as its 1-chunk apron is
 * resident - which it always is. No malloc/free after init. */
struct CrLight {
    long long   seed;
    int         world_type;      /* 0 overworld, 1 superflat, 2 Nether, 3 End */
    McSinTable  st;
    const CrCaps *caps;
    LChunk    **slots;        /* light_slots pre-allocated LChunk, toroidal */
    int         light_D, light_slots;
    ChunkPrimer *primer;      /* reusable per-chunk gen scratch (alloc once) */
    CpScratch   *scratch;
    CpnHellScratch *hell_scratch;
    CpnHellNoise *hell_noise;
    CpeScratch *end_scratch;
    LCell      *q;            /* block-light BFS queue, allocated once */
    size_t      qcap;         /* capacity (power of two) */
    int         blocklight_dirty; /* recompute block light only when a block changed */
    LCell      *sq;           /* sky-light spread BFS queue, allocated once */
    size_t      sqcap;        /* capacity (power of two) */
    int         skylight_dirty;   /* recompute sky-light spread when terrain changed */
    int         column_relight_dirty; /* bulk snapshot replaced column topology */
    long long   gen_events;       /* chunks generated; worldgen writes (population
                                     spill into neighbours) bypass set_block, so
                                     window-refill memos must fold this in */
    int         dimension;        /* 0 overworld, -1 Nether, 1 End */
    int         has_sky;          /* WorldProvider.hasSkyLight() */
    float       sun_brightness;   /* updateLightmap input */
    float       torch_flicker_x;  /* updateLightmap input */
    float       gamma;            /* GameSettings.gammaSetting */
};

static inline int l_tor(int v, int D) { int m = v % D; if (m < 0) m += D; return m; }
static inline int l_slot(const CrLight *L, int cx, int cz) {
    return l_tor(cx, L->light_D) * L->light_D + l_tor(cz, L->light_D);
}

CrLight *light_create(long long seed) {
    return light_create_type(seed, 0);
}

CrLight *light_create_type(long long seed, int world_type) {
    CrLight *L = (CrLight *)calloc(1, sizeof(CrLight));
    if (!L) return NULL;
    L->seed = seed;
    L->world_type = world_type;
    L->dimension = world_type==2?-1:(world_type==3?1:0);
    L->has_sky = world_type<2;
    L->sun_brightness = world_type==2?0.2f:1.0f;
    L->torch_flicker_x = 0.0f;
    L->gamma = 0.0f;
    L->caps = cr_caps();
    L->light_D     = L->caps->light_D;
    L->light_slots = L->caps->light_slots;
    mc_sin_table_init(&L->st);

    /* fixed toroidal pool: every slot's LChunk pre-allocated up front. */
    L->slots = (LChunk **)calloc((size_t)L->light_slots, sizeof(LChunk *));
    if (!L->slots) { free(L); return NULL; }
    for (int i = 0; i < L->light_slots; ++i) {
        L->slots[i] = (LChunk *)calloc(1, sizeof(LChunk));
        if (!L->slots[i]) { light_destroy(L); return NULL; }
    }
    /* one reusable primer/scratch pair (was malloc'd per gen_chunk). */
    L->primer  = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    L->scratch = (CpScratch *)malloc(sizeof(CpScratch));
    if (!L->primer || !L->scratch) { light_destroy(L); return NULL; }
    if(world_type==2){
        L->hell_scratch=(CpnHellScratch *)malloc(sizeof(CpnHellScratch));
        L->hell_noise=(CpnHellNoise *)malloc(sizeof(CpnHellNoise));
        if(!L->hell_scratch||!L->hell_noise){light_destroy(L);return NULL;}
        cpn_noise_init(L->hell_noise,(i64)seed);
    }else if(world_type==3){
        L->end_scratch=(CpeScratch *)malloc(sizeof(CpeScratch));
        if(!L->end_scratch){light_destroy(L);return NULL;}
    }

    /* One-time BFS queue: block-light radius <= 15 so the ring never overruns.
     * Same 1<<20 capacity the per-call malloc used, now owned for the lifetime. */
    L->qcap = 1u << 20;
    L->q = (LCell *)malloc(L->qcap * sizeof(LCell));
    if (!L->q) { light_destroy(L); return NULL; }

    /* Sky-light horizontal-spread queue. The skylight penumbra boundary (shadowed
     * cells within ~15 blocks of open sky - tree/overhang undersides, cliff faces)
     * is a much larger frontier than block light's (radius<=15) local flood. The
     * peak live frontier measured over the full 361-chunk R=8 region is ~128k
     * cells; 1<<21 gives ~16x headroom. An overrun aborts LOUDLY (SPUSH guard)
     * rather than silently wrapping past an unprocessed head. 2M*12B = 24 MB. */
    L->sqcap = 1u << 21;
    L->sq = (LCell *)malloc(L->sqcap * sizeof(LCell));
    if (!L->sq) { light_destroy(L); return NULL; }

    L->blocklight_dirty = 1; /* first light_ensure must compute */
    L->skylight_dirty   = 1;

    /* Overworld: warm the single base-terrain prefetch worker. Radius matches
     * the light pool geometry so the worker stays exactly one region ahead. */
    if (world_type == 0 && genpf_start)
        genpf_start(seed, (L->light_D - 1) / 2);
    return L;
}

void light_destroy(CrLight *L) {
    if (!L) return;
    if (L->slots) {
        for (int i = 0; i < L->light_slots; ++i) free(L->slots[i]);
        free(L->slots);
    }
    free(L->primer);
    free(L->scratch);
    free(L->hell_scratch);
    free(L->hell_noise);
    free(L->end_scratch);
    free(L->q);
    free(L->sq);
    free(L);
}

static LChunk *find_chunk(const CrLight *L, int cx, int cz) {
    LChunk *c = L->slots[l_slot(L, cx, cz)];
    if (c->valid && c->cx == cx && c->cz == cz) return c;
    return NULL;
}

/* ---------------- per-chunk generation + sky light ------------------------ */

static void compute_skylight(const CrLight *L, LChunk *c) {
    int op[WY], nn[WY], sky[WY];
    for (int y = 0; y < WY; ++y) nn[y] = 1; /* every section treated non-null */
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            for (int y = 0; y < WY; ++y)
                op[y] = state_opacity(c->state[CB_INDEX(x, y, z)]);
            /* topSeg 240 -> nY 256; provider controls whether sky storage exists. */
            c->height[x + z * 16] =
                (u16)cr_k17_skylight_column(240, L->has_sky, op, nn, sky);
            for (int y = 0; y < WY; ++y)
                c->sky[CB_INDEX(x, y, z)] = (u8)sky[y];
        }
    }
}

static LChunk *gen_chunk(CrLight *L, int cx, int cz) {
    LChunk *c = L->slots[l_slot(L, cx, cz)];
    if (c->valid && c->cx == cx && c->cz == cz) return c;   /* already resident */

    /* RECYCLE this toroidal slot for the incoming chunk (its former occupant, if any,
     * is D chunks away and has left the region). All arrays below are fully overwritten,
     * so no clear is needed beyond re-stamping identity. */
    c->cx = cx; c->cz = cz; c->valid = 0;

    ChunkPrimer *primer = L->primer;   /* reusable, allocated once in light_create */
    CpScratch  *scratch = L->scratch;

    if (L->world_type == 1)
        cpf_provide_chunk((CpfPrimer *)primer, NULL);
    else if(L->world_type==2){
        nf_run((CpnPrimer *)primer,L->hell_scratch,&L->st,L->hell_noise,
               (i64)L->seed,cx,cz);
        for(int i=0;i<65536;++i)if(primer->data[i])primer->data[i]|=0x8000u;
    }else if(L->world_type==3){
        cpe_provide_chunk((CpePrimer *)primer,L->end_scratch,(i64)L->seed,cx,cz);
        for(int i=0;i<65536;++i)primer->data[i]=primer->data[i]==CE_END_STONE?
            (u16)(0x8000u|121u):0;
    }else if (!(genpf_take && genpf_take(cx, cz, primer->data)))
        st_run_features(primer, scratch, &L->st, (i64)L->seed, cx, cz, -1);
    memcpy(c->block, primer->data, sizeof(c->block));
    memset(c->meta, 0, sizeof(c->meta)); /* gen primer is id-only; live edits set meta */

    /* Overlay MC populate() decoration (trees/leaves/foliage/lakes) onto THIS chunk,
     * at ANY (cx,cz) - not just the origin 2x2. popmc_decorate_chunk runs blaze's
     * position-parametrized owr_run for the four populate windows that decorate this
     * chunk (base chunks {cx-1,cx}x{cz-1,cz}), cached compute-once, and writes only
     * the DECORATION cells over the base terrain (see world/populate_mc.c). Base
     * terrain stays exactly what cp_provide_chunk produced. */
    if (L->world_type == 0 && !cr_cfg()->no_decor)
        popmc_decorate_chunk_meta(L->seed, cx, cz, c->block, c->meta);

    /* Freeze worldgen's compact PB model keys into authoritative vanilla states
     * before gameplay can observe the chunk. Never guess an unknown key: doing so
     * would recreate the exact namespace collision this bridge exists to prevent. */
    for (int i = 0; i < 65536; ++i) {
        uint16_t state = 0;
        if (gm_model_key_to_state(
                (int)c->block[i], (int)c->meta[i], &state) == GM_MAP_UNSUPPORTED) {
            fprintf(stderr, "[light] FATAL: unsupported worldgen model key %u in chunk (%d,%d)\n",
                    (unsigned)c->block[i], cx, cz);
            abort();
        }
        c->state[i] = (u16)state;
        /* Raw-id passthrough keys (0x8000|id, Nether/End terrain) are a state
         * encoding, not a render key: bm_block() would fall back to stone for
         * every block. Re-derive the canonical render model key from state. */
        if (c->block[i] & (0x8000u | 0x4000u))
            c->block[i] = (u16)gm_state_to_model_key((uint16_t)state);
    }

    /* Support sweep: worldgen decoration windows can strand plants on sand/air
     * (context-divergence residual); real MC never generates an unsupported
     * plant, so removing them moves the world CLOSER to the oracle. Ascending y
     * cascades correctly (removed reed/double-plant bottoms take tops with
     * them). Mushrooms are exempt: vanilla lets them sit on any solid block. */
    if (L->world_type == 0) {
        for (int lx = 0; lx < 16; ++lx)
            for (int lz = 0; lz < 16; ++lz)
                for (int wy = 1; wy < WY; ++wy) {
                    int i  = CB_INDEX(lx, wy, lz);
                    int id = c->state[i] >> 4, meta = c->state[i] & 0xF;
                    int below = c->state[CB_INDEX(lx, wy - 1, lz)] >> 4;
                    int ok;
                    switch (id) {
                    case 6: case 31: case 37: case 38:              /* sapling/grass/flowers */
                        ok = below == 2 || below == 3 || below == 60; break;
                    case 32:                                        /* deadbush */
                        ok = below == 3 || below == 12 || below == 159 || below == 172; break;
                    case 83:                                        /* reeds */
                        ok = below == 83 || below == 2 || below == 3 || below == 12; break;
                    case 175:                                       /* double plant */
                        ok = meta >= 8 ? below == 175
                                       : (below == 2 || below == 3); break;
                    case 111:                                       /* waterlily */
                        ok = below == 8 || below == 9; break;
                    case 81:                                        /* cactus */
                        ok = below == 12 || below == 81; break;
                    default: continue;
                    }
                    if (!ok) { c->state[i] = 0; c->block[i] = 0; }
                }
    }

    /* full-res biome (biomeIndexLayer / voronoi), same as cp getBiomes. gl_getInts
     * returns bump-arena memory (owned by scratch->arena), not a malloc'd buffer -
     * copy out, never free it; reset the arena at this top-level call site. */
    if (L->world_type != 0) {
        int biome=L->world_type==2?8:(L->world_type==3?9:1);
        for (int i = 0; i < 256; ++i) c->biome[i] = biome;
    } else {
        GLNode nodes[GL_MAX_NODES];
        int voronoi;
        gl_build(nodes, (i64)L->seed, &voronoi);
        scratch->arena.off = 0;
        int *fb = gl_getInts(nodes, &scratch->arena, voronoi,
                             cx * 16, cz * 16, 16, 16);
        if (fb)
            for (int i = 0; i < 256; ++i) c->biome[i] = fb[i];
    }

    compute_skylight(L, c);

    c->valid = 1;
    c->sky_dirty = 1;
    L->gen_events++;
    L->blocklight_dirty = 1; /* new terrain -> block light must be recomputed */
    L->skylight_dirty   = 1; /* new terrain -> sky-light spread must be recomputed */
    return c;
}

/* ---------------- global block-light BFS over loaded chunks --------------- */

/* world-coord block/light accessors used by the BFS (NULL chunk => unloaded). */
static LChunk *chunk_of(CrLight *L, int wx, int wz) {
    int cx = wx >> 4, cz = wz >> 4;
    return find_chunk(L, cx, cz);
}
static int world_opacity(CrLight *L, int wx, int wy, int wz, int *loaded) {
    LChunk *c = chunk_of(L, wx, wz);
    *loaded = (c != NULL);
    if (!c) return 0;
    return state_opacity(c->state[CB_INDEX(wx & 15, wy, wz & 15)]);
}

static void compute_blocklight(CrLight *L) {
    /* Dirty-gate: a frame with no block edit and no new chunk skips the BFS
     * entirely (result is already correct for the unchanged world state). */
    if (!L->blocklight_dirty) return;
    L->blocklight_dirty = 0;

    for (int i = 0; i < L->light_slots; ++i) {
        LChunk *c = L->slots[i];
        if (c->valid) memset(c->blk, 0, sizeof(c->blk));
    }

    /* one-time ring buffer queue owned by L; block light radius <= 15 so it
     * terminates without wrapping past the head. */
    LCell *q = L->q;
    size_t qcap = L->qcap;
    if (!q) return;
    size_t head = 0, tail = 0;
#define QPUSH(X, Y, Z) do { q[tail].wx = (X); q[tail].wy = (Y); q[tail].wz = (Z); \
                            tail = (tail + 1) & (qcap - 1); } while (0)

    /* seed emitters */
    for (int i = 0; i < L->light_slots; ++i) {
        LChunk *c = L->slots[i];
        if (!c->valid) continue;
        for (int x = 0; x < 16; ++x)
            for (int z = 0; z < 16; ++z)
                for (int y = 0; y < WY; ++y) {
                    int e = state_emission(c->state[CB_INDEX(x, y, z)]);
                    if (e > 0) {
                        c->blk[CB_INDEX(x, y, z)] = (u8)e;
                        QPUSH(c->cx * 16 + x, y, c->cz * 16 + z);
                    }
                }
    }

    static const int dx[6] = { 0, 0, -1, 1, 0, 0 };
    static const int dy[6] = { -1, 1, 0, 0, 0, 0 };
    static const int dz[6] = { 0, 0, 0, 0, -1, 1 };

    while (head != tail) {
        LCell cur = q[head];
        head = (head + 1) & (qcap - 1);
        LChunk *cc = chunk_of(L, cur.wx, cur.wz);
        if (!cc) continue;
        int curv = cc->blk[CB_INDEX(cur.wx & 15, cur.wy, cur.wz & 15)];
        if (curv <= 1) continue;
        for (int f = 0; f < 6; ++f) {
            int nx = cur.wx + dx[f], ny = cur.wy + dy[f], nz = cur.wz + dz[f];
            if (ny < 0 || ny >= WY) continue;
            LChunk *nc = chunk_of(L, nx, nz);
            if (!nc) continue;
            int ni = CB_INDEX(nx & 15, ny, nz & 15);
            /* getRawLight opacity clamp for the target cell */
            int op = state_opacity(nc->state[ni]);
            if (op >= 15 && state_emission(nc->state[ni]) > 0) op = 1;
            if (op < 1) op = 1;
            int nl = curv - op;
            if (nl > nc->blk[ni]) {
                nc->blk[ni] = (u8)nl;
                QPUSH(nx, ny, nz);
            }
        }
    }
#undef QPUSH
    (void)world_opacity; /* accessor kept for clarity; BFS inlines the lookup */
}

/* --------------- horizontal sky-light spread over loaded chunks ------------ */

/* Chunk.generateSkylightMap gives only the straight-down column value: a cell is
 * 15 where it can see the sky vertically, and drops to 0 the moment ANYTHING
 * opaque sits above it in its own column. Real MC then runs checkLightFor, which
 * floods skylight HORIZONTALLY (attenuating by max(1,opacity) per block) into
 * vertically-shadowed cells - under tree canopy, overhangs, cliff faces. Without
 * that flood those sky-exposed-but-shadowed air cells stay 0, so the terrain face
 * looking into them samples sky=0/blk=0 and renders pure black (the "black blob on
 * a lit hillside"). Genuine caves stay dark: skylight attenuates to 0 within ~15
 * blocks, so no lit neighbour ever reaches a deep cell - they are never raised.
 *
 * We do NOT reset the column baseline here: gen_chunk already computed each
 * chunk's straight-down column floor once, and this flood only ever RAISES cells
 * (monotonic), so re-running it as chunks stream in converges to the same fixed
 * point without a full 361-chunk recompute (that reset cost ~40ms and mattered
 * only for block REMOVAL, which the single-chunk edit path does not re-column
 * anyway). This keeps the per-chunk-load relight cheap. */
static int sky_op(u16 state) { int op = state_opacity(state); return op < 1 ? 1 : op; }

/* Per-block sky-flood attenuation LUT (opacity clamped to >=1), built once. The
 * seed scan touches ~23M cells x up-to-6 neighbours per relight; replacing the
 * state_opacity() call (branch + block-props lookup) with an array read here is the
 * difference between a ~130ms and a ~30ms scan. Real block ids are small (CB_*
 * The LUT spans every packed u16 state, so metadata remains part of the canonical
 * representation without adding a branch to the hot flood loop. */
#define SKY_LUT_N 65536
static u8  g_sky_lut[SKY_LUT_N];
static int g_sky_lut_ready = 0;
static void sky_lut_init(void) {
    for (int state = 0; state < SKY_LUT_N; ++state) {
        int v = sky_op((u16)state);
        g_sky_lut[state] = (u8)(v < 1 ? 1 : (v > 255 ? 255 : v));
    }
    g_sky_lut_ready = 1;
}

/* true if neighbour cell (CH,NI) can be raised from a source of sky level S. */
#define SKY_STEP(CH, NI, S) ((CH)->sky[NI] < (S) - g_sky_lut[(CH)->state[NI]])

static void compute_skylight_spread(CrLight *L) {
    if (!L->skylight_dirty) return;
    L->skylight_dirty = 0;
    /* Diagnostic A/B: vanilla's saved skylight has NO frontier BFS (stale
     * relightBlock stomps only) - this switch isolates how much of a scene's
     * pixel diff is the horizontal flood over-brightening vs the real game. */
    {
        static int off = -1;
        if (off < 0) off = cr_cfg()->no_skyspread ? 1 : 0;
        if (off) return;
    }
    if (!g_sky_lut_ready) sky_lut_init();

    LCell *q = L->sq;
    size_t qcap = L->sqcap;
    if (!q) return;
    size_t head = 0, tail = 0;
#define SPUSH(X, Y, Z) do { q[tail].wx = (X); q[tail].wy = (Y); q[tail].wz = (Z); \
                            tail = (tail + 1) & (qcap - 1);                        \
                            if (tail == head) {                                    \
                                fprintf(stderr, "[skyspread] FATAL: frontier "     \
                                    "overran queue cap %zu\n", qcap);              \
                                assert(0 && "skylight spread queue overflow");     \
                                abort();                                           \
                            } } while (0)

    static const int dx[6] = { 0, 0, -1, 1, 0, 0 };
    static const int dy[6] = { -1, 1, 0, 0, 0, 0 };
    static const int dz[6] = { 0, 0, 0, 0, -1, 1 };
    const int SX = 1 << 12, SZ = 1 << 8;   /* CB_INDEX strides for +x, +z */

    /* boundary seed: enqueue only cells that can raise a neighbour (the penumbra
     * edge), not every lit air cell. Interior neighbours are reached by direct
     * array indexing; only the 4 chunk-border faces fall back to a neighbour
     * chunk pointer (precomputed once per chunk) - no chunk_of in the hot loop.
     * DIRTY-CHUNK NARROWING: only chunks whose cells changed since the last
     * spread (sky_dirty), or that face-border one, can hold new seeds - a
     * clean chunk not adjacent to a dirty one sat at the previous fixed point
     * (no cell could raise any loaded neighbour), and none of its cells or
     * loaded neighbours changed since. The flood itself still runs anywhere
     * the frontier reaches. Result is the SAME fixed point (monotonic max-
     * flood, order-independent); only the redundant 361-chunk seed rescan
     * per relight is skipped. */
    for (int i = 0; i < L->light_slots; ++i) {
        LChunk *c = L->slots[i];
        if (!c->valid) continue;
        LChunk *cXm = find_chunk(L, c->cx - 1, c->cz);
        LChunk *cXp = find_chunk(L, c->cx + 1, c->cz);
        LChunk *cZm = find_chunk(L, c->cx, c->cz - 1);
        LChunk *cZp = find_chunk(L, c->cx, c->cz + 1);
        if (!c->sky_dirty &&
            !(cXm && cXm->sky_dirty) && !(cXp && cXp->sky_dirty) &&
            !(cZm && cZm->sky_dirty) && !(cZp && cZp->sky_dirty))
            continue;
        const int wx0 = c->cx * 16, wz0 = c->cz * 16;
        for (int x = 0; x < 16; ++x)
            for (int z = 0; z < 16; ++z) {
                int col = (x << 12) | (z << 8);
                for (int y = 0; y < WY; ++y) {
                    int idx = col | y;
                    int s = c->sky[idx];
                    if (s <= 1) continue;
                    int seed = 0;
                    if (y > 0)          seed = SKY_STEP(c, idx - 1, s);
                    if (!seed && y < WY - 1) seed = SKY_STEP(c, idx + 1, s);
                    if (!seed) { if (x > 0)  seed = SKY_STEP(c, idx - SX, s);
                                 else if (cXm) seed = SKY_STEP(cXm, (15 << 12) | (z << 8) | y, s); }
                    if (!seed) { if (x < 15) seed = SKY_STEP(c, idx + SX, s);
                                 else if (cXp) seed = SKY_STEP(cXp, (z << 8) | y, s); }
                    if (!seed) { if (z > 0)  seed = SKY_STEP(c, idx - SZ, s);
                                 else if (cZm) seed = SKY_STEP(cZm, (x << 12) | (15 << 8) | y, s); }
                    if (!seed) { if (z < 15) seed = SKY_STEP(c, idx + SZ, s);
                                 else if (cZp) seed = SKY_STEP(cZp, (x << 12) | y, s); }
                    if (seed) SPUSH(wx0 + x, y, wz0 + z);
                }
            }
    }
    /* flags consumed (separate pass: the seed loop reads neighbours' flags) */
    for (int i = 0; i < L->light_slots; ++i) L->slots[i]->sky_dirty = 0;

    while (head != tail) {
        LCell cur = q[head];
        head = (head + 1) & (qcap - 1);
        LChunk *cc = chunk_of(L, cur.wx, cur.wz);
        if (!cc) continue;
        int curv = cc->sky[CB_INDEX(cur.wx & 15, cur.wy, cur.wz & 15)];
        if (curv <= 1) continue;
        for (int f = 0; f < 6; ++f) {
            int nx = cur.wx + dx[f], ny = cur.wy + dy[f], nz = cur.wz + dz[f];
            if (ny < 0 || ny >= WY) continue;
            LChunk *nc = chunk_of(L, nx, nz);
            if (!nc) continue;
            int ni = CB_INDEX(nx & 15, ny, nz & 15);
            int nl = curv - sky_op(nc->state[ni]);
            if (nl > nc->sky[ni]) {
                nc->sky[ni] = (u8)nl;
                SPUSH(nx, ny, nz);
            }
        }
    }
#undef SPUSH
}

/* =============================== public API ============================== */

long long light_gen_events(const CrLight *L) {
    return L ? L->gen_events : 0;
}

void light_ensure(CrLight *L, int ccx, int ccz, int radius) {
    if (!L) return;
    if (L->world_type == 0 && genpf_hint) genpf_hint(ccx, ccz);
    for (int cx = ccx - radius; cx <= ccx + radius; ++cx)
        for (int cz = ccz - radius; cz <= ccz + radius; ++cz)
            gen_chunk(L, cx, cz);
    /* Snapshot patches carry blocks but not Chunk SkyLight nibble arrays. A
     * whole-column replacement cannot be reproduced by treating its ordered
     * cells as independent World.checkLightFor edits: the generic spread
     * attenuates downward through air, while Chunk.generateSkylightMap keeps
     * every direct-sky air cell at 15. Rebuild the vertical baseline once per
     * bulk-load batch before applying the existing horizontal flood. */
    if (L->column_relight_dirty) {
        L->column_relight_dirty = 0;
        if (L->has_sky) {
            for (int i = 0; i < L->light_slots; ++i) {
                LChunk *c = L->slots[i];
                if (c && c->valid) { compute_skylight(L, c); c->sky_dirty = 1; }
            }
            L->skylight_dirty = 1;
        } else {
            for (int i = 0; i < L->light_slots; ++i) {
                LChunk *c = L->slots[i];
                if (c && c->valid) memset(c->sky, 0, sizeof(c->sky));
            }
            L->skylight_dirty = 0;
        }
    }
    compute_skylight_spread(L);
    compute_blocklight(L);
}

int light_loaded_chunks(const CrLight *L) {
    if (!L) return 0;
    int n = 0;
    for (int i = 0; i < L->light_slots; ++i) if (L->slots[i]->valid) n++;
    return n;
}

int light_block(const CrLight *L, int wx, int wy, int wz) {
    if (!L || wy < 0 || wy >= WY) return 0;
    LChunk *c = find_chunk(L, wx >> 4, wz >> 4);
    if (!c) return 0;
    return c->block[CB_INDEX(wx & 15, wy, wz & 15)];
}

uint16_t light_state(const CrLight *L, int wx, int wy, int wz) {
    if (!L || wy < 0 || wy >= WY) return 0;
    LChunk *c = find_chunk(L, wx >> 4, wz >> 4);
    if (!c) return 0;
    return c->state[CB_INDEX(wx & 15, wy, wz & 15)];
}

void light_debug_set_block(CrLight *L, int wx, int wy, int wz, int id) {
    light_debug_set_block_meta(L, wx, wy, wz, id, 0);
}

void light_debug_set_block_meta(CrLight *L, int wx, int wy, int wz, int id, int meta) {
    if (!L || wy < 0 || wy >= WY) return;
    LChunk *c = find_chunk(L, wx >> 4, wz >> 4);
    if (!c) return;
    int i = CB_INDEX(wx & 15, wy, wz & 15);
    uint16_t state = 0;
    if (gm_model_key_to_state(id, meta, &state) == GM_MAP_UNSUPPORTED) {
        fprintf(stderr, "[light] FATAL: debug model key %d has no canonical state\n", id);
        abort();
    }
    c->block[i] = (u16)id;
    c->meta[i]  = (u8)(meta & 15);
    c->state[i] = (u16)state;
    c->sky_dirty = 1;
    L->blocklight_dirty = 1; /* block changed -> next light_ensure recomputes */
    L->skylight_dirty   = 1;
    /* Overworld synthetic tests want full sky; Nether/End stores stay dark. */
    c->sky[i] = L->has_sky ? 15 : 0;
}

static int sky_stored(const CrLight *L, int wx, int wy, int wz) {
    if (wy < 0) wy = 0;
    if (wy >= WY) return 15;
    LChunk *c = find_chunk(L, wx >> 4, wz >> 4);
    return c ? c->sky[CB_INDEX(wx & 15, wy, wz & 15)] : 15;
}

static void sky_store(CrLight *L, int wx, int wy, int wz, int value) {
    if (wy < 0 || wy >= WY) return;
    LChunk *c = find_chunk(L, wx >> 4, wz >> 4);
    if (c) c->sky[CB_INDEX(wx & 15, wy, wz & 15)] = (u8)value;
}

static int sky_can_see(const CrLight *L, int wx, int wy, int wz) {
    LChunk *c = find_chunk(L, wx >> 4, wz >> 4);
    return c && wy >= (int)c->height[(wx & 15) + (wz & 15) * 16];
}

static int sky_raw(CrLight *L, int wx, int wy, int wz) {
    if (sky_can_see(L, wx, wy, wz)) return 15;
    if (wy < 0 || wy >= WY) return 15;
    LChunk *c = find_chunk(L, wx >> 4, wz >> 4);
    if (!c) return 15;
    u16 state = c->state[CB_INDEX(wx & 15, wy, wz & 15)];
    int emission = state_emission(state);
    int opacity = state_opacity(state);
    if (opacity >= 15 && emission > 0) opacity = 1;
    if (opacity < 1) opacity = 1;
    if (opacity >= 15) return 0;
    int value = 0;
    static const int dx[6] = { -1, 1, 0, 0, 0, 0 };
    static const int dy[6] = { 0, 0, -1, 1, 0, 0 };
    static const int dz[6] = { 0, 0, 0, 0, -1, 1 };
    for (int face = 0; face < 6; ++face) {
        int candidate =
            sky_stored(L, wx + dx[face], wy + dy[face], wz + dz[face])
            - opacity;
        if (candidate > value) value = candidate;
        if (value >= 14) return value;
    }
    return value;
}

static int sky_area_loaded(const CrLight *L, int wx, int wz, int radius) {
    int cx0 = (wx - radius) >> 4, cx1 = (wx + radius) >> 4;
    int cz0 = (wz - radius) >> 4, cz1 = (wz + radius) >> 4;
    for (int cx = cx0; cx <= cx1; ++cx)
        for (int cz = cz0; cz <= cz1; ++cz)
            if (!find_chunk(L, cx, cz)) return 0;
    return 1;
}

/* Exact World.checkLightFor(EnumSkyBlock.SKY) two-phase queue. This is the
 * bounded live-edit path Minecraft uses, including the radius-17 darkening
 * dependency walk. It intentionally operates on the current saved nibble
 * field rather than converging an entire chunk to a new global fixed point. */
static void sky_check_light_for(CrLight *L, int ox, int oy, int oz) {
    if (!L || !L->has_sky || !sky_area_loaded(L, ox, oz, 17)) return;
    enum { UPDATE_CAP = 32768 };
    u32 *updates = (u32 *)(void *)L->q;
    int read = 0, count = 0;
    int stored = sky_stored(L, ox, oy, oz);
    int raw = sky_raw(L, ox, oy, oz);
    if (raw > stored) {
        updates[count++] = 133152u;
    } else if (raw < stored) {
        updates[count++] = 133152u | (u32)stored << 18;
        while (read < count) {
            u32 packed = updates[read++];
            int wx = (int)(packed & 63u) - 32 + ox;
            int wy = (int)(packed >> 6 & 63u) - 32 + oy;
            int wz = (int)(packed >> 12 & 63u) - 32 + oz;
            int expected = (int)(packed >> 18 & 15u);
            int current = sky_stored(L, wx, wy, wz);
            if (current != expected) continue;
            sky_store(L, wx, wy, wz, 0);
            if (expected <= 0 ||
                abs(wx - ox) + abs(wy - oy) + abs(wz - oz) >= 17)
                continue;
            static const int dx[6] = { -1, 1, 0, 0, 0, 0 };
            static const int dy[6] = { 0, 0, -1, 1, 0, 0 };
            static const int dz[6] = { 0, 0, 0, 0, -1, 1 };
            for (int face = 0; face < 6 && count < UPDATE_CAP; ++face) {
                int nx = wx + dx[face], ny = wy + dy[face], nz = wz + dz[face];
                LChunk *nc = find_chunk(L, nx >> 4, nz >> 4);
                if (!nc || ny < 0 || ny >= WY) continue;
                int opacity = state_opacity(
                    nc->state[CB_INDEX(nx & 15, ny, nz & 15)]);
                if (opacity < 1) opacity = 1;
                int neighbor = sky_stored(L, nx, ny, nz);
                if (neighbor == expected - opacity) {
                    updates[count++] =
                        (u32)(nx - ox + 32)
                        | (u32)(ny - oy + 32) << 6
                        | (u32)(nz - oz + 32) << 12
                        | (u32)(expected - opacity) << 18;
                }
            }
        }
        read = 0;
    }
    while (read < count) {
        u32 packed = updates[read++];
        int wx = (int)(packed & 63u) - 32 + ox;
        int wy = (int)(packed >> 6 & 63u) - 32 + oy;
        int wz = (int)(packed >> 12 & 63u) - 32 + oz;
        int before = sky_stored(L, wx, wy, wz);
        int after = sky_raw(L, wx, wy, wz);
        if (after == before) continue;
        sky_store(L, wx, wy, wz, after);
        if (after <= before ||
            abs(wx - ox) + abs(wy - oy) + abs(wz - oz) >= 17 ||
            count >= UPDATE_CAP - 6)
            continue;
        if (sky_stored(L, wx - 1, wy, wz) < after)
            updates[count++] = (u32)(wx - 1 - ox + 32)
                | (u32)(wy - oy + 32) << 6 | (u32)(wz - oz + 32) << 12;
        if (sky_stored(L, wx + 1, wy, wz) < after)
            updates[count++] = (u32)(wx + 1 - ox + 32)
                | (u32)(wy - oy + 32) << 6 | (u32)(wz - oz + 32) << 12;
        if (sky_stored(L, wx, wy - 1, wz) < after)
            updates[count++] = (u32)(wx - ox + 32)
                | (u32)(wy - 1 - oy + 32) << 6 | (u32)(wz - oz + 32) << 12;
        if (sky_stored(L, wx, wy + 1, wz) < after)
            updates[count++] = (u32)(wx - ox + 32)
                | (u32)(wy + 1 - oy + 32) << 6 | (u32)(wz - oz + 32) << 12;
        if (sky_stored(L, wx, wy, wz - 1) < after)
            updates[count++] = (u32)(wx - ox + 32)
                | (u32)(wy - oy + 32) << 6 | (u32)(wz - 1 - oz + 32) << 12;
        if (sky_stored(L, wx, wy, wz + 1) < after)
            updates[count++] = (u32)(wx - ox + 32)
                | (u32)(wy - oy + 32) << 6 | (u32)(wz + 1 - oz + 32) << 12;
    }
}

static int sky_column_height(const LChunk *c, int lx, int lz) {
    for (int y = WY - 1; y >= 0; --y)
        if (state_opacity(c->state[CB_INDEX(lx, y, lz)]) != 0) return y + 1;
    return 0;
}

static void sky_relight_column(
    CrLight *L, LChunk *c, int wx, int wz, int old_height, int new_height
) {
    int lx = wx & 15, lz = wz & 15;
    if (new_height < old_height) {
        for (int y = new_height; y < old_height; ++y)
            c->sky[CB_INDEX(lx, y, lz)] = 15;
    } else {
        for (int y = old_height; y < new_height; ++y)
            c->sky[CB_INDEX(lx, y, lz)] = 0;
    }
    int value = 15;
    for (int y = new_height; y > 0 && value > 0;) {
        --y;
        int opacity = state_opacity(c->state[CB_INDEX(lx, y, lz)]);
        if (opacity == 0) opacity = 1;
        value -= opacity;
        if (value < 0) value = 0;
        c->sky[CB_INDEX(lx, y, lz)] = (u8)value;
    }
    int start = old_height < new_height ? old_height : new_height;
    int end = old_height > new_height ? old_height : new_height;
    static const int dx[5] = { -1, 1, 0, 0, 0 };
    static const int dz[5] = { 0, 0, -1, 1, 0 };
    for (int column = 0; column < 5; ++column)
        for (int y = start; y < end; ++y)
            sky_check_light_for(L, wx + dx[column], y, wz + dz[column]);
}

void light_set_state(CrLight *L, int wx, int wy, int wz, uint16_t state) {
    if (!L || wy < 0 || wy >= WY) return;
    LChunk *c = find_chunk(L, wx >> 4, wz >> 4);
    if (!c) return;
    int i = CB_INDEX(wx & 15, wy, wz & 15);
    int old_opacity = state_opacity(c->state[i]);
    int new_opacity = state_opacity(state);
    int old_emission = state_emission(c->state[i]);
    int new_emission = state_emission(state);
    int old_height = c->height[(wx & 15) + (wz & 15) * 16];
    c->state[i] = state;
    c->block[i] = (u16)gm_state_to_model_key((uint16_t)state);
    c->meta[i] = (u8)gm_state_meta((uint16_t)state);
    L->blocklight_dirty = 1;
    if (!L->has_sky) {
        c->sky[i] = 0;
    } else if (new_opacity != old_opacity || new_emission != old_emission) {
        int new_height = sky_column_height(c, wx & 15, wz & 15);
        c->height[(wx & 15) + (wz & 15) * 16] = (u16)new_height;
        if (new_height != old_height)
            sky_relight_column(L, c, wx, wz, old_height, new_height);
        sky_check_light_for(L, wx, wy, wz);
    }
}

void light_load_state(CrLight *L, int wx, int wy, int wz, uint16_t state) {
    if (!L || wy < 0 || wy >= WY) return;
    LChunk *c = find_chunk(L, wx >> 4, wz >> 4);
    if (!c) return;
    int i = CB_INDEX(wx & 15, wy, wz & 15);
    c->state[i] = state;
    c->block[i] = (u16)gm_state_to_model_key(state);
    c->meta[i] = (u8)gm_state_meta(state);
    L->blocklight_dirty = 1;
    L->column_relight_dirty = 1;
}

int light_load_sky_snapshot(CrLight *L, int wx, int wy, int wz, int value) {
    if (!L || wy < 0 || wy >= WY || value < 0 || value > 15) return 0;
    LChunk *c = find_chunk(L, wx >> 4, wz >> 4);
    if (!c) return 0;
    c->sky[CB_INDEX(wx & 15, wy, wz & 15)] = (u8)value;
    return 1;
}

void light_finalize_sky_snapshot(CrLight *L) {
    if (!L) return;
    /* A saved nibble array is already a concrete World/Chunk boundary, not a
     * request to derive a new fixed point. Prevent the next ordinary ensure
     * from overwriting it with the block-only reconstruction. */
    L->column_relight_dirty = 0;
    L->skylight_dirty = 0;
    for (int i = 0; i < L->light_slots; ++i)
        if (L->slots[i]) L->slots[i]->sky_dirty = 0;
}

/* A grass/dirt removal queues Chunk.recheckGaps in vanilla. Its checkLightFor
 * pass updates zero-opacity surface vegetation in the neighboring columns;
 * unlike magma's converged flood, the saved result can remain direct sky (15)
 * beneath population foliage. Preserve that observed saved-light result for
 * the affected 3x3 columns instead of relighting unrelated terrain. */
void light_recheck_break_surfaces(CrLight *L, int wx, int wy, int wz) {
    if (!L || !L->has_sky) return;
    for (int x = wx - 1; x <= wx + 1; ++x)
        for (int z = wz - 1; z <= wz + 1; ++z) {
            LChunk *c = find_chunk(L, x >> 4, z >> 4);
            if (!c) continue;
            for (int y = wy; y <= wy + 4 && y < WY; ++y) {
                int i = CB_INDEX(x & 15, y, z & 15);
                if (y > 0 && gm_state_id(c->state[i]) == 31 &&
                    state_opacity(c->state[i]) == 0 &&
                    state_opacity(c->state[CB_INDEX(x & 15, y - 1, z & 15)]) > 0)
                    c->sky[i] = 15;
            }
        }
}

void light_set_render_state(CrLight *L, int dimension,
                            float torch_flicker_x, float gamma) {
    if (!L) return;
    L->dimension = dimension;
    L->has_sky = dimension == 0;
    L->sun_brightness = cr_dimension_sun_brightness(dimension);
    L->torch_flicker_x = torch_flicker_x;
    L->gamma = gamma;

    if (L->has_sky) {
        for (int s = 0; s < L->light_slots; ++s) {
            LChunk *c = L->slots[s];
            if (c && c->valid) { compute_skylight(L, c); c->sky_dirty = 1; }
        }
        L->skylight_dirty = 1;
        compute_skylight_spread(L);
    } else {
        for (int s = 0; s < L->light_slots; ++s) {
            LChunk *c = L->slots[s];
            if (c && c->valid) memset(c->sky, 0, sizeof(c->sky));
        }
        L->skylight_dirty = 0;
    }

    L->blocklight_dirty = 1;
    compute_blocklight(L);
}

int light_dimension(const CrLight *L) {
    return L ? L->dimension : 0;
}

CrLightmapRgb light_lightmap_rgb(const CrLight *L, int sky, int block) {
    int dimension = L ? L->dimension : 0;
    float sun = L ? L->sun_brightness : 1.0f;
    float torch = L ? L->torch_flicker_x : 0.0f;
    float gamma = L ? L->gamma : 0.0f;
    return cr_lightmap_rgb(dimension, sky, block, sun, torch, gamma);
}

CrRgba light_lightmap_rgba8(const CrLight *L, int sky, int block) {
    return cr_lightmap_rgba8(light_lightmap_rgb(L, sky, block));
}

void light_zero_sky_and_relight(CrLight *L) {
    light_set_render_state(L, -1, 0.0f, 0.0f);
}

int light_meta(const CrLight *L, int wx, int wy, int wz) {
    if (!L || wy < 0 || wy >= WY) return 0;
    LChunk *c = find_chunk(L, wx >> 4, wz >> 4);
    if (!c) return 0;
    return (int)c->meta[CB_INDEX(wx & 15, wy, wz & 15)];
}

int light_sky(const CrLight *L, int wx, int wy, int wz) {
    if (!L || wy < 0 || wy >= WY) return 0;
    LChunk *c = find_chunk(L, wx >> 4, wz >> 4);
    if (!c) return 0;
    return c->sky[CB_INDEX(wx & 15, wy, wz & 15)];
}

int light_blk(const CrLight *L, int wx, int wy, int wz) {
    if (!L || wy < 0 || wy >= WY) return 0;
    LChunk *c = find_chunk(L, wx >> 4, wz >> 4);
    if (!c) return 0;
    return c->blk[CB_INDEX(wx & 15, wy, wz & 15)];
}

/* -------------------------- biome tint colours --------------------------- */
/* Real Minecraft 1.11.2 biome colouring (was a temperature-keyed hex guess).
 *
 * Cites (java/oracle-src):
 *  - Biome.getGrassColorAtPos / getFoliageColorAtPos: d0=clamp(temp,0,1),
 *    d1=clamp(rainfall,0,1) -> ColorizerGrass/Foliage.getGrassColor(d0,d1),
 *    then getModdedBiomeGrassColor/Foliage (Forge event, identity by default).
 *  - ColorizerGrass.getGrassColor / ColorizerFoliage.getFoliageColor: the exact
 *    index math replicated in colormap_index() below; the buffer is the 256x256
 *    colormap PNG (assets/colormap_gen.h, extracted by assets/build_colormap.py).
 *  - Biome.registerBiomes: per-biome temperature (cb_temperature, chunk_provider.h)
 *    / rainfall (cb_rainfall) / waterColor (cb_water_color, default 0xFFFFFF, swamp
 *    0xE0FFAE=14745518).
 *  - Subclass overrides: BiomeSwamp.getGrassColorAtPos (GRASS_COLOR_NOISE at
 *    x*0.0225,z*0.0225 -> d0<-0.1 ? 5011004 : 6975545) + getFoliageColorAtPos
 *    (6975545); BiomeForest ROOFED grass ((i&16711422)+2634762 >> 1); BiomeMesa
 *    grass 9470285 / foliage 10387789.
 *  - BiomeColorHelper.getColorAtPos: the 3x3 area channel-average blend
 *    (cr_k18_blend3x3) over the column and its 8 neighbours.
 *
 * getFloatTemperature(pos): for y>64, subtracts
 *   (TEMPERATURE_NOISE(x/8,z/8)*4 + (y-64)) * 0.05/30
 * from base getTemperature() (Biome.java:258-268). TEMPERATURE_NOISE is
 * NoiseGeneratorPerlin(new Random(1234L), 1). Grass/foliage colormaps clamp the
 * result to [0,1]; swamp/mesa overrides ignore temperature. */
#include "assets/colormap_gen.h"

/* Per-biome rainfall (Biome.registerBiomes setRainfall; BiomeProperties default 0.5). */
static float cb_rainfall(int id) {
    switch (id) {
        case 1: case 16: case 129: return 0.4f;                 /* plains, beach, sunflower */
        case 2: case 8: case 17: case 35: case 36: case 37: case 38: case 39:
        case 130: case 163: case 164: case 165: case 166: case 167:
            return 0.0f;                                        /* deserts, hell, savannas, mesas */
        case 3: case 20: case 25: case 26: case 34: case 131: case 162:
            return 0.3f;                                        /* extreme hills, stone/cold beach */
        case 4: case 5: case 18: case 19: case 29: case 32: case 33:
        case 132: case 133: case 157: case 160: case 161:
            return 0.8f;                                        /* forest/taiga/redwood/roofed */
        case 6: case 21: case 22: case 134: case 149:
            return 0.9f;                                        /* swamp, jungle */
        case 23: case 151: return 0.8f;                         /* jungle edge */
        case 14: case 15: return 1.0f;                          /* mushroom island(/shore) */
        case 27: case 28: case 155: case 156: return 0.6f;      /* birch forest */
        case 30: case 31: case 158: return 0.4f;                /* cold taiga */
        default: return 0.5f;                                   /* ocean/river/ice/void/... */
    }
}

/* Per-biome water tint (Biome.registerBiomes setWaterColor; default 0xFFFFFF).
 * Only swampland/swampland-M set 14745518 (0xE0FFAE); the overworld blue comes
 * from the water texture itself, tinted by this multiplier (white = no tint). */
static int cb_water_color(int id) {
    return (id == 6 || id == 134) ? 14745518 : 16777215;
}

/* Biome.TEMPERATURE_NOISE = NoiseGeneratorPerlin(new Random(1234L), 1) (static).
 * Used by getFloatTemperature for y>64 grass/foliage (and ice/snow elsewhere). */
static const CpPerlin *temperature_noise(void) {
    static CpPerlin tn;
    static int inited = 0;
    if (!inited) {
        JavaRandom r;
        jrand_set(&r, 1234LL);
        tn.n = 1;
        cp_simplex_init(&tn.levels[0], &r);
        inited = 1;
    }
    return &tn;
}

/* Biome.getFloatTemperature(BlockPos) verbatim (Biome.java:258-268).
 * JVM: (float)x/8.0F promoted to double for Perlin; *4.0D cast to float. */
static float get_float_temperature(int biome, int wx, int wy, int wz) {
    float temp = cb_temperature(biome);
    if (wy > 64) {
        float f = (float)(cp_perlin_getValue(temperature_noise(),
                      (double)((float)wx / 8.0f),
                      (double)((float)wz / 8.0f)) * 4.0);
        return temp - (f + (float)wy - 64.0f) * 0.05f / 30.0f;
    }
    return temp;
}

float light_biome_temperature(
        const CrLight *l, int wx, int wy, int wz) {
    if (!l) return 0.5f;
    return get_float_temperature(light_biome(l, wx, wz), wx, wy, wz);
}

/* ColorizerGrass/ColorizerFoliage index math, verbatim (getGrassColor):
 *   humidity = humidity * temperature;
 *   i = (int)((1-temperature)*255); j = (int)((1-humidity)*255);
 *   return buffer[j<<8 | i];
 * temperature/humidity arrive already clamped to [0,1] (Biome.getGrass...AtPos). */
static int colormap_index(int biome, int wx, int wy, int wz, const unsigned int *buffer) {
    double temperature = (double)get_float_temperature(biome, wx, wy, wz);
    if (temperature < 0.0) temperature = 0.0;
    if (temperature > 1.0) temperature = 1.0;
    double humidity = cb_rainfall(biome);
    if (humidity < 0.0) humidity = 0.0;
    if (humidity > 1.0) humidity = 1.0;
    humidity = humidity * temperature;
    int i = (int)((1.0 - temperature) * 255.0);
    int j = (int)((1.0 - humidity) * 255.0);
    return (int)buffer[j << 8 | i];
}

/* GRASS_COLOR_NOISE = NoiseGeneratorPerlin(new Random(2345L), 1) (Biome static),
 * used only by BiomeSwamp.getGrassColorAtPos. Init once (single-threaded mesher). */
static const CpPerlin *swamp_grass_noise(void) {
    static CpPerlin gn;
    static int inited = 0;
    if (!inited) { cp_grass_noise_init(&gn); inited = 1; }
    return &gn;
}

/* MESA family (Biome.registerBiomes ids 37/38/39/165/166/167 -> BiomeMesa). */
static int is_mesa(int biome) {
    return biome == 37 || biome == 38 || biome == 39 ||
           biome == 165 || biome == 166 || biome == 167;
}

int cr_grass_color_biome(int biome, int wx, int wy, int wz) {
    if (biome == 6 || biome == 134) {                 /* BiomeSwamp override */
        double d0 = cp_perlin_getValue(swamp_grass_noise(),
                                       (double)wx * 0.0225, (double)wz * 0.0225);
        return d0 < -0.1 ? 5011004 : 6975545;
    }
    if (is_mesa(biome)) return 9470285;               /* BiomeMesa override */
    int i = colormap_index(biome, wx, wy, wz, CR_GRASS_COLORMAP);
    if (biome == 29 || biome == 157)                  /* BiomeForest ROOFED */
        return ((i & 16711422) + 2634762) >> 1;
    return i;
}
int cr_foliage_color_biome(int biome, int wx, int wy, int wz) {
    if (biome == 6 || biome == 134) return 6975545;   /* BiomeSwamp override */
    if (is_mesa(biome)) return 10387789;              /* BiomeMesa override */
    return colormap_index(biome, wx, wy, wz, CR_FOLIAGE_COLORMAP);
}
int cr_water_color_biome(int biome) {
    return cb_water_color(biome);
}

static int biome_at(const CrLight *L, int wx, int wz) {
    LChunk *c = find_chunk(L, wx >> 4, wz >> 4);
    if (!c) return -1;
    return c->biome[(wx & 15) + (wz & 15) * 16];
}

/* Public read-only biome id (voronoi full-res) at a world column; -1 if the chunk
 * is not loaded. Same value magma renders from (world_diff verifier hook). */
int light_biome(const CrLight *L, int wx, int wz) { return biome_at(L, wx, wz); }

/* Color resolvers: BiomeColorHelper keeps BlockPos.y fixed across the 3x3 (only
 * x/z step ±1); TEMPERATURE_NOISE therefore samples each neighbour's column. */
static int grass_fn(int b, int wx, int wy, int wz) {
    return cr_grass_color_biome(b, wx, wy, wz);
}
static int foliage_fn(int b, int wx, int wy, int wz) {
    return cr_foliage_color_biome(b, wx, wy, wz);
}
static int water_fn(int b, int wx, int wy, int wz) {
    (void)wx; (void)wy; (void)wz;
    return cr_water_color_biome(b);
}

/* Build the 9-colour 3x3 grid (clamping to the centre when a neighbour column is
 * unloaded) and run the exact BiomeColorHelper k18 blend. wy is the block y from
 * the query pos (Java MutableBlockPos keeps y while walking x/z). */
static int tint_blend(const CrLight *L, int wx, int wy, int wz,
                      int (*fn)(int, int, int, int)) {
    int cb = biome_at(L, wx, wz);
    int center = (cb < 0) ? fn(1, wx, wy, wz) : fn(cb, wx, wy, wz);
    int c[9];
    int n = 0;
    for (int dxx = -1; dxx <= 1; ++dxx)
        for (int dzz = -1; dzz <= 1; ++dzz) {
            int b = biome_at(L, wx + dxx, wz + dzz);
            c[n++] = (b < 0) ? center : fn(b, wx + dxx, wy, wz + dzz);
        }
    return cr_k18_blend3x3(c);
}

int light_grass_color(const CrLight *L, int wx, int wy, int wz) {
    return tint_blend(L, wx, wy, wz, grass_fn);
}
int light_foliage_color(const CrLight *L, int wx, int wy, int wz) {
    return tint_blend(L, wx, wy, wz, foliage_fn);
}
int light_water_color(const CrLight *L, int wx, int wz) {
    /* Water tint ignores temperature/elevation; pass y=0 (unused by water_fn). */
    return tint_blend(L, wx, 0, wz, water_fn);
}
