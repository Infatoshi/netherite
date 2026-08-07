/* world/light.h - LIGHT module public contract for magma.
 *
 * Computes Minecraft-1.11.2-faithful sky light, block light, and biome tint
 * colors over the blaze voxel world so the mesher can light/tint vertices.
 *
 * Blocks are read from blaze (core/chunk_provider.h : cp_provide_chunk) and are
 * the unified CB_* small-int ids (CB_AIR=0, CB_STONE=1, ... CB_LAVA=11, ...).
 * Light per block is a 0..15 nibble; tint is 0xRRGGBB.
 *
 * Reference algorithms (render-opt kernels), reproduced here and bit-verified
 * against their goldens by tests/test_light.c:
 *   k17_skylight_gen      Chunk.generateSkylightMap (top-down attenuated ladder)
 *   k16_light_propagation World.checkLightFor BFS / getRawLight semantics
 *   k14_light_query       World.getLightFromNeighborsFor
 *   k15_light_combine_pack World.getCombinedLight tail packing
 *   k18_biome_color_blend BiomeColorHelper.getColorAtPos (3x3 channel-avg blend)
 */
#ifndef MAGMA_WORLD_LIGHT_H
#define MAGMA_WORLD_LIGHT_H

#include <stdint.h>
#include "core/types.h"
#include "world/lightmap.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CrLight CrLight; /* opaque light state over a set of chunks */

CrLight *light_create(long long seed);
/* world_type: 0 default Overworld, 1 vanilla-default superflat RL arena. */
CrLight *light_create_type(long long seed, int world_type);
void     light_destroy(CrLight *);

/* Generate + light every chunk in the square [ccx-radius..ccx+radius] x
 * [ccz-radius..ccz+radius] (chunk coords). Idempotent per chunk; re-runs the
 * global block-light BFS over all loaded chunks each call. */
void     light_ensure(CrLight *, int ccx, int ccz, int radius);
/* Monotonic count of chunks generated (population writes spill into neighbour
 * chunks without going through set_block; refill memos fold this in). */
long long light_gen_events(const CrLight *);

int      light_block(const CrLight *, int wx, int wy, int wz); /* CB/PB id, 0 air / unloaded */
/* Canonical packed vanilla 1.11.2 state used by gameplay. This is deliberately
 * separate from light_block(), whose compact value is only a renderer model key. */
uint16_t light_state(const CrLight *, int wx, int wy, int wz);
int      light_biome(const CrLight *, int wx, int wz);         /* voronoi biome id, -1 unloaded */

/* DEBUG/CAPS: number of chunks currently loaded (LChunk store size). */
int      light_loaded_chunks(const CrLight *);

/* TEST HOOK: force a block id at a world cell of an already-ensured chunk (does
 * not recompute light). Used by tests/test_mesh_models.c to build a synthetic
 * chunk with each non-cube model. No-op if the chunk is not loaded. */
void     light_debug_set_block(CrLight *, int wx, int wy, int wz, int id);
/* Same, plus legacy meta nibble (0..15). Worldgen leaves meta 0. Live dig/place/
 * interact composition uses this so doors/orientation survive fill_window. */
void     light_debug_set_block_meta(CrLight *, int wx, int wy, int wz, int id, int meta);
/* Gameplay mutation. Writes canonical state and derives a non-colliding renderer
 * model key. Unsupported visuals use an explicit fallback without changing state. */
void     light_set_state(CrLight *, int wx, int wy, int wz, uint16_t state);
/* Bulk snapshot mutation. Unlike a single gameplay edit, a block-only saved
 * chunk patch replaces whole columns without carrying vanilla's saved light
 * nibble arrays. The next light_ensure rebuilds Chunk.generateSkylightMap for
 * all resident chunks once, then runs the normal horizontal spread. */
void     light_load_state(CrLight *, int wx, int wy, int wz, uint16_t state);
/* Exact cold restore of vanilla Chunk SkyLight storage. The caller must first
 * finish its block snapshot and light_ensure once, then write every carried
 * nibble and call light_finalize_sky_snapshot before the first replay tick. */
