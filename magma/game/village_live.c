#include "game/village_live.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "mc_rng.h"
#include "assets/village_templates.h"

enum { VW_COUNT = 9 };

typedef struct {
    int kind, weight, spawned, limit, active;
} VillageWeight;

typedef struct {
    GmVillage *village;
    JavaRandom random;
    VillageWeight weights[VW_COUNT];
    int last_weight;
    int terrain_type;
    int start_min_x, start_min_z;
    int pending_roads[GM_VILLAGE_MAX_PIECES];
    int pending_houses[GM_VILLAGE_MAX_PIECES];
    int road_count, house_count;
    int failed;
} VillageBuild;

static int vi_abs(int value) { return value < 0 ? -value : value; }

static GmVillageBox vi_box(int min_x, int min_y, int min_z,
                           int max_x, int max_y, int max_z) {
    GmVillageBox box = {min_x, min_y, min_z, max_x, max_y, max_z};
    return box;
}

static GmVillageBox vi_component_box(int x, int y, int z,
                                     int sx, int sy, int sz, int facing) {
    switch (facing) {
        case GM_VILLAGE_NORTH:
            return vi_box(x, y, z - sz + 1, x + sx - 1, y + sy - 1, z);
        case GM_VILLAGE_SOUTH:
            return vi_box(x, y, z, x + sx - 1, y + sy - 1, z + sz - 1);
        case GM_VILLAGE_WEST:
            return vi_box(x - sz + 1, y, z, x, y + sy - 1, z + sx - 1);
        case GM_VILLAGE_EAST:
            return vi_box(x, y, z, x + sz - 1, y + sy - 1, z + sx - 1);
        default:
            return vi_box(x, y, z, x + sx - 1, y + sy - 1, z + sz - 1);
    }
}

static int vi_intersects(const GmVillageBox *a, const GmVillageBox *b) {
    return a->max_x >= b->min_x && a->min_x <= b->max_x
        && a->max_z >= b->min_z && a->min_z <= b->max_z
        && a->max_y >= b->min_y && a->min_y <= b->max_y;
}

static int vi_find_intersection(const GmVillage *v, const GmVillageBox *box) {
    for (int i = 0; i < v->count; ++i)
        if (vi_intersects(&v->pieces[i].box, box)) return i;
    return -1;
}

static int vi_box_x_size(const GmVillageBox *box) {
    return box->max_x - box->min_x + 1;
}

static int vi_box_z_size(const GmVillageBox *box) {
    return box->max_z - box->min_z + 1;
}

static int vi_random_range(JavaRandom *random, int min, int max) {
    return min + jrand_int_bound(random, max - min + 1);
}

static int vi_random_crop(JavaRandom *random) {
    switch (jrand_int_bound(random, 10)) {
        case 0: case 1: return 141; /* carrots */
        case 2: case 3: return 142; /* potatoes */
        case 4: return 207;         /* beetroots */
        default: return 59;         /* wheat */
    }
}

static int vi_append(VillageBuild *build, int kind, int component_type,
                     int facing, GmVillageBox box) {
    GmVillagePiece *piece;
    if (build->village->count >= GM_VILLAGE_MAX_PIECES) {
        build->failed = 1;
        return -1;
    }
    piece = &build->village->pieces[build->village->count];
    memset(piece, 0, sizeof *piece);
    piece->kind = kind;
    piece->component_type = component_type;
    piece->facing = facing;
    piece->box = box;
    piece->average_ground_lvl = -1;
    return build->village->count++;
}

static int vi_active_weight_count(const VillageBuild *build) {
    int count = 0;
    for (int i = 0; i < VW_COUNT; ++i) count += build->weights[i].active;
    return count;
}

static int vi_weight_total(const VillageBuild *build) {
    int any_remaining = 0;
    int total = 0;
    for (int i = 0; i < VW_COUNT; ++i) {
        const VillageWeight *weight = &build->weights[i];
        if (!weight->active) continue;
        if (weight->limit > 0 && weight->spawned < weight->limit)
            any_remaining = 1;
        total += weight->weight;
    }
    return any_remaining ? total : -1;
}

