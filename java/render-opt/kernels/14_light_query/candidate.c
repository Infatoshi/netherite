/* CANDIDATE: pure-C port of MC 1.11.2 World.getLightFromNeighborsFor()
 *   (src/net/minecraft/world/World.java:852, with Chunk.getLightFor()).
 *
 * The golden is CAPTURED FROM REAL MINECRAFT (capture_mode "live-hook"): NetheriteMod
 * samples loaded block positions around the player and records, per (pos,type) query,
 * the exact light values the method reads plus the method's own output.
 *
 * Input record (one per line, from golden/inputs.txt):
 *     type nb up east west south north own
 *   type : 0=BLOCK, 1=SKY            (informational; output is already baked into the values)
 *   nb   : useNeighborBrightness() of the block at pos (0/1)
 *   up east west south north : World.getLightFor(type, pos.<dir>())  -- the 5 neighbors maxed
 *   own  : World.getLightFor(type, pos)
 *
 * Logic (verbatim from the decompiled method): for a loaded+valid pos,
 *   nb ? max(up, east, west, south, north) : own
 * Note: it maxes only 5 neighbors (NOT down, NOT self). Validity/loading of each neighbor
 * is already folded into the recorded getLightFor values, so this C needs no world.
 *
 * Prints one int (0-15) per line; must BITWISE-match golden/golden.txt. */
#include <stdio.h>

static int light_query(int nb, int up, int east, int west, int south, int north, int own) {
    if (!nb) return own;
    int m = up;
    if (east  > m) m = east;
    if (west  > m) m = west;
    if (south > m) m = south;
    if (north > m) m = north;
    return m;
}

int main(void) {
    int type, nb, up, east, west, south, north, own;
    while (scanf("%d %d %d %d %d %d %d %d",
                 &type, &nb, &up, &east, &west, &south, &north, &own) == 8) {
        printf("%d\n", light_query(nb, up, east, west, south, north, own));
    }
    return 0;
}
