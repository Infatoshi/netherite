/* A vanilla Anvil region file, read only: RegionFile.getChunkDataInputStream's
 * 4 KiB sector table and the zlib chunk stream, then
 * AnvilChunkLoader.readChunkFromNBT's Level compound (Sections, HeightMap,
 * Biomes, Entities, TileEntities, TileTicks, the scalar flags) as parsed
 * canonical-NBT trees.
 *
 * The checkpoint lane's --from start reads these instead of rebuilding the
 * world from the seed: a chunk the player changed before the save must come
 * back as it was saved, not as the generator makes it. */
#ifndef NETHERITE_REGION_H
#define NETHERITE_REGION_H

#include "nbtjson.h"

/* DIR/region/r.RX.RZ.mca for chunk (cx, cz); returns the Level compound (an
 * owned tree) or NULL when the file has no entry for the chunk. */
nbt *region_load_chunk(const char *save_dir, int cx, int cz);

/* The chunk's scalar state as readChunkFromNBT applies it: 1 when the Level
 * compound holds TerrainPopulated 1, and LastUpdate for saveChunk's
 * needsSaving path. */
int region_level_terrain_populated(const nbt *level);

/* Enumerate every chunk present in DIR/region: for each r.RX.RZ.mca file, the
 * nonzero offset-table slots become (cx, cz) pairs appended to OUT (which
 * grows as needed, *out_n is the count). Returns 0 on success. */
int region_enumerate(const char *save_dir, int **out, int *out_n);

#endif
