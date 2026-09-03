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
/* magma mob_live.c:1618. Bits are not 1<<enum: swim=1 wander=8 watch=16 idle=32
 * melee=64 swell=128 bow=256. Snapshot task_bits and the 256u HBOW tests
 * hash this packing. */
MC_HD static inline unsigned hai_bit(int task) {
    static const unsigned b[HN] = {
        1u, 64u, 128u, 256u, 512u, 1024u, 2048u, 4096u, 8192u, 8u, 16u, 32u
    };
    return (task >= 0 && task < HN) ? b[task] : 0u;
}

#define HT_HURT 1u
#define HT_PLAYER 2u
#define HT_VILLAGER 4u
#define HT_GOLEM 8u

#define GM_MOB_DESPAWN_SOFT 32.0
#define GM_MOB_DESPAWN_HARD 128.0
#define GM_MOB_DESPAWN_DELAY 600
#define GM_MOB_FIRE_TICKS 160


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
    /* magma mob_live.c:1624 */
    if (task == HSWIM) return type == EW_TYPE_ZOMBIE ? 0 : 1;
    if (task == HMELEE) return type == EW_TYPE_ZOMBIE ? 2 : 4;
    if (task == HSWELL) return 2;
    if (task == HBOW || task == HREST)
        return type == EW_TYPE_SKELETON && task == HREST ? 2 : 4;
    if (task == HFLEE || task == HAVOID) return 3;
    if (task == HHOME) return 5;
    if (task == HVILL) return 6;
    if (task == HWAND) return type == EW_TYPE_ZOMBIE ? 7 : 5;
    if (task == HWATCH || task == HIDLE) return type == EW_TYPE_ZOMBIE ? 8 : 6;
    return 99;
}

MC_HD static inline int hai_mutex(int task) {
    /* magma mob_live.c:1636. Wander mutex 1 overlaps melee/bow/idle 3. */
    if (task == HSWIM) return 4;
    if (task == HMELEE || task == HBOW || task == HIDLE) return 3;
    if (task == HWATCH) return 2;
    if (task == HREST) return 0;
    return 1;
}

MC_HD static inline float hai_follow(int type) {
    return type == EW_TYPE_ZOMBIE ? 35.0f : 16.0f;
}

