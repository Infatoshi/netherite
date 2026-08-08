#include "mansion_live.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../assets/mansion_templates.h"
#include "mc_rng.h"

enum { MF_DOWN, MF_UP, MF_NORTH, MF_SOUTH, MF_WEST, MF_EAST, MF_NONE = -1 };

typedef struct { int x, y, z; } ManPos;
typedef struct { int cell[11][11]; int outside; } ManGrid;
typedef struct {
    JavaRandom random;
    ManGrid base;
    ManGrid third;
    ManGrid rooms[3];
} ManBuild;
typedef struct { ManPos pos; int rotation; const char *wall; } ManPlacement;

static const int MAN_DX[6] = {0, 0, 0, 0, -1, 1};
static const int MAN_DZ[6] = {0, 0, -1, 1, 0, 0};
static const int MAN_HORIZONTAL[4] = {MF_SOUTH, MF_WEST, MF_NORTH, MF_EAST};
static const int MAN_PLANE_HORIZONTAL[4] = {MF_NORTH, MF_EAST, MF_SOUTH, MF_WEST};

static int man_floor_div(int value, int divisor) {
    int q = value / divisor;
    int r = value % divisor;
    return r < 0 ? q - 1 : q;
}

static int man_facing_opposite(int facing) {
    static const int opposite[6] = {MF_UP, MF_DOWN, MF_SOUTH, MF_NORTH,
                                    MF_EAST, MF_WEST};
    return opposite[facing];
}

static int man_facing_cw(int facing) {
    static const int clockwise[6] = {MF_DOWN, MF_UP, MF_EAST, MF_WEST,
                                     MF_NORTH, MF_SOUTH};
    return clockwise[facing];
}

static int man_facing_ccw(int facing) {
    static const int counter[6] = {MF_DOWN, MF_UP, MF_WEST, MF_EAST,
                                   MF_SOUTH, MF_NORTH};
    return counter[facing];
}

static int man_rotate_facing(int rotation, int facing) {
    rotation &= 3;
    while (rotation-- > 0) facing = man_facing_cw(facing);
    return facing;
}

static ManPos man_add(ManPos pos, int x, int y, int z) {
    pos.x += x; pos.y += y; pos.z += z;
    return pos;
}

static ManPos man_offset(ManPos pos, int facing, int amount) {
    pos.x += MAN_DX[facing] * amount;
    pos.z += MAN_DZ[facing] * amount;
    if (facing == MF_UP) pos.y += amount;
    else if (facing == MF_DOWN) pos.y -= amount;
    return pos;
}

static ManPos man_rotated_offset(
        ManPos pos, int rotation, int facing, int amount) {
    return man_offset(pos, man_rotate_facing(rotation, facing), amount);
}

static ManPos man_rotate_pos(ManPos pos, int rotation) {
    int x = pos.x, z = pos.z;
    rotation &= 3;
    if (rotation == 1) { pos.x = -z; pos.z = x; }
    else if (rotation == 2) { pos.x = -x; pos.z = -z; }
    else if (rotation == 3) { pos.x = z; pos.z = -x; }
    return pos;
}

void gm_mansion_transform(
        int mirror, int rotation, int x, int y, int z,
        int *out_x, int *out_y, int *out_z) {
    if (mirror == 1) z = -z;
    else if (mirror == 2) x = -x;
    ManPos pos = {x, y, z};
    pos = man_rotate_pos(pos, rotation);
    if (out_x) *out_x = pos.x;
    if (out_y) *out_y = pos.y;
    if (out_z) *out_z = pos.z;
}

static void man_grid_init(ManGrid *grid, int outside) {
    memset(grid, 0, sizeof *grid);
    grid->outside = outside;
}

static int man_grid_get(const ManGrid *grid, int x, int z) {
    return x >= 0 && x < 11 && z >= 0 && z < 11
        ? grid->cell[x][z] : grid->outside;
}

static void man_grid_set(ManGrid *grid, int x, int z, int value) {
    if (x >= 0 && x < 11 && z >= 0 && z < 11)
        grid->cell[x][z] = value;
}

static void man_grid_fill(
        ManGrid *grid, int min_x, int min_z, int max_x, int max_z, int value) {
    for (int z = min_z; z <= max_z; ++z)
        for (int x = min_x; x <= max_x; ++x)
            man_grid_set(grid, x, z, value);
}

static void man_grid_set_if(
        ManGrid *grid, int x, int z, int expected, int value) {
    if (man_grid_get(grid, x, z) == expected)
        man_grid_set(grid, x, z, value);
}

static int man_grid_edges_to(const ManGrid *grid, int x, int z, int value) {
    return man_grid_get(grid, x - 1, z) == value
        || man_grid_get(grid, x + 1, z) == value
        || man_grid_get(grid, x, z - 1) == value
        || man_grid_get(grid, x, z + 1) == value;
}

static int man_is_house(const ManGrid *grid, int x, int z) {
    int value = man_grid_get(grid, x, z);
    return value >= 1 && value <= 4;
}

static void man_recursive_corridor(
        ManBuild *build, ManGrid *grid, int x, int z,
        int facing, int depth) {
    if (depth <= 0) return;
    man_grid_set(grid, x, z, 1);
    man_grid_set_if(grid, x + MAN_DX[facing], z + MAN_DZ[facing], 0, 1);
    for (int i = 0; i < 8; ++i) {
        int next = MAN_HORIZONTAL[jrand_int_bound(&build->random, 4)];
        if (next == man_facing_opposite(facing)
                || (next == MF_EAST && jrand_next(&build->random, 1)))
            continue;
        int fx = x + MAN_DX[facing], fz = z + MAN_DZ[facing];
        if (man_grid_get(grid, fx + MAN_DX[next], fz + MAN_DZ[next]) == 0
                && man_grid_get(grid, fx + MAN_DX[next] * 2,
                                fz + MAN_DZ[next] * 2) == 0) {
            man_recursive_corridor(build, grid,
                x + MAN_DX[facing] + MAN_DX[next],
                z + MAN_DZ[facing] + MAN_DZ[next], next, depth - 1);
            break;
        }
    }
    int cw = man_facing_cw(facing), ccw = man_facing_ccw(facing);
    man_grid_set_if(grid, x + MAN_DX[cw], z + MAN_DZ[cw], 0, 2);
    man_grid_set_if(grid, x + MAN_DX[ccw], z + MAN_DZ[ccw], 0, 2);
    man_grid_set_if(grid, x + MAN_DX[facing] + MAN_DX[cw],
                    z + MAN_DZ[facing] + MAN_DZ[cw], 0, 2);
    man_grid_set_if(grid, x + MAN_DX[facing] + MAN_DX[ccw],
                    z + MAN_DZ[facing] + MAN_DZ[ccw], 0, 2);
    man_grid_set_if(grid, x + MAN_DX[facing] * 2,
                    z + MAN_DZ[facing] * 2, 0, 2);
    man_grid_set_if(grid, x + MAN_DX[cw] * 2, z + MAN_DZ[cw] * 2, 0, 2);
    man_grid_set_if(grid, x + MAN_DX[ccw] * 2, z + MAN_DZ[ccw] * 2, 0, 2);
}

