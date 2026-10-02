/* A seeded model of an RL policy, written as an agent script for
 * make -C oracle script (JSON lines: {"n":k,"act":{...}} steps, Act.java's agent
 * form, and {"cmd":...} commands). The same arguments always give the same
 * script.
 *
 *   out/native/playfuzz_agentgen SEED [--ticks N] [--pos X,Y,Z] [--dev 0|1] [--dim D]
 *
 * Where playfuzz_gen models a person at the SDL input path, this models what a
 * policy emits through the agent input: every tick a chord of holds and
 * presses (attack, use, drop, jump, sneak, sprint, pick, perspective,
 * inventory and the hotbar keys together, press counts over 1), a hotbar
 * index, look deltas up to 180 degrees and absolute looks with the pitch at
 * its limits, ctrl for a whole-stack drop, and gui ops (click, close,
 * respawn, wake, trsel) in any state, valid or not: the harness refuses what
 * a vanilla client cannot produce, and native must refuse it the same way.
 * A session is a run of episodes drawn by weight: chords every tick, sticky
 * chords held for many ticks, long idles, screens with slot clicks under
 * held keys, spins, mining, towers, fights (the agent's aim), held uses
 * interrupted, and (with --dev) standing in a Nether portal pressing E and
 * use. --dev also sets the scene before the first tick: a kit of items, a
 * stone floor, a portal beside the start, a chest, a furnace, a crafting
 * table, a few mobs, and later dev ops (a tp back, a summon, the time).
 * POS is the start position (the checkpoint's, or seed 1's spawn). Every draw
 * is Python's random.Random's random(): the C port of agentgen.py
 * (lane/cport), the same scripts byte for byte. */
#define _GNU_SOURCE
#include "../tool.h"

static const char *const KEYS_ALL[12] = {"forward", "back", "left", "right", "jump", "sneak", "sprint", "attack", "use",
                                         "drop", "inventory", "pick"};
static const char *const HOTBAR[9] = {"hotbar.1", "hotbar.2", "hotbar.3", "hotbar.4", "hotbar.5",
                                      "hotbar.6", "hotbar.7", "hotbar.8", "hotbar.9"};
/* (item, meta, max count): blocks, tools, food, throwables, buckets, containers */
static const int KIT[][3] = {
    {1, 0, 64}, {3, 0, 64}, {4, 0, 64}, {5, 0, 64}, {12, 0, 64}, {13, 0, 64}, {17, 0, 64},
    {20, 0, 64}, {50, 0, 64}, {54, 0, 4}, {58, 0, 4}, {61, 0, 4}, {65, 0, 64}, {85, 0, 64},
    {107, 0, 8}, {53, 0, 64}, {324, 0, 1}, {355, 0, 1}, {326, 0, 1}, {327, 0, 1}, {325, 0, 16},
    {259, 0, 1}, {261, 0, 1}, {262, 0, 64}, {332, 0, 16}, {344, 0, 16}, {368, 0, 16},
    {297, 0, 64}, {260, 0, 64}, {319, 0, 64}, {322, 0, 8}, {267, 0, 1}, {268, 0, 1},
    {278, 0, 1}, {274, 0, 1}, {270, 0, 1}, {359, 0, 1}, {256, 0, 1}, {258, 0, 1}, {296, 0, 64},
    {295, 0, 64}, {351, 15, 64}, {280, 0, 64}, {263, 0, 64}, {265, 0, 64}, {287, 0, 64},
    {306, 0, 1}, {307, 0, 1}, {308, 0, 1}, {309, 0, 1}, {46, 0, 16}, {81, 0, 64}, {338, 0, 64},
    {391, 0, 64}, {282, 0, 1}, {281, 0, 16}, {39, 0, 64}, {2, 0, 64}, {98, 0, 64},
};
static const char *const MOBS[12] = {"Zombie", "Skeleton", "Creeper", "Spider", "Cow", "Pig", "Sheep", "Chicken", "Villager",
                                     "Zombie", "Cow", "Villager"};
static const char *AIM = "EntityZombie|EntitySkeleton|EntityCreeper|EntitySpider|EntityCow|EntityPig|EntitySheep|"
                         "EntityChicken|EntityVillager|EntityPigZombie|EntityEnderman|EntityGhast|EntityBlaze|"
                         "EntityLavaSlime|EntitySlime|EntityWitch|EntityDragon|EntityEnderCrystal";
