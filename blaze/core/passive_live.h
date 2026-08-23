/* passive_live.h - magma/blaze shared cow/pig/sheep/chicken live tick.
 *
 * Magma magma/game/mob_live.c and blaze-CPU/CUDA compile this one source.
 * Magma wrappers stay thin; do not re-derive gm_mobs_tick.
 *
 * Java 1.11.2 (java/oracle-src):
 *   EntityCow.setSize 0.9x1.4                  EntityCow.java:33
 *   EntityPig.setSize 0.9x0.9                  EntityPig.java:54
 *   EntitySheep.setSize 0.9x1.3                EntitySheep.java:82
 *   EntityChicken.setSize 0.4x0.7              EntityChicken.java:50
 *   MAX_HEALTH cow 10 / pig 10 / sheep 8 / chicken 4
 *                                              EntityCow.java:56 EntityPig.java:73
 *                                              EntitySheep.java:124 EntityChicken.java:75
 *   MOVEMENT_SPEED cow 0.20000000298023224 / pig 0.25 / sheep 0.23 / chicken 0.25
 *   EntityAnimal.canDespawn false              EntityAnimal.java:137-140
 *   EntityLiving.despawnEntity                 EntityLiving.java:787-831
 *     canDespawn false => never setDead; persist still zeros age
 *   EntityAIWander.shouldExecute               EntityAIWander.java:37-39
 *     getAge() >= 100 returns false (age is entityAge)
 *   EntityChicken.onLivingUpdate               EntityChicken.java:83-111
 *     motionY*=0.6 when !onGround && motionY<0; fall() empty (:113-115)
 *   EntityAIPanic speed cow 2.0 / pig 1.25 / sheep 1.25 / chicken 1.4
 *                                              EntityCow.java:44 EntityPig.java:60
 *                                              EntitySheep.java:91 EntityChicken.java:58
 *   RandomPositionGenerator.findRandomTarget   RandomPositionGenerator.java:24-27,66-156
 *     10 samples, entity.rand nextInt(2*xz+1)-xz, nextInt(2*y+1)-y
 *   EntityAIWander.executionChance 120         EntityAIWander.java:19,42
 *   EntityAIWanderAvoidWater 10x7 land         EntityAIWanderAvoidWater.java:13,32
 *     nextFloat >= 0.001F then getLandPos(10,7) else findRandomTarget(10,7)
 *   EntityAILookIdle nextFloat < 0.02F         EntityAILookIdle.java:27
 *     startExecuting: 2*PI*nextDouble, 20+nextInt(20)  EntityAILookIdle.java:43-46
 *   EntityAnimal.getExperiencePoints           EntityAnimal.java:145-148
 *     1 + world.rand.nextInt(3)  -- world.rand is owned by lane/worldrand;
 *     this path draws the same nextInt from entity.rand (documented deviation)
 *   Drops (looting 0), entity.rand:
 *     cow leather 0..2 + beef 1..3             loot ENTITIES_COW / 1.8 dropFewItems
 *     pig porkchop 1..3
 *     sheep mutton 1..2 + wool if not sheared
 *     chicken feather 0..2 + chicken 1
 *   EntitySheep.getRandomSheepColor            EntitySheep.java:333-336
 *
 * Magma extras (M1 is magma semantics; do not "fix" to Java here):
 *   det_entity_rng off: no EntityAITasks mutex scheduler, no PathNavigateGround
 *     A* (GPU_MOB_AI.md). Panic/wander consume the cited entity.rand draws
 *     then walk a straight line like ml_hostile_ai. LookIdle consumes the
 *     cited draws and yaws; LookHelper interpolation is skipped.
 *   EntityAISwimming: feet in water => jump. No A*.
 *   EntityAIMate / EntityAITempt / EntityAIFollowParent / EntityAIEatGrass
 *     / EntityAIWatchClosest OUT (breeding/shearing/tempt/parent/watch).
 *   Chunk-gen performWorldGenSpawning OUT: snapshots are loaded regions.
 *
 * Include after defining ML_BLOCK for the world half (same as hostile_live.h).
 */
