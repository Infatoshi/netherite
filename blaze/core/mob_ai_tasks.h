/* mob_ai_tasks.h — shared GPU/CPU Java EntityAITasks + PathFinder A*
 *
 * Implements GPU_MOB_AI.md (v2, codex-reviewed):
 *   - Lane-0 sequential mob AI tick with magma-exact semantics
 *   - 32x24x32 PathFinder window, 48-point path cap, 200 heap-pop break
 *   - IntHashMap openPoint aliasing reproduced via PNP_VOL
 *   - JavaRandom 48-bit cursor preservation
 *   - Exact EntityAITasks schedulers for passives and hostiles
 *
 * Port target: java/oracle-src/net/minecraft/entity/ai/
 * Bit-verified reference: magma/game/mob_live.c (detmob arc)
 */
#ifndef MC_MOB_AI_TASKS_H
#define MC_MOB_AI_TASKS_H

#include "mc.h"
#include "mc_math.h"
#include "mc_blocks.h"
#include "mc_rng.h"
#include "living_base.h"
#include "entity_hostile_spine.h"
#include "path_finder.h"

#include <math.h>


/* ---- Task Enums & Constants ---- */
enum {
    PAI_SWIM = 0,
    PAI_PANIC,
    PAI_EAT,
    PAI_WANDER,
    PAI_WATCH,
    PAI_IDLE,
    PAI_NTASKS
};
#define PAI_BIT(t) (1u << (t))
#define PAI_NAV_MAX 48

enum {
    HSWIM = 0, HMELEE, HSWELL, HBOW, HREST, HFLEE, HAVOID, HHOME, HVILL, HWAND, HWATCH, HIDLE, HN
};
#define hai_bit(t) (1u << (t))

#define HT_HURT 1u
#define HT_PLAYER 2u
#define HT_VILLAGER 4u
#define HT_GOLEM 8u

/* ---- Species Attributes & Sizing ---- */
MC_HD static inline void mai_size(int type, float *width, float *height) {
    if (type == EW_TYPE_BLAZE) { *width = 0.6f; *height = 1.8f; return; }
    if (type == EW_TYPE_PIGMAN) { *width = 0.6f; *height = 1.95f; return; }
    if (type == EW_TYPE_ENDERMAN) { *width = 0.6f; *height = 2.9f; return; }
    if (type == EW_TYPE_WITCH) { *width = 0.6f; *height = 1.95f; return; }
    *width = 0.9f;
    if (type == EW_TYPE_SHEEP) *height = 1.3f;
    else if (type == EW_TYPE_PIG) *height = 0.9f;
    else if (type == EW_TYPE_COW) *height = 1.4f;
    else if (type == EW_TYPE_ZOMBIE) { *width = 0.6f; *height = 1.95f; }
    else if (type == EW_TYPE_SKELETON) { *width = 0.6f; *height = 1.99f; }
    else if (type == EW_TYPE_CREEPER) { *width = 0.6f; *height = 1.7f; }
    else { *width = 0.4f; *height = 0.7f; }
}

MC_HD static inline double mai_eye_height(int type) {
    float width, height;
    mai_size(type, &width, &height);
    (void)width;
    if (type == EW_TYPE_SHEEP) return (double)(0.95f * height);
    if (type == EW_TYPE_COW) return 1.3;
    if (type == EW_TYPE_CHICKEN) return (double)height;
    if (type == EW_TYPE_ZOMBIE || type == EW_TYPE_SKELETON)
        return (double)1.74f;
    if (type == EW_TYPE_ENDERMAN) return (double)2.55f;
    if (type == EW_TYPE_CREEPER) return (double)(1.7f * 0.85f);
    return (double)(height * 0.85f);
}

MC_HD static inline double mai_player_eye_y(double py) {
    return py + (double)(float)PSV_EYE_HEIGHT;
}

MC_HD static inline double mai_watch_range_sq(int type) {
    if (type == EW_TYPE_BLAZE || type == EW_TYPE_PIGMAN || type == EW_TYPE_ENDERMAN)
        return 64.0;
    return 36.0;
}

MC_HD static inline float mai_avoid_water_p(int type) {
    if (type == EW_TYPE_BLAZE || type == EW_TYPE_ENDERMAN) return 0.0f;
    return 0.001f;
}

MC_HD static inline int mai_talk_interval(int type) {
    if (type == EW_TYPE_BLAZE || type == EW_TYPE_PIGMAN || type == EW_TYPE_ENDERMAN)
        return 80;
    return 120;
}

MC_HD static inline double mai_panic_multiplier(int type) {
    if (type == EW_TYPE_COW) return 2.0;
    if (type == EW_TYPE_CHICKEN) return 1.4;
    return 1.25;
}

