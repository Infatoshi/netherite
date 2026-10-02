/* The playfuzz model of a human player (gen.c) that ends.c extends: a seeded
 * session of SDL input events for out/native/play's --input-script, and the
 * kit (Dev header entries) a session with a pose gets. The C port of
 * gen.py's Kit and Gen classes (lane/cport): the same SEED, TICKS and pose
 * give the same script byte for byte, every draw a random.Random draw in
 * Python's order (tests/tool.h's pyrand). */
#ifndef NETHERITE_PLAYGEN_H
#define NETHERITE_PLAYGEN_H

#include "../tool.h"

enum { SW = 427, SH = 240 }; /* GuiScreen scaled size of the 854x480 window at gui scale 2 */

struct pt {
    int x, y;
};

/* centres of the slots of every screen the player can have open */
static struct pt PLAYER_SLOTS[45], ALL_SLOTS[211], MERCHANT_SLOTS[5], MERCHANT_IN[2], MERCHANT_OUT, MERCHANT_NEXT,
    MERCHANT_PREV;
static int n_all;

static void add_pts(int left, int top, const struct pt *xy, int n)
{
    for (int i = 0; i < n; ++i) ALL_SLOTS[n_all++] = (struct pt){left + xy[i].x + 8, top + xy[i].y + 8};
}

static void slot_points(void)
{
    struct pt v[100];
    int n = 0;
    v[n++] = (struct pt){144, 36};
    for (int i = 0; i < 4; ++i) v[n++] = (struct pt){88 + (i % 2) * 18, 26 + (i / 2) * 18};
    for (int i = 0; i < 4; ++i) v[n++] = (struct pt){8, 8 + i * 18};
    for (int i = 0; i < 27; ++i) v[n++] = (struct pt){8 + (i % 9) * 18, 84 + (i / 9) * 18};
    for (int i = 0; i < 9; ++i) v[n++] = (struct pt){8 + i * 18, 142};
    int top166 = (SH - 166) / 2, left = (SW - 176) / 2;
    add_pts(left, top166, v, n);
    memcpy(PLAYER_SLOTS, ALL_SLOTS, sizeof PLAYER_SLOTS);
    n = 0;
    v[n++] = (struct pt){124, 35};
    for (int i = 0; i < 9; ++i) v[n++] = (struct pt){30 + (i % 3) * 18, 17 + (i / 3) * 18};
    add_pts(left, top166, v, n); /* workbench */
    n = 0;
    v[n++] = (struct pt){56, 17}, v[n++] = (struct pt){56, 53}, v[n++] = (struct pt){116, 35};
    add_pts(left, top166, v, n); /* furnace */
    for (int rows = 3; rows <= 6; rows += 3) { /* chests */
        int h = 114 + rows * 18, top = (SH - h) / 2;
        n = 0;
        for (int i = 0; i < rows * 9; ++i) v[n++] = (struct pt){8 + (i % 9) * 18, 18 + (i / 9) * 18};
        add_pts(left, top, v, n);
        n = 0;
        for (int i = 0; i < 27; ++i) v[n++] = (struct pt){8 + (i % 9) * 18, 103 + (rows - 4) * 18 + (i / 9) * 18};
        add_pts(left, top, v, n);
        n = 0;
        for (int i = 0; i < 9; ++i) v[n++] = (struct pt){8 + i * 18, 161 + (rows - 4) * 18};
        add_pts(left, top, v, n);
    }
    int ml = (SW - 176) / 2, mt = (SH - 166) / 2;
    /* GuiMerchant: the two inputs, the result, and the next and previous trade buttons (12x19) */
    MERCHANT_IN[0] = (struct pt){ml + 36 + 8, mt + 53 + 8};
    MERCHANT_IN[1] = (struct pt){ml + 62 + 8, mt + 53 + 8};
    MERCHANT_OUT = (struct pt){ml + 120 + 8, mt + 53 + 8};
    MERCHANT_NEXT = (struct pt){ml + 120 + 27 + 6, mt + 24 - 1 + 9};
    MERCHANT_PREV = (struct pt){ml + 36 - 19 + 6, mt + 24 - 1 + 9};
    MERCHANT_SLOTS[0] = MERCHANT_IN[0], MERCHANT_SLOTS[1] = MERCHANT_IN[1], MERCHANT_SLOTS[2] = MERCHANT_OUT;
    MERCHANT_SLOTS[3] = MERCHANT_NEXT, MERCHANT_SLOTS[4] = MERCHANT_PREV;
}

/* the centre of inventory index i (0-8 the hotbar, 9-35 the rest) on a 166-high screen */
static struct pt inv_point(int i)
{
    int left = (SW - 176) / 2, top = (SH - 166) / 2;
    if (i < 9) return (struct pt){left + 8 + i * 18 + 8, top + 142 + 8};
    return (struct pt){left + 8 + ((i - 9) % 9) * 18 + 8, top + 84 + ((i - 9) / 9) * 18 + 8};
}

static const int OX[4] = {0, -1, 0, 1}, OZ[4] = {1, 0, -1, 0}; /* 0 south (+z), 1 west, 2 north, 3 east */
static const int DRINK[] = {8193, 8194, 8195, 8196, 8197, 8198, 8200, 8201, 8202, 8204, 8205, 8206, 8225, 8226, 8229,
                            8233, 8257, 8258, 8259, 0, 16, 64};
static const int SPLASH[] = {16385, 16386, 16388, 16389, 16392, 16393, 16394, 16396, 16398, 16420, 16421, 16426, 16452};
#define NDRINK ((int)(sizeof DRINK / sizeof *DRINK))
#define NSPLASH ((int)(sizeof SPLASH / sizeof *SPLASH))

/* a kit's tools: item, count (a random meta for the potions), in TOOLS' order */
static const struct tool {
    const char *name;
    int item, count;
} TOOLS[] = {{"pearl", 368, 16}, {"eye", 381, 16}, {"potion", 373, 1}, {"splash", 373, 1}, {"fishing_rod", 346, 1},
             {"boat", 333, 1}, {"minecart", 328, 1}, {"tnt", 46, 16}, {"flint", 259, 1}, {"enchanting_table", 116, 4},
             {"anvil", 145, 2}, {"brewing_stand", 379, 4}, {"saddle", 329, 1}, {"carrot_stick", 398, 1},
             {"lead", 420, 4}, {"fence", 85, 16}, {"emerald", 388, 64}};
#define NTOOLS ((int)(sizeof TOOLS / sizeof *TOOLS))
/* the goods a villager buys or takes with emeralds, in the inventory's first row */
static const int GOODS[9][2] = {{388, 64}, {296, 64}, {339, 64}, {263, 64}, {367, 64}, {287, 64}, {365, 64}, {266, 64}, {265, 64}};