#ifndef MC_PASSIVE_LIVE_H
#define MC_PASSIVE_LIVE_H

#include <math.h>
#include <string.h>

#include "mc.h"
#include "mc_math.h"
#include "mc_rng.h"
#include "mc_blocks.h"
#include "entity_hostile_spine.h"
#include "entity_spine.h"
#include "hostile_live.h"
#include "../env/blaze_snapshot.h"

#ifndef PL_BLOCKS
#define PL_BLOCKS ESS_MOB_BLOCKS
#endif

#define PL_WANDER_CHANCE 120          /* EntityAIWander.java:19 */
#define PL_WANDER_XZ 10               /* EntityAIWanderAvoidWater.java:32 */
#define PL_WANDER_Y 7
#define PL_PANIC_XZ 5                 /* EntityAIPanic.java:57 */
#define PL_PANIC_Y 4
#define PL_REVENGE_TICKS 101          /* magma; EntityCreature revenge ~100 */
#define PL_LOOK_CHANCE 0.02f          /* EntityAILookIdle.java:27 */
#define PL_AVOID_WATER_P 0.001f       /* EntityAIWanderAvoidWater.java:13 */

#define PL_ITEM_LEATHER 334
#define PL_ITEM_BEEF 363
#define PL_ITEM_PORK 319
#define PL_ITEM_MUTTON 423
#define PL_ITEM_WOOL 35
#define PL_ITEM_FEATHER 288
#define PL_ITEM_CHICKEN 365

MC_HD static inline int pl_is_roster(int type) {
    return ehs_is_passive((u8)type);
}

MC_HD static inline double pl_panic_mul(int type) {
    if (type == EW_TYPE_COW) return 2.0;       /* EntityCow.java:44 */
    if (type == EW_TYPE_CHICKEN) return 1.4;   /* EntityChicken.java:58 */
    return 1.25;                               /* EntityPig.java:60 EntitySheep.java:91 */
}

/* EntitySheep DYE_COLOR: low 4 bits color, bit 4 sheared. */
MC_HD static inline int pl_sheep_color(int swell) { return swell & 15; }
MC_HD static inline int pl_sheep_sheared(int swell) { return (swell & 16) != 0; }
MC_HD static inline int pl_sheep_pack(int color, int sheared) {
    return (color & 15) | (sheared ? 16 : 0);
}

/* EntitySheep.getRandomSheepColor lives next to CREATURE insert
 * (hostile_spawn.h hs_random_sheep_color). */

typedef struct {
    int item, count, meta;
} PlDrop;

/* Loot-table counts at looting 0, drawn from entity.rand (EntityLiving.dropLoot
 * uses this.rand when deathLootTableSeed==0, EntityLiving.java:602). */
MC_HD static inline int pl_drop_few(int type, int sheared, int color,
                                    JavaRandom *er, PlDrop *out, int cap) {
    int n = 0;
    if (!er || !out || cap <= 0) return 0;
    if (type == EW_TYPE_COW) {
        if (n < cap) { out[n].item = PL_ITEM_LEATHER; out[n].count = jrand_int_bound(er, 3);
                       out[n].meta = 0; if (out[n].count > 0) ++n; }
        if (n < cap) { out[n].item = PL_ITEM_BEEF; out[n].count = jrand_int_bound(er, 3) + 1;
                       out[n].meta = 0; ++n; }
    } else if (type == EW_TYPE_PIG) {
        if (n < cap) { out[n].item = PL_ITEM_PORK; out[n].count = jrand_int_bound(er, 3) + 1;
                       out[n].meta = 0; ++n; }
    } else if (type == EW_TYPE_SHEEP) {
        if (n < cap) { out[n].item = PL_ITEM_MUTTON; out[n].count = jrand_int_bound(er, 2) + 1;
                       out[n].meta = 0; ++n; }
        if (!sheared && n < cap) {
            out[n].item = PL_ITEM_WOOL; out[n].count = 1; out[n].meta = color & 15;
            ++n;
        }
    } else if (type == EW_TYPE_CHICKEN) {
        if (n < cap) { out[n].item = PL_ITEM_FEATHER; out[n].count = jrand_int_bound(er, 3);
                       out[n].meta = 0; if (out[n].count > 0) ++n; }
        if (n < cap) { out[n].item = PL_ITEM_CHICKEN; out[n].count = 1; out[n].meta = 0; ++n; }
    }
    return n;
}

