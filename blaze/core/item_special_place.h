#ifndef MC_ITEM_SPECIAL_PLACE_H
#define MC_ITEM_SPECIAL_PLACE_H

#include "mc.h"

typedef struct {
    int accepted;
    int block;
    int meta;
    int tile_kind; /* 1 sign, 2 banner, 3 skull */
    int tile_aux;  /* banner base or skull type*16+rotation */
} IspResult;

/* Pure half of ItemBed/ItemSign/ItemSkull/ItemBanner.onItemUse. World
 * replaceability and support checks remain with the caller. EnumFacing uses
 * the 1.11.2 D,U,N,S,W,E ordinal. */
MC_HD static inline IspResult isp_plan(
        int item, int item_meta, int face, int yaw_quad) {
    IspResult out = {0, 0, 0, 0, 0};
    int rotation = ((yaw_quad & 3) * 4 + 8) & 15;
    if (face < 0 || face > 5) return out;
    if (item == 355) {
        if (face != 1) return out;
        out.accepted = 1;
        out.block = 26;
        out.meta = yaw_quad & 3;
        return out;
    }
    if (item != 323 && item != 397 && item != 425) return out;
    if (face == 0) return out;
    out.accepted = 1;
    if (item == 323) {
        out.block = face == 1 ? 63 : 68;
        out.meta = face == 1 ? rotation : face;
        out.tile_kind = 1;
    } else if (item == 425) {
        out.block = face == 1 ? 176 : 177;
        out.meta = face == 1 ? rotation : face;
        out.tile_kind = 2;
        out.tile_aux = item_meta & 15;
    } else if (item_meta >= 0 && item_meta <= 5) {
        int skull_rotation = face == 1 ? (yaw_quad & 3) * 4 : 0;
        out.block = 144;
        out.meta = face;
        out.tile_kind = 3;
        out.tile_aux = item_meta * 16 + skull_rotation;
    } else {
        out.accepted = 0;
    }
    return out;
}

#endif