MC_HD static inline const int *hai_goals(int type, int skel_melee) {
    /* magma mob_live.c:1646. Skeleton combat is LinkedHashSet-appended after
     * wander/watch/idle so those shouldExecute draws still run that setup tick. */
    static const int z[] = {HSWIM, HMELEE, HHOME, HVILL, HWAND, HWATCH, HIDLE, -1};
    static const int s_bow[] = {HSWIM, HREST, HFLEE, HAVOID, HWAND, HWATCH, HIDLE, HBOW, -1};
    static const int s_melee[] = {HSWIM, HREST, HFLEE, HAVOID, HWAND, HWATCH, HIDLE, HMELEE, -1};
    static const int c[] = {HSWIM, HSWELL, HAVOID, HMELEE, HWAND, HWATCH, HIDLE, -1};
    static const int empty[] = {-1};
    if (type == EW_TYPE_SKELETON) return skel_melee ? s_melee : s_bow;
    if (type == EW_TYPE_CREEPER) return c;
    if (type == EW_TYPE_ZOMBIE) return z;
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
    /* magma pai_deg (mob_live.c:1340) rounds the constant to float first. */
    return (float)(rad * (float)(180.0 / (float)MC_PI));
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

/* EntityMoveHelper.limitAngle (EntityMoveHelper.java:152-172): the result is
 * folded into [0, 360], NOT wrapped into [-180, 180). magma pai_limit_angle
 * (mob_live.c:1329). */
MC_HD static inline float mai_limit_angle(float current, float target, float max_delta) {
    float f1 = mai_update_rotation(current, target, max_delta);
    if (f1 < 0.0f) f1 += 360.0f;
    else if (f1 > 360.0f) f1 -= 360.0f;
    return f1;
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

MC_HD static inline float mai_brightness(const Blaze *e, int x, int y, int z) {
    int light = cu_world_light(e, x, y, z);
    if (light < 0) light = 0;
    if (light > 15) light = 15;
    float f1 = 1.0f - (float)light / 15.0f;
    return (1.0f - f1) / (f1 * 3.0f + 1.0f);
}

MC_HD static inline float mai_light_brightness(const Blaze *e, int dim, int x, int y, int z) {
    float t = mai_brightness(e, x, y, z);
    if (dim == -1) t = t * 0.9f + 0.1f;
    return t;
}

MC_HD static inline float mai_block_path_weight(const Blaze *e, int dim, int type, int x, int y, int z) {
    float br = mai_light_brightness(e, dim, x, y, z);
    if (mai_is_passive(type)) {
        if (cu_world_block(e, x, y - 1, z) == 2) return 10.0f;
        return br - 0.5f;
    }
    if (type == EW_TYPE_BLAZE || type == EW_TYPE_PIGMAN ||
        type == EW_TYPE_ZOMBIE || type == EW_TYPE_SKELETON ||
        type == EW_TYPE_CREEPER)
        return 0.5f - br;
    return 0.0f;
}

MC_HD static inline int mai_in_material(const Blaze *e, const RlSnapMob *m, int lava) {
    /* magma pai_in_material (mob_live.c:766). Water inset 0.001 is the
     * isInWater stand-in WanderAvoidWater.shouldExecute uses; lava uses
     * isInsideOfMaterial 0.1 / no y-inset. */
    float width, height;
    double inset;
    int x0, x1, y0, y1, z0, z1, x, y, z;
    mai_size(m->type, &width, &height);
    inset = lava ? 0.10000000149011612 : 0.001;
    x0 = mc_floor(m->x - (double)width * 0.5 + inset);
    x1 = mc_floor(m->x + (double)width * 0.5 - inset);
    z0 = mc_floor(m->z - (double)width * 0.5 + inset);
    z1 = mc_floor(m->z + (double)width * 0.5 - inset);
    y0 = mc_floor(m->y - 0.4000000059604645 + (lava ? 0.0 : 0.001));
    y1 = mc_floor(m->y + (double)height - (lava ? 0.0 : 0.001));
    for (x = x0; x <= x1; ++x)
        for (y = y0; y <= y1; ++y)
            for (z = z0; z <= z1; ++z) {
                int id = cu_world_block(e, x, y, z);
                if ((!lava && (id == 8 || id == 9)) ||
                    (lava && (id == 10 || id == 11))) return 1;
            }
    return 0;
}

MC_HD static inline int mai_nearest_water(const Blaze *e, const RlSnapMob *m,
                                          double *out_x, double *out_y, double *out_z) {
    int ox = mc_floor(m->x), oy = mc_floor(m->y), oz = mc_floor(m->z);
    float best = 5.0f * 5.0f * 4.0f * 2.0f;
    int found = 0;
    for (int x = ox - 5; x <= ox + 5; ++x) {
        for (int y = oy - 4; y <= oy + 4; ++y) {
            for (int z = oz - 5; z <= oz + 5; ++z) {
                int id = cu_world_block(e, x, y, z);
                if (id != 8 && id != 9) continue;
                float dx = (float)(x - ox), dy = (float)(y - oy), dz = (float)(z - oz);
                float dist = dx * dx + dy * dy + dz * dz;
                if (dist < best) {
                    best = dist;
                    *out_x = x; *out_y = y; *out_z = z;
                    found = 1;
                }
            }
        }
    }
    return found;
}

/* RandomPositionGenerator.findRandomTarget. The caller owns the JavaRandom
 * cursor: magma pai_random_position (mob_live.c:851) draws straight from the
 * entity's live java.util.Random, so the draws MUST thread through the same
 * cursor the surrounding EntityAIBase.shouldExecute roll used. */
MC_HD static inline int mai_random_position(const Blaze *e, RlSnapMob *m,
                                            int xz, int yrange, int land,
                                            JavaRandom *jr,
                                            double *out_x, double *out_y, double *out_z) {
    int found = 0, best_dx = 0, best_dy = 0, best_dz = 0;
    float best = -99999.0f;
    for (int k = 0; k < 10; ++k) {
        int dx = jrand_int_bound(jr, 2 * xz + 1) - xz;
        int dy = jrand_int_bound(jr, 2 * yrange + 1) - yrange;
        int dz = jrand_int_bound(jr, 2 * xz + 1) - xz;
        int bx = mc_floor(m->x + dx);
        int by = mc_floor(m->y + dy);
        int bz = mc_floor(m->z + dz);
        if (by <= 0 || !mai_is_full_block(cu_world_block(e, bx, by - 1, bz))) continue;
        int score_y = by;
        if (land && ml_solid_id(cu_world_block(e, bx, score_y, bz))) {
            while (score_y < 256 && ml_solid_id(cu_world_block(e, bx, score_y, bz)))
                ++score_y;
        }
        if (land) {
            int id = cu_world_block(e, bx, score_y, bz);
            if (id == 8 || id == 9) continue;
        }
        float score = mai_block_path_weight(e, 0, m->type, bx, score_y, bz);
        if (score > best) {
            best = score;
            best_dx = dx; best_dy = dy; best_dz = dz;
            found = 1;
        }
    }
    if (!found) return 0;
    *out_x = m->x + best_dx;
    *out_y = m->y + best_dy;
    *out_z = m->z + best_dz;
    return 1;
}

/* PathNavigateGround.getPathToPos is a THREE-way dispatch on the destination
 * block's Material, not a two-way solid/not-solid test. Transcribed from
 * magma/game/mob_live.c pai_mat_air / pai_mat_solid / pai_path_to_pos, which
 * is the bit-verified reference for
 * java/oracle-src/net/minecraft/pathfinding/PathNavigateGround.java:46-66. */
MC_HD static inline int mai_mat_air(int id) { return id == 0; }

MC_HD static inline int mai_mat_solid(int id) {
    BptProps p;
    if (id == 0 || id == 8 || id == 9 || id == 10 || id == 11 || id == 51)
        return 0;
    p = mc_bpt_props(id);
    return (p.flags & BF_SOLID) != 0;
}

MC_HD static inline void mai_path_to_pos(const Blaze *e, int *bx, int *by, int *bz) {
    int id = cu_world_block(e, *bx, *by, *bz);
    if (mai_mat_air(id)) {
        int y = *by - 1;
        while (y > 0 && mai_mat_air(cu_world_block(e, *bx, y, *bz))) --y;
        if (y > 0) {
            *by = y + 1;
            return;
        }
        while (*by < 256 && mai_mat_air(cu_world_block(e, *bx, *by, *bz))) ++*by;
        return;
    }
    /* Non-air, non-solid (water, lava, torch, fire, snow layer): Java falls
     * through to super.getPathToPos(pos) with pos unchanged. The old blaze
     * code walked DOWN here, landing a panicking mob on the lakebed instead
     * of the water surface. */
    if (!mai_mat_solid(id)) return;
    while (*by < 256 && mai_mat_solid(cu_world_block(e, *bx, *by, *bz))) ++*by;
}

MC_HD static inline void mai_fill_pf(Blaze *e, int i, int *ox, int *oy, int *oz) {
    RlSnapMob *m = &e->mobs[i];
    float width, height;
    int x, y, z;
    memset(e->pf.blocks, 0, sizeof e->pf.blocks);
    e->pf.overflow = 0;
    *ox = mc_floor(m->x) - 16;
    *oy = mc_floor(m->y) - 8;
    *oz = mc_floor(m->z) - 16;
    if (*oy < 0) *oy = 0;
    for (y = 0; y < PNP_DY; ++y)
        for (z = 0; z < PNP_DZ; ++z)
            for (x = 0; x < PNP_DX; ++x)
                pnp_setblock(e->pf.blocks, x, y, z,
                             mai_mc_to_pb(cu_world_block(e, *ox + x, *oy + y, *oz + z)));
    memset(&e->pf.ent, 0, sizeof e->pf.ent);
    mai_size(m->type, &width, &height);
    e->pf.ent.width = width;
    e->pf.ent.height = height;
    e->pf.ent.stepHeight = 0.6f;
    e->pf.ent.canSwim = 0;
    e->pf.ent.canEnterDoors = 1;
    e->pf.ent.canBreakDoors = 0;
    e->pf.ent.maxFallHeight = 3;
    e->pf.ent.onGround = m->on_ground ? 1 : 0;
    e->pf.ent.inWater = mai_in_material(e, m, 0);
    e->pf.ent.posX = m->x - (double)(*ox);
    e->pf.ent.posY = m->y - (double)(*oy);
    e->pf.ent.posZ = m->z - (double)(*oz);
    pnp_ent_default_priorities(&e->pf.ent);
    if (m->type == EW_TYPE_BLAZE) {
        e->pf.ent.pathPriority[PNT_WATER] = -1.0f;
        e->pf.ent.pathPriority[PNT_LAVA] = 8.0f;
        e->pf.ent.pathPriority[PNT_DANGER_FIRE] = 0.0f;
        e->pf.ent.pathPriority[PNT_DAMAGE_FIRE] = 0.0f;
    }
}

MC_HD static inline int mai_position_clear(const Blaze *e, int ox, int oy, int oz,
                                           int x, int y, int z, int sizeX, int sizeY, int sizeZ,
                                           double vx, double vz, double d0, double d1) {
    int ix, iy, iz;
    for (ix = x; ix < x + sizeX; ++ix)
        for (iy = y; iy < y + sizeY; ++iy)
            for (iz = z; iz < z + sizeZ; ++iz) {
                double dx = (double)ix + 0.5 - vx;
                double dz = (double)iz + 0.5 - vz;
                if (dx * d0 + dz * d1 >= 0.0) {
                    int id = pnp_getblock(e->pf.blocks, ix - ox, iy - oy, iz - oz);
                    if (!pnp_blockdef(id).isPassable) return 0;
                }
            }
    return 1;
}

MC_HD static inline int mai_safe_stand(Blaze *e, int ox, int oy, int oz,
                                       int x, int y, int z, int sizeX, int sizeY, int sizeZ,
                                       double vx, double vz, double d0, double d1) {
    int i = x - sizeX / 2;
    int j = z - sizeZ / 2;
    int k, l;
    if (!mai_position_clear(e, ox, oy, oz, i, y, j, sizeX, sizeY, sizeZ, vx, vz, d0, d1))
        return 0;
    for (k = i; k < i + sizeX; ++k) {
        for (l = j; l < j + sizeZ; ++l) {
            double dx = (double)k + 0.5 - vx;
            double dz = (double)l + 0.5 - vz;
            if (dx * d0 + dz * d1 >= 0.0) {
                int t, t2;
                float f;
                if (!pnp_in(k - ox, (y - 1) - oy, l - oz) ||
                    !pnp_in(k - ox, y - oy, l - oz))
                    return 0;
                t = pnp_getPathNodeTypeSize(&e->pf, k - ox, (y - 1) - oy, l - oz,
                                            sizeX, sizeY, sizeZ,
                                            e->pf.ent.canBreakDoors,
                                            e->pf.ent.canEnterDoors);
                if (t == PNT_WATER || t == PNT_LAVA || t == PNT_OPEN) return 0;
                t2 = pnp_getPathNodeTypeSize(&e->pf, k - ox, y - oy, l - oz,
                                             sizeX, sizeY, sizeZ,
                                             e->pf.ent.canBreakDoors,
                                             e->pf.ent.canEnterDoors);
                f = pnp_getPathPriority(&e->pf.ent, t2);
                if (f < 0.0f || f >= 8.0f) return 0;
                if (t2 == PNT_DAMAGE_FIRE || t2 == PNT_DANGER_FIRE || t2 == PNT_DAMAGE_OTHER)
                    return 0;
            }
        }
    }
    return 1;
}

MC_HD static inline int mai_direct_path(Blaze *e, int ox, int oy, int oz,
                                        double x1, double y1, double z1,
                                        double x2, double y2, double z2,
                                        int sizeX, int sizeY, int sizeZ) {
    int i = pnp_floor_d(x1);
    int j = pnp_floor_d(z1);
    double d0 = x2 - x1;
    double d1 = z2 - z1;
    double d2 = d0 * d0 + d1 * d1;
    double d3, d4, d5, d6, d7;
    int k, l, i1, j1, k1, l1;
    (void)y2;
    if (d2 < 1.0e-8) return 0;
    d3 = 1.0 / sqrt(d2);
    d0 *= d3;
    d1 *= d3;
    sizeX += 2;
    sizeZ += 2;
    if (!mai_safe_stand(e, ox, oy, oz, i, (int)y1, j, sizeX, sizeY, sizeZ, x1, z1, d0, d1))
        return 0;
    sizeX -= 2;
    sizeZ -= 2;
    d4 = 1.0 / fabs(d0);
    d5 = 1.0 / fabs(d1);
    d6 = (double)i - x1;
    d7 = (double)j - z1;
    if (d0 >= 0.0) ++d6;
    if (d1 >= 0.0) ++d7;
    d6 = d6 / d0;
    d7 = d7 / d1;
    k = d0 < 0.0 ? -1 : 1;
    l = d1 < 0.0 ? -1 : 1;
    i1 = pnp_floor_d(x2);
    j1 = pnp_floor_d(z2);
    k1 = i1 - i;
    l1 = j1 - j;
    while (k1 * k > 0 || l1 * l > 0) {
        if (d6 < d7) {
            d6 += d4;
            i += k;
            k1 = i1 - i;
        } else {
            d7 += d5;
            j += l;
            l1 = j1 - j;
        }
        if (!mai_safe_stand(e, ox, oy, oz, i, (int)y1, j, sizeX, sizeY, sizeZ, x1, z1, d0, d1))
            return 0;
    }
    return 1;
}

MC_HD static inline int mai_can_navigate(const RlSnapMob *m) {
    return m->on_ground != 0;
}

MC_HD static inline int mai_find_path(Blaze *e, int i, double tx, double ty, double tz) {
    RlSnapMob *m = &e->mobs[i];
    int ox, oy, oz, n, k;
    int bx = mc_floor(tx), by = mc_floor(ty), bz = mc_floor(tz);
    if (!mai_can_navigate(m)) {
        m->path_n = 0;
        return 0;
    }
    mai_fill_pf(e, i, &ox, &oy, &oz);
    mai_path_to_pos(e, &bx, &by, &bz);
    e->pf_calls++;
    n = pf12_findPath(&e->pf,
                      (double)((float)bx + 0.5f) - (double)ox,
                      (double)((float)by + 0.5f) - (double)oy,
                      (double)((float)bz + 0.5f) - (double)oz,
                      e->mob_follow[i] > 0.5f ? e->mob_follow[i]
                                              : mai_follow_range(m->type));
    if (n > 0 && !pnp_in(bx - ox, by - oy, bz - oz)) {
        int sx = e->pf.resultPts[0], sy = e->pf.resultPts[1], sz = e->pf.resultPts[2];
        int ex = e->pf.resultPts[(n - 1) * 3 + 0];
        int ey = e->pf.resultPts[(n - 1) * 3 + 1];
        int ez = e->pf.resultPts[(n - 1) * 3 + 2];
        int md = (ex > sx ? ex - sx : sx - ex)
               + (ey > sy ? ey - sy : sy - ey)
               + (ez > sz ? ez - sz : sz - ez);
        if (md <= 1) n = 0;
    }
    if (n <= 0) {
        m->path_n = 0;
        return 0;
    }
    if (n > PAI_NAV_MAX) n = PAI_NAV_MAX;
    e->pf_paths++;
    m->path_n = (unsigned char)n;
    m->path_i = 0;
    for (k = 0; k < n; ++k) {
        m->path_x[k] = (short)(e->pf.resultPts[k * 3 + 0] + ox);
        m->path_y[k] = (short)(e->pf.resultPts[k * 3 + 1] + oy);
        m->path_z[k] = (short)(e->pf.resultPts[k * 3 + 2] + oz);
    }
    return 1;
}

MC_HD static inline int mai_set_path(Blaze *e, int i, double x, double y, double z, double speed) {
    e->mob_nav_speed[i] = speed;
    if (!mai_find_path(e, i, x, y, z)) {
        e->mobs[i].path_n = 0;
        return 0;
    }
    return 1;
}

MC_HD static inline int mai_path_done(const Blaze *e, const RlSnapMob *m, double tx, double ty, double tz) {
    (void)e;
    float width, height;
    mai_size(m->type, &width, &height);
    (void)height;
    float waypoint = width > 0.75f ? width * 0.5f : 0.75f - width * 0.5f;
    if (fabs(m->x - tx) < waypoint &&
        fabs(m->z - tz) < waypoint &&
        fabs(m->y - ty) < 1.0) return 1;
    return 0;
}

MC_HD static inline void mai_nav_follow(Blaze *e, int i) {
    RlSnapMob *m = &e->mobs[i];
    float width, height, maxDist;
    int idx, n, j, same_y_end, k, l, i1, ox, oy, oz;
    double ex, ey, ez;
    mai_size(m->type, &width, &height);
    maxDist = width > 0.75f ? width / 2.0f : 0.75f - width / 2.0f;
    idx = m->path_i;
    n = m->path_n;
    ex = m->x;
    ey = (double)((int)(m->y + 0.5));
    ez = m->z;
    same_y_end = n;
    for (j = idx; j < n; ++j) {
        if ((double)m->path_y[j] != floor(ey)) {
            same_y_end = j;
            break;
        }
    }
    if (idx < n) {
        double cx = (double)m->path_x[idx] + 0.5;
        double cy = (double)m->path_y[idx];
        double cz = (double)m->path_z[idx] + 0.5;
        if (fabsf((float)(m->x - cx)) < maxDist &&
            fabsf((float)(m->z - cz)) < maxDist &&
            fabs(m->y - cy) < 1.0)
            m->path_i = (unsigned char)(idx + 1);
    }
    k = mai_ceil_f(width);
    l = mai_ceil_f(height);
    i1 = k;
    if (m->path_i < n) {
        mai_fill_pf(e, i, &ox, &oy, &oz);
        for (j = same_y_end - 1; j >= (int)m->path_i; --j) {
            int off = pnp_floor_d((double)(width + 1.0f));
            double tx = (double)m->path_x[j] + (double)off * 0.5;
            double ty = (double)m->path_y[j];
            double tz = (double)m->path_z[j] + (double)off * 0.5;
            if (mai_direct_path(e, ox, oy, oz, ex, ey, ez, tx, ty, tz, k, l, i1)) {
                m->path_i = (unsigned char)j;
                break;
            }
        }
    }
}

MC_HD static inline void mai_nav_stuck(Blaze *e, int i) {
    RlSnapMob *m = &e->mobs[i];
    double ex = m->x;
    double ey = (double)((int)(m->y + 0.5));
    double ez = m->z;
    if (m->nav_ticks - m->nav_stuck_at > 100) {
        double dx = ex - m->nav_stuck_x;
        double dy = ey - m->nav_stuck_y;
        double dz = ez - m->nav_stuck_z;
        if (dx * dx + dy * dy + dz * dz < 2.25) {
            m->path_n = 0;
        }
        m->nav_stuck_at = m->nav_ticks;
        m->nav_stuck_x = ex;
        m->nav_stuck_y = ey;
        m->nav_stuck_z = ez;
    }
}

MC_HD static inline void mai_nav_update(Blaze *e, int i) {
    RlSnapMob *m = &e->mobs[i];
    int idx, n;
    if (m->path_n == 0) return;
    ++m->nav_ticks;
    if (mai_can_navigate(m)) {
        mai_nav_follow(e, i);
        mai_nav_stuck(e, i);
    } else {
        float width, height;
        int off;
        double vx, vy, vz, ey;
        mai_size(m->type, &width, &height);
        idx = m->path_i;
        n = m->path_n;
        if (idx < n) {
            off = pnp_floor_d((double)(width + 1.0f));
            vx = (double)m->path_x[idx] + (double)off * 0.5;
            vy = (double)m->path_y[idx];
            vz = (double)m->path_z[idx] + (double)off * 0.5;
            ey = (double)((int)(m->y + 0.5));
            if (ey > vy && mc_floor(m->x) == mc_floor(vx) &&
                mc_floor(m->z) == mc_floor(vz))
                m->path_i = (unsigned char)(idx + 1);
        }
    }
}

MC_HD static inline void mai_look_update(Blaze *e, int i, int looking,
                                         double look_x, double look_y, double look_z) {
    RlSnapMob *m = &e->mobs[i];
    float pitch = 0.0f;
    float head = e->mob_head_yaw[i];
    float body = m->yaw_body;
    if (looking) {
        double dx = look_x - m->x;
        double dy = look_y - (m->y + mai_eye_height(m->type));
        double dz = look_z - m->z;
        double horiz = (double)(float)sqrt(dx * dx + dz * dz);
        float target_yaw = mai_atan2_yaw(dz, dx);
        float target_pitch = -mai_deg(mc_atan2(dy, horiz));
        pitch = mai_update_rotation(0.0f, target_pitch, 40.0f);
        head = mai_update_rotation(head, target_yaw, 10.0f);
    } else {
        head = mai_update_rotation(head, body, 10.0f);
    }
    if (m->path_n > 0 && m->path_i < m->path_n) {
        float rel = mai_wrap_degrees(head - body);
        if (rel < -75.0f) head = body - 75.0f;
        if (rel > 75.0f) head = body + 75.0f;
    }
    e->mob_head_yaw[i] = head;
    m->pitch = pitch;
}

MC_HD static inline void mai_apply_current_look(Blaze *e, int i,
                                                double px, double py, double pz) {
    RlSnapMob *m = &e->mobs[i];
    if (m->task_bits & PAI_BIT(PAI_WATCH)) {
        mai_look_update(e, i, 1, px, mai_player_eye_y(py), pz);
    } else if (m->task_bits & PAI_BIT(PAI_IDLE)) {
        mai_look_update(e, i, 1,
                        m->x + m->wander_x,
                        m->y + mai_eye_height(m->type),
                        m->z + m->wander_z);
    } else {
        mai_look_update(e, i, 0, 0.0, 0.0, 0.0);
    }
}

MC_HD static inline void mai_hai_look(Blaze *e, int i,
                                      double px, double py, double pz) {
    RlSnapMob *m = &e->mobs[i];
    unsigned t = m->task_bits;
    float yaw_d = 10.0f, pitch_d = 40.0f;
    int looking = 0;
    double lx = 0.0, ly = 0.0, lz = 0.0;
    if (t & (hai_bit(HMELEE) | hai_bit(HBOW))) {
        looking = 1;
        yaw_d = 30.0f; pitch_d = 30.0f;
        lx = px; ly = mai_player_eye_y(py); lz = pz;
    } else if (t & hai_bit(HWATCH)) {
        looking = 1;
        lx = px; ly = mai_player_eye_y(py); lz = pz;
    } else if (t & hai_bit(HIDLE)) {
        looking = 1;
        lx = m->x + m->wander_x;
        ly = m->y + mai_eye_height(m->type);
        lz = m->z + m->wander_z;
    }
    {
        float pitch = 0.0f;
        float head = e->mob_head_yaw[i];
        float body = m->yaw_body;
        if (looking) {
            double dx = lx - m->x;
            double dy = ly - (m->y + mai_eye_height(m->type));
            double dz = lz - m->z;
            double horiz = (double)(float)sqrt(dx * dx + dz * dz);
            float target_yaw = mai_atan2_yaw(dz, dx);
            float target_pitch = -mai_deg(mc_atan2(dy, horiz));
            pitch = mai_update_rotation(0.0f, target_pitch, pitch_d);
            head = mai_update_rotation(head, target_yaw, yaw_d);
        } else {
            head = mai_update_rotation(head, body, 10.0f);
        }
        if (m->path_n > 0 && m->path_i < m->path_n) {
            float rel = mai_wrap_degrees(head - body);
            if (rel < -75.0f) head = body - 75.0f;
            if (rel > 75.0f) head = body + 75.0f;
        }
        e->mob_head_yaw[i] = head;
        m->pitch = pitch;
    }
}

MC_HD static inline void mai_body_update(Blaze *e, int i, double prev_x, double prev_z) {
    RlSnapMob *m = &e->mobs[i];
    double d0 = m->x - prev_x;
    double d1 = m->z - prev_z;
    if (d0 * d0 + d1 * d1 > 2.500000277905201e-7) {
        m->yaw_body = m->yaw;
        e->mob_head_yaw[i] = mai_angle_bound(m->yaw_body, e->mob_head_yaw[i], 75.0f);
        e->mob_prev_head_yaw[i] = e->mob_head_yaw[i];
        e->mob_body_ticks[i] = 0;
    } else {
        float f = 75.0f;
        float head = e->mob_head_yaw[i];
        if (fabsf(head - e->mob_prev_head_yaw[i]) > 15.0f) {
            e->mob_body_ticks[i] = 0;
            e->mob_prev_head_yaw[i] = head;
        } else {
            ++e->mob_body_ticks[i];
            if (e->mob_body_ticks[i] > 10)
                f = fmaxf(1.0f - (float)(e->mob_body_ticks[i] - 10) / 10.0f, 0.0f) * 75.0f;
        }
        m->yaw_body = mai_angle_bound(head, m->yaw_body, f);
    }
}

MC_HD static inline int mai_pai_can_use(const Blaze *e, int type, int i, int task) {
    const RlSnapMob *m = &e->mobs[i];
    int mask = mai_mutex(task);
    for (int t = 0; t < PAI_NTASKS; ++t) {
        if (t == task) continue;
        if ((m->task_bits & PAI_BIT(t)) &&
            (mai_mutex(t) & mask) &&
            mai_priority(type, t) <= mai_priority(type, task))
            return 0;
    }
    return 1;
}

MC_HD static inline int mai_pai_continue(const Blaze *e, int i, int task,
                                         double px, double py, double pz) {
    const RlSnapMob *m = &e->mobs[i];
    if (task == PAI_SWIM) {
        return mai_in_material(e, m, 0) || mai_in_material(e, m, 1);
    }
    if (task == PAI_PANIC) {
        return m->path_n > 0 && m->path_i < m->path_n;
    }
    if (task == PAI_EAT) {
        return e->mob_eat_time[i] > 0;
    }
    if (task == PAI_WANDER) {
        return m->path_n > 0 && m->path_i < m->path_n;
    }
    if (task == PAI_WATCH) {
        if (e->mob_watch_time[i] <= 0) return 0;
        double dx = px - m->x;
        double dy = py - m->y;
        double dz = pz - m->z;
        return (dx * dx + dy * dy + dz * dz) <= mai_watch_range_sq(m->type);
    }
    if (task == PAI_IDLE) {
        /* magma pai_continue (mob_live.c:1372) uses >= 0, not > 0. */
        return e->mob_idle_time[i] >= 0;
    }
    return 0;
}

MC_HD static inline void mai_pai_reset(Blaze *e, int i, int task) {
    RlSnapMob *m = &e->mobs[i];
    m->task_bits &= ~PAI_BIT(task);
    if (task == PAI_PANIC || task == PAI_WANDER) {
        m->path_n = 0;
    } else if (task == PAI_EAT) {
        e->mob_eat_time[i] = 0;
    } else if (task == PAI_WATCH) {
        e->mob_watch_time[i] = 0;
    } else if (task == PAI_IDLE) {
        e->mob_idle_time[i] = 0;
    }
}

MC_HD static inline int mai_pai_try_start(Blaze *e, int i, int task,
                                          double px, double py, double pz) {
    RlSnapMob *m = &e->mobs[i];
    int type = m->type;
    JavaRandom jr; jr.seed = m->seed48;
    int started = 0;

    if (task == PAI_SWIM) {
        if (mai_in_material(e, m, 0) || mai_in_material(e, m, 1)) {
            m->task_bits |= PAI_BIT(task);
            started = 1;
        }
    } else if (task == PAI_PANIC) {
        /* EntityAIPanic.shouldExecute, magma pai_try_start (mob_live.c:1393-1401).
         * isBurning() alone is enough to panic; the fallback wander is land=0;
         * and a failed setPath still marks the task using. */
        int burning = m->fire_ticks > 0;
        if (m->panic > 0 || burning) {
            double tx, ty, tz;
            int found = burning && mai_nearest_water(e, m, &tx, &ty, &tz);
            if (!found)
                found = mai_random_position(e, m, 5, 4, 0, &jr, &tx, &ty, &tz);
            if (found) {
                /* magma pai_jr mutates the live cursor before setPath. Write
                 * back so PathFinder cannot reload the pre-shouldExecute seed. */
                m->seed48 = jr.seed;
                (void)mai_set_path(e, i, tx, ty, tz, mai_panic_multiplier(type));
                jr.seed = m->seed48;
                m->task_bits |= PAI_BIT(task);
                started = 1;
            }
        }
    } else if (task == PAI_EAT) {
        if (jrand_int_bound(&jr, 1000) == 0) {
            int bx = mc_floor(m->x), by = mc_floor(m->y), bz = mc_floor(m->z);
            int ok = 0;
            if (cu_world_block(e, bx, by, bz) == 31 &&
                cu_world_meta(e, bx, by, bz) == 1) ok = 1;
            else if (cu_world_block(e, bx, by - 1, bz) == 2) ok = 1;
            if (ok) {
                e->mob_eat_time[i] = 40;
                m->task_bits |= PAI_BIT(task);
                m->path_n = 0;
                started = 1;
            }
        }
    } else if (task == PAI_WANDER) {
        /* EntityAIWanderAvoidWater.shouldExecute, magma pai_try_start
         * (mob_live.c:1408-1421). The 100-tick idle gate is checked BEFORE the
         * 1-in-120 roll, so an aged mob consumes no draw at all. */
        if (e->mob_entity_age[i] >= 100) {
            m->seed48 = jr.seed;
            return 0;
        }
        if (jrand_int_bound(&jr, 120) == 0) {
            double tx, ty, tz;
            int ok;
            if (mai_in_material(e, m, 0)) {
                ok = mai_random_position(e, m, 15, 7, 1, &jr, &tx, &ty, &tz);
                if (!ok)
                    ok = mai_random_position(e, m, 10, 7, 0, &jr, &tx, &ty, &tz);
            } else {
                int land = jrand_float(&jr) >= mai_avoid_water_p(m->type);
                ok = mai_random_position(e, m, 10, 7, land, &jr, &tx, &ty, &tz);
            }
            if (ok) {
                /* setPath failure does NOT cancel the task: magma marks it
                 * using and lets continueExecuting drop it next tick. Live
                 * seed48 is committed before A* the same way pai_jr is. */
                m->seed48 = jr.seed;
                (void)mai_set_path(e, i, tx, ty, tz, 1.0);
                jr.seed = m->seed48;
                m->task_bits |= PAI_BIT(task);
                started = 1;
            }
        }
    } else if (task == PAI_WATCH) {
        if (jrand_float(&jr) < 0.02f) {
            double dx = px - m->x;
            double dy = py - m->y;
            double dz = pz - m->z;
            if ((dx * dx + dy * dy + dz * dz) <= mai_watch_range_sq(type)) {
                e->mob_watch_time[i] = 40 + jrand_int_bound(&jr, 40);
                m->task_bits |= PAI_BIT(task);
                started = 1;
            }
        }
    } else if (task == PAI_IDLE) {
        if (jrand_float(&jr) < 0.02f) {
            double ang = 2.0 * MC_PI * jrand_double(&jr);
            m->wander_x = cos(ang);
            m->wander_z = sin(ang);
            e->mob_idle_time[i] = 20 + jrand_int_bound(&jr, 20);
            m->task_bits |= PAI_BIT(task);
            started = 1;
        }
    }

    m->seed48 = jr.seed;
    return started;
}

MC_HD static inline void mai_pai_tick(Blaze *e, int i,
                                      double px, double py, double pz, int mob_griefing,
                                      int *moving, int *jump, int *wandering, int *swim_jump,
                                      double *nav_speed) {
    RlSnapMob *m = &e->mobs[i];
    int type = m->type;
    int setup = (e->mob_task_tick[i]++ % 3) == 0;

    if (setup && type == EW_TYPE_BLAZE) {
        JavaRandom jr; jr.seed = m->seed48;
        (void)jrand_int_bound(&jr, 10);
        m->seed48 = jr.seed;
    }
    if (setup && type == EW_TYPE_ENDERMAN) {
        JavaRandom jr; jr.seed = m->seed48;
        (void)jrand_int_bound(&jr, 10);
        m->seed48 = jr.seed;
    }

    for (int task = 0; task < PAI_NTASKS; ++task) {
        if (mai_priority(type, task) >= 99) continue;
        int using_task = (m->task_bits & PAI_BIT(task)) != 0;
        if (setup) {
            if (using_task) {
                if (!mai_pai_can_use(e, type, i, task) ||
                    !mai_pai_continue(e, i, task, px, py, pz))
                    mai_pai_reset(e, i, task);
            } else if (mai_pai_can_use(e, type, i, task)) {
                (void)mai_pai_try_start(e, i, task, px, py, pz);
            }
        } else if (using_task && !mai_pai_continue(e, i, task, px, py, pz)) {
            mai_pai_reset(e, i, task);
        }
    }

    for (int task = 0; task < PAI_NTASKS; ++task) {
        if (!(m->task_bits & PAI_BIT(task))) continue;
        if (task == PAI_SWIM) {
            JavaRandom jr; jr.seed = m->seed48;
            if (jrand_float(&jr) < 0.8f) *swim_jump = 1;
            m->seed48 = jr.seed;
        } else if (task == PAI_EAT) {
            if (e->mob_eat_time[i] > 0) --e->mob_eat_time[i];
            if (e->mob_eat_time[i] == 4) {
                int bx = mc_floor(m->x), by = mc_floor(m->y), bz = mc_floor(m->z);
                if (cu_world_block(e, bx, by, bz) == 31 &&
                    cu_world_meta(e, bx, by, bz) == 1) {
                    if (mob_griefing) cu_world_set_state(e, bx, by, bz, 0, 0);
                    e->mob_cstate[i] = 0; /* eatGrassBonus resets sheared */
                } else if (cu_world_block(e, bx, by - 1, bz) == 2) {
                    if (mob_griefing) cu_world_set_state(e, bx, by - 1, bz, 3, 0);
                    e->mob_cstate[i] = 0;
                }
            }
        } else if (task == PAI_WATCH) {
            --e->mob_watch_time[i];
        } else if (task == PAI_IDLE) {
            --e->mob_idle_time[i];
        }
    }

    if (m->path_n > 0)
        mai_nav_update(e, i);

    if (type == EW_TYPE_BLAZE) {
        --e->mob_blaze_hot[i];
        if (e->mob_blaze_hot[i] <= 0) {
            e->mob_blaze_hot[i] = 100;
            e->mob_blaze_hof[i] = 0.5f + (float)mai_gaussian(m) * 3.0f;
        }
    }

    *moving = (m->path_n > 0 && m->path_i < m->path_n);
    *wandering = *moving && (m->task_bits & PAI_BIT(PAI_WANDER));
    *jump = 0;
    *nav_speed = *moving ? e->mob_nav_speed[i] : 0.0;
    /* magma pai_tick (mob_live.c:1601): revenge timer counts down AFTER
     * shouldExecute, so a planted panic=1 still paths on that setup tick. */
    if (m->panic > 0) --m->panic;
}

MC_HD static inline int mai_hai_can_use(const Blaze *e, int type, int i, int task) {
    unsigned mutex = (unsigned)hai_mutex(task);
    int pri = hai_pri(type, task), other;
    for (other = 0; other < HN; ++other) {
        if (other == task || !(e->mobs[i].task_bits & hai_bit(other))) continue;
        if (pri >= hai_pri(type, other) && (mutex & (unsigned)hai_mutex(other))) return 0;
    }
    return 1;
}

MC_HD static inline int mai_hai_player_range(const Blaze *e, int i, double px, double py, double pz) {
    const RlSnapMob *m = &e->mobs[i];
    double fr = hai_follow(m->type);
    double dx = px - m->x, dz = pz - m->z;
    double dy = mai_player_eye_y(py) - (m->y + mai_eye_height(m->type));
    if (dx * dx + dz * dz > fr * fr) return 0;
    if (fabs(dy) > fr) return 0;
    return 1;
}

MC_HD static inline void mai_hai_clear_nav(Blaze *e, int i) {
    e->mobs[i].path_n = 0;
}

MC_HD static inline int mai_hai_continue(const Blaze *e, int i, int task,
                                         double px, double py, double pz) {
    const RlSnapMob *m = &e->mobs[i];
    if (task == HSWIM) return mai_in_material(e, m, 0) || mai_in_material(e, m, 1);
    if (task == HMELEE) return (m->target_idx != 0) && (m->path_n > 0 && m->path_i < m->path_n);
    if (task == HSWELL) return 1;
    if (task == HBOW) return m->target_idx != 0;
    if (task == HREST) return 0;
    if (task == HFLEE || task == HAVOID || task == HHOME || task == HVILL || task == HWAND)
        return (m->path_n > 0 && m->path_i < m->path_n);
    if (task == HWATCH) {
        double dx = px - m->x, dy = py - m->y, dz = pz - m->z;
        return (dx * dx + dy * dy + dz * dz <= 64.0) && (e->mob_watch_time[i] > 0);
    }
    if (task == HIDLE) return e->mob_idle_time[i] >= 0;
    return 0;
}

MC_HD static inline void mai_hai_reset(Blaze *e, int i, int task) {
    RlSnapMob *m = &e->mobs[i];
    m->task_bits &= ~hai_bit(task);
    if (task == HWATCH) e->mob_watch_time[i] = 0;
    if (task == HMELEE || task == HWAND || task == HBOW)
        mai_hai_clear_nav(e, i);
    if (task == HMELEE) e->mob_raise_arm[i] = 0;
    if (task == HBOW) {
        m->see_time = 0;
        m->bow_attack_time = -1;
        m->stime = -1;
    }
}

MC_HD static inline int mai_hai_try_start(Blaze *e, int i, int task,
                                          double px, double py, double pz, int day) {
    RlSnapMob *m = &e->mobs[i];
    int type = m->type;
    (void)day;
    if (task == HSWIM)
        return mai_pai_try_start(e, i, PAI_SWIM, px, py, pz);
    if (task == HMELEE) {
        if (!m->target_idx) return 0;
        if (!mai_find_path(e, i, px, py, pz)) {
            float width, height;
            double reach, ddx, ddz, ddy;
            mai_size(type, &width, &height);
            (void)height;
            reach = (double)(width * 2.0f * width * 2.0f + 0.6f);
            ddx = px - m->x; ddz = pz - m->z;
            ddy = py - m->y;
            if (ddx * ddx + ddz * ddz + ddy * ddy > reach) return 0;
        }
        e->mob_nav_speed[i] = type == EW_TYPE_SKELETON ? 1.2 : 1.0;
        m->melee_delay = 0;
        e->mob_melee_tx[i] = e->mob_melee_ty[i] = e->mob_melee_tz[i] = 0.0;
        e->mob_raise_arm[i] = 0;
        m->task_bits |= hai_bit(HMELEE);
        return 1;
    }
    if (task == HSWELL) {
        double dx = px - m->x, dy = py - m->y, dz = pz - m->z;
        if (e->mob_cstate[i] <= 0 && !(m->target_idx && dx * dx + dy * dy + dz * dz < 9.0))
            return 0;
        mai_hai_clear_nav(e, i);
        m->task_bits |= hai_bit(HSWELL);
        return 1;
    }
    if (task == HBOW) {
        if (!m->target_idx || type != EW_TYPE_SKELETON) return 0;
        e->mob_nav_speed[i] = 1.0;
        m->task_bits |= hai_bit(HBOW);
        return 1;
    }
    if (task == HREST || task == HFLEE || task == HAVOID || task == HHOME || task == HVILL)
        return 0;
    if (task == HWAND) {
        int ok = mai_pai_try_start(e, i, PAI_WANDER, px, py, pz);
        if (ok && type == EW_TYPE_CREEPER) e->mob_nav_speed[i] = 0.8;
        return ok;
    }
    if (task == HWATCH) {
        JavaRandom jr; jr.seed = m->seed48;
        if (jrand_float(&jr) >= 0.02f) {
            m->seed48 = jr.seed;
            return 0;
        }
        double dx = px - m->x, dy = py - m->y, dz = pz - m->z;
        if (dx * dx + dy * dy + dz * dz > 64.0) {
            m->seed48 = jr.seed;
            return 0;
        }
        e->mob_watch_time[i] = 40 + jrand_int_bound(&jr, 40);
        m->task_bits |= hai_bit(HWATCH);
        m->seed48 = jr.seed;
        return 1;
    }
    if (task == HIDLE)
        return mai_pai_try_start(e, i, PAI_IDLE, px, py, pz);
    return 0;
}

MC_HD static inline void mai_hai_melee_update(Blaze *e, int i,
                                              double px, double py, double pz) {
    RlSnapMob *m = &e->mobs[i];
    int see, fire_repath;
    double d0, moved;
    --m->melee_delay;
    see = ml_los_clear(e, m->x, m->y + mai_eye_height(m->type), m->z,
                       px, mai_player_eye_y(py), pz);
    d0 = (px - m->x) * (px - m->x) + (py - m->y) * (py - m->y) + (pz - m->z) * (pz - m->z);
    moved = (px - e->mob_melee_tx[i]) * (px - e->mob_melee_tx[i])
          + (py - e->mob_melee_ty[i]) * (py - e->mob_melee_ty[i])
          + (pz - e->mob_melee_tz[i]) * (pz - e->mob_melee_tz[i]);
    fire_repath = 0;
    JavaRandom jr; jr.seed = m->seed48;
    if (see && m->melee_delay <= 0) {
        if (e->mob_melee_tx[i] == 0.0 && e->mob_melee_ty[i] == 0.0 && e->mob_melee_tz[i] == 0.0)
            fire_repath = 1;
        else if (moved >= 1.0)
            fire_repath = 1;
        else if (jrand_float(&jr) < 0.05f)
            fire_repath = 1;
    }
    if (fire_repath) {
        e->mob_melee_tx[i] = px;
        e->mob_melee_ty[i] = py;
        e->mob_melee_tz[i] = pz;
        m->melee_delay = 4 + jrand_int_bound(&jr, 7);
        if (d0 > 1024.0) m->melee_delay += 10;
        else if (d0 > 256.0) m->melee_delay += 5;
        if (!mai_find_path(e, i, px, py, pz))
            m->melee_delay += 15;
        e->mob_nav_speed[i] = (m->type == EW_TYPE_SKELETON ? 1.2 : 1.0);
    }
    m->seed48 = jr.seed;
    ++e->mob_raise_arm[i];
}

MC_HD static inline void mai_hai_bow_update(Blaze *e, int i,
                                            double px, double py, double pz) {
    RlSnapMob *m = &e->mobs[i];
    int see;
    double d0;
    see = ml_los_clear(e, m->x, m->y + mai_eye_height(m->type), m->z,
                       px, mai_player_eye_y(py), pz);
    d0 = (px - m->x) * (px - m->x) + (py - m->y) * (py - m->y) + (pz - m->z) * (pz - m->z);
    if (see) {
        if (m->see_time < 0) m->see_time = 0;
        ++m->see_time;
    } else {
        if (m->see_time > 0) m->see_time = 0;
        --m->see_time;
    }
    if (d0 <= 225.0 && m->see_time >= 20) {
        mai_hai_clear_nav(e, i);
        ++m->stime;
    } else {
        (void)mai_find_path(e, i, px, py, pz);
        e->mob_nav_speed[i] = 1.0;
        m->stime = -1;
    }
    JavaRandom jr; jr.seed = m->seed48;
    if (m->stime >= 20) {
        if ((double)jrand_float(&jr) < 0.3)
            e->mob_strafe_cw[i] = (unsigned char)!e->mob_strafe_cw[i];
        if ((double)jrand_float(&jr) < 0.3)
            e->mob_strafe_back[i] = (unsigned char)!e->mob_strafe_back[i];
        m->stime = 0;
    }
    if (m->stime > -1) {
        if (d0 > 225.0 * 0.75) e->mob_strafe_back[i] = 0;
        else if (d0 < 225.0 * 0.25) e->mob_strafe_back[i] = 1;
    }
    if (e->mob_raise_arm[i] > 0) {
        if (!see && m->see_time < -60) e->mob_raise_arm[i] = 0;
        else if (see) {
            ++e->mob_raise_arm[i];
            if (e->mob_raise_arm[i] >= 20) {
                (void)jrand_float(&jr); /* shoot playSound pitch */
                e->mob_raise_arm[i] = 0;
                m->bow_attack_time = 40;
            }
        }
    } else if (--m->bow_attack_time <= 0 && m->see_time >= -60) {
        e->mob_raise_arm[i] = 1;
    }
    m->seed48 = jr.seed;
}

MC_HD static inline void mai_hai_swell_update(Blaze *e, int i,
                                              double px, double py, double pz) {
    RlSnapMob *m = &e->mobs[i];
    double dx = px - m->x, dy = py - m->y, dz = pz - m->z;
    double d2 = dx * dx + dy * dy + dz * dz;
    int see = ml_los_clear(e, m->x, m->y + mai_eye_height(m->type), m->z,
                           px, mai_player_eye_y(py), pz);
    if (!m->target_idx || d2 > 49.0 || !see) e->mob_cstate[i] = -1;
    else e->mob_cstate[i] = 1;
}

MC_HD static inline void mai_hai_target_tick(Blaze *e, int i,
                                             double px, double py, double pz) {
    RlSnapMob *m = &e->mobs[i];
    int type = m->type;
    int setup = (e->mob_target_tick[i]++ % 3) == 0;
    unsigned using_t = m->target_tasks;
    int order[4];
    int n = 0, k;
    if (type == EW_TYPE_CREEPER) { order[n++] = 2; order[n++] = 1; }
    else {
        order[n++] = 1;
        order[n++] = 2;
        if (type == EW_TYPE_ZOMBIE) order[n++] = 4;
        if (type == EW_TYPE_ZOMBIE || type == EW_TYPE_SKELETON) order[n++] = 8;
    }
    JavaRandom jr; jr.seed = m->seed48;
    for (k = 0; k < n; ++k) {
        unsigned bit = (unsigned)order[k];
        int is_using = (using_t & bit) != 0;
        int mutex_ok = 1;
        unsigned o;
        for (o = 1; o <= 8; o <<= 1) {
            if (o == bit) continue;
            if ((using_t & o) && 1) { mutex_ok = 0; break; }
        }
        if (!setup) {
            if (is_using) {
                int keep = 0;
                if (bit == HT_PLAYER)
                    keep = m->target_idx && mai_hai_player_range(e, i, px, py, pz);
                if (!keep) {
                    using_t &= ~bit;
                    if (bit == HT_PLAYER) m->target_idx = 0;
                }
            }
            continue;
        }
        if (is_using) {
            int keep = (bit == HT_PLAYER) && m->target_idx && mai_hai_player_range(e, i, px, py, pz);
            if (!keep) {
                using_t &= ~bit;
                if (bit == HT_PLAYER) m->target_idx = 0;
            }
        } else if (mutex_ok) {
            if (bit == HT_HURT) {
                /* no revenge in these tapes */
            } else {
                if (jrand_int_bound(&jr, 10) == 0) {
                    if (bit == HT_PLAYER && mai_hai_player_range(e, i, px, py, pz) &&
                        ml_los_clear(e, m->x, m->y + mai_eye_height(type), m->z,
                                     px, mai_player_eye_y(py), pz)) {
                        using_t |= HT_PLAYER;
                        m->target_idx = 1;
                    }
                }
            }
        }
    }
    m->target_tasks = using_t;
    m->seed48 = jr.seed;
}

MC_HD static inline void mai_hai_living(Blaze *e, int i, int day) {
    RlSnapMob *m = &e->mobs[i];
    int type = m->type;
    int lst = e->mob_living_sound[i];
    JavaRandom jr; jr.seed = m->seed48;
    int sound_draw = jrand_int_bound(&jr, 1000);
    e->mob_living_sound[i] = lst + 1;
    if (sound_draw < lst) {
        e->mob_living_sound[i] = -80;
        if (type != EW_TYPE_CREEPER) {
            (void)jrand_float(&jr);
            (void)jrand_float(&jr);
        }
    }
    if (day && (type == EW_TYPE_ZOMBIE || type == EW_TYPE_SKELETON)) {
        float f = mai_brightness(e, mc_floor(m->x), mc_floor(m->y), mc_floor(m->z));
        if (f > 0.5f) {
            float nf = jrand_float(&jr);
            if (nf * 30.0f < (f - 0.4f) * 2.0f &&
                ml_sky_exposed(e, m->x, m->y, m->z))
                m->fire_ticks = 160;
        }
    }
    e->mob_entity_age[i] = 0;
    m->seed48 = jr.seed;
}

MC_HD static inline void mai_hai_tick(Blaze *e, int i,
                                      double px, double py, double pz, int day,
                                      int *moving, int *jump, int *wandering, int *swim_jump,
                                      double *nav_speed, int mob_griefing) {
    RlSnapMob *m = &e->mobs[i];
    int type = m->type;
    const int *goals = hai_goals(type, e->mob_skel_melee[i]);
    int setup, g;
    if (type == EW_TYPE_CREEPER) {
        m->swell += (int)e->mob_cstate[i];
        if (m->swell < 0) m->swell = 0;
        if (m->swell >= 30) {
            m->alive = 0;
            m->type = EW_TYPE_NONE;
            m->swell = 0;
            e->explosion_pending = 1;
            e->explosion_x = m->x;
            e->explosion_y = m->y;
            e->explosion_z = m->z;
            e->explosion_size = exl_creeper_size(0);
            e->explosion_smoking = mob_griefing ? 1 : 0;
            e->explosion_flaming = 0;
            cu_explode(e, e->explosion_x, e->explosion_y, e->explosion_z,
                       e->explosion_size, e->explosion_smoking,
                       e->explosion_flaming);
            e->explosion_pending = 0;
            *moving = 0; *jump = 0; *wandering = 0; *swim_jump = 0; *nav_speed = 0.0;
            return;
        }
    }
    setup = (e->mob_task_tick[i]++ % 3) == 0;
    mai_hai_target_tick(e, i, px, py, pz);
    for (g = 0; goals[g] >= 0; ++g) {
        int task = goals[g];
        int using_task = (m->task_bits & hai_bit(task)) != 0;
        if (setup) {
            if (using_task) {
                if (!mai_hai_can_use(e, type, i, task) ||
                    !mai_hai_continue(e, i, task, px, py, pz))
                    mai_hai_reset(e, i, task);
            } else if (mai_hai_can_use(e, type, i, task)) {
                (void)mai_hai_try_start(e, i, task, px, py, pz, day);
            }
        } else if (using_task && !mai_hai_continue(e, i, task, px, py, pz)) {
            mai_hai_reset(e, i, task);
        }
    }
    *swim_jump = 0;
    if (m->task_bits & hai_bit(HSWIM)) {
        JavaRandom jr; jr.seed = m->seed48;
        if (jrand_float(&jr) < 0.8f) *swim_jump = 1;
        m->seed48 = jr.seed;
    }
    if (m->task_bits & hai_bit(HWATCH)) --e->mob_watch_time[i];
    if (m->task_bits & hai_bit(HIDLE)) --e->mob_idle_time[i];
    if (m->task_bits & hai_bit(HMELEE))
        mai_hai_melee_update(e, i, px, py, pz);
    if (m->task_bits & hai_bit(HBOW))
        mai_hai_bow_update(e, i, px, py, pz);
    if (m->task_bits & hai_bit(HSWELL))
        mai_hai_swell_update(e, i, px, py, pz);

    if (m->path_n > 0)
        mai_nav_update(e, i);

    *moving = (m->path_n > 0 && m->path_i < m->path_n);
    if ((m->task_bits & hai_bit(HBOW)) && m->stime > -1)
        *moving = 1;
    *wandering = *moving && (m->task_bits & hai_bit(HWAND));
    *jump = 0;
    *nav_speed = *moving ? e->mob_nav_speed[i] : 0.0;
}

MC_HD static inline void mai_living_aabb(const Blaze *e, int i, McAABB *out) {
    const RlSnapMob *m = &e->mobs[i];
    if (m->box_on) {
        *out = mc_aabb_make(m->box_minx, m->box_miny, m->box_minz,
                            m->box_maxx, m->box_maxy, m->box_maxz);
        return;
    }
    float w, h;
    mai_size(m->type, &w, &h);
    *out = mc_aabb_make(m->x - (double)w * 0.5, m->y, m->z - (double)w * 0.5,
                        m->x + (double)w * 0.5, m->y + (double)h,
                        m->z + (double)w * 0.5);
}

MC_HD static inline void mai_apply_collision_vel(double ax, double az, double bx, double bz,
                                                double *avx, double *avz, double *bvx, double *bvz) {
    double d0 = bx - ax;
    double d1 = bz - az;
    double ad0 = d0 < 0.0 ? -d0 : d0;
    double ad1 = d1 < 0.0 ? -d1 : d1;
    double d2 = ad0 > ad1 ? ad0 : ad1;
    double d3;
    if (d2 < 0.009999999776482582) return;
    d2 = (double)(float)sqrt(d2);
    d0 /= d2;
    d1 /= d2;
    d3 = 1.0 / d2;
    if (d3 > 1.0) d3 = 1.0;
    d0 *= d3;
    d1 *= d3;
    d0 *= 0.05000000074505806;
    d1 *= 0.05000000074505806;
    if (avx) {
        *avx -= d0;
        *avz -= d1;
    }
    if (bvx) {
        *bvx += d0;
        *bvz += d1;
    }
}

MC_HD static inline void mai_collide_nearby(Blaze *e, int i, const McAABB *player_bb, double px, double pz) {
    McAABB self;
    unsigned j;
    RlSnapMob *m = &e->mobs[i];
    if (!m->alive || !mai_det_living(m->type)) return;
    mai_living_aabb(e, i, &self);
    for (j = 0; j < e->n_mobs; ++j) {
        McAABB other;
        RlSnapMob *oj = &e->mobs[j];
        if ((int)j == i || !oj->alive || !mai_det_living(oj->type)) continue;
        mai_living_aabb(e, j, &other);
        if (!mc_aabb_intersects(&self, &other)) continue;
        mai_apply_collision_vel(m->x, m->z, oj->x, oj->z,
                                &m->mx, &m->mz, &oj->mx, &oj->mz);
    }
    if (player_bb && mc_aabb_intersects(&self, player_bb))
        mai_apply_collision_vel(m->x, m->z, px, pz,
                                &m->mx, &m->mz, NULL, NULL);
}

MC_HD static inline void mai_player_collide_mobs(Blaze *e, const McAABB *player_bb, double px, double pz) {
    unsigned i;
    for (i = 0; i < e->n_mobs; ++i) {
        McAABB mob;
        RlSnapMob *m = &e->mobs[i];
        if (!m->alive || !mai_det_living(m->type)) continue;
        mai_living_aabb(e, i, &mob);
        if (!mc_aabb_intersects(player_bb, &mob)) continue;
        mai_apply_collision_vel(px, pz, m->x, m->z,
                                NULL, NULL, &m->mx, &m->mz);
    }
}

MC_HD static inline int mai_collect_blocks(const Blaze *e, const McAABB *q, PcfBlock *out, int cap) {
    int n = 0;
    int x0 = mc_floor(q->minX) - 1, x1 = mc_floor(q->maxX) + 1;
    int y0 = mc_floor(q->minY) - 1, y1 = mc_floor(q->maxY) + 1;
    int z0 = mc_floor(q->minZ) - 1, z1 = mc_floor(q->maxZ) + 1;
    if (y0 < 0) y0 = 0;
    if (y1 > 255) y1 = 255;
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z) {
                int id = cu_world_block(e, x, y, z);
                if (!ml_solid_id(id)) continue;
                if (n == cap) return n;
                out[n].block_id = id;
                out[n].ox = x; out[n].oy = y; out[n].oz = z;
                out[n].ladder_facing = 0;
                ++n;
            }
    return n;
}

MC_HD static inline void mai_move_mob(Blaze *e, const McSinTable *st, int i,
                                      int moving, int jump, int swim_jump, double nav_speed,
                                      double *prev_x, double *prev_z) {
    RlSnapMob *m = &e->mobs[i];
    EhsIntent intent;
    EbLiving liv;
    PcfBlock blocks[ESS_MOB_BLOCKS];
    int type = m->type;

    *prev_x = m->x;
    *prev_z = m->z;

    intent.yaw = m->yaw;
    intent.moveForward = 0.0f;
    intent.moveStrafing = 0.0f;
    intent.isJumping = 0;

    double tx = 0.0, ty = 0.0, tz = 0.0;
    if (moving && m->path_n > 0 && m->path_i < m->path_n) {
        float width, height;
        mai_size(type, &width, &height);
        int off = pnp_floor_d((double)(width + 1.0f));
        tx = (double)m->path_x[m->path_i] + (double)off * 0.5;
        ty = (double)m->path_y[m->path_i];
        tz = (double)m->path_z[m->path_i] + (double)off * 0.5;

        double ddx = tx - m->x;
        double ddy = ty - m->y;
        double ddz = tz - m->z;
        if (ddx * ddx + ddy * ddy + ddz * ddz >= 2.500000277905201e-7) {
            intent.yaw = mai_limit_angle(m->yaw, mai_atan2_yaw(ddz, ddx), 90.0f);
        } else {
            moving = 0;
            intent.yaw = m->yaw;
        }
    } else if (!moving && !(hai_ok(type) && (m->task_bits & 256u) && m->stime > -1)) {
        moving = 0;
    }

    if (moving && jump) intent.isJumping = 1;

    /* Load living */
    float w, h;
    mai_size(type, &w, &h);
    elb_init(&liv, w, h, m->x, m->y, m->z);
    liv.base.phys.motionX = m->mx;
    liv.base.phys.motionY = m->my;
    liv.base.phys.motionZ = m->mz;
    liv.base.phys.onGround = m->on_ground ? 1 : 0;
    liv.base.rotationYaw = intent.yaw;
    liv.jumpMovementFactor = 0.02f;
    liv.isServerWorld = 1;
    liv.jumpTicks = 0;

    if (m->box_on) {
        liv.base.phys.box = mc_aabb_make(m->box_minx, m->box_miny, m->box_minz,
                                         m->box_maxx, m->box_maxy, m->box_maxz);
    }

    float ai_speed = (float)(nav_speed * mai_attribute_speed(type));
    liv.landMovementFactor = ai_speed;
    liv.moveForward = moving ? ai_speed : 0.0f;
    liv.moveStrafing = 0.0f;
    if (hai_ok(type) && (m->task_bits & 256u) && m->stime > -1) {
        liv.moveForward = (e->mob_strafe_back[i] ? -0.5f : 0.5f) * ai_speed;
        liv.moveStrafing = (e->mob_strafe_cw[i] ? 0.5f : -0.5f) * ai_speed;
    }
    if (moving) {
        double ddx = tx - m->x;
        double ddy = ty - m->y;
        double ddz = tz - m->z;
        if (ddy > liv.base.phys.stepHeight && ddx * ddx + ddz * ddz < fmax(1.0, (double)liv.base.width))
            liv.isJumping = 1;
    }

    /* EntityBlaze slow fall */
    if (type == EW_TYPE_BLAZE && !liv.base.phys.onGround && liv.base.phys.motionY < 0.0)
        liv.base.phys.motionY *= 0.6;

    /* Fluid travel */
    if (mai_in_material(e, m, 0) || mai_in_material(e, m, 1)) {
        int in_water = mai_in_material(e, m, 0);
        eb_on_entity_update(&liv.base);
        if (fabs(liv.base.phys.motionX) < 0.003) liv.base.phys.motionX = 0.0;
        if (fabs(liv.base.phys.motionY) < 0.003) liv.base.phys.motionY = 0.0;
        if (fabs(liv.base.phys.motionZ) < 0.003) liv.base.phys.motionZ = 0.0;
        if (swim_jump) liv.base.phys.motionY += 0.03999999910593033;
        liv.moveStrafing *= 0.98f;
        liv.moveForward *= 0.98f;
        eb_move_relative(&liv.base, liv.moveStrafing, liv.moveForward, 0.02f, st);
        McAABB fq = mc_aabb_addcoord(&liv.base.phys.box, liv.base.phys.motionX,
                                     liv.base.phys.motionY, liv.base.phys.motionZ);
        fq.minY -= liv.base.phys.stepHeight; fq.maxY += liv.base.phys.stepHeight;
        int fn = mai_collect_blocks(e, &fq, blocks, ESS_MOB_BLOCKS);
        eb_move(&liv.base, liv.base.phys.motionX, liv.base.phys.motionY,
                liv.base.phys.motionZ, blocks, fn);
        double drag = in_water ? 0.800000011920929 : 0.5;
        liv.base.phys.motionX *= drag;
        liv.base.phys.motionY *= drag;
        liv.base.phys.motionZ *= drag;
        if (!liv.base.hasNoGravity) liv.base.phys.motionY -= 0.02;

        m->x = liv.base.phys.posX;
        m->y = liv.base.phys.posY;
        m->z = liv.base.phys.posZ;
        m->mx = liv.base.phys.motionX;
        m->my = liv.base.phys.motionY;
        m->mz = liv.base.phys.motionZ;
        m->on_ground = liv.base.phys.onGround ? 1 : 0;
        m->yaw = liv.base.rotationYaw;
        m->box_minx = liv.base.phys.box.minX;
        m->box_miny = liv.base.phys.box.minY;
        m->box_minz = liv.base.phys.box.minZ;
        m->box_maxx = liv.base.phys.box.maxX;
        m->box_maxy = liv.base.phys.box.maxY;
        m->box_maxz = liv.base.phys.box.maxZ;
        m->box_on = 1;
        return;
    }

    float slip = 0.6f;
    if (liv.base.phys.onGround) {
        int id = cu_world_block(e, mc_floor(liv.base.phys.posX),
                                mc_floor(liv.base.phys.box.minY) - 1,
                                mc_floor(liv.base.phys.posZ));
        if (id == 79 || id == 174 || id == 212) slip = 0.98f;
        if (id == 8 || id == 9) slip = 0.8f;
    }
    if (type == EW_TYPE_PIGMAN && m->anger > 0)
        liv.landMovementFactor += 0.05f;

    McAABB q = mc_aabb_addcoord(&liv.base.phys.box, liv.base.phys.motionX,
                                liv.base.phys.motionY, liv.base.phys.motionZ);
    q.minY -= liv.base.phys.stepHeight; q.maxY += liv.base.phys.stepHeight;
    int n = mai_collect_blocks(e, &q, blocks, ESS_MOB_BLOCKS);
    eb_tick_living(&liv, slip, 0, blocks, n, st);

    m->x = liv.base.phys.posX;
    m->y = liv.base.phys.posY;
    m->z = liv.base.phys.posZ;
    m->mx = liv.base.phys.motionX;
    m->my = liv.base.phys.motionY;
    m->mz = liv.base.phys.motionZ;
    m->on_ground = liv.base.phys.onGround ? 1 : 0;
    m->yaw = liv.base.rotationYaw;
    m->box_minx = liv.base.phys.box.minX;
    m->box_miny = liv.base.phys.box.minY;
    m->box_minz = liv.base.phys.box.minZ;
    m->box_maxx = liv.base.phys.box.maxX;
    m->box_maxy = liv.base.phys.box.maxY;
    m->box_maxz = liv.base.phys.box.maxZ;
    m->box_on = 1;
}

MC_HD static inline void mai_det_tick(Blaze *e, const McSinTable *st,
                                      double px, double py, double pz, int day) {
    unsigned i;
    McAABB player_bb = mc_aabb_make(px - 0.3, py, pz - 0.3, px + 0.3, py + 1.8, pz + 0.3);

    mai_player_collide_mobs(e, &player_bb, px, pz);

    for (i = 0; i < e->n_mobs; ++i) {
        RlSnapMob *m = &e->mobs[i];
        if (!m->alive || !mai_det_living(m->type)) continue;

        int type = m->type;
        int hostile = mai_is_hostile(type);
        int passive = mai_is_passive(type);

        /* Corpse tick / death */
        if (m->health <= 0.0f) {
            if (m->death_time == 0) cu_mob_on_death(e, m);
            if (!ml_on_death_update(m)) cu_mob_finish_dead(e, m);
            continue;
        }

        /* Attack cooldown / anger */
        if (type != EW_TYPE_BLAZE && m->attack_time > 0) --m->attack_time;
        if (type == EW_TYPE_PIGMAN && m->anger > 0) --m->anger;

        /* Despawn check */
        double dx = px - m->x, dy = py - m->y, dz = pz - m->z;
        double d = sqrt(dx * dx + dy * dy + dz * dz);
        if (hostile) {
            if (!(hai_ok(type) || m->persist)) {
                if (d > GM_MOB_DESPAWN_HARD) { m->alive = 0; continue; }
                if (d > GM_MOB_DESPAWN_SOFT) {
                    if (++e->mob_despawn[i] >= GM_MOB_DESPAWN_DELAY) {
                        m->alive = 0; continue;
                    }
                } else e->mob_despawn[i] = 0;
            }
        }

        /* Fire ticks from daylight burn for zombie/skeleton */
        if (!hai_ok(type) && day && (type == EW_TYPE_ZOMBIE || type == EW_TYPE_SKELETON) &&
            m->fire_ticks <= 0 && ml_sky_exposed(e, m->x, m->y, m->z))
            m->fire_ticks = GM_MOB_FIRE_TICKS;

        /* Lava fire: setFire(15) = 300 ticks */
        if (passive && mai_in_material(e, m, 1) && m->fire_ticks < 300)
            m->fire_ticks = 300;

        /* Fire countdown & damage every 20 ticks */
        if (m->fire_ticks > 0) {
            --m->fire_ticks;
            if (m->fire_ticks % 20 == 0) {
                m->health -= 1.0f;
                if (m->health <= 0.0f) {
                    m->health = 0.0f;
                    if (m->death_time == 0) cu_mob_on_death(e, m);
                    if (!ml_on_death_update(m)) cu_mob_finish_dead(e, m);
                    continue;
                }
            }
        }

        /* Aggro check */
        int aggro = 0;
        if (hostile) {
            int wants = 1;
            if (type == EW_TYPE_ENDERMAN) wants = m->anger > 0;
            else if (type == EW_TYPE_SPIDER) wants = !day || m->anger > 0;
            else if (type == EW_TYPE_PIGMAN) wants = m->anger > 0;
            else if (type == EW_TYPE_SLIME) wants = m->swell > 1;
            if (wants && d <= mai_follow_range(type)) {
                float mw, mh;
                mai_size(type, &mw, &mh);
                if (type == EW_TYPE_GHAST) aggro = 1;
                else aggro = ml_los_clear(e, m->x, m->y + (double)mh * 0.85, m->z,
                                          px, py + (double)(float)PSV_EYE_HEIGHT, pz);
            }
        }

        int moving = 0, jump = 0, wandering = 0, swim_jump = 0;
        double nav_speed = 1.0;

        /* Hostiles vs Passives living check */
        if (hai_ok(type)) {
            mai_hai_living(e, i, day);
        }
        if (mai_det_ai(type)) {
            int lst = e->mob_living_sound[i];
            JavaRandom jr; jr.seed = m->seed48;
            int sound_draw = jrand_int_bound(&jr, 1000);
            lst += 1;
            if (sound_draw < lst - 1) {
                lst = -mai_talk_interval(type);
                (void)jrand_float(&jr);
                (void)jrand_float(&jr);
            }
            e->mob_living_sound[i] = lst;

            double d3 = dx * dx + dy * dy + dz * dz;
            e->mob_entity_age[i]++;
            if (m->persist) {
                e->mob_entity_age[i] = 0;
            } else if (e->mob_entity_age[i] > 600) {
                (void)jrand_int_bound(&jr, 800);
                if (d3 < 1024.0) e->mob_entity_age[i] = 0;
            } else if (d3 < 1024.0) {
                e->mob_entity_age[i] = 0;
            }
            m->seed48 = jr.seed;
        }

        /* AI tick */
        if (mai_det_ai(type) && !aggro) {
            mai_pai_tick(e, i, px, py, pz, e->mob_griefing,
                         &moving, &jump, &wandering, &swim_jump, &nav_speed);
        } else if (hai_ok(type)) {
            mai_hai_tick(e, i, px, py, pz, day,
                         &moving, &jump, &wandering, &swim_jump, &nav_speed,
                         e->mob_griefing);
            if (!m->alive) continue;
        }

        /* Look update before move */
        if (mai_det_ai(type)) mai_apply_current_look(e, i, px, py, pz);
        if (hai_ok(type)) mai_hai_look(e, i, px, py, pz);

        /* Move mob */
        double prev_x = m->x, prev_z = m->z;
        mai_move_mob(e, st, i, moving, jump, swim_jump, nav_speed, &prev_x, &prev_z);

        /* Collide nearby */
        mai_collide_nearby(e, i, &player_bb, px, pz);

        /* Body update */
        mai_body_update(e, i, prev_x, prev_z);

        /* Chicken egg timer */
        if (passive && type == EW_TYPE_CHICKEN) {
            if (--e->mob_chicken_egg[i] <= 0) {
                JavaRandom jr; jr.seed = m->seed48;
                (void)jrand_float(&jr);
                (void)jrand_float(&jr);
                e->mob_chicken_egg[i] = jrand_int_bound(&jr, 6000) + 6000;
                m->seed48 = jr.seed;
            }
        }

        /* Slime squish */
        if (type == EW_TYPE_SLIME) {
            int on = m->on_ground ? 1 : 0;
            if (on && !m->see_time) m->see_time = 1;
        }
    }
}

#endif /* MC_MOB_AI_TASKS_H */