static int tool_index(const char *k)
{
    for (int i = 0; i < NTOOLS; ++i)
        if (!strcmp(TOOLS[i].name, k)) return i;
    die("no tool %s", k);
    return -1;
}

/* ------------------------------------------------------------ the kit */

struct kitop {
    char *json;
    int is_slot, slot, item, count;
};

struct kit {
    int on, dim;
    double yaw, pitch;
    const char *stage;
    struct kitop ops[128];
    int nops;
    int slot[NTOOLS]; /* the hotbar slot of each tool, -1 if none */
    int inv_item[9], inv_slot[9], ninv;
    int f, px, py, pz;
    double feet;
    int has_mob;
    double mob[3];
};

static void kit_add(struct kit *k, int at_front, char *json, int is_slot, int slot, int item, int count)
{
    if (k->nops == 128) die("kit: too many ops");
    if (at_front) memmove(k->ops + 1, k->ops, (size_t)k->nops * sizeof *k->ops);
    struct kitop *o = at_front ? &k->ops[0] : &k->ops[k->nops];
    *o = (struct kitop){json, is_slot, slot, item, count};
    ++k->nops;
}

static char *fmt_float(double x)
{
    static char b[8][64];
    static int i;
    return py_repr(x, b[i++ & 7]);
}

static void kit_at(const struct kit *k, int a, int l, int dy, int *x, int *y, int *z)
{
    int f = k->f, rr = (k->f + 1) & 3;
    *x = k->px + OX[f] * a + OX[rr] * l;
    *y = k->py + dy;
    *z = k->pz + OZ[f] * a + OZ[rr] * l;
}

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

static void kit_box(struct kit *k, int a0, int a1, int l0, int l1, int dy0, int dy1, int block, int meta)
{
    int x0, y0, z0, x1, y1, z1;
    kit_at(k, a0, l0, dy0, &x0, &y0, &z0);
    kit_at(k, a1, l1, dy1, &x1, &y1, &z1);
    kit_add(k, 0, xasprintf("{\"op\":\"fill\",\"x0\":%d,\"y0\":%d,\"z0\":%d,\"x1\":%d,\"y1\":%d,\"z1\":%d,\"id\":%d,\"meta\":%d}",
                            imin(x0, x1), imin(y0, y1), imin(z0, z1), imax(x0, x1), imax(y0, y1), imax(z0, z1), block, meta),
            0, 0, 0, 0);
}

static void kit_set(struct kit *k, int a, int l, int dy, int block, int meta)
{
    int x, y, z;
    kit_at(k, a, l, dy, &x, &y, &z);
    kit_add(k, 0, xasprintf("{\"op\":\"setblock\",\"x\":%d,\"y\":%d,\"z\":%d,\"id\":%d,\"meta\":%d}", x, y, z, block, meta),
            0, 0, 0, 0);
}

static int kit_floor_id(const struct kit *k) { return k->dim == 1 ? 121 : k->dim == -1 ? 87 : 1; }

static void kit_clear(struct kit *k, int a0, int a1, int l0, int l1, int h)
{
    kit_box(k, a0, a1, l0, l1, 0, h, 0, 0);
    kit_box(k, a0, a1, l0, l1, -1, -1, kit_floor_id(k), 0);
}

static void kit_summon(struct kit *k, const char *name, int a, int l)
{
    int x, y, z;
    kit_at(k, a, l, 0, &x, &y, &z);
    kit_add(k, 0, xasprintf("{\"op\":\"summon\",\"name\":\"%s\",\"x\":%s,\"y\":%s,\"z\":%s}", name, fmt_float(x + 0.5),
                            fmt_float(k->feet), fmt_float(z + 0.5)),
            0, 0, 0, 0);
    k->has_mob = 1;
    k->mob[0] = x + 0.5, k->mob[1] = k->feet, k->mob[2] = z + 0.5;
}

static void kit_stage(struct kit *k, struct pyrand *r)
{
    const char *st = k->stage;
    if (!strcmp(st, "nether_portal")) {
        /* obsidian 4 wide (lateral -1..2) and 5 high around a 2x3 inside, 2 ahead; a wall behind it */
        kit_clear(k, 1, 4, -2, 3, 4);
        kit_box(k, 2, 2, -1, 2, -1, 3, 49, 0);
        kit_box(k, 2, 2, 0, 1, 0, 2, 0, 0);
        kit_box(k, 3, 3, 0, 1, 0, 1, kit_floor_id(k), 0);
    } else if (!strcmp(st, "end_portal")) {
        /* the ring: every frame but the near middle one holds an eye (meta + 4), each facing the inside */
        int f = k->f, rr = (k->f + 1) & 3;
        kit_clear(k, 1, 7, -3, 3, 3);
        for (int l = -1; l <= 1; ++l) {
            kit_set(k, 2, l, 0, 120, f + (l == 0 ? 0 : 4));
            kit_set(k, 6, l, 0, 120, ((f + 2) & 3) + 4);
        }
        for (int a = 3; a <= 5; ++a) {
            kit_set(k, a, -2, 0, 120, rr + 4);
            kit_set(k, a, 2, 0, 120, ((rr + 2) & 3) + 4);
        }
    } else if (!strcmp(st, "trade")) {
        kit_clear(k, 1, 3, -1, 1, 2);
        kit_summon(k, "Villager", 2, 0);
    } else if (!strcmp(st, "pig")) {
        kit_clear(k, 1, 6, -3, 3, 2);
        kit_summon(k, "Pig", 2, 0);
    } else if (!strcmp(st, "lead")) {
        /* an animal ahead, a fence post two behind */
        kit_clear(k, -3, -1, -1, 1, 2);
        kit_clear(k, 1, 3, -1, 1, 2);
        kit_set(k, -2, 0, 0, 85, 0);
        static const char *const A[4] = {"Cow", "Pig", "Sheep", "Chicken"};
        const char *name = A[(int)(pr_random(r) * 4)];
        kit_summon(k, name, 2, 0);
    } else if (!strcmp(st, "rail")) {
        kit_clear(k, 1, 3, -1, 1, 2);
        kit_set(k, 2, 0, 0, 66, 0);
    }
}

static int has_tool(const char *const *tools, int n, const char *name)
{
    for (int i = 0; i < n; ++i)
        if (!strcmp(tools[i], name)) return 1;
    return 0;
}

/* what a kit session is given, and where its scene stands; its own Random,
 * so the session's draws are the model's alone */
