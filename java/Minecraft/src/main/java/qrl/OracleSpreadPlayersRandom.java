package qrl;

import java.util.Random;

/** Test-only constructor seed for the bounded spreadplayers oracle case. */
public final class OracleSpreadPlayersRandom {
    private static boolean armed;
    private static long seed;

    private OracleSpreadPlayersRandom() { }

    public static synchronized void arm(long value) {
        seed = value;
        armed = true;
    }

    public static synchronized Random construct() {
        if (!armed) return new Random();
        armed = false;
        return new Random(seed);
    }

    public static synchronized void clear() {
        armed = false;
    }
}
