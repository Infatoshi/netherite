#include "end_city_live.h"

#include <limits.h>
#include <string.h>

#include "../assets/end_city_templates.h"
#include "mc_rng.h"

enum {
    EC_BASE_FLOOR = 0, EC_BASE_ROOF = 1, EC_BRIDGE_END = 2,
    EC_BRIDGE_GENTLE = 3, EC_BRIDGE_PIECE = 4, EC_BRIDGE_STEEP = 5,
    EC_FAT_BASE = 6, EC_FAT_MIDDLE = 7, EC_FAT_TOP = 8,
    EC_SECOND_FLOOR = 9, EC_SECOND_FLOOR_2 = 10, EC_SECOND_ROOF = 11,
    EC_SHIP = 12, EC_THIRD_FLOOR = 13, EC_THIRD_FLOOR_C = 15,
    EC_THIRD_ROOF = 16, EC_TOWER_BASE = 17, EC_TOWER_PIECE = 19,
    EC_TOWER_TOP = 20
};

typedef struct {
    JavaRandom random;
    int ship_created;
    int overflow;
} EcBuild;

static int ec_floor_div(int value, int divisor) {
    int q = value / divisor;
    int r = value % divisor;
    return r < 0 ? q - 1 : q;
}

void gm_end_city_candidate_for_region(
        long long seed, int region_x, int region_z,
        int *chunk_x, int *chunk_z) {
    JavaRandom random;
    uint64_t mixed = (uint64_t)(int64_t)region_x * UINT64_C(341873128712)
        + (uint64_t)(int64_t)region_z * UINT64_C(132897987541)
        + (uint64_t)seed + UINT64_C(10387313);
    jrand_set(&random, (int64_t)mixed);
    int x_draw_a = jrand_int_bound(&random, 9);
    int x_draw_b = jrand_int_bound(&random, 9);
    int z_draw_a = jrand_int_bound(&random, 9);
    int z_draw_b = jrand_int_bound(&random, 9);
    int candidate_x = region_x * 20 + (x_draw_a + x_draw_b) / 2;
    int candidate_z = region_z * 20 + (z_draw_a + z_draw_b) / 2;
    if (chunk_x) *chunk_x = candidate_x;
    if (chunk_z) *chunk_z = candidate_z;
}

int gm_end_city_candidate(long long seed, int chunk_x, int chunk_z) {
    int candidate_x, candidate_z;
    gm_end_city_candidate_for_region(
        seed, ec_floor_div(chunk_x, 20), ec_floor_div(chunk_z, 20),
        &candidate_x, &candidate_z);
    return chunk_x == candidate_x && chunk_z == candidate_z;
}

void gm_end_city_transform(
        int rotation, int x, int y, int z,
        int *out_x, int *out_y, int *out_z) {
    int tx = x, tz = z;
    if (rotation == 1) { tx = -z; tz = x; }
    else if (rotation == 2) { tx = -x; tz = -z; }
    else if (rotation == 3) { tx = z; tz = -x; }
    if (out_x) *out_x = tx;
    if (out_y) *out_y = y;
    if (out_z) *out_z = tz;
}

static int ec_rotate_facing(int facing, int rotation) {
    static const int clockwise[6] = {0, 1, 5, 4, 2, 3};
    while (rotation-- > 0)
        facing = clockwise[facing];
    return facing;
}

int gm_end_city_rotate_meta(int block_id, int meta, int rotation) {
    rotation &= 3;
    if (!rotation) return meta;
    if (block_id == 203) {
        static const int stair_facing[4] = {3, 0, 1, 2}; /* S,W,N,E -> E,S,W,N */
        int facing = meta & 3;
        while (rotation-- > 0) facing = stair_facing[facing];
        return (meta & ~3) | facing;
    }
    if (block_id == 202 && (meta == 4 || meta == 8)
            && (rotation == 1 || rotation == 3))
        return meta == 4 ? 8 : 4;
    if (block_id == 54 || block_id == 65 || block_id == 130
            || block_id == 144 || block_id == 177 || block_id == 198) {
        if (meta >= 2 && meta <= 5)
            return ec_rotate_facing(meta, rotation);
    }
    return meta;
}