#define N(a) ((int)(sizeof(a) / sizeof *(a)))

static struct pyrand R;
static long long left_, ticks_;
static int dev, dim, windows = 3, has_portal, portal_tp;
static long long pos[3], portal[3];

static double u(double a, double b) { return a + (b - a) * pr_random(&R); }
static long long gi(long long a, long long b) { return a + (long long)(pr_random(&R) * (double)(b - a + 1)); }
static int p(double x) { return pr_random(&R) < x; }
static int pick_i(int n) { return (int)(pr_random(&R) * n); }
#define PICK(arr) ((arr)[pick_i(N(arr))])

static int weighted_i(const int *ws, int n)
{
    int total = 0;
    for (int i = 0; i < n; ++i) total += ws[i];
    double x = pr_random(&R) * total;
    for (int i = 0; i < n; ++i) {
        x -= ws[i];
        if (x < 0) return i;
    }
    return n - 1;
}

/* weighted over (weight, value) pairs of ints, the values drawn already */
static long long weighted_v(const int *ws, const long long *vs, int n) { return vs[weighted_i(ws, n)]; }

/* a press count: mostly 1, sometimes a burst */
static long long count(void)
{
    long long burst = gi(4, 12);
    static const int W[4] = {10, 3, 2, 1};
    long long V[4] = {1, 2, 3, burst};
    return weighted_v(W, V, 4);
}

static char *fl(double x)
{
    static char b[8][64];
    static int i;
    return py_repr(x, b[i++ & 7]);
}

/* ------------------------------------------------------------ an act */

enum { K_HOLD, K_PRESS, K_HOTBAR, K_DLOOK, K_LOOK, K_GUI, K_CTRL, K_AIM, NK };
static const char *const KNAME[NK] = {"hold", "press", "hotbar", "dlook", "look", "gui", "ctrl", "aim"};

struct act {
    int order[NK], n;
    const char *hold[16];
    int nhold;
    const char *press[512];
    int npress;
    long long hotbar;
    double dlook[2], look[2];
    char gui[4096];
    int aim;
};

static void act_set(struct act *a, int k)
{
    for (int i = 0; i < a->n; ++i)
        if (a->order[i] == k) return;
    a->order[a->n++] = k;
}

static int act_has(const struct act *a, int k)
{
    for (int i = 0; i < a->n; ++i)
        if (a->order[i] == k) return 1;
    return 0;
}

static void act_pop(struct act *a, int k)
{
    for (int i = 0; i < a->n; ++i)
        if (a->order[i] == k) {
            memmove(a->order + i, a->order + i + 1, (size_t)(a->n - i - 1) * sizeof *a->order);
            --a->n;
            return;
        }
}

static void strs(struct sb *b, const char *const *v, int n)
{
    sb_putc(b, '[');
    for (int i = 0; i < n; ++i) sb_printf(b, "%s\"%s\"", i ? "," : "", v[i]);
    sb_putc(b, ']');
}

static void act_json(struct sb *b, const struct act *a)
{
    sb_putc(b, '{');
    for (int i = 0; i < a->n; ++i) {
        int k = a->order[i];
        sb_printf(b, "%s\"%s\":", i ? "," : "", KNAME[k]);
        switch (k) {
        case K_HOLD: strs(b, a->hold, a->nhold); break;
        case K_PRESS: strs(b, a->press, a->npress); break;
        case K_HOTBAR: sb_printf(b, "%lld", a->hotbar); break;
        case K_DLOOK: sb_printf(b, "[%s,%s]", fl(a->dlook[0]), fl(a->dlook[1])); break;
        case K_LOOK: sb_printf(b, "[%s,%s]", fl(a->look[0]), fl(a->look[1])); break;
        case K_GUI: sb_puts(b, a->gui); break;
        case K_CTRL: sb_puts(b, "1"); break;
        case K_AIM: sb_printf(b, "{\"cls\":\"%s\",\"range\":%s}", AIM, fl(a->aim == 0 ? 4.0 : a->aim == 1 ? 6.0 : 16.0)); break;
        }
    }
    sb_putc(b, '}');
}

