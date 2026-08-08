#include "game/structures_live.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#define GL_ARENA_INTS 262144
#include "genlayer_biomes.h"
#include "biome_props_full.h"
#include "chunk_provider.h"
#include "chunk_provider_end.h"
#include "map_gen_stronghold.h"
#include "map_gen_fortress.h"
#include "stronghold_loot.h"
#pragma GCC diagnostic pop

static int stronghold_biome_allowed(int biome) {
    return mc_bpf_baseHeight(biome) > 0.0f;
}

static int stronghold_find_biome(
        GLNode *nodes, int gen_biomes, GlArena *arena,
        JavaRandom *random, int x, int z, int *found_x, int *found_z) {
    const int range = 112;
    int x0 = (x - range) >> 2;
    int z0 = (z - range) >> 2;
    int x1 = (x + range) >> 2;
    int z1 = (z + range) >> 2;
    int width = x1 - x0 + 1;
    int height = z1 - z0 + 1;
    int choices = 0, present = 0;
    arena->off = 0;
    int *biomes = gl_getInts(
        nodes, arena, gen_biomes, x0, z0, width, height);
    for (int index = 0; index < width * height; ++index) {
        if (!stronghold_biome_allowed(biomes[index])) continue;
        if (!present || jrand_int_bound(random, choices + 1) == 0) {
            *found_x = (x0 + index % width) << 2;
            *found_z = (z0 + index / width) << 2;
            present = 1;
            ++choices;
        }
    }
    return present;
}

static int stronghold_positions_exact(
        long long seed, int out_x[128], int out_z[128]) {
    GLNode nodes[GL_MAX_NODES];
    GlArena *arena = (GlArena *)malloc(sizeof *arena);
    JavaRandom random;
    int voronoi, node_count, ring = 0, in_ring = 0, spread = 3;
    double angle;
    if (!arena) return 0;
    node_count = gl_build(nodes, (i64)seed, &voronoi);
    if (node_count <= 0 || voronoi < 0 || voronoi >= node_count) {
        free(arena);
        return 0;
    }
    jrand_set(&random, (i64)seed);
    angle = jrand_double(&random) * M_PI * 2.0;
    for (int index = 0; index < 128; ++index) {
        double distance = 128.0 + 192.0 * ring
            + (jrand_double(&random) - 0.5) * 80.0;
        int cx = (int)round(cos(angle) * distance);
        int cz = (int)round(sin(angle) * distance);
        int biome_x, biome_z;
        if (stronghold_find_biome(
                nodes, nodes[voronoi].parent, arena, &random,
                (cx << 4) + 8, (cz << 4) + 8,
                &biome_x, &biome_z)) {
            cx = biome_x >> 4;
            cz = biome_z >> 4;
        }
        out_x[index] = cx;
        out_z[index] = cz;
        angle += M_PI * 2.0 / spread;
        if (++in_ring == spread) {
            ++ring;
            in_ring = 0;
            spread += 2 * spread / (ring + 1);
            if (spread > 128 - index) spread = 128 - index;
            angle += jrand_double(&random) * M_PI * 2.0;
        }
    }
    free(arena);
    return 1;
}

static int locate_chunk(long long seed, int index, int *cx, int *cz) {
    int xs[128], zs[128], count = 0;
    sh_find_positions((i64)seed, xs, zs, &count);
    if (index < 0 || index >= count) return 0;
    *cx = xs[index]; *cz = zs[index]; return 1;
}

int gm_stronghold_locate(long long seed, int index, int *block_x, int *block_z) {
    int cx,cz;
    if(!block_x||!block_z||!locate_chunk(seed,index,&cx,&cz))return 0;
    *block_x=cx*16+8;*block_z=cz*16+8;return 1;
}