static int man_clean_edges(ManGrid *grid) {
    int changed = 0;
    for (int z = 0; z < 11; ++z) for (int x = 0; x < 11; ++x) {
        if (man_grid_get(grid, x, z) != 0) continue;
        int sides = man_is_house(grid, x + 1, z) + man_is_house(grid, x - 1, z)
            + man_is_house(grid, x, z + 1) + man_is_house(grid, x, z - 1);
        if (sides >= 3) {
            man_grid_set(grid, x, z, 2);
            changed = 1;
        } else if (sides == 2) {
            int corners = man_is_house(grid, x + 1, z + 1)
                + man_is_house(grid, x - 1, z + 1)
                + man_is_house(grid, x + 1, z - 1)
                + man_is_house(grid, x - 1, z - 1);
            if (corners <= 1) {
                man_grid_set(grid, x, z, 2);
                changed = 1;
            }
        }
    }
    return changed;
}

static int man_room_id(
        const ManBuild *build, int x, int z, int floor, int room_id) {
    return (man_grid_get(&build->rooms[floor], x, z) & 65535) == room_id;
}

static int man_room_direction(
        const ManBuild *build, int x, int z, int floor, int room_id) {
    for (int i = 0; i < 4; ++i) {
        int facing = MAN_PLANE_HORIZONTAL[i];
        if (man_room_id(build, x + MAN_DX[facing], z + MAN_DZ[facing],
                        floor, room_id))
            return facing;
    }
    return MF_NONE;
}

static void man_identify_rooms(
        ManBuild *build, const ManGrid *house, ManGrid *rooms) {
    ManPos cells[121];
    int count = 0;
    for (int z = 0; z < 11; ++z)
        for (int x = 0; x < 11; ++x)
            if (man_grid_get(house, x, z) == 2)
                cells[count++] = (ManPos){x, 0, z};
    for (int i = count; i > 1; --i) {
        int pick = jrand_int_bound(&build->random, i);
        ManPos swap = cells[i - 1];
        cells[i - 1] = cells[pick];
        cells[pick] = swap;
    }
    int room_id = 10;
    for (int cell = 0; cell < count; ++cell) {
        int x = cells[cell].x, z = cells[cell].z;
        if (man_grid_get(rooms, x, z) != 0) continue;
        int min_x = x, max_x = x, min_z = z, max_z = z;
        int shape = 65536;
        if (man_grid_get(rooms, x + 1, z) == 0
                && man_grid_get(rooms, x, z + 1) == 0
                && man_grid_get(rooms, x + 1, z + 1) == 0
                && man_grid_get(house, x + 1, z) == 2
                && man_grid_get(house, x, z + 1) == 2
                && man_grid_get(house, x + 1, z + 1) == 2) {
            max_x = x + 1; max_z = z + 1; shape = 262144;
        } else if (man_grid_get(rooms, x - 1, z) == 0
                && man_grid_get(rooms, x, z + 1) == 0
                && man_grid_get(rooms, x - 1, z + 1) == 0
                && man_grid_get(house, x - 1, z) == 2
                && man_grid_get(house, x, z + 1) == 2
                && man_grid_get(house, x - 1, z + 1) == 2) {
            min_x = x - 1; max_z = z + 1; shape = 262144;
        } else if (man_grid_get(rooms, x - 1, z) == 0
                && man_grid_get(rooms, x, z - 1) == 0
                && man_grid_get(rooms, x - 1, z - 1) == 0
                && man_grid_get(house, x - 1, z) == 2
                && man_grid_get(house, x, z - 1) == 2
                && man_grid_get(house, x - 1, z - 1) == 2) {
            min_x = x - 1; min_z = z - 1; shape = 262144;
        } else if (man_grid_get(rooms, x + 1, z) == 0
                && man_grid_get(house, x + 1, z) == 2) {
            max_x = x + 1; shape = 131072;
        } else if (man_grid_get(rooms, x, z + 1) == 0
                && man_grid_get(house, x, z + 1) == 2) {
            max_z = z + 1; shape = 131072;
        } else if (man_grid_get(rooms, x - 1, z) == 0
                && man_grid_get(house, x - 1, z) == 2) {
            min_x = x - 1; shape = 131072;
        } else if (man_grid_get(rooms, x, z - 1) == 0
                && man_grid_get(house, x, z - 1) == 2) {
            min_z = z - 1; shape = 131072;
        }
        int door_x = jrand_next(&build->random, 1) ? min_x : max_x;
        int door_z = jrand_next(&build->random, 1) ? min_z : max_z;
        int has_door = 2097152;
        if (!man_grid_edges_to(house, door_x, door_z, 1)) {
            door_x = door_x == min_x ? max_x : min_x;
            door_z = door_z == min_z ? max_z : min_z;
            if (!man_grid_edges_to(house, door_x, door_z, 1)) {
                door_z = door_z == min_z ? max_z : min_z;
                if (!man_grid_edges_to(house, door_x, door_z, 1)) {
                    door_x = door_x == min_x ? max_x : min_x;
                    door_z = door_z == min_z ? max_z : min_z;
                    if (!man_grid_edges_to(house, door_x, door_z, 1)) {
                        has_door = 0; door_x = min_x; door_z = min_z;
                    }
                }
            }
        }
        for (int rz = min_z; rz <= max_z; ++rz)
            for (int rx = min_x; rx <= max_x; ++rx)
                man_grid_set(rooms, rx, rz,
                    shape | room_id | ((rx == door_x && rz == door_z)
                                       ? 1048576 | has_door : 0));
        ++room_id;
    }
}

