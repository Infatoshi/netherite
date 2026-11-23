/* MapGenNetherBridge and StructureNetherBridgePieces: the spawn test (one
 * fortress per 16x16 chunk region, at a chunk drawn from the region) and the
 * start's tree of pieces, one type of the shared structure layer
 * (structure.h). Registered in structure_types. */
#ifndef NETHERITE_FORTRESS_H
#define NETHERITE_FORTRESS_H

#include <stdint.h>

#include "structure.h"
#include "structure_blocks.h"

extern const struct structure_type structure_fortress;

/* The nearest candidate start chunk in radius 16x16-chunk regions, from the vanilla seed test without worldgen. */
int fortress_locate_start(int64_t seed, int bx, int bz, int radius, int *chunk_x, int *chunk_z);
/* The closest generated throne within range blocks of an arrival, by horizontal distance to the throne's centre.
 * This search generates structure boxes only; it neither populates nor reads world chunks. */
int fortress_locate_throne(int64_t seed, int bx, int bz, int range, int target[2], int start_chunk[2]);
/* Closest enclosed corridor or crossing's standing cell, chosen from generated piece boxes without loading chunks. */
int fortress_locate_entry(int64_t seed, int bx, int by, int bz, int range, int target[3], int *kind);

/* Every nether bridge piece's buildComponent, dispatched from structure.c. */
void fortress_build_component(struct piece *p, struct piece *parent, struct start *s, jrand *rand);

/* Every piece's addComponentParts, the block half; the piece_fn a sc_generate
 * step runs. See fortress_blocks.c. */
int fortress_blocks(struct sc_ctx *c, struct piece *p);

#endif
