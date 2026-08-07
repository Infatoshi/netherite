/* items_core: ItemStack (item,count,meta) + tool dig speed, food constants, bucket fill/empty.
 * PORT: ItemTool.getStrVsBlock, ItemFood healAmount, ItemBucket onItemRightClick (synthetic world).
 * Pure deterministic battery; CPU==CUDA. Subset matches crafting_recipes tool ids. */
#ifndef MC_ITEMS_CORE_H
#define MC_ITEMS_CORE_H

#include "mc.h"
#include "mc_world.h"
#include "mc_blocks.h"

/* StoredEnchantments-equivalent for enchanted books (n_enchants==0 for normal). */
enum { IC_MAX_ENCHANTS = 8 };
typedef struct { i16 id; i16 level; } IcEnch;
typedef struct ICStack {
    i32 item;
    i32 count;
    i32 meta;
    i32 n_enchants;
    IcEnch enchants[IC_MAX_ENCHANTS];
} ICStack;

enum {
    IC_AIR            = 0,
    IC_STONE          = 1,
    IC_IRON_ORE       = 15,
    IC_WOODEN_PICKAXE = 270,
    IC_STONE_PICKAXE  = 274,
    IC_APPLE          = 260,
    IC_BREAD          = 297,
    IC_ENCHANTED_BOOK = 403, /* ItemEnchantedBook; max stack 1 in 1.11.2 */
    IC_BUCKET         = 325,
    IC_WATER_BUCKET   = 326,
    IC_LAVA_BUCKET    = 327,
    IC_CARROT_ON_A_STICK = 398,
    IC_FLOWING_WATER  = 8,
    IC_FLOWING_LAVA   = 10
};

/* Compact stand-in for the Fireworks compound on items 401/402. Vanilla
 * metadata remains zero; magma stores the sound-relevant payload summary in
 * otherwise-unused bits so hot simulation stacks stay fixed-size. */
MC_HD static inline int ic_firework_meta_payload(
        int flight, int explosions, int large, int flicker) {
    return (flight & 0xff) | ((explosions & 0x1f) << 8)
        | ((large != 0) << 13) | ((flicker != 0) << 14);
}
MC_HD static inline int ic_firework_meta(int flight, int explosions) {
    return ic_firework_meta_payload(flight, explosions, 0, 0);
}
MC_HD static inline int ic_firework_flight(const ICStack *s) {
    return s && s->item == 401 ? s->meta & 0xff : 0;
}
MC_HD static inline int ic_firework_explosions(const ICStack *s) {
    return s && (s->item == 401 || s->item == 402)
        ? (s->meta >> 8) & 0x1f : 0;
}
MC_HD static inline int ic_firework_large(const ICStack *s) {
    return s && (s->item == 401 || s->item == 402)
        ? (s->meta >> 13) & 1 : 0;
}
MC_HD static inline int ic_firework_flicker(const ICStack *s) {
    return s && (s->item == 401 || s->item == 402)
        ? (s->meta >> 14) & 1 : 0;
}

#define IC_W 8
#define IC_VOL (IC_W * IC_W * IC_W)

typedef struct { u16 cells[IC_VOL]; } ICWorld;

MC_HD static inline ICStack ic_empty(void) {
    ICStack s;
    int i;
    s.item = IC_AIR; s.count = 0; s.meta = 0; s.n_enchants = 0;
    for (i = 0; i < IC_MAX_ENCHANTS; ++i) { s.enchants[i].id = 0; s.enchants[i].level = 0; }
    return s;
}
MC_HD static inline ICStack ic_mk(i32 item, i32 count, i32 meta) {
    ICStack s = ic_empty();
    s.item = item; s.count = count; s.meta = meta;
    return s;
}
MC_HD static inline void ic_copy_enchants(ICStack *dst, const IcEnch *src, int n) {
    int i;
    if (!dst) return;
    if (n < 0) n = 0;
    if (n > IC_MAX_ENCHANTS) n = IC_MAX_ENCHANTS;
    dst->n_enchants = n;
    for (i = 0; i < n; ++i) dst->enchants[i] = src[i];
    for (; i < IC_MAX_ENCHANTS; ++i) { dst->enchants[i].id = 0; dst->enchants[i].level = 0; }
}

/* ItemStack.areItemStackTagsEqual subset: StoredEnchantments list must match. */
MC_HD static inline int ic_enchants_equal(const ICStack *a, const ICStack *b) {
    int i;
    if (!a || !b) return 0;
    if (a->n_enchants != b->n_enchants) return 0;
    for (i = 0; i < a->n_enchants; ++i)
        if (a->enchants[i].id != b->enchants[i].id ||
            a->enchants[i].level != b->enchants[i].level)
            return 0;
    return 1;
}

/* ItemStack.areItemsEqual + areItemStackTagsEqual (item, meta, enchants). */
MC_HD static inline int ic_stack_equal(const ICStack *a, const ICStack *b) {
    if (!a || !b) return 0;
    if (a->item == IC_AIR || a->count <= 0 || b->item == IC_AIR || b->count <= 0) return 0;
    if (a->item != b->item || a->meta != b->meta) return 0;
    return ic_enchants_equal(a, b);
}

