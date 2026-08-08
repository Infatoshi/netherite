/* Exact Minecraft 1.11.2 DesertPyramid structure-piece block placement. */
#ifndef MC_SCATTERED_DESERT_H
#define MC_SCATTERED_DESERT_H

#include "mc.h"
#include "mc_rng.h"

enum {
    SD_NORTH = 2, SD_SOUTH = 3, SD_WEST = 4, SD_EAST = 5,
    SD_CHEST_MAX = 4
};

typedef struct {
    void *ctx;
    u16 (*get)(void *ctx, int x, int y, int z);
    void (*set)(void *ctx, int x, int y, int z, u16 packed);
    int (*contains)(void *ctx, int x, int y, int z);
} SdAccess;

typedef struct {
    int x, y, z;
    int facing;
    i64 loot_seed;
} SdChest;

typedef struct {
    int origin_x, base_y, origin_z, facing;
    /* Local structure depth. Zero preserves the 21-block desert-pyramid
     * default; narrower scattered pieces use their actual local Z size. */
    int size_z;
    SdChest chests[SD_CHEST_MAX];
    int chest_count;
} SdDesertPyramid;

MC_HD static inline u16 sd_state(int id, int meta) {
    return (u16)((id << 4) | (meta & 15));
}

MC_HD static inline void sd_world_pos(
        const SdDesertPyramid *p, int x, int y, int z,
        int *wx, int *wy, int *wz) {
    *wy = p->base_y + y;
    int zmax = (p->size_z ? p->size_z : 21) - 1;
    if (p->facing == SD_NORTH) {
        *wx = p->origin_x + x; *wz = p->origin_z + zmax - z;
    } else if (p->facing == SD_SOUTH) {
        *wx = p->origin_x + x; *wz = p->origin_z + z;
    } else if (p->facing == SD_WEST) {
        *wx = p->origin_x + zmax - z; *wz = p->origin_z + x;
    } else {
        *wx = p->origin_x + z; *wz = p->origin_z + x;
    }
}

MC_HD static inline int sd_mirror_rotate_facing(int facing, int base) {
    if (base == SD_SOUTH || base == SD_WEST) {
        if (facing == SD_NORTH) facing = SD_SOUTH;
        else if (facing == SD_SOUTH) facing = SD_NORTH;
    }
    if (base == SD_WEST || base == SD_EAST) {
        if (facing == SD_NORTH) facing = SD_EAST;
        else if (facing == SD_EAST) facing = SD_SOUTH;
        else if (facing == SD_SOUTH) facing = SD_WEST;
        else if (facing == SD_WEST) facing = SD_NORTH;
    }
    return facing;
}

MC_HD static inline u16 sd_transform_state(
        const SdDesertPyramid *p, u16 packed) {
    int id = packed >> 4, meta = packed & 15;
    if (id == 53 || id == 67 || id == 108 || id == 109 || id == 114
            || id == 128 || id == 134 || id == 135 || id == 136
            || id == 156 || id == 163 || id == 164 || id == 180
            || id == 203) {
        int facing = 5 - (meta & 3);
        facing = sd_mirror_rotate_facing(facing, p->facing);
        meta = (meta & 4) | (5 - facing);
    } else if (id == 23 || id == 29 || id == 33) {
        if ((meta & 7) >= SD_NORTH && (meta & 7) <= SD_EAST) {
            int facing = sd_mirror_rotate_facing(meta & 7, p->facing);
            meta = (meta & 8) | facing;
        }
    } else if (id == 93 || id == 94 || id == 131) {
        static const int by_horizontal[4] = {
            SD_SOUTH, SD_WEST, SD_NORTH, SD_EAST
        };
        int facing = sd_mirror_rotate_facing(by_horizontal[meta & 3], p->facing);
        int horizontal = facing == SD_SOUTH ? 0 : facing == SD_WEST ? 1
                       : facing == SD_NORTH ? 2 : 3;
        meta = (meta & 12) | horizontal;
    } else if (id == 69 && meta >= 1 && meta <= 4) {
        int facing = meta == 1 ? SD_EAST : meta == 2 ? SD_WEST
                   : meta == 3 ? SD_SOUTH : SD_NORTH;
        facing = sd_mirror_rotate_facing(facing, p->facing);
        meta = facing == SD_EAST ? 1 : facing == SD_WEST ? 2
             : facing == SD_SOUTH ? 3 : 4;
    } else if (id == 106) {
        int transformed = 0;
        const int bits[4] = {4, 1, 2, 8};
        const int faces[4] = {SD_NORTH, SD_SOUTH, SD_WEST, SD_EAST};
        for (int i = 0; i < 4; ++i)
            if (meta & bits[i]) {
                int facing = sd_mirror_rotate_facing(faces[i], p->facing);
                transformed |= facing == SD_NORTH ? 4 : facing == SD_SOUTH ? 1
                             : facing == SD_WEST ? 2 : 8;
            }
        meta = transformed;
    }
    return sd_state(id, meta);
}