static void kit_init(struct kit *k, long long seed, const double *pose)
{
    struct pyrand r;
    pr_seed_int(&r, seed * 1000003 + 7);
    memset(k, 0, sizeof *k);
    double x = pose[0], y = pose[1], z = pose[2], yaw = pose[3], pitch = pose[4];
    k->dim = (int)pose[5];
    k->yaw = yaw, k->pitch = pitch;
    for (int i = 0; i < NTOOLS; ++i) k->slot[i] = -1;
    /* no kit where its scene would leave the world */
    k->on = pr_random(&r) < 0.6 && 2 <= y && y <= 250;
    if (!k->on) return;
    static const struct {
        int w;
        const char *st;
    } ALL[] = {{3, "nether_portal"}, {4, "end_portal"}, {4, "trade"}, {3, "pig"}, {3, "lead"}, {1, "rail"}, {3, NULL}};
    int idx[7], n = 0, tot = 0;
    for (int i = 0; i < 7; ++i) {
        if (k->dim == 1 && ALL[i].st && !strcmp(ALL[i].st, "nether_portal")) continue;
        idx[n++] = i;
        tot += ALL[i].w;
    }
    double v = pr_random(&r) * tot;
    const char *st = NULL;
    for (int i = 0; i < n; ++i) {
        v -= ALL[idx[i]].w;
        st = ALL[idx[i]].st;
        if (v < 0) break;
    }
    k->stage = st;
    const char *need[2];
    int nneed = 0;
    if (st) {
        if (!strcmp(st, "nether_portal")) need[nneed++] = "flint";
        else if (!strcmp(st, "end_portal")) need[nneed++] = "eye";
        else if (!strcmp(st, "pig")) need[nneed++] = "saddle", need[nneed++] = "carrot_stick";
        else if (!strcmp(st, "lead")) need[nneed++] = "lead";
        else if (!strcmp(st, "rail")) need[nneed++] = "minecart", need[nneed++] = "boat";
    }
    const char *rest[NTOOLS];
    int nrest = 0;
    for (int i = 0; i < NTOOLS; ++i) {
        const char *t = TOOLS[i].name;
        int in = 0;
        for (int j = 0; j < nneed; ++j) in |= !strcmp(need[j], t);
        if (in || !strcmp(t, "emerald") || !strcmp(t, "carrot_stick") || !strcmp(t, "fence")) continue;
        rest[nrest++] = t;
    }
    pr_shuffle(&r, rest, nrest, sizeof *rest);
    const char *tools[NTOOLS + 2];
    int nt = 0;
    for (int j = 0; j < nneed; ++j) tools[nt++] = need[j];
    int more = (int)(3 + pr_random(&r) * 6) - nneed;
    if (more < 0) more = 0;
    for (int j = 0; j < more && j < nrest; ++j) tools[nt++] = rest[j];
    if (has_tool(tools, nt, "tnt") && !has_tool(tools, nt, "flint") && nt < 9) tools[nt++] = "flint";
    if (has_tool(tools, nt, "saddle") && !has_tool(tools, nt, "carrot_stick") && nt < 9) tools[nt++] = "carrot_stick";
    if (nt > 9) nt = 9;
    int hot[9];
    for (int i = 0; i < 9; ++i) hot[i] = i;
    pr_shuffle(&r, hot, 9, sizeof *hot);
    for (int i = 0; i < nt; ++i) {
        int ti = tool_index(tools[i]), s = hot[i];
        k->slot[ti] = s;
        int item = TOOLS[ti].item, count = TOOLS[ti].count;
        char *j;
        if (!strcmp(tools[i], "potion"))
            j = xasprintf("{\"op\":\"slot\",\"slot\":%d,\"item\":%d,\"count\":%d,\"meta\":%d}", s, item, count,
                          DRINK[(int)(pr_random(&r) * NDRINK)]);
        else if (!strcmp(tools[i], "splash"))
            j = xasprintf("{\"op\":\"slot\",\"slot\":%d,\"item\":%d,\"count\":%d,\"meta\":%d}", s, item, count,
                          SPLASH[(int)(pr_random(&r) * NSPLASH)]);
        else j = xasprintf("{\"op\":\"slot\",\"slot\":%d,\"item\":%d,\"count\":%d}", s, item, count);
        kit_add(k, 0, j, 1, s, item, count);
    }
    /* more potions and the trade goods in the inventory */
    int np = (int)(pr_random(&r) * 4);
    for (int i = 0; i < np; ++i) {
        int c = (int)(pr_random(&r) * (NDRINK + NSPLASH));
        int m = c < NDRINK ? DRINK[c] : SPLASH[c - NDRINK];
        kit_add(k, 0, xasprintf("{\"op\":\"slot\",\"slot\":%d,\"item\":373,\"count\":1,\"meta\":%d}", 27 + i, m), 1, 27 + i, 373, 1);
    }
    if ((st && !strcmp(st, "trade")) || pr_random(&r) < 0.2)
        for (int i = 0; i < 9; ++i) {
            k->inv_item[k->ninv] = GOODS[i][0], k->inv_slot[k->ninv++] = 9 + i;
            kit_add(k, 0, xasprintf("{\"op\":\"slot\",\"slot\":%d,\"item\":%d,\"count\":%d}", 9 + i, GOODS[i][0], GOODS[i][1]), 1,
                    9 + i, GOODS[i][0], GOODS[i][1]);
        }
    /* the scene, in front of the player at the block centre it is moved to */
    k->f = (int)((long long)floor(yaw * 4 / 360 + 0.5) & 3);
    k->px = (int)floor(x), k->pz = (int)floor(z);
    k->py = (int)ceil(y - 0.01);
    k->feet = fabs(y - rint(y)) < 0.01 ? (double)k->py : y;
    if (st) {
        kit_add(k, 1, xasprintf("{\"op\":\"tp\",\"x\":%s,\"y\":%s,\"z\":%s}", fmt_float(k->px + 0.5), fmt_float(k->feet),
                                fmt_float(k->pz + 0.5)),
                0, 0, 0, 0);
        kit_stage(k, &r);
    }
    for (int i = 0; i < k->nops; ++i) {
        char *w = xasprintf("{\"tick\":2,\"class\":\"Dev\",\"cmd\":%s}", k->ops[i].json);
        free(k->ops[i].json);
        k->ops[i].json = w;
    }
}

/* ------------------------------------------------------------ the model */

struct gen {
    struct pyrand main_r, *r;
    struct kit *kit;
    long long t, end;
    struct sv out;
    char *held[64]; /* keys ("w") and mouse buttons ("#1") the model holds */
    int nheld;
    double yaw, pitch;
    const struct pt *all_slots;
    int n_slots;
    struct pt slots_kit[211 + 5];
    struct sv acts;
};

