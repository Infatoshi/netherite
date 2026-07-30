/* magma world: seed -> blaze chunk voxels, cached, queried in world coords.
 * Implements world/world.h. Owns a McSinTable (built once) and a growable cache
 * of generated ChunkPrimer chunks keyed by (cx,cz). See world/mesh.c for meshing. */
#include "world/world.h"
#include "world/blocks.h"

#include <stdlib.h>
#include <string.h>

#include "chunk_provider.h"   /* McSinTable, CpScratch, ChunkPrimer, cp_provide_chunk, cb_get */

typedef struct {
    int          cx, cz;
    ChunkPrimer *primer;
} CachedChunk;

struct CrWorld {
    long long    seed;
    McSinTable  *sin;          /* ~256KB, built once, reused for every chunk */
    CpScratch   *scratch;      /* reused generation scratch */
    CachedChunk *chunks;
    int          nchunks;
    int          cap;
    int          atlas_ready;
};

/* Static, never-freed procedural atlas (shared by all worlds). */
static CrRgba g_atlas[BLK_ATLAS_W * BLK_ATLAS_H];
static int    g_atlas_built = 0;

/* Java-style floor division/modulo for mapping world coords to chunk + local. */
static int fdiv16(int a) { return mc_floor_div(a, 16); }
static int fmod16(int a) { return mc_floor_mod(a, 16); }

static CachedChunk *find_chunk(const CrWorld *w, int cx, int cz) {
    for (int i = 0; i < w->nchunks; ++i)
        if (w->chunks[i].cx == cx && w->chunks[i].cz == cz)
            return &w->chunks[i];
    return NULL;
}

CrWorld *world_create(long long seed) {
    CrWorld *w = (CrWorld *)calloc(1, sizeof(CrWorld));
    if (!w) return NULL;
    w->seed = seed;
    w->sin = (McSinTable *)malloc(sizeof(McSinTable));
    w->scratch = (CpScratch *)malloc(sizeof(CpScratch));
    if (!w->sin || !w->scratch) {
        free(w->sin); free(w->scratch); free(w);
        return NULL;
    }
    mc_sin_table_init(w->sin);
    w->chunks = NULL; w->nchunks = 0; w->cap = 0;

    if (!g_atlas_built) { block_build_atlas(g_atlas); g_atlas_built = 1; }
    w->atlas_ready = 1;
    return w;
}

void world_destroy(CrWorld *w) {
    if (!w) return;
    for (int i = 0; i < w->nchunks; ++i) free(w->chunks[i].primer);
    free(w->chunks);
    free(w->sin);
    free(w->scratch);
    free(w);
}

static void generate_chunk(CrWorld *w, int cx, int cz) {
    if (find_chunk(w, cx, cz)) return;   /* already cached */
    ChunkPrimer *primer = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    if (!primer) return;
    cp_provide_chunk(primer, w->scratch, w->sin, (i64)w->seed, cx, cz);

    if (w->nchunks == w->cap) {
        int ncap = w->cap ? w->cap * 2 : 16;
        CachedChunk *nc = (CachedChunk *)realloc(w->chunks, (size_t)ncap * sizeof(CachedChunk));
        if (!nc) { free(primer); return; }
        w->chunks = nc; w->cap = ncap;
    }
    w->chunks[w->nchunks].cx = cx;
    w->chunks[w->nchunks].cz = cz;
    w->chunks[w->nchunks].primer = primer;
    w->nchunks++;
}

void world_ensure(CrWorld *w, int ccx, int ccz, int radius) {
    if (!w || radius < 0) return;
    for (int cx = ccx - radius; cx <= ccx + radius; ++cx)
        for (int cz = ccz - radius; cz <= ccz + radius; ++cz)
            generate_chunk(w, cx, cz);
}

int world_block(const CrWorld *w, int wx, int wy, int wz) {
    if (!w || wy < 0 || wy > 255) return CB_AIR;
    CachedChunk *c = find_chunk(w, fdiv16(wx), fdiv16(wz));
    if (!c) return CB_AIR;
    return cb_get(c->primer, fmod16(wx), wy, fmod16(wz));
}

int world_surface_y(const CrWorld *w, int wx, int wz) {
    if (!w) return 64;
    CachedChunk *c = find_chunk(w, fdiv16(wx), fdiv16(wz));
    if (!c) return 64;
    int lx = fmod16(wx), lz = fmod16(wz);
    for (int y = 255; y >= 0; --y)
        if (cb_get(c->primer, lx, y, lz) != CB_AIR)
            return y + 1;
    return 0;
}

CrTexture world_atlas(const CrWorld *w) {
    (void)w;
    if (!g_atlas_built) { block_build_atlas(g_atlas); g_atlas_built = 1; }
    CrTexture t;
    t.w = BLK_ATLAS_W;
    t.h = BLK_ATLAS_H;
    t.texels = g_atlas;
    t.tile = BLK_TILE;
    return t;
}
