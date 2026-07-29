/* path_navigate: PathNavigateGround path-FOLLOW tick (not findPath).
 *
 * Oracle line ranges (java/oracle-src/net/minecraft/pathfinding/):
 *   PathNavigate.java
 *     onUpdateNavigation()     L226-265  (pathFollow + getPosition target; stuck/repath CUT)
 *     pathFollow()             L267-303
 *     noPath()                 L352-355
 *     setPath / speed          L186-214  (speed field only; removeSunnyPath no-op here)
 *     checkForStuck            L309-347  CUT (wall-clock System.currentTimeMillis)
 *   PathNavigateGround.java
 *     canNavigate()            L33-36    (flat ground: onGround only; swim/ride CUT)
 *     getEntityPosition()      L38-41
 *     getPathablePosY()        L98-124   (non-swim branch L121-123 only)
 *     isDirectPathBetweenPoints L174-250 OPEN-GRID simplify: no solids => always clear
 *                                 when horizontal d2 >= 1e-8 (DDA/isSafeToStand omitted)
 *   Path.java
 *     getVectorFromIndex       L90-96
 *     getPosition              L101-104
 *     getCurrentPos            L106-110
 *     index / isFinished       L32-85
 *
 * SCOPE: follow an already-found (or hand-built) Path of waypoints. getPathToEntityLiving /
 * findPath are OUT (pathfinding12 owns findPath). Entity advances by a fixed move speed toward
 * the current waypoint each tick on a flat plane - not full LivingBase.travel / MoveHelper.
 *
 * Battery: synthetic 4-8 point open-ground paths. Emit pathIndex, finished, posXYZ double bits
 * per tick as %016llx. java==cpu==cuda. */
#ifndef MC_PATH_NAVIGATE_H
#define MC_PATH_NAVIGATE_H

#include "mc.h"
#include <math.h>
#include <string.h>

#define PN_MAX_PTS   16
#define PN_NUM_CASES 6
#define PN_TICKS     40
/* per tick: pathIndex, finished, xbits, ybits, zbits */
#define PN_FIELDS    5
#define PN_OUT       (PN_NUM_CASES * PN_TICKS * PN_FIELDS)

typedef struct {
    i32 x, y, z;
} PnPoint;

typedef struct {
    PnPoint pts[PN_MAX_PTS];
    i32 pathLen;
    i32 pathIndex;
} PnPath;

typedef struct {
    double posX, posY, posZ;
    float width, height;
    int onGround;
    double speed;
    PnPath path;
} PnNav;

MC_HD static inline u64 pn_dbits(double d) {
    u64 u;
    memcpy(&u, &d, sizeof(u));
    return u;
}

/* MathHelper.floor(double) - PathNavigate + PathNavigateGround. */
MC_HD static inline i32 pn_floor_d(double v) {
    i32 i = (i32)v;
    return v < (double)i ? i - 1 : i;
}

/* MathHelper.ceil(float). */
MC_HD static inline i32 pn_ceil_f(float v) {
    i32 i = (i32)v;
    return v > (float)i ? i + 1 : i;
}

/* MathHelper.abs(float). */
MC_HD static inline float pn_abs_f(float v) {
    return v >= 0.0f ? v : -v;
}

/* PathNavigateGround.getPathablePosY non-swim: (int)(minY + 0.5). posY == minY on flat. */
MC_HD static inline i32 pn_pathable_pos_y(const PnNav *n) {
    return (i32)(n->posY + 0.5);
}

/* PathNavigateGround.getEntityPosition. */
MC_HD static inline void pn_entity_position(const PnNav *n, double *ox, double *oy, double *oz) {
    *ox = n->posX;
    *oy = (double)pn_pathable_pos_y(n);
    *oz = n->posZ;
}

/* PathNavigateGround.canNavigate flat-ground: onGround (swim/ride CUT). */
MC_HD static inline int pn_can_navigate(const PnNav *n) {
    return n->onGround != 0;
}

MC_HD static inline int pn_no_path(const PnPath *p) {
    return p->pathLen <= 0 || p->pathIndex >= p->pathLen;
}

/* Path.getVectorFromIndex */
MC_HD static inline void pn_vector_from_index(const PnNav *n, i32 index,
                                              double *ox, double *oy, double *oz) {
    i32 off = (i32)(n->width + 1.0f);
    const PnPoint *pt = &n->path.pts[index];
    *ox = (double)pt->x + (double)off * 0.5;
    *oy = (double)pt->y;
    *oz = (double)pt->z + (double)off * 0.5;
}