static int vi_piece_dimensions(int kind, int *sx, int *sy, int *sz) {
    switch (kind) {
        case GM_VILLAGE_HOUSE4_GARDEN: *sx=5;  *sy=6;  *sz=5;  return 1;
        case GM_VILLAGE_CHURCH:       *sx=5;  *sy=12; *sz=9;  return 1;
        case GM_VILLAGE_HOUSE1:       *sx=9;  *sy=9;  *sz=6;  return 1;
        case GM_VILLAGE_WOOD_HUT:     *sx=4;  *sy=6;  *sz=5;  return 1;
        case GM_VILLAGE_HALL:         *sx=9;  *sy=7;  *sz=11; return 1;
        case GM_VILLAGE_FIELD1:       *sx=13; *sy=4;  *sz=9;  return 1;
        case GM_VILLAGE_FIELD2:       *sx=7;  *sy=4;  *sz=9;  return 1;
        case GM_VILLAGE_HOUSE2:       *sx=10; *sy=6;  *sz=7;  return 1;
        case GM_VILLAGE_HOUSE3:       *sx=9;  *sy=7;  *sz=12; return 1;
        default: return 0;
    }
}

static int vi_create_house(VillageBuild *build, int kind, int x, int y, int z,
                           int facing, int component_type) {
    int sx, sy, sz;
    GmVillageBox box;
    int index;
    if (!vi_piece_dimensions(kind, &sx, &sy, &sz)) return -1;
    box = vi_component_box(x, y, z, sx, sy, sz, facing);
    if (kind != GM_VILLAGE_HOUSE4_GARDEN && box.min_y <= 10) return -1;
    if (vi_find_intersection(build->village, &box) >= 0) return -1;
    index = vi_append(build, kind, component_type, facing, box);
    if (index < 0) return -1;
    GmVillagePiece *piece = &build->village->pieces[index];
    if (kind == GM_VILLAGE_HOUSE4_GARDEN)
        piece->extra[0] = jrand_next(&build->random, 1) != 0;
    else if (kind == GM_VILLAGE_WOOD_HUT) {
        piece->extra[0] = jrand_next(&build->random, 1) != 0;
        piece->extra[1] = jrand_int_bound(&build->random, 3);
    } else if (kind == GM_VILLAGE_FIELD1) {
        for (int i = 0; i < 4; ++i)
            piece->extra[i] = vi_random_crop(&build->random);
    } else if (kind == GM_VILLAGE_FIELD2) {
        piece->extra[0] = vi_random_crop(&build->random);
        piece->extra[1] = vi_random_crop(&build->random);
    }
    return index;
}

static int vi_create_torch(VillageBuild *build, int x, int y, int z,
                           int facing, int component_type) {
    GmVillageBox box = vi_component_box(x, y, z, 3, 4, 2, facing);
    if (vi_find_intersection(build->village, &box) >= 0) return -1;
    return vi_append(build, GM_VILLAGE_TORCH, component_type, facing, box);
}

static int vi_create_component(VillageBuild *build, int x, int y, int z,
                               int facing, int component_type) {
    int total = vi_weight_total(build);
    if (total <= 0) return -1;
    for (int attempt = 0; attempt < 5; ++attempt) {
        int roll = jrand_int_bound(&build->random, total);
        for (int i = 0; i < VW_COUNT; ++i) {
            VillageWeight *weight = &build->weights[i];
            int index;
            if (!weight->active) continue;
            roll -= weight->weight;
            if (roll >= 0) continue;
            if (weight->spawned >= weight->limit
                    || (i == build->last_weight
                        && vi_active_weight_count(build) > 1))
                break;
            index = vi_create_house(build, weight->kind, x, y, z,
                                    facing, component_type);
            if (index < 0) continue;
            ++weight->spawned;
            build->last_weight = i;
            if (weight->spawned >= weight->limit) weight->active = 0;
            return index;
        }
    }
    return vi_create_torch(build, x, y, z, facing, component_type);
}

