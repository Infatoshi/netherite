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

MC_HD static inline int mai_in_material(const Blaze *e, const RlSnapMob *m, int mat) {
    float width, height;
    mai_size(m->type, &width, &height);
    double hf = (double)width * 0.5;
    McAABB bb = mc_aabb_make(m->x - hf, m->y, m->z - hf,
                             m->x + hf, m->y + (double)height, m->z + hf);
    McAABB eb = mc_aabb_expand(&bb, -0.10000000149011612, -0.4000000059604645, -0.10000000149011612);
    int x0 = mc_floor(eb.minX), x1 = mc_floor(eb.maxX + 1.0);
    int y0 = mc_floor(eb.minY), y1 = mc_floor(eb.maxY + 1.0);
    int z0 = mc_floor(eb.minZ), z1 = mc_floor(eb.maxZ + 1.0);
    for (int x = x0; x < x1; ++x)
        for (int y = y0; y < y1; ++y)
            for (int z = z0; z < z1; ++z) {
                int id = cu_world_block(e, x, y, z);
                if (mat == 0 && (id == 8 || id == 9)) return 1;
                if (mat == 1 && (id == 10 || id == 11)) return 1;
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

MC_HD static inline int mai_random_position(const Blaze *e, RlSnapMob *m,
                                            int xz, int yrange, int land,
                                            double *out_x, double *out_y, double *out_z) {
    int found = 0, best_dx = 0, best_dy = 0, best_dz = 0;
    float best = -99999.0f;
    JavaRandom jr; jr.seed = m->seed48;
    for (int k = 0; k < 10; ++k) {
        int dx = jrand_int_bound(&jr, 2 * xz + 1) - xz;
        int dy = jrand_int_bound(&jr, 2 * yrange + 1) - yrange;
        int dz = jrand_int_bound(&jr, 2 * xz + 1) - xz;
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
    m->seed48 = jr.seed;
    if (!found) return 0;
    *out_x = m->x + best_dx;
    *out_y = m->y + best_dy;
    *out_z = m->z + best_dz;
    return 1;
}

MC_HD static inline void mai_path_to_pos(const Blaze *e, int *bx, int *by, int *bz) {
    int score_y = *by;
    if (ml_solid_id(cu_world_block(e, *bx, score_y, *bz))) {
        while (score_y < 256 && ml_solid_id(cu_world_block(e, *bx, score_y, *bz)))
            score_y++;
    } else {
        while (score_y > 0 && !ml_solid_id(cu_world_block(e, *bx, score_y - 1, *bz)))
            score_y--;
    }
    *by = score_y;
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

#endif /* MC_MOB_AI_TASKS_H */
