/* CANDIDATE: pure-C port of MC 1.11.2 World.checkLightFor() BFS light propagation
 *   (src/net/minecraft/world/World.java:3025 checkLightFor + :2967 getRawLight).
 *
 * The golden is CAPTURED FROM REAL MINECRAFT (capture_mode "live-hook"): a placed
 * block light source (luminance 15) at src, captured as the BEFORE light state of a
 * radius-R region (all the cells the update touches) plus per-cell block opacity, and
 * the AFTER per-cell block light the real checkLightFor() produced.
 *
 * golden/inputs.txt:
 *   line 0 (header): "# rel_to_first_cell src=(X,Y,Z) luminance=L radius=R"
 *   then one cell per line:  rel_x rel_y rel_z light_before opacity
 *     rel_*       : cell position relative to the first cell's anchor (src given in header)
 *     light_before: stored EnumSkyBlock.BLOCK light BEFORE the update (this capture: 0)
 *     opacity     : block getLightOpacity() at the cell (this capture: 0 = air)
 *
 * golden/golden.txt: the AFTER BLOCK light (0-15) per cell, SAME ORDER as inputs.
 *
 * We replay the exact decompiled BFS on the before-state and must BITWISE-match.
 *
 * Scope note: the captured region is the radius-R cube around src. The real method
 * also touches cells just outside it, but since this is a pure brightening update from
 * a single central source, every in-cube cell's value is fixed by inward (toward-src)
 * shortest paths that stay inside the cube; out-of-cube neighbors are strictly farther
 * (dimmer) and never raise an in-cube value. So we treat out-of-bounds getLightFor as 0
 * and confine the queue to the cube. NOTE: this exactness holds for THIS capture's geometry
 * (a single source centered in the cube). For an off-center / edge-adjacent source the
 * in_bounds enqueue guard could drop a brighter out-of-cube path; widen the capture region
 * (or drop the guard with real OOB world data) for that case. */
#include <stdio.h>
#include <stdlib.h>

#define MAXR 16
#define DIM  (2 * MAXR + 1)
#define CAP  32768   /* World.lightUpdateBlockList length */

static int R;                       /* radius */
static int sx, sy, sz;              /* source rel coords */
static int LUM;                     /* source luminance */
static signed char light[DIM][DIM][DIM];
static signed char opac[DIM][DIM][DIM];

static int in_bounds(int x, int y, int z) {
    return x >= -R && x <= R && y >= -R && y <= R && z >= -R && z <= R;
}
static int IX(int v) { return v + MAXR; }

static int getLightFor(int x, int y, int z) {
    if (!in_bounds(x, y, z)) return 0;
    return light[IX(x)][IX(y)][IX(z)];
}
static void setLightFor(int x, int y, int z, int v) {
    if (!in_bounds(x, y, z)) return;
    light[IX(x)][IX(y)][IX(z)] = (signed char)v;
}
static int lumin(int x, int y, int z) {
    return (x == sx && y == sy && z == sz) ? LUM : 0;
}
static int opacity(int x, int y, int z) {
    if (!in_bounds(x, y, z)) return 0;
    return opac[IX(x)][IX(y)][IX(z)];
}

/* World.getRawLight() for EnumSkyBlock.BLOCK */
static int getRawLight(int x, int y, int z) {
    int blockLight = lumin(x, y, z);
    int i = blockLight;            /* lightType==SKY ? 0 : blockLight */
    int j = opacity(x, y, z);
    if (j >= 15 && blockLight > 0) j = 1;
    if (j < 1) j = 1;
    if (j >= 15) return 0;
    if (i >= 14) return i;
    static const int dx[6] = {0, 0, -1, 1, 0, 0};
    static const int dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int dz[6] = {0, 0, 0, 0, -1, 1};
    for (int f = 0; f < 6; f++) {
        int k = getLightFor(x + dx[f], y + dy[f], z + dz[f]) - j;
        if (k > i) i = k;
        if (i >= 14) return i;
    }
    return i;
}

static int q[CAP];