/* Path.getPosition */
MC_HD static inline void pn_get_position(const PnNav *n, double *ox, double *oy, double *oz) {
    pn_vector_from_index(n, n->path.pathIndex, ox, oy, oz);
}

/* Path.getCurrentPos - integer block coords of current index. */
MC_HD static inline void pn_current_pos(const PnPath *p, double *ox, double *oy, double *oz) {
    const PnPoint *pt = &p->pts[p->pathIndex];
    *ox = (double)pt->x;
    *oy = (double)pt->y;
    *oz = (double)pt->z;
}

/* PathNavigateGround.isDirectPathBetweenPoints OPEN-GRID simplify:
 * Vanilla DDA + isSafeToStandAt always succeed on open air/full-cube floor with no hazards.
 * Only the d2 < 1e-8 early-out remains. sizeX/Y/Z unused (no volume sweep). */
MC_HD static inline int pn_is_direct_path(double x1, double y1, double z1,
                                          double x2, double y2, double z2,
                                          int sizeX, int sizeY, int sizeZ) {
    (void)y1; (void)y2; (void)sizeX; (void)sizeY; (void)sizeZ;
    double d0 = x2 - x1;
    double d1 = z2 - z1;
    double d2 = d0 * d0 + d1 * d1;
    if (d2 < 1.0E-8) return 0;
    return 1;
}

/* PathNavigate.pathFollow - verbatim control flow (stuck check CUT). */
MC_HD static inline void pn_path_follow(PnNav *n) {
    double ex, ey, ez;
    pn_entity_position(n, &ex, &ey, &ez);

    i32 i = n->path.pathLen;
    for (i32 j = n->path.pathIndex; j < n->path.pathLen; ++j) {
        if ((double)n->path.pts[j].y != floor(ey)) {
            i = j;
            break;
        }
    }

    float maxDist = n->width > 0.75f ? n->width / 2.0f : 0.75f - n->width / 2.0f;
    double cx, cy, cz;
    pn_current_pos(&n->path, &cx, &cy, &cz);

    if (pn_abs_f((float)(n->posX - (cx + 0.5))) < maxDist
        && pn_abs_f((float)(n->posZ - (cz + 0.5))) < maxDist
        && fabs(n->posY - cy) < 1.0) {
        n->path.pathIndex = n->path.pathIndex + 1;
    }

    i32 k = pn_ceil_f(n->width);
    i32 l = pn_ceil_f(n->height);
    i32 i1 = k;

    if (!pn_no_path(&n->path)) {
        for (i32 j1 = i - 1; j1 >= n->path.pathIndex; --j1) {
            double tx, ty, tz;
            pn_vector_from_index(n, j1, &tx, &ty, &tz);
            if (pn_is_direct_path(ex, ey, ez, tx, ty, tz, k, l, i1)) {
                n->path.pathIndex = j1;
                break;
            }
        }
    }
}

/* Flat-plane step toward current waypoint (MoveHelper substitute; no LivingBase.travel). */
MC_HD static inline void pn_step_toward(PnNav *n) {
    if (pn_no_path(&n->path)) return;
    double tx, ty, tz;
    pn_get_position(n, &tx, &ty, &tz);
    double dx = tx - n->posX;
    double dz = tz - n->posZ;
    double dist = sqrt(dx * dx + dz * dz);
    if (dist > 1.0E-8) {
        double step = n->speed < dist ? n->speed : dist;
        n->posX += (dx / dist) * step;
        n->posZ += (dz / dist) * step;
    }
    (void)ty; /* flat plane: keep posY */
}

/* One navigation tick: canNavigate -> pathFollow -> step. Mirrors onUpdateNavigation slice. */
MC_HD static inline void pn_tick(PnNav *n) {
    if (pn_no_path(&n->path)) return;
    if (pn_can_navigate(n)) {
        pn_path_follow(n);
    }
    if (!pn_no_path(&n->path)) {
        pn_step_toward(n);
    }
}

MC_HD static inline void pn_set_point(PnPath *p, i32 i, i32 x, i32 y, i32 z) {
    p->pts[i].x = x;
    p->pts[i].y = y;
    p->pts[i].z = z;
}

