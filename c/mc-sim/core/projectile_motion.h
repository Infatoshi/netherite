/* projectile_motion: EntityArrow in-air flight (MC 1.11.2 EntityArrow.onUpdate else branch).
 *
 * INTERNAL verify (CPU==CUDA). Synthetic 16x16x32 voxel scenes; gravity + 0.99 drag +
 * World.rayTraceBlocks(full-cube blocks only) block hit. Emits posX/posY/posZ per tick as
 * raw double hex. Read-only deps: physics_collision_math.h (McAABB), block_props_table.h.
 *
 * TRIMMED (output-identical for our scenes): inGround-at-start tile check, arrowShake,
 * findEntityOnPath / entity onHit, critical/water/wet particles, rotation smoothing,
 * doBlockCollisions, super.onUpdate, RNG inaccuracy (fixed initial motion per scenario). */
#ifndef MC_PROJECTILE_MOTION_H
#define MC_PROJECTILE_MOTION_H

#include "mc.h"
#include "mc_world.h"
#include "mc_blocks.h"
#include "mc_math.h"
#include "physics_collision_math.h"
#include "block_props_table.h"

#define PM_DIM_X 16
#define PM_DIM_Y 32
#define PM_DIM_Z 16
#define PM_VOL (PM_DIM_X * PM_DIM_Y * PM_DIM_Z)

#define PM_MAX_TICKS 48
#define PM_NUM_SCENARIOS 4

typedef struct {
    double posX, posY, posZ;
    double motionX, motionY, motionZ;
    int inGround;
    int ticksInAir;
} McArrow;

typedef struct {
    int hit;
    double hitX, hitY, hitZ;
    int bx, by, bz;
} PmRayHit;

typedef void (*PmEmitFn)(u64 bits, void *ctx);

MC_HD static inline int pm_idx(int x, int y, int z) {
    return (y * PM_DIM_Z + z) * PM_DIM_X + x;
}

MC_HD static inline int pm_in(int x, int y, int z) {
    return x >= 0 && x < PM_DIM_X && y >= 0 && y < PM_DIM_Y && z >= 0 && z < PM_DIM_Z;
}

MC_HD static inline u16 pm_get(const u16 *grid, int x, int y, int z) {
    return pm_in(x, y, z) ? grid[pm_idx(x, y, z)] : mc_state(BLK_AIR, 0);
}

MC_HD static inline void pm_set(u16 *grid, int x, int y, int z, u16 s) {
    if (pm_in(x, y, z)) grid[pm_idx(x, y, z)] = s;
}

MC_HD static inline void pm_fill(u16 *grid, u16 s) {
    for (int i = 0; i < PM_VOL; ++i) grid[i] = s;
}

MC_HD static inline void pm_fill_box(u16 *grid, int x0, int y0, int z0,
                                     int x1, int y1, int z1, u16 s) {
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x)
                pm_set(grid, x, y, z, s);
}

MC_HD static inline int pm_is_solid(u16 st) {
    int id = mc_state_id(st);
    if (id <= 0) return 0;
    return (mc_bpt_props(id).flags & BF_SOLID) != 0;
}

MC_HD static inline int pm_can_collide(u16 st) {
    return pm_is_solid(st);
}

/* MathHelper.sqrt(double) -> (float)Math.sqrt */
MC_HD static inline float pm_sqrt(double v) {
    return (float)sqrt(v);
}

/* AxisAlignedBB.isClosest: strict < on square distance. */
MC_HD static inline int pm_is_closest(double ax, double ay, double az,
                                      int have, double bx, double by, double bz,
                                      double cx, double cy, double cz) {
    if (!have) return 1;
    double dx1 = cx - ax, dy1 = cy - ay, dz1 = cz - az;
    double dx2 = bx - ax, dy2 = by - ay, dz2 = bz - az;
    return (dx1 * dx1 + dy1 * dy1 + dz1 * dz1) < (dx2 * dx2 + dy2 * dy2 + dz2 * dz2);
}

