/* groundmap (lane/pground): the hunting grounds round a place, from the world generator alone (no play, no oracle).
 *
 *   out/native/groundmap SEED X Z [R [STEP]]  > FILE.tsv          (R 1500, STEP 32: about 20 s, 0.5 ms a chunk)
 *
 * Every chunk within R + 128 blocks of (X, Z) is generated raw (chunkgen_raw: density, biomes, surface, caves and
 * ravines; no population, so no trees, lakes, structures or ores) and counted by the cells a mob 3 high can spawn on
 * as SpawnerAnimals tests a place (the cell and the two over it air, the cell under it an opaque solid block, not a
 * liquid): under the sky (the cell over its column's top block, which is not water, lava or ice) and under ground
 * (any other: caves and ravines), with the chunk's draw range (getTopFilledSegment + 16: SpawnerAnimals draws one y in
 * it a chunk and creature type each tick and walks its pack at that y). A mushroom island column has no hostile spawn
 * list and counts nothing. Then on a STEP grid anchored at (X, Z) within R, a place's spawn area (the 15 x 15 chunks
 * round its chunk, findChunksForSpawning's eligible ones for one player): sky and cave as the summed cells over the
 * draw range (a rate per tick, in draws that land on a cell), the sky share of the two, and within 64 blocks (the 9 x 9
 * chunks) the water share of the columns and the land's top height's mean and sd. Rows:
 *   C cx cz biome sky_cells cave_cells water_cols top_sum top_sq land_cols draw_range        (every chunk)
 *   P x z dist biome name sky cave share water64 top64 sd64                                    (every place)
 * Endermen are a tenth of a hostile pack's weight in every overworld biome but the mushroom island's (1.7.10), so the
 * share is theirs as much as any hostile's; how many exist is the mob cap's (70 for one player), not the ground's. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../csrc/engine/biomes.h"
#include "../csrc/engine/chunkgen.h"
#include "../csrc/engine/terrain.h"

struct cc {
    int biome;
    int32_t sky, cave, water, land, range;
    double top_sum, top_sq;
};

static int64_t g_seed;
static int g_x0, g_z0, g_n, g_ccx0, g_ccz0;
static struct cc *g_c;

static int solid(int b) { return b != 0 && b != 8 && b != 9 && b != 10 && b != 11 && b != 79 && b != 78 && b != 31; }

static void count_chunk(struct chunkgen *g, int cx, int cz, uint16_t *ids, uint8_t *metas, struct cc *o)
{
    int biomes[256];
    chunkgen_raw(g, cx, cz, ids, metas, biomes);
    memset(o, 0, sizeof *o);
    o->biome = biomes[8 << 4 | 8];
    int hi = 0;
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z)
        {
            const uint16_t *col = ids + (x << 12 | z << 8);
            int top = 255;
            while (top > 0 && (col[top] & 4095) == 0) --top;
            if (top > hi) hi = top;
            int tb = col[top] & 4095, bio = biomes[z << 4 | x];
            if (tb == 8 || tb == 9 || tb == 79) ++o->water;
            else ++o->land, o->top_sum += top + 1, o->top_sq += (double)(top + 1) * (top + 1);
            if (bio == 14 || bio == 15) continue;   /* the mushroom island: no hostile spawns */
            if (solid(tb) && tb != 10 && tb != 11 && top + 3 < 256) ++o->sky;
            for (int y = 1; y + 2 < top; ++y)
                if ((col[y] & 4095) == 0 && (col[y + 1] & 4095) == 0 && (col[y + 2] & 4095) == 0 && solid(col[y - 1] & 4095))
                    ++o->cave;
        }
    o->range = (hi >> 4) * 16 + 16;
}


static const struct cc *at(int cx, int cz)
{
    int i = cx - g_ccx0, j = cz - g_ccz0;
    if (i < 0 || j < 0 || i >= g_n || j >= g_n) return NULL;
    return &g_c[j * g_n + i];
}