static void gen_init(struct gen *g, long long seed, long long ticks, long long t0, struct kit *kit)
{
    memset(g, 0, sizeof *g);
    pr_seed_int(&g->main_r, seed);
    g->r = &g->main_r;
    g->kit = kit && kit->on ? kit : NULL;
    g->t = t0;
    g->end = t0 + ticks;
    g->yaw = pr_random(g->r) * 360 - 180;
    g->pitch = 0.0;
    g->all_slots = ALL_SLOTS;
    g->n_slots = n_all;
    if (g->kit) {
        memcpy(g->slots_kit, ALL_SLOTS, sizeof ALL_SLOTS);
        memcpy(g->slots_kit + n_all, MERCHANT_SLOTS, sizeof MERCHANT_SLOTS);
        g->all_slots = g->slots_kit;
        g->n_slots = n_all + 5;
    }
}

/* ---- draws */
static double u(struct gen *g, double a, double b) { return a + (b - a) * pr_random(g->r); }

/* an int in [a, b] */
static long long gi(struct gen *g, long long a, long long b) { return a + (long long)(pr_random(g->r) * (double)(b - a + 1)); }

static int p(struct gen *g, double x) { return pr_random(g->r) < x; }

static int pick_i(struct gen *g, int n) { return (int)(pr_random(g->r) * n); }

#define PICK(g, arr) ((arr)[pick_i((g), (int)(sizeof(arr) / sizeof *(arr)))])

/* weighted: the index of the drawn entry of ws */
static int weighted(struct gen *g, const int *ws, int n)
{
    int total = 0;
    for (int i = 0; i < n; ++i) total += ws[i];
    double x = pr_random(g->r) * total;
    for (int i = 0; i < n; ++i) {
        x -= ws[i];
        if (x < 0) return i;
    }
    return n - 1;
}

/* ---- events */
static struct sb *ev_begin(struct gen *g)
{
    if (g->t >= g->end) return NULL;
    struct sb *b = xcalloc(1, sizeof *b);
    sb_printf(b, "{\"t\":%lld", g->t);
    return b;
}

static void ev_end(struct gen *g, struct sb *b)
{
    sb_putc(b, '}');
    sv_push(&g->out, b->s);
    free(b);
}

static void gwait(struct gen *g, long long n) { g->t += n > 0 ? n : 0; }

static void held_set(struct gen *g, const char *k, int down)
{
    int i;
    for (i = 0; i < g->nheld; ++i)
        if (!strcmp(g->held[i], k)) break;
    if (down && i == g->nheld) g->held[g->nheld++] = xstrdup(k);
    else if (!down && i < g->nheld) {
        free(g->held[i]);
        g->held[i] = g->held[--g->nheld];
    }
}

static int held_has(struct gen *g, const char *k)
{
    for (int i = 0; i < g->nheld; ++i)
        if (!strcmp(g->held[i], k)) return 1;
    return 0;
}

static void key(struct gen *g, const char *k, int down)
{
    struct sb *b = ev_begin(g);
    if (b) {
        sb_printf(b, ",\"key\":\"%s\",\"down\":%d", k, down ? 1 : 0);
        ev_end(g, b);
    }
    held_set(g, k, down);
}

static void tap(struct gen *g, const char *k, long long hold)
{
    key(g, k, 1);
    gwait(g, hold);
    key(g, k, 0);
}

static void tap_n(struct gen *g, long long n, long long hold)
{
    char k[24];
    snprintf(k, sizeof k, "%lld", n);
    tap(g, k, hold);
}

static void mouse(struct gen *g, int btn, int down, int x, int y)
{
    struct sb *b = ev_begin(g);
    if (b) {
        sb_printf(b, ",\"mouse\":%d,\"x\":%d,\"y\":%d,\"down\":%d", btn, x, y, down ? 1 : 0);
        ev_end(g, b);
    }
    char k[8];
    snprintf(k, sizeof k, "#%d", btn);
    held_set(g, k, down);
}

static void mouse_c(struct gen *g, int btn, int down) { mouse(g, btn, down, SW / 2, SH / 2); }

static void click(struct gen *g, int btn, int x, int y, long long hold)
{
    mouse(g, btn, 1, x, y);
    gwait(g, hold);
    mouse(g, btn, 0, x, y);
}

static void click_c(struct gen *g, int btn, long long hold) { click(g, btn, SW / 2, SH / 2, hold); }

static void move(struct gen *g, double dx, double dy)
{
    long long ix = (long long)rint(dx), iy = (long long)rint(dy);
    if (ix || iy) {
        struct sb *b = ev_begin(g);
        if (b) {
            sb_printf(b, ",\"motion\":1,\"x\":%d,\"y\":%d,\"dx\":%lld,\"dy\":%lld", SW / 2, SH / 2, ix, iy);
            ev_end(g, b);
        }
    }
}

static void pointer(struct gen *g, int x, int y)
{
    struct sb *b = ev_begin(g);
    if (b) {
        sb_printf(b, ",\"motion\":1,\"x\":%d,\"y\":%d", x, y);
        ev_end(g, b);
    }
}

static void look(struct gen *g, double yaw, double pitch)
{
    g->yaw = yaw;
    g->pitch = py_max(-90.0, py_min(90.0, pitch));
    struct sb *b = ev_begin(g);
    if (b) {
        sb_printf(b, ",\"look\":[%s,%s]", fmt_float(py_round(g->yaw, 3)), fmt_float(py_round(g->pitch, 3)));
        ev_end(g, b);
    }
}

static void wheel(struct gen *g, int v)
{
    struct sb *b = ev_begin(g);
    if (b) {
        sb_printf(b, ",\"wheel\":%d", v);
        ev_end(g, b);
    }
}

/* the sort key of a held name: str() of a key or of a button's int */
static const char *held_str(const char *k) { return k[0] == '#' ? k + 1 : k; }

static int cmp_held(const void *a, const void *b) { return strcmp(held_str(*(char *const *)a), held_str(*(char *const *)b)); }

static void release_all(struct gen *g)
{
    int n = g->nheld;
    char **ks = xmalloc((size_t)(n ? n : 1) * sizeof *ks);
    for (int i = 0; i < n; ++i) ks[i] = xstrdup(g->held[i]);
    if (n > 1) qsort(ks, (size_t)n, sizeof *ks, cmp_held);
    for (int i = 0; i < n; ++i) {
        if (ks[i][0] == '#') mouse_c(g, atoi(ks[i] + 1), 0);
        else key(g, ks[i], 0);
        free(ks[i]);
    }
    free(ks);
}