/* EntityAnimal.getExperiencePoints EntityAnimal.java:147 is
 * 1 + world.rand.nextInt(3). world.rand is a different lane; consume the
 * same nextInt from entity.rand. */
MC_HD static inline int pl_xp_points(JavaRandom *er) {
    if (!er) return 1;
    return 1 + jrand_int_bound(er, 3);
}

/* EntityAnimal.canDespawn false: never setDead from distance. persist still
 * zeros age (EntityLiving.java:790-793). entityAge still ticks;
 * EntityAIWander.shouldExecute returns false when getAge()>=100
 * (EntityAIWander.java:37-39). */
MC_HD static inline void pl_passive_pre(MlMob *m) {
    RlSnapMob *s;
    if (!m || !m->snap.alive || !pl_is_roster(m->snap.type)) return;
    s = &m->snap;
    if (s->persist) {
        m->despawn_ticks = 0;
        return;
    }
    ++m->despawn_ticks;
}

typedef struct {
    int moving, jump;
    float speed_mul;
} PlAiOut;

#endif /* MC_PASSIVE_LIVE_H */

#ifdef ML_W
#ifndef MC_PASSIVE_LIVE_WORLD_H
#define MC_PASSIVE_LIVE_WORLD_H

#ifndef ML_SKY
#define ML_SKY(w, x, y, z) 15
#endif
#ifndef ML_BLK
#define ML_BLK(w, x, y, z) 0
#endif

MC_HD static inline int pl_in_water(ML_W *w, double x, double y, double z) {
    int id = ML_BLOCK(w, mc_floor(x), mc_floor(y), mc_floor(z));
    return id == BLK_FLOWING_WATER || id == BLK_WATER;
}

MC_HD static inline int pl_full_block(int id) {
    BptProps p;
    if (id == 0) return 0;
    p = mc_bpt_props(id);
    return (p.flags & BF_SOLID) && p.light_opacity == 255;
}

/* PathNavigate.canEntityStandOnPos: IBlockState.isFullBlock of pos.down
 * (PathNavigate.java:392-395). */
MC_HD static inline int pl_can_stand(ML_W *w, int x, int y, int z) {
    if (y <= 0) return 0;
    return pl_full_block(ML_BLOCK(w, x, y - 1, z));
}

MC_HD static inline int pl_combined_light(ML_W *w, int x, int y, int z) {
    int sky = ML_SKY(w, x, y, z);
    int blk = ML_BLK(w, x, y, z);
    return blk > sky ? blk : sky;
}

/* WorldProvider.lightBrightnessTable overworld: (1-f1)/(f1*3+1),
 * f1 = 1 - light/15. EntityAnimal.getBlockPathWeight EntityAnimal.java:83-86. */
MC_HD static inline float pl_path_weight(ML_W *w, int type, int x, int y, int z) {
    int light;
    float f1, br;
    if (ehs_is_passive((u8)type) && ML_BLOCK(w, x, y - 1, z) == BLK_GRASS)
        return 10.0f;
    light = pl_combined_light(w, x, y, z);
    if (light < 0) light = 0;
    if (light > 15) light = 15;
    f1 = 1.0f - (float)light / 15.0f;
    br = (1.0f - f1) / (f1 * 3.0f + 1.0f);
    return br - 0.5f;
}

