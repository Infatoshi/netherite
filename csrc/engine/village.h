/* MapGenVillage and StructureVillagePieces. See village.c for the port and the
 * Java's draw order. The biome test on the spawn path and on every piece reads
 * the world's WorldChunkManager, so village_begin() is needed like the
 * temple's. */
#ifndef NETHERITE_VILLAGE_H
#define NETHERITE_VILLAGE_H

#include "structure.h"

#include "structure_blocks.h"

extern const struct structure_type structure_village;

/* The well's and the roads' buildComponent, dispatched from structure.c; every
 * other village piece uses StructureComponent's empty default. */
void village_build_component(struct piece *p, struct piece *parent, struct start *s, jrand *rand);

/* The pieces' block half (addComponentParts), the dispatch sc_ctx.piece_fn
 * points at for the Village. */
int village_blocks(struct sc_ctx *c, struct piece *p);

#endif