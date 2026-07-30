/* furnace_full_tick: TileEntityFurnace.update over N ticks with full KEEP smelt/fuel tables.
 *
 * Composes READ-ONLY smelting_recipes (getSmeltingResult, getItemBurnTime) with verbatim
 * TileEntityFurnace.update() server logic (burn countdown, fuel consume, cook progress,
 * smelt output merge, cookTime decay when unlit). Per-tick dump: input/fuel/output counts,
 * burn_time, cook_time.
 *
 * Harness: seed picks cookable input + fuel from sr batteries and stack depths; default
 * FFT_NUM_TICKS=400. CUT: world.isRemote gate, BlockFurnace.setState, lava-bucket container
 * item, sponge+bucket special case, markDirty/NBT. CPU==CUDA. */
#ifndef MC_FURNACE_FULL_TICK_H
#define MC_FURNACE_FULL_TICK_H

#include "mc.h"
#include "smelting_recipes.h"
#include "tile_entity_furnace.h"

#define FFT_STACK_LIMIT 64

#define FFT_NUM_TICKS    400
#define FFT_DUMP_FIELDS  5

typedef struct {
    SRStack slot0;
    SRStack slot1;
    SRStack slot2;
    i32 burn_time;
    i32 current_burn_time;
    i32 cook_time;
    i32 total_cook;
    SRRecipe recipes[SR_NRECIPES];
    int nrecipes;
} FftFurnace;

MC_HD static inline int fft_is_empty(const SRStack *s) {
    return sr_isEmpty(*s);
}

MC_HD static inline int fft_is_burning(const FftFurnace *f) {
    return f->burn_time > 0;
}

MC_HD static inline i32 fft_get_cook_time(const SRStack *in) {
    (void)in;
    return TE_COOK_TICKS;
}

MC_HD static inline int fft_can_smelt(const FftFurnace *f) {
    SRStack result;
    i32 max_out;
    if (fft_is_empty(&f->slot0)) return 0;
    result = sr_getSmeltingResult(f->recipes, f->nrecipes, f->slot0);
    if (result.item == (i32)0xffffffff || result.count <= 0) return 0;
    if (fft_is_empty(&f->slot2)) return 1;
    if (f->slot2.item != result.item || f->slot2.meta != result.meta) return 0;
    max_out = FFT_STACK_LIMIT;
    return f->slot2.count + result.count <= max_out;
}

MC_HD static inline void fft_smelt(FftFurnace *f) {
    SRStack result;
    if (!fft_can_smelt(f)) return;
    result = sr_getSmeltingResult(f->recipes, f->nrecipes, f->slot0);
    if (fft_is_empty(&f->slot2))
        f->slot2 = result;
    else if (f->slot2.item == result.item && f->slot2.meta == result.meta)
        f->slot2.count += result.count;
    f->slot0.count--;
    if (f->slot0.count <= 0) f->slot0 = sr_empty();
}

MC_HD static inline void fft_init(FftFurnace *f, u64 seed) {
    static const i32 cookable[17] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    static const i32 fuels[6] = {0, 1, 2, 3, 4, 5};
    SRStack smelt_in[SR_NSMELT];
    SRStack fuel_in[SR_NFUEL];
    i32 ci, fi, in_cnt, fu_cnt;

    f->nrecipes = sr_build(f->recipes);
    sr_smelt_battery(smelt_in);
    sr_fuel_battery(fuel_in);

    ci = cookable[seed % 17];
    fi = fuels[(seed / 17ULL) % 6ULL];
    in_cnt = 1 + (i32)((seed / 102ULL) % 8ULL);
    fu_cnt = 1 + (i32)((seed / 816ULL) % 4ULL);

    f->slot0 = smelt_in[ci];
    f->slot0.count = in_cnt;
    f->slot1 = fuel_in[fi];
    f->slot1.count = fu_cnt;
    f->slot2 = sr_empty();
    f->burn_time = 0;
    f->current_burn_time = 0;
    f->cook_time = 0;
    f->total_cook = fft_get_cook_time(&f->slot0);
}

MC_HD static inline void fft_tick(FftFurnace *f) {
    int was_burning = fft_is_burning(f);

    if (fft_is_burning(f))
        f->burn_time--;

    if (fft_is_burning(f) || (!fft_is_empty(&f->slot1) && !fft_is_empty(&f->slot0))) {
        if (!fft_is_burning(f) && fft_can_smelt(f)) {
            i32 burn = sr_getItemBurnTime(f->slot1);
            f->burn_time = burn;
            f->current_burn_time = burn;
            if (fft_is_burning(f)) {
                f->slot1.count--;
                if (f->slot1.count <= 0)
                    f->slot1 = sr_empty();
            }
        }
        if (fft_is_burning(f) && fft_can_smelt(f)) {
            f->cook_time++;
            if (f->cook_time >= f->total_cook) {
                f->cook_time = 0;
                f->total_cook = fft_get_cook_time(&f->slot0);
                fft_smelt(f);
            }
        } else {
            f->cook_time = 0;
        }
    } else if (!fft_is_burning(f) && f->cook_time > 0) {
        i32 v = f->cook_time - 2;
        if (v < 0) v = 0;
        if (v > f->total_cook) v = f->total_cook;
        f->cook_time = v;
    }

    (void)was_burning;
}

MC_HD static inline void fft_dump_tick(const FftFurnace *f, u64 *out, int base) {
    out[base + 0] = (u64)(u32)f->slot0.count;
    out[base + 1] = (u64)(u32)f->slot1.count;
    out[base + 2] = (u64)(u32)f->slot2.count;
    out[base + 3] = (u64)(u32)f->burn_time;
    out[base + 4] = (u64)(u32)f->cook_time;
}

MC_HD static inline void fft_run(FftFurnace *f, u64 seed, int nticks, u64 *out) {
    int t;
    fft_init(f, seed);
    for (t = 0; t < nticks; ++t) {
        fft_tick(f);
        fft_dump_tick(f, out, t * FFT_DUMP_FIELDS);
    }
}

#endif /* MC_FURNACE_FULL_TICK_H */