int gm_stronghold_locate_nearest(
        long long seed, int origin_x, int origin_z,
        int *block_x, int *block_z) {
    int xs[128], zs[128], best = -1;
    unsigned long long best_distance = 0;
    if (!block_x || !block_z) return 0;
    if (!stronghold_positions_exact(seed, xs, zs)) return 0;
    for (int i = 0; i < 128; ++i) {
        long long x = (long long)xs[i] * 16 + 8;
        long long z = (long long)zs[i] * 16 + 8;
        long long dx = x - origin_x;
        long long dz = z - origin_z;
        unsigned long long distance =
            (unsigned long long)(dx * dx + dz * dz);
        if (best < 0 || distance < best_distance) {
            best = i;
            best_distance = distance;
        }
    }
    if (best < 0) return 0;
    *block_x = xs[best] * 16 + 8;
    *block_z = zs[best] * 16 + 8;
    return 1;
}

static int structure_floor_div(int value, int divisor) {
    int quotient = value / divisor;
    int remainder = value % divisor;
    return remainder && ((remainder < 0) != (divisor < 0))
        ? quotient - 1 : quotient;
}

static int village_biome_allowed(int biome) {
    return biome == 1 || biome == 2 || biome == 5 || biome == 35;
}

int gm_village_locate_nearest(
        long long seed, int origin_x, int origin_z,
        int *block_x, int *block_z) {
    GLNode nodes[GL_MAX_NODES];
    GlArena *arena;
    int voronoi, node_count;
    int origin_chunk_x = structure_floor_div(origin_x, 16);
    int origin_chunk_z = structure_floor_div(origin_z, 16);
    if (!block_x || !block_z) return 0;
    arena = (GlArena *)malloc(sizeof *arena);
    if (!arena) return 0;
    node_count = gl_build(nodes, (i64)seed, &voronoi);
    if (node_count <= 0 || voronoi < 0 || voronoi >= node_count) {
        free(arena);
        return 0;
    }
    for (int radius = 0; radius <= 100; ++radius) {
        for (int ring_x = -radius; ring_x <= radius; ++ring_x) {
            int x_edge = ring_x == -radius || ring_x == radius;
            for (int ring_z = -radius; ring_z <= radius; ++ring_z) {
                int z_edge = ring_z == -radius || ring_z == radius;
                int region_x, region_z, candidate_x, candidate_z;
                int block_center_x, block_center_z, biome;
                if (!x_edge && !z_edge) continue;
                region_x = structure_floor_div(
                    origin_chunk_x + 32 * ring_x, 32);
                region_z = structure_floor_div(
                    origin_chunk_z + 32 * ring_z, 32);
                {
                    JavaRandom random;
                    uint64_t mixed = (uint64_t)seed
                        + (uint64_t)(int64_t)region_x
                            * UINT64_C(341873128712)
                        + (uint64_t)(int64_t)region_z
                            * UINT64_C(132897987541)
                        + UINT64_C(10387312);
                    jrand_set(&random, (int64_t)mixed);
                    candidate_x = region_x * 32
                        + jrand_int_bound(&random, 24);
                    candidate_z = region_z * 32
                        + jrand_int_bound(&random, 24);
                }
                block_center_x = candidate_x * 16 + 8;
                block_center_z = candidate_z * 16 + 8;
                arena->off = 0;
                biome = gl_getInts(
                    nodes, arena, nodes[voronoi].parent,
                    block_center_x >> 2, block_center_z >> 2, 1, 1)[0];
                if (!village_biome_allowed(biome)) continue;
                *block_x = block_center_x;
                *block_z = block_center_z;
                free(arena);
                return 1;
            }
        }
    }
    free(arena);
    return 0;
}

static int temple_biome_allowed(int biome) {
    return biome == B_DESERT || biome == B_DESERT_HILLS
        || biome == B_JUNGLE || biome == B_JUNGLE_HILLS
        || biome == B_SWAMP || biome == B_ICE_PLAINS
        || biome == B_COLD_TAIGA;
}