int main(int argc, char **argv)
{
    if (argc < 4)
        return fprintf(stderr, "usage: groundmap SEED X Z [R [STEP]]\n"), 2;
    g_seed = strtoll(argv[1], NULL, 10), g_x0 = atoi(argv[2]), g_z0 = atoi(argv[3]);
    int r = argc > 4 ? atoi(argv[4]) : 1500, step = argc > 5 ? atoi(argv[5]) : 32;
    if (r == 0)
    {
        /* R 0: the columns of the 5 x 5 round (X, Z), raw (no trees): T x z top_y top_block (a dev tp's height) */
        struct chunkgen *g = malloc(sizeof *g);
        uint16_t *ids = malloc(CHUNK_CELLS * sizeof *ids);
        uint8_t *metas = malloc(CHUNK_CELLS);
        int biomes[256];
        chunkgen_init(g, g_seed);
        for (int dz = -2; dz <= 2; ++dz)
            for (int dx = -2; dx <= 2; ++dx)
            {
                int x = g_x0 + dx, z = g_z0 + dz;
                chunkgen_raw(g, x >> 4, z >> 4, ids, metas, biomes);
                const uint16_t *col = ids + ((x & 15) << 12 | (z & 15) << 8);
                int top = 255;
                while (top > 0 && (col[top] & 4095) == 0) --top;
                printf("T\t%d\t%d\t%d\t%d\n", x, z, top, col[top] & 4095);
            }
        free(g), free(ids), free(metas);
        return 0;
    }
    if (r < 0 || step <= 0) return fprintf(stderr, "groundmap: bad R or STEP\n"), 2;
    int rc = (r + 128) / 16 + 1;
    g_ccx0 = (g_x0 >> 4) - rc, g_ccz0 = (g_z0 >> 4) - rc, g_n = 2 * rc + 1;
    g_c = calloc((size_t)g_n * g_n, sizeof *g_c);
    {
        /* one generator, one thread: chunkgen_raw takes its scratch from the one env's stack */
        struct chunkgen *g = malloc(sizeof *g);
        uint16_t *ids = malloc(CHUNK_CELLS * sizeof *ids);
        uint8_t *metas = malloc(CHUNK_CELLS);
        chunkgen_init(g, g_seed);
        for (int k = 0; k < g_n * g_n; ++k) count_chunk(g, g_ccx0 + k % g_n, g_ccz0 + k / g_n, ids, metas, &g_c[k]);
        free(g), free(ids), free(metas);
    }
    printf("# groundmap seed %lld at %d %d, R %d, step %d: C cx cz biome sky cave water top_sum top_sq land range; "
           "P x z dist biome name sky cave share water64 top64 sd64\n", (long long)g_seed, g_x0, g_z0, r, step);
    for (int k = 0; k < g_n * g_n; ++k)
    {
        const struct cc *c = &g_c[k];
        printf("C\t%d\t%d\t%d\t%d\t%d\t%d\t%.0f\t%.0f\t%d\t%d\n", g_ccx0 + k % g_n, g_ccz0 + k / g_n, c->biome, c->sky,
               c->cave, c->water, c->top_sum, c->top_sq, c->land, c->range);
    }
    struct chunkgen *g = malloc(sizeof *g);
    chunkgen_init(g, g_seed);
    for (int dz = -r; dz <= r; dz += step)
        for (int dx = -r; dx <= r; dx += step)
        {
            double d = sqrt((double)dx * dx + (double)dz * dz);
            if (d > r) continue;
            int x = g_x0 + dx, z = g_z0 + dz, pcx = x >> 4, pcz = z >> 4;
            double sky = 0, cave = 0, wat = 0, land = 0, ts = 0, tq = 0;
            for (int j = -7; j <= 7; ++j)
                for (int i = -7; i <= 7; ++i)
                {
                    const struct cc *c = at(pcx + i, pcz + j);
                    if (c == NULL || c->range <= 0) continue;
                    sky += (double)c->sky / c->range, cave += (double)c->cave / c->range;
                    if (i >= -4 && i <= 4 && j >= -4 && j <= 4)
                        wat += c->water, land += c->land, ts += c->top_sum, tq += c->top_sq;
                }
            int bio;
            biomes_full(&g->terrain.layers, x, z, 1, 1, &bio);
            double m = land > 0 ? ts / land : -1, sd = land > 0 ? sqrt(fmax(0.0, tq / land - m * m)) : -1;
            printf("P\t%d\t%d\t%.0f\t%d\t%s\t%.2f\t%.2f\t%.3f\t%.3f\t%.1f\t%.1f\n", x, z, d, bio,
                   bio >= 0 && bio < 256 && BIOMES[bio].name ? BIOMES[bio].name : "?", sky, cave,
                   sky + cave > 0 ? sky / (sky + cave) : 0.0, wat + land > 0 ? wat / (wat + land) : 0.0, m, sd);
        }
    free(g);
    free(g_c);
    return 0;
}