static void ec_set_bounds(GmEndCityPiece *piece) {
    const GmEcTemplate *t = &GM_EC_TEMPLATES[piece->template_index];
    int sx = t->sx, sz = t->sz;
    piece->min_y = piece->y;
    piece->max_y = piece->y + t->sy - 1;
    if (piece->rotation == 0) {
        piece->min_x = piece->x; piece->max_x = piece->x + sx;
        piece->min_z = piece->z; piece->max_z = piece->z + sz;
    } else if (piece->rotation == 1) {
        piece->min_x = piece->x - sz; piece->max_x = piece->x;
        piece->min_z = piece->z; piece->max_z = piece->z + sx;
    } else if (piece->rotation == 2) {
        piece->min_x = piece->x - sx; piece->max_x = piece->x;
        piece->min_z = piece->z - sz; piece->max_z = piece->z;
    } else {
        piece->min_x = piece->x; piece->max_x = piece->x + sz;
        piece->min_z = piece->z - sx; piece->max_z = piece->z;
    }
}

static GmEndCityPiece *ec_append_piece(
        EcBuild *build, GmEndCity *list, int template_index,
        int x, int y, int z, int rotation, int overwrite) {
    if (list->count >= GM_END_CITY_MAX_PIECES) {
        build->overflow = 1;
        return NULL;
    }
    GmEndCityPiece *piece = &list->pieces[list->count++];
    memset(piece, 0, sizeof *piece);
    piece->template_index = (short)template_index;
    piece->x = x; piece->y = y; piece->z = z;
    piece->rotation = (unsigned char)(rotation & 3);
    piece->overwrite = overwrite ? 1 : 0;
    ec_set_bounds(piece);
    return piece;
}

static GmEndCityPiece *ec_add_connected(
        EcBuild *build, GmEndCity *list, const GmEndCityPiece *parent,
        int dx, int dy, int dz, int template_index,
        int rotation, int overwrite) {
    int tx, ty, tz;
    gm_end_city_transform(parent->rotation, dx, dy, dz, &tx, &ty, &tz);
    return ec_append_piece(build, list, template_index,
                           parent->x + tx, parent->y + ty, parent->z + tz,
                           rotation, overwrite);
}

static int ec_intersects(const GmEndCityPiece *a, const GmEndCityPiece *b) {
    return a->max_x >= b->min_x && a->min_x <= b->max_x
        && a->max_z >= b->min_z && a->min_z <= b->max_z
        && a->max_y >= b->min_y && a->min_y <= b->max_y;
}

static GmEndCityPiece *ec_find_intersection(
        GmEndCity *list, const GmEndCityPiece *piece) {
    for (int i = 0; i < list->count; ++i)
        if (ec_intersects(&list->pieces[i], piece)) return &list->pieces[i];
    return NULL;
}

static int ec_append_list(EcBuild *build, GmEndCity *dest, GmEndCity *src) {
    if (dest->count + src->count > GM_END_CITY_MAX_PIECES) {
        build->overflow = 1;
        return 0;
    }
    memcpy(dest->pieces + dest->count, src->pieces,
           (size_t)src->count * sizeof src->pieces[0]);
    dest->count += src->count;
    return 1;
}

enum { EC_GEN_HOUSE, EC_GEN_TOWER, EC_GEN_BRIDGE, EC_GEN_FAT };
static int ec_recursive(EcBuild *, int, int, const GmEndCityPiece *,
                        int, int, int, GmEndCity *);

static int ec_generate_house(
        EcBuild *b, int depth, const GmEndCityPiece *parent,
        int connect_x, int connect_y, int connect_z, GmEndCity *list) {
    if (depth > 8) return 0;
    int rot = parent->rotation;
    GmEndCityPiece *p = ec_add_connected(
        b, list, parent, connect_x, connect_y, connect_z,
        EC_BASE_FLOOR, rot, 1);
    if (!p) return 0;
    int choice = jrand_int_bound(&b->random, 3);
    if (choice == 0) {
        return ec_add_connected(b, list, p, -1, 4, -1,
                                EC_BASE_ROOF, rot, 1) != NULL;
    }
    p = ec_add_connected(b, list, p, -1, 0, -1,
                         EC_SECOND_FLOOR_2, rot, 0);
    if (!p) return 0;
    if (choice == 1) {
        p = ec_add_connected(b, list, p, -1, 8, -1,
                             EC_SECOND_ROOF, rot, 0);
    } else {
        p = ec_add_connected(b, list, p, -1, 4, -1,
                             EC_THIRD_FLOOR_C, rot, 0);
        if (p) p = ec_add_connected(b, list, p, -1, 8, -1,
                                    EC_THIRD_ROOF, rot, 1);
    }
    if (!p) return 0;
    (void)ec_recursive(b, EC_GEN_TOWER, depth + 1, p, 0, 0, 0, list);
    return !b->overflow;
}

static const int EC_TOWER_BRIDGE_ROT[4] = {0, 1, 3, 2};
static const int EC_TOWER_BRIDGE_POS[4][3] = {
    {1,-1,0}, {6,-1,1}, {0,-1,5}, {5,-1,6}
};

