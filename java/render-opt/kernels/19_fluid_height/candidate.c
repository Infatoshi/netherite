/* CANDIDATE: pure-C port of MC 1.11.2 BlockFluidRenderer.getFluidHeight()
 *   (src/net/minecraft/client/renderer/BlockFluidRenderer.java:273).
 *
 * The golden is CAPTURED FROM REAL MINECRAFT (capture_mode "live-hook"): NetheriteMod
 * (command "capture_fluidheight") samples loaded fluid block positions around the player,
 * reflectively invokes the real (private) getFluidHeight(blockAccess, pos, blockMaterial),
 * and records, per query, the exact per-neighbor decisions the method makes plus the
 * method's raw float output bits.
 *
 * The method loops j=0..3 over neighbor offsets (-(j&1), 0, -(j>>1&1)) relative to pos:
 *   j=0 -> (0,0,0)=pos itself, j=1 -> (-1,0,0), j=2 -> (0,0,-1), j=3 -> (-1,0,-1).
 * For each j it (1) checks if the block ABOVE that neighbor has the same material
 * (if so the whole method returns 1.0F immediately), then (2) classifies the neighbor.
 *
 * Input record (one per line, from golden/inputs.txt): 12 ints = 4 triples
 *     upSame0 kind0 k0  upSame1 kind1 k1  upSame2 kind2 k2  upSame3 kind3 k3
 *   upSame : block above this neighbor has material == blockMaterial (0/1)
 *   kind   : 0 = same material (a liquid block, level k matters)
 *            1 = different material, NOT solid (counts as full height: ++f,++i)
 *            2 = different material, solid (ignored)
 *   k      : BlockLiquid.LEVEL value of that block (only meaningful when kind==0)
 *
 * Output: one int per line = Float.floatToRawIntBits(getFluidHeight) (32-bit signed
 * decimal). Must BITWISE-match golden/golden.txt. Build with -ffp-contract=off so the
 * float accumulation rounds identically to the JVM. */
#include <stdio.h>
#include <string.h>

/* verbatim from BlockLiquid.getLiquidHeightPercent(int) */
static float getLiquidHeightPercent(int meta) {
    if (meta >= 8) meta = 0;
    return (float)(meta + 1) / 9.0F;
}

static float fluid_height(const int up[4], const int kind[4], const int k[4]) {
    int i = 0;
    float f = 0.0F;
    for (int j = 0; j < 4; ++j) {
        if (up[j]) return 1.0F;               /* up neighbor is same material -> short-circuit */
        if (kind[j] == 0) {                   /* same-material liquid block */
            int kk = k[j];
            if (kk >= 8 || kk == 0) {
                f += getLiquidHeightPercent(kk) * 10.0F;
                i += 10;
            }
            f += getLiquidHeightPercent(kk);
            ++i;
        } else if (kind[j] == 1) {            /* different, non-solid */
            ++f;
            ++i;
        }
        /* kind==2 (solid) contributes nothing */
    }
    return 1.0F - f / (float)i;
}

int main(void) {
    int up[4], kind[4], k[4];
    while (scanf("%d %d %d %d %d %d %d %d %d %d %d %d",
                 &up[0], &kind[0], &k[0], &up[1], &kind[1], &k[1],
                 &up[2], &kind[2], &k[2], &up[3], &kind[3], &k[3]) == 12) {
        float out = fluid_height(up, kind, k);
        int bits;
        memcpy(&bits, &out, sizeof(bits));
        printf("%d\n", bits);
    }
    return 0;
}