MC_HD static inline int pl_is_water_id(int id) {
    return id == BLK_FLOWING_WATER || id == BLK_WATER;
}

MC_HD static inline int pl_is_solid_mat(int id) {
    BptProps p;
    if (id == 0) return 0;
    p = mc_bpt_props(id);
    return (p.flags & BF_SOLID) && !(p.flags & BF_LIQUID);
}

/* RandomPositionGenerator.moveAboveSolid :158-174. */
MC_HD static inline int pl_move_above_solid(ML_W *w, int x, int y, int z) {
    if (!pl_is_solid_mat(ML_BLOCK(w, x, y, z))) return y;
    while (y < 255 && pl_is_solid_mat(ML_BLOCK(w, x, y, z))) ++y;
    return y;
}

/* RandomPositionGenerator.generateRandomPos :66-156.
 * land=1 is getLandPos (p_191379_4_=false). Returns 1 and writes dest. */
MC_HD static inline int pl_random_pos(ML_W *w, JavaRandom *er, int type,
                                      double x, double y, double z,
                                      int xz, int yrange, int land,
                                      double *ox, double *oy, double *oz) {
    int k, found = 0;
    int best_dx = 0, best_dy = 0, best_dz = 0;
    float best = -99999.0f;
    if (!er) return 0;
    for (k = 0; k < 10; ++k) {
        int dx = jrand_int_bound(er, 2 * xz + 1) - xz;
        int dy = jrand_int_bound(er, 2 * yrange + 1) - yrange;
        int dz = jrand_int_bound(er, 2 * xz + 1) - xz;
        int bx = mc_floor(x + (double)dx);
        int by = mc_floor(y + (double)dy);
        int bz = mc_floor(z + (double)dz);
        int score_y;
        float score;
        if (!pl_can_stand(w, bx, by, bz)) continue;
        score_y = by;
        if (land) {
            score_y = pl_move_above_solid(w, bx, by, bz);
            if (pl_is_water_id(ML_BLOCK(w, bx, score_y, bz))) continue;
        }
        score = pl_path_weight(w, type, bx, score_y, bz);
        if (score > best) {
            best = score;
            best_dx = dx;
            best_dy = dy;
            best_dz = dz;
            found = 1;
        }
    }
    if (!found) return 0;
    *ox = x + (double)best_dx;
    *oy = y + (double)best_dy;
    *oz = z + (double)best_dz;
    return 1;
}

/* EntityAIPanic.getRandPos water search EntityAIPanic.java:89-122. */
MC_HD static inline int pl_nearest_water(ML_W *w, double x, double y, double z,
                                         double *ox, double *oy, double *oz) {
    int i = mc_floor(x), j = mc_floor(y), k = mc_floor(z);
    float best = (float)(5 * 5 * 4 * 2);
    int found = 0, lx, ly, lz;
    for (lx = i - 5; lx <= i + 5; ++lx)
        for (ly = j - 4; ly <= j + 4; ++ly)
            for (lz = k - 5; lz <= k + 5; ++lz) {
                float dx, dy, dz, d;
                if (!pl_is_water_id(ML_BLOCK(w, lx, ly, lz))) continue;
                dx = (float)(lx - i);
                dy = (float)(ly - j);
                dz = (float)(lz - k);
                d = dx * dx + dy * dy + dz * dz;
                if (d < best) {
                    best = d;
                    *ox = (double)lx;
                    *oy = (double)ly;
                    *oz = (double)lz;
                    found = 1;
                }
            }
    return found;
}

MC_HD static inline void pl_set_dest(RlSnapMob *s, double tx, double tz) {
    s->wander_x = tx;
    s->wander_z = tz;
    s->task_bits = EW_AI_CHASE;
}