int gm_temple_locate_nearest(
        long long seed, int origin_x, int origin_z,
        int *block_x, int *block_z) {
    GLNode nodes[GL_MAX_NODES];
    GlArena *arena;
    int voronoi, node_count;
    int origin_chunk_x = structure_floor_div(origin_x, 16);
    int origin_chunk_z = structure_floor_div(origin_z, 16);
    if (!block_x || !block_z) return 0;
    arena = (GlArena *)malloc(sizeof *arena);
    if (!arena) return 0;
    node_count = gl_build(nodes, (i64)seed, &voronoi);
    if (node_count <= 0 || voronoi < 0 || voronoi >= node_count) {
        free(arena);
        return 0;
    }
    for (int radius = 0; radius <= 100; ++radius) {
        for (int ring_x = -radius; ring_x <= radius; ++ring_x) {
            int x_edge = ring_x == -radius || ring_x == radius;
            for (int ring_z = -radius; ring_z <= radius; ++ring_z) {
                int z_edge = ring_z == -radius || ring_z == radius;
                int region_x, region_z, candidate_x, candidate_z;
                int block_center_x, block_center_z, biome;
                JavaRandom random;
                uint64_t mixed;
                if (!x_edge && !z_edge) continue;
                region_x = structure_floor_div(
                    origin_chunk_x + 32 * ring_x, 32);
                region_z = structure_floor_div(
                    origin_chunk_z + 32 * ring_z, 32);
                mixed = (uint64_t)seed
                    + (uint64_t)(int64_t)region_x
                        * UINT64_C(341873128712)
                    + (uint64_t)(int64_t)region_z
                        * UINT64_C(132897987541)
                    + UINT64_C(14357617);
                jrand_set(&random, (int64_t)mixed);
                candidate_x = region_x * 32
                    + jrand_int_bound(&random, 24);
                candidate_z = region_z * 32
                    + jrand_int_bound(&random, 24);
                block_center_x = candidate_x * 16 + 8;
                block_center_z = candidate_z * 16 + 8;
                arena->off = 0;
                biome = gl_getInts(
                    nodes, arena, voronoi,
                    block_center_x, block_center_z, 1, 1)[0];
                if (!temple_biome_allowed(biome)) continue;
                *block_x = block_center_x;
                *block_z = block_center_z;
                free(arena);
                return 1;
            }
        }
    }
    free(arena);
    return 0;
}

int gm_mineshaft_locate_nearest(
        long long seed, int origin_x, int origin_z,
        int *block_x, int *block_z) {
    int origin_chunk_x = structure_floor_div(origin_x, 16);
    int origin_chunk_z = structure_floor_div(origin_z, 16);
    if (!block_x || !block_z) return 0;
    for (int radius = 0; radius <= 1000; ++radius) {
        for (int ring_x = -radius; ring_x <= radius; ++ring_x) {
            int x_edge = ring_x == -radius || ring_x == radius;
            for (int ring_z = -radius; ring_z <= radius; ++ring_z) {
                int z_edge = ring_z == -radius || ring_z == radius;
                int candidate_x, candidate_z, distance;
                int32_t mixed_chunks;
                JavaRandom random;
                if (!x_edge && !z_edge) continue;
                candidate_x = origin_chunk_x + ring_x;
                candidate_z = origin_chunk_z + ring_z;
                mixed_chunks = (int32_t)candidate_x
                    ^ (int32_t)candidate_z;
                jrand_set(&random,
                    (int64_t)mixed_chunks ^ (int64_t)seed);
                (void)jrand_int(&random);
                distance = abs(candidate_x) > abs(candidate_z)
                    ? abs(candidate_x) : abs(candidate_z);
                if (jrand_double(&random) >= 0.004
                        || jrand_int_bound(&random, 80) >= distance)
                    continue;
                *block_x = candidate_x * 16 + 8;
                *block_z = candidate_z * 16 + 8;
                return 1;
            }
        }
    }
    return 0;
}

