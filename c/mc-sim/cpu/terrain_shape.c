/* CPU reference: ChunkProviderOverworld.generateHeightmap for chunk (0,0) at a given world seed.
 * Emits the 825 doubles of heightMap (5x33x5) as raw IEEE754 bits, one per line, for bitwise diff. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/terrain_shape.h"

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    int chunkX = 0, chunkZ = 0;

    TerrainNoise t;
    terrain_noise_init(&t, seed);

    double heightMap[825];
    terrain_generate_heightmap(&t, heightMap, chunkX * 4, 0, chunkZ * 4);

    for (int i = 0; i < 825; i++) {
        u64 bits; memcpy(&bits, &heightMap[i], 8);
        printf("%016llx\n", (unsigned long long)bits);
    }
    return 0;
}
