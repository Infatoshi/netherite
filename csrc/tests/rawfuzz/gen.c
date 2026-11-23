/* A seeded stand-in person for the Java client: a RawDrive step list
 * (oracle/harness/netherite/oracle/RawDrive.java) of human-like play at the LWJGL
 * event level, for csrc/tests/rawfuzz/run.sh. The same SEED and PROFILE
 * always give the same list; what the session does with it depends on the
 * world it meets (the closed-loop steps look at the client's world and screen).
 *
 *   out/native/rawfuzz_gen SEED [--profile fresh|cp] [--ticks N] [--salt START] > drive.jsonl
 *
 * A session is a run of episodes drawn by weight until about N ticks (default
 * 900 to 2400) are planned: walking with any mix of W A S D held and swapped
 * (sometimes in one frame), sprinting (control, or W tapped twice), sneaking,
 * jumping (taps and holds), the mouse turning as a hand does (flicks with an
 * overshoot, slow drifts, pitch past the limits) the whole time; looking
 * around; the hotbar keys and the wheel (several in one frame); dropping (Q,
 * Q with either control, Q held); mining (a nearby block found and held on,
 * or held at whatever the crosshair meets); using and placing (right taps
 * and holds, eating); fighting whatever animal or monster is near; the middle
 * button; the F-keys the native client maps (F1, F3 with and without shift,
 * F5, F3 with H, B, F, A, P); and the screens: the inventory and any
 * workbench, furnace or chest nearby, with left, right and middle clicks,
 * shift clicks (either shift, pressed before the click or in its frame), left
 * and right drags gliding over slots, double clicks, number keys and Q or
 * control-Q over a hovered slot, clicks outside, pointer wandering, the wheel,
 * and closing with E or Escape (with a stack on the cursor, mid-drag, with a
 * movement key or a button held across). The fresh profile (a world's first
 * join, empty-handed) first chops a few logs and crafts planks, a table and
 * sticks so the screens have something to move; the cp profile starts from a
 * checkpoint's inventory. Every draw is Python's random.Random (seeded with
 * SEED, or the string "SEED:START" with a salt): the C port of gen.py
 * (lane/cport), the same lists byte for byte. */
#define _GNU_SOURCE
#include "../tool.h"

/* Container index ranges every screen has: the inventory's 45, the
 * furnace's 39, the workbench's 46, a chest's 63 or 90 */
enum { ANY_SLOTS = 39 };
static const int BLOCKS_MINE[] = {1, 2, 3, 4, 12, 13, 17, 18, 24, 31, 37, 38, 78, 80, 81, 83, 87, 88, 121};
static const char *MOBS = "EntityCow|EntityPig|EntitySheep|EntityChicken|EntityZombie|EntitySkeleton|EntitySpider|"
                          "EntityCreeper|EntitySlime|EntityPigZombie|EntityEnderman|EntityVillager|EntitySquid|EntityBat|"
                          "EntityMagmaCube";
static const int FOOD[] = {260, 297, 320, 322, 349, 350, 357, 360, 363, 364, 365, 366, 367, 391, 392, 393, 396, 400};
static const int PLACEABLE[] = {1, 2, 3, 4, 5, 12, 13, 17, 20, 24, 35, 45, 48, 50, 54, 58, 61, 65, 85, 87, 98};
#define N(a) ((int)(sizeof(a) / sizeof *(a)))

static struct pyrand R;
static int fresh;
static long long budget, t;
static struct sv held;

static long long ri(long long a, long long b) { return pr_randint(&R, a, b); }
static int chance(double p) { return pr_random(&R) < p; }
static long long choice_pm1(void) { return pr_below(&R, 2) ? 1 : -1; }
#define PICK(arr) ((arr)[pr_below(&R, N(arr))])

/* ------------------------------------------------------------ output */