MC_HD static inline int pm_intersects_yz(const McAABB *bb, double x, double y, double z) {
    return y >= bb->minY && y <= bb->maxY && z >= bb->minZ && z <= bb->maxZ;
}

MC_HD static inline int pm_intersects_xz(const McAABB *bb, double x, double y, double z) {
    return x >= bb->minX && x <= bb->maxX && z >= bb->minZ && z <= bb->maxZ;
}

MC_HD static inline int pm_intersects_xy(const McAABB *bb, double x, double y, double z) {
    return x >= bb->minX && x <= bb->maxX && y >= bb->minY && y <= bb->maxY;
}

/* Vec3d.getIntermediateWithXValue(vec, x) */
MC_HD static inline int pm_intermediate_x(double ax, double ay, double az,
                                            double bx, double by, double bz,
                                            double x, double *ox, double *oy, double *oz) {
    double d0 = bx - ax;
    double d1 = by - ay;
    double d2 = bz - az;
    if (d0 * d0 < 1.0000000116860974E-7) return 0;
    double d3 = (x - ax) / d0;
    if (d3 >= 0.0 && d3 <= 1.0) {
        *ox = ax + d0 * d3;
        *oy = ay + d1 * d3;
        *oz = az + d2 * d3;
        return 1;
    }
    return 0;
}

MC_HD static inline int pm_intermediate_y(double ax, double ay, double az,
                                            double bx, double by, double bz,
                                            double y, double *ox, double *oy, double *oz) {
    double d0 = bx - ax;
    double d1 = by - ay;
    double d2 = bz - az;
    if (d1 * d1 < 1.0000000116860974E-7) return 0;
    double d3 = (y - ay) / d1;
    if (d3 >= 0.0 && d3 <= 1.0) {
        *ox = ax + d0 * d3;
        *oy = ay + d1 * d3;
        *oz = az + d2 * d3;
        return 1;
    }
    return 0;
}

MC_HD static inline int pm_intermediate_z(double ax, double ay, double az,
                                            double bx, double by, double bz,
                                            double z, double *ox, double *oy, double *oz) {
    double d0 = bx - ax;
    double d1 = by - ay;
    double d2 = bz - az;
    if (d2 * d2 < 1.0000000116860974E-7) return 0;
    double d3 = (z - az) / d2;
    if (d3 >= 0.0 && d3 <= 1.0) {
        *ox = ax + d0 * d3;
        *oy = ay + d1 * d3;
        *oz = az + d2 * d3;
        return 1;
    }
    return 0;
}

/* AxisAlignedBB.calculateIntercept for a local-space AABB. side out: EnumFacing.ordinal. */
MC_HD static inline int pm_aabb_intercept(const McAABB *bb,
                                            double ax, double ay, double az,
                                            double bx, double by, double bz,
                                            double *hx, double *hy, double *hz, int *side) {
    int have = 0;
    double cx = 0.0, cy = 0.0, cz = 0.0;
    int face = 4; /* WEST */

    double tx, ty, tz;
    if (pm_intermediate_x(ax, ay, az, bx, by, bz, bb->minX, &tx, &ty, &tz) &&
        pm_intersects_yz(bb, tx, ty, tz)) {
        cx = tx; cy = ty; cz = tz; have = 1; face = 4;
    }
    if (pm_intermediate_x(ax, ay, az, bx, by, bz, bb->maxX, &tx, &ty, &tz) &&
        pm_intersects_yz(bb, tx, ty, tz) &&
        pm_is_closest(ax, ay, az, have, cx, cy, cz, tx, ty, tz)) {
        cx = tx; cy = ty; cz = tz; have = 1; face = 5;
    }
    if (pm_intermediate_y(ax, ay, az, bx, by, bz, bb->minY, &tx, &ty, &tz) &&
        pm_intersects_xz(bb, tx, ty, tz) &&
        pm_is_closest(ax, ay, az, have, cx, cy, cz, tx, ty, tz)) {
        cx = tx; cy = ty; cz = tz; have = 1; face = 0;
    }
    if (pm_intermediate_y(ax, ay, az, bx, by, bz, bb->maxY, &tx, &ty, &tz) &&
        pm_intersects_xz(bb, tx, ty, tz) &&
        pm_is_closest(ax, ay, az, have, cx, cy, cz, tx, ty, tz)) {
        cx = tx; cy = ty; cz = tz; have = 1; face = 1;
    }
    if (pm_intermediate_z(ax, ay, az, bx, by, bz, bb->minZ, &tx, &ty, &tz) &&
        pm_intersects_xy(bb, tx, ty, tz) &&
        pm_is_closest(ax, ay, az, have, cx, cy, cz, tx, ty, tz)) {
        cx = tx; cy = ty; cz = tz; have = 1; face = 2;
    }
    if (pm_intermediate_z(ax, ay, az, bx, by, bz, bb->maxZ, &tx, &ty, &tz) &&
        pm_intersects_xy(bb, tx, ty, tz) &&
        pm_is_closest(ax, ay, az, have, cx, cy, cz, tx, ty, tz)) {
        cx = tx; cy = ty; cz = tz; have = 1; face = 3;
    }

    if (!have) return 0;
    *hx = cx; *hy = cy; *hz = cz;
    *side = face;
    return 1;
}

