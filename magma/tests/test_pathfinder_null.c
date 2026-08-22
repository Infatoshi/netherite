/* PathFinder.findPath returns null when closest == start
 * (PathFinder.java:113-116). PathNavigate.getPathToPos returns null when
 * !canNavigate (PathNavigate.java:108-112). PathNavigateGround.getPathToPos
 * walk-up (PathNavigateGround.java:76-83) can place dest outside magma's
 * 32x24x32 window; on an open floor A* still returns a same-Y closest path
 * (a neighbour is closer), so PathFinder.java:113-116 does not null that
 * case by itself. */
#include "path_finder.h"
#include "pathfinding12.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

static void expect_n(const char *tag, int n, int want)
{
    if (n != want) {
        printf("FAIL: %s n=%d want=%d\n", tag, n, want);
        fails++;
    }
}

int main(void)
{
    Pf12 *p = (Pf12 *)calloc(1, sizeof *p);
    Pf12Case c;
    int n, start_idx, dest_idx;

    if (!p) {
        perror("calloc");
        return 1;
    }

    /* Same-cell dest: A* closest stays at start -> n=0. */
    c = pf12_build_case(p, 0, 0);
    pnp_initProcessor(p);
    start_idx = pnp_getStart(p);
    dest_idx = pnp_getPathPointToCoords(p, p->ent.posX, p->ent.posY, p->ent.posZ);
    n = pf12_findPath_pts(p, start_idx, dest_idx, 16.0f);
    expect_n("closest==start same cell", n, 0);
    if (start_idx != dest_idx) {
        float md = pnp_distanceManhattan(&p->points[start_idx], &p->points[dest_idx]);
        if (md != 0.0f) {
            printf("FAIL: same-cell manhattan %g\n", md);
            fails++;
        }
    }

    /* Reachable dest on the flat battery (case 0 walks to a far cell). */
    c = pf12_build_case(p, 0, 0);
    n = pf12_findPath(p, c.tx, c.ty, c.tz, c.maxDistance);
    if (n <= 0) {
        printf("FAIL: flat case 0 expected a path, n=%d dest=(%g,%g,%g)\n",
               n, c.tx, c.ty, c.tz);
        fails++;
    }

    /* Far-Y dest (nether T182511 walk-up to y=96, ~37 above walk Y). Open
     * floor: a same-Y neighbour is closer, so closest != start and n>0.
     * PathFinder.java:113-116 does not explain a Java null by itself. */
    {
        double far_y = (double)PF12_WALK_Y + 37.5;
        c = pf12_build_case(p, 0, 0);
        n = pf12_findPath(p, p->ent.posX + 1.0, far_y, p->ent.posZ, 48.0f);
        if (n <= 0) {
            printf("FAIL: far-Y dest on open floor expected n>0, n=%d\n", n);
            fails++;
        } else {
            int ey = p->resultPts[(n - 1) * 3 + 1];
            if (ey != PF12_WALK_Y) {
                printf("FAIL: far-Y closest end y=%d want walk Y %d\n",
                       ey, PF12_WALK_Y);
                fails++;
            }
        }
    }

    /* PathNavigateGround.canNavigate (java:33-36) is onGround || swim;
     * live pai_can_navigate returns null before findPath
     * (PathNavigate.java:108-112). Not re-tested here: the wrapper is static
     * in mob_live.c. */

    free(p);
    if (fails) {
        printf("%d FAIL\n", fails);
        return 1;
    }
    printf("ok pathfinder closest==start null; far-Y open-floor n>0\n");
    return 0;
}