static int ec_generate_tower(
        EcBuild *b, int depth, const GmEndCityPiece *parent, GmEndCity *list) {
    int rot = parent->rotation;
    int offset_x = 3 + jrand_int_bound(&b->random, 2);
    int offset_z = 3 + jrand_int_bound(&b->random, 2);
    GmEndCityPiece *p = ec_add_connected(
        b, list, parent, offset_x, -3, offset_z,
        EC_TOWER_BASE, rot, 1);
    if (!p) return 0;
    p = ec_add_connected(b, list, p, 0, 7, 0, EC_TOWER_PIECE, rot, 1);
    if (!p) return 0;
    int branch_index = jrand_int_bound(&b->random, 3) == 0
        ? list->count - 1 : -1;
    int levels = 1 + jrand_int_bound(&b->random, 3);
    for (int i = 0; i < levels; ++i) {
        p = ec_add_connected(b, list, p, 0, 4, 0, EC_TOWER_PIECE, rot, 1);
        if (!p) return 0;
        if (i < levels - 1 && jrand_next(&b->random, 1))
            branch_index = list->count - 1;
    }
    if (branch_index >= 0) {
        for (int i = 0; i < 4; ++i) {
            if (!jrand_next(&b->random, 1)) continue;
            GmEndCityPiece *bridge = ec_add_connected(
                b, list, &list->pieces[branch_index],
                EC_TOWER_BRIDGE_POS[i][0], EC_TOWER_BRIDGE_POS[i][1],
                EC_TOWER_BRIDGE_POS[i][2], EC_BRIDGE_END,
                (rot + EC_TOWER_BRIDGE_ROT[i]) & 3, 1);
            if (!bridge) return 0;
            (void)ec_recursive(
                b, EC_GEN_BRIDGE, depth + 1, bridge, 0, 0, 0, list);
        }
        return ec_add_connected(b, list, p, -1, 4, -1,
                                EC_TOWER_TOP, rot, 1) != NULL;
    }
    if (depth != 7)
        return ec_recursive(b, EC_GEN_FAT, depth + 1, p, 0, 0, 0, list);
    return ec_add_connected(b, list, p, -1, 4, -1,
                            EC_TOWER_TOP, rot, 1) != NULL;
}

static int ec_generate_bridge(
        EcBuild *b, int depth, const GmEndCityPiece *parent, GmEndCity *list) {
    int rot = parent->rotation;
    int length = jrand_int_bound(&b->random, 4) + 1;
    GmEndCityPiece *p = ec_add_connected(
        b, list, parent, 0, 0, -4, EC_BRIDGE_PIECE, rot, 1);
    if (!p) return 0;
    p->component_type = -1;
    int rise = 0;
    for (int i = 0; i < length; ++i) {
        if (jrand_next(&b->random, 1)) {
            p = ec_add_connected(b, list, p, 0, rise, -4,
                                 EC_BRIDGE_PIECE, rot, 1);
            rise = 0;
        } else if (jrand_next(&b->random, 1)) {
            p = ec_add_connected(b, list, p, 0, rise, -4,
                                 EC_BRIDGE_STEEP, rot, 1);
            rise = 4;
        } else {
            p = ec_add_connected(b, list, p, 0, rise, -8,
                                 EC_BRIDGE_GENTLE, rot, 1);
            rise = 4;
        }
        if (!p) return 0;
    }
    if (!b->ship_created && jrand_int_bound(&b->random, 10 - depth) == 0) {
        int ship_x = -8 + jrand_int_bound(&b->random, 8);
        int ship_z = -70 + jrand_int_bound(&b->random, 10);
        if (!ec_add_connected(b, list, p, ship_x, rise, ship_z,
                              EC_SHIP, rot, 1))
            return 0;
        b->ship_created = 1;
    } else if (!ec_recursive(
            b, EC_GEN_HOUSE, depth + 1, p, -3, rise + 1, -11, list)) {
        return 0;
    }
    p = ec_add_connected(b, list, p, 4, rise, 0, EC_BRIDGE_END,
                         (rot + 2) & 3, 1);
    if (!p) return 0;
    p->component_type = -1;
    return 1;
}

static const int EC_FAT_BRIDGE_ROT[4] = {0, 1, 3, 2};
static const int EC_FAT_BRIDGE_POS[4][3] = {
    {4,-1,0}, {12,-1,4}, {0,-1,8}, {8,-1,12}
};

