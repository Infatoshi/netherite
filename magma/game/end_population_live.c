#include "game/end_population_live.h"

#include <math.h>
#include <stdlib.h>

enum {
    END_AIR = 0,
    END_BEDROCK = 7,
    END_STONE = 121,
    END_CHORUS_PLANT = 199,
    END_CHORUS_FLOWER = 200,
    END_GATEWAY = 209
};

static int end_air(GmEndBlockAccess *world, int x, int y, int z) {
    return y >= 0 && y < 256 && world->get(world->ctx, x, y, z) == END_AIR;
}

/* EnumFacing.Plane.HORIZONTAL iteration order is NORTH,EAST,SOUTH,WEST. */
static int end_neighbors_empty(
    GmEndBlockAccess *world, int x, int y, int z, int except) {
    static const int dx[4] = {0, 1, 0, -1};
    static const int dz[4] = {-1, 0, 1, 0};
    for (int facing = 0; facing < 4; ++facing)
        if (facing != except
                && !end_air(world, x + dx[facing], y, z + dz[facing]))
            return 0;
    return 1;
}

static void end_grow_chorus(
    GmEndBlockAccess *world, int x, int y, int z,
    JavaRandom *random, int root_x, int root_z, int radius, int depth) {
    static const int dx[4] = {0, 1, 0, -1};
    static const int dz[4] = {-1, 0, 1, 0};
    int height = jrand_int_bound(random, 4) + 1;
    if (depth == 0) ++height;
    for (int j = 0; j < height; ++j) {
        if (!end_neighbors_empty(world, x, y + j + 1, z, -1))
            return;
        world->set(world->ctx, x, y + j + 1, z, END_CHORUS_PLANT, 0);
    }
    int branched = 0;
    if (depth < 4) {
        int branches = jrand_int_bound(random, 4);
        if (depth == 0) ++branches;
        for (int k = 0; k < branches; ++k) {
            int facing = jrand_int_bound(random, 4);
            int bx = x + dx[facing], by = y + height, bz = z + dz[facing];
            int opposite = (facing + 2) & 3;
            if (abs(bx - root_x) < radius && abs(bz - root_z) < radius
                    && end_air(world, bx, by, bz)
                    && end_air(world, bx, by - 1, bz)
                    && end_neighbors_empty(world, bx, by, bz, opposite)) {
                branched = 1;
                world->set(world->ctx, bx, by, bz, END_CHORUS_PLANT, 0);
                end_grow_chorus(world, bx, by, bz, random,
                                root_x, root_z, radius, depth + 1);
            }
        }
    }
    if (!branched)
        world->set(world->ctx, x, y + height, z, END_CHORUS_FLOWER, 5);
}

void gm_end_generate_chorus(
    GmEndBlockAccess *world, int x, int y, int z,
    JavaRandom *random, int radius) {
    if (!world || !world->get || !world->set || !random) return;
    world->set(world->ctx, x, y, z, END_CHORUS_PLANT, 0);
    end_grow_chorus(world, x, y, z, random, x, z, radius, 0);
}

void gm_end_generate_island(
    GmEndBlockAccess *world, int x, int y, int z, JavaRandom *random) {
    if (!world || !world->set || !random) return;
    float radius = (float)(jrand_int_bound(random, 3) + 4);
    for (int dy = 0; radius > 0.5f; --dy) {
        int lo = (int)floorf(-radius), hi = (int)ceilf(radius);
        float limit = (radius + 1.0f) * (radius + 1.0f);
        for (int dx = lo; dx <= hi; ++dx)
            for (int dz = lo; dz <= hi; ++dz)
                if ((float)(dx * dx + dz * dz) <= limit)
                    world->set(world->ctx, x + dx, y + dy, z + dz,
                               END_STONE, 0);
        radius = (float)((double)radius
                         - ((double)jrand_int_bound(random, 2) + 0.5));
    }
}

void gm_end_generate_gateway(GmEndBlockAccess *world, int x, int y, int z) {
    if (!world || !world->set) return;
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -2; dy <= 2; ++dy)
            for (int dz = -1; dz <= 1; ++dz) {
                int center_x = dx == 0, center_y = dy == 0, center_z = dz == 0;
                int edge_y = abs(dy) == 2;
                int id = END_AIR;
                if (center_x && center_y && center_z) id = END_GATEWAY;
                else if (center_y) id = END_AIR;
                else if (edge_y && center_x && center_z) id = END_BEDROCK;
                else if ((center_x || center_z) && !edge_y) id = END_BEDROCK;
                world->set(world->ctx, x + dx, y + dy, z + dz, id, 0);
            }
}