static int mansion_biomes_viable(
        GLNode *nodes, int gen_biomes, GlArena *arena,
        int center_x, int center_z) {
    int min_x = (center_x - 32) >> 2;
    int min_z = (center_z - 32) >> 2;
    int max_x = (center_x + 32) >> 2;
    int max_z = (center_z + 32) >> 2;
    int width = max_x - min_x + 1;
    int height = max_z - min_z + 1;
    arena->off = 0;
    int *biomes = gl_getInts(
        nodes, arena, gen_biomes, min_x, min_z, width, height);
    for (int index = 0; index < width * height; ++index)
        if (biomes[index] != B_ROOFED_FOREST
                && biomes[index] != B_ROOFED_FOREST + 128)
            return 0;
    return 1;
}

int gm_mansion_locate_nearest(
        long long seed, int origin_x, int origin_z,
        int *block_x, int *block_z) {
    GLNode nodes[GL_MAX_NODES];
    GlArena *arena;
    int voronoi, node_count;
    int origin_chunk_x = structure_floor_div(origin_x, 16);
    int origin_chunk_z = structure_floor_div(origin_z, 16);
    if (!block_x || !block_z) return 0;
    arena = (GlArena *)malloc(sizeof *arena);
    if (!arena) return 0;
    node_count = gl_build(nodes, (i64)seed, &voronoi);
    if (node_count <= 0 || voronoi < 0 || voronoi >= node_count) {
        free(arena);
        return 0;
    }
    for (int radius = 0; radius <= 100; ++radius) {
        for (int ring_x = -radius; ring_x <= radius; ++ring_x) {
            int x_edge = ring_x == -radius || ring_x == radius;
            for (int ring_z = -radius; ring_z <= radius; ++ring_z) {
                int z_edge = ring_z == -radius || ring_z == radius;
                int region_x, region_z, candidate_x, candidate_z;
                int block_center_x, block_center_z;
                JavaRandom random;
                uint64_t mixed;
                if (!x_edge && !z_edge) continue;
                region_x = structure_floor_div(
                    origin_chunk_x + 80 * ring_x, 80);
                region_z = structure_floor_div(
                    origin_chunk_z + 80 * ring_z, 80);
                mixed = (uint64_t)seed
                    + (uint64_t)(int64_t)region_x
                        * UINT64_C(341873128712)
                    + (uint64_t)(int64_t)region_z
                        * UINT64_C(132897987541)
                    + UINT64_C(10387319);
                jrand_set(&random, (int64_t)mixed);
                candidate_x = region_x * 80
                    + (jrand_int_bound(&random, 60)
                        + jrand_int_bound(&random, 60)) / 2;
                candidate_z = region_z * 80
                    + (jrand_int_bound(&random, 60)
                        + jrand_int_bound(&random, 60)) / 2;
                block_center_x = candidate_x * 16 + 8;
                block_center_z = candidate_z * 16 + 8;
                if (!mansion_biomes_viable(
                        nodes, nodes[voronoi].parent, arena,
                        block_center_x, block_center_z))
                    continue;
                *block_x = block_center_x;
                *block_z = block_center_z;
                free(arena);
                return 1;
            }
        }
    }
    free(arena);
    return 0;
}

static int monument_biomes_viable(
        GLNode *nodes, int gen_biomes, GlArena *arena,
        int center_x, int center_z, int radius, int deep_only) {
    int min_x = (center_x - radius) >> 2;
    int min_z = (center_z - radius) >> 2;
    int max_x = (center_x + radius) >> 2;
    int max_z = (center_z + radius) >> 2;
    int width = max_x - min_x + 1;
    int height = max_z - min_z + 1;
    arena->off = 0;
    int *biomes = gl_getInts(
        nodes, arena, gen_biomes, min_x, min_z, width, height);
    for (int index = 0; index < width * height; ++index) {
        int biome = biomes[index];
        if (deep_only ? biome != B_DEEP_OCEAN
                : biome != B_OCEAN && biome != B_DEEP_OCEAN
                    && biome != B_RIVER && biome != B_FROZEN_OCEAN
                    && biome != B_FROZEN_RIVER)
            return 0;
    }
    return 1;
}