static void man_setup_third(ManBuild *build) {
    ManPos candidates[121];
    int count = 0;
    ManGrid *second_rooms = &build->rooms[1];
    for (int z = 0; z < 11; ++z) for (int x = 0; x < 11; ++x) {
        int cell = man_grid_get(second_rooms, x, z);
        if ((cell & 983040) == 131072 && (cell & 2097152) == 2097152)
            candidates[count++] = (ManPos){x, 0, z};
    }
    if (count == 0) {
        man_grid_fill(&build->third, 0, 0, 11, 11, 5);
        return;
    }
    ManPos chosen = candidates[jrand_int_bound(&build->random, count)];
    int old = man_grid_get(second_rooms, chosen.x, chosen.z);
    man_grid_set(second_rooms, chosen.x, chosen.z, old | 4194304);
    int facing = man_room_direction(
        build, chosen.x, chosen.z, 1, old & 65535);
    int other_x = chosen.x + MAN_DX[facing];
    int other_z = chosen.z + MAN_DZ[facing];
    for (int z = 0; z < 11; ++z) for (int x = 0; x < 11; ++x) {
        if (!man_is_house(&build->base, x, z))
            man_grid_set(&build->third, x, z, 5);
        else if (x == chosen.x && z == chosen.z)
            man_grid_set(&build->third, x, z, 3);
        else if (x == other_x && z == other_z) {
            man_grid_set(&build->third, x, z, 3);
            man_grid_set(&build->rooms[2], x, z, 8388608);
        }
    }
    int facings[4], facing_count = 0;
    for (int i = 0; i < 4; ++i) {
        int dir = MAN_PLANE_HORIZONTAL[i];
        if (man_grid_get(&build->third,
                other_x + MAN_DX[dir], other_z + MAN_DZ[dir]) == 0)
            facings[facing_count++] = dir;
    }
    if (facing_count == 0) {
        man_grid_fill(&build->third, 0, 0, 11, 11, 5);
        man_grid_set(second_rooms, chosen.x, chosen.z, old);
        return;
    }
    facing = facings[jrand_int_bound(&build->random, facing_count)];
    man_recursive_corridor(build, &build->third,
        other_x + MAN_DX[facing], other_z + MAN_DZ[facing], facing, 4);
    while (man_clean_edges(&build->third)) {}
}

static void man_build_grid_cursor(ManBuild *build, JavaRandom random) {
    memset(build, 0, sizeof *build);
    build->random = random;
    man_grid_init(&build->base, 5);
    man_grid_fill(&build->base, 7, 4, 8, 5, 3);
    man_grid_fill(&build->base, 6, 4, 6, 5, 2);
    man_grid_fill(&build->base, 9, 2, 10, 7, 5);
    man_grid_fill(&build->base, 8, 2, 8, 3, 1);
    man_grid_fill(&build->base, 8, 6, 8, 7, 1);
    man_grid_set(&build->base, 6, 3, 1);
    man_grid_set(&build->base, 6, 6, 1);
    man_grid_fill(&build->base, 0, 0, 11, 1, 5);
    man_grid_fill(&build->base, 0, 9, 11, 11, 5);
    man_recursive_corridor(build, &build->base, 7, 2, MF_WEST, 6);
    man_recursive_corridor(build, &build->base, 7, 7, MF_WEST, 6);
    man_recursive_corridor(build, &build->base, 5, 3, MF_WEST, 3);
    man_recursive_corridor(build, &build->base, 5, 6, MF_WEST, 3);
    while (man_clean_edges(&build->base)) {}
    for (int floor = 0; floor < 3; ++floor)
        man_grid_init(&build->rooms[floor], 5);
    man_identify_rooms(build, &build->base, &build->rooms[0]);
    man_identify_rooms(build, &build->base, &build->rooms[1]);
    man_grid_fill(&build->rooms[0], 8, 4, 8, 5, 8388608);
    man_grid_fill(&build->rooms[1], 8, 4, 8, 5, 8388608);
    man_grid_init(&build->third, 5);
    man_setup_third(build);
    man_identify_rooms(build, &build->third, &build->rooms[2]);
}

static int man_template_index(const char *name) {
    for (int i = 0; i < GM_MANSION_TEMPLATE_COUNT; ++i)
        if (strcmp(GM_MANSION_TEMPLATES[i].name, name) == 0) return i;
    return -1;
}

static void man_set_bounds(GmMansionPiece *piece) {
    const GmMansionTemplate *t = &GM_MANSION_TEMPLATES[piece->template_index];
    int sx = t->sx, sy = t->sy, sz = t->sz;
    int tx = (piece->rotation & 1) ? sz : sx;
    int tz = (piece->rotation & 1) ? sx : sz;
    int min_x = 0, min_z = 0, max_x = tx, max_z = tz;
    if (piece->rotation == 1) { min_x -= tx; max_x -= tx; }
    else if (piece->rotation == 2) {
        min_x -= tx; max_x -= tx; min_z -= tz; max_z -= tz;
    } else if (piece->rotation == 3) { min_z -= tz; max_z -= tz; }
    if (piece->mirror == 2) {
        int facing = piece->rotation == 1 || piece->rotation == 3
            ? man_rotate_facing(piece->rotation, MF_WEST)
            : piece->rotation == 2 ? MF_EAST : MF_WEST;
        min_x += MAN_DX[facing] * (piece->rotation & 1 ? tz : tx);
        max_x += MAN_DX[facing] * (piece->rotation & 1 ? tz : tx);
        min_z += MAN_DZ[facing] * (piece->rotation & 1 ? tz : tx);
        max_z += MAN_DZ[facing] * (piece->rotation & 1 ? tz : tx);
    } else if (piece->mirror == 1) {
        int facing = piece->rotation == 1 || piece->rotation == 3
            ? man_rotate_facing(piece->rotation, MF_NORTH)
            : piece->rotation == 2 ? MF_SOUTH : MF_NORTH;
        min_x += MAN_DX[facing] * (piece->rotation & 1 ? tx : tz);
        max_x += MAN_DX[facing] * (piece->rotation & 1 ? tx : tz);
        min_z += MAN_DZ[facing] * (piece->rotation & 1 ? tx : tz);
        max_z += MAN_DZ[facing] * (piece->rotation & 1 ? tx : tz);
    }
    piece->min_x = piece->x + min_x; piece->max_x = piece->x + max_x;
    piece->min_y = piece->y; piece->max_y = piece->y + sy - 1;
    piece->min_z = piece->z + min_z; piece->max_z = piece->z + max_z;
}

