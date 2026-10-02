/* MapGenMineshaft: the spawn test and StructureMineshaftStart, one piece of the
 * shared structure layer (structure.h). Registered in structure_types. */
#ifndef NETHERITE_MINESHAFT_H
#define NETHERITE_MINESHAFT_H

#include "structure.h"
#include "structure_blocks.h"

extern const struct structure_type structure_mineshaft;

/* The four shaft pieces' buildComponent, dispatched from structure.c. */
void mineshaft_build_component(struct piece *p, struct piece *parent, struct start *s, jrand *rand);

/* The four pieces' addComponentParts, the block half; the piece_fn a
 * sc_generate step runs. See mineshaft_blocks.c. */
int mineshaft_blocks(struct sc_ctx *c, struct piece *p);

#endif