int gm_monument_locate_nearest(
        long long seed, int origin_x, int origin_z,
        int *block_x, int *block_z) {
    GLNode nodes[GL_MAX_NODES];
    GlArena *arena;
    int voronoi, node_count;
    int origin_chunk_x = structure_floor_div(origin_x, 16);
    int origin_chunk_z = structure_floor_div(origin_z, 16);
    if (!block_x || !block_z) return 0;
    arena = (GlArena *)malloc(sizeof *arena);
    if (!arena) return 0;
    node_count = gl_build(nodes, (i64)seed, &voronoi);
    if (node_count <= 0 || voronoi < 0 || voronoi >= node_count) {
        free(arena);
        return 0;
    }
    for (int radius = 0; radius <= 100; ++radius) {
        for (int ring_x = -radius; ring_x <= radius; ++ring_x) {
            int x_edge = ring_x == -radius || ring_x == radius;
            for (int ring_z = -radius; ring_z <= radius; ++ring_z) {
                int z_edge = ring_z == -radius || ring_z == radius;
                int region_x, region_z, candidate_x, candidate_z;
                int block_center_x, block_center_z;
                JavaRandom random;
                uint64_t mixed;
                if (!x_edge && !z_edge) continue;
                region_x = structure_floor_div(
                    origin_chunk_x + 32 * ring_x, 32);
                region_z = structure_floor_div(
                    origin_chunk_z + 32 * ring_z, 32);
                mixed = (uint64_t)seed
                    + (uint64_t)(int64_t)region_x
                        * UINT64_C(341873128712)
                    + (uint64_t)(int64_t)region_z
                        * UINT64_C(132897987541)
                    + UINT64_C(10387313);
                jrand_set(&random, (int64_t)mixed);
                candidate_x = region_x * 32
                    + (jrand_int_bound(&random, 27)
                        + jrand_int_bound(&random, 27)) / 2;
                candidate_z = region_z * 32
                    + (jrand_int_bound(&random, 27)
                        + jrand_int_bound(&random, 27)) / 2;
                block_center_x = candidate_x * 16 + 8;
                block_center_z = candidate_z * 16 + 8;
                if (!monument_biomes_viable(
                        nodes, nodes[voronoi].parent, arena,
                        block_center_x, block_center_z, 16, 1)
                        || !monument_biomes_viable(
                            nodes, nodes[voronoi].parent, arena,
                            block_center_x, block_center_z, 29, 0))
                    continue;
                *block_x = block_center_x;
                *block_z = block_center_z;
                free(arena);
                return 1;
            }
        }
    }
    free(arena);
    return 0;
}

static int end_city_ground_y(const CpePrimer *primer, int x, int z) {
    for (int y = 255; y >= 0; --y)
        if (cpe_get(primer, x, y, z) != CE_AIR) return y;
    return 0;
}