static void hold_set(struct act *a, const char *const *h, int n)
{
    memcpy(a->hold, h, (size_t)n * sizeof *h);
    a->nhold = n;
    act_set(a, K_HOLD);
}

static void press_add(struct act *a, const char *k, long long times)
{
    for (long long i = 0; i < times; ++i) {
        if (a->npress == N(a->press)) die("agentgen: too many presses");
        a->press[a->npress++] = k;
    }
}

/* ------------------------------------------------------------ output */

static void step(const struct act *a, long long n)
{
    if (left_ <= 0) return;
    if (n > left_) n = left_;
    struct sb b = {0};
    act_json(&b, a);
    printf("{\"n\":%lld,\"act\":%s}\n", n, b.s);
    sb_free(&b);
    left_ -= n;
}

static void step_empty(long long n)
{
    struct act a = {.n = 0};
    step(&a, n);
}

static void devop(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void devop(const char *fmt, ...)
{
    if (!dev) return;
    va_list ap;
    va_start(ap, fmt);
    printf("{\"cmd\":\"dev\",");
    vprintf(fmt, ap);
    printf("}\n");
    va_end(ap);
}

/* ------------------------------------------------------------ pieces of an act */

static void gui_op(struct sb *b)
{
    static const int W[5] = {10, 3, 2, 1, 1};
    static const char *const K[5] = {"click", "close", "respawn", "wake", "trsel"};
    int k = weighted_i(W, 5);
    if (k == 0) {
        long long w = p(0.85) ? -1 : gi(0, windows + 2);
        long long s0 = gi(0, 44), s1 = gi(0, 90), s4 = gi(-20, -2);
        static const int SW_[5] = {12, 3, 2, 2, 1};
        long long SV[5] = {s0, s1, -999, -1, s4};
        long long slot = weighted_v(SW_, SV, 5);
        static const int MW[7] = {8, 4, 3, 2, 3, 3, 2};
        long long MV[7] = {0, 1, 2, 3, 4, 5, 6};
        long long mode = weighted_v(MW, MV, 7), button;
        if (mode == 2) button = p(0.9) ? gi(0, 8) : gi(9, 12);
        else if (mode == 5) {
            static const int B[9] = {0, 1, 2, 4, 5, 6, 8, 9, 10};
            button = PICK(B);
        } else button = gi(0, 2);
        sb_printf(b, "[\"click\",%lld,%lld,%lld,%lld]", w, slot, button, mode);
        return;
    }
    if (k == 4) {
        long long a = gi(1, windows + 2);
        long long c = gi(-1, 4);
        sb_printf(b, "[\"trsel\",%lld,%lld]", a, c);
        return;
    }
    sb_printf(b, "[\"%s\"]", K[k]);
}

static void gui_ops(struct act *a)
{
    long long many = gi(3, 6);
    static const int W[3] = {6, 2, 1};
    long long V[3] = {1, 2, many};
    long long n = weighted_v(W, V, 3);
    struct sb b = {0};
    sb_putc(&b, '[');
    for (long long i = 0; i < n; ++i) {
        if (i) sb_putc(&b, ',');
        gui_op(&b);
    }
    sb_putc(&b, ']');
    if (b.n >= sizeof a->gui) die("agentgen: gui too long");
    strcpy(a->gui, b.s);
    sb_free(&b);
    act_set(a, K_GUI);
}

static void look(struct act *a, double big)
{
    double x = pr_random(&R);
    if (x < 0.55) {
        double d0 = u(-180, 180);
        d0 = py_round(d0 * (p(big) ? 1 : 0.1), 3);
        double d1 = u(-180, 180);
        d1 = py_round(d1 * (p(big) ? 1 : 0.05), 3);
        a->dlook[0] = d0, a->dlook[1] = d1;
        act_set(a, K_DLOOK);
    } else if (x < 0.62) {
        double last = py_round(u(-90, 90), 3);
        double P[6] = {90.0, -90.0, 89.99, -89.99, 0.0, last};
        double pitch = P[pick_i(6)];
        double yaw = p(0.8) ? py_round(u(-180, 180), 3) : py_round(u(-1e6, 1e6), 1);
        a->look[0] = yaw, a->look[1] = pitch;
        act_set(a, K_LOOK);
    }
}

/* one tick of a policy's output: any keys at once */
static void chord(struct act *a, double hold_p, double press_p)
{
    memset(a, 0, sizeof *a);
    const char *hold[12];
    int nh = 0;
    for (int i = 0; i < 12; ++i)
        if (p(hold_p * (i < 4 ? 1.6 : 1.0))) hold[nh++] = KEYS_ALL[i];
    if (nh) hold_set(a, hold, nh);
    for (int i = 0; i < 12; ++i)
        if (p(press_p * (!strcmp(KEYS_ALL[i], "inventory") ? 0.4 : 1.0))) press_add(a, KEYS_ALL[i], count());
    if (p(0.004)) press_add(a, "perspective", 1);
    if (p(0.06)) {
        const char *h = PICK(HOTBAR);
        press_add(a, h, count());
    }
    if (a->npress) {
        pr_shuffle(&R, a->press, a->npress, sizeof *a->press);
        act_set(a, K_PRESS);
    }
    if (p(0.12)) {
        a->hotbar = gi(0, 8);
        act_set(a, K_HOTBAR);
    }
    look(a, 0.5);
    if (p(0.05)) gui_ops(a);
    if (p(0.04)) act_set(a, K_CTRL);
}

static int maybe_hold(const char **out)
{
    int n = 0;
    for (int i = 0; i < 12; ++i)
        if (p(0.12)) out[n++] = KEYS_ALL[i];
    return n;
}

static void valid_click(struct sb *b)
{
    static const int MW[6] = {8, 4, 3, 2, 2, 2};
    long long MV[6] = {0, 1, 2, 4, 6, 5};
    long long mode = weighted_v(MW, MV, 6);
    long long slot = p(0.9) ? gi(0, 44) : -999;
    if (mode == 2) {
        long long bt = gi(0, 8);
        sb_printf(b, "[\"click\",-1,%lld,%lld,2]", slot > 0 ? slot : 0, bt);
    } else if (mode == 5) {
        long long s = p(0.5) ? slot : -999;
        static const int B[6] = {0, 1, 2, 4, 5, 6};
        int bt = PICK(B);
        sb_printf(b, "[\"click\",-1,%lld,%d,5]", s, bt);
    } else {
        long long bt = gi(0, 1);
        sb_printf(b, "[\"click\",-1,%lld,%lld,%lld]", slot, bt, mode);
    }
}

/* ------------------------------------------------------------ episodes */

static void ep_chords(void)
{
    double hp = u(0.05, 0.45);
    double pp = u(0.02, 0.3);
    long long n = gi(10, 300);
    for (long long i = 0; i < n; ++i) {
        struct act a;
        chord(&a, hp, pp);
        step(&a, 1);
    }
}

/* a chord held, with one key spammed or not */
static void ep_sticky(void)
{
    struct act base;
    chord(&base, u(0.1, 0.5), 0);
    act_pop(&base, K_GUI);
    static const char *const SPAM[8] = {"attack", "use", "drop", "jump", NULL, NULL, "pick", "sneak"};
    const char *spam = PICK(SPAM);
    long long n = gi(5, 400);
    long long every = gi(1, 20);
    const char **hold = base.hold;
    int nhold = act_has(&base, K_HOLD) ? base.nhold : 0;
    int has_dlook = 0;
    double dl[2] = {0, 0};
    if (p(0.4)) {
        dl[0] = py_round(u(-30, 30), 3);
        dl[1] = py_round(u(-5, 5), 3);
        has_dlook = 1;
    }
    long long t = 0;
    while (t < n && left_ > 0) {
        long long k = every < n - t ? every : n - t;
        struct act a;
        memset(&a, 0, sizeof a);
        if (nhold) hold_set(&a, hold, nhold);
        if (spam) {
            long long c = p(0.3) ? count() : 1;
            press_add(&a, spam, c);
            act_set(&a, K_PRESS);
        }
        if (has_dlook) {
            a.dlook[0] = dl[0], a.dlook[1] = dl[1];
            act_set(&a, K_DLOOK);
        }
        if (t == 0 && act_has(&base, K_HOTBAR)) {
            a.hotbar = base.hotbar;
            act_set(&a, K_HOTBAR);
        }
        step(&a, 1);
        if (k > 1) {
            struct act h;
            memset(&h, 0, sizeof h);
            if (nhold) hold_set(&h, hold, nhold);
            step(&h, k - 1);
        }
        t += k;
    }
}

static void ep_idle(void)
{
    long long a = gi(20, 200), b = gi(200, 800), c = gi(800, 2000);
    static const int W[3] = {4, 2, 1};
    long long V[3] = {a, b, c};
    step_empty(weighted_v(W, V, 3));
}

/* open the inventory (or whatever the crosshair offers) and click, keys held all along */
static void ep_screen(void)
{
    {
        struct act a;
        memset(&a, 0, sizeof a);
        press_add(&a, p(0.7) ? "inventory" : "use", 1);
        act_set(&a, K_PRESS);
        const char *h[12];
        int nh = maybe_hold(h);
        hold_set(&a, h, nh);
        step(&a, 1);
    }
    windows += 1;
    long long n = gi(3, 120);
    for (long long i = 0; i < n; ++i) {
        struct act a;
        memset(&a, 0, sizeof a);
        if (p(0.3)) chord(&a, 0.2, 0.15);
        else {
            const char *h[12];
            int nh = maybe_hold(h);
            if (nh) hold_set(&a, h, nh);
        }
        if (p(0.6)) {
            long long many = gi(3, 8);
            static const int W[3] = {5, 2, 1};
            long long V[3] = {1, 2, many};
            long long m = weighted_v(W, V, 3);
            struct sb b = {0};
            sb_putc(&b, '[');
            for (long long j = 0; j < m; ++j) {
                if (j) sb_putc(&b, ',');
                if (p(0.8)) valid_click(&b);
                else gui_op(&b);
            }
            sb_putc(&b, ']');
            if (b.n >= sizeof a.gui) die("agentgen: gui too long");
            strcpy(a.gui, b.s);
            sb_free(&b);
            act_set(&a, K_GUI);
        }
        step(&a, 1);
    }
    if (p(0.7)) {
        struct act a;
        memset(&a, 0, sizeof a);
        strcpy(a.gui, "[[\"close\"]]");
        act_set(&a, K_GUI);
        const char *h[12];
        int nh = maybe_hold(h);
        hold_set(&a, h, nh);
        step(&a, 1);
    }
}

static void ep_spin(void)
{
    static const int S[2] = {1, -1};
    int s = PICK(S);
    double yaw = s * u(150, 180);
    double pitch = u(-3, 3);
    long long n = gi(50, 600);
    for (long long t = 0; t < n; ++t) {
        struct act a;
        memset(&a, 0, sizeof a);
        const char *h[2] = {"forward", "sprint"};
        hold_set(&a, h, p(0.5) ? 2 : 1);
        a.dlook[0] = py_round(yaw, 3), a.dlook[1] = py_round(pitch, 3);
        act_set(&a, K_DLOOK);
        if (t % gi(3, 40) == 0) {
            static const char *const P[4] = {"use", "attack", "jump", "drop"};
            press_add(&a, PICK(P), 1);
            act_set(&a, K_PRESS);
        }
        step(&a, 1);
    }
}

static void ep_mine(void)
{
    double pitch = u(20, 90);
    {
        struct act a;
        memset(&a, 0, sizeof a);
        a.look[0] = py_round(u(-180, 180), 3), a.look[1] = py_round(pitch, 3);
        act_set(&a, K_LOOK);
        a.hotbar = gi(0, 8);
        act_set(&a, K_HOTBAR);
        step(&a, 1);
    }
    long long n = gi(20, 400);
    for (long long i = 0; i < n; ++i) {
        struct act a;
        memset(&a, 0, sizeof a);
        const char *h[3];
        int nh = 0;
        h[nh++] = "attack";
        if (p(0.3)) h[nh++] = "forward";
        if (p(0.2)) h[nh++] = "use";
        hold_set(&a, h, nh);
        if (p(0.1)) {
            a.dlook[0] = py_round(u(-20, 20), 3);
            a.dlook[1] = py_round(u(-10, 10), 3);
            act_set(&a, K_DLOOK);
        }
        if (p(0.05)) {
            long long hb = gi(1, 9);
            const char *P[4] = {"attack", "use", HOTBAR[hb - 1], "drop"};
            press_add(&a, P[pick_i(4)], 1);
            act_set(&a, K_PRESS);
        }
        step(&a, 1);
    }
}

static void ep_tower(void)
{
    {
        struct act a;
        memset(&a, 0, sizeof a);
        a.look[0] = py_round(u(-180, 180), 3), a.look[1] = 90.0;
        act_set(&a, K_LOOK);
        a.hotbar = gi(0, 8);
        act_set(&a, K_HOTBAR);
        step(&a, 1);
    }
    long long n = gi(10, 120);
    for (long long t = 0; t < n; ++t) {
        struct act a;
        memset(&a, 0, sizeof a);
        const char *h[3];
        int nh = 0;
        h[nh++] = "jump";
        if (p(0.5)) h[nh++] = "use";
        if (p(0.1)) h[nh++] = "sneak";
        hold_set(&a, h, nh);
        if (t % 3 == 0) {
            press_add(&a, "use", p(0.8) ? 1 : count());
            act_set(&a, K_PRESS);
        }
        step(&a, 1);
    }
}

static void ep_fight(void)
{
    int aim = pick_i(3);
    {
        struct act a;
        memset(&a, 0, sizeof a);
        a.hotbar = gi(0, 8);
        act_set(&a, K_HOTBAR);
        step(&a, 1);
    }
    long long n = gi(10, 200);
    for (long long i = 0; i < n; ++i) {
        struct act a;
        memset(&a, 0, sizeof a);
        a.aim = aim;
        act_set(&a, K_AIM);
        if (p(0.5)) {
            press_add(&a, "attack", count());
            act_set(&a, K_PRESS);
        }
        if (p(0.15)) {
            press_add(&a, "use", count());
            act_set(&a, K_PRESS);
        }
        const char *h[3];
        int nh = 0;
        if (p(0.6)) h[nh++] = KEYS_ALL[pick_i(4)];
        if (p(0.3)) h[nh++] = "jump";
        if (p(0.2)) h[nh++] = "sprint";
        if (nh) hold_set(&a, h, nh);
        step(&a, 1);
    }
}

/* hold use (eat, drink, draw, block), interrupted by a hotbar key, a drop or E */
static void ep_use(void)
{
    static const char *const USE[1] = {"use"};
    {
        struct act a;
        memset(&a, 0, sizeof a);
        a.hotbar = gi(0, 8);
        act_set(&a, K_HOTBAR);
        press_add(&a, "use", 1);
        act_set(&a, K_PRESS);
        hold_set(&a, USE, 1);
        step(&a, 1);
    }
    long long n = gi(3, 60);
    {
        struct act a;
        memset(&a, 0, sizeof a);
        hold_set(&a, USE, 1);
        step(&a, n);
    }
    static const char *const X[5] = {"hotbar", "drop", "inventory", "release", "attack"};
    const char *x = PICK(X);
    struct act a;
    memset(&a, 0, sizeof a);
    if (!strcmp(x, "hotbar")) {
        hold_set(&a, USE, 1);
        a.hotbar = gi(0, 8);
        act_set(&a, K_HOTBAR);
    } else if (strcmp(x, "release")) {
        hold_set(&a, USE, 1);
        press_add(&a, x, 1);
        act_set(&a, K_PRESS);
    }
    step(&a, 1);
    memset(&a, 0, sizeof a);
    hold_set(&a, USE, p(0.5) ? 1 : 0);
    step(&a, gi(1, 40));
}

/* the death screen's op, whether or not the player is dead */
static void ep_respawn(void)
{
    long long n = gi(1, 5);
    for (long long i = 0; i < n; ++i) {
        struct act a;
        memset(&a, 0, sizeof a);
        long long k = gi(1, 2);
        strcpy(a.gui, k == 1 ? "[[\"respawn\"]]" : "[[\"respawn\"],[\"respawn\"]]");
        act_set(&a, K_GUI);
        press_add(&a, KEYS_ALL[pick_i(12)], 1);
        act_set(&a, K_PRESS);
        step(&a, 1);
        step_empty(gi(0, 30));
    }
}

/* stand in the portal: E, use, attack and clicks at once, screens opening and closing */
static void ep_portal(void)
{
    if (!portal_tp && ticks_ - left_ < 600) {
        portal_tp = 1;
        double yaw = py_round(u(-180, 180), 1);
        double pitch = py_round(u(-60, 60), 1);
        devop("\"op\":\"tp\",\"x\":%s,\"y\":%s,\"z\":%s,\"yaw\":%s,\"pitch\":%s", fl(portal[0] + 0.9), fl((double)portal[1]),
              fl(portal[2] + 1.0), fl(yaw), fl(pitch));
        step_empty(2);
    }
    long long n = gi(10, 140);
    for (long long i = 0; i < n; ++i) {
        struct act a;
        memset(&a, 0, sizeof a);
        if (p(0.4)) {
            static const char *const P[6] = {"inventory", "use", "attack", "inventory", "drop", "jump"};
            long long m = gi(1, 3);
            for (long long j = 0; j < m; ++j) press_add(&a, PICK(P), 1);
            act_set(&a, K_PRESS);
        }
        if (p(0.3)) {
            static const char *const H[4] = {"use", "attack", "sneak", "jump"};
            const char *h[1] = {PICK(H)};
            hold_set(&a, h, 1);
        }
        if (p(0.3)) {
            struct sb b = {0};
            sb_putc(&b, '[');
            if (p(0.7)) valid_click(&b);
            else gui_op(&b);
            sb_putc(&b, ']');
            strcpy(a.gui, b.s);
            sb_free(&b);
            act_set(&a, K_GUI);
        }
        if (p(0.2)) look(&a, 0.2);
        step(&a, 1);
    }
}

static void ep_devop(void)
{
    static const char *const K[3] = {"time", "time", "state"};
    const char *k = PICK(K);
    if (!strcmp(k, "time")) {
        static const int V[6] = {1000, 6000, 12500, 13000, 18000, 23000};
        devop("\"op\":\"time\",\"value\":%d", PICK(V));
    } else {
        static const int H[4] = {1, 4, 10, 20}, F[4] = {2, 6, 15, 20};
        int h = PICK(H);
        int f = PICK(F);
        devop("\"op\":\"state\",\"health\":%s,\"food\":%d,\"saturation\":0.0", fl((double)h), f);
    }
    step_empty(2);
}

/* ------------------------------------------------------------ the session */

static void setup(void)
{
    long long x = pos[0], y = pos[1], z = pos[2];
    devop("\"op\":\"fill\",\"x0\":%lld,\"y0\":%lld,\"z0\":%lld,\"x1\":%lld,\"y1\":%lld,\"z1\":%lld,\"id\":1,\"meta\":0", x - 6, y - 1,
          z - 6, x + 6, y - 1, z + 6);
    devop("\"op\":\"fill\",\"x0\":%lld,\"y0\":%lld,\"z0\":%lld,\"x1\":%lld,\"y1\":%lld,\"z1\":%lld,\"id\":0,\"meta\":0", x - 6, y, z - 6,
          x + 6, y + 5, z + 6);
    /* a portal along z at x + 3 (portal-inv-s1's form); none in the End */
    if (dim != 1) {
        has_portal = 1;
        portal[0] = x + 3, portal[1] = y, portal[2] = z - 1;
        devop("\"op\":\"fill\",\"x0\":%lld,\"y0\":%lld,\"z0\":%lld,\"x1\":%lld,\"y1\":%lld,\"z1\":%lld,\"id\":49,\"meta\":0", x + 3,
              y - 1, z - 2, x + 3, y + 3, z + 1);
        devop("\"op\":\"fill\",\"x0\":%lld,\"y0\":%lld,\"z0\":%lld,\"x1\":%lld,\"y1\":%lld,\"z1\":%lld,\"id\":90,\"meta\":2", x + 3, y,
              z - 1, x + 3, y + 2, z);
    }
    static const long long SB[5][3] = {{-2, 2, 54}, {-3, 2, 58}, {-4, 2, 61}, {-2, -3, 54}, {-3, -3, 54}};
    static const int SM[5] = {2, 0, 2, 3, 3};
    for (int i = 0; i < 5; ++i)
        devop("\"op\":\"setblock\",\"x\":%lld,\"y\":%lld,\"z\":%lld,\"id\":%lld,\"meta\":%d,\"mode\":\"replace\"", x + SB[i][0], y,
              z + SB[i][1], SB[i][2], SM[i]);
    /* the tp first: each slot op queues an S2F, and native's dev tp wants room for its S08s */
    double yaw = py_round(u(-180, 180), 1);
    devop("\"op\":\"tp\",\"x\":%s,\"y\":%s,\"z\":%s,\"yaw\":%s,\"pitch\":0.0", fl(x + 0.5), fl((double)y), fl(z + 0.5), fl(yaw));
    int slots[36];
    for (int i = 0; i < 36; ++i) slots[i] = i;
    pr_shuffle(&R, slots, 36, sizeof *slots);
    long long ns = gi(6, 30);
    for (long long i = 0; i < ns && i < 36; ++i) {
        const int *k = KIT[pick_i(N(KIT))];
        long long c = gi(1, k[2]);
        devop("\"op\":\"slot\",\"slot\":%d,\"item\":%d,\"count\":%lld,\"meta\":%d", slots[i], k[0], c, k[1]);
    }
    if (p(0.3)) {
        static const int ARM[4] = {309, 308, 307, 306};
        for (int s = 36; s < 40; ++s)
            if (p(0.5)) devop("\"op\":\"slot\",\"slot\":%d,\"item\":%d,\"count\":1,\"meta\":0", s, ARM[s - 36]);
    }
    long long nm = gi(0, 4);
    for (long long i = 0; i < nm; ++i) {
        const char *name = PICK(MOBS);
        long long dx = gi(-5, 5);
        long long dz = gi(-5, 5);
        devop("\"op\":\"summon\",\"name\":\"%s\",\"x\":%s,\"y\":%s,\"z\":%s", name, fl(x + dx + 0.5), fl((double)y), fl(z + dz + 0.5));
    }
    devop("\"op\":\"state\",\"health\":20.0,\"food\":20,\"saturation\":5.0");
    step_empty(3);
}

static void run(void)
{
    printf("{\"cmd\":\"run\",\"class\":\"Snapshot\"}\n");
    if (dev) setup();
    typedef void (*ep)(void);
    ep fs[12] = {ep_chords, ep_sticky, ep_idle, ep_screen, ep_spin, ep_mine, ep_tower, ep_fight, ep_use, ep_respawn, ep_devop, ep_portal};
    int ws[12] = {10, 6, 3, 5, 2, 3, 2, 4, 3, 2, 2, 4};
    int n = 10 + (dev ? 1 + has_portal : 0);
    while (left_ > 0) fs[weighted_i(ws, n)]();
}

static int is_digits(const char *s)
{
    if (!*s) return 0;
    for (; *s; ++s)
        if (*s < '0' || *s > '9') return 0;
    return 1;
}

int main(int argc, char **argv)
{
    const char *a0 = argc > 1 ? argv[1] : "";
    while (*a0 == '-') ++a0;
    if (argc < 2 || !is_digits(a0)) {
        fputs("usage: playfuzz_agentgen SEED [--ticks N] [--pos X,Y,Z] [--dev 0|1] [--dim D]\n", stderr);
        return 2;
    }
    long long seed = strtoll(argv[1], NULL, 10);
    const char *ticks_s = NULL, *pos_s = "161,67,253", *dev_s = "0", *dim_s = "0";
    /* dict(zip(args[1::2], args[2::2])): option, value pairs, the last of a key winning */
    for (int i = 2; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--ticks")) ticks_s = argv[i + 1];
        else if (!strcmp(argv[i], "--pos")) pos_s = argv[i + 1];
        else if (!strcmp(argv[i], "--dev")) dev_s = argv[i + 1];
        else if (!strcmp(argv[i], "--dim")) dim_s = argv[i + 1];
    }
    struct pyrand r;
    pr_seed_int(&r, seed * 7919 + 1);
    long long def = 2000 + (long long)(pr_random(&r) * 3001);
    ticks_ = ticks_s ? strtoll(ticks_s, NULL, 10) : def;
    char *ps = xstrdup(pos_s);
    int k = 0;
    for (char *t = strtok(ps, ","); t && k < 3; t = strtok(NULL, ",")) pos[k++] = (long long)strtod(t, NULL);
    if (k != 3) die("playfuzz_agentgen: --pos wants X,Y,Z");
    dev = !strcmp(dev_s, "1");
    dim = atoi(dim_s);
    pr_seed_int(&R, seed);
    left_ = ticks_;
    run();
    return 0;
}