MC_HD static inline void sd_set(
        SdAccess *a, SdDesertPyramid *p,
        int x, int y, int z, u16 packed) {
    int wx, wy, wz;
    sd_world_pos(p, x, y, z, &wx, &wy, &wz);
    if (a->contains && !a->contains(a->ctx, wx, wy, wz)) return;
    a->set(a->ctx, wx, wy, wz, sd_transform_state(p, packed));
}

MC_HD static inline void sd_fill(
        SdAccess *a, SdDesertPyramid *p,
        int x0, int y0, int z0, int x1, int y1, int z1,
        u16 boundary, u16 inside) {
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            for (int z = z0; z <= z1; ++z)
                sd_set(a, p, x, y, z,
                    y != y0 && y != y1 && x != x0 && x != x1
                        && z != z0 && z != z1 ? inside : boundary);
}

MC_HD static inline int sd_normal_cube(u16 packed) {
    int id = packed >> 4;
    return id != 0 && id != 8 && id != 9 && id != 10 && id != 11
        && id != 44 && id != 50 && id != 53 && id != 54 && id != 55
        && id != 67 && id != 69 && id != 70 && id != 93 && id != 94
        && id != 106 && id != 128 && id != 131 && id != 132;
}

MC_HD static inline int sd_chest_facing(
        SdAccess *a, int x, int y, int z) {
    int north = sd_normal_cube(a->get(a->ctx, x, y, z - 1));
    int south = sd_normal_cube(a->get(a->ctx, x, y, z + 1));
    int west = sd_normal_cube(a->get(a->ctx, x - 1, y, z));
    int east = sd_normal_cube(a->get(a->ctx, x + 1, y, z));
    int facing = SD_NORTH;
    if (north && !south) facing = SD_SOUTH;
    if (south && !north) facing = SD_NORTH;
    if (west && !east) facing = SD_EAST;
    if (east && !west) facing = SD_WEST;
    return facing;
}

MC_HD static inline void sd_chest(
        SdAccess *a, SdDesertPyramid *p, JavaRandom *random,
        int x, int y, int z) {
    int wx, wy, wz, facing;
    sd_world_pos(p, x, y, z, &wx, &wy, &wz);
    if (a->contains && !a->contains(a->ctx, wx, wy, wz)) return;
    if ((a->get(a->ctx, wx, wy, wz) >> 4) == 54) return;
    facing = sd_chest_facing(a, wx, wy, wz);
    a->set(a->ctx, wx, wy, wz, sd_state(54, facing));
    if (p->chest_count < SD_CHEST_MAX) {
        SdChest *chest = &p->chests[p->chest_count++];
        chest->x = wx; chest->y = wy; chest->z = wz;
        chest->facing = facing;
        chest->loot_seed = jrand_long(random);
    }
}

#define SD_SANDSTONE sd_state(24, 0)
#define SD_SMOOTH sd_state(24, 2)
#define SD_CHISELED sd_state(24, 1)
#define SD_AIR sd_state(0, 0)
#define SD_ORANGE sd_state(159, 1)

