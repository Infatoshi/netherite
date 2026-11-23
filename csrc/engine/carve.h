/* MapGenBase carvers run by provideChunk after the surface pass. */
#ifndef NETHERITE_CARVE_H
#define NETHERITE_CARVE_H

#include <stdint.h>

/* MapGenCaves.func_151539_a over chunk (cx, cz). biomes are the chunk's 16x16
 * ids (index x + z*16); tops is each biome object's topBlock as the surface
 * pass left it (struct surface.top), which caves put back over exposed dirt. */
void caves_pass(int64_t seed, int cx, int cz, const int *biomes, const uint16_t *tops, uint16_t *ids);

/* MapGenRavine, same arguments; runs after caves. */
void ravines_pass(int64_t seed, int cx, int cz, const int *biomes, const uint16_t *tops, uint16_t *ids);

#endif
