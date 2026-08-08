package qrl;

import java.util.Random;

/** Shared state for the test-only ItemShears constructor redirect. */
public final class OracleShearsRandom {
    private static boolean armed;
    private static long seed48;
    private static Random lastRandom;

    private OracleShearsRandom() { }

    public static synchronized void arm(long rawSeed48) {
        seed48 = rawSeed48 & ((1L << 48) - 1L);
        armed = true;
        lastRandom = null;
    }

    public static synchronized Random construct() {
        Random random = armed
            ? new Random(seed48 ^ 0x5DEECE66DL)
            : new Random();
        if (armed) {
            armed = false;
            lastRandom = random;
        }
        return random;
    }

    public static synchronized Random lastRandom() {
        return lastRandom;
    }

    public static synchronized void clear() {
        armed = false;
        lastRandom = null;
    }
}
