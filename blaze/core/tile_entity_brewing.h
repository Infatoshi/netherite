/* tile_entity_brewing: one water+nether_wart -> awkward brew tick loop
 * (TileEntityBrewingStand.update subset). Blaze powder fuel (20 uses/charge).
 * Deterministic; no NBT/containers. CPU==CUDA. */
#ifndef MC_TILE_ENTITY_BREWING_H
#define MC_TILE_ENTITY_BREWING_H

#include "mc.h"

enum {
    TB_POTION       = 373,
    TB_NETHER_WART  = 372,
    TB_BLAZE_POWDER = 377,
    TB_BREW_TICKS   = 400,
    TB_FUEL_CHARGE  = 20,
    /* Potion type encoded in meta (NBT "Potion" string simplified). */
    TB_PT_WATER   = 1,
    TB_PT_AWKWARD = 2
};

typedef struct {
    i32 slot0_item, slot0_count, slot0_meta;
    i32 slot1_item, slot1_count, slot1_meta;
    i32 slot2_item, slot2_count, slot2_meta;
    i32 slot3_item, slot3_count;
    i32 slot4_item, slot4_count;
    i32 brew_time;
    i32 fuel;
    i32 ingredient_id;
} TeBrewing;

MC_HD static inline int tb_is_potion_input(i32 item, i32 count) {
    return item == TB_POTION && count == 1;
}

MC_HD static inline int tb_is_reagent(i32 item) {
    return item == TB_NETHER_WART;
}

MC_HD static inline int tb_has_conversion(i32 meta, i32 reagent) {
    return meta == TB_PT_WATER && reagent == TB_NETHER_WART;
}

MC_HD static inline int tb_can_brew(const TeBrewing *b) {
    if (b->slot3_count <= 0 || !tb_is_reagent(b->slot3_item)) return 0;
    if (tb_is_potion_input(b->slot0_item, b->slot0_count)
        && tb_has_conversion(b->slot0_meta, b->slot3_item)) return 1;
    if (tb_is_potion_input(b->slot1_item, b->slot1_count)
        && tb_has_conversion(b->slot1_meta, b->slot3_item)) return 1;
    if (tb_is_potion_input(b->slot2_item, b->slot2_count)
        && tb_has_conversion(b->slot2_meta, b->slot3_item)) return 1;
    return 0;
}

MC_HD static inline void tb_apply_brew(TeBrewing *b) {
    if (tb_is_potion_input(b->slot0_item, b->slot0_count)
        && tb_has_conversion(b->slot0_meta, b->slot3_item)) {
        b->slot0_meta = TB_PT_AWKWARD;
    }
    if (tb_is_potion_input(b->slot1_item, b->slot1_count)
        && tb_has_conversion(b->slot1_meta, b->slot3_item)) {
        b->slot1_meta = TB_PT_AWKWARD;
    }
    if (tb_is_potion_input(b->slot2_item, b->slot2_count)
        && tb_has_conversion(b->slot2_meta, b->slot3_item)) {
        b->slot2_meta = TB_PT_AWKWARD;
    }
    b->slot3_count--;
    if (b->slot3_count <= 0) { b->slot3_item = 0; b->slot3_count = 0; }
}

MC_HD static inline void tb_init(TeBrewing *b) {
    b->slot0_item = TB_POTION; b->slot0_count = 1; b->slot0_meta = TB_PT_WATER;
    b->slot1_item = 0; b->slot1_count = 0; b->slot1_meta = 0;
    b->slot2_item = 0; b->slot2_count = 0; b->slot2_meta = 0;
    b->slot3_item = TB_NETHER_WART; b->slot3_count = 1;
    b->slot4_item = TB_BLAZE_POWDER; b->slot4_count = 1;
    b->brew_time = 0;
    b->fuel = 0;
    b->ingredient_id = 0;
}

MC_HD static inline void tb_tick(TeBrewing *b) {
    if (b->fuel <= 0 && b->slot4_item == TB_BLAZE_POWDER && b->slot4_count > 0) {
        b->fuel = TB_FUEL_CHARGE;
        b->slot4_count--;
        if (b->slot4_count <= 0) { b->slot4_item = 0; b->slot4_count = 0; }
    }

    if (b->brew_time > 0) {
        b->brew_time--;
        if (b->brew_time == 0 && tb_can_brew(b)) {
            tb_apply_brew(b);
        } else if (!tb_can_brew(b)) {
            b->brew_time = 0;
        } else if (b->ingredient_id != b->slot3_item) {
            b->brew_time = 0;
        }
    } else if (tb_can_brew(b) && b->fuel > 0) {
        b->fuel--;
        b->brew_time = TB_BREW_TICKS;
        b->ingredient_id = b->slot3_item;
    }
}

#define TB_NDUMP 5
#define TB_OUT (TB_NDUMP * 5)

MC_HD static inline void tb_run_dump(TeBrewing *b, u64 *out) {
    tb_init(b);
    static const int marks[TB_NDUMP] = {0, 50, 100, 200, 450};
    int cur = 0, o = 0;
    for (int m = 0; m < TB_NDUMP; ++m) {
        while (cur < marks[m]) { tb_tick(b); cur++; }
        out[o++] = (u64)(u32)b->slot0_meta;
        out[o++] = (u64)(u32)b->slot3_count;
        out[o++] = (u64)(u32)b->slot4_count;
        out[o++] = (u64)(u32)b->brew_time;
        out[o++] = (u64)(u32)b->fuel;
    }
}

#endif /* MC_TILE_ENTITY_BREWING_H */