static int man_append(
        GmMansion *out, const char *name, ManPos pos,
        int rotation, int mirror) {
    if (out->count >= GM_MANSION_MAX_PIECES) return 0;
    int index = man_template_index(name);
    if (index < 0) return 0;
    GmMansionPiece *piece = &out->pieces[out->count++];
    memset(piece, 0, sizeof *piece);
    piece->template_index = (short)index;
    piece->x = pos.x; piece->y = pos.y; piece->z = pos.z;
    piece->rotation = (unsigned char)(rotation & 3);
    piece->mirror = (unsigned char)mirror;
    man_set_bounds(piece);
    return 1;
}

static int man_wall_piece(GmMansion *out, ManPlacement *place) {
    ManPos pos = man_rotated_offset(place->pos, place->rotation, MF_EAST, 7);
    if (!man_append(out, place->wall, pos, place->rotation, 0)) return 0;
    place->pos = man_rotated_offset(
        place->pos, place->rotation, MF_SOUTH, 8);
    return 1;
}

static int man_turn(GmMansion *out, ManPlacement *place) {
    place->pos = man_rotated_offset(
        place->pos, place->rotation, MF_SOUTH, -1);
    if (!man_append(out, "wall_corner", place->pos, place->rotation, 0))
        return 0;
    place->pos = man_rotated_offset(
        place->pos, place->rotation, MF_SOUTH, -7);
    place->pos = man_rotated_offset(
        place->pos, place->rotation, MF_WEST, -6);
    place->rotation = (place->rotation + 1) & 3;
    return 1;
}

static void man_inner_turn(ManPlacement *place) {
    place->pos = man_rotated_offset(
        place->pos, place->rotation, MF_SOUTH, 6);
    place->pos = man_rotated_offset(
        place->pos, place->rotation, MF_EAST, 8);
    place->rotation = (place->rotation + 3) & 3;
}

static int man_outer_walls(
        GmMansion *out, ManPlacement *place, const ManGrid *grid,
        int facing, int x, int z, int end_x, int end_z) {
    int initial = facing;
    do {
        int fx = x + MAN_DX[facing], fz = z + MAN_DZ[facing];
        if (!man_is_house(grid, fx, fz)) {
            if (!man_turn(out, place)) return 0;
            facing = man_facing_cw(facing);
            if (x != end_x || z != end_z || initial != facing)
                if (!man_wall_piece(out, place)) return 0;
        } else if (man_is_house(grid, fx + MAN_DX[man_facing_ccw(facing)],
                                      fz + MAN_DZ[man_facing_ccw(facing)])) {
            man_inner_turn(place);
            x = fx; z = fz;
            facing = man_facing_ccw(facing);
        } else {
            x = fx; z = fz;
            if (x != end_x || z != end_z || initial != facing)
                if (!man_wall_piece(out, place)) return 0;
        }
    } while (x != end_x || z != end_z || initial != facing);
    return 1;
}

static ManPos man_cell_pos(
        ManPos base, int rotation, int start_x, int start_z, int x, int z) {
    base = man_rotated_offset(
        base, rotation, MF_SOUTH, 8 + (z - start_z) * 8);
    return man_rotated_offset(base, rotation, MF_EAST, (x - start_x) * 8);
}

