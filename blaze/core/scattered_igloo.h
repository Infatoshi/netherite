/* Exact Minecraft 1.11.2 Igloo template placement and population RNG. */
#ifndef MC_SCATTERED_IGLOO_H
#define MC_SCATTERED_IGLOO_H

#include "igloo_templates.h"
#include "scattered_desert.h"

enum {
    IG_ROT_NONE = 0,
    IG_ROT_CW90 = 1,
    IG_ROT_CW180 = 2,
    IG_ROT_CCW90 = 3,
    IG_ENTITY_VILLAGER = 1,
    IG_ENTITY_ZOMBIE_VILLAGER = 2,
    IG_ENTITY_MAX = 2
};

typedef struct {
    double x, y, z, vx, vy, vz;
    float health, yaw, pitch;
    int conversion_time;
    short fire, air;
    signed char profession;
    unsigned char persistence, on_ground;
    int kind;
} IgSpawn;

typedef struct {
    int origin_x, base_y, origin_z;
    int rotation;
    int has_basement;
    int middle_count;
    int chest_x, chest_y, chest_z, chest_facing;
    i64 chest_loot_seed;
    int chest_placed;
    IgSpawn entities[IG_ENTITY_MAX];
    int entity_count;
} IgIgloo;

MC_HD static inline void ig_transform_block(
        int rotation, int x, int z, int *tx, int *tz) {
    if (rotation == IG_ROT_CCW90) {
        *tx = z; *tz = -x;
    } else if (rotation == IG_ROT_CW90) {
        *tx = -z; *tz = x;
    } else if (rotation == IG_ROT_CW180) {
        *tx = -x; *tz = -z;
    } else {
        *tx = x; *tz = z;
    }
}

MC_HD static inline void ig_transform_entity(
        int rotation, double x, double z, double *tx, double *tz) {
    if (rotation == IG_ROT_CCW90) {
        *tx = z; *tz = 1.0 - x;
    } else if (rotation == IG_ROT_CW90) {
        *tx = 1.0 - z; *tz = x;
    } else if (rotation == IG_ROT_CW180) {
        *tx = 1.0 - x; *tz = 1.0 - z;
    } else {
        *tx = x; *tz = z;
    }
}

MC_HD static inline int ig_rotate_facing(int facing, int rotation) {
    if (facing < SD_NORTH || facing > SD_EAST) return facing;
    if (rotation == IG_ROT_CW90)
        return facing == SD_NORTH ? SD_EAST
             : facing == SD_EAST ? SD_SOUTH
             : facing == SD_SOUTH ? SD_WEST : SD_NORTH;
    if (rotation == IG_ROT_CW180)
        return facing == SD_NORTH ? SD_SOUTH
             : facing == SD_SOUTH ? SD_NORTH
             : facing == SD_WEST ? SD_EAST : SD_WEST;
    if (rotation == IG_ROT_CCW90)
        return facing == SD_NORTH ? SD_WEST
             : facing == SD_WEST ? SD_SOUTH
             : facing == SD_SOUTH ? SD_EAST : SD_NORTH;
    return facing;
}

MC_HD static inline int ig_horizontal_to_facing(int meta) {
    static const int facing[4] = {SD_SOUTH, SD_WEST, SD_NORTH, SD_EAST};
    return facing[meta & 3];
}

MC_HD static inline int ig_facing_to_horizontal(int facing) {
    return facing == SD_SOUTH ? 0 : facing == SD_WEST ? 1
         : facing == SD_NORTH ? 2 : 3;
}

MC_HD static inline u16 ig_rotate_state(u16 state, int rotation) {
    int id = state >> 4, meta = state & 15, facing;
    if (id == 26) {
        facing = ig_rotate_facing(ig_horizontal_to_facing(meta), rotation);
        meta = (meta & 12) | ig_facing_to_horizontal(facing);
    } else if (id == 134) {
        facing = 5 - (meta & 3);
        facing = ig_rotate_facing(facing, rotation);
        meta = (meta & 4) | (5 - facing);
    } else if (id == 54 || id == 61 || id == 65 || id == 68) {
        facing = ig_rotate_facing(meta, rotation);
        meta = facing;
    } else if (id == 50 || id == 76) {
        facing = meta == 1 ? SD_EAST : meta == 2 ? SD_WEST
               : meta == 3 ? SD_SOUTH : meta == 4 ? SD_NORTH : 1;
        if (meta != 5) {
            facing = ig_rotate_facing(facing, rotation);
            meta = facing == SD_EAST ? 1 : facing == SD_WEST ? 2
                 : facing == SD_SOUTH ? 3 : 4;
        }
    } else if (id == 96) {
        facing = (meta & 3) == 0 ? SD_NORTH
               : (meta & 3) == 1 ? SD_SOUTH
               : (meta & 3) == 2 ? SD_WEST : SD_EAST;
        facing = ig_rotate_facing(facing, rotation);
        meta = (meta & 12) | (facing == SD_NORTH ? 0
             : facing == SD_SOUTH ? 1 : facing == SD_WEST ? 2 : 3);
    }
    return sd_state(id, meta);
}