MC_HD static inline double mai_attribute_speed(int type) {
    if (type == EW_TYPE_SHEEP) return 0.23000000417232513;
    if (type == EW_TYPE_PIG || type == EW_TYPE_CHICKEN) return 0.25;
    if (type == EW_TYPE_COW) return 0.20000000298023224;
    if (type == EW_TYPE_ZOMBIE) return 0.23000000417232513;
    if (type == EW_TYPE_SKELETON || type == EW_TYPE_CREEPER) return 0.25;
    if (type == EW_TYPE_ENDERMAN) return 0.30000001192092896;
    if (type == EW_TYPE_WITCH) return 0.25;
    return 0.23000000417232513;
}

MC_HD static inline float mai_follow_range(int type) {
    if (type == EW_TYPE_ENDERMAN) return 64.0f;
    if (type == EW_TYPE_BLAZE) return 48.0f;
    if (type == EW_TYPE_ZOMBIE || type == EW_TYPE_PIGMAN) return 35.0f;
    return 16.0f;
}

MC_HD static inline int mai_priority(int type, int task) {
    if (type == EW_TYPE_BLAZE || type == EW_TYPE_ENDERMAN) {
        if (task == PAI_WANDER) return 7;
        if (task == PAI_WATCH || task == PAI_IDLE) return 8;
        return 99;
    }
    if (type == EW_TYPE_PIGMAN) {
        if (task == PAI_SWIM) return 0;
        if (task == PAI_WANDER) return 7;
        if (task == PAI_WATCH || task == PAI_IDLE) return 8;
        return 99;
    }
    if (task == PAI_SWIM) return 0;
    if (task == PAI_PANIC) return 1;
    if (type == EW_TYPE_SHEEP) {
        if (task == PAI_EAT) return 5;
        if (task == PAI_WANDER) return 6;
        if (task == PAI_WATCH) return 7;
        if (task == PAI_IDLE) return 8;
    } else if (type == EW_TYPE_PIG) {
        if (task == PAI_WANDER) return 6;
        if (task == PAI_WATCH) return 7;
        if (task == PAI_IDLE) return 8;
    } else if (type == EW_TYPE_COW || type == EW_TYPE_CHICKEN) {
        if (task == PAI_WANDER) return 5;
        if (task == PAI_WATCH) return 6;
        if (task == PAI_IDLE) return 7;
    }
    return 99;
}

MC_HD static inline int mai_mutex(int task) {
    if (task == PAI_SWIM) return 4;
    if (task == PAI_PANIC || task == PAI_WANDER) return 1;
    if (task == PAI_WATCH) return 2;
    if (task == PAI_IDLE) return 3;
    if (task == PAI_EAT) return 7;
    return 0;
}

MC_HD static inline int hai_pri(int type, int task) {
    if (task == HSWIM) return 1;
    if (type == EW_TYPE_CREEPER) {
        if (task == HSWELL) return 2;
        if (task == HAVOID) return 3;
        if (task == HMELEE) return 4;
        if (task == HWAND) return 5;
        if (task == HWATCH) return 6;
        if (task == HIDLE) return 6;
    } else if (type == EW_TYPE_SKELETON) {
        if (task == HREST) return 2;
        if (task == HFLEE) return 3;
        if (task == HAVOID) return 4;
        if (task == HBOW || task == HMELEE) return 4;
        if (task == HWAND) return 5;
        if (task == HWATCH) return 6;
        if (task == HIDLE) return 6;
    } else if (type == EW_TYPE_ZOMBIE) {
        if (task == HMELEE) return 2;
        if (task == HWAND) return 7;
        if (task == HWATCH) return 8;
        if (task == HIDLE) return 8;
    }
    return 99;
}

MC_HD static inline int hai_mutex(int task) {
    if (task == HSWIM) return 4;
    if (task == HREST || task == HFLEE || task == HAVOID || task == HMELEE ||
        task == HBOW || task == HHOME || task == HVILL || task == HWAND) return 1;
    if (task == HSWELL) return 1;
    if (task == HWATCH) return 2;
    if (task == HIDLE) return 3;
    return 0;
}

MC_HD static inline float hai_follow(int type) {
    return type == EW_TYPE_ZOMBIE ? 35.0f : 16.0f;
}

MC_HD static inline const int *hai_goals(int type, int skel_melee) {
    static const int creeper[] = {HSWIM, HSWELL, HAVOID, HMELEE, HWAND, HWATCH, HIDLE, -1};
    static const int skel_bow[] = {HSWIM, HREST, HFLEE, HAVOID, HBOW, HWAND, HWATCH, HIDLE, -1};
    static const int skel_mel[] = {HSWIM, HREST, HFLEE, HAVOID, HMELEE, HWAND, HWATCH, HIDLE, -1};
    static const int zombie[] = {HSWIM, HMELEE, HWAND, HWATCH, HIDLE, -1};
    static const int empty[] = {-1};
    if (type == EW_TYPE_CREEPER) return creeper;
    if (type == EW_TYPE_SKELETON) return skel_melee ? skel_mel : skel_bow;
    if (type == EW_TYPE_ZOMBIE) return zombie;
    return empty;
}