static int vi_add_component(VillageBuild *build, int x, int y, int z,
                            int facing, int parent_type) {
    int index;
    if (parent_type > 50
            || vi_abs(x - build->start_min_x) > 112
            || vi_abs(z - build->start_min_z) > 112)
        return -1;
    index = vi_create_component(build, x, y, z, facing, parent_type + 1);
    if (index >= 0) {
        if (build->house_count >= GM_VILLAGE_MAX_PIECES) {
            build->failed = 1;
            return -1;
        }
        build->pending_houses[build->house_count++] = index;
    }
    return index;
}

static int vi_find_path_box(VillageBuild *build, int x, int y, int z,
                            int facing, GmVillageBox *box) {
    int length = 7 * vi_random_range(&build->random, 3, 5);
    for (; length >= 7; length -= 7) {
        GmVillageBox candidate = vi_component_box(x, y, z, 3, 3, length, facing);
        if (vi_find_intersection(build->village, &candidate) < 0) {
            *box = candidate;
            return 1;
        }
    }
    return 0;
}

static int vi_add_road(VillageBuild *build, int x, int y, int z,
                       int facing, int component_type) {
    GmVillageBox box;
    int index;
    if (component_type > 3 + build->terrain_type
            || vi_abs(x - build->start_min_x) > 112
            || vi_abs(z - build->start_min_z) > 112
            || !vi_find_path_box(build, x, y, z, facing, &box)
            || box.min_y <= 10)
        return -1;
    index = vi_append(build, GM_VILLAGE_PATH, component_type, facing, box);
    if (index < 0) return -1;
    build->village->pieces[index].extra[0] =
        vi_box_x_size(&box) > vi_box_z_size(&box)
            ? vi_box_x_size(&box) : vi_box_z_size(&box);
    if (build->road_count >= GM_VILLAGE_MAX_PIECES) {
        build->failed = 1;
        return -1;
    }
    build->pending_roads[build->road_count++] = index;
    return index;
}

static int vi_next_nn(VillageBuild *build, const GmVillagePiece *piece,
                      int y_offset, int along) {
    if (piece->facing == GM_VILLAGE_NORTH
            || piece->facing == GM_VILLAGE_SOUTH)
        return vi_add_component(build, piece->box.min_x - 1,
            piece->box.min_y + y_offset, piece->box.min_z + along,
            GM_VILLAGE_WEST, piece->component_type);
    return vi_add_component(build, piece->box.min_x + along,
        piece->box.min_y + y_offset, piece->box.min_z - 1,
        GM_VILLAGE_NORTH, piece->component_type);
}

static int vi_next_pp(VillageBuild *build, const GmVillagePiece *piece,
                      int y_offset, int along) {
    if (piece->facing == GM_VILLAGE_NORTH
            || piece->facing == GM_VILLAGE_SOUTH)
        return vi_add_component(build, piece->box.max_x + 1,
            piece->box.min_y + y_offset, piece->box.min_z + along,
            GM_VILLAGE_EAST, piece->component_type);
    return vi_add_component(build, piece->box.min_x + along,
        piece->box.min_y + y_offset, piece->box.max_z + 1,
        GM_VILLAGE_SOUTH, piece->component_type);
}

