/* item_food_eat: ItemFood use duration (32 ticks) + FoodStats hunger/saturation over ticks.
 *
 * Subset: ItemFood.onItemRightClick/canEat, getMaxItemUseDuration, onItemUseFinish/addStats;
 * EntityLivingBase.updateActiveHand countdown; FoodStats.addStats/addExhaustion/onUpdate (NORMAL,
 * naturalRegeneration off). Apple + bread only (items_core ids/heal).
 *
 * Harness: seed-varied initial hunger; eat at tick 0, finish ~tick 31; retry tick 45 if hungry.
 * Per-tick sprint exhaustion (0.05). Per-tick dump: food, sat, exhaust (float bits), use_count,
 * stack_count, active, event.
 *
 * READ-ONLY: items_core.h (IC_APPLE/IC_BREAD, ic_food_heal). CUT: potion effects, sounds/stats,
 * Forge onItemUse*, natural regen/heal/starve timer paths (naturalRegen=0). CPU==CUDA. */
#ifndef MC_ITEM_FOOD_EAT_H
#define MC_ITEM_FOOD_EAT_H

#include "mc.h"
#include "items_core.h"

#define IFE_USE_TICKS     32
#define IFE_NUM_TICKS     80
#define IFE_DUMP_FIELDS   7
#define IFE_OUT           (IFE_NUM_TICKS * IFE_DUMP_FIELDS)

enum {
    IFE_EVT_NONE   = 0,
    IFE_EVT_START  = 1,
    IFE_EVT_FINISH = 2,
    IFE_EVT_DENY   = 3
};

typedef struct {
    i32 food_level;
    float food_saturation;
    float food_exhaustion;
    i32 food_timer;
} IfeFoodStats;

typedef struct {
    IfeFoodStats food;
    i32 held_item;
    i32 stack_count;
    i32 use_count;
    int active;
} IfePlayer;

MC_HD static inline float ife_food_sat_mod(i32 item_id) {
    if (item_id == IC_APPLE) return 0.3f;
    if (item_id == IC_BREAD) return 0.6f;
    return 0.6f;
}

MC_HD static inline int ife_need_food(const IfeFoodStats *fs) {
    return fs->food_level < 20;
}

MC_HD static inline int ife_can_eat(const IfePlayer *p, int always_edible) {
    return (always_edible || ife_need_food(&p->food)) ? 1 : 0;
}

MC_HD static inline void ife_add_stats(IfeFoodStats *fs, i32 heal, float sat_mod) {
    i32 sum = heal + fs->food_level;
    fs->food_level = sum < 20 ? sum : 20;
    {
        float add = (float)heal * sat_mod * 2.0f;
        float ns = fs->food_saturation + add;
        float cap = (float)fs->food_level;
        fs->food_saturation = ns < cap ? ns : cap;
    }
}

MC_HD static inline void ife_add_exhaustion(IfeFoodStats *fs, float ex) {
    float ns = fs->food_exhaustion + ex;
    fs->food_exhaustion = ns < 40.0f ? ns : 40.0f;
}

MC_HD static inline void ife_food_on_update(IfeFoodStats *fs, int peaceful) {
    if (fs->food_exhaustion > 4.0f) {
        fs->food_exhaustion -= 4.0f;
        if (fs->food_saturation > 0.0f) {
            float ns = fs->food_saturation - 1.0f;
            fs->food_saturation = ns > 0.0f ? ns : 0.0f;
        } else if (!peaceful) {
            i32 nl = fs->food_level - 1;
            fs->food_level = nl > 0 ? nl : 0;
        }
    }
    if (fs->food_level <= 0) {
        fs->food_timer++;
        if (fs->food_timer >= 80)
            fs->food_timer = 0;
    } else {
        fs->food_timer = 0;
    }
}

MC_HD static inline void ife_on_item_use_finish(IfePlayer *p) {
    i32 heal = ic_food_heal(p->held_item);
    float sat = ife_food_sat_mod(p->held_item);
    ife_add_stats(&p->food, heal, sat);
    p->stack_count--;
    if (p->stack_count < 0) p->stack_count = 0;
    p->active = 0;
    p->use_count = 0;
}

MC_HD static inline void ife_update_active_hand(IfePlayer *p, i32 *event) {
    if (!p->active) return;
    if (p->use_count <= 25 && (p->use_count % 4) == 0) {
        /* updateItemUse particles only */
    }
    p->use_count--;
    if (p->use_count <= 0) {
        ife_on_item_use_finish(p);
        if (event) *event = IFE_EVT_FINISH;
    }
}

MC_HD static inline void ife_try_start_eat(IfePlayer *p, i32 *event) {
    if (p->active || p->stack_count <= 0) return;
    if (ife_can_eat(p, 0)) {
        p->active = 1;
        p->use_count = IFE_USE_TICKS;
        *event = IFE_EVT_START;
    } else {
        *event = IFE_EVT_DENY;
    }
}

MC_HD static inline void ife_init(IfePlayer *p, i64 seed) {
    p->food.food_level = 6 + (i32)(seed % 8);
    p->food.food_saturation = (float)p->food.food_level * 0.1f;
    p->food.food_exhaustion = (float)(seed % 5) * 0.5f;
    p->food.food_timer = 0;
    p->held_item = (seed & 1) ? IC_BREAD : IC_APPLE;
    p->stack_count = 2;
    p->use_count = 0;
    p->active = 0;
}

MC_HD static inline u64 ife_fbits(float f) {
    union { float f; u32 u; } u;
    u.f = f;
    return (u64)u.u;
}

MC_HD static inline void ife_emit_tick(const IfePlayer *p, i32 event, u64 *out, int base) {
    out[base + 0] = (u64)(u32)p->food.food_level;
    out[base + 1] = ife_fbits(p->food.food_saturation);
    out[base + 2] = ife_fbits(p->food.food_exhaustion);
    out[base + 3] = (u64)(u32)p->use_count;
    out[base + 4] = (u64)(u32)p->stack_count;
    out[base + 5] = (u64)(u32)(p->active ? 1 : 0);
    out[base + 6] = (u64)(u32)event;
}

MC_HD static inline void ife_run(i64 seed, u64 *out) {
    IfePlayer p;
    int t;
    ife_init(&p, seed);
    for (t = 0; t < IFE_NUM_TICKS; ++t) {
        i32 event = IFE_EVT_NONE;
        if (t == 0 || t == 45)
            ife_try_start_eat(&p, &event);
        ife_add_exhaustion(&p.food, 0.05f);
        ife_update_active_hand(&p, &event);
        ife_food_on_update(&p.food, 0);
        ife_emit_tick(&p, event, out, t * IFE_DUMP_FIELDS);
    }
}

#endif /* MC_ITEM_FOOD_EAT_H */
