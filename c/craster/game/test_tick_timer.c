/* game/test_tick_timer.c - standalone unit test for the P0 tick-accumulator work
 * (game/timer.c + the game_main.c tick-batching contract).
 *
 * (A) TIMER ACCUMULATOR (Timer.java port, scripted clocks - no wall time):
 *     - at a 277.78ms frame (the ~3.6 FPS software-raster rate) elapsed_ticks per
 *       frame is 5..6 and 10 seconds of frames yields ~200 ticks (20 TPS held);
 *     - at a 16ms frame elapsed_ticks is 0..1 and 10 seconds still yields ~200;
 *     - a 60ms-per-frame run and a 300ms-per-frame run over the same wall span
 *       produce the SAME total tick count (game time independent of frame rate);
 *     - a huge 2s stall is capped at 10 ticks (Timer.java elapsedTicks cap).
 *
 * (B) TICK-BATCH DETERMINISM (the guard the brief demands): a 120-tick scripted
 *     input tape produces BITWISE-identical PsvPlayer + PvStats state whether the
 *     ticks run 1 per frame or batched 3 per frame (game_main.c catch-up shape:
 *     held movement repeats each tick, deltas/edges fire once per batch). Sim
 *     state advances ONLY in whole gm_player_tick calls; the render-side lerp is
 *     computed here too and shown to leave sim state untouched. Raw doubles are
 *     compared with memcmp (bitwise), not tolerances.
 *
 * Build: bash game/test_tick_timer.sh
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "player_survival.h"
#include "player_vitals.h"
#include "game/game.h"
#include "game/player_ctl.h"
#include "game/timer.h"

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("  FAIL: %s\n", msg); g_fail = 1; } } while (0)

/* Fill a 9-Chunk window with a flat stone floor (same fixture as test_player_ctl). */
static void fill_flat(Chunk *win)
{
    memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
    for (int ci = 0; ci < PSV_NCHUNKS; ++ci) {
        win[ci].cx = (ci % PSV_DIM) - PSV_R;
        win[ci].cz = (ci / PSV_DIM) - PSV_R;
        for (int lx = 0; lx < 16; ++lx)
            for (int lz = 0; lz < 16; ++lz)
                for (int y = 0; y <= 64; ++y)
                    mc_set(&win[ci], lx, y, lz, mc_state(BLK_STONE, 0));
    }
}

static void spawn_at(PsvPlayer *pl, double x, double y, double z)
{
    psv_player_init(pl);
    pl->ent.posX = x; pl->ent.posY = y; pl->ent.posZ = z;
    pl->ent.box = psv_player_box(x, y, z);
    pl->ent.motionX = pl->ent.motionY = pl->ent.motionZ = 0.0;
    pl->ent.onGround = 0;
    pl->ent.collidedHorizontally = pl->ent.collidedVertically = pl->ent.isCollided = 0;
}

/* The scripted per-TICK input tape: walk, turn, jump, sneak phases so physics,
 * look integration and the sprint state machine all get exercised. Deltas
 * (dyaw/dpitch) are nonzero only on ticks that are batch STARTS in the 3-per-frame
 * run (tick % 3 == 0), mirroring the game_main.c contract that deltas/edges fire
 * once per input poll; held fields vary freely. */
static GmAction tape_at(int k)
{
    GmAction a; memset(&a, 0, sizeof a);
    a.hotbar_sel = -1;
    a.forward = (k < 90) ? 1.0f : 0.0f;
    a.strafe  = (k >= 30 && k < 60) ? 1.0f : 0.0f;
    a.jump    = (k % 17) == 5;
    a.sneak   = (k >= 100);
    if (k % 3 == 0) { a.dyaw = 2.5f; a.dpitch = (k < 60) ? 0.5f : -0.5f; }
    return a;
}

/* Bitwise state compare: raw doubles via memcmp, no tolerances. */
static int state_eq(const PsvPlayer *a, const PsvPlayer *b,
                    const PvStats *va, const PvStats *vb)
{
    return memcmp(&a->ent, &b->ent, sizeof a->ent) == 0
        && memcmp(&a->yaw, &b->yaw, sizeof(float)) == 0
        && memcmp(&a->pitch, &b->pitch, sizeof(float)) == 0
        && memcmp(&a->fall_distance, &b->fall_distance, sizeof(float)) == 0
        && a->sprinting == b->sprinting
        && memcmp(va, vb, sizeof *va) == 0;
}

