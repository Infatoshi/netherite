package netheritemod;

import java.util.Random;
import java.util.concurrent.ConcurrentHashMap;

/**
 * Default-off deterministic Entity.rand (lane/detmob). Vanilla 1.11.2 does
 * {@code rand = new Random()} from nanoTime, so two Java runs diverge and magma
 * cannot match ambient AI. When {@link QLaunch#DET_ENTITY_RNG} is on, each
 * Entity constructor gets {@code new Random(userSeed(entityId))} before the
 * UUID draws. Magma reads the logged seed48 from the tape; entity-id order
 * does not need to be derivable.
 *
 * userSeed is stable but arbitrary. seed48_init is the internal 48-bit LCG
 * cursor after the Random constructor scramble and before any next() call.
 */
public final class DetEntityRng {
    /** "NETHRNL" — mix constant, not a security key. */
    public static final long MIX = 0x4E455448524E4CL;
    public static final long GOLDEN = 0x9E3779B97F4A7C15L;
    private static final long JR_MULT = 0x5DEECE66DL;
    private static final long JR_MASK = (1L << 48) - 1L;

    private static final ConcurrentHashMap<Integer, Long> USER =
        new ConcurrentHashMap<Integer, Long>();
    private static final ConcurrentHashMap<Integer, Long> INIT48 =
        new ConcurrentHashMap<Integer, Long>();

    private DetEntityRng() {}

    public static long userSeed(int entityId) {
        return MIX ^ ((long) entityId * GOLDEN);
    }

    /** Internal cursor after {@code new Random(userSeed)} and 0 draws. */
    public static long seed48Init(long userSeed) {
        return (userSeed ^ JR_MULT) & JR_MASK;
    }

    public static Random create(int entityId, boolean record) {
        long us = userSeed(entityId);
        Random r = new Random(us);
        if (record) {
            Integer k = Integer.valueOf(entityId);
            USER.put(k, Long.valueOf(us));
            INIT48.put(k, Long.valueOf(seed48Init(us)));
        }
        return r;
    }

    public static long loggedUserSeed(int entityId, long fallback) {
        Long v = USER.get(Integer.valueOf(entityId));
        return v == null ? fallback : v.longValue();
    }

    public static long loggedInit48(int entityId, long fallback) {
        Long v = INIT48.get(Integer.valueOf(entityId));
        return v == null ? fallback : v.longValue();
    }
}