static void vi_build_path(VillageBuild *build, int piece_index) {
    /* Appends can move the logical end but never the fixed array. Copying the
     * path also makes the sequencing explicit. */
    GmVillagePiece path = build->village->pieces[piece_index];
    int length = path.extra[0];
    int made_house = 0;
    for (int i = jrand_int_bound(&build->random, 5); i < length - 8;
         i += 2 + jrand_int_bound(&build->random, 5)) {
        int index = vi_next_nn(build, &path, 0, i);
        if (index >= 0) {
            const GmVillageBox *box = &build->village->pieces[index].box;
            int span = vi_box_x_size(box) > vi_box_z_size(box)
                ? vi_box_x_size(box) : vi_box_z_size(box);
            i += span;
            made_house = 1;
        }
    }
    for (int i = jrand_int_bound(&build->random, 5); i < length - 8;
         i += 2 + jrand_int_bound(&build->random, 5)) {
        int index = vi_next_pp(build, &path, 0, i);
        if (index >= 0) {
            const GmVillageBox *box = &build->village->pieces[index].box;
            int span = vi_box_x_size(box) > vi_box_z_size(box)
                ? vi_box_x_size(box) : vi_box_z_size(box);
            i += span;
            made_house = 1;
        }
    }
    if (made_house && jrand_int_bound(&build->random, 3) > 0) {
        switch (path.facing) {
            case GM_VILLAGE_SOUTH:
                vi_add_road(build, path.box.min_x - 1, path.box.min_y,
                            path.box.max_z - 2, GM_VILLAGE_WEST,
                            path.component_type); break;
            case GM_VILLAGE_WEST:
                vi_add_road(build, path.box.min_x, path.box.min_y,
                            path.box.min_z - 1, GM_VILLAGE_NORTH,
                            path.component_type); break;
            case GM_VILLAGE_EAST:
                vi_add_road(build, path.box.max_x - 2, path.box.min_y,
                            path.box.min_z - 1, GM_VILLAGE_NORTH,
                            path.component_type); break;
            default:
                vi_add_road(build, path.box.min_x - 1, path.box.min_y,
                            path.box.min_z, GM_VILLAGE_WEST,
                            path.component_type); break;
        }
    }
    if (made_house && jrand_int_bound(&build->random, 3) > 0) {
        switch (path.facing) {
            case GM_VILLAGE_SOUTH:
                vi_add_road(build, path.box.max_x + 1, path.box.min_y,
                            path.box.max_z - 2, GM_VILLAGE_EAST,
                            path.component_type); break;
            case GM_VILLAGE_WEST:
                vi_add_road(build, path.box.min_x, path.box.min_y,
                            path.box.max_z + 1, GM_VILLAGE_SOUTH,
                            path.component_type); break;
            case GM_VILLAGE_EAST:
                vi_add_road(build, path.box.max_x - 2, path.box.min_y,
                            path.box.max_z + 1, GM_VILLAGE_SOUTH,
                            path.component_type); break;
            default:
                vi_add_road(build, path.box.max_x + 1, path.box.min_y,
                            path.box.min_z, GM_VILLAGE_EAST,
                            path.component_type); break;
        }
    }
}

static int vi_remove_pending(int *items, int *count, int index) {
    int value = items[index];
    memmove(&items[index], &items[index + 1],
            (size_t)(*count - index - 1) * sizeof items[0]);
    --*count;
    return value;
}