int gm_end_city_locate_nearest(
        long long seed, int origin_x, int origin_z,
        int *block_x, int *block_z) {
    CpePrimer *primer;
    CpeScratch *scratch;
    int origin_chunk_x = structure_floor_div(origin_x, 16);
    int origin_chunk_z = structure_floor_div(origin_z, 16);
    if (!block_x || !block_z) return 0;
    primer = (CpePrimer *)malloc(sizeof *primer);
    scratch = (CpeScratch *)malloc(sizeof *scratch);
    if (!primer || !scratch) {
        free(primer);
        free(scratch);
        return 0;
    }
    cpe_noise_init(&scratch->noise, (i64)seed);
    for (int radius = 0; radius <= 100; ++radius) {
        for (int ring_x = -radius; ring_x <= radius; ++ring_x) {
            int x_edge = ring_x == -radius || ring_x == radius;
            for (int ring_z = -radius; ring_z <= radius; ++ring_z) {
                int z_edge = ring_z == -radius || ring_z == radius;
                int region_x, region_z, candidate_x, candidate_z;
                int rotation, off_x, off_z, min_y;
                JavaRandom random;
                uint64_t mixed;
                if (!x_edge && !z_edge) continue;
                region_x = structure_floor_div(
                    origin_chunk_x + 20 * ring_x, 20);
                region_z = structure_floor_div(
                    origin_chunk_z + 20 * ring_z, 20);
                mixed = (uint64_t)seed
                    + (uint64_t)(int64_t)region_x
                        * UINT64_C(341873128712)
                    + (uint64_t)(int64_t)region_z
                        * UINT64_C(132897987541)
                    + UINT64_C(10387313);
                jrand_set(&random, (int64_t)mixed);
                candidate_x = region_x * 20
                    + (jrand_int_bound(&random, 9)
                        + jrand_int_bound(&random, 9)) / 2;
                candidate_z = region_z * 20
                    + (jrand_int_bound(&random, 9)
                        + jrand_int_bound(&random, 9)) / 2;
                if ((long long)candidate_x * candidate_x
                            + (long long)candidate_z * candidate_z <= 4096
                        || cpe_getIslandHeightValue(
                            &scratch->noise, candidate_x, candidate_z, 1, 1)
                            < 0.0F)
                    continue;
                cpe_provide_chunk(
                    primer, scratch, (i64)seed, candidate_x, candidate_z);
                jrand_set(&random,
                    (int64_t)candidate_x
                        + (int64_t)candidate_z * INT64_C(10387313));
                rotation = jrand_int_bound(&random, 4);
                off_x = rotation == 1 || rotation == 2 ? -5 : 5;
                off_z = rotation == 2 || rotation == 3 ? -5 : 5;
                min_y = end_city_ground_y(primer, 7, 7);
                {
                    int sample = end_city_ground_y(primer, 7, 7 + off_z);
                    if (sample < min_y) min_y = sample;
                    sample = end_city_ground_y(primer, 7 + off_x, 7);
                    if (sample < min_y) min_y = sample;
                    sample = end_city_ground_y(
                        primer, 7 + off_x, 7 + off_z);
                    if (sample < min_y) min_y = sample;
                }
                if (min_y < 60) continue;
                *block_x = candidate_x * 16 + 8;
                *block_z = candidate_z * 16 + 8;
                free(primer);
                free(scratch);
                return 1;
            }
        }
    }
    free(primer);
    free(scratch);
    return 0;
}

int gm_stronghold_portal_room(long long seed, int index, GmStructureBox *box) {
    int cx,cz;
    if(!box||!locate_chunk(seed,index,&cx,&cz))return 0;
    SHStart *s=(SHStart *)malloc(sizeof *s);
    if(!s)return 0;
    sh_generate(s,(i64)seed,cx,cz);
    if(!s->valid||s->portal_room_idx<0){free(s);return 0;}
    SHBB b=s->pieces[s->portal_room_idx].bb;
    box->min_x=b.minX;box->min_y=b.minY;box->min_z=b.minZ;
    box->max_x=b.maxX;box->max_y=b.maxY;box->max_z=b.maxZ;
    free(s);return 1;
}

static int fortress_chunk(long long seed,int radius,int *cx,int *cz){
    for(int r=0;r<=radius;++r)for(int x=-r;x<=r;++x)for(int z=-r;z<=r;++z){
        if(r&&abs(x)!=r&&abs(z)!=r)continue;
        if(ft_can_spawn((i64)seed,x,z)){*cx=x;*cz=z;return 1;}
    }return 0;
}

int gm_fortress_locate(long long seed,int radius,int *block_x,int *block_z){
    int cx,cz;if(!block_x||!block_z||!fortress_chunk(seed,radius,&cx,&cz))return 0;
    *block_x=cx*16+8;*block_z=cz*16+8;return 1;
}