MC_HD static inline void ig_place_template(
        SdAccess *access, int template_index, int rotation,
        int ox, int oy, int oz) {
    for (int i = 0; i < ig_template_block_count(template_index); ++i) {
        IgBlock block = ig_template_block_at(template_index, i);
        int tx, tz, wx, wy, wz;
        if (block.id == 255) continue; /* ignoreStructureBlock=true */
        ig_transform_block(rotation, block.x, block.z, &tx, &tz);
        wx = ox + tx; wy = oy + block.y; wz = oz + tz;
        if (access->contains && !access->contains(access->ctx, wx, wy, wz))
            continue;
        access->set(access->ctx, wx, wy, wz,
                    ig_rotate_state(sd_state(block.id, block.meta), rotation));
    }
}

MC_HD static inline void ig_connected_pos(
        int rotation, int ax, int ay, int az, int bx, int by, int bz,
        int *x, int *y, int *z) {
    int atx, atz, btx, btz;
    ig_transform_block(rotation, ax, az, &atx, &atz);
    ig_transform_block(rotation, bx, bz, &btx, &btz);
    *x = atx - btx; *y = ay - by; *z = atz - btz;
}

MC_HD MC_NOINLINE static void ig_igloo_generate(
        SdAccess *access, IgIgloo *igloo, JavaRandom *random) {
    int rotation = jrand_int_bound(random, 4);
    igloo->rotation = rotation;
    ig_place_template(access, 0, rotation,
                      igloo->origin_x, igloo->base_y, igloo->origin_z);
    if (jrand_double(random) < 0.5) {
        int count = jrand_int_bound(random, 8) + 4;
        igloo->has_basement = 1;
        igloo->middle_count = count;
        for (int j = 0; j < count; ++j) {
            int dx, dy, dz;
            ig_connected_pos(rotation, 3, -1 - j * 3, 5, 1, 2, 1,
                             &dx, &dy, &dz);
            ig_place_template(access, 1, rotation,
                igloo->origin_x + dx, igloo->base_y + dy,
                igloo->origin_z + dz);
        }
        {
            int dx, dy, dz, bottom_x, bottom_y, bottom_z;
            ig_connected_pos(rotation, 3, -1 - count * 3, 5, 3, 5, 7,
                             &dx, &dy, &dz);
            bottom_x = igloo->origin_x + dx;
            bottom_y = igloo->base_y + dy;
            bottom_z = igloo->origin_z + dz;
            ig_place_template(access, 2, rotation,
                              bottom_x, bottom_y, bottom_z);
            for (int m = 0; m < ig_template_marker_count(2); ++m) {
                IgMarker marker = ig_template_marker_at(2, m);
                int tx, tz, mx, my, mz;
                ig_transform_block(rotation, marker.x, marker.z, &tx, &tz);
                mx = bottom_x + tx; my = bottom_y + marker.y; mz = bottom_z + tz;
                access->set(access->ctx, mx, my, mz, sd_state(0, 0));
                if (marker.kind == 1) {
                    u16 chest = 0;
                    for (int b = 0; b < ig_template_block_count(2); ++b) {
                        IgBlock block = ig_template_block_at(2, b);
                        if (block.x == marker.x && block.y == marker.y - 1
                                && block.z == marker.z) {
                            chest = ig_rotate_state(
                                sd_state(block.id, block.meta), rotation);
                            break;
                        }
                    }
                    if ((chest >> 4) == 54) {
                        igloo->chest_x = mx;
                        igloo->chest_y = my - 1;
                        igloo->chest_z = mz;
                        igloo->chest_facing = chest & 15;
                        igloo->chest_loot_seed = jrand_long(random);
                        igloo->chest_placed = 1;
                    }
                }
            }
            for (int e = 0; e < ig_template_entity_count(2)
                    && igloo->entity_count < IG_ENTITY_MAX; ++e) {
                IgEntity entity = ig_template_entity_at(2, e);
                IgSpawn *spawn = &igloo->entities[igloo->entity_count++];
                double tx, tz;
                float rotated_yaw = entity.yaw;
                ig_transform_entity(rotation, entity.x, entity.z, &tx, &tz);
                spawn->x = bottom_x + tx;
                spawn->y = bottom_y + entity.y;
                spawn->z = bottom_z + tz;
                spawn->vx = entity.vx;
                spawn->vy = entity.vy;
                spawn->vz = entity.vz;
                if (rotation == IG_ROT_CW90)
                    rotated_yaw += 90.0f;
                else if (rotation == IG_ROT_CW180)
                    rotated_yaw += 180.0f;
                else if (rotation == IG_ROT_CCW90)
                    rotated_yaw += 270.0f;
                spawn->health = entity.health;
                spawn->yaw = entity.yaw + (entity.yaw - rotated_yaw);
                spawn->pitch = entity.pitch;
                spawn->conversion_time = entity.conversion_time;
                spawn->fire = entity.fire;
                spawn->air = entity.air;
                spawn->profession = entity.profession;
                spawn->persistence = entity.persistence;
                spawn->on_ground = entity.on_ground;
                spawn->kind = entity.kind;
            }
        }
    } else {
        int tx, tz, x, y, z;
        ig_transform_block(rotation, 3, 5, &tx, &tz);
        x = igloo->origin_x + tx;
        y = igloo->base_y;
        z = igloo->origin_z + tz;
        access->set(access->ctx, x, y, z, sd_state(80, 0));
    }
}

#endif
