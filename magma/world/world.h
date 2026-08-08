/* magma world contract: seed -> chunk voxels (via blaze) -> visible-face mesh.
 * Shared by the world .c files (impl) and app/main.c (consumer). Do not change signatures
 * without updating SPEC.md. Block ids are blaze CB_* small ints (0 = CB_AIR). */
#ifndef MAGMA_WORLD_H
#define MAGMA_WORLD_H
#include "core/types.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct CrWorld CrWorld;

/* Create a world for `seed`. Generates nothing yet. */
CrWorld *world_create(long long seed);
void     world_destroy(CrWorld *w);

/* Ensure every chunk within Chebyshev `radius` of chunk (ccx,ccz) is generated
 * and cached. Safe to call repeatedly; already-generated chunks are skipped. */
void world_ensure(CrWorld *w, int ccx, int ccz, int radius);

/* CB_* block id at WORLD coords. Returns 0 (air) if the chunk is not generated
 * or y is outside [0,255]. Cheap for already-generated chunks. */
int world_block(const CrWorld *w, int wx, int wy, int wz);

/* Highest non-air block y at world column (wx,wz), + 1 (a stand-on y). Returns a
 * sensible default (e.g. 64) if the column's chunk is not generated. */
int world_surface_y(const CrWorld *w, int wx, int wz);

/* Procedural block atlas (owned by the world; do not free). tile = tile size. */
CrTexture world_atlas(const CrWorld *w);

/* Build the visible-face mesh for chunk (ccx,ccz) into `out` (world-space
 * CrVertex, triangle list, count is a multiple of 3). Emits a face only when the
 * neighbouring block is air/transparent (uses adjacent chunks, so world_ensure a
 * radius >= 1 around (ccx,ccz) first). Per-face directional shade + neighbour AO
 * folded into CrVertex.light/ao; grass/foliage get a biome-ish tint; uv maps each
 * face to its block's atlas tile. Returns vertex count written (<= max). */
int world_mesh_chunk(CrWorld *w, int ccx, int ccz, CrVertex *out, int max);

#ifdef __cplusplus
}
#endif
#endif