MC_HD static inline int pl_has_dest(const RlSnapMob *s) {
    double dx, dz;
    if (!s || s->task_bits != EW_AI_CHASE) return 0;
    dx = s->wander_x - s->x;
    dz = s->wander_z - s->z;
    return dx * dx + dz * dz >= 1.0;
}

/* Generic (det_entity_rng off) passive body. Straight-line dest, cited RNG.
 * PathNavigateGround A* is GPU_MOB_AI.md; not executed here. */
MC_HD static inline void pl_passive_ai(MlMob *m, ML_W *w, PlAiOut *out) {
    RlSnapMob *s;
    JavaRandom er;
    int type, moving = 0, jump = 0;
    float speed_mul = 1.0f;
    double tx, ty, tz;
    if (out) {
        out->moving = 0;
        out->jump = 0;
        out->speed_mul = 1.0f;
    }
    if (!m || !m->snap.alive) return;
    s = &m->snap;
    type = s->type;
    if (!pl_is_roster(type)) return;
    er.seed = s->seed48;
    if (s->panic > 0) --s->panic;     /* revenge ticks */

    if (pl_in_water(w, s->x, s->y, s->z))
        jump = 1;                     /* EntityAISwimming; no rand */

    if (s->panic > 0 || m->fire_ticks > 0) {
        /* EntityAIPanic.shouldExecute once; continueExecuting while the
         * navigator still has a path. Straight-line dest is the path stand-in. */
        if (!pl_has_dest(s)) {
            int found = 0;
            if (m->fire_ticks > 0)
                found = pl_nearest_water(w, s->x, s->y, s->z, &tx, &ty, &tz);
            if (!found)
                found = pl_random_pos(w, &er, type, s->x, s->y, s->z,
                                      PL_PANIC_XZ, PL_PANIC_Y, 0,
                                      &tx, &ty, &tz);
            if (found) pl_set_dest(s, tx, tz);
        }
        if (pl_has_dest(s)) {
            speed_mul = (float)pl_panic_mul(type);
            moving = 1;
        }
    } else if (pl_has_dest(s)) {
        moving = 1;
        speed_mul = 1.0f;
    } else if (s->see_time > 0) {
        /* EntityAILookIdle mutex 3 conflicts with wander mutex 1. */
        --s->see_time;
        s->task_bits = EW_AI_IDLE;
    } else {
        s->task_bits = EW_AI_IDLE;
        if (m->despawn_ticks < 100) {
            if (jrand_int_bound(&er, PL_WANDER_CHANCE) == 0) {
                int ok;
                if (pl_in_water(w, s->x, s->y, s->z)) {
                    ok = pl_random_pos(w, &er, type, s->x, s->y, s->z,
                                       15, 7, 1, &tx, &ty, &tz);
                    if (!ok)
                        ok = pl_random_pos(w, &er, type, s->x, s->y, s->z,
                                           PL_WANDER_XZ, PL_WANDER_Y, 0,
                                           &tx, &ty, &tz);
                } else {
                    int land = jrand_float(&er) >= PL_AVOID_WATER_P;
                    ok = pl_random_pos(w, &er, type, s->x, s->y, s->z,
                                       PL_WANDER_XZ, PL_WANDER_Y, land,
                                       &tx, &ty, &tz);
                }
                if (ok) {
                    pl_set_dest(s, tx, tz);
                    moving = 1;
                    speed_mul = 1.0f;
                }
            }
        }
        if (!moving) {
            /* EntityAILookIdle. WatchClosest is not consumed (no A* scheduler). */
            if (s->see_time > 0) {
                --s->see_time;
            } else if (jrand_float(&er) < PL_LOOK_CHANCE) {
                double a = (double)(2.0 * MC_PI) * jrand_double(&er);
                double lx = cos(a), lz = sin(a);
                s->yaw = ehs_yaw_toward(lx, lz);
                s->see_time = 20 + jrand_int_bound(&er, 20);
            }
        }
    }

    if (moving) {
        double mvx = s->wander_x - s->x, mvz = s->wander_z - s->z;
        double len = sqrt(mvx * mvx + mvz * mvz);
        s->yaw = ehs_yaw_toward(mvx, mvz);
        if (len > 0.01) {
            int ax = mc_floor(s->x + mvx / len * 0.9);
            int az = mc_floor(s->z + mvz / len * 0.9);
            int fy = mc_floor(s->y);
            if (ess_solid_id(ML_BLOCK(w, ax, fy, az)) &&
                !ess_solid_id(ML_BLOCK(w, ax, fy + 1, az)) &&
                !ess_solid_id(ML_BLOCK(w, ax, fy + 2, az)))
                jump = 1;
        }
    }

    s->seed48 = er.seed;
    if (out) {
        out->moving = moving;
        out->jump = jump;
        out->speed_mul = speed_mul;
    }
}

