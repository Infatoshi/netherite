/* WorldInfo rain/thunder timer unit tests (world_weather.h).
 *
 * Java World.updateWeatherBody timers + WorldServer time advance. Magma
 * live uses this kernel through gm_world_tick; strengths stay 0. */
#include "world_weather.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void expect(int cond, const char *msg) {
    if (cond)
        fprintf(stderr, "OK: %s\n", msg);
    else {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails = 1;
    }
}

int main(void) {
    WwState s, s2;
    int i;

    ww_init(&s, 12345);
    expect(s.totalTime == 0 && s.worldTime == 0, "init times are 0");
    expect(s.raining == 1 && s.rainTime == 50, "init raining rainTime=50");
    expect(s.thundering == 0 && s.thunderTime == 100,
           "init clear thunderTime=100");

    for (i = 0; i < 50; ++i) ww_tick(&s);
    expect(s.worldTime == 50 && s.totalTime == 50, "50 ticks advance both times");
    expect(s.raining == 0 && s.rainTime == 0,
           "tick 50: rainTime hits 0 and raining flips off");
    expect(s.thundering == 0 && s.thunderTime == 50,
           "thunder timer decremented, not yet flipped");

    ww_tick(&s);
    expect(s.raining == 0 && s.rainTime > 0,
           "tick 51: clear-rain re-roll nextInt(168000)+12000");
    expect(s.rainTime >= 12000, "clear-rain re-roll is at least 12000");

    ww_init(&s, 12345);
    for (i = 0; i < 100; ++i) ww_tick(&s);
    expect(s.thundering == 1 && s.thunderTime == 0,
           "tick 100: thunderTime hits 0 and thundering flips on");
    ww_tick(&s);
    expect(s.thundering == 1 && s.thunderTime >= 3600,
           "tick 101: thunder re-roll nextInt(12000)+3600");

    ww_init(&s, 12345);
    ww_init(&s2, 12345);
    for (i = 0; i < 64; ++i) {
        ww_tick(&s);
        ww_tick_gated(&s2, 0, 0);
    }
    expect(s.rainTime == s2.rainTime && s.raining == s2.raining &&
               s.worldTime == s2.worldTime && s.totalTime == s2.totalTime,
           "ww_tick_gated(0,0) matches ww_tick");

    ww_init(&s, 12345);
    for (i = 0; i < 64; ++i) ww_tick_gated(&s, 1, 0);
    expect(s.raining == 1 && s.rainTime == 50 && s.thundering == 0 &&
               s.thunderTime == 100,
           "freeze_weather leaves timers and flags");
    expect(s.totalTime == 64 && s.worldTime == 64,
           "freeze_weather still advances both times");

    ww_init(&s, 12345);
    for (i = 0; i < 64; ++i) ww_tick_gated(&s, 0, 1);
    expect(s.worldTime == 0 && s.totalTime == 64,
           "freeze_daylight freezes worldTime only");
    expect(s.raining == 0 && s.rainTime > 0,
           "freeze_daylight still runs weather timers");

    ww_init(&s, 10);
    ww_init(&s2, 10);
    for (i = 0; i < 64; ++i) {
        ww_tick(&s);
        ww_tick(&s2);
    }
    expect(s.rainTime == s2.rainTime && s.thunderTime == s2.thunderTime,
           "same seed, same re-rolls");

    return fails ? 1 : 0;
}