static int man_create_roof(
        GmMansion *out, ManPos base, int rotation,
        int start_x, int start_z, const ManGrid *grid,
        const ManGrid *upper) {
    for (int z = 0; z < 11; ++z) for (int x = 0; x < 11; ++x) {
        ManPos pos = man_cell_pos(base, rotation, start_x, start_z, x, z);
        int covered = upper && man_is_house(upper, x, z);
        if (!man_is_house(grid, x, z) || covered) continue;
        if (!man_append(out, "roof", man_offset(pos, MF_UP, 3), rotation, 0))
            return 0;
        if (!man_is_house(grid, x + 1, z)) {
            ManPos p = man_rotated_offset(pos, rotation, MF_EAST, 6);
            if (!man_append(out, "roof_front", p, rotation, 0)) return 0;
        }
        if (!man_is_house(grid, x - 1, z)) {
            ManPos p = man_rotated_offset(pos, rotation, MF_SOUTH, 7);
            if (!man_append(out, "roof_front", p, (rotation + 2) & 3, 0))
                return 0;
        }
        if (!man_is_house(grid, x, z - 1)) {
            ManPos p = man_rotated_offset(pos, rotation, MF_WEST, 1);
            if (!man_append(out, "roof_front", p, (rotation + 3) & 3, 0))
                return 0;
        }
        if (!man_is_house(grid, x, z + 1)) {
            ManPos p = man_rotated_offset(pos, rotation, MF_EAST, 6);
            p = man_rotated_offset(p, rotation, MF_SOUTH, 6);
            if (!man_append(out, "roof_front", p, (rotation + 1) & 3, 0))
                return 0;
        }
    }
    if (upper) {
        for (int z = 0; z < 11; ++z) for (int x = 0; x < 11; ++x) {
            ManPos pos = man_cell_pos(base, rotation, start_x, start_z, x, z);
            if (!man_is_house(grid, x, z) || !man_is_house(upper, x, z))
                continue;
            if (!man_is_house(grid, x + 1, z)) {
                ManPos p = man_rotated_offset(pos, rotation, MF_EAST, 7);
                if (!man_append(out, "small_wall", p, rotation, 0)) return 0;
            }
            if (!man_is_house(grid, x - 1, z)) {
                ManPos p = man_rotated_offset(pos, rotation, MF_WEST, 1);
                p = man_rotated_offset(p, rotation, MF_SOUTH, 6);
                if (!man_append(out, "small_wall", p, (rotation + 2) & 3, 0))
                    return 0;
            }
            if (!man_is_house(grid, x, z - 1)) {
                ManPos p = man_rotated_offset(pos, rotation, MF_NORTH, 1);
                if (!man_append(out, "small_wall", p, (rotation + 3) & 3, 0))
                    return 0;
            }
            if (!man_is_house(grid, x, z + 1)) {
                ManPos p = man_rotated_offset(pos, rotation, MF_EAST, 6);
                p = man_rotated_offset(p, rotation, MF_SOUTH, 7);
                if (!man_append(out, "small_wall", p, (rotation + 1) & 3, 0))
                    return 0;
            }
            if (!man_is_house(grid, x + 1, z)) {
                if (!man_is_house(grid, x, z - 1)) {
                    ManPos p = man_rotated_offset(pos, rotation, MF_EAST, 7);
                    p = man_rotated_offset(p, rotation, MF_NORTH, 2);
                    if (!man_append(out, "small_wall_corner", p, rotation, 0))
                        return 0;
                }
                if (!man_is_house(grid, x, z + 1)) {
                    ManPos p = man_rotated_offset(pos, rotation, MF_EAST, 8);
                    p = man_rotated_offset(p, rotation, MF_SOUTH, 7);
                    if (!man_append(out, "small_wall_corner", p,
                                    (rotation + 1) & 3, 0)) return 0;
                }
            }
            if (!man_is_house(grid, x - 1, z)) {
                if (!man_is_house(grid, x, z - 1)) {
                    ManPos p = man_rotated_offset(pos, rotation, MF_WEST, 2);
                    p = man_rotated_offset(p, rotation, MF_NORTH, 1);
                    if (!man_append(out, "small_wall_corner", p,
                                    (rotation + 3) & 3, 0)) return 0;
                }
                if (!man_is_house(grid, x, z + 1)) {
                    ManPos p = man_rotated_offset(pos, rotation, MF_WEST, 1);
                    p = man_rotated_offset(p, rotation, MF_SOUTH, 8);
                    if (!man_append(out, "small_wall_corner", p,
                                    (rotation + 2) & 3, 0)) return 0;
                }
            }
        }
    }
    for (int z = 0; z < 11; ++z) for (int x = 0; x < 11; ++x) {
        ManPos pos = man_cell_pos(base, rotation, start_x, start_z, x, z);
        int covered = upper && man_is_house(upper, x, z);
        if (!man_is_house(grid, x, z) || covered) continue;
        if (!man_is_house(grid, x + 1, z)) {
            ManPos edge = man_rotated_offset(pos, rotation, MF_EAST, 6);
            if (!man_is_house(grid, x, z + 1)) {
                ManPos p = man_rotated_offset(edge, rotation, MF_SOUTH, 6);
                if (!man_append(out, "roof_corner", p, rotation, 0)) return 0;
            } else if (man_is_house(grid, x + 1, z + 1)) {
                ManPos p = man_rotated_offset(edge, rotation, MF_SOUTH, 5);
                if (!man_append(out, "roof_inner_corner", p, rotation, 0))
                    return 0;
            }
            if (!man_is_house(grid, x, z - 1)) {
                if (!man_append(out, "roof_corner", edge, (rotation + 3) & 3, 0))
                    return 0;
            } else if (man_is_house(grid, x + 1, z - 1)) {
                ManPos p = man_rotated_offset(pos, rotation, MF_EAST, 9);
                p = man_rotated_offset(p, rotation, MF_NORTH, 2);
                if (!man_append(out, "roof_inner_corner", p,
                                (rotation + 1) & 3, 0)) return 0;
            }
        }
        if (!man_is_house(grid, x - 1, z)) {
            if (!man_is_house(grid, x, z + 1)) {
                ManPos p = man_rotated_offset(pos, rotation, MF_SOUTH, 6);
                if (!man_append(out, "roof_corner", p, (rotation + 1) & 3, 0))
                    return 0;
            } else if (man_is_house(grid, x - 1, z + 1)) {
                ManPos p = man_rotated_offset(pos, rotation, MF_SOUTH, 8);
                p = man_rotated_offset(p, rotation, MF_WEST, 3);
                if (!man_append(out, "roof_inner_corner", p,
                                (rotation + 3) & 3, 0)) return 0;
            }
            if (!man_is_house(grid, x, z - 1)) {
                if (!man_append(out, "roof_corner", pos, (rotation + 2) & 3, 0))
                    return 0;
            } else if (man_is_house(grid, x - 1, z - 1)) {
                ManPos p = man_rotated_offset(pos, rotation, MF_SOUTH, 1);
                if (!man_append(out, "roof_inner_corner", p,
                                (rotation + 2) & 3, 0)) return 0;
            }
        }
    }
    return 1;
}

static const char *man_numbered(
        char *buffer, const char *prefix, JavaRandom *random, int count) {
    snprintf(buffer, 32, "%s%d", prefix, jrand_int_bound(random, count) + 1);
    return buffer;
}

static const char *man_room_1x1(
        char *buffer, ManBuild *build, int floor) {
    return floor == 0
        ? man_numbered(buffer, "1x1_a", &build->random, 5)
        : man_numbered(buffer, "1x1_b", &build->random, 4);
}

static const char *man_room_1x1_secret(char *buffer, ManBuild *build) {
    return man_numbered(buffer, "1x1_as", &build->random, 4);
}

static const char *man_room_1x2_side(
        char *buffer, ManBuild *build, int floor, int stairs) {
    if (floor != 0 && stairs) return "1x2_c_stairs";
    return floor == 0
        ? man_numbered(buffer, "1x2_a", &build->random, 9)
        : man_numbered(buffer, "1x2_c", &build->random, 4);
}

static const char *man_room_1x2_front(
        char *buffer, ManBuild *build, int floor, int stairs) {
    if (floor != 0 && stairs) return "1x2_d_stairs";
    return floor == 0
        ? man_numbered(buffer, "1x2_b", &build->random, 5)
        : man_numbered(buffer, "1x2_d", &build->random, 5);
}

static const char *man_room_1x2_secret(
        char *buffer, ManBuild *build, int floor) {
    return floor == 0
        ? man_numbered(buffer, "1x2_s", &build->random, 2)
        : man_numbered(buffer, "1x2_se", &build->random, 1);
}