/* Block.collisionRayTrace for a full cube at (bx,by,bz). */
MC_HD static inline int pm_block_raytrace(int bx, int by, int bz,
                                            double sx, double sy, double sz,
                                            double ex, double ey, double ez,
                                            PmRayHit *out) {
    McAABB bb = mc_aabb_make(0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
    double lsx = sx - (double)bx;
    double lsy = sy - (double)by;
    double lsz = sz - (double)bz;
    double lex = ex - (double)bx;
    double ley = ey - (double)by;
    double lez = ez - (double)bz;
    double hx, hy, hz;
    int side;
    if (!pm_aabb_intercept(&bb, lsx, lsy, lsz, lex, ley, lez, &hx, &hy, &hz, &side))
        return 0;
    out->hit = 1;
    out->hitX = hx + (double)bx;
    out->hitY = hy + (double)by;
    out->hitZ = hz + (double)bz;
    out->bx = bx; out->by = by; out->bz = bz;
    return 1;
}

/* World.rayTraceBlocks(start, end, stopOnLiquid=false, ignoreBlockWithoutBoundingBox=true,
 * returnLastUncollidableBlock=false) for full-cube solids in grid. */
MC_HD static inline int pm_ray_trace_blocks(const u16 *grid,
                                              double sx, double sy, double sz,
                                              double ex, double ey, double ez,
                                              PmRayHit *out) {
    out->hit = 0;
    if (sx != sx || sy != sy || sz != sz) return 0;
    if (ex != ex || ey != ey || ez != ez) return 0;

    int i = mc_floor(ex);
    int j = mc_floor(ey);
    int k = mc_floor(ez);
    int l = mc_floor(sx);
    int i1 = mc_floor(sy);
    int j1 = mc_floor(sz);

    u16 st = pm_get(grid, l, i1, j1);
    if (pm_can_collide(st)) {
        if (pm_block_raytrace(l, i1, j1, sx, sy, sz, ex, ey, ez, out))
            return 1;
    }

    double curX = sx, curY = sy, curZ = sz;
    int k1 = 200;

    while (k1-- >= 0) {
        if (curX != curX || curY != curY || curZ != curZ) return 0;
        if (l == i && i1 == j && j1 == k) return 0;

        int flag2 = 1, flag = 1, flag1 = 1;
        double d0 = 999.0, d1 = 999.0, d2 = 999.0;

        if (i > l)      d0 = (double)l + 1.0;
        else if (i < l) d0 = (double)l + 0.0;
        else flag2 = 0;

        if (j > i1)      d1 = (double)i1 + 1.0;
        else if (j < i1) d1 = (double)i1 + 0.0;
        else flag = 0;

        if (k > j1)      d2 = (double)j1 + 1.0;
        else if (k < j1) d2 = (double)j1 + 0.0;
        else flag1 = 0;

        double d3 = 999.0, d4 = 999.0, d5 = 999.0;
        double d6 = ex - curX;
        double d7 = ey - curY;
        double d8 = ez - curZ;

        if (flag2) d3 = (d0 - curX) / d6;
        if (flag)  d4 = (d1 - curY) / d7;
        if (flag1) d5 = (d2 - curZ) / d8;

        if (d3 == -0.0) d3 = -1.0E-4;
        if (d4 == -0.0) d4 = -1.0E-4;
        if (d5 == -0.0) d5 = -1.0E-4;

        int nf; /* EnumFacing.ordinal: DOWN=0 UP=1 NORTH=2 SOUTH=3 WEST=4 EAST=5 */
        if (d3 < d4 && d3 < d5) {
            nf = (i > l) ? 4 : 5;
            curX = d0;
            curY = curY + d7 * d3;
            curZ = curZ + d8 * d3;
        } else if (d4 < d5) {
            nf = (j > i1) ? 0 : 1;
            curX = curX + d6 * d4;
            curY = d1;
            curZ = curZ + d8 * d4;
        } else {
            nf = (k > j1) ? 2 : 3;
            curX = curX + d6 * d5;
            curY = curY + d7 * d5;
            curZ = d2;
        }

        l  = mc_floor(curX) - (nf == 5 ? 1 : 0);
        i1 = mc_floor(curY) - (nf == 1 ? 1 : 0);
        j1 = mc_floor(curZ) - (nf == 3 ? 1 : 0);

        st = pm_get(grid, l, i1, j1);
        if (pm_can_collide(st)) {
            if (pm_block_raytrace(l, i1, j1, curX, curY, curZ, ex, ey, ez, out))
                return 1;
        }
    }
    return 0;
}

/* EntityArrow.onHit block branch (entity branch CUT). */
MC_HD static inline void pm_arrow_on_hit_block(McArrow *a, const PmRayHit *hit) {
    a->motionX = (double)((float)(hit->hitX - a->posX));
    a->motionY = (double)((float)(hit->hitY - a->posY));
    a->motionZ = (double)((float)(hit->hitZ - a->posZ));
    float f2 = pm_sqrt(a->motionX * a->motionX + a->motionY * a->motionY + a->motionZ * a->motionZ);
    if (f2 > 0.0f) {
        a->posX -= a->motionX / (double)f2 * 0.05000000074505806;
        a->posY -= a->motionY / (double)f2 * 0.05000000074505806;
        a->posZ -= a->motionZ / (double)f2 * 0.05000000074505806;
    }
    a->inGround = 1;
}

/* One EntityArrow.onUpdate tick (in-air branch only). */
MC_HD static inline void pm_arrow_tick(McArrow *a, const u16 *grid) {
    if (a->inGround) return;

    a->ticksInAir++;

    double endX = a->posX + a->motionX;
    double endY = a->posY + a->motionY;
    double endZ = a->posZ + a->motionZ;

    PmRayHit hit;
    int got = pm_ray_trace_blocks(grid, a->posX, a->posY, a->posZ,
                                  endX, endY, endZ, &hit);

    if (got)
        pm_arrow_on_hit_block(a, &hit);

    a->posX += a->motionX;
    a->posY += a->motionY;
    a->posZ += a->motionZ;

    /* EntityArrow.onUpdate: float f1 = 0.99F; motionX *= (double)f1. The drag factor is a
     * FLOAT literal widened to double, NOT the double 0.99: (double)0.99f differs from 0.99
     * in the low mantissa bits, so the exact widened value is required for a bit-faithful
     * per-tick match vs the real game (verified by entity_trace_verify, scenarios a-c). */
    double f1 = (double)0.99f;
    a->motionX *= f1;
    a->motionY *= f1;
    a->motionZ *= f1;
    a->motionY -= 0.05000000074505806;
}

MC_HD static inline void pm_emit_double(double v, PmEmitFn emit, void *ctx) {
    u64 bits;
    /* bitcast double -> u64; memcpy pattern matches cpu driver */
    union { double d; u64 u; } u;
    u.d = v;
    bits = u.u;
    emit(bits, ctx);
}

MC_HD static inline void pm_scene_flat(u16 *grid) {
    u16 stone = mc_state(BLK_STONE, 0);
    pm_fill(grid, mc_state(BLK_AIR, 0));
    pm_fill_box(grid, 0, 0, 0, PM_DIM_X - 1, 0, PM_DIM_Z - 1, stone);
}

MC_HD static inline void pm_scene_wall(u16 *grid) {
    u16 stone = mc_state(BLK_STONE, 0);
    pm_scene_flat(grid);
    pm_fill_box(grid, 10, 1, 0, 10, 8, PM_DIM_Z - 1, stone);
}

MC_HD static inline void pm_scene_corner(u16 *grid) {
    u16 stone = mc_state(BLK_STONE, 0);
    pm_scene_flat(grid);
    pm_fill_box(grid, 10, 1, 0, 10, 10, PM_DIM_Z - 1, stone);
    pm_fill_box(grid, 0, 1, 10, PM_DIM_X - 1, 10, 10, stone);
}

MC_HD static inline void pm_scene_open(u16 *grid) {
    pm_fill(grid, mc_state(BLK_AIR, 0));
}

MC_HD static inline void pm_scenario_init(int idx, McArrow *a) {
    a->inGround = 0;
    a->ticksInAir = 0;
    switch (idx) {
        case 0: /* horizontal into wall */
            a->posX = 2.0; a->posY = 5.0; a->posZ = 8.0;
            a->motionX = 3.0; a->motionY = 0.0; a->motionZ = 0.0;
            break;
        case 1: /* parabolic onto floor */
            a->posX = 8.0; a->posY = 15.0; a->posZ = 8.0;
            a->motionX = 0.3; a->motionY = 1.5; a->motionZ = 0.0;
            break;
        case 2: /* diagonal into corner */
            a->posX = 2.0; a->posY = 8.0; a->posZ = 2.0;
            a->motionX = 2.0; a->motionY = 0.5; a->motionZ = 2.0;
            break;
        default: /* open air */
            a->posX = 8.0; a->posY = 20.0; a->posZ = 8.0;
            a->motionX = 1.0; a->motionY = 0.2; a->motionZ = 0.0;
            break;
    }
}

MC_HD static inline int pm_scenario_max_ticks(int idx) {
    return (idx == 3) ? 30 : PM_MAX_TICKS;
}

MC_HD static inline void pm_run_scenario(int idx, i64 seed, u16 *grid,
                                         McArrow *a, PmEmitFn emit, void *ctx) {
    (void)seed;
    switch (idx) {
        case 0: pm_scene_wall(grid); break;
        case 1: pm_scene_flat(grid); break;
        case 2: pm_scene_corner(grid); break;
        default: pm_scene_open(grid); break;
    }
    pm_scenario_init(idx, a);

    int max_t = pm_scenario_max_ticks(idx);
    for (int t = 0; t < max_t; ++t) {
        pm_arrow_tick(a, grid);
        pm_emit_double(a->posX, emit, ctx);
        pm_emit_double(a->posY, emit, ctx);
        pm_emit_double(a->posZ, emit, ctx);
    }
    emit((u64)(u32)a->inGround, ctx);
}

MC_HD static inline void pm_run_all(i64 seed, PmEmitFn emit, void *ctx) {
    u16 grid[PM_VOL];
    McArrow arrow;
    for (int i = 0; i < PM_NUM_SCENARIOS; ++i)
        pm_run_scenario(i, seed, grid, &arrow, emit, ctx);
}

#endif /* MC_PROJECTILE_MOTION_H */