int gm_village_build(long long random_seed, int x, int z,
                     int biome_type, int size, GmVillage *out) {
    static const int kinds[VW_COUNT] = {
        GM_VILLAGE_HOUSE4_GARDEN, GM_VILLAGE_CHURCH,
        GM_VILLAGE_HOUSE1, GM_VILLAGE_WOOD_HUT, GM_VILLAGE_HALL,
        GM_VILLAGE_FIELD1, GM_VILLAGE_FIELD2,
        GM_VILLAGE_HOUSE2, GM_VILLAGE_HOUSE3
    };
    static const int weights[VW_COUNT] = {4,20,20,3,15,3,3,15,8};
    int limits[VW_COUNT];
    VillageBuild build;
    static const int facings[4] = {
        GM_VILLAGE_NORTH, GM_VILLAGE_EAST,
        GM_VILLAGE_SOUTH, GM_VILLAGE_WEST
    };
    if (!out || size < 0 || biome_type < GM_VILLAGE_PLAINS
            || biome_type > GM_VILLAGE_TAIGA)
        return 0;
    memset(out, 0, sizeof *out);
    memset(&build, 0, sizeof build);
    build.village = out;
    build.last_weight = -1;
    build.terrain_type = size;
    build.start_min_x = x;
    build.start_min_z = z;
    jrand_set(&build.random, random_seed);

    limits[0] = vi_random_range(&build.random, 2 + size, 4 + size * 2);
    limits[1] = vi_random_range(&build.random, size, 1 + size);
    limits[2] = vi_random_range(&build.random, size, 2 + size);
    limits[3] = vi_random_range(&build.random, 2 + size, 5 + size * 3);
    limits[4] = vi_random_range(&build.random, size, 2 + size);
    limits[5] = vi_random_range(&build.random, 1 + size, 4 + size);
    limits[6] = vi_random_range(&build.random, 2 + size, 4 + size * 2);
    limits[7] = vi_random_range(&build.random, 0, 1 + size);
    limits[8] = vi_random_range(&build.random, size, 3 + size * 2);
    for (int i = 0; i < VW_COUNT; ++i) {
        build.weights[i] = (VillageWeight){
            kinds[i], weights[i], 0, limits[i], limits[i] != 0
        };
    }

    int facing = facings[jrand_int_bound(&build.random, 4)];
    int start = vi_append(&build, GM_VILLAGE_START, 0, facing,
                          vi_box(x, 64, z, x + 5, 78, z + 5));
    if (start < 0) return 0;
    out->biome_type = biome_type;
    out->zombie_infested = jrand_int_bound(&build.random, 50) == 0;
    out->pieces[start].extra[0] = biome_type;
    out->pieces[start].extra[1] = out->zombie_infested;

    const GmVillageBox *well = &out->pieces[start].box;
    vi_add_road(&build, well->min_x - 1, well->max_y - 4,
                well->min_z + 1, GM_VILLAGE_WEST, 0);
    vi_add_road(&build, well->max_x + 1, well->max_y - 4,
                well->min_z + 1, GM_VILLAGE_EAST, 0);
    vi_add_road(&build, well->min_x + 1, well->max_y - 4,
                well->min_z - 1, GM_VILLAGE_NORTH, 0);
    vi_add_road(&build, well->min_x + 1, well->max_y - 4,
                well->max_z + 1, GM_VILLAGE_SOUTH, 0);

    while (!build.failed && (build.road_count || build.house_count)) {
        if (build.road_count) {
            int pick = jrand_int_bound(&build.random, build.road_count);
            int index = vi_remove_pending(
                build.pending_roads, &build.road_count, pick);
            vi_build_path(&build, index);
        } else {
            int pick = jrand_int_bound(&build.random, build.house_count);
            (void)vi_remove_pending(
                build.pending_houses, &build.house_count, pick);
            /* Ordinary village houses do not add child components. */
        }
    }
    if (build.failed) {
        memset(out, 0, sizeof *out);
        return 0;
    }
    int non_roads = 0;
    for (int i = 0; i < out->count; ++i)
        if (out->pieces[i].kind != GM_VILLAGE_PATH) ++non_roads;
    out->valid = non_roads > 2;
    return 1;
}

static int vi_floor_div(int value, int divisor) {
    int q = value / divisor;
    int r = value % divisor;
    return r && ((r < 0) != (divisor < 0)) ? q - 1 : q;
}

void gm_village_candidate_for_region(long long world_seed, int region_x,
                                     int region_z, int *chunk_x, int *chunk_z) {
    JavaRandom random;
    uint64_t mixed = (uint64_t)world_seed
        + (uint64_t)(int64_t)region_x * UINT64_C(341873128712)
        + (uint64_t)(int64_t)region_z * UINT64_C(132897987541)
        + UINT64_C(10387312);
    jrand_set(&random, (int64_t)mixed);
    if (chunk_x) *chunk_x = region_x * 32 + jrand_int_bound(&random, 24);
    else (void)jrand_int_bound(&random, 24);
    if (chunk_z) *chunk_z = region_z * 32 + jrand_int_bound(&random, 24);
}

int gm_village_candidate(long long world_seed, int chunk_x, int chunk_z) {
    int candidate_x, candidate_z;
    gm_village_candidate_for_region(world_seed,
        vi_floor_div(chunk_x, 32), vi_floor_div(chunk_z, 32),
        &candidate_x, &candidate_z);
    return candidate_x == chunk_x && candidate_z == chunk_z;
}