int      light_load_sky_snapshot(CrLight *, int wx, int wy, int wz, int value);
void     light_finalize_sky_snapshot(CrLight *);
/* Apply the saved-skylight part of vanilla's deferred Chunk.recheckGaps after
 * a surface block is removed. The caller remeshes the affected chunk. */
void     light_recheck_break_surfaces(CrLight *, int wx, int wy, int wz);
/* Meta nibble at world cell (0 if unloaded). */
int      light_meta(const CrLight *, int wx, int wy, int wz);
int      light_sky  (const CrLight *, int wx, int wy, int wz); /* 0..15 sky light   */
int      light_blk  (const CrLight *, int wx, int wy, int wz); /* 0..15 block light */

/* Select the renderer's dimension and recompute light storage accordingly.
 * 0 = overworld (skylight), -1 = Nether (no skylight + Hell brightness table),
 * 1 = End (no skylight + EntityRenderer's explicit End RGB lightmap override).
 * Ambient belongs to the lightmap transfer, never the stored block-light nibble.
 * torch_flicker_x and gamma are the exact EntityRenderer inputs captured from MC. */
void     light_set_render_state(CrLight *, int dimension,
                                float torch_flicker_x, float gamma);
int      light_dimension(const CrLight *);
CrLightmapRgb light_lightmap_rgb(const CrLight *, int sky, int block);
CrRgba       light_lightmap_rgba8(const CrLight *, int sky, int block);

/* Backward-compatible no-sky relight entry point. It now selects a faithful
 * Nether state and does NOT inject the old fake block-light floor. */
void     light_zero_sky_and_relight(CrLight *);

/* Biome grass/foliage/water tint at a world column, as 0xRRGGBB, produced by the
 * k18-style 3x3 channel-average blend over the column and its 8 neighbours.
 * Grass/foliage take wy so Biome.getFloatTemperature(pos) can apply the y>64
 * elevation + TEMPERATURE_NOISE adjustment (Java BlockPos y). Water ignores y. */
int      light_grass_color  (const CrLight *, int wx, int wy, int wz);
int      light_foliage_color(const CrLight *, int wx, int wy, int wz);
int      light_water_color  (const CrLight *, int wx, int wz);
/* Biome.getFloatTemperature at an exact block position, including the
 * y>64 TEMPERATURE_NOISE/elevation adjustment used by weather ice and snow. */
float    light_biome_temperature(const CrLight *, int wx, int wy, int wz);

/* Per-biome (pre-blend) tint colours, 0xRRGGBB, exactly as Minecraft's Biome
 * subclasses compute them: getFloatTemperature(pos)+rainfall -> grass/foliage
 * colormap plus swamp/roofed-forest/mesa overrides and per-biome waterColor.
 * Exposed so tests/test_biome_color.c can bit-verify them against a verbatim-Java
 * golden. (wx,wy,wz) feed TEMPERATURE_NOISE / elevation and BiomeSwamp grass
 * dither; pass wy<=64 for base-temperature goldens. */
int  cr_grass_color_biome  (int biome, int wx, int wy, int wz);
int  cr_foliage_color_biome(int biome, int wx, int wy, int wz);
int  cr_water_color_biome  (int biome);

/* ---- pure reference kernels (exposed so tests/test_light.c can bit-verify
 * them against the render-opt goldens; used internally by the world code). ---- */

/* k17: Chunk.generateSkylightMap for one column. topSeg = topFilledSegment
 * (nY = topSeg+16); op[]/nn[] length nY. Writes sky[0..nY-1], returns heightMap. */
int  cr_k17_skylight_column(int topSeg, int hasSky,
                            const int *op, const int *nn, int *sky);
/* k18: 3x3 channel-average blend of nine 0xRRGGBB colours. */
int  cr_k18_blend3x3(const int c[9]);
/* k14: World.getLightFromNeighborsFor tail. */
int  cr_k14_light_query(int nb, int up, int east, int west,
                        int south, int north, int own);
/* k15: World.getCombinedLight tail packing. */
int  cr_k15_combine(int sky, int block, int override_val);

#ifdef __cplusplus
}
#endif
#endif /* MAGMA_WORLD_LIGHT_H */
