/* path_finder: verbatim MC 1.11.2 PathHeap + PathFinder over the pf12 node processor.
 *
 * Port target: java/oracle-src/net/minecraft/pathfinding/PathHeap.java (binary min-heap keyed on
 * PathPoint.distanceToTarget) and PathFinder.java (the A* loop: 200-iteration cap, decrease-key via
 * changeDistance, maxDistance gates, closest-point fallback, createEntityPath backtrace).
 *
 * Tie behavior that makes expansion order observable (matched exactly, per task):
 *   sortBack  uses strict `<`  (parent swap only when strictly smaller).
 *   sortForward chooses the RIGHT child on equal child priority (`if (f1 < f2)` false when equal),
 *              and stops with `>=` against the sifting node's key.
 */
#ifndef MC_PATH_FINDER_H
#define MC_PATH_FINDER_H

#include "path_node_processor.h"

/* ---- PathHeap (PathHeap.java) : operates on Pf12.heap[] of point indices ---- */
MC_HD static inline void pfh_clear(Pf12 *p) { p->heapCount = 0; }
MC_HD static inline int  pfh_empty(Pf12 *p) { return p->heapCount == 0; }

/* sortBack (PathHeap.java:82) */
MC_HD static inline void pfh_sortBack(Pf12 *p, int index) {
    int pidx = p->heap[index];
    float f = p->points[pidx].distanceToTarget;
    while (index > 0) {
        int i = (index - 1) >> 1;
        int pidx1 = p->heap[i];
        if (f >= p->points[pidx1].distanceToTarget) break;
        p->heap[index] = pidx1;
        p->points[pidx1].index = index;
        index = i;
    }
    p->heap[index] = pidx;
    p->points[pidx].index = index;
}

/* sortForward (PathHeap.java:108) */
MC_HD static inline void pfh_sortForward(Pf12 *p, int index) {
    int pidx = p->heap[index];
    float f = p->points[pidx].distanceToTarget;
    for (;;) {
        int i = 1 + (index << 1);
        int j = i + 1;
        if (i >= p->heapCount) break;
        int pidx1 = p->heap[i];
        float f1 = p->points[pidx1].distanceToTarget;
        int pidx2;
        float f2;
        if (j >= p->heapCount) { pidx2 = -1; f2 = INFINITY; }
        else { pidx2 = p->heap[j]; f2 = p->points[pidx2].distanceToTarget; }
        if (f1 < f2) {
            if (f1 >= f) break;
            p->heap[index] = pidx1; p->points[pidx1].index = index; index = i;
        } else {
            if (f2 >= f) break;
            p->heap[index] = pidx2; p->points[pidx2].index = index; index = j;
        }
    }
    p->heap[index] = pidx;
    p->points[pidx].index = index;
}

/* addPoint (PathHeap.java:13) */
MC_HD static inline void pfh_addPoint(Pf12 *p, int pidx) {
    /* point.index must be < 0 (PathHeap "OW KNOWS!"); array is fixed-size (no doubling needed). */
    int c = p->heapCount;
    p->heap[c] = pidx;
    p->points[pidx].index = c;
    pfh_sortBack(p, c);
    p->heapCount = c + 1;
}

/* dequeue (PathHeap.java:46) */
MC_HD static inline int pfh_dequeue(Pf12 *p) {
    int ret = p->heap[0];
    p->heapCount -= 1;
    p->heap[0] = p->heap[p->heapCount];
    if (p->heapCount > 0) pfh_sortForward(p, 0);
    p->points[ret].index = -1;
    return ret;
}

/* changeDistance (PathHeap.java:64) */
MC_HD static inline void pfh_changeDistance(Pf12 *p, int pidx, float distance) {
    float f = p->points[pidx].distanceToTarget;
    p->points[pidx].distanceToTarget = distance;
    if (distance < f) pfh_sortBack(p, p->points[pidx].index);
    else pfh_sortForward(p, p->points[pidx].index);
}

/* ---- PathFinder.findPath (PathFinder.java:50) ---- */
/* Returns the number of path points written to p->resultPts (start..end, inclusive), 0 if null.
 * Also sets p->resultDist = end.totalPathDistance (float bits emitted by the driver). */