static const char *man_room_2x2(
        char *buffer, ManBuild *build, int floor) {
    return floor == 0
        ? man_numbered(buffer, "2x2_a", &build->random, 4)
        : man_numbered(buffer, "2x2_b", &build->random, 5);
}

static ManPos man_zero_7x7(int rotation) {
    ManPos pos = {1, 0, 0};
    if (rotation == 1) pos = man_add(pos, 6, 0, 0);
    else if (rotation == 2) pos = man_add(pos, 6, 0, 6);
    else if (rotation == 3) pos = man_add(pos, 0, 0, 6);
    return pos;
}

static int man_add_room_1x1(
        ManBuild *build, GmMansion *out, ManPos pos,
        int global_rotation, int door, int floor) {
    char name_a[32], name_b[32];
    const char *name = man_room_1x1(name_a, build, floor);
    int rotation = 0;
    if (door != MF_EAST) {
        if (door == MF_NORTH) rotation = 3;
        else if (door == MF_WEST) rotation = 2;
        else if (door == MF_SOUTH) rotation = 1;
        else name = man_room_1x1_secret(name_b, build);
    }
    ManPos offset = man_rotate_pos(man_zero_7x7(rotation), global_rotation);
    return man_append(out, name, man_add(pos, offset.x, 0, offset.z),
                      (rotation + global_rotation) & 3, 0);
}

static int man_add_room_1x2(
        ManBuild *build, GmMansion *out, ManPos pos, int rotation,
        int room_direction, int door, int floor, int stairs) {
    char name[32];
    ManPos p;
    const char *room;
    if (door == MF_EAST && room_direction == MF_SOUTH) {
        p = man_rotated_offset(pos, rotation, MF_EAST, 1);
        room = man_room_1x2_side(name, build, floor, stairs);
        return man_append(out, room, p, rotation, 0);
    } else if (door == MF_EAST && room_direction == MF_NORTH) {
        p = man_rotated_offset(pos, rotation, MF_EAST, 1);
        p = man_rotated_offset(p, rotation, MF_SOUTH, 6);
        room = man_room_1x2_side(name, build, floor, stairs);
        return man_append(out, room, p, rotation, 1);
    } else if (door == MF_WEST && room_direction == MF_NORTH) {
        p = man_rotated_offset(pos, rotation, MF_EAST, 7);
        p = man_rotated_offset(p, rotation, MF_SOUTH, 6);
        room = man_room_1x2_side(name, build, floor, stairs);
        return man_append(out, room, p, (rotation + 2) & 3, 0);
    } else if (door == MF_WEST && room_direction == MF_SOUTH) {
        p = man_rotated_offset(pos, rotation, MF_EAST, 7);
        room = man_room_1x2_side(name, build, floor, stairs);
        return man_append(out, room, p, rotation, 2);
    } else if (door == MF_SOUTH && room_direction == MF_EAST) {
        p = man_rotated_offset(pos, rotation, MF_EAST, 1);
        room = man_room_1x2_side(name, build, floor, stairs);
        return man_append(out, room, p, (rotation + 1) & 3, 1);
    } else if (door == MF_SOUTH && room_direction == MF_WEST) {
        p = man_rotated_offset(pos, rotation, MF_EAST, 7);
        room = man_room_1x2_side(name, build, floor, stairs);
        return man_append(out, room, p, (rotation + 1) & 3, 0);
    } else if (door == MF_NORTH && room_direction == MF_WEST) {
        p = man_rotated_offset(pos, rotation, MF_EAST, 7);
        p = man_rotated_offset(p, rotation, MF_SOUTH, 6);
        room = man_room_1x2_side(name, build, floor, stairs);
        return man_append(out, room, p, (rotation + 1) & 3, 2);
    } else if (door == MF_NORTH && room_direction == MF_EAST) {
        p = man_rotated_offset(pos, rotation, MF_EAST, 1);
        p = man_rotated_offset(p, rotation, MF_SOUTH, 6);
        room = man_room_1x2_side(name, build, floor, stairs);
        return man_append(out, room, p, (rotation + 3) & 3, 0);
    } else if (door == MF_SOUTH && room_direction == MF_NORTH) {
        p = man_rotated_offset(pos, rotation, MF_EAST, 1);
        p = man_rotated_offset(p, rotation, MF_NORTH, 8);
        room = man_room_1x2_front(name, build, floor, stairs);
        return man_append(out, room, p, rotation, 0);
    } else if (door == MF_NORTH && room_direction == MF_SOUTH) {
        p = man_rotated_offset(pos, rotation, MF_EAST, 7);
        p = man_rotated_offset(p, rotation, MF_SOUTH, 14);
        room = man_room_1x2_front(name, build, floor, stairs);
        return man_append(out, room, p, (rotation + 2) & 3, 0);
    } else if (door == MF_WEST && room_direction == MF_EAST) {
        p = man_rotated_offset(pos, rotation, MF_EAST, 15);
        room = man_room_1x2_front(name, build, floor, stairs);
        return man_append(out, room, p, (rotation + 1) & 3, 0);
    } else if (door == MF_EAST && room_direction == MF_WEST) {
        p = man_rotated_offset(pos, rotation, MF_WEST, 7);
        p = man_rotated_offset(p, rotation, MF_SOUTH, 6);
        room = man_room_1x2_front(name, build, floor, stairs);
        return man_append(out, room, p, (rotation + 3) & 3, 0);
    } else if (door == MF_UP && room_direction == MF_EAST) {
        p = man_rotated_offset(pos, rotation, MF_EAST, 15);
        room = man_room_1x2_secret(name, build, floor);
        return man_append(out, room, p, (rotation + 1) & 3, 0);
    } else if (door == MF_UP && room_direction == MF_SOUTH) {
        p = man_rotated_offset(pos, rotation, MF_EAST, 1);
        room = man_room_1x2_secret(name, build, floor);
        return man_append(out, room, p, rotation, 0);
    }
    return 1;
}