MC_HD static inline void pl_move_passive(MlMob *m, ML_W *w, const McSinTable *st,
                                         int moving, int jump, float speed_mul) {
    EbLiving liv;
    EhsIntent intent;
    PcfBlock blocks[PL_BLOCKS];
    McAABB q;
    int n = 0, x, y, z, x0, x1, y0, y1, z0, z1, under;
    float slip;
    RlSnapMob *s;
    if (!m || !m->snap.alive || !pl_is_roster(m->snap.type)) return;
    s = &m->snap;
    ehs_intent_from_ai((u8)s->type, (u32)s->task_bits, moving,
                       s->x, s->z, s->wander_x, s->wander_z,
                       s->wander_x, s->wander_z, &intent);
    if (!moving) intent.yaw = s->yaw;
    if (moving && jump) intent.isJumping = 1;
    ess_load_pose(&liv, s->type, s->x, s->y, s->z, s->mx, s->my, s->mz,
                  s->on_ground, intent.yaw, 0, 0, 0, 0, 0, 0, 0);
    liv.moveForward = intent.moveForward;
    liv.moveStrafing = intent.moveStrafing;
    liv.isJumping = intent.isJumping;
    liv.landMovementFactor = ehs_land_speed((u8)s->type) * speed_mul;
    ess_query_box(&liv, &q);
    x0 = mc_floor(q.minX) - 1; x1 = mc_floor(q.maxX) + 1;
    y0 = mc_floor(q.minY) - 1; y1 = mc_floor(q.maxY) + 1;
    z0 = mc_floor(q.minZ) - 1; z1 = mc_floor(q.maxZ) + 1;
    if (y0 < 0) y0 = 0;
    if (y1 > 255) y1 = 255;
    for (x = x0; x <= x1; ++x)
        for (y = y0; y <= y1; ++y)
            for (z = z0; z <= z1; ++z) {
                n = ess_collect_push(blocks, n, PL_BLOCKS,
                                     ML_BLOCK(w, x, y, z), x, y, z);
                if (n == PL_BLOCKS) goto collected;
            }
collected:
    under = ML_BLOCK(w, mc_floor(liv.base.phys.posX),
                     mc_floor(liv.base.phys.box.minY) - 1,
                     mc_floor(liv.base.phys.posZ));
    slip = ess_slip_on_ground(&liv, under);
    ess_tick_living(&liv, slip, blocks, n, st);
    ess_chicken_glide(&liv, s->type);
    {
        unsigned char box_on = s->box_on;
        double minx = s->box_minx, miny = s->box_miny, minz = s->box_minz;
        double maxx = s->box_maxx, maxy = s->box_maxy, maxz = s->box_maxz;
        ess_store_snap(s, &liv);
        s->box_on = box_on;
        s->box_minx = minx; s->box_miny = miny; s->box_minz = minz;
        s->box_maxx = maxx; s->box_maxy = maxy; s->box_maxz = maxz;
    }
}

#endif /* MC_PASSIVE_LIVE_WORLD_H */
#endif /* ML_W */
