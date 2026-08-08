/* Exact Minecraft 1.11.2 JunglePyramid structure-piece block placement. */
#ifndef MC_SCATTERED_JUNGLE_H
#define MC_SCATTERED_JUNGLE_H

#include "scattered_desert.h"

enum {
    SJ_SITE_CHEST = 1,
    SJ_SITE_DISPENSER = 2,
    SJ_SITE_MAX = 4
};

typedef struct {
    int kind, x, y, z, facing;
    i64 loot_seed;
} SjLootSite;

typedef struct {
    SdDesertPyramid base;
    SjLootSite sites[SJ_SITE_MAX];
    int site_count;
} SjJunglePyramid;

MC_HD static inline void sj_set(SdAccess *a, SjJunglePyramid *p,
                                int x, int y, int z, u16 state) {
    sd_set(a, &p->base, x, y, z, state);
}

MC_HD static inline void sj_air(SdAccess *a, SjJunglePyramid *p,
        int x0, int y0, int z0, int x1, int y1, int z1) {
    sd_fill(a, &p->base, x0, y0, z0, x1, y1, z1,
            sd_state(0, 0), sd_state(0, 0));
}

/* Stones.selectBlocks runs before the clipped setBlockState call, so every
 * coordinate consumes nextFloat even when this population chunk cannot write it. */
MC_HD static inline void sj_random_fill(SdAccess *a, SjJunglePyramid *p,
        JavaRandom *random, int x0, int y0, int z0, int x1, int y1, int z1) {
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            for (int z = z0; z <= z1; ++z)
                sj_set(a, p, x, y, z,
                    jrand_float(random) < 0.4f ? sd_state(4, 0)
                                               : sd_state(48, 0));
}

MC_HD static inline void sj_record_site(SjJunglePyramid *p, int kind,
        int x, int y, int z, int facing, i64 seed) {
    if (p->site_count >= SJ_SITE_MAX) return;
    SjLootSite *site = &p->sites[p->site_count++];
    site->kind = kind; site->x = x; site->y = y; site->z = z;
    site->facing = facing; site->loot_seed = seed;
}

MC_HD static inline void sj_chest(SdAccess *a, SjJunglePyramid *p,
        JavaRandom *random, int x, int y, int z) {
    int wx, wy, wz;
    sd_world_pos(&p->base, x, y, z, &wx, &wy, &wz);
    if (a->contains && !a->contains(a->ctx, wx, wy, wz)) return;
    if ((a->get(a->ctx, wx, wy, wz) >> 4) == 54) return;
    int facing = sd_chest_facing(a, wx, wy, wz);
    a->set(a->ctx, wx, wy, wz, sd_state(54, facing));
    sj_record_site(p, SJ_SITE_CHEST, wx, wy, wz, facing, jrand_long(random));
}

MC_HD static inline void sj_dispenser(SdAccess *a, SjJunglePyramid *p,
        JavaRandom *random, int x, int y, int z, int facing) {
    int wx, wy, wz;
    sd_world_pos(&p->base, x, y, z, &wx, &wy, &wz);
    if (a->contains && !a->contains(a->ctx, wx, wy, wz)) return;
    if ((a->get(a->ctx, wx, wy, wz) >> 4) == 23) return;
    u16 state = sd_transform_state(&p->base, sd_state(23, facing));
    a->set(a->ctx, wx, wy, wz, state);
    sj_record_site(p, SJ_SITE_DISPENSER, wx, wy, wz, state & 15,
                   jrand_long(random));
}

