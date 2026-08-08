#include "game/end_population_live.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_BLOCKS 4096

typedef struct { int x, y, z, id, meta; } Cell;
typedef struct { Cell cells[MAX_BLOCKS]; int count; } SparseWorld;

static int sparse_get(void *ctx, int x, int y, int z) {
    SparseWorld *world = (SparseWorld *)ctx;
    for (int i = 0; i < world->count; ++i)
        if (world->cells[i].x == x && world->cells[i].y == y
                && world->cells[i].z == z)
            return world->cells[i].id;
    return 0;
}

static void sparse_set(
    void *ctx, int x, int y, int z, int id, int meta) {
    SparseWorld *world = (SparseWorld *)ctx;
    for (int i = 0; i < world->count; ++i) {
        Cell *cell = &world->cells[i];
        if (cell->x != x || cell->y != y || cell->z != z) continue;
        if (id == 0) {
            world->cells[i] = world->cells[--world->count];
        } else {
            cell->id = id; cell->meta = meta;
        }
        return;
    }
    if (id == 0) return;
    if (world->count >= MAX_BLOCKS) abort();
    world->cells[world->count++] = (Cell){x, y, z, id, meta};
}

static int cell_compare(const void *av, const void *bv) {
    const Cell *a = (const Cell *)av, *b = (const Cell *)bv;
    if (a->x != b->x) return a->x < b->x ? -1 : 1;
    if (a->y != b->y) return a->y < b->y ? -1 : 1;
    if (a->z != b->z) return a->z < b->z ? -1 : 1;
    if (a->id != b->id) return a->id < b->id ? -1 : 1;
    return (a->meta > b->meta) - (a->meta < b->meta);
}

static uint64_t hash_world(SparseWorld *world) {
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    qsort(world->cells, (size_t)world->count, sizeof(world->cells[0]),
          cell_compare);
    for (int i = 0; i < world->count; ++i) {
        const Cell *cell = &world->cells[i];
        int value[5] = {cell->x, cell->y, cell->z, cell->id, cell->meta};
        for (int k = 0; k < 5; ++k) {
            hash ^= (uint32_t)value[k];
            hash *= UINT64_C(0x100000001b3);
        }
    }
    return hash;
}

static void chorus(long long seed, int x, int y, int z, int radius) {
    SparseWorld world = {0};
    GmEndBlockAccess access = {&world, sparse_get, sparse_set};
    JavaRandom random;
    jrand_set(&random, seed);
    gm_end_generate_chorus(&access, x, y, z, &random, radius);
    printf("C %lld %d %016llx %012llx\n", seed, world.count,
           (unsigned long long)hash_world(&world),
           (unsigned long long)random.seed);
}

static void island(long long seed, int x, int y, int z) {
    SparseWorld world = {0};
    GmEndBlockAccess access = {&world, sparse_get, sparse_set};
    JavaRandom random;
    jrand_set(&random, seed);
    gm_end_generate_island(&access, x, y, z, &random);
    printf("I %lld %d %016llx %012llx\n", seed, world.count,
           (unsigned long long)hash_world(&world),
           (unsigned long long)random.seed);
}

static void gateway(int x, int y, int z) {
    SparseWorld world = {0};
    GmEndBlockAccess access = {&world, sparse_get, sparse_set};
    gm_end_generate_gateway(&access, x, y, z);
    printf("G %d %016llx\n", world.count,
           (unsigned long long)hash_world(&world));
}

int main(void) {
    chorus(0, 0, 70, 0, 8);
    chorus(1, 17, 63, -9, 8);
    chorus(123456789, -23, 91, 31, 8);
    chorus(-99887766, 4, 50, 7, 5);
    island(0, 0, 70, 0);
    island(1, 17, 63, -9);
    island(123456789, -23, 91, 31);
    island(-99887766, 4, 50, 7);
    gateway(32, 75, -48);
    return 0;
}
