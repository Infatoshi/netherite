// Verbatim MC 1.11.2 weather timer + worldTime advance ground truth (vanilla).
// Eval-pure: no game launch. Args: [seed=12345] [nticks=256], matching cpu/world_weather.c.
//
// Logic copied VERBATIM from the decompiled oracle:
//   net/minecraft/world/World.java          updateWeatherBody rain/thunder timers
//                                           (doWeatherCycle, cleanWeatherTime==0 path)
//   net/minecraft/world/WorldServer.java    tick totalWorldTime++ / worldTime++ under
//                                           doDaylightCycle
//   net/minecraft/world/storage/WorldInfo   rainTime, thunderTime, raining, thundering
//
// Fixed harness initial WorldInfo (matches core/world_weather.h):
//   totalTime=0, worldTime=0, raining=true rainTime=50, thundering=false thunderTime=100.
// Re-roll ranges (Random.nextInt):
//   thundering:  nextInt(12000)+3600 ; clear thunder: nextInt(168000)+12000
//   raining:     nextInt(12000)+12000; clear rain:    nextInt(168000)+12000
// Output: nticks * 6 lines of %016x (totalTime, worldTime, rainTime, thunderTime,
// raining, thundering) matching cpu/world_weather.c.
import java.util.Random;

public class Golden {
    static final int NTICKS = 256;
    static final int INIT_RAIN_TIME = 50;
    static final int INIT_THUNDER_TIME = 100;
    static final boolean INIT_RAINING = true;
    static final boolean INIT_THUNDERING = false;

    static long totalTime;
    static long worldTime;
    static int rainTime;
    static int thunderTime;
    static boolean raining;
    static boolean thundering;
    static Random rand;

    // World.updateWeatherBody timer body: doWeatherCycle=true, cleanWeatherTime=0.
    static void updateWeather() {
        int j = thunderTime;
        if (j <= 0) {
            if (thundering) {
                thunderTime = rand.nextInt(12000) + 3600;
            } else {
                thunderTime = rand.nextInt(168000) + 12000;
            }
        } else {
            --j;
            thunderTime = j;
            if (j <= 0) {
                thundering = !thundering;
            }
        }

        int k = rainTime;
        if (k <= 0) {
            if (raining) {
                rainTime = rand.nextInt(12000) + 12000;
            } else {
                rainTime = rand.nextInt(168000) + 12000;
            }
        } else {
            --k;
            rainTime = k;
            if (k <= 0) {
                raining = !raining;
            }
        }
    }

    // WorldServer.tick slice: weather then time advance (doDaylightCycle true).
    static void tick() {
        updateWeather();
        totalTime = totalTime + 1L;
        worldTime = worldTime + 1L;
    }

    static void emit(StringBuilder sb, long v) {
        sb.append(String.format("%016x", v)).append('\n');
    }

    public static void main(String[] args) {
        long seed = args.length > 0 ? Long.parseLong(args[0]) : 12345L;
        int ticks = args.length > 1 ? Integer.parseInt(args[1]) : NTICKS;
        if (ticks < 1) ticks = NTICKS;

        totalTime = 0L;
        worldTime = 0L;
        rainTime = INIT_RAIN_TIME;
        thunderTime = INIT_THUNDER_TIME;
        raining = INIT_RAINING;
        thundering = INIT_THUNDERING;
        rand = new Random(seed);

        StringBuilder sb = new StringBuilder();
        for (int t = 0; t < ticks; ++t) {
            tick();
            emit(sb, totalTime);
            emit(sb, worldTime);
            emit(sb, ((long) rainTime) & 0xffffffffL);
            emit(sb, ((long) thunderTime) & 0xffffffffL);
            emit(sb, raining ? 1L : 0L);
            emit(sb, thundering ? 1L : 0L);
        }
        System.out.print(sb);
    }
}