MC_HD MC_NOINLINE static void sj_jungle_generate(
        SdAccess *a, SjJunglePyramid *p, JavaRandom *random) {
    const u16 stairs_e = sd_state(67, 0), stairs_w = sd_state(67, 1);
    const u16 stairs_s = sd_state(67, 2), stairs_n = sd_state(67, 3);
    sj_random_fill(a,p,random,0,-4,0,11,0,14);
    sj_random_fill(a,p,random,2,1,2,9,2,2);
    sj_random_fill(a,p,random,2,1,12,9,2,12);
    sj_random_fill(a,p,random,2,1,3,2,2,11);
    sj_random_fill(a,p,random,9,1,3,9,2,11);
    sj_random_fill(a,p,random,1,3,1,10,6,1);
    sj_random_fill(a,p,random,1,3,13,10,6,13);
    sj_random_fill(a,p,random,1,3,2,1,6,12);
    sj_random_fill(a,p,random,10,3,2,10,6,12);
    sj_random_fill(a,p,random,2,3,2,9,3,12);
    sj_random_fill(a,p,random,2,6,2,9,6,12);
    sj_random_fill(a,p,random,3,7,3,8,7,11);
    sj_random_fill(a,p,random,4,8,4,7,8,10);
    sj_air(a,p,3,1,3,8,2,11); sj_air(a,p,4,3,6,7,3,9);
    sj_air(a,p,2,4,2,9,5,12); sj_air(a,p,4,6,5,7,6,9);
    sj_air(a,p,5,7,6,6,7,8); sj_air(a,p,5,1,2,6,2,2);
    sj_air(a,p,5,2,12,6,2,12); sj_air(a,p,5,5,1,6,5,1);
    sj_air(a,p,5,5,13,6,5,13);
    sj_set(a,p,1,5,5,sd_state(0,0)); sj_set(a,p,10,5,5,sd_state(0,0));
    sj_set(a,p,1,5,9,sd_state(0,0)); sj_set(a,p,10,5,9,sd_state(0,0));
    for (int z=0;z<=14;z+=14) {
        sj_random_fill(a,p,random,2,4,z,2,5,z);
        sj_random_fill(a,p,random,4,4,z,4,5,z);
        sj_random_fill(a,p,random,7,4,z,7,5,z);
        sj_random_fill(a,p,random,9,4,z,9,5,z);
    }
    sj_random_fill(a,p,random,5,6,0,6,6,0);
    for (int x=0;x<=11;x+=11) {
        for (int z=2;z<=12;z+=2)
            sj_random_fill(a,p,random,x,4,z,x,5,z);
        sj_random_fill(a,p,random,x,6,5,x,6,5);
        sj_random_fill(a,p,random,x,6,9,x,6,9);
    }
    sj_random_fill(a,p,random,2,7,2,2,9,2);
    sj_random_fill(a,p,random,9,7,2,9,9,2);
    sj_random_fill(a,p,random,2,7,12,2,9,12);
    sj_random_fill(a,p,random,9,7,12,9,9,12);
    sj_random_fill(a,p,random,4,9,4,4,9,4);
    sj_random_fill(a,p,random,7,9,4,7,9,4);
    sj_random_fill(a,p,random,4,9,10,4,9,10);
    sj_random_fill(a,p,random,7,9,10,7,9,10);
    sj_random_fill(a,p,random,5,9,7,6,9,7);
    sj_set(a,p,5,9,6,stairs_n); sj_set(a,p,6,9,6,stairs_n);
    sj_set(a,p,5,9,8,stairs_s); sj_set(a,p,6,9,8,stairs_s);
    for(int x=4;x<=7;++x) sj_set(a,p,x,0,0,stairs_n);
    sj_set(a,p,4,1,8,stairs_n); sj_set(a,p,4,2,9,stairs_n);
    sj_set(a,p,4,3,10,stairs_n); sj_set(a,p,7,1,8,stairs_n);
    sj_set(a,p,7,2,9,stairs_n); sj_set(a,p,7,3,10,stairs_n);
    sj_random_fill(a,p,random,4,1,9,4,1,9);
    sj_random_fill(a,p,random,7,1,9,7,1,9);
    sj_random_fill(a,p,random,4,1,10,7,2,10);
    sj_random_fill(a,p,random,5,4,5,6,4,5);
    sj_set(a,p,4,4,5,stairs_e); sj_set(a,p,7,4,5,stairs_w);
    for(int k=0;k<4;++k) {
        sj_set(a,p,5,-k,6+k,stairs_s); sj_set(a,p,6,-k,6+k,stairs_s);
        sj_air(a,p,5,-k,7+k,6,-k,9+k);
    }
    sj_air(a,p,1,-3,12,10,-1,13); sj_air(a,p,1,-3,1,3,-1,13);
    sj_air(a,p,1,-3,1,9,-1,5);
    for(int z=1;z<=13;z+=2) sj_random_fill(a,p,random,1,-3,z,1,-2,z);
    for(int z=2;z<=12;z+=2) sj_random_fill(a,p,random,1,-1,z,3,-1,z);
    sj_random_fill(a,p,random,2,-2,1,5,-2,1);
    sj_random_fill(a,p,random,7,-2,1,9,-2,1);
    sj_random_fill(a,p,random,6,-3,1,6,-3,1);
    sj_random_fill(a,p,random,6,-1,1,6,-1,1);
    sj_set(a,p,1,-3,8,sd_state(131,7)); sj_set(a,p,4,-3,8,sd_state(131,5));
    sj_set(a,p,2,-3,8,sd_state(132,4)); sj_set(a,p,3,-3,8,sd_state(132,4));
    for(int z=1;z<=7;++z) sj_set(a,p,5,-3,z,sd_state(55,0));
    sj_set(a,p,4,-3,1,sd_state(55,0)); sj_set(a,p,3,-3,1,sd_state(48,0));
    sj_dispenser(a,p,random,3,-2,1,SD_NORTH);
    sj_set(a,p,3,-2,2,sd_state(106,1));
    sj_set(a,p,7,-3,1,sd_state(131,6)); sj_set(a,p,7,-3,5,sd_state(131,4));
    sj_set(a,p,7,-3,2,sd_state(132,4)); sj_set(a,p,7,-3,3,sd_state(132,4));
    sj_set(a,p,7,-3,4,sd_state(132,4));
    sj_set(a,p,8,-3,6,sd_state(55,0)); sj_set(a,p,9,-3,6,sd_state(55,0));
    sj_set(a,p,9,-3,5,sd_state(55,0)); sj_set(a,p,9,-3,4,sd_state(48,0));
    sj_set(a,p,9,-2,4,sd_state(55,0));
    sj_dispenser(a,p,random,9,-2,3,SD_WEST);
    sj_set(a,p,8,-1,3,sd_state(106,8)); sj_set(a,p,8,-2,3,sd_state(106,8));
    sj_chest(a,p,random,8,-3,3);
    sj_set(a,p,9,-3,2,sd_state(48,0)); sj_set(a,p,8,-3,1,sd_state(48,0));
    sj_set(a,p,4,-3,5,sd_state(48,0)); sj_set(a,p,5,-2,5,sd_state(48,0));
    sj_set(a,p,5,-1,5,sd_state(48,0)); sj_set(a,p,6,-3,5,sd_state(48,0));
    sj_set(a,p,7,-2,5,sd_state(48,0)); sj_set(a,p,7,-1,5,sd_state(48,0));
    sj_set(a,p,8,-3,5,sd_state(48,0));
    sj_random_fill(a,p,random,9,-1,1,9,-1,5);
    sj_air(a,p,8,-3,8,10,-1,10);
    for(int x=8;x<=10;++x) sj_set(a,p,x,-2,11,sd_state(98,3));
    for(int x=8;x<=10;++x) sj_set(a,p,x,-2,12,sd_state(69,4));
    sj_random_fill(a,p,random,8,-3,8,8,-3,10);
    sj_random_fill(a,p,random,10,-3,8,10,-3,10);
    sj_set(a,p,10,-2,9,sd_state(48,0));
    sj_set(a,p,8,-2,9,sd_state(55,0)); sj_set(a,p,8,-2,10,sd_state(55,0));
    sj_set(a,p,10,-1,9,sd_state(55,0));
    sj_set(a,p,9,-2,8,sd_state(29,1));
    sj_set(a,p,10,-2,8,sd_state(29,4)); sj_set(a,p,10,-1,8,sd_state(29,4));
    sj_set(a,p,10,-2,10,sd_state(93,2));
    sj_chest(a,p,random,9,-3,10);
}

#endif