static int ec_generate_fat(
        EcBuild *b, int depth, const GmEndCityPiece *parent, GmEndCity *list) {
    int rot = parent->rotation;
    GmEndCityPiece *p = ec_add_connected(
        b, list, parent, -3, 4, -3, EC_FAT_BASE, rot, 1);
    if (!p) return 0;
    p = ec_add_connected(b, list, p, 0, 4, 0, EC_FAT_MIDDLE, rot, 1);
    if (!p) return 0;
    for (int level = 0; level < 2
            && jrand_int_bound(&b->random, 3) != 0; ++level) {
        p = ec_add_connected(b, list, p, 0, 8, 0,
                             EC_FAT_MIDDLE, rot, 1);
        if (!p) return 0;
        int parent_index = list->count - 1;
        for (int i = 0; i < 4; ++i) {
            if (!jrand_next(&b->random, 1)) continue;
            GmEndCityPiece *bridge = ec_add_connected(
                b, list, &list->pieces[parent_index],
                EC_FAT_BRIDGE_POS[i][0], EC_FAT_BRIDGE_POS[i][1],
                EC_FAT_BRIDGE_POS[i][2], EC_BRIDGE_END,
                (rot + EC_FAT_BRIDGE_ROT[i]) & 3, 1);
            if (!bridge) return 0;
            (void)ec_recursive(
                b, EC_GEN_BRIDGE, depth + 1, bridge, 0, 0, 0, list);
        }
        p = &list->pieces[parent_index];
    }
    return ec_add_connected(b, list, p, -2, 8, -2,
                            EC_FAT_TOP, rot, 1) != NULL;
}

static int ec_recursive(
        EcBuild *b, int generator, int depth,
        const GmEndCityPiece *parent,
        int connect_x, int connect_y, int connect_z, GmEndCity *dest) {
    if (depth > 8 || b->overflow) return 0;
    GmEndCity temp;
    memset(&temp, 0, sizeof temp);
    int generated = generator == EC_GEN_HOUSE
        ? ec_generate_house(
            b, depth, parent, connect_x, connect_y, connect_z, &temp)
        : generator == EC_GEN_TOWER
        ? ec_generate_tower(b, depth, parent, &temp)
        : generator == EC_GEN_BRIDGE
        ? ec_generate_bridge(b, depth, parent, &temp)
        : ec_generate_fat(b, depth, parent, &temp);
    if (!generated || b->overflow) return 0;
    int component_type = jrand_int(&b->random);
    for (int i = 0; i < temp.count; ++i) {
        temp.pieces[i].component_type = component_type;
        GmEndCityPiece *hit = ec_find_intersection(dest, &temp.pieces[i]);
        if (hit && hit->component_type != parent->component_type)
            return 0;
    }
    return ec_append_list(b, dest, &temp);
}

int gm_end_city_build(
        long long seed, int chunk_x, int chunk_z,
        int start_y, GmEndCity *out) {
    if (!out || start_y < 0 || start_y > 255) return 0;
    memset(out, 0, sizeof *out);
    EcBuild build;
    memset(&build, 0, sizeof build);
    JavaRandom base;
    jrand_set(&base, seed);
    long long mul_x = jrand_long(&base);
    long long mul_z = jrand_long(&base);
    uint64_t mixed = (uint64_t)(int64_t)chunk_x * (uint64_t)mul_x
        ^ (uint64_t)(int64_t)chunk_z * (uint64_t)mul_z
        ^ (uint64_t)seed;
    jrand_set(&build.random, (int64_t)mixed);
    JavaRandom rotation_random;
    jrand_set(&rotation_random,
              (long long)chunk_x + (long long)chunk_z * 10387313LL);
    int rotation = jrand_int_bound(&rotation_random, 4);
    GmEndCityPiece *p = ec_append_piece(
        &build, out, EC_BASE_FLOOR,
        chunk_x * 16 + 8, start_y, chunk_z * 16 + 8, rotation, 1);
    if (!p) return 0;
    p = ec_add_connected(&build, out, p, -1, 0, -1,
                         EC_SECOND_FLOOR, rotation, 0);
    if (p) p = ec_add_connected(&build, out, p, -1, 4, -1,
                                EC_THIRD_FLOOR, rotation, 0);
    if (p) p = ec_add_connected(&build, out, p, -1, 8, -1,
                                EC_THIRD_ROOF, rotation, 1);
    if (!p || !ec_recursive(&build, EC_GEN_TOWER, 1, p, 0, 0, 0, out)
            || build.overflow)
        return 0;
    out->ship_created = 0;
    for (int i = 0; i < out->count; ++i)
        if (out->pieces[i].template_index == EC_SHIP) {
            out->ship_created = 1;
            break;
        }
    return 1;
}