MC_HD MC_NOINLINE static void sd_desert_generate(
        SdAccess *a, SdDesertPyramid *p, JavaRandom *random) {
    const u16 stairs_n = sd_state(128, 3), stairs_s = sd_state(128, 2);
    const u16 stairs_e = sd_state(128, 0), stairs_w = sd_state(128, 1);
    sd_fill(a,p,0,-4,0,20,0,20,SD_SANDSTONE,SD_SANDSTONE);
    for (int i=1;i<=9;++i) {
        sd_fill(a,p,i,i,i,20-i,i,20-i,SD_SANDSTONE,SD_SANDSTONE);
        sd_fill(a,p,i+1,i,i+1,19-i,i,19-i,SD_AIR,SD_AIR);
    }
    for (int x=0;x<21;++x) for (int z=0;z<21;++z) {
        int wx,wy,wz;
        sd_world_pos(p,x,-5,z,&wx,&wy,&wz);
        if (a->contains && !a->contains(a->ctx,wx,wy,wz)) continue;
        while (wy>1 && ((a->get(a->ctx,wx,wy,wz)>>4)==0
                || (a->get(a->ctx,wx,wy,wz)>>4)==8
                || (a->get(a->ctx,wx,wy,wz)>>4)==9)) {
            a->set(a->ctx,wx,wy,wz,SD_SANDSTONE); --wy;
        }
    }
    sd_fill(a,p,0,0,0,4,9,4,SD_SANDSTONE,SD_AIR);
    sd_fill(a,p,1,10,1,3,10,3,SD_SANDSTONE,SD_SANDSTONE);
    sd_set(a,p,2,10,0,stairs_n); sd_set(a,p,2,10,4,stairs_s);
    sd_set(a,p,0,10,2,stairs_e); sd_set(a,p,4,10,2,stairs_w);
    sd_fill(a,p,16,0,0,20,9,4,SD_SANDSTONE,SD_AIR);
    sd_fill(a,p,17,10,1,19,10,3,SD_SANDSTONE,SD_SANDSTONE);
    sd_set(a,p,18,10,0,stairs_n); sd_set(a,p,18,10,4,stairs_s);
    sd_set(a,p,16,10,2,stairs_e); sd_set(a,p,20,10,2,stairs_w);
    sd_fill(a,p,8,0,0,12,4,4,SD_SANDSTONE,SD_AIR);
    sd_fill(a,p,9,1,0,11,3,4,SD_AIR,SD_AIR);
    for(int y=1;y<=3;++y) sd_set(a,p,9,y,1,SD_SMOOTH);
    sd_set(a,p,10,3,1,SD_SMOOTH);
    for(int y=1;y<=3;++y) sd_set(a,p,11,y,1,SD_SMOOTH);
    sd_fill(a,p,4,1,1,8,3,3,SD_SANDSTONE,SD_AIR);
    sd_fill(a,p,4,1,2,8,2,2,SD_AIR,SD_AIR);
    sd_fill(a,p,12,1,1,16,3,3,SD_SANDSTONE,SD_AIR);
    sd_fill(a,p,12,1,2,16,2,2,SD_AIR,SD_AIR);
    sd_fill(a,p,5,4,5,15,4,15,SD_SANDSTONE,SD_SANDSTONE);
    sd_fill(a,p,9,4,9,11,4,11,SD_AIR,SD_AIR);
    for(int x=8;x<=12;x+=4) for(int z=8;z<=12;z+=4)
        sd_fill(a,p,x,1,z,x,3,z,SD_SMOOTH,SD_SMOOTH);
    sd_fill(a,p,1,1,5,4,4,11,SD_SANDSTONE,SD_SANDSTONE);
    sd_fill(a,p,16,1,5,19,4,11,SD_SANDSTONE,SD_SANDSTONE);
    sd_fill(a,p,6,7,9,6,7,11,SD_SANDSTONE,SD_SANDSTONE);
    sd_fill(a,p,14,7,9,14,7,11,SD_SANDSTONE,SD_SANDSTONE);
    sd_fill(a,p,5,5,9,5,7,11,SD_SMOOTH,SD_SMOOTH);
    sd_fill(a,p,15,5,9,15,7,11,SD_SMOOTH,SD_SMOOTH);
    sd_set(a,p,5,5,10,SD_AIR); sd_set(a,p,5,6,10,SD_AIR);
    sd_set(a,p,6,6,10,SD_AIR); sd_set(a,p,15,5,10,SD_AIR);
    sd_set(a,p,15,6,10,SD_AIR); sd_set(a,p,14,6,10,SD_AIR);
    sd_fill(a,p,2,4,4,2,6,4,SD_AIR,SD_AIR);
    sd_fill(a,p,18,4,4,18,6,4,SD_AIR,SD_AIR);
    sd_set(a,p,2,4,5,stairs_n); sd_set(a,p,2,3,4,stairs_n);
    sd_set(a,p,18,4,5,stairs_n); sd_set(a,p,18,3,4,stairs_n);
    sd_fill(a,p,1,1,3,2,2,3,SD_SANDSTONE,SD_SANDSTONE);
    sd_fill(a,p,18,1,3,19,2,3,SD_SANDSTONE,SD_SANDSTONE);
    sd_set(a,p,1,1,2,SD_SANDSTONE); sd_set(a,p,19,1,2,SD_SANDSTONE);
    sd_set(a,p,1,2,2,sd_state(44,1)); sd_set(a,p,19,2,2,sd_state(44,1));
    sd_set(a,p,2,1,2,stairs_w); sd_set(a,p,18,1,2,stairs_e);
    sd_fill(a,p,4,3,5,4,3,18,SD_SANDSTONE,SD_SANDSTONE);
    sd_fill(a,p,16,3,5,16,3,17,SD_SANDSTONE,SD_SANDSTONE);
    sd_fill(a,p,3,1,5,4,2,16,SD_AIR,SD_AIR);
    sd_fill(a,p,15,1,5,16,2,16,SD_AIR,SD_AIR);
    for(int z=5;z<=17;z+=2) {
        sd_set(a,p,4,1,z,SD_SMOOTH); sd_set(a,p,4,2,z,SD_CHISELED);
        sd_set(a,p,16,1,z,SD_SMOOTH); sd_set(a,p,16,2,z,SD_CHISELED);
    }
    const int orange[][2]={{10,7},{10,8},{9,9},{11,9},{8,10},{12,10},
        {7,10},{13,10},{9,11},{11,11},{10,12},{10,13}};
    for(int i=0;i<12;++i) sd_set(a,p,orange[i][0],0,orange[i][1],SD_ORANGE);
    sd_set(a,p,10,0,10,sd_state(159,11));
    for(int x=0;x<=20;x+=20) {
        sd_set(a,p,x,2,1,SD_SMOOTH); sd_set(a,p,x,2,2,SD_ORANGE);
        sd_set(a,p,x,2,3,SD_SMOOTH); sd_set(a,p,x,3,1,SD_SMOOTH);
        sd_set(a,p,x,3,2,SD_ORANGE); sd_set(a,p,x,3,3,SD_SMOOTH);
        sd_set(a,p,x,4,1,SD_ORANGE); sd_set(a,p,x,4,2,SD_CHISELED);
        sd_set(a,p,x,4,3,SD_ORANGE); sd_set(a,p,x,5,1,SD_SMOOTH);
        sd_set(a,p,x,5,2,SD_ORANGE); sd_set(a,p,x,5,3,SD_SMOOTH);
        sd_set(a,p,x,6,1,SD_ORANGE); sd_set(a,p,x,6,2,SD_CHISELED);
        sd_set(a,p,x,6,3,SD_ORANGE);
        for(int z=1;z<=3;++z) sd_set(a,p,x,7,z,SD_ORANGE);
        for(int z=1;z<=3;++z) sd_set(a,p,x,8,z,SD_SMOOTH);
    }
    for(int x=2;x<=18;x+=16) {
        sd_set(a,p,x-1,2,0,SD_SMOOTH); sd_set(a,p,x,2,0,SD_ORANGE);
        sd_set(a,p,x+1,2,0,SD_SMOOTH); sd_set(a,p,x-1,3,0,SD_SMOOTH);
        sd_set(a,p,x,3,0,SD_ORANGE); sd_set(a,p,x+1,3,0,SD_SMOOTH);
        sd_set(a,p,x-1,4,0,SD_ORANGE); sd_set(a,p,x,4,0,SD_CHISELED);
        sd_set(a,p,x+1,4,0,SD_ORANGE); sd_set(a,p,x-1,5,0,SD_SMOOTH);
        sd_set(a,p,x,5,0,SD_ORANGE); sd_set(a,p,x+1,5,0,SD_SMOOTH);
        sd_set(a,p,x-1,6,0,SD_ORANGE); sd_set(a,p,x,6,0,SD_CHISELED);
        sd_set(a,p,x+1,6,0,SD_ORANGE);
        for(int dx=-1;dx<=1;++dx) sd_set(a,p,x+dx,7,0,SD_ORANGE);
        for(int dx=-1;dx<=1;++dx) sd_set(a,p,x+dx,8,0,SD_SMOOTH);
    }
    sd_fill(a,p,8,4,0,12,6,0,SD_SMOOTH,SD_SMOOTH);
    sd_set(a,p,8,6,0,SD_AIR); sd_set(a,p,12,6,0,SD_AIR);
    sd_set(a,p,9,5,0,SD_ORANGE); sd_set(a,p,10,5,0,SD_CHISELED);
    sd_set(a,p,11,5,0,SD_ORANGE);
    sd_fill(a,p,8,-14,8,12,-11,12,SD_SMOOTH,SD_SMOOTH);
    sd_fill(a,p,8,-10,8,12,-10,12,SD_CHISELED,SD_CHISELED);
    sd_fill(a,p,8,-9,8,12,-9,12,SD_SMOOTH,SD_SMOOTH);
    sd_fill(a,p,8,-8,8,12,-1,12,SD_SANDSTONE,SD_SANDSTONE);
    sd_fill(a,p,9,-11,9,11,-1,11,SD_AIR,SD_AIR);
    sd_set(a,p,10,-11,10,sd_state(70,0));
    sd_fill(a,p,9,-13,9,11,-13,11,sd_state(46,0),SD_AIR);
    sd_set(a,p,8,-11,10,SD_AIR); sd_set(a,p,8,-10,10,SD_AIR);
    sd_set(a,p,7,-10,10,SD_CHISELED); sd_set(a,p,7,-11,10,SD_SMOOTH);
    sd_set(a,p,12,-11,10,SD_AIR); sd_set(a,p,12,-10,10,SD_AIR);
    sd_set(a,p,13,-10,10,SD_CHISELED); sd_set(a,p,13,-11,10,SD_SMOOTH);
    sd_set(a,p,10,-11,8,SD_AIR); sd_set(a,p,10,-10,8,SD_AIR);
    sd_set(a,p,10,-10,7,SD_CHISELED); sd_set(a,p,10,-11,7,SD_SMOOTH);
    sd_set(a,p,10,-11,12,SD_AIR); sd_set(a,p,10,-10,12,SD_AIR);
    sd_set(a,p,10,-10,13,SD_CHISELED); sd_set(a,p,10,-11,13,SD_SMOOTH);
    sd_chest(a,p,random,10,-11,8);
    sd_chest(a,p,random,12,-11,10);
    sd_chest(a,p,random,10,-11,12);
    sd_chest(a,p,random,8,-11,10);
}

#undef SD_SANDSTONE
#undef SD_SMOOTH
#undef SD_CHISELED
#undef SD_AIR
#undef SD_ORANGE

#endif
