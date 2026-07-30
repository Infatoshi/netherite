/* magma world: block appearance + classification table (internal to the world module).
 * Maps blaze CB_* block ids to a procedural-atlas tile index and a base colour,
 * and answers the two questions meshing needs: is this block solid, and does it
 * occlude the face of a neighbour (opaque)? Not part of the public contract. */
#ifndef MAGMA_WORLD_BLOCKS_H
#define MAGMA_WORLD_BLOCKS_H

#include "core/types.h"

/* Procedural atlas layout: 8 columns x 2 rows of 16x16 tiles => 128x32 texels. */
#define BLK_TILE       16
#define BLK_ATLAS_COLS 8
#define BLK_ATLAS_ROWS 2
#define BLK_ATLAS_W    (BLK_ATLAS_COLS * BLK_TILE)   /* 128 */
#define BLK_ATLAS_H    (BLK_ATLAS_ROWS * BLK_TILE)   /* 32  */

/* non-air */
int block_is_solid(int cb);
/* solid AND occludes the neighbouring face (air/water/ice/lily do NOT occlude) */
int block_is_opaque(int cb);

/* atlas tile index (0..BLK_ATLAS_COLS*BLK_ATLAS_ROWS-1) for a block id */
int block_tile(int cb);
/* base RGBA colour used to paint the tile */
CrRgba block_color(int cb);

/* Fill `dst` (BLK_ATLAS_W*BLK_ATLAS_H texels, row-major) with the procedural atlas.
 * Called once by the world; the storage is static and never freed. */
void block_build_atlas(CrRgba *dst);

#endif /* MAGMA_WORLD_BLOCKS_H */