/* Build battery case. All open flat ground y=5 walk cells; solid floor implied below. */
MC_HD static inline void pn_build_case(PnNav *n, int id) {
    memset(n, 0, sizeof(*n));
    n->width = 0.6f;
    n->height = 1.95f;
    n->onGround = 1;
    n->speed = 0.25;
    n->posY = 5.0;
    n->path.pathIndex = 0;

    switch (id) {
    case 0: /* straight +X, 6 pts, start at first center */
        n->path.pathLen = 6;
        pn_set_point(&n->path, 0, 2, 5, 2);
        pn_set_point(&n->path, 1, 4, 5, 2);
        pn_set_point(&n->path, 2, 6, 5, 2);
        pn_set_point(&n->path, 3, 8, 5, 2);
        pn_set_point(&n->path, 4, 10, 5, 2);
        pn_set_point(&n->path, 5, 12, 5, 2);
        n->posX = 2.5; n->posZ = 2.5;
        break;
    case 1: /* same path, start offset from first cell (no immediate close-advance) */
        n->path.pathLen = 6;
        pn_set_point(&n->path, 0, 2, 5, 2);
        pn_set_point(&n->path, 1, 4, 5, 2);
        pn_set_point(&n->path, 2, 6, 5, 2);
        pn_set_point(&n->path, 3, 8, 5, 2);
        pn_set_point(&n->path, 4, 10, 5, 2);
        pn_set_point(&n->path, 5, 12, 5, 2);
        n->posX = 1.2; n->posZ = 2.5;
        break;
    case 2: /* wide entity (width 1.4 -> entitySize offset 1.0, maxDist = width/2) */
        n->width = 1.4f;
        n->path.pathLen = 5;
        pn_set_point(&n->path, 0, 2, 5, 4);
        pn_set_point(&n->path, 1, 5, 5, 4);
        pn_set_point(&n->path, 2, 8, 5, 4);
        pn_set_point(&n->path, 3, 11, 5, 4);
        pn_set_point(&n->path, 4, 14, 5, 4);
        n->posX = 2.5; n->posZ = 4.5;
        n->speed = 0.3;
        break;
    case 3: /* L-shaped 7 pts: +X then +Z */
        n->path.pathLen = 7;
        pn_set_point(&n->path, 0, 2, 5, 2);
        pn_set_point(&n->path, 1, 4, 5, 2);
        pn_set_point(&n->path, 2, 6, 5, 2);
        pn_set_point(&n->path, 3, 8, 5, 2);
        pn_set_point(&n->path, 4, 8, 5, 4);
        pn_set_point(&n->path, 5, 8, 5, 6);
        pn_set_point(&n->path, 6, 8, 5, 8);
        n->posX = 2.5; n->posZ = 2.5;
        n->speed = 0.2;
        break;
    case 4: /* start mid-path index 2, 5 remaining-relevant pts (len 5, index 2) */
        n->path.pathLen = 5;
        pn_set_point(&n->path, 0, 0, 5, 0);
        pn_set_point(&n->path, 1, 2, 5, 0);
        pn_set_point(&n->path, 2, 4, 5, 0);
        pn_set_point(&n->path, 3, 6, 5, 0);
        pn_set_point(&n->path, 4, 8, 5, 0);
        n->path.pathIndex = 2;
        n->posX = 4.5; n->posZ = 0.5;
        break;
    case 5: /* already at final cell center: one tick should finish after look-ahead/close */
        n->path.pathLen = 4;
        pn_set_point(&n->path, 0, 2, 5, 2);
        pn_set_point(&n->path, 1, 4, 5, 2);
        pn_set_point(&n->path, 2, 6, 5, 2);
        pn_set_point(&n->path, 3, 8, 5, 2);
        n->posX = 8.5; n->posZ = 2.5;
        n->path.pathIndex = 3;
        break;
    default:
        n->path.pathLen = 0;
        break;
    }
}

MC_HD static inline void pn_run(u64 *out) {
    int o = 0;
    for (int c = 0; c < PN_NUM_CASES; ++c) {
        PnNav n;
        pn_build_case(&n, c);
        for (int t = 0; t < PN_TICKS; ++t) {
            pn_tick(&n);
            out[o++] = (u64)(u32)n.path.pathIndex;
            out[o++] = (u64)(u32)(pn_no_path(&n.path) ? 1 : 0);
            out[o++] = pn_dbits(n.posX);
            out[o++] = pn_dbits(n.posY);
            out[o++] = pn_dbits(n.posZ);
        }
    }
}

#endif /* MC_PATH_NAVIGATE_H */
