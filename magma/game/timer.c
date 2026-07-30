/* game/timer.c - TIMER module. Line-for-line port of Timer.java updateTimer():
 * same field types (float accumulator, double clock seconds), same clamp order,
 * same 10-tick cap. See game/timer.h for the module contract. */
#include "game/timer.h"

#include <time.h>

/* One monotonic ms clock backs both Java clocks (getSystemTime / nanoTime/1e6). */
static long long gm_timer_ms_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + (long long)(ts.tv_nsec / 1000000L);
}

void gm_timer_init(GmTimer *t, float tps)
{
    long long ms = gm_timer_ms_now();
    t->ticks_per_second      = tps;
    t->last_hr_time          = 0.0;
    t->elapsed_ticks         = 0;
    t->render_partial_ticks  = 0.0f;
    t->timer_speed           = 1.0f;
    t->elapsed_partial_ticks = 0.0f;
    t->last_sync_sys_clock   = ms;   /* Minecraft.getSystemTime() */
    t->last_sync_hr_clock    = ms;   /* System.nanoTime() / 1000000L */
    t->counter               = 0;
    t->time_sync_adjustment  = 1.0;
}

void gm_timer_update_at(GmTimer *t, long long sys_ms, long long hr_ms)
{
    long long i = sys_ms;                    /* Minecraft.getSystemTime() */
    long long j = i - t->last_sync_sys_clock;
    long long k = hr_ms;                     /* System.nanoTime() / 1000000L */
    double    d0 = (double)k / 1000.0;

    if (j <= 1000 && j >= 0) {
        t->counter += j;

        if (t->counter > 1000) {
            long long l  = k - t->last_sync_hr_clock;
            double    d1 = (double)t->counter / (double)l;
            /* Java literal 0.20000000298023224D == (double)0.2f */
            t->time_sync_adjustment += (d1 - t->time_sync_adjustment) * 0.20000000298023224;
            t->last_sync_hr_clock = k;
            t->counter = 0;
        }

        if (t->counter < 0)
            t->last_sync_hr_clock = k;
    } else {
        t->last_hr_time = d0;
    }

    t->last_sync_sys_clock = i;
    double d2 = (d0 - t->last_hr_time) * t->time_sync_adjustment;
    t->last_hr_time = d0;
    if (d2 < 0.0) d2 = 0.0;                 /* MathHelper.clamp(d2, 0.0D, 1.0D) */
    if (d2 > 1.0) d2 = 1.0;
    t->elapsed_partial_ticks = (float)((double)t->elapsed_partial_ticks
                                       + d2 * (double)t->timer_speed * (double)t->ticks_per_second);
    t->elapsed_ticks = (int)t->elapsed_partial_ticks;
    t->elapsed_partial_ticks -= (float)t->elapsed_ticks;

    if (t->elapsed_ticks > 10)
        t->elapsed_ticks = 10;

    t->render_partial_ticks = t->elapsed_partial_ticks;
}

void gm_timer_update(GmTimer *t)
{
    long long ms = gm_timer_ms_now();
    gm_timer_update_at(t, ms, ms);
}
