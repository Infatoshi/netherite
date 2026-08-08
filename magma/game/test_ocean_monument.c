#include "game/ocean_monument_live.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SIDE = 96, HEIGHT = 256 };

typedef struct {
    int min_x, min_z;
    unsigned short *blocks;
    double elders[3][3];
    int elder_count;
} MemoryWorld;

static size_t index_of(const MemoryWorld *world, int x, int y, int z) {
    return ((size_t)(x - world->min_x) * SIDE
        + (size_t)(z - world->min_z)) * HEIGHT + (size_t)y;
}

static unsigned short baseline(int y) {
    if (y < 32) return (unsigned short)(1 << 4);
    if (y < 64) return (unsigned short)(9 << 4);
    return 0;
}

static unsigned short get_block(void *opaque, int x, int y, int z) {
    MemoryWorld *world = (MemoryWorld *)opaque;
    if (y < 0 || y >= HEIGHT || x < world->min_x
            || x >= world->min_x + SIDE || z < world->min_z
            || z >= world->min_z + SIDE) {
        fprintf(stderr, "monument access out of bounds: %d %d %d\n", x, y, z);
        abort();
    }
    return world->blocks[index_of(world, x, y, z)];
}

static void set_block(
        void *opaque, int x, int y, int z, unsigned short state) {
    MemoryWorld *world = (MemoryWorld *)opaque;
    if (y < 0 || y >= HEIGHT || x < world->min_x
            || x >= world->min_x + SIDE || z < world->min_z
            || z >= world->min_z + SIDE) {
        fprintf(stderr, "monument write out of bounds: %d %d %d\n", x, y, z);
        abort();
    }
    world->blocks[index_of(world, x, y, z)] = state;
}

static void spawn_elder(void *opaque, double x, double y, double z) {
    MemoryWorld *world = (MemoryWorld *)opaque;
    if (world->elder_count >= 3) {
        fprintf(stderr, "too many monument elders\n");
        abort();
    }
    world->elders[world->elder_count][0] = x;
    world->elders[world->elder_count][1] = y;
    world->elders[world->elder_count][2] = z;
    ++world->elder_count;
}

static uint64_t add_hash(uint64_t hash, int value) {
    hash ^= (unsigned)value & 255U; hash *= UINT64_C(0x100000001b3);
    hash ^= (unsigned)value >> 8 & 255U; hash *= UINT64_C(0x100000001b3);
    return hash;
}

static int compare_elder(const void *left, const void *right) {
    const double *a = (const double *)left, *b = (const double *)right;
    for (int i = 0; i < 3; ++i) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }
    return 0;
}

static uint64_t double_bits(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static void run(long long seed, int chunk_x, int chunk_z) {
    int origin_x = chunk_x * 16 + 8 - 29;
    int origin_z = chunk_z * 16 + 8 - 29;
    MemoryWorld world;
    memset(&world, 0, sizeof world);
    world.min_x = origin_x - 16;
    world.min_z = origin_z - 16;
    world.blocks = (unsigned short *)malloc(
        (size_t)SIDE * SIDE * HEIGHT * sizeof *world.blocks);
    if (!world.blocks) abort();
    for (int x = 0; x < SIDE; ++x)
        for (int z = 0; z < SIDE; ++z)
            for (int y = 0; y < HEIGHT; ++y)
                world.blocks[((size_t)x * SIDE + z) * HEIGHT + y] = baseline(y);
    GmMonumentAccess access = {
        &world, get_block, set_block, spawn_elder, 63
    };
    int facing = gm_monument_generate(seed, chunk_x, chunk_z, &access);
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    int rough = 0, bricks = 0, dark = 0, lantern = 0;
    int gold = 0, sponge = 0, water = 0, stone = 0;
    for (int y = 2; y <= 64; ++y)
        for (int z = origin_z - 5; z <= origin_z + 62; ++z)
            for (int x = origin_x - 5; x <= origin_x + 62; ++x) {
                int raw = get_block(&world, x, y, z);
                int id = raw >> 4, meta = raw & 15;
                if (getenv("MONUMENT_VERBOSE") && raw != baseline(y))
                    printf("B %d %d %d %04x\n",
                        x - origin_x, y, z - origin_z, raw);
                hash = add_hash(hash, raw);
                if (id == 168 && meta == 0) ++rough;
                else if (id == 168 && meta == 1) ++bricks;
                else if (id == 168 && meta == 2) ++dark;
                else if (id == 169) ++lantern;
                else if (id == 41) ++gold;
                else if (id == 19 && meta == 1) ++sponge;
                else if (id == 9) ++water;
                else if (id == 1) ++stone;
            }
    qsort(world.elders, (size_t)world.elder_count,
          sizeof world.elders[0], compare_elder);
    printf("M %lld %d %d %d %016llx %d %d %d %d %d %d %d %d %d\n",
        seed, chunk_x, chunk_z, facing, (unsigned long long)hash,
        rough, bricks, dark, lantern, gold, sponge, water, stone,
        world.elder_count);
    for (int i = 0; i < world.elder_count; ++i)
        printf("E %016llx %016llx %016llx\n",
            (unsigned long long)double_bits(world.elders[i][0]),
            (unsigned long long)double_bits(world.elders[i][1]),
            (unsigned long long)double_bits(world.elders[i][2]));
    free(world.blocks);
}

static void candidate(long long seed, int region_x, int region_z) {
    int chunk_x, chunk_z;
    gm_monument_candidate_for_region(
        seed, region_x, region_z, &chunk_x, &chunk_z);
    printf("C %lld %d %d %d %d\n",
        seed, region_x, region_z, chunk_x, chunk_z);
}

int main(void) {
    run(0, 0, 0);
    if (getenv("MONUMENT_ONE")) return 0;
    run(1, 3, -7);
    run(123456789, -12, 19);
    run(-99887766, 33, -41);
    run(0x5eed5eedLL, -64, -64);
    candidate(0, 0, 0);
    candidate(1, 3, -7);
    candidate(123456789, -12, 19);
    candidate(-99887766, 33, -41);
    candidate(0x5eed5eedLL, -64, -64);
    return 0;
}
