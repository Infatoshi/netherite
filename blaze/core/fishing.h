/* Minecraft 1.11.2 EntityFishHook catching timers and fishing loot weights. */
#ifndef MC_FISHING_H
#define MC_FISHING_H

#include "mc_rng.h"
#include "enchant_table.h"

enum {
    FISH_EVENT_NONE = 0,
    FISH_EVENT_APPROACH = 1,
    FISH_EVENT_BITE = 2,
    FISH_EVENT_WINDOW_END = 4
};

typedef struct {
    int ticks_catchable;
    int ticks_caught_delay;
    int ticks_catchable_delay;
    int lure;
    int luck;
    float approach_angle;
    float bite_pitch;
    double motion_y;
} FishCatchState;

typedef struct {
    int item, count, meta;
    int category; /* 0 junk, 1 treasure, 2 fish */
    int n_enchants;
    i16 enchant_id[4];
    i16 enchant_level[4];
} FishLoot;

MC_HD static inline int fish_effective_weight(
        int weight, int quality, float luck) {
    int effective = (int)floorf((float)weight + (float)quality * luck);
    return effective > 0 ? effective : 0;
}

/* One server catchingFish tick. The caller supplies whether the sampled
 * approach/splash column is water because that lookup controls later draws. */
MC_HD static inline int fish_catch_tick(
        FishCatchState *s, JavaGaussianRandom *random,
        int raining_above, int can_see_sky,
        int approach_water, int splash_water) {
    int speed = 1;
    int event = FISH_EVENT_NONE;
    if (jrand_float(&random->random) < 0.25F && raining_above) ++speed;
    if (jrand_float(&random->random) < 0.5F && !can_see_sky) --speed;
    if (s->ticks_catchable > 0) {
        --s->ticks_catchable;
        if (s->ticks_catchable <= 0) {
            s->ticks_caught_delay = 0;
            s->ticks_catchable_delay = 0;
            event |= FISH_EVENT_WINDOW_END;
        } else {
            s->motion_y -= 0.2 * (double)jrand_float(&random->random)
                * (double)jrand_float(&random->random);
        }
    } else if (s->ticks_catchable_delay > 0) {
        s->ticks_catchable_delay -= speed;
        if (s->ticks_catchable_delay > 0) {
            s->approach_angle = (float)(
                (double)s->approach_angle
                    + jrand_gaussian_next(random) * 4.0);
            if (approach_water
                    && jrand_float(&random->random) < 0.15F)
                event |= FISH_EVENT_APPROACH;
        } else {
            s->motion_y = (double)(-0.4F
                * (0.6F + jrand_float(&random->random) * 0.4F));
            {
                float first = jrand_float(&random->random);
                float second = jrand_float(&random->random);
                s->bite_pitch = 1.0F + (first - second) * 0.4F;
            }
            s->ticks_catchable = 20
                + jrand_int_bound(&random->random, 21);
            event |= FISH_EVENT_BITE;
        }
    } else if (s->ticks_caught_delay > 0) {
        float chance = 0.15F;
        s->ticks_caught_delay -= speed;
        if (s->ticks_caught_delay < 20)
            chance = (float)((double)chance
                + (double)(20 - s->ticks_caught_delay) * 0.05);
        else if (s->ticks_caught_delay < 40)
            chance = (float)((double)chance
                + (double)(40 - s->ticks_caught_delay) * 0.02);
        else if (s->ticks_caught_delay < 60)
            chance = (float)((double)chance
                + (double)(60 - s->ticks_caught_delay) * 0.01);
        if (jrand_float(&random->random) < chance) {
            (void)jrand_float(&random->random);
            (void)jrand_float(&random->random);
            if (splash_water)
                (void)jrand_int_bound(&random->random, 2);
            event |= FISH_EVENT_APPROACH;
        }
        if (s->ticks_caught_delay <= 0) {
            s->approach_angle = jrand_float(&random->random) * 360.0F;
            s->ticks_catchable_delay = 20
                + jrand_int_bound(&random->random, 61);
        }
    } else {
        s->ticks_caught_delay = 100
            + jrand_int_bound(&random->random, 501) - s->lure * 100;
    }
    return event;
}

MC_HD static inline FishLoot fish_generate_loot(
        JavaGaussianRandom *random, float luck) {
    static const int junk_item[11] = {
        301, 334, 352, 373, 287, 346, 281, 280, 351, 131, 367
    };
    static const int junk_weight[11] = {10,10,10,10,5,2,10,5,1,10,10};
    static const int treasure_item[6] = {111,421,329,261,346,340};
    static const int fish_meta[4] = {0,1,2,3};
    static const int fish_weight[4] = {60,25,2,13};
    FishLoot out;
    out.item = 0; out.count = 1; out.meta = 0; out.category = 2;
    out.n_enchants = 0;
    for (int i = 0; i < 4; ++i) {
        out.enchant_id[i] = 0; out.enchant_level[i] = 0;
    }
    int junk = fish_effective_weight(10, -2, luck);
    int treasure = fish_effective_weight(5, 2, luck);
    int fish = fish_effective_weight(85, -1, luck);
    int roll = jrand_int_bound(&random->random, junk + treasure + fish);
    if (roll < junk) {
        int total = 0;
        out.category = 0;
        for (int i = 0; i < 11; ++i) total += junk_weight[i];
        roll = jrand_int_bound(&random->random, total);
        for (int i = 0; i < 11; ++i) {
            if (roll < junk_weight[i]) {
                out.item = junk_item[i];
                if (out.item == 351) out.count = 10;
                if (out.item == 301)
                    out.meta = mc_floorf((1.0F
                        - jrand_float(&random->random) * 0.90F) * 65.0F);
                else if (out.item == 346)
                    out.meta = mc_floorf((1.0F
                        - jrand_float(&random->random) * 0.90F) * 64.0F);
                else if (out.item == 373)
                    out.meta = 1; /* compact TB_PT_WATER for Potion:"water" */
                break;
            }
            roll -= junk_weight[i];
        }
    } else if ((roll -= junk) < treasure) {
        out.category = 1;
        out.item = treasure_item[
            jrand_int_bound(&random->random, 6)];
        if (out.item == 261)
            out.meta = mc_floorf((1.0F
                - jrand_float(&random->random) * 0.25F) * 384.0F);
        else if (out.item == 346)
            out.meta = mc_floorf((1.0F
                - jrand_float(&random->random) * 0.25F) * 64.0F);
        if (out.item == 261 || out.item == 346 || out.item == 340) {
            EtData enchants[ET_MAX_LIST];
            int kind = et_item_kind_from_id(out.item);
            int n = et_build_list(&random->random,
                kind >= 0 ? kind : ET_ITEM_BOOK, 30, 1,
                enchants, ET_MAX_LIST);
            if (out.item == 340) out.item = 403;
            if (n > 4) n = 4;
            out.n_enchants = n;
            for (int i = 0; i < n; ++i) {
                out.enchant_id[i] = (i16)enchants[i].id;
                out.enchant_level[i] = (i16)enchants[i].level;
            }
        }
    } else {
        out.category = 2;
        roll = jrand_int_bound(&random->random, 100);
        for (int i = 0; i < 4; ++i) {
            if (roll < fish_weight[i]) {
                out.item = 349;
                out.meta = fish_meta[i];
                break;
            }
            roll -= fish_weight[i];
        }
    }
    return out;
}

#endif
