/* CANDIDATE: pure-C port of MC 1.11.2 Block.shouldSideBeRendered()
 *   (src/net/minecraft/block/Block.java:471).
 *
 * The golden is CAPTURED FROM REAL MINECRAFT (capture_mode "live-hook"): NetheriteMod
 * (capture_shouldsiderender) samples loaded blocks that use the BASE Block implementation
 * (no override) plus staged partial-bb blocks (soul sand, cactus), and records per (pos,side)
 * the exact snapshot the method reads plus the method's boolean output.
 *
 * Input record (one per line, from golden/inputs.txt):
 *     side minXbits minYbits minZbits maxXbits maxYbits maxZbits nbr
 *   side : EnumFacing.getIndex()  0=DOWN 1=UP 2=NORTH 3=SOUTH 4=WEST 5=EAST
 *   *bits: AxisAlignedBB component as Double.doubleToRawLongBits (the block's bounding box)
 *   nbr  : blockAccess.getBlockState(pos.offset(side)).doesSideBlockRendering(...) (0/1)
 *
 * Logic (verbatim from the decompiled method): per side, an early "return true" if the box
 * does not reach that face, else "return !neighborDoesSideBlockRendering".
 *
 * Prints one int (0/1) per line; must BITWISE-match golden/golden.txt. */
#include <stdio.h>
#include <string.h>

static double bits2d(long long b) { double d; memcpy(&d, &b, 8); return d; }

int main(void) {
    int side, nbr;
    long long mnx, mny, mnz, mxx, mxy, mxz;
    while (scanf("%d %lld %lld %lld %lld %lld %lld %d",
                 &side, &mnx, &mny, &mnz, &mxx, &mxy, &mxz, &nbr) == 8) {
        double minX = bits2d(mnx), minY = bits2d(mny), minZ = bits2d(mnz);
        double maxX = bits2d(mxx), maxY = bits2d(mxy), maxZ = bits2d(mxz);
        int early = 0;
        switch (side) {
            case 0: if (minY > 0.0) early = 1; break; /* DOWN  */
            case 1: if (maxY < 1.0) early = 1; break; /* UP    */
            case 2: if (minZ > 0.0) early = 1; break; /* NORTH */
            case 3: if (maxZ < 1.0) early = 1; break; /* SOUTH */
            case 4: if (minX > 0.0) early = 1; break; /* WEST  */
            case 5: if (maxX < 1.0) early = 1; break; /* EAST  */
        }
        printf("%d\n", early ? 1 : (nbr ? 0 : 1));
    }
    return 0;
}