static int man_add_room_2x2(
        ManBuild *build, GmMansion *out, ManPos pos, int global_rotation,
        int room_direction, int door, int floor) {
    int east = 0, south = 0, rotation = global_rotation, mirror = 0;
    if (door == MF_EAST && room_direction == MF_SOUTH) east = -7;
    else if (door == MF_EAST && room_direction == MF_NORTH) {
        east = -7; south = 6; mirror = 1;
    } else if (door == MF_NORTH && room_direction == MF_EAST) {
        east = 1; south = 14; rotation = (global_rotation + 3) & 3;
    } else if (door == MF_NORTH && room_direction == MF_WEST) {
        east = 7; south = 14; rotation = (global_rotation + 3) & 3; mirror = 1;
    } else if (door == MF_SOUTH && room_direction == MF_WEST) {
        east = 7; south = -8; rotation = (global_rotation + 1) & 3;
    } else if (door == MF_SOUTH && room_direction == MF_EAST) {
        east = 1; south = -8; rotation = (global_rotation + 1) & 3; mirror = 1;
    } else if (door == MF_WEST && room_direction == MF_NORTH) {
        east = 15; south = 6; rotation = (global_rotation + 2) & 3;
    } else if (door == MF_WEST && room_direction == MF_SOUTH) {
        east = 15; mirror = 2;
    }
    pos = man_rotated_offset(pos, global_rotation, MF_EAST, east);
    pos = man_rotated_offset(pos, global_rotation, MF_SOUTH, south);
    char name[32];
    return man_append(out, man_room_2x2(name, build, floor),
                      pos, rotation, mirror);
}

static int man_add_room_2x2_secret(
        GmMansion *out, ManPos pos, int rotation) {
    pos = man_rotated_offset(pos, rotation, MF_EAST, 1);
    return man_append(out, "2x2_s1", pos, rotation, 0);
}

static int man_create(
        ManBuild *build, ManPos start, int rotation, GmMansion *out) {
    ManPlacement first = {start, rotation, "wall_flat"};
    ManPos entrance = man_rotated_offset(start, rotation, MF_WEST, 9);
    if (!man_append(out, "entrance", entrance, rotation, 0)) return 0;
    first.pos = man_rotated_offset(first.pos, rotation, MF_SOUTH, 16);
    ManPlacement second = {
        man_offset(first.pos, MF_UP, 8), first.rotation, "wall_window"};
    int start_x = 8, start_z = 5;
    if (!man_outer_walls(out, &first, &build->base,
                         MF_SOUTH, start_x, start_z, 8, 4)) return 0;
    if (!man_outer_walls(out, &second, &build->base,
                         MF_SOUTH, start_x, start_z, 8, 4)) return 0;
    ManPlacement third = {
        man_offset(first.pos, MF_UP, 19), first.rotation, "wall_window"};
    int found = 0;
    for (int z = 0; z < 11 && !found; ++z) {
        for (int x = 10; x >= 0 && !found; --x) {
            if (!man_is_house(&build->third, x, z)) continue;
            third.pos = man_rotated_offset(
                third.pos, rotation, MF_SOUTH, 8 + (z - start_z) * 8);
            third.pos = man_rotated_offset(
                third.pos, rotation, MF_EAST, (x - start_x) * 8);
            if (!man_wall_piece(out, &third)) return 0;
            if (!man_outer_walls(out, &third, &build->third,
                                 MF_SOUTH, x, z, x, z)) return 0;
            found = 1;
        }
    }
    if (!man_create_roof(out, man_offset(start, MF_UP, 16), rotation,
                         start_x, start_z, &build->base, &build->third))
        return 0;
    if (!man_create_roof(out, man_offset(start, MF_UP, 27), rotation,
                         start_x, start_z, &build->third, NULL))
        return 0;

    for (int floor = 0; floor < 3; ++floor) {
        ManPos floor_pos = man_offset(
            start, MF_UP, floor * 8 + (floor == 2 ? 3 : 0));
        const ManGrid *rooms = &build->rooms[floor];
        const ManGrid *grid = floor == 2 ? &build->third : &build->base;
        const char *south_carpet = floor == 0 ? "carpet_south" : "carpet_south_2";
        const char *west_carpet = floor == 0 ? "carpet_west" : "carpet_west_2";
        for (int z = 0; z < 11; ++z) for (int x = 0; x < 11; ++x) {
            if (man_grid_get(grid, x, z) != 1) continue;
            ManPos pos = man_cell_pos(
                floor_pos, rotation, start_x, start_z, x, z);
            if (!man_append(out, "corridor_floor", pos, rotation, 0)) return 0;
            if (man_grid_get(grid, x, z - 1) == 1
                    || (man_grid_get(rooms, x, z - 1) & 8388608)) {
                ManPos p = man_rotated_offset(pos, rotation, MF_EAST, 1);
                if (!man_append(out, "carpet_north", man_offset(p, MF_UP, 1),
                                rotation, 0)) return 0;
            }
            if (man_grid_get(grid, x + 1, z) == 1
                    || (man_grid_get(rooms, x + 1, z) & 8388608)) {
                ManPos p = man_rotated_offset(pos, rotation, MF_SOUTH, 1);
                p = man_rotated_offset(p, rotation, MF_EAST, 5);
                if (!man_append(out, "carpet_east", man_offset(p, MF_UP, 1),
                                rotation, 0)) return 0;
            }
            if (man_grid_get(grid, x, z + 1) == 1
                    || (man_grid_get(rooms, x, z + 1) & 8388608)) {
                ManPos p = man_rotated_offset(pos, rotation, MF_SOUTH, 5);
                p = man_rotated_offset(p, rotation, MF_WEST, 1);
                if (!man_append(out, south_carpet, p, rotation, 0)) return 0;
            }
            if (man_grid_get(grid, x - 1, z) == 1
                    || (man_grid_get(rooms, x - 1, z) & 8388608)) {
                ManPos p = man_rotated_offset(pos, rotation, MF_WEST, 1);
                p = man_rotated_offset(p, rotation, MF_NORTH, 1);
                if (!man_append(out, west_carpet, p, rotation, 0)) return 0;
            }
        }

        const char *wall = floor == 0 ? "indoors_wall" : "indoors_wall_2";
        const char *door_name = floor == 0 ? "indoors_door" : "indoors_door_2";
        for (int z = 0; z < 11; ++z) for (int x = 0; x < 11; ++x) {
            int special_third = floor == 2 && man_grid_get(grid, x, z) == 3;
            if (man_grid_get(grid, x, z) != 2 && !special_third) continue;
            int room = man_grid_get(rooms, x, z);
            int shape = room & 983040;
            int room_id = room & 65535;
            special_third = special_third && (room & 8388608);
            int candidates[4], candidate_count = 0;
            if (room & 2097152) {
                for (int i = 0; i < 4; ++i) {
                    int facing = MAN_PLANE_HORIZONTAL[i];
                    if (man_grid_get(grid,
                            x + MAN_DX[facing], z + MAN_DZ[facing]) == 1)
                        candidates[candidate_count++] = facing;
                }
            }
            int room_door = MF_NONE;
            if (candidate_count)
                room_door = candidates[
                    jrand_int_bound(&build->random, candidate_count)];
            else if (room & 1048576)
                room_door = MF_UP;

            ManPos pos = man_rotated_offset(
                floor_pos, rotation, MF_SOUTH, 8 + (z - start_z) * 8);
            pos = man_rotated_offset(
                pos, rotation, MF_EAST, -1 + (x - start_x) * 8);
            if (man_is_house(grid, x - 1, z)
                    && !man_room_id(build, x - 1, z, floor, room_id)) {
                const char *name = room_door == MF_WEST ? door_name : wall;
                if (!man_append(out, name, pos, rotation, 0)) return 0;
            }
            if (man_grid_get(grid, x + 1, z) == 1 && !special_third) {
                ManPos p = man_rotated_offset(pos, rotation, MF_EAST, 8);
                const char *name = room_door == MF_EAST ? door_name : wall;
                if (!man_append(out, name, p, rotation, 0)) return 0;
            }
            if (man_is_house(grid, x, z + 1)
                    && !man_room_id(build, x, z + 1, floor, room_id)) {
                ManPos p = man_rotated_offset(pos, rotation, MF_SOUTH, 7);
                p = man_rotated_offset(p, rotation, MF_EAST, 7);
                const char *name = room_door == MF_SOUTH ? door_name : wall;
                if (!man_append(out, name, p, (rotation + 1) & 3, 0)) return 0;
            }
            if (man_grid_get(grid, x, z - 1) == 1 && !special_third) {
                ManPos p = man_rotated_offset(pos, rotation, MF_NORTH, 1);
                p = man_rotated_offset(p, rotation, MF_EAST, 7);
                const char *name = room_door == MF_NORTH ? door_name : wall;
                if (!man_append(out, name, p, (rotation + 1) & 3, 0)) return 0;
            }
            if (shape == 65536) {
                if (!man_add_room_1x1(
                        build, out, pos, rotation, room_door, floor)) return 0;
            } else if (shape == 131072 && room_door != MF_NONE) {
                int room_direction = man_room_direction(
                    build, x, z, floor, room_id);
                if (!man_add_room_1x2(build, out, pos, rotation,
                        room_direction, room_door, floor,
                        (room & 4194304) != 0)) return 0;
            } else if (shape == 262144 && room_door != MF_NONE
                       && room_door != MF_UP) {
                int room_direction = man_facing_cw(room_door);
                if (!man_room_id(build, x + MAN_DX[room_direction],
                                 z + MAN_DZ[room_direction], floor, room_id))
                    room_direction = man_facing_opposite(room_direction);
                if (!man_add_room_2x2(build, out, pos, rotation,
                        room_direction, room_door, floor)) return 0;
            } else if (shape == 262144 && room_door == MF_UP) {
                if (!man_add_room_2x2_secret(out, pos, rotation)) return 0;
            }
        }
    }
    return 1;
}

