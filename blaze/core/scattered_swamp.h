/* Exact Minecraft 1.11.2 SwampHut structure-piece block placement. */
#ifndef MC_SCATTERED_SWAMP_H
#define MC_SCATTERED_SWAMP_H

#include "scattered_desert.h"

typedef struct {
    SdDesertPyramid base;
    int pot_x, pot_y, pot_z;
    int pot_placed;
    int witch_x, witch_y, witch_z;
    int witch_placed;
} SsSwampHut;

MC_HD static inline void ss_set(SdAccess *a, SsSwampHut *p,
                                int x, int y, int z, u16 state) {
    sd_set(a, &p->base, x, y, z, state);
}

MC_HD static inline void ss_fill(SdAccess *a, SsSwampHut *p,
        int x0, int y0, int z0, int x1, int y1, int z1, u16 state) {
    sd_fill(a, &p->base, x0, y0, z0, x1, y1, z1, state, state);
}

MC_HD static inline void ss_replace_down(SdAccess *a, SsSwampHut *p,
                                         int x, int y, int z, u16 state) {
    int wx, wy, wz;
    sd_world_pos(&p->base, x, y, z, &wx, &wy, &wz);
    if (a->contains && !a->contains(a->ctx, wx, wy, wz)) return;
    while (wy > 1) {
        int id = a->get(a->ctx, wx, wy, wz) >> 4;
        if (id != 0 && id != 8 && id != 9 && id != 10 && id != 11)
            break;
        a->set(a->ctx, wx, wy, wz, state);
        --wy;
    }
}

MC_HD MC_NOINLINE static void ss_swamp_generate(
        SdAccess *a, SsSwampHut *p) {
    const u16 spruce = sd_state(5, 1);
    const u16 log = sd_state(17, 0);
    const u16 fence = sd_state(85, 0);
    const u16 stairs_n = sd_state(134, 3);
    const u16 stairs_e = sd_state(134, 0);
    const u16 stairs_w = sd_state(134, 1);
    const u16 stairs_s = sd_state(134, 2);
    ss_fill(a,p,1,1,1,5,1,7,spruce);
    ss_fill(a,p,1,4,2,5,4,7,spruce);
    ss_fill(a,p,2,1,0,4,1,0,spruce);
    ss_fill(a,p,2,2,2,3,3,2,spruce);
    ss_fill(a,p,1,2,3,1,3,6,spruce);
    ss_fill(a,p,5,2,3,5,3,6,spruce);
    ss_fill(a,p,2,2,7,4,3,7,spruce);
    ss_fill(a,p,1,0,2,1,3,2,log);
    ss_fill(a,p,5,0,2,5,3,2,log);
    ss_fill(a,p,1,0,7,1,3,7,log);
    ss_fill(a,p,5,0,7,5,3,7,log);
    ss_set(a,p,2,3,2,fence);
    ss_set(a,p,3,3,7,fence);
    ss_set(a,p,1,3,4,sd_state(0,0));
    ss_set(a,p,5,3,4,sd_state(0,0));
    ss_set(a,p,5,3,5,sd_state(0,0));
    ss_set(a,p,1,3,5,sd_state(140,0));
    {
        int wx, wy, wz;
        sd_world_pos(&p->base, 1, 3, 5, &wx, &wy, &wz);
        if (!a->contains || a->contains(a->ctx, wx, wy, wz)) {
            p->pot_x = wx; p->pot_y = wy; p->pot_z = wz;
            p->pot_placed = 1;
        }
    }
    ss_set(a,p,3,2,6,sd_state(58,0));
    ss_set(a,p,4,2,6,sd_state(118,0));
    ss_set(a,p,1,2,1,fence);
    ss_set(a,p,5,2,1,fence);
    ss_fill(a,p,0,4,1,6,4,1,stairs_n);
    ss_fill(a,p,0,4,2,0,4,7,stairs_e);
    ss_fill(a,p,6,4,2,6,4,7,stairs_w);
    ss_fill(a,p,0,4,8,6,4,8,stairs_s);
    for (int z = 2; z <= 7; z += 5)
        for (int x = 1; x <= 5; x += 4)
            ss_replace_down(a,p,x,-1,z,log);
    {
        int wx, wy, wz;
        sd_world_pos(&p->base, 2, 2, 5, &wx, &wy, &wz);
        if (!a->contains || a->contains(a->ctx, wx, wy, wz)) {
            p->witch_x = wx; p->witch_y = wy; p->witch_z = wz;
            p->witch_placed = 1;
        }
    }
}

#endif