MC_HD static inline int pf12_findPath_pts(Pf12 *p, int startIdx, int targetIdx, float maxDistance) {
    p->resultLen = 0;
    p->resultDist = 0.0f;

    PfPoint *s = &p->points[startIdx];
    PfPoint *tgt = &p->points[targetIdx];
    s->totalPathDistance = 0.0f;
    s->distanceToNext = pnp_distanceManhattan(s, tgt);
    s->distanceToTarget = s->distanceToNext;
    pfh_clear(p);
    /* closedSet.clear() is dead code in 1.11.2 (never read) - omitted. */
    pfh_addPoint(p, startIdx);

    int pathpoint = startIdx;
    int i = 0;

    while (!pfh_empty(p)) {
        ++i;
        if (i >= 200) break;

        int cur = pfh_dequeue(p);

        if (p->points[cur].hash == tgt->hash &&
            p->points[cur].x == tgt->x && p->points[cur].y == tgt->y && p->points[cur].z == tgt->z) {
            pathpoint = targetIdx;
            break;
        }

        if (pnp_distanceManhattan(&p->points[cur], tgt) <
            pnp_distanceManhattan(&p->points[pathpoint], tgt))
            pathpoint = cur;

        p->points[cur].visited = 1;
        int j = pnp_findPathOptions(p, p->pathOptions, cur, targetIdx, maxDistance);

        for (int k = 0; k < j; ++k) {
            int pp2 = p->pathOptions[k];
            float f = pnp_distanceManhattan(&p->points[cur], &p->points[pp2]);
            p->points[pp2].distanceFromOrigin = p->points[cur].distanceFromOrigin + f;
            p->points[pp2].cost = f + p->points[pp2].costMalus;
            float f1 = p->points[cur].totalPathDistance + p->points[pp2].cost;

            if (p->points[pp2].distanceFromOrigin < maxDistance &&
                (!(p->points[pp2].index >= 0) || f1 < p->points[pp2].totalPathDistance)) {
                p->points[pp2].previous = cur;
                p->points[pp2].totalPathDistance = f1;
                p->points[pp2].distanceToNext =
                    pnp_distanceManhattan(&p->points[pp2], tgt) + p->points[pp2].costMalus;

                if (p->points[pp2].index >= 0) {
                    pfh_changeDistance(p, pp2,
                        p->points[pp2].totalPathDistance + p->points[pp2].distanceToNext);
                } else {
                    p->points[pp2].distanceToTarget =
                        p->points[pp2].totalPathDistance + p->points[pp2].distanceToNext;
                    pfh_addPoint(p, pp2);
                }
            }
        }
    }

    if (pathpoint == startIdx) {
        p->resultLen = 0;
        return 0;
    }

    /* createEntityPath (PathFinder.java:127): count via previous chain, then fill start..end. */
    int n = 1;
    for (int q = pathpoint; p->points[q].previous != -1; q = p->points[q].previous) ++n;

    int cap = (int)(sizeof(p->resultPts) / sizeof(p->resultPts[0])) / 3;
    if (n > cap) { p->overflow = 1; n = cap; }

    int idx = n - 1;
    int q = pathpoint;
    /* write end at idx, then walk previous */
    p->resultPts[idx * 3 + 0] = p->points[q].x;
    p->resultPts[idx * 3 + 1] = p->points[q].y;
    p->resultPts[idx * 3 + 2] = p->points[q].z;
    while (p->points[q].previous != -1 && idx > 0) {
        q = p->points[q].previous;
        --idx;
        p->resultPts[idx * 3 + 0] = p->points[q].x;
        p->resultPts[idx * 3 + 1] = p->points[q].y;
        p->resultPts[idx * 3 + 2] = p->points[q].z;
    }

    p->resultLen = n;
    p->resultDist = p->points[pathpoint].totalPathDistance;
    return n;
}

/* Full entry: PathFinder.findPath(world, entity, x,y,z) (PathFinder.java:38) -> getStart /
 * getPathPointToCoords then the A* loop. Target coords are block-center (x+0.5). */
MC_HD static inline int pf12_findPath(Pf12 *p, double tx, double ty, double tz, float maxDistance) {
    pnp_initProcessor(p);
    int startIdx = pnp_getStart(p);
    int targetIdx = pnp_getPathPointToCoords(p, tx, ty, tz);
    return pf12_findPath_pts(p, startIdx, targetIdx, maxDistance);
}

#endif /* MC_PATH_FINDER_H */
