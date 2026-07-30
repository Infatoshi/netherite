// Verbatim MC 1.11.2 mob-spawner RNG-FREE ground truth (isActivated + delay countdown).
// Eval-pure: no game launch.
//
// Logic copied VERBATIM from the decompiled oracle:
//   net/minecraft/world/World.java  isAnyPlayerWithinRangeAt(): `range < 0 || getDistanceSq < range*range`
//   net/minecraft/entity/Entity.java getDistanceSq(x,y,z): dx*dx + dy*dy + dz*dz
//   net/minecraft/tileentity/MobSpawnerBaseLogic.java  isActivated() (center = block + 0.5),
//     updateSpawner() delay path: `if (spawnDelay > 0) { --spawnDelay; return; }`, and the
//     `if (!isActivated()) { ... }` early-out that leaves spawnDelay untouched.
//
// DOCUMENTED DIVERGENCE (see core/spawner_activate.h): the spawn-position draws and per-reset
// spawnDelay VALUES use the sim's hash-based stateless RNG (SPEC.md rule 1), which is NOT
// bit-comparable to vanilla's stateful world.rand -- a sanctioned architectural substitution.
// This golden covers only the RNG-free surface. Output matches cpu/spawner_activate.c:
// SA_NPOS activation flags then SA_NCD countdown results, %016llx.
public class Golden {
    static final double RANGE = 16.0;
    // spawner (8,65,8); center = (8.5, 65.5, 8.5).
    static final double CX = 8 + 0.5, CY = 65 + 0.5, CZ = 8 + 0.5;

    // player positions (stored as float in the C scene, chosen exact in float and double).
    static final float[][] POS = {
        { 8.5f, 65.5f,  8.5f}, {24.5f, 65.5f,  8.5f}, {23.5f, 65.5f,  8.5f}, {25.5f, 65.5f,  8.5f},
        { 8.5f, 65.5f, 24.5f}, { 8.5f, 81.5f,  8.5f}, {14.5f, 71.5f,  8.5f}, {18.5f, 75.5f,  8.5f},
        {19.5f, 75.5f,  8.5f}, {20.5f, 76.5f,  8.5f}, {1000.5f, 65.5f, 8.5f}, {-7.5f, 65.5f, 8.5f},
    };
    // {activated, initial spawnDelay, nticks}
    static final int[][] CD = { {1, 10, 3}, {1, 10, 9}, {0, 10, 5}, {1, 200, 50}, {1, 50, 49} };

    // World.isAnyPlayerWithinRangeAt / MobSpawnerBaseLogic.isActivated (single player).
    static boolean isActivated(double px, double py, double pz) {
        double d0 = px - CX, d1 = py - CY, d2 = pz - CZ;
        double distSq = d0 * d0 + d1 * d1 + d2 * d2;   // Entity.getDistanceSq
        return RANGE < 0.0 || distSq < RANGE * RANGE;
    }

    static void emit(StringBuilder sb, int v) {
        sb.append(String.format("%016x", ((long) v) & 0xFFFFFFFFL)).append('\n');
    }

    public static void main(String[] args) {
        StringBuilder sb = new StringBuilder();
        for (float[] p : POS)
            emit(sb, isActivated((double) p[0], (double) p[1], (double) p[2]) ? 1 : 0);
        for (int[] c : CD) {
            boolean activated = c[0] != 0;
            int delay = c[1];
            for (int t = 0; t < c[2]; ++t) {
                // updateSpawner: !activated -> no change; else the delay>0 branch decrements.
                if (activated && delay > 0) delay--;
            }
            emit(sb, delay);
        }
        System.out.print(sb);
    }
}