int gm_village_build_for_world(long long world_seed, int chunk_x, int chunk_z,
                               int biome_type, int size, GmVillage *out) {
    JavaRandom random;
    int64_t xmul, zmul;
    uint64_t mixed;
    jrand_set(&random, (int64_t)world_seed);
    xmul = jrand_long(&random);
    zmul = jrand_long(&random);
    mixed = (uint64_t)(int64_t)chunk_x * (uint64_t)xmul
          ^ (uint64_t)(int64_t)chunk_z * (uint64_t)zmul
          ^ (uint64_t)world_seed;
    jrand_set(&random, (int64_t)mixed);
    (void)jrand_int(&random); /* MapGenStructure.recursiveGenerate */
    /* gm_village_build accepts a java.util.Random constructor seed. Convert
     * the already-advanced internal 48-bit cursor back to that representation. */
    int64_t resumed_seed = (int64_t)(random.seed ^ MC_JR_MULT);
    return gm_village_build(resumed_seed, chunk_x * 16 + 2,
                            chunk_z * 16 + 2, biome_type, size, out);
}

static int vi_access_contains(const GmVillageAccess *access,
                              int x, int y, int z) {
    return y >= 0 && y < 256
        && (!access->contains || access->contains(access->ctx, x, y, z));
}

static int vi_state_id(uint16_t state) { return state >> 4; }

static int vi_is_air_liquid(uint16_t state) {
    int id = vi_state_id(state);
    return id == 0 || (id >= 8 && id <= 11);
}

static uint16_t vi_biome_state(uint16_t state, int biome_type) {
    int id = state >> 4, meta = state & 15;
    if (biome_type == GM_VILLAGE_DESERT) {
        if (id == 17 || id == 162 || id == 4 || id == 13)
            return (uint16_t)(24 << 4);
        if (id == 5) return (uint16_t)((24 << 4) | 2);
        if (id == 53 || id == 67)
            return (uint16_t)((128 << 4) | meta);
    } else if (biome_type == GM_VILLAGE_SAVANNA) {
        if (id == 17 || id == 162)
            return (uint16_t)((162 << 4) | (meta & 12));
        if (id == 5) return (uint16_t)((5 << 4) | 4);
        if (id == 53) return (uint16_t)((163 << 4) | meta);
        if (id == 4) return (uint16_t)(162 << 4);
        if (id == 85) return (uint16_t)(192 << 4);
        if (id == 64) return (uint16_t)((196 << 4) | meta);
    } else if (biome_type == GM_VILLAGE_TAIGA) {
        if (id == 17 || id == 162)
            return (uint16_t)((17 << 4) | (meta & 12) | 1);
        if (id == 5) return (uint16_t)((5 << 4) | 1);
        if (id == 53) return (uint16_t)((134 << 4) | meta);
        if (id == 85) return (uint16_t)(188 << 4);
        if (id == 64) return (uint16_t)((193 << 4) | meta);
    }
    return state;
}

static const GmVillageTemplate *vi_template(const GmVillagePiece *piece) {
    int variant = piece->kind == GM_VILLAGE_HOUSE4_GARDEN
        ? !!piece->extra[0]
        : piece->kind == GM_VILLAGE_WOOD_HUT
            ? !!piece->extra[0] * 3 + piece->extra[1] : 0;
    for (int i = 0; i < GM_VILLAGE_TEMPLATE_COUNT; ++i) {
        const GmVillageTemplate *value = &gm_village_templates[i];
        if (value->kind == piece->kind && value->variant == variant
                && value->facing == piece->facing)
            return value;
    }
    return NULL;
}

static void vi_world_pos(const GmVillagePiece *piece, int x, int z,
                         int *world_x, int *world_z) {
    if (piece->facing == GM_VILLAGE_NORTH) {
        *world_x = piece->box.min_x + x;
        *world_z = piece->box.max_z - z;
    } else if (piece->facing == GM_VILLAGE_SOUTH) {
        *world_x = piece->box.min_x + x;
        *world_z = piece->box.min_z + z;
    } else if (piece->facing == GM_VILLAGE_WEST) {
        *world_x = piece->box.max_x - z;
        *world_z = piece->box.min_z + x;
    } else {
        *world_x = piece->box.min_x + z;
        *world_z = piece->box.min_z + x;
    }
}

