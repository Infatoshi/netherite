/* tile_entity_furnace: smelt tick for iron ore + coal fuel (TileEntityFurnace.update subset).
 * One recipe: iron ore (block id 15) -> iron ingot (265). Coal burn 1600 ticks, cook 200 ticks.
 * Deterministic; no NBT/containers. CPU==CUDA. */
#ifndef MC_TILE_ENTITY_FURNACE_H
#define MC_TILE_ENTITY_FURNACE_H

#include "mc.h"

enum {
    TE_IRON_ORE   = 15,
    TE_COAL       = 263,
    TE_IRON_INGOT = 265,
    TE_COOK_TICKS = 200,
    TE_COAL_BURN  = 1600
};

typedef struct {
    i32 slot0_item, slot0_count;
    i32 slot1_item, slot1_count;
    i32 slot2_item, slot2_count;
    i32 burn_time;
    i32 cook_time;
    i32 total_cook;
} TeFurnace;

MC_HD static inline int te_is_fuel(i32 item) { return item == TE_COAL; }
MC_HD static inline int te_fuel_burn(i32 item) { return item == TE_COAL ? TE_COAL_BURN : 0; }

MC_HD static inline int te_can_smelt(const TeFurnace *f) {
    if (f->slot0_item != TE_IRON_ORE || f->slot0_count <= 0) return 0;
    if (f->slot2_item == 0 || f->slot2_count <= 0) return 1;
    if (f->slot2_item == TE_IRON_INGOT && f->slot2_count < 64) return 1;
    return 0;
}

MC_HD static inline void te_smelt(TeFurnace *f) {
    if (!te_can_smelt(f)) return;
    f->slot0_count--;
    if (f->slot0_count <= 0) { f->slot0_item = 0; f->slot0_count = 0; }
    if (f->slot2_item == 0 || f->slot2_count <= 0) {
        f->slot2_item = TE_IRON_INGOT;
        f->slot2_count = 1;
    } else {
        f->slot2_count++;
    }
}

MC_HD static inline void te_init(TeFurnace *f) {
    f->slot0_item = TE_IRON_ORE; f->slot0_count = 1;
    f->slot1_item = TE_COAL; f->slot1_count = 1;
    f->slot2_item = 0; f->slot2_count = 0;
    f->burn_time = 0; f->cook_time = 0; f->total_cook = TE_COOK_TICKS;
}

MC_HD static inline void te_tick(TeFurnace *f) {
    if (f->burn_time > 0) f->burn_time--;
    if (f->burn_time > 0 || (f->slot1_count > 0 && f->slot0_count > 0)) {
        if (f->burn_time == 0 && te_can_smelt(f) && te_is_fuel(f->slot1_item)) {
            f->burn_time = te_fuel_burn(f->slot1_item);
            f->slot1_count--;
            if (f->slot1_count <= 0) { f->slot1_item = 0; f->slot1_count = 0; }
        }
        if (f->burn_time > 0 && te_can_smelt(f)) {
            f->cook_time++;
            if (f->cook_time >= f->total_cook) {
                f->cook_time = 0;
                te_smelt(f);
            }
        } else {
            /* TileEntityFurnace.update: unconditional `else { cookTime = 0; }` when not
             * (burning && canSmelt). (Behaviorally equal to the prior burn_time==0 guard for
             * a closed furnace, since canSmelt cannot flip false mid-cook, but kept verbatim.) */
            f->cook_time = 0;
        }
    } else if (f->cook_time > 0) {
        f->cook_time -= 2;
        if (f->cook_time < 0) f->cook_time = 0;
    }
}

#define TE_NDUMP 5
#define TE_OUT (TE_NDUMP * 5)

MC_HD static inline void te_run_dump(TeFurnace *f, u64 *out) {
    te_init(f);
    static const int marks[TE_NDUMP] = {0, 50, 100, 200, 400};
    int cur = 0, o = 0;
    for (int m = 0; m < TE_NDUMP; ++m) {
        while (cur < marks[m]) { te_tick(f); cur++; }
        out[o++] = (u64)(u32)f->slot0_count;
        out[o++] = (u64)(u32)f->slot1_count;
        out[o++] = (u64)(u32)f->slot2_count;
        out[o++] = (u64)(u32)f->burn_time;
        out[o++] = (u64)(u32)f->cook_time;
    }
}

#endif /* MC_TILE_ENTITY_FURNACE_H */