int gm_mansion_build(
        long long layout_seed, int start_x, int start_y, int start_z,
        int rotation, GmMansion *out) {
    if (!out || start_y < 0 || start_y > 255) return 0;
    memset(out, 0, sizeof *out);
    ManBuild build;
    JavaRandom random;
    jrand_set(&random, layout_seed);
    man_build_grid_cursor(&build, random);
    return man_create(&build, (ManPos){start_x, start_y, start_z},
                      rotation & 3, out);
}

int gm_mansion_build_at_chunk(
        long long world_seed, int chunk_x, int chunk_z, int start_y,
        GmMansion *out) {
    if (!out || start_y < 0 || start_y > 255) return 0;
    JavaRandom seed_random, layout_random;
    jrand_set(&seed_random, world_seed);
    long long mul_x = jrand_long(&seed_random);
    long long mul_z = jrand_long(&seed_random);
    uint64_t mixed = (uint64_t)(int64_t)chunk_x * (uint64_t)mul_x
        ^ (uint64_t)(int64_t)chunk_z * (uint64_t)mul_z
        ^ (uint64_t)world_seed;
    jrand_set(&layout_random, (int64_t)mixed);
    int rotation = jrand_int_bound(&layout_random, 4);
    memset(out, 0, sizeof *out);
    ManBuild build;
    man_build_grid_cursor(&build, layout_random);
    return man_create(&build,
        (ManPos){chunk_x * 16 + 8, start_y, chunk_z * 16 + 8},
        rotation, out);
}

void gm_mansion_candidate_for_region(
        long long world_seed, int region_x, int region_z,
        int *chunk_x, int *chunk_z) {
    JavaRandom random;
    uint64_t mixed = (uint64_t)(int64_t)region_x * UINT64_C(341873128712)
        + (uint64_t)(int64_t)region_z * UINT64_C(132897987541)
        + (uint64_t)world_seed + UINT64_C(10387319);
    jrand_set(&random, (int64_t)mixed);
    int x = region_x * 80
        + (jrand_int_bound(&random, 60) + jrand_int_bound(&random, 60)) / 2;
    int z = region_z * 80
        + (jrand_int_bound(&random, 60) + jrand_int_bound(&random, 60)) / 2;
    if (chunk_x) *chunk_x = x;
    if (chunk_z) *chunk_z = z;
}

int gm_mansion_candidate(long long world_seed, int chunk_x, int chunk_z) {
    int region_x = man_floor_div(chunk_x, 80);
    int region_z = man_floor_div(chunk_z, 80);
    int candidate_x, candidate_z;
    gm_mansion_candidate_for_region(
        world_seed, region_x, region_z, &candidate_x, &candidate_z);
    return candidate_x == chunk_x && candidate_z == chunk_z;
}