MC_HD static inline int mai_det_ai(int type) {
    return type == EW_TYPE_SHEEP || type == EW_TYPE_PIG ||
           type == EW_TYPE_COW || type == EW_TYPE_CHICKEN ||
           type == EW_TYPE_BLAZE || type == EW_TYPE_PIGMAN ||
           type == EW_TYPE_ENDERMAN;
}

MC_HD static inline int hai_ok(int type) {
    return type == EW_TYPE_ZOMBIE || type == EW_TYPE_SKELETON ||
           type == EW_TYPE_CREEPER;
}

MC_HD static inline int mai_det_living(int type) {
    return mai_det_ai(type) || hai_ok(type);
}

MC_HD static inline int mai_is_hostile(int type) {
    return type == EW_TYPE_ZOMBIE || type == EW_TYPE_SKELETON ||
           type == EW_TYPE_CREEPER || type == EW_TYPE_SLIME ||
           type == EW_TYPE_GHAST || type == EW_TYPE_SPIDER ||
           type == EW_TYPE_PIGMAN || type == EW_TYPE_ENDERMAN ||
           type == EW_TYPE_BLAZE || type == EW_TYPE_WITCH;
}

MC_HD static inline int mai_is_passive(int type) {
    return type == EW_TYPE_SHEEP || type == EW_TYPE_PIG ||
           type == EW_TYPE_COW || type == EW_TYPE_CHICKEN;
}

/* ---- Math and Geometry Helpers ---- */
MC_HD static inline int mai_ceil_f(float v) {
    int i = (int)v;
    return v > (float)i ? i + 1 : i;
}

MC_HD static inline float mai_wrap_degrees(float v) {
    v = fmodf(v, 360.0f);
    if (v >= 180.0f) v -= 360.0f;
    if (v < -180.0f) v += 360.0f;
    return v;
}

MC_HD static inline float mai_deg(double rad) {
    return (float)(rad * (180.0 / MC_PI));
}

MC_HD static inline float mai_atan2_yaw(double dz, double dx) {
    return mai_deg(mc_atan2(dz, dx)) - 90.0f;
}

MC_HD static inline float mai_update_rotation(float current, float target, float max_delta) {
    float d = mai_wrap_degrees(target - current);
    if (d > max_delta) d = max_delta;
    if (d < -max_delta) d = -max_delta;
    return current + d;
}

MC_HD static inline float mai_limit_angle(float current, float target, float max_delta) {
    float d = mai_wrap_degrees(target - current);
    if (d > max_delta) d = max_delta;
    if (d < -max_delta) d = -max_delta;
    return mai_wrap_degrees(current + d);
}

MC_HD static inline float mai_angle_bound(float a1, float a2, float maxd) {
    float d = mai_wrap_degrees(a1 - a2);
    if (d < -maxd) d = -maxd;
    if (d >= maxd) d = maxd;
    return a1 - d;
}

/* Polar Box-Muller Gaussian for Blaze */
MC_HD static inline double mai_gaussian(RlSnapMob *m) {
    if (m->have_gauss) {
        m->have_gauss = 0;
        return m->gauss;
    }
    JavaRandom jr; jr.seed = m->seed48;
    double v1, v2, s, mul;
    do {
        v1 = 2.0 * jrand_double(&jr) - 1.0;
        v2 = 2.0 * jrand_double(&jr) - 1.0;
        s = v1 * v1 + v2 * v2;
    } while (s >= 1.0 || s == 0.0);
    mul = sqrt(-2.0 * log(s) / s);
    m->gauss = v2 * mul;
    m->have_gauss = 1;
    m->seed48 = jr.seed;
    return v1 * mul;
}

/* ---- Block and Material Queries ---- */
MC_HD static inline int mai_mc_to_pb(int id) {
    if (id == 0) return PB_AIR;
    if (id == 8 || id == 9) return PB_WATER;
    if (id == 10 || id == 11) return PB_LAVA;
    if (id == 51) return PB_FIRE;
    if (id == 81) return PB_CACTUS;
    if (id == 85 || id == 113 || id == 188 || id == 189 || id == 190 ||
        id == 191 || id == 192 || id == 139 || id == 107)
        return PB_FENCE;
    if (id == 96 || id == 167) return PB_TRAPDOOR;
    if (id == 27 || id == 28 || id == 66 || id == 157) return PB_RAIL;
    if (!ml_solid_id(id)) return PB_AIR;
    return PB_STONE;
}

MC_HD static inline int mai_is_full_block(int id) {
    BptProps p;
    if (id == 0) return 0;
    switch (id) {
        case 44: case 43: case 125: case 126: case 181: case 182:
        case 53: case 67: case 108: case 109: case 114: case 128:
        case 134: case 135: case 136: case 156: case 163: case 164:
        case 180: case 203:
        case 85: case 113: case 188: case 189: case 190: case 191: case 192:
        case 139: case 107:
        case 65: case 96: case 167:
            return 0;
        default:
            break;
    }
    p = mc_bpt_props(id);
    return (p.flags & BF_SOLID) && !(p.flags & BF_LIQUID) && p.light_opacity >= 255;
}

#endif /* MC_MOB_AI_TASKS_H */