static void vi_set_crop(const GmVillageAccess *access,
                        const GmVillagePiece *piece, int base_y,
                        JavaRandom *random, int crop_id, int x, int z) {
    int max_age = crop_id == 207 ? 3 : 7;
    int min_age = max_age / 3;
    int age = vi_random_range(random, min_age, max_age);
    int world_x, world_z;
    vi_world_pos(piece, x, z, &world_x, &world_z);
    if (vi_access_contains(access, world_x, base_y + 1, world_z))
        access->set(access->ctx, world_x, base_y + 1, world_z,
                    (uint16_t)((crop_id << 4) | age));
}

static void vi_place_crops(const GmVillageAccess *access,
                           const GmVillagePiece *piece, int base_y,
                           JavaRandom *random) {
    if (piece->kind == GM_VILLAGE_FIELD1) {
        static const int rows[4][2] = {{1,2},{4,5},{7,8},{10,11}};
        for (int z = 1; z <= 7; ++z)
            for (int crop = 0; crop < 4; ++crop)
                for (int column = 0; column < 2; ++column)
                    vi_set_crop(access, piece, base_y, random,
                                piece->extra[crop], rows[crop][column], z);
    } else if (piece->kind == GM_VILLAGE_FIELD2) {
        static const int rows[2][2] = {{1,2},{4,5}};
        for (int z = 1; z <= 7; ++z)
            for (int crop = 0; crop < 2; ++crop)
                for (int column = 0; column < 2; ++column)
                    vi_set_crop(access, piece, base_y, random,
                                piece->extra[crop], rows[crop][column], z);
    }
}

static void vi_spawn_villagers(const GmVillageAccess *access,
                               GmVillagePiece *piece, int base_y,
                               int zombie_infested) {
    int x = 0, y = 1, z = 2, count = 0;
    switch (piece->kind) {
        case GM_VILLAGE_HOUSE4_GARDEN: x = 1; count = 1; break;
        case GM_VILLAGE_CHURCH: x = 2; count = 1; break;
        case GM_VILLAGE_HOUSE1: x = 2; count = 1; break;
        case GM_VILLAGE_WOOD_HUT: x = 1; count = 1; break;
        case GM_VILLAGE_HALL: x = 4; count = 2; break;
        case GM_VILLAGE_HOUSE2: x = 7; z = 1; count = 1; break;
        case GM_VILLAGE_HOUSE3: x = 4; count = 2; break;
        default: return;
    }
    for (int i = piece->villagers_spawned; i < count; ++i) {
        int world_x, world_z;
        int profession = 0;
        vi_world_pos(piece, x + i, z, &world_x, &world_z);
        if (!vi_access_contains(access, world_x, base_y + y, world_z))
            break;
        ++piece->villagers_spawned;
        if (piece->kind == GM_VILLAGE_CHURCH) profession = 2;
        else if (piece->kind == GM_VILLAGE_HOUSE1) profession = 1;
        else if (piece->kind == GM_VILLAGE_HOUSE2) profession = 3;
        else if (piece->kind == GM_VILLAGE_HALL && i == 0) profession = 4;
        if (access->villager)
            access->villager(access->ctx, world_x, base_y + y, world_z,
                             zombie_infested ? -1 : profession,
                             zombie_infested);
    }
}