static void step(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void step(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    putchar('{');
    vprintf(fmt, ap);
    puts("}");
    va_end(ap);
}

static void wait_(long long n)
{
    if (n > 0) {
        step("\"wait\":%lld", n);
        t += n;
    }
}

static int held_has(const char *k)
{
    for (int i = 0; i < held.n; ++i)
        if (!strcmp(held.v[i], k)) return 1;
    return 0;
}

static void key(const char *k, int down)
{
    step("\"key\":\"%s\",\"down\":%d", k, down ? 1 : 0);
    if (down && !held_has(k)) sv_push(&held, xstrdup(k));
    else if (!down)
        for (int i = 0; i < held.n; ++i)
            if (!strcmp(held.v[i], k)) {
                free(held.v[i]);
                held.v[i] = held.v[--held.n];
                break;
            }
}

static void tap(const char *k) { step("\"tap\":\"%s\"", k); }

static void tap_n(long long n)
{
    char k[24];
    snprintf(k, sizeof k, "%lld", n);
    tap(k);
}

/* sorted(held & set) or, with minus, sorted(held - set) */
static struct sv held_sorted(const char *const *set, int n, int minus)
{
    struct sv out = {0};
    for (int i = 0; i < held.n; ++i) {
        int in = 0;
        for (int k = 0; k < n; ++k) in |= !strcmp(held.v[i], set[k]);
        if (in != minus) sv_push(&out, held.v[i]);
    }
    sv_sort(&out);
    return out;
}

static void turn(double dx, double dy, double ticks)
{
    long long tk = (long long)ticks;
    step("\"turn\":[%lld,%lld],\"ticks\":%lld", (long long)dx, (long long)dy, tk > 1 ? tk : 1);
}

/* ------------------------------------------------------------ the mouse */

/* one hand motion: a flick with its overshoot, a drift, or a pitch sweep */
static void look_move(void)
{
    double kind = pr_random(&R);
    if (kind < 0.4) {
        long long s = choice_pm1();
        long long dx = s * ri(150, 1200);
        long long dy = ri(-250, 250);
        long long n = ri(2, 8);
        turn((double)dx, (double)dy, (double)n);
        if (chance(0.6)) {
            wait_(n);
            double a = -dx * pr_uniform(&R, 0.03, 0.12);
            double b = -dy * pr_uniform(&R, 0.0, 0.2);
            turn(a, b, (double)ri(1, 4));
        }
    } else if (kind < 0.8) {
        long long a = ri(-150, 150);
        long long b = ri(-60, 60);
        turn((double)a, (double)b, (double)ri(10, 60));
    } else {
        /* pitch past the limits and back (the clamp at 90 degrees is 600 counts) */
        long long s = choice_pm1();
        long long dy = s * ri(500, 1100);
        long long a = ri(-40, 40);
        turn((double)a, (double)dy, (double)ri(4, 20));
        wait_(ri(5, 20));
        a = ri(-40, 40);
        double b = -dy * pr_uniform(&R, 0.3, 0.9);
        turn((double)a, b, (double)ri(4, 20));
    }
}

/* ------------------------------------------------------------ episodes */

/* an event of a timeline: (tick, order, what) */
enum { E_KEY, E_KEYUP_CTRL, E_TAP, E_LOOK };
struct ev {
    long long t;
    int order, kind, down;
    const char *k;
};

static struct ev evs[64];
static int nev;

static void ev_add(long long tk, int order, int kind, const char *k, int down)
{
    if (nev == N(evs)) die("rawfuzz_gen: timeline too long");
    evs[nev++] = (struct ev){tk, order, kind, down, k};
}

static void ep_walk(void);

static void timeline(long long dur)
{
    /* a stable sort by (tick, order) */
    for (int i = 1; i < nev; ++i) {
        struct ev e = evs[i];
        int j = i - 1;
        while (j >= 0 && (evs[j].t > e.t || (evs[j].t == e.t && evs[j].order > e.order))) {
            evs[j + 1] = evs[j];
            --j;
        }
        evs[j + 1] = e;
    }
    struct ev list[64];
    int n = nev;
    memcpy(list, evs, sizeof list);
    nev = 0;
    long long now = 0;
    const char *ctrl = NULL;
    for (int i = 0; i < n; ++i) {
        struct ev *e = &list[i];
        wait_(e->t - now);
        now = now > e->t ? now : e->t;
        if (e->kind == E_KEY) {
            if (!strcmp(e->k, "LCONTROL") || !strcmp(e->k, "RCONTROL")) ctrl = e->k;
            key(e->k, e->down);
        } else if (e->kind == E_KEYUP_CTRL) {
            if (ctrl) key(ctrl, 0);
        } else if (e->kind == E_TAP) tap(e->k);
        else look_move();
    }
    wait_(dur - now);
    static const char *const MOVE[8] = {"W", "A", "S", "D", "LSHIFT", "SPACE", "LCONTROL", "RCONTROL"};
    struct sv ks = held_sorted(MOVE, 8, 0);
    for (int i = 0; i < ks.n; ++i)
        if (chance(0.85)) key(ks.v[i], 0);
    free(ks.v);
}

/* movement keys held and swapped over a while, the mouse steering */
static void ep_walk(void)
{
    long long dur = ri(20, 120);
    nev = 0;
    static const char *const WASD[4] = {"W", "A", "S", "D"};
    static const double P[4] = {0.8, 0.3, 0.3, 0.3};
    for (int i = 0; i < 4; ++i)
        if (chance(P[i])) {
            long long a = ri(0, dur / 2);
            long long b = a + ri(3, dur);
            b = b < dur ? b : dur;
            ev_add(a, 0, E_KEY, WASD[i], 1);
            ev_add(b, 1, E_KEY, WASD[i], 0);
        }
    if (chance(0.35)) {
        /* sprint: control with W, or W tapped twice (the sprint toggle timer, 7 ticks) */
        if (chance(0.5)) {
            long long a = ri(0, dur / 2);
            static const char *const C[3] = {"LCONTROL", "LCONTROL", "RCONTROL"};
            ev_add(a, 0, E_KEY, PICK(C), 1);
            ev_add(ri(a + 1, dur), 1, E_KEYUP_CTRL, NULL, 0);
            ev_add(a, 0, E_KEY, "W", 1);
            long long b = a + ri(10, 60);
            ev_add(b < dur ? b : dur, 1, E_KEY, "W", 0);
        } else {
            long long a = ri(0, dur / 2);
            ev_add(a, 0, E_KEY, "W", 1);
            ev_add(a + ri(1, 3), 1, E_KEY, "W", 0);
            long long b = a + ri(3, 9);
            ev_add(b, 0, E_KEY, "W", 1);
            long long c = b + ri(10, 60);
            ev_add(c < dur ? c : dur, 1, E_KEY, "W", 0);
        }
    }
    if (chance(0.3)) {
        long long a = ri(0, dur - 1);
        ev_add(a, 0, E_KEY, "LSHIFT", 1);
        ev_add(ri(a + 1, dur), 1, E_KEY, "LSHIFT", 0);
    }
    long long n = ri(0, 4);
    for (long long i = 0; i < n; ++i) {
        long long a = ri(0, dur);
        if (chance(0.7)) ev_add(a, 2, E_TAP, "SPACE", 0);
        else {
            ev_add(a, 0, E_KEY, "SPACE", 1);
            long long b = a + ri(2, 30);
            ev_add(b < dur ? b : dur, 1, E_KEY, "SPACE", 0);
        }
    }
    if (chance(0.2)) {
        /* a key swapped in one frame: A released and D pressed together */
        long long a = ri(0, dur);
        ev_add(a, 0, E_KEY, "A", 1);
        long long b = a + ri(2, 15);
        b = b < dur ? b : dur;
        ev_add(b, 1, E_KEY, "A", 0);
        ev_add(b, 1, E_KEY, "D", 1);
        long long c = b + ri(2, 15);
        ev_add(c < dur ? c : dur, 1, E_KEY, "D", 0);
    }
    if (chance(0.15)) {
        /* pressed and released in one frame (a key shorter than a tick) */
        long long a = ri(0, dur);
        const char *k = PICK(WASD);
        ev_add(a, 3, E_KEY, k, 1);
        ev_add(a, 4, E_KEY, k, 0);
    }
    n = ri(0, 5);
    for (long long i = 0; i < n; ++i) ev_add(ri(0, dur), 5, E_LOOK, NULL, 0);
    timeline(dur);
}

static void ep_look(void)
{
    long long n = ri(1, 5);
    for (long long i = 0; i < n; ++i) {
        look_move();
        wait_(ri(1, 20));
    }
}

static void ep_hotbar(void)
{
    long long n = ri(2, 8);
    for (long long i = 0; i < n; ++i) {
        double c = pr_random(&R);
        if (c < 0.4) tap_n(ri(1, 9));
        else if (c < 0.55) {
            /* two number keys in one frame: the last one wins */
            tap_n(ri(1, 9));
            tap_n(ri(1, 9));
        } else if (c < 0.85) {
            long long m = ri(1, 3);
            for (long long j = 0; j < m; ++j) step("\"wheel\":%lld", choice_pm1());
        } else {
            /* a number key and the wheel in one frame */
            tap_n(ri(1, 9));
            step("\"wheel\":%lld", choice_pm1());
        }
        wait_(ri(0, 8));
    }
}

static void ep_drop(void)
{
    tap_n(ri(1, 9));
    wait_(ri(1, 4));
    long long n = ri(1, 3);
    for (long long i = 0; i < n; ++i) {
        double c = pr_random(&R);
        if (c < 0.45) tap("Q");
        else if (c < 0.8) {
            static const char *const C[2] = {"LCONTROL", "RCONTROL"};
            const char *ctrl = PICK(C);
            key(ctrl, 1);
            if (chance(0.5)) wait_(ri(1, 3));
            tap("Q");
            wait_(ri(0, 4));
            key(ctrl, 0);
        } else {
            /* Q held: no repeats reach the game */
            key("Q", 1);
            wait_(ri(10, 30));
            key("Q", 0);
        }
        wait_(ri(2, 10));
    }
    if (chance(0.4)) {
        step("\"collect\":6,\"within\":0.3,\"ticks\":60");
        t += 30;
    }
}

static void ep_mine(void)
{
    if (chance(0.65)) {
        int k = (int)ri(3, 8), idx[8];
        pr_sample_idx(&R, N(BLOCKS_MINE), k, idx);
        struct sb b = {0};
        for (int i = 0; i < k; ++i) sb_printf(&b, "%s%d", i ? "," : "", BLOCKS_MINE[idx[i]]);
        step("\"find\":[%s],\"r\":5,\"dy\":[-2,3],\"as\":\"blk\"", b.s);
        sb_free(&b);
        step("\"mine\":\"blk\",\"ticks\":%lld", ri(40, 160));
        t += 60;
        if (chance(0.5)) {
            step("\"collect\":5,\"within\":0.3,\"ticks\":60");
            t += 30;
        }
    } else {
        /* held at whatever the crosshair meets, the look drifting */
        long long a = ri(-100, 100);
        long long b = -ri(100, 350);
        turn((double)a, (double)b, (double)ri(3, 10));
        wait_(ri(3, 10));
        step("\"button\":0,\"down\":1");
        long long n = ri(10, 80);
        long long m = ri(0, 3);
        for (long long i = 0; i < m; ++i) {
            a = ri(-80, 80);
            b = ri(-40, 40);
            turn((double)a, (double)b, (double)ri(5, 20));
        }
        if (chance(0.3)) {
            static const char *const AD[2] = {"A", "D"};
            key(PICK(AD), 1);
        }
        wait_(n);
        step("\"button\":0,\"down\":0");
        if (held_has("A")) key("A", 0);
        if (held_has("D")) key("D", 0);
    }
}

static void ep_use(void)
{
    if (chance(0.4)) step("\"select\":%d", PICK(PLACEABLE));
    else tap_n(ri(1, 9));
    wait_(ri(1, 5));
    if (chance(0.4)) {
        long long a = ri(-60, 60);
        long long b = -ri(150, 400);
        turn((double)a, (double)b, (double)ri(3, 8));
        wait_(ri(4, 10));
    }
    long long n = ri(1, 4);
    for (long long i = 0; i < n; ++i) {
        if (chance(0.6)) {
            step("\"click\":1");
            wait_(ri(1, 8));
        } else {
            step("\"hold\":1,\"ticks\":%lld", ri(5, 45));
            t += 25;
        }
        if (chance(0.3)) look_move();
    }
}

static void ep_eat(void)
{
    step("\"select\":%d", PICK(FOOD));
    wait_(ri(1, 4));
    step("\"hold\":1,\"ticks\":%lld", ri(20, 50));
    t += 40;
}

static void ep_attack(void)
{
    if (chance(0.6)) {
        long long r = ri(6, 14);
        long long tk = ri(60, 200);
        step("\"attack\":\"%s\",\"r\":%lld,\"ticks\":%lld,\"kills\":1", MOBS, r, tk);
        t += 100;
    } else {
        /* clicks at the air or the ground, a click spam */
        long long n = ri(3, 12);
        for (long long i = 0; i < n; ++i) {
            step("\"click\":0");
            wait_(ri(1, 6));
            if (chance(0.3)) look_move();
        }
    }
}

static void ep_middle(void)
{
    long long a = ri(-200, 200);
    long long b = -ri(0, 300);
    turn((double)a, (double)b, (double)ri(3, 8));
    wait_(ri(4, 10));
    long long n = ri(1, 3);
    for (long long i = 0; i < n; ++i) {
        step("\"click\":2");
        wait_(ri(1, 6));
    }
}

static void ep_fkeys(void)
{
    long long n = ri(1, 3);
    for (long long i = 0; i < n; ++i) {
        double c = pr_random(&R);
        if (c < 0.2) tap("F1");
        else if (c < 0.4) tap("F5");
        else if (c < 0.55) tap("F3");
        else if (c < 0.65) {
            /* the profiler chart: F3 with shift */
            key("LSHIFT", 1);
            tap("F3");
            wait_(ri(1, 3));
            key("LSHIFT", 0);
        } else {
            key("F3", 1);
            if (chance(0.4)) wait_(ri(1, 4));
            static const char *const K[5] = {"H", "B", "F", "A", "P"};
            tap(PICK(K));
            wait_(ri(0, 3));
            key("F3", 0);
        }
        wait_(ri(3, 30));
        if (chance(0.4)) ep_walk();
    }
}

/* a tower of the held block, then off its edge (the fall hurts, sometimes kills) */
static void ep_pillar(void)
{
    step("\"select\":%d", PICK(PLACEABLE));
    wait_(ri(1, 4));
    long long n = ri(2, 9);
    step("\"pillar\":%lld", n);
    t += 25 * n;
    long long b = ri(400, 700);
    turn(0, (double)b, (double)ri(4, 10));
    wait_(ri(5, 12));
    key("W", 1);
    wait_(ri(10, 25));
    key("W", 0);
    wait_(ri(20, 40));
}

/* ------------------------------------------------------------ the screens */

/* a slot by what it holds (a name the steps bind), or an index every screen has */
struct slotv {
    const char *name;
    long long idx;
};

static const char *slotv_json(struct slotv s)
{
    static char b[8][64];
    static int i;
    char *o = b[i++ & 7];
    if (s.name) snprintf(o, 64, "\"%s\"", s.name);
    else snprintf(o, 64, "%lld", s.idx);
    return o;
}

static struct slotv slot_any(const char *name)
{
    double c = pr_random(&R);
    if (c < 0.55) step("\"finditem\":-1,\"nth\":%lld,\"as\":\"%s\"", ri(0, 40), name);
    else if (c < 0.8) step("\"finditem\":0,\"nth\":%lld,\"as\":\"%s\"", ri(0, 40), name);
    else return (struct slotv){NULL, pr_below(&R, ANY_SLOTS)};
    return (struct slotv){name, 0};
}

static const char *off(void)
{
    static char b[8][48];
    static int i;
    char *o = b[i++ & 7];
    if (chance(0.6)) {
        long long x = ri(-7, 7);
        long long y = ri(-7, 7);
        snprintf(o, 48, "[%lld,%lld]", x, y);
    } else strcpy(o, "[0,0]");
    return o;
}

static const char *pace(void)
{
    double c = pr_random(&R);
    return c < 0.6 ? "tick" : c < 0.85 ? "frame" : "burst";
}

static void screen_ops(long long n)
{
    for (long long i = 0; i < n; ++i) {
        double c = pr_random(&R);
        if (c < 0.25) {
            struct slotv s = slot_any("s");
            static const int B[3] = {0, 0, 1};
            int b = PICK(B);
            const char *o = off();
            step("\"slot\":%s,\"button\":%d,\"off\":%s,\"pace\":\"%s\"", slotv_json(s), b, o, pace());
        } else if (c < 0.37) {
            /* a shift click, the shift pressed before or in the click's frame */
            static const char *const SH[3] = {"LSHIFT", "LSHIFT", "RSHIFT"};
            const char *sh = PICK(SH);
            struct slotv s = slot_any("s");
            key(sh, 1);
            if (chance(0.6)) wait_(ri(1, 2));
            static const int B[3] = {0, 0, 1};
            int b = PICK(B);
            const char *o = off();
            step("\"slot\":%s,\"button\":%d,\"off\":%s,\"pace\":\"%s\"", slotv_json(s), b, o, pace());
            if (chance(0.5)) {
                struct slotv s2 = slot_any("s2");
                step("\"slot\":%s,\"pace\":\"%s\"", slotv_json(s2), pace());
            }
            if (chance(0.7)) wait_(ri(0, 2));
            key(sh, 0);
        } else if (c < 0.49) {
            /* a drag: a stack on the cursor, pressed on one slot, gliding over others */
            step("\"finditem\":-1,\"nth\":%lld,\"as\":\"src\"", ri(0, 40));
            step("\"slot\":\"src\",\"pace\":\"%s\"", pace());
            static const int B[3] = {0, 1, 1};
            int b = PICK(B);
            step("\"finditem\":0,\"nth\":%lld,\"as\":\"d0\"", ri(0, 40));
            if (chance(0.3)) {
                /* the wheel is a motion to the pointer for a held button: the pressed slot joins */
                step("\"slotdown\":\"d0\",\"button\":%d,\"nomove\":1", b);
                step("\"wheel\":%lld", choice_pm1());
            } else step("\"slotdown\":\"d0\",\"button\":%d", b);
            long long m = ri(1, 5), j;
            for (j = 0; j < m; ++j) {
                step("\"finditem\":0,\"nth\":%lld,\"as\":\"d%lld\"", ri(0, 40), j + 1);
                if (chance(0.5)) {
                    long long fr = ri(1, 5);
                    step("\"glide\":\"d%lld\",\"frames\":%lld,\"off\":%s", j + 1, fr, off());
                } else step("\"slotmove\":\"d%lld\"", j + 1);
                if (chance(0.2)) wait_(1);
            }
            if (chance(0.1)) step("\"slotup\":\"d0\",\"button\":%d", b);
            else step("\"slotup\":\"d%lld\",\"button\":%d", j, b);
            if (chance(0.4)) step("\"slot\":\"src\",\"pace\":\"%s\"", pace());
        } else if (c < 0.56) {
            struct slotv s = slot_any("s");
            step("\"slot\":%s,\"pace\":\"%s\"", slotv_json(s), pace());
            struct slotv s2 = slot_any("s2");
            step("\"dbl\":%s", slotv_json(s2));
        } else if (c < 0.68) {
            /* a number key over a hovered slot (the hover the last frame drew) */
            struct slotv s = slot_any("s");
            step("\"hover\":%s,\"off\":%s", slotv_json(s), off());
            if (chance(0.7)) wait_(ri(1, 3));
            tap_n(ri(1, 9));
            wait_(ri(0, 3));
        } else if (c < 0.75) {
            struct slotv s = slot_any("s");
            step("\"hover\":%s,\"off\":%s", slotv_json(s), off());
            wait_(ri(1, 3));
            if (chance(0.5)) {
                static const char *const C[2] = {"LCONTROL", "RCONTROL"};
                const char *ctrl = PICK(C);
                key(ctrl, 1);
                tap("Q");
                wait_(ri(0, 2));
                key(ctrl, 0);
            } else tap("Q");
        } else if (c < 0.8) {
            static const int B[2] = {0, 1};
            int b = PICK(B);
            step("\"outside\":1,\"button\":%d,\"pace\":\"%s\"", b, pace());
        } else if (c < 0.82) {
            struct slotv s = slot_any("s");
            step("\"slot\":%s,\"button\":2,\"pace\":\"%s\"", slotv_json(s), pace());
        } else if (c < 0.84) {
            /* a side button: GuiContainer takes its press and release too */
            struct slotv s = slot_any("s");
            static const int B[2] = {3, 4};
            int b = PICK(B);
            step("\"slot\":%s,\"button\":%d,\"pace\":\"%s\"", slotv_json(s), b, pace());
        } else if (c < 0.92) {
            /* the pointer wandering */
            long long m = ri(1, 4);
            for (long long j = 0; j < m; ++j) {
                if (chance(0.5)) {
                    long long x = ri(0, 426);
                    long long y = ri(0, 239);
                    step("\"glide\":[%lld,%lld],\"frames\":%lld", x, y, ri(1, 8));
                } else {
                    struct slotv s = slot_any("s");
                    long long fr = ri(1, 8);
                    step("\"glide\":%s,\"frames\":%lld,\"off\":%s", slotv_json(s), fr, off());
                }
                wait_(ri(0, 3));
            }
        } else if (c < 0.95) {
            long long m = ri(1, 3);
            for (long long j = 0; j < m; ++j) step("\"wheel\":%lld", choice_pm1());
            wait_(ri(0, 3));
        } else {
            /* a key the screen does not use, a movement key held into it */
            static const char *const K[7] = {"W", "A", "SPACE", "F1", "F3", "F5", "1"};
            const char *k = PICK(K);
            key(k, 1);
            wait_(ri(1, 10));
            if (chance(0.6)) key(k, 0);
        }
        wait_(ri(0, 6));
        t += 4;
    }
}

static void close_screen(void)
{
    static const char *const EE[2] = {"E", "ESCAPE"};
    double c = pr_random(&R);
    if (c < 0.15) {
        /* mid-drag: a button held on a slot when the screen goes */
        static const int B[2] = {0, 1};
        int b = PICK(B);
        struct slotv s = slot_any("s");
        step("\"slotdown\":%s,\"button\":%d", slotv_json(s), b);
        step("\"close\":1,\"with\":\"%s\"", PICK(EE));
        wait_(ri(1, 10));
        step("\"button\":%d,\"down\":0", b);
    } else if (c < 0.3) {
        /* a movement key pressed as it closes, held into the game */
        static const char *const WASD[4] = {"W", "A", "S", "D"};
        const char *k = PICK(WASD);
        key(k, 1);
        step("\"close\":1,\"with\":\"%s\"", PICK(EE));
        wait_(ri(5, 30));
        key(k, 0);
    } else if (c < 0.45) {
        /* the close and more keys in its frame: the closed screen still takes them */
        struct slotv s = slot_any("s");
        step("\"hover\":%s,\"off\":%s", slotv_json(s), off());
        wait_(ri(1, 3));
        step("\"close\":1,\"with\":\"%s\"", PICK(EE));
        long long m = ri(1, 2);
        for (long long j = 0; j < m; ++j) {
            static const char *const K[6] = {"1", "4", "9", "Q", "E", "ESCAPE"};
            const char *k = PICK(K);
            if (!strcmp(k, "Q") && chance(0.5)) {
                key("LCONTROL", 1);
                step("\"tap\":\"Q\",\"ifscreen\":1");
                key("LCONTROL", 0);
            } else step("\"tap\":\"%s\",\"ifscreen\":1", k);
        }
    } else step("\"close\":1,\"with\":\"%s\"", chance(0.7) ? "E" : "ESCAPE");
    /* the release of a key held since the screen (the game never saw it go down) */
    static const char *const SH[2] = {"LSHIFT", "RSHIFT"};
    struct sv ks = held_sorted(SH, 2, 1);
    for (int i = 0; i < ks.n; ++i) {
        char *k = xstrdup(ks.v[i]);
        key(k, 0);
        free(k);
    }
    free(ks.v);
    wait_(ri(2, 10));
}

static void ep_inventory(void)
{
    if (chance(0.25)) {
        /* walking into the screen: the keys held when it opens */
        static const char *const WASD[4] = {"W", "A", "S", "D"};
        const char *k = PICK(WASD);
        key(k, 1);
        wait_(ri(3, 15));
        tap("E");
        if (chance(0.5)) wait_(ri(1, 5));
        key(k, 0);
    } else tap("E");
    wait_(ri(2, 6));
    screen_ops(ri(3, 14));
    close_screen();
}

static void ep_container(void)
{
    step("\"find\":[54,58,61,62,146],\"r\":6,\"dy\":[-2,3],\"as\":\"box\"");
    step("\"open\":\"box\",\"ticks\":60");
    t += 20;
    screen_ops(ri(3, 12));
    close_screen();
}

/* the stack in SLOT to a hotbar slot by its number key over it: the key */
static long long to_hotbar(long long slot)
{
    long long d = ri(1, 9);
    step("\"hover\":%lld", slot);
    wait_(2);
    tap_n(d);
    wait_(3);
    return d;
}

/* the stack of ITEM (from slot LO) on the cursor, one each over SLOTS, the rest back */
static void right_drag(int item, int lo, const int *slots, int n)
{
    step("\"finditem\":%d,\"as\":\"m\",\"from\":%d", item, lo);
    step("\"slot\":\"m\"");
    wait_(2);
    step("\"slotdown\":%d,\"button\":1", slots[0]);
    for (int i = 1; i < n - 1; ++i) {
        if (chance(0.5)) step("\"glide\":%d,\"frames\":%lld", slots[i], ri(1, 4));
        else step("\"slotmove\":%d", slots[i]);
    }
    step("\"slotup\":%d,\"button\":1", slots[n - 1]);
    wait_(2);
    step("\"slot\":\"m\"");
    wait_(2);
}

/* a crafting table from the inventory's planks, then a chest or a furnace at
 * it, placed and opened: the container screens a checkpoint's world has none of nearby */
static void ep_build(void)
{
    step("\"log\":\"rawfuzz: build\"");
    tap("E");
    wait_(ri(3, 6));
    static const int S1[4] = {1, 2, 4, 3}, S2[8] = {1, 2, 3, 6, 9, 8, 7, 4};
    right_drag(5, 9, S1, 4);
    long long d = to_hotbar(0);
    close_screen();
    tap_n(d);
    wait_(2);
    step("\"place\":58,\"as\":\"table\"");
    wait_(4);
    step("\"open\":\"table\",\"ticks\":60");
    wait_(3);
    static const int IB[3][2] = {{5, 54}, {5, 54}, {4, 61}};
    const int *ib = IB[pr_below(&R, 3)];
    right_drag(ib[0], 10, S2, 8);
    d = to_hotbar(0);
    screen_ops(ri(2, 5));
    close_screen();
    tap_n(d);
    wait_(2);
    step("\"place\":%d,\"as\":\"box\"", ib[1]);
    wait_(4);
    step("\"open\":\"box\",\"ticks\":60");
    wait_(3);
    screen_ops(ri(4, 12));
    close_screen();
    t += 300;
}

/* several things in one frame, in the order a hand makes them */
static void ep_chord(void)
{
    static const struct {
        const char *kind, *v;
    } ACTS[17] = {{"tap", "E"}, {"tap", "Q"}, {"tap", "1"}, {"tap", "5"}, {"tap", "9"}, {"click", "0"}, {"click", "1"},
                  {"click", "2"}, {"wheel", "1"}, {"wheel", "-1"}, {"tap", "SPACE"}, {"tap", "F5"}, {"down", "W"},
                  {"down", "LSHIFT"}, {"down", "LCONTROL"}, {"click", "3"}, {"click", "4"}};
    long long n = ri(1, 3);
    for (long long i = 0; i < n; ++i) {
        int k = (int)ri(2, 4), idx[4];
        pr_sample_idx(&R, 17, k, idx);
        for (int j = 0; j < k; ++j) {
            const char *kind = ACTS[idx[j]].kind, *v = ACTS[idx[j]].v;
            if (!strcmp(kind, "tap")) tap(v);
            else if (!strcmp(kind, "click")) step("\"click\":%s", v);
            else if (!strcmp(kind, "wheel")) step("\"wheel\":%s", v);
            else key(v, 1);
        }
        wait_(ri(1, 6));
        static const char *const H[3] = {"W", "LSHIFT", "LCONTROL"};
        struct sv ks = held_sorted(H, 3, 0);
        for (int j = 0; j < ks.n; ++j) {
            char *kk = xstrdup(ks.v[j]);
            key(kk, 0);
            free(kk);
        }
        free(ks.v);
        if (chance(0.5)) {
            /* whatever screen it opened */
            screen_ops(ri(1, 4));
            close_screen();
        }
    }
}

/* ---------------------------------------------------------- fresh start */

/* the first minutes of a world: logs, planks, a table, sticks */
static void chop_and_craft(void)
{
    step("\"log\":\"rawfuzz: logs\"");
    step("\"find\":[17],\"r\":10,\"dy\":[-2,2],\"as\":\"log\"");
    if (chance(0.4)) step("\"goto\":\"log\",\"within\":1.8,\"ticks\":160,\"sprint\":1");
    else step("\"goto\":\"log\",\"within\":1.8,\"ticks\":160");
    long long n = ri(2, 4);
    for (long long i = 0; i < n; ++i) {
        step("\"find\":[17],\"r\":5,\"dy\":[-2,4],\"as\":\"log\"");
        step("\"mine\":\"log\",\"ticks\":140");
    }
    for (int i = 0; i < 3; ++i) step("\"collect\":7,\"within\":0.3,\"ticks\":60");
    t += 500;
    step("\"log\":\"rawfuzz: planks, a table, sticks\"");
    tap("E");
    wait_(ri(3, 6));
    step("\"finditem\":17,\"as\":\"logs\",\"from\":9");
    step("\"slot\":\"logs\",\"pace\":\"%s\"", pace());
    long long s = ri(1, 4);
    step("\"slot\":%lld,\"pace\":\"%s\"", s, pace());
    /* the placing click lands on the release: shift only after it */
    wait_(2);
    static const char *const SH[2] = {"LSHIFT", "RSHIFT"};
    const char *sh = PICK(SH);
    key(sh, 1);
    wait_(1);
    step("\"slot\":0");
    wait_(ri(3, 5));
    key(sh, 0);
    wait_(2);
    step("\"finditem\":5,\"as\":\"planks\",\"from\":9");
    step("\"slot\":\"planks\"");
    step("\"slotdown\":1,\"button\":1");
    static const int G[3] = {2, 4, 3};
    for (int i = 0; i < 3; ++i) {
        if (chance(0.5)) step("\"glide\":%d,\"frames\":%lld", G[i], ri(1, 4));
        else step("\"slotmove\":%d", G[i]);
    }
    step("\"slotup\":3,\"button\":1");
    step("\"slot\":\"planks\"");
    step("\"slot\":0,\"pace\":\"%s\"", pace());
    step("\"finditem\":0,\"as\":\"empty\",\"from\":36");
    step("\"slot\":\"empty\"");
    screen_ops(ri(2, 6));
    close_screen();
    step("\"select\":58");
    wait_(2);
    step("\"place\":58,\"as\":\"table\"");
    wait_(4);
    step("\"open\":\"table\"");
    t += 40;
    screen_ops(ri(3, 10));
    close_screen();
}

/* ------------------------------------------------------------- session */

static const struct {
    const char *name;
    int w;
    void (*fn)(void);
} EPISODES[] = {{"walk", 30, ep_walk}, {"look", 10, ep_look}, {"hotbar", 10, ep_hotbar}, {"drop", 6, ep_drop},
                {"mine", 12, ep_mine}, {"use", 8, ep_use}, {"eat", 3, ep_eat}, {"attack", 6, ep_attack},
                {"middle", 3, ep_middle}, {"fkeys", 6, ep_fkeys}, {"inventory", 18, ep_inventory},
                {"container", 6, ep_container}, {"chord", 8, ep_chord}, {"pillar", 3, ep_pillar}, {"build", 5, ep_build}};

static void run(void)
{
    wait_(ri(5, 20));
    if (fresh && chance(0.6)) chop_and_craft();
    int total = 0;
    for (int i = 0; i < N(EPISODES); ++i) total += EPISODES[i].w;
    while (t < budget) {
        double x = pr_uniform(&R, 0, total);
        int e = 0;
        for (e = 0; e < N(EPISODES); ++e) {
            x -= EPISODES[e].w;
            if (x <= 0) break;
        }
        if (e == N(EPISODES)) e = N(EPISODES) - 1;
        step("\"log\":\"rawfuzz: %s\"", EPISODES[e].name);
        EPISODES[e].fn();
        wait_(ri(0, 15));
    }
    step("\"end\":1");
}

static const char *USAGE = "usage: rawfuzz_gen SEED [--profile fresh|cp] [--ticks N] [--salt START] > drive.jsonl\n";

int main(int argc, char **argv)
{
    if (argc < 2) {
        fputs(USAGE, stderr);
        return 2;
    }
    char *e;
    long long seed = strtoll(argv[1], &e, 10);
    if (*e || e == argv[1]) die("rawfuzz_gen: not a seed: %s", argv[1]);
    const char *profile = "fresh", *salt = "", *ticks_s = NULL;
    /* a.index(OPT) + 1: the first occurrence's value */
    for (int i = argc - 1; i >= 1; --i) {
        if (i + 1 >= argc) continue;
        if (!strcmp(argv[i], "--profile")) profile = argv[i + 1];
        else if (!strcmp(argv[i], "--salt")) salt = argv[i + 1];
        else if (!strcmp(argv[i], "--ticks")) ticks_s = argv[i + 1];
    }
    if (ticks_s) budget = strtoll(ticks_s, NULL, 10);
    else {
        struct pyrand r;
        pr_seed_int(&r, seed * 7919 + 1);
        budget = pr_randint(&r, 900, 2400);
    }
    fresh = !strcmp(profile, "fresh");
    /* the start's name salts the draws: one seed, a different session per start */
    if (salt[0]) {
        char *s = xasprintf("%lld:%s", seed, salt);
        pr_seed_str(&R, s);
        free(s);
    } else pr_seed_int(&R, seed);
    run();
    return 0;
}
