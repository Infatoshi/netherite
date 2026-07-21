/* game/timer.h - TIMER module: faithful port of net/minecraft/util/Timer.java.
 *
 * The fixed 20 TPS tick accumulator that decouples the sim rate from the frame
 * rate: each frame gm_timer_update() converts elapsed wall time into
 * elapsed_ticks whole sim ticks (capped at 10, exactly Timer.updateTimer) plus
 * render_partial_ticks in [0,1) for prev->cur render interpolation.
 *
 * Field-for-field port, same float/double mix as the Java class so the
 * accumulation arithmetic matches. Java's two clocks (Minecraft.getSystemTime()
 * = LWJGL Sys ms, System.nanoTime()/1e6 = HR ms) both map to CLOCK_MONOTONIC
 * here, so timeSyncAdjustment converges to 1.0; the sync machinery is kept
 * verbatim anyway so the port stays diffable against Timer.java.
 *
 * gm_timer_update_at() takes both clocks explicitly (pure, no wall-clock read)
 * so the accumulator is unit-testable at scripted frame rates; see
 * game/test_tick_timer.c.
 */
#ifndef MAGMA_GAME_TIMER_H
#define MAGMA_GAME_TIMER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float     ticks_per_second;      /* timer ticks per second of real time (20) */
    double    last_hr_time;          /* HR clock at the last update, in SECONDS */
    int       elapsed_ticks;         /* whole ticks turned over since last update, capped at 10 */
    float     render_partial_ticks;  /* time since the last tick, in ticks [0,1) */
    float     timer_speed;           /* game-speed multiplier (1.0 = normal) */
    float     elapsed_partial_ticks; /* fractional tick accumulator [0,1) */
    long long last_sync_sys_clock;   /* system clock at the last sync, ms */
    long long last_sync_hr_clock;    /* HR clock at the last sync, ms */
    long long counter;               /* ms accumulated toward the 1000ms resync */
    double    time_sync_adjustment;  /* HR->sys clock ratio, updated once per second */
} GmTimer;

/* Timer(float tps): seed both sync clocks from the real (monotonic) clock. */
void gm_timer_init(GmTimer *t, float tps);

/* Timer.updateTimer(): read the real clock, fill elapsed_ticks + render_partial_ticks. */
void gm_timer_update(GmTimer *t);

/* updateTimer() with both clocks supplied (ms). Pure over *t; the unit-test entry.
 * gm_timer_update passes the same monotonic ms for both. */
void gm_timer_update_at(GmTimer *t, long long sys_ms, long long hr_ms);

#ifdef __cplusplus
}
#endif
#endif /* MAGMA_GAME_TIMER_H */
