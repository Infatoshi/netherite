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

/* Every nether bridge piece's buildComponent, dispatched from structure.c. */
void fortress_build_component(struct piece *p, struct piece *parent, struct start *s, jrand *rand);

/* Every piece's addComponentParts, the block half; the piece_fn a sc_generate
 * step runs. See fortress_blocks.c. */
int fortress_blocks(struct sc_ctx *c, struct piece *p);

#endif