/* n ticks with the mouse wandering */
static void drift(struct gen *g, long long n, double speed)
{
    double vx = u(g, -speed, speed);
    double vy = u(g, -speed / 3, speed / 3);
    for (long long i = 0; i < n; ++i) {
        if (p(g, 0.3)) {
            vx += u(g, -speed, speed) / 3;
            vy += u(g, -speed, speed) / 8;
            vx = py_max(-2 * speed, py_min(2 * speed, vx));
            vy = py_max(-speed, py_min(speed, vy));
        }
        if (p(g, 0.6)) move(g, vx, vy);
        gwait(g, 1);
    }
}

/* ---- episodes */
static void ep_walk(struct gen *g)
{
    static const int W[4] = {8, 1, 1, 1};
    static const char *const K[4] = {"w", "s", "a", "d"}, *const AD[2] = {"a", "d"}, *const ADS[3] = {"a", "d", "s"};
    const char *keys[2];
    int nk = 0;
    keys[nk++] = K[weighted(g, W, 4)];
    if (p(g, 0.3)) keys[nk++] = PICK(g, AD);
    long long n = gi(g, 10, 140);
    if (!strcmp(keys[0], "w") && p(g, 0.4)) {
        if (p(g, 0.5)) key(g, "ctrl", 1);
        else { /* double tap forward */
            long long h = gi(g, 0, 2);
            tap(g, "w", h);
            gwait(g, gi(g, 1, 4));
        }
    }
    for (int i = 0; i < nk; ++i) key(g, keys[i], 1);
    long long done = 0;
    while (done < n) {
        long long step = gi(g, 3, 20);
        double sp = u(g, 2, 40);
        drift(g, step, sp);
        done += step;
        if (p(g, 0.3)) tap(g, "space", gi(g, 0, 8));
        if (p(g, 0.05)) {
            const char *k = PICK(g, ADS);
            tap(g, k, gi(g, 2, 10));
        }
    }
    for (int i = 0; i < nk; ++i) key(g, keys[i], 0);
    if (held_has(g, "ctrl") && p(g, 0.7)) key(g, "ctrl", 0);
}

static void ep_jumps(struct gen *g)
{
    long long n = gi(g, 1, 8);
    for (long long i = 0; i < n; ++i) {
        if (p(g, 0.3)) {
            key(g, "space", 1);
            gwait(g, gi(g, 5, 40));
            key(g, "space", 0);
        } else tap(g, "space", gi(g, 0, 3));
        gwait(g, gi(g, 0, 12));
    }
}

static void ep_sneak(struct gen *g)
{
    static const char *const K[4] = {"w", "a", "s", "d"};
    static const int B[2] = {1, 2};
    key(g, "shift", 1);
    long long n = gi(g, 1, 4);
    for (long long i = 0; i < n; ++i) {
        const char *k = PICK(g, K);
        key(g, k, 1);
        long long d = gi(g, 5, 40);
        double s = u(g, 1, 15);
        drift(g, d, s);
        key(g, k, 0);
        if (p(g, 0.3)) {
            int b = PICK(g, B);
            click_c(g, b, gi(g, 0, 4));
        }
    }
    gwait(g, gi(g, 0, 20));
    key(g, "shift", 0);
}

static void ep_look_around(struct gen *g)
{
    double kind = pr_random(g->r);
    if (kind < 0.6) {
        long long d = gi(g, 5, 40);
        drift(g, d, u(g, 10, 150));
    } else if (kind < 0.85) {
        double a = g->yaw + u(g, -180, 180);
        look(g, a, u(g, -90, 90));
        gwait(g, gi(g, 1, 20));
    } else { /* spins: the yaw far from [-180, 180] */
        static const int S[2] = {-1, 1};
        double y0 = g->yaw;
        int s = PICK(g, S);
        double a = y0 + s * u(g, 360, 3600);
        look(g, a, u(g, -60, 60));
        gwait(g, gi(g, 1, 10));
    }
}

static void ep_mine(struct gen *g)
{
    if (p(g, 0.5)) {
        double a = g->yaw + u(g, -40, 40);
        look(g, a, u(g, -30, 70));
    }
    mouse_c(g, 1, 1);
    int walking = p(g, 0.3);
    if (walking) key(g, "w", 1);
    long long d = gi(g, 10, 200);
    drift(g, d, u(g, 0, 8));
    if (walking) key(g, "w", 0);
    mouse_c(g, 1, 0);
}

static void ep_fight(struct gen *g)
{
    static const char *const S[3] = {"a", "d", NULL};
    const char *strafe = PICK(g, S);
    if (strafe) key(g, strafe, 1);
    if (p(g, 0.5)) key(g, "w", 1);
    long long n = gi(g, 3, 20);
    for (long long i = 0; i < n; ++i) {
        if (p(g, 0.3)) {
            tap(g, "space", gi(g, 0, 2));
            gwait(g, gi(g, 3, 8));
        }
        click_c(g, 1, gi(g, 0, 2));
        long long d = gi(g, 1, 12);
        drift(g, d, u(g, 10, 80));
    }
    key(g, "w", 0);
    if (strafe) key(g, strafe, 0);
}

static void ep_use(struct gen *g)
{
    if (p(g, 0.4)) {
        double a = g->yaw + u(g, -60, 60);
        look(g, a, u(g, -20, 80));
    }
    if (p(g, 0.5)) {
        long long n = gi(g, 1, 5);
        for (long long i = 0; i < n; ++i) {
            click_c(g, 2, gi(g, 0, 3));
            long long d = gi(g, 1, 10);
            drift(g, d, u(g, 0, 20));
        }
    } else { /* eat, drink, draw a bow, place while held */
        mouse_c(g, 2, 1);
        int walk = p(g, 0.4);
        if (walk) key(g, "w", 1);
        long long d = gi(g, 8, 60);
        drift(g, d, u(g, 0, 30));
        if (walk) key(g, "w", 0);
        mouse_c(g, 2, 0);
    }
}

static void ep_hotbar(struct gen *g)
{
    if (p(g, 0.7)) {
        long long k = gi(g, 1, 9);
        tap_n(g, k, gi(g, 0, 2));
    } else {
        static const int W[8] = {-3, -2, -1, -1, 1, 1, 2, 3};
        wheel(g, PICK(g, W));
    }
    gwait(g, gi(g, 0, 5));
}

static void ep_tower(struct gen *g)
{
    ep_hotbar(g);
    look(g, g->yaw, u(g, 80, 90));
    long long n = gi(g, 2, 12);
    for (long long i = 0; i < n; ++i) {
        tap(g, "space", gi(g, 0, 2));
        gwait(g, gi(g, 3, 8));
        click_c(g, 2, gi(g, 0, 2));
        gwait(g, gi(g, 1, 10));
    }
}

