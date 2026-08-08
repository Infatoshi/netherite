#include "scattered_desert.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SX 21
#define SY 256
#define SZ 21

typedef struct {
    u16 state[SX * SY * SZ];
    unsigned char written[SX * SY * SZ];
} TestWorld;

static int index_at(int x, int y, int z) {
    return (y * SZ + z) * SX + x;
}

static u16 world_get(void *ctx, int x, int y, int z) {
    TestWorld *world = (TestWorld *)ctx;
    if (x < 0 || x >= SX || y < 0 || y >= SY || z < 0 || z >= SZ)
        return y < 64 ? sd_state(1, 0) : sd_state(0, 0);
    int index = index_at(x, y, z);
    return world->written[index] ? world->state[index]
        : y < 64 ? sd_state(1, 0) : sd_state(0, 0);
}

static void world_set(void *ctx, int x, int y, int z, u16 state) {
    TestWorld *world = (TestWorld *)ctx;
    if (x < 0 || x >= SX || y < 0 || y >= SY || z < 0 || z >= SZ)
        return;
    int index = index_at(x, y, z);
    world->written[index] = 1;
    world->state[index] = state;
}

static int chest_compare(const void *left, const void *right) {
    const SdChest *a = (const SdChest *)left;
    const SdChest *b = (const SdChest *)right;
    if (a->z != b->z) return a->z - b->z;
    if (a->x != b->x) return a->x - b->x;
    return a->y - b->y;
}

static void run(long long seed) {
    static const int facing[4] = {SD_NORTH, SD_EAST, SD_SOUTH, SD_WEST};
    TestWorld *world = (TestWorld *)calloc(1, sizeof *world);
    SdAccess access = {world, world_get, world_set, NULL};
    SdDesertPyramid pyramid;
    JavaRandom random;
    if (!world) exit(2);
    memset(&pyramid, 0, sizeof pyramid);
    jrand_set(&random, (i64)seed);
    pyramid.origin_x = 0;
    pyramid.base_y = 64;
    pyramid.origin_z = 0;
    pyramid.facing = facing[jrand_int_bound(&random, 4)];
    sd_desert_generate(&access, &pyramid, &random);
    printf("O %lld %d\n", seed, pyramid.facing);
    for (int y = 50; y <= 74; ++y)
        for (int z = 0; z <= 20; ++z)
            for (int x = 0; x <= 20; ++x)
                printf("%04x\n", (unsigned)world_get(world, x, y, z));
    qsort(pyramid.chests, (size_t)pyramid.chest_count,
          sizeof pyramid.chests[0], chest_compare);
    for (int i = 0; i < pyramid.chest_count; ++i)
        printf("C %d %d %d %lld\n", pyramid.chests[i].x,
               pyramid.chests[i].y, pyramid.chests[i].z,
               (long long)pyramid.chests[i].loot_seed);
    free(world);
}

int main(void) {
    const long long seeds[] = {-100000, -98304, -94208, -86016};
    for (int i = 0; i < 4; ++i) run(seeds[i]);
    return 0;
}