int gm_village_place_piece(const GmVillageAccess *access,
                           GmVillagePiece *piece, int biome_type,
                           int zombie_infested, JavaRandom *placement_random) {
    const GmVillageTemplate *template;
    int sum = 0, count = 0, average, base_y;
    if (!access || !piece || !access->get || !access->set || !access->top
            || !placement_random)
        return 0;
    if (piece->kind == GM_VILLAGE_PATH) {
        uint16_t path = vi_biome_state((uint16_t)(208 << 4), biome_type);
        uint16_t planks = vi_biome_state((uint16_t)(5 << 4), biome_type);
        uint16_t gravel = vi_biome_state((uint16_t)(13 << 4), biome_type);
        uint16_t cobble = vi_biome_state((uint16_t)(4 << 4), biome_type);
        for (int x = piece->box.min_x; x <= piece->box.max_x; ++x)
            for (int z = piece->box.min_z; z <= piece->box.max_z; ++z) {
                if (!vi_access_contains(access, x, 64, z)) continue;
                int y = access->top(access->ctx, x, z) - 1;
                if (y < 63) y = 63;
                while (y >= 63) {
                    uint16_t current = access->get(access->ctx, x, y, z);
                    int id = vi_state_id(current);
                    if (id == 2 && vi_state_id(access->get(
                            access->ctx, x, y + 1, z)) == 0) {
                        access->set(access->ctx, x, y, z, path); break;
                    }
                    if (id >= 8 && id <= 11) {
                        access->set(access->ctx, x, y, z, planks); break;
                    }
                    if (id == 12 || id == 24 || id == 179) {
                        access->set(access->ctx, x, y, z, gravel);
                        if (y > 0) access->set(access->ctx, x, y - 1, z, cobble);
                        break;
                    }
                    --y;
                }
            }
        return 1;
    }
    template = vi_template(piece);
    if (!template) return 0;
    for (int z = piece->box.min_z; z <= piece->box.max_z; ++z)
        for (int x = piece->box.min_x; x <= piece->box.max_x; ++x)
            if (vi_access_contains(access, x, 64, z)) {
                int top = access->top(access->ctx, x, z);
                sum += top > 63 ? top : 63;
                ++count;
            }
    if (piece->average_ground_lvl < 0) {
        if (!count) return 1;
        piece->average_ground_lvl = sum / count;
    }
    average = piece->average_ground_lvl;
    base_y = piece->kind == GM_VILLAGE_START ? average - 11 : average;

    /* The piece loops clear every footprint column from its declared height. */
    for (int z = piece->box.min_z; z <= piece->box.max_z; ++z)
        for (int x = piece->box.min_x; x <= piece->box.max_x; ++x) {
            int y = base_y + template->sy;
            while (y < 255 && vi_access_contains(access, x, y, z)
                    && vi_state_id(access->get(access->ctx, x, y, z)) != 0) {
                access->set(access->ctx, x, y, z, 0);
                ++y;
            }
        }

    for (unsigned i = 0; i < template->count; ++i) {
        const GmVillageTemplateCell *cell =
            &gm_village_template_cells[template->first + i];
        int x = piece->box.min_x + cell->x;
        int y = base_y + cell->y;
        int z = piece->box.min_z + cell->z;
        uint16_t state = vi_biome_state(cell->state, biome_type);
        int id = vi_state_id(state);
        if (zombie_infested && (id == 50 || id == 64 || id == 193
                                || id == 196))
            continue;
        if (!vi_access_contains(access, x, y, z)) continue;
        access->set(access->ctx, x, y, z, state);
        if (id == 54 && piece->kind == GM_VILLAGE_HOUSE2
                && !(piece->placement_flags & 1)) {
            long long loot_seed = jrand_long(placement_random);
            piece->placement_flags |= 1;
            if (access->chest)
                access->chest(access->ctx, x, y, z, state & 15, loot_seed);
        }
    }

    vi_place_crops(access, piece, base_y, placement_random);

    if (piece->kind != GM_VILLAGE_START && piece->kind != GM_VILLAGE_TORCH) {
        uint16_t foundation = piece->kind == GM_VILLAGE_FIELD1
                           || piece->kind == GM_VILLAGE_FIELD2
            ? (uint16_t)(3 << 4)
            : vi_biome_state((uint16_t)(4 << 4), biome_type);
        for (int z = piece->box.min_z; z <= piece->box.max_z; ++z)
            for (int x = piece->box.min_x; x <= piece->box.max_x; ++x) {
                int y = base_y - 1;
                while (y > 1 && vi_access_contains(access, x, y, z)
                        && vi_is_air_liquid(access->get(access->ctx, x, y, z))) {
                    access->set(access->ctx, x, y, z, foundation);
                    --y;
                }
            }
    }
    vi_spawn_villagers(access, piece, base_y, zombie_infested);
    return 1;
}
