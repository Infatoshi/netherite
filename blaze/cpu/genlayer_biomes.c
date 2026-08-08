/* CPU reference: MC 1.11.2 GenLayer biome stack. Builds the DEFAULT overworld biomeIndexLayer
 * (full-resolution VoronoiZoom) and dumps the 16x16 region at (0,0) row-major: 256 biome ids,
 * each as raw 32-bit hex (%08x), one per line. Matches cuda/genlayer_biomes.cu and the golden. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/genlayer_biomes.h"

int main(int argc, char **argv) {
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    GLNode nodes[GL_MAX_NODES];
    int biomeIndex;
    gl_build(nodes, seed, &biomeIndex);
    GlArena *arena = (GlArena *)malloc(sizeof(GlArena));
    arena->off = 0;
    int *out = gl_getInts(nodes, arena, biomeIndex, 0, 0, 16, 16);
    for (int i = 0; i < 16 * 16; ++i)
        printf("%08x\n", (unsigned)out[i]);
    free(arena);
    return 0;
}
