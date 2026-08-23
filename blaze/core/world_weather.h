/* world_weather: WorldInfo rain/thunder timers + worldTime/totalTime advance.
 * Magma live and blaze-CPU/CUDA compile this one source. Magma's
 * gm_world_tick / gm_world_tick_clear stay thin wrappers.
 *
 * Java 1.11.2 (java/oracle-src/net/minecraft):
 *   World.tick                       World.java:2707-2710  updateWeather()
 *   World.updateWeatherBody          World.java:2741-2836
 *     doWeatherCycle                 GameRules.java:32
 *     thunder re-roll                :2763-2783  nextInt(12000)+3600 / +12000+168000
 *     rain re-roll                   :2785-2807  nextInt(12000)+12000 / +12000+168000
 *     strength fade +-0.01D clamp    :2810-2833  (magma live does not fade)
 *   WorldServer.tick                 WorldServer.java:180-223
 *     super.tick() then              :182
 *     totalWorldTime++               :218
 *     worldTime++ if doDaylightCycle :220-223  GameRules.java:22
 *   WorldInfo rain/thunder/DayTime   WorldInfo.java:43,57-64,370,376-379
 *   World.rand                       World.java:108  java.util.Random
 *
 * Magma RNG: not the shared Java World.rand stream and not mc_hash_seed.
 * ww_init seeds an isolated JavaRandom from the world seed (jrand_set).
 * M1 matches that stream. Strengths stay 0 on the live path
 * (magma/game/runtime.h rain_strength comment).
 *
 * SCOPE (this kernel):
 *   - Struct: totalTime, worldTime, rainTime, thunderTime, raining, thundering + JavaRandom
 *   - Per tick: weather timers then time advance (doWeatherCycle + doDaylightCycle true)
 *   - cleanWeatherTime forced 0 (the "clear weather" hold path is out of scope)
 *   - rainingStrength / thunderingStrength fades, sky-light, lightning: out of scope
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
 * (doDaylightCycle true). Sleep-skip is ww_tick_gated_sleep; skylight stays
 * out of this kernel. */
MC_HD static inline void ww_tick(WwState *s) {
    ww_update_weather(s);
    s->totalTime += 1;
    s->worldTime += 1;
}

/* WorldServer.java:195-196: long i = getWorldTime() + 24000L; i - i % 24000L. */
MC_HD static inline i64 ww_next_dawn(i64 world_time) {
    i64 i = world_time + 24000LL;
    return i - i % 24000LL;
}

/* WorldProvider.resetRainAndThunder WorldProvider.java:584-589. */
MC_HD static inline void ww_reset_rain_and_thunder(WwState *s) {
    s->rainTime = 0;
    s->raining = 0;
    s->thunderTime = 0;
    s->thundering = 0;
}

/* Magma gm_world_tick: GameRules doWeatherCycle / doDaylightCycle.
 * freeze_weather skips timer/RNG work (World.updateWeatherBody :2747-2808)
 * but still advances totalTime, and worldTime when daylight is on
 * (WorldServer.java:218-223). Strength fade is not applied either way. */
MC_HD static inline void ww_tick_gated_sleep(WwState *s, int freeze_weather,
                                             int freeze_daylight,
                                             int sleep_skip) {
    /* WorldServer.tick :182 super.tick (updateWeather) then :191-200
     * areAllPlayersAsleep skip + wakeAllPlayers, then :218-223 time++.
     * sleep_skip is 0 on every RL tick so weather_optional is unchanged. */
    if (freeze_weather) {
        if (sleep_skip && !freeze_daylight)
            s->worldTime = ww_next_dawn(s->worldTime);
        s->totalTime += 1;
        if (!freeze_daylight) s->worldTime += 1;
        return;
    }
    {
        i64 wt_prev = s->worldTime;
        ww_update_weather(s);
        if (sleep_skip) {
            if (!freeze_daylight)
                s->worldTime = ww_next_dawn(s->worldTime);
            ww_reset_rain_and_thunder(s);
        }
        s->totalTime += 1;
        s->worldTime += 1;
        if (freeze_daylight) s->worldTime = wt_prev;
    }
}

MC_HD static inline void ww_tick_gated(WwState *s, int freeze_weather,
                                       int freeze_daylight) {
    ww_tick_gated_sleep(s, freeze_weather, freeze_daylight, 0);
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
