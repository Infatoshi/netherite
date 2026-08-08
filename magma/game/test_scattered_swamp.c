#include "scattered_swamp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SX 9
#define SY 256
#define SZ 9

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
    if (x < 0 || x >= SX || y < 0 || y >= SY || z < 0 || z >= SZ) return;
    int index = index_at(x, y, z);
    world->written[index] = 1;
    world->state[index] = state;
}

static void run(long long seed) {
    static const int facing[4] = {SD_NORTH, SD_EAST, SD_SOUTH, SD_WEST};
    TestWorld *world = (TestWorld *)calloc(1, sizeof *world);
    SdAccess access = {world, world_get, world_set, NULL};
    SsSwampHut hut;
    JavaRandom random;
    if (!world) exit(2);
    memset(&hut, 0, sizeof hut);
    jrand_set(&random, seed);
    hut.base.origin_x = 0; hut.base.base_y = 64; hut.base.origin_z = 0;
    hut.base.facing = facing[jrand_int_bound(&random, 4)];
    hut.base.size_z = 9;
    ss_swamp_generate(&access, &hut);
    printf("O %lld %d\n", seed, hut.base.facing);
    for (int y = 55; y <= 75; ++y)
        for (int z = 0; z <= 8; ++z)
            for (int x = 0; x <= 8; ++x)
                printf("%04x\n", (unsigned)world_get(world, x, y, z));
    if (hut.witch_placed)
        printf("W %.1f %.1f %.1f\n", hut.witch_x + 0.5,
               (double)hut.witch_y, hut.witch_z + 0.5);
    if (hut.pot_placed)
        printf("P 0 0\n");
    free(world);
}

int main(void) {
    const long long seeds[] = {-100000, -98304, -94208, -86016};
    for (int i = 0; i < 4; ++i) run(seeds[i]);
    return 0;
}
