/* CANDIDATE: pure-C port of MC 1.11.2 BiomeColorHelper.getColorAtPos()
 *   (src/net/minecraft/world/biome/BiomeColorHelper.java:33).
 *
 * The golden is CAPTURED FROM REAL MINECRAFT (capture_mode "live-hook"): NetheriteMod
 * (command "capture_biome") samples loaded positions around the player, records the 9
 * per-block biome GRASS colors that the 3x3 blend reads, and records the method's blended
 * int output.
 *
 * The method sums the 9 colors channel-wise over the 3x3 box (pos-1,-1 .. pos+1,+1),
 * integer-divides each channel sum by 9, masks to a byte, and repacks:
 *     return (i/9 & 255) << 16 | (j/9 & 255) << 8 | k/9 & 255;
 * where i,j,k are the summed R,G,B channels. Integer arithmetic only -> bitwise compare.
 *
 * Note: this is the GRASS_COLOR resolver (getGrassColorAtPos); the method is generic over
 * grass/foliage/water but only the resolver differs, the blend is identical.
 *
 * Input record (one per line, from golden/inputs.txt): 9 ints = the 9 packed 0xRRGGBB
 *   colors of the 3x3 grid (order matches BlockPos.getAllInBoxMutable; order-independent
 *   for the sum anyway).
 * Output: one int per line = blended color. Must BITWISE-match golden/golden.txt. */
#include <stdio.h>

static int blend(const int c[9]) {
    int i = 0, j = 0, k = 0;
    for (int n = 0; n < 9; ++n) {
        int l = c[n];
        i += (l & 16711680) >> 16;   /* R */
        j += (l & 65280) >> 8;       /* G */
        k += l & 255;                /* B */
    }
    return (i / 9 & 255) << 16 | (j / 9 & 255) << 8 | k / 9 & 255;
}

int main(void) {
    int c[9];
    while (scanf("%d %d %d %d %d %d %d %d %d",
                 &c[0], &c[1], &c[2], &c[3], &c[4], &c[5], &c[6], &c[7], &c[8]) == 9) {
        printf("%d\n", blend(c));
    }
    return 0;
}