int main(void) {
    char header[512];
    if (!fgets(header, sizeof(header), stdin)) return 1;
    if (sscanf(header, "# rel_to_first_cell src=(%d,%d,%d) luminance=%d radius=%d",
               &sx, &sy, &sz, &LUM, &R) != 5) {
        fprintf(stderr, "bad header: %s", header);
        return 1;
    }
    if (R > MAXR) { fprintf(stderr, "radius %d > MAXR\n", R); return 1; }

    /* read cells in order, remember order for output */
    int n = 0, cap = (2 * R + 1) * (2 * R + 1) * (2 * R + 1);
    int *cx = malloc(sizeof(int) * cap), *cy = malloc(sizeof(int) * cap),
        *cz = malloc(sizeof(int) * cap);
    int rx, ry, rz, lb, op;
    while (scanf("%d %d %d %d %d", &rx, &ry, &rz, &lb, &op) == 5) {
        light[IX(rx)][IX(ry)][IX(rz)] = (signed char)lb;
        opac[IX(rx)][IX(ry)][IX(rz)] = (signed char)op;
        cx[n] = rx; cy[n] = ry; cz[n] = rz; n++;
    }

    /* checkLightFor(BLOCK, src): work in rel coords (pos == origin). */
    int qi = 0, qj = 0;
    int k = getLightFor(sx, sy, sz);
    int l = getRawLight(sx, sy, sz);
    if (l > k) {
        q[qj++] = 133152;
    } else if (l < k) {
        /* darkening branch (not exercised by this capture, ported for fidelity) */
        q[qj++] = 133152 | (k << 18);
        while (qi < qj) {
            int l1 = q[qi++];
            int i2 = (l1 & 63) - 32 + sx;
            int j2 = (l1 >> 6 & 63) - 32 + sy;
            int k2 = (l1 >> 12 & 63) - 32 + sz;
            int l2 = l1 >> 18 & 15;
            int i3 = getLightFor(i2, j2, k2);
            if (i3 == l2) {
                setLightFor(i2, j2, k2, 0);
                if (l2 > 0) {
                    int j3 = abs(i2 - sx), k3 = abs(j2 - sy), l3 = abs(k2 - sz);
                    if (j3 + k3 + l3 < 17) {
                        static const int dx[6] = {0, 0, -1, 1, 0, 0};
                        static const int dy[6] = {-1, 1, 0, 0, 0, 0};
                        static const int dz[6] = {0, 0, 0, 0, -1, 1};
                        for (int f = 0; f < 6; f++) {
                            int i4 = i2 + dx[f], j4 = j2 + dy[f], k4 = k2 + dz[f];
                            int l4 = opacity(i4, j4, k4);
                            if (l4 < 1) l4 = 1;
                            i3 = getLightFor(i4, j4, k4);
                            if (i3 == l2 - l4 && qj < CAP) {
                                q[qj++] = (i4 - sx + 32) | (j4 - sy + 32 << 6) |
                                          (k4 - sz + 32 << 12) | (l2 - l4 << 18);
                            }
                        }
                    }
                }
            }
        }
        qi = 0;
    }

    /* brightening loop */
    while (qi < qj) {
        int i5 = q[qi++];
        int j5 = (i5 & 63) - 32 + sx;
        int k5 = (i5 >> 6 & 63) - 32 + sy;
        int l5 = (i5 >> 12 & 63) - 32 + sz;
        int i6 = getLightFor(j5, k5, l5);
        int j6 = getRawLight(j5, k5, l5);
        if (j6 != i6) {
            setLightFor(j5, k5, l5, j6);
            if (j6 > i6) {
                int k6 = abs(j5 - sx), l6 = abs(k5 - sy), i7 = abs(l5 - sz);
                int flag = qj < CAP - 6;
                if (k6 + l6 + i7 < 17 && flag) {
                    if (in_bounds(j5 - 1, k5, l5) && getLightFor(j5 - 1, k5, l5) < j6)
                        q[qj++] = (j5 - 1 - sx + 32) + (k5 - sy + 32 << 6) + (l5 - sz + 32 << 12);
                    if (in_bounds(j5 + 1, k5, l5) && getLightFor(j5 + 1, k5, l5) < j6)
                        q[qj++] = (j5 + 1 - sx + 32) + (k5 - sy + 32 << 6) + (l5 - sz + 32 << 12);
                    if (in_bounds(j5, k5 - 1, l5) && getLightFor(j5, k5 - 1, l5) < j6)
                        q[qj++] = (j5 - sx + 32) + (k5 - 1 - sy + 32 << 6) + (l5 - sz + 32 << 12);
                    if (in_bounds(j5, k5 + 1, l5) && getLightFor(j5, k5 + 1, l5) < j6)
                        q[qj++] = (j5 - sx + 32) + (k5 + 1 - sy + 32 << 6) + (l5 - sz + 32 << 12);
                    if (in_bounds(j5, k5, l5 - 1) && getLightFor(j5, k5, l5 - 1) < j6)
                        q[qj++] = (j5 - sx + 32) + (k5 - sy + 32 << 6) + (l5 - 1 - sz + 32 << 12);
                    if (in_bounds(j5, k5, l5 + 1) && getLightFor(j5, k5, l5 + 1) < j6)
                        q[qj++] = (j5 - sx + 32) + (k5 - sy + 32 << 6) + (l5 + 1 - sz + 32 << 12);
                }
            }
        }
    }

    for (int t = 0; t < n; t++)
        printf("%d\n", getLightFor(cx[t], cy[t], cz[t]));
    return 0;
}