static void ep_dig_down(struct gen *g)
{
    look(g, g->yaw, u(g, 75, 90));
    mouse_c(g, 1, 1);
    long long d = gi(g, 30, 250);
    drift(g, d, u(g, 0, 3));
    mouse_c(g, 1, 0);
}

static void ep_bridge(struct gen *g)
{
    ep_hotbar(g);
    look(g, g->yaw, u(g, 70, 85));
    key(g, "shift", 1);
    key(g, "s", 1);
    long long n = gi(g, 2, 10);
    for (long long i = 0; i < n; ++i) {
        gwait(g, gi(g, 3, 12));
        click_c(g, 2, gi(g, 0, 1));
    }
    key(g, "s", 0);
    if (p(g, 0.7)) key(g, "shift", 0);
}

/* the middle button: pick block over whatever the crosshair hits */
static void ep_pick_block(struct gen *g)
{
    if (p(g, 0.5)) {
        double a = g->yaw + u(g, -60, 60);
        look(g, a, u(g, -30, 80));
    }
    click_c(g, 3, gi(g, 0, 2));
    gwait(g, gi(g, 0, 10));
}

static void ep_drop(struct gen *g)
{
    long long n = gi(g, 1, 4);
    for (long long i = 0; i < n; ++i) {
        if (p(g, 0.3)) { /* ctrl held (the sprint key) over the drop */
            key(g, "ctrl", 1);
            tap(g, "q", 0);
            key(g, "ctrl", 0);
        } else tap(g, "q", gi(g, 0, 2));
        gwait(g, gi(g, 0, 8));
    }
}

static struct pt gslot(struct gen *g)
{
    double x = pr_random(g->r);
    if (x < 0.6) return PLAYER_SLOTS[pick_i(g, 45)];
    if (x < 0.85) return g->all_slots[pick_i(g, g->n_slots)];
    if (x < 0.95) {
        long long a = gi(g, (SW - 176) / 2, (SW + 176) / 2);
        long long b = gi(g, (SH - 222) / 2, (SH + 222) / 2);
        return (struct pt){(int)a, (int)b};
    }
    long long c[2];
    c[0] = gi(g, 0, 100);
    c[1] = gi(g, 330, SW - 1);
    long long a = c[pick_i(g, 2)];
    long long b = gi(g, 0, SH - 1);
    return (struct pt){(int)a, (int)b}; /* outside: a drop */
}

static void screen_actions(struct gen *g, long long n)
{
    static const int W[8] = {10, 5, 1, 4, 3, 2, 3, 2};
    enum { LEFT, RIGHT, MIDDLE, SHIFT, NUMBER, Q, DRAG, DOUBLE };
    for (long long i = 0; i < n; ++i) {
        struct pt s = gslot(g);
        int x = s.x, y = s.y;
        int kind = weighted(g, W, 8);
        pointer(g, x, y);
        if (kind <= MIDDLE) click(g, kind + 1, x, y, gi(g, 0, 3));
        else if (kind == SHIFT) {
            static const int B[3] = {1, 1, 2};
            key(g, "shift", 1);
            int b = PICK(g, B);
            click(g, b, x, y, gi(g, 0, 1));
            key(g, "shift", 0);
        } else if (kind == NUMBER) tap_n(g, gi(g, 1, 9), 0);
        else if (kind == Q) {
            if (p(g, 0.3)) {
                key(g, "ctrl", 1);
                tap(g, "q", 0);
                key(g, "ctrl", 0);
            } else tap(g, "q", 0);
        } else if (kind == DRAG) {
            static const int B[2] = {1, 2};
            int b = PICK(g, B);
            click(g, 1, x, y, 0); /* pick a stack up */
            gwait(g, gi(g, 0, 3));
            s = gslot(g);
            x = s.x, y = s.y;
            mouse(g, b, 1, x, y);
            long long m = gi(g, 1, 5);
            for (long long j = 0; j < m; ++j) {
                gwait(g, gi(g, 0, 2));
                s = p(g, 0.7) ? PLAYER_SLOTS[pick_i(g, 45)] : gslot(g);
                x = s.x, y = s.y;
                pointer(g, x, y);
            }
            gwait(g, gi(g, 0, 2));
            mouse(g, b, 0, x, y);
        } else { /* a double click, within 250 ms */
            click(g, 1, x, y, 0);
            gwait(g, gi(g, 0, 3));
            click(g, 1, x, y, 0);
        }
        gwait(g, gi(g, 0, 15));
    }
}

static void close_screen(struct gen *g)
{
    gwait(g, gi(g, 0, 10));
    if (p(g, 0.5)) tap(g, "e", 0);
    else {
        tap(g, "escape", 0);
        tap(g, "escape", 0); /* a pause is lifted in the same tick */
    }
    gwait(g, gi(g, 1, 5));
}

static void ep_inventory(struct gen *g)
{
    if (p(g, 0.5)) release_all(g);
    tap(g, "e", 0);
    gwait(g, gi(g, 2, 10));
    screen_actions(g, gi(g, 1, 15));
    close_screen(g);
}

/* right click what is in front, and work whatever screen opened */
static void ep_container(struct gen *g)
{
    double a = g->yaw + u(g, -30, 30);
    look(g, a, u(g, -10, 60));
    click_c(g, 2, 0);
    gwait(g, gi(g, 2, 8));
    screen_actions(g, gi(g, 1, 12));
    close_screen(g);
}

static void ep_pause(struct gen *g)
{
    tap(g, "escape", 0);
    if (p(g, 0.5)) tap(g, "escape", 0);
    else click(g, 1, SW / 2, SH / 4 + 34, 0); /* Back to Game */
    gwait(g, gi(g, 0, 5));
}

static void ep_respawn(struct gen *g)
{
    if (p(g, 0.5)) tap(g, "r", 0);
    else click(g, 1, SW / 2, SH / 4 + 82, 0);
    gwait(g, gi(g, 0, 30));
}

static void ep_sleep(struct gen *g)
{
    double a = g->yaw + u(g, -30, 30);
    look(g, a, u(g, 20, 70));
    click_c(g, 2, 0);
    gwait(g, gi(g, 20, 150));
    tap(g, "escape", 0);
    tap(g, "escape", 0);
    gwait(g, gi(g, 0, 10));
}

static void ep_idle(struct gen *g) { gwait(g, gi(g, 1, 40)); }

/* ---- the kit's actions */
static void glog(struct gen *g, const char *action)
{
    if (g->t < g->end) sv_push(&g->acts, xasprintf("%s\t%lld", action, g->t));
}

static int gselect(struct gen *g, const char *tool)
{
    int s = g->kit->slot[tool_index(tool)];
    if (s < 0) return 0;
    long long h = gi(g, 0, 1);
    tap_n(g, s + 1, h);
    gwait(g, gi(g, 1, 3));
    return 1;
}