int gm_fortress_locate_nearest(
        long long seed, int origin_x, int origin_z,
        int *block_x, int *block_z) {
    int origin_chunk_x = structure_floor_div(origin_x, 16);
    int origin_chunk_z = structure_floor_div(origin_z, 16);
    if (!block_x || !block_z) return 0;
    for (int radius = 0; radius <= 1000; ++radius) {
        for (int ring_x = -radius; ring_x <= radius; ++ring_x) {
            int x_edge = ring_x == -radius || ring_x == radius;
            for (int ring_z = -radius; ring_z <= radius; ++ring_z) {
                int z_edge = ring_z == -radius || ring_z == radius;
                int candidate_x, candidate_z;
                if (!x_edge && !z_edge) continue;
                candidate_x = origin_chunk_x + ring_x;
                candidate_z = origin_chunk_z + ring_z;
                if (!ft_can_spawn((i64)seed, candidate_x, candidate_z))
                    continue;
                *block_x = candidate_x * 16 + 8;
                *block_z = candidate_z * 16 + 8;
                return 1;
            }
        }
    }
    return 0;
}

int gm_fortress_spawner_room(long long seed,int radius,GmStructureBox *box){
    int cx,cz;if(!box||!fortress_chunk(seed,radius,&cx,&cz))return 0;
    FtStart *s=(FtStart *)malloc(sizeof *s);if(!s)return 0;
    ft_generate(s,(i64)seed,cx,cz);
    for(int i=0;i<s->piece_count;++i)if(s->pieces[i].type==FT_P_THRONE){
        FtBB b=s->pieces[i].bb;box->min_x=b.minX;box->min_y=b.minY;box->min_z=b.minZ;
        box->max_x=b.maxX;box->max_y=b.maxY;box->max_z=b.maxZ;free(s);return 1;
    }
    free(s);return 0;
}

/* Look up table_id + loot_seed for a block that the real C placement stream
 * placed as a stronghold chest. Seed is the nextLong taken at that site after
 * all preceding stone-brick RNG in piece order — not a worldseed/xor/ordinal
 * helper, and not phantom sites for unplaced crossing/large-library chests. */
int gm_stronghold_chest_info(long long seed, int x, int y, int z,
                             int *table_id, long long *loot_seed)
{
    int xs[128], zs[128], n = 0;
    sh_find_positions((i64)seed, xs, zs, &n);
    for (int i = 0; i < n; ++i) {
        SHStart *s = (SHStart *)malloc(sizeof *s);
        if (!s) return 0;
        sh_generate(s, (i64)seed, xs[i], zs[i]);
        if (!s->valid) { free(s); continue; }
        sh_capture_chest_sites(s);
        for (int c = 0; c < s->n_chest_sites; ++c) {
            const SHChestSite *cs = &s->chest_sites[c];
            if (cs->x == x && cs->y == y && cs->z == z) {
                if (table_id) *table_id = cs->table_id;
                if (loot_seed) *loot_seed = (long long)cs->loot_seed;
                free(s);
                return 1;
            }
        }
        free(s);
    }
    return 0;
}

/* Oracle fixture helper: enumerate placement-stream chest sites for seed0 sh0. */
int gm_stronghold_chest_sites(long long seed, int index,
                              int *out_x, int *out_y, int *out_z,
                              int *out_table, long long *out_seed, int max_out)
{
    int cx, cz, n_out = 0;
    SHStart *s;
    if (max_out <= 0 || !locate_chunk(seed, index, &cx, &cz)) return 0;
    s = (SHStart *)malloc(sizeof *s);
    if (!s) return 0;
    sh_generate(s, (i64)seed, cx, cz);
    if (!s->valid) { free(s); return 0; }
    sh_capture_chest_sites(s);
    for (int c = 0; c < s->n_chest_sites && n_out < max_out; ++c) {
        if (out_x) out_x[n_out] = s->chest_sites[c].x;
        if (out_y) out_y[n_out] = s->chest_sites[c].y;
        if (out_z) out_z[n_out] = s->chest_sites[c].z;
        if (out_table) out_table[n_out] = s->chest_sites[c].table_id;
        if (out_seed) out_seed[n_out] = (long long)s->chest_sites[c].loot_seed;
        ++n_out;
    }
    free(s);
    return n_out;
}
