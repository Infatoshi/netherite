/* world_weather: WorldInfo rain/thunder timers + worldTime/totalTime advance.
 * Exact port of the doWeatherCycle timer body in World.updateWeatherBody (overworld,
 * hasSkyLight, cleanWeatherTime=0) plus the totalTime/worldTime increments from
 * WorldServer.tick. One MC_HD source, CPU==CUDA, verbatim-Java golden.
 *
 * Sources (java/oracle-src/net/minecraft):
 *   world/World.java                 updateWeatherBody rain/thunder timers (2741-2807)
 *   world/WorldServer.java           tick: totalWorldTime++ / worldTime++ (218-223)
 *   world/storage/WorldInfo.java     rainTime, thunderTime, raining, thundering, DayTime
 *   java.util.Random                 nextInt bounds used for re-rolls
 *
 * SCOPE (this kernel):
 *   - Struct: totalTime, worldTime, rainTime, thunderTime, raining, thundering + JavaRandom
 *   - Per tick: weather timers then time advance (doWeatherCycle + doDaylightCycle true)
 *   - cleanWeatherTime forced 0 (the "clear weather" hold path is out of scope)
 *   - rainingStrength / thunderingStrength fades, sky-light, lightning draws: out of scope
 *   - Re-roll ranges (when timer already <= 0 at top of tick):
 *       thundering:  nextInt(12000) + 3600
 *       clear thunder: nextInt(168000) + 12000
 *       raining:     nextInt(12000) + 12000
 *       clear rain:  nextInt(168000) + 12000
 *   - When timer > 0: decrement; on hit 0 flip the boolean (re-roll is next tick)
 *
 * Fixed initial WorldInfo (chosen so both flip + re-roll fire inside WW_NTICKS):
 *   totalTime=0, worldTime=0,
 *   raining=1 rainTime=50, thundering=0 thunderTime=100.
 * Tape emits 6 u64s per tick as %016llx. */
#ifndef MC_WORLD_WEATHER_H
#define MC_WORLD_WEATHER_H

#include "mc.h"
#include "mc_rng.h"

#ifndef WW_NTICKS
#define WW_NTICKS 256
#endif
#define WW_FIELDS 6

/* Fixed harness initial WorldInfo values (not taken from seed). */
#define WW_INIT_RAIN_TIME    50
#define WW_INIT_THUNDER_TIME 100
#define WW_INIT_RAINING      1
#define WW_INIT_THUNDERING   0

typedef struct {
    i64 totalTime;     /* WorldInfo.totalTime / getWorldTotalTime */
    i64 worldTime;     /* WorldInfo.worldTime / DayTime */
    i32 rainTime;      /* WorldInfo.rainTime */
    i32 thunderTime;   /* WorldInfo.thunderTime */
    i32 raining;       /* WorldInfo.raining  (0/1) */
    i32 thundering;    /* WorldInfo.thundering (0/1) */
    JavaRandom rand;   /* World.rand (java.util.Random) */
} WwState;

/* Seed only the world RNG; WorldInfo timers start from the fixed harness constants. */
MC_HD static inline void ww_init(WwState *s, i64 seed) {
    s->totalTime = 0;
    s->worldTime = 0;
    s->rainTime = WW_INIT_RAIN_TIME;
    s->thunderTime = WW_INIT_THUNDER_TIME;
    s->raining = WW_INIT_RAINING;
    s->thundering = WW_INIT_THUNDERING;
    jrand_set(&s->rand, seed);
}

/* World.updateWeatherBody rain/thunder timer body with doWeatherCycle=true,
 * cleanWeatherTime=0, hasSkyLight, !isRemote. Strength fades omitted. */
MC_HD static inline void ww_update_weather(WwState *s) {
    i32 j = s->thunderTime;
    if (j <= 0) {
        if (s->thundering)
            s->thunderTime = jrand_int_bound(&s->rand, 12000) + 3600;
        else
            s->thunderTime = jrand_int_bound(&s->rand, 168000) + 12000;
    } else {
        --j;
        s->thunderTime = j;
        if (j <= 0)
            s->thundering = s->thundering ? 0 : 1;
    }

    {
        i32 k = s->rainTime;
        if (k <= 0) {
            if (s->raining)
                s->rainTime = jrand_int_bound(&s->rand, 12000) + 12000;
            else
                s->rainTime = jrand_int_bound(&s->rand, 168000) + 12000;
        } else {
            --k;
            s->rainTime = k;
            if (k <= 0)
                s->raining = s->raining ? 0 : 1;
        }
    }
}

/* One WorldServer-style tick slice: weather then totalTime/worldTime advance
 * (doDaylightCycle true). Sleep-skip and skylight are out of scope. */
MC_HD static inline void ww_tick(WwState *s) {
    ww_update_weather(s);
    s->totalTime += 1;
    s->worldTime += 1;
}

MC_HD static inline void ww_dump(const WwState *s, u64 *out) {
    out[0] = (u64)s->totalTime;
    out[1] = (u64)s->worldTime;
    out[2] = (u64)(u32)s->rainTime;
    out[3] = (u64)(u32)s->thunderTime;
    out[4] = (u64)(u32)s->raining;
    out[5] = (u64)(u32)s->thundering;
}

/* Full tape: init from seed, tick nticks times, dump WW_FIELDS u64s after each tick. */
MC_HD static inline void ww_run(WwState *s, i64 seed, i32 nticks, u64 *out) {
    i32 t;
    ww_init(s, seed);
    for (t = 0; t < nticks; ++t) {
        ww_tick(s);
        ww_dump(s, out + (i64)t * WW_FIELDS);
    }
}

#endif /* MC_WORLD_WEATHER_H */