/* look from the scene's standing point (the block centre the kit moved the player to) */
static void aim(struct gen *g, double tx, double ty, double tz)
{
    struct kit *k = g->kit;
    double dx = tx - (k->px + 0.5), dy = ty - (k->feet + 1.62), dz = tz - (k->pz + 0.5);
    double yaw = py_degrees(atan2(-dx, dz));
    yaw += (double)(360 * (long long)rint((g->yaw - yaw) / 360)); /* round() is an int: no -0.0 */
    look(g, yaw, -py_degrees(atan2(dy, py_hypot(dx, dz))));
}

static void aim_block(struct gen *g, int a, int l, int dy, double fy)
{
    int x, y, z;
    kit_at(g->kit, a, l, dy, &x, &y, &z);
    aim(g, x + 0.5, y + fy, z + 0.5);
}

static void aim_mob(struct gen *g, double h) { aim(g, g->kit->mob[0], g->kit->mob[1] + h, g->kit->mob[2]); }

static void st_nether_portal(struct gen *g)
{
    gselect(g, "flint");
    aim_block(g, 2, 0, -1, 1.0); /* the top of the frame's bottom, inside it */
    gwait(g, gi(g, 1, 3));
    glog(g, "nether_portal_light");
    click_c(g, 2, 0);
    gwait(g, gi(g, 2, 6));
    key(g, "w", 1);
    glog(g, "nether_portal");
    long long d = gi(g, 12, 25);
    drift(g, d, u(g, 0, 2));
    key(g, "w", 0);
    d = gi(g, 90, 200);
    drift(g, d, u(g, 0, 3)); /* 80 ticks inside, then the other side */
}

static void st_end_portal(struct gen *g)
{
    gselect(g, "eye");
    aim_block(g, 2, 0, 0, 0.8125); /* the near middle frame, the one without an eye */
    gwait(g, gi(g, 1, 3));
    glog(g, "end_portal_eye");
    click_c(g, 2, 0);
    gwait(g, gi(g, 3, 8));
    glog(g, "end_portal");
    key(g, "w", 1);
    long long n = gi(g, 2, 5);
    for (long long i = 0; i < n; ++i) {
        tap(g, "space", gi(g, 0, 3));
        gwait(g, gi(g, 4, 10));
    }
    key(g, "w", 0);
    gwait(g, gi(g, 20, 80));
}