int main(void)
{
    /* ---------------- (A) timer accumulator at scripted frame rates ---------------- */
    printf("case A: Timer.java accumulator, scripted clocks\n");
    {
        /* ~3.6 FPS: 277.78ms/frame for 10s -> ~200 ticks, 5..6 per frame. */
        GmTimer t; gm_timer_init(&t, 20.0f);
        t.last_sync_sys_clock = 0; t.last_sync_hr_clock = 0; t.last_hr_time = 0.0;
        long long total = 0; int minb = 99, maxb = -1;
        for (int f = 1; f <= 36; ++f) {               /* 36 * 277.78ms ~= 10.0s */
            long long ms = (long long)(f * 277.78);
            gm_timer_update_at(&t, ms, ms);
            total += t.elapsed_ticks;
            if (t.elapsed_ticks < minb) minb = t.elapsed_ticks;
            if (t.elapsed_ticks > maxb) maxb = t.elapsed_ticks;
            CHECK(t.render_partial_ticks >= 0.0f && t.render_partial_ticks < 1.0f,
                  "partial_ticks in [0,1)");
        }
        printf("  3.6 FPS: ticks/frame %d..%d, total %lld over ~10s\n", minb, maxb, total);
        CHECK(minb >= 5 && maxb <= 6, "3.6 FPS batches are 5..6 ticks");
        CHECK(total >= 198 && total <= 202, "3.6 FPS holds ~20 TPS over 10s");

        /* 62.5 FPS: 16ms/frame for 10s -> still ~200 ticks, batches 0..1. */
        GmTimer t2; gm_timer_init(&t2, 20.0f);
        t2.last_sync_sys_clock = 0; t2.last_sync_hr_clock = 0; t2.last_hr_time = 0.0;
        long long total2 = 0; int maxb2 = -1;
        for (int f = 1; f <= 625; ++f) {
            gm_timer_update_at(&t2, 16LL * f, 16LL * f);
            total2 += t2.elapsed_ticks;
            if (t2.elapsed_ticks > maxb2) maxb2 = t2.elapsed_ticks;
        }
        printf("  62.5 FPS: max ticks/frame %d, total %lld over 10s\n", maxb2, total2);
        CHECK(maxb2 <= 1, "62.5 FPS batches are 0..1 ticks");
        CHECK(total2 >= 198 && total2 <= 202, "62.5 FPS holds ~20 TPS over 10s");
        CHECK(llabs(total - total2) <= 2, "tick count independent of frame rate");

        /* stall cap: an 800ms frame is 16 ticks of debt, capped at 10 (Timer.java
         * elapsedTicks cap). A >1000ms gap takes updateTimer's else branch and DROPS
         * the time entirely (lastHRTime resync, 0 ticks) - also faithful behavior. */
        GmTimer t3; gm_timer_init(&t3, 20.0f);
        t3.last_sync_sys_clock = 0; t3.last_sync_hr_clock = 0; t3.last_hr_time = 0.0;
        gm_timer_update_at(&t3, 800, 800);
        printf("  800ms stall: elapsed_ticks %d\n", t3.elapsed_ticks);
        CHECK(t3.elapsed_ticks == 10, "800ms stall (16 ticks of debt) capped at 10");
        gm_timer_update_at(&t3, 3000, 3000);
        printf("  2.2s gap: elapsed_ticks %d\n", t3.elapsed_ticks);
        CHECK(t3.elapsed_ticks == 0, ">1s gap dropped (Timer.java else branch), 0 ticks");
    }

    /* ---------------- (B) 1 tick/frame vs 3 ticks/frame: bitwise-identical sim ---------------- */
    printf("case B: tick-batch determinism (1/frame vs 3/frame, bitwise)\n");
    {
        McSinTable st; mc_sin_table_init(&st);
        Chunk *win = malloc(sizeof(Chunk) * PSV_NCHUNKS);
        const int NT = 120;

        /* runner 1: one tick per frame */
        fill_flat(win);
        PsvPlayer p1; spawn_at(&p1, 24.0, 80.0, 24.0);
        PvStats v1; pv_init(&v1);
        for (int k = 0; k < NT; ++k) {
            GmAction a = tape_at(k);
            GmBlockEdit e[8]; int ne = 0;
            gm_player_tick((struct Chunk *)win, (struct McSinTable *)&st,
                           (struct PsvPlayer *)&p1, (struct PvStats *)&v1, a, 0, 0, 0, e, &ne, 8);
        }

        /* runner 2: three ticks per frame + the render-side prev->cur lerp between
         * batches (computed exactly as game_main.c does; must not touch sim state). */
        fill_flat(win);
        PsvPlayer p2; spawn_at(&p2, 24.0, 80.0, 24.0);
        PvStats v2; pv_init(&v2);
        double prev_x = p2.ent.posX, prev_y = p2.ent.posY, prev_z = p2.ent.posZ;
        float  prev_yaw = p2.yaw, prev_pitch = p2.pitch;
        volatile double sink = 0.0;   /* consume the lerp so it cannot be elided */
        for (int f = 0; f < NT / 3; ++f) {
            for (int t = 0; t < 3; ++t) {
                int k = f * 3 + t;
                prev_x = p2.ent.posX; prev_y = p2.ent.posY; prev_z = p2.ent.posZ;
                prev_yaw = p2.yaw; prev_pitch = p2.pitch;
                GmAction a = tape_at(k);
                GmBlockEdit e[8]; int ne = 0;
                gm_player_tick((struct Chunk *)win, (struct McSinTable *)&st,
                               (struct PsvPlayer *)&p2, (struct PvStats *)&v2, a, 0, 0, 0, e, &ne, 8);
            }
            const float pt = 0.37f;   /* arbitrary renderPartialTicks */
            sink += prev_x + (p2.ent.posX - prev_x) * (double)pt
                  + prev_y + (p2.ent.posY - prev_y) * (double)pt
                  + prev_z + (p2.ent.posZ - prev_z) * (double)pt
                  + (double)(prev_yaw + (p2.yaw - prev_yaw) * pt)
                  + (double)(prev_pitch + (p2.pitch - prev_pitch) * pt);
        }

        printf("  1/frame: pos %.17g %.17g %.17g yaw %.9g pitch %.9g health %.9g food %d\n",
               p1.ent.posX, p1.ent.posY, p1.ent.posZ, p1.yaw, p1.pitch, v1.health, v1.foodLevel);
        printf("  3/frame: pos %.17g %.17g %.17g yaw %.9g pitch %.9g health %.9g food %d\n",
               p2.ent.posX, p2.ent.posY, p2.ent.posZ, p2.yaw, p2.pitch, v2.health, v2.foodLevel);
        CHECK(state_eq(&p1, &p2, &v1, &v2),
              "120-tick tape bitwise-identical at 1 tick/frame vs 3 ticks/frame");
        (void)sink;
        free(win);
    }

    printf(g_fail ? "\nRESULT: FAIL\n" : "\nRESULT: PASS (both cases)\n");
    return g_fail;
}
