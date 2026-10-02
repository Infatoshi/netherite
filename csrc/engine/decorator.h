/* BiomeDecorator and the per-biome decorate overrides, the reference for the
 * decorator stage markers of the oracle's PopulateProbe. The table below is
 * read straight off the biome constructors in oracle/src/world/biome/. */
#ifndef NETHERITE_DECORATOR_H
#define NETHERITE_DECORATOR_H

struct populate;

/* BiomeDecorator.func_150512_a for one chunk: the per-biome decorate override
 * (none for most biomes), then func_150513_a over the biome's decorator
 * counts, with the choice helpers dispatched on the object the override
 * passes. Fires the POP_* stage hooks at the vanilla markers. */
void decorator_decorate(struct populate *p, int biome, int chunk_x, int chunk_z);

#endif