static int cmp_int(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

static void merchant_actions(struct gen *g, long long n)
{
    int goods[9], ng = g->kit->ninv;
    memcpy(goods, g->kit->inv_slot, sizeof goods);
    qsort(goods, (size_t)ng, sizeof *goods, cmp_int);
    static const int W[4] = {4, 3, 2, 3};
    for (long long i = 0; i < n; ++i) {
        int kind = weighted(g, W, 4);
        if (kind == 0 && ng) {
            struct pt s = inv_point(goods[pick_i(g, ng)]);
            pointer(g, s.x, s.y);
            click(g, 1, s.x, s.y, 0);
            gwait(g, gi(g, 0, 3));
            s = MERCHANT_IN[pick_i(g, 2)];
            pointer(g, s.x, s.y);
            static const int B[3] = {1, 1, 2};
            click(g, PICK(g, B), s.x, s.y, 0);
        } else if (kind == 1) {
            struct pt s = MERCHANT_OUT;
            pointer(g, s.x, s.y);
            glog(g, "trade");
            if (p(g, 0.4)) {
                key(g, "shift", 1);
                click(g, 1, s.x, s.y, 0);
                key(g, "shift", 0);
            } else click(g, 1, s.x, s.y, 0);
        } else if (kind == 2) {
            struct pt s = pick_i(g, 2) ? MERCHANT_PREV : MERCHANT_NEXT;
            pointer(g, s.x, s.y);
            click(g, 1, s.x, s.y, 0);
        } else screen_actions(g, 1);
        gwait(g, gi(g, 0, 8));
    }
}

static void st_trade(struct gen *g)
{
    long long n = gi(g, 1, 2);
    for (long long i = 0; i < n; ++i) {
        aim_mob(g, u(g, 0.6, 1.4));
        gwait(g, gi(g, 0, 2));
        glog(g, "trade_open");
        click_c(g, 2, 0);
        gwait(g, gi(g, 3, 6));
        merchant_actions(g, gi(g, 2, 12));
        close_screen(g);
    }
}

static void st_pig(struct gen *g)
{
    gselect(g, "saddle");
    aim_mob(g, u(g, 0.3, 0.7));
    gwait(g, gi(g, 0, 2));
    glog(g, "pig_saddle");
    click_c(g, 2, 0);
    gwait(g, gi(g, 2, 5));
    gselect(g, "carrot_stick");
    aim_mob(g, u(g, 0.3, 0.7));
    glog(g, "pig_ride");
    click_c(g, 2, 0);
    gwait(g, gi(g, 2, 5));
    key(g, "w", 1);
    long long n = gi(g, 2, 8);
    for (long long i = 0; i < n; ++i) {
        long long d = gi(g, 10, 40);
        drift(g, d, u(g, 2, 30));
        if (p(g, 0.3)) click_c(g, 2, 0); /* the carrot's boost */
    }
    key(g, "w", 0);
    if (p(g, 0.5)) tap(g, "shift", gi(g, 0, 3)); /* get off */
}

static void st_lead(struct gen *g)
{
    gselect(g, "lead");
    aim_mob(g, u(g, 0.3, 0.8));
    gwait(g, gi(g, 0, 2));
    glog(g, "lead");
    click_c(g, 2, 0);
    gwait(g, gi(g, 2, 8));
    aim_block(g, -2, 0, 0, 0.5); /* the fence post behind */
    gwait(g, gi(g, 0, 3));
    glog(g, "lead_fence");
    click_c(g, 2, 0);
    gwait(g, gi(g, 5, 30));
    if (p(g, 0.5)) {
        aim_block(g, -2, 0, 0, 1.2); /* the knot on the post */
        static const int B[2] = {1, 2};
        click_c(g, PICK(g, B), 0);
        gwait(g, gi(g, 2, 10));
    }
}

static void st_rail(struct gen *g)
{
    static const char *const T[2] = {"minecart", "boat"};
    for (int i = 0; i < 2; ++i)
        if (gselect(g, T[i])) {
            aim_block(g, 2, 0, 0, 0.06);
            gwait(g, gi(g, 0, 2));
            glog(g, T[i]);
            click_c(g, 2, 0);
            gwait(g, gi(g, 2, 8));
        }
}

static void st_run(struct gen *g, const char *stage)
{
    if (!strcmp(stage, "nether_portal")) st_nether_portal(g);
    else if (!strcmp(stage, "end_portal")) st_end_portal(g);
    else if (!strcmp(stage, "trade")) st_trade(g);
    else if (!strcmp(stage, "pig")) st_pig(g);
    else if (!strcmp(stage, "lead")) st_lead(g);
    else if (!strcmp(stage, "rail")) st_rail(g);
}

static void throw_(struct gen *g, const char *tool, const char *action, int p0, int p1)
{
    if (!gselect(g, tool)) return;
    if (p(g, 0.7)) {
        double a = g->yaw + u(g, -90, 90);
        look(g, a, u(g, p0, p1));
    }
    gwait(g, gi(g, 0, 2));
    long long n = gi(g, 1, 3);
    for (long long i = 0; i < n; ++i) {
        glog(g, action);
        click_c(g, 2, gi(g, 0, 2));
        gwait(g, gi(g, 2, 15));
    }
    long long d = gi(g, 5, 60);
    drift(g, d, u(g, 0, 20));
}

static void k_pearl(struct gen *g) { throw_(g, "pearl", "pearl", -60, 40); }
static void k_eye(struct gen *g) { throw_(g, "eye", "eye", -60, 30); }
static void k_splash(struct gen *g) { throw_(g, "splash", "potion_throw", -30, 90); }
static void k_fish(struct gen *g) { throw_(g, "fishing_rod", "fishing_rod", -20, 60); }
static void k_boat(struct gen *g) { throw_(g, "boat", "boat", 20, 80); }
static void k_minecart(struct gen *g) { throw_(g, "minecart", "minecart", 20, 80); }

static void k_drink(struct gen *g)
{
    if (!gselect(g, "potion")) return;
    glog(g, "potion_drink");
    mouse_c(g, 2, 1);
    long long d = p(g, 0.8) ? gi(g, 33, 45) : gi(g, 5, 30);
    drift(g, d, u(g, 0, 10));
    mouse_c(g, 2, 0);
    gwait(g, gi(g, 0, 10));
}

static void k_tnt(struct gen *g)
{
    if (!gselect(g, "tnt")) return;
    double a = g->yaw + u(g, -40, 40);
    look(g, a, u(g, 40, 80));
    click_c(g, 2, 0);
    gwait(g, gi(g, 1, 4));
    if (gselect(g, "flint")) {
        glog(g, "tnt");
        click_c(g, 2, 0);
        gwait(g, gi(g, 0, 3));
    }
    key(g, "s", 1);
    long long d = gi(g, 20, 90);
    drift(g, d, u(g, 0, 5));
    key(g, "s", 0);
}

static void k_table(struct gen *g)
{
    static const char *const T[3] = {"enchanting_table", "anvil", "brewing_stand"};
    const char *have[3];
    int nh = 0;
    for (int i = 0; i < 3; ++i)
        if (g->kit->slot[tool_index(T[i])] >= 0) have[nh++] = T[i];
    if (!nh) return;
    const char *tool = have[pick_i(g, nh)];
    gselect(g, tool);
    double a = g->yaw + u(g, -40, 40);
    look(g, a, u(g, 30, 80));
    click_c(g, 2, 0);
    glog(g, tool);
    gwait(g, gi(g, 2, 8));
    long long n = gi(g, 1, 3);
    for (long long i = 0; i < n; ++i) {
        click_c(g, 2, gi(g, 0, 2)); /* use it: a plain block (config.yaml), so the held item acts on it */
        gwait(g, gi(g, 2, 10));
    }
}

typedef void (*episode)(struct gen *);
static const int EP_W[] = {14, 5, 4, 8, 8, 6, 8, 3, 2, 2, 6, 2, 2, 5, 3, 1, 3, 1, 4, /* the kit's: */ 3, 2, 2, 2, 1, 1, 1, 2, 3};
static const episode EP_F[] = {ep_walk, ep_jumps, ep_sneak, ep_look_around, ep_mine, ep_fight, ep_use, ep_tower,
                               ep_dig_down, ep_bridge, ep_hotbar, ep_pick_block, ep_drop, ep_inventory, ep_container,
                               ep_pause, ep_respawn, ep_sleep, ep_idle, k_pearl, k_eye, k_drink, k_splash, k_fish,
                               k_boat, k_minecart, k_tnt, k_table};
enum { N_EPISODES = 19, N_KIT_EPISODES = 9 };

/* one weighted episode of the table (with the kit's when there is a kit) */
static void episode_step(struct gen *g)
{
    int n = N_EPISODES + (g->kit ? N_KIT_EPISODES : 0);
    EP_F[weighted(g, EP_W, n)](g);
}

static void quit_row(struct gen *g, long long t) { sv_push(&g->out, xasprintf("{\"t\":%lld,\"quit\":1}", t)); }

__attribute__((unused)) static void gen_run(struct gen *g)
{
    gwait(g, gi(g, 0, 5));
    if (g->kit) {
        /* the kit's slot ops land at tick 2 and reach the client two ticks later */
        if (g->t < 5) g->t = 5;
        g->yaw = g->kit->yaw, g->pitch = g->kit->pitch;
        if (g->kit->stage) st_run(g, g->kit->stage);
    }
    while (g->t < g->end) {
        episode_step(g);
        if (p(g, 0.1)) release_all(g);
    }
    /* quit: two sessions in three leave what they held down (a quit mid-action) */
    if (p(g, 1.0 / 3)) {
        g->t = g->end - 1;
        release_all(g);
    }
    g->t = g->end;
    quit_row(g, g->end);
}

static void write_rows(const struct sv *rows)
{
    for (int i = 0; i < rows->n; ++i) {
        fputs(rows->v[i], stdout);
        fputc('\n', stdout);
    }
}

static int write_kit(const char *path, const struct gen *g, const struct kit *kit)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "cannot write %s\n", path);
        return 1;
    }
    if (g->kit)
        for (int i = 0; i < kit->nops; ++i) fprintf(f, "%s\n", kit->ops[i].json);
    fclose(f);
    return 0;
}

/* the session's default length: 200 to 2,000 ticks, most short */
static long long default_ticks(long long seed)
{
    struct pyrand r;
    pr_seed_int(&r, seed * 7919 + 1);
    return 200 + (long long)(1800 * pow(pr_random(&r), 3));
}

/* "X Y Z YAW PITCH DIM" */
static int parse_pose(const char *s, double *pose)
{
    char *t = xstrdup(s), *f[16];
    int n = split_ws(t, f, 16);
    for (int i = 0; i < n && i < 6; ++i) pose[i] = strtod(f[i], NULL);
    free(t);
    if (n && n != 6) die("--pose wants X Y Z YAW PITCH DIM");
    return n;
}

#endif
