/* world/mesh_mc.h - MC-faithful chunk mesher contract.
 *
 * Composes the wave-3 modules into per-render-layer CrVertex triangle lists:
 *   - blocks + sky/block light + biome tint from world/light.h (CrLight)
 *   - per-face atlas sprite / tint class / render layer from assets/blockmodels.h
 *   - real stitched MC atlas (with mips) from bm_atlas()
 * Output feeds cr_transform + cr_raster_* directly (world-space CrVertex).
 */
#ifndef MAGMA_WORLD_MESH_MC_H
#define MAGMA_WORLD_MESH_MC_H
#include "core/types.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Per-layer mesh for one chunk. Arrays indexed by CrRenderLayer (0..3); each is a
 * flat triangle-list of CrVertex (count = nverts[l], multiple of 3), malloc'd. */
typedef struct {
    CrVertex *verts[4];
    int       nverts[4];
} CrChunkMeshMC;

typedef struct CrWorldMC CrWorldMC;

CrWorldMC *worldmc_create(long long seed);
CrWorldMC *worldmc_create_type(long long seed, int world_type);
void       worldmc_destroy(CrWorldMC *w);

/* Generate + light all chunks within Chebyshev radius of (ccx,ccz). */
void worldmc_ensure(CrWorldMC *w, int ccx, int ccz, int radius);

/* Build the per-layer mesh for chunk (ccx,ccz). Neighbours must be ensured
 * (radius >= 1) for correct face culling. Returns total vertex count across
 * layers; fills *out (caller releases with worldmc_free_mesh). Faces are emitted
 * only when the neighbour block is non-opaque; uv from bm_sprite_uv; per-vertex
 * light = combined sky/block light normalized to [0,1]; ao from neighbour
 * occupancy; tint = white unless the face's BM tint class applies (then the
 * corresponding light_*_color, as CrRgba). Winding: CCW when viewed from outside
 * the block (front-facing under the raster's CR_FRONT_SIGN convention). */
int  worldmc_mesh_chunk(CrWorldMC *w, int ccx, int ccz, CrChunkMeshMC *out);
void worldmc_free_mesh(CrChunkMeshMC *m);

/* Verifier ablation: suppress only fire quads while preserving the real fire
 * block and its propagated light in CrLight. Production default is enabled. */
void worldmc_set_fire_mesh_enabled(int enabled);

/* ALLOCATE-ONCE variant: mesh into caller-provided fixed per-layer slabs. out->verts[l]
 * must point at cap[l] pre-allocated CrVertex; out->nverts[l] is reset and filled. No
 * realloc/malloc/free - a chunk whose layer exceeds cap[l] aborts with a clear message
 * (a loud cap-too-small beats silent corruption). Returns total vertex count. */
int  worldmc_mesh_chunk_into(CrWorldMC *w, int ccx, int ccz, CrChunkMeshMC *out,
                             const int cap[4]);

/* The atlas to bind as CrShadeCtx.atlas (== bm_atlas()). */
CrTexture worldmc_atlas(const CrWorldMC *w);

/* Shade-time lightmap mode (game binaries opt in; default 0 = legacy noon
 * fold, which is what every golden harness verifies). When on, overworld
 * meshes carry lightmap COORDS in CrVertex.light/.blk (sky/blk levels, face
 * shade folded into .ao) and the renderer must set CrShadeCtx.lightmap to the
 * frame's 16x16 lightmap texture. Non-overworld dims keep the legacy fold. */
void worldmc_set_lightmap_mode(int on);
int  worldmc_lightmap_mode(void);

#ifdef __cplusplus
}
#endif
#endif /* MAGMA_WORLD_MESH_MC_H */