/* Non-destructive partial copy: item/meta/enchants retained, count = amount. */
MC_HD static inline ICStack ic_with_count(const ICStack *src, i32 amount) {
    ICStack out;
    if (!src || amount <= 0 || src->item == IC_AIR || src->count <= 0) return ic_empty();
    if (amount > src->count) amount = src->count;
    out = ic_mk(src->item, amount, src->meta);
    ic_copy_enchants(&out, src->enchants, src->n_enchants);
    return out;
}

MC_HD static inline int ic_idx(int x, int y, int z) { return (y * IC_W + z) * IC_W + x; }

MC_HD static inline u16 ic_get(const ICWorld *w, int x, int y, int z) {
    if (x < 0 || x >= IC_W || y < 0 || y >= IC_W || z < 0 || z >= IC_W) return mc_state(BLK_AIR, 0);
    return w->cells[ic_idx(x, y, z)];
}

MC_HD static inline void ic_set(ICWorld *w, int x, int y, int z, u16 s) {
    if (x < 0 || x >= IC_W || y < 0 || y >= IC_W || z < 0 || z >= IC_W) return;
    w->cells[ic_idx(x, y, z)] = s;
}

MC_HD static inline int ic_is_pickaxe_effective(int block_id) {
    return block_id == BLK_STONE || block_id == BLK_COBBLESTONE || block_id == BLK_IRON_ORE
        || block_id == BLK_COAL_ORE || block_id == BLK_OBSIDIAN;
}

MC_HD static inline float ic_tool_dig_speed(i32 tool_id, int block_id) {
    float eff = 1.0f;
    if (tool_id == IC_WOODEN_PICKAXE) eff = 2.0f;
    else if (tool_id == IC_STONE_PICKAXE) eff = 4.0f;
    else return 1.0f;
    return ic_is_pickaxe_effective(block_id) ? eff : 1.0f;
}

MC_HD static inline i32 ic_food_heal(i32 item_id) {
    if (item_id == IC_APPLE) return 4;
    if (item_id == IC_BREAD) return 5;
    return 0;
}

MC_HD static inline ICStack ic_bucket_fill(ICWorld *w, ICStack bucket, int x, int y, int z) {
    u16 s = ic_get(w, x, y, z);
    int id = mc_state_id(s);
    if (bucket.item != IC_BUCKET || bucket.count != 1) return bucket;
    if (id == IC_FLOWING_WATER && mc_state_meta(s) == 0) {
        ic_set(w, x, y, z, mc_state(BLK_AIR, 0));
        return ic_mk(IC_WATER_BUCKET, 1, 0);
    }
    if (id == IC_FLOWING_LAVA && mc_state_meta(s) == 0) {
        ic_set(w, x, y, z, mc_state(BLK_AIR, 0));
        return ic_mk(IC_LAVA_BUCKET, 1, 0);
    }
    return bucket;
}

MC_HD static inline ICStack ic_bucket_place(ICWorld *w, ICStack bucket, int x, int y, int z) {
    u16 s = ic_get(w, x, y, z);
    if (mc_state_id(s) != BLK_AIR) return bucket;
    if (bucket.item == IC_WATER_BUCKET && bucket.count == 1) {
        ic_set(w, x, y, z, mc_state(BLK_FLOWING_WATER, 0));
        return ic_mk(IC_BUCKET, 1, 0);
    }
    if (bucket.item == IC_LAVA_BUCKET && bucket.count == 1) {
        ic_set(w, x, y, z, mc_state(BLK_FLOWING_LAVA, 0));
        return ic_mk(IC_BUCKET, 1, 0);
    }
    return bucket;
}

#define IC_NTESTS 8

MC_HD static inline void ic_run_battery(u64 *out) {
    int k = 0;
    /* dig speed tests: float bits */
    {
        float v = ic_tool_dig_speed(IC_STONE_PICKAXE, BLK_STONE);
        union { float f; u32 u; } u; u.f = v; out[k++] = u.u;
    }
    {
        float v = ic_tool_dig_speed(IC_WOODEN_PICKAXE, BLK_STONE);
        union { float f; u32 u; } u; u.f = v; out[k++] = u.u;
    }
    {
        float v = ic_tool_dig_speed(IC_AIR, BLK_STONE);
        union { float f; u32 u; } u; u.f = v; out[k++] = u.u;
    }
    /* food heal */
    out[k++] = (u64)(u32)ic_food_heal(IC_APPLE);
    out[k++] = (u64)(u32)ic_food_heal(IC_BREAD);
    /* bucket: fill water, place water, cell state */
    ICWorld w;
    for (int i = 0; i < IC_VOL; ++i) w.cells[i] = mc_state(BLK_AIR, 0);
    ic_set(&w, 4, 4, 4, mc_state(BLK_FLOWING_WATER, 0));
    ICStack b = ic_bucket_fill(&w, ic_mk(IC_BUCKET, 1, 0), 4, 4, 4);
    out[k++] = (u64)(u32)b.item;
    out[k++] = (u64)(u32)mc_state_id(ic_get(&w, 4, 4, 4));
    b = ic_bucket_place(&w, ic_mk(IC_WATER_BUCKET, 1, 0), 3, 4, 4);
    out[k++] = (u64)(u32)b.item;
    out[k++] = (u64)(u32)mc_state_id(ic_get(&w, 3, 4, 4));
}

#endif /* MC_ITEMS_CORE_H */
